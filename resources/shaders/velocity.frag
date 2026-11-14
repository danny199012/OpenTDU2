#version 450
// OpenTDU2 replacement shader: the camera-motion velocity buffer that the
// temporal upscalers (FSR 3, XeSS, DLSS) read. For every pixel it rebuilds the
// view space position from the scene depth, carries it into the previous
// frame's clip space (previous projection * previous view * inverse of the
// current view, all UNjittered) and writes where that point was on screen:
//   RG = previous uv - current uv   (uv units, 0..1 across the render target)
// Covers the static world and any camera motion. Moving meshes (cars, traffic, wheels) come from the
// velobject pass and override it where they are the visible surface.
layout( set = 0, binding = 0 ) uniform sampler2D uDepth;
layout( set = 0, binding = 1 ) uniform sampler2D uObjectVelocity; // velobject pass (moving meshes)
layout( set = 0, binding = 2 ) uniform sampler2D uObjectDepth;
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
    // A moving mesh (car, wheels) that is the visible surface here: its own
    // motion replaces the camera-only one.
    float objectDepth = texture( uObjectDepth, inTexcoord ).r;
    if ( objectDepth < 0.9999999 ) {
        float objectDistance = zNear * zFar / ( zFar - objectDepth * ( zFar - zNear ) );
        if ( objectDistance <= distanceToCamera + max( 0.05, 0.004 * distanceToCamera ) ) {
            outColor = vec4( texture( uObjectVelocity, inTexcoord ).rg, 0.0, 0.0 );
            return;
        }
    }
    const vec4 previousClip = pc.CurrentToPrevious * vec4( viewPosition, 1.0 );
    const vec2 previousNdc = previousClip.xy / previousClip.w;
    const vec2 previousUv = vec2( previousNdc.x * 0.5 + 0.5, 0.5 - previousNdc.y * 0.5 );
    outColor = vec4( previousUv - inTexcoord, 0.0, 0.0 );
}
