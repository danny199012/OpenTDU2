#include "shared.h"
#include "casino_trip.h"

#include "filesystem/gs_file.h"
#include "game/cinematic/gm_cin_viewer.h"
#include "system/cmd_line.h"

#include <cstring>
#include <vector>

CasinoTrip &GetCasinoTrip() {
    static CasinoTrip trip;
    return trip;
}

namespace {
constexpr const char *kTripFolder = "scenarics/cinematics/casino/";

std::string DockPrefix( const std::string &island ) {
    return island == "casino" ? "elandca" : "elandcaa";
}

std::string Upper( std::string text ) {
    for ( char &c : text ) {
        if ( c >= 'a' && c <= 'z' ) {
            c = static_cast<char>( c - 'a' + 'A' );
        }
    }
    return text;
}

bool BankExists( const std::string &bank ) {
    return gpFile != nullptr && gpFile->fileExists( ( gpFile->getBankFolder() + bank ).c_str() );
}

// One trip scene: the bank spt_<island>_<leg>.bnk, its C_<ISLAND>_<LEG>
// timeline, in the frame of the island's dock with the dock exterior.
bool FillScene( CinViewerRequest &request, const std::string &island, const char *pLeg,
                const CinematicCar::Desc *pCar ) {
    const std::string bank = std::string( kTripFolder ) + "spt_" + island + "_" + pLeg + ".bnk";
    float x = 0.0f, z = 0.0f;
    if ( !BankExists( bank ) || !CasinoTripScenes::FindDock( island, x, z ) ) {
        OTDU_LOG_WARN( "CasinoTrip: '%s' or the %s dock is missing\n", bank.c_str(), island.c_str() );
        return false;
    }
    request = CinViewerRequest();
    request.Bank = bank;
    request.Cinematic = "C_" + Upper( island ) + "_" + Upper( pLeg );
    request.bFromStory = true; // no -cin* overrides, no tool layer
    request.bStory = false;
    request.PickX = x;
    request.PickZ = z;
    request.Island = island;
    request.ExteriorPrefix = DockPrefix( island );
    if ( pCar != nullptr ) {
        // The player's own car drives up to / away from the dock: the .sce
        // actor PLAYER_CAR of the island legs; the casino departure animates
        // its Entity__CinCar (d_car) [DATA].
        request.bPlayerCar = true;
        request.Car = *pCar;
        request.CarActor = island == "casino" ? "" : "PLAYER_CAR";
    }
    return true;
}
} // namespace

bool CasinoTripScenes::FindDock( const std::string &island, float &outX, float &outZ ) {
    // islands/<island>/level/spots.bin: 0x110-byte records from 0x20, +8 the
    // bank name (8 chars), +0x10 the position [DATA, map_screen.cpp].
    std::vector<uint8_t> data;
    if ( gpFile == nullptr ||
         !gpFile->loadFile( ( gpFile->getBankFolder() + "islands/" + island + "/level/spots.bin" ).c_str(), data ) ||
         data.size() < 0x20 ) {
        return false;
    }
    const std::string prefix = Upper( DockPrefix( island ) );
    for ( size_t offset = 0x20; offset + 0x110 <= data.size(); offset += 0x110 ) {
        char name[9] = {};
        memcpy( name, data.data() + offset + 8, 8 );
        if ( prefix == name ) {
            float position[3];
            memcpy( position, data.data() + offset + 0x10, sizeof( position ) );
            outX = position[0];
            outZ = position[2];
            return true;
        }
    }
    return false;
}

bool CasinoTripScenes::FindCasinoDoor( Vector3f &outPosition, float &outHeading ) {
    std::vector<uint8_t> data;
    if ( gpFile == nullptr ||
         !gpFile->loadFile( ( gpFile->getBankFolder() + "islands/casino/level/spots.bin" ).c_str(), data ) ||
         data.size() < 0x20 ) {
        return false;
    }
    for ( size_t offset = 0x20; offset + 0x110 <= data.size(); offset += 0x110 ) {
        char name[9] = {};
        memcpy( name, data.data() + offset + 8, 8 );
        if ( strcmp( name, "ECAS" ) == 0 ) {
            memcpy( &outPosition, data.data() + offset + 0x28, 12 );
            memcpy( &outHeading, data.data() + offset + 0x38, 4 );
            return outPosition.x != 0.0f || outPosition.z != 0.0f;
        }
    }
    return false;
}

bool CasinoTripScenes::RequestFlightToCasino( const std::string &originIsland, const CinematicCar::Desc &car ) {
    if ( CmdLine::HasArg( "-nocasinotrip" ) ) {
        return false;
    }
    const std::string origin = originIsland == "hawai" ? "hawai" : "ibiza";
    CinViewerRequest departure;
    CinViewerRequest arrival;
    if ( !FillScene( departure, origin, "departure", &car ) || !FillScene( arrival, "casino", "arrival", nullptr ) ) {
        return false;
    }
    arrival.Next = eGameMode::GM_Casino;
    departure.Next = eGameMode::GM_Casino; // the chain decides; kept for a lone scene
    departure.After.push_back( arrival );
    CasinoTrip &trip = GetCasinoTrip();
    trip.OriginIsland = origin;
    trip.Car = car;
    trip.bCarSet = true;
    GetCinViewerRequest() = departure;
    OTDU_LOG_INFO( "CasinoTrip: %s -> casino (%s, then %s)\n", origin.c_str(), departure.Cinematic.c_str(),
                   arrival.Cinematic.c_str() );
    return true;
}

bool CasinoTripScenes::RequestFlightHome() {
    if ( CmdLine::HasArg( "-nocasinotrip" ) ) {
        return false;
    }
    CasinoTrip &trip = GetCasinoTrip();
    const CinematicCar::Desc car = trip.bCarSet ? trip.Car : CinematicCar::Desc();
    CinViewerRequest departure;
    CinViewerRequest arrival;
    if ( !FillScene( departure, "casino", "departure", &car ) ||
         !FillScene( arrival, trip.OriginIsland, "arrival", &car ) ) {
        return false;
    }
    arrival.Next = eGameMode::GM_FreeRide; // the drive resumes where the scene leaves the car
    departure.Next = eGameMode::GM_FreeRide;
    departure.After.push_back( arrival );
    GetCinViewerRequest() = departure;
    OTDU_LOG_INFO( "CasinoTrip: casino -> %s (%s, then %s)\n", trip.OriginIsland.c_str(), departure.Cinematic.c_str(),
                   arrival.Cinematic.c_str() );
    return true;
}
