#pragma once

#include <cstdint>

struct GPUTexture;
class D3D12Device;

// Renderer2D's Direct3D 12 pipeline (docs/backends.md, phase 3).
//
// The Vulkan 2D layer is one GLSL pair (`resources/shaders/ui2d.*`) drawn with
// push constants, one descriptor set and four stencil pipelines. This is the
// same thing in HLSL: the shader is compiled at run time with D3DCompile (the
// d3dcompiler_47.dll every Windows has, so no dxc / SPIRV-Cross step is on the
// critical path - SM 5.0 DXBC is valid input for D3D12), the constants and the
// vertices come from a per-frame upload ring, and the texture is a descriptor
// table into the device's shader-visible SRV heap.
//
// Coordinates, blending, the alpha-only (font atlas) mode, saturation and the
// three stencil modes match Renderer2D exactly; the caller (Renderer2D::flush)
// keeps doing the canvas -> clip space arithmetic.
class D3D12UI {
public:
    // 2D vertices are Renderer2D::Vertex2D: x, y, u, v (floats) + RGBA8 colour.
    static constexpr uint32_t kVertexStride = 20;
    // Same count as D3D12Device::kFramesInFlight: the ring is indexed by the
    // device's frame slot, whose fence beginFrame has already waited for.
    static constexpr uint32_t kFrameSlots = 2;
    // Per frame: 200k vertices or so. A frame that overruns it drops its
    // remaining 2D draws with one warning rather than corrupting the ring.
    static constexpr uint64_t kUploadBytesPerFrame = 4ull * 1024ull * 1024ull;

    D3D12UI();
    ~D3D12UI();

    bool initialize( D3D12Device *pDevice );
    void destroy();
    inline bool isReady() const { return pRootSignature != nullptr && ppPipelines[0] != nullptr; }

    // Called after D3D12Device::beginFrame: this frame slot's ring is free.
    void beginFrame();

    // One batch. 'stencilMode' is the eStencilMode value (0 none, 1 increment,
    // 2 decrement, 3 test-equal); 1 and 2 write no colour, like the Vulkan
    // pipelines.
    void draw( const void *pVertices,
               uint32_t numVertices,
               const GPUTexture *pTexture,
               const float *pProjection,
               int32_t scissorX,
               int32_t scissorY,
               int32_t scissorWidth,
               int32_t scissorHeight,
               bool bAlphaOnly,
               float saturation,
               uint32_t stencilMode,
               uint32_t stencilReference );

private:
    // Everything COM-shaped is kept as void* so this header (and renderer_2d.h,
    // which includes it) stays free of windows.h / d3d12.h.
    D3D12Device *pDevice = nullptr;
    void *pRootSignature = nullptr;
    void *ppPipelines[4] = {};
    void *ppUploadBuffer[kFrameSlots] = {};
    void *ppMapped[kFrameSlots] = {};
    uint64_t pUploadCursor[kFrameSlots] = {};
    bool bOverflowReported = false;
    // -d3d12-logui: one line per frame with the batch count (a black frame is
    // otherwise unexplained).
    bool bLogBatches = false;
    uint64_t frameDrawFrame = 0;
    uint32_t frameBatches = 0;
    uint32_t frameVertices = 0;
    uint32_t frameColourBatches = 0;

    bool createRootSignature();
    bool createPipelines();
};
