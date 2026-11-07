#include "shared.h"
#include "gm_world_viewer.h"

#include "debug/gs_imgui.h"
#include "filesystem/gs_file.h"
#include "game/freeride/chase_camera.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"

#include <SDL3/SDL.h>

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kMaxPitch = 89.0f * kPi / 180.0f;
// Spot start: camera above the spot (developer tool values) [INF].
constexpr float kSpotHeight = 25.0f;
constexpr float kGroundClearance = 1.5f;
constexpr float kNoSectorHeight = 400.0f; // no sector bank there: high enough to see the island level heightfield
// OpenTDU CameraMouse sensitivity steps per wheel notch (DAT_00f4174c / DAT_00f417e4).
constexpr float kWheelUp = 1.1f;
constexpr float kWheelDown = 0.9090909f;

const char *IslandLabel( const char *pIsland ) {
    if ( strcmp( pIsland, "ibiza" ) == 0 ) {
        return "Ibiza";
    }
    if ( strcmp( pIsland, "hawai" ) == 0 ) {
        return "Hawai";
    }
    return "Casino";
}

int32_t SectorKey( const Vector3f &p ) {
    const EdenWorld::SectorId id = EdenWorld::getSectorAt( p );
    const int32_t column = id.AreaX * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + id.X;
    const int32_t row = id.AreaY * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + id.Y;
    return row * 1024 + column;
}

bool IsKnownIsland( const std::string &name ) {
    for ( const char *pIsland : GMWorldViewer::kIslands ) {
        if ( name == pIsland ) {
            return true;
        }
    }
    return false;
}
} // namespace

GMWorldViewer::GMWorldViewer() : GameMode( eGameMode::GM_WorldViewer ) {}

GMWorldViewer::~GMWorldViewer() { exit(); }

bool GMWorldViewer::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    sectorRadius = Clamp( CmdLine::GetArgInt( "-sectorradius", 1 ), 0, 4 );
    bExteriors = !CmdLine::HasArg( "-noexteriors" );
    if ( const char *pSpeed = CmdLine::GetArgValue( "-flyspeed" ) ) {
        baseSpeed = Clamp( static_cast<float>( atof( pSpeed ) ), 1.0f, 5000.0f );
    }
    const char *pIsland = CmdLine::GetArgValue( "-island" );
    std::string name = ToLower( std::string( pIsland != nullptr ? pIsland : "ibiza" ) );
    if ( name == "hawaii" || name == "oahu" ) {
        name = "hawai";
    }
    if ( !IsKnownIsland( name ) ) {
        OTDU_LOG_WARN( "WORLD_VIEWER: unknown island '%s' (ibiza, hawai, casino), using ibiza\n", name.c_str() );
        name = "ibiza";
    }
    // The command line camera applies to the first island only.
    loadIsland( name );
    if ( const char *pPosition = CmdLine::GetArgValue( "-campos" ) ) {
        Vector3f p;
        if ( sscanf( pPosition, "%f,%f,%f", &p.x, &p.y, &p.z ) == 3 ) {
            position = p;
        }
    }
    if ( const char *pYaw = CmdLine::GetArgValue( "-camyaw" ) ) {
        yaw = static_cast<float>( atof( pYaw ) ) * kPi / 180.0f;
    }
    if ( const char *pPitch = CmdLine::GetArgValue( "-campitch" ) ) {
        pitch = Clamp( static_cast<float>( atof( pPitch ) ) * kPi / 180.0f, -kMaxPitch, kMaxPitch );
    }
    streamWorld( true );
    // The mode stays active when the island fails to load: the panel lets
    // the user pick another one.
    return true;
}

void GMWorldViewer::loadSpots() {
    spots.clear();
    // spots.bin (game/spots/spot_exteriors.h): the record fields plus the
    // exterior bank path when the file is there.
    spots = LoadSpotList( island );
    // Sector banks present around the spot (3 x 3 sectors): the start spot is
    // the best covered one (partial game data).
    for ( SpotRecord &spot : spots ) {
        const EdenWorld::SectorId center = EdenWorld::getSectorAt( spot.Position );
        const int32_t centerColumn = center.AreaX * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + center.X;
        const int32_t centerRow = center.AreaY * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + center.Y;
        for ( int32_t row = centerRow - 1; row <= centerRow + 1; row++ ) {
            for ( int32_t column = centerColumn - 1; column <= centerColumn + 1; column++ ) {
                if ( row < 0 || column < 0 ) {
                    continue;
                }
                EdenWorld::SectorId id;
                id.AreaX = column / static_cast<int32_t>( EdenWorld::kSectorsPerArea );
                id.AreaY = row / static_cast<int32_t>( EdenWorld::kSectorsPerArea );
                id.X = column % static_cast<int32_t>( EdenWorld::kSectorsPerArea );
                id.Y = row % static_cast<int32_t>( EdenWorld::kSectorsPerArea );
                if ( gpFile->fileExists( EdenWorld::GetSectorBankPath( island.c_str(), id ).c_str() ) ) {
                    spot.NumSectors++;
                    spot.bHasSector |= id == center;
                }
            }
        }
    }
    OTDU_LOG_INFO( "WORLD_VIEWER: %s: %zu spots\n", island.c_str(), spots.size() );
    // The same list drives the streamed exteriors.
    exteriors.setSpots( spots );
}

bool GMWorldViewer::loadIsland( const std::string &name ) {
    unloadIsland();
    island = name;
    statusText.clear();
    loadSpots();

    // Start spot: -spot <bank name>, else the spot with the most sector banks
    // present around it (partial game data), else the first spot.
    selectedSpot = -1;
    const char *pSpotArg = CmdLine::GetArgValue( "-spot" );
    for ( size_t i = 0; i < spots.size() && pSpotArg != nullptr; i++ ) {
        if ( StringCmpI( spots[i].Bank.c_str(), pSpotArg ) == 0 ) {
            selectedSpot = static_cast<int32_t>( i );
            break;
        }
    }
    if ( selectedSpot < 0 ) {
        uint32_t bestSectors = 0;
        for ( size_t i = 0; i < spots.size(); i++ ) {
            if ( spots[i].bHasSector && spots[i].NumSectors > bestSectors ) {
                bestSectors = spots[i].NumSectors;
                selectedSpot = static_cast<int32_t>( i );
            }
        }
    }
    if ( selectedSpot < 0 && !spots.empty() ) {
        selectedSpot = 0;
    }
    if ( selectedSpot >= 0 ) {
        const SpotRecord &spot = spots[selectedSpot];
        teleport( spot.Position, spot.Heading, spot.bHasSector ? kSpotHeight : kNoSectorHeight );
        OTDU_LOG_INFO( "WORLD_VIEWER: start spot %s (%.1f, %.1f, %.1f)%s\n", spot.Bank.c_str(), spot.Position.x,
                       spot.Position.y, spot.Position.z, spot.bHasSector ? "" : " (no sector bank there)" );
    } else {
        teleport( Vector3f( 3072.0f, 0.0f, 3072.0f ), 0.0f, kNoSectorHeight );
    }

    if ( !view.initialize( island.c_str(), position, sectorRadius ) ) {
        statusText = "island '" + island + "' could not be loaded (Euro/Bnk/islands/" + island + "/level)";
        OTDU_LOG_ERROR( "WORLD_VIEWER: %s\n", statusText.c_str() );
        view.destroy();
        return false;
    }
    view.getWorld().setIslandTerrainEnabled( true );
    // Mod props (docs/re/modding.md): our own meshes from the mods overlay.
    if ( EdenRenderer *pEdenRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr ) {
        modProps.load( *pEdenRenderer, island );
    }
    view.setDrawCallback( [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
        if ( pass == EdenSceneView::ePass::ProjectorActors ) {
            return;
        }
        exteriors.draw( renderer );
        modProps.draw( renderer );
    } );
    // Island weather: game_weathers.xmb time of day presets, "14" by default
    // like FREERIDE [INF: the clock is encrypted].
    gameWeathers = WeatherLibrary();
    gameWeathers.load( ( gpFile->getBankFolder() + "islands/" + island + "/fx/game_weathers.xmb" ).c_str() );
    const char *pWeatherName = CmdLine::GetArgValue( "-weather" );
    pWeather = gameWeathers.find( pWeatherName != nullptr ? pWeatherName : "14" );
    if ( pWeather == nullptr && !gameWeathers.getPresets().empty() ) {
        pWeather = &gameWeathers.getPresets().front();
    }
    if ( pWeather == nullptr ) {
        // No game weather: the first cinematic weather of the island.
        pWeather = !view.getWeathers().getPresets().empty() ? &view.getWeathers().getPresets().front() : nullptr;
    }
    selectedWeather = 0;
    for ( size_t i = 0; i < gameWeathers.getPresets().size(); i++ ) {
        if ( &gameWeathers.getPresets()[i] == pWeather ) {
            selectedWeather = static_cast<int32_t>( i );
        }
    }
    view.setWeather( pWeather );
    view.setShadowConfig( EdenShadows::FindConfig( "FREERIDE" ) );
    lastSectorKey = SectorKey( position );
    bWorldReady = true;
    const EdenWorld &world = view.getWorld();
    OTDU_LOG_INFO( "WORLD_VIEWER: %s ready: %d x %d areas grid, %zu areas, %zu sectors, island heightfield %s, weather '%s'\n",
                   island.c_str(), world.getAreaColumns(), world.getAreaRows(), world.getNumAreas(), world.getNumSectors(),
                   world.hasIslandTerrain() ? "yes" : "no", pWeather != nullptr ? pWeather->Name.c_str() : "(none)" );
    return true;
}

void GMWorldViewer::unloadIsland() {
    if ( gpRender != nullptr ) {
        gpRender->getDevice().waitIdle();
    }
    exteriors.unload();
    modProps.unload();
    view.destroy();
    pWeather = nullptr;
    bWorldReady = false;
    lastSectorKey = INT32_MIN;
}

void GMWorldViewer::teleport( const Vector3f &target, float heading, float height ) {
    position = target + Vector3f( 0.0f, height, 0.0f );
    yaw = heading;
    pitch = -15.0f * kPi / 180.0f;
}

Vector3f GMWorldViewer::getForward() const {
    return Vector3f( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
}

void GMWorldViewer::setRelativeMouse( bool bEnable ) {
    if ( bEnable == bRelativeMouse || pGameInstance == nullptr || pGameInstance->getWindow() == nullptr ) {
        return;
    }
    bRelativeMouse = bEnable;
    SDL_SetWindowRelativeMouseMode( pGameInstance->getWindow(), bEnable );
}

void GMWorldViewer::updateCamera( float deltaTime ) {
    // Look: a mouse button held outside the panel (OpenTDU CameraMouse
    // "only while a mouse button is held"), or the Tab captured mode.
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_TAB ) ) {
        bCaptured = !bCaptured;
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F2 ) ) {
        bShowPanel = !bShowPanel;
    }
    // F5 / F6 / F7: Ibiza / Hawai / Casino (same as the panel buttons).
    for ( uint32_t i = 0; i < 3; i++ ) {
        if ( gpInput->wasKeyPressed( SDL_SCANCODE_F5 + i ) && island != kIslands[i] ) {
            pendingIsland = kIslands[i];
        }
    }
    const bool bButton = gpInput->isMouseButtonDown( SDL_BUTTON_RIGHT ) || gpInput->isMouseButtonDown( SDL_BUTTON_LEFT );
    if ( !bLooking && bButton && ( gpImGui == nullptr || !gpImGui->wantsMouse() ) ) {
        bLooking = true;
    } else if ( bLooking && !bButton ) {
        bLooking = false;
    }
    setRelativeMouse( bLooking || bCaptured );
    if ( bLooking || bCaptured ) {
        yaw -= gpInput->getMouseDeltaX() * lookSensitivity;
        pitch = Clamp( pitch - gpInput->getMouseDeltaY() * lookSensitivity, -kMaxPitch, kMaxPitch );
        if ( yaw > kPi ) {
            yaw -= 2.0f * kPi;
        } else if ( yaw < -kPi ) {
            yaw += 2.0f * kPi;
        }
    }

    // Base speed: mouse wheel (x1.1 per notch, OpenTDU), Shift x2.
    const float wheel = gpInput->getMouseWheel();
    if ( wheel > 0.0f ) {
        baseSpeed = Min( baseSpeed * std::pow( kWheelUp, wheel ), 5000.0f );
    } else if ( wheel < 0.0f ) {
        baseSpeed = Max( baseSpeed * std::pow( kWheelDown, -wheel ), 1.0f );
    }
    auto key = []( SDL_Scancode a, SDL_Scancode b = SDL_SCANCODE_UNKNOWN, SDL_Scancode c = SDL_SCANCODE_UNKNOWN ) {
        return gpInput->isKeyDown( a ) || ( b != SDL_SCANCODE_UNKNOWN && gpInput->isKeyDown( b ) ) ||
               ( c != SDL_SCANCODE_UNKNOWN && gpInput->isKeyDown( c ) );
    };
    const bool bFast = key( SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT );
    currentSpeed = baseSpeed * ( bFast ? 2.0f : 1.0f );

    const Vector3f up( 0.0f, 1.0f, 0.0f );
    const Vector3f forward = getForward();
    // Eden camera basis: x = up ^ forward is the left of the screen
    // (ChaseCamera::LookRotation), so the right is forward ^ up.
    Vector3f right = forward.cross( up );
    right = right.length() > 1e-4f ? right.normalized() : Vector3f( -1.0f, 0.0f, 0.0f );
    Vector3f move;
    if ( key( SDL_SCANCODE_W, SDL_SCANCODE_UP ) ) {
        move = move + forward;
    }
    if ( key( SDL_SCANCODE_S, SDL_SCANCODE_DOWN ) ) {
        move = move - forward;
    }
    if ( key( SDL_SCANCODE_D, SDL_SCANCODE_RIGHT ) ) {
        move = move + right;
    }
    if ( key( SDL_SCANCODE_A, SDL_SCANCODE_LEFT ) ) {
        move = move - right;
    }
    if ( key( SDL_SCANCODE_E, SDL_SCANCODE_SPACE, SDL_SCANCODE_PAGEUP ) ) {
        move = move + up;
    }
    if ( key( SDL_SCANCODE_Q, SDL_SCANCODE_LCTRL, SDL_SCANCODE_PAGEDOWN ) ) {
        move = move - up;
    }
    if ( move.length() > 1e-4f ) {
        position = position + move.normalized() * ( currentSpeed * deltaTime );
    }
    // Ground clamp (loaded sector heightfields only): the camera stays above
    // the terrain unless turned off in the panel.
    float ground = 0.0f;
    if ( bGroundClamp && bWorldReady && view.getWorld().getHeight( position.x, position.z, ground ) &&
         position.y < ground + kGroundClearance ) {
        position.y = ground + kGroundClearance;
    }
}

void GMWorldViewer::streamWorld( bool bForce ) {
    if ( !bWorldReady ) {
        return;
    }
    const int32_t key = SectorKey( position );
    if ( key == lastSectorKey && !bForce ) {
        return;
    }
    lastSectorKey = key;
    EdenWorld &world = view.getWorld();
    // Hysteresis: sectors are dropped one ring beyond the load radius.
    if ( world.unloadFar( position, sectorRadius + 1 ) != 0 ) {
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pRenderer->clearShadowCasterCache();
        }
    }
    world.loadAround( position, sectorRadius );
}

void GMWorldViewer::streamExteriors() {
    // Spots sharing a bank share one SpotExterior (one frame per spot); the
    // load / unload band and the lightmap / shadow caster rebuilds are the
    // streamer's (game/spots/spot_exteriors.h). Nothing else in this mode is
    // lightmapped, so the return value (banks dropped) is not needed here.
    exteriors.tick( view, position, bExteriors );
}

void GMWorldViewer::tick( float deltaTime ) {
    if ( !pendingIsland.empty() ) {
        const std::string name = pendingIsland;
        pendingIsland.clear();
        loadIsland( name );
    }
    updateCamera( deltaTime );
    if ( !bWorldReady ) {
        return;
    }
    streamWorld( false );
    streamExteriors();

    EdenSceneView::Camera camera;
    camera.Position = position;
    camera.Rotation = ChaseCamera::LookRotation( position, position + getForward() );
    camera.Fov = fovDegrees * kPi / 180.0f;
    view.setCamera( camera );
    view.setWeather( pWeather );
    view.setEnvironmentCenter( position, false );
    view.tick( deltaTime );
    // The street lamp glow follows the weather hour (the car lights rule).
    view.getWorld().setNightLights( ( pWeather != nullptr && ( pWeather->Hour < 6 || pWeather->Hour >= 20 ) ) ||
                                    CmdLine::HasArg( "-lampday" ) );
}

void GMWorldViewer::drawOffscreen() {
    if ( bWorldReady && gpRender->getEdenRenderer() != nullptr ) {
        view.renderOffscreen();
    }
}

void GMWorldViewer::draw() {
    if ( bWorldReady ) {
        view.draw();
    }
}

void GMWorldViewer::exit() {
    setRelativeMouse( false );
    if ( bWorldReady || view.isReady() ) {
        unloadIsland();
    }
}


void GMWorldViewer::refreshEditorLists() {
    editorProps = ModProps::listAvailableProps();
    editorPlacements = ModProps::listPlacements();
    bEditorStale = false;
}

void GMWorldViewer::commitEditorPlacement( bool bNewLine ) {
    if ( editor.Name[ 0 ] == 0 ) {
        editorStatus = "no prop selected";
        return;
    }
    const Vector3f position( editor.Position.x, editor.Position.y, editor.Position.z );
    const float heading = editor.HeadingDegrees * kPi / 180.0f;
    const std::string shader( editor.Shader );
    const std::string name( editor.Name );
    bool bOk = false;
    if ( bNewLine || editor.FileLine < 0 ) {
        bOk = ModProps::appendPlacement( island, name, position, heading, editor.Scale, editor.bFuelStation, shader );
        editor.FileLine = -1;
    } else {
        char head[ 192 ];
        snprintf( head, sizeof( head ), "%s %s %.2f %.2f %.2f %.4f", island.c_str(), editor.Name, position.x,
                  position.y, position.z, heading );
        std::string line( head );
        char tail[ 64 ];
        if ( fabsf( editor.Scale - 1.0f ) > 1e-3f ) {
            snprintf( tail, sizeof( tail ), " %.3f", editor.Scale );
            line += tail;
        }
        if ( StringCmpI( shader.c_str(), "TD_Gouraud" ) != 0 ) {
            line += " " + shader;
        }
        if ( editor.bFuelStation ) {
            line += " fuel";
        }
        bOk = ModProps::replacePlacementLine( static_cast<size_t>( editor.FileLine ), line );
    }
    if ( !bOk ) {
        editorStatus = "props.txt write FAILED";
        return;
    }
    if ( EdenRenderer *pRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr ) {
        modProps.load( *pRenderer, island ); // the placed prop appears at once
    }
    refreshEditorLists();
    editorStatus = bNewLine ? "placed -> props.txt" : "updated -> props.txt";
}

void GMWorldViewer::drawEditorUI() {
    if ( !ImGui::CollapsingHeader( "Map editor" ) ) {
        return;
    }
    if ( bEditorStale ) {
        refreshEditorLists();
        if ( editor.Name[ 0 ] == 0 && !editorProps.empty() ) {
            snprintf( editor.Name, sizeof( editor.Name ), "%s", editorProps.front().c_str() );
        }
    }
    ImGui::SetNextItemWidth( 220.0f );
    if ( ImGui::BeginCombo( "prop", editor.Name ) ) {
        for ( const std::string &prop : editorProps ) {
            if ( ImGui::Selectable( prop.c_str(), StringCmpI( prop.c_str(), editor.Name ) == 0 ) ) {
                snprintf( editor.Name, sizeof( editor.Name ), "%s", prop.c_str() );
            }
        }
        ImGui::EndCombo();
    }
    ImGui::InputFloat3( "position", &editor.Position.x, "%.2f" );
    ImGui::SliderFloat( "heading", &editor.HeadingDegrees, -180.0f, 180.0f, "%.1f deg" );
    ImGui::SliderFloat( "scale", &editor.Scale, 0.05f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic );
    ImGui::SetNextItemWidth( 160.0f );
    ImGui::InputText( "shader", editor.Shader, sizeof( editor.Shader ) );
    ImGui::Checkbox( "fuel station", &editor.bFuelStation );
    if ( ImGui::Button( "at camera" ) ) {
        editor.Position = position;
        float ground = 0.0f;
        if ( bWorldReady && view.getWorld().getHeight( editor.Position.x, editor.Position.z, ground ) ) {
            editor.Position.y = ground;
        }
    }
    ImGui::SameLine();
    if ( ImGui::Button( "snap Y" ) ) {
        float ground = 0.0f;
        if ( bWorldReady && view.getWorld().getHeight( editor.Position.x, editor.Position.z, ground ) ) {
            editor.Position.y = ground;
        } else {
            editorStatus = "no heightfield under the point";
        }
    }
    ImGui::SameLine();
    if ( ImGui::Button( editor.FileLine >= 0 ? "update line" : "place" ) ) {
        commitEditorPlacement( editor.FileLine < 0 );
    }
    if ( editor.FileLine >= 0 ) {
        ImGui::SameLine();
        if ( ImGui::Button( "new from this" ) ) {
            editor.FileLine = -1;
            editorStatus = "editing a new placement";
        }
    }
    if ( !editorStatus.empty() ) {
        ImGui::TextColored( ImVec4( 0.6f, 1.0f, 0.6f, 1.0f ), "%s", editorStatus.c_str() );
    }
    if ( ImGui::CollapsingHeader( "placed lines" ) ) {
        if ( ImGui::SmallButton( "refresh" ) ) {
            refreshEditorLists();
        }
        for ( const ModProps::PlacementLine &line : editorPlacements ) {
            char label[ 128 ];
            snprintf( label, sizeof( label ), "%s  %.0f %.0f %.0f  %.0f deg%s", line.Name.c_str(), line.Position.x,
                      line.Position.y, line.Position.z, line.Heading * 180.0f / kPi,
                      line.bFuelStation ? "  [fuel]" : "" );
            ImGui::PushID( static_cast<int>( line.FileLine ) );
            const bool bSelected = editor.FileLine == static_cast<int32_t>( line.FileLine );
            if ( ImGui::Selectable( label, bSelected ) ) {
                snprintf( editor.Name, sizeof( editor.Name ), "%s", line.Name.c_str() );
                editor.Position = line.Position;
                editor.HeadingDegrees = line.Heading * 180.0f / kPi;
                editor.Scale = line.Scale;
                editor.bFuelStation = line.bFuelStation;
                snprintf( editor.Shader, sizeof( editor.Shader ), "%s", line.ShaderName.c_str() );
                editor.FileLine = static_cast<int32_t>( line.FileLine );
                editorStatus.clear();
            }
            ImGui::PopID();
        }
    }
}

void GMWorldViewer::drawUI() {
    if ( !bShowPanel ) {
        return;
    }
    ImGui::SetNextWindowPos( DpiScaled( 10.0f, 10.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( DpiScaled( 400.0f, 0.0f ), ImGuiCond_FirstUseEver );
    if ( !ImGui::Begin( "World Viewer" ) ) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted( "Island" );
    for ( const char *pIsland : kIslands ) {
        ImGui::SameLine();
        if ( ImGui::RadioButton( IslandLabel( pIsland ), island == pIsland ) && island != pIsland ) {
            pendingIsland = pIsland;
        }
    }
    if ( !pendingIsland.empty() ) {
        ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "loading %s...", IslandLabel( pendingIsland.c_str() ) );
    }
    if ( !statusText.empty() ) {
        ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.3f, 1.0f ), "%s", statusText.c_str() );
    }
    if ( bWorldReady ) {
        const EdenWorld &world = view.getWorld();
        const EdenWorld::SectorId id = EdenWorld::getSectorAt( position );
        ImGui::Text( "grid %d x %d areas, %zu areas, %zu sectors loaded", world.getAreaColumns(), world.getAreaRows(),
                     world.getNumAreas(), world.getNumSectors() );
        ImGui::Text( "missing sector banks skipped: %zu%s", world.getNumFailedSectors(),
                     world.hasIslandTerrain() ? ", island heightfield" : "" );
        ImGui::Text( "sector-%d-%d-%d-%d", id.AreaX, id.AreaY, id.X, id.Y );
    }
    ImGui::Separator();
    ImGui::Text( "pos %.1f %.1f %.1f", position.x, position.y, position.z );
    ImGui::Text( "yaw %.1f pitch %.1f deg", yaw * 180.0f / kPi, pitch * 180.0f / kPi );
    ImGui::Text( "speed %.1f m/s (%.0f km/h)%s", currentSpeed, currentSpeed * 3.6f,
                 currentSpeed > baseSpeed ? "  [Shift x2]" : "" );
    ImGui::SliderFloat( "base speed", &baseSpeed, 1.0f, 2000.0f, "%.0f m/s", ImGuiSliderFlags_Logarithmic );
    ImGui::SliderFloat( "fov", &fovDegrees, 20.0f, 110.0f, "%.0f deg" );
    ImGui::SliderFloat( "look", &lookSensitivity, 0.0005f, 0.01f, "%.4f rad/px", ImGuiSliderFlags_Logarithmic );
    if ( ImGui::SliderInt( "sector radius", &sectorRadius, 0, 4 ) ) {
        streamWorld( true );
    }
    ImGui::Checkbox( "ground clamp", &bGroundClamp );
    ImGui::SameLine();
    ImGui::Checkbox( "spot exteriors", &bExteriors );
    ImGui::SameLine();
    ImGui::Text( "(%zu banks loaded)", exteriors.getNumBanks() );
    if ( !gameWeathers.getPresets().empty() &&
         ImGui::BeginCombo( "weather", pWeather != nullptr ? pWeather->Name.c_str() : "(none)" ) ) {
        const std::vector<WeatherPreset> &presets = gameWeathers.getPresets();
        for ( size_t i = 0; i < presets.size(); i++ ) {
            if ( ImGui::Selectable( presets[i].Name.c_str(), &presets[i] == pWeather ) ) {
                pWeather = &presets[i];
                selectedWeather = static_cast<int32_t>( i );
            }
        }
        ImGui::EndCombo();
    }
    if ( !spots.empty() ) {
        ImGui::Separator();
        ImGui::Text( "spots.bin: %zu spots (n = sector banks present /9)", spots.size() );
        if ( ImGui::BeginListBox( "##spots", ImVec2( -1.0f, 160.0f ) ) ) {
            for ( size_t i = 0; i < spots.size(); i++ ) {
                const SpotRecord &spot = spots[i];
                char label[96];
                snprintf( label, sizeof( label ), "%u %-8s (%.0f, %.0f, %.0f)##%zu", spot.NumSectors,
                          spot.Bank.c_str(), spot.Position.x, spot.Position.y, spot.Position.z, i );
                if ( ImGui::Selectable( label, selectedSpot == static_cast<int32_t>( i ) ) ) {
                    selectedSpot = static_cast<int32_t>( i );
                    teleport( spot.Position, spot.Heading, spot.bHasSector ? kSpotHeight : kNoSectorHeight );
                    streamWorld( true );
                }
            }
            ImGui::EndListBox();
        }
    }
    ImGui::Separator();
    drawEditorUI();
    ImGui::Separator();
    ImGui::TextDisabled( "RMB (or LMB outside panel): look, Tab: capture mouse" );
    ImGui::TextDisabled( "WSAD / arrows: move, E/Space up, Q/Ctrl down" );
    ImGui::TextDisabled( "Shift: x2, wheel: base speed, F2: panel, F1: debug" );
    ImGui::TextDisabled( "F5 / F6 / F7: Ibiza / Hawai / Casino" );
    ImGui::End();
}
