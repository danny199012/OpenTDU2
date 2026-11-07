#include "shared.h"
#include "risk_driving.h"

#include "core/crypto/cpr.h"
#include "filesystem/gs_file.h"

namespace {
// Linear map of x from [x0, x1] to [y0, y1], clamped to the range.
float Remap( float x, float x0, float x1, float y0, float y1 ) {
    if ( std::fabs( x1 - x0 ) < 1e-6f ) {
        return y0;
    }
    const float t = Clamp( ( x - x0 ) / ( x1 - x0 ), 0.0f, 1.0f );
    return y0 + ( y1 - y0 ) * t;
}

// The value of attribute pName of the first <pTag .../> inside text[begin, end).
bool ReadAttribute( const std::string &text, size_t begin, size_t end, const char *pTag, const char *pName, float &outValue ) {
    const size_t tag = text.find( std::string( "<" ) + pTag, begin );
    if ( tag == std::string::npos || tag >= end ) {
        return false;
    }
    const size_t close = text.find( '>', tag );
    const size_t attribute = text.find( std::string( " " ) + pName + "=\"", tag );
    if ( attribute == std::string::npos || attribute > close ) {
        return false;
    }
    outValue = static_cast<float>( atof( text.c_str() + attribute + strlen( pName ) + 3 ) );
    return true;
}

// [begin, end) of <pSection> ... </pSection> (the whole text when absent).
void FindSection( const std::string &text, const char *pSection, size_t &outBegin, size_t &outEnd ) {
    const size_t open = text.find( std::string( "<" ) + pSection + ">" );
    const size_t close = text.find( std::string( "</" ) + pSection + ">" );
    outBegin = open != std::string::npos ? open : 0;
    outEnd = close != std::string::npos ? close : text.size();
}
} // namespace

bool RiskDriving::LoadConfig( Config &config ) {
    std::string text;
    if ( gpFile == nullptr || !LoadCPRText( ( gpFile->getBankFolder() + "scenarics/frpoints.cpr" ).c_str(), text ) ) {
        OTDU_LOG_WARN( "Risk: scenarics/frpoints.cpr not read, built-in FRIM tuning\n" );
        return false;
    }
    const size_t all = text.size();
    ReadAttribute( text, 0, all, "CALCUL_PTS", "Value", config.CalculPts );
    for ( int32_t i = 0; i < 4; i++ ) {
        const std::string val = "val" + std::to_string( i + 1 ), pts = "pts" + std::to_string( i + 1 );
        ReadAttribute( text, 0, all, "DEC_SPEED", val.c_str(), config.DecSpeedVal[i] );
        ReadAttribute( text, 0, all, "DEC_SPEED", pts.c_str(), config.DecSpeedPts[i] );
    }
    for ( int32_t i = 0; i <= 10; i++ ) {
        const std::string lvl = "lvl" + std::to_string( i );
        // "<PA_TARGET " (with the space) so PA_TARGET2 is not taken for it.
        ReadAttribute( text, 0, all, "PA_TARGET ", lvl.c_str(), config.PaTarget[i] );
        ReadAttribute( text, 0, all, "PA_TARGET2", lvl.c_str(), config.PaTarget2[i] );
        ReadAttribute( text, 0, all, "PP_TARGET", lvl.c_str(), config.PpTarget[i] );
    }
    size_t begin = 0, end = 0;
    FindSection( text, "ESQUIVE", begin, end );
    ReadAttribute( text, begin, end, "DIST", "min", config.DodgeDistMin );
    ReadAttribute( text, begin, end, "DIST", "max", config.DodgeDistMax );
    ReadAttribute( text, begin, end, "DIST", "pts_min", config.DodgePtsMin );
    ReadAttribute( text, begin, end, "DIST", "pts_max", config.DodgePtsMax );
    ReadAttribute( text, begin, end, "DIFF_SPEED", "min", config.DodgeDiffMin );
    ReadAttribute( text, begin, end, "DIFF_SPEED", "max", config.DodgeDiffMax );
    ReadAttribute( text, begin, end, "DIFF_SPEED", "pts_min", config.DodgeMulMin );
    ReadAttribute( text, begin, end, "DIFF_SPEED", "pts_max", config.DodgeMulMax );
    ReadAttribute( text, begin, end, "SPEED", "value", config.DodgeSpeed );
    FindSection( text, "SAUT", begin, end );
    ReadAttribute( text, begin, end, "HAUTEUR", "min", config.JumpHeightMin );
    ReadAttribute( text, begin, end, "HAUTEUR", "max", config.JumpHeightMax );
    ReadAttribute( text, begin, end, "HAUTEUR", "pts_min", config.JumpHeightPtsMin );
    ReadAttribute( text, begin, end, "HAUTEUR", "pts_max", config.JumpHeightPtsMax );
    ReadAttribute( text, begin, end, "LONGUEUR", "min", config.JumpLengthMin );
    ReadAttribute( text, begin, end, "LONGUEUR", "max", config.JumpLengthMax );
    ReadAttribute( text, begin, end, "LONGUEUR", "pts_min", config.JumpLengthPtsMin );
    ReadAttribute( text, begin, end, "LONGUEUR", "pts_max", config.JumpLengthPtsMax );
    ReadAttribute( text, begin, end, "SPEED", "value", config.JumpSpeed );
    FindSection( text, "DERAPAGE", begin, end );
    ReadAttribute( text, begin, end, "DIST", "min", config.DriftDistMin );
    ReadAttribute( text, begin, end, "DIST", "max", config.DriftDistMax );
    ReadAttribute( text, begin, end, "DIST", "pts_min", config.DriftPtsMin );
    ReadAttribute( text, begin, end, "DIST", "pts_max", config.DriftPtsMax );
    ReadAttribute( text, begin, end, "SPEED", "value", config.DriftSpeed );
    ReadAttribute( text, begin, end, "SLIP_ANGLE", "value", config.DriftSlip );
    OTDU_LOG_INFO( "Risk: frpoints.cpr - PP_TARGET 1 %.0f .. 10 %.0f, PA_TARGET 1 $%.0f .. 10 $%.0f, DEC_SPEED %.0f/%.0f/%.0f/%.0f\n",
                   config.PpTarget[1], config.PpTarget[10], config.PaTarget[1], config.PaTarget[10], config.DecSpeedPts[0],
                   config.DecSpeedPts[1], config.DecSpeedPts[2], config.DecSpeedPts[3] );
    return true;
}

RiskDriving::RiskDriving() { LoadConfig( config ); }

void RiskDriving::selfTest() {
    // At the car's top speed (+8 pts/s) the gauge alone reaches level 1 in
    // 37.5 s; near misses at 1 m and 50 km/h closing (70..150 pts x 1..1.5)
    // fill it much faster. Then a crash (nothing lost), a slow stretch (the
    // gauge drains, the level drops), and the bank.
    RiskDriving risk;
    int32_t paid = 0;
    risk.setOnBank( [ &paid ]( int32_t amount ) { paid += amount; } );
    Frame frame;
    frame.DeltaTime = 1.0f / 60.0f;
    frame.MaxSpeedKmh = 250.0f;
    frame.SpeedKmh = 240.0f;
    for ( int32_t i = 0; i < 60 * 40; i++ ) {
        frame.NearMisses = i % 120 == 60 ? 1 : 0;
        frame.NearMissGap = 1.0f;
        frame.NearMissClosing = 50.0f / 3.6f;
        frame.Z += frame.SpeedKmh / 3.6f * frame.DeltaTime;
        risk.tick( frame );
    }
    OTDU_LOG_INFO( "Risk: selftest 40 s at 240 km/h + a near miss every 2 s: level %d (best %d), gauge %d / %d, bank %s, cur $%d next $%d\n",
                   risk.getLevel(), risk.bestLevel, risk.getProgress(), risk.getThreshold(), risk.isBanking() ? "available" : "no",
                   risk.getMoneyForLevel(), risk.getMoneyForNextLevel() );
    frame.NearMisses = 0;
    frame.bCrash = true;
    risk.tick( frame );
    frame.bCrash = false;
    OTDU_LOG_INFO( "Risk: selftest crash: level %d kept\n", risk.getLevel() );
    const int32_t levelBeforeSlow = risk.getLevel();
    risk.requestBank();
    risk.onBankFSCommand( "frgp:bank" );
    OTDU_LOG_INFO( "Risk: selftest bank at level %d: paid $%d, level now %d\n", levelBeforeSlow, paid, risk.getLevel() );
    // Back to level 2 without banking, then 20 s at 30 km/h: the gauge drains
    // (the pending bank of the new best level holds the level until end_bank).
    for ( int32_t i = 0; i < 60 * 30; i++ ) {
        frame.NearMisses = i % 60 == 30 ? 1 : 0;
        risk.tick( frame );
    }
    risk.onBankFSCommand( "frgp:end_bank" );
    const int32_t levelBeforeDrain = risk.getLevel();
    frame.SpeedKmh = 30.0f;
    frame.NearMisses = 0;
    for ( int32_t i = 0; i < 60 * 20; i++ ) {
        risk.tick( frame );
    }
    OTDU_LOG_INFO( "Risk: selftest 20 s at 30 km/h (rate %.1f /s): level %d -> %d, gauge %d\n", risk.rate( 30.0f, 250.0f ),
                   levelBeforeDrain, risk.getLevel(), risk.getProgress() );
}

void RiskDriving::reset() {
    // 0xa767f0: gauge, level, best level to 0.
    progress = 0.0f;
    level = 0;
    bestLevel = 0;
    bBankAvailable = false;
    bBankRequested = false;
    bankWait = 0.0f;
}

int32_t RiskDriving::getMoneyForLevel() const {
    const float *pTable = bSecondTable ? config.PaTarget2 : config.PaTarget;
    return static_cast<int32_t>( pTable[Clamp( level, 0, 10 )] );
}

int32_t RiskDriving::getMoneyForNextLevel() const {
    const float *pTable = bSecondTable ? config.PaTarget2 : config.PaTarget;
    return level <= 9 ? static_cast<int32_t>( pTable[level + 1] ) : 0;
}

float RiskDriving::rate( float speedKmh, float maxSpeedKmh ) const {
    // 0x7cae18 (PS3) / 0xa75920: the breakpoints are val_i x the car's top
    // speed, piecewise linear between them; below the first / above the last
    // the end values [INF: the clamp].
    const float scale = maxSpeedKmh > 1.0f ? maxSpeedKmh : 250.0f;
    const float *pVal = config.DecSpeedVal;
    const float *pPts = config.DecSpeedPts;
    if ( speedKmh <= pVal[0] * scale ) {
        return pPts[0];
    }
    for ( int32_t i = 0; i < 3; i++ ) {
        if ( speedKmh <= pVal[i + 1] * scale ) {
            return Remap( speedKmh, pVal[i] * scale, pVal[i + 1] * scale, pPts[i], pPts[i + 1] );
        }
    }
    return pPts[3];
}

void RiskDriving::addPerfEvent( float points, const char *pPerfType ) {
    // 0xa752fc: the trick's points go into the gauge (below level 10), and to
    // the LastPts badge / its icon.
    const float scaled = points * config.CalculPts;
    if ( level < 10 ) {
        progress += scaled;
    }
    lastPoints = static_cast<int32_t>( scaled );
    perfType = pPerfType;
    bNewPerf = true;
    updateLevel();
}

void RiskDriving::updateLevel() {
    // 0xa769e0: level up while the gauge reaches the next target (the rest
    // carries over); a new best level makes the bank available.
    while ( level < 10 && progress >= config.PpTarget[level + 1] ) {
        progress -= config.PpTarget[level + 1];
        level++;
        if ( level > bestLevel ) {
            bestLevel = level;
            bBankAvailable = true;
        }
    }
    if ( level >= 10 ) {
        progress = 0.0f;
    }
    // 0xa76ae4: below 0 the level drops (unless a bank is pending); at level 0
    // the gauge stays at 0.
    while ( progress < 0.0f && level > 0 && !bBankAvailable ) {
        level--;
        progress += config.PpTarget[level + 1];
    }
    if ( progress < 0.0f && level == 0 ) {
        progress = 0.0f;
    }
}

void RiskDriving::bank() {
    // 0xa76e50 "bank": total += money[level]; AddMoney(amount, 0, 0x14); then
    // the chain reset 0xa767f0.
    const int32_t amount = getMoneyForLevel();
    total += amount;
    if ( onBank && amount > 0 ) {
        onBank( amount );
    }
    OTDU_LOG_INFO( "Risk: bank at level %d -> $%d (total $%d)\n", level, amount, total );
    reset();
}

void RiskDriving::requestBank() {
    if ( bBankAvailable && !bBankRequested ) {
        bBankRequested = true;
        bankWait = 0.0f;
    }
}

void RiskDriving::onBankFSCommand( const std::string &command ) {
    if ( command == "frgp:bank" ) {
        if ( bBankAvailable ) {
            bank();
        } else {
            OTDU_LOG_INFO( "Risk: frgp:bank without a bank available\n" );
        }
    } else if ( command == "frgp:end_bank" ) {
        // The prompt is gone: the level can drop again.
        bBankAvailable = false;
        bBankRequested = false;
    }
}

int32_t RiskDriving::tick( const Frame &frame ) {
    const float dt = frame.DeltaTime;
    // The hit: the HUD "hit" for 2 s, nothing else [CODE 0xa76bd0].
    if ( frame.bCrash && hitTimer <= 0.0f ) {
        bHit = true;
        hitTimer = kHitTime;
    }
    hitTimer = Max( 0.0f, hitTimer - dt );
    // The gauge rate (0xa767c0), below level 10.
    if ( level < 10 ) {
        progress += rate( frame.SpeedKmh, frame.MaxSpeedKmh ) * config.CalculPts * dt;
    }
    updateLevel();
    // ESQUIVE: a near miss at or above SPEED, by gap and closing speed.
    for ( int32_t i = 0; i < frame.NearMisses; i++ ) {
        if ( frame.SpeedKmh >= config.DodgeSpeed && frame.NearMissGap <= config.DodgeDistMin ) {
            const float points = Remap( frame.NearMissGap, config.DodgeDistMin, config.DodgeDistMax, config.DodgePtsMin, config.DodgePtsMax );
            const float factor = Remap( frame.NearMissClosing * 3.6f, config.DodgeDiffMin, config.DodgeDiffMax, config.DodgeMulMin, config.DodgeMulMax );
            addPerfEvent( points * factor, "dodge" );
        }
    }
    // SAUT: height above the take-off and length on landing [INF: the sum of
    // the two scores; the retail jump detector is [ENC]].
    if ( !frame.bOnGround ) {
        if ( !bAirborne ) {
            bAirborne = true;
            takeoffX = frame.X;
            takeoffY = frame.Y;
            takeoffZ = frame.Z;
            takeoffSpeed = frame.SpeedKmh;
            peakY = frame.Y;
        }
        peakY = Max( peakY, frame.Y );
    } else if ( bAirborne ) {
        bAirborne = false;
        const float height = peakY - takeoffY;
        const float dx = frame.X - takeoffX, dz = frame.Z - takeoffZ;
        const float length = std::sqrt( dx * dx + dz * dz );
        if ( takeoffSpeed >= config.JumpSpeed && ( height >= config.JumpHeightMin || length >= config.JumpLengthMin ) ) {
            float points = 0.0f;
            if ( height >= config.JumpHeightMin ) {
                points += Remap( height, config.JumpHeightMin, config.JumpHeightMax, config.JumpHeightPtsMin, config.JumpHeightPtsMax );
            }
            if ( length >= config.JumpLengthMin ) {
                points += Remap( length, config.JumpLengthMin, config.JumpLengthMax, config.JumpLengthPtsMin, config.JumpLengthPtsMax );
            }
            addPerfEvent( points, "jump" );
        }
    }
    // DERAPAGE: the distance driven at or above SLIP_ANGLE and SPEED, scored
    // when the slide ends [INF: the moment of the score].
    const float stepX = bHasLast ? frame.X - lastX : 0.0f, stepZ = bHasLast ? frame.Z - lastZ : 0.0f;
    if ( frame.bOnGround && frame.SlipAngle >= config.DriftSlip && frame.SpeedKmh >= config.DriftSpeed ) {
        driftDistance += std::sqrt( stepX * stepX + stepZ * stepZ );
    } else if ( driftDistance > 0.0f ) {
        if ( driftDistance >= config.DriftDistMin ) {
            addPerfEvent( Remap( driftDistance, config.DriftDistMin, config.DriftDistMax, config.DriftPtsMin, config.DriftPtsMax ), "drift" );
        }
        driftDistance = 0.0f;
    }
    lastX = frame.X;
    lastZ = frame.Z;
    bHasLast = true;
    // The player's bank: paid on the movie's frgp:bank, or after a short wait.
    if ( bBankRequested ) {
        bankWait += dt;
        if ( bankWait >= kBankWait && bBankAvailable ) {
            OTDU_LOG_INFO( "Risk: no frgp:bank from the movie after the player's bank, paying\n" );
            bank();
        }
    }
    return 0;
}
