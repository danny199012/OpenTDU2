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

// The recolour palette of the monochrome packs (the retail editor recolours
// its monochrome stickers [INF: DBStickersBanks._monochrome]; the colours are
// the port's palette - white first = as shipped).
const uint32_t kTintPalette[] = {
    Rgba( 255, 255, 255 ), Rgba( 30, 30, 30 ),   Rgba( 200, 200, 200 ), Rgba( 220, 40, 40 ),
    Rgba( 240, 130, 20 ),  Rgba( 240, 220, 40 ), Rgba( 60, 180, 60 ),   Rgba( 40, 90, 220 ),
    Rgba( 130, 60, 200 ),  Rgba( 240, 100, 180 ), Rgba( 60, 220, 220 ),
};

// The grid of the sticker page: 6 columns of thumbnails.
constexpr int32_t kGridColumns = 6;

// The angle wrap of the editor's rotation dial (-180..180).
float WrapAngle( float deg ) {
    while ( deg > 180.0f ) {
        deg -= 360.0f;
    }
    while ( deg < -180.0f ) {
        deg += 360.0f;
    }
    return deg;
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
    page = Page::Paint;
    state = StickerState::BankList;
    bankSelected = 0;
    editIndex = -1;
    soldBanks.clear();
    if ( StickerBankCatalog::isEnabled() ) {
        StickerBankCatalog::get().load();
        for ( size_t i = 0; i < StickerBankCatalog::get().getBankCount(); i++ ) {
            if ( StickerBankCatalog::get().isBankSold( i ) ) {
                soldBanks.push_back( i );
            }
        }
    }
    OTDU_LOG_INFO( "Paint: shop for car %d '%s': %zu catalogue row(s), %zu sticker pack(s)\n", carId, carName.c_str(),
                   paints.size(), soldBanks.size() );
}

void PaintMenu::close() {
    carId = -1;
    paints.clear();
    editIndex = -1;
}

void PaintMenu::setStickerContext( StickerCanvas *pCanvas, int32_t newStickerCarId ) {
    pStickerCanvas = pCanvas;
    stickerCarId = newStickerCarId;
}

std::vector<StickerPlacement> &PaintMenu::stickerLayout() {
    static std::vector<StickerPlacement> empty;
    if ( stickerCarId < 0 ) {
        return empty;
    }
    return GetPlayerProfile().OwnedCarStickers[stickerCarId];
}

GPUTexture *PaintMenu::artOf( const StickerPlacement &placement ) const {
    return StickerArt( placement );
}

void PaintMenu::resolveArt( const std::vector<StickerPlacement> &layout, std::vector<GPUTexture *> &outArt ) const {
    StickerResolveArt( layout, outArt );
}

void PaintMenu::pushLayoutToCanvas() {
    if ( pStickerCanvas == nullptr ) {
        return;
    }
    std::vector<GPUTexture *> art;
    resolveArt( stickerLayout(), art );
    pStickerCanvas->setLayout( stickerLayout(), art );
}

void PaintMenu::saveStickers( const char *pReason ) {
    if ( stickerCarId < 0 ) {
        return;
    }
    SaveProfile( pReason );
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
    if ( page == Page::Stickers && StickerBankCatalog::isEnabled() ) {
        tickStickers( deltaTime );
        return;
    }
    const int32_t count = static_cast<int32_t>( paints.size() );
    if ( count == 0 ) {
        // No DBCarsAndColors rows: nothing to sell this car. Right may still
        // reach the sticker page (a car without paint rows can take stickers).
        if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) && !soldBanks.empty() ) {
            page = Page::Stickers;
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ||
                    gpInput->wasPressed( eInputAction::IA_MenuValidate ) ||
                    gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            close();
        }
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        page = Page::Stickers;
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

void PaintMenu::tickStickers( float deltaTime ) {
    PlayerProfile &profile = GetPlayerProfile();
    std::vector<StickerPlacement> &layout = stickerLayout();
    switch ( state ) {
    case StickerState::BankList: {
        if ( soldBanks.empty() ) {
            if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuLeft ) ||
                 gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
                page = Page::Paint;
            }
            return;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            page = Page::Paint;
            return;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            bankSelected = ( bankSelected + soldBanks.size() - 1 ) % soldBanks.size();
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            bankSelected = ( bankSelected + 1 ) % soldBanks.size();
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            StickerBankCatalog &catalog = StickerBankCatalog::get();
            if ( catalog.openBank( soldBanks[bankSelected] ) != nullptr ) {
                gridSelected = 0;
                state = StickerState::Grid;
            } else {
                message = "This pack is not readable";
                messageTimer = 3.0f;
            }
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            close();
        }
        return;
    }
    case StickerState::Grid: {
        StickerBankCatalog &catalog = StickerBankCatalog::get();
        const std::vector<StickerEntry> *pEntries = catalog.openBank( soldBanks[bankSelected] );
        const int32_t count = pEntries != nullptr ? static_cast<int32_t>( pEntries->size() ) : 0;
        if ( count <= 0 ) {
            state = StickerState::BankList;
            return;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            gridSelected = static_cast<size_t>( ( static_cast<int32_t>( gridSelected ) + count - kGridColumns ) % count );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            gridSelected = static_cast<size_t>( ( static_cast<int32_t>( gridSelected ) + kGridColumns ) % count );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            gridSelected = static_cast<size_t>( ( static_cast<int32_t>( gridSelected ) + count - 1 ) % count );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            gridSelected = static_cast<size_t>( ( static_cast<int32_t>( gridSelected ) + 1 ) % count );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            // A new placement, dead centre until the player moves it.
            StickerPlacement placement;
            placement.Bank = catalog.getBank( soldBanks[bankSelected] ).BankFile;
            placement.Index = static_cast<int32_t>( gridSelected );
            placement.Tint = kTintPalette[0];
            layout.push_back( placement );
            editIndex = static_cast<int32_t>( layout.size() ) - 1;
            editBackup = placement;
            bEditNew = true;
            tintIndex = 0;
            pushLayoutToCanvas();
            state = StickerState::Place;
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            state = StickerState::BankList;
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            close();
        }
        return;
    }
    case StickerState::Place: {
        if ( editIndex < 0 || editIndex >= static_cast<int32_t>( layout.size() ) ) {
            state = StickerState::Placed;
            return;
        }
        StickerBankCatalog &catalog = StickerBankCatalog::get();
        const StickerBankInfo *pBankInfo = nullptr;
        for ( size_t i = 0; i < catalog.getBankCount(); i++ ) {
            if ( StringCmpI( catalog.getBank( i ).BankFile.c_str(), layout[static_cast<size_t>( editIndex )].Bank.c_str() ) == 0 ) {
                pBankInfo = &catalog.getBank( i );
                break;
            }
        }
        StickerPlacement &p = layout[static_cast<size_t>( editIndex )];
        // Move (held): 0.35 canvas/s, a fifth of it with Start (the fine dial).
        static constexpr float kMove = 0.35f, kFine = 0.20f, kScale = 0.30f, kRotate = 140.0f;
        const float moveScale = gpInput->isDown( eInputAction::IA_MenuPause ) ? kFine : 1.0f;
        if ( gpInput->isDown( eInputAction::IA_MenuLeft ) ) {
            p.U = Clamp( p.U - kMove * moveScale * deltaTime, 0.02f, 0.98f );
        }
        if ( gpInput->isDown( eInputAction::IA_MenuRight ) ) {
            p.U = Clamp( p.U + kMove * moveScale * deltaTime, 0.02f, 0.98f );
        }
        if ( gpInput->isDown( eInputAction::IA_MenuUp ) ) {
            p.V = Clamp( p.V - kMove * moveScale * deltaTime, 0.02f, 0.98f );
        }
        if ( gpInput->isDown( eInputAction::IA_MenuDown ) ) {
            p.V = Clamp( p.V + kMove * moveScale * deltaTime, 0.02f, 0.98f );
        }
        // Scale (the accessories wheel's left / right: numpad 4 / 6, D-pad on
        // a pad while the menu is up) and rotate (up / down).
        if ( gpInput->isDown( eInputAction::IA_AccessoriesLeft ) ) {
            p.Scale = Clamp( p.Scale - kScale * deltaTime, 0.06f, 1.0f );
        }
        if ( gpInput->isDown( eInputAction::IA_AccessoriesRight ) ) {
            p.Scale = Clamp( p.Scale + kScale * deltaTime, 0.06f, 1.0f );
        }
        if ( gpInput->isDown( eInputAction::IA_AccessoriesUp ) ) {
            p.AngleDeg = WrapAngle( p.AngleDeg - kRotate * deltaTime );
        }
        if ( gpInput->isDown( eInputAction::IA_AccessoriesDown ) ) {
            p.AngleDeg = WrapAngle( p.AngleDeg + kRotate * deltaTime );
        }
        // The monochrome packs recolour (Option1 / X); the colour packs keep
        // their art.
        if ( gpInput->wasPressed( eInputAction::IA_MenuOption1 ) && pBankInfo != nullptr && pBankInfo->bMonochrome ) {
            tintIndex = ( tintIndex + 1 ) % ( sizeof( kTintPalette ) / sizeof( kTintPalette[0] ) );
            p.Tint = kTintPalette[tintIndex];
        }
        pushLayoutToCanvas();
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            if ( bEditNew ) {
                const int32_t price = pBankInfo != nullptr ? pBankInfo->Price : 0;
                if ( profile.Money < price ) {
                    // Cannot pay: drop the sticker back out.
                    layout.erase( layout.begin() + editIndex );
                    pushLayoutToCanvas();
                    editIndex = -1;
                    message = "Not enough money";
                    messageTimer = 3.0f;
                    state = StickerState::Placed;
                    return;
                }
                profile.Money -= price;
                message = "Sticker bought: " + FormatMoney( price );
                OTDU_LOG_INFO( "Paint: car %d sticker '%s' #%d bought for $%d (money now $%d)\n", carId, p.Bank.c_str(),
                               p.Index, price, profile.Money );
            } else {
                message = "Sticker moved";
            }
            messageTimer = 2.5f;
            placedSelected = static_cast<size_t>( editIndex );
            editIndex = -1;
            saveStickers( "sticker bought" );
            state = StickerState::Placed;
            return;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            // Cancel: a new sticker is dropped, an owned one reverts.
            if ( bEditNew ) {
                layout.erase( layout.begin() + editIndex );
            } else {
                layout[static_cast<size_t>( editIndex )] = editBackup;
            }
            pushLayoutToCanvas();
            editIndex = -1;
            state = StickerState::Placed;
            return;
        }
        return;
    }
    case StickerState::Placed: {
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            page = Page::Paint;
            return;
        }
        if ( layout.empty() ) {
            if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
                state = StickerState::BankList;
            }
            return;
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            placedSelected = ( placedSelected + layout.size() - 1 ) % layout.size();
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            placedSelected = ( placedSelected + 1 ) % layout.size();
        }
        placedSelected = Min( placedSelected, layout.size() - 1 );
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            editIndex = static_cast<int32_t>( placedSelected );
            editBackup = layout[placedSelected];
            bEditNew = false;
            for ( size_t t = 0; t < sizeof( kTintPalette ) / sizeof( kTintPalette[0] ); t++ ) {
                if ( kTintPalette[t] == layout[placedSelected].Tint ) {
                    tintIndex = t;
                    break;
                }
            }
            state = StickerState::Place;
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuOption2 ) ) {
            // Remove a placed sticker (no refund; the retail respray rule).
            OTDU_LOG_INFO( "Paint: car %d sticker '%s' #%d removed\n", carId, layout[placedSelected].Bank.c_str(),
                           layout[placedSelected].Index );
            layout.erase( layout.begin() + static_cast<int32_t>( placedSelected ) );
            if ( !layout.empty() ) {
                placedSelected = Min( placedSelected, layout.size() - 1 );
            }
            pushLayoutToCanvas();
            saveStickers( "sticker removed" );
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            state = StickerState::BankList;
        }
        return;
    }
    }
}

void PaintMenu::drawStickers( Renderer2D &renderer, float x0, float y0, float w, float h, float textSize ) {
    const Font *pFont = gpFlash->getTitleFont();
    const PlayerProfile &profile = GetPlayerProfile();
    const float pad = 20.0f;
    float y = y0 + 64.0f;
    renderer.drawText( pFont, "Left: paint page", x0 + pad, y0 + 34.0f, 15.0f, Rgba( 150, 170, 190 ) );
    switch ( state ) {
    case StickerState::BankList: {
        renderer.drawText( pFont, "Sticker packs", x0 + pad, y, textSize + 4.0f, Rgba( 255, 255, 255 ) );
        y += 34.0f;
        if ( soldBanks.empty() ) {
            renderer.drawText( pFont, "This shop has no packs.", x0 + pad, y, textSize, Rgba( 200, 200, 200 ) );
            break;
        }
        StickerBankCatalog &catalog = StickerBankCatalog::get();
        const int32_t visible = static_cast<int32_t>( ( y0 + h - 110.0f - y ) / 34.0f );
        const int32_t first = Clamp( static_cast<int32_t>( bankSelected ) - visible / 2, 0,
                                     Max( 0, static_cast<int32_t>( soldBanks.size() ) - visible ) );
        for ( int32_t i = first; i < static_cast<int32_t>( soldBanks.size() ) && i < first + visible; i++ ) {
            const StickerBankInfo &info = catalog.getBank( soldBanks[static_cast<size_t>( i )] );
            const bool bSel = static_cast<size_t>( i ) == bankSelected;
            if ( bSel ) {
                renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 30.0f ), Rgba( 20, 70, 96, 255 ) );
            }
            renderer.drawText( pFont, info.Name.c_str(), x0 + 24.0f, y, textSize, Rgba( 255, 255, 255 ) );
            const std::string extra = info.bMonochrome ? FormatMoney( info.Price ) + "  (recolourable)"
                                                       : FormatMoney( info.Price );
            renderer.drawText( pFont, extra.c_str(), x0 + w - 24.0f, y, textSize, Rgba( 200, 200, 200 ), eTextAlign::TA_Right );
            y += 34.0f;
        }
        break;
    }
    case StickerState::Grid: {
        StickerBankCatalog &catalog = StickerBankCatalog::get();
        const StickerBankInfo &info = catalog.getBank( soldBanks[bankSelected] );
        const std::vector<StickerEntry> *pEntries = catalog.openBank( soldBanks[bankSelected] );
        renderer.drawText( pFont, info.Name.c_str(), x0 + pad, y, textSize + 4.0f, Rgba( 255, 255, 255 ) );
        renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 24.0f, y, textSize, Rgba( 120, 255, 120 ),
                           eTextAlign::TA_Right );
        y += 40.0f;
        if ( pEntries == nullptr || pEntries->empty() ) {
            renderer.drawText( pFont, "Empty pack.", x0 + pad, y, textSize, Rgba( 200, 200, 200 ) );
            break;
        }
        // The thumbnail grid: the art (the icons of these banks are the art).
        const float cell = Min( ( w - 2.0f * pad ) / kGridColumns - 8.0f, 72.0f );
        const int32_t count = static_cast<int32_t>( pEntries->size() );
        for ( int32_t i = 0; i < count; i++ ) {
            const int32_t col = i % kGridColumns, row = i / kGridColumns;
            const float cx = x0 + pad + col * ( cell + 8.0f ), cy = y + row * ( cell + 10.0f );
            const bool bSel = static_cast<size_t>( i ) == gridSelected;
            renderer.drawRect( Rect2D( cx, cy, cell, cell ), bSel ? Rgba( 20, 70, 96, 255 ) : Rgba( 45, 52, 62, 220 ) );
            GPUTexture *pArt = ( *pEntries )[static_cast<size_t>( i )].pArt;
            if ( pArt != nullptr ) {
                // The art fitted inside the cell (contain, centred).
                const float artW = static_cast<float>( pArt->Width ), artH = static_cast<float>( pArt->Height );
                const float scale = Min( ( cell - 8.0f ) / artW, ( cell - 8.0f ) / artH );
                const float dw = artW * scale, dh = artH * scale;
                renderer.drawQuad( pArt, Rect2D( cx + ( cell - dw ) * 0.5f, cy + ( cell - dh ) * 0.5f, dw, dh ),
                                   info.bMonochrome ? Rgba( 235, 235, 235 ) : 0xffffffffu );
            }
            if ( bSel ) {
                renderer.drawRectOutline( Rect2D( cx, cy, cell, cell ), 2.0f, Rgba( 255, 255, 255, 255 ) );
            }
        }
        y += ( ( count + kGridColumns - 1 ) / kGridColumns ) * ( cell + 10.0f ) + 6.0f;
        renderer.drawText( pFont, "A: place it on the car      B: back", x0 + pad, y, textSize - 2.0f,
                           Rgba( 210, 210, 210 ) );
        break;
    }
    case StickerState::Place: {
        // A bottom strip only: the car is the preview.
        const float stripY = y0 + h - 96.0f;
        renderer.drawRect( Rect2D( x0, stripY, w, 96.0f ), Rgba( 12, 16, 22, 215 ) );
        renderer.drawText( pFont, "Place the sticker (it is on the car now)", x0 + pad, stripY + 10.0f, textSize,
                           Rgba( 255, 220, 120 ) );
        renderer.drawText( pFont, "D-pad/Arrows: move    numpad 4/6 (pad D-pad < >): size    8/2 (^ v): turn",
                           x0 + pad, stripY + 38.0f, textSize - 2.0f, Rgba( 210, 210, 210 ) );
        renderer.drawText( pFont, "X: colour    A: stick it    B: cancel", x0 + pad, stripY + 62.0f, textSize - 2.0f,
                           Rgba( 210, 210, 210 ) );
        if ( messageTimer > 0.0f ) {
            renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, stripY - 30.0f, textSize, Rgba( 255, 210, 80 ),
                               eTextAlign::TA_Center );
        }
        return;
    }
    case StickerState::Placed: {
        std::vector<StickerPlacement> &layout = stickerLayout();
        renderer.drawText( pFont, "On the car", x0 + pad, y, textSize + 4.0f, Rgba( 255, 255, 255 ) );
        y += 34.0f;
        if ( layout.empty() ) {
            renderer.drawText( pFont, "No stickers yet - pick a pack.", x0 + pad, y, textSize, Rgba( 200, 200, 200 ) );
        } else {
            const int32_t visible = static_cast<int32_t>( ( y0 + h - 150.0f - y ) / 34.0f );
            const int32_t first = Clamp( static_cast<int32_t>( placedSelected ) - visible / 2, 0,
                                         Max( 0, static_cast<int32_t>( layout.size() ) - visible ) );
            for ( int32_t i = first; i < static_cast<int32_t>( layout.size() ) && i < first + visible; i++ ) {
                const StickerPlacement &p = layout[static_cast<size_t>( i )];
                const bool bSel = static_cast<size_t>( i ) == placedSelected;
                if ( bSel ) {
                    renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 30.0f ), Rgba( 20, 70, 96, 255 ) );
                }
                // The tint swatch (the sticker's colour).
                const uint32_t tint = p.Tint;
                renderer.drawRect( Rect2D( x0 + 24.0f, y + 4.0f, 16.0f, 16.0f ), tint );
                renderer.drawRectOutline( Rect2D( x0 + 24.0f, y + 4.0f, 16.0f, 16.0f ), 1.0f, Rgba( 90, 90, 90 ) );
                const std::string row = p.Bank + " #" + std::to_string( p.Index + 1 );
                renderer.drawText( pFont, row.c_str(), x0 + 50.0f, y, textSize, Rgba( 255, 255, 255 ) );
                y += 34.0f;
            }
        }
        renderer.drawText( pFont, "A: move it    Y: remove    B: packs", x0 + pad, y0 + h - 60.0f, textSize - 2.0f,
                           Rgba( 210, 210, 210 ) );
        break;
    }
    }
    if ( state != StickerState::Place && messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 92.0f, textSize, Rgba( 255, 210, 80 ),
                           eTextAlign::TA_Center );
    }
}

void PaintMenu::draw( Renderer2D &renderer ) {
    if ( carId < 0 ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    const PlayerProfile &profile = GetPlayerProfile();
    renderer.begin( 1280.0f, 720.0f );
    if ( page == Page::Stickers && state == StickerState::Place ) {
        // The live editor: the car is the screen, only the hint strip draws.
        drawStickers( renderer, 0.0f, 0.0f, 1280.0f, 720.0f, bSidePanel ? 19.0f : 21.0f );
        renderer.end();
        return;
    }
    if ( !bSidePanel ) {
        renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, 150 ) );
    }
    const float x0 = bSidePanel ? 790.0f : 240.0f, y0 = bSidePanel ? 60.0f : 110.0f;
    const float w = bSidePanel ? 460.0f : 800.0f, h = bSidePanel ? 600.0f : 500.0f;
    const float textSize = bSidePanel ? 19.0f : 21.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 18, 22, 28, bSidePanel ? 215 : 235 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 20, 40, 52, 255 ) );
    const bool bStickerPage = page == Page::Stickers;
    const std::string title = std::string( bStickerPage ? "Sticker shop - " : "Paint shop - " ) +
                              ( carName.empty() ? std::string( "your car" ) : carName );
    renderer.drawText( pFont, title.c_str(), x0 + 20.0f, y0 + 10.0f, bSidePanel ? 24.0f : 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                       eTextAlign::TA_Right );
    if ( bStickerPage ) {
        drawStickers( renderer, x0, y0, w, h, textSize );
        renderer.end();
        return;
    }
    float y = y0 + 70.0f;
    if ( paints.empty() ) {
        renderer.drawText( pFont, "This car has no paint catalogue.", x0 + 24.0f, y, textSize, Rgba( 200, 200, 200 ) );
        renderer.drawText( pFont, "Right: stickers      B / Esc: leave", x0 + w * 0.5f, y0 + h - 44.0f,
                           bSidePanel ? 17.0f : 20.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
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
    renderer.drawText( pFont, "A / Enter: paint the car      Right: stickers      B / Esc: leave", x0 + w * 0.5f,
                       y0 + h - 44.0f, bSidePanel ? 17.0f : 20.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    renderer.end();
}
