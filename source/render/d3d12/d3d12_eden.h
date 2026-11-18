#pragma once

#include "render/3dg.h"
#include "render/d3d12/d3d12_scene.h"
#include "render/d3d12/d3d12_shader.h"
// EdenSceneParams / EdenMaterial / EdenProgram: the values a draw is made of.
// The Vulkan device the header also declares is only ever used as a pointer
// here, so including it costs nothing this file did not already need.
#include "render/eden/eden_renderer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct EdenDrawParams;
struct EdenMaterial;
struct EdenProgram;
struct EdenSceneParams;
class D3D12Device;
class EdenShaderSystem;
class TextureCache;

// The Eden draw path on Direct3D 12 (docs/backends.md phase 3b step 2b).
//
// `EdenRenderer::draw` answers three questions per draw: which pipeline (a
// permutation's VS/PS *pair*, the mesh's vertex declaration, the material's
// D3D9 state), which bytes reach the shaders (the D3D9 constant file, the
// specialization block, the alpha reference and the sixteen sampler-heap
// indices) and which textures (a material's slots, bound where the *shader*
// reads them). D3D12Scene answers the mechanics - one root signature, the
// descriptor heaps, the pipeline cache, the mesh upload - and this class answers
// the Eden half, from the pieces the probes already measured:
//
//   programs   D3D12ShaderCompiler::compilePair (the pair, not the two stages:
//              the VS->PS interface is a property of the pair - 3.9)
//   meshes     D3D12PrepareMesh + D3D12Scene::createMeshStreams (3.12 / 3.11)
//   constants  FillEdenConstants / FillEdenSpecConstants - the same functions
//              the Vulkan renderer runs (render/eden/eden_constants.h)
//   textures   the material's slots re-matched from the shader's register names
//              (3.13: SPIRV-Cross renumbers the stage's textures)
//
// Everything is cached by identity, not rebuilt: a program once per
// permutation, a mesh once per primitive, a pipeline once per (program, mesh
// declaration, material state, cull) - a world draws one material thousands of
// times and the frame must not compile, allocate or stall inside it.
//
// What is *not* here: the material itself. `EdenRenderer::setupMaterial` builds
// an `EdenMaterial` from a `.2DM` entry and is backend independent (its only
// right-hand side is TextureCache), so the Direct3D 12 path uses it unchanged
// and this class starts from a material that is already resolved.

// Which D3D9 texture slot each SRV *register* of a compiled stage reads.
//
// SPIRV-Cross renumbers a stage's textures into a dense register list and keeps
// the original D3D9 slot in the variable's name:
//
//   Texture2D<float4> t0_2d  : register(t0, space0);
//   Texture2D<float4> t8_2d  : register(t5, space0);
//
// so the root signature's table is indexed by the *register*, and filling it by
// slot number binds the wrong texture wherever a stage skips a slot (docs
// backends.md 3.13: the 4x4 bound slot 5's scratch map where the shader samples
// slot 8, and a building's nine-slot materials painted wood and grass white).
// 'outSlots' holds D3D12Scene::kTextureTableSize entries, 0xFFFFFFFF where the
// register is not a 2D texture. 'outCollisions' counts two variable names
// claiming one register (a packing bug would otherwise be silent).
void D3D12EdenMapTextureRegisters( const std::string &hlsl, uint32_t *outSlots, uint32_t &outCollisions );

struct D3D12EdenProgram {
    const EdenProgram *pProgram = nullptr;
    D3D12ShaderStage VS;
    D3D12ShaderStage PS;
    // Register -> D3D9 slot, from the HLSL of each stage.
    uint32_t PSRegisters[D3D12Scene::kTextureTableSize] = {};
    uint32_t VSRegisters[D3D12Scene::kTextureTableSize] = {};
    uint32_t RegisterCollisions = 0;
    // The two stages' constant files: the full D3D9 layout (16 int4 registers,
    // kEdenVSFloatRegisters / kEdenPSFloatRegisters float4, the bool bitmask).
    // It cannot be shortened - the shader's cbuffer declares those offsets - so
    // this is also the number of bytes a draw copies into the ring per stage.
    uint32_t VSBytes = 0;
    uint32_t PSBytes = 0;
    // The widest float register the stage's table names (EdenRenderer's
    // VSUsedRegisters / PSUsedRegisters): everything before it is cleared and
    // filled per draw, the tail keeps the 1.0 stand-in.
    uint32_t VSUsedRegisters = 0;
    uint32_t PSUsedRegisters = 0;
    // "t0->s2(name) t2->s4 ...", for a log line.
    std::string RegisterMap;
    std::string Failure;
};

// One uploaded primitive: the D3D12 buffers, the declaration the pipeline needs
// and the draw count.
struct D3D12EdenMesh {
    D3D12Scene::Mesh Mesh;
    std::vector<RawVertexAttribute> Attributes;
    uint32_t Strides[D3D12Scene::Mesh::kMaxStreams] = {};
    uint32_t NumStreams = 0;
    uint32_t IndexCount = 0;
    uint32_t NumVertices = 0;
};

class D3D12Eden {
public:
    // 'pShaders' supplies the permutations (EdenShaderSystem), 'pCache' the
    // white texture an unresolved material slot binds instead of an unfilled
    // descriptor (an unfilled one can fault the GPU, not just paint wrong).
    bool initialize( D3D12Device *pDevice, D3D12Scene *pScene, TextureCache *pCache, EdenShaderSystem *pShaders );
    void destroy();
    inline bool isReady() const { return pScene != nullptr && whiteSrv != 0; }

    // The scene values a draw's constants are filled from; call it once per
    // pass. Derives the view-projection exactly like EdenRenderer::beginScene.
    void beginScene( const EdenSceneParams &params );
    inline const EdenSceneParams &getScene() const { return scene; }

    // Compiles a permutation's pair once. nullptr when it cannot be drawn; every
    // Program also carries the reason in Failure, but only a returned pointer
    // is a compiled one.
    const D3D12EdenProgram *getProgram( const EdenProgram *pProgram );

    bool createMesh( const GeometryPrimitive &primitive, D3D12EdenMesh &out );
    void destroyMesh( D3D12EdenMesh &out );

    // The material's texture table, by SRV register, with the white texture in
    // every slot the shader reads but the material does not carry.
    void resolveTextures( const D3D12EdenProgram &program, const EdenMaterial &material, uint64_t *outSrvs ) const;

    // Created on first use and cached; nullptr when the device refuses it.
    const void *getPipeline( const D3D12EdenProgram &program, const D3D12EdenMesh &mesh,
                             const EdenMaterial &material, bool bMirrored );

    // Records one draw into the frame D3D12Device::beginFrame opened. Returns
    // false (and counts it) when the material has no drawable permutation.
    bool draw( const D3D12EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
               const EdenDrawParams *pParams = nullptr );

    // What a frame cost: draws, the bytes copied into the constant ring, and
    // the tallies a probe logs (a refusal must be countable, not a black frame).
    inline uint64_t getDraws() const { return numDraws; }
    inline uint64_t getConstantBytes() const { return numConstantBytes; }
    inline uint32_t getProgramCount() const { return static_cast<uint32_t>( programs.size() ); }
    inline uint32_t getPipelineCount() const { return static_cast<uint32_t>( pipelines.size() ); }
    inline uint64_t getDrawsWithoutProgram() const { return numWithoutProgram; }
    inline uint64_t getDrawsWithoutPipeline() const { return numWithoutPipeline; }
    void resetCounters();

private:
    struct PipelineKey {
        const void *pProgram = nullptr;
        const void *pMesh = nullptr;
        uint32_t State = 0; // the material's D3D9 bytes, see stateHash()
        uint32_t Flags = 0; // mirrored / depth test / depth write
        inline bool operator==( const PipelineKey &other ) const {
            return pProgram == other.pProgram && pMesh == other.pMesh && State == other.State &&
                   Flags == other.Flags;
        }
    };
    struct PipelineKeyHash {
        inline size_t operator()( const PipelineKey &key ) const {
            uint64_t h = 0xcbf29ce484222325ull;
            h = ( h ^ reinterpret_cast<uint64_t>( key.pProgram ) ) * 0x100000001b3ull;
            h = ( h ^ reinterpret_cast<uint64_t>( key.pMesh ) ) * 0x100000001b3ull;
            h = ( h ^ key.State ) * 0x100000001b3ull;
            h = ( h ^ key.Flags ) * 0x100000001b3ull;
            return static_cast<size_t>( h ^ ( h >> 32 ) );
        }
    };

    static uint32_t stateHash( const EdenMaterial &material );

    D3D12Device *pDevice = nullptr;
    D3D12Scene *pScene = nullptr;
    TextureCache *pCache = nullptr;
    EdenShaderSystem *pShaders = nullptr;
    uint64_t whiteSrv = 0;

    EdenSceneParams scene;
    Matrix4x4f viewProjection;
    uint64_t sceneEpoch = 0;

    std::unordered_map<const EdenProgram *, std::unique_ptr<D3D12EdenProgram>> programs;
    std::unordered_map<PipelineKey, const void *, PipelineKeyHash> pipelines;

    // The per draw scratch the constant files are filled into (one pair, reused:
    // D3D12Scene::draw copies them into its ring before it returns).
    std::vector<uint8_t> vsBlock;
    std::vector<uint8_t> psBlock;
    uint8_t push[128] = {};

    uint64_t numDraws = 0;
    uint64_t numConstantBytes = 0;
    uint64_t numWithoutProgram = 0;
    uint64_t numWithoutPipeline = 0;
};
