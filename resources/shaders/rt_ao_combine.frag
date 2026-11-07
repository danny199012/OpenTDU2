#version 450
// OpenTDU2 replacement shader: the scene blit with the ray traced ambient
// occlusion mask (-rtao) multiplied on. Used in place of blit.frag only when
// the AO pass ran this frame; without it blit.frag is frame-identical.
layout( set = 0, binding = 0 ) uniform sampler2D uSource;
layout( set = 0, binding = 1 ) uniform sampler2D uAO;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

void main() {
    const float ao = texture( uAO, inTexcoord ).r;
    outColor = vec4( texture( uSource, inTexcoord ).rgb * ao, 1.0 );
}
