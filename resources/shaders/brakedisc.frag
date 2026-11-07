#version 450
// OpenTDU2 brake disc heat glow: the rotor in the wheel plane and the light
// spill around it, drawn as the disc comes up to temperature
// (docs/re/surface_effects_spec.md 7). Port-native [INF].
//
// The vertex alpha carries the disc's glow (0 at the first visible red, 1 at
// the hottest the model reaches); the colour is worked out here, per pixel,
// from a temperature that varies across the rotor:
//   - the pads sweep a friction ring, and the middle of that track runs the
//     hottest; its inner / outer edges trail it, and the aluminium hat in the
//     middle (no pad touches it) stays dark,
//   - the colour is a blackbody ramp: dull cherry, red, orange, then a
//     yellow-orange core when the disc is really hot (the first cut stopped
//     at a flat orange, which read as paint),
//   - the hottest part also *adds* light (premultiplied blend: the colour
//     covers the metal, the excess brightens it), so a hot rotor glows
//     instead of being a coloured plate.
// The texture of the face is rotation invariant (concentric pad grooves, a
// fine radial speckle): the disc spins with the wheel but the pose the CPU
// feeds does not, and angular streaks stood still on a turning disc.
// Params.x: scene time (a faint heat shimmer); Params.y: 0 the rotor face,
// 1 the spill around it.
layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 Params;
} pc;

layout( location = 0 ) in vec2 inTexcoord;
layout( location = 1 ) in vec4 inColor;
layout( location = 0 ) out vec4 outColor;

float Hash( float p ) {
    return fract( sin( p * 12.9898 ) * 43758.5453 );
}

// Blackbody-like ramp of a hot steel surface, t 0..1 (about 450 to 900 C).
vec3 Blackbody( float t ) {
    t = clamp( t, 0.0, 1.0 );
    const vec3 cherry = vec3( 0.30, 0.012, 0.004 );
    const vec3 red = vec3( 0.78, 0.055, 0.010 );
    const vec3 orange = vec3( 1.00, 0.30, 0.030 );
    const vec3 yellow = vec3( 1.00, 0.62, 0.180 );
    vec3 c = mix( cherry, red, smoothstep( 0.0, 0.35, t ) );
    c = mix( c, orange, smoothstep( 0.35, 0.75, t ) );
    c = mix( c, yellow, smoothstep( 0.78, 1.0, t ) );
    return c;
}

void main() {
    const vec2 d = inTexcoord * 2.0 - 1.0;
    const float r2 = dot( d, d );
    if ( r2 > 1.0 ) {
        discard;
    }
    const float r = sqrt( r2 );
    const float glow = clamp( inColor.a, 0.0, 1.0 );
    const bool bSpill = pc.Params.y > 0.5;

    // A shimmer of a couple of percent (the air over the disc), slow.
    const float shimmer = 1.0 + 0.025 * sin( pc.Params.x * 7.3 + r * 11.0 ) * glow;

    if ( !bSpill ) {
        // The friction ring (0.50 .. 0.97 of the rotor radius) and the
        // temperature across it: hottest on the pad track's centre line.
        const float ring = smoothstep( 0.46, 0.53, r ) * ( 1.0 - smoothstep( 0.955, 0.995, r ) );
        const float track = 1.0 - pow( abs( r - 0.745 ) / 0.245, 2.0 );
        // Concentric pad grooves and a radial speckle of the casting.
        const float grooves = 1.0 - 0.07 * smoothstep( 0.6, 1.0, sin( r * 150.0 ) );
        const float speckle = 1.0 + 0.06 * ( Hash( floor( r * 220.0 ) ) - 0.5 );
        float t = glow * mix( 0.62, 1.0, clamp( track, 0.0, 1.0 ) ) * grooves * speckle * shimmer;
        // Coverage: the colour replaces the grey metal as the disc heats; the
        // hat stays the metal (a faint warm cast only).
        const float hat = 0.12 * glow * ( 1.0 - smoothstep( 0.10, 0.46, r ) );
        const float coverage = clamp( ring * ( 0.35 + 0.6 * smoothstep( 0.0, 0.35, glow ) ), 0.0, 0.95 );
        const vec3 color = Blackbody( t );
        // The excess light of the hot core (added over the coverage).
        const float emit = ring * smoothstep( 0.45, 1.0, t ) * 0.55;
        const vec3 rgb = color * coverage + color * emit + Blackbody( 0.2 ) * hat;
        outColor = vec4( rgb, max( coverage, hat * 0.5 ) );
        return;
    }
    // The spill: light on the inside of the rim, the caliper and the hub
    // around the rotor - a soft annulus hugging its edge, coloured by a
    // slightly cooler temperature, added (nothing is covered).
    const float spill = smoothstep( 0.30, 0.62, r ) * ( 1.0 - smoothstep( 0.80, 1.0, r ) );
    const vec3 color = Blackbody( glow * 0.8 * shimmer );
    const float strength = spill * smoothstep( 0.05, 0.6, glow ) * 0.45;
    outColor = vec4( color * strength, 0.0 );
}
