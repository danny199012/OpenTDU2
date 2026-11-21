#include "shared.h"
#include "school_voice.h"
#include "audio/gs_audio.h"
#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "filesystem/bank.h"
#include "system/cmd_line.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace SchoolVoice {

namespace {
constexpr int32_t kVoiceClient = 5; // the same client the phone calls speak on
GSAudio::VoiceHandle sVoice = GSAudio::kInvalidVoice;
std::shared_ptr<AudioClip> sClip;
} // namespace

void Stop() {
    if ( gpAudio != nullptr && sVoice != GSAudio::kInvalidVoice ) {
        gpAudio->stop( sVoice );
    }
    sVoice = GSAudio::kInvalidVoice;
}

void PlayEndOfTest( char letter, bool bWon ) {
    if ( CmdLine::HasArg( "-noschoolvoice" ) || gpAudio == nullptr || !gpAudio->isReady() || gpFile == nullptr ) {
        return;
    }
    const char low = static_cast<char>( tolower( static_cast<unsigned char>( letter ) ) );
    if ( low != 'a' && low != 'b' && low != 'c' ) {
        return;
    }
    const std::string language = gpConfig != nullptr ? gpConfig->getLanguage() : std::string( "us" );
    std::vector<std::string> languages = { language, "us" };
    for ( std::string &name : languages ) {
        for ( char &c : name ) {
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        }
    }
    for ( const std::string &lang : languages ) {
        const std::string bankPath = gpFile->getBankFolder() + "scenarics/packs/pack_racingschool__" + lang + ".bnk";
        Bank bank;
        if ( !gpFile->fileExists( bankPath.c_str() ) || !bank.load( bankPath.c_str() ) ) {
            continue;
        }
        // The numbered lines of this school and outcome that the bank holds (win 1..2, loose 1..4), one picked at random.
        std::vector<const BankEntry *> lines;
        for ( int32_t n = 1; n <= 9; n++ ) {
            char name[96];
            snprintf( name, sizeof( name ), "rs%c_endtest_%s_%02d_VOICE_STORYLINE.wav", low, bWon ? "win" : "loose", n );
            if ( const BankEntry *pEntry = bank.findEntry( name ) ) {
                lines.push_back( pEntry );
            }
        }
        if ( lines.empty() ) {
            continue;
        }
        const BankEntry *pPick = lines[static_cast<size_t>( rand() ) % lines.size()];
        Stop();
        sClip = gpAudio->loadClip( pPick->pData, pPick->Size, "school_endtest" );
        if ( sClip == nullptr ) {
            continue;
        }
        GSAudio::VoiceParams params;
        params.Client = kVoiceClient;
        params.Volume = 1.0f;
        sVoice = gpAudio->play( sClip, params );
        OTDU_LOG_INFO( "SchoolVoice: school %c %s line (%zu to pick from), %.1f s\n", letter, bWon ? "win" : "loose", lines.size(),
                       sClip->Duration );
        return;
    }
}

} // namespace SchoolVoice
