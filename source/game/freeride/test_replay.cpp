#include "shared.h"
#include "test_replay.h"
#include "core/xmb.h"
#include "filesystem/gs_file.h"

namespace {
template <typename T> T ReadAt( const std::vector<uint8_t> &buffer, size_t offset ) {
    T value{};
    if ( offset + sizeof( T ) <= buffer.size() ) {
        memcpy( &value, buffer.data() + offset, sizeof( T ) );
    }
    return value;
}
} // namespace

bool TestReplay::load( const std::string &testName ) {
    keys.clear();
    duration = 0.0f;
    if ( gpFile == nullptr ) {
        return false;
    }
    std::string lower = testName;
    for ( char &c : lower ) {
        c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
    }
    const std::string path = gpFile->getBankFolder() + "ig2/replays/" + lower + ".rly";
    std::vector<uint8_t> data;
    if ( !gpFile->fileExists( path.c_str() ) || !gpFile->loadFile( path.c_str(), data ) || data.size() < 64 ) {
        return false;
    }
    const uint32_t xmbSize = ReadAt<uint32_t>( data, 8 );
    XmbDocument document;
    if ( 12 + static_cast<size_t>( xmbSize ) > data.size() || !document.load( data.data() + 12, xmbSize ) ) {
        return false;
    }
    const XmbValue root = document.getRoot();
    const int64_t matrixSize = root["MatrixChunkSize"].asInt();
    const int64_t matrixRate = root["MatrixChunkRate"].asInt();
    const int64_t matrixUsed = root["MatrixChunkNumberUsed"].asInt();
    const int64_t frames = root["FrameCount"].asInt();
    const int64_t timestepSize = root["TimestepChunkSize"].asInt();
    const int64_t inputSize = root["InputChunkSize"].asInt();
    const int64_t inputUsed = root["InputChunkNumberUsed"].asInt();
    if ( matrixSize < 45 || matrixRate <= 0 || matrixUsed <= 0 || frames <= 0 ) {
        return false;
    }
    size_t pos = 12 + xmbSize;
    const uint32_t carSize = ReadAt<uint32_t>( data, pos );
    pos += 4 + carSize;
    for ( int64_t i = 0; i < matrixUsed; i++ ) {
        const size_t at = pos + static_cast<size_t>( i * matrixSize );
        if ( at + 45 > data.size() ) {
            break;
        }
        Key key;
        key.Position = Vector3f( ReadAt<float>( data, at + 9 ), ReadAt<float>( data, at + 13 ), ReadAt<float>( data, at + 17 ) );
        auto axis = [&]( size_t row ) {
            const size_t base = at + 25 + row * 8;
            return Vector3f( ReadAt<int16_t>( data, base ), ReadAt<int16_t>( data, base + 2 ), ReadAt<int16_t>( data, base + 4 ) ) *
                   ( 1.0f / 32767.0f );
        };
        key.Right = axis( 0 );
        key.Up = axis( 1 );
        key.Forward = axis( 2 );
        keys.push_back( key );
    }
    // Timestep chunks: dt and the time of day. They follow the input chunks.
    const size_t timestepAt = pos + static_cast<size_t>( matrixUsed * matrixSize + inputUsed * inputSize );
    float dt = 1.0f / 90.0f;
    if ( timestepSize >= 8 && timestepAt + 8 <= data.size() ) {
        dt = ReadAt<float>( data, timestepAt );
        clock = ReadAt<float>( data, timestepAt + 4 );
        if ( !( dt > 0.0f && dt < 0.1f ) ) {
            dt = 1.0f / 90.0f;
        }
    }
    keyInterval = dt * static_cast<float>( matrixRate );
    duration = dt * static_cast<float>( frames );
    carId = root.getArraySize( "VehicleInfo" ) > 0 ? static_cast<int32_t>( root.getArrayElement( "VehicleInfo", 0 )["Id"].asInt( -1 ) ) : -1;
    OTDU_LOG_INFO( "TestReplay: %s: %zu keyframe(s), %.2f s, car %d, clock %.0f s\n", path.c_str(), keys.size(), duration, carId,
                   clock );
    return !keys.empty();
}

void TestReplay::sample( float time, Vector3f &outPosition, Vector3f &outForward, Vector3f &outUp ) const {
    if ( keys.empty() ) {
        return;
    }
    const float f = Clamp( time / keyInterval, 0.0f, static_cast<float>( keys.size() - 1 ) );
    const size_t a = static_cast<size_t>( f );
    const size_t b = Min( a + 1, keys.size() - 1 );
    const float t = f - static_cast<float>( a );
    outPosition = keys[a].Position + ( keys[b].Position - keys[a].Position ) * t;
    Vector3f forward = keys[a].Forward + ( keys[b].Forward - keys[a].Forward ) * t;
    Vector3f up = keys[a].Up + ( keys[b].Up - keys[a].Up ) * t;
    outForward = forward.length() > 1e-4f ? forward.normalized() : keys[a].Forward;
    outUp = up.length() > 1e-4f ? up.normalized() : Vector3f( 0.0f, 1.0f, 0.0f );
}
