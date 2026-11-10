#pragma once

#include <cstdint>

struct SDL_Gamepad;
struct SDL_Haptic;

// Force feedback of the FREERIDE car on a steering wheel (any SDL gamepad
// whose device exposes haptics - wheels do, pads with plain rumble do not,
// which gates this naturally): a constant torque (the self-alignment pull),
// a speed damper, and a one-shot kick on a crash.
//
// The composition (what the game mode feeds us each frame) is this port's
// own law, written from the physics' per-wheel state (docs/re/ffb_spec.md);
// the shape follows TDU1's device layer as documented in
// docs/re/opentdu1_reuse.md 2.11 (torque + damper + slip lightness, an
// invert flag) - the upstream source carries no licence, so nothing was
// copied from it.
//
// Everything is opt-out (-noffb / the options.ini "ffb" row), re-checked
// every frame (a wheel can be plugged in later) and silent without a device.
class WheelFFB {
public:
    WheelFFB() = default;
    ~WheelFFB() { shutdown(); }
    WheelFFB( const WheelFFB & ) = delete;
    WheelFFB &operator=( const WheelFFB & ) = delete;

    // The steering feel of this frame (composed from the vehicle physics).
    struct Feel {
        bool bActive = false; // driving; menus / cinematics park the wheel
        float Torque = 0.0f;  // [-1, 1] rim torque, + = pull to the right
        float Damper = 0.0f;  // [0, 1] static friction (scales with speed)
    };
    void setFeel( const Feel &f ) { feel = f; }

    // One-shot punch (a crash): 0..1, decays over ~0.18 s, overlaid on the
    // constant torque in its own direction (same call sites as
    // GamepadOutput::pulse).
    void kick( float strength );

    // Called once per frame from the game mode: re-attaches the wheel,
    // updates the effects on a change (a ~0.25 s heartbeat keeps SDL honest).
    void update( float deltaTime );
    // Stops the effects and forgets the device (exit, option off, unplugged).
    void shutdown();

    inline bool isAttached() const { return pHaptic != nullptr; }
    inline bool hasConstant() const { return bConstant; }
    inline bool hasDamper() const { return bDamper; }
    inline float getTorque() const { return feel.Torque; }

private:
    void attach();
    void detach();
    void applyEffects( float deltaTime );

    Feel feel;
    SDL_Gamepad *pPad = nullptr;
    SDL_Haptic *pHaptic = nullptr;
    bool bConstant = false;
    bool bDamper = false;
    bool bDisabled = false;

    int32_t constantEffect = -1; // SDL_HapticEffectID
    int32_t damperEffect = -1;
    int32_t constantLevel = 0;   // last uploaded Sint16 levels
    int32_t damperLevel = 0;
    float heartbeat = 0.0f;      // re-issue timer

    float kickValue = 0.0f;      // decaying overlay
    float kickTimeLeft = 0.0f;
};
