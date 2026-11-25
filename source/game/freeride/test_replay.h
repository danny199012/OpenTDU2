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
    // The other recorded cars (the overtaking tests: CarNumber slots interleaved per keyframe, slot 0 the test car, the
    // school traffic next; ambient traffic / unused slots carry no data and have no keys).
    inline int32_t getSlotCount() const { return static_cast<int32_t>( slots.size() ); }
    int32_t getSlotCarId( int32_t slot ) const;
    bool hasSlot( int32_t slot ) const;
    // The pose of a slot's car (false when the slot was not recorded).
    bool sampleSlot( int32_t slot, float time, Vector3f &outPosition, Vector3f &outForward ) const;

private:
    struct Key {
        Vector3f Position;
        Vector3f Right, Up, Forward;
    };
    std::vector<Key> keys;
    struct Slot {
        int32_t CarId = -1;
        std::vector<Key> Keys;
    };
    std::vector<Slot> slots; // slot 0 mirrors keys
    static void SampleKeys( const std::vector<Key> &keys, float keyInterval, float time, Vector3f &outPosition,
                            Vector3f &outForward, Vector3f &outUp );
    float keyInterval = 0.2f;
    float duration = 0.0f;
    int32_t carId = -1;
    float clock = -1.0f;
};
