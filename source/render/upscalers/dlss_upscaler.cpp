#include "shared.h"
#include "render/upscalers/dlss_upscaler.h"

#include "game/game_options.h"
#include "system/cmd_line.h"

#if defined( OTDU_HAVE_DLSS )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <deque>
#include <filesystem>
#include <string>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_defs.h"
#include "nvsdk_ngx_helpers.h"
#include "nvsdk_ngx_helpers_vk.h"
#include "nvsdk_ngx_params.h"
#include "nvsdk_ngx_vk.h"

namespace {
// A project id for NGX's "custom engine" path (no NVIDIA application id): any stable GUID.
constexpr const char *kProjectId = "6e3c2a5d-8f4e-4c36-9a47-0b9d1c7f52a1";

std::vector<std::wstring> gPaths;
std::vector<const wchar_t *> gPathPointers;
NVSDK_NGX_FeatureCommonInfo gCommonInfo = {};
NVSDK_NGX_FeatureDiscoveryInfo gDiscovery = {};
std::deque<std::string> gExtensionNames; // stable storage for the names handed to Vulkan (never cleared)

void LogCallback( const char *pMessage, NVSDK_NGX_Logging_Level level, NVSDK_NGX_Feature ) {
    std::string text = pMessage != nullptr ? pMessage : "";
    while ( !text.empty() && ( text.back() == '\n' || text.back() == '\r' ) ) {
        text.pop_back();
    }
    if ( level == NVSDK_NGX_LOGGING_LEVEL_OFF ) {
        return;
    }
    OTDU_LOG_INFO( "[dlss] %s\n", text.c_str() );
}

void PrepareDiscovery() {
    if ( !gPaths.empty() ) {
        return;
    }
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW( nullptr, exePath, MAX_PATH );
    const std::filesystem::path exeFolder = std::filesystem::path( exePath ).parent_path();
    gPaths.push_back( exeFolder.wstring() );
    gPaths.push_back( ( exeFolder / "../../third_party/dlss-sdk/bin" ).lexically_normal().wstring() );
    gPaths.push_back( std::filesystem::absolute( "third_party/dlss-sdk/bin" ).wstring() );
    for ( const std::wstring &path : gPaths ) {
        gPathPointers.push_back( path.c_str() );
    }
    gCommonInfo.PathListInfo.Path = gPathPointers.data();
    gCommonInfo.PathListInfo.Length = static_cast<unsigned int>( gPathPointers.size() );
    gCommonInfo.LoggingInfo.LoggingCallback = LogCallback;
    gCommonInfo.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    gCommonInfo.LoggingInfo.DisableOtherLoggingSinks = true;
    gDiscovery.SDKVersion = NVSDK_NGX_Version_API;
    gDiscovery.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    gDiscovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    gDiscovery.Identifier.v.ProjectDesc.ProjectId = kProjectId;
    gDiscovery.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    gDiscovery.Identifier.v.ProjectDesc.EngineVersion = "1.0";
    static std::wstring dataPath = ( std::filesystem::temp_directory_path() / "OpenTDU2-ngx" ).wstring();
    std::error_code error;
    std::filesystem::create_directories( dataPath, error );
    gDiscovery.ApplicationDataPath = dataPath.c_str();
    gDiscovery.FeatureInfo = &gCommonInfo;
}

bool HasExt( const std::vector<VkExtensionProperties> &available, const char *pName ) {
    for ( const VkExtensionProperties &extension : available ) {
        if ( strcmp( extension.extensionName, pName ) == 0 ) {
            return true;
        }
    }
    return false;
}

void AddExtension( const std::vector<VkExtensionProperties> &available, std::vector<const char *> &extensions,
                   const char *pName, const char *pKind ) {
    for ( const char *pExisting : extensions ) {
        if ( strcmp( pExisting, pName ) == 0 ) {
            return;
        }
    }
    if ( !HasExt( available, pName ) ) {
        OTDU_LOG_WARN( "[dlss] required %s extension %s is not available on this system\n", pKind, pName );
        return;
    }
    gExtensionNames.push_back( pName );
    extensions.push_back( gExtensionNames.back().c_str() );
    OTDU_LOG_INFO( "[dlss] %s extension %s\n", pKind, pName );
}

NVSDK_NGX_Resource_VK Resource( const GPUTexture &texture, bool bReadWrite ) {
    return NVSDK_NGX_Create_ImageView_Resource_VK( texture.View, texture.Image,
                                                   { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, texture.Format,
                                                   texture.Width, texture.Height, bReadWrite );
}
} // namespace

bool DlssUpscaler::IsCompiledIn() { return true; }

bool DlssUpscaler::WantedAtStartup() { return CmdLine::HasArg( "-dlss" ) || GetGameOptions().Upscaler == 5; }

void DlssUpscaler::AppendInstanceExtensions( const std::vector<VkExtensionProperties> &available,
                                             std::vector<const char *> &extensions ) {
    PrepareDiscovery();
    uint32_t count = 0;
    VkExtensionProperties *pProperties = nullptr;
    const NVSDK_NGX_Result result =
        NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements( &gDiscovery, &count, &pProperties );
    if ( NVSDK_NGX_FAILED( result ) ) {
        OTDU_LOG_WARN( "[dlss] instance extension query failed (0x%x)\n", static_cast<unsigned>( result ) );
        return;
    }
    for ( uint32_t i = 0; i < count; i++ ) {
        AddExtension( available, extensions, pProperties[i].extensionName, "instance" );
    }
}

void DlssUpscaler::AppendDeviceExtensions( VkInstance instance, VkPhysicalDevice physicalDevice,
                                           const std::vector<VkExtensionProperties> &available,
                                           std::vector<const char *> &extensions ) {
    PrepareDiscovery();
    NVSDK_NGX_FeatureRequirement requirement = {};
    const NVSDK_NGX_Result supported =
        NVSDK_NGX_VULKAN_GetFeatureRequirements( instance, physicalDevice, &gDiscovery, &requirement );
    if ( NVSDK_NGX_SUCCEED( supported ) ) {
        OTDU_LOG_INFO( "[dlss] feature requirement: supported flags 0x%x (0 = supported), min HW arch 0x%x, OS %s\n",
                       static_cast<unsigned>( requirement.FeatureSupported ), requirement.MinHWArchitecture,
                       requirement.MinOSVersion );
    }
    uint32_t count = 0;
    VkExtensionProperties *pProperties = nullptr;
    const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
        instance, physicalDevice, &gDiscovery, &count, &pProperties );
    if ( NVSDK_NGX_FAILED( result ) ) {
        OTDU_LOG_WARN( "[dlss] device extension query failed (0x%x)\n", static_cast<unsigned>( result ) );
        return;
    }
    for ( uint32_t i = 0; i < count; i++ ) {
        AddExtension( available, extensions, pProperties[i].extensionName, "device" );
    }
}

bool DlssUpscaler::ensure( RenderDevice &device, uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth,
                           uint32_t outputHeight ) {
    const bool bSame = contextRenderWidth == renderWidth && contextRenderHeight == renderHeight &&
                       contextOutputWidth == outputWidth && contextOutputHeight == outputHeight;
    if ( pHandle != nullptr && bSame ) {
        return true;
    }
    if ( bFailed && bSame ) {
        return false;
    }
    if ( !device.isDlssReady() ) {
        bFailed = true;
        contextRenderWidth = renderWidth;
        contextRenderHeight = renderHeight;
        contextOutputWidth = outputWidth;
        contextOutputHeight = outputHeight;
        return false;
    }
    vkDeviceWaitIdle( device.getDevice() );
    if ( pHandle != nullptr ) {
        NVSDK_NGX_VULKAN_ReleaseFeature( static_cast<NVSDK_NGX_Handle *>( pHandle ) );
        pHandle = nullptr;
    }
    contextRenderWidth = renderWidth;
    contextRenderHeight = renderHeight;
    contextOutputWidth = outputWidth;
    contextOutputHeight = outputHeight;
    bFailed = true;
    PrepareDiscovery();
    if ( !bInitialised ) {
        const NVSDK_NGX_Result init = NVSDK_NGX_VULKAN_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", gDiscovery.ApplicationDataPath, device.getInstance(),
            device.getPhysicalDevice(), device.getDevice(), vkGetInstanceProcAddr, vkGetDeviceProcAddr, &gCommonInfo,
            NVSDK_NGX_Version_API );
        if ( NVSDK_NGX_FAILED( init ) ) {
            OTDU_LOG_ERROR( "[dlss] NVSDK_NGX_VULKAN_Init failed (0x%x)\n", static_cast<unsigned>( init ) );
            return false;
        }
        bInitialised = true;
        pDevice = device.getDevice();
    }
    NVSDK_NGX_Parameter *pCapabilities = nullptr;
    if ( NVSDK_NGX_FAILED( NVSDK_NGX_VULKAN_GetCapabilityParameters( &pCapabilities ) ) || pCapabilities == nullptr ) {
        OTDU_LOG_ERROR( "[dlss] GetCapabilityParameters failed\n" );
        return false;
    }
    int available = 0;
    NVSDK_NGX_Parameter_GetI( pCapabilities, NVSDK_NGX_Parameter_SuperSampling_Available, &available );
    if ( !available ) {
        int needsDriver = 0;
        NVSDK_NGX_Parameter_GetI( pCapabilities, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver );
        OTDU_LOG_ERROR( "[dlss] DLSS is not available on this system (needs updated driver: %d)\n", needsDriver );
        return false;
    }
    // The preset whose optimal input size is closest to the render size.
    static constexpr NVSDK_NGX_PerfQuality_Value kPresets[] = {
        NVSDK_NGX_PerfQuality_Value_UltraPerformance, NVSDK_NGX_PerfQuality_Value_MaxPerf,
        NVSDK_NGX_PerfQuality_Value_Balanced,         NVSDK_NGX_PerfQuality_Value_MaxQuality,
        NVSDK_NGX_PerfQuality_Value_UltraQuality,     NVSDK_NGX_PerfQuality_Value_DLAA };
    NVSDK_NGX_PerfQuality_Value chosen = NVSDK_NGX_PerfQuality_Value_MaxQuality;
    float best = 1e30f;
    for ( NVSDK_NGX_PerfQuality_Value preset : kPresets ) {
        unsigned optimalW = 0, optimalH = 0, maxW = 0, maxH = 0, minW = 0, minH = 0;
        float sharpness = 0.0f;
        if ( NVSDK_NGX_FAILED( NGX_DLSS_GET_OPTIMAL_SETTINGS( pCapabilities, outputWidth, outputHeight, preset,
                                                              &optimalW, &optimalH, &maxW, &maxH, &minW, &minH,
                                                              &sharpness ) ) ||
             optimalW == 0 ) {
            continue;
        }
        const float dx = static_cast<float>( optimalW ) - static_cast<float>( renderWidth );
        const float dy = static_cast<float>( optimalH ) - static_cast<float>( renderHeight );
        if ( dx * dx + dy * dy < best ) {
            best = dx * dx + dy * dy;
            chosen = preset;
        }
    }
    NVSDK_NGX_Parameter *pParams = nullptr;
    if ( NVSDK_NGX_FAILED( NVSDK_NGX_VULKAN_AllocateParameters( &pParams ) ) || pParams == nullptr ) {
        OTDU_LOG_ERROR( "[dlss] AllocateParameters failed\n" );
        return false;
    }
    NVSDK_NGX_DLSS_Create_Params create = {};
    create.Feature.InWidth = renderWidth;
    create.Feature.InHeight = renderHeight;
    create.Feature.InTargetWidth = outputWidth;
    create.Feature.InTargetHeight = outputHeight;
    create.Feature.InPerfQualityValue = chosen;
    // Tone mapped (LDR) colour, low resolution jitter free vectors, auto exposure.
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    VkCommandBuffer commandBuffer = device.getCommandBuffer();
    NVSDK_NGX_Handle *pCreated = nullptr;
    const NVSDK_NGX_Result created =
        NGX_VULKAN_CREATE_DLSS_EXT1( device.getDevice(), commandBuffer, 1, 1, &pCreated, pParams, &create );
    if ( NVSDK_NGX_FAILED( created ) || pCreated == nullptr ) {
        OTDU_LOG_ERROR( "[dlss] creating the DLSS feature failed (0x%x)\n", static_cast<unsigned>( created ) );
        NVSDK_NGX_VULKAN_DestroyParameters( pParams );
        return false;
    }
    if ( pParameters != nullptr ) {
        NVSDK_NGX_VULKAN_DestroyParameters( static_cast<NVSDK_NGX_Parameter *>( pParameters ) );
    }
    pParameters = pParams;
    pHandle = pCreated;
    bFailed = false;
    OTDU_LOG_INFO( "[dlss] feature created: render %ux%u -> %ux%u, preset %d\n", renderWidth, renderHeight, outputWidth,
                   outputHeight, static_cast<int>( chosen ) );
    return true;
}

bool DlssUpscaler::dispatch( RenderDevice &device, const Inputs &inputs ) {
    if ( pHandle == nullptr || pParameters == nullptr ) {
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

    NVSDK_NGX_Resource_VK color = Resource( *inputs.pColor, false );
    NVSDK_NGX_Resource_VK depth = Resource( *inputs.pDepth, false );
    NVSDK_NGX_Resource_VK motion = Resource( *inputs.pMotion, false );
    NVSDK_NGX_Resource_VK output = Resource( *inputs.pOutput, true );
    NVSDK_NGX_VK_DLSS_Eval_Params eval = {};
    eval.Feature.pInColor = &color;
    eval.Feature.pInOutput = &output;
    eval.pInDepth = &depth;
    eval.pInMotionVectors = &motion;
    eval.InJitterOffsetX = inputs.JitterX;
    eval.InJitterOffsetY = inputs.JitterY;
    eval.InRenderSubrectDimensions = { inputs.RenderWidth, inputs.RenderHeight };
    eval.InReset = inputs.bReset ? 1 : 0;
    // Our vectors are previous uv - current uv: scale to render pixels.
    eval.InMVScaleX = static_cast<float>( inputs.RenderWidth );
    eval.InMVScaleY = static_cast<float>( inputs.RenderHeight );
    eval.InPreExposure = 1.0f;
    eval.InExposureScale = 1.0f;
    eval.InFrameTimeDeltaInMsec = inputs.FrameTimeMs;
    const NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSS_EXT(
        commandBuffer, static_cast<NVSDK_NGX_Handle *>( pHandle ), static_cast<NVSDK_NGX_Parameter *>( pParameters ),
        &eval );

    VkImageMemoryBarrier after = before;
    after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    after.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    after.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &after );
    if ( NVSDK_NGX_FAILED( result ) ) {
        static bool bLogged = false;
        if ( !bLogged ) {
            bLogged = true;
            OTDU_LOG_ERROR( "[dlss] evaluate failed (0x%x)\n", static_cast<unsigned>( result ) );
        }
        return false;
    }
    return true;
}

void DlssUpscaler::destroy() {
    if ( pHandle != nullptr || pParameters != nullptr || bInitialised ) {
        if ( pDevice != nullptr ) {
            vkDeviceWaitIdle( static_cast<VkDevice>( pDevice ) );
        }
        if ( pHandle != nullptr ) {
            NVSDK_NGX_VULKAN_ReleaseFeature( static_cast<NVSDK_NGX_Handle *>( pHandle ) );
        }
        if ( pParameters != nullptr ) {
            NVSDK_NGX_VULKAN_DestroyParameters( static_cast<NVSDK_NGX_Parameter *>( pParameters ) );
        }
        if ( bInitialised && pDevice != nullptr ) {
            NVSDK_NGX_VULKAN_Shutdown1( static_cast<VkDevice>( pDevice ) );
        }
    }
    pHandle = pParameters = pDevice = nullptr;
    bInitialised = false;
    bFailed = false;
    contextRenderWidth = contextRenderHeight = contextOutputWidth = contextOutputHeight = 0;
}

#else // no SDK: the plugin is compiled out

bool DlssUpscaler::IsCompiledIn() { return false; }
bool DlssUpscaler::WantedAtStartup() { return false; }
void DlssUpscaler::AppendInstanceExtensions( const std::vector<VkExtensionProperties> &, std::vector<const char *> & ) {}
void DlssUpscaler::AppendDeviceExtensions( VkInstance, VkPhysicalDevice, const std::vector<VkExtensionProperties> &,
                                           std::vector<const char *> & ) {}
bool DlssUpscaler::ensure( RenderDevice &, uint32_t, uint32_t, uint32_t, uint32_t ) { return false; }
bool DlssUpscaler::dispatch( RenderDevice &, const Inputs & ) { return false; }
void DlssUpscaler::destroy() {}
#endif
