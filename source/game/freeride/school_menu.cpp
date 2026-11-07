#include "shared.h"
#include "school_menu.h"

#include <algorithm>

#include "flash/gs_flash.h"
#include "game/freeride/ig2_challenge.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/renderer_2d.h"

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

std::string FormatSeconds( float seconds ) {
    char text[32];
    const int32_t whole = static_cast<int32_t>( seconds );
    snprintf( text, sizeof( text ), "%d:%02d.%02d", whole / 60, whole % 60,
              static_cast<int32_t>( ( seconds - whole ) * 100.0f ) );
    return text;
}
} // namespace

void SchoolMenu::open( char schoolLetter, int32_t island ) {
    school = schoolLetter;
    licenses = GetSchoolLicenses( schoolLetter, island );
    step = eStep::Licenses;
    selected = 0;
    selectedTest = 0;
    startedTest.clear();
    message.clear();
    messageTimer = 0.0f;
    bOpen = true;
    OTDU_LOG_INFO( "SchoolMenu: school %c, island %d: %zu licence(s)\n", schoolLetter, island, licenses.size() );
    if ( licenses.empty() ) {
        message = "No licence is taught here";
        messageTimer = 3.0f;
    }
}

void SchoolMenu::close() { bOpen = false; }

void SchoolMenu::openAtTest( const std::string &test ) {
    for ( size_t i = 0; i < licenses.size(); i++ ) {
        const std::vector<std::string> &challenges = licenses[i]->Challenges;
        auto it = std::find( challenges.begin(), challenges.end(), test );
        if ( it == challenges.end() ) {
            continue;
        }
        selected = static_cast<int32_t>( i );
        openLicense( selected );
        selectedTest = static_cast<int32_t>( it - challenges.begin() );
        // On the next test when this one is passed and the next is open.
        if ( IsTestPassed( test ) && selectedTest + 1 < static_cast<int32_t>( tests.size() ) &&
             tests[selectedTest + 1].bOpen ) {
            selectedTest++;
        }
        return;
    }
}

void SchoolMenu::openLicense( int32_t index ) {
    tests.clear();
    const LicenseInfo &license = *licenses[index];
    bool bPreviousPassed = true;
    for ( const std::string &name : license.Challenges ) {
        TestRow row;
        row.Name = name;
        Ig2Challenge challenge;
        if ( challenge.load( name ) ) {
            row.Title = challenge.getTitle();
            row.Target = challenge.getTargetMs() / 1000.0f;
        }
        if ( row.Title.empty() ) {
            row.Title = name;
        }
        row.bOpen = bPreviousPassed;
        bPreviousPassed = IsTestPassed( name );
        tests.push_back( row );
    }
    step = eStep::Tests;
    selectedTest = 0;
    // Start on the first test not passed yet.
    for ( size_t i = 0; i < tests.size(); i++ ) {
        if ( !IsTestPassed( tests[i].Name ) ) {
            selectedTest = static_cast<int32_t>( i );
            break;
        }
    }
}

bool SchoolMenu::takeStartedTest( std::string &outTest ) {
    if ( startedTest.empty() ) {
        return false;
    }
    outTest = startedTest;
    startedTest.clear();
    return true;
}

void SchoolMenu::tick( float deltaTime ) {
    if ( !bOpen ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    const bool bBack = gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause );
    const bool bUp = gpInput->wasPressed( eInputAction::IA_MenuUp );
    const bool bDown = gpInput->wasPressed( eInputAction::IA_MenuDown );
    const bool bValidate = gpInput->wasPressed( eInputAction::IA_MenuValidate );
    if ( step == eStep::Tests ) {
        const int32_t count = static_cast<int32_t>( tests.size() );
        if ( count > 0 && bUp ) {
            selectedTest = ( selectedTest + count - 1 ) % count;
        }
        if ( count > 0 && bDown ) {
            selectedTest = ( selectedTest + 1 ) % count;
        }
        if ( count > 0 && bValidate ) {
            if ( tests[selectedTest].bOpen ) {
                startedTest = tests[selectedTest].Name;
                bOpen = false;
            } else {
                message = "Pass the previous test first";
                messageTimer = 3.0f;
            }
        } else if ( bBack ) {
            step = eStep::Licenses;
        }
        return;
    }
    const int32_t count = static_cast<int32_t>( licenses.size() );
    if ( count > 0 && bUp ) {
        selected = ( selected + count - 1 ) % count;
    }
    if ( count > 0 && bDown ) {
        selected = ( selected + 1 ) % count;
    }
    if ( count > 0 && bValidate ) {
        if ( isLicenseOpen( selected ) ) {
            openLicense( selected );
        } else {
            message = "Obtain the previous licence first";
            messageTimer = 3.0f;
        }
    } else if ( bBack ) {
        close();
    }
}

bool SchoolMenu::isLicenseOpen( int32_t index ) const {
    // The licences of a school open in _order, like the tests [INF].
    return index <= 0 || IsLicenseObtained( *licenses[index - 1] );
}

void SchoolMenu::draw( Renderer2D &renderer ) {
    if ( !bOpen ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    renderer.begin( 1280.0f, 720.0f );
    const float x0 = 790.0f, y0 = 60.0f, w = 460.0f, h = 600.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 22, 28, 215 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 24, 52, 30, 255 ) );
    char title[64];
    snprintf( title, sizeof( title ), "Racing school %c", school );
    renderer.drawText( pFont, title, x0 + 20.0f, y0 + 12.0f, 24.0f, Rgba( 255, 255, 255 ) );
    float y = y0 + 70.0f;
    const char *pHelp = "";
    if ( step == eStep::Licenses ) {
        for ( size_t i = 0; i < licenses.size(); i++ ) {
            const LicenseInfo &license = *licenses[i];
            if ( static_cast<int32_t>( i ) == selected ) {
                renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 36.0f ), Rgba( 40, 110, 50, 255 ) );
            }
            int32_t passed = 0;
            for ( const std::string &test : license.Challenges ) {
                passed += IsTestPassed( test ) ? 1 : 0;
            }
            const std::string name = "Licence " + LicenseClasses( license );
            const bool bLicenseOpen = isLicenseOpen( static_cast<int32_t>( i ) );
            renderer.drawText( pFont, name.c_str(), x0 + 24.0f, y, 20.0f, bLicenseOpen ? Rgba( 255, 255, 255 ) : Rgba( 130, 130, 130 ) );
            char status[48];
            if ( IsLicenseObtained( license ) ) {
                snprintf( status, sizeof( status ), "Obtained" );
            } else if ( !bLicenseOpen ) {
                snprintf( status, sizeof( status ), "Locked" );
            } else {
                snprintf( status, sizeof( status ), "%d / %zu tests", passed, license.Challenges.size() );
            }
            renderer.drawText( pFont, status, x0 + w - 24.0f, y, 20.0f,
                               IsLicenseObtained( license ) ? Rgba( 120, 255, 120 ) : Rgba( 200, 200, 200 ), eTextAlign::TA_Right );
            y += 40.0f;
        }
        pHelp = "A / Enter: the tests      B / Esc: leave";
    } else {
        const LicenseInfo &license = *licenses[selected];
        // The classes the licence opens (DBLicense _vehiclecategory_1 / _2).
        std::string name = "Licence " + LicenseClasses( license ) + "   -   classes " + VehicleClassName( license.VehicleCategory[0] );
        if ( license.VehicleCategory[1] > 0 ) {
            name += std::string( ", " ) + VehicleClassName( license.VehicleCategory[1] );
        }
        renderer.drawText( pFont, name.c_str(), x0 + 20.0f, y, 18.0f, Rgba( 255, 210, 80 ) );
        y += 40.0f;
        const PlayerProfile &profile = GetPlayerProfile();
        for ( size_t i = 0; i < tests.size(); i++ ) {
            const TestRow &test = tests[i];
            if ( static_cast<int32_t>( i ) == selectedTest ) {
                renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 48.0f ), Rgba( 40, 110, 50, 255 ) );
            }
            const uint32_t color = test.bOpen ? Rgba( 255, 255, 255 ) : Rgba( 130, 130, 130 );
            char line[160];
            snprintf( line, sizeof( line ), "%zu. %s", i + 1, test.Title.c_str() );
            renderer.drawText( pFont, line, x0 + 24.0f, y, 18.0f, color );
            auto best = profile.PassedTests.find( test.Name );
            std::string detail = "Target " + FormatSeconds( test.Target );
            if ( best != profile.PassedTests.end() ) {
                detail += "   Best " + FormatSeconds( best->second );
            } else if ( !test.bOpen ) {
                detail += "   Locked";
            }
            renderer.drawText( pFont, detail.c_str(), x0 + 40.0f, y + 21.0f, 15.0f,
                               best != profile.PassedTests.end() ? Rgba( 120, 255, 120 ) : Rgba( 190, 190, 190 ) );
            y += 52.0f;
        }
        pHelp = "A / Enter: take the test      B / Esc: back";
    }
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 84.0f, 20.0f, Rgba( 255, 210, 80 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, pHelp, x0 + w * 0.5f, y0 + h - 40.0f, 17.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    renderer.end();
}
