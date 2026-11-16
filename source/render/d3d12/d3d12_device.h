#pragma once

#include "render/render_api.h"

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

// The COM interfaces are only ever used through pointers here, so the header
// stays free of windows.h / d3d12.h and can be included from anywhere.
struct IDXGIFactory6;
struct IDXGISwapChain3;
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct ID3D12Fence;
struct ID3D12DescriptorHeap;
struct ID3D12Resource;

// Direct3D 12 render device (docs/backends.md).
//
// The first Direct3D backend: it owns the device, the direct queue, the flip
// model swapchain of an SDL window and the frame pacing, and it presents the
// clear colour (or, in the self test, a colour bar pattern). The scene - eden,
// the 2D batches, Flash - is still drawn by the Vulkan device; porting it is
// the shader and binding phase of docs/backends.md.
//
// The frame entry points mirror RenderDevice so GSRender can switch between the
// two without the callers caring.
class D3D12Device {
public:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kSwapchainBuffers = 3;

    struct FrameData {
        ID3D12CommandAllocator *pAllocator = nullptr;
        ID3D12GraphicsCommandList *pList = nullptr;
        uint64_t FenceValue = 0;
    };

    D3D12Device();
    ~D3D12Device();

    bool create( SDL_Window *pWindow, bool bVSync, bool bValidation );
    void destroy();
    // Waits for the GPU: teardown, resize, screenshots.
    void waitIdle();

    inline bool isReady() const { return pDevice != nullptr && pSwapchain != nullptr; }

    // Frame flow. beginFrame returns false when there is nothing to present
    // (a minimised window, a lost device).
    bool beginFrame( const ColorRGBA &clearColor );
    void endFrame();
    void onWindowResized() { bSwapchainDirty = true; }

    inline uint32_t getBackbufferWidth() const { return width; }
    inline uint32_t getBackbufferHeight() const { return height; }
    inline const char *getDeviceName() const { return deviceName.c_str(); }
    inline bool isVSyncEnabled() const { return bVSync; }
    inline bool isTearingSupported() const { return bTearing; }
    // A removed device (a GPU hang, a driver reset, a TDR) can never present
    // again; the game loop leaves on it, as it does for Vulkan's device lost.
    inline bool hasDeviceLost() const { return bDeviceLost; }
    inline uint64_t getFrameNumber() const { return frameNumber; }

    // For the backends that draw into our frame (ImGui's D3D12 renderer): the
    // device, the queue and the command list beginFrame opened.
    inline ID3D12Device *getDevice() const { return pDevice; }
    inline ID3D12CommandQueue *getCommandQueue() const { return pQueue; }
    inline ID3D12GraphicsCommandList *getCommandList() const { return frames[frameIndex].pList; }
    inline ID3D12DescriptorHeap *getSrvHeap() const { return pSrvHeap; }
    // DXGI_FORMAT_R8G8B8A8_UNORM, as a plain integer so the header stays free
    // of dxgiformat.h.
    static constexpr uint32_t kSwapchainFormatDXGI = 28;
    static constexpr uint32_t kSrvDescriptors = 64;

    // Shader visible SRV heap (ImGui's font atlas is the first client).
    bool createSrvHeap();
    // The debug layer's pending messages (-d3d12-debug): a consumer backend can
    // fail silently otherwise (the ImGui font upload waits on a fence).
    void flushPendingDebugMessages() { flushDebugMessages( "client" ); }
    // Handles are returned as integers (D3D12_CPU_DESCRIPTOR_HANDLE::ptr) so
    // that callers which only forward them need no d3d12.h.
    bool allocateSrv( uintptr_t &cpuHandle, uintptr_t &gpuHandle );
    void freeSrv( uintptr_t cpuHandle );

    bool saveScreenshot( const char *pPath );
    void requestScreenshot( const char *pPath ) { pendingScreenshot = pPath; }

    // Presents 'frames' frames of the test pattern and returns whether the
    // device, the swapchain and Present all worked (-renderer-selftest).
    bool runSelfTest( uint32_t frames, const ColorRGBA &clearColor );

private:
    bool createFactory( bool bValidation );
    bool pickAdapterAndDevice();
    bool createCommandQueue();
    bool createSwapchain();
    void destroySwapchain();
    bool createFrameResources();
    bool createRtvHeap();
    bool recreateSwapchain();
    // Presents; returns false when the device is gone.
    bool present();
    // Copies the current backbuffer out as a binary PPM (no dependency, the
    // same format RenderDevice::saveScreenshot writes).
    bool captureBackbuffer( const char *pPath );
    // The self test pattern: eight vertical colour bars, cleared with
    // ClearRenderTargetView so the screenshot proves the swapchain really is
    // ours (a flat clear could be a stale buffer).
    void recordTestPattern( ID3D12GraphicsCommandList *pList, uintptr_t rtvHandle );
    void reportDeviceLost( const char *pWhere, int32_t result );
    void flushDebugMessages( const char *pWhere );

    SDL_Window *pWindow = nullptr;
    void *hWindow = nullptr; // HWND, kept as void* so the header needs no windows.h
    IDXGIFactory6 *pFactory = nullptr;
    IDXGISwapChain3 *pSwapchain = nullptr;
    ID3D12Device *pDevice = nullptr;
    ID3D12CommandQueue *pQueue = nullptr;
    ID3D12Fence *pFence = nullptr;
    void *hFenceEvent = nullptr;
    ID3D12DescriptorHeap *pRtvHeap = nullptr;
    ID3D12DescriptorHeap *pSrvHeap = nullptr;
    uint32_t srvDescriptorSize = 0;
    std::vector<bool> srvUsed;
    ID3D12Resource *pBackbuffers[kSwapchainBuffers] = {};
    FrameData frames[kFramesInFlight];
    uint64_t fenceValue = 0;
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;
    uint32_t rtvDescriptorSize = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t frameNumber = 0;
    std::string deviceName;
    std::string pendingScreenshot;
    bool bVSync = true;
    bool bTearing = false;
    bool bSwapchainDirty = false;
    bool bDeviceLost = false;
    bool bInFrame = false;
    bool bDebugLayer = false;
    bool bTestPattern = false;
};

// -renderer-selftest for the D3D12 backend: hidden window, device, test
// pattern, optional screenshot (-renderer-shot <path>, -renderer-window to show
// the window). Runs before the game, so it needs no game data.
bool D3D12RunSelfTest();
