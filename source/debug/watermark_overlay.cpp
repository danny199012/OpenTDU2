#include "shared.h"
#include "watermark_overlay.h"

#include "debug/gs_imgui.h"
#include "system/cmd_line.h"

#include <SDL3/SDL_timer.h>
#include "imgui.h"

#include "build_info.h"

#include <cstdio>
#include <string>

namespace {
std::string gTesterName;
bool gbSuppressed = false;
bool gbShowFps = false;

// Frame-rate readout (-showfps): frames and the worst frame of the last half second.
float gFpsAccum = 0.0f, gFpsWorst = 0.0f, gFpsShown = 0.0f, gMsShown = 0.0f, gWorstShown = 0.0f;
int gFpsFrames = 0;
double gTimes[6] = {};
std::string gGpuSummary;
} // namespace

namespace WatermarkOverlay {

void SetGpuSummary( const char *pText ) {
    gGpuSummary = pText != nullptr ? pText : "";
}

void SetFrameTimes( double updateMs, double offscreenMs, double mainMs, double uiMs, double endMs, double frameMs ) {
    // "wait" = the frame time outside the timed sections (the frame start: waiting for the
    // GPU to finish the previous frame, acquiring the swapchain image).
    gTimes[5] = frameMs - ( updateMs + offscreenMs + mainMs + uiMs + endMs );
    gTimes[0] = updateMs;
    gTimes[1] = offscreenMs;
    gTimes[2] = mainMs;
    gTimes[3] = uiMs;
    gTimes[4] = endMs;
}

void Initialize() {
    if ( const char *pName = CmdLine::GetArgValue( "-tester" ) ) {
        gTesterName = pName;
    }
    gbSuppressed = CmdLine::HasArg( "-nowatermark" );
    gbShowFps = CmdLine::HasArg( "-showfps" );
    // One line every run: the log and the build/branch are the ground truth a
    // bug report is filed against, whether or not the overlay is drawn.
    OTDU_LOG_ALWAYS( "Run: %s (%s built %s)%s\n", OTDU_BUILD_FULL_STRING,
                     gTesterName.empty() ? "no tester name" : gTesterName.c_str(), OTDU_BUILD_TIMESTAMP,
                     gbSuppressed ? " (watermark off)" : "" );
}

void Draw() {
    if ( gbShowFps ) {
        // Real time between draws: the engine clamps its frame delta (0.1 s), which would
        // show a slow frame as a flat 10 FPS.
        static uint64_t lastCounter = 0;
        const uint64_t counter = SDL_GetPerformanceCounter();
        const float dt = lastCounter == 0 ? 0.016f
                                          : static_cast<float>( static_cast<double>( counter - lastCounter ) /
                                                                static_cast<double>( SDL_GetPerformanceFrequency() ) );
        lastCounter = counter;
        gFpsAccum += dt;
        gFpsFrames++;
        gFpsWorst = dt > gFpsWorst ? dt : gFpsWorst;
        if ( gFpsAccum >= 0.5f ) {
            gFpsShown = static_cast<float>( gFpsFrames ) / gFpsAccum;
            gMsShown = gFpsAccum / static_cast<float>( gFpsFrames ) * 1000.0f;
            gWorstShown = gFpsWorst * 1000.0f;
            gFpsAccum = 0.0f;
            gFpsWorst = 0.0f;
            gFpsFrames = 0;
        }
    }
    if ( gbSuppressed && !gbShowFps ) {
        return;
    }
    // Top-right: the coords readout (-coords) owns the top-left corner. The
    // middle dot is Latin-1 0xB7, inside ImGui's default glyph range.
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 offset = DpiScaled( 12.0f, 12.0f );
    ImGui::SetNextWindowPos( ImVec2( viewport->WorkSize.x - offset.x, offset.y ),
                             ImGuiCond_Always, ImVec2( 1.0f, 0.0f ) );
    ImGui::SetNextWindowBgAlpha( 0.35f );
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if ( !ImGui::Begin( "##otdu2_watermark", nullptr, flags ) ) {
        ImGui::End();
        return;
    }
    if ( gbShowFps ) {
        ImGui::Text( "FPS %.0f  ·  %.1f ms  ·  worst %.0f ms", gFpsShown, gMsShown, gWorstShown );
        ImGui::TextDisabled( "update %.0f · offscreen %.0f · main %.0f · ui %.0f · present %.0f · wait %.0f ms", gTimes[0], gTimes[1],
                             gTimes[2], gTimes[3], gTimes[4], gTimes[5] );
        if ( !gGpuSummary.empty() ) {
            ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + DpiScaled( 640.0f, 0.0f ).x );
            ImGui::TextDisabled( "%s", gGpuSummary.c_str() );
            ImGui::PopTextWrapPos();
        }
    }
    if ( !gbSuppressed ) {
        if ( gTesterName.empty() ) {
            ImGui::TextUnformatted( OTDU_BUILD_FULL_STRING );
        } else {
            ImGui::Text( "%s · %s", gTesterName.c_str(), OTDU_BUILD_FULL_STRING );
        }
        ImGui::TextDisabled( "%s", OTDU_BUILD_TIMESTAMP );
    }
    ImGui::End();
}

} // namespace WatermarkOverlay
