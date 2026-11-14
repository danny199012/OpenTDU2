#pragma once

#include "render/vulkan/render_device.h"

// NVIDIA DLSS super resolution through the NGX SDK (statically linked
// nvsdk_ngx_d.lib + the nvngx_dlss.dll the SDK ships): an optional plugin for
// RTX GPUs. NVIDIA's headers, library and DLL are not redistributable from this
// repository (third_party/dlss-sdk/LICENSE.txt): unpack them into
// third_party/dlss-sdk/{include,lib,bin}; CMake then defines OTDU_HAVE_DLSS.
// Without them this file compiles to a stub.
//
// NGX asks for Vulkan instance and device extensions *before* they are
// created, so (like XeSS) the choice takes effect after a restart
// (options.ini upscaler = 5, or -dlss).
class DlssUpscaler {
public:
    static bool IsCompiledIn();
    // True when the option is selected (options.ini / -dlss) and the SDK is built in.
    static bool WantedAtStartup();
    // Instance / device extensions NGX needs (appended when the loader has them).
    static void AppendInstanceExtensions( const std::vector<VkExtensionProperties> &available,
                                          std::vector<const char *> &extensions );
    static void AppendDeviceExtensions( VkInstance instance, VkPhysicalDevice physicalDevice,
                                        const std::vector<VkExtensionProperties> &available,
                                        std::vector<const char *> &extensions );

    struct Inputs {
        GPUTexture *pColor = nullptr;  // SHADER_READ_ONLY
        GPUTexture *pDepth = nullptr;  // R32_SFLOAT colour copy of the depth
        GPUTexture *pMotion = nullptr; // R16G16_SFLOAT, uv units (previous - current)
        GPUTexture *pOutput = nullptr; // STORAGE capable
        bool bOutputDefined = false;
        uint32_t RenderWidth = 0, RenderHeight = 0, OutputWidth = 0, OutputHeight = 0;
        float JitterX = 0.0f, JitterY = 0.0f;
        float FrameTimeMs = 16.6f;
        bool bReset = false;
    };

    ~DlssUpscaler() { destroy(); }
    bool ensure( RenderDevice &device, uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth,
                 uint32_t outputHeight );
    bool dispatch( RenderDevice &device, const Inputs &inputs );
    void destroy();

private:
    void *pHandle = nullptr;     // NVSDK_NGX_Handle*
    void *pParameters = nullptr; // NVSDK_NGX_Parameter*
    void *pDevice = nullptr;     // the VkDevice NGX was initialised with
    bool bInitialised = false;
    bool bFailed = false;
    uint32_t contextRenderWidth = 0, contextRenderHeight = 0, contextOutputWidth = 0, contextOutputHeight = 0;
};
