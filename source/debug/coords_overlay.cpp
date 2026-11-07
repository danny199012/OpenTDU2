#include "shared.h"
#include "coords_overlay.h"

#include "core/math.h"
#include "debug/gs_imgui.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace {
struct Pose {
    float Position[3] = { 0.0f, 0.0f, 0.0f };
    float Direction[3] = { 0.0f, 0.0f, 1.0f };
    bool bValid = false;
};

Pose gPose;
bool gbVisible = false;
int gMarkCount = 0;
char gLastMark[160] = "";

float HeadingDegrees( const Pose &pose ) {
    const float heading = std::atan2( pose.Direction[0], pose.Direction[2] ) * 57.29578f;
    return heading < 0.0f ? heading + 360.0f : heading;
}
} // namespace

namespace CoordsOverlay {

void SetCamera( const Vector3f &position, const Vector3f &direction ) {
    gPose.Position[0] = position.x, gPose.Position[1] = position.y, gPose.Position[2] = position.z;
    gPose.Direction[0] = direction.x, gPose.Direction[1] = direction.y, gPose.Direction[2] = direction.z;
    gPose.bValid = true;
}

void Toggle() { gbVisible = !gbVisible; }

void SetVisible( bool bVisible ) { gbVisible = bVisible; }

void WriteMark() {
    if ( !gPose.bValid ) {
        return;
    }
    const float heading = HeadingDegrees( gPose );
    ++gMarkCount;
    std::snprintf( gLastMark, sizeof( gLastMark ), "mark #%d  %.1f, %.1f, %.1f  hdg %.0f", gMarkCount,
                   gPose.Position[0], gPose.Position[1], gPose.Position[2], heading );
    char line[256];
    const std::time_t now = std::time( nullptr );
    char stamp[32];
    std::strftime( stamp, sizeof( stamp ), "%H:%M:%S", std::localtime( &now ) );
    std::snprintf( line, sizeof( line ), "%s #%d pos=(%.2f, %.2f, %.2f) heading=%.1fdeg fwd=(%.3f, %.3f, %.3f)", stamp,
                   gMarkCount, gPose.Position[0], gPose.Position[1], gPose.Position[2], heading, gPose.Direction[0],
                   gPose.Direction[1], gPose.Direction[2] );
    OTDU_LOG_INFO( "[mark] %s\n", line );
    if ( FILE *pFile = std::fopen( "_marks.txt", "a" ) ) {
        std::fprintf( pFile, "%s\n", line );
        std::fclose( pFile );
    }
}

void Draw() {
    if ( !gbVisible ) {
        return;
    }
    ImGui::SetNextWindowPos( DpiScaled( 12.0f, 12.0f ), ImGuiCond_Always );
    ImGui::SetNextWindowBgAlpha( 0.45f );
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if ( !ImGui::Begin( "##otdu2_coords", nullptr, flags ) ) {
        ImGui::End();
        return;
    }
    if ( gPose.bValid ) {
        ImGui::Text( "X %9.2f", gPose.Position[0] );
        ImGui::Text( "Y %9.2f", gPose.Position[1] );
        ImGui::Text( "Z %9.2f", gPose.Position[2] );
        ImGui::Text( "HDG %6.1f", HeadingDegrees( gPose ) );
    } else {
        ImGui::TextUnformatted( "camera: not available yet" );
    }
    ImGui::TextDisabled( "F6 = mark" );
    if ( gLastMark[0] != '\0' ) {
        ImGui::TextDisabled( "%s", gLastMark );
    }
    ImGui::End();
}

} // namespace CoordsOverlay
