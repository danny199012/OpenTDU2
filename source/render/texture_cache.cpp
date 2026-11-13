#include "shared.h"
#include "texture_cache.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "render/2db.h"
#include "system/cmd_line.h"

#include <filesystem>

namespace {
VkFormat GetVulkanFormat( eViewFormat format, bool bSupportsBC ) {
    switch ( format ) {
    case VF_DXT1:
    case VF_SRGB_DXT1:
        return bSupportsBC ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_R8G8B8A8_UNORM;
    case VF_DXT2:
    case VF_DXT3:
    case VF_SRGB_DXT2:
    case VF_SRGB_DXT3:
        return bSupportsBC ? VK_FORMAT_BC2_UNORM_BLOCK : VK_FORMAT_R8G8B8A8_UNORM;
    case VF_DXT4:
    case VF_DXT5:
    case VF_SRGB_DXT4:
    case VF_SRGB_DXT5:
        return bSupportsBC ? VK_FORMAT_BC3_UNORM_BLOCK : VK_FORMAT_R8G8B8A8_UNORM;
    case VF_A8R8G8B8:
    case VF_X8R8G8B8:
    case VF_SRGB_A8R8G8B8:
    case VF_SRGB_X8R8G8B8:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case VF_R8G8B8A8:
    case VF_SRGB_R8G8B8A8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    default:
        return VK_FORMAT_R8G8B8A8_UNORM; // Converted on the CPU
    }
}

// Converts uncommon formats to RGBA8.
bool ConvertToRGBA8( const uint8_t *pSource,
                     uint32_t width,
                     uint32_t height,
                     eViewFormat format,
                     std::vector<uint8_t> &output ) {
    output.resize( static_cast<size_t>( width ) * height * 4 );
    const size_t numPixels = static_cast<size_t>( width ) * height;
    switch ( format ) {
    case VF_L8:
    case VF_SRGB_L8:
        for ( size_t i = 0; i < numPixels; i++ ) {
            output[i * 4 + 0] = output[i * 4 + 1] = output[i * 4 + 2] = pSource[i];
            output[i * 4 + 3] = 255;
        }
        return true;
    case VF_A8:
    case VF_SRGB_A8:
        for ( size_t i = 0; i < numPixels; i++ ) {
            output[i * 4 + 0] = output[i * 4 + 1] = output[i * 4 + 2] = 255;
            output[i * 4 + 3] = pSource[i];
        }
        return true;
    case VF_R5G6B5:
    case VF_SRGB_R5G6B5:
        for ( size_t i = 0; i < numPixels; i++ ) {
            uint16_t c;
            memcpy( &c, pSource + i * 2, 2 );
            output[i * 4 + 0] = static_cast<uint8_t>( ( ( c >> 11 ) & 31 ) * 255 / 31 );
            output[i * 4 + 1] = static_cast<uint8_t>( ( ( c >> 5 ) & 63 ) * 255 / 63 );
            output[i * 4 + 2] = static_cast<uint8_t>( ( c & 31 ) * 255 / 31 );
            output[i * 4 + 3] = 255;
        }
        return true;
    case VF_A1R5G5B5:
    case VF_SRGB_A1R5G5B5:
        for ( size_t i = 0; i < numPixels; i++ ) {
            uint16_t c;
            memcpy( &c, pSource + i * 2, 2 );
            output[i * 4 + 0] = static_cast<uint8_t>( ( ( c >> 10 ) & 31 ) * 255 / 31 );
            output[i * 4 + 1] = static_cast<uint8_t>( ( ( c >> 5 ) & 31 ) * 255 / 31 );
            output[i * 4 + 2] = static_cast<uint8_t>( ( c & 31 ) * 255 / 31 );
            output[i * 4 + 3] = ( c & 0x8000 ) ? 255 : 0;
        }
        return true;
    case VF_R8G8B8:
    case VF_SRGB_R8G8B8:
        for ( size_t i = 0; i < numPixels; i++ ) {
            output[i * 4 + 0] = pSource[i * 3 + 2];
            output[i * 4 + 1] = pSource[i * 3 + 1];
            output[i * 4 + 2] = pSource[i * 3 + 0];
            output[i * 4 + 3] = 255;
        }
        return true;
    case VF_L16:
    case VF_SRGB_L16:
        for ( size_t i = 0; i < numPixels; i++ ) {
            output[i * 4 + 0] = output[i * 4 + 1] = output[i * 4 + 2] = pSource[i * 2 + 1];
            output[i * 4 + 3] = 255;
        }
        return true;
    default:
        return false;
    }
}
} // namespace

TextureCache::TextureCache()
    : pDevice( nullptr ), pWhiteTexture( nullptr ), pCheckerTexture( nullptr ) {}

TextureCache::~TextureCache() {}

void TextureCache::initialize( RenderDevice *pRenderDevice ) {
    pDevice = pRenderDevice;

    const uint32_t white[16] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
                                 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
                                 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
                                 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
    pWhiteTexture = createFromRGBA( 4, 4, reinterpret_cast<const uint8_t *>( white ),
                                    "OTDU_White" );

    std::vector<uint32_t> checker( 64 * 64 );
    for ( uint32_t y = 0; y < 64; y++ ) {
        for ( uint32_t x = 0; x < 64; x++ ) {
            bool bDark = ( ( x / 8 ) + ( y / 8 ) ) & 1;
            checker[y * 64 + x] = bDark ? 0xff505050 : 0xff707070;
        }
    }
    pCheckerTexture = createFromRGBA( 64, 64, reinterpret_cast<const uint8_t *>( checker.data() ),
                                      "OTDU_Checker" );
}

void TextureCache::destroy() {
    for ( OwnerTextures &owner : owners ) {
        destroyOwnerTextures( owner );
    }
    for ( OwnerTextures &owner : retired ) {
        destroyOwnerTextures( owner );
    }
    owners.clear();
    retired.clear();
    borrows.clear();
    pDevice->destroyTexture( pWhiteTexture );
    pDevice->destroyTexture( pCheckerTexture );
    pWhiteTexture = nullptr;
    pCheckerTexture = nullptr;
}

TextureCache::OwnerTextures &TextureCache::getOwner( const char *pOwner ) {
    const std::string name = pOwner != nullptr ? pOwner : "";
    for ( OwnerTextures &owner : owners ) {
        if ( owner.Owner == name ) {
            return owner;
        }
    }
    owners.push_back( OwnerTextures() );
    owners.back().Owner = name;
    return owners.back();
}

GPUTexture *TextureCache::createFromRGBA( uint32_t width,
                                          uint32_t height,
                                          const uint8_t *pPixels,
                                          const char *pName ) {
    const void *pData = pPixels;
    const uint32_t size = width * height * 4;
    return pDevice->createTexture2D( width, height, 1, VK_FORMAT_R8G8B8A8_UNORM, &pData, &size,
                                     pName );
}

GPUTexture *TextureCache::createFrom2DB( const Texture2DB &texture, const char *pName ) {
    const bool bSupportsBC = pDevice->supportsBC();
    const VkFormat format = GetVulkanFormat( texture.Format, bSupportsBC );
    const bool bBlockCompressed = IsBlockCompressedFormat( texture.Format );
    const bool bNativeLayout =
        ( bBlockCompressed && bSupportsBC ) || format == VK_FORMAT_B8G8R8A8_UNORM ||
        texture.Format == VF_R8G8B8A8 || texture.Format == VF_SRGB_R8G8B8A8;

    // Only keep mips down to the 4x4 block size for compressed formats.
    uint32_t numMips = texture.NumMips;
    static const bool bNoCube = CmdLine::HasArg( "-nocube2db" ); // comparisons: face 0 as 2D (old behaviour)
    const bool bCube = texture.isCube() && !bNoCube;
    if ( texture.Depth > 1 && !bCube ) {
        numMips = 1; // Volume textures: first slice only for now
    }

    // Cube maps (cubemap, env_lph_l3d, cubemap01, cubecarb...: Depth 6,
    // Flags 0xC4 / 0xC8; 2D textures: Depth 1, 0x84 / 0x88) [DATA]: the
    // texels are face major (face 0 with all its mips, then face 1...;
    // checked on env_lph_l3d: mip 1 matches the downsampled mip 0 with this
    // layout only), faces in the D3D order +X -X +Y -Y +Z -Z [INF: standard
    // D3D]. Vulkan cube faces use the same order and texel orientation as
    // D3D9, and the Eden renderer draws in the original (unmirrored) D3D
    // space: the faces are uploaded as they are.
    const uint32_t numLayers = bCube ? 6u : 1u;
    uint32_t faceSize = 0;
    for ( uint32_t i = 0; i < numMips; i++ ) {
        faceSize += CalcMipSize( Max( texture.Width >> i, 1u ), Max( texture.Height >> i, 1u ), texture.Format );
    }
    if ( bCube && faceSize * 6 > texture.TexelsSize ) {
        OTDU_LOG_WARN( "Cube texture '%s': truncated data\n", pName );
        return nullptr;
    }
    auto getSource = [&]( uint32_t layer, uint32_t mip ) -> const uint8_t * {
        if ( !bCube ) {
            return texture.pTexels + texture.getMipOffset( mip );
        }
        uint32_t offset = layer * faceSize;
        for ( uint32_t i = 0; i < mip; i++ ) {
            offset += CalcMipSize( Max( texture.Width >> i, 1u ), Max( texture.Height >> i, 1u ), texture.Format );
        }
        return texture.pTexels + offset;
    };

    const uint32_t numSubresources = numMips * numLayers;
    std::vector<const void *> mipData( numSubresources );
    std::vector<uint32_t> mipSizes( numSubresources );
    std::vector<std::vector<uint8_t>> converted;

    if ( bNativeLayout ) {
        for ( uint32_t s = 0; s < numSubresources; s++ ) {
            const uint32_t i = s % numMips;
            mipData[s] = getSource( s / numMips, i );
            mipSizes[s] = CalcMipSize( Max( texture.Width >> i, 1u ),
                                       Max( texture.Height >> i, 1u ), texture.Format );
        }
    } else {
        converted.resize( numSubresources );
        for ( uint32_t s = 0; s < numSubresources; s++ ) {
            const uint32_t i = s % numMips;
            const uint32_t width = Max( texture.Width >> i, 1u );
            const uint32_t height = Max( texture.Height >> i, 1u );
            const uint8_t *pSource = getSource( s / numMips, i );
            if ( bBlockCompressed ) {
                converted[s].resize( static_cast<size_t>( width ) * height * 4 );
                DecodeBlockCompressed( pSource, width, height, texture.Format, converted[s].data() );
            } else if ( !ConvertToRGBA8( pSource, width, height, texture.Format, converted[s] ) ) {
                OTDU_LOG_WARN( "Unsupported texture format %u for '%s'\n", texture.Format, pName );
                return nullptr;
            }
            mipData[s] = converted[s].data();
            mipSizes[s] = static_cast<uint32_t>( converted[s].size() );
        }
    }

    if ( bCube ) {
        return pDevice->createTexture( VK_IMAGE_VIEW_TYPE_CUBE, texture.Width, texture.Height, 1, 6, numMips, format,
                                       mipData.data(), mipSizes.data(), pName );
    }
    return pDevice->createTexture2D( texture.Width, texture.Height, numMips, format,
                                     mipData.data(), mipSizes.data(), pName );
}

uint32_t TextureCache::loadBankTextures( const Bank &bank, const char *pOwner ) {
    OwnerTextures &owner = getOwner( pOwner );
    // One more holder of this bank path: the textures already in the owner are
    // skipped below (two views of the same island share them), and releaseOwner
    // only frees them when the last holder goes.
    owner.References++;
    uint32_t numLoaded = 0;
    // Restored content (tools/x360/restore_texture.py): a texture cut from the
    // retail game, found in the Xbox 360 prototypes, replaces the bank's own
    // when <userdata>/restored/<bank name>/<texture file> exists (e.g. the Q7
    // Inuit's grey dials, docs/re/CROSSBUILD_PLAN.md). -norestored: retail only.
    static const bool bRestored = !CmdLine::HasArg( "-norestored" );
    // The sector buildings ship 64x64 LOD bakes ('sb_airpo_lodc', 'sb_gazs_a_lodc',
    // 'sb_farma_lod_c', ...) that the retail engine swaps for the full atlas when
    // the camera is close. The port has no per-distance texture swap, so every
    // building drew its LOD mush at every distance - the owner's graffiti-texture
    // buildings and the airport's red/cream runway stripes. -uselodtextures keeps
    // the retail LOD bakes (the A/B).
    static const bool bLodTextures = CmdLine::HasArg( "-uselodtextures" );
    struct LodCandidate {
        std::string Folder;   // the entry's folder (the '.../sb_airpo/maps' part)
        std::string Name;     // entry name, lowercased
        uint32_t Size = 0;
        uint64_t Hashcode = 0;
    };
    std::vector<LodCandidate> lodCandidates;
    std::string bankStem = bank.getPath();
    bankStem = bankStem.substr( bankStem.find_last_of( "/\\" ) + 1 );
    bankStem = ToLower( bankStem.substr( 0, bankStem.find_last_of( '.' ) ) );
    std::vector<uint8_t> restored;
    struct LoadedTexture {
        std::string Folder;
        std::string Name;
        uint32_t Size = 0;
        GPUTexture *pTexture = nullptr;
    };
    std::vector<LoadedTexture> loadedTextures;
    for ( const BankEntry *pEntry : bank.findEntriesByExtension( "2db" ) ) {
        Texture2DB texture;
        const uint8_t *pData = pEntry->pData;
        uint32_t dataSize = pEntry->Size;
        if ( bRestored && gpFile != nullptr ) {
            const std::filesystem::path path =
                gpFile->getUserDataFolder() / "restored" / bankStem / ToLower( pEntry->Filename );
            restored.clear();
            if ( std::filesystem::exists( path ) && gpFile->loadFile( path.string().c_str(), restored ) ) {
                OTDU_LOG_INFO( "TextureCache: '%s' of %s restored from '%s'\n", pEntry->Filename.c_str(), bankStem.c_str(),
                               path.string().c_str() );
                pData = restored.data();
                dataSize = static_cast<uint32_t>( restored.size() );
            }
        }
        if ( !texture.parse( pData, dataSize ) ) {
            OTDU_LOG_WARN( "Failed to parse texture '%s'\n", pEntry->Filename.c_str() );
            continue;
        }

        const uint64_t hashcode = GetIdentifier64bit( pEntry->Name.c_str() );
        // Already loaded for this owner (e.g. shared banks loaded by several
        // vehicles): keep the existing texture, models may reference it.
        if ( owner.Textures.find( hashcode ) != owner.Textures.end() ) {
            continue;
        }

        // The tall-grass atlases (tallgrass.bnk, grass_spec.md 8): the cards
        // are authored with pale, silvery dry-stalk bases, and the retail
        // TD_TallGrass pixel shader darkens them with its height light
        // gradient (roots in shade, tips lit). The port draws the cards with
        // plain Gouraud, which has no gradient - the bases rendered as bright
        // white fringes. The same gradient is baked into the DXT5 colour
        // endpoints here (alpha untouched) [INF: the gradient shape].
        std::vector<uint8_t> grassRooted;
        if ( bankStem == "tallgrass" && !CmdLine::HasArg( "-nograssroots" ) &&
             ( texture.Format == VF_DXT5 || texture.Format == VF_SRGB_DXT5 ) && !texture.isCube() ) {
            grassRooted.assign( pData, pData + dataSize );
            Texture2DB rooted;
            if ( rooted.parse( grassRooted.data(), dataSize ) ) {
                const char *pArg = CmdLine::GetArgValue( "-grassrootdark" );
                const float bottomScale = pArg != nullptr ? static_cast<float>( atof( pArg ) ) : 0.5f;
                uint8_t *pTexels = const_cast<uint8_t *>( rooted.pTexels );
                for ( uint32_t mip = 0; mip < rooted.NumMips; mip++ ) {
                    const uint32_t mipW = Max( rooted.Width >> mip, 1u );
                    const uint32_t mipH = Max( rooted.Height >> mip, 1u );
                    const uint32_t blocksX = ( mipW + 3 ) / 4;
                    const uint32_t blocksY = ( mipH + 3 ) / 4;
                    uint8_t *pBlock = pTexels + rooted.getMipOffset( mip );
                    for ( uint32_t by = 0; by < blocksY; by++ ) {
                        // 0 at the card top, 1 at the card bottom (roots).
                        const float v = ( by + 0.5f ) / blocksY;
                        const float t = Clamp( ( v - 0.35f ) / 0.65f, 0.0f, 1.0f );
                        const float scale = 1.0f - ( 1.0f - bottomScale ) * t * t * ( 3.0f - 2.0f * t );
                        for ( uint32_t bx = 0; bx < blocksX; bx++, pBlock += 16 ) {
                            for ( int e = 0; e < 2; e++ ) {
                                uint16_t c;
                                memcpy( &c, pBlock + 8 + e * 2, 2 );
                                const float r = ( ( c >> 11 ) & 31 ) * scale;
                                const float g = ( ( c >> 5 ) & 63 ) * scale;
                                const float b = ( c & 31 ) * scale;
                                c = static_cast<uint16_t>( ( static_cast<uint32_t>( r + 0.5f ) << 11 ) |
                                                           ( static_cast<uint32_t>( g + 0.5f ) << 5 ) |
                                                           static_cast<uint32_t>( b + 0.5f ) );
                                memcpy( pBlock + 8 + e * 2, &c, 2 );
                            }
                        }
                    }
                }
                texture = rooted;
            }
        }

        GPUTexture *pTexture = createFrom2DB( texture, pEntry->Name.c_str() );
        if ( pTexture == nullptr ) {
            continue;
        }
        owner.Textures[hashcode] = pTexture;
        if ( texture.Hashcode != hashcode ) {
            // Alias with the hashcode stored in the resource (used by materials).
            owner.Aliases[texture.Hashcode] = pTexture;
        }
        if ( !bLodTextures ) {
            const std::string lowerName = ToLower( pEntry->Name );
            const bool bIsLod = lowerName.find( "lodc" ) != std::string::npos ||
                                lowerName.rfind( "_lod" ) == lowerName.size() - 4 ||
                                lowerName.find( "_lod_" ) != std::string::npos;
            std::string folder = pEntry->Path;
            folder = folder.substr( 0, folder.find_last_of( "/\\" ) );
            folder = ToLower( folder );
            if ( bIsLod ) {
                lodCandidates.push_back( { folder, lowerName, pEntry->Size, hashcode } );
            }
            loadedTextures.push_back( { folder, lowerName, pEntry->Size, pTexture } );
        }
        numLoaded++;
    }
        OTDU_LOG_DEBUG( "Loaded %u textures from '%s'\n", numLoaded, bank.getPath().c_str() );
        // The LOD -> full aliases: for each LOD bake, the sibling in the same
        // folder with the longest shared name prefix and the largest size (the
        // retail names are not regular: sb_airpo_lodc -> sb_airpo_big,
        // sb_gazs_a_lodc -> sb_gazs_a, sb_farma_lod_c -> sb_farma_c).
        uint32_t numLodAliased = 0;
        for ( const LodCandidate &lod : lodCandidates ) {
            const LoadedTexture *pBest = nullptr;
            size_t bestPrefix = 0;
            for ( const LoadedTexture &full : loadedTextures ) {
                if ( full.pTexture == nullptr || full.Folder != lod.Folder ||
                     full.Name.find( "lod" ) != std::string::npos ) {
                    continue;
                }
                size_t shared = 0;
                while ( shared < full.Name.size() && shared < lod.Name.size() &&
                        full.Name[shared] == lod.Name[shared] ) {
                    shared++;
                }
                if ( shared < bestPrefix || ( shared == bestPrefix && pBest != nullptr && pBest->Size >= full.Size ) ) {
                    continue;
                }
                if ( shared == 0 ) {
                    continue;
                }
                bestPrefix = shared;
                pBest = &full;
            }
            if ( pBest != nullptr && pBest->Size > lod.Size ) {
                owner.Aliases[lod.Hashcode] = pBest->pTexture;
                numLodAliased++;
            }
        }
        if ( numLodAliased > 0 ) {
            OTDU_LOG_INFO( "TextureCache: %u LOD bakes aliased to their full textures in '%s'\n", numLodAliased,
                           bank.getPath().c_str() );
        }
        return numLoaded;
}

void TextureCache::destroyOwnerTextures( OwnerTextures &owner ) {
    for ( auto &entry : owner.Textures ) {
        pDevice->destroyTexture( entry.second );
    }
    owner.Textures.clear();
    owner.Aliases.clear();
    owner.References = 0;
}

bool TextureCache::isBorrowed( const std::string &lender ) const {
    for ( const auto &borrower : borrows ) {
        if ( borrower.first == lender ) {
            continue;
        }
        auto it = borrower.second.find( lender );
        if ( it != borrower.second.end() && it->second > 0 ) {
            return true;
        }
    }
    return false;
}

void TextureCache::releaseOwner( const char *pOwner ) {
    const std::string name = pOwner != nullptr ? pOwner : "";
    for ( auto it = owners.begin(); it != owners.end(); ++it ) {
        if ( it->Owner != name ) {
            continue;
        }
        // Another view still holds this bank path (the free ride world while a
        // shop interior's own EdenWorld goes away, two cinematics on the same
        // set, ...): hand one reference back and keep the textures. Destroying
        // them here is what freed the flat map's island colour map and the
        // sector textures still being drawn - the "texture corruption" and the
        // device hang that followed it.
        if ( it->References > 1 ) {
            it->References--;
            OTDU_LOG_DEBUG( "Texture cache: '%s' released, %u holder(s) left\n", name.c_str(), it->References );
            return;
        }
        it->References = 0;
        // The models of this owner go with it [INF: every caller unloads the
        // bank's models with its textures]: its own borrows end here.
        borrows.erase( name );
        if ( isBorrowed( name ) ) {
            OTDU_LOG_DEBUG( "Texture cache: '%s' released while borrowed - kept until its borrowers go\n",
                            name.c_str() );
            retired.push_back( std::move( *it ) );
        } else {
            destroyOwnerTextures( *it );
        }
        owners.erase( it );
        break;
    }
    // A retired lender whose last borrower this was.
    for ( auto it = retired.begin(); it != retired.end(); ) {
        if ( !isBorrowed( it->Owner ) ) {
            OTDU_LOG_DEBUG( "Texture cache: retired '%s' destroyed (no borrower left)\n",it->Owner.c_str() );
            destroyOwnerTextures( *it );
            it = retired.erase( it );
        } else {
            ++it;
        }
    }
}

GPUTexture *TextureCache::findForOwner( const char *pBorrower, uint64_t hashcode ) {
    const std::string borrower = pBorrower != nullptr ? pBorrower : "";
    for ( auto it = owners.rbegin(); it != owners.rend(); ++it ) {
        auto texture = it->Textures.find( hashcode );
        GPUTexture *pTexture = texture != it->Textures.end() ? texture->second : nullptr;
        if ( pTexture == nullptr ) {
            auto alias = it->Aliases.find( hashcode );
            pTexture = alias != it->Aliases.end() ? alias->second : nullptr;
        }
        if ( pTexture == nullptr ) {
            continue;
        }
        if ( it->Owner != borrower ) {
            uint32_t &count = borrows[borrower][it->Owner];
            if ( count++ == 0 ) {
                OTDU_LOG_DEBUG( "Texture cache: '%s' borrows from '%s' (first: %s)\n", borrower.c_str(),
                                it->Owner.c_str(), pTexture->Name.c_str() );
            }
        }
        return pTexture;
    }
    return nullptr;
}

GPUTexture *TextureCache::find( uint64_t hashcode ) const {
    for ( auto it = owners.rbegin(); it != owners.rend(); ++it ) {
        auto texture = it->Textures.find( hashcode );
        if ( texture != it->Textures.end() ) {
            return texture->second;
        }
        auto alias = it->Aliases.find( hashcode );
        if ( alias != it->Aliases.end() ) {
            return alias->second;
        }
    }
    return nullptr;
}

GPUTexture *TextureCache::find( const char *pName ) const {
    return find( GetIdentifier64bit( pName ) );
}

GPUTexture *TextureCache::find( const char *pOwner, const char *pName ) const {
    const uint64_t hashcode = GetIdentifier64bit( pName );
    for ( const OwnerTextures &owner : owners ) {
        if ( owner.Owner != pOwner ) {
            continue;
        }
        auto texture = owner.Textures.find( hashcode );
        return texture != owner.Textures.end() ? texture->second : nullptr;
    }
    return nullptr;
}

GPUTexture *TextureCache::find( const char *pOwner, uint64_t hashcode ) const {
    // The material's hash directly: the string round trip (hash ->
    // IdentifierToString -> GetIdentifier64bit) is not idempotent - a slot
    // byte in 'a'..'z' folds again and the derived hash differs from the one
    // the textures are registered under.
    for ( const OwnerTextures &owner : owners ) {
        if ( owner.Owner != pOwner ) {
            continue;
        }
        auto texture = owner.Textures.find( hashcode );
        if ( texture != owner.Textures.end() ) {
            return texture->second;
        }
        auto alias = owner.Aliases.find( hashcode );
        return alias != owner.Aliases.end() ? alias->second : nullptr;
    }
    return nullptr;
}
