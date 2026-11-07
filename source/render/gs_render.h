#pragma once

#include "game/gs_base.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_surface_fx.h"
#include "render/renderer_2d.h"
#include "render/rt/ray_tracing.h"
#include "render/texture_cache.h"
#include "render/vulkan/render_device.h"

struct SDL_Window;

// Render service: owns the Vulkan device, the texture cache and the
// renderers (2D batches, 3D scene).
class GSRender : public GameSystem {
public:
    inline RenderDevice &getDevice() { return device; }
    inline TextureCache &getTextureCache() { return textureCache; }
    inline Renderer2D &get2DRenderer() { return renderer2D; }
    // Wheel spray / dust and the tyre-track decals (eden_surface_fx.h). Always
    // there: the game modes decide whether to feed it.
    inline EdenSurfaceFx &getSurfaceFx() { return surfaceFx; }
    // Original shaders renderer (nullptr if unavailable, e.g. -nosob)
    inline EdenRenderer *getEdenRenderer() { return edenRenderer.isReady() ? &edenRenderer : nullptr; }
    // Ray tracing (optional: isReady() is false when the device has none).
    inline RayTracing &getRayTracing() { return rayTracing; }
    // The frame TLAS (-rtworld, rebuilt at endFrame): nullptr when off.
    inline RayTracing::Tlas *getRtTlas() const { return pRtTlas; }
    // Options > Advanced: the world's acceleration structures are being built.
    inline bool isRayTracingWorldActive() const { return bRtWorld; }
    inline bool isFrameActive() const { return bFrameActive; }

public:
    GSRender();
    ~GSRender();

    const char *getName() const override { return "Service : Render"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void terminate() override;

    bool createDevice( SDL_Window *pWindow, bool bVSync, bool bValidation );

    bool beginFrame();
    void endFrame();
    // Ray tracing (-rtworld): builds the frame's top level structure from what
    // the scene recorded and traces a probe ray from the camera. A no-op
    // unless -rtworld was passed.
    void updateRayTracing();

    void setClearColor( const ColorRGBA &color ) { clearColor = color; }
    void onWindowResized() { device.requestSwapchainRecreation(); }

private:
    RenderDevice device;
    TextureCache textureCache;
    Renderer2D renderer2D;
    EdenRenderer edenRenderer;
    EdenSurfaceFx surfaceFx;
    RayTracing rayTracing;
    RayTracing::Tlas *pRtTlas = nullptr;
    // Replaced TLASes (capacity growth while streaming): kept two frames
    // before destroying - the previous ones are still referenced by the
    // recorded command buffers (the -rtao dispatch binds the TLAS in-frame).
    RayTracing::Tlas *pRtTlasRetired[2] = {};
    uint32_t rtRetireIndex = 0;
    uint32_t rtFrame = 0;
    bool bRtWorld = false;
    ColorRGBA clearColor;
    bool bFrameActive;
};

extern GSRender *gpRender;

// Options > Advanced, ray tracing: whether the world's geometry is put into
// acceleration structures (Options > Advanced, or -rtworld). One place, because
// the device setup and every model load have to agree.
bool IsRayTracingWorldEnabled();
