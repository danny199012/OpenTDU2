#pragma once

#include "audio/gs_audio.h"
#include "core/math.h"

#include <memory>
#include <string>
#include <vector>

class Bank;

// Vehicle sounds, data driven from the CarVSTConfig.xmb stored in the sound
// bank of the car (Euro/Bnk/sound/vehicles/<car>_high.bnk):
//   VehicleConfigExport / SamplesConfig (SampleCInfo):
//     - sSampleName groups the samples by role (EngineLoad_*, EngineUnload_*,
//       Aerodynamic*, Transmission*, Skid*, Rolling*, GearBox*, EngineStart...);
//     - every sample has Links (PhysicsLinks): one curve per physics axis,
//       Audio 0 = pitch curve, Audio 1 = volume curve. Axes seen in the data:
//       0 = engine rpm, 1 = car speed (km/h), 3 = engine load
//       (0 .. ~470/485/620 scale of the car; negative = overrun),
//       26..29 = wheel slip angle (max over the wheels, rad),
//       34..37 = wheel slip ratio (max over the wheels, ratio x 10),
//       43 = suspension travel (not reproduced: ignored).
//     - nMustBeLooped: engine / transmission / skid / rolling / aerodynamic
//       layers are loops; bIsTrigger: one shots (start, gear shifts...).
//     - PlaybackStateVolume: gain per playback state (0 = exterior / track
//       camera view, 1 = interior, ...); the chase camera is state 0.
// The engine layers are all started when the engine starts and mixed by
// evaluating their curves every tick (the mixer ramps the gain changes).
class VehicleAudio {
public:
    struct Params {
        float Rpm = 0.0f;              // engine speed, axis 0
        float SpeedKmh = 0.0f;         // axis 1 (absolute)
        float Throttle = 0.0f;         // 0..1 (drives the smoothed load)
        Vector3f Position;             // car world position
        Vector3f Velocity;             // car world velocity (m/s)
        uint32_t Surface = 0;          // 0 road, 1 dirt, 2 grass (max of wheels)
        float MaxSlipRatio = 0.0f;     // max |slip ratio| over the wheels
        float MaxSlipAngle = 0.0f;     // max |slip angle| over the wheels (rad)
        bool bEngineOn = false;        // false: paused / cinematic / mute all
    };

    VehicleAudio() = default;
    ~VehicleAudio() { unload(); }

    // Loads CarVSTConfig.xmb + the samples from the sound bank of the car
    // (path relative to the game root, e.g.
    // "Euro/Bnk/sound/vehicles/california_high.bnk").
    bool load( const char *pSoundBankPath );
    void unload();

    // Starts the engine: the loop layers + the EngineStart one shot.
    void startEngine();
    // Stops everything.
    void stop();

    // Updates the layer gains / pitches. Call every tick while driving.
    void update( const Params &params, float deltaTime );
    // Gear change one shots (getGear() delta; <= 0 gears are ignored).
    void setGear( int32_t gear );

    // The player's horn (CarVSTConfig "Horn", PanelID 7 -> the EngineFx
    // client, the car's own carhorn_loop_*.wav). Held, like the button: the
    // sample loops while bOn, stops when released.
    void setHorn( bool bOn );
    inline bool hasHorn() const { return hornLayer >= 0; }

    inline bool isLoaded() const { return pBank != nullptr; }
    inline bool isEngineOn() const { return bEngineRunning; }
    // Highest layer gain of the last update (0 = silent, ~1 = audible). The
    // engine layers are started muted and brought in by update(): a silent
    // car with bEngineRunning true shows up here as 0.
    float getMaxLayerGain() const;
    // Smoothed engine load of the last update (axis 3, the overrun side is
    // negative); 0 while bEngineOn is false.
    inline float getLoad() const { return smoothedLoad; }

private:
    struct Curve {
        std::vector<float> Xs;
        std::vector<float> Ys;
        float eval( float x ) const;
    };

    // One playable sample of SamplesConfig.
    struct Layer {
        struct Link {
            int32_t Axis = -1;
            bool bPitch = false;
            Curve CurveData;
        };
        std::string Name;
        std::shared_ptr<AudioClip> Clip;
        std::vector<Link> Links;
        float BaseVolume = 1.0f;
        float StateVolume = 1.0f;
        float ClipNear = 4.0f;
        float ClipFar = 40.0f;
        int32_t Panel = 0;
        bool bLoop = true;
        bool bTrigger = false;
        // Runtime
        GSAudio::VoiceHandle Voice = GSAudio::kInvalidVoice;
        float CurrentGain = -1.0f; // -1: first update (no ramp from 1)
        float CurrentPitch = 1.0f;
        int32_t SoundSurface = -1; // Road / Dirt / Grass family, -1 any
    };

    void playLayer( Layer &layer );
    void updateLayer( Layer &layer, const Params &params, float loadRaw, float gainScale );

    Bank *pBank = nullptr; // owned
    std::vector<Layer> layers;
    int32_t startLayer = -1;
    // The "Horn" sample of CarVSTConfig (PanelID 7) and its held voice.
    int32_t hornLayer = -1;
    GSAudio::VoiceHandle hornVoice = GSAudio::kInvalidVoice;
    // Gear shift triggers by name (GearBoxUp/Down x Internal/External).
    int32_t gearUp[2] = { -1, -1 };    // [0] internal [1] external
    int32_t gearDown[2] = { -1, -1 };

    bool bEngineRunning = false;
    float smoothedLoad = 0.0f;
    float loadScale = 470.0f; // max positive x of the load curves (car specific)
    float smoothedRpm = 0.0f;
    float smoothedSpeed = 0.0f;
    float smoothedSlipRatio = 0.0f;
    float smoothedSlipAngle = 0.0f;
    int32_t lastGear = 0;
    Vector3f lastPosition;
    Vector3f smoothedVelocity;
};
