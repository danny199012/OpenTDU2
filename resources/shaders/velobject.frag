#version 450
// RG = previous uv - current uv of the object's surface (same mapping as velocity.frag).
layout( location = 0 ) in vec4 inCurrentClip;
layout( location = 1 ) in vec4 inPreviousClip;
layout( location = 0 ) out vec4 outColor;
vec2 toUv( vec4 clip ) {
    vec2 ndc = clip.xy / clip.w;
    return vec2( ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5 );
}
void main() {
    outColor = vec4( toUv( inPreviousClip ) - toUv( inCurrentClip ), 0.0, 1.0 );
}
