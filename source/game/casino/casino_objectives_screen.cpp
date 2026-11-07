#include "shared.h"
#include "casino_objectives_screen.h"

#include "game/casino/casino_progression.h"
#include "input/gs_input.h"
#include "render/renderer_2d.h"

#include <cstdio>

namespace {
    constexpr float kRowHeight = 30.0f;
    constexpr float kListTop = 214.0f;
    constexpr int32_t kVisibleRows = 14;

    // A short label for a DBCasinoLevel axis [INF: read off the objectives' own
    // texts - 32 is the games, 33 the variety / exploration ones, 34 the social
    // / online ones, 35 the collection ones].
    const char *AxisLabel( int32_t axis ) {
        switch ( axis ) {
        case 32:
            return "the games";
        case 33:
            return "variety";
        case 34:
            return "social (online)";
        case 35:
            return "collection";
        default:
            return "";
        }
    }
} // namespace

std::string CasinoObjectivesScreen::stripMarkup( const std::string &text ) {
    // The Eden text fields resolve '#color:RRGGBB#' / '#nocolor#' /
    // '#bitmap:..#' themselves; a native draw has to drop them.
    std::string out;
    out.reserve( text.size() );
    for ( size_t i = 0; i < text.size(); ) {
        if ( text[i] == '#' ) {
            const size_t close = text.find( '#', i + 1 );
            if ( close != std::string::npos ) {
                i = close + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

void CasinoObjectivesScreen::open() {
    bOpen = true;
    axisIndex = 0;
    selected = 0;
    scroll = 0;
    stickHold = 0.0f;
    stickRepeat = 0.0f;
    rebuildAxes();
    rebuildRows();
    OTDU_LOG_INFO(
        "CasinoObjectives: open, %zu axis(es), %zu objectives on axis %d\n",
        axes.size(), rows.size(), axes.empty() ? -1 : axes[0] );
}

void CasinoObjectivesScreen::close() {
    bOpen = false;
    rows.clear();
}

void CasinoObjectivesScreen::rebuildAxes() {
    axes.clear();
    for ( const CasinoObjective &objective : GetCasinoObjectives() ) {
        if ( std::find( axes.begin(), axes.end(), objective.Axis ) ==
             axes.end() ) {
            axes.push_back( objective.Axis );
        }
    }
    std::sort( axes.begin(), axes.end() );
    if ( axisIndex >= static_cast<int32_t>( axes.size() ) ) {
        axisIndex = 0;
    }
}

void CasinoObjectivesScreen::rebuildRows() {
    rows.clear();
    if ( axes.empty() ) {
        return;
    }
    const int32_t axis = axes[axisIndex];
    const std::vector<CasinoObjective> &table = GetCasinoObjectives();
    for ( size_t i = 0; i < table.size(); i++ ) {
        if ( table[i].Axis != axis ) {
            continue;
        }
        Row row;
        row.ObjectiveIndex = static_cast<int32_t>( i );
        row.Text = stripMarkup( table[i].Text );
        row.Counter = GetCasinoObjectiveCounter( table[i] );
        row.Target = table[i].ObjectiveValue;
        row.Points = table[i].PointsAwarded;
        row.bDone = row.Target > 0 && row.Counter >= row.Target;
        rows.push_back( row );
    }
}

bool CasinoObjectivesScreen::tick( float deltaTime ) {
    if ( !bOpen ) {
        return false;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ||
         gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        close();
        return true;
    }
    // Left / Right: the axis.
    const int32_t numAxes = static_cast<int32_t>( axes.size() );
    if ( numAxes > 1 ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            axisIndex = ( axisIndex + numAxes - 1 ) % numAxes;
            selected = 0;
            scroll = 0;
            rebuildRows();
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            axisIndex = ( axisIndex + 1 ) % numAxes;
            selected = 0;
            scroll = 0;
            rebuildRows();
        }
    }
    // Up / Down, with a hold repeat (a 73 row list is not browsable one press
    // at a time).
    int32_t move = 0;
    const bool bDown = gpInput->isDown( eInputAction::IA_MenuDown );
    const bool bUp = gpInput->isDown( eInputAction::IA_MenuUp );
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        move = 1;
        stickHold = 0.0f;
        stickRepeat = 0.0f;
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        move = -1;
        stickHold = 0.0f;
        stickRepeat = 0.0f;
    } else if ( bDown || bUp ) {
        stickHold += deltaTime;
        if ( stickHold >= 0.35f ) {
            stickRepeat += deltaTime;
            if ( stickRepeat >= 0.10f ) {
                stickRepeat = 0.0f;
                move = bDown ? 1 : -1;
            }
        }
    } else {
        stickHold = 0.0f;
        stickRepeat = 0.0f;
    }
    if ( move != 0 && !rows.empty() ) {
        selected = Clamp<int32_t>( selected + move, 0,
                                   static_cast<int32_t>( rows.size() ) - 1 );
        if ( selected < scroll ) {
            scroll = selected;
        }
        if ( selected >= scroll + kVisibleRows ) {
            scroll = selected - kVisibleRows + 1;
        }
    }
    return false;
}

void CasinoObjectivesScreen::draw( Renderer2D &renderer, const Font *pFont ) {
    if ( !bOpen || pFont == nullptr ) {
        return;
    }
    renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ),
                       MakeRGBA( 6, 8, 16 ) );
    renderer.drawText( pFont, "GAME PROGRESS", 40.0f, 30.0f, 30.0f,
                       MakeRGBA( 236, 208, 120 ) );
    char line[192] = {};
    snprintf( line, sizeof( line ), "%s - casino level %d - %d points",
              GetCasinoStatusName( GetCasinoStatus() ), GetCasinoStatusLevel(),
              GetCasinoPoints( GetCasinoAxisCount() > 0 ? 32 : 0 ) );
    renderer.drawText( pFont, line, 1240.0f, 36.0f, 20.0f,
                       MakeRGBA( 232, 232, 238 ), eTextAlign::TA_Right );

    // The axis tabs (Left / Right).
    const int32_t numAxes = static_cast<int32_t>( axes.size() );
    for ( int32_t i = 0; i < numAxes; i++ ) {
        const int32_t axis = axes[i];
        int32_t completed = 0;
        int32_t total = 0;
        GetCasinoObjectiveProgress( axis, completed, total );
        const float x = 40.0f + i * 300.0f;
        const bool bCurrent = i == axisIndex;
        if ( bCurrent ) {
            renderer.drawRect( Rect2D( x, 84.0f, 288.0f, 46.0f ),
                               MakeRGBA( 46, 40, 22, 240 ) );
            renderer.drawRectOutline( Rect2D( x, 84.0f, 288.0f, 46.0f ), 2.0f,
                                      MakeRGBA( 236, 208, 120 ) );
        }
        snprintf( line, sizeof( line ), "%d  %s", axis, AxisLabel( axis ) );
        renderer.drawText( pFont, line, x + 14.0f, 92.0f, 18.0f,
                           bCurrent ? MakeRGBA( 236, 208, 120 )
                                    : MakeRGBA( 170, 176, 196 ) );
        snprintf( line, sizeof( line ), "%d / %d", completed, total );
        renderer.drawText( pFont, line, x + 274.0f, 92.0f, 16.0f,
                           MakeRGBA( 170, 176, 196 ), eTextAlign::TA_Right );
    }

    // The current axis.
    if ( numAxes > 0 ) {
        int32_t completed = 0;
        int32_t total = 0;
        GetCasinoObjectiveProgress( axes[axisIndex], completed, total );
        snprintf( line, sizeof( line ),
                  "Axis %d (%s) - %d of %d objectives done - %d shown",
                  axes[axisIndex], AxisLabel( axes[axisIndex] ), completed,
                  total, static_cast<int32_t>( rows.size() ) );
        renderer.drawText( pFont, line, 40.0f, 152.0f, 17.0f,
                           MakeRGBA( 200, 204, 216 ) );
    }

    // The rows.
    for ( int32_t i = 0; i < kVisibleRows; i++ ) {
        const int32_t index = scroll + i;
        if ( index >= static_cast<int32_t>( rows.size() ) ) {
            break;
        }
        const Row &row = rows[index];
        const float y = kListTop + i * kRowHeight;
        if ( index == selected ) {
            renderer.drawRect(
                Rect2D( 32.0f, y - 6.0f, 1216.0f, kRowHeight - 2.0f ),
                MakeRGBA( 30, 34, 48, 240 ) );
        }
        if ( row.bDone ) {
            renderer.drawText( pFont, "DONE", 60.0f, y, 16.0f,
                               MakeRGBA( 120, 236, 140 ) );
        } else {
            snprintf( line, sizeof( line ), "%d / %d", row.Counter,
                      row.Target );
            renderer.drawText( pFont, line, 60.0f, y, 16.0f,
                               MakeRGBA( 150, 154, 168 ) );
        }
        renderer.drawText( pFont, row.Text.c_str(), 210.0f, y, 17.0f,
                           row.bDone ? MakeRGBA( 150, 154, 168 )
                                     : MakeRGBA( 232, 232, 238 ) );
        snprintf( line, sizeof( line ), "+%d", row.Points );
        renderer.drawText( pFont, line, 1232.0f, y, 16.0f,
                           row.bDone ? MakeRGBA( 120, 236, 140 )
                                     : MakeRGBA( 236, 208, 120 ),
                           eTextAlign::TA_Right );
    }

    renderer.drawText(
        pFont,
        "Up / Down: objective    Left / Right: axis    Backspace / B: back",
        640.0f, 676.0f, 15.0f, MakeRGBA( 170, 176, 196 ),
        eTextAlign::TA_Center );
}
