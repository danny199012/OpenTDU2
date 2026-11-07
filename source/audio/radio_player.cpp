#include "shared.h"
#include "radio_player.h"
#include "core/path_utf8.h"

#include "core/xmb.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>

namespace {
// Euro/Bnk/sound/radio/music: one bank per song.
constexpr const char *kRadioMusicFolder = "Euro/Bnk/sound/radio/music";
constexpr const char *kRadioSetup = "Euro/Bnk/sound/radio/setup.xmb";
// bfx client index of the "Radio" pipeline (AudioSetup::GetBfxClientName 9:
// VoiceStoryline..FrontEndMusic then Radio).
constexpr int32_t kRadioClient = 9;

// The retail stations of the HUD movie (the /HUD/radio/radio_logo labels).
struct RetailStation {
    const char *pName;
    const char *pLogoLabel;
};
constexpr RetailStation kRetailStations[] = {
    { "Hariba Radio", "hariba" },
    { "Kalua", "kalua" },
    { "Roadtrack", "roadtrack" },
};
constexpr int32_t kRetailStationCount = static_cast<int32_t>( std::size( kRetailStations ) );

// The custom stations folder inside the user's Music folder.
constexpr const char *kStationsFolderName = "OpenTDU2";

// A station folder's art: the first of these stems, with one of the image
// extensions. The songs are the audio extensions (".bnk" is a retail song
// bank, a player may drop one in as well).
constexpr const char *kIconStems[] = { "logo", "station", "icon", "cover" };
constexpr const char *kIconExtensions[] = { ".png", ".dds", ".jpg", ".jpeg", ".bmp", ".tga" };
// The song extensions a station folder is scanned for. The decoders sniff the
// content, so an ".mp3" that is really an Ogg Opus (or an ".m4a", which has no
// decoder here) is handled / reported by its data, not by its name.
constexpr const char *kSongExtensions[] = { ".mp3", ".ogg", ".oga", ".opus", ".flac", ".wav", ".m4a", ".aac", ".wma",
                                            ".bnk" };

bool HasExtension( const std::filesystem::path &path, const char *const *ppExtensions, size_t count ) {
    const std::string extension = ToLower( PathToUtf8( path.extension() ) );
    for ( size_t i = 0; i < count; i++ ) {
        if ( extension == ppExtensions[i] ) {
            return true;
        }
    }
    return false;
}

// Case insensitive order of file / folder names (the retail data mixes cases).
bool NameLess( const std::filesystem::path &left, const std::filesystem::path &right ) {
    return StringCmpI( PathToUtf8( left.filename() ).c_str(), PathToUtf8( right.filename() ).c_str() ) < 0;
}

// "Artist - Title" out of a file stem (the rule of the retail bank names).
void SplitName( const std::string &stem, RadioPlayer::Track &out ) {
    out.Name = stem;
    const size_t dash = stem.find( " - " );
    out.Artist = dash != std::string::npos ? stem.substr( 0, dash ) : stem;
    out.Title = dash != std::string::npos ? stem.substr( dash + 3 ) : std::string();
}

// The art of a station folder: logo.* first, then station.* / icon.* / cover.*.
std::filesystem::path FindStationIcon( const std::vector<std::filesystem::path> &icons ) {
    for ( const char *pStem : kIconStems ) {
        for ( const std::filesystem::path &icon : icons ) {
            if ( ToLower( PathToUtf8( icon.stem() ) ) == pStem ) {
                return icon;
            }
        }
    }
    return std::filesystem::path();
}

// The note the game leaves in a stations folder it created itself.
void WriteStationsReadme( const std::filesystem::path &folder ) {
    std::ofstream stream( folder / "readme.txt", std::ios::binary | std::ios::trunc );
    if ( !stream.is_open() ) {
        return;
    }
    stream << "OpenTDU2 car radio stations\n"
              "===========================\n"
              "\n"
              "Every folder in here is one radio station:\n"
              "\n"
              "  OpenTDU2/\n"
              "    Hardstyle fm/          <- the station name (the radio panel's first line)\n"
              "      logo.png             <- the station art, optional (.png / .jpg / .dds)\n"
              "      Song.mp3             <- the songs: mp3 / ogg / opus / flac / wav / bnk\n"
              "      Artist - Title.mp3   <- an \"Artist - Title\" name fills the panel's lines\n"
              "    Another station/\n"
              "      ...\n"
              "\n"
              "The songs' format is read from the file itself, not from its extension (a\n"
              "downloaded \".mp3\" is often an Ogg Opus).\n"
              "\n"
              "The retail stations (Hariba Radio, Kalua, Roadtrack) stay the first three;\n"
              "the folders here follow them in the station cycle (D-pad right in the car,\n"
              "or the RADIO_CHANNEL key).\n"
              "\n"
              "Launch options: '-radio <folder>' points the game at another folder,\n"
              "'-noradiofolders' disables the folder stations.\n";
}

// How many seconds before the song ends the next one starts decoding in the
// background (the decode itself takes ~0.15 s warm; the lead absorbs it).
constexpr float kPreloadLeadSeconds = 20.0f;
} // namespace

const RadioPlayer::Station RadioPlayer::kEmptyStation;

void RadioPlayer::loadSetupNames( std::vector<Track> &tracks ) {
    std::vector<uint8_t> data;
    if ( gpFile == nullptr || !gpFile->loadFile( kRadioSetup, data ) ) {
        return;
    }
    XmbDocument setup;
    if ( !setup.load( data.data(), data.size() ) ) {
        return;
    }
    // ProjectExport { TrackExport[] Tracks { f32 Duration; f32[] Markers;
    // f32 LoopStart; f32 LoopEnd; string Name; u32 Hashcode } }.
    std::vector<std::string> names;
    XmbValue root = setup.getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "Tracks" ); i++ ) {
        std::string name = root.getArrayElement( "Tracks", i )["Name"].asString();
        const size_t dot = name.find_last_of( '.' );
        if ( dot != std::string::npos ) {
            name.resize( dot );
        }
        names.push_back( std::move( name ) );
    }
    // Match the banks (their file names are the lowercased setup names).
    uint32_t numMatched = 0;
    for ( Track &track : tracks ) {
        const size_t slash = track.Bank.find_last_of( "/\\" );
        std::string stem = slash != std::string::npos ? track.Bank.substr( slash + 1 ) : track.Bank;
        const size_t dot = stem.find_last_of( '.' );
        if ( dot != std::string::npos ) {
            stem.resize( dot );
        }
        for ( const std::string &name : names ) {
            if ( ToLower( name ) == stem ) {
                track.Name = name;
                numMatched++;
                break;
            }
        }
    }
    OTDU_LOG_INFO( "Radio: %u of %zu setup.xmb names matched\n", numMatched, tracks.size() );
}

void RadioPlayer::loadRetailStations( const std::vector<Track> &tracks ) {
    for ( int32_t i = 0; i < kRetailStationCount; i++ ) {
        Station stationEntry;
        stationEntry.Name = kRetailStations[i].pName;
        stationEntry.LogoLabel = kRetailStations[i].pLogoLabel;
        // Round robin [INF]: setup.xmb is one flat list, so the retail split is
        // not recoverable; dealing the songs keeps every station's list its own
        // (a station change changes the song) and the union complete.
        for ( size_t t = static_cast<size_t>( i ); t < tracks.size(); t += kRetailStationCount ) {
            stationEntry.Tracks.push_back( tracks[t] );
        }
        OTDU_LOG_INFO( "Radio: station %d '%s': %zu song(s), logo '%s'\n", i + 1, stationEntry.Name.c_str(),
                       stationEntry.Tracks.size(), stationEntry.LogoLabel.c_str() );
        stations.push_back( std::move( stationEntry ) );
    }
}

std::filesystem::path RadioPlayer::findStationsFolder() {
    if ( CmdLine::HasArg( "-noradiofolders" ) ) {
        OTDU_LOG_INFO( "Radio: folder stations disabled (-noradiofolders)\n" );
        return std::filesystem::path();
    }
    std::error_code ec;
    const char *pArg = CmdLine::GetArgValue( "-radio" );
    const char *pEnv = SDL_getenv( "OTDU2_RADIO_DIR" );
    std::filesystem::path folder;
    if ( pArg != nullptr ) {
        folder = std::filesystem::absolute( pArg, ec );
    } else if ( pEnv != nullptr ) {
        folder = std::filesystem::absolute( pEnv, ec );
    } else {
        // The user's Music folder (SDL_GetUserFolder, "C:\Users\<user>\Music"):
        // the player drops stations in there, not in the save folder.
        const char *pMusic = SDL_GetUserFolder( SDL_FOLDER_MUSIC );
        if ( pMusic != nullptr && pMusic[0] != '\0' ) {
            folder = std::filesystem::path( pMusic ) / kStationsFolderName;
        } else if ( gpFile != nullptr ) {
            // No Music folder on this platform: next to the saves, so the game
            // still owns a folder for them.
            folder = gpFile->getUserDataFolder() / "radio";
        }
    }
    if ( folder.empty() ) {
        return folder;
    }
    const bool bExisted = std::filesystem::is_directory( folder, ec );
    ec.clear();
    std::filesystem::create_directories( folder, ec );
    if ( !std::filesystem::is_directory( folder, ec ) ) {
        OTDU_LOG_WARN( "Radio: station folder '%s' is unusable, custom stations disabled\n", PathToUtf8( folder ).c_str() );
        return std::filesystem::path();
    }
    if ( !bExisted ) {
        OTDU_LOG_INFO( "Radio: created the station folder '%s'\n", PathToUtf8( folder ).c_str() );
    }
    // The layout note, so the folder explains itself (deleting it brings it
    // back on the next run).
    ec.clear();
    if ( !std::filesystem::exists( folder / "readme.txt", ec ) ) {
        WriteStationsReadme( folder );
        OTDU_LOG_INFO( "Radio: wrote '%s'\n", PathToUtf8( folder / "readme.txt" ).c_str() );
    }
    return folder;
}

void RadioPlayer::loadCustomStations() {
    stationsFolder = findStationsFolder();
    if ( stationsFolder.empty() ) {
        return;
    }
    std::error_code ec;
    std::vector<std::filesystem::path> folders;
    for ( const auto &entry : std::filesystem::directory_iterator( stationsFolder, ec ) ) {
        if ( entry.is_directory( ec ) ) {
            folders.push_back( entry.path() );
        }
    }
    if ( ec ) {
        OTDU_LOG_WARN( "Radio: cannot list '%s'\n", PathToUtf8( stationsFolder ).c_str() );
    }
    std::sort( folders.begin(), folders.end(), NameLess );
    int32_t numCustom = 0;
    for ( const std::filesystem::path &folder : folders ) {
        // One station never stops the game: a name the filesystem cannot convert
        // (or a folder that vanishes mid-scan) skips that station only.
        try {
            Station custom;
            custom.Name = PathToDisplay( folder.filename() );
            std::vector<std::filesystem::path> songs;
            std::vector<std::filesystem::path> icons;
            std::error_code entryEc;
            for ( const auto &entry : std::filesystem::directory_iterator( folder, entryEc ) ) {
                if ( !entry.is_regular_file( entryEc ) ) {
                    continue;
                }
                const std::filesystem::path &path = entry.path();
                if ( HasExtension( path, kSongExtensions, std::size( kSongExtensions ) ) ) {
                    songs.push_back( path );
                } else if ( HasExtension( path, kIconExtensions, std::size( kIconExtensions ) ) ) {
                    icons.push_back( path );
                }
            }
            std::sort( songs.begin(), songs.end(), NameLess );
            for ( const std::filesystem::path &song : songs ) {
                Track track;
                // The path stays UTF-8 (opened through PathFromUtf8), the names shown
                // on the HUD are Windows-1252 like the retail strings.
                track.File = PathToUtf8( song );
                SplitName( PathToDisplay( song.stem() ), track );
                custom.Tracks.push_back( std::move( track ) );
            }
            const std::filesystem::path icon = FindStationIcon( icons );
            custom.IconPath = PathToUtf8( icon );
            if ( custom.Tracks.empty() ) {
                OTDU_LOG_WARN( "Radio: station folder '%s' has no song, skipped\n", PathToUtf8( folder ).c_str() );
                continue;
            }
            const std::string iconNote = icon.empty() ? std::string() : ", icon '" + PathToUtf8( icon.filename() ) + "'";
            OTDU_LOG_INFO( "Radio: station %zu '%s': %zu song(s)%s\n", stations.size() + 1, custom.Name.c_str(),
                           custom.Tracks.size(), iconNote.c_str() );
            numCustom++;
            stations.push_back( std::move( custom ) );
        } catch ( const std::exception &exception ) {
            OTDU_LOG_WARN( "Radio: station folder '%s' skipped (%s)\n", PathToUtf8( folder ).c_str(),
                           exception.what() );
        }
    }
    OTDU_LOG_INFO( "Radio: %d custom station(s) in '%s'\n", numCustom, PathToUtf8( stationsFolder ).c_str() );
}

bool RadioPlayer::load() {
    unload();
    if ( gpFile == nullptr ) {
        return false;
    }
    // Retail songs: one bank per song (listFolder matches the extension with
    // its dot, the mods/ overlay included).
    std::vector<std::filesystem::path> files = gpFile->listFolder( kRadioMusicFolder, ".bnk" );
    std::sort( files.begin(), files.end() );
    std::vector<Track> retailTracks;
    for ( const std::filesystem::path &file : files ) {
        Track track;
        track.Bank = kRadioMusicFolder + std::string( "/" ) + PathToUtf8( file.filename() );
        SplitName( PathToDisplay( file.stem() ), track );
        retailTracks.push_back( std::move( track ) );
    }
    loadSetupNames( retailTracks );
    // Artist / title of the setup.xmb spelling (the file name is the fallback).
    for ( Track &track : retailTracks ) {
        if ( track.Name.empty() ) {
            continue;
        }
        const size_t dash = track.Name.find( " - " );
        if ( dash != std::string::npos ) {
            track.Artist = track.Name.substr( 0, dash );
            track.Title = track.Name.substr( dash + 3 );
        } else {
            track.Artist = track.Name;
            track.Title = std::string();
        }
    }
    OTDU_LOG_INFO( "Radio: %zu retail songs in '%s'\n", retailTracks.size(), kRadioMusicFolder );
    loadRetailStations( retailTracks );
    loadCustomStations();
    if ( stations.empty() ) {
        OTDU_LOG_WARN( "Radio: no station at all (no song bank, no station folder)\n" );
        return false;
    }
    station = 0;
    current = -1;
    OTDU_LOG_INFO( "Radio: %zu station(s)\n", stations.size() );
    return true;
}

void RadioPlayer::unload() {
    stop();
    stations.clear();
    stationsFolder.clear();
    station = 0;
    current = -1;
    {
        std::lock_guard<std::mutex> lock( preloadMutex );
        preloadClip.reset();
        preloadStation = -1;
        preloadIndex = -1;
    }
}

bool RadioPlayer::isPlaying() const {
    return gpAudio != nullptr && voice != GSAudio::kInvalidVoice && gpAudio->isPlaying( voice );
}

bool RadioPlayer::play( int32_t index ) {
    if ( !isLoaded() || gpAudio == nullptr || !gpAudio->isReady() ) {
        return false;
    }
    const Station &info = getStationInfo();
    const size_t count = info.Tracks.size();
    if ( count == 0 ) {
        OTDU_LOG_WARN( "Radio: station '%s' has no song\n", info.Name.c_str() );
        stop();
        return false;
    }
    if ( index < 0 ) {
        index = current >= 0 ? current : 0;
    }
    const int32_t wanted = ( ( index % static_cast<int32_t>( count ) ) + static_cast<int32_t>( count ) ) %
                           static_cast<int32_t>( count );
    stop();
    const Track &track = info.Tracks[wanted];
    // The preloaded clip of this track (the background decode that ran while
    // the previous song played): when it is there, the song change costs
    // nothing on the main thread.
    std::shared_ptr<AudioClip> preloaded;
    takePreload( station, wanted, preloaded );
    std::shared_ptr<AudioClip> newClip = preloaded;
    if ( newClip != nullptr ) {
        OTDU_LOG_INFO( "Radio: '%s' was preloaded (no decode stall)\n", track.Title.c_str() );
    } else if ( !track.Bank.empty() ) {
        // The song is decoded from its retail bank (the bank buffer is
        // released right after: the decoded samples belong to the clip).
        Bank bank;
        if ( !bank.load( track.Bank.c_str() ) ) {
            OTDU_LOG_WARN( "Radio: '%s' not found\n", track.Bank.c_str() );
            return false;
        }
        const std::vector<const BankEntry *> samples = bank.findEntriesByExtension( "wav" );
        if ( samples.empty() ) {
            OTDU_LOG_WARN( "Radio: no wav in '%s'\n", track.Bank.c_str() );
            return false;
        }
        newClip = gpAudio->loadClip( samples.front()->pData, samples.front()->Size, track.Title.c_str() );
        bank.unload();
    } else {
        // A custom station's file (mp3 / ogg / ... - the decoder sniffs it).
        newClip = gpAudio->loadClipFile( PathFromUtf8( track.File ), track.Title.c_str() );
    }
    if ( newClip == nullptr ) {
        OTDU_LOG_WARN( "Radio: '%s' not decoded\n", ( track.Bank.empty() ? track.File : track.Bank ).c_str() );
        return false;
    }
    clip = newClip;
    GSAudio::VoiceParams params;
    params.Client = kRadioClient; // Radio
    params.Volume = volume;
    params.bLoop = false;
    // Test hook (-radioskip <s>): start the song 's' in, so the song-end
    // advance (the next-track decode) fires within seconds of the run — the
    // stall capture for the "random unresponsiveness" report. A value at or
    // past the end means "3 s before the end".
    if ( const char *pSkip = CmdLine::GetArgValue( "-radioskip" ) ) {
        const float skip = static_cast<float>( atof( pSkip ) );
        elapsed = skip >= clip->Duration ? Max( 0.0f, clip->Duration - 3.0f ) : Max( 0.0f, skip );
        if ( elapsed > 0.0f ) {
            params.StartTime = elapsed;
            OTDU_LOG_INFO( "Radio: -radioskip: starting at %.1f s of %.1f\n", elapsed, clip->Duration );
        }
    } else {
        elapsed = 0.0f;
    }
    voice = gpAudio->play( clip, params );
    current = wanted;
    stations[station].Current = wanted;
    OTDU_LOG_INFO( "Radio: station '%s': playing %s (voice %u, %d of %zu, %.1f s)\n", info.Name.c_str(),
                   getCurrentName().c_str(), voice, wanted + 1, count, clip->Duration );
    // (elapsed was set above: the -radioskip seek, or 0.)
    return true;
}


bool RadioPlayer::next() { return play( ( current < 0 ? 0 : current + 1 ) ); }

bool RadioPlayer::previous() { return play( ( current < 0 ? 0 : current - 1 ) ); }

bool RadioPlayer::setStation( int32_t index ) {
    if ( stations.empty() ) {
        return false;
    }
    const int32_t count = static_cast<int32_t>( stations.size() );
    station = ( ( index % count ) + count ) % count;
    current = -1;
    const Station &info = stations[station];
    if ( info.Tracks.empty() ) {
        OTDU_LOG_WARN( "Radio: station '%s' has no song\n", info.Name.c_str() );
        stop();
        return false;
    }
    // A station change starts that station's own song: retail's panel swaps the
    // logo, the name and the song together.
    return play( info.Current >= 0 ? info.Current : 0 );
}

bool RadioPlayer::nextStation() { return setStation( station + 1 ); }

void RadioPlayer::stop() {
    joinPreload();
    if ( gpAudio != nullptr && voice != GSAudio::kInvalidVoice ) {
        gpAudio->stop( voice );
    }
    voice = GSAudio::kInvalidVoice;
    clip.reset();
}

void RadioPlayer::setVolume( float newVolume ) {
    volume = Clamp( newVolume, 0.0f, 1.0f );
    if ( gpAudio != nullptr && voice != GSAudio::kInvalidVoice ) {
        gpAudio->setVolume( voice, volume );
    }
}

void RadioPlayer::preloadTick() {
    if ( !isLoaded() || gpAudio == nullptr || !gpAudio->isReady() || current < 0 || clip == nullptr || stations.empty() ) {
        return;
    }
    const Station &info = stations[station];
    if ( info.Tracks.empty() ) {
        return;
    }
    const int32_t nextIndex = ( current + 1 ) % static_cast<int32_t>( info.Tracks.size() );
    {
        std::lock_guard<std::mutex> lock( preloadMutex );
        if ( bPreloadBusy || ( preloadStation == station && preloadIndex == nextIndex ) ) {
            return; // already decoded / decoding
        }
        if ( clip->Duration - elapsed > kPreloadLeadSeconds ) {
            return; // not yet time
        }
        bPreloadBusy = true;
    }
    const Track &track = info.Tracks[nextIndex];
    // A retail song bank: the bank bytes are read here (gpFile is main-thread
    // only) and the worker only decodes (SDL wav decode + F32 conversion, no
    // shared state). A custom station file: the worker reads and decodes it
    // (SDL file IO only, no gpFile).
    const int32_t trackStation = station;
    if ( !track.Bank.empty() ) {
        auto pBank = std::make_shared<Bank>();
        if ( !pBank->load( track.Bank.c_str() ) ) {
            std::lock_guard<std::mutex> lock( preloadMutex );
            bPreloadBusy = false;
            return;
        }
        std::vector<const BankEntry *> samples = pBank->findEntriesByExtension( "wav" );
        if ( samples.empty() ) {
            std::lock_guard<std::mutex> lock( preloadMutex );
            bPreloadBusy = false;
            return;
        }
        const BankEntry *pWav = samples.front();
        const std::string title = track.Title;
        preloadThread = std::thread( [this, pBank, pWav, title, trackStation, nextIndex]() {
            std::shared_ptr<AudioClip> decoded = gpAudio->loadClip( pWav->pData, pWav->Size, title.c_str() );
            std::lock_guard<std::mutex> lock( preloadMutex );
            preloadClip = decoded;
            preloadStation = trackStation;
            preloadIndex = nextIndex;
            bPreloadBusy = false;
        } );
    } else {
        const std::string file = track.File;
        const std::string title = track.Title;
        preloadThread = std::thread( [this, file, title, trackStation, nextIndex]() {
            std::shared_ptr<AudioClip> decoded = gpAudio->loadClipFile( PathFromUtf8( file ), title.c_str() );
            std::lock_guard<std::mutex> lock( preloadMutex );
            preloadClip = decoded;
            preloadStation = trackStation;
            preloadIndex = nextIndex;
            bPreloadBusy = false;
        } );
    }
}

void RadioPlayer::joinPreload() {
    if ( preloadThread.joinable() ) {
        preloadThread.join();
    }
}

bool RadioPlayer::takePreload( int32_t stationIndex, int32_t index, std::shared_ptr<AudioClip> &outClip ) {
    std::lock_guard<std::mutex> lock( preloadMutex );
    outClip = nullptr;
    if ( preloadStation != stationIndex || preloadIndex != index || bPreloadBusy ) {
        return false;
    }
    outClip = preloadClip;
    preloadClip.reset();
    preloadStation = -1;
    preloadIndex = -1;
    return outClip != nullptr;
}

void RadioPlayer::update( float deltaTime ) {
    elapsed += deltaTime;
    preloadTick();
    // Song ended: the next one of the same station (the retail radio keeps
    // playing).
    if ( current >= 0 && voice != GSAudio::kInvalidVoice && gpAudio != nullptr && !gpAudio->isPlaying( voice ) ) {
        OTDU_LOG_INFO( "Radio: '%s' ended after %.2f s\n", getCurrentName().c_str(), elapsed );
        next();
    }
}

std::string RadioPlayer::getCurrentName() const {
    const Station &info = getStationInfo();
    if ( current < 0 || static_cast<size_t>( current ) >= info.Tracks.size() ) {
        return std::string();
    }
    const Track &track = info.Tracks[current];
    return track.Title.empty() ? track.Artist : track.Artist + " - " + track.Title;
}

std::string RadioPlayer::getCurrentArtist() const {
    const Station &info = getStationInfo();
    return current >= 0 && static_cast<size_t>( current ) < info.Tracks.size() ? info.Tracks[current].Artist
                                                                               : std::string();
}

std::string RadioPlayer::getCurrentTitle() const {
    const Station &info = getStationInfo();
    return current >= 0 && static_cast<size_t>( current ) < info.Tracks.size() ? info.Tracks[current].Title
                                                                               : std::string();
}
