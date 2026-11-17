#include "shared.h"
#include "render/d3d12/d3d12_device.h"

#if defined( OTDU_D3D12 )

#include "render/d3d12/d3d12_scene.h"
#include "system/cmd_line.h"

#include <SDL3/SDL.h>

#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h> // ID3D12Debug, ID3D12InfoQueue, DRED
#include <dxgi1_6.h>

namespace {
template <typename T> void SafeRelease( T *&pObject ) {
    if ( pObject != nullptr ) {
        pObject->Release();
        pObject = nullptr;
    }
}

std::string WideToUtf8( const wchar_t *pWide ) {
    if ( pWide == nullptr ) {
        return std::string();
    }
    const int32_t length = WideCharToMultiByte( CP_UTF8, 0, pWide, -1, nullptr, 0, nullptr, nullptr );
    if ( length <= 1 ) {
        return std::string();
    }
    std::string text( static_cast<size_t>( length ), '\0' );
    WideCharToMultiByte( CP_UTF8, 0, pWide, -1, text.data(), length, nullptr, nullptr );
    text.resize( static_cast<size_t>( length - 1 ) );
    return text;
}

UINT64 AlignUp( UINT64 value, UINT64 alignment ) {
    return ( value + alignment - 1 ) & ~( alignment - 1 );
}

D3D12_RESOURCE_BARRIER MakeTransition( ID3D12Resource *pResource,
                                       D3D12_RESOURCE_STATES before,
                                       D3D12_RESOURCE_STATES after ) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = pResource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

// The self test's colour bars: white, yellow, cyan, green, magenta, red, blue,
// black, so a screenshot can be checked bar by bar.
const float kTestPattern[8][4] = {
    { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f },
    { 0.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f },
    { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
};
} // namespace

D3D12Device::D3D12Device() = default;

D3D12Device::~D3D12Device() { destroy(); }

bool D3D12Device::createFactory( bool bValidation ) {
    // The debug layer is only worth having when asked for: it is slow, and a
    // machine without the Graphics Tools feature does not have it at all.
    UINT factoryFlags = 0;
    if ( bValidation ) {
        ID3D12Debug *pDebug = nullptr;
        if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &pDebug ) ) ) && pDebug != nullptr ) {
            pDebug->EnableDebugLayer();
            bDebugLayer = true;

            // Device Removed Extended Data: when the GPU faults, DRED names the
            // command list and the operation that was running - the D3D12 side
            // of what the Vulkan validation layer gives.
            ID3D12DeviceRemovedExtendedDataSettings *pDredSettings = nullptr;
            if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &pDredSettings ) ) ) &&
                 pDredSettings != nullptr ) {
                pDredSettings->SetAutoBreadcrumbsEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
                pDredSettings->SetPageFaultEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
                pDredSettings->Release();
            }
            pDebug->Release();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            OTDU_LOG_INFO( "D3D12: debug layer and DRED enabled\n" );
        } else {
            OTDU_LOG_WARN( "D3D12: the debug layer is unavailable (install the Graphics "
                           "Tools optional feature); continuing without it\n" );
        }
    }

    HRESULT result = CreateDXGIFactory2( factoryFlags, IID_PPV_ARGS( &pFactory ) );
    if ( FAILED( result ) && factoryFlags != 0 ) {
        // A debug factory needs the SDK's debug layer; the plain one always
        // works.
        result = CreateDXGIFactory2( 0, IID_PPV_ARGS( &pFactory ) );
    }
    if ( FAILED( result ) || pFactory == nullptr ) {
        OTDU_LOG_ERROR( "D3D12: CreateDXGIFactory2 failed (0x%08x)\n",
                        static_cast<uint32_t>( result ) );
        return false;
    }
    return true;
}

bool D3D12Device::pickAdapterAndDevice() {
    const bool bWarp = CmdLine::HasArg( "-d3d12-warp" );
    const wchar_t *pPreferred = bWarp ? L"Microsoft Basic Render Driver" : nullptr;

    // High performance first (the discrete GPU of a laptop), then everything
    // else: D3D12CreateDevice is the real test, so walk the list.
    IDXGIAdapter1 *pChosen = nullptr;
    for ( UINT index = 0; pChosen == nullptr; index++ ) {
        IDXGIAdapter1 *pAdapter = nullptr;
        if ( pFactory->EnumAdapterByGpuPreference( index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                   IID_PPV_ARGS( &pAdapter ) ) ==
                 DXGI_ERROR_NOT_FOUND ||
             pAdapter == nullptr ) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc = {};
        pAdapter->GetDesc1( &desc );
        const bool bMatches =
            pPreferred == nullptr ? ( ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) == 0 )
                                  : ( wcscmp( desc.Description, pPreferred ) == 0 );
        if ( bMatches &&
             SUCCEEDED( D3D12CreateDevice( pAdapter, D3D_FEATURE_LEVEL_11_0,
                                           IID_PPV_ARGS( &pDevice ) ) ) ) {
            deviceName = WideToUtf8( desc.Description );
            OTDU_LOG_INFO( "D3D12: adapter '%s' (%llu MB)\n", deviceName.c_str(),
                           static_cast<unsigned long long>( desc.DedicatedVideoMemory /
                                                            ( 1024 * 1024 ) ) );
            pChosen = pAdapter;
            continue;
        }
        pAdapter->Release();
    }

    if ( pDevice == nullptr && pPreferred == nullptr ) {
        // No hardware adapter took the device: WARP always does, and a slow
        // frame is better than no window.
        for ( UINT index = 0; pDevice == nullptr; index++ ) {
            IDXGIAdapter1 *pAdapter = nullptr;
            if ( pFactory->EnumAdapters1( index, &pAdapter ) == DXGI_ERROR_NOT_FOUND ||
                 pAdapter == nullptr ) {
                break;
            }
            if ( SUCCEEDED( D3D12CreateDevice( pAdapter, D3D_FEATURE_LEVEL_11_0,
                                               IID_PPV_ARGS( &pDevice ) ) ) ) {
                DXGI_ADAPTER_DESC1 desc = {};
                pAdapter->GetDesc1( &desc );
                deviceName = WideToUtf8( desc.Description );
                pChosen = pAdapter;
                continue;
            }
            pAdapter->Release();
        }
        if ( pDevice == nullptr ) {
            OTDU_LOG_WARN( "D3D12: no hardware adapter accepted the device; using WARP\n" );
            if ( SUCCEEDED( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0,
                                               IID_PPV_ARGS( &pDevice ) ) ) ) {
                deviceName = "Microsoft Basic Render Driver (WARP)";
            }
        }
    }
    SafeRelease( pChosen );

    if ( pDevice == nullptr ) {
        OTDU_LOG_ERROR( "D3D12: D3D12CreateDevice failed - no D3D12 capable adapter\n" );
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    if ( SUCCEEDED( pDevice->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options,
                                                  sizeof( options ) ) ) ) {
        OTDU_LOG_INFO( "D3D12: binding tier %d, heap tier %d, conservative raster tier %d\n",
                       static_cast<int>( options.ResourceBindingTier ),
                       static_cast<int>( options.ResourceHeapTier ),
                       static_cast<int>( options.ConservativeRasterizationTier ) );
    }
    return true;
}

bool D3D12Device::create( SDL_Window *pWindow, bool bVSync, bool bValidation ) {
    this->pWindow = pWindow;
    this->bVSync = bVSync;
    if ( pWindow == nullptr ) {
        OTDU_LOG_ERROR( "D3D12: create needs a window\n" );
        return false;
    }
    // The swapchain is made for the window's HWND, not for a surface: SDL
    // keeps it in the window properties (SDL3).
    hWindow = SDL_GetPointerProperty( SDL_GetWindowProperties( pWindow ),
                                      SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr );
    if ( hWindow == nullptr ) {
        OTDU_LOG_ERROR( "D3D12: the SDL window has no Win32 HWND\n" );
        return false;
    }

    if ( !createFactory( bValidation ) ) {
        return false;
    }
    if ( !pickAdapterAndDevice() ) {
        return false;
    }
    if ( !createCommandQueue() ) {
        return false;
    }

    // Tearing needs the feature, and a VSync off run presents with it (the
    // same meaning as the Vulkan path's VK_PRESENT_MODE_IMMEDIATE_KHR).
    IDXGIFactory5 *pFactory5 = nullptr;
    if ( SUCCEEDED( pFactory->QueryInterface( IID_PPV_ARGS( &pFactory5 ) ) ) && pFactory5 != nullptr ) {
        BOOL bAllowTearing = FALSE;
        if ( SUCCEEDED( pFactory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                        &bAllowTearing,
                                                        sizeof( bAllowTearing ) ) ) ) {
            bTearing = bAllowTearing != FALSE;
        }
        pFactory5->Release();
    }
    // Alt+Enter must not put the flip model swapchain into the legacy
    // fullscreen transition (it would fail on the next resize).
    pFactory->MakeWindowAssociation( static_cast<HWND>( hWindow ), DXGI_MWA_NO_ALT_ENTER );

    if ( !createSwapchain() ) {
        return false;
    }
    if ( !createFrameResources() ) {
        return false;
    }
    if ( !createDepthStencil() ) {
        return false;
    }

    OTDU_LOG_INFO( "D3D12: device '%s', %ux%u, %u buffers, vsync %s, tearing %s\n",
                   deviceName.c_str(), width, height, kSwapchainBuffers,
                   bVSync ? "on" : "off", bTearing ? "available" : "unavailable" );
    return true;
}

bool D3D12Device::createCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;
    if ( FAILED( pDevice->CreateCommandQueue( &desc, IID_PPV_ARGS( &pQueue ) ) ) ) {
        OTDU_LOG_ERROR( "D3D12: CreateCommandQueue failed\n" );
        return false;
    }

    if ( FAILED( pDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &pFence ) ) ) ) {
        OTDU_LOG_ERROR( "D3D12: CreateFence failed\n" );
        return false;
    }
    fenceValue = 0;
    hFenceEvent = CreateEventEx( nullptr, nullptr, 0, EVENT_ALL_ACCESS );
    if ( hFenceEvent == nullptr ) {
        OTDU_LOG_ERROR( "D3D12: CreateEventEx failed\n" );
        return false;
    }
    return true;
}

bool D3D12Device::createSwapchain() {
    int32_t windowWidth = 0;
    int32_t windowHeight = 0;
    SDL_GetWindowSizeInPixels( pWindow, &windowWidth, &windowHeight );
    if ( windowWidth <= 0 || windowHeight <= 0 ) {
        OTDU_LOG_ERROR( "D3D12: the window is %dx%d, no swapchain can be made\n",
                        static_cast<int>( windowWidth ), static_cast<int>( windowHeight ) );
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = static_cast<UINT>( windowWidth );
    desc.Height = static_cast<UINT>( windowHeight );
    // R8G8B8A8 (not B8G8R8A8): a screenshot's readback is then R,G,B in memory,
    // which is the PPM's own order.
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kSwapchainBuffers;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = bTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    IDXGISwapChain1 *pSwapchain1 = nullptr;
    const HRESULT result = pFactory->CreateSwapChainForHwnd(
        pQueue, static_cast<HWND>( hWindow ), &desc, nullptr, nullptr, &pSwapchain1 );
    if ( FAILED( result ) || pSwapchain1 == nullptr ) {
        OTDU_LOG_ERROR( "D3D12: CreateSwapChainForHwnd failed (0x%08x)\n",
                        static_cast<uint32_t>( result ) );
        return false;
    }
    pSwapchain1->QueryInterface( IID_PPV_ARGS( &pSwapchain ) );
    pSwapchain1->Release();
    if ( pSwapchain == nullptr ) {
        OTDU_LOG_ERROR( "D3D12: IDXGISwapChain3 unavailable\n" );
        return false;
    }
    // Only a swapchain created with DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
    // accepts this; on any other the debug layer reports error #217 (the setting
    // is a no-op here, the frame pacing is the queue's fences).
    if ( ( desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT ) != 0 ) {
        pSwapchain->SetMaximumFrameLatency( kFramesInFlight );
    }

    if ( !createRtvHeap() ) {
        return false;
    }
    bSwapchainDirty = false;
    return true;
}

bool D3D12Device::createRtvHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = kSwapchainBuffers;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;
    if ( FAILED( pDevice->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &pRtvHeap ) ) ) ) {
        OTDU_LOG_ERROR( "D3D12: the RTV descriptor heap could not be created\n" );
        return false;
    }
    rtvDescriptorSize =
        pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

    D3D12_CPU_DESCRIPTOR_HANDLE handle = pRtvHeap->GetCPUDescriptorHandleForHeapStart();
    for ( uint32_t i = 0; i < kSwapchainBuffers; i++ ) {
        if ( FAILED( pSwapchain->GetBuffer( i, IID_PPV_ARGS( &pBackbuffers[i] ) ) ) ) {
            OTDU_LOG_ERROR( "D3D12: swapchain buffer %u unavailable\n",
                            static_cast<uint32_t>( i ) );
            return false;
        }
        pDevice->CreateRenderTargetView( pBackbuffers[i], nullptr, handle );
        handle.ptr += rtvDescriptorSize;
    }

    const D3D12_RESOURCE_DESC backbuffer = pBackbuffers[0]->GetDesc();
    width = static_cast<uint32_t>( backbuffer.Width );
    height = backbuffer.Height;
    return true;
}

bool D3D12Device::createDepthStencil() {
    if ( pDevice == nullptr || width == 0 || height == 0 ) {
        return false;
    }
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = static_cast<DXGI_FORMAT>( kDepthStencilFormatDXGI );
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = static_cast<DXGI_FORMAT>( kDepthStencilFormatDXGI );
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    if ( FAILED( pDevice->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                                                   IID_PPV_ARGS( &pDepthStencil ) ) ) ) {
        OTDU_LOG_ERROR( "D3D12: the depth-stencil buffer could not be created\n" );
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if ( FAILED( pDevice->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &pDsvHeap ) ) ) ) {
        OTDU_LOG_ERROR( "D3D12: the DSV heap could not be created\n" );
        return false;
    }
    pDevice->CreateDepthStencilView( pDepthStencil, nullptr,
                                     pDsvHeap->GetCPUDescriptorHandleForHeapStart() );
    return true;
}

void D3D12Device::destroyDepthStencil() {
    SafeRelease( pDepthStencil );
    SafeRelease( pDsvHeap );
}

bool D3D12Device::createFrameResources() {
    for ( uint32_t i = 0; i < kFramesInFlight; i++ ) {
        FrameData &frame = frames[i];
        if ( FAILED( pDevice->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      IID_PPV_ARGS( &frame.pAllocator ) ) ) ) {
            OTDU_LOG_ERROR( "D3D12: command allocator %u failed\n", static_cast<uint32_t>( i ) );
            return false;
        }
        if ( FAILED( pDevice->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 frame.pAllocator, nullptr,
                                                 IID_PPV_ARGS( &frame.pList ) ) ) ) {
            OTDU_LOG_ERROR( "D3D12: command list %u failed\n", static_cast<uint32_t>( i ) );
            return false;
        }
        // Created open; every frame opens it again after Reset.
        frame.pList->Close();
        frame.FenceValue = 0;
    }
    frameIndex = 0;
    return true;
}

bool D3D12Device::beginFrame( const ColorRGBA &clearColor ) {
    if ( bDeviceLost || pDevice == nullptr || pSwapchain == nullptr ) {
        return false;
    }
    if ( bSwapchainDirty && !recreateSwapchain() ) {
        return false;
    }

    FrameData &frame = frames[frameIndex];
    // Two frames in flight: the CPU must not reuse a slot the GPU still reads.
    if ( frame.FenceValue != 0 && pFence->GetCompletedValue() < frame.FenceValue ) {
        if ( FAILED( pFence->SetEventOnCompletion( frame.FenceValue,
                                                  static_cast<HANDLE>( hFenceEvent ) ) ) ) {
            reportDeviceLost( "SetEventOnCompletion", -1 );
            return false;
        }
        if ( WaitForSingleObject( static_cast<HANDLE>( hFenceEvent ), 5000 ) != WAIT_OBJECT_0 ) {
            // Five seconds for a two frame deep queue is a stalled GPU, not a
            // slow frame (the same reasoning as the Vulkan path's fence waits).
            reportDeviceLost( "the frame fence", -1 );
            return false;
        }
    }

    if ( FAILED( frame.pAllocator->Reset() ) ||
         FAILED( frame.pList->Reset( frame.pAllocator, nullptr ) ) ) {
        reportDeviceLost( "reset of the command list", -1 );
        return false;
    }

    imageIndex = pSwapchain->GetCurrentBackBufferIndex();
    D3D12_RESOURCE_BARRIER barrier = MakeTransition( pBackbuffers[imageIndex],
                                                     D3D12_RESOURCE_STATE_PRESENT,
                                                     D3D12_RESOURCE_STATE_RENDER_TARGET );
    frame.pList->ResourceBarrier( 1, &barrier );

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = { pRtvHeap->GetCPUDescriptorHandleForHeapStart().ptr +
                                        static_cast<SIZE_T>( imageIndex ) * rtvDescriptorSize };
    // The depth-stencil comes with the swapchain (the 2D pipelines' stencil
    // modes and the Flash masks need it); without one the frame still clears,
    // it just cannot mask.
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
    D3D12_CPU_DESCRIPTOR_HANDLE *pDsv = nullptr;
    if ( pDsvHeap != nullptr ) {
        dsv = pDsvHeap->GetCPUDescriptorHandleForHeapStart();
        pDsv = &dsv;
    }
    frame.pList->OMSetRenderTargets( 1, &rtv, FALSE, pDsv );

    const float clear[4] = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };
    frame.pList->ClearRenderTargetView( rtv, clear, 0, nullptr );
    if ( pDsv != nullptr ) {
        frame.pList->ClearDepthStencilView( dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                            1.0f, 0, 0, nullptr );
    }

    // The viewport and scissor a pass needs; harmless for a clear, required
    // the moment the first real draw lands.
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>( width ),
                                static_cast<float>( height ), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) };
    frame.pList->RSSetViewports( 1, &viewport );
    frame.pList->RSSetScissorRects( 1, &scissor );

    if ( bTestPattern ) {
        recordTestPattern( frame.pList, rtv.ptr );
    }

    bInFrame = true;
    frameNumber++;
    return true;
}

void D3D12Device::endFrame() {
    if ( !bInFrame ) {
        return;
    }
    bInFrame = false;

    FrameData &frame = frames[frameIndex];
    D3D12_RESOURCE_BARRIER barrier = MakeTransition( pBackbuffers[imageIndex],
                                                     D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                     D3D12_RESOURCE_STATE_PRESENT );
    frame.pList->ResourceBarrier( 1, &barrier );
    if ( FAILED( frame.pList->Close() ) ) {
        reportDeviceLost( "the command list close", -1 );
        return;
    }
    ID3D12CommandList *ppLists[] = { frame.pList };
    pQueue->ExecuteCommandLists( 1, ppLists );

    if ( !pendingScreenshot.empty() ) {
        // Queued behind the frame on the same queue, so the image copied out is
        // the one just drawn.
        captureBackbuffer( pendingScreenshot.c_str() );
        pendingScreenshot.clear();
    }

    present();

    // -d3d12-debug: the debug layer's verdict on the first frames, while the
    // image is still simple enough for a message to be actionable (a silent
    // HRESULT in a consumer backend is otherwise a hang with no trace).
    if ( bDebugLayer && frameNumber <= 5 ) {
        flushDebugMessages( "frame" );
    }

    // Signal after Present: the slot is free for the CPU once the GPU is done
    // with it.
    const uint64_t value = ++fenceValue;
    if ( SUCCEEDED( pQueue->Signal( pFence, value ) ) ) {
        frame.FenceValue = value;
    }
    frameIndex = ( frameIndex + 1 ) % kFramesInFlight;
}

bool D3D12Device::present() {
    const UINT syncInterval = bVSync ? 1 : 0;
    const UINT presentFlags = ( !bVSync && bTearing ) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT result = pSwapchain->Present( syncInterval, presentFlags );
    if ( result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ) {
        reportDeviceLost( "Present", static_cast<int32_t>( result ) );
        return false;
    }
    if ( FAILED( result ) ) {
        // DXGI_STATUS_OCCLUDED is a success code (a hidden or covered window),
        // so only a real failure gets here.
        OTDU_LOG_WARN( "D3D12: Present failed (0x%08x)\n", static_cast<uint32_t>( result ) );
        return false;
    }
    return true;
}

bool D3D12Device::recreateSwapchain() {
    if ( pSwapchain == nullptr || pWindow == nullptr ) {
        return false;
    }
    int32_t windowWidth = 0;
    int32_t windowHeight = 0;
    SDL_GetWindowSizeInPixels( pWindow, &windowWidth, &windowHeight );
    if ( windowWidth <= 0 || windowHeight <= 0 ) {
        // Minimised: keep bSwapchainDirty, beginFrame reports "no frame" and
        // the game loop idles on it as it does for Vulkan.
        return false;
    }

    waitIdle();
    destroyDepthStencil();

    for ( uint32_t i = 0; i < kSwapchainBuffers; i++ ) {
        SafeRelease( pBackbuffers[i] );
    }
    SafeRelease( pRtvHeap );

    const UINT flags = bTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    const HRESULT result =
        pSwapchain->ResizeBuffers( kSwapchainBuffers, 0, 0, DXGI_FORMAT_UNKNOWN, flags );
    if ( FAILED( result ) ) {
        OTDU_LOG_ERROR( "D3D12: ResizeBuffers failed (0x%08x)\n",
                        static_cast<uint32_t>( result ) );
        return false;
    }
    if ( !createRtvHeap() ) {
        return false;
    }
    if ( !createDepthStencil() ) {
        return false;
    }

    bSwapchainDirty = false;
    OTDU_LOG_INFO( "D3D12: swapchain resized to %ux%u\n", width, height );
    return true;
}

void D3D12Device::waitIdle() {
    if ( pQueue == nullptr || pFence == nullptr || hFenceEvent == nullptr ) {
        return;
    }
    const uint64_t value = ++fenceValue;
    if ( FAILED( pQueue->Signal( pFence, value ) ) ) {
        return;
    }
    if ( pFence->GetCompletedValue() < value ) {
        if ( SUCCEEDED( pFence->SetEventOnCompletion( value,
                                                     static_cast<HANDLE>( hFenceEvent ) ) ) ) {
            WaitForSingleObject( static_cast<HANDLE>( hFenceEvent ), 5000 );
        }
    }
}

void D3D12Device::destroy() {
    waitIdle();

    for ( FrameData &frame : frames ) {
        SafeRelease( frame.pList );
        SafeRelease( frame.pAllocator );
        frame.FenceValue = 0;
    }
    for ( uint32_t i = 0; i < kSwapchainBuffers; i++ ) {
        SafeRelease( pBackbuffers[i] );
    }
    SafeRelease( pRtvHeap );
    destroyDepthStencil();
    SafeRelease( pSrvHeap );
    SafeRelease( pSrvStorageHeap );
    srvUsed.clear();
    SafeRelease( pSwapchain );
    SafeRelease( pFence );
    SafeRelease( pQueue );
    if ( bDebugLayer ) {
        // Drain what the debug layer has to say before the device goes: a
        // validation message at shutdown is otherwise lost.
        flushDebugMessages( "shutdown" );
    }
    SafeRelease( pDevice );
    SafeRelease( pFactory );

    if ( hFenceEvent != nullptr ) {
        CloseHandle( static_cast<HANDLE>( hFenceEvent ) );
        hFenceEvent = nullptr;
    }
    frameIndex = 0;
    imageIndex = 0;
    width = 0;
    height = 0;
    frameNumber = 0;
    bInFrame = false;
    bSwapchainDirty = false;
    bDeviceLost = false;
}

void D3D12Device::recordTestPattern( ID3D12GraphicsCommandList *pList, uintptr_t rtvHandle ) {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = { rtvHandle };
    const uint32_t barWidth = width / 8;
    if ( barWidth == 0 ) {
        return;
    }
    for ( uint32_t i = 0; i < 8; i++ ) {
        const uint32_t left = i * barWidth;
        const uint32_t right = ( i + 1 == 8 ) ? width : ( i + 1 ) * barWidth;
        D3D12_RECT rect = { static_cast<LONG>( left ), 0, static_cast<LONG>( right ),
                            static_cast<LONG>( height ) };
        pList->ClearRenderTargetView( rtv, kTestPattern[i], 1, &rect );
    }
}

bool D3D12Device::captureBackbuffer( const char *pPath ) {
    if ( pDevice == nullptr || pBackbuffers[imageIndex] == nullptr || pQueue == nullptr ) {
        return false;
    }

    ID3D12Resource *pBackbuffer = pBackbuffers[imageIndex];
    const D3D12_RESOURCE_DESC desc = pBackbuffer->GetDesc();

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    pDevice->GetCopyableFootprints( &desc, 0, 1, 0, &footprint, &numRows, &rowSize, &totalBytes );

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC readbackDesc = {};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = totalBytes;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource *pReadback = nullptr;
    if ( FAILED( pDevice->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE,
                                                   &readbackDesc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS( &pReadback ) ) ) ) {
        OTDU_LOG_WARN( "D3D12: the screenshot readback buffer could not be created\n" );
        return false;
    }

    ID3D12CommandAllocator *pAllocator = nullptr;
    ID3D12GraphicsCommandList *pList = nullptr;
    const bool bCreated =
        SUCCEEDED( pDevice->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS( &pAllocator ) ) ) &&
        SUCCEEDED( pDevice->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, pAllocator,
                                               nullptr, IID_PPV_ARGS( &pList ) ) );
    if ( !bCreated ) {
        OTDU_LOG_WARN( "D3D12: the screenshot command list could not be created\n" );
        SafeRelease( pList );
        SafeRelease( pAllocator );
        SafeRelease( pReadback );
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = MakeTransition( pBackbuffer,
                                                     D3D12_RESOURCE_STATE_PRESENT,
                                                     D3D12_RESOURCE_STATE_COPY_SOURCE );
    pList->ResourceBarrier( 1, &barrier );

    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = pReadback;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = pBackbuffer;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    pList->CopyTextureRegion( &destination, 0, 0, 0, &source, nullptr );

    // Back to PRESENT: endFrame presents this buffer right after this.
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    pList->ResourceBarrier( 1, &barrier );
    pList->Close();

    ID3D12CommandList *ppLists[] = { pList };
    pQueue->ExecuteCommandLists( 1, ppLists );
    const uint64_t value = ++fenceValue;
    if ( SUCCEEDED( pQueue->Signal( pFence, value ) ) && pFence->GetCompletedValue() < value ) {
        if ( SUCCEEDED( pFence->SetEventOnCompletion( value,
                                                     static_cast<HANDLE>( hFenceEvent ) ) ) ) {
            WaitForSingleObject( static_cast<HANDLE>( hFenceEvent ), 5000 );
        }
    }

    bool bSaved = false;
    void *pMapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>( totalBytes ) };
    if ( SUCCEEDED( pReadback->Map( 0, &readRange, &pMapped ) ) && pMapped != nullptr ) {
        const uint32_t pixelWidth = width;
        const uint32_t pixelHeight = height;
        const uint8_t *pPixels = static_cast<const uint8_t *>( pMapped );
        std::vector<uint8_t> row( static_cast<size_t>( pixelWidth ) * 3 );
        std::ofstream stream( pPath, std::ios::binary );
        if ( stream ) {
            stream << "P6\n" << pixelWidth << " " << pixelHeight << "\n255\n";
            for ( uint32_t y = 0; y < pixelHeight; y++ ) {
                // RowPitch, not width * 4: the copy is padded (to 256 bytes).
                const uint8_t *pRow =
                    pPixels + static_cast<size_t>( y ) * footprint.Footprint.RowPitch;
                for ( uint32_t x = 0; x < pixelWidth; x++ ) {
                    row[x * 3 + 0] = pRow[x * 4 + 0];
                    row[x * 3 + 1] = pRow[x * 4 + 1];
                    row[x * 3 + 2] = pRow[x * 4 + 2];
                }
                stream.write( reinterpret_cast<const char *>( row.data() ),
                              static_cast<std::streamsize>( row.size() ) );
            }
            bSaved = true;
        }
        D3D12_RANGE writeRange = { 0, 0 };
        pReadback->Unmap( 0, &writeRange );
    }

    SafeRelease( pList );
    SafeRelease( pAllocator );
    SafeRelease( pReadback );

    if ( bSaved ) {
        OTDU_LOG_INFO( "Screenshot saved to '%s'\n", pPath );
    } else {
        OTDU_LOG_WARN( "D3D12: the screenshot '%s' could not be written\n", pPath );
    }
    return bSaved;
}

void D3D12Device::flushDebugMessages( const char *pWhere ) {
    ID3D12InfoQueue *pInfoQueue = nullptr;
    if ( pDevice == nullptr ||
         FAILED( pDevice->QueryInterface( IID_PPV_ARGS( &pInfoQueue ) ) ) ||
         pInfoQueue == nullptr ) {
        return;
    }
    const UINT64 count = pInfoQueue->GetNumStoredMessages();
    for ( UINT64 i = 0; i < count; i++ ) {
        SIZE_T length = 0;
        if ( FAILED( pInfoQueue->GetMessage( i, nullptr, &length ) ) || length == 0 ) {
            continue;
        }
        std::vector<uint8_t> storage( static_cast<size_t>( length ) );
        D3D12_MESSAGE *pMessage = reinterpret_cast<D3D12_MESSAGE *>( storage.data() );
        if ( FAILED( pInfoQueue->GetMessage( i, pMessage, &length ) ) ) {
            continue;
        }
        if ( pMessage->Severity < D3D12_MESSAGE_SEVERITY_WARNING ||
             pMessage->pDescription == nullptr ) {
            continue;
        }
        OTDU_LOG_WARN( "D3D12 debug (%s): %s\n", pWhere, pMessage->pDescription );
    }
    pInfoQueue->ClearStoredMessages();
    pInfoQueue->Release();
}

void D3D12Device::reportDeviceLost( const char *pWhere, int32_t result ) {
    if ( bDeviceLost ) {
        // Reported once, like the Vulkan side: a per frame message buries the
        // log (the owner's log had 639 identical lines).
        return;
    }
    bDeviceLost = true;

    const HRESULT reason = pDevice != nullptr ? pDevice->GetDeviceRemovedReason() : E_FAIL;
    OTDU_LOG_ERROR( "D3D12: the device is lost at '%s' (call 0x%08x, reason 0x%08x)\n",
                    pWhere, static_cast<uint32_t>( result ), static_cast<uint32_t>( reason ) );

    // DRED names what was running when the GPU faulted (enabled in
    // createFactory with -d3d12-debug).
    ID3D12DeviceRemovedExtendedData *pDred = nullptr;
    if ( pDevice != nullptr && SUCCEEDED( pDevice->QueryInterface( IID_PPV_ARGS( &pDred ) ) ) &&
         pDred != nullptr ) {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
        if ( SUCCEEDED( pDred->GetAutoBreadcrumbsOutput( &breadcrumbs ) ) ) {
            for ( const D3D12_AUTO_BREADCRUMB_NODE *pNode = breadcrumbs.pHeadAutoBreadcrumbNode;
                  pNode != nullptr; pNode = pNode->pNext ) {
                const uint32_t last =
                    pNode->pLastBreadcrumbValue != nullptr ? *pNode->pLastBreadcrumbValue : 0;
                const int32_t operation =
                    ( pNode->pCommandHistory != nullptr && last < pNode->BreadcrumbCount )
                        ? static_cast<int32_t>( pNode->pCommandHistory[last] )
                        : -1;
                OTDU_LOG_ERROR( "D3D12 DRED: list '%s' on queue '%s' executed %u of %u "
                                "operations (last %d)\n",
                                pNode->pCommandListDebugNameA != nullptr
                                    ? pNode->pCommandListDebugNameA
                                    : "?",
                                pNode->pCommandQueueDebugNameA != nullptr
                                    ? pNode->pCommandQueueDebugNameA
                                    : "?",
                                last, pNode->BreadcrumbCount, operation );
            }
        }
        D3D12_DRED_PAGE_FAULT_OUTPUT pageFault = {};
        if ( SUCCEEDED( pDred->GetPageFaultAllocationOutput( &pageFault ) ) &&
             pageFault.PageFaultVA != 0 ) {
            OTDU_LOG_ERROR( "D3D12 DRED: page fault at 0x%llx\n",
                            static_cast<unsigned long long>( pageFault.PageFaultVA ) );
        }
        pDred->Release();
    }
    flushDebugMessages( pWhere );
}

bool D3D12Device::runSelfTest( uint32_t frames, const ColorRGBA &clearColor ) {
    if ( frames == 0 ) {
        frames = 1;
    }
    bTestPattern = true;
    // The screenshot is taken from the last frame: every frame carries the
    // pattern, so only the last one is worth waiting for.
    const std::string screenshot = pendingScreenshot;
    pendingScreenshot.clear();

    bool bQuit = false;
    uint32_t presented = 0;
    for ( uint32_t i = 0; i < frames && !bQuit; i++ ) {
        SDL_Event event;
        while ( SDL_PollEvent( &event ) ) {
            if ( event.type == SDL_EVENT_QUIT ||
                 event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ) {
                bQuit = true;
            }
        }
        if ( ( i + 1 >= frames || bQuit ) && !screenshot.empty() ) {
            pendingScreenshot = screenshot;
        }
        if ( !beginFrame( clearColor ) ) {
            break;
        }
        endFrame();
        presented++;
        if ( bDeviceLost ) {
            break;
        }
    }

    waitIdle();
    bTestPattern = false;
    OTDU_LOG_INFO( "D3D12 self test: %u frame(s) presented on '%s' (%ux%u)%s\n", presented,
                   deviceName.c_str(), width, height, bDeviceLost ? " - DEVICE LOST" : "" );
    return !bDeviceLost && presented > 0;
}

bool D3D12RunSelfTest() {
    int32_t frames = CmdLine::GetArgInt( "-renderer-selftest", 120 );
    if ( frames <= 0 ) {
        frames = 120;
    }
    const char *pScreenshot = CmdLine::GetArgValue( "-renderer-shot" );
    const bool bShowWindow = CmdLine::HasArg( "-renderer-window" );
    const bool bValidation = CmdLine::HasArg( "-d3d12-debug" );

    if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
        OTDU_LOG_ERROR( "-renderer-selftest: SDL video could not start: %s\n", SDL_GetError() );
        return false;
    }
    SDL_Window *pWindow = SDL_CreateWindow( "OpenTDU2 - renderer self test", 1280, 720,
                                            bShowWindow ? SDL_WINDOW_RESIZABLE
                                                        : SDL_WINDOW_HIDDEN );
    if ( pWindow == nullptr ) {
        OTDU_LOG_ERROR( "-renderer-selftest: the window could not be created: %s\n",
                        SDL_GetError() );
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
        return false;
    }

    D3D12Device device;
    bool bOk = device.create( pWindow, /*bVSync=*/false, bValidation );
    if ( bOk ) {
        if ( pScreenshot != nullptr ) {
            device.requestScreenshot( pScreenshot );
        }
        // A dark clear colour under the bars, so a black bar cannot be confused
        // with an uninitialised buffer.
        const ColorRGBA clearColor( 0.05f, 0.06f, 0.08f, 1.0f );
        if ( CmdLine::HasArg( "-d3d12-scenetest" ) ) {
            // The scene path (docs/backends.md phase 3b): shader compilation,
            // one pipeline, a textured cube, a depth tested draw.
            bOk = D3D12RunSceneTest( device, static_cast<uint32_t>( frames ), clearColor );
        } else {
            bOk = device.runSelfTest( static_cast<uint32_t>( frames ), clearColor );
        }
    }
    device.destroy();
    SDL_DestroyWindow( pWindow );
    SDL_QuitSubSystem( SDL_INIT_VIDEO );

    OTDU_LOG_INFO( "-renderer-selftest: %s\n", bOk ? "PASSED" : "FAILED" );
    return bOk;
}

bool D3D12Device::createTexture2D( uint32_t width,
                                   uint32_t height,
                                   uint32_t numMips,
                                   uint32_t dxgiFormat,
                                   const void *const *ppMipData,
                                   const uint32_t *pMipSizes,
                                   Texture &texture,
                                   const char *pName ) {
    if ( pDevice == nullptr || width == 0 || height == 0 || numMips == 0 ||
         ppMipData == nullptr ) {
        return false;
    }
    const char *pLabel = pName != nullptr ? pName : "?";

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = static_cast<UINT16>( numMips );
    desc.Format = static_cast<DXGI_FORMAT>( dxgiFormat );
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    ID3D12Resource *pTexture = nullptr;
    if ( FAILED( pDevice->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS( &pTexture ) ) ) ) {
        OTDU_LOG_WARN( "D3D12: texture '%s' (%ux%u, format %u) could not be created\n", pLabel,
                       width, height, dxgiFormat );
        return false;
    }

    // The staging copy: one footprint per mip inside a single upload buffer
    // (GetCopyableFootprints keeps the 512 byte alignment between them).
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints( numMips );
    std::vector<UINT> rowCounts( numMips );
    std::vector<UINT64> rowSizes( numMips );
    UINT64 totalBytes = 0;
    for ( uint32_t mip = 0; mip < numMips; mip++ ) {
        // Every subresource starts at a 512 byte placement boundary.
        totalBytes = AlignUp( totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT );
        UINT64 rowSize = 0;
        UINT64 mipBytes = 0;
        pDevice->GetCopyableFootprints( &desc, mip, 1, totalBytes, &footprints[mip],
                                        &rowCounts[mip], &rowSize, &mipBytes );
        rowSizes[mip] = rowSize;
        totalBytes += mipBytes;
    }

    D3D12_HEAP_PROPERTIES uploadProperties = {};
    uploadProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadProperties.CreationNodeMask = 1;
    uploadProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource *pUpload = nullptr;
    if ( FAILED( pDevice->CreateCommittedResource( &uploadProperties, D3D12_HEAP_FLAG_NONE,
                                                   &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   nullptr, IID_PPV_ARGS( &pUpload ) ) ) ) {
        OTDU_LOG_WARN( "D3D12: the upload buffer for texture '%s' could not be created\n", pLabel );
        SafeRelease( pTexture );
        return false;
    }

    uint8_t *pMapped = nullptr;
    if ( FAILED( pUpload->Map( 0, nullptr, reinterpret_cast<void **>( &pMapped ) ) ) ) {
        SafeRelease( pUpload );
        SafeRelease( pTexture );
        return false;
    }
    for ( uint32_t mip = 0; mip < numMips; mip++ ) {
        const uint8_t *pSource = static_cast<const uint8_t *>( ppMipData[mip] );
        if ( pSource == nullptr ) {
            continue;
        }
        uint8_t *pDestination = pMapped + footprints[mip].Offset;
        const uint32_t rowPitch = footprints[mip].Footprint.RowPitch;
        // A mip whose bytes are short of the footprint copies what it has and
        // leaves the rest as cleared memory.
        const uint64_t sourceSize = pMipSizes != nullptr ? pMipSizes[mip] : rowSizes[mip] * rowCounts[mip];
        for ( UINT row = 0; row < rowCounts[mip]; row++ ) {
            const uint64_t sourceOffset = static_cast<uint64_t>( row ) * rowSizes[mip];
            if ( sourceOffset >= sourceSize ) {
                break;
            }
            const uint64_t rowBytes =
                Min<uint64_t>( rowSizes[mip], sourceSize - sourceOffset );
            memcpy( pDestination + static_cast<size_t>( row ) * rowPitch, pSource + sourceOffset,
                    static_cast<size_t>( rowBytes ) );
        }
    }
    pUpload->Unmap( 0, nullptr );

    ID3D12CommandAllocator *pAllocator = nullptr;
    ID3D12GraphicsCommandList *pList = nullptr;
    const bool bCreated =
        SUCCEEDED( pDevice->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS( &pAllocator ) ) ) &&
        SUCCEEDED( pDevice->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, pAllocator,
                                               nullptr, IID_PPV_ARGS( &pList ) ) );
    if ( !bCreated ) {
        OTDU_LOG_WARN( "D3D12: the texture upload list for '%s' could not be created\n", pLabel );
        SafeRelease( pList );
        SafeRelease( pAllocator );
        SafeRelease( pUpload );
        SafeRelease( pTexture );
        return false;
    }

    for ( uint32_t mip = 0; mip < numMips; mip++ ) {
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = pTexture;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = mip;

        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = pUpload;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprints[mip];
        pList->CopyTextureRegion( &destination, 0, 0, 0, &source, nullptr );
    }

    D3D12_RESOURCE_BARRIER barrier = MakeTransition( pTexture, D3D12_RESOURCE_STATE_COPY_DEST,
                                                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    pList->ResourceBarrier( 1, &barrier );
    pList->Close();

    ID3D12CommandList *ppLists[] = { pList };
    pQueue->ExecuteCommandLists( 1, ppLists );
    waitIdle();

    SafeRelease( pList );
    SafeRelease( pAllocator );
    SafeRelease( pUpload );

    // The SRV lives in the device's shader visible heap, so the 2D draw binds it
    // with a descriptor table and nothing else; the mirror in the storage heap
    // is what a scene draw's texture table copies from.
    uintptr_t srvCpu = 0;
    uintptr_t srvGpu = 0;
    uintptr_t srvStorageCpu = 0;
    if ( !createSrvHeap() || !allocateSrv( srvCpu, srvGpu, &srvStorageCpu ) ) {
        SafeRelease( pTexture );
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = numMips;
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = { srvCpu };
    pDevice->CreateShaderResourceView( pTexture, &srvDesc, srvHandle );
    if ( srvStorageCpu != 0 ) {
        D3D12_CPU_DESCRIPTOR_HANDLE storageHandle = { srvStorageCpu };
        pDevice->CreateShaderResourceView( pTexture, &srvDesc, storageHandle );
    }

    texture.pResource = pTexture;
    texture.SrvCpu = srvCpu;
    texture.SrvGpu = srvGpu;
    texture.SrvStorageCpu = srvStorageCpu;
    texture.Width = width;
    texture.Height = height;
    return true;
}

void D3D12Device::destroyTexture( Texture &texture ) {
    if ( texture.pResource != nullptr ) {
        static_cast<ID3D12Resource *>( texture.pResource )->Release();
        texture.pResource = nullptr;
    }
    freeSrv( static_cast<uintptr_t>( texture.SrvCpu ) );
    texture.SrvCpu = 0;
    texture.SrvGpu = 0;
    texture.Width = 0;
    texture.Height = 0;
}

bool D3D12Device::createSrvHeap() {
    if ( pDevice == nullptr ) {
        return false;
    }
    if ( pSrvHeap != nullptr ) {
        return true;
    }
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    // Shader visible: it is bound to the command list, not just filled on the
    // CPU (the RTV heap is the other kind).
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NumDescriptors = kSrvDescriptors;
    desc.NodeMask = 0;
    if ( FAILED( pDevice->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &pSrvHeap ) ) ) ) {
        OTDU_LOG_ERROR( "D3D12: the SRV descriptor heap could not be created\n" );
        return false;
    }
    // The copy source for the scene's texture tables: same size, same type, not
    // shader visible (that is the rule CopyDescriptorsSimple enforces).
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if ( FAILED( pDevice->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &pSrvStorageHeap ) ) ) ) {
        OTDU_LOG_ERROR( "D3D12: the SRV storage heap could not be created\n" );
        SafeRelease( pSrvHeap );
        return false;
    }
    srvDescriptorSize =
        pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    srvUsed.assign( kSrvDescriptors, false );
    return true;
}

bool D3D12Device::allocateSrv( uintptr_t &cpuHandle, uintptr_t &gpuHandle, uintptr_t *pStorageCpu ) {
    if ( pSrvHeap == nullptr && !createSrvHeap() ) {
        return false;
    }
    for ( uint32_t index = 0; index < kSrvDescriptors; index++ ) {
        if ( srvUsed[index] ) {
            continue;
        }
        srvUsed[index] = true;
        cpuHandle = pSrvHeap->GetCPUDescriptorHandleForHeapStart().ptr +
                    static_cast<SIZE_T>( index ) * srvDescriptorSize;
        gpuHandle = pSrvHeap->GetGPUDescriptorHandleForHeapStart().ptr +
                    static_cast<SIZE_T>( index ) * srvDescriptorSize;
        if ( pStorageCpu != nullptr ) {
            *pStorageCpu = pSrvStorageHeap != nullptr
                               ? pSrvStorageHeap->GetCPUDescriptorHandleForHeapStart().ptr +
                                     static_cast<SIZE_T>( index ) * srvDescriptorSize
                               : 0;
        }
        return true;
    }
    OTDU_LOG_ERROR( "D3D12: the SRV heap (%u descriptors) is full\n", kSrvDescriptors );
    return false;
}

void D3D12Device::freeSrv( uintptr_t cpuHandle ) {
    if ( pSrvHeap == nullptr || cpuHandle == 0 ) {
        return;
    }
    const SIZE_T start = pSrvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    if ( cpuHandle < start ) {
        return;
    }
    const uint32_t index =
        static_cast<uint32_t>( ( cpuHandle - start ) / srvDescriptorSize );
    if ( index < kSrvDescriptors ) {
        srvUsed[index] = false;
    }
}

#endif // OTDU_D3D12
