#pragma once

#include <filesystem>

// Decoders for the loose audio files of the custom radio stations
// (RadioPlayer): MP3 through dr_mp3, OGG Vorbis through stb_vorbis, OGG Opus
// through opusfile and FLAC through dr_flac. WAV stays on SDL_LoadWAV_IO
// (GSAudio::loadClip) — SDL3 decodes WAV only, which is also what the retail
// radio banks hold.
//
// The format is sniffed from the file's magic bytes, not from its extension: a
// track downloaded as ".mp3" is very often an Ogg Opus (or an MP4/M4A) stream.
//
// The result is interleaved F32 at the file's own rate (the mixer resamples
// with the voice pitch, like for the bank songs), at most 2 channels (a
// multichannel file is downmixed to stereo).
struct DecodedAudio {
    std::vector<float> Frames;
    int32_t Channels = 0; // 1 or 2
    int32_t Rate = 0;
};

// True when the data is a RIFF WAVE (GSAudio::loadClip decodes it with SDL).
bool IsWaveData( const uint8_t *pData, size_t size );

// Decodes pData into out (the file's name is only used in the log messages).
// False when the format is unsupported (MP4 / M4A / WMA) or the data is
// corrupt.
bool DecodeAudioData( const uint8_t *pData, size_t size, const char *pName, DecodedAudio &out );

// Reads pPath and decodes it (see DecodeAudioData).
bool DecodeAudioFile( const std::filesystem::path &path, DecodedAudio &out );
