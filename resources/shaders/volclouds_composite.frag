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

// See volclouds_accum.frag: the packed (alpha, hitDistance) in the alpha channel
// must never be linearly filtered - decode the four nearest texels and blend.
vec2 sampleAlphaHit( sampler2D tex, vec2 uv ) {
    const ivec2 size = textureSize( tex, 0 );
    const vec2 texel = uv * vec2( size ) - 0.5;
    const ivec2 base = ivec2( floor( texel ) );
    const vec2 f = texel - vec2( base );
    vec2 result = vec2( 0.0 );
    for ( int i = 0; i < 4; i++ ) {
        const ivec2 o = ivec2( i & 1, i >> 1 );
        const ivec2 c = clamp( base + o, ivec2( 0 ), size - 1 );
        const vec4 t = texelFetch( tex, c, 0 );
        const vec2 ah = unpackHalf2x16( floatBitsToUint( t.a ) );
        const vec2 w = mix( 1.0 - f, f, vec2( o ) );
        result += ah * w.x * w.y;
    }
    return result;
}

void main() {
    const vec4 cloud = texture( uHistory, inTexcoord );
    const vec2 alphaHit = sampleAlphaHit( uHistory, inTexcoord ); // x alpha, y hit distance (0..1)
    // pc.Planes.z selects a diagnostic mode (see EdenClouds::composite):
    //   0 normal, 1 -showcloudalpha, 2 -noclouddepth, 3 -clouddepthflip.
    const int mode = int( pc.Planes.z + 0.5 );
    const vec2 uvDepth = mode == 3 ? vec2( inTexcoord.x, 1.0 - inTexcoord.y ) : inTexcoord;
    const float sceneDepth = texture( uSceneDepth, uvDepth ).r;
    float alpha = alphaHit.x;
    float cloudDepth = 0.0;
    if ( alpha > 0.0035 ) {
        // Depth of the hit distance (EdenPerspectiveFovRH: right handed, D3D
        // style depth 0..1): depth = far * (near - t) / (t * (near - far)).
        const float t = max( alphaHit.y * kMaxDistance, pc.Planes.x + 1.0 );
        cloudDepth =
            clamp( pc.Planes.y * ( pc.Planes.x - t ) / ( t * ( pc.Planes.x - pc.Planes.y ) ), 0.0, 1.0 );
        // A small bias so a cloud hugging a ridge does not z-fight it.
        if ( mode != 2 && cloudDepth > sceneDepth + 2e-6 ) {
            alpha = 0.0;
        }
    }
    // 1: R = accumulated alpha, G = copied scene depth, B = the cloud's depth.
    if ( mode == 1 ) {
        outColor = vec4( alpha, sceneDepth, cloudDepth, 1.0 );
        return;
    }
    outColor = vec4( cloud.rgb * ( alpha > 0.0 ? 1.0 : 0.0 ), alpha );
}
