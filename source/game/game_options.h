#pragma once

#include <cstdint>
#include <utility>
#include <vector>

// Player options of the OPTIONS screen (MAIN.vmf, the pause menu's Options
// entry). The movie owns the pages; the game fills their variables on
// 'opts:<page>:open', reads them back on the widget updates and keeps them
// on 'vali' (OK) / restores them on 'canc' (Cancel). Kept in
// <userdata>/options.ini (the retail save of these settings is the savegame,
// not implemented) [INF].
struct GameOptions {
    // Audio page: the sliders run 0..20 (initSlider(cmd, value, 20, ...),
    // MAIN.vmf options/audio) [DATA]. Each one is the gain of an
    // audiosetup.xmb mixer: Master / Music / Vehicles / Voices / SFX /
    // VoicePlayers (voice IP) [DATA: the mixer tree, -logaudiomixers]. 20 is
    // the full gain, i.e. the mix before this screen existed [INF default].
    static constexpr int32_t kVolumeMax = 20;
    int32_t MasterVolume = kVolumeMax;
    int32_t MusicVolume = kVolumeMax;
    int32_t VehiclesVolume = kVolumeMax;
    int32_t VoicesVolume = kVolumeMax;
    int32_t SfxVolume = kVolumeMax;
    int32_t VoiceIPVolume = kVolumeMax;
    // Micro (choice box: the voice chat microphone; online is not
    // implemented, the value is only kept).
    int32_t Mike = 0;
    // Speaker layout of the mixer (OpenTDU2, not a retail slider):
    // SetOutputNbSpeakers of SystemPC.cpr. 0 = the layout the audio device
    // reports, 2 stereo, 6 = 5.1, 8 = 7.1. Applied when the audio device opens
    // (a change re-opens it, options.ini + the Advanced graphics page).
    int32_t SpeakerLayout = 0;
    // DualSense extras of the pad (OpenTDU2): 1 = light bar + haptics on,
    // 0 = off (the same as -nods). Ignored by pads without the feature.
    int32_t DualSense = 1;
    // Wheel force feedback (OpenTDU2): 1 = the rim torque / damper of a
    // haptic steering wheel on, 0 = off (the same as -noffb). Ignored by
    // pads without a haptic device (input/wheel_ffb.h).
    int32_t FFB = 1;
    // Ambient beds (OpenTDU2): the island ambience of ambiences.xmb on / off.
    int32_t Ambience = 1;

    // Video page (opts:vide:). 0 / -1 = not chosen yet: the SystemPC.cpr value
    // stands. Saved in options.ini, applied at start-up by GSConfig.
    int32_t VideoWidth = 0;
    int32_t VideoHeight = 0;
    int32_t VSync = -1;     // 0 / 1
    int32_t EnvQuality = -1; // QUALITY_SETTINGS_LOD level 1..3 (Low / Medium / High)
    // Display mode (OpenTDU2, the Video page's own row): -1 = not chosen (the
    // SystemPC.cpr SetWindowed flag of the window creation stands), 0 = windowed,
    // 1 = borderless fullscreen desktop, 2 = exclusive fullscreen. Applied at
    // start-up by ApplyVideoOptions and live from the menu (SDL3
    // SDL_SetWindowFullscreen / SDL_SetWindowFullscreenMode) [INF: retail keeps
    // windowed mode in SystemPC.cpr and has no menu row for it]. -1 resolves
    // through EffectiveWindowMode: the creation flag without a display mode is
    // the borderless fullscreen desktop, so a fullscreen SystemPC.cpr stands
    // for Borderless (not exclusive).
    static constexpr int32_t kWindowModeWindowed = 0;
    static constexpr int32_t kWindowModeBorderless = 1;
    static constexpr int32_t kWindowModeFullscreen = 2;
    int32_t WindowMode = -1;
    // Kept, not applied yet: the renderer has no MSAA, no per-car or FX
    // quality tiers and no texture filtering levels.
    int32_t AntiAlias = 0;   // 0 = Off, 1 = On
    int32_t CarQuality = 3;  // 1..3
    int32_t TextureQuality = 3;
    int32_t FxQuality = 3;

    // Gamma page (opts:gama:): slider 0..20, 10 = the retail default look.
    static constexpr int32_t kGammaMax = 20;
    int32_t Gamma = 10;

    // Advanced graphics page (AdvancedGraphics): not part of the retail
    // options, written to options.ini too.
    // Quality profile (OpenTDU2): 0 Custom, 1 High, 2 Medium, 3 Low,
    // 4 Steam Deck, 5 Legion Go, 6 60 fps. Picking one applies its preset to
    // the rows below; editing any other row makes it Custom (game_options.cpp).
    int32_t QualityProfile = 0;
    int32_t RenderScale = 100;   // % of the window size the 3D scene renders at (50..200)
    int32_t Upscaler = 0;        // eUpscaler: 0 bilinear, 1 bicubic (Catmull-Rom), 2 AMD FSR 1.0, 3 AMD FSR 3, 4 Intel XeSS (needs a restart: the device is made with its requirements)
    int32_t Sharpness = 0;       // 0..10 contrast adaptive sharpening
    int32_t EdgeSmoothing = 0;   // 0 / 1, FXAA-like pass at render resolution
    int32_t ColorGrading = 1;    // the 'photoshop' pass (color balance, bloom, gamma)
    int32_t Haze = 1;            // atmospheric haze pass
    int32_t SunFlare = 1;
    int32_t Shadows = 1;         // real time sun shadows
    // Shadow caster distance relative to the game's MaxCastDistance (the
    // FREERIDE config casts up to 736 m): 50..150 %, 100 = the retail range.
    int32_t ShadowDistance = 100;
    int32_t FpsLimit = 0;        // 0 = unlimited (VSync still applies)
    // HUD detail (OpenTDU2): 2 = the HUD / GAUGES Flash movies, 1 = the
    // minimap + the built-in gauge only (the cheap HUD of the handheld
    // profiles, FreeRideHud::draw).
    int32_t HudQuality = 2;
    // Refresh of the water's mirror reflection (OpenTDU2): 0 = render once
    // (static), 1 = every frame, 2 = every other frame, N = every Nth frame.
    // The pass re-submits the world from the mirror camera (EdenSceneView).
    int32_t OceanReflections = 1;
    // Draw distance of the .pmi instances relative to the data's own LOD
    // distances (50..150 %). 100 = the retail distance (EdenWorld::drawInstances).
    int32_t DrawDistance = 100;
    // Water clarity of the sea (OpenTDU2, not a retail option): 0 = the opaque
    // retail TD_Ocean, 20 = half see-through (the sea bed shows through).
    // EdenOcean::setupMaterial turns it into the water's blend alpha.
    int32_t WaterClarity = 4;
    // Shore swell of the sea (OpenTDU2, not a retail option): the water plane
    // rises and falls by this many centimetres (0..20), so the waterline
    // washes up and down the beaches. 0 = the still retail sea; -tide / -notide
    // override it for a run (EdenOcean, docs/re/ocean_spec.md 11.4).
    int32_t WaterTide = 12;
    // Ray tracing (docs/re/ray_tracing.md). Both are read when the Vulkan device
    // is created and when a model is loaded, so they take effect on restart;
    // -nort / -rtworld override them so a headless A/B run needs no ini edit.
    int32_t RayTracing = 1;      // 0 = the device is created without ray tracing
    int32_t RayTracingWorld = 0; // 1 = build the acceleration structures for the world

    // Button prompts (OpenTDU2, not a retail option): 0 Auto (keyboard /
    // mouse art, or the pad's own family: PlayStation or Xbox), 1 Keyboard &
    // mouse, 2 Xbox, 3 PlayStation. FlashPlayer::SetPromptPlatform.
    int32_t ButtonPrompts = 0;

    // Unit system of the driving readouts (OpenTDU2, not a retail option):
    // 0 metric (km/h, km), 1 imperial (mph, mi). The retail game picks it from
    // the language profile [INF]; here it is a setting, `-imperial` / `-metric`
    // override it and the cheats panel flips it (game/units.h).
    int32_t Units = 0;

    // Screen format of the 2D layers at window aspects other than 16:9
    // (OpenTDU2, not a retail option). Retail stretches its Flash stage to
    // whatever the window is (the kRects table of FlashPlayer::stageRect);
    // the port's own 1280x720 canvas layers letterboxed instead, so at 16:10
    // or ultrawide the two HUD layers drifted apart. One mapping now:
    // 0 = Stretch (default, retail): every 2D layer fills the window, the art
    //     distorts on non 16:9 screens (mild at 16:10).
    // 1 = Fit: every 2D layer sits in a centered band of its own aspect
    //     (16:9 for the HUD), black bars around it, zero distortion.
    static constexpr int32_t kUiLayoutStretch = 0;
    static constexpr int32_t kUiLayoutFit = 1;
    int32_t UiLayout = kUiLayoutStretch;
    // Scale of the 3D camera's vertical FOV in % (50..150, 100 = the authored
    // values). Applied once, at the projection (EdenSceneView::setupScene), so
    // it composes with the retail narrow-aspect FOV law; the map screen pins
    // its own camera to 100 (its framing math assumes the 60 degree lens).
    // -fov <pct> overrides it for a run.
    int32_t FovScale = 100;
    // Screen ratio override (SystemPC.cpr SetScreenRatio picks the HUD
    // arrangements: 1 = the 16:9 layout, 2 = 4:3): 0 = auto from the window
    // aspect (>= 3:2 reads the 16:9 arrangement, narrower the 4:3 one),
    // 1 = force 16:9, 2 = force 4:3. Options > Image settings.
    int32_t ScreenRatioOverride = 0;
};

GameOptions &GetGameOptions();
// <userdata>/options.ini.
void LoadGameOptions();
void SaveGameOptions();
// Pushes the audio volumes to the mixer (GSAudio::setMixerVolume).
void ApplyAudioOptions();
// Window sizes the display supports (ascending, >= 800x600), always
// containing the current one.
std::vector<std::pair<int32_t, int32_t>> GetVideoResolutions();
// Refresh rate of the display the window is on (0 if unknown).
int32_t GetDisplayRefreshRate();
// WindowMode with -1 ("not chosen yet") resolved to the mode the window was
// created with: windowed, or borderless fullscreen desktop for a fullscreen
// SystemPC.cpr (the creation flag carries no display mode). The menu row's
// label and its first step start from this, so they never show a mode the
// window is not in.
int32_t EffectiveWindowMode();
// Pushes the video options to the window / swapchain / LOD config.
void ApplyVideoOptions();
// Applies a quality profile (1 High, 2 Medium, 3 Low, 4 Steam Deck,
// 5 Legion Go, 6 60 fps) to the advanced graphics rows; 0 (Custom) leaves
// them alone. -profile <name> applies one for a run (point 6 of
// docs/PERF_PLAN.md).
void ApplyQualityProfile( GameOptions &options, int32_t profile );
// The 'Gamma' constant of the post-process LUT pass (0.8 at the default).
float GetPostGamma();
// The 3D scene size relative to the window (RenderScale / 100).
float GetRenderScale();
// The screen ratio the HUD art and the Flash stage rect use: the override
// above, else auto from the window aspect (1 = 16:9 arrangement, 2 = 4:3).
int32_t GetEffectiveScreenRatio();
// The normalized backbuffer rect (x0, y0, width, height) the 1280x720 canvas
// maps onto for the UI layout option: the full window (Stretch) or a centered
// 16:9 band (Fit).
void GetUiCanvasRect( float backbufferWidth, float backbufferHeight,
                      float &outX0, float &outY0, float &outWidth, float &outHeight );
