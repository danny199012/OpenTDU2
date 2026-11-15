#version 450
// OpenTDU2 port-native grass blades [INF: the retail grass is alpha-tested cards, no blade geometry].
// Every blade is 7 vertices (a tapered strip: base pair, two mid pairs... tip) expanded on the CPU around its
// root; this stage bends it: a lean of its own, wind (two travelling waves), and the car - blades inside
// the car's footprint lie flat and the ones beside it are pushed away, so driving through a meadow parts it.
layout( location = 0 ) in vec3 inRoot;
layout( location = 1 ) in vec4 inShape; // t along the blade, side (0..1 -> -1..1), hash, flexibility
layout( location = 2 ) in vec4 inColor; // tint rgb, width scale
layout( location = 3 ) in vec2 inSize;  // height / 1.5 m, yaw / 2pi

layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 P0; // time, wind amplitude, car half length, car half width
    vec4 P1; // car position xyz, fade end distance
    vec4 P2; // car forward x, z, camera x, z
    vec4 P3; // light rgb, car active (0 / 1)
} pc;

layout( location = 0 ) out vec3 outColor;

void main() {
    const float t = inShape.x;
    const float side = inShape.y * 2.0 - 1.0;
    const float hash = inShape.z;
    const float flex = inShape.w;
    float height = inSize.x * 1.5;
    const float yaw = inSize.y * 6.2831853;
    const vec2 facing = vec2( cos( yaw ), sin( yaw ) );

    // Shrink with the distance so the layer never ends in a hard ring.
    const vec2 toCamera = inRoot.xz - pc.P2.zw;
    const float distance = length( toCamera );
    height *= 1.0 - smoothstep( pc.P1.w * 0.6, pc.P1.w, distance );

    // The car: oriented box test in the car frame.
    float push = 0.0;
    vec2 pushDir = vec2( 0.0 );
    if ( pc.P3.w > 0.5 ) {
        const vec2 rel = inRoot.xz - pc.P1.xz;
        const vec2 fwd = pc.P2.xy;
        const float along = dot( rel, fwd );
        const float across = rel.x * fwd.y - rel.y * fwd.x;
        const float excess = max( abs( along ) - pc.P0.z, abs( across ) - pc.P0.w );
        push = 1.0 - smoothstep( -0.2, 1.6, excess );
        pushDir = normalize( rel + vec2( 1e-4 ) );
    }

    // Wind: a slow gust over the field plus a faster flutter, stronger at the tip.
    const float phase = inRoot.x * 0.21 + inRoot.z * 0.17;
    const float gust = sin( pc.P0.x * 0.9 + phase ) * 0.6 + sin( pc.P0.x * 2.3 + phase * 1.7 + hash * 6.0 ) * 0.4;
    const vec2 windDir = normalize( vec2( 0.8, 0.6 ) );
    const float bend = t * t;

    vec3 p = inRoot;
    // Own lean toward the facing, then the wind, then the car's push.
    vec2 offset = facing * ( 0.18 + 0.3 * flex ) * height * bend;
    offset += windDir * gust * pc.P0.y * height * bend * ( 0.5 + flex );
    offset += pushDir * push * height * 1.1 * bend;
    p.xz += offset;
    // The tip lowers as it bends (the blade keeps its length), and flattens under the car.
    p.y += height * t * ( 1.0 - 0.45 * bend ) * ( 1.0 - 0.8 * push );

    // Ribbon width: widest above the root, tapering to the tip.
    const float halfWidth = ( 0.010 + 0.016 * inColor.a ) * ( 1.0 - 0.92 * t * t );
    const vec2 across = vec2( -facing.y, facing.x );
    p.xz += across * side * halfWidth;

    gl_Position = pc.ViewProjection * vec4( p, 1.0 );
    // Dark at the root, bright and slightly yellower at the tip; the light comes from the scene.
    const vec3 tint = inColor.rgb * ( 0.85 + 0.3 * hash );
    outColor = tint * mix( 0.32, 1.12, t ) * pc.P3.rgb;
}
