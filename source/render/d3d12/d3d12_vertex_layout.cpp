#include "shared.h"
#include "render/d3d12/d3d12_vertex_layout.h"

#include "render/3dg.h"
#include "render/shaders/dxso_translator.h"

#if defined( OTDU_D3D12 )

namespace {
// EdenMesh::kMaxStreams: the stream the fallback attributes live in, so a
// draw's real streams 0..2 can never collide with them.
constexpr uint32_t kFallbackStream = 3;
// Four float4s: the fallback offsets are 0, 16, 32 and 48 (below).
constexpr uint32_t kFallbackStride = 64;

// DXGI_FORMAT values, kept as integers so this translation unit stays free of
// dxgiformat.h (the same convention D3D12Device uses for the swapchain).
constexpr uint32_t kDxgiUnknown = 0;
constexpr uint32_t kDxgiR32G32B32A32Float = 2;
constexpr uint32_t kDxgiR32G32B32Float = 6;
constexpr uint32_t kDxgiR16G16B16A16Float = 10;
constexpr uint32_t kDxgiR16G16B16A16Unorm = 11;
constexpr uint32_t kDxgiR16G16B16A16Snorm = 13;
constexpr uint32_t kDxgiR32G32Float = 16;
constexpr uint32_t kDxgiR10G10B10A2Unorm = 24;
constexpr uint32_t kDxgiR8G8B8A8Unorm = 28;
constexpr uint32_t kDxgiR8G8B8A8Uint = 30;
constexpr uint32_t kDxgiR16G16Float = 34;
constexpr uint32_t kDxgiR16G16Snorm = 36;
constexpr uint32_t kDxgiR32Float = 41;
constexpr uint32_t kDxgiB8G8R8A8Unorm = 87;

// A declaration-only layout's guess for a D3DDECLUSAGE: the shape the game's
// own meshes store it in. The pipeline state does not look at these (D3D12
// links the stages by *semantic*), they only have to describe a legal stream.
struct UsageFormat {
    uint32_t Dxgi = kDxgiR32G32B32A32Float;
    uint32_t Bytes = 16;
};

UsageFormat FormatForUsage( uint8_t usage ) {
    switch ( usage ) {
    case 0: // POSITION
    case 3: // NORMAL
    case 6: // TANGENT
    case 7: // BINORMAL
        return { kDxgiR32G32B32Float, 12 };
    case 1: // BLENDWEIGHT
    case 4: // PSIZE
    case 8: // TESSFACTOR
    case 11: // FOG
    case 12: // DEPTH
        return { kDxgiR32Float, 4 };
    case 2: // BLENDINDICES
        return { kDxgiR8G8B8A8Uint, 4 };
    case 5: // TEXCOORD
        return { kDxgiR32G32Float, 8 };
    case 9: // POSITIONT
        return { kDxgiR32G32B32A32Float, 16 };
    case 10: // COLOR
        return { kDxgiR8G8B8A8Unorm, 4 };
    default:
        return { kDxgiR32G32B32A32Float, 16 };
    }
}

// Where the fallback stream puts a dcl: the offsets the Vulkan path uses
// (eden_renderer.cpp), so both backends describe the same synthetic data.
uint32_t FallbackOffset( uint8_t usage ) {
    switch ( usage ) {
    case 10: // COLOR
        return 16;
    case 3: // NORMAL
        return 32;
    case 6: // TANGENT
    case 7: // BINORMAL
        return 48;
    default:
        return 0;
    }
}

void EnsureStream( D3D12VertexLayout &layout, uint32_t stream, uint32_t stride ) {
    if ( layout.Strides.size() <= stream ) {
        layout.Strides.resize( stream + 1, 0 );
    }
    if ( layout.Strides[stream] < stride ) {
        layout.Strides[stream] = stride;
    }
    if ( layout.NumStreams < stream + 1 ) {
        layout.NumStreams = stream + 1;
    }
}
} // namespace

uint32_t D3D12GetVertexFormat( eVertexAttributeFormat format ) {
    switch ( format ) {
    case VAF_POSITION_R32G32B32_SFLOAT:
    case VAF_NORMAL_R32G32B32_SFLOAT:
    case VAF_TANGENT_R32G32B32_SFLOAT:
    case VAF_BONEWEIGHT_R32G32B32_SFLOAT:
        return kDxgiR32G32B32Float;
    case VAF_POSITION_R32G32B32A32_SFLOAT:
        return kDxgiR32G32B32A32Float;
    case VAF_POSITION_R16G16B16A16_SFLOAT:
    case VAF_NORMAL_R16G16B16A16_SFLOAT:
    case VAF_TANGENT_R16G16B16A16_SFLOAT:
        return kDxgiR16G16B16A16Float;
    case VAF_COLOR_B8G8R8A8_UNORM:
    case VAF_COLOR_B8G8R8A8_UNORM_2:
    case VAF_COLOR_B8G8R8A8_UNORM_3:
    case VAF_COLOR_B8G8R8A8_UNORM_4:
        return kDxgiB8G8R8A8Unorm;
    case VAF_NORMAL_A2B10G10R10_SNORM_PACK32:
    case VAF_TANGENT_A2B10G10R10_SNORM_PACK32:
        // No DXGI SNORM equivalent of 10:10:10:2 exists; the caller counts it
        // (NumExpandedFormats) and the mesh path has to expand the stream.
        return kDxgiR10G10B10A2Unorm;
    case VAF_NORMAL_R16G16B16A16_UNORM:
        return kDxgiR16G16B16A16Unorm;
    case VAF_NORMAL_R16G16B16A16_SNORM:
    case VAF_TANGENT_R16G16B16A16_SNORM:
        return kDxgiR16G16B16A16Snorm;
    case VAF_TEXCOORD_R32G32_SFLOAT:
    case VAF_BONEWEIGHT_R32G32_SFLOAT:
        return kDxgiR32G32Float;
    case VAF_TEXCOORD_R16G16_SNORM:
        return kDxgiR16G16Snorm;
    case VAF_TEXCOORD_R16G16_SFLOAT:
        return kDxgiR16G16Float;
    case VAF_POSITION_R32_SFLOAT:
    case VAF_BONEWEIGHT_R32_SFLOAT:
        return kDxgiR32Float;
    case VAF_BONEINDEX_R8G8B8A8_USCALED:
        // Vulkan converts the byte to float on fetch (USCALED); Direct3D 12 has
        // no such format, and the vertex shader reads it as an integer index,
        // which is what the byte already is.
        return kDxgiR8G8B8A8Uint;
    default:
        return kDxgiUnknown;
    }
}

bool D3D12BuildVertexLayout( const TranslatedShader &vs,
                             const RawVertexAttribute *pAttributes,
                             uint32_t numAttributes,
                             const uint32_t *pMeshStrides,
                             uint32_t numMeshStreams,
                             D3D12VertexLayout &out ) {
    out.Attributes.clear();
    out.Strides.clear();
    out.NumStreams = 0;
    out.NumFallbackAttributes = 0;
    out.NumExpandedFormats = 0;
    if ( vs.Inputs.empty() ) {
        // A screen-space program: an empty input layout and no vertex buffer.
        return false;
    }

    const bool bMesh = pAttributes != nullptr && numAttributes > 0;
    if ( bMesh ) {
        for ( uint32_t stream = 0; stream < numMeshStreams; stream++ ) {
            EnsureStream( out, stream, pMeshStrides != nullptr ? pMeshStrides[stream] : 0 );
        }
    }

    uint32_t cursor = 0;
    for ( const ShaderInputInfo &input : vs.Inputs ) {
        D3D12Scene::Attribute attribute;
        attribute.Location = input.Location;

        if ( bMesh ) {
            const RawVertexAttribute *pMatch = nullptr;
            for ( uint32_t i = 0; i < numAttributes; i++ ) {
                if ( pAttributes[i].Usage == input.Usage && pAttributes[i].UsageIndex == input.UsageIndex ) {
                    pMatch = &pAttributes[i];
                    break;
                }
            }
            const uint32_t format = pMatch != nullptr ? D3D12GetVertexFormat( pMatch->Format ) : kDxgiUnknown;
            if ( pMatch != nullptr && format != kDxgiUnknown && pMatch->Stream < numMeshStreams ) {
                attribute.Stream = pMatch->Stream;
                attribute.Format = format;
                attribute.Offset = pMatch->Offset;
                EnsureStream( out, attribute.Stream, attribute.Offset + GetVertexAttributeSize( pMatch->Format ) );
                if ( pMatch->Format == VAF_NORMAL_A2B10G10R10_SNORM_PACK32 ||
                     pMatch->Format == VAF_TANGENT_A2B10G10R10_SNORM_PACK32 ) {
                    out.NumExpandedFormats++;
                }
                out.Attributes.push_back( attribute );
                continue;
            }
        } else {
            const UsageFormat usage = FormatForUsage( input.Usage );
            attribute.Stream = 0;
            attribute.Format = usage.Dxgi;
            attribute.Offset = cursor;
            cursor += usage.Bytes;
            // Keep every offset on a 4-byte boundary so the declaration-only
            // layout could address a real stream if it ever had to.
            cursor = ( cursor + 3 ) & ~3u;
            EnsureStream( out, 0, cursor );
            out.Attributes.push_back( attribute );
            continue;
        }

        // A dcl the mesh does not carry: the fallback stream, one float4 each.
        attribute.Stream = kFallbackStream;
        attribute.Format = kDxgiR32G32B32A32Float;
        attribute.Offset = FallbackOffset( input.Usage );
        EnsureStream( out, kFallbackStream, kFallbackStride );
        out.NumFallbackAttributes++;
        out.Attributes.push_back( attribute );
    }
    return true;
}

#endif // OTDU_D3D12
