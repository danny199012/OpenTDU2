#pragma once

#include "flash/flash_player.h"

// The retail emote wheel (docs/re/emotes_spec.md section 5): the /emotes
// clip (character 715) of GENERAL.vmf - frontend/hires/general.bnk, the same
// movie the freeride HUD pops its goal / tutorial boxes from. The movie runs
// its own screens [DATA, GENERAL.vmf]:
//   - gotoAndPlay("open") calls EmotesOpen(): FSCommand 'emop' (the manager
//     parks the walk avatar: self view), 'wmof' (walk mode off), fills the
//     eight slots (/:m_Hello .. /:m_Laugh), plays the 'open' section;
//   - the slot buttons call EmoteRollOver() (/:AvanimNum = slot, FSCommand
//     'avan:seta') and EmotePreValid() (locked test, FSCommand 'sndp:valid'
//     / 'sndp:wrong'), whose 'valid' section ends with EmoteValid():
//     FSCommand 'avan:vali' + the 'close' section (EmotesClose(): 'wmon');
//   - Backspace (SWF key 8) calls B(): FSCommand 'emcl' + the 'close' section.
// The game side answers the FSCommands: 'avan:vali' plays slot /:AvanimNum,
// 'emcl' leaves the self view.
class EmoteWheel {
public:
    // Loads the movie (idempotent). False when general.bnk is not there.
    bool load();
    void unload();
    inline bool isLoaded() const { return player.isLoaded(); }

    // Opens the wheel (plays the 'open' section, rolls over slot 1).
    void open();
    // Closes it without a choice (the 'close' section plays out).
    void close();
    // True from open() until the 'close' section has played out.
    inline bool isOpen() const { return bOpen; }

    // Input (arrows / D-pad / left stick pick, Enter / A validates, Backspace
    // / B cancels) and the movie's own tick. Call every frame; a no-op while
    // closed.
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );

    // The slot (0..7) the movie validated ('avan:vali'), -1 when none; cleared
    // by the call.
    int32_t takeChosen();
    // True once when the player cancelled ('emcl'); cleared by the call.
    bool takeCancelled();

private:
    void select( int32_t slot );
    FlashPlayer player;
    bool bOpen = false;
    float closeTimer = 0.0f;
    int32_t selected = 0;
    int32_t chosen = -1;
    bool bCancelled = false;
    // The stick repeat (the pause menu's law).
    int32_t stickDirection = 0;
    float stickHold = 0.0f;
    float stickRepeat = 0.0f;
};
