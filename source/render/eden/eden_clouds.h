#pragma once

#include "render/postfx/fullscreen_pass.h"
#include "render/vulkan/render_device.h"

// Port-native realistic volumetric clouds (resources/shaders/volclouds*.frag)
// [INF: the retail game draws cloud billboards and a SKYCLO dome, no
// volumetric clouds; this is an addition].
//
// Three passes per frame:
//  1. march      - half-res raymarch of the cloud slab into an RGBA16F buffer
//                  (premultiplied colour, alpha + hit distance packed in a),
//                  lit by a Perlin-Worley 3D noise pair generated on the CPU
//                  at startup (the tileable textures the retail-era machines
//                  used hash noise instead of) and a multi-scatter sun march.
//  2. accumulate - the march is reprojected into the previous frame's camera
//                  and blended into a half-res history (TAA style), which
//                  removes the ray-march jitter.
//  3. composite  - drawn in the blended pass right after the renderer's
//                  deferred flush (the sky dome is a deferred draw itself), it
//                  rejects the cloud where a copy of the scene depth (taken
//                  straight after the opaque pass, before the deferred dome
//                  draws land) is nearer than the cloud's hit distance:
//                  mountains and the car / cockpit hide the clouds, the clouds
//                  stay in front of what is behind them. No depth is written.
class EdenClouds {
public:
    // View + weather parameters of the frame (filled by EdenSceneView).
    struct Params {
        Vector3f camPos;
        Vector3f right, up, forward; // camera basis (world, orthonormal)
        float tanHalf = 1.0f;        // tan(fov / 2)
        float aspect = 1.0f;
        Vector3f toSun;
        Vector3f sunColor;
        Vector3f ambient;
        Vector3f fogColor;
        float windX = 0.0f; // m/s
        float windZ = 0.0f;
        float time = 0.0f;      // the scene clock (wind drift, jitter seed)
        float nearPlane = 0.8f; // projection planes (the composite depth)
        float farPlane = 40000.0f;
        float frameDelta = 0.0f; // s, the weather smoothing
    };

    bool initialize( RenderDevice &device );
    void destroy();
    inline bool isEnabled() const { return pBaseNoise != nullptr; }
    // The debug views (-showclouds / -showcloudhist): the march result and the
    // newest accumulated history, raw.
    inline GPUTexture *getMarchColor() const { return pMarchTarget != nullptr ? &pMarchTarget->Color : nullptr; }
    inline GPUTexture *getHistoryColor() const {
        return pHistory[historyIndex] != nullptr ? &pHistory[historyIndex]->Color : nullptr;
    }
    // The scene-res R32F copy of the opaque depth (copySceneDepth): sampled by
    // the composite and by the cirrus sheet / height fog passes.
    inline GPUTexture *getSceneDepthCopy() const {
        return pCloudDepthTarget != nullptr ? &pCloudDepthTarget->Color : nullptr;
    }
    // Half-res march / history targets (call from EdenSceneView::ensureTargets).
    void ensureTargets( uint32_t sceneWidth, uint32_t sceneHeight );
    void destroyTargets();
    // The weather targets (from clouds_cloud3dname): the parameters approach
    // them smoothly, so a weather blend never pops (the retail cloud sets
    // cross-fade the same way, weather_spec 6.2).
    void setWeatherTargets( float coverage, float darkness, float base, float top );
    // Passes 1 + 2 (own GPU passes, call before the opaque pass).
    void marchAndAccumulate( RenderDevice &device, const Params &params );
    // Copies the scene depth for the composite's manual depth test (call
    // between passes, after the opaque pass: the depth holds the world and
    // the car, but not the deferred sky dome draws).
    void copySceneDepth( RenderDevice &device, GPUTexture *pSceneDepth );
    // Pass 3, into the render pass currently being recorded (the blended pass,
    // after the deferred flush and before the haze).
    void composite( RenderDevice &device, const Params &params );
    // The high cirrus sheet (TDUSC Cloud Cirrus Texture port): a 2D band at
    // 6500-7200 m, drawn into the render pass currently being recorded (the
    // blended pass, right after the deferred flush, BEFORE composite so the
    // deck draws over it). Depth-rejects against the copySceneDepth copy.
    void drawCirrus( RenderDevice &device, const Params &params );

private:
    // Tileable noise volumes, generated on the CPU once (eden_ocean builds
    // its NoiseSlice the same way).
    GPUTexture *pBaseNoise = nullptr;   // R8: Perlin-Worley base shape
    GPUTexture *pDetailNoise = nullptr; // RGBA8: Worley erosion octaves
    GPUTexture *pCirrusNoise = nullptr; // R8 512x256: anisotropic streaks
    GPUTexture *pLayerParams = nullptr; // RGBA32F 1x1: the march push block is
                                        // at the 128-byte cap, so the
                                        // -nosecondlayer gate rides in as a
                                        // texture (uLayerParams.r)
    bool generateNoise();
    bool generateCirrusNoise();
    static float tileablePerlin( float x, float y, float z, uint32_t frequency );
    static float tileableWorley( float x, float y, float z, uint32_t frequency );
    // coverage (0 clear .. 1 overcast) -> the base-noise threshold that leaves
    // exactly that fraction of the field above it. Measured from the generated
    // volume (quantiles), not a hand-tuned constant: the old linear
    // mix(0.72, 0.34, coverage) made the 0.5 default almost clear.
    static constexpr uint32_t kCoverageSteps = 64;
    float coverageThresholds[kCoverageSteps + 1] = {};
    float thresholdForCoverage( float coverage ) const;

    RenderDevice *pDevice = nullptr;
    FullscreenPass fullscreen;
    RenderTarget *pMarchTarget = nullptr;     // RGBA16F, half res
    RenderTarget *pHistory[2] = {};           // RGBA16F, half res, ping-pong
    RenderTarget *pCloudDepthTarget = nullptr; // R32F, scene size: the opaque depth for the composite
    uint32_t historyIndex = 0;
    bool bHistoryValid = false;
    // Previous frame's camera (the reprojection).
    Vector3f prevCamPos;
    Vector3f prevRight, prevUp, prevFwd;
    float prevTanHalf = 0.0f;
    float prevAspect = 0.0f;
    // The smoothed weather parameters and their targets.
    float coverage = 0.5f;
    float darkness = 0.0f;
    float base = 1000.0f;
    float top = 2100.0f;
    float targetCoverage = 0.5f;
    float targetDarkness = 0.0f;
    float targetBase = 1000.0f;
    float targetTop = 2100.0f;
    bool bWeatherInitialized = false;
};
