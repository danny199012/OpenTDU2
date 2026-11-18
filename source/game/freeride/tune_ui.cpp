#include "shared.h"
#include "tune_ui.h"

#include "audio/frontend_audio.h"
#include "config/gs_config.h"
#include "core/xmb.h"
#include "database/gs_database.h"
#include "flash/flash_player.h"
#include "game/car_shop.h"
#include "game/player_profile.h"
#include "physics/vehicle_data.h"
#include "system/cmd_line.h"

#include <cmath>

namespace {
constexpr int32_t kLocMoneyFormat = 0x3a4; // the money text (gm_shop.cpp formatMoney, FUN_0099d810)
constexpr int32_t kMaxLevel = 3;           // the port's levels per stat (the DBRewardCar range)
constexpr int32_t kMovieLines = 4;         // the movie draws level1..level4
// A level costs 12,000 more than the previous one, a rim set 8,000 (the retail pricing table is in
// the encrypted band; the numbers are the native TuningMenu's) [INF].
constexpr int32_t kLevelPrice = 12000;
constexpr int32_t kRimPrice = 8000;
// getUpglineLockState: 0 = the line can be bought, 1 = locked, 2 = bought [INF: the movie's lock
// frames, confirmed on screen - tuning_ui_spec.md].
constexpr int32_t kLineFree = 0, kLineLocked = 1, kLineOwned = 2;

std::string LocString( int32_t id ) {
    const char *pText = gpDatabase != nullptr ? gpDatabase->getString( id ) : nullptr;
    return pText != nullptr ? pText : "";
}

int32_t LinePrice( int32_t line ) { // line 1..3 -> the price of that level
    return line >= 1 && line <= kMaxLevel ? kLevelPrice * line : 0;
}

// "21\" 245/30" - the plus-size tire a rim diameter gets (the native menu's label).
std::string TireLabel( const RimProfile &profile ) {
    if ( !profile.bValid ) {
        return "n/a";
    }
    return std::to_string( profile.DiameterInch ) + "\" " +
           std::to_string( static_cast<int32_t>( std::lround( profile.WidthFront * 1000.0f ) ) ) + "/" +
           std::to_string( static_cast<int32_t>( std::lround( profile.AspectFront ) ) );
}

// The level -> physics law of ApplyTuningToSetup, as the gauges of the movie show it: torque +8 % per
// accel level, top-end torque +10 % and the rev band +5 % per speed level, braking grip +10 % per
// braking level [INF: the port's own modulation].
float AccelAt( float baseSeconds, int32_t level ) { return baseSeconds / std::pow( 1.0f + 0.08f * level, 0.9f ); }
float SpeedAt( float baseKmh, int32_t level ) { return baseKmh * ( 1.0f + 0.05f * level ); }
float BrakingAt( float baseMeters, int32_t level ) { return baseMeters / ( 1.0f + 0.10f * level ); }
} // namespace

std::string TuneUi::formatMoney( int32_t value ) const {
    // The retail money text: groups of three digits separated by "," in "us", " " elsewhere, inside the
    // DB text's "$§1§" (the sticker shop's and the dealer's formatMoney).
    const bool bUs = gpConfig == nullptr || gpConfig->getLanguage() == "us";
    const char *pSeparator = bUs ? "," : " ";
    std::string digits;
    int32_t rest = value < 0 ? 0 : value;
    while ( rest > 999 ) {
        char group[16];
        snprintf( group, sizeof( group ), "%s%03d", pSeparator, rest % 1000 );
        digits = group + digits;
        rest /= 1000;
    }
    digits = std::to_string( rest ) + digits;
    const std::string format = LocString( kLocMoneyFormat );
    const std::string token = "\xC2\xA7" "1" "\xC2\xA7";
    const size_t marker = format.find( token );
    if ( marker == std::string::npos ) {
        return format.empty() ? "$" + digits : format;
    }
    return format.substr( 0, marker ) + digits + format.substr( marker + token.size() );
}

void TuneUi::bind( FlashPlayer *pNewUi, int32_t newCarId, const std::string &newCarName ) {
    pUi = pNewUi;
    carId = newCarId;
    carName = newCarName;
    stat = 0;
    rimMin = rimMax = rimStock = 0;
    RimProfile stockProfile;
    if ( GetStockRimInfo( carId, stockProfile ) ) {
        rimStock = stockProfile.StockDiameterInch;
        GetRimSizeRange( carId, rimMin, rimMax );
    }
    OTDU_LOG_INFO( "TuneUi: car %d '%s', rims %d..%d\" (stock %d\")\n", carId, carName.c_str(), rimMin, rimMax, rimStock );
}

int32_t TuneUi::statOf( const std::string &upgType ) const {
    if ( StringCmpI( upgType.c_str(), "top_speed" ) == 0 ) {
        return 1;
    }
    if ( StringCmpI( upgType.c_str(), "braking" ) == 0 ) {
        return 2;
    }
    return 0;
}

int32_t TuneUi::levelOf( int32_t whichStat ) const {
    const PlayerProfile &profile = GetPlayerProfile();
    const auto found = profile.OwnedCarTuning.find( carId );
    if ( found == profile.OwnedCarTuning.end() ) {
        return 0;
    }
    return whichStat == 0 ? found->second.Accel : whichStat == 1 ? found->second.Speed : found->second.Braking;
}

void TuneUi::pushMoney() {
    const std::string money = formatMoney( GetPlayerProfile().Money );
    pUi->setVariable( "/:MyMoneyStr", money );
    pUi->setVariable( "/:money", money );
    pUi->setVariableNumber( "/:MyMoneyInt", static_cast<double>( GetPlayerProfile().Money ) );
}

void TuneUi::pushCar() {
    pUi->setVariable( "/:Current_car", carName );
    pUi->setVariable( "/:current_car", carName );
    pUi->setVariableBool( "/:IsBike", false );
    pUi->setVariableBool( "/:isCarTunable", true );
    pUi->setVariable( "/:state_Vehicle", "car" );
    // /:cur_group: the car category name (gm_shop's fillCarVariables, table 0x00edfbe0) [CODE + DATA].
    static const std::pair<int32_t, const char *> kCategories[] = {
        { 1, "A1" },  { 2, "A2" },  { 3, "A3" },   { 4, "A4" },   { 5, "A5" },   { 6, "A6" },  { 7, "A7" },
        { 9, "B1" },  { 10, "B2" }, { 11, "B3" },  { 12, "B4" },  { 14, "C1" },  { 15, "C2" }, { 16, "C3" },
        { 17, "C4" }, { 18, "MA1" }, { 19, "MA2" }, { 22, "MC" }, { 23, "R1" }, { 24, "R2" }, { 25, "R3" },
        { 26, "R4" }, { 27, "R5" }, { 28, "R6" } };
    std::string group = "empty";
    const int32_t row = FindCarPhysicsRow( carId );
    if ( row >= 0 && gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        XmbValue physics = root.getArrayElement( "DBCarPhysicsData", static_cast<uint32_t>( row ) );
        const int32_t category = static_cast<int32_t>( physics["_vehiclecategory"].asInt() );
        for ( const auto &entry : kCategories ) {
            if ( entry.first == category ) {
                group = entry.second;
            }
        }
        pUi->setVariableNumber( "/:cur_difficulty", Clamp( static_cast<double>( physics["_difficulty"].asInt() ), 1.0, 5.0 ) );
    }
    pUi->setVariable( "/:cur_group", group );
    pushPreview( stat, levelOf( stat ) );
}

// The gauges: cur_* = the car as it is, new_* = the previewed level of the stat in focus.
void TuneUi::pushPreview( int32_t whichStat, int32_t previewLevel ) {
    CarSpec spec;
    if ( !FillCarSpec( FindCarPhysicsRow( carId ), spec ) ) {
        return;
    }
    const int32_t accelLevel = levelOf( 0 ), speedLevel = levelOf( 1 ), brakingLevel = levelOf( 2 );
    pUi->setVariableNumber( "/:cur_accel", CarStatGauge( AccelAt( spec.Accel, accelLevel ), kAccelGauge ) );
    pUi->setVariableNumber( "/:cur_speed", CarStatGauge( SpeedAt( spec.MaxSpeed, speedLevel ), kSpeedGauge ) );
    pUi->setVariableNumber( "/:cur_braking", CarStatGauge( BrakingAt( spec.Braking, brakingLevel ), kBrakingGauge ) );
    const int32_t newAccel = whichStat == 0 ? previewLevel : accelLevel;
    const int32_t newSpeed = whichStat == 1 ? previewLevel : speedLevel;
    const int32_t newBraking = whichStat == 2 ? previewLevel : brakingLevel;
    pUi->setVariableNumber( "/:new_accel", CarStatGauge( AccelAt( spec.Accel, newAccel ), kAccelGauge ) );
    pUi->setVariableNumber( "/:new_speed", CarStatGauge( SpeedAt( spec.MaxSpeed, newSpeed ), kSpeedGauge ) );
    pUi->setVariableNumber( "/:new_braking", CarStatGauge( BrakingAt( spec.Braking, newBraking ), kBrakingGauge ) );
    // The car sheet's numbers: cur_* the car now, *_upg the previewed level (top speed / 0-100 / power),
    // the dealer's texts (gm_shop fillCarVariables) [INF for the power gain: torque scales with the levels].
    auto speedText = []( int32_t textId, int32_t value ) {
        std::string text = LocString( textId );
        const std::string token = "\xC2\xA7" "SPEED" "\xC2\xA7"; // FUN_00999b00: the section-sign SPEED token
        const size_t marker = text.find( token );
        if ( marker != std::string::npos ) {
            text.replace( marker, token.size(), std::to_string( value ) );
        }
        return text;
    };
    auto sheet = [&]( const char *pSuffix, int32_t accel, int32_t speed ) {
        const float topKmh = SpeedAt( spec.MaxSpeed, speed );
        const float seconds = AccelAt( spec.Accel, accel );
        const int32_t power = static_cast<int32_t>( spec.Power * ( 1.0f + 0.08f * accel ) * ( 1.0f + 0.04f * speed ) );
        const std::string suffix = pSuffix;
        char buffer[64];
        snprintf( buffer, sizeof( buffer ), "%.2f %s", seconds, LocString( 0x2e2 ).c_str() );
        pUi->setVariable( "/:cur_max_speed_m" + suffix, speedText( 0x3e1, static_cast<int32_t>( topKmh ) ) );
        pUi->setVariable( "/:cur_max_speed_i" + suffix, speedText( 0x3e2, static_cast<int32_t>( topKmh * 0.6213712f ) ) );
        pUi->setVariable( "/:cur_0_100_m" + suffix, buffer );
        snprintf( buffer, sizeof( buffer ), "%.2f %s", seconds * 0.9f, LocString( 0x2e2 ).c_str() ); // 0-60 mph
        pUi->setVariable( "/:cur_0_100_i" + suffix, buffer );
        snprintf( buffer, sizeof( buffer ), "%d hp", power );
        pUi->setVariable( "/:cur_power_m" + suffix, buffer );
        pUi->setVariable( "/:cur_power_i" + suffix, buffer );
    };
    sheet( "", accelLevel, speedLevel );
    sheet( "_upg", newAccel, newSpeed );
    // The upgrade badge of the car sheet: the levels bought so far (level0..level5 frames) [INF].
    pUi->setVariable( "/:cur_upgrade", "level" + std::to_string( Min( 5, accelLevel + speedLevel + brakingLevel ) ) );
}

void TuneUi::pushSquares() {
    pUi->setVariableBool( "/:LockUpgAcceleration", false );
    pUi->setVariableBool( "/:LockUpgTopSpeed", false );
    pUi->setVariableBool( "/:LockUpgBraking", false );
    pUi->setVariableBool( "/:LockUpgHandling", false );
    pUi->setVariableBool( "/:LockUpgRims", rimMax <= 0 );
    pUi->setVariableBool( "/:isNoRims", rimMax <= 0 );
}

// The four lines of a stat page: line n (1..4) is level n.
void TuneUi::pushLines( int32_t whichStat ) {
    stat = whichStat;
    const int32_t owned = levelOf( whichStat );
    const int32_t money = GetPlayerProfile().Money;
    for ( int32_t line = 1; line <= kMovieLines; line++ ) {
        const std::string index = std::to_string( line - 1 );
        int32_t lock = kLineLocked;
        if ( line <= owned ) {
            lock = kLineOwned;
        } else if ( line == owned + 1 && line <= kMaxLevel ) {
            lock = kLineFree;
        }
        pUi->setVariableNumber( "/:LockUpgLevel" + index, lock );
        pUi->setVariable( "/:UpgName" + index, "Level " + std::to_string( line ) );
        pUi->setVariable( "/:UpgPrice" + index, line <= kMaxLevel ? formatMoney( LinePrice( line ) ) : std::string() );
    }
    pUi->setVariableNumber( "/:maxUpgrade", kMaxLevel );
    pUi->setVariableNumber( "/:carUpgrade", owned );
    pUi->setVariableBool( "/:EnoughMoney", owned < kMaxLevel && money >= LinePrice( owned + 1 ) );
    pushPreview( whichStat, Min( owned + 1, kMaxLevel ) );
}

int32_t TuneUi::rimSizeOfItem( int32_t item ) const {
    return Clamp( rimMin + item - 1, rimMin, rimMax );
}

void TuneUi::pushRims() {
    const PlayerProfile &profile = GetPlayerProfile();
    const auto foundRim = profile.OwnedCarRimSizes.find( carId );
    const int32_t installed = foundRim != profile.OwnedCarRimSizes.end() ? foundRim->second : rimStock;
    const int32_t count = rimMax >= rimMin && rimMax > 0 ? rimMax - rimMin + 1 : 0;
    pUi->setVariableNumber( "/:nb_items", count );
    pUi->setVariableNumber( "/:num_item", Clamp( installed - rimMin + 1, 1, Max( count, 1 ) ) );
    pushRimText();
}

// The text and the price of the rim number /:num_item.
void TuneUi::pushRimText() {
    const int32_t item = static_cast<int32_t>( atof( pUi->getVariable( "/:num_item" ).c_str() ) );
    const int32_t size = rimSizeOfItem( item );
    RimProfile profile;
    GetRimProfile( carId, size == rimStock ? 0 : size, profile );
    const PlayerProfile &player = GetPlayerProfile();
    const auto foundRim = player.OwnedCarRimSizes.find( carId );
    const int32_t installed = foundRim != player.OwnedCarRimSizes.end() ? foundRim->second : rimStock;
    const int32_t price = size == rimStock ? 0 : kRimPrice;
    // The page's two text fields (the movie's own Move() writes them from '_Jantes' / '_Prix' in test
    // mode): /:option_name and /:cur_op_price [DATA].
    pUi->setVariable( "/:option_name", TireLabel( profile ) + ( size == rimStock ? " (stock)" : "" ) );
    pUi->setVariable( "/:cur_op_price", formatMoney( price ) );
    pUi->setVariableBool( "/:EnoughMoney", player.Money >= price );
    pUi->setVariableNumber( "/:already_buyed", size == installed ? 1 : 0 );
}

void TuneUi::buyLine( int32_t line ) {
    PlayerProfile &profile = GetPlayerProfile();
    TuningLevels &levels = profile.OwnedCarTuning[carId];
    int32_t *pLevel = stat == 0 ? &levels.Accel : stat == 1 ? &levels.Speed : &levels.Braking;
    const int32_t price = LinePrice( line );
    if ( line != *pLevel + 1 || line > kMaxLevel || profile.Money < price ) {
        OTDU_LOG_INFO( "TuneUi: line %d refused (owned level %d, money $%d, price $%d)\n", line, *pLevel, profile.Money, price );
        return;
    }
    profile.Money -= price;
    *pLevel = line;
    OTDU_LOG_INFO( "TuneUi: car %d %s -> level %d for $%d (money now $%d)\n", carId,
                   stat == 0 ? "accel" : stat == 1 ? "speed" : "braking", line, price, profile.Money );
    SaveProfile( "tuning bought" );
    pushMoney();
    pushLines( stat );
}

void TuneUi::fitRim() {
    const int32_t item = static_cast<int32_t>( atof( pUi->getVariable( "/:num_item" ).c_str() ) );
    const int32_t size = rimSizeOfItem( item );
    PlayerProfile &profile = GetPlayerProfile();
    const int32_t target = size == rimStock ? 0 : size;
    const int32_t price = target == 0 ? 0 : kRimPrice;
    const auto foundRim = profile.OwnedCarRimSizes.find( carId );
    const int32_t installed = foundRim != profile.OwnedCarRimSizes.end() ? foundRim->second : 0;
    if ( target == installed || profile.Money < price ) {
        OTDU_LOG_INFO( "TuneUi: rims %d\" refused (fitted %d, money $%d, price $%d)\n", size, installed, profile.Money, price );
        return;
    }
    profile.Money -= price;
    if ( target == 0 ) {
        profile.OwnedCarRimSizes.erase( carId );
    } else {
        profile.OwnedCarRimSizes[carId] = target;
    }
    OTDU_LOG_INFO( "TuneUi: car %d rims %d\" for $%d (money now $%d)\n", carId, size, price, profile.Money );
    SaveProfile( "rim size bought" );
    pushMoney();
    pushRimText();
}

bool TuneUi::onCommand( const std::string &command ) {
    if ( pUi == nullptr ) {
        return false;
    }
    const bool bLog = CmdLine::HasArg( "-logtune" );
    if ( command == "afmk" || command == "camn" ) {
        pushCar();
        pushMoney();
        pushSquares();
        return command == "afmk";
    }
    if ( command == "amtv" ) {
        const std::string type = pUi->getVariable( "/:UpgType" );
        if ( bLog ) {
            OTDU_LOG_INFO( "TuneUi: amtv UpgType '%s'\n", type.c_str() );
        }
        pushMoney();
        if ( StringCmpI( type.c_str(), "rims" ) != 0 ) {
            pushLines( statOf( type ) );
        }
        return true;
    }
    if ( command == "kida" ) {
        // numUpl is the focused line, 0-based (line 0 = "Level 1") [DATA: kida arrives with 0 on opening].
        const int32_t line = static_cast<int32_t>( atof( pUi->getVariable( "/:numUpl" ).c_str() ) ) + 1;
        if ( bLog ) {
            OTDU_LOG_INFO( "TuneUi: kida numUpl %d\n", line );
        }
        pushPreview( stat, Clamp( line, 1, kMaxLevel ) );
        return true;
    }
    if ( command == "evmo" ) {
        const int32_t line = static_cast<int32_t>( atof( pUi->getVariable( "/:UpgradeSelected" ).c_str() ) ) + 1;
        if ( bLog ) {
            OTDU_LOG_INFO( "TuneUi: evmo UpgradeSelected %d UpgType '%s'\n", line, pUi->getVariable( "/:UpgType" ).c_str() );
        }
        if ( bRimsPage ) {
            pushRimText();
        } else {
            pUi->setVariableBool( "/:EnoughMoney", GetPlayerProfile().Money >= LinePrice( line ) );
        }
        return true;
    }
    if ( command == "kiva" ) {
        buyLine( static_cast<int32_t>( atof( pUi->getVariable( "/:UpgradeSelected" ).c_str() ) ) + 1 );
        return true;
    }
    if ( command == "tune:rims:open" ) {
        // The movie re-sends it after its confirm dialog: the page is still up and its choice stays.
        const bool bAgain = bRimsPage;
        bRimsPage = true;
        if ( CmdLine::HasArg( "-dumptuneclips" ) && !bAgain ) {
            pUi->dumpClips( "/", CmdLine::GetArgInt( "-dumptuneclips", 6 ) );
        }
        pushMoney();
        if ( bAgain ) {
            pushRimText();
        } else {
            pushRims();
        }
        return true;
    }
    if ( command == "chop" ) {
        pushRimText();
        if ( bLog ) {
            OTDU_LOG_INFO( "TuneUi: chop item %s of %s: '%s' %s\n", pUi->getVariable( "/:num_item" ).c_str(),
                           pUi->getVariable( "/:nb_items" ).c_str(), pUi->getVariable( "/:option_name" ).c_str(),
                           pUi->getVariable( "/:cur_op_price" ).c_str() );
        }
        return true;
    }
    if ( command == "tune:rims:vali" ) {
        fitRim();
        return true;
    }
    if ( command == "tune:rims:clos" ) {
        bRimsPage = false;
        pushMoney();
        return true;
    }
    if ( command == "tuci" ) {
        // Retail plays the part cinematic (C_BUY_PART) and then the movie goes back to the walk; not
        // played yet - the walk comes back at once [INF].
        pUi->invoke( "/", "BackToWM", {} );
        return true;
    }
    return false;
}
