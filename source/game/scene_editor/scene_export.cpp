#include "shared.h"
#include "scene_export.h"

#include "filesystem/bank.h"
#include "filesystem/bank_write.h"
#include "filesystem/gs_file.h"
#include "game/scene_editor/scene_interior.h"
#include "game/cinematic/cinematic_audio.h"
#include "game/spots/obj_mesh.h"
#include "core/xmb_write.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_renderer.h"
#include "render/image_file.h"
#include "render/render_resources_write.h"
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

// --- M5 last: the composed exterior bank build (export_bank) ----------------

namespace {

// The composed bank's own materials: the .mtl materials as TD_Gouraud clones
// (the OBJ draw route's material law, eden obj_mesh.cpp BuildObjDraws).
void ComposeMaterial( const std::string &name, const ObjMaterialDesc &source, uint64_t shaderGUID,
                      MaterialDesc &out ) {
    out.Name = name;
    out.Hashcode = GetIdentifier64bit( name.c_str() );
    out.ShaderSetGUID = shaderGUID;
    out.DepthTest = 1;
    out.DepthWrite = 1;
    out.CullMode = 3;
    out.SrcBlend = 2;
    out.DstBlend = 1;
    out.DiffuseColor = source.Diffuse;
    out.AmbientColor = source.Diffuse;
    if ( source.Alpha < 0.999f ) {
        // Glass: SRCALPHA / INVSRCALPHA, depth tested but not written, two
        // sided (the OBJ route's bytes, eden obj_mesh.cpp).
        out.DepthWrite = 0;
        out.SrcBlend = 5;
        out.DstBlend = 6;
        out.DiffuseColor.w = source.Alpha;
        out.AmbientColor.w = source.Alpha;
    }
    // Spec permutation: S_NBTEXTURES + S_COORDTEXTURE when textured (the
    // all-zero spec is the no-texture flat white variant).
    if ( !source.Texture.empty() ) {
        out.Spec[ 0 ] = 1;
        out.Spec[ 1 ] = 1;
        MaterialTextureSlot slot;
        slot.SlotType = GetIdentifier64bit( "COLOR" );
        slot.TextureHashcode = GetIdentifier64bit( source.Texture.c_str() );
        slot.AddressU = 1;
        slot.AddressV = 1;
        out.Textures.push_back( slot );
        out.AllTextures.push_back( slot );
    }
}

// The spot-record upsert of export_spots (the shared law: the record keyed on
// "SN"+<id>, the template record of the same bank or the first, the 16-byte
// footer kept at the end).
bool UpsertSpotRecord( const SceneDocument &document, const std::string &bankField, const Vector3f &position,
                       float heading, std::string &outError, int32_t &outAppended, int32_t &outUpdated,
                       std::filesystem::path &outPath ) {
    constexpr uint32_t kRecord = 0x110;
    constexpr uint32_t kHeader = 0x20;
    constexpr uint32_t kSizeAt = 0x14;

    const std::string listPath =
        std::string( "Euro/Bnk/islands/" ) + document.Island + "/level/spots.bin";
    std::vector<uint8_t> list;
    if ( !gpFile->loadFile( listPath.c_str(), list ) || list.size() < kHeader ||
         memcmp( list.data(), ".SPT", 4 ) != 0 ) {
        outError = "no spots.bin for '" + document.Island + "'";
        return false;
    }
    const uint32_t recordCount = static_cast<uint32_t>( ( list.size() - kHeader ) / kRecord );
    if ( recordCount == 0 ) {
        outError = "the spots.bin of '" + document.Island + "' holds no records";
        return false;
    }
    const uint32_t recordsEnd = kHeader + recordCount * kRecord;
    std::vector<uint8_t> footer( list.begin() + recordsEnd, list.end() );
    list.resize( recordsEnd );

    const std::string spotName = "SN" + bankField;
    const uint64_t id = GetIdentifier64bit( spotName.c_str() );

    // The template: an existing record of the same bank, else the first.
    size_t templateIndex = 0;
    for ( uint32_t offset = kHeader; offset + kRecord <= kHeader + recordCount * kRecord; offset += kRecord ) {
        char bank[ 9 ] = {};
        memcpy( bank, list.data() + offset + 8, 8 );
        if ( _stricmp( bank, bankField.c_str() ) == 0 ) {
            templateIndex = ( offset - kHeader ) / kRecord;
            break;
        }
    }

    bool bUpdated = false;
    for ( uint32_t offset = kHeader; offset + kRecord <= kHeader + recordCount * kRecord; offset += kRecord ) {
        uint64_t recordId;
        memcpy( &recordId, list.data() + offset, 8 );
        if ( recordId == id ) {
            memcpy( list.data() + offset + 8, bankField.data(), Min<size_t>( 8, bankField.size() ) );
            memcpy( list.data() + offset + 0x10, &position, 12 );
            memcpy( list.data() + offset + 0x20, &heading, 4 );
            bUpdated = true;
            break;
        }
    }
    if ( !bUpdated ) {
        const uint32_t recordOffset = kHeader + static_cast<uint32_t>( templateIndex ) * kRecord;
        list.insert( list.end(), kRecord, 0 );
        memcpy( list.data() + kHeader + recordCount * kRecord, list.data() + recordOffset, kRecord );
        const uint32_t at = kHeader + recordCount * kRecord;
        memcpy( list.data() + at, &id, 8 );
        memcpy( list.data() + at + 8, bankField.data(), Min<size_t>( 8, bankField.size() ) );
        memcpy( list.data() + at + 0x10, &position, 12 );
        memcpy( list.data() + at + 0x20, &heading, 4 );
        outAppended = 1;
    } else {
        outUpdated = 1;
    }

    list.insert( list.end(), footer.begin(), footer.end() );
    const uint32_t size = static_cast<uint32_t>( list.size() - 0x10 );
    memcpy( list.data() + kSizeAt, &size, 4 );

    const std::filesystem::path modsRoot = gpFile->getModsRoot();
    outPath = modsRoot / "Euro" / "Bnk" / "islands" / document.Island / "level" / "spots.bin";
    std::error_code ec;
    std::filesystem::create_directories( outPath.parent_path(), ec );
    std::ofstream stream( outPath, std::ios::binary | std::ios::trunc );
    if ( !stream ) {
        outError = "cannot write '" + outPath.string() + "'";
        return false;
    }
    stream.write( reinterpret_cast<const char *>( list.data() ), static_cast<std::streamsize>( list.size() ) );
    return static_cast<bool>( stream );
}

struct BankProp {
    std::string Ref; // the OBJ name (props folder)
    Matrix4x4f Local; // anchor-local
};

} // namespace

SceneExportResult SceneExportBank( const SceneDocument &document, const std::string &bankName,
                                   EdenRenderer *pRenderer ) {
    SceneExportResult result;
    if ( gpFile == nullptr ) {
        result.Error = "no filesystem";
        return result;
    }
    if ( bankName.empty() ) {
        result.Error = "no bank name";
        return result;
    }
    if ( pRenderer == nullptr ) {
        result.Error = "no renderer (the shader family GUID comes from it)";
        return result;
    }
    const ShaderLibrary *pLibrary = pRenderer->getShaderSystem().findLibrary( "TD_Gouraud" );
    if ( pLibrary == nullptr ) {
        result.Error = "the TD_Gouraud shader library is not loaded";
        return result;
    }
    const uint64_t shaderGUID = pLibrary->getGUID();

    // The placements (props only; the other kinds export through export_spots
    // / export_pmi).
    std::vector<Matrix4x4f> worlds;
    document.resolveWorlds( worlds );
    std::vector<std::pair<size_t, Matrix4x4f>> propPlacements; // (object index, world)
    for ( size_t i = 0; i < document.Objects.size(); i++ ) {
        const SceneObject &object = document.Objects[ i ];
        if ( ( object.Kind != "prop" && object.Kind != "obj" ) || object.Ref.empty() || !object.bVisible ) {
            continue;
        }
        propPlacements.push_back( { i, worlds[ i ] } );
    }
    if ( propPlacements.empty() ) {
        result.Error = "the scene has no prop placements";
        return result;
    }

    // The anchor: a `spot` object named <bankName> (its composed placement),
    // else the first prop.
    Matrix4x4f anchor = propPlacements.front().second;
    for ( size_t i = 0; i < document.Objects.size(); i++ ) {
        const SceneObject &object = document.Objects[ i ];
        if ( object.Kind == "spot" && _stricmp( object.Ref.c_str(), bankName.c_str() ) == 0 ) {
            anchor = worlds[ i ];
            break;
        }
    }
    const Matrix4x4f anchorInverse = anchor.inverse();
    const Vector3f anchorPosition( anchor.m[ 3 ][ 0 ], anchor.m[ 3 ][ 1 ], anchor.m[ 3 ][ 2 ] );
    const float anchorHeading = std::atan2( -anchor.m[ 2 ][ 0 ], anchor.m[ 2 ][ 2 ] );

    // Parse the distinct OBJs; the shared materials and textures collect.
    std::vector<BankProp> props;
    struct PropModel {
        std::string Ref;
        uint64_t GeometryHash;
        std::vector<uint64_t> MaterialHashes; // the ref's MBNK order
        std::vector<AuthoredPrimitive> Primitives;
    };
    std::vector<PropModel> models;
    std::vector<MaterialDesc> allMaterials;
    std::unordered_map<std::string, uint64_t> materialIndex; // "<ref>::<mtl>" -> MBNK hash
    std::unordered_map<std::string, std::vector<uint64_t>> refMaterials;
    std::vector<std::string> textures; // the map_Kd strings, unique, in order

    for ( const auto &placement : propPlacements ) {
        const SceneObject &object = document.Objects[ placement.first ];
        PropModel *pModel = nullptr;
        for ( PropModel &model : models ) {
            if ( _stricmp( model.Ref.c_str(), object.Ref.c_str() ) == 0 ) {
                pModel = &model;
                break;
            }
        }
        if ( pModel == nullptr ) {
            std::vector<ObjGroupDesc> groups;
            std::vector<ObjMaterialDesc> objMaterials;
            if ( !LoadObjMeshes( "props/", object.Ref.c_str(), groups, objMaterials ) ) {
                result.Error = "the prop '" + object.Ref + "' did not load from <mods>/props/";
                return result;
            }
            PropModel model;
            model.Ref = object.Ref;
            model.GeometryHash = HashResourceName( ( "geometry:" + document.Island + ":" + object.Ref ).c_str() );
            for ( const ObjGroupDesc &group : groups ) {
                // The group's material: the .mtl entry (white default when the
                // OBJ had no usemtl / the name is missing).
                ObjMaterialDesc source;
                auto itDesc = std::find_if( objMaterials.begin(), objMaterials.end(),
                                            [ & ]( const ObjMaterialDesc &d ) { return d.Name == group.Material; } );
                if ( itDesc != objMaterials.end() ) {
                    source = *itDesc;
                }
                const std::string matKey = object.Ref + "::" + group.Material;
                auto itMat = materialIndex.find( matKey );
                if ( itMat == materialIndex.end() ) {
                    const std::string matName =
                        object.Ref + "_" + ( group.Material.empty() ? "default" : group.Material );
                    MaterialDesc desc;
                    ComposeMaterial( matName, source, shaderGUID, desc );
                    allMaterials.push_back( desc );
                    materialIndex[ matKey ] = GetIdentifier64bit( matName.c_str() );
                    if ( !source.Texture.empty() &&
                         std::find( textures.begin(), textures.end(), source.Texture ) == textures.end() ) {
                        textures.push_back( source.Texture );
                    }
                }
                model.MaterialHashes.push_back( materialIndex[ matKey ] );
                AuthoredPrimitive prim;
                prim.MaterialIndex = static_cast<uint16_t>( model.MaterialHashes.size() - 1 );
                prim.Positions.reserve( group.Vertices.size() * 3 );
                prim.Normals.reserve( group.Vertices.size() * 3 );
                prim.UVs.reserve( group.Vertices.size() * 2 );
                for ( const ObjVertex &vertex : group.Vertices ) {
                    prim.Positions.push_back( vertex.Position[ 0 ] );
                    prim.Positions.push_back( vertex.Position[ 1 ] );
                    prim.Positions.push_back( vertex.Position[ 2 ] );
                    prim.Normals.push_back( vertex.Normal[ 0 ] );
                    prim.Normals.push_back( vertex.Normal[ 1 ] );
                    prim.Normals.push_back( vertex.Normal[ 2 ] );
                    prim.UVs.push_back( vertex.UV[ 0 ] );
                    prim.UVs.push_back( vertex.UV[ 1 ] );
                }
                prim.Indices = group.Indices;
                model.Primitives.push_back( std::move( prim ) );
            }
            models.push_back( std::move( model ) );
            pModel = &models.back();
        }
        BankProp prop;
        prop.Ref = object.Ref;
        prop.Local = anchorInverse * placement.second;
        props.push_back( std::move( prop ) );
    }
    for ( const PropModel &model : models ) {
        refMaterials[ model.Ref ] = model.MaterialHashes;
    }

    // The authored files.
    const std::string bankField = ToLower( bankName ).substr( 0, 8 );
    const uint64_t modelRsc = HashResourceName( ( "model:" + document.Island + ":" + bankField ).c_str() );
    char rsc16[ 17 ];
    snprintf( rsc16, sizeof( rsc16 ), "%016llX", static_cast<unsigned long long>( modelRsc ) );
    const std::string stem = bankField + "_" + rsc16;
    const uint64_t sceneRsc = HashResourceName( ( "scene:" + document.Island + ":" + bankField ).c_str() );
    const uint64_t materialsRsc = HashResourceName( ( "materials:" + document.Island + ":" + bankField ).c_str() );
    const uint64_t rootHash = GetIdentifier64bit( ( "SN" + bankField + "_root" ).c_str() );

    // .3dg (one AuthoredGeometry per distinct prop OBJ).
    std::vector<AuthoredGeometry> geometries;
    for ( const PropModel &model : models ) {
        AuthoredGeometry geometry;
        geometry.Hashcode = model.GeometryHash;
        geometry.Primitives = model.Primitives;
        geometries.push_back( std::move( geometry ) );
    }
    std::vector<uint8_t> geometryBytes;
    std::string error;
    if ( !Build3dg( geometries, geometryBytes, error ) ) {
        result.Error = ".3dg: " + error;
        return result;
    }

    // .3dd (an instance per placement; the geometry hash lookup per ref).
    std::vector<AuthoredInstance> instances;
    for ( const BankProp &prop : props ) {
        uint64_t geometryHash = 0;
        for ( const PropModel &model : models ) {
            if ( _stricmp( model.Ref.c_str(), prop.Ref.c_str() ) == 0 ) {
                geometryHash = model.GeometryHash;
                break;
            }
        }
        AuthoredInstance instance;
        instance.Name = prop.Ref.substr( 0, 8 );
        instance.GeometryHashcode = geometryHash;
        instance.Local = prop.Local;
        auto itMats = refMaterials.find( prop.Ref );
        if ( itMats != refMaterials.end() ) {
            instance.Materials = itMats->second;
        }
        instances.push_back( std::move( instance ) );
    }
    std::vector<uint8_t> sceneBytes;
    if ( !Build3dd( rootHash, instances, sceneBytes, error ) ) {
        result.Error = ".3dd: " + error;
        return result;
    }

    // .2dm (the merged material library).
    std::vector<uint8_t> materialBytes;
    if ( !Build2dm( allMaterials, materialBytes ) ) {
        result.Error = ".2dm failed";
        return result;
    }

    // The .2db textures (the props folder's PNGs / JPGs / DDSes).
    std::vector<std::pair<std::string, std::vector<uint8_t>>> textureBytes;
    for ( const std::string &texture : textures ) {
        auto resolved = gpFile->resolvePath( ( std::string( "props/" ) + texture ).c_str() );
        std::vector<uint8_t> pixels;
        uint32_t width = 0, height = 0;
        if ( !resolved || !LoadImageFileRGBA( *resolved, pixels, width, height ) ) {
            result.Error = "the texture '" + texture + "' of '" + bankName + "' is not readable";
            return result;
        }
        std::vector<uint8_t> bytes;
        if ( !Build2dbFromRGBA( texture.c_str(), pixels.data(), width, height, true, bytes ) ) {
            result.Error = ".2db failed for '" + texture + "'";
            return result;
        }
        textureBytes.push_back( { texture, std::move( bytes ) } );
    }

    // entities.xmb + hashcodes.xmb (the XMB writer's fresh form; the two
    // documents the spot loader walks).
    XmbOutNode entitiesRoot = XmbOutNode::MakeComposite( "RootXmb" );
    {
        XmbOutNode bankType = XmbOutNode::MakeUInt32( 8 );
        bankType.Name = "___BankType";
        XmbOutNode bankSubType = XmbOutNode::MakeUInt32( 11 );
        bankSubType.Name = "___BankSubType";
        XmbOutNode nbSplit = XmbOutNode::MakeUInt32( 1 );
        nbSplit.Name = "___NbSplittedBank";
        XmbOutNode exportScenery = XmbOutNode::MakeComposite( "ExportType_Scenery" );
        exportScenery.Name = "ExportType_Scenery";
        XmbOutNode arrayOfScenery = XmbOutNode::MakeArray( "Scenery" );
        arrayOfScenery.Name = "ArrayOf_Scenery";
        XmbOutNode scenery = XmbOutNode::MakeComposite( "Scenery" );
        XmbOutNode hc = XmbOutNode::MakeUInt64( modelRsc );
        hc.Name = "__Hc";
        XmbOutNode name = XmbOutNode::MakeString( stem.c_str() );
        name.Name = "__Name";
        XmbOutNode modelArray = XmbOutNode::MakeArray( "__StructScDataRsc" );
        modelArray.Name = "Model";
        auto addRsc = [ & ]( uint64_t rsc, uint32_t subType ) {
            XmbOutNode entry = XmbOutNode::MakeComposite( "__StructScDataRsc" );
            XmbOutNode rscValue = XmbOutNode::MakeUInt64( rsc );
            rscValue.Name = "rsc";
            XmbOutNode typeValue = XmbOutNode::MakeUInt32( 0 );
            typeValue.Name = "rscType";
            XmbOutNode subValue = XmbOutNode::MakeUInt32( subType );
            subValue.Name = "rscSubType";
            entry.Children.push_back( std::move( rscValue ) );
            entry.Children.push_back( std::move( typeValue ) );
            entry.Children.push_back( std::move( subValue ) );
            modelArray.Children.push_back( std::move( entry ) );
        };
        addRsc( modelRsc, 0 );     // the .3dd (the loader reads this one)
        addRsc( sceneRsc, 1 );     // the .3dg
        addRsc( materialsRsc, 2 ); // the .2dm
        for ( const std::pair<std::string, std::vector<uint8_t>> &texture : textureBytes ) {
            addRsc( HashResourceName( ( "texture:" + document.Island + ":" + texture.first ).c_str() ), 3 );
        }
        scenery.Children.push_back( std::move( hc ) );
        scenery.Children.push_back( std::move( name ) );
        scenery.Children.push_back( std::move( modelArray ) );
        arrayOfScenery.Children.push_back( std::move( scenery ) );
        exportScenery.Children.push_back( std::move( arrayOfScenery ) );
        entitiesRoot.Children.push_back( std::move( bankType ) );
        entitiesRoot.Children.push_back( std::move( bankSubType ) );
        entitiesRoot.Children.push_back( std::move( nbSplit ) );
        entitiesRoot.Children.push_back( std::move( exportScenery ) );
    }
    std::vector<uint8_t> entitiesBytes;
    if ( !BuildXmb( entitiesRoot, entitiesBytes, error ) ) {
        result.Error = "entities.xmb: " + error;
        return result;
    }

    // hashcodes.xmb: the entry indices follow the pack order below
    // [0] hashcodes.xmb, [1] entities.xmb, [2] the .2dm, [3..] the .2dbs,
    // [3+T] the .3dd, [4+T] the .3dg.
    const uint32_t sceneIndex = 3 + static_cast<uint32_t>( textureBytes.size() );
    const uint32_t geometryIndex = sceneIndex + 1;
    const uint32_t materialsIndex = 2;
    XmbOutNode hashcodesRoot = XmbOutNode::MakeComposite( "RootXmb" );
    {
        XmbOutNode bankType = XmbOutNode::MakeUInt32( 8 );
        bankType.Name = "___BankType";
        XmbOutNode bankSubType = XmbOutNode::MakeUInt32( 10 );
        bankSubType.Name = "___BankSubType";
        XmbOutNode nbSplit = XmbOutNode::MakeUInt32( 1 );
        nbSplit.Name = "___NbSplittedBank";
        XmbOutNode array = XmbOutNode::MakeArray( "___CmdSaveFileInfoStruct" );
        array.Name = "___CmdSaveFileInfoArray";
        auto addFileInfo = [ & ]( uint32_t subType, const std::vector<uint32_t> &indices,
                                  const std::vector<uint64_t> &refs ) {
            XmbOutNode entry = XmbOutNode::MakeComposite( "___CmdSaveFileInfoStruct" );
            XmbOutNode typeValue = XmbOutNode::MakeUInt32( 0 );
            typeValue.Name = "Type";
            XmbOutNode subValue = XmbOutNode::MakeUInt32( subType );
            subValue.Name = "SubType";
            XmbOutNode fileIndices = XmbOutNode::MakeArray( "UINT32" );
            fileIndices.Name = "FileIndices";
            for ( uint32_t index : indices ) {
                fileIndices.Children.push_back( XmbOutNode::MakeUInt32( index ) );
            }
            XmbOutNode fileRefCount = XmbOutNode::MakeArray( "UINT32" );
            fileRefCount.Name = "FileRefCount";
            XmbOutNode fileReferences = XmbOutNode::MakeArray( "UINT64" );
            fileReferences.Name = "FileReferences";
            for ( uint64_t rsc : refs ) {
                fileReferences.Children.push_back( XmbOutNode::MakeUInt64( rsc ) );
            }
            XmbOutNode bankIndice = XmbOutNode::MakeArray( "UINT32" );
            bankIndice.Name = "BankIndice";
            for ( size_t i = 0; i < indices.size(); i++ ) {
                bankIndice.Children.push_back( XmbOutNode::MakeUInt32( 0 ) );
            }
            entry.Children.push_back( std::move( typeValue ) );
            entry.Children.push_back( std::move( subValue ) );
            entry.Children.push_back( std::move( fileIndices ) );
            entry.Children.push_back( std::move( fileRefCount ) );
            entry.Children.push_back( std::move( fileReferences ) );
            entry.Children.push_back( std::move( bankIndice ) );
            array.Children.push_back( std::move( entry ) );
        };
        addFileInfo( 0, { sceneIndex }, { modelRsc } );
        addFileInfo( 1, { geometryIndex }, { sceneRsc } );
        addFileInfo( 2, { materialsIndex }, { materialsRsc } );
        if ( !textureBytes.empty() ) {
            std::vector<uint32_t> texIndices;
            std::vector<uint64_t> texRefs;
            for ( uint32_t i = 0; i < textureBytes.size(); i++ ) {
                texIndices.push_back( 3 + i );
                texRefs.push_back(
                    HashResourceName( ( "texture:" + document.Island + ":" + textureBytes[ i ].first ).c_str() ) );
            }
            addFileInfo( 3, texIndices, texRefs );
        }
        hashcodesRoot.Children.push_back( std::move( bankType ) );
        hashcodesRoot.Children.push_back( std::move( bankSubType ) );
        hashcodesRoot.Children.push_back( std::move( nbSplit ) );
        hashcodesRoot.Children.push_back( std::move( array ) );
    }
    std::vector<uint8_t> hashcodesBytes;
    if ( !BuildXmb( hashcodesRoot, hashcodesBytes, error ) ) {
        result.Error = "hashcodes.xmb: " + error;
        return result;
    }

    // The bank pack (the entry order the FileIndices above count on).
    std::vector<BnkWriteEntry> entries;
    {
        BnkWriteEntry entry;
        entry.Path = "hashcodes.xmb";
        entry.Data = hashcodesBytes;
        entry.SubType = 10;
        entry.Group = 8;
        entries.push_back( entry );
        entry.Path = "entities.xmb";
        entry.Data = entitiesBytes;
        entry.SubType = 11;
        entry.Group = 8;
        entries.push_back( entry );
        entry.Path = stem + ".2dm";
        entry.Data = materialBytes;
        entry.SubType = 2;
        entry.Group = 0;
        entries.push_back( entry );
        for ( const std::pair<std::string, std::vector<uint8_t>> &texture : textureBytes ) {
            entry.Path = texture.first + ".2db";
            entry.Data = texture.second;
            entry.SubType = 3;
            entry.Group = 0;
            entries.push_back( entry );
        }
        entry.Path = stem + ".3dd";
        entry.Data = sceneBytes;
        entry.SubType = 0;
        entry.Group = 0;
        entries.push_back( entry );
        entry.Path = stem + ".3dg";
        entry.Data = geometryBytes;
        entry.SubType = 1;
        entry.Group = 0;
        entries.push_back( entry );
    }

    const std::vector<uint8_t> bankBytes = BuildBank( entries );
    if ( bankBytes.empty() ) {
        result.Error = "the bank pack failed";
        return result;
    }
    { // debug dump (gitignored _*)
        std::ofstream stream( "_composed_bank.bnk", std::ios::binary | std::ios::trunc );
        stream.write( reinterpret_cast<const char *>( bankBytes.data() ), static_cast<std::streamsize>( bankBytes.size() ) );
    }

    // The parse-back check: the bank must load and the rsc must resolve to
    // the .3dd (the spot loader's first lookup).
    {
        Bank check;
        std::vector<uint8_t> buffer( bankBytes.begin(), bankBytes.end() );
        const std::string checkName = bankField + ".bnk";
        if ( !check.loadFromMemory( std::move( buffer ), checkName.c_str() ) ) {
            result.Error = "the authored bank does not parse back";
            return result;
        }
        BankHashcodes hashcodes;
        hashcodes.load( check );
        const BankEntry *pScene = hashcodes.find( modelRsc, 0, 0 );
        if ( pScene == nullptr || pScene->Extension != "3dd" ) {
            result.Error = "the authored hashcodes do not resolve the model rsc";
            return result;
        }
        if ( check.findEntry( pScene->Name.c_str(), "3dg" ) == nullptr ||
             check.findEntry( pScene->Name.c_str(), "2dm" ) == nullptr ) {
            result.Error = "the authored geometry / materials are missing from the bank";
            return result;
        }
        XmbDocument entities;
        const BankEntry *pEntities = check.findEntry( "entities.xmb" );
        if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
            result.Error = "the authored entities.xmb does not parse";
            return result;
        }
        const XmbValue scenery = entities.getRoot()[ "ExportType_Scenery" ];
        if ( scenery.getArraySize( "ArrayOf_Scenery" ) != 1 ) {
            result.Error = "the authored entities.xmb has no scenery entry";
            return result;
        }
    }

    // Write the bank + the anchor's spot record.
    const std::filesystem::path modsRoot = gpFile->getModsRoot();
    const std::filesystem::path bankPath =
        modsRoot / "Euro" / "Bnk" / "islands" / document.Island / "exterior" / ( bankField + ".bnk" );
    {
        std::error_code ec;
        std::filesystem::create_directories( bankPath.parent_path(), ec );
        std::ofstream stream( bankPath, std::ios::binary | std::ios::trunc );
        if ( !stream ) {
            result.Error = "cannot write '" + bankPath.string() + "'";
            return result;
        }
        stream.write( reinterpret_cast<const char *>( bankBytes.data() ),
                      static_cast<std::streamsize>( bankBytes.size() ) );
    }
    int32_t appendedRecords = 0;
    int32_t updatedRecords = 0;
    std::filesystem::path listPath;
    if ( !UpsertSpotRecord( document, bankField, anchorPosition, anchorHeading, error, appendedRecords,
                            updatedRecords, listPath ) ) {
        result.Error = error;
        result.Path = bankPath.string();
        return result;
    }

    result.Appended = appendedRecords;
    result.BanksCopied = updatedRecords;
    result.Total = static_cast<int32_t>( instances.size() );
    result.Path = bankPath.string();
    OTDU_LOG_INFO(
        "SceneExport: bank '%s': %zu prop(s) -> %zu model(s), %zu material(s), %zu texture(s); %zu entries, "
        "%zu bytes; spot record %s\n",
        bankName.c_str(), props.size(), models.size(), allMaterials.size(), textureBytes.size(), entries.size(),
        bankBytes.size(), appendedRecords ? "appended" : "updated" );
    return result;
}
