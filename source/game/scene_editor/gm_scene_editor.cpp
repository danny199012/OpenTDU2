#include "shared.h"
#include "gm_scene_editor.h"

#include "debug/gs_imgui.h"
#include "filesystem/gs_file.h"
#include "game/freeride/chase_camera.h"
#include "game/scene_editor/scene_cmd.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kMaxPitch = 89.0f * kPi / 180.0f;
constexpr float kSpotHeight = 25.0f;
constexpr float kNoSectorHeight = 400.0f;
constexpr float kWheelUp = 1.1f;
constexpr float kWheelDown = 0.9090909f;

const char *IslandLabel( const char *pIsland ) {
    if ( strcmp( pIsland, "ibiza" ) == 0 ) {
        return "Ibiza";
    }
    if ( strcmp( pIsland, "hawai" ) == 0 ) {
        return "Hawai";
    }
    if ( strcmp( pIsland, "eden" ) == 0 ) {
        return "Eden";
    }
    if ( strcmp( pIsland, "hongkong" ) == 0 ) {
        return "Hong Kong";
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
    for ( const char *pIsland : GMSceneEditor::kIslands ) {
        if ( name == pIsland ) {
            return true;
        }
    }
    return false;
}
} // namespace

GMSceneEditor::GMSceneEditor() : GameMode( eGameMode::GM_SceneEditor ) {}

GMSceneEditor::~GMSceneEditor() {
    exit();
}

bool GMSceneEditor::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    sectorRadius = Clamp( CmdLine::GetArgInt( "-sectorradius", 2 ), 0, 4 );
    bExteriors = !CmdLine::HasArg( "-noexteriors" );
    bShowPanel = !CmdLine::HasArg( "-nopanel" );
    if ( const char *pSpeed = CmdLine::GetArgValue( "-flyspeed" ) ) {
        baseSpeed = Clamp( static_cast<float>( atof( pSpeed ) ), 1.0f, 5000.0f );
    }
    const char *pIsland = CmdLine::GetArgValue( "-island" );
    std::string name = ToLower( std::string( pIsland != nullptr ? pIsland : "ibiza" ) );
    if ( name == "hawaii" || name == "oahu" ) {
        name = "hawai";
    }
    if ( !IsKnownIsland( name ) ) {
        OTDU_LOG_WARN( "SceneEditor: unknown island '%s' (ibiza, hawai, casino, eden, hongkong), using ibiza\n",
                       name.c_str() );
        name = "ibiza";
    }
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
    // The scene to edit: -scene <file> of the overlay's scenes/ folder.
    if ( const char *pScene = CmdLine::GetArgValue( "-scene" ) ) {
        std::string error;
        if ( doc.loadFromVfs( pScene, error ) ) {
            applyOverrides();
            markSceneChanged();
            OTDU_LOG_INFO( "SceneEditor: scene '%s' loaded (%zu objects, %zu overrides)\n", pScene, doc.Objects.size(),
                           doc.Overrides.size() );
        } else {
            statusText = "scene '" + std::string( pScene ) + "': " + error;
            OTDU_LOG_ERROR( "SceneEditor: %s\n", statusText.c_str() );
        }
    }
    // One-shot batch: -editorscript <file.json> (a JSON array of commands,
    // docs/studio_ai_api.md).
    bBatchRun = false;
    if ( const char *pScript = CmdLine::GetArgValue( "-editorscript" ) ) {
        SceneEditorRunScript( *this, pScript );
        bBatchRun = true;
    }
    // The live command server (127.0.0.1 only): the AI driver.
    if ( !CmdLine::HasArg( "-noserver" ) ) {
        pCommandServer = std::make_unique<SceneCommandServer>();
        if ( !pCommandServer->start( CmdLine::GetArgInt( "-editorport", 9377 ), this ) ) {
            pCommandServer.reset();
        }
    }
    streamWorld( true );
    return true;
}

void GMSceneEditor::loadSpots() {
    spots.clear();
    spots = LoadSpotList( island );
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
    OTDU_LOG_INFO( "SceneEditor: %s: %zu spots\n", island.c_str(), spots.size() );
    refreshSpotRecords();
}

void GMSceneEditor::refreshSpotRecords() {
    // The scene's spot placements join the island's own spots as extra
    // records: one streaming path (SpotExteriors) for every building. They
    // APPEND (appendSpots): setSpots would unload the loaded banks and leave
    // the view's lightmap list dangling (the crash of the first run).
    std::string signature;
    std::vector<SpotRecord> sceneSpots;
    for ( const SceneObject &object : doc.Objects ) {
        if ( object.Kind != "spot" || object.Ref.empty() ) {
            continue;
        }
        signature += object.Id + "@" + object.Ref + ":" +
                     std::to_string( object.Position.x ) + "," + std::to_string( object.Position.y ) + "," +
                     std::to_string( object.Position.z ) + "," + std::to_string( object.Yaw ) + ";";
        SpotRecord record;
        record.Id = GetIdentifier64bit( object.Id.c_str() );
        record.Bank = object.Ref;
        record.Position = object.Position;
        record.Heading = object.Yaw * kPi / 180.0f;
        const std::string exterior =
            gpFile->getBankFolder() + "islands/" + island + "/exterior/" + ToLower( object.Ref ) + ".bnk";
        if ( gpFile->fileExists( exterior.c_str() ) ) {
            record.ExteriorPath = exterior;
        }
        record.NumSectors = 9;
        record.bHasSector = true;
        sceneSpots.push_back( std::move( record ) );
    }
    if ( signature == spotSignature ) {
        return;
    }
    spotSignature = signature;
    exteriors.appendSpots( sceneSpots );
}

bool GMSceneEditor::loadIsland( const std::string &name ) {
    unloadIsland();
    island = name;
    statusText.clear();
    doc.Island = name;
    loadSpots();

    selectedSpot = -1;
    const char *pSpotArg = CmdLine::GetArgValue( "-spot" );
    for ( size_t i = 0; i < spots.size() && pSpotArg != nullptr; i++ ) {
        if ( StringCmpI( spots[ i ].Bank.c_str(), pSpotArg ) == 0 ) {
            selectedSpot = static_cast<int32_t>( i );
            break;
        }
    }
    if ( selectedSpot < 0 ) {
        uint32_t bestSectors = 0;
        for ( size_t i = 0; i < spots.size(); i++ ) {
            if ( spots[ i ].bHasSector && spots[ i ].NumSectors > bestSectors ) {
                bestSectors = spots[ i ].NumSectors;
                selectedSpot = static_cast<int32_t>( i );
            }
        }
    }
    if ( selectedSpot < 0 && !spots.empty() ) {
        selectedSpot = 0;
    }
    if ( selectedSpot >= 0 ) {
        const SpotRecord &spot = spots[ selectedSpot ];
        teleport( spot.Position, spot.Heading, spot.bHasSector ? kSpotHeight : kNoSectorHeight );
        OTDU_LOG_INFO( "SceneEditor: start spot %s (%.1f, %.1f, %.1f)\n", spot.Bank.c_str(), spot.Position.x,
                       spot.Position.y, spot.Position.z );
    } else {
        teleport( Vector3f( 3072.0f, 0.0f, 3072.0f ), 0.0f, kNoSectorHeight );
    }

    if ( !view.initialize( island.c_str(), position, sectorRadius ) ) {
        statusText = "island '" + island + "' could not be loaded (Euro/Bnk/islands/" + island + "/level)";
        OTDU_LOG_ERROR( "SceneEditor: %s\n", statusText.c_str() );
        view.destroy();
        return false;
    }
    view.getWorld().setIslandTerrainEnabled( true );
    // The scene instances draw with the exteriors through the draw callback;
    // the world pointer feeds the library / actor placements.
    sceneObjects.setWorld( &view.getWorld() );
    if ( EdenRenderer *pEdenRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr ) {
        modProps.load( *pEdenRenderer, island );
    }
    view.setDrawCallback( [ this ]( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
        if ( pass == EdenSceneView::ePass::ProjectorActors ) {
            return;
        }
        exteriors.draw( renderer );
        modProps.draw( renderer );
        sceneObjects.draw( renderer );
    } );
    gameWeathers = WeatherLibrary();
    gameWeathers.load( ( gpFile->getBankFolder() + "islands/" + island + "/fx/game_weathers.xmb" ).c_str() );
    const char *pWeatherName = CmdLine::GetArgValue( "-weather" );
    pWeather = gameWeathers.find( pWeatherName != nullptr ? pWeatherName : "14" );
    if ( pWeather == nullptr && !gameWeathers.getPresets().empty() ) {
        pWeather = &gameWeathers.getPresets().front();
    }
    if ( pWeather == nullptr ) {
        pWeather = !view.getWeathers().getPresets().empty() ? &view.getWeathers().getPresets().front() : nullptr;
    }
    selectedWeather = 0;
    for ( size_t i = 0; i < gameWeathers.getPresets().size(); i++ ) {
        if ( &gameWeathers.getPresets()[ i ] == pWeather ) {
            selectedWeather = static_cast<int32_t>( i );
        }
    }
    view.setWeather( pWeather );
    view.setShadowConfig( EdenShadows::FindConfig( "FREERIDE" ) );
    lastSectorKey = SectorKey( position );
    bWorldReady = true;
    bAssetsStale = true;
    applyOverrides();
    markSceneChanged();
    const EdenWorld &world = view.getWorld();
    OTDU_LOG_INFO( "SceneEditor: %s ready: %d x %d areas grid, %zu areas, %zu sectors, weather '%s'\n", island.c_str(),
                   world.getAreaColumns(), world.getAreaRows(), world.getNumAreas(), world.getNumSectors(),
                   pWeather != nullptr ? pWeather->Name.c_str() : "(none)" );
    return true;
}

void GMSceneEditor::unloadIsland() {
    if ( gpRender != nullptr ) {
        gpRender->getDevice().waitIdle();
    }
    sceneObjects.unload();
    exteriors.unload();
    modProps.unload();
    view.destroy();
    pWeather = nullptr;
    bWorldReady = false;
    lastSectorKey = INT32_MIN;
    spotSignature.clear();
}

void GMSceneEditor::teleport( const Vector3f &target, float heading, float height ) {
    position = target + Vector3f( 0.0f, height, 0.0f );
    yaw = heading;
    pitch = -15.0f * kPi / 180.0f;
}

Vector3f GMSceneEditor::getForward() const {
    return Vector3f( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
}

void GMSceneEditor::setRelativeMouse( bool bEnable ) {
    if ( bEnable == bRelativeMouse || pGameInstance == nullptr || pGameInstance->getWindow() == nullptr ) {
        return;
    }
    bRelativeMouse = bEnable;
    SDL_SetWindowRelativeMouseMode( pGameInstance->getWindow(), bEnable );
}

void GMSceneEditor::updateCamera( float deltaTime ) {
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_TAB ) ) {
        bCaptured = !bCaptured;
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F2 ) ) {
        bShowPanel = !bShowPanel;
    }
    for ( uint32_t i = 0; i < sizeof( kIslands ) / sizeof( kIslands[ 0 ] ); i++ ) {
        if ( gpInput->wasKeyPressed( SDL_SCANCODE_F5 + i ) && island != kIslands[ i ] ) {
            pendingIsland = kIslands[ i ];
        }
    }
    // Undo / redo / duplicate / delete (not while a text field has the keys)
    const bool bUiKeys = gpImGui != nullptr && gpImGui->wantsKeyboard();
    if ( !bUiKeys ) {
        if ( gpInput->isKeyDown( SDL_SCANCODE_LCTRL ) || gpInput->isKeyDown( SDL_SCANCODE_RCTRL ) ) {
            if ( gpInput->wasKeyPressed( SDL_SCANCODE_Z ) ) {
                undo();
            }
            if ( gpInput->wasKeyPressed( SDL_SCANCODE_Y ) ) {
                redo();
            }
            if ( gpInput->wasKeyPressed( SDL_SCANCODE_D ) ) {
                duplicateSelected();
            }
        }
        if ( gpInput->wasKeyPressed( SDL_SCANCODE_DELETE ) || gpInput->wasKeyPressed( SDL_SCANCODE_X ) ) {
            if ( !selectedId.empty() ) {
                deleteSelected();
            }
        }
    }

    // Look: the RIGHT mouse button only (the left one picks / drags), or the
    // Tab captured mode.
    const bool bButton = gpInput->isMouseButtonDown( SDL_BUTTON_RIGHT );
    const bool bMouseFree = gpImGui == nullptr || !gpImGui->wantsMouse();
    if ( !bLooking && bButton && bMouseFree ) {
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
    float ground = 0.0f;
    if ( bGroundClamp && bWorldReady && view.getWorld().getHeight( position.x, position.z, ground ) &&
         position.y < ground + 1.5f ) {
        position.y = ground + 1.5f;
    }
}

void GMSceneEditor::streamWorld( bool bForce ) {
    if ( !bWorldReady ) {
        return;
    }
    const int32_t key = SectorKey( position );
    if ( key == lastSectorKey && !bForce ) {
        return;
    }
    lastSectorKey = key;
    EdenWorld &world = view.getWorld();
    if ( world.unloadFar( position, sectorRadius + 1 ) != 0 ) {
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pRenderer->clearShadowCasterCache();
        }
    }
    world.loadAround( position, sectorRadius );
}

void GMSceneEditor::streamExteriors() {
    exteriors.tick( view, position, bExteriors );
    // The hide overrides of the scene cover the streamed banks too (idempotent,
    // cheap: a bank is a handful of models).
    if ( !doc.Overrides.empty() ) {
        const std::unordered_set<uint64_t> &hiddenNodes = view.getWorld().getHiddenNodeHashes();
        const std::unordered_set<uint64_t> &hiddenMaterials = view.getWorld().getHiddenMaterialHashes();
        if ( !hiddenNodes.empty() || !hiddenMaterials.empty() ) {
            for ( const auto &pExterior : exteriors.getExteriors() ) {
                pExterior->applyVisibilityOverrides( hiddenNodes, hiddenMaterials, view.getWorld().getHiddenMaterialNames() );
            }
        }
    }
}

void GMSceneEditor::tick( float deltaTime ) {
    // The live command server: queued commands run here, on the editor thread.
    if ( pCommandServer != nullptr ) {
        pCommandServer->tick();
    }
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

    // Scene edits re-instantiate on the next tick (a batch / server command
    // may have changed the document outside the UI flow).
    if ( bRebuildPending ) {
        bRebuildPending = false;
        sceneObjects.applyDocument( doc );
        refreshSpotRecords();
    }

    EdenSceneView::Camera camera;
    camera.Position = position;
    camera.Rotation = ChaseCamera::LookRotation( position, position + getForward() );
    camera.Fov = fovDegrees * kPi / 180.0f;
    view.setCamera( camera );
    view.setWeather( pWeather );
    view.setEnvironmentCenter( position, false );
    view.tick( deltaTime );
    view.getWorld().setNightLights( ( pWeather != nullptr && ( pWeather->Hour < 6 || pWeather->Hour >= 20 ) ) ||
                                    CmdLine::HasArg( "-lampday" ) );

    // --- picking / dragging (the ImGui overlay has the mouse over panels) ---
    const bool bMouseFree = gpImGui == nullptr || !gpImGui->wantsMouse();
    const ImGuiIO &io = ImGui::GetIO();
    const bool bLeft = gpInput->isMouseButtonDown( SDL_BUTTON_LEFT ) && bMouseFree && !io.WantCaptureMouse;
    const bool bLeftPressed = bLeft && !bPrevLeftMouse;
    const bool bLeftReleased = !bLeft && bPrevLeftMouse;
    bPrevLeftMouse = bLeft;
    if ( bMouseFree && view.isReady() ) {
        if ( bLeftPressed ) {
            pickAt( io.MousePos.x, io.MousePos.y );
            if ( hover.bSceneObject ) {
                // Pick on the selection: start the ground-plane drag. On
                // anything else: select it.
                if ( hover.ObjectId == selectedId ) {
                    bDragging = true;
                    const SceneObjects::Instance *pInstance = sceneObjects.findInstance( selectedId );
                    const SceneObject *pObject = doc.findObject( selectedId );
                    if ( pObject != nullptr ) {
                        SceneRay ray = screenRay( io.MousePos.x, io.MousePos.y );
                        float t = 0.0f;
                        if ( std::fabs( ray.Direction.y ) > 1e-5f ) {
                            t = ( pObject->Position.y - ray.Origin.y ) / ray.Direction.y;
                        }
                        const Vector3f grab = ray.Origin + ray.Direction * t;
                        dragGrabOffset = grab - pObject->Position;
                    }
                    (void)pInstance;
                } else {
                    selectedId = hover.ObjectId;
                }
            } else {
                selectedId.clear();
            }
        }
        if ( bDragging && bLeft ) {
            const SceneObject *pObject = doc.findObject( selectedId );
            if ( pObject == nullptr ) {
                bDragging = false;
            } else {
                SceneRay ray = screenRay( io.MousePos.x, io.MousePos.y );
                float t = 0.0f;
                if ( std::fabs( ray.Direction.y ) > 1e-5f ) {
                    t = ( pObject->Position.y - ray.Origin.y ) / ray.Direction.y;
                }
                if ( t > 0.0f ) {
                    const Vector3f grab = ray.Origin + ray.Direction * t;
                    Vector3f target = grab - dragGrabOffset;
                    if ( gridSnap > 0.01f ) {
                        target.x = std::floor( target.x / gridSnap + 0.5f ) * gridSnap;
                        target.z = std::floor( target.z / gridSnap + 0.5f ) * gridSnap;
                    }
                    doc.findObject( selectedId )->Position = target;
                    bRebuildPending = true;
                }
            }
        }
        if ( bLeftReleased && bDragging ) {
            bDragging = false;
        }
        if ( !bLeft ) {
            // Hover pick for the status line (cheap enough per frame: the
            // scene objects are AABBs, the world raycast walks the loaded
            // sectors' grids).
            pickAt( io.MousePos.x, io.MousePos.y );
        }
    }
}

void GMSceneEditor::drawOffscreen() {
    if ( bWorldReady && gpRender->getEdenRenderer() != nullptr ) {
        view.renderOffscreen();
    }
}

void GMSceneEditor::draw() {
    if ( bWorldReady ) {
        view.draw();
    }
    // Selection highlight: an ImGui overlay box around the selected object
    // (screen space projection of its AABB; the draw lists draw over the
    // scene, under the panels).
    if ( bWorldReady && !selectedId.empty() && view.isReady() ) {
        const SceneObjects::Instance *pInstance = sceneObjects.findInstance( selectedId );
        if ( pInstance != nullptr ) {
            const EdenSceneParams &params = view.getSceneParams();
            ImDrawList *pDraw = ImGui::GetBackgroundDrawList();
            const Vector3f corners[ 8 ] = {
                Vector3f( pInstance->BoundsMin.x, pInstance->BoundsMin.y, pInstance->BoundsMin.z ),
                Vector3f( pInstance->BoundsMax.x, pInstance->BoundsMin.y, pInstance->BoundsMin.z ),
                Vector3f( pInstance->BoundsMin.x, pInstance->BoundsMax.y, pInstance->BoundsMin.z ),
                Vector3f( pInstance->BoundsMax.x, pInstance->BoundsMax.y, pInstance->BoundsMin.z ),
                Vector3f( pInstance->BoundsMin.x, pInstance->BoundsMin.y, pInstance->BoundsMax.z ),
                Vector3f( pInstance->BoundsMax.x, pInstance->BoundsMin.y, pInstance->BoundsMax.z ),
                Vector3f( pInstance->BoundsMin.x, pInstance->BoundsMax.y, pInstance->BoundsMax.z ),
                Vector3f( pInstance->BoundsMax.x, pInstance->BoundsMax.y, pInstance->BoundsMax.z ),
            };
            const Matrix4x4f vp = params.View * params.Projection; // world -> clip, the row-vector law
            bool bAllBehind = true;
            ImVec2 screen[ 8 ];
            for ( int32_t i = 0; i < 8; i++ ) {
                // clip = p * VP (the row-vector law; no transformVector4 in
                // math.h, so the four rows sum here).
                const Vector3f &p = corners[ i ];
                Vector4f clip( p.x * vp.m[ 0 ][ 0 ] + p.y * vp.m[ 1 ][ 0 ] + p.z * vp.m[ 2 ][ 0 ] + vp.m[ 3 ][ 0 ],
                               p.x * vp.m[ 0 ][ 1 ] + p.y * vp.m[ 1 ][ 1 ] + p.z * vp.m[ 2 ][ 1 ] + vp.m[ 3 ][ 1 ],
                               p.x * vp.m[ 0 ][ 2 ] + p.y * vp.m[ 1 ][ 2 ] + p.z * vp.m[ 2 ][ 2 ] + vp.m[ 3 ][ 2 ],
                               p.x * vp.m[ 0 ][ 3 ] + p.y * vp.m[ 1 ][ 3 ] + p.z * vp.m[ 2 ][ 3 ] + vp.m[ 3 ][ 3 ] );
                if ( clip.w > 1e-4f ) {
                    bAllBehind = false;
                    const float ndcX = clip.x / clip.w;
                    const float ndcY = clip.y / clip.w;
                    screen[ i ] = ImVec2( ( ndcX * 0.5f + 0.5f ) * static_cast<float>( params.Width ),
                                          ( 1.0f - ( ndcY * 0.5f + 0.5f ) ) * static_cast<float>( params.Height ) );
                } else {
                    screen[ i ] = ImVec2( -1000.0f, -1000.0f );
                }
            }
            if ( !bAllBehind ) {
                static const int32_t kEdges[ 12 ][ 2 ] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
                                                           { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
                                                           { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
                for ( const auto &edge : kEdges ) {
                    pDraw->AddLine( screen[ edge[ 0 ] ], screen[ edge[ 1 ] ], IM_COL32( 255, 200, 60, 255 ), 2.0f );
                }
            }
        }
    }
}

void GMSceneEditor::exit() {
    if ( pCommandServer != nullptr ) {
        pCommandServer->stop();
        pCommandServer.reset();
    }
    setRelativeMouse( false );
    if ( bWorldReady || view.isReady() ) {
        unloadIsland();
    }
}

// --- edits ------------------------------------------------------------------

void GMSceneEditor::pushUndo() {
    undoStack.push_back( doc );
    if ( undoStack.size() > 64 ) {
        undoStack.erase( undoStack.begin() );
    }
    redoStack.clear();
}

void GMSceneEditor::undo() {
    if ( undoStack.empty() ) {
        return;
    }
    redoStack.push_back( doc );
    doc = undoStack.back();
    undoStack.pop_back();
    markSceneChanged();
    statusText = "undo";
}

void GMSceneEditor::redo() {
    if ( redoStack.empty() ) {
        return;
    }
    undoStack.push_back( doc );
    doc = redoStack.back();
    redoStack.pop_back();
    markSceneChanged();
    statusText = "redo";
}

void GMSceneEditor::markSceneChanged() {
    bRebuildPending = true;
}

std::string GMSceneEditor::placeAsset( const std::string &kind, const std::string &ref, const Vector3f &atPosition,
                                       bool bAtCamera ) {
    pushUndo();
    SceneObject object;
    object.Id = doc.uniqueId( ref.empty() ? kind : ref );
    object.Kind = kind;
    object.Ref = ref;
    object.Position = atPosition;
    if ( bAtCamera ) {
        object.Position = position + getForward() * 8.0f;
        object.Position.y = position.y - 1.5f;
    }
    if ( bSnapGround && kind != "group" ) {
        float ground = 0.0f;
        if ( view.getWorld().getSurfaceHeight( object.Position.x, object.Position.z, object.Position.y + 50.0f, ground ) ) {
            object.Position.y = ground;
        }
    }
    if ( gridSnap > 0.01f ) {
        object.Position.x = std::floor( object.Position.x / gridSnap + 0.5f ) * gridSnap;
        object.Position.z = std::floor( object.Position.z / gridSnap + 0.5f ) * gridSnap;
    }
    doc.Objects.push_back( object );
    markSceneChanged();
    statusText = "placed " + object.Id;
    return object.Id;
}

void GMSceneEditor::deleteSelected() {
    if ( selectedId.empty() ) {
        return;
    }
    pushUndo();
    doc.removeObject( selectedId );
    selectedId.clear();
    markSceneChanged();
    statusText = "deleted";
}

void GMSceneEditor::duplicateSelected() {
    const SceneObject *pObject = doc.findObject( selectedId );
    if ( pObject == nullptr ) {
        return;
    }
    pushUndo();
    SceneObject copy = *pObject;
    copy.Id = doc.uniqueId( pObject->Id );
    copy.Position = copy.Position + Vector3f( 2.0f, 0.0f, 0.0f );
    doc.Objects.push_back( copy );
    selectedId = copy.Id;
    markSceneChanged();
    statusText = "duplicated -> " + copy.Id;
}

void GMSceneEditor::snapSelectedToGround() {
    SceneObject *pObject = doc.findObject( selectedId );
    if ( pObject == nullptr ) {
        return;
    }
    pushUndo();
    float ground = 0.0f;
    if ( view.getWorld().getSurfaceHeight( pObject->Position.x, pObject->Position.z, pObject->Position.y + 50.0f,
                                           ground ) ) {
        pObject->Position.y = ground;
        markSceneChanged();
        statusText = "snapped to the surface";
    } else {
        statusText = "no heightfield under the object";
    }
}

void GMSceneEditor::addOverride( const std::string &kind, const std::string &target ) {
    if ( target.empty() ) {
        return;
    }
    for ( const SceneOverride &existing : doc.Overrides ) {
        if ( existing.Kind == kind && existing.Target == target ) {
            statusText = "already hidden: " + target;
            return;
        }
    }
    pushUndo();
    SceneOverride override;
    override.Kind = kind;
    override.Target = target;
    override.bHidden = true;
    doc.Overrides.push_back( override );
    applyOverrides();
    statusText = "hidden: " + target;
}

void GMSceneEditor::applyOverrides() {
    if ( !bWorldReady ) {
        return;
    }
    EdenWorld &world = view.getWorld();
    world.clearHiddenOverrides();
    for ( const SceneOverride &override : doc.Overrides ) {
        if ( override.Kind == "material" ) {
            world.addHiddenMaterial( override.Target );
        } else {
            world.addHiddenNode( override.Target );
        }
    }
}

void GMSceneEditor::commitPropsToGame() {
    uint32_t committed = 0;
    for ( const SceneObject &object : doc.Objects ) {
        if ( ( object.Kind != "prop" && object.Kind != "obj" ) || !object.bVisible || object.Ref.empty() ) {
            continue;
        }
        if ( ModProps::appendPlacement( island, object.Ref, object.Position, object.Yaw * kPi / 180.0f,
                                        object.Scale.x, false, "TD_Gouraud" ) ) {
            committed++;
        }
    }
    statusText = std::to_string( committed ) + " prop placements -> props.txt (relaunch the game to see them)";
    OTDU_LOG_INFO( "SceneEditor: %s\n", statusText.c_str() );
}

void GMSceneEditor::refreshAssets() {
    assets.clear();
    bAssetsStale = false;
    if ( !bWorldReady ) {
        return;
    }
    const EdenWorld &world = view.getWorld();
    // Library nodes: the placed pieces of the island (the .pmi V/O palette).
    if ( world.getLibraryModel().isLoaded() ) {
        std::vector<std::string> names;
        for ( const SceneNode &node : world.getLibraryModel().getScene().getNodes() ) {
            if ( node.ObjectHashcode == 0 || node.Name.empty() ) {
                continue;
            }
            names.push_back( node.Name );
        }
        std::sort( names.begin(), names.end() );
        names.erase( std::unique( names.begin(), names.end() ), names.end() );
        for ( const std::string &name : names ) {
            assets.push_back( { name, "library" } );
        }
    }
    // Actors (worldactors.bnk): signs, birds, story props.
    std::vector<std::string> actorNames;
    for ( const auto &entry : world.getActorNames() ) {
        actorNames.push_back( entry.second );
    }
    std::sort( actorNames.begin(), actorNames.end() );
    actorNames.erase( std::unique( actorNames.begin(), actorNames.end() ), actorNames.end() );
    for ( const std::string &name : actorNames ) {
        assets.push_back( { name, "actor" } );
    }
    // The island's building banks (what spots.bin places).
    std::vector<std::string> banks;
    for ( const SpotRecord &spot : spots ) {
        if ( !spot.ExteriorPath.empty() ) {
            banks.push_back( spot.Bank );
        }
    }
    std::sort( banks.begin(), banks.end() );
    banks.erase( std::unique( banks.begin(), banks.end() ), banks.end() );
    for ( const std::string &bank : banks ) {
        assets.push_back( { bank, "spot" } );
    }
    // The overlay's OBJ props.
    for ( const std::string &prop : ModProps::listAvailableProps() ) {
        assets.push_back( { prop, "prop" } );
    }
}

SceneRay GMSceneEditor::screenRay( float x, float y ) const {
    // The real View / Projection of the last view.tick (the scene editor's
    // camera is the only camera): tan halves from the projection itself, so
    // the fov scale / narrow-aspect laws are exact.
    SceneRay ray;
    ray.Origin = position;
    const EdenSceneParams &params = view.getSceneParams();
    const float tanHalfX = 1.0f / std::fabs( params.Projection.m[ 0 ][ 0 ] );
    const float tanHalfY = 1.0f / std::fabs( params.Projection.m[ 1 ][ 1 ] );
    const Vector3f forward = getForward();
    Vector3f right = forward.cross( Vector3f( 0.0f, 1.0f, 0.0f ) );
    right = right.length() > 1e-4f ? right.normalized() : Vector3f( -1.0f, 0.0f, 0.0f );
    const Vector3f up = right.cross( forward ).normalized();
    const float u = x / static_cast<float>( params.Width );
    const float v = y / static_cast<float>( params.Height );
    ray.Direction = ( forward + right * ( ( 2.0f * u - 1.0f ) * tanHalfX ) + up * ( ( 1.0f - 2.0f * v ) * tanHalfY ) )
                        .normalized();
    return ray;
}

void GMSceneEditor::pickAt( float x, float y ) {
    hover = PickInfo();
    if ( !bWorldReady || !view.isReady() ) {
        return;
    }
    const SceneRay ray = screenRay( x, y );
    float sceneDistance = 0.0f;
    std::string objectId;
    const bool bSceneHit = sceneObjects.raycast( ray, sceneDistance, objectId );
    float worldDistance = 0.0f;
    std::string nodeName;
    const bool bWorldHit = view.getWorld().raycastObjects( ray.Origin, ray.Direction, worldDistance, nodeName );
    if ( bSceneHit && ( !bWorldHit || sceneDistance <= worldDistance ) ) {
        hover.bSceneObject = true;
        hover.ObjectId = objectId;
        hover.Distance = sceneDistance;
        hover.Position = ray.Origin + ray.Direction * sceneDistance;
    } else if ( bWorldHit ) {
        hover.bWorld = true;
        hover.NodeName = nodeName;
        hover.Distance = worldDistance;
        hover.Position = ray.Origin + ray.Direction * worldDistance;
    }
}
