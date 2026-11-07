#pragma once

#include "flash/flash_player.h"

#include <string>

class Renderer2D;

// The result screen of a licence test: the retail RESULTS.vmf
// (FrontEnd/HiRes/results.bnk), driven the way the exe drives it
// [DATA: the movie's own scripts, docs/re/licenses_spec.md 9]:
//
//   the game sets the /: variables (TestName, Message, final_score ...) and
//   jumps the root to 'failed_test' or 'success_test'. Those frames register
//   _root.Clip and stop on a thumbs-down / licence panel with an A (OK)
//   prompt; A plays 'menu_rs', where SetRootMenuRS() builds the racing
//   school menu from IsFailed / IsLicensed / LockReplay:
//     failed:            Retry, Next test, Replay, Quit
//     IsLicensed 0:      Next test, Retry, Replay, Quit
//     IsLicensed 1:      Quit, Next test, Retry, Replay
//     IsLicensed 2:      Quit, Replay
//   and ValidMenu() answers the choice with an FSCommand:
//     retrytest -> "rest", nexttest -> "nets", quittest -> "quit",
//     replaytest -> the movie's own 'replay_test' (no replay in the port:
//     locked with LockReplay).
//   B on the menu goes back to the panel (failed_re / success_re).
// The keys reach the movie as SWF key events (Enter 13, Backspace 8, Up 14,
// Down 15), like the pause menu.
class LicenseResultScreen {
public:
    enum class eChoice { None, Retry, Next, Quit };

    struct Info {
        bool bPassed = false;
        // 0: more tests in this licence, 1: the licence was just obtained,
        // 2: no next test (IsLicensed of the movie) [INF: the meaning is
        // read from the menu each value builds].
        int32_t Licensed = 0;
        std::string TestName;    // the .ig2 title (dbna)
        std::string TestDesc;    // the .ig2 description (dbd1)
        std::string Message;     // the result line ("Too slow" ...)
        std::string LicenceName; // "A7"
        float Time = 0.0f;       // seconds
        float Target = 0.0f;     // seconds (timG)
        int32_t TestsDone = 0;
        int32_t TestsTotal = 0;
    };

    bool load();
    void unload();
    inline bool isLoaded() const { return player.isLoaded(); }
    inline bool isOpen() const { return bOpen; }

    void open( const Info &info );
    void close();
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // One shot: the menu choice.
    eChoice takeChoice();

private:
    FlashPlayer player;
    bool bOpen = false;
    eChoice choice = eChoice::None;
    bool bPassed = false;
    bool bReplayBack = false;
    float stickRepeat = 0.0f;
};
