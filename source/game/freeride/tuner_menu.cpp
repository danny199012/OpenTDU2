#include "shared.h"
#include "tuner_menu.h"

#include "audio/frontend_audio.h"
#include "flash/gs_flash.h"
#include "game/cinematic/cinematic_car.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/font.h"
#include "render/renderer_2d.h"
#include "tuner.h"
#include "tuner_parts.h"

#include <cstdio>

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

uint32_t RgbaF( const Vector3f &c, uint32_t a = 255 ) {
    return Rgba( static_cast<uint32_t>( Clamp( c.x, 0.0f, 1.0f ) * 255.0f + 0.5f ),
                 static_cast<uint32_t>( Clamp( c.y, 0.0f, 1.0f ) * 255.0f + 0.5f ),
                 static_cast<uint32_t>( Clamp( c.z, 0.0f, 1.0f ) * 255.0f + 0.5f ), a );
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

// The garage's neon: the interior is a dark, cyan / magenta lit room.
const uint32_t kCyan = Rgba( 40, 220, 255 );
const uint32_t kMagenta = Rgba( 255, 50, 150 );

const char *FinishRowLabel( const std::string &slot ) {
    return slot == "exhaust" ? "TIP FINISH" : "SPOILER FINISH";
}
} // namespace

size_t TunerMenu::partRowOf( const char *pSlot ) const {
    for ( size_t i = 0; i < rows.size(); i++ ) {
        if ( rows[i].Kind == eKind::Part && rows[i].Slot == pSlot ) {
            return i;
        }
    }
    return rows.size();
}

bool TunerMenu::hasFitted( const char *pSlot ) const {
    const size_t index = partRowOf( pSlot );
    return index < rows.size() && rows[index].Fitted >= 0;
}

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

int32_t TunerMenu::installedFinish( const SlotRow &row ) const {
    const PlayerProfile &profile = GetPlayerProfile();
    const auto found = profile.OwnedCarTunerParts.find( carId );
    if ( found == profile.OwnedCarTunerParts.end() ) {
        return -1;
    }
    const auto finish = found->second.Parts.find( TunerParts::finishKey( row.Slot.c_str() ) );
    if ( finish == found->second.Parts.end() ) {
        return -1;
    }
    // A finish the catalog no longer knows reads as none (the data stays
    // saved; the apply step skips it the same way).
    for ( size_t i = 0; i < row.Finishes.size(); i++ ) {
        if ( StringCmpI( row.Finishes[i]->Id.c_str(), finish->second.c_str() ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

int32_t TunerMenu::installedFlame() const {
    const std::vector<TunerFlame> &flames = GetTunerParts().getFlames();
    const PlayerProfile &profile = GetPlayerProfile();
    const auto found = profile.OwnedCarTunerParts.find( carId );
    if ( found == profile.OwnedCarTunerParts.end() ) {
        return flames.empty() ? -1 : 0;
    }
    const auto style = found->second.Parts.find( "exhaust_flames" );
    if ( style == found->second.Parts.end() || style->second.empty() ) {
        return flames.empty() ? -1 : 0;
    }
    if ( StringCmpI( style->second.c_str(), "off" ) == 0 ) {
        return -1;
    }
    for ( size_t i = 0; i < flames.size(); i++ ) {
        if ( StringCmpI( flames[i].Id.c_str(), style->second.c_str() ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    return flames.empty() ? -1 : 0;
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
    // The part's preview resets the material to the mesh's own, so the slot's
    // finish (the finish row's preview, what you see now) goes back on top.
    for ( SlotRow &other : rows ) {
        if ( other.Kind == eKind::Finish && other.Slot == row.Slot ) {
            GetTunerParts().applyFinish(
                *pCar, row.Slot.c_str(),
                other.OptionPreview >= 0 && other.OptionPreview < static_cast<int32_t>( other.Finishes.size() )
                    ? other.Finishes[static_cast<size_t>( other.OptionPreview )]->Id.c_str()
                    : nullptr );
            break;
        }
    }
}

void TunerMenu::previewFinish( SlotRow &row, int32_t index ) {
    row.OptionPreview = index;
    if ( pCar == nullptr ) {
        return;
    }
    GetTunerParts().applyFinish( *pCar, row.Slot.c_str(),
                                 index >= 0 && index < static_cast<int32_t>( row.Finishes.size() )
                                     ? row.Finishes[static_cast<size_t>( index )]->Id.c_str()
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
    // The body colour the "Painted" finish and its swatch take.
    const uint32_t bodyColour = pCar != nullptr ? pCar->getBodyPaintColour() : 0xffffffffu;
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
        // The finish card sits right under its part card (it dresses whatever
        // part of the slot is fitted or previewed).
        SlotRow finishRow;
        finishRow.Kind = eKind::Finish;
        finishRow.Slot = pSlots[i];
        finishRow.Finishes = GetTunerParts().finishesForSlot( pSlots[i] );
        if ( !finishRow.Finishes.empty() ) {
            for ( const TunerFinish *pFinish : finishRow.Finishes ) {
                finishRow.Swatches.push_back( GetTunerParts().finishSwatch( *pFinish, bodyColour ) );
            }
            finishRow.OptionFitted = finishRow.OptionPreview = installedFinish( finishRow );
            rows.push_back( std::move( finishRow ) );
        }
    }
    // The pop-flame card closes the list, when the car takes an exhaust.
    if ( partRowOf( "exhaust" ) < rows.size() && !GetTunerParts().getFlames().empty() ) {
        SlotRow flameRow;
        flameRow.Kind = eKind::Flames;
        flameRow.Slot = "exhaust";
        for ( const TunerFlame &flame : GetTunerParts().getFlames() ) {
            flameRow.Swatches.push_back( flame.Body );
            flameRow.SwatchCores.push_back( flame.Core );
        }
        flameRow.OptionFitted = flameRow.OptionPreview = installedFlame();
        rows.push_back( std::move( flameRow ) );
    }
    // The room car wears what the profile says before the first preview move.
    for ( SlotRow &row : rows ) {
        if ( row.Kind == eKind::Part ) {
            preview( row, row.Preview );
        }
    }
    OTDU_LOG_INFO( "Tuner: garage for car %d '%s' (%zu rows)\n", carId, carName.c_str(), rows.size() );
    PlayFrontEndCue( "mopen" );
}

void TunerMenu::close() {
    // A previewed-but-unbought part goes back to what is fitted (the sticker
    // editor's unbind rule). The finishes first: the part revert below
    // re-applies whatever the finish row previews.
    for ( SlotRow &row : rows ) {
        if ( row.Kind == eKind::Finish && row.OptionPreview != row.OptionFitted ) {
            previewFinish( row, row.OptionFitted );
        }
    }
    for ( SlotRow &row : rows ) {
        if ( row.Kind == eKind::Part && row.Preview != row.Fitted ) {
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
    // Up / down pick the card, left / right cycle stock <-> options of it.
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) && selected > 0 ) {
        selected--;
        PlayFrontEndCue( "move" );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) && selected + 1 < rows.size() ) {
        selected++;
        PlayFrontEndCue( "move" );
    }
    SlotRow &row = rows[selected];
    if ( row.Kind != eKind::Part ) {
        // A finish / flame card: left / right cycle the options (the first,
        // index -1, is the mesh's own look / flames off), validate saves it -
        // free, the part's price covered the metal.
        const int32_t last = static_cast<int32_t>( row.Swatches.size() ) - 1;
        const bool bFlames = row.Kind == eKind::Flames;
        auto setOption = [&]( int32_t index ) {
            if ( bFlames ) {
                row.OptionPreview = index;
            } else {
                previewFinish( row, index );
            }
        };
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            setOption( row.OptionPreview < 0 ? last : row.OptionPreview - 1 );
            PlayFrontEndCue( "move" );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            setOption( row.OptionPreview >= last ? -1 : row.OptionPreview + 1 );
            PlayFrontEndCue( "move" );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            // The stock tips / spoiler are triangles of the retail body (not
            // our material), so a finish needs a tuner part of the slot, and
            // a flame needs a tuner exhaust.
            if ( !hasFitted( row.Slot.c_str() ) ) {
                message = row.Slot == "exhaust" ? "Fit an exhaust first" : "Fit a spoiler first";
            } else if ( row.OptionPreview == row.OptionFitted ) {
                message = "Already picked";
            } else {
                PlayerProfile &profile = GetPlayerProfile();
                TunerCarParts &carParts = profile.OwnedCarTunerParts[carId];
                if ( bFlames ) {
                    const std::vector<TunerFlame> &flames = GetTunerParts().getFlames();
                    const std::string id =
                        row.OptionPreview < 0 ? std::string( "off" ) : flames[static_cast<size_t>( row.OptionPreview )].Id;
                    carParts.Parts["exhaust_flames"] = id;
                    message = row.OptionPreview < 0
                                  ? std::string( "Pop flames: off" )
                                  : "Pop flames: " + flames[static_cast<size_t>( row.OptionPreview )].Name;
                    OTDU_LOG_INFO( "Tuner: car %d exhaust_flames '%s'\n", carId, id.c_str() );
                } else {
                    const std::string key = TunerParts::finishKey( row.Slot.c_str() );
                    if ( row.OptionPreview < 0 ) {
                        carParts.Parts.erase( key );
                        message = "Finish: mesh default";
                    } else {
                        const TunerFinish &finish = *row.Finishes[static_cast<size_t>( row.OptionPreview )];
                        carParts.Parts[key] = finish.Id;
                        message = "Finish: " + finish.Name;
                    }
                    OTDU_LOG_INFO( "Tuner: car %d %s '%s'\n", carId, key.c_str(),
                                   row.OptionPreview >= 0
                                       ? row.Finishes[static_cast<size_t>( row.OptionPreview )]->Id.c_str()
                                       : "(none)" );
                }
                row.OptionFitted = row.OptionPreview;
                SaveProfile( "tuner finish picked" );
                PlayFrontEndCue( "valid" );
                messageTimer = 3.0f;
                return;
            }
            PlayFrontEndCue( "wrong" );
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
    const float w = bSidePanel ? 470.0f : 560.0f;
    const float textSize = bSidePanel ? 20.0f : 22.0f;
    const float smallSize = 14.0f;
    const float cardGap = 6.0f;
    // Card heights: a part card is two lines; the finish / flame cards carry a
    // swatch strip.
    auto cardHeight = [&]( const SlotRow &row ) { return row.Kind == eKind::Part ? 62.0f : 86.0f; };
    float contentH = 0.0f;
    for ( const SlotRow &row : rows ) {
        contentH += cardHeight( row ) + cardGap;
    }
    if ( rows.empty() ) {
        contentH = 50.0f;
    }
    const float headerH = 70.0f, footerH = 92.0f;
    const float h = headerH + 12.0f + contentH + footerH;
    const float x0 = bSidePanel ? 1280.0f - w - 24.0f : ( 1280.0f - w ) * 0.5f;
    const float y0 = Max( 16.0f, ( 720.0f - h ) * 0.5f );

    // Panel: dark glass with a neon edge.
    renderer.drawGradient( Rect2D( x0, y0, w, h ), Rgba( 16, 20, 28, bSidePanel ? 222 : 240 ),
                           Rgba( 8, 10, 15, bSidePanel ? 232 : 248 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 1.0f, Rgba( 70, 80, 96, 255 ) );
    // Header: shop name, the car, the wallet.
    renderer.drawGradient( Rect2D( x0, y0, w, headerH ), Rgba( 26, 34, 48, 255 ), Rgba( 14, 18, 26, 255 ) );
    renderer.drawRect( Rect2D( x0, y0 + headerH - 3.0f, w * 0.5f, 3.0f ), kCyan );
    renderer.drawRect( Rect2D( x0 + w * 0.5f, y0 + headerH - 3.0f, w * 0.5f, 3.0f ), kMagenta );
    renderer.drawText( pFont, shopName.c_str(), x0 + 20.0f, y0 + 8.0f, bSidePanel ? 26.0f : 30.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, carName.empty() ? "your car" : carName.c_str(), x0 + 20.0f, y0 + 40.0f, 16.0f,
                       Rgba( 150, 165, 185 ) );
    {
        const std::string money = FormatMoney( profile.Money );
        const float mw = pFont->measureText( money.c_str(), 22.0f ) + 24.0f;
        renderer.drawRect( Rect2D( x0 + w - mw - 16.0f, y0 + 14.0f, mw, 34.0f ), Rgba( 20, 60, 36, 255 ) );
        renderer.drawRectOutline( Rect2D( x0 + w - mw - 16.0f, y0 + 14.0f, mw, 34.0f ), 1.0f, Rgba( 90, 220, 120 ) );
        renderer.drawText( pFont, money.c_str(), x0 + w - 28.0f, y0 + 19.0f, 22.0f, Rgba( 140, 255, 160 ),
                           eTextAlign::TA_Right );
    }

    float y = y0 + headerH + 12.0f;
    const float cx = x0 + 12.0f, cw = w - 24.0f;
    // A small pill at the right of a card: a status / price.
    auto pill = [&]( float rightX, float topY, const std::string &text, uint32_t fill, uint32_t line, uint32_t ink ) {
        const float pw = pFont->measureText( text.c_str(), 16.0f ) + 20.0f;
        renderer.drawRect( Rect2D( rightX - pw, topY, pw, 24.0f ), fill );
        renderer.drawRectOutline( Rect2D( rightX - pw, topY, pw, 24.0f ), 1.0f, line );
        renderer.drawText( pFont, text.c_str(), rightX - pw * 0.5f, topY + 3.0f, 16.0f, ink, eTextAlign::TA_Center );
    };
    for ( size_t i = 0; i < rows.size(); i++ ) {
        const SlotRow &row = rows[i];
        const float ch = cardHeight( row );
        const bool bSel = i == selected;
        // Card body: the selected one is lit and has the neon bar.
        if ( bSel ) {
            renderer.drawGradient( Rect2D( cx, y, cw, ch ), Rgba( 34, 52, 70, 255 ), Rgba( 22, 34, 48, 255 ) );
            renderer.drawRectOutline( Rect2D( cx, y, cw, ch ), 1.0f, Rgba( 80, 150, 190, 255 ) );
            renderer.drawRect( Rect2D( cx, y, 4.0f, ch ), kCyan );
        } else {
            renderer.drawRect( Rect2D( cx, y, cw, ch ), Rgba( 24, 28, 36, 210 ) );
            renderer.drawRect( Rect2D( cx, y, 3.0f, ch ), Rgba( 56, 64, 80 ) );
        }
        const float tx = cx + 18.0f, rx = cx + cw - 14.0f;
        const uint32_t labelColor = bSel ? kCyan : Rgba( 120, 135, 155 );
        if ( row.Kind == eKind::Part ) {
            renderer.drawText( pFont, TunerParts::slotLabel( row.Slot.c_str() ), tx, y + 6.0f, smallSize, labelColor,
                               eTextAlign::TA_Left, 1.5f );
            const TunerPart *pPart = row.Preview >= 0 ? row.Parts[static_cast<size_t>( row.Preview )] : nullptr;
            renderer.drawText( pFont, pPart != nullptr ? pPart->Name.c_str() : "Stock", tx, y + 25.0f, textSize + 2.0f,
                               Rgba( 255, 255, 255 ) );
            if ( row.Preview == row.Fitted ) {
                pill( rx, y + 8.0f, pPart != nullptr ? "FITTED" : "STOCK", Rgba( 26, 56, 40 ), Rgba( 70, 170, 100 ),
                      Rgba( 140, 240, 160 ) );
            } else {
                const int32_t price = pPart != nullptr ? pPart->Price : 0;
                const bool bAfford = profile.Money >= price;
                pill( rx, y + 8.0f, price > 0 ? FormatMoney( price ) : std::string( "FREE" ),
                      bAfford ? Rgba( 52, 46, 18 ) : Rgba( 64, 24, 24 ), bAfford ? Rgba( 220, 190, 70 ) : Rgba( 200, 80, 80 ),
                      bAfford ? Rgba( 255, 225, 110 ) : Rgba( 255, 130, 130 ) );
            }
            // "3 / 7": where the previewed part sits in the catalog (stock is 0).
            char page[24];
            snprintf( page, sizeof( page ), "< %d / %d >", row.Preview + 1, static_cast<int>( row.Parts.size() ) + 1 );
            renderer.drawText( pFont, page, rx, y + 38.0f, smallSize, bSel ? Rgba( 190, 205, 225 ) : Rgba( 110, 120, 138 ),
                               eTextAlign::TA_Right );
            y += ch + cardGap;
            continue;
        }
        // Finish / flame cards.
        const bool bFlames = row.Kind == eKind::Flames;
        const bool bUsable = hasFitted( row.Slot.c_str() );
        renderer.drawText( pFont, bFlames ? "POP FLAMES" : FinishRowLabel( row.Slot ), tx, y + 6.0f, smallSize, labelColor,
                           eTextAlign::TA_Left, 1.5f );
        std::string name;
        if ( bFlames ) {
            name = row.OptionPreview >= 0 ? GetTunerParts().getFlames()[static_cast<size_t>( row.OptionPreview )].Name
                                          : std::string( "Off" );
        } else {
            name = row.OptionPreview >= 0 ? row.Finishes[static_cast<size_t>( row.OptionPreview )]->Name
                                          : std::string( "Mesh default" );
        }
        renderer.drawText( pFont, name.c_str(), tx, y + 25.0f, textSize, bUsable ? Rgba( 255, 255, 255 ) : Rgba( 130, 135, 145 ) );
        if ( !bUsable ) {
            pill( rx, y + 8.0f, bFlames || row.Slot == "exhaust" ? "NEEDS EXHAUST" : "NEEDS SPOILER", Rgba( 36, 38, 44 ),
                  Rgba( 90, 96, 108 ), Rgba( 150, 156, 168 ) );
        } else if ( row.OptionPreview == row.OptionFitted ) {
            pill( rx, y + 8.0f, "PICKED", Rgba( 26, 56, 40 ), Rgba( 70, 170, 100 ), Rgba( 140, 240, 160 ) );
        } else {
            pill( rx, y + 8.0f, "FREE", Rgba( 52, 46, 18 ), Rgba( 220, 190, 70 ), Rgba( 255, 225, 110 ) );
        }
        // The swatch strip: option -1 (mesh default / flames off) first, then
        // every option; the previewed one is ringed, the picked one marked.
        // Shrinks to fit the card when a slot has many finishes (the spoiler's
        // eleven plus the "none" chip).
        const float chipGap = 6.0f, chipH = 18.0f;
        const float chipCount = static_cast<float>( row.Swatches.size() + 1 );
        const float chipW = Min( 30.0f, ( cw - 36.0f - chipGap * ( chipCount - 1.0f ) ) / chipCount );
        const float chipY = y + ch - chipH - 10.0f;
        float chipX = tx;
        for ( int32_t option = -1; option < static_cast<int32_t>( row.Swatches.size() ); option++ ) {
            const Rect2D chip( chipX, chipY, chipW, chipH );
            if ( option < 0 ) {
                renderer.drawRect( chip, Rgba( 14, 16, 20 ) );
                renderer.drawRect( Rect2D( chipX + 6.0f, chipY + chipH * 0.5f - 1.0f, chipW - 12.0f, 2.0f ),
                                   Rgba( 110, 118, 130 ) ); // the "none" dash
            } else if ( bFlames ) {
                renderer.drawGradient( chip, RgbaF( row.SwatchCores[static_cast<size_t>( option )] ),
                                       RgbaF( row.Swatches[static_cast<size_t>( option )] ) );
            } else {
                renderer.drawRect( chip, RgbaF( row.Swatches[static_cast<size_t>( option )] ) );
                // a little sheen so a flat colour reads as a finish, not a box
                renderer.drawGradient( Rect2D( chipX, chipY, chipW, chipH * 0.5f ), Rgba( 255, 255, 255, 46 ),
                                       Rgba( 255, 255, 255, 0 ) );
            }
            renderer.drawRectOutline( chip, 1.0f, Rgba( 60, 66, 78 ) );
            if ( option == row.OptionPreview ) {
                renderer.drawRectOutline( Rect2D( chipX - 3.0f, chipY - 3.0f, chipW + 6.0f, chipH + 6.0f ), 2.0f,
                                          bSel ? Rgba( 255, 255, 255 ) : Rgba( 170, 176, 188 ) );
            }
            if ( option == row.OptionFitted ) {
                renderer.drawRect( Rect2D( chipX + 2.0f, chipY + chipH + 5.0f, chipW - 4.0f, 2.0f ), Rgba( 90, 220, 120 ) );
            }
            chipX += chipW + chipGap;
        }
        y += ch + cardGap;
    }
    if ( rows.empty() ) {
        renderer.drawText( pFont, "no parts for this car", x0 + 24.0f, y + 6.0f, textSize, Rgba( 150, 150, 150 ) );
        y += 50.0f;
    }

    // Footer: the toast, the coming-soon line, the key hints.
    const float fy = y0 + h - footerH;
    if ( messageTimer > 0.0f ) {
        const float mw = pFont->measureText( message.c_str(), textSize ) + 36.0f;
        renderer.drawRect( Rect2D( x0 + w * 0.5f - mw * 0.5f, fy + 2.0f, mw, 30.0f ), Rgba( 60, 48, 12, 240 ) );
        renderer.drawRectOutline( Rect2D( x0 + w * 0.5f - mw * 0.5f, fy + 2.0f, mw, 30.0f ), 1.0f, Rgba( 230, 190, 60 ) );
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, fy + 5.0f, textSize - 2.0f, Rgba( 255, 214, 90 ),
                           eTextAlign::TA_Center );
    } else {
        renderer.drawText( pFont, "Body kit  -  Neon: coming soon", x0 + w * 0.5f, fy + 6.0f, 15.0f, Rgba( 100, 108, 122 ),
                           eTextAlign::TA_Center );
    }
    // Key hints as keycaps.
    struct Hint {
        const char *Key;
        const char *Action;
    };
    static const Hint kHints[] = { { "Up/Dn", "Card" }, { "L/R", "Change" }, { "A/Enter", "Fit" }, { "B/Esc", "Leave" } };
    float totalW = 0.0f;
    for ( const Hint &hint : kHints ) {
        totalW += pFont->measureText( hint.Key, 14.0f ) + 16.0f + 6.0f + pFont->measureText( hint.Action, 15.0f ) + 16.0f;
    }
    float hx = x0 + ( w - totalW ) * 0.5f;
    const float hy = fy + 46.0f;
    for ( const Hint &hint : kHints ) {
        const float kw = pFont->measureText( hint.Key, 14.0f ) + 16.0f;
        renderer.drawRect( Rect2D( hx, hy, kw, 24.0f ), Rgba( 44, 50, 62 ) );
        renderer.drawRectOutline( Rect2D( hx, hy, kw, 24.0f ), 1.0f, Rgba( 120, 130, 148 ) );
        renderer.drawText( pFont, hint.Key, hx + kw * 0.5f, hy + 4.0f, 14.0f, Rgba( 235, 240, 250 ), eTextAlign::TA_Center );
        hx += kw + 6.0f;
        renderer.drawText( pFont, hint.Action, hx, hy + 4.0f, 15.0f, Rgba( 170, 180, 196 ) );
        hx += pFont->measureText( hint.Action, 15.0f ) + 16.0f;
    }
    renderer.end();
}
