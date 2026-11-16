#version 450
// OpenTDU2 port-native volumetric clouds, march pass [INF: the retail game
// draws cloud billboards and a SKYCLO dome, no volumetric clouds; this is an
// addition].
//
// Half-res raymarch of the cloud slab (EdenClouds::marchAndAccumulate). The
// density is a CPU generated, tileable Perlin-Worley 3D texture (uBase) carved
// by a coverage threshold and a height profile, eroded by a tileable Worley
// detail texture (uDetail). Lit by a 6-tap sun march with a two-octave
// multi-scatter fit (Schneider / Hillaire), a dual-lobe Henyey-Greenstein
// phase and a vertical sky gradient.
//
// Output: premultiplied colour; the alpha channel carries
// packHalf2x16(alpha, hitDistance / kMaxDistance) for the accumulate pass
// (volclouds_accum.frag, temporal reprojection that removes the ray jitter)
// and the composite pass (volclouds_composite.frag, which turns the hit
// distance into a fragment depth so terrain and clouds occlude each other).
layout( set = 0, binding = 0 ) uniform sampler3D uBase;
layout( set = 0, binding = 1 ) uniform sampler3D uDetail;
layout( push_constant ) uniform PushConstants {
    vec4 CamPos;   // xyz camera (world), w time (s)
    vec4 Right;    // xyz camera right * tan(fov / 2) * aspect, w coverage 0..1
    vec4 Up;       // xyz camera up * tan(fov / 2), w cloud base (m)
    vec4 Fwd;      // xyz camera forward, w cloud top (m)
    vec4 ToSun;    // xyz unit vector towards the sun, w darkness 0..1
    vec4 SunColor; // rgb sun light, w wind x (m/s)
    vec4 Ambient;  // rgb sky ambient, w wind z (m/s)
    vec4 Fog;      // rgb fog colour, w unused
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

const float kMaxDistance = 60000.0; // shared with the accumulate / composite passes
const float kShapeScale = 4500.0;   // metres per base-noise tile
const float kDetailScale = 450.0;   // metres per detail-noise tile
const float kMarchLength = 16000.0; // metres marched inside the slab

float heightFraction( float y ) {
    return clamp( ( y - pc.Up.w ) / max( pc.Fwd.w - pc.Up.w, 1.0 ), 0.0, 1.0 );
}

float density( vec3 p, bool bDetail ) {
    const float h = heightFraction( p.y );
    // Rounded bases; the tops thin out so the slab never ends as a lid.
    const float profile = smoothstep( 0.0, 0.10, h ) * ( 1.0 - smoothstep( 0.45, 0.98, h ) );
    const vec3 q = ( p + vec3( pc.SunColor.w, 0.0, pc.Ambient.w ) * pc.CamPos.w ) / kShapeScale;
    float d = texture( uBase, q ).r;
    // Coverage: the lower the threshold, the more of the slab is cloud. The
    // divisor saturates the interior quickly (the pale 1 - threshold scaling
    // left a sky thin enough to see through).
    const float threshold = mix( 0.72, 0.34, pc.Right.w );
    d = clamp( ( d - threshold ) / mix( 0.45, 0.30, pc.Right.w ), 0.0, 1.0 ) * profile;
    if ( bDetail && d > 0.0 ) {
        const float w = dot( texture( uDetail, q * ( kShapeScale / kDetailScale ) ).rgb, vec3( 0.625, 0.25, 0.125 ) );
        // Erode the edges: wispy at the bottom, billowier near the top.
        d = clamp( d - ( 1.0 - w ) * 0.24 * mix( 1.0, 0.5, h ), 0.0, 1.0 );
    }
    return d * mix( 1.0, 1.6, pc.ToSun.w );
}

// Optical depth towards the sun: six taps with a growing step, ~1.4 km reach.
// The detail erosion only runs on the first taps (cost).
float lightMarch( vec3 p ) {
    float sum = 0.0;
    float step = 28.0;
    for ( int s = 0; s < 6; s++ ) {
        sum += density( p + pc.ToSun.xyz * step, s < 2 ) * step;
        step *= 1.9;
    }
    return sum;
}

float henyeyGreenstein( float cosAngle, float g ) {
    const float g2 = g * g;
    return ( 1.0 - g2 ) / ( 12.566 * pow( 1.0 + g2 - 2.0 * g * cosAngle, 1.5 ) );
}

float interleavedGradient( vec2 p, float seed ) {
    return fract( 52.9829189 * fract( dot( p + seed * vec2( 5.41, 17.113 ), vec2( 0.06711056, 0.00583715 ) ) ) );
}

void main() {
    // The texcoords are D3D style (0,0 top left): the vertical NDC of the ray
    // must flip, or the sky renders mirrored below the horizon (attempt 1's
    // "cloud band" was this bug seen edge-on).
    const vec2 ndc = vec2( inTexcoord.x * 2.0 - 1.0, 1.0 - inTexcoord.y * 2.0 );
    const vec3 dir = normalize( pc.Fwd.xyz + pc.Right.xyz * ndc.x + pc.Up.xyz * ndc.y );
    const float base = pc.Up.w, top = pc.Fwd.w;
    // Ray / slab intersection from any camera height (above the deck too).
    float tNear = 0.0, tFar = -1.0;
    if ( abs( dir.y ) > 1e-4 ) {
        const float t0 = ( base - pc.CamPos.y ) / dir.y;
        const float t1 = ( top - pc.CamPos.y ) / dir.y;
        tNear = max( min( t0, t1 ), 0.0 );
        tFar = max( t0, t1 );
    } else if ( pc.CamPos.y >= base && pc.CamPos.y <= top ) {
        tFar = kMaxDistance;
    }
    if ( tFar <= tNear || tNear > kMaxDistance ) {
        outColor = vec4( 0.0 );
        return;
    }
    tFar = min( tFar, tNear + kMarchLength );

    const int kSteps = 56;
    const float stepLength = ( tFar - tNear ) / float( kSteps );
    const float jitter = interleavedGradient( gl_FragCoord.xy, fract( pc.CamPos.w * 0.618034 ) );
    float t = tNear + stepLength * jitter;

    const float cosSun = dot( dir, pc.ToSun.xyz );
    const float phase = mix( henyeyGreenstein( cosSun, 0.60 ), henyeyGreenstein( cosSun, -0.20 ), 0.35 ) * 12.566;
    const float sigma = mix( 0.020, 0.042, pc.ToSun.w ); // extinction per metre of density

    float transmittance = 1.0;
    vec3 light = vec3( 0.0 );
    float hitT = tNear;
    for ( int i = 0; i < kSteps; i++ ) {
        const vec3 p = pc.CamPos.xyz + dir * t;
        const float d = density( p, true );
        if ( d > 0.004 ) {
            // Two multi-scatter octaves: the first is the single scatter with
            // a powder darkening, the second fakes the light that bounces
            // into the cloud interior (Schneider's fit).
            const float lightDepth = lightMarch( p );
            const float powder = 1.0 - exp( -d * 6.0 );
            vec3 sunLight = vec3( 0.0 );
            float scatter = 1.0, extinction = 1.0;
            for ( int o = 0; o < 2; o++ ) {
                sunLight += pc.SunColor.rgb * exp( -lightDepth * sigma * extinction ) * scatter;
                scatter *= 0.42;
                extinction *= 0.30;
            }
            sunLight *= phase * mix( 1.0, powder, 0.55 );
            const float h = heightFraction( p.y );
            const vec3 ambient = pc.Ambient.rgb * mix( 0.45, 1.30, h ) * mix( 1.0, 0.45, pc.ToSun.w );
            const float stepTransmittance = exp( -d * stepLength * sigma );
            // Energy of the step, integrated over its length (Hillaire's analytic form).
            const vec3 scattered = ( sunLight + ambient ) * ( 1.0 - stepTransmittance );
            light += transmittance * scattered;
            transmittance *= stepTransmittance;
            if ( transmittance >= 0.55 ) {
                hitT = t + stepLength * 0.5; // the front surface: the last sample still mostly transparent
            }
            if ( transmittance < 0.02 ) {
                break;
            }
        }
        t += stepLength;
    }
    float alpha = 1.0 - transmittance;
    // Towards the horizon the layer dissolves into the fog colour, so it
    // never ends as a hard line (the composite's depth test keeps it in
    // front of / behind terrain correctly).
    const float fade = clamp( ( tNear - 18000.0 ) / 22000.0, 0.0, 1.0 );
    // Rays grazing the slab (the deck seen edge-on from above / below) sample
    // the noise field at sub-pixel scale: fade them instead of showing alias.
    alpha *= smoothstep( 0.0, 0.10, abs( dir.y ) );
    light = mix( light, pc.Fog.rgb * alpha, fade );
    alpha *= 1.0 - fade * fade * 0.9;
    light *= 1.0 - fade * fade * 0.9;
    const float hitN = clamp( hitT / kMaxDistance, 0.0, 1.0 );
    outColor = vec4( light, uintBitsToFloat( packHalf2x16( vec2( alpha, hitN ) ) ) );
}
