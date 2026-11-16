#version 450
// OpenTDU2 port-native volumetric clouds, composite pass
// (EdenClouds::composite).
//
// Draws the accumulated half-res history (volclouds_accum.frag) into the
// scene, depth tested and depth writing: the fragment depth comes from the
// accumulated hit distance, so terrain nearer than the cloud hides it and the
// cloud hides what is behind it (the first two cloud attempts drew before the
// world instead, so every mountain painted over every cloud). The colour is
// premultiplied. A pixel without cloud writes depth 1.0 (the clear value,
// harmless).
layout( set = 0, binding = 0 ) uniform sampler2D uHistory;
layout( push_constant ) uniform PushConstants {
    vec4 Planes; // x near plane, y far plane (the projection's depth mapping)
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

const float kMaxDistance = 60000.0; // shared with the march / accumulate passes

void main() {
    const vec4 cloud = texture( uHistory, inTexcoord );
    const vec2 alphaHit = unpackHalf2x16( floatBitsToUint( cloud.a ) ); // x alpha, y hit distance (0..1)
    outColor = vec4( cloud.rgb, alphaHit.x );
    // Depth of the hit distance (EdenPerspectiveFovRH: right handed, D3D
    // style depth 0..1): depth = far * (near - t) / (t * (near - far)).
    float depth = 1.0;
    if ( alphaHit.x > 0.0035 ) {
        const float t = max( alphaHit.y * kMaxDistance, pc.Planes.x + 1.0 );
        depth = clamp( pc.Planes.y * ( pc.Planes.x - t ) / ( t * ( pc.Planes.x - pc.Planes.y ) ), 0.0, 1.0 );
    }
    gl_FragDepth = depth;
}
