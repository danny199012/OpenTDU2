#version 450
// OpenTDU2 car lamp glow: the bloom of a lit lamp, a camera-facing quad drawn
// additively over the lens (port-native [INF]: the retail lamps glow through
// the post chain's bloom of the bright TEXLIGHT pixels, and the port's lens
// pass is clamped below it). The colour and the strength come in with the
// vertices (the CPU folds in the lamp's function, the viewing angle and the
// daylight); the shape is here: a tight hot core that saturates towards white,
// a soft halo around it. Params.y: the core's share (1 = a bulb seen head on,
// 0 = halo only).
layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 Params;
} pc;

layout( location = 0 ) in vec2 inTexcoord;
layout( location = 1 ) in vec4 inColor;
layout( location = 0 ) out vec4 outColor;

void main() {
    const vec2 d = inTexcoord * 2.0 - 1.0;
    const float r2 = dot( d, d );
    if ( r2 > 1.0 ) {
        discard;
    }
    const float strength = clamp( inColor.a, 0.0, 1.0 );
    // Halo: a wide falloff that reaches zero at the rim; core: a narrow
    // gaussian whose colour bleaches towards white (a lamp's filament / LED
    // is white hot whatever the lens tint).
    const float halo = pow( 1.0 - r2, 2.2 ) * 0.55;
    const float core = exp( -r2 * 38.0 ) * pc.Params.y;
    const vec3 tint = inColor.rgb;
    const vec3 hot = mix( tint, vec3( 1.0 ), 0.65 );
    const vec3 rgb = tint * halo + hot * core * 1.4;
    // Additive (SRC_ALPHA, ONE): the alpha is the strength.
    outColor = vec4( rgb, strength );
}
