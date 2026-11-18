#include "shared.h"
#include "game/cinematic/gps_screen.h"

#include "physics/vehicle_collision.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "render/vulkan/render_device.h"

#include <cmath>

// The GPS quad is a small dash screen; 2 px per metre keeps a junction legible
// (the HUD minimap runs 3.4, the big map is the island overview).
static constexpr uint32_t kGpsLong = 512;
static constexpr uint32_t kGpsShort = 256;
static constexpr float kLogicalWidth = 190.0f;  // the HUD map rect's width: the dash reads 1:1 with the minimap
static constexpr float kLogicalHeight = 95.0f;  // the dash screen is 2:1
// Redraw cadence: at most every 15 frames when something moved, otherwise a
// slow keep-alive (the clock ticks in frames, draw has no deltaTime).
static constexpr uint32_t kMinIntervalFrames = 5;
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
    pTarget = device.createRenderTarget( layout.Transpose ? kGpsShort : kGpsLong, layout.Transpose ? kGpsLong : kGpsShort,
                                         VK_FORMAT_UNDEFINED, false, "CarGPS" );
    return pTarget != nullptr;
}

void GpsScreen::setLayout( const GpsUvLayout &newLayout ) {
    if ( !( newLayout == layout ) ) {
        destroy();
        layout = newLayout;
        frames = 0;
    }
}

GPUTexture *GpsScreen::getTexture() const {
    return pTarget != nullptr && pTarget->bInitialized ? &pTarget->Color : nullptr;
}

bool GpsScreen::needsRedraw( float, float, size_t ) {
    // The map turns with the car: redraw every few frames (~10-15 Hz).
    return ++frames % kMinIntervalFrames == 0 || frames == 1;
}

void GpsScreen::render( const std::function<void( Renderer2D &, const GpsUvLayout & )> &draw ) {
    if ( pTarget == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    Renderer2D &renderer = gpRender->get2DRenderer();
    device.beginRenderTarget( pTarget, true, ColorRGBA( 12.0f / 255.0f, 14.0f / 255.0f, 18.0f / 255.0f, 1.0f ) );
    renderer.begin( layout.Transpose ? kLogicalHeight : kLogicalWidth, layout.Transpose ? kLogicalWidth : kLogicalHeight, false );
    draw( renderer, layout );
    renderer.end();
    device.endRenderTarget();
}
