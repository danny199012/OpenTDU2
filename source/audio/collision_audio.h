#pragma once

#include "audio/gs_audio.h"
#include "core/math.h"

#include <memory>
#include <string>
#include <vector>

class Bank;

// Impact / scrape sounds of the cars: the PHYSIC_SFX wavs of fx/physicsfxsetup.bnk [DATA]
// (carmetal_low/high01..06, carground_*, carcar_*, carsuspension01..07, carmetal_loop01..04 ...), played
// through the "PhysicSfx" bfx client (21, a spatial pipeline, docs/re/sound3d_spec.md).
// Which sample the retail game plays for which contact is driven by physicsfxsetup's entities.xmb and the
// materials of the Havok contact (both of which the port does not have per contact), so the choice here is
// [INF]: a wall hit is carmetal (low under 6 m/s, high above), a floor hit is carground, a traffic / opponent
// hit is carcar, a wall rubbed along is the carmetal_loop layer whose gain and pitch follow the slide speed.
class CollisionAudio {
public:
    CollisionAudio() = default;
    ~CollisionAudio() { unload(); }
    CollisionAudio( const CollisionAudio & ) = delete;
    CollisionAudio &operator=( const CollisionAudio & ) = delete;

    bool load();
    void unload();
    void stop();

    // One impact at a world position. speed: closing speed along the normal (m/s).
    void hitWall( const Vector3f &position, float speed );
    void hitFloor( const Vector3f &position, float speed );
    void hitCar( const Vector3f &position, float speed );

    // Called every frame: slide speed of the fastest wall contact (0 = none) at `position`.
    void updateScrape( const Vector3f &position, float slideSpeed, float deltaTime );

    inline bool isLoaded() const { return bLoaded; }
    inline int32_t getHitCount() const { return hitCount; }

private:
    struct Pool {
        std::vector<std::shared_ptr<AudioClip>> Clips;
        uint32_t Cursor = 0;
    };
    void loadPool( Bank &bank, Pool &pool, const char *pPrefix, int32_t first, int32_t last );
    void play( Pool &pool, const Vector3f &position, float volume, float pitch );

    Pool metalLow, metalHigh, groundLow, groundHigh, carLow, carHigh;
    Pool scrapeLoop;
    GSAudio::VoiceHandle scrapeVoice = GSAudio::kInvalidVoice;
    float scrapeLevel = 0.0f;
    float scrapeIdle = 0.0f;
    float cooldown = 0.0f; // seconds until the next one shot may start
    uint32_t seed = 0x2468ace1u;
    int32_t hitCount = 0;
    bool bLog = false;
    bool bLoaded = false;
};
