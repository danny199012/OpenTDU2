#pragma once

#include <cstdint>
#include <vector>

class Renderer2D;
class Font;

// The slot machine of the casino, offline.
//
// Retail: the game is a 5-reel video slot played through the frontend movies
// Cas_SlotMachine.vmf / Cas_SlotScreen.vmf / Cas_SlotLines.vmf, and its result
// came from the online casino backend — the only traces left in the client are
// the statistics save (CasinoSlotMachineStatisticsSaveDesc) and the statistics
// messages CRC_{A,R}_STATS_{GET,SET}_CASINO_SLOT_1, plus the beats
// C_SLOT_ACTIVATE / C_SLOT_WIN / C_SLOT_LOOSE / C_SLOT_MENU [DATA: PS3
// anchors]. There is no paytable in the shipped data, so the reel strips and
// the paytable here are OpenTDU2's own and tagged [INF] in
// docs/re/casino_spec.md; they are kept in one place so the community can
// replace them. The machine's *shape* (5 reels x 3 rows, per-line bet) and the
// beats follow the retail movies.
//
// The five reels use one strip, so the return to player is exact and can be
// computed from the symbol counts (analyticRtp) as well as measured
// (selfTest, -slotselftest).
class CasinoSlots {
public:
    static constexpr int32_t kReels = 5;
    static constexpr int32_t kRows = 3;
    static constexpr int32_t kLines = 9;
    static constexpr int32_t kMaxBetPerLine = 10;
    static constexpr int32_t kStripLength = 64;

    enum class eSymbol : uint8_t {
        Cherry = 0,
        Lemon,
        Bell,
        Bar,
        Seven,
        Diamond,
        Jackpot,
        Count,
    };

    // The six machines of the retail slots room [DATA: the
    // CasinoSlotMachineTypeDesc entities of icasslo1.bnk]:
    //
    //   SlotMachineClassic    Price 2    3 lines, 8 symbols
    //   SlotMachineClothes    Price 10   1 line,  4 symbols
    //   SlotMachinePoker      Price 5    1 line, 52 symbols (a card machine)
    //   SlotMachineHawai      Price 15   3 lines, 8 symbols, relaunch
    //   SlotMachineHighLife   Price 10   9 lines, 8 symbols
    //   SlotMachineCar        Price 100  1 line,  6 symbols
    //
    // Price is the machine's stake for one spin at bet level 1 [DATA]; the
    // bet level (1..10) multiplies it and the payout.
    enum class eMachine : uint8_t {
        Car = 0,      // 100 a spin: the car prize (the Audi R8 V10)
        Clothes = 1,  // 10 a spin: a casino clothes item
        Poker = 2,    // 5 a spin: the card machine
        Hawai = 3,    // 15 a spin, 3 lines
        Classic = 4,  // 2 a spin, 3 lines
        HighLife = 5, // 10 a spin, 9 lines (the port's original machine)
        Count,
    };

    // The special line of the car and clothes machines.
    enum class ePrize : uint8_t { None = 0, Car, Clothes };

    struct Win {
        int32_t Line = 0; // index into kPaylines
        eSymbol Symbol = eSymbol::Cherry;
        int32_t Count = 0;  // matching symbols on the line (3, 4 or 5)
        int32_t Payout = 0; // chips (already multiplied by the line bet)
    };

    struct Spin {
        eSymbol Grid[kReels][kRows] = {};
        std::vector<Win> Wins;
        int32_t Wagered = 0;
        int32_t Payout = 0;
    };

public:
    CasinoSlots();

    // Deterministic for a fixed seed (the self test); otherwise call with the
    // clock. A zero seed leaves the current state alone.
    void seed( uint32_t value );

    // Bet is per line, 1..kMaxBetPerLine; the wager is lines * the bet.
    void setBetPerLine( int32_t chips );
    inline int32_t getBetPerLine() const { return betPerLine; }
    // The multi-line machines (NbLines > 1: Classic, Beach, High Life) take
    // their Price per activated line - "the price is 2 per line activated"
    // (m_smc1_p1_2, m_smh1_p1_3, m_sml1_p1_3) [DATA]; the others per spin.
    inline bool isPricePerLine() const { return getMaxLines() > 1; }
    inline int32_t getWager() const { return getMachinePrice() * betPerLine * ( isPricePerLine() ? linesActive : 1 ); }
    // The machine the player stands at (the slots room's six types).
    void setMachine( eMachine machine );
    inline eMachine getMachine() const { return machine; }
    static const char *getMachineName( eMachine machine );
    // The retail Price of the machine (its stake for one spin at bet level 1).
    int32_t getMachinePrice() const;
    // The lines the machine plays (1 / 3 / 9) and its symbol set (4 / 6 / 8;
    // the card machine uses 8 here [INF]).
    inline int32_t getActiveLines() const { return linesActive; }
    // The machine's NbLines, and the activated lines (the retail lines
    // manager of Cas_SlotMachine.vmf: 1..3 on 3 reels, 1 / 3 / 5 / 9 on 5).
    int32_t getMaxLines() const;
    void setActiveLines( int32_t lines );
    // The special line of the last spin (car / clothes machines).
    inline ePrize getPrize() const { return prize; }
    // Cycles the bet through 1..kMaxBetPerLine (the movie's bet control).
    void stepBetPerLine( int32_t delta );

    inline int32_t getChips() const { return chips; }
    void setChips( int32_t value ) { chips = Max<int32_t>( 0, value ); }
    void addChips( int32_t delta ) { chips = Max<int32_t>( 0, chips + delta ); }

    // Starts a spin: deducts the wager and picks the stops. False when a spin
    // is already running or the balance cannot cover the wager.
    bool spin();
    // The second chance machines (Beach, Poker): the first spin pays nothing
    // ("winnings are received after the second draw", m_smh1_p2_2 [DATA]) -
    // setPayNextSpin( false ) before it. respin() turns the reels not held
    // again for free ("free and systematic Second Chance", m_smh1_p1_3) and,
    // with bPay, settles and pays like a spin.
    void setPayNextSpin( bool bPay ) { bPayThisSpin = bPay; }
    bool respin( const bool ( &held )[kReels], bool bPay );
    // The grid of the last settled spin (what the reels show at rest).
    inline eSymbol getGrid( int32_t reel, int32_t row ) const { return grid[reel][row]; }
    // Advances the reel-stop animation. True while the reels are turning; the
    // win is credited on the tick that stops the last reel.
    bool tick( float deltaTime );
    inline bool isSpinning() const { return bSpinning; }
    // The played machine's own reels [DATA: CasinoGame__SlotMachine Spinners,
    // TimeBeforeFirstStop, the spinner TimeAfterPrevSpinner]: a 3-reel machine
    // pays runs of 3 on its 3 reels; 0 = the five reels and the port's timing.
    void setReelLayout( int32_t reels, float firstStop, float stopGap );
    // Each reel's own stop time from the spin start (the machine's spinners:
    // TimeBeforeFirstStop + the running sum of TimeAfterPrevSpinner).
    void setReelStops( const std::vector<float> &stopTimes );
    inline int32_t getReelCount() const { return reelCount; }
    inline bool isReelStopped( int32_t reel ) const { return reel >= 0 && reel < kReels && reelStopped[reel]; }
    // The result (or the idle grid): what reel `reel` shows in row `row` once stopped.
    inline eSymbol getResult( int32_t reel, int32_t row ) const { return grid[reel][row]; }
    inline float getSpinTime() const { return spinTime; }

    // Settled result of the last spin (valid once isSpinning() is false).
    inline const Spin &getLastSpin() const { return last; }
    inline int32_t getTotalWon() const { return totalWon; }
    inline int32_t getTotalWagered() const { return totalWagered; }
    inline int32_t getSpins() const { return spins; }
    // The last payout, 0 after a losing spin; drives the C_SLOT_WIN/LOOSE beat.
    inline int32_t getLastPayout() const { return last.Payout; }

    // The 5 x 3 paylines as row indices (0 top .. 2 bottom).
    static const int32_t ( &getPaylines() )[kLines][kReels];
    static const char *getSymbolName( eSymbol symbol );
    // Single ASCII glyph used by the native renderer (the retail art is in
    // cas_slotmachine.bnk; see the spec for the texture ids).
    static const char *getSymbolGlyph( eSymbol symbol );
    static uint32_t getSymbolColor( eSymbol symbol );

    // Expected return for one line, computed from the strip and the paytable
    // (exact: the five reels share one strip).
    static double analyticRtp();

    // -slotselftest <n>: n spins with a fixed seed, then the measured and the
    // analytic return, logged. Also proves the wallet arithmetic.
    static void selfTest( int32_t spins );

    // Native rendering of the machine (origin = top-left of the 5 x 3 window).
    void draw( Renderer2D &renderer,
               const Font *pFont,
               float x,
               float y,
               float scale ) const;
    // The reel window's size at 'scale' (the caller centres / frames it).
    static void getWindowSize( float scale, float &outWidth, float &outHeight );

private:
    void buildStrips();
    void evaluate();
    int32_t payFor( eSymbol symbol, int32_t count ) const;
    uint32_t nextRandom();
    // The symbol set the machine's strip is built from.
    int32_t getSymbolCount() const;
    // The strip's real length: kStripLength only when every symbol is in the
    // machine's set - SlotMachineClothes (4 symbols) builds 47 stops and
    // SlotMachineCar (6) 61, so a read wrapped at kStripLength ran off the end
    // (the Clothes machine's blank reel seam).
    inline int32_t stripSize() const { return strip.empty() ? 1 : static_cast<int32_t>( strip.size() ); }

    std::vector<eSymbol> strip; // shared by the five reels [INF]
    eSymbol grid[kReels][kRows];
    eSymbol display[kReels]
                   [kRows]; // what the reels show (cycles while spinning)
    int32_t stop[kReels];
    bool reelStopped[kReels];
    float spinTime;
    bool bSpinning;
    int32_t betPerLine;
    eMachine machine = eMachine::HighLife;
    int32_t linesActive = kLines;
    int32_t reelCount = kReels;
    float firstStopTime = 0.0f; // 0 = kFirstStop
    float stopGapTime = 0.0f;   // 0 = kStopGap
    float reelStopTimes[5] = {}; // setReelStops (0: the first / gap rule)
    bool bPayThisSpin = true;
    bool bReelStops = false;
    ePrize prize = ePrize::None;
    int32_t chips;
    Spin last;
    int32_t totalWon;
    int32_t totalWagered;
    int32_t spins;
    uint64_t rng;
};
