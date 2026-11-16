#version 450
// OpenTDU2: the scene depth as an R32_SFLOAT colour image (the FSR 3 backend
// views its depth input as a plain float texture, not as a depth/stencil image).
layout( set = 0, binding = 0 ) uniform sampler2D uDepth;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;
void main() {
    // texelFetch, not texture(): the copy is 1:1, and a filtered/comparison
    // sampler on a depth image can silently return the cleared value (the
    // volumetric clouds' depth test read a constant 1.0 everywhere).
    outColor = vec4( texelFetch( uDepth, ivec2( gl_FragCoord.xy ), 0 ).r, 0.0, 0.0, 1.0 );
}
