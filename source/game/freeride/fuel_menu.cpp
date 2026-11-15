#include "shared.h"
#include "fuel_menu.h"

#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "game/freeride/fuel.h"
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
    return std::string( value < 0 ? "-$" : "$" ) + out;
}

const char *GradeName( int32_t grade ) {
    // The retail strings of the two grades (loc 1636 / 1637).
    return grade == 1 ? LocStr( 1637, "Shell v-power" ) : LocStr( 1636, "Shell basic" );
}
} // namespace

void FuelMenu::open( const std::string &where_ ) {
    bOpen = true;
    bConfirmReplace = false;
    where = where_;
    selected = Clamp( GetFuelTank().Grade, 0, 1 );
    message.clear();
    messageTimer = 0.0f;
    hint = LocStr( 1642, "Fill" );
    OTDU_LOG_INFO( "Fuel: pump screen at %s (tank %.1f / %.1f L, grade %d)\n", where.c_str(), GetFuelTank().Litres,
                   GetFuelTank().Capacity, GetFuelTank().Grade );
}

void FuelMenu::close() {
    bOpen = false;
    bConfirmReplace = false;
}

void FuelMenu::tick( float deltaTime ) {
    if ( !bOpen ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        if ( bConfirmReplace ) {
            bConfirmReplace = false;
            message.clear();
            return;
        }
        close();
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        selected = ( selected + 2 ) % 3;
        bConfirmReplace = false;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        selected = ( selected + 1 ) % 3;
        bConfirmReplace = false;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) && selected == 2 ) {
        payRepair();
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        const FuelTank &tank = GetFuelTank();
        // A tank holds one grade: asking to change it is the retail confirm
        // (loc 1640 / 1641).
        if ( tank.Litres > 0.5f && tank.Grade != selected && !bConfirmReplace ) {
            bConfirmReplace = true;
            message = std::string( LocStr( 1640, "Currently, there is another gas type in this tank." ) ) + "  " +
                      LocStr( 1641, "Replace with this gas type ?" );
            messageTimer = 6.0f;
            return;
        }
        fill();
    }
}

bool FuelMenu::takeRepair() {
    const bool bPaid = bRepairPaid;
    bRepairPaid = false;
    return bPaid;
}

void FuelMenu::payRepair() {
    PlayerProfile &profile = GetPlayerProfile();
    if ( repair.Damage < 1500.0f ) {
        message = "Nothing to repair: the car is in good shape.";
        messageTimer = 4.0f;
        return;
    }
    if ( profile.Money < repair.Price ) {
        message = std::string( LocStr( 1865, "Car repair" ) ) + ": you need " + FormatMoney( repair.Price ) + ".";
        messageTimer = 5.0f;
        return;
    }
    profile.Money -= repair.Price;
    bRepairPaid = true;
    message = std::string( LocStr( 1643, "Bill" ) ) + ": " + FormatMoney( repair.Price ) + "  (" + LocStr( 1865, "Car repair" ) + ")";
    messageTimer = 5.0f;
    SaveProfile( "repair" );
    OTDU_LOG_INFO( "Repair: car repaired for $%d (damage %.0f, money %d)\n", repair.Price, repair.Damage, profile.Money );
}

void FuelMenu::fill() {
    bConfirmReplace = false;
    PlayerProfile &profile = GetPlayerProfile();
    const float litres = FuelLitresToFill();
    const float bill = FuelBill( selected, litres );
    if ( litres <= 0.0f ) {
        message = std::string( LocStr( 1643, "Bill" ) ) + ": the tank is already full.";
        messageTimer = 4.0f;
        return;
    }
    if ( profile.Money >= static_cast<int32_t>( bill ) ) {
        profile.Money -= static_cast<int32_t>( bill + 0.5f );
        FillTank( selected );
        lastBill = bill;
        message = std::string( LocStr( 1643, "Bill" ) ) + ": " + FormatMoney( static_cast<int32_t>( bill + 0.5f ) ) +
                  "  (" + std::to_string( static_cast<int>( litres + 0.5f ) ) + " L " + GradeName( selected ) + ")";
        messageTimer = 5.0f;
        SaveProfile( "fuel" );
        OTDU_LOG_INFO( "Fuel: filled %.1f L %s for $%.2f (money %d)\n", litres, GradeName( selected ), bill,
                       profile.Money );
    } else {
        // The retail line for a player who cannot pay (loc 1645): SHELL pays the
        // bill. The tank is filled anyway - no debt system exists [INF].
        FillTank( selected );
        message = LocStr( 1645, "As you don't have enough money, SHELL is glad to pay the bill..." );
        messageTimer = 6.0f;
        SaveProfile( "fuel" );
        OTDU_LOG_INFO( "Fuel: filled %.1f L %s, the bill of $%.2f is on SHELL (money %d)\n", litres,
                       GradeName( selected ), bill, profile.Money );
    }
}

void FuelMenu::draw( Renderer2D &renderer ) {
    if ( !bOpen ) {
        return;
    }
    const FuelTank &tank = GetFuelTank();
    const PlayerProfile &profile = GetPlayerProfile();
    const Font *pFont = gpFlash->getTitleFont();
    renderer.begin( 1280.0f, 720.0f );
    renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, 140 ) );
    const float x0 = 300.0f, y0 = 150.0f, w = 680.0f, h = 470.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 22, 28, 240 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 56.0f ), Rgba( 20, 40, 52 ) );
    renderer.drawText( pFont, LocStr( 1635, "Gas station" ), x0 + 20.0f, y0 + 12.0f, 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 14.0f, 24.0f,
                       Rgba( 120, 255, 120 ), eTextAlign::TA_Right );

    char line[160];
    snprintf( line, sizeof( line ), "%.1f / %.1f L   %.0f km", tank.Litres, tank.Capacity, tank.RangeKm );
    renderer.drawText( pFont, line, x0 + 24.0f, y0 + 74.0f, 20.0f, Rgba( 200, 220, 230 ) );
    snprintf( line, sizeof( line ), "in the tank: %s", GradeName( tank.Grade ) );
    renderer.drawText( pFont, line, x0 + 24.0f, y0 + 100.0f, 18.0f, Rgba( 170, 180, 190 ) );

    const float litres = FuelLitresToFill();
    float y = y0 + 150.0f;
    for ( int32_t grade = 0; grade < 3; grade++ ) {
        const bool bSelected = grade == selected;
        if ( grade == 2 ) {
            // The car repair row: the retail title (loc 1865) and the bill.
            if ( bSelected ) {
                renderer.drawRect( Rect2D( x0 + 16.0f, y - 6.0f, w - 32.0f, 46.0f ), Rgba( 40, 60, 80 ) );
            }
            renderer.drawText( pFont, LocStr( 1865, "Car repair" ), x0 + 32.0f, y, 22.0f,
                               bSelected ? Rgba( 255, 255, 255 ) : Rgba( 180, 190, 200 ) );
            const std::string repairBill = repair.Damage < 1500.0f ? std::string( "-" ) : FormatMoney( repair.Price );
            renderer.drawText( pFont, repairBill.c_str(), x0 + w - 32.0f, y, 22.0f, Rgba( 200, 220, 200 ), eTextAlign::TA_Right );
            y += 54.0f;
            continue;
        }

        if ( bSelected ) {
            renderer.drawRect( Rect2D( x0 + 16.0f, y - 6.0f, w - 32.0f, 46.0f ), Rgba( 40, 60, 80 ) );
        }
        snprintf( line, sizeof( line ), "%s   $%.2f / L", GradeName( grade ), FuelPricePerLitre( grade ) );
        renderer.drawText( pFont, line, x0 + 32.0f, y, 22.0f,
                           bSelected ? Rgba( 255, 255, 255 ) : Rgba( 180, 190, 200 ) );
        const std::string bill = FormatMoney( static_cast<int32_t>( FuelBill( grade, litres ) + 0.5f ) );
        renderer.drawText( pFont, bill.c_str(), x0 + w - 32.0f, y, 22.0f, Rgba( 200, 220, 200 ), eTextAlign::TA_Right );
        y += 54.0f;
    }
    if ( selected == 2 ) {
        // The damage by zone: four bars (front, rear, left, right) and the whole car.
        static const char *const kZones[4] = { "Front", "Rear", "Left", "Right" };
        for ( int32_t zone = 0; zone < 4; zone++ ) {
            const float bx = x0 + 24.0f + 150.0f * static_cast<float>( zone );
            const float fraction = Clamp( repair.Zone[zone] / 65535.0f, 0.0f, 1.0f );
            renderer.drawText( pFont, kZones[zone], bx, y + 6.0f, 16.0f, Rgba( 170, 180, 190 ) );
            renderer.drawRect( Rect2D( bx, y + 28.0f, 130.0f, 10.0f ), Rgba( 50, 56, 64 ) );
            renderer.drawRect( Rect2D( bx, y + 28.0f, 130.0f * fraction, 10.0f ),
                               Rgba( static_cast<uint32_t>( 80.0f + 175.0f * fraction ), static_cast<uint32_t>( 220.0f - 160.0f * fraction ), 60 ) );
        }
        snprintf( line, sizeof( line ), "%s %.0f %%", LocStr( 72, "Damage" ), 100.0f * Clamp( repair.Damage / 65535.0f, 0.0f, 1.0f ) );
        renderer.drawText( pFont, line, x0 + 24.0f, y + 50.0f, 20.0f, Rgba( 220, 220, 220 ) );
    } else {
        snprintf( line, sizeof( line ), "%s: %.1f L", LocStr( 1642, "Fill" ), litres );
        renderer.drawText( pFont, line, x0 + 24.0f, y + 10.0f, 20.0f, Rgba( 220, 220, 220 ) );
    }

    if ( !message.empty() && messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + 24.0f, y0 + h - 84.0f, 18.0f, Rgba( 255, 230, 140 ) );
    }
    // The help bar, in the states of the retail `help` clip (MAIN.vmf, frames 561 /
    // 572 / 583 [DATA]): `gas` and `vpower` show the grade's help text (loc 1638 /
    // 1639), `fill_up` the fill prompt. The repair row has none of them: its prompt
    // is the repair button.
    const char *pHelp = selected == 0 ? LocStr( 1638, "Select basic gasoline" )
                                      : selected == 1 ? LocStr( 1639, "Select special V-POWER gasoline for better performances!" )
                                                      : LocStr( 1865, "Car repair" );
    renderer.drawRect( Rect2D( x0, y0 + h - 52.0f, w, 52.0f ), Rgba( 0, 0, 0, 120 ) );
    renderer.drawText( pFont, pHelp, x0 + 24.0f, y0 + h - 44.0f, 16.0f, Rgba( 225, 225, 225 ) );
    // The button row: the validate button (A / Enter) says what it does, back leaves.
    renderer.drawText( pFont, selected == 2 ? "A / Enter: repair" : "A / Enter: fill up", x0 + 24.0f, y0 + h - 22.0f, 14.0f,
                       Rgba( 150, 200, 255 ) );
    renderer.drawText( pFont, "Up / Down: choose    Esc / B: leave", x0 + w - 24.0f, y0 + h - 22.0f, 14.0f, Rgba( 150, 160, 170 ),
                       eTextAlign::TA_Right );
}
