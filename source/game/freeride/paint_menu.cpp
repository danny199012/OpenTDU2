#include "shared.h"
#include "paint_menu.h"

#include "flash/gs_flash.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/renderer_2d.h"

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

std::string FormatMoney( int32_t value ) {
    std::string digits = std::to_string( value < 0 ? -value : value );
    std::string out;
    for ( size_t i = 0; i < digits.size(); i++ ) {
        if ( i > 0 && ( digits.size() - i ) % 3 == 0 ) {
            out += ',';
        }
        out += digits[i];
    }
    return ( value < 0 ? "-$" : "$" ) + out;
}
} // namespace

void PaintMenu::open( int32_t newCarId, const std::string &name ) {
    carId = newCarId;
    carName = name;
    paints = CinematicCar::DatabasePaints( carId );
    selected = 0;
    appliedPaint = -1;
    message.clear();
    messageTimer = 0.0f;
    OTDU_LOG_INFO( "Paint: shop for car %d '%s': %zu catalogue row(s)\n", carId, carName.c_str(), paints.size() );
}

void PaintMenu::close() {
    carId = -1;
    paints.clear();
}

bool PaintMenu::takeAppliedPaint( int32_t &outCarId, int32_t &outPaintIndex ) {
    if ( appliedPaint < 0 ) {
        return false;
    }
    outCarId = carId;
    outPaintIndex = appliedPaint;
    appliedPaint = -1;
    return true;
}

void PaintMenu::tick( float deltaTime ) {
    if ( carId < 0 ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    const int32_t count = static_cast<int32_t>( paints.size() );
    if ( count == 0 ) {
        // No DBCarsAndColors rows: nothing to sell this car.
        if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuValidate ) ||
             gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            close();
        }
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        selected = ( selected + count - 1 ) % count;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        selected = ( selected + 1 ) % count;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        const CinematicCar::PaintInfo &paint = paints[selected];
        PlayerProfile &profile = GetPlayerProfile();
        if ( profile.Money < paint.Price ) {
            message = "Not enough money";
        } else {
            profile.Money -= paint.Price;
            profile.OwnedCarPaints[carId] = selected;
            appliedPaint = selected;
            message = "Painted: " + paint.Name;
            OTDU_LOG_INFO( "Paint: car %d painted with row %d '%s' for $%d (money now $%d)\n", carId, selected,
                           paint.Name.c_str(), paint.Price, profile.Money );
            SaveProfile( "paint bought" );
        }
        messageTimer = 3.0f;
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ||
                gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        close();
    }
}

void PaintMenu::draw( Renderer2D &renderer ) {
    if ( carId < 0 ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    const PlayerProfile &profile = GetPlayerProfile();
    renderer.begin( 1280.0f, 720.0f );
    if ( !bSidePanel ) {
        renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, 150 ) );
    }
    const float x0 = bSidePanel ? 790.0f : 240.0f, y0 = bSidePanel ? 60.0f : 110.0f;
    const float w = bSidePanel ? 460.0f : 800.0f, h = bSidePanel ? 600.0f : 500.0f;
    const float textSize = bSidePanel ? 19.0f : 21.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 22, 28, bSidePanel ? 215 : 235 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 20, 40, 52, 255 ) );
    const std::string title = "Paint shop - " + ( carName.empty() ? std::string( "your car" ) : carName );
    renderer.drawText( pFont, title.c_str(), x0 + 20.0f, y0 + 10.0f, bSidePanel ? 24.0f : 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                       eTextAlign::TA_Right );
    float y = y0 + 70.0f;
    if ( paints.empty() ) {
        renderer.drawText( pFont, "This car has no paint catalogue.", x0 + 24.0f, y, textSize, Rgba( 200, 200, 200 ) );
        renderer.drawText( pFont, "B / Esc: leave", x0 + w * 0.5f, y0 + h - 44.0f, bSidePanel ? 17.0f : 20.0f,
                           Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
        renderer.end();
        return;
    }
    const int32_t count = static_cast<int32_t>( paints.size() );
    const int32_t visible = static_cast<int32_t>( ( y0 + h - 110.0f - y ) / 40.0f );
    const int32_t first = Clamp( selected - visible / 2, 0, Max( 0, count - visible ) );
    for ( int32_t i = first; i < count && i < first + visible; i++ ) {
        if ( i == selected ) {
            renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 36.0f ), Rgba( 20, 70, 96, 255 ) );
        }
        renderer.drawText( pFont, paints[i].Name.c_str(), x0 + 24.0f, y, textSize, Rgba( 255, 255, 255 ) );
        const std::string extra =
            paints[i].Price > 0 ? FormatMoney( paints[i].Price ) : std::string( "included" );
        renderer.drawText( pFont, extra.c_str(), x0 + w - 24.0f, y, textSize, Rgba( 200, 200, 200 ), eTextAlign::TA_Right );
        y += 40.0f;
    }
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 90.0f, textSize, Rgba( 255, 210, 80 ),
                           eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, "A / Enter: paint the car      B / Esc: leave", x0 + w * 0.5f, y0 + h - 44.0f,
                       bSidePanel ? 17.0f : 20.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    renderer.end();
}
