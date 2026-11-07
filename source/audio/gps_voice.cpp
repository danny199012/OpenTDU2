#include "shared.h"
#include "gps_voice.h"

#include "config/gs_config.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"

#include <algorithm>

namespace {
    // bfx client index of "VoiceGPS" (AudioSetup::GetBfxClientName 5; the name
    // is also at 0xee6d78, next to the GPS.bnk path pattern).
    constexpr int32_t kVoiceGPSClient = 5;
} // namespace

bool GPSVoice::load() {
    unload();
    if ( gpAudio == nullptr || !gpAudio->isReady() || gpFile == nullptr ) {
        return false;
    }
    const std::string language = gpConfig != nullptr
                                     ? ToLower( gpConfig->getLanguage() )
                                     : std::string( "us" );
    for ( const std::string &candidate : { language, std::string( "us" ) } ) {
        const std::string path =
            gpFile->getBankFolder() + "sound/voices/" + candidate + "/gps.bnk";
        if ( !gpFile->fileExists( path.c_str() ) ) {
            continue;
        }
        Bank bank;
        if ( !bank.load( path.c_str() ) ) {
            continue;
        }
        // VFGPSVoice001_.wav .. 017: the cue is the sample's rank (the bank
        // order is the same, sorted to be safe).
        std::vector<const BankEntry *> samples =
            bank.findEntriesByExtension( "wav" );
        std::sort( samples.begin(), samples.end(),
                   []( const BankEntry *pA, const BankEntry *pB ) {
                       return StringCmpI( pA->Name.c_str(), pB->Name.c_str() ) <
                              0;
                   } );
        int32_t count = 0;
        for ( size_t i = 0; i < samples.size() && i < clips.size(); i++ ) {
            clips[i] = gpAudio->loadClip( samples[i]->pData, samples[i]->Size,
                                          samples[i]->Name.c_str() );
            count += clips[i] != nullptr ? 1 : 0;
        }
        bank.unload();
        bLoaded = count > 0;
        const AudioSetup &setup = gpAudio->getSetup();
        const int32_t client =
            setup.findClient( AudioSetup::GetBfxClientName( kVoiceGPSClient ) );
        float freerideGain = -1.0f;
        const int32_t mode = setup.findGameMode( "FREERIDE" );
        if ( client >= 0 && mode >= 0 ) {
            std::vector<float> gains;
            setup.computeClientGains( setup.getGameModes()[mode].MixPreset,
                                      gains );
            freerideGain = client < static_cast<int32_t>( gains.size() )
                               ? gains[client]
                               : 1.0f;
        }
        OTDU_LOG_INFO( "GPSVoice: '%s', %d of %zu samples, client %d (%s, "
                       "pipeline %d), FREERIDE mix gain %.2f\n",
                       path.c_str(), count, samples.size(), client,
                       client >= 0 ? setup.getClients()[client].Name.c_str()
                                   : "-",
                       client >= 0 ? setup.getClients()[client].Pipeline : -1,
                       freerideGain );
        return bLoaded;
    }
    OTDU_LOG_WARN( "GPSVoice: no gps.bnk for '%s'\n", language.c_str() );
    return false;
}

void GPSVoice::unload() {
    if ( voice != GSAudio::kInvalidVoice && gpAudio != nullptr ) {
        gpAudio->stop( voice );
    }
    voice = GSAudio::kInvalidVoice;
    for ( auto &clip : clips ) {
        clip.reset();
    }
    bLoaded = false;
}

bool GPSVoice::isPlaying() const {
    return voice != GSAudio::kInvalidVoice && gpAudio != nullptr &&
           gpAudio->isPlaying( voice );
}

bool GPSVoice::play( int32_t cue ) {
    if ( !bLoaded || cue < 0 || cue >= GPS_CueCount || clips[cue] == nullptr ) {
        return false;
    }
    if ( isPlaying() ) {
        OTDU_LOG_INFO( "[GPS] Can't play GPS sound (state was: playing) "
                       "(asked: %d) (sample: %s)\n",
                       cue, clips[cue]->Name.c_str() );
        return false;
    }
    GSAudio::VoiceParams params;
    params.Client = kVoiceGPSClient;
    params.Volume = 1.0f;
    voice = gpAudio->play( clips[cue], params );
    OTDU_LOG_INFO( "GPSVoice: cue %d (%s, %.2f s)\n", cue,
                   clips[cue]->Name.c_str(), clips[cue]->Duration );
    return voice != GSAudio::kInvalidVoice;
}
