#include "shared.h"
#include "render/d3d12/d3d12_imgui.h"

#if defined( OTDU_D3D12 )

#include "render/d3d12/d3d12_device.h"
#include "render/gs_render.h"

#include <SDL3/SDL.h>

#include <windows.h>
#include <d3d12.h>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_sdl3.h"

namespace {
bool gbInitialized = false;
char gBackendVersion[64];

// The SRV heap belongs to the D3D12Device (a shader visible
// CBV_SRV_UAV heap): ImGui asks for a descriptor per texture and gives it back,
// so the allocator is the device's, not a private one here.
void AllocateSrv( ImGui_ImplDX12_InitInfo *pInfo,
                  D3D12_CPU_DESCRIPTOR_HANDLE *pCpuHandle,
                  D3D12_GPU_DESCRIPTOR_HANDLE *pGpuHandle ) {
    D3D12Device *pDevice = static_cast<D3D12Device *>( pInfo->UserData );
    uintptr_t cpuHandle = 0;
    uintptr_t gpuHandle = 0;
    if ( pDevice != nullptr && pDevice->allocateSrv( cpuHandle, gpuHandle ) ) {
        pCpuHandle->ptr = cpuHandle;
        pGpuHandle->ptr = gpuHandle;
        return;
    }
    pCpuHandle->ptr = 0;
    pGpuHandle->ptr = 0;
}

void FreeSrv( ImGui_ImplDX12_InitInfo *pInfo,
              D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
              D3D12_GPU_DESCRIPTOR_HANDLE ) {
    D3D12Device *pDevice = static_cast<D3D12Device *>( pInfo->UserData );
    if ( pDevice != nullptr ) {
        pDevice->freeSrv( cpuHandle.ptr );
    }
}
} // namespace

bool D3D12ImGui::initialize( SDL_Window *pWindow ) {
    D3D12Device *pDevice = gpRender != nullptr ? gpRender->getD3D12Device() : nullptr;
    if ( pDevice == nullptr || pDevice->getDevice() == nullptr ) {
        OTDU_LOG_ERROR( "D3D12 ImGui: no Direct3D 12 device to draw into\n" );
        return false;
    }
    if ( !pDevice->createSrvHeap() ) {
        return false;
    }

    // The platform backend only feeds events and wants the window; the renderer
    // is ours (not SDL_Renderer, not Vulkan).
    if ( !ImGui_ImplSDL3_InitForOther( pWindow ) ) {
        OTDU_LOG_ERROR( "D3D12 ImGui: ImGui_ImplSDL3_InitForOther failed\n" );
        return false;
    }

    snprintf( gBackendVersion, sizeof( gBackendVersion ), "OpenTDU2 Direct3D 12 (%s)",
              pDevice->getDeviceName() );

    ImGui_ImplDX12_InitInfo info = {};
    info.Device = pDevice->getDevice();
    info.CommandQueue = pDevice->getCommandQueue();
    // One frame in flight for the backend's own vertex/index buffers: with two,
    // the second frame allocates a second pair of upload buffers while our
    // command list is open, and that allocation waits (frame 1 hung with an
    // idle CPU before this was pinned to 1). The overlay is tiny, so the ring
    // buys nothing here.
    info.NumFramesInFlight = 1;
    info.RTVFormat = static_cast<DXGI_FORMAT>( D3D12Device::kSwapchainFormatDXGI );
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.UserData = pDevice;
    info.SrvDescriptorHeap = pDevice->getSrvHeap();
    info.SrvDescriptorAllocFn = &AllocateSrv;
    info.SrvDescriptorFreeFn = &FreeSrv;
    // ImGui 1.92's backend allocates one descriptor per texture (the font atlas
    // first), which is what the shared heap above is for.
    if ( !ImGui_ImplDX12_Init( &info ) ) {
        OTDU_LOG_ERROR( "D3D12 ImGui: ImGui_ImplDX12_Init failed\n" );
        ImGui_ImplSDL3_Shutdown();
        return false;
    }

    ImGui::GetIO().BackendRendererName = gBackendVersion;
    gbInitialized = true;

    // Create the root signature, the pipeline state and the font atlas now, in
    // initialize, rather than on the first frame: a failure there is a silent
    // hang inside the backend's texture upload (it waits on a fence with
    // INFINITE), and this turns it into one log line plus a working fallback.
    if ( !ImGui_ImplDX12_CreateDeviceObjects() ) {
        OTDU_LOG_ERROR( "D3D12 ImGui: the device objects (root signature, pipeline, font) "
                        "could not be created - the overlay stays off\n" );
        gbInitialized = false;
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        return false;
    }
    // Whatever the debug layer said while they were built (-d3d12-debug).
    pDevice->flushPendingDebugMessages();

    // CreateDeviceObjects has just uploaded the font atlas, but ImGui 1.92's
    // texture bookkeeping can still ask the backend to upload it again from
    // inside RenderDrawData - and that path waits on a fence with INFINITE
    // while our own command list is open, which deadlocks (found the hard way:
    // frame 0 draws, frame 1 hangs). Nothing has changed since the upload, so
    // the textures are marked done here.
    uint32_t numPending = 0;
    for ( ImTextureData *pTexture : ImGui::GetPlatformIO().Textures ) {
        if ( pTexture->Status == ImTextureStatus_OK ) {
            continue;
        }
        pTexture->SetStatus( ImTextureStatus_OK );
        numPending++;
    }
    if ( numPending > 0 ) {
        OTDU_LOG_INFO( "D3D12 ImGui: %u texture(s) uploaded up front\n", numPending );
    }
    OTDU_LOG_INFO( "D3D12 ImGui: backend ready (%s)\n", gBackendVersion );
    return true;
}

void D3D12ImGui::shutdown() {
    if ( !gbInitialized ) {
        return;
    }
    // The backend frees its own descriptors through FreeSrv above.
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    gbInitialized = false;
}

void D3D12ImGui::newFrame() {
    if ( gbInitialized ) {
        ImGui_ImplDX12_NewFrame();
    }
}

void D3D12ImGui::render( void *pDrawData ) {
    D3D12Device *pDevice = gpRender != nullptr ? gpRender->getD3D12Device() : nullptr;
    if ( !gbInitialized || pDevice == nullptr || pDrawData == nullptr ) {
        return;
    }
    ID3D12GraphicsCommandList *pCommandList = pDevice->getCommandList();
    if ( pCommandList == nullptr ) {
        return;
    }
    // The RTV, viewport and scissor are already set by D3D12Device::beginFrame;
    // the backend sets its own pipeline state, root signature and SRV heap.
    ImGui_ImplDX12_RenderDrawData( static_cast<ImDrawData *>( pDrawData ), pCommandList );
}

void D3D12ImGui::updateTexture( void *pTextureData ) {
    if ( !gbInitialized || pTextureData == nullptr ) {
        return;
    }
    ImGui_ImplDX12_UpdateTexture( static_cast<ImTextureData *>( pTextureData ) );
}

#endif // OTDU_D3D12
