#include "tuner_install.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace Launcher {
namespace {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// little endian helpers + files
// ---------------------------------------------------------------------------

uint32_t U32( const Bytes &b, size_t o ) {
    uint32_t v = 0;
    std::memcpy( &v, b.data() + o, 4 );
    return v;
}
int32_t I32( const Bytes &b, size_t o ) { return static_cast<int32_t>( U32( b, o ) ); }
uint16_t U16( const Bytes &b, size_t o ) {
    uint16_t v = 0;
    std::memcpy( &v, b.data() + o, 2 );
    return v;
}
void PutU32( Bytes &b, size_t o, uint32_t v ) { std::memcpy( b.data() + o, &v, 4 ); }
void PutU16( Bytes &b, size_t o, uint16_t v ) { std::memcpy( b.data() + o, &v, 2 ); }
void PutF32( Bytes &b, size_t o, float v ) { std::memcpy( b.data() + o, &v, 4 ); }
float F32( const Bytes &b, size_t o ) {
    float v = 0.0f;
    std::memcpy( &v, b.data() + o, 4 );
    return v;
}

bool ReadAll( const fs::path &path, Bytes &out ) {
    std::ifstream in( path, std::ios::binary );
    if ( !in ) {
        return false;
    }
    in.seekg( 0, std::ios::end );
    const std::streamoff size = in.tellg();
    in.seekg( 0, std::ios::beg );
    out.resize( static_cast<size_t>( size ) );
    if ( size > 0 ) {
        in.read( reinterpret_cast<char *>( out.data() ), size );
    }
    return static_cast<bool>( in );
}

bool WriteAll( const fs::path &path, const Bytes &data ) {
    std::error_code ec;
    fs::create_directories( path.parent_path(), ec );
    // Written beside, then renamed: a half-written bank is never left under its real name.
    const fs::path temp = path.wstring() + L".tmp";
    {
        std::ofstream out( temp, std::ios::binary | std::ios::trunc );
        if ( !out ) {
            return false;
        }
        if ( !data.empty() ) {
            out.write( reinterpret_cast<const char *>( data.data() ), static_cast<std::streamsize>( data.size() ) );
        }
        if ( !out ) {
            return false;
        }
    }
    fs::rename( temp, path, ec );
    if ( ec ) {
        fs::remove( path, ec );
        ec.clear();
        fs::rename( temp, path, ec );
    }
    return !ec;
}

std::string Lower( std::string s ) {
    std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return s;
}

size_t Align( size_t value, size_t alignment ) { return ( value + alignment - 1 ) / alignment * alignment; }

// ---------------------------------------------------------------------------
// the bank (PC, `KNAB`): parse, and rebuild with new payloads in the SAME layout
// (tools/x360/eden_bank.py + build_bank.py). Only payloads change - the entry
// count, names and directory are the source bank's, so the table / types /
// directory / indexes sections are copied byte for byte and only the table
// records, the data section and the two size fields are rewritten.
// ---------------------------------------------------------------------------

struct BankEntry {
    std::string Path;     // decoded: dir\name.ext
    std::string Filename; // lower case
    int32_t Offset = 0, Size = 0, Date = 0, Crc = 0, Alignment = 0;
};

struct Bank {
    Bytes Data;
    std::vector<BankEntry> Entries;
    int32_t TableOffset = 0, DataOffset = 0;
};

std::string DecodeBankPath( const std::string &encoded ) {
    std::string result, pending;
    size_t start = 0;
    while ( start <= encoded.size() ) {
        size_t end = encoded.find( '\\', start );
        if ( end == std::string::npos ) {
            end = encoded.size();
        }
        const std::string component = encoded.substr( start, end - start );
        if ( !component.empty() && component[0] == '.' ) {
            pending = component;
        } else if ( !component.empty() ) {
            if ( !result.empty() ) {
                result += "\\";
            }
            result += component;
        }
        start = end + 1;
    }
    return result + pending;
}

// The path tree -> encoded paths in directory order (eden_bank._parse_directory).
void ParseDirectory( const Bytes &data, size_t start, size_t end, std::vector<std::string> &paths ) {
    struct Walker {
        const Bytes &Data;
        size_t End;
        std::vector<std::string> &Paths;
        bool Node( size_t &p, const std::string &prefix ) {
            if ( p >= End ) {
                return false;
            }
            const int8_t length = static_cast<int8_t>( Data[p++] );
            if ( length < 0 ) {
                size_t nameLength = static_cast<size_t>( -static_cast<int>( length ) );
                if ( length == -0x80 ) {
                    nameLength = static_cast<size_t>( Data[p++] ) + 0x80;
                }
                uint32_t children = 0;
                int shift = 0;
                while ( p < End ) {
                    const int8_t byte = static_cast<int8_t>( Data[p++] );
                    children |= static_cast<uint32_t>( byte & 0x7F ) << shift;
                    shift += 7;
                    if ( byte >= 0 ) {
                        break;
                    }
                }
                if ( p + nameLength > Data.size() ) {
                    return false;
                }
                const std::string name( reinterpret_cast<const char *>( Data.data() + p ), nameLength );
                p += nameLength;
                const std::string childPrefix = prefix + name + "\\";
                for ( uint32_t i = 0; i < children; i++ ) {
                    if ( !Node( p, childPrefix ) ) {
                        return false;
                    }
                }
                return true;
            }
            size_t nameLength = static_cast<size_t>( length );
            if ( length == 0x7F ) {
                nameLength = static_cast<size_t>( Data[p++] ) + 0x7F;
            }
            if ( p + nameLength > Data.size() ) {
                return false;
            }
            Paths.push_back( prefix + std::string( reinterpret_cast<const char *>( Data.data() + p ), nameLength ) );
            p += nameLength;
            return true;
        }
    };
    Walker walker{ data, end, paths };
    size_t p = start;
    while ( p < end && data[p] != 0 ) {
        if ( !walker.Node( p, "" ) ) {
            break;
        }
    }
}

bool ParseBank( Bytes data, Bank &bank, std::string &error ) {
    if ( data.size() < 0x48 || std::memcmp( data.data() + 8, "KNAB", 4 ) != 0 ) {
        error = "not a PC bank";
        return false;
    }
    const int32_t count = I32( data, 0x28 );
    const int32_t tableSection = I32( data, 0x30 );
    const int32_t dirSection = I32( data, 0x38 );
    const int32_t indexSection = I32( data, 0x3C );
    const int32_t dataOffset = I32( data, 0x44 );
    if ( count <= 0 || tableSection < 8 || dirSection < 8 || indexSection < 8 ||
         static_cast<size_t>( tableSection ) + 8 + static_cast<size_t>( count + 1 ) * 0x14 > data.size() ||
         dataOffset <= 0 || static_cast<size_t>( dataOffset ) > data.size() ) {
        error = "unsupported bank layout";
        return false;
    }
    bank.Entries.assign( static_cast<size_t>( count ), BankEntry() );
    const size_t table = static_cast<size_t>( tableSection ) + 8;
    for ( int32_t i = 0; i < count; i++ ) {
        BankEntry &entry = bank.Entries[static_cast<size_t>( i )];
        const size_t rec = table + static_cast<size_t>( i ) * 0x14;
        entry.Offset = I32( data, rec );
        entry.Size = I32( data, rec + 4 );
        entry.Date = I32( data, rec + 8 );
        entry.Crc = I32( data, rec + 12 );
        entry.Alignment = I32( data, rec + 16 );
        if ( entry.Offset < 0 || entry.Size < 0 || static_cast<size_t>( entry.Offset ) + static_cast<size_t>( entry.Size ) > data.size() ) {
            error = "bank entry out of range";
            return false;
        }
    }
    const size_t dirSize = U32( data, static_cast<size_t>( dirSection ) );
    const size_t dirStart = static_cast<size_t>( dirSection ) + 8;
    const size_t dirEnd = dirStart + std::min( dirSize, data.size() - dirStart );
    std::vector<std::string> paths;
    ParseDirectory( data, dirStart, dirEnd, paths );
    const size_t indexes = static_cast<size_t>( indexSection ) + 8;
    for ( size_t order = 0; order < paths.size() && order < static_cast<size_t>( count ); order++ ) {
        size_t index = 0;
        if ( count < 0x100 ) {
            index = data[indexes + order];
        } else if ( count < 0x10000 ) {
            index = U16( data, indexes + order * 2 );
        } else {
            index = U32( data, indexes + order * 4 );
        }
        if ( index >= static_cast<size_t>( count ) ) {
            continue;
        }
        BankEntry &entry = bank.Entries[index];
        entry.Path = DecodeBankPath( paths[order] );
        const size_t slash = entry.Path.find_last_of( '\\' );
        entry.Filename = Lower( entry.Path.substr( slash == std::string::npos ? 0 : slash + 1 ) );
    }
    bank.TableOffset = tableSection;
    bank.DataOffset = dataOffset;
    bank.Data = std::move( data );
    return true;
}

Bytes Payload( const Bank &bank, size_t index ) {
    const BankEntry &entry = bank.Entries[index];
    return Bytes( bank.Data.begin() + entry.Offset, bank.Data.begin() + entry.Offset + entry.Size );
}

// `replacements`: entry index -> new payload. Everything else is copied.
Bytes RebuildBank( const Bank &bank, const std::map<size_t, Bytes> &replacements ) {
    const size_t count = bank.Entries.size();
    Bytes out( bank.Data.begin(), bank.Data.begin() + bank.DataOffset );
    size_t offset = static_cast<size_t>( bank.DataOffset );
    const size_t table = static_cast<size_t>( bank.TableOffset ) + 8;
    Bytes payloads;
    std::vector<std::pair<size_t, size_t>> placed; // offset, size
    for ( size_t i = 0; i < count; i++ ) {
        const auto replacement = replacements.find( i );
        const Bytes *pSource = replacement != replacements.end() ? &replacement->second : nullptr;
        const size_t size = pSource != nullptr ? pSource->size() : static_cast<size_t>( bank.Entries[i].Size );
        placed.emplace_back( offset, size );
        const size_t padded = Align( size, 16 );
        const size_t at = payloads.size();
        payloads.resize( at + padded, 0 );
        if ( pSource != nullptr ) {
            std::memcpy( payloads.data() + at, pSource->data(), size );
        } else if ( size > 0 ) {
            std::memcpy( payloads.data() + at, bank.Data.data() + bank.Entries[i].Offset, size );
        }
        offset += padded;
    }
    const size_t endOfData = offset;
    for ( size_t i = 0; i < count; i++ ) {
        const size_t rec = table + i * 0x14;
        PutU32( out, rec, static_cast<uint32_t>( placed[i].first ) );
        PutU32( out, rec + 4, static_cast<uint32_t>( placed[i].second ) );
        // date / crc / alignment: the source's (the engine never verifies the crc)
        if ( replacements.count( i ) != 0 ) {
            PutU32( out, rec + 12, 0 );
        }
    }
    {
        const size_t rec = table + count * 0x14; // the last record is the data section
        PutU32( out, rec, static_cast<uint32_t>( endOfData ) );
        PutU32( out, rec + 4, 0 );
    }
    PutU32( out, 0x18, static_cast<uint32_t>( Align( endOfData, 16 ) ) ); // Size
    PutU32( out, 0x1C, static_cast<uint32_t>( endOfData ) );              // SizeWithoutPadding
    out.insert( out.end(), payloads.begin(), payloads.end() );
    return out;
}

// ---------------------------------------------------------------------------
// textures (.2db): PNG -> uncompressed sRGB A8R8G8B8 with a full mip chain, and the DXT dimmer
// (make_tuner_interior.png_to_2db / dxt_darken)
// ---------------------------------------------------------------------------

struct Image {
    uint32_t Width = 0, Height = 0;
    std::vector<uint8_t> Rgba;
};

bool DecodePng( const fs::path &path, Image &image ) {
    const HRESULT initResult = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
    const bool bOwnCom = SUCCEEDED( initResult );
    bool bOk = false;
    IWICImagingFactory *pFactory = nullptr;
    IWICBitmapDecoder *pDecoder = nullptr;
    IWICBitmapFrameDecode *pFrame = nullptr;
    IWICFormatConverter *pConverter = nullptr;
    do {
        if ( FAILED( CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &pFactory ) ) ) ) {
            break;
        }
        if ( FAILED( pFactory->CreateDecoderFromFilename( path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder ) ) ) {
            break;
        }
        if ( FAILED( pDecoder->GetFrame( 0, &pFrame ) ) || FAILED( pFactory->CreateFormatConverter( &pConverter ) ) ) {
            break;
        }
        if ( FAILED( pConverter->Initialize( pFrame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                             WICBitmapPaletteTypeCustom ) ) ) {
            break;
        }
        UINT w = 0, h = 0;
        if ( FAILED( pConverter->GetSize( &w, &h ) ) || w == 0 || h == 0 || w > 8192 || h > 8192 ) {
            break;
        }
        image.Width = w;
        image.Height = h;
        image.Rgba.resize( static_cast<size_t>( w ) * h * 4 );
        if ( FAILED( pConverter->CopyPixels( nullptr, w * 4, static_cast<UINT>( image.Rgba.size() ), image.Rgba.data() ) ) ) {
            break;
        }
        bOk = true;
    } while ( false );
    if ( pConverter ) pConverter->Release();
    if ( pFrame ) pFrame->Release();
    if ( pDecoder ) pDecoder->Release();
    if ( pFactory ) pFactory->Release();
    if ( bOwnCom ) CoUninitialize();
    return bOk;
}

// Bilinear resize (the shipped art is authored at the texture's size, so this is the exception).
Image Resize( const Image &src, uint32_t w, uint32_t h ) {
    if ( src.Width == w && src.Height == h ) {
        return src;
    }
    Image out;
    out.Width = w;
    out.Height = h;
    out.Rgba.resize( static_cast<size_t>( w ) * h * 4 );
    for ( uint32_t y = 0; y < h; y++ ) {
        const float fy = ( y + 0.5f ) * src.Height / h - 0.5f;
        const int y0 = std::max( 0, static_cast<int>( std::floor( fy ) ) );
        const int y1 = std::min<int>( src.Height - 1, y0 + 1 );
        const float ty = std::clamp( fy - y0, 0.0f, 1.0f );
        for ( uint32_t x = 0; x < w; x++ ) {
            const float fx = ( x + 0.5f ) * src.Width / w - 0.5f;
            const int x0 = std::max( 0, static_cast<int>( std::floor( fx ) ) );
            const int x1 = std::min<int>( src.Width - 1, x0 + 1 );
            const float tx = std::clamp( fx - x0, 0.0f, 1.0f );
            for ( int c = 0; c < 4; c++ ) {
                const float a = src.Rgba[( static_cast<size_t>( y0 ) * src.Width + x0 ) * 4 + c] * ( 1 - tx ) +
                                src.Rgba[( static_cast<size_t>( y0 ) * src.Width + x1 ) * 4 + c] * tx;
                const float b = src.Rgba[( static_cast<size_t>( y1 ) * src.Width + x0 ) * 4 + c] * ( 1 - tx ) +
                                src.Rgba[( static_cast<size_t>( y1 ) * src.Width + x1 ) * 4 + c] * tx;
                out.Rgba[( static_cast<size_t>( y ) * w + x ) * 4 + c] = static_cast<uint8_t>( a * ( 1 - ty ) + b * ty + 0.5f );
            }
        }
    }
    return out;
}

// 2x2 box filter to the next mip.
Image Halve( const Image &src ) {
    const uint32_t w = std::max( 1u, src.Width / 2 ), h = std::max( 1u, src.Height / 2 );
    Image out;
    out.Width = w;
    out.Height = h;
    out.Rgba.resize( static_cast<size_t>( w ) * h * 4 );
    for ( uint32_t y = 0; y < h; y++ ) {
        for ( uint32_t x = 0; x < w; x++ ) {
            for ( int c = 0; c < 4; c++ ) {
                uint32_t sum = 0;
                for ( uint32_t dy = 0; dy < 2; dy++ ) {
                    for ( uint32_t dx = 0; dx < 2; dx++ ) {
                        const uint32_t sx = std::min( src.Width - 1, x * 2 + dx ), sy = std::min( src.Height - 1, y * 2 + dy );
                        sum += src.Rgba[( static_cast<size_t>( sy ) * src.Width + sx ) * 4 + c];
                    }
                }
                out.Rgba[( static_cast<size_t>( y ) * w + x ) * 4 + c] = static_cast<uint8_t>( ( sum + 2 ) / 4 );
            }
        }
    }
    return out;
}

// An uncompressed sRGB A8R8G8B8 .2db cloned from the original texture's 0x50-byte header (the name hash
// at +0x20 stays), at the original's dimensions, opaque, with a full mip chain.
Bytes PngTo2db( const Bytes &original, const Image &source ) {
    const uint16_t w = U16( original, 0x28 ), h = U16( original, 0x2A );
    Image level = Resize( source, w, h );
    for ( size_t i = 3; i < level.Rgba.size(); i += 4 ) {
        level.Rgba[i] = 255;
    }
    Bytes payload;
    int mips = 0;
    while ( true ) {
        const size_t at = payload.size();
        payload.resize( at + static_cast<size_t>( level.Width ) * level.Height * 4 );
        for ( size_t p = 0; p < static_cast<size_t>( level.Width ) * level.Height; p++ ) {
            payload[at + p * 4 + 0] = level.Rgba[p * 4 + 2]; // B
            payload[at + p * 4 + 1] = level.Rgba[p * 4 + 1]; // G
            payload[at + p * 4 + 2] = level.Rgba[p * 4 + 0]; // R
            payload[at + p * 4 + 3] = level.Rgba[p * 4 + 3]; // A
        }
        mips++;
        if ( level.Width == 1 && level.Height == 1 ) {
            break;
        }
        level = Halve( level );
    }
    Bytes out( original.begin(), original.begin() + 0x50 );
    PutU16( out, 0x28, w );
    PutU16( out, 0x2A, h );
    out[0x2E] = static_cast<uint8_t>( mips );
    out[0x2F] = static_cast<uint8_t>( mips );
    PutU32( out, 0x3C, 31 ); // VF_SRGB_A8R8G8B8
    const uint32_t total = static_cast<uint32_t>( 0x50 + payload.size() );
    PutU32( out, 0x08, total );
    PutU32( out, 0x18, total - 0x10 );
    PutU32( out, 0x1C, total - 0x10 );
    out.insert( out.end(), payload.begin(), payload.end() );
    return out;
}

struct Rgb {
    int R, G, B;
};

Rgb Expand565( uint16_t c ) {
    const int r = ( c >> 11 ) & 31, g = ( c >> 5 ) & 63, b = c & 31;
    return { ( r << 3 ) | ( r >> 2 ), ( g << 2 ) | ( g >> 4 ), ( b << 3 ) | ( b >> 2 ) };
}

// Dims one DXT colour block in place: decode its 16 texels, scale the RGB, re-encode with
// RGB565 endpoints at the brightest / darkest texel (the alpha block of a DXT5 is left as is).
void DarkenColourBlock( uint8_t *pBlock, const float scales[3] ) {
    uint16_t c0, c1;
    std::memcpy( &c0, pBlock, 2 );
    std::memcpy( &c1, pBlock + 2, 2 );
    uint32_t bits;
    std::memcpy( &bits, pBlock + 4, 4 );
    Rgb palette[4];
    palette[0] = Expand565( c0 );
    palette[1] = Expand565( c1 );
    if ( c0 > c1 ) {
        palette[2] = { ( 2 * palette[0].R + palette[1].R ) / 3, ( 2 * palette[0].G + palette[1].G ) / 3, ( 2 * palette[0].B + palette[1].B ) / 3 };
        palette[3] = { ( palette[0].R + 2 * palette[1].R ) / 3, ( palette[0].G + 2 * palette[1].G ) / 3, ( palette[0].B + 2 * palette[1].B ) / 3 };
    } else {
        palette[2] = { ( palette[0].R + palette[1].R ) / 2, ( palette[0].G + palette[1].G ) / 2, ( palette[0].B + palette[1].B ) / 2 };
        palette[3] = { 0, 0, 0 };
    }
    Rgb px[16];
    for ( int i = 0; i < 16; i++ ) {
        const Rgb &src = palette[( bits >> ( 2 * i ) ) & 3];
        px[i] = { std::min( 255, static_cast<int>( src.R * scales[0] ) ), std::min( 255, static_cast<int>( src.G * scales[1] ) ),
                  std::min( 255, static_cast<int>( src.B * scales[2] ) ) };
    }
    int luma[16];
    int hi = 0, lo = 0;
    for ( int i = 0; i < 16; i++ ) {
        luma[i] = px[i].R * 2 + px[i].G * 3 + px[i].B;
        if ( luma[i] > luma[hi] ) hi = i;
        if ( luma[i] < luma[lo] ) lo = i;
    }
    auto rgb565 = []( const Rgb &c ) { return static_cast<uint16_t>( ( ( c.R >> 3 ) << 11 ) | ( ( c.G >> 2 ) << 5 ) | ( c.B >> 3 ) ); };
    uint16_t e0 = rgb565( px[hi] ), e1 = rgb565( px[lo] );
    if ( e0 < e1 ) {
        std::swap( hi, lo );
        std::swap( e0, e1 );
    }
    const Rgb a = px[hi], b = px[lo];
    const Rgb pal[4] = { a, b, { ( 2 * a.R + b.R ) / 3, ( 2 * a.G + b.G ) / 3, ( 2 * a.B + b.B ) / 3 },
                         { ( a.R + 2 * b.R ) / 3, ( a.G + 2 * b.G ) / 3, ( a.B + 2 * b.B ) / 3 } };
    uint32_t outBits = 0;
    for ( int i = 0; i < 16; i++ ) {
        int best = 0, bestDistance = 1 << 30;
        for ( int k = 0; k < 4; k++ ) {
            const int dr = px[i].R - pal[k].R, dg = px[i].G - pal[k].G, db = px[i].B - pal[k].B;
            const int distance = dr * dr + dg * dg + db * db;
            if ( distance < bestDistance ) {
                bestDistance = distance;
                best = k;
            }
        }
        outBits |= static_cast<uint32_t>( best ) << ( 2 * i );
    }
    std::memcpy( pBlock, &e0, 2 );
    std::memcpy( pBlock + 2, &e1, 2 );
    std::memcpy( pBlock + 4, &outBits, 4 );
}

// Dims a PC DXT1 / DXT5 .2db with its byte layout untouched (every mip of every face; a cube map's
// packed tail mips that overrun the payload keep their bytes). A non-DXT texture is returned as is.
Bytes DarkenDxtTexture( const Bytes &blob, float r, float g, float b ) {
    const uint32_t format = U32( blob, 0x3C );
    if ( format != 51 && format != 55 ) {
        return blob;
    }
    const float scales[3] = { r, g, b };
    const bool bDxt5 = format == 55;
    const size_t blockSize = bDxt5 ? 16 : 8;
    const uint16_t w = U16( blob, 0x28 ), h = U16( blob, 0x2A );
    const size_t faces = std::max<size_t>( 1, blob[0x2C] );
    struct Mip {
        uint32_t W, H;
        size_t Size;
    };
    std::vector<Mip> chain;
    for ( uint32_t mw = w, mh = h;; ) {
        chain.push_back( { mw, mh, static_cast<size_t>( ( mw + 3 ) / 4 ) * ( ( mh + 3 ) / 4 ) * blockSize } );
        if ( mw == 1 && mh == 1 ) {
            break;
        }
        mw = std::max( 1u, mw / 2 );
        mh = std::max( 1u, mh / 2 );
    }
    size_t faceSize = 0;
    for ( const Mip &mip : chain ) {
        faceSize += mip.Size;
    }
    Bytes out = blob;
    for ( size_t face = 0; face < faces; face++ ) {
        const size_t base = 0x50 + face * faceSize;
        size_t offset = 0;
        for ( const Mip &mip : chain ) {
            if ( mip.W >= 4 && mip.H >= 4 && base + offset + mip.Size <= out.size() ) {
                const uint32_t bw = mip.W / 4, bh = mip.H / 4;
                for ( uint32_t blockIndex = 0; blockIndex < bw * bh; blockIndex++ ) {
                    DarkenColourBlock( out.data() + base + offset + blockIndex * blockSize + ( bDxt5 ? 8 : 0 ), scales );
                }
            }
            offset += mip.Size;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// .2dm material libraries: Diffuse (+0xB0) and Ambient (+0xC0) float4 of every MAT scaled
// (eden_2dm._visit_sections + make_tuner_interior.darken_materials)
// ---------------------------------------------------------------------------

constexpr size_t kHeader = 0x10, kSection = 0x10;
constexpr uint32_t kMagic2dm = 0x4D44322E, kMat = 0x2E54414D;

void DarkenMaterials( Bytes &data, double scale ) {
    if ( data.size() < kHeader || U32( data, 0x0C ) != kMagic2dm ) {
        return;
    }
    const size_t headerSize = std::min<size_t>( U32( data, 0x08 ), data.size() );
    struct Visitor {
        Bytes &Data;
        double Scale;
        void Range( size_t begin, size_t end ) {
            size_t offset = begin;
            while ( offset + kSection <= end ) {
                const size_t size = U32( Data, offset + 8 );
                if ( size < kSection || offset + size > end ) {
                    return;
                }
                const size_t dataSize = std::max<size_t>( U32( Data, offset + 12 ), kSection );
                if ( U32( Data, offset ) == kMat && size - kSection >= 0x340 ) {
                    const size_t payload = offset + kSection;
                    for ( size_t field : { size_t( 0xB0 ), size_t( 0xC0 ) } ) {
                        for ( size_t c = 0; c < 3; c++ ) {
                            const size_t at = payload + field + c * 4;
                            PutF32( Data, at, static_cast<float>( F32( Data, at ) * Scale ) );
                        }
                    }
                }
                if ( size > dataSize ) {
                    Range( offset + dataSize, offset + size );
                }
                offset += size;
            }
        }
    };
    Visitor visitor{ data, scale };
    visitor.Range( kHeader, headerSize );
}

// ---------------------------------------------------------------------------
// the room recipes (make_tuner_interior.py / make_revyard_interior.py)
// ---------------------------------------------------------------------------

struct Swap {
    const char *Entry;
    const char *Art;
};
struct Scale {
    const char *Entry;
    float R, G, B;
};

// Wangan Works: the tuning shop (itun_a), our art from interior\art.
const Swap kSwapsA[] = {
    { "wall_d.2db", "wall_graffiti.png" },       { "poster2_d.2db", "poster_neon.png" },   { "flare1_d.2db", "flare_neon_a.png" },
    { "flare2_d.2db", "flare_neon_b.png" },      { "flare3_d.2db", "flare_neon_a.png" },   { "glow_d.2db", "glow_neon.png" },
    { "tole_d.2db", "roof_dark.png" },           { "marble_floor_d.2db", "floor_dark.png" }, { "trame_d.2db", "grid_neon.png" },
    { "nuancier_c.2db", "nuancier_dark.png" },   { "tv1_d.2db", "screen_neon.png" },      { "steer_d.2db", "frame_neon_a.png" },
    { "dashboard_d.2db", "frame_neon_b.png" },   { "board_d.2db", "frame_neon_c.png" },   { "jante_d.2db", "frame_neon_d.png" },
    { "jante2_d.2db", "frame_neon_a.png" },      { "disc2_d.2db", "frame_neon_e.png" },    { "pneu_d.2db", "frame_neon_f.png" },
    { "coversmag_d.2db", "frame_neon_g.png" },
};
const Scale kDimA[] = { { "lm_itun_night.2db", 0.42f, 0.46f, 0.60f }, { "lm_itun.2db", 0.55f, 0.58f, 0.68f }, { "cubecar.2db", 0.30f, 0.34f, 0.45f } };
constexpr double kDarkenA = 0.55;

// Rev Yard: the used-car dealer (icdc_wld), our art from interior\art_b, the originals tinted.
const Swap kSwapsB[] = {
    { "marble_02_d.2db", "floor_tiles.png" },
    { "newlogosquare.2db", "sign_revyard.png" },
    { "glow_neons_d.2db", "fill_magenta.png" },
    { "halo02_d.2db", "fill_cyan.png" },
};
const Scale kDimB[] = {
    { "bricks.2db", 0.34f, 0.30f, 0.40f },      { "stone_wall_d.2db", 0.45f, 0.45f, 0.55f },  { "concrete_d.2db", 0.45f, 0.45f, 0.55f },
    { "y_concrete01_d.2db", 0.30f, 0.31f, 0.40f }, { "planks_02_d.2db", 0.28f, 0.28f, 0.36f }, { "ceiling_2_d.2db", 0.26f, 0.26f, 0.34f },
    { "carpet_02_d.2db", 0.55f, 0.50f, 0.65f },  { "metal_sheet_d.2db", 0.55f, 0.55f, 0.65f }, { "windows_d.2db", 0.70f, 0.75f, 0.95f },
    { "marbre.2db", 0.28f, 0.28f, 0.38f },       { "floor01_d.2db", 0.28f, 0.28f, 0.38f },    { "lm_icdc_wld.2db", 0.70f, 0.72f, 0.88f },
    { "cubecar.2db", 0.30f, 0.34f, 0.45f },
};
constexpr double kDarkenB = 0.68;

template <size_t NS, size_t ND>
bool BuildRoom( const fs::path &sourceBank, const fs::path &outBank, const fs::path &artFolder, const Swap ( &swaps )[NS],
                const Scale ( &dims )[ND], double darken, std::string &error ) {
    Bytes raw;
    if ( !ReadAll( sourceBank, raw ) ) {
        error = "cannot read " + sourceBank.string();
        return false;
    }
    Bank bank;
    if ( !ParseBank( std::move( raw ), bank, error ) ) {
        error = sourceBank.filename().string() + ": " + error;
        return false;
    }
    std::map<std::string, size_t> byName;
    for ( size_t i = 0; i < bank.Entries.size(); i++ ) {
        byName[bank.Entries[i].Filename] = i;
    }
    std::map<size_t, Bytes> replacements;
    for ( const Swap &swap : swaps ) {
        const auto it = byName.find( swap.Entry );
        if ( it == byName.end() ) {
            continue; // an entry the room does not have: skipped, like the Python tool
        }
        Image image;
        if ( !DecodePng( artFolder / swap.Art, image ) ) {
            error = "missing or unreadable art: " + ( artFolder / swap.Art ).string();
            return false;
        }
        replacements[it->second] = PngTo2db( Payload( bank, it->second ), image );
    }
    for ( const Scale &dim : dims ) {
        const auto it = byName.find( dim.Entry );
        if ( it != byName.end() && replacements.count( it->second ) == 0 ) {
            replacements[it->second] = DarkenDxtTexture( Payload( bank, it->second ), dim.R, dim.G, dim.B );
        }
    }
    for ( size_t i = 0; i < bank.Entries.size(); i++ ) {
        const std::string &name = bank.Entries[i].Filename;
        if ( name.size() > 4 && name.compare( name.size() - 4, 4, ".2dm" ) == 0 ) {
            Bytes data = Payload( bank, i );
            DarkenMaterials( data, darken );
            replacements[i] = std::move( data );
        }
    }
    const Bytes rebuilt = RebuildBank( bank, replacements );
    // Self-check: the output parses with the same entry list.
    Bank again;
    if ( !ParseBank( rebuilt, again, error ) || again.Entries.size() != bank.Entries.size() ) {
        error = "internal error: the rebuilt " + outBank.filename().string() + " does not parse";
        return false;
    }
    for ( size_t i = 0; i < bank.Entries.size(); i++ ) {
        if ( again.Entries[i].Path != bank.Entries[i].Path ) {
            error = "internal error: the rebuilt " + outBank.filename().string() + " lost entry names";
            return false;
        }
    }
    if ( !WriteAll( outBank, rebuilt ) ) {
        error = "cannot write " + outBank.string();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// spots.bin (tools/mod/add_spot.py): one 0x110-byte record per building
// ---------------------------------------------------------------------------

constexpr size_t kRecord = 0x110, kSpotHeader = 0x20, kSpotSizeAt = 0x14;

uint64_t Ident64( const std::string &name ) {
    uint8_t slots[8] = {};
    for ( size_t i = 0; i < name.size(); i++ ) {
        int c = static_cast<unsigned char>( name[i] );
        if ( c > 0x60 && c < 0x7B ) {
            c -= 0x20;
        }
        slots[i & 7] = static_cast<uint8_t>( slots[i & 7] + c );
    }
    uint64_t out = 0;
    for ( int i = 0; i < 8; i++ ) {
        out |= static_cast<uint64_t>( slots[i] ) << ( i * 8 );
    }
    return out;
}

struct SpotToAdd {
    const char *Bank; // 8 char bank field, e.g. ETNR_A
    float X, Z, Heading;
};

bool AddSpots( const fs::path &gameSpots, const fs::path &overlaySpots, const std::vector<SpotToAdd> &spots, std::string &error ) {
    Bytes data;
    std::error_code ec;
    if ( !( fs::exists( overlaySpots, ec ) && ReadAll( overlaySpots, data ) ) && !ReadAll( gameSpots, data ) ) {
        error = "cannot read " + gameSpots.string();
        return false;
    }
    if ( data.size() < kSpotHeader || std::memcmp( data.data(), ".SPT", 4 ) != 0 ) {
        error = "not a spot list: " + gameSpots.string();
        return false;
    }
    size_t size = U32( data, kSpotSizeAt );
    data.resize( kSpotHeader + std::min( size, data.size() - kSpotHeader ) );
    size = data.size() - kSpotHeader;
    auto banks = [&]() {
        std::vector<std::string> names;
        for ( size_t o = kSpotHeader; o + kRecord <= data.size(); o += kRecord ) {
            std::string bank( reinterpret_cast<const char *>( data.data() + o + 8 ), 8 );
            bank.resize( std::strlen( bank.c_str() ) );
            names.push_back( bank );
        }
        return names;
    };
    for ( const SpotToAdd &spot : spots ) {
        bool bPresent = false;
        for ( const std::string &name : banks() ) {
            bPresent = bPresent || Lower( name ) == Lower( spot.Bank );
        }
        if ( bPresent ) {
            continue;
        }
        // The height: the nearest existing spot's (the terrain there); the record template: the first shop
        // (category 4), else the first record.
        float y = 0.0f, bestDistance = 1e30f;
        size_t templateAt = kSpotHeader;
        bool bTemplate = false;
        for ( size_t o = kSpotHeader; o + kRecord <= data.size(); o += kRecord ) {
            const float dx = F32( data, o + 0x10 ) - spot.X, dz = F32( data, o + 0x18 ) - spot.Z;
            if ( dx * dx + dz * dz < bestDistance ) {
                bestDistance = dx * dx + dz * dz;
                y = F32( data, o + 0x14 );
            }
            if ( !bTemplate && data[o + 0x100] == 4 ) {
                templateAt = o;
                bTemplate = true;
            }
        }
        Bytes record( data.begin() + templateAt, data.begin() + templateAt + kRecord );
        const std::string spotName = std::string( spot.Bank ) + "_" + std::to_string( static_cast<int>( spot.X ) );
        const uint64_t id = Ident64( spotName );
        std::memcpy( record.data(), &id, 8 );
        std::memset( record.data() + 8, 0, 8 );
        std::memcpy( record.data() + 8, spot.Bank, std::min<size_t>( 8, std::strlen( spot.Bank ) ) );
        PutF32( record, 0x10, spot.X );
        PutF32( record, 0x14, y );
        PutF32( record, 0x18, spot.Z );
        PutF32( record, 0x20, spot.Heading );
        record[0x100] = 4;
        data.insert( data.end(), record.begin(), record.end() );
    }
    PutU32( data, kSpotSizeAt, static_cast<uint32_t>( data.size() - kSpotHeader ) );
    if ( !WriteAll( overlaySpots, data ) ) {
        error = "cannot write " + overlaySpots.string();
        return false;
    }
    return true;
}

bool CopyFileTo( const fs::path &from, const fs::path &to, std::string &error ) {
    Bytes data;
    if ( !ReadAll( from, data ) ) {
        error = "cannot read " + from.string();
        return false;
    }
    if ( !WriteAll( to, data ) ) {
        error = "cannot write " + to.string();
        return false;
    }
    return true;
}

} // namespace

bool TunerInstalled( const std::wstring &userData ) {
    std::ifstream in( fs::path( userData ) / L"mods" / L"tuner-install.txt" );
    std::string line;
    while ( std::getline( in, line ) ) {
        if ( line == "version=" + std::to_string( kTunerInstallVersion ) ) {
            return true;
        }
    }
    return false;
}

bool InstallTunerMod( const TunerInstallOptions &options, const TunerProgress &progress, std::string &error ) {
    const fs::path game = fs::path( options.GameRoot ) / L"Euro" / L"Bnk";
    const fs::path mods = fs::path( options.UserData ) / L"mods" / L"Euro" / L"Bnk";
    const fs::path artA = fs::path( options.PackageFolder ) / L"interior" / L"art";
    const fs::path artB = fs::path( options.PackageFolder ) / L"interior" / L"art_b";
    std::error_code ec;
    auto report = [&]( float fraction, const char *what ) { return !progress || progress( fraction, what ); };
    auto cancelled = [&]() {
        error = "cancelled";
        return false;
    };

    const fs::path itun = game / L"interior" / L"itun_a.bnk";
    const fs::path icdc = game / L"interior" / L"icdc_wld.bnk";
    const fs::path etunExterior = game / L"islands" / L"ibiza" / L"exterior" / L"etun_a.bnk";
    for ( const fs::path &needed : { itun, icdc, etunExterior, game / L"islands" / L"ibiza" / L"level" / L"spots.bin",
                                     game / L"islands" / L"hawai" / L"level" / L"spots.bin" } ) {
        if ( !fs::exists( needed, ec ) ) {
            error = "your game data is missing " + needed.string();
            return false;
        }
    }
    if ( !fs::is_directory( artA, ec ) || !fs::is_directory( artB, ec ) ) {
        error = "the tuner mod's art files are missing (" + artA.string() + ") - re-open the launcher from the OpenTDU2 folder";
        return false;
    }

    if ( !report( 0.02f, "Building Wangan Works (the tuning shop room)..." ) ) return cancelled();
    if ( !BuildRoom( itun, mods / L"interior" / L"itnr_a.bnk", artA, kSwapsA, kDimA, kDarkenA, error ) ) return false;
    if ( !report( 0.50f, "Building Rev Yard (the used-car dealer room)..." ) ) return cancelled();
    if ( !BuildRoom( icdc, mods / L"interior" / L"itnr_b.bnk", artB, kSwapsB, kDimB, kDarkenB, error ) ) return false;

    if ( !report( 0.90f, "Adding the shop spots to the islands..." ) ) return cancelled();
    const std::vector<SpotToAdd> ibiza = { { "ETNR_A", 12560.0f, 16700.0f, 1.21f }, { "ETNR_B", 12640.0f, 16640.0f, 1.21f } };
    const std::vector<SpotToAdd> hawai = { { "ETNR_A", 21160.0f, 11500.0f, 2.59f } };
    if ( !AddSpots( game / L"islands" / L"ibiza" / L"level" / L"spots.bin", mods / L"islands" / L"ibiza" / L"level" / L"spots.bin", ibiza, error ) ) return false;
    if ( !AddSpots( game / L"islands" / L"hawai" / L"level" / L"spots.bin", mods / L"islands" / L"hawai" / L"level" / L"spots.bin", hawai, error ) ) return false;
    // The shops' exteriors: copies of the tuning shop's own.
    if ( !CopyFileTo( etunExterior, mods / L"islands" / L"ibiza" / L"exterior" / L"etnr_a.bnk", error ) ) return false;
    if ( !CopyFileTo( etunExterior, mods / L"islands" / L"ibiza" / L"exterior" / L"etnr_b.bnk", error ) ) return false;
    if ( !CopyFileTo( etunExterior, mods / L"islands" / L"hawai" / L"exterior" / L"etnr_a.bnk", error ) ) return false;

    {
        std::ofstream marker( fs::path( options.UserData ) / L"mods" / L"tuner-install.txt", std::ios::trunc );
        marker << "# Written by the tuner-garage installer (tools/launcher/tuner_install.cpp). Delete to rebuild.\n"
               << "version=" << kTunerInstallVersion << "\n";
        if ( !marker ) {
            error = "cannot write the install marker";
            return false;
        }
    }
    report( 1.0f, "Done." );
    return true;
}

} // namespace Launcher
