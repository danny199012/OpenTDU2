#include "shared.h"
#include "cruising_zones.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"

#include <cstring>
#include <fstream>

CruisingZones &GetCruisingZones() {
    static CruisingZones zones;
    return zones;
}

void CruisingZones::load( const std::string &islandName ) {
    island = islandName;
    polygons.clear();
    const std::string path = gpFile->getBankFolder() + "islands/" + island + "/level/cruisingzones.bin";
    std::vector<uint8_t> data;
    if ( !gpFile->fileExists( path.c_str() ) || !gpFile->loadFile( path.c_str(), data ) ) {
        return;
    }
    if ( data.size() < 48 || memcmp( data.data() + 12, "CRZODATA", 8 ) != 0 ) {
        OTDU_LOG_WARN( "CruisingZones: '%s' is not CRZODATA\n", path.c_str() );
        return;
    }
    auto readU32 = [&]( size_t offset ) {
        uint32_t v = 0;
        memcpy( &v, data.data() + offset, 4 );
        return v;
    };
    const uint32_t count = readU32( 32 );
    size_t offset = 48;
    for ( uint32_t zone = 0; zone < count && offset + 16 <= data.size(); zone++ ) {
        const uint32_t points = readU32( offset + 4 );
        offset += 16; // index, count, DEADBEEF x2
        if ( points > 4096 || offset + points * 8 + 8 > data.size() ) {
            break;
        }
        Polygon polygon;
        polygon.Index = static_cast<int32_t>( readU32( offset - 16 ) );
        for ( uint32_t i = 0; i < points; i++ ) {
            float x = 0.0f, z = 0.0f;
            memcpy( &x, data.data() + offset + i * 8, 4 );
            memcpy( &z, data.data() + offset + i * 8 + 4, 4 );
            polygon.X.push_back( x );
            polygon.Z.push_back( z );
        }
        offset += points * 8 + 8; // points, DEADBEEF x2
        polygons.push_back( std::move( polygon ) );
    }
    // Names: the DBCruisingZones rows of this island, in order.
    if ( gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        int32_t islandId = -1;
        for ( uint32_t i = 0; i < root.getArraySize( "DBIslandData" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBIslandData", i );
            if ( StringCmpI( row["_folder"].asString().c_str(), island.c_str() ) == 0 ) {
                islandId = static_cast<int32_t>( row["_island"].asInt( -1 ) );
            }
        }
        size_t next = 0;
        for ( uint32_t i = 0; i < root.getArraySize( "DBCruisingZones" ) && next < polygons.size(); i++ ) {
            XmbValue row = root.getArrayElement( "DBCruisingZones", i );
            if ( row["_island"].asInt( -2 ) != islandId ) {
                continue;
            }
            const char *pName = gpDatabase->getString( static_cast<int32_t>( row["_name_hc"].asUInt64() ) );
            polygons[next++].Name = pName != nullptr ? pName : "";
        }
    }
    OTDU_LOG_INFO( "CruisingZones: %s: %zu zones\n", island.c_str(), polygons.size() );
    for ( size_t i = 0; i < polygons.size(); i++ ) {
        OTDU_LOG_INFO( "CruisingZones:   %zu '%s' (%zu points)\n", i, polygons[i].Name.c_str(), polygons[i].X.size() );
    }
}

int32_t CruisingZones::find( float x, float z ) const {
    for ( size_t zone = 0; zone < polygons.size(); zone++ ) {
        const Polygon &p = polygons[zone];
        bool bInside = false;
        for ( size_t i = 0, j = p.X.size() - 1; i < p.X.size(); j = i++ ) {
            if ( ( p.Z[i] > z ) != ( p.Z[j] > z ) && x < ( p.X[j] - p.X[i] ) * ( z - p.Z[i] ) / ( p.Z[j] - p.Z[i] ) + p.X[i] ) {
                bInside = !bInside;
            }
        }
        if ( bInside ) {
            return static_cast<int32_t>( zone );
        }
    }
    return -1;
}

const std::string &CruisingZones::getName( int32_t zone ) const {
    static const std::string empty;
    return zone >= 0 && zone < getCount() ? polygons[zone].Name : empty;
}
