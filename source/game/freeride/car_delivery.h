#pragma once

#include "core/math.h"

#include <cstdint>
#include <string>
#include <vector>

// Where a bought car is delivered: the shop's own car position.
//
// The retail parks the cars of a shop in the exterior bank's `Entity__CarPos`
// entities (`islands/<island>/exterior/<bank>.bnk`): every entry carries
// `__Name`, `orient_pos_scale` (the placement in the bank's frame),
// `PlayerIndex` (the player slot, -1 for the shared ones) and `CarPosType`
// [DATA]. ECD_FERR's parking entry is named `PosCarParking`.
//
// The exterior banks are placed at the spot frame (`T(spot.Position) *
// R_y(spot.Heading)`, the spot exteriors' rule, spot_exteriors.cpp), so a local
// placement is transformed by it. The port never read these entities before:
// the dealer purchase parks the bought car on one, which is what the retail
// does (the cashier hands the keys over and the car is outside).
struct CarDeliveryPose {
    Vector3f Position;
    float Heading = 0.0f; // radians about +y
    int32_t Type = 0;     // Entity__CarPos.CarPosType
    int32_t PlayerIndex = -1;
    std::string Name;
};

// The car positions of a shop's exterior bank, in the island frame.
// `spotName` is the DBCarShop `_spot_name` ("ECD_FERR_3337I", the spots.bin
// record id) or the spot bank name ("ECD_FERR").
std::vector<CarDeliveryPose> LoadShopCarPositions( const std::string &island, const std::string &spotName );

// The pose a bought car is parked at: the player's own slot when there is one
// (`PlayerIndex >= 0`), the entry named `*Parking*` next, the first one last.
// False when the shop has no car position at all (the caller keeps the spot's
// own frame).
bool FindCarDeliveryPose( const std::string &island, const std::string &spotName, CarDeliveryPose &outPose );
