#pragma once

#include "audio/gs_audio.h"
#include "core/math.h"

#include <memory>
#include <string>
#include <vector>

class Bank;

// Ambient traffic sounds (docs/re/ambience_audio_spec.md 3).
//
// The traffic *model* banks (islands/<island>/vehicules/traffic/*.bnk) carry
// geometry only; the drivable cars' sound banks in sound/vehicles are the
// library the ambient cars are voiced from. The generic car bank
// (sound/vehicles/generic_high_high.bnk, CarVSTConfig + the pagani_* engine
// loops + Horn_class_1.wav) is the one the ambient traffic uses [INF: the
// retail traffic mixes a few vehicle sound banks through the OtherVehicles
// client; one generic set keeps the voice budget sane].
//
// One looped engine voice per nearby car (the OtherVehicles client, a 5-input
// spatial pipeline), pitch / gain from its speed and distance, plus a pool of
// one-shot horns. Ten engines + three horns is the cap: the mixer's spatializer
// is cheap but the callback is single threaded.
class TrafficAudio {
public:
    // One live traffic car, as the game mode hands it over (Traffic fills it).
    struct Agent {
        uint32_t Id = 0;
        Vector3f Position;
        Vector3f Velocity;
        float Speed = 0.0f; // m/s
        bool bPolice = false;
    };

    TrafficAudio() = default;
    ~TrafficAudio() { unload(); }
    TrafficAudio( const TrafficAudio & ) = delete;
    TrafficAudio &operator=( const TrafficAudio & ) = delete;

    // soundBankPath: default "Euro/Bnk/sound/vehicles/generic_high_high.bnk".
    bool load( const char *pSoundBankPath = nullptr );
    void unload();
    void stop();

    // Matches the nearest agents to engine slots. listener: the audio listener
    // (the camera), used for the slot selection only - the mixer does the pan.
    void update( const std::vector<Agent> &agents, const Vector3f &listener, float deltaTime );

    // A horn one shot at a world position (Traffic::HornEvent).
    void horn( const Vector3f &position, float volume = 1.0f );

    inline bool isLoaded() const { return hornClip != nullptr || engineClip != nullptr; }
    inline int32_t getVoiceCount() const { return engineVoices; }
    inline int32_t getHornCount() const { return hornCount; }
    inline float getNearestDistance() const { return nearestDistance; }

private:
    static constexpr int32_t kMaxVoices = 10;
    static constexpr int32_t kMaxHorns = 3;

    struct EngineSlot {
        GSAudio::VoiceHandle Voice = GSAudio::kInvalidVoice;
        uint32_t AgentId = 0;
        float Gain = 0.0f;
        float Pitch = 1.0f;
    };

    struct HornSlot {
        GSAudio::VoiceHandle Voice = GSAudio::kInvalidVoice;
        float Level = 0.0f;
        float TimeLeft = 0.0f;
    };

    Bank *pBank = nullptr; // owned
    std::shared_ptr<AudioClip> engineClip;
    std::shared_ptr<AudioClip> hornClip;
    EngineSlot engines[kMaxVoices];
    HornSlot horns[kMaxHorns];
    int32_t engineVoices = 0;
    int32_t hornCount = 0;
    float nearestDistance = -1.0f;
    uint32_t nextId = 1;
};
