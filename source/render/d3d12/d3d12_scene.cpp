#include "shared.h"
#include "render/d3d12/d3d12_scene.h"

#if defined( OTDU_D3D12 )

#include "render/d3d12/d3d12_device.h"
#include "render/d3d12/d3d12_shader.h"
#include "render/eden/eden_constants.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_shader_system.h"
#include "render/sampler_heap.h"
#include "render/texture_cache.h"
#include "render/shaders/builtin/mesh_frag.h"
#include "render/shaders/builtin/mesh_vert.h"
#include "render/shaders/dxso_translator.h"
#include "core/watchdog.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include <windows.h>
#include <d3d12.h>

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>

namespace {
template <typename T> void SafeRelease( T *&pObject ) {
    if ( pObject != nullptr ) {
        pObject->Release();
        pObject = nullptr;
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle( uint64_t value ) {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
    handle.ptr = static_cast<SIZE_T>( value );
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle( uint64_t value ) {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = {};
    handle.ptr = value;
    return handle;
}

// The pipeline key: every field that changes the PSO. Bytecode identity is the
// stage blob's address - the D3D12ShaderStage outlives the pipeline (its owner
// keeps the cache), the same lifetime rule the Vulkan pipeline cache uses.
std::string MakePipelineKey( const D3D12ShaderStage &vs,
                             const D3D12ShaderStage &ps,
                             const D3D12Scene::PipelineDesc &desc ) {
    std::string key;
    key.reserve( 128 );
    key.append( reinterpret_cast<const char *>( &vs.Bytecode ), sizeof( vs.Bytecode ) );
    key.append( reinterpret_cast<const char *>( &ps.Bytecode ), sizeof( ps.Bytecode ) );
    const char state[6] = { static_cast<char>( desc.Blend ), static_cast<char>( desc.Cull ),
                            static_cast<char>( desc.bDepthTest ), static_cast<char>( desc.bDepthWrite ),
                            static_cast<char>( desc.bFrontFaceCW ), static_cast<char>( desc.NumStreams ) };
    key.append( state, sizeof( state ) );
    // The D3D9 material state: every field that reaches the PSO. The blend factor
    // and the alpha reference are draw state on D3D12 and deliberately absent.
    if ( desc.pState != nullptr ) {
        const char d3d9[8] = { static_cast<char>( desc.pState->SrcBlend ),
                               static_cast<char>( desc.pState->DstBlend ),
                               static_cast<char>( desc.pState->BlendOp ),
                               static_cast<char>( desc.pState->DepthFunc ),
                               static_cast<char>( desc.pState->ColorWriteMask ),
                               static_cast<char>( desc.pState->CullMode ),
                               static_cast<char>( desc.pState->bMirrored ? 1 : 0 ), 0 };
        key.append( d3d9, sizeof( d3d9 ) );
    }
    for ( uint32_t i = 0; i < desc.NumStreams; i++ ) {
        key.append( reinterpret_cast<const char *>( &desc.pStrides[i] ), sizeof( uint32_t ) );
    }
    for ( uint32_t i = 0; i < desc.NumAttributes; i++ ) {
        key.append( reinterpret_cast<const char *>( &desc.pAttributes[i] ), sizeof( D3D12Scene::Attribute ) );
    }
    return key;
}

// D3DBLEND -> D3D12_BLEND. The two enumerations agree on almost nothing past ZERO
// and ONE, so the table is spelled out; an unknown value falls back to ONE, which
// is what D3D9 does for an out-of-range render state.
D3D12_BLEND ToD3D12Blend( uint8_t d3dBlend ) {
    switch ( d3dBlend ) {
    case 1: return D3D12_BLEND_ZERO;
    case 3: return D3D12_BLEND_SRC_COLOR;
    case 4: return D3D12_BLEND_INV_SRC_COLOR;
    case 5: return D3D12_BLEND_SRC_ALPHA;
    case 6: return D3D12_BLEND_INV_SRC_ALPHA;
    case 7: return D3D12_BLEND_DEST_ALPHA;
    case 8: return D3D12_BLEND_INV_DEST_ALPHA;
    case 9: return D3D12_BLEND_DEST_COLOR;
    case 10: return D3D12_BLEND_INV_DEST_COLOR;
    case 11: return D3D12_BLEND_SRC_ALPHA_SAT;
    case 14: return D3D12_BLEND_BLEND_FACTOR;
    case 15: return D3D12_BLEND_INV_BLEND_FACTOR;
    case 2:
    default: return D3D12_BLEND_ONE;
    }
}

// D3DBLENDOP -> D3D12_BLEND_OP (index 0 is not a D3D9 value; ADD is the default).
D3D12_BLEND_OP ToD3D12BlendOp( uint8_t d3dOp ) {
    switch ( d3dOp ) {
    case 2: return D3D12_BLEND_OP_SUBTRACT;
    case 3: return D3D12_BLEND_OP_REV_SUBTRACT;
    case 4: return D3D12_BLEND_OP_MIN;
    case 5: return D3D12_BLEND_OP_MAX;
    case 1:
    default: return D3D12_BLEND_OP_ADD;
    }
}

// D3DCMPFUNC -> D3D12_COMPARISON_FUNC. Index 0 is EdenMaterial's "not set" value
// and means LESSEQUAL, exactly like EdenRenderer::getPipeline's table.
D3D12_COMPARISON_FUNC ToD3D12Comparison( uint8_t d3dFunc ) {
    static constexpr D3D12_COMPARISON_FUNC kFuncs[9] = {
        D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_COMPARISON_FUNC_NEVER,
        D3D12_COMPARISON_FUNC_LESS,       D3D12_COMPARISON_FUNC_EQUAL,
        D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_COMPARISON_FUNC_GREATER,
        D3D12_COMPARISON_FUNC_NOT_EQUAL,  D3D12_COMPARISON_FUNC_GREATER_EQUAL,
        D3D12_COMPARISON_FUNC_ALWAYS
    };
    return kFuncs[d3dFunc <= 8 ? d3dFunc : 0];
}
} // namespace


D3D12Scene::D3D12Scene() = default;

D3D12Scene::~D3D12Scene() { destroy(); }


bool D3D12Scene::createRootSignature() {
    ID3D12Device *pD3D12Device = pDevice->getDevice();

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = kTextureTableSize;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE samplerRange = {};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = kSamplerTableSize;
    samplerRange.BaseShaderRegister = 0;
    samplerRange.RegisterSpace = 0;
    samplerRange.OffsetInDescriptorsFromTableStart = 0;

    // The root parameter order, named once: four constant buffers per stage
    // (b0..b3), the push constants, the texture table and the sampler table.
    D3D12_ROOT_PARAMETER params[11] = {};
    // 0..3: the vertex stage's cbuffers b0..b3 (Eden's VS constant file, then the
    // spec / clip blocks the compiler numbered after it).
    for ( uint32_t i = 0; i < D3D12Scene::kMaxConstantBuffers; i++ ) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[i].Descriptor.ShaderRegister = i;
        params[i].Descriptor.RegisterSpace = 0;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        params[4 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[4 + i].Descriptor.ShaderRegister = i;
        params[4 + i].Descriptor.RegisterSpace = 0;
        params[4 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    // 8: the D3D9 push constants as root constants (d3d12_shader.h's b13, in
    // register space 1 so the remap key cannot collide with a D3D9 sampler slot).
    params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[8].Constants.ShaderRegister = D3D12ShaderCompiler::kPushConstantRegister;
    params[8].Constants.RegisterSpace = D3D12ShaderCompiler::kPushConstantSpace;
    params[8].Constants.Num32BitValues = kMaxRootConstants;
    params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // 9: the material's textures.
    params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[9].DescriptorTable.NumDescriptorRanges = 1;
    params[9].DescriptorTable.pDescriptorRanges = &srvRange;
    params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // 10: the D3D9 sampler set, as a table rather than static samplers. DXSO
    // gives every Eden shader a *runtime* sampler array whose index is read from
    // the push constants; a static sampler cannot be indexed dynamically, so the
    // table is what makes an Eden draw possible at all (docs/backends.md
    // section 4). Its entries are filled by createSamplerHeap.
    params[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[10].DescriptorTable.NumDescriptorRanges = 1;
    params[10].DescriptorTable.pDescriptorRanges = &samplerRange;
    params[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 11;
    desc.pParameters = params;
    // No static samplers: registers s0..s15 belong to the sampler table above.
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers = nullptr;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                 D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                 D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                 D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ID3DBlob *pSignature = nullptr;
    ID3DBlob *pErrors = nullptr;
    const HRESULT result =
        D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &pSignature, &pErrors );
    if ( FAILED( result ) ) {
        OTDU_LOG_ERROR( "D3D12 scene: the root signature could not be serialized: %s\n",
                        pErrors != nullptr ? static_cast<const char *>( pErrors->GetBufferPointer() )
                                           : "no message" );
        SafeRelease( pSignature );
        SafeRelease( pErrors );
        return false;
    }
    SafeRelease( pErrors );

    ID3D12RootSignature *pSignatureObject = nullptr;
    const HRESULT created = pD3D12Device->CreateRootSignature(
        0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(), IID_PPV_ARGS( &pSignatureObject ) );
    SafeRelease( pSignature );
    if ( FAILED( created ) ) {
        OTDU_LOG_ERROR( "D3D12 scene: CreateRootSignature failed (0x%08x)\n", static_cast<uint32_t>( created ) );
        return false;
    }
    pRootSignature = pSignatureObject;

    // The tables the draws copy their textures into: a shader-visible SRV heap
    // of its own, so the device's heap (the 2D layer, ImGui) keeps its own
    // allocation bookkeeping.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = kTableDescriptors;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap *pHeap = nullptr;
    if ( FAILED( pD3D12Device->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &pHeap ) ) ) ) {
        OTDU_LOG_ERROR( "D3D12 scene: the texture table heap could not be created\n" );
        return false;
    }
    pSrvTable = pHeap;
    srvTableCpuStart = pHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    srvTableGpuStart = pHeap->GetGPUDescriptorHandleForHeapStart().ptr;
    srvDescriptorSize = pD3D12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    if ( !createSamplerHeap() ) {
        return false;
    }
    if ( !createConstantRing() ) {
        return false;
    }
    OTDU_LOG_INFO( "D3D12 scene: root signature ready (b0..b%u per stage, b%u space%u root constants, "
                   "t0..t%u, s0..s%u sampler table, %u heap entries, %llu MB constants per frame)\n",
                   D3D12Scene::kMaxConstantBuffers - 1,
                   D3D12ShaderCompiler::kPushConstantRegister,
                   D3D12ShaderCompiler::kPushConstantSpace, kTextureTableSize - 1,
                   kSamplerTableSize - 1, kSamplerHeapSize,
                   static_cast<unsigned long long>( D3D12Scene::kConstantRingBytes / ( 1024 * 1024 ) ) );
    return true;
}

// The D3D9 sampler set as a shader-visible sampler heap. Filled once: the
// per-material choice is the index the shader reads out of the push constants,
// so nothing here is rewritten per draw (the Vulkan sampler set is written once
// for the same reason, EdenRenderer::writeSamplerSet).
bool D3D12Scene::createSamplerHeap() {
    ID3D12Device *pD3D12Device = pDevice->getDevice();
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    heapDesc.NumDescriptors = kSamplerTableSize;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap *pHeap = nullptr;
    if ( FAILED( pD3D12Device->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &pHeap ) ) ) ) {
        OTDU_LOG_ERROR( "D3D12 scene: the sampler heap could not be created\n" );
        return false;
    }
    const uint32_t descriptorSize =
        pD3D12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER );
    const D3D12_CPU_DESCRIPTOR_HANDLE base = pHeap->GetCPUDescriptorHandleForHeapStart();
    for ( uint32_t i = 0; i < kSamplerTableSize; i++ ) {
        // Out of range entries (s14/s15, or a stale material index) are linear
        // wrap - D3D9's default state.
        const SamplerHeapState state = GetSamplerHeapState( i );
        const D3D12_TEXTURE_ADDRESS_MODE addressU = state.AddressU == 2   ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP
                                                    : state.AddressU == 1 ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR
                                                                          : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        const D3D12_TEXTURE_ADDRESS_MODE addressV = state.AddressV == 2   ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP
                                                    : state.AddressV == 1 ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR
                                                                          : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        D3D12_SAMPLER_DESC sampler = {};
        sampler.Filter = state.bPoint ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = addressU;
        sampler.AddressV = addressV;
        sampler.AddressW = addressU;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor[0] = 0.0f;
        sampler.BorderColor[1] = 0.0f;
        sampler.BorderColor[2] = 0.0f;
        sampler.BorderColor[3] = 0.0f;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_CPU_DESCRIPTOR_HANDLE destination = base;
        destination.ptr += static_cast<SIZE_T>( i ) * descriptorSize;
        pD3D12Device->CreateSampler( &sampler, destination );
    }
    pSamplerHeap = pHeap;
    samplerHeapGpuStart = pHeap->GetGPUDescriptorHandleForHeapStart().ptr;
    return true;
}


bool D3D12Scene::createConstantRing() {
    ID3D12Device *pD3D12Device = pDevice->getDevice();
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    auto createBuffer = [&]( uint64_t size, ID3D12Resource *&pResource, void *&pMapped ) {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        if ( FAILED( pD3D12Device->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                            IID_PPV_ARGS( &pResource ) ) ) ) {
            return false;
        }
        // Mapped for the whole session, like the 2D layer's ring: every draw
        // writes its blocks straight into the ring, no staging copy.
        const D3D12_RANGE noRead = { 0, 0 };
        if ( FAILED( pResource->Map( 0, &noRead, &pMapped ) ) || pMapped == nullptr ) {
            return false;
        }
        return true;
    };

    for ( uint32_t slot = 0; slot < kFrameSlots; slot++ ) {
        ID3D12Resource *pResource = nullptr;
        void *pMapped = nullptr;
        if ( !createBuffer( kConstantRingBytes, pResource, pMapped ) ) {
            SafeRelease( pResource );
            OTDU_LOG_ERROR( "D3D12 scene: the constant ring (%llu MB per frame slot) could not be "
                            "created\n",
                            static_cast<unsigned long long>( kConstantRingBytes / ( 1024 * 1024 ) ) );
            return false;
        }
        pConstantRing[slot] = pResource;
        pConstantRingMapped[slot] = pMapped;
        constantRingCursor[slot] = 0;
        constantRingFrame[slot] = 0;
    }
    // The zero block: a root CBV cannot be null, so a stage that fills fewer than
    // four cbuffers still has a valid address at every register.
    ID3D12Resource *pZero = nullptr;
    void *pZeroMapped = nullptr;
    if ( !createBuffer( kMaxConstantBytes, pZero, pZeroMapped ) ) {
        SafeRelease( pZero );
        OTDU_LOG_ERROR( "D3D12 scene: the zero constant block could not be created\n" );
        return false;
    }
    memset( pZeroMapped, 0, kMaxConstantBytes );
    pZeroConstants = pZero;
    zeroConstantsGpu = pZero->GetGPUVirtualAddress();
    return true;
}

// Copies one stage's blocks into the frame slot's ring. The ring is reset when a
// new frame starts (beginFrame has waited the slot's fence) and every block is
// 256 byte aligned, which is what a root CBV requires of its location.
void D3D12Scene::uploadConstants( const StageConstants *pStage, uint64_t *outGpu ) {
    for ( uint32_t i = 0; i < kMaxConstantBuffers; i++ ) {
        outGpu[i] = zeroConstantsGpu;
    }
    if ( pStage == nullptr || zeroConstantsGpu == 0 ) {
        return;
    }
    const uint32_t slot = pDevice->getFrameSlot() % kFrameSlots;
    if ( pConstantRingMapped[slot] == nullptr ) {
        return;
    }
    if ( constantRingFrame[slot] != pDevice->getFrameNumber() ) {
        constantRingFrame[slot] = pDevice->getFrameNumber();
        constantRingCursor[slot] = 0;
        bConstantOverflowReported = false;
    }
    ID3D12Resource *pBuffer = static_cast<ID3D12Resource *>( pConstantRing[slot] );
    uint8_t *pBase = static_cast<uint8_t *>( pConstantRingMapped[slot] );
    for ( uint32_t i = 0; i < kMaxConstantBuffers; i++ ) {
        if ( pStage->pData[i] == nullptr || pStage->Sizes[i] == 0 ) {
            continue;
        }
        const uint32_t size = Min( pStage->Sizes[i], kMaxConstantBytes );
        const uint64_t aligned = ( constantRingCursor[slot] + kConstantAlignment - 1 ) &
                                 ~( kConstantAlignment - 1 );
        const uint64_t next = aligned + ( ( size + kConstantAlignment - 1 ) & ~( kConstantAlignment - 1 ) );
        if ( next > kConstantRingBytes ) {
            if ( !bConstantOverflowReported ) {
                bConstantOverflowReported = true;
                OTDU_LOG_WARN( "D3D12 scene: the per frame constant ring (%llu MB) is full; draws "
                               "from here read the zero block\n",
                               static_cast<unsigned long long>( kConstantRingBytes / ( 1024 * 1024 ) ) );
            }
            continue;
        }
        memcpy( pBase + aligned, pStage->pData[i], size );
        constantRingCursor[slot] = next;
        outGpu[i] = pBuffer->GetGPUVirtualAddress() + aligned;
    }
}

bool D3D12Scene::initialize( D3D12Device *pDeviceIn ) {
    if ( pDeviceIn == nullptr || !pDeviceIn->isReady() ) {
        return false;
    }
    pDevice = pDeviceIn;
    if ( !createRootSignature() ) {
        destroy();
        return false;
    }
    return true;
}

void D3D12Scene::destroy() {
    for ( auto &entry : pipelines ) {
        ID3D12PipelineState *pPipeline = static_cast<ID3D12PipelineState *>( entry.second );
        SafeRelease( pPipeline );
    }
    pipelines.clear();
    for ( uint32_t slot = 0; slot < kFrameSlots; slot++ ) {
        SafeRelease( reinterpret_cast<ID3D12Resource *&>( pConstantRing[slot] ) );
        pConstantRingMapped[slot] = nullptr;
        constantRingCursor[slot] = 0;
        constantRingFrame[slot] = 0;
    }
    SafeRelease( reinterpret_cast<ID3D12Resource *&>( pZeroConstants ) );
    zeroConstantsGpu = 0;
    bConstantOverflowReported = false;
    SafeRelease( reinterpret_cast<ID3D12DescriptorHeap *&>( pSamplerHeap ) );
    SafeRelease( reinterpret_cast<ID3D12DescriptorHeap *&>( pSrvTable ) );
    SafeRelease( reinterpret_cast<ID3D12RootSignature *&>( pRootSignature ) );
    samplerHeapGpuStart = 0;
    srvTableCpuStart = 0;
    srvTableGpuStart = 0;
    srvCursor = 0;
    srvFrameSlot = 0xFFFFFFFFu;
    pDevice = nullptr;
}

// A contiguous run of descriptors for one draw's texture table. The heap is
// reused when the frame slot changes: that slot's fence has retired, so nothing
// the GPU still reads points into the old range.
uint32_t D3D12Scene::allocateTable( uint32_t count ) {
    if ( pSrvTable == nullptr || count == 0 || count > kTableDescriptors ) {
        return 0xFFFFFFFFu;
    }
    const uint32_t slot = pDevice->getFrameSlot();
    if ( slot != srvFrameSlot || srvCursor + count > kTableDescriptors ) {
        srvCursor = 0;
        srvFrameSlot = slot;
    }
    const uint32_t index = static_cast<uint32_t>( srvCursor );
    srvCursor += count;
    return index;
}

void D3D12Scene::dumpShader( const char *pName, const D3D12ShaderStage &stage ) {
    if ( shaderDumpDir.empty() || stage.HLSL.empty() ) {
        return;
    }
    std::string name = pName != nullptr ? pName : "shader";
    for ( char &c : name ) {
        if ( c == ':' || c == '/' || c == '\\' || c == ' ' || c == '\t' ) {
            c = '_';
        }
    }
    const std::string path = shaderDumpDir + "/" + name + ".hlsl";
    FILE *pFile = fopen( path.c_str(), "wb" );
    if ( pFile == nullptr ) {
        OTDU_LOG_WARN( "D3D12 scene: cannot write '%s'\n", path.c_str() );
        return;
    }
    fwrite( stage.HLSL.data(), 1, stage.HLSL.size(), pFile );
    fclose( pFile );
    dumpedShaders++;
}

const void *D3D12Scene::getPipeline( const PipelineDesc &desc ) {
    if ( !isReady() || desc.pVS == nullptr || desc.pPS == nullptr || !desc.pVS->bReady ||
         !desc.pPS->bReady || desc.NumStreams == 0 || desc.NumAttributes == 0 ) {
        return nullptr;
    }
    const std::string key = MakePipelineKey( *desc.pVS, *desc.pPS, desc );
    auto it = pipelines.find( key );
    if ( it != pipelines.end() ) {
        return it->second;
    }

    ID3D12Device *pD3D12Device = pDevice->getDevice();
    std::vector<D3D12_INPUT_ELEMENT_DESC> layout( desc.NumAttributes );
    for ( uint32_t i = 0; i < desc.NumAttributes; i++ ) {
        const Attribute &attribute = desc.pAttributes[i];
        layout[i].SemanticName = D3D12ShaderCompiler::vertexSemantic( attribute.Location );
        layout[i].SemanticIndex = D3D12ShaderCompiler::vertexSemanticIndex( attribute.Location );
        layout[i].Format = static_cast<DXGI_FORMAT>( attribute.Format );
        layout[i].InputSlot = attribute.Stream;
        layout[i].AlignedByteOffset = attribute.Offset;
        layout[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        layout[i].InstanceDataStepRate = 0;
    }

    D3D12_BLEND_DESC blend = {};
    blend.RenderTarget[0].LogicOpEnable = FALSE;
    blend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    if ( desc.pState != nullptr ) {
        // A D3D9 material: the factors and the op are its own render states.
        // Alpha *testing* is not here - DXSO's pixel shader already discards
        // (docs/backends.md section 6), driven by the spec constant block and the
        // push constant alpha reference.
        blend.RenderTarget[0].BlendEnable = desc.pState->isBlended() ? TRUE : FALSE;
        blend.RenderTarget[0].SrcBlend = ToD3D12Blend( desc.pState->SrcBlend );
        blend.RenderTarget[0].DestBlend = ToD3D12Blend( desc.pState->DstBlend );
        blend.RenderTarget[0].BlendOp = ToD3D12BlendOp( desc.pState->BlendOp );
        // D3D9 has one alpha blend state; the Vulkan port keeps ONE /
        // INV_SRC_ALPHA there, and so does this (the colour factors do not carry
        // over: a D3D9 material has no separate alpha pair).
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        const uint8_t mask = desc.pState->ColorWriteMask;
        blend.RenderTarget[0].RenderTargetWriteMask =
            ( ( mask & 1 ) ? D3D12_COLOR_WRITE_ENABLE_RED : 0 ) |
            ( ( mask & 2 ) ? D3D12_COLOR_WRITE_ENABLE_GREEN : 0 ) |
            ( ( mask & 4 ) ? D3D12_COLOR_WRITE_ENABLE_BLUE : 0 ) |
            ( ( mask & 8 ) ? D3D12_COLOR_WRITE_ENABLE_ALPHA : 0 );
    } else {
        blend.RenderTarget[0].BlendEnable = desc.Blend != eBlendMode::BM_Opaque;
        switch ( desc.Blend ) {
        case eBlendMode::BM_Alpha:
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            break;
        case eBlendMode::BM_PremultipliedAlpha:
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            break;
        case eBlendMode::BM_Additive:
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            break;
        case eBlendMode::BM_Opaque:
        default:
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
            break;
        }
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    D3D12_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    if ( desc.pState != nullptr ) {
        // EdenRenderer::getPipeline's mapping, mirrored: its data uses 1 / 3 for
        // "no culling", 4 for the normal back-face cull and 2 for the front-face
        // cull, and a mirroring world matrix swaps the last two. D3D9 and D3D12
        // both decide the front face in screen space (Y down), so clockwise front
        // faces is the same FrontCounterClockwise = FALSE the Vulkan path gets
        // from its flipped viewport.
        uint8_t cull = desc.pState->CullMode;
        if ( cull == 3 ) {
            cull = 1;
        }
        if ( desc.pState->bMirrored && ( cull == 2 || cull == 4 ) ) {
            cull = cull == 2 ? 4 : 2;
        }
        rasterizer.CullMode = cull == 4   ? D3D12_CULL_MODE_BACK
                              : cull == 2 ? D3D12_CULL_MODE_FRONT
                                          : D3D12_CULL_MODE_NONE;
        rasterizer.FrontCounterClockwise = FALSE;
    } else {
        rasterizer.CullMode = desc.Cull == eCullMode::CM_Back     ? D3D12_CULL_MODE_BACK
                              : desc.Cull == eCullMode::CM_Front ? D3D12_CULL_MODE_FRONT
                                                                 : D3D12_CULL_MODE_NONE;
        // Direct3D decides the front face in screen space, where Y points down, so
        // a Vulkan pipeline that calls counter-clockwise "front" maps to
        // FrontCounterClockwise = FALSE (docs/backends.md section 6).
        rasterizer.FrontCounterClockwise = desc.bFrontFaceCW ? TRUE : FALSE;
    }
    rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizer.DepthClipEnable = TRUE;
    rasterizer.MultisampleEnable = FALSE;
    rasterizer.AntialiasedLineEnable = FALSE;

    D3D12_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = desc.bDepthTest ? TRUE : FALSE;
    depth.DepthWriteMask = desc.bDepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    // D3DCMPFUNC of the material (TD_ShadowBox uses GREATEREQUAL); the
    // non-D3D9 path keeps LESSEQUAL, the default.
    depth.DepthFunc = desc.pState != nullptr ? ToD3D12Comparison( desc.pState->DepthFunc )
                                             : D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depth.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = static_cast<ID3D12RootSignature *>( pRootSignature );
    pso.VS.pShaderBytecode = desc.pVS->Bytecode.data();
    pso.VS.BytecodeLength = desc.pVS->Bytecode.size();
    pso.PS.pShaderBytecode = desc.pPS->Bytecode.data();
    pso.PS.BytecodeLength = desc.pPS->Bytecode.size();
    pso.BlendState = blend;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = rasterizer;
    pso.DepthStencilState = depth;
    pso.InputLayout.pInputElementDescs = layout.data();
    pso.InputLayout.NumElements = static_cast<UINT>( layout.size() );
    pso.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = static_cast<DXGI_FORMAT>( D3D12Device::kSwapchainFormatDXGI );
    pso.DSVFormat = static_cast<DXGI_FORMAT>( D3D12Device::kDepthStencilFormatDXGI );
    pso.SampleDesc.Count = 1;
    pso.NodeMask = 0;

    ID3D12PipelineState *pState = nullptr;
    if ( Logger::IsVerbose() ) {
        std::string layoutText;
        char element[96];
        for ( uint32_t i = 0; i < desc.NumAttributes; i++ ) {
            snprintf( element, sizeof( element ), "%s%u:s%u+f%u/%u ", layout[i].SemanticName,
                      layout[i].SemanticIndex, layout[i].InputSlot, layout[i].Format,
                      layout[i].AlignedByteOffset );
            layoutText += element;
        }
        std::string strides;
        for ( uint32_t i = 0; i < desc.NumStreams; i++ ) {
            strides += ( i > 0 ? "," : "" ) + std::to_string( desc.pStrides[i] );
        }
        OTDU_LOG_DEBUG( "D3D12 scene: creating pipeline (%u attribute(s) [%s], stride %s, vs %zu B, ps %zu B, "
                        "blend %d, cull %d, depth %d/%d)\n",
                        desc.NumAttributes, layoutText.c_str(), strides.c_str(), desc.pVS->Bytecode.size(),
                        desc.pPS->Bytecode.size(), static_cast<int>( desc.pState != nullptr ),
                        static_cast<int>( desc.pState != nullptr ? desc.pState->CullMode : 0 ),
                        desc.bDepthTest ? 1 : 0, desc.bDepthWrite ? 1 : 0 );
    }
    const HRESULT result = pD3D12Device->CreateGraphicsPipelineState( &pso, IID_PPV_ARGS( &pState ) );
    if ( FAILED( result ) ) {
        // The device's own message is the first thing to read (the debug layer's
        // errors are kept now - see D3D12Device::flushDebugMessages); the
        // varying lists come next, because a VS/PS slot mismatch is otherwise
        // an E_INVALIDARG with nothing to point at (docs/backends.md 3.9).
        OTDU_LOG_ERROR( "D3D12 scene: CreateGraphicsPipelineState failed (0x%08x); %s\n",
                        static_cast<uint32_t>( result ),
                        D3D12ShaderCompiler::describeVaryings( *desc.pVS, *desc.pPS ).c_str() );
        pDevice->flushPendingDebugMessages();
        return nullptr;
    }
    pipelines[key] = pState;

    if ( !shaderDumpDir.empty() ) {
        const std::string base = "pipeline" + std::to_string( pipelines.size() );
        dumpShader( ( base + "_vs" ).c_str(), *desc.pVS );
        dumpShader( ( base + "_ps" ).c_str(), *desc.pPS );
    }
    OTDU_LOG_INFO( "D3D12 scene: pipeline %u (%u attributes, %u stream(s), %s, depth %s%s)\n",
                   static_cast<uint32_t>( pipelines.size() ), desc.NumAttributes, desc.NumStreams,
                   desc.pState != nullptr ? ( desc.pState->isBlended() ? "D3D9 blend" : "D3D9 opaque" )
                                          : ( desc.Blend != eBlendMode::BM_Opaque ? "blend" : "opaque" ),
                   desc.bDepthTest ? "test" : "off", desc.bDepthWrite ? "+write" : "" );
    return pState;
}

bool D3D12Scene::createMesh( const void *pVertexData,
                             uint32_t vertexBytes,
                             uint32_t vertexStride,
                             const uint32_t *pIndices,
                             uint32_t numIndices,
                             Mesh &mesh ) {
    mesh = Mesh();
    if ( pDevice == nullptr || pVertexData == nullptr || vertexBytes == 0 || vertexStride == 0 ||
         pIndices == nullptr || numIndices == 0 ) {
        return false;
    }
    ID3D12Device *pD3D12Device = pDevice->getDevice();

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    auto createBuffer = [&]( uint64_t size, const void *pData, ID3D12Resource *&pResource ) {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        // An upload heap vertex buffer is written once and read by the GPU for
        // the rest of the run: no copy, no transition. The world's streaming
        // buffers will want a default heap copy instead.
        if ( FAILED( pD3D12Device->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                            IID_PPV_ARGS( &pResource ) ) ) ) {
            return false;
        }
        void *pMapped = nullptr;
        if ( FAILED( pResource->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr ) {
            return false;
        }
        memcpy( pMapped, pData, static_cast<size_t>( size ) );
        pResource->Unmap( 0, nullptr );
        return true;
    };

    ID3D12Resource *pVertexBuffer = nullptr;
    ID3D12Resource *pIndexBuffer = nullptr;
    const uint64_t indexBytes = static_cast<uint64_t>( numIndices ) * sizeof( uint32_t );
    if ( !createBuffer( vertexBytes, pVertexData, pVertexBuffer ) ||
         !createBuffer( indexBytes, pIndices, pIndexBuffer ) ) {
        SafeRelease( pVertexBuffer );
        SafeRelease( pIndexBuffer );
        OTDU_LOG_WARN( "D3D12 scene: mesh upload failed (%u bytes of vertices, %u indices)\n", vertexBytes,
                       numIndices );
        return false;
    }
    mesh.pVertexBuffer = pVertexBuffer;
    mesh.pIndexBuffer = pIndexBuffer;
    mesh.NumIndices = numIndices;
    mesh.VertexStride = vertexStride;
    mesh.VertexBytes = vertexBytes;
    return true;
}

void D3D12Scene::destroyMesh( Mesh &mesh ) {
    SafeRelease( reinterpret_cast<ID3D12Resource *&>( mesh.pVertexBuffer ) );
    SafeRelease( reinterpret_cast<ID3D12Resource *&>( mesh.pIndexBuffer ) );
    mesh.NumIndices = 0;
    mesh.VertexStride = 0;
    mesh.VertexBytes = 0;
}




void D3D12Scene::draw( const void *pPipeline,
                       const Mesh &mesh,
                       const void *pConstants,
                       uint32_t constantsBytes,
                       const uint64_t *pTextureSrvs,
                       uint32_t numTextures,
                       const StageConstants *pVSConstants,
                       const StageConstants *pPSConstants,
                       float blendFactor ) {
    if ( pDevice == nullptr || pPipeline == nullptr || mesh.pVertexBuffer == nullptr ||
         mesh.pIndexBuffer == nullptr || mesh.NumIndices == 0 ) {
        return;
    }
    ID3D12GraphicsCommandList *pList = pDevice->getCommandList();
    // The root signature comes first: a descriptor table set before it is an
    // error (D3D12 error #708), and the driver does not always survive it.
    pList->SetGraphicsRootSignature( static_cast<ID3D12RootSignature *>( pRootSignature ) );

    // The constant buffers, b0..b3 of each stage: copied into this frame slot's
    // ring now, because the caller's bytes are CPU side and may be gone by the
    // time the GPU reads them. A block the caller does not fill binds the zero
    // block - a root CBV cannot be null, and a shader that does not read it does
    // not care.
    uint64_t vsGpu[kMaxConstantBuffers] = {};
    uint64_t psGpu[kMaxConstantBuffers] = {};
    uploadConstants( pVSConstants, vsGpu );
    uploadConstants( pPSConstants, psGpu );
    for ( uint32_t i = 0; i < kMaxConstantBuffers; i++ ) {
        pList->SetGraphicsRootConstantBufferView( i, vsGpu[i] );
        pList->SetGraphicsRootConstantBufferView( kMaxConstantBuffers + i, psGpu[i] );
    }
    // D3D12's blend factor is dynamic state, not pipeline state (D3DBLENDFACTOR /
    // D3DBLEND_INVBLENDFACTOR read it); it is set for every draw, which is what
    // makes a 4-byte change not a new pipeline.
    const FLOAT factor[4] = { blendFactor, blendFactor, blendFactor, blendFactor };
    pList->OMSetBlendFactor( factor );

    // Both heaps, every draw: the 2D layer and ImGui each bind a heap of their
    // own, so the set this draw needs has to be re-established here (a table is
    // only valid while its heap is bound). D3D12 allows one shader-visible heap
    // of each type at a time, which is exactly these two.
    ID3D12DescriptorHeap *ppHeaps[2] = {};
    uint32_t numHeaps = 0;
    if ( pSrvTable != nullptr ) {
        ppHeaps[numHeaps++] = static_cast<ID3D12DescriptorHeap *>( pSrvTable );
    }
    if ( pSamplerHeap != nullptr ) {
        ppHeaps[numHeaps++] = static_cast<ID3D12DescriptorHeap *>( pSamplerHeap );
    }
    if ( numHeaps > 0 ) {
        pList->SetDescriptorHeaps( numHeaps, ppHeaps );
    }

    // The texture table: a material's SRVs are at unrelated offsets in the
    // device's shader-visible heap, and a descriptor table has to be
    // contiguous. The copies come from the shader-opaque mirror of those
    // descriptors (a shader-visible heap is not a legal copy source).
    if ( numTextures > 0 && pTextureSrvs != nullptr && pSrvTable != nullptr ) {
        const uint32_t count = Min( numTextures, kTextureTableSize );
        const uint32_t index = allocateTable( count );
        if ( index == 0xFFFFFFFFu ) {
            return;
        }
        ID3D12Device *pD3D12Device = pDevice->getDevice();
        const D3D12_CPU_DESCRIPTOR_HANDLE base = CpuHandle( srvTableCpuStart + index * srvDescriptorSize );
        for ( uint32_t i = 0; i < count; i++ ) {
            const D3D12_CPU_DESCRIPTOR_HANDLE source = CpuHandle( pTextureSrvs[i] );
            const D3D12_CPU_DESCRIPTOR_HANDLE destination =
                CpuHandle( base.ptr + static_cast<SIZE_T>( i ) * srvDescriptorSize );
            pD3D12Device->CopyDescriptorsSimple( 1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }
        pList->SetGraphicsRootDescriptorTable( kMaxConstantBuffers * 2 + 1,
                                               GpuHandle( srvTableGpuStart + index * srvDescriptorSize ) );
    }

    // The sampler heap: the same table for every draw. Which entry a sample uses
    // is the shader's business (it indexes the array with a push constant).
    if ( pSamplerHeap != nullptr ) {
        pList->SetGraphicsRootDescriptorTable( kMaxConstantBuffers * 2 + 2, GpuHandle( samplerHeapGpuStart ) );
    }

    if ( pConstants != nullptr && constantsBytes > 0 ) {
        const uint32_t dwords = Min<uint32_t>( constantsBytes / 4, kMaxRootConstants );
        pList->SetGraphicsRoot32BitConstants( kMaxConstantBuffers * 2, dwords, pConstants, 0 );
    }
    pList->SetPipelineState( static_cast<ID3D12PipelineState *>( const_cast<void *>( pPipeline ) ) );

    ID3D12Resource *pVertex = static_cast<ID3D12Resource *>( mesh.pVertexBuffer );
    ID3D12Resource *pIndex = static_cast<ID3D12Resource *>( mesh.pIndexBuffer );
    const D3D12_VERTEX_BUFFER_VIEW vertexView = { pVertex->GetGPUVirtualAddress(),
                                                  mesh.VertexBytes != 0 ? mesh.VertexBytes
                                                                        : mesh.VertexStride,
                                                  static_cast<UINT>( mesh.VertexStride ) };
    pList->IASetVertexBuffers( 0, 1, &vertexView );
    const D3D12_INDEX_BUFFER_VIEW indexView = { pIndex->GetGPUVirtualAddress(),
                                                static_cast<UINT>( mesh.NumIndices * sizeof( uint32_t ) ),
                                                DXGI_FORMAT_R32_UINT };
    pList->IASetIndexBuffer( &indexView );
    pList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    pList->DrawIndexedInstanced( mesh.NumIndices, 1, 0, 0, 0 );
}


// -d3d12-scenetest: the whole scene path without game data (docs/backends.md
// section 3.3). The engine's own mesh.vert/mesh.frag - the same SPIR-V the
// Vulkan renderer draws .3DG geometry with - go through SPIRV-Cross and
// D3DCompile, a textured, lit, depth tested cube is uploaded and drawn, and the
// frame is presented. A black frame here is a pipeline problem, not a movie.
bool D3D12RunSceneTest( D3D12Device &device, uint32_t frames, const ColorRGBA &clearColor ) {
    struct Vertex {
        float Position[3];
        float Normal[3];
        float Texcoord[2];
        float Color[4];
    };
    constexpr uint32_t kDXGI_R32G32B32A32_FLOAT = 2;
    constexpr uint32_t kDXGI_R32G32B32_FLOAT = 6;
    constexpr uint32_t kDXGI_R32G32_FLOAT = 16;
    constexpr uint32_t kDXGI_R8G8B8A8_UNORM = 28;

    // A cube: 24 vertices (4 per face, so each face has its own normal and UVs)
    // and 36 indices. Each face is a normal plus the two axes that span it, in
    // the order that makes U x V the normal - i.e. counter-clockwise seen from
    // outside.
    struct Face {
        float Normal[3];
        float U[3];
        float V[3];
    };
    const Face kFaces[6] = {
        { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },  { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } }, { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } }, { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
    };
    const float kHalf = 0.6f;
    Vertex vertices[24];
    for ( uint32_t face = 0; face < 6; face++ ) {
        const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for ( uint32_t corner = 0; corner < 4; corner++ ) {
            const float u = corners[corner][0];
            const float v = corners[corner][1];
            Vertex &vertex = vertices[face * 4 + corner];
            for ( uint32_t axis = 0; axis < 3; axis++ ) {
                vertex.Position[axis] = kFaces[face].Normal[axis] * kHalf +
                                        kFaces[face].U[axis] * ( u * kHalf ) +
                                        kFaces[face].V[axis] * ( v * kHalf );
                vertex.Normal[axis] = kFaces[face].Normal[axis];
            }
            vertex.Texcoord[0] = u * 0.5f + 0.5f;
            vertex.Texcoord[1] = 1.0f - ( v * 0.5f + 0.5f );
            vertex.Color[0] = 1.0f;
            vertex.Color[1] = 1.0f;
            vertex.Color[2] = 1.0f;
            vertex.Color[3] = 1.0f;
        }
    }
    uint32_t indices[36];
    for ( uint32_t face = 0; face < 6; face++ ) {
        const uint32_t base = face * 4;
        const uint32_t pattern[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
        memcpy( &indices[face * 6], pattern, sizeof( pattern ) );
    }

    D3D12Scene scene;
    if ( !scene.initialize( &device ) ) {
        OTDU_LOG_ERROR( "D3D12 scene test: the scene could not be initialised\n" );
        return false;
    }
    if ( const char *pDump = CmdLine::GetArgValue( "-d3d12-dump-shaders" ) ) {
        scene.setShaderDumpDir( pDump );
    }

    D3D12ShaderStage vs;
    D3D12ShaderStage ps;
    TranslatedShader vsShader;
    TranslatedShader psShader;
    vsShader.Stage = eShaderType::ST_Vertex;
    vsShader.SPIRV.assign( kSPIRV_mesh_vert, kSPIRV_mesh_vert + sizeof( kSPIRV_mesh_vert ) / sizeof( uint32_t ) );
    psShader.Stage = eShaderType::ST_Pixel;
    psShader.SPIRV.assign( kSPIRV_mesh_frag, kSPIRV_mesh_frag + sizeof( kSPIRV_mesh_frag ) / sizeof( uint32_t ) );
    if ( !D3D12ShaderCompiler::compile( vsShader, vs, "mesh.vert" ) ||
         !D3D12ShaderCompiler::compile( psShader, ps, "mesh.frag" ) ) {
        OTDU_LOG_ERROR( "D3D12 scene test: shader compile failed: %s %s\n", vs.Error.c_str(), ps.Error.c_str() );
        return false;
    }
    OTDU_LOG_INFO( "D3D12 scene test: mesh.vert -> %u B DXBC (%u root constants), mesh.frag -> %u B DXBC "
                   "(%u texture(s), %u sampler(s))\n",
                   static_cast<uint32_t>( vs.Bytecode.size() ), vs.RootConstants,
                   static_cast<uint32_t>( ps.Bytecode.size() ), ps.NumTextures, ps.NumSamplers );

    D3D12Scene::Attribute attributes[4] = {
        { 0, 0, kDXGI_R32G32B32_FLOAT, 0 },
        { 1, 0, kDXGI_R32G32B32_FLOAT, 12 },
        { 2, 0, kDXGI_R32G32_FLOAT, 24 },
        { 3, 0, kDXGI_R32G32B32A32_FLOAT, 32 },
    };
    const uint32_t stride = sizeof( Vertex );
    D3D12Scene::PipelineDesc pipelineDesc;
    pipelineDesc.pVS = &vs;
    pipelineDesc.pPS = &ps;
    pipelineDesc.pAttributes = attributes;
    pipelineDesc.NumAttributes = 4;
    pipelineDesc.pStrides = &stride;
    pipelineDesc.NumStreams = 1;
    pipelineDesc.Blend = eBlendMode::BM_Opaque;
    // Both faces: the winding convention is settled when Eden's meshes are
    // ported (docs/backends.md section 6) and it must not be the reason this
    // test is black.
    pipelineDesc.Cull = eCullMode::CM_None;
    pipelineDesc.bDepthTest = true;
    pipelineDesc.bDepthWrite = true;
    const void *pPipeline = scene.getPipeline( pipelineDesc );
    if ( pPipeline == nullptr ) {
        OTDU_LOG_ERROR( "D3D12 scene test: the pipeline could not be created\n" );
        return false;
    }

    D3D12Scene::Mesh mesh;
    if ( !scene.createMesh( vertices, sizeof( vertices ), stride, indices, 36, mesh ) ) {
        OTDU_LOG_ERROR( "D3D12 scene test: the cube could not be uploaded\n" );
        return false;
    }

    // A 64x64 checker with a colour ramp: a flat texture would hide a missing UV
    // or a broken sampler.
    std::vector<uint8_t> pixels( 64 * 64 * 4 );
    for ( uint32_t y = 0; y < 64; y++ ) {
        for ( uint32_t x = 0; x < 64; x++ ) {
            const bool bLight = ( ( x / 8 ) + ( y / 8 ) ) % 2 == 0;
            uint8_t *pPixel = &pixels[( y * 64 + x ) * 4];
            pPixel[0] = static_cast<uint8_t>( bLight ? 220 : 40 + x * 3 );
            pPixel[1] = static_cast<uint8_t>( bLight ? 200 : 40 + y * 3 );
            pPixel[2] = static_cast<uint8_t>( bLight ? 160 : 200 - x * 2 );
            pPixel[3] = 255;
        }
    }
    D3D12Device::Texture texture = {};
    const void *pMip = pixels.data();
    const uint32_t mipSize = static_cast<uint32_t>( pixels.size() );
    if ( !device.createTexture2D( 64, 64, 1, kDXGI_R8G8B8A8_UNORM, &pMip, &mipSize, texture, "scenetest" ) ) {
        OTDU_LOG_ERROR( "D3D12 scene test: the checker texture could not be uploaded\n" );
        return false;
    }


    // The video setting is the swapchain's size; the cube is framed for a 16:9
    // window either way.
    const float aspect = device.getBackbufferHeight() > 0
                             ? static_cast<float>( device.getBackbufferWidth() ) /
                                   static_cast<float>( device.getBackbufferHeight() )
                             : 16.0f / 9.0f;
    const float kNear = 0.1f;
    const float kFar = 100.0f;
    const float f = 1.0f / std::tan( 0.5f * 0.8726646f ); // 50 degrees vertical
    float projection[16] = {};
    projection[0] = f / aspect;
    projection[5] = f;
    projection[10] = kFar / ( kNear - kFar );
    projection[11] = -1.0f;
    projection[14] = kNear * kFar / ( kNear - kFar );

    // Off the axes on purpose: three faces visible, each with its own normal, so
    // the frame proves the vertex layout, the lighting and the depth test rather
    // than one flat quad.
    const float eye[3] = { 2.3f, 1.75f, -2.7f };
    const float target[3] = { 0.0f, 0.0f, 0.0f };
    float forward[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    const float forwardLength =
        std::sqrt( forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2] );
    for ( float &component : forward ) {
        component /= forwardLength;
    }
    const float up[3] = { 0.0f, 1.0f, 0.0f };
    float right[3] = { forward[1] * up[2] - forward[2] * up[1], forward[2] * up[0] - forward[0] * up[2],
                       forward[0] * up[1] - forward[1] * up[0] };
    const float rightLength = std::sqrt( right[0] * right[0] + right[1] * right[1] + right[2] * right[2] );
    for ( float &component : right ) {
        component /= rightLength;
    }
    const float cameraUp[3] = { right[1] * forward[2] - right[2] * forward[1],
                                right[2] * forward[0] - right[0] * forward[2],
                                right[0] * forward[1] - right[1] * forward[0] };
    float view[16] = {};
    view[0] = right[0];
    view[1] = cameraUp[0];
    view[2] = -forward[0];
    view[4] = right[1];
    view[5] = cameraUp[1];
    view[6] = -forward[1];
    view[8] = right[2];
    view[9] = cameraUp[2];
    view[10] = -forward[2];
    view[12] = -( right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2] );
    view[13] = -( cameraUp[0] * eye[0] + cameraUp[1] * eye[1] + cameraUp[2] * eye[2] );
    view[14] = forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2];
    view[15] = 1.0f;

    // The push constant block mesh.vert/mesh.frag declare: mat4 + 4 vec4 = 128
    // bytes, exactly RenderDevice::kPushConstantsSize.
    struct SceneConstants {
        float ViewProjection[16];
        float ModelRow0[4];
        float ModelRow1[4];
        float ModelRow2[4];
        float MaterialColor[4];
    };
    SceneConstants constants = {};
    for ( uint32_t column = 0; column < 4; column++ ) {
        for ( uint32_t row = 0; row < 4; row++ ) {
            float sum = 0.0f;
            for ( uint32_t k = 0; k < 4; k++ ) {
                sum += projection[k * 4 + row] * view[column * 4 + k];
            }
            constants.ViewProjection[column * 4 + row] = sum;
        }
    }
    const float kModelRows[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    memcpy( constants.ModelRow0, &kModelRows[0], sizeof( constants.ModelRow0 ) );
    memcpy( constants.ModelRow1, &kModelRows[4], sizeof( constants.ModelRow1 ) );
    memcpy( constants.ModelRow2, &kModelRows[8], sizeof( constants.ModelRow2 ) );
    // rgb: tint, a: flags - 1 = use the vertex colour.
    constants.MaterialColor[0] = 1.0f;
    constants.MaterialColor[1] = 1.0f;
    constants.MaterialColor[2] = 1.0f;
    constants.MaterialColor[3] = 1.0f;

    bool bQuit = false;
    uint32_t presented = 0;
    for ( uint32_t i = 0; i < frames && !bQuit; i++ ) {
        SDL_Event event;
        while ( SDL_PollEvent( &event ) ) {
            if ( event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ) {
                bQuit = true;
            }
        }
        if ( bQuit || !device.beginFrame( clearColor ) ) {
            break;
        }
        scene.draw( pPipeline, mesh, &constants, sizeof( constants ), &texture.SrvStorageCpu, 1 );
        device.endFrame();
        presented++;
        if ( device.hasDeviceLost() ) {
            break;
        }
    }
    device.waitIdle();

    scene.destroyMesh( mesh );
    device.destroyTexture( texture );
    scene.destroy();

    OTDU_LOG_INFO( "D3D12 scene test: %u frame(s) of a 36 index cube on '%s' (%ux%u)%s\n", presented,
                   device.getDeviceName(), device.getBackbufferWidth(), device.getBackbufferHeight(),
                   device.hasDeviceLost() ? " - DEVICE LOST" : "" );
    return presented > 0 && !device.hasDeviceLost();
}

// -d3d12-samplertest (docs/backends.md section 4): the sampler heap, proved.
//
// The scene test draws through a *static* sampler register; an Eden draw does
// not - DXSO hands it `sampler_heap[]` and the shader picks the entry with a
// value from the push constant block. The probe here is the same access in
// hand-written HLSL: one 2x1 two-colour texture, the same UVs in [0,2] on both
// quads, and the only difference between the two draws is the heap index -
// linear wrap (0) on the left, linear clamp/clamp (SH_LinearUV + 8, 13) on the
// right. With wrap the texture repeats twice across the UV range, with clamp
// the last texel stretches, so the halves' mean colours differ by ~47 levels on
// R and B; a heap index that never reaches the sampler would make them equal.
bool D3D12RunSamplerTest( D3D12Device &device, uint32_t frames, const ColorRGBA &clearColor ) {
    constexpr uint32_t kDXGI_R32G32_FLOAT = 16;
    constexpr uint32_t kDXGI_R8G8B8A8_UNORM = 28;

    // Both stages declare the block the scene binds at b13 space1 - two dwords
    // are used, the other two pad the constant buffer to 16 bytes.
    static const char *kProbeHLSLHeader =
        "cbuffer ProbeConstants : register(b13, space1)\n"
        "{\n"
        "    uint gSamplerIndex;\n"
        "    float gOffsetX;\n"
        "    float2 gUnused;\n"
        "};\n";
    static const char *kProbeVSBody =
        "struct VSIn  { float2 pos : TEXCOORD0; float2 uv : TEXCOORD1; };\n"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VSOut main( VSIn input )\n"
        "{\n"
        "    VSOut output;\n"
        "    output.pos = float4( input.pos.x * 0.45 + gOffsetX, input.pos.y * 0.45, 0.5, 1.0 );\n"
        "    output.uv = input.uv;\n"
        "    return output;\n"
        "}\n";
    // The dynamic-index access itself: exactly what SPIRV-Cross emits for DXSO's
    // runtime sampler array (`NonUniformResourceIndex` included).
    static const char *kProbePSBody =
        "Texture2D<float4> gTexture : register(t0);\n"
        "SamplerState gSamplerHeap[] : register(s0, space0);\n"
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main( PSIn input ) : SV_Target\n"
        "{\n"
        "    return gTexture.Sample( gSamplerHeap[NonUniformResourceIndex( gSamplerIndex )], input.uv );\n"
        "}\n";
    const std::string vsSource = std::string( kProbeHLSLHeader ) + kProbeVSBody;
    const std::string psSource = std::string( kProbeHLSLHeader ) + kProbePSBody;

    D3D12Scene scene;
    if ( !scene.initialize( &device ) ) {
        OTDU_LOG_ERROR( "D3D12 sampler test: the scene could not be initialised\n" );
        return false;
    }
    if ( const char *pDump = CmdLine::GetArgValue( "-d3d12-dump-shaders" ) ) {
        scene.setShaderDumpDir( pDump );
    }

    D3D12ShaderStage vs;
    D3D12ShaderStage ps;
    if ( !D3D12ShaderCompiler::compileHlsl( vsSource.c_str(), eShaderType::ST_Vertex, vs, "sampler_probe.vert" ) ||
         !D3D12ShaderCompiler::compileHlsl( psSource.c_str(), eShaderType::ST_Pixel, ps, "sampler_probe.frag" ) ) {
        OTDU_LOG_ERROR( "D3D12 sampler test: shader compile failed: %s %s\n", vs.Error.c_str(),
                        ps.Error.c_str() );
        scene.destroy();
        return false;
    }

    struct ProbeVertex {
        float Position[2];
        float Texcoord[2];
    };
    const ProbeVertex quad[4] = {
        { { -1.0f, -1.0f }, { 0.0f, 1.0f } },
        { { -1.0f, 1.0f }, { 0.0f, 0.0f } },
        { { 1.0f, 1.0f }, { 2.0f, 0.0f } },
        { { 1.0f, -1.0f }, { 2.0f, 1.0f } },
    };
    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    const uint32_t stride = sizeof( ProbeVertex );

    D3D12Scene::Attribute attributes[2] = {
        { 0, 0, kDXGI_R32G32_FLOAT, 0 },
        { 1, 0, kDXGI_R32G32_FLOAT, 8 },
    };
    D3D12Scene::PipelineDesc pipelineDesc;
    pipelineDesc.pVS = &vs;
    pipelineDesc.pPS = &ps;
    pipelineDesc.pAttributes = attributes;
    pipelineDesc.NumAttributes = 2;
    pipelineDesc.pStrides = &stride;
    pipelineDesc.NumStreams = 1;
    pipelineDesc.Blend = eBlendMode::BM_Opaque;
    pipelineDesc.Cull = eCullMode::CM_None;
    pipelineDesc.bDepthTest = true;
    pipelineDesc.bDepthWrite = true;
    const void *pPipeline = scene.getPipeline( pipelineDesc );
    if ( pPipeline == nullptr ) {
        OTDU_LOG_ERROR( "D3D12 sampler test: the pipeline could not be created\n" );
        scene.destroy();
        return false;
    }

    D3D12Scene::Mesh mesh;
    if ( !scene.createMesh( quad, sizeof( quad ), stride, indices, 6, mesh ) ) {
        OTDU_LOG_ERROR( "D3D12 sampler test: the quad could not be uploaded\n" );
        scene.destroy();
        return false;
    }

    // Two texels, two clearly different colours: wrap shows each one for half of
    // the [0,2] UV range, clamp stretches the second one over three quarters.
    const uint8_t texels[2 * 4] = { 230, 40, 40, 255, 40, 40, 230, 255 };
    D3D12Device::Texture texture = {};
    const void *pMip = texels;
    const uint32_t mipSize = sizeof( texels );
    if ( !device.createTexture2D( 2, 1, 1, kDXGI_R8G8B8A8_UNORM, &pMip, &mipSize, texture,
                                  "sampler_probe" ) ) {
        OTDU_LOG_ERROR( "D3D12 sampler test: the texture could not be uploaded\n" );
        scene.destroyMesh( mesh );
        scene.destroy();
        return false;
    }

    struct ProbeConstants {
        uint32_t SamplerIndex;
        float OffsetX;
        uint32_t Pad0;
        uint32_t Pad1;
    };
    const ProbeConstants left = { SH_LinearWrap, -0.5f, 0, 0 };
    const ProbeConstants right = { SH_LinearUV + 8, 0.5f, 0, 0 }; // clamp / clamp

    bool bQuit = false;
    uint32_t presented = 0;
    for ( uint32_t i = 0; i < frames && !bQuit; i++ ) {
        SDL_Event event;
        while ( SDL_PollEvent( &event ) ) {
            if ( event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ) {
                bQuit = true;
            }
        }
        if ( bQuit || !device.beginFrame( clearColor ) ) {
            break;
        }
        scene.draw( pPipeline, mesh, &left, sizeof( left ), &texture.SrvStorageCpu, 1 );
        scene.draw( pPipeline, mesh, &right, sizeof( right ), &texture.SrvStorageCpu, 1 );
        if ( i + 1 == frames ) {
            // endFrame copies the frame it just recorded (before Present).
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

    scene.destroyMesh( mesh );
    device.destroyTexture( texture );
    scene.destroy();

    if ( presented == 0 || device.hasDeviceLost() ) {
        OTDU_LOG_ERROR( "D3D12 sampler test: no frame was presented%s\n",
                        device.hasDeviceLost() ? " (DEVICE LOST)" : "" );
        return false;
    }
    if ( pixels.empty() ) {
        OTDU_LOG_ERROR( "D3D12 sampler test: the frame could not be read back\n" );
        return false;
    }

    // Measure the two halves over the pixels the quads cover: anything far from
    // the clear colour is a quad pixel (the texture's colours are saturated).
    const uint32_t pixelWidth = device.getBackbufferWidth();
    const uint32_t pixelHeight = device.getBackbufferHeight();
    const int background[3] = { static_cast<int>( clearColor.r * 255.0f + 0.5f ),
                                static_cast<int>( clearColor.g * 255.0f + 0.5f ),
                                static_cast<int>( clearColor.b * 255.0f + 0.5f ) };
    double sum[2][3] = {};
    uint32_t covered[2] = {};
    for ( uint32_t y = 0; y < pixelHeight; y++ ) {
        for ( uint32_t x = 0; x < pixelWidth; x++ ) {
            const uint8_t *pPixel = &pixels[( static_cast<size_t>( y ) * pixelWidth + x ) * 4];
            const int dr = static_cast<int>( pPixel[0] ) - background[0];
            const int dg = static_cast<int>( pPixel[1] ) - background[1];
            const int db = static_cast<int>( pPixel[2] ) - background[2];
            if ( dr * dr + dg * dg + db * db < 12 * 12 ) {
                continue;
            }
            const uint32_t half = x < pixelWidth / 2 ? 0 : 1;
            sum[half][0] += pPixel[0];
            sum[half][1] += pPixel[1];
            sum[half][2] += pPixel[2];
            covered[half]++;
        }
    }

    const uint32_t halfPixels = ( pixelWidth / 2 ) * pixelHeight;
    const uint32_t minCoverage = Max<uint32_t>( 1, halfPixels / 50 ); // 2 % of a half
    if ( covered[0] < minCoverage || covered[1] < minCoverage ) {
        OTDU_LOG_ERROR( "D3D12 sampler test: the quads did not draw (left %u px, right %u px of %u)\n",
                        covered[0], covered[1], halfPixels );
        return false;
    }

    float mean[2][3] = {};
    for ( uint32_t half = 0; half < 2; half++ ) {
        for ( uint32_t channel = 0; channel < 3; channel++ ) {
            mean[half][channel] = static_cast<float>( sum[half][channel] ) / covered[half];
        }
    }
    const float deltaR = std::fabs( mean[0][0] - mean[1][0] );
    const float deltaB = std::fabs( mean[0][2] - mean[1][2] );
    const bool bPassed = deltaR >= 20.0f && deltaB >= 20.0f;

    OTDU_LOG_INFO( "D3D12 sampler test: %u frame(s) of two quads on '%s' (%ux%u); wrap mean "
                   "%.1f/%.1f/%.1f (%u px), clamp mean %.1f/%.1f/%.1f (%u px), dR %.1f dB %.1f\n",
                   presented, device.getDeviceName(), pixelWidth, pixelHeight, mean[0][0], mean[0][1],
                   mean[0][2], covered[0], mean[1][0], mean[1][1], mean[1][2], covered[1], deltaR,
                   deltaB );
    OTDU_LOG_INFO( "D3D12 sampler test: %s (dynamic sampler-heap indexing %s)\n",
                   bPassed ? "PASSED" : "FAILED",
                   bPassed ? "reaches the sampler"
                           : "produced identical halves - the index is not reaching the sampler" );
    return bPassed;
}

// -d3d12-constest (docs/backends.md section 3.7): the constant buffers and the
// D3D9 pipeline state, measured. The shader pair is the shape an Eden draw has -
// two cbuffers in the vertex stage (b0 the projection, b1 a per-draw offset) and
// two in the pixel stage (b0 the tint, b2 a scale, with b1 deliberately unused so
// a backend that packs the blocks by order instead of by register fails) - and
// the same mesh is drawn twice with different blocks. A third draw uses the
// D3D9 state path: SRC_ALPHA / INV_SRC_ALPHA over the left quad.
//
// The arithmetic is exact, which is why the test needs no eyes:
//   left  = red (1,0,0) * 1, then blue (0,0,1) at alpha 0.5 over it
//           -> 0.5 red + 0.5 blue = (128, 0, 128)
//   right = green (0,1,0) * 0.5 (the b2 block) -> (0, 128, 0)
// An ignored b1 puts both quads in the middle (the right half would read red, not
// green); an ignored b2 leaves the right half at 255 green; a blend state that
// never reached the pipeline leaves the left half at 255 red.
bool D3D12RunConstTest( D3D12Device &device, uint32_t frames, const ColorRGBA &clearColor ) {
    constexpr uint32_t kDXGI_R32G32_FLOAT = 16;

    static const char *pVSSource =
        "cbuffer Transform : register(b0, space0)\n"
        "{\n"
        "    float4 gScale;\n"
        "};\n"
        "cbuffer Offset : register(b1, space0)\n"
        "{\n"
        "    float4 gOffset;\n"
        "};\n"
        "struct VSIn  { float2 pos : TEXCOORD0; };\n"
        "struct VSOut { float4 pos : SV_Position; };\n"
        "VSOut main( VSIn input )\n"
        "{\n"
        "    VSOut output;\n"
        "    output.pos = float4( input.pos.x * gScale.x + gOffset.x,\n"
        "                         input.pos.y * gScale.y + gOffset.y, 0.5, 1.0 );\n"
        "    return output;\n"
        "}\n";
    static const char *pPSSource =
        "cbuffer Tint : register(b0, space0)\n"
        "{\n"
        "    float4 gTint;\n"
        "};\n"
        "cbuffer ScaleBias : register(b2, space0)\n"
        "{\n"
        "    float4 gScaleBias;\n"
        "};\n"
        "struct PSIn { float4 pos : SV_Position; };\n"
        "float4 main( PSIn input ) : SV_Target\n"
        "{\n"
        "    return float4( gTint.rgb * gScaleBias.rgb, gTint.a );\n"
        "}\n";

    D3D12Scene scene;
    if ( !scene.initialize( &device ) ) {
        OTDU_LOG_ERROR( "D3D12 constant test: the scene could not be initialised\n" );
        return false;
    }
    if ( const char *pDump = CmdLine::GetArgValue( "-d3d12-dump-shaders" ) ) {
        scene.setShaderDumpDir( pDump );
    }

    D3D12ShaderStage vs;
    D3D12ShaderStage ps;
    if ( !D3D12ShaderCompiler::compileHlsl( pVSSource, eShaderType::ST_Vertex, vs, "constant_probe.vert" ) ||
         !D3D12ShaderCompiler::compileHlsl( pPSSource, eShaderType::ST_Pixel, ps, "constant_probe.frag" ) ) {
        OTDU_LOG_ERROR( "D3D12 constant test: shader compile failed: %s %s\n", vs.Error.c_str(),
                        ps.Error.c_str() );
        scene.destroy();
        return false;
    }

    struct ProbeVertex {
        float Position[2];
    };
    const ProbeVertex quad[4] = { { { -1.0f, -1.0f } }, { { -1.0f, 1.0f } }, { { 1.0f, 1.0f } }, { { 1.0f, -1.0f } } };
    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    const uint32_t stride = sizeof( ProbeVertex );

    D3D12Scene::Attribute attributes[1] = { { 0, 0, kDXGI_R32G32_FLOAT, 0 } };
    D3D12Scene::PipelineDesc opaque;
    opaque.pVS = &vs;
    opaque.pPS = &ps;
    opaque.pAttributes = attributes;
    opaque.NumAttributes = 1;
    opaque.pStrides = &stride;
    opaque.NumStreams = 1;
    // D3D9's default opaque state, spelled out the way EdenMaterial carries it.
    D3D12Scene::D3D9State opaqueState;
    opaqueState.SrcBlend = 2;  // ONE
    opaqueState.DstBlend = 1;  // ZERO
    opaqueState.BlendOp = 1;   // ADD
    opaqueState.DepthFunc = 0; // LESSEQUAL (EdenMaterial's default)
    opaqueState.ColorWriteMask = 0xf;
    opaqueState.CullMode = 3; // D3DCULL_CCW (two sided in Eden's data)
    opaque.pState = &opaqueState;
    opaque.bDepthTest = true;
    opaque.bDepthWrite = true;
    const void *pOpaque = scene.getPipeline( opaque );

    D3D12Scene::PipelineDesc blended = opaque;
    D3D12Scene::D3D9State blendState = opaqueState;
    blendState.SrcBlend = 5; // SRC_ALPHA
    blendState.DstBlend = 6; // INV_SRC_ALPHA
    blended.pState = &blendState;
    blended.bDepthTest = false;
    blended.bDepthWrite = false;
    const void *pBlended = scene.getPipeline( blended );
    if ( pOpaque == nullptr || pBlended == nullptr ) {
        OTDU_LOG_ERROR( "D3D12 constant test: the pipelines could not be created\n" );
        scene.destroy();
        return false;
    }

    D3D12Scene::Mesh mesh;
    if ( !scene.createMesh( quad, sizeof( quad ), stride, indices, 6, mesh ) ) {
        OTDU_LOG_ERROR( "D3D12 constant test: the quad could not be uploaded\n" );
        scene.destroy();
        return false;
    }

    // The blocks, 16 bytes each at the register the shader declares.
    struct Block {
        float Value[4];
    };
    const Block kScale = { { 0.45f, 0.9f, 0.0f, 0.0f } };
    const Block kLeftOffset = { { -0.52f, 0.0f, 0.0f, 0.0f } };
    const Block kRightOffset = { { 0.52f, 0.0f, 0.0f, 0.0f } };
    const Block kRed = { { 1.0f, 0.0f, 0.0f, 1.0f } };
    const Block kGreen = { { 0.0f, 1.0f, 0.0f, 1.0f } };
    const Block kBlueHalf = { { 0.0f, 0.0f, 1.0f, 0.5f } };
    const Block kFull = { { 1.0f, 1.0f, 1.0f, 1.0f } };
    const Block kHalf = { { 0.5f, 0.5f, 0.5f, 1.0f } };

    D3D12Scene::StageConstants leftVS = {};
    leftVS.pData[0] = &kScale;
    leftVS.Sizes[0] = sizeof( kScale );
    leftVS.pData[1] = &kLeftOffset;
    leftVS.Sizes[1] = sizeof( kLeftOffset );
    D3D12Scene::StageConstants rightVS = leftVS;
    rightVS.pData[1] = &kRightOffset;
    D3D12Scene::StageConstants redPS = {};
    redPS.pData[0] = &kRed;
    redPS.Sizes[0] = sizeof( kRed );
    redPS.pData[2] = &kFull; // b2, with b1 left empty on purpose
    redPS.Sizes[2] = sizeof( kFull );
    D3D12Scene::StageConstants greenPS = redPS;
    greenPS.pData[0] = &kGreen;
    greenPS.pData[2] = &kHalf;
    D3D12Scene::StageConstants bluePS = redPS;
    bluePS.pData[0] = &kBlueHalf;

    bool bQuit = false;
    uint32_t presented = 0;
    for ( uint32_t i = 0; i < frames && !bQuit; i++ ) {
        SDL_Event event;
        while ( SDL_PollEvent( &event ) ) {
            if ( event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ) {
                bQuit = true;
            }
        }
        if ( bQuit || !device.beginFrame( clearColor ) ) {
            break;
        }
        scene.draw( pOpaque, mesh, nullptr, 0, nullptr, 0, &leftVS, &redPS );
        scene.draw( pOpaque, mesh, nullptr, 0, nullptr, 0, &rightVS, &greenPS );
        // The state path: blue at alpha 0.5 over the left quad, with the D3D9
        // blend factor left at 1 (it is dynamic state, not this pipeline's).
        scene.draw( pBlended, mesh, nullptr, 0, nullptr, 0, &leftVS, &bluePS );
        if ( i + 1 == frames ) {
            // endFrame copies the frame it just recorded (before Present).
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

    scene.destroyMesh( mesh );
    scene.destroy();

    if ( presented == 0 || device.hasDeviceLost() ) {
        OTDU_LOG_ERROR( "D3D12 constant test: no frame was presented%s\n",
                        device.hasDeviceLost() ? " - DEVICE LOST" : "" );
        return false;
    }
    if ( pixels.empty() ) {
        OTDU_LOG_ERROR( "D3D12 constant test: the frame could not be read back\n" );
        return false;
    }

    // The two halves over the pixels the quads cover (the texture-free shader
    // writes saturated colours, so "far from the clear colour" is a quad pixel).
    const uint32_t pixelWidth = device.getBackbufferWidth();
    const uint32_t pixelHeight = device.getBackbufferHeight();
    if ( pixels.size() < static_cast<size_t>( pixelWidth ) * pixelHeight * 4 ) {
        OTDU_LOG_ERROR( "D3D12 constant test: the readback is %zu bytes, expected %u\n", pixels.size(),
                        pixelWidth * pixelHeight * 4 );
        return false;
    }
    const int background[3] = { static_cast<int>( clearColor.r * 255.0f + 0.5f ),
                                static_cast<int>( clearColor.g * 255.0f + 0.5f ),
                                static_cast<int>( clearColor.b * 255.0f + 0.5f ) };
    double sum[2][3] = {};
    uint32_t covered[2] = {};
    for ( uint32_t y = 0; y < pixelHeight; y++ ) {
        for ( uint32_t x = 0; x < pixelWidth; x++ ) {
            const uint8_t *pPixel = &pixels[( static_cast<size_t>( y ) * pixelWidth + x ) * 4];
            const int dr = static_cast<int>( pPixel[0] ) - background[0];
            const int dg = static_cast<int>( pPixel[1] ) - background[1];
            const int db = static_cast<int>( pPixel[2] ) - background[2];
            if ( dr * dr + dg * dg + db * db < 12 * 12 ) {
                continue;
            }
            const uint32_t half = x < pixelWidth / 2 ? 0 : 1;
            sum[half][0] += pPixel[0];
            sum[half][1] += pPixel[1];
            sum[half][2] += pPixel[2];
            covered[half]++;
        }
    }

    const uint32_t halfPixels = ( pixelWidth / 2 ) * pixelHeight;
    const uint32_t minCoverage = Max<uint32_t>( 1, halfPixels / 50 ); // 2 % of a half
    if ( covered[0] < minCoverage || covered[1] < minCoverage ) {
        OTDU_LOG_ERROR( "D3D12 constant test: the quads did not draw (left %u px, right %u px of %u)\n",
                        covered[0], covered[1], halfPixels );
        return false;
    }

    float mean[2][3] = {};
    for ( uint32_t half = 0; half < 2; half++ ) {
        for ( uint32_t channel = 0; channel < 3; channel++ ) {
            mean[half][channel] = static_cast<float>( sum[half][channel] ) / covered[half];
        }
    }
    // left = 0.5 red + 0.5 blue, right = 0.5 green; 20 levels of slack absorbs
    // the UNORM rounding of the blend.
    const bool bLeft = std::fabs( mean[0][0] - 128.0f ) <= 20.0f && mean[0][1] <= 20.0f &&
                       std::fabs( mean[0][2] - 128.0f ) <= 20.0f;
    const bool bRight = mean[1][0] <= 20.0f && std::fabs( mean[1][1] - 128.0f ) <= 20.0f &&
                        mean[1][2] <= 20.0f;
    const bool bPassed = bLeft && bRight;
    OTDU_LOG_INFO( "D3D12 constant test: %u frame(s) of two quads on '%s' (%ux%u); left mean "
                   "%.1f/%.1f/%.1f (%u px), right mean %.1f/%.1f/%.1f (%u px)\n",
                   presented, device.getDeviceName(), pixelWidth, pixelHeight, mean[0][0], mean[0][1],
                   mean[0][2], covered[0], mean[1][0], mean[1][1], mean[1][2], covered[1] );
    OTDU_LOG_INFO( "D3D12 constant test: %s (%s)\n", bPassed ? "PASSED" : "FAILED",
                   bPassed ? "b0/b1/b2 reached both stages and SRC_ALPHA blending applied"
                           : "a half reads the wrong colour - see the means (red on the right: b1 "
                             "did not move the quad; 255 green: b2 did not reach the shader; 255 red "
                             "on the left: the blend state did not apply)" );
    return bPassed;
}

// -d3d12-edentest (docs/backends.md section 3.8): the first *original* TDU2
// shader drawn by the Direct3D 12 backend.
//
// Each link of the chain is already proved on its own - every .sob stage compiles
// (3.5), the sampler heap indexes (3.6), the constants and the D3D9 state arrive
// (3.7) - and this is the same chain with Eden's own material data: a real
// permutation of a real library, its constant file written by the shared
// FillEdenConstants, its texture in the t0 table, its state in the pipeline.
//
// The one thing the probe cannot have is a `.2DM`: a material's parameter values
// live in the game's material banks, and loading one needs the resource manager.
// So the float registers the filler has no value for start at 1.0 rather than
// black (a black frame would hide whether the shader ran), and the log says how
// many registers the filler actually wrote.
struct EdenProbePermutation {
    uint32_t VSAttrib = 0;
    uint32_t PSAttrib = 0;
    std::vector<uint8_t> Spec;
};

// The library's simplest permutations - unskinned, opaque, not the depth or
// shadow-caster pass, at least one declared texture - with the VS/PS attrib
// indices that share each one's hashcode. Picking a permutation from the library
// (instead of inventing a spec) is what a material does, so the program the probe
// draws is one the game really uses. Several are returned because the caller
// still has to compile them: only the compiled stage knows how many textures it
// actually samples, and the probe wants exactly one (the engine's checker).
static uint32_t CollectProbePermutations( ShaderLibrary &library, std::vector<EdenProbePermutation> &out,
                                          uint32_t max ) {
    const ShaderLibrary::Profile *pVS = library.getProfile( ShaderLibrary::SP_VertexShader );
    const ShaderLibrary::Profile *pPS = library.getProfile( ShaderLibrary::SP_PixelShader );
    if ( pVS == nullptr || pPS == nullptr ) {
        return 0;
    }
    out.clear();
    uint32_t considered = 0;
    for ( const ShaderLibrary::Item &item : pPS->Items ) {
        if ( item.Spec.size() <= EdenShaderSystem::S_PASS_SHADOWRECEIVE ) {
            continue;
        }
        if ( item.Spec[EdenShaderSystem::S_NBTEXTURES] < 1 || item.Spec[EdenShaderSystem::S_USESKINNING] != 0 ||
             item.Spec[EdenShaderSystem::S_USEALPHATEST] != 0 || item.Spec[EdenShaderSystem::S_PASS_Z] != 0 ||
             item.Spec[EdenShaderSystem::S_PASS_SHADOWCAST] != 0 ) {
            continue;
        }
        considered++;
        for ( const ShaderLibrary::Item &vsItem : pVS->Items ) {
            if ( vsItem.Hashcode != item.Hashcode ) {
                continue;
            }
            EdenProbePermutation permutation;
            permutation.VSAttrib = vsItem.AttribIndex;
            permutation.PSAttrib = item.AttribIndex;
            permutation.Spec = item.Spec;
            permutation.Spec.resize( EdenShaderSystem::kNumSpecs, 0 );
            out.push_back( std::move( permutation ) );
            break;
        }
        if ( out.size() >= max ) {
            break;
        }
    }
    if ( considered > 0 && out.empty() ) {
        OTDU_LOG_WARN( "D3D12 Eden test: %s has %u simple permutation(s) but no VS item with the same "
                       "hashcode\n",
                       library.getName().c_str(), considered );
    }
    return static_cast<uint32_t>( out.size() );
}

// The probe's draw: the program's own VS/PS, its constant file, its texture.
static bool DrawEdenProbe( D3D12Device &device,
                           const char *pLibraryName,
                           const EdenProgram &program,
                           const EdenProbePermutation &permutation,
                           const D3D12ShaderStage &vs,
                           const D3D12ShaderStage &ps,
                           bool &outRetry ) {
    outRetry = false;
    // 1. The scene. A camera three metres in front of a quad at the origin, built
    //    the way EdenSceneView::setupScene builds one (cameraWorld is
    //    T(position) * rotation * S(-1,1,-1), the view is its inverse, the
    //    projection is D3DXMatrixPerspectiveFovRH / EdenPerspectiveFovRH).
    const float width = static_cast<float>( device.getBackbufferWidth() );
    const float height = Max( 1.0f, static_cast<float>( device.getBackbufferHeight() ) );
    const float aspect = width / height;
    const float fovY = 60.0f * ( 3.14159265358979323846f / 180.0f );
    const float zNear = 0.1f;
    const float zFar = 1000.0f;
    const float cotangent = 1.0f / std::tan( fovY * 0.5f );

    EdenSceneParams scene;
    scene.View = ( Matrix4x4f::Translation( Vector3f( 0.0f, 0.0f, -3.0f ) ) *
                   Matrix4x4f::Scale( Vector3f( -1.0f, 1.0f, -1.0f ) ) ).inverse();
    scene.Projection = Matrix4x4f::Identity();
    scene.Projection.m[0][0] = cotangent / aspect;
    scene.Projection.m[1][1] = cotangent;
    scene.Projection.m[2][2] = zFar / ( zNear - zFar );
    scene.Projection.m[2][3] = -1.0f;
    scene.Projection.m[3][2] = zNear * zFar / ( zNear - zFar );
    scene.Projection.m[3][3] = 0.0f;
    scene.CameraPosition = Vector3f( 0.0f, 0.0f, -3.0f );
    scene.CameraDirection = Vector3f( 0.0f, 0.0f, 1.0f );
    // From behind the camera, so a quad facing it is lit rather than ambient only
    // (Eden's light direction is the direction the light travels).
    scene.SunDirection = Vector3f( 0.25f, -0.35f, 0.9f ).normalized();
    scene.Width = device.getBackbufferWidth();
    scene.Height = device.getBackbufferHeight();
    scene.Time = 0.0f;

    const Matrix4x4f world = Matrix4x4f::Identity();
    const Matrix4x4f viewProjection = scene.Projection.mulPrecise( scene.View );

    EdenMaterial material;
    material.Name = std::string( pLibraryName ) + " (probe)";
    material.ShaderSetGUID = program.ShaderSetGUID;
    material.pProgram = &program;
    memcpy( material.Spec, permutation.Spec.data(),
            Min<size_t>( permutation.Spec.size(), EdenShaderSystem::kNumSpecs ) );
    material.CullMode = 1; // a test quad's winding is not what is being proved
    material.NumSlots = 1;

    // 2. The constant file, filled by the very code the Vulkan renderer runs -
    //    the point of the shared filler. The registers it has no value for keep
    //    the 1.0 stand-in (there is no .2DM behind this material).
    std::vector<uint8_t> vsBlock( kEdenVSConstantsSize, 0 );
    std::vector<uint8_t> psBlock( kEdenPSConstantsSize, 0 );
    float *pVSRegisters = reinterpret_cast<float *>( vsBlock.data() + kEdenIntRegistersSize );
    float *pPSRegisters = reinterpret_cast<float *>( psBlock.data() + kEdenIntRegistersSize );
    for ( uint32_t i = 0; i < kEdenVSFloatRegisters * 4; i++ ) {
        pVSRegisters[i] = 1.0f;
    }
    for ( uint32_t i = 0; i < kEdenPSFloatRegisters * 4; i++ ) {
        pPSRegisters[i] = 1.0f;
    }
    std::unordered_set<std::string> warned;
    EdenConstantsContext context;
    context.pScene = &scene;
    context.pViewProjection = &viewProjection;
    context.SceneEpoch = 1;
    context.pWarned = &warned;
    FillEdenConstants( context, program.VSMembers, pVSRegisters, kEdenVSFloatRegisters, material, world, nullptr,
                       program.Name.c_str(), false );
    FillEdenConstants( context, program.PSMembers, pPSRegisters, kEdenPSFloatRegisters, material, world, nullptr,
                       program.Name.c_str(), true );
    uint32_t specDwords[kEdenSpecDwords] = {};
    FillEdenSpecConstants( specDwords, material.bAlphaTest );

    // 3. The mesh: a quad whose attributes are the VS's own dcl list. Every
    //    attribute is R32G32B32A32 (SPIRV-Cross emits every vertex input as a
    //    float4 and the shader swizzles), and the value comes from the
    //    D3DDECLUSAGE the shader declared - the data a real mesh would carry.
    constexpr uint32_t kDXGI_R32G32B32A32_FLOAT = 2;
    const uint32_t numAttributes = Min<uint32_t>( static_cast<uint32_t>( program.pVS->Inputs.size() ), 8 );
    if ( numAttributes == 0 ) {
        OTDU_LOG_ERROR( "D3D12 Eden test: %s's vertex shader declares no input\n", pLibraryName );
        return false;
    }
    const uint32_t stride = numAttributes * 16;
    D3D12Scene::Attribute attributes[8] = {};
    for ( uint32_t i = 0; i < numAttributes; i++ ) {
        attributes[i].Location = program.pVS->Inputs[i].Location;
        attributes[i].Stream = 0;
        attributes[i].Format = kDXGI_R32G32B32A32_FLOAT;
        attributes[i].Offset = i * 16;
    }
    std::vector<float> vertexData( static_cast<size_t>( 4 ) * numAttributes * 4, 0.0f );
    const float corners[4][2] = { { -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, -1.0f } };
    for ( uint32_t v = 0; v < 4; v++ ) {
        for ( uint32_t i = 0; i < numAttributes; i++ ) {
            float *pValue = &vertexData[( static_cast<size_t>( v ) * numAttributes + i ) * 4];
            switch ( program.pVS->Inputs[i].Usage ) {
            case 0: // D3DDECLUSAGE_POSITION
                pValue[0] = corners[v][0] * 0.8f;
                pValue[1] = corners[v][1] * 0.8f;
                pValue[2] = 0.0f;
                pValue[3] = 1.0f;
                break;
            case 3: // NORMAL: facing the camera
                pValue[2] = -1.0f;
                break;
            case 5: // TEXCOORD: the uv in xy, and w = 1 like a real mesh - a D3D9
                    // shader may divide by it (TD_LightBeam's ps does
                    // `1.0 / t1.w` for a projected lookup, and w = 0 painted the
                    // whole quad black).
                pValue[0] = corners[v][0] * 0.5f + 0.5f;
                pValue[1] = 1.0f - ( corners[v][1] * 0.5f + 0.5f );
                pValue[3] = 1.0f;
                break;
            case 1: // BLENDWEIGHT
                pValue[0] = 1.0f;
                break;
            case 6: // TANGENT
                pValue[0] = 1.0f;
                break;
            case 7: // BINORMAL
                pValue[1] = 1.0f;
                break;
            case 10: // COLOR
                pValue[0] = pValue[1] = pValue[2] = pValue[3] = 1.0f;
                break;
            default: // BLENDINDICES, PSIZE, FOG, DEPTH, TESSFACTOR: zero
                break;
            }
        }
    }
    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };

    // 4. The material's texture: the engine's own checker (TextureCache uploads
    //    it through the D3D12 path), bound as t0 - the one slot this
    //    permutation declares.
    TextureCache textureCache;
    textureCache.initialize( &device );
    GPUTexture *pChecker = textureCache.getCheckerTexture();
    if ( pChecker == nullptr || pChecker->D3D12SrvStorageCpu == 0 ) {
        OTDU_LOG_ERROR( "D3D12 Eden test: the checker texture could not be uploaded\n" );
        return false;
    }
    material.pTextures[0] = pChecker;
    material.SamplerIndex[0] = SH_LinearWrap;

    // 5. The pipeline (the material's own D3D9 state) and the quad.
    D3D12Scene sceneRenderer;
    if ( !sceneRenderer.initialize( &device ) ) {
        OTDU_LOG_ERROR( "D3D12 Eden test: the scene could not be initialised\n" );
        return false;
    }
    if ( const char *pDump = CmdLine::GetArgValue( "-d3d12-dump-shaders" ) ) {
        sceneRenderer.setShaderDumpDir( pDump );
    }
    D3D12Scene::PipelineDesc desc;
    desc.pVS = &vs;
    desc.pPS = &ps;
    desc.pAttributes = attributes;
    desc.NumAttributes = numAttributes;
    desc.pStrides = &stride;
    desc.NumStreams = 1;
    D3D12Scene::D3D9State state;
    state.SrcBlend = material.SrcBlend;
    state.DstBlend = material.DstBlend;
    state.BlendOp = material.BlendOp;
    state.DepthFunc = material.DepthFunc;
    state.ColorWriteMask = material.ColorWriteMask;
    state.CullMode = material.CullMode;
    desc.pState = &state;
    desc.bDepthTest = material.bDepthTest;
    desc.bDepthWrite = material.bDepthWrite;
    const void *pPipeline = sceneRenderer.getPipeline( desc );
    if ( pPipeline == nullptr ) {
        // D3D12 refused this pair (its DXBC or the state drawn from it): the
        // caller tries the next candidate, and the log names this one.
        outRetry = true;
        OTDU_LOG_WARN( "D3D12 Eden test: %s vs%u/ps%u was compiled but the device refused the "
                       "pipeline\n",
                       pLibraryName, permutation.VSAttrib, permutation.PSAttrib );
        sceneRenderer.destroy();
        return false;
    }
    D3D12Scene::Mesh mesh;
    if ( !sceneRenderer.createMesh( vertexData.data(),
                                    static_cast<uint32_t>( vertexData.size() * sizeof( float ) ), stride,
                                    indices, 6, mesh ) ) {
        OTDU_LOG_ERROR( "D3D12 Eden test: the quad could not be uploaded\n" );
        sceneRenderer.destroy();
        return false;
    }

    // 6. The cbuffers by what they are: SPIRV-Cross hands back the *instance*
    //    names DXSO emitted (`c`, `spec_state`, `clip_info`, `D3D9SharedPS`), not
    //    the type names in the HLSL, so the rule is a shape rather than a string:
    //    anything that names the specialization state is the spec block, the
    //    clip/shared blocks stay the zero block (the Vulkan renderer binds a zero
    //    buffer there too) and everything else is the stage's constant file.
    D3D12Scene::StageConstants vsConstants = {};
    D3D12Scene::StageConstants psConstants = {};
    auto assignBlocks = []( const D3D12ShaderStage &stage, D3D12Scene::StageConstants &out, const void *pFile,
                            uint32_t fileBytes, const void *pSpec, uint32_t specBytes ) {
        std::string bound;
        for ( uint32_t i = 0; i < stage.ConstantBufferNames.size() && i < D3D12Scene::kMaxConstantBuffers; i++ ) {
            const std::string name = ToUpper( stage.ConstantBufferNames[i] );
            const char *pWhat = "zero";
            if ( name.find( "SPEC" ) != std::string::npos ) {
                out.pData[i] = pSpec;
                out.Sizes[i] = specBytes;
                pWhat = "spec";
            } else if ( name.find( "CLIP" ) == std::string::npos && name.find( "SHARED" ) == std::string::npos ) {
                out.pData[i] = pFile;
                out.Sizes[i] = fileBytes;
                pWhat = "file";
            }
            if ( !bound.empty() ) {
                bound += ", ";
            }
            bound += "b" + std::to_string( i ) + "=" + pWhat;
        }
        return bound;
    };
    const std::string vsBinding = assignBlocks( vs, vsConstants, vsBlock.data(),
                                                static_cast<uint32_t>( vsBlock.size() ), specDwords,
                                                sizeof( specDwords ) );
    const std::string psBinding = assignBlocks( ps, psConstants, psBlock.data(),
                                                static_cast<uint32_t>( psBlock.size() ), specDwords,
                                                sizeof( specDwords ) );
    auto listBuffers = []( const D3D12ShaderStage &stage ) {
        std::string names;
        for ( const std::string &name : stage.ConstantBufferNames ) {
            names += names.empty() ? name : ( ", " + name );
        }
        return names;
    };
    OTDU_LOG_INFO( "D3D12 Eden test: %s vs%u/ps%u (one texture, unskinned, opaque); cbuffers vs [%s] %s, ps "
                   "[%s] %s\n",
                   pLibraryName, permutation.VSAttrib, permutation.PSAttrib, listBuffers( vs ).c_str(),
                   vsBinding.c_str(), listBuffers( ps ).c_str(), psBinding.c_str() );

    // 7. The frames. The backdrop is magenta, so any pixel the shader painted is
    //    unambiguous; the push block is zero (= alpha reference 0, every slot
    //    sampling heap entry 0, linear wrap).
    const uint32_t frames = 30;
    const ColorRGBA clearColor( 1.0f, 0.0f, 1.0f, 1.0f );
    uint8_t push[128] = {};
    if ( const char *pShot = CmdLine::GetArgValue( "-renderer-shot" ) ) {
        device.requestScreenshot( pShot );
    }
    uint32_t presented = 0;
    for ( uint32_t i = 0; i < frames; i++ ) {
        SDL_Event event;
        while ( SDL_PollEvent( &event ) ) {
            if ( event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ) {
                break;
            }
        }
        if ( !device.beginFrame( clearColor ) ) {
            break;
        }
        sceneRenderer.draw( pPipeline, mesh, push, sizeof( push ), &pChecker->D3D12SrvStorageCpu, 1, &vsConstants,
                            &psConstants, material.BlendFactor );
        if ( i + 1 == frames ) {
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

    sceneRenderer.destroyMesh( mesh );
    sceneRenderer.destroy();
    textureCache.destroy();

    if ( presented == 0 || device.hasDeviceLost() || pixels.empty() ) {
        OTDU_LOG_ERROR( "D3D12 Eden test: %s\n",
                        device.hasDeviceLost() ? "the device was lost" : "no frame was presented" );
        return false;
    }

    // 8. The measurement: the pixels that are not the backdrop, their mean, and
    //    how many float registers the filler wrote (the rest is the 1.0 stand-in
    //    a real .2DM replaces).
    const uint32_t pixelWidth = device.getBackbufferWidth();
    const uint32_t pixelHeight = device.getBackbufferHeight();
    const uint32_t totalPixels = pixelWidth * pixelHeight;
    if ( pixels.size() < static_cast<size_t>( totalPixels ) * 4 ) {
        OTDU_LOG_ERROR( "D3D12 Eden test: the readback is %zu bytes, expected %u\n", pixels.size(),
                        totalPixels * 4 );
        return false;
    }
    double sum[3] = {};
    uint32_t covered = 0;
    for ( uint32_t y = 0; y < pixelHeight; y++ ) {
        for ( uint32_t x = 0; x < pixelWidth; x++ ) {
            const uint8_t *pPixel = &pixels[( static_cast<size_t>( y ) * pixelWidth + x ) * 4];
            if ( pPixel[0] >= 249 && pPixel[1] <= 6 && pPixel[2] >= 249 ) {
                continue; // the backdrop
            }
            sum[0] += pPixel[0];
            sum[1] += pPixel[1];
            sum[2] += pPixel[2];
            covered++;
        }
    }
    auto countWritten = []( const float *pRegisters, uint32_t numRegisters ) {
        uint32_t written = 0;
        for ( uint32_t r = 0; r < numRegisters; r++ ) {
            const float *pRegister = pRegisters + r * 4;
            if ( pRegister[0] != 1.0f || pRegister[1] != 1.0f || pRegister[2] != 1.0f ||
                 pRegister[3] != 1.0f ) {
                written++;
            }
        }
        return written;
    };
    const float mean[3] = { covered > 0 ? static_cast<float>( sum[0] / covered ) : 0.0f,
                            covered > 0 ? static_cast<float>( sum[1] / covered ) : 0.0f,
                            covered > 0 ? static_cast<float>( sum[2] / covered ) : 0.0f };
    const bool bDrew = covered >= Max<uint32_t>( 1, totalPixels / 50 ) &&
                       ( std::fabs( mean[0] - 255.0f ) > 8.0f || mean[1] > 8.0f ||
                         std::fabs( mean[2] - 255.0f ) > 8.0f );
    OTDU_LOG_INFO( "D3D12 Eden test: %u frame(s) of a quad on '%s' (%ux%u); %u px covered (%.2f %%), mean "
                   "%.1f/%.1f/%.1f; %u of %u vs and %u of %u ps registers written\n",
                   presented, device.getDeviceName(), pixelWidth, pixelHeight, covered,
                   totalPixels > 0 ? 100.0f * static_cast<float>( covered ) / static_cast<float>( totalPixels )
                                   : 0.0f,
                   mean[0], mean[1], mean[2], countWritten( pVSRegisters, kEdenVSFloatRegisters ),
                   kEdenVSFloatRegisters, countWritten( pPSRegisters, kEdenPSFloatRegisters ),
                   kEdenPSFloatRegisters );
    OTDU_LOG_INFO( "D3D12 Eden test: %s (%s)\n", bDrew ? "PASSED" : "FAILED",
                   bDrew ? "the original shader painted the quad on Direct3D 12"
                         : "the quad is not on the frame, or the shader wrote the backdrop colour" );
    return bDrew;
}

// -d3d12-edentest: shaders, a hidden window and the device, then the probe.
// This runs in the bootstrap (tdu_instance.cpp) right after the file service,
// because it needs Euro/Shaders - the same place -d3d12-shadertest runs.
bool D3D12RunEdenTest() {
    static const char *kCandidates[] = { "TD_CarAlpha",    "TD_CloudsPerlin", "TD_ElectricalNetwork",
                                         "TD_ChipsStack",  "TD_LightBeam",    "TD_Lamp",
                                         "TD_GPS",         "TD_Flare" };
    // "-d3d12-edentest <text>": only libraries whose name contains <text>, so a
    // named pair can be drawn on its own (the A/B for a fix that changes which
    // candidate comes first). A bare flag leaves the next token to another
    // switch - CmdLine::GetArgValue does not look at the leading '-'.
    const char *pFilterArg = CmdLine::GetArgValue( "-d3d12-edentest" );
    const char *pFilter = ( pFilterArg != nullptr && pFilterArg[0] != '-' ) ? pFilterArg : nullptr;
    EdenShaderSystem shaderSystem;
    if ( !shaderSystem.initialize() ) {
        OTDU_LOG_ERROR( "D3D12 Eden test: the shader libraries could not be loaded (Euro/Shaders)\n" );
        return false;
    }

    bool bPassed = false;
    bool bAbort = false;
    SDL_Window *pWindow = nullptr;
    if ( SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
        pWindow = SDL_CreateWindow( "OpenTDU2 - Eden on Direct3D 12", 1280, 720, SDL_WINDOW_HIDDEN );
    }
    if ( pWindow == nullptr ) {
        OTDU_LOG_ERROR( "D3D12 Eden test: no window: %s\n", SDL_GetError() );
        bAbort = true;
    }

    D3D12Device device;
    if ( !bAbort && !device.create( pWindow, /*bVSync=*/false, CmdLine::HasArg( "-d3d12-debug" ) ) ) {
        OTDU_LOG_ERROR( "D3D12 Eden test: the device could not be created\n" );
        bAbort = true;
    }

    // Two passes over the candidates: a permutation that samples exactly one
    // texture first (the checker then proves the whole texture path), then any
    // simple one. A pair the device refuses is a reason to try the next
    // candidate - the log names it, so the gap is visible rather than fatal.
    for ( int pass = 0; pass < 2 && !bPassed && !bAbort; pass++ ) {
        const bool bRequireTexture = pass == 0;
        for ( const char *pName : kCandidates ) {
            if ( pFilter != nullptr && ToUpper( pName ).find( ToUpper( pFilter ) ) == std::string::npos ) {
                continue;
            }
            ShaderLibrary *pLibrary = shaderSystem.findLibrary( pName );
            if ( pLibrary == nullptr ) {
                OTDU_LOG_WARN( "D3D12 Eden test: no library '%s' in Euro/Shaders\n", pName );
                continue;
            }
            std::vector<EdenProbePermutation> permutations;
            if ( CollectProbePermutations( *pLibrary, permutations, 4 ) == 0 ) {
                OTDU_LOG_WARN( "D3D12 Eden test: %s has no one-texture unskinned opaque permutation\n",
                               pName );
                continue;
            }
            for ( const EdenProbePermutation &permutation : permutations ) {
                const EdenProgram *pProgram = shaderSystem.getProgram( pName, permutation.VSAttrib,
                                                                       permutation.PSAttrib );
                if ( pProgram == nullptr || pProgram->pVS == nullptr || pProgram->pPS == nullptr ) {
                    continue;
                }
                D3D12ShaderStage vs;
                D3D12ShaderStage ps;
                // compilePair, not compile twice: D3D12 links the stages by
                // varying slot and a D3D9 shader may skip a register, so the
                // pixel stage's input struct is padded to the vertex stage's
                // output list (docs/backends.md 3.9).
                if ( !D3D12ShaderCompiler::compilePair( *pProgram->pVS, *pProgram->pPS, vs, ps, pName ) ) {
                    OTDU_LOG_WARN( "D3D12 Eden test: %s vs%u/ps%u does not compile: %s %s\n", pName,
                                   permutation.VSAttrib, permutation.PSAttrib, vs.Error.c_str(),
                                   ps.Error.c_str() );
                    continue;
                }
                if ( bRequireTexture ? ps.NumTextures != 1 : ps.NumTextures > 1 ) {
                    continue;
                }
                bool bRetry = false;
                bPassed = DrawEdenProbe( device, pName, *pProgram, permutation, vs, ps, bRetry );
                if ( bPassed || !bRetry ) {
                    break;
                }
            }
            if ( bPassed ) {
                break;
            }
        }
    }

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

// -d3d12-shadertest (docs/backends.md section 3.5): the measurement that
// decides how much of Eden's shader work the Direct3D 12 path can already
// carry. Every vertex and pixel stage of every .sob library in Euro/Shaders
// makes the full trip - DXSO -> SPIR-V -> HLSL (SPIRV-Cross) -> SM 5.0 DXBC
// (D3DCompile) - and the tally says how many made it. The two failure counts
// are kept apart on purpose: a stage DXSO cannot translate is a Vulkan problem
// too (and already logged there), while a stage that translates but will not
// compile is specific to this backend.
bool D3D12RunShaderTest() {
    if ( gpFile == nullptr ) {
        OTDU_LOG_ERROR( "D3D12 shadertest: the game file system is not up\n" );
        return false;
    }
    EdenShaderSystem shaderSystem;
    if ( !shaderSystem.initialize() ) {
        OTDU_LOG_ERROR( "D3D12 shadertest: no shader library under %s\n", gpFile->getShaderFolder().c_str() );
        return false;
    }
    const std::vector<std::unique_ptr<ShaderLibrary>> &libraries = shaderSystem.getLibraries();
    const char *pDumpDir = CmdLine::GetArgValue( "-d3d12-dump-shaders" );
    // "-d3d12-shadertest <text>": only libraries whose name contains <text>, so
    // one family can be iterated on without compiling the other 115. A bare flag
    // leaves the next token to be another switch (-game ...), which is not a
    // filter: CmdLine::GetArgValue does not look at the leading '-'.
    const char *pFilterArg = CmdLine::GetArgValue( "-d3d12-shadertest" );
    const char *pFilter = ( pFilterArg != nullptr && pFilterArg[0] != '-' ) ? pFilterArg : nullptr;

    uint32_t numPermutations = 0;
    uint32_t numStages = 0;
    uint32_t numLibraries = 0;
    uint32_t numTranslated = 0;
    uint32_t numCompiled = 0;
    uint32_t numTranslateFailures = 0;
    uint32_t numCompileFailures = 0;
    uint32_t numOverTextureTable = 0;
    uint32_t maxTextures = 0;
    uint32_t maxPushConstants = 0;
    uint32_t numDumped = 0;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    // The HLSL is what turns "this stage does not compile" into something
    // readable; the dump directory is the scene test's -d3d12-dump-shaders.
    auto dumpShader = [&]( const std::string &debugName, const D3D12ShaderStage &stage ) {
        if ( pDumpDir == nullptr || stage.HLSL.empty() ) {
            return;
        }
        std::string name = debugName;
        for ( char &c : name ) {
            if ( c == ':' || c == '/' || c == '\\' || c == ' ' || c == '\t' ) {
                c = '_';
            }
        }
        const std::string path = std::string( pDumpDir ) + "/shadertest_" + name + ".hlsl";
        FILE *pFile = fopen( path.c_str(), "wb" );
        if ( pFile == nullptr ) {
            OTDU_LOG_WARN( "D3D12 shadertest: cannot write '%s'\n", path.c_str() );
            return;
        }
        fwrite( stage.HLSL.data(), 1, stage.HLSL.size(), pFile );
        fclose( pFile );
        numDumped++;
    };
    for ( const std::unique_ptr<ShaderLibrary> &pLibrary : libraries ) {
        if ( pFilter != nullptr && pFilter[0] != '\0' ) {
            if ( ToLower( pLibrary->getName() ).find( ToLower( pFilter ) ) == std::string::npos ) {
                continue;
            }
        }
        numLibraries++;
        uint32_t libraryStages = 0;
        uint32_t libraryFailed = 0;
        for ( const ShaderLibrary::Profile &profile : pLibrary->getProfiles() ) {
            const bool bVertex = profile.ID == ShaderLibrary::SP_VertexShader;
            if ( !bVertex && profile.ID != ShaderLibrary::SP_PixelShader ) {
                continue;
            }
            const ShaderLibrary::eProfile profileId =
                bVertex ? ShaderLibrary::SP_VertexShader : ShaderLibrary::SP_PixelShader;
            for ( uint32_t attribIndex = 0; attribIndex < static_cast<uint32_t>( profile.Attribs.size() );
                  attribIndex++ ) {
                const std::string debugName = pLibrary->getName() + ( bVertex ? "_vs_" : "_ps_" ) +
                                              std::to_string( attribIndex );
                numStages++;
                libraryStages++;
                // Compiling every library blocks the boot for a while: the
                // watchdog's kill guard must see that the thread is alive.
                Watchdog::Beat( "shadertest", numStages );

                const TranslatedShader *pShader = pLibrary->getTranslatedShader( profileId, attribIndex );
                if ( pShader == nullptr || pShader->SPIRV.empty() ) {
                    numTranslateFailures++;
                    libraryFailed++;
                    OTDU_LOG_WARN( "D3D12 shadertest: %s: DXSO -> SPIR-V failed\n", debugName.c_str() );
                    continue;
                }
                numTranslated++;

                D3D12ShaderStage stage;
                if ( !D3D12ShaderCompiler::compile( *pShader, stage, debugName.c_str() ) ) {
                    numCompileFailures++;
                    libraryFailed++;
                    OTDU_LOG_WARN( "D3D12 shadertest: %s: %s\n", debugName.c_str(), stage.Error.c_str() );
                    dumpShader( debugName, stage );
                    continue;
                }
                numCompiled++;
                if ( stage.NumTextures > maxTextures ) {
                    maxTextures = stage.NumTextures;
                }
                if ( stage.PushConstantsSize > maxPushConstants ) {
                    maxPushConstants = stage.PushConstantsSize;
                }
                if ( stage.NumTextures > D3D12Scene::kTextureTableSize ) {
                    numOverTextureTable++;
                }
                dumpShader( debugName, stage );
            }
        }
        numPermutations += pLibrary->getNumPermutations();
        OTDU_LOG_INFO( "D3D12 shadertest: %-28s %4u permutation(s), %4u stage(s), %u failed\n",
                       pLibrary->getName().c_str(), pLibrary->getNumPermutations(), libraryStages,
                       libraryFailed );
    }

    const double seconds =
        std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
    OTDU_LOG_INFO( "D3D12 shadertest: %u libraries, %u permutation(s), %u stage(s): %u compiled, "
                   "%u translate failure(s), %u HLSL/D3DCompile failure(s) in %.1f s\n",
                   numLibraries, numPermutations, numStages, numCompiled, numTranslateFailures,
                   numCompileFailures, seconds );
    OTDU_LOG_INFO( "D3D12 shadertest: limits - %u texture(s) max (%u stage(s) over the %u-entry root "
                   "signature table), %u B push constants max (%u root constant(s))%s\n",
                   maxTextures, numOverTextureTable, D3D12Scene::kTextureTableSize, maxPushConstants,
                   ( maxPushConstants + 3 ) / 4, numDumped > 0 ? ", HLSL dumped" : "" );
    const bool bPassed = numStages > 0 && numTranslateFailures == 0 && numCompileFailures == 0;
    OTDU_LOG_INFO( "D3D12 shadertest: %s (%u of %u stages translated)\n",
                   bPassed ? "PASSED" : "FAILED", numTranslated, numStages );
    shaderSystem.destroy();
    return bPassed;
}

#endif // OTDU_D3D12
