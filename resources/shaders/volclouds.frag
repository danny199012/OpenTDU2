#version 450
// OpenTDU2 port-native volumetric clouds [INF: the retail game draws cloud billboards and a SKYCLO dome,
// no volumetric clouds; this is an addition].
//
// A fullscreen pass drawn with the blended scene draws, after the haze. It only touches pixels whose
// view depth (the gViewPos buffer, see viewpos.frag) says "sky": a ray from the camera is marched through
// a horizontal slab of cloud, density is fractal value noise shaped by a height profile and a coverage
// threshold, lit by a short march towards the sun (Beer-Lambert + a forward / back Henyey-Greenstein
// phase for the bright rims) plus a sky ambient term. The result is premultiplied alpha.
layout( set = 0, binding = 0 ) uniform sampler2D uViewPos;
layout( push_constant ) uniform PushConstants {
    vec4 CamPos;   // xyz camera (world), w time (s)
    vec4 Right;    // xyz camera right * tan(fov / 2) * aspect, w coverage 0..1
    vec4 Up;       // xyz camera up * tan(fov / 2), w cloud base (m)
    vec4 Fwd;      // xyz camera forward, w cloud top (m)
    vec4 ToSun;    // xyz unit vector towards the sun, w density scale
    vec4 SunColor; // rgb sun light, w darkness (0 fair .. 1 storm: more absorption, darker bases)
    vec4 Ambient;  // rgb sky ambient, w wind x (m/s)
    vec4 Fog;      // rgb fog colour, w wind z (m/s)
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

float hash( vec3 p ) {
    p = fract( p * 0.3183099 + vec3( 0.1, 0.2, 0.3 ) );
    p *= 17.0;
    return fract( p.x * p.y * p.z * ( p.x + p.y + p.z ) );
}

float noise( vec3 x ) {
    vec3 i = floor( x );
    vec3 f = fract( x );
    f = f * f * ( 3.0 - 2.0 * f );
    return mix( mix( mix( hash( i + vec3( 0, 0, 0 ) ), hash( i + vec3( 1, 0, 0 ) ), f.x ),
                     mix( hash( i + vec3( 0, 1, 0 ) ), hash( i + vec3( 1, 1, 0 ) ), f.x ), f.y ),
                mix( mix( hash( i + vec3( 0, 0, 1 ) ), hash( i + vec3( 1, 0, 1 ) ), f.x ),
                     mix( hash( i + vec3( 0, 1, 1 ) ), hash( i + vec3( 1, 1, 1 ) ), f.x ), f.y ), f.z );
}

float fbm( vec3 p, int octaves ) {
    float sum = 0.0, amp = 0.5;
    for ( int i = 0; i < octaves; i++ ) {
        sum += amp * noise( p );
        p = p * 2.03 + vec3( 17.1, 3.7, 9.2 );
        amp *= 0.5;
    }
    return sum;
}

// 0 at the bottom of the slab, 1 at the top.
float heightFraction( float y ) {
    return clamp( ( y - pc.Up.w ) / max( pc.Fwd.w - pc.Up.w, 1.0 ), 0.0, 1.0 );
}

float density( vec3 p, bool bDetail ) {
    const float h = heightFraction( p.y );
    // Flat bases, rounded tops.
    const float profile = smoothstep( 0.0, 0.12, h ) * smoothstep( 1.0, 0.55, h );
    const vec3 wind = vec3( pc.Ambient.w, 0.0, pc.Fog.w ) * pc.CamPos.w;
    const vec3 q = ( p + wind ) * 0.00045;
    float shape = fbm( q + vec3( 0.0, h * 0.25, 0.0 ), 4 );
    // Coverage: the lower the threshold, the more of the slab is cloud.
    const float threshold = mix( 0.78, 0.36, pc.Right.w );
    float d = ( shape - threshold ) * 3.2 * profile;
    if ( bDetail && d > 0.0 ) {
        d -= ( 1.0 - fbm( q * 6.0 + vec3( 3.1 ), 3 ) ) * 0.28 * ( 1.0 - h * 0.4 );
    }
    return clamp( d, 0.0, 1.0 ) * pc.ToSun.w;
}

float henyeyGreenstein( float cosAngle, float g ) {
    const float g2 = g * g;
    return ( 1.0 - g2 ) / ( 12.566 * pow( 1.0 + g2 - 2.0 * g * cosAngle, 1.5 ) );
}

float interleavedGradient( vec2 p ) {
    return fract( 52.9829189 * fract( dot( p, vec2( 0.06711056, 0.00583715 ) ) ) );
}

void main() {
    // Sky only: the view depth of the dome is ~1 (viewpos.frag), anything else is geometry.
    const vec4 vp = texture( uViewPos, inTexcoord );
    const float byteB = vp.b * 255.0;
    const float exponent = floor( vp.b * 31.875 );
    const float viewZ = exp2( exponent - 15.0 ) * ( ( byteB - 8.0 * exponent ) / 8.0 + vp.a / 8.0 + 1.0 );
    if ( viewZ > 1.5 ) {
        outColor = vec4( 0.0 );
        return;
    }

    const vec2 ndc = vec2( inTexcoord.x * 2.0 - 1.0, inTexcoord.y * 2.0 - 1.0 );
    const vec3 dir = normalize( pc.Fwd.xyz + pc.Right.xyz * ndc.x + pc.Up.xyz * ndc.y );
    const float base = pc.Up.w, top = pc.Fwd.w;
    // Only rays that reach the slab from below (the camera lives under the clouds).
    if ( dir.y < 0.015 || pc.CamPos.y > top ) {
        outColor = vec4( 0.0 );
        return;
    }
    float tNear = ( base - pc.CamPos.y ) / dir.y;
    float tFar = ( top - pc.CamPos.y ) / dir.y;
    if ( pc.CamPos.y > base ) {
        tNear = 0.0;
    }
    const float kMaxDistance = 32000.0;
    if ( tNear > kMaxDistance ) {
        outColor = vec4( 0.0 );
        return;
    }
    tFar = min( tFar, tNear + 9000.0 );
    const int kSteps = 36;
    const float stepLength = ( tFar - tNear ) / float( kSteps );
    float t = tNear + stepLength * interleavedGradient( gl_FragCoord.xy );

    const float cosSun = dot( dir, pc.ToSun.xyz );
    const float phase = mix( henyeyGreenstein( cosSun, 0.55 ), henyeyGreenstein( cosSun, -0.25 ), 0.35 ) * 12.566;
    const float absorption = mix( 0.018, 0.034, pc.SunColor.w );

    float transmittance = 1.0;
    vec3 light = vec3( 0.0 );
    for ( int i = 0; i < kSteps; i++ ) {
        const vec3 p = pc.CamPos.xyz + dir * t;
        const float d = density( p, true );
        if ( d > 0.004 ) {
            // Light that reaches this sample from the sun: four taps along the sun direction.
            float shadow = 0.0;
            for ( int s = 1; s <= 4; s++ ) {
                shadow += density( p + pc.ToSun.xyz * ( float( s ) * 90.0 ), false );
            }
            const float beer = exp( -shadow * 90.0 * absorption );
            const float powder = 1.0 - exp( -d * 8.0 );
            const float h = heightFraction( p.y );
            const vec3 sunLight = pc.SunColor.rgb * ( beer * mix( 1.0, powder, 0.55 ) * phase );
            const vec3 ambient = pc.Ambient.rgb * mix( 0.55, 1.35, h ) * mix( 1.0, 0.55, pc.SunColor.w );
            const vec3 albedo = vec3( 1.0 ) * mix( 1.0, 0.55, pc.SunColor.w );
            const float stepTransmittance = exp( -d * stepLength * absorption );
            // Energy of the step, integrated over its length (Hillaire's analytic form).
            const vec3 scattered = ( sunLight + ambient ) * albedo * ( 1.0 - stepTransmittance );
            light += transmittance * scattered;
            transmittance *= stepTransmittance;
            if ( transmittance < 0.02 ) {
                break;
            }
        }
        t += stepLength;
    }
    float alpha = 1.0 - transmittance;
    // Towards the horizon the layer dissolves into the fog colour, so it never ends as a hard line.
    const float fade = clamp( ( tNear - 6000.0 ) / ( kMaxDistance - 6000.0 ), 0.0, 1.0 );
    light = mix( light, pc.Fog.rgb * alpha, fade );
    alpha *= 1.0 - fade * fade;
    light *= 1.0 - fade * fade;
    outColor = vec4( light, alpha );
}
