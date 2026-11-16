#include "shared.h"
#include "tuner_menu.h"

#include "audio/frontend_audio.h"
#include "flash/gs_flash.h"
#include "game/cinematic/cinematic_car.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/renderer_2d.h"
#include "tuner.h"
#include "tuner_parts.h"

#include <cstdio>

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

int32_t TunerMenu::installedIndex() const {
    const PlayerProfile &profile = GetPlayerProfile();
    const auto found = profile.OwnedCarTunerParts.find( carId );
    if ( found == profile.OwnedCarTunerParts.end() ) {
        return -1;
    }
    const auto slot = found->second.Parts.find( "spoiler" );
    if ( slot == found->second.Parts.end() ) {
        return -1;
    }
    // A part the catalog no longer knows reads as stock (the data stays saved;
    // the apply step skips it the same way).
    for ( size_t i = 0; i < parts.size(); i++ ) {
        if ( StringCmpI( parts[i]->Id.c_str(), slot->second.c_str() ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

void TunerMenu::preview( int32_t index ) {
    previewPart = index;
    if ( pCar == nullptr ) {
        return;
    }
    GetTunerParts().applyPart( *pCar, carId, index >= 0 && index < static_cast<int32_t>( parts.size() )
                                                  ? parts[static_cast<size_t>( index )]
                                                  : nullptr );
}

void TunerMenu::open( int32_t newCarId, const std::string &name, CinematicCar *pPreviewCar ) {
    carId = newCarId;
    carName = name;
    pCar = pPreviewCar;
    message.clear();
    messageTimer = 0.0f;
    GetTunerParts().load();
    parts = GetTunerParts().partsForSlot( carId, "spoiler" );
    fittedPart = previewPart = installedIndex();
    // The room car wears what the profile says before the first preview move.
    preview( previewPart );
    OTDU_LOG_INFO( "Tuner: garage for car %d '%s' (%zu spoilers, fitted %d)\n", carId, carName.c_str(), parts.size(),
                   fittedPart );
    PlayFrontEndCue( "mopen" );
}

void TunerMenu::close() {
    // A previewed-but-unbought part goes back to what is fitted (the sticker
    // editor's unbind rule).
    if ( previewPart != fittedPart ) {
        preview( fittedPart );
    }
    carId = -1;
    pCar = nullptr;
    parts.clear();
    previewPart = fittedPart = -1;
}

void TunerMenu::tick( float deltaTime ) {
    if ( carId < 0 ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    // One interactive row (the spoiler): left / right cycle stock <-> parts.
    const int32_t last = static_cast<int32_t>( parts.size() ) - 1;
    if ( last >= 0 ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            preview( previewPart < 0 ? last : previewPart - 1 ); // stock wraps to the last part
            PlayFrontEndCue( "move" );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            preview( previewPart >= last ? -1 : previewPart + 1 ); // the last part wraps to stock
            PlayFrontEndCue( "move" );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        bool bDone = false;
        if ( previewPart == fittedPart ) {
            message = previewPart < 0 ? "The stock spoiler is already fitted" : "This spoiler is already fitted";
        } else {
            const TunerPart *pPart = previewPart >= 0 ? parts[static_cast<size_t>( previewPart )] : nullptr;
            const int32_t price = pPart != nullptr ? pPart->Price : 0; // back to stock is free
            PlayerProfile &profile = GetPlayerProfile();
            if ( profile.Money < price ) {
                message = "Not enough money";
            } else {
                profile.Money -= price;
                TunerCarParts &carParts = profile.OwnedCarTunerParts[carId];
                if ( pPart == nullptr ) {
                    carParts.Parts.erase( "spoiler" );
                    message = "Stock spoiler fitted";
                } else {
                    carParts.Parts["spoiler"] = pPart->Id;
                    message = "Installed: " + pPart->Name;
                }
                fittedPart = previewPart;
                bDone = true;
                OTDU_LOG_INFO( "Tuner: car %d spoiler '%s' for $%d (money now $%d)\n", carId,
                               pPart != nullptr ? pPart->Id.c_str() : "stock", price, profile.Money );
                SaveProfile( "tuner part bought" );
            }
        }
        messageTimer = 3.0f;
        PlayFrontEndCue( bDone ? "valid" : "wrong" );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ||
                gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        close();
        PlayFrontEndCue( "back" );
    }
}

void TunerMenu::draw( Renderer2D &renderer ) {
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
    const float w = bSidePanel ? 460.0f : 800.0f, h = bSidePanel ? 380.0f : 500.0f;
    const float textSize = bSidePanel ? 19.0f : 21.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 26, 28, bSidePanel ? 215 : 235 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 26, 46, 40, 255 ) );
    const std::string title = "Tuner garage - " + ( carName.empty() ? std::string( "your car" ) : carName );
    renderer.drawText( pFont, title.c_str(), x0 + 20.0f, y0 + 10.0f, bSidePanel ? 24.0f : 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                       eTextAlign::TA_Right );
    float y = y0 + 80.0f;
    // The spoiler row: what the left / right keys preview, and what it costs.
    {
        renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 40.0f ), Rgba( 24, 96, 78, 255 ) );
        renderer.drawText( pFont, "Spoiler", x0 + 24.0f, y, textSize, Rgba( 255, 255, 255 ) );
        std::string right;
        uint32_t color = Rgba( 255, 255, 255 );
        if ( parts.empty() ) {
            right = "no parts for this car";
            color = Rgba( 150, 150, 150 );
        } else {
            const TunerPart *pPart = previewPart >= 0 ? parts[static_cast<size_t>( previewPart )] : nullptr;
            right = pPart != nullptr ? pPart->Name : "Stock";
            if ( previewPart == fittedPart ) {
                right += " fitted";
                color = Rgba( 150, 150, 150 );
            } else {
                const int32_t price = pPart != nullptr ? pPart->Price : 0;
                right += price > 0 ? ": " + FormatMoney( price ) : ": free";
                color = profile.Money >= price ? Rgba( 255, 255, 255 ) : Rgba( 255, 110, 110 );
            }
        }
        renderer.drawText( pFont, right.c_str(), x0 + w - 24.0f, y, textSize, color, eTextAlign::TA_Right );
        y += 44.0f;
    }
    // The other slots the garage will sell (nothing in the catalog yet).
    static const char *pLater[] = { "Body kit", "Neon" };
    for ( const char *pSlot : pLater ) {
        renderer.drawText( pFont, pSlot, x0 + 24.0f, y, textSize, Rgba( 120, 120, 120 ) );
        renderer.drawText( pFont, "coming soon", x0 + w - 24.0f, y, textSize, Rgba( 120, 120, 120 ), eTextAlign::TA_Right );
        y += 44.0f;
    }
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 90.0f, textSize, Rgba( 255, 210, 80 ),
                           eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, "Left / Right: spoiler      A / Enter: fit      B / Esc: leave", x0 + w * 0.5f,
                       y0 + h - 44.0f, bSidePanel ? 17.0f : 20.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    renderer.end();
}
