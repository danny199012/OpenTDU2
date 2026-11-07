#pragma once

#include "game/freeride/ig2_challenge.h"

#include <string>

// One licence test or championship race being driven (docs/re/licenses_spec.md
// 3 and 5): the car is put on the start gate of the .ig2, a 3 s countdown,
// then every gate in file order and the finish (a circuit: the gates then the
// start line, once per lap). A gate is passed when the car crosses its plane
// within half its width; crossing it a little outside (within 25 m) misses
// it and fails [INF: the retail rules are ENC]. The limit is CtDw (fail when
// it runs out). A time attack / licence test passes at or under the target
// timG; a race or a speedtrap has no opponents / score in the port and passes when
// it is finished inside the limit [INF]. Speed challenges are not run.
class LicenseTest {
public:
    enum class eStage { Idle, Countdown, Running, Finished };
    // Beaten: a race with opponents finished behind the winner (set by the
    // game mode from RaceOpponents' ranking; a race is won in 1st place [INF]).
    enum class eResult { None, Passed, TooSlow, MissedGate, OutOfTime, Abandoned, Beaten };

    bool start( const std::string &challengeName );
    void tick( float deltaTime, const Vector3f &carPosition );
    void abandon();
    // The game mode ends the run with a result (a race decided by the
    // opponents: "Beaten" once the field is home).
    void end( eResult newResult );
    // CtDw as the run's clock limit (on by default). A race with opponents
    // turns it off: the file's 240 s CtDw is far under the time a 3-lap,
    // 11 km course takes, so it cannot be the race's limit [INF].
    inline void setTimeLimit( bool bEnabled ) { bTimeLimit = bEnabled; }

    inline eStage getStage() const { return stage; }
    inline bool isActive() const { return stage != eStage::Idle; }
    inline const Ig2Challenge &getChallenge() const { return challenge; }
    // The start pose (gates[0]): position and heading (radians, forward = (sin, 0, cos)).
    Vector3f getStartPosition() const;
    float getStartHeading() const;
    // 3, 2, 1 while counting down (0 = "GO!"), -1 otherwise.
    int32_t getCountdownNumber() const;
    inline float getTime() const { return time; }
    // The next gate to pass: its position and its index in getChallenge().getGates().
    bool getNextGate( Vector3f &outPosition, int32_t &outIndex ) const;
    // The gates to pass in order (a circuit: every gate then the start line,
    // once per lap), and how many are passed.
    inline int32_t getGateCount() const { return static_cast<int32_t>( sequence.size() ); }
    inline int32_t getGatesPassed() const { return static_cast<int32_t>( step ); }
    // The gate index after the next one in the sequence (-1 at the end).
    int32_t getGateAfterNext() const;
    // The gate index of sequence step n (-1 past the end).
    inline int32_t getSequenceGate( size_t n ) const {
        return n < sequence.size() ? static_cast<int32_t>( sequence[n] ) : -1;
    }
    // Laps: the one being driven (1-based) and how many; the gates of a lap.
    inline int32_t getLapCount() const { return challenge.getLaps(); }
    int32_t getLap() const;
    inline int32_t getGatesPerLap() const { return gatesPerLap; }
    // The gate passed in this lap (1-based count, the checkpoint readout).
    inline int32_t getLapGatesPassed() const {
        return gatesPerLap > 0 ? static_cast<int32_t>( step ) % gatesPerLap : 0;
    }
    // One shot per gate passed: the race time at it, the split against the
    // same gate of the previous lap (0 on the first lap), and the lap time
    // when it closed a lap (-1 otherwise).
    struct GateEvent {
        int32_t Step = 0;
        float Time = 0.0f;
        float Split = 0.0f;
        bool bHasSplit = false;
        float LapTime = -1.0f;
        bool bLastLapStarts = false;
    };
    bool takeGateEvent( GateEvent &outEvent );
    // The time the current lap started (race seconds).
    inline float getLapStartTime() const { return lapStart; }
    // How far off the centre a crossing of the gate still counts, and the
    // half width of the model drawn on it (the pass zone is the wider).
    float getPassHalfWidth( size_t gateIndex ) const;
    void setVisualHalfWidth( size_t gateIndex, float halfWidth );
    // Test hook (-licensepark <step>): jump to a step of the sequence as if
    // the gates before it were passed (no events, no splits).
    void debugSkipTo( size_t newStep );
    // One shot at the end: the result and the time (seconds).
    bool takeResult( eResult &outResult, float &outTime );

private:
    Ig2Challenge challenge;
    eStage stage = eStage::Idle;
    eResult result = eResult::None;
    float countdown = 0.0f;
    float time = 0.0f;
    std::vector<size_t> sequence; // gate indices in passing order
    size_t step = 0;              // sequence[step] is the next gate
    Vector3f previous;
    bool bHasPrevious = false;
    bool bResultPending = false;
    bool bTimeLimit = true;
    int32_t gatesPerLap = 0;
    float lapStart = 0.0f;
    std::vector<float> passTimes; // race time at each step passed
    std::vector<float> lapStarts; // race time each lap started at
    std::vector<GateEvent> events;
    std::vector<float> visualHalfWidth; // per gate (0: nothing drawn)

    void finish( eResult newResult );
};

const char *LicenseResultText( LicenseTest::eResult result );
