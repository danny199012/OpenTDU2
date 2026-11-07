#pragma once

#include <string>
#include <vector>

#include "core/math.h"
#include "game/cinematic/cinematic_car.h"

// The car washes of an island (docs/re/car_wash_spec.md): the ECW_* records of
// spots.bin (3 on Ibiza, 5 on Hawai, none in the casino), whose exterior bank
// islands/<island>/exterior/ecw_a.bnk carries the forecourt AND the retail
// washing cutscene (C_WASHING, 16.4 s, three cameras, the three babes and the
// player's car - GMCinViewer plays it, verified session 25). Validate at one:
// the fee, the scene, and FREERIDE again under the StoryHandoff (the car stays
// where the scene left it). There is no pump-style screen (unlike fuel
// 1635..1645): the cutscene IS the screen. Dirt is not rendered by the port, so
// nothing is cleaned yet - the wash is the scene + the fee + the flag.
struct CarWashSpot {
    std::string Bank;    // the spots.bin record's bank, "ECW_A"
    Vector3f Position;   // the building origin (the record's position)
    float Heading = 0.0f; // radians, the frame is T(position) R_y(heading)
};

class CarWash {
public:
    // Parses the island's spots.bin and keeps the ECW_* records (false: none).
    bool load( const std::string &island );
    inline bool isLoaded() const { return !spots.empty(); }
    inline const std::vector<CarWashSpot> &getSpots() const { return spots; }

    // The nearest wash within radius (X/Z metres), or nullptr. outDistance
    // receives the distance (untouched when nullptr is returned).
    const CarWashSpot *findNear( const Vector3f &position, float radius, float &outDistance ) const;

    // Fills the CINVIEWER request for the washing scene at this wash: the spot
    // frame (the pick position), the island's exterior bank and the player's
    // car; FREERIDE next, and bFromStory so the viewer writes the StoryHandoff
    // from the car's end pose and ignores the -cin* overrides.
    static void fillRequest( const CarWashSpot &spot, const std::string &island, const CinematicCar::Desc &carDesc );

    // The wash fee [INF: the retail fee is not in the readable data - no pump
    // screen, no price row; $10 like the port's own fuel prices are chosen].
    static constexpr int32_t kPrice = 10;

private:
    std::vector<CarWashSpot> spots;
};
