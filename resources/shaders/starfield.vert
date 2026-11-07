#version 450
// OpenTDU2 night sky starfield (port-native: the retail star vertex stream
// is generated in encrypted code, weather_spec 3 - see docs/re/weather_spec.md).
// One point per star on a large sphere around the camera; the vertex stage
// only transforms. Brightness / tint / twinkle phase are per star attributes.
layout( location = 0 ) in vec3 inPosition; // world space (direction * radius)
layout( location = 1 ) in vec2 inStar;     // x: brightness 0..1, y: twinkle phase

layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 Params; // x: master alpha (Skydome_starsPercent), y: scene time
} pc;

layout( location = 0 ) out float outBrightness;
layout( location = 1 ) out float outTwinkle;

void main() {
    outBrightness = inStar.x;
    outTwinkle = sin( pc.Params.y * 1.7 + inStar.y * 6.2831853 );
    gl_Position = pc.ViewProjection * vec4( inPosition, 1.0 );
    gl_PointSize = 2.0 + 3.0 * inStar.x;
}
