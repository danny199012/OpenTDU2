#version 450
// OpenTDU2 birds: one sprite per bird. The sprite (a bird in flight, 64 x 64
// DXT5) carries its own coverage in the alpha channel, so the colour is the
// texel tinted by the vertex and darkened with the sky's brightness (a bird
// against a night sky is a silhouette).
layout( set = 0, binding = 0 ) uniform sampler2D birdTex;

layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 Params;
} pc;

layout( location = 0 ) in vec2 inTexcoord;
layout( location = 1 ) in vec4 inColor;
layout( location = 0 ) out vec4 outColor;

void main() {
    const vec4 texel = texture( birdTex, inTexcoord );
    const float alpha = texel.a * inColor.a;
    if ( alpha < 0.06 ) {
        discard;
    }
    outColor = vec4( texel.rgb * inColor.rgb * pc.Params.x, alpha );
}
