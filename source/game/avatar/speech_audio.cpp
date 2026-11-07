#include "shared.h"
#include "speech_audio.h"

#include "audio/gs_audio.h"

#include <cmath>

bool SpeechAudio::load( const char *pPath ) {
    unload();
    if ( pPath == nullptr || gpAudio == nullptr ) {
        return false;
    }
    // The engine's own loader: RIFF through SDL_LoadWAV_IO (PCM and MS / IMA
    // ADPCM - the game's wavs are 4-bit MS ADPCM), everything else through
    // audio/audio_decoders. If the game can play it, the mouth can follow it.
    std::shared_ptr<AudioClip> pClip = gpAudio->loadClipFile( pPath, pPath );
    if ( pClip == nullptr || pClip->NumFrames == 0 ) {
        OTDU_LOG_WARN( "SpeechAudio: '%s' did not decode\n", pPath );
        return false;
    }
    return loadClip( *pClip, pPath );
}

bool SpeechAudio::loadClip( const AudioClip &clip, const char *pName ) {
    unload();
    const int32_t channels = Clamp( clip.Channels, 1, 2 );
    if ( clip.NumFrames == 0 || clip.Rate <= 0 || clip.Frames.size() < static_cast<size_t>( clip.NumFrames ) ) {
        return false;
    }
    // Mix to mono and resample to kRate by nearest frame: the envelope and the
    // zero crossing rate do not need anything better.
    const size_t outFrames = static_cast<size_t>( static_cast<double>( clip.NumFrames ) * kRate / clip.Rate );
    samples.assign( outFrames, 0.0f );
    for ( size_t i = 0; i < outFrames; i++ ) {
        const size_t frame = static_cast<size_t>( static_cast<double>( i ) * clip.Rate / kRate );
        if ( frame >= clip.NumFrames ) {
            break;
        }
        const size_t base = frame * static_cast<size_t>( channels );
        if ( base + static_cast<size_t>( channels ) > clip.Frames.size() ) {
            break;
        }
        float sum = 0.0f;
        for ( int32_t c = 0; c < channels; c++ ) {
            sum += clip.Frames[base + static_cast<size_t>( c )];
        }
        samples[i] = sum / static_cast<float>( channels );
    }
    duration = static_cast<float>( outFrames ) / static_cast<float>( kRate );
    OTDU_LOG_INFO( "SpeechAudio: '%s' %d Hz %d ch -> %zu frames, %.2f s at %d Hz\n",
                   pName != nullptr ? pName : clip.Name.c_str(), clip.Rate, channels, outFrames, duration,
                   kRate );
    // A silent clip loads (it is valid) but drives nothing: say so, it is a
    // confusing test result otherwise.
    float peak = 0.0f;
    for ( const float value : samples ) {
        peak = Max( peak, std::fabs( value ) );
    }
    if ( peak < 1e-4f ) {
        OTDU_LOG_WARN( "SpeechAudio: '%s' is silent (peak %.5f), the mouth will not move\n",
                       pName != nullptr ? pName : clip.Name.c_str(), peak );
    }
    return !samples.empty();
}

void SpeechAudio::unload() {
    samples.clear();
    duration = 0.0f;
}

namespace {
    struct Window {
        int32_t Begin;
        int32_t End;
    };
    // +-20 ms around the index, clamped to the clip.
    Window MakeWindow( int32_t index, size_t size ) {
        constexpr int32_t kHalf = SpeechAudio::kRate / 50;
        Window window;
        window.Begin = index - kHalf < 0 ? 0 : index - kHalf;
        window.End = index + kHalf > static_cast<int32_t>( size ) ? static_cast<int32_t>( size )
                                                                 : index + kHalf;
        return window;
    }
} // namespace

float SpeechAudio::windowRms( int32_t index ) const {
    const Window window = MakeWindow( index, samples.size() );
    if ( window.End <= window.Begin ) {
        return 0.0f;
    }
    double sum = 0.0;
    for ( int32_t i = window.Begin; i < window.End; i++ ) {
        sum += static_cast<double>( samples[i] ) * samples[i];
    }
    return static_cast<float>( std::sqrt( sum / ( window.End - window.Begin ) ) );
}

float SpeechAudio::windowZeroCrossings( int32_t index ) const {
    const Window window = MakeWindow( index, samples.size() );
    if ( window.End - window.Begin < 2 ) {
        return 0.0f;
    }
    int32_t crossings = 0;
    for ( int32_t i = window.Begin + 1; i < window.End; i++ ) {
        crossings += ( samples[i - 1] < 0.0f ) != ( samples[i] < 0.0f ) ? 1 : 0;
    }
    return static_cast<float>( crossings ) / static_cast<float>( window.End - window.Begin );
}

float SpeechAudio::windowHighRatio( int32_t index ) const {
    // Ratio of the first difference's energy to the signal's: a cheap,
    // monotonic stand-in for "how much of this is above ~1 kHz".
    const Window window = MakeWindow( index, samples.size() );
    if ( window.End - window.Begin < 2 || window.Begin < 1 ) {
        return 0.0f;
    }
    double low = 0.0;
    double high = 0.0;
    for ( int32_t i = window.Begin; i < window.End; i++ ) {
        low += static_cast<double>( samples[i] ) * samples[i];
        const double difference = static_cast<double>( samples[i] ) - samples[i - 1];
        high += difference * difference;
    }
    return low > 1e-9 ? static_cast<float>( Min( high / ( low * 4.0 ), 4.0 ) ) : 0.0f;
}

SpeechAudio::Visemes SpeechAudio::sample( float t ) const {
    Visemes visemes;
    if ( samples.empty() ) {
        return visemes;
    }
    const float clamped = Clamp( t, 0.0f, duration );
    const int32_t index = static_cast<int32_t>( clamped * kRate );
    // Level in decibels over a -50 dB floor: a whisper is ~-40 dB, a shout ~-6.
    const float rms = windowRms( index );
    const float db = 20.0f * std::log10( Max( rms, 1e-5f ) );
    const float level = Clamp( ( db + 50.0f ) / 44.0f, 0.0f, 1.0f );
    if ( level <= 0.0f ) {
        return visemes; // silence: the mouth is closed
    }
    // Brightness: zero crossing rate and the difference energy both rise with
    // the frontness of a vowel and with sibilance.
    const float crossings = Clamp( ( windowZeroCrossings( index ) - 0.06f ) / 0.14f, 0.0f, 1.0f );
    const float high = Clamp( ( windowHighRatio( index ) - 0.30f ) / 0.50f, 0.0f, 1.0f );
    const float bright = Max( crossings, high );
    // The jaw follows the level (squared, so quiet syllables barely open it),
    // the front / rounded pair splits the remainder, sibilants take the rest.
    const float jaw = level * level;
    const float sibilant = high * level;
    visemes.Open = jaw;
    visemes.FV = sibilant * 0.5f;
    visemes.ShCh = sibilant;
    visemes.Wide = ( jaw - sibilant ) * bright;
    visemes.W = ( jaw - sibilant ) * ( 1.0f - bright );
    return visemes;
}
