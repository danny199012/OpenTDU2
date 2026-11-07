#include "shared.h"
#include "dealer_menu.h"

#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "game/cheats.h"
#include "game/player_profile.h"
#include "game/skill_levels.h"
#include "input/gs_input.h"
#include "render/font.h"
#include "render/renderer_2d.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

std::string Format( const char *pFormat, ... ) {
    char buffer[256];
    va_list args;
    va_start( args, pFormat );
    vsnprintf( buffer, sizeof( buffer ), pFormat, args );
    va_end( args );
    return buffer;
}

std::string FormatFloat( float value ) { return Format( "%.1f", value ); }

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

bool Contains( const std::string &text, const char *pPart ) {
    return text.find( pPart ) != std::string::npos;
}

// A DB text with an English fallback (the dealer menu is native, the texts of
// the retail one are the movie's).
std::string LocOr( int32_t id, const char *pFallback ) {
    const char *pText = gpDatabase != nullptr ? gpDatabase->getString( id ) : nullptr;
    return pText != nullptr && *pText != '\0' ? std::string( pText ) : std::string( pFallback );
}

// A stat bar of the spec panel (the retail car screens: dark track, amber
// fill, the value in a dark chip at the left).
void DrawStatBar( Renderer2D &renderer, const Font *pFont, float x, float y, float w, float h, int32_t percent ) {
    renderer.drawRect( Rect2D( x, y, w, h ), Rgba( 28, 30, 34, 235 ) );
    const float fillW = w * static_cast<float>( Clamp( percent, 0, 100 ) ) / 100.0f;
    if ( fillW >= 1.0f ) {
        renderer.drawGradient( Rect2D( x, y, fillW, h ), Rgba( 255, 170, 50, 255 ), Rgba( 210, 90, 20, 255 ) );
    }
    renderer.drawText( pFont, std::to_string( Clamp( percent, 0, 100 ) ).c_str(), x - 14.0f, y - 5.0f, 20.0f,
                       Rgba( 255, 190, 90 ), eTextAlign::TA_Right );
}

// One difficulty wheel (5 of them, lit up to the car's rating): a 8-segment
// disc, no wheel art exists in the frontend banks.
void DrawWheelDisc( Renderer2D &renderer, float cx, float cy, float radius, bool bLit ) {
    constexpr int32_t kSegments = 8;
    Vertex2D vertices[kSegments * 3];
    const uint32_t color = bLit ? Rgba( 255, 190, 60, 255 ) : Rgba( 74, 76, 82, 255 );
    for ( int32_t i = 0; i < kSegments; i++ ) {
        const float a0 = static_cast<float>( i ) * 6.2831853f / kSegments;
        const float a1 = static_cast<float>( i + 1 ) * 6.2831853f / kSegments;
        vertices[i * 3 + 0] = { cx, cy, 0.0f, 0.0f, color };
        vertices[i * 3 + 1] = { cx + std::cos( a0 ) * radius, cy + std::sin( a0 ) * radius, 0.0f, 0.0f, color };
        vertices[i * 3 + 2] = { cx + std::cos( a1 ) * radius, cy + std::sin( a1 ) * radius, 0.0f, 0.0f, color };
    }
    renderer.drawTriangles( nullptr, vertices, kSegments * 3 );
}
} // namespace

const CarShop *FindShopForSpot( const std::string &spotName, const std::string &island ) {
    // [INF] the building name picks the shop by its DBCarShop label; Hawai uses
    // the last shop of that label (the second set of the table).
    const char *pLabel = "Used Car Dealer";
    if ( Contains( spotName, "AUDI" ) ) {
        pLabel = "Audi";
    } else if ( Contains( spotName, "FERR" ) ) {
        pLabel = "Ferrari";
    } else if ( Contains( spotName, "MERC" ) ) {
        pLabel = "Mercedes-Benz";
    } else if ( Contains( spotName, "ECDC_ITA" ) ) {
        pLabel = "Italian Classic";
    } else if ( Contains( spotName, "ECDC_EUR" ) ) {
        pLabel = "European Classic";
    } else if ( Contains( spotName, "ECDC_WLD" ) ) {
        pLabel = "World classic";
    } else if ( Contains( spotName, "ITAL" ) ) {
        pLabel = "Italian Cars";
    } else if ( Contains( spotName, "EUR" ) ) {
        pLabel = "European Cars";
    } else if ( Contains( spotName, "US" ) ) {
        pLabel = "U.S. Cars";
    } else if ( Contains( spotName, "ASIA" ) ) {
        pLabel = "Asian Cars";
    } else if ( Contains( spotName, "BRI" ) ) {
        pLabel = "British Cars";
    }
    const CarShop *pFound = nullptr;
    for ( const CarShop &shop : GetCarShops() ) {
        if ( shop.Cars.empty() || shop.Name.compare( 0, strlen( pLabel ), pLabel ) != 0 ) {
            continue;
        }
        if ( pFound == nullptr || island == "hawai" ) {
            pFound = &shop;
        }
        if ( island != "hawai" ) {
            break;
        }
    }
    return pFound != nullptr ? pFound : FindCarShop( 0 );
}

void DealerMenu::open( const CarShop *pNewShop ) {
    pShop = pNewShop;
    selected = 0;
    step = eStep::Cars;
    selectedEntry = eEntry::Buy;
    paints.clear();
    selectedPaint = 0;
    bPreviewDirty = true;
    bTestDrive = false;
    testDrive = DealerTestDrive();
    bCompareValid = false;
    compareName.clear();
    compareSpec = CarSpec();
    comparePrice = 0;
    message.clear();
    messageTimer = 0.0f;
}

void DealerMenu::close() {
    pShop = nullptr;
    step = eStep::Cars;
}

const ShopCar *DealerMenu::selectedCar() const {
    if ( pShop == nullptr || selected < 0 || static_cast<size_t>( selected ) >= pShop->Cars.size() ) {
        return nullptr;
    }
    return &pShop->Cars[static_cast<size_t>( selected )];
}

bool DealerMenu::takePreview( int32_t &outCarId, int32_t &outPaintIndex ) {
    if ( !bPreviewDirty || pShop == nullptr || selected < 0 || static_cast<size_t>( selected ) >= pShop->Cars.size() ) {
        return false;
    }
    bPreviewDirty = false;
    outCarId = pShop->Cars[selected].CarId;
    outPaintIndex = step == eStep::Colors ? selectedPaint : -1;
    return true;
}

bool DealerMenu::takeTestDrive( DealerTestDrive &out ) {
    if ( !bTestDrive ) {
        return false;
    }
    bTestDrive = false;
    out = testDrive;
    return out.CarId >= 0;
}

bool DealerMenu::canBuySelected() {
    const ShopCar &car = pShop->Cars[selected];
    const PlayerProfile &profile = GetPlayerProfile();
    if ( std::find( profile.OwnedCars.begin(), profile.OwnedCars.end(), car.CarId ) != profile.OwnedCars.end() ) {
        message = "You already own this car";
    } else if ( !car.bBuyable && !Cheats::isAllCarsBuyable() ) {
        message = "This car is not for sale";
    } else if ( profile.Money < car.Price ) {
        message = "Not enough money";
    } else {
        return true;
    }
    messageTimer = 3.0f;
    return false;
}

void DealerMenu::chooseColour() {
    if ( pShop == nullptr || selected < 0 || static_cast<size_t>( selected ) >= pShop->Cars.size() || !canBuySelected() ) {
        return;
    }
    // The car's catalogue (DBCarsAndColors rows, paint_spec.md 1) [DATA]; a car
    // without rows is bought in the paint its model carries.
    paints = CinematicCar::DatabasePaints( pShop->Cars[selected].CarId );
    if ( paints.empty() ) {
        buySelected( -1 );
        return;
    }
    step = eStep::Colors;
    selectedPaint = 0;
    bPreviewDirty = true;
}

bool DealerMenu::takeBoughtCar( int32_t &outCarId ) {
    if ( boughtCarId < 0 ) {
        return false;
    }
    outCarId = boughtCarId;
    boughtCarId = -1;
    return true;
}

void DealerMenu::buySelected( int32_t paintIndex ) {
    if ( pShop == nullptr || selected < 0 || static_cast<size_t>( selected ) >= pShop->Cars.size() || !canBuySelected() ) {
        return;
    }
    const ShopCar &car = pShop->Cars[selected];
    PlayerProfile &profile = GetPlayerProfile();
    // A colour's _price is added to the car's [INF: most rows are 0].
    const int32_t paintPrice =
        paintIndex >= 0 && static_cast<size_t>( paintIndex ) < paints.size() ? paints[paintIndex].Price : 0;
    const int32_t total = car.Price + paintPrice;
    if ( profile.Money < total ) {
        message = "Not enough money";
    } else {
        profile.Money -= total;
        profile.OwnedCars.push_back( car.CarId );
        if ( paintIndex >= 0 ) {
            profile.OwnedCarPaints[car.CarId] = paintIndex;
        }
        profile.NbCars++;
        boughtCarId = car.CarId; // the caller parks it outside (takeBoughtCar)
        const std::string colour = paintIndex >= 0 && static_cast<size_t>( paintIndex ) < paints.size()
                                       ? " in " + paints[paintIndex].Name
                                       : std::string();
        message = "Bought: " + car.Brand + " " + car.Model + colour;
        step = eStep::Cars;
        bPreviewDirty = true;
        OTDU_LOG_INFO( "Dealer: bought '%s' paint %d '%s' for $%d (money now $%d, %d cars)\n", car.FileName.c_str(),
                       paintIndex, colour.c_str(), total, profile.Money, profile.NbCars );
        // Vehicles bought: DBRewardXP COLLECTION_CARS_DIF_<n> at 1, 3, 6, 10, 15 ... cars [DATA].
        static const int32_t kThresholds[] = { 1, 3, 6, 10, 15, 20, 25, 30, 40, 50 };
        for ( const int32_t threshold : kThresholds ) {
            if ( profile.NbCars >= threshold ) {
                AwardSkillEvent( ( "COLLECTION_CARS_DIF_" + std::to_string( threshold ) ).c_str() );
            }
        }
        SaveProfile( "car bought" );
    }
    messageTimer = 3.0f;
}

void DealerMenu::recordCompare() {
    // The retail Compare (FUN_00b95840): the selected car becomes the record
    // the next cars are shown against [CODE].
    const ShopCar *pCar = selectedCar();
    if ( pCar == nullptr || !FillCarSpec( pCar->PhysicsRow, compareSpec ) ) {
        return;
    }
    bCompareValid = true;
    compareName = pCar->Brand + " " + pCar->Model;
    comparePrice = pCar->Price;
    message = "Comparing: " + compareName + " - select another car";
    messageTimer = 4.0f;
    OTDU_LOG_INFO( "Dealer: compare record = %s (speed %d accel %d braking %d handling %d, %d km/h, %.2f s, %d bhp)\n",
                   compareName.c_str(), CarStatGauge( compareSpec.MaxSpeed, kSpeedGauge ),
                   CarStatGauge( compareSpec.Accel, kAccelGauge ), CarStatGauge( compareSpec.Braking, kBrakingGauge ),
                   CarStatGauge( compareSpec.Handling, kHandlingGauge ),
                   static_cast<int32_t>( compareSpec.MaxSpeed ), compareSpec.Accel, compareSpec.Power );
}

void DealerMenu::validateEntry() {
    switch ( selectedEntry ) {
    case eEntry::Buy:
        chooseColour(); // the colour step (buys there; a car without rows buys directly)
        break;
    case eEntry::EnterCar:
        // The car from its podium camera (the preview is already driven by the
        // selection); the panel steps aside until B.
        step = eStep::View;
        break;
    case eEntry::Data:
        step = eStep::Data;
        break;
    case eEntry::Compare:
        recordCompare();
        step = eStep::Cars; // the next selection is shown against the record
        break;
    case eEntry::TestDrive: {
        // The caller fills the TestDriveRequest and switches to GM_TestCar
        // (the freeride dealer returns to FREERIDE, the story shop to SHOP).
        // The car in its default rims; the colour step's choice when one was
        // picked before (the retail drive uses the slot record's colour).
        if ( const ShopCar *pCar = selectedCar() ) {
            testDrive = DealerTestDrive();
            testDrive.CarId = pCar->CarId;
            CarSpec spec;
            testDrive.RimsHashcode = FillCarSpec( pCar->PhysicsRow, spec ) ? spec.DefaultRims : 0;
            testDrive.PaintIndex = paints.empty() ? -1 : selectedPaint;
            if ( testDrive.PaintIndex >= 0 ) {
                testDrive.ColorId = CinematicCar::DatabasePaintColorId( pCar->CarId, testDrive.PaintIndex );
            }
            OTDU_LOG_INFO( "Dealer: test drive of '%s' requested (paint %d '%s')\n", pCar->FileName.c_str(),
                           testDrive.PaintIndex, testDrive.ColorId.c_str() );
            bTestDrive = true;
        }
        break;
    }
    default:
        break;
    }
}

void DealerMenu::tick( float deltaTime ) {
    if ( pShop == nullptr ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    const bool bBack = gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause );
    const int32_t count = static_cast<int32_t>( pShop->Cars.size() );
    switch ( step ) {
    case eStep::Colors: {
        // The colour step: up / down pick a paint (the showroom car is
        // repainted live, takePreview), validate buys, back returns to the
        // entries.
        const int32_t paintsCount = static_cast<int32_t>( paints.size() );
        if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            selectedPaint = ( selectedPaint + paintsCount - 1 ) % paintsCount;
            bPreviewDirty = true;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            selectedPaint = ( selectedPaint + 1 ) % paintsCount;
            bPreviewDirty = true;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            buySelected( selectedPaint );
        } else if ( bBack ) {
            step = eStep::Entries;
            bPreviewDirty = true;
        }
        break;
    }
    case eStep::Entries: {
        // The retail mini menu (Buy / Get into the car / Data / Compare /
        // Test drive).
        const int32_t entries = static_cast<int32_t>( eEntry::Count );
        if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            selectedEntry = static_cast<eEntry>( ( static_cast<int32_t>( selectedEntry ) + entries - 1 ) % entries );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            selectedEntry = static_cast<eEntry>( ( static_cast<int32_t>( selectedEntry ) + 1 ) % entries );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            validateEntry();
        } else if ( bBack ) {
            step = eStep::Cars;
        }
        break;
    }
    case eStep::Data:
    case eStep::View:
        if ( bBack ) {
            step = eStep::Entries;
        }
        break;
    case eStep::Cars:
    default:
        if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            selected = ( selected + count - 1 ) % count;
            bPreviewDirty = true;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            selected = ( selected + 1 ) % count;
            bPreviewDirty = true;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            step = eStep::Entries;
            selectedEntry = eEntry::Buy;
        } else if ( bBack ) {
            close();
        }
        break;
    }
}

void DealerMenu::drawSpecPanel( Renderer2D &renderer, const Font *pFont, float x, float y, float w, const ShopCar &car,
                                const CarSpec &spec, bool bWithCompare ) const {
    // The retail spec panel (the used-car-dealer screen): the name and price,
    // three stat bars, the difficulty wheels and the three numbers.
    float ry = y;
    renderer.drawText( pFont, ( car.Brand + " " + car.Model ).c_str(), x + 24.0f, ry, 26.0f, Rgba( 255, 255, 255 ) );
    ry += 36.0f;
    renderer.drawText( pFont, FormatMoney( car.Price ).c_str(), x + w - 24.0f, ry - 26.0f, 22.0f, Rgba( 255, 210, 80 ),
                       eTextAlign::TA_Right );
    if ( bWithCompare && bCompareValid ) {
        renderer.drawText( pFont, ( "vs " + compareName ).c_str(), x + 24.0f, ry, 17.0f, Rgba( 165, 165, 170 ) );
    }
    ry += 24.0f;
    struct GaugeRow {
        const char *pLabel;
        int32_t percent;
        int32_t comparePercent;
    };
    const GaugeRow rows[3] = {
        { "Acceleration", CarStatGauge( spec.Accel, kAccelGauge ),
          bWithCompare && bCompareValid ? CarStatGauge( compareSpec.Accel, kAccelGauge ) : -1 },
        { "Speed", CarStatGauge( spec.MaxSpeed, kSpeedGauge ),
          bWithCompare && bCompareValid ? CarStatGauge( compareSpec.MaxSpeed, kSpeedGauge ) : -1 },
        { "Braking", CarStatGauge( spec.Braking, kBrakingGauge ),
          bWithCompare && bCompareValid ? CarStatGauge( compareSpec.Braking, kBrakingGauge ) : -1 },
    };
    float barW = Max( 120.0f, w - 96.0f - 24.0f -
                              Max( pFont->measureText( "Acceleration", 20.0f ),
                                   Max( pFont->measureText( "Speed", 20.0f ),
                                        pFont->measureText( "Braking", 20.0f ) ) ) -
                              14.0f );
    const bool bCompare = bWithCompare && bCompareValid;
    if ( bCompare ) {
        barW = Max( 120.0f, barW - 34.0f ); // room for the base value after the bar
    }
    for ( const GaugeRow &row : rows ) {
        DrawStatBar( renderer, pFont, x + 96.0f, ry, barW, 20.0f, row.percent );
        renderer.drawText( pFont, row.pLabel, x + w - 24.0f, ry - 2.0f, 20.0f, Rgba( 235, 235, 235 ),
                           eTextAlign::TA_Right );
        if ( row.comparePercent >= 0 ) {
            renderer.drawText( pFont, std::to_string( row.comparePercent ).c_str(), x + 96.0f + barW + 10.0f, ry - 2.0f,
                               16.0f, Rgba( 165, 165, 170 ) );
        }
        ry += 38.0f;
    }
    // Difficulty (the 5 wheels, lit up to the rating).
    renderer.drawText( pFont, "Difficulty", x + w - 24.0f, ry, 20.0f, Rgba( 235, 235, 235 ), eTextAlign::TA_Right );
    ry += 30.0f;
    for ( int32_t i = 0; i < 5; i++ ) {
        DrawWheelDisc( renderer, x + 106.0f + static_cast<float>( i ) * 28.0f, ry + 9.0f, 9.0f, i < spec.Difficulty );
    }
    if ( bCompare ) {
        // The base rating under the wheels (the column right of the bars
        // would run into the row labels).
        for ( int32_t i = 0; i < 5; i++ ) {
            DrawWheelDisc( renderer, x + 106.0f + static_cast<float>( i ) * 28.0f, ry + 31.0f, 5.0f,
                           i < compareSpec.Difficulty );
        }
    }
    ry += bCompare ? 58.0f : 44.0f;
    // The three numbers (top speed / 0-100 / power).
    struct NumberRow {
        std::string text;
        std::string compareText;
    };
    NumberRow numbers[3] = {
        { std::to_string( static_cast<int32_t>( spec.MaxSpeed ) ) + " km/h",
          bWithCompare && bCompareValid ? std::to_string( static_cast<int32_t>( compareSpec.MaxSpeed ) ) + " km/h" : "" },
        { FormatFloat( spec.Accel ) + " sec.",
          bWithCompare && bCompareValid ? FormatFloat( compareSpec.Accel ) + " sec." : "" },
        { std::to_string( spec.Power ) + " bhp",
          bWithCompare && bCompareValid ? std::to_string( compareSpec.Power ) + " bhp" : "" },
    };
    for ( const NumberRow &row : numbers ) {
        renderer.drawText( pFont, row.text.c_str(), x + 96.0f, ry, 22.0f, Rgba( 255, 255, 255 ) );
        if ( !row.compareText.empty() ) {
            renderer.drawText( pFont, row.compareText.c_str(), x + w - 24.0f, ry, 16.0f, Rgba( 165, 165, 170 ),
                               eTextAlign::TA_Right );
        }
        ry += 34.0f;
    }
}

void DealerMenu::drawEntryList( Renderer2D &renderer, const Font *pFont ) const {
    // The retail mini menu, bottom centre (the used-car-dealer screen).
    static const char *kEntryNames[] = { "Buy", "Get into the car", "Data", "Compare", "Test drive" };
    const float w = 460.0f, rowH = 40.0f;
    const float x = 640.0f - w * 0.5f;
    float y = 372.0f;
    for ( int32_t i = 0; i < static_cast<int32_t>( eEntry::Count ); i++ ) {
        const bool bSelected = static_cast<int32_t>( selectedEntry ) == i;
        renderer.drawRect( Rect2D( x, y, w, rowH ), bSelected ? Rgba( 150, 30, 36, 235 ) : Rgba( 12, 13, 16, 190 ) );
        if ( !bSelected ) {
            renderer.drawRectOutline( Rect2D( x, y, w, rowH ), 1.0f, Rgba( 120, 120, 124, 160 ) );
        }
        renderer.drawText( pFont, kEntryNames[i], 640.0f, y + 8.0f, 23.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
        y += rowH + 6.0f;
    }
    renderer.drawText( pFont, "B / Esc: back", 640.0f, y + 6.0f, 19.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
}

void DealerMenu::drawDataSheet( Renderer2D &renderer, const Font *pFont, float x, float y, float w, float h,
                                const ShopCar &car, const CarSpec &spec ) const {
    // The full data sheet (the retail Data page fields, gm_shop's
    // fillCarVariables reads the same DBCarPhysicsData columns). The compare
    // record gets its own column.
    const bool bWithCompare = bCompareValid;
    const float colValue = x + 250.0f;
    const float colCompare = x + 470.0f;
    renderer.drawText( pFont, ( car.Brand + " " + car.Model ).c_str(), x + 24.0f, y + 12.0f, 26.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( car.Price ).c_str(), x + w - 24.0f, y + 16.0f, 22.0f, Rgba( 255, 210, 80 ),
                       eTextAlign::TA_Right );
    if ( bWithCompare ) {
        renderer.drawText( pFont, compareName.c_str(), colCompare, y + 16.0f, 17.0f, Rgba( 165, 165, 170 ) );
    }
    float ry = y + 60.0f;
    auto row = [&]( const char *pKey, const std::string &value, const std::string &compareValue = std::string() ) {
        renderer.drawText( pFont, pKey, x + 24.0f, ry, 19.0f, Rgba( 170, 172, 178 ) );
        renderer.drawText( pFont, value.c_str(), colValue, ry, 19.0f, Rgba( 255, 255, 255 ) );
        if ( bWithCompare && !compareValue.empty() ) {
            renderer.drawText( pFont, compareValue.c_str(), colCompare, ry, 17.0f, Rgba( 165, 165, 170 ) );
        }
        ry += 28.0f;
    };
    // The texts are the DB ids gm_shop's fillCarVariables uses (0x443 bhp,
    // 0x434 Nm, 0x2e8 rpm, 0x438 kg, 0x2e7 cm3, 0x2e2 seconds, 0x1342 gears).
    const std::string bhp = LocOr( 0x443, "bhp" );
    const std::string nm = LocOr( 0x434, "Nm" );
    const std::string rpm = LocOr( 0x2e8, "rpm" );
    const std::string kg = LocOr( 0x438, "kg" );
    const std::string cc = LocOr( 0x2e7, "cm3" );
    const std::string sec = LocOr( 0x2e2, "s" );
    const std::string gearsText = LocOr( 0x1342, "gears" );
    auto compareOf = [&]( const std::string &value ) { return bWithCompare ? value : std::string(); };
    row( "Power",
         Format( "%d %s @%d %s", spec.Power, bhp.c_str(), spec.PowerRpm, rpm.c_str() ),
         compareOf( Format( "%d %s @%d %s", compareSpec.Power, bhp.c_str(), compareSpec.PowerRpm, rpm.c_str() ) ) );
    row( "Torque",
         Format( "%d %s @%d %s", spec.Torque, nm.c_str(), spec.TorqueRpm, rpm.c_str() ),
         compareOf( Format( "%d %s @%d %s", compareSpec.Torque, nm.c_str(), compareSpec.TorqueRpm, rpm.c_str() ) ) );
    row( "Weight", Format( "%d %s", spec.Mass, kg.c_str() ),
         compareOf( Format( "%d %s", compareSpec.Mass, kg.c_str() ) ) );
    row( "Displacement", Format( "%d %s", spec.Displacement, cc.c_str() ),
         compareOf( Format( "%d %s", compareSpec.Displacement, cc.c_str() ) ) );
    // _engine_type -> DB text (gm_shop's jump table) [CODE].
    static const std::pair<int32_t, int32_t> kEngineTypes[] = { { 1, 0x7f0 },  { 2, 0x7e5 },  { 3, 0x7ef },  { 4, 0x7ed },
                                                                { 5, 0x7e4 },  { 7, 0x7ec },  { 8, 0x7e7 },  { 9, 0x7e8 },
                                                                { 10, 0x7ee }, { 11, 0x7eb }, { 12, 0x7e6 }, { 13, 0x7e3 },
                                                                { 14, 0x7ea }, { 15, 0x1fb6 }, { 16, 0x7e9 }, { 17, 0x1fb3 } };
    int32_t engineText = 0x7e2;
    for ( const auto &entry : kEngineTypes ) {
        if ( entry.first == spec.EngineType ) {
            engineText = entry.second;
        }
    }
    row( "Engine", LocOr( engineText, "n/a" ) );
    const int32_t gearboxId = spec.Gearbox == 2 ? 0x80d : spec.Gearbox == 3 ? 0x80c : 0x80a;
    row( "Gearbox", LocOr( gearboxId, "n/a" ) );
    row( "Gears", Format( "%d %s", spec.NbGears, gearsText.c_str() ),
         compareOf( Format( "%d %s", compareSpec.NbGears, gearsText.c_str() ) ) );
    row( "0-100 km/h", Format( "%.2f %s", spec.Accel, sec.c_str() ),
         compareOf( Format( "%.2f %s", compareSpec.Accel, sec.c_str() ) ) );
    row( "Top speed", Format( "%d km/h", static_cast<int32_t>( spec.MaxSpeed ) ),
         compareOf( Format( "%d km/h", static_cast<int32_t>( compareSpec.MaxSpeed ) ) ) );
    row( "Braking 100-0", Format( "%.1f m", spec.Braking ),
         compareOf( Format( "%.1f m", compareSpec.Braking ) ) );
    if ( spec.QuarterMile > 0.0f || ( bWithCompare && compareSpec.QuarterMile > 0.0f ) ) {
        row( "Quarter mile",
             spec.QuarterMile > 0.0f ? Format( "%.1f %s", spec.QuarterMile, sec.c_str() ) : std::string( "n/a" ),
             compareOf( compareSpec.QuarterMile > 0.0f ? Format( "%.1f %s", compareSpec.QuarterMile, sec.c_str() )
                                                       : std::string( "n/a" ) ) );
    }
    row( "L / W / H",
         Format( "%d / %d / %d mm", spec.Length, spec.Width, spec.Height ),
         compareOf( Format( "%d / %d / %d mm", compareSpec.Length, compareSpec.Width, compareSpec.Height ) ) );
}

void DealerMenu::draw( Renderer2D &renderer ) {
    if ( pShop == nullptr ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    const PlayerProfile &profile = GetPlayerProfile();
    renderer.begin( 1280.0f, 720.0f );
    // In a showroom the scene stays clear (the car being shown is the point);
    // elsewhere the screen dims under the panels.
    if ( !bSidePanel ) {
        renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, 150 ) );
    }
    const ShopCar *pCar = selectedCar();
    if ( step == eStep::Colors ) {
        // The colour step: the car, then its catalogue (scrolled: up to 24 rows).
        const float x0 = bSidePanel ? 790.0f : 240.0f, y0 = bSidePanel ? 60.0f : 110.0f;
        const float w = bSidePanel ? 460.0f : 800.0f, h = bSidePanel ? 600.0f : 500.0f;
        const float textSize = bSidePanel ? 20.0f : 22.0f;
        renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 22, 28, bSidePanel ? 215 : 235 ) );
        renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
        renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 60, 20, 24, 255 ) );
        renderer.drawText( pFont, pShop->Name.c_str(), x0 + 20.0f, y0 + 10.0f, bSidePanel ? 24.0f : 28.0f,
                           Rgba( 255, 255, 255 ) );
        const std::string money = FormatMoney( profile.Money );
        renderer.drawText( pFont, money.c_str(), x0 + w - 20.0f, y0 + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                           eTextAlign::TA_Right );
        float y = y0 + 70.0f;
        if ( pCar != nullptr ) {
            const std::string title = pCar->Brand + " " + pCar->Model + " - " + FormatMoney( pCar->Price );
            renderer.drawText( pFont, title.c_str(), x0 + 20.0f, y, textSize, Rgba( 255, 210, 80 ) );
        }
        y += 40.0f;
        const int32_t paintCount = static_cast<int32_t>( paints.size() );
        const int32_t visible = static_cast<int32_t>( ( y0 + h - 110.0f - y ) / 40.0f );
        const int32_t first = Clamp( selectedPaint - visible / 2, 0, Max( 0, paintCount - visible ) );
        for ( int32_t i = first; i < paintCount && i < first + visible; i++ ) {
            if ( i == selectedPaint ) {
                renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 36.0f ), Rgba( 150, 30, 36, 255 ) );
            }
            renderer.drawText( pFont, paints[i].Name.c_str(), x0 + 24.0f, y, textSize, Rgba( 255, 255, 255 ) );
            const std::string extra =
                paints[i].Price > 0 ? "+" + FormatMoney( paints[i].Price ) : std::string( "included" );
            renderer.drawText( pFont, extra.c_str(), x0 + w - 24.0f, y, textSize, Rgba( 200, 200, 200 ),
                               eTextAlign::TA_Right );
            y += 40.0f;
        }
        if ( messageTimer > 0.0f ) {
            renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 90.0f, textSize, Rgba( 255, 210, 80 ),
                               eTextAlign::TA_Center );
        }
        renderer.drawText( pFont, "A / Enter: buy in this colour      B / Esc: back", x0 + w * 0.5f, y0 + h - 44.0f,
                           bSidePanel ? 17.0f : 20.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
        renderer.end();
        return;
    }
    if ( step == eStep::Entries || step == eStep::View ) {
        // The retail mini menu (the spec panel stays up beside it in the
        // showroom; the 'View' step is the car alone with the hint bar).
        if ( pCar != nullptr && step == eStep::Entries ) {
            CarSpec spec;
            if ( FillCarSpec( pCar->PhysicsRow, spec ) ) {
                drawSpecPanel( renderer, pFont, 930.0f, 150.0f, 330.0f, *pCar, spec, false );
            }
        }
        if ( step == eStep::Entries ) {
            drawEntryList( renderer, pFont );
        } else {
            renderer.drawText( pFont, pCar != nullptr ? ( pCar->Brand + " " + pCar->Model ).c_str() : "", 640.0f, 40.0f,
                               28.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
            renderer.drawText( pFont, "B / Esc: back", 640.0f, 660.0f, 20.0f, Rgba( 210, 210, 210 ),
                               eTextAlign::TA_Center );
        }
        renderer.end();
        return;
    }
    if ( step == eStep::Data ) {
        // The full data sheet (the retail Data page), centred.
        if ( pCar != nullptr ) {
            const float x0 = 300.0f, y0 = 90.0f, w = 680.0f, h = 560.0f;
            renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 22, 28, 240 ) );
            renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
            CarSpec spec;
            if ( FillCarSpec( pCar->PhysicsRow, spec ) ) {
                drawDataSheet( renderer, pFont, x0, y0, w, h, *pCar, spec );
            }
            renderer.drawText( pFont, "B / Esc: back", x0 + w * 0.5f, y0 + h - 36.0f, 19.0f, Rgba( 210, 210, 210 ),
                               eTextAlign::TA_Center );
        }
        renderer.end();
        return;
    }
    // The car list with the spec panel beside it (the retail car-dealer
    // screen: the list at the left, the selected car's data at the right).
    {
        const float listX = 60.0f, listY = 120.0f, listW = 560.0f, listH = 480.0f;
        const float textSize = 22.0f;
        renderer.drawRect( Rect2D( listX, listY, listW, listH ), Rgba( 18, 22, 28, bSidePanel ? 200 : 235 ) );
        renderer.drawRectOutline( Rect2D( listX, listY, listW, listH ), 2.0f, Rgba( 200, 200, 200, 255 ) );
        renderer.drawRect( Rect2D( listX, listY, listW, 52.0f ), Rgba( 60, 20, 24, 255 ) );
        renderer.drawText( pFont, pShop->Name.c_str(), listX + 20.0f, listY + 10.0f, 28.0f, Rgba( 255, 255, 255 ) );
        const std::string money = FormatMoney( profile.Money );
        renderer.drawText( pFont, money.c_str(), listX + listW - 20.0f, listY + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                           eTextAlign::TA_Right );
        float y = listY + 70.0f;
        for ( size_t i = 0; i < pShop->Cars.size(); i++ ) {
            const ShopCar &car = pShop->Cars[i];
            const bool bSelectedCar = static_cast<int32_t>( i ) == selected;
            const bool bOwned =
                std::find( profile.OwnedCars.begin(), profile.OwnedCars.end(), car.CarId ) != profile.OwnedCars.end();
            if ( bSelectedCar ) {
                renderer.drawRect( Rect2D( listX + 10.0f, y - 4.0f, listW - 20.0f, 40.0f ), Rgba( 150, 30, 36, 255 ) );
            }
            const std::string name = car.Brand + " " + car.Model;
            renderer.drawText( pFont, name.c_str(), listX + 24.0f, y, textSize, bOwned ? Rgba( 150, 150, 150 )
                                                                                       : Rgba( 255, 255, 255 ) );
            const std::string price = bOwned ? std::string( "Owned" ) : FormatMoney( car.Price );
            const uint32_t priceColor =
                bOwned ? Rgba( 150, 150, 150 ) : ( profile.Money >= car.Price ? Rgba( 255, 255, 255 ) : Rgba( 255, 110, 110 ) );
            renderer.drawText( pFont, price.c_str(), listX + listW - 24.0f, y, textSize, priceColor,
                               eTextAlign::TA_Right );
            y += 44.0f;
        }
        if ( messageTimer > 0.0f ) {
            renderer.drawText( pFont, message.c_str(), listX + listW * 0.5f, listY + listH - 46.0f, textSize,
                               Rgba( 255, 210, 80 ), eTextAlign::TA_Center );
        }
        renderer.drawText( pFont, "A / Enter: open      B / Esc: leave", listX + listW * 0.5f, listY + listH - 44.0f -
                                                                                                ( messageTimer > 0.0f ? 36.0f : 0.0f ),
                           bSidePanel ? 17.0f : 20.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    }
    if ( pCar != nullptr ) {
        CarSpec spec;
        if ( FillCarSpec( pCar->PhysicsRow, spec ) ) {
            drawSpecPanel( renderer, pFont, 660.0f, 150.0f, 560.0f, *pCar, spec, true );
        }
    }
    renderer.end();
}
