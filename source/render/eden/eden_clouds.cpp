#include "shared.h"
#include "eden_clouds.h"

#include "system/cmd_line.h"

#include "render/shaders/builtin/depthcopy_frag.h"
#include "render/shaders/builtin/volclouds_accum_frag.h"
#include "render/shaders/builtin/volclouds_cirrus_frag.h"
#include "render/shaders/builtin/volclouds_composite_frag.h"
#include "render/shaders/builtin/volclouds_frag.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace {
constexpr uint32_t kBaseNoiseSize = 128;  // Perlin-Worley base shape (R8)
constexpr uint32_t kDetailNoiseSize = 32; // Worley erosion octaves (RGBA8)
constexpr uint32_t kCirrusWidth = 512;    // anisotropic streak noise (R8)
constexpr uint32_t kCirrusHeight = 256;
// The cirrus band of volclouds_cirrus.frag (the TDUSC sky puts its cirrus
// far above the cumulus deck - TopCloud 8000, DownCloud 1500).
constexpr float kCirrusBandBottom = 9500.0f;
constexpr float kCirrusBandTop = 10200.0f;
// Shared with volclouds*.frag: the march clamps its distances to this, the
// hit distance travels packed (0..1) and the composite unpacks it.
constexpr float kMaxMarchDistance = 60000.0f;

// ---------------------------------------------------------------- noise ---

uint32_t Hash3( int32_t x, int32_t y, int32_t z, uint32_t seed ) {
    uint32_t h = seed ^ static_cast<uint32_t>( x ) * 0x9E3779B9u;
    h ^= static_cast<uint32_t>( y ) * 0x85EBCA77u;
    h ^= static_cast<uint32_t>( z ) * 0xC2B2AE3Du;
    h = ( h ^ ( h >> 15 ) ) * 0x2545F491u;
    h ^= h >> 13;
    return h;
}

int32_t Wrap( int32_t v, uint32_t frequency ) {
    const int32_t f = static_cast<int32_t>( frequency );
    v %= f;
    return v < 0 ? v + f : v;
}

float Fade( float t ) {
    return t * t * t * ( t * ( t * 6.0f - 15.0f ) + 10.0f );
}

// Perlin's 12 edge directions.
const float kGradients[12][3] = { { 1, 1, 0 },  { -1, 1, 0 },  { 1, -1, 0 }, { -1, -1, 0 },
                                  { 1, 0, 1 },  { -1, 0, 1 },  { 1, 0, -1 }, { -1, 0, -1 },
                                  { 0, 1, 1 },  { 0, -1, 1 },  { 0, 1, -1 }, { 0, -1, -1 } };

// Tileable gradient noise, roughly [-1, 1].
float TileablePerlin( float x, float y, float z, uint32_t frequency, uint32_t seed ) {
    const float px = x * static_cast<float>( frequency );
    const float py = y * static_cast<float>( frequency );
    const float pz = z * static_cast<float>( frequency );
    const int32_t ix = static_cast<int32_t>( floorf( px ) );
    const int32_t iy = static_cast<int32_t>( floorf( py ) );
    const int32_t iz = static_cast<int32_t>( floorf( pz ) );
    const float fx = px - static_cast<float>( ix );
    const float fy = py - static_cast<float>( iy );
    const float fz = pz - static_cast<float>( iz );
    const float u = Fade( fx ), v = Fade( fy ), w = Fade( fz );
    auto dot = [&]( int32_t cx, int32_t cy, int32_t cz, float dx, float dy, float dz ) {
        const float *g = kGradients[Hash3( Wrap( cx, frequency ), Wrap( cy, frequency ), Wrap( cz, frequency ), seed ) % 12];
        return g[0] * dx + g[1] * dy + g[2] * dz;
    };
    const float n000 = dot( ix, iy, iz, fx, fy, fz );
    const float n100 = dot( ix + 1, iy, iz, fx - 1.0f, fy, fz );
    const float n010 = dot( ix, iy + 1, iz, fx, fy - 1.0f, fz );
    const float n110 = dot( ix + 1, iy + 1, iz, fx - 1.0f, fy - 1.0f, fz );
    const float n001 = dot( ix, iy, iz + 1, fx, fy, fz - 1.0f );
    const float n101 = dot( ix + 1, iy, iz + 1, fx - 1.0f, fy, fz - 1.0f );
    const float n011 = dot( ix, iy + 1, iz + 1, fx, fy - 1.0f, fz - 1.0f );
    const float n111 = dot( ix + 1, iy + 1, iz + 1, fx - 1.0f, fy - 1.0f, fz - 1.0f );
    const float x00 = n000 + u * ( n100 - n000 );
    const float x10 = n010 + u * ( n110 - n010 );
    const float x01 = n001 + u * ( n101 - n001 );
    const float x11 = n011 + u * ( n111 - n011 );
    const float y0 = x00 + v * ( x10 - x00 );
    const float y1 = x01 + v * ( x11 - x01 );
    return y0 + w * ( y1 - y0 );
}

// Tileable Worley F1, returned inverted (1 = at a cell centre, 0 = far).
float TileableWorley( float x, float y, float z, uint32_t frequency, uint32_t seed ) {
    const float px = x * static_cast<float>( frequency );
    const float py = y * static_cast<float>( frequency );
    const float pz = z * static_cast<float>( frequency );
    const int32_t cx = static_cast<int32_t>( floorf( px ) );
    const int32_t cy = static_cast<int32_t>( floorf( py ) );
    const int32_t cz = static_cast<int32_t>( floorf( pz ) );
    float best = 8.0f;
    for ( int32_t dz = -1; dz <= 1; dz++ ) {
        for ( int32_t dy = -1; dy <= 1; dy++ ) {
            for ( int32_t dx = -1; dx <= 1; dx++ ) {
                const int32_t nx = cx + dx, ny = cy + dy, nz = cz + dz;
                const uint32_t h = Hash3( Wrap( nx, frequency ), Wrap( ny, frequency ), Wrap( nz, frequency ), seed );
                const float ox = static_cast<float>( nx ) + static_cast<float>( h & 1023 ) / 1023.0f;
                const float oy = static_cast<float>( ny ) + static_cast<float>( ( h >> 10 ) & 1023 ) / 1023.0f;
                const float oz = static_cast<float>( nz ) + static_cast<float>( ( h >> 20 ) & 1023 ) / 1023.0f;
                const float ddx = ox - px, ddy = oy - py, ddz = oz - pz;
                const float d = ddx * ddx + ddy * ddy + ddz * ddz;
                if ( d < best ) {
                    best = d;
                }
            }
        }
    }
    return Clamp( 1.0f - sqrtf( best ), 0.0f, 1.0f );
}

// Perlin-Worley base shape, one octave of large Perlin remapped against a
// Worley fbm (the Horizon Zero Dawn shaping [INF]).
void GenerateBaseVolume( std::vector<uint8_t> &out ) {
    out.resize( static_cast<size_t>( kBaseNoiseSize ) * kBaseNoiseSize * kBaseNoiseSize );
    auto slice = [&]( uint32_t zBegin, uint32_t zEnd ) {
        for ( uint32_t z = zBegin; z < zEnd; z++ ) {
            for ( uint32_t y = 0; y < kBaseNoiseSize; y++ ) {
                for ( uint32_t x = 0; x < kBaseNoiseSize; x++ ) {
                    const float u = ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( kBaseNoiseSize );
                    const float v = ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( kBaseNoiseSize );
                    const float w = ( static_cast<float>( z ) + 0.5f ) / static_cast<float>( kBaseNoiseSize );
                    const float perlin = TileablePerlin( u, v, w, 4, 0x5BD1E3F1u );
                    // Bias above 0.5: the field stays billowy instead of fading to empty.
                    const float perlin01 = Clamp( perlin * 0.95f + 0.62f, 0.0f, 1.0f );
                    const float worley = 0.625f * TileableWorley( u, v, w, 4, 0x1F3A9D2Bu ) +
                                         0.25f * TileableWorley( u, v, w, 8, 0x6C4B7C11u ) +
                                         0.125f * TileableWorley( u, v, w, 16, 0x2E9F0A47u );
                    // Softened Worley remap (measured: median 0.46, 61 % of the
                    // volume above the overcast coverage threshold - the raw
                    // Perlin-Worley remap centres at 0.18 and the sky stays
                    // empty).
                    const float pw = ( perlin01 - worley * 0.6f ) / Max( 1.0f - worley * 0.6f, 0.001f );
                    out[( static_cast<size_t>( z ) * kBaseNoiseSize + y ) * kBaseNoiseSize + x] =
                        static_cast<uint8_t>( Clamp( pw, 0.0f, 1.0f ) * 255.0f + 0.5f );
                }
            }
        }
    };
    const uint32_t threads = Max<uint32_t>( 1, Min<uint32_t>( 8, std::thread::hardware_concurrency() ) );
    std::vector<std::thread> pool;
    const uint32_t step = ( kBaseNoiseSize + threads - 1 ) / threads;
    for ( uint32_t t = 0; t < threads; t++ ) {
        pool.emplace_back( slice, t * step, Min<uint32_t>( ( t + 1 ) * step, kBaseNoiseSize ) );
    }
    for ( std::thread &thread : pool ) {
        thread.join();
    }
}

// Detail texture: one Worley octave per channel, increasing frequency
// (sampled and summed as a 3-octave fbm by the march shader).
void GenerateDetailVolume( std::vector<uint8_t> &out ) {
    out.resize( static_cast<size_t>( kDetailNoiseSize ) * kDetailNoiseSize * kDetailNoiseSize * 4 );
    for ( uint32_t z = 0; z < kDetailNoiseSize; z++ ) {
        for ( uint32_t y = 0; y < kDetailNoiseSize; y++ ) {
            for ( uint32_t x = 0; x < kDetailNoiseSize; x++ ) {
                const float u = ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( kDetailNoiseSize );
                const float v = ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( kDetailNoiseSize );
                const float w = ( static_cast<float>( z ) + 0.5f ) / static_cast<float>( kDetailNoiseSize );
                uint8_t *pTexel = &out[( ( static_cast<size_t>( z ) * kDetailNoiseSize + y ) * kDetailNoiseSize + x ) * 4];
                pTexel[0] = static_cast<uint8_t>( TileableWorley( u, v, w, 2, 0x71C9F015u ) * 255.0f + 0.5f );
                pTexel[1] = static_cast<uint8_t>( TileableWorley( u, v, w, 4, 0x8A2BE211u ) * 255.0f + 0.5f );
                pTexel[2] = static_cast<uint8_t>( TileableWorley( u, v, w, 8, 0x3D57B9A7u ) * 255.0f + 0.5f );
                pTexel[3] = static_cast<uint8_t>( TileableWorley( u, v, w, 16, 0x9E1D74C3u ) * 255.0f + 0.5f );
            }
        }
    }
}

// Cirrus streaks: anisotropic fbm - the noise varies slowly along x (the
// streak direction) and quickly across it, with a large-scale band
// modulation. The sheet uv wraps forever (the pass drifts it with the wind),
// so every octave stays an integer multiple of the tile frequency.
void GenerateCirrusStreaks( std::vector<uint8_t> &out ) {
    out.resize( static_cast<size_t>( kCirrusWidth ) * kCirrusHeight );
    for ( uint32_t y = 0; y < kCirrusHeight; y++ ) {
        for ( uint32_t x = 0; x < kCirrusWidth; x++ ) {
            const float u = ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( kCirrusWidth );
            const float v = ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( kCirrusHeight );
            // The bands: cirrus arrives in larger systems, not uniform grit.
            const float bands = 0.55f + 0.45f * TileablePerlin( u, v, 0.61f, 2, 0x51D9A3F7u );
            float n = 0.0f, amplitude = 0.55f, sum = 0.0f;
            for ( uint32_t octave = 0; octave < 3; octave++ ) {
                const uint32_t freq = 4u << octave;
                // v stretched x8 (streaks along u): the octave tiles because
                // 8 * freq is an integer.
                n += amplitude * TileablePerlin( u, v * 8.0f, 0.37f, freq, 0x2B7E1C4Du + octave * 0x9E37u );
                sum += amplitude;
                amplitude *= 0.55f;
            }
            n = Clamp( n / sum * bands * 1.35f - 0.10f, 0.0f, 1.0f );
            out[static_cast<size_t>( y ) * kCirrusWidth + x] = static_cast<uint8_t>( n * 255.0f + 0.5f );
        }
    }
}
} // namespace

// ------------------------------------------------------------- lifecycle ---

bool EdenClouds::initialize( RenderDevice &device ) {
    pDevice = &device;
    if ( pBaseNoise == nullptr && !generateNoise() ) {
        pDevice = nullptr;
        return false;
    }
    if ( pCirrusNoise == nullptr && !generateCirrusNoise() ) {
        // The sheet is an addition: without it the deck still works.
        OTDU_LOG_ERROR( "EdenClouds: the cirrus texture failed to create - cirrus disabled\n" );
    }
    if ( pLayerParams == nullptr ) {
        // The march push block is at the 128-byte cap, so the TDUSC opt-outs
        // ride in as a 1x1 float texture (volclouds.frag reads uLayerParams.r
        // as the second-layer amplitude).
        const float layerParams[4] = { CmdLine::HasArg( "-nosecondlayer" ) ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f };
        const void *pData = layerParams;
        const uint32_t size = sizeof( layerParams );
        pLayerParams = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_2D, 1, 1, 1, 1, 1,
                                               VK_FORMAT_R32G32B32A32_SFLOAT, &pData, &size, "CloudLayerParams" );
    }
    return true;
}

bool EdenClouds::generateNoise() {
    std::vector<uint8_t> base;
    GenerateBaseVolume( base );
    // Quantiles of the generated field: `coverage` is the fraction of the sky
    // the deck should cover, so the shader threshold is the matching quantile
    // of the base noise. A prime stride samples the volume without locking onto
    // the tile period.
    {
        std::vector<float> sample;
        sample.reserve( base.size() / 97 + 1 );
        for ( size_t i = 0; i < base.size(); i += 97 ) {
            sample.push_back( static_cast<float>( base[i] ) / 255.0f );
        }
        std::sort( sample.begin(), sample.end() );
        for ( uint32_t i = 0; i <= kCoverageSteps; i++ ) {
            const float coverage = static_cast<float>( i ) / static_cast<float>( kCoverageSteps );
            const float q = 1.0f - coverage; // top (1 - coverage) of the field
            const size_t index = Min<size_t>(
                sample.size() - 1, static_cast<size_t>( q * static_cast<float>( sample.size() - 1 ) + 0.5f ) );
            coverageThresholds[i] = sample[index];
        }
        OTDU_LOG_INFO( "EdenClouds: base noise median %.3f, coverage 0.50 -> threshold %.3f, "
                       "0.86 -> %.3f\n",
                       sample[sample.size() / 2], thresholdForCoverage( 0.5f ),
                       thresholdForCoverage( 0.86f ) );
    }
    std::vector<uint8_t> detail;
    GenerateDetailVolume( detail );
    const void *pBaseData = base.data();
    const uint32_t baseSize = static_cast<uint32_t>( base.size() );
    const void *pDetailData = detail.data();
    const uint32_t detailSize = static_cast<uint32_t>( detail.size() );
    pBaseNoise = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_3D, kBaseNoiseSize, kBaseNoiseSize, kBaseNoiseSize, 1, 1,
                                         VK_FORMAT_R8_UNORM, &pBaseData, &baseSize, "CloudBaseNoise" );
    pDetailNoise = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_3D, kDetailNoiseSize, kDetailNoiseSize, kDetailNoiseSize,
                                           1, 1, VK_FORMAT_R8G8B8A8_UNORM, &pDetailData, &detailSize,
                                           "CloudDetailNoise" );
    if ( pBaseNoise == nullptr || pDetailNoise == nullptr ) {
        OTDU_LOG_ERROR( "EdenClouds: the 3D noise textures failed to create - volumetric clouds disabled\n" );
        destroy();
        return false;
    }
    return true;
}

bool EdenClouds::generateCirrusNoise() {
    std::vector<uint8_t> streaks;
    GenerateCirrusStreaks( streaks );
    const void *pData = streaks.data();
    const uint32_t size = static_cast<uint32_t>( streaks.size() );
    pCirrusNoise = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_2D, kCirrusWidth, kCirrusHeight, 1, 1, 1,
                                           VK_FORMAT_R8_UNORM, &pData, &size, "CloudCirrusNoise" );
    return pCirrusNoise != nullptr;
}

void EdenClouds::destroy() {
    destroyTargets();
    if ( pDevice != nullptr ) {
        pDevice->destroyTexture( pBaseNoise );
        pDevice->destroyTexture( pDetailNoise );
        pDevice->destroyTexture( pCirrusNoise );
        pDevice->destroyTexture( pLayerParams );
    }
    pBaseNoise = nullptr;
    pDetailNoise = nullptr;
    pCirrusNoise = nullptr;
    pLayerParams = nullptr;
    fullscreen.destroy();
    pDevice = nullptr;
    bWeatherInitialized = false;
}

void EdenClouds::ensureTargets( uint32_t sceneWidth, uint32_t sceneHeight ) {
    if ( pDevice == nullptr ) {
        return;
    }
    const uint32_t width = Max<uint32_t>( 64, ( sceneWidth + 1 ) / 2 );
    const uint32_t height = Max<uint32_t>( 64, ( sceneHeight + 1 ) / 2 );
    if ( pMarchTarget != nullptr && pMarchTarget->Width == width && pMarchTarget->Height == height ) {
        return;
    }
    destroyTargets();
    // RGBA32F, not RGBA16F: the march packs alpha and the hit distance as two
    // halves inside the alpha channel (packHalf2x16), and a 16-bit float
    // attachment converts that bit pattern as a *number*, destroying both
    // values - the composite then always read alpha 0 and drew nothing (the
    // reason v3 looked empty on screen while -showclouds showed the march).
    // 32-bit float storage round-trips the packed pattern exactly.
    pMarchTarget = pDevice->createRenderTarget( width, height, VK_FORMAT_R32G32B32A32_SFLOAT, false, "CloudMarch" );
    pHistory[0] = pDevice->createRenderTarget( width, height, VK_FORMAT_R32G32B32A32_SFLOAT, false, "CloudHistoryA" );
    pHistory[1] = pDevice->createRenderTarget( width, height, VK_FORMAT_R32G32B32A32_SFLOAT, false, "CloudHistoryB" );
    bHistoryValid = false; // the history is gone: the next accumulate restarts
}

void EdenClouds::destroyTargets() {
    if ( pDevice == nullptr ) {
        return;
    }
    pDevice->destroyRenderTarget( pMarchTarget );
    pDevice->destroyRenderTarget( pHistory[0] );
    pDevice->destroyRenderTarget( pHistory[1] );
    pDevice->destroyRenderTarget( pCloudDepthTarget );
    pMarchTarget = nullptr;
    pHistory[0] = nullptr;
    pHistory[1] = nullptr;
    pCloudDepthTarget = nullptr;
    bHistoryValid = false;
}

void EdenClouds::copySceneDepth( RenderDevice &device, GPUTexture *pSceneDepth ) {
    if ( pDevice == nullptr || pSceneDepth == nullptr ) {
        return;
    }
    pDevice = &device;
    if ( pCloudDepthTarget == nullptr || pCloudDepthTarget->Width != pSceneDepth->Width ||
         pCloudDepthTarget->Height != pSceneDepth->Height ) {
        pDevice->destroyRenderTarget( pCloudDepthTarget );
        pCloudDepthTarget = pDevice->createRenderTarget( pSceneDepth->Width, pSceneDepth->Height,
                                                         VK_FORMAT_R32_SFLOAT, false, "CloudDepth" );
    }
    device.beginGpuPass( "clouddepth" );
    device.beginRenderTarget( pCloudDepthTarget, true );
    fullscreen.draw( device, kSPIRV_depthcopy_frag, sizeof( kSPIRV_depthcopy_frag ), &pSceneDepth, 1, nullptr, 0,
                     eBlendMode::BM_Opaque, eSamplerFilter::SF_Point );
    device.endRenderTarget();
    device.endGpuPass();
}

float EdenClouds::thresholdForCoverage( float coverage ) const {
    const float c = Clamp( coverage, 0.0f, 1.0f );
    // coverageThresholds[i] already holds the quantile for coverage i / steps (the
    // table is filled from the top of the sorted field), so it is indexed by
    // the coverage itself. The first version indexed it by 1 - coverage, which
    // inverted the weather: the "overcast" preset had FEWER clouds than fair
    // weather. 0.38c keeps a lot of the field clear even at full cover.
    const float position = Clamp( 0.38f * c, 0.0f, 1.0f ) * static_cast<float>( kCoverageSteps );
    const uint32_t index = static_cast<uint32_t>( position );
    const float fraction = position - static_cast<float>( index );
    const uint32_t next = Min<uint32_t>( index + 1, kCoverageSteps );
    return coverageThresholds[index] + ( coverageThresholds[next] - coverageThresholds[index] ) * fraction;
}

void EdenClouds::setWeatherTargets( float newCoverage, float newDarkness, float newBase, float newTop ) {
    targetCoverage = Clamp( newCoverage, 0.0f, 1.0f );
    targetDarkness = Clamp( newDarkness, 0.0f, 1.0f );
    targetBase = Max( newBase, 50.0f );
    targetTop = Max( newTop, targetBase + 200.0f );
}

// ---------------------------------------------------------------- passes ---

void EdenClouds::marchAndAccumulate( RenderDevice &device, const Params &params ) {
    if ( pDevice == nullptr || pBaseNoise == nullptr || pMarchTarget == nullptr || pHistory[0] == nullptr ||
         pHistory[1] == nullptr ) {
        return;
    }
    pDevice = &device;
    // The weather parameters approach their targets exponentially: the
    // string clouds_cloud3dname switches in the middle of a weather blend,
    // so this is what keeps the coverage / darkness / slab continuous.
    if ( !bWeatherInitialized ) {
        coverage = targetCoverage;
        darkness = targetDarkness;
        base = targetBase;
        top = targetTop;
        bWeatherInitialized = true;
    } else {
        const float dt = Clamp( params.frameDelta, 0.0f, 0.1f );
        const float shape = 1.0f - expf( -dt / 4.0f );
        const float slab = 1.0f - expf( -dt / 6.0f );
        coverage += ( targetCoverage - coverage ) * shape;
        darkness += ( targetDarkness - darkness ) * shape;
        base += ( targetBase - base ) * slab;
        top += ( targetTop - top ) * slab;
    }

    // Pass 1: the march, half res. The camera basis is pre-scaled like the
    // first volumetric version (right * tanHalf * aspect, up * tanHalf).
    const float push[32] = {
        params.camPos.x, params.camPos.y, params.camPos.z, params.time,
        params.right.x * params.tanHalf * params.aspect, params.right.y * params.tanHalf * params.aspect,
        params.right.z * params.tanHalf * params.aspect, coverage,
        params.up.x * params.tanHalf, params.up.y * params.tanHalf, params.up.z * params.tanHalf, base,
        params.forward.x, params.forward.y, params.forward.z, top,
        params.toSun.x, params.toSun.y, params.toSun.z, darkness,
        params.sunColor.x, params.sunColor.y, params.sunColor.z, params.windX,
        params.ambient.x, params.ambient.y, params.ambient.z, params.windZ,
        params.fogColor.x, params.fogColor.y, params.fogColor.z, thresholdForCoverage( coverage ),
    };
    GPUTexture *pMarchTextures[3] = { pBaseNoise, pDetailNoise, pLayerParams };
    device.beginGpuPass( "clouds" );
    device.beginRenderTarget( pMarchTarget, false );
    // The 3D noise tiles are seamless: sample them wrapped (the default clamp
    // would flatten everything beyond half a tile).
    fullscreen.draw( device, kSPIRV_volclouds_frag, sizeof( kSPIRV_volclouds_frag ), pMarchTextures, 3, push,
                     sizeof( push ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Linear, false, false,
                     eSamplerAddress::SA_Wrap );
    device.endRenderTarget();

    // Pass 2: temporal accumulation. The history is reprojected with the
    // previous camera; a camera jump (or the first frame) resets it.
    const Vector3f cameraDelta = params.camPos - prevCamPos;
    const bool bJump = bHistoryValid && cameraDelta.dot( cameraDelta ) > 2000.0f * 2000.0f;
    // -cloudnohistory: skip the temporal reprojection (history weight 0, reset
    // every frame). A/B that separates "the march found no cloud" from "the
    // accumulation ate it".
    static const bool bNoCloudHistory = CmdLine::HasArg( "-cloudnohistory" );
    const uint32_t writeIndex = historyIndex ^ 1;
    GPUTexture *pAccumTextures[2] = { &pMarchTarget->Color, &pHistory[historyIndex]->Color };
    static const bool bShowMarchAlpha = CmdLine::HasArg( "-showcloudmarchalpha" );
    const float accumPush[32] = {
        params.camPos.x, params.camPos.y, params.camPos.z, bNoCloudHistory ? 0.0f : 0.88f, // history weight
        params.right.x, params.right.y, params.right.z, params.tanHalf,
        params.up.x, params.up.y, params.up.z, params.aspect,
        params.forward.x, params.forward.y, params.forward.z,
        ( !bHistoryValid || bJump || bNoCloudHistory ) ? 1.0f : 0.0f,
        prevCamPos.x, prevCamPos.y, prevCamPos.z, prevTanHalf,
        prevRight.x, prevRight.y, prevRight.z, prevAspect,
        prevUp.x, prevUp.y, prevUp.z, bShowMarchAlpha ? 1.0f : 0.0f,
        prevFwd.x, prevFwd.y, prevFwd.z, 0.0f,
    };
    device.beginRenderTarget( pHistory[writeIndex], false );
    fullscreen.draw( device, kSPIRV_volclouds_accum_frag, sizeof( kSPIRV_volclouds_accum_frag ), pAccumTextures, 2,
                     accumPush, sizeof( accumPush ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Linear );
    device.endRenderTarget();
    device.endGpuPass();
    historyIndex = writeIndex;
    bHistoryValid = true;
    prevCamPos = params.camPos;
    prevRight = params.right;
    prevUp = params.up;
    prevFwd = params.forward;
    prevTanHalf = params.tanHalf;
    prevAspect = params.aspect;
}

void EdenClouds::composite( RenderDevice &device, const Params &params ) {
    if ( pDevice == nullptr || !bHistoryValid || pHistory[historyIndex] == nullptr || pCloudDepthTarget == nullptr ) {
        return;
    }
    pDevice = &device;
    GPUTexture *pTextures[2] = { &pHistory[historyIndex]->Color, &pCloudDepthTarget->Color };
    // The diagnostic / A/B selector the composite shader reads in pc.Planes.z:
    // 1 -showcloudalpha (alpha + both depths as colour), 2 -noclouddepth (skip
    // the depth rejection), 3 -clouddepthflip (sample the distance copy
    // vertically flipped). 0 = the shipping path.
    static const bool bShowAlpha = CmdLine::HasArg( "-showcloudalpha" );
    static const bool bNoCloudDepth = CmdLine::HasArg( "-noclouddepth" );
    static const bool bCloudDepthFlip = CmdLine::HasArg( "-clouddepthflip" );
    const float mode = bShowAlpha ? 1.0f : bNoCloudDepth ? 2.0f : bCloudDepthFlip ? 3.0f : 0.0f;
    const float push[4] = { params.nearPlane, params.farPlane, mode, 0.0f };
    // The composite rejects the cloud against the copied opaque depth (the
    // shader), blending the premultiplied colour over the scene.
    // The composite is drawn inside the blended pass, where resumeScene() has
    // set the D3D-style flipped viewport for the scene draws. A fullscreen pass
    // under that viewport maps texcoord (0,0) to the BOTTOM row, which flipped
    // the whole layer (clouds for up-rays landed on the ground) and inverted the
    // depth test. Draw it with a plain Vulkan viewport, then restore.
    const VkViewport savedViewport = device.getCurrentViewport();
    device.setViewport( VkViewport{ 0.0f, 0.0f, std::abs( savedViewport.width ),
                                   std::abs( savedViewport.height ), 0.0f, 1.0f } );
    fullscreen.draw( device, kSPIRV_volclouds_composite_frag, sizeof( kSPIRV_volclouds_composite_frag ), pTextures, 2,
                     push, sizeof( push ), eBlendMode::BM_PremultipliedAlpha, eSamplerFilter::SF_Linear );
    device.setViewport( savedViewport );
}

void EdenClouds::drawCirrus( RenderDevice &device, const Params &params ) {
    if ( pDevice == nullptr || pCirrusNoise == nullptr || pCloudDepthTarget == nullptr ) {
        return;
    }
    // -nocirrus: the whole sheet is an A/B-able addition (TDUSC port).
    static const bool bNoCirrus = CmdLine::HasArg( "-nocirrus" );
    if ( bNoCirrus ) {
        return;
    }
    pDevice = &device;
    GPUTexture *pTextures[2] = { pCirrusNoise, &pCloudDepthTarget->Color };
    // The smoothed storm scalar (marchAndAccumulate ran earlier this frame)
    // carries the template's Rainy Cloud coupling: storms dissolve the sheet.
    const float push[32] = {
        params.camPos.x, params.camPos.y, params.camPos.z, params.time,
        params.right.x * params.tanHalf * params.aspect, params.right.y * params.tanHalf * params.aspect,
        params.right.z * params.tanHalf * params.aspect, params.tanHalf,
        params.forward.x, params.forward.y, params.forward.z, 0.0f,
        params.up.x, params.up.y, params.up.z, 0.0f,
        params.toSun.x, params.toSun.y, params.toSun.z, darkness,
        params.sunColor.x, params.sunColor.y, params.sunColor.z, 1.0f, // w = cirrus amount
        params.nearPlane, params.farPlane, 0.0f, 0.0f,
        params.windX, params.windZ, kCirrusBandBottom, kCirrusBandTop,
    };
    // Same flipped-viewport trap as the composite: draw with a plain Vulkan
    // viewport inside the blended pass, then restore.
    const VkViewport savedViewport = device.getCurrentViewport();
    device.setViewport( VkViewport{ 0.0f, 0.0f, std::abs( savedViewport.width ),
                                   std::abs( savedViewport.height ), 0.0f, 1.0f } );
    // The noise tiles forever, so the drift samples wrapped; the depth copy
    // clamps (the shader insets its uv off the wrap boundary).
    fullscreen.draw( device, kSPIRV_volclouds_cirrus_frag, sizeof( kSPIRV_volclouds_cirrus_frag ), pTextures, 2,
                     push, sizeof( push ), eBlendMode::BM_PremultipliedAlpha, eSamplerFilter::SF_Linear, false,
                     false, eSamplerAddress::SA_Wrap );
    device.setViewport( savedViewport );
}
