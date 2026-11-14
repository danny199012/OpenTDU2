#include "shared.h"
#include "sticker_shop_ui.h"

#include "config/gs_config.h"
#include "core/xmb.h"
#include "database/gs_database.h"
#include "flash/flash_player.h"
#include "game/cinematic/cinematic_car.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"

#include <SDL3/SDL_scancode.h>

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

void StickerShopUi::tintCube( const std::string &clipPath, uint32_t colour ) {
    // The cubes' colour sprites are fully transparent solid placeholders the
    // exe fills natively (the dealer logo's lure kind): a white texture bound
    // to the placeholder, tinted through the clip's colour transform.
    static GPUTexture *pWhite = nullptr;
    if ( pWhite == nullptr && gpRender != nullptr ) {
        const uint32_t white[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
        pWhite = gpRender->getTextureCache().createFromRGBA( 2, 2, reinterpret_cast<const uint8_t *>( white ),
                                                             "sticker_colour_white" );
    }
    if ( pUi == nullptr || pWhite == nullptr ) {
        return;
    }
    if ( colour == 0u ) {
        pUi->setBitmapOverride( clipPath, nullptr, 0.0f, 0.0f, 1.0f, 1.0f );
        return;
    }
    pUi->setBitmapOverride( clipPath, pWhite, 0.0f, 0.0f, 1.0f, 1.0f );
    pUi->setClipColor( clipPath, ( colour & 0xff ) / 255.0f, ( ( colour >> 8 ) & 0xff ) / 255.0f,
                       ( ( colour >> 16 ) & 0xff ) / 255.0f );
}

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

void StickerShopUi::bind( FlashPlayer *pPlayer, StickerCanvas *pCanvasIn, int32_t newCarId, CinematicCar *pCarIn ) {
    pUi = pPlayer;
    pCanvas = pCanvasIn;
    pCar = pCarIn;
    carId = newCarId;
    bPaintOpen = false;
    {
        // The paint the car wears: a custom one from a past visit, else the
        // dealer's (the working colours start on the body's own colour).
        const auto found = GetPlayerProfile().OwnedCarCustomPaints.find( carId );
        bCommittedCustom = found != GetPlayerProfile().OwnedCarCustomPaints.end();
        paintCommitted = bCommittedCustom ? found->second : PlayerProfile::CustomPaint();
        paintWork = paintCommitted;
        bPaintCustom = bCommittedCustom;
        if ( !bCommittedCustom && pCar != nullptr ) {
            paintWork.Colour1 = paintWork.Colour2 = pCar->getBodyPaintColour();
        }
    }
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
    // The Paint page needs a car to recolour (-nostickerpaint keeps the lock).
    static const bool bNoPaint = CmdLine::HasArg( "-nostickerpaint" );
    pUi->setVariableBool( "/:IsVehiclePaintable", bPaintable && pCar != nullptr && !bNoPaint );
    pUi->setVariableBool( "/:BicolourPaint", paintWork.Bicolour );
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
    pCar = nullptr;
    bPaintOpen = false;
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
    // The paint goes back too (the dealer's, or the custom one of a past visit).
    bPaintCustom = bCommittedCustom;
    paintWork = paintCommitted;
    bPaintChanged = false;
    applyPaintToCar();
    if ( !bCommittedCustom && pCar != nullptr ) {
        paintWork.Colour1 = paintWork.Colour2 = pCar->getBodyPaintColour();
    }
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
        mirror.AngleDeg = WrapAngle( 0.0f - stamped.AngleDeg );
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

uint32_t StickerShopUi::pickedColour() const {
    if ( bPaintOpen ) {
        return activePaintColour();
    }
    return GradientColour( cursorU, cursorV, cursorVal );
}

uint32_t StickerShopUi::activePaintColour() const {
    const bool bSecond = paintWork.Bicolour && pUi != nullptr && atoi( pUi->getVariable( "/:CurrentColor" ).c_str() ) == 2;
    return bSecond ? paintWork.Colour2 : paintWork.Colour1;
}

std::string StickerShopUi::colourRoot() const {
    return bPaintOpen ? "/paint" : "/stickers/colors";
}

float StickerShopUi::BrightnessInput() {
    // The brightness bar: the triggers (accelerate / brake), Page Up / Down.
    float value = 0.0f;
    if ( gpInput->isDown( eInputAction::IA_CarAccel ) || gpInput->isKeyDown( SDL_SCANCODE_PAGEUP ) ) value += 1.0f;
    if ( gpInput->isDown( eInputAction::IA_CarBrake ) || gpInput->isKeyDown( SDL_SCANCODE_PAGEDOWN ) ) value -= 1.0f;
    return value;
}

void StickerShopUi::CursorFromColour( uint32_t colour, float &u, float &v, float &val ) {
    // The picker's inverse: hue across, saturation down (white at the bottom),
    // the brightness bar's value [INF: the retail picker's layout from the
    // owner's video frames: a hue x saturation square and a value bar].
    const float r = ( colour & 0xff ) / 255.0f, g = ( ( colour >> 8 ) & 0xff ) / 255.0f, b = ( ( colour >> 16 ) & 0xff ) / 255.0f;
    const float maxc = Max( r, Max( g, b ) ), minc = Min( r, Min( g, b ) );
    const float saturation = maxc > 0.0f ? ( maxc - minc ) / maxc : 0.0f;
    float hue = 0.0f;
    if ( maxc > minc ) {
        if ( maxc == r ) hue = std::fmod( ( g - b ) / ( maxc - minc ), 6.0f );
        else if ( maxc == g ) hue = ( b - r ) / ( maxc - minc ) + 2.0f;
        else hue = ( r - g ) / ( maxc - minc ) + 4.0f;
        hue *= 60.0f;
        if ( hue < 0.0f ) hue += 360.0f;
    }
    u = hue / 360.0f;
    v = 1.0f - saturation;
    val = maxc;
}

uint32_t StickerShopUi::GradientColour( float u, float v, float val ) {
    u = Clamp( u, 0.0f, 1.0f );
    v = Clamp( v, 0.0f, 1.0f );
    const float pickHue = u * 360.0f;
    const float pickSaturation = 1.0f - v;
    const float pickValue = Clamp( val, 0.0f, 1.0f );
    // HSV -> RGBA8 (MakeRGBA order: R low byte, A top).
    const float h = std::fmod( pickHue, 360.0f ) / 60.0f;
    const float c = pickValue * pickSaturation;
    const float x = c * ( 1.0f - std::fabs( std::fmod( h, 2.0f ) - 1.0f ) );
    const float m = pickValue - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if ( h < 1.0f ) { r = c; g = x; }
    else if ( h < 2.0f ) { r = x; g = c; }
    else if ( h < 3.0f ) { g = c; b = x; }
    else if ( h < 4.0f ) { g = x; b = c; }
    else if ( h < 5.0f ) { r = x; b = c; }
    else { r = c; b = x; }
    const uint32_t R = static_cast<uint32_t>( Clamp( ( r + m ) * 255.0f + 0.5f, 0.0f, 255.0f ) );
    const uint32_t G = static_cast<uint32_t>( Clamp( ( g + m ) * 255.0f + 0.5f, 0.0f, 255.0f ) );
    const uint32_t B = static_cast<uint32_t>( Clamp( ( b + m ) * 255.0f + 0.5f, 0.0f, 255.0f ) );
    return R | ( G << 8 ) | ( B << 16 ) | ( 0xffu << 24 );
}

void StickerShopUi::applyPickedToSticker() {
    if ( editIndex < 0 || editIndex >= static_cast<int32_t>( working.size() ) ) {
        return;
    }
    working[static_cast<size_t>( editIndex )].Tint = pickedColour();
    pushToCanvas();
}

float StickerShopUi::activeValue() const {
    if ( bPaintOpen ) {
        return paintVal[paintWork.Bicolour && pUi != nullptr && atoi( pUi->getVariable( "/:CurrentColor" ).c_str() ) == 2 ? 1 : 0];
    }
    return cursorVal;
}

void StickerShopUi::bindGradient() {
    // The picker's lure_grad is a placeholder the exe fills with its colour
    // gradient: a generated one is bound to it - the hue x saturation square
    // (90 %) and the brightness bar the owner's retail frames show on its
    // right [INF]. The bar's handle is drawn into one of 17 variants.
    constexpr int32_t kSteps = 16;
    static GPUTexture *pGradients[kSteps + 1] = {};
    const int32_t step = static_cast<int32_t>( Clamp( activeValue(), 0.0f, 1.0f ) * kSteps + 0.5f );
    if ( pGradients[step] == nullptr && gpRender != nullptr ) {
        constexpr uint32_t kSize = 128;
        std::vector<uint32_t> pixels( kSize * kSize );
        const float handleV = 1.0f - static_cast<float>( step ) / kSteps;
        for ( uint32_t y = 0; y < kSize; y++ ) {
            const float fy = ( y + 0.5f ) / kSize;
            for ( uint32_t x = 0; x < kSize; x++ ) {
                const float fx = ( x + 0.5f ) / kSize;
                uint32_t colour = 0u;
                if ( fx < 0.9f ) {
                    colour = GradientColour( fx / 0.9f, fy, 1.0f );
                } else if ( fx > 0.93f ) {
                    // The bar: white on top down to black.
                    const uint32_t g = static_cast<uint32_t>( ( 1.0f - fy ) * 255.0f );
                    colour = g | ( g << 8 ) | ( g << 16 ) | ( 0xffu << 24 );
                }
                if ( fx > 0.9f && std::fabs( fy - handleV ) < 0.018f ) {
                    colour = 0xff202020u; // the handle: a dark rule with a light core
                    if ( std::fabs( fy - handleV ) < 0.007f ) colour = 0xffffffffu;
                }
                pixels[y * kSize + x] = colour;
            }
        }
        pGradients[step] = gpRender->getTextureCache().createFromRGBA(
            kSize, kSize, reinterpret_cast<const uint8_t *>( pixels.data() ), ( "sticker_colour_gradient_" + std::to_string( step ) ).c_str() );
    }
    if ( pUi != nullptr && pGradients[step] != nullptr ) {
        pUi->setBitmapOverride( colourRoot() + "/color_box/color_picker/lure_grad", pGradients[step], 0.0f, 0.0f, 1.0f, 1.0f );
    }
}

void StickerShopUi::placeCursor() {
    if ( pUi == nullptr ) {
        return;
    }
    // lure_grad spans 40..5080 twips of the picker (2..254 px) [DATA: the
    // movie's display list]; the square is its left 90 %.
    // The cursor sprite's origin is not its tip (the dropper art hangs ~57 px
    // down-right of it) and its scale against the gradient is 1.19: fitted
    // against screenshots at both corners [INF].
    const std::string picker = colourRoot() + "/color_box/color_picker";
    constexpr float kSquare = 0.9f;
    if ( bPaintOpen ) {
        // Two cursors: `cursor` is the body colour, `cursor_2` the bicolour's.
        pUi->setClipTransform( picker + "/cursor", 57.0f + paintU[0] * 212.0f * kSquare, 59.0f + paintV[0] * 212.0f, 0.0f, 100.0f, 100.0f );
        if ( paintWork.Bicolour ) {
            pUi->setClipTransform( picker + "/cursor_2", 57.0f + paintU[1] * 212.0f * kSquare, 59.0f + paintV[1] * 212.0f, 0.0f, 100.0f,
                                   100.0f );
        }
        return;
    }
    pUi->setClipTransform( picker + "/cursor", 57.0f + cursorU * 212.0f * kSquare, 59.0f + cursorV * 212.0f, 0.0f, 100.0f, 100.0f );
}

int32_t StickerShopUi::selectedSlot() const {
    if ( pUi == nullptr ) {
        return -1;
    }
    // The box's own cursor (num_c, 0..5) [DATA: the movie's variable].
    const std::string value = pUi->getVariable( colourRoot() + "/my_colors_box/:num_c" );
    return value.empty() ? 0 : atoi( value.c_str() );
}

void StickerShopUi::writeMyColours() {
    if ( pUi == nullptr ) {
        return;
    }
    PlayerProfile &profile = GetPlayerProfile();
    profile.StickerMyColors.resize( 6, 0u );
    const std::string box = colourRoot() + "/my_colors_box";
    for ( int32_t i = 0; i < 6; i++ ) {
        const uint32_t colour = profile.StickerMyColors[static_cast<size_t>( i )];
        pUi->setVariableBool( box + "/c" + std::to_string( i ) + "/:isColor", colour != 0u );
        tintCube( box + "/c" + std::to_string( i ) + "/cube/color", colour );
    }
    const bool bInvoked = pUi->invoke( box, "setColors", {} );
    OTDU_LOG_INFO( "StickerShopUi: my colours written: c0 isColor '%s', box num_c '%s', setColors %d, clip %s\n",
                   pUi->getVariable( box + "/c0/:isColor" ).c_str(), pUi->getVariable( box + "/:num_c" ).c_str(),
                   bInvoked ? 1 : 0, pUi->findClip( box + "/c0/cube/color" ) != nullptr ? "found" : "MISSING" );
    tintCube( box + "/picked_color/color", pickedColour() );
    if ( CmdLine::HasArg( "-logstickerui" ) ) {
        for ( const char *pSub : { "/c0/cube/color", "/c0/cube", "/c0", "/picked_color/color" } ) {
            FlashPlayer::Clip *pClip = pUi->findClip( box + pSub );
            if ( pClip != nullptr && pClip->pInstance != nullptr ) {
                const FlashColorTransform &t = pClip->pInstance->ColorTransform;
                OTDU_LOG_INFO( "StickerShopUi: %s visible %d mul %.2f %.2f %.2f %.2f add %.0f %.0f %.0f %.0f frame %u\n", pSub,
                               pClip->pInstance->bVisible ? 1 : 0, t.Mul[0], t.Mul[1], t.Mul[2], t.Mul[3], t.Add[0],
                               t.Add[1], t.Add[2], t.Add[3], pClip->CurrentFrame );
            }
        }
    }
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
    // The paint the car now wears is the car's from now on.
    if ( bPaintCustom ) {
        profile.OwnedCarCustomPaints[carId] = paintWork;
    } else {
        profile.OwnedCarCustomPaints.erase( carId );
    }
    bCommittedCustom = bPaintCustom;
    paintCommitted = paintWork;
    bPaintChanged = false;
    SaveProfile( "stickers paid" );
    OTDU_LOG_INFO( "StickerShopUi: paid %d (%zu sticker(s) on car %d%s), money now %d\n", total, working.size(), carId,
                   bPaintCustom ? ", custom paint" : "", profile.Money );
    refreshPrices();
    writeModifiedFlags();
}

bool StickerShopUi::paintDiffers() const {
    if ( bPaintCustom != bCommittedCustom ) {
        return true;
    }
    if ( !bPaintCustom ) {
        return false;
    }
    return paintWork.Type != paintCommitted.Type || paintWork.Bicolour != paintCommitted.Bicolour ||
           paintWork.Colour1 != paintCommitted.Colour1 || ( paintWork.Bicolour && paintWork.Colour2 != paintCommitted.Colour2 );
}

void StickerShopUi::applyPaintToCar() {
    if ( pCar == nullptr ) {
        return;
    }
    if ( bPaintCustom ) {
        pCar->applyCustomPaint( paintWork.Type, paintWork.Colour1, paintWork.Bicolour, paintWork.Colour2 );
    } else {
        pCar->restorePaint();
    }
}

void StickerShopUi::writePaintType() {
    // The finish name in the "Color type" box (left / right changes it): the
    // text field's variable is `_root.ColorType` [DATA: -dumpclips]. The names
    // are the colors.2dm base materials' [INF: the retail strings are the
    // game's].
    static const char *const kNames[] = { "Glossy", "Matt", "Metal", "Carbon" };
    const int32_t type = Clamp( paintWork.Type, 0, 3 );
    pUi->setVariable( "/:ColorType", kNames[type] );
}

bool StickerShopUi::onPaintCommand( const std::string &command ) {
    // The movie's Paint page: pain:colo:open / vali / canc / clos, bich (the
    // active colour moved to the other one), bion / biof (the bicolour
    // switch), type:open / next / prev (the finish), pain:myco:* (My Colors).
    if ( command == "colo:open" ) {
        bPaintOpen = true;
        bBackupCustom = bPaintCustom;
        paintBackup = paintWork;
        CursorFromColour( paintWork.Colour1, paintU[0], paintV[0], paintVal[0] );
        CursorFromColour( paintWork.Colour2, paintU[1], paintV[1], paintVal[1] );
        pUi->setVariableBool( "/:BicolourPaint", paintWork.Bicolour );
        if ( CmdLine::HasArg( "-logstickerui" ) ) {
            pUi->dumpClips( "/paint", 7 );
        }
    } else if ( command == "myco:init" || command == "myco:open" ) {
        bPaintOpen = true;
        writeMyColours();
    } else if ( command == "colo:vali" ) {
        bPaintChanged = paintDiffers();
        refreshPrices();
        writeModifiedFlags();
    } else if ( command == "colo:canc" ) {
        bPaintCustom = bBackupCustom;
        paintWork = paintBackup;
        bPaintChanged = paintDiffers();
        applyPaintToCar();
        refreshPrices();
        writeModifiedFlags();
    } else if ( command == "colo:clos" ) {
        bPaintOpen = false;
    } else if ( command == "colo:bion" || command == "colo:biof" ) {
        paintWork.Bicolour = command == "colo:bion";
        bPaintCustom = true;
        pUi->setVariableBool( "/:BicolourPaint", paintWork.Bicolour );
        applyPaintToCar();
    } else if ( command == "colo:bich" ) {
        // /:CurrentColor moved; the tick reads it.
    } else if ( command == "colo:type:open" ) {
        writePaintType();
    } else if ( command == "colo:type:next" || command == "colo:type:prev" ) {
        paintWork.Type = ( paintWork.Type + ( command == "colo:type:next" ? 1 : 3 ) ) % 4;
        bPaintCustom = true;
        applyPaintToCar();
        writePaintType();
    } else if ( command == "myco:vali" || command == "myco:fill" || command == "myco:eras" ) {
        PlayerProfile &profile = GetPlayerProfile();
        profile.StickerMyColors.resize( 6, 0u );
        const int32_t slot = selectedSlot();
        const bool bSecond = paintWork.Bicolour && atoi( pUi->getVariable( "/:CurrentColor" ).c_str() ) == 2;
        if ( slot >= 0 && slot < 6 ) {
            uint32_t &entry = profile.StickerMyColors[static_cast<size_t>( slot )];
            if ( command == "myco:fill" ) {
                entry = activePaintColour();
                SaveProfile( "paint colour slot" );
            } else if ( command == "myco:eras" ) {
                entry = 0u;
                SaveProfile( "paint colour slot" );
            } else if ( entry != 0u ) {
                ( bSecond ? paintWork.Colour2 : paintWork.Colour1 ) = entry;
                CursorFromColour( entry, paintU[bSecond ? 1 : 0], paintV[bSecond ? 1 : 0], paintVal[bSecond ? 1 : 0] );
                bPaintCustom = true;
                applyPaintToCar();
            }
        }
        writeMyColours();
    } else if ( command == "myco:clos" || command == "myco:clea" ) {
        // The slots closed; the picker stays up.
    } else {
        OTDU_LOG_INFO( "StickerShopUi: unhandled paint command '%s'\n", command.c_str() );
    }
    return true;
}

void StickerShopUi::tickPaint( float deltaTime, float ax, float ay ) {
    const std::string root = colourRoot();
    const std::string picker = root + "/color_box/color_picker";
    const bool bSecond = paintWork.Bicolour && atoi( pUi->getVariable( "/:CurrentColor" ).c_str() ) == 2;
    const int32_t at = bSecond ? 1 : 0;
    // The movie re-places the cubes' colour sprites as the box animates: the
    // tints are written every frame.
    const PlayerProfile &profile = GetPlayerProfile();
    const std::string box = root + "/my_colors_box";
    for ( size_t i = 0; i < 6 && i < profile.StickerMyColors.size(); i++ ) {
        tintCube( box + "/c" + std::to_string( i ) + "/cube/color", profile.StickerMyColors[i] );
    }
    tintCube( box + "/picked_color/color", activePaintColour() );
    bindGradient();
    tintCube( picker + "/color1/cube/color", paintWork.Colour1 );
    if ( paintWork.Bicolour ) {
        tintCube( picker + "/color2/cube/color", paintWork.Colour2 );
    }
    const bool bEditingSlots = getFlag( ( root + "/:editingColors" ).c_str() );
    const float bright = BrightnessInput();
    if ( !bEditingSlots && bright != 0.0f ) {
        paintVal[at] = Clamp( paintVal[at] + bright * 0.6f * deltaTime, 0.0f, 1.0f );
        ( bSecond ? paintWork.Colour2 : paintWork.Colour1 ) = GradientColour( paintU[at], paintV[at], paintVal[at] );
        bPaintCustom = true;
        applyPaintToCar();
    }
    if ( !bEditingSlots && ( ax != 0.0f || ay != 0.0f ) ) {
        paintU[at] = Clamp( paintU[at] + ax * 0.45f * deltaTime, 0.0f, 1.0f );
        paintV[at] = Clamp( paintV[at] + ay * 0.45f * deltaTime, 0.0f, 1.0f );
        ( bSecond ? paintWork.Colour2 : paintWork.Colour1 ) = GradientColour( paintU[at], paintV[at], paintVal[at] );
        bPaintCustom = true;
        applyPaintToCar();
    }
    placeCursor();
}

bool StickerShopUi::onCommand( const std::string &fullCommand ) {
    if ( fullCommand.compare( 0, 5, "stsh:" ) != 0 || pUi == nullptr ) {
        return false;
    }
    const std::string command = fullCommand.substr( 5 );
    OTDU_LOG_INFO( "StickerShopUi: %s\n", command.c_str() );
    if ( command.compare( 0, 5, "pain:" ) == 0 ) {
        return onPaintCommand( command.substr( 5 ) );
    }
    if ( command == "mcin" ) {
        bCarView = true;
    } else if ( command == "mcou" ) {
        bCarView = false;
        // Backing out of the tools (B) drops the unapplied sticker; the colour
        // box also takes the camera out (the shop shelves are its pipette's
        // ground) and the sticker stays under the tools [INF].
        if ( editIndex >= 0 && !bMyColoursOpen ) {
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
    } else if ( command == "stic:colo:open" || command == "stic:myco:init" || command == "stic:myco:open" ) {
        bMyColoursOpen = true;
        if ( command == "stic:colo:open" && editIndex >= 0 && editIndex < static_cast<int32_t>( working.size() ) ) {
            // The cursor starts on the sticker's own colour.
            CursorFromColour( working[static_cast<size_t>( editIndex )].Tint, cursorU, cursorV, cursorVal );
        }
        bindGradient();
        writeMyColours();
    } else if ( command == "stic:colo:clos" ) {
        bMyColoursOpen = false;
    } else if ( command == "stic:myco:clos" || command == "stic:myco:clea" ) {
        // The "My Colors" slots closed: the picker panel stays up.
    } else if ( command == "stic:myco:vali" || command == "stic:myco:fill" || command == "stic:myco:eras" ) {
        PlayerProfile &profile = GetPlayerProfile();
        profile.StickerMyColors.resize( 6, 0u );
        const int32_t slot = selectedSlot();
        if ( slot >= 0 && slot < 6 ) {
            if ( command == "stic:myco:fill" ) {
                profile.StickerMyColors[static_cast<size_t>( slot )] = pickedColour();
                SaveProfile( "sticker colour slot" );
            } else if ( command == "stic:myco:eras" ) {
                profile.StickerMyColors[static_cast<size_t>( slot )] = 0u;
                SaveProfile( "sticker colour slot" );
            } else if ( profile.StickerMyColors[static_cast<size_t>( slot )] != 0u && editIndex >= 0 &&
                        editIndex < static_cast<int32_t>( working.size() ) ) {
                working[static_cast<size_t>( editIndex )].Tint = profile.StickerMyColors[static_cast<size_t>( slot )];
                pushToCanvas();
            }
        }
        writeMyColours();
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
    if ( !bPaintOpen && ( editIndex < 0 || editIndex >= static_cast<int32_t>( working.size() ) ) ) {
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
    if ( bPaintOpen ) {
        tickPaint( deltaTime, ax, ay );
        return;
    }
    if ( bMyColoursOpen ) {
        // The movie's own frames re-place the cubes' colour sprites as the box
        // opens and as a slot lights: the tint is written every frame.
        const PlayerProfile &colours = GetPlayerProfile();
        const std::string box = "/stickers/colors/my_colors_box";
        for ( size_t i = 0; i < 6 && i < colours.StickerMyColors.size(); i++ ) {
            tintCube( box + "/c" + std::to_string( i ) + "/cube/color", colours.StickerMyColors[i] );
        }
        tintCube( box + "/picked_color/color", pickedColour() );
        // The pipette: the left stick (or a mouse drag) moves the cursor over
        // the gradient while the picker has the focus; the colour under it is
        // the sticker's, live. In "My Colors" the movie moves its own slots
        // and the X press saves the colour under the cursor [INF].
        bindGradient();
        tintCube( "/stickers/colors/color_box/color_picker/color1/cube/color", pickedColour() );
        const bool bEditingSlots = getFlag( "/stickers/colors/:editingColors" );
        const float bright = BrightnessInput();
        if ( !bEditingSlots && bright != 0.0f ) {
            cursorVal = Clamp( cursorVal + bright * 0.6f * deltaTime, 0.0f, 1.0f );
            applyPickedToSticker();
        }
        if ( !bEditingSlots && ( ax != 0.0f || ay != 0.0f ) ) {
            cursorU = Clamp( cursorU + ax * 0.45f * deltaTime, 0.0f, 1.0f );
            cursorV = Clamp( cursorV + ay * 0.45f * deltaTime, 0.0f, 1.0f );
            applyPickedToSticker();
            static int32_t sLogCounter = 0;
            if ( CmdLine::HasArg( "-logstickerui" ) && ( sLogCounter++ % 10 ) == 0 ) {
                OTDU_LOG_INFO( "StickerShopUi: picker cursor (%.3f, %.3f) colour %08x dt %.4f\n", cursorU, cursorV,
                               pickedColour(), deltaTime );
            }
        }
        placeCursor();
        return;
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
