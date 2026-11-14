#pragma once

#include "flash/flash_player.h"
#include "game/game_options.h"

// Pause menu of the original frontend: the *root* timeline of
// FrontEnd/HiRes/main.bnk -> MAIN.vmf carries the pause screen. Its labels
// (dumped with -dumplabels, 210 frames @ 30 fps):
//
//   pause_in -> pause_stop        menu in (interactive at pause_stop)
//   pause_back                    back from the options screen
//   pause_out / pause_out_direct  menu out (with / without the animation)
//
// The entries are the root variables /:m_* (texts, set from the database)
// and the /:Lock* flags — a locked entry is grayed out and skipped. The
// game-side entry list mirrors the movie's setSquareMenu() for freeride
// (WhatGameMode 2): resume / game_progress / players / photo / gme /
// options / profiles / exit_game (see pause_menu.cpp). /:IsInOptions tells
// that the options screen (root labels options / help_options / newOptions)
// is up.
//
// A game mode owns one instance: ESC (IA_MenuPause) opens / closes it, the
// mode freezes the world while isOpen() and reacts to takeResult().
class PauseMenu {
public:
    enum class eResult : uint8_t {
        None,
        Resume,
        Options,
        Quit,
        Photo,
        Progress
    };

public:
    // Loads MAIN.vmf and sets the localized entries.
    bool load();
    void unload();
    inline bool isLoaded() const { return player.isLoaded(); }
    inline bool isOpen() const { return bOpen; }
    inline bool isInOptions() const { return bInOptions; }

    // Locks an entry (the original does it per game mode: "quit" is disabled
    // in a race, "options" in a cinematic...).
    void setEntryEnabled( bool bResume, bool bOptions, bool bQuit );
    // The casino's pause list. The retail setSquareMenu branches on /:IsCasino
    // and builds 6 entries for the casino — resume / game_progress / players /
    // options / profiles / exit_game — with no 'photo' and no 'gme' [DATA:
    // docs/STATE.md session 18, retail-pause-casino.png]. Call before or after
    // load().
    void setCasinoMode( bool bCasino );
    // The pause menu's movie (the casino mode pushes its flash globals on it).
    inline FlashPlayer &getPlayer() { return player; }
    void pushProgressData();
    // 'gapr:part:<raci|crui|coll|soci>': the Game Progress page of one skill
    // asks for its numbers.
    void onProgressCommand( const std::string &command );

    // Plays pause_in / pause_out.
    void open();
    void close();

    // Keys and mouse of the menu (call every frame while isOpen()).
    void tick( float deltaTime );
    // The game clock (weather_cycle.h) publishes its time here every minute;
    // refreshClock shows it instead of the PC's clock while set.
    void setClock( int32_t hours, int32_t minutes );
    void draw( Renderer2D &renderer );

    // Reads and clears the last entry chosen by the movie.
    eResult takeResult();

private:
    // Entry list of the movie is rebuilt here (see pause_menu.cpp: the
    // movie's own setSquareMenu() script returns early, so the squares are
    // driven directly: sq0..sq8 of /pause_menu/menu_sq with the state
    // labels open / on / open_lock / on_lock / default).
    void refreshEntries();
    // Validates the selected entry (the 'valid' flash + the result).
    void validateSelected();
    // The options screen is the root's own 'options' sprite (316 frames, the
    // page labels save / audiovideo / help / credits / language / controls /
    // game_settings / display / audio / video / gamma / device...). The
    // movie's LaunchOptions function plays its 'open' label; the game drives
    // the same steps (see pause_menu.cpp) and closes it with 'close'.
    void openOptions();
    void closeOptions();
    // The entry squares of MAIN.vmf are authored as a *vertical column*
    // (sq0..sq8 at menu_sq local (48, 48.5 + 96.3 n) px, menu_sq scaled 0.85
    // at stage (640, 343)): the console frontend lays the icons out as a row
    // across the top instead. The squares are driven by the game anyway, so
    // the row is imposed here (every frame: the state animations write their
    // own matrix back). kEntryRow* below are the tuning constants.
    void layoutEntries();

private:
    FlashPlayer player;
    // The game-clock override (setClock): -1 = follow the world clock.
    int32_t clockOverrideHours_ = -1, clockOverrideMinutes_ = 0;
    bool bOpen = false;
    bool bInOptions = false;
    bool bClosing = false; // pause_out animation playing
    float closeTimer = 0.0f;
    eResult result = eResult::None;
    int32_t selected = 0;        // index into the entry list
    bool bEntriesShown = false;  // squares driven (after pause_in settled)
    bool bOptionsDumped = false; // -logpause: the options page was dumped
    // Pushes the clock digits once the clock sprite is placed (pause_in).
    void refreshClock();
    // The movie owns the menu (its square scripts run): keys / pad go to the
    // movie as SWF key events (its key-press buttons call Move / A / B).
    void tickMovieInput( float deltaTime );
    // The left stick as a D-pad (the retail pad input is the D-pad; a stick
    // should navigate the same way): the direction is emitted on the first
    // frame past the dead zone and repeated while it is held, so a stick works
    // without the 8-way pad.
    enum class eNav : uint8_t { None, Left, Right, Up, Down };
    eNav takeStickNav( float deltaTime );
    eNav stickNav = eNav::None;
    float stickHoldTime = 0.0f;
    float stickRepeatTimer = 0.0f;
    // 'opts:<page>:...' FSCommands of the options pages (values in / out).
    void onOptionsCommand( const std::string &command );
    void onVideoOptionsCommand( const std::string &what );
    std::string videoRowText( int32_t row );
    std::string localized( const char *pName, const char *pFallback );
    // The game's own rows on the options pages (OpenTDU2): one tick after a
    // page opened, the page's retail setParam table is complete and the game
    // appends its rows to it (Display mode on Video, the graphics rows on
    // Gamma), clones a root choicebox row per row and lets the page's
    // setParameters initialize them (extendOptionsRows). The clones are
    // removed when the page closes (removeExtraOptionRows).
    enum class eRowsPage : uint8_t { None, Audio, Video, Gamma, Advanced };
    void extendOptionsRows();
    void removeExtraOptionRows();
    // The panel's game-row slots are p1..p7 (slot 0 is the page's own first
    // row: the Video page's Resolution / the Gamma page's slider). The rows
    // scroll a window of game rows through those slots: Up / Down on an open
    // page moves the focus (the page's own Move law: wrap at the ends), and a
    // focus that walks off the visible game rows shifts the window one row
    // (writeOptionsWindow rebinds setParam[1..7] and re-runs the page's
    // setParameters). Retail needs none of this - its parameter pages fit
    // their panels - so the law only drives where the game added rows.
    static constexpr int32_t kRowSlots = 7;
    void writeOptionsWindow( const char *pPageClip );
    void moveOptionsFocus( int32_t direction );
    int32_t focusSlot = 0;  // the focused panel slot (0 = the page's first row)
    int32_t windowFirst = 0; // the first game row (of the open page's list) shown in slot 1
    // The Driving Controls page (/options/controls_menu, 'opts:cont:'): the
    // movie's list page (content_menu/pc) shows the bindings in 9 view lines
    // and drives its own navigation / bind dialog; the game fills the rows
    // (fillControlsList), feeds the binder (tickControlsPage: the dialog
    // waits for the next raw input, the capture is GSInput's) and persists
    // (Y / Save writes controls.ini; Back restores the opening state).
    static constexpr const char *kControlsPcPath =
        "/options/controls_menu/content_menu/pc";
    void tickControlsPage();
    void fillControlsList();
    bool bControlsPendingFill = false; // bind:open: fill + play the page's 'open'
    int32_t controlsOffset = 0;        // the first row of the 9-line window
    int32_t controlsLastLineView = 0;  // the movie's focused line last tick
    bool bBindCaptureArmed = false;    // the bind dialog is being captured
    std::string controlsBackup;        // the bindings when the page opened
    // LB / RB on an open options page: the previous / next page of the ring
    // audio -> video -> gamma (the pages share one template: close the open
    // one, play the next one's 'open' - its own frame script fires the page's
    // 'open' and the values / game rows follow).
    void cycleOptionsPage( int32_t direction );
    eRowsPage openPage = eRowsPage::None; // the options page currently shown
    // The value text of one of the game's rows (g_extraRows: the defaults
    // kDefaultRows + menu_rows.ini, pause_menu.cpp).
    std::string extraRowText( const char *pCommand );
    // Left / Right on one of the game's rows ('opts:<page>:<cmd>:scpr/scne').
    bool stepExtraRow( const std::string &command, int32_t step );
    void onAdvancedGraphicsCommand( const std::string &what );
    eRowsPage pendingRowsPage = eRowsPage::None;
    bool bRowsExtended = false; // the open page's rows were appended
    bool bOnVideoPage =
        false; // the Video page is up: Y / Triangle opens Advanced settings
    bool bOnControlsPage =
        false; // the Controls page is up (opts:cont:): Y / Triangle too
    bool bPendingVideoClose =
        false; // OK on the video page: step back once 'test' returned
    GameOptions optionsBackup; // the values when the page opened (Cancel)
    bool bMovieDriven = false;
    bool bCasinoMode = false;   // the casino's 6-entry list (setCasinoMode)
    bool bTabRowLifted = false; // menu_sq moved up by kTabRowLift (see tick)
    float fTabRowBaseTY = 0.0f; // its own settled y, twips
    // -logclock: the digit clips' state once the roll animation is over.
    void dumpClockDebug();
    // The clock feeds on the PC's clock (world_clock.h) every tick while
    // open; it rolls the digits only when the minute changed.
    bool bClockShown = false; // the first push ran for this opening
    // The card's money / cars / houses: load() pushes them, tick() refreshes
    // them on change (refreshPlayerPanel) while open.
    void refreshPlayerPanel();
    int32_t panelMoney = -1;
    int32_t panelCars = -1;
    int32_t panelHouses = -1;
    // Per-entry lock, sized to the retail freeride list (see kEntries in
    // pause_menu.cpp: defaults come from the movie's setSquareMenu locks).
    static constexpr int32_t kMaxEntries = 8;
    bool bEntryEnabled[kMaxEntries] = {};
};

// Debug: logs the timelines (labels, placements, actions) of the clips whose
// path contains pFilter (-logdbprogress uses it on the GENERAL movie).
void DumpFlashSubtree( FlashPlayer &player,
                       const char *pFilter,
                       int32_t maxLevel,
                       uint32_t maxFrames );
void DumpFlashFunctions( FlashPlayer &player, const char *pClipPath = nullptr );
// The root timeline itself (its frame scripts), up to maxFrames frames.
void DumpFlashRootTimeline( FlashPlayer &player, uint32_t maxFrames );
