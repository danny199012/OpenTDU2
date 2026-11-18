#include "shared.h"
#include "sticker_bank.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "database/gs_database.h"
#include "system/cmd_line.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "render/texture_cache.h"
#include "render/vulkan/render_device.h"
#include "render/2dm.h"
#include "render/image_file.h"
#include "core/path_utf8.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

// The canvas resolution: the sticker art ships 512², so 1024 keeps a placed
// sticker sharp while the whole body UV space stays one target.
static constexpr uint32_t kCanvasSize = 1024;

// ---- StickerCanvas ----

StickerCanvas::~StickerCanvas() {
    destroy();
}

void StickerCanvas::setLayout( const std::vector<StickerPlacement> &newLayout, const std::vector<GPUTexture *> &art ) {
    layout = newLayout;
    artTextures = art;
    bDirty = true;
}

bool StickerCanvas::ensureTarget( RenderDevice &device ) {
    if ( pTarget != nullptr ) {
        return true;
    }
    // The swapchain's format (VK_FORMAT_UNDEFINED resolves to it): the 2D
    // pipeline is created against mainRenderPass, and a render target on
    // another format would make the passes incompatible (GpsScreen's note).
    pTarget = device.createRenderTarget( kCanvasSize, kCanvasSize, VK_FORMAT_UNDEFINED, false, "CarStickers" );
    return pTarget != nullptr;
}

GPUTexture *StickerCanvas::getTexture() {
    return pTarget != nullptr && pTarget->bInitialized ? &pTarget->Color : nullptr;
}

void StickerCanvas::render( RenderDevice &device, Renderer2D &renderer ) {
    if ( !bDirty ) {
        return;
    }
    if ( layout.empty() && pTarget == nullptr ) {
        bDirty = false; // nothing was ever drawn: nothing to clear
        return;
    }
    if ( !ensureTarget( device ) ) {
        return;
    }
    bDirty = false;
    device.beginRenderTarget( pTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 0.0f ) );
    renderer.begin( static_cast<float>( kCanvasSize ), static_cast<float>( kCanvasSize ), false );
    for ( size_t i = 0; i < layout.size() && i < artTextures.size(); i++ ) {
        const StickerPlacement &p = layout[i];
        GPUTexture *pArt = artTextures[i];
        if ( pArt == nullptr || pArt->Width == 0 || pArt->Height == 0 ) {
            continue;
        }
        // Centre + size in canvas pixels; the rotation in the canvas plane.
        const float cx = p.U * kCanvasSize, cy = p.V * kCanvasSize;
        const float width = Max( 8.0f, p.Scale * kCanvasSize );
        const float height = width * static_cast<float>( pArt->Height ) / static_cast<float>( pArt->Width ) * p.YStretch;
        const float angle = p.AngleDeg * kDegToRad;
        const float ca = std::cos( angle ), sa = std::sin( angle );
        const float hx = width * 0.5f, hy = height * 0.5f;
        const Vector2f corners[4] = { Vector2f( -hx, -hy ), Vector2f( hx, -hy ), Vector2f( hx, hy ),
                                      Vector2f( -hx, hy ) };
        const float u0 = p.bFlip ? 1.0f : 0.0f, u1 = p.bFlip ? 0.0f : 1.0f;
        const Vector2f uvs[4] = { Vector2f( u0, 0.0f ), Vector2f( u1, 0.0f ), Vector2f( u1, 1.0f ),
                                  Vector2f( u0, 1.0f ) };
        // The transparency tool scales the tint's alpha (MakeRGBA order: A in the top byte).
        uint32_t tint = p.Tint;
        if ( p.Alpha < 0.999f ) {
            const uint32_t a = static_cast<uint32_t>( ( tint >> 24 ) * Clamp( p.Alpha, 0.0f, 1.0f ) + 0.5f );
            tint = ( tint & 0x00ffffffu ) | ( a << 24 );
        }
        static const int32_t kOrder[6] = { 0, 1, 2, 0, 2, 3 };
        Vertex2D verts[6];
        for ( int32_t v = 0; v < 6; v++ ) {
            const int32_t c = kOrder[v];
            verts[v].X = cx + corners[c].x * ca - corners[c].y * sa;
            verts[v].Y = cy + corners[c].x * sa + corners[c].y * ca;
            verts[v].U = uvs[c].x;
            verts[v].V = uvs[c].y;
            verts[v].Color = tint;
        }
        renderer.drawTriangles( pArt, verts, 6 );
    }
    renderer.end();
    device.endRenderTarget();
}

void StickerCanvas::destroy() {
    if ( pTarget != nullptr && gpRender != nullptr ) {
        gpRender->getDevice().destroyRenderTarget( pTarget );
        pTarget = nullptr;
    }
    layout.clear();
    artTextures.clear();
    bDirty = false;
}

// ---- StickerBankCatalog ----

// DBStickersBanks -> the pack banks. -nostickers turns the feature off for
// the whole run (the menu hides the page, spawn application is skipped).
bool StickerBankCatalog::isEnabled() {
    return !CmdLine::HasArg( "-nostickers" );
}

StickerBankCatalog &StickerBankCatalog::get() {
    static StickerBankCatalog catalog;
    return catalog;
}

bool StickerBankCatalog::isBankSold( size_t index ) const {
    if ( index >= banks.size() ) {
        return false;
    }
    // The shop packs (type 1); the shipped-but-off rows (_isvalid 0) and the
    // event-only specials (4 casino, 5 chase, $0) stay out of the catalogue.
    const StickerBankInfo &info = banks[index];
    return info.bValid && info.Type != 4 && info.Type != 5;
}

void StickerBankCatalog::load() {
    if ( bLoaded || gpDatabase == nullptr ) {
        return;
    }
    bLoaded = true;
    static const bool bLog = CmdLine::HasArg( "-logstickers" );
    XmbValue root = gpDatabase->getDocument().getRoot();
    const uint32_t rows = root.getArraySize( "DBStickersBanks" );
    banks.reserve( rows );
    loaded.resize( rows );
    for ( uint32_t i = 0; i < rows; i++ ) {
        XmbValue row = root.getArrayElement( "DBStickersBanks", i );
        StickerBankInfo info;
        info.BankFile = row["_bankfile"].asString();
        if ( const char *pName = gpDatabase->getString( static_cast<int32_t>( row["_name_hc"].asUInt64() ) ) ) {
            info.Name = pName;
        }
        info.Type = row["_type"].asInt();
        info.Price = row["_price"].asInt();
        info.bMonochrome = row["_monochrome"].asBool();
        info.bValid = row["_isvalid"].asBool();
        if ( bLog ) {
            OTDU_LOG_INFO( "Stickers: bank %u '%s' name '%s' type %d price $%d mono %d valid %d\n", i,
                           info.BankFile.c_str(), info.Name.c_str(), info.Type, info.Price,
                           info.bMonochrome ? 1 : 0, info.bValid ? 1 : 0 );
        }
        banks.push_back( std::move( info ) );
    }
    scanModPacks();
    loaded.resize( banks.size() );
}

namespace {
std::string Trim( const std::string &s ) {
    const size_t a = s.find_first_not_of( " \t\r\n" );
    if ( a == std::string::npos ) {
        return std::string();
    }
    return s.substr( a, s.find_last_not_of( " \t\r\n" ) - a + 1 );
}

// pack.ini: 'key = value' lines, '#' / ';' comments. Keys: name, price,
// mono (0/1), vinyl (0/1), cars (comma list of _id_car).
struct PackIni {
    std::map<std::string, std::string> values;
};

PackIni ReadPackIni( const std::filesystem::path &path ) {
    PackIni ini;
    std::ifstream in( path );
    std::string line;
    while ( std::getline( in, line ) ) {
        line = Trim( line );
        if ( line.empty() || line[0] == '#' || line[0] == ';' ) {
            continue;
        }
        const size_t eq = line.find( '=' );
        if ( eq == std::string::npos ) {
            continue;
        }
        const std::string key = ToLower( Trim( line.substr( 0, eq ) ) ), value = Trim( line.substr( eq + 1 ) );
        if ( key == "name" || key == "price" || key == "mono" || key == "vinyl" || key == "cars" ) {
            ini.values[key] = value;
        }
    }
    return ini;
}
} // namespace

// <mods>/stickers/<id>/pack.ini + *.png -> extra banks after the retail rows
// (BankFile 'mod_<id>', type 1, valid). Sticker packs and full-car vinyl
// packs share the format; the shop UI sees them as ordinary banks.
void StickerBankCatalog::scanModPacks() {
    if ( gpFile == nullptr || CmdLine::HasArg( "-nomodstickers" ) ) {
        return;
    }
    const std::filesystem::path root = gpFile->getModsRoot() / "stickers";
    std::error_code error;
    if ( !std::filesystem::is_directory( root, error ) ) {
        return;
    }
    std::vector<std::filesystem::path> folders;
    for ( const auto &item : std::filesystem::directory_iterator( root, error ) ) {
        if ( item.is_directory( error ) && std::filesystem::is_regular_file( item.path() / "pack.ini", error ) ) {
            folders.push_back( item.path() );
        }
    }
    std::sort( folders.begin(), folders.end() );
    for ( const std::filesystem::path &folder : folders ) {
        const PackIni ini = ReadPackIni( folder / "pack.ini" );
        StickerBankInfo info;
        const std::string id = PathToUtf8( folder.filename() );
        info.BankFile = "mod_" + ToLower( id );
        auto get = [&]( const char *key, const char *fallback ) {
            const auto it = ini.values.find( key );
            return it != ini.values.end() ? it->second : std::string( fallback );
        };
        info.Name = get( "name", id.c_str() );
        info.Price = std::atoi( get( "price", "0" ).c_str() );
        info.bMonochrome = get( "mono", "0" ) == "1";
        info.bVinyl = get( "vinyl", "0" ) == "1";
        for ( size_t start = 0, n = 0; start < get( "cars", "" ).size(); start = n + 1 ) {
            const std::string list = get( "cars", "" );
            n = list.find( ',', start );
            if ( n == std::string::npos ) {
                n = list.size();
            }
            const std::string token = Trim( list.substr( start, n - start ) );
            if ( !token.empty() ) {
                info.Cars.push_back( std::atoi( token.c_str() ) );
            }
        }
        info.Type = 1;
        info.bValid = true;
        info.bMod = true;
        info.ModFolder = PathToUtf8( folder );
        OTDU_LOG_INFO( "Stickers: mod pack '%s' (%s) '%s' price %d%s\n", info.BankFile.c_str(), id.c_str(),
                       info.Name.c_str(), info.Price, info.bVinyl ? " [vinyl]" : "" );
        banks.push_back( std::move( info ) );
    }
}

bool StickerBankCatalog::bankFitsCar( size_t index, int32_t carId ) const {
    if ( index >= banks.size() || banks[index].Cars.empty() ) {
        return true;
    }
    return std::find( banks[index].Cars.begin(), banks[index].Cars.end(), carId ) != banks[index].Cars.end();
}

const std::vector<StickerEntry> *StickerBankCatalog::openModBank( size_t index ) {
    LoadedBank &bank = loaded[index];
    const StickerBankInfo &info = banks[index];
    const std::filesystem::path folder = PathFromUtf8( info.ModFolder );
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for ( const auto &item : std::filesystem::directory_iterator( folder, error ) ) {
        if ( item.is_regular_file( error ) && ToLower( PathToUtf8( item.path().extension() ) ) == ".png" ) {
            files.push_back( item.path() );
        }
    }
    std::sort( files.begin(), files.end() ); // file order = sticker order (stable placements)
    for ( const std::filesystem::path &file : files ) {
        std::vector<uint8_t> pixels;
        uint32_t width = 0, height = 0;
        if ( !LoadImageFileRGBA( file, pixels, width, height ) || width == 0 || height == 0 ) {
            OTDU_LOG_WARN( "Stickers: '%s' not loaded\n", PathToUtf8( file ).c_str() );
            continue;
        }
        StickerEntry entry;
        entry.Name = PathToUtf8( file.stem() );
        entry.pArt =
            gpRender->getTextureCache().createFromRGBA( width, height, pixels.data(), PathToUtf8( file ).c_str() );
        if ( entry.pArt == nullptr ) {
            continue;
        }
        bank.ownedTextures.push_back( entry.pArt );
        entry.Width = static_cast<int32_t>( width );
        entry.Height = static_cast<int32_t>( height );
        bank.entries.push_back( std::move( entry ) );
    }
    bank.bModLoaded = true;
    OTDU_LOG_INFO( "Stickers: mod pack '%s': %zu image(s)\n", info.BankFile.c_str(), bank.entries.size() );
    return &bank.entries;
}

const std::vector<StickerEntry> *StickerBankCatalog::openBank( size_t index ) {
    if ( index >= banks.size() || banks[index].BankFile.empty() ) {
        return nullptr;
    }
    LoadedBank &bank = loaded[index];
    if ( bank.bFailed ) {
        return nullptr;
    }
    if ( banks[index].bMod ) {
        return bank.bModLoaded ? &bank.entries : openModBank( index );
    }
    if ( bank.pBank != nullptr ) {
        return &bank.entries;
    }
    static const bool bLog = CmdLine::HasArg( "-logstickers" );
    // The packs sit beside the other scenarics packs (the bank list of the
    // retail install: Euro/Bnk/scenarics/packs/stickers/<_bankfile>.bnk).
    const std::string path = gpFile->getBankFolder() + "scenarics/packs/stickers/" + banks[index].BankFile + ".bnk";
    bank.pBank = new Bank();
    if ( !bank.pBank->load( path.c_str() ) ) {
        OTDU_LOG_WARN( "Stickers: bank '%s' failed to load\n", path.c_str() );
        delete bank.pBank;
        bank.pBank = nullptr;
        bank.bFailed = true;
        return nullptr;
    }
    // The art + icon textures into the cache (owner "stickers/<bankfile>",
    // released by shutdown like the other borrowed banks).
    const std::string owner = "stickers/" + ToLower( banks[index].BankFile );
    TextureCache &textures = gpRender->getTextureCache();
    const uint32_t numTextures = textures.loadBankTextures( *bank.pBank, owner.c_str() );
    // The material library: one MAT_%02d per sticker, the art on COLOR, the
    // shop icon on IC_NB (the .2dm of the bank, file order = sticker order).
    Render2DM library;
    for ( const BankEntry &entry : bank.pBank->getEntries() ) {
        if ( entry.Extension == "2dm" && library.parse( entry.pData, entry.Size ) ) {
            break;
        }
    }
    for ( const MaterialDesc &material : library.getOrderedMaterials() ) {
        StickerEntry entry;
        entry.Name = material.Name;
        const uint64_t artHash = material.findTexture( "COLOR" );
        const uint64_t iconHash = material.findTexture( "IC_NB" );
        if ( artHash != 0 ) {
            entry.pArt = textures.find( owner.c_str(), artHash );
        }
        if ( iconHash != 0 ) {
            entry.pIcon = textures.find( owner.c_str(), iconHash );
        }
        if ( entry.pArt != nullptr ) {
            entry.Width = static_cast<int32_t>( entry.pArt->Width );
            entry.Height = static_cast<int32_t>( entry.pArt->Height );
        }
        if ( bLog ) {
            OTDU_LOG_INFO( "Stickers:   '%s/%s': art %016llx (%s), icon %016llx (%s)\n",
                           banks[index].BankFile.c_str(), entry.Name.c_str(),
                           static_cast<unsigned long long>( artHash ), entry.pArt != nullptr ? "found" : "MISSING",
                           static_cast<unsigned long long>( iconHash ), entry.pIcon != nullptr ? "found" : "none" );
        }
        bank.entries.push_back( std::move( entry ) );
    }
    if ( bLog ) {
        OTDU_LOG_INFO( "Stickers: bank '%s': %u texture(s), %zu sticker material(s)\n", banks[index].BankFile.c_str(),
                       numTextures, bank.entries.size() );
    }
    return &bank.entries;
}

void StickerBankCatalog::shutdown() {
    if ( gpRender != nullptr ) {
        TextureCache &textures = gpRender->getTextureCache();
        for ( size_t i = 0; i < loaded.size() && i < banks.size(); i++ ) {
            if ( loaded[i].pBank != nullptr ) {
                textures.releaseOwner( ( "stickers/" + ToLower( banks[i].BankFile ) ).c_str() );
            }
        }
    }
    for ( LoadedBank &bank : loaded ) {
        if ( gpRender != nullptr ) {
            for ( GPUTexture *pTexture : bank.ownedTextures ) {
                gpRender->getDevice().destroyTexture( pTexture );
            }
        }
        bank.ownedTextures.clear();
        bank.bModLoaded = false;
        delete bank.pBank;
        bank.pBank = nullptr;
        bank.entries.clear();
    }
    banks.clear();
    bLoaded = false;
}

GPUTexture *StickerArt( const StickerPlacement &placement ) {
    if ( !StickerBankCatalog::isEnabled() ) {
        return nullptr;
    }
    StickerBankCatalog &catalog = StickerBankCatalog::get();
    catalog.load();
    for ( size_t i = 0; i < catalog.getBankCount(); i++ ) {
        if ( StringCmpI( catalog.getBank( i ).BankFile.c_str(), placement.Bank.c_str() ) != 0 ) {
            continue;
        }
        const std::vector<StickerEntry> *pEntries = catalog.openBank( i );
        if ( pEntries == nullptr || placement.Index < 0 || placement.Index >= static_cast<int32_t>( pEntries->size() ) ) {
            return nullptr;
        }
        return ( *pEntries )[static_cast<size_t>( placement.Index )].pArt;
    }
    return nullptr;
}

void StickerResolveArt( const std::vector<StickerPlacement> &layout, std::vector<GPUTexture *> &outArt ) {
    outArt.clear();
    outArt.reserve( layout.size() );
    for ( const StickerPlacement &placement : layout ) {
        outArt.push_back( StickerArt( placement ) );
    }
}
