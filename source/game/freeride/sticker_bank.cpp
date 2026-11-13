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
    if ( !bDirty || layout.empty() ) {
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
        const float height = width * static_cast<float>( pArt->Height ) / static_cast<float>( pArt->Width );
        const float angle = p.AngleDeg * kDegToRad;
        const float ca = std::cos( angle ), sa = std::sin( angle );
        const float hx = width * 0.5f, hy = height * 0.5f;
        const Vector2f corners[4] = { Vector2f( -hx, -hy ), Vector2f( hx, -hy ), Vector2f( hx, hy ),
                                      Vector2f( -hx, hy ) };
        const Vector2f uvs[4] = { Vector2f( 0.0f, 0.0f ), Vector2f( 1.0f, 0.0f ), Vector2f( 1.0f, 1.0f ),
                                  Vector2f( 0.0f, 1.0f ) };
        static const int32_t kOrder[6] = { 0, 1, 2, 0, 2, 3 };
        Vertex2D verts[6];
        for ( int32_t v = 0; v < 6; v++ ) {
            const int32_t c = kOrder[v];
            verts[v].X = cx + corners[c].x * ca - corners[c].y * sa;
            verts[v].Y = cy + corners[c].x * sa + corners[c].y * ca;
            verts[v].U = uvs[c].x;
            verts[v].V = uvs[c].y;
            verts[v].Color = p.Tint;
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
}

const std::vector<StickerEntry> *StickerBankCatalog::openBank( size_t index ) {
    if ( index >= banks.size() || banks[index].BankFile.empty() ) {
        return nullptr;
    }
    LoadedBank &bank = loaded[index];
    if ( bank.bFailed ) {
        return nullptr;
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
