#pragma once

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
class GpsScreen {
public:
    ~GpsScreen();
    // Creates the render target once (the swapchain's format, so the 2D
    // pipeline's render pass stays compatible with it). False when the
    // device refuses.
    bool ensureTarget( RenderDevice &device );
    // The texture bound into the satellitemap slot (null until the first
    // render initialised the target).
    GPUTexture *getTexture() const;
    // True when render() should run: rate limited by frame count, the car
    // movement and route changes (deltaTime is not plumbed into draw).
    bool needsRedraw( float carX, float carZ, size_t routeSize );
    // Redraws the map into the target (call outside any render pass, e.g.
    // from GMFreeRide::drawOffscreen like GMAssetViewer::drawOffscreen).
    void render( float carX, float carZ, float headingX, float headingZ, const VehicleWorldCollision *pCollision,
                 const std::vector<Vector3f> *pRoute, GPUTexture *pIslandTexture, float extentX, float extentZ );

private:
    void destroy();
    RenderTarget *pTarget = nullptr;
    uint32_t frames = 0;
    float lastX = 0.0f;
    float lastZ = 0.0f;
    size_t lastRouteSize = 0;
};
