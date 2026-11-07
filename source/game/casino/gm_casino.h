#pragma once

#include "flash/flash_player.h"
#include "game/casino/casino_desk_hud.h"
#include "game/casino/casino_interior.h"
#include "game/casino/casino_objectives_screen.h"
#include "game/casino/casino_poker.h"
#include "game/casino/casino_roulette.h"
#include "game/casino/casino_slot_hud.h"
#include "game/casino/casino_slots.h"
#include "game/cheats.h"
#include "game/clothes_shop.h"
#include "game/gm_base.h"
#include "game/pause_menu.h"

#include <string>

class Renderer2D;
class Font;

// "CASINO": the casino of the original, offline.
//
// Retail: the casino is its own frontend — the shell movie Casino.vmf
// (FrontEnd/HiRes/casino.bnk) plus one movie per game (Cas_SlotMachine.vmf,
// Cas_SlotLines.vmf, Cas_SlotScreen.vmf, Cas_Roulette.vmf, Cas_Poker.vmf) — and
// the games were played on the online casino backend (the client only carries
// the statistics save descriptors CasinoSlotMachineStatisticsSaveDesc /
// CasinoRouletteStatisticsSaveDesc / CasinoPokerStatisticsSaveDesc, the
// statistics messages CRC_*_STATS_*_CASINO_SLOT_1, the roulette table phases
// CASROU_* and the shared jackpot art C_SLOT_SHARED_JACKPOT_BACKGROUND
// [DATA: PS3 anchors]). The casino was also gated: "<INVITE> Invite rejected :
// Casino locked", CASINO_FORBID_SLOT_MACHINE, CasinoUnlockable, CasinoSave /
// CasinoProgressionSave / CasinoRankingsSave, and the VIP / ACE status
// cinematics [DATA]. docs/re/casino_spec.md has the whole contract.
//
// Offline: this mode plays the games against the machine. No session, no other
// players, no rankings — the wallet is the profile's
// (PlayerProfile::CasinoChips) and the tables are in casino_slots.cpp /
// casino_roulette.cpp. The retail unlockables gate the games: an unlockable
// present in the profile locks its game (CASINO_FORBID_SLOT_MACHINE by default
// is *not* present, so the casino is playable).
//
// The shell movie is loaded and drawn behind the native screens when it is
// available (-nocasinomovie turns it off, -dumpcasinolabels dumps its
// timelines); the games themselves are native, because the retail movies are
// driven by the server's messages (the same reason DealerMenu is native).
//
// Command line: -gm casino starts here; -casinochips <n> sets the starting
// balance; -slotselftest <n> / -rouletteselftest <n> run the return self tests
// and exit.
class GMCasino : public GameMode {
public:
    GMCasino();
    ~GMCasino();

    void drawOffscreen() override;
    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void draw() override;
    void exit() override;
    void drawDebug() override;
    // The cheats panel (F4): in here it is the wallet (money / casino chips)
    // that matters; the island teleport list stays empty, so the panel hides it.
    void drawUI() override;

    // The unlockable that locks the slot machine [DATA:
    // CASINO_FORBID_SLOT_MACHINE].
    static uint64_t getSlotMachineLockHash();

private:
    enum class eScreen : uint8_t { Floor = 0, Slots, Roulette, Poker, Cashier, Bar };

    // The developer cheats (source/game/cheats.h): the wallet at the tables.
    Cheats cheats;

    FlashPlayer casinoShell; // Casino.vmf (-casinomovie / -dumpcasinolabels)
    bool bShellLoaded = false;
    bool bDrawShell = false; // -casinomovie (see docs/re/casino_spec.md §5)
    CasinoSlots slots;
    CasinoRoulette roulette;
    CasinoPoker poker;
    // The retail casino interior (docs/re/casino_spec.md 9): the hall and its
    // game rooms, walked in first person. Without the banks (-nocasinointerior)
    // the mode falls back to the native floor menu.
    CasinoInterior interior;
    // The casino's own clothes shop (ishop_ca: DBCarShop._clothesshopbrand 0x57
    // "Casino") and the dressing room's wardrobe list.
    ClothesMenu clothesMenu;
    bool bShopOpen = false;
    bool bWardrobeOpen = false;
    int32_t wardrobeSelection = 0;
    // The cashier / the bar of the hall (their own native screens).
    int32_t cashierSelection = 0;
    int32_t barSelection = 0;
    // The Game Progress page of the pause menu's casino list: the
    // DBCasinoObjectiveDetail objectives, per axis.
    CasinoObjectivesScreen objectives;
    PauseMenu pauseMenu;

    eScreen screen = eScreen::Floor;
    int32_t floorSelection = 0;
    int32_t cursorCol = 0;
    int32_t cursorRow = 1;
    int32_t pokerCursor = 0;
    int32_t chips = 0;
    std::string message;
    float messageTimer = 0.0f;
    // Win streaks (the "Win N times in a row" objectives).
    int32_t slotStreak = 0;
    int32_t rouletteStreak = 0;
    int32_t pokerStreak = 0;
    // The progression line of the header (level / status / objectives).
    std::string progressLine;

    void openSlots();
    void openRoulette();
    void openPoker();
    // The casino shop (ishop_ca) and the slot machine prizes.
    void openCasinoShop();
    void awardSlotPrize( CasinoSlots::ePrize prize );
    void backToFloor();
    void buyChips();
    void leaveCasino();
    void setMessage( const std::string &text );
    // Rebuilds progressLine from the DBCasino* tables and the profile.
    void refreshProgressLine();
    // The objectives: the games report their rounds / spins / hands here.
    void reportSlotSpin( int32_t payout, int32_t winningLines, bool bJackpot );
    void reportRouletteRound();
    void reportPokerHand();
    // The _filterb id of a roulette bet [DATA: the objective texts].
    static int32_t rouletteBetFilter( CasinoRoulette::eBet bet );
    // The _filterb id of a poker hand [DATA: the objective texts].
    static int32_t pokerHandFilter( CasinoPoker::eResult result );

    void tickFloor( float deltaTime );
    void tickSlots( float deltaTime );
    void tickRoulette( float deltaTime );
    void tickPoker( float deltaTime );
    // The interior: the hall's doors, the machines / tables, the cashier, the
    // bar, the shop and the wardrobe.
    void tickInterior( float deltaTime );
    void handleInteriorAction( CasinoInterior::eAction action );
    void tickCashier( float deltaTime );
    void tickBar( float deltaTime );
    // The retail bar / cashier screens (casino_spec 9.12).
    CasinoDeskHud deskHud;
    enum class eBarStep : uint8_t { None = 0, List, Prepare, Drink, Raffle };
    eBarStep barStep = eBarStep::None;
    bool bRaffleTutorialShown = false;
    bool bCashierGiftGiven = false;
    bool raffleWon = false;
    int32_t raffleLevel = 1;
    void openCashier();
    void openBar();
    void buyCocktail( const std::string &id );
    void playDrinkCinematic();
    void tickDeskHud( float deltaTime );
    void tickWardrobe( float deltaTime );

    void drawFloor( Renderer2D &renderer, const Font *pFont );
    void drawSlots( Renderer2D &renderer, const Font *pFont );
    void drawRoulette( Renderer2D &renderer, const Font *pFont );
    void drawPoker( Renderer2D &renderer, const Font *pFont );
    // The interior's own header (room name, the selectable under the reticle).
    void drawInteriorHud( Renderer2D &renderer, const Font *pFont );
    void drawCashier( Renderer2D &renderer, const Font *pFont );
    void drawBar( Renderer2D &renderer, const Font *pFont );
    void drawWardrobe( Renderer2D &renderer, const Font *pFont );
    void drawHeader( Renderer2D &renderer,
                     const Font *pFont,
                     const char *pTitle,
                     const char *pHint );
    // The casino's flash globals (/:IsCasino, /:HasCasino, ...): pushed to the
    // shell and to the pause menu's movie.
    void pushFlashGlobals();
    bool isSlotMachineLocked() const;
    // The played machine's reel count (its Spinners) [DATA].
    int32_t slotReels = 0;
    int32_t slotAtlasColumns = 0;
    int32_t slotAtlasRows = 0;
    float reelRoll[5] = {};
    float handleTime = -1.0f; // the handle pull clip time (-1: at rest)
    int32_t slotSpinnerType = 0;     // SpinnerType: 0 drum, 1 horizontal (Fashion), 2 cards
    float slotRotationSpeed = 1.3f; // RotationSpeed [DATA]
    void updateReels( float deltaTime );
    // The retail slot HUD (Cas_SlotMachine.vmf + Cas_SlotScreen.vmf), up while
    // a machine is played; -noslothud keeps the native text.
    CasinoSlotHud slotHud;
    void handleSlotHudEvents();
    // Starts a spin from the HUD or the native input; false (and the movie's
    // error box) when the wallet cannot cover it.
    bool startSlotSpin();
    // The second chance machines (Poker, Beach): spin, hold / lock, respin
    // (casino_spec 9.9).
    enum class eRelaunch : uint8_t { None = 0, Holding, Second };
    eRelaunch relaunch = eRelaunch::None;
    // The jackpot cutscenes (casino_spec 9.7): the machine's C_JACKPOT_* plays
    // at the machine; a first car win then walks to the hall for C_WINCAR.
    bool bWinCarAfterJackpot = false;
    // prize: the special line paid (a car / clothes machine); the last
    // awardSlotPrize says whether the item itself was won or chips instead.
    bool playSlotJackpot( CasinoSlots::ePrize prize );
    bool bLastPrizeGranted = false;
    bool slotHeld[5] = {};
    int32_t holdCursor = 0;
    CasinoPoker::Card pokerHand[5];
    CasinoPoker::Card pokerDeck[52];
    int32_t pokerNext = 0;
    int32_t pokerStake = 0;
    bool bPokerDealt = false;
    uint32_t pokerRng = 0x2f1b3d57u;
    bool isSecondChanceMachine() const;
    void dealPokerSlot();
    void autoHold();
    void settleSecondChance();
    void drawHoldRow( Renderer2D &renderer, const Font *pFont );
    static int32_t PokerCell( const CasinoPoker::Card &card );
};
