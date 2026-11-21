#pragma once

#include "core/math.h"

#include <string>
#include <vector>

// A racing school test's recorded run (Euro/Bnk/ig2/replays/<test>.rly, docs/re/licenses_spec.md 12.11): the retail pre-test
// presentation plays this recording of the test car driving the course [CODE-dev: GMCompDrivingSchool "replay trajectory"].
// Layout [DATA]: u32 version (18), u32 end of the recording data, u32 XMB size, the XMB (ReplayBaseParams: chunk sizes / rates and
// counts), u32 car chunk size + the car chunk, MatrixChunkNumberUsed matrix chunks (one per MatrixChunkRate frames, 105 bytes:
// header, then at +9 x y z (float) and at +25 a 3 x 8 byte int16 / 32767 rotation matrix, the rows being the car's x / y / z axes),
// InputChunkNumberUsed input chunks, FrameCount timestep chunks (float dt = 1/90 s, float time of day in seconds).
// Only the keyframe poses are read: they are every MatrixChunkRate (18) frames = 0.2 s, smooth enough to drive the car by.
class TestReplay {
public:
    bool load( const std::string &testName );
    inline bool isLoaded() const { return !keys.empty(); }
    inline float getDuration() const { return duration; }
    // The car id (DBCarPhysicsData) the run was recorded with.
    inline int32_t getCarId() const { return carId; }
    // Time of day of the recording (seconds since midnight), -1 when unknown.
    inline float getClock() const { return clock; }
    // The pose at a time (seconds from the start of the recording): position and the car's axes.
    void sample( float time, Vector3f &outPosition, Vector3f &outForward, Vector3f &outUp ) const;

private:
    struct Key {
        Vector3f Position;
        Vector3f Right, Up, Forward;
    };
    std::vector<Key> keys;
    float keyInterval = 0.2f;
    float duration = 0.0f;
    int32_t carId = -1;
    float clock = -1.0f;
};
