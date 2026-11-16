#include "shared.h"
#include "audio/frontend_audio.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include <chrono>

namespace {
    constexpr const char *kFrontendBank = "frontend/sounds/fe_snd.bnk";
    constexpr const char *kSoundsIni = "sounds.ini";
    // "FrontEnd" in the bfx client table (AudioSetup::GetBfxClientName 7); the
    // audiosetup client of that name sits on the "FrontEndStereo" pipeline.
    constexpr int32_t kFrontEndClient = 7;
    // The movies fire some cues twice for one press (the options tabs run their
    // 'open' on two frames): a repeat inside this window is the same sound.
    constexpr double kMinInterval = 0.04;

    std::string Trim( const std::string &value ) {
        const size_t begin = value.find_first_not_of( " \t\r\n" );
        if ( begin == std::string::npos ) {
            return std::string();
        }
        const size_t end = value.find_last_not_of( " \t\r\n" );
        return value.substr( begin, end - begin + 1 );
    }

    double Now() {
        return std::chrono::duration<double>(
                   std::chrono::steady_clock::now().time_since_epoch() )
            .count();
    }
} // namespace

FrontEndAudio *gpFrontEndAudio = nullptr;

FrontEndAudio::FrontEndAudio() { gpFrontEndAudio = this; }

FrontEndAudio::~FrontEndAudio() {
    unload();
    if ( gpFrontEndAudio == this ) {
        gpFrontEndAudio = nullptr;
    }
}

bool FrontEndAudio::load() {
    unload();
    if ( gpAudio == nullptr || !gpAudio->isReady() || gpFile == nullptr ||
         CmdLine::HasArg( "-nofrontendaudio" ) ) {
        return false;
    }
    bLog = CmdLine::HasArg( "-logfrontendaudio" );
    const std::string path = gpFile->getBankFolder() + kFrontendBank;
    pBank = std::make_unique<Bank>();
    if ( !pBank->load( path.c_str() ) ) {
        OTDU_LOG_WARN( "FrontEndAudio: no bank '%s'\n", path.c_str() );
        pBank.reset();
        return false;
    }
    const BankEntry *pIni = pBank->findEntry( kSoundsIni );
    if ( pIni == nullptr ) {
        OTDU_LOG_WARN( "FrontEndAudio: '%s' has no %s\n", path.c_str(),
                       kSoundsIni );
        pBank.reset();
        return false;
    }
    if ( !parseSoundsIni( pIni->pData, pIni->Size ) || cues.empty() ) {
        OTDU_LOG_WARN( "FrontEndAudio: %s of '%s' has no AddFESound cues\n",
                       kSoundsIni, path.c_str() );
        pBank.reset();
        return false;
    }
    bLoaded = true;
    const AudioSetup &setup = gpAudio->getSetup();
    const int32_t client =
        setup.findClient( AudioSetup::GetBfxClientName( kFrontEndClient ) );
    OTDU_LOG_INFO(
        "FrontEndAudio: '%s', %zu cues, client %d (%s, pipeline %d)\n",
        path.c_str(), cues.size(), client,
        client >= 0 ? setup.getClients()[client].Name.c_str() : "-",
        client >= 0 ? setup.getClients()[client].Pipeline : -1 );
    return true;
}

void FrontEndAudio::unload() {
    cues.clear();
    clips.clear();
    unknownCues.clear();
    pBank.reset();
    bLoaded = false;
}

bool FrontEndAudio::parseSoundsIni( const uint8_t *pData, size_t size ) {
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
        if ( StringNCmpI( line.c_str(), "AddFESound", 10 ) != 0 ) {
            continue;
        }
        // The two quoted fields: the flash message (cue) and the wav.
        std::string quoted[2];
        int32_t numQuoted = 0;
        size_t cursor = 10;
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
            OTDU_LOG_WARN( "FrontEndAudio: malformed line '%s'\n",
                           line.c_str() );
            continue;
        }
        Cue cue;
        cue.File = quoted[1];
        const std::string tail =
            Trim( line.substr( cursor < line.size() ? cursor : line.size() ) );
        if ( !tail.empty() ) {
            cue.Volume =
                Clamp( static_cast<float>( strtod( tail.c_str(), nullptr ) ),
                       0.0f, 4.0f );
        }
        cues[ToLower( quoted[0] )] = std::move( cue );
    }
    return !cues.empty();
}

std::shared_ptr<AudioClip> FrontEndAudio::getClip( Cue &cue ) {
    if ( cue.Clip != nullptr ) {
        return cue.Clip;
    }
    const std::string key = ToLower( cue.File );
    const auto known = clips.find( key );
    if ( known != clips.end() ) {
        cue.Clip = known->second;
        return cue.Clip;
    }
    const BankEntry *pEntry =
        pBank != nullptr ? pBank->findEntry( cue.File.c_str() ) : nullptr;
    if ( pEntry == nullptr ) {
        OTDU_LOG_WARN( "FrontEndAudio: '%s' is not in the bank\n",
                       cue.File.c_str() );
        clips[key] = nullptr;
        return nullptr;
    }
    // loadClip copies the decoded frames: the bank can stay mapped (the SDL
    // decoder reads the MS-ADPCM straight out of it).
    cue.Clip =
        gpAudio->loadClip( pEntry->pData, pEntry->Size, pEntry->Name.c_str() );
    clips[key] = cue.Clip;
    if ( bLog ) {
        OTDU_LOG_INFO( "FrontEndAudio: decoded '%s' (%.2f s)\n",
                       cue.File.c_str(),
                       cue.Clip != nullptr ? cue.Clip->Duration : 0.0f );
    }
    return cue.Clip;
}

bool FrontEndAudio::play( const std::string &cueName, float volumeScale ) {
    if ( !bLoaded || gpAudio == nullptr || !gpAudio->isReady() ) {
        return false;
    }
    const std::string key = ToLower( cueName );
    const auto found = cues.find( key );
    if ( found == cues.end() ) {
        if ( unknownCues.emplace( key, 0 ).second ) {
            OTDU_LOG_WARN( "FrontEndAudio: unknown cue '%s'\n",
                           cueName.c_str() );
        }
        return false;
    }
    Cue &cue = found->second;
    const double now = Now();
    if ( now - cue.LastPlay < kMinInterval ) {
        return false;
    }
    const std::shared_ptr<AudioClip> pClip = getClip( cue );
    if ( pClip == nullptr ) {
        return false;
    }
    cue.LastPlay = now;
    GSAudio::VoiceParams params;
    params.Client = kFrontEndClient;
    params.Volume = Clamp( cue.Volume * volumeScale, 0.0f, 4.0f );
    gpAudio->play( pClip, params );
    if ( bLog ) {
        OTDU_LOG_INFO( "FrontEndAudio: '%s' -> %s (volume %.2f, %.2f s)\n",
                       cueName.c_str(), cue.File.c_str(), params.Volume,
                       pClip->Duration );
    }
    return true;
}

bool FrontEndAudio::handleCommand( const std::string &command ) {
    if ( StringNCmpI( command.c_str(), "sndp:", 5 ) != 0 ) {
        return false;
    }
    play( command.substr( 5 ) );
    return true;
}

void PlayFrontEndCue( const char *pCue, float volumeScale ) {
    if ( gpFrontEndAudio != nullptr && pCue != nullptr && pCue[0] != '\0' ) {
        gpFrontEndAudio->play( pCue, volumeScale );
    }
}
