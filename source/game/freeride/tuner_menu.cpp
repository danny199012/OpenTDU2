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

int32_t TunerMenu::installedIndex( const SlotRow &row ) const {
    const PlayerProfile &profile = GetPlayerProfile();
    const auto found = profile.OwnedCarTunerParts.find( carId );
    if ( found == profile.OwnedCarTunerParts.end() ) {
        return -1;
    }
    const auto slot = found->second.Parts.find( row.Slot );
    if ( slot == found->second.Parts.end() ) {
        return -1;
    }
    // A part the catalog no longer knows reads as stock (the data stays saved;
    // the apply step skips it the same way).
    for ( size_t i = 0; i < row.Parts.size(); i++ ) {
        if ( StringCmpI( row.Parts[i]->Id.c_str(), slot->second.c_str() ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

int32_t TunerMenu::installedFinish() const {
    const PlayerProfile &profile = GetPlayerProfile();
    const auto found = profile.OwnedCarTunerParts.find( carId );
    if ( found == profile.OwnedCarTunerParts.end() ) {
        return -1;
    }
    const auto finish = found->second.Parts.find( "exhaust_finish" );
    if ( finish == found->second.Parts.end() ) {
        return -1;
    }
    // A finish the catalog no longer knows reads as none (the data stays
    // saved; the apply step skips it the same way).
    const std::vector<TunerFinish> &finishes = GetTunerParts().getFinishes();
    for ( size_t i = 0; i < finishes.size(); i++ ) {
        if ( StringCmpI( finishes[i].Id.c_str(), finish->second.c_str() ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

void TunerMenu::preview( SlotRow &row, int32_t index ) {
    row.Preview = index;
    if ( pCar == nullptr ) {
        return;
    }
    GetTunerParts().applyPart( *pCar, carId, row.Slot.c_str(),
                               index >= 0 && index < static_cast<int32_t>( row.Parts.size() )
                                   ? row.Parts[static_cast<size_t>( index )]
                                   : nullptr );
    // The exhaust row's preview resets the material to the mesh's own, so the
    // finish (the finish row's preview, what you see now) goes back on top.
    if ( row.Slot == "exhaust" ) {
        for ( SlotRow &other : rows ) {
            if ( other.Slot == "exhaust_finish" ) {
                GetTunerParts().applyFinish(
                    *pCar, "exhaust",
                    other.FinishPreview >= 0 &&
                            other.FinishPreview <
                                static_cast<int32_t>( GetTunerParts().getFinishes().size() )
                        ? GetTunerParts().getFinishes()[static_cast<size_t>( other.FinishPreview )]
                              .Id.c_str()
                        : nullptr );
                break;
            }
        }
    }
}

void TunerMenu::previewFinish( SlotRow &row, int32_t index ) {
    row.FinishPreview = index;
    if ( pCar == nullptr ) {
        return;
    }
    const std::vector<TunerFinish> &finishes = GetTunerParts().getFinishes();
    GetTunerParts().applyFinish(
        *pCar, "exhaust",
        index >= 0 && index < static_cast<int32_t>( finishes.size() )
            ? finishes[static_cast<size_t>( index )].Id.c_str()
            : nullptr );
}

void TunerMenu::open( int32_t newCarId, const std::string &name, CinematicCar *pPreviewCar ) {
    carId = newCarId;
    carName = name;
    pCar = pPreviewCar;
    message.clear();
    messageTimer = 0.0f;
    GetTunerParts().load();
    rows.clear();
    selected = 0;
    size_t slotCount = 0;
    const char *const *pSlots = TunerParts::slotNames( slotCount );
    for ( size_t i = 0; i < slotCount; i++ ) {
        SlotRow row;
        row.Slot = pSlots[i];
        row.Parts = GetTunerParts().partsForSlot( carId, pSlots[i] );
        if ( row.Parts.empty() ) {
            continue;
        }
        row.Fitted = row.Preview = installedIndex( row );
        rows.push_back( std::move( row ) );
    }
    // The room car wears what the profile says before the first preview move.
    for ( SlotRow &row : rows ) {
        preview( row, row.Preview );
    }
    // The tip finish sits right under the exhaust row (it colours whatever
    // exhaust part is fitted or previewed).
    for ( size_t i = 0; i < rows.size(); i++ ) {
        if ( rows[i].Slot == "exhaust" ) {
            SlotRow finishRow;
            finishRow.Slot = "exhaust_finish";
            finishRow.FinishFitted = finishRow.FinishPreview = installedFinish();
            rows.insert( rows.begin() + static_cast<ptrdiff_t>( i ) + 1,
                         std::move( finishRow ) );
            break;
        }
    }
    OTDU_LOG_INFO( "Tuner: garage for car %d '%s' (%zu slots)\n", carId, carName.c_str(), rows.size() );
    PlayFrontEndCue( "mopen" );
}

void TunerMenu::close() {
    // A previewed-but-unbought part goes back to what is fitted (the sticker
    // editor's unbind rule). The finish first: the exhaust revert below
    // re-applies whatever the finish row previews.
    for ( SlotRow &row : rows ) {
        if ( row.Slot == "exhaust_finish" && row.FinishPreview != row.FinishFitted ) {
            previewFinish( row, row.FinishFitted );
        }
    }
    for ( SlotRow &row : rows ) {
        if ( row.Preview != row.Fitted ) {
            preview( row, row.Fitted );
        }
    }
    carId = -1;
    pCar = nullptr;
    rows.clear();
}

void TunerMenu::tick( float deltaTime ) {
    if ( carId < 0 ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    if ( rows.empty() ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            close();
            PlayFrontEndCue( "back" );
        }
        return;
    }
    // Up / down pick the slot row, left / right cycle stock <-> parts of it.
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) && selected > 0 ) {
        selected--;
        PlayFrontEndCue( "move" );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) && selected + 1 < rows.size() ) {
        selected++;
        PlayFrontEndCue( "move" );
    }
    SlotRow &row = rows[selected];
    if ( row.Slot == "exhaust_finish" ) {
        // The tip finish: left / right cycle the catalog's finishes on the
        // fitted exhaust (the mesh's own look first), validate saves it - a
        // finish is free, the part's price covered the metal.
        const std::vector<TunerFinish> &finishes = GetTunerParts().getFinishes();
        const int32_t lastFinish = static_cast<int32_t>( finishes.size() ) - 1;
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            previewFinish( row, row.FinishPreview < 0 ? lastFinish
                                                      : row.FinishPreview - 1 );
            PlayFrontEndCue( "move" );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            previewFinish( row, row.FinishPreview >= lastFinish
                                    ? -1
                                    : row.FinishPreview + 1 );
            PlayFrontEndCue( "move" );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            // The stock tips are triangles of the retail body (not our
            // material), so a finish needs a tuner exhaust on the car.
            bool bHasExhaust = false;
            for ( const SlotRow &other : rows ) {
                if ( other.Slot == "exhaust" && other.Fitted >= 0 ) {
                    bHasExhaust = true;
                    break;
                }
            }
            if ( !bHasExhaust ) {
                message = "Fit an exhaust first";
            } else if ( row.FinishPreview == row.FinishFitted ) {
                message = row.FinishPreview < 0
                              ? std::string( "Mesh default already picked" )
                              : "This finish is already picked";
            } else {
                PlayerProfile &profile = GetPlayerProfile();
                TunerCarParts &carParts = profile.OwnedCarTunerParts[carId];
                if ( row.FinishPreview < 0 ) {
                    carParts.Parts.erase( "exhaust_finish" );
                    message = "Tip finish: mesh default";
                } else {
                    carParts.Parts["exhaust_finish"] =
                        finishes[static_cast<size_t>( row.FinishPreview )].Id;
                    message = "Tip finish: " +
                              finishes[static_cast<size_t>( row.FinishPreview )].Name;
                }
                row.FinishFitted = row.FinishPreview;
                OTDU_LOG_INFO( "Tuner: car %d exhaust_finish '%s'\n", carId,
                               row.FinishPreview >= 0
                                   ? finishes[static_cast<size_t>( row.FinishPreview )]
                                         .Id.c_str()
                                   : "(none)" );
                SaveProfile( "tuner finish picked" );
                PlayFrontEndCue( "valid" );
            }
            messageTimer = 3.0f;
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ||
                    gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            close();
            PlayFrontEndCue( "back" );
        }
        return;
    }
    const int32_t last = static_cast<int32_t>( row.Parts.size() ) - 1;
    if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        preview( row, row.Preview < 0 ? last : row.Preview - 1 ); // stock wraps to the last part
        PlayFrontEndCue( "move" );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        preview( row, row.Preview >= last ? -1 : row.Preview + 1 ); // the last part wraps to stock
        PlayFrontEndCue( "move" );
    }
    const char *pLabel = TunerParts::slotLabel( row.Slot.c_str() );
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        bool bDone = false;
        if ( row.Preview == row.Fitted ) {
            message = row.Preview < 0 ? std::string( "Stock " ) + pLabel + " already fitted"
                                      : std::string( "This part is already fitted" );
        } else {
            const TunerPart *pPart = row.Preview >= 0 ? row.Parts[static_cast<size_t>( row.Preview )] : nullptr;
            const int32_t price = pPart != nullptr ? pPart->Price : 0; // back to stock is free
            PlayerProfile &profile = GetPlayerProfile();
            if ( profile.Money < price ) {
                message = "Not enough money";
            } else {
                profile.Money -= price;
                TunerCarParts &carParts = profile.OwnedCarTunerParts[carId];
                if ( pPart == nullptr ) {
                    carParts.Parts.erase( row.Slot );
                    message = std::string( "Stock " ) + pLabel + " fitted";
                } else {
                    carParts.Parts[row.Slot] = pPart->Id;
                    message = "Installed: " + pPart->Name;
                }
                row.Fitted = row.Preview;
                bDone = true;
                OTDU_LOG_INFO( "Tuner: car %d %s '%s' for $%d (money now $%d)\n", carId, row.Slot.c_str(),
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
    const std::string title = shopName + " - " + ( carName.empty() ? std::string( "your car" ) : carName );
    renderer.drawText( pFont, title.c_str(), x0 + 20.0f, y0 + 10.0f, bSidePanel ? 24.0f : 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                       eTextAlign::TA_Right );
    float y = y0 + 80.0f;
    // One row per slot the car takes: what left / right preview, and the price.
    // The finish row under the exhaust row cycles the tip finishes (free).
    for ( size_t i = 0; i < rows.size(); i++ ) {
        const SlotRow &row = rows[i];
        if ( i == selected ) {
            renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 40.0f ), Rgba( 24, 96, 78, 255 ) );
        }
        if ( row.Slot == "exhaust_finish" ) {
            renderer.drawText( pFont, "Tip finish", x0 + 24.0f, y, textSize,
                               Rgba( 255, 255, 255 ) );
            const std::vector<TunerFinish> &finishes = GetTunerParts().getFinishes();
            const std::string right =
                row.FinishPreview >= 0 &&
                        row.FinishPreview < static_cast<int32_t>( finishes.size() )
                    ? finishes[static_cast<size_t>( row.FinishPreview )].Name
                    : std::string( "Mesh default" );
            uint32_t color = Rgba( 255, 255, 255 );
            std::string suffix;
            if ( row.FinishPreview == row.FinishFitted ) {
                suffix = " picked";
                color = Rgba( 150, 150, 150 );
            } else {
                suffix = ": free";
            }
            renderer.drawText( pFont, ( right + suffix ).c_str(), x0 + w - 24.0f, y,
                               textSize, color, eTextAlign::TA_Right );
            y += 44.0f;
            continue;
        }
        renderer.drawText( pFont, TunerParts::slotLabel( row.Slot.c_str() ), x0 + 24.0f, y, textSize,
                           Rgba( 255, 255, 255 ) );
        const TunerPart *pPart = row.Preview >= 0 ? row.Parts[static_cast<size_t>( row.Preview )] : nullptr;
        std::string right = pPart != nullptr ? pPart->Name : "Stock";
        uint32_t color = Rgba( 255, 255, 255 );
        if ( row.Preview == row.Fitted ) {
            right += " fitted";
            color = Rgba( 150, 150, 150 );
        } else {
            const int32_t price = pPart != nullptr ? pPart->Price : 0;
            right += price > 0 ? ": " + FormatMoney( price ) : ": free";
            color = profile.Money >= price ? Rgba( 255, 255, 255 ) : Rgba( 255, 110, 110 );
        }
        renderer.drawText( pFont, right.c_str(), x0 + w - 24.0f, y, textSize, color, eTextAlign::TA_Right );
        y += 44.0f;
    }
    if ( rows.empty() ) {
        renderer.drawText( pFont, "no parts for this car", x0 + 24.0f, y, textSize, Rgba( 150, 150, 150 ) );
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
    renderer.drawText( pFont, "Up / Down: slot    Left / Right: part    A / Enter: fit    B / Esc: leave", x0 + w * 0.5f,
                       y0 + h - 44.0f, bSidePanel ? 17.0f : 20.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    renderer.end();
}
