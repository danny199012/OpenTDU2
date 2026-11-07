#pragma once

#include "game/cinematic/cinematic_car.h"

#include <string>

// The seaplane trip to the casino island and back (docs/re/casino_spec.md 10).
//
// The retail banks [DATA: scenarics/cinematics/casino/]:
//   spt_ibiza_departure / spt_hawai_departure  C_<ISLAND>_DEPARTURE: the car
//       drives up to the island's seaplane dock (PLAYER_CAR), the avatar boards;
//   spt_casino_arrival                          C_CASINO_ARRIVAL: the flight,
//       the landing at the casino island's dock, the walk to the resort;
//   spt_casino_departure                        C_CASINO_DEPARTURE: back to
//       the casino dock (Entity__CinCar d_car), the take-off;
//   spt_ibiza_arrival / spt_hawai_arrival       C_<ISLAND>_ARRIVAL: the landing
//       at the home dock, the car handed back (PLAYER_CAR).
// Each scene plays in the frame of its island's dock spot (spots.bin
// ELANDCAA on Ibiza / Hawai, category 0x0e; ELANDCA on the casino island)
// with the dock's exterior bank (islands/<island>/exterior/elandcaa.bnk,
// casino/exterior/elandca.bnk) [DATA]. The scene frame = the spot frame is
// [INF] (the retail placing code is encrypted), checked on the shots.
struct CasinoTrip {
    std::string OriginIsland = "ibiza"; // the island the player flew from
    CinematicCar::Desc Car;             // the car left at the dock
    bool bCarSet = false;
};
CasinoTrip &GetCasinoTrip();

namespace CasinoTripScenes {
// The dock spot of an island (ELANDCAA / ELANDCA): its spots.bin position.
bool FindDock( const std::string &island, float &outX, float &outZ );
// The casino building's door on the casino island: the spots.bin ECAS record's
// second point (+0x28) and heading (+0x38), the road-side entrance every spot
// record carries (the dealer / house exits use the same field) [DATA].
bool FindCasinoDoor( Vector3f &outPosition, float &outHeading );
// FREERIDE -> the departure at the origin dock -> the casino arrival -> CASINO.
// Fills GetCinViewerRequest(); false when the banks / the dock are missing
// (the caller then enters the casino directly).
bool RequestFlightToCasino( const std::string &originIsland, const CinematicCar::Desc &car );
// CASINO -> the casino departure -> the arrival at the origin dock -> FREERIDE
// (the car where the arrival scene leaves it).
bool RequestFlightHome();
} // namespace CasinoTripScenes
