#include "shared.h"
#include "service_stations.h"

#include "core/logger.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"

#include <cmath>
#include <cstdio>

// The tarmac of the petrol stations: the generated texture and the drive from each forecourt to the nearest road
// (the shipped terrain has a bare dirt track there). Port-native [INF].

GPUTexture *GetStationAsphalt() {
    static GPUTexture *pAsphalt = nullptr;
    if ( pAsphalt == nullptr && gpRender != nullptr ) {
        // Generated tarmac: grey grain with soft darker patches (periodic, so it tiles).
        const uint32_t size = 256;
        std::vector<uint8_t> pixels( size * size * 4 );
        uint32_t seed = 12345u;
        for ( uint32_t y = 0; y < size; y++ ) {
            for ( uint32_t x = 0; x < size; x++ ) {
                seed = seed * 1664525u + 1013904223u;
                const float grain = ( ( seed >> 16 ) & 255 ) / 255.0f;
                const float patch = 0.5f + 0.25f * std::sin( x * 6.2831853f / 64.0f + y * 6.2831853f / 128.0f ) +
                                    0.25f * std::sin( y * 6.2831853f / 32.0f - x * 6.2831853f / 256.0f );
                const float v = 0.21f + 0.025f * patch + 0.05f * grain;
                uint8_t *px = &pixels[( y * size + x ) * 4];
                px[0] = static_cast<uint8_t>( v * 255.0f );
                px[1] = static_cast<uint8_t>( v * 255.0f * 1.01f );
                px[2] = static_cast<uint8_t>( v * 255.0f * 1.05f );
                px[3] = 255;
            }
        }
        pAsphalt = gpRender->getTextureCache().createFromRGBA( size, size, pixels.data(), "station_asphalt" );
    }
    return pAsphalt;
}

void ServiceStations::unloadDrives() {
    for ( Station &station : stations ) {
        UnloadObjDraws( station.Drive );
        station.bDriveTried = false;
    }
}

void ServiceStations::tickDrives( EdenRenderer &renderer, const Vector3f &carPosition,
                                  const std::function<bool( float, float, float, float & )> &heightFn,
                                  const std::function<bool( const Vector3f &, Vector3f & )> &roadFn ) {
    static const bool bNoDrives = CmdLine::HasArg( "-nostationdrive" );
    if ( bNoDrives ) {
        return;
    }
    for ( Station &station : stations ) {
        const Vector3f centre = station.Position + station.LightOffset;
        const float dx = centre.x - carPosition.x, dz = centre.z - carPosition.z;
        if ( dx * dx + dz * dz >= 250.0f * 250.0f ) {
            if ( !station.Drive.empty() || station.bDriveTried ) {
                UnloadObjDraws( station.Drive );
                station.bDriveTried = false;
            }
            continue;
        }
        if ( station.bDriveTried ) {
            continue;
        }
        station.bDriveTried = true;
        // The path: the traced dirt track when the station has one, else (opt-in, -stationdrivegeneric) a straight connector
        // to the nearest map road.
        std::vector<Vector3f> path = station.DrivePath;
        if ( path.empty() ) {
            static const bool bGeneric = CmdLine::HasArg( "-stationdrivegeneric" );
            Vector3f road;
            if ( !bGeneric || !roadFn( centre, road ) ) {
                continue;
            }
            const float full = std::sqrt( ( road.x - centre.x ) * ( road.x - centre.x ) + ( road.z - centre.z ) * ( road.z - centre.z ) );
            if ( full < 9.0f ) {
                continue;
            }
            const float k = ( full - 5.0f ) / full;
            path = { Vector3f( centre.x, 0.0f, centre.z ), Vector3f( centre.x + ( road.x - centre.x ) * k, 0.0f, centre.z + ( road.z - centre.z ) * k ) };
        }
        // Resample every <= 3 m.
        std::vector<Vector3f> pts;
        for ( size_t i = 0; i + 1 < path.size(); i++ ) {
            const Vector3f d = path[i + 1] - path[i];
            const float len = std::sqrt( d.x * d.x + d.z * d.z );
            const int32_t n = Max( 1, static_cast<int32_t>( std::ceil( len / 3.0f ) ) );
            for ( int32_t j = 0; j < n; j++ ) {
                pts.push_back( path[i] + d * ( static_cast<float>( j ) / n ) );
            }
        }
        pts.push_back( path.back() );
        if ( pts.size() < 2 ) {
            continue;
        }
        const float halfWidth = 3.6f;
        const Vector3f origin = pts[0];
        // OBJ in the strip's own frame, origin at the first point. The loader mirrors X (3DG convention): x is negated.
        std::string obj;
        char line[200];
        float lastHeight = carPosition.y;
        float originHeight = 0.0f;
        float along = 0.0f;
        bool bOk = true;
        const int32_t segments = static_cast<int32_t>( pts.size() ) - 1;
        for ( size_t i = 0; i < pts.size() && bOk; i++ ) {
            const Vector3f &prev = pts[i > 0 ? i - 1 : 0], &next = pts[i + 1 < pts.size() ? i + 1 : i];
            Vector3f tangent( next.x - prev.x, 0.0f, next.z - prev.z );
            const float tl = std::sqrt( tangent.x * tangent.x + tangent.z * tangent.z );
            tangent = tangent * ( 1.0f / Max( tl, 0.001f ) );
            const Vector3f side( tangent.z, 0.0f, -tangent.x );
            if ( i > 0 ) {
                const float sx = pts[i].x - pts[i - 1].x, sz = pts[i].z - pts[i - 1].z;
                along += std::sqrt( sx * sx + sz * sz );
            }
            for ( int32_t k = 0; k < 2; k++ ) {
                const float sign = k == 0 ? -1.0f : 1.0f;
                const float wx = pts[i].x + side.x * halfWidth * sign;
                const float wz = pts[i].z + side.z * halfWidth * sign;
                float h = 0.0f;
                if ( !heightFn( wx, wz, lastHeight + 2.0f, h ) ) {
                    bOk = false;
                    break;
                }
                lastHeight = h;
                if ( i == 0 && k == 0 ) {
                    originHeight = h;
                }
                snprintf( line, sizeof( line ), "v %.3f %.3f %.3f\nvn 0 1 0\nvt %.3f %.3f\n", -( wx - origin.x ),
                          h + 0.07f - originHeight, wz - origin.z, k == 0 ? 0.0f : 1.0f, along / 4.0f );
                obj += line;
            }
        }
        if ( !bOk ) {
            station.bDriveTried = false; // the terrain is not resident yet: try again next tick
            continue;
        }
        obj += "usemtl tarmac\n";
        for ( int32_t i = 0; i < segments; i++ ) {
            const int32_t a = i * 2 + 1, b = a + 1, c = a + 2, d = a + 3;
            snprintf( line, sizeof( line ), "f %d/%d/%d %d/%d/%d %d/%d/%d\nf %d/%d/%d %d/%d/%d %d/%d/%d\n", a, a, a, c, c, c, d, d, d,
                      a, a, a, d, d, d, b, b, b );
            obj += line;
        }
        const std::string mtl = "newmtl tarmac\nKd 1 1 1\nmap_Kd station_asphalt.png\n";
        if ( !BuildObjDraws( renderer, "", "stationdrive", "TD_Gouraud", obj, mtl, station.Drive, nullptr, nullptr, nullptr,
                             "StationDrive" ) ) {
            continue;
        }
        if ( GPUTexture *pTexture = GetStationAsphalt() ) {
            for ( ObjMeshDraw &draw : station.Drive ) {
                renderer.assignTexture( draw.Material, "COLOR", pTexture );
                renderer.updateProgram( draw.Material );
                draw.pTexture = nullptr; // shared with the texture cache: not ours to destroy
            }
        }
        station.DriveOrigin = Vector3f( origin.x, originHeight, origin.z );
        OTDU_LOG_INFO( "StationDrive: %s: %d segments from (%.0f, %.0f)\n", station.Name.c_str(), segments, origin.x, origin.z );
    }
}

void ServiceStations::drawDrives( EdenRenderer &renderer ) const {
    for ( const Station &station : stations ) {
        if ( station.Drive.empty() ) {
            continue;
        }
        const Matrix4x4f world = Matrix4x4f::Translation( station.DriveOrigin );
        for ( const ObjMeshDraw &draw : station.Drive ) {
            renderer.draw( draw.Mesh, draw.Material, world );
        }
    }
}
