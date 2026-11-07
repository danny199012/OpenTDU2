#version 450
// OpenTDU2 night sky starfield: a soft point with a slight warm / cool tint
// by brightness (the hot stars warmer), faded by the weather's star percent.
layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 Params; // x: master alpha, y: scene time
} pc;

layout( location = 0 ) in float inBrightness;
layout( location = 1 ) in float inTwinkle;
layout( location = 0 ) out vec4 outColor;

void main() {
    // Soft round falloff over the point sprite.
    const vec2 d = gl_PointCoord - vec2( 0.5 );
    const float r = length( d ) * 2.0;
    const float falloff = clamp( 1.0 - r, 0.0, 1.0 );
    // Warmer for the brighter stars (mimics the retail star art's tint mix).
    const vec3 tint = mix( vec3( 0.82, 0.88, 1.0 ), vec3( 1.0, 0.95, 0.85 ), inBrightness );
    const float twinkle = 1.0 - 0.18 * max( inTwinkle, 0.0 );
    // x2.5: the additive path scales rgb by alpha (SRC_ALPHA / ONE), so the
    // core of the point must reach ~1 to read as a star against the dome.
    const float alpha = 2.5 * pc.Params.x * inBrightness * falloff * falloff * twinkle;
    outColor = vec4( tint, clamp( alpha, 0.0, 1.0 ) );
}
