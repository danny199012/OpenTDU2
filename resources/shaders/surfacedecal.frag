#version 450
// OpenTDU2 ground decals: the tyre track left on sand / mud / snow / wet
// tarmac. The quad lies on the surface (CPU), u runs across the tyre and v
// along the track, so the tread is a function of u (two shoulders) and v (the
// lugs). Colour: the darkened ground the track leaves behind.
// Params.z = 1 switches to the light-pool falloff (the headlight / lamp ground
// projection): a soft elliptical pool with a gentle centre hotspot and no
// grain - the track falloff's straight shoulders and hash cells read as a
// square stamp on a light quad.
layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 Params; // x: scene time, y: tread amount, z: 1 = light pool falloff
} pc;

layout( location = 0 ) in vec2 inTexcoord;
layout( location = 1 ) in vec4 inColor;
layout( location = 0 ) out vec4 outColor;

float Hash( vec2 p ) {
    return fract( sin( dot( p, vec2( 12.9898, 78.233 ) ) ) * 43758.5453 );
}

void main() {
    const vec2 uv = inTexcoord;
    if ( pc.Params.z > 0.5 ) {
        // The light pool: radial in the quad's own (u, v) space, so a
        // rectangular beam quad gives a pool that reaches zero exactly at
        // the rim - no square edge, nothing to blend against. v runs along
        // the beam (0 the near edge at the lamps, 1 the far end): brightest
        // just ahead of the lamps and dying toward the far rim - the even
        // blob read as a floating patch, a beam is asymmetric.
        const vec2 p = ( uv - vec2( 0.5 ) ) * 2.0;
        const float r = length( vec2( p.x, p.y * 0.85 ) );
        const float base = 1.0 - smoothstep( 0.35, 1.0, r );
        const float near = 1.0 - smoothstep( -0.6, 1.0, p.y );
        const float hotspot = smoothstep( 0.9, 0.0, length( vec2( p.x * 1.2, ( p.y + 0.4 ) * 0.8 ) ) ) * 0.45;
        const float alpha = inColor.a * ( base * ( 0.55 + 0.45 * near ) + hotspot );
        outColor = vec4( inColor.rgb, clamp( alpha, 0.0, 1.0 ) );
        return;
    }
    // Across the tyre: soft shoulders, no hard quad edge.
    const float across = smoothstep( 0.0, 0.18, uv.x ) * smoothstep( 1.0, 0.82, uv.x );
    // Along the track: fade the two ends of the quad so the trail is continuous.
    const float along = smoothstep( 0.0, 0.25, uv.y ) * smoothstep( 1.0, 0.75, uv.y );
    // Tread lugs: Params.y is the tread amount (0 sand, 1 mud / snow).
    const float lugs = 0.55 + 0.45 * step( 0.5, fract( uv.y * 5.0 ) );
    const float tread = mix( 1.0, lugs, clamp( pc.Params.y, 0.0, 1.0 ) );
    // Grain breaks the straight edges of a stamp.
    const float grain = 0.85 + 0.3 * Hash( floor( uv * 48.0 ) );
    float alpha = inColor.a * across * along * tread * grain;
    outColor = vec4( inColor.rgb, clamp( alpha, 0.0, 1.0 ) );
}

