#include "shared.h"
#include "car_wash.h"

#include "game/cinematic/cinematic_car.h"
#include "game/cinematic/gm_cin_viewer.h"
#include "game/spots/spot_exteriors.h"
#include "system/cmd_line.h"

bool CarWash::load( const std::string &island ) {
    spots.clear();
    for ( const SpotRecord &record : LoadSpotList( island ) ) {
        if ( record.Bank.size() >= 3 && _strnicmp( record.Bank.c_str(), "ECW", 3 ) == 0 ) {
            CarWashSpot spot;
            spot.Bank = record.Bank;
            spot.Position = record.Position;
            spot.Heading = record.Heading;
            spots.push_back( spot );
        }
    }
    if ( !spots.empty() ) {
        OTDU_LOG_INFO( "CarWash: %zu wash(es) on '%s', first '%s' at (%.1f, %.1f)\n", spots.size(), island.c_str(),
                       spots.front().Bank.c_str(), spots.front().Position.x, spots.front().Position.z );
    }
    return !spots.empty();
}

const CarWashSpot *CarWash::findNear( const Vector3f &position, float radius, float &outDistance ) const {
    const CarWashSpot *pBest = nullptr;
    float best = radius;
    for ( const CarWashSpot &spot : spots ) {
        const float dx = spot.Position.x - position.x;
        const float dz = spot.Position.z - position.z;
        const float distance = std::sqrt( dx * dx + dz * dz );
        if ( distance < best ) {
            best = distance;
            pBest = &spot;
        }
    }
    if ( pBest != nullptr ) {
        outDistance = best;
    }
    return pBest;
}

void CarWash::fillRequest( const CarWashSpot &spot, const std::string &island, const CinematicCar::Desc &carDesc ) {
    CinViewerRequest &request = GetCinViewerRequest();
    request = CinViewerRequest();
    request.Bank = "islands/" + island + "/exterior/ecw_a.bnk";
    // The retail beat (the owner's reference video, car_wash_spec 10): the car
    // drives in to the centre of the forecourt (C_INTRO_E_1: PLAYER_CAR from
    // (24.2, 0, 0.8) to the wash place (1.47, 0.13, -17.51), the C_WASHING
    // rest pose) [DATA], the babes put the radio down and wash (C_WASHING),
    // then walk off (C_OUTRO). The ENTITY names, not the .cin file names (the
    // spec's trap). C_INCAR (20 s, NO_CAMERA: the free in-car view of the
    // wash) and the C_BABE_0x booth loops are not played [not ported].
    request.Cinematic = CmdLine::HasArg( "-washonly" ) ? "C_WASHING" : "C_INTRO_E_1";
    if ( !CmdLine::HasArg( "-washonly" ) ) {
        // The fee is asked after the drive-in, over the loop shot (C_LOOP:
        // 3 shots of the car at the wash place) [the owner + DATA].
        request.PromptAfter = "C_INTRO_E_1";
        request.PromptLoop = "C_LOOP";
        request.Then = { "C_WASHING", "C_INCAR", "C_OUTRO" };
        request.InCar = "C_INCAR";
        request.InCarBackgrounds = { "C_BABE_01", "C_BABE_02", "C_BABE_03" };
    } else {
        request.Then.clear();
    }
    request.Price = kPrice;
    request.Next = eGameMode::GM_FreeRide;
    request.bFromStory = true; // the StoryHandoff + no -cin* override
    request.bStory = false;    // the plain drive comes back (no story goal)
    request.PickX = spot.Position.x;
    request.PickZ = spot.Position.z;
    request.Island = island;
    request.ExteriorPrefix = "ecw_a";
    request.bPlayerCar = true;
    request.Car = carDesc;
    request.CarActor = "PLAYER_CAR"; // the .sce actor animating the car (no Entity__CinCar in this bank)
    OTDU_LOG_INFO( "CarWash: '%s' on '%s' at (%.1f, %.1f) hdg %.2f -> %s\n", spot.Bank.c_str(), island.c_str(),
                   spot.Position.x, spot.Position.z, spot.Heading, request.Cinematic.c_str() );
}
