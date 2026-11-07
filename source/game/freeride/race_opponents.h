#pragma once

#include "game/freeride/ai_drivers.h"

#include <memory>
#include <string>
#include <vector>

class EdenRenderer;
struct GPUTexture;
struct WeatherPreset;

// The AI opponents of a race (docs/re/opponents_spec.md 2-4): the
// championship's DBLicense _bot_n drivers in their own cars (DBPNJCar, the car
// of the championship's class), on a two-wide grid behind the start line,
// racing the gates of the course.
//
// The retail AI is in the encrypted band [ENC] (the .ig2 IASA..IASJ keys are
// its settings, not decoded). The port's driver [INF]:
//   - the course: the gates in passing order (laps unrolled), each leg routed
//     over the island road graph (shortest path between the nearest link
//     points), resampled every 3 m and lightly smoothed;
//   - a speed profile per driver: the curve limit sqrt(lateral grip x radius)
//     and a backward braking pass, capped by the car's top speed
//     (_max_theorical_speed); the acceleration follows _acceleration_0_100_kmh;
//     grip, braking and the top speed scale with the bot's _difficulty;
//   - lane offsets: each car keeps its own line across the road, moves out to
//     pass a slower car ahead (more readily with a higher _aggressiveness) and
//     otherwise brakes behind it; car to car contact separates the two;
//   - a mild catch-up: a car far ahead of the player lifts a little, one far
//     behind pushes a little (the retail rubber band is not readable).
// The player's progress is the projection of the car onto the same course,
// which ranks the racers (finished first, by time; then by distance).
class RaceOpponents {
public:
    struct Entry {
        uint64_t Pnj = 0;
        std::string Name;
        int32_t CarId = 0;
        std::string ColorId;
        float Difficulty = 0.5f;
        float Aggressiveness = 0.5f;
        bool bRival = false;
    };
    struct Standing {
        std::string Name; // "" for the player
        bool bPlayer = false;
        bool bFinished = false;
        float Time = 0.0f;
        float Progress = 0.0f; // metres along the course
    };

    // The island road graph (MapScreen's; must outlive this object).
    void setRoads( const std::vector<Vector3f> *pNodes, const std::vector<std::pair<int32_t, int32_t>> *pEdges,
                   const std::vector<std::vector<Vector3f>> *pShapes );
    // waypoints: the start line, then every gate in passing order (laps
    // unrolled); startHeading: the grid's heading (radians, forward = (sin, 0, cos)).
    bool start( const std::vector<Vector3f> &waypoints, float startHeading, const std::vector<Entry> &entries,
                const CinematicCar::SurfaceFunction &surface );
    void stop();
    inline bool isActive() const { return !racers.empty(); }

    // bRunning: the race has started (after the countdown); raceTime: its clock.
    void tick( float deltaTime, bool bRunning, float raceTime, const Vector3f &playerPosition,
               const CinematicCar::SurfaceFunction &surface );
    // The player crossed the finish (the gate law of LicenseTest decides).
    void playerFinished( float time );

    // 1-based rank of the player, and the number of racers (player included).
    int32_t getPlayerPosition() const;
    inline int32_t getRacerCount() const { return static_cast<int32_t>( racers.size() ) + 1; }
    std::vector<Standing> getStandings() const;
    // Every opponent is home: the race time the last one finished (-1 otherwise).
    float getLastFinishTime() const;
    inline float getCourseLength() const { return finishDistance - startDistance; }
    // The grid slot of the player (beside the first opponent row).
    inline Vector3f getPlayerGridPosition() const { return playerGrid; }
    // Test hook (-opponentfollow <n>): a pose `behind` metres behind racer n
    // on the course (the chase camera rides with the pack).
    bool getFollowPose( int32_t index, float behind, Vector3f &outPosition, float &outYaw ) const;
    // The opponents' world positions (the traffic yields to them, the minimap).
    void appendPositions( std::vector<Vector3f> &out ) const;

    // Contact of the player car with the opponents (Traffic::resolveCollision's
    // circles and impulse) [INF].
    bool resolveCollision( const Vector3f &position, const Vector3f &forward, const Vector3f &velocity, float width,
                           float length, float mass, Vector3f &outDeltaVelocity );

    void assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube );
    void draw( EdenRenderer &renderer, const WeatherPreset *pWeather );

private:
    struct Racer {
        Entry Info;
        std::unique_ptr<AiCar> Car;
        std::vector<float> Profile; // target speed per course point (m/s)
        float Distance = 0.0f;      // metres along the course
        float Speed = 0.0f;
        float Lateral = 0.0f;       // metres right of the course line
        float TargetLateral = 0.0f;
        float HomeLateral = 0.0f;   // the car's own line
        float TopSpeed = 50.0f;
        float Accel = 6.0f;         // m/s^2 at rest
        float Brake = 8.0f;
        float Steer = 0.0f;
        float Stun = 0.0f;          // s of lost drive after a hit
        bool bFinished = false;
        bool bBraking = false;
        float FinishTime = 0.0f;
        Vector3f Position;
        float Yaw = 0.0f;
        float GroundY = 0.0f;
        bool bGround = false;
    };

    void buildCourse( const std::vector<Vector3f> &waypoints, float startHeading );
    void routeLeg( const Vector3f &from, const Vector3f &to, std::vector<Vector3f> &out ) const;
    struct EdgePoint {
        int32_t Edge = -1;
        float Along = 0.0f; // metres from the edge's first node
        float Gap = 1e9f;
    };
    EdgePoint nearestEdgePoint( const Vector3f &point ) const;
    void appendEdgeCurve( int32_t edge, float fromAlong, float toAlong, std::vector<Vector3f> &out ) const;
    void buildProfile( Racer &racer ) const;
    // Course point / tangent / curvature at a distance.
    void sample( float distance, Vector3f &outPoint, Vector3f &outTangent ) const;
    float curvatureAt( float distance ) const;
    float profileAt( const Racer &racer, float distance ) const;
    float projectPlayer( const Vector3f &position, float &outLateral ) const;
    // The nearest course point to `position` between two distances; -1 when
    // the car is more than 45 m off the course there.
    float projectWindow( const Vector3f &position, float from, float to, float &outLateral ) const;

    const std::vector<Vector3f> *pNodes = nullptr;
    const std::vector<std::pair<int32_t, int32_t>> *pEdges = nullptr;
    const std::vector<std::vector<Vector3f>> *pShapes = nullptr;
    std::vector<std::vector<int32_t>> adjacency; // node -> links
    std::vector<std::vector<float>> cumulative;  // per link: XZ length up to each curve point
    std::vector<float> edgeLength;

    std::vector<Vector3f> course;   // every kStep metres
    std::vector<float> curvature;   // signed 1/m per course point (positive = left)
    float startDistance = 0.0f;     // the start line on the course
    float finishDistance = 0.0f;    // the finish line
    std::vector<Racer> racers;
    Vector3f playerGrid;
    float playerDistance = 0.0f;
    float playerLateral = 0.0f;
    bool bPlayerFinished = false;
    float playerFinishTime = 0.0f;
    CinematicCar::SurfaceFunction surfaceFn;
    bool bNight = false;
};
