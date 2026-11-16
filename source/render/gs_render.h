#pragma once

#include "game/gs_base.h"
#include "render/d3d12/d3d12_device.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_surface_fx.h"
#include "render/renderer_2d.h"
#include "render/rhi/render_backend.h"
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

    // The backend this run uses: -renderer <name> / OTDU_RENDERER
    // (docs/backends.md), Vulkan when neither is given.
    inline eRenderBackend getBackend() const { return backend; }
    // True when the backend can draw the game (scene, 2D UI, Flash). The
    // Direct3D backends present the swapchain only for now, so the frame loop
    // asks this before drawing anything (tdu_instance.cpp).
    inline bool canDrawScene() const { return RenderBackend::CanRenderScene( backend ); }
    // True when the ImGui overlay can draw (the debug UI, the build watermark,
    // the Options panels): Vulkan through the 2D renderer, D3D12 through
    // imgui_impl_dx12 - which is opt-in with -d3d12-ui until the driver stall
    // in docs/backends.md section 6 is understood.
    inline bool canDrawUI() const {
        return backend == eRenderBackend::Vulkan ||
               ( backend == eRenderBackend::D3D12 && bD3D12Ui );
    }
    // The D3D12 device, or nullptr on another backend.
    inline D3D12Device *getD3D12Device() {
        return backend == eRenderBackend::D3D12 ? &d3d12Device : nullptr;
    }
    // The presented size, whichever backend runs: the Flash stage layout and the
    // ImGui viewport need it (they used to read the Vulkan device directly).
    inline uint32_t getBackbufferWidth() const {
        return backend == eRenderBackend::D3D12 ? d3d12Device.getBackbufferWidth()
                                                : device.getBackbufferWidth();
    }
    inline uint32_t getBackbufferHeight() const {
        return backend == eRenderBackend::D3D12 ? d3d12Device.getBackbufferHeight()
                                                : device.getBackbufferHeight();
    }
    // Same question, from a backend pointer that does not know what it drives.
    inline const char *getBackendName() const { return RenderBackend::GetName( backend ); }

    bool beginFrame();
    void endFrame();
    // The main (swapchain) pass of the frame, whichever backend runs it.
    void beginMainPass();
    // -autoplay shot=<path> / the photo camera: the backend writes the file.
    void requestScreenshot( const char *pPath );
    // A lost device (Vulkan or Direct3D): the game loop leaves on it.
    bool hasDeviceLost() const;
    // Waits for the GPU (game mode changes, teardown).
    void waitIdle();

    // Ray tracing (-rtworld): builds the frame's top level structure from what
    // the scene recorded and traces a probe ray from the camera. A no-op
    // unless -rtworld was passed.
    void updateRayTracing();

    void setClearColor( const ColorRGBA &color ) { clearColor = color; }
    void onWindowResized() {
        if ( backend == eRenderBackend::D3D12 ) {
            d3d12Device.onWindowResized();
        } else {
            device.requestSwapchainRecreation();
        }
    }

private:
    // Vulkan: the complete renderer. Direct3D: the swapchain only, so far.
    eRenderBackend backend = eRenderBackend::Vulkan;
    // -d3d12-ui: draw the ImGui overlay on the D3D12 backend (opt-in, see
    // docs/backends.md section 6).
    bool bD3D12Ui = false;
    RenderDevice device;
    D3D12Device d3d12Device;
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
