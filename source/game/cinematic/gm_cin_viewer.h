#pragma once

#include "game/avatarchoice/intro_cinematic.h"
#include "game/cinematic/cinematic_car.h"
#include "game/gm_base.h"

// "CINVIEWER": plays one cinematic of a scenarics/cinematics bank in the Ibiza
// party-house frame through IntroCinematic (the same player as the intro and
// the garage), then continues to the next game mode. Developer tool
// (`-gm CINVIEWER -cinbank <bank> -cin <C_NAME>`) and the story cutscenes:
// the request below picks the bank / cinematic / next mode.
struct CinViewerRequest {
    std::string Bank = "scenarics/cinematics/intro/meeting_tess.bnk";
    std::string Cinematic = "C_MEETING_TESS";
    eGameMode Next = eGameMode::GM_FreeRide;
    int32_t StoryStepAfter = -1; // profile story step after the cinematic (-1: unchanged)
    bool bFromStory = false;     // true: continue the story (no developer overrides)
    bool bStory = false;         // the story dream itself: the return keeps the story goal
                                 // (GMFreeRide bFromDream); a mode's own scene (the car wash)
                                 // leaves it false and returns to the plain drive
    // The spot frame by position (the nearest spots.bin record within 60 m, the
    // HousePickX mechanism) instead of the default story house — the car wash
    // plays at its forecourt (Setup::HousePickX, docs/re/car_wash_spec.md).
    // NaN = off.
    float PickX = NAN, PickZ = NAN;
    // The exterior's island and building prefix: the exterior bank becomes
    // islands/<Island>/exterior/<ExteriorPrefix>.bnk. Empty = the command line
    // / the story default (the car wash bank exists on both islands, so the
    // request must follow the island).
    std::string Island;
    std::string ExteriorPrefix;
    // The scene car: the player's current one (bPlayerCar) instead of the
    // bank's default PosCam car (the California). CarActor: the .sce actor
    // that animates it when the bank has no Entity__CinCar (the car wash's
    // PLAYER_CAR - gm_cine passes the same for its scenes); empty = the
    // default lookups.
    CinematicCar::Desc Car;
    std::string CarActor;
    bool bPlayerCar = false;
    // Cinematics of the same bank played after Cinematic, in order (the car
    // wash: C_INTRO_E_1 -> C_WASHING -> C_OUTRO); the end of the last one ends
    // the request (handoff / Next).
    std::vector<std::string> Then;
    // A fee asked after a cinematic (the car wash: after the drive-in
    // C_INTRO_E_1, over the PromptLoop C_LOOP): validate pays Price and the
    // sequence goes on, back leaves (the drive resumes, nothing paid).
    std::string PromptAfter;
    std::string PromptLoop;
    int32_t Price = 0;
    // The in-game in-car part (C_INCAR, NO_CAMERA): the car's cockpit camera
    // and these background loops (the three C_BABE_0x around the car).
    std::string InCar;
    std::vector<std::string> InCarBackgrounds;
    // Requests played after this one, each in its own scene (another bank,
    // another island: the casino seaplane trip, departure then arrival). The
    // last one's Next ends the chain; only it writes the StoryHandoff.
    std::vector<CinViewerRequest> After;
};
CinViewerRequest &GetCinViewerRequest();

// The car pose at the end of a story cutscene, for the next FREERIDE to start
// from (C_MEETING_TESS: the California in front of the hotel). Consumed (bSet
// cleared) by GMFreeRide::enter. bStory marks the dream's own handoff (the
// story goal continues); a mode's own scene hands the car back without it.
struct StoryHandoff {
    bool bSet = false;
    Vector3f Position;
    float Yaw = 0.0f;      // radians, atan2( forward.x, forward.z )
    bool bWhiteIn = false; // the next mode fades in from white
    bool bStory = false;   // the story drive (the Solar Crown goal), not a plain return
    std::string Island;    // the island the drive resumes on ("" = -island / ibiza): the Hawai story scenes
    std::string EntranceSpot; // non-empty: resume at this spots.bin spot's road-side entrance, not Position
};
StoryHandoff &GetStoryHandoff();

// The driven car's live state and the avatar the player wore when FREERIDE
// left for a scene (the owner, 2026-11-04: "the vehicles state and avatar
// such as clothes etc... must be remembered when entering ... all scenes are
// like that pretty much except the todd driving you to the used car dealer").
// GMFreeRide::exit writes it; a scene that shows the player's own car (the car
// wash: CinViewerRequest::bPlayerCar) applies it to its scene car, and
// GMFreeRide::enter puts it back on the car it spawns. The story scenes (GMCine,
// the Todd drive to the dealer) keep their own story car and never read it.
// Every mode switch builds a new GMFreeRide, so before this the roof, the
// windows and the mud / dust / wetness of the body were reset by any scene.
struct VehicleSceneState {
    bool bSet = false;
    std::string CarFile;    // CinematicCar::Desc::FileName of the driven car
    bool bHardtopOpen = false;
    bool bWindowsOpen = false;
    float Mud = 0.0f, Dirt = 0.0f, Hardness = 0.0f, Wetness = 0.0f; // CarDirt
    std::string AvatarConfig; // PlayerProfile::AvatarConfig at the time
};
VehicleSceneState &GetVehicleSceneState();

class GMCinViewer : public GameMode {
public:
    GMCinViewer();
    ~GMCinViewer();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void drawUI() override;
    void exit() override;

private:
    IntroCinematic cinematic;
    CinViewerRequest request;
    bool bLoaded = false;
    bool bEnded = false;
    float endFade = -1.0f; // story end: the white flash out (s), -1 = none
    float time = 0.0f;
    // The tool layer (-cinehud 0|1, -cinepaths; the camera key pauses, the
    // menu left / right step half a second while paused).
    bool bPaused = false;
    bool bCineHud = true;
    bool bTool = true; // the developer viewer (-gm CINVIEWER): the browser and the hud; false for a game / story scene
    bool bCinePaths = false;
    char bankEdit[260] = {};   // the browser's bank / cinematic text
    char cinEdit[128] = {};
    void drawHud();            // the shot timeline + camera overlay
    // The car wash stages (CinViewerRequest PromptAfter / InCar).
    bool bPrompt = false;
    bool bInCar = false;
    bool bCockpit = false;     // the car's mode 23 camera was found
    Vector3f cockpitPosition;  // car space [DATA camerasingame.xmb]
    Vector3f cockpitTarget;
    float cockpitFov = 0.96f;
    float cockpitNear = 0.05f;
    void playNext( const std::string &next );
    void startInCar();
    void updateInCarCamera();
    void loadCockpit();
    void drawPrompt();
};
