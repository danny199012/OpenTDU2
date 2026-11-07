#version 450
// OpenTDU2 replacement of one RT SHADOW 2D slice pass (FSShadowMat[i],
// edrender_pfx_shadow ps_36 / ps_18 / ps_12), same inputs and law:
// view position from the encoded view depth, receiver depth = ViewToLight[i]
// row (bias included), map uv = UVMatrix[2i..2i+1], 2x2 PCF on the R32F map
// (stored 2z - 1), a texel with no caster (>= 0.999) never shadows,
// out = 1 + opacity * (lit - 1). Used with -ownshadowpass (A/B against the
// translated original).
layout( set = 0, binding = 0 ) uniform sampler2D uViewPos;
layout( set = 0, binding = 1 ) uniform sampler2D uSlice;
layout( push_constant ) uniform PushConstants {
    vec4 DepthRow; // ViewToLight[i] (biased)
    vec4 UvRow0;   // UVMatrix[2i]
    vec4 UvRow1;   // UVMatrix[2i + 1]
    vec4 Params;   // x: P00, y: P11, z: map size, w: sun shadow opacity
    vec4 Mode;     // x: 0 the original's taps (weights fract(uv N)), 1 texel centre bilinear
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

float occluded( ivec2 texel, float receiver ) {
    const ivec2 size = textureSize( uSlice, 0 );
    const float stored = texelFetch( uSlice, clamp( texel, ivec2( 0 ), size - 1 ), 0 ).r * 0.5 + 0.5;
    return ( stored < 0.999 && stored < receiver ) ? 1.0 : 0.0;
}

void main() {
    const vec4 viewPos = texture( uViewPos, inTexcoord );
    const float b = floor( viewPos.b * 255.0 + 0.5 );
    const float a = floor( viewPos.a * 255.0 + 0.5 );
    const float exponent = floor( b / 8.0 );
    const float viewZ = exp2( exponent - 15.0 ) * ( 1.0 + ( b - exponent * 8.0 ) / 8.0 + a / 2040.0 );
    // D3D clip space (y up) of the pixel centre.
    const vec2 ndc = vec2( inTexcoord.x * 2.0 - 1.0, 1.0 - inTexcoord.y * 2.0 );
    const vec4 position = vec4( ndc.x * viewZ / pc.Params.x, ndc.y * viewZ / pc.Params.y, -viewZ, 1.0 );
    const float receiver = clamp( dot( position, pc.DepthRow ), 0.0, 1.0 );
    const vec2 uv = vec2( dot( position, pc.UvRow0 ), dot( position, pc.UvRow1 ) );
    const float size = pc.Params.z;
    vec2 t = uv * size;
    if ( pc.Mode.x > 0.5 ) {
        t -= 0.5;
    }
    const ivec2 base = ivec2( floor( t ) );
    const vec2 f = fract( t );
    const float s00 = occluded( base, receiver );
    const float s10 = occluded( base + ivec2( 1, 0 ), receiver );
    const float s01 = occluded( base + ivec2( 0, 1 ), receiver );
    const float s11 = occluded( base + ivec2( 1, 1 ), receiver );
    const float shadow = mix( mix( s00, s10, f.x ), mix( s01, s11, f.x ), f.y );
    const float lit = 1.0 - shadow;
    if ( pc.Mode.y > 0.0 ) {
        // Debug: margin of the nearest tap, stored - receiver, in metres x Mode.y
        // (Mode.z = depth range): 0.5 = equal, brighter = lit side.
        const ivec2 nearest = ivec2( floor( uv * size ) );
        const float stored = texelFetch( uSlice, clamp( nearest, ivec2( 0 ), textureSize( uSlice, 0 ) - 1 ), 0 ).r * 0.5 + 0.5;
        outColor = vec4( clamp( 0.5 + ( stored - receiver ) * pc.Mode.z * pc.Mode.y, 0.0, 1.0 ), 0.0, 0.0, 1.0 );
        return;
    }
    outColor = vec4( 1.0 + pc.Params.w * ( lit - 1.0 ), 0.0, 0.0, 1.0 );
}
