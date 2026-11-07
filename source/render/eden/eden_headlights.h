#pragma once

#include "core/math.h"

#include <vector>

class EdenModel;

// Port-native headlight projection (docs/re/rain_shadow_spec.md B.7, the
// port-native half of docs/re/car_lights_spec.md): the retail engine rendered
// light volumes into a HeadLightsRT and blitted them into the G/B channels of
// the s15 screen shadow buffer; every shadow-receiving scene shader decodes
// those channels (a log-encoded term, x32, added to lit) and the port's
// translated shaders still do - they have read G = B = 0 so far. The game
// modes push this frame's active headlight cones here (world space);
// EdenShadows::resolve accumulates them and encodes the sum into G/B.
struct EdenHeadlightCone {
    Vector3f Position;       // world, between the front lamps
    Vector3f Direction;      // world, unit, levelled (the beams aim level)
    float Range = 45.0f;     // m
    float Intensity = 1.2f;  // the added 'lit' term at the cone centre
    float InnerCos = 0.982f; // ~11 deg half angle
    float OuterCos = 0.906f; // ~25 deg half angle (the retail HEADLIGHT
                             // TD_ShadowBox is 9.6 m wide 6 m ahead,
                             // car_lights_spec.md 4 [DATA])
    // Ground-pool overrides (EdenSurfaceFx reads them). Alpha 0 keeps the
    // registry's tint (the white front / red rear sources); the reverse and
    // indicator pools carry their own colour.
    Vector4f Tint = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ); // rgb tint, a strength
    float PoolAhead = 0.0f;  // quad centre this far ahead of the cone origin
    float PoolAlong = 0.0f;  // quad half length (0 = the source's default)
    float PoolAcross = 0.0f; // quad half width
};

// Per-frame registry: the game mode assigns the whole array every tick and
// the consumers only read it - assignment, not append, so there is no
// clearing handshake. EdenSurfaceFx draws the ground pools from these; the
// rear cones (brake / reverse) ride the same registry (Position is the rear
// of the car, Direction points backwards).
std::vector<EdenHeadlightCone> &EdenHeadlightFrameCones();
std::vector<EdenHeadlightCone> &EdenRearLightFrameCones();

// The glow of a lit car lamp (port-native [INF], car_lights_spec.md 8): a
// soft additive camera-facing quad over the lens, the bloom the retail post
// chain gives the bright TEXLIGHT pixels. The game modes refill the registry
// every frame (CinematicCar::appendLampGlows); EdenSurfaceFx draws it.
struct EdenLampGlow {
    Vector3f Position;          // world, on the lens
    Vector3f Normal;            // world, the way the lamp shines (unit)
    Vector3f Color;             // the lens tint
    float Size = 0.15f;         // half extent of the quad, m
    float Strength = 1.0f;      // 0..1
    float Core = 1.0f;          // the white-hot centre's share (0 = halo only)
    float Spread = 0.25f;       // the glow left when seen edge on (0..1)
};
std::vector<EdenLampGlow> &EdenLampGlowFrame();

// One bloom anchor per FA_<L|B|S|R|W>_<FL|FR|RL|RR>[_n] lamp sprite node of a
// car model (car_lights_spec.md 2.5), model space — where the agent's world
// matrix puts the glow quad over the lens. Traffic's shared models and the
// police bank collect these once at load (the player car's CinematicCar keeps
// its own richer anchor set).
struct EdenLampAnchor {
    Vector3f Local;       // model space, the node's origin
    Vector3f Normal;      // model space unit, the way the lamp shines
    float Size = 0.12f;   // quad half extent, m
    int32_t Function = 0; // 0 L, 1 B, 2 S, 3 R, 4 W
    bool bLeft = false;
};
void EdenCollectLampAnchors( EdenModel &model, std::vector<EdenLampAnchor> &out );
