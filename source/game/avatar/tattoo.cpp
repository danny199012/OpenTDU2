#include "shared.h"
#include "tattoo.h"

#include "filesystem/gs_file.h"
#include "render/image_file.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {
struct LoadedMask {
    std::vector<uint8_t> Pixels;
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool bValid = false;
};

// <userdata>/tattoos/<name>.png : the alpha is the mask (opaque files: 1 - luma, so black line art works).
const LoadedMask &GetFileMask( const std::string &design ) {
    static std::map<std::string, LoadedMask> cache;
    auto it = cache.find( design );
    if ( it != cache.end() ) {
        return it->second;
    }
    LoadedMask &mask = cache[design];
    std::vector<uint8_t> pixels;
    if ( gpFile == nullptr ) {
        return mask;
    }
    const std::filesystem::path path = gpFile->getUserDataFolder() / "tattoos" / ( design + ".png" );
    if ( LoadImageFileRGBA( path, pixels, mask.Width, mask.Height ) && mask.Width > 0 && mask.Height > 0 ) {
        bool bHasAlpha = false;
        for ( size_t i = 3; i < pixels.size(); i += 4 ) {
            if ( pixels[i] != 255 ) {
                bHasAlpha = true;
                break;
            }
        }
        mask.Pixels.resize( static_cast<size_t>( mask.Width ) * mask.Height );
        for ( size_t i = 0; i < mask.Pixels.size(); i++ ) {
            const uint8_t *p = &pixels[i * 4];
            mask.Pixels[i] = bHasAlpha ? p[3] : static_cast<uint8_t>( 255 - ( p[0] * 77 + p[1] * 150 + p[2] * 29 ) / 256 );
        }
        mask.bValid = true;
    }
    return mask;
}

float Smooth( float edge, float width, float value ) {
    const float t = ( value - edge ) / width;
    return t <= 0.0f ? 0.0f : ( t >= 1.0f ? 1.0f : t * t * ( 3.0f - 2.0f * t ) );
}

bool IsBuiltIn( const std::string &design ) {
    return design == "ring" || design == "star" || design == "cross" || design == "bars" || design == "tribal";
}
} // namespace

float TattooMask( const std::string &design, float x, float y ) {
    if ( x * x + y * y > 1.0f ) {
        return 0.0f;
    }
    const float r = std::sqrt( x * x + y * y );
    const float a = std::atan2( y, x );
    if ( design == "ring" ) {
        return Smooth( 0.62f, 0.06f, r ) * ( 1.0f - Smooth( 0.86f, 0.06f, r ) );
    }
    if ( design == "star" ) {
        const float limit = 0.72f + 0.28f * std::cos( 5.0f * a );
        return 1.0f - Smooth( limit - 0.08f, 0.08f, r );
    }
    if ( design == "cross" ) {
        return 1.0f - Smooth( 0.13f, 0.05f, std::min( std::fabs( x ), std::fabs( y ) ) );
    }
    if ( design == "bars" ) {
        const float band = std::fabs( std::fmod( y * 3.0f + 3.0f, 1.0f ) - 0.5f );
        return ( 1.0f - Smooth( 0.17f, 0.05f, band ) ) * ( 1.0f - Smooth( 0.9f, 0.1f, std::fabs( x ) ) );
    }
    if ( design == "tribal" ) {
        // a mirrored thorn: thick in the middle, tapering to both ends
        const float width = 0.28f * ( 1.0f - std::fabs( y ) ) * ( 1.0f + 0.5f * std::cos( 6.0f * y ) );
        return 1.0f - Smooth( width - 0.04f, 0.06f, std::fabs( x ) );
    }
    const LoadedMask &file = GetFileMask( design );
    if ( file.bValid ) {
        const uint32_t px = std::min<uint32_t>( file.Width - 1, static_cast<uint32_t>( ( x * 0.5f + 0.5f ) * file.Width ) );
        const uint32_t py = std::min<uint32_t>( file.Height - 1, static_cast<uint32_t>( ( y * 0.5f + 0.5f ) * file.Height ) );
        return file.Pixels[py * file.Width + px] / 255.0f;
    }
    return -1.0f;
}

void CompositeTattoos( uint8_t *pRGBA, uint32_t width, uint32_t height, const std::vector<TattooDecal> &decals ) {
    for ( const TattooDecal &decal : decals ) {
        if ( !IsBuiltIn( decal.Design ) && !GetFileMask( decal.Design ).bValid ) {
            OTDU_LOG_WARN( "Tattoo: unknown design '%s'\n", decal.Design.c_str() );
            continue;
        }
        const float halfPx = std::max( 1.0f, decal.Scale * width );
        const float rotation = decal.Rotation * 3.14159265f / 180.0f;
        const float cosR = std::cos( rotation ), sinR = std::sin( rotation );
        const float centreX = decal.U * width, centreY = decal.V * height;
        const int32_t reach = static_cast<int32_t>( halfPx * 1.5f ) + 1;
        const uint8_t ink[3] = { static_cast<uint8_t>( decal.Color >> 16 ), static_cast<uint8_t>( decal.Color >> 8 ),
                                 static_cast<uint8_t>( decal.Color ) };
        for ( int32_t dy = -reach; dy <= reach; dy++ ) {
            for ( int32_t dx = -reach; dx <= reach; dx++ ) {
                const int32_t px = static_cast<int32_t>( centreX ) + dx;
                const int32_t py = static_cast<int32_t>( centreY ) + dy;
                if ( px < 0 || py < 0 || px >= static_cast<int32_t>( width ) || py >= static_cast<int32_t>( height ) ) {
                    continue;
                }
                const float lx = ( cosR * dx + sinR * dy ) / halfPx;
                const float ly = ( -sinR * dx + cosR * dy ) / halfPx;
                const float mask = TattooMask( decal.Design, lx, ly );
                if ( mask <= 0.0f ) {
                    continue;
                }
                const float alpha = std::min( 1.0f, mask * decal.Opacity );
                uint8_t *pDst = pRGBA + ( static_cast<size_t>( py ) * width + px ) * 4;
                for ( int32_t c = 0; c < 3; c++ ) {
                    pDst[c] = static_cast<uint8_t>( pDst[c] + ( ink[c] - pDst[c] ) * alpha );
                }
            }
        }
    }
}

std::string SerializeTattoos( const std::vector<TattooDecal> &decals ) {
    std::string out;
    char buffer[160];
    for ( const TattooDecal &d : decals ) {
        const int flags = ( d.Head ? 1 : 0 ) | ( d.Side << 1 );
        char tail[16] = "";
        if ( flags != 0 ) {
            snprintf( tail, sizeof( tail ), ",%d", flags );
        }
        snprintf( buffer, sizeof( buffer ), "%s,%.4f,%.4f,%.4f,%.1f,%.2f,%06x%s;", d.Design.c_str(), d.U, d.V, d.Scale,
                  d.Rotation, d.Opacity, d.Color & 0xffffff, tail );
        out += buffer;
    }
    return out;
}

std::vector<TattooDecal> ParseTattoos( const std::string &text ) {
    std::vector<TattooDecal> out;
    size_t at = 0;
    while ( at < text.size() ) {
        size_t end = text.find( ';', at );
        if ( end == std::string::npos ) {
            end = text.size();
        }
        const std::string item = text.substr( at, end - at );
        at = end + 1;
        char design[64] = {};
        TattooDecal d;
        unsigned color = d.Color;
        int head = 0;
        if ( sscanf( item.c_str(), "%63[^,],%f,%f,%f,%f,%f,%x,%d", design, &d.U, &d.V, &d.Scale, &d.Rotation, &d.Opacity,
                     &color, &head ) >= 4 ) {
            d.Head = ( head & 1 ) != 0;
            d.Side = ( head >> 1 ) & 3;
            d.Design = design;
            d.Color = color & 0xffffff;
            out.push_back( d );
        }
    }
    return out;
}
