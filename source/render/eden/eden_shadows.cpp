#include "shared.h"
#include "eden_shadows.h"
#include "game/game_options.h"
#include "system/cmd_line.h"

#include "render/shaders/builtin/shadowcombine_frag.h"
#include "render/shaders/builtin/shadowslice_frag.h"

#include "render/eden/eden_weather.h"
#include "render/gs_render.h"

namespace {
constexpr uint64_t kShadowPassShaderSet = 0xe674a83899eaeb37ull; // edrender_pfx_shadow
constexpr float kLightDistance = 900.0f;                         // shadow manager +0x464
constexpr uint8_t kSamplerPointClamp = 3;

Vector4f Row( const Matrix4x4f &m, int r ) { return Vector4f( m.m[0][r], m.m[1][r], m.m[2][r], m.m[3][r] ); }
} // namespace

namespace {
struct ShadowConfigEntry {
    const char *pName;
    EdenShadows::Config Config;
};

EdenShadows::Config MakeShadowConfig( float n, float f, float lambda, float b0, float b1, float b2, float b3,
                                      float maxCast ) {
    EdenShadows::Config config;
    config.Near = n;
    config.Far = f;
    config.Lambda = lambda;
    config.Bias[0] = b0;
    config.Bias[1] = b1;
    config.Bias[2] = b2;
    config.Bias[3] = b3;
    config.MaxCastDistance = maxCast;
    return config;
}

// Quality_Settings.cpr, section "// Shadows": SET_SHADOW_CONFIG_SLICES (near,
// far, heuristic weight) / _BIAS (slice near, medium, far, car) /
// _MAX_CAST_DIST. The block is repeated at every quality level with the same
// values; SET_SHADOW_CONFIG_STEREO_CONV (the 3D stereo convergence) is not
// used here. Names registered without a block (AVATAR_CHOICE) and "DEFAULT"
// itself keep the built-in defaults of FUN_00973a30.
const ShadowConfigEntry kShadowConfigs[] = {
    { "DEFAULT", MakeShadowConfig( 0.8f, 32.0f, 0.5f, 0.7f, 1.5f, 1.5f, 1.0f, 150.0f ) },
    { "LOGIN", MakeShadowConfig( 0.8f, 136.0f, 0.5f, 2.5f, 2.5f, 2.5f, 1.0f, 196.0f ) },
    { "FREERIDE", MakeShadowConfig( 4.2f, 275.0f, 0.9f, 0.7f, 2.5f, 2.5f, 1.0f, 736.0f ) },
    { "FREERIDE_COCKPIT", MakeShadowConfig( 2.9f, 275.0f, 1.0f, 0.7f, 2.5f, 2.5f, 1.0f, 736.0f ) },
    { "FREERIDE_HOOD", MakeShadowConfig( 2.3f, 275.0f, 1.0f, 1.1f, 2.5f, 2.5f, 0.5f, 736.0f ) },
    { "FREERIDE_HOOD_1030", MakeShadowConfig( 2.6f, 275.0f, 0.2f, 0.7f, 2.5f, 2.5f, 0.5f, 736.0f ) },
    { "WALKMODE_FIRST_PERSON", MakeShadowConfig( 5.0f, 38.0f, 0.6f, 1.3f, 2.9f, 2.6f, 1.0f, 150.0f ) },
    { "WALKMODE_THIRD_PERSON", MakeShadowConfig( 5.0f, 38.0f, 0.6f, 1.3f, 1.1f, 1.0f, 1.0f, 150.0f ) },
    { "DRESSING", MakeShadowConfig( 5.0f, 5.0f, 0.0f, 1.2f, 1.6f, 2.0f, 0.6f, 10.0f ) },
    { "GARAGE_FIRST_PERSON", MakeShadowConfig( 0.8f, 48.0f, 0.5f, 2.0f, 2.0f, 2.0f, 1.0f, 96.0f ) },
    { "CARDEALER_FIRST_PERSON", MakeShadowConfig( 0.8f, 48.0f, 0.5f, 2.0f, 2.0f, 2.0f, 1.0f, 96.0f ) },
    { "CINE_VERY_FAR", MakeShadowConfig( 0.8f, 500.0f, 0.5f, 2.5f, 2.5f, 2.5f, 1.0f, 500.0f ) },
    { "CINE_VERY_NEAR", MakeShadowConfig( 0.70f, 1.40f, 1.0f, 0.0f, 1.0f, 1.4f, 0.5f, 12.0f ) },
    { "PRB4", MakeShadowConfig( 0.1f, 10.0f, 1.0f, 1.5f, 1.0f, 1.0f, 1.0f, 20.0f ) },
    { "GP", MakeShadowConfig( 0.4f, 10.0f, 0.6f, 0.2f, 1.0f, 1.0f, 1.0f, 20.0f ) },
    { "FAR_REPLAY", MakeShadowConfig( 4.0f, 130.0f, 0.3f, 1.5f, 1.7f, 3.5f, 3.5f, 150.0f ) },
    { "PG", MakeShadowConfig( 1.6f, 90.0f, 0.5f, 2.0f, 2.0f, 2.0f, 2.0f, 150.0f ) },
    { "PM", MakeShadowConfig( 1.0f, 20.0f, 0.5f, 0.3f, 0.5f, 0.7f, 1.0f, 50.0f ) },
    { "PA", MakeShadowConfig( 1.1f, 20.0f, 1.0f, 0.4f, 1.0f, 0.5f, 1.0f, 40.0f ) },
    { "PR", MakeShadowConfig( 0.8f, 20.0f, 1.0f, 0.4f, 1.0f, 1.0f, 1.0f, 30.0f ) },
};

// REGISTER_SHADOW_CONFIG_ALIAS of Quality_Settings.cpr: an alias reuses the
// values of its target (the file only overrides the stereo convergence).
const char *const kShadowConfigAliases[][2] = {
    { "CINE_DEFAULT", "DEFAULT" },
    { "CINE_NEAR", "DEFAULT" },
    { "CINE_MEDIUM", "GARAGE_FIRST_PERSON" },
    { "CINE_FAR", "CINE_VERY_FAR" },
    { "WALKMODE", "DEFAULT" },
    { "WALKMODE_COCKPIT", "DEFAULT" },
    { "WALKMODE_HOOD", "DEFAULT" },
    { "WALKMODE_BUMPER", "DEFAULT" },
    { "LOBBY_FIRST_PERSON", "DEFAULT" },
    { "LOBBY_THIRD_PERSON", "DEFAULT" },
    { "LOBBY", "FREERIDE" },
    { "LOBBY_COCKPIT", "FREERIDE_COCKPIT" },
    { "LOBBY_HOOD", "FREERIDE_HOOD" },
    { "LOBBY_BUMPER", "FREERIDE_HOOD" },
    { "FREERIDE_BUMPER", "FREERIDE_HOOD" },
    { "FREERIDE_NEAR", "FREERIDE" },
    { "FREERIDE_FAR", "FREERIDE" },
    { "FREERIDE_NEAR_0207", "FREERIDE" },
    { "FREERIDE_FAR_0207", "FREERIDE" },
    { "MAP", "FREERIDE" },
    { "GARAGE", "GARAGE_FIRST_PERSON" },
    { "GARAGE_THIRD_PERSON", "GARAGE_FIRST_PERSON" },
    { "GARAGE_COCKPIT", "GARAGE_FIRST_PERSON" },
    { "GARAGE_HOOD", "GARAGE_FIRST_PERSON" },
    { "GARAGE_BUMPER", "GARAGE_FIRST_PERSON" },
    { "CARDEALER", "CARDEALER_FIRST_PERSON" },
    { "CARDEALER_THIRD_PERSON", "CARDEALER_FIRST_PERSON" },
    { "CARDEALER_COCKPIT", "CARDEALER_FIRST_PERSON" },
    { "CARDEALER_HOOD", "CARDEALER_FIRST_PERSON" },
    { "CARDEALER_BUMPER", "CARDEALER_FIRST_PERSON" },
};

const EdenShadows::Config kDefaultShadowConfig = MakeShadowConfig( 0.8f, 32.0f, 0.5f, 0.7f, 1.5f, 1.5f, 1.0f, 150.0f );
} // namespace

EdenShadows::Config EdenShadows::FindConfig( const std::string &name ) {
    std::string key = ToUpper( name );
    for ( uint32_t hop = 0; hop < 4; hop++ ) {
        bool bAliased = false;
        for ( const auto &alias : kShadowConfigAliases ) {
            if ( key == alias[0] ) {
                key = alias[1];
                bAliased = true;
                break;
            }
        }
        if ( !bAliased ) {
            break;
        }
    }
    for ( const ShadowConfigEntry &entry : kShadowConfigs ) {
        if ( key == entry.pName ) {
            return entry.Config;
        }
    }
    OTDU_LOG_DEBUG( "EdenShadows: unknown shadow config '%s': built-in defaults\n", name.c_str() );
    return kDefaultShadowConfig;
}

EdenShadows::EdenShadows() {}

EdenShadows::~EdenShadows() { destroy(); }

bool EdenShadows::initialize( EdenRenderer &renderer, const Config &newConfig ) {
    destroy();
    config = newConfig;
    RenderDevice &device = gpRender->getDevice();
    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        // SHADOW_NEAR / MID / FAR: R32F + depth (FUN_00c32380 / FUN_00c32880).
        slices[i].pTarget =
            device.createRenderTarget( config.Resolution, config.Resolution, VK_FORMAT_R32_SFLOAT, true, "ShadowSlice" );
    }
    const float vertices[] = { -1.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f, 3.0f, -1.0f, 0.0f };
    const uint32_t indices[] = { 0, 1, 2 };
    pVertexBuffer = device.createBuffer( sizeof( vertices ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices );
    pIndexBuffer = device.createBuffer( sizeof( indices ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices );
    mesh.NumStreams = 1;
    mesh.pStreamBuffers[0] = pVertexBuffer;
    mesh.Strides[0] = 12;
    RawVertexAttribute position;
    position.Usage = 0;
    position.Format = VAF_POSITION_R32G32B32_SFLOAT;
    mesh.Attributes.push_back( position );
    mesh.pIndexBuffer = pIndexBuffer;
    mesh.IndexCount = 3;
    mesh.computeLayoutKey();

    // FSShadowMat[i] (FUN_00977e80): spec[36 + i] = 1, spec[40] = 3,
    // blend ONE / ONE / MIN, color write R only, no depth.
    bReady = true;
    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        EdenMaterial &material = passMaterials[i];
        material = EdenMaterial();
        material.Name = "FSShadowMat";
        material.ShaderSetGUID = kShadowPassShaderSet;
        material.Spec[36 + i] = 1;
        material.Spec[40] = 3;
        material.NumSlots = 0;
        for ( uint32_t slot = 0; slot < 16; slot++ ) {
            material.SamplerIndex[slot] = kSamplerPointClamp;
        }
        material.SrcBlend = 2;
        material.DstBlend = 2;
        material.BlendOp = 4;
        material.ColorWriteMask = 1;
        material.CullMode = 1;
        material.bDepthTest = false;
        material.bDepthWrite = false;
        renderer.updateProgram( material );
        bReady = bReady && material.pProgram != nullptr && slices[i].pTarget != nullptr;
        OTDU_LOG_DEBUG( "EdenShadows: slice %u pass %s\n", i,
                        material.pProgram != nullptr ? material.pProgram->Name.c_str() : "-" );
    }
    return bReady;
}

void EdenShadows::setConfig( const Config &newConfig ) {
    const uint32_t resolution = config.Resolution;
    config = newConfig;
    config.Resolution = resolution;
}

void EdenShadows::destroy() {
    if ( gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        for ( Slice &slice : slices ) {
            device.destroyRenderTarget( slice.pTarget );
            slice.pTarget = nullptr;
        }
        device.destroyRenderTarget( pScreenShadow );
        for ( RenderTarget *&pTarget : pSliceScreens ) {
            device.destroyRenderTarget( pTarget );
            pTarget = nullptr;
        }
        combinePass.destroy();
        slicePass.destroy();
        device.destroyBuffer( pVertexBuffer );
        device.destroyBuffer( pIndexBuffer );
    }
    pScreenShadow = nullptr;
    pVertexBuffer = pIndexBuffer = nullptr;
    bReady = false;
}

void EdenShadows::setupSceneConstants( EdenSceneParams &scene, const WeatherPreset *pWeather ) const {
    // Shadow_SlicesDist.w: weather sun_shadow_opacity (manager +0x431 set).
    const float opacity = pWeather != nullptr ? pWeather->getFloat( "sun_shadow_opacity", 1.0f ) : 1.0f;
    static const bool bLogOpacity = CmdLine::HasArg( "-logshadows" );
    if ( bLogOpacity ) {
        OTDU_LOG_INFO( "ShadowFit: weather '%s' sun_shadow_opacity %.3f\n", pWeather != nullptr ? pWeather->Name.c_str() : "-",
                       opacity );
    }
    scene.Vectors["Shadow_SlicesDist"] = Vector4f( splits[0], splits[1], splits[2], opacity );
    const float texel = 1.0f / static_cast<float>( config.Resolution );
    scene.Vectors["Shadow_RTSizes01"] = Vector4f( texel, texel, texel, texel );
    scene.Vectors["Shadow_RTSizes23"] = Vector4f( texel, texel, texel, texel );
    scene.Vectors["Shadow_ExtraParams"] =
        Vector4f( -1.0f / scene.Projection.m[0][0], -1.0f / scene.Projection.m[1][1], 0.0f, 0.0f );
}

void EdenShadows::renderCasters( EdenRenderer &renderer, const EdenSceneParams &scene,
                                 const DrawCasters &drawCasters ) {
    if ( !bReady ) {
        return;
    }
    static const bool bLogFit = CmdLine::HasArg( "-logshadows" );
    RenderDevice &device = gpRender->getDevice();
    // Split distances (FUN_00973890).
    const float n = config.Near;
    const float f = config.Far;
    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        const float k = static_cast<float>( i + 1 ) / kNumSlices;
        splits[i] = config.Lambda * n * std::pow( f / n, k ) + ( 1.0f - config.Lambda ) * ( n + ( f - n ) * k );
    }
    const Matrix4x4f cameraWorld = scene.View.inverse();
    const Vector3f right( cameraWorld.m[0][0], cameraWorld.m[0][1], cameraWorld.m[0][2] );
    const Vector3f up( cameraWorld.m[1][0], cameraWorld.m[1][1], cameraWorld.m[1][2] );
    const Vector3f forward( -cameraWorld.m[2][0], -cameraWorld.m[2][1], -cameraWorld.m[2][2] );
    const Vector3f position = cameraWorld.getTranslation();
    const float tanX = 1.0f / scene.Projection.m[0][0];
    const float tanY = 1.0f / scene.Projection.m[1][1];
    const float cameraNear = scene.Projection.m[3][2] / scene.Projection.m[2][2];
    const Vector3f light = scene.SunDirection.normalized();

    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        Slice &slice = slices[i];
        // Slice frustum (FUN_00976300): camera near or the previous split.
        const float sliceNear = i == 0 ? cameraNear : splits[i - 1];
        const float sliceFar = splits[i];
        Vector3f corners[8];
        Vector3f center( 0.0f, 0.0f, 0.0f );
        for ( uint32_t c = 0; c < 8; c++ ) {
            const float d = ( c & 4 ) ? sliceFar : sliceNear;
            const float sx = ( c & 1 ) ? 1.0f : -1.0f;
            const float sy = ( c & 2 ) ? 1.0f : -1.0f;
            corners[c] = position + forward * d + right * ( sx * tanX * d ) + up * ( sy * tanY * d );
            center = center + corners[c] * 0.125f;
        }
        // Light camera 900 m back along the sun direction, up (0, 1, 0).
        const Vector3f lightUp = std::abs( light.y ) > 0.9999f ? Vector3f( 1.0f, 0.0f, 0.0f ) : Vector3f( 0.0f, 1.0f, 0.0f );
        // Stabilise the slice: a bounding sphere of the frustum slice (does not change when the
        // camera turns) whose centre is snapped to the shadow texel grid of a light space that
        // only rotates with the sun, so a small camera move does not slide the texels under the
        // casters (the shadow edges crawled / flipped on the pool and the faces at every small
        // camera move: the previous snap was taken in a view centred on the slice centre itself,
        // where it is always 0) [INF]. -shadowfit restores the per-frame tight fit.
        static const bool bTightFit = CmdLine::HasArg( "-shadowfit" );
        Vector3f sliceCenter = center;
        float radius = 0.0f;
        if ( !bTightFit ) {
            for ( const Vector3f &corner : corners ) {
                radius = Max( radius, ( corner - center ).length() );
            }
            radius = std::ceil( radius * 16.0f ) / 16.0f;
            const float texelSize = 2.0f * radius / static_cast<float>( config.Resolution );
            const Matrix4x4f rotation = Matrix4x4f::LookAt( Vector3f( 0.0f, 0.0f, 0.0f ), light, lightUp );
            const Vector3f lightCenter = rotation.transformPoint( center );
            const Vector3f snapped( std::floor( lightCenter.x / texelSize ) * texelSize,
                                    std::floor( lightCenter.y / texelSize ) * texelSize, lightCenter.z );
            sliceCenter = rotation.inverse().transformPoint( snapped );
        }
        const Vector3f eye = sliceCenter - light * kLightDistance;
        const Matrix4x4f view = Matrix4x4f::LookAt( eye, sliceCenter, lightUp );
        Vector3f boundsMin( 1e30f, 1e30f, 1e30f );
        Vector3f boundsMax( -1e30f, -1e30f, -1e30f );
        if ( !bTightFit ) {
            boundsMin = Vector3f( -radius, -radius, -kLightDistance - radius );
            boundsMax = Vector3f( radius, radius, -kLightDistance + radius );
        } else {
            for ( const Vector3f &corner : corners ) {
                const Vector3f v = view.transformPoint( corner );
                boundsMin = Vector3f( Min( boundsMin.x, v.x ), Min( boundsMin.y, v.y ), Min( boundsMin.z, v.z ) );
                boundsMax = Vector3f( Max( boundsMax.x, v.x ), Max( boundsMax.y, v.y ), Max( boundsMax.z, v.z ) );
            }
        }
        // Casters up to MAX_CAST_DIST toward the sun [INF: the original
        // intersects the range with the caster bounds]. The quality profiles
        // scale it: the FREERIDE config's 736 m is the largest CPU cost of the
        // shadow pass (docs/PERF_PLAN.md §1, session 73).
        const float castScale =
            static_cast<float>( Clamp( GetGameOptions().ShadowDistance, 50, 150 ) ) / 100.0f;
        const float depthNear = Max( 1.0f, -boundsMax.z - config.MaxCastDistance * castScale );
        const float depthFar = -boundsMin.z + 1.0f;
        const float width = boundsMax.x - boundsMin.x;
        const float height = boundsMax.y - boundsMin.y;
        Matrix4x4f projection = Matrix4x4f::Identity();
        projection.m[0][0] = 2.0f / width;
        projection.m[1][1] = 2.0f / height;
        projection.m[2][2] = -1.0f / ( depthFar - depthNear );
        projection.m[3][0] = -( boundsMin.x + boundsMax.x ) / width;
        projection.m[3][1] = -( boundsMin.y + boundsMax.y ) / height;
        projection.m[3][2] = -depthNear / ( depthFar - depthNear );
        slice.LightViewProjection = projection.mulPrecise( view );
        slice.LightViewProjectionD = Matrix4x4d::FromFloat( slice.LightViewProjection );
        if ( !bTightFit ) {
            // The same transform in double, snap included: x = (s.p - snapped) / r keeps the
            // texel phase of every world point fixed while the camera moves. In float, at island
            // coordinates (36 km at the house, ~4 mm float spacing vs a ~9 mm texel) the light
            // matrix re-rounded the grid by a fraction of a texel at some camera poses, and every
            // flat sunlit surface (a wall, the terrace floor, the banner) flipped between lit and
            // self-shadowed at once.
            const double fx = light.x, fy = light.y, fz = light.z;
            const double ux0 = lightUp.x, uy0 = lightUp.y, uz0 = lightUp.z;
            double sx = fy * uz0 - fz * uy0, sy = fz * ux0 - fx * uz0, sz = fx * uy0 - fy * ux0;
            const double sLength = std::sqrt( sx * sx + sy * sy + sz * sz );
            sx /= sLength, sy /= sLength, sz /= sLength;
            const double ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
            // Slice centre from the camera in double.
            const Matrix4x4d cameraD = Matrix4x4d::RigidInverse( scene.View );
            const double px = cameraD.m[3][0], py = cameraD.m[3][1], pz = cameraD.m[3][2];
            const double dMid = 0.5 * ( static_cast<double>( sliceNear ) + sliceFar );
            const double cx = px - cameraD.m[2][0] * dMid, cy = py - cameraD.m[2][1] * dMid, cz = pz - cameraD.m[2][2] * dMid;
            const double texel = 2.0 * radius / static_cast<double>( config.Resolution );
            const double lsx = std::floor( ( sx * cx + sy * cy + sz * cz ) / texel ) * texel;
            const double lsy = std::floor( ( ux * cx + uy * cy + uz * cz ) / texel ) * texel;
            const double range = static_cast<double>( depthFar ) - depthNear;
            const double depthOffset = -( fx * cx + fy * cy + fz * cz ) + kLightDistance - depthNear;
            Matrix4x4d &m = slice.LightViewProjectionD;
            m.m[0][0] = sx / radius, m.m[1][0] = sy / radius, m.m[2][0] = sz / radius, m.m[3][0] = -lsx / radius;
            m.m[0][1] = ux / radius, m.m[1][1] = uy / radius, m.m[2][1] = uz / radius, m.m[3][1] = -lsy / radius;
            m.m[0][2] = fx / range, m.m[1][2] = fy / range, m.m[2][2] = fz / range, m.m[3][2] = depthOffset / range;
            m.m[0][3] = m.m[1][3] = m.m[2][3] = 0.0, m.m[3][3] = 1.0;
            slice.LightViewProjection = m.toFloat();
        }
        slice.DepthRange = depthFar - depthNear;
        slice.TexelSize = Max( width, height ) / static_cast<float>( config.Resolution );
        if ( bLogFit && i == 0 ) {
            const Vector3f camForward( -scene.View.m[2][0], -scene.View.m[2][1], -scene.View.m[2][2] );
            OTDU_LOG_INFO(
                "ShadowFit: cam fwd (%.2f %.2f %.2f) light (%.2f %.2f %.2f) slice0 w %.1f h %.1f depth %.1f..%.1f "
                "texel %.3f\n",
                camForward.x, camForward.y, camForward.z, light.x, light.y, light.z, width, height, depthNear,
                depthFar, slice.TexelSize );
            OTDU_LOG_INFO( "ShadowFit: config %.2f..%.2f lambda %.2f splits %.2f %.2f %.2f camera near %.3f\n",
                           config.Near, config.Far, config.Lambda, splits[0], splits[1], splits[2], cameraNear );
        }

        EdenSceneParams lightScene = scene;
        lightScene.View = view;
        lightScene.Projection = projection;
        lightScene.CameraPosition = eye;
        lightScene.CameraDirection = light;
        lightScene.Width = lightScene.Height = config.Resolution;
        lightScene.bMirroredView = false;
        device.beginRenderTarget( slice.pTarget, true, ColorRGBA( 1.0f, 1.0f, 1.0f, 1.0f ) );
        renderer.setShadowCasterMode( true );
        renderer.beginScene( lightScene );
        renderer.setPreciseViewProjection( &slice.LightViewProjectionD );
        drawCasters( renderer, lightScene );
        renderer.setPreciseViewProjection( nullptr );
        if ( bLogFit ) {
            OTDU_LOG_INFO( "ShadowFit: slice %u: %u caster draws\n", i, renderer.getNumDrawCalls() );
        }
        renderer.endScene();
        renderer.setShadowCasterMode( false );
        device.endRenderTarget();
    }
}

GPUTexture *EdenShadows::resolve( EdenRenderer &renderer, const EdenSceneParams &scene, GPUTexture *pViewPos,
                                  const WeatherPreset *pWeather ) {
    if ( !bReady || pViewPos == nullptr ) {
        return nullptr;
    }
    RenderDevice &device = gpRender->getDevice();
    if ( pScreenShadow == nullptr || pScreenShadow->Width != scene.Width || pScreenShadow->Height != scene.Height ) {
        device.destroyRenderTarget( pScreenShadow );
        pScreenShadow =
            device.createRenderTarget( scene.Width, scene.Height, VK_FORMAT_R8G8B8A8_UNORM, false, "ShadowRealTime" );
        for ( RenderTarget *&pTarget : pSliceScreens ) {
            device.destroyRenderTarget( pTarget );
            pTarget = device.createRenderTarget( scene.Width, scene.Height, VK_FORMAT_R8G8B8A8_UNORM, false,
                                                 "ShadowSliceScreen" );
        }
    }
    EdenSceneParams passScene = scene;
    setupSceneConstants( passScene, pWeather );
    renderer.beginScene( passScene );
    EdenDrawParams params;
    std::vector<Vector4f> viewToLight( 4, Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) );
    std::vector<Vector4f> uvMatrix( 8, Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) );
    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        // View -> light clip; uv = (0.5 x + 0.5, 0.5 - 0.5 y) (Vulkan
        // samples at texel centers: no D3D9 half texel).
        // In double: both matrices carry the island-scale translation (36 km at the
        // house); the float product cancelled to ~cm, the size of the bias, and the
        // flat floor self-shadowed all at once at some camera angles.
        const Matrix4x4f viewToClip =
            ( slices[i].LightViewProjectionD * Matrix4x4d::RigidInverse( scene.View ) ).toFloat();
        Vector4f depthRow = Row( viewToClip, 2 );
        // SET_SHADOW_CONFIG_BIAS, in texels of the slice [INF: the original
        // adds it to the w of the light row].
        // Shadow acne (the ground self-shadowing in moire arcs, flickering as the camera moves): the
        // bias read from the config was too small; 3x clears it (measured: ground brightness std
        // over a slow pan 15.3 at 1x, 3.6 at 1.5x, 1.2 at 2x and above). -shadowbias <percent>.
        static const float biasScale = static_cast<float>( CmdLine::GetArgInt( "-shadowbias", 300 ) ) * 0.01f;
        // A floor in world units: the cinematic configs (PR / PA / PM: 20 m far, slice 0 texel
        // ~4 mm) give a 3-4 mm bias, under the depth error of a grazing wall, so the party's
        // building walls, DJ decks and table cloth flipped between lit and self-shadowed from
        // frame to frame as the dancers' casters changed the map (intro party, 16-frame lit /
        // shadow state flips: 24 % of the frame at the default bias, 5 % with the DEFAULT config's
        // ~4 cm). -shadowminbias <cm> (default 5, 0 = off).
        static const float minBias = static_cast<float>( CmdLine::GetArgInt( "-shadowminbias", 5 ) ) * 0.01f;
        const float worldBias = Max( biasScale * config.Bias[i] * slices[i].TexelSize, minBias );
        depthRow.w -= worldBias / slices[i].DepthRange;
        viewToLight[i] = depthRow;
        const Vector4f row0 = Row( viewToClip, 0 );
        const Vector4f row1 = Row( viewToClip, 1 );
        const Vector4f row3 = Row( viewToClip, 3 );
        uvMatrix[2 * i] = Vector4f( 0.5f * row0.x + 0.5f * row3.x, 0.5f * row0.y + 0.5f * row3.y,
                                    0.5f * row0.z + 0.5f * row3.z, 0.5f * row0.w + 0.5f * row3.w );
        uvMatrix[2 * i + 1] = Vector4f( -0.5f * row1.x + 0.5f * row3.x, -0.5f * row1.y + 0.5f * row3.y,
                                        -0.5f * row1.z + 0.5f * row3.z, -0.5f * row1.w + 0.5f * row3.w );
    }
    params.Arrays["ViewToLight__array4"] = viewToLight;
    params.Arrays["UVMatrix__array8"] = uvMatrix;
    params.Vectors["SlicePhaseScale"] = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    params.Vectors["SliceUvOffset"] = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    params.Vectors["FadeAlphaFactor"] = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
    params.Vectors["vars"] =
        Vector4f( 0.0f, 0.0f, static_cast<float>( scene.Width ), static_cast<float>( scene.Height ) );
    // Each slice pass (MIN blend, cleared to 1) in its own buffer; the
    // original restricts them with the stencil (FSShadowMatStencil, geometry
    // not resolved): emulated by the combine pass, which keeps for every
    // pixel the slice of its view depth (none beyond the last split).
    // -ownshadowpass <mode>: the slice passes with OpenTDU2's GLSL version of
    // the same law (resources/shaders/shadowslice.frag) instead of the
    // translated FSShadowMat shaders (A/B).
    static const bool bOwnPass = CmdLine::HasArg( "-ownshadowpass" );
    static const float ownMode = static_cast<float>( CmdLine::GetArgInt( "-ownshadowpass", 0 ) );
    if ( bOwnPass ) {
        // -shadowmargin <k>: the slice passes show the depth margin (map - receiver) x k per metre.
        static const float debugScale = static_cast<float>( CmdLine::GetArgInt( "-shadowmargin", 0 ) );
        const float opacity = passScene.Vectors["Shadow_SlicesDist"].w;
        for ( uint32_t i = 0; i < kNumSlices; i++ ) {
            device.beginRenderTarget( pSliceScreens[i], true, ColorRGBA( 1.0f, 0.0f, 0.0f, 1.0f ) );
            const float push[20] = { viewToLight[i].x, viewToLight[i].y, viewToLight[i].z, viewToLight[i].w,
                                     uvMatrix[2 * i].x, uvMatrix[2 * i].y, uvMatrix[2 * i].z, uvMatrix[2 * i].w,
                                     uvMatrix[2 * i + 1].x, uvMatrix[2 * i + 1].y, uvMatrix[2 * i + 1].z, uvMatrix[2 * i + 1].w,
                                     scene.Projection.m[0][0], scene.Projection.m[1][1],
                                     static_cast<float>( config.Resolution ), opacity, ownMode, debugScale, slices[i].DepthRange, 0.0f };
            GPUTexture *textures[2] = { pViewPos, &slices[i].pTarget->Color };
            slicePass.draw( device, kSPIRV_shadowslice_frag, sizeof( kSPIRV_shadowslice_frag ), textures, 2, push,
                            sizeof( push ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Point );
            device.endRenderTarget();
        }
    }
    for ( uint32_t i = 0; i < kNumSlices && !bOwnPass; i++ ) {
        device.beginRenderTarget( pSliceScreens[i], true, ColorRGBA( 1.0f, 0.0f, 0.0f, 1.0f ) );
        renderer.beginScene( passScene );
        EdenMaterial &material = passMaterials[i];
        material.pTextures[0] = pViewPos;
        material.pTextures[1 + i] = &slices[i].pTarget->Color;
        renderer.draw( mesh, material, Matrix4x4f::Identity(), &params );
        renderer.endScene();
        device.endRenderTarget();
    }
    device.beginRenderTarget( pScreenShadow, true, ColorRGBA( 1.0f, 0.0f, 0.0f, 1.0f ) );
    GPUTexture *textures[4] = { pViewPos, &pSliceScreens[0]->Color, &pSliceScreens[1]->Color,
                                &pSliceScreens[2]->Color };
    static const float debugSlice = static_cast<float>( CmdLine::GetArgInt( "-shadowslice", 0 ) );
    const float combineParams[4] = { splits[0], splits[1], splits[2], debugSlice };
    combinePass.draw( device, kSPIRV_shadowcombine_frag, sizeof( kSPIRV_shadowcombine_frag ), textures, 4,
                      combineParams, sizeof( combineParams ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Point );
    device.endRenderTarget();
    return &pScreenShadow->Color;
}
