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
                           0xff707070u ); // dimmed: the roads and route must read on it
    }
    std::vector<Vertex2D> vertices;
    if ( pCollision != nullptr ) {
        // The roads around the car (the HUD minimap's data, AIRL_ROAD white).
        const float radius = 0.5f * sqrtf( kGpsWidth * kGpsWidth + kGpsHeight * kGpsHeight ) * kMetersPerPixel;
        pCollision->forEachRoadTriangle(
            Vector3f( carX, 0.0f, carZ ), radius, [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
                const Vector2f sa = toScreen( a.x, a.z ), sb = toScreen( b.x, b.z ), sc = toScreen( c.x, c.z );
                const float area = ( sb.x - sa.x ) * ( sc.y - sa.y ) - ( sc.x - sa.x ) * ( sb.y - sa.y );
                if ( std::fabs( area ) < 0.05f ) {
                    return; // a sliver on this zoom: skip (the quad doubles it)
                }
                // The road triangles filled as they are (the minimap draws the
                // raw triangles): widening one edge only traced the outline.
                // A touch of dilation keeps neighbours from showing seams.
                const Vector2f centre( ( sa.x + sb.x + sc.x ) / 3.0f, ( sa.y + sb.y + sc.y ) / 3.0f );
                auto grow = [&]( const Vector2f &p ) {
                    const float dx = p.x - centre.x, dy = p.y - centre.y;
                    const float len = sqrtf( dx * dx + dy * dy );
                    return len > 0.001f ? Vector2f( p.x + dx / len * 0.75f, p.y + dy / len * 0.75f ) : p;
                };
                Vertex2D v{};
                v.U = v.V = 0.0f;
                v.Color = 0xff9a8c80u; // AABBGGRR: a mid blue-grey road (the dash shader lights it, white clips)
                const Vector2f ga = grow( sa ), gb = grow( sb ), gc = grow( sc );
                v.X = ga.x; v.Y = ga.y; vertices.push_back( v );
                v.X = gb.x; v.Y = gb.y; vertices.push_back( v );
                v.X = gc.x; v.Y = gc.y; vertices.push_back( v );
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
            const float half = 2.5f;
            const float nx = -ey / length * half, ny = ex / length * half;
            Vertex2D v{};
            v.U = v.V = 0.0f;
            v.Color = 0xff0066ffu; // AABBGGRR: GamePC.cpr [HudMap] route 255/102/0
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
        const Vector2f tip = arrow( 13.0f, 0.0f ), left = arrow( -8.0f, 9.0f ), right = arrow( -8.0f, -9.0f );
        std::vector<Vertex2D> arrowVerts;
        Vertex2D v{};
        v.U = v.V = 0.0f;
        v.Color = 0xff00d7ffu; // AABBGGRR: amber car arrow
        v.X = tip.x; v.Y = tip.y; arrowVerts.push_back( v );
        v.X = left.x; v.Y = left.y; arrowVerts.push_back( v );
        v.X = right.x; v.Y = right.y; arrowVerts.push_back( v );
        renderer.drawTriangles( nullptr, arrowVerts.data(), static_cast<uint32_t>( arrowVerts.size() ) );
    }
    renderer.end();
    device.endRenderTarget();
}
