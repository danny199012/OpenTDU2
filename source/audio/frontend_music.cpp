#include "shared.h"
#include "audio/frontend_music.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

namespace {
    constexpr const char *kFrontendBank = "frontend/sounds/fe_snd.bnk";
    constexpr const char *kSoundsIni = "sounds.ini";
    constexpr const char *kMusicFolder = "sound/musics/";
    // "FrontEndMusic" in the bfx client table (AudioSetup::GetBfxClientName 8).
    constexpr int32_t kFrontEndMusicClient = 8;
    // The fades, in gain per second (the tracks are minutes long, so a slow
    // start reads as retail's "music fades in over the room").
    constexpr float kFadeIn = 0.5f;
    constexpr float kFadeOut = 1.2f;

    std::string Trim( const std::string &value ) {
        const size_t begin = value.find_first_not_of( " \t\r\n" );
        if ( begin == std::string::npos ) {
            return std::string();
        }
        const size_t end = value.find_last_not_of( " \t\r\n" );
        return value.substr( begin, end - begin + 1 );
    }
} // namespace

FrontEndMusic *gpFrontEndMusic = nullptr;

FrontEndMusic::FrontEndMusic() { gpFrontEndMusic = this; }

FrontEndMusic::~FrontEndMusic() {
    unload();
    if ( gpFrontEndMusic == this ) {
        gpFrontEndMusic = nullptr;
    }
}

bool FrontEndMusic::load() {
    unload();
    if ( gpAudio == nullptr || !gpAudio->isReady() || gpFile == nullptr ||
         CmdLine::HasArg( "-nofrontendmusic" ) ) {
        return false;
    }
    bLog = CmdLine::HasArg( "-logfrontendaudio" );
    const std::string path = gpFile->getBankFolder() + kFrontendBank;
    Bank bank;
    if ( !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "FrontEndMusic: no bank '%s'\n", path.c_str() );
        return false;
    }
    const BankEntry *pIni = bank.findEntry( kSoundsIni );
    if ( pIni == nullptr ) {
        OTDU_LOG_WARN( "FrontEndMusic: '%s' has no %s\n", path.c_str(),
                       kSoundsIni );
        return false;
    }
    // The table is text: parse it and drop the bank (the wavs are loose files).
    const bool bParsed = parseMusicTable( pIni->pData, pIni->Size );
    bank.unload();
    if ( !bParsed || tracks.empty() ) {
        OTDU_LOG_WARN( "FrontEndMusic: %s of '%s' has no SetMusic entries\n",
                       kSoundsIni, path.c_str() );
        return false;
    }
    bLoaded = true;
    OTDU_LOG_INFO( "FrontEndMusic: '%s', %zu place tracks, client %d (%s)\n",
                   path.c_str(), tracks.size(), kFrontEndMusicClient,
                   AudioSetup::GetBfxClientName( kFrontEndMusicClient ) );
    return true;
}

void FrontEndMusic::unload() {
    stopVoice();
    tracks.clear();
    unknownKeys.clear();
    pCurrentClip.reset();
    currentKey.clear();
    gain = 0.0f;
    targetGain = 0.0f;
    bLoaded = false;
}

bool FrontEndMusic::parseMusicTable( const uint8_t *pData, size_t size ) {
    const std::string text( reinterpret_cast<const char *>( pData ), size );
    size_t pos = 0;
    while ( pos <= text.size() ) {
        const size_t end = text.find( '\n', pos );
        const std::string line = Trim( text.substr(
            pos, end == std::string::npos ? std::string::npos : end - pos ) );
        pos = end == std::string::npos ? text.size() + 1 : end + 1;
        if ( line.empty() || line[0] == '[' || line[0] == ';' ||
             line.compare( 0, 2, "//" ) == 0 ) {
            continue;
        }
        if ( StringNCmpI( line.c_str(), "SetMusic", 8 ) != 0 ) {
            continue;
        }
        // SetMusic "<place key>" "<wav>" <volume> <unknown>
        std::string quoted[2];
        int32_t numQuoted = 0;
        size_t cursor = 8;
        while ( numQuoted < 2 ) {
            const size_t open = line.find( '"', cursor );
            if ( open == std::string::npos ) {
                break;
            }
            const size_t close = line.find( '"', open + 1 );
            if ( close == std::string::npos ) {
                break;
            }
            quoted[numQuoted++] = line.substr( open + 1, close - open - 1 );
            cursor = close + 1;
        }
        if ( numQuoted < 2 ) {
            OTDU_LOG_WARN( "FrontEndMusic: malformed line '%s'\n",
                           line.c_str() );
            continue;
        }
        Track track;
        track.File = quoted[1];
        const std::string tail =
            Trim( line.substr( cursor < line.size() ? cursor : line.size() ) );
        if ( !tail.empty() ) {
            track.Volume =
                Clamp( static_cast<float>( strtod( tail.c_str(), nullptr ) ),
                       0.0f, 4.0f );
        }
        tracks[ToLower( quoted[0] )] = std::move( track );
    }
    return !tracks.empty();
}

std::shared_ptr<AudioClip> FrontEndMusic::loadTrack( const std::string &file ) {
    const std::string base = gpFile->getBankFolder() + kMusicFolder;
    // The shipped wavs are lowercase; the ini keeps the authoring case.
    for ( const std::string &candidate : { file, ToLower( file ) } ) {
        const std::string path = base + candidate;
        if ( !gpFile->fileExists( path.c_str() ) ) {
            continue;
        }
        // gpFile reads loose files and bigfiles alike (loadClipFile takes a
        // real path, which the virtual bank folder is not).
        std::vector<uint8_t> bytes;
        if ( !gpFile->loadFile( path.c_str(), bytes ) || bytes.empty() ) {
            OTDU_LOG_WARN( "FrontEndMusic: cannot read '%s'\n", path.c_str() );
            continue;
        }
        std::shared_ptr<AudioClip> pClip =
            gpAudio->loadClip( bytes.data(), bytes.size(), file.c_str() );
        if ( pClip != nullptr ) {
            if ( bLog ) {
                OTDU_LOG_INFO( "FrontEndMusic: loaded '%s' (%.1f s)\n",
                               path.c_str(), pClip->Duration );
            }
            return pClip;
        }
        OTDU_LOG_WARN( "FrontEndMusic: '%s' could not be decoded\n",
                       path.c_str() );
    }
    OTDU_LOG_WARN( "FrontEndMusic: no '%s%s'\n", kMusicFolder, file.c_str() );
    return nullptr;
}

bool FrontEndMusic::playKey( const std::string &key, float volumeScale ) {
    if ( !bLoaded || gpAudio == nullptr || !gpAudio->isReady() ) {
        return false;
    }
    const std::string lower = ToLower( key );
    if ( lower == currentKey && voice != GSAudio::kInvalidVoice ) {
        return true; // already this track: no restart
    }
    const auto found = tracks.find( lower );
    if ( found == tracks.end() ) {
        if ( unknownKeys.emplace( lower, 0 ).second ) {
            OTDU_LOG_INFO( "FrontEndMusic: no track for '%s'\n", key.c_str() );
        }
        stop();
        return false;
    }
    std::shared_ptr<AudioClip> pClip = loadTrack( found->second.File );
    if ( pClip == nullptr ) {
        stop();
        return false;
    }
    stopVoice();
    pCurrentClip = pClip;
    currentKey = lower;
    targetGain = Clamp( found->second.Volume * volumeScale, 0.0f, 4.0f );
    gain = 0.0f;
    GSAudio::VoiceParams params;
    params.Client = kFrontEndMusicClient;
    params.bLoop = true;  // a track loops until the place changes
    params.Volume = 0.0f; // update() fades it in
    voice = gpAudio->play( pCurrentClip, params );
    if ( bLog ) {
        OTDU_LOG_INFO(
            "FrontEndMusic: '%s' -> %s (volume %.2f, %.1f s, voice %u)\n",
            key.c_str(), found->second.File.c_str(), targetGain,
            pClip->Duration, voice );
    }
    return voice != GSAudio::kInvalidVoice;
}

bool FrontEndMusic::playForSpot( const std::string &spotBank,
                                 float volumeScale ) {
    if ( !bLoaded ) {
        return false;
    }
    // The shops' interior entry carries the suffix (ECD_FERR -> ECD_FERRI); the
    // clubs / airport only have the spot's own key.
    if ( tracks.find( ToLower( spotBank + "I" ) ) != tracks.end() ) {
        return playKey( spotBank + "I", volumeScale );
    }
    return playKey( spotBank, volumeScale );
}

void FrontEndMusic::stop() {
    if ( voice == GSAudio::kInvalidVoice ) {
        return;
    }
    targetGain = 0.0f;
}

void FrontEndMusic::update( float deltaTime ) {
    if ( voice == GSAudio::kInvalidVoice || gpAudio == nullptr ) {
        return;
    }
    // A loop the mixer dropped (device change): forget it.
    if ( !gpAudio->isPlaying( voice ) ) {
        stopVoice();
        return;
    }
    const float step = deltaTime * ( targetGain > gain ? kFadeIn : kFadeOut );
    if ( targetGain > gain ) {
        gain = Min( targetGain, gain + step );
    } else {
        gain = Max( targetGain, gain - step );
    }
    if ( targetGain <= 0.0f && gain <= 0.001f ) {
        stopVoice();
        return;
    }
    if ( bLog && gain >= targetGain && gain > 0.0f ) {
        // Once per track: the fade reached the authored volume.
        static std::string lastLogged;
        if ( lastLogged != currentKey ) {
            lastLogged = currentKey;
            OTDU_LOG_INFO( "FrontEndMusic: '%s' at gain %.2f (voice %u)\n",
                           currentKey.c_str(), gain, voice );
        }
    }
    gpAudio->setVolume( voice, gain );
}

void FrontEndMusic::stopVoice() {
    if ( voice != GSAudio::kInvalidVoice && gpAudio != nullptr ) {
        gpAudio->stop( voice );
        if ( bLog ) {
            OTDU_LOG_INFO( "FrontEndMusic: '%s' stopped\n",
                           currentKey.c_str() );
        }
    }
    voice = GSAudio::kInvalidVoice;
    pCurrentClip.reset();
    currentKey.clear();
    gain = 0.0f;
    targetGain = 0.0f;
}
