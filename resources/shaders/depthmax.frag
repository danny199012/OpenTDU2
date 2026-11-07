#version 450
// OpenTDU2 occlusion grid (EdenOcclusion): one output texel per screen tile,
// the FARTHEST depth of the tile's pixels in the depth pre-pass (D3D depth,
// [0, 1], 1 = sky). Read back to the CPU (~160 x 90 floats instead of the whole
// depth buffer: the full 4K copy cost ~3.5 ms of GPU time over PCIe).
layout( set = 0, binding = 0 ) uniform sampler2D uDepth;
layout( push_constant ) uniform PushConstants {
    vec4 Params; // x: tile size (pixels), y: depth width, z: depth height
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

void main() {
    const int tileSize = int( pc.Params.x );
    const ivec2 size = ivec2( pc.Params.yz );
    const ivec2 origin = ivec2( gl_FragCoord.xy ) * tileSize;
    const ivec2 end = min( origin + ivec2( tileSize ), size );
    float farthest = 0.0;
    for ( int y = origin.y; y < end.y; y++ ) {
        for ( int x = origin.x; x < end.x; x++ ) {
            farthest = max( farthest, texelFetch( uDepth, ivec2( x, y ), 0 ).r );
        }
    }
    outColor = vec4( farthest, 0.0, 0.0, 1.0 );
}
