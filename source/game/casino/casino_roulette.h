#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Renderer2D;
class Font;

// Roulette of the casino, offline.
//
// Retail: Cas_Roulette.vmf, played at a table whose phases are the PS3
// selectors CASROU_RoulettePlaceYourBetSelector / BetsAreDoneSelector /
// RienNeVaPlusSelector / WinnerSelector, with the bet names BET_RED, BET_BLACK,
// BET_1TO18, BET_19TO36 and the dealer strings CASROU_PlaceYourBet /
// CASROU_BetsAreDone / CASROU_RienNeVaPlus / CASROU_%sC%d [DATA: PS3 anchors].
// The wheel itself is the standard single-zero (European) wheel; the payouts
// (35:1 straight, 2:1 dozen/column, 1:1 even money) are the standard ones
// [INF], because the client only names the bets.
//
// Offline: the game is played against the machine — no session, no other
// players — so the phase machine runs on a timer instead of waiting for the
// table (the retail "no time for bet" selector CASROU_NoTime has no meaning
// here).
class CasinoRoulette {
public:
    // The betting cells: the 37 numbers plus the outside bets.
    enum class eBet : uint8_t {
        Straight = 0, // a single number (0..36)
        Red,
        Black,
        Odd,
        Even,
        Low,  // 1..18  (BET_1TO18)
        High, // 19..36 (BET_19TO36)
        Dozen1,
        Dozen2,
        Dozen3,
        Column1,
        Column2,
        Column3,
        Count,
    };

    // The retail table phases (CASROU_* selectors).
    enum class ePhase : uint8_t {
        PlaceYourBet = 0,
        BetsAreDone,
        RienNeVaPlus,
        Winner
    };

    struct Placed {
        int32_t Cell = 0; // index into the 49 cells (see cellBet / cellNumber)
        eBet Bet = eBet::Straight;
        int32_t Number = -1; // the number of a straight bet, -1 otherwise
        int32_t Amount = 0;
    };

    CasinoRoulette();

    // Deterministic for a fixed seed (the self test); otherwise call with the
    // clock. A zero seed leaves the current state alone.
    void seed( uint32_t value );

    // 37 numbers in the wheel's own order (single zero).
    static const int32_t ( &getWheel() )[37];
    static bool isRed( int32_t number );
    static const char *getBetName( eBet bet );
    // The number of a straight cell, or -1.
    static int32_t cellNumber( int32_t cell );
    static eBet cellBet( int32_t cell );

    // The betting grid: 3 rows x 12 columns of numbers, the 0 to their left,
    // then two rows of six outside bets. Cursor cells are (col, row):
    //   col -1, row 1        the 0
    //   col 0..11, row 0..2  the numbers (row 0 = 3,6,..36; row 2 = 1,4,..34)
    //   row 3                 RED BLACK ODD EVEN 1-18 19-36
    //   row 4                 D1 D2 D3 C1 C2 C3
    static constexpr int32_t kGridRows = 5;
    static constexpr int32_t kGridCols = 12;
    static bool cellFromCursor( int32_t col, int32_t row, int32_t &outCell );
    static void
    cursorFromCell( int32_t cell, int32_t &outCol, int32_t &outRow );

    inline int32_t getChips() const { return chips; }
    void setChips( int32_t value ) { chips = Max<int32_t>( 0, value ); }
    void addChips( int32_t delta ) { chips = Max<int32_t>( 0, chips + delta ); }

    // Chip denominations of the felt (1 / 5 / 25 / 100).
    inline int32_t getDenomination() const {
        return denominations[denomination];
    }
    void stepDenomination( int32_t delta );
    // The table's minimum chip (CasinoRouletteTypeDesc Price [DATA]): no chip
    // smaller than it is played, and the denomination starts on it.
    void setTablePrice( int32_t price );
    inline int32_t getTablePrice() const { return tablePrice; }

    inline const std::vector<Placed> &getBets() const { return bets; }
    // The bets that won the last round (valid in ePhase::Winner).
    inline const std::vector<Placed> &getWinningBets() const {
        return winningBets;
    }
    inline int32_t getStaked() const { return staked; }
    // Places the current denomination on a cell. False when the balance cannot
    // cover it or the phase is not PlaceYourBet.
    bool placeBet( int32_t cell );
    // Takes the bets back (before the wheel spins).
    void clearBets();
    // Re-places the bets of the last round (the retail Rebet button, the
    // "Use the Rebet button on roulette" objective). False when there is no
    // last bet or the balance cannot cover it.
    bool rebet();
    inline bool hasLastBets() const { return !lastBets.empty(); }

    inline ePhase getPhase() const { return phase; }
    inline float getPhaseTime() const { return phaseTime; }
    inline int32_t getWinningNumber() const { return winningNumber; }
    inline int32_t getLastPayout() const { return lastPayout; }
    // The last numbers that came up, newest first (the wheel's history strip).
    inline const std::vector<int32_t> &getHistory() const { return history; }
    inline int32_t getSpins() const { return spins; }

    // Starts the spin (PlaceYourBet -> BetsAreDone); false with no bet down.
    bool spin();
    void tick( float deltaTime );

    // -rouletteselftest <n>: n spins of every bet type, then the measured
    // return of each (the wheel and the payouts must agree with the odds).
    static void selfTest( int32_t spins );

    void draw( Renderer2D &renderer,
               const Font *pFont,
               float x,
               float y,
               float scale,
               int32_t cursorCol,
               int32_t cursorRow ) const;
    static void getTableSize( float scale, float &outWidth, float &outHeight );

private:
    void settle();

    std::vector<Placed> bets;
    std::vector<Placed> lastBets;    // the bets of the previous round (Rebet)
    std::vector<Placed> winningBets; // the bets that won the last round
    std::vector<int32_t> history;
    int32_t denominations[4] = { 1, 5, 25, 100 };
    int32_t denomination = 0;
    int32_t chips = 0;
    int32_t staked = 0;
    int32_t lastPayout = 0;
    int32_t winningNumber = -1;
    int32_t spins = 0;
    int32_t tablePrice = 1;
    ePhase phase = ePhase::PlaceYourBet;
    float phaseTime = 0.0f;
    uint64_t rng;
};
