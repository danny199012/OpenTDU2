#pragma once

#include "flash/flash_player.h"

#include <cstdint>
#include <string>
#include <vector>

class Renderer2D;
class Font;

// The hall's two desks on their retail movies (docs/re/casino_spec.md 9.12):
//
// * The bar: Cas_Common.vmf's 'cocktail' frame and its 'barman' clip - the
//   list of the 15 cocktails (cocktail_00..04, 10..14, 20..24, four price
//   tiers), their lock state (cocktail_XX_IsLock) and the "Solar Crown Raffle"
//   tutorial (tut_tombo). A on an open cocktail closes the list with the
//   FSCommand 'buy cock <id>' then 'clco'; B closes with 'clco'. The raffle
//   result is the movie's tombola_popup (var_win, var_level_win, Open()).
// * The cashier: Casino.vmf's 'money_conv' clip - page 1 picks dollars ->
//   chips ('casi:chx1') or chips -> dollars ('casi:chx2'), B quits
//   ('casi:qtcv'); the game fills valSliderMin / valSliderMax and sets
//   isReady, page 2 is the slider (slle / slri, conv:minv / conv:maxv) and
//   A / B answer 'conv:vali' / 'conv:canc' [DATA: the movies' own scripts].
//
// The pad -> movie routing is the exe's [ENC]; the port calls the root /
// _root.Clip methods the movies define (A, B, UP, DOWN, LEFT, RIGHT, Left,
// Right, LB, RB) [INF].
class CasinoDeskHud {
public:
    enum class eEvent : uint8_t {
        None = 0,
        BuyCocktail,   // 'buy cock <id>': getCocktailId()
        BarClosed,     // 'clco'
        ConvertChoice, // 'casi:chx1' / 'casi:chx2': isChipsToDollars()
        ConvertDone,   // 'conv:vali': getSliderValue()
        CashierClosed, // 'casi:qtcv'
    };

    bool load();
    void unload();
    inline bool isLoaded() const { return bBarLoaded && bCashierLoaded; }
    inline bool isBarLoaded() const { return bBarLoaded; }
    inline bool isCashierLoaded() const { return bCashierLoaded; }

    // The bar: the 15 retail ids ("00".."24") with their lock state; the
    // raffle tutorial plays the first time.
    void openBar( const std::vector<std::string> &unlockedIds, bool bShowTutorial );
    // The raffle result popup (the cherry in the glass): bWin and the pass
    // level 1..4.
    void showRaffle( bool bWin, int32_t level );
    inline bool isBarOpen() const { return bBarOpen; }
    bool isRaffleShown();
    // The DBXO 'not_enough_money' box of the bar (setMsgMoneyBarman) is drawn
    // natively, like the slot HUD's boxes [INF].
    void showMessage( const std::string &text );

    // The cashier: chips and money of the player.
    void openCashier( int32_t chipsBalance, int32_t moneyBalance );
    // The second page: the slider from 0 to maxValue (in the source currency).
    void setConversionRange( int32_t maxValue, int32_t startValue );
    inline bool isCashierOpen() const { return bCashierOpen; }

    void routeInput();
    void tick( float deltaTime );
    void draw( Renderer2D &renderer, const Font *pFont );
    std::vector<eEvent> takeEvents();

    inline const std::string &getCocktailId() const { return cocktailId; }
    inline bool isChipsToDollars() const { return bChipsToDollars; }
    inline int32_t getSliderValue() const { return sliderValue; }

    // The cocktail tier (1..4) of a retail id: the movie's own price groups
    // (setCockInfo's m_cockprice_1..4 and displayTicket's lvl1..4) [DATA].
    static int32_t CocktailTier( const std::string &id );
    // The 15 ids in the movie's cursor order (getCocktailIdFromCursor) [DATA].
    static const std::vector<std::string> &CocktailIds();

private:
    void onBarCommand( const std::string &command );
    void onCashierCommand( const std::string &command );
    void stepSlider( int32_t direction, bool bToEnd );
    void refreshSlider();

    FlashPlayer bar;     // Cas_Common.vmf
    FlashPlayer cashier; // Casino.vmf
    bool bBarLoaded = false;
    bool bCashierLoaded = false;
    bool bBarOpen = false;
    bool bRaffle = false;
    bool bCashierOpen = false;
    bool bSliderPage = false;
    bool bChipsToDollars = false;
    bool bSliderDirty = false;
    int32_t chips = 0;
    int32_t money = 0;
    int32_t sliderMax = 0;
    int32_t sliderValue = 0;
    std::string cocktailId;
    std::string messageText;
    float messageTimer = 0.0f;
    std::vector<eEvent> events;
};
