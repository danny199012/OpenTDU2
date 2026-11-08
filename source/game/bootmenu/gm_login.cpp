#include "shared.h"
#include "gm_login.h"

#include <SDL3/SDL_scancode.h>

#include "database/gs_database.h"
#include "audio/gs_audio.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"
#include "game/player_profile.h"

#include "imgui.h"

namespace {
// Localized strings (db_<lang>.cpr line ids, DBLoc _flash variables).
constexpr int32_t kLocPressStart = 938;          // m_PressStart
constexpr int32_t kLocNATMessage = 7963;         // m_NAT_msg_
constexpr int32_t kLocNoProgressionData = 7097;  // savegame popup text (FUN_00ac4c80, status 1, load)
constexpr int32_t kLocOk = 219;                  // m_Ok
constexpr int32_t kLocRetry = 1839;              // m_Retry_min
constexpr int32_t kLocOnline = 296;              // "Online" (the green badge of the New profile screen)
constexpr int32_t kLocOffline = 965;             // "Offline" (the button / the badge in the offline state)
constexpr int32_t kLocNewProfile = 1394;         // mpc_NewProfile (the dialog title)
constexpr int32_t kLocPassword = 1396;           // mpc_Password
constexpr int32_t kLocNickname = 1397;           // mpc_Nickname
constexpr int32_t kLocQuit = 519;                // m_Quit
constexpr int32_t kLocAccountName = 7445;        // m_Username_ ("Atari.com account name")
constexpr int32_t kLocAvertAccount = 7446;       // m_AvertAtariAccount_ (the note under the fields)
constexpr const char *kGameName = "TDU2";        // DAT_00f18f8c (§GAME_NAME§)

// The field variable paths of the popup, in the focus cycle order (S250
// focuses them through /:TextToFocus; the edit texts bind the same paths).
// Indexed by GMLogin::eField.
constexpr const char *kFieldPaths[3] = { "/:nickname", "/:email", "/:password" };

std::string ReplaceAll( std::string text, const std::string &from, const std::string &to ) {
    size_t position = 0;
    while ( ( position = text.find( from, position ) ) != std::string::npos ) {
        text.replace( position, from.size(), to );
        position += to.size();
    }
    return text;
}
} // namespace

GMLogin::GMLogin() : GameMode( eGameMode::GM_Login ) {}

GMLogin::~GMLogin() {}

void GMLogin::loadTitleScene( int32_t cinematicIndex ) {
    titleScene.unload();
    if ( !CmdLine::HasArg( "-notitlescene" ) ) {
        titleScene.load( island.c_str(), cinematicIndex );
    }
}

bool GMLogin::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    if ( gpAudio != nullptr ) {
        gpAudio->setGameMode( "LOGIN" );
        // FlashSounds::PlayFrontEndMusic "LOGIN" (FUN_00d85280): the music
        // entry of the sound/musics folder (a loose wav on the PC build).
        std::vector<uint8_t> musicData;
        if ( musicClip == nullptr && gpFile->loadFile( "Euro/Bnk/sound/musics/login.wav", musicData ) ) {
            musicClip = gpAudio->loadClip( musicData.data(), musicData.size(), "login" );
        }
        if ( musicClip != nullptr ) {
            GSAudio::VoiceParams params;
            params.Client = 8; // FrontEndMusic
            params.bLoop = true;
            musicVoice = gpAudio->play( musicClip, params );
            OTDU_LOG_INFO( "LOGIN: front end music playing (voice %u)\n", musicVoice );
        } else {
            OTDU_LOG_WARN( "LOGIN: sound/musics/login.wav not found / decoded\n" );
        }
    }

    // Press start cinematic (FUN_009c6b20): random, FORCELOGINCINEINDEX n
    // (-cine n: alias).
    int32_t cinematicIndex = -1;
    if ( CmdLine::HasArg( "-FORCELOGINCINEINDEX" ) ) {
        cinematicIndex = CmdLine::GetArgInt( "-FORCELOGINCINEINDEX", -1 );
    } else if ( CmdLine::HasArg( "-cine" ) ) {
        cinematicIndex = CmdLine::GetArgInt( "-cine", -1 );
    }
    loadTitleScene( cinematicIndex );

    // FUN_009c7ea0: LOG_IN2 (/:WhatGameMode = 0, play), GENERAL
    // (/:WhatGameMode = 0); NAT message iff the registry NetworkNatType is
    // not "Open" (no registry here: Open).
    if ( login2.load( gpFlash->getMovie( "log_in2" ) ) ) {
        login2.setFSCommandCallback( [this]( const std::string &command, const std::string & ) {
            if ( command == "stok" ) {
                bStok = true;
            }
        } );
        login2.setVariableNumber( "/:WhatGameMode", 0.0 );
        login2.setVariable( "/:m_PressStart", LocStr( kLocPressStart, "Press START to play" ) );
        login2.setVariable( "/:m_NAT_msg_", LocStr( kLocNATMessage, "" ) );
        login2.setVariableBool( "/:NAT_msg", false );
    }
    // FUN_00d8e200 -> FUN_00d8d930 (GENERAL, MAIN): /:WhatPlatform = 1 (PC;
    // the button glyph sprites pick their "pc" frame from it).
    if ( general.load( gpFlash->getMovie( "general" ) ) ) {
        general.setVariableNumber( "/:WhatGameMode", 0.0 );
        general.setVariableNumber( "/:WhatPlatform", 1.0 );
    }
    setState( LS_PressStart );
    return true;
}

void GMLogin::exit() {
    if ( gpAudio != nullptr && musicVoice != GSAudio::kInvalidVoice ) {
        gpAudio->stop( musicVoice );
        musicVoice = GSAudio::kInvalidVoice;
    }
    if ( bProfileUiReady ) {
        gpInput->stopTextInput();
        bProfileUiReady = false;
    }
    titleScene.unload();
    login2.unload();
    general.unload();
    main.unload();
}

void GMLogin::setState( eState newState ) {
    state = newState;
    subState = 0;
}

void GMLogin::showNoDataPopup() {
    // FUN_00ac4c80 -> FUN_00d86c00: GENERAL "avert", buttons type 3 (A = OK,
    // B = Retry), label "no_data", text with §GAME_NAME§ = "TDU2".
    std::string text = ReplaceAll( LocStr( kLocNoProgressionData, "" ), "\xc2\xa7GAME_NAME\xc2\xa7", kGameName );
    text = ReplaceAll( text, "\xa7GAME_NAME\xa7", kGameName );
    general.setVariable( "/:m_AvertText", text );
    general.setVariable( "/:AvStateButtonA", LocStr( kLocOk, "OK" ) );
    general.setVariable( "/:AvStateButtonB", LocStr( kLocRetry, "Retry" ) );
    general.gotoLabel( "/avert", "no_data", true );
    bPopupShown = true;
}

bool GMLogin::isPopupClosing() {
    // avert "close_no_data" (f61..f71) ends with gotoLabel "default" (f0).
    FlashPlayer::Clip *pAvert = general.findClip( "/avert" );
    return pAvert != nullptr && pAvert->CurrentFrame >= 61;
}

void GMLogin::enterProfileScreen() {
    // The "New profile" dialog of MAIN.vmf (/menus/profile/new_profile,
    // sprite 257 + its popup sprite 250; dumped in _main_vmf.txt). The retail
    // exe drives the same movie on this screen; the GameSpy login widget of
    // its state 25 is not ported (no network layer) [ENC].
    FlashMovie *pMovie = gpFlash != nullptr ? gpFlash->getMovie( "main" ) : nullptr;
    if ( pMovie == nullptr || !main.load( pMovie ) ) {
        OTDU_LOG_WARN( "LOGIN: MAIN.vmf not available - the New profile screen is skipped\n" );
        setState( LS_SaveCheck );
        return;
    }
    main.setFSCommandCallback( [this]( const std::string &command, const std::string &args ) {
        handleProfileCommand( command, args );
    } );
    // The root setup of retail's login (root_in, WhatGameMode 0 branch): the
    // menus tree becomes visible and shows the profiles page.
    main.setVariableNumber( "/:WhatGameMode", 0.0 );
    main.setVariableNumber( "/:WhatPlatform", 1.0 );
    // The dialog laws of the movie (S250 / S257):
    // - /:NoProfile = 1: the back button means "quit the game" (FSCommand
    //   quit + pcan) instead of "cancel";
    // - BetaPC false: the green Online badge is visible (x_online._visible =
    //   !_root.BetaPC).
    main.setVariableBool( "/:NoProfile", true );
    main.setVariableBool( "/:BetaPC", false );
    // The captions (the edit texts bind these root variables; the shipped
    // defaults are the French retail strings).
    main.setVariable( "/:mpc_NewProfile", LocStr( kLocNewProfile, "New profile" ) );
    main.setVariable( "/:mpc_Nickname", LocStr( kLocNickname, "Nickname" ) );
    main.setVariable( "/:m_Username_", LocStr( kLocAccountName, "Atari.com account name" ) );
    main.setVariable( "/:mpc_Password", LocStr( kLocPassword, "Password" ) );
    main.setVariable( "/:m_Ok", LocStr( kLocOk, "OK" ) );
    main.setVariable( "/:m_Quit", LocStr( kLocQuit, "Quit" ) );
    main.setVariable( "/:m_OnLine", LocStr( kLocOnline, "Online" ) );
    main.setVariable( "/:m_OffLine_", LocStr( kLocOffline, "Offline" ) );
    main.setVariable( "/:m_Offline_", LocStr( kLocOffline, "Offline" ) );
    main.setVariable( "/:m_AvertAtariAccount_", LocStr( kLocAvertAccount,
        "You must have an atari.com account to play TDU2 online. If you do not have one, "
        "please create one by clicking on the link." ) );
    // The typed buffers: the nickname pre-fills from the current name law
    // (the launcher's -playername / an older save), the account fields stay
    // empty (nothing connects yet).
    fields[Field_Nickname] = GetPlayerProfile().Name;
    fields[Field_Email].clear();
    fields[Field_Password].clear();
    focusedField = Field_Nickname;
    // The retail navigation to this screen: root_in places the menus tree and
    // makes it visible (its script starts the main menu page), then the
    // profiles page replaces it (profiles_in -> profiles_stop) and the two
    // open labels chain down to the dialog (/menus/profile f1 ->
    // new_profile f1, whose script focuses /:nickname and puts the popup on
    // its gamespy_on state).
    main.gotoLabel( "/", "root_in", true );
    main.gotoLabel( "/", "root_stop", false );
    main.gotoLabel( "/menus", "profiles_in", true );
    main.gotoLabel( "/menus/profile", "open", true );
    main.gotoLabel( "/menus/profile/new_profile", "open", true );
    pushFieldVariables();
    gpInput->startTextInput();
    bProfileUiReady = true;
    if ( CmdLine::HasArg( "-dumpclips" ) ) {
        main.dumpClips( "/", CmdLine::GetArgInt( "-dumpclips", 5 ) );
    }
    OTDU_LOG_INFO( "LOGIN: New profile screen (nickname pre-filled '%s')\n", fields[Field_Nickname].c_str() );
}

void GMLogin::setFocusedField( int32_t field, bool bNotifyMovie ) {
    focusedField = Clamp( field, 0, Field_Count - 1 );
    if ( bNotifyMovie ) {
        // The movie's own focus law (S250): /:TextToFocus = the field path,
        // then FSCommand:tfoc (the game-side handler follows the same
        // variable, so no round trip here).
        main.setVariable( "/:TextToFocus", kFieldPaths[focusedField] );
    }
}

void GMLogin::pushFieldVariables() {
    for ( int32_t field = 0; field < Field_Count; field++ ) {
        main.setVariable( kFieldPaths[field], fields[field] );
    }
}

void GMLogin::tickProfileScreen( float deltaTime ) {
    if ( !bProfileUiReady ) {
        return;
    }
    // Typed characters land in the focused field (SDL text input; the
    // autoplay harness feeds the same buffer with text=...).
    const std::string &typed = gpInput->getTextInput();
    if ( !typed.empty() ) {
        std::string &buffer = fields[focusedField];
        constexpr size_t kMaxFieldLength = 32; // [INF: no retail limit decoded]
        const size_t head = buffer.size() + typed.size();
        if ( head <= kMaxFieldLength ) {
            buffer += typed;
        } else if ( buffer.size() < kMaxFieldLength ) {
            buffer += typed.substr( 0, kMaxFieldLength - buffer.size() );
        }
        gpInput->clearTextInput();
        pushFieldVariables();
    }
    // Backspace edits the focused field; on an empty field it is the movie's
    // B (button 254 cond 0x1000 -> /:NoProfile == 1: FSCommand quit + pcan,
    // the game quits - the retail law of this dialog).
    if ( gpInput->wasBackspacePressed() ) {
        std::string &buffer = fields[focusedField];
        if ( !buffer.empty() ) {
            // Strip one UTF-8 rune: the continuation bytes (10xxxxxx) go with
            // their lead byte.
            while ( !buffer.empty() && ( buffer.back() & 0xC0 ) == 0x80 ) {
                buffer.pop_back();
            }
            if ( !buffer.empty() ) {
                buffer.pop_back();
            }
            pushFieldVariables();
        } else {
            main.sendKey( 8 );
        }
    }
    // Tab / Up / Down walk the fields, Enter validates (button 254 cond
    // 0x1a00 = SWF key 13: close + FSCommand pnew).
    const bool bNext = gpInput->wasKeyPressed( SDL_SCANCODE_TAB ) || gpInput->wasPressed( eInputAction::IA_MenuDown );
    const bool bPrevious = gpInput->wasPressed( eInputAction::IA_MenuUp );
    if ( bNext || bPrevious ) {
        setFocusedField( ( focusedField + ( bNext ? 1 : Field_Count - 1 ) ) % Field_Count, true );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        main.sendKey( 13 );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        // ESC = B: quit on this screen (the fields are cleared backspaces
        // first, the retail dialog has no cancel without a profile).
        main.sendKey( 8 );
    }
    if ( gpInput->wasMouseClicked() ) {
        main.sendMouseRelease( gpInput->getMouseX(), gpInput->getMouseY() );
    }
    main.tick( deltaTime );
    if ( bCommitRequested ) {
        bCommitRequested = false;
        bProfileUiReady = false;
        gpInput->stopTextInput();
        main.unload();
        setState( LS_SaveCheck );
    }
}

void GMLogin::commitProfile() {
    // FSCommand pnew (the OK button): the nickname becomes the profile name.
    // An empty one keeps the dialog open (the movie has no inline invalid
    // state here; loc 1530 is a separate avert the retail exe drove [ENC]).
    std::string name = fields[Field_Nickname];
    while ( !name.empty() && ( name.front() == ' ' || name.front() == '\t' ) ) {
        name.erase( name.begin() );
    }
    while ( !name.empty() && ( name.back() == ' ' || name.back() == '\t' ) ) {
        name.pop_back();
    }
    if ( name.empty() ) {
        OTDU_LOG_INFO( "LOGIN: empty nickname - the New profile dialog stays open\n" );
        setFocusedField( Field_Nickname, true );
        return;
    }
    GetPlayerProfile().Name = name;
    SaveProfile( "new profile" );
    OTDU_LOG_INFO( "LOGIN: profile named '%s' (saved)\n", name.c_str() );
    // The teardown waits for main.tick() to return: this runs inside its
    // pending-action callback and unloading the movie here would free the
    // clips the tick is still walking.
    bCommitRequested = true;
}

void GMLogin::handleProfileCommand( const std::string &command, const std::string &args ) {
    OTDU_LOG_DEBUG( "LOGIN: New profile FSCommand '%s' '%s'\n", command.c_str(), args.c_str() );
    if ( command == "pnew" ) {
        commitProfile();
    } else if ( command == "quit" || command == "pcan" ) {
        // The Quit button (B with /:NoProfile == 1): the retail dialog has no
        // cancel without a profile, the game exits.
        OTDU_LOG_INFO( "LOGIN: quit from the New profile screen\n" );
        pGameInstance->requestExit();
    } else if ( command == "tfoc" ) {
        // The movie moved the focus (a click on a field): follow /:TextToFocus.
        const std::string &path = main.getVariable( "/:TextToFocus" );
        for ( int32_t field = 0; field < Field_Count; field++ ) {
            if ( path == kFieldPaths[field] ) {
                focusedField = field;
                break;
            }
        }
    }
}

void GMLogin::tick( float deltaTime ) {
    timerMs += static_cast<int32_t>( std::lround( deltaTime * 1000.0f ) );
    titleScene.tick( deltaTime );
    login2.tick( deltaTime );
    general.tick( deltaTime );

    switch ( state ) {
    case LS_PressStart:
        if ( subState == 0 ) {
            subState = 1; // no loading screen video
        }
        if ( subState == 1 && timerMs > 3000 ) {
            subState = 2;
        }
        if ( subState <= 2 && gpInput->wasKeyPressed( SDL_SCANCODE_R ) && titleScene.getNumCinematics() > 0 ) {
            // OpenTDU2 key: next press start cinematic.
            loadTitleScene( ( titleScene.getCinematicIndex() + 1 ) % titleScene.getNumCinematics() );
        }
        if ( subState == 2 && gpInput->wasStartReleased() ) {
            login2.gotoLabel( "/", "next", true );
            bStok = false;
            subState = 3;
        }
        if ( subState == 3 && bStok ) {
            // State 2: the "New profile" screen (retail: the profile check /
            // login widget band; the port shows the MAIN.vmf dialog), then
            // states 4 -> 6 (offline, profile present).
            setState( LS_Profile );
        }
        break;
    case LS_Profile:
        if ( subState == 0 ) {
            enterProfileScreen();
            subState = 1;
        }
        tickProfileScreen( deltaTime );
        break;
    case LS_SaveCheck:
        // The save backend (edFileSaveGamePC) is encrypted in the
        // executable: no progression data is ever found here. A new popup
        // waits for the previous one to be closed.
        if ( !isPopupClosing() ) {
            showNoDataPopup();
            setState( LS_SaveResult );
        }
        break;
    case LS_SaveResult:
        // GENERAL avert buttons: key catcher ch481 (Enter: "avye" = OK,
        // Backspace: "avno" = Retry), then "close_no_data"; listener
        // FUN_009c7010: OK = new game, Retry = check again.
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            general.gotoLabel( "/avert", "close_no_data", true );
            setState( LS_NextMode );
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            general.gotoLabel( "/avert", "close_no_data", true );
            setState( LS_SaveCheck ); // next = prev: the check runs again
        }
        break;
    case LS_NextMode:
        // FUN_009c9180: SCENARIOTUTORIAL = ON and no progression loaded:
        // AVATAR_CHOICE.
        nextMode = eGameMode::GM_AvatarChoice;
        setState( LS_Leave );
        break;
    case LS_Leave:
        if ( subState == 0 ) {
            pGameInstance->setNextGameMode( nextMode );
            subState = 1;
        }
        break;
    }
}

void GMLogin::drawOffscreen() { titleScene.renderOffscreen(); }

void GMLogin::draw() {
    Renderer2D &renderer = gpRender->get2DRenderer();
    titleScene.draw();
    static const bool bNoTitleFlash = CmdLine::HasArg( "-notitleflash" );
    if ( !bNoTitleFlash && login2.isLoaded() ) {
        login2.draw( renderer );
    }
    // The New profile dialog over the title scene (only its popup chain is
    // visible; the rest of the menus tree stays off).
    if ( bProfileUiReady && main.isLoaded() ) {
        main.draw( renderer );
    }
    if ( bPopupShown && general.isLoaded() ) {
        general.drawClipOnly( renderer, "/avert" );
    }
}

void GMLogin::drawDebug() {
    if ( ImGui::Begin( "LOGIN" ) ) {
        ImGui::Text( "state %d sub %d timer %d ms", static_cast<int32_t>( state ), subState, timerMs );
        ImGui::Text( "cinematic %d / %d", titleScene.getCinematicIndex(), titleScene.getNumCinematics() );
    }
    ImGui::End();
}
