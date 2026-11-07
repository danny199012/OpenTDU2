#include "shared.h"
#include "championship_menu.h"

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
    snprintf( text, sizeof( text ), "%d:%02d.%02d", whole / 60, whole % 60, static_cast<int32_t>( ( seconds - whole ) * 100.0f ) );
    return text;
}

std::string FormatMoney( int32_t value ) {
    std::string digits = std::to_string( value );
    std::string out;
    for ( size_t i = 0; i < digits.size(); i++ ) {
        if ( i > 0 && ( digits.size() - i ) % 3 == 0 ) {
            out += ',';
        }
        out += digits[i];
    }
    return "$" + out;
}
} // namespace

void ChampionshipMenu::open( const LicenseInfo &championship ) {
    pChampionship = &championship;
    races.clear();
    bool bPreviousDone = true;
    for ( const std::string &name : championship.Challenges ) {
        RaceRow row;
        row.Name = name;
        Ig2Challenge challenge;
        if ( challenge.load( name ) ) {
            row.Title = challenge.getTitle();
            const bool bTimed = challenge.getMode() == Ig2Challenge::ModeTimeAttack;
            row.Target = ( bTimed ? challenge.getTargetMs() : challenge.getLimitMs() ) / 1000.0f;
            row.Laps = challenge.getLaps();
        }
        row.bOpen = bPreviousDone && IsRaceRunnable( name );
        // A speed challenge does not hold the next race back (it is not run).
        bPreviousDone = IsRaceRunnable( name ) ? IsTestPassed( name ) : bPreviousDone;
        races.push_back( row );
    }
    selected = 0;
    for ( size_t i = 0; i < races.size(); i++ ) {
        if ( races[i].bOpen && !IsTestPassed( races[i].Name ) ) {
            selected = static_cast<int32_t>( i );
            break;
        }
    }
    startedRace.clear();
    message.clear();
    messageTimer = 0.0f;
    OTDU_LOG_INFO( "ChampionshipMenu: '%s' - %zu race(s)\n", championship.Name.c_str(), races.size() );
}

void ChampionshipMenu::close() { pChampionship = nullptr; }

bool ChampionshipMenu::takeStartedRace( std::string &outRace ) {
    if ( startedRace.empty() ) {
        return false;
    }
    outRace = startedRace;
    startedRace.clear();
    return true;
}

void ChampionshipMenu::tick( float deltaTime ) {
    if ( pChampionship == nullptr ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    const int32_t count = static_cast<int32_t>( races.size() );
    if ( count > 0 && gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        selected = ( selected + count - 1 ) % count;
    }
    if ( count > 0 && gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        selected = ( selected + 1 ) % count;
    }
    if ( count > 0 && gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        const RaceRow &race = races[selected];
        if ( !IsRaceRunnable( race.Name ) ) {
            message = "Speed challenges are not available yet";
            messageTimer = 3.0f;
        } else if ( !race.bOpen ) {
            message = "Finish the previous race first";
            messageTimer = 3.0f;
        } else {
            startedRace = race.Name;
            pChampionship = nullptr;
        }
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        close();
    }
}

void ChampionshipMenu::draw( Renderer2D &renderer ) {
    if ( pChampionship == nullptr ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    const PlayerProfile &profile = GetPlayerProfile();
    renderer.begin( 1280.0f, 720.0f );
    const float x0 = 760.0f, y0 = 50.0f, w = 490.0f, h = 620.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 22, 28, 220 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 70, 40, 12, 255 ) );
    renderer.drawText( pFont, pChampionship->Name.c_str(), x0 + 16.0f, y0 + 14.0f, 20.0f, Rgba( 255, 255, 255 ) );
    float y = y0 + 64.0f;
    char header[160];
    const int32_t category = pChampionship->VehicleCategory[0];
    snprintf( header, sizeof( header ), "%s%s   -   %s a race, %s the championship", category > 0 ? "Class " : "All classes",
              VehicleClassName( category ),
              FormatMoney( pChampionship->RaceMoney ).c_str(), FormatMoney( pChampionship->ChampMoney ).c_str() );
    renderer.drawText( pFont, header, x0 + 16.0f, y, 15.0f, Rgba( 255, 210, 80 ) );
    y += 32.0f;
    for ( size_t i = 0; i < races.size(); i++ ) {
        const RaceRow &race = races[i];
        if ( static_cast<int32_t>( i ) == selected ) {
            renderer.drawRect( Rect2D( x0 + 8.0f, y - 4.0f, w - 16.0f, 50.0f ), Rgba( 130, 70, 20, 255 ) );
        }
        const bool bRunnable = IsRaceRunnable( race.Name );
        const uint32_t color = race.bOpen ? Rgba( 255, 255, 255 ) : Rgba( 130, 130, 130 );
        char line[200];
        // The titles carry their mode ("Race: Salt marsh challenge") [DATA].
        if ( race.Title.empty() ) {
            snprintf( line, sizeof( line ), "%zu. %s - %s", i + 1, RaceModeName( race.Name ), race.Name.c_str() );
        } else {
            snprintf( line, sizeof( line ), "%zu. %s", i + 1, race.Title.c_str() );
        }
        renderer.drawText( pFont, line, x0 + 20.0f, y, 17.0f, color );
        std::string detail;
        if ( !bRunnable ) {
            detail = "Not available yet";
        } else {
            const bool bTimed = RaceModeLetter( race.Name ) == 'T';
            detail = ( bTimed ? "Target " : "Limit " ) + FormatSeconds( race.Target );
            if ( race.Laps > 1 ) {
                detail += "   " + std::to_string( race.Laps ) + " laps";
            }
            auto best = profile.PassedTests.find( race.Name );
            if ( best != profile.PassedTests.end() ) {
                detail += "   Best " + FormatSeconds( best->second );
            } else if ( !race.bOpen ) {
                detail += "   Locked";
            }
        }
        renderer.drawText( pFont, detail.c_str(), x0 + 36.0f, y + 22.0f, 14.0f,
                           IsTestPassed( race.Name ) ? Rgba( 120, 255, 120 ) : Rgba( 190, 190, 190 ) );
        y += 56.0f;
    }
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 80.0f, 18.0f, Rgba( 255, 210, 80 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, "A / Enter: race      B / Esc: leave", x0 + w * 0.5f, y0 + h - 38.0f, 16.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.end();
}
