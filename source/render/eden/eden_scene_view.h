#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_neons.h"
#include "render/eden/eden_ocean.h"
#include "render/eden/eden_rain.h"
#include "render/eden/eden_windscreen.h"
#include "render/eden/eden_planar_reflections.h"
#include "render/eden/eden_postfx.h"
#include "render/eden/eden_shadows.h"
#include "render/eden/eden_sky.h"
#include "render/eden/eden_sun.h"
#include "render/eden/eden_weather.h"
#include "render/postfx/fullscreen_pass.h"
#include "system/cmd_line.h"

#include <cstdlib>
#include "world/eden_world.h"

// Island view rendered like the original renderer: streamed world (.pmi
// instances, wet road), sky dome, sun halo / flares, 3D clouds, dynamic
// environment cube, real time sun shadows, haze and photoshop post
// processing, lit by a (cinematic) weather. Used by the press start screen
// and the real time cinematics; the owner draws its own objects (cars,
// characters, cinematic sets) through the draw callback.
class EdenSceneView {
public:
    enum class ePass {
        Environment,   // environment cube faces (world only)
        DepthPrepass,  // Z pre-pass (FullscreenDepthTarget)
        ShadowCasters, // sun shadow slices
        Opaque,        // main scene (opaque, blended draws are deferred)
        Reflection,    // ocean reflection (mirror camera, opaque only)
        // GSReflection actor layers: the actors only (avatars, cars), seen
        // by a projector camera (see setPlanarReflectionActors).
        ProjectorActors,
        // GSReflection wall mirrors: the whole scene (sets, props, avatars)
        // seen by the mirror camera — a vertical actor layer reflects the
        // room, not just the actors (session 45, the clothes-shop mirror).
        ProjectorScene,
        // Port effects drawn over the scene (wheel spray, tyre tracks): called
        // in the blended pass, after the deferred blended draws and before the
        // haze, so the haze and the post processing cover them. The owner draws
        // nothing else there (it is not a scene pass).
        Effects,
    };
    // Lightmap day / night blend (NIGHTLMAP) driven by the weather.
    enum class eLightmapScope {
        Exterior,       // spot exterior: TD_Building with U6 <- spot_exterior_lightmap_day_night
        Interior,       // house interior: TD_Building, TD_Furnitures, d5013ed0dcacafaf <- spot_interior_lightmap_day_night
        ExteriorSprite, // TD_Sprite with OcclusionDayNight: .z = 1 - x per frame
        ExteriorLamp,   // TD_Lamp nodes: __Instance_Color.w = y (the night lamp emissive)
        Neon,           // neon set materials <- building_neon_strength (EdenNeons)
    };
    using DrawCallback = std::function<void( EdenRenderer &renderer, ePass pass )>;

    struct Camera {
        Vector3f Position;
        Quaternionf Rotation; // Eden convention: forward = conj(q) (0, 0, 1)
        float Fov = 60.0f * kDegToRad; // vertical
        // Per-camera FOV scale override (OpenTDU2 fov_scale): negative = the
        // global option stands (GetGameOptions().FovScale). The map screen
        // pins 1.0: its framing math assumes the authored 60 degree lens.
        float FovScale = -1.0f;
        float Near = 0.8f;    // cinematic defaults (FUN_00b3eba0)
        float Far = 40000.0f;
    };

public:
    EdenSceneView();
    ~EdenSceneView();

    // Loads the world (sectors around streamCenter), the cinematic weathers,
    // clouds, sky, sun and post processing.
    bool initialize( const char *pIsland, const Vector3f &streamCenter, int32_t sectorRadius );
    void destroy();
    inline bool isReady() const { return bReady; }

    inline EdenWorld &getWorld() { return world; }
    inline WeatherLibrary &getWeathers() { return weathers; }
    inline const WeatherPreset *getWeather() const { return pWeather; }
    inline const EdenSceneParams &getSceneParams() const { return sceneParams; }
    // Dynamic environment cube (nullptr until its first faces are drawn).
    GPUTexture *getEnvironmentCube() const;

    void setWeather( const WeatherPreset *pPreset ) { pWeather = pPreset; }
    void setShadowConfig( const EdenShadows::Config &config ) { shadows.setConfig( config ); }
    void setCamera( const Camera &newCamera ) {
        camera = newCamera;
        // A/B: -forcenear <m> overrides every camera's near plane (is a
        // surface cut by the near plane?).
        static const float forcedNear = static_cast<float>( atof( CmdLine::GetArgValue( "-forcenear" ) != nullptr
                                                                       ? CmdLine::GetArgValue( "-forcenear" )
                                                                       : "0" ) );
        if ( forcedNear > 0.0f ) {
            camera.Near = forcedNear;
        }
    }
    // World map view: no distance fog and no haze pass (the retail map is
    // clear at every zoom, owner's grabs).
    // bKeepSky: the sky and the sun stay (the fly-out from the car / back to it
    // starts in the normal sky, only the fog and haze are off).
    // Per-scene atmosphere switches: the full-screen haze pass (weather
    // Skydome_haze, postfx_spec 5) and the shaders' distance fog (fog_max).
    // The intro cinematic turns both off (the owner, 2026-11-04: "that scene
    // originally shouldn't have any fog or haze"; whether retail draws the
    // haze there is open in postfx_spec 5).
    void setHazeEnabled( bool bEnable ) { bHazeEnabled = bEnable; }
    void setFogEnabled( bool bEnable ) { bFogEnabled = bEnable; }
    void setMapView( bool bEnable, bool bKeepSky = false ) {
        bMapView = bEnable;
        bMapKeepSky = bEnable && bKeepSky;
    }
    // Center of the environment cube (the car); disabled: no cube.
    void setEnvironmentCenter( const Vector3f &center, bool bEnable );
    void setDrawCallback( DrawCallback callback ) { drawCallback = std::move( callback ); }

    // Ocean (normal map, reflection, TD_Ocean draws) on / off: closed rooms
    // rendered without the island (FIRST_WM garage).
    void setOceanEnabled( bool bEnable );
    inline bool isOceanEnabled() const { return bOceanEnabled; }

    // GSReflection (planar / refraction projective maps, see
    // EdenPlanarReflections). Per game mode switch:
    // setPlanarReflectionsEnabled( EdenPlanarReflections::IsEnabledForGameMode( 0x20 ) ).
    void setPlanarReflectionsEnabled( bool bEnable );
    // Registers the projective materials (TD_Water U4, TD_Furnitures U7) of
    // a model of a registered bank (call for each model of the bank).
    uint32_t registerProjectiveMaterials( EdenModel &model );
    // Builds the projectors of an Entity__Actor__Mesh D_REFLECT model drawn
    // at 'world' (the model must outlive the view or clearPlanarReflections
    // be called). pBankName: the bank (EPH / ECAS banks: no actor layers),
    // pActorLayer1 / 2: the entity AvatarReflectlayername1 / 2.
    uint32_t registerReflectionScene( const EdenModel &model, const Matrix4x4f &world, const char *pBankName,
                                      const char *pActorLayer1 = nullptr, const char *pActorLayer2 = nullptr );
    // Actor layers / refraction actors: the draw callback is called with
    // ePass::ProjectorActors (it must then draw the actors only). Off by
    // default.
    inline void setPlanarReflectionActors( bool bEnable ) { bProjectorActors = bEnable; }
    // mgr+0x24: actors drawn in the refraction layer (C_JACCUZZI / C_A4).
    inline void setRefractionActors( bool bEnable ) { planarReflections.setRefractionActors( bEnable ); }
    void clearPlanarReflections();
    inline EdenPlanarReflections &getPlanarReflections() { return planarReflections; }

    // NIGHTLMAP of the lightmapped materials of a model, overwritten every
    // frame from the weather (FUN_00980e50 / FUN_00a4ee30). Returns the
    // number of materials registered.
    uint32_t registerLightmapMaterials( EdenModel &model, eLightmapScope scope );
    // Neon materials of a model (world neon set, FUN_00985e40 [CODE]):
    // TD_NEONDECAL 63078165ca37416d and 18315cacac6cff57 (any switches),
    // fcdaa96bdeadbeef sprites with U4 (spec 0x23) set. Every frame
    // (FUN_009834d0 -> FUN_009829d0) WeatherEmissive = (x, x, x, 0) and the
    // 0x97909ca3a58487f1 parameter x = building_neon_strength.x; blinking
    // materials get an AnimValue (EdenNeons), updated every frame. World
    // sector / area / .pmi neons are registered by EdenWorld itself.
    uint32_t registerNeonMaterials( EdenModel &model );
    void clearLightmapMaterials() {
        lightmapMaterials.clear();
        neonBlinks.clear();
    }

    // Port effects (wheel spray, tyre tracks) in the blended pass: off unless a
    // game mode turns it on (only FREERIDE feeds the effects today, and the
    // other owners of this view draw their actors in every pass, so an extra
    // pass would draw them twice).
    inline void setEffectsPass( bool bEnable ) { bEffectsPass = bEnable; }
    // Speed radial blur of the final image (the retail camera blur_int_*): 0 =
    // plain, 1 = the full strength of the data. It is applied by the final
    // blit / upscale shader, so it also covers the frontend-composite path.
    inline void setSpeedBlur( float amount ) { speedBlur = Clamp( amount, 0.0f, 1.0f ); }

    // Falling rain (WFX_Rain blocks, drop streaks, ground splashes): on when
    // the frame's weather rains, -rain forces it on a dry sky, -norain turns
    // the falling rain off (the wet ground keeps the weather values).
    inline void setRainEnabled( bool bEnable ) { bRainEnabled = bEnable; }
    inline EdenRain &getRain() { return rain; }
    // The sea of the view: its water plane height is the reference the island
    // ambience (sea / cliff / mountain) and the surface effects measure from.
    inline EdenOcean &getOcean() { return ocean; }
    inline const EdenOcean &getOcean() const { return ocean; }

    // Rain on the windscreen (droplets + wet film, cleared by the wipers):
    // the camera owner gates it on the inside views (EdenWindscreen).
    inline void setDropletsEnabled( bool bEnable ) {
        if ( bEnable != bDropletsEnabled ) {
            bDropletsEnabled = bEnable;
            OTDU_LOG_INFO( "EdenSceneView: windscreen droplets %s\n", bEnable ? "on" : "off" );
        }
        windscreen.setEnabled( bEnable );
    }
    inline EdenWindscreen &getWindscreen() { return windscreen; }

    void tick( float deltaTime );
    void renderOffscreen();
    void draw();

private:
    EdenWorld world;
    WeatherLibrary weathers;
    EdenSky sky;
    EdenSun sun;
    EdenPostFx postFx;
    EdenShadows shadows;
    EdenOcean ocean;
    // Occlusion culling of the main view (eden_occlusion.h): the grid of this
    // frame (nullptr = none), set around the pre-pass / opaque traversal.
    EdenOcclusion occlusion;
    const EdenOcclusion::Grid *pOcclusionGrid = nullptr;
    RenderTarget *pOcclusionTarget = nullptr; // the tile maxima of the pre-pass depth (R32)
    bool bLastWaterHidden = false; // -logocclusion: log the ocean-skip changes
    EdenPlanarReflections planarReflections;
    // NIGHTLMAP materials of the scene (the day / night blend of the
    // buildings): owner is the model that owns the material, so a streamed
    // sector can unregister its own entries when it unloads.
    struct LightmapMaterial {
        EdenMaterial *pMaterial;
        eLightmapScope Scope;
        const EdenModel *pOwner;
    };
    std::vector<LightmapMaterial> lightmapMaterials;
    void unregisterLightmapMaterials( const EdenModel &model );
    std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> neonBlinks;
    // Ocean mirror refresh counter (OceanReflections): reset when the view
    // (re)initializes, so a "render once" profile always has a first frame.
    uint32_t oceanReflectionFrame = 0;
    EdenRain rain;
    bool bRainEnabled = false;
    EdenWindscreen windscreen;
    bool bDropletsEnabled = false;
    bool bOceanEnabled = true;
    bool bProjectorActors = false;
    bool bEffectsPass = false;
    bool bMapView = false;
    bool bHazeEnabled = true;
    bool bFogEnabled = true;
    bool bMapKeepSky = false;
    Bank cloudsBank;
    EdenModel clouds;
    std::string cloudsSuffix;
    FullscreenPass fullscreen;
    const WeatherPreset *pWeather = nullptr;
    Camera camera;
    DrawCallback drawCallback;
    EdenSceneParams sceneParams;
    RenderTarget *pSceneTarget = nullptr;
    RenderTarget *pViewPosTarget = nullptr;
    RenderTarget *pEnvironmentTarget = nullptr;
    // The faces render into this scratch cube, copied to pEnvironmentTarget
    // after the pass: the cars drawn in the Environment pass sample the cube
    // being built, and a sampled image must not be the render target too.
    RenderTarget *pEnvironmentScratch = nullptr;
    RenderTarget *pFinalTarget = nullptr;
    // The windscreen droplets (normal.xy + presence), composited by the final
    // blit while the effect is active (half the scene size).
    RenderTarget *pDropletTarget = nullptr;
    // Ray traced ambient occlusion mask (-rtao <samples>): R8, written by the
    // rt_ao compute dispatch (see ray_tracing.h), multiplied onto the scene by
    // the swapchain blit. nullptr unless the flag was passed.
    RenderTarget *pAOTarget = nullptr;
    bool bAORan = false; // the mask is valid (the dispatch succeeded once)
    Vector3f environmentCenter;
    bool bEnvironmentEnabled = false;
    uint32_t environmentFace = 0;
    uint32_t environmentFrame = 0;
    float time = 0.0f;
    float frameDelta = 0.0f;
    // Speed radial blur strength (setSpeedBlur): the final blit / upscale pass
    // multiplies it into its radial smear (docs/re/needles_spec.md).
    float speedBlur = 0.0f;
    // The vertical FOV setupScene projected with (the camera's scale and the
    // narrow-aspect law applied): the RT AO frustum builds its rays from it.
    float effectiveFov = 60.0f * kDegToRad;
    bool bReady = false;

private:
    void setupScene( EdenSceneParams &params );
    void ensureTargets();
    void destroyTargets();
    void renderEnvironment( const EdenSceneParams &mainScene, bool bAllFaces );
    void generateViewPos();
    bool loadClouds( const std::string &islandFolder );
    void drawClouds( EdenRenderer &renderer );
    void drawObjects( EdenRenderer &renderer, ePass pass );
    void updateLightmapMaterials();
    // Neon set + AnimValue blink update (FUN_009834d0), called by renderOffscreen.
    void updateNeons();
};

// Helpers shared by the cinematic users.
Matrix4x4f EdenPerspectiveFovRH( float fovY, float aspect, float zNear, float zFar );
// The vertical FOV the projection actually uses: the camera's own scale
// (or the global fov_scale option when scale < 0) times the retail
// narrow-aspect law (FUN_00c9f680: below 16:9 the vertical FOV widens to
// keep the horizontal one constant). setupScene builds the matrix with it;
// the HUD's world markers project with the same value
// (GMFreeRide::projectToCanvas).
float EdenEffectiveFov( float fov, float fovScale, float aspect );
Matrix4x4f EdenRotationFromQuaternion( const Quaternionf &q );
