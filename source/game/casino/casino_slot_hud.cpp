#include "shared.h"
#include "casino_slot_hud.h"

#include "flash/flash_localization.h"
#include "flash/gs_flash.h"
#include "input/gs_input.h"
#include "render/font.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"

#include <cstdlib>

namespace {
    // The movie's machine pages (getHelpName / setTitle / define_SM_Options),
    // indexed by CasinoSlots::eMachine: smca = TDU 2 (sm_car), smcl = Fashion
    // (sm_cloth), smpo = Video Poker, smh1 = Beach (sm_hawai1), smc1 = Classic,
    // sml1 = High Life (sm_luxe1) [DATA: the help names and m_smxx titles].
    constexpr const char *kPages[] = { "smca", "smcl", "smpo",
                                       "smh1", "smc1", "sml1" };
    // getHelpName() of each page at currentStep 1: the help bar frame of the
    // play controls (help/txtContainer/help_display labels) [DATA].
    constexpr const char *kPlayHelp[] = { "sm_car_1",      "sm_cloth_1",
                                          "sm_poker_1",    "sm_hawai1_1",
                                          "sm_classic1_1", "sm_luxe1_1" };

    // Cas_SlotScreen.vmf is 512 x 256 (2:1); the retail draws it on the
    // machine's CasinoFlashScreen. The port has no movie -> texture path yet,
    // so it sits as a panel in the top right corner of the machine view [INF].
    constexpr float kScreenRect[4] = { 0.735f, 0.17f, 0.24f,
                                       0.24f * 1280.0f / 720.0f * 0.5f };
} // namespace

bool CasinoSlotHud::load() {
    unload();
    if ( gpFlash == nullptr || CmdLine::HasArg( "-noslothud" ) ) {
        return false;
    }
    FlashMovie *pMachine = gpFlash->getMovie( "cas_slotmachine" );
    if ( pMachine == nullptr || !machine.load( pMachine ) ) {
        OTDU_LOG_WARN( "CasinoSlotHud: Cas_SlotMachine.vmf "
                       "(cas_slotmachine.bnk) not available\n" );
        return false;
    }
    machine.setFSCommandCallback(
        [this]( const std::string &command, const std::string &args ) {
            onCommand( command, args );
        } );
    // The first frames run SetBDD (db_casino -> sbdd) and resetVariables.
    machine.tick( 1.0f / 30.0f );
    ApplyFlashLocalization( machine );
    bLoaded = true;

    if ( FlashMovie *pScreen = gpFlash->getMovie( "cas_slotscreen" ) ) {
        bScreenLoaded = screen.load( pScreen );
        if ( bScreenLoaded ) {
            screen.setFSCommandCallback(
                []( const std::string &command, const std::string & ) {
                    if ( CmdLine::HasArg( "-logslothud" ) ) {
                        OTDU_LOG_INFO( "CasinoSlotHud: screen FSCommand '%s'\n",
                                       command.c_str() );
                    }
                } );
            screen.tick( 1.0f / 30.0f );
            bIdleLoaded = idleScreen.load( pScreen );
            if ( bIdleLoaded ) {
                idleScreen.tick( 1.0f / 30.0f );
            }
        }
    }
    if ( FlashMovie *pLines = gpFlash->getMovie( "cas_slotlines" ) ) {
        bLinesLoaded = lines.load( pLines );
        if ( bLinesLoaded ) {
            lines.tick( 1.0f / 30.0f );
        }
    }
    // The screens are drawn on the machines (render targets) unless
    // -slotscreenpanel asks for the 2D panel of the first HUD build.
    bScreenTargets = bScreenLoaded && gpRender != nullptr &&
                     !CmdLine::HasArg( "-slotscreenpanel" );
    const float fullStage[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    if ( bScreenLoaded ) {
        screen.setStageRect( bScreenTargets ? fullStage : kScreenRect );
    }
    if ( bIdleLoaded ) {
        idleScreen.setStageRect( fullStage );
    }
    if ( bLinesLoaded ) {
        lines.setStageRect( fullStage );
    }
    static uint64_t sStamp = 0x5c4ee70000000000ull;
    textureStamp = ++sStamp;
    OTDU_LOG_INFO(
        "CasinoSlotHud: Cas_SlotMachine.vmf loaded, slot screen %s\n",
        bScreenLoaded ? "loaded" : "missing" );
    return true;
}

void CasinoSlotHud::unload() {
    if ( bLoaded ) {
        machine.unload();
    }
    if ( bScreenLoaded ) {
        screen.unload();
    }
    if ( bIdleLoaded ) {
        idleScreen.unload();
    }
    if ( bLinesLoaded ) {
        lines.unload();
    }
    if ( gpFlash != nullptr && ( bLoaded || bScreenLoaded ) ) {
        gpFlash->releaseMovie( "cas_slotmachine" );
        gpFlash->releaseMovie( "cas_slotscreen" );
    }
    if ( gpFlash != nullptr && bLinesLoaded ) {
        gpFlash->releaseMovie( "cas_slotlines" );
    }
    if ( gpRender != nullptr ) {
        for ( RenderTarget **ppTarget :
              { &pScreenTarget, &pIdleTarget, &pLinesTarget } ) {
            if ( *ppTarget != nullptr ) {
                gpRender->getDevice().destroyRenderTarget( *ppTarget );
                *ppTarget = nullptr;
            }
        }
    }
    bLoaded = bScreenLoaded = bIdleLoaded = bLinesLoaded = bScreenTargets =
        bOpen = bDialog = false;
    idleFrames = 0;
    events.clear();
}

void CasinoSlotHud::open( int32_t machineIndex,
                          int32_t price,
                          int32_t reels,
                          int32_t chips,
                          bool bMaleOutfit,
                          bool bMultiLineMachine ) {
    if ( !bLoaded ) {
        return;
    }
    const char *pPage =
        machineIndex >= 0 && machineIndex < 6 ? kPages[machineIndex] : "sml1";
    playHelp = machineIndex >= 0 && machineIndex < 6 ? kPlayHelp[machineIndex]
                                                     : "sm_luxe1_1";
    // The root's own defaults are the Fashion machine for a man with 2000 in
    // the account (frame 0) [DATA]; the game overrides them per machine.
    machine.gotoLabel( "/", "default", false );
    machine.tick( 1.0f / 30.0f );
    machine.setVariable( "/:WhatPage", pPage );
    machine.setVariableNumber( "/:var_Money_InAccount", chips );
    machine.setVariableNumber( "/:varMachineBuyIn", price );
    machine.setVariableNumber( "/:nbSpins", reels >= 5 ? 5 : 3 );
    machine.setVariableNumber( "/:nbActiveLines", 1 );
    machine.setVariableBool( "/:is_smcl_man", bMaleOutfit );
    machine.setVariableBool( "/:Input", true );
    machine.gotoLabel( "/", "menu", true );
    bOpen = true;
    bDialog = false;
    bRelaunchStep = false;
    bMultiLine = bMultiLineMachine;
    pendingHelp.clear();
    OTDU_LOG_INFO( "CasinoSlotHud: machine page '%s' (price %d, %d reels, %d "
                   "chips) -> menu\n",
                   pPage, price, reels, chips );
}

void CasinoSlotHud::close() {
    if ( !bLoaded ) {
        return;
    }
    machine.gotoLabel( "/", "default", false );
    bOpen = false;
    bDialog = false;
}

std::vector<CasinoSlotHud::eEvent> CasinoSlotHud::takeEvents() {
    std::vector<eEvent> out;
    out.swap( events );
    return out;
}

int32_t CasinoSlotHud::getActiveLines() {
    return bLoaded ? atoi( machine.getVariable( "/:nbActiveLines" ).c_str() )
                   : 1;
}

bool CasinoSlotHud::isPlaying() {
    // The active controls frame registers the root as _root.Clip and the
    // menu is closed.
    if ( !bLoaded || !bOpen ) {
        return false;
    }
    const FlashPlayer::Clip *pRoot = machine.getRoot();
    return machine.getVariable( "/:InRootMenu" ) != "true" &&
           machine.getClipVariable( "/:Clip" ) == pRoot;
}

void CasinoSlotHud::onCommand( const std::string &command,
                               const std::string &args ) {
    if ( CmdLine::HasArg( "-logslothud" ) &&
         command.compare( 0, 5, "sndp:" ) != 0 ) {
        OTDU_LOG_INFO( "CasinoSlotHud: FSCommand '%s' '%s'\n", command.c_str(),
                       args.c_str() );
    }
    if ( command == "sbdd" ) {
        ApplyFlashLocalization( machine );
    } else if ( command == "smon" ) {
        // Play: the 'play' frame calls _root.SetHelp(getHelpName()) without
        // its Change flag, which leaves the menu's help bar up (help.SetHelp
        // acts on Change == true / false only). The exe swaps the bar itself
        // [INF: the casino code is encrypted]: the play help with Change true.
        // (Deferred to the next tick: this runs inside the movie's script.)
        pendingHelp = playHelp;
    } else if ( command == "smof" ) {
        pendingHelp = "menu";
        events.push_back( eEvent::BackToMenu );
    } else if ( command == "smpl" ) {
        events.push_back( eEvent::Spin );
    } else if ( command == "sloo" ) {
        events.push_back( eEvent::Leave );
    } else if ( command == "lino" ) {
        events.push_back( eEvent::LinesChanged );
    } else if ( command == "left" ) {
        events.push_back( eEvent::Left );
    } else if ( command == "right" ) {
        events.push_back( eEvent::Right );
    } else if ( command == "lock" ) {
        events.push_back( eEvent::Lock );
    } else if ( command == "dbxo" ) {
        // DBXO(Case, Txt1, Txt2, Type1, Type2, Choice): /:YesNo is set for the
        // question boxes (Choice true / false), /:PlayerChoice otherwise.
        dialogText = machine.getVariable( "/:Msg1Txt" );
        bDialogYesNo = machine.getVariable( "/:DgbxCase" ) == "sure_quit" ||
                       machine.getVariable( "/:DgbxCase" ) == "error_money2";
        bDialog = true;
    }
    // smon / smof / lini / linc / wmon / wmof / hsys:<page> / sndp:<sound>: the
    // movie animates itself; nothing for the game to do offline.
}

void CasinoSlotHud::sendToFocus( const char *pMethod, const char *pMoveArg ) {
    // The menu: its own Move / A while /:InRootMenu [DATA: the menu's 'open'
    // frame sets /:InRootMenu and /:RootMenuPath = this].
    if ( machine.getVariable( "/:InRootMenu" ) == "true" ) {
        if ( pMoveArg != nullptr ) {
            machine.invokeMethod( "/:RootMenuPath", "Move", { pMoveArg } );
        } else {
            machine.invokeMethod( "/:RootMenuPath", pMethod, {} );
        }
        return;
    }
    // Everything else registers itself as _root.Clip: the active controls and
    // the lines popup (root functions A / B / Left / Right / X_Lines / X_Lock),
    // the help system (A / B / Move('left' | 'right')).
    if ( !machine.invokeMethod( "/:Clip", pMethod, {} ) &&
         pMoveArg != nullptr ) {
        machine.invokeMethod( "/:Clip", "Move", { pMoveArg } );
    }
}

void CasinoSlotHud::routeInput() {
    if ( !bLoaded || !bOpen || gpInput == nullptr ) {
        return;
    }
    if ( bDialog ) {
        // The dialog box answer: 'a' validates, anything else cancels.
        const bool bYes = gpInput->wasPressed( eInputAction::IA_MenuValidate );
        const bool bNo = gpInput->wasPressed( eInputAction::IA_MenuBack );
        if ( bYes || ( bNo && bDialogYesNo ) ) {
            bDialog = false;
            machine.setVariable( "/:PlayerAnswer", bYes ? "a" : "b" );
            machine.invoke( "/", "DialogBox", {} );
        }
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        sendToFocus( "A", nullptr );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        if ( machine.getVariable( "/:InRootMenu" ) == "true" ) {
            // B on the machine menu: the movie's own "are you sure you want
            // to quit this machine?" box (DialogBox 'sure_quit' sends sloo on
            // A) [INF: the menu clip defines no B].
            machine.setVariable( "/:DgbxCase", "sure_quit" );
            machine.setVariable( "/:Msg1Txt",
                                 machine.getVariable( "/:m_slo_conf_quit" ) );
            machine.setVariableBool( "/:YesNo", true );
            dialogText = machine.getVariable( "/:m_slo_conf_quit" );
            bDialogYesNo = true;
            bDialog = true;
        } else {
            sendToFocus( "B", nullptr );
        }
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        sendToFocus( "Up", "up" );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        sendToFocus( "Down", "down" );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        sendToFocus( "Left", "left" );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        sendToFocus( "Right", "right" );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuOption1 ) ) {
        // X / Square: the lines manager (X_Lines) of the active controls -
        // only on the machines whose help bar offers it (the movie's
        // X_Lines does not check) [INF].
        // At the second chance step X is "Lock/Unlock" (the step-2 help bar
        // of sm_poker_2 / sm_hawai1_2) [DATA].
        if ( bRelaunchStep ) {
            sendToFocus( "X_Lock", nullptr );
        } else if ( bMultiLine ) {
            sendToFocus( "X_Lines", nullptr );
        }
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuOption2 ) ) {
        // Y / Triangle: X_Lock (hold on the second chance machines).
        sendToFocus( "X_Lock", nullptr );
    }
}

void CasinoSlotHud::tick( float deltaTime ) {
    if ( bLoaded && bOpen ) {
        machine.tick( deltaTime );
        if ( !pendingHelp.empty() ) {
            machine.invokeWithFlag( "/", "SetHelp", pendingHelp, true );
            pendingHelp.clear();
        }
    }
    if ( bScreenLoaded && bOpen ) {
        screen.tick( deltaTime );
    }
}

void CasinoSlotHud::draw( Renderer2D &renderer, const Font *pFont ) {
    if ( !bLoaded || !bOpen ) {
        return;
    }
    if ( bScreenLoaded && !bScreenTargets ) {
        screen.draw( renderer );
    }
    machine.draw( renderer );
    if ( bDialog && pFont != nullptr ) {
        // The dialog box of GENERAL.vmf is not up in the casino: a native box
        // with the movie's text [INF].
        renderer.clearClipRect();
        renderer.setStencil( eStencilMode::SM_None, 0 );
        renderer.begin( 1280.0f, 720.0f );
        const Rect2D box( 290.0f, 290.0f, 700.0f, 130.0f );
        renderer.drawRect( box, MakeRGBA( 10, 14, 28, 235 ) );
        renderer.drawRectOutline( box, 2.0f, MakeRGBA( 90, 130, 191 ) );
        renderer.drawText( pFont, dialogText.c_str(), 640.0f, 330.0f, 20.0f,
                           MakeRGBA( 236, 236, 240 ), eTextAlign::TA_Center );
        renderer.drawText( pFont,
                           bDialogYesNo
                               ? "A / Enter: yes      B / Backspace: no"
                               : "A / Enter: OK",
                           640.0f, 385.0f, 16.0f, MakeRGBA( 170, 176, 196 ),
                           eTextAlign::TA_Center );
    }
}

void CasinoSlotHud::drawMovieInto( FlashPlayer &player,
                                   RenderTarget *&pTarget,
                                   uint32_t width,
                                   uint32_t height,
                                   const char *pName ) {
    RenderDevice &device = gpRender->getDevice();
    if ( pTarget == nullptr ) {
        // The main formats (colour + depth / stencil): the 2D pipelines and
        // the flash masks' stencil work in it as in the main pass.
        pTarget = device.createRenderTarget( width, height, VK_FORMAT_UNDEFINED,
                                             true, pName );
        if ( pTarget == nullptr ) {
            return;
        }
    }
    Renderer2D &renderer = gpRender->get2DRenderer();
    // -slotscreenclear: a red clear (the A/B of the binding vs the movie draw).
    static const bool bRedClear = CmdLine::HasArg( "-slotscreenclear" );
    device.beginRenderTarget( pTarget, true,
                              bRedClear ? ColorRGBA( 1.0f, 0.0f, 0.0f, 1.0f )
                                        : ColorRGBA( 0.0f, 0.0f, 0.0f, 0.0f ) );
    renderer.setTargetSize( width, height );
    player.draw( renderer );
    renderer.end();
    renderer.setTargetSize( 0, 0 );
    device.endRenderTarget();
}

void CasinoSlotHud::drawOffscreen() {
    if ( !bScreenTargets || gpRender == nullptr ) {
        return;
    }
    // Cas_SlotScreen is 512 x 256 and Cas_SlotLines 1024 x 512 [DATA: the
    // movie headers]. The idle screen and the lines do not move: drawn on
    // the first frames, then every second.
    if ( bOpen ) {
        drawMovieInto( screen, pScreenTarget, 512, 256, "slotscreen_played" );
    }
    if ( idleFrames++ % 60 == 0 || pIdleTarget == nullptr ) {
        if ( bIdleLoaded ) {
            drawMovieInto( idleScreen, pIdleTarget, 512, 256,
                           "slotscreen_idle" );
        }
        if ( bLinesLoaded ) {
            drawMovieInto( lines, pLinesTarget, 1024, 512, "slotlines" );
        }
    }
}

GPUTexture *CasinoSlotHud::getPlayedScreenTexture() const {
    return bOpen && pScreenTarget != nullptr ? &pScreenTarget->Color : nullptr;
}

GPUTexture *CasinoSlotHud::getIdleScreenTexture() const {
    return pIdleTarget != nullptr ? &pIdleTarget->Color : nullptr;
}

GPUTexture *CasinoSlotHud::getLinesTexture() const {
    return pLinesTarget != nullptr ? &pLinesTarget->Color : nullptr;
}

void CasinoSlotHud::setScreenValues( int32_t chips, int32_t wager ) {
    if ( !bScreenLoaded ) {
        return;
    }
    screen.setVariableNumber( "/:MoneyInMachine", chips );
    screen.setVariableNumber( "/:BetValue", wager );
    if ( bLoaded ) {
        machine.setVariableNumber( "/:var_Money_InAccount", chips );
        machine.setVariableNumber( "/:moneyInMachine", chips );
    }
}

void CasinoSlotHud::screenSpinStarted( int32_t chipsBefore, int32_t wager ) {
    if ( !bScreenLoaded ) {
        return;
    }
    // 'loss' -> 'loop_loss': MoneyInMachine -= BetValue, += GainValue (0).
    screen.setVariableNumber( "/:MoneyInMachine", chipsBefore );
    screen.setVariableNumber( "/:BetValue", wager );
    screen.setVariableNumber( "/:GainValue", 0 );
    screen.gotoLabel( "/", "loss", true );
}

void CasinoSlotHud::screenResult( int32_t payout ) {
    if ( !bScreenLoaded ) {
        return;
    }
    if ( payout > 0 ) {
        // 'gain' counts GainValue into MoneyInMachine (1 a frame, or by
        // var_percent above 120) and ends with endi [DATA].
        screen.setVariableNumber( "/:GainValue", payout );
        screen.setVariableNumber( "/:var_percent",
                                  payout > 120 ? payout / 120.0 : 1.0 );
        screen.gotoLabel( "/", "gain", true );
    } else {
        screen.gotoLabel( "/", "not_win", true );
    }
}

void CasinoSlotHud::enterRelaunch() {
    if ( !bLoaded || !bOpen ) {
        return;
    }
    // 'relaunch_step' sets currentStep 2 and calls SetHelp( getHelpName() )
    // without its Change flag (the same gap as 'play'): the step-2 bar is
    // swapped in by the game [INF].
    machine.gotoLabel( "/", "relaunch_step", true );
    bRelaunchStep = true;
    pendingHelp = playHelp == "sm_poker_1"    ? "sm_poker_2"
                  : playHelp == "sm_hawai1_1" ? "sm_hawai1_2"
                                              : playHelp;
}

void CasinoSlotHud::leaveRelaunch() {
    if ( !bLoaded || !bOpen ) {
        return;
    }
    machine.setVariableNumber( "/:currentStep", 1 );
    bRelaunchStep = false;
    pendingHelp = playHelp;
}

void CasinoSlotHud::showError( int32_t errorType ) {
    if ( !bLoaded ) {
        return;
    }
    machine.setVariableNumber( "/:error_type", errorType );
    machine.gotoLabel( "/", "error", true );
}
