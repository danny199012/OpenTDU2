#include "shared.h"
#include "school_screen.h"

#include "flash/flash_localization.h"
#include "flash/gs_flash.h"
#include "game/freeride/ig2_challenge.h"
#include "input/gs_input.h"
#include "system/cmd_line.h"

#include <algorithm>

namespace {
// The path of the first clip below 'clip' that holds a child named 'child'.
bool FindClipHolding( FlashPlayer::Clip &clip, const std::string &path, const char *pChild, std::string &outPath ) {
    for ( auto &entry : clip.DisplayList ) {
        FlashPlayer::Instance &instance = *entry.second;
        if ( !instance.pClip ) {
            continue;
        }
        if ( StringCmpI( instance.Name.c_str(), pChild ) == 0 ) {
            outPath = path;
            return true;
        }
    }
    for ( auto &entry : clip.DisplayList ) {
        FlashPlayer::Instance &instance = *entry.second;
        if ( instance.pClip && !instance.Name.empty() &&
             FindClipHolding( *instance.pClip, path == "/" ? "/" + instance.Name : path + "/" + instance.Name, pChild,
                              outPath ) ) {
            return true;
        }
    }
    return false;
}

// The test line's icon (R_SCHOOL.vmf type frames: turn, turn90, turn180,
// acceleration, braking, circuit, mixed, overtaking, slalom, obstacles, jump,
// manual, drift) [DATA: the labels]. The .ig2 files carry no type, so it is
// read from the title's words, then the course [INF]: traffic cars (VEHI)
// overtaking, slalom poles slalom, road gates circuit.
std::string TestType( const Ig2Challenge &challenge ) {
    // The title's words first (they name the manoeuvre), then the course.
    std::string title = ToLower( challenge.getTitle() );
    auto has = [&]( const char *pWord ) { return title.find( pWord ) != std::string::npos; };
    if ( has( "overtak" ) || challenge.getVehicleCount() > 0 ) {
        return "overtaking";
    }
    if ( has( "brak" ) ) {
        return "braking";
    }
    if ( has( "slalom" ) ) {
        return "slalom";
    }
    if ( has( "hairpin" ) || has( "180" ) ) {
        return "turn180";
    }
    if ( has( "90" ) ) {
        return "turn90";
    }
    if ( has( "accel" ) ) {
        return "acceleration";
    }
    if ( has( "mix" ) ) {
        return "mixed";
    }
    if ( has( "drift" ) ) {
        return "drift";
    }
    for ( const Ig2Gate &gate : challenge.getGates() ) {
        if ( gate.isSlalom() ) {
            return "slalom";
        }
    }
    for ( const Ig2Gate &gate : challenge.getGates() ) {
        if ( gate.bRoad ) {
            return "circuit";
        }
    }
    return "turn";
}
} // namespace

bool SchoolScreen::load() {
    if ( player.isLoaded() ) {
        return true;
    }
    FlashMovie *pMovie = gpFlash != nullptr ? gpFlash->getMovie( "r_school" ) : nullptr;
    if ( pMovie == nullptr || !player.load( pMovie ) ) {
        OTDU_LOG_WARN( "SchoolScreen: R_SCHOOL.vmf (FrontEnd/HiRes/r_school.bnk) not available\n" );
        return false;
    }
    player.setVariableNumber( "/:WhatPlatform", 1.0 );
    player.setFSCommandCallback(
        [this]( const std::string &command, const std::string & ) { onCommand( command ); } );
    ApplyFlashLocalization( player );
    player.tick( 0.0f );
    return true;
}

void SchoolScreen::unload() {
    bOpen = false;
    player.unload();
}

void SchoolScreen::open( char schoolLetter, int32_t island ) { openAtTest( schoolLetter, island, std::string() ); }

void SchoolScreen::openAtTest( char schoolLetter, int32_t island, const std::string &test ) {
    if ( !load() ) {
        return;
    }
    licenses = GetSchoolLicenses( schoolLetter, island );
    // The carousel starts on the first licence not obtained (or the test's).
    startLicence = 0;
    for ( size_t i = 0; i < licenses.size(); i++ ) {
        if ( !IsLicenseObtained( *licenses[i] ) ) {
            startLicence = static_cast<int32_t>( i );
            break;
        }
    }
    for ( size_t i = 0; i < licenses.size() && !test.empty(); i++ ) {
        if ( std::find( licenses[i]->Challenges.begin(), licenses[i]->Challenges.end(), test ) !=
             licenses[i]->Challenges.end() ) {
            startLicence = static_cast<int32_t>( i );
        }
    }
    startedTest.clear();
    tests.clear();
    testsClip.clear();
    stickRepeat = 0.0f;
    bOpen = true;
    player.setVariableBool( "/:fake", false );
    player.gotoLabel( "/", "open_school", true, true );
    // The movie's walk-mode cursor picked the tutor: ItemType('valid') with
    // SelItemType 'tutor' -> gotoAndStop('school_part'), Open() (the licence
    // window) [DATA]. The port's interior walk already chose the tutor.
    player.tick( 0.0f );
    player.setVariable( "/:SelItemType", "tutor" );
    player.invoke( "/", "ItemType", { "valid" } );
    OTDU_LOG_INFO( "SchoolScreen: school %c, island %d: %zu licence(s), on #%d\n", schoolLetter, island,
                   licenses.size(), startLicence );
}

void SchoolScreen::close() {
    if ( bOpen && player.isLoaded() ) {
        player.gotoLabel( "/", "school_wm", false );
    }
    bOpen = false;
}

bool SchoolScreen::takeStartedTest( std::string &outTest ) {
    if ( startedTest.empty() ) {
        return false;
    }
    outTest = startedTest;
    startedTest.clear();
    return true;
}

void SchoolScreen::fillLicences() {
    // li<n>_state: 'done' (obtained), 'current' (open: the one before is
    // obtained), 'locked' [DATA: the carousel's state frames; INF: the rule,
    // SchoolMenu's]. li<n>_gp1 / _gp2: the licence's two class frames ('A7' ...).
    for ( size_t i = 0; i < licenses.size(); i++ ) {
        const LicenseInfo &license = *licenses[i];
        const bool bOpenLicence = i == 0 || IsLicenseObtained( *licenses[i - 1] );
        const std::string prefix = "/:li" + std::to_string( i );
        player.setVariable( prefix + "_state",
                            IsLicenseObtained( license ) ? "done" : bOpenLicence ? "current" : "locked" );
        for ( int32_t g = 0; g < 2; g++ ) {
            const int32_t category = license.VehicleCategory[g];
            std::string frame = category > 0 ? VehicleClassName( category ) : std::string( "empty" );
            player.setVariable( prefix + ( g == 0 ? "_gp1" : "_gp2" ), frame );
        }
    }
    player.setVariableNumber( "/:nb_licences", static_cast<double>( licenses.size() ) );
    player.setVariableNumber( "/:num_licence", startLicence );
    // The licence date of the card: today's [INF: the retail date source is ENC].
    time_t now = time( nullptr );
    char date[16];
    strftime( date, sizeof( date ), "%d%b%Y", localtime( &now ) );
    player.setVariable( "/:LicenceDate", ToUpper( date ) );
}

int32_t SchoolScreen::currentLicence() {
    const int32_t index = static_cast<int32_t>( atof( player.getVariable( "/:num_licence" ).c_str() ) );
    return Clamp( index, 0, Max( 0, static_cast<int32_t>( licenses.size() ) - 1 ) );
}

void SchoolScreen::fillTests() {
    tests.clear();
    if ( licenses.empty() ) {
        return;
    }
    if ( player.getRoot() == nullptr || !FindClipHolding( *player.getRoot(), "/", "line0", testsClip ) ) {
        OTDU_LOG_WARN( "SchoolScreen: the test list clip (line0 ...) was not found\n" );
        testsClip = "/";
    }
    const LicenseInfo &license = *licenses[currentLicence()];
    int32_t first = -1;
    for ( size_t i = 0; i < license.Challenges.size(); i++ ) {
        const std::string &name = license.Challenges[i];
        tests.push_back( name );
        Ig2Challenge challenge;
        const bool bLoaded = challenge.load( name );
        const std::string line = ( testsClip == "/" ? "/" : testsClip + "/" ) + "line" + std::to_string( i ) + ":";
        std::string title = bLoaded ? challenge.getTitle() : name;
        player.setVariable( line + "t_name", title.empty() ? name : title );
        player.setVariable( line + "type", bLoaded ? TestType( challenge ) : "turn" );
        // Weather: the tests are driven in the island's weather [INF]: sun.
        player.setVariable( line + "meteo", "sun" );
        bool bRoad = false;
        for ( const Ig2Gate &gate : challenge.getGates() ) {
            bRoad = bRoad || gate.bRoad;
        }
        player.setVariable( line + "place", bRoad ? "road" : "r_school" );
        player.setVariableBool( line + "done", IsTestPassed( name ) );
        if ( first < 0 && !IsTestPassed( name ) ) {
            first = static_cast<int32_t>( i );
        }
    }
    player.setVariableNumber( "/:nb_tests_total", static_cast<double>( tests.size() ) );
    player.setVariableNumber( "/:num_test", Max( 0, first ) );
    setTestDescription();
    if ( CmdLine::HasArg( "-loglicenses" ) ) {
        OTDU_LOG_INFO( "SchoolScreen: licence %s: %zu test(s) in '%s'\n", LicenseClasses( license ).c_str(),
                       tests.size(), testsClip.c_str() );
    }
}

void SchoolScreen::setTestDescription() {
    const int32_t index = static_cast<int32_t>( atof( player.getVariable( "/:num_test" ).c_str() ) );
    if ( index < 0 || index >= static_cast<int32_t>( tests.size() ) ) {
        return;
    }
    Ig2Challenge challenge;
    if ( challenge.load( tests[index] ) ) {
        char target[32];
        const int32_t ms = challenge.getTargetMs();
        snprintf( target, sizeof( target ), "%d:%02d.%02d", ms / 60000, ( ms / 1000 ) % 60, ( ms / 10 ) % 100 );
        player.setVariable( "/:TestDesc", challenge.getDescription() + "\n" + target );
    }
}

void SchoolScreen::onCommand( const std::string &command ) {
    if ( CmdLine::HasArg( "-loglicenses" ) ) {
        OTDU_LOG_INFO( "SchoolScreen: FSCommand '%s' (num_licence %s, num_test %s)\n", command.c_str(),
                       player.getVariable( "/:num_licence" ).c_str(), player.getVariable( "/:num_test" ).c_str() );
    }
    if ( command == "sbdd" ) {
        ApplyFlashLocalization( player );
    } else if ( command == "rsch:stli" ) {
        fillLicences();
    } else if ( command == "rsch:dits" ) {
        fillTests();
    } else if ( command == "rsch:dtup" ) {
        setTestDescription();
    } else if ( command == "rsch:tspr" ) {
        const int32_t index = static_cast<int32_t>( atof( player.getVariable( "/:num_test" ).c_str() ) );
        if ( index >= 0 && index < static_cast<int32_t>( tests.size() ) ) {
            startedTest = tests[index];
            bOpen = false;
        }
    } else if ( bOpen && ( command == "wmon" || command == "rsch:exit" ) ) {
        // BackToWM (B on the carousel): back to the walk.
        bOpen = false;
    }
}

void SchoolScreen::tick( float deltaTime ) {
    if ( !bOpen ) {
        return;
    }
    // SWF keys like the pause menu: the carousel steps on left / right, the
    // test list on up / down; Enter A(), Backspace B().
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
    const float stickX = gpInput->getLeftStickX(), stickY = gpInput->getLeftStickY();
    stickRepeat = Max( 0.0f, stickRepeat - deltaTime );
    if ( std::fabs( stickX ) > 0.6f || std::fabs( stickY ) > 0.6f ) {
        if ( stickRepeat <= 0.0f ) {
            player.sendKey( std::fabs( stickX ) > std::fabs( stickY ) ? ( stickX < 0.0f ? 1 : 2 )
                                                                       : ( stickY < 0.0f ? 14 : 15 ) );
            stickRepeat = 0.3f;
        }
    } else {
        stickRepeat = 0.0f;
    }
    player.tick( deltaTime );
}

void SchoolScreen::draw( Renderer2D &renderer ) {
    if ( bOpen && player.isLoaded() ) {
        player.draw( renderer );
    }
}
