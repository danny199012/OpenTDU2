#version 450
// TDUSC-style height + noise fog, drawn over the finished scene as a
// full-screen pass before the final blit (EdenSceneView::applyTDFog, -notdfog
// opts out).
//
// The TDUSC TimeCycleElementFog template (docs/re/tdusc_weather_physics.md)
// carries a second, height-based fog layer - "Second Height Falloff" 0.02
// per metre, "Noise Amount" 0.4 / "Noise Scale" 0.1 drifting with the wind -
// on top of its Froxel distance fog. This pass ports that idea onto the
// retail image: the retail haze (edrender_pfx_haze) already applied the
// distance fog, this adds the ground-hugging layer the haze pass cannot
// express, so valleys and low ground sink into the fog colour while the
// hilltops stay clear.
//
// Inputs: the finished scene colour (post photoshop) and the opaque depth.
// The fog amount scales with the retail Skydome_haze weather key (the same
// scalar the haze pass uses), so rainy / hazy presets deepen it. The night
// fog colour of the template (8;96;255) is the reference for the tint.
layout( set = 0, binding = 0 ) uniform sampler2D uColor;
layout( set = 0, binding = 1 ) uniform sampler2D uDepth;
layout( push_constant ) uniform PushConstants {
    vec4 CamPos;    // xyz camera (world), w time (s)
    vec4 Right;     // xyz camera right * tan(fov / 2) * aspect, w tanHalf
    vec4 Up;        // xyz camera up * tanHalf, w unused
    vec4 Fwd;       // xyz camera forward, w unused
    vec4 Planes;    // x near plane, y far plane (the depth mapping)
    vec4 FogColor;  // rgb fog colour, w fog amount (Skydome_haze driven)
    vec4 HeightFog; // x density (per km), y height falloff (per m), z layer height (m), w max clamp
    vec4 Noise;     // x noise amount, y noise scale, z wind x (m/s), w wind z (m/s)
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

float hash( vec2 p ) {
    return fract( sin( dot( p, vec2( 127.1, 311.7 ) ) ) * 43758.5453 );
}

// Value noise + 3 octaves of it: the fog drift, not a texture (the pass has
// no noise binding and the amounts are low frequency anyway).
float valueNoise( vec2 p ) {
    const vec2 i = floor( p );
    const vec2 f = fract( p );
    const vec2 u = f * f * ( 3.0 - 2.0 * f );
    const float a = hash( i );
    const float b = hash( i + vec2( 1.0, 0.0 ) );
    const float c = hash( i + vec2( 0.0, 1.0 ) );
    const float d = hash( i + vec2( 1.0, 1.0 ) );
    return mix( mix( a, b, u.x ), mix( c, d, u.x ), u.y );
}

float fbm( vec2 p ) {
    return ( valueNoise( p ) + 0.5 * valueNoise( p * 2.13 ) + 0.25 * valueNoise( p * 4.41 ) ) / 1.75;
}

void main() {
    const vec4 scene = texture( uColor, inTexcoord );
    // D3D-style texcoords, the same vertical flip as the cloud march.
    const vec2 ndc = vec2( inTexcoord.x * 2.0 - 1.0, 1.0 - inTexcoord.y * 2.0 );
    const vec3 dir = normalize( pc.Fwd.xyz + pc.Right.xyz * ndc.x + pc.Up.xyz * ndc.y );
    const float depth = texture( uDepth, inTexcoord ).r;
    // The dome draws at the far plane: the sky already has its aerial
    // perspective, the height fog must not double-haze it.
    if ( depth >= 0.99995 ) {
        outColor = scene;
        return;
    }
    // Depth -> view distance (right handed, D3D style depth 0..1, the
    // composite's mapping inverted).
    const float t = ( pc.Planes.y * pc.Planes.x ) / ( depth * ( pc.Planes.x - pc.Planes.y ) + pc.Planes.y );
    const vec3 worldPos = pc.CamPos.xyz + dir * t;
    // Height term: the TDUSC second layer hugs the ground (falloff 0.02 per
    // metre - 100 m above the layer the fog has faded to ~13 %).
    const float height = exp( -max( worldPos.y - pc.HeightFog.z, 0.0 ) * pc.HeightFog.y );
    // Distance through the fogged air, in kilometres against the density.
    const float distanceKm = t / 1000.0;
    // Noise modulation: the template's amount 0.4 / scale 0.1, drifted by the
    // cloud wind so the fog rolls with the weather (the wind is m/s, the
    // noise coordinate metres - the pattern moves at wind speed).
    const float n = fbm( worldPos.xz * pc.Noise.y + vec2( pc.Noise.z, pc.Noise.w ) * pc.CamPos.w * pc.Noise.y ) - 0.5;
    float fog = ( 1.0 - exp( -pc.HeightFog.x * height * distanceKm ) ) * ( 1.0 + pc.Noise.x * n );
    fog = clamp( fog, 0.0, 1.0 ) * pc.FogColor.w;
    fog = min( fog, pc.HeightFog.w );
    outColor = vec4( mix( scene.rgb, pc.FogColor.rgb, fog ), scene.a );
}
