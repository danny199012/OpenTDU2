#include "shared.h"
#include "gm_scene_editor.h"

#include "debug/gs_imgui.h"
#include "filesystem/gs_file.h"
#include "game/freeride/chase_camera.h"
#include "game/scene_editor/scene_cmd.h"
#include "game/scene_editor/scene_export.h"
#include "game/scene_editor/scene_interior.h"
#include "input/gs_input.h"
#include "physics/havok_collision.h"
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

// The walk body (the walk_mode.cpp law the editor walk mirrors: the proxy
// centre + eye offset, the 0.3 m sphere, the 0.4 m step / 2 m drop bounds).
constexpr float kWalkEyeHeight = 1.6f;
constexpr float kWalkRadius = 0.3f;
constexpr float kWalkStepUp = 0.4f;
constexpr float kWalkMaxDrop = 2.0f;
constexpr float kWalkStartRay = 10.0f;

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
    streamWorld( true, position );
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
    // The scene's spot AND interior placements join the island's own spots as
    // extra records: one streaming path (SpotExteriors) for every building
    // and every room (M4). They APPEND (appendSpots): setSpots would unload
    // the loaded banks and leave the view's lightmap list dangling (the
    // crash of the first run).
    //
    // The placement rides the parent chain (an interior attached to a group
    // composes): position + heading come out of the resolved world matrix.
    // The heading is the doc's own yaw (the object matrix's local-Z column:
    // atan2(-z.x, z.z) returns the same degrees the plain placement carried);
    // pitch / roll / scale do not survive a spot record - the same law the
    // plain placements have always had.
    std::string signature;
    std::vector<SpotRecord> sceneSpots;
    std::vector<Matrix4x4f> worlds;
    doc.resolveWorlds( worlds );
    for ( size_t i = 0; i < doc.Objects.size(); i++ ) {
        const SceneObject &object = doc.Objects[ i ];
        const bool bSpot = object.Kind == "spot";
        const bool bInterior = object.Kind == "interior";
        if ( ( !bSpot && !bInterior ) || object.Ref.empty() ) {
            continue;
        }
        const Matrix4x4f &world = worlds[ i ];
        const Vector3f recordPos( world.m[ 3 ][ 0 ], world.m[ 3 ][ 1 ], world.m[ 3 ][ 2 ] );
        const float headingRad = std::atan2( -world.m[ 2 ][ 0 ], world.m[ 2 ][ 2 ] );
        signature += object.Id + "@" + object.Ref + ":" +
                     std::to_string( recordPos.x ) + "," + std::to_string( recordPos.y ) + "," +
                     std::to_string( recordPos.z ) + "," + std::to_string( headingRad ) + ";";
        SpotRecord record;
        record.Id = GetIdentifier64bit( object.Id.c_str() );
        record.Bank = bInterior ? SceneInterior::DescribeRef( object.Ref ) : object.Ref;
        record.Position = recordPos;
        record.Heading = headingRad;
        std::string exterior;
        if ( bInterior ) {
            exterior = SceneInterior::ResolveBankPath( object.Ref );
        } else {
            exterior = gpFile->getBankFolder() + "islands/" + island + "/exterior/" + ToLower( object.Ref ) + ".bnk";
            if ( !gpFile->fileExists( exterior.c_str() ) ) {
                exterior.clear();
            }
        }
        if ( !exterior.empty() ) {
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
        modRoads.draw( renderer );
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
    // A fresh document imports the island's roads file (so the editor opens
    // on the existing roads; an explicit scene keeps its own).
    if ( doc.Roads.empty() ) {
        if ( importRoadsFile() > 0 ) {
            OTDU_LOG_INFO( "SceneEditor: imported the roads of '%s'\n", island.c_str() );
        }
    }
    markSceneChanged();
    const EdenWorld &world = view.getWorld();
    OTDU_LOG_INFO( "SceneEditor: %s ready: %d x %d areas grid, %zu areas, %zu sectors, weather '%s'\n", island.c_str(),
                   world.getAreaColumns(), world.getAreaRows(), world.getNumAreas(), world.getNumSectors(),
                   pWeather != nullptr ? pWeather->Name.c_str() : "(none)" );
    return true;
}

void GMSceneEditor::unloadIsland() {
    exitWalk(); // a walk across the island switch ends here (plain data: no GPU)
    if ( gpRender != nullptr ) {
        gpRender->getDevice().waitIdle();
    }
    sceneObjects.unload();
    modRoads.unload();
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
    if ( bWalkActive ) {
        return; // tickWalk owns the input while walking (F6 / Esc exit there)
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_TAB ) ) {
        bCaptured = !bCaptured;
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F2 ) ) {
        bShowPanel = !bShowPanel;
    }
    // Walk entry (M4): the selected / nearest interior placement, else the
    // scene grounds.
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F6 ) ) {
        enterWalk();
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

// The scene rooms' grass clearings: the footprints of every loaded interior
// bank (see refreshGrassClearings in the header).

void GMSceneEditor::refreshGrassClearings() {
    if ( !bWorldReady ) {
        return;
    }
    EdenWorld &world = view.getWorld();
    world.clearGrassClearings();
    for ( const auto &pExterior : exteriors.getExteriors() ) {
        if ( pExterior->getPath().find( "/interior/" ) == std::string::npos ) {
            continue;
        }
        // The bank's render bounds (bank frame) through every placement
        // frame: the footprint rect in world XZ.
        for ( const Matrix4x4f &frame : pExterior->getFrames() ) {
            const Vector3f &bmin = pExterior->getRenderBoundsMin();
            const Vector3f &bmax = pExterior->getRenderBoundsMax();
            Vector3f lo( 1e30f, 0.0f, 1e30f ), hi( -1e30f, 0.0f, -1e30f );
            for ( int32_t i = 0; i < 4; i++ ) {
                const Vector3f corner( ( i & 1 ) ? bmax.x : bmin.x, 0.0f, ( i & 2 ) ? bmax.z : bmin.z );
                const Vector3f world3 = frame.transformPoint( corner );
                lo = Vector3f( Min( lo.x, world3.x ), 0.0f, Min( lo.z, world3.z ) );
                hi = Vector3f( Max( hi.x, world3.x ), 0.0f, Max( hi.z, world3.z ) );
            }
            EdenWorld::GrassClearing clearing;
            clearing.MinX = lo.x;
            clearing.MinZ = lo.z;
            clearing.MaxX = hi.x;
            clearing.MaxZ = hi.z;
            world.addGrassClearing( clearing );
        }
    }
}


// --- walk (M4): first person inside a composed interior -----------------------

bool GMSceneEditor::enterWalk( const std::string &interiorId, const Vector3f &atBankLocal, float yawLocalDegrees ) {
    if ( !bWorldReady ) {
        walkNote = "the world is not ready";
        return false;
    }
    exitWalk();

    // The room: the explicit id, else the selected interior, else the nearest
    // interior placement. None: the scene grounds (terrain / road floors, no
    // walls).
    std::vector<Matrix4x4f> worlds;
    doc.resolveWorlds( worlds );
    int32_t targetIndex = -1;
    if ( !interiorId.empty() ) {
        const SceneObject *pObject = doc.findObject( interiorId );
        if ( pObject == nullptr || pObject->Kind != "interior" ) {
            walkNote = "not an interior object: " + interiorId;
            return false;
        }
        targetIndex = static_cast<int32_t>( pObject - doc.Objects.data() );
    } else if ( !selectedId.empty() ) {
        const SceneObject *pObject = doc.findObject( selectedId );
        if ( pObject != nullptr && pObject->Kind == "interior" ) {
            targetIndex = static_cast<int32_t>( pObject - doc.Objects.data() );
        }
    }
    if ( targetIndex < 0 ) {
        float bestDistance2 = FLT_MAX;
        for ( size_t i = 0; i < doc.Objects.size(); i++ ) {
            const SceneObject &object = doc.Objects[ i ];
            if ( object.Kind != "interior" ) {
                continue;
            }
            const float dx = worlds[ i ].m[ 3 ][ 0 ] - position.x;
            const float dz = worlds[ i ].m[ 3 ][ 2 ] - position.z;
            const float distance2 = dx * dx + dz * dz;
            if ( distance2 < bestDistance2 ) {
                bestDistance2 = distance2;
                targetIndex = static_cast<int32_t>( i );
            }
        }
    }

    const Quaternionf headingFromYaw = [ yawLocalDegrees, this ]() {
        if ( yawLocalDegrees != FLT_MAX ) {
            return Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yawLocalDegrees * kPi / 180.0f );
        }
        const Vector3f forward = getForward();
        return Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), std::atan2( forward.x, forward.z ) );
    }();

    if ( targetIndex < 0 ) {
        // The scene grounds walk: the fly camera's ground becomes the floor.
        Vector3f eye( position.x, 0.0f, position.z );
        float ground = 0.0f;
        if ( !view.getWorld().getSurfaceHeight( eye.x, eye.z, position.y + 1.0f, ground ) ) {
            walkNote = "no surface under the camera (walk over the island first)";
            return false;
        }
        eye.y = ground + kWalkEyeHeight;
        walkFrame = Matrix4x4f::Identity();
        walkCollision = HavokCollision();
        walkInteriorId.clear();
        walkController.setCollide( [ this ]( const Vector3f &from, const Vector3f &to ) {
            // Floor only (terrain / roads / scene roads): a step over
            // kWalkStepUp or a ledge beyond kWalkMaxDrop stays.
            float floor = 0.0f;
            if ( !view.getWorld().getSurfaceHeight( to.x, to.z, from.y + kWalkStepUp + 0.1f, floor ) ) {
                return from;
            }
            const float feet = from.y - kWalkEyeHeight;
            if ( floor > feet + kWalkStepUp || floor < feet - kWalkMaxDrop ) {
                return from;
            }
            return Vector3f( to.x, floor + kWalkEyeHeight, to.z );
        } );
        walkController.reset( eye, headingFromYaw );
        walkNote = "walking the scene grounds (no interior placed)";
        statusText = walkNote;
        OTDU_LOG_INFO( "SceneEditor: walk: the scene grounds at (%.1f, %.1f, %.1f), floor %.2f\n", eye.x, eye.y, eye.z,
                       ground );
    } else {
        const SceneObject &target = doc.Objects[ static_cast<size_t>( targetIndex ) ];
        const std::string bankPath = SceneInterior::ResolveBankPath( target.Ref );
        if ( bankPath.empty() ) {
            walkNote = "no interior bank for '" + target.Ref + "'";
            return false;
        }
        SceneInterior::WalkData walkData;
        if ( !SceneInterior::LoadWalkData( bankPath, walkData ) ) {
            walkNote = "the bank of '" + target.Ref + "' could not be read";
            return false;
        }
        walkCollision = std::move( walkData.Collision );
        walkInteriorId = target.Id;
        // The placement frame: the SpotExteriors law (T(position) R_y(heading))
        // - exactly the frames the room draws with, so the walk eye is in the
        // room the view renders.
        const Matrix4x4f &world = worlds[ static_cast<size_t>( targetIndex ) ];
        const Vector3f framePos( world.m[ 3 ][ 0 ], world.m[ 3 ][ 1 ], world.m[ 3 ][ 2 ] );
        const float headingRad = std::atan2( -world.m[ 2 ][ 0 ], world.m[ 2 ][ 2 ] );
        walkFrame = Matrix4x4f::Translation( framePos ) *
                    Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), headingRad ) );

        Vector3f eye = walkData.StartEye;
        Quaternionf heading = walkData.StartHeading;
        if ( !walkData.bHasStart ) {
            // No FirstPerson camera in the bank: the middle of the room
            // (the collision bounds centre), looking along the room yaw.
            eye = ( walkCollision.BoundsMin + walkCollision.BoundsMax ) * 0.5f + Vector3f( 0.0f, 1.8f, 0.0f );
            heading = headingFromYaw;
        }
        if ( atBankLocal.x != FLT_MAX ) {
            eye = atBankLocal;
            heading = headingFromYaw;
        }
        // The eye on the floor (the walk start law: ray down, eye = floor + 1.6;
        // off the floors the bank's camera height stands - walk_mode.cpp).
        float floorY = 0.0f;
        const bool bFloor = FindHavokFloor( walkCollision, eye, 0.1f, kWalkStartRay, floorY );
        if ( bFloor ) {
            eye.y = floorY + kWalkEyeHeight;
        }
        walkController.setCollide( [ this ]( const Vector3f &from, const Vector3f &to ) {
            // The WalkMode::collide law in the bank frame: the body sphere is
            // pushed out of the .shk walls, the feet follow the floor (step
            // up / drop), and off the room's floors the walk freezes (the
            // retail law - the .shk is a closed room).
            const HavokCollision &collision = walkCollision;
            if ( collision.Indices.empty() && collision.Boxes.empty() ) {
                return Vector3f( to.x, from.y, to.z );
            }
            Vector3f feet( to.x, from.y - kWalkEyeHeight, to.z );
            Vector3f body = feet + Vector3f( 0.0f, kWalkStepUp + kWalkRadius, 0.0f );
            body = ResolveSphereHavokCollision( collision, body, kWalkRadius );
            feet = Vector3f( body.x, feet.y, body.z );
            float floorValue = 0.0f;
            bool bFound = FindHavokFloor( collision, feet, kWalkStepUp, kWalkMaxDrop, floorValue );
            for ( int32_t i = 0; i < 8 && !bFound; i++ ) {
                // The floor under the body footprint (the body is not a point;
                // walk_mode.cpp's 8-ray ring over the floor seams).
                const float angle = i * ( 2.0f * kPi / 8.0f );
                float y = 0.0f;
                if ( FindHavokFloor( collision, feet + Vector3f( std::cos( angle ) * kWalkRadius, 0.0f,
                                                                 std::sin( angle ) * kWalkRadius ),
                                     kWalkStepUp, kWalkMaxDrop, y ) ) {
                    floorValue = y;
                    bFound = true;
                }
            }
            if ( !bFound ) {
                return from;
            }
            feet.y = floorValue;
            return feet + Vector3f( 0.0f, kWalkEyeHeight, 0.0f );
        } );
        walkController.reset( eye, heading );
        walkNote = "walking '" + target.Id + "' (" + SceneInterior::DescribeRef( target.Ref ) + ")" +
                   ( walkData.bHasStart ? ( " from " + walkData.StartCameraName ) : std::string() ) +
                   ( bFloor ? "" : " (no floor under the start)" );
        statusText = walkNote;
        OTDU_LOG_INFO( "SceneEditor: walk: '%s' bank %s, eye bank-local (%.2f, %.2f, %.2f), start camera '%s'%s\n",
                       target.Id.c_str(), bankPath.c_str(), eye.x, eye.y, eye.z, walkData.StartCameraName.c_str(),
                       bFloor ? "" : " (no floor under the start)" );
    }

    walkEyeWorld = walkFrame.transformPoint( walkController.getPosition() );
    walkForwardWorld = walkFrame.transformVector( walkController.getForward() ).normalized();
    setRelativeMouse( true );
    bWalkActive = true;
    return true;
}

void GMSceneEditor::exitWalk() {
    if ( !bWalkActive ) {
        return;
    }
    // The fly camera continues at the eye (looking where the walk looked).
    position = walkEyeWorld;
    const Vector3f forward = walkForwardWorld;
    if ( forward.length() > 1e-4f ) {
        yaw = std::atan2( forward.x, forward.z );
        pitch = std::asin( Clamp( forward.y / forward.length(), -1.0f, 1.0f ) );
    }
    bWalkActive = false;
    walkInteriorId.clear();
    walkCollision = HavokCollision();
    walkFrame = Matrix4x4f::Identity();
    setRelativeMouse( false );
    statusText = "walk ended";
    OTDU_LOG_INFO( "SceneEditor: walk ended (eye %.1f %.1f %.1f)\n", position.x, position.y, position.z );
}

void GMSceneEditor::tickWalk( float deltaTime ) {
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F2 ) ) {
        bShowPanel = !bShowPanel;
    }
    // Exit: Esc or F6 (B is the game's back key, but it is not bound in the
    // editor; Esc is IA_MenuBack anyway).
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_ESCAPE ) || gpInput->wasKeyPressed( SDL_SCANCODE_F6 ) ) {
        exitWalk();
        return;
    }
    const bool bUiKeys = gpImGui != nullptr && gpImGui->wantsKeyboard();
    walkController.update( deltaTime, !bUiKeys );
    walkEyeWorld = walkFrame.transformPoint( walkController.getPosition() );
    walkForwardWorld = walkFrame.transformVector( walkController.getForward() ).normalized();
}

void GMSceneEditor::streamWorld( bool bForce, const Vector3f &center ) {
    if ( !bWorldReady ) {
        return;
    }
    const int32_t key = SectorKey( center );
    if ( key == lastSectorKey && !bForce ) {
        return;
    }
    lastSectorKey = key;
    EdenWorld &world = view.getWorld();
    if ( world.unloadFar( center, sectorRadius + 1 ) != 0 ) {
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pRenderer->clearShadowCasterCache();
        }
    }
    world.loadAround( center, sectorRadius );
}

void GMSceneEditor::streamExteriors( const Vector3f &center ) {
    exteriors.tick( view, center, bExteriors );
    refreshGrassClearings();
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
        exitWalk(); // the walked bank belonged to the old island (if any)
        loadIsland( name );
    }
    if ( bWalkActive ) {
        tickWalk( deltaTime );
    } else {
        updateCamera( deltaTime );
    }
    if ( !bWorldReady ) {
        return;
    }
    // Streaming follows the camera: the walk eye when walking (the room and
    // its surroundings stay loaded), the fly position otherwise.
    const Vector3f streamCenter = bWalkActive ? walkEyeWorld : position;
    streamWorld( false, streamCenter );
    streamExteriors( streamCenter );

    // Scene edits re-instantiate on the next tick (a batch / server command
    // may have changed the document outside the UI flow).
    if ( bRebuildPending ) {
        bRebuildPending = false;
        sceneObjects.applyDocument( doc );
        refreshSpotRecords();
        if ( EdenRenderer *pRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr ) {
            modRoads.loadDocument( *pRenderer, doc, &view.getWorld() );
            modRoads.registerSurfaces( view.getWorld() ); // a scene road is a driveable surface
        }
        if ( bWalkActive && !walkInteriorId.empty() && doc.findObject( walkInteriorId ) == nullptr ) {
            exitWalk(); // the walked interior was deleted under the walker
        }
    }

    EdenSceneView::Camera camera;
    if ( bWalkActive ) {
        camera.Position = walkEyeWorld;
        camera.Rotation = ChaseCamera::LookRotation( walkEyeWorld, walkEyeWorld + walkForwardWorld );
        camera.Fov = walkFovDegrees * kPi / 180.0f;
    } else {
        camera.Position = position;
        camera.Rotation = ChaseCamera::LookRotation( position, position + getForward() );
        camera.Fov = fovDegrees * kPi / 180.0f;
    }
    view.setCamera( camera );
    view.setWeather( pWeather );
    view.setEnvironmentCenter( streamCenter, false );
    view.tick( deltaTime );
    view.getWorld().setNightLights( ( pWeather != nullptr && ( pWeather->Hour < 6 || pWeather->Hour >= 20 ) ) ||
                                    CmdLine::HasArg( "-lampday" ) );

    // --- picking / dragging (the ImGui overlay has the mouse over panels) ---
    const bool bMouseFree = ( gpImGui == nullptr || !gpImGui->wantsMouse() ) && !bWalkActive;
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


void GMSceneEditor::exportSpotsToGame() {
    const SceneExportResult result = SceneExportSpots( doc );
    if ( !result.Error.empty() ) {
        statusText = "export FAILED: " + result.Error;
        OTDU_LOG_WARN( "SceneEditor: %s\n", statusText.c_str() );
        return;
    }
    statusText = "exported: " + std::to_string( result.Appended ) + " appended, " +
                 std::to_string( result.BanksCopied ) + " bank(s) copied -> " + result.Path +
                 " (relaunch FREERIDE to see the scene streamed natively)";
    OTDU_LOG_INFO( "SceneEditor: %s\n", statusText.c_str() );
}

void GMSceneEditor::exportRoadsFile() {
    if ( gpFile == nullptr ) {
        return;
    }
    // {"roads": [...]} with the doc's road records (what ModRoads::load and
    // the other game modes read).
    JsonValue root = JsonValue::MakeObject();
    JsonValue list = JsonValue::MakeArray();
    for ( const SceneRoad &road : doc.Roads ) {
        JsonValue entry = JsonValue::MakeObject();
        entry.add( "id", JsonValue::MakeString( road.Id ) );
        JsonValue points = JsonValue::MakeArray();
        for ( const Vector3f &point : road.Points ) {
            JsonValue p = JsonValue::MakeArray();
            p.push( JsonValue::MakeNumber( point.x ) );
            p.push( JsonValue::MakeNumber( point.y ) );
            p.push( JsonValue::MakeNumber( point.z ) );
            points.push( std::move( p ) );
        }
        entry.add( "points", std::move( points ) );
        entry.add( "width", JsonValue::MakeNumber( road.Width ) );
        entry.add( "lanes", JsonValue::MakeNumber( road.Lanes ) );
        entry.add( "closed", JsonValue::MakeBool( road.bClosed ) );
        entry.add( "collide", JsonValue::MakeBool( road.bCollide ) );
        if ( !road.Texture.empty() ) {
            entry.add( "texture", JsonValue::MakeString( road.Texture ) );
        }
        list.push( std::move( entry ) );
    }
    root.add( "roads", std::move( list ) );
    const std::filesystem::path folder = gpFile->getModsRoot() / "roads";
    std::error_code ec;
    std::filesystem::create_directories( folder, ec );
    const std::filesystem::path file = folder / ( island + ".json" );
    FILE *pFile = fopen( file.string().c_str(), "wb" );
    if ( pFile == nullptr ) {
        statusText = "cannot write '" + file.string() + "'";
        return;
    }
    const std::string text = JsonWrite( root );
    const bool bOk = fwrite( text.data(), 1, text.size(), pFile ) == text.size();
    fclose( pFile );
    statusText = bOk ? ( std::to_string( doc.Roads.size() ) + " roads -> " + file.string() )
                     : "write failed";
    OTDU_LOG_INFO( "SceneEditor: %s\n", statusText.c_str() );
}

void GMSceneEditor::bakeRoadToProps( const std::string &roadId ) {
    const SceneRoad *pRoad = doc.findRoad( roadId );
    if ( pRoad == nullptr || gpFile == nullptr ) {
        statusText = "no such road: " + roadId;
        return;
    }
    std::vector<Vector3f> center;
    ModRoads::BuildCenterline( pRoad->Points, pRoad->bClosed, 2.0f, center );
    std::string objText, mtlText;
    const std::string texture = pRoad->Texture.empty() ? ModRoadsDefaultTexture() : pRoad->Texture;
    ModRoads::BuildRoadObjText( center, pRoad->Width, texture, objText, mtlText );
    const std::filesystem::path folder = gpFile->getModsRoot() / "props";
    std::error_code ec;
    std::filesystem::create_directories( folder, ec );
    const std::string base = "road_" + roadId;
    bool bOk = false;
    if ( FILE *pFile = fopen( ( folder / ( base + ".obj" ) ).string().c_str(), "wb" ) ) {
        bOk = fwrite( objText.data(), 1, objText.size(), pFile ) == objText.size();
        fclose( pFile );
    }
    if ( FILE *pFile = fopen( ( folder / ( base + ".mtl" ) ).string().c_str(), "wb" ) ) {
        const bool bMtlOk = fwrite( mtlText.data(), 1, mtlText.size(), pFile ) == mtlText.size();
        fclose( pFile );
        bOk = bOk && bMtlOk;
    }
    if ( !bOk ) {
        statusText = "bake write FAILED";
        return;
    }
    // The placement line: the ribbon is world space, so identity (the OBJ is
    // in the authoring space; the prop loader mirrors it back). "solid" gives
    // the car collision in the modes that draw props.
    const std::string line = island + " " + base + " 0 0 0 0 solid";
    ModProps::appendPlacement( island, base, Vector3f( 0.0f, 0.0f, 0.0f ), 0.0f, 1.0f, false, "TD_Gouraud" );
    statusText = "baked '" + base + "' -> props (see props.txt)";
    OTDU_LOG_INFO( "SceneEditor: %s\n", statusText.c_str() );
}

int32_t GMSceneEditor::importRoadsFile() {
    // <mods>/roads/<island>.json into the document (a fresh doc imports at
    // island load; load_roads re-imports).
    std::vector<uint8_t> bytes;
    const std::string path = std::string( "roads/" ) + island + ".json";
    if ( !gpFile->loadFile( path.c_str(), bytes ) ) {
        return -1;
    }
    std::string error;
    JsonValue root;
    if ( !JsonParse( std::string( bytes.begin(), bytes.end() ), root, error ) ) {
        OTDU_LOG_WARN( "SceneEditor: '%s': %s\n", path.c_str(), error.c_str() );
        return -1;
    }
    const JsonValue *pRoads = root.find( "roads" );
    if ( pRoads == nullptr && root.isArray() ) {
        pRoads = &root;
    }
    int32_t imported = 0;
    if ( pRoads != nullptr ) {
        for ( const JsonValue &entry : pRoads->array ) {
            SceneRoad road;
            road.Id = entry.getString( "id", "" );
            if ( const JsonValue *pPoints = entry.find( "points" ) ) {
                for ( const JsonValue &point : pPoints->array ) {
                    if ( point.isArray() && point.array.size() >= 3 ) {
                        road.Points.push_back( Vector3f( static_cast<float>( point.array[ 0 ].asNumber() ),
                                                         static_cast<float>( point.array[ 1 ].asNumber() ),
                                                         static_cast<float>( point.array[ 2 ].asNumber() ) ) );
                    }
                }
            }
            road.Width = static_cast<float>( entry.getNumber( "width", 7.0 ) );
            road.Lanes = static_cast<int32_t>( entry.getNumber( "lanes", 2.0 ) );
            road.bClosed = entry.getBool( "closed", false );
            road.bCollide = entry.getBool( "collide", true );
            road.Texture = entry.getString( "texture", "" );
            if ( road.Id.empty() || road.Points.size() < 2 || doc.findRoad( road.Id ) != nullptr ) {
                continue;
            }
            doc.Roads.push_back( std::move( road ) );
            imported++;
        }
    }
    return imported;
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
    // The interior banks (M4): the game's walkable rooms (icd_*, ias_*,
    // lph_*/gph_* house rooms...).
    for ( const std::string &stem : SceneInterior::ListBanks() ) {
        assets.push_back( { stem, "interior" } );
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
