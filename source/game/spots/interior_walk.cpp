#include "shared.h"
#include "interior_walk.h"

#include "filesystem/gs_file.h"
#include "flash/flash_player.h"
#include "game/clothes_shop.h"
#include "game/house_shop.h"
#include "game/spots/house_furniture.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "system/cmd_line.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace {
// Walk camera Fov: the house walk value (cam_entree_xx, AVATAR_CHOICE) [INF:
// the interior's own walk camera Fov is not read].
constexpr float kWalkFov = 1.0472f;

bool Contains( const std::string &text, const char *pPart ) { return text.find( pPart ) != std::string::npos; }
} // namespace

std::string InteriorWalk::InteriorBankFor( const std::string &spotBank ) {
    if ( spotBank.size() < 2 || ( spotBank[0] != 'E' && spotBank[0] != 'e' ) ) {
        return "";
    }
    // Asked every frame for the spots around the car: one file probe per name.
    static std::unordered_map<std::string, std::string> cache;
    auto it = cache.find( spotBank );
    if ( it != cache.end() ) {
        return it->second;
    }
    std::string name = spotBank.substr( 1 );
    std::transform( name.begin(), name.end(), name.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    std::string bank = "interior/i" + name + ".bnk";
    if ( gpFile == nullptr || !gpFile->fileExists( ( gpFile->getBankFolder() + bank ).c_str() ) ) {
        bank.clear();
    }
    cache[spotBank] = bank;
    return bank;
}

std::string InteriorWalk::HouseBankFor( const std::string &spotBank, bool bGarage ) {
    // The house levels: EPH_<level>[_<id>...] -> lph_<level> / gph_<level>
    // (EPH_L3DI_5067I -> lph_l3di, its spots.bin truncation EPH_L3DI too;
    // docs/re/pool_interior_spec.md 4.1) [DATA: the file names].
    static std::unordered_map<std::string, std::string> cache;
    const std::string key = spotBank + ( bGarage ? ":g" : ":l" );
    auto it = cache.find( key );
    if ( it != cache.end() ) {
        return it->second;
    }
    std::string bank;
    if ( spotBank.size() > 4 && _strnicmp( spotBank.c_str(), "EPH_", 4 ) == 0 ) {
        size_t end = spotBank.find( '_', 4 ); // the level ends at the next '_' (or the end)
        if ( end == std::string::npos ) {
            end = spotBank.size();
        }
        bank = std::string( bGarage ? "interior/gph_" : "interior/lph_" ) + spotBank.substr( 4, end - 4 ) + ".bnk";
        std::transform( bank.begin(), bank.end(), bank.begin(),
                        []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
        if ( gpFile == nullptr || !gpFile->fileExists( ( gpFile->getBankFolder() + bank ).c_str() ) ) {
            bank.clear();
        }
    }
    cache[key] = bank;
    return bank;
}

size_t InteriorWalk::placeDisplayCars( const std::vector<int32_t> &carIds ) {
    if ( !bActive || CmdLine::HasArg( "-nodisplaycars" ) ) {
        return 0;
    }
    // Slot i -> PosCams[i] [INF: the order pairing; both lists are the
    // shop's own, the placing code is ENC].
    const std::vector<std::string> &posCams = scene.getShopPosCams();
    size_t placed = 0;
    for ( size_t i = 0; i < carIds.size() && i < posCams.size(); i++ ) {
        CinematicCar::Desc desc;
        if ( posCams[i].empty() || !CinematicCar::DescFromDatabase( carIds[i], desc ) ) {
            continue;
        }
        // Paint: podium i shows the car's DBCarsAndColors row i (wrapped), so
        // the showroom shows the colour range [INF: the showroom's choice is
        // ENC; row 0 is the colour the car's .2dm already carries, so all
        // row 0 would look unpainted]. -paintindex <n>: one row for every car.
        const int32_t numPaints = Max( 1, CinematicCar::CountDatabasePaints( carIds[i] ) );
        const int32_t paintIndex = CmdLine::HasArg( "-paintindex" ) ? CmdLine::GetArgInt( "-paintindex", 0 )
                                                                     : static_cast<int32_t>( i ) % numPaints;
        if ( scene.addDisplayCar( desc, posCams[i], paintIndex ) ) {
            placed++;
            OTDU_LOG_INFO( "InteriorWalk:   car %d '%s' on %s\n", carIds[i], desc.FileName.c_str(), posCams[i].c_str() );
        } else {
            OTDU_LOG_WARN( "InteriorWalk: car %d '%s' could not be placed on %s\n", carIds[i], desc.FileName.c_str(),
                           posCams[i].c_str() );
        }
    }
    OTDU_LOG_INFO( "InteriorWalk: %zu display car(s) of %zu, %zu place(s) in the shop\n", placed, carIds.size(),
                   posCams.size() );
    return placed;
}

bool InteriorWalk::previewCar( int32_t carId, int32_t paintIndex ) {
    return bActive && scene.focusDisplayCar( carId, paintIndex );
}

void InteriorWalk::endPreview() {
    if ( bActive && state == eState::Walk ) {
        walk.resume( scene );
    }
}

int32_t InteriorWalk::findNearDisplayCar( float radius ) const {
    if ( !bActive ) {
        return -1;
    }
    const Vector3f position = walk.getPosition();
    float best = radius * radius;
    int32_t nearest = -1;
    for ( size_t i = 0; i < scene.getNumDisplayCars(); i++ ) {
        Vector3f carPosition;
        int32_t carId = -1;
        if ( !scene.getDisplayCarInfo( i, carPosition, carId ) ) {
            continue;
        }
        const float dx = carPosition.x - position.x;
        const float dz = carPosition.z - position.z;
        const float d = dx * dx + dz * dz;
        if ( d < best ) {
            best = d;
            nearest = static_cast<int32_t>( i );
        }
    }
    return nearest;
}

int32_t InteriorWalk::getDisplayCarCarId( int32_t index ) const {
    Vector3f position;
    int32_t carId = -1;
    return index >= 0 && scene.getDisplayCarInfo( static_cast<size_t>( index ), position, carId ) ? carId : -1;
}

void InteriorWalk::showMessage( const std::string &text ) {
    if ( bActive ) {
        scene.showTutorial( text );
    }
}

bool InteriorWalk::enter( const std::string &bank, const std::string &island ) {
    exit();
    const std::string path = InteriorBankFor( bank );
    if ( path.empty() ) {
        OTDU_LOG_WARN( "InteriorWalk: no interior bank for spot '%s'\n", bank.c_str() );
        return false;
    }
    return loadRoom( path, bank, false, nullptr, island );
}

bool InteriorWalk::enterHouse( const HouseRow &house, bool bToGarage, const std::string &island ) {
    exit();
    livingBank = HouseBankFor( house.SpotName, false );
    garageBank = HouseBankFor( house.SpotName, true );
    if ( livingBank.empty() || ( bToGarage && garageBank.empty() ) ) {
        OTDU_LOG_WARN( "InteriorWalk: no %s bank for house '%s'\n", bToGarage ? "garage" : "living",
                       house.SpotName.c_str() );
        return false;
    }
    bHouse = true;
    houseSpot = house.SpotName;
    if ( CmdLine::HasArg( "-logfurniture" ) ) {
        GetFurniture();
    }
    houseIsland = island;
    // The living interior is placed in the house frame (its nodes stand at
    // the house spot, y 31..36 m - pool_interior_spec.md 4.2); the garage is
    // its own space (x -23..14, y 0..4).
    // The living room streams its island around it by default (glm session
    // 71 - the owner's "when inside the exterior didn't seem to be loaded",
    // docs/re/houses_spec.md 5.1); loadRoom's -nointeriorworld restores the
    // closed-room A/B, and the garage stays its own space either way.
    const std::string roomIsland = bToGarage ? std::string() : island;
    if ( !loadRoom( bToGarage ? garageBank : livingBank, bToGarage ? std::string() : house.SpotName, bToGarage,
                    nullptr, roomIsland ) ) {
        bHouse = false;
        houseSpot.clear();
        houseIsland.clear();
        return false;
    }
    OTDU_LOG_INFO( "InteriorWalk: house '%s' -> %s room, island '%s'%s\n", house.SpotName.c_str(),
                   bInGarage ? "garage" : "living", roomIsland.c_str(),
                   bInGarage ? " (the garage is its own space: no world)" :
                               ( roomIsland.empty() ? " (closed room: no island)"
                                                    : ( CmdLine::HasArg( "-nointeriorworld" )
                                                            ? " (world streaming off: -nointeriorworld)"
                                                            : " (world streamed around the room)" ) ) );
    if ( bInGarage ) {
        placeGarageCars();
        applyTestAim();
    }
    return true;
}

bool InteriorWalk::loadRoom( const std::string &path, const std::string &houseSpotName, bool bGarageRoom,
                             const char *pWalkCamera, const std::string &island ) {
    walkCamera = pWalkCamera != nullptr ? pWalkCamera : std::string();
    // A closed room (the FIRST_WM garage setup): the house living room in
    // its house spot's frame; a shop in its spot's frame with the island
    // streamed around it - the view out of its windows (-nointeriorworld:
    // the room back at the island origin, no world). The house rooms keep
    // their verified closed-room frame.
    IntroCinematic::Setup setup;
    setup.Bank = path;
    setup.HouseSpot = houseSpotName;
    // The house rooms are the retail HOME.vmf's (living_in / garage_in, the
    // dressing, diary and garage pages; houses_spec.md 10); -nohomeui
    // keeps the generic INTRO walk screen (the A/B).
    bHomeUi = bHouse && !CmdLine::HasArg( "-nohomeui" );
    if ( bHomeUi ) {
        setup.UiMovie = "home";
    }
    setup.HouseIsland = island;
    setup.ExteriorBank.clear();
    setup.ExteriorPrefix.clear();
    setup.bWorld = false;
    setup.bInterior = true;
    setup.bStreamWorld = !island.empty() && !CmdLine::HasArg( "-nointeriorworld" );
    // The house living room in its island (the retail house presentation, the
    // AVATAR_CHOICE party house setup): the spot exterior bank draws the shell
    // around the room (the caravan body, the garage building, the plot) and
    // the island streams with the bWorld law (per-frame streaming, the ocean)
    // - the world out of the windows, and the yard is walkable to the Garage
    // volume (lph_l0c's sits 14 m out across the plot, outside the room's
    // .shk floors). The garage room keeps its own closed frame (it stands at
    // the island origin, not at the house spot).
    if ( bHouse && !bGarageRoom && !island.empty() && !CmdLine::HasArg( "-nointeriorworld" ) ) {
        const size_t slash = path.find_last_of( "/\\" );
        const size_t start = slash != std::string::npos ? slash + 1 : 0;
        // lph_l0c.bnk -> l0c (the level part of the house banks, HouseBankFor).
        const size_t dot = path.find( '.', start );
        std::string level = path.substr( start + 4, ( dot != std::string::npos ? dot : path.size() ) - start - 4 );
        std::transform( level.begin(), level.end(), level.begin(),
                        []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
        setup.ExteriorBank = "islands/" + island + "/exterior/eph_" + level + ".bnk";
        if ( gpFile == nullptr || !gpFile->fileExists( ( gpFile->getBankFolder() + setup.ExteriorBank ).c_str() ) ) {
            OTDU_LOG_WARN( "InteriorWalk: no exterior bank '%s' (the room keeps its bare frame)\n",
                           setup.ExteriorBank.c_str() );
            setup.ExteriorBank.clear();
        }
        setup.bWorld = true;
    }
    // The house's MyTDUHouse choices replace the authored furniture models.
    std::unordered_map<std::string, std::string> choices;
    if ( bHouse && !bGarageRoom ) {
        const std::string prefix = houseSpot + ":";
        for ( const auto &choice : GetPlayerProfile().FurnitureChoices ) {
            if ( choice.first.compare( 0, prefix.size(), prefix ) == 0 ) {
                choices[choice.first.substr( prefix.size() )] = choice.second;
            }
        }
    }
    scene.setFurnitureChoices( std::move( choices ) );
    furnitureSelectables.clear();
    furnitureEdit = FurnitureEdit();
    myHouseMode = false;
    scene.setCommandCallback(
        [this]( const std::string &command, const std::string &args ) { onUiCommand( command, args ); } );
    scene.setGeneralCommandCallback(
        [this]( const std::string &command, const std::string &args ) { onUiCommand( command, args ); } );
    if ( !scene.load( setup ) ) {
        OTDU_LOG_WARN( "InteriorWalk: '%s' could not be loaded\n", path.c_str() );
        scene.unload();
        return false;
    }
    spotBank = houseSpotName.empty() ? path : houseSpotName;
    bActive = true;
    if ( bHouse && !bGarageRoom ) {
        addFurnitureSelectables();
        // The saved walls / floors of this house.
        const PlayerProfile &profile = GetPlayerProfile();
        for ( int32_t type = 1; type <= 2; type++ ) {
            auto it = profile.FurnitureChoices.find( houseSpot + ":" + SurfaceKey( type ) );
            if ( it != profile.FurnitureChoices.end() && !it->second.empty() ) {
                scene.applyRoomMaterial( SurfacePrefix( type ), it->second );
            }
        }
    }
    bInGarage = bGarageRoom;
    bRoomSwapped = false; // the swap that loads this room sets it after loadRoom
    // The yard walk (the streamed living room): where the room's .shk has no
    // floor (outside the room), the island under the plot is the ground, and
    // the world's obstacles push the body - the sector statics around the spot
    // (fences, trunks, the camping ground; the roads are floor, not wall: the
    // sphere bottom rides kStepUp above the feet, so any surface within the
    // step height never touches it) and the exterior bank's own triangles (the
    // caravan shell, the garage building). The step / drop bounds are
    // WalkMode's own (walk_mode.cpp kStepUp / kMaxDrop).
    if ( setup.bWorld && !bGarageRoom && !island.empty() ) {
        exteriorCollision = HavokCollision();
        scene.collectExteriorCollisionTriangles( exteriorCollision.Vertices, exteriorCollision.Indices );
        worldCollision.clear();
        worldCollision.setIsland( island.c_str() );
        worldCollision.setHeightFunction( [this]( float x, float z, float fromY, float &outHeight ) {
            // The carve-aware ground (EdenWorld::getGroundHeight): the height
            // the drive physics stands on, tunnel / bridge bores included.
            return scene.getWorld().getGroundHeight( x, z, fromY, outHeight );
        } );
        const Vector3f spotPosition = scene.getSceneOrigin().getTranslation();
        worldCollision.streamAround( spotPosition, 1 );
        OTDU_LOG_INFO( "InteriorWalk: yard collision: %zu exterior triangle(s), %zu sector triangle(s) around (%.0f, %.0f)\n",
                       exteriorCollision.Indices.size() / 3, worldCollision.getNumTriangles(), spotPosition.x,
                       spotPosition.z );
        walk.setWorldHook( [this]( Vector3f &body, const Vector3f &bodyPrevious, float &outFloorY ) {
            const Matrix4x4f &origin = scene.getSceneOrigin();
            const Matrix4x4f originInverse = origin.inverse();
            Vector3f islandBody = origin.transformPoint( body );
            const Vector3f islandPrevious = origin.transformPoint( bodyPrevious );
            // The sector statics: the deepest contact per round, a few rounds
            // (the same resolve law the drive's chassis uses).
            for ( int i = 0; i < 4; i++ ) {
                VehicleWorldCollision::Contact contact;
                if ( !worldCollision.sphereContact( islandBody, 0.3f, contact, &islandPrevious ) ) {
                    break;
                }
                islandBody += contact.Normal * contact.Depth;
            }
            // The exterior bank's own walls (the caravan shell, the garage
            // building): the walk radius is WalkMode's kRadius.
            islandBody = ResolveSphereHavokCollision( exteriorCollision, islandBody, 0.3f, 4 );
            // The ground under the pushed body (the terrain, the roads, the
            // deck of the exterior bank); the rotation is about Y only, so
            // the height maps straight down.
            const float feetY = islandBody.y - 0.7f; // kStepUp + kRadius above the feet
            float height = 0.0f;
            const bool bGround = scene.getSurfaceHeight( islandBody.x, islandBody.z, feetY + 0.4f, height ) &&
                                 height >= feetY - 2.0f;
            body = originInverse.transformPoint( islandBody );
            if ( !bGround ) {
                return false;
            }
            outFloorY = height - origin.getTranslation().y;
            return true;
        } );
    } else {
        walk.setWorldHook( nullptr );
    }
    // The player avatar (the .sce actor "Avatar" of the shop cinematics).
    const PlayerProfile &profile = GetPlayerProfile();
    scene.setPlayerAvatar( profile.AvatarConfig.empty() ? "AVT_Male_Intro" : profile.AvatarConfig );

    // The shop's life: the looping cinematics (M_LIFE_HOSTESS, M_LIFE_MUSIC,
    // M_LOOP, M_INT_LOOP ...) run in the background; M_INTRO / M_INTRO_I is
    // the entry shot [INF: the names; the spot code that starts them is
    // ENC]. The house banks: C_LIFE / C_LIFE_VENTILO loop (C_MYLIFE is a
    // movie, not an idle); the entry shot is C_INTRO (C_FIRST_INTRO_I in the
    // studio bank).
    introName.clear();
    std::string names;
    for ( const std::string &name : scene.getCinematicNames() ) {
        names += " " + name;
        if ( bHouse ? name.rfind( "C_LIFE", 0 ) == 0 : ( Contains( name, "LIFE" ) || Contains( name, "LOOP" ) ) ) {
            scene.playBackground( name, true );
        } else if ( !bHouse && introName.empty() && Contains( name, "INTRO" ) ) {
            introName = name;
        }
    }
    if ( bHouse ) {
        for ( const std::string &name : scene.getCinematicNames() ) {
            if ( name == "C_INTRO" || name == "C_FIRST_INTRO_I" ) {
                introName = name;
                break;
            }
        }
        if ( introName.empty() ) {
            for ( const std::string &name : scene.getCinematicNames() ) {
                if ( Contains( name, "INTRO" ) ) {
                    introName = name;
                    break;
                }
            }
        }
    }
    const auto &selectables = scene.getSelectables();
    OTDU_LOG_INFO( "InteriorWalk: '%s' (%s): cinematics%s; start camera '%s'; %zu selectables\n", spotBank.c_str(),
                   path.c_str(), names.c_str(), scene.getFirstPersonCamera().c_str(), selectables.size() );
    for ( const IntroCinematic::Volume &volume : selectables ) {
        OTDU_LOG_INFO( "InteriorWalk:   selectable '%s' type %d '%s' at (%.2f, %.2f, %.2f)\n", volume.Name.c_str(),
                       volume.SelectableType, volume.SelectableString.c_str(), volume.Center.x, volume.Center.y,
                       volume.Center.z );
    }
    // The house entry shot plays ONCE per house and profile - the owner:
    // "everytime you enter it seems to replay the caravan scene". The key is the
    // room bank's name (lph_l0c / gph_l0c), so the houses sharing a bank share
    // the entry. Shops keep replaying theirs; -replayhouseintro forces one,
    // -nointeriorintro disables them all.
    bool bIntroSeen = false;
    std::string introKey;
    if ( bHouse && path.size() > 4 ) {
        const size_t slash = path.find_last_of( '/' );
        introKey = path.substr( slash == std::string::npos ? 0 : slash + 1 );
        introKey = introKey.substr( 0, introKey.size() - 4 ); // ".bnk"
        const std::vector<std::string> &seen = GetPlayerProfile().SeenHouseIntros;
        bIntroSeen = std::find( seen.begin(), seen.end(), introKey ) != seen.end();
        if ( bIntroSeen && !introName.empty() && !CmdLine::HasArg( "-replayhouseintro" ) ) {
            OTDU_LOG_INFO( "InteriorWalk: '%s': '%s' already played - straight to the walk "
                           "(-replayhouseintro replays it)\n",
                           introKey.c_str(), introName.c_str() );
            introName.clear();
        }
    }
    if ( !introName.empty() && !CmdLine::HasArg( "-nointeriorintro" ) && scene.playMain( introName ) ) {
        state = eState::Intro;
        if ( bHouse && !bIntroSeen && !introKey.empty() ) {
            PlayerProfile &profile = GetPlayerProfile();
            profile.SeenHouseIntros.push_back( introKey );
            SaveProfile( "house intro seen" );
        }
    } else {
        startWalk( walkCamera.c_str() );
    }
    return true;
}

bool InteriorWalk::tookRoomSwap() {
    const bool bSwapped = bRoomSwapped;
    bRoomSwapped = false;
    return bSwapped;
}

void InteriorWalk::placeGarageCars() {
    // The owned cars on the garage bank's Entity__ShopData slots (D_CAR_01..)
    // [INF: the pairing; the retail garage stores the cars itself].
    const PlayerProfile &profile = GetPlayerProfile();
    placeDisplayCars( profile.OwnedCars );
}

void InteriorWalk::exit() {
    if ( !bActive ) {
        return;
    }
    scene.setCameraOverride( nullptr );
    walk.setWorldHook( nullptr );
    worldCollision.clear();
    exteriorCollision = HavokCollision();
    scene.unload();
    bActive = false;
    bHouse = false;
    bInGarage = false;
    OTDU_LOG_INFO( "InteriorWalk: left '%s'\n", spotBank.c_str() );
    spotBank.clear();
    houseIsland.clear();
}

void InteriorWalk::startWalk( const char *pPreferredCamera ) {
    if ( walk.isInSelfView() ) {
        walk.endSelfView( scene ); // a room swap restarts the walk cleanly
    }
    bEmoteWasPlaying = false;
    scene.stopMain();
    // Entity__FirstPerson StartCamera; a bank without one starts at its
    // first walk / entry camera [INF]. A room swap names the door camera it
    // came through (the living room's cam_entree_garage).
    std::string camera = pPreferredCamera != nullptr ? pPreferredCamera : std::string();
    if ( camera.empty() ) {
        camera = scene.getFirstPersonCamera();
    } else {
        Vector3f position;
        Quaternionf heading;
        if ( !scene.getEntityCameraLocal( camera, position, heading ) ) {
            camera = scene.getFirstPersonCamera();
        }
    }
    if ( camera.empty() ) {
        for ( const char *pName : { "cam_walkmode", "cam_entree" } ) {
            Vector3f position;
            Quaternionf heading;
            if ( scene.getEntityCameraLocal( pName, position, heading ) ) {
                camera = pName;
                break;
            }
        }
    }
    if ( camera.empty() || !walk.start( scene, camera, kWalkFov ) ) {
        OTDU_LOG_WARN( "InteriorWalk: no walk start camera in '%s'\n", spotBank.c_str() );
    }
    // INTRO walk-mode screen: the reticle and the selection labels
    // (/:SelItemType "exit", "carseller" ...), as in the house walk. The
    // house rooms: HOME.vmf's living_in / garage_in.
    if ( bHomeUi ) {
        homePage.clear();
        scene.getUi().setVariableBool( "/:InGarage", bInGarage );
        scene.getUi().gotoLabel( "/", bInGarage ? "garage_in" : "living_in", true );
    } else {
        scene.getUi().gotoLabel( "/", "wm_home", true );
    }
    state = eState::Walk;
    applyTestAim();
}

void InteriorWalk::applyTestAim() {
    // -walkaim <selectable type / name>: face that selectable from 1.4 m (a scripted
    // run presses it without steering the reticle). The first walk only.
    static bool bAimed = false;
    if ( state != eState::Walk ) {
        return;
    }
    if ( const char *pAim = CmdLine::GetArgValue( "-walkaim" ) ) {
        // "car": next to the first display car (the garage pick, the dealer
        // spec card).
        Vector3f carPosition;
        int32_t carId = -1;
        if ( !bAimed && _stricmp( pAim, "car" ) == 0 && scene.getDisplayCarInfo( 0, carPosition, carId ) ) {
            bAimed = true;
            Vector3f away = walk.getPosition() - carPosition;
            away.y = 0.0f;
            away = away.normalized();
            Vector3f eye = carPosition + away * 2.2f;
            eye.y = walk.getPosition().y;
            walk.placeLooking( scene, eye, carPosition );
            OTDU_LOG_INFO( "InteriorWalk: -walkaim car: next to car %d\n", carId );
        }
        if ( !bAimed ) {
            for ( const IntroCinematic::Volume &volume : scene.getSelectables() ) {
                if ( _stricmp( volume.SelectableString.c_str(), pAim ) != 0 && _stricmp( volume.Name.c_str(), pAim ) != 0 ) {
                    continue;
                }
                bAimed = true;
                Vector3f away = walk.getPosition() - volume.Center;
                away.y = 0.0f;
                away = away.normalized();
                Vector3f eye = volume.Center + away * 1.4f;
                eye.y = walk.getPosition().y;
                walk.placeLooking( scene, eye, volume.Center );
                OTDU_LOG_INFO( "InteriorWalk: -walkaim '%s': looking at '%s'\n", pAim, volume.Name.c_str() );
                break;
            }
        }
    }
}

// The emotes (docs/re/emotes_spec.md), the same law as the FIRST_WM walk:
// X toggles the self view, 1..8 the slots, the view returns at the clip end;
// -emote <label> plays one on the walk start (headless tests).
void InteriorWalk::updateEmotes( float deltaTime ) {
    (void)deltaTime;
    // -emote <label>: play once on the walk start; before the self view gate
    // (it is what starts the view).
    static const char *pAutoEmote = CmdLine::GetArgValue( "-emote" );
    if ( pAutoEmote != nullptr && !bAutoEmotePlayed ) {
        bAutoEmotePlayed = true;
        const int32_t index = IntroCinematic::findPlayerEmote( pAutoEmote );
        if ( index >= 0 ) {
            if ( !scene.isPlayerEmoteActive() ) {
                walk.startSelfView( scene );
            }
            scene.playPlayerEmote( index );
        } else {
            OTDU_LOG_WARN( "EMOTE: -emote '%s' is not one of the slots\n", pAutoEmote );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_Emote ) ) {
        if ( walk.isInSelfView() ) {
            walk.endSelfView( scene );
        } else {
            walk.startSelfView( scene );
        }
    }
    if ( !walk.isInSelfView() ) {
        bEmoteWasPlaying = false;
        return;
    }
    const int32_t emoteCount = static_cast<int32_t>( IntroCinematic::getPlayerEmotes().size() );
    for ( int32_t i = 0; i < emoteCount && i < 9; i++ ) {
        if ( gpInput->wasKeyPressed( static_cast<SDL_Scancode>( SDL_SCANCODE_1 + i ) ) ) {
            scene.playPlayerEmote( i );
            break;
        }
    }
    const bool bPlaying = scene.isPlayerEmotePlaying();
    if ( bEmoteWasPlaying && !bPlaying ) {
        walk.endSelfView( scene );
    }
    bEmoteWasPlaying = bPlaying;
}

InteriorWalk::eResult InteriorWalk::tick( float deltaTime ) {
    if ( !bActive ) {
        return eResult::None;
    }
    // The press that skips the intro is not reused by the walk.
    bool bConsumed = false;
    if ( state == eState::Intro && ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ||
                                     gpInput->wasPressed( eInputAction::IA_MenuBack ) ) ) {
        scene.skipMain();
        bConsumed = true;
    }
    scene.tick( deltaTime );
    for ( const std::string &name : scene.takeEndEvents() ) {
        if ( state == eState::Intro && name == introName ) {
            startWalk( walkCamera.c_str() );
        }
    }
    if ( state != eState::Walk ) {
        return eResult::None;
    }
    if ( bDialogOpen ) {
        // GENERAL dialog_box key catchers: 13 -> A(), 8 -> B() [DATA].
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            scene.getGeneral().sendKey( 13 );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            scene.getGeneral().sendKey( 8 );
        }
        return eResult::None;
    }
    if ( !homePage.empty() ) {
        routeHomeInput();
        return eResult::None;
    }
    if ( pendingSurfaceEdit != 0 && myHouseMode ) {
        startSurfaceEdit( pendingSurfaceEdit );
        pendingSurfaceEdit = 0;
        return eResult::None;
    }
    if ( furnitureEdit.bActive ) {
        tickFurnitureEdit();
        return eResult::None;
    }
    walk.update( scene, deltaTime, true );
    updateEmotes( deltaTime );
    if ( bConsumed ) {
        return eResult::None;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        if ( walk.isInSelfView() ) {
            // B in the self view goes back to first person, not out of the room.
            walk.endSelfView( scene );
            return eResult::None;
        }
        if ( myHouseMode ) {
            // "Quit MyTDUHouse" (the header's B prompt [DATA]): the movie's own
            // back, not the way out of the house.
            FlashPlayer &ui = scene.getUi();
            if ( !ui.sendKey( 8 ) ) {
                ui.invokeMethod( "/:Clip", "B", {} );
            }
            OTDU_LOG_INFO( "InteriorWalk: MyTDUHouse quit\n" );
            return eResult::None;
        }
        return eResult::Leave;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        if ( walk.isInSelfView() ) {
            return eResult::None; // the selection is cleared in the self view
        }
        const char *pType = walk.getSelectedType( scene );
        OTDU_LOG_INFO( "InteriorWalk: validate on '%s'\n", pType );
        if ( myHouseMode && strcmp( pType, "furniture" ) == 0 ) {
            startFurnitureEdit( walk.getSelected() );
            return eResult::None;
        }
        if ( myHouseMode && *pType == '\0' ) {
            // Nothing under the reticle: the floor when looking down, else the
            // wall in front [INF: the retail wall / floor pick is ENC].
            startSurfaceEdit( walk.getForward().y < -0.45f ? 1 : 2 );
            return eResult::None;
        }
        if ( strcmp( pType, "exit" ) == 0 ) {
            return eResult::Leave;
        }
        if ( bHouse ) {
            // The house rooms: the Garage selectable of the living room loads
            // the garage (your cars on its slots), the Living selectable of
            // the garage comes back. The other volumes are not wired.
            if ( strcmp( pType, "garage" ) == 0 && !bInGarage ) {
                if ( garageBank.empty() ) {
                    showMessage( "This house has no garage." );
                } else if ( loadRoom( garageBank, std::string(), true ) ) {
                    placeGarageCars();
                    applyTestAim();
                    bRoomSwapped = true;
                }
                return eResult::None;
            }
            if ( strcmp( pType, "living" ) == 0 && bInGarage && !livingBank.empty() ) {
                // Back through the garage door: its camera (the FpSelectable's
                // CameraFPEntryPoint, cam_entree_garage in every living bank
                // checked [DATA]) instead of the front-door one. The island
                // comes along (the yard the camera stands in is streamed).
                if ( loadRoom( livingBank, houseSpot, false, "cam_entree_garage", houseIsland ) ) {
                    bRoomSwapped = true;
                }
                return eResult::None;
            }
            if ( bHomeUi && ( strcmp( pType, "diary" ) == 0 || strcmp( pType, "dressing" ) == 0 ||
                              strcmp( pType, "billboard" ) == 0 ) ) {
                openHomePage( pType );
                return eResult::None;
            }
            if ( strcmp( pType, "diary" ) == 0 || strcmp( pType, "dressing" ) == 0 ||
                 strcmp( pType, "td_media" ) == 0 || strcmp( pType, "billboard" ) == 0 ) {
                showMessage( "This is not wired yet." );
                return eResult::None;
            }
        }
        if ( *pType != '\0' ) {
            OTDU_LOG_INFO( "InteriorWalk: A on '%s'\n", pType );
            return eResult::Shop;
        }
    }
    return eResult::None;
}

void InteriorWalk::tickBackground( float deltaTime ) {
    if ( bActive ) {
        scene.tick( deltaTime );
    }
}

void InteriorWalk::renderOffscreen() {
    if ( bActive ) {
        scene.renderOffscreen();
    }
}

void InteriorWalk::draw() {
    if ( bActive ) {
        scene.draw();
    }
}

void InteriorWalk::onUiCommand( const std::string &command, const std::string &args ) {
    OTDU_LOG_INFO( "InteriorWalk: FSCommand %s %s (page '%s')\n", command.c_str(), args.c_str(), homePage.c_str() );
    if ( !bHomeUi ) {
        return;
    }
    FlashPlayer &ui = scene.getUi();
    PlayerProfile &profile = GetPlayerProfile();
    if ( command == "clin" ) {
        // The walk-in closet opened a category (HOME.vmf sets /:BtValid to
        // its key: "hats", "glasses", "tops" ... [DATA, read at run time])
        // and reads /:Nb_Items for its item strip (neit / prit page it, seit
        // picks; FSCommands of the dressing page [DATA]). The strip holds the
        // owned clothes of that body part for the avatar's sex [INF: the
        // retail closet content is ENC].
        const std::string key = ToLower( ui.getVariable( "/:BtValid" ) );
        closetPartType = 0;
        if ( key.find( "hat" ) != std::string::npos || key.find( "helmet" ) != std::string::npos ) {
            closetPartType = 5;
        } else if ( key.find( "glass" ) != std::string::npos ) {
            closetPartType = 2;
        } else if ( key.find( "top" ) != std::string::npos || key.find( "jacket" ) != std::string::npos ) {
            closetPartType = 7;
        } else if ( key.find( "bot" ) != std::string::npos || key.find( "pant" ) != std::string::npos ||
                    key.find( "trouser" ) != std::string::npos || key.find( "leg" ) != std::string::npos ) {
            closetPartType = 1;
        } else if ( key.find( "shoe" ) != std::string::npos ) {
            closetPartType = 6;
        } else if ( key.find( "glove" ) != std::string::npos ) {
            closetPartType = 4;
        }
        closetItems.clear();
        GetClothes(); // the DBClothe table (cached)
        const int32_t sex = PlayerClothesSex();
        // The free "No hat" / "Bare chest" row of the part first (the DEFAULT
        // brand, $0, M_NO_* files [DATA]): taking an item off.
        for ( const ClotheItem &item : GetClothes() ) {
            if ( item.PartType == closetPartType && item.Price == 0 && item.Brand.empty() &&
                 ( item.Sex == sex || sex == 0 ) && item.Name.compare( 0, 3, "No " ) == 0 ) {
                closetItems.push_back( item.Id );
                break;
            }
        }
        for ( const int32_t id : profile.OwnedClothes ) {
            const ClotheItem *pItem = FindClothe( id );
            if ( pItem != nullptr && pItem->PartType == closetPartType && ( pItem->Sex == sex || sex == 0 ) ) {
                closetItems.push_back( id );
            }
        }
        closetIndex = 0;
        auto worn = profile.WornClothes.find( closetPartType );
        for ( size_t i = 0; worn != profile.WornClothes.end() && i < closetItems.size(); i++ ) {
            if ( closetItems[i] == worn->second ) {
                closetIndex = static_cast<int32_t>( i );
            }
        }
        ui.setVariableNumber( "/:Nb_Items", static_cast<double>( closetItems.size() ) );
        updateClosetItem();
        OTDU_LOG_INFO( "InteriorWalk: closet '%s' -> _parttype %d, %zu owned item(s)\n", key.c_str(), closetPartType,
                       closetItems.size() );
    } else if ( command == "neit" || command == "prit" ) {
        const int32_t count = static_cast<int32_t>( closetItems.size() );
        if ( count > 0 ) {
            closetIndex = ( closetIndex + ( command == "neit" ? 1 : count - 1 ) ) % count;
        }
        updateClosetItem();
    } else if ( command == "seit" ) {
        if ( closetIndex >= 0 && closetIndex < static_cast<int32_t>( closetItems.size() ) ) {
            profile.WornClothes[closetPartType] = closetItems[static_cast<size_t>( closetIndex )];
            const ClotheItem *pItem = FindClothe( closetItems[static_cast<size_t>( closetIndex )] );
            OTDU_LOG_INFO( "InteriorWalk: wearing %d '%s' (_parttype %d)\n", closetItems[static_cast<size_t>( closetIndex )],
                           pItem != nullptr ? pItem->Name.c_str() : "?", closetPartType );
            SaveProfile( "closet" );
        }
    } else if ( command == "wmon" || command == "camf" || command == "avou" || command == "liin" ||
                command == "gain" ) {
        // The page hands the walk back (the diary's Quit: wmon + camf; the
        // closet's root Back: avou + liin) [DATA: the FSCommands seen at run
        // time].
        if ( !homePage.empty() ) {
            OTDU_LOG_INFO( "InteriorWalk: HOME page '%s' closed (%s)\n", homePage.c_str(), command.c_str() );
            homePage.clear();
            walk.resume( scene );
        }
    } else if ( command == "dbxo" ) {
        // FUN_00d80ba0 'dbxo' (gm_shop.cpp): the source movie's dialog texts
        // copied to GENERAL, GENERAL DialogBox() [CODE].
        FlashPlayer &general = scene.getGeneral();
        for ( const char *pVariable : { "/:Msg1Txt", "/:Msg2Txt", "/:Msg1Type", "/:Msg2Type", "/:YesNo" } ) {
            general.setVariable( pVariable, ui.getVariable( pVariable ) );
        }
        general.setVariable( "/:DialogBoxSource", "HOME" );
        scene.setDialogBoxVisible( true );
        bDialogOpen = true;
        general.invoke( "/", "DialogBox", {} );
    } else if ( command == "dbxc" ) {
        // 'dbxc': GENERAL /:PlayerAnswer back to the source, its DialogBox().
        FlashPlayer &general = scene.getGeneral();
        bDialogOpen = false;
        scene.setDialogBoxVisible( false );
        ui.setVariable( "/:PlayerAnswer", general.getVariable( "/:PlayerAnswer" ) );
        ui.invoke( "/", "DialogBox", {} );
        OTDU_LOG_INFO( "InteriorWalk: dialog answer '%s'\n", general.getVariable( "/:PlayerAnswer" ).c_str() );
    } else if ( command == "myhs:open" ) {
        // MyTDUHouse (the diary's furniture page): the walk with the
        // MyTDUHouse header [DATA]; the furniture choice itself is not wired.
        myHouseMode = true;
        // -surfaceedit floor|wall: straight into the floor / walls edit (a
        // scripted run, the reticle aside).
        if ( const char *pSurface = CmdLine::GetArgValue( "-surfaceedit" ) ) {
            pendingSurfaceEdit = _stricmp( pSurface, "floor" ) == 0 ? 1 : 2;
        }
        // -furnaim <slot>: face that furniture slot (a scripted run edits it).
        if ( const char *pSlot = CmdLine::GetArgValue( "-furnaim" ) ) {
            for ( const IntroCinematic::Volume &volume : scene.getSelectables() ) {
                if ( volume.SelectableString == "furniture" && _stricmp( volume.Name.c_str(), pSlot ) == 0 ) {
                    Vector3f away = walk.getPosition() - volume.Center;
                    away.y = 0.0f;
                    away = away.normalized();
                    Vector3f eye = volume.Center + away * 2.0f;
                    eye.y = walk.getPosition().y;
                    walk.placeLooking( scene, eye, volume.Center );
                    OTDU_LOG_INFO( "InteriorWalk: -furnaim: looking at '%s'\n", volume.Name.c_str() );
                    break;
                }
            }
        }
    } else if ( command.compare( 0, 5, "myhs:" ) == 0 && command != "myhs:open" ) {
        myHouseMode = false;
    }
}

void InteriorWalk::updateClosetItem() {
    // The strip's labels: /:ItemName (read by the closet root) and the brand
    // logo frame /:logo_Brand [DATA: the names; the logo frame names are not
    // decoded, left empty].
    FlashPlayer &ui = scene.getUi();
    const ClotheItem *pItem = closetIndex >= 0 && closetIndex < static_cast<int32_t>( closetItems.size() )
                                  ? FindClothe( closetItems[static_cast<size_t>( closetIndex )] )
                                  : nullptr;
    ui.setVariable( "/:ItemName", pItem != nullptr ? pItem->Name : std::string() );
    ui.setVariable( "/:logo_Brand", pItem != nullptr ? ToLower( pItem->Brand ) : std::string() );
}

void InteriorWalk::openHomePage( const std::string &type ) {
    // The FpSelectable types of the house banks (houses_spec.md 5) -> the
    // HOME.vmf pages [DATA: the root labels dressing_in, diary_in, garage_*].
    const char *pLabel = type == "dressing" ? "dressing_in" : ( type == "diary" ? "diary_in" : "garage_in" );
    homePage = type;
    walk.clearSelection( scene );
    scene.getUi().gotoLabel( "/", pLabel, true );
    OTDU_LOG_INFO( "InteriorWalk: HOME page '%s' (%s)\n", type.c_str(), pLabel );
}

void InteriorWalk::routeHomeInput() {
    // Pad / keyboard -> HOME.vmf, the SHOPSCAR routing (gm_shop.cpp
    // routeInput): the buttons' key conditions (13 validate, 14 / 15 up /
    // down, 1 / 2 left / right), B() of the focus clip /:Clip [DATA + INF].
    FlashPlayer &ui = scene.getUi();
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        if ( !ui.sendKey( 13 ) ) {
            ui.invokeMethod( "/:Clip", "A", {} );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        ui.invokeMethod( "/:Clip", "B", {} );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        ui.sendKey( 14 );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        ui.sendKey( 15 );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        ui.sendKey( 1 );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        ui.sendKey( 2 );
    }
}

void InteriorWalk::addFurnitureSelectables() {
    // Each Entity__MyHouseObject model a box selectable over its bounds (the
    // shop cars' law, gm_shop.cpp), "furniture" for the walk, live only in
    // the MyTDUHouse mode [INF: the retail furniture pick is ENC].
    furnitureSelectables.clear();
    for ( const IntroCinematic::FurnitureSlotInfo &slot : scene.getFurnitureSlots() ) {
        const Vector3f size = slot.BoundsMax - slot.BoundsMin;
        if ( size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f ) {
            continue;
        }
        const Vector3f center = ( slot.BoundsMin + slot.BoundsMax ) * 0.5f;
        const Matrix4x4f toUnit = Matrix4x4f::Scale( Vector3f( 1.0f / size.x, 1.0f / size.y, 1.0f / size.z ) ) *
                                  Matrix4x4f::Translation( center * -1.0f ) * slot.ModelToScene.inverse();
        IntroCinematic::Volume volume;
        volume.Name = slot.Slot;
        volume.OfType = 2;
        volume.SelectableType = 57;
        volume.SelectableString = "furniture";
        for ( int32_t r = 0; r < 4; r++ ) {
            for ( int32_t c = 0; c < 4; c++ ) {
                volume.Matrix[r][c] = toUnit.m[r][c];
            }
        }
        volume.Center = slot.ModelToScene.transformPoint( center );
        const int32_t index = scene.addSelectable( volume );
        if ( index >= 0 ) {
            furnitureSelectables[index] = slot.Slot;
        }
    }
    // The furniture is selectable in MyTDUHouse only, the room's own
    // selectables outside it.
    walk.setSelectableFilter( [this]( int32_t index ) {
        const bool bFurniture = furnitureSelectables.count( index ) != 0;
        return myHouseMode ? bFurniture : !bFurniture;
    } );
    OTDU_LOG_INFO( "InteriorWalk: %zu furniture selectable(s)\n", furnitureSelectables.size() );
}

const char *InteriorWalk::SurfacePrefix( int32_t type ) {
    // The room materials a finish replaces: lph_l3di's Mat_Floor1_09 /
    // Mat_Wall1_38 (pool_interior_spec.md 13: MAT_FLOOR1_, MAT_WALL1_) [DATA].
    return type == 1 ? "mat_floor" : "mat_wall";
}

const char *InteriorWalk::SurfaceKey( int32_t type ) { return type == 1 ? "#floor" : "#wall"; }

void InteriorWalk::startSurfaceEdit( int32_t type ) {
    furnitureEdit = FurnitureEdit();
    for ( const RoomMaterialItem *pItem : FindRoomMaterialsOfType( type ) ) {
        furnitureEdit.Banks.push_back( pItem->Bank );
    }
    if ( furnitureEdit.Banks.empty() ) {
        return;
    }
    // The current finish: the saved choice, else the authored one ("").
    const auto &choices = GetPlayerProfile().FurnitureChoices;
    auto it = choices.find( houseSpot + ":" + SurfaceKey( type ) );
    furnitureEdit.OriginalBank = it != choices.end() ? it->second : std::string();
    furnitureEdit.Banks.insert( furnitureEdit.Banks.begin(), std::string() ); // 1: the house's own
    furnitureEdit.Index = 0;
    for ( size_t i = 0; i < furnitureEdit.Banks.size(); i++ ) {
        if ( furnitureEdit.Banks[i] == furnitureEdit.OriginalBank ) {
            furnitureEdit.Index = static_cast<int32_t>( i );
        }
    }
    furnitureEdit.bActive = true;
    furnitureEdit.SurfaceType = type;
    walk.clearSelection( scene );
    OTDU_LOG_INFO( "InteriorWalk: MyTDUHouse: editing the %s ('%s'), %zu finish(es)\n", type == 1 ? "floor" : "walls",
                   furnitureEdit.OriginalBank.c_str(), furnitureEdit.Banks.size() - 1 );
    showFurnitureChoice();
}

void InteriorWalk::startFurnitureEdit( int32_t selectable ) {
    auto it = furnitureSelectables.find( selectable );
    if ( it == furnitureSelectables.end() ) {
        return;
    }
    const std::vector<IntroCinematic::FurnitureSlotInfo> slots = scene.getFurnitureSlots();
    const IntroCinematic::FurnitureSlotInfo *pSlot = nullptr;
    for ( const IntroCinematic::FurnitureSlotInfo &slot : slots ) {
        if ( slot.Slot == it->second ) {
            pSlot = &slot;
        }
    }
    if ( pSlot == nullptr ) {
        return;
    }
    furnitureEdit = FurnitureEdit();
    // The slot's choices: the DBFurniture rows of _type = slot Type + 1 [DATA:
    // ChaiSalD 1 <-> chaisal* 2, sofaL 8 <-> sofal_* 9, TabRecE 11 <-> tabrec* 12].
    for ( const FurnitureItem *pItem : FindFurnitureOfType( pSlot->Type + 1 ) ) {
        furnitureEdit.Banks.push_back( pItem->Bank );
    }
    if ( std::find( furnitureEdit.Banks.begin(), furnitureEdit.Banks.end(), pSlot->Bank ) == furnitureEdit.Banks.end() ) {
        furnitureEdit.Banks.insert( furnitureEdit.Banks.begin(), pSlot->Bank );
    }
    // A Group > 0 is a set (the six dining chairs: group 2, the bar stools:
    // group 3 [DATA]) that changes together [INF: the retail set rule is ENC].
    for ( const IntroCinematic::FurnitureSlotInfo &slot : slots ) {
        const bool bSame = slot.Slot == pSlot->Slot ||
                           ( pSlot->Group > 0 && slot.Group == pSlot->Group && slot.Type == pSlot->Type );
        if ( bSame ) {
            furnitureEdit.Slots.push_back( slot.Slot );
            furnitureEdit.Originals.push_back( { slot.Bank, slot.Color } );
        }
    }
    furnitureEdit.bActive = true;
    furnitureEdit.OriginalBank = pSlot->Bank;
    furnitureEdit.OriginalColor = pSlot->Color;
    furnitureEdit.Color = pSlot->Color;
    furnitureEdit.NumColors = pSlot->NumColors;
    for ( size_t i = 0; i < furnitureEdit.Banks.size(); i++ ) {
        if ( furnitureEdit.Banks[i] == pSlot->Bank ) {
            furnitureEdit.Index = static_cast<int32_t>( i );
        }
    }
    walk.clearSelection( scene );
    OTDU_LOG_INFO( "InteriorWalk: MyTDUHouse: editing '%s' (type %d, group %d, %s colour %d/%d), %zu model(s), %zu slot(s)\n",
                   pSlot->Slot.c_str(), pSlot->Type, pSlot->Group, pSlot->Bank.c_str(), pSlot->Color + 1, pSlot->NumColors,
                   furnitureEdit.Banks.size(), furnitureEdit.Slots.size() );
    showFurnitureChoice();
}

void InteriorWalk::showFurnitureChoice() {
    const std::string &bank = furnitureEdit.Banks[static_cast<size_t>( furnitureEdit.Index )];
    if ( furnitureEdit.SurfaceType != 0 ) {
        const RoomMaterialItem *pFinish = FindRoomMaterialByBank( bank );
        const bool bSame = bank == furnitureEdit.OriginalBank;
        std::string text = std::string( furnitureEdit.SurfaceType == 1 ? "Floor: " : "Walls: " ) +
                           ( bank.empty() ? std::string( "the house's own" ) : ( pFinish != nullptr ? pFinish->Name : bank ) ) +
                           "  (" + std::to_string( furnitureEdit.Index + 1 ) + "/" +
                           std::to_string( furnitureEdit.Banks.size() ) + ")";
        text += bSame ? "  - yours" : "  - $" + std::to_string( pFinish != nullptr ? pFinish->Price : 0 );
        text += "\nLeft / Right: change   A: " + std::string( bSame ? "keep" : "buy" ) + "   B: cancel";
        showMessage( text );
        return;
    }
    const FurnitureItem *pItem = FindFurnitureByBank( bank );
    const bool bSameModel = bank == furnitureEdit.OriginalBank;
    const int32_t pieces = static_cast<int32_t>( furnitureEdit.Slots.size() );
    std::string text = ( pItem != nullptr ? pItem->Name : bank ) + "  (" + std::to_string( furnitureEdit.Index + 1 ) + "/" +
                       std::to_string( furnitureEdit.Banks.size() ) + ")";
    if ( pieces > 1 ) {
        text += "  x" + std::to_string( pieces );
    }
    text += bSameModel ? "  - yours" : "  - $" + std::to_string( ( pItem != nullptr ? pItem->Price : 0 ) * pieces );
    if ( furnitureEdit.NumColors > 1 ) {
        text += "\nColour " + std::to_string( furnitureEdit.Color + 1 ) + "/" + std::to_string( furnitureEdit.NumColors );
    }
    text += "\nLeft / Right: model   Up / Down: colour   A: " +
            std::string( !bSameModel ? "buy" : ( furnitureEdit.Color != furnitureEdit.OriginalColor ? "apply" : "keep" ) ) +
            "   B: cancel";
    showMessage( text );
}

void InteriorWalk::tickSurfaceEdit() {
    const int32_t count = static_cast<int32_t>( furnitureEdit.Banks.size() );
    const char *pPrefix = SurfacePrefix( furnitureEdit.SurfaceType );
    int32_t step = 0;
    if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        step = 1;
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        step = count - 1;
    }
    if ( step != 0 ) {
        for ( int32_t tries = 0; tries < count; tries++ ) {
            furnitureEdit.Index = ( furnitureEdit.Index + step ) % count;
            if ( scene.applyRoomMaterial( pPrefix, furnitureEdit.Banks[static_cast<size_t>( furnitureEdit.Index )] ) >= 0 ) {
                break;
            }
        }
        showFurnitureChoice();
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        scene.applyRoomMaterial( pPrefix, furnitureEdit.OriginalBank );
        furnitureEdit = FurnitureEdit();
        scene.closeTutorial();
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        const std::string bank = furnitureEdit.Banks[static_cast<size_t>( furnitureEdit.Index )];
        if ( bank != furnitureEdit.OriginalBank ) {
            // The row's _price [INF: the retail prices / _level gate are ENC];
            // back to the house's own finish is free.
            const RoomMaterialItem *pFinish = FindRoomMaterialByBank( bank );
            const int32_t price = pFinish != nullptr ? pFinish->Price : 0;
            PlayerProfile &profile = GetPlayerProfile();
            if ( profile.Money < price ) {
                showMessage( "Not enough money ($" + std::to_string( price ) + ")." );
                return;
            }
            profile.Money -= price;
            profile.FurnitureChoices[houseSpot + ":" + SurfaceKey( furnitureEdit.SurfaceType )] = bank;
            SaveProfile( "MyTDUHouse walls / floor" );
            OTDU_LOG_INFO( "InteriorWalk: MyTDUHouse: %s -> '%s' for $%d (money %d)\n",
                           furnitureEdit.SurfaceType == 1 ? "floor" : "walls", bank.c_str(), price, profile.Money );
            showMessage( bank.empty() ? std::string( "Back to the house's own." )
                                      : ( pFinish != nullptr ? pFinish->Name : bank ) + " bought." );
        } else {
            scene.closeTutorial();
        }
        furnitureEdit = FurnitureEdit();
    }
}

void InteriorWalk::tickFurnitureEdit() {
    if ( furnitureEdit.SurfaceType != 0 ) {
        tickSurfaceEdit();
        return;
    }
    const int32_t count = static_cast<int32_t>( furnitureEdit.Banks.size() );
    int32_t step = 0;
    if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        step = 1;
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        step = count - 1;
    }
    if ( step != 0 ) {
        // A bank that does not load is skipped (a DBFurniture row may name a
        // model the folder does not ship).
        for ( int32_t tries = 0; tries < count; tries++ ) {
            furnitureEdit.Index = ( furnitureEdit.Index + step ) % count;
            const std::string &bank = furnitureEdit.Banks[static_cast<size_t>( furnitureEdit.Index )];
            bool bLoaded = true;
            for ( const std::string &slot : furnitureEdit.Slots ) {
                bLoaded = scene.replaceFurniture( slot, bank ) && bLoaded;
            }
            if ( bLoaded ) {
                break;
            }
        }
        // The new model's colour count (the colour stays when it has it).
        for ( const IntroCinematic::FurnitureSlotInfo &slot : scene.getFurnitureSlots() ) {
            if ( slot.Slot == furnitureEdit.Slots.front() ) {
                furnitureEdit.NumColors = slot.NumColors;
                furnitureEdit.Color = slot.Color;
            }
        }
        showFurnitureChoice();
    }
    if ( furnitureEdit.NumColors > 1 &&
         ( gpInput->wasPressed( eInputAction::IA_MenuUp ) || gpInput->wasPressed( eInputAction::IA_MenuDown ) ) ) {
        const int32_t colors = furnitureEdit.NumColors;
        furnitureEdit.Color =
            ( furnitureEdit.Color + ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ? 1 : colors - 1 ) ) % colors;
        for ( const std::string &slot : furnitureEdit.Slots ) {
            scene.setFurnitureColor( slot, furnitureEdit.Color );
        }
        OTDU_LOG_INFO( "InteriorWalk: MyTDUHouse: colour %d/%d\n", furnitureEdit.Color + 1, colors );
        showFurnitureChoice();
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        for ( size_t i = 0; i < furnitureEdit.Slots.size(); i++ ) {
            scene.replaceFurniture( furnitureEdit.Slots[i], furnitureEdit.Originals[i].first );
            scene.setFurnitureColor( furnitureEdit.Slots[i], furnitureEdit.Originals[i].second );
        }
        furnitureEdit = FurnitureEdit();
        scene.closeTutorial();
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        const std::string &bank = furnitureEdit.Banks[static_cast<size_t>( furnitureEdit.Index )];
        const bool bNewModel = bank != furnitureEdit.OriginalBank;
        const bool bNewColor = furnitureEdit.Color != furnitureEdit.OriginalColor;
        PlayerProfile &profile = GetPlayerProfile();
        const FurnitureItem *pItem = FindFurnitureByBank( bank );
        if ( bNewModel || bNewColor ) {
            // A new model costs its row's _price per piece; a colour of a
            // model you have is free [INF: the retail MyTDUHouse prices and
            // the _level gate are ENC].
            const int32_t price = bNewModel ? ( pItem != nullptr ? pItem->Price : 0 ) *
                                                  static_cast<int32_t>( furnitureEdit.Slots.size() )
                                            : 0;
            if ( profile.Money < price ) {
                showMessage( "Not enough money ($" + std::to_string( price ) + ")." );
                return;
            }
            profile.Money -= price;
            for ( const std::string &slot : furnitureEdit.Slots ) {
                profile.FurnitureChoices[houseSpot + ":" + slot] = bank + "#" + std::to_string( furnitureEdit.Color );
            }
            SaveProfile( "MyTDUHouse furniture" );
            OTDU_LOG_INFO( "InteriorWalk: MyTDUHouse: %zu slot(s) -> %s colour %d for $%d (money %d)\n",
                           furnitureEdit.Slots.size(), bank.c_str(), furnitureEdit.Color + 1, price, profile.Money );
            showMessage( ( pItem != nullptr ? pItem->Name : bank ) + ( bNewModel ? " bought." : ": new colour." ) );
        } else {
            scene.closeTutorial();
        }
        furnitureEdit = FurnitureEdit();
    }
}
