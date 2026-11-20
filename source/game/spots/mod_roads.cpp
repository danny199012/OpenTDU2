#include "shared.h"
#include "mod_roads.h"

#include "filesystem/gs_file.h"
#include "physics/vehicle_collision.h"
#include "render/gs_render.h"
#include "render/image_file.h"
#include "world/eden_world.h"

#include <cmath>
#include <cstring>

namespace {

constexpr float kSampleSpacing = 2.0f;   // metres between centerline samples
constexpr float kDefaultWidth = 7.0f;

// Catmull-Rom (uniform, the classic 0.5 tension), p1/p2 the segment ends.
Vector3f CatmullRom( const Vector3f &p0, const Vector3f &p1, const Vector3f &p2, const Vector3f &p3, float t ) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return ( p1 * 2.0f + ( p2 - p0 ) * t +
             ( p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3 ) * t2 +
             ( p1 * 3.0f - p0 - p2 * 3.0f + p3 ) * t3 ) * 0.5f;
}

std::string ModsPropsPath( const std::string &name ) {
    // The props folder inside the mods overlay, as an OS path (the texture
    // writer needs one; LoadObjDraws reads through the VFS).
    return ( gpFile->getModsRoot() / "props" / name ).string();
}

} // namespace

std::string ModRoadsDefaultTexture() {
    // Generated asphalt: mid grey with darker speckle, our own art (the
    // trademark law of modding.md 4 - nothing of the game's textures).
    const std::string name = "asphalt_road.png";
    if ( gpFile == nullptr || gpFile->fileExists( ( std::string( "props/" ) + name ).c_str() ) ) {
        return name;
    }
    constexpr uint32_t kSize = 256;
    std::vector<uint8_t> rgba( kSize * kSize * 4 );
    uint32_t seed = 0x9e3779b9u;
    auto nextRand = [ &seed ]() {
        seed = seed * 1664525u + 1013904223u;
        return ( seed >> 16 ) & 0xFFu;
    };
    for ( uint32_t y = 0; y < kSize; y++ ) {
        for ( uint32_t x = 0; x < kSize; x++ ) {
            const uint8_t base = 82;
            const uint8_t speckle = static_cast<uint8_t>( nextRand() % 28 );
            const uint8_t v = static_cast<uint8_t>( base - speckle / 2 + ( nextRand() % 10 ) );
            uint8_t *p = &rgba[ ( y * kSize + x ) * 4 ];
            p[ 0 ] = v;
            p[ 1 ] = v;
            p[ 2 ] = static_cast<uint8_t>( v + 4 );
            p[ 3 ] = 255;
        }
    }
    std::error_code ec;
    std::filesystem::create_directories( gpFile->getModsRoot() / "props", ec );
    if ( SaveImagePng( ModsPropsPath( name ).c_str(), rgba.data(), kSize, kSize ) ) {
        OTDU_LOG_INFO( "ModRoads: default asphalt texture written -> '%s'\n", ModsPropsPath( name ).c_str() );
    }
    return name;
}

void ModRoads::BuildCenterline( const std::vector<Vector3f> &points, bool bClosed, float spacing,
                                std::vector<Vector3f> &outCenter ) {
    outCenter.clear();
    if ( points.size() < 2 ) {
        return;
    }
    const int32_t n = static_cast<int32_t>( points.size() );
    // The control-point neighbours (Catmull-Rom wants p(i-1)..p(i+2)).
    auto pointAt = [ & ]( int32_t i ) -> const Vector3f & {
        if ( bClosed ) {
            i = ( ( i % n ) + n ) % n;
        } else {
            i = Clamp( i, 0, n - 1 );
        }
        return points[ i ];
    };
    const int32_t segmentCount = bClosed ? n : n - 1;
    for ( int32_t i = 0; i < segmentCount; i++ ) {
        const Vector3f &p0 = pointAt( i - 1 );
        const Vector3f &p1 = pointAt( i );
        const Vector3f &p2 = pointAt( i + 1 );
        const Vector3f &p3 = pointAt( i + 2 );
        const float length = ( p2 - p1 ).length();
        const int32_t steps = Max( 1, static_cast<int32_t>( std::ceil( length / spacing ) ) );
        for ( int32_t s = 0; s < steps; s++ ) {
            const float t = static_cast<float>( s ) / static_cast<float>( steps );
            outCenter.push_back( CatmullRom( p0, p1, p2, p3, t ) );
        }
    }
    if ( !bClosed ) {
        outCenter.push_back( points.back() );
    } else {
        // The wrap segment ends at the first point.
        outCenter.push_back( points.front() );
    }
}

void ModRoads::BuildRoadObjText( const std::vector<Vector3f> &center, float width, const std::string &textureName,
                                 std::string &outObj, std::string &outMtl ) {
    outObj.clear();
    outMtl.clear();
    const size_t count = center.size();
    if ( count < 2 || width <= 0.0f ) {
        return;
    }
    const float half = width * 0.5f;
    const Vector3f up( 0.0f, 1.0f, 0.0f );

    // Left / right rails and the cumulative v (distance / width, square
    // tiling). Render space; the OBJ text mirrors X (the authoring space).
    std::vector<Vector3f> left( count ), right( count );
    std::vector<float> vCoord( count );
    float distance = 0.0f;
    for ( size_t i = 0; i < count; i++ ) {
        const Vector3f &prev = center[ i > 0 ? i - 1 : ( count - 1 ) ];
        const Vector3f &next = center[ i + 1 < count ? i + 1 : 0 ];
        Vector3f tangent = next - prev;
        if ( tangent.length() < 1e-6f ) {
            tangent = Vector3f( 0.0f, 0.0f, 1.0f );
        }
        tangent = tangent.normalized();
        Vector3f side = up.cross( tangent );
        side = side.length() > 1e-6f ? side.normalized() : Vector3f( 1.0f, 0.0f, 0.0f );
        left[ i ] = center[ i ] - side * half;
        right[ i ] = center[ i ] + side * half;
        if ( i > 0 ) {
            distance += ( center[ i ] - center[ i - 1 ] ).length();
        }
        vCoord[ i ] = distance / width;
    }

    char buffer[ 128 ];
    snprintf( buffer, sizeof( buffer ), "mod_road_%p", (void *)&center );
    std::string mtlName = buffer;
    outObj += "# OpenTDU2 scene road (generated; the loader mirrors X)\n";
    outObj += "mtllib " + mtlName + ".mtl\n";
    outObj += "usemtl road\n";
    for ( size_t i = 0; i < count; i++ ) {
        snprintf( buffer, sizeof( buffer ), "v %.3f %.3f %.3f\n", -left[ i ].x, left[ i ].y, left[ i ].z );
        outObj += buffer;
    }
    for ( size_t i = 0; i < count; i++ ) {
        snprintf( buffer, sizeof( buffer ), "v %.3f %.3f %.3f\n", -right[ i ].x, right[ i ].y, right[ i ].z );
        outObj += buffer;
    }
    for ( size_t i = 0; i < count; i++ ) {
        snprintf( buffer, sizeof( buffer ), "vn 0 1 0\n" );
        outObj += buffer;
    }
    for ( size_t i = 0; i < count; i++ ) {
        snprintf( buffer, sizeof( buffer ), "vt 0 %.3f\n", vCoord[ i ] );
        outObj += buffer;
    }
    for ( size_t i = 0; i < count; i++ ) {
        snprintf( buffer, sizeof( buffer ), "vt 1 %.3f\n", vCoord[ i ] );
        outObj += buffer;
    }
    // Quads (l_i, r_i, l_i+1, r_i+1) -> two triangles, wound for the AUTHORING
    // space (the loader reverses them into render space). Face vertex tuples
    // are v/vt/vn, 1-based: left v i+1, right v count+i+1, normals i+1 (all
    // up), uvs like the v ranges.
    for ( size_t i = 0; i + 1 < count; i++ ) {
        const uint32_t li = static_cast<uint32_t>( i + 1 );
        const uint32_t li1 = static_cast<uint32_t>( i + 2 );
        const uint32_t ri = static_cast<uint32_t>( count + i + 1 );
        const uint32_t ri1 = static_cast<uint32_t>( count + i + 2 );
        const uint32_t ni = static_cast<uint32_t>( i + 1 );
        const uint32_t ni1 = static_cast<uint32_t>( i + 2 );
        snprintf( buffer, sizeof( buffer ), "f %u/%u/%u %u/%u/%u %u/%u/%u\n", li, li, ni, ri, ri, ni, ri1, ri1, ni1 );
        outObj += buffer;
        snprintf( buffer, sizeof( buffer ), "f %u/%u/%u %u/%u/%u %u/%u/%u\n", li, li, ni, ri1, ri1, ni1, li1, li1, ni1 );
        outObj += buffer;
    }
    outMtl += "newmtl road\n";
    outMtl += "Kd 0.32 0.32 0.34\n";
    if ( !textureName.empty() ) {
        outMtl += "map_Kd " + textureName + "\n";
    }
}

void ModRoads::BuildRoadSolid( const std::vector<Vector3f> &center, float width, std::vector<Vector3f> &outVertices,
                               std::vector<uint32_t> &outIndices ) {
    const size_t count = center.size();
    if ( count < 2 || width <= 0.0f ) {
        return;
    }
    const float half = width * 0.5f;
    const Vector3f up( 0.0f, 1.0f, 0.0f );
    const size_t base = outVertices.size();
    for ( size_t i = 0; i < count; i++ ) {
        const Vector3f &prev = center[ i > 0 ? i - 1 : ( count - 1 ) ];
        const Vector3f &next = center[ i + 1 < count ? i + 1 : 0 ];
        Vector3f tangent = next - prev;
        if ( tangent.length() < 1e-6f ) {
            tangent = Vector3f( 0.0f, 0.0f, 1.0f );
        }
        tangent = tangent.normalized();
        Vector3f side = up.cross( tangent );
        side = side.length() > 1e-6f ? side.normalized() : Vector3f( 1.0f, 0.0f, 0.0f );
        outVertices.push_back( center[ i ] - side * half );
        outVertices.push_back( center[ i ] + side * half );
    }
    for ( size_t i = 0; i + 1 < count; i++ ) {
        const uint32_t a = static_cast<uint32_t>( base + i * 2 );
        const uint32_t b = a + 1;
        const uint32_t c = a + 2;
        const uint32_t d = a + 3;
        // Up-facing: orient each triangle by the y of its cross product.
        auto pushUpTriangle = [ & ]( uint32_t i0, uint32_t i1, uint32_t i2 ) {
            const Vector3f e1 = outVertices[ i1 ] - outVertices[ i0 ];
            const Vector3f e2 = outVertices[ i2 ] - outVertices[ i0 ];
            // (e1 x e2).y in this layout: e1.z*e2.x - e1.x*e2.z.
            const float ny = e1.z * e2.x - e1.x * e2.z;
            if ( ny >= 0.0f ) {
                outIndices.push_back( i0 );
                outIndices.push_back( i1 );
                outIndices.push_back( i2 );
            } else {
                outIndices.push_back( i0 );
                outIndices.push_back( i2 );
                outIndices.push_back( i1 );
            }
        };
        pushUpTriangle( a, b, d );
        pushUpTriangle( a, d, c );
    }
}

bool ModRoads::load( EdenRenderer &renderer, const std::string &island, EdenWorld *pWorld ) {
    SceneDocument document;
    std::string error;
    const std::string path = std::string( "roads/" ) + island + ".json";
    std::vector<uint8_t> bytes;
    if ( !gpFile->loadFile( path.c_str(), bytes ) ) {
        return false; // no roads file: the normal case
    }
    std::string text( bytes.begin(), bytes.end() );
    JsonValue root;
    if ( !JsonParse( text, root, error ) ) {
        OTDU_LOG_WARN( "ModRoads: '%s': %s\n", path.c_str(), error.c_str() );
        return false;
    }
    // Either a {"roads": [...]} wrapper or a bare array.
    const JsonValue *pRoads = root.find( "roads" );
    if ( pRoads == nullptr && root.isArray() ) {
        pRoads = &root;
    }
    if ( pRoads == nullptr ) {
        return false;
    }
    document.Roads.clear();
    for ( const JsonValue &entry : pRoads->array ) {
        SceneRoad road;
        road.Id = entry.getString( "id", "" );
        if ( const JsonValue *pPoints = entry.find( "points" ) ) {
            for ( const JsonValue &point : pPoints->array ) {
                if ( point.isArray() && point.array.size() >= 3 ) {
                    road.Points.push_back( Vector3f( static_cast<float>( point.array[ 0 ].asNumber() ),
                                                     static_cast<float>( point.array[ 1 ].asNumber() ),
                                                     static_cast<float>( point.array[ 2 ].asNumber() ) ) );
                }
            }
        }
        road.Width = static_cast<float>( entry.getNumber( "width", kDefaultWidth ) );
        road.Lanes = static_cast<int32_t>( entry.getNumber( "lanes", 2.0 ) );
        road.bClosed = entry.getBool( "closed", false );
        road.bCollide = entry.getBool( "collide", true );
        road.Texture = entry.getString( "texture", "" );
        if ( road.Id.empty() || road.Points.size() < 2 ) {
            continue;
        }
        document.Roads.push_back( std::move( road ) );
    }
    loadDocument( renderer, document, pWorld );
    return bLoaded;
}

void ModRoads::loadDocument( EdenRenderer &renderer, const SceneDocument &document, EdenWorld *pWorld ) {
    unload();
    for ( const SceneRoad &sceneRoad : document.Roads ) {
        Road road;
        road.Id = sceneRoad.Id;
        road.Points = sceneRoad.Points;
        road.Width = sceneRoad.Width > 0.5f ? sceneRoad.Width : kDefaultWidth;
        road.Lanes = sceneRoad.Lanes;
        road.bClosed = sceneRoad.bClosed;
        road.bCollide = sceneRoad.bCollide;
        road.Texture = sceneRoad.Texture.empty() ? ModRoadsDefaultTexture() : sceneRoad.Texture;

        BuildCenterline( road.Points, road.bClosed, kSampleSpacing, road.Centerline );
        if ( road.Centerline.size() < 2 ) {
            continue;
        }
        // The heightfield rolls between the control points, the spline does
        // not: lift the samples that dip under the terrain (+5 cm against the
        // z-fight). A bridge's explicit height stays (the terrain is below).
        if ( pWorld != nullptr ) {
            for ( Vector3f &p : road.Centerline ) {
                float ground = 0.0f;
                if ( pWorld->getSurfaceHeight( p.x, p.z, p.y + 30.0f, ground ) && ground > p.y ) {
                    p.y = ground + 0.05f;
                }
            }
        }
        std::string objText, mtlText;
        BuildRoadObjText( road.Centerline, road.Width, road.Texture, objText, mtlText );
        Vector3f boundsMin, boundsMax;
        uint32_t numTriangles = 0;
        if ( BuildObjDraws( renderer, "props/", ( "road_" + road.Id ).c_str(), "TD_Gouraud", objText, mtlText,
                            road.Draws, &boundsMin, &boundsMax, &numTriangles, "ModRoads" ) ) {
            road.BoundsMin = boundsMin;
            road.BoundsMax = boundsMax;
        }
        BuildRoadSolid( road.Centerline, road.Width, road.SolidVertices, road.SolidIndices );
        roads.push_back( std::move( road ) );
    }
    bLoaded = !roads.empty();
    if ( bLoaded ) {
        uint32_t triangles = 0;
        for ( const Road &road : roads ) {
            triangles += static_cast<uint32_t>( road.SolidIndices.size() / 3 );
        }
        OTDU_LOG_INFO( "ModRoads: %zu road(s), %u surface triangles\n", roads.size(), triangles );
    }
}

void ModRoads::unload() {
    for ( Road &road : roads ) {
        if ( !road.Draws.empty() ) {
            UnloadObjDraws( road.Draws );
        }
    }
    roads.clear();
    bLoaded = false;
}

void ModRoads::draw( EdenRenderer &renderer ) const {
    static const Matrix4x4f identity = Matrix4x4f::Identity();
    for ( const Road &road : roads ) {
        if ( !renderer.isBoxVisible( identity, road.BoundsMin, road.BoundsMax ) ) {
            continue;
        }
        for ( const ObjMeshDraw &draw : road.Draws ) {
            // The ribbon vertices are already world space: the identity frame.
            renderer.draw( draw.Mesh, draw.Material, identity );
        }
    }
}

uint32_t ModRoads::registerCollision( VehicleWorldCollision &collision ) const {
    uint32_t added = 0;
    for ( const Road &road : roads ) {
        if ( !road.bCollide || road.SolidVertices.empty() ) {
            continue;
        }
        collision.addStaticTriangles( road.SolidVertices, road.SolidIndices );
        added += static_cast<uint32_t>( road.SolidIndices.size() / 3 );
    }
    return added;
}

void ModRoads::registerSurfaces( EdenWorld &world ) const {
    std::vector<std::vector<Vector3f>> shapes;
    for ( const Road &road : roads ) {
        if ( !road.SolidVertices.empty() ) {
            world.addRoadSurfaceTriangles( road.SolidVertices, road.SolidIndices );
        }
        if ( road.Centerline.size() >= 2 ) {
            shapes.push_back( road.Centerline );
        }
    }
    if ( !shapes.empty() ) {
        world.appendRoadShapes( shapes );
    }
}
