#pragma once

#include "core/math.h"
#include "render/vulkan/render_device.h"

#include <functional>

struct EdenSceneParams;

// Ground materials of the port's surface effects. The original game has three
// tyre surfaces in the car data (TyreSetup: tarmac, dirt, grass) and encodes
// the rest in the terrain colour maps (the beach sand is a texture, the mud is
// dirt under the rain), so the classification below is the port's own: the
// physics hit gives road / terrain, the wheel surface gives the tyre family and
// the height above the sea splits a low beach off the grass.
enum class eGroundMaterial : uint8_t {
    Tarmac = 0,
    Dirt,
    Sand,
    Grass,
    Snow,
    Count,
};

const char *GroundMaterialName( eGroundMaterial material );

// Wetness of the ground, per material: rain soaks it, the sun dries it. This is
// what makes mud (wet dirt) splash and dry sand dust, and it is what a future
// snow cover / terrain shader can read too. The rates are the port's [INF] (the
// original keeps no such state: its weather preset switches the wet road on and
// off in one frame).
struct GroundWetness {
    float Value[static_cast<size_t>( eGroundMaterial::Count )] = {};

    // rainIntensity: 0..1 (rain_ground_global_intensity of the preset),
    // sunEnergy: 0..~1.2 (the luminance of the preset's sun_diffuse).
    void update( float deltaTime, float rainIntensity, float sunEnergy );
    float get( eGroundMaterial material ) const {
        return Value[static_cast<size_t>( material )];
    }
    // Mud is dirt holding water: the effects read this to pick a dark, sticky
    // splash and a treaded track instead of a dust cloud.
    bool isMud( float threshold = 0.55f ) const { return get( eGroundMaterial::Dirt ) >= threshold; }
};

// Wheel spray / dust and the tyre-track decal trail (docs/re/surface_effects_spec.md).
//
// Nothing in the original draws particles (the .prt files are not parsed and
// the port has no billboard system), so this is port-native: a CPU pool of
// camera facing quads for the splashes and a ring buffer of ground quads for
// the tracks, both drawn with the port's own GLSL shaders
// (resources/shaders/surfacefx.*, surfacedecal.frag) inside the scene's blended
// pass, so the haze and the post processing cover them like the rest of the
// scene.
class EdenSurfaceFx {
public:
    // One wheel of one frame, as the physics reports it.
    struct WheelContact {
        Vector3f Position; // world contact point
        Vector3f Normal = Vector3f( 0.0f, 1.0f, 0.0f );
        Vector3f Velocity; // wheel travel (m/s), world
        eGroundMaterial Material = eGroundMaterial::Tarmac;
        float Slip = 0.0f; // |slip ratio| + |slip angle|
        float Load = 0.0f; // N
        bool bContact = false;
        // Hard braking (docs/re/surface_effects_spec.md 7): the pad torque the
        // brake controller applies (VehicleDynamics::getBrakeTorque) and how
        // much of the tyre's friction it uses (0..1). The torque heats the
        // disc, the slide leaves the black mark even when the ABS keeps the
        // slip ratio under the slide threshold.
        float BrakeTorque = 0.0f;  // N m
        float WheelRadius = 0.34f; // m (the disc turns at v / R)
        float BrakeSlip = 0.0f;    // 0..1
        bool bFront = true;        // the front discs are the bigger ones
        // The wheel's brake disc (drawing): world centre, the axle direction
        // towards the outside of the car and the wheel's up axis. Radius 0
        // hides the disc (a car without a modelled hub, a bike).
        Vector3f DiscCenter;
        Vector3f DiscAxis = Vector3f( 1.0f, 0.0f, 0.0f );
        Vector3f DiscUp = Vector3f( 0.0f, 1.0f, 0.0f );
        float DiscRadius = 0.0f; // m
    };

    static constexpr uint32_t kMaxParticles = 3000;
    static constexpr uint32_t kMaxDecals = 1024;
    static constexpr uint32_t kMaxWheels = 8;

    // How a material throws its spray around (port tuning, [INF]).
    struct MaterialProfile {
        Vector4f SplashColor; // dry
        Vector4f WetColor;    // soaked (mud, damp sand)
        Vector4f DecalColor;
        float SplashRate = 0.0f; // particles per second at 1 m/s of travel
        float SlipBoost = 0.0f;  // extra rate per unit of slip
        float UpSpeed = 1.0f;    // m/s of the initial upward velocity
        float BackSpeed = 0.2f;  // fraction of the wheel travel sent backwards
        float Size = 0.15f;      // m
        float Life = 0.7f;       // s
        float DecalRate = 0.0f;  // decals per metre travelled (0: none)
        float DecalLife = 20.0f; // s
        float Tread = 0.5f;
        float WetnessBoost = 0.0f; // how much the wetness multiplies the rate
    };


    EdenSurfaceFx();
    ~EdenSurfaceFx();

    bool initialize( RenderDevice *pDevice );
    void destroy();
    inline bool isReady() const { return bReady; }

    // -nosurfacefx. Off: nothing is spawned and nothing is drawn (the wetness
    // model keeps running, so a -nosurfacefx A/B shows the ground only).
    inline void setEnabled( bool bEnable ) { bEnabled = bEnable; }
    inline bool isEnabled() const { return bEnabled; }
    // -surfacefxscale <n>: emission multiplier (0 = none, 2 = double).
    inline void setEmissionScale( float scale ) { emissionScale = Max( scale, 0.0f ); }
    // -nobrakeheat: the discs keep their temperature (the model is untouched,
    // the cooling still runs) but no glow is drawn - the A/B of the effect.
    inline void setBrakeGlowEnabled( bool bEnable ) { bBrakeGlow = bEnable; }
    inline bool isBrakeGlowEnabled() const { return bBrakeGlow; }

    // Ages the pools and integrates the wetness of the ground.
    void update( float deltaTime, float rainIntensity, float sunEnergy );

    // Spawns the spray / dust of one wheel (the per-wheel rate is integrated
    // here, so calling it every frame with the current contact is enough) and
    // lays a track decal once the wheel has rolled far enough. The contact also
    // carries the brake pads (torque, slide) and the disc pose: the disc heats
    // up with the braking power and cools in the air flow here too.
    void emitWheel( uint32_t wheelIndex, const WheelContact &contact, float deltaTime );

    // Rain falling on the car: small splash particles on the body's upper
    // surfaces (boundsMin/Max: the body's world AABB). The rate follows the
    // rain intensity and the speed (the airflow throws more of it onto the
    // body). They are ordinary Particle slots: aged by update(), drawn with
    // the rest of the spray.
    // `wetness` (0..1) adds the water blown off a wet body at speed: those
    // droplets leave the rear / flanks and read as the car shedding its film.
    void emitRainImpacts( float deltaTime, const Vector3f &boundsMin, const Vector3f &boundsMax, float rainIntensity,
                          float speedKmh, float wetness = 0.0f );

    // Draws the tracks and the particles. Call inside the scene's blended pass:
    // depth tested, no depth write, alpha blended. `viewProjection` is the
    // scene's (EdenRenderer::getViewProjection), `sceneParams` gives the camera
    // basis for the billboards and the camera position for the culling.
    void draw( RenderDevice &device, const Matrix4x4f &viewProjection, const EdenSceneParams &sceneParams );

    inline const GroundWetness &getWetness() const { return wetness; }
    inline uint32_t getNumParticles() const { return numAliveParticles; }
    inline uint32_t getNumDecals() const { return numAliveDecals; }
    // The brake disc of one wheel: its temperature (C) and the 0..1 glow the
    // drawing derives from it (-brakeheattest / -logbrakeheat).
    inline float getBrakeTemperature( uint32_t wheelIndex ) const {
        return wheelIndex < kMaxWheels ? brakeDiscs[wheelIndex].Temperature : 0.0f;
    }
    inline float getBrakeGlow( uint32_t wheelIndex ) const {
        return wheelIndex < kMaxWheels ? brakeDiscs[wheelIndex].Glow : 0.0f;
    }
    float getHottestBrakeTemperature() const;

    // Terrain puddles (session 65, port-native [INF]: the retail terrain
    // shader carries no rain constants - the ROADS pool through the retail
    // RainSpecs permutation): a coarse grid around the car is sampled every
    // so often through the height query; local minima become puddle quads
    // whose alpha follows the ground wetness - they pool up in the rain and
    // dry away with it (-nopupdles off).
    static constexpr uint32_t kMaxPuddles = 48;
    void setHeightQuery( std::function<bool( float x, float z, float &outHeight )> query );
    // The surface query (EdenWorld::getDriveHeight, road meshes included):
    // the pool corners' first choice — the heightfield alone sits below the
    // road meshes, and the roadOffset measured at the car can not follow a
    // delta that varies along the road (cuts, embankments, climbs, tunnels,
    // where the heightfield refuses the carved cells at all): the pool sank
    // under the tarmac and flickered / vanished uphill. Fallen back to
    // heightQuery + roadOffset where the surface query has no answer.
    void setSurfaceQuery( std::function<bool( float x, float z, float fromY, float &outHeight )> query );
    // The headlight / rear-light ground pools (EdenHeadlightFrameCones /
    // EdenRearLightFrameCones): one soft additive-strength quad per cone,
    // corners height-queried like the puddles. Stateless per frame.
    void updateLightPools();
    // The vertical offset between the road mesh and the heightfield under the
    // car (the game measures it at the car: the pool corners sample the
    // heightfield, the roads are sector objects above it — without the offset
    // the headlight pools draw under the road and vanish, session 69).
    inline void setRoadOffset( float offset ) { roadOffset = offset; }
    inline void setPuddlesEnabled( bool bEnable ) { bPuddlesEnabled = bEnable; }
    void updatePuddles( float deltaTime, const Vector3f &center, float wetness );
    inline uint32_t getNumPuddles() const { return numAlivePuddles; }
    // Log lines (-logsurfacefx): the wetness and the pool sizes when they
    // change, so a headless run can prove the system ran.
    void logStats();
    // Log line (-logbrakeheat): every disc's temperature when the hottest one
    // moved by more than 20 C, so a headless drive proves the heat law.
    void logBrakeHeat();
    // Forgets every particle, decal and disc temperature (a car teleport, a
    // car change: the discs belong to the car that just left).
    void clear();
    // Sets every material's wetness (a test start, -surfacefxwet): with a dry
    // preset the ground then dries out over the next minutes, which is how the
    // drying law is checked headlessly.
    void seedWetness( float value );
    // Sets every disc to a temperature (a test start, -brakeheattest <0..100>
    // of the dial): the glow is then visible from the first frame, so a shot
    // does not need a braking run.
    void seedBrakeHeat( float celsius );

private:
    // One quad of the frame's vertex buffer (world space, already expanded).
    struct FxVertex {
        float X, Y, Z;
        float U, V;
        uint32_t Color; // RGBA8
    };

    struct Particle {
        Vector3f Position;
        Vector3f Velocity;
        Vector4f Color;
        float Life = 0.0f;    // s, counted up
        float MaxLife = 1.0f; // s
        float Size = 0.2f;    // half extent, m
        float Gravity = -9.81f;
        float Drag = 1.2f;
        bool bAlive = false;
    };

    struct Decal {
        Vector3f Position;
        Vector3f Normal;
        Vector3f Tangent; // travel direction, on the surface
        Vector4f Color;
        float HalfWidth = 0.1f;
        float HalfLength = 0.15f;
        float Tread = 0.5f; // surfacedecal.frag: 0 smooth (sand), 1 lugs (mud / snow)
        float Age = 0.0f;
        float Life = 20.0f;
        bool bAlive = false;
    };

    // One wheel's brake disc (docs/re/surface_effects_spec.md 7): the pose the
    // glow is drawn at and the temperature the physics-driven heat integrated.
    struct BrakeDisc {
        Vector3f Position;                    // world, hub centre
        Vector3f Axis = Vector3f( 1, 0, 0 );  // world, outward along the axle
        Vector3f Up = Vector3f( 0, 1, 0 );    // world, the wheel's up axis
        float Radius = 0.0f;                  // m
        float Temperature = 35.0f;            // C, ambient at spawn
        float Glow = 0.0f;                    // 0..1, derived from the temperature
        bool bValid = false;                  // a disc pose was fed this frame
    };

    void updateBrakeDisc( uint32_t wheelIndex, const WheelContact &contact, float deltaTime );
    void spawnParticle( const Particle &particle );
    void spawnDecal( const Decal &decal );
    void appendQuad( std::vector<FxVertex> &vertices, const Vector3f corners[4], uint32_t packed ) const;
    void appendBillboard( std::vector<FxVertex> &vertices, const Vector3f &position, const Vector3f &right,
                          const Vector3f &up, float size, const Vector4f &color ) const;
    void appendGroundQuad( std::vector<FxVertex> &vertices, const Decal &decal ) const;
    void appendDiscQuad( std::vector<FxVertex> &vertices, const BrakeDisc &disc, float radius, float outboard,
                         const Vector4f &color ) const;

    RenderDevice *pDevice = nullptr;
    // Per render pass: a pipeline must be created against the pass it draws
    // in (the surface FX draw inside the offscreen scene pass; pipelines
    // built against the swapchain pass tripped
    // VUID-vkCmdDraw-renderPass-02684 every frame).
    struct PassPipelines {
        VkPipeline particle = VK_NULL_HANDLE;
        VkPipeline decal = VK_NULL_HANDLE;
        VkPipeline brakeGlow = VK_NULL_HANDLE;
        VkPipeline lampGlow = VK_NULL_HANDLE;
    };
    std::unordered_map<VkRenderPass, PassPipelines> passPipelines;
    bool bReady = false;
    bool bEnabled = true;
    bool bBrakeGlow = true;
    float emissionScale = 1.0f;
    float sceneTime = 0.0f;

    std::vector<Particle> particles;
    std::vector<Decal> decals;
    uint32_t nextParticle = 0;
    uint32_t nextDecal = 0;
    uint32_t numAliveParticles = 0;
    uint32_t numAliveDecals = 0;

    GroundWetness wetness;
    // The material of the last emission (the -logsurfacefx line names it: it is
    // how a headless run proves what the port classified the ground as).
    eGroundMaterial lastMaterial = eGroundMaterial::Tarmac;
    bool bHasEmission = false;
    // Per wheel: the emission accumulator (particles) and the position the last
    // track decal was laid at.
    float wheelEmission[kMaxWheels] = {};
    // The rain-on-the-car impact accumulator (emitRainImpacts) and the water
    // film's drip accumulator.
    float rainImpactAccum = 0.0f;
    float wetDripAccum = 0.0f;
    Vector3f wheelDecalAnchor[kMaxWheels] = {};
    bool bWheelDecalAnchor[kMaxWheels] = {};
    // The brake discs (docs/re/surface_effects_spec.md 7).
    BrakeDisc brakeDiscs[kMaxWheels];

    std::vector<FxVertex> vertexScratch;      // the frame's particle quads
    std::vector<FxVertex> decalScratch[2];    // tyre tracks: 0 smooth, 1 treaded
    std::vector<FxVertex> discScratch;        // brake disc faces
    std::vector<FxVertex> haloScratch;        // brake disc halos (camera facing)
    std::vector<FxVertex> lampScratch[2];     // car lamp glows: 0 halo only, 1 with the hot core
    std::vector<FxVertex> poolScratch;        // the light pools (the radial falloff mode)
    float roadOffset = 0.0f;                  // road mesh minus heightfield (setRoadOffset)
    std::function<bool( float, float, float, float & )> surfaceQuery; // x, z, fromY -> surface height

    // The terrain puddles (the comment at setPuddlesEnabled).
    struct Puddle {
        Vector3f Position;
        float Size = 3.0f;    // half extent, m
        float Corner[4] = {}; // sampled corner heights (the quad follows the slope)
        bool bAlive = false;
    };
    std::vector<Puddle> puddles;
    uint32_t numAlivePuddles = 0;
    float puddleScanTimer = 0.0f;
    bool bPuddlesEnabled = true;
    std::function<bool( float, float, float & )> heightQuery;
    float loggedWetness = -1.0f;
    float loggedBrakeHeat = -1000.0f;
    uint32_t loggedParticles = 0xFFFFFFFFu;
    uint32_t loggedDecals = 0xFFFFFFFFu;
};
