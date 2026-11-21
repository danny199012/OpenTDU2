#include "shared.h"
#include "gm_scene_editor.h"

#include "core/logger.h"
#include "filesystem/gs_file.h"
#include "world/eden_world.h"

#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// The live drive test (docs/re/scene_editor.md 12): the editor exports what the game reads from small text / json
// files in the mods overlay (the roads file, stations.txt, the prop placements - all idempotent) and starts the
// game as a SECOND process, FREERIDE on the same island, the car dropped at the target. The editor keeps running,
// so the loop is: place -> drive test -> close the game -> adjust. (The banks built by export_bank / the spots
// are NOT exported here: they are explicit, heavier steps.)

std::string GMSceneEditor::driveTest( const std::string &id, bool bLaunch, const std::string &extraArgs ) {
    if ( gpFile == nullptr ) {
        return "no game folder";
    }
    // 1. The target: an object / station id, the selection, the active road, else the camera focus.
    Vector3f target = position + getForward() * 8.0f;
    float yaw = 0.0f;
    std::string what = "the camera focus";
    const std::string key = !id.empty() ? id : selectedId;
    bool bFound = false;
    if ( !key.empty() ) {
        if ( const SceneStation *pStation = doc.findStation( key ) ) {
            target = pStation->Position;
            yaw = pStation->Yaw == FLT_MAX ? 0.0f : pStation->Yaw;
            what = "station " + key;
            bFound = true;
        } else if ( const SceneObject *pObject = doc.findObject( key ) ) {
            target = pObject->Position;
            yaw = pObject->Yaw;
            what = "object " + key;
            bFound = true;
        }
    }
    if ( !bFound && !activeRoadId.empty() ) {
        if ( const SceneRoad *pRoad = doc.findRoad( activeRoadId ) ) {
            if ( pRoad->Points.size() >= 2 ) {
                target = pRoad->Points[ 0 ];
                const Vector3f d = pRoad->Points[ 1 ] - pRoad->Points[ 0 ];
                yaw = std::atan2( d.x, d.z ) * 180.0f / kPI;
                what = "the start of road " + activeRoadId;
            }
        }
    }
    float ground = target.y;
    if ( bWorldReady ) {
        view.getWorld().getSurfaceHeight( target.x, target.z, target.y + 50.0f, ground );
    }
    const float dropY = Max( ground, target.y ) + 2.0f; // the mode's fit probe lands the car on the surface

    // 2. Export what the game reads at start.
    std::string exported;
    if ( !doc.Stations.empty() ) {
        exportStationsToGame(); // the roads file + stations.txt + the canopy placements
        exported += "stations+roads, ";
    } else if ( !doc.Roads.empty() ) {
        exportRoadsFile();
        exported += "roads, ";
    }
    commitPropsToGame();
    exported += "props";

    // 3. The command line.
    // FREERIDE's -heading h faces the car along (-sin h, -cos h) [measured on the flat pad: 0 -> -Z, 90 -> -X, 180 -> +Z],
    // a scene yaw y faces (sin y, cos y): heading = yaw + 180.
    float heading = std::fmod( yaw + 180.0f, 360.0f );
    char at[96];
    snprintf( at, sizeof( at ), "%.1f,%.1f,%.1f", target.x, dropY, target.z );
    std::string args = "-gm FREERIDE -island " + island + " -skipcine -tp \"" + at + "\" -heading " + std::to_string( heading );
    if ( !doc.Stations.empty() ) {
        args += " -fuelmod"; // the pump screen is a mod
    }
    args += " -game \"" + gpFile->getGameRoot().string() + "\" -userdata \"" + gpFile->getUserDataFolder().string() + "\"";
    if ( !extraArgs.empty() ) {
        args += " " + extraArgs;
    }
    std::string status = "drive test at " + what + " (" + at + ", heading " + std::to_string( static_cast<int32_t>( heading ) ) + "); exported " + exported;
    if ( !bLaunch ) {
        return status + "; args: " + args;
    }
#ifdef _WIN32
    wchar_t exePath[ MAX_PATH ] = {};
    GetModuleFileNameW( nullptr, exePath, MAX_PATH );
    std::wstring commandLine = L"\"" + std::wstring( exePath ) + L"\" " + std::wstring( args.begin(), args.end() );
    STARTUPINFOW startup = {};
    startup.cb = sizeof( startup );
    PROCESS_INFORMATION process = {};
    if ( !CreateProcessW( nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process ) ) {
        return status + "; LAUNCH FAILED (error " + std::to_string( GetLastError() ) + ")";
    }
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    OTDU_LOG_INFO( "SceneEditor: %s; launched FREERIDE\n", status.c_str() );
    return status + "; FREERIDE started in a second window";
#else
    return status + "; launching is Windows-only here - run the game with: " + args;
#endif
}
