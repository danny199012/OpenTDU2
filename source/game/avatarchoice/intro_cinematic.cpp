#include "shared.h"
#include "intro_cinematic.h"
#include "render/eden/eden_neons.h"

#include "audio/gs_audio.h"
#include "config/gs_config.h"

#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/flash_localization.h"
#include "game/avatar/speech_audio.h"
#include "game/avatar/speech_synth.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "core/xmb.h"
#include "render/eden/eden_planar_reflections.h"

#include <unordered_set>

namespace {
constexpr uint32_t kTrackAnimation = 0xdcd2e210; // "Animation"
constexpr uint32_t kTrackVisibility = 0xd2df4b2c; // "Visibility" (byte keys, 1 = shown)
constexpr uint32_t kTrackMorph = 0x70726fb5;     // "Morph" (FaceFX)
constexpr uint16_t kRefMorph = 0x0306;           // .sce REFS FaceFX animation
constexpr float kFaceFxFade = 0.2f;              // DAT_00ede428 (FUN_00b77600 / FUN_00b77b20)
// CinProps bits (intro_spec 7.2).
constexpr uint32_t kCinPropLoop = 0x2;
constexpr uint32_t kCinPropNoCamera = 0x10;

Quaternionf Conjugate( const Quaternionf &q ) { return Quaternionf( -q.x, -q.y, -q.z, q.w ); }

std::string SceneFileName( const std::string &path ) {
    const size_t slash = path.find_last_of( "\\/" );
    std::string file = slash != std::string::npos ? path.substr( slash + 1 ) : path;
    const size_t dot = file.find_last_of( '.' );
    if ( dot != std::string::npos ) {
        file = file.substr( 0, dot );
    }
    return file;
}

uint32_t PackColor( const Vector3f &rgb, float alpha ) {
    auto channel = []( float value ) { return static_cast<uint32_t>( Clamp( value, 0.0f, 1.0f ) * 255.0f + 0.5f ); };
    return channel( rgb.x ) | ( channel( rgb.y ) << 8 ) | ( channel( rgb.z ) << 16 ) | ( channel( alpha ) << 24 );
}

} // namespace

IntroCinematic::IntroCinematic() {}

IntroCinematic::~IntroCinematic() { unload(); }

bool IntroCinematic::load() { return load( Setup() ); }

bool IntroCinematic::load( const Setup &newSetup ) {
    unload();
    setup = newSetup;
    const std::string bankPath = gpFile->getBankFolder() + setup.Bank;
    if ( !introBank.load( bankPath.c_str() ) ) {
        OTDU_LOG_WARN( "IntroCinematic: '%s' not found\n", bankPath.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( introBank, introBank.getPath().c_str() );
    introHashcodes.load( introBank );
    const BankEntry *pEntities = introBank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "IntroCinematic: entities.xmb not found\n" );
        return false;
    }
    loadDefinitions( entities );
    if ( definitions.empty() ) {
        OTDU_LOG_WARN( "IntroCinematic: no cinematic in '%s'\n", setup.Bank.c_str() );
        return false;
    }
    sounds.load( introBank );

    // Frontend intro.vmf: credits (generik1..5), "Select your character"
    // (label avatar), walk mode (wm_home).
    ui.setFSCommandCallback( [this]( const std::string &command, const std::string &args ) {
        if ( command == "sbdd" ) {
            // Handled by the engine after the action block (the movie sets
            // its own defaults right after the FSCommand, e.g. m_EnterCar_).
            bPendingUiLocalization = true;
        } else if ( commandCallback ) {
            commandCallback( command, args );
        }
    } );
    ui.load( gpFlash->getMovie( setup.UiMovie.c_str() ) );
    if ( ui.isLoaded() ) {
        ApplyFlashLocalization( ui );
        bPendingUiLocalization = false;
        // FUN_00da92e0 / FUN_00da7f40: no casino save data; PC platform
        // (validate glyph, sprite 19).
        ui.setVariableBool( "/:IsCasinoSaveData", false );
        ui.setVariableNumber( "/:WhatPlatform", 1.0 );
        ui.setVariableNumber( "/:IsTransition", 1.0 );
        OTDU_LOG_DEBUG( "IntroCinematic: INTRO fake='%s' IsCasinoSaveData='%s' CasinoAvatar='%s'\n",
                        ui.getVariable( "/:fake" ).c_str(), ui.getVariable( "/:IsCasinoSaveData" ).c_str(),
                        ui.getVariable( "/:CasinoAvatar" ).c_str() );
    }
    // Cinematic start (FUN_00b3eba0, CinProps bit 4 clear): black stripes on,
    // only in the 4:3 screen ratio mode (DAT_00fe4c78 == 0).
    // GENERAL.vmf: letterbox and the objective box (pop_goal).
    general.setFSCommandCallback( [this]( const std::string &command, const std::string &args ) {
        if ( command == "sbdd" ) {
            ApplyFlashLocalization( general );
        } else if ( generalCommandCallback ) {
            generalCommandCallback( command, args );
        }
    } );
    if ( general.load( gpFlash->getMovie( "general" ) ) ) {
        ApplyFlashLocalization( general );
        if ( CmdLine::HasArg( "-dumpgeneral" ) ) {
            general.dumpClips( "/", CmdLine::GetArgInt( "-dumpgeneral", 2 ) );
        }
        if ( gpConfig->getVideoSettings().ScreenRatio == 0 ) {
            general.gotoLabel( "/black_stripes", "on", false );
        }
    }

    // Cinematic scene space -> island: the house spot (spots.bin), world =
    // local * RotationY(heading) * Translation(position) (D3D row vectors).
    // -nointeriorworld: the streamed world outside the windows off, the
    // closed room back in its own frame at the island origin (the A/B).
    static const bool bInteriorWorld = !CmdLine::HasArg( "-nointeriorworld" );
    Vector3f housePosition( 0.0f, 0.0f, 0.0f );
    float heading = 0.0f;
    if ( !setup.HouseSpot.empty() && bInteriorWorld && !findHouseSpot( housePosition, heading ) ) {
        OTDU_LOG_WARN( "IntroCinematic: spot '%s' not found\n", setup.HouseSpot.c_str() );
    }
    sceneRotation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), heading );
    sceneOrigin = Matrix4x4f::Translation( housePosition ) * Matrix4x4f::FromQuaternion( sceneRotation );
    spotOrigin = sceneOrigin;
    if ( setup.bSceneAt ) {
        sceneRotation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), setup.SceneAtHeading );
        sceneOrigin = Matrix4x4f::Translation( setup.SceneAtPosition ) * Matrix4x4f::FromQuaternion( sceneRotation );
        // The scene frame is off the spot frame: the car floor queries go
        // through the world (fitOnSceneFloor) when the spot collision is
        // loaded (Setup::bExteriorCollision).
        bSceneMoved = true;
        OTDU_LOG_INFO( "IntroCinematic: cinematic frame at (%.2f, %.2f, %.2f) heading %.4f (spot frame kept for the exterior)\n",
                       setup.SceneAtPosition.x, setup.SceneAtPosition.y, setup.SceneAtPosition.z, setup.SceneAtHeading );
    }
    sounds.setSceneOrigin( sceneOrigin );
    collisionLift = 0.0f;
    bSceneMoved = false;
    const int32_t radius = CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1;
    // The streamed interior (bStreamWorld): the sectors load around the spot
    // the room stands at; the ocean stays keyed on bWorld (the shops inland).
    const bool bStream = setup.bWorld || ( setup.bStreamWorld && bInteriorWorld );
    const char *pWorldIsland = !setup.HouseIsland.empty() ? setup.HouseIsland.c_str() : "ibiza";
    // No haze or distance fog in the intro (the owner's call, 2026-11-04:
    // the 'Intro' weather's Skydome_haze 0.7 + fog 0..1200 m read as a grey
    // wash over the bay and its far trees). -introhaze / -introfog restore them.
    view.setHazeEnabled( CmdLine::HasArg( "-introhaze" ) );
    view.setFogEnabled( CmdLine::HasArg( "-introfog" ) );
    if ( !view.initialize( pWorldIsland, housePosition, bStream ? radius : -1 ) ) {
        return false;
    }
    view.setOceanEnabled( setup.bWorld );
    // A closed room (no island, no streaming: the casino rooms, the intro
    // garage, the islandless interiors) draws no island ground either. The
    // area tiles load whatever the radius, and the room's frame sits at the
    // island origin: in the casino hall the Ibiza tile there cut through the
    // floor at -0.6 m and hid the sunken red carpet and the car's podium
    // (owner report 2026-11-05; -noareas showed them). -closedroomareas = A/B.
    view.getWorld().setAreasEnabled( bStream || CmdLine::HasArg( "-closedroomareas" ) );
    view.setDrawCallback(
        [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) { drawSets( renderer, pass ); } );
    loadSets( entities );
    loadFurniture( entities );
    loadPnjs( entities );
    if ( !pnjs.empty() && !CmdLine::HasArg( "-nofacefx" ) && faceFx.load() ) {
        faceFx.addAnimSets( introBank );
    }
    // -lognodes: every node name of both face graphs. The names are the curve
    // names of a FaceFX animation (.fxe or a synthesized one), so this is how a
    // phoneme target is checked against the actor's data instead of guessed.
    if ( CmdLine::HasArg( "-lognodes" ) ) {
        for ( int32_t sex = 0; sex < 2; sex++ ) {
            const FaceFxActor *pFaceActor = faceFx.getActor( sex == 1 );
            if ( pFaceActor == nullptr ) {
                continue;
            }
            OTDU_LOG_INFO( "FaceFX nodes: %s, %zu\n", pFaceActor->getName().c_str(),
                           pFaceActor->getNodes().size() );
            for ( const FaceFxActor::Node &node : pFaceActor->getNodes() ) {
                OTDU_LOG_INFO( "  node %s\n", node.Name.c_str() );
            }
        }
    }
    // -say "<text>": procedural lip sync for dialog the game never recorded
    // (game/avatar/speech_synth.*). Built once; applyFaceFx evaluates it on
    // the scene time instead of the .sce Morph track's baked animation.
    if ( const char *pSay = CmdLine::GetArgValue( "-say" ) ) {
        bSay = !CmdLine::HasArg( "-nofacefx" ) && SpeechAnimation::Build( pSay, sayAnim );
        if ( bSay ) {
            sayText = pSay;
            OTDU_LOG_INFO( "IntroCinematic: -say \"%s\" -> %zu curves, %.2f s%s\n", pSay,
                           sayAnim.Curves.size(), sayAnim.EndTime,
                           faceFx.isLoaded() ? "" : " (no FaceFX actor: silent)" );
        } else {
            OTDU_LOG_WARN( "IntroCinematic: -say \"%s\" built nothing (empty text?)\n", pSay );
        }
    }
    // -saywav "<file.wav>": the same lip sync driven by the waveform instead of
    // the letters (game/avatar/speech_audio.*) - the path a microphone or a
    // network voice stream uses, minus the capture. The mouth shapes go in as
    // live registers, so there is no animation asset at all.
    if ( const char *pSayWav = CmdLine::GetArgValue( "-saywav" ) ) {
        bSayWav = !CmdLine::HasArg( "-nofacefx" ) && sayWav.load( pSayWav );
        if ( bSayWav ) {
            OTDU_LOG_INFO( "IntroCinematic: -saywav '%s' -> %.2f s of mouth shapes%s\n", pSayWav,
                           sayWav.getDuration(), faceFx.isLoaded() ? "" : " (no FaceFX actor: silent)" );
        } else {
            OTDU_LOG_WARN( "IntroCinematic: -saywav '%s' not loaded\n", pSayWav );
        }
    }
    loadVolumes( entities );
    loadLights( entities );
    collision = HavokCollision();
    for ( const BankEntry *pShk : introBank.findEntriesByExtension( "shk" ) ) {
        if ( LoadHavokCollision( pShk->pData, pShk->Size, collision, true ) ) {
            OTDU_LOG_INFO( "IntroCinematic: collision %s: %zu triangles, %zu boxes, %zu capsules\n",
                           pShk->Name.c_str(), collision.Indices.size() / 3, collision.Boxes.size(),
                           collision.Capsules.size() );
        }
        break;
    }
    if ( setup.bExteriorCollision && collision.getNumTriangles() == 0 && collision.Boxes.empty() ) {
        for ( const BankEntry *pShk : exteriorBank.findEntriesByExtension( "shk" ) ) {
            if ( LoadHavokCollision( pShk->pData, pShk->Size, collision, true ) ) {
                OTDU_LOG_INFO( "IntroCinematic: exterior collision %s: %zu triangles, %zu boxes\n", pShk->Name.c_str(),
                               collision.Indices.size() / 3, collision.Boxes.size() );
                break;
            }
        }
    }
    applyShadowConfig( "CINE_DEFAULT" );
    camera = EdenSceneView::Camera();
    camera.Position = housePosition;
    bLoaded = true;
    return true;
}

void IntroCinematic::unload() {
    stop( main );
    stopBackground();
    sounds.stop( musicVoices );
    sounds.unload();
    ui.unload();
    general.unload();
    bLogoTv = bLogoSc = false;
    view.clearPlanarReflections();
    view.clearLightmapMaterials();
    view.destroy();
    car.unload();
    showCars.clear();
    seatedShowCar = -1;
    displayCars.clear();
    shopPosCams.clear();
    posCamCameras.clear();
    bCarEnvironment = false;
    bCarStaticCube = false;
    interiorSetStart = 0;
    entityPlacements.clear();
    cinCarHashcodes.clear();
    carActorName.clear();
    actorMessages.clear();
    sets.clear();
    setIsExterior.clear();
    reflectSets.clear();
    props.clear();
    roomMaterialOriginals.clear();
    retiredFurniture.clear();
    neonAnims.clear(); // index the props
    neonCurves.clear();
    neonClock = 0.0f;
    furnitureBanks.clear();
    pnjs.clear();
    player = Pnj();
    animations.clear();
    entityCameras.clear();
    avatarLibrary.unload();
    faceFx.unload();
    if ( !exteriorBank.getEntries().empty() ) {
        gpRender->getTextureCache().releaseOwner( exteriorBank.getPath().c_str() );
    }
    exteriorBank.unload();
    if ( !interiorBank.getEntries().empty() ) {
        gpRender->getTextureCache().releaseOwner( interiorBank.getPath().c_str() );
    }
    interiorBank.unload();
    if ( !introBank.getEntries().empty() ) {
        gpRender->getTextureCache().releaseOwner( introBank.getPath().c_str() );
    }
    introBank.unload();
    definitions.clear();
    endEvents.clear();
    weatherName.clear();
    modifierName.clear();
    volumes.clear();
    lights.clear();
    selectables.clear();
    goalTimer = -1.0f;
    bPlayerSeated = false;
    bDriverPose = false;
    driverBank.unload();
    bCameraOverride = false;
    bLoaded = false;
}

// Entity__Cinematic: CinFile[] rsc (subtype 16 .cin, 17 .sce), CinProps,
// DefaultShadowConfig, additionalsAnims[i].xan[0] and
// additionalsStyleAnims[s] { style; anims[i].xan[0] }.
void IntroCinematic::loadDefinitions( const XmbDocument &entities ) {
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Cinematic" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Cinematic", i );
        auto pDef = std::make_unique<Definition>();
        pDef->Name = entry["__Name"].asString();
        pDef->CinProps = entry.getArraySize( "CinProps" ) > 0
                             ? static_cast<uint32_t>( entry.getArrayElement( "CinProps", 0 ).asInt() )
                             : 0u;
        pDef->ShadowConfig = entry["DefaultShadowConfig"].asString();
        const BankEntry *pCin = nullptr;
        for ( uint32_t j = 0; j < entry.getArraySize( "CinFile" ) && pCin == nullptr; j++ ) {
            XmbValue file = entry.getArrayElement( "CinFile", j );
            if ( file["rscSubType"].asInt() == 16 ) {
                pCin = introHashcodes.find( file["rsc"].asUInt64(), -1, 16 );
                if ( pCin == nullptr ) {
                    pCin = introHashcodes.find( file["rsc"].asUInt64() );
                }
            }
        }
        if ( pCin == nullptr || !pDef->Cin.parse( pCin->pData, pCin->Size ) ) {
            OTDU_LOG_WARN( "IntroCinematic: .cin of '%s' not found\n", pDef->Name.c_str() );
            continue;
        }
        // Scenes: the .sce named by the scene references (source path).
        for ( int32_t reference : pDef->Cin.getSceneReferences() ) {
            CinematicScene scene;
            if ( reference >= 0 && reference < static_cast<int32_t>( pDef->Cin.getReferences().size() ) ) {
                const std::string file = SceneFileName( pDef->Cin.getReferences()[reference].Text );
                const BankEntry *pScene = introBank.findEntry( file.c_str(), "sce" );
                if ( pScene == nullptr || !scene.parseScene( pScene->pData, pScene->Size ) ) {
                    OTDU_LOG_WARN( "IntroCinematic: scene '%s' not found\n", file.c_str() );
                }
            }
            pDef->Scenes.push_back( std::move( scene ) );
        }
        // Style anims: base additionalsAnims[i] -> anims[i] of the style.
        const uint32_t numAnims = entry.getArraySize( "additionalsAnims" );
        for ( uint32_t s = 0; s < entry.getArraySize( "additionalsStyleAnims" ); s++ ) {
            XmbValue style = entry.getArrayElement( "additionalsStyleAnims", s );
            const std::string styleName = style["style"].asString();
            const int32_t styleIndex = styleName == "m" ? 0 : styleName == "w" ? 1 : -1;
            if ( styleIndex < 0 ) {
                continue;
            }
            for ( uint32_t a = 0; a < numAnims && a < style.getArraySize( "anims" ); a++ ) {
                XmbValue base = entry.getArrayElement( "additionalsAnims", a );
                XmbValue styled = style.getArrayElement( "anims", a );
                if ( base.getArraySize( "xan" ) == 0 || styled.getArraySize( "xan" ) == 0 ) {
                    continue;
                }
                const uint64_t from = base.getArrayElement( "xan", 0 )["rsc"].asUInt64();
                const uint64_t to = styled.getArrayElement( "xan", 0 )["rsc"].asUInt64();
                if ( from != 0 && to != 0 ) {
                    pDef->StyleAnims[styleIndex][from] = to;
                }
            }
        }
        OTDU_LOG_DEBUG( "IntroCinematic: %s %.2f s, %zu shots, %zu scenes, props 0x%x\n", pDef->Name.c_str(),
                        pDef->Cin.getDuration(), pDef->Cin.getShots().size(), pDef->Scenes.size(), pDef->CinProps );
        definitions.push_back( std::move( pDef ) );
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu cinematics\n", definitions.size() );
}

const IntroCinematic::Definition *IntroCinematic::findDefinition( const std::string &name ) const {
    for ( const auto &pDef : definitions ) {
        if ( pDef->Name == name ) {
            return pDef.get();
        }
    }
    return nullptr;
}

uint32_t IntroCinematic::getCinProps( const std::string &name ) const {
    const Definition *pDef = findDefinition( name );
    return pDef != nullptr ? pDef->CinProps : 0;
}

bool IntroCinematic::setCinematicOrigin( const std::string &name, const Matrix4x4f &origin ) {
    for ( auto &pDef : definitions ) {
        if ( pDef->Name != name ) {
            continue;
        }
        for ( CinematicScene &scene : pDef->Scenes ) {
            scene.setOrigin( origin );
        }
        return true;
    }
    return false;
}

std::vector<std::string> IntroCinematic::getCinematicNames() const {
    std::vector<std::string> names;
    for ( const auto &pDef : definitions ) {
        names.push_back( pDef->Name );
    }
    return names;
}

const std::string &IntroCinematic::getMainName() const {
    static const std::string kEmpty;
    return main.pDef != nullptr ? main.pDef->Name : kEmpty;
}

// FUN_00b402a0 / FUN_00b3ff00: time = start time, messages and Fx at the
// start time dispatched.
void IntroCinematic::start( Playing &playing, const Definition *pDef, bool bLoop, bool bNoCamera, float startTime ) {
    stop( playing );
    playing.pDef = pDef;
    playing.bLoop = bLoop;
    playing.bNoCamera = bNoCamera;
    playing.Time = Clamp( startTime, 0.0f, Max( pDef->Cin.getDuration() - 0.001f, 0.0f ) );
    const bool bMain = &playing == &main;
    if ( bMain && !bNoCamera && !pDef->ShadowConfig.empty() ) {
        // FUN_00b3eba0: the entity shadow config (bit 4 clear).
        applyShadowConfig( pDef->ShadowConfig );
    }
    // A start time > 0 (-cinetime tests) is a seek: only the state messages
    // (weather, modifier, shadows) before it are applied.
    seekTime = playing.Time > 0.0f ? playing.Time : -1.0f;
    processMessages( playing, -1.0f, playing.Time, bMain );
    seekTime = -1.0f;
    if ( playing.Time <= 0.0f ) {
        updateFx( playing, -1.0f, playing.Time );
    }
    if ( bMain ) {
        actorMessages.clear();
        collectActorMessages( playing, -1.0f, playing.Time );
    }
    playing.PreviousTime = playing.Time;
    if ( bMain ) {
        // The authored captions of this cinematic (accessibility; retail has
        // none for the cutscene speech). -cinesubdump writes the skeleton to
        // fill (never over an existing csv).
        bSubtitles = CmdLine::HasArg( "-subtitles" )
                         ? CmdLine::GetArgInt( "-subtitles", 1 ) != 0
                         : true;
        subtitles.load( pDef->Name );
        if ( CmdLine::HasArg( "-cinesubdump" ) ) {
            CineSubtitles::DumpSkeleton( pDef->Name, pDef->Cin, pDef->Scenes,
                                         sounds );
        }
    }
}

void IntroCinematic::stop( Playing &playing ) {
    if ( !playing.Voices.empty() ) {
        sounds.stop( playing.Voices );
    }
    playing.Voices.clear();
    playing.pDef = nullptr;
    playing.bEnded = false;
    playing.Time = 0.0f;
    playing.PreviousTime = -1.0f;
}

bool IntroCinematic::playMain( const std::string &name, bool bStopPrevious, float startTime ) {
    // FUN_00da7fd0: the previous main cinematic is stopped (no event 7).
    const Definition *pDef = findDefinition( name );
    if ( pDef == nullptr ) {
        OTDU_LOG_WARN( "IntroCinematic: cinematic '%s' not found\n", name.c_str() );
        return false;
    }
    (void)bStopPrevious;
    OTDU_LOG_DEBUG( "IntroCinematic: main %s\n", name.c_str() );
    start( main, pDef, ( pDef->CinProps & kCinPropLoop ) != 0, ( pDef->CinProps & kCinPropNoCamera ) != 0,
           startTime >= 0.0f ? startTime
           : main.pDef == nullptr && name == "C_INTRO" ? static_cast<float>( CmdLine::GetArgInt( "-cinetime", 0 ) )
                                                       : 0.0f );
    return true;
}

bool IntroCinematic::playBackground( const std::string &name, bool bLoop ) {
    // FUN_00da8040 (C_AVATAR) / FUN_00da8d90 (C_WALKMODE): CinProps |= 0x10,
    // loop forced.
    const Definition *pDef = findDefinition( name );
    if ( pDef == nullptr ) {
        OTDU_LOG_WARN( "IntroCinematic: cinematic '%s' not found\n", name.c_str() );
        return false;
    }
    OTDU_LOG_DEBUG( "IntroCinematic: background %s\n", name.c_str() );
    stopBackground( name );
    backgrounds.push_back( std::make_unique<Playing>() );
    start( *backgrounds.back(), pDef, bLoop, true, 0.0f );
    return true;
}

void IntroCinematic::stopMain() { stop( main ); }

void IntroCinematic::stopBackground() {
    for ( auto &pPlaying : backgrounds ) {
        stop( *pPlaying );
    }
    backgrounds.clear();
}

void IntroCinematic::stopBackground( const std::string &name ) {
    for ( auto it = backgrounds.begin(); it != backgrounds.end(); ) {
        if ( ( *it )->pDef != nullptr && ( *it )->pDef->Name == name ) {
            stop( **it );
            it = backgrounds.erase( it );
        } else {
            ++it;
        }
    }
}

bool IntroCinematic::isBackgroundPlaying( const std::string &name ) const {
    for ( const auto &pPlaying : backgrounds ) {
        if ( pPlaying->pDef != nullptr && pPlaying->pDef->Name == name ) {
            return true;
        }
    }
    return false;
}

void IntroCinematic::skipMain() {
    // Skip: jump to the end pose, the end is then reached on the next
    // update (listener event 7) [INF: the skip input path is encrypted].
    if ( main.pDef == nullptr ) {
        return;
    }
    sounds.stop( main.Voices );
    main.Time = Max( main.pDef->Cin.getDuration() - 0.001f, 0.0f );
    main.PreviousTime = main.Time;
    main.bLoop = false;
}

void IntroCinematic::seekMain( float time ) {
    // The scrub: a restart at the new time through start()'s seek path (the
    // state messages up to it are applied, the voices restart there).
    if ( main.pDef == nullptr ) {
        return;
    }
    start( main, main.pDef, main.bLoop, main.bNoCamera, time );
}

const Cinematic *IntroCinematic::getMainCinematic() const {
    return main.pDef != nullptr ? &main.pDef->Cin : nullptr;
}

std::vector<std::string> IntroCinematic::takeEndEvents() {
    std::vector<std::string> result;
    result.swap( endEvents );
    return result;
}

// FUN_00b40350: t += dt; past duration - 0.001: end (event 7, stop) or loop
// (t = t <= duration ? 0.001 : t - duration, restart).
void IntroCinematic::advance( Playing &playing, float deltaTime, bool bMain ) {
    if ( playing.pDef == nullptr ) {
        return;
    }
    const Definition *pDef = playing.pDef;
    const float duration = pDef->Cin.getDuration();
    if ( duration <= 0.0f ) {
        // 0 s timeline (C_LIFE_SOUND): its Fx were started with it; nothing
        // to advance [INF: the restart of FUN_00b402f0 on such a timeline is
        // not traced].
        return;
    }
    const float from = playing.PreviousTime;
    float t = playing.Time + deltaTime;
    if ( t > duration - 0.001f ) {
        if ( !playing.bLoop ) {
            t = duration - 0.001f;
            processMessages( playing, from, t, bMain );
            updateFx( playing, from, t );
            if ( bMain ) {
                collectActorMessages( playing, from, t );
            }
            playing.Time = t;
            playing.PreviousTime = t;
            endEvents.push_back( pDef->Name );
            // Stop(1), CinEnd 3: end pose kept until the listener starts
            // the next cinematic.
            sounds.stop( playing.Voices );
            playing.bEnded = true;
            return;
        }
        processMessages( playing, from, duration, bMain );
        updateFx( playing, from, duration );
        t = t <= duration ? 0.001f : t - duration;
        // Restart (FUN_00b402f0): the Fx of the cinematic start again.
        sounds.stop( playing.Voices );
        processMessages( playing, -1.0f, t, bMain );
        updateFx( playing, -1.0f, t );
    } else {
        processMessages( playing, from, t, bMain );
        updateFx( playing, from, t );
        if ( bMain ) {
            collectActorMessages( playing, from, t );
        }
    }
    playing.Time = t;
    playing.PreviousTime = t;
}

void IntroCinematic::updateFx( Playing &playing, float from, float to ) {
    // .cin Fx events and the Fx tracks of the actors of the current scene
    // (voices of Tess in M_TESS_Tess.sce...).
    const Cinematic &cinematic = playing.pDef->Cin;
    sounds.update( cinematic, from, to, &playing.Voices );
    const Cinematic::Shot *pShot = cinematic.getShot( Max( to, 0.0f ) );
    if ( pShot == nullptr || pShot->Scene >= playing.pDef->Scenes.size() ) {
        return;
    }
    const Cinematic::Shot *pPreviousShot = from >= 0.0f ? cinematic.getShot( from ) : nullptr;
    const float sceneTime = pShot->getSceneTime( to );
    const float previousSceneTime =
        pPreviousShot == pShot && from >= 0.0f ? pShot->getSceneTime( from ) : pShot->CameraStart - 1e-4f;
    const CinematicScene &scene = playing.pDef->Scenes[pShot->Scene];
    sounds.updateSceneFx( scene, previousSceneTime, sceneTime, sceneOrigin * scene.getOrigin(), &playing.Voices );
}

void IntroCinematic::processMessages( const Playing &playing, float from, float to, bool bMain ) {
    // Only the main cinematic reaches the game mode listener (CinProps bit
    // 4 of the background cinematics).
    if ( !bMain || playing.pDef == nullptr ) {
        return;
    }
    const Cinematic &cinematic = playing.pDef->Cin;
    const auto &references = cinematic.getReferences();
    for ( const Cinematic::Event &event : cinematic.getMessages() ) {
        if ( !( event.Time > from && event.Time <= to ) && !( from < 0.0f && event.Time <= 0.0f && to >= 0.0f ) ) {
            continue;
        }
        if ( event.Reference < 0 || event.Reference >= static_cast<int32_t>( references.size() ) ) {
            continue;
        }
        const std::string &message = references[event.Reference].Text;
        if ( seekTime >= 0.0f && event.Time < seekTime - 0.5f &&
             ( message.rfind( "flsh:", 0 ) == 0 || message.rfind( "snd:", 0 ) == 0 ) ) {
            continue;
        }
        // Listener FUN_00da8040 (event 8), then FUN_009be090 -> FUN_00a91820.
        if ( message.rfind( "flsh:", 0 ) == 0 ) {
            const std::string label = message.substr( 5 );
            if ( label == "generik5" ) {
                // /:generik_date: localized string 0x139b.
                const char *pDate = gpDatabase != nullptr ? gpDatabase->getString( 0x139b ) : nullptr;
                ui.setVariable( "/:generik_date", pDate != nullptr ? pDate : "" );
            }
            ui.gotoLabel( "/", label, true );
        } else if ( message.rfind( "lgtv:", 0 ) == 0 || message.rfind( "lgsc:", 0 ) == 0 ) {
            applyLogoMessage( message, seekTime >= 0.0f && event.Time < seekTime - 0.5f );
        } else if ( message.rfind( "shad:", 0 ) == 0 ) {
            applyShadowConfig( message.substr( 5 ) );
        } else if ( message.rfind( "weat:", 0 ) == 0 ) {
            weatherName = message.substr( 5 );
            modifierName.clear();
        } else if ( message.rfind( "wmod:", 0 ) == 0 ) {
            modifierName = message.substr( 5 );
        } else if ( message == "snd:timestop" ) {
            if ( const auto *pSounds = sounds.findEmitter( "S_TimeStop" ) ) {
                sounds.play( *pSounds, Max( to - event.Time, 0.0f ) );
            }
        } else if ( message == "snd:music" ) {
            // The 4 party music emitters (looped).
            sounds.stop( musicVoices );
            for ( const char *pName : { "S_WALKMODE_Music", "S_WALKMODE_Music_01", "S_WALKMODE_Music_02",
                                        "S_WALKMODE_Music_03" } ) {
                if ( const auto *pSounds = sounds.findEmitter( pName ) ) {
                    sounds.play( *pSounds, Max( to - event.Time, 0.0f ), true, &musicVoices );
                }
            }
        }
    }
}

// The TV-show overlays of C_SOLAR_CROWN_I (GENERAL.vmf, drawn by draw()):
//   lgtv:open / lgtv:clos      -> /logo_TV  labels open (8) / close (26): the
//                                 Solar Crown TV logo that slides in top right;
//   lgsc:open:<name> / lgsc:clos -> /logo_SC labels open (9) / close (38): the
//                                 name caption bottom right, its text field
//                                 is /:PNJName (sprite 852) [DATA: the .cin
//                                 message texts, the sprite labels and the
//                                 text variable of GENERAL.vmf].
// The message listener that reads them is in the encrypted band [ENC];
// "-player" is the player's own caption (shot 7 cam_challenger, the camera
// on the avatar): /:PNJName takes /:PlayerName (set by GMCine) [INF: the
// '-' marks a variable, not a name; open_double (both names, sprites 852 +
// 854) is not cued by any cinematic of the tree].
void IntroCinematic::applyLogoMessage( const std::string &message, bool bSeekSkipped ) {
    if ( !general.isLoaded() ) {
        return;
    }
    const bool bTv = message.rfind( "lgtv:", 0 ) == 0;
    const std::string rest = message.substr( 5 );
    const bool bOpen = rest.rfind( "open", 0 ) == 0;
    if ( bTv ) {
        // A state: a seek past the cue still shows the logo.
        general.gotoLabel( "/logo_TV", bOpen ? "open" : "close", true );
        bLogoTv = bOpen || bLogoTv;
        return;
    }
    if ( bSeekSkipped ) {
        return; // a passing caption, not replayed by a seek
    }
    if ( bOpen ) {
        std::string name = rest.size() > 5 ? rest.substr( 5 ) : std::string();
        if ( name == "-player" ) {
            name = general.getVariable( "/:PlayerName" );
        }
        general.setVariable( "/:PNJName", name );
        bLogoSc = true;
    }
    general.gotoLabel( "/logo_SC", bOpen ? "open" : "close", true );
    OTDU_LOG_INFO( "CINE: %s -> %s\n", message.c_str(), bTv ? "/logo_TV" : "/logo_SC" );
}

void IntroCinematic::stopMusic() { sounds.stop( musicVoices ); }

void IntroCinematic::collectActorMessages( const Playing &playing, float from, float to ) {
    // Object "Message" tracks of the current shot's scene, keys in the
    // scene time range crossed (same range rule as the scene Fx tracks).
    const Cinematic &cinematic = playing.pDef->Cin;
    const Cinematic::Shot *pShot = cinematic.getShot( Max( to, 0.0f ) );
    if ( pShot == nullptr || pShot->Scene >= playing.pDef->Scenes.size() ) {
        return;
    }
    const Cinematic::Shot *pPreviousShot = from >= 0.0f ? cinematic.getShot( from ) : nullptr;
    const float sceneTime = pShot->getSceneTime( to );
    const float previousSceneTime =
        pPreviousShot == pShot && from >= 0.0f ? pShot->getSceneTime( from ) : pShot->CameraStart - 1e-4f;
    const CinematicScene &scene = playing.pDef->Scenes[pShot->Scene];
    std::vector<std::pair<float, ActorMessage>> crossed;
    for ( const CinematicScene::Object &object : scene.getObjects() ) {
        for ( const CinematicScene::Track &track : object.Tracks ) {
            if ( track.Id != CinematicScene::kTrackMessage || track.NumComponents < 1 ) {
                continue;
            }
            for ( size_t k = 0; k < track.Times.size(); k++ ) {
                const float t = track.Times[k];
                const bool bCrossed = from < 0.0f ? t <= sceneTime : ( t > previousSceneTime && t <= sceneTime );
                if ( !bCrossed ) {
                    continue;
                }
                uint32_t ref = 0;
                memcpy( &ref, &track.Values[k * track.NumComponents], sizeof( ref ) );
                if ( ref < scene.getReferences().size() ) {
                    crossed.push_back( { t, ActorMessage{ object.Name, scene.getReferences()[ref].Name } } );
                }
            }
        }
    }
    std::stable_sort( crossed.begin(), crossed.end(),
                      []( const auto &a, const auto &b ) { return a.first < b.first; } );
    for ( auto &entry : crossed ) {
        actorMessages.push_back( std::move( entry.second ) );
    }
}

std::vector<IntroCinematic::ActorMessage> IntroCinematic::takeActorMessages() {
    std::vector<ActorMessage> result;
    result.swap( actorMessages );
    return result;
}

bool IntroCinematic::playEmitter( const char *pName ) {
    if ( const auto *pSounds = sounds.findEmitter( pName ) ) {
        sounds.play( *pSounds );
        return true;
    }
    return false;
}

void IntroCinematic::tick( float deltaTime ) {
    if ( !neonAnims.empty() ) {
        updateNeons( deltaTime );
    }
    if ( !bLoaded ) {
        return;
    }
    lastDeltaTime = deltaTime;
    // The turntables (the casino hall's socle car).
    for ( DisplayCar &display : displayCars ) {
        if ( display.SpinRate == 0.0f ) {
            continue;
        }
        display.SpinAngle = std::fmod( display.SpinAngle + display.SpinRate * deltaTime, 6.2831853f );
        const Matrix4x4f turn =
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), display.SpinAngle ) );
        display.World = sceneOrigin * Matrix4x4f::Translation( display.Pivot ) * turn *
                        Matrix4x4f::Translation( Vector3f( 0.0f, 0.0f, 0.0f ) - display.Pivot ) * display.RestLocal;
    }
    // Side windows: the opw / clw cinematic messages request the state
    // (updateCarDoors), the glass slides there.
    car.updateWindows( deltaTime );
    if ( !main.bEnded ) {
        advance( main, deltaTime, true );
    }
    for ( auto &pPlaying : backgrounds ) {
        advance( *pPlaying, deltaTime, false );
    }
    for ( auto it = backgrounds.begin(); it != backgrounds.end(); ) {
        if ( ( *it )->bEnded ) {
            stop( **it );
            it = backgrounds.erase( it );
        } else {
            ++it;
        }
    }
    if ( goalTimer > 0.0f ) {
        // 30 s auto close (DAT_00ede1fc -> DAT_00f803b4).
        goalTimer -= deltaTime;
        if ( goalTimer <= 0.0f ) {
            closeGoal();
        }
    }
    ui.tick( deltaTime );
    if ( bPendingUiLocalization ) {
        ApplyFlashLocalization( ui );
        bPendingUiLocalization = false;
    }
    general.tick( deltaTime );
    view.tick( deltaTime );
}

void IntroCinematic::showGoal( const std::string &text ) {
    // FUN_00d61790: /:GoalTxt, pop_goal goto "open" (/:IsWalkmode false in
    // AVATAR_CHOICE: the non-WM animation), 30 s timer.
    if ( !general.isLoaded() ) {
        return;
    }
    general.setVariable( "/:GoalTxt", text );
    general.gotoLabel( "/pop_goal", "open", true );
    goalTimer = 30.0f;
}

void IntroCinematic::showTutorial( const std::string &text ) {
    // FUN_00d62ed0: FUN_00d62a90 sets /:TutText (the PC text _messpc_hc when
    // given and the device is not the X360 pad, else _mess_hc), then the
    // movie goes to "open_tuto" on /pop_tutorial (GENERAL.vmf sprite 647:
    // open_tuto_WM when _root.IsWalkMode; /:IsWalkmode stays false here, it
    // is only set for the "invi" message).
    if ( !general.isLoaded() ) {
        return;
    }
    general.setVariable( "/:TutText", text );
    general.gotoLabel( "/pop_tutorial", "open_tuto", true );
}

void IntroCinematic::closeTutorial() {
    // FUN_00d61600: pop_tutorial.close_popup().
    if ( general.isLoaded() ) {
        general.invoke( "/pop_tutorial", "close_popup", {} );
    }
}

float IntroCinematic::liftOntoFloor( const std::string &restEntity ) {
    auto it = entityPlacements.find( restEntity );
    if ( it == entityPlacements.end() ) {
        return 0.0f;
    }
    const Vector3f rest = it->second.first;
    float floorY = 0.0f;
    // Highest floor of the spot collision under a point 3 m above the
    // entity (below the hotel canopy) [INF].
    if ( !FindHavokFloor( collision, Vector3f( rest.x, rest.y + 3.0f, rest.z ), 0.0f, 10.0f, floorY ) ) {
        return 0.0f;
    }
    const float lift = floorY - rest.y;
    collisionLift = lift;
    bSceneMoved = true;
    sceneOrigin = Matrix4x4f::Translation( Vector3f( 0.0f, lift, 0.0f ) ) * spotOrigin;
    sounds.setSceneOrigin( sceneOrigin );
    OTDU_LOG_INFO( "IntroCinematic: %s at local y %.3f, spot floor %.3f: cinematic frame lifted by %.3f m\n",
                   restEntity.c_str(), rest.y, floorY, lift );
    return lift;
}

void IntroCinematic::placeEntityAt( const std::string &restEntity, const Vector3f &worldPosition, float worldHeading ) {
    auto it = entityPlacements.find( restEntity );
    if ( it == entityPlacements.end() ) {
        return;
    }
    // Entity frame (bank local): T(p) R(conj q) (Eden heading convention).
    const Vector3f p = it->second.first;
    const Quaternionf q = it->second.second;
    const Matrix4x4f entityLocal = Matrix4x4f::Translation( p ) * EdenRotationFromQuaternion( q );
    // Its yaw: the entity +z axis in the bank frame.
    const Vector3f forward = entityLocal.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    const float entityYaw = std::atan2( forward.x, forward.z );
    // Target frame of the entity: T(world) R_y(heading) with the same
    // convention as the spot transform (x' = x cos + z sin).
    const float yaw = worldHeading - entityYaw;
    const Quaternionf rotation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw );
    const Matrix4x4f frameRotation = Matrix4x4f::FromQuaternion( rotation );
    const Vector3f rotated = frameRotation.transformPoint( p );
    collisionLift = 0.0f;
    bSceneMoved = true;
    sceneRotation = rotation;
    sceneOrigin = Matrix4x4f::Translation( worldPosition - rotated ) * frameRotation;
    sounds.setSceneOrigin( sceneOrigin );
    OTDU_LOG_INFO( "IntroCinematic: %s placed at (%.2f, %.2f, %.2f) heading %.4f (frame yaw %.4f)\n", restEntity.c_str(),
                   worldPosition.x, worldPosition.y, worldPosition.z, worldHeading, yaw );
}

bool IntroCinematic::ReadSpot( const char *pName, SpotRecord &out ) {
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( ( gpFile->getBankFolder() + "islands/ibiza/level/spots.bin" ).c_str(), data ) ||
         data.size() < 0x20 || memcmp( data.data() + 0x10, "SPTA", 4 ) != 0 ) {
        return false;
    }
    uint32_t size = 0;
    memcpy( &size, data.data() + 0x14, 4 );
    const uint64_t id = GetIdentifier64bit( pName );
    for ( size_t offset = 0x20; offset + 0x110 <= data.size() && offset < 0x20 + size; offset += 0x110 ) {
        uint64_t recordId = 0;
        memcpy( &recordId, data.data() + offset, 8 );
        if ( recordId == id ) {
            memcpy( &out.Position, data.data() + offset + 0x10, 12 );
            memcpy( &out.Heading, data.data() + offset + 0x20, 4 );
            memcpy( &out.Position2, data.data() + offset + 0x28, 12 );
            memcpy( &out.Heading2, data.data() + offset + 0x38, 4 );
            out.Type = data[offset + 0x100];
            return true;
        }
    }
    return false;
}

bool IntroCinematic::getGroundHeight( float x, float z, float &outHeight ) const {
    return const_cast<EdenSceneView &>( view ).getWorld().getHeight( x, z, outHeight );
}

bool IntroCinematic::getCarWorld( Matrix4x4f &outWorld ) const {
    return car.isLoaded() && computeCarWorld( outWorld );
}

bool IntroCinematic::getCarLocal( Matrix4x4f &outLocal ) const {
    Matrix4x4f world;
    if ( !getCarWorld( world ) ) {
        return false;
    }
    outLocal = sceneOrigin.inverse() * world;
    return true;
}

bool IntroCinematic::getPnjScenePosition( size_t index, Vector3f &outPosition ) const {
    if ( index >= pnjs.size() || !pnjs[index].bVisible ) {
        return false;
    }
    outPosition = sceneOrigin.inverse().transformPoint( pnjs[index].World.getTranslation() );
    return true;
}

bool IntroCinematic::setPlayerSeated( bool bSeated ) {
    bPlayerSeated = bSeated;
    if ( !bSeated || bDriverPose ) {
        return bDriverPose || !bSeated;
    }
    // Entity__AvatarPlayer DriverMan / DriverWoman (avatarsdriver.bnk):
    // anim_blend_drive[0..6] = driveiklayer0..6.bas [DATA]. Layer 0 blends
    // stance_turn.xan (weight 1) with turn.xan (weight 0, the steering);
    // stance_turn.anm is the static wheel-straight pose (= turn.anm at
    // mid-clip, plus a ROOT key (0, -0.019, 0.059)) [DATA]. Layers 1 / 3
    // (stance.xan, empty), 2 (hand_size), 4 (hand_open, weight 0), 5 (gear
    // clips, weight 0) add nothing at rest; layer 6 holds the IK nodes
    // (updateSeatedPlayer) [DATA]. hand_size's sample time is not decoded:
    // the fingers keep the stance_turn grip [INF].
    const std::string path = gpFile->getBankFolder() + "scenarics/avatars/avatarsdriver.bnk";
    if ( driverBank.getEntries().empty() && !driverBank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "IntroCinematic: '%s' not found\n", path.c_str() );
        return false;
    }
    const BankEntry *pStance = driverBank.findEntry( "stance_turn.anm" );
    bDriverPose = pStance != nullptr && driverPose.parse( pStance->pData, pStance->Size );
    bDriverIKLoaded = false;
    return bDriverPose;
}

void IntroCinematic::setSeatedShowCar( int32_t showCarIndex ) {
    if ( showCarIndex != seatedShowCar ) {
        bDriverIKLoaded = false; // IKOffsetDB record of the new car
    }
    seatedShowCar = showCarIndex >= 0 && static_cast<size_t>( showCarIndex ) < showCars.size() ? showCarIndex : -1;
}

// Euro/Bnk/DataBase/IKOffsetDB.bin (FUN_00b97cc0 load, FUN_00b96ba0 parse
// [CODE]; layout docs/re/driver_seat_spec.md). Big endian (the PC parser
// byte-swaps when the magic reads "BDKI") [CODE + DATA]:
//   +0x00 u16 2, u16 3, u32 0, u32 ?, "IKDB"
//   +0x10 chunk "CAIK", u16 2 (sexes), u16 0, u32, u32
//   +0x20 u32 numCars, then per car: u32 _id_car,
//         2 x { f32 seat[5]; 27 x { f32 offset[3]; f32 unused[3] } },
//         11 x f32[3]
//   then chunk "BIIK" (bikes).
// seat[] reads r1..r5 are stored at record +0x220, +0x228, +0x224, +0x230,
// +0x22c: the driver seat group moves by (0, +0x224, +0x228) = (0, r3, r2)
// (FUN_00b8ade0) [CODE]. Node offset = the first 3 floats (record entry
// +0x20, the one FUN_00b7e940 reads) [CODE]. Sex index 1 = woman
// (FUN_00cabec0() == 1) [INF: DBPNJ _sex 1 = woman].
bool IntroCinematic::loadDriverIKOffsets( int32_t idCar, bool bWoman ) {
    driverIK = DriverIKOffsets();
    std::vector<uint8_t> file;
    const std::string folder = gpFile->getBankFolder();
    if ( !gpFile->loadFile( ( folder + "DataBase/IKOffsetDB.bin" ).c_str(), file ) &&
         !gpFile->loadFile( ( folder + "database/ikoffsetdb.bin" ).c_str(), file ) ) {
        return false;
    }
    auto u32 = [&]( size_t offset ) {
        return static_cast<uint32_t>( file[offset] ) << 24 | static_cast<uint32_t>( file[offset + 1] ) << 16 |
               static_cast<uint32_t>( file[offset + 2] ) << 8 | file[offset + 3];
    };
    auto f32 = [&]( size_t offset ) {
        const uint32_t bits = u32( offset );
        float value;
        memcpy( &value, &bits, 4 );
        return value;
    };
    constexpr size_t kSexSize = 5 * 4 + 27 * 24;
    constexpr size_t kCarSize = 4 + 2 * kSexSize + 11 * 12;
    if ( file.size() < 0x24 || memcmp( file.data() + 0x0c, "BDKI", 4 ) != 0 ||
         memcmp( file.data() + 0x10, "KIAC", 4 ) != 0 ) {
        return false;
    }
    const uint32_t numCars = u32( 0x20 );
    for ( uint32_t c = 0; c < numCars && 0x24 + ( c + 1 ) * kCarSize <= file.size(); c++ ) {
        const size_t car = 0x24 + c * kCarSize;
        if ( static_cast<int32_t>( u32( car ) ) != idCar ) {
            continue;
        }
        const size_t sex = car + 4 + ( bWoman ? kSexSize : 0 );
        driverIK.SeatY = f32( sex + 8 );
        driverIK.SeatZ = f32( sex + 4 );
        for ( uint32_t n = 0; n < 27; n++ ) {
            const size_t entry = sex + 20 + n * 24;
            driverIK.Node[n] = Vector3f( f32( entry ), f32( entry + 4 ), f32( entry + 8 ) );
        }
        driverIK.bValid = true;
        return true;
    }
    return false;
}

void IntroCinematic::updateSeatedPlayer() {
    player.bVisible = false;
    Matrix4x4f carWorld;
    Matrix4x4f seat;
    const bool bShowCar = seatedShowCar >= 0;
    CinematicCar &seatedCar = bShowCar ? *showCars[static_cast<size_t>( seatedShowCar )].Car : car;
    const bool bWorld =
        bShowCar ? getShowCarWorld( static_cast<size_t>( seatedShowCar ), carWorld ) : getCarWorld( carWorld );
    if ( !player.Avatar || !bDriverPose || !bWorld || !seatedCar.getNodeModelTransform( "DVRSEAT_LR", seat ) ) {
        return;
    }
    if ( !bDriverIKLoaded ) {
        bDriverIKLoaded = true;
        const int32_t idCar = seatedCar.getIdCar();
        if ( !loadDriverIKOffsets( idCar, player.Avatar->isWoman() ) ) {
            OTDU_LOG_WARN( "IntroCinematic: no IKOffsetDB record for car %d (offsets 0)\n", idCar );
        }
    }
    const EdenSkeleton &skeleton = player.Avatar->getSkeleton();
    std::vector<BoneTransform> locals( skeleton.getNumBones() );
    for ( uint32_t b = 0; b < skeleton.getNumBones(); b++ ) {
        locals[b] = skeleton.getBindLocal( b );
    }
    driverPose.sample( skeleton, 0.0f, false, locals );

    // Avatar root (FUN_00b8ade0 / FUN_00b82450 [CODE]): car world with the
    // translation car * (DvrSeat node position + R_DvrSeat * IKOffsetDB
    // offset of DvrSeat), per car / per sex from IKOffsetDB.bin (0 when the
    // car has no record, FUN_00b97cc0). The seat group SEAT_*_L is also
    // moved by the stored (y, z) seat adjustment (+0x224 / +0x228 of the same
    // record) [CODE].
    // Seat group adjustment (0, y, z) moves DvrSeat and Back_L (children of
    // Seat_HR_L) [CODE]; FUN_0042b4f0's add-vs-set is [ENC]: added, since
    // the zero default must leave the seat in place [INF].
    const Vector3f seatAdjust( 0.0f, driverIK.SeatY, driverIK.SeatZ );
    seat = Matrix4x4f::Translation( seatAdjust + driverIK.Node[0] ) * seat; // + R_DvrSeat * offset (identity R)
    const Matrix4x4f seatInverse = seat.inverse();

    // IK goals (FUN_00b7e940 [CODE]): the car nodes Hand_*_L / Hand_*_R,
    // Foot_*_R / Foot_*_L and Back_*_L (+ R_node * their IKOffsetDB offsets),
    // weights 1 (+0x1df0 / +0x1df4 = 1.0 at seat-in, FUN_00b7fc74); chains of
    // driveiklayer6.bas [DATA]. Car nodes are read on the exterior model
    // (the _LR names; the interior _HR ones are at the same places [DATA]).
    // Node index in the IKOffsetDB 27-entry table (0xf7dbf0 LR / 0xf7db68 HR).
    auto goal = [&]( const char *pNode, int32_t index, Vector3f &out ) {
        Matrix4x4f node;
        if ( !seatedCar.getNodeModelTransform( pNode, node ) ) {
            return false;
        }
        Vector3f position = node.getTranslation();
        if ( index >= 0 ) {
            position += node.transformVector( driverIK.Node[index] );
        }
        if ( index == 6 ) {
            position += seatAdjust; // Back_L is in the seat group
        }
        out = seatInverse.transformPoint( position );
        return true;
    };
    auto bone = [&]( const char *pName ) { return skeleton.findBone( EdenNameHash4( pName ) ); };
    Vector3f target;
    if ( goal( "BACK_LR_L", 6, target ) ) {
        SolveAimIK( skeleton, locals, bone( "Spine" ), bone( "IK_Back" ), target );
    }
    if ( goal( "HAND_LR_R", 1, target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "Arm_R" ), bone( "ForeArm_R" ), bone( "IK_Right_Hand" ), target,
                        bone( "Hand_R" ) );
    }
    if ( goal( "HAND_LR_L", 2, target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "Arm_L" ), bone( "ForeArm_L" ), bone( "IK_Left_Hand" ), target,
                        bone( "Hand_L" ) );
    }
    if ( goal( "FOOT_LR_R", 3, target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "UpLeg_R" ), bone( "Leg_R" ), bone( "IK_Right_Foot" ), target,
                        bone( "Foot_R" ) );
    }
    if ( goal( "FOOT_LR_L", 4, target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "UpLeg_L" ), bone( "Leg_L" ), bone( "IK_Left_Foot" ), target,
                        bone( "Foot_L" ) );
    }
    skeleton.computeSkinMatrices( locals, player.WorldBones, player.Skin );
    player.World = carWorld * seat;
    player.bVisible = true;
    static int32_t logSeat = CmdLine::HasArg( "-logseat" ) ? 1 : 0;
    if ( logSeat > 0 ) {
        logSeat--;
        OTDU_LOG_INFO( "SEAT IKOffsetDB car %d valid %d seat (%.3f %.3f)\n", seatedCar.getIdCar(), driverIK.bValid ? 1 : 0,
                       driverIK.SeatY, driverIK.SeatZ );
        struct {
            const char *pBone, *pNode;
            int32_t Index;
        } pairs[] = { { "IK_Left_Hand", "HAND_LR_L", 2 }, { "IK_Right_Hand", "HAND_LR_R", 1 },
                      { "IK_Left_Foot", "FOOT_LR_L", 4 }, { "IK_Right_Foot", "FOOT_LR_R", 3 },
                      { "IK_Back", "BACK_LR_L", 6 },      { "Head", "DVRSEAT_LR", -1 } };
        for ( const auto &pair : pairs ) {
            const int32_t b = bone( pair.pBone );
            Vector3f t;
            if ( b >= 0 && goal( pair.pNode, pair.Index, t ) ) {
                const Vector3f p = player.WorldBones[b].getTranslation();
                OTDU_LOG_INFO( "SEAT %-14s (%.3f %.3f %.3f) goal %s (%.3f %.3f %.3f) dist %.3f\n", pair.pBone, p.x,
                               p.y, p.z, pair.pNode, t.x, t.y, t.z, ( p - t ).length() );
            }
        }
    }
    computeAvatarLighting( player.World.getTranslation(), player.Lighting );
}

void IntroCinematic::closeGoal() {
    // FUN_00d61700: pop_goal.close_popup().
    goalTimer = -1.0f;
    if ( general.isLoaded() ) {
        general.invoke( "/pop_goal", "close_popup", {} );
    }
}

void IntroCinematic::setLocalCamera( const Vector3f &position, const Quaternionf &heading, float fov,
                                     float nearPlane ) {
    EdenSceneView::Camera local = camera;
    local.Position = sceneOrigin.transformPoint( position );
    local.Rotation = heading * Conjugate( sceneRotation );
    local.Fov = fov;
    // First person cameras pass their own near plane: the cinematic default
    // (0.8 m, FUN_00b3eba0) clips every wall / object within 80 cm of the eye
    // (the walk mode showed the world cut open when standing close to
    // something).
    if ( nearPlane > 0.0f ) {
        local.Near = nearPlane;
    }
    setCameraOverride( &local );
}

bool IntroCinematic::getEntityCameraLocal( const std::string &name, Vector3f &outPosition,
                                           Quaternionf &outHeading ) const {
    auto it = entityCameras.find( name );
    if ( it == entityCameras.end() ) {
        return false;
    }
    outPosition = it->second.first;
    outHeading = it->second.second;
    return true;
}

bool IntroCinematic::getPosCamViewLocal( const std::string &posCamName, Vector3f &outPosition,
                                         Quaternionf &outHeading ) const {
    const auto posCam = posCamCameras.find( posCamName );
    if ( posCam == posCamCameras.end() ) {
        return false;
    }
    return getEntityCameraLocal( posCam->second, outPosition, outHeading );
}

const IntroCinematic::Volume *IntroCinematic::findVolume( const std::string &name ) const {
    for ( const Volume &volume : volumes ) {
        if ( volume.Name == name ) {
            return &volume;
        }
    }
    for ( const Volume &volume : selectables ) {
        if ( volume.Name == name ) {
            return &volume;
        }
    }
    return nullptr;
}

Vector3f IntroCinematic::Volume::toUnit( const Vector3f &p ) const {
    // D3D row vector: p' = (p, 1) * M.
    return Vector3f( p.x * Matrix[0][0] + p.y * Matrix[1][0] + p.z * Matrix[2][0] + Matrix[3][0],
                     p.x * Matrix[0][1] + p.y * Matrix[1][1] + p.z * Matrix[2][1] + Matrix[3][1],
                     p.x * Matrix[0][2] + p.y * Matrix[1][2] + p.z * Matrix[2][2] + Matrix[3][2] );
}

Vector3f IntroCinematic::Volume::toUnitDirection( const Vector3f &d ) const {
    return Vector3f( d.x * Matrix[0][0] + d.y * Matrix[1][0] + d.z * Matrix[2][0],
                     d.x * Matrix[0][1] + d.y * Matrix[1][1] + d.z * Matrix[2][1],
                     d.x * Matrix[0][2] + d.y * Matrix[1][2] + d.z * Matrix[2][2] );
}

bool IntroCinematic::Volume::contains( const Vector3f &p ) const {
    // FUN_00b40e50 (types 1..3).
    const Vector3f u = toUnit( p );
    switch ( OfType ) {
    case 1:
        return u.dot( u ) <= 1.0f;
    case 2:
        return u.x >= -0.5f && u.x <= 0.5f && u.y >= -0.5f && u.y <= 0.5f && u.z >= -0.5f && u.z <= 0.5f;
    case 3:
        return u.y >= -0.5f && u.y <= 0.5f && u.x * u.x + u.z * u.z <= 1.0f;
    default:
        return false;
    }
}

bool IntroCinematic::Volume::raycast( const Vector3f &origin, const Vector3f &direction, float maxDistance,
                                      float &outT, float *pOutExit ) const {
    // Slab test in the unit box (the selectable hit test vf04 is encrypted
    // [ENC]; the volume shape is the data).
    const Vector3f o = toUnit( origin );
    const Vector3f d = toUnitDirection( direction );
    float tMin = 0.0f;
    float tMax = maxDistance;
    const float origin3[3] = { o.x, o.y, o.z };
    const float direction3[3] = { d.x, d.y, d.z };
    for ( int32_t axis = 0; axis < 3; axis++ ) {
        if ( std::fabs( direction3[axis] ) < 1e-8f ) {
            if ( origin3[axis] < -0.5f || origin3[axis] > 0.5f ) {
                return false;
            }
            continue;
        }
        float t0 = ( -0.5f - origin3[axis] ) / direction3[axis];
        float t1 = ( 0.5f - origin3[axis] ) / direction3[axis];
        if ( t0 > t1 ) {
            std::swap( t0, t1 );
        }
        tMin = Max( tMin, t0 );
        tMax = Min( tMax, t1 );
        if ( tMin > tMax ) {
            return false;
        }
    }
    outT = tMin;
    if ( pOutExit != nullptr ) {
        *pOutExit = tMax;
    }
    return true;
}

void IntroCinematic::loadVolumes( const XmbDocument &entities ) {
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    auto read = []( XmbValue entry, Volume &volume ) {
        volume.Name = entry["__Name"].asString();
        volume.OfType = static_cast<uint32_t>( entry["OfType"].asInt() );
        XmbValue matrix = entry["volume_mat"];
        const char *pRows[4] = { "_0", "_1", "_2", "_3" };
        const char *pColumns[4] = { "X", "Y", "Z", "W" };
        for ( int32_t r = 0; r < 4; r++ ) {
            for ( int32_t c = 0; c < 4; c++ ) {
                volume.Matrix[r][c] = matrix[pRows[r]][pColumns[c]].asFloat();
            }
        }
        XmbValue position = entry["orient_pos_scale"]["pos"];
        volume.Center = Vector3f( position["X"].asFloat(), position["Y"].asFloat(), position["Z"].asFloat() );
    };
    XmbValue volumeExport = entities.getRoot()["ExportType_Volume"];
    for ( uint32_t i = 0; i < volumeExport.getArraySize( "ArrayOf_Entity__Volume" ); i++ ) {
        Volume volume;
        read( volumeExport.getArrayElement( "ArrayOf_Entity__Volume", i ), volume );
        volumes.push_back( volume );
    }
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Volume__FpSelectable" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Volume__FpSelectable", i );
        Volume volume;
        read( entry, volume );
        volume.SelectableType = static_cast<int32_t>( entry["Type"].asInt() );
        volume.SelectableString = entry["SelectableString"].asString();
        selectables.push_back( volume );
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu volumes, %zu selectables\n", volumes.size(), selectables.size() );
}

void IntroCinematic::setCameraOverride( const EdenSceneView::Camera *pCamera ) {
    bCameraOverride = pCamera != nullptr;
    if ( pCamera != nullptr ) {
        cameraOverride = *pCamera;
    }
}

bool IntroCinematic::getEntityCamera( const std::string &name, EdenSceneView::Camera &outCamera ) const {
    auto it = entityCameras.find( name );
    if ( it == entityCameras.end() ) {
        return false;
    }
    // Entity rot: same convention as the .sce camera Heading (forward =
    // R(q*) (0, 0, 1)) [INF: cam_entree_0N face the party, as c_fiesta_02].
    outCamera = camera;
    outCamera.Position = sceneOrigin.transformPoint( it->second.first );
    outCamera.Rotation = it->second.second * Conjugate( sceneRotation );
    auto fov = entityCameraFovs.find( name );
    if ( fov != entityCameraFovs.end() ) {
        // CameraCasino Fov: degrees when above pi, else radians [INF: the
        // values are read and logged, -logcasino].
        outCamera.Fov = fov->second > 3.2f ? fov->second * 0.017453293f : fov->second;
    }
    return true;
}

bool IntroCinematic::getEntityCamera( uint64_t hashcode, EdenSceneView::Camera &outCamera ) const {
    // By hashcode: the 38 slot machines all name their game camera CAM_GAME.
    auto it = entityCamerasByHash.find( hashcode );
    if ( it == entityCamerasByHash.end() ) {
        return false;
    }
    outCamera = camera;
    outCamera.Position = sceneOrigin.transformPoint( it->second.Position );
    outCamera.Rotation = it->second.Rotation * Conjugate( sceneRotation );
    if ( it->second.Fov > 0.0f ) {
        outCamera.Fov = it->second.Fov > 3.2f ? it->second.Fov * 0.017453293f : it->second.Fov;
    }
    return true;
}

std::string IntroCinematic::getEntityCameraName( uint64_t hashcode ) const {
    auto it = entityCameraHashcodes.find( hashcode );
    return it != entityCameraHashcodes.end() ? it->second : std::string();
}

bool IntroCinematic::buildInCarFallback() {
    // The scene car's driver seat (DVRSEAT_LR, car model space) -> the eye at
    // head height on the passenger side, slightly ahead, looking back at the
    // driver's head [INF: framed on the retail car wash video, 15 s].
    Matrix4x4f carWorld;
    Matrix4x4f seat;
    if ( !car.isLoaded() || !getCarWorld( carWorld ) || !car.getNodeModelTransform( "DVRSEAT_LR", seat ) ) {
        return false;
    }
    const Vector3f seatLocal = seat.getTranslation();
    const Vector3f headLocal = seatLocal + Vector3f( 0.0f, 0.62f, -0.05f );
    const Vector3f eyeLocal( -seatLocal.x * 0.9f, seatLocal.y + 0.72f, seatLocal.z + 0.55f );
    const Vector3f eye = carWorld.transformPoint( eyeLocal );
    const Vector3f head = carWorld.transformPoint( headLocal );
    Vector3f direction = head - eye;
    const float length = std::sqrt( direction.x * direction.x + direction.y * direction.y + direction.z * direction.z );
    if ( length < 1e-3f ) {
        return false;
    }
    direction = direction * ( 1.0f / length );
    // The camera basis of GMShop::updateCockpitCamera (forward = R(conj q) +Z).
    const float yaw = std::atan2( direction.x, direction.z );
    const float pitch = std::asin( Clamp( direction.y, -1.0f, 1.0f ) );
    const Quaternionf conjugate = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw ) *
                                  Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), -pitch );
    camera.Position = eye;
    camera.Rotation = Quaternionf( -conjugate.x, -conjugate.y, -conjugate.z, conjugate.w );
    camera.Fov = 40.0f * 3.14159265f / 180.0f; // the shot's own Fov key [DATA]
    camera.Near = 0.05f;
    return true;
}

void IntroCinematic::updateView() {
    if ( bCameraOverride ) {
        camera = cameraOverride;
    } else if ( main.pDef != nullptr && !main.bNoCamera ) {
        Vector3f position;
        Quaternionf rotation;
        float fov = camera.Fov;
        bool bCamera = main.pDef->Cin.getCamera( main.pDef->Scenes, main.Time, position, rotation, fov );
        if ( bCamera && !( std::isfinite( rotation.x ) && std::isfinite( rotation.y ) && std::isfinite( rotation.z ) &&
                           std::isfinite( rotation.w ) && std::isfinite( position.x ) && std::isfinite( fov ) ) ) {
            // A camera the port cannot decode yet: C_WASHING c_washing_02 (the
            // in-car shot, 2.03..4.24 s) has a 4-component Position and Heading
            // (Heading component 0 is NaN) plus an extra 0x7055 track - it is
            // attached to something (car_wash_spec 9) [DATA]. A NaN basis
            // defeated every cull and flooded the shadow pass (411k draws, the
            // black screen). Until it is decoded: the in-car view of the retail
            // video (the driver's face from the front passenger side), built
            // from the scene car's driver seat, else the last good camera [INF].
            bCamera = false;
            buildInCarFallback(); // false: the camera keeps its last good pose
        }
        if ( bCamera ) {
            // Scene space -> island.
            camera.Position = sceneOrigin.transformPoint( position );
            // Eden convention: the frame is R(conj q); world frame R_y R(conj q).
            camera.Rotation = rotation * Conjugate( sceneRotation );
            camera.Fov = fov;
            // -logcincam: the main cinematic's camera, scene-local and world.
            static const bool bLogCinCam = CmdLine::HasArg( "-logcincam" );
            static int32_t cinCamLogs = 0;
            if ( bLogCinCam && ( cinCamLogs++ % 30 ) == 0 ) {
                const Vector3f originAt = sceneOrigin.getTranslation();
                OTDU_LOG_INFO( "IntroCinematic: '%s' t %.2f camera local (%.2f, %.2f, %.2f) world (%.2f, %.2f, %.2f), "
                               "scene origin (%.2f, %.2f, %.2f)\n",
                               main.pDef->Name.c_str(), main.Time, position.x, position.y, position.z,
                               camera.Position.x, camera.Position.y, camera.Position.z, originAt.x, originAt.y,
                               originAt.z );
            }
            // In-car shots: the meeting_tess camera sits at the dashboard looking
            // back at the driver. The cinematic near plane (0.8 m) cut away the
            // wheel, the dash and most of the windscreen header, and the sliver
            // of the header left over was the "black sail" (A/B -forcenear 0.05:
            // the sail is gone, the authored shot through the wheel appears).
            // Within the default near distance of the car body the walk
            // camera's near plane is used instead [INF: the retail handling of
            // these shots is not readable; a global 0.05 would cost depth
            // precision at the 40 km far plane].
            const EdenSceneView::Camera defaults;
            camera.Near = defaults.Near;
            Matrix4x4f carWorld;
            Vector3f bodyMin, bodyMax;
            if ( setup.bInterior ) {
                // The interior entry shots stand inside the room with no car
                // to key on (the photographer, the sellers): the walk
                // camera's near plane all along, like WalkMode::kWalkNearPlane
                // and the dealer PosCam (focusDisplayCar).
                camera.Near = 0.05f;
            } else if ( car.isLoaded() && getCarWorld( carWorld ) && car.getBodyBounds( bodyMin, bodyMax ) ) {
                const Vector3f local = carWorld.inverse().transformPoint( camera.Position );
                const float margin = defaults.Near;
                if ( local.x > bodyMin.x - margin && local.x < bodyMax.x + margin && local.y > bodyMin.y - margin &&
                     local.y < bodyMax.y + margin && local.z > bodyMin.z - margin && local.z < bodyMax.z + margin ) {
                    camera.Near = 0.05f;
                }
            }
        }
    }
    view.setCamera( camera );
    // Listener: the camera (FUN_00d944b0; Micro = 0 for these cinematics).
    if ( gpAudio != nullptr ) {
        const Vector3f forward = EdenRotationFromQuaternion( camera.Rotation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        cameraForward = forward;
        const float frameTime = Max( lastDeltaTime, 1e-4f );
        gpAudio->setListener( camera.Position, forward, ( camera.Position - previousListener ) * ( 1.0f / frameTime ) );
        previousListener = camera.Position;
    }
    // -cineweather <preset>: one cinematic_weathers preset for every shot and
    // the walk (A/B of time of day / weather); -listweathers logs the names.
    static const char *pForcedWeather = CmdLine::GetArgValue( "-cineweather" );
    static bool bListed = false;
    if ( !bListed && CmdLine::HasArg( "-listweathers" ) ) {
        bListed = true;
        for ( const WeatherPreset &preset : view.getWeathers().getPresets() ) {
            const Vector3f sun = preset.getVector3( "sundirection", Vector3f() );
            const Vector3f diffuse = preset.getVector3( "sun_diffuse", Vector3f() );
            // The NIGHTLMAP day / night weights of the spot exteriors and the
            // house interior (updateLightmapMaterials).
            const Vector4f extLmap =
                preset.getVector4( "spot_exterior_lightmap_day_night", Vector4f( -1.0f, -1.0f, -1.0f, -1.0f ) );
            const Vector4f intLmap =
                preset.getVector4( "spot_interior_lightmap_day_night", Vector4f( -1.0f, -1.0f, -1.0f, -1.0f ) );
            const Vector4f sio =
                preset.getVector4( "avatar_spotlight_interior_exterior_influence", Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) );
            OTDU_LOG_INFO( "IntroCinematic: weather preset '%s' sun (%.2f %.2f %.2f) diffuse (%.2f %.2f %.2f) shadow %.2f "
                           "lmap ext (%.2f %.2f) int (%.2f %.2f) sio (%.2f %.2f) cam %.2f shadinf %.2f\n",
                           preset.Name.c_str(), sun.x, sun.y, sun.z, diffuse.x, diffuse.y, diffuse.z,
                           preset.getFloat( "sun_shadow_opacity", 1.0f ), extLmap.x, extLmap.y, intLmap.x, intLmap.y, sio.x,
                           sio.y, preset.getFloat( "avatar_cameralight_factor", 0.0f ),
                           preset.getFloat( "avatar_shadow_influence", 1.0f ) );
        }
    }
    if ( pForcedWeather != nullptr ) {
        // Not a cinematic preset: the island's in-game ones (time of day
        // names "14", "01.30", "23.20"... as FREERIDE).
        static WeatherLibrary gameWeathers;
        static bool bGameWeathersLoaded = false;
        const WeatherPreset *pPreset = view.getWeathers().find( pForcedWeather );
        if ( pPreset == nullptr ) {
            if ( !bGameWeathersLoaded ) {
                bGameWeathersLoaded = true;
                gameWeathers.load( ( gpFile->getBankFolder() + "islands/ibiza/fx/game_weathers.xmb" ).c_str() );
                for ( const WeatherPreset &preset : gameWeathers.getPresets() ) {
                    const Vector3f sun = preset.getVector3( "sundirection", Vector3f() );
                    const Vector4f sio =
                        preset.getVector4( "avatar_spotlight_interior_exterior_influence", Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) );
                    OTDU_LOG_INFO( "IntroCinematic: game weather '%s' sun (%.2f %.2f %.2f) shadow %.2f sio (%.2f %.2f) cam %.2f "
                                   "shadinf %.2f\n",
                                   preset.Name.c_str(), sun.x, sun.y, sun.z, preset.getFloat( "sun_shadow_opacity", 1.0f ), sio.x,
                                   sio.y, preset.getFloat( "avatar_cameralight_factor", 0.0f ),
                                   preset.getFloat( "avatar_shadow_influence", 1.0f ) );
                }
            }
            pPreset = gameWeathers.find( pForcedWeather );
        }
        view.setWeather( pPreset );
    } else {
        view.setWeather( weatherName.empty()
                             ? nullptr
                             : view.getWeathers().findModified( weatherName.c_str(), modifierName.c_str() ) );
    }
    Matrix4x4f carWorld;
    if ( car.isLoaded() && !bCarStaticCube && computeCarWorld( carWorld ) ) {
        view.setEnvironmentCenter( carWorld.getTranslation(), true );
    } else {
        view.setEnvironmentCenter( camera.Position, false );
    }
    // Draw ranges reach ~960 m with LOD factor 4 (spec_pmi_streaming 9.7).
    const float lodFactor = gpConfig->getVideoSettings().GlobalLodFactor;
    if ( setup.bWorld ) {
        static const int32_t forcedRadius = CmdLine::GetArgInt( "-cineradius", 0 );
        const int32_t radius = forcedRadius > 0 ? forcedRadius : ( lodFactor > 1.0f ? 2 : 1 );
        // The cinematic camera flies across the island (the "generique" sea
        // shots) and comes back to the house: without unloading, every sector
        // the camera ever flew over stays resident for the whole intro
        // (9 -> 24 -> 26 -> 32 sectors during C_INTRO) and every pass draws
        // all of them, so the frame time grows with the flight (87 -> 147 ms
        // measured). Drop the ring one sector beyond the load radius
        // (hysteresis, like the world viewer) before loading the new one.
        // The caster / Z pass caches hold material pointers into the sector
        // objects: they must be dropped (eden_world.h, unloadFar).
        // The camera path is authored: load where the camera will be, not only
        // where it is (the sea sweep flew into sectors that loaded on arrival:
        // pop-in). -cinelookahead <s> (default 2, 0 = off).
        Vector3f ahead = camera.Position;
        static const float lookahead =
            CmdLine::HasArg( "-cinelookahead" ) ? static_cast<float>( CmdLine::GetArgInt( "-cinelookahead", 2 ) ) : 2.0f;
        if ( lookahead > 0.0f && !bCameraOverride && main.pDef != nullptr && !main.bNoCamera ) {
            Vector3f position;
            Quaternionf rotation;
            float fov = camera.Fov;
            const float aheadTime = Min( main.Time + lookahead, main.pDef->Cin.getDuration() );
            if ( main.pDef->Cin.getCamera( main.pDef->Scenes, aheadTime, position, rotation, fov ) ) {
                ahead = sceneOrigin.transformPoint( position );
            }
        }
        if ( view.getWorld().unloadFar( camera.Position, ahead, radius + 1 ) != 0 ) {
            if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
                pRenderer->clearShadowCasterCache();
            }
        }
        // A sector still missing around the camera itself loads on arrival,
        // in view: logged as the pop-in measure.
        if ( view.getWorld().loadAround( camera.Position, radius ) && main.pDef != nullptr ) {
            OTDU_LOG_INFO( "IntroCinematic: pop-in load at the camera ('%s' t %.2f)\n", main.pDef->Name.c_str(),
                           main.Time );
        }
        view.getWorld().loadAround( ahead, radius );
    }
}

const CinematicScene::Object *IntroCinematic::findActor( const Playing &playing, uint64_t entityHashcode,
                                                         const char *pName, const CinematicScene *&outScene,
                                                         float &outSceneTime ) const {
    if ( playing.pDef == nullptr ) {
        return nullptr;
    }
    const Cinematic::Shot *pShot = playing.pDef->Cin.getShot( playing.Time );
    if ( pShot == nullptr || pShot->Scene >= playing.pDef->Scenes.size() ) {
        return nullptr;
    }
    const CinematicScene &scene = playing.pDef->Scenes[pShot->Scene];
    for ( const CinematicScene::Object &object : scene.getObjects() ) {
        if ( object.isCamera() ) {
            continue;
        }
        const bool bMatch = pName != nullptr ? object.Name == pName : object.EntityHashcode == entityHashcode;
        if ( bMatch ) {
            outScene = &scene;
            outSceneTime = pShot->getSceneTime( playing.Time );
            return &object;
        }
    }
    return nullptr;
}

std::vector<const IntroCinematic::Playing *> IntroCinematic::playingOrder() const {
    // Main cinematic first, then the most recent background ones.
    std::vector<const Playing *> order;
    order.push_back( &main );
    for ( auto it = backgrounds.rbegin(); it != backgrounds.rend(); ++it ) {
        order.push_back( it->get() );
    }
    return order;
}

void IntroCinematic::drawSets( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
    if ( pass == EdenSceneView::ePass::Environment ) {
        return;
    }
    // Projector actor layers (REFLECT 2D refraction / REFLECT FOR ACTORS): the
    // actors (avatars, car) and the props (the pool accessories: the inflatable
    // ring, the loungers...) — the sets stay out of the mirror render (cost).
    // Without the props the water reflects the swimmers but not what they hold
    // (the ring shows no reflection) [INF].
    const bool bActorsOnly = pass == EdenSceneView::ePass::ProjectorActors;
    static const bool bNoSets = CmdLine::HasArg( "-cinnosets" ); // A/B: the set models of the bank
    // playingOrder() was rebuilt once per prop per pass (~192 vector copies a
    // frame); hoisted out of the loop.
    const std::vector<const Playing *> order = playingOrder();
    for ( size_t i = 0; i < sets.size(); i++ ) {
        if ( bActorsOnly || bNoSets ) {
            break;
        }
        // The spot frame: the exterior bank sets (setIsExterior) and the
        // Setup::InteriorBank room (CINE kind 3) stand at the spot even when
        // the cinematic frame moved (liftOntoFloor / placeEntityAt).
        const bool bSpotFrame = ( i < setIsExterior.size() && setIsExterior[i] ) || i >= interiorSetStart;
        sets[i]->draw( renderer, bSpotFrame ? spotOrigin : sceneOrigin );
    }
    // Props: .sce actor of the main / background cinematic (same entity) or
    // the entity placement.
    static const bool bNoProps = CmdLine::HasArg( "-noprops" ); // A/B: no props at all
    for ( const Prop &prop : props ) {
        if ( bNoProps || !prop.bVisible ) {
            continue;
        }
        if ( prop.bAnimated ) {
            prop.Model->drawSkinned( renderer, prop.World, prop.Skin, &prop.Lighting );
            continue;
        }
        if ( prop.bHandHeld ) {
            prop.Model->draw( renderer, prop.HeldWorld * prop.RootInverse );
            continue;
        }
        Matrix4x4f local = prop.Local;
        for ( const Playing *pPlaying : order ) {
            const CinematicScene *pScene = nullptr;
            float sceneTime = 0.0f;
            const CinematicScene::Object *pObject = findActor( *pPlaying, prop.EntityHashcode, nullptr, pScene, sceneTime );
            if ( pObject != nullptr && pObject->findTrack( CinematicScene::kTrackPosition ) ) {
                // The scene origin places the scene-space track (meeting_tess: rotated ~140 degrees).
                local = pScene->getOrigin() * Matrix4x4f::Translation( pObject->samplePosition( sceneTime ) ) *
                        EdenRotationFromQuaternion( pObject->sampleRotation( sceneTime ) );
                break;
            }
        }
        // -logpropat <name>: that prop's placement every 60 draws (a prop
        // driven by a cutscene track, e.g. the casino key ring CF_KCar).
        static const char *pLogPropAt = CmdLine::GetArgValue( "-logpropat" );
        if ( pLogPropAt != nullptr && prop.Name == pLogPropAt ) {
            static int32_t propLogs = 0;
            if ( ( propLogs++ % 60 ) == 0 ) {
                const Vector3f at = local.getTranslation();
                OTDU_LOG_INFO( "IntroCinematic: prop '%s' %016llx local (%.2f, %.2f, %.2f), main '%s'\n",
                               prop.Name.c_str(), static_cast<unsigned long long>( prop.EntityHashcode ), at.x, at.y,
                               at.z, main.pDef != nullptr ? main.pDef->Name.c_str() : "" );
            }
        }
        prop.Model->draw( renderer, ( prop.bExterior ? spotOrigin : sceneOrigin ) * local * prop.RootInverse );
    }
    // The avatars and cars are culled against the frustum of the pass being
    // recorded (the main camera, each shadow slice, the ocean mirror, the
    // projector). Before this each of the 31 PNJs was submitted to every
    // pass; -noobjcull (or -nopnj) is the A/B.
    for ( const Pnj &pnj : pnjs ) {
        if ( pnj.bVisible &&
             renderer.isSphereVisible( pnj.World.getTranslation() + Vector3f( 0.0f, 1.0f, 0.0f ), 1.4f ) ) {
            pnj.Avatar->draw( renderer, pnj.World, pnj.Skin, &pnj.Lighting );
        }
    }
    const Vector3f carSphereOffset( 0.0f, 0.8f, 0.0f );
    if ( player.Avatar && player.bVisible &&
         renderer.isSphereVisible( player.World.getTranslation() + Vector3f( 0.0f, 1.0f, 0.0f ), 1.4f ) ) {
        // Seated under the game camera (cockpit view, camera mode 23): the
        // head and what is worn on it are not drawn (the eye is inside the
        // head; the original seated view shows no head / hair) [INF: the
        // cockpit render branch FUN_00b75130 -> FUN_00b940a0(DAT_00ffbfd4)
        // was not decoded further].
        const bool bDrawHead = !( bPlayerSeated && bCameraOverride );
        player.Avatar->draw( renderer, player.World, player.Skin, &player.Lighting, bDrawHead );
    }
    Matrix4x4f carWorld;
    if ( car.isLoaded() && computeCarWorld( carWorld ) &&
         renderer.isSphereVisible( carWorld.getTranslation() + carSphereOffset, 3.5f ) ) {
        car.draw( renderer, carWorld, view.getWeather(), bCarEnvironment );
    }
    for ( size_t i = 0; i < showCars.size(); i++ ) {
        Matrix4x4f showWorld;
        if ( showCars[i].bVisible && getShowCarWorld( i, showWorld ) &&
             renderer.isSphereVisible( showWorld.getTranslation() + carSphereOffset, 3.5f ) ) {
            showCars[i].Car->draw( renderer, showWorld, view.getWeather(), showCars[i].bEnvironment );
        }
    }
    for ( const DisplayCar &display : displayCars ) {
        if ( renderer.isSphereVisible( display.World.getTranslation() + carSphereOffset, 3.5f ) ) {
            display.Car->draw( renderer, display.World, view.getWeather(), display.bEnvironment );
        }
    }
}

bool IntroCinematic::addDisplayCar( const CinematicCar::Desc &desc, const std::string &posCam, int32_t paintIndex ) {
    auto it = entityPlacements.find( posCam );
    if ( it == entityPlacements.end() ) {
        return false;
    }
    DisplayCar display;
    display.Car = std::make_unique<CinematicCar>();
    if ( !display.Car->load( desc ) ) {
        return false;
    }
    if ( paintIndex >= 0 ) {
        display.Car->applyDatabasePaint( paintIndex );
    }
    // At rest on the PosCam, the tires on the scene floor (the D_CAR rest of
    // computeCarWorld).
    const Matrix4x4f local =
        display.Car->fitOnGround( it->second.first, it->second.second, [this]( float x, float z, float y, float &outHeight ) {
            return FindHavokFloor( collision, Vector3f( x, y, z ), 0.0f, 3.0f, outHeight );
        } );
    display.World = sceneOrigin * local;
    display.RestLocal = local;
    display.Pivot = it->second.first;
    display.CarId = display.Car->getIdCar();
    display.PosCam = posCam;
    // The scene's static car cube (icd_*: cubecar.2db, like garage.bnk's).
    if ( GPUTexture *pCube = gpRender->getTextureCache().find( introBank.getPath().c_str(), "CUBECAR" ) ) {
        display.Car->assignEnvironment( *gpRender->getEdenRenderer(), pCube, false );
        display.bEnvironment = true;
    }
    displayCars.push_back( std::move( display ) );
    return true;
}

void IntroCinematic::spinLastDisplayCar( float radiansPerSecond ) {
    if ( !displayCars.empty() ) {
        displayCars.back().SpinRate = radiansPerSecond;
    }
}

float IntroCinematic::getPropClipDuration( uint64_t clipRsc ) {
    const EdenAnimation *pClip = getPropClip( clipRsc );
    return pClip != nullptr ? pClip->getDuration() : 0.0f;
}

const EdenAnimation *IntroCinematic::getPropClip( uint64_t clipRsc ) {
    // The entity's rsc (rscSubType 9) names an .anm of the scene bank; the
    // hashcode index files animations under sub type 8 (getAnimation) [DATA:
    // the cinematics' clips], so both are tried.
    auto it = propClips.find( clipRsc );
    if ( it != propClips.end() ) {
        return it->second.get();
    }
    std::unique_ptr<EdenAnimation> pClip;
    const BankEntry *pEntry = introHashcodes.find( clipRsc, 0, 9 );
    if ( pEntry == nullptr ) {
        pEntry = introHashcodes.find( clipRsc, 0, 8 );
    }
    if ( pEntry == nullptr ) {
        pEntry = introHashcodes.find( clipRsc );
    }
    if ( pEntry != nullptr && pEntry->Size >= 16 ) {
        OTDU_LOG_INFO( "IntroCinematic: prop clip entry '%s' type '%s' %u bytes, head %02x%02x%02x%02x %02x%02x%02x%02x\n",
                       pEntry->Name.c_str(), pEntry->Extension.c_str(), static_cast<uint32_t>( pEntry->Size ),
                       pEntry->pData[0], pEntry->pData[1], pEntry->pData[2], pEntry->pData[3], pEntry->pData[4],
                       pEntry->pData[5], pEntry->pData[6], pEntry->pData[7] );
    }
    // A "BAS!" layer (cas_sm_spin-tri.bas, 192 bytes) names its clip as a
    // .xan; the bank holds it as the .anm of that name (the seated avatar's
    // hitchhikerlayer0.bas -> hit_stand_car.xan pattern) [DATA].
    if ( pEntry != nullptr && pEntry->Size >= 4 && memcmp( pEntry->pData, "BAS!", 4 ) == 0 ) {
        const std::string text( reinterpret_cast<const char *>( pEntry->pData ), pEntry->Size );
        const size_t xan = text.find( ".xan" );
        const BankEntry *pAnm = nullptr;
        if ( xan != std::string::npos ) {
            size_t start = xan;
            while ( start > 0 && text[start - 1] >= 0x21 && text[start - 1] < 0x7f && text[start - 1] != '\\' &&
                    text[start - 1] != '/' ) {
                start--;
            }
            const std::string clipName = text.substr( start, xan - start ) + ".anm";
            pAnm = getSceneBank().findEntry( clipName.c_str() );
            OTDU_LOG_INFO( "IntroCinematic: prop clip layer '%s' -> '%s' %s\n", pEntry->Name.c_str(), clipName.c_str(),
                           pAnm != nullptr ? "found" : "missing" );
        }
        pEntry = pAnm;
    }
    if ( pEntry != nullptr ) {
        pClip = std::make_unique<EdenAnimation>();
        if ( !pClip->parse( pEntry->pData, pEntry->Size ) ) {
            pClip.reset();
        }
    }
    OTDU_LOG_INFO( "IntroCinematic: prop clip %016llx -> %s (%.2f s)\n", static_cast<unsigned long long>( clipRsc ),
                   pEntry != nullptr ? pEntry->Name.c_str() : "(missing)", pClip ? pClip->getDuration() : 0.0f );
    const EdenAnimation *pResult = pClip.get();
    propClips[clipRsc] = std::move( pClip );
    return pResult;
}

bool IntroCinematic::setPropClip( uint64_t entityHashcode, uint64_t clipRsc, float time ) {
    const EdenAnimation *pClip = time >= 0.0f ? getPropClip( clipRsc ) : nullptr;
    bool bFound = false;
    for ( Prop &prop : props ) {
        if ( prop.EntityHashcode == entityHashcode && prop.Skeleton ) {
            prop.pDrivenClip = pClip;
            prop.DrivenTime = time;
            bFound = true;
        }
    }
    return bFound && ( time < 0.0f || pClip != nullptr );
}

bool IntroCinematic::setPropTexture( uint64_t entityHashcode, const char *pMaterial, uint32_t slot, GPUTexture *pTexture,
                                     uint64_t hash ) {
    bool bFound = false;
    for ( Prop &prop : props ) {
        if ( prop.EntityHashcode != entityHashcode || slot >= 16 ) {
            continue;
        }
        for ( EdenMaterial &material : prop.Model->getMaterials() ) {
            if ( pMaterial != nullptr && material.Name != pMaterial ) {
                continue;
            }
            static int32_t logged = 0;
            if ( CmdLine::HasArg( "-logslothud" ) && logged < 6 ) {
                logged++;
                std::string slots;
                for ( uint32_t k = 0; k < material.NumSlots && k < 16; k++ ) {
                    char text[96];
                    snprintf( text, sizeof( text ), " [%u] tex %p hash %016llx type %016llx", k,
                              static_cast<void *>( material.pTextures[k] ),
                              static_cast<unsigned long long>( material.TextureHashes[k] ),
                              static_cast<unsigned long long>( material.SlotTypes[k] ) );
                    slots += text;
                }
                OTDU_LOG_INFO( "IntroCinematic: prop '%s' material '%s' (%u slots):%s\n", prop.Name.c_str(),
                               material.Name.c_str(), material.NumSlots, slots.c_str() );
            }
            material.pTextures[slot] = pTexture;
            material.TextureHashes[slot] = hash;
            bFound = true;
        }
    }
    return bFound;
}

float IntroCinematic::NeonCurve::evaluate( float x ) const {
    // FUN_00b43940 [CODE]: before the first key / past the last its value;
    // else the cubic Hermite of the segment (FUN_00b43830: h00 P0 + h10
    // TgtOut0 + h01 P1 + h11 TgtIn1, on x and y), solved for x by 32 halvings
    // of the parameter, and its y.
    if ( Keys.empty() ) {
        return 1.0f;
    }
    if ( x <= Keys.front().x ) {
        return Keys.front().y;
    }
    if ( x >= Keys.back().x ) {
        return Keys.back().y;
    }
    size_t i = 0;
    while ( i + 1 < Keys.size() && Keys[i + 1].x < x ) {
        i++;
    }
    const Vector2f p0 = Keys[i], p1 = Keys[i + 1];
    const Vector2f t0 = i < TangentOut.size() ? TangentOut[i] : Vector2f( 0.0f, 0.0f );
    const Vector2f t1 = i + 1 < TangentIn.size() ? TangentIn[i + 1] : Vector2f( 0.0f, 0.0f );
    auto hermite = [&]( float s ) {
        const float s2 = s * s, s3 = s2 * s;
        const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f, h10 = s3 - 2.0f * s2 + s;
        const float h01 = -2.0f * s3 + 3.0f * s2, h11 = s3 - s2;
        return Vector2f( h00 * p0.x + h10 * t0.x + h01 * p1.x + h11 * t1.x,
                         h00 * p0.y + h10 * t0.y + h01 * p1.y + h11 * t1.y );
    };
    float lo = 0.0f, hi = 1.0f;
    for ( int32_t k = 0; k < 32; k++ ) {
        const float mid = ( lo + hi ) * 0.5f;
        if ( hermite( mid ).x < x ) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const float y = hermite( ( lo + hi ) * 0.5f ).y;
    // MinScale / MaxScale (0..1 on every curve of the slots room): taken as
    // the y range [INF].
    return MinScale.y + y * ( MaxScale.y - MinScale.y );
}

void IntroCinematic::loadNeonCurves( const XmbDocument &entities ) {
    XmbValue block = entities.getRoot()["ExportType_Curve"];
    const uint32_t count = block.getArraySize( "ArrayOf_Curve" );
    for ( uint32_t i = 0; i < count; i++ ) {
        XmbValue curve = block.getArrayElement( "ArrayOf_Curve", i );
        NeonCurve out;
        auto pairs = [&]( const char *pName, std::vector<Vector2f> &target ) {
            const uint32_t n = curve.getArraySize( pName );
            for ( uint32_t k = 0; k + 1 < n; k += 2 ) {
                target.push_back( Vector2f( curve.getArrayElement( pName, k ).asFloat(),
                                            curve.getArrayElement( pName, k + 1 ).asFloat() ) );
            }
        };
        pairs( "PosArray_", out.Keys );
        pairs( "TgtInArray_", out.TangentIn );
        pairs( "TgtOutArray_", out.TangentOut );
        out.MinScale = Vector2f( curve["MinScale"]["X"].asFloat(), curve["MinScale"]["Y"].asFloat() );
        out.MaxScale = Vector2f( curve["MaxScale"]["X"].asFloat( 1.0f ), curve["MaxScale"]["Y"].asFloat( 1.0f ) );
        neonCurves[curve["__Hc"].asUInt64()] = std::move( out );
    }
    OTDU_LOG_INFO( "IntroCinematic: %u neon curves\n", count );
}

void IntroCinematic::readNeonState( const XmbValue &params, NeonStateParams &out ) {
    static const char *const kChannels[] = { "RedParams", "GreenParams", "BlueParams" };
    for ( int32_t c = 0; c < 3; c++ ) {
        XmbValue channel = params[kChannels[c]];
        out.Channels[c].TimeFactor = channel["TimeFactor"].asFloat( 1.0f );
        out.Channels[c].Illumination = channel["IlluminationFactor"].asFloat( 1.0f );
        auto it = neonCurves.find( channel["CurveFactor"].asUInt64() );
        out.Channels[c].pCurve = it != neonCurves.end() ? &it->second : nullptr;
    }
}

bool IntroCinematic::flashNeon( uint64_t entityHashcode, float seconds ) {
    bool bFound = false;
    for ( const Prop &prop : props ) {
        if ( prop.EntityHashcode == entityHashcode && prop.NeonIndex >= 0 ) {
            neonAnims[static_cast<size_t>( prop.NeonIndex )].FlashTime = seconds;
            bFound = true;
        }
    }
    if ( CmdLine::HasArg( "-logcasinoneons" ) ) {
        OTDU_LOG_INFO( "IntroCinematic: neon %016llx flashes %.1f s (%s)\n", static_cast<unsigned long long>( entityHashcode ),
                       seconds, bFound ? "found" : "missing" );
    }
    return bFound;
}

bool IntroCinematic::setNeonState( uint64_t entityHashcode, eNeonState state ) {
    bool bFound = false;
    for ( const Prop &prop : props ) {
        if ( prop.EntityHashcode != entityHashcode || prop.NeonIndex < 0 ) {
            continue;
        }
        NeonAnim &anim = neonAnims[static_cast<size_t>( prop.NeonIndex )];
        const NeonStateParams &params = anim.States[static_cast<int32_t>( state )];
        if ( params.bDefined && params.bCanActivate && anim.State != static_cast<int32_t>( state ) ) {
            anim.State = static_cast<int32_t>( state );
            anim.StateTime = 0.0f;
            if ( CmdLine::HasArg( "-logcasinoneons" ) ) {
                OTDU_LOG_INFO( "IntroCinematic: neon '%s' -> state %d (illum %.2f %.2f %.2f, time %.2f %.2f %.2f, curves %d%d%d, "
                               "duration %.2f)\n",
                               prop.Name.c_str(), anim.State, params.Channels[0].Illumination,
                               params.Channels[1].Illumination, params.Channels[2].Illumination,
                               params.Channels[0].TimeFactor, params.Channels[1].TimeFactor, params.Channels[2].TimeFactor,
                               params.Channels[0].pCurve != nullptr, params.Channels[1].pCurve != nullptr,
                               params.Channels[2].pCurve != nullptr, params.Duration );
            }
        } else if ( CmdLine::HasArg( "-logcasinoneons" ) && anim.State != static_cast<int32_t>( state ) ) {
            OTDU_LOG_INFO( "IntroCinematic: neon '%s' keeps state %d (state %d: CanBeActivate 0)\n", prop.Name.c_str(),
                           anim.State, static_cast<int32_t>( state ) );
        }
        bFound = true;
    }
    return bFound;
}

void IntroCinematic::updateNeons( float deltaTime ) {
    // FUN_00b44500 / FUN_00b44350 [CODE]: channel = IlluminationFactor x
    // ( curve ? curve( fmod( t, 3 / TimeFactor ) / ( 3 / TimeFactor ) ) : 1 ),
    // t = the clock + the random start; FUN_00b44400 writes AnimValue =
    // (R, G, B, 0). WeatherEmissive 1 (the neon manager's
    // building_neon_strength is not applied to props) [INF].
    neonClock += deltaTime;
    for ( NeonAnim &anim : neonAnims ) {
        anim.StateTime += deltaTime;
        const NeonStateParams *pState = &anim.States[anim.State];
        // A timed state (Duration > 0) falls back to NeonActive [INF].
        if ( pState->Duration > 0.0f && anim.StateTime > pState->Duration &&
             anim.States[static_cast<int32_t>( eNeonState::Active )].bDefined ) {
            anim.State = static_cast<int32_t>( eNeonState::Active );
            anim.StateTime = 0.0f;
            pState = &anim.States[anim.State];
        }
        const float t = neonClock + anim.TimeOffset;
        float value[3];
        for ( int32_t c = 0; c < 3; c++ ) {
            const NeonChannel &channel = pState->Channels[c];
            float curve = 1.0f;
            if ( channel.pCurve != nullptr && channel.TimeFactor > 0.0f ) {
                const float period = 3.0f / channel.TimeFactor;
                curve = channel.pCurve->evaluate( std::fmod( t, period ) / period );
            }
            value[c] = channel.Illumination * curve;
        }
        if ( anim.FlashTime > 0.0f ) {
            // 4 blinks a second, twice as bright / nearly off [INF].
            anim.FlashTime -= deltaTime;
            const float gain = std::fmod( neonClock * 4.0f, 1.0f ) < 0.5f ? 2.0f : 0.15f;
            for ( float &v : value ) {
                v *= gain;
            }
        }
        Prop &prop = props[anim.PropIndex];
        for ( EdenMaterial &material : prop.Model->getMaterials() ) {
            if ( EdenNeons::IsNeonSetMaterial( material ) ) {
                material.Parameters[EdenNeons::kWeatherEmissive] = Vector4f( 1.0f, 1.0f, 1.0f, 0.0f );
                material.Parameters[EdenNeons::kAnimValue] = Vector4f( value[0], value[1], value[2], 0.0f );
            }
        }
    }
}

bool IntroCinematic::setPropEmissive( uint64_t entityHashcode, float strength ) {
    bool bFound = false;
    for ( Prop &prop : props ) {
        if ( prop.EntityHashcode != entityHashcode ) {
            continue;
        }
        for ( EdenMaterial &material : prop.Model->getMaterials() ) {
            material.Parameters[EdenNeons::kWeatherEmissive] = Vector4f( strength, strength, strength, 0.0f );
            material.Parameters[EdenNeons::kAnimValue] = Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
            bFound = true;
        }
    }
    return bFound;
}

EdenModel *IntroCinematic::findPropModel( uint64_t entityHashcode ) {
    for ( Prop &prop : props ) {
        if ( prop.EntityHashcode == entityHashcode ) {
            return prop.Model.get();
        }
    }
    return nullptr;
}

int32_t IntroCinematic::addShowCar( const CinematicCar::Desc &desc, const std::string &restEntity ) {
    if ( entityPlacements.find( restEntity ) == entityPlacements.end() ) {
        OTDU_LOG_WARN( "IntroCinematic: show car rest entity '%s' not found\n", restEntity.c_str() );
        return -1;
    }
    ShowCar showCar;
    showCar.Car = std::make_unique<CinematicCar>();
    if ( !showCar.Car->load( desc ) ) {
        return -1;
    }
    showCar.RestEntity = restEntity;
    showCars.push_back( std::move( showCar ) );
    return static_cast<int32_t>( showCars.size() ) - 1;
}

void IntroCinematic::setShowCarVisible( size_t index, bool bVisible ) {
    if ( index < showCars.size() ) {
        showCars[index].bVisible = bVisible;
    }
}

bool IntroCinematic::getShowCarWorld( size_t index, Matrix4x4f &outWorld ) const {
    if ( index >= showCars.size() ) {
        return false;
    }
    auto it = entityPlacements.find( showCars[index].RestEntity );
    if ( it == entityPlacements.end() ) {
        return false;
    }
    outWorld = sceneOrigin * fitOnSceneFloor( *showCars[index].Car, it->second.first, it->second.second );
    return true;
}

int32_t IntroCinematic::addSelectable( const Volume &volume ) {
    selectables.push_back( volume );
    return static_cast<int32_t>( selectables.size() ) - 1;
}

bool IntroCinematic::getDisplayCarInfo( size_t index, Vector3f &outPosition, int32_t &outCarId ) const {
    if ( index >= displayCars.size() ) {
        return false;
    }
    // The walk controller's frame (scene-local): the display car's island
    // world position through sceneOrigin's inverse (focusDisplayCar's toLocal).
    outPosition = sceneOrigin.inverse().transformPoint( displayCars[index].World.getTranslation() );
    outCarId = displayCars[index].CarId;
    return true;
}

bool IntroCinematic::focusDisplayCar( int32_t carId, int32_t paintIndex ) {
    for ( DisplayCar &display : displayCars ) {
        if ( display.CarId != carId ) {
            continue;
        }
        if ( paintIndex >= 0 ) {
            display.Car->applyDatabasePaint( paintIndex );
        }
        // The PosCam's own camera when the bank has it; the dealer interiors'
        // PosCam Camera fields name cameras that are not in the bank (icd_ferr
        // has 2 Entity__Camera: cam_entree and one more) [DATA], so the car is
        // framed from a three-quarter front view instead [INF]: 6.5 m from its
        // centre, 35 deg off its nose, at eye height, looking at its middle.
        auto it = posCamCameras.find( display.PosCam );
        EdenSceneView::Camera shot;
        if ( it != posCamCameras.end() && getEntityCamera( it->second, shot ) ) {
            shot.Near = 0.05f; // the walk camera's near plane: the shot stands inside the room
            setCameraOverride( &shot );
            return true;
        }
        const Matrix4x4f toLocal = sceneOrigin.inverse();
        const Vector3f center = toLocal.transformPoint( display.World.getTranslation() ) + Vector3f( 0.0f, 0.6f, 0.0f );
        Vector3f nose = toLocal.transformVector( display.World.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
        nose.y = 0.0f;
        const float noseYaw = std::atan2( nose.x, nose.z );
        const float viewYaw = noseYaw + 35.0f * kDegToRad;
        const Vector3f eye = center + Vector3f( std::sin( viewYaw ) * 6.5f, 0.9f, std::cos( viewYaw ) * 6.5f );
        const Vector3f look = center - eye;
        // The walk controller's heading: q* = Ry(yaw) Rx(-pitch), forward = (sin yaw cos p, sin p, cos yaw cos p).
        const float yaw = std::atan2( look.x, look.z );
        const float pitch = std::atan2( look.y, std::sqrt( look.x * look.x + look.z * look.z ) );
        const Quaternionf conjugate = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw ) *
                                      Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), -pitch );
        setLocalCamera( eye, Quaternionf( -conjugate.x, -conjugate.y, -conjugate.z, conjugate.w ), 0.8f, 0.05f );
        return true;
    }
    return false;
}

bool IntroCinematic::getSurfaceHeight( float x, float z, float maxY, float &outHeight ) {
    bool bFound = view.getWorld().getSurfaceHeight( x, z, maxY, outHeight );
    const Matrix4x4f toSpot = spotOrigin.inverse();
    for ( size_t i = 0; i < sets.size() && i < setIsExterior.size(); i++ ) {
        if ( !setIsExterior[i] ) {
            continue;
        }
        const Vector3f local = toSpot.transformPoint( Vector3f( x, maxY, z ) );
        float height = 0.0f;
        if ( sets[i]->getSurfaceHeight( local.x, local.z, local.y, height ) ) {
            const float world = spotOrigin.transformPoint( Vector3f( local.x, height, local.z ) ).y;
            if ( !bFound || world > outHeight ) {
                outHeight = world;
                bFound = true;
            }
        }
    }
    return bFound;
}

void IntroCinematic::collectExteriorCollisionTriangles( std::vector<Vector3f> &outVertices,
                                                        std::vector<uint32_t> &outIndices ) const {
    for ( size_t i = 0; i < sets.size() && i < setIsExterior.size(); i++ ) {
        if ( !setIsExterior[i] ) {
            continue;
        }
        const std::vector<Vector3f> &vertices = sets[i]->getCollisionVertices();
        const std::vector<uint32_t> &indices = sets[i]->getCollisionIndices();
        if ( indices.empty() ) {
            continue;
        }
        const uint32_t base = static_cast<uint32_t>( outVertices.size() );
        outVertices.insert( outVertices.end(), vertices.begin(), vertices.end() );
        outIndices.reserve( outIndices.size() + indices.size() );
        for ( const uint32_t index : indices ) {
            outIndices.push_back( base + index );
        }
    }
}

void IntroCinematic::logGroundContacts( float time ) {
    auto ground = [this]( const Vector3f &p, float &outHeight ) {
        return getSurfaceHeight( p.x, p.z, p.y + 3.0f, outHeight );
    };
    std::string line;
    char text[128];
    Matrix4x4f carWorld;
    if ( getCarWorld( carWorld ) ) {
        for ( const char *pWheel : { "D_RIM_FL", "D_RIM_FR", "D_RIM_RL", "D_RIM_RR" } ) {
            Matrix4x4f hub;
            float height = 0.0f;
            if ( car.isLoaded() && car.getNodeModelTransform( pWheel, hub ) ) {
                const Vector3f p = carWorld.transformPoint( hub.getTranslation() );
                if ( ground( p, height ) ) {
                    snprintf( text, sizeof( text ), " %s hub-ground %.2f", pWheel + 6, p.y - height );
                    line += text;
                }
            }
        }
    }
    auto actor = [&]( const Pnj &pnj, const char *pName ) {
        if ( !pnj.bVisible || pnj.WorldBones.empty() ) {
            return;
        }
        Vector3f lowest( 0.0f, 1e30f, 0.0f );
        for ( const Matrix4x4f &bone : pnj.WorldBones ) {
            const Vector3f p = pnj.World.transformPoint( bone.getTranslation() );
            if ( p.y < lowest.y ) {
                lowest = p;
            }
        }
        float height = 0.0f;
        if ( ground( lowest, height ) ) {
            snprintf( text, sizeof( text ), " %s foot-ground %.2f", pName, lowest.y - height );
            line += text;
        }
    };
    for ( const Pnj &pnj : pnjs ) {
        actor( pnj, pnj.Name.c_str() );
    }
    actor( player, "player" );
    OTDU_LOG_INFO( "GROUND t %.1f:%s\n", time, line.c_str() );
}

void IntroCinematic::offsetSceneOrigin( const Vector3f &delta ) {
    sceneOrigin = Matrix4x4f::Translation( delta ) * sceneOrigin;
    sounds.setSceneOrigin( sceneOrigin );
}

bool IntroCinematic::snapSceneToGround() {
    // The scene rests on the ground like the car would: the four tyres are put
    // on the surface below them, the whole cinematic frame lifted and tilted
    // to that plane (the car, the driver seated in it and the actors standing
    // by it stay together). Least squares plane through the four contacts,
    // two passes (the hubs move a little with the tilt) [INF: how the original
    // grounds a cinematic on a spot is not read; the meeting_tess drive slopes
    // ~0.14 m along the car, a level frame sank the rear wheels and the feet].
    if ( !car.isLoaded() ) {
        return false;
    }
    const float radius = car.getTireRadius();
    for ( int32_t pass = 0; pass < 2; pass++ ) {
        Matrix4x4f carWorld;
        if ( !getCarWorld( carWorld ) ) {
            return false;
        }
        Vector3f hubs[4];
        float targets[4];
        int32_t count = 0;
        for ( const char *pWheel : { "D_RIM_FL", "D_RIM_FR", "D_RIM_RL", "D_RIM_RR" } ) {
            Matrix4x4f hub;
            float ground = 0.0f;
            if ( !car.getNodeModelTransform( pWheel, hub ) ) {
                continue;
            }
            const Vector3f p = carWorld.transformPoint( hub.getTranslation() );
            if ( !getSurfaceHeight( p.x, p.z, p.y + 3.0f, ground ) ) {
                continue;
            }
            hubs[count] = p;
            targets[count] = ground + radius;
            count++;
        }
        if ( count < 3 ) {
            return false;
        }
        // dy(x, z) = d + a (x - cx) + b (z - cz): least squares on the hub errors.
        Vector3f center( 0.0f, 0.0f, 0.0f );
        for ( int32_t i = 0; i < count; i++ ) {
            center = center + hubs[i] * ( 1.0f / count );
        }
        float sxx = 0.0f, sxz = 0.0f, szz = 0.0f, sxe = 0.0f, sze = 0.0f, d = 0.0f;
        for ( int32_t i = 0; i < count; i++ ) {
            const float x = hubs[i].x - center.x, z = hubs[i].z - center.z, e = targets[i] - hubs[i].y;
            sxx += x * x;
            sxz += x * z;
            szz += z * z;
            sxe += x * e;
            sze += z * e;
            d += e / count;
        }
        const float det = sxx * szz - sxz * sxz;
        const float a = std::fabs( det ) > 1e-6f ? ( sxe * szz - sze * sxz ) / det : 0.0f;
        const float b = std::fabs( det ) > 1e-6f ? ( sze * sxx - sxe * sxz ) / det : 0.0f;
        // The tilt: the plane normal (-a, 1, -b); rotate up onto it about the car centre.
        const Vector3f up( 0.0f, 1.0f, 0.0f );
        const Vector3f normal = Vector3f( -a, 1.0f, -b ).normalized();
        const Vector3f axis = up.cross( normal );
        const float sine = axis.length();
        Matrix4x4f tilt = Matrix4x4f::Identity();
        if ( sine > 1e-6f ) {
            const float angle = std::atan2( sine, up.dot( normal ) );
            tilt = Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( axis * ( 1.0f / sine ), angle ) );
        }
        sceneOrigin = Matrix4x4f::Translation( center + Vector3f( 0.0f, d, 0.0f ) ) * tilt * Matrix4x4f::Translation( center * -1.0f ) *
                      sceneOrigin;
        sounds.setSceneOrigin( sceneOrigin );
        OTDU_LOG_INFO( "IntroCinematic: scene grounded on the tyres (pass %d): %+.2f m, slope x %.3f z %.3f\n", pass, d, a, b );
    }
    return true;
}

bool IntroCinematic::loadCar( const CinematicCar::Desc &desc, const std::string &restEntity,
                              const std::string &cinCarEntity, const std::string &carActor ) {
    carRestEntity = restEntity;
    carActorName = carActor;
    auto it = cinCarHashcodes.find( cinCarEntity );
    carEntityHashcode = it != cinCarHashcodes.end() ? it->second : 0;
    if ( !car.load( desc ) ) {
        return false;
    }
    // FIRST_WM load (FUN_009aa2c0): FUN_00a4baf0 looks the scene bank
    // textures up for "CUBECAR" (garage.bnk cubecar.2db, a 512^2 cube shot
    // in the garage) and the car slot manager binds it to the cars as their
    // static cube (FUN_00c62440 -> FUN_00d58d20) instead of the dynamic
    // one (docs/re/car_glass_spec.md).
    bCarStaticCube = false;
    static const bool bNoCubeCar = CmdLine::HasArg( "-nocubecar" ); // comparisons: dynamic cube
    GPUTexture *pCube =
        bNoCubeCar ? nullptr : gpRender->getTextureCache().find( introBank.getPath().c_str(), "CUBECAR" );
    if ( pCube != nullptr ) {
        car.assignEnvironment( *gpRender->getEdenRenderer(), pCube, false );
        bCarStaticCube = true;
    }
    return true;
}

bool IntroCinematic::computeCarWorld( Matrix4x4f &outWorld ) const {
    // .sce actor of the CinCar entity when a cinematic animates it (or the
    // named actor, e.g. PLAYER_CAR of meeting_tess / solar_crown), else the
    // rest pose (PosCam); the tires rest on the scene collision floor.
    Vector3f position;
    Quaternionf rotation;
    bool bPose = false;
    if ( carEntityHashcode != 0 || !carActorName.empty() ) {
        for ( const Playing *pPlaying : playingOrder() ) {
            const CinematicScene *pScene = nullptr;
            float sceneTime = 0.0f;
            const CinematicScene::Object *pObject =
                findActor( *pPlaying, carEntityHashcode, carActorName.empty() ? nullptr : carActorName.c_str(), pScene,
                           sceneTime );
            if ( pObject != nullptr && pObject->findTrack( CinematicScene::kTrackPosition ) ) {
                position = pScene->getOrigin().transformPoint( pObject->samplePosition( sceneTime ) );
                // Actor rotation is scene space; to cinematic-local (the
                // frame fitOnGround / sceneOrigin live in).
                rotation = pObject->sampleRotation( sceneTime ) * Conjugate( pScene->getOriginRotation() );
                bPose = true;
                break;
            }
        }
    }
    if ( !bPose ) {
        auto it = entityPlacements.find( carRestEntity );
        if ( it == entityPlacements.end() ) {
            return false;
        }
        position = it->second.first;
        rotation = it->second.second;
    }
    outWorld = sceneOrigin * fitOnSceneFloor( car, position, rotation );
    return true;
}

bool IntroCinematic::getNeedleSweep( float &outAngleDeg ) const {
    // The garage scene keys its ND_RPM actor (the baked rev needle of the
    // garage cockpit, garage_int_C31591700F0D216E) with a Heading track
    // (C_OUT / M_OUT_out.sce: hold 2.54..5.96 s, sweep to 8.64 s - the retail
    // rev sweep of the cockpit close-up). The port draws the car interior's
    // ND_RPM mesh in those shots instead of the baked cockpit, so the keyed
    // sweep is transferred to it: each key's rotation from the track's first
    // key (the needle at rest) about the sweep axis is the dial angle. The
    // needle mesh of the interior and the baked one are the same California
    // needle (needles_spec.md), so the relative rotation reads 1:1 as the
    // needle driver's angle (setDashboardNeedles).
    outAngleDeg = 0.0f;
    static const bool bLogNeedle = CmdLine::HasArg( "-logneedle" );
    for ( const Playing *pPlaying : playingOrder() ) {
        const CinematicScene *pScene = nullptr;
        float sceneTime = 0.0f;
        const CinematicScene::Object *pObject = findActor( *pPlaying, 0, "ND_RPM", pScene, sceneTime );
        if ( pObject == nullptr ) {
            continue;
        }
        const CinematicScene::Track *pHeading = pObject->findTrack( CinematicScene::kTrackRotation );
        if ( pHeading == nullptr || pHeading->Times.empty() || pHeading->NumComponents < 4 ) {
            continue;
        }
        const Quaternionf qFirst( pHeading->Values[0], pHeading->Values[1], pHeading->Values[2],
                                  pHeading->Values[3] );
        // The sweep axis: the rotation axis of the last key from the first
        // (the whole move; the needle turns about one fixed dial normal).
        const size_t lastKey = pHeading->Times.size() - 1;
        const Quaternionf qLast( pHeading->Values[lastKey * pHeading->NumComponents],
                                 pHeading->Values[lastKey * pHeading->NumComponents + 1],
                                 pHeading->Values[lastKey * pHeading->NumComponents + 2],
                                 pHeading->Values[lastKey * pHeading->NumComponents + 3] );
        const Quaternionf relTotal = qLast * Conjugate( qFirst );
        Vector3f axis( relTotal.x, relTotal.y, relTotal.z );
        const float axisLength = axis.length();
        if ( axisLength < 1e-5f ) {
            return false; // no sweep keyed (the track holds one pose)
        }
        axis = axis * ( 1.0f / axisLength );
        const Quaternionf rel = pObject->sampleRotation( sceneTime ) * Conjugate( qFirst );
        const float angle = 2.0f * std::atan2( axis.dot( Vector3f( rel.x, rel.y, rel.z ) ), rel.w );
        outAngleDeg = angle * ( 180.0f / kPI );
        if ( bLogNeedle ) {
            static float lastLogged = -1.0f;
            if ( std::abs( outAngleDeg - lastLogged ) > 2.0f ) {
                lastLogged = outAngleDeg;
                OTDU_LOG_INFO( "IntroCinematic: ND_RPM sweep t %.2f -> %.1f deg (axis %.2f %.2f %.2f)\n", sceneTime,
                               outAngleDeg, axis.x, axis.y, axis.z );
            }
        }
        return true;
    }
    return false;
}

Matrix4x4f IntroCinematic::fitOnSceneFloor( const CinematicCar &fitCar, const Vector3f &position,
                                            const Quaternionf &rotation ) const {
    return fitCar.fitOnGround( position, rotation, [this]( float x, float z, float y, float &outHeight ) {
        if ( !bSceneMoved || !setup.bExteriorCollision ) {
            return FindHavokFloor( collision, Vector3f( x, y, z ), 0.0f, 3.0f, outHeight );
        }
        // Scene frame moved off the spot frame (liftOntoFloor / placeEntityAt):
        // the spot collision through the world, else the island terrain.
        const Vector3f world = sceneOrigin.transformPoint( Vector3f( x, y, z ) );
        const Vector3f spotLocal = spotOrigin.inverse().transformPoint( world );
        float worldY = 0.0f;
        if ( FindHavokFloor( collision, spotLocal, 0.0f, 3.0f, outHeight ) ) {
            worldY = outHeight + spotOrigin.getTranslation().y;
        } else if ( !const_cast<EdenSceneView &>( view ).getWorld().getHeight( world.x, world.z, worldY ) ) {
            return false;
        }
        outHeight = worldY - sceneOrigin.getTranslation().y;
        return true;
    } );
}

void IntroCinematic::renderOffscreen() {
    if ( !bLoaded ) {
        return;
    }
    updateView();
    updatePnjs();
    updateProps();
    updateCarDoors();
    if ( car.isLoaded() && !bCarEnvironment && !bCarStaticCube ) {
        if ( GPUTexture *pCube = view.getEnvironmentCube() ) {
            car.assignEnvironment( *gpRender->getEdenRenderer(), pCube );
            bCarEnvironment = true;
        }
    }
    // Showroom cars: the dynamic cube of the view [INF: the shop scene car
    // slots (FUN_00a507e0) bind the cars of the scene to the view cube].
    for ( ShowCar &showCar : showCars ) {
        if ( !showCar.bEnvironment ) {
            if ( GPUTexture *pCube = view.getEnvironmentCube() ) {
                showCar.Car->assignEnvironment( *gpRender->getEdenRenderer(), pCube );
                showCar.bEnvironment = true;
            }
        }
    }
    view.renderOffscreen();
}

void IntroCinematic::drawFade() {
    // Shot start transition (+0x14 type 1: fade from a colour over +0x18 s)
    // [INF].
    if ( main.pDef == nullptr ) {
        return;
    }
    const Cinematic::Shot *pShot = main.pDef->Cin.getShot( main.Time );
    if ( pShot == nullptr || pShot->FadeType != 1 || pShot->FadeDuration <= 0.0f ) {
        return;
    }
    const float elapsed = main.Time - pShot->Start;
    if ( elapsed < 0.0f || elapsed >= pShot->FadeDuration ) {
        return;
    }
    Renderer2D &renderer = gpRender->get2DRenderer();
    renderer.begin();
    renderer.drawRect( Rect2D( -10000.0f, -10000.0f, 20000.0f, 20000.0f ),
                       PackColor( pShot->FadeColor, 1.0f - elapsed / pShot->FadeDuration ) );
    renderer.end();
}

void IntroCinematic::draw() {
    if ( !bLoaded ) {
        return;
    }
    view.draw();
    drawFade();
    static const bool bNoBlackStripes = CmdLine::HasArg( "-noblackstripes" ); // A/B: the letterbox art
    if ( !bNoBlackStripes && general.isLoaded() && gpConfig->getVideoSettings().ScreenRatio == 0 &&
         main.pDef != nullptr && !main.bNoCamera && !bCameraOverride ) {
        general.drawClipOnly( gpRender->get2DRenderer(), "/black_stripes" );
    }
    if ( ui.isLoaded() ) {
        ui.draw( gpRender->get2DRenderer() );
    }
    if ( general.isLoaded() ) {
        if ( bLogoTv ) {
            general.drawClipOnly( gpRender->get2DRenderer(), "/logo_TV" );
        }
        if ( bLogoSc ) {
            general.drawClipOnly( gpRender->get2DRenderer(), "/logo_SC" );
        }
        general.drawClipOnly( gpRender->get2DRenderer(), "/pop_goal" );
        general.drawClipOnly( gpRender->get2DRenderer(), "/pop_tutorial" );
        if ( bDialogBoxVisible ) {
            general.drawClipOnly( gpRender->get2DRenderer(), "/dialog_box" );
        }
    }
    // The authored captions (bottom center) over the scene UI.
    if ( bSubtitles && main.pDef != nullptr ) {
        subtitles.draw( gpRender->get2DRenderer(), main.Time );
    }
}

void IntroCinematic::drawUi() {
    if ( !bLoaded ) {
        return;
    }
    if ( ui.isLoaded() ) {
        ui.draw( gpRender->get2DRenderer() );
    }
    if ( general.isLoaded() ) {
        if ( bLogoTv ) {
            general.drawClipOnly( gpRender->get2DRenderer(), "/logo_TV" );
        }
        if ( bLogoSc ) {
            general.drawClipOnly( gpRender->get2DRenderer(), "/logo_SC" );
        }
        general.drawClipOnly( gpRender->get2DRenderer(), "/pop_goal" );
        general.drawClipOnly( gpRender->get2DRenderer(), "/pop_tutorial" );
        if ( bDialogBoxVisible ) {
            general.drawClipOnly( gpRender->get2DRenderer(), "/dialog_box" );
        }
    }
}

void IntroCinematic::drawDebugPaths( Renderer2D &renderer ) {
    if ( !bLoaded || main.pDef == nullptr ) {
        return;
    }
    auto Rgba = []( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) {
        return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 );
    };
    const EdenSceneView::Camera &cam = camera;
    // The camera basis: forward = conj(Rotation) (0, 0, 1) - the convention
    // of the [scene] logs (gm_cin_viewer.cpp).
    const Quaternionf conj( -cam.Rotation.x, -cam.Rotation.y, -cam.Rotation.z,
                            cam.Rotation.w );
    const Vector3f fwd = conj.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
    const Vector3f upWorld =
        fabsf( fwd.y ) > 0.99f ? Vector3f( 0.0f, 0.0f, -1.0f )
                               : Vector3f( 0.0f, 1.0f, 0.0f );
    const Vector3f right = fwd.cross( upWorld ).normalized();
    const Vector3f up = right.cross( fwd ).normalized();
    const float canvasW = renderer.getCanvasWidth();
    const float canvasH = renderer.getCanvasHeight();
    const float aspect = canvasW / canvasH;
    const float tanF = tanf( cam.Fov * 0.5f );
    auto project = [&]( const Vector3f &world, Vector2f &out ) -> bool {
        const Vector3f rel = world - cam.Position;
        const float z = rel.dot( fwd );
        if ( z <= Max( cam.Near, 0.05f ) ) {
            return false;
        }
        const float x = rel.dot( right );
        const float y = rel.dot( up );
        out.x = canvasW * ( 0.5f + x / ( 2.0f * z * tanF * aspect ) );
        out.y = canvasH * ( 0.5f - y / ( 2.0f * z * tanF ) );
        return true;
    };
    // A path as a chain of small rects (the 2D batcher has no line
    // primitive); the keys are strided so a baked 733-key path stays cheap.
    auto drawPath = [&]( const Vector3f *pPoints, size_t count, uint32_t color,
                         float thickness ) {
        Vector2f previous;
        bool bHave = false;
        for ( size_t i = 0; i < count; i++ ) {
            Vector2f point;
            if ( !project( pPoints[i], point ) ) {
                bHave = false;
                continue;
            }
            if ( bHave ) {
                const Vector2f delta = point - previous;
                const float length = std::sqrt( delta.x * delta.x + delta.y * delta.y );
                const int steps = Max( 1, static_cast<int>( length / 12.0f ) );
                for ( int s = 0; s < steps; s++ ) {
                    const float f = static_cast<float>( s ) / steps;
                    const Vector2f at = previous + delta * f;
                    renderer.drawRect(
                        Rect2D( at.x - thickness * 0.5f,
                                at.y - thickness * 0.5f, thickness, thickness ),
                        color );
                }
            }
            previous = point;
            bHave = true;
        }
    };
    renderer.begin();
    for ( const CinematicScene &scene : main.pDef->Scenes ) {
        const Matrix4x4f localToWorld = sceneOrigin * scene.getOrigin();
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            const CinematicScene::Track *pPosition =
                object.findTrack( CinematicScene::kTrackPosition );
            if ( pPosition == nullptr || pPosition->Times.size() < 2 ) {
                continue;
            }
            const size_t keys = pPosition->Times.size();
            const size_t stride = Max<size_t>( 1, keys / 64 );
            std::vector<Vector3f> points;
            for ( size_t k = 0; k < keys; k += stride ) {
                const Vector3f local = object.samplePosition( pPosition->Times[k] );
                points.push_back( localToWorld.transformPoint( local ) );
            }
            const bool bCamera = object.isCamera();
            drawPath( points.data(), points.size(),
                      bCamera ? Rgba( 80, 220, 255 ) : Rgba( 255, 150, 40 ),
                      bCamera ? 4.0f : 3.0f );
        }
        // The scene origin axes (x red, y green, z blue), 2 m.
        const Vector3f origin = localToWorld.getTranslation();
        const Vector3f axes[3] = { localToWorld.transformVector( Vector3f( 2.0f, 0.0f, 0.0f ) ),
                                   localToWorld.transformVector( Vector3f( 0.0f, 2.0f, 0.0f ) ),
                                   localToWorld.transformVector( Vector3f( 0.0f, 0.0f, 2.0f ) ) };
        const uint32_t colors[3] = { Rgba( 255, 60, 60 ), Rgba( 60, 255, 60 ),
                                     Rgba( 80, 80, 255 ) };
        Vector2f originScreen;
        if ( project( origin, originScreen ) ) {
            renderer.drawRect( Rect2D( originScreen.x - 3.0f, originScreen.y - 3.0f, 6.0f, 6.0f ),
                               Rgba( 255, 255, 255 ) );
        }
        for ( int a = 0; a < 3; a++ ) {
            const Vector3f pair[2] = { origin, origin + axes[a] };
            drawPath( pair, 2, colors[a], 3.0f );
        }
    }
    renderer.end();
}

bool IntroCinematic::findHouseSpot( Vector3f &outPosition, float &outHeading ) const {
    // islands/<island>/level/spots.bin: ".SPT", "SPTA" { u32 size } then
    // 0x110 byte records { u64 id; char bank[8]; vec3 position; f32; f32
    // heading; ... }. The intro house is Ibiza's; the houses of the other
    // islands stand in their own list (walked into from FREERIDE).
    for ( const char *pIsland : { "ibiza", "hawai", "casino" } ) {
        if ( !setup.HouseIsland.empty() && setup.HouseIsland != pIsland ) {
            continue;
        }
        std::vector<uint8_t> data;
        if ( gpFile->loadFile( ( gpFile->getBankFolder() + "islands/" + pIsland + "/level/spots.bin" ).c_str(), data ) &&
             data.size() >= 0x20 && memcmp( data.data() + 0x10, "SPTA", 4 ) == 0 &&
             findHouseSpotIn( data, outPosition, outHeading ) ) {
            return true;
        }
    }
    return false;
}

bool IntroCinematic::findHouseSpotIn( const std::vector<uint8_t> &data, Vector3f &outPosition, float &outHeading ) const {
    uint32_t size = 0;
    memcpy( &size, data.data() + 0x14, 4 );
    const uint64_t id = GetIdentifier64bit( setup.HouseSpot.c_str() );
    for ( size_t offset = 0x20; offset + 0x110 <= data.size() && offset < 0x20 + size; offset += 0x110 ) {
        uint64_t recordId = 0;
        memcpy( &recordId, data.data() + offset, 8 );
        bool bMatch = recordId == id;
        if ( !bMatch && !setup.HouseIsland.empty() ) {
            // The shop spots: the id hashes the full record name (the photo
            // shop of Ibiza is EPHOT_B_3337I, Hawai's EPHOT_B_9830 [DATA
            // spots.bin]) while the walk carries the record's 8 byte bank
            // field (EPHOT_B) - the same short name on every island, so this
            // fallback only runs with the island pinned (HouseIsland).
            char bank[8];
            memcpy( bank, data.data() + offset + 8, 8 );
            bMatch = strncmp( bank, setup.HouseSpot.c_str(), 8 ) == 0;
        }
        if ( !std::isnan( setup.HousePickX ) ) {
            float px = 0.0f, pz = 0.0f;
            memcpy( &px, data.data() + offset + 0x10, 4 );
            memcpy( &pz, data.data() + offset + 0x18, 4 );
            bMatch = ( px - setup.HousePickX ) * ( px - setup.HousePickX ) + ( pz - setup.HousePickZ ) * ( pz - setup.HousePickZ ) < 60.0f * 60.0f;
        }
        if ( bMatch ) {
            memcpy( &outPosition, data.data() + offset + 0x10, 12 );
            memcpy( &outHeading, data.data() + offset + 0x20, 4 );
            outHeading += setup.HouseHeadingOffset;
            OTDU_LOG_INFO( "IntroCinematic: house %s at (%.2f, %.2f, %.2f) heading %.4f\n", setup.HouseSpot.c_str(),
                           outPosition.x, outPosition.y, outPosition.z, outHeading );
            return true;
        }
    }
    return false;
}

namespace {
void ReadPlacement( XmbValue placement, Vector3f &position, Quaternionf &rotation, Vector3f &scale ) {
    rotation = Quaternionf( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                            placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
    position = Vector3f( placement["pos"]["X"].asFloat(), placement["pos"]["Y"].asFloat(), placement["pos"]["Z"].asFloat() );
    scale = Vector3f( placement["scale"]["X"].asFloat( 1.0f ), placement["scale"]["Y"].asFloat( 1.0f ),
                      placement["scale"]["Z"].asFloat( 1.0f ) );
}
} // namespace

// Models of a scene bank by entity (spec_pool_interior 6, P6):
// Scenery and Entity__Actor__Mesh (D_REFLECT) in the bank frame,
// Entity__Actor at entityTRS * rootLocal^-1 (the entity transform replaces
// the root node: the furniture roots sit at their absolute position),
// Entity__Actor__Interior (the views-through-windows LODs of an exterior
// bank) skipped when the real interior is loaded.
void IntroCinematic::loadBankModels( const Bank &bank, const BankHashcodes &hashcodes, const XmbDocument &entities,
                                     bool bSkipInteriorLods ) {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    auto loadModel = [&]( uint64_t rsc ) -> std::unique_ptr<EdenModel> {
        const BankEntry *pScene = hashcodes.find( rsc, 0, 0 );
        if ( pScene == nullptr ) {
            return nullptr;
        }
        const BankEntry *pGeometry = bank.findEntry( pScene->Name.c_str(), "3dg" );
        const BankEntry *pMaterials = bank.findEntry( pScene->Name.c_str(), "2dm" );
        auto pModel = std::make_unique<EdenModel>();
        pModel->setKeepCollision( bSkipInteriorLods ); // exterior: ground of the cinematics placed on it
        if ( pGeometry == nullptr ||
             !pModel->load( *pRenderer, *pScene, *pGeometry, pMaterials, pLibrary, bank.getPath().c_str() ) ) {
            return nullptr;
        }
        // GSReflection projective slots, .UVA animations, NIGHTLMAP from the
        // weather (spec_pool_interior P2..P5).
        view.registerProjectiveMaterials( *pModel );
        pModel->bindUVAnimations( bank );
        view.registerLightmapMaterials( *pModel, bSkipInteriorLods ? EdenSceneView::eLightmapScope::Exterior
                                                                   : EdenSceneView::eLightmapScope::Interior );
        return pModel;
    };
    auto firstMesh = []( XmbValue entry, const char *pField ) -> uint64_t {
        for ( uint32_t j = 0; j < entry.getArraySize( pField ); j++ ) {
            XmbValue mesh = entry.getArrayElement( pField, j );
            if ( mesh["rscSubType"].asInt() == 0 ) {
                return mesh["rsc"].asUInt64();
            }
        }
        return 0;
    };
    XmbValue scenery = entities.getRoot()["ExportType_Scenery"];
    for ( uint32_t i = 0; i < scenery.getArraySize( "ArrayOf_Scenery" ); i++ ) {
        XmbValue entry = scenery.getArrayElement( "ArrayOf_Scenery", i );
        for ( uint32_t j = 0; j < entry.getArraySize( "Model" ); j++ ) {
            XmbValue model = entry.getArrayElement( "Model", j );
            if ( model["rscSubType"].asInt() != 0 ) {
                continue;
            }
            if ( auto pModel = loadModel( model["rsc"].asUInt64() ) ) {
                sets.push_back( std::move( pModel ) );
                setIsExterior.push_back( bSkipInteriorLods );
            }
        }
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor__Mesh" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Actor__Mesh", i );
        if ( auto pModel = loadModel( firstMesh( entry, "mesh" ) ) ) {
            // GSReflection projectors of the D_REFLECT scene (FUN_00d7e950).
            std::string bankName = bank.getPath();
            const size_t slash = bankName.find_last_of( "/\\" );
            if ( slash != std::string::npos ) {
                bankName = bankName.substr( slash + 1 );
            }
            view.registerReflectionScene( *pModel, bSkipInteriorLods ? spotOrigin : sceneOrigin, bankName.c_str(),
                                          entry["AvatarReflectlayername1"].asString().c_str(),
                                          entry["AvatarReflectlayername2"].asString().c_str() );
            reflectSets.push_back( pModel.get() );
            sets.push_back( std::move( pModel ) );
            setIsExterior.push_back( bSkipInteriorLods );
        }
    }
    if ( !bSkipInteriorLods ) {
        for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor__Interior" ); i++ ) {
            XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Actor__Interior", i );
            if ( auto pModel = loadModel( firstMesh( entry, "mesh" ) ) ) {
                sets.push_back( std::move( pModel ) );
                setIsExterior.push_back( bSkipInteriorLods );
            }
        }
    }
    // The slot machines' reels (Entity__Actor__SlotMachineSpinner, icasslo1:
    // 140 of them) are actors of their own: one model each, so each reel's
    // TD_Casino_scroll material takes its own scroll constants [DATA].
    // -noslotspinners leaves them out.
    static const bool bSpinners = !CmdLine::HasArg( "-noslotspinners" );
    // The machines' handles (Entity__Actor__SlotMachineTrigger, 38, own
    // skeleton + pull Anim) and screens (Entity__Actor__CasinoFlashScreen and
    // its Ranking / Jackpot kinds, Movie 'Cas_SlotScreen', Material 'flash')
    // are actors of their own too [DATA]; they were never loaded.
    for ( const char *pActorArray :
          { "ArrayOf_Entity__Actor", "ArrayOf_Entity__Actor__SlotMachineSpinner", "ArrayOf_Entity__Actor__SlotMachineTrigger",
            "ArrayOf_Entity__Actor__CasinoFlashScreen", "ArrayOf_Entity__Actor__CasinoFlashScreen__CasinoFlashScreenRanking",
            "ArrayOf_Entity__Actor__CasinoFlashScreen__CasinoFlashScreenJackpot", "ArrayOf_Entity__Actor__Neon",
            "ArrayOf_Entity__Actor__NeonSlotMachine" } ) {
    if ( !bSpinners && strcmp( pActorArray, "ArrayOf_Entity__Actor" ) != 0 ) {
        continue;
    }
    // The neon actors only in the slots room (the bank with the machines):
    // they were checked there; the other banks keep their old prop set [INF].
    if ( strstr( pActorArray, "Neon" ) != nullptr &&
         entityExport.getArraySize( "ArrayOf_Entity__Actor__CasinoGame__SlotMachine" ) == 0 ) {
        continue;
    }
    for ( uint32_t i = 0; i < entityExport.getArraySize( pActorArray ); i++ ) {
        XmbValue actor = entityExport.getArrayElement( pActorArray, i );
        Prop prop;
        prop.Name = actor["__Name"].asString();
        // HOLOGRAMS (new_intro: the garage icon + pulse rings above the
        // garage door): entities are created active (+0x9c = 7, bit 0 =
        // active: updated FUN_00dc7290 / shown FUN_00b45960, ctor 0xb94ef0);
        // FUN_009b4360 / FUN_009b42d0 clear / set that bit of "HOLOGRAMS"
        // only around photo mode, the shop UI and the LIVING sub-modes,
        // never in AVATAR_CHOICE, and no cinematic of the bank references
        // it: shown all along [CODE + DATA].
        prop.Model = loadModel( firstMesh( actor, "mesh" ) );
        if ( !prop.Model ) {
            continue;
        }
        prop.EntityHashcode = actor["__Hc"].asUInt64();
        Vector3f position, scale;
        Quaternionf rotation;
        ReadPlacement( actor["orient_pos_scale"], position, rotation, scale );
        prop.Local = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( rotation ) *
                     Matrix4x4f::Scale( scale );
        // The garage scene's rev-counter needle actor (garage_int ND_RPM): its
        // rest heading is the dial-0 pose the C_OUT scene's Heading track
        // sweeps from (the needle mesh sits on the actor origin). getNeedleSweep
        // keys the car's interior needle on it (gm_first_walk_mode).
        if ( prop.Name == "ND_RPM" ) {
            needleActorRest = rotation;
            bNeedleActorRest = true;
        }
        // -logprops: scene-local placement (the frame of -walkat) and materials.
        static const bool bLogProps = CmdLine::HasArg( "-logprops" );
        if ( bLogProps ) {
            std::string materials;
            for ( const EdenMaterial &material : prop.Model->getMaterials() ) {
                materials += material.Name + ( material.pProgram != nullptr ? "(" + material.pProgram->Name + ") " : " " );
            }
            OTDU_LOG_INFO( "IntroCinematic: prop '%s' at (%.2f, %.2f, %.2f): %s\n", prop.Name.c_str(), position.x,
                           position.y, position.z, materials.c_str() );
        }
        for ( const SceneNode &node : prop.Model->getScene().getNodes() ) {
            if ( node.Parent < 0 ) {
                prop.RootInverse = node.Local.inverse();
                break;
            }
        }
        // An animated prop brings its skeleton (key_ring_ferrari.XSB,
        // a-phone.XSB of meeting_tess) [DATA].
        if ( const BankEntry *pMesh = hashcodes.find( firstMesh( actor, "mesh" ), 0, 0 ) ) {
            if ( const BankEntry *pSkeleton = bank.findEntry( pMesh->Name.c_str(), "xsb" ) ) {
                auto skeleton = std::make_unique<EdenSkeleton>();
                if ( skeleton->parse( pSkeleton->pData, pSkeleton->Size ) ) {
                    prop.Skeleton = std::move( skeleton );
                }
            }
        }
        // The casino neons (Entity__Actor__Neon: the machines' top pay art
        // 'PartOfSM<type>_gain_<n>', the room signs; Entity__Actor__NeonSlotMachine:
        // the machines' buttons) are TD_NEONDECAL glows the neon manager lights
        // (emissive = dp4( NEONGLOW, AnimValue ) x WeatherEmissive). Props are
        // not registered there, so they drew black / unlit. Lit steadily here:
        // AnimValue = the entity's Red / Green / Blue IlluminationFactor (the
        // buttons: their NeonInactive rest state), WeatherEmissive 1 [DATA:
        // the factors; the steady light (no TimeFactor blink) is INF].
        // -nocasinoneonlight leaves them as loaded.
        static const bool bNeonLight = !CmdLine::HasArg( "-nocasinoneonlight" );
        const bool bNeonActor = strcmp( pActorArray, "ArrayOf_Entity__Actor__Neon" ) == 0;
        const bool bSlotNeon = strcmp( pActorArray, "ArrayOf_Entity__Actor__NeonSlotMachine" ) == 0;
        if ( bNeonLight && ( bNeonActor || bSlotNeon ) ) {
            if ( neonCurves.empty() ) {
                loadNeonCurves( entities );
            }
            NeonAnim anim;
            anim.PropIndex = props.size();
            if ( bNeonActor ) {
                // Entity__Actor__Neon: one state, its 'Neon' params (always on).
                readNeonState( actor["Neon"], anim.States[0] );
                anim.States[0].bCanActivate = true;
                anim.States[0].bDefined = true;
            } else {
                static const char *const kStates[] = { "NeonInactive", "NeonActive", "NeonWin", "NeonLoose",
                                                       "NeonJackpot", "NeonWinRelaunch", "NeonLooseRelaunch" };
                for ( int32_t k = 0; k < static_cast<int32_t>( eNeonState::Count ); k++ ) {
                    XmbValue state = actor[kStates[k]];
                    if ( !state.isValid() ) {
                        continue;
                    }
                    readNeonState( state["NeonParams"], anim.States[k] );
                    anim.States[k].bCanActivate = state["CanBeActivate"].asInt() != 0;
                    anim.States[k].Duration = state["Duration"].asFloat( -1.0f );
                    anim.States[k].bDefined = true;
                }
            }
            // FUN_00b43fb0: UseDecalRandTime -> start offset = rand * 3.0 (the
            // engine LCG; a stable per-entity seed here [INF]).
            XmbValue first = bNeonActor ? actor["Neon"] : actor["NeonInactive"]["NeonParams"];
            if ( first["UseDecalRandTime"].asInt() != 0 ) {
                uint32_t seed = static_cast<uint32_t>( actor["__Hc"].asUInt64() ^ ( actor["__Hc"].asUInt64() >> 32 ) );
                seed = seed * 0x7ff8a3edu + 0x2aa01d31u;
                anim.TimeOffset = static_cast<float>( seed / 4294967296.0 * 3.0 );
            }
            if ( CmdLine::HasArg( "-logcasinoneons" ) && bSlotNeon ) {
                std::string states;
                for ( int32_t k = 0; k < static_cast<int32_t>( eNeonState::Count ); k++ ) {
                    states += anim.States[k].bCanActivate ? '1' : '0';
                }
                OTDU_LOG_INFO( "IntroCinematic: slot neon %016llx '%s' states (in act win loo jac wre lre) %s, win illum %.2f %.2f %.2f "
                               "curve %d time %.2f, loose illum %.2f %.2f %.2f\n",
                               static_cast<unsigned long long>( prop.EntityHashcode ), prop.Name.c_str(), states.c_str(),
                               anim.States[2].Channels[0].Illumination,
                               anim.States[2].Channels[1].Illumination, anim.States[2].Channels[2].Illumination,
                               anim.States[2].Channels[0].pCurve != nullptr, anim.States[2].Channels[0].TimeFactor,
                               anim.States[3].Channels[0].Illumination, anim.States[3].Channels[1].Illumination,
                               anim.States[3].Channels[2].Illumination );
            }
            prop.NeonIndex = static_cast<int32_t>( neonAnims.size() );
            neonAnims.push_back( anim );
            XmbValue params = bNeonActor ? actor["Neon"] : actor["NeonInactive"]["NeonParams"];
            static int32_t loggedCurves = 0;
            if ( CmdLine::HasArg( "-logcasinoneons" ) && loggedCurves < 4 ) {
                for ( const char *pChannel : { "RedParams", "GreenParams", "BlueParams" } ) {
                    const uint64_t curve = params[pChannel]["CurveFactor"].asUInt64();
                    if ( curve == 0 ) {
                        continue;
                    }
                    loggedCurves++;
                    const BankEntry *pCurve = hashcodes.find( curve );
                    std::string head;
                    for ( uint32_t k = 0; pCurve != nullptr && k < pCurve->Size && k < 96; k++ ) {
                        char hex[4];
                        snprintf( hex, sizeof( hex ), "%02x", pCurve->pData[k] );
                        head += hex;
                        if ( k % 4 == 3 ) {
                            head += ' ';
                        }
                    }
                    OTDU_LOG_INFO( "IntroCinematic: neon '%s' %s curve %016llx -> %s.%s (%u bytes, sub %d): %s\n",
                                   prop.Name.c_str(), pChannel, static_cast<unsigned long long>( curve ),
                                   pCurve != nullptr ? pCurve->Name.c_str() : "(missing)",
                                   pCurve != nullptr ? pCurve->Extension.c_str() : "",
                                   pCurve != nullptr ? pCurve->Size : 0u, pCurve != nullptr ? pCurve->SubType : -1,
                                   head.c_str() );
                }
            }
            const Vector4f steady( params["RedParams"]["IlluminationFactor"].asFloat( 1.0f ),
                                 params["GreenParams"]["IlluminationFactor"].asFloat( 1.0f ),
                                 params["BlueParams"]["IlluminationFactor"].asFloat( 1.0f ), 0.0f );
            for ( EdenMaterial &material : prop.Model->getMaterials() ) {
                if ( EdenNeons::IsNeonSetMaterial( material ) ) {
                    material.Parameters[EdenNeons::kWeatherEmissive] = Vector4f( 1.0f, 1.0f, 1.0f, 0.0f );
                    material.Parameters[EdenNeons::kAnimValue] = steady;
                }
            }
        }
        prop.bExterior = bSkipInteriorLods;
        props.push_back( std::move( prop ) );
    }
    }
    // Contact shadow decals: every prop carries a "patch" quad (patch_occlu_01,
    // SRCALPHA / INVSRCALPHA) drawn with TD_Building (vs2 / ps2, bucket 0x21).
    // The turntables' (cf_plat.2DM) is authored in TD_Skin2, a family whose
    // vertex shaders are all skinned and whose pixel shader lights skin: on the
    // static quad it paints the solid black rectangles under the decks [DATA].
    // Such a decal takes the settings of a TD_Building patch of the bank that
    // samples the same texture [INF: how the original copes is not read].
    constexpr uint64_t kTdBuilding = 0x183157b81c6cff57ull, kTdSkin2 = 0xd5013ed0dbcdcdcdull;
    auto isPatch = []( const EdenMaterial &material ) { return material.Name.rfind( "patch", 0 ) == 0; };
    auto findTemplate = [&]( uint64_t texture ) -> const EdenMaterial * {
        for ( const Prop &prop : props ) {
            for ( const EdenMaterial &material : prop.Model->getMaterials() ) {
                if ( isPatch( material ) && material.ShaderSetGUID == kTdBuilding && material.pProgram != nullptr &&
                     material.TextureHashes[0] == texture ) {
                    return &material;
                }
            }
        }
        return nullptr;
    };
    for ( Prop &prop : props ) {
        for ( EdenMaterial &material : prop.Model->getMaterials() ) {
            if ( !isPatch( material ) || material.ShaderSetGUID != kTdSkin2 ) {
                continue;
            }
            if ( const EdenMaterial *pTemplate = findTemplate( material.TextureHashes[0] ) ) {
                OTDU_LOG_INFO( "IntroCinematic: '%s' patch decal (TD_Skin2) drawn as '%s' (%s)\n", prop.Name.c_str(),
                               pTemplate->Name.c_str(), pTemplate->pProgram->Name.c_str() );
                material = *pTemplate;
            }
        }
    }
}

// The furniture slots of a house bank (Entity__MyHouseObject: __Name, the
// orient_pos_scale placement, Type / Group / DefaultColor): the model is the
// myhouse bank of the same name (sofaL_E -> interior/myhouse/sofal_e.bnk
// [DATA: the folder]) placed at the slot transform, like an Entity__Actor
// prop without a cinematic (the .sce actor of a cinematic still animates the
// slot by its entity hashcode, as for the props).
bool IntroCinematic::loadFurnitureModel( const std::string &bankName, Prop &prop ) {
    // interior/myhouse/<bank>.bnk; the slot names of a set carry per-instance
    // suffixes (ChaiSalD_04): the bank is the base name (chaisald.bnk) [DATA:
    // the folder].
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    std::string lower = ToLower( bankName );
    std::string path = gpFile->getBankFolder() + "interior/myhouse/" + lower + ".bnk";
    auto pBank = std::make_unique<Bank>();
    if ( !pBank->load( path.c_str() ) ) {
        const size_t underscore = lower.rfind( '_' );
        if ( underscore != std::string::npos && underscore > 0 ) {
            const std::string basePath = gpFile->getBankFolder() + "interior/myhouse/" + lower.substr( 0, underscore ) + ".bnk";
            if ( pBank->load( basePath.c_str() ) ) {
                path = basePath;
                lower = lower.substr( 0, underscore ); // the bank is the base model
            }
        }
    }
    if ( pBank->getEntries().empty() ) {
        OTDU_LOG_WARN( "IntroCinematic: furniture bank '%s' not found\n", path.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( *pBank, pBank->getPath().c_str() );
    BankHashcodes hashcodes;
    hashcodes.load( *pBank );
    const BankEntry *pFurnEntities = pBank->findEntry( "entities.xmb" );
    XmbDocument furnEntities;
    if ( pFurnEntities == nullptr || !furnEntities.load( pFurnEntities->pData, pFurnEntities->Size ) ) {
        OTDU_LOG_WARN( "IntroCinematic: furniture bank '%s' has no entities\n", path.c_str() );
        return false;
    }
    // The model: the first mesh entity of the bank (its own Entity__Actor).
    XmbValue furnExport = furnEntities.getRoot()["ExportType_Entity"];
    XmbValue meshEntry;
    bool bFound = false;
    for ( const char *pArray : { "ArrayOf_Entity__Actor", "ArrayOf_Entity__Actor__Mesh" } ) {
        if ( furnExport.getArraySize( pArray ) > 0 ) {
            meshEntry = furnExport.getArrayElement( pArray, 0 );
            bFound = true;
            break;
        }
    }
    uint64_t rsc = 0;
    if ( bFound ) {
        for ( uint32_t j = 0; j < meshEntry.getArraySize( "mesh" ); j++ ) {
            XmbValue mesh = meshEntry.getArrayElement( "mesh", j );
            if ( mesh["rscSubType"].asInt() == 0 ) {
                rsc = mesh["rsc"].asUInt64();
                break;
            }
        }
    } else {
        // The wall / floor finish banks (ground_nn, wall_nn) are a Scenery
        // entry (ArrayOf_Scenery[0].Model) [DATA].
        XmbValue scenery = furnEntities.getRoot()["ExportType_Scenery"];
        if ( scenery.getArraySize( "ArrayOf_Scenery" ) > 0 ) {
            XmbValue entry = scenery.getArrayElement( "ArrayOf_Scenery", 0 );
            for ( uint32_t j = 0; j < entry.getArraySize( "Model" ); j++ ) {
                XmbValue model = entry.getArrayElement( "Model", j );
                if ( model["rscSubType"].asInt() == 0 ) {
                    rsc = model["rsc"].asUInt64();
                    break;
                }
            }
        }
    }
    const BankEntry *pScene = rsc != 0 ? hashcodes.find( rsc, 0, 0 ) : nullptr;
    if ( pScene == nullptr ) {
        return false;
    }
    auto pModel = std::make_unique<EdenModel>();
    const BankEntry *pGeometry = pBank->findEntry( pScene->Name.c_str(), "3dg" );
    const BankEntry *pMaterials = pBank->findEntry( pScene->Name.c_str(), "2dm" );
    if ( pGeometry == nullptr ||
         !pModel->load( *pRenderer, *pScene, *pGeometry, pMaterials, pLibrary, pBank->getPath().c_str() ) ) {
        OTDU_LOG_WARN( "IntroCinematic: furniture '%s' could not be loaded\n", bankName.c_str() );
        return false;
    }
    view.registerProjectiveMaterials( *pModel );
    pModel->bindUVAnimations( *pBank );
    view.registerLightmapMaterials( *pModel, EdenSceneView::eLightmapScope::Interior );
    if ( CmdLine::HasArg( "-logfurniture" ) ) {
        std::string names;
        for ( const EdenMaterial &material : pModel->getMaterials() ) {
            names += material.Name + " ";
        }
        OTDU_LOG_INFO( "IntroCinematic: furniture bank %s materials: %s\n", lower.c_str(), names.c_str() );
    }
    prop.Model = std::move( pModel );
    prop.RootInverse = Matrix4x4f();
    for ( const SceneNode &node : prop.Model->getScene().getNodes() ) {
        if ( node.Parent < 0 ) {
            prop.RootInverse = node.Local.inverse();
            break;
        }
    }
    prop.FurnitureBank = lower;
    // The bank stays loaded with the scene (a replaced model's bank too: its
    // textures may still be in flight, released at unload).
    furnitureBanks.push_back( std::move( pBank ) );
    return true;
}

void IntroCinematic::loadFurniture( const XmbDocument &entities ) {
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    const uint32_t numSlots = entityExport.getArraySize( "ArrayOf_Entity__MyHouseObject" );
    if ( numSlots == 0 || CmdLine::HasArg( "-nofurniture" ) ) {
        return;
    }
    static const bool bLogProps = CmdLine::HasArg( "-logprops" );
    size_t placed = 0;
    for ( uint32_t i = 0; i < numSlots; i++ ) {
        XmbValue slot = entityExport.getArrayElement( "ArrayOf_Entity__MyHouseObject", i );
        const std::string name = slot["__Name"].asString();
        Prop prop;
        // The player's MyTDUHouse choice for this slot (houses_spec.md 11):
        // a DBFurniture bank of the slot's type replaces the authored one.
        // The choice is "<bank>[#<colour>]".
        std::string bankName = name;
        int32_t chosenColor = -1;
        auto choice = furnitureChoices.find( name );
        if ( choice != furnitureChoices.end() && !choice->second.empty() ) {
            bankName = choice->second;
            const size_t hash = bankName.find( '#' );
            if ( hash != std::string::npos ) {
                chosenColor = atoi( bankName.c_str() + hash + 1 );
                bankName.resize( hash );
            }
            if ( bankName.empty() ) {
                bankName = name;
            }
        }
        if ( !loadFurnitureModel( bankName, prop ) && ( bankName == name || !loadFurnitureModel( name, prop ) ) ) {
            continue;
        }
        prop.Name = "FURNITURE " + name;
        prop.FurnitureSlot = name;
        prop.FurnitureType = static_cast<int32_t>( slot["Type"].asInt() );
        prop.FurnitureColor = chosenColor >= 0 ? chosenColor : static_cast<int32_t>( slot["DefaultColor"].asInt() );
        prop.FurnitureGroup = static_cast<int32_t>( slot["Group"].asInt() );
        if ( prop.FurnitureColor > 0 ) {
            prop.Model->setMyHouseColor( prop.FurnitureColor );
        }
        prop.EntityHashcode = slot["__Hc"].asUInt64();
        Vector3f position, scale;
        Quaternionf rotation;
        ReadPlacement( slot["orient_pos_scale"], position, rotation, scale );
        prop.Local = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( rotation ) *
                     Matrix4x4f::Scale( scale );
        if ( bLogProps ) {
            OTDU_LOG_INFO( "IntroCinematic: furniture '%s' (type %d, group %d, colour %d, bank %s) at (%.2f, %.2f, %.2f)\n",
                           name.c_str(), prop.FurnitureType, static_cast<int32_t>( slot["Group"].asInt() ),
                           prop.FurnitureColor, prop.FurnitureBank.c_str(), position.x, position.y, position.z );
        }
        props.push_back( std::move( prop ) );
        placed++;
    }
    if ( placed > 0 ) {
        OTDU_LOG_INFO( "IntroCinematic: %zu furniture slot(s) placed\n", placed );
    }
}

std::vector<IntroCinematic::FurnitureSlotInfo> IntroCinematic::getFurnitureSlots() const {
    std::vector<FurnitureSlotInfo> slots;
    for ( const Prop &prop : props ) {
        if ( prop.FurnitureSlot.empty() || !prop.Model ) {
            continue;
        }
        FurnitureSlotInfo info;
        info.Slot = prop.FurnitureSlot;
        info.Bank = prop.FurnitureBank;
        info.Type = prop.FurnitureType;
        info.Group = prop.FurnitureGroup;
        info.Color = prop.FurnitureColor;
        info.NumColors = prop.Model->countMyHouseColors();
        info.ModelToScene = prop.Local * prop.RootInverse;
        info.BoundsMin = prop.Model->getBoundsMin();
        info.BoundsMax = prop.Model->getBoundsMax();
        slots.push_back( info );
    }
    return slots;
}

int32_t IntroCinematic::applyRoomMaterial( const char *pPrefix, const std::string &bankName ) {
    const std::string prefix = ToLower( pPrefix );
    const EdenMaterial *pSource = nullptr;
    if ( !bankName.empty() ) {
        // The finish bank: a quad whose material is the finish (Ground_09
        // "PARKET", Wall_nn) [DATA]; the model stays alive with the scene (its
        // textures, CLAUDE.md trap 13).
        Prop loaded;
        if ( !loadFurnitureModel( bankName, loaded ) ) {
            return -1;
        }
        for ( const EdenMaterial &material : loaded.Model->getMaterials() ) {
            const std::string name = ToLower( material.Name );
            if ( name.find( "patch" ) == std::string::npos ) {
                pSource = &material;
                break;
            }
        }
        retiredFurniture.push_back( std::move( loaded.Model ) );
        if ( pSource == nullptr ) {
            return -1;
        }
    }
    int32_t changed = 0;
    for ( size_t s = 0; s < sets.size(); s++ ) {
        if ( s < setIsExterior.size() && setIsExterior[s] ) {
            continue;
        }
        std::vector<EdenMaterial> &materials = sets[s]->getMaterials();
        for ( size_t m = 0; m < materials.size(); m++ ) {
            const auto key = std::make_pair( s, m );
            auto original = roomMaterialOriginals.find( key );
            const std::string name = ToLower( original != roomMaterialOriginals.end() ? original->second.Name : materials[m].Name );
            // "<prefix><digit>_<digits>": Mat_Floor1_09, MAT_Wall1_28, MAT_Floor2_23
            // - the naming of all 21 living banks [DATA, houses_spec 13.1];
            // MAT_FLOOR_03 (lph_l1bi) is another, unpaintable surface.
            if ( name.compare( 0, prefix.size(), prefix ) != 0 || name.size() < prefix.size() + 3 ||
                 !isdigit( static_cast<unsigned char>( name[prefix.size()] ) ) || name[prefix.size() + 1] != '_' ||
                 !isdigit( static_cast<unsigned char>( name[prefix.size() + 2] ) ) ) {
                continue;
            }
            if ( original == roomMaterialOriginals.end() ) {
                original = roomMaterialOriginals.emplace( key, materials[m] ).first;
            }
            if ( pSource != nullptr ) {
                // The finish's textures into the room material's slots of the
                // same type (the room keeps its shader, parameters and its own
                // lightmap - the finish's lightmap slot is its FAKE_LM white
                // texture, which drew the floor flat white). The slot type
                // names are packed: the lightmap one reads "...LIG" [DATA].
                auto slotName = []( uint64_t type ) {
                    std::string text;
                    for ( int32_t b = 0; b < 8; b++ ) {
                        const char c = static_cast<char>( ( type >> ( 8 * b ) ) & 0xff );
                        if ( c != 0 ) {
                            text += c;
                        }
                    }
                    return ToUpper( text );
                };
                EdenMaterial updated = original->second;
                std::string copied;
                for ( uint32_t i = 0; i < 16; i++ ) {
                    if ( updated.SlotTypes[i] == 0 || slotName( updated.SlotTypes[i] ).find( "LIG" ) != std::string::npos ) {
                        continue;
                    }
                    for ( uint32_t j = 0; j < 16; j++ ) {
                        if ( pSource->SlotTypes[j] == updated.SlotTypes[i] && pSource->pTextures[j] != nullptr ) {
                            updated.pTextures[i] = pSource->pTextures[j];
                            updated.TextureHashes[i] = pSource->TextureHashes[j];
                            copied += slotName( updated.SlotTypes[i] ) + " ";
                            break;
                        }
                    }
                }
                if ( CmdLine::HasArg( "-logfurniture" ) ) {
                    std::string roomSlots, sourceSlots;
                    for ( uint32_t i = 0; i < 16; i++ ) {
                        if ( original->second.SlotTypes[i] != 0 ) {
                            roomSlots += slotName( original->second.SlotTypes[i] ) + " ";
                        }
                        if ( pSource->SlotTypes[i] != 0 ) {
                            sourceSlots += slotName( pSource->SlotTypes[i] ) + " ";
                        }
                    }
                    OTDU_LOG_INFO( "IntroCinematic: '%s' slots [%s] <- '%s' [%s]: copied %s\n", original->second.Name.c_str(),
                                   roomSlots.c_str(), pSource->Name.c_str(), sourceSlots.c_str(), copied.c_str() );
                }
                materials[m] = updated;
            } else {
                materials[m] = original->second;
            }
            changed++;
        }
    }
    OTDU_LOG_INFO( "IntroCinematic: room '%s*' -> %s: %d material(s)\n", prefix.c_str(),
                   bankName.empty() ? "(authored)" : bankName.c_str(), changed );
    return changed;
}

bool IntroCinematic::setFurnitureColor( const std::string &slotName, int32_t color ) {
    for ( Prop &prop : props ) {
        if ( prop.FurnitureSlot == slotName && prop.Model ) {
            if ( color < 0 || color >= prop.Model->countMyHouseColors() ) {
                return false;
            }
            prop.FurnitureColor = color;
            prop.Model->setMyHouseColor( color );
            return true;
        }
    }
    return false;
}

bool IntroCinematic::replaceFurniture( const std::string &slotName, const std::string &bankName ) {
    for ( Prop &prop : props ) {
        if ( prop.FurnitureSlot != slotName ) {
            continue;
        }
        if ( prop.FurnitureBank == ToLower( bankName ) ) {
            return true;
        }
        Prop loaded;
        if ( !loadFurnitureModel( bankName, loaded ) ) {
            return false;
        }
        // The old model may still be referenced by a frame in flight: it is
        // retired, not destroyed, until the scene unloads (CLAUDE.md trap 13).
        retiredFurniture.push_back( std::move( prop.Model ) );
        prop.Model = std::move( loaded.Model );
        // The colour stays (clamped to what the new model has).
        const int32_t colors = prop.Model->countMyHouseColors();
        if ( prop.FurnitureColor >= colors ) {
            prop.FurnitureColor = 0;
        }
        prop.Model->setMyHouseColor( prop.FurnitureColor );
        prop.RootInverse = loaded.RootInverse;
        prop.FurnitureBank = loaded.FurnitureBank;
        OTDU_LOG_INFO( "IntroCinematic: furniture slot '%s' -> %s\n", slotName.c_str(), prop.FurnitureBank.c_str() );
        return true;
    }
    return false;
}

void IntroCinematic::loadSets( const XmbDocument &entities ) {
    // GSReflection on/off per game mode (table 0xf817e0); the actor layers
    // (avatars in the mirror layers) on [INF: the intro-mode check is
    // encrypted; the original layer setup includes them].
    view.setPlanarReflectionsEnabled( EdenPlanarReflections::IsEnabledForGameMode( setup.GameModeId ) );
    // Actor mirror layers (REFLECT FOR ACTORS): the avatars seen by the mirror
    // camera of a projector (the deck artwork mirror). -noactorreflect: off.
    view.setPlanarReflectionActors( !CmdLine::HasArg( "-noactorreflect" ) );
    // Swimmers in the pool refraction (the REFRACT projector's REFLECT 2D
    // layer): the original only adds the actors there for C_JACCUZZI / C_A4
    // (mgr+0x24), so in C_INTRO the water replaces every submerged body part
    // with the basin and the legs look cut off at the surface. OpenTDU2 keeps
    // them; -norefractactors restores the original look.
    view.setRefractionActors( !CmdLine::HasArg( "-norefractactors" ) );
    loadBankModels( introBank, introHashcodes, entities, false );
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    // Entity__PosCam / Entity__CinCar placements (car rest pose).
    std::unordered_map<uint64_t, std::string> posCamNames;
    std::unordered_map<std::string, uint64_t> posCamCameraHashcodes; // Entity__PosCam Camera
    vehiclePositions.clear();
    for ( const char *pArray :
          { "ArrayOf_Entity__PosCam", "ArrayOf_Entity__CinCar", "ArrayOf_Entity__Actor__ActorVehiclePosition" } ) {
        for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
            XmbValue entry = entityExport.getArrayElement( pArray, i );
            Vector3f position, scale;
            Quaternionf rotation;
            ReadPlacement( entry["orient_pos_scale"], position, rotation, scale );
            entityPlacements[entry["__Name"].asString()] = std::make_pair( position, rotation );
            if ( strcmp( pArray, "ArrayOf_Entity__Actor__ActorVehiclePosition" ) == 0 ) {
                // The casino hall's car on its socle (icashal.bnk: VehicleId,
                // PaintId, RimIndice, InteriorIndice, NbTurnByMinute) [DATA].
                VehiclePosition vehicle;
                vehicle.Name = entry["__Name"].asString();
                vehicle.VehicleId = static_cast<int32_t>( entry["VehicleId"].asInt() );
                vehicle.PaintId = static_cast<int32_t>( entry["PaintId"].asInt() );
                vehicle.RimIndex = static_cast<int32_t>( entry["RimIndice"].asInt() );
                vehicle.InteriorIndex = static_cast<int32_t>( entry["InteriorIndice"].asInt() );
                vehicle.TurnsPerMinute = entry["NbTurnByMinute"].asFloat();
                vehicle.Position = position;
                OTDU_LOG_INFO( "IntroCinematic: vehicle position '%s' car %d paint %d rims %d interior %d, %.2f turns/min at "
                               "(%.2f, %.2f, %.2f)\n",
                               vehicle.Name.c_str(), vehicle.VehicleId, vehicle.PaintId, vehicle.RimIndex,
                               vehicle.InteriorIndex, vehicle.TurnsPerMinute, position.x, position.y, position.z );
                vehiclePositions.push_back( vehicle );
                continue;
            }
            if ( strcmp( pArray, "ArrayOf_Entity__CinCar" ) == 0 ) {
                cinCarHashcodes[entry["__Name"].asString()] = entry["__Hc"].asUInt64();
            } else {
                posCamNames[entry["__Hc"].asUInt64()] = entry["__Name"].asString();
                posCamCameraHashcodes[entry["__Name"].asString()] = entry["Camera"].asUInt64();
            }
        }
    }
    // Entity__ShopData { MainCamera, PosCams[] { PosCam } }: the car places of
    // a dealership / a garage, in order (tess_garage_spec.md: the garage's
    // PosCams[0] = D_CAR) [DATA].
    shopPosCams.clear();
    if ( entityExport.getArraySize( "ArrayOf_Entity__ShopData" ) > 0 ) {
        XmbValue shopData = entityExport.getArrayElement( "ArrayOf_Entity__ShopData", 0 );
        for ( uint32_t i = 0; i < shopData.getArraySize( "PosCams" ); i++ ) {
            auto it = posCamNames.find( shopData.getArrayElement( "PosCams", i )["PosCam"].asUInt64() );
            shopPosCams.push_back( it != posCamNames.end() ? it->second : std::string() );
        }
    }
    // Entity__Camera (cam_entree_0N: walk mode start poses).
    std::unordered_map<uint64_t, std::string> cameraNames;
    // Entity__Camera__CameraCasino: the casino machines' own game cameras
    // (CasinoGame__SlotMachine CameraGame / CameraLines), with their Fov [DATA].
    entityCameraFovs.clear();
    entityCameraHashcodes.clear();
    entityCamerasByHash.clear();
    for ( const char *pArray : { "ArrayOf_Entity__Camera", "ArrayOf_Entity__Camera__CameraCasino" } ) {
        for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
            XmbValue entry = entityExport.getArrayElement( pArray, i );
            Vector3f position, scale;
            Quaternionf rotation;
            ReadPlacement( entry["orient_pos_scale"], position, rotation, scale );
            const std::string name = entry["__Name"].asString();
            entityCameras[name] = std::make_pair( position, rotation );
            cameraNames[entry["__Hc"].asUInt64()] = name;
            entityCameraHashcodes[entry["__Hc"].asUInt64()] = name;
            HashCamera byHash;
            byHash.Position = position;
            byHash.Rotation = rotation;
            if ( strcmp( pArray, "ArrayOf_Entity__Camera__CameraCasino" ) == 0 ) {
                const float fov = entry["Fov"].asFloat();
                if ( fov > 0.01f ) {
                    entityCameraFovs[name] = fov;
                    byHash.Fov = fov;
                }
            }
            entityCamerasByHash[entry["__Hc"].asUInt64()] = byHash;
        }
    }
    // Entity__PosCam Camera: the shot that frames the car standing there (the
    // dealer's car presentation camera) [DATA].
    posCamCameras.clear();
    for ( const auto &posCam : posCamCameraHashcodes ) {
        auto it = cameraNames.find( posCam.second );
        if ( it != cameraNames.end() ) {
            posCamCameras[posCam.first] = it->second;
        }
    }
    // Entity__FirstPerson { StartCamera = Entity__Camera __Hc }: the walk
    // mode start pose of a spot interior (tess_garage_spec.md, the garage's
    // cam_walkmode; pool_interior_spec.md 4.4, the living's cam_entree) [DATA].
    firstPersonCamera.clear();
    if ( entityExport.getArraySize( "ArrayOf_Entity__FirstPerson" ) > 0 ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__FirstPerson", 0 );
        auto it = cameraNames.find( entry["StartCamera"].asUInt64() );
        if ( it != cameraNames.end() ) {
            firstPersonCamera = it->second;
        }
    }
    // House exterior (spot bank), same frame; its interior LODs are hidden
    // (the intro bank carries the interior).
    const std::string exteriorPath = gpFile->getBankFolder() + setup.ExteriorBank;
    if ( !setup.ExteriorBank.empty() && exteriorBank.load( exteriorPath.c_str() ) ) {
        gpRender->getTextureCache().loadBankTextures( exteriorBank, exteriorBank.getPath().c_str() );
        BankHashcodes exteriorHashcodes;
        exteriorHashcodes.load( exteriorBank );
        const BankEntry *pExteriorEntities = exteriorBank.findEntry( "entities.xmb" );
        XmbDocument exteriorEntities;
        if ( pExteriorEntities != nullptr && exteriorEntities.load( pExteriorEntities->pData, pExteriorEntities->Size ) ) {
            loadBankModels( exteriorBank, exteriorHashcodes, exteriorEntities, true );
        }
    }
    // The interior bank of the spot (CINE kind 3: the Solar Crown TV set
    // room ich_ib): its models drawn in the spot frame, from interiorSetStart
    // on (drawSets).
    const std::string interiorPath = gpFile->getBankFolder() + setup.InteriorBank;
    if ( !setup.InteriorBank.empty() ) {
        if ( interiorBank.load( interiorPath.c_str() ) ) {
            gpRender->getTextureCache().loadBankTextures( interiorBank, interiorBank.getPath().c_str() );
            BankHashcodes interiorHashcodes;
            interiorHashcodes.load( interiorBank );
            const BankEntry *pInteriorEntities = interiorBank.findEntry( "entities.xmb" );
            XmbDocument interiorEntities;
            if ( pInteriorEntities != nullptr &&
                 interiorEntities.load( pInteriorEntities->pData, pInteriorEntities->Size ) ) {
                interiorSetStart = sets.size();
                loadBankModels( interiorBank, interiorHashcodes, interiorEntities, false );
                OTDU_LOG_INFO( "IntroCinematic: interior %s: %zu models\n", setup.InteriorBank.c_str(),
                               sets.size() - interiorSetStart );
            }
        } else {
            OTDU_LOG_WARN( "IntroCinematic: interior '%s' not found\n", interiorPath.c_str() );
        }
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu set models, %zu props, %zu cameras\n", sets.size(), props.size(),
                   entityCameras.size() );
}

void IntroCinematic::applyShadowConfig( const std::string &name ) {
    // -shadowconfig <name>: one config for every shot (A/B of the shot configs).
    const char *pForced = CmdLine::GetArgValue( "-shadowconfig" );
    view.setShadowConfig( EdenShadows::FindConfig( pForced != nullptr ? pForced : name ) );
    // The shots carry the original's "shad:<config>" messages: log them (a
    // CINE_VERY_NEAR shot has a 1.4 m shadow range, i.e. no visible shadow).
    OTDU_LOG_INFO( "IntroCinematic: shadow config '%s'\n", name.c_str() );
}

void IntroCinematic::loadPnjs( const XmbDocument &entities ) {
    if ( CmdLine::HasArg( "-nopnj" ) || !avatarLibrary.load() ) {
        return;
    }
    // The PNJ animated by a scene of one of the cinematics.
    std::unordered_set<uint64_t> used;
    for ( const auto &pDef : definitions ) {
        for ( const CinematicScene &scene : pDef->Scenes ) {
            for ( const CinematicScene::Object &object : scene.getObjects() ) {
                used.insert( object.EntityHashcode );
            }
        }
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__PNJ" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__PNJ", i );
        Pnj pnj;
        pnj.EntityHashcode = entry["__Hc"].asUInt64();
        pnj.Name = entry["__Name"].asString();
        if ( used.count( pnj.EntityHashcode ) == 0 ) {
            continue;
        }
        pnj.Avatar = std::make_unique<EdenAvatar>();
        if ( pnj.Avatar->load( avatarLibrary, entry["ConfigName"].asString().c_str() ) ) {
            pnjs.push_back( std::move( pnj ) );
        }
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu PNJ avatars\n", pnjs.size() );
}

bool IntroCinematic::setPlayerAvatar( const std::string &configId ) {
    // FUN_00da8d90: the player avatar rebuilt from the DBPNJ configuration.
    player = Pnj();
    player.Name = "Avatar";
    player.Avatar = std::make_unique<EdenAvatar>();
    if ( !avatarLibrary.isLoaded() && !avatarLibrary.load() ) {
        player.Avatar.reset();
        return false;
    }
    if ( !player.Avatar->load( avatarLibrary, configId.c_str() ) ) {
        OTDU_LOG_WARN( "IntroCinematic: player avatar '%s' not found\n", configId.c_str() );
        player.Avatar.reset();
        return false;
    }
    OTDU_LOG_INFO( "IntroCinematic: player avatar %s\n", configId.c_str() );
    return true;
}

const EdenAnimation *IntroCinematic::getAnimation( uint64_t hashcode ) {
    auto it = animations.find( hashcode );
    if ( it != animations.end() ) {
        return it->second.get();
    }
    std::unique_ptr<EdenAnimation> pAnimation;
    if ( const BankEntry *pEntry = introHashcodes.find( hashcode, 0, 8 ) ) {
        pAnimation = std::make_unique<EdenAnimation>();
        if ( !pAnimation->parse( pEntry->pData, pEntry->Size ) ) {
            pAnimation.reset();
        }
    }
    const EdenAnimation *pResult = pAnimation.get();
    animations[hashcode] = std::move( pAnimation );
    return pResult;
}

bool IntroCinematic::sampleAnimationTrack( const EdenSkeleton &skeleton, bool bWoman, const Playing &playing,
                                          const CinematicScene &scene, const CinematicScene::Object &object, float sceneTime,
                                          std::vector<BoneTransform> &locals ) {
    const auto &styleAnims = playing.pDef->StyleAnims[bWoman ? 1 : 0];
    const CinematicScene::Track *pTrack = object.findTrack( kTrackAnimation );
    if ( pTrack == nullptr || pTrack->NumComponents < 10 || pTrack->Times.empty() ) {
        return false;
    }
    {
        auto keyField = [pTrack]( size_t key, uint32_t field ) {
            return pTrack->Values[key * pTrack->NumComponents + field];
        };
        auto keyRef = [&]( size_t key ) {
            uint32_t index = 0;
            const float value = keyField( key, 0 );
            memcpy( &index, &value, sizeof( index ) );
            return index;
        };
        auto samplePose = [&]( size_t key, std::vector<BoneTransform> &pose ) {
            const uint32_t ref = keyRef( key );
            if ( ref >= scene.getReferences().size() ) {
                return false;
            }
            uint64_t hashcode = scene.getReferences()[ref].Hashcode;
            auto style = styleAnims.find( hashcode );
            if ( style != styleAnims.end() ) {
                hashcode = style->second;
            }
            const EdenAnimation *pAnimation = getAnimation( hashcode );
            if ( pAnimation == nullptr ) {
                return false;
            }
            const float clipTime = keyField( key, 2 ) - keyField( key, 7 ) + ( sceneTime - pTrack->Times[key] );
            pAnimation->sample( skeleton, clipTime, true, pose );
            return true;
        };
        size_t key = 0;
        for ( size_t k = 0; k < pTrack->Times.size(); k++ ) {
            if ( pTrack->Times[k] <= sceneTime ) {
                key = k;
            }
        }
        std::vector<BoneTransform> current = locals;
        samplePose( key, current );
        const float blendIn = keyField( key, 7 );
        const float sinceKey = sceneTime - pTrack->Times[key];
        if ( key > 0 && blendIn > 0.0f && sinceKey < blendIn ) {
            std::vector<BoneTransform> previous = locals;
            if ( samplePose( key - 1, previous ) ) {
                BlendPoses( previous, current, Clamp( sinceKey / blendIn, 0.0f, 1.0f ), locals );
            } else {
                locals = current;
            }
        } else {
            locals = current;
        }
    }
    return true;
}

void IntroCinematic::updateCarDoors() {
    // The door object of the scene car (garage_car_spec.md 2.2) [CODE]: the
    // cinematic messages opdl / opdr / opda (open left / right / both) and
    // cldl / cldr / clda with the duration as argument (FUN_00dc5a40 ->
    // FUN_00a48770), opwl / clwl ... for the windows. Tick 0x00a49400: f =
    // elapsed / duration (1 - f when closing), angle = -max * ease(f) for the
    // left door, + for the right (FUN_00a484c0); ease FUN_00a48310. The state
    // is derived from the scene time each frame (the last message of each
    // side) [INF: same result as the stateful object without reversals, and
    // correct after a seek]. The car object carries several Message tracks.
    if ( !car.isLoaded() ) {
        return;
    }
    if ( carDoorMaxAngle < 0.0f ) {
        carDoorMaxAngle = 1.0f;
        const int32_t idCar = car.getIdCar();
        if ( gpDatabase != nullptr && idCar >= 0 ) {
            XmbValue root = gpDatabase->getDocument().getRoot();
            for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
                XmbValue record = root.getArrayElement( "DBCarPhysicsData", i );
                if ( record["_id_car"].asInt( -1 ) == idCar ) {
                    carDoorMaxAngle = record["_doormaxangle"].asFloat( 1.0f );
                    break;
                }
            }
        }
    }
    for ( const Playing *pPlaying : playingOrder() ) {
        const CinematicScene *pScene = nullptr;
        float sceneTime = 0.0f;
        // The CinCar entity's object, else the scene's PLAYER_CAR (meeting_tess
        // has no CinCar entity: the car rests on D_CAR) [DATA].
        const CinematicScene::Object *pObject =
            carEntityHashcode != 0 ? findActor( *pPlaying, carEntityHashcode, nullptr, pScene, sceneTime ) : nullptr;
        if ( pObject == nullptr ) {
            pObject = findActor( *pPlaying, 0, "PLAYER_CAR", pScene, sceneTime );
        }
        if ( pObject == nullptr ) {
            static const bool bLogMissing = CmdLine::HasArg( "-logdoors" );
            static float lastMissing = -10.0f;
            if ( bLogMissing && std::fabs( pPlaying->Time - lastMissing ) >= 1.0f && pPlaying->pDef != nullptr ) {
                lastMissing = pPlaying->Time;
                const Cinematic::Shot *pShot = pPlaying->pDef->Cin.getShot( pPlaying->Time );
                OTDU_LOG_INFO( "CarDoors: no car object, '%s' t %.2f shot %s scene %u of %zu\n", pPlaying->pDef->Name.c_str(), pPlaying->Time,
                               pShot != nullptr ? "ok" : "none", pShot != nullptr ? pShot->Scene : 0u, pPlaying->pDef->Scenes.size() );
            }
            continue;
        }
        struct DoorKey {
            float Time = -1.0f;
            float Duration = 0.0f;
            bool bOpen = false;
        } doorKeys[2];
        float windowTime = -1.0f;
        bool bWindowsOpen = car.areWindowsOpen();
        for ( const CinematicScene::Track &track : pObject->Tracks ) {
            if ( track.Id != CinematicScene::kTrackMessage || track.NumComponents < 1 ) {
                continue;
            }
            for ( size_t k = 0; k < track.Times.size(); k++ ) {
                if ( track.Times[k] > sceneTime ) {
                    break;
                }
                uint32_t ref = 0;
                memcpy( &ref, &track.Values[k * track.NumComponents], sizeof( ref ) );
                if ( ref >= pScene->getReferences().size() ) {
                    continue;
                }
                const std::string &message = pScene->getReferences()[ref].Name;
                if ( message.size() < 4 ) {
                    continue;
                }
                const std::string verb = message.substr( 0, 3 );
                const char side = message[3];
                const float argument = message.size() > 5 ? static_cast<float>( atof( message.c_str() + 5 ) ) : 0.0f;
                if ( verb == "opd" || verb == "cld" ) {
                    for ( int32_t door = 0; door < 2; door++ ) {
                        if ( side == 'a' || side == ( door == 0 ? 'l' : 'r' ) ) {
                            if ( track.Times[k] >= doorKeys[door].Time ) {
                                doorKeys[door].Time = track.Times[k];
                                doorKeys[door].Duration = argument;
                                doorKeys[door].bOpen = verb == "opd";
                            }
                        }
                    }
                } else if ( ( verb == "opw" || verb == "clw" ) && track.Times[k] >= windowTime ) {
                    windowTime = track.Times[k];
                    bWindowsOpen = verb == "opw";
                }
            }
        }
        for ( int32_t door = 0; door < 2; door++ ) {
            const DoorKey &key = doorKeys[door];
            float angle = 0.0f;
            if ( key.Time >= 0.0f ) {
                float f = key.Duration > 0.0f ? Clamp( ( sceneTime - key.Time ) / key.Duration, 0.0f, 1.0f ) : 1.0f;
                if ( !key.bOpen ) {
                    f = 1.0f - f;
                }
                angle = carDoorMaxAngle * ( f < 0.1f ? ( key.bOpen ? f * 0.5f : f * 2.0f )
                                                     : ( key.bOpen ? f * 1.0555556f - 0.0555556f : f * 0.8888889f + 0.1111111f ) );
                angle = door == 0 ? -angle : angle;
            }
            if ( angle != car.getDoorAngle( door ) ) {
                car.setDoorAngle( door, angle );
            }
        }
        static const bool bLogDoors = CmdLine::HasArg( "-logdoors" );
        static float lastLog = -10.0f;
        if ( bLogDoors && std::fabs( sceneTime - lastLog ) >= 0.5f ) {
            lastLog = sceneTime;
            OTDU_LOG_INFO( "CarDoors: scene %.2f left %.3f right %.3f windows %d\n", sceneTime, car.getDoorAngle( 0 ), car.getDoorAngle( 1 ),
                           car.areWindowsOpen() ? 1 : 0 );
        }
        if ( windowTime >= 0.0f && bWindowsOpen != car.areWindowsOpen() ) {
            car.setWindowsOpen( bWindowsOpen );
        }
        return;
    }
}

namespace {
// The Visibility track of a .sce actor at a scene time: the last key at or
// before it (byte keys, 1 = shown). Only a track of more than one key is
// applied: Tess, the avatar and the car carry a lone 0 key and are on screen
// in retail [INF]; a track with real keys is authored on purpose [DATA].
bool SampleVisibility( const CinematicScene::Object &object, float sceneTime ) {
    static const bool bNoVisibility = CmdLine::HasArg( "-nocinevisibility" ); // A/B: everything shown
    const CinematicScene::Track *pVisibility = object.findTrack( kTrackVisibility );
    if ( bNoVisibility || pVisibility == nullptr || pVisibility->Times.size() <= 1 || pVisibility->NumComponents != 1 ) {
        return true;
    }
    float value = pVisibility->Values[0];
    for ( size_t k = 0; k < pVisibility->Times.size(); k++ ) {
        if ( pVisibility->Times[k] <= sceneTime ) {
            value = pVisibility->Values[k];
        }
    }
    return value != 0.0f;
}
} // namespace

void IntroCinematic::updateHandHeld( Prop &prop ) {
    static const bool bNoGrab = CmdLine::HasArg( "-nokeygrab" );
    const void *pCinematic = main.pDef;
    if ( prop.pHeldCinematic != pCinematic ) {
        prop.bHandHeld = false; // a new cutscene: back on its track
        prop.pHeldCinematic = pCinematic;
    }
    if ( bNoGrab || prop.Name != "CF_KCar" || !player.Avatar || !player.bVisible || player.WorldBones.empty() ||
         !prop.bVisible || pCinematic == nullptr ) {
        return;
    }
    const int32_t hand = player.Avatar->getSkeleton().findBone( EdenNameHash4( "Hand_R" ) );
    if ( hand < 0 || hand >= static_cast<int32_t>( player.WorldBones.size() ) ) {
        return;
    }
    const Matrix4x4f handWorld = player.World * player.WorldBones[hand];
    if ( !prop.bHandHeld ) {
        // Where the ring rests now (its track, as drawSets places it).
        const CinematicScene *pScene = nullptr;
        float sceneTime = 0.0f;
        const CinematicScene::Object *pObject = findActor( main, prop.EntityHashcode, nullptr, pScene, sceneTime );
        if ( pObject == nullptr || !pObject->findTrack( CinematicScene::kTrackPosition ) ) {
            return;
        }
        const Matrix4x4f ringWorld = sceneOrigin * pScene->getOrigin() *
                                     Matrix4x4f::Translation( pObject->samplePosition( sceneTime ) ) *
                                     EdenRotationFromQuaternion( pObject->sampleRotation( sceneTime ) );
        // Only after the drop (the track has stopped moving) and within a
        // hand's reach of it.
        const Vector3f now = pObject->samplePosition( sceneTime );
        const Vector3f before = pObject->samplePosition( Max( 0.0f, sceneTime - 0.2f ) );
        const float reach = ( handWorld.getTranslation() - ringWorld.getTranslation() ).length();
        if ( ( now - before ).length() > 1e-3f || reach > 0.12f ) {
            return;
        }
        prop.HandOffset = handWorld.inverse() * ringWorld;
        prop.bHandHeld = true;
        OTDU_LOG_INFO( "IntroCinematic: '%s' taken in the hand at t %.2f (%.2f m)\n", prop.Name.c_str(), sceneTime,
                       reach );
    }
    prop.HeldWorld = handWorld * prop.HandOffset;
}

void IntroCinematic::updateProps() {
    // Props: the .sce actor of the main / background cinematic. The animated
    // ones (own skeleton) are posed by their Animation track (the cf_* clips:
    // the key ring and the phone move with the hands, the clips are authored
    // in the actor's root frame like the avatars') and shown by the Visibility
    // track (meeting_tess: the phone 17.02..44.26 s, the keys from 14.10 s)
    // [DATA], with the multi-key rule of SampleVisibility (shared with the avatars).
    const bool bWoman = player.Avatar && player.Avatar->isWoman();
    for ( Prop &prop : props ) {
        prop.bAnimated = false;
        prop.bVisible = true;
        for ( const Playing *pPlaying : playingOrder() ) {
            const CinematicScene *pScene = nullptr;
            float sceneTime = 0.0f;
            const CinematicScene::Object *pObject = findActor( *pPlaying, prop.EntityHashcode, nullptr, pScene, sceneTime );
            if ( pObject == nullptr ) {
                continue;
            }
            prop.bVisible = SampleVisibility( *pObject, sceneTime );
            static const bool bNoPropAnim = CmdLine::HasArg( "-nopropanim" ); // A/B: static props as before
            if ( prop.Skeleton && !bNoPropAnim ) {
                std::vector<BoneTransform> locals( prop.Skeleton->getNumBones() );
                for ( uint32_t b = 0; b < prop.Skeleton->getNumBones(); b++ ) {
                    locals[b] = prop.Skeleton->getBindLocal( b );
                }
                // A clip that leaves every bone at rest (the casino key ring
                // of C_JACKPOT: kcar_cas_sm_winjackpot) is drawn on its track
                // as a static prop - the skinned draw showed nothing there.
                bool bPosed = sampleAnimationTrack( *prop.Skeleton, bWoman, *pPlaying, *pScene, *pObject, sceneTime, locals );
                if ( bPosed ) {
                    bool bMoved = false;
                    for ( uint32_t b = 0; b < prop.Skeleton->getNumBones() && !bMoved; b++ ) {
                        const BoneTransform &bind = prop.Skeleton->getBindLocal( b );
                        bMoved = ( locals[b].Translation - bind.Translation ).length() > 1e-4f ||
                                 std::abs( locals[b].Rotation.x - bind.Rotation.x ) + std::abs( locals[b].Rotation.y - bind.Rotation.y ) +
                                         std::abs( locals[b].Rotation.z - bind.Rotation.z ) +
                                         std::abs( locals[b].Rotation.w - bind.Rotation.w ) >
                                     1e-4f;
                    }
                    bPosed = bMoved;
                }
                if ( bPosed ) {
                    prop.World = sceneOrigin * pScene->getOrigin() * Matrix4x4f::Translation( pObject->samplePosition( sceneTime ) ) *
                                 EdenRotationFromQuaternion( pObject->sampleRotation( sceneTime ) );
                    prop.Skeleton->computeSkinMatrices( locals, prop.WorldBones, prop.Skin );
                    static const char *pLogPropAt = CmdLine::GetArgValue( "-logpropat" );
                    if ( pLogPropAt != nullptr && prop.Name == pLogPropAt && !prop.WorldBones.empty() ) {
                        static int32_t skinLogs = 0;
                        if ( ( skinLogs++ % 30 ) == 0 ) {
                            const Vector3f at = prop.World.getTranslation();
                            const Vector3f bone = prop.WorldBones.back().getTranslation();
                            OTDU_LOG_INFO( "IntroCinematic: prop '%s' skinned t %.2f at (%.2f, %.2f, %.2f), last bone "
                                           "(%.2f, %.2f, %.2f) of %zu\n",
                                           prop.Name.c_str(), sceneTime, at.x, at.y, at.z, bone.x, bone.y, bone.z,
                                           prop.WorldBones.size() );
                        }
                    }
                    computeAvatarLighting( prop.World.getTranslation(), prop.Lighting );
                    prop.bAnimated = true;
                }
            }
            break;
        }
        // The key ring's pick-up (the owner's account of retail, 2026-11-05:
        // "the key dropped and avatar picked it up"): once the ring has
        // dropped and the player's right hand comes within reach of it, it
        // stays in that hand until the cutscene ends [INF: the grab rule; the
        // retail clip that carries it is not applied here]. -nokeygrab = off.
        updateHandHeld( prop );
        // Not posed by a cinematic: a game-driven clip (setPropClip) [the
        // handle pull of a slot machine].
        if ( !prop.bAnimated && prop.pDrivenClip != nullptr && prop.Skeleton ) {
            std::vector<BoneTransform> locals( prop.Skeleton->getNumBones() );
            for ( uint32_t b = 0; b < prop.Skeleton->getNumBones(); b++ ) {
                locals[b] = prop.Skeleton->getBindLocal( b );
            }
            prop.pDrivenClip->sample( *prop.Skeleton, prop.DrivenTime, false, locals );
            prop.World = ( prop.bExterior ? spotOrigin : sceneOrigin ) * prop.Local;
            prop.Skeleton->computeSkinMatrices( locals, prop.WorldBones, prop.Skin );
            computeAvatarLighting( prop.World.getTranslation(), prop.Lighting );
            prop.bAnimated = true;
        }
    }
}

void IntroCinematic::animatePnj( Pnj &pnj, const Playing &playing, const CinematicScene &scene,
                                 const CinematicScene::Object &object, float sceneTime ) {
    // .sce actor: Position / Heading (actor root) and the Animation track
    // (avatar_spec 4.1): key k = last T_k <= t, clip time u = f2 - f7 +
    // (t - T_k), linear crossfade with the previous key during f7 seconds;
    // the clips loop. The clip reference is replaced by the style anim of
    // the avatar (additionalsStyleAnims "m" / "w").
    const EdenSkeleton &skeleton = pnj.Avatar->getSkeleton();
    std::vector<BoneTransform> locals( skeleton.getNumBones() );
    for ( uint32_t b = 0; b < skeleton.getNumBones(); b++ ) {
        locals[b] = skeleton.getBindLocal( b );
    }
    sampleAnimationTrack( skeleton, pnj.Avatar->isWoman(), playing, scene, object, sceneTime, locals );
    // ActorWorld = S * R(q*) * T(pos) * Origin.
    pnj.World = sceneOrigin * scene.getOrigin() * Matrix4x4f::Translation( object.samplePosition( sceneTime ) ) *
                EdenRotationFromQuaternion( object.sampleRotation( sceneTime ) );
    computeLookAt( pnj, scene, object, sceneTime, locals, pnj.World );
    applyFaceFx( pnj, scene, object, sceneTime, locals );
    skeleton.computeSkinMatrices( locals, pnj.WorldBones, pnj.Skin );
    // The multi-key Visibility track hides an avatar between shots too: the
    // player of C_SOLAR_CROWN_I is hidden 11.41..36.4 and 39.48..58 s of
    // its scene (the TV interview of Tess, the Todd dialogue), where it
    // otherwise stands in front of the TV cameras [DATA].
    pnj.bVisible = SampleVisibility( object, sceneTime );
    computeAvatarLighting( pnj.World.getTranslation(), pnj.Lighting );
}

bool IntroCinematic::setLookAtTarget( const std::string &pnjName, const Vector3f &worldTarget ) {
    // FUN_00b858f0: copies the target vec4 (a snapshot, not a pointer).
    for ( Pnj &pnj : pnjs ) {
        if ( pnj.Name == pnjName ) {
            pnj.LookTarget = worldTarget;
            pnj.bLookOn = true;
            return true;
        }
    }
    return false;
}

bool IntroCinematic::stopLookAt( const std::string &pnjName ) {
    // lkof (FUN_00b8dcd0): +0x1d68 = 1.
    for ( Pnj &pnj : pnjs ) {
        if ( pnj.Name == pnjName ) {
            pnj.bLookOn = false;
            return true;
        }
    }
    return false;
}

void IntroCinematic::computeLookAt( Pnj &pnj, const CinematicScene &scene, const CinematicScene::Object &object,
                                    float sceneTime, const std::vector<BoneTransform> &locals,
                                    const Matrix4x4f &world ) {
    // docs/re/facefx_spec.md 6. Two sources set the eye target (avatar
    // +0x1e30, flag +0x1e40) consumed once per frame by FUN_00b871a0:
    // - the actor property "LookAtTargetFaceFx" (FUN_00b854d0 -> +0x1c58;
    //   FUN_00b869e0: "-m" -> "-w" for a woman, the cinematic object of that
    //   name -> +0x210): target = that object's position, every frame;
    // - the actor message lkon (FUN_00b8dcd0 -> FUN_00b858f0(camera position
    //   DAT_01195b80, 1)) / lkof (+0x1d68 = 1): the head controller
    //   FUN_00b89730 passes the target on when dot(normalize(target - Head),
    //   actor forward) > 0.2 (+0x1e04, DAT_00ede428).
    pnj.bLookAt = false;
    if ( !faceFx.isLoaded() || !pnj.Avatar ) {
        return;
    }
    // lkon / lkof: the last key <= scene time; the target is captured when
    // the key is reached (FUN_00b858f0 copies the camera position once).
    if ( const CinematicScene::Track *pMessages = object.findTrack( CinematicScene::kTrackMessage ) ) {
        const void *pKey = nullptr;
        bool bOn = false;
        for ( size_t k = 0; k < pMessages->Times.size() && pMessages->NumComponents >= 1; k++ ) {
            if ( pMessages->Times[k] > sceneTime ) {
                break;
            }
            uint32_t ref = 0;
            memcpy( &ref, &pMessages->Values[k * pMessages->NumComponents], sizeof( ref ) );
            if ( ref >= scene.getReferences().size() ) {
                continue;
            }
            const std::string &message = scene.getReferences()[ref].Name;
            if ( message == "lkon" || message == "lkof" ) {
                pKey = &pMessages->Values[k * pMessages->NumComponents];
                bOn = message == "lkon";
            }
        }
        if ( pKey != pnj.pLookMessageKey ) {
            pnj.pLookMessageKey = pKey;
            if ( pKey != nullptr ) {
                pnj.bLookOn = bOn;
                if ( bOn ) {
                    pnj.LookTarget = camera.Position; // DAT_01195b80 [INF: view position]
                }
            }
        }
    } else {
        // No message track: the state set by an earlier lkon / lkof or by
        // setLookAtTarget stays (only FUN_00b858f0 and lkof write it).
        pnj.pLookMessageKey = nullptr;
    }

    const EdenSkeleton &skeleton = pnj.Avatar->getSkeleton();
    std::vector<Matrix4x4f> bones, skin;
    bool bPose = false;
    auto boneWorld = [&]( const char *pName, Matrix4x4f &outMatrix ) {
        const int32_t b = skeleton.findBone( EdenNameHash4( pName ) );
        if ( b < 0 ) {
            return false;
        }
        if ( !bPose ) {
            skeleton.computeSkinMatrices( locals, bones, skin );
            bPose = true;
        }
        outMatrix = world * bones[b];
        return true;
    };

    Vector3f target;
    bool bProperty = false;
    bool bTarget = false;
    if ( const CinematicScene::Property *pProperty = object.findProperty( "LookAtTargetFaceFx" ) ) {
        std::string name = pProperty->valueAt( sceneTime );
        const size_t male = name.find( "-m" );
        if ( pnj.Avatar->isWoman() && male != std::string::npos ) {
            name = name.substr( 0, male ) + "-w"; // writes "-w\0" (0xf0b5a8)
        }
        if ( const CinematicScene::Object *pTarget = name.empty() ? nullptr : scene.findObject( name.c_str() ) ) {
            target = ( sceneOrigin * scene.getOrigin() ).transformPoint( pTarget->samplePosition( sceneTime ) );
            bProperty = bTarget = true;
        }
    }
    Matrix4x4f head;
    if ( !bTarget && pnj.bLookOn && boneWorld( "Head", head ) ) {
        // FUN_00b89730: cone test against row 2 of the actor matrix.
        const Vector3f direction = ( pnj.LookTarget - head.getTranslation() ).normalized();
        const Vector3f forward = world.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        if ( direction.dot( forward ) > 0.2f ) {
            target = pnj.LookTarget;
            bTarget = true;
        }
    }
    Matrix4x4f eyeL, eyeR;
    if ( !bTarget || !boneWorld( "L_Eye_All", eyeL ) || !boneWorld( "R_Eye_All", eyeR ) ) {
        return;
    }
    // FUN_00b871a0: mid point of the eyes; up = world Y with a property
    // target, else the Y axis (row 1) of R_Eye_All; side = dot(axis, dir),
    // vertical = dot(up without its axis part, dir) (0x42c2a0 / 0x433510
    // [ENC]: cross product / normalisation [INF]).
    const Vector3f left = eyeL.getTranslation();
    const Vector3f right = eyeR.getTranslation();
    const Vector3f middle = ( left + right ) * 0.5f;
    Vector3f up = bProperty ? Vector3f( 0.0f, 1.0f, 0.0f )
                            : Vector3f( eyeR.m[1][0], eyeR.m[1][1], eyeR.m[1][2] ).normalized();
    const Vector3f axis = ( left - right ).normalized();
    const Vector3f forward = up.cross( left - right ).normalized();
    const Vector3f upright = axis.cross( forward ).normalized();
    const Vector3f direction = ( target - middle ).normalized();
    float side = axis.dot( direction );
    float vertical = upright.dot( direction );
    if ( !bProperty ) {
        // Head controller target: vertical clamped to [-0.2, 0.07]
        // (0.07 @0xedf1f4, -0.2 @0xf49c98 / 0xf7db64).
        if ( vertical > 0.07f ) {
            vertical = 0.07f;
        }
        if ( -0.2 > vertical ) {
            vertical = -0.2f;
        }
    }
    pnj.bLookAt = true;
    pnj.LookAt[0] = side;
    pnj.LookAt[1] = vertical;
    if ( CmdLine::HasArg( "-logfacefx" ) ) {
        OTDU_LOG_INFO( "FaceFX look-at: %s %s side %.3f vertical %.3f target (%.2f %.2f %.2f)\n", pnj.Name.c_str(),
                       bProperty ? "property" : "lkon", side, vertical, target.x, target.y, target.z );
    }
}

void IntroCinematic::applyFaceFx( Pnj &pnj, const CinematicScene &scene, const CinematicScene::Object &object,
                                  float sceneTime, std::vector<BoneTransform> &locals ) {
    // .sce "Morph" track: 5 dwords per key {u32 REFS index (0x0306
    // "<path>\female.fxa::<group>:<anim>"), u64 (hash), f32 clip length,
    // u32 0}. A key starts the FaceFX animation on the actor's animator
    // (vtable 0xeabc8c slot 6 FUN_00d96ab0 -> FUN_00b77a50 -> FUN_00b77600,
    // fade 0.2 s); FUN_00b77b20 ticks it with the frame time and writes the
    // 42 facial bones over the skeleton pose while it plays (its end:
    // FxAnim end time, then a 0.2 s fade, FUN_00480d10 [ENC]).
    // Here: stateless at the scene time (anim time = scene time - key time),
    // linear 0.2 s blends in / out [INF].
    if ( !faceFx.isLoaded() || !pnj.Avatar ) {
        return;
    }
    const FaceFxActor *pActor = faceFx.getActor( pnj.Avatar->isWoman() );
    const CinematicScene::Track *pTrack = object.findTrack( kTrackMorph );
    const EdenSkeleton &skeleton = pnj.Avatar->getSkeleton();
    // -say "<text>" / -saywav "<file>": speech the game never recorded. The
    // text path evaluates a synthesized FaceFX animation; the audio path feeds
    // the waveform's mouth shapes in as live registers (no animation at all),
    // which is the shape a microphone or a network voice stream produces.
    // Both loop on the scene time, so a still frame is reproducible, and the
    // look-at registers still apply.
    if ( ( bSay || bSayWav ) && pActor != nullptr ) {
        const float length = bSayWav ? Max( sayWav.getDuration(), 0.1f ) : Max( sayAnim.EndTime, 0.1f );
        float time = sceneTime - length * std::floor( sceneTime / length );
        if ( time < 0.0f ) {
            time += length;
        }
        FaceFxPose pose;
        SpeechAudio::Visemes visemes;
        if ( bSayWav ) {
            visemes = sayWav.sample( time );
            const FaceFxRegister registers[5] = { { "open", visemes.Open },
                                                  { "wide", visemes.Wide },
                                                  { "W", visemes.W },
                                                  { "ShCh", visemes.ShCh },
                                                  { "FV", visemes.FV } };
            FaceFxLibrary::Evaluate( *pActor, nullptr, 0.0f, pnj.Avatar->getFaceParams(),
                                     pnj.bLookAt ? pnj.LookAt : nullptr, registers, 5, pose );
        } else {
            FaceFxLibrary::Evaluate( *pActor, &sayAnim, time, pnj.Avatar->getFaceParams(),
                                     pnj.bLookAt ? pnj.LookAt : nullptr, pose );
        }
        for ( const auto &entry : pose.Bones ) {
            const int32_t bone = skeleton.findBone( entry.first );
            if ( bone >= 0 && static_cast<size_t>( bone ) < locals.size() ) {
                locals[bone] = entry.second;
            }
        }
        pnj.Avatar->setFacialCoefficients( pose.FacialCoef1, pose.FacialCoef2 );
        pnj.bFaceFxUsed = true;
        if ( CmdLine::HasArg( "-logfacefx" ) ) {
            static const uint32_t kLowerLip = EdenNameHash4( "L_LowerLip" );
            for ( const auto &entry : pose.Bones ) {
                if ( entry.first == kLowerLip ) {
                    OTDU_LOG_INFO( "FaceFX say: %s %s t=%.3f L_LowerLip q(%.4f %.4f %.4f %.4f) "
                                   "t(%.5f %.5f %.5f) mouth(open %.3f wide %.3f W %.3f ShCh %.3f FV %.3f)\n",
                                   pnj.Name.c_str(), bSayWav ? "audio" : sayAnim.Name.c_str(), time,
                                   entry.second.Rotation.x, entry.second.Rotation.y, entry.second.Rotation.z,
                                   entry.second.Rotation.w, entry.second.Translation.x,
                                   entry.second.Translation.y, entry.second.Translation.z, visemes.Open,
                                   visemes.Wide, visemes.W, visemes.ShCh, visemes.FV );
                }
            }
        }
        return;
    }
    auto animOf = [&]( size_t key ) -> const FaceFxAnim * {
        uint32_t ref = 0;
        const float bits = pTrack->Values[key * pTrack->NumComponents];
        memcpy( &ref, &bits, sizeof( ref ) );
        if ( ref >= scene.getReferences().size() || scene.getReferences()[ref].Type != kRefMorph ) {
            return nullptr;
        }
        return faceFx.findAnim( *pActor, scene.getReferences()[ref].Name );
    };
    // Weight of key k at the scene time (0 when not playing).
    auto weightOf = [&]( size_t key, const FaceFxAnim &anim, float &outAnimTime ) {
        const float animTime = sceneTime - pTrack->Times[key];
        outAnimTime = Min( animTime, anim.EndTime );
        if ( animTime < 0.0f ) {
            return 0.0f;
        }
        float weight = Min( animTime / kFaceFxFade, 1.0f );
        if ( animTime > anim.EndTime ) {
            weight = Min( weight, 1.0f - ( animTime - anim.EndTime ) / kFaceFxFade );
        }
        return Max( weight, 0.0f );
    };
    FaceFxPose pose, previousPose;
    float weight = 0.0f;
    const float *pLook = pnj.bLookAt ? pnj.LookAt : nullptr;
    if ( pActor != nullptr && pTrack != nullptr && pTrack->NumComponents >= 1 && !pTrack->Times.empty() ) {
        size_t key = 0;
        bool bFound = false;
        for ( size_t k = 0; k < pTrack->Times.size(); k++ ) {
            if ( pTrack->Times[k] <= sceneTime ) {
                key = k;
                bFound = true;
            }
        }
        const FaceFxAnim *pAnim = bFound ? animOf( key ) : nullptr;
        float animTime = 0.0f;
        if ( pAnim != nullptr && ( weight = weightOf( key, *pAnim, animTime ) ) > 0.0f ) {
            FaceFxLibrary::Evaluate( *pActor, pAnim, animTime, pnj.Avatar->getFaceParams(), pLook, pose );
            // Blend in from the previous animation while it still plays.
            const FaceFxAnim *pPrevious = key > 0 ? animOf( key - 1 ) : nullptr;
            float previousTime = 0.0f;
            const float previousWeight =
                weight < 1.0f && pPrevious != nullptr ? weightOf( key - 1, *pPrevious, previousTime ) : 0.0f;
            if ( previousWeight > 0.0f ) {
                FaceFxLibrary::Evaluate( *pActor, pPrevious, previousTime, pnj.Avatar->getFaceParams(), pLook,
                                         previousPose );
            }
            auto blend = [&]( const FaceFxPose &facePose, float w ) {
                for ( const auto &entry : facePose.Bones ) {
                    const int32_t bone = skeleton.findBone( entry.first );
                    if ( bone < 0 || static_cast<size_t>( bone ) >= locals.size() ) {
                        continue;
                    }
                    if ( w >= 1.0f ) {
                        locals[bone] = entry.second;
                    } else {
                        std::vector<BoneTransform> a( 1, locals[bone] ), b( 1, entry.second ), out;
                        BlendPoses( a, b, w, out );
                        locals[bone] = out[0];
                    }
                }
            };
            auto madd = []( const Vector4f &a, const Vector4f &b, float w ) {
                return Vector4f( a.x + b.x * w, a.y + b.y * w, a.z + b.z * w, a.w + b.w * w );
            };
            const Vector4f zero( 0.0f, 0.0f, 0.0f, 0.0f );
            Vector4f coef1 = madd( zero, pose.FacialCoef1, weight ), coef2 = madd( zero, pose.FacialCoef2, weight );
            if ( previousWeight > 0.0f ) {
                blend( previousPose, previousWeight );
                coef1 = madd( coef1, previousPose.FacialCoef1, previousWeight * ( 1.0f - weight ) );
                coef2 = madd( coef2, previousPose.FacialCoef2, previousWeight * ( 1.0f - weight ) );
            }
            blend( pose, weight );
            pnj.Avatar->setFacialCoefficients( coef1, coef2 );
            if ( CmdLine::HasArg( "-logfacefx" ) ) {
                static const uint32_t kLowerLip = EdenNameHash4( "L_LowerLip" );
                for ( const auto &entry : pose.Bones ) {
                    if ( entry.first == kLowerLip ) {
                        OTDU_LOG_INFO( "FaceFX: %s %s t=%.3f w=%.2f L_LowerLip q(%.4f %.4f %.4f %.4f) "
                                       "t(%.5f %.5f %.5f) coef1(%.3f %.3f %.3f) coef2(%.3f %.3f %.3f)\n",
                                       pnj.Name.c_str(), pAnim->Name.c_str(), animTime, weight,
                                       entry.second.Rotation.x, entry.second.Rotation.y, entry.second.Rotation.z,
                                       entry.second.Rotation.w, entry.second.Translation.x,
                                       entry.second.Translation.y, entry.second.Translation.z, coef1.x, coef1.y,
                                       coef1.z, coef2.x, coef2.y, coef2.z );
                    }
                }
            }
            pnj.bFaceFxUsed = true;
            return;
        }
    }
    if ( pActor != nullptr && pLook != nullptr ) {
        // No animation: the animator still ticks while the look-at runs
        // (FUN_00b75f50 sets +0x1ec0 / +0x1ec2): graph = registers only.
        FaceFxLibrary::Evaluate( *pActor, nullptr, 0.0f, pnj.Avatar->getFaceParams(), pLook, pose );
        for ( const auto &entry : pose.Bones ) {
            const int32_t bone = skeleton.findBone( entry.first );
            if ( bone >= 0 && static_cast<size_t>( bone ) < locals.size() ) {
                locals[bone] = entry.second;
            }
        }
        pnj.Avatar->setFacialCoefficients( pose.FacialCoef1, pose.FacialCoef2 );
        pnj.bFaceFxUsed = true;
        return;
    }
    if ( pnj.bFaceFxUsed ) {
        // FUN_00b77b20 fade branch: zero coefficients once FaceFX stopped.
        pnj.Avatar->setFacialCoefficients( Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ), Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) );
    }
}

void IntroCinematic::updatePnjs() {
    // Each avatar: the actor of the main cinematic, else of the background
    // one, sampled at the scene time of its shot.
    auto update = [this]( Pnj &pnj, const char *pName ) {
        pnj.bVisible = false;
        if ( !pnj.Avatar ) {
            return;
        }
        for ( const Playing *pPlaying : playingOrder() ) {
            const CinematicScene *pScene = nullptr;
            float sceneTime = 0.0f;
            const CinematicScene::Object *pObject = findActor( *pPlaying, pnj.EntityHashcode, pName, pScene, sceneTime );
            if ( pObject != nullptr ) {
                animatePnj( pnj, *pPlaying, *pScene, *pObject, sceneTime );
                return;
            }
        }
    };
    for ( Pnj &pnj : pnjs ) {
        update( pnj, nullptr );
    }
    if ( bPlayerSeated ) {
        updateSeatedPlayer();
    } else {
        update( player, "Avatar" );
        // The casino slot machines' cutscenes (icasslo1.bnk *_totem) name the
        // player "Avatar_" (entity 0x6666666666666666, like "Avatar") [DATA].
        if ( !player.bVisible ) {
            update( player, "Avatar_" );
        }
    }
}

void IntroCinematic::loadLights( const XmbDocument &entities ) {
    // Entity__Light (loader FUN_00def230, entry FUN_00a4c7a0 / FUN_00da07c0).
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    auto vec3 = []( XmbValue v ) { return Vector3f( v["X"].asFloat(), v["Y"].asFloat(), v["Z"].asFloat() ); };
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Light" ) && lights.size() < 64; i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Light", i );
        XmbValue placement = entry["orient_pos_scale"];
        const Quaternionf rotation( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                                    placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
        Light light;
        light.Position = sceneOrigin.transformPoint( vec3( placement["pos"] ) );
        // E+0x60 = normalize(-Y axis of the entity matrix).
        const Vector3f localDirection = rotation.rotate( Vector3f( 0.0f, -1.0f, 0.0f ) );
        light.Direction = sceneOrigin.transformVector( localDirection ).normalized();
        const float intensity = entry["Intensity"].asFloat( 1.0f );
        light.Ambient = vec3( entry["Ambient"] ) * intensity;
        light.Diffuse = vec3( entry["Diffuse"] ) * intensity;
        light.Specular = vec3( entry["Specular"] ) * intensity;
        const float drop = entry["DistanceDropOff"].asFloat();
        const float cut = entry["DistanceCutOff"].asFloat();
        // E+0x88 / E+0x8c = 2 x angle; cones use cos(angle / 2).
        const float cosOuter = std::cos( entry["AngularCutOff"].asFloat() );
        const float cosInner = std::cos( entry["AngularDropOff"].asFloat() );
        light.CosOuter = cosOuter;
        light.ConeScale = ( cosInner - cosOuter > 1.1920929e-7f ) ? 1.0f / ( cosInner - cosOuter ) : 8388608.0f;
        light.ConeBias = -cosOuter * light.ConeScale;
        // FUN_004bf410 is encrypted: linear fall-off from drop to cut [INF].
        if ( cut - drop > 1e-6f ) {
            light.DistanceA = cut / ( cut - drop );
            light.DistanceB = 1.0f / ( cut - drop );
        }
        light.bInterior = entry["isInterior"].asInt( 1 ) != 0;
        lights.push_back( light );
    }
    OTDU_LOG_INFO( "IntroCinematic: %zu lights\n", lights.size() );
}

void IntroCinematic::computeAvatarLighting( const Vector3f &rootPosition, EdenAvatarLighting &out ) {
    // FUN_00a4da90 (gathering, AVATAR_CHOICE / FIRST_WM: interior light
    // modes) -> FUN_00a4ca50 (camera light) -> FUN_00b87aa0 (useSun false in
    // these modes) -> FUN_00caf850 (CARINTEG / ATTEFACT, AVATAROUTCARLIGHTFACTOR).
    const WeatherPreset *pWeather = view.getWeather();
    // The shop cinematics carry no weather message, so their view runs
    // without a preset and the avatar light falls back to no spot gathering,
    // full sun shadow (CARINTEG.z = 1: the roof kills the sun term) and no
    // camera light - the near-black seller NPC in the lit room. Retail keeps
    // the gathering on in every interior mode (SHOP..PHOTOGRAPH..
    // AVATAR_CHOICE, avatar_lighting_spec.md 2.1) and the interior avatar
    // preset carries its parameters: fall back to weat:Avatar for the avatar
    // light only, the scene weather stays unset [DATA: spotlight influence
    // (0.5, 0), camera light 0.25, shadow influence 0.4].
    if ( pWeather == nullptr && setup.bInterior ) {
        pWeather = view.getWeathers().find( "Avatar" );
    }
    const EdenSceneParams &sceneParams = view.getSceneParams();
    auto getFloat = [pWeather]( const char *pName, float value ) {
        return pWeather != nullptr ? pWeather->getFloat( pName, value ) : value;
    };
    auto getVector3 = [pWeather]( const char *pName, const Vector3f &value ) {
        return pWeather != nullptr ? pWeather->getVector3( pName, value ) : value;
    };
    const float cameraFactor = getFloat( "avatar_cameralight_factor", 0.0f );
    const Vector4f influence = pWeather != nullptr
                                   ? pWeather->getVector4( "avatar_spotlight_interior_exterior_influence",
                                                           Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) )
                                   : Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    const float shadowInfluence = getFloat( "avatar_shadow_influence", 1.0f );
    const Vector3f sunDirection = sceneParams.SunDirection.normalized();
    // P: FUN_00b87100 (a skeleton point) is encrypted: root + (0, 1, 0) [INF].
    const Vector3f point = rootPosition + Vector3f( 0.0f, 1.0f, 0.0f );
    Vector3f direction = sunDirection;
    Vector3f ambient = sceneParams.Ambient;
    Vector3f diffuse = sceneParams.SunDiffuse;
    Vector3f specular = sceneParams.SunSpecular;
    if ( influence.x > 0.0f || influence.y > 0.0f || cameraFactor > 0.0f ) {
        // FUN_00a4beb0.
        float best = 1.17549435e-38f;
        float bestQ = 1.0f;
        int32_t bestIndex = -1;
        Vector3f accDirection, accAmbient, accDiffuse, accSpecular;
        for ( size_t i = 0; i < lights.size(); i++ ) {
            const Light &light = lights[i];
            const Vector3f d = point - light.Position;
            const float s = Clamp( d.dot( d ), 1.1920929e-7f, 100000.0f );
            const float lightInfluence = light.bInterior ? influence.x : influence.y;
            const float along = light.Direction.dot( d );
            if ( !( lightInfluence > 0.0f ) || !( along > 0.0f ) || along * along < light.CosOuter * light.CosOuter * s ) {
                continue;
            }
            const float length = std::sqrt( s );
            const Vector3f n = d * ( 1.0f / length );
            const float a = Clamp( light.DistanceA - light.DistanceB * length, 0.0f, 1.0f ) *
                            Clamp( light.ConeScale * light.Direction.dot( n ) + light.ConeBias, 0.0f, 1.0f ) *
                            lightInfluence;
            if ( !( a > 0.0f ) ) {
                continue;
            }
            const float q = s * s;
            accDirection = accDirection + n * ( a / q );
            accAmbient = accAmbient + light.Ambient * a;
            accDiffuse = accDiffuse + light.Diffuse * a;
            accSpecular = accSpecular + light.Specular * a;
            if ( a > best ) {
                best = a;
                bestQ = q;
                bestIndex = static_cast<int32_t>( i );
            }
        }
        float w = best;
        if ( bestIndex != -1 ) {
            const float t = 1.0f - ( best - 1.1920929e-7f );
            accDirection = accDirection + sunDirection * ( t / bestQ );
            ambient = accAmbient + sceneParams.Ambient * t;
            diffuse = accDiffuse + sceneParams.SunDiffuse * t;
            specular = accSpecular + sceneParams.SunSpecular * t;
            direction = accDirection.normalized();
        } else {
            w = 0.0f;
        }
        out.Shadow = shadowInfluence * ( 1.0f - w );
        // FUN_00a4ca50: the camera light (instance spot light), at the
        // anchor + 1e4 x camera forward (the surfaces facing the camera are
        // lit) [INF: camera axis FUN_0043dae0 is encrypted].
        const Vector3f anchor = rootPosition + Vector3f( 0.0f, 1.0f, 0.0f );
        if ( getFloat( "avatar_cameralight_desynchro", 0.0f ) < 0.5f ) {
            out.SpotPosition = anchor + cameraForward * 10000.0f;
        } else {
            out.SpotPosition =
                anchor - getVector3( "avatar_cameralight_direction", Vector3f( 0.0f, -1.0f, 0.0f ) ).normalized() * 10000.0f;
        }
        out.SpotAmbient = ambient * cameraFactor;
        out.SpotDiffuse = diffuse * cameraFactor;
        out.SpotSpecular = specular * cameraFactor;
    } else {
        out.Shadow = shadowInfluence;
        out.SpotAmbient = out.SpotDiffuse = out.SpotSpecular = Vector3f();
        out.SpotPosition = point;
    }
    // FUN_00b87aa0 (m = 1 out of a car).
    const float v = Clamp( 1.0f, getFloat( "avatar_lum_min", 0.0f ), getFloat( "avatar_lum_max", 1.0f ) );
    out.Ambient = ambient * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_ambiant_factor", 1.0f ) );
    out.Diffuse = diffuse * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_diffuse_factor", 1.0f ) );
    out.Specular = specular * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_specular_factor", 1.0f ) );
    out.Direction = direction;
    out.Environment =
        getFloat( "avatar_car_envmap_factor", 0.7f ) * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_car_night_reflection_factor", 0.0f ) );
    // -logavatarlight: the gathered avatar light once a second (the
    // -logcarlights cadence): whether an interior gathers its spots at all
    // (shadow 1 = the sun term is fully shadow-map-multiplied, i.e. dark
    // indoors; spot (0,0,0) = no camera light).
    static const bool bLogAvatarLight = CmdLine::HasArg( "-logavatarlight" );
    if ( bLogAvatarLight ) {
        static float logTimer = 0.0f;
        logTimer += lastDeltaTime;
        if ( logTimer >= 1.0f ) {
            logTimer = 0.0f;
            OTDU_LOG_INFO( "AvatarLight: at (%.1f %.1f %.1f) amb (%.2f %.2f %.2f) diff (%.2f %.2f %.2f) shadow %.2f "
                           "spot (%.2f %.2f %.2f) lights %zu weather %s\n",
                           rootPosition.x, rootPosition.y, rootPosition.z, out.Ambient.x, out.Ambient.y, out.Ambient.z,
                           out.Diffuse.x, out.Diffuse.y, out.Diffuse.z, out.Shadow, out.SpotAmbient.x, out.SpotAmbient.y,
                           out.SpotAmbient.z, lights.size(), pWeather != nullptr ? pWeather->Name.c_str() : "none" );
        }
    }
}
