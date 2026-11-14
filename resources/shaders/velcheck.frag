#version 450
// OpenTDU2 debug (-velcheck): reprojection test of the velocity buffer.
// Warps last frame's image with the velocity and compares it to this frame:
//   R = |cur - prev(uv + v)|   (the buffer's prediction: should be the smallest)
//   G = |cur - prev(uv)|       (no compensation)
//   B = |cur - prev(uv - v)|   (the opposite sign: a control)
layout( set = 0, binding = 0 ) uniform sampler2D uCurrent;
layout( set = 0, binding = 1 ) uniform sampler2D uPrevious;
layout( set = 0, binding = 2 ) uniform sampler2D uVelocity;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;
float err( vec3 a, vec3 b ) { return dot( abs( a - b ), vec3( 1.0 / 3.0 ) ); }
void main() {
    vec2 v = texture( uVelocity, inTexcoord ).rg;
    vec3 c = texture( uCurrent, inTexcoord ).rgb;
    outColor = vec4( err( c, texture( uPrevious, inTexcoord + v ).rgb ), err( c, texture( uPrevious, inTexcoord ).rgb ),
                    err( c, texture( uPrevious, inTexcoord - v ).rgb ), 1.0 );
}
