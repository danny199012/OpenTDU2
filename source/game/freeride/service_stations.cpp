#include "shared.h"
#include "service_stations.h"

#include "core/logger.h"
#include "filesystem/gs_file.h"

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
    };
    static const BuiltIn kBuiltIn[] = {
        { "ibiza", "OIL Station (Area 1)", 18961.0f, 18097.0f },
        { "ibiza", "OIL Station (Car Wash)", 27002.0f, 11622.0f },
        { "ibiza", "OIL Station (East)", 27305.0f, 13962.0f },
    };
    for ( const BuiltIn &entry : kBuiltIn ) {
        if ( ToLower( entry.Island ) == ToLower( island ) ) {
            stations.push_back( { entry.Name, Vector3f( entry.X, 0.0f, entry.Z ) } );
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
