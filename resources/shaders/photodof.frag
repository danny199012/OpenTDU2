#version 450
// OpenTDU2 photo mode depth of field (the Options panel's two Depth of field rows). The original
// "crappy" DoF variant only changes the luminance index of the colour LUT (docs/re/postfx_spec.md
// 2.5), which is not a visible blur, so this is the port's own gather blur: a golden-angle disc
// whose radius follows the circle of confusion from the view depth buffer (gViewPos B, A: the
// encoding of viewpos.frag, decoded as in postfx_spec 6.2).
layout( set = 0, binding = 0 ) uniform sampler2D uScene;
layout( set = 0, binding = 1 ) uniform sampler2D uViewPos;
layout( push_constant ) uniform PushConstants {
    vec4 Params; // x: focus distance (m), y: max blur radius (pixels of this target), z: aspect, w: 1 / focus falloff scale
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

float viewDepth( vec2 uv ) {
    ivec2 size = textureSize( uViewPos, 0 );
    vec4 t = texelFetch( uViewPos, clamp( ivec2( uv * vec2( size ) ), ivec2( 0 ), size - 1 ), 0 );
    float e = floor( t.b * 31.875 );
    float lo3 = t.b * 255.0 - 8.0 * e;
    float d = exp2( e - 15.0 ) * ( 1.0 + lo3 * 0.125 + t.a * 0.125 );
    return d < 1.02 ? 30000.0 : d; // the sky dome writes ~1: infinitely far
}

float coc( float d ) {
    float focus = pc.Params.x;
    return clamp( abs( d - focus ) * pc.Params.w / ( focus + 2.0 ), 0.0, 1.0 );
}

void main() {
    vec2 texel = 1.0 / vec2( textureSize( uScene, 0 ) );
    if( pc.Params.z > 1.5 && pc.Params.z < 2.5 ) { // debug: -photodofdebug shows the decoded depth (white = 50 m)
        outColor = vec4( vec3( clamp( viewDepth( inTexcoord ) / 50.0, 0.0, 1.0 ) ), 1.0 );
        return;
    }
    float c0 = coc( viewDepth( inTexcoord ) );
    if( pc.Params.z > 2.5 ) { // debug 2: the circle of confusion, radius / 100 px
        outColor = vec4( vec3( c0 * pc.Params.y / 100.0 ), 1.0 );
        return;
    }
    float radius = c0 * pc.Params.y;
    vec3 sum = texture( uScene, inTexcoord ).rgb;
    float weight = 1.0;
    if( radius > 0.5 ) {
        const int kTaps = 28;
        for( int i = 0; i < kTaps; i++ ) {
            float r = sqrt( ( float( i ) + 0.5 ) / float( kTaps ) );
            float a = float( i ) * 2.39996323;
            vec2 uv = inTexcoord + vec2( cos( a ), sin( a ) ) * r * radius * texel;
            // A sharper tap in front of a blurred pixel must not bleed into it.
            float tapCoc = coc( viewDepth( uv ) );
            float w = tapCoc * pc.Params.y >= r * radius * 0.5 ? 1.0 : 0.15;
            sum += texture( uScene, uv ).rgb * w;
            weight += w;
        }
    }
    outColor = vec4( sum / weight, 1.0 );
}
