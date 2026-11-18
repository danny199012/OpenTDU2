#pragma once

#include <functional>
#include <vector>

struct Vector3f;
struct GPUTexture;
struct RenderTarget;
class RenderDevice;
class Renderer2D;
class VehicleWorldCollision;

// The dashboard sat-nav screen (docs/re/opentdu1_reuse.md 1, the #1 reuse
// item). The interior GPS quad (TD_GPS material, satellitemap slot) samples
// a small render target that is redrawn car-centred from the map data the
// HUD minimap already uses (the road triangles of the vehicle collision, the
// GPS route, the island colour map) — retail binds its shared HUD minimap
// atlas instead (TDU1 HudMinimap::bindGpsTextureToPlayerCar -> VehicleBase::
// setGPSTexture), which this port does not have. North-up like the map
// screen; the redraw is rate limited (the car moved / the route changed).
// How a car's dash quad maps the texture onto the screen, read from the quad's
// own UVs (the 370Z lays them out transposed: texture +x runs down the screen).
// u = (Transpose ? y : x), v = (Transpose ? x : y), then flipped, where
// (x, y) is the upright content position in 0..1 (right, down).
struct GpsUvLayout {
    bool Transpose = false;
    bool FlipU = false;
    bool FlipV = false;
    bool operator==( const GpsUvLayout &o ) const {
        return Transpose == o.Transpose && FlipU == o.FlipU && FlipV == o.FlipV;
    }
};

class GpsScreen {
public:
    ~GpsScreen();
    // Creates the render target once (the swapchain's format, so the 2D
    // pipeline's render pass stays compatible with it). False when the
    // device refuses.
    bool ensureTarget( RenderDevice &device );
    // The car's UV layout (portrait target when transposed); a change drops the target.
    void setLayout( const GpsUvLayout &layout );
    inline const GpsUvLayout &getLayout() const { return layout; }
    // The texture bound into the satellitemap slot (null until the first
    // render initialised the target).
    GPUTexture *getTexture() const;
    // True when render() should run: rate limited by frame count, the car
    // movement and route changes (deltaTime is not plumbed into draw).
    bool needsRedraw( float carX, float carZ, size_t routeSize );
    // Redraws the screen into the target (call outside any render pass, e.g.
    // from GMFreeRide::drawOffscreen like GMAssetViewer::drawOffscreen). The
    // callback draws the content on a kLogicalWidth x kLogicalHeight canvas
    // (the HUD minimap's own scale, FreeRideHud::drawDashSatnav).
    void render( const std::function<void( Renderer2D &, const GpsUvLayout & )> &draw );

private:
    void destroy();
    RenderTarget *pTarget = nullptr;
    GpsUvLayout layout;
    uint32_t frames = 0;
    float lastX = 0.0f;
    float lastZ = 0.0f;
    size_t lastRouteSize = 0;
};
