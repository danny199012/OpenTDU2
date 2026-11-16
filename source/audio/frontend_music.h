#pragma once

#include "audio/gs_audio.h"

#include <memory>
#include <string>
#include <unordered_map>

// The frontend (place) music: retail's `SetMusic` table of
// Euro/bnk/frontend/sounds/fe_snd.bnk -> sounds.ini [DATA], one loose wav per
// place in `Euro/bnk/sound/musics/`:
//
//   SetMusic  "<place key>"  "<wav>"  1  0
//
// 113 entries: the car dealers (ECD_FERRI -> CarDealer.wav), the clothes /
// tuner / sticker shops, the clubs (ECH_IA -> ClubIbiza.wav), the realtor, the
// racing schools, the airport, the houses (EPH_L2E -> PlayerHouse02.wav), the
// map, the login screen and the lobby. The key is the spot's bank name; the
// shops carry a second entry with an `I` suffix for their interior (ECD_FERR ->
// ECD_FERRI) - docs/re/frontend_audio_spec.md 5.
//
// Played on the "FrontEndMusic" bfx client (8): audiosetup.xmb puts it on the
// "Music" mixer (with the radio), so the player's Music volume drives it, and
// the "Menu" preset has it at 0.2 against the world at 0 [DATA].
//
// One track at a time (retail's PlayFrontEndMusic), decoded on switch - the
// decoded float frames of a 1.4 MB wav are ~25 MB, so only the current track is
// kept in memory. playKey() with the current key is a no-op, so a caller may
// assert it every frame.
class FrontEndMusic {
public:
    FrontEndMusic();
    ~FrontEndMusic();
    FrontEndMusic( const FrontEndMusic & ) = delete;
    FrontEndMusic &operator=( const FrontEndMusic & ) = delete;

    // Parses the SetMusic table (the bank is read once, the wavs are not).
    bool load();
    void unload();
    inline bool isLoaded() const { return bLoaded; }
    inline int32_t getTrackCount() const {
        return static_cast<int32_t>( tracks.size() );
    }

    // The track of a place key, case insensitive. Same key: no-op. Unknown key
    // or a wav that does not exist: the current track fades out (logged once
    // per key). True when the key resolved to a playable wav.
    bool playKey( const std::string &key, float volumeScale = 1.0f );
    // A spot bank ("ECD_FERR"): the interior entry ("ECD_FERRI") when the table
    // has it, else the spot's own.
    bool playForSpot( const std::string &spotBank, float volumeScale = 1.0f );

    // Fades the current track out (update() finishes the stop).
    void stop();

    // The fade in / out (call every frame).
    void update( float deltaTime );

    inline bool isPlaying() const { return voice != GSAudio::kInvalidVoice; }
    inline const std::string &getKey() const { return currentKey; }
    inline float getGain() const { return gain; }

private:
    struct Track {
        std::string File;    // the wav of sounds.ini
        float Volume = 1.0f; // volume of the entry
    };

    bool parseMusicTable( const uint8_t *pData, size_t size );
    std::shared_ptr<AudioClip> loadTrack( const std::string &file );
    void stopVoice();

    std::unordered_map<std::string, Track> tracks; // lowercase key -> entry
    std::unordered_map<std::string, uint32_t> unknownKeys; // logged once each
    std::shared_ptr<AudioClip> pCurrentClip;
    GSAudio::VoiceHandle voice = GSAudio::kInvalidVoice;
    std::string currentKey;
    float gain = 0.0f;
    float targetGain = 0.0f;
    bool bLoaded = false;
    bool bLog = false;
};

extern FrontEndMusic *gpFrontEndMusic;
