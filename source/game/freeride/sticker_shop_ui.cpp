#include "shared.h"
#include "sticker_shop_ui.h"

#include "config/gs_config.h"
#include "core/xmb.h"
#include "database/gs_database.h"
#include "flash/flash_player.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "system/cmd_line.h"

namespace {
constexpr int32_t kLocMoneyFormat = 0x3a4; // the money text (gm_shop.cpp formatMoney, FUN_0099d810)
constexpr int32_t kPricingLabour = 1000;   // DBStickersPricing 'Labour' [DATA]
constexpr int32_t kPricingPaint = 1000;    // 'Paint'
constexpr int32_t kPricingErase = 1000;    // 'Erase'
constexpr int32_t kGridSquares = 24;       // the movie's 4 lines x 6 columns
constexpr float kDefaultScale = 0.22f;     // a fresh sticker's width on the canvas (100 % in the strings)

std::string Format( const char *pFormat, ... ) {
    char buffer[512];
    va_list args;
    va_start( args, pFormat );
    vsnprintf( buffer, sizeof( buffer ), pFormat, args );
    va_end( args );
    return buffer;
}

std::string LocString( int32_t id ) {
    const char *pText = gpDatabase != nullptr ? gpDatabase->getString( id ) : nullptr;
    return pText != nullptr ? pText : "";
}

float WrapAngle( float deg ) {
    while ( deg > 180.0f ) {
        deg -= 360.0f;
    }
    while ( deg < -180.0f ) {
        deg += 360.0f;
    }
    return deg;
}

bool SameLayout( const std::vector<StickerPlacement> &a, const std::vector<StickerPlacement> &b ) {
    if ( a.size() != b.size() ) {
        return false;
    }
    for ( size_t i = 0; i < a.size(); i++ ) {
        const StickerPlacement &x = a[i], &y = b[i];
        if ( StringCmpI( x.Bank.c_str(), y.Bank.c_str() ) != 0 || x.Index != y.Index || x.U != y.U || x.V != y.V ||
             x.Scale != y.Scale || x.AngleDeg != y.AngleDeg || x.Tint != y.Tint || x.Alpha != y.Alpha ||
             x.bFlip != y.bFlip || x.YStretch != y.YStretch ) {
            return false;
        }
    }
    return true;
}
} // namespace

std::string StickerShopUi::formatMoney( int32_t value ) const {
    // The retail money text (gm_shop.cpp formatMoney): groups of three digits
    // separated by "," in "us", " " elsewhere, inside the DB text's "$§1§".
    const bool bUs = gpConfig == nullptr || gpConfig->getLanguage() == "us";
    const char *pSeparator = bUs ? "," : " ";
    std::string digits;
    int32_t rest = value < 0 ? 0 : value;
    while ( rest > 999 ) {
        digits = Format( "%s%03d%s", pSeparator, rest % 1000, digits.c_str() );
        rest /= 1000;
    }
    digits = Format( "%d%s", rest, digits.c_str() );
    const std::string format = LocString( kLocMoneyFormat );
    const std::string token = "\xC2\xA7" "1" "\xC2\xA7";
    const size_t marker = format.find( token );
    if ( marker == std::string::npos ) {
        return format.empty() ? "$" + digits : format;
    }
    return format.substr( 0, marker ) + digits + format.substr( marker + token.size() );
}

bool StickerShopUi::getFlag( const char *pVariable ) const {
    if ( pUi == nullptr ) {
        return false;
    }
    const std::string value = pUi->getVariable( pVariable );
    return value == "1" || value == "true" || value == "True";
}

void StickerShopUi::bind( FlashPlayer *pPlayer, StickerCanvas *pCanvasIn, int32_t newCarId ) {
    pUi = pPlayer;
    pCanvas = pCanvasIn;
    carId = newCarId;
    committed.clear();
    auto found = GetPlayerProfile().OwnedCarStickers.find( carId );
    if ( found != GetPlayerProfile().OwnedCarStickers.end() ) {
        committed = found->second;
    }
    working = committed;
    history.assign( 1, working );
    historyAt = 0;
    category = 0;
    bListOpen = false;
    editIndex = -1;
    bCarView = false;
    bThumbsBound = false;
    thumbCategory = -1;
    labourCost = paintCost = eraseCost = 0;
    bPaintChanged = false;
    buildCategories();
    if ( pUi == nullptr ) {
        return;
    }
    // What the movie's StickersMenu() reads in a real (not fake) run: the
    // car's flags (DBCarPhysicsData _isvalidsticker / _isvalidcarpaintdeluxe),
    // whether it has been modified, the money.
    bool bStickable = true, bPaintable = true;
    {
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBCarPhysicsData", i );
            if ( row["_id_car"].asInt() == carId ) {
                bStickable = row["_isvalidsticker"].asInt() != 0;
                bPaintable = row["_isvalidcarpaintdeluxe"].asInt() != 0;
                break;
            }
        }
    }
    pUi->setVariableBool( "/:IsVehicleStickable", bStickable );
    // The colour picker pages are not wired yet: the paint entry stays locked
    // (docs/re/stickers_ui_spec.md 7).
    pUi->setVariableBool( "/:IsVehiclePaintable", bPaintable && CmdLine::HasArg( "-stickerpaint" ) );
    pUi->setVariableBool( "/:IsBike", false );
    pUi->setVariableBool( "/:UseSymmetry", true );
    pUi->setVariableBool( "/:IsUniformScale", true );
    pUi->setVariableBool( "/:CanUndo", false );
    pUi->setVariableBool( "/:CanColourSticker", false );
    pUi->setVariableBool( "/:CategoryLoaded", false );
    writeModifiedFlags();
    refreshPrices();
    OTDU_LOG_INFO( "StickerShopUi: car %d bound, %zu sticker(s) on it, %zu pack(s) sold, stickable %d paintable %d\n",
                   carId, committed.size(), categories.size(), bStickable ? 1 : 0, bPaintable ? 1 : 0 );
}

void StickerShopUi::unbind() {
    if ( pUi == nullptr ) {
        return;
    }
    // Leaving without paying: the car wears what it wore on the way in.
    revert();
    pUi = nullptr;
    pCanvas = nullptr;
    bCarView = false;
}

void StickerShopUi::buildCategories() {
    categories.clear();
    if ( !StickerBankCatalog::isEnabled() ) {
        return;
    }
    StickerBankCatalog &catalog = StickerBankCatalog::get();
    catalog.load();
    for ( size_t i = 0; i < catalog.getBankCount(); i++ ) {
        if ( catalog.isBankSold( i ) ) {
            categories.push_back( i );
        }
    }
}

void StickerShopUi::pushToCanvas() {
    if ( pCanvas == nullptr ) {
        return;
    }
    std::vector<GPUTexture *> art;
    StickerResolveArt( working, art );
    pCanvas->setLayout( working, art );
}

void StickerShopUi::pushHistory() {
    // The layout without the sticker under the tools (that one is not applied).
    std::vector<StickerPlacement> snapshot = working;
    if ( editIndex >= 0 && editIndex < static_cast<int32_t>( snapshot.size() ) ) {
        snapshot.erase( snapshot.begin() + editIndex );
    }
    history.resize( static_cast<size_t>( historyAt ) + 1 );
    history.push_back( std::move( snapshot ) );
    historyAt = static_cast<int32_t>( history.size() ) - 1;
}

void StickerShopUi::undo() {
    if ( historyAt <= 0 ) {
        return;
    }
    historyAt--;
    working = history[static_cast<size_t>( historyAt )];
    editIndex = -1;
    pushToCanvas();
}

void StickerShopUi::redo() {
    if ( historyAt + 1 >= static_cast<int32_t>( history.size() ) ) {
        return;
    }
    historyAt++;
    working = history[static_cast<size_t>( historyAt )];
    editIndex = -1;
    pushToCanvas();
}

void StickerShopUi::clearAll() {
    working.clear();
    editIndex = -1;
    history.resize( static_cast<size_t>( historyAt ) + 1 );
    history.push_back( working );
    historyAt = static_cast<int32_t>( history.size() ) - 1;
    pushToCanvas();
}

void StickerShopUi::revert() {
    working = committed;
    editIndex = -1;
    pushToCanvas();
}

int32_t StickerShopUi::stickerCost() const {
    // A pack is paid once (the profile keeps the packs bought): the invoice's
    // "Stickers" line is the packs this layout newly uses [INF].
    const PlayerProfile &profile = GetPlayerProfile();
    StickerBankCatalog &catalog = StickerBankCatalog::get();
    std::vector<std::string> counted;
    int32_t cost = 0;
    for ( const StickerPlacement &placement : working ) {
        const std::string key = ToLower( placement.Bank );
        if ( std::find( profile.OwnedStickerBanks.begin(), profile.OwnedStickerBanks.end(), key ) !=
                 profile.OwnedStickerBanks.end() ||
             std::find( counted.begin(), counted.end(), key ) != counted.end() ) {
            continue;
        }
        counted.push_back( key );
        for ( size_t i = 0; i < catalog.getBankCount(); i++ ) {
            if ( StringCmpI( catalog.getBank( i ).BankFile.c_str(), placement.Bank.c_str() ) == 0 ) {
                cost += catalog.getBank( i ).Price;
                break;
            }
        }
    }
    return cost;
}

void StickerShopUi::refreshPrices() {
    if ( pUi == nullptr ) {
        return;
    }
    const bool bLayoutChanged = !SameLayout( working, committed );
    const bool bEmptied = bLayoutChanged && working.empty() && !committed.empty();
    eraseCost = bEmptied ? kPricingErase : 0;
    labourCost = bLayoutChanged && !bEmptied ? kPricingLabour : 0;
    paintCost = bPaintChanged ? kPricingPaint : 0;
    const int32_t stickers = stickerCost();
    const int32_t total = stickers + labourCost + paintCost + eraseCost;
    const int32_t money = GetPlayerProfile().Money;
    pUi->setVariable( "/:StickersCostStr", formatMoney( stickers ) );
    pUi->setVariable( "/:PaintCostStr", formatMoney( paintCost ) );
    pUi->setVariable( "/:LabourCostStr", formatMoney( labourCost + eraseCost ) );
    pUi->setVariable( "/:TotalPrice", formatMoney( total ) );
    pUi->setVariable( "/:TotalPriceStr", formatMoney( total ) );
    pUi->setVariable( "/:money", formatMoney( money ) );
    pUi->setVariable( "/:MyMoneyStr", formatMoney( money ) );
    pUi->setVariableBool( "/:EnoughMoney", money >= total );
    // The stats page of the diary ("Stickers bought / total", "vehicles
    // sticked / painted").
    int32_t totalStickers = 0;
    StickerBankCatalog &catalog = StickerBankCatalog::get();
    for ( const size_t bank : categories ) {
        if ( const std::vector<StickerEntry> *pEntries = catalog.openBank( bank ) ) {
            totalStickers += static_cast<int32_t>( pEntries->size() );
        }
    }
    pUi->setVariableNumber( "/:NbStickersTotal", totalStickers );
}

void StickerShopUi::writeModifiedFlags() {
    if ( pUi == nullptr ) {
        return;
    }
    const bool bModified = !SameLayout( working, committed ) || bPaintChanged;
    pUi->setVariableBool( "/:HasModifiedVehicle", bModified );
    pUi->setVariableBool( "/:CanUndo", historyAt > 0 );
}

void StickerShopUi::loadCategory( int32_t index ) {
    if ( pUi == nullptr ) {
        return;
    }
    if ( categories.empty() ) {
        pUi->setVariableNumber( "/:NbStickersCat", 0 );
        pUi->setVariableNumber( "/:NbStickersCurPage", 0 );
        pUi->setVariableBool( "/:CategoryLoaded", true );
        return;
    }
    const int32_t count = static_cast<int32_t>( categories.size() );
    category = ( ( index % count ) + count ) % count;
    StickerBankCatalog &catalog = StickerBankCatalog::get();
    const size_t bankIndex = categories[static_cast<size_t>( category )];
    const StickerBankInfo &info = catalog.getBank( bankIndex );
    const std::vector<StickerEntry> *pEntries = catalog.openBank( bankIndex );
    const int32_t numStickers = pEntries != nullptr ? Min( static_cast<int32_t>( pEntries->size() ), kGridSquares ) : 0;
    const bool bOwned = std::find( GetPlayerProfile().OwnedStickerBanks.begin(), GetPlayerProfile().OwnedStickerBanks.end(),
                                   ToLower( info.BankFile ) ) != GetPlayerProfile().OwnedStickerBanks.end();
    pUi->setVariableNumber( "/:NbStickersCat", count );
    pUi->setVariableNumber( "/:CurrentStickersCat", category );
    pUi->setVariableNumber( "/:NbStickersCurPage", numStickers );
    pUi->setVariable( "/:StickersCatStr", info.Name );
    pUi->setVariable( "/:PriceCat", formatMoney( info.Price ) );
    pUi->setVariableBool( "/:IsCategoryBought", bOwned );
    pUi->setVariableBool( "/:StickLocked", false );
    pUi->setVariableBool( "/:DLC_Locked", false );
    pUi->setVariableBool( "/:CanColourSticker", info.bMonochrome );
    thumbCategory = category;
    bindThumbnails();
    pUi->setVariableBool( "/:CategoryLoaded", true );
    OTDU_LOG_INFO( "StickerShopUi: category %d/%d '%s' (%s): %d sticker(s), %s\n", category + 1, count,
                   info.BankFile.c_str(), info.Name.c_str(), numStickers, bOwned ? "owned" : formatMoney( info.Price ).c_str() );
}

void StickerShopUi::bindThumbnails() {
    if ( pUi == nullptr || categories.empty() || thumbCategory < 0 ) {
        return;
    }
    StickerBankCatalog &catalog = StickerBankCatalog::get();
    const std::vector<StickerEntry> *pEntries = catalog.openBank( categories[static_cast<size_t>( thumbCategory )] );
    int32_t bound = 0;
    for ( int32_t i = 0; i < kGridSquares; i++ ) {
        const std::string path = "/stickers/list/container/st" + std::to_string( i ) + "/lure";
        GPUTexture *pTexture = nullptr;
        if ( pEntries != nullptr && i < static_cast<int32_t>( pEntries->size() ) ) {
            const StickerEntry &entry = ( *pEntries )[static_cast<size_t>( i )];
            pTexture = entry.pIcon != nullptr ? entry.pIcon : entry.pArt;
        }
        if ( pTexture != nullptr && pUi->setBitmapOverride( path, pTexture, 0.0f, 0.0f, 1.0f, 1.0f ) ) {
            bound++;
        }
    }
    bThumbsBound = bound > 0;
    OTDU_LOG_INFO( "StickerShopUi: %d thumbnail(s) bound\n", bound );
}

void StickerShopUi::beginEdit( int32_t stickerIndex ) {
    if ( categories.empty() ) {
        return;
    }
    StickerBankCatalog &catalog = StickerBankCatalog::get();
    const StickerBankInfo &info = catalog.getBank( categories[static_cast<size_t>( category )] );
    StickerPlacement placement;
    placement.Bank = info.BankFile;
    placement.Index = stickerIndex;
    placement.Scale = kDefaultScale;
    editIndex = static_cast<int32_t>( working.size() );
    working.push_back( placement );
    tool = eTool::Move;
    pUi->setVariableBool( "/:CanColourSticker", info.bMonochrome );
    pushToCanvas();
    writeEditStrings();
    refreshPrices();
    writeModifiedFlags();
    OTDU_LOG_INFO( "StickerShopUi: editing '%s' #%d\n", info.BankFile.c_str(), stickerIndex );
}

void StickerShopUi::endEdit( bool bKeep ) {
    if ( editIndex >= 0 && editIndex < static_cast<int32_t>( working.size() ) && !bKeep ) {
        working.erase( working.begin() + editIndex );
    }
    editIndex = -1;
    pushToCanvas();
    refreshPrices();
    writeModifiedFlags();
}

void StickerShopUi::applyEdit() {
    if ( editIndex < 0 || editIndex >= static_cast<int32_t>( working.size() ) ) {
        return;
    }
    // Symmetry (the movie's Y): a mirrored twin on the other side of the
    // canvas, its art flipped [INF: the retail stamp math is ENC].
    const StickerPlacement stamped = working[static_cast<size_t>( editIndex )];
    pushHistory(); // the layout without the unapplied copy... then the stamp joins it
    history.back().push_back( stamped );
    std::vector<StickerPlacement> &snapshot = history.back();
    if ( getFlag( "/:UseSymmetry" ) ) {
        StickerPlacement mirror = stamped;
        mirror.U = 1.0f - stamped.U;
        mirror.bFlip = !stamped.bFlip;
        mirror.AngleDeg = WrapAngle( -stamped.AngleDeg );
        if ( std::fabs( mirror.U - stamped.U ) > 0.02f ) {
            snapshot.push_back( mirror );
        }
    }
    // The next sticker under the tools starts as a copy of the stamped one.
    working = snapshot;
    working.push_back( stamped );
    editIndex = static_cast<int32_t>( working.size() ) - 1;
    pushToCanvas();
    refreshPrices();
    writeModifiedFlags();
    writeEditStrings();
    OTDU_LOG_INFO( "StickerShopUi: stamped '%s' #%d at (%.3f, %.3f) scale %.3f angle %.1f; %zu on the car\n",
                   stamped.Bank.c_str(), stamped.Index, stamped.U, stamped.V, stamped.Scale, stamped.AngleDeg,
                   snapshot.size() );
}

void StickerShopUi::writeEditStrings() {
    if ( pUi == nullptr || editIndex < 0 || editIndex >= static_cast<int32_t>( working.size() ) ) {
        return;
    }
    const StickerPlacement &p = working[static_cast<size_t>( editIndex )];
    pUi->setVariable( "/:StickerXScaleStr", Format( "%.1f%%", p.Scale / kDefaultScale * 100.0f ) );
    pUi->setVariable( "/:StickerYScaleStr", Format( "%.1f%%", p.Scale * p.YStretch / kDefaultScale * 100.0f ) );
    pUi->setVariable( "/:StickerRotateAngleStr", Format( "%.1f", p.AngleDeg ) );
    pUi->setVariable( "/:StickerAlphaStr", Format( "%.1f%%", p.Alpha * 100.0f ) );
}

void StickerShopUi::pay() {
    PlayerProfile &profile = GetPlayerProfile();
    refreshPrices();
    const int32_t total = stickerCost() + labourCost + paintCost + eraseCost;
    profile.Money = Max( 0, profile.Money - total );
    for ( const StickerPlacement &placement : working ) {
        const std::string key = ToLower( placement.Bank );
        if ( std::find( profile.OwnedStickerBanks.begin(), profile.OwnedStickerBanks.end(), key ) ==
             profile.OwnedStickerBanks.end() ) {
            profile.OwnedStickerBanks.push_back( key );
        }
    }
    if ( working.empty() ) {
        profile.OwnedCarStickers.erase( carId );
    } else {
        profile.OwnedCarStickers[carId] = working;
    }
    committed = working;
    bPaintChanged = false;
    SaveProfile( "stickers paid" );
    OTDU_LOG_INFO( "StickerShopUi: paid %d (%zu sticker(s) on car %d), money now %d\n", total, working.size(), carId,
                   profile.Money );
    refreshPrices();
    writeModifiedFlags();
}

bool StickerShopUi::onCommand( const std::string &fullCommand ) {
    if ( fullCommand.compare( 0, 5, "stsh:" ) != 0 || pUi == nullptr ) {
        return false;
    }
    const std::string command = fullCommand.substr( 5 );
    OTDU_LOG_INFO( "StickerShopUi: %s\n", command.c_str() );
    if ( command == "mcin" ) {
        bCarView = true;
    } else if ( command == "mcou" ) {
        bCarView = false;
        // Backing out of the tools (B) drops the unapplied sticker.
        if ( editIndex >= 0 ) {
            endEdit( false );
        }
    } else if ( command == "stic:list:open" ) {
        bListOpen = true;
        loadCategory( static_cast<int32_t>( pUi->getVariable( "/:CurrentStickersCat" ).empty()
                                                ? category
                                                : atoi( pUi->getVariable( "/:CurrentStickersCat" ).c_str() ) ) );
    } else if ( command == "stic:list:prca" || command == "stic:list:neca" ) {
        // The movie moved CurrentStickersCat before sending this.
        loadCategory( atoi( pUi->getVariable( "/:CurrentStickersCat" ).c_str() ) );
    } else if ( command == "stic:list:vali" ) {
        beginEdit( atoi( pUi->getVariable( "/:SelectedSticker" ).c_str() ) );
    } else if ( command == "stic:list:canc" || command == "stic:list:clos" ) {
        bListOpen = false;
    } else if ( command == "stic:move" ) {
        tool = eTool::Move;
    } else if ( command == "stic:rota" ) {
        tool = eTool::Rotate;
    } else if ( command == "stic:scal" ) {
        tool = eTool::Scale;
    } else if ( command == "stic:alph" ) {
        tool = eTool::Alpha;
    } else if ( command == "stic:colo" ) {
        tool = eTool::Colour;
    } else if ( command == "stic:appl" ) {
        applyEdit();
    } else if ( command == "stic:flip" ) {
        if ( editIndex >= 0 && editIndex < static_cast<int32_t>( working.size() ) ) {
            StickerPlacement &p = working[static_cast<size_t>( editIndex )];
            p.bFlip = !p.bFlip;
            pushToCanvas();
        }
    } else if ( command == "stic:hist:undo" ) {
        undo();
        writeModifiedFlags();
        refreshPrices();
    } else if ( command == "stic:hist:redo" ) {
        redo();
        writeModifiedFlags();
        refreshPrices();
    } else if ( command == "stic:hist:open" || command == "stic:hist:canc" ) {
        writeModifiedFlags();
    } else if ( command == "stic:uscn" || command == "stic:uscf" || command == "stic:symn" || command == "stic:symf" ) {
        // The movie keeps IsUniformScale / UseSymmetry itself.
    } else if ( command == "clea" ) {
        clearAll();
        writeModifiedFlags();
        refreshPrices();
    } else if ( command == "canc" ) {
        revert();
        writeModifiedFlags();
        refreshPrices();
    } else if ( command == "save:open" ) {
        refreshPrices();
    } else if ( command == "save:vali" ) {
        pay();
    } else if ( command == "menu:enbl" || command == "menu:dsbl" ) {
        // The menu buttons' enabled state: the movie's own.
    } else {
        OTDU_LOG_INFO( "StickerShopUi: unhandled '%s'\n", command.c_str() );
    }
    return true;
}

void StickerShopUi::tick( float deltaTime ) {
    if ( pUi == nullptr ) {
        return;
    }
    if ( editIndex < 0 || editIndex >= static_cast<int32_t>( working.size() ) ) {
        return;
    }
    // The movie owns the keys (the tool strip moves on left / right, A stamps,
    // B backs out); the values of the tool come from the analog sticks, the
    // mouse drag and the accessories keys (numpad 4 / 6 / 8 / 2).
    // (The right stick and the wheel are the editor's camera: GMFreeRide.)
    float ax = gpInput->getLeftStickX();
    float ay = gpInput->getLeftStickY();
    if ( gpInput->isMouseButtonDown( 0 ) ) {
        ax += gpInput->getMouseDeltaX() * 0.15f;
        ay += gpInput->getMouseDeltaY() * 0.15f;
    }
    if ( gpInput->isDown( eInputAction::IA_AccessoriesLeft ) ) {
        ax -= 1.0f;
    }
    if ( gpInput->isDown( eInputAction::IA_AccessoriesRight ) ) {
        ax += 1.0f;
    }
    if ( gpInput->isDown( eInputAction::IA_AccessoriesUp ) ) {
        ay -= 1.0f;
    }
    if ( gpInput->isDown( eInputAction::IA_AccessoriesDown ) ) {
        ay += 1.0f;
    }
    const float wheel = 0.0f;
    ax = Clamp( ax, -1.0f, 1.0f );
    ay = Clamp( ay, -1.0f, 1.0f );
    if ( std::fabs( ax ) < 0.12f ) {
        ax = 0.0f;
    }
    if ( std::fabs( ay ) < 0.12f ) {
        ay = 0.0f;
    }
    if ( ax == 0.0f && ay == 0.0f && wheel == 0.0f ) {
        return;
    }
    StickerPlacement &p = working[static_cast<size_t>( editIndex )];
    switch ( tool ) {
    case eTool::Move:
        p.U = Clamp( p.U + ax * 0.35f * deltaTime, 0.02f, 0.98f );
        p.V = Clamp( p.V + ay * 0.35f * deltaTime, 0.02f, 0.98f );
        break;
    case eTool::Rotate:
        p.AngleDeg = WrapAngle( p.AngleDeg + ( ax + wheel * 0.5f ) * 140.0f * deltaTime );
        break;
    case eTool::Scale: {
        const float factor = 1.0f + ( ax + wheel * 0.5f ) * 0.9f * deltaTime;
        const float oldWidth = p.Scale;
        p.Scale = Clamp( p.Scale * factor, 0.05f, 1.0f );
        if ( getFlag( "/:IsUniformScale" ) ) {
            // Both axes together: the stretch rides along.
        } else {
            // The stick's vertical axis stretches the height alone.
            p.YStretch = Clamp( p.YStretch * ( 1.0f - ay * 0.9f * deltaTime ), 0.2f, 5.0f );
            (void)oldWidth;
        }
        break;
    }
    case eTool::Alpha:
        p.Alpha = Clamp( p.Alpha + ( ax + wheel * 0.5f ) * 0.6f * deltaTime, 0.05f, 1.0f );
        break;
    case eTool::Colour:
        break;
    }
    pushToCanvas();
    writeEditStrings();
}
