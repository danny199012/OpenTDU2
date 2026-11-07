#pragma once

#include "audio/gs_audio.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Bank;

// Car radio (sound/radio): the retail songs are one bank per song in
// Euro/Bnk/sound/radio/music ("Phoenix - 1901.bnk" - the file name carries the
// artist and the title), decoded on demand and played through the "Radio"
// client of audiosetup.xmb (bfx index 9, AudioSetup::GetBfxClientName) so it
// follows the game mode mix preset and ducks with the engine like retail.
//
// Artist / title in their original spelling come from sound/radio/setup.xmb
// (ProjectExport/Tracks: "Dmitry Fyodrov - Wolf Brigade.wav", duration and
// markers per track; the bank file names are the lowercased versions) —
// matched case-insensitively, the file name is the fallback.
//
// Stations. The three retail stations are the logo labels of the HUD movie's
// /HUD/radio/radio_logo sprite ('hariba' / 'kalua' / 'roadtrack', one logo per
// label): retail shows the station name on the first panel line
// ("▶ Hariba Radio"), the artist and the title below. setup.xmb is one flat
// list (the per-station split of retail is not in the readable data), so the
// retail songs are dealt round robin over the three stations [INF] — every
// station plays its own list, and changing station changes the song.
//
// Custom stations. Every subfolder of the stations folder is a station: the
// folder name is the station name, its music files are its songs
// (.mp3 / .ogg / .opus / .flac / .wav / .bnk — the decoders sniff the content,
// a downloaded ".mp3" is very often an Ogg Opus) named "Artist - Title.ext",
// the rule of the banks, and its logo.* / station.* / icon.* file
// (.png / .jpg / .dds) is its art, drawn by the HUD in place of the movie's
// logo sprite. The folder defaults to "<user Music>/OpenTDU2" (created at load,
// with a readme.txt explaining the layout); '-radio <dir>' / OTDU2_RADIO_DIR
// overrides it and '-noradiofolders' keeps the retail stations only. Custom
// stations are appended after the retail three, sorted by name.
class RadioPlayer {
public:
    struct Track {
        std::string Bank;   // retail: "Euro/Bnk/sound/radio/music/<song>.bnk"
        std::string File;   // custom station: the song file (Bank stays empty)
        std::string Name;   // "Phoenix - 1901" (setup.xmb spelling)
        std::string Artist; // "Phoenix"
        std::string Title;  // "1901"
    };
    struct Station {
        std::string Name;      // panel line 1 ("Hariba Radio", the folder name)
        std::string LogoLabel; // radio_logo label of the movie ("" = none)
        std::string IconPath;  // custom art file ("" = none)
        std::vector<Track> Tracks;
        int32_t Current = -1; // index of the station's own song (-1: none yet)
    };

    RadioPlayer() = default;
    ~RadioPlayer() { unload(); }

    // Enumerates the retail song banks (case insensitive, the mods/ overlay
    // included) and the custom station folders (creating the folder).
    bool load();
    void unload();

    inline bool isLoaded() const { return !stations.empty(); }
    inline size_t getNumStations() const { return stations.size(); }
    inline size_t getNumTracks() const { return getStationInfo().Tracks.size(); }
    inline int32_t getCurrentTrack() const { return current; }
    inline float getVolume() const { return volume; }
    inline int32_t getStation() const { return station; }
    inline const Station &getStationInfo() const { return stations.empty() ? kEmptyStation : stations[station]; }
    bool isPlaying() const;

    // The folder the custom stations are read from (empty when disabled).
    inline const std::filesystem::path &getStationsFolder() const { return stationsFolder; }

    // Starts a track of the current station (index < 0: its current, or the
    // first one).
    bool play( int32_t index = -1 );
    bool next();
    bool previous();
    // Switches station (wrapping) and starts its current song: a station
    // change is a song change, like retail.
    bool nextStation();
    bool setStation( int32_t index );
    void stop();
    void setVolume( float newVolume );

    // Starts the next song when the current one ended (call every tick).
    void update( float deltaTime );

    // "Artist - Title" of the current track ("" when none).
    std::string getCurrentName() const;
    std::string getCurrentArtist() const;
    std::string getCurrentTitle() const;

private:
    // sound/radio/setup.xmb: the original spellings of the track names.
    void loadSetupNames( std::vector<Track> &tracks );
    // The retail stations, from the song banks (round robin [INF]).
    void loadRetailStations( const std::vector<Track> &tracks );
    // The custom stations: the subfolders of the stations folder.
    void loadCustomStations();
    // Reads (and creates) the stations folder; empty when disabled.
    std::filesystem::path findStationsFolder();

    static const Station kEmptyStation;

    std::vector<Station> stations;
    std::filesystem::path stationsFolder;

    // The next song decoded in the background while the current one plays:
    // the synchronous decode at the song end stalled the frame that advanced
    // the playlist (hundreds of ms on a warm cache, seconds cold - session
    // 46's "random unresponsiveness after driving for a while"). The bank
    // bytes are read on the main thread (gpFile is not thread safe); the
    // worker only runs the SDL wav decode / F32 conversion.
    void preloadTick();
    void joinPreload();
    bool takePreload( int32_t stationIndex, int32_t index, std::shared_ptr<AudioClip> &outClip );

    std::thread preloadThread;
    std::mutex preloadMutex;
    std::shared_ptr<AudioClip> preloadClip;
    int32_t preloadStation = -1; // the station the worker is decoding for
    int32_t preloadIndex = -1;   // the track index within that station
    bool bPreloadBusy = false;

    int32_t station = 0;
    int32_t current = -1; // index in stations[station].Tracks
    std::shared_ptr<AudioClip> clip;
    GSAudio::VoiceHandle voice = GSAudio::kInvalidVoice;
    float volume = 0.8f;
    float elapsed = 0.0f; // s played of the current song
};
