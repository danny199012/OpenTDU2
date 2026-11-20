#include "shared.h"
#include "render_file_write.h"

#include <cstring>

namespace {

void AppendU16( std::vector<uint8_t> &out, uint16_t value ) {
    out.push_back( static_cast<uint8_t>( value & 0xff ) );
    out.push_back( static_cast<uint8_t>( value >> 8 ) );
}

void AppendU32( std::vector<uint8_t> &out, uint32_t value ) {
    for ( uint32_t i = 0; i < 4; i++ ) {
        out.push_back( static_cast<uint8_t>( ( value >> ( i * 8 ) ) & 0xff ) );
    }
}

uint32_t ReadU32( const uint8_t *pData ) {
    uint32_t value;
    memcpy( &value, pData, 4 );
    return value;
}

uint16_t ReadU16( const uint8_t *pData ) {
    uint16_t value;
    memcpy( &value, pData, 2 );
    return value;
}

// Emits one section: header, own payload, children (recursively), trailing.
void WriteSection( std::vector<uint8_t> &out, const RenderSectionWrite &section ) {
    // The Size accounting: 0x10 header + payload + the children's bytes +
    // trailing. DataSize covers the payload + trailing (the child region
    // starts after it - VisitSections' law).
    std::vector<uint8_t> body;
    body.reserve( section.Payload.size() + section.Trailing.size() + 0x40 );
    for ( const RenderSectionWrite &child : section.Children ) {
        WriteSection( body, child );
    }
    // DataSize is measured from the SECTION START (the child region begins
    // at DataSize, VisitSections' law), so the own payload counts after the
    // 0x10 header.
    const uint32_t dataSize =
        0x10 + static_cast<uint32_t>( section.Payload.size() + section.Trailing.size() );
    const uint32_t size = dataSize + static_cast<uint32_t>( body.size() );
    AppendU32( out, section.Type );
    AppendU16( out, section.VersionMajor );
    AppendU16( out, section.VersionMinor );
    AppendU32( out, size );
    AppendU32( out, dataSize );
    out.insert( out.end(), section.Payload.begin(), section.Payload.end() );
    out.insert( out.end(), body.begin(), body.end() );
    out.insert( out.end(), section.Trailing.begin(), section.Trailing.end() );
}

bool ParseRange( const uint8_t *pFile, uint32_t begin, uint32_t end, int32_t depth,
                 std::vector<RenderSectionWrite> &out, std::string &error ) {
    if ( depth > 32 ) {
        error = "section tree deeper than 32";
        return false;
    }
    uint32_t offset = begin;
    while ( offset + 0x10 <= end ) {
        RenderSectionWrite section;
        section.Type = ReadU32( pFile + offset );
        section.VersionMajor = ReadU16( pFile + offset + 4 );
        section.VersionMinor = ReadU16( pFile + offset + 6 );
        const uint32_t size = ReadU32( pFile + offset + 8 );
        uint32_t dataSize = ReadU32( pFile + offset + 12 );
        if ( size < 0x10 || offset + size > end ) {
            error = "section size out of range";
            return false;
        }
        if ( dataSize < 0x10 ) {
            dataSize = 0x10; // VisitSections' guard
        }
        const uint32_t payloadBegin = offset + 0x10;
        const uint32_t payloadEnd = offset + size;
        if ( payloadBegin > payloadEnd ) {
            error = "section payload out of range";
            return false;
        }
        if ( size > dataSize ) {
            // Children live in the payload's tail; the own payload stops at
            // DataSize. Capture each part separately so the rebuild is
            // byte-exact whatever the DataSize says.
            const uint32_t childBegin = offset + Min( dataSize, size );
            if ( childBegin > payloadBegin ) {
                section.Payload.assign( pFile + payloadBegin, pFile + childBegin );
            }
            if ( !ParseRange( pFile, childBegin, payloadEnd, depth + 1, section.Children, error ) ) {
                return false;
            }
        } else {
            section.Payload.assign( pFile + payloadBegin, pFile + payloadEnd );
        }
        out.push_back( std::move( section ) );
        offset += size;
    }
    return true;
}

} // namespace

std::vector<uint8_t> BuildRenderFile( uint32_t magic, uint16_t versionMajor, uint16_t versionMinor,
                                      uint32_t flags, const std::vector<RenderSectionWrite> &sections ) {
    std::vector<uint8_t> body;
    for ( const RenderSectionWrite &section : sections ) {
        WriteSection( body, section );
    }
    std::vector<uint8_t> out;
    out.reserve( 0x10 + body.size() );
    AppendU16( out, versionMajor );
    AppendU16( out, versionMinor );
    AppendU32( out, flags );
    AppendU32( out, static_cast<uint32_t>( 0x10 + body.size() ) );
    AppendU32( out, magic );
    out.insert( out.end(), body.begin(), body.end() );
    return out;
}

std::vector<uint8_t> BuildRenderFile( const RenderSectionWrite &root ) {
    return BuildRenderFile( root.Type, root.VersionMajor, root.VersionMinor,
                            ReadU32( root.Payload.data() ), root.Children );
}

bool ParseRenderFileTree( const uint8_t *pFile, uint32_t fileSize, RenderSectionWrite &root, std::string &error ) {
    if ( pFile == nullptr || fileSize < 0x10 ) {
        error = "too small for a header";
        return false;
    }
    root = RenderSectionWrite();
    // The header: { u16 VMaj @0; u16 VMin @2; u32 Flags @4; u32 Size @8;
    // u32 Magic @12 } - the magic rides in root.Type, Flags/Size/Magic in
    // the root's payload (BuildRenderFile recomputes Size and Magic).
    root.Type = ReadU32( pFile + 12 );
    root.VersionMajor = ReadU16( pFile );
    root.VersionMinor = ReadU16( pFile + 2 );
    root.Payload.assign( pFile + 4, pFile + 0x10 ); // Flags, Size, Magic
    const uint32_t declaredSize = ReadU32( pFile + 8 ); // the header's Size field
    const uint32_t end = Min( declaredSize, fileSize );
    if ( end < 0x10 ) {
        error = "header size field too small";
        return false;
    }
    if ( !ParseRange( pFile, 0x10, end, 0, root.Children, error ) ) {
        return false;
    }
    // Trailing bytes past the declared size (never in retail; captured so a
    // roundtrip of a padded file stays exact).
    if ( fileSize > end ) {
        root.Trailing.assign( pFile + end, pFile + fileSize );
    }
    return true;
}
