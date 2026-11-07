#version 450
// OpenTDU2 surface effects: one soft, slightly grainy puff per quad. The
// particle colour (sand, mud, water spray...) comes in with the vertex, the
// alpha is the coverage the CPU already faded out with the particle's life.
layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 Params;
} pc;

layout( location = 0 ) in vec2 inTexcoord;
layout( location = 1 ) in vec4 inColor;
layout( location = 0 ) out vec4 outColor;

float Hash( vec2 p ) {
    return fract( sin( dot( p, vec2( 12.9898, 78.233 ) ) ) * 43758.5453 );
}

void main() {
    const vec2 d = inTexcoord * 2.0 - 1.0;
    const float r2 = dot( d, d );
    if ( r2 > 1.0 ) {
        discard;
    }
    // A droplet / dust puff: dense core, soft edge.
    const float edge = 1.0 - r2;
    float alpha = inColor.a * edge * edge;
    // Grain, so a cloud of them reads as material and not as flat discs.
    const float grain = Hash( floor( gl_FragCoord.xy * 0.5 ) + vec2( pc.Params.x * 7.0 ) );
    alpha *= 0.72 + 0.5 * grain;
    outColor = vec4( inColor.rgb, clamp( alpha, 0.0, 1.0 ) );
}
