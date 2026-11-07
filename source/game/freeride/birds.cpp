#include "shared.h"
#include "birds.h"

#include "filesystem/gs_file.h"
#include "render/2dm.h"
#include "render/eden/eden_renderer.h"
#include "render/gs_render.h"
#include "render/shaders/builtin/bird_frag.h"
#include "render/shaders/builtin/bird_vert.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <cmath>

namespace {
// The push constants of bird.vert / bird.frag: the scene view-projection and
// the per-draw parameters.
struct BirdPushConstants {
    float ViewProjection[16];
    float Params[4];
};
static_assert( sizeof( BirdPushConstants ) <= RenderDevice::kPushConstantsSize,
               "bird push constants exceed the device limit" );

// Flock and bird behaviour: the port's own [INF] (the data has the sprites, not
// a flock definition).
constexpr float kPi = 3.14159265f;
constexpr int32_t kBirdsPerFlockMin = 5, kBirdsPerFlockMax = 9;
constexpr int32_t kFlocksAround = 5;
constexpr float kSpawnMinFar = 250.0f, kSpawnMaxFar = 700.0f;
constexpr float kSpawnMinNear = 60.0f, kSpawnMaxNear = 200.0f;
// A flock this far from the player is recycled (out of sight: the draw radius
// is smaller, so it never happens in view).
constexpr float kFlockRange = 1100.0f;
constexpr float kDrawRadius = 800.0f;
// Wingspans per species name [DATA: the 2db sprite is a bird in flight; the
// sizes are the real birds' and only set the on screen scale].
struct SpeciesInfo {
    const char *Name;
    float Wingspan;
};
const SpeciesInfo kSpeciesInfo[] = {
    { "MOUETTE", 1.35f }, // seagull
    { "FAUCON", 1.10f },  // falcon
    { "PIGEON", 0.70f },
    { "MOINEAU", 0.28f }, // sparrow (material MOINEAU, texture MOINO)
    { "NOIR", 0.95f },    // crow
};

uint32_t PackColor( const Vector4f &color ) {
    const uint32_t r = static_cast<uint32_t>( Saturate( color.x ) * 255.0f + 0.5f );
    const uint32_t g = static_cast<uint32_t>( Saturate( color.y ) * 255.0f + 0.5f );
    const uint32_t b = static_cast<uint32_t>( Saturate( color.z ) * 255.0f + 0.5f );
    const uint32_t a = static_cast<uint32_t>( Saturate( color.w ) * 255.0f + 0.5f );
    return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 );
}

float Wingspan( const char *pName ) {
    for ( const SpeciesInfo &info : kSpeciesInfo ) {
        if ( StringCmpI( info.Name, pName ) == 0 ) {
            return info.Wingspan;
        }
    }
    return 0.9f;
}
} // namespace

Birds::Birds() : random( 0x5eed1234u ) {}

Birds::~Birds() { unload(); }

bool Birds::isNearMode() const {
    static const bool bNear = CmdLine::HasArg( "-birdsnear" );
    return bNear;
}

void Birds::unload() {
    bLoaded = false;
    species.clear();
    flocks.clear();
    birds.clear();
    for ( auto &batch : vertexScratch ) {
        batch.clear();
    }
    drawnCount = 0;
    loggedDrawn = -1;
    loggedFlocks = -1;
    logTimer = 0.0f;
    if ( bank.isLoaded() ) {
        if ( gpRender != nullptr ) {
            gpRender->getTextureCache().releaseOwner( bank.getPath().c_str() );
        }
        bank.unload();
    }
    // The sprite pipeline belongs to this system (the device outlives it: the
    // mode unloads before the view is destroyed).
    if ( pDevice != nullptr && pipeline != VK_NULL_HANDLE ) {
        pDevice->destroyPipeline( pipeline );
    }
    pipeline = VK_NULL_HANDLE;
    bReady = false;
    pDevice = nullptr;
}

bool Birds::initializePipeline( RenderDevice &device ) {
    if ( bReady ) {
        return true;
    }
    pDevice = &device;
    PipelineDesc desc;
    desc.pVertexSPIRV = kSPIRV_bird_vert;
    desc.VertexSPIRVSize = sizeof( kSPIRV_bird_vert );
    desc.pFragmentSPIRV = kSPIRV_bird_frag;
    desc.FragmentSPIRVSize = sizeof( kSPIRV_bird_frag );
    desc.Attributes = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof( BirdVertex, X ) },
                        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( BirdVertex, U ) },
                        { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof( BirdVertex, Color ) } };
    desc.BindingStrides = { sizeof( BirdVertex ) };
    desc.Blend = eBlendMode::BM_Alpha;
    desc.Cull = eCullMode::CM_None;
    // Blended over the scene: the depth buffer hides a bird behind a building,
    // the depth write stays off (a bird must not cut the next one).
    desc.bDepthTest = true;
    desc.bDepthWrite = false;
    // The Effects pass draws into the scene render target, not into the
    // main (swapchain) render pass, and a pipeline is bound to one render pass:
    // the current one is the target the birds are drawn into.
    desc.RenderPass = device.getCurrentRenderPass();
    pipeline = device.createPipeline( desc );
    bReady = pipeline != VK_NULL_HANDLE;
    if ( !bReady ) {
        OTDU_LOG_WARN( "Birds: the sprite pipeline could not be created\n" );
    }
    return bReady;
}

bool Birds::load( const std::string &island ) {
    unload();
    const std::string path = gpFile->getBankFolder() + "islands/" + island + "/fx/details/birds.bnk";
    if ( !gpFile->fileExists( path.c_str() ) || !bank.load( path.c_str() ) ) {
        OTDU_LOG_INFO( "Birds: '%s' not available (no birds)\n", path.c_str() );
        return false;
    }
    TextureCache &textures = gpRender->getTextureCache();
    textures.loadBankTextures( bank, bank.getPath().c_str() );
    // The species come from the material library: one material per bird, its
    // COLOR texture slot the sprite. The 2db whose name hashes to that slot is
    // the file to bind.
    const BankEntry *pMaterials = bank.findEntry( "birds", "2dm" );
    Render2DM materials;
    if ( pMaterials == nullptr || !materials.parse( pMaterials->pData, pMaterials->Size ) ) {
        OTDU_LOG_WARN( "Birds: '%s' has no birds.2dm\n", path.c_str() );
        return false;
    }
    for ( const MaterialDesc &material : materials.getOrderedMaterials() ) {
        if ( static_cast<int32_t>( species.size() ) >= kMaxSpecies ) {
            break;
        }
        const uint64_t hashcode = material.findTexture( "COLOR" );
        if ( hashcode == 0 ) {
            continue;
        }
        std::string textureName;
        for ( const BankEntry &entry : bank.getEntries() ) {
            if ( entry.Extension == "2db" && GetIdentifier64bit( entry.Name.c_str() ) == hashcode ) {
                textureName = entry.Name;
                break;
            }
        }
        Species entry;
        entry.Name = material.Name;
        entry.Wingspan = Wingspan( material.Name.c_str() );
        entry.pTexture = textureName.empty() ? nullptr : textures.find( bank.getPath().c_str(), textureName.c_str() );
        if ( entry.pTexture == nullptr ) {
            OTDU_LOG_WARN( "Birds: material '%s': texture 0x%016llx not found\n", material.Name.c_str(),
                           static_cast<unsigned long long>( hashcode ) );
            continue;
        }
        OTDU_LOG_INFO( "Birds: species '%s' -> '%s.2db', wingspan %.2f m\n", entry.Name.c_str(), textureName.c_str(),
                       entry.Wingspan );
        species.push_back( entry );
    }
    if ( species.empty() ) {
        OTDU_LOG_WARN( "Birds: '%s' has no usable species\n", path.c_str() );
        return false;
    }
    bLoaded = true;
    OTDU_LOG_INFO( "Birds: %s: %zu species, up to %d in %d flocks of %d..%d, draw %.0f m\n", island.c_str(),
                   species.size(), kMaxBirds, kFlocksAround, kBirdsPerFlockMin, kBirdsPerFlockMax, kDrawRadius );
    return bLoaded;
}

void Birds::spawnFlock( Flock &flock, const Vector3f &cameraPosition, const Vector3f &cameraForward,
                        const SurfaceFunction &surface, std::mt19937 &random ) {
    const bool bNear = isNearMode();
    // -birdsfront (test): the flock appears in front of the camera, inside the
    // screen: a screenshot off the road would otherwise rarely catch one.
    static const bool bFront = CmdLine::HasArg( "-birdsfront" );
    const float minRing = bFront ? 25.0f : ( bNear ? kSpawnMinNear : kSpawnMinFar );
    const float maxRing = bFront ? 70.0f : ( bNear ? kSpawnMaxNear : kSpawnMaxFar );
    const Vector3f heading =
        cameraForward.length() > 0.0001f ? cameraForward.normalized() : Vector3f( 0.0f, 0.0f, 1.0f );
    const float headingAngle = std::atan2( heading.x, heading.z );
    for ( int32_t attempt = 0; attempt < 8; attempt++ ) {
        const float distance = std::uniform_real_distribution<float>( minRing, maxRing )( random );
        const float angle = bFront ? headingAngle + std::uniform_real_distribution<float>( -0.35f, 0.35f )( random )
                                  : std::uniform_real_distribution<float>( 0.0f, 2.0f * kPi )( random );
        // The angle is a yaw in this engine's convention (x = sin, z = cos), the
        // same atan2(heading.x, heading.z) the flock steers by.
        const Vector3f offset( std::sin( angle ) * distance, 0.0f, std::cos( angle ) * distance );
        // Never in front of the player: a flock appearing in the middle of the
        // screen is the one thing that reads as a bug.
        if ( !bFront && distance < maxRing * 0.75f && offset.x * heading.x + offset.z * heading.z > 0.35f * distance ) {
            continue;
        }
        const Vector3f centre = cameraPosition + offset;
        // The ground: the collision raycast reaches 60 m, so try a few start
        // heights (the same set the traffic uses).
        float ground = 0.0f;
        bool bGround = false;
        for ( const float lift : { 40.0f, 100.0f, -20.0f, -80.0f } ) {
            if ( surface( centre.x, centre.z, cameraPosition.y + lift, ground ) ) {
                bGround = true;
                break;
            }
        }
        if ( !bGround ) {
            continue;
        }
        flock.Altitude = bFront ? std::uniform_real_distribution<float>( 6.0f, 16.0f )( random )
                                : ( bNear ? std::uniform_real_distribution<float>( 10.0f, 26.0f )( random )
                                          : std::uniform_real_distribution<float>( 25.0f, 70.0f )( random ) );
        flock.Centre = Vector3f( centre.x, ground + flock.Altitude, centre.z );
        flock.Ground = ground;
        flock.Heading = Vector3f( std::sin( angle ), 0.0f, std::cos( angle ) );
        flock.Speed = std::uniform_real_distribution<float>( 8.0f, 15.0f )( random );
        flock.Turn = std::uniform_real_distribution<float>( -0.12f, 0.12f )( random );
        flock.GroundTimer = 0.0f;
        flock.Species = std::uniform_int_distribution<int32_t>( 0, static_cast<int32_t>( species.size() ) - 1 )( random );
        flock.bAlive = true;
        if ( CmdLine::HasArg( "-logbirds" ) ) {
            OTDU_LOG_INFO( "Birds: flock at (%.0f, %.0f, %.0f) %.0f m from the camera, %s, altitude %.0f m\n",
                           flock.Centre.x, flock.Centre.y, flock.Centre.z,
                           Vector3f( flock.Centre.x - cameraPosition.x, 0.0f, flock.Centre.z - cameraPosition.z ).length(),
                           species[static_cast<size_t>( flock.Species )].Name.c_str(), flock.Altitude );
        }
        return;
    }
    // No grounded spot around (no streamed terrain): retry on a later frame.
    flock.bAlive = false;
}

void Birds::rebuildBirds() {
    birds.clear();
    std::uniform_real_distribution<float> unit( 0.0f, 1.0f );
    for ( size_t f = 0; f < flocks.size() && static_cast<int32_t>( birds.size() ) < kMaxBirds; f++ ) {
        const Flock &flock = flocks[f];
        if ( !flock.bAlive ) {
            continue;
        }
        const int32_t count = std::uniform_int_distribution<int32_t>( kBirdsPerFlockMin, kBirdsPerFlockMax )( random );
        for ( int32_t i = 0; i < count && static_cast<int32_t>( birds.size() ) < kMaxBirds; i++ ) {
            Bird bird;
            bird.Flock = static_cast<int32_t>( f );
            // A loose bunch around the flock centre, each bird milling about it
            // at its own rate.
            const float spread = 12.0f + 6.0f * unit( random );
            bird.Offset = Vector3f( std::uniform_real_distribution<float>( -spread, spread )( random ),
                                    std::uniform_real_distribution<float>( -4.0f, 4.0f )( random ),
                                    std::uniform_real_distribution<float>( -spread, spread )( random ) );
            bird.Orbit = std::uniform_real_distribution<float>( 0.05f, 0.25f )( random ) *
                         ( unit( random ) < 0.5f ? -1.0f : 1.0f );
            bird.Angle = std::uniform_real_distribution<float>( 0.0f, 2.0f * kPi )( random );
            bird.Flap = std::uniform_real_distribution<float>( 0.0f, 2.0f * kPi )( random );
            bird.FlapRate = std::uniform_real_distribution<float>( 6.0f, 11.0f )( random );
            bird.Size = std::uniform_real_distribution<float>( 0.85f, 1.15f )( random );
            bird.Position = flock.Centre;
            bird.bAlive = true;
            birds.push_back( bird );
        }
    }
}

void Birds::tick( float deltaTime, float rain, float sun, const SurfaceFunction &surface ) {
    if ( !bLoaded || deltaTime <= 0.0f ) {
        return;
    }
    time += deltaTime;
    rainAmount = Saturate( rain );
    sunBrightness = 0.35f + 0.65f * Saturate( sun );
    // No drawn frame yet: the look direction is not known, so no flock is
    // placed (it would be placed relative to a guess).
    if ( !bHasCameraPose ) {
        return;
    }
    // Heavy rain grounds the flocks (the birds are a clear sky detail; the
    // sprites would be the only thing flying in a storm) [INF].
    const bool bGroundThem = rainAmount > 0.55f;
    if ( flocks.size() < static_cast<size_t>( kFlocksAround ) ) {
        flocks.resize( kFlocksAround );
    }
    bool bRebuild = false;
    for ( Flock &flock : flocks ) {
        const Vector3f delta = flock.Centre - cameraPosition;
        if ( flock.bAlive && ( bGroundThem || delta.dot( delta ) > kFlockRange * kFlockRange ) ) {
            flock.bAlive = false;
            bRebuild = true;
        }
        if ( !flock.bAlive ) {
            if ( !bGroundThem ) {
                spawnFlock( flock, cameraPosition, cameraForward, surface, random );
                bRebuild = bRebuild || flock.bAlive;
            }
            continue;
        }
        // A wandering heading: the turn drifts slowly, like a flock circling a
        // thermal.
        flock.Turn = Clamp( flock.Turn + std::uniform_real_distribution<float>( -0.05f, 0.05f )( random ) * deltaTime,
                            -0.15f, 0.15f );
        const float heading = std::atan2( flock.Heading.x, flock.Heading.z ) + flock.Turn * deltaTime;
        flock.Heading = Vector3f( std::sin( heading ), 0.0f, std::cos( heading ) );
        flock.Centre = flock.Centre + flock.Heading * ( flock.Speed * deltaTime );
        // Stay above the terrain (a flock crossing a hill climbs over it).
        flock.GroundTimer -= deltaTime;
        if ( flock.GroundTimer <= 0.0f ) {
            flock.GroundTimer = 2.0f;
            float ground = 0.0f;
            // The ray reaches 60 m down: start a little above the flock.
            if ( surface( flock.Centre.x, flock.Centre.z, flock.Centre.y + 20.0f, ground ) ) {
                flock.Ground = ground;
            }
        }
        const float target = flock.Ground + flock.Altitude;
        flock.Centre.y += ( target - flock.Centre.y ) * Clamp( deltaTime * 0.8f, 0.0f, 1.0f );
        if ( flock.Centre.y < flock.Ground + 12.0f ) {
            flock.Centre.y = flock.Ground + 12.0f;
        }
    }
    if ( bRebuild ) {
        rebuildBirds();
    }
    for ( Bird &bird : birds ) {
        if ( bird.Flock < 0 || static_cast<size_t>( bird.Flock ) >= flocks.size() ) {
            bird.bAlive = false;
            continue;
        }
        const Flock &flock = flocks[static_cast<size_t>( bird.Flock )];
        bird.bAlive = flock.bAlive;
        if ( !bird.bAlive ) {
            continue;
        }
        bird.Angle += bird.Orbit * deltaTime;
        const float ca = std::cos( bird.Angle ), sa = std::sin( bird.Angle );
        const Vector3f orbit( bird.Offset.x * ca - bird.Offset.z * sa, bird.Offset.y,
                              bird.Offset.x * sa + bird.Offset.z * ca );
        // A slow bob, so a flock is not a rigid formation.
        const float bob = std::sin( time * 1.7f + bird.Offset.x * 0.3f ) * 0.8f;
        bird.Position = flock.Centre + orbit + Vector3f( 0.0f, bob, 0.0f );
        bird.Flap += bird.FlapRate * deltaTime;
        if ( bird.Flap > 2.0f * kPi ) {
            bird.Flap -= 2.0f * kPi;
        }
    }
}

void Birds::draw( RenderDevice &device, const Matrix4x4f &viewProjection, const EdenSceneParams &sceneParams ) {
    drawnCount = 0;
    if ( !bLoaded ) {
        return;
    }
    // Camera basis for the billboards: the rows of the view matrix's rotation
    // (Matrix4x4f is column major with M * v, so the axes are its rows).
    const Matrix4x4f &view = sceneParams.View;
    Vector3f right( view.m[0][0], view.m[1][0], view.m[2][0] );
    Vector3f up( view.m[0][1], view.m[1][1], view.m[2][1] );
    if ( right.length() < 1e-4f || up.length() < 1e-4f ) {
        right = Vector3f( 1.0f, 0.0f, 0.0f );
        up = Vector3f( 0.0f, 1.0f, 0.0f );
    }
    right = right.normalized();
    up = up.normalized();
    const float fadeStart = kDrawRadius * 0.72f;
    // The pose the next tick places the flocks around. The rows of the view
    // matrix are the only reliable look direction (params.CameraDirection and
    // the quaternion helper both disagree with the drawn view). Recorded even
    // before the first flock exists: this is what lets tick() place one.
    cameraPosition = sceneParams.CameraPosition;
    cameraForward = -Vector3f( view.m[0][2], view.m[1][2], view.m[2][2] );
    cameraForward.y = 0.0f;
    bHasCameraPose = cameraForward.length() > 0.1f;
    if ( bHasCameraPose ) {
        cameraForward = cameraForward.normalized();
    }
    if ( birds.empty() ) {
        return;
    }
    if ( device.getCurrentRenderPass() == VK_NULL_HANDLE || !initializePipeline( device ) ) {
        return;
    }

    for ( auto &batch : vertexScratch ) {
        batch.clear();
    }
    for ( const Bird &bird : birds ) {
        if ( !bird.bAlive || bird.Flock < 0 || static_cast<size_t>( bird.Flock ) >= flocks.size() ) {
            continue;
        }
        const Flock &flock = flocks[static_cast<size_t>( bird.Flock )];
        const Species &speciesInfo = species[static_cast<size_t>( Min( flock.Species,
                                                                      static_cast<int32_t>( species.size() ) - 1 ) )];
        const Vector3f delta = bird.Position - cameraPosition;
        const float distance = delta.length();
        if ( distance > kDrawRadius ) {
            continue;
        }
        // Fade the far ones out instead of popping, and the very close ones
        // slightly (a 1.35 m seagull three metres from the lens is a wall).
        float alpha = 1.0f - Saturate( ( distance - fadeStart ) / Max( kDrawRadius - fadeStart, 1.0f ) );
        alpha *= Saturate( ( distance - 2.0f ) / 6.0f );
        if ( alpha <= 0.01f ) {
            continue;
        }
        const float half = 0.5f * speciesInfo.Wingspan * bird.Size;
        // Wings up mid-flap: the sprite is narrow, wings out: full span.
        const float flap = 0.45f + 0.55f * std::fabs( std::cos( bird.Flap ) );
        const Vector3f x = right * ( half * flap );
        const Vector3f y = up * half;
        const Vector3f &position = bird.Position;
        const uint32_t color = PackColor( Vector4f( 1.0f, 1.0f, 1.0f, alpha ) );
        const Vector3f corners[4] = { position - x - y, position + x - y, position + x + y, position - x + y };
        const float u[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
        const float v[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
        std::vector<BirdVertex> &batch_ = vertexScratch[static_cast<size_t>( flock.Species )];
        for ( int32_t corner : { 0, 1, 2, 0, 2, 3 } ) {
            batch_.push_back( BirdVertex{ corners[corner].x, corners[corner].y, corners[corner].z, u[corner], v[corner],
                                          color } );
        }
        drawnCount++;
    }
    if ( drawnCount == 0 ) {
        return;
    }
    VkCommandBuffer commandBuffer = device.getCommandBuffer();
    VkPipelineLayout layout = device.getDefaultPipelineLayout();
    BirdPushConstants push = {};
    memcpy( push.ViewProjection, &viewProjection.m[0][0], sizeof( push.ViewProjection ) );
    push.Params[0] = sunBrightness;
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
    vkCmdPushConstants( commandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ),
                        &push );
    const VkSampler sampler = device.getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Clamp );
    for ( size_t s = 0; s < species.size(); s++ ) {
        std::vector<BirdVertex> &batch_ = vertexScratch[s];
        if ( batch_.empty() || species[s].pTexture == nullptr ) {
            continue;
        }
        const VkDeviceSize bytes = static_cast<VkDeviceSize>( batch_.size() ) * sizeof( BirdVertex );
        TransientAllocation allocation = device.allocateTransient( bytes, 16 );
        if ( allocation.pData == nullptr ) {
            continue;
        }
        memcpy( allocation.pData, batch_.data(), static_cast<size_t>( bytes ) );
        GPUTexture *pTexture = species[s].pTexture;
        VkDescriptorSet set = device.allocateTextureSet( &pTexture, 1, sampler );
        if ( set == VK_NULL_HANDLE ) {
            continue;
        }
        vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr );
        vkCmdBindVertexBuffers( commandBuffer, 0, 1, &allocation.Buffer, &allocation.Offset );
        vkCmdDraw( commandBuffer, static_cast<uint32_t>( batch_.size() ), 1, 0, 0 );
    }
}

void Birds::logStats( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    logTimer += deltaTime;
    if ( logTimer < 5.0f ) {
        return;
    }
    logTimer = 0.0f;
    if ( drawnCount == loggedDrawn && getNumFlocks() == loggedFlocks ) {
        return;
    }
    loggedDrawn = drawnCount;
    loggedFlocks = getNumFlocks();
    OTDU_LOG_INFO( "Birds: %d drawn of %zu, %d flocks, rain %.2f, sun %.2f\n", drawnCount, birds.size(),
                   getNumFlocks(), rainAmount, sunBrightness );
}
