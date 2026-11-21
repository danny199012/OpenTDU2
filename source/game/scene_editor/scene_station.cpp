#include "shared.h"
#include "gm_scene_editor.h"

#include "core/logger.h"
#include "filesystem/gs_file.h"
#include "game/map_screen.h"
#include "game/spots/mod_props.h"
#include "game/spots/mod_roads.h"
#include "render/image_file.h"
#include "world/eden_world.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

// The petrol station tool (docs/re/scene_editor.md 11). A station record (SceneStation) generates:
//   <id>_ground  a short wide road: the forecourt slab ("road ground", the same ribbon + collision + surface
//                registration every scene road gets, so the car drives on it and the grass is suppressed);
//   <id>_link    a drive from the slab's nearer end to the nearest retail (ROAD_MAP) road point;
//   <id>_canopy  a `prop` object (the generated station_canopy.obj: roof on four posts + two pump islands).
// and the export writes the stations.txt line that makes the place WORK (pump screen, repair row, map icon,
// forecourt lights - service_stations.cpp). All generated art is our own (flat colours, no game texture).

namespace {

constexpr const char *kCanopyObject = "station_canopy";
constexpr float kSlabClearance = 0.05f; // the road ribbon sits 5 cm over the ground (the ModRoads law)

// A box as 6 quads with outward normals (the OBJ is authored right-handed CCW; the prop loader mirrors X +
// reverses the winding). `mat` is the usemtl name.
void AppendBox( std::string &obj, int32_t &vertexBase, int32_t &normalBase, int32_t &uvBase, const char *mat,
                const Vector3f &a, const Vector3f &b ) {
    obj += std::string( "usemtl " ) + mat + "\n";
    struct Face {
        Vector3f N;
        Vector3f V[4];
    };
    const Face faces[6] = {
        { Vector3f( 0, 1, 0 ), { Vector3f( a.x, b.y, b.z ), Vector3f( b.x, b.y, b.z ), Vector3f( b.x, b.y, a.z ), Vector3f( a.x, b.y, a.z ) } },
        { Vector3f( 0, -1, 0 ), { Vector3f( a.x, a.y, a.z ), Vector3f( b.x, a.y, a.z ), Vector3f( b.x, a.y, b.z ), Vector3f( a.x, a.y, b.z ) } },
        { Vector3f( 0, 0, 1 ), { Vector3f( a.x, a.y, b.z ), Vector3f( b.x, a.y, b.z ), Vector3f( b.x, b.y, b.z ), Vector3f( a.x, b.y, b.z ) } },
        { Vector3f( 0, 0, -1 ), { Vector3f( b.x, a.y, a.z ), Vector3f( a.x, a.y, a.z ), Vector3f( a.x, b.y, a.z ), Vector3f( b.x, b.y, a.z ) } },
        { Vector3f( 1, 0, 0 ), { Vector3f( b.x, a.y, b.z ), Vector3f( b.x, a.y, a.z ), Vector3f( b.x, b.y, a.z ), Vector3f( b.x, b.y, b.z ) } },
        { Vector3f( -1, 0, 0 ), { Vector3f( a.x, a.y, a.z ), Vector3f( a.x, a.y, b.z ), Vector3f( a.x, b.y, b.z ), Vector3f( a.x, b.y, a.z ) } },
    };
    (void)normalBase;
    (void)uvBase;
    char line[160];
    for ( const Face &f : faces ) {
        for ( int32_t i = 0; i < 4; i++ ) {
            snprintf( line, sizeof( line ), "v %.3f %.3f %.3f\nvn %.0f %.0f %.0f\nvt %d %d\n", f.V[i].x, f.V[i].y, f.V[i].z, f.N.x, f.N.y,
                      f.N.z, i == 1 || i == 2 ? 1 : 0, i >= 2 ? 1 : 0 );
            obj += line;
        }
        const int32_t i0 = vertexBase + 1;
        snprintf( line, sizeof( line ), "f %d/%d/%d %d/%d/%d %d/%d/%d\nf %d/%d/%d %d/%d/%d %d/%d/%d\n", i0, i0, i0, i0 + 1, i0 + 1, i0 + 1,
                  i0 + 2, i0 + 2, i0 + 2, i0, i0, i0, i0 + 2, i0 + 2, i0 + 2, i0 + 3, i0 + 3, i0 + 3 );
        obj += line;
        vertexBase += 4;
    }
}

bool WriteTextFile( const std::filesystem::path &path, const std::string &text ) {
    std::ofstream stream( path, std::ios::binary | std::ios::trunc );
    if ( !stream.is_open() ) {
        return false;
    }
    stream << text;
    return stream.good();
}

// Writes the canopy OBJ / MTL / flat-colour PNGs into <mods>/props once (our own art).
bool EnsureCanopyAssets() {
    if ( gpFile == nullptr ) {
        return false;
    }
    const std::filesystem::path folder = gpFile->getModsRoot() / "props";
    std::error_code ec;
    std::filesystem::create_directories( folder, ec );
    struct Colour {
        const char *Name;
        uint8_t R, G, B;
    };
    const Colour colours[] = { { "station_roof", 196, 36, 36 }, { "station_post", 225, 225, 225 }, { "station_pump", 60, 64, 72 } };
    for ( const Colour &c : colours ) {
        const std::filesystem::path png = folder / ( std::string( c.Name ) + ".png" );
        if ( !std::filesystem::exists( png ) ) {
            std::vector<uint8_t> rgba( 8 * 8 * 4 );
            for ( size_t i = 0; i < 64; i++ ) {
                rgba[i * 4 + 0] = c.R;
                rgba[i * 4 + 1] = c.G;
                rgba[i * 4 + 2] = c.B;
                rgba[i * 4 + 3] = 255;
            }
            if ( !SaveImagePng( png.string().c_str(), rgba.data(), 8, 8 ) ) {
                return false;
            }
        }
    }
    const std::filesystem::path objPath = folder / ( std::string( kCanopyObject ) + ".obj" );
    const std::filesystem::path mtlPath = folder / ( std::string( kCanopyObject ) + ".mtl" );
    if ( std::filesystem::exists( objPath ) && std::filesystem::exists( mtlPath ) ) {
        return true;
    }
    // Local frame: the slab's long axis is +Z, the roof 8 m across x 14 m along z, 5 m up.
    std::string obj = std::string( "# OpenTDU2 station canopy (generated)\nmtllib " ) + kCanopyObject + ".mtl\n";
    int32_t vb = 0, nb = 0, ub = 0;
    AppendBox( obj, vb, nb, ub, "roof", Vector3f( -4.5f, 4.6f, -7.5f ), Vector3f( 4.5f, 5.2f, 7.5f ) );
    for ( const float x : { -3.6f, 3.6f } ) {
        for ( const float z : { -6.6f, 6.6f } ) {
            AppendBox( obj, vb, nb, ub, "post", Vector3f( x - 0.2f, 0.0f, z - 0.2f ), Vector3f( x + 0.2f, 4.6f, z + 0.2f ) );
        }
    }
    for ( const float z : { -3.5f, 3.5f } ) {
        AppendBox( obj, vb, nb, ub, "pump", Vector3f( -0.4f, 0.0f, z - 1.2f ), Vector3f( 0.4f, 1.5f, z + 1.2f ) );
    }
    std::string mtl;
    for ( const char *name : { "roof", "post", "pump" } ) {
        mtl += std::string( "newmtl " ) + name + "\nKd 1 1 1\nmap_Kd station_" + name + ".png\n\n";
    }
    return WriteTextFile( objPath, obj ) && WriteTextFile( mtlPath, mtl );
}

float YawDegrees( const Vector3f &direction ) {
    return std::atan2( direction.x, direction.z ) * 180.0f / kPI;
}

} // namespace

bool GMSceneEditor::findRetailRoad( const Vector3f &from, float maxDistance, Vector3f &outPoint ) {
    if ( !roadGraph || roadGraphIsland != island ) {
        roadGraph = std::make_unique<MapScreen>();
        roadGraph->loadRoads( island );
        roadGraphIsland = island;
    }
    return roadGraph->getRoadPointNear( from, maxDistance, outPoint );
}

void GMSceneEditor::removeStationParts( const std::string &stationId ) {
    for ( size_t i = doc.Roads.size(); i-- > 0; ) {
        if ( doc.Roads[i].Id == stationId + "_ground" || doc.Roads[i].Id == stationId + "_link" ) {
            doc.Roads.erase( doc.Roads.begin() + i );
        }
    }
    doc.removeObject( stationId + "_canopy" );
}

std::string GMSceneEditor::rebuildStation( const std::string &stationId ) {
    SceneStation *pStation = doc.findStation( stationId );
    if ( pStation == nullptr ) {
        return "no such station: " + stationId;
    }
    SceneStation station = *pStation;
    removeStationParts( stationId );
    EdenWorld *pWorld = bWorldReady ? &view.getWorld() : nullptr;
    bool bGroundMissed = false; // the terrain is not streamed there (fly the camera over it first)
    auto groundAt = [&]( float x, float z, float hint ) {
        float ground = hint;
        if ( pWorld == nullptr || !pWorld->getSurfaceHeight( x, z, hint + 60.0f, ground ) ) {
            bGroundMissed = true;
            ground = hint;
        }
        return ground;
    };

    // The nearest retail road decides the auto yaw (the slab's axis points at it) and the drive.
    Vector3f road;
    const bool bRoad = station.bConnect && findRetailRoad( station.Position, station.MaxLink, road );
    std::string status;
    if ( bRoad && station.Yaw == FLT_MAX ) {
        station.Yaw = YawDegrees( Vector3f( road.x - station.Position.x, 0.0f, road.z - station.Position.z ) );
    } else if ( station.Yaw == FLT_MAX ) {
        station.Yaw = 0.0f;
    }
    const float yawRad = station.Yaw * kPI / 180.0f;
    const Vector3f axis( std::sin( yawRad ), 0.0f, std::cos( yawRad ) );
    const float half = station.Length * 0.5f;
    Vector3f endA = station.Position - axis * half, endB = station.Position + axis * half;
    // The slab's ground: the surface at the centre (a flat forecourt), the ribbon lifts where the terrain rises.
    const float centreY = groundAt( station.Position.x, station.Position.z, station.Position.y );
    station.Position.y = centreY;
    endA.y = endB.y = centreY;

    SceneRoad ground;
    ground.Id = stationId + "_ground";
    ground.Points = { endA, station.Position, endB };
    ground.Width = station.Width;
    ground.Lanes = 2;
    ground.bCollide = true;
    doc.Roads.push_back( ground );

    if ( bRoad ) {
        // The slab end nearer the road is the entrance.
        const float dA = ( endA.x - road.x ) * ( endA.x - road.x ) + ( endA.z - road.z ) * ( endA.z - road.z );
        const float dB = ( endB.x - road.x ) * ( endB.x - road.x ) + ( endB.z - road.z ) * ( endB.z - road.z );
        const Vector3f entry = dA < dB ? endA : endB;
        const Vector3f toRoad( road.x - entry.x, 0.0f, road.z - entry.z );
        const float full = toRoad.length();
        if ( full > 3.0f ) {
            SceneRoad link;
            link.Id = stationId + "_link";
            link.Points.push_back( entry );
            for ( const float t : { 0.33f, 0.66f } ) {
                const float x = entry.x + toRoad.x * t, z = entry.z + toRoad.z * t;
                link.Points.emplace_back( x, groundAt( x, z, entry.y ), z );
            }
            link.Points.push_back( road ); // ends on the retail road at its own height
            link.Width = 7.0f;
            link.Lanes = 2;
            link.bCollide = true;
            doc.Roads.push_back( link );
            char text[160];
            snprintf( text, sizeof( text ), "drive %.0f m to the retail road at (%.0f, %.0f)", full, road.x, road.z );
            status = text;
        } else {
            status = "the forecourt already touches the retail road";
        }
    } else if ( station.bConnect ) {
        char text[160];
        snprintf( text, sizeof( text ), "NO retail road within %.0f m: no drive built", station.MaxLink );
        status = text;
    } else {
        status = "no drive (connect false)";
    }

    if ( EnsureCanopyAssets() ) {
        SceneObject canopy;
        canopy.Id = stationId + "_canopy";
        canopy.Kind = "prop";
        canopy.Ref = kCanopyObject;
        canopy.Position = Vector3f( station.Position.x, centreY + kSlabClearance, station.Position.z );
        canopy.Yaw = station.Yaw;
        doc.Objects.push_back( canopy );
    } else {
        status += "; canopy assets could not be written";
    }
    if ( bGroundMissed ) {
        status += "; WARNING: the terrain is not streamed here, heights kept from pos (fly the camera over the site, then set_station)";
    }
    *doc.findStation( stationId ) = station;
    markSceneChanged();
    OTDU_LOG_INFO( "SceneEditor: station '%s' at (%.1f %.1f %.1f) yaw %.1f: %s\n", stationId.c_str(), station.Position.x, station.Position.y,
                   station.Position.z, station.Yaw, status.c_str() );
    return status;
}

void GMSceneEditor::exportStationsToGame() {
    if ( gpFile == nullptr ) {
        statusText = "no mods folder";
        return;
    }
    // 1. The roads file (the slabs and drives load in FREERIDE through the normal ModRoads route).
    exportRoadsFile();
    // 2. stations.txt: this scene's block replaces its previous block (idempotent re-export).
    const std::string tag = "# otdu2-scene-stations: " + ( doc.Name.empty() ? std::string( "scene" ) : doc.Name );
    const std::filesystem::path path = gpFile->getModsRoot() / "stations.txt";
    std::vector<std::string> kept;
    {
        std::ifstream in( path );
        std::string line;
        bool bSkip = false;
        while ( std::getline( in, line ) ) {
            if ( !line.empty() && line.back() == '\r' ) {
                line.pop_back();
            }
            if ( line == tag ) {
                bSkip = true;
                continue;
            }
            if ( bSkip && ( line.empty() || line[0] == '#' ) ) {
                bSkip = false;
            }
            if ( !bSkip ) {
                kept.push_back( line );
            }
        }
    }
    uint32_t written = 0;
    if ( !doc.Stations.empty() ) {
        kept.push_back( tag );
        for ( const SceneStation &station : doc.Stations ) {
            std::string name = station.Name.empty() ? station.Id : station.Name;
            std::replace( name.begin(), name.end(), ' ', '_' );
            char line[200];
            snprintf( line, sizeof( line ), "%s %s %.1f %.1f", island.c_str(), name.c_str(), station.Position.x, station.Position.z );
            kept.push_back( line );
            written++;
        }
        kept.push_back( "" );
    }
    std::string text;
    for ( const std::string &line : kept ) {
        text += line + "\n";
    }
    if ( !WriteTextFile( path, text ) ) {
        statusText = "cannot write stations.txt";
        return;
    }
    // 3. The canopy placements (props.txt): appended once per (name, position).
    uint32_t props = 0;
    const std::vector<ModProps::PlacementLine> existing = ModProps::listPlacements();
    for ( const SceneStation &station : doc.Stations ) {
        const SceneObject *pCanopy = doc.findObject( station.Id + "_canopy" );
        if ( pCanopy == nullptr ) {
            continue;
        }
        bool bHave = false;
        for ( const ModProps::PlacementLine &line : existing ) {
            if ( line.Name == pCanopy->Ref && std::fabs( line.Position.x - pCanopy->Position.x ) < 0.5f &&
                 std::fabs( line.Position.z - pCanopy->Position.z ) < 0.5f ) {
                bHave = true;
                break;
            }
        }
        if ( !bHave && ModProps::appendPlacement( island, pCanopy->Ref, pCanopy->Position, pCanopy->Yaw * kPI / 180.0f, 1.0f, false,
                                                  "TD_Gouraud" ) ) {
            props++;
        }
    }
    statusText = std::to_string( written ) + " station(s) -> stations.txt, " + std::to_string( doc.Roads.size() ) + " roads -> roads file, " +
                 std::to_string( props ) + " new canopy placement(s) -> props.txt (relaunch FREERIDE)";
    OTDU_LOG_INFO( "SceneEditor: %s\n", statusText.c_str() );
}
