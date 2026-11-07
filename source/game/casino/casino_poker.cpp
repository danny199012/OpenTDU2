#include "shared.h"
#include "casino_poker.h"

#include "core/watchdog.h"
#include "render/renderer_2d.h"

#include <cmath>
#include <cstdio>

namespace {
    // 9/6 Jacks or Better [INF]: the paytable of the retail table was server
    // side (see the header). Optimal play returns 99.54%.
    constexpr int32_t
        kPayout[static_cast<int32_t>( CasinoPoker::eResult::Count )] = {
            0,   // None
            1,   // Jacks or better
            2,   // Two pair
            3,   // Three of a kind
            4,   // Straight
            6,   // Flush
            9,   // Full house
            25,  // Four of a kind
            50,  // Straight flush
            250, // Royal flush (flat, not the max-bet 800)
    };

    constexpr const char *kRankNames[13] = { "2", "3",  "4", "5", "6", "7", "8",
                                             "9", "10", "J", "Q", "K", "A" };
    constexpr const char *kSuitNames[4] = { "S", "H", "D", "C" };

    constexpr float kCardWidth = 92.0f;
    constexpr float kCardHeight = 132.0f;
    constexpr float kCardGap = 12.0f;
    // The Result phase returns to Idle on its own after this long.
    constexpr float kResultTime = 3.0f;
} // namespace

int32_t CasinoPoker::getPayout( eResult result ) {
    const int32_t index = static_cast<int32_t>( result );
    return index >= 0 && index < static_cast<int32_t>( eResult::Count )
               ? kPayout[index]
               : 0;
}

int32_t CasinoPoker::getPayoutForBet( eResult result, int32_t bet ) {
    if ( result == eResult::RoyalFlush ) {
        // The max-bet royal bonus (800 per coin at kMaxBet, 250 below): the
        // difference between the 99.54% and the 98.37% return.
        return ( bet >= kMaxBet ? 800 : 250 ) * bet;
    }
    return getPayout( result ) * bet;
}

const char *CasinoPoker::getResultName( eResult result ) {
    switch ( result ) {
    case eResult::None:
        return "No win";
    case eResult::JacksOrBetter:
        return "Jacks or better";
    case eResult::TwoPair:
        return "Two pair";
    case eResult::ThreeOfAKind:
        return "Three of a kind";
    case eResult::Straight:
        return "Straight";
    case eResult::Flush:
        return "Flush";
    case eResult::FullHouse:
        return "Full house";
    case eResult::FourOfAKind:
        return "Four of a kind";
    case eResult::StraightFlush:
        return "Straight flush";
    case eResult::RoyalFlush:
        return "Royal flush";
    default:
        return "?";
    }
}

CasinoPoker::eResult CasinoPoker::evaluate( const Card *pCards ) {
    int32_t counts[13] = {};
    int32_t suits[4] = {};
    for ( int32_t i = 0; i < kHand; i++ ) {
        counts[pCards[i].Rank % 13]++;
        suits[pCards[i].Suit % 4]++;
    }
    const bool bFlush = suits[0] == kHand || suits[1] == kHand ||
                        suits[2] == kHand || suits[3] == kHand;

    // A straight is five distinct consecutive ranks, or A-2-3-4-5.
    int32_t distinct = 0;
    int32_t lowest = -1;
    int32_t highest = -1;
    for ( int32_t r = 0; r < 13; r++ ) {
        if ( counts[r] > 0 ) {
            distinct++;
            if ( lowest < 0 ) {
                lowest = r;
            }
            highest = r;
        }
    }
    bool bStraight = distinct == kHand && highest - lowest == kHand - 1;
    if ( !bStraight && counts[12] == 1 && counts[0] == 1 && counts[1] == 1 &&
         counts[2] == 1 && counts[3] == 1 ) {
        bStraight = true; // A-2-3-4-5
    }

    int32_t four = 0;
    int32_t three = 0;
    int32_t pairs = 0;
    int32_t highPairs = 0; // jacks or better
    for ( int32_t r = 0; r < 13; r++ ) {
        if ( counts[r] == 4 ) {
            four++;
        } else if ( counts[r] == 3 ) {
            three++;
        } else if ( counts[r] == 2 ) {
            pairs++;
            if ( r >= static_cast<int32_t>( eRank::Jack ) ) {
                highPairs++;
            }
        }
    }

    if ( bFlush && bStraight ) {
        // The royal is 10..A of one suit (ranks 8..12).
        const bool bRoyal = counts[8] == 1 && counts[9] == 1 &&
                            counts[10] == 1 && counts[11] == 1 &&
                            counts[12] == 1;
        return bRoyal ? eResult::RoyalFlush : eResult::StraightFlush;
    }
    if ( four > 0 ) {
        return eResult::FourOfAKind;
    }
    if ( three > 0 && pairs > 0 ) {
        return eResult::FullHouse;
    }
    if ( bFlush ) {
        return eResult::Flush;
    }
    if ( bStraight ) {
        return eResult::Straight;
    }
    if ( three > 0 ) {
        return eResult::ThreeOfAKind;
    }
    if ( pairs == 2 ) {
        return eResult::TwoPair;
    }
    if ( pairs == 1 && highPairs == 1 ) {
        return eResult::JacksOrBetter;
    }
    return eResult::None;
}

CasinoPoker::CasinoPoker() {
    for ( int32_t i = 0; i < kHand; i++ ) {
        hand[i] = Card();
        held[i] = false;
    }
    shuffle();
}

void CasinoPoker::seed( uint32_t value ) {
    if ( value != 0 ) {
        rng = 0x9E3779B97F4A7C15ull ^
              ( static_cast<uint64_t>( value ) * 0x2545F4914F6CDD1Dull );
    }
}

uint32_t CasinoPoker::nextRandom() {
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return static_cast<uint32_t>( ( rng * 0x2545F4914F6CDD1Dull ) >> 32 );
}

void CasinoPoker::shuffle() {
    int32_t at = 0;
    for ( int32_t suit = 0; suit < 4; suit++ ) {
        for ( int32_t rank = 0; rank < 13; rank++ ) {
            deck[at].Rank = static_cast<uint8_t>( rank );
            deck[at].Suit = static_cast<uint8_t>( suit );
            at++;
        }
    }
    // Fisher-Yates.
    for ( int32_t i = 51; i > 0; i-- ) {
        const int32_t j = static_cast<int32_t>(
            nextRandom() % static_cast<uint32_t>( i + 1 ) );
        const Card swap = deck[i];
        deck[i] = deck[j];
        deck[j] = swap;
    }
    nextCard = 0;
}

void CasinoPoker::stepBet( int32_t delta ) {
    int32_t value = bet + delta;
    if ( value > 5 ) {
        value = 1;
    } else if ( value < 1 ) {
        value = 5;
    }
    bet = value;
}

bool CasinoPoker::deal() {
    if ( phase != ePhase::Idle || chips < bet ) {
        return false;
    }
    chips -= bet;
    totalWagered += bet;
    hands++;
    shuffle();
    for ( int32_t i = 0; i < kHand; i++ ) {
        hand[i] = deck[nextCard++];
        held[i] = false;
    }
    result = eResult::None;
    lastPayout = 0;
    phase = ePhase::Holding;
    OTDU_LOG_INFO( "CasinoPoker: hand %d dealt, bet %d, balance %d\n", hands,
                   bet, chips );
    return true;
}

void CasinoPoker::toggleHold( int32_t index ) {
    if ( phase != ePhase::Holding || index < 0 || index >= kHand ) {
        return;
    }
    held[index] = !held[index];
}

bool CasinoPoker::draw() {
    if ( phase != ePhase::Holding ) {
        return false;
    }
    for ( int32_t i = 0; i < kHand; i++ ) {
        if ( !held[i] ) {
            hand[i] = deck[nextCard++];
        }
    }
    result = evaluate( hand );
    lastPayout = getPayoutForBet( result, bet );
    chips += lastPayout;
    totalWon += lastPayout;
    phase = ePhase::Result;
    resultTime = 0.0f;
    OTDU_LOG_INFO( "CasinoPoker: %s -> payout %d chips, balance %d\n",
                   getResultName( result ), lastPayout, chips );
    return true;
}

void CasinoPoker::acknowledge() {
    if ( phase == ePhase::Result ) {
        phase = ePhase::Idle;
        result = eResult::None;
        lastPayout = 0;
    }
}

void CasinoPoker::tick( float deltaTime ) {
    if ( phase == ePhase::Result ) {
        resultTime += deltaTime;
        if ( resultTime >= kResultTime ) {
            acknowledge();
        }
    }
}

namespace {
    // The exact expected return (in bet multiples) of holding the cards in
    // `mask`, averaged over every way the remaining cards can be dealt.
    double EvaluateHold( const CasinoPoker::Card *pHand,
                         const CasinoPoker::Card *pRest,
                         int32_t restCount,
                         int32_t mask,
                         int32_t bet ) {
        CasinoPoker::Card kept[CasinoPoker::kHand];
        int32_t keptCount = 0;
        for ( int32_t i = 0; i < CasinoPoker::kHand; i++ ) {
            if ( ( mask & ( 1 << i ) ) != 0 ) {
                kept[keptCount++] = pHand[i];
            }
        }
        const int32_t need = CasinoPoker::kHand - keptCount;
        if ( need == 0 ) {
            return static_cast<double>( CasinoPoker::getPayoutForBet(
                       CasinoPoker::evaluate( kept ), bet ) ) /
                   static_cast<double>( bet );
        }
        CasinoPoker::Card final[CasinoPoker::kHand];
        for ( int32_t i = 0; i < keptCount; i++ ) {
            final[i] = kept[i];
        }
        // Every combination of `need` cards out of the remaining ones.
        int32_t idx[CasinoPoker::kHand];
        for ( int32_t i = 0; i < need; i++ ) {
            idx[i] = i;
        }
        int64_t total = 0;
        int64_t count = 0;
        while ( true ) {
            for ( int32_t i = 0; i < need; i++ ) {
                final[keptCount + i] = pRest[idx[i]];
            }
            total += CasinoPoker::getPayoutForBet(
                CasinoPoker::evaluate( final ), bet );
            count++;
            int32_t i = need - 1;
            while ( i >= 0 && idx[i] == restCount - need + i ) {
                i--;
            }
            if ( i < 0 ) {
                break;
            }
            idx[i]++;
            for ( int32_t j = i + 1; j < need; j++ ) {
                idx[j] = idx[j - 1] + 1;
            }
        }
        return count > 0 ? static_cast<double>( total ) /
                               static_cast<double>( count ) /
                               static_cast<double>( bet )
                         : 0.0;
    }
} // namespace

// A fixed set of hands with the result the evaluator must return: this part is
// exact (the RTP below is a sample, this is not).
namespace {
    struct HandCase {
        const char *pName;
        uint8_t Ranks[CasinoPoker::kHand];
        uint8_t Suits[CasinoPoker::kHand];
        CasinoPoker::eResult Expected;
    };

    // Ranks: 0 = Two .. 12 = Ace. Suits: 0 spades, 1 hearts, 2 diamonds, 3
    // clubs.
    const HandCase kHandCases[] = {
        {"royal flush",
         { 12, 11, 10, 9, 8 },
         { 0, 0, 0, 0, 0 },
         CasinoPoker::eResult::RoyalFlush   },
        {"straight flush",
         { 7, 6, 5, 4, 3 },
         { 1, 1, 1, 1, 1 },
         CasinoPoker::eResult::StraightFlush},
        {"four of a kind",
         { 5, 5, 5, 5, 0 },
         { 0, 1, 2, 3, 0 },
         CasinoPoker::eResult::FourOfAKind  },
        {"full house",
         { 3, 3, 3, 11, 11 },
         { 0, 1, 2, 0, 1 },
         CasinoPoker::eResult::FullHouse    },
        {"flush",
         { 12, 9, 6, 3, 0 },
         { 3, 3, 3, 3, 3 },
         CasinoPoker::eResult::Flush        },
        {"straight",
         { 7, 6, 5, 4, 3 },
         { 0, 1, 2, 3, 0 },
         CasinoPoker::eResult::Straight     },
        {"wheel (A-2-3-4-5)",
         { 12, 0, 1, 2, 3 },
         { 0, 1, 2, 3, 0 },
         CasinoPoker::eResult::Straight     },
        {"three of a kind",
         { 2, 2, 2, 7, 0 },
         { 0, 1, 2, 0, 1 },
         CasinoPoker::eResult::ThreeOfAKind },
        {"two pair",
         { 11, 11, 1, 1, 6 },
         { 0, 1, 0, 1, 0 },
         CasinoPoker::eResult::TwoPair      },
        {"high pair (jacks)",
         { 9, 9, 5, 2, 0 },
         { 0, 1, 0, 1, 0 },
         CasinoPoker::eResult::JacksOrBetter},
        {"high pair (aces)",
         { 12, 12, 5, 2, 0 },
         { 0, 1, 0, 1, 0 },
         CasinoPoker::eResult::JacksOrBetter},
        {"low pair",
         { 7, 7, 11, 5, 1 },
         { 0, 1, 0, 1, 0 },
         CasinoPoker::eResult::None         },
        {"junk",
         { 12, 11, 7, 5, 2 },
         { 0, 1, 2, 3, 0 },
         CasinoPoker::eResult::None         },
    };
} // namespace

void CasinoPoker::selfTest( int32_t numHands ) {
    // 1. The evaluator: exact, one case per hand class.
    int32_t failed = 0;
    for ( const HandCase &testCase : kHandCases ) {
        Card cards[kHand];
        for ( int32_t i = 0; i < kHand; i++ ) {
            cards[i].Rank = testCase.Ranks[i];
            cards[i].Suit = testCase.Suits[i];
        }
        const eResult got = evaluate( cards );
        const bool bOk = got == testCase.Expected;
        if ( !bOk ) {
            failed++;
        }
        OTDU_LOG_INFO( "CasinoPoker evaluate: %-18s -> %-16s (%s)\n",
                       testCase.pName, getResultName( got ),
                       bOk ? "ok" : "FAIL" );
    }
    OTDU_LOG_INFO( "CasinoPoker evaluate: %zu cases, %d failed\n",
                   sizeof( kHandCases ) / sizeof( kHandCases[0] ), failed );

    // 2. The return under exact optimal play, sampled over `numHands` deals.
    // The
    //    per-deal optimal EV has a large spread (the rare dealt full houses,
    //    flushes, straights and 4-to-a-royals), so the mean is reported with
    //    its standard error rather than as a fixed figure.
    CasinoPoker game;
    game.seed( 1u );
    game.setChips( 100000000 );
    game.setBet(
        kMaxBet ); // the max-bet royal bonus, so the reference is 99.54%
    const int32_t bet = game.getBet();
    double evSum = 0.0;
    double evSumSquares = 0.0;
    int32_t dealt = 0;
    int32_t royals = 0;
    int32_t wins = 0;
    for ( int32_t h = 0; h < numHands; h++ ) {
        // The exact optimal hold scores ~2.6 M draws per hand (C(47,5) for the
        // full discard alone), so this loop runs for minutes on the frame
        // thread: beat, or the watchdog's kill guard ends it (exit 0x43544157,
        // whose low byte 87 read as a "crash after ~1000 hands").
        Watchdog::Beat( "pokerselftest", static_cast<uint64_t>( h ) );
        if ( !game.deal() ) {
            break;
        }
        dealt++;
        // The 47 cards the deal left.
        const Card *pRest = &game.deck[game.nextCard];
        const int32_t restCount = 52 - game.nextCard;
        int32_t bestMask = 0;
        double bestEv = -1.0;
        for ( int32_t mask = 0; mask < ( 1 << kHand ); mask++ ) {
            const double ev =
                EvaluateHold( game.hand, pRest, restCount, mask, bet );
            if ( ev > bestEv ) {
                bestEv = ev;
                bestMask = mask;
            }
        }
        evSum += bestEv;
        evSumSquares += bestEv * bestEv;
        // Play the hand out with that hold, so the wallet arithmetic is
        // checked.
        for ( int32_t i = 0; i < kHand; i++ ) {
            game.held[i] = ( bestMask & ( 1 << i ) ) != 0;
        }
        game.draw();
        if ( game.getResult() == eResult::RoyalFlush ) {
            royals++;
        }
        if ( game.getResult() != eResult::None ) {
            wins++;
        }
        game.acknowledge();
    }
    if ( dealt <= 0 ) {
        OTDU_LOG_WARN( "CasinoPoker self test: no hand was dealt\n" );
        return;
    }
    const double mean = evSum / dealt;
    double variance = 0.0;
    if ( dealt > 1 ) {
        variance =
            ( evSumSquares / dealt - mean * mean ) * dealt / ( dealt - 1 );
    }
    const double standardError = std::sqrt( Max( 0.0, variance ) / dealt );
    OTDU_LOG_INFO(
        "CasinoPoker self test: %d hands at the max bet, optimal-play EV mean "
        "%.2f%% +- %.2f%% (1 SE); "
        "9/6 Jacks or Better is 99.54%% at max bet, 98.37%% at one coin\n",
        dealt, 100.0 * mean, 100.0 * standardError );
    const int32_t expected =
        100000000 - game.getTotalWagered() + game.getTotalWon();
    OTDU_LOG_INFO( "CasinoPoker self test: %d winning hands (%.1f%%), %d "
                   "royal(s), balance %d, expected %d (%s)\n",
                   wins, 100.0 * wins / dealt, royals, game.getChips(),
                   expected, game.getChips() == expected ? "ok" : "MISMATCH" );
}

void CasinoPoker::getTableSize( float scale,
                                float &outWidth,
                                float &outHeight ) {
    outWidth = ( kCardWidth + kCardGap ) * kHand * scale;
    outHeight = kCardHeight * scale;
}

void CasinoPoker::draw( Renderer2D &renderer,
                        const Font *pFont,
                        float x,
                        float y,
                        float scale ) const {
    const float cardWidth = kCardWidth * scale;
    const float cardHeight = kCardHeight * scale;
    const float gap = kCardGap * scale;
    renderer.drawRect( Rect2D( x - 10.0f * scale, y - 10.0f * scale,
                               ( cardWidth + gap ) * kHand + 20.0f * scale,
                               cardHeight + 20.0f * scale ),
                       MakeRGBA( 16, 58, 34 ) );
    for ( int32_t i = 0; i < kHand; i++ ) {
        const float cx = x + i * ( cardWidth + gap );
        const bool bRed =
            hand[i].Suit == static_cast<uint8_t>( eSuit::Hearts ) ||
            hand[i].Suit == static_cast<uint8_t>( eSuit::Diamonds );
        // The card face is dark: the 2D text layer's glyph cells carry an
        // opaque black background, which only shows on a light surface (the
        // rest of the port's UI is dark for the same reason).
        const uint32_t face =
            bRed ? MakeRGBA( 236, 96, 96 ) : MakeRGBA( 226, 228, 236 );
        renderer.drawRect( Rect2D( cx, y, cardWidth, cardHeight ),
                           MakeRGBA( 30, 32, 44 ) );
        renderer.drawRectOutline( Rect2D( cx, y, cardWidth, cardHeight ),
                                  2.0f * scale, MakeRGBA( 96, 100, 120 ) );
        if ( held[i] ) {
            renderer.drawRectOutline( Rect2D( cx - 3.0f, y - 3.0f,
                                              cardWidth + 6.0f,
                                              cardHeight + 6.0f ),
                                      3.0f * scale, MakeRGBA( 255, 214, 64 ) );
        }
        if ( pFont != nullptr ) {
            renderer.drawText( pFont, kRankNames[hand[i].Rank % 13],
                               cx + cardWidth * 0.5f, y + cardHeight * 0.42f,
                               34.0f * scale, face, eTextAlign::TA_Center );
            renderer.drawText( pFont, kSuitNames[hand[i].Suit % 4],
                               cx + cardWidth * 0.5f, y + cardHeight * 0.76f,
                               26.0f * scale, face, eTextAlign::TA_Center );
            if ( held[i] ) {
                renderer.drawText( pFont, "HELD", cx + cardWidth * 0.5f,
                                   y - 20.0f * scale, 14.0f * scale,
                                   MakeRGBA( 255, 214, 64 ),
                                   eTextAlign::TA_Center );
            }
        }
    }
}
