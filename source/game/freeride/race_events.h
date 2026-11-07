#pragma once

#include "core/math.h"

#include <functional>
#include <string>
#include <vector>

// Instant challenges of the free ride (the retail race markers with the
// "Press ... to challenge other players" hint, loc 2220). A race is a point
// to point along the island road graph: drive into a start marker, press A,
// countdown (the HUD's own /_3_2_1_go), then pass the checkpoints to the
// finish; the time and the reward show at the end.
//
// The retail race events are DBEvent rows (_eventtype / _moneyreward /
// _eventfile); the rows carry no world position and the event files of the
// encrypted band are not readable [ENC], so the start positions and the
// routes here are built from the road graph [INF: 8 starts spread over the
// island, 3-5 km point to point], with the DBEvent rewards when the rows
// load.
class RaceEvents {
public:
    struct RaceStart {
        Vector3f Position;
        std::string Name;   // DBEvent hashcode name or "Instant challenge"
        int32_t Reward = 500;
        bool bUsed = false; // the race was won once (the retail markers stay)
    };

    enum class eStage { Idle, Countdown, Running, Finished };

    using SurfaceFunction = std::function<bool( float x, float z, float y, float &outHeight )>;

    // starts: the road graph to place the starts and the routes on.
    bool load( const std::string &island, const std::vector<Vector3f> &roadNodes,
               const std::vector<std::pair<int32_t, int32_t>> &roadEdges );
    void unload();
    inline bool isLoaded() const { return !starts.empty(); }
    inline const std::vector<RaceStart> &getStarts() const { return starts; }

    // The nearest start within the radius (-1: none).
    int32_t findStartNear( const Vector3f &position, float radius ) const;

    // A -> the countdown begins (3 s), then the race runs.
    bool start( int32_t startIndex );
    void tick( float deltaTime, const Vector3f &carPosition );

    inline eStage getStage() const { return stage; }
    // 3, 2, 1 while counting down (0 = "GO!" on the last tick), -1 otherwise.
    int32_t getCountdownNumber() const;
    inline bool isRunning() const { return stage == eStage::Running; }
    inline float getRaceTime() const { return raceTime; }
    // The next checkpoint (world position) and its index, when running.
    bool getNextCheckpoint( Vector3f &outPosition, int32_t &outIndex ) const;
    inline int32_t getCheckpointCount() const { return static_cast<int32_t>( checkpoints.size() ); }
    inline const std::vector<Vector3f> &getCheckpoints() const { return checkpoints; }
    inline int32_t getNextCheckpointIndex() const { return nextCheckpoint; }
    // The start of the race being run (-1: none).
    inline int32_t getActiveStart() const { return stage == eStage::Idle ? -1 : activeStart; }
    // One shot: the race finished (outTime = the seconds, outReward = paid).
    bool takeFinished( float &outTime, int32_t &outReward, bool &outSuccess );
    void abandon();

private:
    void buildRoute( int32_t startNode );

    std::vector<RaceStart> starts;
    std::vector<Vector3f> roadNodes;
    std::vector<std::vector<int32_t>> adjacency; // node -> neighbour nodes
    eStage stage = eStage::Idle;
    float countdownTime = 0.0f;
    float raceTime = 0.0f;
    int32_t activeStart = -1;
    std::vector<Vector3f> checkpoints;
    int32_t nextCheckpoint = 0;
    float finishTime = 0.0f;
    bool bFinishEvent = false;
};
