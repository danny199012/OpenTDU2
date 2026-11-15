#include "shared.h"
#include "service_stations.h"

#include "core/logger.h"
#include "filesystem/gs_file.h"

#include <cstdio>
#include <fstream>
#include <sstream>

void ServiceStations::load( const std::string &island ) {
    stations.clear();
    if ( gpFile == nullptr ) {
        return;
    }
    const std::filesystem::path path = gpFile->getModsRoot() / "stations.txt";
    std::ifstream file( path );
    if ( !file.is_open() ) {
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
