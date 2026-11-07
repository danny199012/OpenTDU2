#include "shared.h"
#include "gm_cin_viewer.h"

#include "audio/gs_audio.h"
#include "core/xmb.h"
#include "database/gs_database.h"
#include "debug/gs_imgui.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"
#include <imgui.h>

StoryHandoff &GetStoryHandoff() {
    static StoryHandoff handoff;
    return handoff;
}

VehicleSceneState &GetVehicleSceneState() {
    static VehicleSceneState state;
    return state;
}

namespace {
// The white flash that closes a story cutscene before the drive (owner: "should
// flash white and then you on a quest") [INF: duration].
constexpr float kStoryEndFade = 1.0f;

uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) {
    return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 );
}
} // namespace

CinViewerRequest &GetCinViewerRequest() {
    static CinViewerRequest request;
    return request;
}

GMCinViewer::GMCinViewer() : GameMode( eGameMode::GM_CinViewer ) {}

GMCinViewer::~GMCinViewer() {}

bool GMCinViewer::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    request = GetCinViewerRequest();
    if ( !request.bFromStory ) {
        if ( const char *pBank = CmdLine::GetArgValue( "-cinbank" ) ) {
            request.Bank = pBank;
        }
        if ( const char *pName = CmdLine::GetArgValue( "-cin" ) ) {
            request.Cinematic = pName;
        }
        request.Next = eGameMode::GM_None; // the viewer ends on the last pose
    }
    // The tool layer: the shot timeline / camera overlay, the camera-path
    // debug draw (-cinepaths), the pause / scrub (the camera key pauses, the
    // menu left / right step while paused) and the browser (drawUI).
    // A scene the game or the story requested (the car wash, the dream) is not
    // the tool: no browser, no timeline over it unless -cinehud asks.
    bTool = !request.bFromStory;
    bCineHud = CmdLine::GetArgInt( "-cinehud", bTool ? 1 : 0 ) != 0;
    bCinePaths = CmdLine::HasArg( "-cinepaths" );
    bPaused = false;
    snprintf( bankEdit, sizeof( bankEdit ), "%s", request.Bank.c_str() );
    snprintf( cinEdit, sizeof( cinEdit ), "%s", request.Cinematic.c_str() );
    if ( CmdLine::HasArg( "-logentities" ) ) {
        // The entities of the bank: names and placements (scene frame).
        Bank bank;
        XmbDocument document;
        if ( bank.load( ( gpFile->getBankFolder() + request.Bank ).c_str() ) ) {
            const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
            if ( pEntities != nullptr && document.load( pEntities->pData, pEntities->Size ) ) {
                XmbValue root = document.getRoot()["ExportType_Entity"];
                for ( uint32_t i = 0; i < root.getArraySize( "ArrayOf_Entity__Cinematic" ); i++ ) {
                    XmbValue row = root.getArrayElement( "ArrayOf_Entity__Cinematic", i );
                    OTDU_LOG_INFO( "ENTITY cinematic '%s' Znear %.3f Zfar %.1f CinEnd %lld\n", row["__Name"].asString().c_str(),
                                   row["Znear"].asFloat( -1.0f ), row["Zfar"].asFloat( -1.0f ),
                                   static_cast<long long>( row["CinEnd"].asInt() ) );
                    // StreamCenter / Micro / CinCamera: entity hashcodes (__Hc).
                    const uint64_t center = row["StreamCenter"].asUInt64();
                    std::string centerName = "?";
                    for ( const char *pArray : { "ArrayOf_Entity", "ArrayOf_Entity__PNJ", "ArrayOf_Entity__Actor", "ArrayOf_Entity__PosCam" } ) {
                        for ( uint32_t j = 0; j < root.getArraySize( pArray ); j++ ) {
                            if ( root.getArrayElement( pArray, j )["__Hc"].asUInt64() == center ) {
                                centerName = root.getArrayElement( pArray, j )["__Name"].asString();
                            }
                        }
                    }
                    XmbValue placement = row["orient_pos_scale"]["pos"];
                    OTDU_LOG_INFO( "ENTITY   StreamCenter %016llx ('%s') Micro %016llx CinCamera %016llx pos (%.2f, %.2f, %.2f)\n",
                                   static_cast<unsigned long long>( center ), centerName.c_str(),
                                   static_cast<unsigned long long>( row["Micro"].asUInt64() ),
                                   static_cast<unsigned long long>( row["CinCamera"].asUInt64() ), placement["X"].asFloat(),
                                   placement["Y"].asFloat(), placement["Z"].asFloat() );
                }
                for ( const char *pArray : { "ArrayOf_Entity", "ArrayOf_Entity__PNJ", "ArrayOf_Entity__Actor", "ArrayOf_Entity__PosCam" } ) {
                    for ( uint32_t i = 0; i < root.getArraySize( pArray ); i++ ) {
                        XmbValue row = root.getArrayElement( pArray, i );
                        XmbValue pos = row["orient_pos_scale"]["pos"];
                        OTDU_LOG_INFO( "ENTITY %s '%s' config '%s' at (%.2f, %.2f, %.2f)\n", pArray, row["__Name"].asString().c_str(),
                                       row["ConfigName"].asString().c_str(), pos["X"].asFloat(), pos["Y"].asFloat(), pos["Z"].asFloat() );
                    }
                }
            }
        }
    }
    if ( CmdLine::HasArg( "-logshots" ) ) {
        // The shot records of every .cin of the bank: cinematic time -> scene time.
        Bank bank;
        if ( bank.load( ( gpFile->getBankFolder() + request.Bank ).c_str() ) ) {
            for ( const BankEntry &entry : bank.getEntries() ) {
                Cinematic cin;
                if ( entry.Extension != "cin" || !cin.parse( entry.pData, entry.Size ) ) {
                    continue;
                }
                OTDU_LOG_INFO( "CIN %s: %.2f s, %zu shots\n", entry.Name.c_str(), cin.getDuration(), cin.getShots().size() );
                for ( const Cinematic::Shot &shot : cin.getShots() ) {
                    OTDU_LOG_INFO( "  shot start %.2f dur %.2f scene %u camera %08x scene time %.2f..%.2f\n", shot.Start, shot.Duration,
                                   shot.Scene, shot.CameraHash, shot.CameraStart, shot.CameraEnd );
                }
            }
        }
    }
    if ( CmdLine::HasArg( "-logscene" ) ) {
        // The objects of every .sce of the bank: actors / cameras and their tracks.
        Bank bank;
        if ( bank.load( ( gpFile->getBankFolder() + request.Bank ).c_str() ) ) {
            for ( const BankEntry &entry : bank.getEntries() ) {
                if ( entry.Extension != "sce" ) {
                    continue;
                }
                CinematicScene scene;
                if ( !scene.parseScene( entry.pData, entry.Size ) ) {
                    continue;
                }
                OTDU_LOG_INFO( "SCENE %s: %.1f s, %zu objects, %zu refs\n", entry.Name.c_str(), scene.getDuration(), scene.getObjects().size(),
                               scene.getReferences().size() );
                {
                    const Matrix4x4f &o = scene.getOrigin();
                    OTDU_LOG_INFO( "  origin cols: x(%.3f %.3f %.3f) y(%.3f %.3f %.3f) z(%.3f %.3f %.3f) t(%.2f %.2f %.2f)\n", o.m[0][0], o.m[0][1],
                                   o.m[0][2], o.m[1][0], o.m[1][1], o.m[1][2], o.m[2][0], o.m[2][1], o.m[2][2], o.m[3][0], o.m[3][1], o.m[3][2] );
                }
                for ( const CinematicScene::Object &object : scene.getObjects() ) {
                    const Vector3f p = object.samplePosition( 0.0f );
                    OTDU_LOG_INFO( "  %s '%s' %zu tracks, pos0 (%.2f, %.2f, %.2f)\n", object.isCamera() ? "camera" : "actor", object.Name.c_str(),
                                   object.Tracks.size(), p.x, p.y, p.z );
                    for ( const CinematicScene::Track &track : object.Tracks ) {
                        std::string keys;
                        for ( size_t k = 0; k < track.Times.size() && k < 16; k++ ) {
                            char text[64];
                            uint32_t ref = 0;
                            memcpy( &ref, &track.Values[k * track.NumComponents], 4 );
                            snprintf( text, sizeof( text ), " %.2f:%u", track.Times[k],
                                      track.NumComponents >= 10 ? ref
                                      : track.NumComponents == 1 ? static_cast<uint32_t>( track.Values[k] )
                                                                 : 0u );
                            keys += text;
                            if ( track.Id == CinematicScene::kTrackMessage && ref < scene.getReferences().size() ) {
                                keys += "'" + scene.getReferences()[ref].Name + "'";
                            }
                        }
                        OTDU_LOG_INFO( "    track %08x %u comps %zu keys%s\n", track.Id, track.NumComponents, track.Times.size(), keys.c_str() );
                    }
                }
                for ( const CinematicScene::Reference &ref : scene.getReferences() ) {
                    OTDU_LOG_INFO( "  ref type %04x '%s'\n", ref.Type, ref.Name.c_str() );
                }
            }
        }
    }
    IntroCinematic::Setup setup;
    setup.Bank = request.Bank;
    if ( CmdLine::HasArg( "-loghouses" ) && gpDatabase != nullptr ) {
        XmbValue dbRoot = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < dbRoot.getArraySize( "DBHouse" ); i++ ) {
            XmbValue row = dbRoot.getArrayElement( "DBHouse", i );
            const char *pName = gpDatabase->getString( static_cast<int32_t>( row["_housename_hc"].asUInt64() ) );
            OTDU_LOG_INFO( "HOUSE spot '%s' name '%s' level %d buy %d slots %d\n", row["_spotname"].asString().c_str(), pName != nullptr ? pName : "",
                           static_cast<int>( row["_level"].asInt() ), static_cast<int>( row["_buyprice"].asInt() ),
                           static_cast<int>( row["_nbcarslots"].asInt() ) );
        }
    }
    if ( request.Cinematic.rfind( "C_MEETING_TESS", 0 ) == 0 ) {
        // The dream plays in front of the E_HOTELA exterior (the seafront hotel:
        // arcade, flags, semicircular fountains - the owner's 360 recomp grabs,
        // 2026-10-29). Frame heading 0: the recomp's camera headings equal
        // 180 - the port's .sce camera headings for three shots (the 360 frame
        // is the port's turned 180 deg about X, no extra turn) [DATA + INF]. The
        // position puts D_CAR on the drive in front of the entrance arches, left
        // of the central fountain (checked from above with -cintopview) [INF];
        // the height is snapped to the ground under the car.
        setup.HousePickX = 11099.0f;
        setup.HousePickZ = 17233.0f;
        setup.ExteriorPrefix = "e_hotela";
        setup.ExteriorBank = "islands/ibiza/exterior/e_hotela.bnk";
        setup.bSceneAt = true;
        setup.SceneAtPosition = Vector3f( 11103.0f, 4.6f, 17252.0f );
        setup.SceneAtHeading = 0.0f;
    }
    if ( const char *pSpot = CmdLine::GetArgValue( "-cinspot" ) ) {
        float x = 0.0f, z = 0.0f;
        if ( sscanf( pSpot, "%f,%f", &x, &z ) == 2 ) {
            setup.HousePickX = x; // the spot nearest to a position instead of the story house
            setup.HousePickZ = z;
        }
    }
    if ( const char *pHeading = CmdLine::GetArgValue( "-cinheading" ) ) {
        setup.HouseHeadingOffset = static_cast<float>( atof( pHeading ) ); // radians added to the spot heading
    }
    // -cinisland <ibiza|hawai|casino>: the spot search, the streamed world and
    // the -cinexterior folder follow that island (the casino trips play on the
    // casino island and at the Ibiza / Hawai seaplane docks).
    const char *pCinIsland = CmdLine::GetArgValue( "-cinisland" );
    if ( pCinIsland != nullptr ) {
        setup.HouseIsland = pCinIsland;
    }
    if ( const char *pExterior = CmdLine::GetArgValue( "-cinexterior" ) ) {
        setup.ExteriorPrefix = pExterior; // e.g. e_hotela: islands/ibiza/exterior/<prefix>.bnk
        setup.ExteriorBank = std::string( "islands/" ) + ( pCinIsland != nullptr ? pCinIsland : "ibiza" ) + "/exterior/" +
                             pExterior + ".bnk";
    }
    // A mode's own request (the car wash, docs/re/car_wash_spec.md): the spot
    // frame by position, the island's exterior and the player's car — after the
    // command-line block, so the request wins (bFromStory already ignores
    // -cinbank / -cin; these three applied late for the same reason).
    if ( !std::isnan( request.PickX ) && !std::isnan( request.PickZ ) ) {
        setup.HousePickX = request.PickX;
        setup.HousePickZ = request.PickZ;
    }
    if ( !request.Island.empty() ) {
        setup.HouseIsland = request.Island;
        if ( !request.ExteriorPrefix.empty() ) {
            setup.ExteriorPrefix = request.ExteriorPrefix;
            setup.ExteriorBank = "islands/" + request.Island + "/exterior/" + request.ExteriorPrefix + ".bnk";
        }
    }
    if ( const char *pAt = CmdLine::GetArgValue( "-cinsceneat" ) ) {
        // The cinematic frame alone (x,y,z,heading radians): the spot keeps the exterior.
        float x = 0.0f, y = 0.0f, z = 0.0f, h = 0.0f;
        if ( sscanf( pAt, "%f,%f,%f,%f", &x, &y, &z, &h ) == 4 ) {
            setup.bSceneAt = true;
            setup.SceneAtPosition = Vector3f( x, y, z );
            setup.SceneAtHeading = h;
        }
    }
    if ( CmdLine::HasArg( "-cinnoworld" ) ) {
        setup.HouseSpot.clear();
        setup.ExteriorBank.clear();
        setup.ExteriorPrefix.clear();
        setup.bWorld = false;
    }
    bLoaded = cinematic.load( setup );
    if ( !bLoaded ) {
        OTDU_LOG_WARN( "CINVIEWER: '%s' could not be loaded\n", request.Bank.c_str() );
        return true;
    }
    // The avatar the player has (the same config every mode draws); a profile
    // without one gets the default for this scene only, the profile is not
    // rewritten by a viewer [INF].
    PlayerProfile &profile = GetPlayerProfile();
    std::string avatarConfig = profile.AvatarConfig;
    if ( avatarConfig.empty() ) {
        const char *pAvatar = CmdLine::GetArgValue( "-avatar" );
        avatarConfig = pAvatar != nullptr ? pAvatar : "AVT_Male_Intro";
    }
    // The scene car rests at the bank's D_CAR PosCam: the player's current car
    // when the request says so (the car wash), else the bank default (the
    // California, car 387). The player's owned paint follows [INF: retail
    // repaints the scene car the same way - C_WASHING shows the driven car].
    cinematic.loadCar( request.bPlayerCar ? request.Car : CinematicCar::Desc(), "D_CAR", "CIN_CAR", request.CarActor );
    if ( request.bPlayerCar ) {
        const PlayerProfile &profile = GetPlayerProfile();
        auto it = profile.OwnedCarPaints.find( cinematic.getCar().getIdCar() );
        if ( it != profile.OwnedCarPaints.end() ) {
            const std::string name = cinematic.getCar().applyDatabasePaint( it->second );
            OTDU_LOG_INFO( "CINVIEWER: scene car '%s' in its ordered paint %d '%s'\n", request.Car.FileName.c_str(),
                           it->second, name.c_str() );
        }
    }
    if ( request.bPlayerCar ) {
        // The driven car as it entered: roof, windows, the body's mud / dust /
        // wetness (VehicleSceneState, written by GMFreeRide::exit).
        const VehicleSceneState &state = GetVehicleSceneState();
        CinematicCar &sceneCar = cinematic.getCar();
        if ( state.bSet && state.CarFile == request.Car.FileName && sceneCar.isLoaded() ) {
            sceneCar.setHardtopOpen( state.bHardtopOpen );
            sceneCar.updateHardtop( 1000.0f ); // the end pose at once, no 17 s roof run
            sceneCar.setWindowsOpen( state.bWindowsOpen );
            sceneCar.updateWindows( 1000.0f );
            sceneCar.applySurfaceState( state.Mud, state.Dirt, state.Hardness, state.Wetness );
            OTDU_LOG_INFO( "CINVIEWER: scene car '%s' as driven: roof %s, windows %s, mud %.2f dirt %.2f wet %.2f\n",
                           state.CarFile.c_str(), state.bHardtopOpen ? "open" : "closed",
                           state.bWindowsOpen ? "open" : "closed", state.Mud, state.Dirt, state.Wetness );
        }
        if ( state.bSet && !state.AvatarConfig.empty() ) {
            avatarConfig = state.AvatarConfig;
        }
    }
    cinematic.setPlayerAvatar( avatarConfig );
    if ( setup.bSceneAt ) {
        cinematic.snapSceneToGround();
    }
    // The courtyard bushes of the streamed world stand where the dream scene puts its
    // cameras; the retail scene shows none there [INF: 14 m around the car].
    {
        Matrix4x4f carWorld;
        if ( cinematic.getCarWorld( carWorld ) && !CmdLine::HasArg( "-cinbushes" ) ) {
            cinematic.setInstanceHide( carWorld.getTranslation(), 14.0f );
            float ground = 0.0f;
            const Vector3f car = carWorld.getTranslation();
            for ( const float dx : { 0.0f, 3.0f, -3.0f } ) {
                if ( cinematic.getSurfaceHeight( car.x + dx, car.z, car.y + 5.0f, ground ) ) {
                    OTDU_LOG_INFO( "CINVIEWER: car at (%.2f, %.2f, %.2f), ground %.2f below it (dx %.0f)\n", car.x, car.y, car.z, ground, dx );
                }
            }
        }
    }
    if ( !cinematic.playMain( request.Cinematic, true,
                              CmdLine::HasArg( "-cinetime" )
                                  ? static_cast<float>( CmdLine::GetArgInt( "-cinetime", 0 ) )
                                  : -1.0f ) ) {
        OTDU_LOG_WARN( "CINVIEWER: cinematic '%s' not found in '%s'\n", request.Cinematic.c_str(), request.Bank.c_str() );
    }
    OTDU_LOG_INFO( "CINVIEWER: playing '%s' of '%s'\n", request.Cinematic.c_str(), request.Bank.c_str() );
    return true;
}

void GMCinViewer::exit() {
    cinematic.unload();
    bLoaded = false;
}

void GMCinViewer::tick( float deltaTime ) {
    if ( !bLoaded ) {
        pGameInstance->setNextGameMode( eGameMode::GM_Login );
        return;
    }
    // The fee prompt (the car wash): validate pays and the wash goes on, back
    // leaves without paying [INF: the retail prompt is not readable; the owner:
    // "it automatically drives you to a spot where then it should ask if you
    // want to pay"].
    bool bLeave = false;
    if ( bPrompt ) {
        PlayerProfile &profile = GetPlayerProfile();
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) && profile.Money >= request.Price ) {
            profile.Money -= request.Price;
            profile.BeenInCarWash = true; // the retail SCPhoneCallsSave +4 flag
            // The wash cleans the body: the mud / dust / crust reset, dripping
            // wet (CarDirt::wash), on the scene car and for the drive after.
            VehicleSceneState &state = GetVehicleSceneState();
            state.Mud = state.Dirt = state.Hardness = 0.0f;
            state.Wetness = 1.0f;
            cinematic.getCar().applySurfaceState( 0.0f, 0.0f, 0.0f, 1.0f );
            bPrompt = false;
            OTDU_LOG_INFO( "CINVIEWER: paid $%d (money %d)\n", request.Price, profile.Money );
            if ( !request.Then.empty() ) {
                const std::string next = request.Then.front();
                request.Then.erase( request.Then.begin() );
                playNext( next );
            } else {
                bLeave = true;
            }
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            bPrompt = false;
            request.Then.clear();
            bLeave = true;
            OTDU_LOG_INFO( "CINVIEWER: the fee refused, leaving\n" );
        }
    }
    // ESC / validate skips the cinematic (CinProps skippable) [INF].
    if ( !bEnded && !bPrompt && !bLeave && ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) ) {
        cinematic.skipMain();
        bPaused = false;
    }
    // The camera key pauses / resumes (headless: press=camera); the menu
    // left / right step half a second while paused.
    if ( !bEnded && gpInput->wasPressed( eInputAction::IA_CarCamera ) ) {
        bPaused = !bPaused;
        OTDU_LOG_INFO( "CINVIEWER: %s at %.2f s\n", bPaused ? "paused" : "resumed",
                       cinematic.getMainTime() );
    }
    if ( bPaused && cinematic.getMainCinematic() != nullptr ) {
        const float duration = cinematic.getMainCinematic()->getDuration();
        if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            cinematic.seekMain( Min( cinematic.getMainTime() + 0.5f, duration - 0.01f ) );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            cinematic.seekMain( Max( cinematic.getMainTime() - 0.5f, 0.0f ) );
        }
    }
    // -cintopview (debug): a camera 35 m straight above the car, +Z up on the
    // screen, to see where the scene sits in the world.
    static const bool bTopView = CmdLine::HasArg( "-cintopview" );
    Matrix4x4f topCar;
    if ( bTopView && cinematic.getCarWorld( topCar ) ) {
        EdenSceneView::Camera top = cinematic.getCamera();
        top.Position = topCar.getTranslation() + Vector3f( 0.0f, 35.0f, 0.0f );
        // pitch -90 deg about X (looking down), camera forward = R(conj q) (0, 0, 1)
        top.Rotation = Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), -1.5707963f );
        top.Fov = 1.0f;
        cinematic.setCameraOverride( &top );
    }
    if ( !bPaused ) {
        time += deltaTime;
        cinematic.tick( deltaTime );
    }
    for ( const IntroCinematic::ActorMessage &message : cinematic.takeActorMessages() ) {
        // avm4 seats the avatar at the driver seat of the scene car, another
        // avm gives it back to its tracks (as GMCine) [INF: the handler is ENC].
        if ( message.Actor == "Avatar" && message.Message.compare( 0, 3, "avm" ) == 0 ) {
            cinematic.setPlayerSeated( message.Message.compare( 0, 4, "avm4" ) == 0 );
        }
    }
    if ( bInCar ) {
        updateInCarCamera();
    }
    static const bool bLogGround = CmdLine::HasArg( "-logground" );
    static float groundTimer = 0.0f;
    groundTimer += deltaTime;
    if ( bLogGround && groundTimer >= 1.0f ) {
        groundTimer = 0.0f;
        cinematic.logGroundContacts( time );
    }
    if ( CmdLine::HasArg( "-logcin" ) ) {
        static float logTimer = 0.0f;
        logTimer += deltaTime;
        if ( logTimer >= 1.0f ) {
            logTimer = 0.0f;
            const EdenSceneView::Camera &camera = cinematic.getCamera();
            // Heading like the recomp's [scene] lines: degrees from +Z towards
            // +X of the camera forward (R(conj q) (0, 0, 1)).
            const Quaternionf conj( -camera.Rotation.x, -camera.Rotation.y, -camera.Rotation.z, camera.Rotation.w );
            const Vector3f forward = conj.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
            OTDU_LOG_INFO( "CINVIEWER: t %.1f main '%s' %.1f camera (%.1f, %.1f, %.1f) heading %.1f fov %.2f\n", time,
                           cinematic.getMainName().c_str(), cinematic.getMainTime(), camera.Position.x, camera.Position.y, camera.Position.z,
                           std::atan2( forward.x, forward.z ) * 57.29578f, camera.Fov );
        }
    }
    auto finish = [this]() {
        if ( request.StoryStepAfter >= 0 ) {
            GetPlayerProfile().StoryStep = request.StoryStepAfter;
            SaveProfile( "cutscene end" );
        }
        if ( !request.After.empty() ) {
            // The next scene of the chain (its own bank / island).
            CinViewerRequest next = request.After.front();
            next.After.insert( next.After.end(), request.After.begin() + 1, request.After.end() );
            OTDU_LOG_INFO( "CINVIEWER: '%s' done -> '%s' of '%s'\n", request.Cinematic.c_str(), next.Cinematic.c_str(),
                           next.Bank.c_str() );
            GetCinViewerRequest() = next;
            pGameInstance->setNextGameMode( eGameMode::GM_CinViewer );
            return;
        }
        if ( request.Next != eGameMode::GM_None ) {
            pGameInstance->setNextGameMode( request.Next );
        }
    };
    std::vector<std::string> ended = cinematic.takeEndEvents();
    if ( bInCar && cinematic.getMainCinematic() != nullptr &&
         cinematic.getMainTime() >= cinematic.getMainCinematic()->getDuration() - 0.02f ) {
        ended.push_back( request.Cinematic ); // a looping C_INCAR ends after one pass
    }
    if ( bLeave ) {
        ended.push_back( request.Cinematic );
    }
    for ( const std::string &name : ended ) {
        if ( bPrompt && name == request.Cinematic ) {
            cinematic.playMain( request.Cinematic, true, 0.0f ); // the loop behind the prompt
            continue;
        }
        if ( name == request.Cinematic && !bEnded && !bLeave && !request.PromptAfter.empty() &&
             name == request.PromptAfter ) {
            // The fee is asked here, the loop shot behind it.
            request.PromptAfter.clear();
            bPrompt = true;
            OTDU_LOG_INFO( "CINVIEWER: end of '%s' after %.1f s -> the fee prompt ($%d) over '%s'\n", name.c_str(),
                           time, request.Price, request.PromptLoop.c_str() );
            if ( !request.PromptLoop.empty() ) {
                request.Cinematic = request.PromptLoop;
                cinematic.playMain( request.PromptLoop, true, 0.0f );
            }
            continue;
        }
        if ( name == request.Cinematic && !bEnded && !bLeave && !request.Then.empty() ) {
            // The next timeline of the sequence, in the same scene frame.
            const std::string next = request.Then.front();
            request.Then.erase( request.Then.begin() );
            OTDU_LOG_INFO( "CINVIEWER: end of '%s' after %.1f s -> '%s'\n", name.c_str(), time, next.c_str() );
            playNext( next );
            continue;
        }
        if ( name == request.Cinematic && !bEnded ) {
            if ( bInCar ) {
                bInCar = false;
                cinematic.stopBackground();
                cinematic.setCameraOverride( nullptr );
            }
            bEnded = true;
            OTDU_LOG_INFO( "CINVIEWER: end of '%s' after %.1f s\n", name.c_str(), time );
            if ( request.bFromStory ) {
                // The drive starts where the cinematic left the car.
                StoryHandoff &handoff = GetStoryHandoff();
                Matrix4x4f carWorld;
                handoff = StoryHandoff();
                // Only the scene that hands over to the drive places the car
                // (a chain's earlier scenes, or a scene ending in another
                // mode - the casino - leave the drive alone).
                const bool bToDrive = request.After.empty() && request.Next == eGameMode::GM_FreeRide;
                if ( bToDrive && !request.Island.empty() ) {
                    handoff.Island = request.Island;
                }
                if ( bToDrive && cinematic.getCarWorld( carWorld ) ) {
                    const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
                    handoff.bSet = true;
                    handoff.Position = carWorld.getTranslation();
                    handoff.Yaw = std::atan2( forward.x, forward.z );
                    handoff.bStory = request.bStory;
                }
                handoff.bWhiteIn = bToDrive;
                endFade = 0.0f;
            } else {
                finish();
            }
        }
    }
    if ( endFade >= 0.0f ) {
        endFade += deltaTime;
        if ( endFade >= kStoryEndFade && endFade - deltaTime < kStoryEndFade ) {
            finish();
        }
    }
}

void GMCinViewer::playNext( const std::string &next ) {
    if ( bInCar && next != request.InCar ) {
        // Out of the in-car part: the girls' loops stop, the shots own the camera.
        bInCar = false;
        cinematic.stopBackground();
        cinematic.setCameraOverride( nullptr );
    }
    request.Cinematic = next;
    if ( !cinematic.playMain( next, true, 0.0f ) ) {
        OTDU_LOG_WARN( "CINVIEWER: cinematic '%s' not found in '%s'\n", next.c_str(), request.Bank.c_str() );
    }
    if ( !request.InCar.empty() && next == request.InCar ) {
        startInCar();
    }
}

void GMCinViewer::startInCar() {
    // C_INCAR is NO_CAMERA: the wash seen from the driver's seat, in game
    // (the owner: "it kinda goes in game in car view there should be girl in
    // front with hose and the other 2 girls either side of the car") [DATA:
    // the shot camera NO_CAMERA; the C_BABE_0x loops are authored around the
    // car - their scene origins (0.28, -18.34), (-0.63, -17.58), (0.28, -16.68)
    // beside PLAYER_CAR (1.47, -17.51)].
    bInCar = true;
    for ( const std::string &background : request.InCarBackgrounds ) {
        if ( !cinematic.playBackground( background, true ) ) {
            OTDU_LOG_WARN( "CINVIEWER: in-car loop '%s' not found\n", background.c_str() );
        }
    }
    cinematic.setPlayerSeated( true ); // the avatar drives (its head is not drawn from the eye)
    loadCockpit();
    updateInCarCamera();
    OTDU_LOG_INFO( "CINVIEWER: in-car part '%s' with %zu loop(s), cockpit camera %s\n", request.InCar.c_str(),
                   request.InCarBackgrounds.size(), bCockpit ? "of the car" : "(seat fallback)" );
}

void GMCinViewer::loadCockpit() {
    // database/camerasingame.xmb "Car n" of the car's DBCarPhysicsData
    // _camera (else its _id_car), the camera of mode 23 = the cockpit
    // (GMShop::loadCockpitCamera, FIRST_WM) [DATA].
    bCockpit = false;
    if ( gpDatabase == nullptr ) {
        return;
    }
    const std::string carFile = request.bPlayerCar ? request.Car.FileName : std::string( "California" );
    int32_t cameraId = 0;
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBCarPhysicsData", i );
        if ( StringCmpI( row["_car_file_name"].asString().c_str(), carFile.c_str() ) == 0 ) {
            cameraId = static_cast<int32_t>( row["_camera"].asInt() );
            if ( cameraId == 0 ) {
                cameraId = static_cast<int32_t>( row["_id_car"].asInt() );
            }
            break;
        }
    }
    XmbDocument document;
    std::vector<uint8_t> data;
    if ( cameraId == 0 ||
         !gpFile->loadFile( ( gpFile->getBankFolder() + "database/camerasingame.xmb" ).c_str(), data ) ||
         !document.load( data ) ) {
        return;
    }
    XmbValue cameras = document.getRoot()["CameraInGame"];
    for ( int32_t c = 0; c < 400 && !bCockpit; c++ ) {
        XmbValue entry = cameras[( "Car " + std::to_string( c ) ).c_str()];
        if ( !entry.isValid() || entry["id_car"].asInt() != cameraId ) {
            continue;
        }
        const int32_t count = static_cast<int32_t>( entry["nb_cameras"].asInt() );
        for ( int32_t k = 0; k < count; k++ ) {
            XmbValue camera = entry[( "Camera " + std::to_string( k ) ).c_str()];
            if ( !camera.isValid() || camera["mode"].asInt() != 23 ) {
                continue;
            }
            cockpitPosition = Vector3f( camera["fixed_pos_x"].asFloat(), camera["fixed_pos_y"].asFloat(),
                                        camera["fixed_pos_z"].asFloat() );
            cockpitTarget = Vector3f( camera["fixed_targ_x"].asFloat(), camera["fixed_targ_y"].asFloat(),
                                      camera["fixed_targ_z"].asFloat() );
            cockpitFov = camera["fov"].asFloat( 0.96f );
            cockpitNear = camera["nearProjPlane"].asFloat( 0.05f );
            bCockpit = true;
            break;
        }
    }
}

void GMCinViewer::updateInCarCamera() {
    Matrix4x4f carWorld;
    if ( !bCockpit || !cinematic.getCarWorld( carWorld ) ) {
        return; // the last shot camera stays
    }
    const Vector3f eye = carWorld.transformPoint( cockpitPosition );
    Vector3f direction = carWorld.transformPoint( cockpitTarget ) - eye;
    const float length = std::sqrt( direction.x * direction.x + direction.y * direction.y + direction.z * direction.z );
    if ( length < 1e-4f ) {
        return;
    }
    direction = direction * ( 1.0f / length );
    // Eden convention: forward = R(q*) (0, 0, 1), q* = Ry(yaw) Rx(-pitch)
    // (GMShop::updateCockpitCamera).
    const float yaw = std::atan2( direction.x, direction.z );
    const float pitch = std::asin( Clamp( direction.y, -1.0f, 1.0f ) );
    const Quaternionf conjugate = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw ) *
                                  Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), -pitch );
    EdenSceneView::Camera camera = cinematic.getCamera();
    camera.Position = eye;
    camera.Rotation = Quaternionf( -conjugate.x, -conjugate.y, -conjugate.z, conjugate.w );
    camera.Fov = cockpitFov;
    camera.Near = cockpitNear;
    cinematic.setCameraOverride( &camera );
}

void GMCinViewer::drawPrompt() {
    // The fee box over the loop shot [INF: layout; the retail wash prompt is
    // not readable and no localized string asks it - loc 3913 is the hint].
    const Font *pFont = gpFlash != nullptr ? gpFlash->getTitleFont() : nullptr;
    if ( pFont == nullptr ) {
        return;
    }
    const PlayerProfile &profile = GetPlayerProfile();
    const bool bCanPay = profile.Money >= request.Price;
    char title[96];
    char line[128];
    snprintf( title, sizeof( title ), "Car wash - $%d", request.Price );
    snprintf( line, sizeof( line ), bCanPay ? "Validate: pay and wash      Back: leave"
                                            : "You cannot pay the wash      Back: leave" );
    Renderer2D &renderer = gpRender->get2DRenderer();
    renderer.begin();
    renderer.drawRect( Rect2D( 390.0f, 560.0f, 500.0f, 110.0f ), 0xc0000000u );
    renderer.drawText( pFont, title, 640.0f, 575.0f, 30.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
    renderer.drawText( pFont, line, 640.0f, 625.0f, 20.0f, Rgba( 220, 220, 220 ), eTextAlign::TA_Center );
    renderer.end();
}

void GMCinViewer::drawOffscreen() {
    if ( bLoaded ) {
        cinematic.renderOffscreen();
    }
}

void GMCinViewer::draw() {
    if ( bLoaded ) {
        cinematic.draw();
    }
    if ( bLoaded && bPrompt ) {
        drawPrompt();
    }
    if ( endFade >= 0.0f ) {
        const uint32_t alpha = static_cast<uint32_t>( Clamp( endFade / kStoryEndFade, 0.0f, 1.0f ) * 255.0f );
        Renderer2D &renderer = gpRender->get2DRenderer();
        renderer.begin();
        renderer.drawRect( Rect2D( -10000.0f, -10000.0f, 20000.0f, 20000.0f ), 0x00ffffffu | ( alpha << 24 ) );
        renderer.end();
    }
    if ( bLoaded && bCinePaths ) {
        // The camera / actor paths and the scene origin axes over the view.
        cinematic.drawDebugPaths( gpRender->get2DRenderer() );
    }
    if ( bLoaded && bCineHud ) {
        drawHud();
    }
}

// The CINEHUD: the shot timeline with the cut points and the Fx ticks
// (amber = voices), the active camera readout, the pause badge.
void GMCinViewer::drawHud() {
    const Cinematic *pCin = cinematic.getMainCinematic();
    if ( pCin == nullptr || pCin->getDuration() <= 0.0f ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    if ( pFont == nullptr ) {
        return;
    }
    const float duration = pCin->getDuration();
    const float t = cinematic.getMainTime();
    const float barX = 240.0f, barY = 36.0f, barW = 800.0f, barH = 10.0f;
    Renderer2D &renderer = gpRender->get2DRenderer();
    renderer.begin();
    renderer.drawRect( Rect2D( barX - 2.0f, barY - 2.0f, barW + 4.0f, barH + 4.0f ), Rgba( 0, 0, 0, 170 ) );
    renderer.drawRect( Rect2D( barX, barY, barW * Clamp( t / duration, 0.0f, 1.0f ), barH ), Rgba( 80, 220, 255 ) );
    for ( const Cinematic::Shot &shot : pCin->getShots() ) {
        const float x = barX + barW * Clamp( shot.Start / duration, 0.0f, 1.0f );
        renderer.drawRect( Rect2D( x, barY - 4.0f, 2.0f, barH + 8.0f ), Rgba( 255, 255, 255, 150 ) );
    }
    for ( const Cinematic::Event &event : pCin->getFxEvents() ) {
        uint64_t root = 0;
        if ( event.Reference >= 0 && event.Reference < static_cast<int32_t>( pCin->getReferences().size() ) ) {
            root = pCin->getReferences()[event.Reference].Hashcode;
        }
        const float x = barX + barW * Clamp( event.Time / duration, 0.0f, 1.0f );
        const bool bVoice = cinematic.getSounds().isVoiceRoot( root );
        renderer.drawRect( Rect2D( x, barY + barH + 2.0f, 2.0f, 7.0f ),
                           bVoice ? Rgba( 255, 180, 40 ) : Rgba( 170, 170, 170, 170 ) );
    }
    renderer.end();
    // The readout: the shot, the time and the active camera.
    size_t shotIndex = 0;
    for ( size_t s = 0; s < pCin->getShots().size(); s++ ) {
        if ( pCin->getShots()[s].Start <= t + 0.001f ) {
            shotIndex = s;
        }
    }
    const EdenSceneView::Camera &camera = cinematic.getCamera();
    const Quaternionf conj( -camera.Rotation.x, -camera.Rotation.y, -camera.Rotation.z, camera.Rotation.w );
    const Vector3f forward = conj.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
    char line[192];
    snprintf( line, sizeof( line ), "shot %zu/%zu   t %.1f / %.1f s%s", shotIndex + 1,
              pCin->getShots().size(), t, duration, bPaused ? "   PAUSED" : "" );
    char line2[160];
    snprintf( line2, sizeof( line2 ), "cam (%.1f, %.1f, %.1f)  yaw %.1f  fov %.0f", camera.Position.x,
              camera.Position.y, camera.Position.z, std::atan2( forward.x, forward.z ) * 57.29578f,
              camera.Fov * 57.29578f );
    renderer.begin();
    renderer.drawText( pFont, line, 640.0f, 52.0f, 17.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
    renderer.drawText( pFont, line2, 640.0f, 74.0f, 15.0f, Rgba( 190, 230, 255 ), eTextAlign::TA_Center );
    renderer.end();
}

// The browser (ImGui): the current bank's cinematics (click to play), the
// bank / cinematic text fields to load another bank (the mode re-enters) and
// the scrub slider (paused playback, the audio frozen).
void GMCinViewer::drawUI() {
    if ( !bLoaded || ( !bTool && !CmdLine::HasArg( "-cinehud" ) ) ) {
        return;
    }
    ImGui::SetNextWindowPos( DpiScaled( 20.0f, 60.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( DpiScaled( 340.0f, 420.0f ), ImGuiCond_FirstUseEver );
    if ( !ImGui::Begin( "Cinematic browser" ) ) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted( "Cinematics of the bank:" );
    for ( const std::string &name : cinematic.getCinematicNames() ) {
        const bool bPlaying = name == cinematic.getMainName();
        if ( ImGui::Selectable( name.c_str(), bPlaying ) ) {
            if ( cinematic.playMain( name ) ) {
                bPaused = false;
                bEnded = false;
                OTDU_LOG_INFO( "CINVIEWER: browser plays '%s'\n", name.c_str() );
            }
        }
    }
    ImGui::Separator();
    ImGui::InputText( "bank", bankEdit, sizeof( bankEdit ) );
    ImGui::InputText( "cinematic", cinEdit, sizeof( cinEdit ) );
    if ( ImGui::Button( "load bank (re-enter)" ) ) {
        request.Bank = bankEdit;
        request.Cinematic = cinEdit;
        pGameInstance->setNextGameMode( eGameMode::GM_CinViewer );
    }
    const Cinematic *pCin = cinematic.getMainCinematic();
    if ( pCin != nullptr && pCin->getDuration() > 0.0f ) {
        ImGui::Separator();
        ImGui::Checkbox( "paused (camera key)", &bPaused );
        float t = cinematic.getMainTime();
        if ( ImGui::SliderFloat( "time", &t, 0.0f, pCin->getDuration() - 0.01f, "%.2f s" ) ) {
            cinematic.seekMain( t );
        }
    }
    ImGui::End();
}
