#include "shared.h"
#include "render/upscalers/xess_upscaler.h"

#include "system/cmd_line.h"

#if __has_include( "xess/xess_vk.h" )
#define OTDU_HAVE_XESS 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include "xess/xess.h"
#include "xess/xess_vk.h"

namespace {
HMODULE gXess = nullptr;
bool gTriedLoad = false;
template <typename T> T Proc( const char *pName ) {
    return gXess != nullptr ? reinterpret_cast<T>( GetProcAddress( gXess, pName ) ) : nullptr;
}
// Function pointer types are the header's own prototypes.
decltype( &xessVKGetRequiredInstanceExtensions ) pGetInstanceExtensions = nullptr;
decltype( &xessVKGetRequiredDeviceExtensions ) pGetDeviceExtensions = nullptr;
decltype( &xessVKGetRequiredDeviceFeatures ) pGetDeviceFeatures = nullptr;
decltype( &xessVKCreateContext ) pCreateContext = nullptr;
decltype( &xessVKInit ) pInit = nullptr;
decltype( &xessVKExecute ) pExecute = nullptr;
decltype( &xessDestroyContext ) pDestroyContext = nullptr;
decltype( &xessSetVelocityScale ) pSetVelocityScale = nullptr;
decltype( &xessSetJitterScale ) pSetJitterScale = nullptr;
decltype( &xessGetInputResolution ) pGetInputResolution = nullptr;

xess_quality_settings_t QualityFor( float ratio ) {
    // The nearest preset to the display / render ratio (3.0 / 2.0 / 1.7 / 1.5 / 1.3 / 1.0).
    if ( ratio >= 2.5f ) {
        return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
    }
    if ( ratio >= 1.85f ) {
        return XESS_QUALITY_SETTING_PERFORMANCE;
    }
    if ( ratio >= 1.6f ) {
        return XESS_QUALITY_SETTING_BALANCED;
    }
    if ( ratio >= 1.4f ) {
        return XESS_QUALITY_SETTING_QUALITY;
    }
    if ( ratio >= 1.15f ) {
        return XESS_QUALITY_SETTING_ULTRA_QUALITY;
    }
    return XESS_QUALITY_SETTING_AA;
}

xess_vk_image_view_info View( const GPUTexture &texture ) {
    xess_vk_image_view_info info = {};
    info.imageView = texture.View;
    info.image = texture.Image;
    info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    info.format = texture.Format;
    info.width = texture.Width;
    info.height = texture.Height;
    return info;
}
} // namespace

bool XessUpscaler::IsCompiledIn() { return true; }

bool XessUpscaler::LoadPlugin() {
    if ( gTriedLoad ) {
        return gXess != nullptr;
    }
    gTriedLoad = true;
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW( nullptr, exePath, MAX_PATH );
    const std::filesystem::path exeFolder = std::filesystem::path( exePath ).parent_path();
    const std::filesystem::path candidates[] = { exeFolder / "libxess.dll",
                                                  exeFolder / "../../third_party/xess-sdk/bin/libxess.dll",
                                                  "third_party/xess-sdk/bin/libxess.dll" };
    for ( const std::filesystem::path &candidate : candidates ) {
        std::error_code error;
        if ( std::filesystem::exists( candidate, error ) ) {
            gXess = LoadLibraryExW( std::filesystem::absolute( candidate ).wstring().c_str(), nullptr,
                                    LOAD_WITH_ALTERED_SEARCH_PATH );
            if ( gXess != nullptr ) {
                break;
            }
        }
    }
    if ( gXess == nullptr ) {
        OTDU_LOG_WARN( "[xess] libxess.dll not found (exe folder, third_party/xess-sdk/bin): XeSS is unavailable\n" );
        return false;
    }
    pGetInstanceExtensions = Proc<decltype( pGetInstanceExtensions )>( "xessVKGetRequiredInstanceExtensions" );
    pGetDeviceExtensions = Proc<decltype( pGetDeviceExtensions )>( "xessVKGetRequiredDeviceExtensions" );
    pGetDeviceFeatures = Proc<decltype( pGetDeviceFeatures )>( "xessVKGetRequiredDeviceFeatures" );
    pCreateContext = Proc<decltype( pCreateContext )>( "xessVKCreateContext" );
    pInit = Proc<decltype( pInit )>( "xessVKInit" );
    pExecute = Proc<decltype( pExecute )>( "xessVKExecute" );
    pDestroyContext = Proc<decltype( pDestroyContext )>( "xessDestroyContext" );
    pSetVelocityScale = Proc<decltype( pSetVelocityScale )>( "xessSetVelocityScale" );
    pSetJitterScale = Proc<decltype( pSetJitterScale )>( "xessSetJitterScale" );
    pGetInputResolution = Proc<decltype( pGetInputResolution )>( "xessGetInputResolution" );
    if ( pGetInstanceExtensions == nullptr || pGetDeviceExtensions == nullptr || pGetDeviceFeatures == nullptr ||
         pCreateContext == nullptr || pInit == nullptr || pExecute == nullptr || pDestroyContext == nullptr ) {
        OTDU_LOG_ERROR( "[xess] libxess.dll lacks the Vulkan entry points\n" );
        FreeLibrary( gXess );
        gXess = nullptr;
        return false;
    }
    return true;
}

bool XessUpscaler::IsLibraryLoaded() { return gXess != nullptr; }

uint32_t XessUpscaler::MinVulkanVersion() {
    if ( gXess == nullptr ) {
        return 0;
    }
    uint32_t count = 0, minVersion = 0;
    const char *const *pNames = nullptr;
    if ( pGetInstanceExtensions( &count, &pNames, &minVersion ) != XESS_RESULT_SUCCESS ) {
        return 0;
    }
    OTDU_LOG_INFO( "[xess] needs Vulkan %u.%u and %u instance extensions\n", VK_API_VERSION_MAJOR( minVersion ),
                   VK_API_VERSION_MINOR( minVersion ), count );
    for ( uint32_t i = 0; i < count; i++ ) {
        OTDU_LOG_INFO( "[xess]   instance extension %s (the instance already enables the ones it can)\n", pNames[i] );
    }
    return minVersion;
}

void XessUpscaler::AppendDeviceExtensions( VkInstance instance, VkPhysicalDevice physicalDevice,
                                           std::vector<const char *> &extensions ) {
    if ( gXess == nullptr ) {
        return;
    }
    uint32_t count = 0;
    const char *const *pNames = nullptr;
    if ( pGetDeviceExtensions( instance, physicalDevice, &count, &pNames ) != XESS_RESULT_SUCCESS ) {
        OTDU_LOG_WARN( "[xess] xessVKGetRequiredDeviceExtensions failed\n" );
        return;
    }
    for ( uint32_t i = 0; i < count; i++ ) {
        bool bHave = false;
        for ( const char *pExisting : extensions ) {
            bHave = bHave || strcmp( pExisting, pNames[i] ) == 0;
        }
        OTDU_LOG_INFO( "[xess] device extension %s%s\n", pNames[i], bHave ? " (already enabled)" : "" );
        if ( !bHave ) {
            extensions.push_back( pNames[i] );
        }
    }
}

bool XessUpscaler::PatchDeviceFeatures( VkInstance instance, VkPhysicalDevice physicalDevice, void *pFeatures2Chain ) {
    if ( gXess == nullptr ) {
        return false;
    }
    void *pChain = pFeatures2Chain;
    const xess_result_t result = pGetDeviceFeatures( instance, physicalDevice, &pChain );
    if ( result != XESS_RESULT_SUCCESS ) {
        OTDU_LOG_WARN( "[xess] xessVKGetRequiredDeviceFeatures failed (%d)\n", static_cast<int>( result ) );
        return false;
    }
    for ( const VkBaseOutStructure *pNode = static_cast<const VkBaseOutStructure *>( pChain ); pNode != nullptr;
          pNode = pNode->pNext ) {
        OTDU_LOG_INFO( "[xess] device feature chain: sType %d\n", static_cast<int>( pNode->sType ) );
    }
    return true;
}

bool XessUpscaler::ensure( RenderDevice &device, VkInstance instance, uint32_t renderWidth, uint32_t renderHeight,
                           uint32_t outputWidth, uint32_t outputHeight ) {
    if ( pContext != nullptr && contextRenderWidth == renderWidth && contextRenderHeight == renderHeight &&
         contextOutputWidth == outputWidth && contextOutputHeight == outputHeight ) {
        return true;
    }
    if ( bFailed && contextRenderWidth == renderWidth && contextRenderHeight == renderHeight &&
         contextOutputWidth == outputWidth && contextOutputHeight == outputHeight ) {
        return false;
    }
    destroy();
    contextRenderWidth = renderWidth;
    contextRenderHeight = renderHeight;
    contextOutputWidth = outputWidth;
    contextOutputHeight = outputHeight;
    bFailed = true;
    if ( gXess == nullptr || !device.isXessReady() ) {
        return false; // the device was not created with XeSS's extensions (restart after choosing it)
    }
    xess_context_handle_t context = nullptr;
    xess_result_t result = pCreateContext( instance, device.getPhysicalDevice(), device.getDevice(), &context );
    if ( result != XESS_RESULT_SUCCESS && result != XESS_RESULT_WARNING_OLD_DRIVER ) {
        OTDU_LOG_ERROR( "[xess] xessVKCreateContext failed (%d)\n", static_cast<int>( result ) );
        return false;
    }
    xess_vk_init_params_t init = {};
    init.outputResolution = { outputWidth, outputHeight };
    init.qualitySetting = QualityFor( static_cast<float>( outputWidth ) / static_cast<float>( renderWidth ) );
    // The preset whose own input size is closest to the render size (XeSS 2
    // changed the ratios: Quality is 1.7 here, not the 1.5 of XeSS 1).
    if ( pGetInputResolution != nullptr ) {
        float best = 1e30f;
        for ( int quality = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE; quality <= XESS_QUALITY_SETTING_AA; quality++ ) {
            xess_2d_t expected = {};
            if ( pGetInputResolution( context, &init.outputResolution, static_cast<xess_quality_settings_t>( quality ),
                                      &expected ) != XESS_RESULT_SUCCESS ) {
                continue;
            }
            const float dx = static_cast<float>( expected.x ) - static_cast<float>( renderWidth );
            const float dy = static_cast<float>( expected.y ) - static_cast<float>( renderHeight );
            if ( dx * dx + dy * dy < best ) {
                best = dx * dx + dy * dy;
                init.qualitySetting = static_cast<xess_quality_settings_t>( quality );
            }
        }
    }
    // The final image is tone mapped (LDR); depth is not inverted; the
    // vectors are low resolution, jitter free, in pixels (scaled below).
    init.initFlags = XESS_INIT_FLAG_LDR_INPUT_COLOR | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
    result = pInit( context, &init );
    if ( result != XESS_RESULT_SUCCESS ) {
        OTDU_LOG_ERROR( "[xess] xessVKInit failed (%d)\n", static_cast<int>( result ) );
        pDestroyContext( context );
        return false;
    }
    // Our vectors are previous uv - current uv: scale to render pixels.
    if ( pSetVelocityScale != nullptr ) {
        pSetVelocityScale( context, static_cast<float>( renderWidth ), static_cast<float>( renderHeight ) );
    }
    if ( pGetInputResolution != nullptr ) {
        xess_2d_t expected = {};
        pGetInputResolution( context, &init.outputResolution, init.qualitySetting, &expected );
        OTDU_LOG_INFO( "[xess] quality %d expects input %ux%u, render %ux%u\n", static_cast<int>( init.qualitySetting ),
                       expected.x, expected.y, renderWidth, renderHeight );
    }
    pContext = context;
    bFailed = false;
    OTDU_LOG_INFO( "[xess] context created: render %ux%u -> %ux%u\n", renderWidth, renderHeight, outputWidth,
                   outputHeight );
    return true;
}

bool XessUpscaler::dispatch( RenderDevice &device, const Inputs &inputs ) {
    if ( pContext == nullptr ) {
        return false;
    }
    VkCommandBuffer commandBuffer = device.getCommandBuffer();
    VkImageMemoryBarrier before = {};
    before.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before.srcAccessMask = inputs.bOutputDefined ? VK_ACCESS_SHADER_READ_BIT : 0;
    before.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    before.oldLayout = inputs.bOutputDefined ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    before.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    before.srcQueueFamilyIndex = before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.image = inputs.pOutput->Image;
    before.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier( commandBuffer,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &before );

    xess_vk_execute_params_t params = {};
    params.colorTexture = View( *inputs.pColor );
    params.velocityTexture = View( *inputs.pMotion );
    params.depthTexture = View( *inputs.pDepth );
    params.outputTexture = View( *inputs.pOutput );
    params.jitterOffsetX = inputs.JitterX;
    params.jitterOffsetY = inputs.JitterY;
    params.exposureScale = 1.0f;
    params.resetHistory = inputs.bReset ? 1u : 0u;
    params.inputWidth = inputs.RenderWidth;
    params.inputHeight = inputs.RenderHeight;
    const xess_result_t result = pExecute( static_cast<xess_context_handle_t>( pContext ), commandBuffer, &params );

    VkImageMemoryBarrier after = before;
    after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    after.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    after.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &after );
    if ( result != XESS_RESULT_SUCCESS ) {
        static bool bLogged = false;
        if ( !bLogged ) {
            bLogged = true;
            OTDU_LOG_ERROR( "[xess] xessVKExecute failed (%d)\n", static_cast<int>( result ) );
        }
        return false;
    }
    return true;
}

void XessUpscaler::destroy() {
    if ( pContext != nullptr && pDestroyContext != nullptr ) {
        pDestroyContext( static_cast<xess_context_handle_t>( pContext ) );
    }
    pContext = nullptr;
    contextRenderWidth = contextRenderHeight = contextOutputWidth = contextOutputHeight = 0;
    bFailed = false;
}

#else // no SDK headers: the plugin is compiled out

bool XessUpscaler::IsCompiledIn() { return false; }
bool XessUpscaler::LoadPlugin() { return false; }
bool XessUpscaler::IsLibraryLoaded() { return false; }
uint32_t XessUpscaler::MinVulkanVersion() { return 0; }
void XessUpscaler::AppendDeviceExtensions( VkInstance, VkPhysicalDevice, std::vector<const char *> & ) {}
bool XessUpscaler::PatchDeviceFeatures( VkInstance, VkPhysicalDevice, void * ) { return false; }
bool XessUpscaler::ensure( RenderDevice &, VkInstance, uint32_t, uint32_t, uint32_t, uint32_t ) { return false; }
bool XessUpscaler::dispatch( RenderDevice &, const Inputs & ) { return false; }
void XessUpscaler::destroy() {}
#endif
