#include "shared.h"
#include "estate_menu.h"

#include "flash/gs_flash.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/renderer_2d.h"

#include <algorithm>

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

void EstateMenu::open( uint64_t realtorId ) {
    stock = FindHousesByRealtor( realtorId );
    realtorName = "Real Estate Agency";
    // The agency's own name: its loca label when the DBHouse realtor names one
    // (the rows carry the building name "ERE_B_2236", not a label) [INF].
    selected = 0;
    bConfirmSell = false;
    message.clear();
    messageTimer = 0.0f;
    if ( !stock.empty() ) {
        OTDU_LOG_INFO( "Estate: realtor 0x%llx '%s': %zu house(s) on sale\n",
                       static_cast<unsigned long long>( realtorId ), stock.front()->Realtor.c_str(), stock.size() );
    }
}

void EstateMenu::close() {
    stock.clear();
    bConfirmSell = false;
}

void EstateMenu::buy( const HouseRow &house ) {
    PlayerProfile &profile = GetPlayerProfile();
    if ( profile.Money < house.BuyPrice ) {
        message = "Not enough money";
    } else {
        profile.Money -= house.BuyPrice;
        profile.OwnedHouses.push_back( house.SpotName );
        profile.NbHouses = static_cast<int32_t>( profile.OwnedHouses.size() );
        message = "Bought: " + house.Name;
        OTDU_LOG_INFO( "Estate: bought '%s' (spot '%s') for $%d (money now $%d, %d house(s))\n", house.Name.c_str(),
                       house.SpotName.c_str(), house.BuyPrice, profile.Money, profile.NbHouses );
        SaveProfile( "house bought" );
    }
    messageTimer = 3.0f;
}

void EstateMenu::sell( const HouseRow &house ) {
    PlayerProfile &profile = GetPlayerProfile();
    const auto found = std::find( profile.OwnedHouses.begin(), profile.OwnedHouses.end(), house.SpotName );
    if ( found == profile.OwnedHouses.end() || profile.OwnedHouses.size() <= 1 ) {
        message = "You cannot sell your only house";
        messageTimer = 3.0f;
        return;
    }
    profile.OwnedHouses.erase( found );
    profile.NbHouses = static_cast<int32_t>( profile.OwnedHouses.size() );
    profile.Money += house.SellPrice;
    if ( profile.HomeHouse == house.SpotName ) {
        profile.HomeHouse = profile.OwnedHouses.front();
    }
    message = "Sold: " + house.Name;
    OTDU_LOG_INFO( "Estate: sold '%s' (spot '%s') for $%d (money now $%d, %d house(s))\n", house.Name.c_str(),
                   house.SpotName.c_str(), house.SellPrice, profile.Money, profile.NbHouses );
    SaveProfile( "house sold" );
    messageTimer = 3.0f;
}

void EstateMenu::tick( float deltaTime ) {
    if ( stock.empty() ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    const int32_t count = static_cast<int32_t>( stock.size() );
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        selected = ( selected + count - 1 ) % count;
        bConfirmSell = false;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        selected = ( selected + 1 ) % count;
        bConfirmSell = false;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        const HouseRow &house = *stock[selected];
        const PlayerProfile &profile = GetPlayerProfile();
        const bool bOwned =
            std::find( profile.OwnedHouses.begin(), profile.OwnedHouses.end(), house.SpotName ) != profile.OwnedHouses.end();
        if ( bOwned ) {
            // Two validates: the first one asks, the second one sells.
            if ( bConfirmSell ) {
                sell( house );
                bConfirmSell = false;
            } else {
                bConfirmSell = true;
                message = "Press A again to sell for " + FormatMoney( house.SellPrice );
                messageTimer = 3.0f;
            }
        } else {
            buy( house );
        }
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ||
                gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        close();
    }
}

void EstateMenu::draw( Renderer2D &renderer ) {
    if ( stock.empty() ) {
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
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 24, 46, 30, 255 ) );
    renderer.drawText( pFont, realtorName.c_str(), x0 + 20.0f, y0 + 10.0f, bSidePanel ? 24.0f : 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                       eTextAlign::TA_Right );
    float y = y0 + 70.0f;
    const int32_t count = static_cast<int32_t>( stock.size() );
    const int32_t visible = static_cast<int32_t>( ( y0 + h - 110.0f - y ) / 44.0f );
    const int32_t first = Clamp( selected - visible / 2, 0, Max( 0, count - visible ) );
    for ( int32_t i = first; i < count && i < first + visible; i++ ) {
        const HouseRow &house = *stock[i];
        const bool bSelected = i == selected;
        const bool bOwned =
            std::find( profile.OwnedHouses.begin(), profile.OwnedHouses.end(), house.SpotName ) != profile.OwnedHouses.end();
        if ( bSelected ) {
            renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 40.0f ), Rgba( 30, 90, 44, 255 ) );
        }
        std::string info = house.Name;
        if ( info.empty() ) {
            info = "House L" + std::to_string( house.Level );
        }
        info += " - L" + std::to_string( house.Level ) + ", " + std::to_string( house.NbCarSlots ) + " cars";
        renderer.drawText( pFont, info.c_str(), x0 + 24.0f, y, textSize, bOwned ? Rgba( 150, 150, 150 ) : Rgba( 255, 255, 255 ) );
        std::string price;
        uint32_t priceColor = Rgba( 255, 255, 255 );
        if ( bOwned ) {
            price = profile.HomeHouse == house.SpotName ? "Yours - home" : "Yours";
            priceColor = Rgba( 150, 150, 150 );
        } else {
            price = FormatMoney( house.BuyPrice );
            priceColor = profile.Money >= house.BuyPrice ? Rgba( 255, 255, 255 ) : Rgba( 255, 110, 110 );
        }
        renderer.drawText( pFont, price.c_str(), x0 + w - 24.0f, y, textSize, priceColor, eTextAlign::TA_Right );
        y += 44.0f;
    }
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 90.0f, textSize, Rgba( 255, 210, 80 ),
                           eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, "A / Enter: buy (again on yours: sell)      B / Esc: leave", x0 + w * 0.5f, y0 + h - 44.0f,
                       bSidePanel ? 17.0f : 20.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    renderer.end();
}
