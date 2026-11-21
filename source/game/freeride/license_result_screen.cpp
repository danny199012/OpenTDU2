#include "shared.h"
#include "license_result_screen.h"

#include "database/gs_database.h"
#include "flash/flash_localization.h"
#include "flash/gs_flash.h"
#include "input/gs_input.h"
#include "system/cmd_line.h"

namespace {
// The licence panels print times as "03 : 22 : 12" (their placeholder text,
// RESULTS.vmf edit texts 353 / 356 / 363) [DATA].
std::string FormatPanelTime( float seconds ) {
    const int32_t centis = static_cast<int32_t>( Max( 0.0f, seconds ) * 100.0f + 0.5f );
    char text[32];
    snprintf( text, sizeof( text ), "%02d : %02d : %02d", centis / 6000, ( centis / 100 ) % 60, centis % 100 );
    return text;
}
} // namespace

bool LicenseResultScreen::load() {
    if ( player.isLoaded() ) {
        return true;
    }
    FlashMovie *pMovie = gpFlash != nullptr ? gpFlash->getMovie( "results" ) : nullptr;
    if ( pMovie == nullptr || !player.load( pMovie ) ) {
        OTDU_LOG_WARN( "LicenseResultScreen: RESULTS.vmf (FrontEnd/HiRes/results.bnk) not available\n" );
        return false;
    }
    // FUN_00d8e200 (the frontend movies): /:WhatPlatform 1 = PC (the button
    // glyphs pick their "pc" frame from it).
    player.setVariableNumber( "/:WhatPlatform", 1.0 );
    player.setFSCommandCallback( [this]( const std::string &command, const std::string &args ) {
        if ( CmdLine::HasArg( "-loglicenses" ) ) {
            OTDU_LOG_INFO( "LicenseResultScreen: FSCommand '%s' '%s'\n", command.c_str(), args.c_str() );
        }
        if ( command == "sbdd" ) {
            ApplyFlashLocalization( player );
            return;
        }
        // ValidMenu(): retrytest -> rest, nexttest -> nets, quittest -> quit [DATA].
        if ( !bOpen ) {
            return;
        }
        if ( command == "rest" ) {
            choice = eChoice::Retry;
        } else if ( command == "nets" ) {
            choice = eChoice::Next;
        } else if ( command == "quit" ) {
            choice = eChoice::Quit;
        } else if ( command == "rpin" ) {
            // 'replay_test' asks the game to play the run back ("replay in");
            // the port records no replay, so the panel comes back instead
            // (the B path of the menu: failed_re / success_re) [INF].
            bReplayBack = true;
        }
    } );
    ApplyFlashLocalization( player );
    // The root's first frame runs the movie's set-up (its localization
    // request, the menu functions); park it there until a result opens.
    player.tick( 0.0f );
    return true;
}

void LicenseResultScreen::unload() {
    bOpen = false;
    player.unload();
}

void LicenseResultScreen::open( const Info &info ) {
    if ( !load() ) {
        return;
    }
    choice = eChoice::None;
    // The variables the licence frames read [DATA: RESULTS.vmf scripts and edit
    // texts]: the title bar (TestName, TestDesc), the result line (Message),
    // the times (final_score, ref_score), the licence progress panel
    // (LicenceName, nb_tests_done / nb_tests_total) and the menu's switches.
    player.setVariable( "/:TestName", info.TestName );
    player.setVariable( "/:TestDesc", info.TestDesc );
    // The cart's sentence is the game's own text [DATA: loc 1625 "You failed the test. Try again! ...", 1626 "Well done!
    // You've succeeded! Let's go on to the next test.", 1628 "Well done, you've obtained the §LICENSENAME§ license ..."; CODE-dev
    // 0xb5cb1b picks 0x659 / 0x65a / 0x65b by the failed / licensed flags; the 360 capture shows both carts with text].
    std::string message = info.Message;
    const uint32_t locId = !info.bPassed ? 1625u : ( info.Licensed == 1 ? 1628u : 1626u );
    if ( gpDatabase != nullptr ) {
        if ( const char *pText = gpDatabase->getString( locId ); pText != nullptr && pText[0] != '\0' ) {
            message = pText;
            const size_t mark = message.find( "\xC2\xA7LICENSENAME\xC2\xA7" ); // the §LICENSENAME§ placeholder
            if ( mark != std::string::npos ) {
                message.replace( mark, 15, info.LicenceName );
            }
        }
    }
    // The movie's own switches (openCarts: with IsRacingSchool true the scores panel hides and Message2 shows; the 360's
    // school carts show the sentence AND "Final time" [CODE-dev: /:IsRacingSchool, /:IsTestScore]).
    player.setVariableBool( "/:IsRacingSchool", true );
    player.setVariableBool( "/:IsTestScore", true );
    player.setVariable( "/:Message", message );
    player.setVariable( "/:Message2", message );
    player.setVariable( "/:final_score", FormatPanelTime( info.Time ) );
    // Base time + penalty = final (the 360 T06 cart: Base 146"95 + Penalty +00"62 = Final 147"57) [CODE-dev: /:basetime, /:penalty, /:IsPenalty].
    player.setVariable( "/:BaseTime", FormatPanelTime( info.Time - info.Penalty ) );
    player.setVariable( "/:basetime", FormatPanelTime( info.Time - info.Penalty ) );
    player.setVariable( "/:penalty", "+" + FormatPanelTime( info.Penalty ) );
    player.setVariableBool( "/:IsPenalty", info.Penalty > 0.0f );
    player.setVariable( "/:ref_score", FormatPanelTime( info.Target ) );
    player.setVariable( "/:LicenceName", info.LicenceName );
    player.setVariable( "/:nb_tests_done", std::to_string( info.TestsDone ) );
    player.setVariable( "/:nb_tests_total", std::to_string( info.TestsTotal ) );
    player.setVariableNumber( "/:IsLicensed", info.Licensed );
    // IsReward is the Quit row's fourth field and LockReplay the Replay row's
    // (the lock of a row) [INF: from SetRootMenuRS]; the port has no replays.
    player.setVariableNumber( "/:IsReward", 0.0 );
    player.setVariableNumber( "/:LockReplay", 1.0 );
    player.setVariableBool( "/:Input", false );
    bPassed = info.bPassed;
    bReplayBack = false;
    if ( const char *pPreview = CmdLine::GetArgValue( "-resultlabel" ) ) { // inspector: any root label of RESULTS.vmf
        player.gotoLabel( "/", pPreview, true, true );
        if ( CmdLine::HasArg( "-dumpresults" ) ) {
            player.dumpClips( "/", CmdLine::GetArgInt( "-dumpresults", 4 ) );
        }
    } else
    player.gotoLabel( "/", info.bPassed ? "success_test" : "failed_test", true, true );
    bOpen = true;
    stickRepeat = 0.0f;
    if ( CmdLine::HasArg( "-loglicenses" ) ) {
        OTDU_LOG_INFO( "LicenseResultScreen: %s '%s' %s, time %.2f target %.2f, licence %s %d/%d, IsLicensed %d\n",
                       info.bPassed ? "success_test" : "failed_test", info.TestName.c_str(), info.Message.c_str(),
                       info.Time, info.Target, info.LicenceName.c_str(), info.TestsDone, info.TestsTotal, info.Licensed );
    }
}

bool LicenseResultScreen::openPreview( const std::string &title, const std::string &description, const std::string &type,
                                       const std::string &place, const std::string &meteo ) {
    if ( !load() ) {
        return false;
    }
    player.setVariable( "/:TestName", title );
    player.setVariable( "/:TestDesc", description );
    player.setVariableBool( "/:Input", false );
    player.gotoLabel( "/", "preview_in", true, true );
    // The icons: frame labels of /desc/type, /desc/place and /desc/meteo.
    player.gotoLabel( "/desc/type", type.c_str(), false );
    player.gotoLabel( "/desc/place", place.c_str(), false );
    player.gotoLabel( "/desc/meteo", meteo.c_str(), false );
    bPreview = true;
    return true;
}

void LicenseResultScreen::closePreview() {
    if ( bPreview && player.isLoaded() ) {
        player.gotoLabel( "/", "init", false );
    }
    bPreview = false;
}

void LicenseResultScreen::close() {
    if ( bOpen && player.isLoaded() ) {
        // Back to the root's resting frame, so the next result starts clean.
        player.gotoLabel( "/", "init", false );
    }
    bOpen = false;
}

LicenseResultScreen::eChoice LicenseResultScreen::takeChoice() {
    const eChoice taken = choice;
    choice = eChoice::None;
    return taken;
}

void LicenseResultScreen::tick( float deltaTime ) {
    if ( bPreview ) {
        player.tick( deltaTime );
        return;
    }
    if ( !bOpen ) {
        return;
    }
    // The menus take keys through their own on(keyPress) buttons, each guarded
    // by _root.Input (button 374: Enter -> A() on the panel; button 388:
    // Backspace -> B() on the menu) [DATA]. SWF key codes as in PauseMenu.
    static const struct {
        eInputAction Action;
        uint8_t SwfKey;
    } kKeys[] = {
        {eInputAction::IA_MenuUp,       14},
        {eInputAction::IA_MenuDown,     15},
        {eInputAction::IA_MenuLeft,     1 },
        {eInputAction::IA_MenuRight,    2 },
        {eInputAction::IA_MenuValidate, 13},
        {eInputAction::IA_MenuBack,     8 },
    };
    for ( const auto &key : kKeys ) {
        if ( gpInput->wasPressed( key.Action ) ) {
            player.sendKey( key.SwfKey );
        }
    }
    // The left stick steps the menu like the D-pad.
    const float stickY = gpInput->getLeftStickY();
    stickRepeat = Max( 0.0f, stickRepeat - deltaTime );
    if ( std::fabs( stickY ) > 0.6f ) {
        if ( stickRepeat <= 0.0f ) {
            player.sendKey( stickY < 0.0f ? 14 : 15 );
            stickRepeat = 0.3f;
        }
    } else {
        stickRepeat = 0.0f;
    }
    player.tick( deltaTime );
    if ( bReplayBack ) {
        bReplayBack = false;
        player.gotoLabel( "/", bPassed ? "success_re" : "failed_re", true );
    }
}

void LicenseResultScreen::draw( Renderer2D &renderer ) {
    if ( ( bOpen || bPreview ) && player.isLoaded() ) {
        player.draw( renderer );
    }
}
