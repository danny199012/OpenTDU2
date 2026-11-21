#include "shared.h"
#include "scene_export.h"

#include "filesystem/bank.h"
#include "filesystem/bank_write.h"
#include "filesystem/gs_file.h"
#include "game/scene_editor/scene_interior.h"
#include "render/eden/eden_model.h"
#include "world/eden_world.h"
#include "world/pmi.h"
#include "world/pmi_write.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

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

// --- M5: the library / actor placements into the sector .pmi lists ----------

namespace {

// A patched sector bank: every entry of the source with `patchedName`'s
// payload replaced. The layout laws are the ones the roundtrip proved
// (alignments, directory tree, extra section, stream tail); the CRCs are
// recomputed (zlib — the engine never verifies them, and the patched entry
// changes its own anyway).
void RebuildSectorBank( const std::vector<uint8_t> &sourceBytes, const Bank &bank, const std::string &patchedName,
                        const std::vector<uint8_t> &patchedBytes, const BnkDirectoryNode &directoryTree,
                        const std::vector<std::string> &directoryLeaves, std::vector<uint8_t> &out ) {
    BnkWriteOptions options;
    memcpy( &options.SectionAlignment, sourceBytes.data() + 8 + 0x18, 4 );
    memcpy( &options.DataAlignment, sourceBytes.data() + 8 + 0x1C, 4 );
    memcpy( &options.SizeWithoutPadding, sourceBytes.data() + 8 + 0x14, 4 );
    options.DirectoryTree = directoryTree;
    options.bHasDirectoryTree = true;
    // The tree's leaf order: the indexes map tree positions to table entries,
    // so the captured order is what keeps every name on its own payload.
    options.DirectoryOrder = directoryLeaves;

    std::vector<BnkWriteEntry> entries;
    entries.reserve( bank.getNumEntries() );
    for ( const BankEntry &entry : bank.getEntries() ) {
        BnkWriteEntry out;
        out.Path = entry.Path;
        out.SubType = entry.SubType;
        out.Group = entry.Group;
        if ( _stricmp( entry.Filename.c_str(), patchedName.c_str() ) == 0 ) {
            out.Data = patchedBytes;
        } else {
            out.Data.assign( entry.pData, entry.pData + entry.Size );
        }
        entries.push_back( std::move( out ) );
    }
    // The stream tail and the extra inter-section table (the roundtrip's laws).
    const auto u32At = [ & ]( uint32_t at ) {
        uint32_t v;
        memcpy( &v, sourceBytes.data() + at, 4 );
        return v;
    };
    const int32_t numEntries = static_cast<int32_t>( u32At( 8 + 0x20 ) );
    if ( numEntries > 0 ) {
        const uint32_t tableOffset = u32At( 8 + 0x28 );
        const uint32_t lastOffset = u32At( tableOffset + 8 + static_cast<uint32_t>( numEntries - 1 ) * 0x14 );
        const uint32_t lastSize = u32At( tableOffset + 8 + static_cast<uint32_t>( numEntries - 1 ) * 0x14 + 4 );
        const uint32_t sourceSize = u32At( 8 + 0x10 );
        const uint32_t lastEnd = lastOffset + lastSize;
        if ( lastEnd < sourceSize && sourceSize <= sourceBytes.size() ) {
            options.StreamTail.assign( sourceBytes.begin() + lastEnd, sourceBytes.begin() + sourceSize );
        }
        const uint32_t indexesOffset = u32At( 8 + 0x34 );
        const uint32_t indexesEnd = indexesOffset + 8 + u32At( indexesOffset );
        const uint32_t dataStart = u32At( 8 + 0x3C );
        if ( dataStart != 0 && indexesEnd < dataStart && dataStart <= sourceBytes.size() ) {
            options.ExtraSection.assign( sourceBytes.begin() + indexesEnd, sourceBytes.begin() + dataStart );
        }
    }
    out = BuildBank( entries, options );
}

struct PmiPlacement {
    uint64_t Hashcode = 0;
    uint8_t Type = 1;
    Vector3f Position;
    float Yaw = 0.0f;
    Vector3f Scale = Vector3f( 1.0f, 1.0f, 1.0f );
    Vector3f BoxMin; // world
    Vector3f BoxMax;
};

} // namespace

SceneExportResult SceneExportPmi( const SceneDocument &document, const EdenWorld &world ) {
    SceneExportResult result;
    if ( gpFile == nullptr ) {
        result.Error = "no filesystem";
        return result;
    }

    // The placements (library -> the -o list as type 1, actor -> -a as type
    // 2), resolved through the parent chains like the spot records.
    std::vector<Matrix4x4f> worlds;
    document.resolveWorlds( worlds );
    std::unordered_map<uint32_t, std::vector<PmiPlacement>> placementsBySector;
    for ( size_t i = 0; i < document.Objects.size(); i++ ) {
        const SceneObject &object = document.Objects[ i ];
        const bool bLibrary = object.Kind == "library";
        const bool bActor = object.Kind == "actor";
        if ( ( !bLibrary && !bActor ) || object.Ref.empty() ) {
            continue;
        }
        PmiPlacement placement;
        placement.Type = bLibrary ? 1 : 2;
        if ( bLibrary ) {
            // The library node hashcode: the ref is the node name, or
            // "hc:<16 hex>" when the browser could not recover a name.
            if ( object.Ref.size() > 3 && _strnicmp( object.Ref.c_str(), "hc:", 3 ) == 0 ) {
                placement.Hashcode = static_cast<uint64_t>( strtoull( object.Ref.c_str() + 3, nullptr, 16 ) );
            } else {
                placement.Hashcode = GetIdentifier64bit( object.Ref.c_str() );
            }
            if ( world.getLibraryNodes().find( placement.Hashcode ) == world.getLibraryNodes().end() ) {
                result.Error = "not a library node: '" + object.Ref + "'";
                continue;
            }
        } else {
            placement.Hashcode = GetIdentifier64bit( object.Ref.c_str() );
            if ( world.getActorNames().find( placement.Hashcode ) == world.getActorNames().end() ) {
                result.Error = "not a worldactors entity: '" + object.Ref + "'";
                continue;
            }
        }
        const Matrix4x4f &m = worlds[ i ];
        placement.Position = Vector3f( m.m[ 3 ][ 0 ], m.m[ 3 ][ 1 ], m.m[ 3 ][ 2 ] );
        placement.Yaw = std::atan2( -m.m[ 2 ][ 0 ], m.m[ 2 ][ 2 ] );
        const Vector3f c0( m.m[ 0 ][ 0 ], m.m[ 0 ][ 1 ], m.m[ 0 ][ 2 ] );
        const Vector3f c1( m.m[ 1 ][ 0 ], m.m[ 1 ][ 1 ], m.m[ 1 ][ 2 ] );
        const Vector3f c2( m.m[ 2 ][ 0 ], m.m[ 2 ][ 1 ], m.m[ 2 ][ 2 ] );
        placement.Scale = Vector3f( c0.length(), c1.length(), c2.length() );
        if ( bLibrary ) {
            const int32_t model = world.getLibraryNodes().at( placement.Hashcode );
            Vector3f bmin, bmax;
            if ( !world.getLibraryModel().getNodeBounds( model, bmin, bmax ) ) {
                result.Error = "the library model has no bounds: '" + object.Ref + "'";
                continue;
            }
            Vector3f wmin( 1e30f, 1e30f, 1e30f ), wmax( -1e30f, -1e30f, -1e30f );
            for ( int32_t corner = 0; corner < 8; corner++ ) {
                const Vector3f point( ( corner & 1 ) ? bmax.x : bmin.x, ( corner & 2 ) ? bmax.y : bmin.y,
                                      ( corner & 4 ) ? bmax.z : bmin.z );
                const Vector3f transformed = m.transformPoint( point );
                wmin = Vector3f( Min( wmin.x, transformed.x ), Min( wmin.y, transformed.y ),
                                 Min( wmin.z, transformed.z ) );
                wmax = Vector3f( Max( wmax.x, transformed.x ), Max( wmax.y, transformed.y ),
                                 Max( wmax.z, transformed.z ) );
            }
            placement.BoxMin = wmin;
            placement.BoxMax = wmax;
        } else {
            // The port's own actor proxy box (loadInstances' law).
            placement.BoxMin = placement.Position - Vector3f( 3.0f, 3.0f, 3.0f );
            placement.BoxMax = placement.Position + Vector3f( 3.0f, 6.0f, 3.0f );
        }
        const EdenWorld::SectorId id = EdenWorld::getSectorAt( placement.Position );
        const uint32_t key = static_cast<uint32_t>( ( id.AreaX * 8 + id.X ) << 20 ) ^
                             static_cast<uint32_t>( ( id.AreaY * 8 + id.Y ) << 12 );
        placementsBySector[ key ].push_back( placement );
    }
    if ( placementsBySector.empty() ) {
        result.Error = result.Error.empty() ? "the scene has no library / actor placements" : result.Error;
        return result;
    }
    // A bad ref only skips its placement (the note survives as the error).

    // Per sector: the overlay (or retail) bank, the lists patched, the bank
    // rebuilt into the overlay (re-exports stack: the overlay bank is the
    // next run's source; placements already present within 0.5 m are skipped).
    const std::filesystem::path modsRoot = gpFile->getModsRoot();
    int32_t patchedLists = 0;
    int32_t appended = 0;
    constexpr float kPosition = 0.0156252384185791f;
    std::unordered_map<uint32_t, EdenWorld::SectorId> sectorIds;
    for ( const auto &entry : placementsBySector ) {
        const PmiPlacement &first = entry.second.front();
        sectorIds[ entry.first ] = EdenWorld::getSectorAt( first.Position );
    }
    for ( const auto &sectorEntry : sectorIds ) {
        const EdenWorld::SectorId id = sectorEntry.second;
        auto it = placementsBySector.find( sectorEntry.first );
        if ( it == placementsBySector.end() ) {
            continue;
        }
        const std::string bankPath = EdenWorld::GetSectorBankPath( document.Island.c_str(), id );
        std::vector<uint8_t> bankBytes;
        if ( !gpFile->loadFile( bankPath.c_str(), bankBytes ) ) {
            result.Error = "no sector bank: " + bankPath;
            continue;
        }
        Bank bank;
        std::vector<uint8_t> bankBuffer( bankBytes.begin(), bankBytes.end() );
        if ( !bank.loadFromMemory( std::move( bankBuffer ), bankPath.c_str() ) ) {
            result.Error = "the sector bank does not parse: " + bankPath;
            continue;
        }
        BnkDirectoryNode directoryTree;
        std::vector<std::string> directoryLeaves;
        {
            const uint32_t dirOffset = [ & ]() {
                uint32_t v;
                memcpy( &v, bankBytes.data() + 8 + 0x30, 4 );
                return v;
            }();
            if ( dirOffset != 0 && dirOffset + 8 <= bankBytes.size() ) {
                const uint32_t dirSize = [ & ]() {
                    uint32_t v;
                    memcpy( &v, bankBytes.data() + dirOffset, 4 );
                    return v;
                }();
                CaptureBankDirectoryTree(
                    bankBytes.data() + dirOffset + 8,
                    bankBytes.data() + dirOffset + 8 +
                        Min<uint32_t>( dirSize, static_cast<uint32_t>( bankBytes.size() ) - dirOffset - 8 ),
                    directoryTree, directoryLeaves );
            }
        }
        const Vector3f origin = EdenWorld::getSectorOrigin( id );
        char sectorName[ 64 ];
        snprintf( sectorName, sizeof( sectorName ), "sector-%d-%d-%d-%d", id.AreaX, id.AreaY, id.X, id.Y );

        for ( const char *pSuffix : { "-o", "-a" } ) {
            const uint8_t wantType = pSuffix[ 1 ] == 'o' ? 1 : 2;
            std::vector<const PmiPlacement *> placements;
            for ( const PmiPlacement &placement : it->second ) {
                if ( placement.Type == wantType ) {
                    placements.push_back( &placement );
                }
            }
            if ( placements.empty() ) {
                continue;
            }
            const BankEntry *pEntry = bank.findEntry( ( std::string( sectorName ) + pSuffix ).c_str(), "pmi" );
            std::vector<uint8_t> originalPmi;
            const uint8_t *pOriginal = nullptr;
            uint32_t originalSize = 0;
            if ( pEntry != nullptr ) {
                originalPmi.assign( pEntry->pData, pEntry->pData + pEntry->Size );
                pOriginal = originalPmi.data();
                originalSize = static_cast<uint32_t>( originalPmi.size() );
            }
            std::vector<PmiGroup> groups;
            if ( pOriginal != nullptr ) {
                ParsePmiGroups( pOriginal, originalSize, groups );
            }
            // The dedupe: the same model within 0.5 m of a placement.
            std::vector<const PmiPlacement *> fresh;
            for ( const PmiPlacement *pPlacement : placements ) {
                bool bDuplicate = false;
                for ( const PmiGroup &group : groups ) {
                    if ( group.Hashcode != pPlacement->Hashcode ) {
                        continue;
                    }
                    for ( const PmiRecord &record : group.Records ) {
                        const float lx = record.W[ 0 ] * kPosition - 128.0f;
                        const float lz = record.W[ 2 ] * kPosition - 128.0f;
                        const float dx = origin.x + lx - pPlacement->Position.x;
                        const float dz = origin.z + lz - pPlacement->Position.z;
                        if ( dx * dx + dz * dz < 0.25f ) {
                            bDuplicate = true;
                            break;
                        }
                    }
                    if ( bDuplicate ) {
                        break;
                    }
                }
                if ( !bDuplicate ) {
                    fresh.push_back( pPlacement );
                }
            }
            if ( fresh.empty() ) {
                continue;
            }

            // The new records (sector-local; the box relative to the point),
            // merged into the group of the same hashcode (or appended).
            int32_t placed = 0;
            for ( const PmiPlacement *pPlacement : fresh ) {
                PmiGroup *pExistingGroup = nullptr;
                for ( PmiGroup &group : groups ) {
                    if ( group.Hashcode == pPlacement->Hashcode && group.Type == pPlacement->Type ) {
                        pExistingGroup = &group;
                        break;
                    }
                }
                PmiRecord record = EncodePmiRecord(
                    Vector3f( pPlacement->Position.x - origin.x, 0.0f, pPlacement->Position.z - origin.z ),
                    pPlacement->Position.y, pPlacement->Scale, 0.0f, pPlacement->Yaw, 0.0f,
                    pPlacement->BoxMin - pPlacement->Position, pPlacement->BoxMax - pPlacement->Position );
                if ( pExistingGroup != nullptr ) {
                    if ( !pExistingGroup->Records.empty() && !pExistingGroup->Records.back().PayloadRegion.empty() ) {
                        // A payload group (the skinned statics' bone palettes):
                        // the new record rides the same palette (the retail
                        // bones are rigid per model, pmi_spec 1.5).
                        record.PayloadRegion = pExistingGroup->Records.back().PayloadRegion;
                    }
                    pExistingGroup->Records.push_back( std::move( record ) );
                } else {
                    PmiGroup group;
                    group.Hashcode = pPlacement->Hashcode;
                    group.Type = pPlacement->Type;
                    group.Records.push_back( std::move( record ) );
                    groups.push_back( std::move( group ) );
                }
                placed++;
            }
            std::vector<uint8_t> patched;
            if ( !BuildPmi( groups, pOriginal, originalSize, patched ) ) {
                result.Error = "the list did not rebuild: " + std::string( sectorName ) + pSuffix;
                continue;
            }
            const std::string patchedName = std::string( sectorName ) + pSuffix + ".pmi";
            std::vector<uint8_t> rebuiltBank;
            RebuildSectorBank( bankBytes, bank, patchedName, patched, directoryTree, directoryLeaves, rebuiltBank );
            Bank check;
            std::vector<uint8_t> checkBuffer( rebuiltBank.begin(), rebuiltBank.end() );
            if ( !check.loadFromMemory( std::move( checkBuffer ), bankPath.c_str() ) ) {
                result.Error = "the rebuilt sector bank does not parse: " + bankPath;
                continue;
            }
            std::string nativePath = bankPath;
            std::replace( nativePath.begin(), nativePath.end(), '/', '\\' );
            const std::filesystem::path destination = modsRoot / std::filesystem::path( nativePath );
            if ( !WriteFile( destination, rebuiltBank ) ) {
                result.Error = "cannot write '" + destination.string() + "'";
                continue;
            }
            patchedLists++;
            appended += placed;
        }
    }
    result.Appended = appended;
    result.Total = patchedLists;
    result.Path = modsRoot.string();
    if ( result.Error.empty() ) {
        OTDU_LOG_INFO( "SceneExport: pmi: %d instance(s) appended into %d list(s)\n", appended, patchedLists );
    } else {
        OTDU_LOG_WARN( "SceneExport: pmi: %s\n", result.Error.c_str() );
    }
    return result;
}
