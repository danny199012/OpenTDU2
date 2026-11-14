#include "shared.h"
#include "emote_wheel.h"

#include "flash/flash_localization.h"
#include "flash/gs_flash.h"
#include "game/game_options.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <string>

namespace {
constexpr int32_t kSlotCount = 8;
// The 'close' section is label close (frame 22) .. frame 32 of the 30 fps
// movie [DATA]; the wheel reports closed a little after it.
constexpr float kCloseTime = 0.5f;
} // namespace

bool EmoteWheel::load() {
    if ( player.isLoaded() ) {
        return true;
    }
    FlashMovie *pMovie = gpFlash != nullptr ? gpFlash->getMovie( "general" ) : nullptr;
    if ( pMovie == nullptr ) {
        OTDU_LOG_WARN( "EmoteWheel: GENERAL.vmf (frontend/hires/general.bnk) not available\n" );
        return false;
    }
    static const bool bLog = CmdLine::HasArg( "-logemotes" );
    player.setFSCommandCallback( [ this ]( const std::string &command, const std::string &args ) {
        if ( bLog ) {
            OTDU_LOG_INFO( "EMOTE: wheel FSCommand '%s' '%s'\n", command.c_str(), args.c_str() );
        }
        if ( command == "sbdd" ) {
            ApplyFlashLocalization( player );
        } else if ( command == "avan:vali" ) {
            const int32_t slot = std::atoi( player.getVariable( "/:AvanimNum" ).c_str() ) - 1;
            chosen = ( slot >= 0 && slot < kSlotCount ) ? slot : selected;
        } else if ( command == "avan:seta" ) {
            const int32_t slot = std::atoi( player.getVariable( "/:AvanimNum" ).c_str() ) - 1;
            if ( slot >= 0 && slot < kSlotCount ) {
                selected = slot;
            }
        } else if ( command == "emcl" ) {
            bCancelled = true;
        } else if ( command == "wmon" && closeTimer <= 0.0f ) {
            closeTimer = kCloseTime; // EmotesClose(): the 'close' section is playing
        }
    } );
    if ( !player.load( pMovie ) ) {
        return false;
    }
    ApplyFlashLocalization( player );
    // PC glyphs for the prompt sprites (the pause menu does the same).
    player.setVariableNumber( "/:WhatPlatform", 1.0 );
    return true;
}

void EmoteWheel::unload() {
    player.unload();
    bOpen = false;
}

void EmoteWheel::open() {
    if ( !load() ) {
        return;
    }
    bOpen = true;
    closeTimer = 0.0f;
    chosen = -1;
    bCancelled = false;
    selected = 0;
    player.setVariableNumber( "/:AvanimNum", 1.0 );
    player.gotoLabel( "/emotes", "open", true );
    static const bool bDump = CmdLine::HasArg( "-dumpemotes" );
    if ( bDump ) {
        player.dumpClips( "/emotes", 4 );
    }
}

void EmoteWheel::close() {
    if ( !bOpen || closeTimer > 0.0f ) {
        return;
    }
    player.gotoLabel( "/emotes", "close", true );
    closeTimer = kCloseTime;
}

void EmoteWheel::select( int32_t slot ) {
    slot = ( slot % kSlotCount + kSlotCount ) % kSlotCount;
    if ( slot == selected ) {
        return;
    }
    selected = slot;
    player.setVariable( "/:AvanimNum", std::to_string( slot + 1 ) );
    player.invoke( "/emotes", "EmoteRollOver", {} );
}

void EmoteWheel::tick( float deltaTime ) {
    if ( !bOpen ) {
        return;
    }
    player.tick( deltaTime );
    if ( closeTimer > 0.0f ) {
        closeTimer -= deltaTime;
        if ( closeTimer <= 0.0f ) {
            bOpen = false;
        }
        return;
    }
    // Slot choice: the ring reads left / right (and up / down) as previous /
    // next [INF: the retail pad wheel law is not decoded].
    int32_t step = 0;
    if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) || gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        step = -1;
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ||
                gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        step = 1;
    }
    const float x = gpInput->getLeftStickX();
    const float y = gpInput->getLeftStickY();
    int32_t direction = 0;
    if ( std::fabs( x ) > 0.5f || std::fabs( y ) > 0.5f ) {
        direction = ( std::fabs( x ) > std::fabs( y ) ) ? ( x < 0.0f ? -1 : 1 ) : ( y < 0.0f ? -1 : 1 );
    }
    if ( direction == 0 ) {
        stickDirection = 0;
        stickHold = stickRepeat = 0.0f;
    } else if ( direction != stickDirection ) {
        stickDirection = direction;
        stickHold = stickRepeat = 0.0f;
        step = direction;
    } else {
        stickHold += deltaTime;
        if ( stickHold >= 0.4f ) {
            stickRepeat += deltaTime;
            if ( stickRepeat >= 0.22f ) {
                stickRepeat = 0.0f;
                step = direction;
            }
        }
    }
    if ( step != 0 ) {
        select( selected + step );
    }
    // Keys 1..8 pick and validate a slot directly.
    for ( int32_t i = 0; i < kSlotCount; i++ ) {
        if ( gpInput->wasKeyPressed( static_cast<SDL_Scancode>( SDL_SCANCODE_1 + i ) ) ) {
            select( i );
            player.sendKey( 13 );
            return;
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        player.sendKey( 13 ); // button 714: Enter -> EmotePreValid()
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_Emote ) ) {
        player.sendKey( 8 ); // button 714: Backspace -> B() -> 'emcl'
    }
}

void EmoteWheel::draw( Renderer2D &renderer ) {
    if ( bOpen && player.isLoaded() ) {
        player.draw( renderer );
    }
}

int32_t EmoteWheel::takeChosen() {
    const int32_t value = chosen;
    chosen = -1;
    return value;
}

bool EmoteWheel::takeCancelled() {
    const bool value = bCancelled;
    bCancelled = false;
    return value;
}
