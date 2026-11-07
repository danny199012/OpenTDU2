#include "shared.h"
#include "casino_slots.h"

#include "core/watchdog.h"
#include "render/renderer_2d.h"

#include <cmath>
#include <cstdio>

namespace {
    // Reel stop animation: the first reel stops at kFirstStop, the next every
    // kStopGap (the retail movie plays one 'stop' beat per reel:
    // C_SLOT_ACTIVATE then the win / loose beat).
    constexpr float kFirstStop = 0.35f;
    constexpr float kStopGap = 0.16f;
    // How fast a turning reel cycles its symbols.
    constexpr float kRollSpeed = 22.0f;

    // Reel strip symbol counts [INF]: the five reels share one 64-stop strip,
    // so the return is exact and closed form (analyticRtp). The counts are the
    // only tuning knob for the return.
    constexpr int32_t
        kStripCounts[static_cast<int32_t>( CasinoSlots::eSymbol::Count )] = {
            14, // Cherry
            12, // Lemon
            11, // Bell
            10, // Bar
            8,  // Seven
            6,  // Diamond
            3,  // Jackpot
    };

    // Paytable in line-bet multiples [INF] (docs/re/casino_spec.md; the retail
    // table was server side). 0 = no win.
    constexpr int32_t kPay3[static_cast<int32_t>(
        CasinoSlots::eSymbol::Count )] = { 10, 15, 25, 30, 60, 80, 0 };
    constexpr int32_t kPay4[static_cast<int32_t>(
        CasinoSlots::eSymbol::Count )] = { 30, 45, 75, 100, 200, 250, 0 };
    constexpr int32_t kPay5[static_cast<int32_t>(
        CasinoSlots::eSymbol::Count )] = { 90, 150, 250, 300, 600, 1000, 5000 };

    // 5 x 3 paylines: the row index read on each reel (0 top .. 2 bottom).
    constexpr int32_t kPaylines[CasinoSlots::kLines][CasinoSlots::kReels] = {
        {1, 1, 1, 1, 1},
        {0, 0, 0, 0, 0},
        {2, 2, 2, 2, 2},
        {0, 1, 2, 1, 0},
        {2, 1, 0, 1, 2},
        {0, 0, 1, 0, 0},
        {2, 2, 1, 2, 2},
        {1, 0, 0, 0, 1},
        {1, 2, 2, 2, 1},
    };

    constexpr float kCellWidth = 96.0f;
    constexpr float kCellHeight = 64.0f;
    constexpr float kCellGap = 6.0f;

    // The retail CasinoSlotMachineTypeDesc of each slot machine type
    // [DATA: Euro/Bnk/Interior/icasslo1.bnk entities.xmb]: Price (the stake
    // of one spin), NbLines and NbDifferentSymbol. Index = eMachine.
    struct MachineInfo {
        const char *pName;
        int32_t Price;
        int32_t Lines;
        int32_t Symbols;
    };
    constexpr MachineInfo kMachines[static_cast<int32_t>( CasinoSlots::eMachine::Count )] = {
        { "SlotMachineCar",      100, 1, 6  },
        { "SlotMachineClothes",   10, 1, 4  },
        { "SlotMachinePoker",      5, 1, 8  },
        { "SlotMachineHawai",     15, 3, 8  },
        { "SlotMachineClassic",    2, 3, 8  },
        { "SlotMachineHighLife",  10, 9, 8  },
    };
} // namespace

const int32_t ( &CasinoSlots::getPaylines() )
    [CasinoSlots::kLines][CasinoSlots::kReels] { return kPaylines; }

const char *CasinoSlots::getSymbolName( eSymbol symbol ) {
    switch ( symbol ) {
    case eSymbol::Cherry:
        return "Cherry";
    case eSymbol::Lemon:
        return "Lemon";
    case eSymbol::Bell:
        return "Bell";
    case eSymbol::Bar:
        return "Bar";
    case eSymbol::Seven:
        return "Seven";
    case eSymbol::Diamond:
        return "Diamond";
    case eSymbol::Jackpot:
        return "Jackpot";
    default:
        return "?";
    }
}

const char *CasinoSlots::getSymbolGlyph( eSymbol symbol ) {
    switch ( symbol ) {
    case eSymbol::Cherry:
        return "C";
    case eSymbol::Lemon:
        return "L";
    case eSymbol::Bell:
        return "B";
    case eSymbol::Bar:
        return "R";
    case eSymbol::Seven:
        return "7";
    case eSymbol::Diamond:
        return "D";
    case eSymbol::Jackpot:
        return "J";
    default:
        return "?";
    }
}

uint32_t CasinoSlots::getSymbolColor( eSymbol symbol ) {
    switch ( symbol ) {
    case eSymbol::Cherry:
        return MakeRGBA( 200, 40, 50 );
    case eSymbol::Lemon:
        return MakeRGBA( 220, 200, 50 );
    case eSymbol::Bell:
        return MakeRGBA( 220, 140, 40 );
    case eSymbol::Bar:
        return MakeRGBA( 200, 200, 210 );
    case eSymbol::Seven:
        return MakeRGBA( 140, 60, 200 );
    case eSymbol::Diamond:
        return MakeRGBA( 60, 200, 220 );
    case eSymbol::Jackpot:
        return MakeRGBA( 60, 190, 90 );
    default:
        return MakeRGBA( 120, 120, 120 );
    }
}

CasinoSlots::CasinoSlots()
    : spinTime( 0.0f ),
      bSpinning( false ),
      betPerLine( 1 ),
      chips( 0 ),
      totalWon( 0 ),
      totalWagered( 0 ),
      spins( 0 ),
      rng( 0x9E3779B97F4A7C15ull ) {
    buildStrips();
    // An idle grid so the window is not empty before the first spin.
    for ( int32_t reel = 0; reel < kReels; reel++ ) {
        stop[reel] = static_cast<int32_t>( nextRandom() % stripSize() );
        reelStopped[reel] = true;
        for ( int32_t row = 0; row < kRows; row++ ) {
            grid[reel][row] = strip[( stop[reel] + row ) % stripSize()];
            display[reel][row] = grid[reel][row];
        }
    }
}

void CasinoSlots::seed( uint32_t value ) {
    if ( value != 0 ) {
        rng = 0x9E3779B97F4A7C15ull ^
              ( static_cast<uint64_t>( value ) * 0x2545F4914F6CDD1Dull );
    }
}

void CasinoSlots::buildStrips() {
    // Spread the counts as evenly as possible: at every step take the symbol
    // with the most stops left to place. Deterministic, so the strip (and the
    // return) is the same in every run. The machine's symbol set keeps only
    // its first N symbols (SlotMachineClothes: 4, SlotMachineCar: 6) [DATA].
    const int32_t symbolCount = getSymbolCount();
    int32_t remaining[static_cast<int32_t>( eSymbol::Count )];
    int32_t total = 0;
    for ( int32_t s = 0; s < static_cast<int32_t>( eSymbol::Count ); s++ ) {
        remaining[s] = s < symbolCount ? kStripCounts[s] : 0;
        total += remaining[s];
    }
    strip.clear();
    strip.reserve( kStripLength );
    for ( int32_t placed = 0; placed < kStripLength; placed++ ) {
        int32_t best = -1;
        for ( int32_t s = 0; s < static_cast<int32_t>( eSymbol::Count ); s++ ) {
            if ( remaining[s] > 0 &&
                 ( best < 0 || remaining[s] > remaining[best] ) ) {
                best = s;
            }
        }
        if ( best < 0 ) {
            break;
        }
        // Two stops of the same symbol in a row would look wrong on the reel.
        if ( placed > 0 && best == static_cast<int32_t>( strip.back() ) ) {
            for ( int32_t s = 0; s < static_cast<int32_t>( eSymbol::Count );
                  s++ ) {
                if ( s != best && remaining[s] > 0 ) {
                    best = s;
                    break;
                }
            }
        }
        strip.push_back( static_cast<eSymbol>( best ) );
        remaining[best]--;
    }
    if ( static_cast<int32_t>( strip.size() ) < kStripLength ) {
        // Expected for a machine with fewer symbols (Clothes 4 -> 47 stops,
        // Car 6 -> 61): every read wraps at stripSize().
        OTDU_LOG_DEBUG( "CasinoSlots: %zu-stop strip (%d symbols)\n", strip.size(), getSymbolCount() );
    }
    (void)total;
}

int32_t CasinoSlots::getSymbolCount() const {
    return kMachines[static_cast<int32_t>( machine )].Symbols;
}

const char *CasinoSlots::getMachineName( eMachine machine ) {
    return kMachines[static_cast<int32_t>( machine )].pName;
}

int32_t CasinoSlots::getMachinePrice() const {
    return kMachines[static_cast<int32_t>( machine )].Price;
}

void CasinoSlots::setReelLayout( int32_t reels, float firstStop, float stopGap ) {
    reelCount = reels >= 3 && reels <= kReels ? reels : kReels;
    firstStopTime = Max( 0.0f, firstStop );
    stopGapTime = Max( 0.0f, stopGap );
    bReelStops = false;
    OTDU_LOG_INFO( "CasinoSlots: %d reels, first stop %.2f s, then every %.2f s\n", reelCount,
                   firstStopTime > 0.0f ? firstStopTime : kFirstStop, stopGapTime > 0.0f ? stopGapTime : kStopGap );
}

int32_t CasinoSlots::getMaxLines() const {
    return Clamp<int32_t>( kMachines[static_cast<int32_t>( machine )].Lines, 1, kLines );
}

void CasinoSlots::setActiveLines( int32_t lines ) {
    linesActive = Clamp<int32_t>( lines, 1, getMaxLines() );
}

void CasinoSlots::setReelStops( const std::vector<float> &stopTimes ) {
    bReelStops = !stopTimes.empty();
    for ( int32_t reel = 0; reel < kReels; reel++ ) {
        reelStopTimes[reel] = reel < static_cast<int32_t>( stopTimes.size() ) ? stopTimes[reel] : 0.0f;
    }
}

void CasinoSlots::setMachine( eMachine newMachine ) {
    machine = newMachine;
    linesActive = Clamp<int32_t>( kMachines[static_cast<int32_t>( machine )].Lines, 1, kLines );
    prize = ePrize::None;
    buildStrips();
    // The idle grid follows the new symbol set.
    for ( int32_t reel = 0; reel < kReels; reel++ ) {
        for ( int32_t row = 0; row < kRows; row++ ) {
            grid[reel][row] = strip[( stop[reel] + row ) % stripSize()];
            display[reel][row] = grid[reel][row];
        }
    }
    OTDU_LOG_INFO( "CasinoSlots: machine '%s' (%d chips a spin, %d line(s), %d symbols)\n",
                   getMachineName( machine ), getMachinePrice(), linesActive, getSymbolCount() );
}

uint32_t CasinoSlots::nextRandom() {
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return static_cast<uint32_t>( ( rng * 0x2545F4914F6CDD1Dull ) >> 32 );
}

void CasinoSlots::setBetPerLine( int32_t value ) {
    betPerLine = Clamp<int32_t>( value, 1, kMaxBetPerLine );
}

void CasinoSlots::stepBetPerLine( int32_t delta ) {
    int32_t value = betPerLine + delta;
    if ( value > kMaxBetPerLine ) {
        value = 1;
    } else if ( value < 1 ) {
        value = kMaxBetPerLine;
    }
    betPerLine = value;
}

bool CasinoSlots::spin() {
    if ( bSpinning || chips < getWager() ) {
        return false;
    }
    chips -= getWager();
    totalWagered += getWager();
    spins++;
    last.Wins.clear();
    last.Wagered = getWager();
    last.Payout = 0;
    for ( int32_t reel = 0; reel < kReels; reel++ ) {
        stop[reel] = static_cast<int32_t>( nextRandom() % stripSize() );
        reelStopped[reel] = false;
        for ( int32_t row = 0; row < kRows; row++ ) {
            grid[reel][row] = strip[( stop[reel] + row ) % stripSize()];
        }
    }
    spinTime = 0.0f;
    bSpinning = true;
    OTDU_LOG_INFO( "CasinoSlots: spin %d, bet %d/line (%d chips), balance %d\n",
                   spins, betPerLine, getWager(), chips );
    return true;
}

bool CasinoSlots::respin( const bool ( &held )[kReels], bool bPay ) {
    if ( bSpinning ) {
        return false;
    }
    for ( int32_t reel = 0; reel < kReels; reel++ ) {
        if ( held[reel] && reel < reelCount ) {
            reelStopped[reel] = true; // stays on its symbols
            continue;
        }
        stop[reel] = static_cast<int32_t>( nextRandom() % stripSize() );
        reelStopped[reel] = false;
        for ( int32_t row = 0; row < kRows; row++ ) {
            grid[reel][row] = strip[( stop[reel] + row ) % stripSize()];
        }
    }
    last.Wins.clear();
    last.Payout = 0;
    spinTime = 0.0f;
    bSpinning = true;
    bPayThisSpin = bPay;
    OTDU_LOG_INFO( "CasinoSlots: second chance, held %d%d%d%d%d\n", held[0], held[1], held[2], held[3], held[4] );
    return true;
}

bool CasinoSlots::tick( float deltaTime ) {
    if ( !bSpinning ) {
        return false;
    }
    spinTime += deltaTime;
    const int32_t roll = static_cast<int32_t>( spinTime * kRollSpeed );
    bool bAllStopped = true;
    for ( int32_t reel = 0; reel < kReels; reel++ ) {
        const float first = firstStopTime > 0.0f ? firstStopTime : kFirstStop;
        const float gap = stopGapTime > 0.0f ? stopGapTime : kStopGap;
        const float stopAt = bReelStops && reel < reelCount ? reelStopTimes[reel] : first + reel * gap;
        if ( !reelStopped[reel] && ( reel >= reelCount || spinTime >= stopAt ) ) {
            reelStopped[reel] = true;
        }
        for ( int32_t row = 0; row < kRows; row++ ) {
            if ( reelStopped[reel] ) {
                display[reel][row] = grid[reel][row];
            } else {
                const int32_t at =
                    ( stop[reel] + roll + reel * 7 + row ) % stripSize();
                display[reel][row] =
                    strip[( at + stripSize() ) % stripSize()];
            }
        }
        if ( !reelStopped[reel] ) {
            bAllStopped = false;
        }
    }
    if ( !bAllStopped ) {
        return true;
    }
    bSpinning = false;
    evaluate();
    if ( !bPayThisSpin ) {
        // A first draw of a second chance machine: the result is shown and
        // kept, the pay waits for the respin.
        bPayThisSpin = true;
        OTDU_LOG_INFO( "CasinoSlots: first draw settled (%d chips on %zu line(s) if kept), balance %d\n", last.Payout,
                       last.Wins.size(), chips );
        return false;
    }
    chips += last.Payout;
    totalWon += last.Payout;
    if ( last.Payout > 0 ) {
        OTDU_LOG_INFO( "CasinoSlots: win %d chips on %zu line(s), balance %d\n",
                       last.Payout, last.Wins.size(), chips );
    }
    return false;
}

int32_t CasinoSlots::payFor( eSymbol symbol, int32_t count ) const {
    const int32_t index = static_cast<int32_t>( symbol );
    if ( index < 0 || index >= static_cast<int32_t>( eSymbol::Count ) ) {
        return 0;
    }
    if ( count >= 5 ) {
        return kPay5[index];
    }
    if ( count == 4 ) {
        return kPay4[index];
    }
    if ( count == 3 ) {
        return kPay3[index];
    }
    return 0;
}

void CasinoSlots::evaluate() {
    for ( int32_t reel = 0; reel < kReels; reel++ ) {
        for ( int32_t row = 0; row < kRows; row++ ) {
            last.Grid[reel][row] = grid[reel][row];
        }
    }
    last.Payout = 0;
    last.Wins.clear();
    prize = ePrize::None;
    // The machine's share of the paylines: 1 (the middle row), 3 (middle, top,
    // bottom) or 9 (all) [DATA: NbLines]; the activated lines are the first
    // linesActive of them. The paytable is per line-bet: a per-line machine
    // stakes Price on each activated line and each line pays paytable x bet
    // level x Price; a one-line machine is the same with one line. Either way
    // the return stays the paytable's (~96%) [INF].
    const int32_t lines = Clamp<int32_t>( linesActive, 1, kLines );
    for ( int32_t line = 0; line < lines; line++ ) {
        const eSymbol symbol = grid[0][kPaylines[line][0]];
        int32_t count = 1;
        while ( count < reelCount && grid[count][kPaylines[line][count]] == symbol ) {
            count++;
        }
        if ( count < 3 ) {
            continue;
        }
        const int64_t payout64 =
            static_cast<int64_t>( payFor( symbol, count ) ) * betPerLine * getMachinePrice();
        const int32_t payout = static_cast<int32_t>( payout64 );
        if ( payout <= 0 ) {
            continue;
        }
        Win win;
        win.Line = line;
        win.Symbol = symbol;
        win.Count = count;
        win.Payout = payout;
        last.Wins.push_back( win );
        last.Payout += payout;
    }
    // The car and clothes machines pay their prize on the first line with
    // three of the machine's top symbol (the retail resolve table came from
    // the server; this rule is [INF], docs/re/casino_spec.md 3.1).
    const eSymbol topSymbol = static_cast<eSymbol>( getSymbolCount() - 1 );
    if ( machine == eMachine::Car || machine == eMachine::Clothes ) {
        if ( grid[0][kPaylines[0][0]] == topSymbol && grid[1][kPaylines[0][1]] == topSymbol &&
             grid[2][kPaylines[0][2]] == topSymbol ) {
            prize = machine == eMachine::Car ? ePrize::Car : ePrize::Clothes;
        }
    }
}

double CasinoSlots::analyticRtp() {
    double rtp = 0.0;
    for ( int32_t s = 0; s < static_cast<int32_t>( eSymbol::Count ); s++ ) {
        const double p = static_cast<double>( kStripCounts[s] ) /
                         static_cast<double>( kStripLength );
        const double exactly3 = p * p * p * ( 1.0 - p );
        const double exactly4 = p * p * p * p * ( 1.0 - p );
        const double exactly5 = p * p * p * p * p;
        rtp += exactly3 * kPay3[s] + exactly4 * kPay4[s] + exactly5 * kPay5[s];
    }
    return rtp;
}

void CasinoSlots::selfTest( int32_t numSpins ) {
    CasinoSlots machine;
    // The 9-line machine of the port (SlotMachineHighLife) with enough chips
    // for the run: its 10-chip stake is the smallest one of the six.
    machine.setMachine( eMachine::HighLife );
    machine.seed( 1u );
    const int32_t startChips = 100000000;
    machine.setChips( startChips );
    machine.setBetPerLine( 1 );
    int32_t wins = 0;
    int32_t best = 0;
    for ( int32_t i = 0; i < numSpins; i++ ) {
        if ( ( i & 1023 ) == 0 ) {
            Watchdog::Beat( "slotselftest", static_cast<uint64_t>( i ) );
        }
        if ( !machine.spin() ) {
            break;
        }
        // Fast forward: the animation is only presentation.
        while ( machine.tick( 0.1f ) ) {
        }
        if ( machine.getLastPayout() > 0 ) {
            wins++;
            best = Max( best, machine.getLastPayout() );
        }
    }
    const double wagered = static_cast<double>( machine.getTotalWagered() );
    const double won = static_cast<double>( machine.getTotalWon() );
    OTDU_LOG_INFO(
        "CasinoSlots self test: %d spins, wagered %d, won %d, measured RTP "
        "%.2f%%, analytic RTP %.2f%%\n",
        machine.getSpins(), machine.getTotalWagered(), machine.getTotalWon(),
        wagered > 0.0 ? 100.0 * won / wagered : 0.0, 100.0 * analyticRtp() );
    OTDU_LOG_INFO(
        "CasinoSlots self test: %d winning spins (%.1f%%), best win %d chips\n",
        wins, machine.getSpins() > 0 ? 100.0 * wins / machine.getSpins() : 0.0,
        best );
    // The wallet arithmetic must balance: chips == start - wagered + won.
    const int32_t expected =
        startChips - machine.getTotalWagered() + machine.getTotalWon();
    OTDU_LOG_INFO( "CasinoSlots self test: balance %d, expected %d (%s)\n",
                   machine.getChips(), expected,
                   machine.getChips() == expected ? "ok" : "MISMATCH" );
}

void CasinoSlots::getWindowSize( float scale,
                                 float &outWidth,
                                 float &outHeight ) {
    outWidth = ( kCellWidth + kCellGap ) * kReels * scale;
    outHeight = ( kCellHeight + kCellGap ) * kRows * scale;
}

void CasinoSlots::draw( Renderer2D &renderer,
                        const Font *pFont,
                        float x,
                        float y,
                        float scale ) const {
    const float cellWidth = kCellWidth * scale;
    const float cellHeight = kCellHeight * scale;
    const float gap = kCellGap * scale;
    // Frame + reel window.
    renderer.drawRect( Rect2D( x - 8.0f * scale, y - 8.0f * scale,
                               ( cellWidth + gap ) * kReels + 16.0f * scale,
                               ( cellHeight + gap ) * kRows + 16.0f * scale ),
                       MakeRGBA( 24, 26, 40 ) );
    for ( int32_t reel = 0; reel < kReels; reel++ ) {
        for ( int32_t row = 0; row < kRows; row++ ) {
            const float cx = x + reel * ( cellWidth + gap );
            const float cy = y + row * ( cellHeight + gap );
            const eSymbol symbol = display[reel][row];
            bool bWinning = false;
            for ( const Win &win : last.Wins ) {
                if ( !bSpinning && kPaylines[win.Line][reel] == row ) {
                    bWinning = true;
                    break;
                }
            }
            renderer.drawRect( Rect2D( cx, cy, cellWidth, cellHeight ),
                               MakeRGBA( 46, 48, 66 ) );
            renderer.drawRect( Rect2D( cx + 3.0f, cy + 3.0f, cellWidth - 6.0f,
                                       cellHeight - 6.0f ),
                               MultiplyAlpha( getSymbolColor( symbol ),
                                              bWinning ? 1.0f : 0.55f ) );
            if ( bWinning ) {
                renderer.drawRectOutline(
                    Rect2D( cx - 2.0f, cy - 2.0f, cellWidth + 4.0f,
                            cellHeight + 4.0f ),
                    2.0f * scale, MakeRGBA( 255, 220, 80 ) );
            }
            if ( pFont != nullptr ) {
                renderer.drawText(
                    pFont, getSymbolGlyph( symbol ), cx + cellWidth * 0.5f,
                    cy + cellHeight * 0.5f, 26.0f * scale,
                    MakeRGBA( 250, 250, 255 ), eTextAlign::TA_Center );
            }
        }
    }
}
