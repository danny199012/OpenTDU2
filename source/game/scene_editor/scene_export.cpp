#include "shared.h"
#include "scene_export.h"

#include "filesystem/gs_file.h"
#include "game/scene_editor/scene_interior.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace {
constexpr uint32_t kRecord = 0x110; // bytes per spot record
constexpr uint32_t kHeader = 0x20;  // records start here
constexpr uint32_t kSizeAt = 0x14;  // u32: the record region's byte size

// The list's own fields (the .SPT law of LoadSpotList / add_spot.py).
bool IsSpotList( const std::vector<uint8_t> &data ) {
    return data.size() >= kHeader && memcmp( data.data(), ".SPT", 4 ) == 0;
}

uint32_t ListSize( const std::vector<uint8_t> &data ) {
    uint32_t size = 0;
    memcpy( &size, data.data() + kSizeAt, 4 );
    return size;
}

void PutU32( std::vector<uint8_t> &data, uint32_t offset, uint32_t value ) {
    memcpy( data.data() + offset, &value, 4 );
}

// Patches the record's id / bank / position / heading in place (the
// template's unknown bytes + the category at +0x100 ride along - the same
// law add_spot.py uses).
void PatchRecord( std::vector<uint8_t> &data, uint32_t offset, uint64_t id, const std::string &bankField,
                  const Vector3f &position, float heading ) {
    memcpy( data.data() + offset, &id, 8 );
    memset( data.data() + offset + 8, 0, 8 );
    memcpy( data.data() + offset + 8, bankField.data(), Min<size_t>( 8, bankField.size() ) );
    memcpy( data.data() + offset + 0x10, &position, 12 );
    memcpy( data.data() + offset + 0x20, &heading, 4 );
}

// A records-eye view for the dedupe / template search.
struct ExistingRecord {
    uint64_t Id;
    std::string Bank;
    Vector3f Position;
    float Heading;
};

std::vector<ExistingRecord> ReadRecords( const std::vector<uint8_t> &data ) {
    std::vector<ExistingRecord> out;
    const uint32_t size = Min<uint32_t>( ListSize( data ), static_cast<uint32_t>( data.size() ) - kHeader );
    for ( uint32_t offset = kHeader; offset + kRecord <= kHeader + size; offset += kRecord ) {
        ExistingRecord record;
        memcpy( &record.Id, data.data() + offset, 8 );
        char bank[ 9 ] = {};
        memcpy( bank, data.data() + offset + 8, 8 );
        record.Bank = bank;
        memcpy( &record.Position, data.data() + offset + 0x10, 12 );
        memcpy( &record.Heading, data.data() + offset + 0x20, 4 );
        out.push_back( record );
    }
    return out;
}

bool WriteFile( const std::filesystem::path &path, const std::vector<uint8_t> &data ) {
    std::error_code ec;
    std::filesystem::create_directories( path.parent_path(), ec );
    std::ofstream stream( path, std::ios::binary | std::ios::trunc );
    if ( !stream ) {
        return false;
    }
    stream.write( reinterpret_cast<const char *>( data.data() ), static_cast<std::streamsize>( data.size() ) );
    return static_cast<bool>( stream );
}
} // namespace

SceneExportResult SceneExportSpots( const SceneDocument &document ) {
    SceneExportResult result;
    if ( gpFile == nullptr ) {
        result.Error = "no filesystem";
        return result;
    }

    // The island's own list through the VFS: the overlay copy when a
    // previous export wrote one (the exports stack, add_spot.py's law), the
    // retail list otherwise.
    const std::string listPath =
        std::string( "Euro/Bnk/islands/" ) + document.Island + "/level/spots.bin";
    std::vector<uint8_t> list;
    if ( !gpFile->loadFile( listPath.c_str(), list ) || !IsSpotList( list ) || list.size() < kHeader ) {
        result.Error = "no spots.bin for '" + document.Island + "'";
        return result;
    }
    // The list's own layout: [0x20 header][records][footer] - the retail
    // files carry a 16-byte tail the size field (fileSize - 0x10) counts in,
    // so the record count comes off the FILE length, and the footer moves to
    // the end when records are appended.
    const uint32_t recordCount = static_cast<uint32_t>( ( list.size() - kHeader ) / kRecord );
    if ( recordCount == 0 ) {
        result.Error = "the spots.bin of '" + document.Island + "' holds no records";
        return result;
    }
    const uint32_t recordsEnd = kHeader + recordCount * kRecord;
    std::vector<uint8_t> footer( list.begin() + recordsEnd, list.end() );
    list.resize( recordsEnd );

    // The composed placements (spot + interior objects; the parent chains
    // resolve the same law the editor's records use).
    std::vector<Matrix4x4f> worlds;
    document.resolveWorlds( worlds );

    struct Placement {
        std::string Id;         // the doc object's id (the record name seed)
        std::string BankField;  // the record's 8-char bank
        Vector3f Position;
        float Heading;
        bool bInterior;         // interior placements copy their bank
        std::string InteriorRef;
    };
    std::vector<Placement> placements;
    for ( size_t i = 0; i < document.Objects.size(); i++ ) {
        const SceneObject &object = document.Objects[ i ];
        const bool bSpot = object.Kind == "spot";
        const bool bInterior = object.Kind == "interior";
        if ( ( !bSpot && !bInterior ) || object.Ref.empty() ) {
            continue;
        }
        Placement placement;
        placement.Id = object.Id;
        placement.bInterior = bInterior;
        if ( bInterior ) {
            // The bank field: the interior stem truncated to 8 (the record's
            // own bank field width; the file is written under that name).
            std::string stem = SceneInterior::DescribeRef( object.Ref );
            if ( stem.size() > 8 ) {
                stem.resize( 8 );
            }
            placement.BankField = ToLower( stem );
            placement.InteriorRef = object.Ref;
        } else {
            placement.BankField = ToLower( object.Ref );
        }
        const Matrix4x4f &world = worlds[ i ];
        placement.Position = Vector3f( world.m[ 3 ][ 0 ], world.m[ 3 ][ 1 ], world.m[ 3 ][ 2 ] );
        placement.Heading = std::atan2( -world.m[ 2 ][ 0 ], world.m[ 2 ][ 2 ] );
        placements.push_back( std::move( placement ) );
    }
    if ( placements.empty() ) {
        result.Error = "the scene has no spot / interior placements";
        return result;
    }

    const std::vector<ExistingRecord> existing = ReadRecords( list );
    const uint32_t oldSize = recordCount * kRecord;
    const uint32_t oldCount = recordCount;

    int32_t appended = 0;
    int32_t updated = 0;
    for ( const Placement &placement : placements ) {
        // The record name (the id's preimage): scene-unique and position
        // INDEPENDENT, so a re-export of a moved object rewrites its record
        // (upsert) instead of appending a second one.
        const std::string spotName = "SN" + placement.Id;
        const uint64_t id = GetIdentifier64bit( spotName.c_str() );

        // The template: an existing record of the same bank (its unknown
        // bytes + category are the building family's own), else the first.
        size_t templateIndex = 0;
        for ( size_t i = 0; i < existing.size(); i++ ) {
            if ( _stricmp( existing[ i ].Bank.c_str(), placement.BankField.c_str() ) == 0 ) {
                templateIndex = i;
                break;
            }
        }

        // Upsert by id: a re-export rewrites the moved record in place.
        bool bUpdated = false;
        for ( uint32_t offset = kHeader; offset + kRecord <= kHeader + oldSize; offset += kRecord ) {
            uint64_t recordId;
            memcpy( &recordId, list.data() + offset, 8 );
            if ( recordId == id ) {
                PatchRecord( list, offset, id, placement.BankField, placement.Position, placement.Heading );
                bUpdated = true;
                break;
            }
        }
        if ( bUpdated ) {
            updated++;
            continue;
        }
        // Append: the template record's bytes with the fields rewritten.
        const uint32_t recordOffset = kHeader + static_cast<uint32_t>( templateIndex ) * kRecord;
        list.insert( list.end(), kRecord, 0 );
        memcpy( list.data() + kHeader + oldSize + static_cast<uint32_t>( appended ) * kRecord,
                list.data() + recordOffset, kRecord );
        PatchRecord( list, kHeader + oldSize + static_cast<uint32_t>( appended ) * kRecord, id,
                     placement.BankField, placement.Position, placement.Heading );
        appended++;
    }

    // The interior banks: the reader resolves every exterior under
    // islands/<island>/exterior/, so the referenced rooms are copied there
    // (once; the file name is the bank field).
    const std::filesystem::path modsRoot = gpFile->getModsRoot();
    int32_t banksCopied = 0;
    for ( const Placement &placement : placements ) {
        if ( !placement.bInterior ) {
            continue;
        }
        const std::string sourcePath = SceneInterior::ResolveBankPath( placement.InteriorRef );
        if ( sourcePath.empty() ) {
            result.Error = "no interior bank for '" + placement.InteriorRef + "'";
            continue;
        }
        const std::filesystem::path destination =
            modsRoot / "Euro" / "Bnk" / "islands" / document.Island / "exterior" /
            ( placement.BankField + ".bnk" );
        std::error_code ec;
        if ( std::filesystem::exists( destination, ec ) ) {
            continue;
        }
        std::vector<uint8_t> bankBytes;
        if ( !gpFile->loadFile( sourcePath.c_str(), bankBytes ) ) {
            result.Error = "the bank of '" + placement.InteriorRef + "' could not be read";
            continue;
        }
        if ( WriteFile( destination, bankBytes ) ) {
            banksCopied++;
        } else {
            result.Error = "cannot write '" + destination.string() + "'";
        }
    }

    // The footer back at the end; the size field stays the file's own law
    // (its bytes after the first 16 - LoadSpotList reads to the file end
    // anyway, min(fileSize, 0x20 + field)).
    list.insert( list.end(), footer.begin(), footer.end() );
    PutU32( list, kSizeAt, static_cast<uint32_t>( list.size() - 0x10 ) );

    const std::filesystem::path destination =
        modsRoot / "Euro" / "Bnk" / "islands" / document.Island / "level" / "spots.bin";
    if ( !WriteFile( destination, list ) ) {
        result.Error = "cannot write '" + destination.string() + "'";
        return result;
    }
    result.Appended = appended;
    result.BanksCopied = banksCopied;
    result.Total = static_cast<int32_t>( oldCount + appended );
    result.Path = destination.string();
    if ( result.Error.empty() ) {
        OTDU_LOG_INFO(
            "SceneExport: %s: %d record(s) appended, %d updated, %d bank(s) copied -> %d records total\n",
            result.Path.c_str(), appended, updated, banksCopied, result.Total );
    } else {
        OTDU_LOG_WARN( "SceneExport: %s (%s)\n", result.Error.c_str(), result.Path.c_str() );
    }
    return result;
}
