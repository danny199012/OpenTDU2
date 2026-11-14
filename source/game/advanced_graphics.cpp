#include "shared.h"
#include "advanced_graphics.h"

#include "debug/gs_imgui.h"
#include "game/game_options.h"
#include "game/units.h"
#include "input/gs_input.h"
#include "render/gs_render.h"

#include <SDL3/SDL.h>

#include "imgui.h"

namespace {
struct Row {
    const char *pLabel;
    int32_t GameOptions::*pValue;
    int32_t Min;
    int32_t Max;
    int32_t Step;
    std::string ( *pFormat )( int32_t );
    const char *pHelp;
};

std::string OnOff( int32_t value ) { return value != 0 ? "On" : "Off"; }
std::string Percent( int32_t value ) { return std::to_string( value ) + " %"; }
std::string UpscalerName( int32_t value ) {
    return value == 5 ? "NVIDIA DLSS (temporal)" : value == 4 ? "Intel XeSS (temporal)" : value == 3 ? "AMD FSR 3 (temporal)" : value == 2 ? "AMD FSR 1.0" : value == 1 ? "Bicubic (Catmull-Rom)" : "Bilinear";
}
std::string SharpnessName( int32_t value ) { return value == 0 ? "Off" : std::to_string( value ); }
std::string FpsName( int32_t value ) { return value == 0 ? "Unlimited" : std::to_string( value ) + " fps"; }
std::string WaterClarityName( int32_t value ) {
    if ( value == 0 ) {
        return "Opaque";
    }
    char text[32];
    snprintf( text, sizeof( text ), "%d %% see-through", value * 5 / 2 ); // 20 -> 50 %
    return text;
}
std::string ShoreSwellName( int32_t value ) {
    if ( value == 0 ) {
        return "Still";
    }
    char text[32];
    snprintf( text, sizeof( text ), "%d cm", value );
    return text;
}
std::string PromptsName( int32_t value ) {
    static const char *const kNames[] = { "Auto", "Keyboard & mouse", "Xbox", "PlayStation" };
    return kNames[Clamp( value, 0, 3 )];
}
std::string ProfileName( int32_t value ) {
    static const char *const kNames[] = { "Custom", "High", "Medium", "Low", "Steam Deck", "Legion Go", "60 fps" };
    return kNames[Clamp( value, 0, 6 )];
}
std::string UnitsName( int32_t value ) {
    return value == 1 ? "mph / miles (imperial)" : "km/h / km (metric)";
}
std::string HudQualityName( int32_t value ) {
    return value <= 1 ? "Simple (minimap + gauge)" : "Full (HUD + gauges)";
}
std::string ScreenRatioName( int32_t value ) {
    static const char *const kNames[] = { "Auto (window aspect)", "16:9", "4:3" };
    return kNames[Clamp( value, 0, 2 )];
}
std::string UiLayoutName( int32_t value ) {
    return value == GameOptions::kUiLayoutFit ? "Fit (16:9 band, bars)" : "Stretch (fill, retail)";
}
std::string FovScaleName( int32_t value ) { return std::to_string( value ) + " %"; }
std::string ReflectionsName( int32_t value ) {
    if ( value <= 0 ) {
        return "Static (rendered once)";
    }
    if ( value == 1 ) {
        return "Every frame";
    }
    return "Every " + std::to_string( value ) + " frames";
}

const Row kRows[] = {
    { "Quality profile", &GameOptions::QualityProfile, 0, 6, 1, ProfileName,
      "One preset for the whole engine. Steam Deck and Legion Go also set the window to their panel size; "
      "60 fps keeps the full resolution but drops the sun shadows and the moving water reflection. High is "
      "the retail look. Editing any row below makes it Custom." },
    // The unit system of every driving readout (game/units.h). The retail game
    // picks it from the language profile and has no readable menu row for it
    // [INF], so the port puts it here — the native page the pause menu's
    // Options screen opens with Y / Triangle (Video or Controls page) or F2.
    { "Speed units", &GameOptions::Units, 0, 1, 1, UnitsName,
      "The unit system of the driving readouts: the speedometer digits, the odometer, the GPS / detector "
      "distances and the fuel range. Metric is km/h and km, Imperial is mph and miles. The car's own gauge "
      "needle table follows the same pick. Saved to options.ini; -imperial / -metric override it for a run." },
    { "Screen ratio", &GameOptions::ScreenRatioOverride, 0, 2, 1, ScreenRatioName,
      "Which HUD arrangement the Flash movies use (the retail SystemPC.cpr choice). Auto reads the 16:9 "
      "layout on 16:10 / wide windows and the 4:3 one on narrower screens; the stage rect and the cinematic "
      "letterbox follow the same pick." },
    { "UI layout", &GameOptions::UiLayout, 0, 1, 1, UiLayoutName,
      "Where the 2D layers (HUD, menus) sit when the window is not 16:9: Stretch fills the window like "
      "retail (the art distorts a little), Fit centers a 16:9 band with black bars (zero distortion)." },
    { "FOV scale", &GameOptions::FovScale, 50, 150, 5, FovScaleName,
      "Scales every 3D camera's vertical field of view (100 % = the authored values, 150 % sees 50 % "
      "wider). The map screen keeps its fixed lens. -fov <pct> overrides it for a run." },
    { "Button prompts", &GameOptions::ButtonPrompts, 0, 3, 1, PromptsName,
      "The button icons of the menus and the HUD. Auto follows what you last used: the keyboard / mouse "
      "icons, or your pad's own (PlayStation or Xbox)." },
    { "Render scale", &GameOptions::RenderScale, 50, 200, 5, Percent,
      "Resolution the 3D scene renders at, relative to the window. Below 100 is faster, above 100 is supersampling." },
    { "Upscaling filter", &GameOptions::Upscaler, 0, 5, 1, UpscalerName,
      "How the scene is brought to the window size when the render scale is not 100 %. AMD FSR 1.0 is "
      "edge-adaptive (works on any GPU) and is best at a render scale of 67-77 %. FSR 3 is temporal "
      "(needs AMD's amd_fidelityfx_*_vk.dll next to the game, see docs/re/advanced_graphics.md)." },
    { "Sharpening", &GameOptions::Sharpness, 0, 10, 1, SharpnessName,
      "Sharpening applied after the upscale (AMD RCAS when the filter is FSR 1.0)." },
    { "Edge smoothing (FXAA-like)", &GameOptions::EdgeSmoothing, 0, 1, 1, OnOff,
      "Cheap anti-aliasing pass at the render resolution." },
    { "Color grading and bloom", &GameOptions::ColorGrading, 0, 1, 1, OnOff,
      "The original 'photoshop' pass: color balance, soft light, bloom, gamma." },
    { "Atmospheric haze", &GameOptions::Haze, 0, 1, 1, OnOff, "Distance haze and fog over the scene." },
    { "Sun flare", &GameOptions::SunFlare, 0, 1, 1, OnOff, "Sun halo and lens flares." },
    { "Sun shadows", &GameOptions::Shadows, 0, 1, 1, OnOff, "Real time shadows of the sun." },
    { "Shadow distance", &GameOptions::ShadowDistance, 50, 150, 10, Percent,
      "How far from the camera the sun's shadow casters are drawn, relative to the game's own range "
      "(FREERIDE casts up to 736 m). Lower is faster; the shadows simply end nearer." },
    { "HUD detail", &GameOptions::HudQuality, 1, 2, 1, HudQualityName,
      "Full draws the HUD and GAUGES Flash movies; Simple keeps the minimap and the built-in gauge. "
      "The minimap itself is cached now, so Full is cheap on a desktop." },
    { "Water reflections", &GameOptions::OceanReflections, 0, 4, 1, ReflectionsName,
      "How often the sea's mirror pass re-renders the world. It re-submits every mesh, so the handheld "
      "profiles render it once (Static) or every other frame." },
    { "Draw distance", &GameOptions::DrawDistance, 50, 150, 5, Percent,
      "Distance of the streamed vegetation / props relative to the game data (100 % = retail). Lower is "
      "faster and pops objects in sooner." },
    { "Water clarity", &GameOptions::WaterClarity, 0, 20, 1, WaterClarityName,
      "How much of the sea bed shows through the water. Opaque is the retail water; the sea life of "
      "the world will be visible through it." },
    { "Shore swell", &GameOptions::WaterTide, 0, 20, 1, ShoreSwellName,
      "How far the sea rises and falls at the beach (the waterline washes up and down the sand). "
      "Still is the retail water plane; 12 cm is the default." },
    { "Frame rate limit", &GameOptions::FpsLimit, 0, 240, 30, FpsName,
      "Caps the frame rate (0 = unlimited; VSync still applies)." },
    // Ray tracing (docs/re/ray_tracing.md). The device is created once and the
    // structures are built as models load, so these are restart settings.
    { "Ray tracing", &GameOptions::RayTracing, 0, 1, 1, OnOff,
      "Ray traced acceleration structures (VK_KHR_ray_query). Restart to apply." },
    { "Ray traced world", &GameOptions::RayTracingWorld, 0, 1, 1, OnOff,
      "Put the world's meshes into acceleration structures: memory and load time, nothing is lit "
      "by them yet. Restart to apply." },
};
constexpr int32_t kNumRows = static_cast<int32_t>( sizeof( kRows ) / sizeof( kRows[0] ) );
// The preset row: changing it applies the preset, any other row clears it.
constexpr int32_t kProfileRow = 0;
// The unit system row (game/units.h): it is not a graphics setting, so it does
// not clear the quality profile, and the change goes through SetImperialUnits
// (which logs it and saves options.ini at once).
constexpr int32_t kUnitsRow = 1;
// The display / camera rows (screen ratio, UI layout, FOV scale): presentation
// choices, not graphics quality - they leave the quality profile alone.
constexpr int32_t kScreenRatioRow = 2;
constexpr int32_t kUiLayoutRow = 3;
constexpr int32_t kFovScaleRow = 4;
// One extra selectable row after the settings: the self test is an action, not
// a value, so it is not part of the table above.
constexpr int32_t kSelfTestRow = kNumRows;
constexpr int32_t kNumSelectableRows = kNumRows + 1;
} // namespace

AdvancedGraphics &GetAdvancedGraphics() {
    static AdvancedGraphics page;
    return page;
}

void AdvancedGraphics::open() {
    bOpen = true;
    selected = 0;
}

void AdvancedGraphics::close() {
    if ( bOpen ) {
        bOpen = false;
        SaveGameOptions();
    }
}

void AdvancedGraphics::runSelfTest() {
    if ( gpRender == nullptr ) {
        selfTestResult = "no renderer";
        return;
    }
    RayTracing &rayTracing = gpRender->getRayTracing();
    if ( !rayTracing.isReady() ) {
        selfTestResult = "unavailable";
        return;
    }
    selfTestResult = rayTracing.runSelfTest() ? "PASSED" : "FAILED";
    OTDU_LOG_INFO( "Advanced graphics: ray tracing self test %s\n", selfTestResult.c_str() );
}

void AdvancedGraphics::drawUI() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if ( bHint && !bOpen ) {
        ImGui::SetNextWindowPos( ImVec2( display.x - 16.0f, display.y - 16.0f ), ImGuiCond_Always, ImVec2( 1.0f, 1.0f ) );
        ImGui::SetNextWindowBgAlpha( 0.6f );
        ImGui::Begin( "##advhint", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize );
        ImGui::Text( "Y / Triangle: Advanced settings (speed units, graphics)" );
        ImGui::End();
    }
    if ( gpInput != nullptr && gpInput->wasKeyPressed( SDL_SCANCODE_F2 ) ) {
        // F2 toggles it anywhere (in the world the game keeps taking the
        // keys too: the pause menu entry is the clean one).
        if ( bOpen ) {
            close();
        } else {
            open();
        }
        return;
    }
    if ( !bOpen ) {
        return;
    }
    GameOptions &options = GetGameOptions();
    if ( gpInput != nullptr ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            selected = ( selected + kNumSelectableRows - 1 ) % kNumSelectableRows;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            selected = ( selected + 1 ) % kNumSelectableRows;
        }
        const int32_t step = ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ? 1 : 0 ) -
                             ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ? 1 : 0 );
        if ( step != 0 ) {
            if ( selected == kSelfTestRow ) {
                // An action, not a value: Right runs it (Left does nothing).
                if ( step > 0 ) {
                    runSelfTest();
                }
            } else {
                const Row &row = kRows[selected];
                int32_t &value = options.*row.pValue;
                const int32_t newValue = Clamp( value + step * row.Step, row.Min, row.Max );
                if ( selected == kUnitsRow ) {
                    // Unit system: the HUD, the gauge and the distance lines
                    // read it every frame, so it applies live. Save on close
                    // like every other row of this page (close() calls
                    // SaveGameOptions): saving here would also persist retail
                    // page rows the player has not confirmed yet, and Cancel on
                    // that page would then disagree with the file.
                    options.Units = newValue;
                    OTDU_LOG_INFO( "Advanced settings: units %s (saved on close)\n",
                                   newValue == 1 ? "imperial (mph / mi)" : "metric (km/h / km)" );
                } else if ( selected == kProfileRow ) {
                    // Picking a preset overwrites the rows below it (window
                    // size and fps cap included) and applies the video side
                    // immediately.
                    value = newValue;
                    ApplyQualityProfile( options, value );
                    ApplyVideoOptions();
                } else if ( selected == kScreenRatioRow || selected == kUiLayoutRow ||
                            selected == kFovScaleRow ) {
                    // Presentation choices: they apply live (the stage rect
                    // and the canvas mapping re-read the option per frame)
                    // and keep the quality profile.
                    value = newValue;
                } else {
                    value = newValue;
                    // Any hand edit makes the profile Custom.
                    options.QualityProfile = 0;
                }
            }
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuValidate ) ||
             gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            close();
            return;
        }
    }

    ImGui::SetNextWindowPos( ImVec2( display.x * 0.5f, display.y * 0.5f ), ImGuiCond_Always, ImVec2( 0.5f, 0.5f ) );
    ImGui::SetNextWindowSize(
        ImVec2( Min( display.x - DpiScale() * 40.0f, DpiScale() * 720.0f ), 0.0f ) );
    ImGui::SetNextWindowBgAlpha( 0.96f );
    ImGui::Begin( "Advanced settings", nullptr,
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav );
    ImGui::SetWindowFontScale( 1.3f );
    for ( int32_t i = 0; i < kNumRows; i++ ) {
        const Row &row = kRows[i];
        const int32_t value = options.*row.pValue;
        const std::string text = row.pFormat( value );
        const bool bSelected = i == selected;
        ImGui::PushStyleColor( ImGuiCol_Text, bSelected ? ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ) : ImVec4( 0.65f, 0.65f, 0.7f, 1.0f ) );
        if ( bSelected ) {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( pos.x - 6.0f, pos.y - 2.0f ),
                                                       ImVec2( pos.x + ImGui::GetContentRegionAvail().x + 6.0f,
                                                               pos.y + ImGui::GetTextLineHeight() + 2.0f ),
                                                       IM_COL32( 60, 70, 90, 200 ) );
        }
        ImGui::TextUnformatted( row.pLabel );
        ImGui::SameLine( ImGui::GetWindowWidth() * 0.58f );
        ImGui::Text( "%s%s%s", bSelected && value > row.Min ? "< " : "  ", text.c_str(),
                     bSelected && value < row.Max ? " >" : "" );
        ImGui::PopStyleColor();
    }
    // The self test row: an action, so it shows its last verdict, not a value.
    {
        const bool bSelected = selected == kSelfTestRow;
        ImGui::PushStyleColor( ImGuiCol_Text, bSelected ? ImVec4( 1.0f, 1.0f, 1.0f, 1.0f )
                                                        : ImVec4( 0.65f, 0.65f, 0.7f, 1.0f ) );
        if ( bSelected ) {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( pos.x - 6.0f, pos.y - 2.0f ),
                                                       ImVec2( pos.x + ImGui::GetContentRegionAvail().x + 6.0f,
                                                               pos.y + ImGui::GetTextLineHeight() + 2.0f ),
                                                       IM_COL32( 60, 70, 90, 200 ) );
        }
        ImGui::TextUnformatted( "Run ray tracing self test" );
        ImGui::SameLine( ImGui::GetWindowWidth() * 0.58f );
        if ( bSelected ) {
            ImGui::TextUnformatted( "> run" );
        } else {
            ImGui::TextUnformatted( selfTestResult.empty() ? "not run yet" : selfTestResult.c_str() );
        }
        ImGui::PopStyleColor();
    }
    ImGui::Separator();
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.75f, 0.8f, 0.9f, 1.0f ) );
    ImGui::TextWrapped( "%s", selected == kSelfTestRow
                                  ? "Builds one triangle whose answer is known, traces three rays at it (hit, miss, "
                                    "rotated) and reports the verdict here and in the log."
                                  : kRows[selected].pHelp );
    ImGui::PopStyleColor();
    if ( options.RenderScale != 100 && gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        ImGui::Text( "Scene: %u x %u  ->  window %u x %u",
                     static_cast<uint32_t>( device.getBackbufferWidth() * GetRenderScale() + 0.5f ),
                     static_cast<uint32_t>( device.getBackbufferHeight() * GetRenderScale() + 0.5f ),
                     device.getBackbufferWidth(), device.getBackbufferHeight() );
    }
    if ( gpRender != nullptr ) {
        // The device is created once and the structures are built as models
        // load, so a changed setting only shows up after a restart: say what is
        // actually live instead of pretending it applied.
        const RenderDevice &device = gpRender->getDevice();
        const bool bActive = device.supportsRayTracing();
        const bool bWorldActive = gpRender->isRayTracingWorldActive();
        ImGui::TextDisabled( "Ray tracing: %s",
                             bActive ? "active (VK_KHR_ray_query)" : "not active on this device" );
        ImGui::TextDisabled( "World structures: %s", bWorldActive ? "on" : "off" );
        if ( ( options.RayTracing != 0 ) != device.wantsRayTracing() ||
             ( options.RayTracingWorld != 0 ) != bWorldActive ) {
            ImGui::TextDisabled( "Saved setting not in effect yet: restart to apply" );
        }
    }
    ImGui::TextDisabled( "Up / Down: row   Left / Right: change   OK / Back: close and save" );
    ImGui::End();
}
