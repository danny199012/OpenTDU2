#include "shared.h"
#include "bank_write.h"

#include <algorithm>
#include <cstring>
#include <functional>

namespace {
constexpr uint32_t kMagicBank = 0x42414E4B; // "KNAB" little endian (bytes K N A B)
// The section-gap filler: the retail files cycle the "STNICC2000
// RULEZPADDING " header string AND ITS CONTINUATION (a 28-byte gap reads
// "...RULEZPADDING DATA", the icd_ferr tail ends "...RULEZPADDING
// DATAS...-OR" - the full literal, restarting at every gap).
constexpr const char *kFiller = "STNICC2000 RULEZPADDING DATAS...-OR";
constexpr uint32_t kHeaderSize = 0x40;
constexpr uint32_t kFillerLength = 35;

uint32_t Align( uint32_t value, uint32_t alignment ) {
    return ( value + alignment - 1 ) / alignment * alignment;
}

void AppendU16( std::vector<uint8_t> &out, uint16_t value ) {
    out.push_back( static_cast<uint8_t>( value & 0xff ) );
    out.push_back( static_cast<uint8_t>( value >> 8 ) );
}

void AppendU32( std::vector<uint8_t> &out, uint32_t value ) {
    for ( uint32_t i = 0; i < 4; i++ ) {
        out.push_back( static_cast<uint8_t>( ( value >> ( i * 8 ) ) & 0xff ) );
    }
}

void AppendI32( std::vector<uint8_t> &out, int32_t value ) {
    AppendU32( out, static_cast<uint32_t>( value ) );
}

void AppendU64( std::vector<uint8_t> &out, uint64_t value ) {
    for ( uint32_t i = 0; i < 8; i++ ) {
        out.push_back( static_cast<uint8_t>( ( value >> ( i * 8 ) ) & 0xff ) );
    }
}

void AppendSectionPrefix( std::vector<uint8_t> &out, uint32_t contentSize, uint32_t crc ) {
    AppendU32( out, contentSize );
    AppendU32( out, crc );
}

void PadTo( std::vector<uint8_t> &out, uint32_t target ) {
    while ( out.size() < target ) {
        out.push_back( 0 );
    }
}

// The gap between two sections carries the "STNICC2000 RULEZPADDING "
// pattern (restarting at each gap, truncated) - the retail files fill EVERY
// section gap with it, not just the one after the header.
void PadToWithFiller( std::vector<uint8_t> &out, uint32_t gapStart, uint32_t target ) {
    while ( out.size() < target ) {
        const size_t index = out.size() - gapStart;
        out.push_back( static_cast<uint8_t>( kFiller[ index % kFillerLength ] ) );
    }
}

// The reader's DecodeBankPath (bank.cpp): the internal path layout
// ("dir\.ext\file") to the regular path ("dir\file.ext"). The directory
// order of a source bank comes out of Bank::parseDirectory in the INTERNAL
// form, so the writer decodes it first (build_bank.py's decode_bank_path).
std::string DecodeBankPath( const std::string &encoded ) {
    std::vector<std::string> components;
    size_t start = 0;
    while ( start <= encoded.size() ) {
        size_t end = encoded.find( '\\', start );
        if ( end == std::string::npos ) {
            end = encoded.size();
        }
        components.push_back( encoded.substr( start, end - start ) );
        start = end + 1;
    }
    std::string result;
    std::string pendingExtension;
    for ( const std::string &component : components ) {
        if ( !component.empty() && component[ 0 ] == '.' ) {
            pendingExtension = component;
            continue;
        }
        if ( !result.empty() ) {
            result += '\\';
        }
        result += component;
    }
    result += pendingExtension;
    return result;
}

// A directory tree node: the writer side of Bank::parseDirectory.
struct DirectoryNode {
    std::string Name;
    bool bLeaf = false;
    std::vector<DirectoryNode> Children; // directories only; files are leaves

    DirectoryNode *Child( const std::string &name ) {
        for ( DirectoryNode &node : Children ) {
            if ( !node.bLeaf && node.Name == name ) {
                return &node;
            }
        }
        Children.push_back( DirectoryNode() );
        Children.back().Name = name;
        return &Children.back();
    }
};

// Decoded bank path `dir\name.ext` -> the tree components
// (`dir`, `.ext`, `name`): a component starting with '.' is the extension of
// the file that follows (the reader's DecodeBankPath in reverse).
std::vector<std::string> DirectoryComponents( const std::string &path ) {
    std::vector<std::string> parts;
    size_t start = 0;
    while ( start <= path.size() ) {
        size_t end = path.find( '\\', start );
        if ( end == std::string::npos ) {
            end = path.size();
        }
        parts.push_back( path.substr( start, end - start ) );
        start = end + 1;
    }
    const std::string &filename = parts.back();
    size_t dot = filename.rfind( '.' );
    std::string name = dot != std::string::npos ? filename.substr( 0, dot ) : filename;
    std::string ext = dot != std::string::npos ? filename.substr( dot ) : "";
    parts.pop_back();
    if ( !ext.empty() ) {
        parts.push_back( ext );
    }
    parts.push_back( name );
    return parts;
}

void WriteNode( std::vector<uint8_t> &out, const DirectoryNode &node ) {
    const std::string &name = node.Name;
    if ( !node.bLeaf && !node.Children.empty() ) {
        const size_t length = name.size();
        if ( length < 0x80 ) {
            out.push_back( static_cast<uint8_t>( -static_cast<int32_t>( length ) & 0xff ) );
        } else {
            out.push_back( 0x80 );
            out.push_back( static_cast<uint8_t>( length - 0x80 ) );
        }
        // 7-bit varint child count.
        uint32_t count = static_cast<uint32_t>( node.Children.size() );
        while ( true ) {
            const uint8_t byte = count & 0x7f;
            count >>= 7;
            if ( count != 0 ) {
                out.push_back( byte | 0x80 );
            } else {
                out.push_back( byte );
                break;
            }
        }
        out.insert( out.end(), name.begin(), name.end() );
        for ( const DirectoryNode &child : node.Children ) {
            WriteNode( out, child );
        }
    } else {
        const size_t length = name.size();
        if ( length < 0x7f ) {
            out.push_back( static_cast<uint8_t>( length ) );
        } else {
            out.push_back( 0x7f );
            out.push_back( static_cast<uint8_t>( length - 0x7f ) );
        }
        out.insert( out.end(), name.begin(), name.end() );
    }
}

// The decoded paths in tree (leaf) order.
void TreeLeafOrder( const DirectoryNode &node, const std::string &prefix,
                    std::vector<std::string> &out ) {
    for ( const DirectoryNode &child : node.Children ) {
        if ( !child.Children.empty() ) {
            TreeLeafOrder( child, prefix + child.Name + "\\", out );
        } else {
            out.push_back( prefix + child.Name );
        }
    }
}

// A captured node -> the writer's node shape (same fields; the recursive
// copy keeps the byte order, duplicate names included).
DirectoryNode ConvertCapturedNode( const BnkDirectoryNode &from ) {
    DirectoryNode to;
    to.Name = from.Name;
    to.bLeaf = from.bLeaf;
    for ( const BnkDirectoryNode &child : from.Children ) {
        to.Children.push_back( ConvertCapturedNode( child ) );
    }
    return to;
}

// The raw tree capture (CaptureBankDirectoryTree's walk): one node per
// length byte, children appended in byte order.
const uint8_t *CaptureNode( const uint8_t *p, const uint8_t *pEnd, const std::string &prefix,
                            BnkDirectoryNode &node, std::vector<std::string> &outLeaves ) {
    if ( p >= pEnd ) {
        return nullptr;
    }
    const int8_t length = static_cast<int8_t>( *p++ );
    if ( length < 0 ) {
        int32_t nameLength = -static_cast<int32_t>( length );
        if ( length == -0x80 ) {
            if ( p >= pEnd ) {
                return nullptr;
            }
            nameLength = *p++ + 0x80;
        }
        // The child count comes BEFORE the name (bank.cpp's law; the writer
        // emits the same order).
        uint32_t numChildren = 0;
        uint32_t shift = 0;
        while ( true ) {
            if ( p >= pEnd ) {
                return nullptr;
            }
            const int8_t byte = static_cast<int8_t>( *p++ );
            numChildren |= ( static_cast<uint32_t>( byte ) & 0x7f ) << shift;
            shift += 7;
            if ( byte >= 0 ) {
                break;
            }
        }
        if ( p + nameLength > pEnd ) {
            return nullptr;
        }
        node.Name.assign( reinterpret_cast<const char *>( p ), nameLength );
        p += nameLength;
        const std::string childPrefix = prefix + node.Name + "\\";
        for ( uint32_t i = 0; i < numChildren; i++ ) {
            BnkDirectoryNode child;
            p = CaptureNode( p, pEnd, childPrefix, child, outLeaves );
            if ( p == nullptr ) {
                return nullptr;
            }
            node.Children.push_back( std::move( child ) );
        }
        return p;
    }
    int32_t nameLength = length;
    if ( length == 0x7f ) {
        if ( p >= pEnd ) {
            return nullptr;
        }
        nameLength = *p++ + 0x7f;
    }
    if ( p + nameLength > pEnd ) {
        return nullptr;
    }
    node.Name.assign( reinterpret_cast<const char *>( p ), nameLength );
    node.bLeaf = true;
    outLeaves.push_back( prefix + node.Name );
    return p + nameLength;
}
} // namespace

uint32_t BnkCrc32( const uint8_t *pData, size_t size ) {
    // The standard (zlib polynomial 0xEDB88320) - the engine never verifies
    // bank CRCs; this is only the writer's own consistency value.
    static uint32_t table[ 256 ];
    static bool bTableReady = false;
    if ( !bTableReady ) {
        for ( uint32_t i = 0; i < 256; i++ ) {
            uint32_t c = i;
            for ( int32_t k = 0; k < 8; k++ ) {
                c = ( c & 1 ) != 0 ? 0xEDB88320u ^ ( c >> 1 ) : c >> 1;
            }
            table[ i ] = c;
        }
        bTableReady = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for ( size_t i = 0; i < size; i++ ) {
        crc = table[ ( crc ^ pData[ i ] ) & 0xff ] ^ ( crc >> 8 );
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> BuildBankDirectory( const BnkDirectoryNode &capturedRoot ) {
    std::vector<uint8_t> out;
    for ( const BnkDirectoryNode &child : capturedRoot.Children ) {
        WriteNode( out, ConvertCapturedNode( child ) );
    }
    out.push_back( 0 );
    return out;
}

bool CaptureBankDirectoryTree( const uint8_t *pDirectory, const uint8_t *pDirectoryEnd,
                               BnkDirectoryNode &outRoot, std::vector<std::string> &outLeaves ) {
    outRoot = BnkDirectoryNode();
    outLeaves.clear();
    if ( pDirectory == nullptr || pDirectoryEnd == nullptr || pDirectory >= pDirectoryEnd ) {
        return false;
    }
    const uint8_t *p = pDirectory;
    while ( p < pDirectoryEnd && *p != 0 ) {
        BnkDirectoryNode child;
        p = CaptureNode( p, pDirectoryEnd, "", child, outLeaves );
        if ( p == nullptr ) {
            return false;
        }
        outRoot.Children.push_back( std::move( child ) );
    }
    return true;
}

std::vector<uint8_t> BuildBankDirectory( const std::vector<std::string> &paths ) {
    DirectoryNode root;
    root.Name.clear();
    for ( const std::string &path : paths ) {
        DirectoryNode *node = &root;
        for ( const std::string &component : DirectoryComponents( path ) ) {
            node = node->Child( component );
        }
    }
    std::vector<uint8_t> out;
    for ( const DirectoryNode &child : root.Children ) {
        WriteNode( out, child );
    }
    out.push_back( 0 ); // the reader stops at the 0 byte
    return out;
}

std::vector<uint8_t> BuildBank( const std::vector<BnkWriteEntry> &entries,
                                const BnkWriteOptions &options ) {
    const uint32_t numEntries = static_cast<uint32_t>( entries.size() );

    // The tree order: the option's order (a source bank's parseDirectory
    // order - the INTERNAL encoded form, decoded here), or the entries' own
    // (already the decoded form the entries carry).
    std::vector<std::string> order;
    order.reserve( options.DirectoryOrder.size() );
    for ( const std::string &path : options.DirectoryOrder ) {
        order.push_back( DecodeBankPath( path ) );
    }
    if ( order.empty() ) {
        order.reserve( entries.size() );
        for ( const BnkWriteEntry &entry : entries ) {
            order.push_back( entry.Path );
        }
    }

    const std::vector<uint8_t> directory = options.bHasDirectoryTree
                                               ? BuildBankDirectory( options.DirectoryTree )
                                               : BuildBankDirectory( order );
    const uint32_t sectionAlignment = options.SectionAlignment;
    const uint32_t dataAlignment = options.DataAlignment;

    // Layout: header (8 + 0x40) + the alignment filler to the table, then the
    // four section-aligned sections, then the 16-aligned payloads. The
    // retail files: 32-aligned banks fill [0x48, 0x60) with the
    // "STNICC2000 RULEZPADDING " string, 4-aligned banks start the table at
    // 0x48 (an empty filler) - one law, tableOffset = Align(0x48, sa).
    uint32_t offset = Align( 0x48, sectionAlignment );
    const uint32_t tableOffset = offset;
    offset = Align( offset + 8 + ( numEntries + 1 ) * 0x14, sectionAlignment );
    const uint32_t typesOffset = offset;
    offset = Align( offset + 8 + numEntries * 4, sectionAlignment );
    const uint32_t directoryOffset = offset;
    offset = Align( offset + 8 + static_cast<uint32_t>( directory.size() ), sectionAlignment );
    const uint32_t indexesSize = numEntries < 0x100 ? numEntries
                                                    : ( numEntries < 0x10000 ? numEntries * 2 : numEntries * 4 );
    const uint32_t indexesOffset = offset;
    // The data start aligns from the INDEXES' end: the extra inter-section
    // table (a rebuild's verbatim copy) lives inside the alignment gap, so
    // it does not shift the payloads.
    offset = Align( offset + 8 + indexesSize, dataAlignment );
    const uint32_t dataOffset = offset;

    // Payloads, then the table records (the last record is the data section).
    uint32_t runningOffset = dataOffset;
    struct TableRecord {
        uint32_t Offset, Size, Crc, Alignment;
        int32_t Date;
    };
    std::vector<TableRecord> records;
    for ( size_t i = 0; i < entries.size(); i++ ) {
        const BnkWriteEntry &entry = entries[ i ];
        const uint32_t crc = entry.bCopyCrc ? entry.Crc : BnkCrc32( entry.Data.data(), entry.Data.size() );
        records.push_back( { runningOffset, static_cast<uint32_t>( entry.Data.size() ), crc, dataAlignment,
                             entry.Date != 0 ? entry.Date : options.CreationDate } );
        // The last payload is NOT padded (the retail sentinel sits right at
        // its last byte; the 2048 file padding follows) - the assembly loop
        // below must stay in step.
        const uint32_t next = runningOffset + static_cast<uint32_t>( entry.Data.size() );
        runningOffset = i + 1 < entries.size() ? Align( next, dataAlignment ) : next;
    }
    records.push_back( { runningOffset, 0, 0, dataAlignment, 0 } ); // the data section itself
    const uint32_t endOfData = runningOffset;
    // The file size pads to the (retail) 2048-byte multiple after the
    // resident region AND the stream tail.
    const uint32_t residentEnd = endOfData + static_cast<uint32_t>( options.StreamTail.size() );
    const uint32_t fileSize = Max( Align( residentEnd, options.FileAlignment ), residentEnd );

    // Directory order -> table order (the reader's resolveIndex).
    std::vector<uint8_t> indexBytes;
    indexBytes.reserve( indexesSize );
    for ( const std::string &path : order ) {
        int32_t tableIndex = -1;
        for ( uint32_t i = 0; i < entries.size(); i++ ) {
            if ( entries[ i ].Path == path ) {
                tableIndex = static_cast<int32_t>( i );
                break;
            }
        }
        if ( tableIndex < 0 ) {
            tableIndex = 0; // a directory order not matching the entries: keep the layout valid
        }
        if ( numEntries < 0x100 ) {
            indexBytes.push_back( static_cast<uint8_t>( tableIndex ) );
        } else if ( numEntries < 0x10000 ) {
            AppendU16( indexBytes, static_cast<uint16_t>( tableIndex ) );
        } else {
            AppendU32( indexBytes, static_cast<uint32_t>( tableIndex ) );
        }
    }

    // BankHeader (offsets relative to the struct at file 0x08).
    std::vector<uint8_t> header;
    header.reserve( 0x40 );
    AppendU32( header, kMagicBank );       // 0x00 KNAB
    AppendU32( header, 0 );                // 0x04 flags
    AppendU32( header, 0 );                // 0x08 version
    AppendU16( header, static_cast<uint16_t>( options.BankType ) ); // 0x0C type
    AppendU16( header, 0 );                // 0x0E type unknown
    AppendU32( header, fileSize );         // 0x10 size
    AppendU32( header, options.SizeWithoutPadding >= 0 ? static_cast<uint32_t>( options.SizeWithoutPadding )
                                                       : endOfData ); // 0x14 size without padding
    AppendU32( header, sectionAlignment ); // 0x18
    AppendU32( header, dataAlignment );    // 0x1C
    AppendU32( header, numEntries );       // 0x20
    AppendU32( header, static_cast<uint32_t>( options.CreationDate ) ); // 0x24
    AppendU32( header, tableOffset );      // 0x28
    AppendU32( header, typesOffset );      // 0x2C
    AppendU32( header, directoryOffset );  // 0x30
    AppendU32( header, indexesOffset );    // 0x34
    AppendU32( header, 0 );                // 0x38 reversed indexes: unused
    AppendU32( header, dataOffset );       // 0x3C
    header.resize( 0x40 );

    // The CRC of the header needs the finished header bytes; the section
    // offsets are in, so compute here.
    const uint32_t headerCrc = options.bCopySectionCrcs ? options.HeaderCrc : BnkCrc32( header );

    std::vector<uint8_t> table;
    for ( const TableRecord &record : records ) {
        AppendI32( table, static_cast<int32_t>( record.Offset ) );
        AppendI32( table, static_cast<int32_t>( record.Size ) );
        AppendI32( table, record.Date );
        AppendU32( table, record.Crc );
        AppendI32( table, static_cast<int32_t>( record.Alignment ) );
    }
    const uint32_t tableCrc = options.bCopySectionCrcs ? options.TableCrc : BnkCrc32( table );

    std::vector<uint8_t> types;
    for ( const BnkWriteEntry &entry : entries ) {
        AppendU16( types, static_cast<uint16_t>( entry.SubType ) );
        AppendU16( types, static_cast<uint16_t>( entry.Group ) );
    }
    const uint32_t typesCrc = options.bCopySectionCrcs ? options.TypesCrc : BnkCrc32( types );
    const uint32_t directoryCrc = options.bCopySectionCrcs ? options.DirectoryCrc : BnkCrc32( directory );
    const uint32_t indexesCrc = options.bCopySectionCrcs ? options.IndexesCrc : BnkCrc32( indexBytes );

    std::vector<uint8_t> out;
    out.reserve( fileSize );
    AppendU32( out, kHeaderSize );
    AppendU32( out, headerCrc );
    out.insert( out.end(), header.begin(), header.end() );
    // The alignment filler 0x48 -> tableOffset: the retail filler string
    // (truncated to the gap; a 4-aligned bank has no gap at all).
    PadToWithFiller( out, 0x48, tableOffset );
    AppendSectionPrefix( out, static_cast<uint32_t>( table.size() ), tableCrc );
    out.insert( out.end(), table.begin(), table.end() );
    PadToWithFiller( out, tableOffset + 8 + static_cast<uint32_t>( table.size() ), typesOffset );
    AppendSectionPrefix( out, static_cast<uint32_t>( types.size() ), typesCrc );
    out.insert( out.end(), types.begin(), types.end() );
    PadToWithFiller( out, typesOffset + 8 + static_cast<uint32_t>( types.size() ), directoryOffset );
    AppendSectionPrefix( out, static_cast<uint32_t>( directory.size() ), directoryCrc );
    out.insert( out.end(), directory.begin(), directory.end() );
    PadToWithFiller( out, directoryOffset + 8 + static_cast<uint32_t>( directory.size() ), indexesOffset );
    AppendSectionPrefix( out, static_cast<uint32_t>( indexBytes.size() ), indexesCrc );
    out.insert( out.end(), indexBytes.begin(), indexBytes.end() );
    // The extra inter-section table (the streamed banks' per-entry records):
    // it sits right after the indexes content, then the filler to the data.
    out.insert( out.end(), options.ExtraSection.begin(), options.ExtraSection.end() );
    PadToWithFiller( out, indexesOffset + 8 + static_cast<uint32_t>( indexBytes.size() ) +
                               static_cast<uint32_t>( options.ExtraSection.size() ),
                     dataOffset );
    for ( size_t i = 0; i < entries.size(); i++ ) {
        const BnkWriteEntry &entry = entries[ i ];
        out.insert( out.end(), entry.Data.begin(), entry.Data.end() );
        // The table's offsets are data-aligned: the gap to the next payload
        // carries the filler pattern (the retail files pad EVERY gap with
        // it) - except after the LAST payload: the retail resident region
        // ends right at its last byte (the 2048 file padding / stream tail
        // follows).
        if ( i + 1 < entries.size() ) {
            const uint32_t gapStart = records[ i ].Offset + static_cast<uint32_t>( entry.Data.size() );
            PadToWithFiller( out, gapStart, records[ i + 1 ].Offset );
        }
    }
    // The stream tail (the retail banks' streamed texture data after the
    // resident region), then the file-alignment padding.
    out.insert( out.end(), options.StreamTail.begin(), options.StreamTail.end() );
    PadTo( out, fileSize );
    return out;
}
