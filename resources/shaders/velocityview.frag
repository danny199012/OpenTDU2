#version 450
// OpenTDU2 debug view (-showvelocity): the velocity buffer as colour, grey =
// still, red = the point was further right, green = further down (x40).
layout( set = 0, binding = 0 ) uniform sampler2D uVelocity;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;
void main() {
    vec2 v = texture( uVelocity, inTexcoord ).rg;
    outColor = vec4( clamp( 0.5 + v * 40.0, 0.0, 1.0 ), 0.5, 1.0 );
}
