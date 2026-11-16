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
//  3. composite  - the history is drawn into the scene with a depth test and
//                  gl_FragDepth from the hit distance: mountains occlude the
//                  clouds behind them and the clouds occlude what is behind
//                  them, in both directions (session: replace the fixed
//                  "clouds right after the sky" order of the first two
//                  attempts).
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
    // Half-res march / history targets (call from EdenSceneView::ensureTargets).
    void ensureTargets( uint32_t sceneWidth, uint32_t sceneHeight );
    void destroyTargets();
    // The weather targets (from clouds_cloud3dname): the parameters approach
    // them smoothly, so a weather blend never pops (the retail cloud sets
    // cross-fade the same way, weather_spec 6.2).
    void setWeatherTargets( float coverage, float darkness, float base, float top );
    // Passes 1 + 2 (own GPU passes, call before the opaque pass).
    void marchAndAccumulate( RenderDevice &device, const Params &params );
    // Pass 3, into the render pass currently being recorded (depth tested).
    void composite( RenderDevice &device, const Params &params );

private:
    // Tileable noise volumes, generated on the CPU once (eden_ocean builds
    // its NoiseSlice the same way).
    GPUTexture *pBaseNoise = nullptr;   // R8: Perlin-Worley base shape
    GPUTexture *pDetailNoise = nullptr; // RGBA8: Worley erosion octaves
    bool generateNoise();
    static float tileablePerlin( float x, float y, float z, uint32_t frequency );
    static float tileableWorley( float x, float y, float z, uint32_t frequency );

    RenderDevice *pDevice = nullptr;
    FullscreenPass fullscreen;
    RenderTarget *pMarchTarget = nullptr; // RGBA16F, half res
    RenderTarget *pHistory[2] = {};       // RGBA16F, half res, ping-pong
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
