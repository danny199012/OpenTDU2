#include "shared.h"
#include "watermark_overlay.h"

#include "debug/gs_imgui.h"
#include "system/cmd_line.h"

#include "imgui.h"

#include "build_info.h"

#include <cstdio>
#include <string>

namespace {
std::string gTesterName;
bool gbSuppressed = false;
} // namespace

namespace WatermarkOverlay {

void Initialize() {
    if ( const char *pName = CmdLine::GetArgValue( "-tester" ) ) {
        gTesterName = pName;
    }
    gbSuppressed = CmdLine::HasArg( "-nowatermark" );
    // One line every run: the log and the build/branch are the ground truth a
    // bug report is filed against, whether or not the overlay is drawn.
    OTDU_LOG_ALWAYS( "Run: %s (%s built %s)%s\n", OTDU_BUILD_FULL_STRING,
                     gTesterName.empty() ? "no tester name" : gTesterName.c_str(), OTDU_BUILD_TIMESTAMP,
                     gbSuppressed ? " (watermark off)" : "" );
}

void Draw() {
    if ( gbSuppressed ) {
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
    if ( gTesterName.empty() ) {
        ImGui::TextUnformatted( OTDU_BUILD_FULL_STRING );
    } else {
        ImGui::Text( "%s · %s", gTesterName.c_str(), OTDU_BUILD_FULL_STRING );
    }
    ImGui::TextDisabled( "%s", OTDU_BUILD_TIMESTAMP );
    ImGui::End();
}

} // namespace WatermarkOverlay
