#include "shared.h"
#include "tuning_menu.h"

#include "flash/gs_flash.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "physics/vehicle_data.h"
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

// The next level's price. The shop pricing table (DBStickersPricing) did not
// name the tuning actions when this was written [DATA: -logshops dumps it];
// a level costs 12,000 more than the previous one [INF].
constexpr int32_t kLevelPrice = 12000;
int32_t NextLevelPrice( int32_t level ) {
    return level < 0 || level >= 3 ? 0 : kLevelPrice * ( level + 1 );
}

// A set of four rims costs this much (the retail pricing table is in the
// encrypted band; a tuning level is $12k / $24k / $36k, a rim set sits below
// the first) [INF]. Going back to the stock rims is free.
constexpr int32_t kRimPrice = 8000;

// "21\" 245/30" - the plus-size tire the rim gets (front axle; the rear keeps
// its own width and gets the same sidewall).
std::string TireLabel( const RimProfile &profile ) {
    if ( !profile.bValid ) {
        return "n/a";
    }
    return std::to_string( profile.DiameterInch ) + "\" " +
           std::to_string( static_cast<int32_t>( std::lround( profile.WidthFront * 1000.0f ) ) ) + "/" +
           std::to_string( static_cast<int32_t>( std::lround( profile.AspectFront ) ) );
}
} // namespace

void TuningMenu::open( int32_t newCarId, const std::string &name ) {
    carId = newCarId;
    carName = name;
    selected = 0;
    message.clear();
    messageTimer = 0.0f;
    rimMin = rimMax = rimPreview = rimInstalled = 0;
    const PlayerProfile &profile = GetPlayerProfile();
    const auto foundTuning = profile.OwnedCarTuning.find( carId );
    const TuningLevels levels = foundTuning != profile.OwnedCarTuning.end() ? foundTuning->second : TuningLevels();
    RimProfile stock;
    int32_t stockDiameter = 0;
    if ( GetStockRimInfo( carId, stock ) ) {
        stockDiameter = stock.StockDiameterInch;
        GetRimSizeRange( carId, rimMin, rimMax );
        const auto foundRim = profile.OwnedCarRimSizes.find( carId );
        rimInstalled = foundRim != profile.OwnedCarRimSizes.end() ? foundRim->second : 0;
        rimPreview = rimInstalled > 0 ? rimInstalled : stockDiameter;
    }
    OTDU_LOG_INFO( "Tuning: shop for car %d '%s' (accel %d, braking %d, speed %d, rims %d\" of %d..%d)\n", carId,
                   carName.c_str(), levels.Accel, levels.Braking, levels.Speed,
                   rimInstalled > 0 ? rimInstalled : stockDiameter, rimMin, rimMax );
}

void TuningMenu::close() {
    carId = -1;
}

void TuningMenu::tick( float deltaTime ) {
    if ( carId < 0 ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        selected = ( selected + 3 ) % 4;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        selected = ( selected + 1 ) % 4;
    }
    // Rim size: left / right preview the shop's range, validate fits it.
    if ( selected == 3 && rimMax > rimMin ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            rimPreview = Max( rimMin, rimPreview - 1 );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            rimPreview = Min( rimMax, rimPreview + 1 );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        if ( selected == 3 ) {
            RimProfile stock;
            GetStockRimInfo( carId, stock );
            if ( rimMin <= 0 || rimMax <= 0 ) {
                message = "No rim sizes for this car";
            } else {
                const int32_t target = rimPreview == stock.StockDiameterInch ? 0 : rimPreview;
                if ( target == rimInstalled ) {
                    message = target == 0 ? "Stock rims already fitted" : "These rims are already fitted";
                } else {
                    const int32_t price = target == 0 ? 0 : kRimPrice;
                    PlayerProfile &profile = GetPlayerProfile();
                    if ( profile.Money < price ) {
                        message = "Not enough money";
                    } else {
                        profile.Money -= price;
                        if ( target == 0 ) {
                            profile.OwnedCarRimSizes.erase( carId );
                        } else {
                            profile.OwnedCarRimSizes[carId] = target;
                        }
                        rimInstalled = target;
                        RimProfile fitted;
                        GetRimProfile( carId, target, fitted );
                        message = target == 0 ? "Stock rims fitted" : "Installed: " + TireLabel( fitted );
                        OTDU_LOG_INFO( "Tuning: car %d rims %d\" (%s) for $%d (money now $%d)\n", carId, target,
                                       TireLabel( fitted ).c_str(), price, profile.Money );
                        SaveProfile( "rim size bought" );
                    }
                }
            }
        } else {
            TuningLevels &levels = GetPlayerProfile().OwnedCarTuning[carId];
            int32_t *pLevel = selected == 0 ? &levels.Accel : selected == 1 ? &levels.Braking : &levels.Speed;
            if ( *pLevel >= 3 ) {
                message = "Already at the maximum level";
            } else {
                PlayerProfile &profile = GetPlayerProfile();
                const int32_t price = NextLevelPrice( *pLevel );
                if ( profile.Money < price ) {
                    message = "Not enough money";
                } else {
                    profile.Money -= price;
                    ( *pLevel )++;
                    message = "Installed: level " + std::to_string( *pLevel );
                    OTDU_LOG_INFO( "Tuning: car %d %s -> level %d for $%d (money now $%d)\n", carId,
                                   selected == 0 ? "accel" : selected == 1 ? "braking" : "speed", *pLevel, price,
                                   profile.Money );
                    SaveProfile( "tuning bought" );
                }
            }
        }
        messageTimer = 3.0f;
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ||
                gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        close();
    }
}

void TuningMenu::draw( Renderer2D &renderer ) {
    if ( carId < 0 ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    const PlayerProfile &profile = GetPlayerProfile();
    static const TuningLevels kNoTuning;
    const auto found = profile.OwnedCarTuning.find( carId );
    const TuningLevels &levels = found != profile.OwnedCarTuning.end() ? found->second : kNoTuning;
    renderer.begin( 1280.0f, 720.0f );
    if ( !bSidePanel ) {
        renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, 150 ) );
    }
    const float x0 = bSidePanel ? 790.0f : 240.0f, y0 = bSidePanel ? 60.0f : 110.0f;
    const float w = bSidePanel ? 460.0f : 800.0f, h = bSidePanel ? 600.0f : 500.0f;
    const float textSize = bSidePanel ? 19.0f : 21.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 22, 28, bSidePanel ? 215 : 235 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 46, 40, 16, 255 ) );
    const std::string title = "Tuning - " + ( carName.empty() ? std::string( "your car" ) : carName );
    renderer.drawText( pFont, title.c_str(), x0 + 20.0f, y0 + 10.0f, bSidePanel ? 24.0f : 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                       eTextAlign::TA_Right );
    float y = y0 + 80.0f;
    const char *pRows[4] = { "Acceleration", "Braking", "Top speed", "Rim size" };
    for ( int32_t row = 0; row < 4; row++ ) {
        if ( row == selected ) {
            renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 40.0f ), Rgba( 120, 90, 20, 255 ) );
        }
        renderer.drawText( pFont, pRows[row], x0 + 24.0f, y, textSize, Rgba( 255, 255, 255 ) );
        std::string right;
        uint32_t color = Rgba( 255, 255, 255 );
        if ( row < 3 ) {
            const int32_t level = row == 0 ? levels.Accel : row == 1 ? levels.Braking : levels.Speed;
            if ( level >= 3 ) {
                right = "Level 3/3 (max)";
                color = Rgba( 150, 150, 150 );
            } else {
                right = "Level " + std::to_string( level ) + " -> " + std::to_string( level + 1) + ": " +
                        FormatMoney( NextLevelPrice( level ) );
                color = profile.Money >= NextLevelPrice( level ) ? Rgba( 255, 255, 255 ) : Rgba( 255, 110, 110 );
            }
        } else if ( rimMin <= 0 || rimMax <= 0 ) {
            right = "n/a";
            color = Rgba( 150, 150, 150 );
        } else {
            RimProfile stock, preview;
            GetStockRimInfo( carId, stock );
            const bool bStock = rimPreview == stock.StockDiameterInch;
            GetRimProfile( carId, bStock ? 0 : rimPreview, preview );
            right = TireLabel( preview );
            if ( bStock ) {
                right += " (stock)";
            }
            const int32_t target = bStock ? 0 : rimPreview;
            if ( target == rimInstalled ) {
                right += " fitted";
                color = Rgba( 150, 150, 150 );
            } else {
                const int32_t price = target == 0 ? 0 : kRimPrice;
                right += price > 0 ? ": " + FormatMoney( price ) : ": free";
                color = profile.Money >= price ? Rgba( 255, 255, 255 ) : Rgba( 255, 110, 110 );
            }
        }
        renderer.drawText( pFont, right.c_str(), x0 + w - 24.0f, y, textSize, color, eTextAlign::TA_Right );
        y += 44.0f;
    }
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 90.0f, textSize, Rgba( 255, 210, 80 ),
                           eTextAlign::TA_Center );
    }
    const char *pHint = selected == 3 ? "Left / Right: rim size      A / Enter: fit      B / Esc: leave"
                                      : "A / Enter: buy the next level      B / Esc: leave";
    renderer.drawText( pFont, pHint, x0 + w * 0.5f, y0 + h - 44.0f, bSidePanel ? 17.0f : 20.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.end();
}
