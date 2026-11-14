#pragma once
#include <functional>

#include <memory>

#include "render/2dm.h"
#include "render/3dg.h"
#include "render/eden/eden_occlusion.h"
#include "render/eden/eden_shader_system.h"
#include "render/eden/eden_uv_animation.h"
#include "render/vulkan/render_device.h"

class TextureCache;

// Renderer driving the original TDU2 shaders (.sob, translated to SPIR-V
// through DXVK's DXSO front-end) with the original data, in the original
// left handed D3D space: positions, matrices and constants are the ones the
// game feeds to Direct3D 9. The viewport is flipped so the D3D clip space
// convention (y up) is kept.
//
// Shader constants are bound by name, from the constant tables the HLSL
// compiler left in the bytecode: Var_Frame_VS (c0), Var_Scene_VS/PS,
// Var_VS (c60), Var_Skin_VS (c148), Var_PS... Engine parameters use the
// "__Name" convention (__World_View_Proj__float4x4, __DirLight_Dir_0...),
// the other members are material parameters (.2DM parameter list, keyed by
// the Eden identifier of the upper case name: "sectrOff" -> SECTROFF).
// Material texture slot i is bound to sampler s<i>.

// Raw vertex/index data uploaded once (one buffer per model).
struct EdenMesh {
    static constexpr uint32_t kMaxStreams = 3;
    GPUBuffer *pStreamBuffers[kMaxStreams] = {}; // Shared, not owned
    GPUBuffer *pIndexBuffer = nullptr;           // Shared, not owned
    VkDeviceSize StreamOffsets[kMaxStreams] = {};
    uint32_t Strides[kMaxStreams] = {};
    uint32_t NumStreams = 0;
    std::vector<RawVertexAttribute> Attributes;
    uint32_t FirstIndex = 0;
    uint32_t IndexCount = 0;
    int32_t VertexOffset = 0;
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    uint64_t LayoutKey = 0;
    // Ray tracing: device address of the bottom level acceleration structure
    // of this mesh (0: none — procedural meshes, and the terrain heightfield
    // whose positions are reconstructed in the shader). Set at model load.
    uint64_t BlasAddress = 0;

    void computeLayoutKey();
};

struct EdenMaterial {
    std::string Name;
    uint64_t Hashcode = 0;
    uint64_t ShaderSetGUID = 0;
    uint8_t Spec[EdenShaderSystem::kNumSpecs] = {};
    uint8_t States[16] = {}; // Raw .2DM render states (MAT. +0x30)
    int32_t Bucket = 0x39;   // Render bucket (ApplyEdenMaterialHandler)
    const EdenProgram *pProgram = nullptr;
    GPUTexture *pTextures[16] = {};
    uint8_t SamplerIndex[16] = {};
    bool bNormalSlot[16] = {};
    uint64_t TextureHashes[16] = {};
    uint64_t SlotTypes[16] = {};
    uint32_t NumSlots = 0;
    // A stamp of the CURRENT slot hashes for the draw-set cache (the runtime
    // swaps slot content; see DrawSetKey::textures). Cheap: 16 words.
    uint64_t textureStamp() const {
        uint64_t h = 0xcbf29ce484222325ull;
        for ( uint32_t i = 0; i < 16; i++ ) {
            h ^= TextureHashes[i];
            h *= 0x100000001b3ull;
        }
        return h;
    }
    uint8_t HeightfieldGrid = 0; // TD_HeightMap: 0 sector (3 m), 1 area (24 m)
    std::unordered_map<uint64_t, Vector4f> Parameters;
    // Texture coordinate animations by slot (__UVA_k; null: identity).
    std::shared_ptr<const EdenUVAnimation> UVAnimations[4];
    // GSReflection projective map slot (TD_Water U4: 4, TD_Furnitures U7:
    // 6), -1: none. Bound to the frame projective texture (render target of
    // the REFLECT layers) when the renderer has one.
    int8_t ProjectiveSlot = -1;
    // Runtime constants by shader constant name, per stage (0 VS, 1 PS).
    std::unordered_map<std::string, std::vector<Vector4f>> StageConstants[2];
    Vector4f Diffuse = Vector4f( 1, 1, 1, 1 );
    Vector4f Ambient = Vector4f( 1, 1, 1, 1 );
    Vector4f Specular = Vector4f( 0, 0, 0, 1 );
    Vector4f Emissive = Vector4f( 0, 0, 0, 1 );
    uint8_t SrcBlend = 2;
    uint8_t DstBlend = 1;
    uint8_t CullMode = 3; // D3DCULL
    uint8_t AlphaRef = 0;
    uint8_t DepthFunc = 0; // D3DCMPFUNC, 0: LESSEQUAL
    uint8_t BlendOp = 1;   // D3DBLENDOP (1 add, 2 subtract, 3 revsubtract, 4 min, 5 max)
    uint8_t ColorWriteMask = 0xf; // D3DCOLORWRITEENABLE bits (R 1, G 2, B 4, A 8)
    // D3D9 D3DBLENDFACTOR (blend states 14 / 15): the constant alpha a
    // material blends with. The original TD_Ocean pixel shader always writes
    // alpha 1, so SRC_ALPHA cannot make the sea see-through; the water clarity
    // option (EdenOcean::setupMaterial) feeds this instead.
    float BlendFactor = 1.0f;
    bool bAlphaTest = false;
    bool bDepthTest = true;
    bool bDepthWrite = true;

    bool isBlended() const { return !( SrcBlend == 2 && DstBlend == 1 ); }
    // Quantized BlendFactor, the part of it the pipeline state (and its cache
    // key) depends on.
    uint8_t blendFactorByte() const {
        const float clamped = BlendFactor < 0.0f ? 0.0f : ( BlendFactor > 1.0f ? 1.0f : BlendFactor );
        return static_cast<uint8_t>( clamped * 255.0f + 0.5f );
    }
};

// Per-frame / per-scene values (the frame & scene parameter structs).
struct EdenSceneParams {
    Matrix4x4f View;       // D3D row-vector convention (Matrix4x4f memory = D3D layout)
    Matrix4x4f Projection; // D3D LH, z in [0, 1]
    Vector3f CameraPosition;
    Vector3f CameraDirection;
    float Time = 0.0f;
    Vector3f SunDirection = Vector3f( 0.3f, -0.8f, 0.4f ); // direction the light travels
    Vector3f SunDiffuse = Vector3f( 1.0f, 0.95f, 0.85f );
    Vector3f SunSpecular = Vector3f( 1.0f, 1.0f, 1.0f );
    Vector3f Ambient = Vector3f( 0.45f, 0.5f, 0.6f );
    Vector3f FogColor = Vector3f( 0.7f, 0.75f, 0.8f );
    Vector4f FogConfig = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    Vector4f UserVectors[8] = {};
    uint32_t Width = 1280;
    uint32_t Height = 720;
    // Mirroring view (environment cube faces): the culling flips.
    bool bMirroredView = false;
    // Scene wide named constants (sky scattering, shared by td_dome, the
    // clouds and the heightmap aerial perspective).
    std::unordered_map<std::string, Vector4f> Vectors;
    std::unordered_map<std::string, Matrix4x4f> Matrices;
};

// Optional per-draw overrides (constants by name).
// Avatar light of a character (FUN_00b87aa0 / FUN_00a4ca50 / FUN_00caf850,
// docs/re/avatar_lighting_spec.md), world space.
struct EdenAvatarLighting {
    Vector3f Direction = Vector3f( 0.0f, -1.0f, 0.0f ); // direction the light travels
    Vector3f Ambient, Diffuse, Specular;
    Vector3f SpotPosition;                              // camera light
    Vector3f SpotAmbient, SpotDiffuse, SpotSpecular;
    float Shadow = 1.0f;                                // CARINTEG.z
    float Environment = 0.7f;                           // ATTEFACT.w
};

struct EdenDrawParams {
    std::unordered_map<std::string, Vector4f> Vectors;
    std::unordered_map<std::string, Matrix4x4f> Matrices;
    std::unordered_map<std::string, std::vector<Vector4f>> Arrays; // "balance__array3"...
    std::unordered_map<std::string, Vector4f> VertexVectors;       // stage specific
    std::unordered_map<std::string, Vector4f> PixelVectors;
    // Per material instance parameter overrides (the neon AnimValue of the
    // .pmi instances: one blink state per (material, model instance), so a
    // shared material can carry a different phase per instance draw).
    struct MaterialVector {
        const void *pMaterial = nullptr;
        uint64_t Parameter = 0;
        Vector4f Value;
    };
    std::vector<MaterialVector> MaterialVectors;
    const Matrix4x4f *pSkinMatrices = nullptr;
    uint32_t NumSkinMatrices = 0;
    const EdenAvatarLighting *pAvatarLighting = nullptr;
    // Nodes EdenModel::draw skips for this draw, beyond the model's own
    // visibility (a shared model drawn per agent: the per agent lamp
    // sprites). By value: blended draws copy the params when deferred.
    std::vector<int32_t> ForceHiddenNodes;
    // Items (model-local, by item index) EdenModel::draw skips - the animated
    // wipers hide the parked wiper prim of the body this way (session 65).
    std::vector<int32_t> ForceHiddenItems;
    // Per-draw texture substitutions: a material slot of the given shader set
    // with sampler heap index Sampler (the .pmi instance list override
    // record list +0x7670 of FUN_00987e80 / FUN_00982af0: TD_Ground SHADOW =
    // the sector -s map, D3DTADDRESS 2 / 2 [CODE], night_lighting_spec 2.1).
    struct TextureOverride {
        uint64_t ShaderSetGUID = 0;
        uint64_t SlotType = 0;
        GPUTexture *pTexture = nullptr;
        uint16_t Sampler = 0;
    };
    std::vector<TextureOverride> TextureOverrides;
    // Blended draws with parameters are recorded immediately unless set
    // (then deferred and sorted with the other blended draws; the skin
    // matrices are copied).
    bool bDeferBlended = false;
    std::vector<Matrix4x4f> SkinStorage;
};

class EdenRenderer : public RenderDevice::PendingRecordHook {
public:
    EdenRenderer();
    ~EdenRenderer();

    bool initialize( RenderDevice *pDevice, TextureCache *pTextureCache );
    void refreshSamplers();
    void destroy();
    inline bool isReady() const { return bReady; }
    inline EdenShaderSystem &getShaderSystem() { return shaderSystem; }

    // Street lamp glow (TD_Lamp heads): the retail lamp system switches them at
    // dusk, the port has none, so EdenSceneView::drawObjects feeds the weather's
    // day / night hour here (eden_renderer.cpp draw()). Default true: a scene
    // that never sets it (the asset viewer, tests) keeps drawing the lamps.
    bool bNightLights = true;

    // Builds the runtime material (shader permutation, textures) of a .2DM entry.
    void setupMaterial( const MaterialDesc &desc, EdenMaterial &outMaterial, const char *pTextureOwner = nullptr );
    // Runtime texture assignment (e.g. sector shadow maps), call updateProgram after.
    bool assignTexture( EdenMaterial &material, const char *pSlotType, GPUTexture *pTexture );
    void updateProgram( EdenMaterial &material );
    // Blend / depth / cull / alpha test from EdenMaterial::States.
    static void applyRenderStates( EdenMaterial &material );

    void beginScene( const EdenSceneParams &params );
    void draw( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
               const EdenDrawParams *pParams = nullptr );
    // Blended draws are deferred and sorted back to front.
    // Re-applies the scene viewport (after switching render passes; the
    // deferred blended draws are kept).
    void resumeScene();
    void endScene();
    // Records the deferred blended draws now (back to front).
    void flushDeferred();
    // Records the kept opaque water draws now (also done by flushDeferred
    // and endScene).
    void flushWater();
    // Screen space buffer of the frame ("gViewPos": R sun visibility, B/A
    // view depth), bound to s15 (gTexSamplerShadowRealTime) and to the
    // runtime slot of TD_ShadowBox. nullptr: 1x1 default (lit, no depth).
    inline void setScreenTexture( GPUTexture *pTexture ) { pScreenTexture = pTexture; }
    // gTexSamplerShadowRealTime (s15) of the scene shaders: R sun
    // visibility, G / B additional light, A light multiplier. nullptr: lit.
    inline void setShadowRealTimeTexture( GPUTexture *pTexture ) { pShadowRealTimeOverride = pTexture; }
    // GSReflection render target (REFLECT 2D / actor layers), bound to the
    // EdenMaterial::ProjectiveSlot of the registered materials. nullptr:
    // transparent black.
    inline void setProjectiveTexture( GPUTexture *pTexture ) { pProjectiveTexture = pTexture; }
    // Depth only pass (Z pre-pass): color writes off, blended draws skipped.
    inline void setDepthOnly( bool bEnable ) { bDepthOnly = bEnable; }
    // While set, every draw() goes to the hook instead of the material
    // pipelines (the per-object velocity pass, EdenSceneView::runObjectVelocity).
    using DrawHook =
        std::function<void( const EdenMesh &, const EdenMaterial &, const Matrix4x4f &, const EdenDrawParams * )>;
    inline void setDrawHook( DrawHook hook ) { drawHook = std::move( hook ); }
    // Shadow caster pass: opaque draws use their caster permutation
    // (S_PASS_SHADOWCAST of the vegetation families, TD_ShadowCaster for
    // the others: R32F = clip z * 2 - 1); blended draws are skipped.
    inline void setShadowCasterMode( bool bEnable ) { bShadowCaster = bEnable; }
    // Ocean reflection pass (SEAREF_N): no blended draws, no water.
    inline void setReflectionPass( bool bEnable ) { bReflectionPass = bEnable; }
    inline bool isShadowCasterMode() const { return bShadowCaster; }
    inline bool isReflectionPass() const { return bReflectionPass; }
    void clearShadowCasterCache() {
        casterMaterials.clear();
        zPassMaterials.clear();
    }

    uint32_t getNumDrawCalls() const { return numDrawCalls; }
    // -logrecordtimes: running totals of the time spent in recordDraw (the
    // Vulkan recording: constants, descriptor set, binds) and of the draws it
    // recorded, so the scene timer can split each pass into traversal vs
    // recording (docs/PERF_PLAN.md 0a).
    double getRecordMsTotal() const { return recordMsTotal; }
    uint64_t getRecordCountTotal() const { return recordCountTotal; }
    inline const Matrix4x4f &getViewProjection() const { return viewProjection; }
    // Shadow casters: the light view-projection in double; while set, the
    // world-view-projection constant is formed from it in double (the float
    // light matrix at island coordinates re-rounds the texel grid by a
    // fraction of a texel from one camera pose to the next).
    void setPreciseViewProjection( const Matrix4x4d *pMatrix ) { pPreciseViewProjection = pMatrix; }
    // Frustum test of a node-local box drawn with `world` (bounding sphere
    // against the side and far planes of the current scene; the near plane is
    // not tested so shadow casters behind the light slice still cast).
    // `margin` (m) widens the sphere (skinned meshes: bind pose bounds).
    bool isBoxVisible( const Matrix4x4f &world, const Vector3f &boundsMin, const Vector3f &boundsMax,
                       float margin = 0.0f ) const;
    // Frustum test of a world-space sphere (same side / far planes as
    // isBoxVisible). The intro culls its avatars and cars per pass with it:
    // every pass used to submit all 31 PNJs.
    bool isSphereVisible( const Vector3f &center, float radius ) const;
    inline const EdenSceneParams &getSceneParams() const { return scene; }

    // Occlusion probe (-occlusionprobe, a measurement, not a cull): while
    // recording, every draw stores the world box of the object it belongs to
    // (the box of the last isBoxVisible that passed, or setProbeBox from the
    // instance loop; none for the terrain and the batches). EdenSceneView
    // tests the boxes of the depth pre-pass against its depth buffer.
    struct ProbeDraw {
        Vector3f Min;
        Vector3f Max;
        uint8_t Kind = 0; // 0 no box, 1 model item, 2 .pmi instance
    };
    void beginOcclusionProbe() {
        probeDraws.clear();
        bProbeRecording = true;
        probeKind = 0;
    }
    std::vector<ProbeDraw> endOcclusionProbe() {
        bProbeRecording = false;
        return std::move( probeDraws );
    }
    inline bool isProbeRecording() const { return bProbeRecording; }
    // Occlusion culling (EdenOcclusion): while a grid is set, isBoxVisible and
    // the instance loop also drop the boxes hidden in it. Set only around the
    // main view's pre-pass / opaque traversal (never shadows / reflections).
    void setOcclusionGrid( const EdenOcclusion::Grid *pGrid ) { pOcclusionGrid = pGrid; }
    bool isOccluded( const Vector3f &boxMin, const Vector3f &boxMax ) const;
    // Running totals (tested, culled), reset by the caller.
    void takeOcclusionCounts( uint32_t &tested, uint32_t &culled ) {
        tested = occlusionTested;
        culled = occlusionCulled;
        occlusionTested = occlusionCulled = 0;
    }
    // World-space box of the next draws (cleared by each draw).
    void setProbeBox( const Vector3f &boxMin, const Vector3f &boxMax, uint8_t kind ) const {
        probeMin = boxMin;
        probeMax = boxMax;
        probeKind = kind;
    }

    // Ray tracing: the instances of the main scene recorded this frame, which
    // the top level structure is built from (off by default, -rtworld). The
    // shadow caster, reflection and depth only passes draw the same meshes
    // again and are not captured. Cleared by beginScene.
    struct RtInstance {
        uint64_t BlasAddress = 0;
        Matrix4x4f World;
    };
    inline void setRtInstanceCapture( bool bEnable ) { bRtCapture = bEnable; }
    inline const std::vector<RtInstance> &getRtInstances() const { return rtInstances; }
    // Camera of the scene the instances came from: a frame draws several scenes
    // (environment cube, shadow casters, reflections, post FX) and each has its
    // own camera, so the last beginScene is not the one to use.
    inline const Vector3f &getRtCameraPosition() const { return rtCameraPosition; }
    inline const Vector3f &getRtCameraDirection() const { return rtCameraDirection; }
    // Once per frame, before the scene is drawn: a frame draws many scenes
    // (environment cube, shadow casters, reflections, the main pass), so the
    // list must not be cleared by beginScene.
    inline void clearRtInstances() { rtInstances.clear(); }

    // Parallel command recording (docs/PERF_PLAN.md 0a): inside an offscreen
    // render target the resolved draws are queued and recorded into secondary
    // command buffers by worker threads when anything else needs the frame's
    // command buffer. -noparallelrecord = the A/B.
    void flushPendingRecords() override;

private:
    struct DeferredDraw {
        EdenMesh Mesh;
        const EdenMaterial *pMaterial;
        Matrix4x4f World;
        float Distance;
        std::shared_ptr<EdenDrawParams> pParams;
    };

    RenderDevice *pDevice;
    TextureCache *pTextureCache;
    EdenShaderSystem shaderSystem;
    VkDescriptorSetLayout setLayouts[2];
    VkPipelineLayout pipelineLayout;
    VkDescriptorPool staticPool;
    VkDescriptorPool framePools[RenderDevice::kFramesInFlight];
    // Extra pools of the frame slot when framePools[slot] is full.
    std::vector<VkDescriptorPool> overflowPools[RenderDevice::kFramesInFlight];
    uint32_t overflowUsed[RenderDevice::kFramesInFlight] = {};
    uint32_t setsAllocated[RenderDevice::kFramesInFlight] = {};
    VkDescriptorSet allocateFrameSet();
    uint64_t framePoolNumbers[RenderDevice::kFramesInFlight];
    VkDescriptorSet samplerSet;
    // P2 (PERF_PLAN): recordDraw caches its descriptor set per material /
    // override combination per frame slot. The three transient constant
    // buffers are UNIFORM_BUFFER_DYNAMIC bindings - the set only changes when
    // the textures change, their offsets are passed at bind time - so a draw
    // pays no descriptor allocation or writes unless it is the first of its
    // combination in that frame slot. Cleared when the slot is reused (its
    // descriptor pool is reset then). -nodescriptorcache disables it.
    struct DrawSetKey {
        const void *pProgram;
        uint64_t hashcode;
        const void *pShadowOverride;
        const void *pScreen;
        const void *pProjective;
        // The material's CURRENT texture-slot hashes. The runtime swaps a
        // slot's content behind a stable material: the direction signs swap
        // their composed rs_maps face per instance, the police bar swaps the
        // gyro lens art, the sector setup assigns the SHADOW map. A key
        // without this field reuses a set full of the previous views - and
        // when a sector unloaded and another loaded in the same frame, the
        // freed material's address (and hashcode) came back with DIFFERENT
        // textures: the draws sampled views that the pending-delete flush was
        // about to destroy.
        uint64_t textures;
        // Hash-fold of the per-draw EdenDrawParams::TextureOverride textures
        // (the shared library TD_Ground materials draw with a different
        // sector -s map per instance list, so the resolved set differs per
        // draw with the same material stamp).
        uint64_t overrideTextures;
        bool operator==( const DrawSetKey &other ) const {
            return pProgram == other.pProgram && hashcode == other.hashcode &&
                   pShadowOverride == other.pShadowOverride && pScreen == other.pScreen &&
                   pProjective == other.pProjective && textures == other.textures &&
                   overrideTextures == other.overrideTextures;
        }
    };
    std::unordered_map<const EdenMaterial *, std::vector<std::pair<DrawSetKey, VkDescriptorSet>>>
        drawSetCache[RenderDevice::kFramesInFlight];
    uint32_t drawSetCacheSlot = 0xFFFFFFFFu;
    uint64_t drawSetHits = 0;
    void fillSamplerHeap();
    void writeSamplerSet();
    std::vector<VkSampler> samplerHeap;
    GPUBuffer *pZeroBuffer;
    GPUBuffer *pDefaultAttributes;
    GPUTexture *pWhite2D;
    GPUTexture *pWhiteCube;
    GPUTexture *pWhite3D;
    GPUTexture *pShadowRealTime;
    GPUTexture *pFlatNormal;
    GPUTexture *pScreenTexture;
    GPUTexture *pShadowRealTimeOverride = nullptr;
    GPUTexture *pProjectiveTexture = nullptr;
    GPUTexture *pBlack2D = nullptr; // (0, 0, 0, 0)
    bool bDepthOnly = false;
    DrawHook drawHook;
    bool bShadowCaster = false;
    bool bReflectionPass = false;
    std::unordered_map<const EdenMaterial *, std::pair<uint64_t, std::unique_ptr<EdenMaterial>>> casterMaterials;
    std::unordered_map<const EdenMaterial *, std::pair<uint64_t, std::unique_ptr<EdenMaterial>>> zPassMaterials;
    // ZPREPASS permutation (S_PASS_Z) of a material, nullptr when its
    // family has none.
    const EdenMaterial *getZPassMaterial( const EdenMaterial &material );
    const EdenMaterial *getCasterMaterial( const EdenMaterial &material );
    // Everything that selects a pipeline, as plain words (the lookup runs for every draw).
    struct PipelineKey {
        uint64_t Words[6] = {};
        bool operator==( const PipelineKey &o ) const { return memcmp( Words, o.Words, sizeof( Words ) ) == 0; }
    };
    struct PipelineKeyHash {
        size_t operator()( const PipelineKey &k ) const {
            uint64_t h = 1469598103934665603ull;
            for ( uint64_t w : k.Words ) {
                h = ( h ^ w ) * 1099511628211ull;
                h ^= h >> 29;
            }
            return static_cast<size_t>( h );
        }
    };
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines;
    PipelineKey lastPipelineKey;
    VkPipeline lastPipeline = VK_NULL_HANDLE; // getPipeline's one-entry memo
    std::unordered_set<std::string> warnedParameters;
    std::unordered_set<std::string> dumpedPrograms;
    EdenSceneParams scene;
    uint64_t sceneEpoch = 0; // bumped by beginScene (ShaderConstantMember lookup cache)
    uint32_t drawWindowIndex = 0; // reset by beginScene (-drawfrom/-drawcount bisect)
    Matrix4x4f viewProjection;
    const Matrix4x4d *pPreciseViewProjection = nullptr;
    Vector4f frustumPlanes[5]; // left, right, bottom, top, far (inside: dot >= 0)
    bool bProbeRecording = false;
    std::vector<ProbeDraw> probeDraws;
    mutable Vector3f probeMin;
    mutable Vector3f probeMax;
    mutable uint8_t probeKind = 0;
    const EdenOcclusion::Grid *pOcclusionGrid = nullptr;
    mutable uint32_t occlusionTested = 0;
    mutable uint32_t occlusionCulled = 0;
    std::vector<DeferredDraw> deferredDraws;
    std::vector<DeferredDraw> waterDraws; // opaque water buckets, submission order
    std::vector<RtInstance> rtInstances;
    Vector3f rtCameraPosition;
    Vector3f rtCameraDirection;
    bool bRtCapture = false;
    uint32_t numDrawCalls;
    // Redundant-bind filter (docs/PERF_PLAN.md 0a step 1): what this renderer
    // last bound in the frame's command buffer. Valid only while
    // RenderDevice::getCommandTouches() still equals BoundTouches (no one
    // else recorded since) and the buffer is the same; -nobindcache = A/B.
    struct BoundState {
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        uint64_t Touches = ~0ull;
        VkPipeline Pipeline = VK_NULL_HANDLE;
        VkBuffer VertexBuffers[EdenMesh::kMaxStreams + 1] = {};
        VkDeviceSize VertexOffsets[EdenMesh::kMaxStreams + 1] = {};
        VkBuffer IndexBuffer = VK_NULL_HANDLE;
        uint8_t Push[RenderDevice::kPushConstantsSize] = {};
        bool bPushValid = false;
        bool bVertexValid = false;
    } bound;
    // A fully resolved draw: everything recordDraw computed from the material
    // and the params (constants written, descriptor set written), so it can be
    // recorded later on any thread - the material may change right after.
    struct DrawPacket {
        VkPipeline Pipeline;
        VkDescriptorSet Set;
        uint32_t DynamicOffsets[3];
        VkBuffer VertexBuffers[EdenMesh::kMaxStreams + 1];
        VkDeviceSize VertexOffsets[EdenMesh::kMaxStreams + 1];
        VkBuffer IndexBuffer;
        uint32_t IndexCount;
        uint32_t FirstIndex;
        int32_t VertexOffset;
        uint8_t Push[RenderDevice::kPushConstantsSize];
    };
    std::vector<DrawPacket> pendingDraws;
    void recordPacket( VkCommandBuffer commandBuffer, BoundState &state, const DrawPacket &packet,
                       bool bFilter ) const;
    struct ParallelRecord;
    std::unique_ptr<ParallelRecord> pParallel;
    uint64_t parallelFlushes = 0, parallelDraws = 0, inlineFlushDraws = 0;
    double recordMsTotal = 0.0;
    uint64_t recordCountTotal = 0;
    bool bInScene;
    bool bReady;

private:
    VkPipeline getPipeline( const EdenMesh &mesh, const EdenMaterial &material, bool bMirrored );
    void recordDraw( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
                     const EdenDrawParams *pParams );
    void recordDrawImpl( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
                         const EdenDrawParams *pParams );
    void fillConstants( const std::vector<ShaderConstantMember> &members, float *pRegisters, uint32_t numRegisters,
                        const EdenMaterial &material, const Matrix4x4f &world, const EdenDrawParams *pParams,
                        const char *pProgramName , bool bPixel );
    GPUTexture *createWhiteTexture( VkImageViewType viewType, const char *pName );
};
