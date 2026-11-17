#include "shared.h"
#include "surgery_menu.h"

#include "audio/frontend_audio.h"
#include "core/xmb.h"
#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "game/avatar/facefx.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"

#include <ctime>

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

// One notch of a slider: the FaceParamArray byte is value / 100 in the FaceFX
// register (facefx.cpp), -100..100 [CODE]; the notch size is the port's [INF].
constexpr int32_t kNotch = 10;
constexpr int32_t kLimit = 100;
// The bandage lasts this long in real seconds [INF: the retail BandageTime
// rule is ENC].
constexpr int64_t kBandageSeconds = 600;
constexpr int32_t kLocBought = 3373; // "Surgery bought"

std::string Loc( int32_t id, const char *pFallback ) {
    const char *pText = gpDatabase != nullptr ? gpDatabase->getString( id ) : nullptr;
    return pText != nullptr && *pText != 0 ? pText : pFallback;
}

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

// "C_Nose_Tall_or_Small" -> "Nose Tall or Small".
std::string PrettyName( std::string name ) {
    if ( name.compare( 0, 2, "C_" ) == 0 ) {
        name.erase( 0, 2 );
    }
    if ( name.compare( 0, 8, "CustPos " ) == 0 ) {
        name.erase( 0, 8 );
    }
    for ( char &c : name ) {
        if ( c == '_' ) {
            c = ' ';
        }
    }
    return name;
}
} // namespace

bool SurgeryMenu::IsBandaged() {
    return GetPlayerProfile().BandageUntil > static_cast<int64_t>( time( nullptr ) );
}

void SurgeryMenu::open( const std::string &newShopName, const int8_t *pBaselineParams, float baselineAge, float baselineFreckle ) {
    shopName = newShopName;
    memcpy( baseParams, pBaselineParams, sizeof( baseParams ) );
    memcpy( params, pBaselineParams, sizeof( params ) );
    baseAge = age = baselineAge;
    baseFreckle = freckle = baselineFreckle;
    rows.clear();
    // DBSurgeryCost: { _cost, _surgname } - the prices are the retail table [DATA].
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBSurgeryCost" ); i++ ) {
        XmbValue entry = root.getArrayElement( "DBSurgeryCost", i );
        const std::string name = entry["_surgname"].asString();
        Row row;
        row.Cost = static_cast<int32_t>( entry["_cost"].asUInt64() );
        if ( name == "FRECKLES" ) {
            row.Kind = 2;
            row.Label = "Freckles";
        } else if ( name == "CustPos Older" ) {
            row.Kind = 1;
            row.Label = "Age";
        } else {
            int32_t found = -1;
            for ( int32_t n = 0; n < 49; n++ ) {
                if ( StringCmpI( kFaceFxCustomNodes[n], name.c_str() ) == 0 ) {
                    found = n;
                    break;
                }
            }
            if ( found < 0 ) {
                continue; // EYE_COLOR / EYE_BROWS_TYPE / TINT: not applied by the avatar yet
            }
            row.Index = found;
            row.Label = PrettyName( name );
        }
        rows.push_back( row );
    }
    selected = 0;
    message.clear();
    messageTimer = 0.0f;
    bOpen = true;
    OTDU_LOG_INFO( "Surgery: '%s' open, %zu row(s) of DBSurgeryCost\n", shopName.c_str(), rows.size() );
    PlayFrontEndCue( "mopen" );
}

void SurgeryMenu::close() {
    bOpen = false;
}

int32_t SurgeryMenu::rowValue( const Row &row ) const {
    if ( row.Kind == 1 ) {
        return static_cast<int32_t>( age * 100.0f + 0.5f );
    }
    if ( row.Kind == 2 ) {
        return static_cast<int32_t>( freckle * 100.0f + 0.5f );
    }
    return params[row.Index];
}

int32_t SurgeryMenu::rowBase( const Row &row ) const {
    if ( row.Kind == 1 ) {
        return static_cast<int32_t>( baseAge * 100.0f + 0.5f );
    }
    if ( row.Kind == 2 ) {
        return static_cast<int32_t>( baseFreckle * 100.0f + 0.5f );
    }
    return baseParams[row.Index];
}

// The movie's TotalPriceStr / NbNotches: every changed row costs its _cost once.
int32_t SurgeryMenu::totalPrice( int32_t *pChanged ) const {
    int32_t total = 0, changed = 0;
    for ( const Row &row : rows ) {
        if ( rowValue( row ) != rowBase( row ) ) {
            total += row.Cost;
            changed++;
        }
    }
    if ( pChanged != nullptr ) {
        *pChanged = changed;
    }
    return total;
}

void SurgeryMenu::buy() {
    int32_t changed = 0;
    const int32_t price = totalPrice( &changed );
    PlayerProfile &profile = GetPlayerProfile();
    if ( changed == 0 ) {
        message = "Nothing changed";
        PlayFrontEndCue( "wrong" );
    } else if ( profile.Money < price ) {
        message = "Not enough money";
        PlayFrontEndCue( "wrong" );
    } else {
        profile.Money -= price;
        profile.FaceParams.assign( params, params + 49 );
        profile.FaceAge = age;
        profile.FaceFreckle = freckle;
        profile.NbSurgeries++;
        profile.BandageUntil = static_cast<int64_t>( time( nullptr ) ) + kBandageSeconds;
        memcpy( baseParams, params, sizeof( baseParams ) );
        baseAge = age;
        baseFreckle = freckle;
        message = Loc( kLocBought, "Surgery bought" );
        PlayFrontEndCue( "valid" );
        OTDU_LOG_INFO( "Surgery: bought %d change(s) for $%d (money now $%d, %d surgeries, bandaged until %lld)\n", changed, price,
                       profile.Money, profile.NbSurgeries, static_cast<long long>( profile.BandageUntil ) );
        SaveProfile( "surgery bought" );
    }
    messageTimer = 3.0f;
}

void SurgeryMenu::tick( float deltaTime ) {
    if ( !bOpen || rows.empty() ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    const int32_t count = static_cast<int32_t>( rows.size() );
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        selected = ( selected + count - 1 ) % count;
        PlayFrontEndCue( "move" );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        selected = ( selected + 1 ) % count;
        PlayFrontEndCue( "move" );
    }
    const int32_t dir = gpInput->wasPressed( eInputAction::IA_MenuRight ) ? 1
                                                                         : ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ? -1 : 0 );
    if ( dir != 0 ) {
        const Row &row = rows[selected];
        if ( row.Kind == 0 ) {
            params[row.Index] = static_cast<int8_t>( Clamp( static_cast<int32_t>( params[row.Index] ) + dir * kNotch, -kLimit, kLimit ) );
        } else if ( row.Kind == 1 ) {
            age = Clamp( age + dir * 0.1f, 0.0f, 1.0f );
        } else {
            freckle = Clamp( freckle + dir * 0.1f, 0.0f, 1.0f );
        }
        PlayFrontEndCue( "move" );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        buy();
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        close();
        PlayFrontEndCue( "back" );
    }
}

void SurgeryMenu::draw( Renderer2D &renderer ) {
    if ( !bOpen ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    const PlayerProfile &profile = GetPlayerProfile();
    renderer.begin( 1280.0f, 720.0f );
    if ( !bSidePanel ) {
        renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, 150 ) );
    }
    const float w = 560.0f, h = 560.0f;
    const float x0 = bSidePanel ? 1280.0f - w - 40.0f : 360.0f, y0 = 80.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 22, 26, 34, 235 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 34, 52, 84, 255 ) );
    renderer.drawText( pFont, shopName.c_str(), x0 + 20.0f, y0 + 10.0f, 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                       eTextAlign::TA_Right );
    const int32_t count = static_cast<int32_t>( rows.size() );
    const int32_t visible = 9;
    const int32_t first = Clamp( selected - visible / 2, 0, Max( 0, count - visible ) );
    float y = y0 + 66.0f;
    for ( int32_t i = first; i < count && i < first + visible; i++ ) {
        const Row &row = rows[i];
        const int32_t value = rowValue( row );
        const bool bChanged = value != rowBase( row );
        if ( i == selected ) {
            renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 38.0f ), Rgba( 40, 70, 130, 255 ) );
        }
        renderer.drawText( pFont, row.Label.c_str(), x0 + 24.0f, y, 22.0f, bChanged ? Rgba( 255, 210, 80 ) : Rgba( 255, 255, 255 ) );
        // The slider bar: -100..100 (face) or 0..100 (age, freckles).
        const float bx = x0 + 280.0f, bw = 170.0f;
        renderer.drawRect( Rect2D( bx, y + 12.0f, bw, 6.0f ), Rgba( 70, 74, 84, 255 ) );
        const float t = row.Kind == 0 ? ( value + kLimit ) / ( 2.0f * kLimit ) : value / 100.0f;
        renderer.drawRect( Rect2D( bx + Clamp( t, 0.0f, 1.0f ) * bw - 4.0f, y + 6.0f, 8.0f, 18.0f ), Rgba( 230, 230, 230, 255 ) );
        renderer.drawText( pFont, FormatMoney( row.Cost ).c_str(), x0 + w - 24.0f, y, 20.0f, Rgba( 190, 190, 190 ),
                           eTextAlign::TA_Right );
        y += 40.0f;
    }
    int32_t changed = 0;
    const int32_t total = totalPrice( &changed );
    const std::string summary = std::to_string( changed ) + " change(s)   Total " + FormatMoney( total );
    renderer.drawText( pFont, summary.c_str(), x0 + w * 0.5f, y0 + h - 118.0f, 24.0f,
                       profile.Money >= total ? Rgba( 255, 255, 255 ) : Rgba( 255, 110, 110 ), eTextAlign::TA_Center );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 84.0f, 24.0f, Rgba( 255, 210, 80 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, "Left / Right: adjust   A / Enter: buy   B / Esc: leave", x0 + w * 0.5f, y0 + h - 40.0f, 18.0f,
                       Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    renderer.end();
}
