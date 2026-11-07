#version 450
// OpenTDU2 replacement shader: the windscreen droplet quads (EdenWindscreen)
// drawn into the Droplets_Windshield render target. Positions arrive in clip
// space (the CPU builds them straight in screen uv -> clip), no matrices.
layout( location = 0 ) in vec2 inPosition; // clip space (-1..1)
layout( location = 1 ) in vec2 inTexcoord; // the quad's own 0..1 (the lens shape)
layout( location = 2 ) in vec4 inColor;    // rgb unused, a = the drop's alpha
layout( location = 0 ) out vec2 outTexcoord;
layout( location = 1 ) out vec4 outColor;

void main() {
    gl_Position = vec4( inPosition, 0.0, 1.0 );
    outTexcoord = inTexcoord;
    outColor = inColor;
}
