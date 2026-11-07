#pragma once

#include <string>
#include <vector>

struct AudioClip;

// Audio driven mouth shapes: the same FaceFX targets `speech_synth.*` drives from
// letters, taken from a waveform instead (docs/re/custom_animations.md §4.1).
//
// This is the path a live voice needs. It is deliberately front-end free: a
// window of samples goes in, a handful of 0..1 values come out, and the face
// graph turns them into the 42 facial bones through the *registers* of
// FaceFxLibrary::Evaluate (the per-frame override the look-at already uses), so
// no animation asset is built at all. A microphone capture and a network
// stream only have to produce the same buffer.
//
// Decoding is the engine's, not this class's: `load` goes through
// GSAudio::loadClipFile, which reads RIFF through SDL_LoadWAV_IO (PCM **and**
// MS / IMA ADPCM - the game's own wavs are 4-bit MS ADPCM, 22 kHz stereo) and
// the compressed formats through audio/audio_decoders. Whatever the game can
// play, the mouth can follow.
//
// What the analysis can and cannot tell apart [INF]:
//   - level  -> the jaw (`open`): every study of loudness driven lips starts here;
//   - bright / dark energy -> `wide` (front vowels) vs `W` (rounded);
//   - high band ratio      -> `ShCh` (sibilants: s, sh, ch, f);
//   - lip closure (`PBM`) is NOT recoverable from loudness alone - it needs a
//     phonetic classifier - so it stays 0 and the mouth closes through `open`.
class SpeechAudio {
public:
    // The analysis rate: 8 kHz mono is plenty for an envelope and a zero
    // crossing rate, and it keeps the per-frame cost in the microseconds.
    static constexpr int32_t kRate = 8000;

    struct Visemes {
        float Open = 0.0f;
        float Wide = 0.0f;
        float W = 0.0f;
        float ShCh = 0.0f;
        float FV = 0.0f;
    };

    // A file the engine can decode (any .wav, mp3, ogg...), mixed to 8 kHz mono.
    bool load( const char *pPath );
    // An already decoded clip (GSAudio::loadClip): downmixed and resampled.
    bool loadClip( const AudioClip &clip, const char *pName );
    void unload();
    inline bool isLoaded() const { return !samples.empty(); }
    inline float getDuration() const { return duration; }

    // The mouth at time t (clamped to the clip). 40 ms window, 20 ms hop
    // smoothed by a fast attack / slower release inside sample().
    Visemes sample( float t ) const;

private:
    std::vector<float> samples; // mono, -1..1
    float duration = 0.0f;

    // RMS of the window centred on `index` (0..1) and its zero crossing rate.
    float windowRms( int32_t index ) const;
    float windowZeroCrossings( int32_t index ) const;
    float windowHighRatio( int32_t index ) const;
};
