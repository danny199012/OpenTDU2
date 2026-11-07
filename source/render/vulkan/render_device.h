#pragma once

#include "render/render_api.h"
#include "render/vulkan/vulkan_include.h"

struct SDL_Window;

struct GPUTexture {
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t NumMips = 1;
    VkImageViewType ViewType = VK_IMAGE_VIEW_TYPE_2D;
    std::string Name;
};

struct GPUBuffer {
    VkBuffer Buffer = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
    void *pMapped = nullptr;
    VkDeviceSize Size = 0;
};

// Sub-allocation from the per-frame host visible ring buffer (dynamic vertex,
// index and uniform data). Valid until the frame is presented.
struct TransientAllocation {
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceSize Offset = 0;
    void *pData = nullptr;
};

// Offscreen render target: color (sampled after the pass) + optional depth
// (sampled after the pass through a depth only view). The render passes of
// render targets in the main formats are compatible with the main render
// pass, so the same pipelines can be used.
struct RenderTarget {
    GPUTexture Color;
    GPUTexture Depth;                               // View: depth aspect, sampled
    VkImageView DepthAttachmentView = VK_NULL_HANDLE; // Depth (+ stencil) aspect
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool bHasDepth = false;
    bool bInitialized = false; // Contents defined (a pass can load them)
    // Cube targets: one attachment view / framebuffer per face (Color.View
    // is the cube view), the depth buffer is shared by the faces.
    uint32_t NumLayers = 1;
    uint32_t InitializedLayers = 0; // Bit mask
    VkImageView LayerViews[6] = {};
    VkFramebuffer LayerFramebuffers[6] = {};
};

enum class eBlendMode : uint8_t {
    BM_Opaque = 0,
    BM_Alpha,
    BM_PremultipliedAlpha,
    BM_Additive,
};

enum class eCullMode : uint8_t {
    CM_None = 0,
    CM_Back,
    CM_Front,
};

struct VertexAttributeDesc {
    uint32_t Location;
    uint32_t Binding;
    VkFormat Format;
    uint32_t Offset;
};

// Stencil use of a pipeline (flash masks: ClipDepth layers).
enum class eStencilMode : uint8_t {
    SM_None = 0,
    SM_Increment, // mask shape: stencil + 1, no colour
    SM_Decrement, // mask shape removed: stencil - 1, no colour
    SM_TestEqual, // masked content: stencil == reference
    SM_Count
};

struct PipelineDesc {
    const uint32_t *pVertexSPIRV = nullptr;
    size_t VertexSPIRVSize = 0; // in bytes
    const uint32_t *pFragmentSPIRV = nullptr;
    size_t FragmentSPIRVSize = 0; // in bytes

    std::vector<VertexAttributeDesc> Attributes;
    std::vector<uint32_t> BindingStrides;
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    eBlendMode Blend = eBlendMode::BM_Opaque;
    eCullMode Cull = eCullMode::CM_None;
    bool bDepthTest = false;
    bool bDepthWrite = false;
    bool bFrontFaceCW = false;
    eStencilMode Stencil = eStencilMode::SM_None; // reference: dynamic state
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkRenderPass RenderPass = VK_NULL_HANDLE; // Main render pass if null
};

enum class eSamplerFilter : uint8_t { SF_Linear = 0, SF_Point };
enum class eSamplerAddress : uint8_t { SA_Clamp = 0, SA_Wrap, SA_Mirror };

class RenderDevice {
public:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kMaxTexturesPerSet = 4;
    static constexpr uint32_t kPushConstantsSize = 128;

    inline VkDevice getDevice() const { return device; }
    // Every caller that records into the frame's command buffer fetches it
    // here, so the fetch counts as a "touch": EdenRenderer skips re-binding
    // the pipeline / vertex + index buffers / push constants of consecutive
    // draws only while no one else has touched the buffer since its last draw
    // (anything else may have bound its own state in between).
    inline VkCommandBuffer getCommandBuffer() const {
        flushPendingRecords();
        ++commandTouches;
        return frames[frameIndex].CommandBuffer;
    }
    // A deferred recorder (EdenRenderer's parallel recording) registers here
    // while it holds draws that must reach the frame's command buffer before
    // anything else does: every fetch of the buffer and every device-side
    // recording call (render target begin / end / clear, timestamps, the main
    // pass, the frame end) flushes it first, so the command order is exactly
    // the order of the calls. The hook is dropped before it runs.
    struct PendingRecordHook {
        virtual void flushPendingRecords() = 0;

    protected:
        ~PendingRecordHook() = default;
    };
    inline void setPendingRecordHook( PendingRecordHook *pHook ) { pPendingRecordHook = pHook; }
    inline void flushPendingRecords() const {
        if ( pPendingRecordHook != nullptr ) {
            PendingRecordHook *pHook = pPendingRecordHook;
            pPendingRecordHook = nullptr;
            pHook->flushPendingRecords();
        }
    }
    // The offscreen render target pass being recorded (secondary command
    // buffers inherit it).
    inline bool isInRenderTarget() const { return pCurrentTarget != nullptr; }
    inline VkRenderPass getActiveRenderPass() const { return activeRenderPass; }
    VkFramebuffer getCurrentFramebuffer() const;
    uint32_t getCurrentTargetWidth() const;
    uint32_t getCurrentTargetHeight() const;
    inline uint32_t getGraphicsQueueFamily() const { return graphicsQueueFamily; }
    // Ends the current render target pass and begins it again with LOAD ops
    // (same target and layer, the pass barrier in between). bSecondary: the
    // new pass takes its contents from secondary command buffers
    // (vkCmdExecuteCommands only); otherwise it is inline again, with the
    // viewport and scissor that were in effect re-set.
    void restartRenderTarget( bool bSecondary );
    // The dynamic viewport / scissor in effect in the frame's command buffer.
    // Everything that changes them goes through here, so a deferred recorder's
    // secondary command buffers (which inherit no dynamic state) start from
    // exactly what an inline draw would have used.
    void setViewport( const VkViewport &viewport );
    void setScissor( const VkRect2D &scissor );
    inline const VkViewport &getCurrentViewport() const { return currentViewport; }
    inline const VkRect2D &getCurrentScissor() const { return currentScissor; }
    // The same buffer without the touch (EdenRenderer's own draws).
    inline VkCommandBuffer peekCommandBuffer() const { return frames[frameIndex].CommandBuffer; }
    inline uint64_t getCommandTouches() const { return commandTouches; }
    inline uint32_t getBackbufferWidth() const { return swapchainExtent.width; }
    inline uint32_t getBackbufferHeight() const { return swapchainExtent.height; }
    inline VkRenderPass getMainRenderPass() const { return mainRenderPass; }
    inline VkPipelineLayout getDefaultPipelineLayout() const {
        return defaultPipelineLayout;
    }
    inline bool supportsBC() const { return bSupportsBC; }
    // Features required by the SPIR-V produced from the original D3D9 shaders
    // (.sob -> DXVK DXSO): runtime descriptor arrays, demote, clip distances.
    inline bool supportsDxsoShaders() const { return bSupportsDxso; }
    // Ray tracing: the device has the whole extension chain
    // (VK_KHR_acceleration_structure + VK_KHR_ray_query and what they need),
    // so acceleration structures can be built and ray queries run. Nothing
    // renders with it yet, see docs/re/ray_tracing.md.
    inline bool supportsRayTracing() const { return bSupportsRayTracing; }
    // Options > Advanced: whether the extensions were requested at all. The
    // device is created once, so this must be set before create().
    void setRayTracingEnabled( bool bEnable ) { bWantRayTracing = bEnable; }
    inline bool wantsRayTracing() const { return bWantRayTracing; }
    inline uint32_t getFrameSlot() const { return frameIndex; }
    inline VkDeviceSize getUniformBufferAlignment() const { return uniformBufferAlignment; }
    inline uint64_t getFrameNumber() const { return frameNumber; }
    inline const char *getDeviceName() const { return deviceName.c_str(); }

public:
    RenderDevice();
    ~RenderDevice();

    bool create( SDL_Window *pWindow, bool bVSync, bool bValidation );
    void destroy();
    void waitIdle();

    // Frame flow. beginFrame returns false if the swapchain is not ready
    // (e.g. minimized window).
    // beginFrame opens the command buffer; the main (swapchain) render pass
    // starts with beginMainPass (or at endFrame). Offscreen render targets
    // are drawn in between.
    bool beginFrame( const ColorRGBA &clearColor );
    void beginMainPass();
    void endFrame();
    inline bool isInMainPass() const { return bInMainPass; }

    // Offscreen render targets (colorFormat VK_FORMAT_UNDEFINED: swapchain
    // format).
    RenderTarget *createRenderTarget( uint32_t width, uint32_t height, VkFormat colorFormat, bool bDepth,
                                      const char *pName );
    RenderTarget *createCubeRenderTarget( uint32_t size, bool bDepth, const char *pName );
    void destroyRenderTarget( RenderTarget *pTarget );
    void beginRenderTarget( RenderTarget *pTarget, bool bClear, const ColorRGBA &clearColor = ColorRGBA(),
                            uint32_t layer = 0 );
    void endRenderTarget();
    // Clears the depth of the render target being drawn (inside its pass).
    void clearRenderTargetDepth();
    // Copies a region of a render target depth (D32_SFLOAT) to a host
    // visible buffer (outside render passes; read it once the frame slot
    // fence has been waited, i.e. kFramesInFlight frames later).
    bool copyRenderTargetDepth( RenderTarget *pTarget, int32_t x, int32_t y, uint32_t width, uint32_t height,
                                GPUBuffer *pDestination );
    // Same for the colour attachment (4 bytes per texel formats: R32F, RGBA8).
    bool copyRenderTargetColor( RenderTarget *pTarget, int32_t x, int32_t y, uint32_t width, uint32_t height,
                                GPUBuffer *pDestination );
    // Image -> image copy recorded into the frame's command buffer (no host
    // readback): numLayers mip-0 subresources starting at baseLayer, same size
    // and format. Both images must have been rendered to (or sampled) once:
    // the layouts are expected in SHADER_READ_ONLY_OPTIMAL, the state a
    // target is left in by endRenderTarget. Used to move the environment cube
    // faces from their scratch target to the sampled one, so a car drawn into
    // the cube never samples an image that is also the pass's render target.
    bool copyTexture( GPUTexture *pSource, GPUTexture *pDestination, uint32_t numLayers = 1, uint32_t baseLayer = 0 );
    // Render pass of the pass being recorded (pipelines are created for it).
    inline VkRenderPass getCurrentRenderPass() const { return currentRenderPass; }
    inline VkFormat getSwapchainFormat() const { return swapchainFormat; }
    void requestSwapchainRecreation() { bSwapchainDirty = true; }
    // Options > Video: VSync changes the present mode, so the swapchain is rebuilt.
    void setVSync( bool bEnable ) {
        if ( bVSync != bEnable ) {
            bVSync = bEnable;
            bSwapchainDirty = true;
        }
    }
    SDL_Window *getWindow() const { return pWindow; }
    // Options > Video, texture filtering: anisotropy of the linear samplers
    // (1 = off). Samplers made before keep their handles; getSampler() hands
    // out the new ones, callers that cached a handle must ask again.
    void setMaxAnisotropy( uint32_t level ) {
        maxAnisotropy = level < 1 ? 1 : ( level > deviceMaxAnisotropy ? deviceMaxAnisotropy : level );
    }
    uint32_t getMaxAnisotropy() const { return maxAnisotropy; }

    // Resources
    GPUTexture *createTexture2D( uint32_t width,
                                 uint32_t height,
                                 uint32_t numMips,
                                 VkFormat format,
                                 const void *const *ppMipData,
                                 const uint32_t *pMipSizes,
                                 const char *pName );
    // Generic texture (2D, cube: 6 layers, 3D). ppData/pSizes: numLayers x
    // numMips entries, layer major (one entry per mip for volumes).
    GPUTexture *createTexture( VkImageViewType viewType,
                               uint32_t width,
                               uint32_t height,
                               uint32_t depth,
                               uint32_t numLayers,
                               uint32_t numMips,
                               VkFormat format,
                               const void *const *ppData,
                               const uint32_t *pSizes,
                               const char *pName );
    void destroyTexture( GPUTexture *pTexture );
    inline VkRenderPass getRenderPass() const { return mainRenderPass; }
    inline VkPipelineCache getPipelineCache() const { return pipelineCache; }
    inline VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }

    GPUBuffer *createBuffer( VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             const void *pInitialData );
    void destroyBuffer( GPUBuffer *pBuffer );

    // Ray tracing (valid when supportsRayTracing()). Acceleration structures
    // and their inputs are referenced by device address rather than bound, so
    // those buffers need VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, and VMA
    // refuses that usage unless the allocator was created with the buffer
    // device address flag.
    VkDeviceAddress getBufferDeviceAddress( GPUBuffer *pBuffer );
    // Device local buffer without host mapping (acceleration structure storage,
    // scratch space).
    GPUBuffer *createDeviceBuffer( VkDeviceSize size, VkBufferUsageFlags usage );
    // Host visible buffer meant to be read back (createBuffer is write only).
    GPUBuffer *createReadbackBuffer( VkDeviceSize size, VkBufferUsageFlags usage );
    // Host visible buffer memory (createBuffer / createReadbackBuffer).
    void writeBuffer( GPUBuffer *pBuffer, const void *pSource, VkDeviceSize size );
    void readBuffer( GPUBuffer *pBuffer, void *pDestination, VkDeviceSize size );
    // Makes the GPU writes to a readback buffer visible to pMapped (non-coherent memory).
    void invalidateBuffer( GPUBuffer *pBuffer );
    // Records 'record' into a one time command buffer and waits for it (asset
    // uploads, acceleration structure builds: call it outside a frame).
    void executeImmediate( const std::function<void( VkCommandBuffer )> &record );
    // Texture uploads and host waits since the last call (the [hitch] line and
    // the [perf] upload line): what a streaming frame spent on the GPU side of
    // its loads.
    struct UploadStats {
        uint32_t Textures = 0;
        uint64_t Bytes = 0;
        uint32_t Submits = 0;     // batches (or, -syncuploads, textures) submitted
        uint32_t Waits = 0;       // host waits on an immediate fence
        double WaitMs = 0.0;
    };
    UploadStats takeUploadStats();
    // Writes the VkPipelineCache to <userdata>/pipeline_cache.bin (done at
    // destroy; loaded back at device creation).
    void savePipelineCache();
    // minAccelerationStructureScratchOffsetAlignment of the device.
    inline VkDeviceSize getAccelScratchAlignment() const { return accelScratchAlignment; }

    // The frame's transient ring itself, for meshes whose streams live in it
    // (EdenWorld's impostor batches: EdenMesh points at GPUBuffers).
    inline GPUBuffer *getTransientBuffer() { return &frames[frameIndex].TransientBuffer; }
    TransientAllocation allocateTransient( VkDeviceSize size,
                                           VkDeviceSize alignment = 16 );

    VkSampler getSampler( eSamplerFilter filter, eSamplerAddress address ) {
        return getSampler( filter, address, address );
    }
    VkSampler getSampler( eSamplerFilter filter, eSamplerAddress addressU, eSamplerAddress addressV );

    // Default pipeline layout: set 0 = kMaxTexturesPerSet combined image
    // samplers (fragment), push constants = kPushConstantsSize bytes.
    VkDescriptorSet allocateTextureSet( GPUTexture *const *ppTextures,
                                        uint32_t numTextures,
                                        VkSampler sampler );

    VkPipeline createPipeline( const PipelineDesc &desc );
    void destroyPipeline( VkPipeline pipeline );

    // Screenshot of the last presented frame (debug / automated tests). The
    // capture carries the burnt-in build stamp (source/debug/build_stamp.h)
    // unless bBuildStamp is false - the photo camera saves clean photos.
    bool saveScreenshot( const char *pPath );
    void requestScreenshot( const char *pPath, bool bBuildStamp = true ) {
        pendingScreenshot = pPath;
        pendingScreenshotStamp = bBuildStamp;
    }

    // A lost Vulkan device (VK_ERROR_DEVICE_LOST: the driver reset the GPU
    // after a hang or a timeout) can never present again. Set once, reported
    // once - the game loop leaves on it (source/tdu_instance.cpp) instead of
    // spinning on failing presents with the window frozen on the last image.
    inline bool hasDeviceLost() const { return bDeviceLost; }

    // VMA's device memory usage, summed over the heaps (perf logging: a device
    // lost after a long drive is often the driver running out of memory).
    void getMemoryUsage( uint64_t &usedBytes, uint64_t &budgetBytes ) const;

    // GPU pass timing (-loggputimes): a timestamp pair per named pass,
    // recorded in the frame's command buffer and read back when the frame
    // slot is reused - its fence has been waited by then, so the results are
    // ready with no host stall (the -lumalog trick). The averages print as a
    // [perf] gpu: line every 120 frames, next to the CPU [perf] scene line.
    // vkCmdWriteTimestamp is not allowed inside a render pass, so beginGpuPass
    // must be called outside one: right before the pass's beginRenderTarget.
    void beginGpuPass( const char *pName );
    void endGpuPass();

private:
    struct FrameData {
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkSemaphore ImageAvailable = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
        VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
        GPUBuffer TransientBuffer;
        VkDeviceSize TransientOffset = 0;
        // Overflow log cap (see allocateTransient): a flooded frame once
        // measured 400k+ failing allocations - logging each was 2.6 GB of log
        // per minute, which read as a hang. The count still lands in the last
        // line of the frame.
        uint32_t TransientOverflows = 0;
        std::vector<GPUTexture *> PendingTextureDeletes;
        std::vector<GPUBuffer *> PendingBufferDeletes;
        // The queues of the PREVIOUS recycle of this slot: a destroy queued
        // while frame N used this slot is flushed when slot N+2*kFramesInFlight
        // begins, not N+kFramesInFlight. One recycle only retires frame N's own
        // fence and resets this slot's descriptor pools - the eden renderers'
        // sets of that same frame N live in pools that reset later still (at
        // the next scene begin), and destroying a view/buffer they hold is the
        // use-after-destroy the validation layers caught (graffiti textures,
        // then device lost on long sessions).
        std::vector<GPUTexture *> PendingTextureDeletesOld;
        std::vector<GPUBuffer *> PendingBufferDeletesOld;
        // -lumalog: rows of the presented image (diagnostics).
        GPUBuffer *pLumaProbe = nullptr;
        uint64_t LumaFrame = 0;
        bool bLumaPending = false;
    };

    SDL_Window *pWindow;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;
    VmaAllocator allocator;
    std::string deviceName;

    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    GPUTexture depthTexture;
    VkRenderPass mainRenderPass;
    bool bVSync;
    uint32_t maxAnisotropy = 1;
    bool enabledAnisotropy = false;
    uint32_t deviceMaxAnisotropy = 1;
    bool bSwapchainDirty;
    bool bDeviceLost = false;
    bool bSupportsBC;
    bool bSupportsDxso = false;
    bool bSupportsRayTracing = false;
    bool bWantRayTracing = true;
    VkDeviceSize accelScratchAlignment = 256;
    VkDeviceSize uniformBufferAlignment = 256;
    bool bFrameStarted;
    bool bInMainPass = false;
    ColorRGBA frameClearColor;
    VkRenderPass currentRenderPass = VK_NULL_HANDLE;
    mutable uint64_t commandTouches = 0; // see getCommandBuffer
    mutable PendingRecordHook *pPendingRecordHook = nullptr;
    VkRenderPass activeRenderPass = VK_NULL_HANDLE; // the begun one (LOAD variant after a restart)
    uint32_t currentTargetLayer = 0;
    VkViewport currentViewport = {};
    VkRect2D currentScissor = {};
    void recordPassBarrier( VkCommandBuffer commandBuffer );
    RenderTarget *pCurrentTarget = nullptr;
    std::unordered_map<uint64_t, VkRenderPass> targetRenderPasses;

    FrameData frames[kFramesInFlight];
    uint32_t frameIndex;
    uint32_t imageIndex;
    uint64_t frameNumber;

    // GPU pass timing (see beginGpuPass): one query pool per frame slot, two
    // queries per pass; the pass names are recorded on the CPU next to them
    // so the read back line can name the passes.
    static constexpr uint32_t kMaxGpuPasses = 16;
    struct GpuPassSlot {
        VkQueryPool QueryPool = VK_NULL_HANDLE;
        char Names[kMaxGpuPasses][16] = {};
        uint32_t NumPasses = 0;
    };
    GpuPassSlot gpuPasses[kFramesInFlight];
    float timestampPeriodNs = 0.0f; // 0: the device or queue has no timestamps
    bool bGpuTiming = false;        // -loggputimes and timestamps available
    struct GpuPassAverage {
        char Name[16] = {};
        double SumMs = 0.0;
        uint32_t Frames = 0;
    };
    GpuPassAverage gpuAverages[kMaxGpuPasses];
    uint32_t gpuNumAverages = 0;
    uint32_t gpuReadFrames = 0;
    void readGpuPasses();

    VkCommandPool uploadCommandPool;
    // Batched texture uploads (see createTexture): the copies of every texture
    // created since the last submit share one open command buffer; it is
    // submitted (fence, no wait) before the frame's own submit, before any
    // immediate submit, or when its staging passes kUploadBatchBytes. Same
    // queue, so the queue submission order plus the barriers in the batch make
    // the textures ready for every later submit. The staging buffers are freed
    // when the batch's fence signals (retireUploads). -syncuploads = the old
    // submit-and-wait per texture (the A/B).
    static constexpr VkDeviceSize kUploadBatchBytes = 64ull << 20;
    struct UploadInFlight {
        VkFence Fence = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        std::vector<GPUBuffer *> Staging;
    };
    VkCommandBuffer uploadBatch = VK_NULL_HANDLE;
    std::vector<GPUBuffer *> uploadBatchStaging;
    VkDeviceSize uploadBatchBytes = 0;
    std::vector<UploadInFlight> uploadsInFlight;
    UploadStats uploadStats;
    VkCommandBuffer getUploadBatch();
    void submitUploadBatch();
    void retireUploads( bool bWaitAll );
    VkDescriptorSetLayout textureSetLayout;
    VkPipelineLayout defaultPipelineLayout;
    VkPipelineCache pipelineCache;
    std::unordered_map<uint32_t, VkSampler> samplers;
    std::string pendingScreenshot;
    bool pendingScreenshotStamp = true;

private:
    bool createInstance( bool bValidation );
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    void destroySwapchain();
    bool createMainRenderPass();
    VkRenderPass getTargetRenderPass( VkFormat colorFormat, VkFormat depthFormat, bool bClear );
    bool createFrameResources();
    void flushPendingDeletes( FrameData &frame );
    VkCommandBuffer beginImmediateCommands();
    void endImmediateCommands( VkCommandBuffer commandBuffer );
    void captureSwapchainImage( const char *pPath, bool bBuildStamp );
    void reportDeviceLost( const char *pWhere, VkResult result );
    bool waitForFrameFence( FrameData &frame );
};
