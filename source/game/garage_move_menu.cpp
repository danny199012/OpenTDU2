#include "shared.h"
#include "garage_move_menu.h"

#include "audio/frontend_audio.h"
#include "flash/gs_flash.h"
#include "game/car_shop.h"
#include "game/house_shop.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/renderer_2d.h"

#include <algorithm>

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

std::string HouseName( const std::string &spot ) {
    for ( const HouseRow &house : GetHouses() ) {
        if ( house.SpotName == spot ) {
            return house.Name.empty() ? "House L" + std::to_string( house.Level ) : house.Name;
        }
    }
    return spot;
}
} // namespace

void GarageMoveMenu::open( const std::string &from ) {
    fromHouse = from;
    fromName = HouseName( from );
    step = eStep::Car;
    selected = 0;
    carSelected = 0;
    message.clear();
    messageTimer = 0.0f;
    bOpen = true;
    rebuild();
    PlayFrontEndCue( "mopen" );
    OTDU_LOG_INFO( "GarageMove: opened in '%s' (%zu car(s), %zu other house(s))\n", from.c_str(), carIds.size(), houses.size() );
}

void GarageMoveMenu::close() {
    bOpen = false;
}

void GarageMoveMenu::rebuild() {
    const PlayerProfile &profile = GetPlayerProfile();
    carIds = HouseGarageCars( fromHouse );
    houses.clear();
    for ( const std::string &spot : profile.OwnedHouses ) {
        if ( spot == fromHouse ) {
            continue;
        }
        HouseChoice choice;
        choice.Spot = spot;
        choice.Name = HouseName( spot );
        choice.Cars = static_cast<int32_t>( HouseGarageCars( spot ).size() );
        choice.Capacity = HouseGarageCapacity( spot );
        houses.push_back( choice );
    }
}

void GarageMoveMenu::tick( float deltaTime ) {
    if ( !bOpen ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    const int32_t count = static_cast<int32_t>( step == eStep::Car ? carIds.size() : houses.size() );
    if ( count > 0 ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            selected = ( selected + count - 1 ) % count;
            PlayFrontEndCue( "move" );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            selected = ( selected + 1 ) % count;
            PlayFrontEndCue( "move" );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        if ( step == eStep::House ) {
            step = eStep::Car;
            selected = carSelected;
        } else {
            close();
        }
        PlayFrontEndCue( "back" );
        return;
    }
    if ( !gpInput->wasPressed( eInputAction::IA_MenuValidate ) || count == 0 ) {
        return;
    }
    if ( step == eStep::Car ) {
        if ( houses.empty() ) {
            message = "You own no other house to move a vehicle to";
            messageTimer = 3.0f;
            PlayFrontEndCue( "wrong" );
            return;
        }
        carSelected = selected;
        step = eStep::House;
        selected = 0;
        PlayFrontEndCue( "valid" );
        return;
    }
    const HouseChoice &target = houses[static_cast<size_t>( selected )];
    if ( MoveCarBetweenGarages( carIds[static_cast<size_t>( carSelected )], fromHouse, target.Spot ) ) {
        bMoved = true;
        message = "Moved to " + target.Name;
        messageTimer = 3.0f;
        PlayFrontEndCue( "bigvalid" );
        rebuild();
        step = eStep::Car;
        selected = 0;
        carSelected = 0;
    } else {
        message = "No free place in " + target.Name;
        messageTimer = 3.0f;
        PlayFrontEndCue( "wrong" );
    }
}

void GarageMoveMenu::draw( Renderer2D &renderer ) {
    if ( !bOpen ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    renderer.begin( 1280.0f, 720.0f );
    renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, 140 ) );
    const float x0 = 300.0f, y0 = 130.0f, w = 680.0f, h = 460.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 22, 28, 235 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 120, 24, 24, 255 ) );
    const std::string title = step == eStep::Car ? "Move a vehicle - " + fromName : std::string( "Move to which house?" );
    renderer.drawText( pFont, title.c_str(), x0 + 20.0f, y0 + 10.0f, 26.0f, Rgba( 255, 255, 255 ) );
    float y = y0 + 70.0f;
    if ( step == eStep::Car ) {
        if ( carIds.empty() ) {
            renderer.drawText( pFont, "No vehicle in this garage", x0 + w * 0.5f, y + 40.0f, 22.0f, Rgba( 200, 200, 200 ),
                               eTextAlign::TA_Center );
        }
        for ( size_t i = 0; i < carIds.size() && i < 8; i++ ) {
            if ( static_cast<int32_t>( i ) == selected ) {
                renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 40.0f ), Rgba( 150, 30, 30, 255 ) );
            }
            renderer.drawText( pFont, FindCarDisplayName( carIds[i] ).c_str(), x0 + 24.0f, y, 22.0f, Rgba( 255, 255, 255 ) );
            y += 44.0f;
        }
    } else {
        for ( size_t i = 0; i < houses.size() && i < 8; i++ ) {
            const HouseChoice &choice = houses[i];
            const bool bFull = choice.Cars >= choice.Capacity;
            if ( static_cast<int32_t>( i ) == selected ) {
                renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 40.0f ), Rgba( 150, 30, 30, 255 ) );
            }
            renderer.drawText( pFont, choice.Name.c_str(), x0 + 24.0f, y, 22.0f,
                               bFull ? Rgba( 150, 150, 150 ) : Rgba( 255, 255, 255 ) );
            const std::string places = "Garage " + std::to_string( choice.Cars ) + " / " + std::to_string( choice.Capacity );
            renderer.drawText( pFont, places.c_str(), x0 + w - 24.0f, y, 22.0f, bFull ? Rgba( 255, 110, 110 ) : Rgba( 120, 255, 120 ),
                               eTextAlign::TA_Right );
            y += 44.0f;
        }
    }
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 90.0f, 21.0f, Rgba( 255, 210, 80 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, step == eStep::Car ? "A / Enter: choose      B / Esc: close" : "A / Enter: move here      B / Esc: back",
                       x0 + w * 0.5f, y0 + h - 44.0f, 20.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    renderer.end();
}
