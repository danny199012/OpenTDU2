#include "shared.h"
#include "render/d3d12/d3d12_ui.h"

#if defined( OTDU_D3D12 )

#include "render/d3d12/d3d12_device.h"
#include "render/vulkan/render_device.h"
#include "system/cmd_line.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

namespace {
template <typename T> void SafeRelease( T *&pObject ) {
    if ( pObject != nullptr ) {
        pObject->Release();
        pObject = nullptr;
    }
}

uint64_t AlignUp( uint64_t value, uint64_t alignment ) {
    return ( value + alignment - 1 ) & ~( alignment - 1 );
}

// The ui2d.* pair of resources/shaders, written out in HLSL: same vertex
// layout, same alpha test for the font atlas, same saturation mix. D3DCompile
// turns it into SM 5.0 DXBC, which D3D12 accepts directly - the SPIR-V ->
// HLSL -> DXIL route of docs/backends.md is not needed for the 2D layer.
const char *kUIShader = R"(
cbuffer UITransform : register( b0 )
{
    float4x4 Projection;
    float4 Params; // x: texture mode (1: alpha only), y: saturation
};

Texture2D Texture0 : register( t0 );
SamplerState Sampler0 : register( s0 );

struct VSInput
{
    float2 Position : POSITION;
    float2 Texcoord : TEXCOORD0;
    float4 Color    : COLOR;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float2 Texcoord : TEXCOORD0;
    float4 Color    : COLOR;
};

VSOutput VSMain( VSInput input )
{
    VSOutput output;
    output.Position = mul( Projection, float4( input.Position, 0.0, 1.0 ) );
    output.Texcoord = input.Texcoord;
    output.Color = input.Color;
    return output;
}

float4 PSMain( VSOutput input ) : SV_Target
{
    float4 texel = Texture0.Sample( Sampler0, input.Texcoord );
    if ( Params.x > 0.5 )
    {
        texel = float4( 1.0, 1.0, 1.0, texel.r );
    }
    float4 color = texel * input.Color;
    float luma = dot( color.rgb, float3( 0.299, 0.587, 0.114 ) );
    color.rgb = lerp( float3( luma, luma, luma ), color.rgb, Params.y );
    return color;
}
)";

ID3DBlob *CompileShader( const char *pEntryPoint, const char *pTarget ) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( _DEBUG )
    flags |= D3DCOMPILE_DEBUG;
#endif
    ID3DBlob *pShader = nullptr;
    ID3DBlob *pErrors = nullptr;
    const HRESULT result = D3DCompile( kUIShader, strlen( kUIShader ), "d3d12_ui", nullptr,
                                       nullptr, pEntryPoint, pTarget, flags, 0, &pShader, &pErrors );
    if ( FAILED( result ) ) {
        OTDU_LOG_ERROR( "D3D12 UI: %s failed to compile: %s\n", pEntryPoint,
                        pErrors != nullptr ? static_cast<const char *>( pErrors->GetBufferPointer() )
                                           : "no message" );
        SafeRelease( pShader );
    } else if ( pErrors != nullptr ) {
        OTDU_LOG_INFO( "D3D12 UI: %s compiled with notes: %s\n", pEntryPoint,
                       static_cast<const char *>( pErrors->GetBufferPointer() ) );
    }
    SafeRelease( pErrors );
    return pShader;
}
} // namespace

D3D12UI::D3D12UI() = default;

bool D3D12UI::createRootSignature() {
    ID3D12Device *pD3D12Device = pDevice->getDevice();
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // A static sampler: the 2D layers only ever clamp and filter linearly (the
    // Vulkan side picks SF_Linear / SA_Clamp for every batch), so the sampler
    // costs no descriptor.
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob *pSignature = nullptr;
    ID3DBlob *pErrors = nullptr;
    const HRESULT result =
        D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &pSignature, &pErrors );
    if ( FAILED( result ) ) {
        OTDU_LOG_ERROR( "D3D12 UI: the root signature could not be serialized: %s\n",
                        pErrors != nullptr ? static_cast<const char *>( pErrors->GetBufferPointer() )
                                           : "no message" );
        SafeRelease( pSignature );
        SafeRelease( pErrors );
        return false;
    }
    SafeRelease( pErrors );

    ID3D12RootSignature *pSignatureObject = nullptr;
    const HRESULT created = pD3D12Device->CreateRootSignature(
        0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(),
        IID_PPV_ARGS( &pSignatureObject ) );
    SafeRelease( pSignature );
    if ( FAILED( created ) ) {
        OTDU_LOG_ERROR( "D3D12 UI: CreateRootSignature failed (0x%08x)\n",
                        static_cast<uint32_t>( created ) );
        return false;
    }
    pRootSignature = pSignatureObject;
    return true;
}

bool D3D12UI::createPipelines() {
    ID3D12Device *pD3D12Device = pDevice->getDevice();
    ID3DBlob *pVertexShader = CompileShader( "VSMain", "vs_5_0" );
    ID3DBlob *pPixelShader = CompileShader( "PSMain", "ps_5_0" );
    if ( pVertexShader == nullptr || pPixelShader == nullptr ) {
        SafeRelease( pVertexShader );
        SafeRelease( pPixelShader );
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC layout[3] = {};
    layout[0] = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    layout[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    layout[2] = { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16,
                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

    for ( uint32_t mode = 0; mode < 4; mode++ ) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = static_cast<ID3D12RootSignature *>( pRootSignature );
        desc.VS.pShaderBytecode = pVertexShader->GetBufferPointer();
        desc.VS.BytecodeLength = pVertexShader->GetBufferSize();
        desc.PS.pShaderBytecode = pPixelShader->GetBufferPointer();
        desc.PS.BytecodeLength = pPixelShader->GetBufferSize();
        desc.InputLayout.pInputElementDescs = layout;
        desc.InputLayout.NumElements = 3;

        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FrontCounterClockwise = FALSE;
        desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.RasterizerState.MultisampleEnable = FALSE;
        desc.RasterizerState.AntialiasedLineEnable = FALSE;
        desc.RasterizerState.ForcedSampleCount = 0;
        desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        desc.BlendState.AlphaToCoverageEnable = FALSE;
        desc.BlendState.IndependentBlendEnable = FALSE;
        D3D12_RENDER_TARGET_BLEND_DESC &blend = desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.LogicOpEnable = FALSE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;

        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.DepthStencilState.StencilEnable = mode != 0;
        desc.DepthStencilState.StencilReadMask = 0xff;
        desc.DepthStencilState.StencilWriteMask = 0xff;
        D3D12_DEPTH_STENCILOP_DESC stencil = {};
        stencil.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        stencil.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        if ( mode == 3 ) {
            // SM_TestEqual: the masked children draw where the mask counted.
            stencil.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
            stencil.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        } else {
            stencil.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            stencil.StencilPassOp = mode == 1 ? D3D12_STENCIL_OP_INCR_SAT : D3D12_STENCIL_OP_DECR_SAT;
        }
        desc.DepthStencilState.FrontFace = stencil;
        desc.DepthStencilState.BackFace = stencil;
        // A mask shape counts in the stencil and writes no colour (the Vulkan
        // side's increment / decrement pipelines, which the Flash masks use).
        blend.RenderTargetWriteMask = ( mode == 1 || mode == 2 ) ? 0 : D3D12_COLOR_WRITE_ENABLE_ALL;

        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat = static_cast<DXGI_FORMAT>( D3D12Device::kDepthStencilFormatDXGI );
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState *pPipeline = nullptr;
        const HRESULT result =
            pD3D12Device->CreateGraphicsPipelineState( &desc, IID_PPV_ARGS( &pPipeline ) );
        if ( FAILED( result ) ) {
            OTDU_LOG_ERROR( "D3D12 UI: the 2D pipeline (stencil mode %u) failed (0x%08x)\n", mode,
                            static_cast<uint32_t>( result ) );
            SafeRelease( pVertexShader );
            SafeRelease( pPixelShader );
            return false;
        }
        ppPipelines[mode] = pPipeline;
    }

    SafeRelease( pVertexShader );
    SafeRelease( pPixelShader );
    return true;
}

D3D12UI::~D3D12UI() { destroy(); }

bool D3D12UI::initialize( D3D12Device *pDevice ) {
    this->pDevice = pDevice;
    if ( pDevice == nullptr || pDevice->getDevice() == nullptr ) {
        return false;
    }
    bLogBatches = CmdLine::HasArg( "-d3d12-logui" ) ||
                  CmdLine::HasArg( "-d3d12-debug" );
    if ( !createRootSignature() ) {
        destroy();
        return false;
    }
    if ( !createPipelines() ) {
        destroy();
        return false;
    }

    // One upload buffer per frame slot, mapped for the whole session: the
    // constants and the vertices are written straight into the ring.
    for ( uint32_t slot = 0; slot < kFrameSlots; slot++ ) {
        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kUploadBytesPerFrame;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ID3D12Resource *pBuffer = nullptr;
        if ( FAILED( pDevice->getDevice()->CreateCommittedResource(
                &heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &pBuffer ) ) ) ) {
            OTDU_LOG_ERROR( "D3D12 UI: the vertex upload ring could not be created\n" );
            destroy();
            return false;
        }
        void *pMapped = nullptr;
        if ( FAILED( pBuffer->Map( 0, nullptr, &pMapped ) ) ) {
            pBuffer->Release();
            OTDU_LOG_ERROR( "D3D12 UI: the vertex upload ring could not be mapped\n" );
            destroy();
            return false;
        }
        ppUploadBuffer[slot] = pBuffer;
        ppMapped[slot] = pMapped;
        pUploadCursor[slot] = 0;
    }
    return true;
}

void D3D12UI::destroy() {
    for ( uint32_t mode = 0; mode < 4; mode++ ) {
        ID3D12PipelineState *pPipeline = static_cast<ID3D12PipelineState *>( ppPipelines[mode] );
        ppPipelines[mode] = nullptr;
        if ( pPipeline != nullptr ) {
            pPipeline->Release();
        }
    }
    for ( uint32_t slot = 0; slot < kFrameSlots; slot++ ) {
        ID3D12Resource *pBuffer = static_cast<ID3D12Resource *>( ppUploadBuffer[slot] );
        ppUploadBuffer[slot] = nullptr;
        ppMapped[slot] = nullptr;
        pUploadCursor[slot] = 0;
        if ( pBuffer != nullptr ) {
            pBuffer->Unmap( 0, nullptr );
            pBuffer->Release();
        }
    }
    ID3D12RootSignature *pSignature = static_cast<ID3D12RootSignature *>( pRootSignature );
    pRootSignature = nullptr;
    if ( pSignature != nullptr ) {
        pSignature->Release();
    }
    pDevice = nullptr;
    bOverflowReported = false;
}

void D3D12UI::beginFrame() {
    if ( pDevice == nullptr ) {
        return;
    }
    const uint32_t slot = pDevice->getFrameSlot();
    if ( slot < kFrameSlots ) {
        pUploadCursor[slot] = 0;
    }
}

void D3D12UI::draw( const void *pVertices,
                    uint32_t numVertices,
                    const GPUTexture *pTexture,
                    const float *pProjection,
                    int32_t scissorX,
                    int32_t scissorY,
                    int32_t scissorWidth,
                    int32_t scissorHeight,
                    bool bAlphaOnly,
                    float saturation,
                    uint32_t stencilMode,
                    uint32_t stencilReference ) {
    if ( pDevice == nullptr || numVertices == 0 || pVertices == nullptr || pProjection == nullptr ) {
        return;
    }
    ID3D12GraphicsCommandList *pList = pDevice->getCommandList();
    if ( pList == nullptr ) {
        return;
    }
    if ( pTexture == nullptr || pTexture->D3D12SrvGpu == 0 ) {
        // Nothing to sample (a texture that failed to upload): the batch would
        // be undefined on D3D12, unlike Vulkan's placeholder binding.
        static bool bLoggedSkip = false;
        if ( !bLoggedSkip ) {
            bLoggedSkip = true;
            OTDU_LOG_WARN( "D3D12 UI: a %u vertex batch has no SRV ('%s') - skipped\n", numVertices,
                           pTexture != nullptr ? pTexture->Name.c_str() : "(null)" );
        }
        return;
    }
    const uint32_t slot = pDevice->getFrameSlot();
    if ( slot >= kFrameSlots || ppMapped[slot] == nullptr ) {
        return;
    }

    // Layout of one batch in the ring: the 80 byte constants (16 floats of
    // projection + 4 of params, 256 byte aligned for a root CBV), then the
    // vertices.
    const uint64_t constantsBytes = sizeof( float ) * 20;
    const uint64_t constantsOffset = AlignUp( pUploadCursor[slot], 256 );
    const uint64_t vertexOffset = AlignUp( constantsOffset + constantsBytes, 16 );
    const uint64_t vertexBytes = static_cast<uint64_t>( numVertices ) * kVertexStride;
    if ( vertexOffset + vertexBytes > kUploadBytesPerFrame ) {
        if ( !bOverflowReported ) {
            bOverflowReported = true;
            OTDU_LOG_WARN( "D3D12 UI: the per frame upload ring (%llu KB) is full; the rest of "
                           "this frame's 2D layers are dropped\n",
                           static_cast<unsigned long long>( kUploadBytesPerFrame / 1024 ) );
        }
        beginFrame();
        return;
    }

    uint8_t *pBase = static_cast<uint8_t *>( ppMapped[slot] );
    memcpy( pBase + constantsOffset, pProjection, sizeof( float ) * 16 );
    float params[4] = { bAlphaOnly ? 1.0f : 0.0f, saturation, 0.0f, 0.0f };
    memcpy( pBase + constantsOffset + sizeof( float ) * 16, params, sizeof( params ) );
    memcpy( pBase + vertexOffset, pVertices, static_cast<size_t>( vertexBytes ) );
    pUploadCursor[slot] = vertexOffset + vertexBytes;

    ID3D12Resource *pBuffer = static_cast<ID3D12Resource *>( ppUploadBuffer[slot] );
    const D3D12_GPU_VIRTUAL_ADDRESS bufferAddress = pBuffer->GetGPUVirtualAddress();

    static bool bLoggedFirstBatch = false;
    if ( bLogBatches && !bLoggedFirstBatch ) {
        bLoggedFirstBatch = true;
        OTDU_LOG_INFO( "D3D12 UI: first batch %u vertices, texture '%s' (%ux%u)\n", numVertices,
                       pTexture->Name.c_str(), pTexture->Width, pTexture->Height );
    }

    ID3D12DescriptorHeap *ppHeaps[] = { pDevice->getSrvHeap() };
    pList->SetGraphicsRootSignature( static_cast<ID3D12RootSignature *>( pRootSignature ) );
    pList->SetDescriptorHeaps( 1, ppHeaps );
    pList->SetPipelineState( static_cast<ID3D12PipelineState *>(
        ppPipelines[stencilMode < 4 ? stencilMode : 0] ) );
    D3D12_GPU_DESCRIPTOR_HANDLE table = {};
    table.ptr = pTexture->D3D12SrvGpu;
    pList->SetGraphicsRootDescriptorTable( 0, table );
    pList->SetGraphicsRootConstantBufferView( 1, bufferAddress + constantsOffset );

    D3D12_VERTEX_BUFFER_VIEW vertexView = {};
    vertexView.BufferLocation = bufferAddress + vertexOffset;
    vertexView.SizeInBytes = static_cast<UINT>( vertexBytes );
    vertexView.StrideInBytes = kVertexStride;
    pList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    pList->IASetVertexBuffers( 0, 1, &vertexView );

    // The scissor the 2D batcher computed (a clip rectangle, or the whole
    // backbuffer). The viewport stays the one beginFrame set.
    const int32_t width = static_cast<int32_t>( pDevice->getBackbufferWidth() );
    const int32_t height = static_cast<int32_t>( pDevice->getBackbufferHeight() );
    D3D12_RECT scissor = { scissorX, scissorY,
                           Min( scissorX + scissorWidth, width ),
                           Min( scissorY + scissorHeight, height ) };
    if ( scissor.right <= scissor.left || scissor.bottom <= scissor.top ) {
        return;
    }
    pList->RSSetScissorRects( 1, &scissor );
    if ( stencilMode != 0 ) {
        pList->OMSetStencilRef( stencilReference );
    }
    pList->DrawInstanced( numVertices, 1, 0, 0 );

    // Per frame batch accounting (-d3d12-logui): a frame whose art did not
    // appear is otherwise indistinguishable from one whose vertices never
    // reached the pipeline.
    frameBatches++;
    frameVertices += numVertices;
    if ( stencilMode == 0 || stencilMode == 3 ) {
        frameColourBatches++;
    }
    const uint64_t frame = pDevice->getFrameNumber();
    if ( frameDrawFrame != frame ) {
        if ( bLogBatches && frameDrawFrame != 0 ) {
            OTDU_LOG_INFO( "D3D12 UI: frame %llu: %u batches (%u colour), %u vertices\n",
                           static_cast<unsigned long long>( frameDrawFrame ), frameBatches,
                           frameColourBatches, frameVertices );
        }
        frameDrawFrame = frame;
        frameBatches = 0;
        frameVertices = 0;
        frameColourBatches = 0;
    }
}

#else // !OTDU_D3D12

D3D12UI::D3D12UI() = default;
D3D12UI::~D3D12UI() {}
bool D3D12UI::initialize( D3D12Device * ) { return false; }
void D3D12UI::destroy() {}
void D3D12UI::beginFrame() {}
void D3D12UI::draw( const void *, uint32_t, const GPUTexture *, const float *, int32_t, int32_t,
                    int32_t, int32_t, bool, float, uint32_t, uint32_t ) {}

#endif // OTDU_D3D12
