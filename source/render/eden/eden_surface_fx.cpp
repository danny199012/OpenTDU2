#include "shared.h"
#include "eden_surface_fx.h"

#include "system/cmd_line.h"
#include "render/eden/eden_headlights.h"
#include "render/eden/eden_renderer.h"
#include "render/shaders/builtin/brakedisc_frag.h"
#include "render/shaders/builtin/lampglow_frag.h"
#include "render/shaders/builtin/surfacedecal_frag.h"
#include "render/shaders/builtin/surfacefx_frag.h"
#include "render/shaders/builtin/surfacefx_vert.h"

namespace {
// Scalar lerp for the rain impact sampling.
inline float Lerp1( float a, float b, float t ) {
    return a + ( b - a ) * t;
}

// The push constants of both shaders (resources/shaders/surfacefx.vert): the
// scene view-projection and the per-draw parameters.
struct FxPushConstants {
    float ViewProjection[16];
    float Params[4];
};
static_assert( sizeof( FxPushConstants ) <= RenderDevice::kPushConstantsSize,
               "surface effects push constants exceed the device limit" );

// Brake disc heat (docs/re/surface_effects_spec.md 7) [INF]: the original's
// discs glow after hard braking; the port keeps a temperature per wheel.
//   heat in:  the pad torque the controller applies x the disc's angular
//             velocity (v / tyre radius), i.e. the power the brakes actually
//             put into the disc;
//   heat out: Newton cooling whose rate grows with the temperature (the
//             radiation) and with the air flow (a parked car cools slower).
// The capacities are a real pair of discs (a 380 mm vented front disc, a
// 330 mm rear one: about 700 / 300 J/K), so one hard stop from 100 km/h lights
// the front disc and warms the rear one, and a second stop lights both.
constexpr float kBrakeAmbient = 35.0f;          // C
constexpr float kBrakeCapacityFront = 700.0f;   // J/K
constexpr float kBrakeCapacityRear = 300.0f;    // J/K
constexpr float kBrakeGlowStart = 260.0f;       // C, the first visible red
constexpr float kBrakeGlowFull = 780.0f;        // C, bright orange
constexpr float kBrakeMax = 1050.0f;            // C, the pads would fade
constexpr float kBrakeCoolBase = 0.030f;        // 1/s at ambient
constexpr float kBrakeCoolRadiation = 0.00007f; // 1/(s C), the T^4 term [INF]

// Deterministic pseudo random numbers: a fixed seed keeps two headless runs of
// the same drive comparable (the screenshot A/B needs that).
uint32_t gRandomState = 0x1234abcdu;

inline float NextRandom() {
    gRandomState ^= gRandomState << 13;
    gRandomState ^= gRandomState >> 17;
    gRandomState ^= gRandomState << 5;
    return static_cast<float>( gRandomState & 0xffffff ) / static_cast<float>( 0xffffff );
}

inline float NextRandom( float minValue, float maxValue ) {
    return minValue + ( maxValue - minValue ) * NextRandom();
}

inline uint32_t PackColor( const Vector4f &color ) {
    const uint32_t r = static_cast<uint32_t>( Saturate( color.x ) * 255.0f + 0.5f );
    const uint32_t g = static_cast<uint32_t>( Saturate( color.y ) * 255.0f + 0.5f );
    const uint32_t b = static_cast<uint32_t>( Saturate( color.z ) * 255.0f + 0.5f );
    const uint32_t a = static_cast<uint32_t>( Saturate( color.w ) * 255.0f + 0.5f );
    return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 );
}

// One orthonormal pair of tangents around a surface normal, the first one as
// close to `preferred` as the surface allows.
void BuildTangents( const Vector3f &normal, const Vector3f &preferred, Vector3f &outTangent,
                    Vector3f &outBitangent ) {
    const Vector3f n = normal.length() > 1e-4f ? normal.normalized() : Vector3f( 0.0f, 1.0f, 0.0f );
    Vector3f tangent = preferred - n * n.dot( preferred );
    if ( tangent.length() < 1e-3f ) {
        tangent = Vector3f( 1.0f, 0.0f, 0.0f ) - n * n.x;
        if ( tangent.length() < 1e-3f ) {
            tangent = Vector3f( 0.0f, 0.0f, 1.0f ) - n * n.z;
        }
    }
    outTangent = tangent.normalized();
    outBitangent = n.cross( outTangent ).normalized();
}

// (The disc colour is brakedisc.frag's: a blackbody ramp over a temperature
// profile across the rotor; the vertices carry the glow only.)
} // namespace

const char *GroundMaterialName( eGroundMaterial material ) {
    switch ( material ) {
    case eGroundMaterial::Tarmac:
        return "tarmac";
    case eGroundMaterial::Dirt:
        return "dirt";
    case eGroundMaterial::Sand:
        return "sand";
    case eGroundMaterial::Grass:
        return "grass";
    case eGroundMaterial::Snow:
        return "snow";
    default:
        return "?";
    }
}

void GroundWetness::update( float deltaTime, float rainIntensity, float sunEnergy ) {
    if ( deltaTime <= 0.0f ) {
        return;
    }
    const float rain = Saturate( rainIntensity );
    const float sun = Saturate( sunEnergy );
    // Soak rate: how fast a material takes the water in. Dirt is the slowest
    // (the water has to reach the soil), tarmac only holds a film.
    static const float kSoakRate[static_cast<size_t>( eGroundMaterial::Count )] = {
        0.9f,  // tarmac
        0.35f, // dirt
        0.7f,  // sand
        0.45f, // grass
        0.5f,  // snow (melt)
    };
    // Dry rate: how fast the sun takes it back out. Mud keeps its water for
    // about three minutes in full sun, a road dries in half a minute.
    static const float kDryRate[static_cast<size_t>( eGroundMaterial::Count )] = {
        0.030f, // tarmac
        0.006f, // dirt
        0.020f, // sand
        0.012f, // grass
        0.004f, // snow
    };
    for ( size_t i = 0; i < static_cast<size_t>( eGroundMaterial::Count ); i++ ) {
        float value = Value[i];
        // Rain pushes the wetness towards 1 at the material's own rate.
        value += ( 1.0f - value ) * kSoakRate[i] * rain * deltaTime;
        // The sun (and a dry sky) pulls it back to 0.
        value -= value * kDryRate[i] * ( 0.25f + 2.0f * sun ) * deltaTime;
        Value[i] = Saturate( value );
    }
}


namespace {
// Spray / dust / track per ground material. The colours are the port's reading
// of the terrain textures (the beach plage.2db, the D3_SAND detail, the dirt
// and grass colour maps of the sectors): dry sand is pale and dusty, wet sand
// and mud are dark and sticky, wet tarmac throws a grey spray.
const EdenSurfaceFx::MaterialProfile kProfiles[static_cast<size_t>( eGroundMaterial::Count )] = {
    // Tarmac: nothing on a dry road, a grey spray on a wet one, black skid
    // marks when the tyre slips.
    { Vector4f( 0.70f, 0.74f, 0.78f, 0.45f ), Vector4f( 0.62f, 0.67f, 0.72f, 0.55f ),
      Vector4f( 0.10f, 0.10f, 0.11f, 0.55f ),
      1.2f, 0.6f, 0.9f, 0.10f, 0.10f, 0.55f, 0.12f, 12.0f, 0.35f, 2.5f },
    // Dirt: a dust cloud when dry, mud thrown back when the rain soaked it.
    { Vector4f( 0.52f, 0.44f, 0.33f, 0.50f ), Vector4f( 0.26f, 0.20f, 0.14f, 0.80f ),
      Vector4f( 0.20f, 0.16f, 0.12f, 0.70f ),
      4.0f, 1.2f, 1.3f, 0.30f, 0.14f, 0.85f, 1.8f, 25.0f, 0.85f, 1.8f },
    // Sand: the pale dust of the beach, a damp dark track at the water's edge.
    { Vector4f( 0.80f, 0.72f, 0.52f, 0.55f ), Vector4f( 0.50f, 0.44f, 0.32f, 0.75f ),
      Vector4f( 0.44f, 0.38f, 0.27f, 0.65f ),
      5.5f, 1.0f, 1.5f, 0.35f, 0.13f, 0.75f, 1.8f, 30.0f, 0.15f, 1.4f },
    // Grass: a light spray of cut grass and soil.
    { Vector4f( 0.30f, 0.38f, 0.20f, 0.45f ), Vector4f( 0.20f, 0.26f, 0.14f, 0.60f ),
      Vector4f( 0.18f, 0.24f, 0.12f, 0.45f ),
      2.5f, 0.8f, 1.1f, 0.25f, 0.12f, 0.60f, 1.2f, 18.0f, 0.30f, 1.0f },
    // Snow: powder, and the deep blue grey of a pressed track.
    { Vector4f( 0.90f, 0.93f, 0.98f, 0.65f ), Vector4f( 0.72f, 0.76f, 0.84f, 0.75f ),
      Vector4f( 0.52f, 0.56f, 0.64f, 0.55f ),
      3.0f, 0.6f, 1.1f, 0.20f, 0.15f, 0.90f, 1.6f, 40.0f, 1.0f, 0.6f },
};
} // namespace

EdenSurfaceFx::EdenSurfaceFx() = default;

EdenSurfaceFx::~EdenSurfaceFx() { destroy(); }

bool EdenSurfaceFx::initialize( RenderDevice *pDevice ) {
    if ( pDevice == nullptr ) {
        return false;
    }
    this->pDevice = pDevice;
    // The pipelines themselves are created on the first draw, against the
    // render pass they will draw in (draw() -> passPipelines).
    bReady = true;

    particles.assign( kMaxParticles, Particle() );
    decals.assign( kMaxDecals, Decal() );
    puddles.assign( kMaxPuddles, Puddle() );
    vertexScratch.reserve( kMaxParticles * 6 );
    decalScratch[0].reserve( kMaxDecals * 6 );
    decalScratch[1].reserve( kMaxDecals * 6 );
    discScratch.reserve( kMaxWheels * 6 );
    haloScratch.reserve( kMaxWheels * 6 );
    numAliveParticles = 0;
    numAliveDecals = 0;
    nextParticle = 0;
    nextDecal = 0;
    for ( uint32_t i = 0; i < kMaxWheels; i++ ) {
        wheelEmission[i] = 0.0f;
        bWheelDecalAnchor[i] = false;
        brakeDiscs[i] = BrakeDisc();
    }
    OTDU_LOG_INFO( "EdenSurfaceFx: %s (%u particles, %u decals)\n", bReady ? "ready" : "FAILED", kMaxParticles,
                   kMaxDecals );
    return bReady;
}

void EdenSurfaceFx::destroy() {
    if ( pDevice != nullptr ) {
        for ( auto &entry : passPipelines ) {
            if ( entry.second.particle != VK_NULL_HANDLE ) {
                pDevice->destroyPipeline( entry.second.particle );
            }
            if ( entry.second.decal != VK_NULL_HANDLE ) {
                pDevice->destroyPipeline( entry.second.decal );
            }
            if ( entry.second.brakeGlow != VK_NULL_HANDLE ) {
                pDevice->destroyPipeline( entry.second.brakeGlow );
            }
            if ( entry.second.lampGlow != VK_NULL_HANDLE ) {
                pDevice->destroyPipeline( entry.second.lampGlow );
            }
        }
    }
    passPipelines.clear();
    particles.clear();
    decals.clear();
    puddles.clear();
    vertexScratch.clear();
    decalScratch[0].clear();
    decalScratch[1].clear();
    discScratch.clear();
    haloScratch.clear();
    numAliveParticles = 0;
    numAliveDecals = 0;
    numAlivePuddles = 0;
    bReady = false;
    pDevice = nullptr;
}

void EdenSurfaceFx::clear() {
    for ( Particle &particle : particles ) {
        particle.bAlive = false;
    }
    for ( Decal &decal : decals ) {
        decal.bAlive = false;
    }
    numAliveParticles = 0;
    numAliveDecals = 0;
    for ( Puddle &puddle : puddles ) {
        puddle.bAlive = false;
    }
    numAlivePuddles = 0;
    for ( uint32_t i = 0; i < kMaxWheels; i++ ) {
        wheelEmission[i] = 0.0f;
        bWheelDecalAnchor[i] = false;
        // A new car's discs are cold (a teleport, a bought car).
        brakeDiscs[i].Temperature = kBrakeAmbient;
        brakeDiscs[i].Glow = 0.0f;
    }
    loggedBrakeHeat = -1000.0f;
}

void EdenSurfaceFx::seedWetness( float value ) {
    const float seeded = Saturate( value );
    for ( size_t i = 0; i < static_cast<size_t>( eGroundMaterial::Count ); i++ ) {
        wetness.Value[i] = seeded;
    }
    loggedWetness = -1.0f; // force the next -logsurfacefx line
}

void EdenSurfaceFx::seedBrakeHeat( float celsius ) {
    const float seeded = Clamp( celsius, kBrakeAmbient, kBrakeMax );
    for ( uint32_t i = 0; i < kMaxWheels; i++ ) {
        brakeDiscs[i].Temperature = seeded;
        brakeDiscs[i].Glow = Saturate( ( seeded - kBrakeGlowStart ) / ( kBrakeGlowFull - kBrakeGlowStart ) );
    }
    loggedBrakeHeat = -1000.0f; // force the next -logbrakeheat line
}

void EdenSurfaceFx::update( float deltaTime, float rainIntensity, float sunEnergy ) {
    if ( deltaTime <= 0.0f ) {
        return;
    }
    sceneTime += deltaTime;
    wetness.update( deltaTime, rainIntensity, sunEnergy );
    numAliveParticles = 0;
    for ( Particle &particle : particles ) {
        if ( !particle.bAlive ) {
            continue;
        }
        particle.Life += deltaTime;
        if ( particle.Life >= particle.MaxLife ) {
            particle.bAlive = false;
            continue;
        }
        // Gravity and drag. Nothing stops a puff at the ground: one that lands
        // simply sinks behind the terrain in the depth test.
        particle.Velocity.y += particle.Gravity * deltaTime;
        particle.Velocity = particle.Velocity * Max( 0.0f, 1.0f - particle.Drag * deltaTime );
        particle.Position += particle.Velocity * deltaTime;
        numAliveParticles++;
    }
    numAliveDecals = 0;
    for ( Decal &decal : decals ) {
        if ( !decal.bAlive ) {
            continue;
        }
        decal.Age += deltaTime;
        if ( decal.Age >= decal.Life ) {
            decal.bAlive = false;
            continue;
        }
        numAliveDecals++;
    }
}

void EdenSurfaceFx::emitWheel( uint32_t wheelIndex, const WheelContact &contact, float deltaTime ) {
    if ( !bReady || !bEnabled || wheelIndex >= kMaxWheels || deltaTime <= 0.0f ) {
        return;
    }
    // The disc heats and cools whatever the wheel touches: a locked wheel in the
    // air still has its pads on, and the discs cool all the time.
    updateBrakeDisc( wheelIndex, contact, deltaTime );
    if ( !contact.bContact ) {
        // A wheel in the air leaves nothing behind: the next touch lays a fresh
        // track instead of drawing a line through the jump.
        bWheelDecalAnchor[wheelIndex] = false;
        wheelEmission[wheelIndex] = 0.0f;
        return;
    }
    const size_t materialIndex = static_cast<size_t>( contact.Material );
    if ( materialIndex >= static_cast<size_t>( eGroundMaterial::Count ) ) {
        return;
    }
    const MaterialProfile &profile = kProfiles[materialIndex];
    lastMaterial = contact.Material;
    bHasEmission = true;
    const float wet = wetness.get( contact.Material );
    const Vector3f travel( contact.Velocity.x, 0.0f, contact.Velocity.z );
    const float speed = travel.length();
    const float slip = Saturate( contact.Slip );
    // A dry road throws nothing; every material has its own threshold.
    static const float kMinSpeed[static_cast<size_t>( eGroundMaterial::Count )] = { 4.0f, 1.0f, 1.0f, 1.5f,
                                                                                  1.0f };
    float rate = profile.SplashRate * Max( speed - kMinSpeed[materialIndex], 0.0f );
    rate *= 1.0f + slip * profile.SlipBoost;
    rate *= 1.0f + wet * profile.WetnessBoost;
    if ( contact.Material == eGroundMaterial::Tarmac ) {
        // The road only sprays when it is wet (the rain states of the weather).
        rate *= wet;
    }
    rate *= emissionScale;
    if ( rate > 0.0f ) {
        wheelEmission[wheelIndex] += rate * deltaTime;
        // A bounded burst per frame: a teleport or a frame spike must not empty
        // the pool in one call.
        const uint32_t numSpawn = Min( static_cast<uint32_t>( wheelEmission[wheelIndex] ), 24u );
        wheelEmission[wheelIndex] -= static_cast<float>( numSpawn );
        const Vector3f normal =
            contact.Normal.length() > 1e-4f ? contact.Normal.normalized() : Vector3f( 0.0f, 1.0f, 0.0f );
        Vector3f tangent, bitangent;
        BuildTangents( normal, travel.length() > 1e-3f ? travel : Vector3f( 0.0f, 0.0f, 1.0f ), tangent, bitangent );
        // Dry and soaked ground throw different material: the splash colour is
        // the mix of the two by the wetness of the surface.
        const Vector4f color( Lerp( profile.SplashColor.xyz(), profile.WetColor.xyz(), wet ),
                              profile.SplashColor.w + ( profile.WetColor.w - profile.SplashColor.w ) * wet );
        for ( uint32_t i = 0; i < numSpawn; i++ ) {
            Particle particle;
            particle.Position = contact.Position + tangent * NextRandom( -0.12f, 0.12f ) +
                                bitangent * NextRandom( -0.12f, 0.12f ) + normal * NextRandom( 0.02f, 0.10f );
            // Thrown up and back from the wheel, with the wheel's own travel.
            const Vector3f back = travel.length() > 1e-3f ? travel * ( -profile.BackSpeed ) : Vector3f();
            particle.Velocity = back + normal * ( profile.UpSpeed * NextRandom( 0.6f, 1.4f ) ) +
                                tangent * NextRandom( -0.5f, 0.5f ) + bitangent * NextRandom( -0.5f, 0.5f );
            particle.Color = color;
            particle.Size = profile.Size * NextRandom( 0.6f, 1.5f );
            particle.MaxLife = profile.Life * NextRandom( 0.7f, 1.3f );
            particle.Gravity = -9.81f;
            particle.Drag = 1.1f;
            spawnParticle( particle );
        }
    }


    // Tyre track: one decal every ~0.6 m the wheel rolls. A dry road keeps its
    // marks only when the tyre slips or is braked hard (a skid); sand, mud and
    // snow always take an imprint.
    const float brake = Saturate( contact.BrakeSlip );
    const float skid = Max( slip, brake );
    float decalRate = profile.DecalRate * ( 1.0f + slip * 3.0f ) * emissionScale;
    if ( contact.Material == eGroundMaterial::Tarmac ) {
        decalRate *= Max( wet, skid > 0.35f ? 1.0f : 0.0f );
        // A hard stop lays a continuous dark line: the road profile's splash
        // rate is the wrong budget for it (it is a per-metre spray rate), so
        // the mark gets its own, closer stamp.
        if ( skid > 0.35f ) {
            decalRate = Max( decalRate, 3.0f * skid * emissionScale );
        }
    }
    if ( decalRate <= 0.0f ) {
        bWheelDecalAnchor[wheelIndex] = false;
        return;
    }
    const float spacing = 1.0f / Max( decalRate, 0.05f );
    if ( !bWheelDecalAnchor[wheelIndex] ) {
        wheelDecalAnchor[wheelIndex] = contact.Position;
        bWheelDecalAnchor[wheelIndex] = true;
        return;
    }
    const Vector3f delta = contact.Position - wheelDecalAnchor[wheelIndex];
    const Vector3f flat( delta.x, 0.0f, delta.z );
    if ( flat.length() < spacing ) {
        return;
    }
    wheelDecalAnchor[wheelIndex] = contact.Position;
    Decal decal;
    decal.Position = contact.Position;
    decal.Normal = contact.Normal.length() > 1e-4f ? contact.Normal.normalized() : Vector3f( 0.0f, 1.0f, 0.0f );
    decal.Tangent = flat.length() > 1e-3f ? flat.normalized() : Vector3f( 0.0f, 0.0f, 1.0f );
    decal.Color = profile.DecalColor;
    if ( contact.Material == eGroundMaterial::Tarmac && brake > 0.0f ) {
        // A tyre dragged at the limit puts rubber down: darker than the damp
        // road film the same profile lays.
        decal.Color.w = Max( decal.Color.w, 0.80f * brake );
    }
    decal.HalfWidth = 0.12f;
    decal.HalfLength = Min( spacing * 0.75f, 0.55f );
    decal.Tread = profile.Tread;
    decal.Life = profile.DecalLife;
    spawnDecal( decal );
}

void EdenSurfaceFx::updateBrakeDisc( uint32_t wheelIndex, const WheelContact &contact, float deltaTime ) {
    BrakeDisc &disc = brakeDiscs[wheelIndex];
    // The pose comes every frame, even with the wheel in the air: the disc is
    // drawn wherever the hub is.
    disc.bValid = contact.DiscRadius > 0.0f;
    if ( disc.bValid ) {
        disc.Position = contact.DiscCenter;
        disc.Axis = contact.DiscAxis.length() > 1e-4f ? contact.DiscAxis.normalized() : Vector3f( 1.0f, 0.0f, 0.0f );
        disc.Up = contact.DiscUp.length() > 1e-4f ? contact.DiscUp.normalized() : Vector3f( 0.0f, 1.0f, 0.0f );
        disc.Radius = contact.DiscRadius;
    }
    // Heat in: the pad torque x the disc's angular velocity. The wheel rolls at
    // v / R, so the brake power is the force the pads apply at the tyre radius
    // times the road speed - the energy the car actually loses to the brakes.
    const float speed = contact.Velocity.length();
    const float omega = speed / Max( contact.WheelRadius, 0.1f );
    const float power = Max( contact.BrakeTorque, 0.0f ) * omega; // W
    const float capacity = contact.bFront ? kBrakeCapacityFront : kBrakeCapacityRear;
    disc.Temperature += power * deltaTime / capacity;
    // Heat out: Newton cooling with a rate that grows with the temperature (the
    // radiation) and with the air flow (a parked car cools slower).
    const float above = disc.Temperature - kBrakeAmbient;
    if ( above > 0.0f ) {
        const float flow = 0.55f + 0.45f * Saturate( speed / 30.0f );
        const float rate = kBrakeCoolBase + kBrakeCoolRadiation * above;
        disc.Temperature -= above * Min( 1.0f, rate * flow * deltaTime );
    }
    disc.Temperature = Min( disc.Temperature, kBrakeMax );
    disc.Glow = Saturate( ( disc.Temperature - kBrakeGlowStart ) / ( kBrakeGlowFull - kBrakeGlowStart ) );
}

void EdenSurfaceFx::spawnParticle( const Particle &particle ) {
    if ( particles.empty() ) {
        return;
    }
    Particle &slot = particles[nextParticle];
    const bool bWasAlive = slot.bAlive;
    slot = particle;
    slot.bAlive = true;
    nextParticle = ( nextParticle + 1 ) % kMaxParticles;
    if ( !bWasAlive ) {
        numAliveParticles++;
    }
}

void EdenSurfaceFx::spawnDecal( const Decal &decal ) {
    if ( decals.empty() ) {
        return;
    }
    Decal &slot = decals[nextDecal];
    const bool bWasAlive = slot.bAlive;
    slot = decal;
    slot.bAlive = true;
    nextDecal = ( nextDecal + 1 ) % kMaxDecals;
    if ( !bWasAlive ) {
        numAliveDecals++;
    }
}

void EdenSurfaceFx::appendQuad( std::vector<FxVertex> &vertices, const Vector3f corners[4], uint32_t packed ) const {
    static const float kU[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    static const float kV[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    static const uint32_t kOrder[6] = { 0, 1, 2, 0, 2, 3 };
    for ( uint32_t i = 0; i < 6; i++ ) {
        const uint32_t corner = kOrder[i];
        vertices.push_back(
            { corners[corner].x, corners[corner].y, corners[corner].z, kU[corner], kV[corner], packed } );
    }
}

void EdenSurfaceFx::appendBillboard( std::vector<FxVertex> &vertices, const Vector3f &position, const Vector3f &right,
                                     const Vector3f &up, float size, const Vector4f &color ) const {
    const Vector3f r = right * size;
    const Vector3f u = up * size;
    const Vector3f corners[4] = { position - r - u, position + r - u, position + r + u, position - r + u };
    appendQuad( vertices, corners, PackColor( color ) );
}

void EdenSurfaceFx::appendDiscQuad( std::vector<FxVertex> &vertices, const BrakeDisc &disc, float radius, float outboard,
                                    const Vector4f &color ) const {
    // In the wheel plane, a hair outboard of the hub: the disc is a flat plate
    // between the pads, and the spokes in front of it must win the depth test.
    const Vector3f center = disc.Position + disc.Axis * outboard;
    Vector3f up = disc.Up;
    Vector3f right = disc.Axis.cross( up );
    if ( right.length() < 1e-4f || up.length() < 1e-4f ) {
        right = Vector3f( 1.0f, 0.0f, 0.0f );
        up = Vector3f( 0.0f, 1.0f, 0.0f );
    }
    right = right.normalized() * radius;
    up = up.normalized() * radius;
    const Vector3f corners[4] = { center - right - up, center + right - up, center + right + up, center - right + up };
    appendQuad( vertices, corners, PackColor( color ) );
}

void EdenSurfaceFx::appendGroundQuad( std::vector<FxVertex> &vertices, const Decal &decal ) const {
    Vector3f tangent, bitangent;
    BuildTangents( decal.Normal, decal.Tangent, tangent, bitangent );
    // Lifted off the surface by a couple of centimetres: the depth test is
    // LESS_OR_EQUAL and the terrain is drawn before the decals, so the quad has
    // to win against the triangle it lies on.
    const Vector3f center = decal.Position + decal.Normal * 0.035f;
    const Vector3f t = tangent * decal.HalfLength;
    const Vector3f b = bitangent * decal.HalfWidth;
    const Vector3f corners[4] = { center - b - t, center + b - t, center + b + t, center - b + t };
    appendQuad( vertices, corners, PackColor( decal.Color ) );
}


void EdenSurfaceFx::draw( RenderDevice &device, const Matrix4x4f &viewProjection,
                          const EdenSceneParams &sceneParams ) {
    if ( !bReady || !bEnabled ) {
        return;
    }
    bool bAnyDiscGlow = false;
    if ( bBrakeGlow ) {
        for ( const BrakeDisc &disc : brakeDiscs ) {
            bAnyDiscGlow = bAnyDiscGlow || ( disc.bValid && disc.Glow > 0.01f );
        }
    }
    if ( numAliveParticles + numAliveDecals + numAlivePuddles == 0 && EdenHeadlightFrameCones().empty() &&
         EdenRearLightFrameCones().empty() && !bAnyDiscGlow && EdenLampGlowFrame().empty() ) {
        return;
    }
    if ( device.getCurrentRenderPass() == VK_NULL_HANDLE ) {
        return;
    }
    // Created here, against the pass actually current: a pipeline built for
    // the swapchain pass (initialize() runs before any scene pass) drew the
    // whole session in an incompatible offscreen pass.
    const VkRenderPass renderPass = device.getCurrentRenderPass();
    PassPipelines &pipelines = passPipelines[renderPass];
    if ( pipelines.particle == VK_NULL_HANDLE || pipelines.decal == VK_NULL_HANDLE ||
         pipelines.brakeGlow == VK_NULL_HANDLE || pipelines.lampGlow == VK_NULL_HANDLE ) {
        PipelineDesc desc;
        desc.pVertexSPIRV = kSPIRV_surfacefx_vert;
        desc.VertexSPIRVSize = sizeof( kSPIRV_surfacefx_vert );
        desc.pFragmentSPIRV = kSPIRV_surfacefx_frag;
        desc.FragmentSPIRVSize = sizeof( kSPIRV_surfacefx_frag );
        desc.Attributes = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof( FxVertex, X ) },
                            { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( FxVertex, U ) },
                            { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof( FxVertex, Color ) } };
        desc.BindingStrides = { sizeof( FxVertex ) };
        desc.Blend = eBlendMode::BM_Alpha;
        desc.Cull = eCullMode::CM_None;
        desc.bDepthTest = true;
        desc.bDepthWrite = false;
        desc.RenderPass = renderPass;
        pipelines.particle = pDevice->createPipeline( desc );
        desc.pFragmentSPIRV = kSPIRV_surfacedecal_frag;
        desc.FragmentSPIRVSize = sizeof( kSPIRV_surfacedecal_frag );
        pipelines.decal = pDevice->createPipeline( desc );
        // The brake disc glow: the same vertex layout. Blended, not additive: an
        // additive orange over the silver rim adds up past white, and the
        // original's discs read orange red (the colour replaces what is behind
        // it).
        // Premultiplied (brakedisc.frag): the colour covers the grey metal as
        // the disc heats and the hot core adds light on top of it.
        desc.pFragmentSPIRV = kSPIRV_brakedisc_frag;
        desc.FragmentSPIRVSize = sizeof( kSPIRV_brakedisc_frag );
        desc.Blend = eBlendMode::BM_PremultipliedAlpha;
        pipelines.brakeGlow = pDevice->createPipeline( desc );
        // The car lamp glows: added light (lampglow.frag).
        desc.pFragmentSPIRV = kSPIRV_lampglow_frag;
        desc.FragmentSPIRVSize = sizeof( kSPIRV_lampglow_frag );
        desc.Blend = eBlendMode::BM_Additive;
        pipelines.lampGlow = pDevice->createPipeline( desc );
        if ( pipelines.particle == VK_NULL_HANDLE || pipelines.decal == VK_NULL_HANDLE ||
             pipelines.brakeGlow == VK_NULL_HANDLE || pipelines.lampGlow == VK_NULL_HANDLE ) {
            OTDU_LOG_WARN( "EdenSurfaceFx: pipeline creation failed for the scene pass\n" );
            return;
        }
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
    const Vector3f cameraPosition = sceneParams.CameraPosition;
    // Beyond this nothing is visible anyway (the ring buffer recycles the rest).
    const float maxDistanceSq = 120.0f * 120.0f;

    VkCommandBuffer commandBuffer = device.getCommandBuffer();
    FxPushConstants push = {};
    memcpy( push.ViewProjection, &viewProjection.m[0][0], sizeof( push.ViewProjection ) );
    push.Params[0] = sceneTime;

    auto record = [&]( VkPipeline pipeline, const std::vector<FxVertex> &vertices ) {
        if ( vertices.empty() ) {
            return;
        }
        const VkDeviceSize bytes = static_cast<VkDeviceSize>( vertices.size() ) * sizeof( FxVertex );
        TransientAllocation allocation = device.allocateTransient( bytes, 16 );
        if ( allocation.pData == nullptr ) {
            return;
        }
        memcpy( allocation.pData, vertices.data(), static_cast<size_t>( bytes ) );
        vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
        vkCmdPushConstants( commandBuffer, device.getDefaultPipelineLayout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
        vkCmdBindVertexBuffers( commandBuffer, 0, 1, &allocation.Buffer, &allocation.Offset );
        vkCmdDraw( commandBuffer, static_cast<uint32_t>( vertices.size() ), 1, 0, 0 );
    };

    // The tracks first: they lie on the ground, the spray is in the air above.
    // The tread amount is a push constant, so the smooth (sand) and the treaded
    // (mud, snow) tracks are two batches.
    decalScratch[0].clear();
    decalScratch[1].clear();
    // The light pools re-generate every frame from the cone registries into
    // their own batch (the radial falloff push constant is 1 there — the
    // track falloff's straight shoulders and hash cells read as a square
    // stamp on a light quad).
    poolScratch.clear();
    updateLightPools();
    for ( const Decal &decal : decals ) {
        if ( !decal.bAlive ) {
            continue;
        }
        const Vector3f delta = decal.Position - cameraPosition;
        if ( delta.dot( delta ) > maxDistanceSq ) {
            continue;
        }
        // Fade the last third of the life out instead of popping.
        const float remaining = 1.0f - decal.Age / Max( decal.Life, 1e-3f );
        Decal faded = decal;
        faded.Color = Vector4f( decal.Color.xyz(), decal.Color.w * Saturate( remaining * 3.0f ) );
        appendGroundQuad( decalScratch[decal.Tread > 0.5f ? 1 : 0], faded );
    }
    // The terrain puddles ride the smooth batch (the same soft-shouldered
    // quad shader): dark translucent water, alpha following the wetness.
    if ( bPuddlesEnabled && numAlivePuddles != 0 ) {
        const float puddleWet = Saturate( ( wetness.get( eGroundMaterial::Tarmac ) - 0.25f ) / 0.75f );
        if ( puddleWet > 0.0f ) {
            const Vector4f water( 0.30f, 0.36f, 0.45f, puddleWet * puddleWet * 0.55f );
            for ( const Puddle &puddle : puddles ) {
                if ( !puddle.bAlive ) {
                    continue;
                }
                const Vector3f delta = puddle.Position - cameraPosition;
                if ( delta.dot( delta ) > maxDistanceSq ) {
                    continue;
                }
                const float s = puddle.Size;
                const Vector3f corners[4] = {
                    Vector3f( puddle.Position.x - s, puddle.Corner[0], puddle.Position.z - s ),
                    Vector3f( puddle.Position.x + s, puddle.Corner[1], puddle.Position.z - s ),
                    Vector3f( puddle.Position.x + s, puddle.Corner[2], puddle.Position.z + s ),
                    Vector3f( puddle.Position.x - s, puddle.Corner[3], puddle.Position.z + s ),
                };
                appendQuad( decalScratch[0], corners, PackColor( water ) );
            }
        }
    }
    // The pools first (under the tracks), with the light-pool falloff.
    push.Params[1] = 0.0f;
    push.Params[2] = 1.0f;
    record( pipelines.decal, poolScratch );
    push.Params[1] = 0.0f;
    push.Params[2] = 0.0f;
    record( pipelines.decal, decalScratch[0] );
    push.Params[1] = 1.0f;
    push.Params[2] = 0.0f;
    record( pipelines.decal, decalScratch[1] );

    // The spray / dust: one batch, camera facing.
    push.Params[1] = 0.0f;
    vertexScratch.clear();
    for ( const Particle &particle : particles ) {
        if ( !particle.bAlive ) {
            continue;
        }
        const Vector3f delta = particle.Position - cameraPosition;
        if ( delta.dot( delta ) > maxDistanceSq ) {
            continue;
        }
        // Grow a little as it ages and fade out over the life.
        const float age = Saturate( particle.Life / Max( particle.MaxLife, 1e-3f ) );
        const float alpha = particle.Color.w * ( 1.0f - age ) * ( 1.0f - age );
        const Vector4f color( particle.Color.xyz(), alpha );
        appendBillboard( vertexScratch, particle.Position, right, up, particle.Size * ( 0.7f + age * 0.9f ), color );
    }
    record( pipelines.particle, vertexScratch );

    // The brake discs (docs/re/surface_effects_spec.md 7): the hot face in the
    // wheel plane, then the halo that spills out of the rim. Both blended with
    // the disc's own orange, so the colour is the heat's, never the rim's.
    discScratch.clear();
    haloScratch.clear();
    if ( bBrakeGlow ) {
        for ( const BrakeDisc &disc : brakeDiscs ) {
            if ( !disc.bValid || disc.Glow <= 0.01f ) {
                continue;
            }
            const Vector3f delta = disc.Position - cameraPosition;
            if ( delta.dot( delta ) > maxDistanceSq ) {
                continue;
            }
            // brakedisc.frag works the colour out per pixel from the glow
            // (the vertex alpha); the rgb is unused.
            const Vector4f color( 1.0f, 1.0f, 1.0f, Saturate( disc.Glow ) );
            appendDiscQuad( discScratch, disc, disc.Radius, 0.025f, color );
            // The spill sits in the wheel plane, a couple of centimetres
            // outboard of the rotor face and well inboard of the spokes, so the
            // rim, the spokes and the caliper depth-cull it: it only shows
            // through the rim's openings, around the hot disc. The first cut
            // drew a 0.62 m billboard 0.9 x radius outboard of the hub - in
            // front of the whole wheel - and the flat orange washed the spokes,
            // the tyre and the caliper (the owner's close-up).
            const Vector3f haloCenter = disc.Position + disc.Axis * 0.020f;
            // The spill reaches a little past the rotor: the caliper and the
            // rim's inside take the light (added, never covered). In the
            // wheel plane, not camera facing: a facing quad seen at an angle
            // stands out in front of the spokes and tinted them orange.
            (void)haloCenter;
            appendDiscQuad( haloScratch, disc, disc.Radius * 1.2f, 0.020f, color );
        }
    }
    push.Params[1] = 0.0f;
    record( pipelines.brakeGlow, discScratch );
    push.Params[1] = 1.0f;
    record( pipelines.brakeGlow, haloScratch );

    // The car lamp glows (EdenLampGlowFrame, car_lights_spec.md 8): camera
    // facing, added. The strength falls off as the lamp turns away from the
    // camera (a lens shines along its axis), down to the lamp's Spread seen
    // edge on and to nothing from behind.
    lampScratch[0].clear();
    lampScratch[1].clear();
    static const bool bNoLampGlow = CmdLine::HasArg( "-nolampglow" );
    if ( !bNoLampGlow ) {
        for ( const EdenLampGlow &glow : EdenLampGlowFrame() ) {
            const Vector3f toCamera = cameraPosition - glow.Position;
            const float distance = toCamera.length();
            if ( distance < 0.05f || distance > 400.0f || glow.Strength <= 0.0f ) {
                continue;
            }
            const Vector3f view = toCamera * ( 1.0f / distance );
            const float facing = glow.Normal.dot( view );
            if ( facing <= -0.05f ) {
                continue;
            }
            const float axis = Saturate( facing );
            const float strength = glow.Strength * ( glow.Spread + ( 1.0f - glow.Spread ) * axis * axis );
            if ( strength <= 0.003f ) {
                continue;
            }
            // Pulled towards the camera so the lens / bodywork it sits on
            // does not cut the quad in half; a far lamp keeps a minimum
            // apparent size (a distant tail light still reads as a light).
            const Vector3f center = glow.Position + view * Min( 0.18f, distance * 0.25f );
            const float size = Max( glow.Size, distance * 0.006f );
            appendBillboard( lampScratch[glow.Core > 0.5f ? 1 : 0], center, right, up, size,
                             Vector4f( glow.Color, strength ) );
        }
    }
    push.Params[1] = 0.0f;
    record( pipelines.lampGlow, lampScratch[0] );
    push.Params[1] = 1.0f;
    record( pipelines.lampGlow, lampScratch[1] );
}


void EdenSurfaceFx::updateLightPools() {
    // The retail's visible headlight pool is the TD_ShadowBox caustics
    // projection (collected on the cars, renderer still skips it); until that
    // translates, the pools are port-native ground quads in this decal batch
    // (session 2026-11-02: the s15 G/B term is rain-permutation-only in the
    // real libraries - TD_Road ps_0 reads R and the A multiplier only).
    if ( !bEnabled || !heightQuery ) {
        static bool bWarned = false;
        if ( !bWarned ) {
            bWarned = true;
            OTDU_LOG_WARN( "SurfaceFx: light pools off (enabled %d, heightQuery %d)", bEnabled ? 1 : 0,
                           heightQuery ? 1 : 0 );
        }
        return;
    }
    struct PoolSource {
        const std::vector<EdenHeadlightCone> *pCones;
        Vector4f color; // rgb tint, a strength
        float ahead;    // metres the pool centre sits ahead of the cone origin
        float along;    // pool half length
        float across;   // pool half width
    };
    const PoolSource sources[2] = {
        { &EdenHeadlightFrameCones(), Vector4f( 1.0f, 0.95f, 0.80f, 0.34f ), 7.5f, 6.5f, 4.5f },
        { &EdenRearLightFrameCones(), Vector4f( 0.85f, 0.10f, 0.06f, 0.30f ), 3.2f, 2.6f, 3.4f },
    };
    for ( const PoolSource &source : sources ) {
        for ( const EdenHeadlightCone &cone : *source.pCones ) {
            const Vector3f forward( cone.Direction.x, 0.0f, cone.Direction.z );
            if ( forward.dot( forward ) < 1e-6f ) {
                continue;
            }
            const Vector3f dir = forward.normalized();
            const Vector3f side( dir.z, 0.0f, -dir.x );
            // The cone's own tint / shape (a reverse pool is white, an
            // indicator pool a small amber patch); a = 0 keeps the source's.
            const Vector4f tint = cone.Tint.w > 0.0f ? cone.Tint : source.color;
            const float ahead = cone.PoolAhead > 0.0f ? cone.PoolAhead : source.ahead;
            const float along = cone.PoolAlong > 0.0f ? cone.PoolAlong : source.along;
            const float across = cone.PoolAcross > 0.0f ? cone.PoolAcross : source.across;
            const Vector3f center = cone.Position + dir * ahead;
            const float strength = tint.w * Clamp( cone.Intensity, 0.0f, 2.5f );
            if ( strength <= 0.01f ) {
                continue;
            }
            const Vector3f alongVec = dir * along;
            const Vector3f acrossVec = side * across;
            const Vector3f corners[4] = {
                center - acrossVec - alongVec, center + acrossVec - alongVec,
                center + acrossVec + alongVec, center - acrossVec + alongVec
            };
            Vector3f ground[4];
            bool bValid = true;
            for ( int i = 0; i < 4; i++ ) {
                float h = 0.0f;
                // First choice: the surface query (the road meshes, the tunnel
                // bores, the heightfield) — the corner sits on the tarmac it
                // draws on, whatever the mesh-vs-heightfield delta does along
                // the road. fromY is the cone origin (a lamp ~0.6-0.9 m up);
                // the headroom covers the road climbing ahead of the car.
                if ( surfaceQuery && surfaceQuery( corners[ i ].x, corners[ i ].z, cone.Position.y + 4.5f, h ) ) {
                    ground[ i ] = Vector3f( corners[ i ].x, h + 0.06f, corners[ i ].z );
                } else if ( heightQuery( corners[ i ].x, corners[ i ].z, h ) ) {
                    // + the road offset: the heightfield the query walks sits
                    // below the road meshes (sector objects); the game measures
                    // the difference at the car (setRoadOffset).
                    ground[ i ] = Vector3f( corners[ i ].x, h + roadOffset + 0.05f, corners[ i ].z );
                } else {
                    bValid = false;
                    break;
                }
            }
            if ( !bValid ) {
                continue;
            }
            // A longer tail behind the car than ahead of it (the beams spread
            // forward): shift the quad centre towards -dir by easing the far
            // corners' alpha down through the shader's along-falloff instead.
            const Vector3f quad[4] = { ground[0], ground[1], ground[2], ground[3] };
            appendQuad( poolScratch, quad, PackColor( Vector4f( tint.x, tint.y, tint.z, strength ) ) );
            static bool bLogged = false;
            if ( !bLogged ) {
                bLogged = true;
                OTDU_LOG_INFO( "SurfaceFx: first light pool drawn (%u front, %u rear cones)",
                               static_cast<uint32_t>( EdenHeadlightFrameCones().size() ),
                               static_cast<uint32_t>( EdenRearLightFrameCones().size() ) );
            }
        }
    }
}

void EdenSurfaceFx::logStats() {
    const float wetnessSum = wetness.get( eGroundMaterial::Tarmac ) + wetness.get( eGroundMaterial::Dirt ) +
                             wetness.get( eGroundMaterial::Sand );
    if ( fabsf( wetnessSum - loggedWetness ) < 0.02f && numAliveParticles == loggedParticles &&
         numAliveDecals == loggedDecals ) {
        return;
    }
    loggedWetness = wetnessSum;
    loggedParticles = numAliveParticles;
    loggedDecals = numAliveDecals;
    OTDU_LOG_INFO( "SurfaceFx: %u particles, %u decals, %u puddles, wetness tarmac %.2f dirt %.2f%s sand %.2f "
                   "grass %.2f snow %.2f, ground %s, hottest disc %.0f C\n",
                   numAliveParticles, numAliveDecals, numAlivePuddles, wetness.get( eGroundMaterial::Tarmac ),
                   wetness.get( eGroundMaterial::Dirt ), wetness.isMud() ? " (mud)" : "",
                   wetness.get( eGroundMaterial::Sand ), wetness.get( eGroundMaterial::Grass ),
                   wetness.get( eGroundMaterial::Snow ), bHasEmission ? GroundMaterialName( lastMaterial ) : "-",
                   getHottestBrakeTemperature() );
}

float EdenSurfaceFx::getHottestBrakeTemperature() const {
    float hottest = -1.0f;
    for ( const BrakeDisc &disc : brakeDiscs ) {
        hottest = Max( hottest, disc.Temperature );
    }
    return hottest;
}

void EdenSurfaceFx::logBrakeHeat() {
    const float hottest = getHottestBrakeTemperature();
    // A line per 20 C of the hottest disc: a braking run reads as the heat
    // climbing and the cooling falling, without one line per frame.
    if ( fabsf( hottest - loggedBrakeHeat ) < 20.0f ) {
        return;
    }
    loggedBrakeHeat = hottest;
    char text[192] = {};
    size_t length = 0;
    for ( uint32_t i = 0; i < kMaxWheels; i++ ) {
        // The pool has 8 slots, a car four: only the posed discs are its.
        if ( !brakeDiscs[i].bValid ) {
            continue;
        }
        const int32_t written = std::snprintf( text + length, sizeof( text ) - length, "%s%.0f C/%.2f",
                                               length == 0 ? "" : " ", brakeDiscs[i].Temperature, brakeDiscs[i].Glow );
        if ( written <= 0 || length + static_cast<size_t>( written ) >= sizeof( text ) ) {
            break;
        }
        length += static_cast<size_t>( written );
    }
    OTDU_LOG_INFO( "BrakeHeat: %s, hottest %.0f C%s\n", text, hottest, bBrakeGlow ? "" : " (glow off)" );
}

void EdenSurfaceFx::setHeightQuery( std::function<bool( float x, float z, float &outHeight )> query ) {
    heightQuery = std::move( query );
}

void EdenSurfaceFx::setSurfaceQuery( std::function<bool( float x, float z, float fromY, float &outHeight )> query ) {
    surfaceQuery = std::move( query );
}

// The terrain puddles (the comment at setPuddlesEnabled): every half second a
// 5x5 grid of 5 m cells around the car goes through the height query; a cell
// sitting 8 cm below all of its neighbours is a depression and pools. A
// puddle dies with the wetness (the retail road puddles switch off the same
// way: the RainSpecs intensity gates their permutation), its alpha follows it
// while it lasts.
void EdenSurfaceFx::updatePuddles( float deltaTime, const Vector3f &center, float wetness ) {
    if ( !bPuddlesEnabled || !heightQuery ) {
        if ( numAlivePuddles != 0 ) {
            for ( Puddle &puddle : puddles ) {
                puddle.bAlive = false;
            }
            numAlivePuddles = 0;
        }
        return;
    }
    if ( wetness < 0.25f ) {
        if ( numAlivePuddles != 0 ) {
            for ( Puddle &puddle : puddles ) {
                puddle.bAlive = false;
            }
            numAlivePuddles = 0;
        }
        puddleScanTimer = 0.0f;
        return;
    }
    puddleScanTimer -= deltaTime;
    if ( puddleScanTimer > 0.0f ) {
        return;
    }
    puddleScanTimer = 0.5f;
    constexpr int32_t kGrid = 5;
    constexpr float kCell = 5.0f;
    float heights[kGrid * kGrid] = {};
    bool bValid[kGrid * kGrid] = {};
    const float originX = center.x - ( kGrid - 1 ) * 0.5f * kCell;
    const float originZ = center.z - ( kGrid - 1 ) * 0.5f * kCell;
    for ( int32_t gz = 0; gz < kGrid; gz++ ) {
        for ( int32_t gx = 0; gx < kGrid; gx++ ) {
            const int32_t i = gz * kGrid + gx;
            bValid[i] = heightQuery( originX + gx * kCell, originZ + gz * kCell, heights[i] );
        }
    }
    // One line for the first scans under -logsurfacefx: the grid's validity
    // and height spread, so a headless run shows why the minima did (or did
    // not) pool.
    static const bool bLogPuddles = CmdLine::HasArg( "-logsurfacefx" );
    if ( bLogPuddles ) {
        static int32_t numLoggedScans = 0;
        if ( numLoggedScans < 3 ) {
            numLoggedScans++;
            int32_t numValidCells = 0;
            float hMin = FLT_MAX, hMax = -FLT_MAX;
            for ( int32_t i = 0; i < kGrid * kGrid; i++ ) {
                if ( !bValid[i] ) {
                    continue;
                }
                numValidCells++;
                hMin = Min( hMin, heights[i] );
                hMax = Max( hMax, heights[i] );
            }
            char rowText[96];
            std::string gridText;
            for ( int32_t gz = 0; gz < kGrid; gz++ ) {
                snprintf( rowText, sizeof( rowText ), "%.2f %.2f %.2f %.2f %.2f | ", heights[gz * kGrid + 0],
                          heights[gz * kGrid + 1], heights[gz * kGrid + 2], heights[gz * kGrid + 3],
                          heights[gz * kGrid + 4] );
                gridText += rowText;
            }
            OTDU_LOG_INFO( "SurfaceFx puddle scan at (%.0f, %.0f): %d/%d cells %s\n", center.x, center.z,
                           numValidCells, kGrid * kGrid, gridText.c_str() );
        }
    }
    for ( int32_t gz = 1; gz < kGrid - 1; gz++ ) {
        for ( int32_t gx = 1; gx < kGrid - 1; gx++ ) {
            const int32_t i = gz * kGrid + gx;
            if ( !bValid[i] ) {
                continue;
            }
            const float h = heights[i];
            // Water sits on FLAT ground: the cell level with its neighbours
            // in an 8 cm band - slopes, banks and dune faces never pass.
            // (The island heightfield is so smooth that true local minima
            // basically do not exist - measured 5.94..6.08 as a pure
            // monotone gradient at the dealer delta - so the earlier
            // minima / low-ground rules found nothing.)
            float nMin = FLT_MAX, nMax = -FLT_MAX;
            static const int32_t kDirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for ( const auto &dir : kDirs ) {
                const int32_t j = ( gz + dir[1] ) * kGrid + ( gx + dir[0] );
                if ( bValid[j] ) {
                    nMin = Min( nMin, heights[j] );
                    nMax = Max( nMax, heights[j] );
                }
            }
            if ( nMin == FLT_MAX || nMax - h > 0.08f || h - nMin > 0.08f ) {
                continue;
            }
            const float x = originX + gx * kCell;
            const float z = originZ + gz * kCell;
            bool bKnown = false;
            for ( const Puddle &puddle : puddles ) {
                if ( !puddle.bAlive ) {
                    continue;
                }
                const float dx = puddle.Position.x - x;
                const float dz = puddle.Position.z - z;
                if ( dx * dx + dz * dz < 16.0f ) {
                    bKnown = true;
                    break;
                }
            }
            if ( bKnown || numAlivePuddles >= kMaxPuddles ) {
                continue;
            }
            for ( Puddle &puddle : puddles ) {
                if ( puddle.bAlive ) {
                    continue;
                }
                puddle = Puddle();
                puddle.bAlive = true;
                // Jittered inside the cell, so the pools do not form a
                // lattice.
                puddle.Position =
                    Vector3f( x + ( NextRandom() - 0.5f ) * kCell * 0.6f, h,
                              z + ( NextRandom() - 0.5f ) * kCell * 0.6f );
                puddle.Size = 1.4f + NextRandom() * 1.8f;
                puddle.Position.y = h;
                // The quad follows the terrain: the corner heights sampled now
                // (a flat quad on a slope would bury or float).
                const float s = puddle.Size;
                heightQuery( x - s, z - s, puddle.Corner[0] );
                heightQuery( x + s, z - s, puddle.Corner[1] );
                heightQuery( x + s, z + s, puddle.Corner[2] );
                heightQuery( x - s, z + s, puddle.Corner[3] );
                numAlivePuddles++;
                break;
            }
        }
    }
}

void EdenSurfaceFx::emitRainImpacts( float deltaTime, const Vector3f &boundsMin, const Vector3f &boundsMax,
                                     float rainIntensity, float speedKmh, float wetness ) {
    if ( !bReady || !bEnabled || deltaTime <= 0.0f ) {
        rainImpactAccum = 0.0f;
        wetDripAccum = 0.0f;
        return;
    }
    const Vector3f size = boundsMax - boundsMin;
    if ( size.x < 0.2f || size.y < 0.1f || size.z < 0.2f ) {
        return; // no body bounds (a car whose art failed to load)
    }
    static uint32_t seed = 0x9e3779b9u;
    const auto rnd = []( uint32_t &s ) {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>( s >> 8 ) * ( 1.0f / 16777216.0f );
    };
    const float speed = Clamp( speedKmh, 0.0f, 300.0f );
    // The impacts: the car collects the vertical rain; the airflow throws more
    // of it onto the body as the speed grows (the same reading as the wheel
    // spray). No rain, no impacts - the wet film still drips below.
    if ( rainIntensity > 0.001f ) {
        const float rate =
            Clamp( rainIntensity, 0.0f, 1.0f ) * ( 10.0f + speed * 0.20f ) * Max( emissionScale, 0.0f );
        rainImpactAccum += rate * deltaTime;
        int32_t count = static_cast<int32_t>( rainImpactAccum );
        if ( count > 0 ) {
            rainImpactAccum -= static_cast<float>( count );
            count = Min( count, 16 );
            for ( int32_t i = 0; i < count; i++ ) {
                // A point on the upper shell of the body: the roof / bonnet /
                // boot plane of the body's AABB (the port's stand-in for the
                // art, the same kind of approximation the wiper search uses
                // for the glass band) [INF].
                const float u = rnd( seed );
                const float v = rnd( seed );
                const float lift = rnd( seed );
                Particle particle;
                particle.Position = Vector3f( Lerp1( boundsMin.x, boundsMax.x, 0.12f + 0.76f * u ),
                                              boundsMin.y + size.y * ( 0.55f + 0.45f * lift ),
                                              Lerp1( boundsMin.z, boundsMax.z, 0.12f + 0.76f * v ) );
                // A tiny bounce off the paint, then the drag kills it: rain
                // does not spray like a wheel.
                particle.Velocity = Vector3f( ( rnd( seed ) - 0.5f ) * 0.7f, 0.5f + rnd( seed ) * 1.1f,
                                              ( rnd( seed ) - 0.5f ) * 0.7f );
                particle.Color = Vector4f( 0.70f, 0.76f, 0.84f, 0.28f + 0.25f * rnd( seed ) );
                particle.Life = 0.0f;
                particle.MaxLife = 0.18f + rnd( seed ) * 0.16f;
                particle.Size = 0.012f + rnd( seed ) * 0.022f;
                particle.Gravity = -9.81f * 0.55f;
                particle.Drag = 5.5f;
                spawnParticle( particle );
            }
        }
    } else {
        rainImpactAccum = 0.0f;
    }
    // The water film on the paint: drops run down the flanks and the tail as
    // the body sheds it (the "water particles so the car looks wet" ask). The
    // rate follows how soaked the body is and the airflow.
    const float wet = Clamp( wetness, 0.0f, 1.0f );
    if ( wet > 0.15f ) {
        wetDripAccum += wet * ( 3.0f + speed * 0.10f ) * Max( emissionScale, 0.0f ) * deltaTime;
        int32_t drips = static_cast<int32_t>( wetDripAccum );
        if ( drips > 0 ) {
            wetDripAccum -= static_cast<float>( drips );
            drips = Min( drips, 8 );
            for ( int32_t i = 0; i < drips; i++ ) {
                // A drop low on a flank / the tail: it leaves the body and
                // falls, so it reads as water running off the paint.
                const float u = rnd( seed );
                const float v = rnd( seed );
                const bool bSide = ( i & 1 ) != 0;
                Particle particle;
                particle.Position =
                    Vector3f( bSide ? ( u < 0.5f ? boundsMin.x : boundsMax.x ) : Lerp1( boundsMin.x, boundsMax.x, u ),
                              boundsMin.y + size.y * ( 0.18f + 0.35f * v ),
                              bSide ? Lerp1( boundsMin.z, boundsMax.z, v ) : boundsMin.z + size.z * 0.05f );
                particle.Velocity = Vector3f( ( rnd( seed ) - 0.5f ) * 0.35f, -0.15f - rnd( seed ) * 0.35f,
                                              ( rnd( seed ) - 0.5f ) * 0.35f );
                particle.Color = Vector4f( 0.66f, 0.73f, 0.82f, 0.22f + 0.18f * rnd( seed ) );
                particle.Life = 0.0f;
                particle.MaxLife = 0.35f + rnd( seed ) * 0.35f;
                particle.Size = 0.010f + rnd( seed ) * 0.016f;
                particle.Gravity = -9.81f;
                particle.Drag = 1.0f;
                spawnParticle( particle );
            }
        }
    } else {
        wetDripAccum = 0.0f;
    }
}
