#version 450
// OpenTDU2 replacement shader: the final 3D scene blit with the windscreen
// droplets composited (the retail rain-on-glass: the droplet simulation
// renders into an offscreen RT the TD_Droplets_Oncam material refracts the
// frame buffer with, docs/re/rain_shadow_spec.md A.6 - RTs Droplets_OnCam /
// Droplets_FBCopy / Droplets_Windshield). Used in place of upscale.frag only
// while the windscreen effect is active; without it the stock shaders are
// frame-identical.
//
// uDroplets: R,G = surface normal xy (0.5 centred), A = presence. A droplet
// refracts the scene sample along its normal and glints at the rim; the wet
// film (presence with a flat normal) fogs the glass a touch. The wiper sweep
// clears both (EdenWindscreen::wipe).
layout( set = 0, binding = 0 ) uniform sampler2D uSource;
layout( set = 0, binding = 1 ) uniform sampler2D uDroplets;
layout( push_constant ) uniform Params {
    vec4 uSize;  // xy: source size in texels, z: sharpness 0..1, w: edge smoothing 0/1
    vec4 uMode;  // x: 0 bilinear, 1 bicubic, y: droplet refraction strength (uv)
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

vec3 fetch( vec2 uv ) { return texture( uSource, uv ).rgb; }
float luma( vec3 c ) { return dot( c, vec3( 0.299, 0.587, 0.114 ) ); }

// Catmull-Rom with 5 bilinear taps.
vec3 bicubic( vec2 uv ) {
    vec2 size = pc.uSize.xy;
    vec2 pos = uv * size;
    vec2 center = floor( pos - 0.5 ) + 0.5;
    vec2 f = pos - center;
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;
    vec2 w0 = -0.5 * f3 + f2 - 0.5 * f;
    vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    vec2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    vec2 w3 = 0.5 * f3 - 0.5 * f2;
    vec2 w12 = w1 + w2;
    vec2 tc0 = ( center - 1.0 ) / size;
    vec2 tc3 = ( center + 2.0 ) / size;
    vec2 tc12 = ( center + w2 / w12 ) / size;
    vec3 result = fetch( vec2( tc12.x, tc0.y ) ) * ( w12.x * w0.y ) + fetch( vec2( tc0.x, tc12.y ) ) * ( w0.x * w12.y ) +
                  fetch( vec2( tc12.x, tc12.y ) ) * ( w12.x * w12.y ) + fetch( vec2( tc3.x, tc12.y ) ) * ( w3.x * w12.y ) +
                  fetch( vec2( tc12.x, tc3.y ) ) * ( w12.x * w3.y );
    float sum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max( result / sum, vec3( 0.0 ) );
}

void main() {
    vec2 uv = inTexcoord;
    vec2 texel = 1.0 / pc.uSize.xy;

    // The droplets of the frame (the RT is cleared to 0: no presence, flat
    // normal).
    vec4 drop = texture( uDroplets, uv );
    vec2 normal = drop.rg * 2.0 - 1.0;
    float presence = drop.a;
    vec2 sceneUV = uv + normal * presence * pc.uMode.y;

    vec3 color = pc.uMode.x > 0.5 ? bicubic( sceneUV ) : fetch( sceneUV );

    if ( pc.uSize.w > 0.5 ) {
        // Edge smoothing: where the local contrast is high, pull the pixel
        // towards the average of the 4 neighbours (stronger for a larger
        // contrast), measured at the render resolution.
        vec3 c = fetch( uv );
        vec3 n = fetch( uv + vec2( 0.0, -texel.y ) );
        vec3 s = fetch( uv + vec2( 0.0, texel.y ) );
        vec3 e = fetch( uv + vec2( texel.x, 0.0 ) );
        vec3 w = fetch( uv + vec2( -texel.x, 0.0 ) );
        float lc = luma( c ), ln = luma( n ), ls = luma( s ), le = luma( e ), lw = luma( w );
        float lmax = max( lc, max( max( ln, ls ), max( le, lw ) ) );
        float lmin = min( lc, min( min( ln, ls ), min( le, lw ) ) );
        float range = lmax - lmin;
        if ( range >= max( 0.0625, lmax * 0.125 ) ) {
            // Blend along the edge direction: horizontal edge -> average e/w.
            float horizontal = abs( ln + ls - 2.0 * lc );
            float vertical = abs( le + lw - 2.0 * lc );
            vec3 smoothed = horizontal >= vertical ? ( n + s + 2.0 * c ) * 0.25 : ( e + w + 2.0 * c ) * 0.25;
            vec3 average = ( n + s + e + w ) * 0.25;
            float t = clamp( range * 2.0, 0.0, 1.0 ) * 0.75;
            vec3 aa = mix( c, mix( smoothed, average, 0.5 ), t );
            color = pc.uMode.x > 0.5 ? mix( color, aa, 0.75 ) : aa;
        }
    }

    if ( pc.uSize.z > 0.001 ) {
        // Contrast adaptive sharpening: the weaker the local range (and the
        // farther from clipping), the stronger the sharpening.
        vec3 n = fetch( uv + vec2( 0.0, -texel.y ) );
        vec3 s = fetch( uv + vec2( 0.0, texel.y ) );
        vec3 e = fetch( uv + vec2( texel.x, 0.0 ) );
        vec3 w = fetch( uv + vec2( -texel.x, 0.0 ) );
        vec3 mn = min( color, min( min( n, s ), min( e, w ) ) );
        vec3 mx = max( color, max( max( n, s ), max( e, w ) ) );
        vec3 amp = sqrt( clamp( min( mn, 1.0 - mx ) / max( mx, vec3( 1e-4 ) ), 0.0, 1.0 ) );
        vec3 weight = -amp * mix( 0.125, 0.2, pc.uSize.z );
        color = clamp( ( ( n + s + e + w ) * weight + color ) / ( 1.0 + 4.0 * weight ), 0.0, 1.0 );
    }

    // Rain on the glass: the droplet lens bends and glints, the wet film fogs.
    float flatness = 1.0 - clamp( length( normal ) * 2.0, 0.0, 1.0 ); // 1 on the film, 0 on the drop rims
    float film = presence * flatness;
    color = mix( color, color * vec3( 0.72, 0.76, 0.84 ), clamp( film * 1.2, 0.0, 0.5 ) );
    // The rim of a drop catches the sky (the upper half of the lens, where the
    // normal points up towards the light).
    float lens = presence * clamp( length( normal ) * 2.0, 0.0, 1.0 );
    color += vec3( 0.9, 0.95, 1.05 ) * lens * clamp( normal.y, 0.0, 1.0 ) * 0.35;

    outColor = vec4( color, 1.0 );
}
