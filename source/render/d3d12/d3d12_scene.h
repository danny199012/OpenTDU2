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
//   0..3   CBV          b0..b3     vertex stage constant buffers
//   4..7   CBV          b0..b3     pixel stage constant buffers
//   8      32-bit const b13 space1 the D3D9 push constants (render state)
//   9      SRV table    t0..t15    the material's textures
//   10     sampler table s0..s15   the D3D9 sampler set (render/sampler_heap.h)
//
// A stage's cbuffers are numbered b0..bN in SPIR-V order by the compiler; four
// per stage covers the widest .sob stage measured (-d3d12-shadertest). They are
// filled per draw from a host visible ring - a root CBV is a GPU address - see
// draw() and uploadConstants().
//
// The samplers are a descriptor table, not static samplers: DXSO hands every
// Eden shader a *runtime* sampler array (`sampler_heap[] : register(s0, space0)`)
// whose index comes out of the push constant block, and D3D12 cannot index a
// static sampler dynamically (docs/backends.md section 4). The heap holds the
// same entries, in the same order, as EdenRenderer's Vulkan sampler set, so a
// material samples the same texels on both backends.
//
// Everything else is per pipeline state: blend, cull, depth, the vertex layout -
// and, for a D3D9 material, its own blend factors, write mask, depth func and
// cull mode (PipelineDesc::D3D9State, docs/backends.md section 3.7).
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

    // How many constant buffers one stage binds per draw: the compiler remaps a
    // stage's cbuffers into b0..bN in SPIR-V order (d3d12_shader.h), and the
    // widest .sob stage measured by -d3d12-shadertest has three (the D3D9
    // constant file, spec_state_t, clip_info_t). Four leaves that plus one.
    static constexpr uint32_t kMaxConstantBuffers = 4;

    // One stage's constant buffers for a draw (b0..b3). Eden fills them from its
    // D3D9 constant files (EdenRenderer::fillConstants); a shader that declares
    // no cbuffer - the built-in mesh pair - passes nullptr and reads zeros.
    struct StageConstants {
        const void *pData[kMaxConstantBuffers] = {};
        uint32_t Sizes[kMaxConstantBuffers] = {};
    };

    // D3D9 pipeline state in the D3D9 enumeration values EdenMaterial stores
    // (MAT. render states +0x30), so a material's values pass through unchanged.
    // Passing one replaces Blend / Cull / bDepthTest: a D3D9 material cannot
    // always be expressed as the four eBlendMode cases (SRCCOLOR / INVDESTCOLOR,
    // subtract, MIN / MAX, a write mask, a depth func).
    //
    // The blend factor and the alpha test are *not* pipeline state on D3D12: the
    // factor is dynamic state (OMSetBlendFactor, draw()'s blendFactor argument)
    // and the alpha test is a `discard` DXSO already emits, driven by the spec
    // constant block (b2) and the push constant alpha reference - so neither is
    // part of the pipeline cache key.
    struct D3D9State {
        uint8_t SrcBlend = 2;         // D3DBLEND_ONE
        uint8_t DstBlend = 1;         // D3DBLEND_ZERO
        uint8_t BlendOp = 1;          // D3DBLENDOP_ADD
        uint8_t DepthFunc = 0;        // D3DCMPFUNC, 0: LESSEQUAL (EdenMaterial's default)
        uint8_t ColorWriteMask = 0xf; // D3DCOLORWRITEENABLE bits (R 1, G 2, B 4, A 8)
        uint8_t CullMode = 3;         // D3DCULL (1 none, 2 CW, 3 CCW, 4 two-sided)
        bool bMirrored = false;       // negative determinant / cube face: swaps 2 and 4
        bool isBlended() const { return !( SrcBlend == 2 && DstBlend == 1 ); }
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
        // D3D9 material state; when set, SrcBlend / DstBlend / BlendOp,
        // ColorWriteMask, DepthFunc and CullMode replace Blend / Cull above
        // (bDepthTest / bDepthWrite still apply).
        const D3D9State *pState = nullptr;
    };

    // An uploaded mesh: up to three vertex streams and one 32-bit index buffer,
    // all in upload heaps for the rest of the run (a streaming world wants a
    // default-heap copy; the Eden port is where that lands, docs/backends.md
    // section 5). Three streams because that is what a `.3DG` node has - the
    // position/normal/colour stream, the texture-coordinate/weight stream and
    // the per-vertex bone stream (EdenMesh::kMaxStreams) - and every attribute
    // in PipelineDesc::Attribute names the stream it is read from.
    struct Mesh {
        static constexpr uint32_t kMaxStreams = 3;
        void *pVertexBuffers[kMaxStreams] = {};
        // The vertex buffer's size in bytes: D3D12_VERTEX_BUFFER_VIEW::SizeInBytes
        // is the whole buffer, not one vertex (passing the stride silently makes
        // the input assembler treat the buffer as a single vertex and drop every
        // triangle that indexes past it).
        uint32_t VertexBytes[kMaxStreams] = {};
        uint32_t Strides[kMaxStreams] = {};
        uint32_t NumStreams = 0;
        void *pIndexBuffer = nullptr;
        uint32_t NumIndices = 0;

        inline const void *streamBuffer( uint32_t stream ) const {
            return stream < kMaxStreams ? pVertexBuffers[stream] : nullptr;
        }
        inline uint32_t streamSize( uint32_t stream ) const {
            return stream < kMaxStreams ? ( VertexBytes[stream] != 0 ? VertexBytes[stream] : Strides[stream] ) : 0;
        }
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

    // The sampler table size of the same root signature (s0..s15 - D3D9's 16
    // sampler slots). The first kSamplerHeapSize entries are the Eden sampler
    // set (render/sampler_heap.h); the rest stay linear wrap, so an engine shader
    // that names s14/s15 still resolves.
    static constexpr uint32_t kSamplerTableSize = 16;

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
    // The multi-stream form (an Eden mesh has up to three streams): one vertex
    // buffer per entry, each with its own stride, all bound by one draw.
    bool createMeshStreams( const void *const *ppVertexData,
                            const uint32_t *pVertexBytes,
                            const uint32_t *pStrides,
                            uint32_t numStreams,
                            const uint32_t *pIndices,
                            uint32_t numIndices,
                            Mesh &mesh );
    void destroyMesh( Mesh &mesh );

    // Records one indexed draw into the frame D3D12Device::beginFrame opened.
    // 'pConstants' is the push constant block (root constants at b13);
    // 'pTextureSrvs' are the *storage-heap* SRV handles of t0.. in order
    // (D3D12Device::Texture::SrvStorageCpu, GPUTexture::D3D12SrvStorageCpu) -
    // a shader-visible descriptor is not a legal copy source for the table.
    // Both descriptor heaps (the SRV table and the sampler heap) are made the
    // command list's set here, because a descriptor table is only valid while
    // its heap is bound.
    //
    // 'pVSConstants' / 'pPSConstants' are the stages' b0..b3 blocks (Eden's
    // constant files, an engine shader's cbuffers). They are copied into the
    // frame slot's upload ring here - a root CBV needs a GPU virtual address and
    // the caller's bytes are CPU side and may be overwritten right after the
    // call - so the ring, not the caller, decides the address. A null or empty
    // entry binds the zero block. 'blendFactor' is D3D12's dynamic blend factor
    // (D3DBLENDFACTOR / D3DBLEND_INVBLENDFACTOR).
    void draw( const void *pPipeline,
               const Mesh &mesh,
               const void *pConstants,
               uint32_t constantsBytes,
               const uint64_t *pTextureSrvs,
               uint32_t numTextures,
               const StageConstants *pVSConstants = nullptr,
               const StageConstants *pPSConstants = nullptr,
               float blendFactor = 1.0f );

    // -d3d12-dump-shaders <dir>: the HLSL of every pipeline the scene builds,
    // so a black draw can be read instead of guessed at.
    inline void setShaderDumpDir( const char *pDir ) { shaderDumpDir = pDir != nullptr ? pDir : ""; }

    // No INFO line per created pipeline. The catalogue tally (-d3d12-pipetest)
    // creates thousands; its own totals are the report and a refusal still
    // logs as an error.
    inline void setQuietPipelines( bool bEnable ) { bQuietPipelines = bEnable; }

    // Releases every cached pipeline (the GPU state objects only - meshes and
    // heaps stay). The tally uses it to bound memory: it creates one PSO per
    // .sob permutation and keeps none of them.
    void releasePipelines();

    // The per-frame SRV table cache (docs/backends.md 3.13): one descriptor table
    // per *distinct texture set*, not per draw. The heap holds 4096 descriptors
    // per frame slot, and a scene records thousands of draws with the same
    // material's set over and over, so a table per draw cannot fit while a table
    // per material does. Cumulative counts, for a probe's log.
    inline uint64_t getTextureTableHits() const { return srvTableHits; }
    inline uint64_t getTextureTableMisses() const { return srvTableMisses; }
    inline uint32_t getTextureTableCount() const { return static_cast<uint32_t>( srvTables.size() ); }

    // The per frame constant ring: its capacity, and how much of the frame
    // slot's ring the last frame used. A draw copies its two D3D9 constant files
    // (~8 KB) into it, so this is the number that decides how many draws a frame
    // can hold (docs/backends.md 3.14).
    //
    // 256 MB per slot, i.e. ~29,000 draws - measured, not estimated: a draw
    // puts 9,216 bytes in the ring (the VS file 4,368 and the PS file 3,856, the
    // 64-byte spec block that both stages bind, and every block rounded up to
    // 256 bytes), so the 64 MB this started at overflowed at 7,281 draws and a
    // 12,800-draw frame lost its last 40 % to the zero block. A world's main
    // pass is ~7,600 draws and the Z pre-pass draws the same geometry again, so
    // the budget has to cover several passes; the Vulkan renderer's per frame
    // transient buffer is 512 MB for exactly that reason. Host-visible memory:
    // two slots cost 512 MB, the same as ONE of Vulkan's.
    static constexpr uint64_t kConstantRingBytes = 256ull * 1024ull * 1024ull;
    uint64_t getFrameConstantBytes() const;

private:
    static constexpr uint32_t kMaxRootConstants = 32; // 128 bytes, the Vulkan budget
    // The table heap: a bump allocator over shader-visible SRV descriptors,
    // reset when the frame slot changes (the slot's fence has retired it).
    static constexpr uint32_t kTableDescriptors = 4096;
    // The constant ring, one upload buffer per frame slot: every draw copies its
    // b0..b3 blocks into it and points the root CBVs at the copy. 256 MB per slot
    // (kConstantRingBytes, public: the probe reports against it) - a draw puts
    // 9,216 bytes in it, so that is ~29,000 draws, and the ring
    // resets when the slot's fence has passed. An overflow is logged once per
    // frame and the draw keeps the zero block (a measurable frame is better than
    // a corrupted ring).
    // A root CBV cannot be null, and the alignment D3D12 requires for a constant
    // buffer view's offset.
    static constexpr uint64_t kConstantAlignment = 256;
    // D3D12's cbuffer limit (the debug layer rejects a view whose shader reads
    // past 64 KB).
    static constexpr uint32_t kMaxConstantBytes = 64 * 1024;

    D3D12Device *pDevice = nullptr;
    void *pRootSignature = nullptr;
    void *pSrvTable = nullptr;
    uint64_t srvTableCpuStart = 0;
    uint64_t srvTableGpuStart = 0;
    uint64_t srvCursor = 0;
    uint32_t srvFrameSlot = 0xFFFFFFFFu;
    uint32_t srvDescriptorSize = 0;
    // The sampler heap: filled once with the Eden sampler set (render/sampler_heap.h)
    // and never rewritten - the per-material choice is the *index* the shader
    // reads out of the push constants, exactly like the Vulkan sampler set.
    void *pSamplerHeap = nullptr;
    uint64_t samplerHeapGpuStart = 0;
    // The constant ring: two mapped upload buffers (one per frame slot), the
    // bump cursor of each, and the zero block a draw binds for a cbuffer it
    // does not fill. One ring per D3D12Device::kFramesInFlight slot - the same
    // count as D3D12UI::kFrameSlots, kept locally so this header needs no
    // d3d12_device.h.
    static constexpr uint32_t kFrameSlots = 2;
    void *pConstantRing[kFrameSlots] = {};
    void *pConstantRingMapped[kFrameSlots] = {};
    uint64_t constantRingCursor[kFrameSlots] = {};
    // The device frame number the slot's cursor was reset for: D3D12Device::
    // beginFrame has waited the slot's fence, so a new frame means the range is
    // free again.
    uint64_t constantRingFrame[kFrameSlots] = {};
    void *pZeroConstants = nullptr;
    uint64_t zeroConstantsGpu = 0;
    bool bConstantOverflowReported = false;
    std::unordered_map<std::string, void *> pipelines;
    std::string shaderDumpDir;
    uint32_t dumpedShaders = 0;
    bool bQuietPipelines = false;

    // The SRV table cache, reset when the frame slot changes: one entry per
    // distinct (count, handles) set, its handles kept for a collision-safe hit
    // test.
    struct SrvTable {
        uint64_t Key = 0;
        uint32_t Count = 0;
        uint32_t Index = 0;
        std::vector<uint64_t> Handles;
    };
    std::vector<SrvTable> srvTables;
    std::unordered_map<uint64_t, std::vector<uint32_t>> srvTablesByKey;
    uint32_t srvTableSlot = 0xFFFFFFFFu;
    uint64_t srvTableHits = 0;
    uint64_t srvTableMisses = 0;

    bool createRootSignature();
    bool createSamplerHeap();
    bool createConstantRing();
    // Reserves 'count' contiguous descriptors and returns their base index.
    uint32_t allocateTable( uint32_t count );
    // Copies one stage's blocks into the frame slot's ring; outGpu[i] is the
    // address to bind at b<i> (the zero block when the entry is empty).
    void uploadConstants( const StageConstants *pStage, uint64_t *outGpu );
    void dumpShader( const char *pName, const D3D12ShaderStage &stage );
};

// -d3d12-scenetest: the scene path without game data. Draws a lit, textured,
// depth tested cube through the engine's own mesh.vert/mesh.frag (SPIR-V ->
// HLSL -> DXBC), presents it and reports whether the frame holds geometry.
// Runs inside -renderer-selftest's device/window (D3D12RunSelfTest).
bool D3D12RunSceneTest( D3D12Device &device, uint32_t frames, const ColorRGBA &clearColor );

// -d3d12-samplertest: the one thing the scene test cannot prove - that a shader
// can *index the sampler heap dynamically*, which is the access every Eden draw
// makes (DXSO's `sampler_heap[]`, docs/backends.md section 4). Two quads of the
// same 2x1 two-colour texture and the same UVs in [0,2] are drawn with two
// different heap indices (linear wrap and linear clamp/clamp); the frame is read
// back and the two halves' mean colours are compared - wrap shows both texels
// half of the time, clamp stretches the edge one, so the halves can only match
// if the index is not reaching the sampler. Returns false on a device loss or
// when the measured colours do not differ.
bool D3D12RunSamplerTest( D3D12Device &device, uint32_t frames, const ColorRGBA &clearColor );

// -d3d12-constest: the other half of the draw an Eden material makes - its
// *constant buffers* (docs/backends.md section 3.7). b0 and b1 of the vertex
// stage carry a projection and a per-draw offset, b0 and b2 of the pixel stage a
// tint and a scale; two quads are drawn with the same pipeline and different
// blocks, and the frame is read back. The halves can only differ if every one of
// those slots reaches its stage (an ignored b1 puts both quads in the same place,
// an ignored b2 leaves the right half at full brightness). The same frame also
// measures the D3D9 state path: a third quad is drawn with SRC_ALPHA /
// INV_SRC_ALPHA over the first, so a blend state that never reached the pipeline
// shows as an unwritten pixel.
bool D3D12RunConstTest( D3D12Device &device, uint32_t frames, const ColorRGBA &clearColor );

// -d3d12-edentest: the first *original* TDU2 shader on Direct3D 12
// (docs/backends.md section 3.8). Loads Euro/Shaders through EdenShaderSystem,
// picks a library's simplest one-texture opaque permutation, compiles its VS/PS
// with D3D12ShaderCompiler, fills the constant file with the *shared* filler
// (render/eden/eden_constants.h), binds the engine's checker into the t0 table
// and draws a quad with the material's D3D9 state. It needs the file service and
// its own window, so it runs in the bootstrap next to -d3d12-shadertest rather
// than inside -renderer-selftest. Returns false when no candidate library could
// be drawn or the quad did not reach the frame.
bool D3D12RunEdenTest();

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

// -d3d12-pipetest: the breadth measurement that comes before EdenRenderer is
// pointed at the backend (docs/backends.md section 3.10). The shader tally
// (above) proves every *stage* compiles; this proves the *pairs* link and that
// the device accepts them as a pipeline. Every .sob library's permutations are
// matched VS item <-> PS item by hashcode (the pairing the libraries encode),
// the pair goes through D3D12ShaderCompiler::compilePair - so a sparse varying
// register set is padded into alignment - the vertex input layout is built from
// the vertex shader's dcl list, and D3D12Scene::getPipeline creates a real
// PSO. The tally is per library and in total: pairs, compiled, refused by the
// device, and the classes that are not a pair at all (a one-sided library, a
// permutation with no partner). It needs Euro/Shaders plus a device, so it
// runs in the bootstrap next to -d3d12-edentest and ends the run there.
bool D3D12RunPipelineTest();
