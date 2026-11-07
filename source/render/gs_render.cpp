#include "shared.h"
#include "gs_render.h"

#include "game/game_options.h"
#include "render/eden/eden_surface_fx.h"
#include "system/cmd_line.h"

#include <SDL3/SDL.h>

GSRender *gpRender = nullptr;

bool IsRayTracingWorldEnabled() {
    return CmdLine::HasArg( "-rtworld" ) || GetGameOptions().RayTracingWorld != 0;
}

GSRender::GSRender() : clearColor( 0.0f, 0.0f, 0.0f, 1.0f ), bFrameActive( false ) {
    gpRender = this;
}

GSRender::~GSRender() { gpRender = nullptr; }

bool GSRender::initialize( TestDriveGameInstance *pInstance ) {
    return GameSystem::initialize( pInstance );
}

bool GSRender::createDevice( SDL_Window *pWindow, bool bVSync, bool bValidation ) {
    // Options > Advanced, ray tracing. The device is created once, so the
    // choice is made here; -nort still overrides it for a headless A/B run.
    device.setRayTracingEnabled( GetGameOptions().RayTracing != 0 && !CmdLine::HasArg( "-nort" ) );
    if ( !device.create( pWindow, bVSync, bValidation ) ) {
        return false;
    }
    rayTracing.initialize( device );
    textureCache.initialize( &device );
    if ( !renderer2D.initialize( &device, textureCache.getWhiteTexture() ) ) {
        OTDU_LOG_ERROR( "Failed to initialize the 2D renderer\n" );
        return false;
    }
    if ( !edenRenderer.initialize( &device, &textureCache ) ) {
        OTDU_LOG_WARN( "Original shaders renderer unavailable\n" );
    }
    // Wheel spray / dust and the tyre-track decals (docs/re/surface_effects_spec.md).
    if ( !surfaceFx.initialize( &device ) ) {
        OTDU_LOG_WARN( "Surface effects unavailable\n" );
    }
    // -rttest: proves the ray tracing chain (acceleration structures, the ray
    // query shader, the descriptors) with a triangle whose answer is known in
    // advance. See docs/re/ray_tracing.md.
    if ( rayTracing.isReady() && CmdLine::HasArg( "-rttest" ) ) {
        rayTracing.runSelfTest();
    }
    // Options > Advanced, ray tracing: the world's acceleration structures (a
    // BLAS per model mesh at load, a TLAS per frame from what the scene
    // recorded) and the camera probes.
    bRtWorld = rayTracing.isReady() && IsRayTracingWorldEnabled();
    edenRenderer.setRtInstanceCapture( bRtWorld );
    if ( bRtWorld ) {
        OTDU_LOG_INFO( "Ray tracing: world structures enabled (Options > Advanced / -rtworld)\n" );
    }
    return true;
}

void GSRender::terminate() {
    device.waitIdle();
    if ( pRtTlas != nullptr ) {
        rayTracing.destroyTlas( pRtTlas );
        pRtTlas = nullptr;
    }
    rayTracing.destroy();
    surfaceFx.destroy();
    edenRenderer.destroy();
    renderer2D.destroy();
    textureCache.destroy();
    device.destroy();
}

bool GSRender::beginFrame() {
    if ( bRtWorld ) {
        // A frame draws many scenes (the environment cube, the shadow casters,
        // the reflections, the main pass): the instance list is per frame, not
        // per scene.
        edenRenderer.clearRtInstances();
    }
    bFrameActive = device.beginFrame( clearColor );
    return bFrameActive;
}

void GSRender::endFrame() {
    if ( bFrameActive ) {
        updateRayTracing();
        device.endFrame();
        bFrameActive = false;
    }
}

void GSRender::updateRayTracing() {
    if ( !bRtWorld || !rayTracing.isReady() ) {
        return;
    }
    const std::vector<EdenRenderer::RtInstance> &instances = edenRenderer.getRtInstances();
    const bool bReport = ( rtFrame++ % 60 ) == 0;
    if ( instances.empty() ) {
        if ( bReport ) {
            OTDU_LOG_INFO( "Ray tracing world: no instances recorded this frame\n" );
        }
        return;
    }
    if ( pRtTlas == nullptr || pRtTlas->Capacity < instances.size() ) {
        // Retire instead of destroying: the old TLAS is still referenced by
        // the frame's recorded commands (the -rtao dispatch binds it); it is
        // destroyed two retirements later, when every fence has been waited.
        if ( pRtTlas != nullptr ) {
            if ( pRtTlasRetired[rtRetireIndex] != nullptr ) {
                rayTracing.destroyTlas( pRtTlasRetired[rtRetireIndex] );
            }
            pRtTlasRetired[rtRetireIndex] = pRtTlas;
            rtRetireIndex = ( rtRetireIndex + 1 ) % 2;
            pRtTlas = nullptr;
        }
        pRtTlas = rayTracing.createTlas( static_cast<uint32_t>( instances.size() ) );
        if ( pRtTlas == nullptr ) {
            OTDU_LOG_ERROR( "Ray tracing: the frame TLAS could not be created\n" );
            return;
        }
    }

    std::vector<RayTracing::Instance> rtInstances( instances.size() );
    for ( size_t i = 0; i < instances.size(); i++ ) {
        rtInstances[i].Blas = instances[i].BlasAddress;
        rtInstances[i].Transform = instances[i].World;
    }

    const uint64_t start = SDL_GetPerformanceCounter();
    if ( !rayTracing.buildTlas( pRtTlas, rtInstances.data(), static_cast<uint32_t>( rtInstances.size() ) ) ) {
        return;
    }
    const double buildMs = static_cast<double>( SDL_GetPerformanceCounter() - start ) /
                           static_cast<double>( SDL_GetPerformanceFrequency() ) * 1000.0;

    // Probe rays from the camera of the scene the instances came from: straight
    // down (the ground is right below) and straight up. The hit distances are
    // the check that the structures really hold the world: the terrain
    // heightfield is *not* in them (it carries heights, not positions), so a
    // downward ray only hits what a model provides, and its distance is the
    // height above whatever that is.
    const Vector3f cameraPosition = edenRenderer.getRtCameraPosition();
    RayTracing::ProbeResult down;
    RayTracing::ProbeResult up;
    const bool bDown =
        rayTracing.traceProbe( pRtTlas, cameraPosition, Vector3f( 0.0f, -1.0f, 0.0f ), 2000.0f, down );
    const bool bUp = rayTracing.traceProbe( pRtTlas, cameraPosition, Vector3f( 0.0f, 1.0f, 0.0f ), 2000.0f, up );

    RayTracing::ProbeResult forward;
    const bool bForward = rayTracing.traceProbe( pRtTlas, cameraPosition, edenRenderer.getRtCameraDirection(),
                                                2000.0f, forward );

    // One report per 60 frames: the numbers move with the camera, and this is
    // not meant to be a per frame trace.
    if ( bReport ) {
        OTDU_LOG_INFO( "Ray tracing world: %zu instances, TLAS %llu KB, built in %.2f ms, camera (%.1f, %.1f, %.1f)\n",
                       instances.size(), static_cast<unsigned long long>( pRtTlas->Size / 1024 ), buildMs,
                       cameraPosition.x, cameraPosition.y, cameraPosition.z );
        if ( bDown ) {
            OTDU_LOG_INFO( "Ray tracing world: down ray -> hit %u, t %.2f m\n", down.Hit, down.Distance );
        }
        if ( bUp ) {
            OTDU_LOG_INFO( "Ray tracing world: up ray -> hit %u, t %.2f m\n", up.Hit, up.Distance );
        }
        if ( bForward ) {
            OTDU_LOG_INFO( "Ray tracing world: forward ray -> hit %u, t %.2f m, instance %u, primitive %u\n",
                           forward.Hit, forward.Distance, forward.Instance, forward.Primitive );
        }
    }
}
