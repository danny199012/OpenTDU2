#pragma once

#include "game/cinematic/cinematic_car.h"
#include "core/math.h"

#include <string>
#include <vector>

class EdenRenderer;
class EdenWorld;

// The island's aeroplanes (docs/re/aircraft_birds_spec.md).
//
// [DATA] islands/<island>/scenarics/packs/world_aircrafts.bnk, entities.xmb:
//   - ExportType_Volume / ArrayOf_Entity__Volume: the path nodes, one
//     orient_pos_scale each ("Path1RunWayStart", "T1", "A4" ...).
//   - ExportType_Entity / ArrayOf_Entity__AircraftPath: the flight loops, each
//     a list of PathPoints { positionVolume (__Hc of a volume), Type }, an
//     AircraftType, Min/MaxDistBetween2Aircraft, SpeedOnGround / SpeedOnAir and
//     the two engine sounds (boeing_747_idle / _in_flight.wav, same bank).
//   Ibiza has 4: Path01 / Path_04 loop a runway (their first two points sit on
//   the ground, ~20 m and ~60 m), Path02 / Path03 are 350 m high circuits
//   across the whole island.
// [DATA] islands/<island>/scenarics/packs/aircrafts.bnk, entities.xmb:
//   ExportType_Entity / ArrayOf_Entity__Actor__Aircraft: the aircraft of each
//   AircraftType. The record names its model with ObjectInCommonRef, an 8
//   character name packed in a u64 - "FU_AIR_B" / "FU_AIR_C" - i.e. an object
//   of the shared library scene (commonworld.bnk library.3dd, drawn through the
//   .pmi V / O path). The bank carries no geometry of its own.
//
// The port flies the loops: a plane wraps around its route at SpeedOnGround
// while the segment joins two ground points and at SpeedOnAir otherwise, so a
// route reads as a landing (the last air point descends onto the runway), a
// ground roll down the runway and a take-off (the runway climbs back into the
// air). Several planes of the same route are spaced Min..MaxDist apart, as the
// data says. The model is shared by every plane of a type: drawing it is
// EdenWorld::drawLibraryNode, so nothing is uploaded per plane.
// Not done: the engine sounds, the .cin cinematics of the airport spot
// (eair_a.bnk C_LANDED / C_TAKE_OFF / C_FIRST_TAKE_OFF), landing gear / flaps.
class Aircraft {
public:
    using SurfaceFunction = CinematicCar::SurfaceFunction;

    Aircraft();
    ~Aircraft();

    // Loads the two banks of the island and resolves the models in the
    // island's common library. surface() is used once per path point, to know
    // which points sit on the ground (a point is a ground point when its
    // altitude is within 60 m of the terrain below it).
    bool load( const std::string &island, EdenWorld &world, const SurfaceFunction &surface );
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    void tick( float deltaTime );
    // cameraPosition: planes farther than getViewRadius() are skipped.
    void draw( EdenRenderer &renderer, EdenWorld &world, const Vector3f &cameraPosition ) const;

    // Radius (m) planes are drawn inside. The high circuits are 350 m up and
    // visible from the whole island, so this is far beyond the traffic's.
    static constexpr float kViewRadius = 9000.0f;

    inline int32_t getNumPlanes() const { return static_cast<int32_t>( planes.size() ); }
    inline int32_t getNumRoutes() const { return static_cast<int32_t>( routes.size() ); }
    // Planes whose current segment is a ground one (taxi / runway roll).
    int32_t getNumOnGround() const;
    // The take-offs and landings since the last call (a ground segment entered
    // / left): the headless proof that the routes really land and take off.
    int32_t takeTakeoffs();
    // -logaircraft: the routes, the models and the plane states.
    void logStats( float deltaTime );
    // -aircraftpark (debug): the first planes are pinned in front of the player
    // (45 m and 80 m, side by side), standing, so a screenshot shows the
    // aircraft model without hunting for one. The game mode feeds the pose
    // every frame, like Traffic::setParkForward.
    void setParkPose( const Vector3f &position, const Vector3f &forward );

private:
    // One Entity__AircraftPath: a closed loop resampled from its points.
    struct Route {
        std::string Name;
        int32_t TypeId = 0;
        float GroundSpeed = 15.0f;  // m/s (SpeedOnGround)
        float AirSpeed = 80.0f;     // m/s (SpeedOnAir)
        float MinSpacing = 3000.0f; // m (MinDistBetween2Aircraft)
        float MaxSpacing = 5000.0f;
        std::vector<Vector3f> Points;  // the resampled loop
        std::vector<float> Distances;  // cumulative, Distances.back() = Length
        std::vector<uint8_t> Ground;   // per segment Points[i] -> Points[i + 1]
        float Length = 0.0f;
        int32_t RunwayStart = -1; // the middle of the first ground segment
        int32_t NumGroundPoints = 0;
        std::vector<float> SpawnDistances; // the planes of this route
    };
    // One aircraft of a type (Entity__Actor__Aircraft).
    struct Model {
        int32_t TypeId = -1;
        uint64_t Hashcode = 0; // ObjectInCommonRef
        int32_t Node = -1;     // library node of the island (EdenWorld)
        Vector3f BoundsMin, BoundsMax;
        Vector3f Centre;      // bounds centre: the plane turns about it
        float Length = 0.0f;  // nose to tail (m)
    };
    struct Plane {
        int32_t Route = 0;
        int32_t ModelType = 0;
        float Distance = 0.0f; // m along the loop
        float Speed = 0.0f;
        float Heading = 0.0f;  // rad
        float Pitch = 0.0f;    // rad, + nose up
        float Bank = 0.0f;     // rad, + right wing down
        Vector3f Position;
        bool bGround = false;
        int32_t Segment = 0;
    };

    void buildRoute( Route &route, const std::vector<Vector3f> &points,
                     const std::vector<int32_t> &pointTypes, const SurfaceFunction &surface,
                     bool bLog ) const;
    // Position / heading / segment at a distance along the loop.
    void sampleRoute( const Route &route, float distance, Vector3f &outPosition, float &outHeading,
                      int32_t &outSegment ) const;
    void updatePlane( Plane &plane, float deltaTime );
    void placePlane( Plane &plane );

    bool bLoaded = false;
    std::string islandName;
    std::vector<Route> routes;
    std::vector<Model> models;
    std::vector<Plane> planes;
    int32_t takeoffCount = 0;
    // -aircraftpark: every plane frozen on the middle of its route's runway
    // segment (a deterministic screenshot).
    bool bPark = false;
    Vector3f parkPosition; // the player car (setParkPose)
    Vector3f parkForward;
    bool bHasParkPose = false;
    // Log throttles.
    float logTimer = 0.0f;
    int32_t loggedPlanes = -1;
    int32_t loggedGround = -1;
};
