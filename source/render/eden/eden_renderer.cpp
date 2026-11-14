#include "shared.h"
#include "eden_renderer.h"

#include <cfloat>
#include <chrono>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>
#include "eden_material_handlers.h"

#include "render/texture_cache.h"
#include "system/cmd_line.h"

namespace {
constexpr uint32_t kIntRegistersSize = 16 * 16;
constexpr uint32_t kBoolBitmaskSize = 16;
constexpr uint32_t kVSFloatRegisters = 256;
constexpr uint32_t kPSFloatRegisters = 224;
constexpr VkDeviceSize kVSConstantsSize = kIntRegistersSize + kVSFloatRegisters * 16 + kBoolBitmaskSize;
constexpr VkDeviceSize kPSConstantsSize = kIntRegistersSize + kPSFloatRegisters * 16 + kBoolBitmaskSize;
constexpr VkDeviceSize kZeroBufferSize = 8192;
constexpr uint32_t kSpecDwords = 16;
constexpr uint32_t kMaxSetsPerFrame = 8192;
constexpr uint32_t kNumVSImages = 4;
constexpr uint32_t kNumPSImages = 16;
constexpr uint32_t kVSImageBinding = 6;
constexpr uint32_t kPSImageBinding = 13;
constexpr uint32_t kDefaultAttributeBinding = EdenMesh::kMaxStreams;

// D3D9SpecializationInfo: dword 1 bits 21..23 = alpha compare op.
constexpr uint32_t kSpecAlphaCompareOpShift = 21;
// D3D9RenderStateInfo::alphaRef
constexpr uint32_t kPushAlphaRefOffset = 24;
// GetPushSamplerOffset(0) = MaxSharedPushDataSize
constexpr uint32_t kPushSamplerOffset = 64;

enum eSamplerHeapIndex : uint8_t {
    SH_LinearWrap = 0,
    SH_LinearClamp,
    SH_PointWrap,
    SH_PointClamp,
    SH_LinearMirror,
    // 2DM per-slot addressing (MaterialTextureSlot::AddressU / V): linear,
    // SH_LinearUV + 3 * u + v with 0 wrap, 1 mirror, 2 clamp.
    SH_LinearUV,
    SH_Count = SH_LinearUV + 9
};

// 2DM slot address byte -> 0 wrap, 1 mirror, 2 clamp. 1 wrap and 2 mirror are
// verified (the avatar LUT / implant mirror, skin_zones_spec 1.7). 3 is NOT
// honoured as clamp [DATA + INF]: every world material whose slot 0 carries a
// 3 and whose UV0 leaves [0,1] on that axis would collapse to one texel
// row/column - the airport apron 'Airport_lines' (beton_b_c U=3, u 0.47..5.03)
// drew the atlas's red/white kerb column over the whole apron (the owner's
// red runway stripes / red speckle ground), SA_Stone_White (u 110..138),
// Sector-2-4-6-5_ (v 7.5..8.5), SF_Elec_IronPol, SB_aq_LOD... 9 of 13 slot-0
// "clamp" materials around the casino, 7/11 at the car wash, 5/5 at the
// airport are out of range (the -loguvrange '*' survey, session 2026-11-04).
// So 3 (and 4) sample as wrap; -edenclamp3 restores clamp (the A/B).
uint32_t AddressIndex( uint8_t d3dAddress ) {
    static const bool bClamp3 = CmdLine::HasArg( "-edenclamp3" );
    switch ( d3dAddress ) {
    case 2:
    case 5:
        return 1;
    case 3:
    case 4:
        return bClamp3 ? 2 : 0;
    default:
        return 0;
    }
}

VkFormat GetAttributeFormat( eVertexAttributeFormat format ) {
    switch ( format ) {
    case VAF_POSITION_R32G32B32_SFLOAT:
    case VAF_NORMAL_R32G32B32_SFLOAT:
    case VAF_TANGENT_R32G32B32_SFLOAT:
    case VAF_BONEWEIGHT_R32G32B32_SFLOAT:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VAF_POSITION_R32G32B32A32_SFLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VAF_POSITION_R16G16B16A16_SFLOAT:
    case VAF_NORMAL_R16G16B16A16_SFLOAT:
    case VAF_TANGENT_R16G16B16A16_SFLOAT:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case VAF_COLOR_B8G8R8A8_UNORM:
    case VAF_COLOR_B8G8R8A8_UNORM_2:
    case VAF_COLOR_B8G8R8A8_UNORM_3:
    case VAF_COLOR_B8G8R8A8_UNORM_4:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case VAF_NORMAL_A2B10G10R10_SNORM_PACK32:
    case VAF_TANGENT_A2B10G10R10_SNORM_PACK32:
        return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    case VAF_NORMAL_R16G16B16A16_UNORM:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case VAF_NORMAL_R16G16B16A16_SNORM:
    case VAF_TANGENT_R16G16B16A16_SNORM:
        return VK_FORMAT_R16G16B16A16_SNORM;
    case VAF_TEXCOORD_R32G32_SFLOAT:
    case VAF_BONEWEIGHT_R32G32_SFLOAT:
        return VK_FORMAT_R32G32_SFLOAT;
    case VAF_TEXCOORD_R16G16_SNORM:
        return VK_FORMAT_R16G16_SNORM;
    case VAF_TEXCOORD_R16G16_SFLOAT:
        return VK_FORMAT_R16G16_SFLOAT;
    case VAF_POSITION_R32_SFLOAT:
    case VAF_BONEWEIGHT_R32_SFLOAT:
        return VK_FORMAT_R32_SFLOAT;
    case VAF_BONEINDEX_R8G8B8A8_USCALED:
        return VK_FORMAT_R8G8B8A8_USCALED;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

VkBlendFactor GetBlendFactor( uint8_t d3dBlend ) {
    switch ( d3dBlend ) {
    case 1: return VK_BLEND_FACTOR_ZERO;
    case 2: return VK_BLEND_FACTOR_ONE;
    case 3: return VK_BLEND_FACTOR_SRC_COLOR;
    case 4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 5: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 7: return VK_BLEND_FACTOR_DST_ALPHA;
    case 8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 9: return VK_BLEND_FACTOR_DST_COLOR;
    case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 11: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    // D3DBLENDFACTOR / D3DBLEND_INVBLENDFACTOR: the constant blend factor
    // (EdenMaterial::BlendFactor, the water clarity).
    case 14: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 15: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

// Writes a D3D matrix (Matrix4x4f memory = D3D row major layout) to shader
// registers. HLSL packs matrices column major by default: register k holds
// column k.
void WriteMatrix( float *pOut, uint32_t numRegisters, uint8_t parameterClass, const Matrix4x4f &matrix ) {
    static const bool bTranspose = CmdLine::HasArg( "-edentranspose" );
    if ( bTranspose ) {
        parameterClass = parameterClass == 2 ? 3 : 2;
    }
    for ( uint32_t k = 0; k < numRegisters && k < 4; k++ ) {
        for ( uint32_t i = 0; i < 4; i++ ) {
            pOut[k * 4 + i] = parameterClass == 2 ? matrix.m[k][i] : matrix.m[i][k];
        }
    }
}

void WriteVector( float *pOut, const Vector4f &v ) {
    pOut[0] = v.x;
    pOut[1] = v.y;
    pOut[2] = v.z;
    pOut[3] = v.w;
}

void WriteVector( float *pOut, const Vector3f &v, float w = 0.0f ) { WriteVector( pOut, Vector4f( v, w ) ); }

Vector3f Mul( const Vector3f &a, const Vector4f &b ) { return Vector3f( a.x * b.x, a.y * b.y, a.z * b.z ); }

std::string StripSuffix( const std::string &name ) {
    const size_t position = name.find( "__" );
    if ( position == std::string::npos || position == 0 ) {
        return name;
    }
    return name.substr( 0, position );
}
} // namespace

void EdenMesh::computeLayoutKey() {
    uint64_t hash = 0xcbf29ce484222325ull;
    auto mix = [&]( uint64_t value ) {
        hash ^= value;
        hash *= 0x100000001b3ull;
    };
    mix( NumStreams );
    for ( uint32_t i = 0; i < kMaxStreams; i++ ) {
        mix( Strides[i] );
    }
    mix( static_cast<uint64_t>( Topology ) );
    for ( const RawVertexAttribute &attribute : Attributes ) {
        mix( ( static_cast<uint64_t>( attribute.Usage ) << 32 ) | ( static_cast<uint64_t>( attribute.UsageIndex ) << 24 ) |
             ( static_cast<uint64_t>( attribute.Stream ) << 16 ) | ( static_cast<uint64_t>( attribute.Format ) << 8 ) );
        mix( attribute.Offset );
    }
    LayoutKey = hash;
}

// ---- parallel command recording (docs/PERF_PLAN.md 0a) ---------------------
//
// One context per chunk index: chunk k of every flush is recorded by the same
// thread role (0 = the main thread, k = worker k-1) into a secondary command
// buffer of context k, so a command pool is only ever used by one thread at a
// time. The pools are reset when their frame slot comes back (beginScene's
// slot reset, after the device waited that slot's fence).
struct EdenRenderer::ParallelRecord {
    struct Context {
        VkCommandPool Pools[RenderDevice::kFramesInFlight] = {};
        std::vector<VkCommandBuffer> Buffers[RenderDevice::kFramesInFlight];
        uint32_t Used[RenderDevice::kFramesInFlight] = {};
    };
    struct Worker {
        std::thread Thread;
        std::mutex Mutex;
        std::condition_variable Cv;
        bool bHasTask = false;
        bool bDone = true;
        bool bStop = false;
        uint32_t Chunk = 0;
    };
    std::vector<Context> Contexts;
    std::vector<std::unique_ptr<Worker>> Workers;
    const std::function<void( uint32_t )> *pJob = nullptr;

    // Joins the workers even when destroy() did not run (an early exit): a
    // joinable std::thread destroyed would terminate the process.
    ~ParallelRecord() {
        for ( auto &pWorker : Workers ) {
            {
                std::lock_guard<std::mutex> lock( pWorker->Mutex );
                pWorker->bStop = true;
            }
            pWorker->Cv.notify_all();
            if ( pWorker->Thread.joinable() ) {
                pWorker->Thread.join();
            }
        }
    }

    void workerLoop( Worker &worker ) {
        std::unique_lock<std::mutex> lock( worker.Mutex );
        for ( ;; ) {
            worker.Cv.wait( lock, [&] { return worker.bHasTask || worker.bStop; } );
            if ( worker.bStop ) {
                return;
            }
            worker.bHasTask = false;
            const uint32_t chunk = worker.Chunk;
            lock.unlock();
            ( *pJob )( chunk );
            lock.lock();
            worker.bDone = true;
            worker.Cv.notify_all();
        }
    }
    // job(0) on the calling thread, job(k) on worker k-1; returns when all ran.
    void run( uint32_t count, const std::function<void( uint32_t )> &job ) {
        pJob = &job;
        for ( uint32_t k = 1; k < count; k++ ) {
            Worker &worker = *Workers[k - 1];
            {
                std::lock_guard<std::mutex> lock( worker.Mutex );
                worker.Chunk = k;
                worker.bDone = false;
                worker.bHasTask = true;
            }
            worker.Cv.notify_all();
        }
        job( 0 );
        for ( uint32_t k = 1; k < count; k++ ) {
            Worker &worker = *Workers[k - 1];
            std::unique_lock<std::mutex> lock( worker.Mutex );
            worker.Cv.wait( lock, [&] { return worker.bDone; } );
        }
        pJob = nullptr;
    }
};

EdenRenderer::EdenRenderer()
    : pDevice( nullptr ),
      pTextureCache( nullptr ),
      setLayouts{ VK_NULL_HANDLE, VK_NULL_HANDLE },
      pipelineLayout( VK_NULL_HANDLE ),
      staticPool( VK_NULL_HANDLE ),
      framePools{},
      framePoolNumbers{},
      samplerSet( VK_NULL_HANDLE ),
      pZeroBuffer( nullptr ),
      pDefaultAttributes( nullptr ),
      pWhite2D( nullptr ),
      pWhiteCube( nullptr ),
      pWhite3D( nullptr ),
      pShadowRealTime( nullptr ),
      pFlatNormal( nullptr ),
      pScreenTexture( nullptr ),
      numDrawCalls( 0 ),
      bInScene( false ),
      bReady( false ) {}

EdenRenderer::~EdenRenderer() {}

GPUTexture *EdenRenderer::createWhiteTexture( VkImageViewType viewType, const char *pName ) {
    static const uint32_t kWhite = 0xffffffffu;
    const uint32_t numLayers = viewType == VK_IMAGE_VIEW_TYPE_CUBE ? 6 : 1;
    const void *data[6] = { &kWhite, &kWhite, &kWhite, &kWhite, &kWhite, &kWhite };
    const uint32_t sizes[6] = { 4, 4, 4, 4, 4, 4 };
    return pDevice->createTexture( viewType, 1, 1, 1, numLayers, 1, VK_FORMAT_R8G8B8A8_UNORM, data, sizes, pName );
}

void EdenRenderer::fillSamplerHeap() {
    samplerHeap.resize( SH_Count );
    samplerHeap[SH_LinearWrap] = pDevice->getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Wrap );
    samplerHeap[SH_LinearClamp] = pDevice->getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Clamp );
    samplerHeap[SH_PointWrap] = pDevice->getSampler( eSamplerFilter::SF_Point, eSamplerAddress::SA_Wrap );
    samplerHeap[SH_PointClamp] = pDevice->getSampler( eSamplerFilter::SF_Point, eSamplerAddress::SA_Clamp );
    samplerHeap[SH_LinearMirror] = pDevice->getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Mirror );
    static const eSamplerAddress kAddress[3] = { eSamplerAddress::SA_Wrap, eSamplerAddress::SA_Mirror,
                                                 eSamplerAddress::SA_Clamp };
    for ( uint32_t u = 0; u < 3; u++ ) {
        for ( uint32_t v = 0; v < 3; v++ ) {
            samplerHeap[SH_LinearUV + 3 * u + v] =
                pDevice->getSampler( eSamplerFilter::SF_Linear, kAddress[u], kAddress[v] );
        }
    }

}

void EdenRenderer::writeSamplerSet() {
    VkDevice device = pDevice->getDevice();
    std::vector<VkDescriptorImageInfo> samplerInfos( SH_Count );
    for ( uint32_t i = 0; i < SH_Count; i++ ) {
        samplerInfos[i].sampler = samplerHeap[i];
    }
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = samplerSet;
    write.dstBinding = 0;
    write.descriptorCount = SH_Count;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = samplerInfos.data();
    vkUpdateDescriptorSets( device, 1, &write, 0, nullptr );

}

// Options > Video: the texture filtering level changed the linear samplers.
void EdenRenderer::refreshSamplers() {
    if ( pDevice == nullptr || samplerSet == VK_NULL_HANDLE ) {
        return;
    }
    pDevice->waitIdle();
    fillSamplerHeap();
    writeSamplerSet();
}

bool EdenRenderer::initialize( RenderDevice *pRenderDevice, TextureCache *pCache ) {
    pDevice = pRenderDevice;
    pTextureCache = pCache;
    if ( CmdLine::HasArg( "-nosob" ) || !pDevice->supportsDxsoShaders() ) {
        return false;
    }
    if ( !shaderSystem.initialize() ) {
        return false;
    }
    VkDevice device = pDevice->getDevice();

    // Set 0: DXVK D3D9 resource slots (see computeResourceSlotId).
    // Bindings 0 / 10 / 31 are the transient constant buffers (VS, PS, spec)
    // as UNIFORM_BUFFER_DYNAMIC: the shader cannot tell the difference, and
    // the per-draw offsets at bind time let recordDraw cache the set (P2).
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.push_back(
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr } );
    bindings.push_back( { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr } );
    for ( uint32_t i = 0; i < kNumVSImages; i++ ) {
        bindings.push_back(
            { kVSImageBinding + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr } );
    }
    bindings.push_back(
        { 10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } );
    bindings.push_back( { 12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } );
    for ( uint32_t i = 0; i < kNumPSImages; i++ ) {
        bindings.push_back(
            { kPSImageBinding + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } );
    }
    bindings.push_back( { 31, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } );
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>( bindings.size() );
    layoutInfo.pBindings = bindings.data();
    if ( vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &setLayouts[0] ) != VK_SUCCESS ) {
        return false;
    }
    // Set 1: sampler heap
    const VkDescriptorSetLayoutBinding samplerBinding = {
        0, VK_DESCRIPTOR_TYPE_SAMPLER, SH_Count, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    if ( vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &setLayouts[1] ) != VK_SUCCESS ) {
        return false;
    }
    VkPushConstantRange pushRange = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                      RenderDevice::kPushConstantsSize };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if ( vkCreatePipelineLayout( device, &pipelineLayoutInfo, nullptr, &pipelineLayout ) != VK_SUCCESS ) {
        return false;
    }

    // Static resources
    std::vector<uint8_t> zeros( kZeroBufferSize, 0 );
    pZeroBuffer = pDevice->createBuffer( kZeroBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, zeros.data() );
    const float defaults[] = {
        0.0f, 0.0f, 0.0f, 1.0f, // 0: generic
        1.0f, 1.0f, 1.0f, 1.0f, // 16: colors
        0.0f, 1.0f, 0.0f, 0.0f, // 32: normal
        1.0f, 0.0f, 0.0f, 0.0f, // 48: tangent / binormal
    };
    pDefaultAttributes = pDevice->createBuffer( sizeof( defaults ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, defaults );
    pWhite2D = createWhiteTexture( VK_IMAGE_VIEW_TYPE_2D, "eden_white_2d" );
    pWhiteCube = createWhiteTexture( VK_IMAGE_VIEW_TYPE_CUBE, "eden_white_cube" );
    pWhite3D = createWhiteTexture( VK_IMAGE_VIEW_TYPE_3D, "eden_white_3d" );
    {
        static const uint32_t kBlack = 0u;
        const void *data = &kBlack;
        const uint32_t size = 4;
        pBlack2D = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_2D, 1, 1, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &data, &size,
                                           "eden_black_2d" );
    }
    {
        // gTexSamplerShadowRealTime (s15): screen space sun shadow / light
        // buffer. x, w: sun visibility, y, z: packed additional lighting.
        static const uint32_t kLit = 0xff0000ffu; // RGBA (255, 0, 0, 255)
        const void *data = &kLit;
        const uint32_t size = 4;
        pShadowRealTime = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_2D, 1, 1, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &data,
                                                  &size, "eden_shadow_realtime" );
        static const uint32_t kFlatNormal = 0xffff8080u; // (128, 128, 255, 255)
        const void *normalData = &kFlatNormal;
        pFlatNormal = pDevice->createTexture( VK_IMAGE_VIEW_TYPE_2D, 1, 1, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                              &normalData, &size, "eden_flat_normal" );
    }

    samplerHeap.resize( SH_Count );
    fillSamplerHeap();
    const VkDescriptorPoolSize staticSizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, SH_Count } };
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = staticSizes;
    vkCreateDescriptorPool( device, &poolInfo, nullptr, &staticPool );

    const VkDescriptorPoolSize frameSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxSetsPerFrame * 2 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kMaxSetsPerFrame * 3 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSetsPerFrame * ( kNumVSImages + kNumPSImages ) },
    };
    poolInfo.maxSets = kMaxSetsPerFrame;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = frameSizes;
    for ( VkDescriptorPool &pool : framePools ) {
        vkCreateDescriptorPool( device, &poolInfo, nullptr, &pool );
    }

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = staticPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayouts[1];
    if ( vkAllocateDescriptorSets( device, &allocInfo, &samplerSet ) != VK_SUCCESS ) {
        return false;
    }
    writeSamplerSet();

    // Parallel command recording (docs/PERF_PLAN.md 0a): the worker threads
    // record the queued draws of an offscreen pass into secondary command
    // buffers. -recordthreads <n> sets the worker count (default: up to 6,
    // leaving two hardware threads for the main thread and the rest);
    // -noparallelrecord (or 0 workers) records every draw inline.
    {
        const uint32_t hardware = Max( 1u, std::thread::hardware_concurrency() );
        const int32_t defaultWorkers = static_cast<int32_t>( Min( 6u, hardware > 2 ? hardware - 2 : 0u ) );
        const int32_t workers =
            CmdLine::HasArg( "-noparallelrecord" ) ? 0 : Clamp( CmdLine::GetArgInt( "-recordthreads", defaultWorkers ), 0, 15 );
        if ( workers > 0 ) {
            pParallel = std::make_unique<ParallelRecord>();
            pParallel->Contexts.resize( static_cast<size_t>( workers ) + 1 );
            for ( ParallelRecord::Context &context : pParallel->Contexts ) {
                for ( uint32_t slot = 0; slot < RenderDevice::kFramesInFlight; slot++ ) {
                    VkCommandPoolCreateInfo poolInfo = {};
                    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                    poolInfo.queueFamilyIndex = pDevice->getGraphicsQueueFamily();
                    vkCreateCommandPool( device, &poolInfo, nullptr, &context.Pools[slot] );
                }
            }
            for ( int32_t i = 0; i < workers; i++ ) {
                pParallel->Workers.push_back( std::make_unique<ParallelRecord::Worker>() );
                ParallelRecord::Worker &worker = *pParallel->Workers.back();
                ParallelRecord *pRecord = pParallel.get();
                worker.Thread = std::thread( [pRecord, &worker]() { pRecord->workerLoop( worker ); } );
            }
            OTDU_LOG_INFO( "EdenRenderer: parallel command recording, %d worker threads\n", workers );
        }
    }

    bReady = pZeroBuffer != nullptr && pDefaultAttributes != nullptr && pWhite2D != nullptr &&
             pWhiteCube != nullptr && pWhite3D != nullptr;
    return bReady;
}

void EdenRenderer::destroy() {
    if ( pDevice == nullptr ) {
        return;
    }
    VkDevice device = pDevice->getDevice();
    if ( pParallel != nullptr ) {
        pendingDraws.clear();
        pDevice->setPendingRecordHook( nullptr );
        for ( auto &pWorker : pParallel->Workers ) {
            {
                std::lock_guard<std::mutex> lock( pWorker->Mutex );
                pWorker->bStop = true;
            }
            pWorker->Cv.notify_all();
            if ( pWorker->Thread.joinable() ) {
                pWorker->Thread.join();
            }
        }
        for ( ParallelRecord::Context &context : pParallel->Contexts ) {
            for ( uint32_t slot = 0; slot < RenderDevice::kFramesInFlight; slot++ ) {
                if ( context.Pools[slot] != VK_NULL_HANDLE ) {
                    vkDestroyCommandPool( device, context.Pools[slot], nullptr );
                }
            }
        }
        OTDU_LOG_INFO( "EdenRenderer: parallel recording - %llu flushes, %llu draws in parallel, %llu inline\n",
                       static_cast<unsigned long long>( parallelFlushes ), static_cast<unsigned long long>( parallelDraws ),
                       static_cast<unsigned long long>( inlineFlushDraws ) );
        pParallel.reset();
    }
    for ( auto &pipeline : pipelines ) {
        pDevice->destroyPipeline( pipeline.second );
    }
    pipelines.clear();
    lastPipeline = VK_NULL_HANDLE;
    if ( pipelineLayout != VK_NULL_HANDLE ) {
        vkDestroyPipelineLayout( device, pipelineLayout, nullptr );
        pipelineLayout = VK_NULL_HANDLE;
    }
    for ( VkDescriptorSetLayout &layout : setLayouts ) {
        if ( layout != VK_NULL_HANDLE ) {
            vkDestroyDescriptorSetLayout( device, layout, nullptr );
        }
        layout = VK_NULL_HANDLE;
    }
    for ( VkDescriptorPool &pool : framePools ) {
        if ( pool != VK_NULL_HANDLE ) {
            vkDestroyDescriptorPool( device, pool, nullptr );
        }
        pool = VK_NULL_HANDLE;
    }
    for ( std::vector<VkDescriptorPool> &pools : overflowPools ) {
        for ( VkDescriptorPool pool : pools ) {
            vkDestroyDescriptorPool( device, pool, nullptr );
        }
        pools.clear();
    }
    if ( staticPool != VK_NULL_HANDLE ) {
        vkDestroyDescriptorPool( device, staticPool, nullptr );
        staticPool = VK_NULL_HANDLE;
    }
    pDevice->destroyBuffer( pZeroBuffer );
    pDevice->destroyBuffer( pDefaultAttributes );
    pDevice->destroyTexture( pWhite2D );
    pDevice->destroyTexture( pWhiteCube );
    pDevice->destroyTexture( pWhite3D );
    pDevice->destroyTexture( pShadowRealTime );
    pDevice->destroyTexture( pFlatNormal );
    pDevice->destroyTexture( pBlack2D );
    pBlack2D = nullptr;
    pZeroBuffer = pDefaultAttributes = nullptr;
    pWhite2D = pWhiteCube = pWhite3D = nullptr;
    shaderSystem.destroy();
    bReady = false;
}

void EdenRenderer::setupMaterial( const MaterialDesc &desc, EdenMaterial &outMaterial, const char *pTextureOwner ) {
    outMaterial.Name = desc.Name;
    outMaterial.Hashcode = desc.Hashcode;
    outMaterial.ShaderSetGUID = desc.ShaderSetGUID;
    memcpy( outMaterial.Spec, desc.Spec, sizeof( outMaterial.Spec ) );
    memcpy( outMaterial.States, desc.States, sizeof( outMaterial.States ) );
    outMaterial.Parameters = desc.Parameters;
    outMaterial.Diffuse = desc.DiffuseColor;
    outMaterial.Ambient = desc.AmbientColor;
    outMaterial.Specular = desc.SpecularColor;
    outMaterial.Emissive = desc.EmissiveColor;
    outMaterial.SrcBlend = desc.SrcBlend;
    outMaterial.DstBlend = desc.DstBlend;
    static const bool bNoBlend = CmdLine::HasArg( "-edennoblend" );
    if ( bNoBlend ) {
        outMaterial.SrcBlend = 2;
        outMaterial.DstBlend = 1;
    }
    outMaterial.CullMode = desc.CullMode;
    outMaterial.AlphaRef = desc.AlphaRef;
    outMaterial.bAlphaTest = desc.AlphaRef != 0 && desc.AlphaTest == 0;
    outMaterial.bDepthTest = desc.DepthTest != 0;
    outMaterial.bDepthWrite = desc.DepthWrite != 0;

    for ( uint32_t i = 0; i < 16; i++ ) {
        outMaterial.pTextures[i] = nullptr;
        outMaterial.SamplerIndex[i] = SH_LinearWrap;
    }
    for ( size_t i = 0; i < desc.AllTextures.size() && i < 16; i++ ) {
        const uint64_t textureHash = desc.AllTextures[i].TextureHashcode;
        outMaterial.bNormalSlot[i] =
            ToUpper( IdentifierToString( desc.AllTextures[i].SlotType ) ).find( "NORMAL" ) != std::string::npos;
        // Slot addressing (2DM slot +0x18 / +0x19).
        const uint32_t addressU = AddressIndex( desc.AllTextures[i].AddressU );
        uint32_t addressV = AddressIndex( desc.AllTextures[i].AddressV );
        // The hairline cards of the avatars (hair_implant_front_nuque, the
        // 'implant_front' / 'Implant_front_n' TD_Hair shells) carry V up to
        // 1.26 with a V = mirror slot byte: the mirrored rows 0.74..1.0 hold
        // hair, which draws as a dark curved stroke on the forehead above one
        // eyebrow (character-select male, build 35). Wrap there is the full
        // black band (skin_zones_spec 1.7 #2); clamp reads the transparent
        // bottom row and leaves the forehead clean [EXP, -implantmirror is the
        // A/B]. The retail sampler state for these layers is [ENC].
        static const bool bImplantMirror = CmdLine::HasArg( "-implantmirror" );
        if ( !bImplantMirror && addressV == 1 && desc.Name.find( "mplant_front" ) != std::string::npos ) {
            addressV = 2;
        }
        static const bool bWrapAll = CmdLine::HasArg( "-edenwrapall" ); // A/B: ignore the 2DM addressing
        if ( ( addressU != 0 || addressV != 0 ) && !bWrapAll ) {
            outMaterial.SamplerIndex[i] = static_cast<uint8_t>( SH_LinearUV + 3 * addressU + addressV );
        }
        if ( textureHash == 0 ) {
            continue;
        }
        GPUTexture *pTexture = nullptr;
        if ( pTextureOwner != nullptr ) {
            // By hash: the string form re-hashes IdentifierToString(hashcode),
            // whose round trip is not idempotent (slot bytes in 'a'..'z' fold
            // again) and missed textures registered under their true hash.
            pTexture = pTextureCache->find( pTextureOwner, textureHash );
        }
        if ( pTexture == nullptr ) {
            // Not in the material's own bank: borrowed from another owner,
            // which then outlives this one (TextureCache::findForOwner).
            pTexture = pTextureCache->findForOwner( pTextureOwner, textureHash );
        }
        outMaterial.pTextures[i] = pTexture;
        if ( pTexture == nullptr ) {
            OTDU_LOG_DEBUG( "Eden material '%s': texture '%s' (slot %zu) not found\n", desc.Name.c_str(),
                            IdentifierToString( textureHash ).c_str(), i );
        }
    }
    for ( size_t i = 0; i < 16; i++ ) {
        outMaterial.TextureHashes[i] = i < desc.AllTextures.size() ? desc.AllTextures[i].TextureHashcode : 0;
        outMaterial.SlotTypes[i] = i < desc.AllTextures.size() ? desc.AllTextures[i].SlotType : 0;
    }
    outMaterial.NumSlots = static_cast<uint32_t>( Min<size_t>( desc.AllTextures.size(), 16 ) );

    // Load time preparation of the original renderer (GUID / switches /
    // render states rewritten by the shader family handler).
    outMaterial.Bucket = ApplyEdenMaterialHandler( outMaterial );
    applyRenderStates( outMaterial );
    updateProgram( outMaterial );
}

void EdenRenderer::applyRenderStates( EdenMaterial &material ) {
    const uint8_t *pStates = material.States;
    const bool bBlend = pStates[0xa] != 6;
    material.SrcBlend = bBlend ? pStates[8] : 2;
    material.DstBlend = bBlend ? pStates[9] : 1;
    static const bool bNoBlend = CmdLine::HasArg( "-edennoblend" );
    if ( bNoBlend ) {
        material.SrcBlend = 2;
        material.DstBlend = 1;
    }
    material.bDepthTest = pStates[0] != 0;
    material.bDepthWrite = pStates[1] != 0 && pStates[1] != 2;
    material.CullMode = pStates[3];
    material.AlphaRef = pStates[7];
    material.bAlphaTest = pStates[5] == 0;
    material.DepthFunc = pStates[0xb] <= 8 ? pStates[0xb] : 0;
    // TD_ShadowBox volumes (back faces drawn with GREATEREQUAL) are wound
    // the other way round than the rest of the scene in this renderer
    // (observed: their stated culling keeps the front faces).
    // (4 = OpenTDU2 internal "cull the other face"; 3 is two-sided).
    if ( material.ShaderSetGUID == 0x132435465768798aull && ( material.CullMode == 2 || material.CullMode == 3 ) ) {
        material.CullMode = material.CullMode == 2 ? 4 : 2;
    }
}

bool EdenRenderer::assignTexture( EdenMaterial &material, const char *pSlotType, GPUTexture *pTexture ) {
    const uint64_t slotHash = GetIdentifier64bit( pSlotType );
    for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
        if ( material.SlotTypes[i] == slotHash ) {
            material.pTextures[i] = pTexture;
            material.TextureHashes[i] = pTexture != nullptr ? GetIdentifier64bit( pTexture->Name.c_str() ) : 0;
            return true;
        }
    }
    return false;
}

void EdenRenderer::updateProgram( EdenMaterial &material ) {
    // Runtime switches (the scene type handler of the original renderer is in
    // the encrypted part of the executable; its effect is reproduced from the
    // permutations the libraries contain).
    uint8_t spec[EdenShaderSystem::kNumSpecs];
    memcpy( spec, material.Spec, sizeof( spec ) );
    uint32_t numTextures = 0;
    for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
        if ( material.TextureHashes[i] != 0 ) {
            numTextures++;
        }
    }
    ShaderLibrary *pLibrary = shaderSystem.findLibrary( material.ShaderSetGUID );
    const std::string family = pLibrary != nullptr ? ToUpper( pLibrary->getName() ) : std::string();
    if ( family == "TD_HEIGHTMAP" || family == "TD_GROUND_HM" ) {
        // Slots: COLOR, SHADOW (+ night lights, grass), DETAIL1, DETAIL1 normal,
        // DETAIL2, DETAIL2 normal, DETAIL3 (normal xy + gloss + color).
        // S_USERTYPE4: grid (0: sector 3 m, 1: area 24 m, 2: island 384 m)
        // S_USERTYPE7: detail layers mask.
        spec[35] = material.HeightfieldGrid;
        if ( material.HeightfieldGrid == 0 ) {
            const bool bDetail1 = material.TextureHashes[2] != 0;
            const bool bDetail2 = material.TextureHashes[4] != 0;
            const bool bDetail3 = material.TextureHashes[6] != 0;
            spec[38] = static_cast<uint8_t>( ( bDetail1 ? 1 : 0 ) | ( bDetail2 ? 2 : 0 ) | ( bDetail3 ? 4 : 0 ) );
            spec[0] = static_cast<uint8_t>( 2 + ( bDetail1 ? 2 : 0 ) + ( bDetail2 ? 2 : 0 ) + ( bDetail3 ? 1 : 0 ) );
        } else {
            spec[38] = 0;
            spec[0] = 1;
        }
    } else if ( numTextures > 0 ) {
        spec[EdenShaderSystem::S_NBTEXTURES] = static_cast<uint8_t>( numTextures );
    }
    material.pProgram = shaderSystem.getProgram( material.ShaderSetGUID, spec );
}

void EdenRenderer::beginScene( const EdenSceneParams &params ) {
    scene = params;
    sceneEpoch++;
    if ( CmdLine::GetArgInt( "-drawfrom", -1 ) >= 0 ) {
        drawWindowIndex = 0; // the window addresses draws within each pass
    }
    viewProjection = scene.Projection.mulPrecise( scene.View );
    // Clip space planes of viewProjection (clip_j = sum_i m[i][j] p_i, D3D:
    // -w <= x, y <= w, z <= w), normalised for sphere distances.
    {
        const Matrix4x4f &m = viewProjection;
        auto plane = [&m]( int32_t axis, float sign ) {
            Vector4f p( m.m[0][3] + sign * m.m[0][axis], m.m[1][3] + sign * m.m[1][axis],
                        m.m[2][3] + sign * m.m[2][axis], m.m[3][3] + sign * m.m[3][axis] );
            const float length = std::sqrt( p.x * p.x + p.y * p.y + p.z * p.z );
            return length > 1e-12f ? Vector4f( p.x / length, p.y / length, p.z / length, p.w / length ) : Vector4f( 0, 0, 0, 1 );
        };
        frustumPlanes[0] = plane( 0, 1.0f );
        frustumPlanes[1] = plane( 0, -1.0f );
        frustumPlanes[2] = plane( 1, 1.0f );
        frustumPlanes[3] = plane( 1, -1.0f );
        frustumPlanes[4] = plane( 2, -1.0f );
    }
    deferredDraws.clear();
    waterDraws.clear();
    numDrawCalls = 0;
    bInScene = true;

    VkDevice device = pDevice->getDevice();
    const uint32_t slot = pDevice->getFrameSlot();
    if ( framePoolNumbers[slot] != pDevice->getFrameNumber() ) {
        vkResetDescriptorPool( device, framePools[slot], 0 );
        for ( VkDescriptorPool pool : overflowPools[slot] ) {
            vkResetDescriptorPool( device, pool, 0 );
        }
        // The pool reset frees every set this slot handed out - the cache must
        // drop them HERE, not lazily at the first draw: a frame that records
        // without a scene-begin reset on this slot (the map's flat views skip
        // the 3D scene) would otherwise bind the reset sets again, which the
        // validation layers caught as "not a valid VkDescriptorSet" during the
        // map interaction.
        drawSetCache[slot].clear();
        drawSetCacheSlot = slot;
        if ( pParallel != nullptr ) {
            for ( ParallelRecord::Context &context : pParallel->Contexts ) {
                vkResetCommandPool( device, context.Pools[slot], 0 );
                context.Used[slot] = 0;
            }
        }
        static const bool bLogSets = CmdLine::HasArg( "-logsets" );
        if ( bLogSets ) {
            OTDU_LOG_INFO( "Eden sets last frame of slot %u: %u\n", slot, setsAllocated[slot] );
        }
        setsAllocated[slot] = 0;
        overflowUsed[slot] = 0;
        framePoolNumbers[slot] = pDevice->getFrameNumber();
    }

    resumeScene();
}

bool EdenRenderer::isBoxVisible( const Matrix4x4f &world, const Vector3f &boundsMin, const Vector3f &boundsMax,
                                 float margin ) const {
    static const bool bNoCull = CmdLine::HasArg( "-noobjcull" );
    if ( bNoCull || boundsMin.x > boundsMax.x ) {
        return true;
    }
    const Vector3f center = world.transformPoint( ( boundsMin + boundsMax ) * 0.5f );
    const auto axisLength = [&world]( int32_t row ) {
        return std::sqrt( world.m[row][0] * world.m[row][0] + world.m[row][1] * world.m[row][1] +
                          world.m[row][2] * world.m[row][2] );
    };
    const float scale = Max( axisLength( 0 ), Max( axisLength( 1 ), axisLength( 2 ) ) );
    const float radius = ( boundsMax - boundsMin ).length() * 0.5f * scale + margin;
    for ( const Vector4f &p : frustumPlanes ) {
        if ( p.x * center.x + p.y * center.y + p.z * center.z + p.w < -radius ) {
            return false;
        }
    }
    if ( bProbeRecording || pOcclusionGrid != nullptr ) {
        // The world AABB of the 8 transformed corners (+ the margin).
        Vector3f worldMin( FLT_MAX, FLT_MAX, FLT_MAX ), worldMax( -FLT_MAX, -FLT_MAX, -FLT_MAX );
        for ( int32_t corner = 0; corner < 8; corner++ ) {
            const Vector3f local( ( corner & 1 ) ? boundsMax.x : boundsMin.x, ( corner & 2 ) ? boundsMax.y : boundsMin.y,
                                  ( corner & 4 ) ? boundsMax.z : boundsMin.z );
            const Vector3f point = world.transformPoint( local );
            worldMin = Vector3f( Min( worldMin.x, point.x ), Min( worldMin.y, point.y ), Min( worldMin.z, point.z ) );
            worldMax = Vector3f( Max( worldMax.x, point.x ), Max( worldMax.y, point.y ), Max( worldMax.z, point.z ) );
        }
        const Vector3f pad( margin, margin, margin );
        // (The cube around the bounding sphere was tried here: it culled 76 %
        // instead of 84 % and did not change the 2 faint 4K pixels - the box.)
        if ( isOccluded( worldMin - pad, worldMax + pad ) ) {
            return false;
        }
        if ( bProbeRecording ) {
            setProbeBox( worldMin - pad, worldMax + pad, 1 );
        }
    }
    return true;
}

bool EdenRenderer::isOccluded( const Vector3f &boxMin, const Vector3f &boxMax ) const {
    if ( pOcclusionGrid == nullptr ) {
        return false;
    }
    occlusionTested++;
    if ( !pOcclusionGrid->isHidden( boxMin, boxMax ) ) {
        return false;
    }
    occlusionCulled++;
    return true;
}

bool EdenRenderer::isSphereVisible( const Vector3f &center, float radius ) const {
    static const bool bNoCull = CmdLine::HasArg( "-noobjcull" );
    if ( bNoCull ) {
        return true;
    }
    for ( const Vector4f &p : frustumPlanes ) {
        if ( p.x * center.x + p.y * center.y + p.z * center.z + p.w < -radius ) {
            return false;
        }
    }
    return true;
}

void EdenRenderer::resumeScene() {
    // D3D clip space (y up): flipped viewport (Vulkan clip space is y down).
    const float width = static_cast<float>( scene.Width );
    const float height = static_cast<float>( scene.Height );
    static const bool bNoFlip = CmdLine::HasArg( "-edennoflip" );
    VkViewport viewport = { 0.0f, bNoFlip ? 0.0f : height, width, bNoFlip ? height : -height, 0.0f, 1.0f };
    pDevice->setViewport( viewport );
}

void EdenRenderer::flushWater() {
    for ( const DeferredDraw &draw : waterDraws ) {
        recordDraw( draw.Mesh, *draw.pMaterial, draw.World, draw.pParams.get() );
    }
    waterDraws.clear();
}

void EdenRenderer::flushDeferred() {
    flushWater();
    std::sort( deferredDraws.begin(), deferredDraws.end(),
               []( const DeferredDraw &a, const DeferredDraw &b ) { return a.Distance > b.Distance; } );
    for ( const DeferredDraw &draw : deferredDraws ) {
        recordDraw( draw.Mesh, *draw.pMaterial, draw.World, draw.pParams.get() );
    }
    deferredDraws.clear();
}

void EdenRenderer::endScene() {
    if ( !bInScene ) {
        return;
    }
    flushWater();
    std::sort( deferredDraws.begin(), deferredDraws.end(),
               []( const DeferredDraw &a, const DeferredDraw &b ) { return a.Distance > b.Distance; } );
    for ( const DeferredDraw &draw : deferredDraws ) {
        recordDraw( draw.Mesh, *draw.pMaterial, draw.World, draw.pParams.get() );
    }
    deferredDraws.clear();
    if ( CmdLine::GetArgInt( "-drawfrom", -1 ) >= 0 ) {
        static uint64_t epochLogged = 0;
        if ( sceneEpoch != epochLogged ) {
            epochLogged = sceneEpoch;
            OTDU_LOG_INFO( "pass %llu: %u draw calls\n", static_cast<unsigned long long>( sceneEpoch ),
                           drawWindowIndex );
        }
    }

    const float width = static_cast<float>( scene.Width );
    const float height = static_cast<float>( scene.Height );
    VkViewport viewport = { 0.0f, 0.0f, width, height, 0.0f, 1.0f };
    pDevice->setViewport( viewport );
    bInScene = false;
}

void EdenRenderer::draw( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
                         const EdenDrawParams *pParams ) {
    if ( !bReady || !bInScene || material.pProgram == nullptr || mesh.IndexCount == 0 ) {
        return;
    }
    if ( bProbeRecording ) {
        ProbeDraw &probe = probeDraws.emplace_back();
        probe.Min = probeMin;
        probe.Max = probeMax;
        probe.Kind = probeKind;
        probeKind = 0; // a box covers the draws right after its test only
    }
    // -drawfrom N -drawcount M: draw only the Nth..N+Mth draw calls of the
    // MAIN opaque pass (its first draw is the sky dome 'td_dome'; gating the
    // depth prepass too would break the depth test for everything the
    // prepass skipped). -logdrawwindow names the draws of the window.
    static const int32_t drawFrom = CmdLine::GetArgInt( "-drawfrom", -1 );
    static const int32_t drawCount = CmdLine::GetArgInt( "-drawcount", -1 );
    static const bool bDrawWindowLog = CmdLine::HasArg( "-logdrawwindow" );
    static bool bWindowPass = false;
    if ( drawFrom >= 0 && drawCount >= 0 ) {
        const uint32_t index = drawWindowIndex++;
        if ( index == 0 ) {
            bWindowPass = material.pProgram != nullptr &&
                          material.pProgram->Name.rfind( "td_dome", 0 ) == 0;
        }
        if ( !bWindowPass || index < static_cast<uint32_t>( drawFrom ) ||
             index >= static_cast<uint32_t>( drawFrom + drawCount ) ) {
            return;
        }
        if ( bWindowPass && bDrawWindowLog && index < static_cast<uint32_t>( drawFrom ) + 5 ) {
            OTDU_LOG_INFO( "pass %llu draw #%u: '%s' (%s) idx %u blend %u/%u\n",
                           static_cast<unsigned long long>( sceneEpoch ), index, material.Name.c_str(),
                           material.pProgram->Name.c_str(), mesh.IndexCount, material.SrcBlend,
                           material.DstBlend );
        }
    }
    // -logmaterials: one line per distinct material drawn (name, permutation,
    // index count, blend, slots and their textures). A "NONE" texture is a
    // missing one — the usual cause of a surface that renders solid black.
    static const bool bLogMaterials = CmdLine::HasArg( "-logmaterials" );
    if ( bLogMaterials ) {
        static std::unordered_map<std::string, int> loggedMaterials;
        char materialKey[320];
        snprintf( materialKey, sizeof( materialKey ), "%s|%s|%u|%u|%u|%u|%u", material.Name.c_str(),
                  material.pProgram->Name.c_str(), mesh.IndexCount, material.NumSlots, material.SrcBlend,
                  material.DstBlend, static_cast<uint32_t>( material.Bucket ) );
        if ( loggedMaterials.emplace( materialKey, 1 ).second ) {
            std::string textures;
            for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
                textures += material.pTextures[i] != nullptr ? material.pTextures[i]->Name : "NONE";
                if ( material.SamplerIndex[i] != SH_LinearWrap ) {
                    // the slot's sampler (SH_LinearUV + 3 * u + v: 0 wrap, 1 mirror, 2 clamp)
                    textures += "@s" + std::to_string( material.SamplerIndex[i] );
                }
                textures += " ";
            }
            OTDU_LOG_INFO(
                "Eden material '%s' (%s) guid %016llx idx %u slots %u nbtex(2dm) %u blend %u/%u cull %u bucket 0x%x: %s\n",
                material.Name.c_str(), material.pProgram->Name.c_str(),
                static_cast<unsigned long long>( material.ShaderSetGUID ), mesh.IndexCount, material.NumSlots,
                static_cast<uint32_t>( material.Spec[EdenShaderSystem::S_NBTEXTURES] ),
                material.SrcBlend, material.DstBlend, material.CullMode, static_cast<uint32_t>( material.Bucket ),
                textures.c_str() );
        }
    }
    // Street lamps: the lamp head's glow mesh (TD_Lamp, material
    // 'SF_Light_Lamp', texture sf_light_c) and the additive light_fx sprite at
    // it ('FX_LIGHT#1') are unlit quads - they glow whatever the time of day.
    // The retail lamp system switches them with the street lamps (the lights of
    // fx/streetlamps.xml, on from dusk), which the port does not have, so the
    // weather's hour does it here (EdenSceneView::drawObjects, the same rule the
    // car headlights use). Two glows at one lamp post (the head *and* the
    // sprite) also read as two light sources: the sprite is dropped by default,
    // -lampflares brings it back. -lampday keeps the lamps lit for an A/B.
    static const bool bLampDay = CmdLine::HasArg( "-lampday" );
    static const bool bLampFlares = CmdLine::HasArg( "-lampflares" );
    if ( !bLampDay ) {
        const bool bLampHead = material.ShaderSetGUID == 0xc913741cbe17490dull; // TD_Lamp
        const bool bLampFlare = material.Name.rfind( "FX_LIGHT", 0 ) == 0;      // TD_Sprite, light_fx
        const bool bSkipHead = bLampHead && !bNightLights;
        const bool bSkipFlare = bLampFlare && !bLampFlares;
        if ( bSkipHead || bSkipFlare ) {
            static std::unordered_set<std::string> skippedLamps;
            if ( skippedLamps.emplace( material.Name ).second ) {
                OTDU_LOG_INFO( "Eden: street lamp %s '%s' skipped (%s)\n",
                               bSkipHead ? "glow" : "sprite", material.Name.c_str(),
                               bSkipHead ? "day: the weather hour is 6..20" : "the double light source, -lampflares");
            }
            return;
        }
    }
    // TD_ShadowCaster_Only (ac78df32bc4589df, handler 0x00d70f30 → bucket 0x38)
    // and TD_ShadowCaster (…ff) are the *sector shadow map* casters: the
    // original never draws them in the visible scene (bucket 0x38 is not a
    // render layer — only the shadow pass uses them). Drawn here they show up
    // as solid black quads: the two "<sector>Shadow_tunn" meshes of the intro
    // party scene. The caster pass builds its own TD_ShadowCaster material
    // (getCasterMaterial) and goes through bShadowCaster, so only the scene
    // draws are skipped. -shadowcasters draws them anyway.
    static const bool bDrawShadowCasterMaterials = CmdLine::HasArg( "-shadowcasters" );
    if ( !bDrawShadowCasterMaterials && !bShadowCaster &&
         ( material.ShaderSetGUID == 0xac78df32bc4589dfull || material.ShaderSetGUID == 0xac78df32bc4589ffull ) ) {
        // Once per material name (the intro house has two of them, drawn by
        // every pass: remembering only the last name logged every draw).
        static std::unordered_set<std::string> skippedCasters;
        if ( skippedCasters.emplace( material.Name ).second ) {
            OTDU_LOG_INFO( "Eden: shadow caster material '%s' skipped in the scene (sector shadow map only, "
                           "-shadowcasters to draw)\n",
                           material.Name.c_str() );
        }
        return;
    }
    // TD_ShadowBox (projected AO boxes / light beams, handler 0x00d70ea0): its
    // pixel shader projects the scene position through the box constants
    // (Var_Scene_PS) that the original's shadow box system writes every frame.
    // That system is not implemented, so the shader runs on zeros and the
    // ZERO/SRCCOLOR (multiply) blend paints solid black quads — seen under the
    // DJ decks of the intro party. Skipped until it is decoded;
    // -shadowboxes draws them anyway.
    static const bool bShadowBoxes = CmdLine::HasArg( "-shadowboxes" );
    if ( !bShadowBoxes && material.ShaderSetGUID == 0x132435465768798aull ) {
        static std::unordered_set<std::string> skippedBoxes;
        if ( skippedBoxes.emplace( material.Name ).second ) {
            OTDU_LOG_INFO( "Eden: TD_ShadowBox '%s' skipped (projection constants not implemented, "
                           "-shadowboxes to draw)\n",
                           material.Name.c_str() );
        }
        return;
    }
    if ( bReflectionPass && ( material.isBlended() || material.ShaderSetGUID == 0x0cea000000000002ull ) ) {
        return;
    }
    if ( bShadowCaster ) {
        // Blended materials cast only when alpha tested (vegetation leaves:
        // their S_PASS_SHADOWCAST permutation texkills).
        if ( material.isBlended() && !material.bAlphaTest ) {
            return;
        }
        // Water surfaces (TD_Water 0x19 / 0x2e, TD_WaterRiver 0x17 / 0x18) do
        // not cast: drawn as casters, the pool shadowed itself (solid black up
        // to the first split, acne beyond, the split line sweeping over the
        // water at every camera move) [INF: the original's caster layers are
        // not decoded; a water surface casting is not plausible].
        if ( material.Bucket >= 0x17 && material.Bucket <= 0x19 || material.Bucket == 0x2e ) {
            return;
        }
        const EdenMaterial *pCaster = getCasterMaterial( material );
        if ( bLogMaterials && pCaster != nullptr && pCaster->pProgram != nullptr ) {
            // -logmaterials: which caster permutation each material gets (the
            // avatars must get the skinned TD_ShadowCaster vs7/vs8).
            static std::unordered_map<std::string, int> loggedCasters;
            const std::string casterKey = material.Name + " -> " + pCaster->pProgram->Name;
            if ( loggedCasters.emplace( casterKey, 1 ).second ) {
                OTDU_LOG_INFO( "Eden caster '%s' -> %s cull %u -> %u\n", material.Name.c_str(),
                               pCaster->pProgram->Name.c_str(), material.CullMode, pCaster->CullMode );
            }
        }
        if ( pCaster != nullptr && pCaster->pProgram != nullptr ) {
            recordDraw( mesh, *pCaster, world, pParams );
        }
        return;
    }
    if ( bDepthOnly ) {
        // Z pre-pass (ZPREPASS layer): drawn with the materials' S_PASS_Z
        // permutations (TD_Road ps_100, TD_Building ps_544...: depth write,
        // no blending); families without one: opaque and alpha tested draws.
        if ( const EdenMaterial *pZPass = getZPassMaterial( material ) ) {
            recordDraw( mesh, *pZPass, world, pParams );
            return;
        }
        if ( material.isBlended() && !material.bAlphaTest ) {
            return;
        }
        recordDraw( mesh, material, world, pParams );
        return;
    }
    // Water buckets (TD_Water 0x19 / 0x2e, TD_WaterRiver 0x17 / 0x18) come
    // after the opaque, avatar and building buckets in the layer order
    // [CODE 0x00d70cf0 + layer table 0x00f7f604]: the opaque water draws are
    // kept until the blended draws are flushed (and recorded first).
    // The sea (TD_Ocean) joins that bucket **whether or not it blends**: since
    // the water clarity made it a blended material it used to fall into the
    // distance sorted blended flush, where the shore foam (TD_backwash
    // `Ecume`, also blended) and the water are at the same distance and swap
    // order as the camera moves - the water then paints over the foam and the
    // surf pops in and out. Fixed order: opaque sea bed, then the water, then
    // the foam and every other blended draw.
    constexpr uint64_t kOceanShaderSet = 0x0cea000000000002ull;
    const bool bOcean = material.ShaderSetGUID == kOceanShaderSet;
    const bool bLateWater = !material.isBlended() && material.Bucket >= 0x17 && material.Bucket <= 0x19;
    const bool bLateWaterAlpha = !material.isBlended() && material.Bucket == 0x2e;
    if ( bOcean || bLateWater || bLateWaterAlpha ||
         ( material.isBlended() && ( pParams == nullptr || pParams->bDeferBlended ) ) ) {
        DeferredDraw deferred;
        if ( pParams != nullptr ) {
            deferred.pParams = std::make_shared<EdenDrawParams>( *pParams );
            if ( pParams->pSkinMatrices != nullptr && pParams->NumSkinMatrices > 0 ) {
                deferred.pParams->SkinStorage.assign( pParams->pSkinMatrices,
                                                      pParams->pSkinMatrices + pParams->NumSkinMatrices );
                deferred.pParams->pSkinMatrices = deferred.pParams->SkinStorage.data();
            }
        }
        deferred.Mesh = mesh;
        deferred.pMaterial = &material;
        deferred.World = world;
        const Vector3f position = world.getTranslation();
        deferred.Distance = ( position - scene.CameraPosition ).length();
        if ( bOcean || bLateWater || bLateWaterAlpha ) {
            waterDraws.push_back( deferred );
        } else {
            deferredDraws.push_back( deferred );
        }
        return;
    }
    recordDraw( mesh, material, world, pParams );
}

VkPipeline EdenRenderer::getPipeline( const EdenMesh &mesh, const EdenMaterial &material, bool bMirrored ) {
    const EdenProgram *pProgram = material.pProgram;
    static const bool bSwapCull = CmdLine::HasArg( "-edenswapcull" );
    static const bool bNoCull = CmdLine::HasArg( "-nocull" );
    uint8_t cullMode = bNoCull ? 1 : material.CullMode;
    // Eden cull state 3 = two-sided (data: *_dblS meshes, eyelashes, car
    // glass, the DJ shirt; spec_skin_zones 3.1).
    if ( cullMode == 3 ) {
        cullMode = 1;
    }
    // Mirroring world matrix (negative determinant): D3D culls in screen
    // space, the winding of the mesh flips.
    if ( bMirrored && ( cullMode == 2 || cullMode == 4 ) ) {
        cullMode = cullMode == 2 ? 4 : 2;
    }
    const uint8_t writeMask = bDepthOnly ? 0 : material.ColorWriteMask;
    PipelineKey fullKey;
    fullKey.Words[0] = reinterpret_cast<uintptr_t>( pDevice->getCurrentRenderPass() );
    fullKey.Words[1] = reinterpret_cast<uintptr_t>( pProgram->pVS );
    fullKey.Words[2] = reinterpret_cast<uintptr_t>( pProgram->pPS );
    fullKey.Words[3] = mesh.LayoutKey;
    fullKey.Words[4] = static_cast<uint64_t>( material.SrcBlend ) | static_cast<uint64_t>( material.DstBlend ) << 8 |
                       static_cast<uint64_t>( cullMode ) << 16 | static_cast<uint64_t>( material.bDepthTest ? 1 : 0 ) << 24 |
                       static_cast<uint64_t>( material.bDepthWrite ? 1 : 0 ) << 32 |
                       static_cast<uint64_t>( material.isBlended() ? 1 : 0 ) << 40 |
                       static_cast<uint64_t>( material.BlendOp ) << 48;
    fullKey.Words[5] = static_cast<uint64_t>( writeMask ) | static_cast<uint64_t>( material.DepthFunc ) << 8 |
                       static_cast<uint64_t>( material.blendFactorByte() ) << 16;
    // One-entry memo: consecutive draws mostly ask for the same pipeline, and
    // comparing six words is cheaper than hashing them (~2 % of the main
    // thread at 4K, -sampleprof).
    if ( lastPipeline != VK_NULL_HANDLE && lastPipelineKey == fullKey ) {
        return lastPipeline;
    }
    auto it = pipelines.find( fullKey );
    if ( it != pipelines.end() ) {
        lastPipelineKey = fullKey;
        lastPipeline = it->second;
        return it->second;
    }

    VkDevice device = pDevice->getDevice();
    auto createModule = [&]( const std::vector<uint32_t> &code ) {
        VkShaderModuleCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size() * sizeof( uint32_t );
        info.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        vkCreateShaderModule( device, &info, nullptr, &module );
        return module;
    };
    VkShaderModule vertexModule = createModule( pProgram->pVS->SPIRV );
    VkShaderModule fragmentModule = createModule( pProgram->pPS->SPIRV );

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    // Vertex input: match the VS dcl semantics with the original streams.
    std::vector<VkVertexInputBindingDescription> bindings;
    for ( uint32_t i = 0; i < mesh.NumStreams; i++ ) {
        bindings.push_back( { i, mesh.Strides[i], VK_VERTEX_INPUT_RATE_VERTEX } );
    }
    bool bUsesDefaults = false;
    std::vector<VkVertexInputAttributeDescription> attributes;
    for ( const ShaderInputInfo &input : pProgram->pVS->Inputs ) {
        const RawVertexAttribute *pMatch = nullptr;
        for ( const RawVertexAttribute &attribute : mesh.Attributes ) {
            if ( attribute.Usage == input.Usage && attribute.UsageIndex == input.UsageIndex ) {
                pMatch = &attribute;
                break;
            }
        }
        const VkFormat format = pMatch != nullptr ? GetAttributeFormat( pMatch->Format ) : VK_FORMAT_UNDEFINED;
        if ( pMatch != nullptr && format != VK_FORMAT_UNDEFINED && pMatch->Stream < mesh.NumStreams ) {
            attributes.push_back( { input.Location, pMatch->Stream, format, pMatch->Offset } );
            continue;
        }
        uint32_t offset = 0;
        if ( input.Usage == 10 ) { // COLOR
            offset = 16;
        } else if ( input.Usage == 3 ) { // NORMAL
            offset = 32;
        } else if ( input.Usage == 6 || input.Usage == 7 ) { // TANGENT, BINORMAL
            offset = 48;
        }
        attributes.push_back( { input.Location, kDefaultAttributeBinding, VK_FORMAT_R32G32B32A32_SFLOAT, offset } );
        bUsesDefaults = true;
    }
    if ( bUsesDefaults ) {
        while ( bindings.size() < kDefaultAttributeBinding ) {
            bindings.push_back( { static_cast<uint32_t>( bindings.size() ), 4, VK_VERTEX_INPUT_RATE_VERTEX } );
        }
        bindings.push_back( { kDefaultAttributeBinding, 0, VK_VERTEX_INPUT_RATE_VERTEX } );
    }

    if ( Logger::IsVerbose() ) {
        std::string description;
        for ( const ShaderInputInfo &input : pProgram->pVS->Inputs ) {
            description += "[u" + std::to_string( input.Usage ) + "." + std::to_string( input.UsageIndex ) + "@" +
                           std::to_string( input.Location ) + "]";
        }
        for ( const VkVertexInputAttributeDescription &attribute : attributes ) {
            description += " loc" + std::to_string( attribute.location ) + "=b" + std::to_string( attribute.binding ) +
                           "+" + std::to_string( attribute.offset ) + "/f" + std::to_string( attribute.format );
        }
        OTDU_LOG_DEBUG( "Eden pipeline %s (%s): %s blend %u/%u op %u at %d zw %d mask %x\n", pProgram->Name.c_str(),
                        material.Name.c_str(), description.c_str(), material.SrcBlend, material.DstBlend,
                        material.BlendOp, material.bAlphaTest ? 1 : 0, material.bDepthWrite ? 1 : 0, writeMask );
    }

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>( bindings.size() );
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>( attributes.size() );
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = mesh.Topology;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // D3D9: front faces are clockwise on screen, D3DCULL_CCW (3) culls the
    // back faces, D3DCULL_CW (2) the front faces. With the flipped viewport,
    // the D3D clockwise triangles are counter clockwise for Vulkan.
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    // D3D: clockwise front faces (the flipped viewport keeps the screen
    // space winding of D3D).
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    if ( cullMode == 4 ) {
        rasterizer.cullMode = bSwapCull ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
    } else if ( cullMode == 2 ) {
        rasterizer.cullMode = bSwapCull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
    }
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = material.bDepthTest ? VK_TRUE : VK_FALSE;
    // D3DRS_ZWRITEENABLE of the material, blended or not.
    depthStencil.depthWriteEnable = material.bDepthWrite ? VK_TRUE : VK_FALSE;
    // D3DCMPFUNC (MAT. render state +0x3b: TD_ShadowBox uses GREATEREQUAL)
    static constexpr VkCompareOp kCompareOps[9] = { VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_NEVER,
                                                    VK_COMPARE_OP_LESS,          VK_COMPARE_OP_EQUAL,
                                                    VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER,
                                                    VK_COMPARE_OP_NOT_EQUAL,     VK_COMPARE_OP_GREATER_OR_EQUAL,
                                                    VK_COMPARE_OP_ALWAYS };
    depthStencil.depthCompareOp = kCompareOps[material.DepthFunc <= 8 ? material.DepthFunc : 0];

    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.colorWriteMask = ( ( writeMask & 1 ) ? VK_COLOR_COMPONENT_R_BIT : 0 ) |
                                     ( ( writeMask & 2 ) ? VK_COLOR_COMPONENT_G_BIT : 0 ) |
                                     ( ( writeMask & 4 ) ? VK_COLOR_COMPONENT_B_BIT : 0 ) |
                                     ( ( writeMask & 8 ) ? VK_COLOR_COMPONENT_A_BIT : 0 );
    if ( material.isBlended() ) {
        static constexpr VkBlendOp kBlendOps[6] = { VK_BLEND_OP_ADD, VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT,
                                                    VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX };
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = GetBlendFactor( material.SrcBlend );
        blendAttachment.dstColorBlendFactor = GetBlendFactor( material.DstBlend );
        blendAttachment.colorBlendOp = kBlendOps[material.BlendOp <= 5 ? material.BlendOp : 1];
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo colorBlend = {};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    // D3DBLENDFACTOR / D3DBLEND_INVBLENDFACTOR (14 / 15): the constant alpha
    const float blendFactor = static_cast<float>( material.blendFactorByte() ) / 255.0f;
    colorBlend.blendConstants[0] = blendFactor;
    colorBlend.blendConstants[1] = blendFactor;
    colorBlend.blendConstants[2] = blendFactor;
    colorBlend.blendConstants[3] = blendFactor;

    VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass =
        pDevice->getCurrentRenderPass() != VK_NULL_HANDLE ? pDevice->getCurrentRenderPass() : pDevice->getRenderPass();

    VkPipeline pipeline = VK_NULL_HANDLE;
    if ( vkCreateGraphicsPipelines( device, pDevice->getPipelineCache(), 1, &pipelineInfo, nullptr, &pipeline ) !=
         VK_SUCCESS ) {
        OTDU_LOG_WARN( "Eden: pipeline creation failed for %s\n", pProgram->Name.c_str() );
        pipeline = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule( device, vertexModule, nullptr );
    vkDestroyShaderModule( device, fragmentModule, nullptr );
    pipelines[fullKey] = pipeline;
    return pipeline;
}

// Names fillConstants answers from the scene / draw state (the chain in it).
// Everything else is a material parameter (.2DM); the answer is cached per
// shader member since the chain is long and runs for every constant of every draw.
static bool IsFixedConstantName( const std::string &name ) {
    static const std::unordered_set<std::string> kExact = {
        "__World_View_Proj__float4x4", "__World__float4x4", "LastWorld__float4x4", "__World_View__float4x4",
        "__Inv_World__float4x4", "__Inv_World_View__float4x4", "__T_Inv_World_View__float4x4", "View", "LastView",
        "Proj", "ViewProj", "InvView", "InvProj", "InvViewProj", "__Skin_Matrix__array96", "__CamPos_ObjSpace",
        "__CamDir_ObjSpace", "__Material_Emissive", "__Material_Specular", "__Instance_Color", "__Alpha_Ref",
        "__Lod_Dist", "Time_", "FogColor", "FogConfig", "RenderTargetSize", "StereoUvMadd" };
    if ( kExact.count( name ) != 0 ) {
        return true;
    }
    static const char *const kPrefixes[] = { "__UVA_", "__SpotLight_", "__AmbLight_", "__DirLight_", "Shadow_", "UserVector" };
    for ( const char *pPrefix : kPrefixes ) {
        if ( name.rfind( pPrefix, 0 ) == 0 ) {
            return true;
        }
    }
    return false;
}

namespace {
// What fillConstants writes for a member, from its name alone. Classified
// once per shader member (ShaderConstantMember::Kind / AvatarKind) so the
// per draw loop switches on an enum instead of comparing strings.
enum eConstantKind : uint8_t {
    CK_Material, // .2DM parameter
    CK_None,     // a fixed name that receives nothing (second lights, shadows: disabled)
    CK_WorldViewProj, CK_World, CK_WorldView, CK_InvWorld, CK_InvWorldView, CK_TInvWorldView,
    CK_View, CK_Proj, CK_ViewProj, CK_InvView, CK_InvProj, CK_InvViewProj, CK_UVA, CK_Skin,
    CK_CamPos, CK_CamDir, CK_DirLightDir0, CK_DirLightDir1, CK_DirLightDiffuse0, CK_DirLightRawDiffuse0,
    CK_DirLightSpecular0, CK_Emissive, CK_Ambient0, CK_RawAmbient0, CK_MaterialSpecular, CK_InstanceColor,
    CK_AlphaRef, CK_LodDist, CK_UserVector, CK_Time, CK_FogColor, CK_FogConfig, CK_RenderTargetSize,
    CK_StereoUvMadd,
};
enum eAvatarConstantKind : uint8_t {
    AK_None, AK_DirLightDir0, AK_Ambient0, AK_Diffuse0, AK_Specular0, AK_SpotPos, AK_SpotAmbient,
    AK_SpotDiffuse, AK_SpotSpecular, AK_CarInteg, AK_Attefact, AK_ShadowAttenuation,
};

uint8_t ClassifyConstant( const std::string &name, bool bFixed ) {
    if ( !bFixed ) {
        return CK_Material;
    }
    static const std::unordered_map<std::string, uint8_t> kNames = {
        { "__World_View_Proj__float4x4", CK_WorldViewProj }, { "__World__float4x4", CK_World },
        { "LastWorld__float4x4", CK_World }, { "__World_View__float4x4", CK_WorldView },
        { "__Inv_World__float4x4", CK_InvWorld }, { "__Inv_World_View__float4x4", CK_InvWorldView },
        { "__T_Inv_World_View__float4x4", CK_TInvWorldView }, { "View", CK_View }, { "LastView", CK_View },
        { "Proj", CK_Proj }, { "ViewProj", CK_ViewProj }, { "InvView", CK_InvView }, { "InvProj", CK_InvProj },
        { "InvViewProj", CK_InvViewProj }, { "__Skin_Matrix__array96", CK_Skin }, { "__CamPos_ObjSpace", CK_CamPos },
        { "__CamDir_ObjSpace", CK_CamDir }, { "__DirLight_Dir_0", CK_DirLightDir0 }, { "__DirLight_Dir_1", CK_DirLightDir1 },
        { "__DirLight_Diffuse_0", CK_DirLightDiffuse0 }, { "__DirLight_Diffuse_NoInstance_0", CK_DirLightDiffuse0 },
        { "__DirLight_RawDiffuse_0", CK_DirLightRawDiffuse0 }, { "__DirLight_Specular_0", CK_DirLightSpecular0 },
        { "__DirLight_Specular_NoInstance_0", CK_DirLightSpecular0 }, { "__DirLight_Emissive_0", CK_Emissive },
        { "__Material_Emissive", CK_Emissive }, { "__AmbLight_Ambient_0", CK_Ambient0 },
        { "__AmbLight_RawAmbient_0", CK_RawAmbient0 }, { "__Material_Specular", CK_MaterialSpecular },
        { "__Instance_Color", CK_InstanceColor }, { "__Alpha_Ref", CK_AlphaRef }, { "__Lod_Dist", CK_LodDist },
        { "Time_", CK_Time }, { "FogColor", CK_FogColor }, { "FogConfig", CK_FogConfig },
        { "RenderTargetSize", CK_RenderTargetSize }, { "StereoUvMadd", CK_StereoUvMadd },
    };
    auto it = kNames.find( name );
    if ( it != kNames.end() ) {
        return it->second;
    }
    if ( name.rfind( "__UVA_", 0 ) == 0 ) {
        return CK_UVA;
    }
    if ( name.rfind( "UserVector", 0 ) == 0 ) {
        return CK_UserVector;
    }
    return CK_None; // the other __SpotLight_ / __AmbLight_ / __DirLight_ / Shadow_ names
}

uint8_t ClassifyAvatarConstant( const std::string &name ) {
    static const std::unordered_map<std::string, uint8_t> kNames = {
        { "__DirLight_Dir_0", AK_DirLightDir0 }, { "__AmbLight_Ambient_0", AK_Ambient0 },
        { "__DirLight_Diffuse_0", AK_Diffuse0 }, { "__DirLight_Diffuse_NoInstance_0", AK_Diffuse0 },
        { "__DirLight_Specular_0", AK_Specular0 }, { "__DirLight_Specular_NoInstance_0", AK_Specular0 },
        { "__SpotLight_Pos_0", AK_SpotPos }, { "__SpotLight_Ambient_0", AK_SpotAmbient },
        { "__SpotLight_Diffuse_0", AK_SpotDiffuse }, { "__SpotLight_Specular_0", AK_SpotSpecular },
    };
    auto it = kNames.find( name );
    if ( it != kNames.end() ) {
        return it->second;
    }
    const std::string base = ToUpper( StripSuffix( name ) );
    if ( base == "CARINTEG" ) {
        return AK_CarInteg;
    }
    if ( base == "ATTEFACT" ) {
        return AK_Attefact;
    }
    if ( base == "SHADOWATTENUATION" ) {
        return AK_ShadowAttenuation;
    }
    return AK_None;
}
} // namespace

namespace {
// fillConstants looks every shader constant name up in the draw's small
// override maps (a TD_Ground instance carries one "sectrOff" vector): hashing
// the name for each member was ~8 % of the main thread at 4K (-sampleprof).
// A map of a few entries is scanned instead - the string compare rejects on
// the length or the first bytes.
template <typename Map>
typename Map::const_iterator FindSmall( const Map &map, const std::string &key ) {
    if ( map.empty() ) {
        return map.end();
    }
    if ( map.size() <= 8 ) {
        for ( auto it = map.begin(); it != map.end(); ++it ) {
            if ( it->first == key ) {
                return it;
            }
        }
        return map.end();
    }
    return map.find( key );
}
} // namespace

void EdenRenderer::fillConstants( const std::vector<ShaderConstantMember> &members, float *pRegisters,
                                  uint32_t numRegisters, const EdenMaterial &material, const Matrix4x4f &world,
                                  const EdenDrawParams *pParams, const char *pProgramName, bool bPixel ) {
    Matrix4x4f inverseWorld;
    bool bInverseWorld = false;
    auto getInverseWorld = [&]() -> const Matrix4x4f & {
        if ( !bInverseWorld ) {
            inverseWorld = world.inverse();
            bInverseWorld = true;
        }
        return inverseWorld;
    };

    for ( const ShaderConstantMember &member : members ) {
        if ( member.RegisterSet != 2 || member.Register + member.NumRegisters > numRegisters ) {
            continue;
        }
        float *pOut = pRegisters + member.Register * 4;
        const std::string &name = member.Name;

        // Stage specific overrides (the same name in the VS and the PS:
        // TD_Ocean / TD_ocean_nmap_from_hmap "matParam").
        {
            const auto &overrides = material.StageConstants[bPixel ? 1 : 0];
            auto it = FindSmall( overrides, name );
            if ( it != overrides.end() ) {
                for ( uint32_t i = 0; i < member.NumRegisters && i < it->second.size(); i++ ) {
                    WriteVector( pOut + i * 4, it->second[i] );
                }
                continue;
            }
        }
        if ( pParams != nullptr ) {
            const auto &stageVectors = bPixel ? pParams->PixelVectors : pParams->VertexVectors;
            auto itStage = FindSmall( stageVectors, name );
            if ( itStage != stageVectors.end() ) {
                WriteVector( pOut, itStage->second );
                continue;
            }
            auto it = FindSmall( pParams->Vectors, name );
            if ( it != pParams->Vectors.end() ) {
                WriteVector( pOut, it->second );
                continue;
            }
            auto itMatrix = FindSmall( pParams->Matrices, name );
            if ( itMatrix != pParams->Matrices.end() ) {
                WriteMatrix( pOut, member.NumRegisters, member.Class, itMatrix->second );
                continue;
            }
            auto itArray = FindSmall( pParams->Arrays, name );
            if ( itArray != pParams->Arrays.end() ) {
                for ( uint32_t i = 0; i < member.NumRegisters && i < itArray->second.size(); i++ ) {
                    WriteVector( pOut + i * 4, itArray->second[i] );
                }
                continue;
            }
        }
        if ( member.AvatarKind == 0xFF ) {
            member.AvatarKind = ClassifyAvatarConstant( name );
        }
        if ( pParams != nullptr && pParams->pAvatarLighting != nullptr && member.AvatarKind != AK_None ) {
            // Avatar light set (instance ambient / directional / spot lights)
            // and the CARINTEG / ATTEFACT symbols written by FUN_00caf850 for
            // every avatar family except TD_Avatar (it keeps its 2DM values).
            const EdenAvatarLighting &light = *pParams->pAvatarLighting;
            const uint64_t family = material.ShaderSetGUID;
            const bool bTdAvatar = family == 0xd5013ed0db79dadbull;
            const bool bSymbols = !bTdAvatar && ( family == 0xd5013ed0db79bbbbull || family == 0xd5013ed0dbcdcdcdull ||
                                                  family == 0xd5013ed0dbcddbcaull || family == 0xd5013ed0db79aeaeull ||
                                                  family == 0xd5013ed0cacadacaull || family == 0xd5013ed0dba9dacaull ||
                                                  family == 0xd5013ed0dbdededeull || family == 0x413ab646426c5358ull );
            bool bHandled = true;
            switch ( member.AvatarKind ) {
            case AK_DirLightDir0:
                WriteVector( pOut, getInverseWorld().transformVector( light.Direction ).normalized() );
                break;
            case AK_Ambient0:
                WriteVector( pOut, Mul( light.Ambient, material.Ambient ), 1.0f );
                break;
            case AK_Diffuse0:
                WriteVector( pOut, Mul( light.Diffuse, material.Diffuse ), 1.0f );
                break;
            case AK_Specular0:
                WriteVector( pOut, Mul( light.Specular, material.Specular ), 1.0f );
                break;
            case AK_SpotPos:
                WriteVector( pOut, getInverseWorld().transformPoint( light.SpotPosition ), 1.0f );
                break;
            case AK_SpotAmbient:
                // Material colour multiply of the spot light [INF: binder ENC].
                WriteVector( pOut, Mul( light.SpotAmbient, material.Ambient ), 1.0f );
                break;
            case AK_SpotDiffuse:
                WriteVector( pOut, Mul( light.SpotDiffuse, material.Diffuse ), 1.0f );
                break;
            case AK_SpotSpecular:
                WriteVector( pOut, Mul( light.SpotSpecular, material.Specular ), 1.0f );
                break;
            case AK_CarInteg:
                // (p1, p2, shadow, 1) with AVATAROUTCARLIGHTFACTOR p1 = p2 = 0:
                // xy = (0, 1) gives the same k = 1 without the 0 * inf of
                // D3D9 (NaN in SPIR-V).
                bHandled = bSymbols;
                if ( bHandled ) {
                    WriteVector( pOut, Vector4f( 0.0f, 1.0f, light.Shadow, 1.0f ) );
                }
                break;
            case AK_Attefact:
                bHandled = bSymbols;
                if ( bHandled ) {
                    WriteVector( pOut, Vector4f( 1.0f, 1.0f, 1.0f, light.Environment ) );
                }
                break;
            case AK_ShadowAttenuation:
                bHandled = family == 0xd5013ed0db79baedull;
                if ( bHandled ) {
                    WriteVector( pOut, Vector4f( light.Shadow, light.Shadow, light.Shadow, 1.0f ) );
                }
                break;
            default:
                bHandled = false;
                break;
            }
            if ( bHandled ) {
                continue;
            }
        }
        {
            if ( member.SceneEpoch != sceneEpoch ) {
                auto it = scene.Vectors.find( name );
                auto itMatrix = scene.Matrices.find( name );
                member.pSceneVector = it != scene.Vectors.end() ? &it->second : nullptr;
                member.pSceneMatrix = itMatrix != scene.Matrices.end() ? &itMatrix->second : nullptr;
                member.SceneEpoch = sceneEpoch;
            }
            if ( member.pSceneVector != nullptr ) {
                WriteVector( pOut, *static_cast<const Vector4f *>( member.pSceneVector ) );
                continue;
            }
            if ( member.pSceneMatrix != nullptr ) {
                WriteMatrix( pOut, member.NumRegisters, member.Class, *static_cast<const Matrix4x4f *>( member.pSceneMatrix ) );
                continue;
            }
        }

        if ( member.Kind == 0xFF ) {
            member.Kind = ClassifyConstant( name, IsFixedConstantName( name ) );
        }
        switch ( member.Kind ) {
        case CK_Material: {
            // Material parameter (.2DM): Eden identifier of the upper case name.
            if ( !member.bParamIdValid ) {
                member.ParamId = GetIdentifier64bit( StripSuffix( name ).c_str() );
                member.bParamIdValid = true;
            }
            const Vector4f *pOverride = nullptr;
            if ( pParams != nullptr && !pParams->MaterialVectors.empty() ) {
                for ( const EdenDrawParams::MaterialVector &entry : pParams->MaterialVectors ) {
                    if ( entry.pMaterial == &material && entry.Parameter == member.ParamId ) {
                        pOverride = &entry.Value;
                        break;
                    }
                }
            }
            auto it = material.Parameters.find( member.ParamId );
            if ( pOverride != nullptr ) {
                WriteVector( pOut, *pOverride );
            } else if ( it != material.Parameters.end() ) {
                WriteVector( pOut, it->second );
            } else if ( Logger::IsVerbose() ) {
                const std::string warnKey = std::string( pProgramName ) + "|" + name;
                if ( warnedParameters.insert( warnKey ).second ) {
                    OTDU_LOG_DEBUG( "Eden: no value for '%s' (%s, material '%s')\n", name.c_str(), pProgramName,
                                    material.Name.c_str() );
                }
            }
            break;
        }
        // Matrices
        case CK_WorldViewProj:
            WriteMatrix( pOut, member.NumRegisters, member.Class,
                         pPreciseViewProjection != nullptr
                             ? ( *pPreciseViewProjection * Matrix4x4d::FromFloat( world ) ).toFloat()
                             : viewProjection.mulPrecise( world ) );
            break;
        case CK_World:
            WriteMatrix( pOut, member.NumRegisters, member.Class, world );
            break;
        case CK_WorldView:
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.View.mulPrecise( world ) );
            break;
        case CK_InvWorld:
            WriteMatrix( pOut, member.NumRegisters, member.Class, getInverseWorld() );
            break;
        case CK_InvWorldView:
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.View.mulPrecise( world ).inverse() );
            break;
        case CK_TInvWorldView:
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.View.mulPrecise( world ).inverse().transposed() );
            break;
        case CK_View:
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.View );
            break;
        case CK_Proj:
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.Projection );
            break;
        case CK_ViewProj:
            WriteMatrix( pOut, member.NumRegisters, member.Class, viewProjection );
            break;
        case CK_InvView:
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.View.inverse() );
            break;
        case CK_InvProj:
            WriteMatrix( pOut, member.NumRegisters, member.Class, scene.Projection.inverse() );
            break;
        case CK_InvViewProj:
            WriteMatrix( pOut, member.NumRegisters, member.Class, viewProjection.inverse() );
            break;
        case CK_UVA: {
            // Texture coordinate animation of slot k (.UVA): register rows
            // (su, 0, 0, offU), (0, sv, 0, offV) [SHADER TD_Water vs 46].
            const uint32_t slot = static_cast<uint32_t>( name[6] - '0' );
            if ( slot < 4 && material.UVAnimations[slot] != nullptr ) {
                Vector4f registers[4];
                material.UVAnimations[slot]->evaluateRegisters( scene.Time, registers );
                for ( uint32_t i = 0; i < member.NumRegisters && i < 4; i++ ) {
                    WriteVector( pOut + i * 4, registers[i] );
                }
            } else {
                WriteMatrix( pOut, member.NumRegisters, member.Class, Matrix4x4f::Identity() );
            }
            break;
        }
        case CK_Skin: {
            const uint32_t numBones = member.NumRegisters / 3;
            for ( uint32_t bone = 0; bone < numBones; bone++ ) {
                const Matrix4x4f matrix = ( pParams != nullptr && bone < pParams->NumSkinMatrices )
                                              ? pParams->pSkinMatrices[bone]
                                              : Matrix4x4f::Identity();
                WriteMatrix( pOut + bone * 12, 3, 3, matrix );
            }
            break;
        }
        // Camera / lights
        case CK_CamPos:
            WriteVector( pOut, getInverseWorld().transformPoint( scene.CameraPosition ), 1.0f );
            break;
        case CK_CamDir:
            WriteVector( pOut, getInverseWorld().transformVector( scene.CameraDirection ).normalized() );
            break;
        case CK_DirLightDir0:
            WriteVector( pOut, getInverseWorld().transformVector( scene.SunDirection ).normalized() );
            break;
        case CK_DirLightDir1:
            WriteVector( pOut, getInverseWorld().transformVector( Vector3f( 0.0f, -1.0f, 0.0f ) ) );
            break;
        case CK_DirLightDiffuse0:
            WriteVector( pOut, Mul( scene.SunDiffuse, material.Diffuse ), 1.0f );
            break;
        case CK_DirLightRawDiffuse0:
            WriteVector( pOut, scene.SunDiffuse, 1.0f );
            break;
        case CK_DirLightSpecular0:
            WriteVector( pOut, Mul( scene.SunSpecular, material.Specular ), 1.0f );
            break;
        case CK_Emissive:
            WriteVector( pOut, material.Emissive );
            break;
        case CK_Ambient0:
            WriteVector( pOut, Mul( scene.Ambient, material.Ambient ), 1.0f );
            break;
        case CK_RawAmbient0:
            WriteVector( pOut, scene.Ambient, 1.0f );
            break;
        case CK_MaterialSpecular:
            WriteVector( pOut, material.Specular );
            break;
        case CK_InstanceColor:
            WriteVector( pOut, Vector4f( 1.0f, 1.0f, 1.0f, 1.0f ) );
            break;
        case CK_AlphaRef: {
            const float reference = material.AlphaRef / 255.0f;
            WriteVector( pOut, Vector4f( reference, reference, reference, reference ) );
            break;
        }
        case CK_LodDist:
            WriteVector( pOut, Vector4f( 0.0f, 1.0f, 0.0f, 1.0f ) );
            break;
        // Frame / scene
        case CK_UserVector: {
            const uint32_t index = static_cast<uint32_t>( name[10] - '0' );
            if ( index < 8 ) {
                WriteVector( pOut, scene.UserVectors[index] );
            }
            break;
        }
        case CK_Time:
            WriteVector( pOut, Vector4f( scene.Time, scene.Time * 0.5f, scene.Time * 0.25f, 1.0f ) );
            break;
        case CK_FogColor:
            WriteVector( pOut, scene.FogColor, 1.0f );
            break;
        case CK_FogConfig:
            WriteVector( pOut, scene.FogConfig );
            break;
        case CK_RenderTargetSize:
            // xy: pixel to UV (shaders sample screen space buffers with vPos * xy)
            WriteVector( pOut, Vector4f( 1.0f / scene.Width, 1.0f / scene.Height, static_cast<float>( scene.Width ),
                                         static_cast<float>( scene.Height ) ) );
            break;
        case CK_StereoUvMadd:
            WriteVector( pOut, Vector4f( 1.0f, 1.0f, 0.0f, 0.0f ) );
            break;
        default:
            break; // second lights, spot lights, real time shadows: disabled
        }
    }
}

void EdenRenderer::recordDraw( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
                               const EdenDrawParams *pParams ) {
    static const bool bLogRecord = CmdLine::HasArg( "-logrecordtimes" );
    if ( bLogRecord ) {
        const auto start = std::chrono::steady_clock::now();
        recordDrawImpl( mesh, material, world, pParams );
        recordMsTotal += std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
        recordCountTotal++;
        return;
    }
    recordDrawImpl( mesh, material, world, pParams );
}

void EdenRenderer::recordDrawImpl( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
                                   const EdenDrawParams *pParams ) {
    const EdenProgram *pProgram = material.pProgram;
    // A/B: -hidematerial <substr[;substr...]> skips every material whose name
    // contains one of the (case-insensitive) substrings, in every pass.
    static const std::vector<std::string> hiddenMaterials = [] {
        std::vector<std::string> list;
        if ( const char *pList = CmdLine::GetArgValue( "-hidematerial" ) ) {
            std::string all = ToUpper( pList );
            for ( size_t start = 0; start <= all.size(); ) {
                const size_t end = Min( all.find( ';', start ), all.size() );
                if ( end > start ) {
                    list.push_back( all.substr( start, end - start ) );
                }
                start = end + 1;
            }
        }
        return list;
    }();
    if ( !hiddenMaterials.empty() ) {
        const std::string name = ToUpper( material.Name );
        for ( const std::string &hidden : hiddenMaterials ) {
            if ( name.find( hidden ) != std::string::npos ) {
                return;
            }
        }
    }
    const float determinant =
        world.m[0][0] * ( world.m[1][1] * world.m[2][2] - world.m[1][2] * world.m[2][1] ) -
        world.m[0][1] * ( world.m[1][0] * world.m[2][2] - world.m[1][2] * world.m[2][0] ) +
        world.m[0][2] * ( world.m[1][0] * world.m[2][1] - world.m[1][1] * world.m[2][0] );
    VkPipeline pipeline = getPipeline( mesh, material, ( determinant < 0.0f ) != scene.bMirroredView );
    if ( pipeline == VK_NULL_HANDLE ) {
        return;
    }
    // Ray tracing: remember what the main scene actually records. The shadow
    // caster, reflection and depth only passes record the same meshes again,
    // and a mirrored view is an environment cube face or a planar reflection.
    if ( bRtCapture && !bShadowCaster && !bReflectionPass && !bDepthOnly && !scene.bMirroredView &&
         mesh.BlasAddress != 0 ) {
        if ( rtInstances.empty() ) {
            rtCameraPosition = scene.CameraPosition;
            rtCameraDirection = scene.CameraDirection;
        }
        rtInstances.push_back( RtInstance{ mesh.BlasAddress, world } );
    }
    VkDevice device = pDevice->getDevice();
    // peek: this renderer's own draws do not count as a foreign touch (the
    // bind filter below).
    VkCommandBuffer commandBuffer = pDevice->peekCommandBuffer();
    const VkDeviceSize alignment = pDevice->getUniformBufferAlignment();

    TransientAllocation vsConstants = pDevice->allocateTransient( kVSConstantsSize, alignment );
    TransientAllocation psConstants = pDevice->allocateTransient( kPSConstantsSize, alignment );
    TransientAllocation specConstants = pDevice->allocateTransient( kSpecDwords * 4, alignment );
    if ( vsConstants.pData == nullptr || psConstants.pData == nullptr || specConstants.pData == nullptr ) {
        return;
    }
    // The constant buffers live in write-combined memory: clear only what the
    // shader can read (int registers, the registers its tables name, the bool
    // bitmask) instead of all ~8 KB per draw.
    auto usedRegisters = []( const std::vector<ShaderConstantMember> &members, uint32_t limit, uint16_t &cache ) {
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
    };
    auto clearConstants = []( void *pData, VkDeviceSize totalSize, uint32_t usedRegs ) {
        uint8_t *pBytes = static_cast<uint8_t *>( pData );
        memset( pBytes, 0, kIntRegistersSize + usedRegs * 16 );
        memset( pBytes + totalSize - kBoolBitmaskSize, 0, kBoolBitmaskSize );
    };
    clearConstants( vsConstants.pData, kVSConstantsSize,
                    usedRegisters( pProgram->VSMembers, kVSFloatRegisters, pProgram->VSUsedRegisters ) );
    clearConstants( psConstants.pData, kPSConstantsSize,
                    usedRegisters( pProgram->PSMembers, kPSFloatRegisters, pProgram->PSUsedRegisters ) );
    fillConstants( pProgram->VSMembers,
                   reinterpret_cast<float *>( static_cast<uint8_t *>( vsConstants.pData ) + kIntRegistersSize ),
                   kVSFloatRegisters, material, world, pParams, pProgram->Name.c_str(), false );
    fillConstants( pProgram->PSMembers,
                   reinterpret_cast<float *>( static_cast<uint8_t *>( psConstants.pData ) + kIntRegistersSize ),
                   kPSFloatRegisters, material, world, pParams, pProgram->Name.c_str(), true );
    static const bool bDumpConstants = CmdLine::HasArg( "-edendumpconstants" );
    if ( bDumpConstants && dumpedPrograms.insert( pProgram->Name ).second ) {
        const float *pVS = reinterpret_cast<const float *>( static_cast<uint8_t *>( vsConstants.pData ) + kIntRegistersSize );
        const float *pPS = reinterpret_cast<const float *>( static_cast<uint8_t *>( psConstants.pData ) + kIntRegistersSize );
        std::string text;
        char line[160];
        for ( uint32_t r = 0; r < kVSFloatRegisters; r++ ) {
            if ( pVS[r * 4] != 0.0f || pVS[r * 4 + 1] != 0.0f || pVS[r * 4 + 2] != 0.0f || pVS[r * 4 + 3] != 0.0f ) {
                snprintf( line, sizeof( line ), "  vs c%u = %g %g %g %g\n", r, pVS[r * 4], pVS[r * 4 + 1], pVS[r * 4 + 2],
                          pVS[r * 4 + 3] );
                text += line;
            }
        }
        for ( uint32_t r = 0; r < kPSFloatRegisters; r++ ) {
            if ( pPS[r * 4] != 0.0f || pPS[r * 4 + 1] != 0.0f || pPS[r * 4 + 2] != 0.0f || pPS[r * 4 + 3] != 0.0f ) {
                snprintf( line, sizeof( line ), "  ps c%u = %g %g %g %g\n", r, pPS[r * 4], pPS[r * 4 + 1], pPS[r * 4 + 2],
                          pPS[r * 4 + 3] );
                text += line;
            }
        }
        OTDU_LOG_INFO( "Eden constants %s (%s):\n%s", pProgram->Name.c_str(), material.Name.c_str(), text.c_str() );
    }
    uint32_t *pSpec = static_cast<uint32_t *>( specConstants.pData );
    memset( pSpec, 0, kSpecDwords * 4 );
    const uint32_t alphaCompareOp =
        material.bAlphaTest ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
    pSpec[1] = alphaCompareOp << kSpecAlphaCompareOpShift;

    // P2 (see drawSetCache in the header): look up the set by the material
    // and the per-pass texture overrides (shadow slot 15, screen, projective)
    // - the resolved image views are a pure function of those. -nodescriptorcache
    // forces the per-draw allocate + writes for A/B.
    static const bool bDescriptorCache = !CmdLine::HasArg( "-nodescriptorcache" );
    const uint32_t frameSlot = pDevice->getFrameSlot();
    if ( frameSlot != drawSetCacheSlot ) {
        drawSetCacheSlot = frameSlot;
        drawSetCache[frameSlot].clear();
    }
    // The per-draw texture overrides (night_lighting_spec 2.1: the -s map of
    // the instance's sector onto the TD_Ground SHADOW slot) resolve to a
    // texture AND a sampler per slot; a zero sampler keeps the material's.
    GPUTexture *pOverrideTextures[16] = {};
    uint16_t overrideSamplers[16] = {};
    uint64_t overrideTextureFold = 0;
    if ( pParams != nullptr && !pParams->TextureOverrides.empty() ) {
        for ( uint32_t i = 0; i < material.NumSlots && i < 16; i++ ) {
            for ( const EdenDrawParams::TextureOverride &entry : pParams->TextureOverrides ) {
                if ( entry.ShaderSetGUID == material.ShaderSetGUID && entry.SlotType == material.SlotTypes[i] &&
                     entry.pTexture != nullptr ) {
                    pOverrideTextures[i] = entry.pTexture;
                    overrideSamplers[i] = entry.Sampler;
                }
            }
        }
        for ( uint32_t i = 0; i < 16; i++ ) {
            if ( pOverrideTextures[i] != nullptr ) {
                const uint64_t ptr = reinterpret_cast<uint64_t>( pOverrideTextures[i] );
                overrideTextureFold ^= ptr + 0x9e3779b97f4a7c15ull + ( overrideTextureFold << 6 ) +
                                      ( overrideTextureFold >> 2 );
            }
        }
    }
    const DrawSetKey key = { pProgram, material.Hashcode, pShadowRealTimeOverride, pScreenTexture,
                             pProjectiveTexture, material.textureStamp(), overrideTextureFold };
    VkDescriptorSet set = VK_NULL_HANDLE;
    if ( bDescriptorCache ) {
        auto it = drawSetCache[frameSlot].find( &material );
        if ( it != drawSetCache[frameSlot].end() ) {
            for ( const auto &entry : it->second ) {
                if ( entry.first == key ) {
                    set = entry.second;
                    drawSetHits++;
                    break;
                }
            }
        }
    }

    if ( set == VK_NULL_HANDLE ) {
        set = allocateFrameSet();
        if ( set == VK_NULL_HANDLE ) {
            return;
        }

        // The three transient buffers are the DYNAMIC bindings: base offset 0
        // in the write, the per-draw offset comes at bind time.
        const VkDescriptorBufferInfo bufferInfos[5] = {
            { vsConstants.Buffer, 0, kVSConstantsSize },
            { pZeroBuffer->Buffer, 0, 6 * 16 },
            { psConstants.Buffer, 0, kPSConstantsSize },
            { pZeroBuffer->Buffer, 0, 8 * 64 },
            { specConstants.Buffer, 0, kSpecDwords * 4 },
        };
        const uint32_t bufferBindings[5] = { 0, 3, 10, 12, 31 };
        const VkDescriptorType bufferTypes[5] = {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
        };
        VkDescriptorImageInfo imageInfos[kNumVSImages + kNumPSImages] = {};
        auto pickTexture = [&]( uint8_t samplerType, GPUTexture *pTexture ) -> GPUTexture * {
            const VkImageViewType wanted = samplerType == 3   ? VK_IMAGE_VIEW_TYPE_CUBE
                                           : samplerType == 4 ? VK_IMAGE_VIEW_TYPE_3D
                                                              : VK_IMAGE_VIEW_TYPE_2D;
            if ( pTexture != nullptr && pTexture->ViewType == wanted ) {
                return pTexture;
            }
            if ( pTexture != nullptr && Logger::IsVerbose() ) {
                const std::string keyName = material.Name + "|" + pTexture->Name;
                if ( warnedParameters.insert( keyName ).second ) {
                    OTDU_LOG_DEBUG( "Eden: material '%s' (%s): texture '%s' view type %d, sampler wants %d\n",
                                    material.Name.c_str(), pProgram->Name.c_str(), pTexture->Name.c_str(),
                                    static_cast<int32_t>( pTexture->ViewType ), static_cast<int32_t>( wanted ) );
                }
            }
            return wanted == VK_IMAGE_VIEW_TYPE_CUBE  ? pWhiteCube
                   : wanted == VK_IMAGE_VIEW_TYPE_3D ? pWhite3D
                                                      : pWhite2D;
        };
        for ( uint32_t i = 0; i < kNumVSImages; i++ ) {
            GPUTexture *pTexture = pickTexture( pProgram->VSSamplerTypes[i], nullptr );
            imageInfos[i].imageView = pTexture->View;
            imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        for ( uint32_t i = 0; i < kNumPSImages; i++ ) {
            GPUTexture *pSource = pOverrideTextures[i] != nullptr ? pOverrideTextures[i] : material.pTextures[i];
            if ( pSource == nullptr && i == 15 ) {
                pSource = pShadowRealTimeOverride != nullptr ? pShadowRealTimeOverride : pShadowRealTime;
            } else if ( pSource == nullptr && pScreenTexture != nullptr && i < material.NumSlots &&
                        material.TextureHashes[i] == 1ull && material.ShaderSetGUID == 0x132435465768798aull ) {
                // Runtime screen buffer slots (TD_ShadowBox gViewPos...)
                pSource = pScreenTexture;
            } else if ( pSource == nullptr && material.ProjectiveSlot == static_cast<int8_t>( i ) ) {
                // GSReflection projective map (FUN_00d7c030: the RT colour of
                // the REFLECT layers, else a default texture).
                pSource = pProjectiveTexture != nullptr ? pProjectiveTexture : pBlack2D;
            } else if ( pSource == nullptr && material.bNormalSlot[i] ) {
                pSource = pFlatNormal;
            }
            static const bool bChecker = CmdLine::HasArg( "-edenchecker" );
            if ( bChecker && i != 15 ) {
                pSource = pTextureCache->getCheckerTexture();
            }
            GPUTexture *pTexture = pickTexture( pProgram->PSSamplerTypes[i], pSource );
            imageInfos[kNumVSImages + i].imageView = pTexture->View;
            imageInfos[kNumVSImages + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        VkWriteDescriptorSet writes[5 + 2] = {};
        for ( uint32_t i = 0; i < 5; i++ ) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = bufferBindings[i];
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = bufferTypes[i];
            writes[i].pBufferInfo = &bufferInfos[i];
        }
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = set;
        writes[5].dstBinding = kVSImageBinding;
        writes[5].descriptorCount = kNumVSImages;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[5].pImageInfo = imageInfos;
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = set;
        writes[6].dstBinding = kPSImageBinding;
        writes[6].descriptorCount = kNumPSImages;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[6].pImageInfo = imageInfos + kNumVSImages;
        vkUpdateDescriptorSets( device, 7, writes, 0, nullptr );

        if ( bDescriptorCache ) {
            drawSetCache[frameSlot][&material].push_back( { key, set } );
        }
    }

    DrawPacket packet;
    memset( packet.Push, 0, sizeof( packet.Push ) );
    const uint32_t alphaRef = material.AlphaRef;
    memcpy( packet.Push + kPushAlphaRefOffset, &alphaRef, sizeof( alphaRef ) );
    for ( uint32_t i = 0; i < 16; i++ ) {
        const uint16_t samplerIndex = overrideSamplers[i] != 0 ? overrideSamplers[i] : material.SamplerIndex[i];
        memcpy( packet.Push + kPushSamplerOffset + i * 2, &samplerIndex, sizeof( samplerIndex ) );
    }
    packet.Pipeline = pipeline;
    packet.Set = set;
    // The DYNAMIC UBO offsets (bindings 0, 10, 31, in set then binding order).
    packet.DynamicOffsets[0] = static_cast<uint32_t>( vsConstants.Offset );
    packet.DynamicOffsets[1] = static_cast<uint32_t>( psConstants.Offset );
    packet.DynamicOffsets[2] = static_cast<uint32_t>( specConstants.Offset );
    for ( uint32_t i = 0; i < EdenMesh::kMaxStreams; i++ ) {
        if ( i < mesh.NumStreams && mesh.pStreamBuffers[i] != nullptr ) {
            packet.VertexBuffers[i] = mesh.pStreamBuffers[i]->Buffer;
            packet.VertexOffsets[i] = mesh.StreamOffsets[i];
        } else {
            packet.VertexBuffers[i] = pDefaultAttributes->Buffer;
            packet.VertexOffsets[i] = 0;
        }
    }
    packet.VertexBuffers[EdenMesh::kMaxStreams] = pDefaultAttributes->Buffer;
    packet.VertexOffsets[EdenMesh::kMaxStreams] = 0;
    packet.IndexBuffer = mesh.pIndexBuffer->Buffer;
    packet.IndexCount = mesh.IndexCount;
    packet.FirstIndex = mesh.FirstIndex;
    packet.VertexOffset = mesh.VertexOffset;
    numDrawCalls++;
    // -logdrawfamilies: every 120 frames, the draws by shader family and how
    // many distinct (mesh, material) pairs they are - the instancing potential
    // (docs/PERF_PLAN.md 0a).
    static const bool bLogFamilies = CmdLine::HasArg( "-logdrawfamilies" );
    if ( bLogFamilies ) {
        struct FamilyStats {
            uint64_t Draws = 0;
            std::unordered_set<uint64_t> Pairs;
        };
        static std::unordered_map<std::string, FamilyStats> families;
        static uint64_t lastFrame = 0, frames = 0;
        if ( pDevice->getFrameNumber() != lastFrame ) {
            lastFrame = pDevice->getFrameNumber();
            if ( ++frames % 120 == 0 ) {
                std::vector<std::pair<std::string, FamilyStats *>> list;
                uint64_t total = 0, pairs = 0;
                for ( auto &entry : families ) {
                    list.push_back( { entry.first, &entry.second } );
                    total += entry.second.Draws;
                    pairs += entry.second.Pairs.size();
                }
                std::sort( list.begin(), list.end(),
                           []( const auto &a, const auto &b ) { return a.second->Draws > b.second->Draws; } );
                OTDU_LOG_INFO( "[perf] draw families (last frame): %llu draws, %llu distinct mesh/material pairs\n",
                               static_cast<unsigned long long>( total ), static_cast<unsigned long long>( pairs ) );
                for ( size_t i = 0; i < list.size() && i < 14; i++ ) {
                    OTDU_LOG_INFO( "[perf]   %-40s %6llu draws %6zu pairs\n", list[i].first.c_str(),
                                   static_cast<unsigned long long>( list[i].second->Draws ), list[i].second->Pairs.size() );
                }
            }
            families.clear();
        }
        const std::string &name = pProgram->Name;
        const size_t cut = name.find( "_vs" );
        FamilyStats &stats = families[cut != std::string::npos ? name.substr( 0, cut ) : name];
        stats.Draws++;
        stats.Pairs.insert( reinterpret_cast<uint64_t>( &material ) * 31 ^
                            reinterpret_cast<uint64_t>( packet.IndexBuffer ) * 7 ^ packet.FirstIndex );
    }

    // Parallel recording: inside an offscreen target the packet waits for the
    // flush (the device calls flushPendingRecords before anything else
    // records), which hands slices of the queue to the worker threads.
    if ( pParallel != nullptr && pDevice->isInRenderTarget() ) {
        if ( pendingDraws.empty() ) {
            pDevice->setPendingRecordHook( this );
        }
        pendingDraws.push_back( packet );
        return;
    }
    // Inline: the redundant-bind filter (see BoundState) - consecutive draws of
    // one model / material share the pipeline, the stream + index buffers and
    // the push constants. Anything else recording into the buffer (the sky
    // stars, surface FX, the 2D renderer, a render pass switch...) bumps the
    // device's touch count and resets the filter.
    static const bool bBindCache = !CmdLine::HasArg( "-nobindcache" );
    if ( !bBindCache || bound.CommandBuffer != commandBuffer || bound.Touches != pDevice->getCommandTouches() ) {
        bound = BoundState();
        bound.CommandBuffer = commandBuffer;
        bound.Touches = pDevice->getCommandTouches();
    }
    recordPacket( commandBuffer, bound, packet, bBindCache );
}

void EdenRenderer::recordPacket( VkCommandBuffer commandBuffer, BoundState &state, const DrawPacket &packet,
                                 bool bFilter ) const {
    if ( !bFilter ) {
        state = BoundState();
    }
    if ( state.Pipeline != packet.Pipeline ) {
        vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, packet.Pipeline );
        state.Pipeline = packet.Pipeline;
    }
    // Always: the dynamic constant offsets change with every draw.
    const VkDescriptorSet sets[2] = { packet.Set, samplerSet };
    vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, sets, 3,
                             packet.DynamicOffsets );
    if ( !state.bPushValid || memcmp( state.Push, packet.Push, sizeof( packet.Push ) ) != 0 ) {
        vkCmdPushConstants( commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof( packet.Push ), packet.Push );
        memcpy( state.Push, packet.Push, sizeof( packet.Push ) );
        state.bPushValid = true;
    }
    if ( !state.bVertexValid || memcmp( state.VertexBuffers, packet.VertexBuffers, sizeof( packet.VertexBuffers ) ) != 0 ||
         memcmp( state.VertexOffsets, packet.VertexOffsets, sizeof( packet.VertexOffsets ) ) != 0 ) {
        vkCmdBindVertexBuffers( commandBuffer, 0, EdenMesh::kMaxStreams + 1, packet.VertexBuffers, packet.VertexOffsets );
        memcpy( state.VertexBuffers, packet.VertexBuffers, sizeof( packet.VertexBuffers ) );
        memcpy( state.VertexOffsets, packet.VertexOffsets, sizeof( packet.VertexOffsets ) );
        state.bVertexValid = true;
    }
    if ( state.IndexBuffer != packet.IndexBuffer ) {
        vkCmdBindIndexBuffer( commandBuffer, packet.IndexBuffer, 0, VK_INDEX_TYPE_UINT32 );
        state.IndexBuffer = packet.IndexBuffer;
    }
    vkCmdDrawIndexed( commandBuffer, packet.IndexCount, 1, packet.FirstIndex, packet.VertexOffset, 0 );
}

void EdenRenderer::flushPendingRecords() {
    if ( pendingDraws.empty() ) {
        return;
    }
    VkCommandBuffer primary = pDevice->peekCommandBuffer();
    static const bool bBindCache = !CmdLine::HasArg( "-nobindcache" );
    // Small batches (a few draws between two state changes) cost more as a
    // render pass restart than they save: recorded inline, in order.
    constexpr uint32_t kMinParallelDraws = 192;
    constexpr uint32_t kMinChunkDraws = 96;
    const uint32_t count = static_cast<uint32_t>( pendingDraws.size() );
    const uint32_t maxChunks = static_cast<uint32_t>( pParallel->Contexts.size() );
    const uint32_t chunks = Min( maxChunks, Max( 1u, count / kMinChunkDraws ) );
    if ( !pDevice->isInRenderTarget() || count < kMinParallelDraws || chunks < 2 ) {
        BoundState state;
        for ( const DrawPacket &packet : pendingDraws ) {
            recordPacket( primary, state, packet, bBindCache );
        }
        inlineFlushDraws += count;
        pendingDraws.clear();
        bound = BoundState(); // the inline filter does not know what was bound
        return;
    }

    pDevice->restartRenderTarget( true );
    const uint32_t slot = pDevice->getFrameSlot();
    VkCommandBufferInheritanceInfo inheritance = {};
    inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritance.renderPass = pDevice->getActiveRenderPass();
    inheritance.subpass = 0;
    inheritance.framebuffer = pDevice->getCurrentFramebuffer();
    const VkViewport viewport = pDevice->getCurrentViewport();
    const VkRect2D scissor = pDevice->getCurrentScissor();
    VkCommandBuffer secondaries[16] = {};
    const std::function<void( uint32_t )> job = [&]( uint32_t chunk ) {
        ParallelRecord::Context &context = pParallel->Contexts[chunk];
        uint32_t &used = context.Used[slot];
        if ( used == context.Buffers[slot].size() ) {
            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = context.Pools[slot];
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            allocInfo.commandBufferCount = 1;
            VkCommandBuffer buffer = VK_NULL_HANDLE;
            vkAllocateCommandBuffers( pDevice->getDevice(), &allocInfo, &buffer );
            context.Buffers[slot].push_back( buffer );
        }
        VkCommandBuffer buffer = context.Buffers[slot][used++];
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        beginInfo.pInheritanceInfo = &inheritance;
        vkBeginCommandBuffer( buffer, &beginInfo );
        vkCmdSetViewport( buffer, 0, 1, &viewport );
        vkCmdSetScissor( buffer, 0, 1, &scissor );
        const uint32_t first = static_cast<uint32_t>( static_cast<uint64_t>( count ) * chunk / chunks );
        const uint32_t last = static_cast<uint32_t>( static_cast<uint64_t>( count ) * ( chunk + 1 ) / chunks );
        BoundState state;
        for ( uint32_t i = first; i < last; i++ ) {
            recordPacket( buffer, state, pendingDraws[i], bBindCache );
        }
        vkEndCommandBuffer( buffer );
        secondaries[chunk] = buffer;
    };
    pParallel->run( chunks, job );
    vkCmdExecuteCommands( primary, chunks, secondaries );
    pDevice->restartRenderTarget( false );
    parallelFlushes++;
    parallelDraws += count;
    pendingDraws.clear();
    bound = BoundState();
}

VkDescriptorSet EdenRenderer::allocateFrameSet() {
    // A frame can need more sets than one pool holds (environment face, Z
    // pre-pass, 3 shadow slices, reflection, scene): further pools are
    // created on demand instead of dropping the draws.
    VkDevice device = pDevice->getDevice();
    const uint32_t slot = pDevice->getFrameSlot();
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayouts[0];
    VkDescriptorSet set = VK_NULL_HANDLE;
    // Pools are switched on the set count: some drivers (lavapipe) do not
    // fail past maxSets and corrupt their pool instead.
    const uint32_t poolIndex = setsAllocated[slot] / kMaxSetsPerFrame;
    setsAllocated[slot]++;
    if ( poolIndex <= overflowUsed[slot] ) {
        allocInfo.descriptorPool =
            overflowUsed[slot] == 0 ? framePools[slot] : overflowPools[slot][overflowUsed[slot] - 1];
        if ( vkAllocateDescriptorSets( device, &allocInfo, &set ) == VK_SUCCESS ) {
            return set;
        }
    }
    if ( overflowUsed[slot] == overflowPools[slot].size() ) {
        const VkDescriptorPoolSize frameSizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxSetsPerFrame * 2 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kMaxSetsPerFrame * 3 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSetsPerFrame * ( kNumVSImages + kNumPSImages ) },
        };
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = kMaxSetsPerFrame;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = frameSizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if ( vkCreateDescriptorPool( device, &poolInfo, nullptr, &pool ) != VK_SUCCESS ) {
            OTDU_LOG_ERROR( "Eden: descriptor pool creation failed\n" );
            return VK_NULL_HANDLE;
        }
        overflowPools[slot].push_back( pool );
        OTDU_LOG_INFO( "Eden: descriptor pool %zu added (frame slot %u)\n", overflowPools[slot].size(), slot );
    }
    allocInfo.descriptorPool = overflowPools[slot][overflowUsed[slot]++];
    if ( vkAllocateDescriptorSets( device, &allocInfo, &set ) != VK_SUCCESS ) {
        return VK_NULL_HANDLE;
    }
    return set;
}

const EdenMaterial *EdenRenderer::getZPassMaterial( const EdenMaterial &material ) {
    auto it = zPassMaterials.find( &material );
    if ( it != zPassMaterials.end() && it->second.first == material.Hashcode ) {
        return it->second.second.get();
    }
    std::unique_ptr<EdenMaterial> pZPass;
    if ( material.pProgram != nullptr && material.Spec[EdenShaderSystem::S_PASS_Z] == 0 ) {
        pZPass = std::make_unique<EdenMaterial>( material );
        pZPass->Spec[EdenShaderSystem::S_PASS_Z] = 1;
        updateProgram( *pZPass );
        // A family without S_PASS_Z permutations resolves to the same item.
        if ( pZPass->pProgram == nullptr || pZPass->pProgram->ItemId == material.pProgram->ItemId ) {
            pZPass.reset();
        } else {
            pZPass->SrcBlend = 2;
            pZPass->DstBlend = 1;
            pZPass->BlendOp = 1;
            pZPass->bDepthTest = true;
            pZPass->bDepthWrite = true;
        }
    }
    if ( Logger::IsVerbose() ) {
        OTDU_LOG_DEBUG( "Eden: Z pass of '%s' (%s): %s\n", material.Name.c_str(),
                        material.pProgram != nullptr ? material.pProgram->Name.c_str() : "-",
                        pZPass != nullptr ? pZPass->pProgram->Name.c_str() : "none" );
    }
    EdenMaterial *pResult = pZPass.get();
    zPassMaterials[&material] = { material.Hashcode, std::move( pZPass ) };
    return pResult;
}

const EdenMaterial *EdenRenderer::getCasterMaterial( const EdenMaterial &material ) {
    auto it = casterMaterials.find( &material );
    if ( it != casterMaterials.end() && it->second.first == material.Hashcode ) {
        return it->second.second.get();
    }
    std::unique_ptr<EdenMaterial> pCaster;
    ShaderLibrary *pLibrary = shaderSystem.findLibrary( material.ShaderSetGUID );
    const std::string family = pLibrary != nullptr ? ToUpper( pLibrary->getName() ) : std::string();
    // Skinned meshes (avatars: TD_Avatar, TD_Clothe2...) get the skinned
    // TD_ShadowCaster permutations (vs_7 / ps_1, alpha tested vs_8 / ps_2):
    // their vertex shader declares __Skin_Matrix__array96, filled from the
    // draw parameters like the scene pass. Without them no avatar casts a
    // shadow (the pool party, walk mode).
    const bool bSkinned = material.Spec[EdenShaderSystem::S_USESKINNING] != 0;
    if ( material.pProgram == nullptr || family == "TD_HEIGHTMAP" || family == "TD_GROUND_HM" ||
         material.ShaderSetGUID == 0x0cea000000000002ull ) {
        // The heightfields (their vertex shader rebuilds the grid, self
        // shadowing is in the sector shadow maps): no caster.
    } else if ( family == "TD_VEGETATION" || family == "TD_VEGETATIONSPR" || family == "EDRENDER_GOURAUD" ) {
        // Families with their own S_PASS_SHADOWCAST permutations (wind,
        // billboard leaves).
        pCaster = std::make_unique<EdenMaterial>( material );
        pCaster->Spec[EdenShaderSystem::S_PASS_SHADOWCAST] = 1;
        // The caster writes the light depth: no blending.
        pCaster->SrcBlend = 2;
        pCaster->DstBlend = 1;
        updateProgram( *pCaster );
    } else {
        // TD_ShadowCaster (ac78df32bc4589ff): vs_4 / ps_1 (skinned vs_7 /
        // ps_1), alpha tested vs_5 / ps_2 (skinned vs_8 / ps_2:
        // texkill( base.a - __Alpha_Ref )).
        pCaster = std::make_unique<EdenMaterial>();
        pCaster->Name = material.Name + " (caster)";
        pCaster->Hashcode = material.Hashcode;
        pCaster->ShaderSetGUID = 0xac78df32bc4589ffull;
        pCaster->CullMode = material.CullMode;
        // Avatars (closed skinned meshes) cast with their back faces: a lit
        // face then tests against the far side of the body, not against
        // itself, and the self shadowing bands (stripes over the faces,
        // necks and shoulders, crawling with the camera) go away. Open
        // geometry (house walls, decks) keeps its own faces: with the back
        // faces its shadows vanish [INF: the original's caster cull is not
        // read]. -casterfrontfaces: the avatars too keep their faces.
        static const bool bFrontFaces = CmdLine::HasArg( "-casterfrontfaces" );
        if ( bSkinned && !bFrontFaces && !material.bAlphaTest && ( material.CullMode == 2 || material.CullMode == 4 ) ) {
            pCaster->CullMode = material.CullMode == 2 ? 4 : 2;
        }
        pCaster->AlphaRef = material.AlphaRef;
        pCaster->bAlphaTest = material.bAlphaTest;
        pCaster->Spec[EdenShaderSystem::S_USESKINNING] = bSkinned ? 1 : 0;
        const uint32_t psAttrib = material.bAlphaTest ? 2 : 1;
        const uint32_t vsAttrib = bSkinned ? ( material.bAlphaTest ? 8 : 7 ) : ( material.bAlphaTest ? 5 : 4 );
        if ( material.bAlphaTest ) {
            pCaster->pTextures[0] = material.pTextures[0];
            pCaster->SamplerIndex[0] = material.SamplerIndex[0];
        }
        pCaster->pProgram = shaderSystem.getProgram( "TD_ShadowCaster", vsAttrib, psAttrib );
        if ( pCaster->pProgram == nullptr && bSkinned ) {
            // No skinned permutation in this library: fall back to the rigid
            // caster (the mesh is then drawn unskinned: better than nothing).
            pCaster->Spec[EdenShaderSystem::S_USESKINNING] = 0;
            pCaster->pProgram = shaderSystem.getProgram( "TD_ShadowCaster", material.bAlphaTest ? 5 : 4, psAttrib );
        }
    }
    if ( pCaster != nullptr ) {
        pCaster->SrcBlend = 2;
        pCaster->DstBlend = 1;
        pCaster->BlendOp = 1;
        pCaster->ColorWriteMask = 0xf;
        pCaster->bDepthTest = true;
        pCaster->bDepthWrite = true;
        pCaster->DepthFunc = 0;
    }
    EdenMaterial *pResult = pCaster.get();
    casterMaterials[&material] = { material.Hashcode, std::move( pCaster ) };
    return pResult;
}
