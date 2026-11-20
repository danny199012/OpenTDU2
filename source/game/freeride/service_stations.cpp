#include "shared.h"
#include "service_stations.h"

#include "core/logger.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

void ServiceStations::load( const std::string &island ) {
    stations.clear();
    // The OIL stations the owner found on Ibiza (2026-11-15, the F6 overlay positions of the car stopped on
    // each forecourt, moved ~20 m towards the pumps): Area 1 in the north-west pines, and the two beside
    // the car wash roads in Area 2.
    struct BuiltIn {
        const char *Island, *Name;
        float X, Z;
        float LightX, LightZ; // canopy centre relative to the entry
    };
    static const BuiltIn kBuiltIn[] = {
        { "ibiza", "OIL Station (Area 1)", 18961.0f, 18097.0f, 3.0f, 22.0f },
        { "ibiza", "OIL Station (Car Wash)", 27002.0f, 11622.0f, -2.0f, -22.0f },
        { "ibiza", "OIL Station (East)", 27305.0f, 13962.0f, 17.0f, 0.0f },
        // Owner's screenshot 2026-11-20: car under the canopy, REPAIR shutters behind (F6 overlay X 20338.6, Z 20153.4).
        { "ibiza", "OIL Station (Area 2)", 20338.0f, 20153.0f, 10.5f, 14.0f },
        // Found by scanning every Ibiza sector for the SB_GazSB building (-logstations; centre of its Metal canopy item,
        // the pump position and canopy offset not measured: radius is 35 m, so the lot is covered).
        { "ibiza", "OIL Station (Wilds NW)", 6985.0f, 27068.0f, 0.0f, 0.0f },
        { "ibiza", "OIL Station (Hills W)", 8143.0f, 26283.0f, 0.0f, 0.0f },
        { "ibiza", "OIL Station (South Coast)", 15918.0f, 25647.0f, 0.0f, 0.0f },
        { "ibiza", "OIL Station (South Shore)", 16462.0f, 27583.0f, 0.0f, 0.0f },
        { "ibiza", "OIL Station (North Hills)", 17903.0f, 9605.0f, 0.0f, 0.0f },
        { "ibiza", "OIL Station (South West)", 18773.0f, 26650.0f, 0.0f, 0.0f },
        { "ibiza", "OIL Station (North)", 20407.0f, 5934.0f, 0.0f, 0.0f },
        { "ibiza", "OIL Station (North East)", 22670.0f, 8336.0f, 0.0f, 0.0f },
        { "ibiza", "OIL Station (East Coast)", 23281.0f, 19374.5f, 22.0f, -26.0f }, // the owner's F6 on the forecourt (2026-11-20); lights at the scanned canopy centre (23303, 19348)
    };
    for ( const BuiltIn &entry : kBuiltIn ) {
        if ( ToLower( entry.Island ) == ToLower( island ) ) {
            stations.push_back( { entry.Name, Vector3f( entry.X, 0.0f, entry.Z ), Vector3f( entry.LightX, 0.0f, entry.LightZ ) } );
            if ( std::string( entry.Name ) == "OIL Station (Area 2)" ) {
                // The dirt track that runs past the pad (traced from a top-down shot, north end first): its two ends are
                // the entrance and the exit.
                stations.back().DrivePath = { Vector3f( 20343.5f, 0.0f, 20228.0f ), Vector3f( 20343.3f, 0.0f, 20224.7f ), Vector3f( 20345.6f, 0.0f, 20207.3f ),
                                              Vector3f( 20344.4f, 0.0f, 20198.0f ), Vector3f( 20339.8f, 0.0f, 20189.9f ), Vector3f( 20338.5f, 0.0f, 20160.0f ),
                                              Vector3f( 20336.3f, 0.0f, 20131.7f ), Vector3f( 20338.1f, 0.0f, 20120.0f ) };
            }
        }
    }
    if ( gpFile == nullptr ) {
        return;
    }
    const std::filesystem::path path = gpFile->getModsRoot() / "stations.txt";
    std::ifstream file( path );
    if ( !file.is_open() ) {
        OTDU_LOG_INFO( "ServiceStations: %zu built-in station(s) on '%s'\n", stations.size(), island.c_str() );
        return; // no list: the normal case
    }
    std::string line;
    while ( std::getline( file, line ) ) {
        if ( line.empty() || line[0] == '#' ) {
            continue;
        }
        std::istringstream words( line );
        std::string lineIsland, name;
        float x = 0.0f, z = 0.0f;
        if ( !( words >> lineIsland >> name >> x >> z ) || ToLower( lineIsland ) != ToLower( island ) ) {
            continue;
        }
        stations.push_back( { name, Vector3f( x, 0.0f, z ) } );
    }
    OTDU_LOG_INFO( "ServiceStations: %zu station(s) on '%s' from %s\n", stations.size(), island.c_str(), path.string().c_str() );
}

const ServiceStations::Station *ServiceStations::findNear( const Vector3f &position, float maxDistance ) const {
    const Station *pBest = nullptr;
    float bestSquared = maxDistance * maxDistance;
    for ( const Station &station : stations ) {
        const float dx = station.Position.x - position.x, dz = station.Position.z - position.z;
        const float squared = dx * dx + dz * dz;
        if ( squared < bestSquared ) {
            bestSquared = squared;
            pBest = &station;
        }
    }
    return pBest;
}
