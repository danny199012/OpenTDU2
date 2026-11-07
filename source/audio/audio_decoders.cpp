#include "shared.h"
#include "audio/audio_decoders.h"
#include "core/path_utf8.h"

// The decoders' implementation units. dr_mp3 / dr_flac are header only (one
// translation unit defines their IMPLEMENTATION); stb_vorbis, libogg and
// opusfile are compiled as C by their own targets, so only declarations are
// pulled in here (STB_VORBIS_HEADER_ONLY, inside the extern "C" the file itself
// opens). Opus matters: a track downloaded as ".mp3" is very often an Ogg Opus.
#define DR_MP3_NO_STDIO // memory decoding only: no FILE / wchar in the headers
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_NO_STDIO
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <opusfile.h>

namespace {
constexpr int32_t kMaxChannels = 2;

// The containers / codecs the sniffer recognises.
enum class eAudioFormat : uint8_t { Unknown, Wave, Mp3, Flac, OggVorbis, OggOpus, Mp4 };

eAudioFormat SniffAudioFormat( const uint8_t *pData, size_t size ) {
    if ( size < 16 ) {
        return eAudioFormat::Unknown;
    }
    if ( memcmp( pData, "RIFF", 4 ) == 0 && memcmp( pData + 8, "WAVE", 4 ) == 0 ) {
        return eAudioFormat::Wave;
    }
    if ( memcmp( pData, "fLaC", 4 ) == 0 ) {
        return eAudioFormat::Flac;
    }
    if ( memcmp( pData, "OggS", 4 ) == 0 ) {
        // The first page's payload starts after the 27-byte page header and its
        // segment table and carries the codec id packet ("\x01vorbis" /
        // "OpusHead").
        const size_t scan = Min( size, static_cast<size_t>( 512 ) );
        for ( size_t i = 27; i + 8 <= scan; i++ ) {
            if ( memcmp( pData + i, "OpusHead", 8 ) == 0 ) {
                return eAudioFormat::OggOpus;
            }
            if ( memcmp( pData + i, "\x01vorbis", 7 ) == 0 ) {
                return eAudioFormat::OggVorbis;
            }
        }
        return eAudioFormat::OggVorbis; // an unknown Ogg: the vorbis reader reports it
    }
    if ( memcmp( pData + 4, "ftyp", 4 ) == 0 ) {
        return eAudioFormat::Mp4; // .m4a / .mp4 (AAC): no decoder here
    }
    if ( memcmp( pData, "ID3", 3 ) == 0 ) {
        return eAudioFormat::Mp3;
    }
    if ( pData[0] == 0xff && ( pData[1] & 0xe0 ) == 0xe0 ) {
        return eAudioFormat::Mp3; // a frame sync, the MP3 of an untagged file
    }
    return eAudioFormat::Unknown;
}

bool ReadFileBytes( const std::filesystem::path &path, std::vector<uint8_t> &outBytes ) {
    std::ifstream stream( path, std::ios::binary | std::ios::ate );
    if ( !stream.is_open() ) {
        return false;
    }
    const std::streamsize size = stream.tellg();
    if ( size <= 0 ) {
        return false;
    }
    stream.seekg( 0, std::ios::beg );
    outBytes.resize( static_cast<size_t>( size ) );
    return static_cast<bool>(
        stream.read( reinterpret_cast<char *>( outBytes.data() ), size ) );
}

// Interleaved F32 of a decoder that reports the file's own channel count: only
// the first 2 channels are kept.
void KeepChannels( const std::vector<float> &interleaved, int32_t channels, DecodedAudio &out ) {
    const int32_t keep = Min( channels, kMaxChannels );
    out.Channels = keep;
    if ( keep == channels ) {
        out.Frames = interleaved;
        return;
    }
    const size_t frames = interleaved.size() / static_cast<size_t>( channels );
    out.Frames.resize( frames * static_cast<size_t>( keep ) );
    for ( size_t frame = 0; frame < frames; frame++ ) {
        for ( int32_t channel = 0; channel < keep; channel++ ) {
            out.Frames[frame * static_cast<size_t>( keep ) + static_cast<size_t>( channel )] =
                interleaved[frame * static_cast<size_t>( channels ) + static_cast<size_t>( channel )];
        }
    }
}


// The vorbis decoder hands back interleaved signed 16-bit samples.
void ConvertShorts( const short *pSamples, size_t numFrames, int32_t channels, DecodedAudio &out ) {
    std::vector<float> interleaved( numFrames * static_cast<size_t>( channels ) );
    for ( size_t i = 0; i < interleaved.size(); i++ ) {
        interleaved[i] = static_cast<float>( pSamples[i] ) / 32768.0f;
    }
    KeepChannels( interleaved, channels, out );
}

bool DecodeMp3( const uint8_t *pData, size_t size, const char *pName, DecodedAudio &out ) {
    drmp3 mp3;
    if ( !drmp3_init_memory( &mp3, pData, size, nullptr ) ) {
        OTDU_LOG_WARN( "Audio: '%s' is not an MP3\n", pName );
        return false;
    }
    const drmp3_uint64 numFrames = drmp3_get_pcm_frame_count( &mp3 );
    const int32_t channels = static_cast<int32_t>( mp3.channels );
    std::vector<float> interleaved( static_cast<size_t>( numFrames ) * static_cast<size_t>( channels ) );
    const drmp3_uint64 read = drmp3_read_pcm_frames_f32( &mp3, numFrames, interleaved.data() );
    interleaved.resize( static_cast<size_t>( read ) * static_cast<size_t>( channels ) );
    out.Rate = static_cast<int32_t>( mp3.sampleRate );
    drmp3_uninit( &mp3 );
    KeepChannels( interleaved, channels, out );
    return true;
}

bool DecodeFlac( const uint8_t *pData, size_t size, const char *pName, DecodedAudio &out ) {
    unsigned int channels = 0, rate = 0;
    drflac_uint64 numFrames = 0;
    float *pPcm = drflac_open_memory_and_read_pcm_frames_f32( pData, size, &channels, &rate, &numFrames, nullptr );
    if ( pPcm == nullptr ) {
        OTDU_LOG_WARN( "Audio: '%s' is not a FLAC file\n", pName );
        return false;
    }
    std::vector<float> interleaved( pPcm, pPcm + static_cast<size_t>( numFrames ) * channels );
    drflac_free( pPcm, nullptr );
    out.Rate = static_cast<int32_t>( rate );
    KeepChannels( interleaved, static_cast<int32_t>( channels ), out );
    return true;
}

bool DecodeVorbis( const uint8_t *pData, size_t size, const char *pName, DecodedAudio &out ) {
    int channels = 0, rate = 0;
    short *pSamples = nullptr;
    // Return value: frames (samples per channel), -1/-2 on error. The buffer is
    // malloc'd (stb_vorbis_decode_memory's contract): free() it.
    const int numFrames = stb_vorbis_decode_memory( pData, static_cast<int>( size ), &channels, &rate, &pSamples );
    if ( numFrames <= 0 || pSamples == nullptr || channels <= 0 ) {
        OTDU_LOG_WARN( "Audio: '%s' is not an OGG Vorbis file\n", pName );
        return false;
    }
    out.Rate = rate;
    ConvertShorts( pSamples, static_cast<size_t>( numFrames ), channels, out );
    free( pSamples );
    return true;
}

bool DecodeOpus( const uint8_t *pData, size_t size, const char *pName, DecodedAudio &out ) {
    int error = 0;
    OggOpusFile *pFile = op_open_memory( pData, size, &error );
    if ( pFile == nullptr ) {
        OTDU_LOG_WARN( "Audio: '%s' is not an OGG Opus file (error %d)\n", pName, error );
        return false;
    }
    // op_read_float_stereo downmixes to 2 channels; Opus always decodes at
    // 48 kHz and opusfile handles the pre-skip / gain.
    constexpr int32_t kChunkFrames = 8192;
    std::vector<float> chunk( kChunkFrames * 2 );
    out.Channels = 2;
    out.Rate = 48000;
    for ( ;; ) {
        const int read = op_read_float_stereo( pFile, chunk.data(), static_cast<int>( chunk.size() ) );
        if ( read <= 0 ) {
            if ( read < 0 ) {
                OTDU_LOG_WARN( "Audio: '%s' stopped at a decode error (%d)\n", pName, read );
            }
            break;
        }
        out.Frames.insert( out.Frames.end(), chunk.begin(), chunk.begin() + static_cast<size_t>( read ) * 2 );
    }
    op_free( pFile );
    return true;
}
} // namespace

bool IsWaveData( const uint8_t *pData, size_t size ) {
    return SniffAudioFormat( pData, size ) == eAudioFormat::Wave;
}

bool DecodeAudioData( const uint8_t *pData, size_t size, const char *pName, DecodedAudio &out ) {
    out = DecodedAudio();
    if ( pData == nullptr || size == 0 ) {
        return false;
    }
    bool bDecoded = false;
    switch ( SniffAudioFormat( pData, size ) ) {
    case eAudioFormat::Mp3:
        bDecoded = DecodeMp3( pData, size, pName, out );
        break;
    case eAudioFormat::Flac:
        bDecoded = DecodeFlac( pData, size, pName, out );
        break;
    case eAudioFormat::OggVorbis:
        bDecoded = DecodeVorbis( pData, size, pName, out );
        break;
    case eAudioFormat::OggOpus:
        bDecoded = DecodeOpus( pData, size, pName, out );
        break;
    case eAudioFormat::Mp4:
        OTDU_LOG_WARN( "Audio: '%s' is an MP4/M4A (AAC) file, not supported (use mp3 / ogg / opus / flac / wav)\n",
                       pName );
        break;
    case eAudioFormat::Wave:
        OTDU_LOG_WARN( "Audio: '%s' is a WAV, decoded by SDL_LoadWAV_IO\n", pName );
        break;
    case eAudioFormat::Unknown:
        OTDU_LOG_WARN( "Audio: '%s' is not a recognised audio file (mp3 / ogg / opus / flac)\n", pName );
        break;
    }
    if ( !bDecoded ) {
        out = DecodedAudio();
        return false;
    }
    if ( out.Frames.empty() || out.Channels <= 0 || out.Rate <= 0 ) {
        OTDU_LOG_WARN( "Audio: '%s' decoded to nothing\n", pName );
        out = DecodedAudio();
        return false;
    }
    return true;
}

bool DecodeAudioFile( const std::filesystem::path &path, DecodedAudio &out ) {
    std::vector<uint8_t> bytes;
    if ( !ReadFileBytes( path, bytes ) ) {
        OTDU_LOG_WARN( "Audio: cannot read '%s'\n", PathToUtf8( path ).c_str() );
        return false;
    }
    return DecodeAudioData( bytes.data(), bytes.size(), PathToUtf8( path.filename() ).c_str(), out );
}
