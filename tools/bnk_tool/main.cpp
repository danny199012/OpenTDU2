// otdu2_bnk_tool: lists / extracts / decrypts / packs TDU2 resources.
//   otdu2_bnk_tool list <file.bnk>
//   otdu2_bnk_tool extract <file.bnk> <output folder>
//   otdu2_bnk_tool pack <input folder> <output.bnk>     (the bank writer, M5)
//   otdu2_bnk_tool roundtrip <file.bnk>                 (byte-compare rebuild)
//   otdu2_bnk_tool decrypt <file.cpr> <output file>
//   otdu2_bnk_tool xmb <file.xmb|file.cpr|file.sob>     (dumps the schema)
#include "shared.h"

#include "core/crypto/cpr.h"
#include "core/xmb.h"
#include "core/xmb_write.h"
#include "filesystem/bank.h"
#include "filesystem/bank_write.h"
#include "filesystem/gs_file.h"
#include "render/render_file_write.h"
#include "system/cmd_line.h"
#include "world/pmi_write.h"

#include <algorithm>
#include <fstream>

static int ListBank( const char *pPath ) {
    Bank bank;
    if ( !bank.load( pPath ) ) {
        return 1;
    }
    for ( const BankEntry &entry : bank.getEntries() ) {
        printf( "%4d  group=%2d sub=%2d  %8u  %s\n", entry.Index, entry.Group,
                entry.SubType, entry.Size, entry.Path.c_str() );
    }
    return 0;
}

static int ExtractBank( const char *pPath, const char *pOutput ) {
    Bank bank;
    if ( !bank.load( pPath ) ) {
        return 1;
    }
    std::filesystem::create_directories( pOutput );
    for ( const BankEntry &entry : bank.getEntries() ) {
        std::filesystem::path outPath = std::filesystem::path( pOutput ) / entry.Filename;
        std::ofstream stream( outPath, std::ios::binary );
        stream.write( reinterpret_cast<const char *>( entry.pData ), entry.Size );
        printf( "%s\n", outPath.string().c_str() );
    }
    return 0;
}

static int Decrypt( const char *pPath, const char *pOutput ) {
    std::vector<uint8_t> buffer;
    if ( !gpFile->loadFile( pPath, buffer ) ) {
        return 1;
    }
    std::vector<uint8_t> decrypted;
    if ( !DecryptCPR( buffer.data(), buffer.size(), decrypted ) ) {
        return 1;
    }
    std::ofstream stream( pOutput, std::ios::binary );
    stream.write( reinterpret_cast<const char *>( decrypted.data() ), decrypted.size() );
    return 0;
}

static void DumpXmbValue( const XmbDocument &document, uint32_t typeIndex, uint32_t offset, int depth,
                          const char *pName ) {
    const XmbType &type = document.getType( typeIndex );
    std::string indent( depth * 2, ' ' );
    if ( depth > 24 ) {
        return;
    }
    if ( type.bComposite ) {
        printf( "%s%s: %s {\n", indent.c_str(), pName, type.Name.c_str() );
        for ( const XmbType::Field &field : type.Fields ) {
            if ( field.bArray ) {
                const uint32_t count = document.readU32( offset + field.Offset );
                const uint32_t arrayOffset = document.readU32( offset + field.Offset + 4 );
                const uint32_t elementSize = document.getType( field.TypeIndex ).Size;
                printf( "%s  %s[%u]\n", indent.c_str(), field.Name.c_str(), count );
                // 4096 elements unless -all (the localization tables hold 6,600+ lines).
                static const uint32_t kMaxElements = CmdLine::HasArg( "-all" ) ? 0xffffffffu : 4096u;
                for ( uint32_t i = 0; i < count && i < kMaxElements; i++ ) {
                    std::string name = field.Name + "[" + std::to_string( i ) + "]";
                    DumpXmbValue( document, field.TypeIndex, arrayOffset + i * elementSize, depth + 2, name.c_str() );
                }
            } else {
                DumpXmbValue( document, field.TypeIndex, offset + field.Offset, depth + 1, field.Name.c_str() );
            }
        }
        printf( "%s}\n", indent.c_str() );
        return;
    }
    XmbValue value( &document, typeIndex, offset );
    switch ( type.Primitive ) {
    case eXmbPrimitive::XP_String:
        printf( "%s%s = \"%s\"\n", indent.c_str(), pName, value.asString().c_str() );
        break;
    case eXmbPrimitive::XP_Float:
    case eXmbPrimitive::XP_Double:
        printf( "%s%s = %g\n", indent.c_str(), pName, value.asDouble() );
        break;
    case eXmbPrimitive::XP_UInt64:
        printf( "%s%s = 0x%016llx\n", indent.c_str(), pName, (unsigned long long)value.asUInt64() );
        break;
    case eXmbPrimitive::XP_Virtual: {
        const uint32_t typeWord = document.readU32( offset );
        const uint32_t target = document.readU32( offset + 4 );
        const uint32_t targetType = typeWord / 2;
        if ( targetType < document.getTypes().size() && target != 0 ) {
            DumpXmbValue( document, targetType, target, depth, pName );
        } else {
            printf( "%s%s = virtual(%u, %u)\n", indent.c_str(), pName, typeWord, target );
        }
        break;
    }
    default:
        printf( "%s%s = %lld\n", indent.c_str(), pName, (long long)value.asInt() );
        break;
    }
}

static int DumpXmbValues( const char *pPath ) {
    XmbDocument document;
    if ( !LoadXmbFromFile( pPath, document ) ) {
        return 1;
    }
    XmbValue root = document.getRoot();
    DumpXmbValue( document, root.getTypeIndex(), root.getOffset(), 0, "root" );
    return 0;
}

static int DumpXmb( const char *pPath ) {
    XmbDocument document;
    if ( !LoadXmbFromFile( pPath, document ) ) {
        return 1;
    }
    const auto &types = document.getTypes();
    for ( size_t i = 0; i < types.size(); i++ ) {
        const XmbType &type = types[i];
        if ( !type.bComposite ) {
            continue;
        }
        printf( "%s (size %u)\n", type.Name.c_str(), type.Size );
        for ( const XmbType::Field &field : type.Fields ) {
            printf( "    +%-4u %s%s %s\n", field.Offset,
                    types[field.TypeIndex].Name.c_str(), field.bArray ? "[]" : "",
                    field.Name.c_str() );
        }
    }
    printf( "root: %s\n", document.getRoot().getType()->Name.c_str() );
    return 0;
}

// --- pack + roundtrip (M5: the bank writer, the port of build_bank.py) -------

static bool ReadWholeFile( const char *pPath, std::vector<uint8_t> &out ) {
    std::ifstream stream( pPath, std::ios::binary );
    if ( !stream ) {
        return false;
    }
    stream.seekg( 0, std::ios::end );
    const size_t size = static_cast<size_t>( stream.tellg() );
    stream.seekg( 0, std::ios::beg );
    out.resize( size );
    stream.read( reinterpret_cast<char *>( out.data() ), static_cast<std::streamsize>( size ) );
    return static_cast<size_t>( stream.gcount() ) == size;
}

static int PackBank( const char *pFolder, const char *pOutput ) {
    // Every file of the folder tree becomes one entry: the bank path is the
    // relative path with backslashes (the decoded form the writer re-encodes
    // into the tree). Deterministic order: paths sorted.
    namespace fs = std::filesystem;
    const fs::path root( pFolder );
    if ( !fs::is_directory( root ) ) {
        printf( "not a folder: %s\n", pFolder );
        return 1;
    }
    std::vector<fs::path> files;
    for ( const fs::directory_entry &entry : fs::recursive_directory_iterator( root ) ) {
        if ( entry.is_regular_file() ) {
            files.push_back( entry.path() );
        }
    }
    std::sort( files.begin(), files.end() );
    std::vector<BnkWriteEntry> entries;
    for ( const fs::path &file : files ) {
        std::vector<uint8_t> data;
        if ( !ReadWholeFile( file.string().c_str(), data ) ) {
            printf( "read failed: %s\n", file.string().c_str() );
            return 1;
        }
        BnkWriteEntry entry;
        std::error_code ec;
        std::string relative = fs::relative( file, root, ec ).string();
        std::replace( relative.begin(), relative.end(), '/', '\\' );
        entry.Path = relative;
        entry.Data = std::move( data );
        if ( const char *pDate = CmdLine::GetArgValue( "-date" ) ) {
            entry.Date = static_cast<int32_t>( atoi( pDate ) );
        }
        entries.push_back( std::move( entry ) );
    }
    BnkWriteOptions options;
    if ( const char *pDate = CmdLine::GetArgValue( "-date" ) ) {
        options.CreationDate = static_cast<int32_t>( atoi( pDate ) );
    }
    if ( const char *pType = CmdLine::GetArgValue( "-type" ) ) {
        options.BankType = static_cast<int16_t>( atoi( pType ) );
    }
    const std::vector<uint8_t> bank = BuildBank( entries, options );
    std::ofstream stream( pOutput, std::ios::binary );
    stream.write( reinterpret_cast<const char *>( bank.data() ), static_cast<std::streamsize>( bank.size() ) );
    stream.close();
    if ( !stream ) {
        printf( "write failed: %s\n", pOutput );
        return 1;
    }
    uint64_t totalBytes = 0;
    for ( const BnkWriteEntry &entry : entries ) {
        totalBytes += entry.Data.size();
    }
    printf( "%s: %zu entr(ies), %llu bytes payload -> %zu bytes bank\n", pOutput, entries.size(),
            static_cast<unsigned long long>( totalBytes ), bank.size() );
    // The proof the packer wrote what the engine reads: parse it back.
    Bank check;
    if ( !check.load( pOutput ) ) {
        printf( "FAIL: the packed bank does not parse\n" );
        return 1;
    }
    printf( "parse-back OK: %u entr(ies) ('%s')\n", check.getNumEntries(),
            check.getNumEntries() > 0 ? check.getEntry( 0 ).Path.c_str() : "" );
    return 0;
}

static int RoundtripBank( const char *pPath ) {
    // Rebuild an existing PC bank and compare. The CRCs are copied from the
    // source (the engine never verifies them; Eden's own algorithm is not
    // reproduced), so a faithful port rebuilds the file BYTE-IDENTICAL - the
    // strongest layout test there is. Anything else is reported precisely.
    std::vector<uint8_t> original;
    if ( !ReadWholeFile( pPath, original ) || original.size() < 0x48 ) {
        printf( "read failed or not a bank: %s\n", pPath );
        return 1;
    }
    auto u32 = [ & ]( uint32_t offset ) -> uint32_t {
        uint32_t value;
        memcpy( &value, original.data() + offset, 4 );
        return value;
    };
    auto i32 = [ & ]( uint32_t offset ) -> int32_t {
        return static_cast<int32_t>( u32( offset ) );
    };
    const uint32_t headerAt = 8;
    const int32_t numEntries = i32( headerAt + 0x20 );
    if ( memcmp( original.data() + headerAt, "KNAB", 4 ) != 0 || numEntries < 0 ||
         numEntries > 10 * 1000 * 1000 ) {
        printf( "not a PC bank: %s\n", pPath );
        return 1;
    }

    // The source table: per-entry offset / size / date / crc (+ the data
    // section's record at the end).
    const uint32_t tableOffset = static_cast<uint32_t>( i32( headerAt + 0x28 ) );
    const uint32_t tableContent = tableOffset + 8;
    struct SourceRecord {
        int32_t Offset, Size, Date, Crc, Alignment;
    };
    std::vector<SourceRecord> records( static_cast<size_t>( numEntries ) + 1 );
    for ( int32_t i = 0; i <= numEntries; i++ ) {
        const uint32_t at = tableContent + static_cast<uint32_t>( i ) * 0x14;
        records[ i ].Offset = i32( at );
        records[ i ].Size = i32( at + 4 );
        records[ i ].Date = i32( at + 8 );
        records[ i ].Crc = i32( at + 12 );
        records[ i ].Alignment = i32( at + 16 );
    }

    // The entries in table order (paths resolved through the reader).
    Bank bank;
    std::vector<uint8_t> bankBuffer( original.begin(), original.end() );
    if ( !bank.loadFromMemory( std::move( bankBuffer ), pPath ) ) {
        printf( "the bank does not parse: %s\n", pPath );
        return 1;
    }
    std::vector<BnkWriteEntry> entries;
    entries.reserve( static_cast<size_t>( numEntries ) );
    for ( int32_t i = 0; i < numEntries; i++ ) {
        const BankEntry &entry = bank.getEntry( static_cast<uint32_t>( i ) );
        BnkWriteEntry out;
        out.Path = entry.Path;
        out.Data.assign( original.data() + records[ i ].Offset,
                         original.data() + records[ i ].Offset + records[ i ].Size );
        out.SubType = entry.SubType;
        out.Group = entry.Group;
        out.Date = records[ i ].Date;
        out.bCopyCrc = true;
        out.Crc = static_cast<uint32_t>( records[ i ].Crc );
        entries.push_back( std::move( out ) );
    }

    // The directory: the raw tree captured (retail trees may list a
    // directory name twice - commonworld's `objects` holds `vegetations`
    // twice - so the rebuild encodes the captured structure verbatim) plus
    // its leaf order for the index mapping.
    const uint32_t directoryOffset = static_cast<uint32_t>( i32( headerAt + 0x30 ) );
    const uint32_t directorySize = u32( directoryOffset );
    BnkDirectoryNode directoryTree;
    std::vector<std::string> directoryOrder;
    if ( !CaptureBankDirectoryTree(
             original.data() + directoryOffset + 8,
             original.data() + directoryOffset + 8 +
                 Min<uint32_t>( directorySize, static_cast<uint32_t>( original.size() ) - directoryOffset - 8 ),
             directoryTree, directoryOrder ) ) {
        printf( "FAIL: the directory tree does not parse\n" );
        return 1;
    }

    BnkWriteOptions options;
    options.CreationDate = i32( headerAt + 0x24 );
    options.BankType = static_cast<int16_t>( i32( headerAt + 0x0C ) );
    // The retail layout is per file: the alignments come out of the header
    // (icd_ferr packs sections 4-aligned with no filler, the frontend banks
    // 32-align with the filler string), and the file pads to a 2048 multiple
    // AFTER the resident region - the tail is the bank's streamed data
    // (the table never references it), copied verbatim.
    options.SectionAlignment = u32( headerAt + 0x18 );
    options.DataAlignment = u32( headerAt + 0x1C );
    options.DirectoryOrder = directoryOrder;
    options.DirectoryTree = std::move( directoryTree );
    options.bHasDirectoryTree = true;
    options.bCopySectionCrcs = true;
    options.HeaderCrc = static_cast<uint32_t>( i32( 4 ) ); // the header section's own CRC (file 0x04)
    options.TableCrc = static_cast<uint32_t>( i32( tableOffset + 4 ) );
    options.TypesCrc = static_cast<uint32_t>( i32( static_cast<uint32_t>( i32( headerAt + 0x2C ) ) + 4 ) );
    options.DirectoryCrc = static_cast<uint32_t>( i32( directoryOffset + 4 ) );
    options.IndexesCrc = static_cast<uint32_t>( i32( static_cast<uint32_t>( i32( headerAt + 0x34 ) ) + 4 ) );
    {
        // The stream region: everything after the LAST entry's payload (the
        // retail SizeWithoutPadding points INTO it - the streamed mips of
        // the last big texture - so the field is copied, not recomputed).
        const uint32_t sourceSize = static_cast<uint32_t>( i32( headerAt + 0x10 ) );
        options.SizeWithoutPadding = i32( headerAt + 0x14 );
        if ( numEntries > 0 && sourceSize <= original.size() ) {
            const uint32_t lastEnd = static_cast<uint32_t>( records[ numEntries - 1 ].Offset + records[ numEntries - 1 ].Size );
            if ( lastEnd < sourceSize ) {
                options.StreamTail.assign( original.begin() + lastEnd, original.begin() + sourceSize );
            }
        }
        // The extra table between the indexes and the data (the streamed
        // banks' own per-entry records): captured verbatim. The data start
        // is the header's data field (0x3C; zero when the bank doesn't use
        // it - then there is nothing to capture).
        {
            const uint32_t indexesOffset = static_cast<uint32_t>( i32( headerAt + 0x34 ) );
            const uint32_t indexesSize = u32( indexesOffset );
            const uint32_t indexesEnd = indexesOffset + 8 + indexesSize;
            const uint32_t dataStart = static_cast<uint32_t>( i32( headerAt + 0x3C ) );
            if ( dataStart != 0 && indexesEnd < dataStart && dataStart <= original.size() ) {
                options.ExtraSection.assign( original.begin() + indexesEnd, original.begin() + dataStart );
            }
        }
    }

    const std::vector<uint8_t> rebuilt = BuildBank( entries, options );
    if ( const char *pOut = CmdLine::GetArgValue( "-out" ) ) {
        std::ofstream stream( pOut, std::ios::binary | std::ios::trunc );
        stream.write( reinterpret_cast<const char *>( rebuilt.data() ),
                      static_cast<std::streamsize>( rebuilt.size() ) );
        printf( "rebuilt bank written to '%s' for comparison\n", pOut );
    }

    auto rebuiltU32 = [ & ]( uint32_t offset ) -> uint32_t {
        uint32_t value;
        memcpy( &value, rebuilt.data() + offset, 4 );
        return value;
    };
    auto rebuiltI32 = [ & ]( uint32_t offset ) -> int32_t {
        return static_cast<int32_t>( rebuiltU32( offset ) );
    };

    int32_t problems = 0;
    if ( rebuilt == original ) {
        printf( "%s: %d entr(ies) rebuilt BYTE-IDENTICAL (%zu bytes)\n", pPath, numEntries, original.size() );
        return 0;
    }
    // Not byte-identical: report where, and whether the ENTRIES still
    // round-trip (the reader-level guarantee, build_bank.py's own bar).
    size_t firstDiff = 0;
    while ( firstDiff < Min( original.size(), rebuilt.size() ) && original[ firstDiff ] == rebuilt[ firstDiff ] ) {
        firstDiff++;
    }
    printf( "%s: rebuilt %zu bytes vs %zu, first byte diff at %zu (0x%zx)\n", pPath, rebuilt.size(),
            original.size(), firstDiff, firstDiff );
    // Entries + directory comparison (the reader-level round-trip).
    std::vector<uint8_t> reparsed( rebuilt.begin(), rebuilt.end() );
    Bank again;
    if ( !again.loadFromMemory( std::move( reparsed ), pPath ) ) {
        printf( "FAIL: the rebuilt bank does not parse\n" );
        return 1;
    }
    if ( again.getNumEntries() != bank.getNumEntries() ) {
        printf( "FAIL: entry count %zu != %u\n", again.getNumEntries(), bank.getNumEntries() );
        problems++;
    }
    for ( uint32_t i = 0; i < bank.getNumEntries() && i < again.getNumEntries(); i++ ) {
        const BankEntry &a = bank.getEntry( i );
        const BankEntry &b = again.getEntry( i );
        if ( a.Path != b.Path || a.Size != b.Size ) {
            printf( "FAIL: entry %u: '%s' (%u) != '%s' (%u)\n", i, a.Path.c_str(), a.Size, b.Path.c_str(), b.Size );
            problems++;
        } else if ( memcmp( a.pData, b.pData, a.Size ) != 0 ) {
            printf( "FAIL: payload bytes differ for '%s'\n", a.Path.c_str() );
            problems++;
        }
    }
    // The directory tree must round-trip byte for byte.
    const uint32_t newDirectoryOffset = static_cast<uint32_t>( rebuiltI32( 8 + 0x30 ) );
    const uint32_t newDirectorySize = rebuiltU32( newDirectoryOffset );
    std::vector<uint8_t> newDirectory(
        rebuilt.begin() + static_cast<ptrdiff_t>( newDirectoryOffset ) + 8,
        rebuilt.begin() + static_cast<ptrdiff_t>( newDirectoryOffset ) + 8 + newDirectorySize );
    std::vector<uint8_t> oldDirectory(
        original.begin() + static_cast<ptrdiff_t>( directoryOffset ) + 8,
        original.begin() + static_cast<ptrdiff_t>( directoryOffset ) + 8 + directorySize );
    if ( newDirectory != oldDirectory ) {
        printf( "NOTE: directory bytes differ (valid but not identical): %zu vs %zu bytes\n", newDirectory.size(),
                oldDirectory.size() );
    }
    printf( "%s: %d entr(ies) rebuilt, %d problem(s)%s\n", pPath, numEntries, problems,
            problems == 0 ? " (entries + directory round-trip OK; the file bytes differ)" : "" );
    return problems == 0 ? 0 : 1;
}

// --- the render-file roundtrip (the .2dm / .3dd / .3dg / .2db writer) --------

static int RoundtripRenderFile( const char *pPath ) {
    // Structured rebuild: parse the section tree (each section's own payload,
    // child sections and trailing bytes captured separately), rebuild from
    // the structure and byte-compare. Validates the container law the
    // scene-export writers build on.
    std::vector<uint8_t> original;
    if ( !ReadWholeFile( pPath, original ) ) {
        printf( "read failed: %s\n", pPath );
        return 1;
    }
    RenderSectionWrite root;
    std::string error;
    if ( !ParseRenderFileTree( original.data(), static_cast<uint32_t>( original.size() ), root, error ) ) {
        printf( "parse failed: %s (%s)\n", pPath, error.c_str() );
        return 1;
    }
    const std::vector<uint8_t> rebuilt = BuildRenderFile( root );
    if ( rebuilt == original ) {
        printf( "%s: %zu bytes rebuilt BYTE-IDENTICAL (%zu top-level sections, magic %.4s)\n", pPath,
                original.size(), root.Children.size(), reinterpret_cast<const char *>( &root.Type ) );
        return 0;
    }
    size_t firstDiff = 0;
    while ( firstDiff < Min( original.size(), rebuilt.size() ) && original[ firstDiff ] == rebuilt[ firstDiff ] ) {
        firstDiff++;
    }
    printf( "%s: rebuilt %zu bytes vs %zu, first byte diff at %zu (0x%zx)\n", pPath, rebuilt.size(),
            original.size(), firstDiff, firstDiff );
    return 1;
}

// --- the XMB roundtrip (the writer, source/core/xmb_write.cpp) --------------

// Value-level tree comparison (the reader-level bar): composites walk their
// fields, arrays their elements, scalars compare through the reader API.
static bool CompareXmbValues( const XmbDocument &a, uint32_t typeA, uint32_t offA, const XmbDocument &b,
                              uint32_t typeB, uint32_t offB, int depth, std::string &outDiff,
                              std::string &outPath ) {
    if ( depth > 24 ) {
        return true;
    }
    if ( typeA >= a.getTypes().size() || typeB >= b.getTypes().size() ) {
        outDiff = "type index out of range";
        return false;
    }
    const XmbType &ta = a.getType( typeA );
    const XmbType &tb = b.getType( typeB );
    if ( ta.Name != tb.Name ) {
        outDiff = "type name '" + ta.Name + "' vs '" + tb.Name + "'";
        return false;
    }
    if ( ta.bComposite != tb.bComposite ) {
        outDiff = "composite kind differs at '" + ta.Name + "'";
        return false;
    }
    if ( ta.bComposite ) {
        if ( ta.Fields.size() != tb.Fields.size() ) {
            outDiff = "field count differs at '" + ta.Name + "'";
            return false;
        }
        for ( size_t i = 0; i < ta.Fields.size(); i++ ) {
            const XmbType::Field &fa = ta.Fields[ i ];
            const XmbType::Field &fb = tb.Fields[ i ];
            if ( fa.Name != fb.Name || fa.bArray != fb.bArray ) {
                outDiff = "field '" + fa.Name + "' differs at '" + ta.Name + "'";
                return false;
            }
            if ( fa.bArray ) {
                const uint32_t countA = a.readU32( offA + fa.Offset );
                const uint32_t countB = b.readU32( offB + fb.Offset );
                if ( countA != countB ) {
                    outDiff = "array count " + std::to_string( countA ) + " vs " + std::to_string( countB ) +
                              " at '" + ta.Name + "." + fa.Name + "'";
                    return false;
                }
                const uint32_t arrayA = a.readU32( offA + fa.Offset + 4 );
                const uint32_t arrayB = b.readU32( offB + fb.Offset + 4 );
                const uint32_t sizeA = a.getType( fa.TypeIndex ).Size;
                const uint32_t sizeB = b.getType( fb.TypeIndex ).Size;
                for ( uint32_t e = 0; e < countA; e++ ) {
                    std::string elementPath = outPath + fa.Name + "[" + std::to_string( e ) + "].";
                    if ( !CompareXmbValues( a, fa.TypeIndex, arrayA + e * sizeA, b, fb.TypeIndex,
                                            arrayB + e * sizeB, depth + 1, outDiff, elementPath ) ) {
                        outPath = elementPath;
                        return false;
                    }
                }
            } else {
                std::string fieldPath = outPath + fa.Name + ".";
                if ( !CompareXmbValues( a, fa.TypeIndex, offA + fa.Offset, b, fb.TypeIndex, offB + fb.Offset,
                                        depth + 1, outDiff, fieldPath ) ) {
                    outPath = fieldPath;
                    return false;
                }
            }
        }
        return true;
    }
    // Scalar (both sides share the primitive law).
    if ( ta.Primitive != tb.Primitive ) {
        outDiff = "primitive differs at '" + ta.Name + "'";
        return false;
    }
    switch ( ta.Primitive ) {
    case eXmbPrimitive::XP_String:
        if ( strcmp( a.readString( offA ), b.readString( offB ) ) != 0 ) {
            outDiff = "string '" + std::string( a.readString( offA ) ) + "' vs '" + b.readString( offB ) + "'";
            return false;
        }
        return true;
    case eXmbPrimitive::XP_UInt64:
    case eXmbPrimitive::XP_Int64:
        if ( a.readU64( offA ) != b.readU64( offB ) ) {
            outDiff = "u64 differs at '" + ta.Name + "'";
            return false;
        }
        return true;
    case eXmbPrimitive::XP_Float:
    case eXmbPrimitive::XP_Double:
        if ( a.readU32( offA ) != b.readU32( offB ) && ta.Primitive == eXmbPrimitive::XP_Float ) {
            outDiff = "float differs at '" + ta.Name + "'";
            return false;
        }
        if ( ta.Primitive == eXmbPrimitive::XP_Double && a.readU64( offA ) != b.readU64( offB ) ) {
            outDiff = "double differs at '" + ta.Name + "'";
            return false;
        }
        return true;
    default:
        if ( a.readU32( offA ) != b.readU32( offB ) ) {
            outDiff = "scalar differs at '" + ta.Name + "'";
            return false;
        }
        return true;
    }
}

static int RoundtripXmb( const char *pPath, const char *pDumpPath ) {
    // Parse the file, rebuild it from the parsed tree (the type table replayed
    // in the file's own record order, the strings re-sorted, the data section
    // re-serialized with the FIFO law) and byte-compare.
    std::vector<uint8_t> original;
    if ( !ReadWholeFile( pPath, original ) ) {
        printf( "read failed: %s\n", pPath );
        return 1;
    }
    XmbDocument document;
    if ( !document.load( original.data(), original.size() ) ) {
        printf( "parse failed: %s\n", pPath );
        return 1;
    }
    std::vector<uint8_t> rebuilt;
    std::string error;
    if ( !RebuildXmb( document, original.data(), static_cast<uint32_t>( original.size() ), rebuilt, error ) ) {
        printf( "rebuild failed: %s (%s)\n", pPath, error.c_str() );
        return 1;
    }
    if ( rebuilt == original ) {
        printf( "%s: %zu bytes rebuilt BYTE-IDENTICAL (%zu type records)\n", pPath, original.size(),
                document.getTypes().size() );
        return 0;
    }
    size_t firstDiff = 0;
    while ( firstDiff < Min( original.size(), rebuilt.size() ) && original[ firstDiff ] == rebuilt[ firstDiff ] ) {
        firstDiff++;
    }
    printf( "%s: rebuilt %zu bytes vs %zu, first byte diff at %zu (0x%zx)\n", pPath, rebuilt.size(),
            original.size(), firstDiff, firstDiff );
    if ( pDumpPath != nullptr ) {
        std::ofstream stream( pDumpPath, std::ios::binary | std::ios::trunc );
        stream.write( reinterpret_cast<const char *>( rebuilt.data() ), static_cast<std::streamsize>( rebuilt.size() ) );
        printf( "rebuilt written to %s\n", pDumpPath );
    }
    // The reader-level guarantee: the rebuilt file must parse to the same
    // VALUES (the retail files carry unreferenced writer garbage - 'AAA\0'
    // placeholder blobs, a '##...KEYMAP##' trailer - the parse never sees).
    XmbDocument again;
    if ( !again.load( rebuilt.data(), rebuilt.size() ) ) {
        printf( "FAIL: the rebuilt file does not parse\n" );
        return 1;
    }
    bool bSame = again.getTypes().size() == document.getTypes().size() &&
                 again.getRoot().getTypeIndex() == document.getRoot().getTypeIndex();
    std::string valueDiff;
    if ( bSame ) {
        std::string valuePath;
        bSame = CompareXmbValues( document, document.getRoot().getTypeIndex(), 0, again,
                                  again.getRoot().getTypeIndex(), 0, 0, valueDiff, valuePath );
        valueDiff = valuePath + " " + valueDiff;
    }
    printf( "%s: rebuilt, %s%s\n", pPath, bSame ? "parses to the same VALUES (bytes differ)" : "VALUES DIFFER",
            valueDiff.empty() ? "" : ( " - " + valueDiff ).c_str() );
    return bSame ? 0 : 1;
}

// --- the .pmi roundtrip (the instance-list writer) ---------------------------

static int RoundtripPmi( const char *pPath ) {
    // Every -v/-o/-s/-a.pmi of a sector bank: parse into raw groups, rebuild
    // unchanged, byte-compare. Validates the u16[16] encode law against the
    // retail data (the height range, the record fields, the payload strides).
    Bank bank;
    if ( !bank.load( pPath ) ) {
        return 1;
    }
    std::vector<uint8_t> original;
    if ( !ReadWholeFile( pPath, original ) ) {
        printf( "read failed: %s\n", pPath );
        return 1;
    }
    int32_t lists = 0, problems = 0;
    for ( const char *pSuffix : { "-v", "-o", "-s", "-a" } ) {
        for ( const BankEntry &entry : bank.getEntries() ) {
            if ( entry.Extension != "pmi" || entry.Name.size() <= strlen( pSuffix ) ||
                 _stricmp( entry.Name.c_str() + entry.Name.size() - strlen( pSuffix ), pSuffix ) != 0 ) {
                continue;
            }
            lists++;
            std::vector<PmiGroup> groups;
            if ( !ParsePmiGroups( entry.pData, entry.Size, groups ) ) {
                printf( "FAIL: '%s' does not parse into groups\n", entry.Filename.c_str() );
                problems++;
                continue;
            }
            std::vector<uint8_t> rebuilt;
            if ( !BuildPmi( groups, entry.pData, entry.Size, rebuilt ) ) {
                printf( "FAIL: '%s' did not rebuild\n", entry.Filename.c_str() );
                problems++;
                continue;
            }
            if ( rebuilt.size() != entry.Size || memcmp( rebuilt.data(), entry.pData, entry.Size ) != 0 ) {
                size_t firstDiff = 0;
                const size_t common = Min( rebuilt.size(), static_cast<size_t>( entry.Size ) );
                while ( firstDiff < common && rebuilt[ firstDiff ] == entry.pData[ firstDiff ] ) {
                    firstDiff++;
                }
                printf( "FAIL: '%s' rebuilt %zu bytes vs %u, first diff at %zu\n", entry.Filename.c_str(),
                        rebuilt.size(), entry.Size, firstDiff );
                problems++;
            } else {
                size_t numRecords = 0;
                for ( const PmiGroup &group : groups ) {
                    numRecords += group.Records.size();
                }
                printf( "'%s': %zu group(s), %zu record(s) rebuilt BYTE-IDENTICAL\n", entry.Filename.c_str(),
                        groups.size(), numRecords );
            }
        }
    }
    printf( "%s: %d pmi list(s), %d problem(s)\n", pPath, lists, problems );
    return problems == 0 ? 0 : 1;
}

int main( int argc, const char **argv ) {
    CmdLine::Initialize( argc, argv );
    Logger::Initialize( nullptr, false, false );
    GSFile file;

    if ( argc < 3 ) {
        printf( "usage: %s list|extract|pack|roundtrip|roundtripfile|roundtripxmb|decrypt|xmb <file> [output]\n", argv[ 0 ] );
        return 1;
    }
    std::string command = argv[ 1 ];
    if ( command == "list" ) {
        return ListBank( argv[ 2 ] );
    } else if ( command == "extract" && argc > 3 ) {
        return ExtractBank( argv[ 2 ], argv[ 3 ] );
    } else if ( command == "pack" && argc > 3 ) {
        return PackBank( argv[ 2 ], argv[ 3 ] );
    } else if ( command == "roundtrip" ) {
        return RoundtripBank( argv[ 2 ] );
    } else if ( command == "roundtripfile" ) {
        return RoundtripRenderFile( argv[ 2 ] );
    } else if ( command == "roundtripxmb" ) {
        return RoundtripXmb( argv[ 2 ], argc > 3 ? argv[ 3 ] : nullptr );
    } else if ( command == "roundtrip-pmi" ) {
        return RoundtripPmi( argv[ 2 ] );
    } else if ( command == "decrypt" && argc > 3 ) {
        return Decrypt( argv[ 2 ], argv[ 3 ] );
    } else if ( command == "xmb" ) {
        return DumpXmb( argv[ 2 ] );
    } else if ( command == "xmbdump" ) {
        return DumpXmbValues( argv[ 2 ] );
    }
    return 1;
}
