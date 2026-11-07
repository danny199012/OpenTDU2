#version 450
// OpenTDU2 replacement shader: one windscreen droplet (EdenWindscreen). The
// R channel pair carries the lens' surface normal (radial, 0.5 centred) the
// windscreen.frag composite refracts the scene with, A the presence. Mode 1
// (push constant) draws the wet film instead: one flat-normal full-screen
// quad whose alpha is the film strength.
layout( push_constant ) uniform Params {
    vec4 u; // x: mode (0 droplet, 1 film), y: film alpha
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 1 ) in vec4 inColor;
layout( location = 0 ) out vec4 outColor;

void main() {
    if ( pc.u.x > 0.5 ) {
        // The wet film: a flat surface (normal 0.5/0.5), the composite fogs by
        // presence alone.
        outColor = vec4( 0.5, 0.5, 0.0, pc.u.y );
        return;
    }
    vec2 p = inTexcoord * 2.0 - 1.0;
    float r2 = dot( p, p );
    if ( r2 > 1.0 ) {
        discard;
    }
    float r = sqrt( r2 );
    // A lens: a soft body with a denser rim (the ring a drop shows).
    float body = smoothstep( 1.0, 0.5, r );
    float rim = smoothstep( 0.55, 0.95, r );
    float alpha = clamp( ( body * 0.5 + rim * 0.5 ) * inColor.a, 0.0, 1.0 );
    outColor = vec4( p * 0.5 + 0.5, 0.0, alpha );
}
