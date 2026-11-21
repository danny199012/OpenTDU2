#include "shared.h"
#include "render_resources_write.h"

#include "render/2dm.h"
#include "render/2db.h"
#include "render/render_file_write.h"

#include <cmath>
#include <cstring>

namespace {

constexpr uint32_t k2DBMagic = MakeFourCC( '.', '2', 'D', 'B' );
constexpr uint32_t kBitmapMagic = MakeFourCC( 'B', 'M', 'A', 'P' );
constexpr uint32_t k2DMMagic = MakeFourCC( '.', '2', 'D', 'M' );
constexpr uint32_t kMaterialArrayMagic = MakeFourCC( 'M', 'A', 'T', 'A' );
constexpr uint32_t kMaterialMagic = MakeFourCC( 'M', 'A', 'T', '.' );
constexpr uint32_t kHashMagic = MakeFourCC( 'H', 'A', 'S', 'H' );
constexpr uint32_t k3DGMagic = MakeFourCC( '.', '3', 'D', 'G' );
constexpr uint32_t kGeometryArrayMagic = MakeFourCC( 'G', 'E', 'O', 'A' );
constexpr uint32_t kGeometryMagic = MakeFourCC( 'G', 'E', 'O', 'M' );
constexpr uint32_t kPrimitiveMagic = MakeFourCC( 'P', 'R', 'I', 'M' );
constexpr uint32_t kVertexBufferArrayMagic = MakeFourCC( 'V', 'B', 'A', '.' );
constexpr uint32_t kIndexBufferArrayMagic = MakeFourCC( 'I', 'B', 'A', '.' );
constexpr uint32_t kVertexBufferMagic = MakeFourCC( 'D', 'X', 'V', 'B' );
constexpr uint32_t kIndexBufferMagic = MakeFourCC( 'D', 'X', 'I', 'B' );
constexpr uint32_t k3DDMagic = MakeFourCC( '.', '3', 'D', 'D' );
constexpr uint32_t kMaterialBankArrayMagic = MakeFourCC( 'M', 'B', 'N', 'A' );
constexpr uint32_t kMaterialBankMagic = MakeFourCC( 'M', 'B', 'N', 'K' );
constexpr uint32_t kObjectArrayMagic = MakeFourCC( 'O', 'B', 'J', 'A' );
constexpr uint32_t kInstanceArrayMagic = MakeFourCC( 'I', 'N', 'S', 'A' );
constexpr uint32_t kHierarchyArrayMagic = MakeFourCC( 'H', 'I', 'E', 'A' );
constexpr uint32_t kInstanceMagic = MakeFourCC( 'I', 'N', 'S', '.' );
constexpr uint32_t kHierarchyMagic = MakeFourCC( 'H', 'I', 'E', '.' );

// eViewFormat of the authored textures (D3D9 A8R8G8B8 - the byte order B, G,
// R, A; the texture cache uploads it natively, GetVulkanFormat / GetDXGIFormat).
constexpr uint32_t kViewFormatA8R8G8B8 = 3;

void AppendU16( std::vector<uint8_t> &out, uint16_t v ) {
    out.push_back( static_cast<uint8_t>( v & 0xff ) );
    out.push_back( static_cast<uint8_t>( ( v >> 8 ) & 0xff ) );
}
void AppendU32( std::vector<uint8_t> &out, uint32_t v ) {
    for ( uint32_t i = 0; i < 4; i++ ) {
        out.push_back( static_cast<uint8_t>( ( v >> ( i * 8 ) ) & 0xff ) );
    }
}
void AppendU64( std::vector<uint8_t> &out, uint64_t v ) {
    for ( uint32_t i = 0; i < 8; i++ ) {
        out.push_back( static_cast<uint8_t>( ( v >> ( i * 8 ) ) & 0xff ) );
    }
}
void AppendF32( std::vector<uint8_t> &out, float v ) {
    uint32_t bits;
    memcpy( &bits, &v, 4 );
    AppendU32( out, bits );
}
void PutU16( std::vector<uint8_t> &data, size_t at, uint16_t v ) {
    memcpy( data.data() + at, &v, 2 );
}
void PutU32( std::vector<uint8_t> &data, size_t at, uint32_t v ) {
    memcpy( data.data() + at, &v, 4 );
}
void PutF32( std::vector<uint8_t> &data, size_t at, float v ) {
    uint32_t bits;
    memcpy( &bits, &v, 4 );
    PutU32( data, at, bits );
}

// The section layout: total size = 0x10 header + payload + children + trailing
// (WriteSection's accounting); the children start right after the payload.
uint32_t SectionBytes( const RenderSectionWrite &section ) {
    uint32_t children = 0;
    for ( const RenderSectionWrite &child : section.Children ) {
        children += SectionBytes( child );
    }
    return 0x10 + static_cast<uint32_t>( section.Payload.size() + section.Trailing.size() ) + children;
}
uint32_t ChildOffset( const RenderSectionWrite &parent, uint32_t parentAbs, size_t index ) {
    uint32_t at = parentAbs + 0x10 + static_cast<uint32_t>( parent.Payload.size() + parent.Trailing.size() );
    for ( size_t i = 0; i < index && i < parent.Children.size(); i++ ) {
        at += SectionBytes( parent.Children[ i ] );
    }
    return at;
}

// One box-filter mip step (RGBA8, even dimensions; odd ones round down).
void DownsampleRGBA( const uint8_t *pSrc, uint32_t w, uint32_t h, std::vector<uint8_t> &out ) {
    const uint32_t w2 = Max<uint32_t>( w / 2, 1u );
    const uint32_t h2 = Max<uint32_t>( h / 2, 1u );
    out.resize( static_cast<size_t>( w2 ) * h2 * 4 );
    for ( uint32_t y = 0; y < h2; y++ ) {
        for ( uint32_t x = 0; x < w2; x++ ) {
            const uint32_t x0 = Min( x * 2, w - 1 ), x1 = Min( x * 2 + 1, w - 1 );
            const uint32_t y0 = Min( y * 2, h - 1 ), y1 = Min( y * 2 + 1, h - 1 );
            for ( uint32_t c = 0; c < 4; c++ ) {
                const uint32_t sum = static_cast<uint32_t>( pSrc[ ( y0 * w + x0 ) * 4 + c ] ) +
                                     pSrc[ ( y0 * w + x1 ) * 4 + c ] + pSrc[ ( y1 * w + x0 ) * 4 + c ] +
                                     pSrc[ ( y1 * w + x1 ) * 4 + c ];
                out[ ( y * w2 + x ) * 4 + c ] = static_cast<uint8_t>( sum / 4 );
            }
        }
    }
}

} // namespace

uint64_t HashResourceName( const char *pName ) {
    // FNV-1a 64: the rsc values of the authored files (entities.xmb /
    // hashcodes.xmb reference each other by value; the retail values are
    // Eden's own path hash - any unique stable value works for the readers).
    uint64_t hash = 0xcbf29ce484222325ull;
    for ( size_t i = 0; pName[ i ] != '\0'; i++ ) {
        hash ^= static_cast<uint8_t>( pName[ i ] );
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool Build2dbFromRGBA( const char *pName, const uint8_t *pRGBA, uint32_t width, uint32_t height, bool bGenMips,
                       std::vector<uint8_t> &outBytes ) {
    outBytes.clear();
    if ( pRGBA == nullptr || width == 0 || height == 0 ) {
        return false;
    }
    // The mip chain (the parser clamps the chain to the available data, and a
    // 1-mip texture would shimmer at distance).
    std::vector<std::vector<uint8_t>> mips;
    {
        // RGBA -> BGRA (A8R8G8B8's byte order).
        std::vector<uint8_t> bgra( static_cast<size_t>( width ) * height * 4 );
        for ( size_t i = 0; i < static_cast<size_t>( width ) * height; i++ ) {
            bgra[ i * 4 + 0 ] = pRGBA[ i * 4 + 2 ];
            bgra[ i * 4 + 1 ] = pRGBA[ i * 4 + 1 ];
            bgra[ i * 4 + 2 ] = pRGBA[ i * 4 + 0 ];
            bgra[ i * 4 + 3 ] = pRGBA[ i * 4 + 3 ];
        }
        mips.push_back( std::move( bgra ) );
        if ( bGenMips ) {
            uint32_t w = width, h = height;
            while ( w > 1 || h > 1 ) {
                std::vector<uint8_t> next;
                DownsampleRGBA( mips.back().data(), w, h, next );
                w = Max<uint32_t>( w / 2, 1u );
                h = Max<uint32_t>( h / 2, 1u );
                mips.push_back( std::move( next ) );
            }
        }
    }
    uint32_t texels = 0;
    for ( const std::vector<uint8_t> &mip : mips ) {
        texels += static_cast<uint32_t>( mip.size() );
    }

    RenderSectionWrite bmap;
    bmap.Type = kBitmapMagic;
    bmap.VersionMajor = 0;
    bmap.VersionMinor = 0;
    bmap.Payload.resize( 0x30 + texels, 0 );
    Texture2DBHeader header;
    memset( &header, 0, sizeof( header ) );
    header.Hashcode = HashResourceName( pName );
    header.Width = static_cast<uint16_t>( width );
    header.Height = static_cast<uint16_t>( height );
    header.Depth = 1;
    header.NumMipmap = static_cast<uint8_t>( mips.size() );
    header.NumPackedMipmap = 0;
    header.Flags = 0x84; // the retail 2D textures' flags (cube bit 0x40 clear)
    header.Format = static_cast<eViewFormat>( kViewFormatA8R8G8B8 );
    memcpy( bmap.Payload.data(), &header, sizeof( header ) );
    size_t at = 0x30;
    for ( const std::vector<uint8_t> &mip : mips ) {
        memcpy( bmap.Payload.data() + at, mip.data(), mip.size() );
        at += mip.size();
    }

    outBytes = BuildRenderFile( k2DBMagic, 2, 0, 0, { bmap } );
    return true;
}

bool Build2dm( const std::vector<MaterialDesc> &materials, std::vector<uint8_t> &outBytes ) {
    outBytes.clear();
    RenderSectionWrite mata;
    mata.Type = kMaterialArrayMagic;
    mata.VersionMajor = 0;
    mata.VersionMinor = 0;

    // HASH first (one entry per MAT.; the parser visits the MAT. children
    // directly, the HASH mirrors the retail shape), then the materials.
    RenderSectionWrite hash;
    hash.Type = kHashMagic;
    hash.Payload.reserve( materials.size() * 16 );
    for ( size_t i = 0; i < materials.size(); i++ ) {
        hash.Payload.resize( hash.Payload.size() + 16, 0 );
        PutU32( hash.Payload, i * 16, static_cast<uint32_t>( materials[ i ].Hashcode & 0xffffffffu ) );
        PutU32( hash.Payload, i * 16 + 4, static_cast<uint32_t>( materials[ i ].Hashcode >> 32 ) );
        // pMAT patched after the layout; the pad word stays 0.
    }

    std::vector<RenderSectionWrite> mats;
    mats.reserve( materials.size() );
    for ( const MaterialDesc &material : materials ) {
        RenderSectionWrite mat;
        mat.Type = kMaterialMagic;
        mat.Payload.resize( 0x340, 0 );
        PutU32( mat.Payload, 0, static_cast<uint32_t>( material.Hashcode & 0xffffffffu ) );
        PutU32( mat.Payload, 4, static_cast<uint32_t>( material.Hashcode >> 32 ) );
        strncpy( reinterpret_cast<char *>( mat.Payload.data() + 0x10 ), material.Name.c_str(), 31 );
        // The render states (the parsed form is the law: +0x30).
        mat.Payload[ 0x30 ] = material.DepthTest;
        mat.Payload[ 0x31 ] = material.DepthWrite;
        mat.Payload[ 0x33 ] = material.CullMode;
        mat.Payload[ 0x35 ] = material.AlphaTest;
        mat.Payload[ 0x37 ] = material.AlphaRef;
        mat.Payload[ 0x38 ] = material.SrcBlend;
        mat.Payload[ 0x39 ] = material.DstBlend;
        mat.Payload[ 0x3a ] = material.States[ 0xa ]; // blend op
        auto putColor = [ & ]( size_t at, const Vector4f &color ) {
            PutF32( mat.Payload, at, color.x );
            PutF32( mat.Payload, at + 4, color.y );
            PutF32( mat.Payload, at + 8, color.z );
            PutF32( mat.Payload, at + 12, color.w );
        };
        putColor( 0xb0, material.DiffuseColor );
        putColor( 0xc0, material.AmbientColor );
        putColor( 0xd0, material.SpecularColor );
        putColor( 0xe0, material.EmissiveColor );
        const uint32_t numSlots = static_cast<uint32_t>( material.AllTextures.size() );
        mat.Payload[ 0xfc ] = static_cast<uint8_t>( numSlots );
        for ( uint32_t s = 0; s < numSlots && s < 16; s++ ) {
            const MaterialTextureSlot &slot = material.AllTextures[ s ];
            const size_t at = 0x100 + s * 0x20;
            PutU32( mat.Payload, at, static_cast<uint32_t>( slot.SlotType & 0xffffffffu ) );
            PutU32( mat.Payload, at + 4, static_cast<uint32_t>( slot.SlotType >> 32 ) );
            PutU32( mat.Payload, at + 8, static_cast<uint32_t>( slot.TextureHashcode & 0xffffffffu ) );
            PutU32( mat.Payload, at + 12, static_cast<uint32_t>( slot.TextureHashcode >> 32 ) );
            mat.Payload[ at + 0x18 ] = slot.AddressU ? slot.AddressU : 1; // wrap
            mat.Payload[ at + 0x19 ] = slot.AddressV ? slot.AddressV : 1;
        }
        PutU32( mat.Payload, 0x300, static_cast<uint32_t>( material.ShaderSetGUID & 0xffffffffu ) );
        PutU32( mat.Payload, 0x304, static_cast<uint32_t>( material.ShaderSetGUID >> 32 ) );
        mat.Payload[ 0x30a ] = material.ShaderVersion[ 0 ];
        mat.Payload[ 0x30b ] = material.ShaderVersion[ 1 ];
        memcpy( mat.Payload.data() + 0x310, material.Spec, sizeof( material.Spec ) );
        mats.push_back( std::move( mat ) );
    }
    mata.Children.push_back( std::move( hash ) );
    for ( RenderSectionWrite &mat : mats ) {
        mata.Children.push_back( std::move( mat ) );
    }

    // Patch the HASH pointers (the absolute offsets of the MAT. sections).
    const uint32_t mataAbs = 0x10;
    const uint32_t hashAbs = ChildOffset( mata, mataAbs, 0 );
    for ( size_t i = 0; i < materials.size(); i++ ) {
        const uint32_t matAbs = ChildOffset( mata, mataAbs, static_cast<uint32_t>( 1 + i ) );
        PutU32( mata.Children[ 0 ].Payload, i * 16 + 8, matAbs );
    }
    ( void )hashAbs;

    outBytes = BuildRenderFile( k2DMMagic, 4, 0, 0, { mata } );
    return true;
}

bool Build3dg( const std::vector<AuthoredGeometry> &geometries, std::vector<uint8_t> &outBytes, std::string &outError ) {
    outBytes.clear();
    // Split any primitive past the u16 index space (the OBJ props are small;
    // the guard keeps big meshes from corrupting).
    std::vector<AuthoredGeometry> geometriesSplit;
    for ( const AuthoredGeometry &geometry : geometries ) {
        AuthoredGeometry out;
        out.Hashcode = geometry.Hashcode;
        for ( const AuthoredPrimitive &prim : geometry.Primitives ) {
            const uint32_t numVertex = static_cast<uint32_t>( prim.Positions.size() / 3 );
            if ( numVertex <= 0xfffeu ) {
                out.Primitives.push_back( prim );
                continue;
            }
            for ( uint32_t base = 0; base < numVertex; base += 0xfffeu ) {
                AuthoredPrimitive chunk;
                chunk.MaterialIndex = prim.MaterialIndex;
                const uint32_t count = Min<uint32_t>( 0xfffeu, numVertex - base );
                chunk.Positions.assign( prim.Positions.begin() + base * 3,
                                        prim.Positions.begin() + ( base + count ) * 3 );
                chunk.Normals.assign( prim.Normals.begin() + base * 3,
                                      prim.Normals.begin() + ( base + count ) * 3 );
                chunk.UVs.assign( prim.UVs.begin() + base * 2, prim.UVs.begin() + ( base + count ) * 2 );
                for ( uint32_t index : prim.Indices ) {
                    if ( index >= base && index < base + count ) {
                        chunk.Indices.push_back( index - base );
                    }
                }
                out.Primitives.push_back( std::move( chunk ) );
            }
        }
        geometriesSplit.push_back( std::move( out ) );
    }

    // GEOA { HASH, GEOM { PRIM } }, VBA. { DXVB }, IBA. { DXIB }.
    RenderSectionWrite geoa;
    geoa.Type = kGeometryArrayMagic;
    RenderSectionWrite vba;
    vba.Type = kVertexBufferArrayMagic;
    RenderSectionWrite iba;
    iba.Type = kIndexBufferArrayMagic;

    uint32_t numPrims = 0;
    for ( const AuthoredGeometry &geometry : geometriesSplit ) {
        numPrims += static_cast<uint32_t>( geometry.Primitives.size() );
    }
    RenderSectionWrite hash;
    hash.Type = kHashMagic;
    hash.Payload.assign( static_cast<size_t>( geometriesSplit.size() ) * 16, 0 );

    // The DXVB / DXIB sections first (their absolute offsets feed the PRIM
    // payloads), then the GEOA tree.
    // Pass 1: build DXVB/DXIB payloads (no pointers needed inside).
    uint32_t primIndex = 0;
    for ( const AuthoredGeometry &geometry : geometriesSplit ) {
        for ( const AuthoredPrimitive &prim : geometry.Primitives ) {
            const uint32_t numVertex = static_cast<uint32_t>( prim.Positions.size() / 3 );
            const uint32_t numIndex = static_cast<uint32_t>( prim.Indices.size() );
            if ( numVertex == 0 || numIndex == 0 ) {
                primIndex++;
                continue;
            }
            RenderSectionWrite dxvb;
            dxvb.Type = kVertexBufferMagic;
            dxvb.Payload.resize( 0x20 + static_cast<size_t>( numVertex ) * 32, 0 );
            PutU32( dxvb.Payload, 0, numVertex );
            // The semantic table: 12 x { count, format } - position f32x3,
            // normal f32x3, texcoord f32x2, all in stream 0 (flags 0x1800).
            dxvb.Payload[ 4 ] = 1;
            dxvb.Payload[ 5 ] = 0x00; // VAF_POSITION_R32G32B32_SFLOAT
            dxvb.Payload[ 6 ] = 1;
            dxvb.Payload[ 7 ] = 0x06; // VAF_NORMAL_R32G32B32_SFLOAT
            dxvb.Payload[ 12 ] = 1;
            dxvb.Payload[ 13 ] = 0x0c; // VAF_TEXCOORD_R32G32_SFLOAT
            PutU32( dxvb.Payload, 0x1c, 0x1800 );
            for ( uint32_t v = 0; v < numVertex; v++ ) {
                uint8_t *pVertex = dxvb.Payload.data() + 0x20 + static_cast<size_t>( v ) * 32;
                memcpy( pVertex, &prim.Positions[ v * 3 ], 12 );
                memcpy( pVertex + 12, &prim.Normals[ v * 3 ], 12 );
                memcpy( pVertex + 24, &prim.UVs[ v * 2 ], 8 );
            }
            RenderSectionWrite dxib;
            dxib.Type = kIndexBufferMagic;
            dxib.Payload.resize( 0x10 + static_cast<size_t>( numIndex ) * 2, 0 );
            PutU32( dxib.Payload, 0, numIndex );
            PutU32( dxib.Payload, 4, 1 ); // 16-bit indices
            memset( dxib.Payload.data() + 0x10, 0, static_cast<size_t>( numIndex ) * 2 );
            vba.Children.push_back( std::move( dxvb ) );
            iba.Children.push_back( std::move( dxib ) );
            primIndex++;
        }
    }

    // Pass 2: the GEOA tree with the pointer patching. Layout (the file):
    // GEOA first, then VBA., then IBA. - compute the absolute offsets.
    // GEOA: HASH + GEOM per geometry (PRIM children).
    std::vector<RenderSectionWrite> geoms;
    for ( const AuthoredGeometry &geometry : geometriesSplit ) {
        RenderSectionWrite geom;
        geom.Type = kGeometryMagic;
        for ( size_t p = 0; p < geometry.Primitives.size(); p++ ) {
            RenderSectionWrite prim;
            prim.Type = kPrimitiveMagic;
            prim.Payload.assign( 0x40, 0 );
            geom.Children.push_back( std::move( prim ) );
        }
        geoms.push_back( std::move( geom ) );
    }
    geoa.Children.push_back( std::move( hash ) );
    for ( RenderSectionWrite &geom : geoms ) {
        geoa.Children.push_back( std::move( geom ) );
    }

    // The absolute offsets: file = header(0x10) + GEOA + VBA. + IBA.
    const uint32_t geoaAbs = 0x10;
    const uint32_t vbaAbs = geoaAbs + SectionBytes( geoa );
    const uint32_t ibaAbs = vbaAbs + SectionBytes( vba );

    // Fill the PRIM payloads (pointers + counts); the PRIM <-> DXVB/DXIB
    // pairing is the push order of pass 1.
    uint32_t pairIndex = 0;
    for ( size_t g = 0; g < geometriesSplit.size(); g++ ) {
        for ( size_t p = 0; p < geometriesSplit[ g ].Primitives.size(); p++ ) {
            const AuthoredPrimitive &prim = geometriesSplit[ g ].Primitives[ p ];
            const uint32_t numVertex = static_cast<uint32_t>( prim.Positions.size() / 3 );
            const uint32_t numIndex = static_cast<uint32_t>( prim.Indices.size() );
            RenderSectionWrite &primSection = geoa.Children[ 1 + g ].Children[ p ];
            if ( numVertex == 0 || numIndex == 0 ) {
                continue;
            }
            const uint32_t vbAbs = ChildOffset( vba, vbaAbs, pairIndex );
            const uint32_t ibAbs = ChildOffset( iba, ibaAbs, pairIndex );
            PutU32( primSection.Payload, 0x00, ibAbs );
            PutU32( primSection.Payload, 0x04, vbAbs );
            PutU16( primSection.Payload, 0x10, prim.MaterialIndex );
            PutU16( primSection.Payload, 0x12, 3 ); // ePrimitiveType::PT_Triangle (list)
            PutU32( primSection.Payload, 0x1c, numVertex );
            PutU32( primSection.Payload, 0x28, numIndex );
            // The indices into the DXIB payload (u16).
            RenderSectionWrite &dxib = iba.Children[ pairIndex ];
            for ( uint32_t i = 0; i < numIndex; i++ ) {
                PutU16( dxib.Payload, 0x10 + static_cast<size_t>( i ) * 2,
                        static_cast<uint16_t>( prim.Indices[ i ] ) );
            }
            pairIndex++;
        }
        // The GEOA HASH entry for this geometry.
        RenderSectionWrite &hashSection = geoa.Children[ 0 ];
        const uint32_t geomAbs = ChildOffset( geoa, geoaAbs, static_cast<uint32_t>( 1 + g ) );
        PutU32( hashSection.Payload, g * 16, static_cast<uint32_t>( geometriesSplit[ g ].Hashcode & 0xffffffffu ) );
        PutU32( hashSection.Payload, g * 16 + 4, static_cast<uint32_t>( geometriesSplit[ g ].Hashcode >> 32 ) );
        PutU32( hashSection.Payload, g * 16 + 8, geomAbs );
    }

    std::vector<RenderSectionWrite> sections;
    sections.push_back( std::move( geoa ) );
    sections.push_back( std::move( vba ) );
    sections.push_back( std::move( iba ) );
    outBytes = BuildRenderFile( k3DGMagic, 3, 4, 0, sections );

    // The retail container has the file size in the header; the readers
    // re-check the bounds against the actual bytes (roundtripfile proves the
    // container). A parse-back check is the writer's own bar.
    RenderSectionWrite check;
    std::string error;
    if ( !ParseRenderFileTree( outBytes.data(), static_cast<uint32_t>( outBytes.size() ), check, error ) ) {
        outError = "the authored .3dg does not parse back: " + error;
        outBytes.clear();
        return false;
    }
    return true;
}

bool Build3dd( uint64_t rootHashcode, const std::vector<AuthoredInstance> &instances, std::vector<uint8_t> &outBytes,
               std::string &outError ) {
    outBytes.clear();
    // MBNA { MBNK per instance }, OBJA { HASH, OBJ. }, INSA { HASH, INS. },
    // HIEA { HASH, HIE. } (the retail .3dd of the sector banks' buildings).
    RenderSectionWrite mbna;
    mbna.Type = kMaterialBankArrayMagic;
    std::vector<RenderSectionWrite> mbnks;
    for ( const AuthoredInstance &instance : instances ) {
        RenderSectionWrite mbnk;
        mbnk.Type = kMaterialBankMagic;
        mbnk.Payload.assign( instance.Materials.size() * 16, 0 );
        for ( size_t m = 0; m < instance.Materials.size(); m++ ) {
            PutU32( mbnk.Payload, m * 16, static_cast<uint32_t>( instance.Materials[ m ] & 0xffffffffu ) );
            PutU32( mbnk.Payload, m * 16 + 4, static_cast<uint32_t>( instance.Materials[ m ] >> 32 ) );
        }
        mbnks.push_back( std::move( mbnk ) );
    }
    for ( RenderSectionWrite &mbnk : mbnks ) {
        mbna.Children.push_back( std::move( mbnk ) );
    }

    RenderSectionWrite obja;
    obja.Type = kObjectArrayMagic;
    RenderSectionWrite objHash;
    objHash.Type = kHashMagic;
    objHash.Payload.assign( instances.size() * 16, 0 );
    std::vector<RenderSectionWrite> objs;
    for ( size_t i = 0; i < instances.size(); i++ ) {
        RenderSectionWrite obj;
        obj.Type = MakeFourCC( 'O', 'B', 'J', '.' );
        obj.Payload.assign( 0x20, 0 );
        // numPrimitives is not read by the port; the bounding sphere neither.
        // The retail shape: 12 zero bytes, the primitive count, the sphere.
        PutU32( obj.Payload, 0x0c, 1 );
        objs.push_back( std::move( obj ) );
        PutU32( objHash.Payload, i * 16, static_cast<uint32_t>( instances[ i ].GeometryHashcode & 0xffffffffu ) );
        PutU32( objHash.Payload, i * 16 + 4, static_cast<uint32_t>( instances[ i ].GeometryHashcode >> 32 ) );
    }
    obja.Children.push_back( std::move( objHash ) );
    for ( RenderSectionWrite &obj : objs ) {
        obja.Children.push_back( std::move( obj ) );
    }

    RenderSectionWrite insa;
    insa.Type = kInstanceArrayMagic;
    RenderSectionWrite insHash;
    insHash.Type = kHashMagic;
    insHash.Payload.assign( ( instances.size() + 1 ) * 16, 0 ); // root first
    std::vector<RenderSectionWrite> inss;
    for ( size_t i = 0; i <= instances.size(); i++ ) {
        RenderSectionWrite ins;
        ins.Type = kInstanceMagic;
        ins.Payload.assign( 0xa0, 0 );
        inss.push_back( std::move( ins ) );
        // The INSA HASH: the root first, then one entry per instance (the
        // node hashcodes - the editor's overrides match through them).
        const uint64_t nodeHash = i == 0 ? rootHashcode
                                         : GetIdentifier64bit( instances[ i - 1 ].Name.c_str() );
        PutU32( insHash.Payload, i * 16, static_cast<uint32_t>( nodeHash & 0xffffffffu ) );
        PutU32( insHash.Payload, i * 16 + 4, static_cast<uint32_t>( nodeHash >> 32 ) );
    }
    insa.Children.push_back( std::move( insHash ) );
    for ( RenderSectionWrite &ins : inss ) {
        insa.Children.push_back( std::move( ins ) );
    }
    // Children[1] is the ROOT's own INS (a zero matrix, no object); the
    // instances live at Children[2..] so hash entry i lands on Children[1+i].

    RenderSectionWrite hiea;
    hiea.Type = kHierarchyArrayMagic;
    RenderSectionWrite hieHash;
    hieHash.Type = kHashMagic;
    hieHash.Payload.assign( ( instances.size() + 1 ) * 16, 0 );
    std::vector<RenderSectionWrite> hies;
    for ( size_t i = 0; i <= instances.size(); i++ ) {
        RenderSectionWrite hie;
        hie.Type = kHierarchyMagic;
        hie.Payload.assign( 0x10, 0 );
        hies.push_back( std::move( hie ) );
        const uint64_t nodeHash = i == 0 ? rootHashcode
                                         : GetIdentifier64bit( instances[ i - 1 ].Name.c_str() );
        PutU32( hieHash.Payload, i * 16, static_cast<uint32_t>( nodeHash & 0xffffffffu ) );
        PutU32( hieHash.Payload, i * 16 + 4, static_cast<uint32_t>( nodeHash >> 32 ) );
    }
    hiea.Children.push_back( std::move( hieHash ) );
    for ( RenderSectionWrite &hie : hies ) {
        hiea.Children.push_back( std::move( hie ) );
    }

    // The absolute offsets and the pointer patching.
    const uint32_t mbnaAbs = 0x10;
    const uint32_t objaAbs = mbnaAbs + SectionBytes( mbna );
    const uint32_t insaAbs = objaAbs + SectionBytes( obja );
    const uint32_t hieaAbs = insaAbs + SectionBytes( insa );

    for ( size_t i = 0; i < instances.size(); i++ ) {
        const uint32_t objAbs = ChildOffset( obja, objaAbs, static_cast<uint32_t>( 1 + i ) );
        PutU32( obja.Children[ 0 ].Payload, i * 16 + 8, objAbs );
    }
    for ( size_t i = 0; i <= instances.size(); i++ ) {
        const uint32_t insAbs = ChildOffset( insa, insaAbs, static_cast<uint32_t>( 1 + i ) );
        PutU32( insa.Children[ 0 ].Payload, i * 16 + 8, insAbs );
        const uint32_t hieAbs = ChildOffset( hiea, hieaAbs, static_cast<uint32_t>( 1 + i ) );
        PutU32( hiea.Children[ 0 ].Payload, i * 16 + 8, hieAbs );
    }

    // The instances' matrices and references. INS. payload: the matrix (the
    // parser reads matrix[c*4+r] into Local.m[c][r]), +0x44 pMBNK, +0x4e
    // numLOD u16, +0x50 the LOD entry { pObjectHashEntry, distanceSquared }.
    // The root's own INS (Children[1]) stays the zero matrix: the retail
    // roots are dummies (no object, identity or zero local).
    const uint32_t objHashPayloadAbs = ChildOffset( obja, objaAbs, 0 ) + 0x10;
    {
        // The root's INS: IDENTITY (the retail roots are identity dummies - a
        // zero local would collapse every child through the parent chain).
        RenderSectionWrite &root = insa.Children[ 1 ];
        const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        for ( int k = 0; k < 16; k++ ) {
            PutF32( root.Payload, static_cast<size_t>( k ) * 4, identity[ k ] );
        }
    }
    for ( size_t i = 0; i < instances.size(); i++ ) {
        const AuthoredInstance &instance = instances[ i ];
        RenderSectionWrite &ins = insa.Children[ 2 + i ];
        for ( int c = 0; c < 4; c++ ) {
            for ( int r = 0; r < 4; r++ ) {
                PutF32( ins.Payload, static_cast<size_t>( c * 4 + r ) * 4, instance.Local.m[ c ][ r ] );
            }
        }
        const uint32_t mbnkAbs = mbnks.empty() ? 0 : ChildOffset( mbna, mbnaAbs, static_cast<uint32_t>( i ) );
        PutU32( ins.Payload, 0x44, mbnkAbs );
        PutU16( ins.Payload, 0x4e, 1 );
        // The LOD entry points at this instance's OBJA HASH ENTRY (the hash
        // section's payload + the entry's stride - the reader keys its table
        // by entry offset).
        PutU32( ins.Payload, 0x50, objHashPayloadAbs + static_cast<uint32_t>( i ) * 16 );
        PutF32( ins.Payload, 0x54, 0.0f );
    }

    // HIE.: { u32 0, u32 pINS., u32 flags (low 16 = descendants), u32 parent }.
    for ( size_t i = 0; i <= instances.size(); i++ ) {
        RenderSectionWrite &hie = hiea.Children[ 1 + i ];
        const uint32_t insAbs = ChildOffset( insa, insaAbs, static_cast<uint32_t>( 1 + i ) );
        PutU32( hie.Payload, 4, insAbs );
        PutU32( hie.Payload, 8, 0x50410000u | ( i == 0 ? static_cast<uint32_t>( instances.size() ) : 0u ) );
        if ( i > 0 ) {
            const uint32_t rootHieAbs = ChildOffset( hiea, hieaAbs, 1 );
            PutU32( hie.Payload, 12, rootHieAbs );
        }
    }

    std::vector<RenderSectionWrite> sections;
    sections.push_back( std::move( mbna ) );
    sections.push_back( std::move( obja ) );
    sections.push_back( std::move( insa ) );
    sections.push_back( std::move( hiea ) );
    outBytes = BuildRenderFile( k3DDMagic, 2, 0, 0, sections );

    RenderSectionWrite check;
    std::string error;
    if ( !ParseRenderFileTree( outBytes.data(), static_cast<uint32_t>( outBytes.size() ), check, error ) ) {
        outError = "the authored .3dd does not parse back: " + error;
        outBytes.clear();
        return false;
    }
    return true;
}
