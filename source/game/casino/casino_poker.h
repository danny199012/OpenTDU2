#pragma once

#include <cstdint>
#include <vector>

class Renderer2D;
class Font;

// Video poker of the casino (the retail table is `Cas_Poker.vmf`, with
// `C_POKER_YOU_BET` / `CasinoPokerStatisticsSaveDesc` in the client [DATA: PS3
// anchors]). Offline it is 9/6 Jacks or Better: five cards, hold any, draw
// once, paid on a pair of Jacks or better.
//
// The paytable is [INF] (the retail one was server side, like the slot
// machine's): the classic 9/6 Jacks or Better table, whose optimal-play return
// is **99.54%**. The self test plays the *exact* optimal strategy (it evaluates
// every one of the 32 hold combinations against every completion) so the
// measured return can be compared with the published 99.54%.
class CasinoPoker {
public:
    static constexpr int32_t kHand = 5;

    // Rank 0 = Two .. 12 = Ace.
    enum class eRank : uint8_t {
        Two = 0,
        Jack = 9,
        Queen = 10,
        King = 11,
        Ace = 12
    };
    enum class eSuit : uint8_t { Spades = 0, Hearts, Diamonds, Clubs };

    struct Card {
        uint8_t Rank = 0; // 0..12
        uint8_t Suit = 0; // 0..3
    };

    // The paying outcomes, best first.
    enum class eResult : uint8_t {
        None = 0,
        JacksOrBetter,
        TwoPair,
        ThreeOfAKind,
        Straight,
        Flush,
        FullHouse,
        FourOfAKind,
        StraightFlush,
        RoyalFlush,
        Count,
    };

    enum class ePhase : uint8_t { Idle = 0, Holding, Result };

    CasinoPoker();

    // Deterministic for a fixed seed (the self test); 0 leaves the state alone.
    void seed( uint32_t value );

    inline int32_t getChips() const { return chips; }
    void setChips( int32_t value ) { chips = Max<int32_t>( 0, value ); }
    void addChips( int32_t delta ) { chips = Max<int32_t>( 0, chips + delta ); }
    inline int32_t getBet() const { return bet; }
    void setBet( int32_t value ) { bet = Clamp<int32_t>( value, 1, kMaxBet ); }
    void stepBet( int32_t delta );

    // 9/6 Jacks or Better, in bet multiples (0 for a losing hand).
    static int32_t getPayout( eResult result );
    // The payout in chips for a bet: a royal flush pays the max-bet bonus
    // (800 per coin at kMaxBet, 250 below), which is what makes the return
    // 99.54% at max bet against 98.37% at one coin.
    static constexpr int32_t kMaxBet = 5;
    static int32_t getPayoutForBet( eResult result, int32_t bet );
    static const char *getResultName( eResult result );
    // The best hand of five cards.
    static eResult evaluate( const Card *pCards );

    inline ePhase getPhase() const { return phase; }
    inline const Card *getHand() const { return hand; }
    inline bool isHeld( int32_t index ) const {
        return index >= 0 && index < kHand && held[index];
    }
    // The result of the finished hand (valid in ePhase::Result).
    inline eResult getResult() const { return result; }
    inline int32_t getLastPayout() const { return lastPayout; }
    inline int32_t getHands() const { return hands; }
    inline int32_t getTotalWon() const { return totalWon; }
    inline int32_t getTotalWagered() const { return totalWagered; }

    // Idle -> Holding: antes the bet and deals five cards.
    bool deal();
    // Holding: holds / releases a card.
    void toggleHold( int32_t index );
    // Holding -> Result: replaces the cards that are not held, evaluates and
    // pays.
    bool draw();
    // Result -> Idle once the player moves on.
    void acknowledge();
    // The Result phase returns to Idle on its own after this long.
    void tick( float deltaTime );

    // -pokerselftest <hands>: exact optimal play at the max bet, then the
    // measured return (99.54% is the published figure for 9/6 Jacks or Better
    // at max bet; one coin pays 250 for the royal and returns 98.37%).
    static void selfTest( int32_t numHands );

    void draw( Renderer2D &renderer,
               const Font *pFont,
               float x,
               float y,
               float scale ) const;
    static void getTableSize( float scale, float &outWidth, float &outHeight );

private:
    void shuffle();
    uint32_t nextRandom();

    Card deck[52];
    int32_t nextCard = 0;
    Card hand[kHand];
    bool held[kHand] = {};
    ePhase phase = ePhase::Idle;
    eResult result = eResult::None;
    int32_t lastPayout = 0;
    int32_t bet = 1;
    int32_t chips = 0;
    int32_t hands = 0;
    int32_t totalWon = 0;
    int32_t totalWagered = 0;
    float resultTime = 0.0f;
    uint64_t rng = 0x9E3779B97F4A7C15ull;
};
