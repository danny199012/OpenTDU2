#pragma once

#include "flash/flash_player.h"

#include <string>
#include <vector>

class Renderer2D;
class Font;
struct RenderTarget;
struct GPUTexture;

// The retail slot-machine HUD: Cas_SlotMachine.vmf (cas_slotmachine.bnk) - the
// machine's menu (Play / Rules / Table of gains / Quit), its title bar, the
// rules and gains pages of its help system, the play help bar, the lines
// manager popup and the error / confirm boxes - plus Cas_SlotScreen.vmf
// (cas_slotscreen.bnk), the machine's own credit / bet / win screen with its
// gain count-up and loss beats. docs/re/casino_spec.md 9.6 has the protocol.
//
// The movie drives itself (its scripts run); the game only:
//  * sets the machine page (/:WhatPage smca / smcl / smpo / smc1 / smh1 /
//    sml1), the wallet and the reel count, then plays the root's 'menu' label;
//  * routes the pad to the focus clip (/:RootMenuPath while /:InRootMenu,
//    else /:Clip: A, B, Move / Left / Right, X_Lines, X_Lock) [INF: the
//    pad -> movie routing is encrypted, the method names are the movie's];
//  * answers its FSCommands: smon (play), smpl (spin), smof (back to the
//    menu), sloo (leave the machine), lini / lino / linc (lines manager open /
//    valid / cancel), left / right / lock, dbxo (show a dialog box) [DATA].
class CasinoSlotHud {
public:
    enum class eEvent : uint8_t {
        None = 0,
        Spin,         // smpl: the play button (A) at the active controls
        Leave,        // sloo: quit the machine
        LinesChanged, // lino: the lines manager was validated (/:nbActiveLines)
        Left, // left / right: the movie's hand cursor (the port: the bet)
        Right,
        Lock, // lock: hold (the second chance machines; the port: max bet)
        BackToMenu, // smof: B at the play controls
    };

public:
    bool load();
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    // At a machine: the page code of eMachine (Car, Clothes, Poker, Hawai,
    // Classic, HighLife), its price, reels and the wallet.
    // bMultiLine: the machine activates lines (its help bar offers X).
    void open( int32_t machineIndex,
               int32_t price,
               int32_t reels,
               int32_t chips,
               bool bMaleOutfit,
               bool bMultiLine );
    void close();
    inline bool isOpen() const { return bOpen; }

    // Pad / keyboard -> the movie (and the native dialog box).
    void routeInput();
    void tick( float deltaTime );
    void draw( Renderer2D &renderer, const Font *pFont );
    std::vector<eEvent> takeEvents();

    // The movie's state the game reads back.
    int32_t getActiveLines();
    // True while the active controls (the spin button) are up.
    bool isPlaying();

    // Cas_SlotScreen.vmf: the credit / bet values, the spin start (the loss
    // beat takes the stake) and its result (the gain count-up, or not_win).
    void setScreenValues( int32_t chips, int32_t wager );
    void screenSpinStarted( int32_t chipsBefore, int32_t wager );
    void screenResult( int32_t payout );

    // The error frame of the movie: 1 no money, 2 no money left (bring more),
    // 3 too many lines for the wallet [DATA: the 'error' label].
    void showError( int32_t errorType );
    // The second chance step of the Poker / Beach machines: the root's
    // 'relaunch_step' (currentStep 2, the step-2 help bar sm_poker_2 /
    // sm_hawai1_2) and back to the play controls [DATA: the labels].
    void enterRelaunch();
    void leaveRelaunch();

    // The machines' own screens (Entity__Actor__CasinoFlashScreen): the
    // movies drawn into render targets in the offscreen phase - the played
    // machine's live Cas_SlotScreen, one idle Cas_SlotScreen shared by the
    // other machines and the Cas_SlotLines overlay. -slotscreenpanel keeps
    // the old 2D panel instead.
    void drawOffscreen();
    GPUTexture *getPlayedScreenTexture() const;
    GPUTexture *getIdleScreenTexture() const;
    GPUTexture *getLinesTexture() const;
    inline uint64_t getTextureStamp() const { return textureStamp; }
    inline bool usesScreenTargets() const { return bScreenTargets; }

private:
    void onCommand( const std::string &command, const std::string &args );
    void sendToFocus( const char *pMethod, const char *pMoveArg );

    FlashPlayer machine;    // Cas_SlotMachine.vmf
    FlashPlayer screen;     // Cas_SlotScreen.vmf
    FlashPlayer idleScreen; // Cas_SlotScreen.vmf at rest (the other machines)
    FlashPlayer lines;      // Cas_SlotLines.vmf
    bool bIdleLoaded = false;
    bool bLinesLoaded = false;
    bool bScreenTargets = false;
    RenderTarget *pScreenTarget = nullptr;
    RenderTarget *pIdleTarget = nullptr;
    RenderTarget *pLinesTarget = nullptr;
    uint32_t idleFrames = 0;
    uint64_t textureStamp = 0;
    void drawMovieInto( FlashPlayer &player,
                        RenderTarget *&pTarget,
                        uint32_t width,
                        uint32_t height,
                        const char *pName );
    bool bLoaded = false;
    bool bScreenLoaded = false;
    bool bOpen = false;
    bool bMultiLine = false;
    bool bRelaunchStep = false; // the second chance step is up
    std::vector<eEvent> events;

    // The movie's dialog box (DBXO -> FSCommand dbxo): drawn natively, the
    // answer goes back through /:PlayerAnswer and DialogBox() [DATA].
    std::string playHelp; // the help bar frame of the play controls
    std::string pendingHelp;
    bool bDialog = false;
    bool bDialogYesNo = false;
    std::string dialogText;
};
