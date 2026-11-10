#include "shared.h"
#include "input/wheel_ffb.h"

#include "game/game_options.h"
#include "input/gs_input.h"
#include "system/cmd_line.h"

#include <SDL3/SDL.h>

#include <cstdlib>

namespace {
// The constant torque is re-issued when it moved by this much (of the full
// scale) or after this long - SDL_UpdateHapticEffect is cheap, a full-rate
// stream is not needed (the device smooths).
constexpr float kTorqueStep = 0.004f;
constexpr float kHeartbeat = 0.25f;
// The kick overlay decays over this long.
constexpr float kKickTime = 0.18f;
// SDL level scale (Sint16 full deflection).
constexpr float kLevel = 32767.0f;
} // namespace

void WheelFFB::attach() {
    if ( pHaptic != nullptr || gpInput == nullptr ) {
        return;
    }
    SDL_Gamepad *pGamepad = gpInput->getGamepad();
    if ( pGamepad == nullptr ) {
        return;
    }
    // Pads with plain rumble (XInput / DualSense) expose no haptic device in
    // SDL3; DirectInput wheels do. The probe is the gate, not the device
    // type. SDL 3.4 has no haptic-from-gamepad opener: the pad's joystick
    // carries it.
    SDL_Joystick *pJoystick = SDL_GetGamepadJoystick( pGamepad );
    if ( pJoystick == nullptr ) {
        return;
    }
    SDL_Haptic *pCandidate = SDL_OpenHapticFromJoystick( pJoystick );
    if ( pCandidate == nullptr ) {
        static bool bLogged = false;
        if ( !bLogged ) {
            bLogged = true;
            OTDU_LOG_INFO( "FFB: the pad has no haptic device (%s) - no wheel force feedback\n",
                           SDL_GetError() );
        }
        return;
    }
    const SDL_HapticID id = SDL_GetHapticID( pCandidate );
    pHaptic = pCandidate;
    pPad = pGamepad;
    const Uint32 features = SDL_GetHapticFeatures( pHaptic );
    bConstant = ( features & SDL_HAPTIC_CONSTANT ) != 0;
    bDamper = ( features & SDL_HAPTIC_DAMPER ) != 0;
    constantEffect = -1;
    damperEffect = -1;
    constantLevel = 0;
    damperLevel = 0;
    heartbeat = 0.0f;
    const char *pName = SDL_GetHapticName( pHaptic );
    OTDU_LOG_INFO( "FFB: '%s' (id %u) attached (constant %s, damper %s)\n",
                   pName != nullptr ? pName : "?", id, bConstant ? "yes" : "no", bDamper ? "yes" : "no" );
}

void WheelFFB::detach() {
    pHaptic = nullptr;
    pPad = nullptr;
    bConstant = false;
    bDamper = false;
    constantEffect = -1;
    damperEffect = -1;
    constantLevel = 0;
    damperLevel = 0;
    kickValue = 0.0f;
    kickTimeLeft = 0.0f;
}

void WheelFFB::shutdown() {
    if ( pHaptic != nullptr ) {
        SDL_StopHapticEffects( pHaptic );
        SDL_CloseHaptic( pHaptic ); // the device stops its own effects on close
    }
    detach();
}

void WheelFFB::kick( float strength ) {
    const float value = Saturate( strength );
    if ( value <= kickValue ) {
        return; // a stronger kick is still running
    }
    kickValue = value;
    kickTimeLeft = kKickTime;
    heartbeat = 0.0f; // apply on this frame
}

void WheelFFB::update( float deltaTime ) {
    static const bool bOptOut = CmdLine::HasArg( "-noffb" );
    const bool bWasDisabled = bDisabled;
    bDisabled = bOptOut || GetGameOptions().FFB == 0;
    if ( bDisabled ) {
        if ( !bWasDisabled && pHaptic != nullptr ) {
            shutdown(); // the option was just turned off: free the wheel
            OTDU_LOG_INFO( "FFB: off\n" );
        }
        return;
    }
    attach();
    if ( pHaptic != nullptr && ( gpInput == nullptr || gpInput->getGamepad() != pPad ) ) {
        // The wheel was unplugged (or replaced): drop the state, attach()
        // picks the new device up on the next frames.
        detach();
    }
    if ( pHaptic != nullptr ) {
        if ( kickTimeLeft > 0.0f ) {
            kickTimeLeft = Max( kickTimeLeft - deltaTime, 0.0f );
            if ( kickTimeLeft <= 0.0f ) {
                kickValue = 0.0f;
            }
        }
        applyEffects( deltaTime );
    }
    static const bool bLog = CmdLine::HasArg( "-logffb" );
    if ( bLog ) {
        // The composition state line, device or not: a headless run proves
        // the physics feed end to end even where no wheel can attach
        // (docs/re/ffb_spec.md 5).
        static float sLogTimer = 0.0f;
        sLogTimer -= deltaTime;
        if ( sLogTimer <= 0.0f ) {
            sLogTimer = 0.5f;
            OTDU_LOG_INFO( "FFB: %s, torque %+.3f, damper %.2f%s%s\n",
                           pHaptic != nullptr ? "attached" : "no device",
                           feel.bActive ? Clamp( feel.Torque, -1.0f, 1.0f ) : 0.0f,
                           feel.bActive ? Saturate( feel.Damper ) : 0.0f,
                           kickTimeLeft > 0.0f ? ", kick" : "", feel.bActive ? "" : ", parked" );
        }
    }
}

void WheelFFB::applyEffects( float deltaTime ) {
    if ( !bConstant && !bDamper ) {
        return;
    }
    // The kick rides on the constant torque in its own direction (a crash
    // from a slide jolts the way the force already pulls; one from the front
    // jolts against the centring pull).
    const float kick = kickValue * 0.45f;
    float torque = feel.bActive ? Clamp( feel.Torque, -1.0f, 1.0f ) : 0.0f;
    if ( kick > 0.0f ) {
        const float sign = torque >= 0.0f ? 1.0f : -1.0f;
        torque = Clamp( torque + sign * kick, -1.0f, 1.0f );
    }
    const bool bInvert = CmdLine::HasArg( "-ffbinvert" );
    float gain = 1.0f;
    if ( const char *pGain = CmdLine::GetArgValue( "-ffbgain" ) ) {
        gain = Clamp( static_cast<float>( atof( pGain ) ), 0.1f, 4.0f );
    }
    if ( bInvert ) {
        torque = -torque;
    }
    const int32_t level = static_cast<int32_t>( lroundf( -torque * kLevel * gain ) );
    const float damper = feel.bActive ? Saturate( feel.Damper ) : 0.0f;
    const int32_t damperCoeff = static_cast<int32_t>( lroundf( damper * kLevel ) );

    bool bChanged = false;
    if ( abs( level - constantLevel ) > static_cast<int32_t>( lroundf( kTorqueStep * kLevel * gain ) ) ) {
        bChanged = true;
    }
    if ( abs( damperCoeff - damperLevel ) > static_cast<int32_t>( lroundf( 0.02f * kLevel ) ) ) {
        bChanged = true;
    }
    heartbeat -= deltaTime;
    if ( heartbeat <= 0.0f ) {
        heartbeat = kHeartbeat;
        bChanged = true;
    }
    if ( !bChanged ) {
        return;
    }

    if ( bConstant ) {
        SDL_HapticEffect effect = {};
        effect.constant.type = SDL_HAPTIC_CONSTANT;
        // Cartesian +x: the steering axis. The level sign carries the side
        // (negative level = the opposite direction on DirectInput).
        effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = 1;
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.level = static_cast<Sint16>( Clamp( level, -32767, 32767 ) );
        if ( constantEffect < 0 ) {
            constantEffect = SDL_CreateHapticEffect( pHaptic, &effect );
            if ( constantEffect < 0 ) {
                OTDU_LOG_INFO( "FFB: constant effect refused (%s)\n", SDL_GetError() );
                bConstant = false;
                return;
            }
        } else {
            SDL_UpdateHapticEffect( pHaptic, constantEffect, &effect );
        }
        constantLevel = level;
    }
    if ( bDamper ) {
        SDL_HapticEffect effect = {};
        effect.condition.type = SDL_HAPTIC_DAMPER;
        effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.condition.direction.dir[0] = 1;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        // Axis velocity resists the motion on both sides; the saturation at
        // full scale lets the coefficient reach the device's own maximum.
        for ( int32_t axis = 0; axis < 3; axis++ ) {
            effect.condition.right_sat[axis] = 0xffff;
            effect.condition.left_sat[axis] = 0xffff;
            effect.condition.right_coeff[axis] = static_cast<Sint16>( Clamp( damperCoeff, 0, 32767 ) );
            effect.condition.left_coeff[axis] = static_cast<Sint16>( Clamp( damperCoeff, 0, 32767 ) );
        }
        if ( damperEffect < 0 ) {
            damperEffect = SDL_CreateHapticEffect( pHaptic, &effect );
            if ( damperEffect < 0 ) {
                OTDU_LOG_INFO( "FFB: damper effect refused (%s)\n", SDL_GetError() );
                bDamper = false;
                return;
            }
        } else {
            SDL_UpdateHapticEffect( pHaptic, damperEffect, &effect );
        }
        damperLevel = damperCoeff;
    }
}
