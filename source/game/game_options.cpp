#include "shared.h"
#include "game_options.h"

#include "audio/gs_audio.h"
#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

#include <SDL3/SDL.h>
#include <algorithm>

#include <fstream>

namespace {
struct OptionField {
    const char *pKey;
    int32_t GameOptions::*pValue;
};

constexpr OptionField kFields[] = {
    { "master_volume", &GameOptions::MasterVolume },   { "music_volume", &GameOptions::MusicVolume },
    { "vehicles_volume", &GameOptions::VehiclesVolume }, { "voices_volume", &GameOptions::VoicesVolume },
    { "sfx_volume", &GameOptions::SfxVolume },         { "voiceip_volume", &GameOptions::VoiceIPVolume },
    { "mike", &GameOptions::Mike },
    { "speaker_layout", &GameOptions::SpeakerLayout }, { "dualsense", &GameOptions::DualSense },
    { "ffb", &GameOptions::FFB },
    { "ambience", &GameOptions::Ambience },
    { "video_width", &GameOptions::VideoWidth },       { "video_height", &GameOptions::VideoHeight },
    { "vsync", &GameOptions::VSync },                  { "environment_quality", &GameOptions::EnvQuality },
    { "window_mode", &GameOptions::WindowMode },
    { "anti_alias", &GameOptions::AntiAlias },         { "car_quality", &GameOptions::CarQuality },
    { "texture_quality", &GameOptions::TextureQuality }, { "fx_quality", &GameOptions::FxQuality },
    { "gamma", &GameOptions::Gamma },
    { "quality_profile", &GameOptions::QualityProfile },
    { "render_scale", &GameOptions::RenderScale },     { "upscaler", &GameOptions::Upscaler },
    { "sharpness", &GameOptions::Sharpness },          { "edge_smoothing", &GameOptions::EdgeSmoothing },
    { "color_grading", &GameOptions::ColorGrading },   { "haze", &GameOptions::Haze },
    { "sun_flare", &GameOptions::SunFlare },           { "shadows", &GameOptions::Shadows },
    { "shadow_distance", &GameOptions::ShadowDistance },
    { "fps_limit", &GameOptions::FpsLimit },
    { "hud_quality", &GameOptions::HudQuality },
    { "ocean_reflections", &GameOptions::OceanReflections },
    { "draw_distance", &GameOptions::DrawDistance },
    { "water_clarity", &GameOptions::WaterClarity },
    { "water_tide", &GameOptions::WaterTide },
    { "ray_tracing", &GameOptions::RayTracing },
    { "ray_tracing_world", &GameOptions::RayTracingWorld },
    { "button_prompts", &GameOptions::ButtonPrompts },
    { "units", &GameOptions::Units },
    { "ui_layout", &GameOptions::UiLayout },
    { "fov_scale", &GameOptions::FovScale },
    { "screen_ratio", &GameOptions::ScreenRatioOverride },
};

std::filesystem::path OptionsPath() {
    return gpFile != nullptr ? gpFile->getUserDataFolder() / "options.ini" : std::filesystem::path( "options.ini" );
}

// -profile <name> (the same names as the Advanced graphics row).
int32_t ProfileFromName( const char *pName ) {
    if ( pName == nullptr ) {
        return 0;
    }
    std::string name = ToLower( pName );
    name.erase( std::remove( name.begin(), name.end(), '-' ), name.end() );
    name.erase( std::remove( name.begin(), name.end(), '_' ), name.end() );
    if ( name == "high" ) {
        return 1;
    }
    if ( name == "medium" || name == "balanced" ) {
        return 2;
    }
    if ( name == "low" || name == "performance" ) {
        return 3;
    }
    if ( name == "steamdeck" || name == "deck" ) {
        return 4;
    }
    if ( name == "legiongo" || name == "legion" ) {
        return 5;
    }
    if ( name == "60" || name == "60fps" || name == "fps60" ) {
        return 6;
    }
    return 0;
}
} // namespace

void ApplyQualityProfile( GameOptions &options, int32_t profile ) {
    profile = Clamp( profile, 0, 6 );
    options.QualityProfile = profile;
    switch ( profile ) {
    case 1: // High: the retail look, no compromise (the default).
        options.VideoWidth = 0; // keep the desktop / SystemPC size
        options.VideoHeight = 0;
        options.RenderScale = 100;
        options.Upscaler = 0;
        options.Sharpness = 0;
        options.ColorGrading = 1;
        options.Haze = 1;
        options.SunFlare = 1;
        options.Shadows = 1;
        options.ShadowDistance = 100;
        options.FpsLimit = 0;
        options.HudQuality = 2;
        options.OceanReflections = 1;
        options.DrawDistance = 100;
        options.EnvQuality = 3;
        break;
    case 2: // Medium: 85 % scene, half rate water mirror, 85 % draw distance.
        options.VideoWidth = 0;
        options.VideoHeight = 0;
        options.RenderScale = 85;
        options.Upscaler = 1;
        options.Sharpness = 2;
        options.ColorGrading = 1;
        options.Haze = 1;
        options.SunFlare = 1;
        options.Shadows = 1;
        options.ShadowDistance = 50;
        options.FpsLimit = 0;
        options.HudQuality = 2;
        options.OceanReflections = 2;
        options.DrawDistance = 85;
        options.EnvQuality = 2;
        break;
    case 3: // Low: no shadows / reflection / post, 60 % draw distance, cheap HUD.
        options.VideoWidth = 0;
        options.VideoHeight = 0;
        options.RenderScale = 70;
        options.Upscaler = 1;
        options.Sharpness = 2;
        options.ColorGrading = 0;
        options.Haze = 0;
        options.SunFlare = 0;
        options.Shadows = 0;
        options.ShadowDistance = 50;
        options.FpsLimit = 60;
        options.HudQuality = 1;
        options.OceanReflections = 0;
        options.DrawDistance = 60;
        options.EnvQuality = 1;
        break;
    case 4: // Steam Deck: 1280x800 panel, 67 % scene (858x536), 60 fps cap.
        options.VideoWidth = 1280;
        options.VideoHeight = 800;
        options.RenderScale = 67;
        options.Upscaler = 1;
        options.Sharpness = 2;
        options.ColorGrading = 1;
        options.Haze = 1;
        options.SunFlare = 0;
        options.Shadows = 0;
        options.ShadowDistance = 50;
        options.FpsLimit = 60;
        options.HudQuality = 1;
        options.OceanReflections = 0;
        options.DrawDistance = 50;
        options.EnvQuality = 1;
        break;
    case 5: // Legion Go: 2560x1600 panel driven at 1920x1200, 67 % scene
        // (1286x804), static water mirror, 60 fps cap.
        options.VideoWidth = 1920;
        options.VideoHeight = 1200;
        options.RenderScale = 67;
        options.Upscaler = 1;
        options.Sharpness = 2;
        options.ColorGrading = 1;
        options.Haze = 1;
        options.SunFlare = 0;
        options.Shadows = 0;
        options.ShadowDistance = 50;
        options.FpsLimit = 60;
        options.HudQuality = 1;
        options.OceanReflections = 0;
        options.DrawDistance = 50;
        options.EnvQuality = 2;
        break;
    case 6: // 60 fps (desktop): full render resolution, no sun shadows, the
        // water mirror rendered once, 75 % vegetation; the look (grading, haze,
        // flare) stays. ~15 ms CPU on the FREERIDE benchmark.
        options.VideoWidth = 0;
        options.VideoHeight = 0;
        options.RenderScale = 100;
        options.Upscaler = 0;
        options.Sharpness = 0;
        options.ColorGrading = 1;
        options.Haze = 1;
        options.SunFlare = 1;
        options.Shadows = 0;
        options.ShadowDistance = 50;
        options.FpsLimit = 60;
        options.HudQuality = 2;
        options.OceanReflections = 0;
        options.DrawDistance = 75;
        options.EnvQuality = 2;
        break;
    default: // 0 Custom: keep the player's own values.
        break;
    }
}

GameOptions &GetGameOptions() {
    static GameOptions options;
    return options;
}

void LoadGameOptions() {
    std::ifstream file( OptionsPath() );
    if ( file ) {
        GameOptions &options = GetGameOptions();
        std::string line;
        while ( std::getline( file, line ) ) {
            const size_t equal = line.find( '=' );
            if ( equal == std::string::npos ) {
                continue;
            }
            std::string key = line.substr( 0, equal );
            key.erase( key.find_last_not_of( " \t" ) + 1 );
            const int32_t value = atoi( line.c_str() + equal + 1 );
            for ( const OptionField &field : kFields ) {
                if ( key == field.pKey ) {
                    options.*field.pValue = value;
                }
            }
        }
        OTDU_LOG_INFO( "GameOptions: loaded '%s'\n", OptionsPath().string().c_str() );
    }
    // -imperial / -metric: the command line has the last word on the unit
    // system (a headless A/B needs no options.ini edit), like -envquality on the
    // video options above. Not saved: a run does not overwrite the player's pick.
    GameOptions &options = GetGameOptions();
    if ( CmdLine::HasArg( "-imperial" ) ) {
        options.Units = 1;
    } else if ( CmdLine::HasArg( "-metric" ) ) {
        options.Units = 0;
    }
    // -borderless / -fullscreen / -w: the display mode for this run, on top of
    // the SystemPC.cpr SetWindowed flag of the window creation (-fullscreen /
    // -w also set VideoSettings::bWindowed in GSConfig, so the created window
    // already matches and ApplyWindowMode only confirms it) [INF].
    if ( CmdLine::HasArg( "-borderless" ) ) {
        options.WindowMode = GameOptions::kWindowModeBorderless;
    } else if ( CmdLine::HasArg( "-fullscreen" ) ) {
        options.WindowMode = GameOptions::kWindowModeFullscreen;
    } else if ( CmdLine::HasArg( "-w" ) ) {
        options.WindowMode = GameOptions::kWindowModeWindowed;
    }
    // -waterclear <0..20>: the sea's clarity for this run (0 restores the opaque
    // retail water), like -imperial above.
    if ( const char *pClear = CmdLine::GetArgValue( "-waterclear" ) ) {
        options.WaterClarity = Clamp( atoi( pClear ), 0, 20 );
    }
    // -tide <0..20 cm> / -notide: the shore swell of the sea for this run (0 =
    // the still retail water plane).
    if ( CmdLine::HasArg( "-notide" ) ) {
        options.WaterTide = 0;
    } else if ( const char *pTide = CmdLine::GetArgValue( "-tide" ) ) {
        options.WaterTide = Clamp( atoi( pTide ), 0, 20 );
    }
    // -profile <name>: apply a quality preset for this run (not saved). The
    // individual overrides after it win, so a test can do e.g.
    // -profile steamdeck -drawdistance 75 -oceanreflections 1.
    if ( const char *pProfile = CmdLine::GetArgValue( "-profile" ) ) {
        const int32_t profile = ProfileFromName( pProfile );
        if ( profile > 0 ) {
            ApplyQualityProfile( options, profile );
        } else {
            OTDU_LOG_WARN( "Unknown -profile '%s' (high, medium, low, 60, steamdeck, legiongo)\n", pProfile );
        }
    }
    if ( const char *pHud = CmdLine::GetArgValue( "-hudquality" ) ) {
        options.HudQuality = Clamp( atoi( pHud ), 1, 2 );
    }
    if ( const char *pReflections = CmdLine::GetArgValue( "-oceanreflections" ) ) {
        options.OceanReflections = Clamp( atoi( pReflections ), 0, 8 );
    }
    if ( const char *pDistance = CmdLine::GetArgValue( "-drawdistance" ) ) {
        options.DrawDistance = Clamp( atoi( pDistance ), 50, 150 );
    }
    if ( const char *pShadowDistance = CmdLine::GetArgValue( "-shadowdistance" ) ) {
        options.ShadowDistance = Clamp( atoi( pShadowDistance ), 50, 150 );
    }
    // -fov <pct>: the camera FOV scale for this run (like -drawdistance: a
    // headless A/B needs no options.ini edit).
    if ( const char *pFov = CmdLine::GetArgValue( "-fov" ) ) {
        options.FovScale = Clamp( atoi( pFov ), 50, 150 );
    }
    OTDU_LOG_INFO( "Graphics: profile %d, render scale %d %%, draw distance %d %%, shadows %d @ %d %%, "
                   "water reflections %d, HUD %d, LOD level %d\n",
                   options.QualityProfile, options.RenderScale, options.DrawDistance, options.Shadows,
                   options.ShadowDistance, options.OceanReflections, options.HudQuality, options.EnvQuality );
}

void SaveGameOptions() {
    std::ofstream file( OptionsPath() );
    if ( !file ) {
        OTDU_LOG_WARN( "GameOptions: cannot write '%s'\n", OptionsPath().string().c_str() );
        return;
    }
    const GameOptions &options = GetGameOptions();
    for ( const OptionField &field : kFields ) {
        file << field.pKey << " = " << options.*field.pValue << "\n";
    }
    OTDU_LOG_INFO( "GameOptions: saved '%s'\n", OptionsPath().string().c_str() );
}

void ApplyAudioOptions() {
    if ( gpAudio == nullptr ) {
        return;
    }
    const GameOptions &options = GetGameOptions();
    // Linear 0..20 -> 0..1 [INF: the retail curve is not identified].
    auto gain = []( int32_t value ) {
        return static_cast<float>( Clamp( value, 0, GameOptions::kVolumeMax ) ) / GameOptions::kVolumeMax;
    };
    gpAudio->setMixerVolume( "Master", gain( options.MasterVolume ) );
    gpAudio->setMixerVolume( "Music", gain( options.MusicVolume ) );
    gpAudio->setMixerVolume( "Vehicles", gain( options.VehiclesVolume ) );
    gpAudio->setMixerVolume( "Voices", gain( options.VoicesVolume ) );
    gpAudio->setMixerVolume( "SFX", gain( options.SfxVolume ) );
    gpAudio->setMixerVolume( "VoicePlayers", gain( options.VoiceIPVolume ) );
    // SetOutputNbSpeakers: only a real change re-opens the device (the mixer
    // drops its live voices when it does).
    gpAudio->setSpeakerLayout( options.SpeakerLayout );
}

std::vector<std::pair<int32_t, int32_t>> GetVideoResolutions() {
    std::vector<std::pair<int32_t, int32_t>> list;
    int count = 0;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if ( SDL_DisplayMode **ppModes = SDL_GetFullscreenDisplayModes( display, &count ) ) {
        for ( int i = 0; i < count; i++ ) {
            if ( ppModes[i]->w >= 800 && ppModes[i]->h >= 600 ) {
                list.emplace_back( ppModes[i]->w, ppModes[i]->h );
            }
        }
        SDL_free( ppModes );
    }
    if ( gpConfig != nullptr ) {
        list.emplace_back( gpConfig->getVideoSettings().Width, gpConfig->getVideoSettings().Height );
    }
    std::sort( list.begin(), list.end() );
    list.erase( std::unique( list.begin(), list.end() ), list.end() );
    return list;
}

int32_t GetDisplayRefreshRate() {
    const SDL_DisplayMode *pMode = SDL_GetDesktopDisplayMode( SDL_GetPrimaryDisplay() );
    return pMode != nullptr ? static_cast<int32_t>( pMode->refresh_rate + 0.5f ) : 0;
}

// WindowMode with -1 ("not chosen yet") resolved: the mode the window was
// created with - SystemPC.cpr SetWindowed. The creation flag without a
// display mode is the borderless fullscreen desktop (tdu_instance.cpp
// createWindow), so a fullscreen SystemPC.cpr stands for Borderless, not
// exclusive. The menu row's label and its first step start from this too, so
// they never show a mode the window is not in.
int32_t EffectiveWindowMode() {
    const int32_t mode = GetGameOptions().WindowMode;
    if ( mode >= 0 ) {
        return mode;
    }
    return gpConfig != nullptr && !gpConfig->getVideoSettings().bWindowed
               ? GameOptions::kWindowModeBorderless
               : GameOptions::kWindowModeWindowed;
}

// Sets the fullscreen display mode closest to the video size and reports the
// picked size (0x0 = the desktop mode itself, only while no resolution is
// chosen or none enumerates). Exclusive and borderless fullscreen share the
// pick, which is what makes the Resolution row live outside windowed mode:
// exclusive switches the scanout, borderless keeps the desktop presentation
// at the picked size - and the swapchain follows the window, so the render
// resolution really changes [INF: retail has no runtime display mode].
static bool ApplyFullscreenMode( SDL_Window *pWindow, int32_t &outWidth, int32_t &outHeight ) {
    outWidth = 0;
    outHeight = 0;
    const GameOptions &options = GetGameOptions();
    if ( options.VideoWidth <= 0 || options.VideoHeight <= 0 ) {
        return SDL_SetWindowFullscreenMode( pWindow, nullptr );
    }
    int count = 0;
    SDL_DisplayID display = SDL_GetDisplayForWindow( pWindow );
    SDL_DisplayMode **ppModes = SDL_GetFullscreenDisplayModes( display, &count );
    SDL_DisplayMode *pBest = nullptr;
    if ( ppModes != nullptr && count > 0 ) {
        pBest = ppModes[0];
        for ( int i = 1; i < count; i++ ) {
            const int32_t distance = std::abs( ppModes[i]->w - options.VideoWidth ) +
                                     std::abs( ppModes[i]->h - options.VideoHeight );
            const int32_t bestDistance =
                std::abs( pBest->w - options.VideoWidth ) + std::abs( pBest->h - options.VideoHeight );
            if ( distance < bestDistance ) {
                pBest = ppModes[i];
            }
        }
    }
    // Applied before the list is freed (SDL copies the mode out).
    const bool bApplied = SDL_SetWindowFullscreenMode( pWindow, pBest );
    if ( pBest != nullptr ) {
        outWidth = pBest->w;
        outHeight = pBest->h;
    }
    if ( ppModes != nullptr ) {
        SDL_free( ppModes );
    }
    return bApplied;
}

// Display mode (WindowMode): windowed, borderless fullscreen desktop or
// exclusive fullscreen, switched at runtime with the SDL3 window API. Only a
// change acts (the menu re-runs ApplyVideoOptions on every row step); the
// swapchain follows through the window size events / the explicit recreate
// request [INF: retail has no runtime display mode].
static void ApplyWindowMode() {
    if ( gpRender == nullptr ) {
        return;
    }
    SDL_Window *pWindow = gpRender->getDevice().getWindow();
    if ( pWindow == nullptr ) {
        return;
    }
    // What is applied, mode AND fullscreen size: a Resolution step in a
    // fullscreen mode re-picks the display mode (a windowed resize is the
    // SDL_SetWindowSize of ApplyVideoOptions below, never this). Cancel's
    // backup restore walks back through here the same way.
    static int32_t appliedMode = -1;
    static int32_t appliedWidth = -1;
    static int32_t appliedHeight = -1;
    const int32_t mode = EffectiveWindowMode();
    const GameOptions &options = GetGameOptions();
    const int32_t width = mode != GameOptions::kWindowModeWindowed ? options.VideoWidth : -1;
    const int32_t height = mode != GameOptions::kWindowModeWindowed ? options.VideoHeight : -1;
    if ( mode == appliedMode && width == appliedWidth && height == appliedHeight ) {
        return;
    }
    bool bApplied = false;
    int32_t pickedWidth = -1;
    int32_t pickedHeight = -1;
    if ( mode == GameOptions::kWindowModeWindowed ) {
        // Leaving fullscreen restores the bordered window at the video size.
        bApplied = SDL_SetWindowFullscreen( pWindow, false ) &&
                   SDL_SetWindowBordered( pWindow, true );
    } else {
        // Both fullscreen flavors ride the display mode closest to the video
        // size; a null mode = the borderless fullscreen desktop, only while no
        // resolution is chosen. (Exclusive with a failed enumeration degrades
        // to the same null mode - the picked 0x0 then re-applies on the next
        // Resolution step instead of latching.)
        bApplied = ApplyFullscreenMode( pWindow, pickedWidth, pickedHeight ) &&
                   SDL_SetWindowFullscreen( pWindow, true );
    }
    if ( bApplied ) {
        appliedMode = mode;
        appliedWidth = pickedWidth;
        appliedHeight = pickedHeight;
        const char *pName = mode == GameOptions::kWindowModeWindowed     ? "windowed"
                            : mode == GameOptions::kWindowModeBorderless ? "borderless fullscreen"
                                                                         : "exclusive fullscreen";
        if ( mode != GameOptions::kWindowModeWindowed && pickedWidth > 0 ) {
            OTDU_LOG_INFO( "Window mode: %s at %dx%d\n", pName, pickedWidth, pickedHeight );
        } else if ( mode != GameOptions::kWindowModeWindowed ) {
            OTDU_LOG_INFO( "Window mode: %s (desktop resolution)\n", pName );
        } else {
            OTDU_LOG_INFO( "Window mode: windowed\n" );
        }
        gpRender->getDevice().requestSwapchainRecreation();
    } else {
        OTDU_LOG_WARN( "Window mode: SDL refused the switch (%s)\n", SDL_GetError() );
    }
}

void ApplyVideoOptions() {
    if ( gpConfig == nullptr || gpRender == nullptr ) {
        return;
    }
    ApplyWindowMode();
    const GameOptions &options = GetGameOptions();
    SDL_Window *pWindow = gpRender->getDevice().getWindow();
    if ( options.VideoWidth > 0 && options.VideoHeight > 0 && pWindow != nullptr ) {
        gpConfig->setVideoSize( options.VideoWidth, options.VideoHeight );
        int width = 0, height = 0;
        SDL_GetWindowSize( pWindow, &width, &height );
        if ( ( width != options.VideoWidth || height != options.VideoHeight ) &&
             ( SDL_GetWindowFlags( pWindow ) & SDL_WINDOW_FULLSCREEN ) == 0 ) {
            SDL_SetWindowSize( pWindow, options.VideoWidth, options.VideoHeight );
        }
    }
    if ( options.VSync >= 0 ) {
        gpRender->getDevice().setVSync( options.VSync != 0 );
    }
    // Texture filtering: Low = trilinear, Medium = 4x, High = 16x anisotropic.
    {
        const uint32_t level = options.TextureQuality <= 1 ? 1u : options.TextureQuality == 2 ? 4u : 16u;
        RenderDevice &device = gpRender->getDevice();
        device.setMaxAnisotropy( level );
        static uint32_t appliedLevel = 1;
        if ( device.getMaxAnisotropy() != appliedLevel ) {
            appliedLevel = device.getMaxAnisotropy();
            if ( EdenRenderer *pEden = gpRender->getEdenRenderer() ) {
                pEden->refreshSamplers();
            }
        }
    }
    if ( options.EnvQuality >= 0 ) {
        gpConfig->setEnvironmentQuality( options.EnvQuality );
    }
}

float GetPostGamma() {
    // [INF] slider 0..20 -> 1.1 .. 0.5, the middle is the 0.8 the LUT pass
    // used before the option existed; the retail curve is not identified.
    const float t = static_cast<float>( Clamp( GetGameOptions().Gamma, 0, GameOptions::kGammaMax ) ) / GameOptions::kGammaMax;
    return 1.1f - 0.6f * t;
}

float GetRenderScale() {
    return static_cast<float>( Clamp( GetGameOptions().RenderScale, 50, 200 ) ) / 100.0f;
}

int32_t GetEffectiveScreenRatio() {
    const int32_t override_ = GetGameOptions().ScreenRatioOverride;
    if ( override_ == 1 || override_ == 2 ) {
        return override_;
    }
    // Auto: the window's own aspect. Retail only knew 16:9 (ratio 1) and 4:3
    // (ratio 2); a 16:10 or wider window still reads the 16:9 arrangements
    // (the narrow-aspect FOV law keeps their horizontal FOV), 4:3 and
    // narrower (5:4) read the 4:3 ones. The window when the renderer is not
    // up yet: the SystemPC.cpr video size.
    float aspect = 16.0f / 9.0f;
    if ( gpRender != nullptr ) {
        const uint32_t height = gpRender->getDevice().getBackbufferHeight();
        if ( height > 0 ) {
            aspect = static_cast<float>( gpRender->getDevice().getBackbufferWidth() ) / static_cast<float>( height );
        }
    } else if ( gpConfig != nullptr && gpConfig->getVideoSettings().Height > 0 ) {
        aspect = static_cast<float>( gpConfig->getVideoSettings().Width ) /
                 static_cast<float>( gpConfig->getVideoSettings().Height );
    }
    return aspect >= 1.5f ? 1 : 2;
}

void GetUiCanvasRect( float backbufferWidth, float backbufferHeight, float &outX0, float &outY0,
                      float &outWidth, float &outHeight ) {
    if ( GetGameOptions().UiLayout == GameOptions::kUiLayoutStretch || backbufferWidth <= 0.0f ||
         backbufferHeight <= 0.0f ) {
        // Stretch: the canvas covers the window (the retail mapping).
        outX0 = 0.0f;
        outY0 = 0.0f;
        outWidth = 1.0f;
        outHeight = 1.0f;
        return;
    }
    // Fit: a centered band of the canvas' own aspect (16:9), bars around it.
    const float windowAspect = backbufferWidth / backbufferHeight;
    constexpr float kCanvasAspect = 1280.0f / 720.0f;
    if ( windowAspect >= kCanvasAspect ) {
        outWidth = kCanvasAspect / windowAspect;
        outHeight = 1.0f;
    } else {
        outWidth = 1.0f;
        outHeight = windowAspect / kCanvasAspect;
    }
    outX0 = ( 1.0f - outWidth ) * 0.5f;
    outY0 = ( 1.0f - outHeight ) * 0.5f;
}
