#include "iso_archive.h"

#include "launcher_util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace Launcher {

namespace {

constexpr uint32_t kIsoSector = 2048;

uint16_t ReadLe16( const uint8_t *p ) {
    return static_cast<uint16_t>( p[0] | ( p[1] << 8 ) );
}
uint32_t ReadLe32( const uint8_t *p ) {
    return static_cast<uint32_t>( p[0] ) | ( static_cast<uint32_t>( p[1] ) << 8 ) |
           ( static_cast<uint32_t>( p[2] ) << 16 ) | ( static_cast<uint32_t>( p[3] ) << 24 );
}
uint64_t ReadLe64( const uint8_t *p ) {
    return static_cast<uint64_t>( ReadLe32( p ) ) |
           ( static_cast<uint64_t>( ReadLe32( p + 4 ) ) << 32 );
}

void AppendUtf8( std::string &out, uint32_t codePoint ) {
    if ( codePoint <= 0x7Fu ) {
        out += static_cast<char>( codePoint );
    } else if ( codePoint <= 0x7FFu ) {
        out += static_cast<char>( 0xC0u | ( codePoint >> 6 ) );
        out += static_cast<char>( 0x80u | ( codePoint & 0x3Fu ) );
    } else if ( codePoint <= 0xFFFFu ) {
        out += static_cast<char>( 0xE0u | ( codePoint >> 12 ) );
        out += static_cast<char>( 0x80u | ( ( codePoint >> 6 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( codePoint & 0x3Fu ) );
    } else {
        out += static_cast<char>( 0xF0u | ( codePoint >> 18 ) );
        out += static_cast<char>( 0x80u | ( ( codePoint >> 12 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( ( codePoint >> 6 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( codePoint & 0x3Fu ) );
    }
}

// ISO 9660 Joliet / UDF d-strings: UCS-2 (Joliet: big endian).
std::string DecodeUcs2Be( const uint8_t *data, size_t byteCount ) {
    std::string out;
    for ( size_t i = 0; i + 1 < byteCount; i += 2 ) {
        const uint32_t code = ( static_cast<uint32_t>( data[i] ) << 8 ) | data[i + 1];
        if ( code == 0 ) {
            break;
        }
        AppendUtf8( out, code );
    }
    return out;
}

// UDF compressed Unicode: first byte 8 (bytes follow) or 16 (UCS-2 big endian).
std::string DecodeUdfString( const uint8_t *data, size_t byteCount ) {
    if ( byteCount == 0 ) {
        return std::string();
    }
    const uint8_t compression = data[0];
    if ( compression == 16 ) {
        return DecodeUcs2Be( data + 1, byteCount - 1 );
    }
    std::string out;
    for ( size_t i = 1; i < byteCount; i++ ) {
        if ( data[i] == 0 ) {
            break;
        }
        out += static_cast<char>( data[i] );
    }
    return out;
}

std::string TrimSpaces( std::string text ) {
    while ( !text.empty() && ( text.back() == ' ' || text.back() == '\0' ) ) {
        text.pop_back();
    }
    const size_t start = text.find_first_not_of( ' ' );
    return start == std::string::npos ? std::string() : text.substr( start );
}

std::string JoinIsoPath( const std::string &parent, const std::string &name ) {
    return parent.empty() ? name : parent + "/" + name;
}

std::string StripIsoVersion( std::string name ) {
    const size_t semicolon = name.find( ';' );
    if ( semicolon != std::string::npos ) {
        name.resize( semicolon );
    }
    if ( !name.empty() && name.back() == '.' ) {
        name.pop_back();
    }
    return name;
}

} // namespace

IsoArchive::~IsoArchive() { Close(); }

bool IsoArchive::ReadAt( uint64_t offset, void *buffer, uint32_t size ) {
    if ( mFile == INVALID_HANDLE_VALUE ) {
        return false;
    }
    LARGE_INTEGER position = {};
    position.QuadPart = static_cast<LONGLONG>( offset );
    if ( !SetFilePointerEx( mFile, position, nullptr, FILE_BEGIN ) ) {
        return false;
    }
    uint8_t *out = static_cast<uint8_t *>( buffer );
    uint32_t total = 0;
    while ( total < size ) {
        DWORD read = 0;
        if ( !::ReadFile( mFile, out + total, size - total, &read, nullptr ) || read == 0 ) {
            return false;
        }
        total += read;
    }
    return true;
}

void IsoArchive::Close() {
    if ( mFile != INVALID_HANDLE_VALUE ) {
        CloseHandle( mFile );
        mFile = INVALID_HANDLE_VALUE;
    }
    mFiles.clear();
    mTotalBytes = 0;
    mVolume = IsoVolume();
    mLastError.clear();
}

bool IsoArchive::Open( const std::wstring &path, std::string &outError ) {
    Close();
    outError.clear();
    if ( !FileExists( path ) ) {
        outError = "No such file: " + WideToUtf8( path );
        return false;
    }
    mFile = CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( mFile == INVALID_HANDLE_VALUE ) {
        outError = "Cannot open the image (" + std::to_string( GetLastError() ) + "): " +
                   WideToUtf8( path );
        return false;
    }
    mVolume.ImageSize = FileSizeOf( path );
    if ( mVolume.ImageSize < 64 * 1024 ) {
        outError = "The file is too small to be an ISO image";
        Close();
        return false;
    }

    std::string isoError;
    if ( OpenIso9660( isoError ) ) {
        return true;
    }

    std::string udfError;
    if ( OpenUdf( udfError ) ) {
        return true;
    }

    outError = "Unsupported or unreadable image.";
    if ( !isoError.empty() ) {
        outError += " ISO 9660: " + isoError;
    }
    if ( !udfError.empty() ) {
        outError += " UDF: " + udfError;
    }
    Close();
    return false;
}

const IsoFile *IsoArchive::FindEntry( const std::string &path ) const {
    for ( const IsoFile &file : mFiles ) {
        if ( file.Path == path ) {
            return &file;
        }
    }
    return nullptr;
}

bool IsoArchive::FindDirectory( const std::string &path, std::string &outActualPath ) const {
    const std::string wanted = ToLowerAscii( path );
    for ( const IsoFile &file : mFiles ) {
        if ( file.bDirectory && ToLowerAscii( file.Path ) == wanted ) {
            outActualPath = file.Path;
            return true;
        }
    }
    return false;
}

bool IsoArchive::FindGameRoot( std::string &outPrefix, bool &bOutPacked ) const {
    outPrefix.clear();
    bOutPacked = false;

    // Directory paths, lowercased -> actual. The root is always a directory.
    std::map<std::string, std::string> directories;
    directories[""] = "";
    for ( const IsoFile &file : mFiles ) {
        if ( file.bDirectory ) {
            directories[ToLowerAscii( file.Path )] = file.Path;
        }
    }

    // A game root is the folder that holds Euro/Bnk (GSFile::HasBankFolder).
    for ( const auto &dir : directories ) {
        const std::string &lowerPath = dir.first;
        if ( lowerPath.empty() ) {
            continue;
        }
        const size_t slash = lowerPath.find_last_of( '/' );
        const std::string leaf = slash == std::string::npos ? lowerPath : lowerPath.substr( slash + 1 );
        if ( leaf != "euro" ) {
            continue;
        }
        if ( directories.find( lowerPath + "/bnk" ) == directories.end() ) {
            continue;
        }
        // The game root is the Euro folder's parent.
        const std::string &euroActual = dir.second;
        const size_t euroSlash = euroActual.find_last_of( '/' );
        outPrefix = euroSlash == std::string::npos ? std::string() : euroActual.substr( 0, euroSlash );
        return true;
    }

    // The retail packed install: bigfile_EU_*.map + TDU2v*.big in one folder.
    for ( const IsoFile &file : mFiles ) {
        if ( file.bDirectory ) {
            continue;
        }
        const std::string lower = ToLowerAscii( file.Path );
        const size_t slash = lower.find_last_of( '/' );
        const std::string leaf = slash == std::string::npos ? lower : lower.substr( slash + 1 );
        if ( leaf.size() > 4 && leaf.compare( leaf.size() - 4, 4, ".map" ) == 0 &&
             leaf.rfind( "bigfile_", 0 ) == 0 ) {
            bOutPacked = true;
            break;
        }
    }
    return false;
}

uint32_t IsoArchive::ReadFile( const IsoFile &file, uint64_t offset, void *buffer,
                               uint32_t size ) {
    if ( offset >= file.Size || size == 0 ) {
        return 0;
    }
    const uint64_t available = file.Size - offset;
    const uint32_t toRead = static_cast<uint32_t>( std::min<uint64_t>( available, size ) );
    uint8_t *out = static_cast<uint8_t *>( buffer );

    uint64_t logical = 0;
    uint32_t done = 0;
    for ( const IsoExtent &extent : file.Extents ) {
        if ( done >= toRead ) {
            break;
        }
        if ( offset >= logical + extent.Size ) {
            logical += extent.Size;
            continue;
        }
        const uint64_t inside = offset > logical ? offset - logical : 0;
        const uint64_t extentLeft = extent.Size - inside;
        const uint32_t chunk =
            static_cast<uint32_t>( std::min<uint64_t>( extentLeft, toRead - done ) );
        if ( !ReadAt( extent.Offset + inside, out + done, chunk ) ) {
            return done;
        }
        done += chunk;
        offset += chunk;
        logical += extent.Size;
    }
    return done;
}

bool IsoArchive::OpenIso9660( std::string &outError ) {
    uint8_t primary[kIsoSector] = {};
    uint8_t joliet[kIsoSector] = {};
    bool bHasPrimary = false;
    bool bHasJoliet = false;

    for ( uint32_t sector = 16; sector < 16 + 64; sector++ ) {
        uint8_t descriptor[kIsoSector];
        if ( !ReadAt( static_cast<uint64_t>( sector ) * kIsoSector, descriptor, kIsoSector ) ) {
            break;
        }
        if ( memcmp( descriptor + 1, "CD001", 5 ) != 0 ) {
            break;
        }
        const uint8_t type = descriptor[0];
        if ( type == 1 ) {
            memcpy( primary, descriptor, kIsoSector );
            bHasPrimary = true;
        } else if ( type == 2 && descriptor[88] == '%' && descriptor[89] == '/' &&
                    ( descriptor[90] == '@' || descriptor[90] == 'C' ||
                      descriptor[90] == 'E' ) ) {
            memcpy( joliet, descriptor, kIsoSector );
            bHasJoliet = true;
        } else if ( type == 255 ) {
            break;
        }
    }
    if ( !bHasPrimary && !bHasJoliet ) {
        outError = "no ISO 9660 volume descriptor";
        return false;
    }

    const uint8_t *volumeDescriptor = bHasJoliet ? joliet : primary;
    const bool bJolietNames = bHasJoliet;

    if ( bHasPrimary ) {
        mVolume.Label = TrimSpaces(
            std::string( reinterpret_cast<const char *>( primary + 40 ), 32 ) );
    }
    mVolume.Filesystem = bJolietNames ? "ISO 9660 + Joliet" : "ISO 9660";

    struct DirectoryJob {
        std::string Path;
        uint32_t Lba = 0;
        uint32_t Size = 0;
    };
    std::vector<DirectoryJob> queue;

    const uint8_t *rootRecord = volumeDescriptor + 156;
    queue.push_back( { std::string(), ReadLe32( rootRecord + 2 ),
                       ReadLe32( rootRecord + 10 ) } );

    // Multi-extent files: the previous record was flagged "more extents
    // follow"; the next record with the same name appends to it instead of
    // adding a second file.
    std::string pendingMultiExtent;

    for ( size_t jobIndex = 0; jobIndex < queue.size(); jobIndex++ ) {
        const DirectoryJob job = queue[jobIndex];
        if ( job.Size == 0 || job.Size > 64u * 1024u * 1024u ) {
            continue;
        }
        std::vector<uint8_t> data( job.Size );
        if ( !ReadAt( static_cast<uint64_t>( job.Lba ) * kIsoSector, data.data(),
                      job.Size ) ) {
            continue;
        }
        size_t pos = 0;
        while ( pos + 34 <= data.size() ) {
            const uint8_t recordLength = data[pos];
            if ( recordLength == 0 ) {
                // Padding at the end of a sector: jump to the next sector.
                const size_t next = ( ( pos / kIsoSector ) + 1 ) * kIsoSector;
                if ( next <= pos ) {
                    break;
                }
                pos = next;
                continue;
            }
            if ( recordLength < 34 || pos + recordLength > data.size() ) {
                break;
            }
            const uint8_t *record = data.data() + pos;
            pos += recordLength;

            const uint32_t lba = ReadLe32( record + 2 );
            const uint32_t fileSize = ReadLe32( record + 10 );
            const uint8_t flags = record[25];
            const uint8_t nameLength = record[32];
            const uint8_t *rawName = record + 33;

            if ( 33 + nameLength > recordLength ) {
                continue;
            }
            if ( nameLength == 1 && ( rawName[0] == 0 || rawName[0] == 1 ) ) {
                continue; // "." and ".."
            }


            std::string name;
            if ( bJolietNames ) {
                name = StripIsoVersion( DecodeUcs2Be( rawName, nameLength ) );
            } else {
                name.assign( reinterpret_cast<const char *>( rawName ), nameLength );
                name = StripIsoVersion( name );
                // Rock Ridge NM (long names, mixed case) in the system use area.
                size_t sua = 33 + nameLength;
                if ( ( nameLength & 1 ) == 0 ) {
                    sua++; // padding after an even-length identifier
                }
                while ( sua + 4 <= recordLength ) {
                    const uint8_t *entry = record + sua;
                    const uint8_t entryLength = entry[2];
                    if ( entryLength < 4 || sua + entryLength > recordLength ) {
                        break;
                    }
                    if ( entry[0] == 'N' && entry[1] == 'M' && entryLength >= 5 ) {
                        const uint8_t rrFlags = entry[4];
                        name.assign( reinterpret_cast<const char *>( entry + 5 ),
                                     entryLength - 5 );
                        if ( ( rrFlags & 0x01 ) == 0 ) {
                            break; // the last NM entry of the name
                        }
                    }
                    sua += entryLength;
                }
            }
            if ( name.empty() || name == "." || name == ".." ) {
                continue;
            }

            const std::string path = JoinIsoPath( job.Path, name );
            IsoFile file;
            file.Path = path;
            file.Size = fileSize;
            file.bDirectory = ( flags & 0x02 ) != 0;
            file.Extents.push_back(
                { static_cast<uint64_t>( lba ) * kIsoSector, fileSize } );

            if ( file.bDirectory ) {
                mFiles.push_back( file );
                queue.push_back( { path, lba, fileSize } );
                pendingMultiExtent.clear();
                continue;
            }

            bool bAppended = false;
            if ( pendingMultiExtent == path ) {
                for ( IsoFile &existing : mFiles ) {
                    if ( !existing.bDirectory && existing.Path == path ) {
                        existing.Extents.push_back( file.Extents.front() );
                        existing.Size += fileSize;
                        bAppended = true;
                        break;
                    }
                }
            }
            if ( !bAppended ) {
                mFiles.push_back( file );
            }
            pendingMultiExtent = ( flags & 0x80 ) != 0 ? path : std::string();
        }
    }

    for ( const IsoFile &file : mFiles ) {
        if ( !file.bDirectory ) {
            mTotalBytes += file.Size;
        }
    }
    if ( mTotalBytes == 0 ) {
        outError = "the ISO 9660 tree holds no files";
        mFiles.clear();
        return false;
    }
    return true;
}

namespace {

bool UdfTagValid( const uint8_t *descriptor, uint16_t expectedId ) {
    uint8_t sum = 0;
    for ( int i = 0; i < 16; i++ ) {
        sum = static_cast<uint8_t>( sum + descriptor[i] );
    }
    return sum == 0 && ReadLe16( descriptor ) == expectedId;
}

} // namespace

bool IsoArchive::OpenUdf( std::string &outError ) {
    // Anchor Volume Descriptor Pointer at sector 256 (2048-byte sectors - every
    // DVD / Blu-ray image OpenTDU2 cares about).
    uint8_t anchor[kIsoSector] = {};
    if ( !ReadAt( 256ull * kIsoSector, anchor, kIsoSector ) ||
         !UdfTagValid( anchor, 2 ) ) {
        outError = "no UDF anchor (not a UDF image, or a non-2048 sector size)";
        return false;
    }
    const uint32_t vdsLocation = ReadLe32( anchor + 20 );
    if ( vdsLocation == 0 ) {
        outError = "the UDF anchor has no main volume descriptor sequence";
        return false;
    }

    uint32_t partitionStart = 0;
    bool bHasPartition = false;
    uint32_t logicalBlockSize = kIsoSector;
    uint32_t fileSetBlock = 0;
    std::string label;
    std::string revision;
    bool bMetadataPartition = false;

    for ( uint32_t index = 0; index < 64; index++ ) {
        std::vector<uint8_t> descriptor( kIsoSector );
        if ( !ReadAt( ( static_cast<uint64_t>( vdsLocation ) + index ) * kIsoSector,
                      descriptor.data(), kIsoSector ) ) {
            break;
        }
        const uint16_t id = ReadLe16( descriptor.data() );
        if ( !UdfTagValid( descriptor.data(), id ) ) {
            break;
        }
        if ( id == 8 ) {
            break; // terminating descriptor
        }
        if ( id == 5 ) {
            partitionStart = ReadLe32( descriptor.data() + 188 );
            bHasPartition = true;
        } else if ( id == 6 ) {
            logicalBlockSize = ReadLe32( descriptor.data() + 212 );
            const uint32_t mapTableLength = ReadLe32( descriptor.data() + 264 );
            fileSetBlock = ReadLe32( descriptor.data() + 252 );
            label = TrimSpaces( DecodeUdfString( descriptor.data() + 84, 128 ) );
            const uint16_t udfRevision = static_cast<uint16_t>(
                ( descriptor[240] << 8 ) | descriptor[241] );
            char revisionText[32];
            std::snprintf( revisionText, sizeof( revisionText ), "UDF %u.%02u",
                           udfRevision >> 8, udfRevision & 0xFFu );
            revision = revisionText;
            // A virtual partition map of the "*UDF Metadata Partition" kind is
            // the Blu-ray (UDF 2.50) layout; its file tree lives in a metadata
            // file this reader does not follow.
            if ( mapTableLength > 0 && 440u + mapTableLength <= kIsoSector ) {
                size_t mapPos = 440;
                const size_t mapEnd = 440 + mapTableLength;
                while ( mapPos + 2 <= mapEnd ) {
                    const uint8_t mapType = descriptor[mapPos];
                    const uint8_t mapLength = descriptor[mapPos + 1];
                    if ( mapLength < 2 || mapPos + mapLength > mapEnd ) {
                        break;
                    }
                    if ( mapType == 2 ) {
                        bMetadataPartition = true;
                    }
                    mapPos += mapLength;
                }
            }
        }
    }

    if ( !bHasPartition || fileSetBlock == 0 ) {
        outError = "no UDF partition / file set descriptor";
        return false;
    }
    if ( bMetadataPartition ) {
        outError = "UDF metadata partition (Blu-ray layout) is not supported";
        return false;
    }
    if ( logicalBlockSize < 512 || logicalBlockSize > 65536 ||
         ( logicalBlockSize & ( logicalBlockSize - 1 ) ) != 0 ) {
        logicalBlockSize = kIsoSector;
    }

    // File Set Descriptor: the logical volume contents use (a long_ad).
    std::vector<uint8_t> fileSet( kIsoSector );
    const uint64_t fileSetSector =
        static_cast<uint64_t>( partitionStart + fileSetBlock ) * logicalBlockSize;
    if ( !ReadAt( fileSetSector, fileSet.data(), kIsoSector ) ||
         !UdfTagValid( fileSet.data(), 256 ) ) {
        outError = "the UDF file set descriptor is unreadable";
        return false;
    }
    // File Set Descriptor: LogicalVolumeIdentifier at 112 (dstring), the root
    // directory ICB at 496 (long_ad: length 496, location 500).
    const std::string fileSetLabel = TrimSpaces( DecodeUdfString( fileSet.data() + 112, 128 ) );
    if ( !fileSetLabel.empty() ) {
        label = fileSetLabel;
    }
    const uint32_t rootBlock = ReadLe32( fileSet.data() + 500 );

    mVolume.Label = label;
    mVolume.Filesystem = revision.empty() ? "UDF" : revision;




    // --- File Entry reader --------------------------------------------------
    struct FileEntry {
        uint64_t InfoLength = 0;
        uint8_t FileType = 0;
        std::vector<IsoExtent> Extents;
        bool bOk = false;
    };

    const auto readFileEntry = [&]( uint32_t block, FileEntry &out ) -> bool {
        std::vector<uint8_t> entry( kIsoSector );
        const uint64_t sector =
            static_cast<uint64_t>( partitionStart + block ) * logicalBlockSize;
        if ( !ReadAt( sector, entry.data(), kIsoSector ) ) {
            return false;
        }
        const uint16_t id = ReadLe16( entry.data() );
        if ( id != 261 && id != 266 ) {
            return false;
        }
        if ( !UdfTagValid( entry.data(), id ) ) {
            return false;
        }
        out.FileType = entry[27];
        out.InfoLength = ReadLe64( entry.data() + 56 );
        uint32_t lengthEa = 0;
        uint32_t lengthAd = 0;
        uint32_t adOffset = 0;
        if ( id == 261 ) {
            lengthEa = ReadLe32( entry.data() + 168 );
            lengthAd = ReadLe32( entry.data() + 172 );
            adOffset = 176 + lengthEa;
        } else {
            lengthEa = ReadLe32( entry.data() + 208 );
            lengthAd = ReadLe32( entry.data() + 212 );
            adOffset = 216 + lengthEa;
        }
        const uint16_t adType = ReadLe16( entry.data() + 34 ) & 0x7;
        if ( adOffset >= kIsoSector ) {
            return false;
        }
        const uint32_t adEnd = std::min<uint32_t>( adOffset + lengthAd, kIsoSector );
        uint32_t pos = adOffset;
        while ( pos < adEnd ) {
            if ( adType == 3 ) { // data embedded in the file entry
                if ( pos + 4 > adEnd ) {
                    break;
                }
                const uint32_t length = ReadLe32( entry.data() + pos );
                pos += 4;
                if ( pos + length > adEnd ) {
                    break;
                }
                out.Extents.push_back( { sector + pos, length } );
                pos += ( length + 3u ) & ~3u;
            } else if ( adType == 0 ) { // short_ad
                if ( pos + 8 > adEnd ) {
                    break;
                }
                const uint32_t length = ReadLe32( entry.data() + pos ) & 0x3FFFFFFFu;
                const uint32_t location = ReadLe32( entry.data() + pos + 4 );
                out.Extents.push_back(
                    { static_cast<uint64_t>( partitionStart + location ) * logicalBlockSize,
                      length } );
                pos += 8;
            } else if ( adType == 1 ) { // long_ad
                if ( pos + 16 > adEnd ) {
                    break;
                }
                const uint32_t length = ReadLe32( entry.data() + pos ) & 0x3FFFFFFFu;
                const uint32_t location = ReadLe32( entry.data() + pos + 4 );
                out.Extents.push_back(
                    { static_cast<uint64_t>( partitionStart + location ) * logicalBlockSize,
                      length } );
                pos += 16;
            } else {
                return false; // extended_ad / unknown: report, do not guess
            }
        }
        out.bOk = true;
        return true;
    };

    const auto readExtents = [&]( const std::vector<IsoExtent> &extents, uint64_t total ) {
        std::vector<uint8_t> data;
        const uint64_t cap = std::min<uint64_t>( total, 64u * 1024u * 1024u );
        data.reserve( static_cast<size_t>( cap ) );
        for ( const IsoExtent &extent : extents ) {
            if ( data.size() >= total ) {
                break;
            }
            const uint64_t want = std::min<uint64_t>( extent.Size, total - data.size() );
            const size_t oldSize = data.size();
            data.resize( oldSize + static_cast<size_t>( want ) );
            if ( !ReadAt( extent.Offset, data.data() + oldSize,
                          static_cast<uint32_t>( want ) ) ) {
                data.resize( oldSize );
                break;
            }
        }
        return data;
    };

    struct DirectoryJob {
        std::string Path;
        uint32_t Block = 0;
    };
    std::vector<DirectoryJob> queue;
    queue.push_back( { std::string(), rootBlock } );

    for ( size_t jobIndex = 0; jobIndex < queue.size(); jobIndex++ ) {
        const DirectoryJob job = queue[jobIndex];
        FileEntry directory;
        if ( !readFileEntry( job.Block, directory ) || directory.FileType != 4 ) {
            continue;
        }
        const std::vector<uint8_t> data =
            readExtents( directory.Extents, directory.InfoLength );
        for ( size_t blockStart = 0; blockStart + 38 <= data.size();
              blockStart += logicalBlockSize ) {
            size_t pos = blockStart;
            const size_t blockEnd =
                std::min<size_t>( blockStart + logicalBlockSize, data.size() );
            while ( pos + 38 <= blockEnd ) {
                const uint32_t fidLength = ReadLe32( data.data() + pos );
                if ( fidLength < 38 || pos + fidLength > data.size() ) {
                    break;
                }
                const uint8_t characteristics = data[pos + 18];
                const uint8_t nameLength = data[pos + 19];
                const uint32_t icbBlock = ReadLe32( data.data() + pos + 24 );
                const std::string name =
                    nameLength > 0 ? DecodeUdfString( data.data() + pos + 38, nameLength )
                                   : std::string();
                pos += ( fidLength + 3u ) & ~3u;
                if ( name.empty() || name == "." || name == ".." ) {
                    continue;
                }
                const std::string path = JoinIsoPath( job.Path, name );
                if ( ( characteristics & 0x02 ) != 0 ) {
                    IsoFile directoryEntry;
                    directoryEntry.Path = path;
                    directoryEntry.bDirectory = true;
                    mFiles.push_back( directoryEntry );
                    queue.push_back( { path, icbBlock } );
                    continue;
                }
                if ( ( characteristics & 0x08 ) != 0 ) {
                    continue; // associated file: not part of the tree
                }
                FileEntry entry;
                if ( !readFileEntry( icbBlock, entry ) || entry.FileType != 5 ) {
                    continue; // a symlink or a special file
                }
                IsoFile file;
                file.Path = path;
                file.Size = entry.InfoLength;
                file.Extents = entry.Extents;
                mFiles.push_back( file );
            }
        }
    }

    for ( const IsoFile &file : mFiles ) {
        if ( !file.bDirectory ) {
            mTotalBytes += file.Size;
        }
    }
    if ( mTotalBytes == 0 ) {
        outError = "the UDF tree holds no files";
        mFiles.clear();
        return false;
    }
    return true;
}

} // namespace Launcher

