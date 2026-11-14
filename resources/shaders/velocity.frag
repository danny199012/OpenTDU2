#version 450
// OpenTDU2 replacement shader: the camera-motion velocity buffer that the
// temporal upscalers (FSR 3, XeSS, DLSS) read. For every pixel it rebuilds the
// view space position from the scene depth, carries it into the previous
// frame's clip space (previous projection * previous view * inverse of the
// current view, all UNjittered) and writes where that point was on screen:
//   RG = previous uv - current uv   (uv units, 0..1 across the render target)
// Covers the static world and any camera motion. Moving objects (cars, traffic,
// skinned meshes) need their own previous transforms and are not in this pass.
layout( set = 0, binding = 0 ) uniform sampler2D uDepth;
layout( push_constant ) uniform PushConstants {
    mat4 CurrentToPrevious; // projPrev * viewPrev * inverse( viewCur )
    vec4 Lens;              // x: 1 / proj[0][0] (tan half fov * aspect), y: 1 / proj[1][1], z: near, w: far
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

void main() {
    float depth = texture( uDepth, inTexcoord ).r;
    const float zNear = pc.Lens.z;
    const float zFar = pc.Lens.w;
    // Sky (the dome sits on the far plane): treat as very far, so only the
    // camera rotation moves it.
    const float distanceToCamera = depth >= 0.9999999 ? zFar : zNear * zFar / ( zFar - depth * ( zFar - zNear ) );
    const vec2 ndc = vec2( inTexcoord.x * 2.0 - 1.0, 1.0 - inTexcoord.y * 2.0 );
    // Right handed view space, the camera looks down -Z.
    const vec3 viewPosition = vec3( ndc.x * pc.Lens.x * distanceToCamera, ndc.y * pc.Lens.y * distanceToCamera,
                                    -distanceToCamera );
    const vec4 previousClip = pc.CurrentToPrevious * vec4( viewPosition, 1.0 );
    const vec2 previousNdc = previousClip.xy / previousClip.w;
    const vec2 previousUv = vec2( previousNdc.x * 0.5 + 0.5, 0.5 - previousNdc.y * 0.5 );
    outColor = vec4( previousUv - inTexcoord, 0.0, 0.0 );
}
