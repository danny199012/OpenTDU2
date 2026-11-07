#include "shared.h"
#include "flash_movie.h"

#include "filesystem/gs_file.h"
#include "render/2db.h"
#include "render/image_file.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

#include "stb_image_write.h"

namespace {
std::string GetFlashTextureName( uint32_t characterId ) {
    char name[32];
    snprintf( name, sizeof( name ), "fla_%04u", characterId );
    return name;
}
} // namespace

FlashMovie::FlashMovie() : bLoaded( false ) {}

FlashMovie::~FlashMovie() { unload(); }

bool FlashMovie::load( const char *pName, const char *pResolution ) {
    unload();
    name = pName;

    std::string path = gpFile->getBankFolder() + "FrontEnd/" + pResolution + "/" + pName + ".bnk";
    if ( !gpFile->fileExists( path.c_str() ) ) {
        path = gpFile->getBankFolder() + "FrontEnd/" + pName + ".bnk";
    }
    if ( !bank.load( path.c_str() ) ) {
        return false;
    }
    owner = path;

    gpRender->getTextureCache().loadBankTextures( bank, owner.c_str() );

    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( entry.Extension == "2db" && StringNCmpI( entry.Name.c_str(), "page_", 5 ) == 0 ) {
            GPUTexture *pTexture = gpRender->getTextureCache().find( owner.c_str(), entry.Name.c_str() );
            if ( pTexture != nullptr ) {
                pages.emplace_back( ToLower( entry.Name ), pTexture );
            }
        }
    }

    const std::vector<const BankEntry *> movies = bank.findEntriesByExtension( "vmf" );
    if ( movies.empty() || !vmf.load( movies[0]->pData, movies[0]->Size ) ) {
        OTDU_LOG_WARN( "Bank '%s' has no valid flash movie\n", path.c_str() );
    } else {
        OTDU_LOG_INFO( "Flash movie '%s': %.0fx%.0f, %u frames @ %.0f fps, %zu tags, %zu bitmaps, "
                       "%zu fonts\n",
                       pName, vmf.getStageWidth(), vmf.getStageHeight(), vmf.getFrameCount(),
                       vmf.getFrameRate(), vmf.getTags().size(), vmf.getBitmaps().size(),
                       vmf.getFonts().size() );
        createFonts();
        // -dumpflashart <all|bank,bank...>: every bitmap character of each
        // loaded movie as _flashart/<movie>/<id>.png (standalones and atlas
        // cells, the engine's own decode) - the UI art inspector, for finding
        // which character a retail capture's icon is. GSFlash::initialize
        // loads the named banks (or every frontend bank for 'all').
        if ( CmdLine::HasArg( "-dumpflashart" ) ) {
            {
                const std::filesystem::path folder = std::filesystem::path( "_flashart" ) / pName;
                std::error_code error;
                std::filesystem::create_directories( folder, error );
                uint32_t count = 0;
                for ( const FlashBitmapDesc &desc : vmf.getBitmaps() ) {
                    std::vector<uint8_t> rgba;
                    uint32_t w = 0, h = 0;
                    if ( !getBitmapPixels( desc.CharacterId, rgba, w, h ) &&
                         !getAtlasCellPixels( desc.CharacterId, rgba, w, h ) ) {
                        continue;
                    }
                    const std::string file = ( folder / ( std::to_string( desc.CharacterId ) + ".png" ) ).string();
                    stbi_write_png( file.c_str(), static_cast<int>( w ), static_cast<int>( h ), 4, rgba.data(),
                                    static_cast<int>( w * 4 ) );
                    count++;
                }
                OTDU_LOG_INFO( "Flash movie '%s': %u bitmaps dumped to %s\n", pName, count,
                               folder.string().c_str() );
            }
        }
    }

    bLoaded = true;
    return true;
}

GPUTexture *FlashMovie::findPage( const char *pName ) const {
    const std::string lower = ToLower( pName );
    for ( const auto &page : pages ) {
        if ( page.first == lower || page.first == lower + ".2db" ) {
            return page.second;
        }
    }
    return nullptr;
}

bool FlashMovie::addExtraPages( const char *pBankPath ) {
    const std::string path = gpFile->getBankFolder() + pBankPath;
    for ( const std::string &existing : extraOwners ) {
        if ( StringCmpI( existing.c_str(), path.c_str() ) == 0 ) {
            return true;
        }
    }
    auto pBank = std::make_unique<Bank>();
    if ( !pBank->load( path.c_str() ) ) {
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( *pBank, path.c_str() );
    for ( const BankEntry &entry : pBank->getEntries() ) {
        if ( entry.Extension != "2db" ) {
            continue;
        }
        if ( GPUTexture *pTexture = gpRender->getTextureCache().find( path.c_str(), entry.Name.c_str() ) ) {
            pages.emplace_back( ToLower( entry.Name ), pTexture );
        }
    }
    extraOwners.push_back( path );
    extraBanks.push_back( std::move( pBank ) );
    return true;
}

void FlashMovie::unload() {
    if ( !owner.empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( owner.c_str() );
    }
    if ( gpRender != nullptr ) {
        for ( const std::string &extra : extraOwners ) {
            gpRender->getTextureCache().releaseOwner( extra.c_str() );
        }
    }
    extraOwners.clear();
    extraBanks.clear();
    owner.clear();
    fonts.clear();
    pages.clear();
    // The mods flash-art overrides are owned here (createFromRGBA registers
    // no owner).
    if ( gpRender != nullptr ) {
        for ( auto &entry : modOverrides ) {
            if ( entry.second.pTexture != nullptr ) {
                gpRender->getDevice().destroyTexture( entry.second.pTexture );
            }
        }
    }
    modOverrides.clear();
    bank.unload();
    bLoaded = false;
}

GPUTexture *FlashMovie::findTexture( const std::string &textureName ) const {
    if ( bank.findEntry( textureName.c_str(), "2db" ) == nullptr ) {
        return nullptr;
    }
    return gpRender->getTextureCache().find( owner.c_str(), textureName.c_str() );
}

void FlashMovie::createFonts() {
    for ( const FlashFontDesc &desc : vmf.getFonts() ) {
        FontEntry entry;
        entry.Name = desc.Name;

        // The textured glyph table maps to fla_<CharacterId>; TDU2 renders the
        // bold variant with the same atlas (the secondary padded atlas is a glow
        // rendition used by some animated texts).
        GPUTexture *pAtlas = findTexture( GetFlashTextureName( desc.CharacterId ) );
        if ( pAtlas != nullptr && !desc.Glyphs.empty() ) {
            auto pFont = std::make_unique<Font>();
            if ( pFont->createFromFlash( desc, desc.Glyphs, pAtlas ) ) {
                entry.pRegular = std::move( pFont );
            }
        }
        if ( entry.pBold != nullptr || entry.pRegular != nullptr ) {
            fonts.push_back( std::move( entry ) );
        }
    }
}

const Font *FlashMovie::getFont( const char *pFontName, bool bRegular ) const {
    for ( const FontEntry &entry : fonts ) {
        if ( pFontName != nullptr && StringCmpI( entry.Name.c_str(), pFontName ) != 0 ) {
            continue;
        }
        if ( bRegular && entry.pRegular != nullptr ) {
            return entry.pRegular.get();
        }
        if ( entry.pBold != nullptr ) {
            return entry.pBold.get();
        }
        return entry.pRegular.get();
    }
    return nullptr;
}

const Font *FlashMovie::getDefaultFont() const {
    return fonts.empty() ? nullptr : getFont( fonts[0].Name.c_str(), true );
}

bool FlashMovie::getFontPagePixels( const char *pFontName, std::vector<uint8_t> &outRGBA, uint32_t &outWidth,
                                    uint32_t &outHeight ) const {
    outRGBA.clear();
    outWidth = outHeight = 0;
    // The Font of that name carries its glyphs' atlas UVs; the pixels are the
    // fla_<CharacterId>.2db of this bank the font was built from (createFonts).
    uint16_t characterId = 0;
    bool bFound = false;
    for ( const FlashFontDesc &desc : vmf.getFonts() ) {
        if ( pFontName != nullptr && StringCmpI( desc.Name.c_str(), pFontName ) != 0 ) {
            continue;
        }
        characterId = desc.CharacterId;
        bFound = true;
        break;
    }
    if ( !bFound ) {
        return false;
    }
    const BankEntry *pEntry = bank.findEntry( GetFlashTextureName( characterId ).c_str(), "2db" );
    if ( pEntry == nullptr ) {
        return false;
    }
    Texture2DB texture;
    if ( !texture.parse( pEntry->pData, pEntry->Size ) || !IsBlockCompressedFormat( texture.Format ) ) {
        // The shipped font pages are all DXT (fla_0116: 1024 x 512 DXT1 sRGB) [DATA].
        return false;
    }
    outWidth = texture.Width;
    outHeight = texture.Height;
    outRGBA.resize( static_cast<size_t>( texture.Width ) * texture.Height * 4 );
    DecodeBlockCompressed( texture.pTexels, texture.Width, texture.Height, texture.Format, outRGBA.data() );
    return true;
}

bool FlashMovie::getBitmapPixels( uint16_t characterId, std::vector<uint8_t> &outRGBA, uint32_t &outWidth,
                                  uint32_t &outHeight ) const {
    outRGBA.clear();
    outWidth = outHeight = 0;
    const BankEntry *pEntry = bank.findEntry( GetFlashTextureName( characterId ).c_str(), "2db" );
    if ( pEntry == nullptr ) {
        return false;
    }
    Texture2DB texture;
    if ( !texture.parse( pEntry->pData, pEntry->Size ) || !IsBlockCompressedFormat( texture.Format ) ) {
        return false;
    }
    outWidth = texture.Width;
    outHeight = texture.Height;
    outRGBA.resize( static_cast<size_t>( texture.Width ) * texture.Height * 4 );
    DecodeBlockCompressed( texture.pTexels, texture.Width, texture.Height, texture.Format, outRGBA.data() );
    return true;
}

bool FlashMovie::getAtlasCellPixels( uint16_t characterId, std::vector<uint8_t> &outRGBA, uint32_t &outWidth,
                                     uint32_t &outHeight ) const {
    outRGBA.clear();
    outWidth = outHeight = 0;
    const FlashBitmapDesc *pDesc = vmf.findBitmap( characterId );
    if ( pDesc == nullptr || pDesc->U1 <= pDesc->U0 || pDesc->V1 <= pDesc->V0 ) {
        return false;
    }
    // getBitmap's page pick: the page whose size matches the UV/pixel ratio.
    const float expectedWidth = pDesc->Width / ( pDesc->U1 - pDesc->U0 );
    const float expectedHeight = pDesc->Height / ( pDesc->V1 - pDesc->V0 );
    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( entry.Extension != "2db" || StringNCmpI( entry.Name.c_str(), "page_", 5 ) != 0 ) {
            continue;
        }
        Texture2DB texture;
        if ( !texture.parse( entry.pData, entry.Size ) || !IsBlockCompressedFormat( texture.Format ) ) {
            continue;
        }
        if ( std::fabs( expectedWidth - texture.Width ) > 2.0f || std::fabs( expectedHeight - texture.Height ) > 2.0f ) {
            continue;
        }
        std::vector<uint8_t> page( static_cast<size_t>( texture.Width ) * texture.Height * 4 );
        DecodeBlockCompressed( texture.pTexels, texture.Width, texture.Height, texture.Format, page.data() );
        const int32_t x0 = static_cast<int32_t>( pDesc->U0 * texture.Width );
        const int32_t y0 = static_cast<int32_t>( pDesc->V0 * texture.Height );
        const int32_t x1 = static_cast<int32_t>( pDesc->U1 * texture.Width );
        const int32_t y1 = static_cast<int32_t>( pDesc->V1 * texture.Height );
        outWidth = static_cast<uint32_t>( x1 - x0 > 0 ? x1 - x0 : 1 );
        outHeight = static_cast<uint32_t>( y1 - y0 > 0 ? y1 - y0 : 1 );
        outRGBA.resize( static_cast<size_t>( outWidth ) * outHeight * 4 );
        for ( uint32_t y = 0; y < outHeight; y++ ) {
            const int32_t sy = y0 + static_cast<int32_t>( y ) < texture.Height - 1
                                   ? y0 + static_cast<int32_t>( y )
                                   : texture.Height - 1;
            for ( uint32_t x = 0; x < outWidth; x++ ) {
                const int32_t sx = x0 + static_cast<int32_t>( x ) < texture.Width - 1
                                       ? x0 + static_cast<int32_t>( x )
                                       : texture.Width - 1;
                memcpy( &outRGBA[( y * outWidth + x ) * 4], &page[( static_cast<size_t>( sy ) * texture.Width + sx ) * 4], 4 );
            }
        }
        return true;
    }
    return false;
}

FlashMovie::BitmapRef FlashMovie::getTexture( const char *pTextureName ) const {
    BitmapRef ref;
    ref.pTexture = findTexture( pTextureName );
    if ( ref.pTexture != nullptr ) {
        ref.UV = Rect2D( 0.0f, 0.0f, 1.0f, 1.0f );
        ref.Width = static_cast<float>( ref.pTexture->Width );
        ref.Height = static_cast<float>( ref.pTexture->Height );
    }
    return ref;
}

GPUTexture *FlashMovie::findModOverride( uint16_t characterId ) const {
    auto it = modOverrides.find( characterId );
    if ( it != modOverrides.end() ) {
        return it->second.pTexture;
    }
    // The mods flash-art override: '<mods>/flash/fla_<id>.png' replaces the
    // retail bitmap (the icon remake route — the runtime-composed UI icons
    // get hand-made stand-ins, e.g. the return-to-road badge).
    ModOverride override;
    if ( gpFile != nullptr && gpRender != nullptr ) {
        const std::filesystem::path path =
            gpFile->getModsRoot() / "flash" / ( GetFlashTextureName( characterId ) + ".png" );
        std::vector<uint8_t> pixels;
        uint32_t width = 0, height = 0;
        // Probe first: almost no bitmap has an override, and LoadImageFileRGBA
        // warns on a missing file (one WARN per UI bitmap, hundreds per run).
        std::error_code error;
        if ( std::filesystem::is_regular_file( path, error ) && LoadImageFileRGBA( path, pixels, width, height ) &&
             width > 0 && height > 0 ) {
            override.pTexture = gpRender->getTextureCache().createFromRGBA( width, height, pixels.data(), path.string().c_str() );
            override.Width = static_cast<float>( width );
            override.Height = static_cast<float>( height );
            OTDU_LOG_INFO( "Flash movie '%s': bitmap %u overridden from '%s' (%ux%u)\n", name.c_str(), characterId,
                           path.string().c_str(), width, height );
        }
    }
    modOverrides[characterId] = override;
    return override.pTexture;
}

FlashMovie::BitmapRef FlashMovie::getBitmap( uint16_t characterId ) const {
    BitmapRef ref;
    const FlashBitmapDesc *pDesc = vmf.findBitmap( characterId );
    if ( pDesc == nullptr ) {
        return ref;
    }
    // The mods override wins over every retail source. The logical size
    // stays the retail desc's: the shape fills map their coords through it
    // (fx / BitmapWidth), so an override PNG of ANY resolution fills the
    // same shape full-frame.
    if ( GPUTexture *pOverride = findModOverride( characterId ) ) {
        ref.pTexture = pOverride;
        ref.UV = Rect2D( 0.0f, 0.0f, 1.0f, 1.0f );
        ref.Width = pDesc->Width;
        ref.Height = pDesc->Height;
        return ref;
    }
    ref.UV = Rect2D( pDesc->U0, pDesc->V0, pDesc->U1 - pDesc->U0, pDesc->V1 - pDesc->V0 );
    ref.Width = pDesc->Width;
    ref.Height = pDesc->Height;

    // Standalone bitmap texture (fla_<id>.2db) or atlas page: pick the page
    // whose size matches the UV rectangle / pixel size ratio.
    ref.pTexture = findTexture( GetFlashTextureName( characterId ) );
    if ( ref.pTexture != nullptr ) {
        return ref;
    }
    float bestError = 1e9f;
    for ( const auto &page : pages ) {
        const float expectedWidth = ref.UV.Width > 0.0f ? ref.Width / ref.UV.Width : 0.0f;
        const float expectedHeight = ref.UV.Height > 0.0f ? ref.Height / ref.UV.Height : 0.0f;
        const float error = std::fabs( expectedWidth - page.second->Width ) +
                            std::fabs( expectedHeight - page.second->Height );
        if ( error < bestError ) {
            bestError = error;
            ref.pTexture = page.second;
        }
    }
    return ref;
}
