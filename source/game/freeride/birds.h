#pragma once

#include "core/math.h"
#include "filesystem/bank.h"
#include "render/vulkan/render_device.h"

#include <random>
#include <string>
#include <vector>

class EdenRenderer;
struct EdenSceneParams;
struct GPUTexture;

// The birds of the island (docs/re/aircraft_birds_spec.md).
//
// [DATA] islands/<island>/fx/details/birds.bnk holds one 64 x 64 DXT5 sprite of
// a bird in flight per species - 5 materials in birds.2dm, each with a COLOR
// texture slot: NOIR (crow), FAUCON (falcon), MOUETTE (seagull), PIGEON and
// MOINEAU (sparrow) - and no geometry: the original draws them as sprites.
// Nothing else in the data says how many or where (fx.ini has the grass counts,
// not the birds; modifiers.xmb does not name them), so the flocking below is the
// port's own [INF]: flocks of 5..9 birds wander 25..70 m above the ground around
// the player, respawn out of sight, and land when it rains hard.
//
// The port draws them as camera facing quads through its own GLSL shaders
// (resources/shaders/bird.*) inside the scene's blended pass (ePass::Effects),
// so the haze and the post processing cover them like the rest of the scene.
class Birds {
public:
    using SurfaceFunction = std::function<bool( float x, float z, float y, float &outHeight )>;

    static constexpr int32_t kMaxBirds = 96;
    static constexpr int32_t kMaxFlocks = 8;
    // The sprites are 5 species; more materials than this are ignored.
    static constexpr int32_t kMaxSpecies = 8;

    Birds();
    ~Birds();

    // Loads birds.bnk and its species sprites. surface() keeps the flocks above
    // the terrain.
    bool load( const std::string &island );
    void unload();
    inline bool isLoaded() const { return bLoaded; }
    inline int32_t getSpeciesCount() const { return static_cast<int32_t>( species.size() ); }

    // cameraPosition / cameraForward: where a new flock appears (never in
    // front of the player). rain: 0..1 (a rain preset grounds most of them),
    // sun: 0..1 brightness of the sky (the sprite tint).
    // The camera pose comes from the last draw() (the view matrix is the only
    // reliable source of the look direction): the first frame after load only
    // records it.
    void tick( float deltaTime, float rain, float sun, const SurfaceFunction &surface );
    // Blended pass (EdenSceneView::ePass::Effects).
    void draw( RenderDevice &device, const Matrix4x4f &viewProjection, const EdenSceneParams &sceneParams );
    // Birds drawn on the last frame (within the view radius and awake).
    inline int32_t getNumDrawn() const { return drawnCount; }
    inline int32_t getNumFlocks() const { return static_cast<int32_t>( flocks.size() ); }
    // -logbirds: the flock / bird counts when they change.
    void logStats( float deltaTime );
    // -birdsnear: the flocks spawn 60..200 m around the player instead of
    // 250..700 m, so a test shot has birds in frame.
    bool isNearMode() const;

private:
    struct Species {
        std::string Name; // material name ("MOUETTE")
        GPUTexture *pTexture = nullptr;
        float Wingspan = 1.2f; // m
    };
    struct Flock {
        Vector3f Centre;
        Vector3f Heading; // flat unit vector
        float Speed = 12.0f;  // m/s
        float Turn = 0.0f;    // rad/s
        float Altitude = 40.0f; // m above the ground
        float Ground = 0.0f;
        float GroundTimer = 0.0f;
        int32_t Species = 0;
        bool bAlive = false;
    };
    struct Bird {
        int32_t Flock = -1;
        Vector3f Offset;   // flock frame offset (m)
        float Orbit = 0.0f; // rad/s around the flock centre
        float Angle = 0.0f;
        float Flap = 0.0f;  // rad
        float FlapRate = 0.0f;
        float Size = 1.0f;  // wingspan scale
        Vector3f Position;
        bool bAlive = false;
    };

    void spawnFlock( Flock &flock, const Vector3f &cameraPosition, const Vector3f &cameraForward,
                     const SurfaceFunction &surface, std::mt19937 &random );
    void rebuildBirds();
    bool initializePipeline( RenderDevice &device );

    bool bLoaded = false;
    Bank bank;
    std::vector<Species> species;
    std::vector<Flock> flocks;
    std::vector<Bird> birds;
    std::mt19937 random;
    float time = 0.0f;
    // The camera pose the flocks are spawned / recycled around, taken from the
    // last draw() (the view matrix rows are the only reliable look direction).
    Vector3f cameraPosition;
    Vector3f cameraForward;
    bool bHasCameraPose = false;
    float sunBrightness = 1.0f;
    float rainAmount = 0.0f;
    int32_t drawnCount = 0;
    // Log throttles.
    float logTimer = 0.0f;
    int32_t loggedDrawn = -1;
    int32_t loggedFlocks = -1;

    RenderDevice *pDevice = nullptr;
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool bReady = false;
    // One vertex batch per species (the texture is one binding).
    struct BirdVertex {
        float X, Y, Z;
        float U, V;
        uint32_t Color;
    };
    std::vector<BirdVertex> vertexScratch[kMaxSpecies];
};
