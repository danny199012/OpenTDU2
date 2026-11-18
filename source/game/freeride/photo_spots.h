#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

// The photograph challenges (the Photographer's viewpoints): euro/bnk/scenarics/photospot.xml holds the
// 20 viewpoints of each island [DATA], DBPhotograph (same _hashcode) the name, the prize and the weather
// / car conditions [DATA]. A photo counts when it is taken from near the viewpoint, aimed at the subject,
// inside the hour window [INF: the retail angle / zoom tolerances (tol_angle_*, tol_zoom_*) use an
// angle convention that is not decoded].
struct PhotoSpot {
    uint32_t Hash = 0;
    int32_t Island = 0; // 0 ibiza, 1 hawai
    Vector3f Viewpoint; // pos_vp
    Vector3f Subject;   // pos_spot
    Vector3f CarPos;    // car_pos
    float StartHour = 0.0f, EndHour = 24.0f;
    // DBPhotograph
    std::string Name;
    int32_t Money = 0;
    int32_t PlayerCar = 0; // _id_car the photo needs (0: any)
    int32_t Weather = 0;
};

class PhotoSpots {
public:
    void load(); // photospot.xml + DBPhotograph
    inline const std::vector<PhotoSpot> &getSpots() const { return spots; }
    // The island index of an island name ("ibiza" 0, "hawai" 1, other -1).
    static int32_t IslandIndex( const std::string &island );
    int32_t countOnIsland( int32_t island ) const;
    // The photo spot a camera at `position` looking along `forward` (unit) satisfies, or -1: within
    // `maxDistance` of the viewpoint, the subject within `maxAngleDeg` of the view direction.
    int32_t match( int32_t island, const Vector3f &position, const Vector3f &forward, float hour, float maxDistance,
                   float maxAngleDeg ) const;

private:
    std::vector<PhotoSpot> spots;
};
