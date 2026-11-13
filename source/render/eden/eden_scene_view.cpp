#include "shared.h"
#include "eden_scene_view.h"

#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "game/game_options.h"
#include "render/shaders/builtin/blit_frag.h"
#include "render/shaders/builtin/depthmax_frag.h"
#include "render/shaders/builtin/rt_ao_combine_frag.h"
#include "render/shaders/builtin/upscale_frag.h"
#include "render/shaders/builtin/windscreen_frag.h"
#include "render/shaders/builtin/viewpos_frag.h"
#include "system/cmd_line.h"
#include "debug/coords_overlay.h"

#include <cfloat>
#include <chrono>
#include <fstream>

namespace {
// -occlusionprobe <f1,f2,...>: on those scene frames, how many draws of the
// depth pre-pass an occlusion cull could have skipped. Each draw's world box
// (EdenRenderer::ProbeDraw) is projected; it is hidden when its NEAREST depth
// is behind the FARTHEST depth of the pre-pass in the screen rectangle it
// covers (the conservative hierarchical-Z test, on 8 x 8 pixel tiles; an object
// can never hide itself: its own surfaces are behind its box's nearest point).
// It measures, it does not cull. The result is logged as "[occlusion]" and a
// _occlusion_f<frame>.ppm shows the depth with the hidden boxes outlined.
struct OcclusionProbe {
    std::vector<EdenRenderer::ProbeDraw> Draws;
    Matrix4x4f ViewProjection;
    GPUBuffer *pDepth = nullptr;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Slot = 0;
    uint64_t DeviceFrame = 0;
    uint64_t SceneFrame = 0;
    bool bPending = false;
};

void EvaluateOcclusionProbe( OcclusionProbe &probe ) {
    const uint32_t width = probe.Width, height = probe.Height;
    const float *pDepth = static_cast<const float *>( probe.pDepth->pMapped );
    constexpr uint32_t kTile = 8;
    const uint32_t tilesX = ( width + kTile - 1 ) / kTile, tilesY = ( height + kTile - 1 ) / kTile;
    std::vector<float> tileMax( static_cast<size_t>( tilesX ) * tilesY, 0.0f );
    uint64_t skyPixels = 0;
    for ( uint32_t y = 0; y < height; y++ ) {
        for ( uint32_t x = 0; x < width; x++ ) {
            const float depth = pDepth[static_cast<size_t>( y ) * width + x];
            skyPixels += depth >= 1.0f ? 1 : 0;
            float &tile = tileMax[static_cast<size_t>( y / kTile ) * tilesX + x / kTile];
            tile = Max( tile, depth );
        }
    }
    struct Counts {
        uint32_t Draws = 0, Hidden = 0, Offscreen = 0, NearPlane = 0;
    } counts[3];
    std::vector<std::array<int32_t, 4>> hiddenRects;
    const Matrix4x4f &vp = probe.ViewProjection;
    for ( const EdenRenderer::ProbeDraw &draw : probe.Draws ) {
        Counts &count = counts[Min<uint32_t>( draw.Kind, 2 )];
        count.Draws++;
        if ( draw.Kind == 0 ) {
            continue; // no box: counted as drawn
        }
        float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX, minZ = FLT_MAX;
        bool bCrossesNear = false;
        for ( int32_t corner = 0; corner < 8; corner++ ) {
            const float p[3] = { ( corner & 1 ) ? draw.Max.x : draw.Min.x, ( corner & 2 ) ? draw.Max.y : draw.Min.y,
                                 ( corner & 4 ) ? draw.Max.z : draw.Min.z };
            float clip[4];
            for ( int32_t j = 0; j < 4; j++ ) {
                clip[j] = p[0] * vp.m[0][j] + p[1] * vp.m[1][j] + p[2] * vp.m[2][j] + vp.m[3][j];
            }
            if ( clip[3] <= 0.01f ) {
                bCrossesNear = true;
                break;
            }
            const float inverse = 1.0f / clip[3];
            minX = Min( minX, clip[0] * inverse );
            maxX = Max( maxX, clip[0] * inverse );
            minY = Min( minY, clip[1] * inverse );
            maxY = Max( maxY, clip[1] * inverse );
            minZ = Min( minZ, clip[2] * inverse );
        }
        if ( bCrossesNear ) {
            count.NearPlane++;
            continue;
        }
        // D3D clip space and the flipped viewport: NDC y = +1 is pixel row 0.
        const int32_t x0 = static_cast<int32_t>( std::floor( ( minX * 0.5f + 0.5f ) * width ) );
        const int32_t x1 = static_cast<int32_t>( std::floor( ( maxX * 0.5f + 0.5f ) * width ) );
        const int32_t y0 = static_cast<int32_t>( std::floor( ( 0.5f - maxY * 0.5f ) * height ) );
        const int32_t y1 = static_cast<int32_t>( std::floor( ( 0.5f - minY * 0.5f ) * height ) );
        if ( x1 < 0 || y1 < 0 || x0 >= static_cast<int32_t>( width ) || y0 >= static_cast<int32_t>( height ) ||
             minZ > 1.0f ) {
            count.Offscreen++;
            continue;
        }
        const int32_t tx0 = Max( x0, 0 ) / static_cast<int32_t>( kTile );
        const int32_t ty0 = Max( y0, 0 ) / static_cast<int32_t>( kTile );
        const int32_t tx1 = Min( x1, static_cast<int32_t>( width ) - 1 ) / static_cast<int32_t>( kTile );
        const int32_t ty1 = Min( y1, static_cast<int32_t>( height ) - 1 ) / static_cast<int32_t>( kTile );
        float farthest = 0.0f;
        for ( int32_t ty = ty0; ty <= ty1 && farthest < minZ; ty++ ) {
            for ( int32_t tx = tx0; tx <= tx1; tx++ ) {
                farthest = Max( farthest, tileMax[static_cast<size_t>( ty ) * tilesX + tx] );
            }
        }
        if ( minZ > farthest ) {
            count.Hidden++;
            hiddenRects.push_back( { x0, y0, x1, y1 } );
        }
    }
    const uint32_t total = counts[0].Draws + counts[1].Draws + counts[2].Draws;
    const uint32_t hidden = counts[1].Hidden + counts[2].Hidden;
    OTDU_LOG_INFO( "[occlusion] scene frame %llu: %u pre-pass draws, %u hidden (%.1f %%) - models %u/%u hidden "
                   "(%u off-screen box, %u at the near plane), .pmi instances %u/%u hidden (%u off-screen, %u near), "
                   "%u without a box (terrain, batches); sky %.1f %% of the pixels\n",
                   static_cast<unsigned long long>( probe.SceneFrame ), total, hidden,
                   total > 0 ? 100.0 * hidden / total : 0.0, counts[1].Hidden, counts[1].Draws, counts[1].Offscreen,
                   counts[1].NearPlane, counts[2].Hidden, counts[2].Draws, counts[2].Offscreen, counts[2].NearPlane,
                   counts[0].Draws, 100.0 * static_cast<double>( skyPixels ) / ( static_cast<double>( width ) * height ) );
    // The picture: depth (near bright), hidden boxes outlined in red.
    std::vector<uint8_t> rgb( static_cast<size_t>( width ) * height * 3 );
    for ( size_t i = 0; i < static_cast<size_t>( width ) * height; i++ ) {
        const float depth = pDepth[i];
        const uint8_t value = depth >= 1.0f ? 0 : static_cast<uint8_t>( 255.0f * Clamp( ( 1.0f - depth ) * 40.0f, 0.1f, 1.0f ) );
        rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = value;
    }
    auto plot = [&]( int32_t x, int32_t y ) {
        if ( x >= 0 && y >= 0 && x < static_cast<int32_t>( width ) && y < static_cast<int32_t>( height ) ) {
            uint8_t *pPixel = &rgb[( static_cast<size_t>( y ) * width + x ) * 3];
            pPixel[0] = 255;
            pPixel[1] = pPixel[2] = 0;
        }
    };
    for ( const auto &rect : hiddenRects ) {
        for ( int32_t x = rect[0]; x <= rect[2]; x++ ) {
            plot( x, rect[1] );
            plot( x, rect[3] );
        }
        for ( int32_t y = rect[1]; y <= rect[3]; y++ ) {
            plot( rect[0], y );
            plot( rect[2], y );
        }
    }
    char path[64];
    snprintf( path, sizeof( path ), "_occlusion_f%llu.ppm", static_cast<unsigned long long>( probe.SceneFrame ) );
    std::ofstream stream( path, std::ios::binary );
    if ( stream.is_open() ) {
        stream << "P6\n" << width << " " << height << "\n255\n";
        stream.write( reinterpret_cast<const char *>( rgb.data() ), static_cast<std::streamsize>( rgb.size() ) );
    }
}
} // namespace

namespace {
// -logscenetimes: CPU time of each step of renderOffscreen, averaged and
// logged every 120 frames (the [perf] "offscreen" figure, split).
struct SceneTimes {
    enum eStep { Env, Ocean, Prepass, Shadows, Planar, Opaque, Blended, Post, Count };
    double Sum[Count] = {};
    // -logrecordtimes: the part of each step spent in EdenRenderer::recordDraw
    // and the draws it recorded (the rest is the scene traversal).
    double Record[Count] = {};
    uint64_t Draws[Count] = {};
    double LastRecord = 0.0;
    uint64_t LastDraws = 0;
    const EdenRenderer *pRenderer = nullptr;
    uint32_t Frames = 0;
    std::chrono::steady_clock::time_point Last;
    void start( const EdenRenderer *pEden ) {
        Last = std::chrono::steady_clock::now();
        pRenderer = pEden;
        if ( pRenderer != nullptr ) {
            LastRecord = pRenderer->getRecordMsTotal();
            LastDraws = pRenderer->getRecordCountTotal();
        }
    }
    void mark( eStep step ) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        Sum[step] += std::chrono::duration<double, std::milli>( now - Last ).count();
        Last = now;
        if ( pRenderer != nullptr ) {
            const double record = pRenderer->getRecordMsTotal();
            const uint64_t draws = pRenderer->getRecordCountTotal();
            Record[step] += record - LastRecord;
            Draws[step] += draws - LastDraws;
            LastRecord = record;
            LastDraws = draws;
        }
    }
    void endFrame() {
        if ( ++Frames < 120 ) {
            return;
        }
        OTDU_LOG_INFO( "[perf] scene: env %.1f  ocean %.1f  prepass %.1f  shadows %.1f  planar %.1f  opaque %.1f  "
                       "blended %.1f  post %.1f ms\n",
                       Sum[Env] / Frames, Sum[Ocean] / Frames, Sum[Prepass] / Frames, Sum[Shadows] / Frames,
                       Sum[Planar] / Frames, Sum[Opaque] / Frames, Sum[Blended] / Frames, Sum[Post] / Frames );
        static const bool bLogRecord = CmdLine::HasArg( "-logrecordtimes" );
        if ( bLogRecord ) {
            static const char *kNames[Count] = { "env", "ocean", "prepass", "shadows", "planar", "opaque", "blended", "post" };
            std::string line;
            char part[96];
            for ( uint32_t i = 0; i < Count; i++ ) {
                snprintf( part, sizeof( part ), "  %s %.1f/%.1f ms %llu dr", kNames[i], Record[i] / Frames, Sum[i] / Frames,
                          static_cast<unsigned long long>( Draws[i] / Frames ) );
                line += part;
            }
            OTDU_LOG_INFO( "[perf] record/pass:%s\n", line.c_str() );
        }
        *this = SceneTimes();
    }
};
SceneTimes gSceneTimes;
} // namespace

namespace {
constexpr uint32_t kEnvironmentSize = 256;
constexpr uint64_t kTD_Building = 0x183157b81c6cff57ull;
constexpr uint64_t kTD_Furnitures = 0xd5013ed0db79afafull;
constexpr uint64_t kTD_FurnituresAlt = 0xd5013ed0dcacafafull;
// The retail spot-scene arrays (63 exterior / 64 interior). The streamed
// island registers far more building materials across its loaded sectors, and
// the port only uses the value to update the NIGHTLMAP parameter, so the
// exterior cap is raised to a loaded world's worth (the retail limit is kept
// in the comment for the reverse-engineering trail).
constexpr uint32_t kMaxExteriorLightmaps = 512; // FUN_00981870 (< 0x3f) per spot scene
constexpr uint32_t kMaxInteriorLightmaps = 64;  // FUN_00a4d7e0 (<= 0x3f)
const uint64_t kNightLightmap = GetIdentifier64bit( "NIGHTLMAP" );
} // namespace

// D3DXMatrixPerspectiveFovRH (Matrix4x4f memory = D3D row major layout).
// Eden view space is right handed (camera looking down -Z): the shaders
// rebuilding view positions from the depth (TD_ShadowBox, post effects) use
// z = -depth.
Matrix4x4f EdenPerspectiveFovRH( float fovY, float aspect, float zNear, float zFar ) {
    Matrix4x4f m = Matrix4x4f::Identity();
    const float h = 1.0f / std::tan( fovY * 0.5f );
    m.m[0][0] = h / aspect;
    m.m[1][1] = h;
    m.m[2][2] = zFar / ( zNear - zFar );
    m.m[2][3] = -1.0f;
    m.m[3][2] = zNear * zFar / ( zNear - zFar );
    m.m[3][3] = 0.0f;
    return m;
}

float EdenEffectiveFov( float fov, float fovScale, float aspect ) {
    // The camera's own scale, else the global fov_scale option (50..150 %).
    const float scale = fovScale >= 0.0f ? fovScale
                                         : static_cast<float>( GetGameOptions().FovScale ) / 100.0f;
    const float scaledFov = fov * scale;
    // FUN_00c9f680 (reference aspect 16/9): screens narrower than 16:9 get
    // the FOV scaled by (16/9) / aspect, so the horizontal FOV stays.
    static constexpr float kReferenceAspect = 16.0f / 9.0f;
    return aspect < kReferenceAspect ? scaledFov * kReferenceAspect / aspect : scaledFov;
}

// Eden quaternions rotate the local axes the other way round compared to
// Matrix4x4f::FromQuaternion (the camera forward is +Z rotated by the
// conjugate).
Matrix4x4f EdenRotationFromQuaternion( const Quaternionf &q ) {
    return Matrix4x4f::FromQuaternion( Quaternionf( -q.x, -q.y, -q.z, q.w ) );
}

EdenSceneView::EdenSceneView() {}

EdenSceneView::~EdenSceneView() { destroy(); }

bool EdenSceneView::initialize( const char *pIsland, const Vector3f &streamCenter, int32_t sectorRadius ) {
    destroy();
    oceanReflectionFrame = 0;
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( pRenderer == nullptr ) {
        return false;
    }
    const std::string islandFolder = gpFile->getBankFolder() + "islands/" + pIsland + "/";
    weathers.load( ( islandFolder + "fx/cinematic_weathers.xmb" ).c_str() );
    weathers.loadModifiers( ( islandFolder + "fx/cinematic_modifiers.xmb" ).c_str() );
    if ( !world.initialize( *pRenderer, pIsland ) ) {
        return false;
    }
    // Streamed sector / area buildings: register their NIGHTLMAP materials for
    // the weather's day / night blend as they load, drop them when they unload.
    // Before this only cinematic sets and spot exteriors were registered, so
    // the island's windows stayed on their day bake at night.
    world.setModelCallbacks(
        [this]( EdenModel &model ) { registerLightmapMaterials( model, eLightmapScope::Exterior ); },
        [this]( EdenModel &model ) { unregisterLightmapMaterials( model ); } );
    world.loadAround( streamCenter, sectorRadius );
    sky.initialize( *pRenderer );
    sun.initialize( *pRenderer );
    postFx.initialize( *pRenderer );
    if ( !CmdLine::HasArg( "-noshadows" ) ) {
        // Resolution 2048 (Shadows.cpr SET_SHADOWS_RESOLUTION); the config
        // (slices, bias...) is set by the cinematic (DefaultShadowConfig,
        // "shad:" messages).
        EdenShadows::Config shadowConfig;
        shadowConfig.Resolution = static_cast<uint32_t>( Max( 256, CmdLine::GetArgInt( "-shadowres", 2048 ) ) );
        shadows.initialize( *pRenderer, shadowConfig );
    }
    if ( !CmdLine::HasArg( "-noocean" ) ) {
        ocean.initialize( *pRenderer );
        // The retail sea plane is not y = 0 on every island (Hawaii: about
        // -1.8 m, docs/re/ocean_spec.md 5.3): the reflection and the tide
        // base come from the loaded island water mesh, which already sits at
        // that height. Ibiza and Casino are 0.
        ocean.setWaterBaseLevel( world.getIslandBoundsMin().y );
    }
    loadClouds( islandFolder );
    if ( !CmdLine::HasArg( "-norain" ) ) {
        rain.initialize( *pRenderer, islandFolder );
    }
    if ( !CmdLine::HasArg( "-nodroplets" ) ) {
        windscreen.initialize( &gpRender->getDevice() );
    }
    bReady = true;
    return true;
}

void EdenSceneView::destroy() {
    if ( gpRender != nullptr ) {
        occlusion.destroy( gpRender->getDevice() );
    }
    destroyTargets();
    fullscreen.destroy();
    sky.destroy();
    sun.destroy();
    postFx.destroy();
    shadows.destroy();
    ocean.destroy();
    planarReflections.destroy();
    rain.destroy();
    windscreen.destroy();
    lightmapMaterials.clear();
    if ( gpRender != nullptr ) {
        if ( EdenRenderer *pEden = gpRender->getEdenRenderer() ) {
            pEden->clearShadowCasterCache();
        }
        if ( !cloudsBank.getEntries().empty() ) {
            gpRender->getTextureCache().releaseOwner( cloudsBank.getPath().c_str() );
        }
    }
    clouds.destroy();
    cloudsBank.unload();
    cloudsSuffix.clear();
    world.destroy();
    world.setModelCallbacks( nullptr, nullptr );
    pWeather = nullptr;
    bReady = false;
}

GPUTexture *EdenSceneView::getEnvironmentCube() const {
    return pEnvironmentTarget != nullptr && pEnvironmentTarget->bInitialized && pEnvironmentTarget->InitializedLayers == 0x3f
               ? &pEnvironmentTarget->Color
               : nullptr;
}

void EdenSceneView::setEnvironmentCenter( const Vector3f &center, bool bEnable ) {
    environmentCenter = center;
    bEnvironmentEnabled = bEnable;
}

void EdenSceneView::setOceanEnabled( bool bEnable ) {
    bOceanEnabled = bEnable;
    world.setOceanVisible( bEnable );
}

void EdenSceneView::setPlanarReflectionsEnabled( bool bEnable ) {
    if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
        planarReflections.setEnabled( *pRenderer, bEnable );
    }
}

uint32_t EdenSceneView::registerProjectiveMaterials( EdenModel &model ) {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    return pRenderer != nullptr ? planarReflections.registerMaterials( *pRenderer, model ) : 0;
}

uint32_t EdenSceneView::registerReflectionScene( const EdenModel &model, const Matrix4x4f &world,
                                                 const char *pBankName, const char *pActorLayer1,
                                                 const char *pActorLayer2 ) {
    return planarReflections.registerScene( model, world, pBankName, pActorLayer1, pActorLayer2 );
}

void EdenSceneView::clearPlanarReflections() {
    planarReflections.clear();
    if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
        planarReflections.unbind( *pRenderer );
    }
}

uint32_t EdenSceneView::registerLightmapMaterials( EdenModel &model, eLightmapScope scope ) {
    // Exterior (FUN_00981870): TD_Building with U6 (MAT. +0x335) = 1.
    // Interior (FUN_00a4d7e0): TD_Furnitures, TD_Building, d5013ed0dcacafaf.
    // Both: materials that have the NIGHTLMAP parameter. The streamed island's
    // sectors register through EdenWorld::setModelCallbacks; the retail limit
    // of 63 was a spot-scene array, so the port allows a loaded world's worth
    // (512) - the parameter update is per material and cheap.
    uint32_t numExterior = 0;
    uint32_t numInterior = 0;
    for ( const LightmapMaterial &entry : lightmapMaterials ) {
        numExterior += entry.Scope == eLightmapScope::Exterior ? 1 : 0;
        numInterior += entry.Scope == eLightmapScope::Interior ? 1 : 0;
    }
    static const bool bLog = CmdLine::HasArg( "-lognightlmaps" );
    uint32_t numRegistered = 0;
    if ( scope == eLightmapScope::Exterior ) {
        // NIGHT (rain / night engineer): FUN_00981870 also collects the
        // sprites (GUID fcdaa96bdeadbeef) that have OcclusionDayNight
        // (0x97909ca3a58487f1, at most 127) and FUN_00980d30 the nodes drawn
        // with TD_Lamp (c913741cbe17490d, at most 64; node flags |= 0x30).
        // FUN_00980e50 writes OcclusionDayNight.z = 1 - x and the node colour
        // .w (+0x8c, __Instance_Color of TD_Lamp: emissive * colour.rgb *
        // colour.w + ambient) = y of spot_exterior_lightmap_day_night [CODE
        // + SHADER]. Node colour rgb kept (1, 1, 1 [INF]).
        static const uint64_t kOcclusionDayNight = 0x97909ca3a58487f1ull;
        static const uint64_t kSpriteShader = 0xfcdaa96bdeadbeefull;
        static const uint64_t kLampShader = 0xc913741cbe17490dull;
        uint32_t numSprites = 0;
        uint32_t numLamps = 0;
        for ( const auto &entry : lightmapMaterials ) {
            numSprites += entry.Scope == eLightmapScope::ExteriorSprite ? 1 : 0;
            numLamps += entry.Scope == eLightmapScope::ExteriorLamp ? 1 : 0;
        }
        for ( EdenMaterial &material : model.getMaterials() ) {
            eLightmapScope extra = eLightmapScope::Exterior;
            if ( material.ShaderSetGUID == kSpriteShader &&
                 material.Parameters.find( kOcclusionDayNight ) != material.Parameters.end() && numSprites < 127 ) {
                extra = eLightmapScope::ExteriorSprite;
                numSprites++;
            } else if ( material.ShaderSetGUID == kLampShader && numLamps < 64 ) {
                extra = eLightmapScope::ExteriorLamp;
                numLamps++;
            } else {
                continue;
            }
            bool bKnown = false;
            for ( const auto &entry : lightmapMaterials ) {
                bKnown |= entry.pMaterial == &material && entry.Scope == extra;
            }
            if ( !bKnown ) {
                lightmapMaterials.push_back( { &material, extra, &model } );
                OTDU_LOG_DEBUG( "EdenSceneView: exterior %s '%s'\n",
                                extra == eLightmapScope::ExteriorLamp ? "lamp" : "sprite", material.Name.c_str() );
            }
        }
    }
    for ( EdenMaterial &material : model.getMaterials() ) {
        if ( material.Parameters.find( kNightLightmap ) == material.Parameters.end() ) {
            continue;
        }
        // The retail exterior rule (FUN_00981870) selects TD_Building with
        // U6 = 1 - but every streamed-world building material the port sees
        // carries U6 = 0 (175 of them around one Ibiza spawn, all with
        // NIGHTLMAP: SB_*_window, SB_*_wall, ...). Keeping the U6 test made
        // the whole island ignore the night bake, which is exactly the
        // "no windows lit at night" report. Every TD_Building material with
        // NIGHTLMAP is a lightmapped building surface, so register it.
        const bool bMatchesExterior = material.ShaderSetGUID == kTD_Building;
        const bool bMatchesInterior = material.ShaderSetGUID == kTD_Building || material.ShaderSetGUID == kTD_Furnitures ||
                                      material.ShaderSetGUID == kTD_FurnituresAlt;
        const bool bMatch = scope == eLightmapScope::Exterior ? bMatchesExterior : bMatchesInterior;
        if ( bLog ) {
            auto night = material.Parameters.find( kNightLightmap );
            const Vector4f value = night != material.Parameters.end() ? night->second : Vector4f();
            OTDU_LOG_INFO( "NightLMAP: '%s' guid %016llx u6 %u scope %d match %d value (%.2f %.2f %.2f %.2f)\n",
                           material.Name.c_str(), static_cast<unsigned long long>( material.ShaderSetGUID ),
                           static_cast<uint32_t>( material.Spec[0x1f + 6] ), static_cast<int32_t>( scope ),
                           bMatch ? 1 : 0, value.x, value.y, value.z, value.w );
        }
        if ( !bMatch ) {
            continue;
        }
        if ( scope == eLightmapScope::Exterior ? numExterior >= kMaxExteriorLightmaps
                                                : numInterior >= kMaxInteriorLightmaps ) {
            static bool bWarned = false;
            if ( !bWarned ) {
                OTDU_LOG_WARN( "Eden lightmaps: %s limit reached (%u), further NIGHTLMAP materials are not blended\n",
                               scope == eLightmapScope::Exterior ? "exterior" : "interior",
                               scope == eLightmapScope::Exterior ? numExterior : numInterior );
                bWarned = true;
            }
            continue;
        }
        bool bKnown = false;
        for ( const LightmapMaterial &entry : lightmapMaterials ) {
            bKnown |= entry.pMaterial == &material;
        }
        if ( bKnown ) {
            continue;
        }
        lightmapMaterials.push_back( { &material, scope, &model } );
        ( scope == eLightmapScope::Exterior ? numExterior : numInterior )++;
        numRegistered++;
    }
    return numRegistered;
}

uint32_t EdenSceneView::registerNeonMaterials( EdenModel &model ) {
    // FUN_00985e40 for the model instance (see EdenNeons): neon set + one
    // blink state per blinking material of this model.
    uint32_t numRegistered = 0;
    for ( EdenMaterial &material : model.getMaterials() ) {
        if ( !EdenNeons::IsNeonSetMaterial( material ) && !EdenNeons::IsBlinkMaterial( material ) ) {
            continue;
        }
        bool bKnown = false;
        for ( const LightmapMaterial &entry : lightmapMaterials ) {
            bKnown |= entry.pMaterial == &material;
        }
        if ( bKnown ) {
            continue;
        }
        if ( EdenNeons::IsNeonSetMaterial( material ) ) {
            lightmapMaterials.push_back( { &material, eLightmapScope::Neon, &model } );
        }
        if ( EdenNeons::IsBlinkMaterial( material ) ) {
            EdenNeons::Blink blink;
            blink.initialize( static_cast<uint32_t>( material.Hashcode ^ ( material.Hashcode >> 32 ) ) +
                              static_cast<uint32_t>( neonBlinks.size() ) );
            material.Parameters[EdenNeons::kAnimValue] = blink.Value;
            neonBlinks.emplace_back( &material, blink );
        }
        numRegistered++;
    }
    return numRegistered;
}

void EdenSceneView::unregisterLightmapMaterials( const EdenModel &model ) {
    // A sector / area is unloading: its materials are about to be destroyed,
    // so drop every entry that points into that model.
    lightmapMaterials.erase( std::remove_if( lightmapMaterials.begin(), lightmapMaterials.end(),
                                             [&model]( const LightmapMaterial &entry ) {
                                                 return entry.pOwner == &model;
                                             } ),
                             lightmapMaterials.end() );
}

void EdenSceneView::updateLightmapMaterials() {
    // FUN_00980e50 (exterior: NIGHTLMAP = (x, y, 0, 0) + the sprite / lamp
    // values) and FUN_00a4ee30 (interior: x, y written, z, w kept). Weathers
    // without the parameter leave the material value [INF].
    if ( pWeather == nullptr || lightmapMaterials.empty() ) {
        return;
    }
    static const bool bDisabled = CmdLine::HasArg( "-nonightlmap" );
    if ( bDisabled ) {
        return;
    }
    auto exteriorIt = pWeather->Values.find( "spot_exterior_lightmap_day_night" );
    auto interiorIt = pWeather->Values.find( "spot_interior_lightmap_day_night" );
    float neonStrength = 0.0f;
    const bool bNeonStrength = EdenNeons::GetStrength( pWeather, neonStrength );
    for ( auto &entry : lightmapMaterials ) {
        if ( entry.Scope == eLightmapScope::Neon ) {
            // FUN_009829d0: only the parameters the material has.
            if ( bNeonStrength ) {
                EdenNeons::ApplyStrength( *entry.pMaterial, neonStrength );
            }
            continue;
        }
        if ( entry.Scope == eLightmapScope::ExteriorSprite || entry.Scope == eLightmapScope::ExteriorLamp ) {
            if ( exteriorIt == pWeather->Values.end() ) {
                continue;
            }
            static const uint64_t kOcclusionDayNight = 0x97909ca3a58487f1ull;
            if ( entry.Scope == eLightmapScope::ExteriorSprite ) {
                auto occlusion = entry.pMaterial->Parameters.find( kOcclusionDayNight );
                if ( occlusion != entry.pMaterial->Parameters.end() ) {
                    occlusion->second.z = 1.0f - exteriorIt->second.x; // 1.0 (DAT_00f44c70) - x
                }
            } else {
                // The TD_Lamp __Instance_Color is a stage constant vector (one
                // register): replace row 0, keep any further rows.
                std::vector<Vector4f> &colour = entry.pMaterial->StageConstants[0]["__Instance_Color"];
                if ( colour.empty() ) {
                    colour.resize( 1 );
                }
                colour[0] = Vector4f( 1.0f, 1.0f, 1.0f, exteriorIt->second.y );
            }
            continue;
        }
        auto parameter = entry.pMaterial->Parameters.find( kNightLightmap );
        if ( parameter == entry.pMaterial->Parameters.end() ) {
            continue;
        }
        if ( entry.Scope == eLightmapScope::Exterior ) {
            // glm deviation from retail (session-45 regression guard): the
            // exterior banks author their own day / lamp blend and the retail
            // day overwrite (1, 0) switched every shop lamp off at midday, so
            // the exterior NIGHTLMAP is only applied when the weather's blend
            // points at the night lightmap (y > 0). Day keeps the authored
            // value, night gets the retail (0, 1).
            if ( exteriorIt != pWeather->Values.end() && exteriorIt->second.y > 0.0f ) {                parameter->second = Vector4f( exteriorIt->second.x, exteriorIt->second.y, 0.0f, 0.0f );
            }
        } else if ( interiorIt != pWeather->Values.end() ) {
            parameter->second.x = interiorIt->second.x;
            parameter->second.y = interiorIt->second.y;
        }
    }
}

void EdenSceneView::updateNeons() {
    // FUN_009834d0: the neon set strength (in updateLightmapMaterials for the
    // view's own models) then every AnimValue blink.
    for ( auto &entry : neonBlinks ) {
        entry.second.update( frameDelta );
        entry.first->Parameters[EdenNeons::kAnimValue] = entry.second.Value;
    }
}

void EdenSceneView::tick( float deltaTime ) {
    time += deltaTime;
    frameDelta = deltaTime;
}

void EdenSceneView::setupScene( EdenSceneParams &params ) {
    const float aspectRatio =
        static_cast<float>( pSceneTarget->Width ) / Max( 1.0f, static_cast<float>( pSceneTarget->Height ) );
    // Eden camera: right handed, looking down its local -Z. The track
    // quaternion (conjugated) gives a frame looking down +Z: turned by 180
    // degrees around Y (determinant +1: no mirroring).
    const Matrix4x4f cameraWorld = Matrix4x4f::Translation( camera.Position ) *
                                   EdenRotationFromQuaternion( camera.Rotation ) *
                                   Matrix4x4f::Scale( Vector3f( -1.0f, 1.0f, -1.0f ) );
    params.View = cameraWorld.inverse();
    // The FOV scale option (fov_scale, per camera override in Camera::FovScale)
    // and the retail narrow-aspect law, both inside EdenEffectiveFov.
    effectiveFov = EdenEffectiveFov( camera.Fov, camera.FovScale, aspectRatio );
    params.Projection = EdenPerspectiveFovRH( effectiveFov, aspectRatio, camera.Near, camera.Far );
    params.CameraPosition = camera.Position;
    params.CameraDirection = cameraWorld.transformVector( Vector3f( 0.0f, 0.0f, -1.0f ) );
    CoordsOverlay::SetCamera( params.CameraPosition, params.CameraDirection );
    params.Time = time;
    if ( pWeather != nullptr ) {
        params.SunDirection = pWeather->getVector3( "sundirection", params.SunDirection ).normalized();
        static const bool bLogSun = CmdLine::HasArg( "-logsun" );
        if ( bLogSun ) {
            OTDU_LOG_INFO( "Sun dir (%.3f %.3f %.3f) camera dir (%.3f %.3f %.3f) pos (%.1f %.1f %.1f)\n",
                           params.SunDirection.x, params.SunDirection.y, params.SunDirection.z,
                           params.CameraDirection.x, params.CameraDirection.y, params.CameraDirection.z,
                           camera.Position.x, camera.Position.y, camera.Position.z );
        }
        params.SunDiffuse = pWeather->getVector3( "sun_diffuse", params.SunDiffuse );
        params.SunSpecular = pWeather->getVector3( "sun_specular", params.SunSpecular );
        params.Ambient = pWeather->getVector3( "sun_ambient", params.Ambient );
        params.FogColor = pWeather->getVector3( "fog_color", params.FogColor );
        const float fogStart = pWeather->getFloat( "fog_start", 100.0f );
        const float fogEnd = pWeather->getFloat( "fog_end", 2000.0f );
        const float fogMax = ( bMapView || !bFogEnabled ) ? 0.0f : pWeather->getFloat( "fog_max", 1.0f );
        params.FogConfig = Vector4f( fogStart, fogEnd, fogMax, 1.0f / Max( fogEnd - fogStart, 1.0f ) );
    }
    // Frame user vectors (Var_Frame_VS). FUN_00d769e0 (per frame, from the
    // weather; night_lighting_spec 2): UserVector1 = (1, outside_street_lightmap,
    // 1, building_nightwindow_frequency) — y drives the baked street-lamp light
    // of the sector -s maps (TD_HeightMap / TD_Road / TD_Ground: G * 2 * y, 1.5
    // at night in the data), w gates the TD_Building night windows (slot 6,
    // active at w >= 0.5). UserVector2 = (building_shop_front_back_lighting.xy,
    // 0, 0). The day presets carry y = w = 0, so the default look is unchanged.
    static const bool bNoStreetLights = CmdLine::HasArg( "-nostreetlights" );
    if ( pWeather != nullptr && !bNoStreetLights ) {
        params.UserVectors[1] = Vector4f( 1.0f, pWeather->getFloat( "outside_street_lightmap", 0.0f ), 1.0f,
                                          pWeather->getFloat( "building_nightwindow_frequency", 0.0f ) );
        const Vector4f shop = pWeather->getVector4( "building_shop_front_back_lighting", Vector4f() );
        params.UserVectors[2] = Vector4f( shop.x, shop.y, 0.0f, 0.0f );
    } else {
        params.UserVectors[1] = Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
        params.UserVectors[2] = Vector4f();
    }
    // The far-tree impostors' light (FUN_00d769e0 -> renderer +0x45c0 [CODE]):
    // the weather's impostor_ambient, see EdenWorld::impostorAmbient. Without a
    // weather, the scene ambient.
    {
        const Vector3f impostorAmbient =
            pWeather != nullptr ? pWeather->getVector3( "impostor_ambient", params.Ambient ) : params.Ambient;
        world.setImpostorAmbient( impostorAmbient );
        static const char *pAmb2Arg = CmdLine::GetArgValue( "-impostoramb2" );
        world.setImpostorAmbient2( params.Ambient * ( pAmb2Arg != nullptr ? static_cast<float>( atof( pAmb2Arg ) ) : 1.0f ) );
        static const bool bLogImpostors = CmdLine::HasArg( "-logimpostors" );
        static Vector3f lastLogged( -1.0f, -1.0f, -1.0f );
        if ( bLogImpostors && ( impostorAmbient - lastLogged ).length() > 0.01f ) {
            lastLogged = impostorAmbient;
            OTDU_LOG_INFO( "EdenSceneView: impostor_ambient (%.3f, %.3f, %.3f)%s\n", impostorAmbient.x, impostorAmbient.y,
                           impostorAmbient.z, pWeather != nullptr ? "" : " (no weather: scene ambient)" );
            OTDU_LOG_INFO( "EdenSceneView: sun_ambient (%.3f, %.3f, %.3f) sun_diffuse (%.3f, %.3f, %.3f)\n", params.Ambient.x, params.Ambient.y, params.Ambient.z, params.SunDiffuse.x, params.SunDiffuse.y, params.SunDiffuse.z );
        }
    }
    // -lognight: the night-light feed once a second (headless A/B checks).
    if ( CmdLine::HasArg( "-lognight" ) ) {
        static float nightLogTimer = 0.0f;
        nightLogTimer += frameDelta;
        if ( nightLogTimer >= 1.0f ) {
            nightLogTimer = 0.0f;
            OTDU_LOG_INFO( "Night: street %.2f windows %.2f shop (%.2f %.2f) preset hour %d:%02d\n",
                           params.UserVectors[1].y, params.UserVectors[1].w, params.UserVectors[2].x,
                           params.UserVectors[2].y, pWeather != nullptr ? pWeather->Hour : -1,
                           pWeather != nullptr ? pWeather->Minutes : -1 );
        }
    }    EdenSky::setupAtmosphere( params, pWeather );
    params.Width = pSceneTarget->Width;
    params.Height = pSceneTarget->Height;
}

void EdenSceneView::ensureTargets() {
    RenderDevice &device = gpRender->getDevice();
    // Options > Advanced: the scene renders at RenderScale % of the window
    // and the final pass reconstructs it at the window size.
    const float scale = GetRenderScale();
    const uint32_t width = Max<uint32_t>( 64, static_cast<uint32_t>( device.getBackbufferWidth() * scale + 0.5f ) );
    const uint32_t height = Max<uint32_t>( 64, static_cast<uint32_t>( device.getBackbufferHeight() * scale + 0.5f ) );
    if ( pSceneTarget != nullptr && pSceneTarget->Width == width && pSceneTarget->Height == height ) {
        return;
    }
    RenderTarget *pEnvironment = pEnvironmentTarget;
    RenderTarget *pEnvironmentScratchKeep = pEnvironmentScratch;
    pEnvironmentTarget = nullptr;
    pEnvironmentScratch = nullptr;
    destroyTargets();
    pEnvironmentTarget = pEnvironment;
    pEnvironmentScratch = pEnvironmentScratchKeep;
    pSceneTarget = device.createRenderTarget( width, height, VK_FORMAT_UNDEFINED, true, "EdenScene" );
    pViewPosTarget = device.createRenderTarget( width, height, VK_FORMAT_R8G8B8A8_UNORM, false, "gViewPos" );
    pFinalTarget = device.createRenderTarget( width, height, VK_FORMAT_UNDEFINED, false, "EdenSceneFinal" );
    pDropletTarget = device.createRenderTarget( Max<uint32_t>( 64, width / 2 ), Max<uint32_t>( 64, height / 2 ),
                                                VK_FORMAT_R8G8B8A8_UNORM, false, "Droplets_Windshield" );
    windscreen.setTarget( pDropletTarget );
    static const int32_t aoSamples = CmdLine::GetArgInt( "-rtao", 0 );
    if ( aoSamples > 0 ) {
        pAOTarget = device.createRenderTarget( width, height, VK_FORMAT_R8_UNORM, false, "rtAO" );
    }    // Occlusion grid: one R32 texel per tile of the pre-pass depth (depthmax.frag).
    if ( EdenOcclusion::IsEnabled() ) {
        const uint32_t tile = EdenOcclusion::TileSizeFor( width );
        pOcclusionTarget = device.createRenderTarget( ( width + tile - 1 ) / tile, ( height + tile - 1 ) / tile,
                                                      VK_FORMAT_R32_SFLOAT, false, "OcclusionTiles" );
    }
}

void EdenSceneView::destroyTargets() {
    if ( gpRender == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    device.destroyRenderTarget( pSceneTarget );
    device.destroyRenderTarget( pViewPosTarget );
    device.destroyRenderTarget( pEnvironmentTarget );
    device.destroyRenderTarget( pEnvironmentScratch );
    device.destroyRenderTarget( pFinalTarget );
    device.destroyRenderTarget( pDropletTarget );
    device.destroyRenderTarget( pAOTarget );
    device.destroyRenderTarget( pOcclusionTarget );
    pSceneTarget = pViewPosTarget = pEnvironmentTarget = pEnvironmentScratch = pFinalTarget = pDropletTarget = pAOTarget = nullptr;
    pOcclusionTarget = nullptr;
}

void EdenSceneView::drawObjects( EdenRenderer &renderer, ePass pass ) {
    // Street lamps: the port has no lamp system, so the weather's hour switches
    // their glow (the same rule the car headlights use: on from 20:00, off from
    // 06:00) - once per call, all passes see the same state.
    renderer.bNightLights = pWeather == nullptr || pWeather->Hour < 6 || pWeather->Hour >= 20;
    if ( drawCallback ) {
        drawCallback( renderer, pass );
    }
}

void EdenSceneView::renderEnvironment( const EdenSceneParams &mainScene, bool bAllFaces ) {
    // Dynamic environment cube around the car (car_envmap_factor; the static
    // cubemap of common_car.bnk is the fallback). The car shaders sample it
    // with (x, y, -z): the faces are rendered for the z mirrored directions.
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    RenderDevice &device = gpRender->getDevice();
    if ( pEnvironmentTarget == nullptr ) {
        pEnvironmentTarget = device.createCubeRenderTarget( kEnvironmentSize, true, "CarEnvironment" );
        pEnvironmentScratch = device.createCubeRenderTarget( kEnvironmentSize, true, "CarEnvironmentScratch" );
        bAllFaces = true;
    }
    // The faces render into a scratch cube that is copied to the sampled one
    // afterwards (RenderDevice::copyTexture): the cars drawn in the
    // Environment pass and the wet ground sample this very cube, and a sampled
    // image must not also be the render target of the pass that draws them.
    // The sampled cube is rendered into directly only for the first refresh
    // (nothing samples it yet - getEnvironmentCube() is null until the copy
    // path has filled it once). Retail ping-pongs two cube targets instead
    // (DAT_011c9ef0: cube 1 + cube 2, rain_shadow_spec A.5); the copy is the
    // same idea without the one-refresh staleness [INF].
    RenderTarget *pWriteTarget =
        ( pEnvironmentScratch != nullptr && pEnvironmentTarget->bInitialized ) ? pEnvironmentScratch : pEnvironmentTarget;
    static const Vector3f kForward[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    static const Vector3f kUp[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
    auto mirror = []( const Vector3f &v ) { return Vector3f( v.x, v.y, -v.z ); };
    const uint32_t firstFace = bAllFaces ? 0 : environmentFace;
    const uint32_t lastFace = bAllFaces ? 6 : environmentFace + 1;
    environmentFace = ( environmentFace + 1 ) % 6;
    for ( uint32_t face = firstFace; face < lastFace; face++ ) {
        const Vector3f forward = mirror( kForward[face] );
        const Vector3f up = mirror( kUp[face] );
        const Vector3f right = mirror( kUp[face].cross( kForward[face] ) );
        Matrix4x4f cameraWorld = Matrix4x4f::Identity();
        cameraWorld.m[0][0] = right.x, cameraWorld.m[0][1] = right.y, cameraWorld.m[0][2] = right.z;
        cameraWorld.m[1][0] = up.x, cameraWorld.m[1][1] = up.y, cameraWorld.m[1][2] = up.z;
        cameraWorld.m[2][0] = -forward.x, cameraWorld.m[2][1] = -forward.y, cameraWorld.m[2][2] = -forward.z;
        cameraWorld.m[3][0] = environmentCenter.x, cameraWorld.m[3][1] = environmentCenter.y + 1.0f;
        cameraWorld.m[3][2] = environmentCenter.z;

        EdenSceneParams params = mainScene;
        params.View = cameraWorld.inverse();
        params.Projection = EdenPerspectiveFovRH( 90.0f * kDegToRad, 1.0f, camera.Near, camera.Far );
        params.CameraPosition = cameraWorld.getTranslation();
        params.CameraDirection = forward;
        params.Width = params.Height = kEnvironmentSize;
        params.bMirroredView = false; // (right, up, back) frame: determinant +1

        device.beginRenderTarget( pWriteTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ), face );
        pRenderer->beginScene( params );
        sky.draw( *pRenderer );
        sky.drawMoon( *pRenderer, params, pWeather );
        sky.drawStars( *pRenderer, params, pWeather );
        drawClouds( *pRenderer );
        world.setMinInstanceRatio( 0.02f );
        world.draw( *pRenderer, params.CameraPosition );
        world.setMinInstanceRatio( 0.0f );
        drawObjects( *pRenderer, ePass::Environment );
        pRenderer->endScene();
        device.endRenderTarget();
    }
    if ( pWriteTarget != pEnvironmentTarget ) {
        device.copyTexture( &pWriteTarget->Color, &pEnvironmentTarget->Color, lastFace - firstFace, firstFace );
        pEnvironmentTarget->InitializedLayers |= pWriteTarget->InitializedLayers;
        pEnvironmentTarget->bInitialized = pEnvironmentTarget->InitializedLayers == ( ( 1u << pEnvironmentTarget->NumLayers ) - 1 );
    }
}

void EdenSceneView::generateViewPos() {
    RenderDevice &device = gpRender->getDevice();
    device.beginRenderTarget( pViewPosTarget, true );
    const float viewPosParams[4] = { camera.Near, camera.Far, 0.0f, 0.0f };
    GPUTexture *pDepth = &pSceneTarget->Depth;
    fullscreen.draw( device, kSPIRV_viewpos_frag, sizeof( kSPIRV_viewpos_frag ), &pDepth, 1, viewPosParams,
                     sizeof( viewPosParams ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Point );
    device.endRenderTarget();
}

void EdenSceneView::renderOffscreen() {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( !bReady || pRenderer == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    ensureTargets();
    static const bool bLogTimes = CmdLine::HasArg( "-logscenetimes" );
    gSceneTimes.start( pRenderer );
    EdenSceneParams params;
    setupScene( params );

    // Environment cube. Each face is a full scene draw, which dominates CPU
    // frame time, so the cube is refreshed coherently: all 6 faces are
    // rendered together, only when the content they see actually changed
    // (the car moved / the camera turned / a refresh period elapsed). The
    // previous one-face-per-frame rotation mixed 6 different instants in
    // the cube, and the car paint / glass reflections visibly flickered
    // while driving. -staticcubemap stops updates entirely,
    // -cubemapinterval N forces a refresh at most every Nth frame.
    static const bool bStaticCubemap = CmdLine::HasArg( "-staticcubemap" );
    static const uint32_t kCubeMapInterval =
        static_cast<uint32_t>( Max( 1, CmdLine::GetArgInt( "-cubemapinterval", 1 ) ) );
    device.beginGpuPass( "env" );
    if ( bEnvironmentEnabled && !bStaticCubemap ) {
        world.updateWetGround( *pRenderer, nullptr, nullptr, 0.0f );
        static Vector3f lastCenter( FLT_MAX, FLT_MAX, FLT_MAX );
        static float lastYaw = FLT_MAX;
        static float refreshTimer = 0.0f;
        refreshTimer += frameDelta;
        const Vector3f forward =
            EdenRotationFromQuaternion( camera.Rotation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        const float yaw = atan2f( forward.x, forward.z );
        const float moved =
            Max( Max( fabsf( environmentCenter.x - lastCenter.x ), fabsf( environmentCenter.z - lastCenter.z ) ),
                 fabsf( environmentCenter.y - lastCenter.y ) );
        float yawDelta = fabsf( yaw - lastYaw );
        if ( yawDelta > kPI ) {
            yawDelta = 2.0f * kPI - yawDelta;
        }
        // Content changed (car moved / camera turned) and the last full
        // update is at least 200 ms old: rebuild the whole cube in one go.
        // Parked: a refresh every 500 ms follows the sun / cloud drift.
        const bool bContentChanged = environmentFrame == 0 || moved > 0.5f || yawDelta > 10.0f * kDegToRad;
        const float minGap = bContentChanged ? 0.2f : 0.5f;
        if ( refreshTimer >= minGap && ( environmentFrame % kCubeMapInterval ) == 0 ) {
            lastCenter = environmentCenter;
            lastYaw = yaw;
            refreshTimer = 0.0f;
            renderEnvironment( params, true );
        }
        environmentFrame++;
    }

    device.endGpuPass();
    gSceneTimes.mark( SceneTimes::Env );
    // Sun visibility: depth around the sun (read back a few frames later).
    sun.updateVisibility( params, pSceneTarget );

    static const bool bNoWet = CmdLine::HasArg( "-nowet" );
    // Falling rain: -rain forces it on a dry sky (the rain values of the
    // preset's own rainy intempery), -norain turns the falling rain off (the
    // wet ground keeps the weather values). The wet ground reads the resolved
    // preset either way, like upstream's World Viewer checkbox.
    static const bool bForceRain = CmdLine::HasArg( "-rain" );
    static const bool bSnow = CmdLine::HasArg( "-snow" );
    if ( bSnow ) {
        rain.setSnow( true );
    }
    const bool bRainEnabled = !CmdLine::HasArg( "-norain" ) &&
                              ( bForceRain || bSnow ||
                                ( pWeather != nullptr && pWeather->getFloat( "rain_alpha", 0.0f ) > 0.0f ) );
    const WeatherPreset *pRainWeather = rain.resolveWeather( pWeather, bRainEnabled );
    if ( rain.isActive() ) {
        const Matrix4x4f cameraWorld = params.View.inverse();
        EdenRain::Frame rainFrame;
        rainFrame.Position = params.CameraPosition;
        rainFrame.Right = cameraWorld.transformVector( Vector3f( 1.0f, 0.0f, 0.0f ) ).normalized();
        rainFrame.Up = cameraWorld.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) ).normalized();
        rainFrame.Forward = params.CameraDirection.normalized();
        rain.update( frameDelta, rainFrame, world, pRainWeather );
    }
    // Rain on the windscreen: the sim runs when the camera owner gated it on
    // (the inside views); the composite samples its RT in the final blit.
    windscreen.update( frameDelta, pRainWeather );
    world.updateWetGround( *pRenderer, bNoWet ? nullptr : pRainWeather, getEnvironmentCube(), frameDelta );

    updateLightmapMaterials();
    updateNeons();

    // Occlusion culling: the newest depth grid usable from this camera
    // (eden_occlusion.h). Not in the map view (its camera jumps by zoom levels).
    Vector3f cameraForward( 0.0f, 0.0f, 1.0f );
    {
        const Matrix4x4f cameraWorld = params.View.inverse();
        cameraForward = Vector3f( -cameraWorld.m[2][0], -cameraWorld.m[2][1], -cameraWorld.m[2][2] );
    }
    pOcclusionGrid = bMapView ? nullptr : occlusion.beginFrame( device, params.CameraPosition, cameraForward, world.getDrawEpoch() );

    // Ocean: noise normal map, mirror reflection, TD_Ocean runtime setup.
    device.beginGpuPass( "ocean" );
    if ( ocean.isReady() && bOceanEnabled ) {
        ocean.updateNormalMap( *pRenderer, frameDelta, pWeather );
        // The port's shore swell: every ocean level (island / area / sector)
        // rides the same water plane height, computed by the tide clock.
        world.setSeaLevelOffset( ocean.getWaterLevel() );
        // Quality profiles: the mirror pass re-submits the whole world, so a
        // handheld profile renders it once (0) or every Nth frame; 1 = the
        // retail every-frame refresh. The first frame always renders, so the
        // static setting still has a reflection to sample.
        const int32_t reflectionInterval = Clamp( GetGameOptions().OceanReflections, 0, 8 );
        // No sea on screen (the occlusion grid: every line of sight meets
        // nearer geometry before the water plane - the middle of town): the
        // mirror pass re-submitted the whole world (~2,000 draws, 2.5 ms) for a
        // texture nobody saw. -nooceanskip = the A/B. When the water comes into
        // view the grid shows it 2-3 frames later; until then the reflection
        // is the last one rendered.
        static const bool bOceanSkip = !CmdLine::HasArg( "-nooceanskip" );
        const bool bWaterHidden = bOceanSkip && pOcclusionGrid != nullptr && oceanReflectionFrame != 0 &&
                                  !pOcclusionGrid->isPlaneVisible( ocean.getWaterPlaneHeight() );
        static const bool bLogOceanSkip = CmdLine::HasArg( "-logocclusion" );
        if ( bLogOceanSkip && bWaterHidden != bLastWaterHidden ) {
            OTDU_LOG_INFO( "[occlusion] ocean reflection %s\n", bWaterHidden ? "skipped (no water visible)" : "back on" );
        }
        bLastWaterHidden = bWaterHidden;
        const bool bRenderReflection =
            !bWaterHidden &&
            ( oceanReflectionFrame == 0 || ( reflectionInterval > 0 && ( oceanReflectionFrame % reflectionInterval ) == 0 ) );
        oceanReflectionFrame++;
        if ( bRenderReflection ) {
            const Vector3f mainCamera = params.CameraPosition;
            ocean.renderReflection( *pRenderer, params, pWeather,
                                    [this, mainCamera]( EdenRenderer &renderer, const EdenSceneParams & ) {
                                        world.draw( renderer, mainCamera );
                                        drawObjects( renderer, ePass::Reflection );
                                    } );
        }
        world.forEachOceanMaterial( [this, pRenderer]( EdenMaterial &material, uint8_t kind, GPUTexture *pColorMap ) {
            ocean.setupMaterial( *pRenderer, material, static_cast<EdenOcean::eKind>( kind ), pColorMap, pWeather );
        } );
    } else {
        world.setSeaLevelOffset( 0.0f );
    }

    device.endGpuPass();
    gSceneTimes.mark( SceneTimes::Ocean );
    // 0. Z pre-pass (ZPREPASS layer: opaque and alpha tested draws) ->
    //    gViewPos (FullscreenDepthTarget), read by the haze, the projected
    //    shadow boxes and the sun shadow pass; then the real time sun
    //    shadows: caster maps of the 3 slices, screen shadow buffer (s15).
    const bool bNoShadows = CmdLine::HasArg( "-noshadows" ) || GetGameOptions().Shadows == 0;
    GPUTexture *pShadowBuffer = nullptr;
    shadows.setupSceneConstants( params, pWeather );
    // -occlusionprobe f1,f2,...: the measurement described at OcclusionProbe.
    static OcclusionProbe occlusionProbe;
    static uint64_t sceneFrame = 0;
    sceneFrame++;
    bool bOcclusionProbe = false;
    if ( const char *pProbeFrames = CmdLine::GetArgValue( "-occlusionprobe" ) ) {
        if ( occlusionProbe.bPending && device.getFrameSlot() == occlusionProbe.Slot &&
             device.getFrameNumber() > occlusionProbe.DeviceFrame ) {
            EvaluateOcclusionProbe( occlusionProbe ); // the slot's fence was waited by beginFrame
            occlusionProbe.bPending = false;
        }
        for ( const char *p = pProbeFrames; *p != '\0' && !occlusionProbe.bPending; ) {
            if ( strtoull( p, nullptr, 10 ) == sceneFrame ) {
                bOcclusionProbe = true;
                break;
            }
            const char *pComma = strchr( p, ',' );
            p = pComma != nullptr ? pComma + 1 : "";
        }
    }
    device.beginGpuPass( "prepass" );
    device.beginRenderTarget( pSceneTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    pRenderer->setDepthOnly( true );
    pRenderer->beginScene( params );
    if ( bOcclusionProbe ) {
        pRenderer->beginOcclusionProbe();
    }
    // Only the static world is culled, and only the static world's depth is
    // kept for the grid: the player's car moves with the camera, so in a 2-3
    // frame old depth its body stands where the world is now, and the parts
    // behind it were culled against it (a 2,600-pixel difference behind the
    // car on the drive). The moving objects (drawObjects) are drawn after the
    // copy and never culled. The probe measures the whole set: no cull on its
    // frame.
    pRenderer->setOcclusionGrid( bOcclusionProbe ? nullptr : pOcclusionGrid );
    world.draw( *pRenderer, params.CameraPosition );
    pRenderer->setOcclusionGrid( nullptr );
    const Matrix4x4f prepassViewProjection = pRenderer->getViewProjection();
    if ( !bMapView && EdenOcclusion::IsEnabled() && pOcclusionTarget != nullptr ) {
        // The static world's depth -> tile maxima on the GPU -> a ~60 KB
        // readback (the whole 4K depth was 33 MB a frame, ~3.5 ms of GPU).
        device.endRenderTarget();
        device.beginRenderTarget( pOcclusionTarget, false );
        const float tileParams[4] = { static_cast<float>( EdenOcclusion::TileSizeFor( pSceneTarget->Width ) ),
                                      static_cast<float>( pSceneTarget->Width ),
                                      static_cast<float>( pSceneTarget->Height ), 0.0f };
        GPUTexture *pDepth = &pSceneTarget->Depth;
        fullscreen.draw( device, kSPIRV_depthmax_frag, sizeof( kSPIRV_depthmax_frag ), &pDepth, 1, tileParams,
                         sizeof( tileParams ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Point );
        device.endRenderTarget();
        occlusion.afterPrepass( device, pOcclusionTarget, pSceneTarget->Width, pSceneTarget->Height,
                                prepassViewProjection, params.CameraPosition, cameraForward, world.getDrawEpoch() );
        device.beginRenderTarget( pSceneTarget, false );
        pRenderer->resumeScene();
    }
    drawObjects( *pRenderer, ePass::DepthPrepass );
    if ( bOcclusionProbe ) {
        occlusionProbe.Draws = pRenderer->endOcclusionProbe();
        occlusionProbe.ViewProjection = prepassViewProjection;
    }
    pRenderer->endScene();
    pRenderer->setDepthOnly( false );
    device.endRenderTarget();
    if ( bOcclusionProbe ) {
        const VkDeviceSize size = static_cast<VkDeviceSize>( pSceneTarget->Width ) * pSceneTarget->Height * 4;
        if ( occlusionProbe.pDepth == nullptr || occlusionProbe.pDepth->Size < size ) {
            if ( occlusionProbe.pDepth != nullptr ) {
                device.destroyBuffer( occlusionProbe.pDepth );
            }
            occlusionProbe.pDepth = device.createReadbackBuffer( size, VK_BUFFER_USAGE_TRANSFER_DST_BIT );
        }
        occlusionProbe.Width = pSceneTarget->Width;
        occlusionProbe.Height = pSceneTarget->Height;
        occlusionProbe.Slot = device.getFrameSlot();
        occlusionProbe.DeviceFrame = device.getFrameNumber();
        occlusionProbe.SceneFrame = sceneFrame;
        occlusionProbe.bPending = occlusionProbe.pDepth != nullptr &&
                                  device.copyRenderTargetDepth( pSceneTarget, 0, 0, occlusionProbe.Width,
                                                                occlusionProbe.Height, occlusionProbe.pDepth );
        if ( !occlusionProbe.bPending ) {
            OTDU_LOG_WARN( "[occlusion] depth readback refused (format / pass state)\n" );
        }
    }
    generateViewPos();
    device.endGpuPass();
    gSceneTimes.mark( SceneTimes::Prepass );
    device.beginGpuPass( "shadows" );
    if ( !bNoShadows ) {
        const Vector3f cameraPosition = params.CameraPosition;
        shadows.renderCasters( *pRenderer, params,
                               [this, cameraPosition]( EdenRenderer &renderer, const EdenSceneParams & ) {
                                   world.draw( renderer, cameraPosition );
                                   drawObjects( renderer, ePass::ShadowCasters );
                               } );
        pShadowBuffer = shadows.resolve( *pRenderer, params, &pViewPosTarget->Color, pWeather );
        // -shadowprobe x,y: per-frame texel readback at one pixel of the
        // encoded gViewPos depth (B/A, ~11 mantissa bits - 1.5 cm of viewZ
        // quantization at 30 m, 3 cm at 60) against the raw D32 depth. The
        // shadow-flicker discriminator: if the decoded value steps between
        // stable plateaus while the raw one is smooth, the slice passes'
        // receiver reconstruction quantizes and crosses the bias band.
        // Each line is the readback queued kFramesInFlight frames earlier
        // (its fence is waited by beginFrame), like EdenSun::updateVisibility.
        static const bool bShadowProbe = CmdLine::HasArg( "-shadowprobe" );
        if ( bShadowProbe ) {
            static int probeX = 0;
            static int probeY = 0;
            static bool bProbeReady = false;
            static GPUBuffer *pProbeColor[RenderDevice::kFramesInFlight] = {};
            static GPUBuffer *pProbeDepth[RenderDevice::kFramesInFlight] = {};
            static bool bProbePending[RenderDevice::kFramesInFlight] = {};
            static uint64_t probeFrame[RenderDevice::kFramesInFlight] = {};
            static uint64_t probeQueued = 0;
            if ( !bProbeReady ) {
                if ( const char *pValue = CmdLine::GetArgValue( "-shadowprobe" ) ) {
                    sscanf( pValue, "%d,%d", &probeX, &probeY );
                }
                if ( probeX < 0 || probeY < 0 ) {
                    probeX = static_cast<int>( pSceneTarget->Width / 2 );
                    probeY = static_cast<int>( pSceneTarget->Height / 2 );
                }
                for ( GPUBuffer *&pBuffer : pProbeColor ) {
                    pBuffer = device.createBuffer( 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr );
                }
                for ( GPUBuffer *&pBuffer : pProbeDepth ) {
                    pBuffer = device.createBuffer( 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr );
                }
                bProbeReady = true;
            }
            const uint32_t slot = device.getFrameSlot();
            if ( bProbePending[slot] && pProbeColor[slot]->pMapped != nullptr &&
                 pProbeDepth[slot]->pMapped != nullptr ) {
                const uint8_t *pViewPosTexel = static_cast<const uint8_t *>( pProbeColor[slot]->pMapped );
                const float rawDepth = *static_cast<const float *>( pProbeDepth[slot]->pMapped );
                // R8_UNORM bytes are floor(normalized * 255 + 0.5) already;
                // the shader decodes the normalized values.
                const float b = static_cast<float>( pViewPosTexel[2] );
                const float a = static_cast<float>( pViewPosTexel[3] );
                const float exponent = std::floor( b / 8.0f );
                const float viewZ = exp2f( exponent - 15.0f ) *
                                    ( 1.0f + ( b - exponent * 8.0f ) / 8.0f + a / 2040.0f );
                OTDU_LOG_INFO( "ShadowProbe: f %llu raw %.6f viewZ %.5f\n",
                               static_cast<unsigned long long>( probeFrame[slot] ), rawDepth, viewZ );
                bProbePending[slot] = false;
            }
            probeFrame[slot] = probeQueued++;
            bProbePending[slot] =
                device.copyRenderTargetColor( pViewPosTarget, probeX, probeY, 1, 1, pProbeColor[slot] ) &&
                device.copyRenderTargetDepth( pSceneTarget, probeX, probeY, 1, 1, pProbeDepth[slot] );
        }
    }
    pRenderer->setShadowRealTimeTexture( pShadowBuffer );
    device.endGpuPass();
    gSceneTimes.mark( SceneTimes::Shadows );

    // GSReflection layers (after HEADLIGHTS BLIT, before NEAR_OPAQUE):
    // projective map of the water / reflective floors.
    device.beginGpuPass( "planar" );
    if ( bProjectorActors ) {
        planarReflections.render(
            *pRenderer, params,
            [this]( EdenRenderer &renderer, const EdenPlanarReflections::Projector &projector ) {
                if ( std::abs( projector.Normal.y ) < 0.5f ) {
                    // A wall mirror: the room reflects (sets, props, avatars)
                    // from the mirror camera. Horizontal actor layers keep
                    // the pool law (the avatars only: the surface shader
                    // provides its own look).
                    drawObjects( renderer, EdenSceneView::ePass::ProjectorScene );
                    return;
                }
                drawObjects( renderer, EdenSceneView::ePass::ProjectorActors );
            } );
    } else {
        planarReflections.render( *pRenderer, params, nullptr );
    }

    device.endGpuPass();
    gSceneTimes.mark( SceneTimes::Planar );
    // 1. Opaque scene (blended draws are deferred by the renderer).
    // World map view: the deep sea colour of the island colour map (0, 14, 46)
    // instead of the sky, so the sea continues past the loaded area.
    device.beginGpuPass( "opaque" );
    device.beginRenderTarget( pSceneTarget, true,
                              ( bMapView && !bMapKeepSky ) ? ColorRGBA( 0.0f, 14.0f / 255.0f, 46.0f / 255.0f, 1.0f ) : ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    pRenderer->setScreenTexture( nullptr );
    pRenderer->beginScene( params );
    static const bool bNoSky = CmdLine::HasArg( "-nosky" ); // A/B: the sky dome
    if ( !bNoSky && ( !bMapView || bMapKeepSky ) ) {
        sky.draw( *pRenderer );
        sky.drawMoon( *pRenderer, params, pWeather );
        sky.drawStars( *pRenderer, params, pWeather );
    }
    const bool bNoSunFlare = CmdLine::HasArg( "-nosunflare" ) || GetGameOptions().SunFlare == 0;
    if ( !bNoSunFlare && ( !bMapView || bMapKeepSky ) ) {
        sun.drawHalo( *pRenderer, params, pWeather );
    }
    drawClouds( *pRenderer );
    // The same cull as the pre-pass (the blended draws are deferred from this
    // traversal, so they follow it).
    pRenderer->setOcclusionGrid( bOcclusionProbe ? nullptr : pOcclusionGrid );
    world.draw( *pRenderer, params.CameraPosition );
    pRenderer->setOcclusionGrid( nullptr ); // the moving objects are never culled (see the pre-pass)
    if ( rain.isActive() ) {
        // The rain materials are blended: the renderer defers these draws
        // into the blended flush (main view only, like upstream's).
        rain.draw( *pRenderer, params.CameraPosition );
    }
    drawObjects( *pRenderer, ePass::Opaque );
    pRenderer->setOcclusionGrid( nullptr );
    static const bool bLogOcclusion = CmdLine::HasArg( "-logocclusion" );
    if ( bLogOcclusion ) {
        static uint32_t frames = 0, sumTested = 0, sumCulled = 0, framesWithGrid = 0;
        uint32_t tested = 0, culled = 0;
        pRenderer->takeOcclusionCounts( tested, culled );
        sumTested += tested;
        sumCulled += culled;
        framesWithGrid += pOcclusionGrid != nullptr ? 1 : 0;
        if ( ++frames == 120 ) {
            OTDU_LOG_INFO( "[occlusion] cull: %u of %u boxes hidden per frame (pre-pass + opaque, %.1f %%), grid on %u/120 "
                           "frames\n",
                           sumCulled / frames, sumTested / frames,
                           sumTested > 0 ? 100.0 * sumCulled / sumTested : 0.0, framesWithGrid );
            frames = sumTested = sumCulled = framesWithGrid = 0;
        }
    }
    if ( !bNoSunFlare ) {
        sun.drawFlares( *pRenderer, params, pWeather );
    }
    device.endRenderTarget();
    static const bool bShowDraws = CmdLine::HasArg( "-showdraws" );
    if ( bShowDraws ) {
        OTDU_LOG_INFO( "EdenSceneView: %u draws\n", pRenderer->getNumDrawCalls() );
    }

    device.endGpuPass();
    gSceneTimes.mark( SceneTimes::Opaque );
    // 2. gViewPos comes from the Z pre-pass (step 0).

    // 3. Blended draws (projected shadow boxes read gViewPos, glass,
    //    clouds...).
    device.beginGpuPass( "blended" );
    device.beginRenderTarget( pSceneTarget, false );
    pRenderer->setScreenTexture( &pViewPosTarget->Color );
    pRenderer->resumeScene();
    // Blended scene draws (heightfields with their alpha blended borders,
    // glass...) before the haze: the haze covers the whole scene.
    pRenderer->flushDeferred();
    // Port effects (wheel spray, tyre tracks): depth tested against the scene,
    // no depth write, before the haze so they are fogged like everything else.
    if ( bEffectsPass && drawCallback ) {
        drawCallback( *pRenderer, ePass::Effects );
    }
    const bool bNoHaze = bMapView || !bHazeEnabled || CmdLine::HasArg( "-nohaze" ) || GetGameOptions().Haze == 0;
    // -loghaze: the haze / fog inputs of the drawn weather whenever it changes.
    static const bool bLogHaze = CmdLine::HasArg( "-loghaze" );
    static const WeatherPreset *pLoggedWeather = nullptr;
    if ( bLogHaze && pWeather != pLoggedWeather ) {
        pLoggedWeather = pWeather;
        if ( pWeather != nullptr ) {
            OTDU_LOG_INFO( "EdenSceneView: weather '%s' Skydome_haze %.3f fog %.0f..%.0f max %.3f%s",
                           pWeather->Name.c_str(), pWeather->getFloat( "Skydome_haze", 0.0f ),
                           pWeather->getFloat( "fog_start", -1.0f ), pWeather->getFloat( "fog_end", -1.0f ),
                           pWeather->getFloat( "fog_max", -1.0f ), "\n" );
        }
    }
    if ( !bNoHaze ) {
        postFx.drawHaze( *pRenderer, &pViewPosTarget->Color, pWeather );
    }
    pRenderer->endScene();
    device.endRenderTarget();
    pRenderer->setScreenTexture( nullptr );
    pRenderer->setShadowRealTimeTexture( nullptr );
    planarReflections.unbind( *pRenderer );
    device.endGpuPass();
    gSceneTimes.mark( SceneTimes::Blended );

    // Ray traced ambient occlusion (-rtao <samples>): one compute dispatch
    // against the frame TLAS (rebuilt at endFrame - one frame stale, the world
    // barely moves in that time). The mask multiplies onto the scene in the
    // swapchain blit (draw); the GPU cost scales with the sample count and is
    // the "rtao" pass of the [perf] gpu line. The terrain heightfield is not
    // in the TLAS (heights, not positions), so ground contact is occluded
    // less than it should.
    bAORan = false;
    static const int32_t aoSamples = CmdLine::GetArgInt( "-rtao", 0 );
    if ( pAOTarget != nullptr && aoSamples > 0 && gpRender->isRayTracingWorldActive() ) {
        RayTracing::Tlas *pTlas = gpRender->getRtTlas();
        RayTracing &rayTracing = gpRender->getRayTracing();
        if ( pTlas != nullptr && rayTracing.isReady() ) {
            static constexpr float kAORadius = 3.0f; // metres
            const Matrix4x4f basis = EdenRotationFromQuaternion( camera.Rotation );
            const Vector3f right = basis.transformVector( Vector3f( 1.0f, 0.0f, 0.0f ) );
            const Vector3f up = basis.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) );
            const Vector3f forward = basis.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
            const float tanHalf = tanf( effectiveFov * 0.5f );
            const float aspect =
                static_cast<float>( pSceneTarget->Width ) / static_cast<float>( pSceneTarget->Height );
            RayTracing::AoParams ao = {};
            ao.CameraPosition[0] = camera.Position.x;
            ao.CameraPosition[1] = camera.Position.y;
            ao.CameraPosition[2] = camera.Position.z;
            ao.Right[0] = right.x * tanHalf * aspect;
            ao.Right[1] = right.y * tanHalf * aspect;
            ao.Right[2] = right.z * tanHalf * aspect;
            ao.Up[0] = up.x * tanHalf;
            ao.Up[1] = up.y * tanHalf;
            ao.Up[2] = up.z * tanHalf;
            ao.Forward[0] = forward.x;
            ao.Forward[1] = forward.y;
            ao.Forward[2] = forward.z;
            ao.Params[0] = camera.Near;
            ao.Params[1] = camera.Far;
            ao.Params[2] = kAORadius;
            ao.Params[3] = static_cast<float>( gpRender->getDevice().getFrameNumber() );
            ao.Params2[0] = static_cast<float>( pAOTarget->Width );
            ao.Params2[1] = static_cast<float>( pAOTarget->Height );
            ao.Params2[2] = static_cast<float>( Min( aoSamples, 1024 ) );
            device.beginGpuPass( "rtao" );
            bAORan = rayTracing.dispatchAO( device.getCommandBuffer(), pTlas, &pSceneTarget->Depth, &pAOTarget->Color,
                                            ao, device.getFrameSlot() );
            device.endGpuPass();
        }
    }

    device.beginGpuPass( "post" );
    // 4. Photoshop (color balance / soft light / overlay / bloom LUT).
    const bool bNoPhotoshop = CmdLine::HasArg( "-nophotoshop" ) || GetGameOptions().ColorGrading == 0;
    if ( !bNoPhotoshop ) {
        postFx.applyPhotoshop( *pRenderer, pSceneTarget, pFinalTarget, pWeather );
    }
    // 5. The windscreen droplets (Droplets_Windshield): the final blit
    //    refracts the scene with this RT while the effect is active. The
    //    first pass here initializes the RT (its bInitialized lags one frame
    //    otherwise).
    if ( windscreen.isActive() && pDropletTarget != nullptr ) {
        static bool bLoggedFirst = false;
        if ( !bLoggedFirst ) {
            bLoggedFirst = true;
            OTDU_LOG_INFO( "EdenSceneView: droplet rt pass (%ux%u)\n", pDropletTarget->Width, pDropletTarget->Height );
        }
        device.beginRenderTarget( pDropletTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 0.0f ) );
        windscreen.renderDroplets( gpRender->getDevice() );
        device.endRenderTarget();
    }
    sceneParams = params;
    device.endGpuPass();
    gSceneTimes.mark( SceneTimes::Post );
    if ( bLogTimes ) {
        gSceneTimes.endFrame();
    }
}

void EdenSceneView::draw() {
    if ( !bReady || pSceneTarget == nullptr || !pSceneTarget->bInitialized ) {
        return;
    }
    GPUTexture *pColor =
        pFinalTarget != nullptr && pFinalTarget->bInitialized ? &pFinalTarget->Color : &pSceneTarget->Color;
    static const bool bShowViewPos = CmdLine::HasArg( "-showviewpos" );
    if ( bShowViewPos ) {
        pColor = &pViewPosTarget->Color;
    }
    // -showaomask: the ray traced ambient occlusion mask instead of the scene.
    static const bool bShowAO = CmdLine::HasArg( "-showaomask" );
    if ( bShowAO && pAOTarget != nullptr && bAORan ) {
        pColor = &pAOTarget->Color;
    }
    static const bool bShowShadow = CmdLine::HasArg( "-showshadowbuffer" );
    if ( bShowShadow && shadows.getScreenShadow() != nullptr ) {
        pColor = &shadows.getScreenShadow()->Color;
    }
    // -showslicemap N: the light space depth map of shadow slice N.
    if ( CmdLine::HasArg( "-showslicemap" ) ) {
        if ( RenderTarget *pSlice = shadows.getSliceMap( CmdLine::GetArgInt( "-showslicemap", 0 ) ) ) {
            pColor = &pSlice->Color;
        }
    }
    // Ocean reflection buffer (SEAREF_N, 256^2 mirror camera).
    static const bool bShowSeaRefl = CmdLine::HasArg( "-showsearefl" );
    if ( bShowSeaRefl ) {
        if ( GPUTexture *pSeaRefl = ocean.getReflectionTexture() ) {
            pColor = pSeaRefl;
        }
    }
    // -showdroplets: the windscreen droplet RT raw (normal.xy + presence).
    static const bool bShowDroplets = CmdLine::HasArg( "-showdroplets" );
    if ( bShowDroplets && pDropletTarget != nullptr && pDropletTarget->bInitialized ) {
        pColor = &pDropletTarget->Color;
    }
    // Ocean wave normal map (NoiseNMap, 256^2: the noise volume composed).
    static const bool bShowSeaNMap = CmdLine::HasArg( "-showseanmap" );
    if ( bShowSeaNMap ) {
        if ( GPUTexture *pSeaNMap = ocean.getNormalMapTexture() ) {
            pColor = pSeaNMap;
        }
    }
    static const int32_t showFace = CmdLine::HasArg( "-showenvface" ) ? CmdLine::GetArgInt( "-showenvface", 0 ) : -1;
    // -showenvscratch: the same face of the scratch cube (the copy source),
    // to tell a stale copy from an empty face render.
    static const bool bShowScratch = CmdLine::HasArg( "-showenvscratch" );
    GPUTexture face;
    if ( showFace >= 0 && showFace < 6 && pEnvironmentTarget != nullptr ) {
        RenderTarget *pShown = bShowScratch && pEnvironmentScratch != nullptr ? pEnvironmentScratch : pEnvironmentTarget;
        face = pShown->Color;
        face.View = pShown->LayerViews[showFace];
        face.ViewType = VK_IMAGE_VIEW_TYPE_2D;
        pColor = &face;
    }
    const GameOptions &options = GetGameOptions();
    const bool bPlain = options.Upscaler == 0 && options.Sharpness == 0 && options.EdgeSmoothing == 0 &&
                        pColor->Width == gpRender->getDevice().getBackbufferWidth();
    // Rain on the windscreen: the final blit becomes the droplet composite
    // (the scene refracted through the Droplets_Windshield RT). The debug
    // views keep their stock blits.
    if ( windscreen.isActive() && pDropletTarget != nullptr && pDropletTarget->bInitialized && !bShowViewPos &&
         !bShowAO && !bShowShadow && showFace < 0 && !bShowSeaRefl && !bShowDroplets ) {
        GPUTexture *ppTextures[2] = { pColor, &pDropletTarget->Color };
        const float params[8] = { static_cast<float>( pColor->Width ),
                                  static_cast<float>( pColor->Height ),
                                  static_cast<float>( Clamp( options.Sharpness, 0, 10 ) ) / 10.0f,
                                  options.EdgeSmoothing != 0 ? 1.0f : 0.0f,
                                  options.Upscaler == 1 ? 1.0f : 0.0f,
                                  windscreen.refractionStrength(),
                                  0.0f,
                                  0.0f };
        fullscreen.draw( gpRender->getDevice(), kSPIRV_windscreen_frag, sizeof( kSPIRV_windscreen_frag ), ppTextures,
                         2, params, sizeof( params ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Linear );
        return;
    }
    if ( bPlain ) {
        // The AO mask multiplies on in place of the plain blit (-rtao); the
        // upscaler path does not sample it (defaults are plain).
        if ( pAOTarget != nullptr && bAORan && !bShowViewPos && !bShowShadow && !bShowAO ) {
            GPUTexture *ppAOTextures[2] = { pColor, &pAOTarget->Color };
            fullscreen.draw( gpRender->getDevice(), kSPIRV_rt_ao_combine_frag, sizeof( kSPIRV_rt_ao_combine_frag ),
                             ppAOTextures, 2 );
            return;
        }
        // The blit shader always takes the speed-blur push constant (a zero
        // strength is a plain copy; the retail blur_int_* data drives it).
        const float blitParams[4] = { speedBlur * 0.03f, 0.0f, 0.0f, 0.0f };
        fullscreen.draw( gpRender->getDevice(), kSPIRV_blit_frag, sizeof( kSPIRV_blit_frag ), &pColor, 1,
                         blitParams, sizeof( blitParams ) );
        return;
    }
    const float params[8] = { static_cast<float>( pColor->Width ), static_cast<float>( pColor->Height ),
                              static_cast<float>( Clamp( options.Sharpness, 0, 10 ) ) / 10.0f,
                              options.EdgeSmoothing != 0 ? 1.0f : 0.0f, options.Upscaler == 1 ? 1.0f : 0.0f,
                              speedBlur * 0.03f, 0.0f, 0.0f };
    fullscreen.draw( gpRender->getDevice(), kSPIRV_upscale_frag, sizeof( kSPIRV_upscale_frag ), &pColor, 1, params,
                     sizeof( params ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Linear );
}

bool EdenSceneView::loadClouds( const std::string &islandFolder ) {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const std::string path = islandFolder + "fx/clouds.bnk";
    if ( !cloudsBank.load( path.c_str() ) ) {
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( cloudsBank, cloudsBank.getPath().c_str() );
    return clouds.load( *pRenderer, cloudsBank, "clouds", nullptr, cloudsBank.getPath().c_str() );
}

void EdenSceneView::drawClouds( EdenRenderer &renderer ) {
    static const bool bNoClouds = CmdLine::HasArg( "-noclouds" ); // A/B
    if ( bNoClouds || !clouds.isLoaded() ) {
        return;
    }
    // clouds_cloud3dname "Cloud_X": CLOUD_X (billboard layers) + SKYCLO_X
    // (dome mesh) subtrees of clouds.3dd.
    const std::string name = pWeather != nullptr ? ToUpper( pWeather->getString( "clouds_cloud3dname" ) ) : "CLOUD_B";
    const std::string suffix = name.size() >= 2 ? "_" + name.substr( name.size() - 1 ) + "_" : "_B_";
    if ( suffix != cloudsSuffix ) {
        cloudsSuffix = suffix;
        const Render3DD &scene = clouds.getScene();
        const std::vector<SceneNode> &nodes = scene.getNodes();
        std::vector<int32_t> roots;
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            const std::string &nodeName = nodes[i].Name;
            if ( nodeName.find( "UD" + suffix ) != std::string::npos ||
                 nodeName.find( "CLO" + suffix ) != std::string::npos ) {
                roots.push_back( static_cast<int32_t>( i ) );
            }
        }
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            bool bVisible = false;
            for ( int32_t root : roots ) {
                bVisible |= static_cast<int32_t>( i ) == root || scene.isDescendantOf( static_cast<int32_t>( i ), root );
            }
            clouds.setNodeVisible( static_cast<int32_t>( i ), bVisible );
        }
    }
    auto vector4 = [this]( const char *pName, const Vector4f &defaultValue ) {
        return pWeather != nullptr ? pWeather->getVector4( pName, defaultValue ) : defaultValue;
    };
    EdenDrawParams params;
    params.Vectors["__Instance_Color"] = vector4( "clouds_cloud3dcolor", Vector4f( 1.0f, 1.0f, 1.0f, 0.0f ) );
    params.Vectors["ColorDown"] = vector4( "clouds_cloud3dcolor2", Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
    params.Vectors["AlphaBlend"] = vector4( "clouds_cloud3dAlphaBlend", Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );
    params.Vectors["__DirLight_Emissive_0"] = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
    const float haze = pWeather != nullptr ? pWeather->getFloat( "Skydome_haze", 0.0f ) : 0.0f;
    params.Vectors["Haze"] = Vector4f( haze, haze, haze, haze );
    clouds.draw( renderer, Matrix4x4f::Identity(), &params );
}
