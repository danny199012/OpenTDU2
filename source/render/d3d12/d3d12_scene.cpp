#include "shared.h"
#include "render/d3d12/d3d12_scene.h"

#if defined( OTDU_D3D12 )

#include "render/d3d12/d3d12_device.h"
#include "render/d3d12/d3d12_shader.h"
#include "render/eden/eden_shader_system.h"
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
    for ( uint32_t i = 0; i < desc.NumStreams; i++ ) {
        key.append( reinterpret_cast<const char *>( &desc.pStrides[i] ), sizeof( uint32_t ) );
    }
    for ( uint32_t i = 0; i < desc.NumAttributes; i++ ) {
        key.append( reinterpret_cast<const char *>( &desc.pAttributes[i] ), sizeof( D3D12Scene::Attribute ) );
    }
    return key;
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

    D3D12_ROOT_PARAMETER params[4] = {};
    // 0 / 1: the stage constant buffers (Eden's material block lands here).
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // 2: the D3D9 push constants as root constants (d3d12_shader.h's b13, in
    // register space 1 so the remap key cannot collide with a D3D9 sampler slot).
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister = D3D12ShaderCompiler::kPushConstantRegister;
    params[2].Constants.RegisterSpace = D3D12ShaderCompiler::kPushConstantSpace;
    params[2].Constants.Num32BitValues = kMaxRootConstants;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // 3: the material's textures.
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &srvRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // D3D9's default sampler state (linear filtering, wrap both ways). Eden
    // picks a sampler per material on the Vulkan path; a per-material variant of
    // this signature is a later step, and this one is what the front end and
    // most world materials use.
    D3D12_STATIC_SAMPLER_DESC samplers[kTextureTableSize] = {};
    for ( uint32_t i = 0; i < kTextureTableSize; i++ ) {
        samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[i].MipLODBias = 0.0f;
        samplers[i].MaxAnisotropy = 1;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplers[i].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        samplers[i].MinLOD = 0.0f;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;
        samplers[i].RegisterSpace = 0;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 4;
    desc.pParameters = params;
    desc.NumStaticSamplers = kTextureTableSize;
    desc.pStaticSamplers = samplers;
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
    OTDU_LOG_INFO( "D3D12 scene: root signature ready (b0 constants, b%u space%u root constants, t0..t%u)\n",
                   D3D12ShaderCompiler::kPushConstantRegister,
                   D3D12ShaderCompiler::kPushConstantSpace, kTextureTableSize - 1 );
    return true;
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
    SafeRelease( reinterpret_cast<ID3D12DescriptorHeap *&>( pSrvTable ) );
    SafeRelease( reinterpret_cast<ID3D12RootSignature *&>( pRootSignature ) );
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
    blend.RenderTarget[0].BlendEnable = desc.Blend != eBlendMode::BM_Opaque;
    blend.RenderTarget[0].LogicOpEnable = FALSE;
    blend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
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

    D3D12_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = desc.Cull == eCullMode::CM_Back     ? D3D12_CULL_MODE_BACK
                          : desc.Cull == eCullMode::CM_Front ? D3D12_CULL_MODE_FRONT
                                                             : D3D12_CULL_MODE_NONE;
    // Direct3D decides the front face in screen space, where Y points down, so
    // a Vulkan pipeline that calls counter-clockwise "front" maps to
    // FrontCounterClockwise = FALSE (docs/backends.md section 6).
    rasterizer.FrontCounterClockwise = desc.bFrontFaceCW ? TRUE : FALSE;
    rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizer.DepthClipEnable = TRUE;
    rasterizer.MultisampleEnable = FALSE;
    rasterizer.AntialiasedLineEnable = FALSE;

    D3D12_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = desc.bDepthTest ? TRUE : FALSE;
    depth.DepthWriteMask = desc.bDepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
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
    const HRESULT result = pD3D12Device->CreateGraphicsPipelineState( &pso, IID_PPV_ARGS( &pState ) );
    if ( FAILED( result ) ) {
        OTDU_LOG_ERROR( "D3D12 scene: CreateGraphicsPipelineState failed (0x%08x)\n",
                        static_cast<uint32_t>( result ) );
        pDevice->flushPendingDebugMessages();
        return nullptr;
    }
    pipelines[key] = pState;

    if ( !shaderDumpDir.empty() ) {
        const std::string base = "pipeline" + std::to_string( pipelines.size() );
        dumpShader( ( base + "_vs" ).c_str(), *desc.pVS );
        dumpShader( ( base + "_ps" ).c_str(), *desc.pPS );
    }
    OTDU_LOG_INFO( "D3D12 scene: pipeline %u (%u attributes, %u stream(s), blend %u, cull %u, depth %s%s)\n",
                   static_cast<uint32_t>( pipelines.size() ), desc.NumAttributes, desc.NumStreams,
                   static_cast<uint32_t>( desc.Blend ), static_cast<uint32_t>( desc.Cull ),
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
                       uint32_t numTextures ) {
    if ( pDevice == nullptr || pPipeline == nullptr || mesh.pVertexBuffer == nullptr ||
         mesh.pIndexBuffer == nullptr || mesh.NumIndices == 0 ) {
        return;
    }
    ID3D12GraphicsCommandList *pList = pDevice->getCommandList();
    // The root signature comes first: a descriptor table set before it is an
    // error (D3D12 error #708), and the driver does not always survive it.
    pList->SetGraphicsRootSignature( static_cast<ID3D12RootSignature *>( pRootSignature ) );

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
        ID3D12DescriptorHeap *pHeap = static_cast<ID3D12DescriptorHeap *>( pSrvTable );
        ID3D12Device *pD3D12Device = pDevice->getDevice();
        const D3D12_CPU_DESCRIPTOR_HANDLE base = CpuHandle( srvTableCpuStart + index * srvDescriptorSize );
        for ( uint32_t i = 0; i < count; i++ ) {
            const D3D12_CPU_DESCRIPTOR_HANDLE source = CpuHandle( pTextureSrvs[i] );
            const D3D12_CPU_DESCRIPTOR_HANDLE destination =
                CpuHandle( base.ptr + static_cast<SIZE_T>( i ) * srvDescriptorSize );
            pD3D12Device->CopyDescriptorsSimple( 1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }
        pList->SetDescriptorHeaps( 1, &pHeap );
        pList->SetGraphicsRootDescriptorTable( 3, GpuHandle( srvTableGpuStart + index * srvDescriptorSize ) );
    }

    if ( pConstants != nullptr && constantsBytes > 0 ) {
        const uint32_t dwords = Min<uint32_t>( constantsBytes / 4, kMaxRootConstants );
        pList->SetGraphicsRoot32BitConstants( 2, dwords, pConstants, 0 );
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
