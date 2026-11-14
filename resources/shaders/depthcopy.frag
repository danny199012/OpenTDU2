#version 450
// OpenTDU2: the scene depth as an R32_SFLOAT colour image (the FSR 3 backend
// views its depth input as a plain float texture, not as a depth/stencil image).
layout( set = 0, binding = 0 ) uniform sampler2D uDepth;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;
void main() {
    outColor = vec4( texture( uDepth, inTexcoord ).r, 0.0, 0.0, 1.0 );
}
