#pragma once

#include "audio/gs_audio.h"

#include <array>
#include <memory>

// The sat-nav voice: retail GPSVoice (FlashSounds.cpp; the PlaySound assert
// string at 0xee6cf8). It loads "%sSound/Voices/%s/GPS.bnk" (0xee6d5c) and
// plays one of its 17 samples by index on the "VoiceGPS" client of
// audiosetup.xmb (bfx index 5, AudioSetup::GetBfxClientName) [CODE]. The bank
// holds VFGPSVoice001..017, MS-ADPCM 44.1 kHz mono [DATA]. The sample index is
// the cue: the samples carry no names, the lines below are a transcription of
// the US bank (docs/re/gps_voice_spec.md).
//
// GPSVoice::PlaySound (FUN_00d84950): a cue asked for while the previous one
// is still playing is dropped and logged ("[GPS] Can't play GPS sound"); the
// volume is 1 (0xedf668), the player's Voices volume applies through the
// mixer [CODE].
class GPSVoice {
public:
    enum eCue : int32_t {
        GPS_WrongWay = 0,  // "You are driving in the wrong direction. Turn
                           // around when possible."
        GPS_NextRight = 1, // "At the next intersection, turn right."
        GPS_Right = 2,     // "Turn right."
        GPS_NextLeft = 3,  // "At the next intersection, turn left."
        GPS_Left = 4,      // "Turn left."
        GPS_Destination500m = 5,      // "Your destination is 500 meters away."
                                      // (6..9: 1 / 1.5 / 2 / 3 km)
        GPS_DestinationHalfMile = 10, // "Your destination is half a mile away."
                                      // (11..14: 1 / 1.5 / 2 / 3 miles)
        GPS_Arrived = 15,             // "You have reached your destination."
        GPS_Recalculating = 16,       // "Recalculating."
        GPS_CueCount = 17
    };

    GPSVoice() = default;
    ~GPSVoice() { unload(); }

    // Loads sound/voices/<language>/gps.bnk (gpConfig language, fallback "us").
    bool load();
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    // Returns false when the cue was dropped (not loaded, or a cue is playing).
    bool play( int32_t cue );
    bool isPlaying() const;

private:
    std::array<std::shared_ptr<AudioClip>, GPS_CueCount> clips;
    GSAudio::VoiceHandle voice = GSAudio::kInvalidVoice;
    bool bLoaded = false;
};
