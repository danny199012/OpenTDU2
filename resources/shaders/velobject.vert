#version 450
// OpenTDU2: per-object motion vectors (temporal upscaler groundwork). Draws a
// moving mesh (car body, wheels) with its position stream only; the clip
// position of this frame and of the previous frame (previous world matrix,
// previous camera) travel to velobject.frag.
layout( push_constant ) uniform PushConstants {
    mat4 Current;  // unjittered projection * view * world
    mat4 Previous; // previous frame's projection * view * the object's previous world
} pc;
layout( location = 0 ) in vec4 inPosition; // w is 1 for a three component stream
layout( location = 0 ) out vec4 outCurrentClip;
layout( location = 1 ) out vec4 outPreviousClip;
void main() {
    vec4 position = vec4( inPosition.xyz, 1.0 );
    outCurrentClip = pc.Current * position;
    outPreviousClip = pc.Previous * position;
    gl_Position = outCurrentClip;
}
