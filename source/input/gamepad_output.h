#pragma once

#include "core/math.h"

#include <cstdint>
#include <memory>

struct SDL_Gamepad;
struct AudioClip;

// Controller extras of the FREERIDE pad (DualSense first, any SDL gamepad with
// the capability): light bar, haptics.
//
// What SDL3 3.4 exposes (and what this port therefore can do):
//   - rumble      SDL_RumbleGamepad / SDL_RumbleGamepadTriggers (capability
//                 flags of SDL_GetGamepadProperties). The DualSense vibrators
//                 are driven through it, so a "road feel" of a tyre on gravel
//                 and a punch on a crash both work.
//   - light bar   SDL_SetGamepadLED (the DualSense / DS4 light bar; returns
//                 false when the pad has none). Red / blue alternating while
//                 the police chase you, an idle colour otherwise.
//   - speaker     NOT exposed by SDL3: the PS5 driver of SDL writes the
//                 DualSense audio report bytes (ucSpeakerVolume / the audio
//                 enable bits) but offers no API to push PCM to the pad, and
//                 neither hidapi nor a raw HID path is part of this port. The
//                 interface below keeps the hook (playSpeaker) so a future
//                 backend can fill it; it reports false today.
//   - adaptive    no dedicated API, but SDL_SendGamepadEffect passes a raw
//     triggers    DualSense effects report (DS5EffectsState_t of SDL's
//                 hidapi PS5 driver, the format SDL's own testcontroller
//                 uses): enable bits 0x04 / 0x08 select the right / left
//                 trigger's 11 effect bytes, everything else in the report is
//                 left as it is. See setTriggerFeel.
//
// Everything is opt-out (-nods), re-checked every frame (a pad can be plugged
// in later) and silent without a device.
class GamepadOutput {
public:
    GamepadOutput() = default;
    ~GamepadOutput() { shutdown(); }
    GamepadOutput( const GamepadOutput & ) = delete;
    GamepadOutput &operator=( const GamepadOutput & ) = delete;

    // Called once per frame from the game mode: re-attaches the pad, applies
    // the pending light bar / rumble state.
    void update( float deltaTime );
    // Drops the rumble / light bar and forgets the pad (exit, device change).
    void shutdown();

    // The chase: red / blue alternating at ~1.4 Hz.
    void setWanted( bool bWanted );
    inline bool isWanted() const { return bWanted; }
    // Idle colour (used when not wanted); the default is off.
    void setBaseColor( uint8_t red, uint8_t green, uint8_t blue );

    // One-shot punch (a crash, a gear shift): 0..1, decays over 'seconds'.
    void pulse( float strength, float seconds );
    // Continuous road feel 0..1 (speed on a rough surface, engine idle).
    void setRoadFeel( float strength );
    // Adaptive triggers (DualSense only; port extra, not in the original
    // [INF]): L2 brake = firm resistance, a buzz while ABS works; R2 throttle =
    // light resistance, a buzz on wheelspin / traction control. Inactive (menus,
    // walking, cinematics) = both triggers free. Call every frame; sent only on
    // a change. -notriggers turns it off, -logtriggers logs each change.
    struct TriggerFeel {
        bool bActive = false;
        bool bAbs = false;
        bool bWheelspin = false;
    };
    void setTriggerFeel( const TriggerFeel &feel ) { triggerFeel = feel; }

    inline bool isAttached() const { return pPad != nullptr; }
    inline bool hasRumble() const { return bRumble; }
    inline bool hasLED() const { return bLED; }
    // SDL3 has no controller audio path (see the header comment): always false.
    inline bool hasSpeaker() const { return false; }
    // Hook of the future DualSense speaker backend: plays a one-shot through
    // the pad's own speaker. Returns false (no backend) - the call is a no-op.
    bool playSpeaker( const std::shared_ptr<AudioClip> &clip, float volume = 1.0f );

private:
    void attach();
    void detach();
    void applyRumble( float deltaTime );
    void applyLightBar( float deltaTime );
    void applyTriggers();
    void sendTriggers( const uint8_t ( &right )[11], const uint8_t ( &left )[11] );

    SDL_Gamepad *pPad = nullptr;
    bool bRumble = false;
    bool bLED = false;
    bool bDisabled = false;

    bool bWanted = false;
    uint8_t baseRed = 0, baseGreen = 0, baseBlue = 0;
    float ledClock = 0.0f;
    uint8_t lastRed = 0, lastGreen = 0, lastBlue = 0;
    bool bLedSet = false;

    float roadFeel = 0.0f;
    float pulsePeak = 0.0f;
    float pulseStrength = 0.0f;
    float pulseTimeLeft = 0.0f;
    float pulseDuration = 0.0f;
    float rumbleTimer = 0.0f;
    float lastLow = -1.0f;
    float lastHigh = -1.0f;

    bool bTriggers = false; // a DualSense (SDL_GAMEPAD_TYPE_PS5) is attached
    TriggerFeel triggerFeel;
    uint8_t lastRightTrigger[11] = {};
    uint8_t lastLeftTrigger[11] = {};
    bool bTriggersSent = false;
};
