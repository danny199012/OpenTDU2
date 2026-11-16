#version 450
// OpenTDU2 port-native volumetric clouds, composite pass
// (EdenClouds::composite).
//
// Draws the accumulated half-res history (volclouds_accum.frag) over the
// scene, in the blended pass right after the renderer's deferred flush (the
// sky dome is itself a deferred draw, so it is already on screen here). The
// cloud is rejected where the scene depth copy (uSceneDepth, taken straight
// after the opaque pass, before the deferred dome draws) is nearer than the
// cloud's accumulated hit distance: mountains and the car / cockpit hide the
// clouds, the clouds stay in front of what is behind them. The colour is
// premultiplied; no depth is written.
layout( set = 0, binding = 0 ) uniform sampler2D uHistory;
layout( set = 0, binding = 1 ) uniform sampler2D uSceneDepth;
layout( push_constant ) uniform PushConstants {
    vec4 Planes; // x near plane, y far plane (the projection's depth mapping)
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

const float kMaxDistance = 60000.0; // shared with the march / accumulate passes

void main() {
    const vec4 cloud = texture( uHistory, inTexcoord );
    const vec2 alphaHit = unpackHalf2x16( floatBitsToUint( cloud.a ) ); // x alpha, y hit distance (0..1)
    float alpha = alphaHit.x;
    if ( alpha > 0.0035 ) {
        // Depth of the hit distance (EdenPerspectiveFovRH: right handed, D3D
        // style depth 0..1): depth = far * (near - t) / (t * (near - far)).
        const float t = max( alphaHit.y * kMaxDistance, pc.Planes.x + 1.0 );
        const float cloudDepth =
            clamp( pc.Planes.y * ( pc.Planes.x - t ) / ( t * ( pc.Planes.x - pc.Planes.y ) ), 0.0, 1.0 );
        // A small bias so a cloud hugging a ridge does not z-fight it.
        if ( cloudDepth > texture( uSceneDepth, inTexcoord ).r + 2e-6 ) {
            alpha = 0.0;
        }
    }
    outColor = vec4( cloud.rgb * ( alpha > 0.0 ? 1.0 : 0.0 ), alpha );
}
