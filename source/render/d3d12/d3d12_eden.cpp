#include "shared.h"
#include "render/d3d12/d3d12_eden.h"

#if defined( OTDU_D3D12 )

#include "render/d3d12/d3d12_device.h"
#include "render/d3d12/d3d12_mesh_builder.h"
#include "render/d3d12/d3d12_vertex_layout.h"
#include "render/eden/eden_constants.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_shader_system.h"
#include "render/texture_cache.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_set>

// The Eden half of the Direct3D 12 draw (docs/backends.md phase 3b step 2b).
// Every rule here has a twin in the Vulkan renderer *on purpose*: the same
// register must hold the same number on both backends, or the frame differs by
// a pixel nobody can explain. Where the rule is not backend specific it is a
// shared function (eden_constants.h); where it is, the Vulkan expression is
// quoted in the comment so the two can be compared line by line.

namespace {

// A stage's cbuffers, by what they *are* rather than by their number.
//
// SPIRV-Cross hands back the *instance* names DXSO emitted (`c`,
// `spec_state`, `clip_info`, `D3D9SharedPS`), so anything naming the
// specialization state is the spec block, the clip/shared blocks stay the zero
// block (the Vulkan renderer binds a zero buffer there too) and everything else
// is the stage's constant file. The returned string names the binding, for the
// log (docs/backends.md 3.8, where a strict `== "cbuffer_t"` match silently
// bound nothing and the frame held 0 covered pixels).
std::string AssignStageBlocks( const D3D12ShaderStage &stage,
                               D3D12Scene::StageConstants &out,
                               const void *pFile,
                               uint32_t fileBytes,
                               const void *pSpec,
                               uint32_t specBytes ) {
    std::string bound;
    const uint32_t count =
        Min<uint32_t>( static_cast<uint32_t>( stage.ConstantBufferNames.size() ), D3D12Scene::kMaxConstantBuffers );
    for ( uint32_t i = 0; i < count; i++ ) {
        const std::string name = ToUpper( stage.ConstantBufferNames[i] );
        const char *pWhat = nullptr;
        if ( name.find( "SPEC" ) != std::string::npos ) {
            out.pData[i] = pSpec;
            out.Sizes[i] = specBytes;
            pWhat = "spec";
        } else if ( name.find( "CLIP" ) == std::string::npos && name.find( "SHARED" ) == std::string::npos ) {
            out.pData[i] = pFile;
            out.Sizes[i] = fileBytes;
            pWhat = "file";
        }
        if ( pWhat == nullptr ) {
            pWhat = "zero";
        }
        if ( !bound.empty() ) {
            bound += ", ";
        }
        bound += "b" + std::to_string( i ) + "=" + pWhat;
    }
    return bound;
}

// The widest float register a stage's constant table names, exactly the cache
// EdenRenderer::recordDrawImpl keeps per program (VSUsedRegisters /
// PSUsedRegisters): a program with three constants and one with a hundred must
// not clear and fill the same 4 KB.
uint32_t UsedRegisters( const std::vector<ShaderConstantMember> &members, uint32_t limit, uint16_t &cache ) {
    if ( cache == 0xFFFF ) {
        uint32_t top = 0;
        for ( const ShaderConstantMember &member : members ) {
            if ( member.RegisterSet == 2 ) {
                top = Max<uint32_t>( top, member.Register + member.NumRegisters );
            }
        }
        cache = static_cast<uint16_t>( Min( top, limit ) );
    }
    return static_cast<uint32_t>( cache );
}

// (determinant < 0) != scene.bMirroredView - EdenRenderer::recordDrawImpl's
// mirroring test, which is what swaps the cull faces.
float Determinant3x3( const Matrix4x4f &m ) {
    return m.m[0][0] * ( m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1] ) -
           m.m[0][1] * ( m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0] ) +
           m.m[0][2] * ( m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0] );
}

} // namespace

void D3D12EdenMapTextureRegisters( const std::string &hlsl, uint32_t *outSlots, uint32_t &outCollisions ) {
    for ( uint32_t i = 0; i < D3D12Scene::kTextureTableSize; i++ ) {
        outSlots[i] = 0xFFFFFFFFu;
    }
    // `Texture2D<float4> t<slot>_2d : register(t<register>, space0);` - the
    // variable name keeps the D3D9 slot the shader's source used, the register
    // is SPIRV-Cross's dense renumbering. A cube or 3D texture does not match
    // the marker and is not a table entry.
    const std::string marker = "Texture2D<float4> t";
    size_t position = 0;
    while ( ( position = hlsl.find( marker, position ) ) != std::string::npos ) {
        size_t cursor = position + marker.size();
        uint32_t slot = 0;
        bool bSlotDigits = false;
        while ( cursor < hlsl.size() && std::isdigit( static_cast<unsigned char>( hlsl[cursor] ) ) ) {
            slot = slot * 10 + static_cast<uint32_t>( hlsl[cursor] - '0' );
            cursor++;
            bSlotDigits = true;
        }
        const size_t registerAt = hlsl.find( "register(t", cursor );
        if ( bSlotDigits && registerAt != std::string::npos ) {
            size_t registerCursor = registerAt + 10; // past "register(t"
            uint32_t registerIndex = 0;
            bool bRegisterDigits = false;
            while ( registerCursor < hlsl.size() &&
                    std::isdigit( static_cast<unsigned char>( hlsl[registerCursor] ) ) ) {
                registerIndex = registerIndex * 10 + static_cast<uint32_t>( hlsl[registerCursor] - '0' );
                registerCursor++;
                bRegisterDigits = true;
            }
            if ( bRegisterDigits && registerIndex < D3D12Scene::kTextureTableSize ) {
                if ( outSlots[registerIndex] != 0xFFFFFFFFu && outSlots[registerIndex] != slot ) {
                    outCollisions++;
                } else {
                    outSlots[registerIndex] = slot;
                }
            }
        }
        position = cursor;
    }
}

bool D3D12Eden::initialize( D3D12Device *pDeviceIn, D3D12Scene *pSceneIn, TextureCache *pCacheIn,
                            EdenShaderSystem *pShadersIn ) {
    if ( pDeviceIn == nullptr || pSceneIn == nullptr || pCacheIn == nullptr || pShadersIn == nullptr ||
         !pSceneIn->isReady() ) {
        return false;
    }
    pDevice = pDeviceIn;
    pScene = pSceneIn;
    pCache = pCacheIn;
    pShaders = pShadersIn;
    // Every unresolved material slot binds this instead of an unfilled
    // descriptor: a shader that samples an uninitialized descriptor may fault
    // the GPU rather than paint a wrong colour (docs/backends.md 3.13).
    GPUTexture *pWhite = pCache->getWhiteTexture();
    if ( pWhite == nullptr || pWhite->D3D12SrvStorageCpu == 0 ) {
        OTDU_LOG_ERROR( "D3D12 eden: the white texture has no Direct3D 12 upload\n" );
        destroy();
        return false;
    }
    whiteSrv = pWhite->D3D12SrvStorageCpu;
    vsBlock.assign( kEdenVSConstantsSize, 0 );
    psBlock.assign( kEdenPSConstantsSize, 0 );
    OTDU_LOG_INFO( "D3D12 eden: ready (%u + %u constant file bytes per draw, 16 texture(s), 16 sampler(s))\n",
                   kEdenVSConstantsSize, kEdenPSConstantsSize );
    return true;
}

void D3D12Eden::destroy() {
    for ( auto &entry : programs ) {
        entry.second.reset();
    }
    programs.clear();
    // The pipelines belong to D3D12Scene's cache (releasePipelines), the meshes
    // to whoever created them (destroyMesh): nothing here owns a GPU resource.
    pipelines.clear();
    pDevice = nullptr;
    pScene = nullptr;
    pCache = nullptr;
    pShaders = nullptr;
    whiteSrv = 0;
}

void D3D12Eden::beginScene( const EdenSceneParams &params ) {
    scene = params;
    sceneEpoch++;
    viewProjection = scene.Projection.mulPrecise( scene.View );
}

const D3D12EdenProgram *D3D12Eden::getProgram( const EdenProgram *pEdenProgram ) {
    if ( pEdenProgram == nullptr ) {
        return nullptr;
    }
    auto existing = programs.find( pEdenProgram );
    if ( existing != programs.end() ) {
        return existing->second->VS.bReady ? existing->second.get() : nullptr;
    }
    std::unique_ptr<D3D12EdenProgram> pCreated = std::make_unique<D3D12EdenProgram>();
    pCreated->pProgram = pEdenProgram;
    D3D12EdenProgram &out = *pCreated;
    programs.emplace( pEdenProgram, std::move( pCreated ) );

    if ( pEdenProgram->pVS == nullptr || pEdenProgram->pPS == nullptr ) {
        out.Failure = "the permutation has no stage pair";
        return nullptr;
    }
    // The pair, not the two stages: a D3D9 shader skips varying registers, and
    // the pixel stage's input struct is padded to the vertex stage's outputs
    // (docs/backends.md 3.9).
    if ( !D3D12ShaderCompiler::compilePair( *pEdenProgram->pVS, *pEdenProgram->pPS, out.VS, out.PS,
                                            pEdenProgram->Name.c_str() ) ) {
        out.Failure = "compile: " + out.VS.Error + " " + out.PS.Error;
        OTDU_LOG_WARN( "D3D12 eden: '%s' does not compile: %s\n", pEdenProgram->Name.c_str(),
                       out.Failure.c_str() );
        return nullptr;
    }
    D3D12EdenMapTextureRegisters( out.PS.HLSL, out.PSRegisters, out.RegisterCollisions );
    D3D12EdenMapTextureRegisters( out.VS.HLSL, out.VSRegisters, out.RegisterCollisions );
    for ( uint32_t i = 0; i < D3D12Scene::kTextureTableSize; i++ ) {
        const uint32_t slot = out.PSRegisters[i] != 0xFFFFFFFFu ? out.PSRegisters[i] : out.VSRegisters[i];
        if ( slot == 0xFFFFFFFFu ) {
            continue;
        }
        if ( !out.RegisterMap.empty() ) {
            out.RegisterMap += " ";
        }
        out.RegisterMap += "t" + std::to_string( i ) + "->s" + std::to_string( slot );
    }
    // The clear / fill extent, and the block size. The block is the *full* D3D9
    // constant file: the shader's cbuffer declares those offsets (the bool
    // bitmask sits at the end of it), so it cannot be shortened to what one
    // program happens to name.
    out.VSUsedRegisters =
        UsedRegisters( pEdenProgram->VSMembers, kEdenVSFloatRegisters, pEdenProgram->VSUsedRegisters );
    out.PSUsedRegisters =
        UsedRegisters( pEdenProgram->PSMembers, kEdenPSFloatRegisters, pEdenProgram->PSUsedRegisters );
    out.VSBytes = kEdenVSConstantsSize;
    out.PSBytes = kEdenPSConstantsSize;
    return &out;
}

bool D3D12Eden::createMesh( const GeometryPrimitive &primitive, D3D12EdenMesh &out ) {
    if ( pScene == nullptr ) {
        return false;
    }
    D3D12MeshData meshData;
    if ( !D3D12PrepareMesh( primitive, meshData ) || meshData.NumStreams == 0 || meshData.Indices.empty() ) {
        return false;
    }
    const void *pStreams[D3D12Scene::Mesh::kMaxStreams] = {};
    uint32_t streamBytes[D3D12Scene::Mesh::kMaxStreams] = {};
    for ( uint32_t stream = 0; stream < meshData.NumStreams; stream++ ) {
        pStreams[stream] = meshData.Streams[stream].data();
        streamBytes[stream] = static_cast<uint32_t>( meshData.Streams[stream].size() );
    }
    if ( !pScene->createMeshStreams( pStreams, streamBytes, meshData.Strides, meshData.NumStreams,
                                     meshData.Indices.data(), static_cast<uint32_t>( meshData.Indices.size() ),
                                     out.Mesh ) ) {
        return false;
    }
    out.Attributes = meshData.Attributes;
    for ( uint32_t stream = 0; stream < D3D12Scene::Mesh::kMaxStreams; stream++ ) {
        out.Strides[stream] = meshData.Strides[stream];
    }
    out.NumStreams = meshData.NumStreams;
    out.IndexCount = static_cast<uint32_t>( meshData.Indices.size() );
    out.NumVertices = meshData.NumVertices;
    return true;
}

void D3D12Eden::destroyMesh( D3D12EdenMesh &out ) {
    if ( pScene != nullptr ) {
        pScene->destroyMesh( out.Mesh );
    }
    out.Attributes.clear();
    out.IndexCount = 0;
    out.NumVertices = 0;
}

void D3D12Eden::resolveTextures( const D3D12EdenProgram &program, const EdenMaterial &material,
                                 uint64_t *outSrvs ) const {
    for ( uint32_t i = 0; i < D3D12Scene::kTextureTableSize; i++ ) {
        const uint32_t slot =
            program.PSRegisters[i] != 0xFFFFFFFFu ? program.PSRegisters[i] : program.VSRegisters[i];
        GPUTexture *pTexture =
            slot != 0xFFFFFFFFu && slot < material.NumSlots ? material.pTextures[slot] : nullptr;
        outSrvs[i] = pTexture != nullptr && pTexture->D3D12SrvStorageCpu != 0 ? pTexture->D3D12SrvStorageCpu
                                                                             : whiteSrv;
    }
}

uint32_t D3D12Eden::stateHash( const EdenMaterial &material ) {
    // The six bytes a D3D9 material's pipeline state comes from
    // (PipelineDesc::D3D9State); BlendFactor is dynamic state and the alpha test
    // is a `discard` in the shader, so neither is part of the key.
    const uint8_t bytes[6] = { material.SrcBlend,  material.DstBlend,        material.BlendOp,
                               material.DepthFunc, material.ColorWriteMask, material.CullMode };
    uint32_t h = 2166136261u;
    for ( uint8_t byte : bytes ) {
        h = ( h ^ byte ) * 16777619u;
    }
    return h;
}

const void *D3D12Eden::getPipeline( const D3D12EdenProgram &program, const D3D12EdenMesh &mesh,
                                    const EdenMaterial &material, bool bMirrored ) {
    if ( pScene == nullptr || !program.VS.bReady || !program.PS.bReady ) {
        return nullptr;
    }
    PipelineKey key;
    key.pProgram = program.pProgram;
    key.pMesh = static_cast<const void *>( &mesh );
    key.State = stateHash( material );
    key.Flags = ( bMirrored ? 1u : 0u ) | ( material.bDepthTest ? 2u : 0u ) | ( material.bDepthWrite ? 4u : 0u );
    auto existing = pipelines.find( key );
    if ( existing != pipelines.end() ) {
        return existing->second;
    }

    // The input layout comes from the pair: the vertex stage's dcl list matched
    // against the mesh's own .3DG declaration (docs/backends.md 3.12).
    D3D12VertexLayout layout;
    const bool bHasLayout =
        mesh.NumStreams > 0 && D3D12BuildVertexLayout( *program.pProgram->pVS, mesh.Attributes.data(),
                                                      static_cast<uint32_t>( mesh.Attributes.size() ),
                                                      mesh.Strides, mesh.NumStreams, layout );
    if ( mesh.NumStreams > 0 && !bHasLayout ) {
        return nullptr;
    }

    D3D12Scene::D3D9State state;
    state.SrcBlend = material.SrcBlend;
    state.DstBlend = material.DstBlend;
    state.BlendOp = material.BlendOp;
    state.DepthFunc = material.DepthFunc;
    state.ColorWriteMask = material.ColorWriteMask;
    state.CullMode = material.CullMode;
    state.bMirrored = bMirrored;

    D3D12Scene::PipelineDesc desc;
    desc.pVS = &program.VS;
    desc.pPS = &program.PS;
    if ( bHasLayout ) {
        desc.pAttributes = layout.Attributes.data();
        desc.NumAttributes = static_cast<uint32_t>( layout.Attributes.size() );
        desc.pStrides = layout.Strides.data();
        desc.NumStreams = layout.NumStreams;
    }
    desc.pState = &state;
    desc.bDepthTest = material.bDepthTest;
    desc.bDepthWrite = material.bDepthWrite;
    const void *pPipeline = pScene->getPipeline( desc );
    if ( pPipeline == nullptr ) {
        return nullptr;
    }
    pipelines.emplace( key, pPipeline );
    return pPipeline;
}

bool D3D12Eden::draw( const D3D12EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
                      const EdenDrawParams *pParams ) {
    if ( pScene == nullptr || whiteSrv == 0 ) {
        return false;
    }
    const D3D12EdenProgram *pProgram = getProgram( material.pProgram );
    if ( pProgram == nullptr ) {
        numWithoutProgram++;
        return false;
    }
    // EdenRenderer::recordDrawImpl's mirroring test: a negative determinant (a
    // mirrored instance) or a mirrored view swaps the cull faces.
    const bool bMirrored = ( Determinant3x3( world ) < 0.0f ) != scene.bMirroredView;
    const void *pPipeline = getPipeline( *pProgram, mesh, material, bMirrored );
    if ( pPipeline == nullptr ) {
        numWithoutPipeline++;
        return false;
    }

    // The two constant files. Only what the shader can read is touched: the
    // int4 registers, the float registers the stage's table names, and the bool
    // bitmask at the end of the block. A register the filler has no value for
    // keeps the 1.0 stand-in (the value -d3d12-3dgtest measured the frame with).
    auto fillStage = [&]( std::vector<uint8_t> &block, uint32_t usedRegisters, uint32_t limit ) {
        const uint32_t head = kEdenIntRegistersSize + usedRegisters * 16;
        memset( block.data(), 0, head );
        memset( block.data() + block.size() - kEdenBoolBitmaskSize, 0, kEdenBoolBitmaskSize );
        float *pRegisters = reinterpret_cast<float *>( block.data() + kEdenIntRegistersSize );
        for ( uint32_t i = 0; i < usedRegisters * 4 && i < limit * 4; i++ ) {
            pRegisters[i] = 1.0f;
        }
    };
    fillStage( vsBlock, pProgram->VSUsedRegisters, kEdenVSFloatRegisters );
    fillStage( psBlock, pProgram->PSUsedRegisters, kEdenPSFloatRegisters );

    std::unordered_set<std::string> warnedParameters;
    EdenConstantsContext context;
    context.pScene = &scene;
    context.pViewProjection = &viewProjection;
    context.SceneEpoch = sceneEpoch;
    context.pWarned = &warnedParameters;
    float *pVSRegisters = reinterpret_cast<float *>( vsBlock.data() + kEdenIntRegistersSize );
    float *pPSRegisters = reinterpret_cast<float *>( psBlock.data() + kEdenIntRegistersSize );
    FillEdenConstants( context, pProgram->pProgram->VSMembers, pVSRegisters, kEdenVSFloatRegisters, material,
                       world, pParams, pProgram->pProgram->Name.c_str(), false );
    FillEdenConstants( context, pProgram->pProgram->PSMembers, pPSRegisters, kEdenPSFloatRegisters, material,
                       world, pParams, pProgram->pProgram->Name.c_str(), true );

    uint32_t specDwords[kEdenSpecDwords] = {};
    FillEdenSpecConstants( specDwords, material.bAlphaTest );

    D3D12Scene::StageConstants vsConstants;
    D3D12Scene::StageConstants psConstants;
    AssignStageBlocks( pProgram->VS, vsConstants, vsBlock.data(), pProgram->VSBytes, specDwords,
                       sizeof( specDwords ) );
    AssignStageBlocks( pProgram->PS, psConstants, psBlock.data(), pProgram->PSBytes, specDwords,
                       sizeof( specDwords ) );

    // The push block: the alpha reference the DXSO pixel stage discards on, and
    // the sixteen sampler heap indices the material's slots name - the same
    // bytes EdenRenderer pushes as Vulkan push constants.
    memset( push, 0, sizeof( push ) );
    const uint32_t alphaRef = material.AlphaRef;
    memcpy( push + kEdenPushAlphaRefOffset, &alphaRef, sizeof( alphaRef ) );
    for ( uint32_t i = 0; i < 16; i++ ) {
        const uint16_t samplerIndex = material.SamplerIndex[i];
        memcpy( push + kEdenPushSamplerOffset + i * 2, &samplerIndex, sizeof( samplerIndex ) );
    }

    uint64_t srvs[D3D12Scene::kTextureTableSize] = {};
    resolveTextures( *pProgram, material, srvs );

    pScene->draw( pPipeline, mesh.Mesh, push, sizeof( push ), srvs, D3D12Scene::kTextureTableSize,
                  &vsConstants, &psConstants, material.BlendFactor );
    numDraws++;
    numConstantBytes += pProgram->VSBytes + pProgram->PSBytes;
    return true;
}

void D3D12Eden::resetCounters() {
    numDraws = 0;
    numConstantBytes = 0;
    numWithoutProgram = 0;
    numWithoutPipeline = 0;
}

#endif // OTDU_D3D12
