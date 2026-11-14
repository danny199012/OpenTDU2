#include "shared.h"
#include "render/upscalers/fsr3_upscaler.h"
#include "system/cmd_line.h"

#include <filesystem>

#define PLATFORM_WINDOWS 1
#include "api/include/ffx_api.h"
#include "api/include/ffx_api_loader.h"
#include "api/include/vk/ffx_api_vk.h"
#include "upscalers/include/ffx_upscale.h"

namespace {
ffxFunctions *Functions( void **pStorage ) {
    static_assert( sizeof( ffxFunctions ) == sizeof( void * ) * 5, "ffxFunctions layout" );
    return reinterpret_cast<ffxFunctions *>( pStorage );
}

void FfxMessage( uint32_t type, const wchar_t *pMessage ) {
    char text[512] = {};
    size_t i = 0;
    for ( ; pMessage != nullptr && pMessage[i] != 0 && i < sizeof( text ) - 1; i++ ) {
        text[i] = pMessage[i] < 128 ? static_cast<char>( pMessage[i] ) : '?';
    }
    if ( type == FFX_API_MESSAGE_TYPE_ERROR ) {
        OTDU_LOG_ERROR( "[fsr3] %s\n", text );
    } else {
        OTDU_LOG_INFO( "[fsr3] %s\n", text );
    }
}

// The layout every FSR input is claimed to be in (the backend turns the state
// into a layout for its barriers).
FfxApiResource Resource( const GPUTexture &texture, VkFormat format, uint32_t state ) {
    FfxApiResourceDescription description = {};
    description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    switch ( format ) {
    case VK_FORMAT_R16G16_SFLOAT:
        description.format = FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
        break;
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        description.format = FFX_API_SURFACE_FORMAT_R32_FLOAT;
        break;
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D16_UNORM:
        description.format = FFX_API_SURFACE_FORMAT_R32_FLOAT;
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        description.format = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        break;
    case VK_FORMAT_R8G8B8A8_SRGB:
        description.format = FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB;
        break;
    case VK_FORMAT_B8G8R8A8_UNORM:
        description.format = FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;
        break;
    case VK_FORMAT_B8G8R8A8_SRGB:
        description.format = FFX_API_SURFACE_FORMAT_B8G8R8A8_SRGB;
        break;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        description.format = FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
        break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        description.format = FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
        break;
    default:
        description.format = FFX_API_SURFACE_FORMAT_UNKNOWN;
        break;
    }
    description.width = texture.Width;
    description.height = texture.Height;
    description.depth = 1;
    description.mipCount = 1;
    description.flags = FFX_API_RESOURCE_FLAGS_NONE;
    description.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;
    FfxApiResource resource = {};
    resource.resource = reinterpret_cast<void *>( texture.Image );
    resource.description = description;
    resource.state = state;
    return resource;
}
} // namespace

bool Fsr3Upscaler::ensure( RenderDevice &device, uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth,
                           uint32_t outputHeight ) {
    if ( pContext != nullptr && contextRenderWidth == renderWidth && contextRenderHeight == renderHeight &&
         contextOutputWidth == outputWidth && contextOutputHeight == outputHeight ) {
        return true;
    }
    if ( pContext == nullptr && bFailed && contextRenderWidth == renderWidth && contextRenderHeight == renderHeight &&
         contextOutputWidth == outputWidth && contextOutputHeight == outputHeight ) {
        return false; // already failed at these sizes
    }
    destroy();
    contextRenderWidth = renderWidth;
    contextRenderHeight = renderHeight;
    contextOutputWidth = outputWidth;
    contextOutputHeight = outputHeight;
    bFailed = true;

    // 1. The loader DLL: next to the executable, then the third_party copy.
    std::vector<std::filesystem::path> candidates;
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW( nullptr, exePath, MAX_PATH );
    candidates.push_back( std::filesystem::path( exePath ).parent_path() / "amd_fidelityfx_loader_vk.dll" );
    candidates.push_back( std::filesystem::path( exePath ).parent_path() / "../../third_party/fidelityfx-api/bin/" /
                          "amd_fidelityfx_loader_vk.dll" );
    candidates.push_back( std::filesystem::path( "third_party/fidelityfx-api/bin/amd_fidelityfx_loader_vk.dll" ) );
    candidates.push_back( std::filesystem::path( "../third_party/fidelityfx-api/bin/amd_fidelityfx_loader_vk.dll" ) );
    candidates.push_back( std::filesystem::path( "../../third_party/fidelityfx-api/bin/amd_fidelityfx_loader_vk.dll" ) );
    for ( const std::filesystem::path &candidate : candidates ) {
        std::error_code error;
        if ( std::filesystem::exists( candidate, error ) ) {
            const std::wstring wide = std::filesystem::absolute( candidate ).wstring();
            // The loader finds the upscaler DLL next to itself.
            pModule = LoadLibraryExW( wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
            if ( pModule != nullptr ) {
                break;
            }
        }
    }
    if ( pModule == nullptr ) {
        if ( !bLoggedFailure ) {
            OTDU_LOG_WARN( "[fsr3] amd_fidelityfx_loader_vk.dll not found (exe folder, third_party/fidelityfx-api/bin): "
                           "FSR 3 is unavailable\n" );
            bLoggedFailure = true;
        }
        return false;
    }
    ffxLoadFunctions( Functions( pFunctions ), pModule );
    ffxFunctions *pApi = Functions( pFunctions );
    if ( pApi->CreateContext == nullptr || pApi->Dispatch == nullptr || pApi->DestroyContext == nullptr ) {
        OTDU_LOG_ERROR( "[fsr3] the loader DLL does not export the FidelityFX API\n" );
        destroy();
        contextRenderWidth = renderWidth;
        contextRenderHeight = renderHeight;
        contextOutputWidth = outputWidth;
        contextOutputHeight = outputHeight;
        bFailed = true;
        return false;
    }

    // 2. The context.
    ffxCreateBackendVKDesc backend = {};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.deviceContext.vkDevice = device.getDevice();
    backend.deviceContext.vkPhysicalDevice = device.getPhysicalDevice();
    backend.deviceContext.vkDeviceProcAddr = vkGetDeviceProcAddr;

    ffxCreateContextDescUpscale create = {};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    create.header.pNext = &backend.header;
    // The final image is already tone mapped (LDR, display referred).
    create.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
    if ( CmdLine::HasArg( "-fsr3debug" ) ) {
        create.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
    }
    create.maxRenderSize = { renderWidth, renderHeight };
    create.maxUpscaleSize = { outputWidth, outputHeight };
    create.fpMessage = FfxMessage;
    ffxContext context = nullptr;
    const ffxReturnCode_t result = pApi->CreateContext( &context, &create.header, nullptr );
    if ( result != FFX_API_RETURN_OK || context == nullptr ) {
        OTDU_LOG_ERROR( "[fsr3] ffxCreateContext failed (%u)\n", static_cast<uint32_t>( result ) );
        return false;
    }
    pContext = context;
    bFailed = false;
    OTDU_LOG_INFO( "[fsr3] context created: render %ux%u -> %ux%u\n", renderWidth, renderHeight, outputWidth,
                   outputHeight );
    return true;
}

bool Fsr3Upscaler::dispatch( RenderDevice &device, const Inputs &inputs ) {
    if ( pContext == nullptr || inputs.pColor == nullptr || inputs.pDepth == nullptr || inputs.pMotion == nullptr ||
         inputs.pOutput == nullptr ) {
        return false;
    }
    ffxFunctions *pApi = Functions( pFunctions );
    // getCommandBuffer() (not peek): it tells the renderers their cached
    // pipeline / descriptor / vertex bindings may be gone after FSR.
    VkCommandBuffer commandBuffer = device.getCommandBuffer();

    // Barrier: the output UNDEFINED / SHADER_READ -> GENERAL (storage write).
    // The depth is an R32_SFLOAT colour copy (EdenSceneView::runFsr3), so every
    // input is a plain SHADER_READ_ONLY colour image.
    VkImageMemoryBarrier before[1] = {};
    before[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before[0].srcAccessMask = inputs.bOutputDefined ? VK_ACCESS_SHADER_READ_BIT : 0;
    before[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    before[0].oldLayout = inputs.bOutputDefined ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    before[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    before[0].srcQueueFamilyIndex = before[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].image = inputs.pOutput->Image;
    before[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier( commandBuffer,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, before );

    ffxDispatchDescUpscale dispatch = {};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dispatch.commandList = commandBuffer;
    dispatch.color = Resource( *inputs.pColor, inputs.pColor->Format, FFX_API_RESOURCE_STATE_COMPUTE_READ );
    dispatch.depth = Resource( *inputs.pDepth, inputs.pDepth->Format, FFX_API_RESOURCE_STATE_COMPUTE_READ );
    dispatch.motionVectors = Resource( *inputs.pMotion, inputs.pMotion->Format, FFX_API_RESOURCE_STATE_COMPUTE_READ );
    dispatch.output = Resource( *inputs.pOutput, inputs.pOutput->Format, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS );
    dispatch.output.description.usage = FFX_API_RESOURCE_USAGE_UAV;
    dispatch.jitterOffset = { inputs.JitterX, inputs.JitterY };
    // Our vectors are previous uv - current uv: scale to render pixels.
    dispatch.motionVectorScale = { static_cast<float>( inputs.RenderWidth ), static_cast<float>( inputs.RenderHeight ) };
    dispatch.renderSize = { inputs.RenderWidth, inputs.RenderHeight };
    dispatch.upscaleSize = { inputs.OutputWidth, inputs.OutputHeight };
    dispatch.enableSharpening = inputs.Sharpness > 0.0f;
    dispatch.sharpness = inputs.Sharpness;
    dispatch.frameTimeDelta = inputs.FrameTimeMs;
    dispatch.preExposure = 1.0f;
    dispatch.reset = inputs.bReset;
    dispatch.cameraNear = inputs.Near;
    dispatch.cameraFar = inputs.Far;
    dispatch.cameraFovAngleVertical = inputs.FovY;
    dispatch.viewSpaceToMetersFactor = 1.0f;
    dispatch.flags = 0;
    const ffxReturnCode_t result = pApi->Dispatch( reinterpret_cast<ffxContext *>( &pContext ), &dispatch.header );

    VkImageMemoryBarrier after[1] = {};
    after[0] = before[0];
    after[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    after[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    after[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    after[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, after );
    if ( result != FFX_API_RETURN_OK ) {
        static bool bLogged = false;
        if ( !bLogged ) {
            bLogged = true;
            OTDU_LOG_ERROR( "[fsr3] ffxDispatch failed (%u)\n", static_cast<uint32_t>( result ) );
        }
        return false;
    }
    return true;
}

void Fsr3Upscaler::destroy() {
    ffxFunctions *pApi = Functions( pFunctions );
    if ( pContext != nullptr && pApi->DestroyContext != nullptr ) {
        pApi->DestroyContext( reinterpret_cast<ffxContext *>( &pContext ), nullptr );
    }
    pContext = nullptr;
    if ( pModule != nullptr ) {
        FreeLibrary( static_cast<HMODULE>( pModule ) );
        pModule = nullptr;
    }
    memset( pFunctions, 0, sizeof( pFunctions ) );
    contextRenderWidth = contextRenderHeight = contextOutputWidth = contextOutputHeight = 0;
    bFailed = false;
}
