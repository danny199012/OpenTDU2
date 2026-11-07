#include "shared.h"
#include "casino_roulette.h"

#include "core/watchdog.h"
#include "render/renderer_2d.h"

#include <cstdio>

namespace {
    // The table phases run on a timer offline (retail waited for the other
    // players of the session; CASROU_NoTime / CASROU_BetsAreDone are the dealer
    // beats).
    constexpr float kBetCloseTime = 1.2f; // BetsAreDone -> RienNeVaPlus
    constexpr float kSpinTime = 2.0f;     // RienNeVaPlus -> Winner
    constexpr float kResultTime = 3.5f;   // Winner -> PlaceYourBet

    constexpr float kCellWidth = 44.0f;
    constexpr float kCellHeight = 30.0f;
    constexpr float kOutsideHeight = 26.0f;
    constexpr float kOutsideGap = 5.0f;
    constexpr float kZeroWidth = 44.0f;

    // The wheel's own order (single zero / European) [DATA: the standard wheel;
    // the client names the bets, not the order].
    constexpr int32_t kWheel[37] = {
        0, 32, 15, 19, 4, 21, 2,  25, 17, 34, 6,  27, 13, 36, 11, 30, 8, 23, 10,
        5, 24, 16, 33, 1, 20, 14, 31, 9,  22, 18, 29, 7,  28, 12, 35, 3, 26 };

    // Cell layout: 0..36 = the numbers, then the outside bets.
    constexpr int32_t kCellRed = 37;
    constexpr int32_t kCellBlack = 38;
    constexpr int32_t kCellOdd = 39;
    constexpr int32_t kCellEven = 40;
    constexpr int32_t kCellLow = 41;
    constexpr int32_t kCellHigh = 42;
    constexpr int32_t kCellDozen1 = 43;
    constexpr int32_t kCellColumn1 = 46;
    constexpr int32_t kCellCount = 49;

    constexpr const char *kOutsideLabels[12] = {
        "RED",   "BLACK", "ODD",   "EVEN",  "1-18",  "19-36",
        "1st12", "2nd12", "3rd12", "COL 1", "COL 2", "COL 3" };

    bool IsRedNumber( int32_t number ) {
        static const int32_t kRed[18] = { 1,  3,  5,  7,  9,  12, 14, 16, 18,
                                          19, 21, 23, 25, 27, 30, 32, 34, 36 };
        for ( int32_t red : kRed ) {
            if ( red == number ) {
                return true;
            }
        }
        return false;
    }
} // namespace

const int32_t ( &CasinoRoulette::getWheel() )[37] { return kWheel; }

bool CasinoRoulette::isRed( int32_t number ) { return IsRedNumber( number ); }

const char *CasinoRoulette::getBetName( eBet bet ) {
    switch ( bet ) {
    case eBet::Straight:
        return "Straight";
    case eBet::Red:
        return "Red"; // BET_RED
    case eBet::Black:
        return "Black"; // BET_BLACK
    case eBet::Odd:
        return "Odd";
    case eBet::Even:
        return "Even";
    case eBet::Low:
        return "1-18"; // BET_1TO18
    case eBet::High:
        return "19-36"; // BET_19TO36
    case eBet::Dozen1:
        return "1st 12";
    case eBet::Dozen2:
        return "2nd 12";
    case eBet::Dozen3:
        return "3rd 12";
    case eBet::Column1:
        return "Column 1";
    case eBet::Column2:
        return "Column 2";
    case eBet::Column3:
        return "Column 3";
    default:
        return "?";
    }
}

int32_t CasinoRoulette::cellNumber( int32_t cell ) {
    return cell >= 0 && cell <= 36 ? cell : -1;
}

CasinoRoulette::eBet CasinoRoulette::cellBet( int32_t cell ) {
    switch ( cell ) {
    case kCellRed:
        return eBet::Red;
    case kCellBlack:
        return eBet::Black;
    case kCellOdd:
        return eBet::Odd;
    case kCellEven:
        return eBet::Even;
    case kCellLow:
        return eBet::Low;
    case kCellHigh:
        return eBet::High;
    case kCellDozen1:
        return eBet::Dozen1;
    case kCellDozen1 + 1:
        return eBet::Dozen2;
    case kCellDozen1 + 2:
        return eBet::Dozen3;
    case kCellColumn1:
        return eBet::Column1;
    case kCellColumn1 + 1:
        return eBet::Column2;
    case kCellColumn1 + 2:
        return eBet::Column3;
    default:
        return eBet::Straight;
    }
}

bool CasinoRoulette::cellFromCursor( int32_t col,
                                     int32_t row,
                                     int32_t &outCell ) {
    if ( row >= 0 && row <= 2 ) {
        if ( col == -1 ) {
            outCell = 0;
            return true;
        }
        if ( col >= 0 && col < kGridCols ) {
            // Row 0 reads 3,6,..36; row 1 reads 2,5,..35; row 2 reads 1,4,..34.
            outCell = col * 3 + ( row == 0 ? 3 : row == 1 ? 2 : 1 );
            return outCell >= 1 && outCell <= 36;
        }
        return false;
    }
    if ( col >= 0 && col < 6 ) {
        if ( row == 3 ) {
            outCell = kCellRed + col;
            return true;
        }
        if ( row == 4 ) {
            outCell = kCellDozen1 + col;
            return true;
        }
    }
    return false;
}

void CasinoRoulette::cursorFromCell( int32_t cell,
                                     int32_t &outCol,
                                     int32_t &outRow ) {
    if ( cell == 0 ) {
        outCol = -1;
        outRow = 1;
        return;
    }
    if ( cell >= 1 && cell <= 36 ) {
        outCol = ( cell - 1 ) / 3;
        outRow = 2 - ( ( cell - 1 ) % 3 );
        return;
    }
    if ( cell >= kCellRed && cell < kCellRed + 6 ) {
        outCol = cell - kCellRed;
        outRow = 3;
        return;
    }
    outCol = Clamp<int32_t>( cell - kCellDozen1, 0, 5 );
    outRow = 4;
}

CasinoRoulette::CasinoRoulette() : rng( 0x9E3779B97F4A7C15ull ) {}

void CasinoRoulette::seed( uint32_t value ) {
    if ( value != 0 ) {
        rng = 0x9E3779B97F4A7C15ull ^
              ( static_cast<uint64_t>( value ) * 0x2545F4914F6CDD1Dull );
    }
}

void CasinoRoulette::stepDenomination( int32_t delta ) {
    denomination = ( denomination + delta + 4 ) % 4;
}

void CasinoRoulette::setTablePrice( int32_t price ) {
    // The table minimum is the CasinoRouletteTypeDesc Price (5 / 20 / 25 / 50 /
    // 100 in the retail room) [DATA]. The felt's plaques are 1 / 5 / 25 / 100,
    // so the first one at or above the minimum is the table's smallest chip
    // [INF: the retail plaque set of a table is not in the data].
    tablePrice = Max<int32_t>( 1, price );
    denomination = 3;
    for ( int32_t i = 0; i < 4; i++ ) {
        if ( denominations[i] >= tablePrice ) {
            denomination = i;
            break;
        }
    }
    OTDU_LOG_INFO( "CasinoRoulette: table minimum %d, chip %d\n", tablePrice, denominations[denomination] );
}

bool CasinoRoulette::placeBet( int32_t cell ) {
    if ( phase != ePhase::PlaceYourBet || cell < 0 || cell >= kCellCount ) {
        return false;
    }
    const int32_t amount = denominations[denomination];
    if ( amount < tablePrice ) {
        return false; // below the table's minimum
    }
    if ( chips < amount ) {
        return false;
    }
    chips -= amount;
    staked += amount;
    for ( Placed &placed : bets ) {
        if ( placed.Cell == cell ) {
            placed.Amount += amount;
            return true;
        }
    }
    Placed placed;
    placed.Cell = cell;
    placed.Bet = cellBet( cell );
    placed.Number = cellNumber( cell );
    placed.Amount = amount;
    bets.push_back( placed );
    return true;
}

void CasinoRoulette::clearBets() {
    if ( phase != ePhase::PlaceYourBet ) {
        return;
    }
    chips += staked;
    staked = 0;
    bets.clear();
}

bool CasinoRoulette::spin() {
    if ( phase != ePhase::PlaceYourBet || bets.empty() ) {
        return false;
    }
    phase = ePhase::BetsAreDone;
    phaseTime = 0.0f;
    winningBets.clear();
    OTDU_LOG_INFO( "CasinoRoulette: %zu bet(s), %d chips staked, balance %d\n",
                   bets.size(), staked, chips );
    return true;
}

bool CasinoRoulette::rebet() {
    if ( phase != ePhase::PlaceYourBet || lastBets.empty() ) {
        return false;
    }
    int32_t total = 0;
    for ( const Placed &placed : lastBets ) {
        total += placed.Amount;
    }
    if ( chips < total ) {
        return false;
    }
    clearBets();
    for ( const Placed &placed : lastBets ) {
        chips -= placed.Amount;
        staked += placed.Amount;
        bets.push_back( placed );
    }
    OTDU_LOG_INFO( "CasinoRoulette: rebet %d chips, balance %d\n", total,
                   chips );
    return true;
}

void CasinoRoulette::tick( float deltaTime ) {
    phaseTime += deltaTime;
    switch ( phase ) {
    case ePhase::PlaceYourBet:
        break;
    case ePhase::BetsAreDone:
        if ( phaseTime >= kBetCloseTime ) {
            phase = ePhase::RienNeVaPlus;
            phaseTime = 0.0f;
        }
        break;
    case ePhase::RienNeVaPlus:
        if ( phaseTime >= kSpinTime ) {
            settle();
            phase = ePhase::Winner;
            phaseTime = 0.0f;
        }
        break;
    case ePhase::Winner:
        if ( phaseTime >= kResultTime ) {
            phase = ePhase::PlaceYourBet;
            phaseTime = 0.0f;
            winningNumber = -1;
            lastPayout = 0;
        }
        break;
    }
}

void CasinoRoulette::settle() {
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    const int32_t spin =
        static_cast<int32_t>( ( ( rng * 0x2545F4914F6CDD1Dull ) >> 32 ) % 37 );
    winningNumber = kWheel[spin];
    spins++;

    int32_t payout = 0;
    for ( const Placed &placed : bets ) {
        const int32_t n = winningNumber;
        bool bWin = false;
        int32_t multiplier = 0;
        switch ( placed.Bet ) {
        case eBet::Straight:
            bWin = placed.Number == n;
            multiplier = 36; // 35:1 plus the stake
            break;
        case eBet::Red:
            bWin = n != 0 && isRed( n );
            multiplier = 2;
            break;
        case eBet::Black:
            bWin = n != 0 && !isRed( n );
            multiplier = 2;
            break;
        case eBet::Odd:
            bWin = n != 0 && ( n % 2 ) == 1;
            multiplier = 2;
            break;
        case eBet::Even:
            bWin = n != 0 && ( n % 2 ) == 0;
            multiplier = 2;
            break;
        case eBet::Low:
            bWin = n >= 1 && n <= 18;
            multiplier = 2;
            break;
        case eBet::High:
            bWin = n >= 19 && n <= 36;
            multiplier = 2;
            break;
        case eBet::Dozen1:
            bWin = n >= 1 && n <= 12;
            multiplier = 3;
            break;
        case eBet::Dozen2:
            bWin = n >= 13 && n <= 24;
            multiplier = 3;
            break;
        case eBet::Dozen3:
            bWin = n >= 25 && n <= 36;
            multiplier = 3;
            break;
        case eBet::Column1:
            bWin = n != 0 && ( n % 3 ) == 1;
            multiplier = 3;
            break;
        case eBet::Column2:
            bWin = n != 0 && ( n % 3 ) == 2;
            multiplier = 3;
            break;
        case eBet::Column3:
            bWin = n != 0 && ( n % 3 ) == 0;
            multiplier = 3;
            break;
        default:
            break;
        }
        if ( bWin ) {
            payout += placed.Amount * multiplier;
            winningBets.push_back( placed );
        }
    }
    lastPayout = payout;
    chips += payout;
    history.insert( history.begin(), winningNumber );
    if ( history.size() > 12 ) {
        history.resize( 12 );
    }
    // The bets are consumed by the spin (kept for the Rebet button).
    lastBets = bets;
    staked = 0;
    bets.clear();
    OTDU_LOG_INFO( "CasinoRoulette: %d %s, payout %d chips, balance %d\n",
                   winningNumber,
                   winningNumber == 0       ? "(zero)"
                   : isRed( winningNumber ) ? "red"
                                            : "black",
                   payout, chips );
}

void CasinoRoulette::getTableSize( float scale,
                                   float &outWidth,
                                   float &outHeight ) {
    outWidth = ( kZeroWidth + kCellWidth * kGridCols ) * scale;
    outHeight = ( kCellHeight * 3.0f + kOutsideGap + kOutsideHeight + 4.0f +
                  kOutsideHeight ) *
                scale;
}

namespace {
    int32_t CellForBet( CasinoRoulette::eBet bet ) {
        if ( bet == CasinoRoulette::eBet::Straight ) {
            return 17; // a representative number
        }
        return kCellRed + ( static_cast<int32_t>( bet ) - 1 );
    }

    // The rectangle of a betting cell, in canvas coordinates.
    void
    GetCellRect( int32_t cell, float x, float y, float scale, Rect2D &out ) {
        int32_t col = 0;
        int32_t row = 0;
        CasinoRoulette::cursorFromCell( cell, col, row );
        if ( row >= 0 && row <= 2 ) {
            if ( col == -1 ) {
                out = Rect2D( x - kZeroWidth * scale, y, kZeroWidth * scale,
                              kCellHeight * 3.0f * scale );
                return;
            }
            out = Rect2D( x + col * kCellWidth * scale,
                          y + row * kCellHeight * scale, kCellWidth * scale,
                          kCellHeight * scale );
            return;
        }
        const float outsideY =
            y + ( kCellHeight * 3.0f + kOutsideGap ) * scale +
            ( row == 4 ? ( kOutsideHeight + 4.0f ) * scale : 0.0f );
        out = Rect2D( x + col * kCellWidth * 2.0f * scale, outsideY,
                      kCellWidth * 2.0f * scale, kOutsideHeight * scale );
    }
} // namespace

void CasinoRoulette::selfTest( int32_t numSpins ) {
    // Every bet on a single-zero wheel returns 36/37, whatever the bet type:
    // the measurement must land on the theoretical value for all thirteen.
    const double theoretical = 100.0 * 36.0 / 37.0;
    for ( int32_t b = 0; b < static_cast<int32_t>( eBet::Count ); b++ ) {
        const eBet bet = static_cast<eBet>( b );
        const int32_t cell = CellForBet( bet );
        CasinoRoulette table;
        table.seed( 1000u + static_cast<uint32_t>( b ) );
        table.setChips( 10000000 );
        int32_t wagered = 0;
        int32_t won = 0;
        for ( int32_t i = 0; i < numSpins; i++ ) {
            if ( ( i & 1023 ) == 0 ) {
                Watchdog::Beat( "rouletteselftest", static_cast<uint64_t>( i ) );
            }
            if ( !table.placeBet( cell ) || !table.spin() ) {
                break;
            }
            wagered += 1;
            while ( table.getPhase() != ePhase::Winner ) {
                table.tick( 0.1f );
            }
            won += table.getLastPayout();
            while ( table.getPhase() != ePhase::PlaceYourBet ) {
                table.tick( 0.1f );
            }
        }
        OTDU_LOG_INFO( "CasinoRoulette self test: %-9s %d spins, wagered %d, "
                       "won %d, RTP %.2f%% (theoretical %.2f%%)\n",
                       getBetName( bet ), numSpins, wagered, won,
                       wagered > 0 ? 100.0 * won / wagered : 0.0, theoretical );
    }
}

void CasinoRoulette::draw( Renderer2D &renderer,
                           const Font *pFont,
                           float x,
                           float y,
                           float scale,
                           int32_t cursorCol,
                           int32_t cursorRow ) const {
    const uint32_t kRed = MakeRGBA( 168, 32, 44 );
    const uint32_t kBlack = MakeRGBA( 26, 26, 32 );
    const uint32_t kZero = MakeRGBA( 22, 108, 62 );
    const uint32_t kGridLine = MakeRGBA( 214, 196, 140 );
    const uint32_t kText = MakeRGBA( 240, 236, 220 );

    // Felt.
    float width = 0.0f, height = 0.0f;
    getTableSize( scale, width, height );
    renderer.drawRect( Rect2D( x - kZeroWidth * scale - 8.0f * scale,
                               y - 8.0f * scale, width + 16.0f * scale,
                               height + 16.0f * scale ),
                       MakeRGBA( 12, 60, 34 ) );

    // The numbers, 12 columns x 3 rows (row 0 reads 3,6,..36).
    for ( int32_t row = 0; row < 3; row++ ) {
        for ( int32_t col = 0; col < kGridCols; col++ ) {
            const int32_t number = col * 3 + ( row == 0   ? 3
                                               : row == 1 ? 2
                                                          : 1 );
            const Rect2D rect( x + col * kCellWidth * scale,
                               y + row * kCellHeight * scale,
                               kCellWidth * scale, kCellHeight * scale );
            renderer.drawRect( rect, isRed( number ) ? kRed : kBlack );
            if ( pFont != nullptr ) {
                renderer.drawText( pFont, std::to_string( number ).c_str(),
                                   rect.X + rect.Width * 0.5f,
                                   rect.Y + rect.Height * 0.5f, 13.0f * scale,
                                   kText, eTextAlign::TA_Center );
            }
        }
    }
    // The zero.
    {
        Rect2D rect;
        GetCellRect( 0, x, y, scale, rect );
        renderer.drawRect( rect, kZero );
        if ( pFont != nullptr ) {
            renderer.drawText( pFont, "0", rect.X + rect.Width * 0.5f,
                               rect.Y + rect.Height * 0.5f, 15.0f * scale,
                               kText, eTextAlign::TA_Center );
        }
    }
    // The outside bets (row 3 then row 4).
    for ( int32_t cell = kCellRed; cell < kCellRed + 12; cell++ ) {
        Rect2D rect;
        GetCellRect( cell, x, y, scale, rect );
        renderer.drawRect( rect, MakeRGBA( 22, 34, 26 ) );
        renderer.drawRectOutline( rect, 1.0f * scale, kGridLine );
        if ( pFont != nullptr ) {
            renderer.drawText( pFont, kOutsideLabels[cell - kCellRed],
                               rect.X + rect.Width * 0.5f,
                               rect.Y + rect.Height * 0.5f, 11.0f * scale,
                               kText, eTextAlign::TA_Center );
        }
    }

    // Placed bets (a chip on the cell).
    for ( const Placed &placed : bets ) {
        Rect2D rect;
        GetCellRect( placed.Cell, x, y, scale, rect );
        const float size = 16.0f * scale;
        const Rect2D chip( rect.X + rect.Width * 0.5f - size * 0.5f,
                           rect.Y + rect.Height * 0.5f - size * 0.5f, size,
                           size );
        renderer.drawRect( chip, MakeRGBA( 240, 226, 180 ) );
        if ( pFont != nullptr ) {
            renderer.drawText( pFont, std::to_string( placed.Amount ).c_str(),
                               chip.X + chip.Width * 0.5f,
                               chip.Y + chip.Height * 0.5f, 10.0f * scale,
                               MakeRGBA( 30, 30, 30 ), eTextAlign::TA_Center );
        }
    }

    // The winning number.
    if ( phase == ePhase::Winner && winningNumber >= 0 ) {
        Rect2D rect;
        GetCellRect( winningNumber, x, y, scale, rect );
        renderer.drawRectOutline( rect, 3.0f * scale,
                                  MakeRGBA( 255, 214, 64 ) );
    }

    // The cursor.
    {
        int32_t cell = 0;
        if ( cellFromCursor( cursorCol, cursorRow, cell ) ) {
            Rect2D rect;
            GetCellRect( cell, x, y, scale, rect );
            renderer.drawRectOutline( rect, 2.0f * scale,
                                      MakeRGBA( 255, 255, 255 ) );
        }
    }

    // Phase banner + the history strip (the last numbers, newest first).
    if ( pFont != nullptr ) {
        const char *pPhase = phase == ePhase::PlaceYourBet   ? "PLACE YOUR BETS"
                             : phase == ePhase::BetsAreDone  ? "BETS ARE DONE"
                             : phase == ePhase::RienNeVaPlus ? "RIEN NE VA PLUS"
                                                             : "WINNER";
        renderer.drawText( pFont, pPhase, x, y - 24.0f * scale, 17.0f * scale,
                           MakeRGBA( 255, 214, 64 ) );
        float hx = x + width + 14.0f * scale;
        for ( size_t i = 0; i < history.size(); i++ ) {
            const int32_t number = history[i];
            const Rect2D chip( hx, y, 22.0f * scale, 22.0f * scale );
            renderer.drawRect( chip, number == 0       ? kZero
                                     : isRed( number ) ? kRed
                                                       : kBlack );
            renderer.drawText( pFont, std::to_string( number ).c_str(),
                               chip.X + chip.Width * 0.5f,
                               chip.Y + chip.Height * 0.5f, 11.0f * scale,
                               kText, eTextAlign::TA_Center );
            hx += 24.0f * scale;
        }
    }
}
