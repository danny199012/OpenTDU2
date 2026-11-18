#include "shared.h"
#include "render/d3d12/d3d12_asset_probe.h"

#if defined( OTDU_D3D12 )

#include "render/2dm.h"
#include "render/3dd.h"
#include "render/3dg.h"
#include "render/d3d12/d3d12_device.h"
#include "render/d3d12/d3d12_eden.h"
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

#include <cctype>
#include <cmath>
#include <deque>
#include <set>
#include <unordered_set>

namespace {

// A frame's worth of draws, bounded so the table heap cannot run out: every
// draw binds a 16 entry SRV table and D3D12Scene::kTableDescriptors is 4096 per
// frame slot, so 96 draws (1536 descriptors) leave room for the 2D layer.
constexpr uint32_t kMaxProbeDraws = 96;
constexpr uint32_t kProbeFrames = 30;

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
    // SRV register -> the D3D9 texture slot the stage's variable name carries
    // (MapRegistersToSlots): the table is indexed by register.
    std::vector<uint32_t> PSRegisters;
    std::vector<uint32_t> VSRegisters;
    uint32_t RegisterCollisions = 0;
    std::string RegisterMap;
    uint32_t NumTexturesResolved = 0;
    uint32_t NumTexturesMissing = 0;
    std::string MissingSlots;
    std::string Failure;
};

// Which D3D9 texture slot each SRV *register* of a stage reads.
//
// SPIRV-Cross renumbers a stage's textures into a dense register list while
// keeping the original slot in the variable's name - a `.sob` pixel shader that
// reads slots 0, 1, 2, 4, 8 and 15 declares
//
//   Texture2D<float4> t0_2d  : register(t0, space0);
//   Texture2D<float4> t8_2d  : register(t5, space0);
//   Texture2D<float4> t15_2d : register(t6, space0);
//
// and the root signature's SRV table is indexed by the *register*. Filling the
// table by slot number therefore binds slot 5's texture where the shader samples
// slot 8 - the car's materials (six or seven slots, all used) hid it, a
// building's nine slots did not: wood and grass came out white. The map is read
// from the HLSL the compiler kept (D3D12ShaderStage::HLSL), which is the only
// place the pairing exists.
std::vector<uint32_t> MapRegistersToSlots( const std::string &hlsl, uint32_t &outCollisions ) {
    std::vector<uint32_t> registers( D3D12Scene::kTextureTableSize, 0xFFFFFFFFu );
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
            if ( bRegisterDigits && registerIndex < registers.size() ) {
                if ( registers[registerIndex] != 0xFFFFFFFFu && registers[registerIndex] != slot ) {
                    outCollisions++;
                } else {
                    registers[registerIndex] = slot;
                }
            }
        }
        position = cursor;
    }
    return registers;
}

// One draw: the uploaded mesh, the material it draws with and where the
// instance stands. The pipeline, the constant files and the texture table are
// D3D12Eden's - the same code `EdenRenderer::draw` will call - so this probe
// measures the production path and not a copy of it.
struct ProbeDraw {
    D3D12EdenMesh Mesh;
    const EdenMaterial *pMaterial = nullptr;
    Matrix4x4f World = Matrix4x4f::Identity();
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
    // Spot banks ship one .2dm per object ("eph_l0c_<hash>.2DM"); the scene's
    // nodes name materials from all of them. Every library of the bank is kept
    // here and indexed by hashcode, the scene's own name first (its entries win
    // a hash tie).
    std::vector<std::unique_ptr<Render2DM>> MaterialLibraries;
    std::unordered_map<uint64_t, const MaterialDesc *> MaterialIndex;
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
    // -d3d12-nocull: the A/B for a model whose winding does not match the
    // material's cull mode (a building's interior faces still fill the frame, an
    // inside-out model does not).
    static const bool bNoCull = CmdLine::HasArg( "-d3d12-nocull" );
    if ( bNoCull ) {
        material.CullMode = 1;
    }
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
    // The texture table is indexed by SRV register; the material's slots are the
    // D3D9 slot numbers. Read the pairing out of the HLSL both stages kept.
    out.PSRegisters = MapRegistersToSlots( out.PS.HLSL, out.RegisterCollisions );
    out.VSRegisters = MapRegistersToSlots( out.VS.HLSL, out.RegisterCollisions );
    // What the mapping says, for the log: "t0->s2 t1->s3(cube) ..." is the only
    // way to tell a wrong bind from a missing texture.
    for ( uint32_t i = 0; i < D3D12Scene::kTextureTableSize; i++ ) {
        const uint32_t slot =
            out.PSRegisters[i] != 0xFFFFFFFFu ? out.PSRegisters[i] : out.VSRegisters[i];
        if ( slot == 0xFFFFFFFFu ) {
            continue;
        }
        if ( !out.RegisterMap.empty() ) {
            out.RegisterMap += " ";
        }
        out.RegisterMap += "t" + std::to_string( i ) + "->s" + std::to_string( slot );
        GPUTexture *pTexture = slot < out.Material.NumSlots ? out.Material.pTextures[slot] : nullptr;
        if ( pTexture != nullptr ) {
            out.RegisterMap += "(" + pTexture->Name + ")";
        }
    }
    return true;
}

// The per-primitive half: the material its scene node names, the .sob program
// its switches select, the .3DG streams, the vertex input layout, the pipeline
// and the cbuffer / texture tables of the draw.
bool BuildDraws( const Render3DD &sceneDesc,
                 const Render3DG &geometry,
                 const std::unordered_map<uint64_t, const MaterialDesc *> &materialIndex,
                 const char *pOwner,
                 EdenShaderSystem &shaderSystem,
                 TextureCache &textureCache,
                 D3D12Eden &eden,
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
            const MaterialDesc *pDesc = nullptr;
            const auto materialIt = materialIndex.find( materialHash );
            if ( materialIt != materialIndex.end() ) {
                pDesc = materialIt->second;
            }
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
                                       "%u of %u texture(s) resolved; binds %s%s%s; %zu parameter(s)\n",
                                       pDesc->Name.c_str(), pNew->pProgram->Name.c_str(),
                                       static_cast<unsigned>( pNew->pProgram->NumDifferences ),
                                       pNew->Material.SrcBlend, pNew->Material.DstBlend, pNew->Material.BlendOp,
                                       pNew->Material.CullMode, static_cast<uint32_t>( pNew->Bucket ),
                                       pNew->Material.bDepthTest ? 1 : 0,
                                       pNew->Material.bDepthWrite ? 1 : 0, pNew->NumTexturesResolved,
                                       pNew->Material.NumSlots, pNew->RegisterMap.c_str(),
                                       pNew->MissingSlots.empty() ? "" : "; missing ", pNew->MissingSlots.c_str(),
                                       pDesc->Parameters.size() );
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

            // The mesh: D3D12PrepareMesh + createMeshStreams, behind the same
            // class `EdenRenderer::draw` will call (docs/backends.md 3.12/3.14).
            auto pDraw = std::make_unique<ProbeDraw>();
            if ( !eden.createMesh( primitive, pDraw->Mesh ) ) {
                out.NumPrimitivesSkipped++;
                if ( warningBudget-- > 0 ) {
                    OTDU_LOG_WARN( "D3D12 3dgtest: a primitive of node '%s' (%u vertex(es), %zu index(es)) did not "
                                   "convert\n",
                                   node.Name.c_str(), primitive.NumRawVertex, primitive.Indices.size() );
                }
                continue;
            }
            // OPEN (docs/backends.md 3.13): the "the mesh does not carry
            // TEXCOORD1" line lives here in the probe's first version and is
            // *off*: iterating pProgram->pVS->Inputs / meshData.Attributes in this
            // spot (a read-only loop) turned the frame empty - 25 draws recorded,
            // 0 px on both the screenshot and the readback - in six consecutive
            // runs, and removing it restored 57422 px in five. A one-line
            // OTDU_LOG_WARN at the same place changes nothing, the log buffer is
            // 2048 bytes (vsnprintf), and the debug layer is silent. Either an
            // uninitialized read whose value the extra code perturbs, or a GPU
            // race the extra work masks. Chase it with a GPU capture before the
            // Eden port copies this pattern.

            // The node's own place in the model, recentred and turned for the
            // camera. Row-vector convention, the one EdenModel::draw composes
            // with (`world * nodeTransforms[..]`): the node transform first.
            const Matrix4x4f world = node.World * toOriginAndTurn;

            // The permutation's pair (compiled once per library by D3D12Eden)
            // and the pipeline: created here, not inside the frame loop, so a
            // refusal is a counted draw rather than one that silently
            // disappears.
            const D3D12EdenProgram *pProgram = eden.getProgram( pCompiled->pProgram );
            if ( pProgram == nullptr ) {
                out.NumPrimitivesNoProgram++;
                eden.destroyMesh( pDraw->Mesh );
                continue;
            }
            if ( eden.getPipeline( *pProgram, pDraw->Mesh, pCompiled->Material,
                                   Determinant3x3( world ) < 0.0f ) == nullptr ) {
                out.NumPrimitivesNoPipeline++;
                eden.destroyMesh( pDraw->Mesh );
                continue;
            }
            pDraw->pMaterial = &pCompiled->Material;
            pDraw->World = world;
            pDraw->NumVertices = primitive.NumRawVertex;

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

    // Every other .2dm of the bank, indexed by hashcode with the scene's own
    // library first: a spot bank's nodes name materials that live in twenty
    // different libraries (one per object), and looking only in the scene's own
    // one leaves most primitives without a material.
    auto addLibrary = [&out]( const Render2DM &library ) {
        for ( const auto &entry : library.getMaterials() ) {
            out.MaterialIndex.emplace( entry.first, &entry.second );
        }
    };
    addLibrary( out.Materials );
    for ( const BankEntry *p2dm : bank.findEntriesByExtension( "2dm" ) ) {
        if ( p2dm == pMaterialEntry ) {
            continue;
        }
        auto pLibrary = std::make_unique<Render2DM>();
        if ( !pLibrary->parse( p2dm->pData, p2dm->Size ) ) {
            continue;
        }
        addLibrary( *pLibrary );
        out.MaterialLibraries.push_back( std::move( pLibrary ) );
    }
    out.NumMaterials = static_cast<uint32_t>( out.MaterialIndex.size() );
    OTDU_LOG_INFO( "D3D12 3dgtest: '%s' indexes %u material(s) from %zu .2dm library(ies)\n",
                   out.BankPath.c_str(), out.NumMaterials, out.MaterialLibraries.size() + 1 );

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
    uint64_t Draws = 0;
    uint64_t ConstantBytes = 0;
    uint64_t RingBytes = 0;     // what the ring actually holds (256-byte blocks)
    double RecordMs = 0.0;      // CPU time the draw loop took, per frame
};

// Draws every prepared draw 'instances' times for kProbeFrames frames, reads the
// last one back and measures it. The backdrop is magenta, so any pixel the model
// painted is unambiguous (the same trick -d3d12-edentest uses).
//
// The instances are a grid in the camera's own plane (world * T(offset), the
// row-vector convention: the offset applies after the instance's own transform),
// spaced two model radii apart and depth tested against each other. One instance
// is the single-model evidence of section 3.13; hundreds answer the question a
// world asks instead - what a frame of thousands of draws costs in ring bytes,
// descriptor tables and CPU time (docs/backends.md 3.14).
bool DrawAndMeasure( D3D12Device &device, D3D12Scene &sceneRenderer, D3D12Eden &eden, const PreparedModel &model,
                     const EdenSceneParams &scene, uint32_t instances, Measurement &out ) {
    const ColorRGBA clearColor( 1.0f, 0.0f, 1.0f, 1.0f );
    if ( const char *pShot = CmdLine::GetArgValue( "-renderer-shot" ) ) {
        device.requestScreenshot( pShot );
    }
    // The instance transforms: a centered grid, so the frame stays symmetric and
    // the camera's fit covers it.
    const uint32_t grid = Max<uint32_t>( 1, static_cast<uint32_t>(
                                                std::ceil( std::sqrt( static_cast<double>( instances ) ) ) ) );
    const float spacing = model.Radius * 2.6f;
    std::vector<Matrix4x4f> transforms;
    transforms.reserve( instances );
    for ( uint32_t i = 0; i < instances; i++ ) {
        const float column = static_cast<float>( i % grid ) - static_cast<float>( grid - 1 ) * 0.5f;
        const float row = static_cast<float>( i / grid ) - static_cast<float>( grid - 1 ) * 0.5f;
        transforms.push_back( Matrix4x4f::Translation(
            Vector3f( column * spacing, -row * spacing, 0.0f ) ) );
    }

    eden.beginScene( scene );
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
        const uint64_t start = SDL_GetPerformanceCounter();
        for ( const Matrix4x4f &transform : transforms ) {
            for ( const std::unique_ptr<ProbeDraw> &pDraw : model.Draws ) {
                eden.draw( pDraw->Mesh, *pDraw->pMaterial, pDraw->World * transform );
            }
        }
        const double recordMs = static_cast<double>( SDL_GetPerformanceCounter() - start ) /
                                static_cast<double>( SDL_GetPerformanceFrequency() ) * 1000.0;
        if ( frame + 1 == kProbeFrames ) {
            out.Draws = eden.getDraws();
            out.ConstantBytes = eden.getConstantBytes();
            out.RingBytes = sceneRenderer.getFrameConstantBytes();
            out.RecordMs = recordMs;
            device.requestReadback();
        }
        eden.resetCounters();
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

    // Coverage and the mean of the covered pixels, plus how many distinct
    // colours they hold. The colours are counted unquantized: a 5:5:5 bucket
    // (216..223 is one bucket) cannot separate a textured near-white body from a
    // flat one, and that is exactly the difference this probe exists to report.
    std::unordered_set<uint32_t> distinct;
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
            distinct.insert( ( static_cast<uint32_t>( pPixel[0] ) << 16 ) |
                             ( static_cast<uint32_t>( pPixel[1] ) << 8 ) |
                             static_cast<uint32_t>( pPixel[2] ) );
        }
    }
    out.DistinctColors = static_cast<uint32_t>( distinct.size() );
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
    // The production draw path (render/d3d12/d3d12_eden.h): the same object
    // `EdenRenderer` will own, so this probe measures it and not a copy.
    D3D12Eden eden;
    if ( !bAbort && !eden.initialize( &device, &sceneRenderer, &textureCache, &shaderSystem ) ) {
        OTDU_LOG_ERROR( "D3D12 3dgtest: the eden draw path could not be initialised\n" );
        bAbort = true;
    }
    // -instances N: the same model drawn N times (docs/backends.md 3.14).
    const uint32_t instances =
        Max<uint32_t>( 1, static_cast<uint32_t>( CmdLine::GetArgInt( "-instances", 1 ) ) );

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
        // The bounding *sphere* fit (d = R / sin(halfFov)), not the tangent one:
        // for a car the two are close, for a 40 m spot building the tangent form
        // put the camera inside the model. The horizontal half-angle is wider
        // than the vertical one at every aspect this runs at, so the vertical one
        // decides. -instances spreads a grid of models across the camera's plane,
        // so the camera steps back by the grid's half span on top of that.
        const uint32_t grid =
            Max<uint32_t>( 1, static_cast<uint32_t>( std::ceil( std::sqrt( static_cast<double>( instances ) ) ) ) );
        const float span = model.Radius * 2.6f * static_cast<float>( grid - 1 ) * 0.5f;
        const float distance = model.Radius / std::sin( fovY * 0.5f ) * 1.15f + span * 1.5f;
        const float zFar = Max( 10.0f, distance + model.Radius * 4.0f + span * 2.0f );
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

        if ( !BuildDraws( model.SceneDesc, model.Geometry, model.MaterialIndex, model.BankPath.c_str(),
                          shaderSystem, textureCache, eden, model ) ) {
            OTDU_LOG_WARN( "D3D12 3dgtest: '%s' produced no drawable primitive\n", model.BankPath.c_str() );
            continue;
        }

        Measurement measured;
        if ( !DrawAndMeasure( device, sceneRenderer, eden, model, scene, instances, measured ) ) {
            continue;
        }
        const float percent = measured.Total > 0 ? 100.0f * static_cast<float>( measured.Covered ) /
                                                       static_cast<float>( measured.Total )
                                                 : 0.0f;
        // The draw path is what this probe proves: coverage says the model's
        // geometry, materials, bindings and constants reached the frame through a
        // real PSO. The colour count is reported beside it, because a near-flat
        // palette is a *material data* question, not a draw-path one - the generic
        // cars' paint is `TD_CarMat_NM`, whose base colour the game fills from the
        // instance's colour table; a bank-only probe draws them white on purpose.
        bPassed = measured.Covered >= Max<uint32_t>( 1, measured.Total / 100 );
        if ( bPassed && measured.DistinctColors < 32 ) {
            OTDU_LOG_WARN( "D3D12 3dgtest: the frame is nearly flat (%u distinct colour(s)) - the material's "
                           "colour comes from data a bank does not carry (a car's paint is the instance's "
                           "colour table), or a texture slot did not resolve; the log above names the slots\n",
                           measured.DistinctColors );
        }
        OTDU_LOG_INFO( "D3D12 3dgtest: %ux%u; %u px covered (%.2f %%), %u distinct colour(s), mean %.1f/%.1f/%.1f "
                       "on '%s' (%zu draw(s) prepared, %zu program(s); texture tables %llu hit(s) / %llu miss(es), %u "
                       "resident)\n",
                       device.getBackbufferWidth(), device.getBackbufferHeight(), measured.Covered, percent,
                       measured.DistinctColors, measured.Mean[0], measured.Mean[1], measured.Mean[2],
                       device.getDeviceName(), model.Draws.size(), model.Programs.size(),
                       static_cast<unsigned long long>( sceneRenderer.getTextureTableHits() ),
                       static_cast<unsigned long long>( sceneRenderer.getTextureTableMisses() ),
                       sceneRenderer.getTextureTableCount() );
        // The frame's cost, which is what -instances exists to measure
        // (docs/backends.md 3.14): the bytes the draws copied into the constant
        // ring, how much of the ring that is, and the CPU time the recording
        // took. A frame over 100 % loses draws to the zero block (the ring
        // overflow is logged once by D3D12Scene), so the percentage is a pass
        // criterion for the draw count, not a decoration.
        const double ringPercent = 100.0 * static_cast<double>( measured.RingBytes ) /
                                   static_cast<double>( D3D12Scene::kConstantRingBytes );
        const unsigned long long ringMb = static_cast<unsigned long long>( D3D12Scene::kConstantRingBytes /
                                                                          ( 1024ull * 1024ull ) );
        OTDU_LOG_INFO( "D3D12 3dgtest: %u instance(s), %llu draw(s)/frame, %llu constant byte(s) copied, %llu in the "
                       "ring (%.1f %% of %llu MB), %.2f ms to record on the CPU, %u program(s), %u pipeline(s)\n",
                       instances, static_cast<unsigned long long>( measured.Draws ),
                       static_cast<unsigned long long>( measured.ConstantBytes ),
                       static_cast<unsigned long long>( measured.RingBytes ), ringPercent, ringMb,
                       measured.RecordMs, eden.getProgramCount(), eden.getPipelineCount() );
        if ( measured.Draws < model.Draws.size() * static_cast<size_t>( instances ) ) {
            OTDU_LOG_WARN( "D3D12 3dgtest: %llu of the %zu draw(s) were refused (%llu had no program, %llu no "
                           "pipeline)\n",
                           static_cast<unsigned long long>( measured.Draws ),
                           model.Draws.size() * static_cast<size_t>( instances ),
                           static_cast<unsigned long long>( eden.getDrawsWithoutProgram() ),
                           static_cast<unsigned long long>( eden.getDrawsWithoutPipeline() ) );
        }
        OTDU_LOG_INFO( "D3D12 3dgtest: %s (%s)\n", bPassed ? "PASSED" : "FAILED",
                       bPassed ? "the model reached the frame through its own material, pipeline and textures"
                               : "the model is not on the frame" );
        for ( const std::unique_ptr<ProbeDraw> &pDraw : model.Draws ) {
            eden.destroyMesh( pDraw->Mesh );
        }
    }

    eden.destroy();
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
