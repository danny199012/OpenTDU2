#include "shared.h"
#include "render/d3d12/d3d12_asset_probe.h"

#if defined( OTDU_D3D12 )

#include "render/2dm.h"
#include "render/3dd.h"
#include "render/3dg.h"
#include "render/d3d12/d3d12_device.h"
#include "render/d3d12/d3d12_mesh_builder.h"
#include "render/d3d12/d3d12_scene.h"
#include "render/d3d12/d3d12_shader.h"
#include "render/d3d12/d3d12_vertex_layout.h"
#include "render/eden/eden_constants.h"
#include "render/eden/eden_material_handlers.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_shader_system.h"
#include "render/sampler_heap.h"
#include "render/texture_cache.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include <SDL3/SDL.h>

#include <windows.h>

#include <cmath>
#include <deque>
#include <set>

namespace {

// A frame's worth of draws, bounded so the table heap cannot run out: every
// draw binds a 16 entry SRV table and D3D12Scene::kTableDescriptors is 4096 per
// frame slot, so 96 draws (1536 descriptors) leave room for the 2D layer.
constexpr uint32_t kMaxProbeDraws = 96;
constexpr uint32_t kProbeFrames = 30;
// b0..b3 of each stage; the widest .sob stage measured declares three.
constexpr uint32_t kMaxBlocks = D3D12Scene::kMaxConstantBuffers;

// The 2DM slot address byte -> 0 wrap, 1 mirror, 2 clamp: the rule of
// EdenRenderer::AddressIndex (eden_renderer.cpp), repeated here because the
// Vulkan renderer's copy is file static. 3 (D3DTADDRESS_CLAMP in D3D9) stays
// wrap for the measured reason documented there - the world's slot bytes that
// carry a 3 leave [0,1] on that axis, and clamping collapses them to one texel
// row.
uint32_t AddressMode( uint8_t d3dAddress ) {
    switch ( d3dAddress ) {
    case 2:
    case 5:
        return 1;
    case 3:
    case 4:
        return 0;
    default:
        return 0;
    }
}

// The graffiti atlases (-notags) behave like any other slot here: the probe has
// no scene handler to drop them, and a car has none. Kept for parity with
// EdenRenderer::setupMaterial's hash-0 rule.
bool IsTagTexture( uint64_t textureHash ) {
    static const bool bNoTags = CmdLine::HasArg( "-notags" );
    if ( !bNoTags || textureHash == 0 ) {
        return false;
    }
    return ToUpper( IdentifierToString( textureHash ) ).rfind( "TAGS", 0 ) == 0;
}

// The determinant of the 3x3 part, which is what decides a mirrored instance
// (EdenRenderer::recordDrawImpl: (determinant < 0) != scene.bMirroredView).
float Determinant3x3( const Matrix4x4f &m ) {
    return m.m[0][0] * ( m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1] ) -
           m.m[0][1] * ( m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0] ) +
           m.m[0][2] * ( m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0] );
}

// One material of the bank: the .2DM entry, the runtime material Eden would
// build from it, the .sob program that came out of the switch tuple, and the
// compiled D3D12 pair. Compiled once per material hash, however many primitives
// draw with it.
struct CompiledMaterial {
    bool bTried = false;
    bool bReady = false;
    EdenMaterial Material;
    int32_t Bucket = 0x39;
    const EdenProgram *pProgram = nullptr;
    D3D12ShaderStage VS;
    D3D12ShaderStage PS;
    uint32_t NumTexturesResolved = 0;
    uint32_t NumTexturesMissing = 0;
    std::string MissingSlots;
    std::string Failure;
};

// One draw: the uploaded mesh, the pipeline state object, the cbuffer blocks
// and the texture table. Everything is prepared before the frame loop, because
// a draw recorded inside beginFrame/endFrame must not compile, allocate or
// stall.
struct ProbeDraw {
    D3D12Scene::Mesh Mesh;
    Matrix4x4f World = Matrix4x4f::Identity();
    const void *pPipeline = nullptr;
    uint64_t Srvs[D3D12Scene::kTextureTableSize] = {};
    uint8_t Push[128] = {};
    std::vector<uint8_t> VSBlock;
    std::vector<uint8_t> PSBlock;
    D3D12Scene::StageConstants VSConstants;
    D3D12Scene::StageConstants PSConstants;
    float BlendFactor = 1.0f;
    uint32_t IndexCount = 0;
    uint32_t NumVertices = 0;
};

// What one bank produced: the parsed data, the draw list and the tally the log
// reports. The parsed scene / geometry / material libraries stay here because
// the camera (which frames the model) has to be built before the draws - the
// radius comes from the geometry's own bounds.
struct PreparedModel {
    std::string BankPath;
    std::string SceneEntry;
    std::string GeometryEntry;
    std::string MaterialEntry;
    Render3DD SceneDesc;
    Render3DG Geometry;
    Render2DM Materials;
    uint32_t NumNodes = 0;
    uint32_t NumPrimitives = 0;
    uint32_t NumMaterials = 0;
    uint32_t NumTexturesUploaded = 0;
    uint32_t NumPrimitivesSkipped = 0;
    uint32_t NumPrimitivesNoMaterial = 0;
    uint32_t NumPrimitivesNoProgram = 0;
    uint32_t NumPrimitivesNoLayout = 0;
    uint32_t NumPrimitivesNoPipeline = 0;
    std::set<std::string> Programs;
    std::vector<std::unique_ptr<CompiledMaterial>> CompiledMaterials;
    std::vector<std::unique_ptr<ProbeDraw>> Draws;
    Vector3f Center;
    float Radius = 1.0f;
};

// The cbuffers of one stage, by what they are rather than by their number:
// SPIRV-Cross hands back the *instance* names DXSO emitted (`c`, `spec_state`,
// `clip_info`, `D3D9SharedPS`), so anything naming the specialization state is
// the spec block, the clip/shared blocks stay the zero block (the Vulkan
// renderer binds a zero buffer there too) and everything else is the stage's
// constant file. A returned string names the binding, for the log.
std::string AssignStageBlocks( const D3D12ShaderStage &stage,
                               D3D12Scene::StageConstants &out,
                               const void *pFile,
                               uint32_t fileBytes,
                               const void *pSpec,
                               uint32_t specBytes ) {
    std::string bound;
    const uint32_t count =
        Min<uint32_t>( static_cast<uint32_t>( stage.ConstantBufferNames.size() ), kMaxBlocks );
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

// Builds the runtime material of one .2DM entry exactly the way
// EdenRenderer::setupMaterial does - the same states, the same slot addressing,
// the same texture lookup by hashcode and owner, the same switch tuple - and
// then compiles its .sob pair for Direct3D 12 (compilePair, so a sparse varying
// register set is padded into alignment, docs/backends.md 3.9).
bool CompileMaterial( const MaterialDesc &desc,
                      TextureCache &textureCache,
                      EdenShaderSystem &shaderSystem,
                      const char *pOwner,
                      CompiledMaterial &out ) {
    EdenMaterial &material = out.Material;
    material.Name = desc.Name;
    material.Hashcode = desc.Hashcode;
    material.ShaderSetGUID = desc.ShaderSetGUID;
    memcpy( material.Spec, desc.Spec, sizeof( material.Spec ) );
    memcpy( material.States, desc.States, sizeof( material.States ) );
    material.Parameters = desc.Parameters;
    material.Diffuse = desc.DiffuseColor;
    material.Ambient = desc.AmbientColor;
    material.Specular = desc.SpecularColor;
    material.Emissive = desc.EmissiveColor;
    material.SrcBlend = desc.SrcBlend;
    material.DstBlend = desc.DstBlend;
    material.CullMode = desc.CullMode;
    material.AlphaRef = desc.AlphaRef;
    material.bAlphaTest = desc.AlphaRef != 0 && desc.AlphaTest == 0;
    material.bDepthTest = desc.DepthTest != 0;
    material.bDepthWrite = desc.DepthWrite != 0;

    for ( uint32_t i = 0; i < 16; i++ ) {
        material.pTextures[i] = nullptr;
        material.SamplerIndex[i] = SH_LinearWrap;
    }
    for ( size_t i = 0; i < desc.AllTextures.size() && i < 16; i++ ) {
        const uint64_t textureHash =
            IsTagTexture( desc.AllTextures[i].TextureHashcode ) ? 0 : desc.AllTextures[i].TextureHashcode;
        const uint32_t addressU = AddressMode( desc.AllTextures[i].AddressU );
        const uint32_t addressV = AddressMode( desc.AllTextures[i].AddressV );
        if ( addressU != 0 || addressV != 0 ) {
            material.SamplerIndex[i] = static_cast<uint8_t>( SH_LinearUV + 3 * addressU + addressV );
        }
        if ( textureHash == 0 ) {
            continue;
        }
        GPUTexture *pTexture = textureCache.find( pOwner, textureHash );
        if ( pTexture == nullptr ) {
            pTexture = textureCache.findForOwner( pOwner, textureHash );
        }
        material.pTextures[i] = pTexture;
        if ( pTexture == nullptr || pTexture->D3D12SrvStorageCpu == 0 ) {
            out.NumTexturesMissing++;
            if ( !out.MissingSlots.empty() ) {
                out.MissingSlots += ", ";
            }
            // The hash, not IdentifierToString: a hash whose bytes are not
            // printable (a runtime slot such as CUBEMAP / SHADOW) comes back as
            // mojibake.
            char name[24];
            snprintf( name, sizeof( name ), "%016llx", static_cast<unsigned long long>( textureHash ) );
            out.MissingSlots += name;
            if ( pTexture != nullptr ) {
                out.MissingSlots += "(no d3d12 upload)";
            }
        } else {
            out.NumTexturesResolved++;
        }
    }
    for ( size_t i = 0; i < 16; i++ ) {
        material.TextureHashes[i] = i < desc.AllTextures.size() && !IsTagTexture( desc.AllTextures[i].TextureHashcode )
                                       ? desc.AllTextures[i].TextureHashcode
                                       : 0;
        material.SlotTypes[i] = i < desc.AllTextures.size() ? desc.AllTextures[i].SlotType : 0;
    }
    material.NumSlots = static_cast<uint32_t>( Min<size_t>( desc.AllTextures.size(), 16 ) );

    // The original renderer's per-family handler (FUN_00d71900, the same call
    // EdenRenderer::setupMaterial makes): it rewrites the permutation switches
    // and sometimes the shader GUID for a family that has no library of its own,
    // rewrites the render states, and returns the render bucket. Without it the
    // switch tuple is the .2DM's raw one and getProgram falls back to the nearest
    // permutation - which draws, but not what the game draws.
    out.Bucket = ApplyEdenMaterialHandler( material );
    EdenRenderer::applyRenderStates( material );
    return true;
}

// The .sob program the material's switches select, then the compiled pair:
// EdenRenderer::updateProgram's generic rule (S_NBTEXTURES counts the slots in
// use) and getProgram's own "closest permutation" fallback, which is what the
// Vulkan renderer draws with too.
bool ResolveMaterialProgram( CompiledMaterial &out, EdenShaderSystem &shaderSystem ) {
    EdenMaterial &material = out.Material;
    uint8_t spec[EdenShaderSystem::kNumSpecs];
    memcpy( spec, material.Spec, sizeof( spec ) );
    uint32_t numTextures = 0;
    for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
        if ( material.pTextures[i] != nullptr ) {
            numTextures++;
        }
    }
    if ( numTextures > 0 ) {
        spec[EdenShaderSystem::S_NBTEXTURES] = static_cast<uint8_t>( numTextures );
    }
    out.pProgram = shaderSystem.getProgram( material.ShaderSetGUID, spec );
    material.pProgram = out.pProgram;
    if ( out.pProgram == nullptr || out.pProgram->pVS == nullptr || out.pProgram->pPS == nullptr ) {
        out.Failure = "no .sob permutation (guid " + IdentifierToString( material.ShaderSetGUID ) + ", " +
                      std::to_string( numTextures ) + " texture(s))";
        return false;
    }
    if ( !D3D12ShaderCompiler::compilePair( *out.pProgram->pVS, *out.pProgram->pPS, out.VS, out.PS,
                                            out.pProgram->Name.c_str() ) ) {
        out.Failure = "compile: " + out.VS.Error + " " + out.PS.Error;
        return false;
    }
    return true;
}

// The per-primitive half: the material its scene node names, the .sob program
// its switches select, the .3DG streams, the vertex input layout, the pipeline
// and the cbuffer / texture tables of the draw.
bool BuildDraws( const Render3DD &sceneDesc,
                 const Render3DG &geometry,
                 const Render2DM &materials,
                 const char *pOwner,
                 const EdenSceneParams &sceneParams,
                 EdenShaderSystem &shaderSystem,
                 TextureCache &textureCache,
                 D3D12Scene &sceneRenderer,
                 PreparedModel &out ) {
    GPUTexture *pWhite = textureCache.getWhiteTexture();
    if ( pWhite == nullptr || pWhite->D3D12SrvStorageCpu == 0 ) {
        OTDU_LOG_ERROR( "D3D12 3dgtest: the white texture could not be uploaded\n" );
        return false;
    }
    out.NumNodes = static_cast<uint32_t>( sceneDesc.getNodes().size() );
    uint32_t warningBudget = 8;
    uint32_t loggedMaterials = 0;
    const Matrix4x4f toOrigin = Matrix4x4f::Translation(
        Vector3f( -out.Center.x, -out.Center.y, -out.Center.z ) );
    // A three-quarter view: head-on a car is a box, and the point of the probe is
    // that the *shape* arrives. Rotation about Y in the row-vector convention,
    // applied after the recentring (so the model turns about its own centre).
    constexpr float kYaw = 0.62f; // ~35 degrees
    Matrix4x4f orient = Matrix4x4f::Identity();
    orient.m[0][0] = std::cos( kYaw );
    orient.m[2][0] = std::sin( kYaw );
    orient.m[0][2] = -std::sin( kYaw );
    orient.m[2][2] = std::cos( kYaw );
    const Matrix4x4f toOriginAndTurn = toOrigin * orient;
    const Matrix4x4f viewProjection = sceneParams.Projection.mulPrecise( sceneParams.View );

    for ( const SceneNode &node : sceneDesc.getNodes() ) {
        const Geometry *pGeometry = geometry.find( node.ObjectHashcode );
        if ( pGeometry == nullptr ) {
            continue;
        }
        for ( const GeometryPrimitive &primitive : pGeometry->Primitives ) {
            out.NumPrimitives++;
            if ( primitive.Indices.empty() || primitive.NumRawVertex == 0 ) {
                out.NumPrimitivesSkipped++;
                continue;
            }
            if ( out.Draws.size() >= kMaxProbeDraws ) {
                out.NumPrimitivesSkipped++;
                continue;
            }
            const uint64_t materialHash =
                primitive.MaterialIndex < node.Materials.size() ? node.Materials[primitive.MaterialIndex] : 0;
            const MaterialDesc *pDesc = materials.find( materialHash );
            if ( pDesc == nullptr ) {
                out.NumPrimitivesNoMaterial++;
                if ( warningBudget-- > 0 ) {
                    OTDU_LOG_WARN( "D3D12 3dgtest: node '%s' slot %u names material %016llx, not in the .2dm\n",
                                   node.Name.c_str(), primitive.MaterialIndex,
                                   static_cast<unsigned long long>( materialHash ) );
                }
                continue;
            }

            // One material hash, one program pair, however many primitives draw
            // with it.
            CompiledMaterial *pCompiled = nullptr;
            for ( const std::unique_ptr<CompiledMaterial> &pExisting : out.CompiledMaterials ) {
                if ( pExisting->Material.Hashcode == materialHash ) {
                    pCompiled = pExisting.get();
                    break;
                }
            }
            if ( pCompiled == nullptr ) {
                auto pNew = std::make_unique<CompiledMaterial>();
                CompileMaterial( *pDesc, textureCache, shaderSystem, pOwner, *pNew );
                pNew->bTried = true;
                pNew->bReady = ResolveMaterialProgram( *pNew, shaderSystem );
                if ( loggedMaterials < 32 ) {
                    loggedMaterials++;
                    if ( pNew->bReady ) {
                        OTDU_LOG_INFO( "D3D12 3dgtest: material '%s' -> %s (%u switch(es) from the .2DM's, the "
                                       "nearest permutation); blend %u/%u op %u cull %u bucket 0x%x depth %d/%d, "
                                       "%u of %u texture(s) resolved%s%s\n",
                                       pDesc->Name.c_str(), pNew->pProgram->Name.c_str(),
                                       static_cast<unsigned>( pNew->pProgram->NumDifferences ),
                                       pNew->Material.SrcBlend, pNew->Material.DstBlend, pNew->Material.BlendOp,
                                       pNew->Material.CullMode, static_cast<uint32_t>( pNew->Bucket ),
                                       pNew->Material.bDepthTest ? 1 : 0,
                                       pNew->Material.bDepthWrite ? 1 : 0, pNew->NumTexturesResolved,
                                       pNew->Material.NumSlots,
                                       pNew->MissingSlots.empty() ? "" : "; missing ", pNew->MissingSlots.c_str() );
                    } else {
                        OTDU_LOG_WARN( "D3D12 3dgtest: material '%s' cannot draw: %s\n", pDesc->Name.c_str(),
                                       pNew->Failure.c_str() );
                    }
                }
                pCompiled = pNew.get();
                out.CompiledMaterials.push_back( std::move( pNew ) );
            }
            if ( !pCompiled->bReady ) {
                out.NumPrimitivesNoProgram++;
                continue;
            }

            D3D12MeshData meshData;
            if ( !D3D12PrepareMesh( primitive, meshData ) || meshData.NumStreams == 0 || meshData.Indices.empty() ) {
                out.NumPrimitivesSkipped++;
                if ( warningBudget-- > 0 ) {
                    OTDU_LOG_WARN( "D3D12 3dgtest: a primitive of node '%s' (%u vertex(es), %zu index(es)) did not "
                                   "convert\n",
                                   node.Name.c_str(), primitive.NumRawVertex, primitive.Indices.size() );
                }
                continue;
            }
            D3D12VertexLayout layout;
            if ( !D3D12BuildVertexLayout( *pCompiled->pProgram->pVS, meshData.Attributes.data(),
                                          static_cast<uint32_t>( meshData.Attributes.size() ), meshData.Strides,
                                          meshData.NumStreams, layout ) ) {
                out.NumPrimitivesNoLayout++;
                continue;
            }

            // The node's own place in the model, recentred and turned for the
            // camera. Row-vector convention, the one EdenModel::draw composes
            // with (`world * nodeTransforms[..]`): the node transform first.
            const Matrix4x4f world = node.World * toOriginAndTurn;

            D3D12Scene::D3D9State state;
            state.SrcBlend = pCompiled->Material.SrcBlend;
            state.DstBlend = pCompiled->Material.DstBlend;
            state.BlendOp = pCompiled->Material.BlendOp;
            state.DepthFunc = pCompiled->Material.DepthFunc;
            state.ColorWriteMask = pCompiled->Material.ColorWriteMask;
            state.CullMode = pCompiled->Material.CullMode;
            // A mirrored instance swaps the cull faces (EdenRenderer::draw passes
            // (determinant < 0) != scene.bMirroredView; the probe's camera is the
            // normal one, so the determinant is the whole answer).
            state.bMirrored = Determinant3x3( world ) < 0.0f;

            D3D12Scene::PipelineDesc pipelineDesc;
            pipelineDesc.pVS = &pCompiled->VS;
            pipelineDesc.pPS = &pCompiled->PS;
            pipelineDesc.pAttributes = layout.Attributes.data();
            pipelineDesc.NumAttributes = static_cast<uint32_t>( layout.Attributes.size() );
            pipelineDesc.pStrides = layout.Strides.data();
            pipelineDesc.NumStreams = layout.NumStreams;
            pipelineDesc.pState = &state;
            pipelineDesc.bDepthTest = pCompiled->Material.bDepthTest;
            pipelineDesc.bDepthWrite = pCompiled->Material.bDepthWrite;
            const void *pPipeline = sceneRenderer.getPipeline( pipelineDesc );
            if ( pPipeline == nullptr ) {
                out.NumPrimitivesNoPipeline++;
                continue;
            }

            auto pDraw = std::make_unique<ProbeDraw>();
            pDraw->World = world;
            pDraw->pPipeline = pPipeline;
            pDraw->IndexCount = static_cast<uint32_t>( meshData.Indices.size() );
            pDraw->NumVertices = primitive.NumRawVertex;
            pDraw->BlendFactor = pCompiled->Material.BlendFactor;

            // The cbuffer blocks, filled by the same code the Vulkan renderer
            // runs: a register the filler has no value for keeps the 1.0
            // stand-in, exactly like EdenRenderer.
            pDraw->VSBlock.assign( kEdenVSConstantsSize, 0 );
            pDraw->PSBlock.assign( kEdenPSConstantsSize, 0 );
            float *pVSRegisters = reinterpret_cast<float *>( pDraw->VSBlock.data() + kEdenIntRegistersSize );
            float *pPSRegisters = reinterpret_cast<float *>( pDraw->PSBlock.data() + kEdenIntRegistersSize );
            for ( uint32_t i = 0; i < kEdenVSFloatRegisters * 4; i++ ) {
                pVSRegisters[i] = 1.0f;
            }
            for ( uint32_t i = 0; i < kEdenPSFloatRegisters * 4; i++ ) {
                pPSRegisters[i] = 1.0f;
            }
            std::unordered_set<std::string> warnedParameters;
            EdenConstantsContext context;
            context.pScene = &sceneParams;
            context.pViewProjection = &viewProjection;
            context.SceneEpoch = 1;
            context.pWarned = &warnedParameters;
            FillEdenConstants( context, pCompiled->pProgram->VSMembers, pVSRegisters, kEdenVSFloatRegisters,
                               pCompiled->Material, world, nullptr, pCompiled->pProgram->Name.c_str(), false );
            FillEdenConstants( context, pCompiled->pProgram->PSMembers, pPSRegisters, kEdenPSFloatRegisters,
                               pCompiled->Material, world, nullptr, pCompiled->pProgram->Name.c_str(), true );
            uint32_t specDwords[kEdenSpecDwords] = {};
            FillEdenSpecConstants( specDwords, pCompiled->Material.bAlphaTest );
            AssignStageBlocks( pCompiled->VS, pDraw->VSConstants, pDraw->VSBlock.data(),
                               static_cast<uint32_t>( pDraw->VSBlock.size() ), specDwords, sizeof( specDwords ) );
            AssignStageBlocks( pCompiled->PS, pDraw->PSConstants, pDraw->PSBlock.data(),
                               static_cast<uint32_t>( pDraw->PSBlock.size() ), specDwords, sizeof( specDwords ) );

            // The push block (b13 space1): the alpha reference and the sixteen
            // sampler heap indices, at the byte offsets EdenRenderer writes
            // (kPushAlphaRefOffset / kPushSamplerOffset).
            const uint32_t alphaRef = pCompiled->Material.AlphaRef;
            memcpy( pDraw->Push + 24, &alphaRef, sizeof( alphaRef ) );
            for ( uint32_t i = 0; i < 16; i++ ) {
                const uint16_t samplerIndex = pCompiled->Material.SamplerIndex[i];
                memcpy( pDraw->Push + 64 + i * 2, &samplerIndex, sizeof( samplerIndex ) );
            }

            // The material's sixteen texture slots: an unresolved slot binds the
            // white texture rather than an unfilled descriptor, because a shader
            // that samples garbage may fault the GPU instead of painting
            // something wrong (the rule the Vulkan path's white 2D texture
            // follows).
            for ( uint32_t i = 0; i < D3D12Scene::kTextureTableSize; i++ ) {
                GPUTexture *pTexture =
                    i < pCompiled->Material.NumSlots ? pCompiled->Material.pTextures[i] : nullptr;
                pDraw->Srvs[i] = pTexture != nullptr && pTexture->D3D12SrvStorageCpu != 0
                                     ? pTexture->D3D12SrvStorageCpu
                                     : pWhite->D3D12SrvStorageCpu;
            }

            const void *pStreams[D3D12Scene::Mesh::kMaxStreams] = {};
            uint32_t streamBytes[D3D12Scene::Mesh::kMaxStreams] = {};
            for ( uint32_t stream = 0; stream < meshData.NumStreams; stream++ ) {
                pStreams[stream] = meshData.Streams[stream].data();
                streamBytes[stream] = static_cast<uint32_t>( meshData.Streams[stream].size() );
            }
            if ( !sceneRenderer.createMeshStreams( pStreams, streamBytes, meshData.Strides, meshData.NumStreams,
                                                   meshData.Indices.data(),
                                                   static_cast<uint32_t>( meshData.Indices.size() ), pDraw->Mesh ) ) {
                out.NumPrimitivesSkipped++;
                if ( warningBudget-- > 0 ) {
                    OTDU_LOG_WARN( "D3D12 3dgtest: the mesh of node '%s' could not be uploaded\n",
                                   node.Name.c_str() );
                }
                continue;
            }

            out.Programs.insert( pCompiled->pProgram->Name );
            out.Draws.push_back( std::move( pDraw ) );
        }
    }

    OTDU_LOG_INFO( "D3D12 3dgtest: %u node(s), %u primitive(s): %zu draw(s) prepared, %u skipped, %u without a "
                   "material, %u without a program, %u without a vertex layout, %u refused by the device; "
                   "%zu program(s), centre (%.2f %.2f %.2f), radius %.2f\n",
                   out.NumNodes, out.NumPrimitives, out.Draws.size(), out.NumPrimitivesSkipped,
                   out.NumPrimitivesNoMaterial, out.NumPrimitivesNoProgram, out.NumPrimitivesNoLayout,
                   out.NumPrimitivesNoPipeline, out.Programs.size(), out.Center.x, out.Center.y, out.Center.z,
                   out.Radius );
    return !out.Draws.empty();
}

// Loads one bank and turns its scene, geometry and materials into upload-ready
// draws. Returns false (with the reason logged) when the bank cannot draw, so
// the caller can try the next candidate.
bool PrepareModel( const char *pBankPath, TextureCache &textureCache, PreparedModel &out ) {
    Bank bank;
    if ( !bank.load( pBankPath ) ) {
        OTDU_LOG_WARN( "D3D12 3dgtest: no bank '%s'\n", pBankPath );
        return false;
    }
    out.BankPath = bank.getPath();

    // The (3dd, 3dg) pair with the most indexed geometry. A bank can carry
    // several scene files (a spot bank has a shop and its LODs); the biggest
    // one is the model, and its name is the material library's.
    const BankEntry *pSceneEntry = nullptr;
    const BankEntry *pGeometryEntry = nullptr;
    Render3DD sceneDesc;
    Render3DG geometry;
    uint32_t bestIndices = 0;
    for ( const BankEntry *p3dd : bank.findEntriesByExtension( "3dd" ) ) {
        const BankEntry *p3dg = bank.findEntry( p3dd->Name.c_str(), "3dg" );
        if ( p3dg == nullptr ) {
            continue;
        }
        Render3DD candidateScene;
        Render3DG candidateGeometry;
        if ( !candidateScene.parse( p3dd->pData, p3dd->Size, /*bMirrorX=*/false ) ||
             !candidateGeometry.parse( p3dg->pData, p3dg->Size, /*bMirrorX=*/false ) ) {
            continue;
        }
        uint32_t indices = 0;
        for ( const SceneNode &node : candidateScene.getNodes() ) {
            const Geometry *pGeometry = candidateGeometry.find( node.ObjectHashcode );
            if ( pGeometry == nullptr ) {
                continue;
            }
            for ( const GeometryPrimitive &primitive : pGeometry->Primitives ) {
                indices += static_cast<uint32_t>( primitive.Indices.size() );
            }
        }
        if ( indices > bestIndices ) {
            bestIndices = indices;
            pSceneEntry = p3dd;
            pGeometryEntry = p3dg;
            sceneDesc = std::move( candidateScene );
            geometry = std::move( candidateGeometry );
        }
    }
    if ( pSceneEntry == nullptr || pGeometryEntry == nullptr || bestIndices == 0 ) {
        OTDU_LOG_WARN( "D3D12 3dgtest: '%s' has no (3dd, 3dg) pair with indexed geometry (%u entries)\n",
                       out.BankPath.c_str(), bank.getNumEntries() );
        return false;
    }
    out.SceneEntry = pSceneEntry->Filename;
    out.GeometryEntry = pGeometryEntry->Filename;

    // The material library: the scene's own name when the bank has it, else the
    // largest .2dm (spot banks name theirs "<model>_<hash>.2dm").
    const BankEntry *pMaterialEntry = bank.findEntry( pSceneEntry->Name.c_str(), "2dm" );
    if ( pMaterialEntry == nullptr ) {
        size_t largest = 0;
        for ( const BankEntry *p2dm : bank.findEntriesByExtension( "2dm" ) ) {
            if ( p2dm->Size >= largest ) {
                largest = p2dm->Size;
                pMaterialEntry = p2dm;
            }
        }
    }
    Render2DM materials;
    if ( pMaterialEntry != nullptr && !materials.parse( pMaterialEntry->pData, pMaterialEntry->Size ) ) {
        pMaterialEntry = nullptr;
    }
    if ( pMaterialEntry == nullptr ) {
        OTDU_LOG_WARN( "D3D12 3dgtest: '%s' has no readable .2dm\n", out.BankPath.c_str() );
        return false;
    }
    out.MaterialEntry = pMaterialEntry->Filename;
    out.NumMaterials = static_cast<uint32_t>( materials.getMaterials().size() );
    out.Materials = std::move( materials );

    // Every .2db of the bank, uploaded through the Direct3D 12 path and owned by
    // the bank path, which is the owner name the material lookup uses.
    out.NumTexturesUploaded = textureCache.loadBankTextures( bank, out.BankPath.c_str() );
    // A car's shared maps are not in its own bank: TDU2 keeps them in
    // Vehicules/common_car.bnk (wheels, glass, the common interior pieces).
    // Uploaded under their own owner; the material lookup's findForOwner
    // fallback records the borrow, so releasing the model's bank cannot free a
    // texture the model is still drawing with.
    static const char *kSharedBanks[] = { "Euro/Bnk/Vehicules/common_car.bnk",
                                          "Euro/Bnk/Vehicules/colors.bnk" };
    for ( const char *pSharedPath : kSharedBanks ) {
        Bank shared;
        if ( !shared.load( pSharedPath ) ) {
            continue;
        }
        const uint32_t uploaded = textureCache.loadBankTextures( shared, shared.getPath().c_str() );
        if ( uploaded > 0 ) {
            out.NumTexturesUploaded += uploaded;
        }
    }
    OTDU_LOG_INFO( "D3D12 3dgtest: '%s' - %s + %s + %s, %u material(s), %u texture(s) uploaded\n",
                   out.BankPath.c_str(), out.SceneEntry.c_str(), out.GeometryEntry.c_str(),
                   out.MaterialEntry.c_str(), out.NumMaterials, out.NumTexturesUploaded );

    // The model's bounds: every node's geometry box through its own rest-pose
    // transform, so the camera frames the whole model whatever the hierarchy is.
    Vector3f boundsMin( 1e9f, 1e9f, 1e9f );
    Vector3f boundsMax( -1e9f, -1e9f, -1e9f );
    bool bAnyBounds = false;
    for ( const SceneNode &node : sceneDesc.getNodes() ) {
        const Geometry *pGeometry = geometry.find( node.ObjectHashcode );
        if ( pGeometry == nullptr || pGeometry->BoundsMin.x > pGeometry->BoundsMax.x ) {
            continue;
        }
        for ( uint32_t corner = 0; corner < 8; corner++ ) {
            const Vector3f point( ( corner & 1 ) != 0 ? pGeometry->BoundsMax.x : pGeometry->BoundsMin.x,
                                  ( corner & 2 ) != 0 ? pGeometry->BoundsMax.y : pGeometry->BoundsMin.y,
                                  ( corner & 4 ) != 0 ? pGeometry->BoundsMax.z : pGeometry->BoundsMin.z );
            const Vector3f world = node.World.transformPoint( point );
            boundsMin.x = Min( boundsMin.x, world.x );
            boundsMin.y = Min( boundsMin.y, world.y );
            boundsMin.z = Min( boundsMin.z, world.z );
            boundsMax.x = Max( boundsMax.x, world.x );
            boundsMax.y = Max( boundsMax.y, world.y );
            boundsMax.z = Max( boundsMax.z, world.z );
            bAnyBounds = true;
        }
    }
    if ( !bAnyBounds ) {
        OTDU_LOG_WARN( "D3D12 3dgtest: '%s' has no bounds (the scene's nodes name no parsed geometry)\n",
                       out.BankPath.c_str() );
        return false;
    }
    out.Center = Vector3f( ( boundsMin.x + boundsMax.x ) * 0.5f, ( boundsMin.y + boundsMax.y ) * 0.5f,
                           ( boundsMin.z + boundsMax.z ) * 0.5f );
    const Vector3f diagonal = boundsMax - boundsMin;
    out.Radius = Max( 0.01f, std::sqrt( diagonal.dot( diagonal ) ) * 0.5f );
    // Kept for the draw build: the scene / geometry the bounds and the draws both
    // walk.
    out.SceneDesc = std::move( sceneDesc );
    out.Geometry = std::move( geometry );
    return true;
}

// What the frame measured.
struct Measurement {
    uint32_t Total = 0;
    uint32_t Covered = 0;
    uint32_t DistinctColors = 0;
    float Mean[3] = {};
};

// Draws every prepared draw for kProbeFrames frames, reads the last one back and
// measures it. The backdrop is magenta, so any pixel the model painted is
// unambiguous (the same trick -d3d12-edentest uses).
bool DrawAndMeasure( D3D12Device &device, D3D12Scene &sceneRenderer, const PreparedModel &model,
                     Measurement &out ) {
    const ColorRGBA clearColor( 1.0f, 0.0f, 1.0f, 1.0f );
    if ( const char *pShot = CmdLine::GetArgValue( "-renderer-shot" ) ) {
        device.requestScreenshot( pShot );
    }
    uint32_t presented = 0;
    for ( uint32_t frame = 0; frame < kProbeFrames; frame++ ) {
        SDL_Event event;
        while ( SDL_PollEvent( &event ) ) {
            if ( event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ) {
                break;
            }
        }
        if ( !device.beginFrame( clearColor ) ) {
            break;
        }
        for ( const std::unique_ptr<ProbeDraw> &pDraw : model.Draws ) {
            if ( pDraw->pPipeline == nullptr ) {
                continue;
            }
            sceneRenderer.draw( pDraw->pPipeline, pDraw->Mesh, pDraw->Push, sizeof( pDraw->Push ), pDraw->Srvs,
                                D3D12Scene::kTextureTableSize, &pDraw->VSConstants, &pDraw->PSConstants,
                                pDraw->BlendFactor );
        }
        if ( frame + 1 == kProbeFrames ) {
            device.requestReadback();
        }
        device.endFrame();
        presented++;
        if ( device.hasDeviceLost() ) {
            break;
        }
    }
    device.waitIdle();
    const std::vector<uint8_t> &pixels = device.getLastReadback();
    const uint32_t width = device.getBackbufferWidth();
    const uint32_t height = device.getBackbufferHeight();
    out.Total = width * height;
    if ( presented == 0 || pixels.size() < static_cast<size_t>( out.Total ) * 4 ) {
        OTDU_LOG_ERROR( "D3D12 3dgtest: %s (%u frame(s) presented, %zu bytes read back, expected %u)\n",
                        device.hasDeviceLost() ? "the device was lost" : "no frame to measure", presented,
                        pixels.size(), out.Total * 4 );
        return false;
    }

    // Non-black coverage, the mean of the covered pixels and how many distinct
    // colours they hold: a flat silhouette proves the geometry, a wide palette
    // proves the material's textures and lighting reached the shader.
    std::vector<uint8_t> palette( 32768, 0 ); // 5:5:5 RGB
    double sum[3] = {};
    for ( uint32_t y = 0; y < height; y++ ) {
        for ( uint32_t x = 0; x < width; x++ ) {
            const uint8_t *pPixel = &pixels[( static_cast<size_t>( y ) * width + x ) * 4];
            if ( pPixel[0] >= 249 && pPixel[1] <= 6 && pPixel[2] >= 249 ) {
                continue; // the backdrop
            }
            sum[0] += pPixel[0];
            sum[1] += pPixel[1];
            sum[2] += pPixel[2];
            out.Covered++;
            palette[( ( pPixel[0] >> 3 ) << 10 ) | ( ( pPixel[1] >> 3 ) << 5 ) | ( pPixel[2] >> 3 )] = 1;
        }
    }
    for ( uint8_t bUsed : palette ) {
        out.DistinctColors += bUsed;
    }
    for ( int channel = 0; channel < 3; channel++ ) {
        out.Mean[channel] = out.Covered > 0 ? static_cast<float>( sum[channel] / out.Covered ) : 0.0f;
    }

    // A coarse map of the frame in the log: 64 columns by 24 rows, one character
    // per cell - ' ' where the cell is mostly backdrop and four levels of
    // brightness where the model painted it. A probe whose only evidence is a
    // percentage cannot tell a car from a smear; this can, without an image.
    constexpr uint32_t kColumns = 64;
    constexpr uint32_t kRows = 24;
    static const char *kLevels = ".:-=+*#%@"; // 9 levels, 0..8
    std::vector<std::string> mapRows;
    for ( uint32_t row = 0; row < kRows; row++ ) {
        const uint32_t y0 = row * height / kRows;
        const uint32_t y1 = Max( y0 + 1, ( row + 1 ) * height / kRows );
        std::string mapRow;
        for ( uint32_t column = 0; column < kColumns; column++ ) {
            const uint32_t x0 = column * width / kColumns;
            const uint32_t x1 = Max( x0 + 1, ( column + 1 ) * width / kColumns );
            double luma = 0.0;
            uint32_t cells = 0;
            uint32_t covered = 0;
            for ( uint32_t y = y0; y < y1 && y < height; y++ ) {
                for ( uint32_t x = x0; x < x1 && x < width; x++ ) {
                    const uint8_t *pPixel = &pixels[( static_cast<size_t>( y ) * width + x ) * 4];
                    cells++;
                    if ( pPixel[0] >= 249 && pPixel[1] <= 6 && pPixel[2] >= 249 ) {
                        continue;
                    }
                    covered++;
                    luma += 0.299 * pPixel[0] + 0.587 * pPixel[1] + 0.114 * pPixel[2];
                }
            }
            if ( cells == 0 || covered * 2 <= cells ) {
                mapRow += ' ';
            } else {
                const double mean = luma / covered;
                const uint32_t level = Min<uint32_t>( 8, static_cast<uint32_t>( mean / 28.4 ) );
                mapRow += kLevels[level];
            }
        }
        mapRows.push_back( std::move( mapRow ) );
    }
    // One line per row: the log truncates a multi-line message (the whole map was
    // 1.5 KB and arrived as its first eight rows).
    OTDU_LOG_INFO( "D3D12 3dgtest: the frame (%ux%u cells, ' ' backdrop, '.'..'@' brightness):\n", kColumns,
                   kRows );
    for ( const std::string &mapRow : mapRows ) {
        OTDU_LOG_INFO( "D3D12 3dgtest: |%s|\n", mapRow.c_str() );
    }
    return true;
}

} // namespace

// -d3d12-3dgtest: the shader libraries, a bank out of the game data, a hidden
// window and the device, then the model. Runs in the bootstrap
// (tdu_instance.cpp) next to -d3d12-edentest, because it needs Euro/Bnk and
// Euro/Shaders.
bool D3D12RunMeshAssetTest() {
    if ( gpFile == nullptr ) {
        OTDU_LOG_ERROR( "D3D12 3dgtest: the game file system is not up\n" );
        return false;
    }
    // "-d3d12-3dgtest <bank>": a bank path relative to the game root
    // ("Euro/Bnk/Vehicules/High/4x4.bnk"), or a fragment that selects one of the
    // candidates. A bare flag leaves the next token to another switch -
    // CmdLine::GetArgValue does not look at the leading '-'.
    const char *pArgValue = CmdLine::GetArgValue( "-d3d12-3dgtest" );
    const std::string selection =
        pArgValue != nullptr && pArgValue[0] != '-' ? std::string( pArgValue ) : std::string();

    // Small banks that ship with every install and carry the whole chain in one
    // file: a .3dd scene, its .3dg geometry, a .2dm material library and the
    // .2db textures. The generic cars first - their names are the same in every
    // copy of the game.
    static const char *kCandidates[] = {
        "Euro/Bnk/Vehicules/High/4x4.bnk",
        "Euro/Bnk/Vehicules/High/300_c_srt8.bnk",
        "Euro/Bnk/Vehicules/High/2cv.bnk",
    };
    std::vector<std::string> banks;
    const std::string selectionUpper = ToUpper( selection );
    if ( selectionUpper.size() >= 4 && selectionUpper.substr( selectionUpper.size() - 4 ) == ".BNK" ) {
        banks.push_back( selection );
    } else {
        for ( const char *pCandidate : kCandidates ) {
            if ( selection.empty() || ToUpper( pCandidate ).find( selectionUpper ) != std::string::npos ) {
                banks.push_back( pCandidate );
            }
        }
    }
    if ( banks.empty() ) {
        OTDU_LOG_ERROR( "D3D12 3dgtest: no candidate bank matches '%s'\n", selection.c_str() );
        return false;
    }

    EdenShaderSystem shaderSystem;
    if ( !shaderSystem.initialize() ) {
        OTDU_LOG_ERROR( "D3D12 3dgtest: the shader libraries could not be loaded (Euro/Shaders)\n" );
        return false;
    }

    SDL_Window *pWindow = nullptr;
    if ( SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
        pWindow = SDL_CreateWindow( "OpenTDU2 - a .3DG model on Direct3D 12", 1280, 720, SDL_WINDOW_HIDDEN );
    }
    bool bAbort = pWindow == nullptr;
    if ( bAbort ) {
        OTDU_LOG_ERROR( "D3D12 3dgtest: no window: %s\n", SDL_GetError() );
    }

    D3D12Device device;
    if ( !bAbort && !device.create( pWindow, /*bVSync=*/false, CmdLine::HasArg( "-d3d12-debug" ) ) ) {
        OTDU_LOG_ERROR( "D3D12 3dgtest: the device could not be created\n" );
        bAbort = true;
    }

    TextureCache textureCache;
    D3D12Scene sceneRenderer;
    if ( !bAbort ) {
        textureCache.initialize( &device );
        if ( !sceneRenderer.initialize( &device ) ) {
            OTDU_LOG_ERROR( "D3D12 3dgtest: the scene could not be initialised\n" );
            bAbort = true;
        }
    }
    if ( !bAbort ) {
        if ( const char *pDump = CmdLine::GetArgValue( "-d3d12-dump-shaders" ) ) {
            sceneRenderer.setShaderDumpDir( pDump );
        }
    }

    bool bPassed = false;
    for ( const std::string &bankPath : banks ) {
        if ( bAbort || bPassed ) {
            break;
        }
        PreparedModel model;
        if ( !PrepareModel( bankPath.c_str(), textureCache, model ) ) {
            continue;
        }

        // The camera, built the way EdenSceneView / -d3d12-edentest build one:
        // cameraWorld is T(position) * S(-1,1,-1) (the D3D9 handedness), the view
        // its inverse, the projection D3DXMatrixPerspectiveFovRH's. The model's
        // centre is moved to the origin (BuildDraws recentres it), so the camera
        // only has to stand far enough back to frame the radius.
        const float width = static_cast<float>( device.getBackbufferWidth() );
        const float height = Max( 1.0f, static_cast<float>( device.getBackbufferHeight() ) );
        const float aspect = width / height;
        const float fovY = 60.0f * ( 3.14159265358979323846f / 180.0f );
        const float zNear = 0.1f;
        const float distance = model.Radius / std::tan( fovY * 0.5f ) * 1.15f;
        const float zFar = Max( 10.0f, distance + model.Radius * 4.0f );
        const float cotangent = 1.0f / std::tan( fovY * 0.5f );

        EdenSceneParams scene;
        scene.View = ( Matrix4x4f::Translation( Vector3f( 0.0f, 0.0f, -distance ) ) *
                       Matrix4x4f::Scale( Vector3f( -1.0f, 1.0f, -1.0f ) ) ).inverse();
        scene.Projection = Matrix4x4f::Identity();
        scene.Projection.m[0][0] = cotangent / aspect;
        scene.Projection.m[1][1] = cotangent;
        scene.Projection.m[2][2] = zFar / ( zNear - zFar );
        scene.Projection.m[2][3] = -1.0f;
        scene.Projection.m[3][2] = zNear * zFar / ( zNear - zFar );
        scene.Projection.m[3][3] = 0.0f;
        scene.CameraPosition = Vector3f( 0.0f, 0.0f, -distance );
        scene.CameraDirection = Vector3f( 0.0f, 0.0f, 1.0f );
        scene.SunDirection = Vector3f( 0.25f, -0.35f, 0.9f ).normalized();
        scene.Width = device.getBackbufferWidth();
        scene.Height = device.getBackbufferHeight();
        scene.Time = 0.0f;

        if ( !BuildDraws( model.SceneDesc, model.Geometry, model.Materials, model.BankPath.c_str(), scene,
                          shaderSystem, textureCache, sceneRenderer, model ) ) {
            OTDU_LOG_WARN( "D3D12 3dgtest: '%s' produced no drawable primitive\n", model.BankPath.c_str() );
            continue;
        }

        Measurement measured;
        if ( !DrawAndMeasure( device, sceneRenderer, model, measured ) ) {
            continue;
        }
        const float percent = measured.Total > 0 ? 100.0f * static_cast<float>( measured.Covered ) /
                                                       static_cast<float>( measured.Total )
                                                 : 0.0f;
        // The pass needs both halves: enough of the frame covered that a draw
        // happened at all, and a palette wide enough that it is the model with
        // its textures rather than a flat silhouette or the cleared background.
        bPassed = measured.Covered >= Max<uint32_t>( 1, measured.Total / 50 ) && measured.DistinctColors >= 32;
        OTDU_LOG_INFO( "D3D12 3dgtest: %ux%u; %u px covered (%.2f %%), %u distinct colour(s), mean %.1f/%.1f/%.1f "
                       "on '%s' (%zu draw(s), %zu program(s))\n",
                       device.getBackbufferWidth(), device.getBackbufferHeight(), measured.Covered, percent,
                       measured.DistinctColors, measured.Mean[0], measured.Mean[1], measured.Mean[2],
                       device.getDeviceName(), model.Draws.size(), model.Programs.size() );
        OTDU_LOG_INFO( "D3D12 3dgtest: %s (%s)\n", bPassed ? "PASSED" : "FAILED",
                       bPassed ? "a real .3DG model drew on Direct3D 12 with its own material and textures"
                               : "the model is not on the frame, or it drew as a flat silhouette" );
        for ( const std::unique_ptr<ProbeDraw> &pDraw : model.Draws ) {
            sceneRenderer.destroyMesh( pDraw->Mesh );
        }
    }

    sceneRenderer.destroy();
    textureCache.destroy();
    device.destroy();
    if ( pWindow != nullptr ) {
        SDL_DestroyWindow( pWindow );
    }
    if ( SDL_WasInit( SDL_INIT_VIDEO ) ) {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
    }
    shaderSystem.destroy();
    return bPassed;
}

#endif // OTDU_D3D12
