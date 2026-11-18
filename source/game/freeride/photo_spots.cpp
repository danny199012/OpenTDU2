#include "shared.h"
#include "photo_spots.h"

#include "database/gs_database.h"
#include "filesystem/gs_file.h"

namespace {
// The value of name="..." inside one tag's text, or `fallback`.
float Attr( const std::string &tag, const char *pName, float fallback = 0.0f ) {
    const std::string key = std::string( " " ) + pName + "=\"";
    const size_t at = tag.find( key );
    return at == std::string::npos ? fallback : static_cast<float>( atof( tag.c_str() + at + key.size() ) );
}
} // namespace

int32_t PhotoSpots::IslandIndex( const std::string &island ) {
    return island == "ibiza" ? 0 : island == "hawai" ? 1 : -1;
}

int32_t PhotoSpots::countOnIsland( int32_t island ) const {
    int32_t count = 0;
    for ( const PhotoSpot &spot : spots ) {
        count += spot.Island == island ? 1 : 0;
    }
    return count;
}

void PhotoSpots::load() {
    spots.clear();
    if ( gpFile == nullptr ) {
        return;
    }
    std::vector<uint8_t> data;
    const std::string path = gpFile->getBankFolder() + "scenarics/photospot.xml";
    if ( !gpFile->fileExists( path.c_str() ) || !gpFile->loadFile( path.c_str(), data ) ) {
        return;
    }
    const std::string text( reinterpret_cast<const char *>( data.data() ), data.size() );
    for ( size_t at = text.find( "<VIEWPOINT " ); at != std::string::npos; at = text.find( "<VIEWPOINT ", at + 1 ) ) {
        const std::string tag = text.substr( at, text.find( "/>", at ) - at );
        PhotoSpot spot;
        spot.Hash = static_cast<uint32_t>( Attr( tag, "hashcode" ) );
        spot.Island = static_cast<int32_t>( Attr( tag, "island" ) );
        spot.Viewpoint = Vector3f( Attr( tag, "pos_vp_x" ), Attr( tag, "pos_vp_y" ), Attr( tag, "pos_vp_z" ) );
        spot.Subject = Vector3f( Attr( tag, "pos_spot_x" ), Attr( tag, "pos_spot_y" ), Attr( tag, "pos_spot_z" ) );
        spot.CarPos = Vector3f( Attr( tag, "car_pos_x" ), Attr( tag, "car_pos_y" ), Attr( tag, "car_pos_z" ) );
        spot.StartHour = Attr( tag, "start_hour_h" ) + Attr( tag, "start_hour_m" ) / 60.0f;
        spot.EndHour = Attr( tag, "end_hour_h" ) + Attr( tag, "end_hour_m" ) / 60.0f;
        spots.push_back( spot );
    }
    // DBPhotograph: the name, prize, car and weather by _hashcode.
    if ( gpDatabase != nullptr ) {
        const XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBPhotograph" ); i++ ) {
            const XmbValue row = root.getArrayElement( "DBPhotograph", i );
            const uint32_t hash = static_cast<uint32_t>( row["_hashcode"].asInt() );
            for ( PhotoSpot &spot : spots ) {
                if ( spot.Hash == hash ) {
                    spot.Name = LocStr( static_cast<int32_t>( row["_name_hc"].asInt() ), "" );
                    spot.Money = static_cast<int32_t>( row["_money"].asInt() );
                    spot.PlayerCar = static_cast<int32_t>( row["_playercar"].asInt() );
                    spot.Weather = static_cast<int32_t>( row["_weather"].asInt() );
                }
            }
        }
    }
    OTDU_LOG_INFO( "PhotoSpots: %zu viewpoints (ibiza %d, hawai %d)\n", spots.size(), countOnIsland( 0 ), countOnIsland( 1 ) );
}

int32_t PhotoSpots::match( int32_t island, const Vector3f &position, const Vector3f &forward, float hour,
                           float maxDistance, float maxAngleDeg ) const {
    int32_t best = -1;
    float bestAngle = maxAngleDeg;
    for ( size_t i = 0; i < spots.size(); i++ ) {
        const PhotoSpot &spot = spots[i];
        if ( spot.Island != island ) {
            continue;
        }
        const bool bInHours = spot.StartHour <= spot.EndHour ? ( hour >= spot.StartHour && hour <= spot.EndHour )
                                                             : ( hour >= spot.StartHour || hour <= spot.EndHour );
        const Vector3f toViewpoint = spot.Viewpoint - position;
        if ( !bInHours || toViewpoint.length() > maxDistance ) {
            continue;
        }
        Vector3f toSubject = spot.Subject - position;
        const float length = toSubject.length();
        if ( length < 0.01f ) {
            continue;
        }
        const float cosine = Clamp( toSubject.dot( forward ) / length, -1.0f, 1.0f );
        const float angle = acosf( cosine ) * 180.0f / 3.14159265f;
        if ( angle < bestAngle ) {
            bestAngle = angle;
            best = static_cast<int32_t>( i );
        }
    }
    return best;
}
