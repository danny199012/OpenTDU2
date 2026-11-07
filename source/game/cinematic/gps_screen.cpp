#include "shared.h"
#include "game/cinematic/gps_screen.h"

#include "physics/vehicle_collision.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "render/vulkan/render_device.h"

#include <cmath>

// The GPS quad is a small dash screen; 2 px per metre keeps a junction legible
// (the HUD minimap runs 3.4, the big map is the island overview).
static constexpr float kGpsWidth = 512.0f;
static constexpr float kGpsHeight = 256.0f;
static constexpr float kMetersPerPixel = 2.0f;
// Redraw cadence: at most every 15 frames when something moved, otherwise a
// slow keep-alive (the clock ticks in frames, draw has no deltaTime).
static constexpr uint32_t kMinIntervalFrames = 15;
static constexpr uint32_t kMaxIntervalFrames = 300;
static constexpr float kRedrawMoveMeters = 8.0f;

GpsScreen::~GpsScreen() {
    destroy();
}

void GpsScreen::destroy() {
    if ( pTarget != nullptr ) {
        gpRender->getDevice().destroyRenderTarget( pTarget );
        pTarget = nullptr;
    }
}

bool GpsScreen::ensureTarget( RenderDevice &device ) {
    if ( pTarget != nullptr ) {
        return true;
    }
    // The swapchain's format (VK_FORMAT_UNDEFINED resolves to it): the 2D
    // pipeline is created against mainRenderPass, and a render target on
    // another format would make the passes incompatible (the asset viewer's
    // scene target does the same).
    pTarget = device.createRenderTarget( static_cast<uint32_t>( kGpsWidth ), static_cast<uint32_t>( kGpsHeight ),
                                         VK_FORMAT_UNDEFINED, false, "CarGPS" );
    return pTarget != nullptr;
}

GPUTexture *GpsScreen::getTexture() const {
    return pTarget != nullptr && pTarget->bInitialized ? &pTarget->Color : nullptr;
}

bool GpsScreen::needsRedraw( float carX, float carZ, size_t routeSize ) {
    frames++;
    const float dx = carX - lastX, dz = carZ - lastZ;
    const float moved = sqrtf( dx * dx + dz * dz );
    if ( frames < kMinIntervalFrames ) {
        return false;
    }
    if ( moved < kRedrawMoveMeters && routeSize == lastRouteSize && frames < kMaxIntervalFrames ) {
        return false;
    }
    frames = 0;
    lastX = carX;
    lastZ = carZ;
    lastRouteSize = routeSize;
    return true;
}

void GpsScreen::render( float carX, float carZ, float headingX, float headingZ, const VehicleWorldCollision *pCollision,
                        const std::vector<Vector3f> *pRoute, GPUTexture *pIslandTexture, float extentX,
                        float extentZ ) {
    if ( pTarget == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    Renderer2D &renderer = gpRender->get2DRenderer();

    device.beginRenderTarget( pTarget, true, ColorRGBA( 0.04f, 0.08f, 0.14f, 1.0f ) );
    // The map screen's convention (MapScreen::projectToStage): +x right,
    // +z down, the car at the centre. Same orientation as the flat island
    // map, so the dash screen reads like the map the player already knows.
    const float cx = kGpsWidth * 0.5f, cy = kGpsHeight * 0.5f;
    auto toScreen = [&]( float x, float z ) {
        return Vector2f( cx + ( x - carX ) * kMetersPerPixel, cy + ( z - carZ ) * kMetersPerPixel );
    };
    renderer.begin( kGpsWidth, kGpsHeight, false );
    if ( pIslandTexture != nullptr && extentX > 0.0f && extentZ > 0.0f ) {
        // The island colour map under everything (the satellite look of the
        // retail satellitemap), the full extent in one quad.
        const Vector2f topLeft = toScreen( 0.0f, 0.0f );
        renderer.drawQuad( pIslandTexture,
                           Rect2D( topLeft.x, topLeft.y, extentX * kMetersPerPixel, extentZ * kMetersPerPixel ),
                           0xffffffffu );
    }
    std::vector<Vertex2D> vertices;
    if ( pCollision != nullptr ) {
        // The roads around the car (the HUD minimap's data, AIRL_ROAD white).
        const float radius = 0.5f * sqrtf( kGpsWidth * kGpsWidth + kGpsHeight * kGpsHeight ) * kMetersPerPixel;
        auto pushQuad = [&]( const Vector2f &a, const Vector2f &b, const Vector2f &c, const Vector2f &d,
                             uint32_t color ) {
            Vertex2D v{};
            v.U = v.V = 0.0f;
            v.Color = color;
            v.X = a.x; v.Y = a.y; vertices.push_back( v );
            v.X = b.x; v.Y = b.y; vertices.push_back( v );
            v.X = c.x; v.Y = c.y; vertices.push_back( v );
            v.X = c.x; v.Y = c.y; vertices.push_back( v );
            v.X = d.x; v.Y = d.y; vertices.push_back( v );
            v.X = a.x; v.Y = a.y; vertices.push_back( v );
        };
        pCollision->forEachRoadTriangle(
            Vector3f( carX, 0.0f, carZ ), radius, [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
                const Vector2f sa = toScreen( a.x, a.z ), sb = toScreen( b.x, b.z ), sc = toScreen( c.x, c.z );
                const float area = ( sb.x - sa.x ) * ( sc.y - sa.y ) - ( sc.x - sa.x ) * ( sb.y - sa.y );
                if ( std::fabs( area ) < 0.05f ) {
                    return; // a sliver on this zoom: skip (the quad doubles it)
                }
                // The triangle is a road strip piece; its long axis is the
                // road direction — widen across it (the minimap draws the
                // raw triangles, but at 2 px/m they read thin).
                const float lengthAB = sqrtf( ( sb.x - sa.x ) * ( sb.x - sa.x ) + ( sb.y - sa.y ) * ( sb.y - sa.y ) );
                const float lengthAC = sqrtf( ( sc.x - sa.x ) * ( sc.x - sa.x ) + ( sc.y - sa.y ) * ( sc.y - sa.y ) );
                const float lengthBC = sqrtf( ( sc.x - sb.x ) * ( sc.x - sb.x ) + ( sc.y - sb.y ) * ( sc.y - sb.y ) );
                const Vector2f *pLongA = &sa;
                const Vector2f *pLongB = &sb;
                if ( lengthAC >= lengthAB && lengthAC >= lengthBC ) {
                    pLongA = &sa;
                    pLongB = &sc;
                } else if ( lengthBC >= lengthAB && lengthBC >= lengthAC ) {
                    pLongA = &sb;
                    pLongB = &sc;
                }
                const float ex = pLongB->x - pLongA->x, ey = pLongB->y - pLongA->y;
                const float length = sqrtf( ex * ex + ey * ey );
                if ( length < 0.01f ) {
                    return;
                }
                const float half = 1.6f; // ~3 px road
                const float nx = -ey / length * half, ny = ex / length * half;
                const Vector2f a1( pLongA->x + nx, pLongA->y + ny ), a2( pLongA->x - nx, pLongA->y - ny );
                const Vector2f b1( pLongB->x + nx, pLongB->y + ny ), b2( pLongB->x - nx, pLongB->y - ny );
                pushQuad( a1, b1, b2, a2, 0xffdcdcccu ); // AABBGGRR: R dc G dc B dc
            } );
    }
    if ( pRoute != nullptr && pRoute->size() > 1 ) {
        // The GPS route in red (the map screen / minimap colour).
        for ( size_t i = 0; i + 1 < pRoute->size(); i++ ) {
            const Vector2f a = toScreen( ( *pRoute )[i].x, ( *pRoute )[i].z );
            const Vector2f b = toScreen( ( *pRoute )[i + 1].x, ( *pRoute )[i + 1].z );
            const float ex = b.x - a.x, ey = b.y - a.y;
            const float length = sqrtf( ex * ex + ey * ey );
            if ( length < 0.01f ) {
                continue;
            }
            const float half = 2.0f;
            const float nx = -ey / length * half, ny = ex / length * half;
            Vertex2D v{};
            v.U = v.V = 0.0f;
            v.Color = 0xff1a1affu; // AABBGGRR: R ff G 1a B 1a
            v.X = a.x + nx; v.Y = a.y + ny; vertices.push_back( v );
            v.X = b.x + nx; v.Y = b.y + ny; vertices.push_back( v );
            v.X = b.x - nx; v.Y = b.y - ny; vertices.push_back( v );
            v.X = b.x - nx; v.Y = b.y - ny; vertices.push_back( v );
            v.X = a.x - nx; v.Y = a.y - ny; vertices.push_back( v );
            v.X = a.x + nx; v.Y = a.y + ny; vertices.push_back( v );
        }
    }
    if ( !vertices.empty() ) {
        renderer.drawTriangles( nullptr, vertices.data(), static_cast<uint32_t>( vertices.size() ) );
    }
    // The car: a white arrow rotated to its heading (north-up map, the car
    // turns; the minimap's arrow shape at the centre).
    {
        const float angle = atan2f( headingZ, headingX );
        const float ca = cosf( angle ), sa = sinf( angle );
        auto arrow = [&]( float lx, float ly ) {
            // local arrow space: +x forward
            return Vector2f( cx + lx * ca - ly * sa, cy + lx * sa + ly * ca );
        };
        const Vector2f tip = arrow( 9.0f, 0.0f ), left = arrow( -6.0f, 6.0f ), right = arrow( -6.0f, -6.0f );
        std::vector<Vertex2D> arrowVerts;
        Vertex2D v{};
        v.U = v.V = 0.0f;
        v.Color = 0xffffffffu;
        v.X = tip.x; v.Y = tip.y; arrowVerts.push_back( v );
        v.X = left.x; v.Y = left.y; arrowVerts.push_back( v );
        v.X = right.x; v.Y = right.y; arrowVerts.push_back( v );
        renderer.drawTriangles( nullptr, arrowVerts.data(), static_cast<uint32_t>( arrowVerts.size() ) );
    }
    renderer.end();
    device.endRenderTarget();
}
