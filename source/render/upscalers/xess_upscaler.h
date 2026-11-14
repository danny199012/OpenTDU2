#pragma once

#include "render/vulkan/render_device.h"

// Intel XeSS super resolution (temporal) through libxess.dll: an optional
// runtime plugin. Intel's headers and DLL are not redistributable from this
// repository (third_party/xess-sdk/LICENSE.txt): put the SDK's inc/ and bin/
// folders in third_party/xess-sdk/ to build the plugin (without them this file
// compiles to a stub), and libxess.dll next to the executable or in
// third_party/xess-sdk/bin to run it.
//
// XeSS asks for its Vulkan version / device extensions / device features
// *before* the device exists, so the static functions here are called from
// RenderDevice's createInstance / createDevice when the option is selected
// (options.ini upscaler = 4, or -xess).
class XessUpscaler {
public:
    // True when this build has the plugin code (the SDK headers were present).
    static bool IsCompiledIn();
    // Loads libxess.dll once. False when the build has no plugin or the DLL is missing.
    static bool LoadPlugin();
    static bool IsLibraryLoaded();
    // The Vulkan API version XeSS needs (0 when unknown / not loaded).
    static uint32_t MinVulkanVersion();
    // Device extensions XeSS needs (appended, no duplicates).
    static void AppendDeviceExtensions( VkInstance instance, VkPhysicalDevice physicalDevice,
                                        std::vector<const char *> &extensions );
    // Patches a VkPhysicalDeviceFeatures2 chain with the features XeSS needs.
    static bool PatchDeviceFeatures( VkInstance instance, VkPhysicalDevice physicalDevice, void *pFeatures2Chain );

    struct Inputs {
        GPUTexture *pColor = nullptr;  // SHADER_READ_ONLY
        GPUTexture *pDepth = nullptr;  // R32_SFLOAT colour copy of the depth, SHADER_READ_ONLY
        GPUTexture *pMotion = nullptr; // R16G16_SFLOAT, uv units (previous - current), SHADER_READ_ONLY
        GPUTexture *pOutput = nullptr; // STORAGE capable
        bool bOutputDefined = false;
        uint32_t RenderWidth = 0, RenderHeight = 0, OutputWidth = 0, OutputHeight = 0;
        float JitterX = 0.0f, JitterY = 0.0f;
        bool bReset = false;
    };

    ~XessUpscaler() { destroy(); }
    bool ensure( RenderDevice &device, VkInstance instance, uint32_t renderWidth, uint32_t renderHeight,
                 uint32_t outputWidth, uint32_t outputHeight );
    bool dispatch( RenderDevice &device, const Inputs &inputs );
    void destroy();

private:
    void *pContext = nullptr;
    uint32_t contextRenderWidth = 0, contextRenderHeight = 0, contextOutputWidth = 0, contextOutputHeight = 0;
    bool bFailed = false;
};
