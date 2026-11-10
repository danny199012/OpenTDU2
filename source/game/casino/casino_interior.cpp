#include "shared.h"
#include "casino_interior.h"

#include "filesystem/gs_file.h"
#include "database/gs_database.h"
#include "game/cinematic/cinematic_car.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/eden/eden_scene_view.h"
#include "system/cmd_line.h"

#include <SDL3/SDL_scancode.h>

namespace {
// The walk camera Fov of the spot interiors (InteriorWalk::kWalkFov) [INF].
constexpr float kWalkFov = 1.0472f;

constexpr const char *pSlotMachinesArray = "ArrayOf_Entity__Actor__CasinoGame__SlotMachine";
constexpr const char *pSlotTypeDescs = "ArrayOf_Entity__CasinoBaseDesc__CasinoGameTypeDesc__CasinoSlotMachineTypeDesc";
constexpr const char *pRouletteGames = "ArrayOf_Entity__Actor__CasinoGame__CasinoGameRoulette";
constexpr const char *pRouletteTypes = "ArrayOf_Entity__CasinoBaseDesc__CasinoGameTypeDesc__CasinoRouletteTypeDesc";
constexpr const char *pPokerGames = "ArrayOf_Entity__Actor__CasinoGame__CasinoGamePoker";
constexpr const char *pPokerTypes = "ArrayOf_Entity__CasinoBaseDesc__CasinoGameTypeDesc__CasinoPokerTypeDesc";

// A placement of an entity (orient_pos_scale): position + the volume matrix
// of its SelectableZone (world -> unit box).
struct Placement {
    Vector3f Position;
    bool bHasVolume = false;
    float Matrix[4][4] = {};
};

void ReadPlacement( const XmbValue &entity, Placement &out ) {
    XmbValue pos = entity["orient_pos_scale"]["pos"];
    out.Position = Vector3f( pos["X"].asFloat(), pos["Y"].asFloat(), pos["Z"].asFloat() );
}

void ReadVolumeMatrix( const XmbValue &entity, float ( &outMatrix )[4][4] ) {
    XmbValue matrix = entity["volume_mat"];
    const char *pRows[4] = { "_0", "_1", "_2", "_3" };
    const char *pColumns[4] = { "X", "Y", "Z", "W" };
    for ( int32_t r = 0; r < 4; r++ ) {
        for ( int32_t c = 0; c < 4; c++ ) {
            outMatrix[r][c] = matrix[pRows[r]][pColumns[c]].asFloat();
        }
    }
}

// The CasinoTypeDesc Price of a machine / table (the minimum bet) [DATA].
int32_t FindTypePrice( const XmbDocument &entities, const char *pArray, uint64_t hashcode ) {
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
        XmbValue row = entityExport.getArrayElement( pArray, i );
        if ( row["__Hc"].asUInt64() == hashcode ) {
            return static_cast<int32_t>( row["Price"].asInt() );
        }
    }
    return 0;
}

// The retail SelectableZone volume of an entity: its volume_mat and its
// orient_pos_scale pos.
bool FindZoneVolume( const XmbDocument &entities, uint64_t hashcode, Placement &out ) {
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Volume" ); i++ ) {
        XmbValue row = entityExport.getArrayElement( "ArrayOf_Entity__Volume", i );
        if ( row["__Hc"].asUInt64() != hashcode ) {
            continue;
        }
        ReadPlacement( row, out );
        ReadVolumeMatrix( row, out.Matrix );
        out.bHasVolume = true;
        return true;
    }
    return false;
}
} // namespace

std::string CasinoInterior::RoomBank( eRoom room ) {
    switch ( room ) {
    case eRoom::Hall:
        return "interior/icashal.bnk";
    case eRoom::Slots:
        return "interior/icasslo1.bnk";
    case eRoom::Roulette:
        return "interior/icasrou1.bnk";
    case eRoom::Poker:
        return "interior/icaspok1.bnk";
    case eRoom::VIP:
        return "interior/icasvip.bnk";
    case eRoom::Dressing:
        return "interior/icasdr.bnk";
    case eRoom::Shop:
        return "interior/ishop_ca.bnk";
    default:
        return "";
    }
}

const char *CasinoInterior::RoomName( eRoom room ) {
    switch ( room ) {
    case eRoom::Hall:
        return "hall";
    case eRoom::Slots:
        return "slots";
    case eRoom::Roulette:
        return "roulette";
    case eRoom::Poker:
        return "poker";
    case eRoom::VIP:
        return "vip";
    case eRoom::Dressing:
        return "dressing";
    case eRoom::Shop:
        return "shop";
    default:
        return "none";
    }
}

bool CasinoInterior::enter( eRoom newRoom ) {
    return loadRoom( newRoom );
}

bool CasinoInterior::changeRoom( eRoom newRoom ) {
    if ( newRoom == room ) {
        return true;
    }
    return loadRoom( newRoom );
}

void CasinoInterior::exit() {
    rankings.detach();
    bViewingRankings = false;
    if ( scene.isLoaded() ) {
        scene.unload();
    }
    bWalking = false;
    playingCinematic.clear();
    bCinematicEnded = false;
    bActive = false;
    room = eRoom::None;
    places.clear();
    bankPath.clear();
}

void CasinoInterior::startWalk() {
    bWalking = true;
    scene.stopMain();
    std::string camera = scene.getFirstPersonCamera();
    // The hall's Entity__FirstPerson points at cam_from_Ext, the arrival
    // camera *outside* the building (the retail fade-in comes from the
    // exterior bank, which an interior-only scene does not have): start at the
    // door of a game room instead, inside the hall [INF].
    if ( room == eRoom::Hall && ( camera == "cam_from_Ext" || camera.empty() ) ) {
        for ( const char *pInside : { "cam_from_slo1", "cam_from_rou1", "cam_from_shop" } ) {
            Vector3f position;
            Quaternionf heading;
            if ( scene.getEntityCameraLocal( pInside, position, heading ) ) {
                camera = pInside;
                break;
            }
        }
    }
    // The VIP's Entity__FirstPerson points at cam_from_Hall, 7 m up and ~40 m
    // out at z +23 - outside the room (the table cameras sit at z -30..-34, the
    // car at -17): start at the room's own CameraCasino 'Cam_FirstPerson', the
    // table overview (5.7 m up) [DATA: the -logcasino camera list; the choice
    // INF - CAM_RANKINGS at eye height was tried and faces the screen at 1 m].
    if ( room == eRoom::VIP && _stricmp( camera.c_str(), "cam_from_Hall" ) == 0 ) {
        for ( const char *pInside : { "Cam_FirstPerson" } ) {
            Vector3f position;
            Quaternionf heading;
            if ( scene.getEntityCameraLocal( pInside, position, heading ) ) {
                camera = pInside;
                break;
            }
        }
    }
    if ( camera.empty() ) {
        for ( const char *pName : { "cam_entree", "cam_walkmode" } ) {
            Vector3f position;
            Quaternionf heading;
            if ( scene.getEntityCameraLocal( pName, position, heading ) ) {
                camera = pName;
                break;
            }
        }
    }
    if ( camera.empty() ) {
        // A bank with no Entity__FirstPerson (the dressing room): the first
        // Entity__Camera is the best available start [INF].
        XmbValue entityExport = entitiesDoc.getRoot()["ExportType_Entity"];
        for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Camera" ); i++ ) {
            const std::string name = entityExport.getArrayElement( "ArrayOf_Entity__Camera", i )["__Name"].asString();
            Vector3f position;
            Quaternionf heading;
            if ( !name.empty() && scene.getEntityCameraLocal( name, position, heading ) ) {
                camera = name;
                break;
            }
        }
    }
    // -logcasino: every Entity__Camera of the bank with its scene-local
    // position (to pick / check a room's walk start).
    if ( CmdLine::HasArg( "-logcasino" ) ) {
        XmbValue entityExport = entitiesDoc.getRoot()["ExportType_Entity"];
        for ( const char *pArray : { "ArrayOf_Entity__Camera", "ArrayOf_Entity__Camera__CameraCasino" } ) {
            for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
                const std::string name = entityExport.getArrayElement( pArray, i )["__Name"].asString();
                Vector3f position;
                Quaternionf heading;
                if ( !name.empty() && scene.getEntityCameraLocal( name, position, heading ) ) {
                    OTDU_LOG_INFO( "CasinoInterior: camera '%s' at (%.2f, %.2f, %.2f)\n", name.c_str(), position.x,
                                   position.y, position.z );
                }
            }
        }
    }
    if ( walk.isInSelfView() ) {
        walk.endSelfView( scene ); // a room swap restarts the walk cleanly
    }
    bEmoteWasPlaying = false;
    if ( camera.empty() || !walk.start( scene, camera, kWalkFov ) ) {
        OTDU_LOG_WARN( "CasinoInterior: no walk start camera in '%s'\n", bankPath.c_str() );
    }
    // The walk-mode screen of INTRO.vmf (the reticle and /:SelItemType).
    scene.getUi().gotoLabel( "/", "wm_home", true );
    {
        Vector3f position;
        Quaternionf heading;
        const bool bFound = !camera.empty() && scene.getEntityCameraLocal( camera, position, heading );
        OTDU_LOG_INFO( "CasinoInterior: walking '%s' at '%s' (%.2f, %.2f, %.2f)%s\n", RoomName( room ), camera.c_str(),
                       position.x, position.y, position.z, bFound ? "" : " [no entity camera]" );
    }
}

// The emotes (docs/re/emotes_spec.md), the FIRST_WM law: X toggles the self
// view, 1..8 the slots, the view returns at the clip end; -emote <label>
// plays one on the walk start (headless tests). The casino is where the
// retail Emote_11..33 unlockables come from (casino_spec.md 7).
void CasinoInterior::updateEmotes( float deltaTime ) {
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

namespace {
IntroCinematic::Volume MakeBoxSelectable( const Vector3f &center, const Vector3f &halfExtents, const std::string &type );
} // namespace

bool CasinoInterior::loadRoom( eRoom newRoom ) {
    const std::string bank = RoomBank( newRoom );
    if ( bank.empty() ) {
        return false;
    }
    if ( gpFile == nullptr || !gpFile->fileExists( ( gpFile->getBankFolder() + bank ).c_str() ) ) {
        OTDU_LOG_WARN( "CasinoInterior: '%s' is not in the game data\n", bank.c_str() );
        return false;
    }
    exit();
    // A closed room in its own frame (the spot interior setup): no island
    // streaming, the walk camera's near plane all along.
    IntroCinematic::Setup setup;
    setup.Bank = bank;
    setup.ExteriorBank.clear();
    setup.ExteriorPrefix.clear();
    setup.bWorld = false;
    setup.bInterior = true;
    setup.bStreamWorld = false;
    if ( !scene.load( setup ) ) {
        OTDU_LOG_WARN( "CasinoInterior: '%s' could not be loaded\n", bank.c_str() );
        scene.unload();
        return false;
    }
    bankPath = bank;
    room = newRoom;
    bActive = true;
    // The bank's own entity list (the machines / tables of the room).
    const BankEntry *pEntities = scene.getSceneBank().findEntry( "entities.xmb" );
    if ( pEntities != nullptr ) {
        if ( !entitiesDoc.load( pEntities->pData, pEntities->Size ) ) {
            OTDU_LOG_WARN( "CasinoInterior: '%s' has no readable entities.xmb\n", bank.c_str() );
        }
    }
    // The player avatar (the .sce actor "Avatar" of the room's cinematics).
    const PlayerProfile &profile = GetPlayerProfile();
    scene.setPlayerAvatar( profile.AvatarConfig.empty() ? "AVT_Male_Intro" : profile.AvatarConfig );

    // The life of the room: the C_LIFE* cinematics loop in the background
    // (the bar man, the fish, the jackpot screens ...) [DATA: the names].
    std::string names;
    std::string intro;
    for ( const std::string &name : scene.getCinematicNames() ) {
        names += " " + name;
        if ( name.rfind( "C_LIFE", 0 ) == 0 ) {
            scene.playBackground( name, true );
        } else if ( name == "C_INTRO" ) {
            intro = name;
        }
    }
    OTDU_LOG_INFO( "CasinoInterior: '%s' (%s): cinematics%s; start camera '%s'\n", RoomName( room ),
                   bank.c_str(), names.c_str(), scene.getFirstPersonCamera().c_str() );

    buildGamePlaces();
    placeShowCars();
    if ( newRoom == eRoom::Hall && rankings.attach( scene, entitiesDoc ) ) {
        // The screen's SelectableZone with its SelectableString 'crnk' [DATA].
        Placement zone;
        IntroCinematic::Volume volume;
        if ( FindZoneVolume( entitiesDoc, rankings.getZoneHash(), zone ) ) {
            volume.Name = "FlashScreen_CRNK_TimeAttack";
            volume.OfType = 2;
            volume.SelectableType = 57;
            volume.SelectableString = "crnk";
            volume.Center = zone.Position;
            memcpy( volume.Matrix, zone.Matrix, sizeof( volume.Matrix ) );
        } else {
            volume = MakeBoxSelectable( rankings.getScreenPosition(), Vector3f( 1.2f, 1.0f, 0.6f ), "crnk" );
        }
        scene.addSelectable( volume );
    }
    // Every casino room's C_INTRO is an arrival shot framed from outside the
    // building (the hall, slots, roulette, poker and VIP shots all look at the
    // dunes and the casino facade; the dressing one starts under the sofa):
    // they need the exterior bank an interior-only scene does not have, so the
    // walk starts straight away [INF: 100-frame shots of every room, 2026-11-05].
    // -casinoroomintro plays them anyway (the A/B).
    // -casinocin <name>: play one of the room's cinematics on entry instead
    // (the test hook for the jackpot / rankings / car-win cutscenes).
    if ( const char *pTestCin = CmdLine::GetArgValue( "-casinocin" ) ) {
        static bool bTestCinPlayed = false;
        if ( CmdLine::HasArg( "-casinocinat" ) ) {
            const int32_t at = CmdLine::GetArgInt( "-casinocinat", 0 );
            if ( at >= 0 && at < static_cast<int32_t>( places.size() ) && places[at].bHasAnchor ) {
                scene.setCinematicOrigin( pTestCin, places[at].Anchor );
            }
        }
        if ( !bTestCinPlayed && scene.playMain( pTestCin ) ) {
            bTestCinPlayed = true;
            playingCinematic = pTestCin;
            bWalkAfterCinematic = true;
            scene.setCameraOverride( nullptr );
            OTDU_LOG_INFO( "CasinoInterior: -casinocin '%s' (CinProps 0x%x)\n", pTestCin, scene.getCinProps( pTestCin ) );
            return true;
        }
    }
    const bool bSkipIntro = intro.empty() || !CmdLine::HasArg( "-casinoroomintro" );
    if ( !bSkipIntro && !CmdLine::HasArg( "-nocasinointeriorto" ) ) {
        if ( !scene.playMain( intro ) ) {
            startWalk();
        }
    } else {
        startWalk();
    }
    return true;
}

void CasinoInterior::placeShowCars() {
    if ( CmdLine::HasArg( "-nocasinocar" ) ) {
        return;
    }
    // The car on the hall's socle and the VIP lounge's car: each room's
    // Entity__Actor__ActorVehiclePosition [DATA]. Its VehicleId names the car
    // when it is a DBCars id; otherwise the room's prize is shown: the hall
    // shows the slot machine's jackpot car (DBRewardCar "CASINO_CAR_1", the
    // Audi R8 V10) and the VIP lounge its own ("CASINO_CAR_VIP") [INF: the
    // retail pick is server-driven; the reward rows are the cars the room
    // gives away].
    for ( const IntroCinematic::VehiclePosition &vehicle : scene.getVehiclePositions() ) {
        int32_t carId = vehicle.VehicleId;
        int32_t paintIndex = vehicle.PaintId;
        CinematicCar::Desc desc;
        if ( carId <= 0 || !CinematicCar::DescFromDatabase( carId, desc ) ) {
            const char *pEvent = room == eRoom::VIP ? "CASINO_CAR_VIP" : "CASINO_CAR_1";
            carId = room == eRoom::VIP ? -1 : 432;
            if ( gpDatabase != nullptr ) {
                XmbValue root = gpDatabase->getDocument().getRoot();
                for ( uint32_t i = 0; i < root.getArraySize( "DBRewardCar" ); i++ ) {
                    XmbValue row = root.getArrayElement( "DBRewardCar", i );
                    if ( row["_event"].asString() == pEvent ) {
                        carId = static_cast<int32_t>( row["_carunlocked"].asUInt64() );
                        break;
                    }
                }
            }
            if ( carId <= 0 || !CinematicCar::DescFromDatabase( carId, desc ) ) {
                OTDU_LOG_WARN( "CasinoInterior: no car for '%s' (VehicleId %d, %s)\n", vehicle.Name.c_str(),
                               vehicle.VehicleId, pEvent );
                continue;
            }
        }
        if ( !scene.addDisplayCar( desc, vehicle.Name, paintIndex ) ) {
            OTDU_LOG_WARN( "CasinoInterior: car %d could not be placed on '%s'\n", carId, vehicle.Name.c_str() );
            continue;
        }
        // NbTurnByMinute: the socle's turn rate [DATA]; the car turns with it.
        const float turnsPerMinute = CmdLine::HasArg( "-casinocarturns" )
                                         ? static_cast<float>( atof( CmdLine::GetArgValue( "-casinocarturns" ) ) )
                                         : vehicle.TurnsPerMinute;
        scene.spinLastDisplayCar( turnsPerMinute * 6.2831853f / 60.0f );
        OTDU_LOG_INFO( "CasinoInterior: car %d '%s' on '%s' (paint %d, %.2f turns/min)\n", carId, desc.FileName.c_str(),
                       vehicle.Name.c_str(), paintIndex, turnsPerMinute );
    }
}

namespace {
// The retail hall desks (cashier / bar) carry their own Entity__Volume
// ("SelectableCashier", "SelectableBar") placed right at the desk. Find one by
// name so the selectable box goes where the player actually stands, not where
// the actor's orient_pos_scale happens to sit.
bool FindVolumePosition( const XmbDocument &entities, const char *pName, Placement &out ) {
    XmbValue root = entities.getRoot()[ "ExportType_Entity" ];
    for ( uint32_t i = 0; i < root.getArraySize( "ArrayOf_Entity__Volume" ); i++ ) {
        XmbValue row = root.getArrayElement( "ArrayOf_Entity__Volume", i );
        if ( row[ "__Name" ].asString() == pName ) {
            ReadPlacement( row, out );
            return true;
        }
    }
    return false;
}

// A selectable box of the given half extents around a point: the unit box
// mapping of IntroCinematic::Volume::toUnit (p' = (p - center) / extents).
IntroCinematic::Volume MakeBoxSelectable( const Vector3f &center, const Vector3f &halfExtents,
                                          const std::string &type ) {
    IntroCinematic::Volume volume;
    volume.Name = type;
    volume.OfType = 2;
    volume.SelectableType = 57;
    volume.SelectableString = type;
    volume.Center = center;
    for ( int32_t r = 0; r < 4; r++ ) {
        for ( int32_t c = 0; c < 4; c++ ) {
            volume.Matrix[r][c] = 0.0f;
        }
    }
    volume.Matrix[0][0] = 1.0f / Max( 0.05f, halfExtents.x );
    volume.Matrix[1][1] = 1.0f / Max( 0.05f, halfExtents.y );
    volume.Matrix[2][2] = 1.0f / Max( 0.05f, halfExtents.z );
    volume.Matrix[3][0] = -center.x * volume.Matrix[0][0];
    volume.Matrix[3][1] = -center.y * volume.Matrix[1][1];
    volume.Matrix[3][2] = -center.z * volume.Matrix[2][2];
    volume.Matrix[3][3] = 1.0f;
    return volume;
}
} // namespace

namespace {
// -dumpcasinoentities <substring>: every field of the first rows of the
// matching ArrayOf_* entity arrays of the room (the per-machine pieces).
void DumpXmbFields( const XmbValue &value, const std::string &indent, int32_t depth ) {
    const XmbType *pType = value.getType();
    if ( pType == nullptr || depth > 5 ) {
        return;
    }
    for ( const XmbType::Field &field : pType->Fields ) {
        if ( field.bArray ) {
            const uint32_t count = value.getArraySize( field.Name.c_str() );
            OTDU_LOG_INFO( "%s%s[%u]\n", indent.c_str(), field.Name.c_str(), count );
            for ( uint32_t i = 0; i < count && i < 6; i++ ) {
                const XmbValue element = value.getArrayElement( field.Name.c_str(), i );
                const XmbType *pElementType = element.getType();
                if ( pElementType != nullptr && pElementType->bComposite ) {
                    DumpXmbFields( element, indent + "  ", depth + 1 );
                } else {
                    OTDU_LOG_INFO( "%s  [%u] %s\n", indent.c_str(), i, element.asString().c_str() );
                }
            }
            continue;
        }
        const XmbValue child = value[field.Name.c_str()];
        const XmbType *pChildType = child.getType();
        if ( pChildType != nullptr && pChildType->bComposite ) {
            OTDU_LOG_INFO( "%s%s:\n", indent.c_str(), field.Name.c_str() );
            DumpXmbFields( child, indent + "  ", depth + 1 );
        } else if ( pChildType != nullptr && pChildType->Primitive == eXmbPrimitive::XP_String ) {
            OTDU_LOG_INFO( "%s%s = '%s'\n", indent.c_str(), field.Name.c_str(), child.asString().c_str() );
        } else if ( pChildType != nullptr && ( pChildType->Primitive == eXmbPrimitive::XP_Float ||
                                               pChildType->Primitive == eXmbPrimitive::XP_Double ) ) {
            OTDU_LOG_INFO( "%s%s = %g\n", indent.c_str(), field.Name.c_str(), child.asDouble() );
        } else {
            OTDU_LOG_INFO( "%s%s = %llu (0x%llx)\n", indent.c_str(), field.Name.c_str(),
                           static_cast<unsigned long long>( child.asUInt64() ),
                           static_cast<unsigned long long>( child.asUInt64() ) );
        }
    }
}
} // namespace

void CasinoInterior::buildGamePlaces() {
    places.clear();
    if ( !bActive ) {
        return;
    }
    const XmbDocument &entities = entitiesDoc;
    XmbValue root = entities.getRoot()["ExportType_Entity"];
    if ( const char *pDump = CmdLine::GetArgValue( "-dumpcasinoentities" ) ) {
        // The document's other export blocks (beside ExportType_Entity).
        if ( const XmbType *pDocType = entities.getRoot().getType() ) {
            for ( const XmbType::Field &field : pDocType->Fields ) {
                OTDU_LOG_INFO( "CasinoEntities: root field '%s'%s\n", field.Name.c_str(), field.bArray ? " []" : "" );
                if ( strcmp( pDump, "root" ) == 0 && field.Name == "ExportType_Curve" ) {
                    XmbValue block = entities.getRoot()[field.Name.c_str()];
                    if ( const XmbType *pBlock = block.getType() ) {
                        for ( const XmbType::Field &array : pBlock->Fields ) {
                            const uint32_t count = array.bArray ? block.getArraySize( array.Name.c_str() ) : 0;
                            OTDU_LOG_INFO( "CasinoEntities: curve array '%s' [%u]\n", array.Name.c_str(), count );
                            for ( uint32_t i = 0; i < count; i++ ) {
                                XmbValue curve = block.getArrayElement( array.Name.c_str(), i );
                                if ( i < 2 || curve["__Hc"].asUInt64() == 0x9fef030bf8fb4d6cull ) {
                                    OTDU_LOG_INFO( "CasinoEntities: curve #%u\n", i );
                                    DumpXmbFields( curve, "    ", 0 );
                                }
                            }
                        }
                    }
                }
            }
        }
        if ( const XmbType *pRootType = root.getType() ) {
            for ( const XmbType::Field &field : pRootType->Fields ) {
                if ( !field.bArray ) {
                    continue;
                }
                const uint32_t count = root.getArraySize( field.Name.c_str() );
                OTDU_LOG_INFO( "CasinoEntities: %s [%u]\n", field.Name.c_str(), count );
                // "0x<hash>": the entity of that __Hc, in any array.
                if ( pDump[0] == '0' && pDump[1] == 'x' ) {
                    const uint64_t wanted = strtoull( pDump + 2, nullptr, 16 );
                    for ( uint32_t i = 0; i < count; i++ ) {
                        XmbValue entity = root.getArrayElement( field.Name.c_str(), i );
                        if ( entity["__Hc"].asUInt64() == wanted ) {
                            OTDU_LOG_INFO( "CasinoEntities: %s #%u is 0x%llx\n", field.Name.c_str(), i,
                                           static_cast<unsigned long long>( wanted ) );
                            DumpXmbFields( entity, "    ", 0 );
                        }
                    }
                    continue;
                }
                if ( field.Name.find( pDump ) == std::string::npos ) {
                    continue;
                }
                for ( uint32_t i = 0; i < count && i < 2; i++ ) {
                    OTDU_LOG_INFO( "CasinoEntities: %s #%u\n", field.Name.c_str(), i );
                    DumpXmbFields( root.getArrayElement( field.Name.c_str(), i ), "    ", 0 );
                }
            }
        }
    }
    if ( room == eRoom::Hall ) {
        // The cashier and the bar of the hall are actors, not FpSelectables:
        // they get a selectable box of their own (the retail walk code keys
        // them on their own volume) [DATA: Entity__Actor__CasinoCashier
        // "Sc_Cashier", Entity__Actor__CasinoBarman "Sc_Bar"].
        struct DeskPlace {
            const char *pArray;   // actor array: ArrayOf_Entity__Actor__CasinoCashier
            const char *pVolume;  // retail volume placed at the desk: SelectableCashier
            const char *pType;    // fallback selectable string (Casino.vmf's ItemType)
            eAction Action;
        };
        const DeskPlace desks[] = {
            { "ArrayOf_Entity__Actor__CasinoCashier", "SelectableCashier", "cash", eAction::Cashier },
            { "ArrayOf_Entity__Actor__CasinoBarman",   "SelectableBar",     "barman", eAction::Bar },
        };
        desk = DeskData();
        for ( const DeskPlace &deskPlace : desks ) {
            for ( uint32_t i = 0; i < root.getArraySize( deskPlace.pArray ); i++ ) {
                XmbValue row = root.getArrayElement( deskPlace.pArray, i );
                GamePlace place;
                place.EntityName = row["__Name"].asString();
                place.Action = deskPlace.Action;
                // The actor's own SelectableString ('cash' / 'barman', the
                // ItemType vocabulary of Casino.vmf) [DATA].
                place.Selectable = row["SelectableString"].asString();
                if ( place.Selectable.empty() ) {
                    place.Selectable = deskPlace.pType;
                }
                // The desk's game data [DATA: Entity__Actor__CasinoCashier /
                // CasinoBarman, member names from the PS3 build 0xf48a20..0xf48d50].
                if ( deskPlace.Action == eAction::Cashier ) {
                    desk.CashierGift = static_cast<int32_t>( row["AmountGivedToPlayer"].asInt() );
                } else {
                    desk.DrinkCinematic = row["CinematicPlayerDrink"].asString();
                    desk.DrinkWonCinematic = row["CinematicPlayerDrinkWonTicket"].asString();
                    desk.PrepareCinematic = row["CinematicPrepareDrink"].asString();
                    desk.TicketChance[0] = static_cast<int32_t>( row["WinTicketLevelOneChancePercentage"].asInt() );
                    desk.TicketChance[1] = static_cast<int32_t>( row["WinTicketLevelTwoChancePercentage"].asInt() );
                    desk.TicketChance[2] = static_cast<int32_t>( row["WinTicketLevelThreeChancePercentage"].asInt() );
                    desk.TicketChance[3] = static_cast<int32_t>( row["WinTicketLevelFourChancePercentage"].asInt() );
                }
                // The retail SelectableZone volume (the same path as the slot
                // machines); then Cline's named desk volume; then a box at the
                // actor.
                IntroCinematic::Volume volume;
                Placement zone;
                if ( FindZoneVolume( entities, row["SelectableZone"].asUInt64(), zone ) ) {
                    volume.Name = place.EntityName;
                    volume.OfType = 2;
                    volume.SelectableType = 57;
                    volume.SelectableString = place.Selectable;
                    volume.Center = zone.Position;
                    memcpy( volume.Matrix, zone.Matrix, sizeof( volume.Matrix ) );
                } else {
                    Placement placement;
                    if ( !FindVolumePosition( entities, deskPlace.pVolume, placement ) ) {
                        ReadPlacement( row, placement );
                    }
                    volume = MakeBoxSelectable( placement.Position, Vector3f( 1.2f, 1.1f, 1.2f ), place.Selectable );
                }
                scene.addSelectable( volume );
                places.push_back( place );
            }
        }
        OTDU_LOG_INFO( "CasinoInterior: cashier gift %d, bar tickets %d/%d/%d/%d %%, drink '%s' / '%s', prepare '%s'\n",
                       desk.CashierGift, desk.TicketChance[0], desk.TicketChance[1], desk.TicketChance[2],
                       desk.TicketChance[3], desk.DrinkCinematic.c_str(), desk.DrinkWonCinematic.c_str(),
                       desk.PrepareCinematic.c_str() );
        OTDU_LOG_INFO( "CasinoInterior: hall: %zu desk(s) selectable\n", places.size() );
        return;
    }
    if ( room == eRoom::Slots ) {
        // The 38 machines of the room: each one links its own FpSelectable
        // (SelectableZone) and its SelectableString ("sm_car" ...) [DATA].
        for ( uint32_t i = 0; i < root.getArraySize( pSlotMachinesArray ); i++ ) {
            XmbValue row = root.getArrayElement( pSlotMachinesArray, i );
            GamePlace place;
            place.EntityName = row["__Name"].asString();
            place.GameId = static_cast<int32_t>( row["IDGame"].asInt() );
            place.Selectable = row["SelectableString"].asString();
            if ( place.Selectable.empty() ) {
                place.Selectable = "slot";
            }
            place.Price = FindTypePrice( entities, pSlotTypeDescs, row["TypeDesc"].asUInt64() );
            place.Action = eAction::SlotMachine;
            {
                XmbValue frame = row["orient_pos_scale"];
                const Quaternionf rotation( frame["rot"]["X"].asFloat(), frame["rot"]["Y"].asFloat(),
                                            frame["rot"]["Z"].asFloat(), frame["rot"]["W"].asFloat( 1.0f ) );
                const Vector3f position( frame["pos"]["X"].asFloat(), frame["pos"]["Y"].asFloat(),
                                         frame["pos"]["Z"].asFloat() );
                place.Anchor = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( rotation );
                place.bHasAnchor = true;
                if ( CmdLine::HasArg( "-logcasino" ) ) {
                    OTDU_LOG_INFO( "CasinoInterior: machine %u '%s' at (%.2f, %.2f, %.2f) rot (%.3f, %.3f, %.3f, %.3f)\n", i,
                                   place.EntityName.c_str(), position.x, position.y, position.z, rotation.x, rotation.y,
                                   rotation.z, rotation.w );
                }
            }
            place.CameraGameHash = row["CameraGame"].asUInt64();
            place.CameraGame = scene.getEntityCameraName( place.CameraGameHash );
            place.NumReels = static_cast<int32_t>( row.getArraySize( "Spinners" ) );
            place.TimeBeforeFirstStop = row["TimeBeforeFirstStop"].asFloat();
            place.TriggerHash = row["Trigger"].asUInt64();
            for ( uint32_t n = 0; n < row.getArraySize( "Neons" ); n++ ) {
                place.Neons.push_back( row.getArrayElement( "Neons", n )["NeonSlotMachine"].asUInt64() );
            }
            // ComboActors[n]: the pay panel of combination n on the machine's top
            // (Entity__Actor__Neon 'PartOfSM_Poker_gain_00'..'_08' on a Poker
            // machine) [DATA].
            for ( uint32_t n = 0; n < row.getArraySize( "ComboActors" ); n++ ) {
                place.Combos.push_back( row.getArrayElement( "ComboActors", n )["Actor"].asUInt64() );
            }
            place.SlotScreenHash = row["SlotScreen"].asUInt64();
            place.LinesScreenHash = row["SlotLinesScreen"].asUInt64();
            for ( uint32_t t = 0; t < root.getArraySize( "ArrayOf_Entity__Actor__SlotMachineTrigger" ); t++ ) {
                XmbValue trigger = root.getArrayElement( "ArrayOf_Entity__Actor__SlotMachineTrigger", t );
                if ( trigger["__Hc"].asUInt64() == place.TriggerHash ) {
                    XmbValue anim = trigger["Anim"];
                    if ( anim.getArraySize( "File" ) > 0 ) {
                        place.TriggerClip = anim.getArrayElement( "File", 0 )["rsc"].asUInt64();
                    }
                    break;
                }
            }
            for ( uint32_t r = 0; r < row.getArraySize( "Spinners" ); r++ ) {
                place.Spinners.push_back( row.getArrayElement( "Spinners", r )["Spinner"].asUInt64() );
                float after = 0.0f;
                for ( uint32_t s3 = 0; s3 < root.getArraySize( "ArrayOf_Entity__Actor__SlotMachineSpinner" ); s3++ ) {
                    XmbValue spin = root.getArrayElement( "ArrayOf_Entity__Actor__SlotMachineSpinner", s3 );
                    if ( spin["__Hc"].asUInt64() == place.Spinners.back() ) {
                        after = spin["TimeAfterPrevSpinner"].asFloat();
                        break;
                    }
                }
                place.ReelStopAfter.push_back( after );
            }
            if ( place.NumReels > 0 ) {
                const uint64_t spinner = row.getArrayElement( "Spinners", 0 )["Spinner"].asUInt64();
                for ( uint32_t s = 0; s < root.getArraySize( "ArrayOf_Entity__Actor__SlotMachineSpinner" ); s++ ) {
                    XmbValue spin = root.getArrayElement( "ArrayOf_Entity__Actor__SlotMachineSpinner", s );
                    if ( spin["__Hc"].asUInt64() == spinner ) {
                        place.ReelMaterial = spin["MatName"].asString();
                        place.AtlasColumns = static_cast<int32_t>( spin["NbColumnOfSymbolsByTexture"].asInt() );
                        place.AtlasRows = static_cast<int32_t>( spin["NbLineOfSymbolsByTexture"].asInt() );
                        place.TimeAfterPrevSpinner = spin["TimeAfterPrevSpinner"].asFloat();
                        place.SpinnerType = static_cast<int32_t>( spin["SpinnerType"].asInt() );
                        place.RotationSpeed = spin["RotationSpeed"].asFloat();
                        break;
                    }
                }
            }
            if ( CmdLine::HasArg( "-logcasinoreels" ) ) {
                std::string list;
                for ( uint64_t neon : place.Neons ) {
                    char text[24];
                    snprintf( text, sizeof( text ), " %016llx", static_cast<unsigned long long>( neon ) );
                    list += text;
                }
                std::string combos;
                for ( uint32_t n = 0; n < row.getArraySize( "ComboActors" ); n++ ) {
                    XmbValue combo = row.getArrayElement( "ComboActors", n );
                    char text[48];
                    snprintf( text, sizeof( text ), " %016llx", static_cast<unsigned long long>( combo["Actor"].asUInt64() ) );
                    combos += text;
                }
                OTDU_LOG_INFO( "CasinoInterior: machine %zu '%s' neons%s | combos%s | jackpotfx %016llx\n", places.size(),
                               place.EntityName.c_str(), list.c_str(), combos.c_str(),
                               static_cast<unsigned long long>( row["JackpotFx"].asUInt64() ) );
                // Every reel's own spinner (the first one gives the machine's atlas).
                for ( uint64_t spinnerHash : place.Spinners ) {
                    for ( uint32_t s2 = 0; s2 < root.getArraySize( "ArrayOf_Entity__Actor__SlotMachineSpinner" ); s2++ ) {
                        XmbValue spin = root.getArrayElement( "ArrayOf_Entity__Actor__SlotMachineSpinner", s2 );
                        if ( spin["__Hc"].asUInt64() == spinnerHash ) {
                            OTDU_LOG_INFO( "CasinoInterior: machine %zu reel '%s' material '%s' atlas %dx%d type %d speed %.2f "
                                           "after %.2f\n",
                                           places.size(), spin["__Name"].asString().c_str(), spin["MatName"].asString().c_str(),
                                           static_cast<int32_t>( spin["NbColumnOfSymbolsByTexture"].asInt() ),
                                           static_cast<int32_t>( spin["NbLineOfSymbolsByTexture"].asInt() ),
                                           static_cast<int32_t>( spin["SpinnerType"].asInt() ), spin["RotationSpeed"].asFloat(),
                                           spin["TimeAfterPrevSpinner"].asFloat() );
                        }
                    }
                }
            }
            if ( CmdLine::HasArg( "-logcasino" ) ) {
                OTDU_LOG_INFO( "CasinoInterior: machine %zu '%s' (%s) camera '%s', %d reels, material '%s' atlas %dx%d, "
                               "first stop %.2f s, gap %.2f s, spinner type %d speed %.2f\n",
                               places.size(), place.EntityName.c_str(), place.Selectable.c_str(), place.CameraGame.c_str(),
                               place.NumReels, place.ReelMaterial.c_str(), place.AtlasColumns, place.AtlasRows,
                               place.TimeBeforeFirstStop, place.TimeAfterPrevSpinner, place.SpinnerType,
                               place.RotationSpeed );
            }
            IntroCinematic::Volume volume;
            Placement zone;
            if ( FindZoneVolume( entities, row["SelectableZone"].asUInt64(), zone ) ) {
                volume.Name = place.EntityName;
                volume.OfType = 2;
                volume.SelectableType = 57;
                volume.SelectableString = place.Selectable;
                volume.Center = zone.Position;
                memcpy( volume.Matrix, zone.Matrix, sizeof( volume.Matrix ) );
            } else {
                Placement placement;
                ReadPlacement( row, placement );
                volume = MakeBoxSelectable( placement.Position, Vector3f( 0.9f, 1.2f, 0.9f ), place.Selectable );
            }
            scene.addSelectable( volume );
            places.push_back( place );
        }
        OTDU_LOG_INFO( "CasinoInterior: slots room: %zu machine(s) selectable\n", places.size() );
        return;
    }
    if ( room == eRoom::Roulette ) {
        for ( uint32_t i = 0; i < root.getArraySize( pRouletteGames ); i++ ) {
            XmbValue row = root.getArrayElement( pRouletteGames, i );
            GamePlace place;
            place.EntityName = row["__Name"].asString();
            place.GameId = static_cast<int32_t>( row["IDGame"].asInt() );
            place.Action = eAction::RouletteTable;
            place.Selectable = "roulette";
            place.Price = FindTypePrice( entities, pRouletteTypes, row["TypeDesc"].asUInt64() );
            Placement placement;
            ReadPlacement( row, placement );
            scene.addSelectable( MakeBoxSelectable( placement.Position, Vector3f( 1.6f, 1.1f, 1.6f ), place.Selectable ) );
            places.push_back( place );
        }
        OTDU_LOG_INFO( "CasinoInterior: roulette room: %zu table(s)\n", places.size() );
        return;
    }
    if ( room == eRoom::Poker || room == eRoom::VIP ) {
        for ( uint32_t i = 0; i < root.getArraySize( pPokerGames ); i++ ) {
            XmbValue row = root.getArrayElement( pPokerGames, i );
            GamePlace place;
            place.EntityName = row["__Name"].asString();
            place.GameId = static_cast<int32_t>( row["IDGame"].asInt() );
            place.Selectable = "poker";
            place.Price = FindTypePrice( entities, pPokerTypes, row["TypeDesc"].asUInt64() );
            place.Action = eAction::PokerTable;
            Placement placement;
            ReadPlacement( row, placement );
            scene.addSelectable( MakeBoxSelectable( placement.Position, Vector3f( 1.7f, 1.1f, 1.7f ), place.Selectable ) );
            places.push_back( place );
        }
        OTDU_LOG_INFO( "CasinoInterior: %s room: %zu poker table(s)\n", RoomName( room ), places.size() );
        return;
    }
}

bool CasinoInterior::playCinematicAtPlace( const std::string &name, int32_t placeIndex ) {
    if ( placeIndex >= 0 && placeIndex < static_cast<int32_t>( places.size() ) && places[placeIndex].bHasAnchor ) {
        scene.setCinematicOrigin( name, places[placeIndex].Anchor );
    }
    return playCinematic( name );
}

bool CasinoInterior::playCinematic( const std::string &name ) {
    if ( !bActive || !hasCinematic( name ) || !scene.playMain( name ) ) {
        OTDU_LOG_WARN( "CasinoInterior: no cinematic '%s' in the %s room\n", name.c_str(), RoomName( room ) );
        return false;
    }
    playingCinematic = name;
    bCinematicEnded = false;
    // The cutscene's own cameras: the walk and a machine's game camera step
    // aside and come back when it ends.
    bWalkAfterCinematic = bWalking;
    bRefocusAfterCinematic = bCameraFocused;
    bWalking = false;
    scene.setCameraOverride( nullptr );
    OTDU_LOG_INFO( "CasinoInterior: cinematic '%s'\n", name.c_str() );
    return true;
}

void CasinoInterior::handleEndEvents() {
    for ( const std::string &name : scene.takeEndEvents() ) {
        if ( name == "C_INTRO" && playingCinematic.empty() ) {
            startWalk();
        } else if ( !playingCinematic.empty() && name == playingCinematic ) {
            playingCinematic.clear();
            bCinematicEnded = true;
            if ( bRefocusAfterCinematic && focusedCameraHash != 0 ) {
                focusCamera( focusedCameraHash );
            } else if ( bWalkAfterCinematic ) {
                startWalk();
            }
        }
    }
}

CasinoInterior::eAction CasinoInterior::tick( float deltaTime ) {
    if ( !bActive ) {
        return eAction::None;
    }
    // The press that skips the intro is not reused by the walk.
    bool bConsumed = false;
    if ( scene.getMainName() == "C_INTRO" ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            scene.skipMain();
            bConsumed = true;
        }
    }
    // A room cutscene: A / B skips it, nothing else is read meanwhile.
    if ( !playingCinematic.empty() ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            scene.skipMain();
        }
        scene.tick( deltaTime );
        handleEndEvents();
        return eAction::None;
    }
    // Looking at the rankings screen: B steps back to the walk.
    if ( bViewingRankings ) {
        scene.tick( deltaTime );
        handleEndEvents();
        rankings.update( scene, walk.getPosition(), true, deltaTime );
        if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            bViewingRankings = false;
            releaseCamera();
            bWalking = true;
        }
        return eAction::None;
    }
    scene.tick( deltaTime );
    handleEndEvents();
    if ( bWalking ) {
        walk.update( scene, deltaTime, true );
        updateEmotes( deltaTime );
    }
    rankings.update( scene, walk.getPosition(), bWalking, deltaTime );
    if ( bConsumed || !bWalking ) {
        return eAction::None;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        if ( walk.isInSelfView() ) {
            // B in the self view goes back to first person, not out of the room.
            walk.endSelfView( scene );
            return eAction::None;
        }
        // B: back to the hall from a game room, out of the casino from the hall.
        return room == eRoom::Hall ? eAction::Leave : eAction::BackToHall;
    }
    if ( !gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        return eAction::None;
    }
    if ( walk.isInSelfView() ) {
        return eAction::None; // the selection is cleared in the self view
    }
    const int32_t selected = walk.getSelected();
    if ( selected < 0 ) {
        return eAction::None;
    }
    const std::string type = walk.getSelectedType( scene );
    if ( type.empty() ) {
        return eAction::None;
    }
    // The retail vocabulary of the hall's doors [DATA: SelectableString].
    // The rankings screen ('crnk', SelectableDist 6): look at it through its
    // RankingCamera [DATA]; B steps back.
    if ( type == "crnk" && rankings.isAttached() && rankings.getCameraHash() != 0 ) {
        if ( focusCamera( rankings.getCameraHash() ) ) {
            bViewingRankings = true;
            bWalking = false;
        }
        return eAction::None;
    }
    if ( type == "outdoor" ) {
        return eAction::Leave;
    }
    if ( type == "chal" ) {
        return eAction::BackToHall;
    }
    if ( type == "cslo" ) {
        return eAction::Slots;
    }
    if ( type == "crou" ) {
        return eAction::Roulette;
    }
    if ( type == "cpok" ) {
        return eAction::Poker;
    }
    if ( type == "sitgo" ) {
        return eAction::SitAndGo;
    }
    if ( type == "cvip" ) {
        return eAction::VIP;
    }
    if ( type == "shop" ) {
        return eAction::Shop;
    }
    if ( type == "avat" ) {
        return eAction::Dressing;
    }
    // A machine / a table of the loaded room.
    const size_t base = scene.getSelectables().size() - places.size();
    if ( selected >= static_cast<int32_t>( base ) && selected - base < static_cast<int32_t>( places.size() ) ) {
        const GamePlace &place = places[selected - base];
        fillInteraction( place, selected - static_cast<int32_t>( base ) );
        return place.Action;
    }
    OTDU_LOG_INFO( "CasinoInterior: '%s' selected in the %s room (nothing to do)\n", type.c_str(),
                   RoomName( room ) );
    return eAction::None;
}

void CasinoInterior::tickBackground( float deltaTime ) {
    if ( bActive ) {
        scene.tick( deltaTime );
        handleEndEvents();
        rankings.update( scene, walk.getPosition(), false, deltaTime );
    }
}

CasinoInterior::eAction CasinoInterior::selectPlace( int32_t index ) {
    if ( !bActive || index < 0 || index >= static_cast<int32_t>( places.size() ) ) {
        return eAction::None;
    }
    const GamePlace &place = places[index];
    fillInteraction( place, index );
    return place.Action;
}

void CasinoInterior::fillInteraction( const GamePlace &place, int32_t index ) {
    pending = Interaction();
    pending.Action = place.Action;
    pending.Selectable = place.Selectable;
    pending.EntityName = place.EntityName;
    pending.GameId = place.GameId;
    pending.Index = index;
    pending.Price = place.Price;
    pending.CameraGame = place.CameraGame;
    pending.CameraGameHash = place.CameraGameHash;
    pending.NumReels = place.NumReels;
    pending.ReelMaterial = place.ReelMaterial;
    pending.AtlasColumns = place.AtlasColumns;
    pending.AtlasRows = place.AtlasRows;
    pending.TimeBeforeFirstStop = place.TimeBeforeFirstStop;
    pending.TimeAfterPrevSpinner = place.TimeAfterPrevSpinner;
    pending.SpinnerType = place.SpinnerType;
    pending.RotationSpeed = place.RotationSpeed;
    pending.Spinners = place.Spinners;
    pending.ReelStopAfter = place.ReelStopAfter;
    pending.Neons = place.Neons;
    pending.Combos = place.Combos;
    pending.TriggerHash = place.TriggerHash;
    pending.TriggerClip = place.TriggerClip;
    pending.SlotScreenHash = place.SlotScreenHash;
    pending.LinesScreenHash = place.LinesScreenHash;
}

bool CasinoInterior::focusCamera( uint64_t cameraHash ) {
    EdenSceneView::Camera shot;
    if ( !bActive || cameraHash == 0 || !scene.getEntityCamera( cameraHash, shot ) ) {
        return false;
    }
    shot.Near = 0.05f; // the camera stands a hand away from the glass
    scene.setCameraOverride( &shot );
    bCameraFocused = true;
    focusedCameraHash = cameraHash;
    OTDU_LOG_INFO( "CasinoInterior: looking through camera 0x%llx at (%.2f, %.2f, %.2f) (fov %.3f)\n",
                   static_cast<unsigned long long>( cameraHash ), shot.Position.x, shot.Position.y, shot.Position.z,
                   shot.Fov );
    return true;
}

int32_t CasinoInterior::getPlaceReels( int32_t place ) const {
    return place >= 0 && place < static_cast<int32_t>( places.size() ) ? static_cast<int32_t>( places[place].Spinners.size() )
                                                                       : 0;
}

bool CasinoInterior::setReelScroll( int32_t place, int32_t reel, float u, float v, float blur ) {
    if ( !bActive || place < 0 || place >= static_cast<int32_t>( places.size() ) || reel < 0 ||
         reel >= static_cast<int32_t>( places[place].Spinners.size() ) ) {
        return false;
    }
    EdenModel *pModel = scene.findPropModel( places[place].Spinners[reel] );
    if ( pModel == nullptr ) {
        return false;
    }
    // UV_symbol_space (s, vmax): the shader scrolls a UV1 vertex with column
    // k = u in (k s, (k + 1) s] by scroll_uvk while v < vmax. One offset for
    // the whole strip: s and vmax past the UV range, so every vertex is
    // column 0 [INF: the retail per-column split is not needed to show one
    // stop position].
    const Vector4f offset( u, v, 0.0f, 0.0f );
    for ( EdenMaterial &material : pModel->getMaterials() ) {
        material.StageConstants[0]["scroll_uv0"] = { offset };
        material.StageConstants[0]["scroll_uv1"] = { offset };
        material.StageConstants[0]["scroll_uv2"] = { offset };
        material.StageConstants[0]["UV_symbol_space"] = { Vector4f( 1.0e6f, 1.0e6f, 0.0f, 0.0f ) };
        material.StageConstants[1]["blur_blend_factor"] = { Vector4f( blur, blur, blur, blur ) };
    }
    return true;
}

bool CasinoInterior::setReelSlots( int32_t place, int32_t reel, const float ( &offsets )[3][2], float cellU, float blur ) {
    if ( !bActive || place < 0 || place >= static_cast<int32_t>( places.size() ) || reel < 0 ||
         reel >= static_cast<int32_t>( places[place].Spinners.size() ) ) {
        return false;
    }
    EdenModel *pModel = scene.findPropModel( places[place].Spinners[reel] );
    if ( pModel == nullptr ) {
        return false;
    }
    for ( EdenMaterial &material : pModel->getMaterials() ) {
        material.StageConstants[0]["scroll_uv0"] = { Vector4f( offsets[0][0], offsets[0][1], 0.0f, 0.0f ) };
        material.StageConstants[0]["scroll_uv1"] = { Vector4f( offsets[1][0], offsets[1][1], 0.0f, 0.0f ) };
        material.StageConstants[0]["scroll_uv2"] = { Vector4f( offsets[2][0], offsets[2][1], 0.0f, 0.0f ) };
        material.StageConstants[0]["UV_symbol_space"] = { Vector4f( cellU, 1.0e6f, 0.0f, 0.0f ) };
        material.StageConstants[1]["blur_blend_factor"] = { Vector4f( blur, blur, blur, blur ) };
    }
    return true;
}

void CasinoInterior::flashCombo( int32_t combination, float seconds ) {
    if ( !bActive || pending.Combos.empty() || combination < 0 ) {
        return;
    }
    scene.flashNeon( pending.Combos[static_cast<size_t>( combination ) % pending.Combos.size()], seconds );
}

int32_t CasinoInterior::getComboCount() const {
    return static_cast<int32_t>( pending.Combos.size() );
}

void CasinoInterior::setMachineNeons( IntroCinematic::eNeonState state ) {
    if ( !bActive ) {
        return;
    }
    for ( uint64_t neon : pending.Neons ) {
        scene.setNeonState( neon, state );
    }
}

bool CasinoInterior::setHandleTime( float time ) {
    if ( !bActive || pending.TriggerHash == 0 ) {
        return false;
    }
    return scene.setPropClip( pending.TriggerHash, pending.TriggerClip, time );
}

float CasinoInterior::getHandleDuration() {
    return bActive && pending.TriggerClip != 0 ? scene.getPropClipDuration( pending.TriggerClip ) : 0.0f;
}

void CasinoInterior::setScreenTextures( GPUTexture *pIdleScreen, GPUTexture *pPlayedScreen, GPUTexture *pLines,
                                        uint64_t stamp ) {
    // The screens' TD_NEONDECAL material ('slotscreen' / 'slscreen' /
    // 'slscreen_3R') has two slots: 0 NEONGLOW (a 'BLUE' placeholder) and 1
    // the colour - the CasinoFlashScreen's BitmapId is 1 [DATA: -logslothud
    // dumps the slot types].
    if ( !bActive ) {
        return;
    }
    const uint64_t played = pending.Action == eAction::SlotMachine ? pending.SlotScreenHash : 0;
    for ( const GamePlace &place : places ) {
        if ( place.Action != eAction::SlotMachine ) {
            continue;
        }
        if ( place.SlotScreenHash != 0 ) {
            const bool bPlayed = place.SlotScreenHash == played && pPlayedScreen != nullptr;
            GPUTexture *pScreen = bPlayed ? pPlayedScreen : pIdleScreen;
            const uint64_t hash = stamp ^ ( bPlayed ? 0x51071u : 0x1d1eu );
            scene.setPropTexture( place.SlotScreenHash, nullptr, 1, pScreen, hash );
            // TD_NEONDECAL: emissive = dp4( NEONGLOW, AnimValue ) x
            // WeatherEmissive (eden_neons.h); the screen glows on its own: a
            // white glow (slot 0 empty -> white), channel 0, strength 1 [INF].
            scene.setPropTexture( place.SlotScreenHash, nullptr, 0, nullptr, hash ^ 0x6c0u );
            scene.setPropEmissive( place.SlotScreenHash, 1.0f );
        }
        if ( place.LinesScreenHash != 0 && pLines != nullptr ) {
            scene.setPropTexture( place.LinesScreenHash, nullptr, 1, pLines, stamp ^ 0x11e5u );
            scene.setPropTexture( place.LinesScreenHash, nullptr, 0, nullptr, stamp ^ 0x11e5u ^ 0x6c0u );
            scene.setPropEmissive( place.LinesScreenHash, 1.0f );
        }
    }
}

void CasinoInterior::releaseCamera() {
    if ( bCameraFocused ) {
        scene.setCameraOverride( nullptr );
        bCameraFocused = false;
    }
}

void CasinoInterior::drawOffscreen() {
    if ( bActive ) {
        rankings.drawOffscreen( scene );
        scene.renderOffscreen();
    }
}

void CasinoInterior::draw() {
    if ( bActive ) {
        scene.draw();
    }
}

void CasinoInterior::showMessage( const std::string &text ) {
    if ( scene.isLoaded() ) {
        scene.showTutorial( text );
    }
}

