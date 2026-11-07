#pragma once

#include "core/math.h"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class RenderDevice;
struct RenderTarget;
struct GPUBuffer;

// Occlusion culling of the main view (port addition, 2026-11-04; not in the
// original renderer [INF]). docs/PERF_PLAN.md P1b measured that 76-80 % of the
// depth pre-pass draws are hidden behind nearer geometry (-occlusionprobe).
//
// Every frame the static world's pre-pass depth is reduced on the GPU to a
// grid of tile maxima (depthmax.frag: the farthest depth of each tile, ~160
// tiles wide) and copied into a host buffer (a small pool, one per frame in
// flight + the one the worker holds); the worker unpacks a finished one. The pre-pass and the opaque
// pass of a later frame then skip a box whose nearest depth is behind the
// farthest depth of every tile it covers - projected with the GRID's own
// view-projection, i.e. "it was hidden from where the camera was then".
// Conservative where the old view knows nothing: a box off the old screen or
// crossing its near plane is drawn. The grid is 2-3 frames old, so an object
// coming out from behind an occluder can appear that much late; a camera cut
// (big move / turn since the grid) disables the cull until a fresh grid.
// -noocclusion turns it off; -logocclusion logs the culled counts.
class EdenOcclusion {
public:
    struct Grid {
        std::vector<float> TileMax;
        uint32_t TilesX = 0;
        uint32_t TilesY = 0;
        uint32_t TileSize = 8;
        uint32_t Width = 0;
        uint32_t Height = 0;
        Matrix4x4f ViewProjection; // row-vector: clip = [p, 1] * VP
        Vector3f CameraPosition;
        Vector3f CameraForward;
        uint64_t Frame = 0;
        uint32_t WorldEpoch = 0; // EdenWorld::getDrawEpoch when its depth was drawn
        // True when the world box is behind the grid's depth everywhere it covers.
        bool isHidden( const Vector3f &boxMin, const Vector3f &boxMax ) const;
        // True when the horizontal plane y = height can show anywhere on the
        // grid's screen: some tile's farthest depth lies beyond the point
        // where its line of sight meets the plane (at any of the tile's
        // corners). Used to skip the ocean reflection in town.
        bool isPlaneVisible( float height ) const;
    };

    EdenOcclusion() = default;
    ~EdenOcclusion();
    EdenOcclusion( const EdenOcclusion & ) = delete;
    EdenOcclusion &operator=( const EdenOcclusion & ) = delete;

    static bool IsEnabled();

    // Start of the frame's main view: picks up a finished readback (its fence
    // has been waited by beginFrame) and the newest worker grid. Returns the
    // grid usable for this frame's camera, or nullptr (none yet, camera cut).
    // worldEpoch: EdenWorld::getDrawEpoch() - a grid of another drawn set is not used.
    const Grid *beginFrame( RenderDevice &device, const Vector3f &cameraPosition, const Vector3f &cameraForward,
                            uint32_t worldEpoch );
    // Right after the static world's depth pre-pass (outside a render pass):
    // pTileTarget holds the tile maxima of that depth (R32_SFLOAT, one texel
    // per tile of TileSizeFor( depthWidth ) pixels, drawn by depthmax.frag);
    // queue its copy with the view-projection the depth was drawn with.
    void afterPrepass( RenderDevice &device, RenderTarget *pTileTarget, uint32_t depthWidth, uint32_t depthHeight,
                       const Matrix4x4f &viewProjection, const Vector3f &cameraPosition, const Vector3f &cameraForward,
                       uint32_t worldEpoch );
    // The tile size (pixels) for a depth buffer this wide (~160 tiles across).
    static uint32_t TileSizeFor( uint32_t width );
    void destroy( RenderDevice &device );

private:
    struct Readback {
        GPUBuffer *pBuffer = nullptr;
        uint64_t Size = 0;
        bool bInFlight = false;  // the GPU copy is queued
        bool bAtWorker = false;  // the worker reads it
        uint32_t Slot = 0;
        uint64_t DeviceFrame = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
        Matrix4x4f ViewProjection;
        Vector3f CameraPosition;
        Vector3f CameraForward;
        uint64_t Frame = 0;
        uint32_t WorldEpoch = 0;
    };
    void workerLoop();
    void reduce( const Readback &readback, Grid &out ) const;

    std::vector<Readback> readbacks; // pool
    uint64_t frameCounter = 0;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable signal;
    int32_t workIndex = -1; // readback the worker should reduce
    bool bStop = false;
    std::unique_ptr<Grid> pReady;   // newest grid from the worker
    std::unique_ptr<Grid> pCurrent; // grid used by the main thread
};
