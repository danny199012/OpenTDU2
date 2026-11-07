#version 450
// OpenTDU2 replacement shader: texture copy (composites the 3D scene under
// the frontend), with the speed radial blur of the retail camera data
// (CameraInGame blur_int_*: a smear that grows towards the screen edges).
layout( set = 0, binding = 0 ) uniform sampler2D uSource;
layout( push_constant ) uniform Params {
    vec4 uParams; // x: blur strength in UV (0 = plain copy), y..w: unused
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

vec3 fetch( vec2 uv ) { return texture( uSource, uv ).rgb; }

vec3 speedBlur( vec2 uv ) {
    if ( pc.uParams.x <= 0.0005 ) {
        return fetch( uv );
    }
    vec2 dir = uv - vec2( 0.5 );
    float edge = smoothstep( 0.08, 0.55, length( dir ) );
    float amount = pc.uParams.x * ( 0.25 + 0.75 * edge );
    const int kTaps = 8;
    vec3 sum = vec3( 0.0 );
    for ( int i = 0; i < kTaps; i++ ) {
        float t = float( i ) / float( kTaps - 1 );
        sum += fetch( uv - dir * amount * t );
    }
    return sum / float( kTaps );
}

void main() {
    outColor = vec4( speedBlur( inTexcoord ), 1.0 );
}
