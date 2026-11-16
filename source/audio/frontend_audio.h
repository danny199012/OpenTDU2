#pragma once

#include "audio/gs_audio.h"

#include <memory>
#include <string>
#include <unordered_map>

class Bank;

// The frontend (menu) sounds: retail's edFlash "sndp:<cue>" UI sounds
// (FlashSounds.cpp). The cue names are registered by
// Euro/bnk/frontend/sounds/fe_snd.bnk -> sounds.ini [DATA]:
//
//   AddFESound  "<cue>"  "<wav>"  <volume>
//
// (189 cues over 178 wavs: mopen / mclose on menus, move / valid / wrong /
// back on the lists, ttip on the tooltips, the Med_* media player, the casino's
// UPMONEY / UPBET / HEARTB ..., see /tmp extract of sounds.ini).
//
// The flash movies ask for a cue with FSCommand("sndp:<cue>") — the movie-side
// name is the first column. The wav plays on the "FrontEnd" bfx client (7), a
// non-spatial 2-input pipeline: audiosetup.xmb maps that client to
// "FrontEndStereo" and the "Menu" mix preset keeps FrontEnd at gain 1 while it
// mutes the world (0), so the menus are heard over the frozen game [DATA].
//
// The bank is kept loaded (4.8 MB, MS-ADPCM 48 kHz stereo) and the wavs are
// decoded on first use: the mixer reads float frames, so decoding all 178 up
// front would cost ~40 MB for a handful of cues per menu.
class FrontEndAudio {
public:
    FrontEndAudio();
    ~FrontEndAudio();
    FrontEndAudio( const FrontEndAudio & ) = delete;
    FrontEndAudio &operator=( const FrontEndAudio & ) = delete;

    // Loads fe_snd.bnk and parses its sounds.ini cue map (no wav is decoded
    // yet). False without an audio device, the bank or the ini.
    bool load();
    void unload();
    inline bool isLoaded() const { return bLoaded; }
    inline int32_t getCueCount() const {
        return static_cast<int32_t>( cues.size() );
    }

    // Plays one cue (the name after "sndp:", case insensitive). volumeScale
    // multiplies the entry's own volume. A cue asked again within kMinInterval
    // is dropped: the movies fire some cues twice for one press (the options
    // tabs' 'open' runs on two frames). False for an unknown cue or a late
    // device.
    bool play( const std::string &cue, float volumeScale = 1.0f );

    // FSCommand("sndp:<cue>") of the flash player. Returns true when the
    // command was a sound cue (the menus' own handlers can then skip it).
    bool handleCommand( const std::string &command );

private:
    struct Cue {
        std::string File;                // the wav of sounds.ini
        float Volume = 1.0f;             // volume of the entry
        std::shared_ptr<AudioClip> Clip; // decoded on first play
        double LastPlay = -1.0e9;        // seconds (steady clock), throttle
    };

    bool parseSoundsIni( const uint8_t *pData, size_t size );
    std::shared_ptr<AudioClip> getClip( Cue &cue );

    std::unordered_map<std::string, Cue> cues; // lowercase cue -> entry
    std::unordered_map<std::string, std::shared_ptr<AudioClip>>
        clips; // lowercase wav -> clip
    std::unordered_map<std::string, uint32_t> unknownCues; // logged once each
    std::unique_ptr<Bank> pBank;
    bool bLoaded = false;
    bool bLog = false;
};

extern FrontEndAudio *gpFrontEndAudio;

// The native menus (pause, map, shops) drive their own entries instead of the
// movie's scripts, so the movie never fires their "sndp:" cue: they call this
// directly. No-op without the audio device / the bank.
void PlayFrontEndCue( const char *pCue, float volumeScale = 1.0f );
