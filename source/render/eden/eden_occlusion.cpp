#include "shared.h"
#include "eden_occlusion.h"

#include "render/vulkan/render_device.h"
#include "system/cmd_line.h"

#include <cfloat>
#include <cmath>

namespace {
// A camera that moved / turned this much since the grid's frame is a cut: the
// old depth says nothing reliable about the new view.
constexpr float kCutDistance = 12.0f;  // m
constexpr float kCutCosine = 0.906f;   // 25 degrees
constexpr uint32_t kTargetTilesAcross = 160;
} // namespace

bool EdenOcclusion::IsEnabled() {
    static const bool bEnabled = !CmdLine::HasArg( "-noocclusion" );
    return bEnabled;
}

EdenOcclusion::~EdenOcclusion() {
    {
        std::lock_guard<std::mutex> lock( mutex );
        bStop = true;
    }
    signal.notify_all();
    if ( worker.joinable() ) {
        worker.join();
    }
}

void EdenOcclusion::destroy( RenderDevice &device ) {
    {
        std::lock_guard<std::mutex> lock( mutex );
        bStop = true;
    }
    signal.notify_all();
    if ( worker.joinable() ) {
        worker.join();
    }
    for ( Readback &readback : readbacks ) {
        if ( readback.pBuffer != nullptr ) {
            device.destroyBuffer( readback.pBuffer );
        }
    }
    readbacks.clear();
    pReady.reset();
    pCurrent.reset();
    workIndex = -1;
    bStop = false;
}

bool EdenOcclusion::Grid::isHidden( const Vector3f &boxMinIn, const Vector3f &boxMaxIn ) const {
    // A safety margin: the box must be clearly behind its occluder. The scene
    // is drawn with a double precision view-projection (world coordinates are
    // ~20 km); this float projection put objects resting against a wall on
    // the wrong side of it (367 faint pixels on the 4K drive). 0.5 m + 0.2 %
    // of the distance from the grid's camera.
    const Vector3f center = ( boxMinIn + boxMaxIn ) * 0.5f;
    const float margin = 0.5f + 0.002f * ( center - CameraPosition ).length();
    const Vector3f pad( margin, margin, margin );
    const Vector3f boxMin = boxMinIn - pad;
    const Vector3f boxMax = boxMaxIn + pad;
    const Matrix4x4f &vp = ViewProjection;
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX, minZ = FLT_MAX;
    for ( int32_t corner = 0; corner < 8; corner++ ) {
        const float x = ( corner & 1 ) ? boxMax.x : boxMin.x;
        const float y = ( corner & 2 ) ? boxMax.y : boxMin.y;
        const float z = ( corner & 4 ) ? boxMax.z : boxMin.z;
        const float w = x * vp.m[0][3] + y * vp.m[1][3] + z * vp.m[2][3] + vp.m[3][3];
        if ( w <= 0.01f ) {
            return false; // crosses the near plane of that view: unknown
        }
        const float inverse = 1.0f / w;
        const float cx = ( x * vp.m[0][0] + y * vp.m[1][0] + z * vp.m[2][0] + vp.m[3][0] ) * inverse;
        const float cy = ( x * vp.m[0][1] + y * vp.m[1][1] + z * vp.m[2][1] + vp.m[3][1] ) * inverse;
        const float cz = ( x * vp.m[0][2] + y * vp.m[1][2] + z * vp.m[2][2] + vp.m[3][2] ) * inverse;
        minX = Min( minX, cx );
        maxX = Max( maxX, cx );
        minY = Min( minY, cy );
        maxY = Max( maxY, cy );
        minZ = Min( minZ, cz );
    }
    // Any part off the old screen: that view did not see it, so it proves nothing.
    if ( minX < -1.0f || maxX > 1.0f || minY < -1.0f || maxY > 1.0f || minZ < 0.0f || minZ >= 1.0f ) {
        return false;
    }
    // D3D clip space and the flipped viewport: NDC y = +1 is pixel row 0.
    const float fw = static_cast<float>( Width ), fh = static_cast<float>( Height );
    const int32_t x0 = static_cast<int32_t>( ( minX * 0.5f + 0.5f ) * fw );
    const int32_t x1 = Min( static_cast<int32_t>( ( maxX * 0.5f + 0.5f ) * fw ), static_cast<int32_t>( Width ) - 1 );
    const int32_t y0 = static_cast<int32_t>( ( 0.5f - maxY * 0.5f ) * fh );
    const int32_t y1 = Min( static_cast<int32_t>( ( 0.5f - minY * 0.5f ) * fh ), static_cast<int32_t>( Height ) - 1 );
    const int32_t tile = static_cast<int32_t>( TileSize );
    for ( int32_t ty = Max( y0, 0 ) / tile; ty <= y1 / tile; ty++ ) {
        const float *pRow = TileMax.data() + static_cast<size_t>( ty ) * TilesX;
        for ( int32_t tx = Max( x0, 0 ) / tile; tx <= x1 / tile; tx++ ) {
            if ( pRow[tx] >= minZ ) {
                return false; // something at or behind the box shows through here
            }
        }
    }
    return true;
}

bool EdenOcclusion::Grid::isPlaneVisible( float height ) const {
    if ( TilesX == 0 || CameraPosition.y <= height + 0.05f ) {
        return true; // no data, or the eye at / under the water: always
    }
    const Matrix4x4f inverse = ViewProjection.inverse();
    // Row-vector: world = [ndc, 1] * inverse(VP), then / w.
    auto unproject = [&inverse]( float x, float y, float z ) {
        const float w = x * inverse.m[0][3] + y * inverse.m[1][3] + z * inverse.m[2][3] + inverse.m[3][3];
        const float scale = std::abs( w ) > 1e-12f ? 1.0f / w : 0.0f;
        return Vector3f( ( x * inverse.m[0][0] + y * inverse.m[1][0] + z * inverse.m[2][0] + inverse.m[3][0] ) * scale,
                         ( x * inverse.m[0][1] + y * inverse.m[1][1] + z * inverse.m[2][1] + inverse.m[3][1] ) * scale,
                         ( x * inverse.m[0][2] + y * inverse.m[1][2] + z * inverse.m[2][2] + inverse.m[3][2] ) * scale );
    };
    const Matrix4x4f &vp = ViewProjection;
    // The depth of the plane along the line of sight through an NDC point
    // (2 = the line never meets the plane in front of the eye).
    auto planeDepth = [&]( float x, float y ) {
        const Vector3f nearPoint = unproject( x, y, 0.0f );
        const Vector3f farPoint = unproject( x, y, 1.0f );
        const float dy = farPoint.y - nearPoint.y;
        if ( std::abs( dy ) < 1e-6f ) {
            return 2.0f;
        }
        const float t = ( height - nearPoint.y ) / dy;
        if ( t < 0.0f || t > 1.0f ) {
            return 2.0f; // behind the eye, or past the far plane (the ocean is clipped there)
        }
        const Vector3f p = nearPoint + ( farPoint - nearPoint ) * t;
        const float w = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
        if ( w <= 0.0f ) {
            return 2.0f;
        }
        return ( p.x * vp.m[0][2] + p.y * vp.m[1][2] + p.z * vp.m[2][2] + vp.m[3][2] ) / w;
    };
    // Corner depths once per grid corner, then each tile takes its nearest.
    const uint32_t cornersX = TilesX + 1, cornersY = TilesY + 1;
    std::vector<float> corner( static_cast<size_t>( cornersX ) * cornersY );
    for ( uint32_t cy = 0; cy < cornersY; cy++ ) {
        const float ndcY = 1.0f - 2.0f * Min( static_cast<float>( cy * TileSize ) / Height, 1.0f );
        for ( uint32_t cx = 0; cx < cornersX; cx++ ) {
            const float ndcX = 2.0f * Min( static_cast<float>( cx * TileSize ) / Width, 1.0f ) - 1.0f;
            corner[static_cast<size_t>( cy ) * cornersX + cx] = planeDepth( ndcX, ndcY );
        }
    }
    for ( uint32_t ty = 0; ty < TilesY; ty++ ) {
        for ( uint32_t tx = 0; tx < TilesX; tx++ ) {
            const size_t c = static_cast<size_t>( ty ) * cornersX + tx;
            const float nearest =
                Min( Min( corner[c], corner[c + 1] ), Min( corner[c + cornersX], corner[c + cornersX + 1] ) );
            if ( nearest <= 1.0f && TileMax[static_cast<size_t>( ty ) * TilesX + tx] >= nearest ) {
                return true;
            }
        }
    }
    return false;
}

void EdenOcclusion::reduce( const Readback &readback, Grid &out ) const {
    // The GPU already reduced the depth to the tile maxima (depthmax.frag):
    // the readback is the tile grid itself, R32 floats row by row.
    out.Width = readback.Width;
    out.Height = readback.Height;
    out.TileSize = TileSizeFor( readback.Width );
    out.TilesX = ( readback.Width + out.TileSize - 1 ) / out.TileSize;
    out.TilesY = ( readback.Height + out.TileSize - 1 ) / out.TileSize;
    out.ViewProjection = readback.ViewProjection;
    out.CameraPosition = readback.CameraPosition;
    out.CameraForward = readback.CameraForward;
    out.Frame = readback.Frame;
    out.WorldEpoch = readback.WorldEpoch;
    const float *pTiles = static_cast<const float *>( readback.pBuffer->pMapped );
    out.TileMax.assign( pTiles, pTiles + static_cast<size_t>( out.TilesX ) * out.TilesY );
}

uint32_t EdenOcclusion::TileSizeFor( uint32_t width ) {
    return Max( 8u, ( width + kTargetTilesAcross - 1 ) / kTargetTilesAcross );
}

void EdenOcclusion::workerLoop() {
    for ( ;; ) {
        int32_t index = -1;
        {
            std::unique_lock<std::mutex> lock( mutex );
            signal.wait( lock, [this]() { return bStop || workIndex >= 0; } );
            if ( bStop ) {
                return;
            }
            index = workIndex;
        }
        auto pGrid = std::make_unique<Grid>();
        reduce( readbacks[index], *pGrid );
        std::lock_guard<std::mutex> lock( mutex );
        readbacks[index].bAtWorker = false;
        workIndex = -1;
        pReady = std::move( pGrid );
    }
}

const EdenOcclusion::Grid *EdenOcclusion::beginFrame( RenderDevice &device, const Vector3f &cameraPosition,
                                                      const Vector3f &cameraForward, uint32_t worldEpoch ) {
    if ( !IsEnabled() ) {
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lock( mutex );
        // A copy whose frame slot came round again is complete (beginFrame
        // waited its fence): hand the newest one to an idle worker.
        int32_t newest = -1;
        for ( int32_t i = 0; i < static_cast<int32_t>( readbacks.size() ); i++ ) {
            Readback &readback = readbacks[i];
            if ( readback.bInFlight && readback.Slot == device.getFrameSlot() &&
                 readback.DeviceFrame < device.getFrameNumber() ) {
                readback.bInFlight = false;
                if ( newest < 0 || readback.Frame > readbacks[newest].Frame ) {
                    newest = i;
                }
            }
        }
        if ( newest >= 0 && workIndex < 0 ) {
            device.invalidateBuffer( readbacks[newest].pBuffer );
            readbacks[newest].bAtWorker = true;
            workIndex = newest;
            if ( !worker.joinable() ) {
                worker = std::thread( [this]() { workerLoop(); } );
            }
            signal.notify_one();
        }
        if ( pReady ) {
            pCurrent = std::move( pReady );
        }
    }
    if ( !pCurrent || pCurrent->WorldEpoch != worldEpoch ) {
        return nullptr;
    }
    const Vector3f moved = cameraPosition - pCurrent->CameraPosition;
    if ( moved.dot( moved ) > kCutDistance * kCutDistance || cameraForward.dot( pCurrent->CameraForward ) < kCutCosine ) {
        return nullptr;
    }
    return pCurrent.get();
}

void EdenOcclusion::afterPrepass( RenderDevice &device, RenderTarget *pTileTarget, uint32_t depthWidth,
                                  uint32_t depthHeight, const Matrix4x4f &viewProjection, const Vector3f &cameraPosition,
                                  const Vector3f &cameraForward, uint32_t worldEpoch ) {
    if ( !IsEnabled() || pTileTarget == nullptr ) {
        return;
    }
    const uint64_t size = static_cast<uint64_t>( pTileTarget->Width ) * pTileTarget->Height * 4;
    std::lock_guard<std::mutex> lock( mutex );
    // Pool: one per frame in flight + the one at the worker.
    if ( readbacks.empty() ) {
        readbacks.resize( RenderDevice::kFramesInFlight + 1 );
    }
    Readback *pFree = nullptr;
    for ( Readback &readback : readbacks ) {
        if ( !readback.bInFlight && !readback.bAtWorker ) {
            pFree = &readback;
            break;
        }
    }
    if ( pFree == nullptr ) {
        return;
    }
    if ( pFree->pBuffer == nullptr || pFree->Size < size ) {
        if ( pFree->pBuffer != nullptr ) {
            device.destroyBuffer( pFree->pBuffer );
        }
        pFree->pBuffer = device.createReadbackBuffer( size, VK_BUFFER_USAGE_TRANSFER_DST_BIT );
        pFree->Size = pFree->pBuffer != nullptr ? size : 0;
        if ( pFree->pBuffer == nullptr ) {
            return;
        }
    }
    if ( !device.copyRenderTargetColor( pTileTarget, 0, 0, pTileTarget->Width, pTileTarget->Height, pFree->pBuffer ) ) {
        return;
    }
    pFree->bInFlight = true;
    pFree->Slot = device.getFrameSlot();
    pFree->DeviceFrame = device.getFrameNumber();
    pFree->Width = depthWidth;
    pFree->Height = depthHeight;
    pFree->ViewProjection = viewProjection;
    pFree->CameraPosition = cameraPosition;
    pFree->CameraForward = cameraForward;
    pFree->Frame = ++frameCounter;
    pFree->WorldEpoch = worldEpoch;
}
