#pragma once

#include "render/render_api.h"
// eBlendMode / eCullMode: the Vulkan renderer's enums, kept as the one
// vocabulary for pipeline state so the Eden port passes its own values through
// unchanged.
#include "render/vulkan/render_device.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct GPUTexture;
class D3D12Device;
struct D3D12ShaderStage;

// The Direct3D 12 scene path (docs/backends.md phase 3b).
//
// The 2D layer (D3D12UI) is one HLSL pair and one vertex format. The scene is
// the opposite: hundreds of programs, every material's own state. This class is
// the part that does not depend on Eden - one root signature, pipelines built
// from the compiled stages (D3D12ShaderCompiler), meshes uploaded once, and a
// draw that binds constants, textures and a pipeline.
//
// The root signature (one for every program, because the compiler remaps every
// shader into this convention - see d3d12_shader.h):
//
//   0  CBV          b0        vertex stage constants (unused yet)
//   1  CBV          b0        pixel stage constants (unused yet)
//   2  32-bit const b13       the D3D9 push constants (render state)
//   3  SRV table    t0..t15   the material's textures
//   static samplers s0..s15   linear / wrap / mip linear (D3D9's default state)
//
// Everything else is per pipeline state: blend, cull, depth, the vertex layout.
class D3D12Scene {
public:
    // One vertex attribute, as the mesh declares it: the SPIR-V location the
    // shader reads (SPIRV-Cross writes `TEXCOORD<location>`), the stream, the
    // DXGI_FORMAT (a plain integer: this header keeps no d3d12.h) and the byte
    // offset inside the stream.
    struct Attribute {
        uint32_t Location = 0;
        uint32_t Stream = 0;
        uint32_t Format = 0; // DXGI_FORMAT
        uint32_t Offset = 0;
    };

    struct PipelineDesc {
        const D3D12ShaderStage *pVS = nullptr;
        const D3D12ShaderStage *pPS = nullptr;
        const Attribute *pAttributes = nullptr;
        uint32_t NumAttributes = 0;
        const uint32_t *pStrides = nullptr; // one per vertex stream
        uint32_t NumStreams = 0;
        eBlendMode Blend = eBlendMode::BM_Opaque;
        eCullMode Cull = eCullMode::CM_Back;
        bool bDepthTest = true;
        bool bDepthWrite = true;
        bool bFrontFaceCW = false;
    };

    // An uploaded mesh: vertex + index buffer live in an upload heap for the
    // rest of the run (a streaming world wants a default heap copy; the Eden
    // port is where that lands, docs/backends.md section 5).
    struct Mesh {
        void *pVertexBuffer = nullptr;
        void *pIndexBuffer = nullptr;
        uint32_t NumIndices = 0;
        uint32_t VertexStride = 0;
        // The vertex buffer's size in bytes: D3D12_VERTEX_BUFFER_VIEW::SizeInBytes
        // is the whole buffer, not one vertex (passing the stride silently makes
        // the input assembler treat the buffer as a single vertex and drop every
        // triangle that indexes past it).
        uint32_t VertexBytes = 0;
    };

public:
    D3D12Scene();
    ~D3D12Scene();

    bool initialize( D3D12Device *pDevice );
    void destroy();
    inline bool isReady() const { return pRootSignature != nullptr; }

    // The SRV table size of the one root signature (t0..t15). Public because the
    // shader measurement (-d3d12-shadertest) reports how many Eden stages ask
    // for more textures than a draw can bind. 16 is the compiler's per-stage
    // ceiling (D3D12ShaderCompiler::kMaxResources) and covers the 10 textures
    // the whole .sob set asks for at most - 13 stages wanted more than the 8
    // this was before the tally measured it.
    static constexpr uint32_t kTextureTableSize = 16;

    // Created on first use and cached by the full descriptor (the same program
    // drawn with two blend modes is two pipelines, exactly like the Vulkan
    // renderer's pipeline cache). nullptr when the state cannot be built.
    const void *getPipeline( const PipelineDesc &desc );

    bool createMesh( const void *pVertexData,
                     uint32_t vertexBytes,
                     uint32_t vertexStride,
                     const uint32_t *pIndices,
                     uint32_t numIndices,
                     Mesh &mesh );
    void destroyMesh( Mesh &mesh );

    // Records one indexed draw into the frame D3D12Device::beginFrame opened.
    // 'pConstants' is the push constant block (root constants at b13);
    // 'pTextureSrvs' are the *storage-heap* SRV handles of t0.. in order
    // (D3D12Device::Texture::SrvStorageCpu, GPUTexture::D3D12SrvStorageCpu) -
    // a shader-visible descriptor is not a legal copy source for the table.
    void draw( const void *pPipeline,
               const Mesh &mesh,
               const void *pConstants,
               uint32_t constantsBytes,
               const uint64_t *pTextureSrvs,
               uint32_t numTextures );

    // -d3d12-dump-shaders <dir>: the HLSL of every pipeline the scene builds,
    // so a black draw can be read instead of guessed at.
    inline void setShaderDumpDir( const char *pDir ) { shaderDumpDir = pDir != nullptr ? pDir : ""; }

private:
    static constexpr uint32_t kMaxRootConstants = 32; // 128 bytes, the Vulkan budget
    // The table heap: a bump allocator over shader-visible SRV descriptors,
    // reset when the frame slot changes (the slot's fence has retired it).
    static constexpr uint32_t kTableDescriptors = 4096;

    D3D12Device *pDevice = nullptr;
    void *pRootSignature = nullptr;
    void *pSrvTable = nullptr;
    uint64_t srvTableCpuStart = 0;
    uint64_t srvTableGpuStart = 0;
    uint64_t srvCursor = 0;
    uint32_t srvFrameSlot = 0xFFFFFFFFu;
    uint32_t srvDescriptorSize = 0;
    std::unordered_map<std::string, void *> pipelines;
    std::string shaderDumpDir;
    uint32_t dumpedShaders = 0;

    bool createRootSignature();
    // Reserves 'count' contiguous descriptors and returns their base index.
    uint32_t allocateTable( uint32_t count );
    void dumpShader( const char *pName, const D3D12ShaderStage &stage );
};

// -d3d12-scenetest: the scene path without game data. Draws a lit, textured,
// depth tested cube through the engine's own mesh.vert/mesh.frag (SPIR-V ->
// HLSL -> DXBC), presents it and reports whether the frame holds geometry.
// Runs inside -renderer-selftest's device/window (D3D12RunSelfTest).
bool D3D12RunSceneTest( D3D12Device &device, uint32_t frames, const ColorRGBA &clearColor );

// -d3d12-shadertest: the measurement that sizes the Eden port (docs/backends.md
// section 3.5). Every vertex and pixel stage of every .sob library in
// Euro/Shaders goes through the full scene shader path - DXSO -> SPIR-V ->
// HLSL (SPIRV-Cross) -> SM 5.0 DXBC (D3DCompile) - and the tally is logged:
// libraries, permutations, stages, the two failure counts, the largest push
// constant block and texture count any stage asks for, and the wall time.
//
// It needs the game data (Euro/Shaders) and nothing else - no window, no
// device - so it runs in the game bootstrap (tdu_instance.cpp) right after the
// file system service, not in -renderer-selftest, which reads no -game. Returns
// true when every stage of every library compiled.
bool D3D12RunShaderTest();
