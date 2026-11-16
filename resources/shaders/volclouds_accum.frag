#version 450
// OpenTDU2 port-native volumetric clouds, temporal accumulation pass
// (EdenClouds::marchAndAccumulate).
//
// Reprojects this frame's march (volclouds.frag) into the previous camera and
// blends it into the half-res history, TAA style: the ray jitter of the march
// averages out instead of reading as dither. The history colour and alpha are
// clamped against the neighbourhood of the current march, so weather changes
// and reprojected ghosts converge instead of smearing. The alpha channel of
// both inputs carries packHalf2x16(alpha, hitDistance / kMaxDistance).
layout( set = 0, binding = 0 ) uniform sampler2D uCurrent;
layout( set = 0, binding = 1 ) uniform sampler2D uHistory;
layout( push_constant ) uniform PushConstants {
    vec4 CamPos;     // xyz this frame's camera (world), w history weight (0..1)
    vec4 Right;      // xyz this frame's camera right (unscaled), w tan(fov / 2)
    vec4 Up;         // xyz this frame's camera up (unscaled), w aspect
    vec4 Fwd;        // xyz this frame's camera forward, w reset (1 = no history)
    vec4 PrevCam;    // xyz previous camera (world), w previous tan(fov / 2)
    vec4 PrevRight;  // xyz previous camera right (unscaled), w previous aspect
    vec4 PrevUp;     // xyz previous camera up (unscaled), w unused
    vec4 PrevFwd;    // xyz previous camera forward, w unused
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

const float kMaxDistance = 60000.0; // shared with the march / composite passes

// The alpha channel packs (alpha, hitDistance) as two halves. That value MUST
// NOT be linearly filtered: interpolating the float whose *bits* are two halves
// produces garbage (the composite drew white speckles over the whole scene).
// Unpack the four nearest texels and blend the decoded pair instead.
vec2 sampleAlphaHit( sampler2D tex, vec2 uv ) {
    const ivec2 size = textureSize( tex, 0 );
    const vec2 texel = uv * vec2( size ) - 0.5;
    const ivec2 base = ivec2( floor( texel ) );
    const vec2 f = texel - vec2( base );
    vec2 result = vec2( 0.0 );
    for ( int i = 0; i < 4; i++ ) {
        const ivec2 o = ivec2( i & 1, i >> 1 );
        const ivec2 c = clamp( base + o, ivec2( 0 ), size - 1 );
        const vec4 t = texelFetch( tex, c, 0 );
        const vec2 ah = unpackHalf2x16( floatBitsToUint( t.a ) );
        const vec2 w = mix( 1.0 - f, f, vec2( o ) );
        result += ah * w.x * w.y;
    }
    return result;
}

void main() {
    const vec4 current = texture( uCurrent, inTexcoord );
    const vec2 currentAH = sampleAlphaHit( uCurrent, inTexcoord ); // x alpha, y hit distance (0..1)

    vec4 history = current;
    vec2 historyAH = currentAH;
    if ( pc.Fwd.w < 0.5 ) {
        // The march hit distance -> world position -> previous frame's uv.
        // Vertical NDC flips (D3D texcoords, see volclouds.frag), and so does
        // the uv row: uv y grows downwards on screen.
        const vec2 ndc = vec2( inTexcoord.x * 2.0 - 1.0, 1.0 - inTexcoord.y * 2.0 );
        const vec3 dir = normalize( pc.Fwd.xyz + pc.Right.xyz * ( pc.Right.w * pc.Up.w * ndc.x ) +
                                    pc.Up.xyz * ( pc.Right.w * ndc.y ) );
        const vec3 world = pc.CamPos.xyz + dir * ( currentAH.y * kMaxDistance );
        const vec3 v = world - pc.PrevCam.xyz;
        const float z = dot( v, pc.PrevFwd.xyz );
        if ( z > 1.0 ) {
            const vec2 uvHistory =
                vec2( 0.5 + 0.5 * dot( v, pc.PrevRight.xyz ) / ( z * pc.PrevCam.w * pc.PrevRight.w ),
                      0.5 - 0.5 * dot( v, pc.PrevUp.xyz ) / ( z * pc.PrevCam.w ) );
            if ( all( greaterThan( uvHistory, vec2( 0.0 ) ) ) && all( lessThan( uvHistory, vec2( 1.0 ) ) ) ) {
                history = texture( uHistory, uvHistory );
                historyAH = sampleAlphaHit( uHistory, uvHistory );
                // Clamp the history against the neighbourhood of this frame's
                // march (colour and alpha), so changes converge instead of ghosting.
                const vec2 texel = 1.0 / vec2( textureSize( uCurrent, 0 ) );
                vec4 lo = current, hi = current;
                float alphaLo = currentAH.x, alphaHi = currentAH.x;
                for ( int i = 0; i < 4; i++ ) {
                    const vec2 offset = vec2( ( i & 1 ) != 0 ? 1.5 : -1.5, ( i & 2 ) != 0 ? 1.5 : -1.5 );
                    const vec4 tap = texture( uCurrent, inTexcoord + offset * texel );
                    lo = min( lo, tap );
                    hi = max( hi, tap );
                    const float a = sampleAlphaHit( uCurrent, inTexcoord + offset * texel ).x;
                    alphaLo = min( alphaLo, a );
                    alphaHi = max( alphaHi, a );
                }
                history.rgb = clamp( history.rgb, lo.rgb - 0.004, hi.rgb + 0.004 );
                historyAH.x = clamp( historyAH.x, alphaLo - 0.004, alphaHi + 0.004 );
            } else {
                // The cloud left the screen: this frame only.
                history = current;
                historyAH = currentAH;
            }
        }
    }

    const float keep = clamp( pc.CamPos.w, 0.0, 1.0 ); // history weight
    const float fresh = 1.0 - keep;
    const float alpha = mix( historyAH.x, currentAH.x, fresh );
    const float hit = mix( historyAH.y, currentAH.y, fresh );
    // pc.PrevUp.w is unused (-showcloudmarchalpha): this frame's march alpha as
    // grey, so "the march found nothing" and "the accumulation lost it" can be
    // told apart without guessing.
    if ( pc.PrevUp.w > 0.5 ) {
        outColor = vec4( vec3( currentAH.x ), 1.0 );
        return;
    }
    outColor = vec4( mix( history.rgb, current.rgb, fresh ), uintBitsToFloat( packHalf2x16( vec2( alpha, hit ) ) ) );
}
