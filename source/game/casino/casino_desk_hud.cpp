#include "shared.h"
#include "casino_desk_hud.h"

#include "flash/flash_localization.h"
#include "flash/gs_flash.h"
#include "input/gs_input.h"
#include "render/font.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
// The cocktail price of each tier: m_cockprice_1..4 = "¤50" / "¤200" /
// "¤500" / "¤1000" [DATA: DBLoc]. (m_tombo_desc4 says a 250 cocktail for the
// level 2 pass; the price label of the list is 200 - the list wins [INF].)
constexpr int32_t kTierPrice[4] = { 50, 200, 500, 1000 };

bool LogDesk() {
    static const bool bLog = CmdLine::HasArg( "-logdeskhud" );
    return bLog;
}
} // namespace

const std::vector<std::string> &CasinoDeskHud::CocktailIds() {
    // getCocktailIdFromCursor(1..15) [DATA: the barman clip's constant pool].
    static const std::vector<std::string> kIds = { "00", "01", "10", "11", "02", "03", "12", "20",
                                                   "13", "21", "22", "04", "14", "23", "24" };
    return kIds;
}

int32_t CasinoDeskHud::CocktailTier( const std::string &id ) {
    // displayTicket: cursor 1-4 lvl1, 5-8 lvl2, 9-11 lvl3, 12-15 lvl4 [DATA].
    const std::vector<std::string> &ids = CocktailIds();
    for ( size_t i = 0; i < ids.size(); i++ ) {
        if ( ids[i] == id ) {
            return i < 4 ? 1 : i < 8 ? 2 : i < 11 ? 3 : 4;
        }
    }
    return 1;
}

bool CasinoDeskHud::load() {
    unload();
    if ( gpFlash == nullptr || CmdLine::HasArg( "-nodeskhud" ) ) {
        return false;
    }
    if ( FlashMovie *pCommon = gpFlash->getMovie( "cas_common" ) ) {
        bBarLoaded = bar.load( pCommon );
    }
    if ( bBarLoaded ) {
        bar.setFSCommandCallback( [this]( const std::string &command, const std::string & ) {
            onBarCommand( command );
        } );
        bar.tick( 1.0f / 30.0f );
        ApplyFlashLocalization( bar );
        // Offline: no other players to offer a cocktail to (X is locked) and
        // no tournament popups queued [INF].
        bar.setVariableBool( "/:playerListLocked", true );
        bar.setVariableBool( "/:isPopupLocked", false );
        bar.setVariableBool( "/:IsCasino", true );
    } else {
        OTDU_LOG_WARN( "CasinoDeskHud: Cas_Common.vmf (cas_common.bnk) not available\n" );
    }
    if ( FlashMovie *pCasino = gpFlash->getMovie( "casino" ) ) {
        bCashierLoaded = cashier.load( pCasino );
    }
    if ( bCashierLoaded ) {
        cashier.setFSCommandCallback( [this]( const std::string &command, const std::string & ) {
            onCashierCommand( command );
        } );
        cashier.tick( 1.0f / 30.0f );
        ApplyFlashLocalization( cashier );
        // m_conv_txt2 "The conversion rate is: §DOLL_CONV§ = §CASP_CONV§": the
        // game fills the two tokens (PS3 0xf48e78 "CASP_CONV", 0xf48e88
        // "DOLL_CON") [DATA]; the rate is 1:1 offline [INF].
        std::string rate = cashier.getVariable( "/:m_conv_txt2" );
        // The string tables are Latin-1 (a lone 0xA7 for the section sign);
        // the UTF-8 form is matched too. The chip sign follows the table's own
        // encoding (0xA4, as in m_cockprice_N "¤50").
        struct RateToken {
            const char *pText;
            const char *pValue;
        };
        const RateToken tokens[] = {
            { "\xa7"
              "DOLL_CONV\xa7",
              "$1" },
            { "\xa7"
              "CASP_CONV\xa7",
              "1 \xa4" },
            { "\xc2\xa7"
              "DOLL_CONV\xc2\xa7",
              "$1" },
            { "\xc2\xa7"
              "CASP_CONV\xc2\xa7",
              "1 \xc2\xa4" },
        };
        for ( const RateToken &token : tokens ) {
            const size_t at = rate.find( token.pText );
            if ( at != std::string::npos ) {
                rate.replace( at, strlen( token.pText ), token.pValue );
            }
        }
        cashier.setVariable( "/:m_conv_txt2", rate );
        if ( LogDesk() ) {
            OTDU_LOG_INFO( "CasinoDeskHud: rate text '%s'\n", rate.c_str() );
        }
    } else {
        OTDU_LOG_WARN( "CasinoDeskHud: Casino.vmf (casino.bnk) not available\n" );
    }
    OTDU_LOG_INFO( "CasinoDeskHud: bar movie %s, cashier movie %s\n", bBarLoaded ? "loaded" : "missing",
                   bCashierLoaded ? "loaded" : "missing" );
    return bBarLoaded || bCashierLoaded;
}

void CasinoDeskHud::unload() {
    if ( bBarLoaded ) {
        bar.unload();
        if ( gpFlash != nullptr ) {
            gpFlash->releaseMovie( "cas_common" );
        }
    }
    if ( bCashierLoaded ) {
        cashier.unload();
        if ( gpFlash != nullptr ) {
            gpFlash->releaseMovie( "casino" );
        }
    }
    bBarLoaded = bCashierLoaded = bBarOpen = bRaffle = bCashierOpen = bSliderPage = false;
    events.clear();
}

void CasinoDeskHud::openBar( const std::vector<std::string> &unlockedIds, bool bShowTutorial ) {
    if ( !bBarLoaded ) {
        return;
    }
    for ( const std::string &id : CocktailIds() ) {
        const bool bOpen = std::find( unlockedIds.begin(), unlockedIds.end(), id ) != unlockedIds.end();
        bar.setVariableBool( "/:cocktail_" + id + "_IsLock", !bOpen );
        // /:m_cockPrice_<id> [DATA: PS3 0xf37868 "/:m_cockPrice_"]: the price
        // of the row (the list shows the tier label m_cockprice_N).
        bar.setVariableNumber( "/:m_cockPrice_" + id, kTierPrice[CocktailTier( id ) - 1] );
    }
    // The raffle is on (tombola_available): the list shows the ticket of the
    // highlighted cocktail, the first visit opens the tutorial (open_tuto)
    // unless skipTomboTut [DATA: the root 'cocktail' frame].
    bar.setVariableBool( "/:tombola_available", true );
    bar.setVariableBool( "/:skipTomboTut", !bShowTutorial );
    bar.setVariableBool( "/:Input", true );
    bar.gotoLabel( "/", "cocktail", true );
    bBarOpen = true;
    if ( LogDesk() ) {
        OTDU_LOG_INFO( "CasinoDeskHud: bar open, %zu cocktail(s) unlocked, tutorial %d\n", unlockedIds.size(),
                       bShowTutorial ? 1 : 0 );
    }
}

void CasinoDeskHud::showRaffle( bool bWin, int32_t level ) {
    if ( !bBarLoaded ) {
        return;
    }
    // tombola_popup: Open() -> 'open' runs setTxt(): var_win picks the
    // content/result 'win' / 'lose' art, var_level_win the content/level
    // 'level1'..'level4' pass; the popup closes itself after timer_val loops
    // [DATA: the popup's own script].
    bar.setVariableBool( "/tombola_popup:var_win", bWin );
    bar.setVariableNumber( "/tombola_popup:var_level_win", bWin ? level : 0 );
    bar.invoke( "/tombola_popup", "Open", {} );
    bRaffle = true;
}

bool CasinoDeskHud::isRaffleShown() {
    if ( !bRaffle ) {
        return false;
    }
    // The popup's last frame sets opened = false and goes back to 'default'.
    if ( bar.getVariable( "/tombola_popup:opened" ) != "true" ) {
        bRaffle = false;
    }
    return bRaffle;
}

void CasinoDeskHud::showMessage( const std::string &text ) {
    messageText = text;
    messageTimer = 3.0f;
}

void CasinoDeskHud::openCashier( int32_t chipsBalance, int32_t moneyBalance ) {
    if ( !bCashierLoaded ) {
        return;
    }
    chips = chipsBalance;
    money = moneyBalance;
    // InitButtons: a direction is locked when there is nothing to convert.
    cashier.setVariableBool( "/money_conv:is_DolToCas_locked", money <= 0 );
    cashier.setVariableBool( "/money_conv:is_CasToDol_locked", chips <= 0 );
    cashier.setVariableBool( "/money_conv:isReady", false );
    cashier.setVariableBool( "/:Input", true );
    cashier.setVariableBool( "/:fake", false );
    cashier.invoke( "/money_conv", "Open", {} );
    bCashierOpen = true;
    bSliderPage = false;
    if ( LogDesk() ) {
        OTDU_LOG_INFO( "CasinoDeskHud: cashier open (%d chips, $%d)\n", chips, money );
    }
}

void CasinoDeskHud::setConversionRange( int32_t maxValue, int32_t startValue ) {
    if ( !bCashierLoaded ) {
        return;
    }
    sliderMax = std::max( 0, maxValue );
    sliderValue = std::min( std::max( startValue, 0 ), sliderMax );
    // open2's initSlider: slider.initSlider(convDev, 0, valSliderMax,
    // valSliderMin); the texts are the PS3's :txtSliderMin / :txtSliderMax
    // (0xf48e48) [DATA].
    cashier.setVariableNumber( "/money_conv:valSliderMin", 0 );
    cashier.setVariableNumber( "/money_conv:valSliderMax", sliderMax );
    cashier.setVariable( "/money_conv:txtSliderMin", "0" );
    cashier.setVariable( "/money_conv:txtSliderMax", std::to_string( sliderMax ) );
    cashier.setVariable( "/money_conv:convDev", "conv" ); // the slider's FSCommand prefix
    // 'loop' waits on isReady, then plays open2 (the slider page).
    cashier.setVariableBool( "/money_conv:isReady", true );
    bSliderPage = true;
}

void CasinoDeskHud::refreshSlider() {
    // The PS3 cashier writes the slider's :currentValue / :txtcurrentValue and
    // calls setSliderPosition, and the change button's :txt_bt shows the
    // amount received (0xf48d68..0xf48e00) [DATA].
    cashier.setVariableNumber( "/money_conv/money_box/slider:currentValue", sliderValue );
    cashier.setVariable( "/money_conv/money_box/slider:txtcurrentValue", std::to_string( sliderValue ) );
    cashier.invoke( "/money_conv/money_box/slider", "setSliderPosition", {} );
    cashier.setVariable( "/money_conv/money_box/bt_change:txt_bt", std::to_string( sliderValue ) );
}

void CasinoDeskHud::stepSlider( int32_t direction, bool bToEnd ) {
    if ( bToEnd ) {
        sliderValue = direction < 0 ? 0 : sliderMax;
    } else {
        // A step of a hundredth of the range, at least 1 [INF: the retail
        // step is the exe's].
        const int32_t step = Max( 1, sliderMax / 100 );
        sliderValue = std::min( std::max( sliderValue + direction * step, 0 ), sliderMax );
    }
    // Refreshed on the next tick: this runs inside the movie's script.
    bSliderDirty = true;
}

void CasinoDeskHud::onBarCommand( const std::string &command ) {
    if ( LogDesk() && command.compare( 0, 5, "sndp:" ) != 0 ) {
        OTDU_LOG_INFO( "CasinoDeskHud: bar FSCommand '%s'\n", command.c_str() );
    }
    if ( command.compare( 0, 9, "buy cock " ) == 0 ) {
        cocktailId = command.substr( 9 );
        // The cursor id is the movie's string ("00".."24"); a number form
        // ("buy cock %d" on the PS3 side) keeps two digits.
        if ( cocktailId.size() == 1 ) {
            cocktailId = "0" + cocktailId;
        }
        events.push_back( eEvent::BuyCocktail );
    } else if ( command == "clco" ) {
        bBarOpen = false;
        events.push_back( eEvent::BarClosed );
    }
}

void CasinoDeskHud::onCashierCommand( const std::string &command ) {
    if ( LogDesk() && command.compare( 0, 5, "sndp:" ) != 0 ) {
        OTDU_LOG_INFO( "CasinoDeskHud: cashier FSCommand '%s'\n", command.c_str() );
    }
    if ( command == "casi:chx1" || command == "casi:chx2" ) {
        bChipsToDollars = command == "casi:chx2";
        events.push_back( eEvent::ConvertChoice );
    } else if ( command == "casi:qtcv" ) {
        bCashierOpen = false;
        bSliderPage = false;
        events.push_back( eEvent::CashierClosed );
    } else if ( command == "slle" || command == "conv:sllf" ) {
        stepSlider( -1, false );
    } else if ( command == "slri" || command == "conv:slrg" ) {
        stepSlider( 1, false );
    } else if ( command == "conv:minv" ) {
        stepSlider( -1, true );
    } else if ( command == "conv:maxv" ) {
        stepSlider( 1, true );
    } else if ( command == "conv:vali" ) {
        bSliderPage = false;
        bCashierOpen = false;
        events.push_back( eEvent::ConvertDone );
    } else if ( command == "conv:canc" ) {
        // Back to page 1 (the movie plays open1 itself).
        bSliderPage = false;
        cashier.setVariableBool( "/money_conv:isReady", false );
        cashier.setVariableBool( "/:Input", true );
    }
}

void CasinoDeskHud::routeInput() {
    if ( gpInput == nullptr ) {
        return;
    }
    if ( bBarOpen && bBarLoaded ) {
        // The root's own A / B / UP / DOWN / LEFT / RIGHT of the 'cocktail'
        // frame (they forward to barman.Buy_Cocktail / Cancel_Bar /
        // MoveCursor, and close the tutorial) [DATA].
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            bar.invoke( "/", "A", {} );
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            bar.invoke( "/", "B", {} );
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
            bar.invoke( "/", "UP", {} );
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
            bar.invoke( "/", "DOWN", {} );
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            bar.invoke( "/", "LEFT", {} );
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            bar.invoke( "/", "RIGHT", {} );
        }
        return;
    }
    if ( bCashierOpen && bCashierLoaded ) {
        // _root.Clip is money_conv at stop1 / stop2 [DATA]; its A / B / Left /
        // Right, and LB / RB (min / max) on the slider page.
        const char *pMethod = nullptr;
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            pMethod = "A";
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            pMethod = "B";
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            pMethod = "Left";
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            pMethod = "Right";
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ||
                    gpInput->wasPressed( eInputAction::IA_MenuOption1 ) ) {
            pMethod = "LB";
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ||
                    gpInput->wasPressed( eInputAction::IA_MenuOption2 ) ) {
            pMethod = "RB";
        }
        if ( pMethod != nullptr ) {
            if ( LogDesk() ) {
                OTDU_LOG_INFO( "CasinoDeskHud: cashier %s (page %s)\n", pMethod,
                               cashier.getVariable( "/money_conv:currentPage" ).c_str() );
            }
            cashier.invokeMethod( "/:Clip", pMethod, {} );
        }
    }
}

void CasinoDeskHud::tick( float deltaTime ) {
    if ( bBarLoaded && ( bBarOpen || bRaffle ) ) {
        bar.tick( deltaTime );
    }
    if ( bCashierLoaded && bCashierOpen ) {
        cashier.tick( deltaTime );
        // The slider page re-reads the value the game holds (initSlider
        // starts it at 0).
        if ( bSliderPage && cashier.getVariable( "/money_conv:currentPage" ) == "2" &&
             ( bSliderDirty ||
               cashier.getVariable( "/money_conv/money_box/slider:currentValue" ) != std::to_string( sliderValue ) ) ) {
            bSliderDirty = false;
            refreshSlider();
        }
    }
    messageTimer = std::max( 0.0f, messageTimer - deltaTime );
}

void CasinoDeskHud::draw( Renderer2D &renderer, const Font *pFont ) {
    if ( bBarLoaded && ( bBarOpen || bRaffle ) ) {
        // Only the bar's own clips: the rest of Cas_Common (the player lists,
        // MyLife, the emotes) is online UI.
        bar.drawClipOnly( renderer, "/barman" );
        bar.drawClipOnly( renderer, "/tombola_popup" );
        bar.drawClipOnly( renderer, "/help" );
    }
    if ( bCashierLoaded && bCashierOpen ) {
        cashier.drawClipOnly( renderer, "/money_conv" );
        cashier.drawClipOnly( renderer, "/help" );
    }
    if ( messageTimer > 0.0f && pFont != nullptr ) {
        renderer.clearClipRect();
        renderer.setStencil( eStencilMode::SM_None, 0 );
        renderer.begin( 1280.0f, 720.0f );
        const Rect2D box( 340.0f, 300.0f, 600.0f, 90.0f );
        renderer.drawRect( box, MakeRGBA( 10, 14, 28, 235 ) );
        renderer.drawRectOutline( box, 2.0f, MakeRGBA( 90, 130, 191 ) );
        renderer.drawText( pFont, messageText.c_str(), 640.0f, 335.0f, 20.0f, MakeRGBA( 236, 236, 240 ),
                           eTextAlign::TA_Center );
    }
}

std::vector<CasinoDeskHud::eEvent> CasinoDeskHud::takeEvents() {
    std::vector<eEvent> out;
    out.swap( events );
    return out;
}
