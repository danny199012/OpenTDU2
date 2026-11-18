#include "shared.h"
#include "render/d3d12/d3d12_mesh_builder.h"

#include "render/d3d12/d3d12_vertex_layout.h"

#if defined( OTDU_D3D12 )

#include <cmath>

namespace {
// The expansion always writes four floats - the SNORM packing and the UBYTE4
// index both have four components - so the rewritten declaration names
// VAF_POSITION_R32G32B32A32_SFLOAT and this file needs no DXGI table of its own.

// D3D10's 10:10:10:2 SNORM packing: 10-bit signed components scaled by 511, a
// 2-bit signed alpha scaled by 1 (so it can only be -1, 0 or 1).
float Snorm10( uint32_t bits ) {
    int32_t value = static_cast<int32_t>( bits & 0x3FFu );
    if ( value >= 512 ) {
        value -= 1024;
    }
    return static_cast<float>( value ) / 511.0f;
}

float Snorm2( uint32_t bits ) {
    int32_t value = static_cast<int32_t>( bits & 0x3u );
    if ( value >= 2 ) {
        value -= 4;
    }
    return static_cast<float>( value );
}

// Reads one vertex's attributes out of its source stream and writes the float
// components. 'pAttribute' is already the attribute's address inside the vertex
// (the caller adds the stream stride and the attribute's offset); reads only, so
// a stream can be decoded in place.
uint32_t DecodeAttribute( const uint8_t *pAttribute, const RawVertexAttribute &attribute, float outValues[4] ) {
    switch ( attribute.Format ) {
    case VAF_NORMAL_A2B10G10R10_SNORM_PACK32:
    case VAF_TANGENT_A2B10G10R10_SNORM_PACK32: {
        uint32_t packed = 0;
        memcpy( &packed, pAttribute, sizeof( packed ) );
        outValues[0] = Snorm10( packed );
        outValues[1] = Snorm10( packed >> 10 );
        outValues[2] = Snorm10( packed >> 20 );
        outValues[3] = Snorm2( packed >> 30 );
        return 4;
    }
    case VAF_BONEINDEX_R8G8B8A8_USCALED:
        for ( uint32_t component = 0; component < 4; component++ ) {
            outValues[component] = static_cast<float>( pAttribute[component] );
        }
        return 4;
    default:
        return 0;
    }
}
} // namespace

bool D3D12NeedsFormatExpansion( eVertexAttributeFormat format ) {
    switch ( format ) {
    case VAF_NORMAL_A2B10G10R10_SNORM_PACK32:
    case VAF_TANGENT_A2B10G10R10_SNORM_PACK32:
    case VAF_BONEINDEX_R8G8B8A8_USCALED:
        return true;
    default:
        return false;
    }
}

bool D3D12PrepareMesh( const GeometryPrimitive &primitive, D3D12MeshData &out ) {
    out = D3D12MeshData();
    const uint32_t numVertices = primitive.NumRawVertex;
    if ( numVertices == 0 || primitive.RawAttributes.empty() || primitive.RawStrides[0] == 0 ) {
        return false;
    }
    for ( const RawVertexAttribute &attribute : primitive.RawAttributes ) {
        if ( attribute.Stream >= 2 ) {
            return false; // a .3DG primitive has two streams (EdenMesh adds the third)
        }
    }
    out.NumVertices = numVertices;
    out.Indices = primitive.Indices;

    bool bNeedsExpansion = false;
    for ( const RawVertexAttribute &attribute : primitive.RawAttributes ) {
        if ( D3D12NeedsFormatExpansion( attribute.Format ) ) {
            bNeedsExpansion = true;
            break;
        }
    }

    if ( !bNeedsExpansion ) {
        // The common case: the primitive's own bytes and strides, streams
        // compacted to 0..n-1 (a primitive whose stream 0 is unused would
        // otherwise bind a null buffer).
        uint32_t newStream[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
        uint32_t next = 0;
        for ( uint32_t stream = 0; stream < 2; stream++ ) {
            if ( primitive.RawStreams[stream].empty() || primitive.RawStrides[stream] == 0 ) {
                continue;
            }
            newStream[stream] = next;
            out.Streams[next] = primitive.RawStreams[stream];
            out.Strides[next] = primitive.RawStrides[stream];
            next++;
        }
        if ( next == 0 ) {
            return false;
        }
        out.NumStreams = next;
        out.Attributes = primitive.RawAttributes;
        for ( RawVertexAttribute &attribute : out.Attributes ) {
            if ( attribute.Stream >= 2 || newStream[attribute.Stream] == 0xFFFFFFFFu ) {
                return false;
            }
            attribute.Stream = static_cast<uint8_t>( newStream[attribute.Stream] );
        }
        return true;
    }

    // An expansion: every attribute in one interleaved stream. The attributes
    // that have a DXGI format keep their bytes; the ones that do not are decoded
    // into floats (four of them each, so the stream stays 4-byte aligned).
    struct Placement {
        uint32_t Offset = 0;
        uint32_t Bytes = 0;
        eVertexAttributeFormat Format = VAF_Invalid; // what the rewritten attribute says
        bool bDecoded = false;
    };
    std::vector<Placement> placements( primitive.RawAttributes.size() );
    uint32_t stride = 0;
    for ( size_t i = 0; i < primitive.RawAttributes.size(); i++ ) {
        const RawVertexAttribute &attribute = primitive.RawAttributes[i];
        Placement &placement = placements[i];
        placement.Offset = stride;
        if ( D3D12NeedsFormatExpansion( attribute.Format ) ) {
            placement.bDecoded = true;
            placement.Bytes = 4 * sizeof( float );
            placement.Format = VAF_POSITION_R32G32B32A32_SFLOAT;
            out.NumExpandedAttributes++;
        } else if ( D3D12GetVertexFormat( attribute.Format ) != 0 ) {
            placement.Bytes = GetVertexAttributeSize( attribute.Format );
            placement.Format = attribute.Format;
        } else {
            // No DXGI format and no decoder: keep the bytes so the vertex is not
            // lost, and let the layout fall back to the synthetic stream (the
            // debug layer names it while the pipeline is created).
            placement.Bytes = GetVertexAttributeSize( attribute.Format );
            placement.Format = attribute.Format;
            OTDU_LOG_WARN( "D3D12 mesh: attribute usage %u.%u has no DXGI format (VAF %u) - raw bytes kept\n",
                           attribute.Usage, attribute.UsageIndex, static_cast<uint32_t>( attribute.Format ) );
        }
        stride += placement.Bytes;
        stride = ( stride + 3 ) & ~3u;
    }

    out.Streams[0].assign( static_cast<size_t>( stride ) * numVertices, 0 );
    out.Strides[0] = stride;
    out.NumStreams = 1;
    out.Attributes.resize( primitive.RawAttributes.size() );
    float values[4] = {};
    for ( size_t i = 0; i < primitive.RawAttributes.size(); i++ ) {
        const RawVertexAttribute &attribute = primitive.RawAttributes[i];
        const Placement &placement = placements[i];
        out.Attributes[i].Usage = attribute.Usage;
        out.Attributes[i].UsageIndex = attribute.UsageIndex;
        out.Attributes[i].Stream = 0;
        out.Attributes[i].Format = placement.Format;
        out.Attributes[i].Offset = static_cast<uint16_t>( placement.Offset );
        const uint8_t *pSourceStream = primitive.RawStreams[attribute.Stream].data();
        for ( uint32_t vertex = 0; vertex < numVertices; vertex++ ) {
            const uint8_t *pSource =
                pSourceStream + static_cast<size_t>( vertex ) * primitive.RawStrides[attribute.Stream] +
                attribute.Offset;
            uint8_t *pDest = out.Streams[0].data() + static_cast<size_t>( vertex ) * stride + placement.Offset;
            if ( placement.bDecoded ) {
                DecodeAttribute( pSource, attribute, values );
                memcpy( pDest, values, sizeof( values ) );
            } else {
                memcpy( pDest, pSource, placement.Bytes );
            }
        }
    }
    return true;
}

namespace {
// Packs a float into D3D10's 10:10:10:2 SNORM and returns the 32-bit word.
uint32_t PackSnorm( float x, float y, float z, float w ) {
    auto pack10 = []( float value ) {
        float clamped = value < -1.0f ? -1.0f : ( value > 1.0f ? 1.0f : value );
        int32_t quantised = static_cast<int32_t>( std::lround( clamped * 511.0f ) );
        return static_cast<uint32_t>( quantised ) & 0x3FFu;
    };
    auto pack2 = []( float value ) {
        int32_t quantised = static_cast<int32_t>( std::lround( value ) );
        quantised = quantised < -1 ? -1 : ( quantised > 1 ? 1 : quantised );
        return static_cast<uint32_t>( quantised ) & 0x3u;
    };
    return pack10( x ) | ( pack10( y ) << 10 ) | ( pack10( z ) << 20 ) | ( pack2( w ) << 30 );
}

float ReadFloat( const D3D12MeshData &data, uint32_t attribute, uint32_t vertex, uint32_t component ) {
    const RawVertexAttribute &element = data.Attributes[attribute];
    const uint8_t *pVertex =
        data.Streams[element.Stream].data() + static_cast<size_t>( vertex ) * data.Strides[element.Stream];
    float value = 0.0f;
    memcpy( &value, pVertex + element.Offset + component * sizeof( float ), sizeof( value ) );
    return value;
}

void ReadFloat3( const D3D12MeshData &data, uint32_t attribute, uint32_t vertex, float out[3] ) {
    for ( uint32_t component = 0; component < 3; component++ ) {
        out[component] = ReadFloat( data, attribute, vertex, component );
    }
}
} // namespace
bool D3D12RunMeshConvertTest() {
    // Two vertices: an interleaved stream 0 (position float3, packed SNORM
    // normal, uv float2) and a stream 1 with D3D9's UBYTE4 bone index - the two
    // formats that have no DXGI equivalent, next to three that do.
    struct Vertex {
        float Position[3];
        uint32_t Normal; // A2B10G10R10 SNORM
        float Texcoord[2];
    };
    Vertex vertices[2] = {};
    vertices[0].Position[0] = 1.0f;
    vertices[0].Position[1] = 2.0f;
    vertices[0].Position[2] = 3.0f;
    vertices[0].Normal = PackSnorm( -1.0f, 0.0f, 0.5f, -1.0f );
    vertices[0].Texcoord[0] = 0.25f;
    vertices[0].Texcoord[1] = 0.75f;
    vertices[1].Position[0] = 4.0f;
    vertices[1].Position[1] = 5.0f;
    vertices[1].Position[2] = 6.0f;
    vertices[1].Normal = PackSnorm( 0.0f, -1.0f, 0.0f, 0.0f );
    vertices[1].Texcoord[0] = 0.5f;
    vertices[1].Texcoord[1] = 0.0f;
    const uint8_t boneIndices[2][4] = { { 1, 2, 3, 4 }, { 255, 0, 0, 0 } };

    GeometryPrimitive primitive;
    primitive.NumRawVertex = 2;
    primitive.RawStreams[0].assign( reinterpret_cast<const uint8_t *>( vertices ),
                                    reinterpret_cast<const uint8_t *>( vertices + 2 ) );
    primitive.RawStrides[0] = sizeof( Vertex );
    primitive.RawStreams[1].assign( &boneIndices[0][0], &boneIndices[0][0] + sizeof( boneIndices ) );
    primitive.RawStrides[1] = 4;
    // D3DDECLUSAGE: 0 POSITION, 3 NORMAL, 5 TEXCOORD, 2 BLENDINDICES.
    primitive.RawAttributes = {
        { 0, 0, 0, VAF_POSITION_R32G32B32_SFLOAT, 0 },
        { 3, 0, 0, VAF_NORMAL_A2B10G10R10_SNORM_PACK32, 12 },
        { 5, 0, 0, VAF_TEXCOORD_R32G32_SFLOAT, 16 },
        { 2, 0, 1, VAF_BONEINDEX_R8G8B8A8_USCALED, 0 },
    };
    primitive.Indices = { 0, 1, 0 };

    D3D12MeshData data;
    if ( !D3D12PrepareMesh( primitive, data ) ) {
        OTDU_LOG_ERROR( "D3D12 mesh convert test: the primitive could not be prepared\n" );
        return false;
    }
    bool bPassed = true;
    auto check = [&]( bool bCondition, const char *pWhat ) {
        if ( !bCondition ) {
            OTDU_LOG_ERROR( "D3D12 mesh convert test: FAILED - %s\n", pWhat );
            bPassed = false;
        }
    };
    check( data.NumStreams == 1, "an expansion must fold into one stream" );
    check( data.NumExpandedAttributes == 2, "the packed normal and the bone index are the two expansions" );
    check( data.Attributes.size() == 4, "every attribute must survive" );
    check( data.Indices.size() == 3, "the triangle list must survive" );
    check( data.Strides[0] == 52, "the expanded stride (12 + 16 + 8 + 16, 4-byte aligned) must be 52" );
    for ( const RawVertexAttribute &attribute : data.Attributes ) {
        check( attribute.Stream == 0, "every expanded attribute lives in stream 0" );
    }
    check( data.Attributes[0].Offset == 0, "position at 0" );
    check( data.Attributes[1].Offset == 12, "normal at 12" );
    check( data.Attributes[2].Offset == 28, "uv at 28" );
    check( data.Attributes[3].Offset == 36, "bone index at 36" );

    // The values: a SNORM -1 must be -1.0 (not 0.5, not 0), a 10-bit 0.5 is
    // within one quantum (1/511), the 2-bit alpha of -1 is -1, and UScaled 255
    // is 255 (not 1).
    float normal0[3] = {};
    ReadFloat3( data, 1, 0, normal0 );
    check( std::fabs( normal0[0] + 1.0f ) < 1e-6f, "SNORM normal.x must decode to -1.0" );
    check( std::fabs( normal0[1] ) < 1e-6f, "SNORM normal.y must decode to 0" );
    check( std::fabs( normal0[2] - 0.5f ) <= ( 1.0f / 511.0f ) + 1e-6f, "SNORM normal.z must decode to ~0.5" );
    check( std::fabs( ReadFloat( data, 1, 0, 3 ) + 1.0f ) < 1e-6f, "the 2-bit SNORM alpha must decode to -1" );
    float normal1[3] = {};
    ReadFloat3( data, 1, 1, normal1 );
    check( std::fabs( normal1[1] + 1.0f ) < 1e-6f, "the second normal's -1 must survive" );

    float position[3] = {};
    ReadFloat3( data, 0, 1, position );
    check( position[0] == 4.0f && position[1] == 5.0f && position[2] == 6.0f,
           "a float3 attribute must be copied unchanged" );
    check( ReadFloat( data, 2, 0, 0 ) == 0.25f && ReadFloat( data, 2, 1, 0 ) == 0.5f,
           "a float2 attribute must be copied unchanged" );
    check( ReadFloat( data, 3, 0, 0 ) == 1.0f && ReadFloat( data, 3, 0, 3 ) == 4.0f,
           "UScaled bone indices must decode to their byte values" );
    check( ReadFloat( data, 3, 1, 0 ) == 255.0f, "UScaled 255 must decode to 255, not 1" );

    OTDU_LOG_INFO( "D3D12 mesh convert test: %s (stride %u, %u expanded attribute(s), SNORM -1 -> -1.0, "
                   "UScaled 255 -> 255.0)\n",
                   bPassed ? "PASSED" : "FAILED", data.Strides[0], data.NumExpandedAttributes );
    return bPassed;
}

#endif // OTDU_D3D12

