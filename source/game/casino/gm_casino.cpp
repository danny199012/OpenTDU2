#include "shared.h"
#include "gm_casino.h"
#include "casino_trip.h"

#include "flash/gs_flash.h"
#include "database/gs_database.h"
#include "game/casino/casino_progression.h"
#include "game/cinematic/gm_cin_viewer.h"
#include "game/player_profile.h"
#include "game/clothes_shop.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include <SDL3/SDL.h>

namespace {
    // The chip bundle the "buy chips" entry sells (1:1 with the profile's money
    // [INF]: the retail exchange rate came from the server).
    constexpr int32_t kChipBundle = 1000;

    // Canvas (1280 x 720) layout.
    constexpr float kSlotsScale = 1.05f;
    constexpr float kRouletteScale = 0.92f;
    constexpr float kPokerScale = 0.95f;
    constexpr float kHeaderY = 44.0f;
    constexpr float kFooterY = 664.0f;

    struct FloorEntry {
        const char *pName;
        const char *pHint;
    };

    constexpr int32_t kFloorSlots = 0;
    constexpr int32_t kFloorRoulette = 1;
    constexpr int32_t kFloorPoker = 2;
    constexpr int32_t kFloorBuyChips = 3;
    constexpr int32_t kFloorProgress = 4;
    constexpr int32_t kFloorLeave = 5;
    constexpr int32_t kFloorCount = 6;

    const FloorEntry kFloor[kFloorCount] = {
        {"Slot machine",     "5 reels, 9 lines, 1..10 per line"               },
        {"Roulette",         "single zero, straight to dozens"                },
        {"Poker",            "video poker, 9/6 Jacks or Better"               },
        {"Buy chips",        "1 000 chips for 1 000"                          },
        {"Game progress",    "the casino objectives (DBCasinoObjectiveDetail)"},
        {"Leave the casino", "back to the island"                             },
    };
    // DBCasinoObjectiveDetail._filterb ids [DATA: the objective texts]:
    // roulette bet types and poker hand types.
    constexpr int32_t kRouletteBetStraight = 36; // "Straight-up number"
    constexpr int32_t kRouletteBetColumn = 43;   // "Column"
    constexpr int32_t kRouletteBetRedBlack = 44; // "Black or Red"
    constexpr int32_t kRouletteBetEvenOdd = 45;  // "Even or Odd"
    constexpr int32_t kRouletteBetHalf = 46;     // "Half"
    constexpr int32_t kRouletteBetDozen = 47;    // "Dozen"
    constexpr int32_t kRouletteBetZero = 48;     // "0 or 00"
    constexpr int32_t kPokerHandOnePair = 60;
    constexpr int32_t kPokerHandTwoPair = 61;
    constexpr int32_t kPokerHandThreeOfAKind = 62;
    constexpr int32_t kPokerHandStraight = 63;
    constexpr int32_t kPokerHandFlush = 64;
    constexpr int32_t kPokerHandFullHouse = 65;
    // The slot machine type: OpenTDU2's machine plays the 9 combinations of the
    // "TDU 2" machine (objective _filterb 10) [INF].
    constexpr int32_t kSlotMachineType = 10;
    // The "Play N times with 9 lines activated" objective (_category 18).
    constexpr int32_t kCasinoEventLinesPlayed = 18;
    // The main progression axis: DBCasinoLevel's first axis (32) carries the
    // game objectives; 33 is the variety axis, 34 / 35 the online / social ones
    // [DATA].
    constexpr int32_t kCasinoAxisMain = 32;
} // namespace

GMCasino::GMCasino() : GameMode( eGameMode::GM_Casino ) {}
GMCasino::~GMCasino() {}

uint64_t GMCasino::getSlotMachineLockHash() {
    return GetIdentifier64bit( "CASINO_FORBID_SLOT_MACHINE" );
}

bool GMCasino::isSlotMachineLocked() const {
    const std::vector<uint64_t> &unlocked =
        GetPlayerProfile().CasinoUnlockables;
    return std::find( unlocked.begin(), unlocked.end(),
                      getSlotMachineLockHash() ) != unlocked.end();
}

bool GMCasino::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.02f, 0.03f, 0.06f, 1.0f ) );

    // Self tests, like -rttest: run, report, leave (the log is the evidence).
    if ( const char *pSlots = CmdLine::GetArgValue( "-slotselftest" ) ) {
        CasinoSlots::selfTest( atoi( pSlots ) );
        pInstance->requestExit();
        return true;
    }
    if ( const char *pRoulette = CmdLine::GetArgValue( "-rouletteselftest" ) ) {
        CasinoRoulette::selfTest( atoi( pRoulette ) );
        pInstance->requestExit();
        return true;
    }
    if ( const char *pPoker = CmdLine::GetArgValue( "-pokerselftest" ) ) {
        CasinoPoker::selfTest( atoi( pPoker ) );
        pInstance->requestExit();
        return true;
    }

    // The wallet is the profile's (retail: CasinoSave / IsCasinoSaveData).
    PlayerProfile &profile = GetPlayerProfile();
    chips = profile.CasinoChips;
    if ( const char *pChips = CmdLine::GetArgValue( "-casinochips" ) ) {
        chips = atoi( pChips );
        // The floor wallet re-reads the profile every tick (the cheats' sync).
        profile.CasinoChips = chips;
    }
    slots.seed( static_cast<uint32_t>( SDL_GetTicks() ) );
    roulette.seed( static_cast<uint32_t>( SDL_GetTicks() ^ 0x5bf03635u ) );
    pokerRng = static_cast<uint32_t>( SDL_GetTicks() ^ 0x2f1b3d57u ) | 1u; // the Poker slot machine's deck
    // -casinoseed <n>: a fixed seed for both games, so an automated run (and
    // its screenshots) is reproducible — the same role -fixedtimestep plays for
    // the menus. 0 / absent means the clock.
    if ( const char *pSeed = CmdLine::GetArgValue( "-casinoseed" ) ) {
        const uint32_t seedValue = static_cast<uint32_t>( atoi( pSeed ) );
        slots.seed( seedValue );
        roulette.seed( seedValue ^ 0x5bf03635u );
        poker.seed( seedValue ^ 0x2f1b3d57u );
        pokerRng = ( seedValue ^ 0x6c8e9cf5u ) | 1u;
        OTDU_LOG_INFO( "GMCasino: seed %u\n", seedValue );
    }

    // The casino shell movie (Casino.vmf). It loads and parses (1280x960,
    // 135 frames, 1636 tags, 206 bitmaps, 2 fonts), but drawing it *before* the
    // native screens corrupts them: the header disappears and the glyphs of the
    // 2D layer come out as solid blocks. The flash display list leaves the 2D
    // state (mask / scissor) in a way the native draws after it do not survive
    // — see docs/re/casino_spec.md §5. It is opt-in: -casinomovie draws it,
    // -dumpcasinolabels only loads it to dump its timelines.
    bDrawShell = CmdLine::HasArg( "-casinomovie" );
    const bool bLoadShell =
        bDrawShell || CmdLine::HasArg( "-dumpcasinolabels" );
    if ( bLoadShell && gpFlash != nullptr ) {
        if ( FlashMovie *pMovie = gpFlash->getMovie( "casino" ) ) {
            bShellLoaded = casinoShell.load( pMovie );
        }
        if ( !bShellLoaded ) {
            OTDU_LOG_WARN( "GMCasino: Casino.vmf (FrontEnd/HiRes/casino.bnk) "
                           "not available\n" );
        }
    }

    // The pause menu of the casino: the retail 6-entry list (no photo, no gme
    // — the IsCasino branch of setSquareMenu).
    pauseMenu.setCasinoMode( true );
    if ( !pauseMenu.load() ) {
        OTDU_LOG_WARN(
            "GMCasino: the pause menu movie (MAIN.vmf) is not available\n" );
    }
    pauseMenu.setEntryEnabled( true, true, true );
    pushFlashGlobals();

    if ( CmdLine::HasArg( "-dumpcasinolabels" ) && bShellLoaded ) {
        DumpFlashSubtree( casinoShell, "", 6, 200 );
        DumpFlashFunctions( casinoShell );
    }
    if ( const char *pDumpMovie = CmdLine::GetArgValue( "-dumpcasinomovie" ) ) {
        // Any casino movie's tree and functions (cas_slotmachine,
        // cas_slotscreen, cas_slotlines, cas_roulette, cas_poker).
        if ( FlashMovie *pMovie = gpFlash != nullptr ? gpFlash->getMovie( pDumpMovie ) : nullptr ) {
            FlashPlayer dumpPlayer;
            if ( dumpPlayer.load( pMovie ) ) {
                for ( const std::string &label : pMovie->getVMF().getFrameLabels() ) {
                    OTDU_LOG_INFO( "  LABEL '%s'\n", label.c_str() );
                }
                for ( int32_t i = 0; i < 3; i++ ) {
                    dumpPlayer.tick( 1.0f / 30.0f );
                }
                DumpFlashRootTimeline( dumpPlayer, 400 );
                DumpFlashSubtree( dumpPlayer, "", 8, 400 );
                DumpFlashFunctions( dumpPlayer );
            }
        } else {
            OTDU_LOG_WARN( "GMCasino: movie '%s' not found\n", pDumpMovie );
        }
    }

    // The retail slot HUD (Cas_SlotMachine.vmf / Cas_SlotScreen.vmf).
    slotHud.load();
    // The retail bar and cashier screens (Cas_Common.vmf / Casino.vmf).
    deskHud.load();

    // The retail interior (the hall of the casino island): the room is loaded
    // here unless -nocasinointerior asks for the native floor menu. `-casinoroom
    // <hall|slots|roulette|poker|vip|dressing|shop>` boots straight into a room.
    if ( !CmdLine::HasArg( "-nocasinointerior" ) ) {
        CasinoInterior::eRoom firstRoom = CasinoInterior::eRoom::Hall;
        if ( const char *pRoom = CmdLine::GetArgValue( "-casinoroom" ) ) {
            for ( int32_t i = 1; i <= 7; i++ ) {
                const CasinoInterior::eRoom candidate = static_cast<CasinoInterior::eRoom>( i );
                if ( _stricmp( pRoom, CasinoInterior::RoomName( candidate ) ) == 0 ) {
                    firstRoom = candidate;
                    break;
                }
            }
        }
        if ( interior.enter( firstRoom ) ) {
            OTDU_LOG_INFO( "GMCasino: interior '%s' loaded\n", CasinoInterior::RoomName( firstRoom ) );
        } else {
            OTDU_LOG_WARN( "GMCasino: no casino interior bank, native floor\n" );
        }
    }

    setMessage( "Welcome to the casino" );
    refreshProgressLine();
    // -objectives: open the Game Progress page straight away (a test hook, like
    // -dealer <n> for the dealer menu).
    if ( CmdLine::HasArg( "-objectives" ) ) {
        objectives.open();
    }
    // -logcasino: the DBCasino* tables and where the profile stands on them.
    if ( CmdLine::HasArg( "-logcasino" ) ) {
        DumpCasinoTables();
        for ( int32_t axis = 0; axis < GetCasinoAxisCount(); axis++ ) {
            int32_t axisMax = 0;
            for ( const CasinoLevelRow &row : GetCasinoLevels() ) {
                if ( row.Axis == axis ) {
                    axisMax = Max( axisMax, row.Level );
                }
            }
            if ( axisMax > 0 ) {
                OTDU_LOG_INFO(
                    "Casino: axis %d -> %d points, level %d, next in %d\n",
                    axis, GetCasinoPoints( axis ), GetCasinoLevel( axis ),
                    GetCasinoPointsToNextLevel( axis ) );
            }
        }
        OTDU_LOG_INFO( "Casino: total level %d, status %s\n",
                       GetCasinoTotalLevel(),
                       GetCasinoStatusName( GetCasinoStatus() ) );
        DumpCasinoObjectiveProgress();
    }
    OTDU_LOG_INFO( "GMCasino: entered, %d chips, slot machine %s\n", chips,
                   isSlotMachineLocked() ? "locked" : "open" );
    return true;
}

void GMCasino::exit() {
    GetPlayerProfile().CasinoChips = chips;
    SaveProfile( "casino" );
    interior.exit();
    slotHud.unload();
    deskHud.unload();
    pauseMenu.unload();
    if ( bShellLoaded ) {
        casinoShell.unload();
        bShellLoaded = false;
    }
    if ( gpFlash != nullptr ) {
        gpFlash->releaseMovie( "casino" );
    }
}

void GMCasino::pushFlashGlobals() {
    // The retail casino globals (docs/re/ps3/flash_globals.txt): /:IsCasino,
    // /:HasCasino, /:IsCasinoStandAlone, /:IsCasinoSaveData, /:CasinoAvatar,
    // /:isProgressCasino, /:UseCasinoRankingList. Offline there is no session
    // and no ranking list, so those stay false until the casino progression
    // (DBCasinoLevel / DBCasinoObjectiveDetail / DBCasinoReward) is
    // implemented; the wallet is the save data.
    const auto push = []( FlashPlayer &player ) {
        player.setVariableBool( "/:IsCasino", true );
        player.setVariableBool( "/:HasCasino", true );
        player.setVariableBool( "/:IsCasinoStandAlone", false );
        player.setVariableBool( "/:UseCasinoRankingList", false );
        player.setVariableBool( "/:isProgressCasino", true );
        player.setVariableBool( "/:IsCasinoSaveData",
                                GetPlayerProfile().CasinoChips > 0 );
        player.setVariable( "/:CasinoAvatar", GetPlayerProfile().Name );
    };
    if ( bShellLoaded ) {
        push( casinoShell );
    }
    if ( pauseMenu.isLoaded() ) {
        push( pauseMenu.getPlayer() );
    }
}

void GMCasino::setMessage( const std::string &text ) {
    message = text;
    messageTimer = 3.0f;
    OTDU_LOG_INFO( "GMCasino: %s\n", text.c_str() );
}

void GMCasino::openSlots() {
    if ( isSlotMachineLocked() ) {
        setMessage( "The slot machine is locked (CASINO_FORBID_SLOT_MACHINE)" );
        return;
    }
    slots.setChips( chips );
    slots.setBetPerLine( 1 );
    screen = eScreen::Slots;
    OTDU_LOG_INFO( "GMCasino: slot machine, %d chips\n", chips );
}

void GMCasino::openRoulette() {
    roulette.setChips( chips );
    screen = eScreen::Roulette;
    cursorCol = 0;
    cursorRow = 1;
    OTDU_LOG_INFO( "GMCasino: roulette, %d chips\n", chips );
}

void GMCasino::openPoker() {
    poker.setChips( chips );
    poker.setBet( 1 );
    screen = eScreen::Poker;
    pokerCursor = 0;
    OTDU_LOG_INFO( "GMCasino: poker, %d chips\n", chips );
}

    // The machines' Type in the DBCasino objective vocabulary: the car machine
    // is type 0 and the clothes machine type 1 [DATA: the
    // CasinoSlotMachineTypeDesc Type fields].
    constexpr int32_t kSlotMachineTypeCar = 0;
    constexpr int32_t kSlotMachineTypeClothes = 1;
    // The casino clothes brand (DBBrands 0x57 "CASINO", the ESHOP_CA shop)
    // [DATA].
    constexpr uint64_t kCasinoClothesBrand = 0x57ull;
    // The machine the player stands at (see casino_slots.h).

int32_t GMCasino::rouletteBetFilter( CasinoRoulette::eBet bet ) {
    switch ( bet ) {
    case CasinoRoulette::eBet::Straight:
        return kRouletteBetStraight;
    case CasinoRoulette::eBet::Red:
    case CasinoRoulette::eBet::Black:
        return kRouletteBetRedBlack;
    case CasinoRoulette::eBet::Odd:
    case CasinoRoulette::eBet::Even:
        return kRouletteBetEvenOdd;
    case CasinoRoulette::eBet::Low:
    case CasinoRoulette::eBet::High:
        return kRouletteBetHalf;
    case CasinoRoulette::eBet::Dozen1:
    case CasinoRoulette::eBet::Dozen2:
    case CasinoRoulette::eBet::Dozen3:
        return kRouletteBetDozen;
    case CasinoRoulette::eBet::Column1:
    case CasinoRoulette::eBet::Column2:
    case CasinoRoulette::eBet::Column3:
        return kRouletteBetColumn;
    default:
        return 0;
    }
}

int32_t GMCasino::pokerHandFilter( CasinoPoker::eResult result ) {
    switch ( result ) {
    case CasinoPoker::eResult::JacksOrBetter:
        return kPokerHandOnePair;
    case CasinoPoker::eResult::TwoPair:
        return kPokerHandTwoPair;
    case CasinoPoker::eResult::ThreeOfAKind:
        return kPokerHandThreeOfAKind;
    case CasinoPoker::eResult::Straight:
        return kPokerHandStraight;
    case CasinoPoker::eResult::Flush:
        return kPokerHandFlush;
    case CasinoPoker::eResult::FullHouse:
        return kPokerHandFullHouse;
    default:
        return 0;
    }
}

void GMCasino::reportSlotSpin( int32_t payout,
                               int32_t winningLines,
                               bool bJackpot ) {
    // DBCasinoObjectiveDetail rows: _filtera 4 = slot machines, _filterb 10 =
    // the 9-combination machine OpenTDU2 plays.
    if ( payout > 0 ) {
        slotStreak++;
        ReportCasinoEvent( eCasinoEvent::MoneyWon, eCasinoGame::Slots, 0,
                           payout );
        ReportCasinoEvent( eCasinoEvent::BetWon, eCasinoGame::Slots,
                           kSlotMachineType, winningLines );
        ReportCasinoEvent( eCasinoEvent::WinStreak, eCasinoGame::Slots, 0,
                           slotStreak );
    } else {
        slotStreak = 0;
    }
    if ( bJackpot ) {
        ReportCasinoEvent( eCasinoEvent::Jackpot, eCasinoGame::Slots, 0, 1 );
    }
    // "Play 40 times with 9 lines activated" (_category 18) and "play N types
    // of slot machine" (_category 43 — a *distinct* type, so it is reported
    // once per machine; the port has one).
    ReportCasinoEvent( static_cast<eCasinoEvent>( kCasinoEventLinesPlayed ),
                       eCasinoGame::Slots, 0, 1 );
    if ( MarkCasinoSeen( "casino_slot_type_10" ) ) {
        ReportCasinoEvent( eCasinoEvent::PlayedType, eCasinoGame::Slots,
                           kSlotMachineType, 1 );
    }
    refreshProgressLine();
}

void GMCasino::reportRouletteRound() {
    const int32_t number = roulette.getWinningNumber();
    if ( number < 0 ) {
        return;
    }
    // A number seen for the first time ("See 12 of the possible numbers
    // drawn").
    PlayerProfile &profile = GetPlayerProfile();
    const uint64_t bit = 1ull << ( number & 63 );
    if ( ( profile.CasinoRouletteNumbers & bit ) == 0 ) {
        profile.CasinoRouletteNumbers |= bit;
        ReportCasinoEvent( eCasinoEvent::NumberSeen, eCasinoGame::Roulette, 0,
                           1 );
        if ( number == 0 ) {
            ReportCasinoEvent( eCasinoEvent::NumberSeen, eCasinoGame::Roulette,
                               kRouletteBetZero, 1 );
        }
    }
    // The winning bets of the round (category 5, _filterb = the bet type).
    if ( roulette.getLastPayout() > 0 ) {
        rouletteStreak++;
        ReportCasinoEvent( eCasinoEvent::MoneyWon, eCasinoGame::Roulette, 0,
                           roulette.getLastPayout() );
        ReportCasinoEvent( eCasinoEvent::WinStreak, eCasinoGame::Roulette, 0,
                           rouletteStreak );
    } else {
        rouletteStreak = 0;
    }
    refreshProgressLine();
}

void GMCasino::reportPokerHand() {
    const CasinoPoker::eResult result = poker.getResult();
    if ( result == CasinoPoker::eResult::None ) {
        pokerStreak = 0;
        return;
    }
    pokerStreak++;
    ReportCasinoEvent( eCasinoEvent::MoneyWon, eCasinoGame::PokerCash, 0,
                       poker.getLastPayout() );
    ReportCasinoEvent( eCasinoEvent::HandsWon, eCasinoGame::PokerCash, 0, 1 );
    ReportCasinoEvent( eCasinoEvent::WinStreak, eCasinoGame::PokerCash, 0,
                       pokerStreak );
    const int32_t hand = pokerHandFilter( result );
    if ( hand != 0 ) {
        ReportCasinoEvent( eCasinoEvent::BetWon, eCasinoGame::PokerTable, hand,
                           1 );
    }
    refreshProgressLine();
}

void GMCasino::refreshProgressLine() {
    const int32_t level = GetCasinoStatusLevel();
    const int32_t next = GetCasinoPointsToNextLevel( kCasinoAxisMain );
    int32_t completed = 0;
    int32_t total = 0;
    GetCasinoObjectiveProgress( kCasinoAxisMain, completed, total );
    char line[192] = {};
    if ( next > 0 ) {
        snprintf( line, sizeof( line ),
                  "%s - casino level %d (%d points, %d to the next) - "
                  "objectives %d/%d",
                  GetCasinoStatusName( GetCasinoStatus() ), level,
                  GetCasinoPoints( kCasinoAxisMain ), next, completed, total );
    } else {
        snprintf( line, sizeof( line ),
                  "%s - casino level %d (%d points, maxed) - objectives %d/%d",
                  GetCasinoStatusName( GetCasinoStatus() ), level,
                  GetCasinoPoints( kCasinoAxisMain ), completed, total );
    }
    progressLine = line;
}

void GMCasino::backToFloor() {
    // The wallet follows the game back to the floor (and from there to the
    // profile).
    if ( screen == eScreen::Slots ) {
        chips = slots.getChips();
    } else if ( screen == eScreen::Roulette ) {
        chips = roulette.getChips();
    } else if ( screen == eScreen::Poker ) {
        chips = poker.getChips();
    }
    screen = eScreen::Floor;
    relaunch = eRelaunch::None;
    bPokerDealt = false;
    for ( bool &held : slotHeld ) {
        held = false;
    }
    // (screen is already Floor here: the last interaction's neons, none for a
    // table.)
    interior.setMachineNeons( IntroCinematic::eNeonState::Inactive );
    slotHud.close();
    interior.releaseCamera(); // the walk view again (a machine's game camera)
    GetPlayerProfile().CasinoChips = chips;
    SaveProfile( "casino" );
    pushFlashGlobals();
    OTDU_LOG_INFO( "GMCasino: back on the floor with %d chips\n", chips );
}

void GMCasino::openCasinoShop() {
    // The casino's clothes shop: the DBCarShop row of the ESHOP_CA building
    // carries the brand 0x57 "CASINO" (DBClothe._brand) [DATA].
    bShopOpen = true;
    clothesMenu.open( "Casino - Boutique", kCasinoClothesBrand, PlayerClothesSex() );
}

void GMCasino::awardSlotPrize( CasinoSlots::ePrize prize ) {
    PlayerProfile &profile = GetPlayerProfile();
    bLastPrizeGranted = false;
    if ( prize == CasinoSlots::ePrize::Car ) {
        // The car machine's jackpot line: DBRewardCar _event "CASINO_CAR_1"
        // is the Audi R8 V10 (_carunlocked 432, colour row 12, rims 1)
        // [DATA: db_data.cpr]. Winning it once adds it to the garage; after
        // that the line pays chips instead.
        int32_t carId = 432;
        if ( gpDatabase != nullptr ) {
            XmbValue root = gpDatabase->getDocument().getRoot();
            for ( uint32_t i = 0; i < root.getArraySize( "DBRewardCar" ); i++ ) {
                XmbValue row = root.getArrayElement( "DBRewardCar", i );
                if ( row["_event"].asString() == "CASINO_CAR_1" ) {
                    carId = static_cast<int32_t>( row["_carunlocked"].asUInt64() );
                    break;
                }
            }
        }
        if ( profile.CasinoCarWon ) {
            slots.addChips( 250000 );
            setMessage( "JACKPOT - 250 000 chips (the car is already yours)" );
            OTDU_LOG_INFO( "GMCasino: car line again, 250000 chips paid\n" );
            return;
        }
        profile.CasinoCarWon = true;
        bLastPrizeGranted = true;
        profile.OwnedCars.push_back( carId );
        GrantCasinoUnlockable( CasinoUnlockableHash( "Car" ), "slot machine jackpot" );
        ReportCasinoEvent( eCasinoEvent::Jackpot, eCasinoGame::Slots, kSlotMachineTypeCar, 1 );
        SaveProfile( "casino car won" );
        setMessage( "JACKPOT! The Audi R8 V10 is yours - it is in your garage" );
        interior.showMessage( "JACKPOT! Audi R8 V10" );
        OTDU_LOG_INFO( "GMCasino: slot machine jackpot -> car %d added to the garage\n", carId );
        return;
    }
    if ( prize == CasinoSlots::ePrize::Clothes ) {
        // A casino clothes item the player does not own yet (DBClothe brand
        // 0x57 CASINO, the player's sex) [DATA].
        const int32_t sex = PlayerClothesSex();
        for ( const ClotheItem &item : GetClothes() ) {
            if ( item.BrandHashcode != kCasinoClothesBrand || ( sex != 0 && item.Sex != sex ) || item.Price >= 999999 ) {
                continue;
            }
            if ( std::find( profile.OwnedClothes.begin(), profile.OwnedClothes.end(), item.Id ) !=
                 profile.OwnedClothes.end() ) {
                continue;
            }
            profile.OwnedClothes.push_back( item.Id );
            bLastPrizeGranted = true;
            GrantCasinoUnlockable( CasinoUnlockableHash( "cloth_set_won" ), "slot machine clothes prize" );
            ReportCasinoEvent( eCasinoEvent::Jackpot, eCasinoGame::Slots, kSlotMachineTypeClothes, 1 );
            SaveProfile( "casino clothes won" );
            setMessage( "JACKPOT! You won: " + item.Name );
            interior.showMessage( "JACKPOT! " + item.Name );
            OTDU_LOG_INFO( "GMCasino: slot machine clothes prize '%s' (id %d)\n", item.Name.c_str(), item.Id );
            return;
        }
        // Everything owned: pay chips.
        slots.addChips( 5000 );
        setMessage( "JACKPOT - 5 000 chips (you own every casino outfit)" );
    }
}

void GMCasino::openCashier() {
    screen = eScreen::Cashier;
    cashierSelection = 0;
    if ( !deskHud.isCashierLoaded() ) {
        return;
    }
    PlayerProfile &profile = GetPlayerProfile();
    // The cashier hands a broke player AmountGivedToPlayer (200) [DATA:
    // Sc_Cashier]; CinematicGiveMoney / CinematicRefuse / C_DontGiveMoney_VIP
    // name the two outcomes (PS3 0xf48d18..0xf48d50). When: with no chips and
    // no money to buy any, once per visit of the casino [INF: the retail rule
    // also counted the time between visits, _cashier_time_between_visits_*].
    const int32_t gift = interior.getDeskData().CashierGift;
    if ( gift > 0 && chips <= 0 && profile.Money <= 0 && !bCashierGiftGiven ) {
        bCashierGiftGiven = true;
        chips += gift;
        profile.CasinoChips = chips;
        SaveProfile( "casino cashier gift" );
        setMessage( "The cashier gives you " + std::to_string( gift ) + " chips" );
        interior.showMessage( "The cashier gives you " + std::to_string( gift ) + " chips" );
        OTDU_LOG_INFO( "GMCasino: cashier gift %d chips\n", gift );
    }
    deskHud.openCashier( chips, profile.Money );
}

void GMCasino::openBar() {
    screen = eScreen::Bar;
    barSelection = 0;
    barStep = eBarStep::List;
    if ( !deskHud.isBarLoaded() ) {
        return;
    }
    // The first-tier cocktails (00, 01, 10, 11) carry no unlock condition
    // ("This cocktail is available.", no m_cockUnlock_XX) [DATA]; the others
    // are open once their Cocktail_XX unlockable is owned (the casino level
    // rewards grant them; their poker / sit & go conditions are in DBLoc).
    // -allcocktails opens the whole list (test hook).
    std::vector<std::string> unlocked;
    const bool bAll = CmdLine::HasArg( "-allcocktails" );
    for ( const std::string &id : CasinoDeskHud::CocktailIds() ) {
        bool bOpen = bAll || CasinoDeskHud::CocktailTier( id ) == 1;
        if ( !bOpen ) {
            const std::string name = "Cocktail_" + id;
            for ( const CasinoUnlockable &unlockable : GetCasinoUnlockables() ) {
                if ( unlockable.Description == name && IsCasinoUnlockableOwned( unlockable.Hashcode ) ) {
                    bOpen = true;
                }
            }
        }
        if ( bOpen ) {
            unlocked.push_back( id );
        }
    }
    deskHud.openBar( unlocked, !bRaffleTutorialShown );
    bRaffleTutorialShown = true;
}

void GMCasino::buyCocktail( const std::string &id ) {
    PlayerProfile &profile = GetPlayerProfile();
    const int32_t tier = CasinoDeskHud::CocktailTier( id );
    static const int32_t kPrice[4] = { 50, 200, 500, 1000 }; // m_cockprice_1..4 [DATA]
    const int32_t price = kPrice[tier - 1];
    if ( profile.Money < price ) {
        // setMsgMoneyBarman: DBXO 'not_enough_money' with m_err_money [DATA].
        deskHud.showMessage( "You do not have enough money" );
        OTDU_LOG_INFO( "GMCasino: cocktail %s costs $%d, the player has $%d\n", id.c_str(), price, profile.Money );
        barStep = eBarStep::None;
        return;
    }
    profile.Money -= price;
    const std::string name = "Cocktail_" + id;
    for ( const CasinoUnlockable &unlockable : GetCasinoUnlockables() ) {
        if ( unlockable.Description == name ) {
            profile.CasinoDrinks.push_back( static_cast<int32_t>( unlockable.Hashcode ) );
        }
    }
    // The Solar Crown Raffle: "If you find a cherry in your glass, you have
    // won" - the pass level is the cocktail's tier and the chance is the
    // barman's WinTicketLevel<N>ChancePercentage (1 / 2 / 3 / 5 %) [DATA:
    // m_tombo_desc2..6, Sc_Bar]. The draw itself was the server's [INF:
    // a uniform roll here]; -cherry forces a win (test hook).
    const CasinoInterior::DeskData &desk = interior.getDeskData();
    const int32_t chance = desk.TicketChance[tier - 1] > 0 ? desk.TicketChance[tier - 1] : tier;
    pokerRng = pokerRng * 1664525u + 1013904223u;
    const int32_t roll = static_cast<int32_t>( ( pokerRng >> 8 ) % 100u );
    raffleWon = CmdLine::HasArg( "-cherry" ) || roll < chance;
    raffleLevel = tier;
    if ( raffleWon ) {
        profile.CasinoRafflePasses[tier - 1]++;
    }
    SaveProfile( "casino cocktail" );
    ReportCasinoEvent( eCasinoEvent::FeatureUsed, eCasinoGame::Any, 0, 1 );
    OTDU_LOG_INFO( "GMCasino: cocktail %s (tier %d, $%d), raffle roll %d < %d %% -> %s\n", id.c_str(), tier, price,
                   roll, chance, raffleWon ? "WIN (cherry)" : "lose" );
    setMessage( "Cocktail: $" + std::to_string( price ) );
    // The barman prepares it (CinematicPrepareDrink), the avatar drinks it
    // (CinematicPlayerDrink, or CinematicPlayerDrinkWonTicket with the cherry)
    // [DATA: Sc_Bar]; then the raffle popup.
    barStep = eBarStep::Prepare;
    if ( !interior.isActive() || desk.PrepareCinematic.empty() || !interior.hasCinematic( desk.PrepareCinematic ) ||
         !interior.playCinematic( desk.PrepareCinematic ) ) {
        barStep = eBarStep::Drink;
        playDrinkCinematic();
    }
}

void GMCasino::playDrinkCinematic() {
    const CasinoInterior::DeskData &desk = interior.getDeskData();
    const std::string &name =
        raffleWon && !desk.DrinkWonCinematic.empty() ? desk.DrinkWonCinematic : desk.DrinkCinematic;
    if ( interior.isActive() && !name.empty() && interior.hasCinematic( name ) && interior.playCinematic( name ) ) {
        return;
    }
    barStep = eBarStep::Raffle;
    deskHud.showRaffle( raffleWon, raffleLevel );
}

void GMCasino::tickDeskHud( float deltaTime ) {
    // A bar cutscene: wait for it (A / B skip it).
    if ( interior.isActive() && interior.isCinematicPlaying() ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ||
             gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            interior.skipCinematic();
        }
        return;
    }
    if ( interior.isActive() && interior.takeCinematicEnded() ) {
        if ( barStep == eBarStep::Prepare ) {
            barStep = eBarStep::Drink;
            playDrinkCinematic();
        } else if ( barStep == eBarStep::Drink ) {
            barStep = eBarStep::Raffle;
            deskHud.showRaffle( raffleWon, raffleLevel );
        }
    }
    deskHud.routeInput();
    deskHud.tick( deltaTime );
    PlayerProfile &profile = GetPlayerProfile();
    for ( CasinoDeskHud::eEvent event : deskHud.takeEvents() ) {
        switch ( event ) {
        case CasinoDeskHud::eEvent::BuyCocktail:
            buyCocktail( deskHud.getCocktailId() );
            break;
        case CasinoDeskHud::eEvent::BarClosed:
            if ( barStep == eBarStep::List ) {
                barStep = eBarStep::None; // B: nothing bought
            }
            break;
        case CasinoDeskHud::eEvent::ConvertChoice: {
            // The slider runs over the source wallet: dollars for chx1, chips
            // for chx2; 1 chip = $1 [INF: the retail rate was the server's].
            const int32_t range = deskHud.isChipsToDollars() ? chips : profile.Money;
            deskHud.setConversionRange( range, range / 2 );
            break;
        }
        case CasinoDeskHud::eEvent::ConvertDone: {
            const int32_t amount = deskHud.getSliderValue();
            if ( deskHud.isChipsToDollars() ) {
                const int32_t sold = Min( amount, chips );
                chips -= sold;
                profile.Money += sold;
                setMessage( "Sold " + std::to_string( sold ) + " chips" );
            } else {
                const int32_t bought = Min( amount, profile.Money );
                profile.Money -= bought;
                chips += bought;
                setMessage( "Bought " + std::to_string( bought ) + " chips" );
            }
            profile.CasinoChips = chips;
            SaveProfile( "casino cashier" );
            pushFlashGlobals();
            OTDU_LOG_INFO( "GMCasino: cashier %s %d -> %d chips, $%d\n",
                           deskHud.isChipsToDollars() ? "chips->dollars" : "dollars->chips", amount, chips,
                           profile.Money );
            screen = eScreen::Floor;
            break;
        }
        case CasinoDeskHud::eEvent::CashierClosed:
            screen = eScreen::Floor;
            break;
        default:
            break;
        }
    }
    if ( screen == eScreen::Bar && barStep == eBarStep::Raffle && !deskHud.isRaffleShown() ) {
        barStep = eBarStep::None;
    }
    if ( screen == eScreen::Bar && barStep == eBarStep::None && !deskHud.isBarOpen() ) {
        screen = eScreen::Floor;
    }
}

void GMCasino::tickCashier( float deltaTime ) {
    if ( deskHud.isCashierLoaded() ) {
        tickDeskHud( deltaTime );
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        cashierSelection = ( cashierSelection + 1 ) % 6;
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        cashierSelection = ( cashierSelection + 5 ) % 6;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        screen = eScreen::Floor;
        return;
    }
    if ( !gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        return;
    }
    PlayerProfile &profile = GetPlayerProfile();
    // The retail exchange rate came from the server [INF]: 1 chip = 1 unit of
    // money both ways, in bundles.
    static const int32_t kBundle[3] = { 1000, 10000, 100000 };
    if ( cashierSelection < 3 ) {
        const int32_t bundle = kBundle[cashierSelection];
        if ( profile.Money < bundle ) {
            setMessage( "Not enough money" );
            return;
        }
        profile.Money -= bundle;
        chips += bundle;
        setMessage( "Bought " + std::to_string( bundle ) + " chips" );
    } else if ( cashierSelection == 3 ) {
        if ( chips < 1000 ) {
            setMessage( "You have fewer than 1 000 chips" );
            return;
        }
        chips -= 1000;
        profile.Money += 1000;
        setMessage( "Sold 1 000 chips" );
    } else if ( cashierSelection == 4 ) {
        if ( chips <= 0 ) {
            setMessage( "You have no chips" );
            return;
        }
        profile.Money += chips;
        setMessage( "Sold " + std::to_string( chips ) + " chips" );
        chips = 0;
    } else {
        screen = eScreen::Floor;
        return;
    }
    profile.CasinoChips = chips;
    SaveProfile( "casino cashier" );
    pushFlashGlobals();
}

void GMCasino::tickBar( float deltaTime ) {
    if ( deskHud.isBarLoaded() ) {
        tickDeskHud( deltaTime );
        return;
    }
    const std::vector<CasinoUnlockable> &unlockables = GetCasinoUnlockables();
    const int32_t count = static_cast<int32_t>( unlockables.size() );
    if ( count > 0 ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            barSelection = ( barSelection + 1 ) % count;
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            barSelection = ( barSelection + count - 1 ) % count;
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        screen = eScreen::Floor;
        return;
    }
    if ( !gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        return;
    }
    // The cocktails of DBCasinoUnlockable (Cocktail_00 .. Cocktail_24) [DATA].
    if ( count == 0 ) {
        setMessage( "The bar is not open" );
        return;
    }
    if ( barSelection >= count ) {
        barSelection = 0;
    }
    const CasinoUnlockable &drink = unlockables[barSelection];
    if ( drink.Description.rfind( "Cocktail_", 0 ) != 0 ) {
        setMessage( "That is not a drink" );
        return;
    }
    PlayerProfile &profile = GetPlayerProfile();
    constexpr int32_t kDrinkPrice = 25;
    if ( profile.Money < kDrinkPrice ) {
        setMessage( "A drink costs $" + std::to_string( kDrinkPrice ) );
        return;
    }
    profile.Money -= kDrinkPrice;
    profile.CasinoDrinks.push_back( static_cast<int32_t>( drink.Hashcode ) );
    GrantCasinoUnlockable( drink.Hashcode, "casino bar" );
    SaveProfile( "casino drink" );
    setMessage( "You drink a " + drink.Description );
    ReportCasinoEvent( eCasinoEvent::FeatureUsed, eCasinoGame::Any, 0, 1 );
}

void GMCasino::tickWardrobe( float deltaTime ) {
    (void)deltaTime;
    const PlayerProfile &profile = GetPlayerProfile();
    const int32_t count = static_cast<int32_t>( profile.OwnedClothes.size() );
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        wardrobeSelection = count > 0 ? ( wardrobeSelection + 1 ) % count : 0;
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        wardrobeSelection = count > 0 ? ( wardrobeSelection + count - 1 ) % count : 0;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        bWardrobeOpen = false;
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) && count > 0 && wardrobeSelection < count ) {
        const ClotheItem *pItem = FindClothe( profile.OwnedClothes[wardrobeSelection] );
        if ( pItem != nullptr ) {
            setMessage( "Wearing: " + pItem->Name );
        }
    }
}

void GMCasino::buyChips() {
    PlayerProfile &profile = GetPlayerProfile();
    if ( profile.Money < kChipBundle ) {
        setMessage( "Not enough money for a chip bundle" );
        return;
    }
    profile.Money -= kChipBundle;
    chips += kChipBundle;
    profile.CasinoChips = chips;
    SaveProfile( "casino chips" );
    pushFlashGlobals();
    setMessage( "Bought 1 000 chips" );
}

void GMCasino::leaveCasino() {
    GetPlayerProfile().CasinoChips = chips;
    SaveProfile( "casino" );
    // Out of the door (the owner, 2026-11-05: "Casino should spawn out side
    // casino"): FREERIDE on the casino island at the ECAS spot's road-side
    // entrance; the seaplane home is taken from there (FREERIDE, X / Square
    // at the dock). -casinoflyhome keeps the old direct flight home.
    Vector3f door;
    float doorHeading = 0.0f;
    if ( !CmdLine::HasArg( "-casinoflyhome" ) && CasinoTripScenes::FindCasinoDoor( door, doorHeading ) ) {
        StoryHandoff &handoff = GetStoryHandoff();
        handoff = StoryHandoff();
        handoff.bSet = true;
        handoff.bStory = false;
        handoff.bWhiteIn = false;
        handoff.Island = "casino";
        handoff.Position = door;
        handoff.Yaw = doorHeading;
        OTDU_LOG_INFO( "GMCasino: leaving with %d chips -> FREERIDE at the casino door (%.1f, %.1f, %.1f)\n", chips,
                       door.x, door.y, door.z );
        pGameInstance->setNextGameMode( eGameMode::GM_FreeRide );
        return;
    }
    // The seaplane home: the casino departure, the landing at the dock the
    // player flew from, then the drive (docs/re/casino_spec.md 10).
    if ( CasinoTripScenes::RequestFlightHome() ) {
        OTDU_LOG_INFO( "GMCasino: leaving with %d chips -> the seaplane home\n", chips );
        pGameInstance->setNextGameMode( eGameMode::GM_CinViewer );
        return;
    }
    OTDU_LOG_INFO( "GMCasino: leaving with %d chips -> FREERIDE\n", chips );
    pGameInstance->setNextGameMode( eGameMode::GM_FreeRide );
}

void GMCasino::tick( float deltaTime ) {
    if ( bShellLoaded ) {
        casinoShell.tick( deltaTime );
    }
    if ( messageTimer > 0.0f ) {
        messageTimer = Max( 0.0f, messageTimer - deltaTime );
    }

    // Keep the local wallet in sync with the profile: the cheats panel (F4,
    // Cheats::setChips / addChips in source/game/cheats.cpp) writes
    // profile.CasinoChips directly while the casino is open. Without this the
    // header, the cashier and the tables keep showing the stale local value,
    // and exit() / backToFloor() would overwrite profile.CasinoChips with it,
    // losing the cheat. While at a table the table's own balance overwrites
    // chips below, so only the floor wallet is affected here.
    chips = GetPlayerProfile().CasinoChips;

    // The Game Progress page owns the frame while it is up.
    if ( objectives.isOpen() ) {
        objectives.tick( deltaTime );
        return;
    }

    // ESC: the pause menu (the retail casino list: resume / game_progress /
    // players / options / profiles / exit_game). Everything is frozen while it
    // is up.
    if ( pauseMenu.isOpen() ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            pauseMenu.close();
            return;
        }
        pauseMenu.tick( deltaTime );
        switch ( pauseMenu.takeResult() ) {
        case PauseMenu::eResult::Resume:
            pauseMenu.close();
            break;
        case PauseMenu::eResult::Progress:
            // The casino's Game Progress page (the retail IsCasino list entry).
            pauseMenu.close();
            objectives.open();
            break;
        case PauseMenu::eResult::Quit:
            pauseMenu.close();
            OTDU_LOG_INFO(
                "GMCasino: exit game from the pause menu -> LOGIN\n" );
            pGameInstance->setNextGameMode( eGameMode::GM_Login );
            break;
        default:
            break;
        }
        return;
    }
    if ( pauseMenu.isLoaded() &&
         gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        pauseMenu.open();
        return;
    }

    // Test hook: -casinoleave <frame> leaves the casino (the flight home).
    if ( CmdLine::HasArg( "-casinoleave" ) ) {
        static int32_t leaveFrame = 0;
        if ( ++leaveFrame == CmdLine::GetArgInt( "-casinoleave", 120 ) ) {
            leaveCasino();
            return;
        }
    }
    // Test hook: -casinoprize car|clothes pays a slot-machine prize without a
    // spin (the -dealer pattern: it proves the car / clothes grant end to end).
    if ( const char *pPrize = CmdLine::GetArgValue( "-casinoprize" ) ) {
        // With -casinoselect it waits for the machine and plays the jackpot
        // cutscene as a real win does (the car: then the hall's C_WINCAR).
        static int32_t prizeFrame = 0;
        const bool bAtMachine = CmdLine::HasArg( "-casinoselect" );
        if ( !bAtMachine || screen == eScreen::Slots ) {
            prizeFrame++;
        }
        if ( prizeFrame == 60 ) {
            const bool bCar = _stricmp( pPrize, "car" ) == 0;
            const bool bFirstCar = bCar && !GetPlayerProfile().CasinoCarWon;
            awardSlotPrize( bCar ? CasinoSlots::ePrize::Car : CasinoSlots::ePrize::Clothes );
            if ( bAtMachine && screen == eScreen::Slots &&
                 playSlotJackpot( bCar ? CasinoSlots::ePrize::Car : CasinoSlots::ePrize::Clothes ) ) {
                bWinCarAfterJackpot = bFirstCar && !CmdLine::HasArg( "-nocasinowincar" );
            }
        }
    }

    switch ( screen ) {
    case eScreen::Floor:
        if ( interior.isActive() ) {
            tickInterior( deltaTime );
        } else {
            tickFloor( deltaTime );
        }
        break;
    case eScreen::Slots:
        if ( interior.isActive() ) {
            interior.tickBackground( deltaTime );
        }
        tickSlots( deltaTime );
        break;
    case eScreen::Roulette:
        if ( interior.isActive() ) {
            interior.tickBackground( deltaTime );
        }
        tickRoulette( deltaTime );
        break;
    case eScreen::Poker:
        if ( interior.isActive() ) {
            interior.tickBackground( deltaTime );
        }
        tickPoker( deltaTime );
        break;
    case eScreen::Cashier:
        if ( interior.isActive() ) {
            interior.tickBackground( deltaTime );
        }
        tickCashier( deltaTime );
        break;
    case eScreen::Bar:
        if ( interior.isActive() ) {
            interior.tickBackground( deltaTime );
        }
        tickBar( deltaTime );
        break;
    }
}

void GMCasino::tickFloor( float deltaTime ) {
    (void)deltaTime;
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        floorSelection = ( floorSelection + kFloorCount - 1 ) % kFloorCount;
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        floorSelection = ( floorSelection + 1 ) % kFloorCount;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        switch ( floorSelection ) {
        case kFloorSlots:
            openSlots();
            break;
        case kFloorRoulette:
            openRoulette();
            break;
        case kFloorPoker:
            openPoker();
            break;
        case kFloorBuyChips:
            buyChips();
            break;
        case kFloorProgress:
            objectives.open();
            break;
        case kFloorLeave:
        default:
            leaveCasino();
            break;
        }
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        leaveCasino();
    }
}

void GMCasino::tickInterior( float deltaTime ) {
    // The shop / the wardrobe own the frame while they are up.
    if ( bShopOpen ) {
        interior.tickBackground( deltaTime );
        clothesMenu.tick( deltaTime );
        if ( !clothesMenu.isOpen() ) {
            bShopOpen = false;
            interior.showMessage( "The shop thanks you" );
        }
        return;
    }
    if ( bWardrobeOpen ) {
        interior.tickBackground( deltaTime );
        tickWardrobe( deltaTime );
        return;
    }
    const CasinoInterior::eAction action = interior.tick( deltaTime );
    if ( action != CasinoInterior::eAction::None ) {
        handleInteriorAction( action );
        return;
    }
    // Test hook: -casinoselect <n> plays the n-th machine / table of the room
    // as if the player had walked up to it (the -dealer pattern).
    if ( const char *pSelect = CmdLine::GetArgValue( "-casinoselect" ) ) {
        static int32_t selectFrame = 0;
        if ( ++selectFrame == 90 ) {
            const CasinoInterior::eAction selected = interior.selectPlace( atoi( pSelect ) );
            if ( selected != CasinoInterior::eAction::None ) {
                handleInteriorAction( selected );
            }
        }
    }
}

void GMCasino::handleInteriorAction( CasinoInterior::eAction action ) {
    const CasinoInterior::Interaction &interaction = interior.getInteraction();
    switch ( action ) {
    case CasinoInterior::eAction::Leave:
        leaveCasino();
        break;
    case CasinoInterior::eAction::BackToHall:
        if ( !interior.changeRoom( CasinoInterior::eRoom::Hall ) ) {
            leaveCasino();
        }
        break;
    case CasinoInterior::eAction::Slots:
        interior.changeRoom( CasinoInterior::eRoom::Slots );
        break;
    case CasinoInterior::eAction::Roulette:
        interior.changeRoom( CasinoInterior::eRoom::Roulette );
        break;
    case CasinoInterior::eAction::Poker:
        interior.changeRoom( CasinoInterior::eRoom::Poker );
        break;
    case CasinoInterior::eAction::SitAndGo:
        // The Sit & Go tournaments need other players: they stay online for
        // now (docs/re/casino_spec.md 8). The room is the poker room.
        interior.changeRoom( CasinoInterior::eRoom::Poker );
        setMessage( "The Sit & Go tournaments need opponents (online)" );
        break;
    case CasinoInterior::eAction::VIP: {
        // The VIP lounge is the retail VipAccess unlockable [DATA:
        // DBCasinoUnlockable "VipAccess"].
        if ( GetCasinoStatus() == eCasinoStatus::Player ) {
            setMessage( "The VIP lounge opens at the VIP status (casino level 5)" );
            interior.showMessage( "VIP access is for VIP members" );
        } else {
            interior.changeRoom( CasinoInterior::eRoom::VIP );
        }
        break;
    }
    case CasinoInterior::eAction::Shop:
        interior.changeRoom( CasinoInterior::eRoom::Shop );
        openCasinoShop();
        break;
    case CasinoInterior::eAction::Dressing:
        interior.changeRoom( CasinoInterior::eRoom::Dressing );
        bWardrobeOpen = true;
        wardrobeSelection = 0;
        break;
    case CasinoInterior::eAction::Cashier:
        openCashier();
        break;
    case CasinoInterior::eAction::Bar:
        openBar();
        break;
    case CasinoInterior::eAction::GameProgress:
        objectives.open();
        break;
    case CasinoInterior::eAction::SlotMachine: {
        // The machine the player stands at (SlotMachineCar, SlotMachineClothes,
        // SlotMachine_Poker, SlotMachineHawai, SlotMachineClassic, HighLife).
        const std::string &name = interaction.EntityName;
        CasinoSlots::eMachine machine = CasinoSlots::eMachine::HighLife;
        if ( name == "SlotMachineCar" ) {
            machine = CasinoSlots::eMachine::Car;
        } else if ( name == "SlotMachineClothes" ) {
            machine = CasinoSlots::eMachine::Clothes;
        } else if ( name == "SlotMachine_Poker" || name == "SlotMachinePoker" ) {
            machine = CasinoSlots::eMachine::Poker;
        } else if ( name == "SlotMachineHawai" ) {
            machine = CasinoSlots::eMachine::Hawai;
        } else if ( name == "SlotMachineClassic" ) {
            machine = CasinoSlots::eMachine::Classic;
        }
        slots.setMachine( machine );
        openSlots();
        // The real machine: the player looks at it through its own game
        // camera (CameraGame) while it is played [DATA]; -noslotcamera keeps
        // the walk view.
        if ( screen == eScreen::Slots && !CmdLine::HasArg( "-noslotcamera" ) ) {
            interior.focusCamera( interaction.CameraGameHash );
        }
        slotReels = interaction.NumReels;
        slotAtlasColumns = interaction.AtlasColumns;
        slotAtlasRows = interaction.AtlasRows;
        slotSpinnerType = interaction.SpinnerType;
        slotRotationSpeed = interaction.RotationSpeed > 0.0f ? interaction.RotationSpeed : 1.3f;
        for ( float &roll : reelRoll ) {
            roll = 0.0f;
        }
        if ( interior.isCameraFocused() ) {
            // The machine's own reels and stop timing (TimeBeforeFirstStop 1.5 s;
            // TimeAfterPrevSpinner 0.5 s on the clothes machines, 0 elsewhere:
            // those stop one after the other every 0.35 s [INF]).
            slots.setReelLayout( slotReels, interaction.TimeBeforeFirstStop,
                                 interaction.TimeAfterPrevSpinner > 0.0f ? interaction.TimeAfterPrevSpinner : 0.35f );
            // Each reel's own delay: reel k stops TimeBeforeFirstStop + the sum
            // of TimeAfterPrevSpinner of reels 0..k after the order (High Life
            // 1.5 / 2.2 / 2.9 / 3.8 / 4.9 s) [DATA: the values; the running sum
            // is INF from the name].
            std::vector<float> stops;
            float at = interaction.TimeBeforeFirstStop;
            for ( float after : interaction.ReelStopAfter ) {
                at += after;
                stops.push_back( stops.empty() || at > stops.back() + 0.05f ? at : stops.back() + 0.05f );
            }
            slots.setReelStops( stops );
            updateReels( 0.0f );
        } else {
            slots.setReelLayout( 0, 0.0f, 0.0f );
        }
        // The machine's neons light up while it is played (NeonActive) [INF:
        // the state changes are the exe's; the states are the entity's].
        if ( screen == eScreen::Slots ) {
            interior.setMachineNeons( IntroCinematic::eNeonState::Active );
        }
        if ( screen == eScreen::Slots && slotHud.isLoaded() ) {
            // The movie starts with one activated line (resetVariables:
            // nbActiveLines 1) [DATA].
            slots.setActiveLines( 1 );
            slotHud.open( static_cast<int32_t>( machine ), slots.getMachinePrice(),
                          slotReels > 0 ? slotReels : slots.getReelCount(), slots.getChips(), PlayerClothesSex() == 2,
                          slots.isPricePerLine() );
            slotHud.setScreenValues( slots.getChips(), slots.getWager() );
        }
        break;
    }
    case CasinoInterior::eAction::RouletteTable:
        roulette.setTablePrice( interaction.Price );
        openRoulette();
        break;
    case CasinoInterior::eAction::PokerTable:
        // The table's CasinoPokerTypeDesc Price is its minimum stake [DATA];
        // the native 9/6 Jacks or Better screen plays coins of 1..5.
        OTDU_LOG_INFO( "GMCasino: poker table '%s' (id %d, min %d)\n", interaction.EntityName.c_str(),
                       interaction.GameId, interaction.Price );
        openPoker();
        break;
    default:
        break;
    }
}

bool GMCasino::playSlotJackpot( CasinoSlots::ePrize prize ) {
    if ( !interior.isActive() || CmdLine::HasArg( "-nocasinojackpotcin" ) ) {
        return false;
    }
    // The jackpot cutscenes of icasslo1.bnk, the _W twin for a woman avatar
    // [DATA: the names and what they show; the pick by name is INF - the
    // choosing code is not in the readable part of the exe]:
    //   C_JACKPOT[_W] ("NewJackpot"): the car won - the CF_KCar key ring drops
    //     into the machine's tray (y 1.01 -> 0.64 m) beside 'Win_A_Car';
    //   C_JACKPOT_CAR_MONEY: the car machine paying chips (the car is owned);
    //   C_JACKPOT_CLOTHES / C_CLOTHES_MONEY: an outfit won / chips instead;
    //   C_JACKPOT_POKER / _HAWAI / _HIGH_LIFE / _PROGRESSIF: the other kinds.
    std::string base = "C_JACKPOT";
    const bool bCarWon = prize == CasinoSlots::ePrize::Car && bLastPrizeGranted;
    switch ( slots.getMachine() ) {
    case CasinoSlots::eMachine::Car:
        base = bCarWon ? "C_JACKPOT" : "C_JACKPOT_CAR_MONEY";
        break;
    case CasinoSlots::eMachine::Clothes:
        base = prize == CasinoSlots::ePrize::Clothes && !bLastPrizeGranted ? "C_CLOTHES_MONEY" : "C_JACKPOT_CLOTHES";
        break;
    case CasinoSlots::eMachine::Poker:
        base = "C_JACKPOT_POKER";
        break;
    case CasinoSlots::eMachine::Hawai:
        base = "C_JACKPOT_HAWAI";
        break;
    case CasinoSlots::eMachine::HighLife:
        base = "C_JACKPOT_HIGH_LIFE";
        break;
    case CasinoSlots::eMachine::Classic:
        base = "C_JACKPOT_PROGRESSIF";
        break;
    default:
        break;
    }
    const bool bWoman = PlayerClothesSex() == 1;
    const int32_t place = interior.getInteraction().Index;
    for ( const std::string &name :
          { base + ( bWoman ? "_W" : "" ), base, std::string( bWoman ? "C_JACKPOT_W" : "C_JACKPOT" ) } ) {
        if ( interior.hasCinematic( name ) && interior.playCinematicAtPlace( name, place ) ) {
            OTDU_LOG_INFO( "GMCasino: jackpot cutscene '%s' at machine %d\n", name.c_str(), place );
            return true;
        }
    }
    return false;
}

void GMCasino::tickSlots( float deltaTime ) {
    // A jackpot cutscene: the machine waits (A / B skip it, CasinoInterior).
    if ( interior.isActive() && interior.isCinematicPlaying() ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            interior.skipCinematic();
        }
        return;
    }
    if ( interior.isActive() && interior.takeCinematicEnded() && bWinCarAfterJackpot ) {
        // The first car: off the machine, to the hall, where the hostess and
        // the director hand it over on the podium (C_WINCAR, icashal.bnk).
        bWinCarAfterJackpot = false;
        backToFloor();
        if ( interior.changeRoom( CasinoInterior::eRoom::Hall ) ) {
            interior.playCinematic( "C_WINCAR" );
        }
        return;
    }
    if ( handleTime >= 0.0f ) {
        handleTime += deltaTime;
        const float duration = interior.getHandleDuration();
        if ( duration <= 0.0f || handleTime > duration ) {
            handleTime = -1.0f;
        }
        interior.setHandleTime( handleTime );
    }
    const bool bWasSpinning = slots.isSpinning();
    slots.tick( deltaTime );
    updateReels( deltaTime );
    chips = slots.getChips();
    if ( bWasSpinning && !slots.isSpinning() && isSecondChanceMachine() && relaunch == eRelaunch::None ) {
        // The first draw of a second chance machine: nothing is paid yet; the
        // winning cards / reels lock themselves and the player may change
        // them (m_smpo_p3_3, m_smh1_p4_3) [DATA].
        autoHold();
        relaunch = eRelaunch::Holding;
        holdCursor = 0;
        slotHud.enterRelaunch();
        OTDU_LOG_INFO( "GMCasino: second chance - held %d%d%d%d%d\n", slotHeld[0], slotHeld[1], slotHeld[2],
                       slotHeld[3], slotHeld[4] );
    } else if ( bWasSpinning && !slots.isSpinning() && slots.getMachine() == CasinoSlots::eMachine::Poker &&
                relaunch == eRelaunch::Second ) {
        settleSecondChance();
    } else if ( bWasSpinning && !slots.isSpinning() ) {
        if ( relaunch == eRelaunch::Second ) {
            relaunch = eRelaunch::None; // the Beach machine's respin paid like a spin
            slotHud.leaveRelaunch();
            for ( bool &held : slotHeld ) {
                held = false;
            }
        }
        // The spin settled: report it to the casino objectives.
        const CasinoSlots::Spin &spin = slots.getLastSpin();
        bool bJackpot = false;
        for ( const CasinoSlots::Win &win : spin.Wins ) {
            if ( win.Symbol == CasinoSlots::eSymbol::Jackpot ) {
                bJackpot = true;
                break;
            }
        }
        reportSlotSpin( spin.Payout, static_cast<int32_t>( spin.Wins.size() ),
                        bJackpot );
        // The car / clothes machines' special line.
        const bool bFirstCar = slots.getPrize() == CasinoSlots::ePrize::Car && !GetPlayerProfile().CasinoCarWon;
        if ( slots.getPrize() != CasinoSlots::ePrize::None ) {
            awardSlotPrize( slots.getPrize() );
        }
        // The jackpot line plays the machine's jackpot cutscene, at the
        // machine; the first car won then goes on to the hall ceremony.
        if ( ( bJackpot || slots.getPrize() != CasinoSlots::ePrize::None ) && playSlotJackpot( slots.getPrize() ) ) {
            bWinCarAfterJackpot = bFirstCar && !CmdLine::HasArg( "-nocasinowincar" );
        }
        slotHud.screenResult( spin.Payout );
        // The pay panel of each winning combination flashes: ComboActors[n],
        // n = the symbol's rank in the machine's set [INF: the retail
        // combination order is the server's paytable].
        for ( const CasinoSlots::Win &win : spin.Wins ) {
            interior.flashCombo( static_cast<int32_t>( win.Symbol ), 3.0f );
        }
        // NeonWin / NeonJackpot / NeonLoose on the result [INF: which beat].
        interior.setMachineNeons( bJackpot ? IntroCinematic::eNeonState::Jackpot
                                  : spin.Payout > 0 ? IntroCinematic::eNeonState::Win
                                                    : IntroCinematic::eNeonState::Loose );
    }
    if ( slotHud.isOpen() ) {
        // The retail HUD owns the input: the movie's menu, its active controls
        // and popups answer through FSCommands (CasinoSlotHud).
        slotHud.routeInput();
        slotHud.tick( deltaTime );
        handleSlotHudEvents();
        return;
    }
    if ( slots.isSpinning() ) {
        return; // no input while the reels turn
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        startSlotSpin();
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        slots.stepBetPerLine( -1 );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        slots.stepBetPerLine( 1 );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuOption1 ) ||
                gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        slots.setBetPerLine( CasinoSlots::kMaxBetPerLine );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        backToFloor();
    }
}

bool GMCasino::isSecondChanceMachine() const {
    // define_SM_Options: machineIsRelaunch for smpo (Poker) and smh1 (Beach)
    // [DATA: Cas_SlotMachine.vmf].
    return slots.getMachine() == CasinoSlots::eMachine::Poker || slots.getMachine() == CasinoSlots::eMachine::Hawai;
}

int32_t GMCasino::PokerCell( const CasinoPoker::Card &card ) {
    // The 'Cards' sheet (cards02.2db, 8 x 8): hearts, spades, diamonds, clubs,
    // each A, 2 .. 10, J, Q, K in reading order; cell 52 the back [DATA].
    static const int32_t kSuitBlock[4] = { 1, 0, 2, 3 }; // eSuit Spades, Hearts, Diamonds, Clubs
    const int32_t rank = card.Rank == static_cast<uint8_t>( CasinoPoker::eRank::Ace ) ? 0 : card.Rank + 1;
    return kSuitBlock[card.Suit & 3] * 13 + rank;
}

void GMCasino::dealPokerSlot() {
    auto next = [this]() {
        pokerRng ^= pokerRng << 13;
        pokerRng ^= pokerRng >> 17;
        pokerRng ^= pokerRng << 5;
        return pokerRng;
    };
    for ( int32_t i = 0; i < 52; i++ ) {
        pokerDeck[i].Rank = static_cast<uint8_t>( i % 13 );
        pokerDeck[i].Suit = static_cast<uint8_t>( i / 13 );
    }
    for ( int32_t i = 51; i > 0; i-- ) {
        std::swap( pokerDeck[i], pokerDeck[next() % static_cast<uint32_t>( i + 1 )] );
    }
    for ( int32_t i = 0; i < 5; i++ ) {
        pokerHand[i] = pokerDeck[i];
    }
    pokerNext = 5;
    bPokerDealt = true;
}

void GMCasino::autoHold() {
    for ( bool &held : slotHeld ) {
        held = false;
    }
    if ( slots.getMachine() == CasinoSlots::eMachine::Poker ) {
        // The cards of the paying hand: every card of a pair / three / four of
        // a kind, or all five for a straight, a flush or better [INF: "the
        // cards which compose a winning combination are automatically locked"].
        const CasinoPoker::eResult result = CasinoPoker::evaluate( pokerHand );
        if ( result == CasinoPoker::eResult::None ) {
            return;
        }
        if ( result >= CasinoPoker::eResult::Straight && result != CasinoPoker::eResult::FourOfAKind ) {
            for ( bool &held : slotHeld ) {
                held = true;
            }
            return;
        }
        for ( int32_t i = 0; i < 5; i++ ) {
            int32_t same = 0;
            for ( int32_t j = 0; j < 5; j++ ) {
                same += pokerHand[j].Rank == pokerHand[i].Rank ? 1 : 0;
            }
            slotHeld[i] = same >= 2;
        }
        return;
    }
    // Beach: the reels of each winning line's run (reel 0 .. Count - 1).
    for ( const CasinoSlots::Win &win : slots.getLastSpin().Wins ) {
        for ( int32_t reel = 0; reel < win.Count && reel < 5; reel++ ) {
            slotHeld[reel] = true;
        }
    }
}

void GMCasino::settleSecondChance() {
    // The Poker machine's second draw: the Jacks or Better hand pays the
    // stake x the paytable (CasinoPoker 9/6) [INF: the machine's own
    // paytable was the server's].
    const CasinoPoker::eResult result = CasinoPoker::evaluate( pokerHand );
    const int32_t payout = CasinoPoker::getPayoutForBet( result, 1 ) * pokerStake;
    slots.addChips( payout );
    chips = slots.getChips();
    relaunch = eRelaunch::None;
    for ( bool &held : slotHeld ) {
        held = false;
    }
    slotHud.leaveRelaunch();
    slotHud.screenResult( payout );
    reportSlotSpin( payout, payout > 0 ? 1 : 0, result == CasinoPoker::eResult::RoyalFlush );
    if ( result != CasinoPoker::eResult::None ) {
        // ComboActors[n]: the 9 Poker pay panels, n = the hand (Jacks or
        // better .. royal flush) [INF: the order].
        interior.flashCombo( static_cast<int32_t>( result ) - 1, 3.0f );
    }
    interior.setMachineNeons( payout > 0 ? IntroCinematic::eNeonState::Win : IntroCinematic::eNeonState::Loose );
    setMessage( std::string( CasinoPoker::getResultName( result ) ) +
                ( payout > 0 ? " - " + std::to_string( payout ) + " chips" : "" ) );
    OTDU_LOG_INFO( "GMCasino: poker slot %s, stake %d -> %d chips, balance %d\n", CasinoPoker::getResultName( result ),
                   pokerStake, payout, chips );
}

bool GMCasino::startSlotSpin() {
    if ( slots.isSpinning() ) {
        return false;
    }
    if ( relaunch == eRelaunch::Holding ) {
        // The second draw: free; the cards / reels not held turn again.
        if ( slots.getMachine() == CasinoSlots::eMachine::Poker ) {
            for ( int32_t i = 0; i < 5; i++ ) {
                if ( !slotHeld[i] && pokerNext < 52 ) {
                    pokerHand[i] = pokerDeck[pokerNext++];
                }
            }
            slots.respin( slotHeld, false );
        } else {
            slots.respin( slotHeld, true );
        }
        relaunch = eRelaunch::Second;
        handleTime = 0.0f;
        return true;
    }
    if ( slots.getMachine() == CasinoSlots::eMachine::Poker ) {
        // "The price is ¤5 per draw ... removed from your stack as soon as you
        // pull the lever" [DATA: m_smpo_p1_2]; the reels show dealt cards.
        const int32_t stake = slots.getWager();
        if ( slots.getChips() < stake ) {
            if ( slotHud.isOpen() ) {
                slotHud.showError( 1 );
            }
            setMessage( "Not enough chips for this bet" );
            return false;
        }
        const int32_t beforeDeal = slots.getChips();
        slots.addChips( -stake );
        pokerStake = stake;
        dealPokerSlot();
        const bool none[5] = {};
        slots.respin( none, false );
        slotHud.screenSpinStarted( beforeDeal, stake );
        handleTime = 0.0f;
        interior.setMachineNeons( IntroCinematic::eNeonState::Active );
        return true;
    }
    if ( slots.getMachine() == CasinoSlots::eMachine::Hawai ) {
        slots.setPayNextSpin( false );
    }
    const int32_t before = slots.getChips();
    const int32_t wager = slots.getWager();
    if ( !slots.spin() ) {
        // The movie's error frame: 1 = not even one line / spin (m_slo_err_money),
        // 3 = too many activated lines for the wallet (m_slo_err_lines) [DATA].
        const bool bTooManyLines = slots.isPricePerLine() && slots.getActiveLines() > 1 &&
                                   before >= slots.getMachinePrice() * slots.getBetPerLine();
        if ( slotHud.isOpen() ) {
            slotHud.showError( bTooManyLines ? 3 : 1 );
        }
        setMessage( "Not enough chips for this bet" );
        return false;
    }
    slotHud.screenSpinStarted( before, wager );
    // The handle: the Entity__Actor__SlotMachineTrigger's Anim (the lever and
    // the buttons of the machine) plays from the spin order [DATA: the clip;
    // its start on the spin is INF].
    handleTime = 0.0f;
    interior.setMachineNeons( IntroCinematic::eNeonState::Active );
    return true;
}

void GMCasino::handleSlotHudEvents() {
    for ( const CasinoSlotHud::eEvent event : slotHud.takeEvents() ) {
        switch ( event ) {
        case CasinoSlotHud::eEvent::Spin:
            startSlotSpin();
            break;
        case CasinoSlotHud::eEvent::Leave:
            backToFloor();
            return;
        case CasinoSlotHud::eEvent::LinesChanged:
            slots.setActiveLines( slotHud.getActiveLines() );
            OTDU_LOG_INFO( "GMCasino: %d line(s) activated, %d chips a spin\n", slots.getActiveLines(),
                           slots.getWager() );
            break;
        case CasinoSlotHud::eEvent::BackToMenu:
            // B during the second chance: the hand / reels are kept as they
            // are and settled (the Beach respin with every reel held pays the
            // first draw) [INF].
            if ( relaunch == eRelaunch::Holding ) {
                for ( bool &held : slotHeld ) {
                    held = true;
                }
                startSlotSpin();
            }
            break;
        case CasinoSlotHud::eEvent::Left:
        case CasinoSlotHud::eEvent::Right:
            if ( relaunch == eRelaunch::Holding ) {
                // The hand moves along the reels / cards (the movie's Left /
                // Right of the second chance) [DATA: the help text].
                const int32_t count = Max( 1, slots.getReelCount() );
                holdCursor = ( holdCursor + ( event == CasinoSlotHud::eEvent::Left ? count - 1 : 1 ) ) % count;
                break;
            }
            // The movie's left / right move the hold hand of the second chance
            // machines; the port has no second chance, so they step the bet
            // level [INF].
            if ( !slots.isSpinning() ) {
                slots.stepBetPerLine( event == CasinoSlotHud::eEvent::Left ? -1 : 1 );
            }
            break;
        case CasinoSlotHud::eEvent::Lock:
            if ( relaunch == eRelaunch::Holding ) {
                // X_Lock: hold / unhold the card or reel under the hand.
                slotHeld[holdCursor] = !slotHeld[holdCursor];
                break;
            }
            if ( !slots.isSpinning() ) {
                slots.setBetPerLine( CasinoSlots::kMaxBetPerLine ); // [INF] as above
            }
            break;
        default:
            break;
        }
        if ( !slots.isSpinning() ) {
            slotHud.setScreenValues( slots.getChips(), slots.getWager() );
        }
    }
}

void GMCasino::updateReels( float deltaTime ) {
    // The real machine's reels (its Entity__Actor__SlotMachineSpinner models):
    // a turning reel scrolls its whole strip with the motion-blur texture
    // (blur_blend_factor 1); a stopped reel puts each of its three slots on
    // the atlas cell of the result symbol of that row (symbol i = cell i of
    // the machine's sheet in reading order: SlotMachineCar 0..5 = lamp,
    // wheel, stopwatch, flag, trophy, the R8) [DATA: the sheets; the symbol
    // to cell pairing is INF, the retail reel strips came from the server].
    if ( !interior.isCameraFocused() || slotAtlasColumns <= 0 || slotAtlasRows <= 0 ) {
        return;
    }
    const int32_t place = interior.getInteraction().Index;
    const float cellU = 1.0f / static_cast<float>( slotAtlasColumns );
    const float cellV = 1.0f / static_cast<float>( slotAtlasRows );
    const int32_t cells = slotAtlasColumns * slotAtlasRows;
    for ( int32_t reel = 0; reel < interior.getPlaceReels( place ); reel++ ) {
        float offsets[3][2] = {};
        float blur = 0.0f;
        if ( slots.isSpinning() && !slots.isReelStopped( reel ) && slotSpinnerType == 1 ) {
            // SpinnerType 1 (the Fashion machine's horizontal drums): the
            // strip runs sideways, along the sheet's rows [DATA: SpinnerType;
            // the owner's retail memory: only this machine spins across].
            // Row 0 of the sheet: the Fashion sheets hold their four outfits
            // as row 0 (three) + one cell of row 1, the rest of row 1 is empty
            // grey (row 2 repeats row 0) [DATA: sc_legs / sc_chest / sc_head];
            // the old "row = reel index" ran the chest drum (reel 1) along the
            // empty row - the grey stripes the owner saw.
            reelRoll[reel] = std::fmod( reelRoll[reel] + deltaTime * 4.0f * cellU * 3.0f, 1.0f );
            for ( int32_t k = 0; k < 3; k++ ) {
                offsets[k][0] = reelRoll[reel];
                offsets[k][1] = 0.0f;
            }
            blur = 1.0f;
        } else if ( slots.isSpinning() && !slots.isReelStopped( reel ) ) {
            // SpinnerType 0 (the drums) and 2 (the cards): the symbols run
            // downwards. Each face k (0 top .. 2 bottom) samples its own cell
            // of row 0, so all three are pinned to one sheet column and moved
            // down the column together: face k shows row k minus the roll,
            // which is the strip a turning drum shows [INF: the retail turns
            // the spinner's bone; RotationSpeed 1.3 is read as drum turns a
            // second, one turn = the column (rows symbols) x 3].
            // Only the columns that are full top to bottom: the 4 x 3 sheets
            // leave their last cell (row 2, column 3) empty, the card sheet its
            // last rows past card 52 (columns 0..3 reach row 6) [DATA: the
            // sheets, docs/images/casino_reel_symbol_sheets.webp].
            const int32_t fullColumns = slotAtlasColumns == 4 ? 3 : slotAtlasColumns == 8 ? 4 : slotAtlasColumns;
            const int32_t column = reel % Max( 1, fullColumns );
            // -reelspeedscale <f>: a test aid (a slow drum shows its direction).
            static const float speedScale =
                CmdLine::GetArgValue( "-reelspeedscale" ) != nullptr ? static_cast<float>( atof( CmdLine::GetArgValue( "-reelspeedscale" ) ) ) : 1.0f;
            const float rowsPerSecond = slotRotationSpeed * static_cast<float>( slotAtlasRows ) * 3.0f * speedScale;
            reelRoll[reel] = std::fmod( reelRoll[reel] + deltaTime * rowsPerSecond * cellV, 1.0f );
            for ( int32_t k = 0; k < 3; k++ ) {
                offsets[k][0] = cellU * static_cast<float>( column - k );
                offsets[k][1] = std::fmod( cellV * static_cast<float>( k ) - reelRoll[reel] + 2.0f, 1.0f );
            }
            blur = 1.0f;
        } else if ( slots.getMachine() == CasinoSlots::eMachine::Poker && reel < 5 ) {
            // The card machine: each window shows its card (all three faces on
            // that card's cell), the back before the first deal [DATA: cell 52].
            const int32_t cell = bPokerDealt ? PokerCell( pokerHand[reel] ) : 52;
            for ( int32_t k = 0; k < 3; k++ ) {
                offsets[k][0] = cellU * static_cast<float>( cell % slotAtlasColumns - k );
                offsets[k][1] = cellV * static_cast<float>( cell / slotAtlasColumns );
            }
        } else {
            for ( int32_t k = 0; k < 3; k++ ) {
                const int32_t cell = static_cast<int32_t>( slots.getResult( reel, k ) ) % cells;
                const int32_t column = cell % slotAtlasColumns;
                const int32_t row = cell / slotAtlasColumns;
                offsets[k][0] = cellU * static_cast<float>( column - k );
                offsets[k][1] = cellV * static_cast<float>( row );
            }
        }
        interior.setReelSlots( place, reel, offsets, cellU, blur );
    }
}

void GMCasino::tickRoulette( float deltaTime ) {
    const CasinoRoulette::ePhase phaseBefore = roulette.getPhase();
    roulette.tick( deltaTime );
    chips = roulette.getChips();
    // The wheel just stopped (RienNeVaPlus -> Winner): report the round.
    if ( phaseBefore == CasinoRoulette::ePhase::RienNeVaPlus &&
         roulette.getPhase() == CasinoRoulette::ePhase::Winner ) {
        reportRouletteRound();
        for ( const CasinoRoulette::Placed &bet : roulette.getWinningBets() ) {
            const int32_t filter = rouletteBetFilter( bet.Bet );
            if ( filter != 0 ) {
                ReportCasinoEvent( eCasinoEvent::BetWon, eCasinoGame::Roulette,
                                   filter, 1 );
            }
        }
    }
    if ( roulette.getPhase() != CasinoRoulette::ePhase::PlaceYourBet ) {
        return; // the table is running: no input until the next round
    }
    // Cursor: the numbers are 12 x 3 with the 0 to their left, the outside bets
    // are two rows of six below (their cells are two columns wide).
    const int32_t minCol = cursorRow >= 3 ? 0 : -1;
    const int32_t maxCol = cursorRow >= 3 ? 5 : CasinoRoulette::kGridCols - 1;
    if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        cursorCol = Max<int32_t>( minCol, cursorCol - 1 );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        cursorCol = Min<int32_t>( maxCol, cursorCol + 1 );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        cursorRow = Max<int32_t>( 0, cursorRow - 1 );
        cursorCol = Clamp<int32_t>(
            cursorCol, cursorRow >= 3 ? 0 : -1,
            cursorRow >= 3 ? 5 : CasinoRoulette::kGridCols - 1 );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        cursorRow =
            Min<int32_t>( CasinoRoulette::kGridRows - 1, cursorRow + 1 );
        cursorCol = Clamp<int32_t>(
            cursorCol, cursorRow >= 3 ? 0 : -1,
            cursorRow >= 3 ? 5 : CasinoRoulette::kGridCols - 1 );
    }

    int32_t cell = 0;
    const bool bHasCell =
        CasinoRoulette::cellFromCursor( cursorCol, cursorRow, cell );
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        if ( bHasCell && !roulette.placeBet( cell ) ) {
            setMessage( "Not enough chips for that chip" );
        }
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuOption1 ) ) {
        roulette.stepDenomination( 1 );
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuOption2 ) ) {
        if ( !roulette.spin() ) {
            setMessage( "Place a bet first" );
        }
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        if ( !roulette.getBets().empty() ) {
            roulette.clearBets();
        } else {
            backToFloor();
        }
    }
    // Rebet: R (keyboard). The retail Rebet button re-places the last bets; it
    // is also the "Use the Rebet button on roulette" objective.
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_R ) && roulette.rebet() ) {
        ReportCasinoEvent( eCasinoEvent::UiUsed, eCasinoGame::Roulette, 0, 1 );
        setMessage( "Rebet" );
    }
}

void GMCasino::tickPoker( float deltaTime ) {
    const CasinoPoker::ePhase phaseBefore = poker.getPhase();
    poker.tick( deltaTime );
    chips = poker.getChips();
    // The Result phase reports once (it also auto-returns to Idle).
    if ( phaseBefore == CasinoPoker::ePhase::Result &&
         poker.getPhase() == CasinoPoker::ePhase::Idle ) {
        return;
    }
    switch ( poker.getPhase() ) {
    case CasinoPoker::ePhase::Idle:
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) &&
             !poker.deal() ) {
            setMessage( "Not enough chips for this bet" );
        }
        break;
    case CasinoPoker::ePhase::Holding:
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            pokerCursor =
                ( pokerCursor + CasinoPoker::kHand - 1 ) % CasinoPoker::kHand;
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            pokerCursor = ( pokerCursor + 1 ) % CasinoPoker::kHand;
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            poker.toggleHold( pokerCursor );
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuOption2 ) ||
                    gpInput->wasPressed( eInputAction::IA_MenuOption1 ) ) {
            // X / Y draws. The hand is reported once, here.
            poker.draw();
            reportPokerHand();
            if ( poker.getLastPayout() > 0 ) {
                char text[96] = {};
                snprintf( text, sizeof( text ), "%s - %d chips",
                          CasinoPoker::getResultName( poker.getResult() ),
                          poker.getLastPayout() );
                setMessage( text );
            }
        }
        break;
    case CasinoPoker::ePhase::Result:
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            poker.acknowledge();
        }
        break;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) &&
         poker.getPhase() != CasinoPoker::ePhase::Holding ) {
        poker.acknowledge();
        backToFloor();
    }
}

void GMCasino::drawPoker( Renderer2D &renderer, const Font *pFont ) {
    drawHeader( renderer, pFont, "POKER",
                "video poker - Enter: hold, X / Y: draw, Left / Right: card, "
                "B: floor" );
    float width = 0.0f, height = 0.0f;
    CasinoPoker::getTableSize( kPokerScale, width, height );
    const float x = 640.0f - width * 0.5f;
    const float y = 260.0f;
    poker.draw( renderer, pFont, x, y, kPokerScale );
    if ( pFont == nullptr ) {
        return;
    }
    // The cursor is drawn on top of the cards.
    const float cardPitch = ( 92.0f + 12.0f ) * kPokerScale;
    if ( poker.getPhase() == CasinoPoker::ePhase::Holding ) {
        renderer.drawRectOutline( Rect2D( x + pokerCursor * cardPitch - 6.0f,
                                          y - 6.0f, 92.0f * kPokerScale + 12.0f,
                                          height + 12.0f ),
                                  3.0f, MakeRGBA( 120, 236, 140 ) );
    }
    char line[192] = {};
    snprintf( line, sizeof( line ), "Bet %d   Balance %d   Hands %d   Won %d",
              poker.getBet(), poker.getChips(), poker.getHands(),
              poker.getTotalWon() );
    renderer.drawText( pFont, line, 640.0f, y + height + 44.0f, 20.0f,
                       MakeRGBA( 232, 232, 238 ), eTextAlign::TA_Center );
    const char *pPhase = poker.getPhase() == CasinoPoker::ePhase::Idle
                             ? "Enter / A: deal"
                         : poker.getPhase() == CasinoPoker::ePhase::Holding
                             ? "Enter / A: hold   X / Y: draw"
                             : "Enter / A: next hand";
    renderer.drawText( pFont, pPhase, 640.0f, y - 44.0f, 18.0f,
                       MakeRGBA( 236, 208, 120 ), eTextAlign::TA_Center );
    if ( poker.getPhase() == CasinoPoker::ePhase::Result &&
         poker.getLastPayout() > 0 ) {
        char win[128] = {};
        snprintf( win, sizeof( win ), "%s  +%d chips",
                  CasinoPoker::getResultName( poker.getResult() ),
                  poker.getLastPayout() );
        renderer.drawText( pFont, win, 640.0f, y - 72.0f, 26.0f,
                           MakeRGBA( 120, 236, 140 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont,
                       "Enter / A: hold    X / Y: draw    Left / Right: card   "
                       " Backspace / B: floor",
                       640.0f, kFooterY, 15.0f, MakeRGBA( 170, 176, 196 ),
                       eTextAlign::TA_Center );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), 640.0f, kFooterY - 24.0f,
                           16.0f, MakeRGBA( 236, 208, 120 ),
                           eTextAlign::TA_Center );
    }
}

void GMCasino::drawOffscreen() {
    // The machines' screens first (render targets sampled by the room).
    slotHud.drawOffscreen();
    if ( slotHud.usesScreenTargets() ) {
        interior.setScreenTextures( slotHud.getIdleScreenTexture(), slotHud.getPlayedScreenTexture(),
                                    slotHud.getLinesTexture(), slotHud.getTextureStamp() );
    }
    if ( interior.isActive() ) {
        interior.drawOffscreen();
    }
}

void GMCasino::draw() {
    Renderer2D &renderer = gpRender->get2DRenderer();
    const Font *pFont = gpFlash != nullptr ? gpFlash->getTitleFont() : nullptr;

    // The retail interior (the hall / a game room) behind the native screens.
    const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( interior.isActive() && !bNo3D ) {
        interior.draw();
        // The walk HUD movie (INTRO.vmf) leaves the 2D layer's clip / stencil
        // state set; the native draws after it are clipped away otherwise
        // (docs/re/casino_spec.md 5, the same trap as Casino.vmf).
        renderer.clearClipRect();
        renderer.setStencil( eStencilMode::SM_None, 0 );
        // The native screens are authored on the 1280 x 720 canvas; the movie
        // may have left another canvas size behind (INTRO.vmf is 4:3).
        renderer.begin( 1280.0f, 720.0f );
    }

    // A room cutscene (the jackpots, the car win) owns the screen: no shell,
    // no game HUD, no walk hints over it.
    const bool bCutscene = interior.isActive() && interior.isCinematicPlaying();
    // The retail shell (Casino.vmf) behind the native screens. It is the movie
    // the game drives; the games themselves are native (see the header).
    if ( bDrawShell && bShellLoaded && !bCutscene ) {
        casinoShell.draw( renderer );
    }
    if ( bCutscene ) {
        // nothing over the cutscene
    } else switch ( screen ) {
    case eScreen::Floor:
        if ( interior.isActive() ) {
            drawInteriorHud( renderer, pFont );
        } else {
            drawFloor( renderer, pFont );
        }
        break;
    case eScreen::Slots:
        drawSlots( renderer, pFont );
        break;
    case eScreen::Roulette:
        drawRoulette( renderer, pFont );
        break;
    case eScreen::Poker:
        drawPoker( renderer, pFont );
        break;
    case eScreen::Cashier:
        if ( deskHud.isCashierLoaded() ) {
            deskHud.draw( renderer, pFont );
        } else {
            drawCashier( renderer, pFont );
        }
        break;
    case eScreen::Bar:
        if ( deskHud.isBarLoaded() ) {
            deskHud.draw( renderer, pFont );
        } else {
            drawBar( renderer, pFont );
        }
        break;
    }
    if ( bShopOpen ) {
        clothesMenu.draw( renderer );
    }
    if ( bWardrobeOpen ) {
        drawWardrobe( renderer, pFont );
    }
    if ( objectives.isOpen() ) {
        objectives.draw( renderer, pFont );
    }
    if ( pauseMenu.isOpen() ) {
        pauseMenu.draw( renderer );
    }
}

void GMCasino::drawHeader( Renderer2D &renderer,
                           const Font *pFont,
                           const char *pTitle,
                           const char *pHint ) {
    if ( pFont == nullptr ) {
        return;
    }
    renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 78.0f ),
                       MakeRGBA( 10, 12, 22, 220 ) );
    renderer.drawText( pFont, pTitle, 28.0f, kHeaderY, 30.0f,
                       MakeRGBA( 236, 208, 120 ) );
    renderer.drawText( pFont, pHint, 30.0f, kHeaderY + 26.0f, 15.0f,
                       MakeRGBA( 170, 176, 196 ) );
    const std::string text = std::to_string( chips ) + " chips";
    renderer.drawText( pFont, text.c_str(), 1252.0f, kHeaderY, 24.0f,
                       MakeRGBA( 236, 208, 120 ), eTextAlign::TA_Right );
    // The progression line (level / status / objectives), from the DBCasino*
    // tables and the profile.
    if ( !progressLine.empty() ) {
        renderer.drawText( pFont, progressLine.c_str(), 1252.0f,
                           kHeaderY + 26.0f, 15.0f, MakeRGBA( 170, 176, 196 ),
                           eTextAlign::TA_Right );
    }
}

void GMCasino::drawFloor( Renderer2D &renderer, const Font *pFont ) {
    drawHeader( renderer, pFont, "CASINO",
                "Offline casino - no session, no rankings" );
    if ( pFont == nullptr ) {
        return;
    }
    renderer.drawRect( Rect2D( 300.0f, 150.0f, 680.0f, 420.0f ),
                       MakeRGBA( 14, 16, 30, 230 ) );
    for ( int32_t i = 0; i < kFloorCount; i++ ) {
        const bool bLocked = i == kFloorSlots && isSlotMachineLocked();
        const float y = 182.0f + i * 62.0f;
        if ( i == floorSelection ) {
            renderer.drawRect( Rect2D( 316.0f, y - 16.0f, 648.0f, 56.0f ),
                               MakeRGBA( 46, 40, 22, 240 ) );
            renderer.drawRectOutline(
                Rect2D( 316.0f, y - 16.0f, 648.0f, 56.0f ), 2.0f,
                MakeRGBA( 236, 208, 120 ) );
        }
        const uint32_t color =
            bLocked ? MakeRGBA( 120, 120, 130 ) : MakeRGBA( 232, 232, 238 );
        renderer.drawText( pFont, kFloor[i].pName, 340.0f, y, 24.0f, color );
        renderer.drawText( pFont, bLocked ? "locked" : kFloor[i].pHint, 700.0f,
                           y + 4.0f, 15.0f, MakeRGBA( 160, 164, 180 ) );
    }
    renderer.drawText(
        pFont, "Up / Down: select    Enter / A: choose    Backspace / B: leave",
        340.0f, 596.0f, 15.0f, MakeRGBA( 170, 176, 196 ) );
    renderer.drawText(
        pFont, "Esc: pause - Game Progress (the objectives) / Options / Exit",
        340.0f, 620.0f, 15.0f, MakeRGBA( 170, 176, 196 ) );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), 640.0f, kFooterY, 18.0f,
                           MakeRGBA( 236, 208, 120 ), eTextAlign::TA_Center );
    }
}

void GMCasino::drawInteriorHud( Renderer2D &renderer, const Font *pFont ) {
    // The 3D room is the screen: only the header line and the prompt of the
    // selectable under the reticle are native (the walk HUD of INTRO.vmf draws
    // the reticle and its label itself).
    const std::string type = interior.getSelectedType();
    std::string hint = "Walk: WASD    Look: mouse    Enter / A: use    Backspace / B: hall";
    if ( type == "cslo" ) {
        hint = "Enter / A: the slot machines";
    } else if ( type == "crou" ) {
        hint = "Enter / A: the roulette tables";
    } else if ( type == "cpok" ) {
        hint = "Enter / A: the poker room";
    } else if ( type == "sitgo" ) {
        hint = "Enter / A: the Sit & Go tables";
    } else if ( type == "cvip" ) {
        hint = "Enter / A: the VIP lounge";
    } else if ( type == "shop" ) {
        hint = "Enter / A: the boutique";
    } else if ( type == "avat" ) {
        hint = "Enter / A: the dressing room";
    } else if ( type == "outdoor" ) {
        hint = "Enter / A: leave the casino";
    } else if ( type == "chal" ) {
        hint = "Enter / A: back to the hall";
    } else if ( type == "cash" || type == "cashier" ) {
        hint = "Enter / A: the cashier (currency converter)";
    } else if ( type == "barman" || type == "bar" ) {
        hint = "Enter / A: order a cocktail";
    } else if ( type.rfind( "sm_", 0 ) == 0 ) {
        hint = "Enter / A: play this slot machine";
    } else if ( type == "roulette" ) {
        hint = "Enter / A: sit at this table";
    } else if ( type == "poker" ) {
        hint = "Enter / A: sit at this poker table";
    }
    char header[128] = {};
    snprintf( header, sizeof( header ), "CASINO - %s", CasinoInterior::RoomName( interior.getRoom() ) );
    drawHeader( renderer, pFont, header, "Offline casino - no session, no rankings" );
    if ( pFont == nullptr ) {
        return;
    }
    renderer.drawText( pFont, progressLine.c_str(), 640.0f, 92.0f, 15.0f, MakeRGBA( 200, 200, 214 ),
                       eTextAlign::TA_Center );
    renderer.drawText( pFont, hint.c_str(), 640.0f, kFooterY, 16.0f, MakeRGBA( 236, 208, 120 ),
                       eTextAlign::TA_Center );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), 640.0f, kFooterY - 26.0f, 18.0f, MakeRGBA( 236, 208, 120 ),
                           eTextAlign::TA_Center );
    }
}

void GMCasino::drawCashier( Renderer2D &renderer, const Font *pFont ) {
    drawHeader( renderer, pFont, "CASHIER", "chips are bought and sold here, offline" );
    if ( pFont == nullptr ) {
        return;
    }
    const PlayerProfile &profile = GetPlayerProfile();
    renderer.drawRect( Rect2D( 340.0f, 150.0f, 600.0f, 360.0f ), MakeRGBA( 14, 16, 30, 230 ) );
    static const char *pEntries[6] = {
        "Buy 1 000 chips      $1 000",
        "Buy 10 000 chips     $10 000",
        "Buy 100 000 chips    $100 000",
        "Sell 1 000 chips     +$1 000",
        "Sell every chip",
        "Back to the hall",
    };
    for ( int32_t i = 0; i < 6; i++ ) {
        const float y = 182.0f + i * 54.0f;
        if ( i == cashierSelection ) {
            renderer.drawRect( Rect2D( 356.0f, y - 14.0f, 568.0f, 48.0f ), MakeRGBA( 46, 40, 22, 240 ) );
            renderer.drawRectOutline( Rect2D( 356.0f, y - 14.0f, 568.0f, 48.0f ), 2.0f,
                                      MakeRGBA( 236, 208, 120 ) );
        }
        renderer.drawText( pFont, pEntries[i], 380.0f, y, 22.0f, MakeRGBA( 232, 232, 238 ) );
    }
    char line[160] = {};
    snprintf( line, sizeof( line ), "Money $%d    Chips %d", profile.Money, chips );
    renderer.drawText( pFont, line, 640.0f, 540.0f, 22.0f, MakeRGBA( 120, 236, 140 ), eTextAlign::TA_Center );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), 640.0f, kFooterY, 18.0f, MakeRGBA( 236, 208, 120 ),
                           eTextAlign::TA_Center );
    }
}

void GMCasino::drawBar( Renderer2D &renderer, const Font *pFont ) {
    drawHeader( renderer, pFont, "BAR", "the bar man serves the casino cocktails" );
    if ( pFont == nullptr ) {
        return;
    }
    const PlayerProfile &profile = GetPlayerProfile();
    const std::vector<CasinoUnlockable> &unlockables = GetCasinoUnlockables();
    renderer.drawRect( Rect2D( 340.0f, 150.0f, 600.0f, 400.0f ), MakeRGBA( 14, 16, 30, 230 ) );
    const int32_t visible = 9;
    const int32_t first = Max( 0, barSelection - visible / 2 );
    for ( int32_t i = 0; i < visible && first + i < static_cast<int32_t>( unlockables.size() ); i++ ) {
        const CasinoUnlockable &drink = unlockables[first + i];
        const float y = 182.0f + i * 40.0f;
        if ( first + i == barSelection ) {
            renderer.drawRect( Rect2D( 356.0f, y - 12.0f, 568.0f, 36.0f ), MakeRGBA( 46, 40, 22, 240 ) );
        }
        const bool bOwned = std::find( profile.CasinoUnlockables.begin(), profile.CasinoUnlockables.end(),
                                      drink.Hashcode ) != profile.CasinoUnlockables.end();
        const uint32_t color = bOwned ? MakeRGBA( 140, 200, 140 ) : MakeRGBA( 232, 232, 238 );
        renderer.drawText( pFont, drink.Description.c_str(), 380.0f, y, 20.0f, color );
        renderer.drawText( pFont, bOwned ? "tasted" : "$25", 890.0f, y + 3.0f, 15.0f, MakeRGBA( 170, 176, 196 ),
                           eTextAlign::TA_Right );
    }
    char line[128] = {};
    snprintf( line, sizeof( line ), "Money $%d", profile.Money );
    renderer.drawText( pFont, line, 640.0f, 580.0f, 20.0f, MakeRGBA( 120, 236, 140 ), eTextAlign::TA_Center );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), 640.0f, kFooterY, 18.0f, MakeRGBA( 236, 208, 120 ),
                           eTextAlign::TA_Center );
    }
}

void GMCasino::drawWardrobe( Renderer2D &renderer, const Font *pFont ) {
    drawHeader( renderer, pFont, "DRESSING ROOM", "your clothes - B: back to the room" );
    if ( pFont == nullptr ) {
        return;
    }
    const PlayerProfile &profile = GetPlayerProfile();
    renderer.drawRect( Rect2D( 320.0f, 140.0f, 640.0f, 460.0f ), MakeRGBA( 14, 16, 30, 235 ) );
    if ( profile.OwnedClothes.empty() ) {
        renderer.drawText( pFont, "You own no clothes yet - the boutique sells the casino outfits.", 640.0f, 340.0f,
                           18.0f, MakeRGBA( 200, 200, 214 ), eTextAlign::TA_Center );
        return;
    }
    const int32_t count = static_cast<int32_t>( profile.OwnedClothes.size() );
    const int32_t visible = 12;
    const int32_t first = Max( 0, wardrobeSelection - visible / 2 );
    for ( int32_t i = 0; i < visible && first + i < count; i++ ) {
        const ClotheItem *pItem = FindClothe( profile.OwnedClothes[first + i] );
        const float y = 170.0f + i * 34.0f;
        if ( first + i == wardrobeSelection ) {
            renderer.drawRect( Rect2D( 336.0f, y - 10.0f, 608.0f, 30.0f ), MakeRGBA( 46, 40, 22, 240 ) );
        }
        const std::string name = pItem != nullptr ? pItem->Name : std::to_string( profile.OwnedClothes[first + i] );
        const std::string brand = pItem != nullptr ? pItem->Brand : std::string();
        renderer.drawText( pFont, name.c_str(), 360.0f, y, 18.0f, MakeRGBA( 232, 232, 238 ) );
        if ( !brand.empty() ) {
            renderer.drawText( pFont, brand.c_str(), 920.0f, y + 2.0f, 14.0f, MakeRGBA( 170, 176, 196 ),
                               eTextAlign::TA_Right );
        }
    }
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), 640.0f, kFooterY, 18.0f, MakeRGBA( 236, 208, 120 ),
                           eTextAlign::TA_Center );
    }
}

void GMCasino::drawSlots( Renderer2D &renderer, const Font *pFont ) {
    if ( slotHud.isOpen() ) {
        // The retail HUD: the native grid only away from a real machine.
        if ( !interior.isCameraFocused() || CmdLine::HasArg( "-slotgrid" ) ) {
            float width = 0.0f, height = 0.0f;
            CasinoSlots::getWindowSize( kSlotsScale, width, height );
            slots.draw( renderer, pFont, 640.0f - width * 0.5f, 250.0f, kSlotsScale );
        }
        if ( relaunch != eRelaunch::None ) {
            drawHoldRow( renderer, pFont );
        }
        slotHud.draw( renderer, pFont );
        return;
    }
    // The machine's retail name (the DBCasinoObjectiveDetail texts: "the TDU 2
    // / Classic / Fashion / Poker / High-Life / Beach slot machine") [DATA].
    static const char *const kRetailNames[] = { "TDU 2", "Fashion", "Poker", "Beach", "Classic", "High-Life" };
    const int32_t machineIndex = static_cast<int32_t>( slots.getMachine() );
    char title[64] = {};
    char subtitle[128] = {};
    snprintf( title, sizeof( title ), "SLOT MACHINE - %s",
              machineIndex >= 0 && machineIndex < 6 ? kRetailNames[machineIndex] : "" );
    snprintf( subtitle, sizeof( subtitle ), "%d reels, %d line%s - Enter: spin, Left / Right: bet, Y: max bet",
              slots.getReelCount(), slots.getActiveLines(), slots.getActiveLines() > 1 ? "s" : "" );
    drawHeader( renderer, pFont, title, subtitle );
    float width = 0.0f, height = 0.0f;
    CasinoSlots::getWindowSize( kSlotsScale, width, height );
    const float x = 640.0f - width * 0.5f;
    const float y = 250.0f;
    // At a real machine the reels are its own 3D spinners (CameraGame): the
    // native grid only with -slotgrid (or when the room has no machine).
    const bool bRealMachine = interior.isCameraFocused() && !CmdLine::HasArg( "-slotgrid" );
    if ( !bRealMachine ) {
        slots.draw( renderer, pFont, x, y, kSlotsScale );
    }
    if ( pFont == nullptr ) {
        return;
    }
    char line[160] = {};
    snprintf( line, sizeof( line ), "%s  bet x%d  wager %d  balance %d",
              CasinoSlots::getMachineName( slots.getMachine() ), slots.getBetPerLine(), slots.getWager(),
              slots.getChips() );
    // At the real machine the status sits under the reels, out of the glass.
    const float lineY = bRealMachine ? kFooterY - 56.0f : y + height + 34.0f;
    const float winY = bRealMachine ? kFooterY - 96.0f : y - 44.0f;
    renderer.drawText( pFont, line, 640.0f, lineY, 20.0f,
                       MakeRGBA( 232, 232, 238 ), eTextAlign::TA_Center );
    if ( !slots.isSpinning() && slots.getLastPayout() > 0 ) {
        char win[128] = {};
        snprintf( win, sizeof( win ), "WIN %d chips", slots.getLastPayout() );
        renderer.drawText( pFont, win, 640.0f, winY, 28.0f,
                           MakeRGBA( 120, 236, 140 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont,
                       "Enter / A: spin    Left / Right: bet    Y / Up: max "
                       "bet    Backspace / B: floor",
                       640.0f, kFooterY, 15.0f, MakeRGBA( 170, 176, 196 ),
                       eTextAlign::TA_Center );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), 640.0f, kFooterY - 24.0f,
                           16.0f, MakeRGBA( 236, 208, 120 ),
                           eTextAlign::TA_Center );
    }
}

void GMCasino::drawHoldRow( Renderer2D &renderer, const Font *pFont ) {
    // The second chance hand: one box per card / reel, HOLD when held, the
    // hand on the selected one. The retail draws a hand cursor over the
    // machine (the movie's cursorDisplayed / cursorBlinking); this row is
    // the port's stand-in [INF].
    if ( pFont == nullptr ) {
        return;
    }
    static const char *const kRanks[] = { "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A" };
    static const char *const kSuits[] = { "spades", "hearts", "diamonds", "clubs" };
    const int32_t count = Max( 1, slots.getReelCount() );
    const float boxWidth = 132.0f, gap = 12.0f;
    const float x0 = 640.0f - ( count * boxWidth + ( count - 1 ) * gap ) * 0.5f;
    const float y = 520.0f;
    renderer.clearClipRect();
    renderer.setStencil( eStencilMode::SM_None, 0 );
    renderer.begin( 1280.0f, 720.0f );
    for ( int32_t i = 0; i < count; i++ ) {
        const Rect2D box( x0 + i * ( boxWidth + gap ), y, boxWidth, 58.0f );
        const bool bCursor = relaunch == eRelaunch::Holding && i == holdCursor;
        renderer.drawRect( box, slotHeld[i] ? MakeRGBA( 30, 70, 130, 225 ) : MakeRGBA( 10, 14, 28, 200 ) );
        renderer.drawRectOutline( box, bCursor ? 3.0f : 1.0f,
                                  bCursor ? MakeRGBA( 255, 210, 90 ) : MakeRGBA( 90, 130, 191 ) );
        std::string label;
        if ( slots.getMachine() == CasinoSlots::eMachine::Poker && bPokerDealt ) {
            label = std::string( kRanks[pokerHand[i].Rank % 13] ) + " " + kSuits[pokerHand[i].Suit & 3];
        } else {
            label = "Reel " + std::to_string( i + 1 );
        }
        renderer.drawText( pFont, label.c_str(), box.X + boxWidth * 0.5f, y + 8.0f, 16.0f, MakeRGBA( 230, 230, 236 ),
                           eTextAlign::TA_Center );
        renderer.drawText( pFont, slotHeld[i] ? "HOLD" : "-", box.X + boxWidth * 0.5f, y + 32.0f, 16.0f,
                           slotHeld[i] ? MakeRGBA( 255, 210, 90 ) : MakeRGBA( 140, 146, 166 ), eTextAlign::TA_Center );
    }
    if ( relaunch == eRelaunch::Holding ) {
    }
    // Flush: the movie's stage (begin) after this drops unflushed vertices.
    renderer.end();
}

void GMCasino::drawRoulette( Renderer2D &renderer, const Font *pFont ) {
    drawHeader( renderer, pFont, "ROULETTE",
                "single zero - Enter: place chip, Y: chip size, X: spin, B: "
                "clear / floor" );
    float width = 0.0f, height = 0.0f;
    CasinoRoulette::getTableSize( kRouletteScale, width, height );
    const float x = 640.0f - width * 0.5f + 44.0f * kRouletteScale;
    const float y = 270.0f;
    roulette.draw( renderer, pFont, x, y, kRouletteScale, cursorCol,
                   cursorRow );
    if ( pFont == nullptr ) {
        return;
    }
    char line[160] = {};
    snprintf( line, sizeof( line ),
              "Chip %d   Staked %d   Balance %d   Spins %d",
              roulette.getDenomination(), roulette.getStaked(),
              roulette.getChips(), roulette.getSpins() );
    renderer.drawText( pFont, line, 640.0f, y + height + 40.0f, 20.0f,
                       MakeRGBA( 232, 232, 238 ), eTextAlign::TA_Center );
    if ( roulette.getPhase() == CasinoRoulette::ePhase::Winner &&
         roulette.getLastPayout() > 0 ) {
        char win[128] = {};
        snprintf( win, sizeof( win ), "PAYOUT %d chips",
                  roulette.getLastPayout() );
        renderer.drawText( pFont, win, 640.0f, y - 52.0f, 26.0f,
                           MakeRGBA( 120, 236, 140 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont,
                       "Arrows: move    Enter / A: place    Y: chip size    X: "
                       "spin    Backspace / B: clear or floor",
                       640.0f, kFooterY, 15.0f, MakeRGBA( 170, 176, 196 ),
                       eTextAlign::TA_Center );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), 640.0f, kFooterY - 24.0f,
                           16.0f, MakeRGBA( 236, 208, 120 ),
                           eTextAlign::TA_Center );
    }
}

void GMCasino::drawDebug() {}

void GMCasino::drawUI() {
    // The emote picker while the hall / a game room is walked (docs/re/emotes_spec.md).
    if ( interior.isWalking() ) {
        interior.getWalk().drawEmotePickerUi( interior.getScene() );
    }
    // The cheats panel (F4, or the pad's option button): the wallet, so a table
    // can be tested with chips.
    // (Not the option button at a slot machine: X / Square opens its lines
    // manager there.)
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F4 ) ||
         ( gpInput->wasPressed( eInputAction::IA_MenuOption1 ) && !slotHud.isOpen() ) ) {
        cheats.toggle();
    }
    cheats.drawUI( "CASINO" );
}
