#include "shared.h"
#include "2db.h"

static constexpr uint32_t k2DBMagic = MakeFourCC( '.', '2', 'D', 'B' );
static constexpr uint32_t kBitmapMagic = MakeFourCC( 'B', 'M', 'A', 'P' );

bool Texture2DB::parse( const uint8_t *pFile, uint32_t fileSize ) {
    if ( !RenderFile::IsValid( pFile, fileSize, k2DBMagic ) ||
         fileSize < 0x20 + sizeof( Texture2DBHeader ) ) {
        return false;
    }

    RenderFile::Section section;
    memcpy( &section, pFile + sizeof( RenderFile::Header ), sizeof( section ) );
    if ( section.Type != kBitmapMagic ) {
        return false;
    }

    Texture2DBHeader header;
    const uint32_t headerOffset = sizeof( RenderFile::Header ) + sizeof( RenderFile::Section );
    memcpy( &header, pFile + headerOffset, sizeof( header ) );

    Hashcode = header.Hashcode;
    Name = IdentifierToString( header.Hashcode );
    Width = header.Width;
    Height = header.Height;
    Depth = Max<uint32_t>( header.Depth, 1u );
    NumMips = Max<uint32_t>( header.NumMipmap, 1u );
    Flags = header.Flags;
    Format = header.Format;

    const uint32_t texelOffset = headerOffset + sizeof( Texture2DBHeader );
    pTexels = pFile + texelOffset;
    TexelsSize = fileSize - texelOffset;

    // Clamp the mip chain to the available data.
    uint32_t total = 0;
    for ( uint32_t i = 0; i < NumMips; i++ ) {
        uint32_t mipSize = getMipSize( i );
        if ( total + mipSize > TexelsSize ) {
            NumMips = Max( i, 1u );
            break;
        }
        total += mipSize;
    }
    return Width > 0 && Height > 0 && getMipSize( 0 ) <= TexelsSize;
}

uint32_t Texture2DB::getMipSize( uint32_t mipIndex ) const {
    return CalcMipSize( Max( Width >> mipIndex, 1u ), Max( Height >> mipIndex, 1u ),
                        Format ) * Depth;
}

uint32_t Texture2DB::getMipOffset( uint32_t mipIndex ) const {
    uint32_t offset = 0;
    for ( uint32_t i = 0; i < mipIndex; i++ ) {
        offset += getMipSize( i );
    }
    return offset;
}

namespace {
// 5-bit / 6-bit endpoint expansion (i * 255 / 31 and i * 255 / 63). The two
// divisions per channel are what made the reference decoder slow: a 1024x1024
// DXT1 texture is 65536 blocks and each block ran six integer divisions. The
// tables hold exactly the values the divisions produced (checked bit for bit
// against the reference decoder), so the decoded pixels do not change.
struct ExpandTables {
    uint8_t V5[32];
    uint8_t V6[64];
};

constexpr ExpandTables MakeExpandTables() {
    ExpandTables tables = {};
    for ( int32_t i = 0; i < 32; i++ ) {
        tables.V5[i] = static_cast<uint8_t>( ( i * 255 ) / 31 );
    }
    for ( int32_t i = 0; i < 64; i++ ) {
        tables.V6[i] = static_cast<uint8_t>( ( i * 255 ) / 63 );
    }
    return tables;
}
constexpr ExpandTables kExpand = MakeExpandTables();

void ExpandEndpoint( uint16_t color, uint32_t &outRGBA ) {
    const uint32_t r = kExpand.V5[( color >> 11 ) & 31];
    const uint32_t g = kExpand.V6[( color >> 5 ) & 63];
    const uint32_t b = kExpand.V5[color & 31];
    outRGBA = r | ( g << 8 ) | ( b << 16 ) | 0xff000000u;
}

// The four palette entries as RGBA8 packed into a uint32: little endian puts the
// bytes in R, G, B, A order, which is the destination's layout.
void DecodeColorBlock( const uint8_t *pBlock, uint32_t palette[4], bool bForce4Colors ) {
    uint16_t c0, c1;
    memcpy( &c0, pBlock, 2 );
    memcpy( &c1, pBlock + 2, 2 );
    ExpandEndpoint( c0, palette[0] );
    ExpandEndpoint( c1, palette[1] );
    if ( c0 > c1 || bForce4Colors ) {
        const uint32_t r0 = palette[0] & 0xff, g0 = ( palette[0] >> 8 ) & 0xff, b0 = ( palette[0] >> 16 ) & 0xff;
        const uint32_t r1 = palette[1] & 0xff, g1 = ( palette[1] >> 8 ) & 0xff, b1 = ( palette[1] >> 16 ) & 0xff;
        const uint32_t r2 = ( 2 * r0 + r1 ) / 3, g2 = ( 2 * g0 + g1 ) / 3, b2 = ( 2 * b0 + b1 ) / 3;
        const uint32_t r3 = ( r0 + 2 * r1 ) / 3, g3 = ( g0 + 2 * g1 ) / 3, b3 = ( b0 + 2 * b1 ) / 3;
        palette[2] = r2 | ( g2 << 8 ) | ( b2 << 16 ) | 0xff000000u;
        palette[3] = r3 | ( g3 << 8 ) | ( b3 << 16 ) | 0xff000000u;
    } else {
        // DXT1's one-bit alpha mode: entry 2 is the average, entry 3 is black
        // and fully transparent.
        const uint32_t r2 = ( ( palette[0] & 0xff ) + ( palette[1] & 0xff ) ) / 2;
        const uint32_t g2 = ( ( ( palette[0] >> 8 ) & 0xff ) + ( ( palette[1] >> 8 ) & 0xff ) ) / 2;
        const uint32_t b2 = ( ( ( palette[0] >> 16 ) & 0xff ) + ( ( palette[1] >> 16 ) & 0xff ) ) / 2;
        palette[2] = r2 | ( g2 << 8 ) | ( b2 << 16 ) | 0xff000000u;
        palette[3] = 0;
    }
}
} // namespace

void DecodeBlockCompressed( const uint8_t *pSource,
                            uint32_t width,
                            uint32_t height,
                            eViewFormat format,
                            uint8_t *pDestinationRGBA ) {
    const bool bDXT1 = ( format == VF_DXT1 || format == VF_SRGB_DXT1 );
    const bool bDXT3 = ( format == VF_DXT2 || format == VF_DXT3 ||
                         format == VF_SRGB_DXT2 || format == VF_SRGB_DXT3 );
    const uint32_t blockSize = bDXT1 ? 8 : 16;
    const uint32_t blocksX = Max( 1u, ( width + 3 ) / 4 );
    const uint32_t blocksY = Max( 1u, ( height + 3 ) / 4 );
    const bool bHasAlpha = !bDXT1;

    for ( uint32_t by = 0; by < blocksY; by++ ) {
        const bool bRowFull = ( by * 4 + 4 ) <= height;
        for ( uint32_t bx = 0; bx < blocksX; bx++ ) {
            const bool bInterior = bRowFull && ( bx * 4 + 4 ) <= width;
            const uint8_t *pBlock = pSource + ( by * blocksX + bx ) * blockSize;
            uint32_t palette[4];
            DecodeColorBlock( pBlock + ( bDXT1 ? 0 : 8 ), palette, !bDXT1 );

            uint8_t alphas[16];
            if ( bDXT3 ) {
                for ( int32_t i = 0; i < 16; i++ ) {
                    const uint32_t a = ( pBlock[i / 2] >> ( ( i & 1 ) * 4 ) ) & 0xf;
                    alphas[i] = static_cast<uint8_t>( a * 17 );
                }
            } else if ( bHasAlpha ) {
                const uint8_t a0 = pBlock[0];
                const uint8_t a1 = pBlock[1];
                uint64_t bits = 0;
                for ( int32_t i = 0; i < 6; i++ ) {
                    bits |= static_cast<uint64_t>( pBlock[2 + i] ) << ( 8 * i );
                }
                uint8_t table[8];
                table[0] = a0;
                table[1] = a1;
                if ( a0 > a1 ) {
                    for ( int32_t k = 2; k < 8; k++ ) {
                        table[k] = static_cast<uint8_t>( ( ( 8 - k ) * a0 + ( k - 1 ) * a1 ) / 7 );
                    }
                } else {
                    for ( int32_t k = 2; k < 6; k++ ) {
                        table[k] = static_cast<uint8_t>( ( ( 6 - k ) * a0 + ( k - 1 ) * a1 ) / 5 );
                    }
                    table[6] = 0;
                    table[7] = 255;
                }
                for ( int32_t i = 0; i < 16; i++ ) {
                    alphas[i] = table[( bits >> ( 3 * i ) ) & 7];
                }
            }

            uint32_t indices;
            // The colour block's index bits sit 4 bytes into it (DXT1: block+4,
            // DXT2/3/5: block+8+4 = block+12).
            memcpy( &indices, pBlock + ( bDXT1 ? 4 : 12 ), 4 );

            if ( bInterior ) {
                // Whole block inside the image: build the 16 pixels as four
                // 4-pixel rows and store each row as one 16-byte write (the
                // compiler turns the memcpy into a single 128-bit store).
                uint32_t colors[16];
                for ( uint32_t i = 0; i < 16; i++ ) {
                    uint32_t color = palette[( indices >> ( i * 2 ) ) & 3];
                    if ( bHasAlpha ) {
                        color = ( color & 0x00ffffffu ) |
                                ( static_cast<uint32_t>( alphas[i] ) << 24 );
                    }
                    colors[i] = color;
                }
                uint8_t *pOut = pDestinationRGBA +
                                ( static_cast<size_t>( by * 4 ) * width + bx * 4 ) * 4;
                for ( uint32_t py = 0; py < 4; py++ ) {
                    memcpy( pOut + static_cast<size_t>( py ) * width * 4, colors + py * 4, 16 );
                }
            } else {
                for ( uint32_t py = 0; py < 4; py++ ) {
                    if ( by * 4 + py >= height ) {
                        break;
                    }
                    const uint32_t row = by * 4 + py;
                    uint8_t *pOut = pDestinationRGBA + ( static_cast<size_t>( row ) * width + bx * 4 ) * 4;
                    const uint32_t rowIndices = indices >> ( py * 8 );
                    for ( uint32_t px = 0; px < 4; px++ ) {
                        if ( bx * 4 + px >= width ) {
                            break;
                        }
                        uint32_t color = palette[( rowIndices >> ( px * 2 ) ) & 3];
                        if ( bHasAlpha ) {
                            color = ( color & 0x00ffffffu ) |
                                    ( static_cast<uint32_t>( alphas[py * 4 + px] ) << 24 );
                        }
                        memcpy( pOut + px * 4, &color, 4 );
                    }
                }
            }
        }
    }
}
