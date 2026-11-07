#include "shared.h"
#include "render/image_file.h"

#include "render/2db.h"

// stb_image is header only and part of the linked otdu_stb dependency (the
// PNG writer of render_device.cpp does the same with stb_image_write).
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {
// A DDS file: "DDS " + DDS_HEADER (124 bytes, the DDS_PIXELFORMAT included).
// Only the fields the decoder needs are named; the rest is padding.
#pragma pack( push, 1 )
struct DdsPixelFormat {
    uint32_t Size;
    uint32_t Flags;
    uint32_t FourCC;
    uint32_t RGBBitCount;
    uint32_t RMask, GMask, BMask, AMask;
};
struct DdsHeader {
    uint32_t Size;
    uint32_t Flags;
    uint32_t Height;
    uint32_t Width;
    uint32_t PitchOrLinearSize;
    uint32_t Depth;
    uint32_t MipMapCount;
    uint32_t Reserved1[11];
    DdsPixelFormat PixelFormat;
    uint32_t Caps;
    uint32_t Caps2, Caps3, Caps4, Reserved2;
};
#pragma pack( pop )
OTDU_SIZE_MUST_MATCH( DdsPixelFormat, 32 );
OTDU_SIZE_MUST_MATCH( DdsHeader, 124 );

constexpr uint32_t DdsFourCC( char a, char b, char c, char d ) {
    return static_cast<uint32_t>( a ) | ( static_cast<uint32_t>( b ) << 8 ) | ( static_cast<uint32_t>( c ) << 16 ) |
           ( static_cast<uint32_t>( d ) << 24 );
}

bool ReadFileBytes( const std::filesystem::path &path, std::vector<uint8_t> &outBytes ) {
    std::ifstream stream( path, std::ios::binary | std::ios::ate );
    if ( !stream.is_open() ) {
        return false;
    }
    const std::streamsize size = stream.tellg();
    if ( size <= 0 ) {
        return false;
    }
    stream.seekg( 0, std::ios::beg );
    outBytes.resize( static_cast<size_t>( size ) );
    return static_cast<bool>(
        stream.read( reinterpret_cast<char *>( outBytes.data() ), size ) );
}

// Lowest set bit (shift) and the number of set bits (width) of a channel mask.
void MaskRange( uint32_t mask, int32_t &outShift, int32_t &outBits ) {
    outShift = 0;
    outBits = 0;
    if ( mask == 0 ) {
        return;
    }
    while ( ( ( mask >> outShift ) & 1u ) == 0 ) {
        outShift++;
    }
    for ( uint32_t value = mask >> outShift; ( value & 1u ) != 0; value >>= 1 ) {
        outBits++;
    }
}

uint8_t ExpandChannel( uint32_t value, int32_t shift, int32_t bits ) {
    if ( bits <= 0 ) {
        return 255;
    }
    const uint32_t raw = ( value >> shift ) & ( ( 1u << bits ) - 1u );
    return static_cast<uint8_t>( ( raw * 255u + ( ( 1u << bits ) - 1u ) / 2u ) / ( ( 1u << bits ) - 1u ) );
}
} // namespace

// DDS: DXT1 / DXT3 / DXT5 through the Eden decoder, uncompressed through the
// pixel format masks (32 / 24 / 16 / 8 bpp). Mip 0 only.
static bool DecodeDds( const uint8_t *pData, size_t size, std::vector<uint8_t> &outPixels, uint32_t &outWidth,
                       uint32_t &outHeight ) {
    if ( size < 4 + sizeof( DdsHeader ) || memcmp( pData, "DDS ", 4 ) != 0 ) {
        return false;
    }
    DdsHeader header;
    memcpy( &header, pData + 4, sizeof( header ) );
    if ( header.Size != sizeof( DdsHeader ) || header.Width == 0 || header.Height == 0 ) {
        OTDU_LOG_WARN( "Image: bad DDS header\n" );
        return false;
    }
    const uint8_t *pTexels = pData + 4 + sizeof( header );
    const size_t texelsSize = size - ( 4 + sizeof( header ) );
    outWidth = header.Width;
    outHeight = header.Height;
    outPixels.assign( static_cast<size_t>( header.Width ) * header.Height * 4, 255 );

    eViewFormat format = VF_Invalid;
    switch ( header.PixelFormat.FourCC ) {
    case DdsFourCC( 'D', 'X', 'T', '1' ):
        format = VF_DXT1;
        break;
    case DdsFourCC( 'D', 'X', 'T', '3' ):
        format = VF_DXT3;
        break;
    case DdsFourCC( 'D', 'X', 'T', '5' ):
        format = VF_DXT5;
        break;
    default:
        break;
    }
    if ( format != VF_Invalid ) {
        const uint32_t needed = CalcMipSize( header.Width, header.Height, format );
        if ( texelsSize < needed ) {
            OTDU_LOG_WARN( "Image: DDS truncated (%zu of %u bytes)\n", texelsSize, needed );
            return false;
        }
        DecodeBlockCompressed( pTexels, header.Width, header.Height, format, outPixels.data() );
        return true;
    }
    // Uncompressed: expand every channel from its mask.
    const uint32_t bitCount = header.PixelFormat.RGBBitCount;
    if ( header.PixelFormat.FourCC != 0 ||
         ( bitCount != 8 && bitCount != 16 && bitCount != 24 && bitCount != 32 ) ) {
        OTDU_LOG_WARN( "Image: unsupported DDS format (FourCC 0x%08x, %u bpp)\n", header.PixelFormat.FourCC, bitCount );
        return false;
    }
    const size_t bytesPerPixel = bitCount / 8;
    if ( texelsSize < static_cast<size_t>( header.Width ) * header.Height * bytesPerPixel ) {
        OTDU_LOG_WARN( "Image: DDS truncated\n" );
        return false;
    }
    int32_t rShift = 0, rBits = 0, gShift = 0, gBits = 0, bShift = 0, bBits = 0, aShift = 0, aBits = 0;
    MaskRange( header.PixelFormat.RMask, rShift, rBits );
    MaskRange( header.PixelFormat.GMask, gShift, gBits );
    MaskRange( header.PixelFormat.BMask, bShift, bBits );
    MaskRange( header.PixelFormat.AMask, aShift, aBits );
    for ( size_t pixel = 0; pixel < static_cast<size_t>( header.Width ) * header.Height; pixel++ ) {
        uint32_t value = 0;
        memcpy( &value, pTexels + pixel * bytesPerPixel, bytesPerPixel );
        uint8_t *pOut = outPixels.data() + pixel * 4;
        pOut[0] = ExpandChannel( value, rShift, rBits );
        pOut[1] = ExpandChannel( value, gShift, gBits );
        pOut[2] = ExpandChannel( value, bShift, bBits );
        pOut[3] = header.PixelFormat.AMask != 0 ? ExpandChannel( value, aShift, aBits ) : 255;
    }
    return true;
}

bool LoadImageFileRGBA( const std::filesystem::path &path, std::vector<uint8_t> &outPixels, uint32_t &outWidth,
                        uint32_t &outHeight ) {
    outPixels.clear();
    outWidth = outHeight = 0;
    std::vector<uint8_t> bytes;
    if ( !ReadFileBytes( path, bytes ) ) {
        OTDU_LOG_WARN( "Image: cannot read '%s'\n", path.string().c_str() );
        return false;
    }
    if ( bytes.size() >= 4 && memcmp( bytes.data(), "DDS ", 4 ) == 0 ) {
        if ( !DecodeDds( bytes.data(), bytes.size(), outPixels, outWidth, outHeight ) ) {
            OTDU_LOG_WARN( "Image: '%s' not decoded\n", path.filename().string().c_str() );
            return false;
        }
        return true;
    }
    int width = 0, height = 0, channels = 0;
    stbi_uc *pImage = stbi_load_from_memory( bytes.data(), static_cast<int>( bytes.size() ), &width, &height, &channels, 4 );
    if ( pImage == nullptr ) {
        OTDU_LOG_WARN( "Image: '%s' not decoded (%s)\n", path.filename().string().c_str(), stbi_failure_reason() );
        return false;
    }
    outWidth = static_cast<uint32_t>( width );
    outHeight = static_cast<uint32_t>( height );
    outPixels.assign( pImage, pImage + static_cast<size_t>( width ) * height * 4 );
    stbi_image_free( pImage );
    return true;
}
