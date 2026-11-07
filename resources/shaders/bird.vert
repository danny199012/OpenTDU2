#version 450
// OpenTDU2 birds (docs/re/aircraft_birds_spec.md): camera facing quads of the
// bird sprites of islands/<island>/fx/details/birds.bnk. The quads are expanded
// on the CPU in world space, so the vertex stage only transforms them.
layout( location = 0 ) in vec3 inPosition; // world space quad corner
layout( location = 1 ) in vec2 inTexcoord; // 0..1 across the sprite
layout( location = 2 ) in vec4 inColor;    // rgb tint, a coverage

layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 Params; // x: sky brightness
} pc;

layout( location = 0 ) out vec2 outTexcoord;
layout( location = 1 ) out vec4 outColor;

void main() {
    outTexcoord = inTexcoord;
    outColor = inColor;
    gl_Position = pc.ViewProjection * vec4( inPosition, 1.0 );
}
