#include "shared.h"
#include "input/gamepad_output.h"

#include "audio/gs_audio.h"
#include "game/game_options.h"
#include "input/gs_input.h"
#include "system/cmd_line.h"

#include <SDL3/SDL.h>

namespace {
// Rumble is re-issued this often (SDL cancels the previous effect on every
// call, so a continuous feel needs a heartbeat shorter than its duration).
constexpr float kRumblePeriod = 0.1f;
constexpr uint32_t kRumbleDurationMs = 220;
// Red / blue half period of the chase flash (s).
constexpr float kWantedHalfPeriod = 0.35f;
constexpr uint8_t kWantedRed[3] = { 220, 20, 20 };
constexpr uint8_t kWantedBlue[3] = { 20, 50, 235 };
// Below this strength the rumble is let go (SDL would keep the motors buzzing
// on the smallest value).
constexpr float kRumbleFloor = 0.02f;

// DualSense trigger effects (11 bytes each; the legacy modes SDL's
// testcontroller uses [CODE: SDL test/testcontroller.c CyclePS5TriggerEffect]):
//   0x05                         off (free trigger)
//   0x01 start force             constant resistance from 'start' (0..255 of the pull)
//   0x06 frequency amplitude start  vibrating resistance
// The values are a feel chosen for the port [INF], not from the game.
constexpr uint8_t kTriggerOff[11] = { 0x05 };
constexpr uint8_t kBrakeFirm[11] = { 0x01, 24, 150 };
constexpr uint8_t kBrakeAbs[11] = { 0x06, 28, 63, 24 };
constexpr uint8_t kThrottleLight[11] = { 0x01, 0, 45 };
constexpr uint8_t kThrottleSpin[11] = { 0x06, 38, 40, 16 };

// The part of SDL's DS5EffectsState_t (SDL_hidapi_ps5.c) up to the trigger
// bytes; SDL copies min(size, report) bytes, the rest stays zero (disabled).
struct DualSenseEffects {
    uint8_t EnableBits1;   // 0x04 right trigger, 0x08 left trigger
    uint8_t EnableBits2;
    uint8_t RumbleRight;
    uint8_t RumbleLeft;
    uint8_t HeadphoneVolume;
    uint8_t SpeakerVolume;
    uint8_t MicrophoneVolume;
    uint8_t AudioEnableBits;
    uint8_t MicLightMode;
    uint8_t AudioMuteBits;
    uint8_t RightTrigger[11]; // offset 10
    uint8_t LeftTrigger[11];  // offset 21
};
static_assert( sizeof( DualSenseEffects ) == 32, "DS5EffectsState_t layout" );
} // namespace

void GamepadOutput::attach() {
    if ( pPad != nullptr || gpInput == nullptr ) {
        return;
    }
    SDL_Gamepad *pGamepad = gpInput->getGamepad();
    if ( pGamepad == nullptr ) {
        return;
    }
    pPad = pGamepad;
    const SDL_PropertiesID properties = SDL_GetGamepadProperties( pPad );
    bRumble = SDL_GetBooleanProperty( properties, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false );
    // There is no LED capability property: SDL_SetGamepadLED reports it by
    // returning false, the first applyLightBar settles bLED.
    bLED = true;
    bLedSet = false;
    lastLow = lastHigh = -1.0f;
    bTriggers = SDL_GetGamepadType( pPad ) == SDL_GAMEPAD_TYPE_PS5 && !CmdLine::HasArg( "-notriggers" );
    bTriggersSent = false;
    const char *pName = SDL_GetGamepadName( pPad );
    OTDU_LOG_INFO( "Gamepad: '%s' attached (rumble %s, light bar probed, adaptive triggers %s, speaker %s)\n",
                   pName != nullptr ? pName : "?", bRumble ? "yes" : "no", bTriggers ? "yes" : "no",
                   hasSpeaker() ? "yes" : "no (SDL3)" );
}

void GamepadOutput::detach() {
    pPad = nullptr;
    bTriggers = false;
    bTriggersSent = false;
    bRumble = false;
    bLED = false;
    bLedSet = false;
    lastLow = lastHigh = -1.0f;
}

void GamepadOutput::shutdown() {
    if ( pPad != nullptr ) {
        if ( bRumble ) {
            SDL_RumbleGamepad( pPad, 0, 0, 0 );
        }
        if ( bLED ) {
            SDL_SetGamepadLED( pPad, 0, 0, 0 );
        }
        if ( bTriggers && bTriggersSent ) {
            sendTriggers( kTriggerOff, kTriggerOff ); // never leave the triggers stiff
        }
    }
    detach();
    triggerFeel = TriggerFeel();
    roadFeel = 0.0f;
    pulsePeak = pulseStrength = 0.0f;
    pulseTimeLeft = pulseDuration = 0.0f;
    bWanted = false;
}

void GamepadOutput::update( float deltaTime ) {
    static const bool bOptOut = [] {
        const char *pValue = SDL_getenv( "OTDU_GAMEPAD_EXTRAS" );
        return CmdLine::HasArg( "-nods" ) || ( pValue != nullptr && pValue[0] == '0' );
    }();
    const bool bWasDisabled = bDisabled;
    bDisabled = bOptOut || GetGameOptions().DualSense == 0;
    if ( bDisabled ) {
        if ( !bWasDisabled && pPad != nullptr && bTriggers && bTriggersSent ) {
            sendTriggers( kTriggerOff, kTriggerOff ); // the option was just turned off
            bTriggersSent = false;
        }
        return;
    }
    attach();
    if ( pPad == nullptr ) {
        return;
    }
    if ( gpInput != nullptr && gpInput->getGamepad() != pPad ) {
        // The pad was unplugged (or replaced): drop the state, attach() picks
        // the new one up on the next frames.
        detach();
        return;
    }
    // Pulse decay.
    if ( pulseTimeLeft > 0.0f ) {
        pulseTimeLeft = Max( pulseTimeLeft - deltaTime, 0.0f );
        const float t = pulseDuration > 0.0f ? pulseTimeLeft / pulseDuration : 0.0f;
        pulseStrength = Saturate( t ) * pulsePeak;
    } else {
        pulseStrength = 0.0f;
    }
    applyRumble( deltaTime );
    applyLightBar( deltaTime );
    applyTriggers();
}

void GamepadOutput::applyTriggers() {
    const TriggerFeel &feel = triggerFeel;
    const uint8_t( &right )[11] =
        !feel.bActive ? kTriggerOff : ( feel.bWheelspin ? kThrottleSpin : kThrottleLight );
    const uint8_t( &left )[11] = !feel.bActive ? kTriggerOff : ( feel.bAbs ? kBrakeAbs : kBrakeFirm );
    const bool bChanged = !bTriggersSent || memcmp( right, lastRightTrigger, sizeof( lastRightTrigger ) ) != 0 ||
                          memcmp( left, lastLeftTrigger, sizeof( lastLeftTrigger ) ) != 0;
    if ( !bChanged || !bTriggers || pPad == nullptr ) {
        return;
    }
    sendTriggers( right, left );
}

void GamepadOutput::sendTriggers( const uint8_t ( &right )[11], const uint8_t ( &left )[11] ) {
    DualSenseEffects effects = {};
    effects.EnableBits1 = 0x04 | 0x08;
    memcpy( effects.RightTrigger, right, sizeof( effects.RightTrigger ) );
    memcpy( effects.LeftTrigger, left, sizeof( effects.LeftTrigger ) );
    if ( !SDL_SendGamepadEffect( pPad, &effects, sizeof( effects ) ) ) {
        // Not in enhanced mode (or not a DualSense after all): stop trying.
        OTDU_LOG_INFO( "Gamepad: adaptive triggers refused (%s)\n", SDL_GetError() );
        bTriggers = false;
        return;
    }
    memcpy( lastRightTrigger, right, sizeof( lastRightTrigger ) );
    memcpy( lastLeftTrigger, left, sizeof( lastLeftTrigger ) );
    bTriggersSent = true;
    static const bool bLog = CmdLine::HasArg( "-logtriggers" );
    if ( bLog ) {
        OTDU_LOG_INFO( "Gamepad: triggers R2 %02x %u %u  L2 %02x %u %u\n", right[0], right[1], right[2], left[0],
                       left[1], left[2] );
    }
}

void GamepadOutput::applyRumble( float deltaTime ) {
    if ( !bRumble || pPad == nullptr ) {
        return;
    }
    // Pulse and road feel combine: the stronger one wins, so a crash is felt
    // over the ground rumble instead of cancelling it.
    const float strength = Max( Saturate( roadFeel ), Saturate( pulseStrength ) );
    if ( strength < kRumbleFloor ) {
        if ( lastLow >= 0.0f ) {
            SDL_RumbleGamepad( pPad, 0, 0, 0 );
            lastLow = lastHigh = -1.0f;
        }
        return;
    }
    // Low motor: the body of the car; high motor: the surface texture.
    const float low = Saturate( strength * 0.75f );
    const float high = Saturate( strength * 0.45f + pulseStrength * 0.55f );
    rumbleTimer -= deltaTime;
    if ( rumbleTimer <= 0.0f || fabsf( low - lastLow ) > 0.02f || fabsf( high - lastHigh ) > 0.02f ) {
        rumbleTimer = kRumblePeriod;
        SDL_RumbleGamepad( pPad, static_cast<Uint16>( low * 0xffff ), static_cast<Uint16>( high * 0xffff ),
                           kRumbleDurationMs );
        lastLow = low;
        lastHigh = high;
    }
}

void GamepadOutput::applyLightBar( float deltaTime ) {
    if ( !bLED || pPad == nullptr ) {
        return;
    }
    uint8_t red = baseRed, green = baseGreen, blue = baseBlue;
    if ( bWanted ) {
        // Red / blue half period, the way a light bar reads on screen.
        ledClock += deltaTime;
        const float phase = std::fmod( ledClock, kWantedHalfPeriod * 2.0f );
        const uint8_t *pColor = phase < kWantedHalfPeriod ? kWantedBlue : kWantedRed;
        red = pColor[0];
        green = pColor[1];
        blue = pColor[2];
    } else {
        ledClock = 0.0f;
    }
    if ( bLedSet && red == lastRed && green == lastGreen && blue == lastBlue ) {
        return;
    }
    if ( !SDL_SetGamepadLED( pPad, red, green, blue ) ) {
        // The pad has no light bar (an Xbox pad): stop trying.
        bLED = false;
        const char *pName = SDL_GetGamepadName( pPad );
        OTDU_LOG_INFO( "Gamepad: '%s' has no light bar\n", pName != nullptr ? pName : "?" );
        return;
    }
    bLedSet = true;
    lastRed = red;
    lastGreen = green;
    lastBlue = blue;
}

void GamepadOutput::setWanted( bool wanted ) {
    bWanted = wanted;
}

void GamepadOutput::setBaseColor( uint8_t red, uint8_t green, uint8_t blue ) {
    baseRed = red;
    baseGreen = green;
    baseBlue = blue;
}

void GamepadOutput::pulse( float strength, float seconds ) {
    const float value = Saturate( strength );
    if ( value <= pulseStrength ) {
        return; // a stronger pulse is still running
    }
    pulsePeak = value;
    pulseStrength = value;
    pulseDuration = Max( seconds, 0.01f );
    pulseTimeLeft = pulseDuration;
    rumbleTimer = 0.0f; // apply on this frame
}

void GamepadOutput::setRoadFeel( float strength ) {
    roadFeel = Saturate( strength );
}

bool GamepadOutput::playSpeaker( const std::shared_ptr<AudioClip> &clip, float volume ) {
    // No backend: SDL3 exposes no controller audio output (see the header).
    // Logged once so a caller knows the request was seen and why it is a no-op.
    static bool bLogged = false;
    if ( !bLogged && clip != nullptr ) {
        bLogged = true;
        OTDU_LOG_INFO( "Gamepad: speaker playback of '%s' requested - SDL3 3.4 has no controller audio API, "
                       "the line stays on the main device (docs/re/ambience_audio_spec.md 4)\n",
                       clip->Name.c_str() );
    }
    (void)volume;
    return false;
}

