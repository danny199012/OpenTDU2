#include "shared.h"
#include <functional>
#include <set>
#include "map_screen.h"
#include "game/pause_menu.h"

#include "database/gs_database.h"
#include "flash/flash_localization.h"
#include "flash/gs_flash.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "game/car_shop.h"
#include "game/freeride/chase_camera.h"
#include "game/freeride/cruising_zones.h"
#include "game/game_options.h"
#include "game/house_shop.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "render/image_file.h"
#include "render/renderer_2d.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <map>
#include <cstdlib>
#include <queue>

namespace {
constexpr float kMapWidth = 1280.0f; // the flat map is laid out on a 16:9 virtual canvas
constexpr float kMapHeight = 720.0f;
constexpr float kStageWidth = 1280.0f; // MAP.vmf's stage (the 16:9 band is y 120..840)
constexpr float kStageHeight = 960.0f;
// Stage px <-> the 1280x720 map canvas, through the UI layout mapping (the
// one the movie draws with, FlashStageRect). Mode 1 in Stretch this is the
// retail band math: canvas y = stage y - 120. Exact at every window aspect,
// layout and screen-ratio override.
float StageToCanvasY( float stageY ) {
    float canvasX = 0.0f, canvasY = 0.0f;
    FlashStageToCanvas( 0.0f, stageY, kStageWidth, kStageHeight, canvasX, canvasY );
    return canvasY;
}
float CanvasToStageY( float canvasY ) {
    float stageX = 0.0f, stageY = 0.0f;
    FlashCanvasToStage( 0.0f, canvasY, kStageWidth, kStageHeight, stageX, stageY );
    return stageY;
}
// MAP.vmf's own labels, dumped with -logmap (the movie's 26 frame labels):
//   map_open -> map_stop   the open animation and its resting interactive state
//   map_back               the back transition
//   close / close_test / close__ / close_free   the closing variants, one per
//                          context — close_free is the free ride one
//   map_clic, map_tutorial, contest*, credits, results_in, init, default
// The candidate list keeps a fallback so a re-authored bank still opens.
constexpr const char *kOpenLabels[] = { "map_open", "open", "map_in" };
constexpr const char *kCloseLabels[] = { "close_free", "close", "map_back", "map_out" };
// [INF] the movie's out animation length is not known before it is read; the
// screen is released after this even if the movie loops instead of stopping.
constexpr float kCloseDuration = 1.0f; // the camera flies back to the car meanwhile
constexpr float kTransitionTime = 1.0f;
// Cursor snapping (canvas px of the 1280 x 720 map): the reticle sticks to a
// shop icon within 26 px and to a road within 18 px; a target it is already on
// keeps it 8 px further out, so the cursor does not flicker off it.
constexpr float kSnapSpotPixels = 26.0f;
constexpr float kSnapRoadPixels = 18.0f;
constexpr float kSnapStickyPixels = 8.0f;

bool PlayFirstLabel( FlashPlayer &player, const char *const *pLabels, size_t count, const char *pWhat ) {
    for ( size_t i = 0; i < count; i++ ) {
        if ( player.gotoLabel( "/", pLabels[i], true ) ) {
            OTDU_LOG_INFO( "MapScreen: %s label '%s'\n", pWhat, pLabels[i] );
            return true;
        }
    }
    OTDU_LOG_WARN( "MapScreen: no %s label found (see -logmap for the movie's labels)\n", pWhat );
    return false;
}

// The DBBrands _logo_global_index of a brand hashcode (DBCarPhysicsData
// _car_brand), -1 when the database or the row is missing.
int32_t brandLogoIndex( uint64_t brandHash ) {
    if ( gpDatabase == nullptr || brandHash == 0 ) {
        return -1;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    const uint32_t numBrands = root.getArraySize( "DBBrands" );
    for ( uint32_t i = 0; i < numBrands; i++ ) {
        XmbValue brand = root.getArrayElement( "DBBrands", i );
        if ( brand["_hashcode"].asUInt64() == brandHash ) {
            return brand["_logo_global_index"].asInt();
        }
    }
    return -1;
}

// The map pin's brand logo of a car dealer (the spots.bin category 0x01
// records): one brand logo cell of the brands_map atlas per dealer, which is
// what the retail map shows [DATA: the retail grabs of the "Spots for
// Vehicles" tab, both islands - see docs/re/map_spots_spec.md]. The retail
// pick per dealer is hand-authored (not derivable from the stock: "British
// Cars" sells six Aston Martins and shows Lotus), so the grabbed logos are
// the law, keyed by the building-bank prefix; a dealer the grabs do not
// settle falls back to the most-represented brand of its DBCarShop stock
// (ties: the priciest car's brand), else the "Generic Car" cell (62).
// Brand hashcodes are the DBBrands _hashcode values (the cars' _car_brand).
int32_t dealerLogoIndex( const std::string &name, uint64_t spotId ) {
    static const struct {
        const char *pPrefix;
        uint64_t brandHash;
    } kRetailPick[] = {
        // hashcodes: AUDI FERRARI MERCO ASCA LOTUS SPYK PAGANI SUBARU CHEVY DODGE HUMMER
        { "ECD_AUDI", 0x5 },  { "ECD_FERR", 0xf },  { "ECD_MERC", 0x1b }, { "ECD_BRI1", 0x17 },
        { "ECD_BRI2", 0x3 },  { "ECD_EUR2", 0x27 }, { "ECD_ITAL", 0x1f }, { "ECD_ASIA", 0x44 },
        { "ECD_US1", 0x8 },   { "ECD_US2", 0xb },   { "ECDC_WLD", 0x3f },
    };
    auto startsWith = [&]( const char *pPrefix ) { return name.compare( 0, strlen( pPrefix ), pPrefix ) == 0; };
    for ( const auto &pick : kRetailPick ) {
        if ( startsWith( pick.pPrefix ) ) {
            return brandLogoIndex( pick.brandHash );
        }
    }
    // The stock law: the shop of this spot (DBCarShop._spot_name), its cars'
    // brands counted; a tie goes to the brand of the most expensive car.
    const CarShop *pShop = FindShopBySpotId( spotId );
    if ( pShop != nullptr && !pShop->Cars.empty() ) {
        std::map<uint64_t, int32_t> counts;
        int32_t bestCount = 0;
        uint64_t bestBrand = 0;
        int32_t bestPrice = -1;
        for ( const ShopCar &car : pShop->Cars ) {
            const int32_t count = ++counts[car.BrandHash];
            if ( count > bestCount || ( count == bestCount && car.Price > bestPrice ) ) {
                bestCount = count;
                bestBrand = car.BrandHash;
                bestPrice = car.Price;
            }
        }
        const int32_t index = brandLogoIndex( bestBrand );
        if ( index >= 0 ) {
            return index;
        }
    }
    // The grouped dealers without a settled retail pick and without stock:
    // the movie's "Generic Car" cell [INF: the port's pre-retail-pin choice].
    return 62;
}
} // namespace

bool MapScreen::load() {
    unload();
    FlashMovie *pMovie = gpFlash != nullptr ? gpFlash->getMovie( "map" ) : nullptr;
    if ( pMovie != nullptr ) {
        // The spot tiles' brand logos are bitmaps on the brands_map page of the
        // logo bank [DATA]; it has to be known before the player builds its fills.
        pMovie->addExtraPages( "FrontEnd/AllRes/LogoTexturePage.bnk" );
    }
    if ( pMovie == nullptr || !player.load( pMovie ) ) {
        OTDU_LOG_WARN( "MapScreen: MAP.vmf (FrontEnd/HiRes/map.bnk) not available\n" );
        return false;
    }
    // FUN_00d8e200 (GENERAL, MAIN): /:WhatPlatform = 1 (PC). The map's own
    // button glyphs pick their "pc" frame from it, like the pause menu.
    player.setVariableNumber( "/:WhatPlatform", 1.0 );
    // The screen is only reachable from free ride here (the retail HUD's MAP
    // action), so the mode is the pause menu's: 2 = freeride / explore.
    player.setVariableNumber( "/:WhatGameMode", 2.0 );
    // The gate the movie reads before it will show itself.
    player.setVariableBool( "/:IsMapAvailable", true );
    const int32_t tabArg = CmdLine::GetArgInt( "-maptab", 0 );
    player.setVariableNumber( "/:MapTabObjective", tabArg );
    // The retail return trip: the movie writes these back, the game reads them
    // instead of polling the pad.
    player.setVariableBool( "/:BackFromMap", false );
    player.setVariableBool( "/:IsBackFromMap", false );
    player.setVariableBool( "/:IsMapQuit", false );
    player.setVariableBool( "/:MapCancel", false );
    // The player panel: the marker follows the car the player is in and the
    // panel's vehicle field is a localized string. The car's localized name is
    // not plumbed through here yet [INF: left undefined so the field keeps its
    // authored default, the same choice PauseMenu makes for its empty panel].
    player.setVariableBool( "/:MapPlayerInfosLoaded", true );
    // The screen's localization categories (SetBDD -> /:db_map) are applied by
    // ApplyFlashLocalization, on the movie's sbdd command and once after the
    // load (the FreeRideHud pattern).
    numRouteBars = 0;
    player.setFSCommandCallback( [this]( const std::string &command, const std::string &args ) {
        OTDU_LOG_INFO( "MapScreen: FSCommand '%s' '%s'\n", command.c_str(), args.c_str() );
        if ( command == "sbdd" ) {
            ApplyFlashLocalization( player );
            return;
        }
        // The tab row reports its tab with 'bmap:fltr:icf<N>' (N = 0..5), the cue
        // for the game to fill that tab (the 'icf' without a digit is the
        // initial one) [DATA: -logmaptab]. -maptab <n> pins the game's tab for
        // the whole open (a test hook: the movie's own lit row still follows
        // its clicks; the retail MapTabObjective seeding only holds during the
        // open animation, which would land the row back on 0).
        if ( command.size() == 14 && command.compare( 0, 13, "bmap:fltr:icf" ) == 0 && command[13] >= '0' && command[13] <= '5' ) {
            const int32_t tabArg = CmdLine::GetArgInt( "-maptab", -1 );
            currentTab = ( tabArg >= 0 && tabArg <= 5 ) ? tabArg : command[13] - '0';
            return;
        }
        if ( command == "cred:open" ) {
            buildCredits();
            return;
        }
        if ( command == "cred:close" ) {
            bCreditsOpen = false;
            return;
        }
        // The retail screen reports its way out through its own commands; any
        // of them closes it (the pause menu's resume/close are the same idea).
        if ( StringCmpI( command.c_str(), "close" ) == 0 || StringCmpI( command.c_str(), "back" ) == 0 ||
             StringCmpI( command.c_str(), "mapout" ) == 0 || StringCmpI( command.c_str(), "quit" ) == 0 ) {
            close();
        }
    } );
    ApplyFlashLocalization( player );
    const VMFFile &vmf = pMovie->getVMF();
    if ( CmdLine::HasArg( "-logmap" ) || CmdLine::HasArg( "-dumplabels" ) ) {
        OTDU_LOG_INFO( "MapScreen: MAP.vmf frame labels:\n" );
        for ( const std::string &label : vmf.getFrameLabels() ) {
            OTDU_LOG_INFO( "  LABEL '%s'\n", label.c_str() );
        }
        for ( const auto &exported : vmf.getExports() ) {
            OTDU_LOG_INFO( "  EXPORT '%s' -> %u\n", exported.first.c_str(), exported.second );
        }
        bLabelsDumped = true;
    }
    if ( CmdLine::HasArg( "-dumpmapfuncs" ) ) {
        DumpFlashFunctions( player );
    }
    OTDU_LOG_INFO( "MapScreen: MAP.vmf loaded (%u labels)\n", static_cast<uint32_t>( vmf.getFrameLabels().size() ) );
    return true;
}

void MapScreen::unload() {
    player.unload();
    bOpen = false;
    bClosing = false;
    closeTimer = 0.0f;
    bLabelsDumped = false;
}

void MapScreen::open() {
    if ( !isLoaded() || bOpen ) {
        return;
    }
    bOpen = true;
    bClosing = false;
    bContest = false;
    bContestApplied = false;
    contestEntered.clear();
    bContestExtended = false;
    player.setClipVisible( "/presets", true );
    player.setClipVisible( "/championship", false );
    openTicks = 0;
    transition = 0.0f;
    // -maptab <n>: open on that tab (the retail MapTabObjective variable; the
    // movie resets its row during the open animation, so the icf cue is pinned
    // above while openTicks < 60).
    currentTab = Clamp( CmdLine::GetArgInt( "-maptab", 0 ), 0, 5 );
    // The spot icon clones do not survive a close: close_free is a root
    // timeline frame that drops /pack, so the next map_open builds a fresh one
    // (verified: a run that opened the map twice logged '74 spot icons
    // created' once, and the owner's second map had no POI icons at all -
    // "open map somewhere and drive a little reopen" and they never came
    // back). bSpotsBuilt is the "the clones are in the current /pack" flag,
    // so it has to be cleared with the display list: buildSpots re-clones
    // /pack/point and re-applies the zoom LOD at openTicks 45.
    bSpotsBuilt = false;
    player.setVariableBool( "/:IsMapAvailable", true );
    player.setVariableBool( "/:BackFromMap", false );
    player.setVariableBool( "/:IsBackFromMap", false );
    player.setVariableBool( "/:IsMapQuit", false );
    player.setVariableBool( "/:MapCancel", false );
    PlayFirstLabel( player, kOpenLabels, sizeof( kOpenLabels ) / sizeof( kOpenLabels[0] ), "open" );
}

void MapScreen::openContest( const ContestInfo &info ) {
    open();
    contest = info;
    bContest = true;
    bContestApplied = false;
    contestSelected = 0;
    contestPins.clear();
    contestPinLit = -1;
    for ( size_t i = 0; i < contest.Races.size(); i++ ) {
        if ( contest.Races[i].bEnterable ) {
            contestSelected = static_cast<int32_t>( i );
            break;
        }
    }
}

void MapScreen::applyContest() {
    bContestApplied = true;
    OTDU_LOG_INFO( "MapScreen: contest view '%s' (%s, %s)\n", contest.Name.c_str(), contest.Type.c_str(),
                   contest.ClassLabel.c_str() );
    // The rival line: the movie's own template ("Rival: <white>§RIVAL§").
    {
        std::string line = player.getVariable( "/:m_ChampRivalM" );
        const size_t at = line.find( "RIVAL" );
        if ( line.empty() || at == std::string::npos ) {
            line = "Rival: " + contest.Rival;
        } else {
            size_t start = at, end = at + 5;
            while ( start > 0 && static_cast<uint8_t>( line[start - 1] ) >= 0x80 ) {
                start--;
            }
            while ( end < line.size() && static_cast<uint8_t>( line[end] ) >= 0x80 ) {
                end++;
            }
            line.replace( start, end - start, contest.Rival );
        }
        player.setVariable( "/:ChampRival", line );
    }
    // The retail entry (displayChampionship(true), disassembled with
    // -dumpmapfuncs): ContestName = SpotName, ContestType = WhatSpot,
    // ContestGP = tool_tip.num_contest (the class number on the diamond),
    // SetHelp('map_championship'), championship.Open(0), which reads
    // nb_chall_done / nb_chall (the "0/6") and GoldReward (the reward).
    int32_t done = 0, total = 0;
    sscanf( contest.ChallDone.c_str(), "%d/%d", &done, &total );
    player.setVariable( "/:SpotName", contest.Name );
    player.setVariable( "/:WhatSpot", contest.Type );
    player.setVariable( "/tool_tip:num_contest", contest.ClassLabel.size() > 1 ? contest.ClassLabel.substr( 1 ) : "" );
    player.setVariable( "/tool_tip:ContestGP", contest.ClassLabel.size() > 1 ? contest.ClassLabel.substr( 1 ) : "" );
    player.setVariable( "/:ContestGP", contest.ClassLabel.size() > 1 ? contest.ClassLabel.substr( 1 ) : "" );
    player.setVariableNumber( "/:nb_chall_done", done );
    player.setVariableNumber( "/:nb_chall", total );
    player.setVariable( "/:GoldReward", contest.Reward );
    player.setVariable( "/:RestrictionType", "vehicle" );
    std::string group = contest.ClassLabel;
    for ( char &ch : group ) {
        ch = static_cast<char>( toupper( static_cast<unsigned char>( ch ) ) );
    }
    player.setVariable( "/:GroupRestriction", group );
    player.setClipVisible( "/presets", false ); // the tab row gives way to the header
    player.setClipVisible( "/championship", true );
    player.invokeWithNumbers( "/", "displayChampionship", { 1.0 } );
    player.setClipVisible( "/championship/pages/cont/p/picture", false );
    buildContestPins();
    showContestRace();
}

void MapScreen::buildContestPins() {
    contestPins.clear();
    contestPinLit = -1;
    for ( size_t i = 0; i < contest.Races.size(); i++ ) {
        const std::string name = "cpin" + std::to_string( i );
        if ( !player.duplicate( "/pack/point", name, 700 + static_cast<int32_t>( i ) ) ) {
            contestPins.push_back( std::string() );
            continue;
        }
        const std::string path = "/pack/" + name;
        // The art of the race's kind: the movie's pack/point/spot labels
        // race / timeattack / speedtrap / speed [DATA].
        std::string icon = contest.Races[i].Kind;
        for ( char &ch : icon ) {
            ch = static_cast<char>( tolower( static_cast<unsigned char>( ch ) ) );
        }
        player.gotoLabel( path + "/spot", icon, false );
        player.gotoLabel( path, "zoom_default", false );
        player.setClipVisible( path + "/fleche", false );
        player.setClipVisible( path + "/locator", false );
        contestPins.push_back( name );
    }
    OTDU_LOG_INFO( "MapScreen: %zu championship race pins\n", contestPins.size() );
}

void MapScreen::showContestRace() {
    if ( contest.Races.empty() ) {
        return;
    }
    const ContestInfo::Race &race = contest.Races[static_cast<size_t>( contestSelected )];
    center.x = race.Position.x;
    center.z = race.Position.z;
    // tool_tip.SetInfos() reads the spot variables of the root (the same
    // ones a map spot's bubble uses): the kind, the name, the reward, the
    // distance, the laps, the rules.
    player.setVariable( "/:WhatSpot", race.Kind );
    player.setVariable( "/:SpotName", race.Title );
    player.setVariable( "/:GoldReward", race.Reward );
    player.setVariable( "/:Distance", race.Distance );
    player.setVariable( "/:NbLaps", race.Laps );
    player.setVariable( "/:GameRules", race.Rules );
    player.setVariable( "/:GMDescription", race.Rules );
    player.invoke( "/tool_tip", "SetInfos", {} );
    player.gotoLabel( "/tool_tip", "open", true );
}

void MapScreen::contestStep( int32_t direction ) {
    if ( !bContest || contest.Races.empty() || bContestExtended ) {
        return;
    }
    const int32_t count = static_cast<int32_t>( contest.Races.size() );
    contestSelected = ( contestSelected + direction + count ) % count;
    showContestRace();
}

bool MapScreen::enterContestRace() {
    if ( !bContest || contest.Races.empty() ) {
        return false;
    }
    const ContestInfo::Race &race = contest.Races[static_cast<size_t>( contestSelected )];
    if ( !race.bEnterable ) {
        return false;
    }
    contestEntered = race.Name;
    return true;
}

bool MapScreen::contestAccept() {
    if ( !bContest || contest.Races.empty() ) {
        return false;
    }
    if ( !bContestExtended ) {
        // The first OK extends the bubble (tool_tip.Extend(1), the menu was
        // built by SetInfos -> SetMenu('chall_solo')): picture, rules, Enter / Close.
        bContestExtended = true;
        player.invokeWithNumbers( "/tool_tip", "Extend", { 1.0 } );
        return false;
    }
    return enterContestRace();
}

bool MapScreen::contestBack() {
    if ( !bContest || !bContestExtended ) {
        return false;
    }
    bContestExtended = false;
    player.invokeWithNumbers( "/tool_tip", "Extend", { 0.0 } );
    return true;
}

bool MapScreen::takeContestRace( std::string &outRace ) {
    if ( contestEntered.empty() ) {
        return false;
    }
    outRace = contestEntered;
    contestEntered.clear();
    return true;
}

void MapScreen::close() {
    SaveProfile( "map closed" );
    if ( !isLoaded() || !bOpen || bClosing ) {
        return;
    }
    PlayFirstLabel( player, kCloseLabels, sizeof( kCloseLabels ) / sizeof( kCloseLabels[0] ), "close" );
    bClosing = true;
    closeTimer = kCloseDuration;
}

void MapScreen::tick( float deltaTime ) {
    if ( !isLoaded() || !bOpen ) {
        return;
    }
    player.tick( deltaTime );
    transition = Clamp( transition + ( bClosing ? -deltaTime : deltaTime ) / kTransitionTime, 0.0f, 1.0f );
    if ( bCreditsRequested && !bCreditsOpen && !bClosing ) {
        bCreditsRequested = false;
        openCredits();
    }
    if ( bCreditsOpen && bCreditsBuilt ) {
        // [INF] the retail speed (CreditsPixelPerSec) is not decoded: 45 stage px/s.
        creditsY -= 45.0f * deltaTime;
        player.setVariableNumber( "/credits/scroll:_y", creditsY );
        if ( creditsY < creditsEndY ) {
            player.invoke( "/credits", "Close", {} );
            closeCreditsState();
        }
    }
    // Tabs: LB / RB (keyboard Q / E) call the tab row's own Move('left' /
    // 'right') (/presets, found with -dumpclips: Move, Clic, LightIc,
    // ChangeTxt, setObjective...) [DATA]; the movie lights the tab and sets
    // the title itself.
    // The row opens on tab 0 with nothing lit and no title; one step each way
    // after the open animation lights it [INF: the retail screen is lit on
    // arrival, the call that does it is not identified].
    openTicks++;
    if ( openTicks == 50 && CmdLine::HasArg( "-dumpclipfuncs" ) ) {
        for ( const char *pPath : { "/tool_tip", "/tool_tip/infos", "/championship", "/championship/pages", "/help", "/navigator" } ) {
            OTDU_LOG_INFO( "MapScreen: functions of %s\n", pPath );
            DumpFlashFunctions( player, pPath );
        }
    }
    if ( bContest && !bContestApplied && openTicks == CmdLine::GetArgInt( "-contestafter", 70 ) ) {
        applyContest();
    }
    if ( CmdLine::HasArg( "-mapcredits" ) && openTicks == 80 ) {
        // The real open path (bCreditsOpen has to be set, or Back closes the
        // map instead of the roll).
        openCredits();
    }
    if ( CmdLine::HasArg( "-mapcredits" ) && openTicks == 120 ) {
        // Why does the roll draw dimmed and empty: the scroll's own state and
        // what the first text field carries.
        OTDU_LOG_INFO( "MapScreen: credits scroll alpha '%s' visible '%s' y '%s'; txt0 '%s'\n",
                       player.getVariable( "/credits/scroll:_alpha" ).c_str(),
                       player.getVariable( "/credits/scroll:_visible" ).c_str(),
                       player.getVariable( "/credits/scroll:_y" ).c_str(),
                       player.getVariable( "/credits/scroll/txt0:txt" ).substr( 0, 60 ).c_str() );
    }
    if ( openTicks == 40 ) {
        player.invoke( "/presets", "Move", { "right" } );
    } else if ( openTicks == 55 ) {
        player.invoke( "/presets", "Move", { "left" } );
    }
    if ( !bClosing ) {
        if ( gpInput->wasPressed( eInputAction::IA_CarBackView ) || gpInput->wasKeyPressed( SDL_SCANCODE_Q ) ) {
            player.invoke( "/presets", "Move", { "left" } );
        }
        if ( gpInput->wasPressed( eInputAction::IA_CarCamera ) || gpInput->wasKeyPressed( SDL_SCANCODE_E ) ) {
            player.invoke( "/presets", "Move", { "right" } );
        }
    }
    if ( openTicks == 1 ) {
        for ( const char *pPath : { "/quick_match", "/club_match", "/championship", "/join_player", "/cheatmenu", "/mini_menu", "/lure_chall", "/lure_zoed", "/lure_zoed_free", "/pack/clue" } ) {
            player.setClipVisible( pPath, false );
        }
    }
    if ( openTicks == 45 && !bSpotsBuilt ) {
        buildSpots();
        // -logmapspots: the timeline of /pack/point/spot (the icon art: one
        // label per icon type, the art parts placed over several frames).
        if ( CmdLine::HasArg( "-logmapspots" ) ) {
            const FlashPlayer::Clip *pSpot = player.findClip( "/pack/point/spot" );
            if ( pSpot != nullptr && pSpot->pTimeline != nullptr ) {
                const FlashTimeline &t = *pSpot->pTimeline;
                OTDU_LOG_INFO( "MapScreen: spot timeline %u frames, %zu labels\n", t.FrameCount, t.Labels.size() );
                std::vector<std::pair<std::string, uint32_t>> labels( t.Labels.begin(), t.Labels.end() );
                std::sort( labels.begin(), labels.end(),
                           []( const std::pair<std::string, uint32_t> &a, const std::pair<std::string, uint32_t> &b ) {
                               return a.second < b.second;
                           } );
                for ( size_t l = 0; l < labels.size(); l++ ) {
                    const uint32_t first = labels[l].second;
                    const uint32_t last = l + 1 < labels.size() ? labels[l + 1].second : t.FrameCount;
                    std::string places;
                    for ( uint32_t f = first; f < Min( last, first + 12u ) && f < t.FrameCount; f++ ) {
                        int32_t count = 0;
                        for ( const FlashControlTag &tag : t.Frames[f] ) {
                            if ( tag.Type == eFlashControlType::CT_Place && tag.Place.bHasCharacter ) {
                                count++;
                            }
                        }
                        if ( count > 0 ) {
                            places += " f" + std::to_string( f ) + ":" + std::to_string( count );
                        }
                    }
                    OTDU_LOG_INFO( "MapScreen: spot '%s' f%u..%u places:%s\n", labels[l].first.c_str(), first,
                                   Min( last, first + 12u ), places.c_str() );
                    // The icon art itself: the character placed at the section
                    // frame and its own timeline (the art may be spread over
                    // its frames).
                    for ( const FlashControlTag &tag : t.Frames[first] ) {
                        if ( tag.Type != eFlashControlType::CT_Place ) {
                            continue;
                        }
                        OTDU_LOG_INFO( "MapScreen:   '%s' place depth %u char %u move %d name '%s'\n", labels[l].first.c_str(),
                                       tag.Place.Depth, tag.Place.CharacterId, tag.Place.bMove ? 1 : 0, tag.Place.Name.c_str() );
                        if ( !tag.Place.bHasCharacter || tag.Place.CharacterId == 0 ) {
                            continue;
                        }
                        const FlashTimeline *pIcon = player.getDefinitions().getSprite( tag.Place.CharacterId );
                        if ( pIcon == nullptr ) {
                            continue;
                        }
                        std::string iconLabels;
                        for ( const auto &label : pIcon->Labels ) {
                            iconLabels += " " + label.first + "@" + std::to_string( label.second );
                        }
                        std::string iconPlaces;
                        for ( uint32_t f = 0; f < Min<uint32_t>( pIcon->FrameCount, 8 ); f++ ) {
                            int32_t count = 0;
                            for ( const FlashControlTag &t2 : pIcon->Frames[f] ) {
                                if ( t2.Type == eFlashControlType::CT_Place && t2.Place.bHasCharacter ) {
                                    count++;
                                }
                            }
                            if ( count > 0 ) {
                                iconPlaces += " f" + std::to_string( f ) + ":" + std::to_string( count );
                            }
                        }
                        OTDU_LOG_INFO( "MapScreen:   '%s' -> char %u (%u frames) labels:%s places:%s\n", labels[l].first.c_str(),
                                       tag.Place.CharacterId, pIcon->FrameCount, iconLabels.c_str(), iconPlaces.c_str() );
                    }
                }
            }
        }
    }
    if ( CmdLine::HasArg( "-logmaptab" ) ) {
        static std::string lastTabState;
        const std::string tabState = player.getVariable( "/presets:last_ic" ) + " | " + player.getVariable( "/presets:ic_value" ) + " | i=" +
                                     player.getVariable( "/presets:i" ) + " | " + player.getVariable( "/presets:ic_path" ) + " | cmd=" +
                                     player.getVariable( "/presets:cmd" ) + " | WhatFilter=" + player.getVariable( "/:WhatFilter" ) +
                                     " | numIcFilter=" + player.getVariable( "/:numIcFilter" );
        if ( tabState != lastTabState ) {
            OTDU_LOG_INFO( "[tab] tick %d: %s\n", openTicks, tabState.c_str() );
            lastTabState = tabState;
        }
    }
    if ( bSpotsBuilt ) {
        updateSnap(); // the reticle sticks to a shop / road before the markers are placed
        updateMarkers();
        updateSpots();
    }
    static const bool bDumpClips = CmdLine::HasArg( "-dumpclips" );
    if ( bDumpClips && ++dumpTicks == CmdLine::GetArgInt( "-dumpafter", 90 ) ) {
        const char *pDumpPath = CmdLine::GetArgValue( "-dumppath" );
        player.dumpClips( pDumpPath != nullptr ? pDumpPath : "/", CmdLine::GetArgInt( "-dumpclips", 5 ) );
    }
    // -dumpclabels <path>[;<path>...]: the frame labels (with their frame
    // numbers) of those clips' timelines, once, at -dumpafter ticks (the
    // championship view hunt, docs/re/championship_screen_spec.md).
    if ( const char *pLabelPaths = CmdLine::GetArgValue( "-dumpclabels" ) ) {
        static uint32_t labelTicks = 0;
        if ( ++labelTicks == static_cast<uint32_t>( CmdLine::GetArgInt( "-dumpafter", 90 ) ) ) {
            std::string list = pLabelPaths;
            size_t at = 0;
            while ( at <= list.size() ) {
                const size_t semicolon = list.find( ';', at );
                const std::string path = list.substr( at, semicolon == std::string::npos ? std::string::npos : semicolon - at );
                if ( const FlashPlayer::Clip *pClip = path == "/" ? nullptr : player.findClip( path ) ) {
                    std::string text;
                    if ( pClip->pTimeline != nullptr ) {
                        for ( const auto &label : pClip->pTimeline->Labels ) {
                            text += " " + label.first + "@" + std::to_string( label.second + 1 );
                        }
                    }
                    OTDU_LOG_INFO( "MapScreen: labels of %s (frame %u/%u):%s\n", path.c_str(), pClip->CurrentFrame,
                                   pClip->pTimeline != nullptr ? pClip->pTimeline->FrameCount : 0u, text.c_str() );
                } else {
                    OTDU_LOG_INFO( "MapScreen: labels of %s: clip not found\n", path.c_str() );
                }
                if ( semicolon == std::string::npos ) {
                    break;
                }
                at = semicolon + 1;
            }
        }
    }
    if ( bClosing ) {
        closeTimer -= deltaTime;
        if ( closeTimer <= 0.0f ) {
            bClosing = false;
            bOpen = false;
        }
        return;
    }
    // The screen's own way out: the movie sets one of these when the player
    // backs out of it (the values are the flash boolean "true" as a string).
    const std::string back = player.getVariable( "/:IsBackFromMap" );
    const std::string quit = player.getVariable( "/:IsMapQuit" );
    const std::string cancel = player.getVariable( "/:MapCancel" );
    if ( back == "1" || quit == "1" || cancel == "1" ) {
        close();
    }
}

void MapScreen::draw( Renderer2D &renderer ) {
    if ( !isLoaded() || !bOpen ) {
        return;
    }
    player.draw( renderer );
}

void MapScreen::resetCamera( const Vector3f &newCenter ) {
    center = Vector3f( newCenter.x, 0.0f, newCenter.z );
    distance = 1400.0f;
    yaw = 0.0f;
    pitch = 60.0f * kDegToRad;
    // Island map: the whole island fits on screen like the retail opening
    // (about two thirds of the height), centred.
    mapScale = 0.0f;
    zoomLevel = 0;
    if ( pIslandTexture != nullptr ) {
        center = CmdLine::HasArg( "-mapatcar" ) ? Vector3f( newCenter.x, 0.0f, newCenter.z )
                                                : Vector3f( extentX * 0.5f, 0.0f, extentZ * 0.5f );
        mapScale = 0.73f * kMapHeight / extentZ;
    }
}

namespace {
constexpr float kMinDistance = 150.0f;
constexpr float kMaxDistance = 9000.0f;
constexpr float kMinPitch = 20.0f * kDegToRad;
constexpr float kMaxPitch = 90.0f * kDegToRad;
// [INF] tuning: a full stick pans one screen-distance per second, the zoom
// halves / doubles the distance in about a second.
constexpr float kPanSpeed = 0.9f;
constexpr float kZoomSpeed = 1.2f;
constexpr float kTurnSpeed = 1.6f;
} // namespace

void MapScreen::updateCamera( float deltaTime ) {
    if ( pIslandTexture != nullptr ) {
        if ( !bCreditsOpen && !bCreditsRequested ) {
            updateIslandView( deltaTime );
        }
        return; // the 3D levels only need the state set there
    }
    // Pan: the left stick, or the arrow keys (menu left / right / up / down).
    float panX = gpInput->getLeftStickX();
    float panY = gpInput->getLeftStickY(); // up = -1
    panX += ( gpInput->isDown( eInputAction::IA_MenuRight ) ? 1.0f : 0.0f ) -
            ( gpInput->isDown( eInputAction::IA_MenuLeft ) ? 1.0f : 0.0f );
    panY += ( gpInput->isDown( eInputAction::IA_MenuDown ) ? 1.0f : 0.0f ) -
            ( gpInput->isDown( eInputAction::IA_MenuUp ) ? 1.0f : 0.0f );
    const Vector3f forward( sinf( yaw ), 0.0f, cosf( yaw ) );
    const Vector3f right( -forward.z, 0.0f, forward.x ); // Eden x points to the left of the screen
    const float panStep = kPanSpeed * distance * deltaTime;
    center = center + right * ( panX * panStep ) - forward * ( panY * panStep );
    // Zoom: right / left trigger (accel / brake), W / S, or the mouse wheel.
    float zoom = gpInput->getAnalog( eInputAction::IA_CarAccel ) - gpInput->getAnalog( eInputAction::IA_CarBrake );
    // The wheel is notches per frame, not a rate: each notch is a 15 % step.
    const float wheelScale = expf( -gpInput->getMouseWheel() * 0.15f );
    distance = Clamp( distance * wheelScale * expf( -zoom * kZoomSpeed * deltaTime ), kMinDistance, kMaxDistance );
    // Rotate / tilt: the right stick.
    yaw += gpInput->getRightStickX() * kTurnSpeed * deltaTime;
    pitch = Clamp( pitch - gpInput->getRightStickY() * kTurnSpeed * deltaTime, kMinPitch, kMaxPitch );
}

void MapScreen::getCamera( EdenSceneView::Camera &outCamera ) const {
    float useYaw = yaw, usePitch = pitch, useDistance = distance;
    // The map screen pins its lens: the framing math assumes the authored
    // 60 degrees (the fov_scale option must not zoom the map).
    outCamera.FovScale = 1.0f;
    if ( pIslandTexture != nullptr && zoomLevel == 0 ) {
        // the flat map seen from straight above: same framing as the texture
        // (through the effective lens: the narrow-aspect law widens it off
        // 16:9, the distance follows so the island keeps filling the frame).
        useYaw = kPI;
        usePitch = 89.5f * kDegToRad;
        const float aspect = static_cast<float>( gpRender->getDevice().getBackbufferWidth() ) /
                             Max( 1.0f, static_cast<float>( gpRender->getDevice().getBackbufferHeight() ) );
        useDistance = ( kMapHeight * 0.5f / Max( mapScale, 1e-6f ) ) /
                      tanf( EdenEffectiveFov( 60.0f * kDegToRad, 1.0f, aspect ) * 0.5f );
    }
    const Vector3f forward( sinf( useYaw ), 0.0f, cosf( useYaw ) );
    const Vector3f eye = center - forward * ( cosf( usePitch ) * useDistance ) + Vector3f( 0.0f, sinf( usePitch ) * useDistance, 0.0f );
    outCamera.Position = eye;
    // Looking straight down has no horizon to define "up": keep the yaw by
    // nudging the target along the heading.
    outCamera.Rotation = ChaseCamera::LookRotation( eye, center + forward * 0.01f );
    outCamera.Near = Max( 1.0f, useDistance * 0.02f );
    outCamera.Far = Max( 40000.0f, useDistance * 1.6f );
}

int32_t MapScreen::getStreamRadius() const {
    // [INF] a sector is 768 m; the view at the current zoom reaches about
    // 1.2 x the distance on the ground. Capped: the loader is not free.
    return static_cast<int32_t>( Clamp( ceilf( distance * 1.2f / 768.0f ), 1.0f, 4.0f ) );
}

bool MapScreen::getMarkerOffset( float &outX, float &outY ) {
    if ( !isLoaded() ) {
        return false;
    }
    const std::string x = player.getVariable( "/:minimap_x_offset" );
    const std::string y = player.getVariable( "/:minimap_y_offset" );
    if ( x.empty() || y.empty() ) {
        return false;
    }
    outX = std::strtof( x.c_str(), nullptr );
    outY = std::strtof( y.c_str(), nullptr );
    return true;
}

void MapScreen::setIslandMap( GPUTexture *pTexture, float newExtentX, float newExtentZ ) {
    pIslandTexture = pTexture;
    extentX = newExtentX;
    extentZ = newExtentZ;
}

void MapScreen::setPlayerInfo( const std::string &vehicleName, double money ) {
    if ( !isLoaded() ) {
        return;
    }
    player.setVariable( "/:m_MapPlayerVehicle", vehicleName );
    player.setVariable( "/:player_car", vehicleName );
    player.setVariableNumber( "/:money", money );
    player.setVariableNumber( "/:MapPlayerMoney", money );
}

void MapScreen::setPlayer( const Vector3f &position, float headingX, float headingZ ) {
    playerPosition = position;
    playerHeadingX = headingX;
    playerHeadingZ = headingZ;
}

int32_t MapScreen::takeZoomStep( float deltaTime ) {
    // [INF] the pad hold: the first step at once, then one every
    // kZoomRepeatRate after kZoomRepeatDelay (the retail screen steps once per
    // press; a hold should not need four presses to cross the island).
    constexpr float kZoomRepeatDelay = 0.35f;
    constexpr float kZoomRepeatRate = 0.30f;
    int32_t step = 0;
    // The wheel is notches per frame: one notch = one step.
    const float wheel = gpInput->getMouseWheel();
    if ( CmdLine::HasArg( "-logmapzoom" ) ) {
        static float zoomLogTimer = 0.0f;
        zoomLogTimer += deltaTime;
        if ( zoomLogTimer >= 0.5f ) {
            zoomLogTimer = 0.0f;
            OTDU_LOG_INFO( "MapScreen zoom: transition %.2f level %d wheel %.0f accel %.2f brake %.2f hold %.2f\n",
                           transition, zoomLevel, wheel, gpInput->getAnalog( eInputAction::IA_CarAccel ),
                           gpInput->getAnalog( eInputAction::IA_CarBrake ), zoomHoldTime );
        }
    }
    if ( wheel > 0.0f ) {
        step = 1;
    } else if ( wheel < 0.0f ) {
        step = -1;
    }
    // LT / RT (the game's CAR_ACCEL / CAR_BRAKE triggers; Up / Down on the
    // keyboard) are analog: getAnalog() is the only way to see them, and
    // wasPressed() never fires for an axis.
    const bool bIn = gpInput->getAnalog( eInputAction::IA_CarAccel ) > 0.5f;
    const bool bOut = gpInput->getAnalog( eInputAction::IA_CarBrake ) > 0.5f;
    if ( bIn || bOut ) {
        const bool bSame = ( bIn && bZoomInHeld ) || ( bOut && bZoomOutHeld );
        if ( !bSame ) {
            // just pressed (or reversed): step at once
            zoomHoldTime = 0.0f;
            zoomRepeatTimer = 0.0f;
            step = bIn ? 1 : -1;
        } else {
            zoomHoldTime += deltaTime;
            if ( zoomHoldTime >= kZoomRepeatDelay ) {
                zoomRepeatTimer += deltaTime;
                if ( zoomRepeatTimer >= kZoomRepeatRate ) {
                    zoomRepeatTimer = 0.0f;
                    step = bIn ? 1 : -1;
                }
            }
        }
    } else {
        zoomHoldTime = 0.0f;
        zoomRepeatTimer = 0.0f;
    }
    bZoomInHeld = bIn;
    bZoomOutHeld = bOut;
    return step;
}

void MapScreen::updateIslandView( float deltaTime ) {
    if ( bClosing || transition < 1.0f ) {
        return; // the camera is flying: no input
    }
    // Four zoom steps, like the retail screen (owner's grabs): the whole
    // island, then three tilted 3D views. LT / RT (accel / brake keys) or the
    // wheel step one level; a step zooms to the cursor.
    const int32_t step = takeZoomStep( deltaTime );
    if ( step < 0 && zoomLevel == 0 ) {
        // The retail easter egg: zooming out past the furthest level rolls the
        // credits over the map (owner report: it reads as the game hanging -
        // the map dims, only a small Back hint stays, and the roll runs
        // minutes; Back exits, MapScreen::onBack).
        OTDU_LOG_INFO( "MapScreen: zoom out at level 0 -> the credits roll (Back exits)\n" );
        bCreditsRequested = true; // zooming out past the furthest level shows the credits
    }
    const int32_t previousLevel = zoomLevel;
    const Vector3f previousCenter = center;
    // Zooming to the cursor uses the free cursor: the snapped point is where
    // the reticle is drawn, not where the player is steering it.
    const Vector3f cursorPoint = freeCursorWorld();
    zoomLevel = Clamp( zoomLevel + step, 0, kNumZoomLevels - 1 );
    const float fitScale = 0.73f * kMapHeight / extentZ;
    const Vector3f islandCenter( extentX * 0.5f, 0.0f, extentZ * 0.5f );
    if ( zoomLevel != previousLevel ) {
        applySpotLod();
        if ( previousLevel == 0 ) {
            center = cursorPoint; // zoom to the cursor
            cursorX = kMapWidth * 0.5f;
            cursorY = kMapHeight * 0.5f;
        } else if ( zoomLevel == 0 ) {
            // back out: the cursor stays over the point the view was centred on
            center = islandCenter;
            cursorX = Clamp( kMapWidth * 0.5f + ( previousCenter.x - islandCenter.x ) * fitScale, 150.0f, kMapWidth - 150.0f );
            cursorY = Clamp( kMapHeight * 0.5f + ( previousCenter.z - islandCenter.z ) * fitScale, 150.0f, kMapHeight - 140.0f );
        }
    }
    // Retail scale per grab pixel (owner's four grabs from one spot, 2000 px
    // wide): 47, 11, ~3, <1 m, so each level is ~4x the last. Levels 1..3 are
    // one 3D camera that tilts as it comes in (75 -> 60 -> 45 deg, read off the
    // grabs) [INF: tilt and distances].
    constexpr float kDistance[kNumZoomLevels] = { 0.0f, 10500.0f, 2700.0f, 700.0f };
    constexpr float kPitch[kNumZoomLevels] = { 90.0f, 75.0f, 60.0f, 45.0f };
    if ( zoomLevel == 0 ) {
        mapScale += ( fitScale - mapScale ) * Min( 1.0f, deltaTime * 8.0f );
        center = islandCenter;
    } else {
        distance = kDistance[zoomLevel];
        pitch = kPitch[zoomLevel] * kDegToRad;
        yaw = kPI; // look towards -z: north up, east right, like the flat map
        mapScale = kMapHeight * 0.5f / ( distance * 0.58f );
    }
    float panX = gpInput->getLeftStickX();
    float panY = gpInput->getLeftStickY(); // up = -1
    panX += ( gpInput->isDown( eInputAction::IA_MenuRight ) ? 1.0f : 0.0f ) -
            ( gpInput->isDown( eInputAction::IA_MenuLeft ) ? 1.0f : 0.0f );
    panY += ( gpInput->isDown( eInputAction::IA_MenuDown ) ? 1.0f : 0.0f ) -
            ( gpInput->isDown( eInputAction::IA_MenuUp ) ? 1.0f : 0.0f );
    if ( zoomLevel == 0 ) {
        // the island fits: the cursor moves over it, inside the screen's chrome
        cursorX = Clamp( cursorX + panX * 420.0f * deltaTime, 100.0f, kMapWidth - 100.0f );
        cursorY = Clamp( cursorY + panY * 420.0f * deltaTime, 120.0f, kMapHeight - 110.0f );
    } else {
        cursorX = kMapWidth * 0.5f;
        cursorY = kMapHeight * 0.5f;
        // [INF] half a screen per second at full deflection.
        const float panStep = 0.5f * kMapHeight / mapScale * deltaTime;
        center.x = Clamp( center.x + panX * panStep, 0.0f, extentX );
        center.z = Clamp( center.z + panY * panStep, 0.0f, extentZ );
    }
}

void MapScreen::setFuelStations( const std::vector<Vector3f> &positions ) {
    fuelStations = positions;
}

void MapScreen::updateSnap() {
    // The retail reticle sticks to what it comes close to: a shop icon first,
    // then the road under it - the point it is on is what OK routes to and what
    // the fast travel button uses [INF: the retail snapping rules are in the
    // encrypted band; the radii are the icon cell / a few pixels of line].
    const int32_t previousSpot = snapSpot;
    const int32_t previousRoad = snapRoad;
    snapSpot = -1;
    snapRoad = -1;
    snapX = cursorX;
    snapY = cursorY;
    if ( !isUiShown() ) {
        return; // the reticle is hidden while the camera flies / the credits roll
    }
    auto pixelDistance = [&]( float stageX, float stageY ) {
        const float dx = stageX - cursorX, dy = StageToCanvasY( stageY ) - cursorY;
        return sqrtf( dx * dx + dy * dy );
    };
    // 1) Shops and the other points of interest of the tab on screen: they win
    // over the road, so the cursor lands on the icon of a shop it is over.
    // Undiscovered spots are skipped: no icon on the map, no snap, and so no
    // fast travel to them (the same reason the icons are not drawn).
    float bestSpot = 1e30f;
    for ( size_t i = 0; i < spots.size(); i++ ) {
        const Spot &spot = spots[i];
        if ( spot.pIcon == nullptr || spot.Tab != currentTab ) {
            continue;
        }
        if ( i >= spotDiscovered.size() || spotDiscovered[i] == 0 ) {
            continue;
        }
        float sx = 0.0f, sy = 0.0f;
        if ( !projectToStage( spot.Position, sx, sy ) ) {
            continue;
        }
        float radius = kSnapSpotPixels;
        if ( static_cast<int32_t>( i ) == previousSpot ) {
            radius += kSnapStickyPixels;
        }
        const float d = pixelDistance( sx, sy );
        if ( d < radius && d < bestSpot ) {
            bestSpot = d;
            snapSpot = static_cast<int32_t>( i );
            snapPoint = spot.Position;
            snapX = sx;
            snapY = StageToCanvasY( sy );
        }
    }
    if ( snapSpot >= 0 ) {
        return;
    }
    // 2) The road nearest to the free cursor, inside the snap radius. The
    // radius is a pixel distance: in world metres it grows as the view zooms
    // out (mapScale is pixels per metre).
    if ( roadEdges.empty() || mapScale <= 1e-6f ) {
        return;
    }
    float radiusPixels = kSnapRoadPixels;
    if ( previousRoad >= 0 ) {
        radiusPixels += kSnapStickyPixels;
    }
    const float radius = radiusPixels / mapScale;
    Vector3f point;
    const int32_t edge = findRoadNear( freeCursorWorld(), radius, &point );
    if ( edge < 0 ) {
        return;
    }
    float sx = 0.0f, sy = 0.0f;
    if ( !projectToStage( point, sx, sy ) || pixelDistance( sx, sy ) > radiusPixels ) {
        return;
    }
    snapRoad = edge;
    snapPoint = point;
    snapX = sx;
    snapY = StageToCanvasY( sy );
}

void MapScreen::drawFuelIcons( Renderer2D &renderer ) {
    // The gas stations of our own (a `fuel` prop, docs/re/modding.md 3): the movie
    // has no station icon - its pack/point/spot sprite carries 102 labels and not
    // one is a station - and no bank holds station icon art, so retail never had
    // one either (docs/re/fuel_gas_station_spec.md). The port draws it here, in the
    // map's own space, over the island and under the route.
    //
    // The art is <mods>/icons/fuel.png when present - an icon generated with any
    // image tool drops straight in (any size, aspect kept) - otherwise a small
    // built-in pump glyph, so a station is visible with no assets at all.
    if ( fuelStations.empty() || CmdLine::HasArg( "-nofuelicons" ) ) {
        return;
    }
    if ( !bFuelIconTried ) {
        bFuelIconTried = true;
        auto resolved = gpFile->resolvePath( "icons/fuel.png" );
        std::vector<uint8_t> pixels;
        uint32_t width = 0, height = 0;
        if ( resolved && LoadImageFileRGBA( *resolved, pixels, width, height ) && width > 0 && height > 0 ) {
            pFuelIconTexture = gpRender->getTextureCache().createFromRGBA( width, height, pixels.data(),
                                                                           "icons/fuel.png" );
            fuelIconAspect = static_cast<float>( width ) / static_cast<float>( height );
            OTDU_LOG_INFO( "MapScreen: fuel station icon '%s' (%ux%u)\n", resolved->string().c_str(), width, height );
        } else {
            OTDU_LOG_INFO( "MapScreen: no fuel station icon at 'icons/fuel.png' - the built-in glyph is used\n" );
        }
    }
    // The retail icons swap LOD art per zoom step (applySpotLod); the fuel icon
    // has one art, so it follows their on-screen size per level instead
    // [INF: matched to the LOD icons by eye - dezoom_lod2 ~25 px, dezoom_lod
    // ~46, zoom_lod between it and zoom_default ~64].
    static constexpr float kIconSize[kNumZoomLevels] = { 24.0f, 44.0f, 54.0f, 64.0f };
    const float size = kIconSize[Clamp( zoomLevel, 0, kNumZoomLevels - 1 )];
    const float w = fuelIconAspect >= 1.0f ? size : size * fuelIconAspect;
    const float h = fuelIconAspect >= 1.0f ? size / Max( fuelIconAspect, 0.01f ) : size;
    uint32_t drawn = 0;
    float firstX = 0.0f, firstY = 0.0f;
    for ( const Vector3f &position : fuelStations ) {
        float x = 0.0f, stageY = 0.0f;
        if ( !projectToStage( position, x, stageY ) ) {
            continue;
        }
        const float y = StageToCanvasY( stageY ); // the map's stage offset (drawIsland)
        if ( x < -50.0f || x > 1330.0f || y < -50.0f || y > 770.0f ) {
            continue;
        }
        if ( pFuelIconTexture != nullptr ) {
            renderer.drawQuad( pFuelIconTexture, Rect2D( x - w * 0.5f, y - h * 0.5f, w, h ), 0xffffffffu );
        } else {
            // Colour words are AABBGGRR (renderer_2d): white body, amber window,
            // dark nozzle and base - a pump at 22 px.
            renderer.drawRect( Rect2D( x - 6.0f, y - 10.0f, 10.0f, 18.0f ), 0xfff0f0f0u );
            renderer.drawRect( Rect2D( x - 4.0f, y - 8.0f, 6.0f, 6.0f ), 0xff30a0e0u );
            renderer.drawRect( Rect2D( x + 4.0f, y - 8.0f, 2.0f, 10.0f ), 0xff202020u );
            renderer.drawRect( Rect2D( x - 8.0f, y + 8.0f, 14.0f, 2.0f ), 0xff202020u );
        }
        drawn++;
        if ( drawn == 1 ) {
            firstX = x;
            firstY = y;
        }
    }
    if ( drawn > 0 ) {
        static bool bLogged = false;
        if ( !bLogged ) {
            bLogged = true;
            OTDU_LOG_INFO( "MapScreen: %u fuel station icon(s) on the map%s, first at (%.0f, %.0f)\n", drawn,
                           pFuelIconTexture != nullptr ? " (icons/fuel.png)" : " (built-in glyph)", firstX, firstY );
        }
    }
}

void MapScreen::drawIsland( Renderer2D &renderer ) {
    if ( !isOpen() ) {
        return;
    }
    const float alpha = getFlatAlpha();
    const bool bRoute = isUiShown() && routePoints.size() > 1;
    const bool bExplored = isUiShown() && exploredLength > 0.0f;
    // The whole road network is drawn now (grey where it is undiscovered), so
    // the pass runs whenever the graph is loaded and the screen is up.
    const bool bRoads = isUiShown() && !roadEdges.empty();
    if ( alpha <= 0.0f && !bRoute && !bExplored && !bRoads && fuelStations.empty() ) {
        return;
    }
    renderer.begin( kMapWidth, kMapHeight, false );
    if ( alpha > 0.0f ) {
        const uint32_t a8 = static_cast<uint32_t>( alpha * 255.0f ) << 24;
        // The sea: the texture's own deep blue continued past its edge. The
        // colour word is AABBGGRR: the alpha byte must come from a8 — with a
        // literal 0x002e0e00 the rectangle was fully transparent and the map
        // showed no sea at all outside the island texture.
        renderer.drawRect( Rect2D( -2000.0f, -2000.0f, kMapWidth + 4000.0f, kMapHeight + 4000.0f ),
                           a8 | 0x002e0e00u );
        const float cx = kMapWidth * 0.5f - center.x * mapScale;
        const float cy = kMapHeight * 0.5f - center.z * mapScale;
        renderer.drawQuad( pIslandTexture, Rect2D( cx, cy, extentX * mapScale, extentZ * mapScale ), a8 | 0x00ffffffu );
    }
    if ( bRoads ) {
        // The island's roads, coloured the retail way [DATA: GamePC.cpr [HudMap]
        // via docs/re/freeride_spec.md 4]: a road that has been driven takes the
        // colour of its class (offroad = yellow 243/196/6, the tarmac classes
        // blue 0/255/255, a highway 29/180/215, a circuit 102/102/204), a road
        // still to be discovered stays grey 140/140/140 - the player can see the
        // whole network, and only the discovered part can be fast travelled to.
        static const float kRoadWidth[kNumZoomLevels] = { 2.5f, 5.0f, 7.0f, 9.0f };
        const float width = kRoadWidth[Clamp( zoomLevel, 0, kNumZoomLevels - 1 )];
        const uint8_t dirtClass = getDirtClass();
        auto classColour = [&]( uint8_t cls ) {
            if ( cls == dirtClass ) {
                return MakeRGBA( 243, 196, 6 ); // offroad: the yellow of the retail table
            }
            switch ( cls ) {
            case 0: return MakeRGBA( 0, 255, 255 ); // road / secondary: cyan blue
            case 2: return MakeRGBA( 29, 180, 215 ); // highway
            case 4: return MakeRGBA( 102, 102, 204 ); // circuit
            default: return MakeRGBA( 0, 255, 255 );
            }
        };
        // One vertex list per bucket: the undiscovered roads, the dirt and the
        // tarmac. The grey goes down first, the dirt on top.
        std::vector<Vertex2D> vertices[3];
        std::vector<float> px, py;
        for ( size_t i = 0; i < roadEdges.size(); i++ ) {
            const bool bDriven = i < edgeExplored.size() && edgeExplored[i] != 0;
            const uint8_t cls = i < edgeClass.size() ? edgeClass[i] : 0;
            const uint32_t colour = bDriven ? classColour( cls ) : MakeRGBA( 140, 140, 140 );
            const size_t bucket = bDriven ? ( cls == dirtClass ? 1u : 2u ) : 0u;
            const std::vector<Vector3f> &shape = roadShapes[i];
            // Project the curve, dropping the points that land on the same
            // pixel as the last kept one: at the island level a road point is
            // every ~28 m, and drawing all of them costs a few hundred thousand
            // vertices a frame for no visible difference.
            px.clear();
            py.clear();
            for ( const Vector3f &point : shape ) {
                float stageY = 0.0f;
                float x = 0.0f;
                if ( !projectToStage( point, x, stageY ) ) {
                    continue;
                }
                const float y = StageToCanvasY( stageY );
                if ( !px.empty() ) {
                    const float dx = x - px.back(), dy = y - py.back();
                    if ( dx * dx + dy * dy < 1.5f * 1.5f ) {
                        continue;
                    }
                }
                px.push_back( x );
                py.push_back( y );
            }
            for ( size_t k = 0; k + 1 < px.size(); k++ ) {
                const float ax = px[k], ay = py[k], bx = px[k + 1], by = py[k + 1];
                if ( ( ax < -100.0f && bx < -100.0f ) || ( ax > 1380.0f && bx > 1380.0f ) || ( ay < -100.0f && by < -100.0f ) ||
                     ( ay > 820.0f && by > 820.0f ) ) {
                    continue;
                }
                const float dx = bx - ax, dy = by - ay;
                const float length = sqrtf( dx * dx + dy * dy );
                if ( length < 0.01f ) {
                    continue;
                }
                const float half = 0.5f * width * ( ( cls == 2 || cls == 4 ) ? 1.33f : 1.0f ); // the retail 4 vs 3 px
                const float ux = dx / length, uy = dy / length;
                const float nx = -uy * half, ny = ux * half;
                auto vertex = [&]( float vx, float vy ) {
                    Vertex2D v;
                    v.X = vx;
                    v.Y = vy;
                    v.U = v.V = 0.0f;
                    v.Color = colour;
                    return v;
                };
                std::vector<Vertex2D> &out = vertices[bucket];
                out.push_back( vertex( ax - ux * half + nx, ay - uy * half + ny ) );
                out.push_back( vertex( ax - ux * half - nx, ay - uy * half - ny ) );
                out.push_back( vertex( bx + ux * half + nx, by + uy * half + ny ) );
                out.push_back( vertex( bx + ux * half + nx, by + uy * half + ny ) );
                out.push_back( vertex( ax - ux * half - nx, ay - uy * half - ny ) );
                out.push_back( vertex( bx + ux * half - nx, by + uy * half - ny ) );
            }
        }
        for ( std::vector<Vertex2D> &out : vertices ) {
            if ( !out.empty() ) {
                renderer.drawTriangles( nullptr, out.data(), static_cast<uint32_t>( out.size() ) );
            }
        }
    }
    // The gas stations of our own, over the island and under the route.
    drawFuelIcons( renderer );
    if ( bRoute ) {
        // The GPS route: a thick red line along the projected polyline. The
        // movie's own stroke clips (strokes_pack) are DefineMorphShape objects,
        // which the Flash runtime does not play, so the line is drawn here
        // [INF: colour / width from the owner's grabs: pure red, ~3.5 px at the
        // island level up to ~14 px at the closest level, on a 1280 canvas].
        static const float kThickness[kNumZoomLevels] = { 3.5f, 7.0f, 10.0f, 13.0f };
        const float half = 0.5f * kThickness[Clamp( zoomLevel, 0, kNumZoomLevels - 1 )];
        std::vector<Vertex2D> vertices;
        std::vector<float> px( routePoints.size() ), py( routePoints.size() );
        std::vector<char> ok( routePoints.size() );
        for ( size_t k = 0; k < routePoints.size(); k++ ) {
            float stageY = 0.0f;
            ok[k] = projectToStage( routePoints[k], px[k], stageY ) ? 1 : 0;
            py[k] = StageToCanvasY( stageY ); // stage -> the 1280 x 720 canvas
        }
        constexpr uint32_t kRed = 0xff1a1affu; // RGBA8: R ff, G 1a, B 1a, A ff
        auto vertex = [&]( float x, float y ) {
            Vertex2D v;
            v.X = x;
            v.Y = y;
            v.U = v.V = 0.0f;
            v.Color = kRed;
            return v;
        };
        for ( size_t k = 0; k + 1 < routePoints.size(); k++ ) {
            if ( !ok[k] || !ok[k + 1] ) {
                continue;
            }
            const float dx = px[k + 1] - px[k], dy = py[k + 1] - py[k];
            const float length = sqrtf( dx * dx + dy * dy );
            if ( length < 0.01f || ( px[k] < -100.0f && px[k + 1] < -100.0f ) || ( px[k] > 1380.0f && px[k + 1] > 1380.0f ) ||
                 ( py[k] < -100.0f && py[k + 1] < -100.0f ) || ( py[k] > 820.0f && py[k + 1] > 820.0f ) ) {
                continue;
            }
            const float ux = dx / length, uy = dy / length;
            const float nx = -uy * half, ny = ux * half;
            // a little overhang at both ends covers the joints
            const float ax = px[k] - ux * half, ay = py[k] - uy * half;
            const float bx = px[k + 1] + ux * half, by = py[k + 1] + uy * half;
            vertices.push_back( vertex( ax + nx, ay + ny ) );
            vertices.push_back( vertex( ax - nx, ay - ny ) );
            vertices.push_back( vertex( bx + nx, by + ny ) );
            vertices.push_back( vertex( bx + nx, by + ny ) );
            vertices.push_back( vertex( ax - nx, ay - ny ) );
            vertices.push_back( vertex( bx - nx, by - ny ) );
        }
        if ( !vertices.empty() ) {
            renderer.drawTriangles( nullptr, vertices.data(), static_cast<uint32_t>( vertices.size() ) );
        }
    }
    renderer.end();
}

bool MapScreen::onBack() {
    if ( bCreditsOpen ) {
        player.invoke( "/credits", "B", {} );
        player.invoke( "/credits", "Close", {} );
        closeCreditsState();
        return true;
    }
    return false;
}

void MapScreen::closeCreditsState() {
    // The 'credits' open moved the ROOT timeline onto its credits frame (the
    // clip dump after an exit holds only the 'fake' backdrop and the text, the
    // whole map tree is gone [DATA: -dumpclips]) - 'credits_back' is the root's
    // own return label [DATA: MAP.vmf labels]. Dropping the credits child alone
    // (its Close) left the stage black after an early exit.
    player.gotoLabel( "/", "credits_back", false );
    player.setClipVisible( "/credits", false );
    // The frame change replaced the display list that held the spot icon
    // clones (verified: the post-credits map had no POI icons) - back onto the
    // open frame the pack lives on and rebuild them (buildSpots re-clones
    // /pack/point and re-applies the LOD).
    PlayFirstLabel( player, kOpenLabels, sizeof( kOpenLabels ) / sizeof( kOpenLabels[0] ), "open" );
    buildSpots();
    bCreditsOpen = false;
}

void MapScreen::openCredits() {
    // _root.openCredits() runs credits.beginScroll() and sends the FSCommand
    // 'cred:open', the game's cue to fill the scroll [DATA].
    player.setClipVisible( "/credits", true ); // onBack hid it (its Close blacks the stage)
    bCreditsBuilt = false;
    const bool bOpened = player.invoke( "/", "openCredits", {} );
    bCreditsOpen = bOpened;
    OTDU_LOG_INFO( "MapScreen: openCredits: invoke %d -> bCreditsOpen %d\n", bOpened ? 1 : 0, bCreditsOpen ? 1 : 0 );
}

void MapScreen::buildCredits() {
    // FrontEnd/AllRes/credits.txt (MngCredits): "%Title" = a section title
    // (clip scroll/title%d), "[[Role]]" a grey line and "((names))" white
    // lines (clip scroll/txt%d), "/" a gap [DATA]. The markup colours are
    // MngCredits' own strings (#color:999999# #color:FFFFFF# #color:64c8ff#).
    std::vector<uint8_t> data;
    if ( gpFile == nullptr || !gpFile->loadFile( ( gpFile->getBankFolder() + "frontend/allres/credits.txt" ).c_str(), data ) ) {
        OTDU_LOG_WARN( "MapScreen: frontend/allres/credits.txt not found\n" );
        return;
    }
    struct Entry {
        bool bTitle;
        std::string Text;
        float Gap; // stage px before this entry
    };
    std::vector<Entry> entries;
    const std::string text( data.begin(), data.end() );
    std::string current;
    float pendingGap = 0.0f;
    auto flush = [&]() {
        if ( !current.empty() ) {
            entries.push_back( { false, current, pendingGap } );
            current.clear();
            pendingGap = 0.0f;
        }
    };
    bool bInNames = false;
    size_t pos = 0;
    while ( pos <= text.size() ) {
        size_t end = text.find( '\n', pos );
        if ( end == std::string::npos ) {
            end = text.size();
        }
        std::string line = text.substr( pos, end - pos );
        pos = end + 1;
        while ( !line.empty() && ( line.back() == '\r' || line.back() == ' ' ) ) {
            line.pop_back();
        }
        if ( bInNames ) {
            const bool bLast = line.size() >= 2 && line.compare( line.size() - 2, 2, "))" ) == 0;
            current += "\n" + ( bLast ? line.substr( 0, line.size() - 2 ) : line );
            if ( bLast ) {
                current += "#nocolor#";
                bInNames = false;
                flush();
            }
        } else if ( line.size() >= 4 && line.compare( 0, 2, "[[" ) == 0 ) {
            flush();
            current = "#color:999999#" + line.substr( 2, line.size() - 4 ) + "#nocolor#";
        } else if ( line.size() >= 2 && line.compare( 0, 2, "((" ) == 0 ) {
            const bool bLast = line.size() >= 4 && line.compare( line.size() - 2, 2, "))" ) == 0;
            current += "\n#color:FFFFFF#" + ( bLast ? line.substr( 2, line.size() - 4 ) : line.substr( 2 ) );
            if ( bLast ) {
                current += "#nocolor#";
                flush();
            } else {
                bInNames = true;
            }
        } else if ( !line.empty() && line[0] == '%' ) {
            flush();
            entries.push_back( { true, "#color:64c8ff#" + line.substr( 1 ) + "#nocolor#", pendingGap + 30.0f } );
            pendingGap = 0.0f;
        } else if ( line == "/" ) {
            flush();
            pendingGap += 22.0f;
        }
        if ( end == text.size() ) {
            break;
        }
    }
    flush();
    // Lay the entries out below each other: the template clips are title0 and
    // txt0; the others are clones of them.
    int32_t numTitles = 0, numTexts = 0;
    float y = 0.0f;
    for ( size_t i = 0; i < entries.size(); i++ ) {
        const Entry &entry = entries[i];
        const char *pKind = entry.bTitle ? "title" : "txt";
        const int32_t index = entry.bTitle ? numTitles++ : numTexts++;
        const std::string name = std::string( pKind ) + std::to_string( index );
        const std::string path = "/credits/scroll/" + name;
        if ( index > 0 ) {
            player.duplicate( std::string( "/credits/scroll/" ) + pKind + "0", name, 100 + static_cast<int32_t>( i ) );
        }
        y += entry.Gap;
        player.setVariable( path + ":txt", entry.Text );
        player.setVariableNumber( path + ":_y", y );
        int32_t lines = 1;
        for ( char c : entry.Text ) {
            lines += c == '\n' ? 1 : 0;
        }
        y += entry.bTitle ? 36.0f : 26.0f * lines + 4.0f;
    }
    // Start below the visible band (stage y 120..840) and end once the last
    // line has left it.
    creditsY = 860.0f;
    bCreditsBuilt = true;
    creditsEndY = -y - 160.0f;
    player.setVariableNumber( "/credits/scroll:_y", creditsY );
    // The movie leaves the scroll at alpha 0 for the game to fade in [DATA: dump].
    player.setVariableNumber( "/credits/scroll:_alpha", 100.0 );
    OTDU_LOG_INFO( "MapScreen: credits built: %zu entries (%d titles, %d texts), %.0f px\n", entries.size(), numTitles,
                   numTexts, y );
}

void MapScreen::loadSpots( const std::string &island ) {
    spots.clear();
    spotDiscovered.clear();
    spotsIsland.clear();
    if ( CmdLine::HasArg( "-logbrands" ) && gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        const uint32_t numBrands = root.getArraySize( "DBBrands" );
        for ( uint32_t i = 0; i < numBrands; i++ ) {
            XmbValue brand = root.getArrayElement( "DBBrands", i );
            OTDU_LOG_INFO( "[brand] %u hc 0x%llx index %d id '%s' name '%s' notcar %d\n", i,
                           static_cast<unsigned long long>( brand["_hashcode"].asUInt64() ),
                           static_cast<int>( brand["_logo_global_index"].asInt() ),
                           brand["_manufacturer_id"].asString().c_str(), brand["_manufacturer_name"].asString().c_str(),
                           brand["_is_not_a_car_brand"].asBool() ? 1 : 0 );
        }
    }
    bSpotsBuilt = false;
    // islands/<island>/level/spots.bin: ".SPT", "SPTA" { u32 size } then 0x110-byte
    // records [DATA]: +8 bank name (8 chars), +0x10 position (3 floats),
    // +0x100 the category byte (checked against the building names).
    std::vector<uint8_t> data;
    if ( gpFile == nullptr ||
         !gpFile->loadFile( ( gpFile->getBankFolder() + "islands/" + island + "/level/spots.bin" ).c_str(), data ) ||
         data.size() < 0x20 ) {
        return;
    }
    const size_t count = ( data.size() - 0x20 ) / 0x110;
    for ( size_t i = 0; i < count; i++ ) {
        const uint8_t *pRecord = data.data() + 0x20 + i * 0x110;
        Spot spot;
        char name[9] = {};
        memcpy( name, pRecord + 8, 8 );
        spot.Name = name;
        memcpy( &spot.Id, pRecord, 8 );
        float position[3];
        memcpy( position, pRecord + 0x10, sizeof( position ) );
        spot.Position = Vector3f( position[0], position[1], position[2] );
        const uint8_t category = pRecord[0x100];
        auto startsWith = [&]( const char *pPrefix ) { return spot.Name.compare( 0, strlen( pPrefix ), pPrefix ) == 0; };
        // [INF] icon and tab per building category: the names are the building
        // banks ('ECD' dealers, 'ETUN' tuning, 'EPH' houses...), the labels are
        // the frame labels of the movie's spot clip, the tab indices the tab row
        // (0 single player, 2 avatar spots, 3 vehicle spots).
        spot.pIcon = nullptr;
        spot.Tab = -1;
        switch ( category ) {
        case 0x00: spot.pIcon = "estates"; spot.Tab = 2; break;
        case 0x01:
            spot.pIcon = "cardealer";
            spot.Tab = 3;
            spot.LogoIndex = dealerLogoIndex( spot.Name, spot.Id );
            break;
        // 3 and 5 [DATA: the source paths inside the interior banks]: EAS_* /
        // ESHOP_CA are spots\avatshp (the clothes shops), ECH_* spots\clubhous.
        case 0x03: spot.pIcon = "clothes"; spot.Tab = 2; break;
        case 0x04: spot.pIcon = "tuning"; spot.Tab = 3; break;
        case 0x05: spot.pIcon = "club"; spot.Tab = 2; break;
        case 0x06: spot.pIcon = "gamblinghall"; spot.Tab = 2; break;
        case 0x08:
            if ( startsWith( "ECW" ) ) { spot.pIcon = "carwash"; spot.Tab = 3; }
            else if ( startsWith( "ESTICK" ) ) { spot.pIcon = "carpaint"; spot.Tab = 3; }
            else if ( startsWith( "ENC" ) ) { spot.pIcon = "club"; spot.Tab = 2; }
            else if ( startsWith( "ERE" ) ) { spot.pIcon = "estates"; spot.Tab = 2; }
            // EUC_*: spots\under_construction (building sites, no icon) [DATA].
            break;
        // ELANDCAA (Ibiza / Hawai): the seaplane dock of the casino trip; the
        // casino icon [INF: the retail icon of category 0x0e is not read].
        case 0x0e:
            if ( startsWith( "ELANDCA" ) ) { spot.pIcon = "gamblinghall"; spot.Tab = 2; }
            break;
        case 0x09: spot.pIcon = "rschool"; spot.Tab = 0; break;
        case 0x0b: spot.pIcon = "photograph"; spot.Tab = 0; break;
        case 0x10: spot.pIcon = "surgery"; spot.Tab = 2; break;
        case 0x11: spot.pIcon = "stolencar"; spot.Tab = 3; break;
        case 0x13: spot.pIcon = "hair"; spot.Tab = 2; break;
        case 0x16: spot.pIcon = "airport"; spot.Tab = 3; break;
        case 0x19: spot.pIcon = "home"; spot.Tab = 2; break;
        default: break;
        }
        if ( spot.pIcon != nullptr ) {
            char clip[16];
            snprintf( clip, sizeof( clip ), "point%03d", static_cast<int>( spots.size() ) + 1 );
            spot.Clip = clip;
            spots.push_back( spot );
        }
    }
    // The discovery state (the retail law: a spot's icon shows on the map only
    // once the car drove within discovery range of the building; the profile
    // keeps the ids per island). Auto-known: the houses the player owns (their
    // home icon is on the map from the start) and the home spots (0x19, the
    // story's hotel) [INF: the retail KnownSpots seeding is not decoded].
    spotsIsland = island;
    spotDiscovered.assign( spots.size(), 0 );
    const PlayerProfile &profile = GetPlayerProfile();
    const auto known = profile.KnownSpots.find( island );
    int32_t numDiscovered = 0;
    for ( size_t i = 0; i < spots.size(); i++ ) {
        const Spot &spot = spots[i];
        const bool bAutoKnown = ( spot.pIcon != nullptr && strcmp( spot.pIcon, "estates" ) == 0 &&
                                  spot.Name.compare( 0, 3, "EPH" ) == 0 && IsHouseOwned( spot.Id ) ) ||
                                strcmp( spot.pIcon, "home" ) == 0;
        if ( bAutoKnown ) {
            spotDiscovered[i] = 1;
            numDiscovered++;
        } else if ( known != profile.KnownSpots.end() ) {
            for ( uint64_t id : known->second ) {
                if ( id == spot.Id ) {
                    spotDiscovered[i] = 1;
                    numDiscovered++;
                    break;
                }
            }
        }
    }
    OTDU_LOG_INFO( "MapScreen: %zu spots with an icon (of %zu in spots.bin), %d discovered\n", spots.size(), count,
                   numDiscovered );
    if ( CmdLine::HasArg( "-logspots" ) ) {
        for ( size_t i = 0; i < spots.size(); i++ ) {
            const Spot &spot = spots[i];
            OTDU_LOG_INFO( "MapScreen: spot '%s' icon '%s' logo %d %s at (%.0f, %.0f)\n", spot.Name.c_str(), spot.pIcon,
                           spot.LogoIndex, spotDiscovered[i] ? "known" : "undiscovered", spot.Position.x, spot.Position.z );
        }
    }
}

void MapScreen::buildSpots() {
    // The native clones /pack/point per spot (PS3 strings "/pack/point%03d",
    // "resetIconStatus"...) and picks the art with the frame label of
    // pack/point/spot [DATA]. Undiscovered spots get no clone: the retail map
    // draws a spot's icon only once the car discovered it [INF, see
    // discoverNear] - which is also why the icons are rebuilt on every open.
    bSpotsBuilt = true;
    int32_t made = 0;
    for ( size_t i = 0; i < spots.size(); i++ ) {
        const Spot &spot = spots[i];
        if ( i >= spotDiscovered.size() || spotDiscovered[i] == 0 ) {
            continue;
        }
        if ( !player.duplicate( "/pack/point", spot.Clip, 200 + static_cast<int32_t>( i ) ) ) {
            continue;
        }
        const std::string path = "/pack/" + spot.Clip;
        // The houses you own carry the home icon (the "estates" art stays for
        // the ones still on sale) [INF: the retail law of the f_home filter].
        const char *pIcon = spot.pIcon;
        if ( spot.pIcon != nullptr && strcmp( spot.pIcon, "estates" ) == 0 && spot.Name.compare( 0, 3, "EPH" ) == 0 &&
             IsHouseOwned( spot.Id ) ) {
            pIcon = "home";
            OTDU_LOG_INFO( "MapScreen: '%s' is home: home icon\n", spot.Name.c_str() );
        }
        player.gotoLabel( path + "/spot", pIcon, false );
        player.gotoLabel( path, "zoom_default", false );
        if ( spot.LogoIndex >= 0 ) {
            // pack/point/spot/ic/logo: one frame per logo of the brands_map
            // atlas, the frame being _logo_global_index + 1 [INF].
            const uint32_t logoFrame = CmdLine::HasArg( "-logoframe" ) ? static_cast<uint32_t>( CmdLine::GetArgInt( "-logoframe", 1 ) )
                                                                       : static_cast<uint32_t>( spot.LogoIndex + 1 );
            const bool bLogo = player.gotoFrame( path + "/spot/ic/logo", logoFrame, false );
            player.gotoLabel( path + "/spot/ic", "loop", true );
            // The logo quad (logo/lure) is a placeholder: bind its cell of the
            // 1024 x 256 brands_map atlas, 16 x 4 cells of 64 px in
            // _logo_global_index order [DATA: DBBrands and the atlas].
            if ( FlashMovie *pMovie = gpFlash != nullptr ? gpFlash->getMovie( "map" ) : nullptr ) {
                if ( i == 5 ) {
                    OTDU_LOG_INFO( "MapScreen: brands_map page %p\n", static_cast<void *>( pMovie->findPage( "brands_map" ) ) );
                }
                if ( GPUTexture *pAtlas = pMovie->findPage( "brands_map" ) ) {
                    const int32_t cell = spot.LogoIndex;
                    const float u0 = static_cast<float>( ( cell % 16 ) * 64 ) / 1024.0f;
                    const float v0 = static_cast<float>( ( cell / 16 ) * 64 ) / 256.0f;
                    player.setBitmapOverride( path + "/spot/ic/logo/lure", pAtlas, u0, v0, u0 + 64.0f / 1024.0f, v0 + 64.0f / 256.0f );
                }
            }
            if ( i == 5 ) {
                OTDU_LOG_INFO( "MapScreen: logo of %s -> frame %u: %d\n", path.c_str(), logoFrame, bLogo ? 1 : 0 );
            }
        }
        // The pin and the arrow under the icon are selection / GPS markers; the
        // retail grabs show the bare icon.
        // -dumpmaplocator: the art of the first spot's fleche / locator
        // children (the GPS-destination dressing hunt): character, labels and
        // the bitmaps of every shape under them.
        if ( i == 0 && CmdLine::HasArg( "-dumpmaplocator" ) ) {
            const FlashDefinitions &defs = player.getDefinitions();
            std::function<void( uint16_t, int )> walk = [&]( uint16_t id, int level ) {
                if ( level > 5 ) {
                    return;
                }
                if ( const FlashShape *pShape = defs.getShape( id ) ) {
                    for ( const FlashFill &fill : pShape->Fills ) {
                        OTDU_LOG_INFO( "MapScreen: %*sshape %u fill type %d bitmap %u color %08x bounds (%.1f %.1f)-(%.1f %.1f)\n",
                                       level * 2, "", id, static_cast<int>( fill.Type ), fill.BitmapId, fill.Color,
                                       pShape->XMin / 20.0f, pShape->YMin / 20.0f, pShape->XMax / 20.0f, pShape->YMax / 20.0f );
                    }
                    return;
                }
                if ( const FlashTimeline *pSprite = defs.getSprite( id ) ) {
                    std::string labels;
                    for ( const auto &label : pSprite->Labels ) {
                        labels += " '" + label.first + "'@" + std::to_string( label.second + 1 );
                    }
                    OTDU_LOG_INFO( "MapScreen: %*ssprite %u frames %u labels%s\n", level * 2, "", id, pSprite->FrameCount,
                                   labels.c_str() );
                    std::set<uint16_t> seen;
                    for ( const auto &frame : pSprite->Frames ) {
                        for ( const FlashControlTag &tag : frame ) {
                            if ( tag.Type == eFlashControlType::CT_Place && tag.Place.bHasCharacter &&
                                 seen.insert( tag.Place.CharacterId ).second ) {
                                OTDU_LOG_INFO( "MapScreen: %*s depth %u name '%s'\n", level * 2, "", tag.Place.Depth,
                                               tag.Place.Name.c_str() );
                                walk( tag.Place.CharacterId, level + 1 );
                            }
                        }
                    }
                }
            };
            for ( const char *pChild : { "/fleche", "/locator" } ) {
                if ( const FlashPlayer::Clip *pClip = player.findClip( path + pChild ) ) {
                    OTDU_LOG_INFO( "MapScreen: --- %s%s ---\n", path.c_str(), pChild );
                    walk( pClip->pInstance != nullptr ? pClip->pInstance->CharacterId : 0, 0 );
                }
            }
        }
        player.setClipVisible( path + "/fleche", false );
        player.setClipVisible( path + "/locator", false );
        // NOTE: the status children (news / shape / glow / target / hlight /
        // link / rank / lock / forbidden / favorite / done / offroad) stay at
        // their authored state: 'shape' IS the tile art and the offroad child
        // draws the bottom-left tab - hiding them strips the icon (verified
        // -vs- the session 25 grab). The PS3 resetIconStatus protocol needs
        // the spots.bin status fields first [INF].
        made++;
    }
    // The template itself stays hidden and the layer sits at the stage origin:
    // the icons are placed in stage pixels.
    player.setClipVisible( "/pack/point", false );
    applySpotLod(); // the clones are built on zoom_default; the open level decides
    OTDU_LOG_INFO( "MapScreen: %d spot icons created (%d undiscovered hidden)\n", made,
                   static_cast<int32_t>( spots.size() ) - made );
}

// The discovery radius [INF: the retail range is not decoded - 150 m covers
// the spot's forecourt from the road, the distance the enter prompt appears
// at]. Retail's own seeding of KnownSpots (what a fresh profile knows) is not
// decoded either; the port starts empty and auto-knows owned houses and the
// home spots (loadSpots).
constexpr float kSpotDiscoverRadius = 150.0f;

int32_t MapScreen::discoverNear( const Vector3f &position ) {
    if ( spots.empty() || spotsIsland.empty() ) {
        return 0;
    }
    const float radiusSq = kSpotDiscoverRadius * kSpotDiscoverRadius;
    int32_t found = 0;
    PlayerProfile &profile = GetPlayerProfile();
    std::vector<uint64_t> &known = profile.KnownSpots[spotsIsland];
    for ( size_t i = 0; i < spots.size(); i++ ) {
        if ( i < spotDiscovered.size() && spotDiscovered[i] != 0 ) {
            continue;
        }
        const Vector3f delta = spots[i].Position - position;
        if ( delta.x * delta.x + delta.z * delta.z <= radiusSq ) {
            spotDiscovered[i] = 1;
            known.push_back( spots[i].Id );
            found++;
            OTDU_LOG_INFO( "MapScreen: discovered '%s' (%s)\n", spots[i].Name.c_str(), spots[i].pIcon );
        }
    }
    if ( found > 0 ) {
        SaveProfile( "map: spots discovered" );
    }
    return found;
}

void MapScreen::unlockAllSpots() {
    // The dev menu's "ALL SPOTS" (the KnownSpots twin of unlockAllRoads).
    if ( spots.empty() || spotsIsland.empty() ) {
        return;
    }
    PlayerProfile &profile = GetPlayerProfile();
    std::vector<uint64_t> &known = profile.KnownSpots[spotsIsland];
    for ( size_t i = 0; i < spots.size(); i++ ) {
        if ( i < spotDiscovered.size() && spotDiscovered[i] == 0 ) {
            spotDiscovered[i] = 1;
            known.push_back( spots[i].Id );
        }
    }
    OTDU_LOG_INFO( "MapScreen: all %d spots discovered\n", static_cast<int32_t>( spots.size() ) );
}

int32_t MapScreen::getNumDiscovered() const {
    int32_t count = 0;
    for ( size_t i = 0; i < spots.size() && i < spotDiscovered.size(); i++ ) {
        count += spotDiscovered[i] != 0 ? 1 : 0;
    }
    return count;
}

void MapScreen::applySpotLod() {
    // The exe drives one frame label per zoom step on every point clone:
    // zoom_default / zoom_lod / dezoom_lod / dezoom_lod2 [CODE: the exe's
    // strings, docs/STATE.md session 25] - the movie art carries the matching
    // labels on the clone timeline (buildSpots used to stop at zoom_default,
    // so every icon stayed full-size at every zoom - the owner's "icons too
    // big" on the whole-island view). Level 0 is the furthest view.
    if ( !bSpotsBuilt ) {
        return;
    }
    static const char *const kLodLabels[kNumZoomLevels] = { "dezoom_lod2", "dezoom_lod", "zoom_lod", "zoom_default" };
    const char *pLabel = kLodLabels[Clamp( zoomLevel, 0, kNumZoomLevels - 1 )];
    for ( const Spot &spot : spots ) {
        player.gotoLabel( "/pack/" + spot.Clip, pLabel, false );
    }
}

void MapScreen::updateSpots() {
    // The tab help bubble (presets/ttip, shown by showHsys after a tab change) is
    // not in the retail grabs [INF: hidden].
    player.setClipVisible( "/presets/ttip", false );
    if ( openTicks % 30 == 0 ) {
        player.invoke( "/presets", "showHsys", { "0" } );
    }
    player.setVariableNumber( "/pack:_x", 0.0 );
    player.setVariableNumber( "/pack:_y", 0.0 );
    player.setVariableNumber( "/pack:_xscale", 100.0 );
    player.setVariableNumber( "/pack:_yscale", 100.0 );
    const bool bBackdrop = !isUiShown();
    for ( const Spot &spot : spots ) {
        const std::string path = "/pack/" + spot.Clip;
        float x, y;
        const bool bShow = !bBackdrop && !bContest && spot.Tab == currentTab && projectToStage( spot.Position, x, y ) && x > -80.0f &&
                           x < 1360.0f && y > 40.0f && y < 920.0f;
        player.setClipVisible( path, bShow );
        if ( bShow ) {
            player.setVariableNumber( path + ":_x", x );
            player.setVariableNumber( path + ":_y", y );
        }
    }
    // The championship's race pins: the selected one wears the target ring.
    for ( size_t i = 0; i < contestPins.size() && bContest; i++ ) {
        if ( contestPins[i].empty() ) {
            continue;
        }
        const std::string path = "/pack/" + contestPins[i];
        float x, y;
        const bool bShow = !bBackdrop && projectToStage( contest.Races[i].Position, x, y );
        player.setClipVisible( path, bShow );
        if ( bShow ) {
            player.setVariableNumber( path + ":_x", x );
            player.setVariableNumber( path + ":_y", y );
        }
        if ( static_cast<int32_t>( i ) == contestSelected && contestPinLit != contestSelected ) {
            if ( contestPinLit >= 0 && static_cast<size_t>( contestPinLit ) < contestPins.size() && !contestPins[contestPinLit].empty() ) {
                player.gotoLabel( "/pack/" + contestPins[contestPinLit] + "/spot/target", "default", false );
            }
            player.gotoLabel( path + "/spot/target", "loop", true );
            contestPinLit = contestSelected;
        }
    }
}

bool MapScreen::projectToStage( const Vector3f &world, float &outX, float &outY ) const {
    if ( isFlatLevel() ) {
        outX = kMapWidth * 0.5f + ( world.x - center.x ) * mapScale;
        outY = 120.0f + kMapHeight * 0.5f + ( world.z - center.z ) * mapScale;
        return true;
    }
    // The tilted 3D camera of getCamera(): eye behind and above the centre,
    // looking at it; the authored 60 degree vertical lens (the map pins its
    // FOV scale) with the retail narrow-aspect law, projected through the
    // stage rect the movie draws with (FlashStageRect: markers land on their
    // spots at every window aspect and UI layout).
    const Vector3f forwardH( sinf( yaw ), 0.0f, cosf( yaw ) );
    const Vector3f eye = center - forwardH * ( cosf( pitch ) * distance ) + Vector3f( 0.0f, sinf( pitch ) * distance, 0.0f );
    const Vector3f f = ( center - eye ).normalized();
    const Vector3f r = f.cross( Vector3f( 0.0f, 1.0f, 0.0f ) ).normalized();
    const Vector3f u = r.cross( f );
    const Vector3f d = world - eye;
    const float z = d.dot( f );
    if ( z < 1.0f ) {
        return false;
    }
    float windowWidth = 1280.0f, windowHeight = 720.0f;
    if ( gpRender != nullptr ) {
        windowWidth = static_cast<float>( gpRender->getDevice().getBackbufferWidth() );
        windowHeight = static_cast<float>( gpRender->getDevice().getBackbufferHeight() );
    }
    const float aspect = windowWidth / Max( 1.0f, windowHeight );
    const float tanHalf = tanf( EdenEffectiveFov( 60.0f * kDegToRad, 1.0f, aspect ) * 0.5f );
    const float ndcX = d.dot( r ) / ( z * tanHalf * aspect );
    const float ndcY = d.dot( u ) / ( z * tanHalf );
    const float fx = ( ndcX + 1.0f ) * 0.5f;
    const float fy = ( 1.0f - ndcY ) * 0.5f;
    float sx0 = 0.0f, sy0 = 0.0f, sw = 1.0f, sh = 1.0f;
    FlashStageRect( GetEffectiveScreenRatio(), 1280.0f, 960.0f, windowWidth, windowHeight, sx0, sy0, sw, sh );
    outX = ( ( fx - sx0 ) / Max( sw, 1e-6f ) ) * kMapWidth;
    outY = ( ( fy - sy0 ) / Max( sh, 1e-6f ) ) * 960.0f;
    return true;
}

void MapScreen::getTransitionCamera( EdenSceneView::Camera &outCamera ) const {
    EdenSceneView::Camera mapCamera;
    getCamera( mapCamera );
    // smoothstep: the camera leaves the car and settles on the map gently
    const float t = transition * transition * ( 3.0f - 2.0f * transition );
    outCamera = mapCamera;
    outCamera.Position = startCamera.Position + ( mapCamera.Position - startCamera.Position ) * t;
    Quaternionf a = startCamera.Rotation, b = mapCamera.Rotation;
    if ( a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f ) {
        b = Quaternionf( -b.x, -b.y, -b.z, -b.w ); // shortest way round
    }
    Quaternionf q( a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t, a.w + ( b.w - a.w ) * t );
    const float len = sqrtf( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );
    outCamera.Rotation = len > 1e-6f ? Quaternionf( q.x / len, q.y / len, q.z / len, q.w / len ) : mapCamera.Rotation;
    outCamera.Near = startCamera.Near + ( mapCamera.Near - startCamera.Near ) * t;
    outCamera.Far = Max( startCamera.Far, mapCamera.Far );
}

float MapScreen::getFlatAlpha() const {
    if ( pIslandTexture == nullptr || zoomLevel >= 1 ) {
        return 0.0f;
    }
    const float t = Clamp( ( transition - 0.55f ) / 0.45f, 0.0f, 1.0f );
    return t * t * ( 3.0f - 2.0f * t );
}

Vector3f MapScreen::freeCursorWorld() const {
    if ( pIslandTexture != nullptr && zoomLevel == 0 ) {
        return Vector3f( center.x + ( cursorX - kMapWidth * 0.5f ) / mapScale, 0.0f,
                         center.z + ( cursorY - kMapHeight * 0.5f ) / mapScale );
    }
    return center;
}

Vector3f MapScreen::cursorWorld() const {
    // The point the reticle is on: the snapped shop / road when the cursor
    // stuck to one, the free cursor otherwise.
    if ( snapSpot >= 0 && static_cast<size_t>( snapSpot ) < spots.size() ) {
        return spots[snapSpot].Position;
    }
    if ( snapRoad >= 0 ) {
        return snapPoint;
    }
    return freeCursorWorld();
}

uint8_t MapScreen::getDirtClass() {
    // The road map's class byte is not decoded (docs/re/roads_spec.md): the
    // offroad slot of the retail [HudMap] table is the default, -dirtclass
    // switches it when the map shows the wrong roads in yellow.
    const int32_t arg = CmdLine::GetArgInt( "-dirtclass", kRoadClassDirt );
    return static_cast<uint8_t>( Clamp<int32_t>( arg, 0, 255 ) );
}

int32_t MapScreen::findRoadNear( const Vector3f &point, float maxDistance, Vector3f *pOutPoint,
                                 float *pOutAlong ) const {
    if ( roadEdges.empty() || maxDistance <= 0.0f ) {
        return -1;
    }
    const float maxDistanceSquared = maxDistance * maxDistance;
    int32_t best = -1;
    float bestDistance = maxDistanceSquared;
    Vector3f bestPoint = point;
    float bestAlong = 0.0f;
    for ( size_t e = 0; e < roadEdges.size(); e++ ) {
        // The link's bounding box culls the island in a few tests.
        const std::array<float, 4> &box = edgeBoxes[e];
        if ( point.x < box[0] - maxDistance || point.x > box[2] + maxDistance || point.z < box[1] - maxDistance ||
             point.z > box[3] + maxDistance ) {
            continue;
        }
        const std::vector<Vector3f> &shape = roadShapes[e];
        float walked = 0.0f;
        for ( size_t k = 0; k + 1 < shape.size(); k++ ) {
            const Vector3f &a = shape[k];
            const Vector3f &b = shape[k + 1];
            const float abx = b.x - a.x, abz = b.z - a.z;
            const float lengthSquared = abx * abx + abz * abz;
            const float segmentLength = sqrtf( lengthSquared );
            float t = lengthSquared > 0.0001f ? ( ( point.x - a.x ) * abx + ( point.z - a.z ) * abz ) / lengthSquared : 0.0f;
            t = Clamp( t, 0.0f, 1.0f );
            const float px = a.x + abx * t, pz = a.z + abz * t;
            const float dx = point.x - px, dz = point.z - pz;
            const float d = dx * dx + dz * dz;
            if ( d < bestDistance ) {
                bestDistance = d;
                best = static_cast<int32_t>( e );
                bestPoint = Vector3f( px, a.y + ( b.y - a.y ) * t, pz );
                bestAlong = walked + t * segmentLength;
            }
            walked += segmentLength;
        }
    }
    if ( best >= 0 ) {
        if ( pOutPoint != nullptr ) {
            *pOutPoint = bestPoint;
        }
        if ( pOutAlong != nullptr ) {
            *pOutAlong = bestAlong;
        }
    }
    return best;
}

int32_t MapScreen::findExploredRoadNear( const Vector3f &point, float maxDistance ) const {
    if ( roadEdges.empty() || maxDistance <= 0.0f ) {
        return -1;
    }
    const float maxDistanceSquared = maxDistance * maxDistance;
    int32_t best = -1;
    float bestDistance = maxDistanceSquared;
    for ( size_t e = 0; e < roadEdges.size(); e++ ) {
        if ( e >= edgeExplored.size() || edgeExplored[e] == 0 ) {
            continue;
        }
        const std::array<float, 4> &box = edgeBoxes[e];
        if ( point.x < box[0] - maxDistance || point.x > box[2] + maxDistance || point.z < box[1] - maxDistance ||
             point.z > box[3] + maxDistance ) {
            continue;
        }
        const std::vector<Vector3f> &shape = roadShapes[e];
        for ( size_t k = 0; k + 1 < shape.size(); k++ ) {
            const Vector3f &a = shape[k];
            const Vector3f &b = shape[k + 1];
            const float abx = b.x - a.x, abz = b.z - a.z;
            const float lengthSquared = abx * abx + abz * abz;
            float t = lengthSquared > 0.0001f ? ( ( point.x - a.x ) * abx + ( point.z - a.z ) * abz ) / lengthSquared : 0.0f;
            t = Clamp( t, 0.0f, 1.0f );
            const float dx = point.x - ( a.x + abx * t ), dz = point.z - ( a.z + abz * t );
            const float d = dx * dx + dz * dz;
            if ( d < bestDistance ) {
                bestDistance = d;
                best = static_cast<int32_t>( e );
            }
        }
    }
    return best;
}

void MapScreen::loadRoads( const std::string &island ) {
    roadNodes.clear();
    roadNodeKinds.clear();
    roadLinks.clear();
    roadEdges.clear();
    bAreasBuilt = false;
    pendingEvents.clear();
    roadShapes.clear();
    edgeLengths.clear();
    edgeBoxes.clear();
    edgeExplored.clear();
    edgeClass.clear();
    exploredLength = totalRoadLength = 0.0f;
    roadIsland = island;
    bHasTarget = false;
    routePoints.clear();
    routeAlong.clear();
    routeTurns.clear();
    // islands/<island>/level/<island>.bnk holds <Island>_ROAD_MAP.IA [DATA]:
    // 'RI41DAEH' header, then 'EDON' node records (40 bytes: +4 id (1-based),
    // +12 x, +16 y, +20 z floats) and 'DAOR' road records (48 bytes: +28 and +32
    // the two node ids). 856 nodes / 1392 roads on Ibiza; the road links resolve
    // for all but 42 (those have 0 ids) [DATA: checked on the island texture].
    Bank bank;
    const std::string bankPath = gpFile->getBankFolder() + "islands/" + island + "/level/" + island + ".bnk";
    if ( !bank.load( bankPath.c_str() ) ) {
        return;
    }
    const BankEntry *pEntry = nullptr;
    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( entry.Extension == "ia" && StringNCmpI( entry.Name.c_str(), ( island + "_ROAD_MAP" ).c_str(), island.size() + 9 ) == 0 ) {
            pEntry = &entry;
        }
    }
    if ( pEntry == nullptr ) {
        return;
    }
    // <Island>_ROAD_DETAIL.IA: per road an 'ATED' record whose sub-blocks hold
    // the curve as float triples (x, y, z), 0 separators [DATA: the first and
    // last points land within ~10 m of the road's two nodes, checked on Ibiza].
    const BankEntry *pDetail = nullptr;
    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( entry.Extension == "ia" && StringNCmpI( entry.Name.c_str(), ( island + "_ROAD_DETAIL" ).c_str(), island.size() + 12 ) == 0 ) {
            pDetail = &entry;
        }
    }
    std::map<uint32_t, std::vector<Vector3f>> detail;
    if ( pDetail != nullptr ) {
        const uint8_t *pD = pDetail->pData;
        const size_t n = pDetail->Size;
        auto u32 = [&]( size_t offset ) {
            uint32_t v = 0;
            memcpy( &v, pD + offset, 4 );
            return v;
        };
        auto f32 = [&]( size_t offset ) {
            float v = 0.0f;
            memcpy( &v, pD + offset, 4 );
            return v;
        };
        std::vector<size_t> records;
        size_t firstOther = n;
        for ( size_t i = 0; i + 16 <= n; i += 4 ) {
            if ( memcmp( pD + i, "ATED", 4 ) == 0 ) {
                records.push_back( i );
            } else if ( firstOther == n && ( memcmp( pD + i, "FTED", 4 ) == 0 || memcmp( pD + i, "ETED", 4 ) == 0 ) ) {
                firstOther = i;
            }
        }
        for ( size_t r = 0; r < records.size(); r++ ) {
            const size_t start = records[r];
            const size_t recordEnd = r + 1 < records.size() ? records[r + 1] : firstOther;
            const uint32_t rid = u32( start + 4 ), count = u32( start + 8 );
            if ( count == 0 || count > 64 || start + 12 + 4 * count > recordEnd ) {
                continue;
            }
            std::vector<Vector3f> &points = detail[rid];
            for ( uint32_t k = 0; k < count; k++ ) {
                const size_t blockStart = start + u32( start + 12 + 4 * k );
                const size_t blockEnd = k + 1 < count ? start + u32( start + 12 + 4 * ( k + 1 ) ) : recordEnd;
                if ( blockEnd > recordEnd || blockStart + 20 > blockEnd ) {
                    continue;
                }
                for ( size_t j = blockStart + 20; j + 12 <= blockEnd; ) {
                    const float x = f32( j ), y = f32( j + 4 ), z = f32( j + 8 );
                    // A stray float triple (not a point of the curve) jumps far from the previous point.
                    const bool bPlausible = fabsf( x ) > 100.0f && fabsf( x ) < 1e5f && fabsf( z ) > 100.0f && fabsf( z ) < 1e5f &&
                                            fabsf( y ) < 5000.0f &&
                                            ( points.empty() || ( ( x - points.back().x ) * ( x - points.back().x ) +
                                                                   ( z - points.back().z ) * ( z - points.back().z ) ) < 250.0f * 250.0f );
                    if ( bPlausible ) {
                        points.emplace_back( x, y, z );
                        j += 12;
                    } else {
                        j += 4;
                    }
                }
            }
        }
    }
    const uint8_t *pData = pEntry->pData;
    const size_t size = pEntry->Size;
    auto readU32 = [&]( size_t offset ) {
        uint32_t v = 0;
        memcpy( &v, pData + offset, 4 );
        return v;
    };
    auto readF32 = [&]( size_t offset ) {
        float v = 0.0f;
        memcpy( &v, pData + offset, 4 );
        return v;
    };
    std::vector<size_t> roadOffsets;
    for ( size_t i = 0; i + 40 <= size; i++ ) {
        if ( memcmp( pData + i, "EDON", 4 ) == 0 ) {
            const uint32_t id = readU32( i + 4 );
            const float x = readF32( i + 12 ), z = readF32( i + 20 );
            if ( id < 100000 && fabsf( x ) < 1e6f && fabsf( z ) < 1e6f ) {
                if ( roadNodes.size() <= id ) {
                    roadNodes.resize( id + 1, Vector3f( -1.0f, -1.0f, -1.0f ) );
                    roadNodeKinds.resize( id + 1, 0 );
                }
                roadNodes[id] = Vector3f( x, 0.0f, z );
                // +8 the link count, +10 the node kind: 3 on the multi-exit
                // nodes of the towns (Ibiza: 11 nodes of 4..7 links) = the
                // roundabouts the GPS arrow tests [DATA + CODE].
                roadNodeKinds[id] = pData[i + 10];
            }
        } else if ( memcmp( pData + i, "DAOR", 4 ) == 0 && i + 48 <= size ) {
            roadOffsets.push_back( i );
        }
    }
    roadLinks.resize( roadNodes.size() );
    int32_t links = 0;
    for ( const size_t offset : roadOffsets ) {
        const uint32_t a = readU32( offset + 28 ), b = readU32( offset + 32 );
        if ( a == b || a >= roadNodes.size() || b >= roadNodes.size() || roadNodes[a].x < 0.0f || roadNodes[b].x < 0.0f ) {
            continue;
        }
        roadEdges.emplace_back( static_cast<int32_t>( a ), static_cast<int32_t>( b ) );
        // The curve from a to b, closed on the nodes (their height is the curve's).
        std::vector<Vector3f> shape;
        const auto found = detail.find( readU32( offset + 4 ) );
        if ( found != detail.end() && found->second.size() >= 2 ) {
            std::vector<Vector3f> points = found->second;
            auto flat = [&]( const Vector3f &p, const Vector3f &q ) { return ( p.x - q.x ) * ( p.x - q.x ) + ( p.z - q.z ) * ( p.z - q.z ); };
            if ( flat( points.front(), roadNodes[a] ) > flat( points.front(), roadNodes[b] ) ) {
                std::reverse( points.begin(), points.end() );
            }
            shape.emplace_back( roadNodes[a].x, points.front().y, roadNodes[a].z );
            shape.insert( shape.end(), points.begin(), points.end() );
            shape.emplace_back( roadNodes[b].x, points.back().y, roadNodes[b].z );
        } else {
            shape = { roadNodes[a], roadNodes[b] };
        }
        float shapeLength = 0.0f;
        std::array<float, 4> box = { 1e30f, 1e30f, -1e30f, -1e30f };
        for ( size_t k = 0; k < shape.size(); k++ ) {
            if ( k > 0 ) {
                shapeLength += sqrtf( ( shape[k].x - shape[k - 1].x ) * ( shape[k].x - shape[k - 1].x ) +
                                      ( shape[k].z - shape[k - 1].z ) * ( shape[k].z - shape[k - 1].z ) );
            }
            box = { std::min( box[0], shape[k].x ), std::min( box[1], shape[k].z ), std::max( box[2], shape[k].x ), std::max( box[3], shape[k].z ) };
        }
        // The route search weighs a link by the length of its curve.
        roadLinks[a].push_back( { static_cast<int32_t>( b ), shapeLength } );
        roadLinks[b].push_back( { static_cast<int32_t>( a ), shapeLength } );
        roadShapes.push_back( std::move( shape ) );
        edgeLengths.push_back( shapeLength );
        edgeBoxes.push_back( box );
        // The road's class (the byte at 'DAOR' +12): the map colours a road by
        // it [DATA: 0..4 on Ibiza, 0..11 on Hawai; docs/re/roads_spec.md].
        edgeClass.push_back( pData[offset + 12] );
        totalRoadLength += shapeLength;
        links++;
    }
    // The saved explored links of this island (profile.sav, ids = edge index).
    edgeExplored.assign( roadEdges.size(), 0 );
    const auto saved = GetPlayerProfile().ExploredRoads.find( island );
    if ( saved != GetPlayerProfile().ExploredRoads.end() ) {
        for ( const uint32_t id : saved->second ) {
            if ( id < edgeExplored.size() && edgeExplored[id] == 0 ) {
                edgeExplored[id] = 1;
                exploredLength += edgeLengths[id];
            }
        }
    }
    edgeBetween.clear();
    for ( size_t e = 0; e < roadEdges.size(); e++ ) {
        for ( const auto &key : { roadEdges[e], std::make_pair( roadEdges[e].second, roadEdges[e].first ) } ) {
            const auto found = edgeBetween.find( key );
            if ( found == edgeBetween.end() || edgeLengths[e] < edgeLengths[found->second] ) {
                edgeBetween[key] = static_cast<int32_t>( e );
            }
        }
    }
    OTDU_LOG_INFO( "MapScreen: road graph %zu nodes, %d links\n", roadNodes.size(), links );
    // The class histogram: the road map's class byte is not decoded yet (which
    // value is a dirt track), so -logroads prints it for a comparison with the
    // map's colours.
    if ( CmdLine::HasArg( "-logroads" ) ) {
        std::map<uint8_t, int32_t> histogram;
        for ( const uint8_t c : edgeClass ) {
            histogram[c]++;
        }
        std::string line;
        for ( const auto &entry : histogram ) {
            line += " " + std::to_string( static_cast<int32_t>( entry.first ) ) + ":" + std::to_string( entry.second );
        }
        OTDU_LOG_INFO( "MapScreen: road classes (DAOR +12)%s; dirt class %d (-dirtclass overrides)\n", line.c_str(),
                       static_cast<int32_t>( getDirtClass() ) );
    }
}

void MapScreen::buildAreas() {
    const CruisingZones &zones = GetCruisingZones();
    if ( zones.getCount() <= 0 || zones.getIsland() != roadIsland || roadEdges.empty() ) {
        return;
    }
    bAreasBuilt = true;
    const size_t count = static_cast<size_t>( zones.getCount() );
    edgeArea.assign( roadEdges.size(), -1 );
    areaTotal.assign( count, 0.0f );
    areaExplored.assign( count, 0.0f );
    areaAwarded.assign( count, { false, false, false, false, false } );
    for ( size_t i = 0; i < roadEdges.size(); i++ ) {
        const Vector3f &middle = roadShapes[i][roadShapes[i].size() / 2];
        edgeArea[i] = zones.find( middle.x, middle.z );
        if ( edgeArea[i] >= 0 ) {
            areaTotal[edgeArea[i]] += edgeLengths[i];
            if ( edgeExplored[i] != 0 ) {
                areaExplored[edgeArea[i]] += edgeLengths[i];
            }
        }
    }
    // Progress loaded from the save was awarded before: only new milestones pay.
    static const float kMilestones[5] = { 10.0f, 25.0f, 50.0f, 75.0f, 100.0f };
    for ( size_t a = 0; a < count; a++ ) {
        const float percent = areaTotal[a] > 1.0f ? 100.0f * areaExplored[a] / areaTotal[a] : 0.0f;
        for ( int32_t m = 0; m < 5; m++ ) {
            areaAwarded[a][m] = percent >= kMilestones[m];
        }
        OTDU_LOG_INFO( "MapScreen: area %zu: %.1f km of roads, %.0f %% explored\n", a + 1, areaTotal[a] / 1000.0f, percent );
    }
}

void MapScreen::checkAreaMilestones( int32_t area ) {
    static const float kMilestones[5] = { 10.0f, 25.0f, 50.0f, 75.0f, 100.0f };
    const float percent = areaTotal[area] > 1.0f ? 100.0f * areaExplored[area] / areaTotal[area] : 0.0f;
    for ( int32_t m = 0; m < 5; m++ ) {
        if ( !areaAwarded[area][m] && percent >= kMilestones[m] - 0.001f ) {
            areaAwarded[area][m] = true;
            std::string island = roadIsland;
            for ( char &c : island ) {
                c = static_cast<char>( toupper( static_cast<unsigned char>( c ) ) );
            }
            pendingEvents.push_back( "CRUISING_" + island + "_ROADS_AREA" + std::to_string( area + 1 ) + "_" +
                                     std::to_string( static_cast<int32_t>( kMilestones[m] ) ) );
        }
    }
}

std::vector<std::string> MapScreen::takeEvents() {
    std::vector<std::string> events;
    events.swap( pendingEvents );
    return events;
}

float MapScreen::trackCar( const Vector3f &position, float deltaTime ) {
    if ( roadEdges.empty() ) {
        return 0.0f;
    }
    if ( !bAreasBuilt ) {
        buildAreas();
    }
    trackTimer += deltaTime;
    if ( trackTimer < 0.25f ) {
        return 0.0f;
    }
    trackTimer = 0.0f;
    // A link is explored when the car is within kExploreRadius of its segment
    // [INF: a lane and the road verge, ~one road width].
    constexpr float kExploreRadius = 18.0f;
    float newLength = 0.0f;
    for ( size_t i = 0; i < roadEdges.size(); i++ ) {
        if ( edgeExplored[i] != 0 ) {
            continue;
        }
        const std::array<float, 4> &box = edgeBoxes[i];
        if ( position.x < box[0] - kExploreRadius || position.x > box[2] + kExploreRadius || position.z < box[1] - kExploreRadius ||
             position.z > box[3] + kExploreRadius ) {
            continue;
        }
        // Distance to the curve of the link (its segments).
        const std::vector<Vector3f> &shape = roadShapes[i];
        bool bNear = false;
        for ( size_t k = 0; k + 1 < shape.size() && !bNear; k++ ) {
            const Vector3f &a = shape[k];
            const Vector3f &b = shape[k + 1];
            const float abx = b.x - a.x, abz = b.z - a.z;
            const float lengthSquared = abx * abx + abz * abz;
            float t = lengthSquared > 0.0001f ? ( ( position.x - a.x ) * abx + ( position.z - a.z ) * abz ) / lengthSquared : 0.0f;
            t = Clamp( t, 0.0f, 1.0f );
            const float dx = position.x - ( a.x + abx * t ), dz = position.z - ( a.z + abz * t );
            bNear = dx * dx + dz * dz < kExploreRadius * kExploreRadius;
        }
        if ( bNear ) {
            edgeExplored[i] = 1;
            newLength += edgeLengths[i];
            GetPlayerProfile().ExploredRoads[roadIsland].push_back( static_cast<uint32_t>( i ) );
            if ( bAreasBuilt && edgeArea[i] >= 0 ) {
                areaExplored[edgeArea[i]] += edgeLengths[i];
                checkAreaMilestones( edgeArea[i] );
            }
        }
    }
    exploredLength += newLength;
    return newLength;
}

int32_t MapScreen::getExploredPercent() const {
    return totalRoadLength > 1.0f ? static_cast<int32_t>( 100.0f * exploredLength / totalRoadLength ) : 0;
}

int32_t MapScreen::getNumExploredRoads() const {
    int32_t count = 0;
    for ( uint8_t explored : edgeExplored ) {
        if ( explored != 0 ) {
            count++;
        }
    }
    return count;
}

void MapScreen::unlockAllRoads() {
    // The retail dev menu's "ALL ROADS": mark every link of the loaded graph
    // explored. The per-area counters move with it, so the milestone events the
    // areas award fire exactly like driving the roads would (that is the point
    // of the cheat: testing the road / Discovery features).
    if ( roadEdges.empty() || edgeExplored.empty() ) {
        OTDU_LOG_WARN( "MapScreen: all roads: no road graph loaded\n" );
        return;
    }
    const int32_t before = getNumExploredRoads();
    float newLength = 0.0f;
    for ( size_t i = 0; i < edgeExplored.size(); i++ ) {
        if ( edgeExplored[i] != 0 ) {
            continue;
        }
        edgeExplored[i] = 1;
        newLength += edgeLengths[i];
        GetPlayerProfile().ExploredRoads[roadIsland].push_back( static_cast<uint32_t>( i ) );
        if ( bAreasBuilt && edgeArea[i] >= 0 ) {
            areaExplored[edgeArea[i]] += edgeLengths[i];
            checkAreaMilestones( edgeArea[i] );
        }
    }
    exploredLength += newLength;
    OTDU_LOG_INFO( "MapScreen: all roads unlocked: %d -> %d of %d link(s), %.0f m, %d%%\n", before,
                   getNumExploredRoads(), getNumRoadEdges(), exploredLength, getExploredPercent() );
}

bool MapScreen::targetNearestUnexplored( const Vector3f &from ) {
    // "TARGET NEAREST LOCKED ROAD": the destination is the closest road node
    // that carries a link the player has not driven, so the debug route leads
    // to a road the map still shows locked.
    int32_t best = -1;
    float bestDistance = 1e30f;
    for ( size_t i = 0; i < roadEdges.size() && i < edgeExplored.size(); i++ ) {
        if ( edgeExplored[i] != 0 ) {
            continue;
        }
        const int32_t nodes[2] = { roadEdges[i].first, roadEdges[i].second };
        for ( const int32_t node : nodes ) {
            if ( node < 0 || node >= static_cast<int32_t>( roadNodes.size() ) ||
                 roadNodes[node].x < 0.0f ) {
                continue; // the -1 filler nodes of the graph
            }
            const float dx = roadNodes[node].x - from.x, dz = roadNodes[node].z - from.z;
            const float distance = dx * dx + dz * dz;
            if ( distance < bestDistance ) {
                bestDistance = distance;
                best = node;
            }
        }
    }
    if ( best < 0 ) {
        OTDU_LOG_INFO( "MapScreen: no locked road left on '%s'\n", roadIsland.c_str() );
        return false;
    }
    setTarget( roadNodes[best] );
    OTDU_LOG_INFO( "MapScreen: nearest locked road: node %d, %.0f m away (route %.0f m)\n", best,
                   sqrtf( bestDistance ), getRouteLength() );
    return true;
}

bool MapScreen::isCursorValid() const {
    // The reticle's own two states (the movie's /:IsTargetOK): a snapped shop
    // or road is a target; without a snap the loose land test below keeps the
    // "no" sign off the middle of the island (2.5 km of a road node).
    if ( snapSpot >= 0 || snapRoad >= 0 ) {
        return true;
    }
    const Vector3f under = freeCursorWorld();
    for ( const Vector3f &node : roadNodes ) {
        if ( node.x >= 0.0f && fabsf( node.x - under.x ) < 2500.0f && fabsf( node.z - under.z ) < 2500.0f &&
             ( node - under ).length() < 2500.0f ) {
            return true;
        }
    }
    return false;
}

bool MapScreen::getFastTravelPoint( Vector3f &outPoint ) const {
    if ( !isOpen() || !isUiShown() || roadEdges.empty() ) {
        return false;
    }
    // The retail only travels to a road that has been discovered: a shop goes
    // through the road in front of it, a road through itself, and a free cursor
    // through the road nearest to it.
    if ( snapRoad >= 0 ) {
        if ( !isRoadExplored( snapRoad ) ) {
            return false;
        }
        outPoint = snapPoint;
        return true;
    }
    const Vector3f point = cursorWorld();
    const bool bSpot = snapSpot >= 0;
    if ( findExploredRoadNear( point, bSpot ? 120.0f : 60.0f ) < 0 ) {
        return false;
    }
    outPoint = point;
    return true;
}

void MapScreen::toggleTarget() {
    if ( roadNodes.empty() || !isCursorValid() ) {
        return; // the cursor shows the "no" sign: nothing to route to
    }
    // The point the reticle is on: a snapped shop's position, the point of the
    // snapped road, or the free cursor.
    const Vector3f point = cursorWorld();
    // The route ends on a road node: the destination snaps to the nearest one.
    int32_t best = -1;
    float bestDistance = 1e30f;
    for ( size_t i = 0; i < roadNodes.size(); i++ ) {
        if ( roadNodes[i].x < 0.0f ) {
            continue;
        }
        const float d = ( roadNodes[i] - point ).length();
        if ( d < bestDistance ) {
            bestDistance = d;
            best = static_cast<int32_t>( i );
        }
    }
    if ( best < 0 ) {
        return;
    }
    // OK on the destination again clears it (a snapped shop can be a few tens
    // of metres from the node the route ends on, so the test is 30 m).
    if ( bHasTarget && ( roadNodes[best] - target ).length() < 30.0f ) {
        bHasTarget = false;
        routePoints.clear();
        routeAlong.clear();
        routeTurns.clear();
        OTDU_LOG_INFO( "MapScreen: destination cleared\n" );
        return;
    }
    target = roadNodes[best];
    bHasTarget = true;
    computeRoute();
    OTDU_LOG_INFO( "MapScreen: destination %s at (%.0f, %.0f), %.0f m from the cursor, route %.0f m\n",
                   snapSpot >= 0 ? "on a shop" : ( snapRoad >= 0 ? "on a road" : "on the map" ), target.x, target.z,
                   bestDistance, routeLength );
}

void MapScreen::computeRoute() {
    routePoints.clear();
    routeAlong.clear();
    routeTurns.clear();
    routeLength = 0.0f;
    if ( !bHasTarget || roadNodes.empty() ) {
        return;
    }
    auto nearest = [&]( const Vector3f &p ) {
        int32_t best = -1;
        float bestDistance = 1e30f;
        for ( size_t i = 0; i < roadNodes.size(); i++ ) {
            if ( roadNodes[i].x < 0.0f || roadLinks[i].empty() ) {
                continue;
            }
            const float d = ( roadNodes[i] - p ).length();
            if ( d < bestDistance ) {
                bestDistance = d;
                best = static_cast<int32_t>( i );
            }
        }
        return best;
    };
    const int32_t to = nearest( target );
    if ( to < 0 ) {
        return;
    }
    // The search starts on the link nearest to the car (its curve), from both
    // of its nodes at the distance along the curve: the node nearest to the car
    // may be on another road (a bridge over it, a parallel street) and send the
    // route around [INF: the retail GPS start is in the encrypted band].
    int32_t startEdge = -1;
    size_t startSegment = 0;
    float startAlong = 0.0f;
    Vector3f startPoint = playerPosition;
    {
        constexpr float kSearch = 400.0f;
        float bestDistance = kSearch * kSearch;
        for ( size_t e = 0; e < roadEdges.size(); e++ ) {
            const std::array<float, 4> &box = edgeBoxes[e];
            if ( playerPosition.x < box[0] - kSearch || playerPosition.x > box[2] + kSearch ||
                 playerPosition.z < box[1] - kSearch || playerPosition.z > box[3] + kSearch ) {
                continue;
            }
            const std::vector<Vector3f> &shape = roadShapes[e];
            float walked = 0.0f;
            for ( size_t k = 0; k + 1 < shape.size(); k++ ) {
                const Vector3f &a = shape[k];
                const Vector3f &b = shape[k + 1];
                const float abx = b.x - a.x, abz = b.z - a.z;
                const float lengthSquared = abx * abx + abz * abz;
                float t = lengthSquared > 0.0001f ? ( ( playerPosition.x - a.x ) * abx + ( playerPosition.z - a.z ) * abz ) / lengthSquared : 0.0f;
                t = Clamp( t, 0.0f, 1.0f );
                const float dx = playerPosition.x - ( a.x + abx * t ), dz = playerPosition.z - ( a.z + abz * t );
                const float d = dx * dx + dz * dz;
                const float segmentLength = std::sqrt( lengthSquared );
                if ( d < bestDistance ) {
                    bestDistance = d;
                    startEdge = static_cast<int32_t>( e );
                    startSegment = k;
                    startAlong = walked + t * segmentLength;
                    startPoint = Vector3f( a.x + abx * t, a.y + ( b.y - a.y ) * t, a.z + abz * t );
                }
                walked += segmentLength;
            }
        }
    }
    const int32_t from = startEdge < 0 ? nearest( playerPosition ) : -1;
    if ( startEdge < 0 && from < 0 ) {
        return;
    }
    // Dijkstra over the road graph
    std::vector<float> dist( roadNodes.size(), 1e30f );
    std::vector<int32_t> previous( roadNodes.size(), -1 );
    using Item = std::pair<float, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    if ( startEdge >= 0 ) {
        const int32_t a = roadEdges[startEdge].first, b = roadEdges[startEdge].second;
        dist[a] = startAlong;
        queue.push( { startAlong, a } );
        const float toB = Max( 0.0f, edgeLengths[startEdge] - startAlong );
        if ( toB < dist[b] ) {
            dist[b] = toB;
            queue.push( { toB, b } );
        }
    } else {
        dist[from] = 0.0f;
        queue.push( { 0.0f, from } );
    }
    while ( !queue.empty() ) {
        const Item top = queue.top();
        queue.pop();
        if ( top.first > dist[top.second] ) {
            continue;
        }
        if ( top.second == to ) {
            break;
        }
        for ( const auto &link : roadLinks[top.second] ) {
            const float next = top.first + link.second;
            if ( next < dist[link.first] ) {
                dist[link.first] = next;
                previous[link.first] = top.second;
                queue.push( { next, link.first } );
            }
        }
    }
    std::vector<Vector3f> path;
    std::vector<std::pair<size_t, int32_t>> nodeAt; // (index in path, road node) of the junctions passed
    path.push_back( Vector3f( playerPosition.x, 0.0f, playerPosition.z ) );
    if ( dist[to] < 1e29f ) {
        std::vector<int32_t> nodes;
        for ( int32_t node = to; node >= 0; node = previous[node] ) {
            nodes.push_back( node );
        }
        std::reverse( nodes.begin(), nodes.end() );
        // From the car along its link to the first node.
        if ( startEdge >= 0 ) {
            const std::vector<Vector3f> &shape = roadShapes[startEdge];
            path.push_back( startPoint );
            if ( nodes.front() == roadEdges[startEdge].first ) {
                for ( size_t k = startSegment + 1; k-- > 0; ) {
                    path.push_back( shape[k] );
                }
            } else {
                for ( size_t k = startSegment + 1; k < shape.size(); k++ ) {
                    path.push_back( shape[k] );
                }
            }
        } else {
            path.push_back( roadNodes[nodes.front()] );
        }
        if ( nodes.size() > 1 ) {
            nodeAt.emplace_back( path.size() - 1, nodes.front() );
        }
        // Each step follows the curve of the shortest link between its two nodes.
        for ( size_t i = 0; i + 1 < nodes.size(); i++ ) {
            const auto found = edgeBetween.find( { nodes[i], nodes[i + 1] } );
            if ( found == edgeBetween.end() ) {
                path.push_back( roadNodes[nodes[i + 1]] );
            } else {
                const std::vector<Vector3f> &shape = roadShapes[found->second];
                if ( roadEdges[found->second].first == nodes[i] ) {
                    path.insert( path.end(), shape.begin() + 1, shape.end() );
                } else {
                    path.insert( path.end(), shape.rbegin() + 1, shape.rend() );
                }
            }
            if ( i + 2 < nodes.size() ) {
                nodeAt.emplace_back( path.size() - 1, nodes[i + 1] );
            }
        }
    }
    path.push_back( target );
    // Chaikin corner cutting: the graph has one node per junction, the retail
    // route is a smooth curve
    // (the real curves of the links need no smoothing)
    for ( int32_t pass = 0; pass < 0; pass++ ) {
        std::vector<Vector3f> smooth;
        smooth.push_back( path.front() );
        for ( size_t i = 0; i + 1 < path.size(); i++ ) {
            smooth.push_back( path[i] * 0.75f + path[i + 1] * 0.25f );
            smooth.push_back( path[i] * 0.25f + path[i + 1] * 0.75f );
        }
        smooth.push_back( path.back() );
        path.swap( smooth );
    }
    routePoints = path;
    routeLength = 0.0f;
    routeAlong.assign( path.size(), 0.0f );
    for ( size_t i = 0; i + 1 < path.size(); i++ ) {
        const float dx = path[i + 1].x - path[i].x, dz = path[i + 1].z - path[i].z;
        routeLength += std::sqrt( dx * dx + dz * dz );
        routeAlong[i + 1] = routeLength;
    }
    // The junctions: inner nodes with 3+ distinct neighbours, with the retail
    // junction angle and HUD arrow (FUN_00935fd0) [CODE]. Headings are taken
    // 15 m from the node (the curve points right at a junction are often a
    // metre apart) [INF]. The retail angle of a link is its heading minus the
    // heading of the link back to where the route comes from, 0..360; in the
    // port's frame that is 180 - the clockwise angle from the direction of
    // travel, right = (-forward.z, forward.x) (the minimap's convention).
    auto linkDirection = [&]( int32_t node, int32_t neighbour, Vector3f &outDirection ) {
        const auto found = edgeBetween.find( { node, neighbour } );
        if ( found == edgeBetween.end() ) {
            return false;
        }
        const std::vector<Vector3f> &shape = roadShapes[found->second];
        const bool bForward = roadEdges[found->second].first == node;
        const Vector3f origin = roadNodes[node];
        Vector3f point = bForward ? shape.back() : shape.front();
        for ( size_t k = 0; k < shape.size(); k++ ) {
            const Vector3f &candidate = bForward ? shape[k] : shape[shape.size() - 1 - k];
            const float dx = candidate.x - origin.x, dz = candidate.z - origin.z;
            if ( dx * dx + dz * dz >= 15.0f * 15.0f ) {
                point = candidate;
                break;
            }
        }
        const Vector3f direction( point.x - origin.x, 0.0f, point.z - origin.z );
        if ( direction.length() < 1e-3f ) {
            return false;
        }
        outDirection = direction.normalized();
        return true;
    };
    for ( const auto &at : nodeAt ) {
        const size_t index = at.first;
        if ( index == 0 || index + 1 >= path.size() ) {
            continue;
        }
        std::vector<int32_t> neighbours;
        for ( const auto &link : roadLinks[at.second] ) {
            if ( std::find( neighbours.begin(), neighbours.end(), link.first ) == neighbours.end() ) {
                neighbours.push_back( link.first );
            }
        }
        if ( neighbours.size() < 3 ) {
            continue;
        }
        const float along = routeAlong[index];
        size_t before = index, after = index;
        while ( before > 0 && along - routeAlong[before] < 15.0f ) {
            before--;
        }
        while ( after + 1 < path.size() && routeAlong[after] - along < 15.0f ) {
            after++;
        }
        const Vector3f in( path[index].x - path[before].x, 0.0f, path[index].z - path[before].z );
        const Vector3f out( path[after].x - path[index].x, 0.0f, path[after].z - path[index].z );
        if ( in.length() < 1e-3f || out.length() < 1e-3f ) {
            continue;
        }
        const Vector3f inDir = in.normalized(), outDir = out.normalized();
        const Vector3f right( -inDir.z, 0.0f, inDir.x );
        auto retailAngle = [&]( const Vector3f &direction ) {
            float angle = 180.0f - std::atan2( direction.dot( right ), direction.dot( inDir ) ) * 57.29578f;
            while ( angle < 0.0f ) {
                angle += 360.0f;
            }
            while ( angle >= 360.0f ) {
                angle -= 360.0f;
            }
            return angle;
        };
        const float a = retailAngle( outDir );
        // The other links of the node: how many lie within 91 deg of the link
        // taken, and how many in the four side bands (the retail counters).
        int32_t nearOut = 0, bandA = 0, bandB = 0, bandC = 0, bandD = 0;
        for ( const int32_t neighbour : neighbours ) {
            Vector3f direction;
            if ( !linkDirection( at.second, neighbour, direction ) ) {
                continue;
            }
            const float apart = std::acos( Clamp( direction.dot( outDir ), -1.0f, 1.0f ) ) * 57.29578f;
            nearOut += apart < 91.0f ? 1 : 0;
            const float r = retailAngle( direction );
            bandA += r > 44.0f && r < 91.0f ? 1 : 0;
            bandB += r > 89.0f && r < 136.0f ? 1 : 0;
            bandC += r > 224.0f && r < 271.0f ? 1 : 0;
            bandD += r > 269.0f && r < 316.0f ? 1 : 0;
        }
        int32_t arrow = 0;
        if ( a < 61.0f ) {
            arrow = 3;
        } else if ( a < 121.0f ) {
            arrow = a < 89.0f && bandB >= 1 ? 3 : ( a > 91.0f && bandA >= 1 ? 1 : 2 );
        } else if ( a < 151.0f ) {
            arrow = a <= 149.0f || nearOut > 1 ? 1 : 0;
        } else if ( a < 209.0f ) {
            arrow = 0;
        } else if ( a < 241.0f ) {
            arrow = a < 211.0f && nearOut <= 1 ? 0 : 7;
        } else if ( a < 299.0f ) {
            arrow = a < 269.0f && bandD >= 1 ? 7 : ( a > 271.0f && bandC >= 1 ? 5 : 6 );
        } else {
            arrow = 5;
        }
        if ( static_cast<size_t>( at.second ) < roadNodeKinds.size() && roadNodeKinds[at.second] == 3 ) {
            arrow += 8;
        }
        RouteTurn turn;
        turn.Node = at.second;
        turn.Along = along;
        turn.AngleDeg = a;
        turn.Arrow = arrow;
        routeTurns.push_back( turn );
    }
    static const bool bLogTurns = CmdLine::HasArg( "-logroute" );
    static size_t loggedTurns = 0;
    if ( bLogTurns && routeTurns.size() != loggedTurns ) {
        loggedTurns = routeTurns.size();
        for ( const RouteTurn &turn : routeTurns ) {
            OTDU_LOG_INFO( "MapScreen: junction node %d at %.0f m, angle %.1f deg, arrow %d\n", turn.Node, turn.Along, turn.AngleDeg,
                           turn.Arrow );
        }
    }
    static const bool bLogRoute = CmdLine::HasArg( "-logroute" );
    if ( bLogRoute )
        OTDU_LOG_INFO( "MapScreen: route %zu points, %.1f km, %zu junctions\n", routePoints.size(), dist[to] / 1000.0f, routeTurns.size() );
}

float MapScreen::projectOnRoute( const Vector3f &position, float &outDistance, Vector3f &outTangent ) const {
    outDistance = 1e30f;
    outTangent = Vector3f( 0.0f, 0.0f, 1.0f );
    if ( routePoints.size() < 2 || routeAlong.size() != routePoints.size() ) {
        return -1.0f;
    }
    float best = -1.0f;
    // Segment 0 is the link from the car to the road (computeRoute inserts the
    // car position first): it is not the route.
    for ( size_t i = routePoints.size() > 2 ? 1 : 0; i + 1 < routePoints.size(); i++ ) {
        const Vector3f a( routePoints[i].x, 0.0f, routePoints[i].z );
        const Vector3f segment( routePoints[i + 1].x - a.x, 0.0f, routePoints[i + 1].z - a.z );
        const float length2 = segment.dot( segment );
        if ( length2 < 1e-6f ) {
            continue;
        }
        const Vector3f toPosition( position.x - a.x, 0.0f, position.z - a.z );
        const float t = Clamp( toPosition.dot( segment ) / length2, 0.0f, 1.0f );
        const Vector3f nearest = a + segment * t;
        const float dx = position.x - nearest.x, dz = position.z - nearest.z;
        const float d = std::sqrt( dx * dx + dz * dz );
        if ( d < outDistance ) {
            outDistance = d;
            best = routeAlong[i] + t * std::sqrt( length2 );
            outTangent = segment * ( 1.0f / std::sqrt( length2 ) );
        }
    }
    return best;
}

void MapScreen::setTarget( const Vector3f &position ) {
    int32_t best = -1;
    float bestDistance = 1e30f;
    for ( size_t i = 0; i < roadNodes.size(); i++ ) {
        if ( roadNodes[i].x < 0.0f || roadLinks[i].empty() ) {
            continue;
        }
        const float dx = roadNodes[i].x - position.x, dz = roadNodes[i].z - position.z;
        const float d = dx * dx + dz * dz;
        if ( d < bestDistance ) {
            bestDistance = d;
            best = static_cast<int32_t>( i );
        }
    }
    if ( best < 0 ) {
        return;
    }
    target = roadNodes[best];
    bHasTarget = true;
    computeRoute();
}

void MapScreen::tickRoute( const Vector3f &carPosition, float deltaTime ) {
    playerPosition = carPosition; // also the start of a route set before the next tick (setTarget)
    if ( !bHasTarget ) {
        return;
    }
    const float dx = carPosition.x - target.x, dz = carPosition.z - target.z;
    if ( dx * dx + dz * dz < 40.0f * 40.0f ) {
        bHasTarget = false;
        routePoints.clear();
        routeAlong.clear();
        routeTurns.clear();
        routeLength = 0.0f;
        bArrivedFlag = true;
        OTDU_LOG_INFO( "MapScreen: destination reached\n" );
        return;
    }
    routeTimer += deltaTime;
    if ( routeTimer >= 1.0f ) {
        routeTimer = 0.0f;
        computeRoute();
    }
}

void MapScreen::updateMarkers() {
    const bool bShow = isUiShown();
    // the cursor: the movie's own reticle ('curseur': cible + anim); the status
    // variables (PS3 strings /:IsTargetPresent, /:IsTargetOK) tell it whether the
    // point under it is a valid GPS target: a snapped shop / road, or within
    // 2.5 km of a road node [INF]
    {
        const bool bValid = isCursorValid();
        // Fast travel: the retail only offers it on a road already discovered.
        Vector3f travelPoint;
        const bool bTravel = bShow && getFastTravelPoint( travelPoint );
        player.setVariableBool( "/:IsTargetPresent", true );
        player.setVariableBool( "/:IsTargetOK", bValid );
        player.setVariableBool( "/:IsTeleportOK", bTravel );
        // the reticle's own states (frame labels of /curseur/anim, found with
        // -dumplabelsclip): 'stop' = the "no" sign, 'on' = a valid target
        static int32_t lastCursorState = -1;
        if ( bShow && lastCursorState != ( bValid ? 1 : 0 ) ) {
            lastCursorState = bValid ? 1 : 0;
            player.gotoLabel( "/curseur/anim", bValid ? "on" : "stop", false );
            player.gotoLabel( "/curseur/cible", bValid ? "on" : "off", false );
        }
    }
    player.setClipVisible( "/curseur", bShow );
    if ( bShow ) {
        // The reticle is drawn where the snap put it: on the shop icon or on
        // the road, so the player sees what OK / fast travel will use.
        player.setVariableNumber( "/curseur:_x", snapX );
        player.setVariableNumber( "/curseur:_y", CanvasToStageY( snapY ) );
    }
    // the car (silhouette + ring), at its place on the map
    float x = 0.0f, y = 0.0f;
    const bool bCar = bShow && projectToStage( playerPosition, x, y ) && x > -60.0f && x < 1340.0f && y > 60.0f && y < 960.0f;
    for ( const char *pPath : { "/player", "/target__" } ) {
        player.setClipVisible( pPath, bCar );
        if ( bCar ) {
            player.setVariableNumber( std::string( pPath ) + ":_x", x );
            player.setVariableNumber( std::string( pPath ) + ":_y", y );
        }
    }
    // the destination
    float tx = 0.0f, ty = 0.0f;
    const bool bTarget = bShow && bHasTarget && projectToStage( target, tx, ty );
    player.setClipVisible( "/target", bTarget );
    if ( bTarget ) {
        player.setVariableNumber( "/target:_x", tx );
        player.setVariableNumber( "/target:_y", ty );
    }
    // the authored stroke clips are morph shapes the runtime cannot play: the
    // route is drawn by drawIsland instead
    static bool bHidden = false;
    if ( !bHidden ) {
        bHidden = true;
        for ( int32_t i = 1; i <= 20; i++ ) {
            char name[24];
            snprintf( name, sizeof( name ), "/strokes_pack/stroke_%02d", i );
            player.setClipVisible( name, false );
        }
        // clips of other modes that are not part of the retail grabs
        for ( const char *pPath : { "/quick_match", "/club_match", "/championship", "/join_player", "/cheatmenu", "/mini_menu", "/lure_chall", "/lure_zoed", "/lure_zoed_free", "/pack/clue" } ) {
            player.setClipVisible( pPath, false );
        }
    }
}

bool MapScreen::findSpotNear( const Vector3f &position, const char *pIcon, float radius, std::string &outName,
                              Vector3f *pOutPosition ) const {
    float best = radius * radius;
    bool bFound = false;
    for ( const Spot &spot : spots ) {
        if ( spot.pIcon == nullptr || strcmp( spot.pIcon, pIcon ) != 0 ) {
            continue;
        }
        const float dx = spot.Position.x - position.x, dz = spot.Position.z - position.z;
        const float d = dx * dx + dz * dz;
        if ( d < best ) {
            best = d;
            outName = spot.Name;
            if ( pOutPosition != nullptr ) {
                *pOutPosition = spot.Position;
            }
            bFound = true;
        }
    }
    return bFound;
}

void MapScreen::forEachSpot(
    const std::function<void( const std::string &, const char *, const Vector3f &, uint64_t, int32_t )> &visit ) const {
    for ( const Spot &spot : spots ) {
        if ( spot.pIcon != nullptr ) {
            visit( spot.Name, spot.pIcon, spot.Position, spot.Id, spot.LogoIndex );
        }
    }
}

bool MapScreen::findNearestSpot( const Vector3f &position, float radius,
                                 const std::function<bool( const std::string &, uint64_t )> &filter,
                                 std::string &outName, const char *&outIcon, Vector3f *pOutPosition,
                                 uint64_t *pOutId ) const {
    float best = radius * radius;
    bool bFound = false;
    for ( const Spot &spot : spots ) {
        const float dx = spot.Position.x - position.x, dz = spot.Position.z - position.z;
        const float d = dx * dx + dz * dz;
        if ( spot.pIcon == nullptr || d >= best || ( filter && !filter( spot.Name, spot.Id ) ) ) {
            continue;
        }
        best = d;
        outName = spot.Name;
        outIcon = spot.pIcon;
        if ( pOutPosition != nullptr ) {
            *pOutPosition = spot.Position;
        }
        if ( pOutId != nullptr ) {
            *pOutId = spot.Id;
        }
        bFound = true;
    }
    return bFound;
}

