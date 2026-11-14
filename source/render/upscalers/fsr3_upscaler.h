#pragma once

#include "render/vulkan/render_device.h"

// AMD FidelityFX Super Resolution 3 upscaler (temporal) through the FidelityFX
// API: an optional runtime plugin. It loads amd_fidelityfx_loader_vk.dll (and
// the amd_fidelityfx_upscaler_vk.dll the loader pulls in) from the executable's
// folder or third_party/fidelityfx-api/bin; those signed binaries are AMD's and
// are not part of the repository (their licence: third_party/fidelityfx-api/
// LICENSE.md). Without them the option is simply unavailable.
//
// Inputs: the scene colour at render resolution, its depth, the velocity buffer
// (EdenSceneView::runVelocity, uv units) and the projection jitter; output: a
// window sized colour image. Docs: docs/re/advanced_graphics.md.
class Fsr3Upscaler {
public:
    struct Inputs {
        GPUTexture *pColor = nullptr;   // SHADER_READ_ONLY
        GPUTexture *pDepth = nullptr;   // R32_SFLOAT colour copy of the depth, SHADER_READ_ONLY
        GPUTexture *pMotion = nullptr;  // SHADER_READ_ONLY, uv units (previous - current)
        GPUTexture *pOutput = nullptr;  // STORAGE capable; any layout, left SHADER_READ_ONLY
        bool bOutputDefined = false;    // the output holds an image (not UNDEFINED)
        uint32_t RenderWidth = 0, RenderHeight = 0;
        uint32_t OutputWidth = 0, OutputHeight = 0;
        float JitterX = 0.0f, JitterY = 0.0f; // pixels, the offset applied to the projection
        float FrameTimeMs = 16.6f;
        float Near = 0.1f, Far = 1000.0f, FovY = 1.0f;
        float Sharpness = 0.0f; // 0 = off
        bool bReset = false;
    };

    ~Fsr3Upscaler() { destroy(); }
    // True when the DLLs were found and a context exists for these sizes.
    bool ensure( RenderDevice &device, uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth,
                 uint32_t outputHeight );
    bool dispatch( RenderDevice &device, const Inputs &inputs );
    void destroy();
    // The DLL could not be loaded or the context could not be created (stays
    // false-y until the sizes change).
    bool hasFailed() const { return bFailed; }

private:
    void *pModule = nullptr;
    void *pContext = nullptr;
    // ffxFunctions: five entry points.
    void *pFunctions[5] = {};
    uint32_t contextRenderWidth = 0, contextRenderHeight = 0, contextOutputWidth = 0, contextOutputHeight = 0;
    bool bFailed = false;
    bool bLoggedFailure = false;
};
