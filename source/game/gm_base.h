#pragma once

class TestDriveGameInstance;

// TDU2 game modes. Names come from the original executable (command line
// switches and "GAME MODE : %s" banner), the ordering is OpenTDU2 specific.
enum class eGameMode : int32_t {
    GM_None = 0,
    GM_Login,       // "LOGIN" (0x14, GMLogin): press start screen (LOG_IN2.vmf)
    GM_VideoBumper, // "VIDEOBUMPER" (0xe): Atari / Eden bumpers
    GM_AvatarChoice, // "AVATAR_CHOICE" (0x20): C_INTRO cinematic, avatar choice
    GM_FreeRide,     // "FREERIDE"
    GM_CarShowcase,  // "CARSHOWCASE"
    GM_CinViewer,    // "CINVIEWER"
    GM_TestCar,      // "TESTCAR"
    GM_FirstWalkMode, // "FIRST_WM" (0x23): story step 1 "First Garage"
                      // (garage.bnk)
    GM_Casino, // "CASINO": the casino frontend (Casino.vmf + the game movies),
               // offline
    GM_AssetViewer, // "ASSET_VIEWER": OpenTDU2 developer tool (browse banks,
                    // label assets)
    GM_WorldViewer, // "WORLD_VIEWER": OpenTDU2 developer tool (free-fly camera
                    // over an island), not a TDU2 mode
    // The story modes of the retail executable (ported from CamilleKMI/OpenTDU2
    // commit 335759a, the "MAJOR Update"; docs/re/upstream_335759a.md).
    GM_Cine,      // "CINE" (0x1f): story cinematics at a spot (steps 3, 5, 6)
    GM_Mission,   // "MISSION" (4): events (story step 4 "Driving Tess", TESSEVEN)
    GM_Shop,      // "SHOP" (3): car / clothes shops (story step 7 "Used Car Dealer")
    GM_FreeDrive, // "FREE_DRIVE": OpenTDU2 developer / sandbox tool (drive any
                  // car on any island), not a TDU2 mode
    // "UI_VIEWER": OpenTDU2 developer tool (the frontend flash movies as data:
    // the display list, the element bounds and the timeline labels, with the
    // movie drawn live), not a TDU2 mode. Appended last so the values of the
    // modes above do not move.
    GM_UiViewer,

    GM_Count
};

const char *GetGameModeName( eGameMode gameMode );
eGameMode GetGameModeFromName( const char *pName );

// Base class of a game mode ("gm_*"). Only one game mode is active at a
// time; switching is requested through TestDriveGameInstance::setNextGameMode.
class GameMode {
public:
    GameMode( eGameMode mode ) : gameMode( mode ), pGameInstance( nullptr ) {}
    virtual ~GameMode() {}

    inline eGameMode getGameMode() const { return gameMode; }

    virtual bool enter( TestDriveGameInstance *pInstance ) {
        pGameInstance = pInstance;
        return true;
    }
    virtual void tick( float deltaTime ) = 0;
    // Offscreen passes (render targets), before the main render pass.
    virtual void drawOffscreen() {}
    virtual void draw() = 0;
    virtual void exit() {}
    virtual void drawDebug() {}
    // ImGui drawn every frame (not only with the F1 debug UI): tool panels.
    virtual void drawUI() {}

protected:
    eGameMode gameMode;
    TestDriveGameInstance *pGameInstance;
};
