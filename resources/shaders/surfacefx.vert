#version 450
// OpenTDU2 surface effects: camera facing billboards (splashes, dust) and the
// ground decals (tyre tracks). The quads are expanded on the CPU in world
// space, so the vertex stage only transforms them: the projection carries the
// Vulkan clip convention already (Matrix4x4f is column major, M * v).
layout( location = 0 ) in vec3 inPosition; // world space quad corner
layout( location = 1 ) in vec2 inTexcoord; // 0..1 across the quad
layout( location = 2 ) in vec4 inColor;    // rgb tint, a coverage

layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 Params; // x: scene time, y: decal tread amount (0 sand, 1 mud / snow)
} pc;

layout( location = 0 ) out vec2 outTexcoord;
layout( location = 1 ) out vec4 outColor;

void main() {
    outTexcoord = inTexcoord;
    outColor = inColor;
    gl_Position = pc.ViewProjection * vec4( inPosition, 1.0 );
}
