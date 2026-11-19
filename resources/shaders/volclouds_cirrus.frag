#version 450
// OpenTDU2 port-native cirrus sheet (EdenClouds::drawCirrus).
//
// TDUSC renders its high cirrus from a 2D texture (TimeCycleElementSky's
// Cloud Cirrus Texture / CLOUDCIRRUS.PTX), not raymarched - this pass matches
// that: a ray/band intersection at 6500-7200 m sampling a CPU-generated
// anisotropic streak noise, tinted by the sun with a forward-scatter glow at
// TDUSC's Mie Anisotropy 0.8. Drawn in the blended pass right after the
// deferred flush and BEFORE the volumetric composite, so the cumulus deck
// draws over it; the copied opaque depth (uSceneDepth, the same copy the
// composite uses) rejects the sheet behind mountains. The template's Rainy
// Cloud coupling is the darkness scalar dissolving the cirrus in a storm.
//
// Output: premultiplied colour, no depth written.
layout( set = 0, binding = 0 ) uniform sampler2D uCirrus;
layout( set = 0, binding = 1 ) uniform sampler2D uSceneDepth;
layout( push_constant ) uniform PushConstants {
    vec4 CamPos;   // xyz camera (world), w time (s)
    vec4 Right;    // xyz camera right * tan(fov / 2) * aspect, w tanHalf
    vec4 Fwd;      // xyz camera forward, w unused
    vec4 Up;       // xyz camera up, w unused
    vec4 ToSun;    // xyz unit vector towards the sun, w darkness 0..1
    vec4 SunColor; // rgb sun light, w cirrus amount 0..1
    vec4 Planes;   // x near plane, y far plane (the depth reject)
    vec4 Wind;     // x wind x (m/s), y wind z (m/s), z band bottom (m), w band top (m)
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

const float kMaxDistance = 60000.0; // shared with the other cloud passes
const float kSheetScale = 90000.0;  // metres per streak-noise tile
const float kDepthEpsilon = 2e-6;   // same ridge bias as the composite

float hash( vec2 p ) {
    return fract( sin( dot( p, vec2( 127.1, 311.7 ) ) ) * 43758.5453 );
}

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

void main() {
    // D3D-style texcoords, the same vertical flip as the march pass.
    const vec2 ndc = vec2( inTexcoord.x * 2.0 - 1.0, 1.0 - inTexcoord.y * 2.0 );
    const vec3 dir = normalize( pc.Fwd.xyz + pc.Right.xyz * ndc.x + pc.Up.xyz * pc.Right.w * ndc.y );
    float alpha = pc.SunColor.w;
    if ( alpha <= 0.0 ) {
        outColor = vec4( 0.0 );
        return;
    }
    // Ray / band intersection from any camera height.
    float tEntry = 0.0;
    bool bHit = false;
    if ( abs( dir.y ) > 1e-4 ) {
        const float t0 = ( pc.Wind.z - pc.CamPos.y ) / dir.y;
        const float t1 = ( pc.Wind.w - pc.CamPos.y ) / dir.y;
        tEntry = max( min( t0, t1 ), 0.0 );
        bHit = max( t0, t1 ) > tEntry && tEntry < kMaxDistance;
    } else if ( pc.CamPos.y >= pc.Wind.z && pc.CamPos.y <= pc.Wind.w ) {
        bHit = true;
    }
    if ( !bHit ) {
        outColor = vec4( 0.0 );
        return;
    }
    // Reject the sheet where the opaque world is nearer than the band entry
    // (the composite's depth mapping: right handed, D3D style depth 0..1).
    // The pass wraps the noise sampler (the sheet drifts forever), so clamp
    // the depth uv off the wrap boundary.
    const vec2 uvDepth = min( inTexcoord, vec2( 0.9999 ) );
    const float sceneDepth = texture( uSceneDepth, uvDepth ).r;
    const float t = max( tEntry, pc.Planes.x + 1.0 );
    const float cloudDepth =
        clamp( pc.Planes.y * ( pc.Planes.x - t ) / ( t * ( pc.Planes.x - pc.Planes.y ) ), 0.0, 1.0 );
    if ( cloudDepth > sceneDepth + kDepthEpsilon ) {
        outColor = vec4( 0.0 );
        return;
    }
    // The sheet is thin: fade the density towards the band edges so the slab
    // never shows a hard top/bottom cut.
    const float hBand = 1.0 - abs( ( pc.CamPos.y + dir.y * tEntry ) - ( pc.Wind.z + pc.Wind.w ) * 0.5 ) /
                                 ( ( pc.Wind.w - pc.Wind.z ) * 0.5 );
    // The drift is unbounded, so the sheet uv wraps (the streak noise tiles
    // seamlessly - integer frequency multiples at generation).
    const vec2 uv = fract( 0.5 + ( pc.CamPos.xz + dir.xz * tEntry +
                                   vec2( pc.Wind.x, pc.Wind.y ) * pc.CamPos.w ) / kSheetScale );
    // Two octaves of the streak noise; the texture is already anisotropic
    // (stretched at generation), the second tap roughens the wisps.
    const float n = 0.65 * texture( uCirrus, uv ).r + 0.35 * texture( uCirrus, uv * 3.0 + vec2( 0.33, 0.71 ) ).r;
    alpha *= smoothstep( 0.28, 0.62, n ) * clamp( hBand, 0.0, 1.0 );
    // The template's NewCirrusCoverage tops out at 0.6 - a high haze, never a
    // solid lid. Storms dissolve it (Rainy Cloud).
    alpha *= 0.55 * ( 1.0 - 0.85 * pc.ToSun.w );
    // Horizon fade: a sheet at 7 km reads as a haze line at grazing angles.
    alpha *= smoothstep( 0.015, 0.10, abs( dir.y ) );
    // Forward-scatter glow around the sun (Mie anisotropy 0.8), lit by the
    // same tinted sun the deck uses.
    const float glow = 0.45 + 0.55 * pow( max( dot( dir, pc.ToSun.xyz ), 0.0 ), 8.0 );
    const vec3 color = pc.SunColor.rgb * glow + vec3( 0.5, 0.6, 0.75 ) * 0.35;
    outColor = vec4( color * alpha, alpha );
}
