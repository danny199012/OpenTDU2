#pragma once

#include "game/freeride/aircraft.h"
#include <unordered_set>
#include "game/freeride/birds.h"
#include "audio/ambience_audio.h"
#include "audio/traffic_audio.h"
#include "audio/vehicle_audio.h"
#include "audio/radio_player.h"
#include "input/gamepad_output.h"
#include "input/wheel_ffb.h"
#include "game/cheats.h"
#include "filesystem/bank.h"
#include "game/avatar/seated_avatar.h"
#include "game/cinematic/cinematic_car.h"
#include "game/cinematic/gps_screen.h"
#include "game/freeride/car_delivery.h"
#include "game/freeride/car_wash.h"
#include "game/clothes_shop.h"
#include "game/freeride/chase_camera.h"
#include "game/freeride/chase_mode.h"
#include "game/freeride/fade_freeride.h"
#include "game/freeride/first_outro.h"
#include "game/freeride/fuel_menu.h"
#include "game/freeride/freeride_hud.h"
#include "game/freeride/gps_guidance.h"
#include "game/freeride/photo_camera.h"
#include "game/freeride/dealer_menu.h"
#include "game/freeride/estate_menu.h"
#include "game/freeride/paint_menu.h"
#include "game/freeride/sticker_shop_ui.h"
#include "game/freeride/tuning_menu.h"
#include "game/freeride/discovery_points.h"
#include "game/freeride/race_events.h"
#include "game/freeride/race_gates.h"
#include "game/freeride/race_opponents.h"
#include "game/freeride/risk_driving.h"
#include "game/freeride/traffic.h"
#include "game/freeride/weather_cycle.h"
#include "game/freeride/car_dirt.h"
#include "game/gm_base.h"
#include "game/spots/mod_props.h"
#include "game/spots/spot_exteriors.h"
#include "game/map_screen.h"
#include "game/pause_menu.h"
#include "game/freeride/championship_menu.h"
#include "game/freeride/license_result_screen.h"
#include "game/freeride/license_test.h"
#include "game/freeride/school_menu.h"
#include "game/freeride/school_screen.h"
#include "game/spots/interior_walk.h"
#include "game/spots/spot_exteriors.h"
#include "physics/vehicle_collision.h"
#include "physics/vehicle_dynamics.h"
#include "render/eden/eden_scene_view.h"
#include "render/eden/eden_weather.h"

// "FREERIDE" (mode 1) entered by story step 2 "First Freeride"
// (FUN_00b10da0 case 3, docs: /tmp/claude-0/spec_freeride.md 1):
//   1. the spot cinematic C_FIRST_OUTRO of the EPH_L3DI house (the
//      California leaves the garage, 14.31 s), requested with FUN_00b99c20
//      and played by FUN_00a7cc70;
//   2. at its end (listener event 3) the game camera comes back
//      (FUN_00ca18b0), "/:IsWalkmode" false, pop_goal loc 5038 "Enjoy!",
//      TUTO_ENJOY, roof opened (FUN_00c3fcc0), timer 90 s;
//   3. when the timer runs out: the white fade plays (the whole story beat,
//      the scenes are cut for now) and the freeride resumes with the step-3
//      goal text 'On the road to "Solar Crown"'. The retail sequence is
//      C_FADE_FREERIDE (the knock at the window, over the gameplay, BEFORE
//      the flash) + C_MEETING_TESS "thedream" (meeting_tess.bnk, 66.7 s,
//      16 shots: the in-car wake up, her phone call, the keys) — data
//      mapped in docs/STATE.md; the built knock scene + placeholder
//      behind -wakeup.
// The player drives the current car (387 California) with VehicleDynamics
// on the streamed island (sector .shk / -R.shk collision), chase camera
// from CamerasInGame.xmb, HUD (FreeRideHud).
// Command line: -gm FREERIDE starts here; -skipcine starts at the end of
// C_FIRST_OUTRO; -weather <game_weathers name> (default "14" [INF]);
// -storytime <s> overrides the 90 s timer; -wakeup plays the cut scenes
// (the knock + the wake-up placeholder); -island <ibiza|hawai|casino> and
// -tp x,z[,y] with -heading <deg> spawn the car anywhere for touring (the
// house / cinematic flow is ibiza only; the spots are in
// islands/<island>/level/spots.bin, tools_probe/spots_dump.py lists them).
// The spot buildings themselves (dealerships, shops, houses) stream from
// islands/<island>/exterior/<bank>.bnk around the car; -noexteriors turns
// them off (the world viewer's flag).

class GMFreeRide : public GameMode {
public:
    GMFreeRide();
    ~GMFreeRide();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;
    void drawDebug() override;
    // The cheats panel (F4): money, teleport, weather, story step.
    void drawUI() override;

private:
    enum class eState { Outro, Drive, Knock, Fade, Wakeup };

    EdenSceneView view;
    WeatherLibrary gameWeathers;
    // The in-game clock and the weather it drives (weather_cycle.h): the
    // preset pWeather points into gameWeathers (the base presets and their
    // intemperies), and the cycle re-points it as the clock and the states move.
    WeatherCycle weatherCycle;
    const WeatherPreset *pWeather = nullptr;
    // -intempery <n>: the picked preset with that intempery's block resolved
    // over its base (a copy, since the presets vector stays read-only).
    WeatherPreset intemperyWeather;
    void applyForcedIntempery();
    FirstOutro outro;
    FadeFreeride wakeup;
    CinematicCar car;
    CinematicCar::Desc carDesc;
    // The driver (and, in the story, Tess as passenger) seated in the car:
    // avatarsdriver.bnk base clips + the car's seat IK (docs/re/driver_seat_spec.md).
    EdenAvatarLibrary avatars;
    Bank driverBank;
    SeatedAvatar driver;
    SeatedAvatar passenger;
    bool bDriverSeated = false;
    bool bPassengerSeated = false;
    void loadOccupants();
    void unloadOccupants();

    // Story step 4 "Driving Tess" (event TESSEVEN, DBEvent 21; FUN_00b10da0
    // case 5, docs/re/story_spec.md 4 of CamilleKMI/OpenTDU2 335759a): drive Tess
    // from the hotel to the Solar Crown venue. ig2/tesseven.ig2 [DATA] holds the
    // start gate (#0), the finish gate (#1), timG 160 s (target) and CtDw 210 s
    // (time limit, as the licence tests' files). Tess rides as the passenger.
    Ig2Challenge tessEvent;
    bool bTessMission = false;
    float tessTimeLeft = 0.0f;
    bool bTessTimeShown = false;
    Vector3f tessPreviousCar;
    void startTessMission();
    void tickTessMission( float deltaTime );
    void finishTessMission();
    // One-time wirings of this instance (callbacks that capture this mode).
    bool bPuddlesWired = false;
    bool bBankWired = false;
    bool bEventCallbackWired = false;
    // PlayerProfile::DrivenCarId as this mode loaded it (-1: the starter car);
    // a house garage pick changes the profile's, and leaving the house then
    // reloads the mode in the new car.
    int32_t drivenCarIdAtEnter = -1;
    // The car's dynamic dirt / mud / water state (CarDirt): driven in
    // tickSurfaceFx, applied to the paint by CinematicCar::applySurfaceState,
    // emptied by the car wash.
    CarDirt carDirt;
    GpsScreen gpsScreen;         // the dashboard sat-nav's map target
    // -stickertest <bankfile>,<index>: bind one sticker pack art texture to
    // the paint materials' USER_TEX slot at spawn (the shader-slot probe of
    // the sticker work, then the static proof).
    std::string stickerTestBank;
    int32_t stickerTestIndex = -1;
    bool bStickerTestBound = false;
    // The driven car's sticker layer: the profile layout
    // (PlayerProfile::OwnedCarStickers) composited onto a canvas and bound to
    // the paint materials' sticker slot; the paint menu's sticker page edits
    // it live (source/game/freeride/sticker_bank.h).
    StickerCanvas stickerCanvas;
    int32_t stickerCanvasCarId = -1;
    bool bStickerCanvasBound = false;
    // The sticker shop's own copy of the car, parked on the room's car place
    // (the retail design studio shows it in the room); the canvas is bound to it.
    CinematicCar *pStickerRoomCar = nullptr;
    bool bStickerRoomCarBound = false;
    bool bStickerPreviewCamera = false; // the place-mode camera cut to the car
    float stickerCamYaw = 0.0f, stickerCamPitch = 0.35f, stickerCamDistance = 6.5f; // its orbit
    VehicleSetup setup;
    VehicleDynamics vehicle;
    VehicleWorldCollision collision;
    VehicleAudio vehicleAudio;
    // The island's ambient beds (islands/<island>/sound/ambience/ambiences.xmb)
    // and the ambient traffic engines / horns (sound/vehicles/generic_high_high).
    AmbienceAudio ambience;
    TrafficAudio trafficAudio;
    std::vector<TrafficAudio::Agent> audioAgents;
    std::vector<Traffic::HornEvent> hornEvents;
    // The pad (DualSense light bar / haptics): chase flash, crash punches, the
    // road feel of this frame.
    GamepadOutput gamepad;
    // The steering wheel (haptic devices only): the rim torque / damper of
    // this frame, a kick on a crash (input/wheel_ffb.h).
    WheelFFB wheelFfb;
    float roadFeel = 0.0f;
    RadioPlayer radio;
    bool bRadioOpen = false;
    bool bCarMenuOpen = false;   // accessories wheel (D-pad left)
    float accessoriesHideTimer = 0.0f; // auto-hide of the radio panel / wheel
    ChaseCamera chase;
    FreeRideHud hud;
    PauseMenu pauseMenu;
    // World map (FrontEnd/HiRes/map.bnk): the retail HUD's MAP action, which
    // Default_KB.xmb / Device*.xmb bind to the pad's *back* button (Select on
    // a DualSense, the Share/View button) and to the keyboard equivalent.
    MapScreen mapScreen;
    // Spoken sat-nav along the map's route (sound/voices/<lang>/gps.bnk).
    GpsGuidance gps;
    // -gpsdrive x,z[,speed] (test): routes to x,z and moves a virtual car
    // along the route at speed m/s (negative: backwards), so every cue fires.
    bool bGpsDrive = false;
    bool bGpsDriveStarted = false;
    Vector3f gpsDriveTarget;
    float gpsDriveSpeed = 25.0f;
    Vector3f gpsDrivePosition;
    Traffic traffic;
    // The island's aeroplanes (game/freeride/aircraft.h): the flight loops of
    // scenarics/packs/world_aircrafts.bnk, flown by the library aircraft of
    // aircrafts.bnk - landing, rolling and taking off on the airport runways.
    // -noaircraft / -logaircraft / -aircraftpark.
    Aircraft aircraft;
    bool bLogAircraft = false;
    // The island's birds (game/freeride/birds.h): flocks of the five sprites of
    // fx/details/birds.bnk, drawn as billboards in the Effects pass.
    // -nobirds / -logbirds / -birdsnear.
    Birds birds;
    bool bLogBirds = false;
    RiskDriving risk;
    DiscoveryPoints discovery;
    DealerMenu dealer;
    ClothesMenu clothesMenu;
    // The realtor (ERE_*), the tuning shops (ETUN_*) and the sticker /
    // paint shops (ESTICK_*): the menus of the spots that have stock
    // (DBHouse / the tuning levels / the car's DBCarsAndColors rows).
    EstateMenu estate;
    TuningMenu tuningMenu;
    PaintMenu paintMenu;
    // The retail sticker shop UI (SHOPSCAR.vmf's stickers page) inside an
    // ESTICK room; -nostickerui keeps the native paintMenu there.
    StickerShopUi stickerShop;
    void bindStickerShop();
    // The sticker editor's car view: the native place mode or the movie's
    // stsh:mcin .. stsh:mcou span.
    bool stickerCarView() const;
    ChaseMode police;
    RaceEvents raceEvents;
    // The buildings of the island (dealerships, shops, houses...): the exterior
    // banks around the car, streamed like the world viewer does (spots.bin).
    SpotExteriors spotExteriors;
    // The streamed buildings' own collision (<bank>_sc_*.shk at each spot
    // frame) already handed to `collision`: bank path + frame index.
    std::unordered_set<std::string> spotCollisionsAdded;
    void addSpotExteriorCollisions();
    bool bSpotExteriors = true; // -noexteriors
    // Our own buildings (docs/re/modding.md): OBJ + MTL + PNG meshes placed from
    // the mods overlay (props.txt), drawn with the spot exteriors. -nomodprops.
    ModProps modProps;
    // The Eden renderer is not ready at enter() (it readies with the first world
    // draw), so the props - and with them the fuel stations' map icons - load on
    // the first tick the renderer exists.
    void ensureModPropsLoaded();
    bool bModPropsTried = false;
    // Fuel (docs/re/fuel_gas_station_spec.md): the tank burns for the distance
    // driven, and the pump screen (retail strings 1635..1645) opens in your house
    // garage - where the only gas pump model of the game is - and at a station
    // placed from the mods overlay (a `fuel` prop).
    FuelMenu fuelMenu;
    float fuelOdometer = 0.0f;
    bool bFuelEmpty = false;
    // The car washes of the island (docs/re/car_wash_spec.md): the ECW_* spots;
    // stopped next to one, validate pays the fee and plays the retail washing
    // cutscene (C_WASHING) through GM_CinViewer. -nowash off.
    CarWash carWash;
    // A at a dealership / clothes shop walks into its interior
    // (interior/i<name>.bnk); the shop menu opens at the seller. -nointeriors:
    // the menu opens straight from the car, as before.
    InteriorWalk interior;
    // Racing schools: the desk's licence menu and the test being driven
    // (docs/re/licenses_spec.md).
    SchoolMenu schoolMenu;   // the port's panel (-nativeschool, or no R_SCHOOL.vmf)
    SchoolScreen schoolScreen; // the retail desk screen
    void openSchool( char letter, const std::string &test );
    LicenseTest licenseTest;
    // The retail result screen of a licence test (RESULTS.vmf 'failed_test' /
    // 'success_test' and its racing school menu: Retry, Next test, Quit).
    LicenseResultScreen licenseResult;
    std::string resultTest; // the test the screen is about
    void openLicenseResult( const std::string &test, bool bPassed, LicenseTest::eResult result, float time,
                            bool bLicenceNew );
    void tickLicenseResult( float deltaTime );
    // Back at the desk of the test's school, on its licence's tests.
    void returnToSchool( const std::string &test );
    // The gates of the race being driven (the retail checkpoint pack's start /
    // checkpoint / finish holograms, game/freeride/race_gates.h).
    RaceGates raceGates;
    bool bRaceGates = true; // -noracegates
    void updateRaceGates();
    // The furniture of a licence test, built once per run: the slalom poles,
    // the DEC2 cone lines and the racing line (-noracingline) with its
    // braking zones (docs/re/licenses_spec.md 10).
    std::string courseMarkersFor;
    void buildCourseMarkers();
    // The AI opponents of a race ('R' files): the championship's DBLicense
    // _bot_n drivers in their own cars (game/freeride/race_opponents.h,
    // docs/re/opponents_spec.md). They stay on the course a few seconds after
    // the result (opponentsHold) before they are removed.
    RaceOpponents opponents;
    float opponentsHold = -1.0f;
    void startRaceOpponents( const std::string &test );
    void tickRaceOpponents( float deltaTime, bool bRunning, float raceTime );
    // Championships of the island: where each is entered (its first race's
    // start gate) and its HUD tile label (licenses.h ChampionshipFamily).
    struct ChampionshipSpot {
        const LicenseInfo *pChampionship = nullptr;
        Vector3f Position;
        std::string Family;
    };
    std::vector<ChampionshipSpot> championshipSpots;
    ChampionshipMenu championshipMenu;
    void loadChampionshipSpots();
    // The championship a race belongs to (nullptr: a licence test).
    const LicenseInfo *findRaceChampionship( const std::string &race ) const;
    // Leaves the shop and puts the car on the test's start gate.
    void startLicenseTest( const std::string &test );
    // Puts the car down at a position / heading (radians) on the ground.
    void placeCar( const Vector3f &position, float heading );
    // The licence test per frame (countdown, gates, HUD, the result).
    void tickLicenseTest( float deltaTime );
    std::string interiorKind; // the map icon kind of the spot entered ("cardealer", "clothes", "hair" ...)
    uint64_t interiorSpotId = 0; // its spots.bin record id
    // The DBCarShop row of a spot (by its _spot_name), the name rule as fallback.
    const CarShop *findSpotCarShop( const std::string &spotBank, uint64_t spotId ) const;
    // Opens the shop menu of a spot kind (dealer, clothes, hairdresser); false
    // for a kind with no menu.
    bool openSpotShop( const std::string &kind, const std::string &spotBank, uint64_t spotId );
    // The driven car in the paint it was bought in (PlayerProfile::OwnedCarPaints).
    void applyOwnedPaint();
    // The sticker layout of the car onto the canvas (applyOwnedPaint's twin
    // for PlayerProfile::OwnedCarStickers; bound in drawOffscreen).
    void applyOwnedStickers();
    // The tuning levels of the driven car (PlayerProfile::OwnedCarTuning, bought
    // at a tuning shop) into the physics: the base setup + the levels, applied to
    // the live vehicle (setupVehicle() applies them at spawn, this re-applies
    // after a shop purchase).
    void applyTuning();
    // "Take photo" of the pause screen (PhotoCamera): a free camera around the
    // car that writes a PNG of the frame; bPendingPhoto waits for the pause
    // menu's out animation before it opens.
    PhotoCamera photoCamera;
    bool bPendingPhoto = false;
    EdenSceneView::Camera camera;
    std::string islandName;       // -island (ibiza | hawai | casino)
    Vector3f tpPosition;          // -tp x,z[,y] spawn override
    float tpYaw = 0.0f;           // -heading <deg>
    bool bTp = false;
    // Entered from the end of C_MEETING_TESS (StoryHandoff): the drive to the
    // Solar Crown starts at the hotel, out of a white flash.
    bool bFromDream = false;
    float dreamWhiteIn = -1.0f;
    eState state = eState::Outro;
    float stepTimer = 0.0f;       // story step timer (this[7], 90 s)
    float firstFreerideTime = 90.0f; // -storytime override of the 90 s
    float fadeTime = 0.0f;
    float backOnTrackHold = 0.0f; // BACK_ON_TRACK button hold (s)
    bool bBackOnTrackDone = false; // one reset per hold
    bool bBackOnTrackRejected = false; // this press is refused: the reset cooldown (the badge's red X)
    bool bBackOnTrackHeld = false;     // BACK_ON_TRACK is down (the press edge)
    float backOnTrackCooldown = 0.0f;  // s left before the next reset is accepted
    // The traffic violation latch (the police chase): a collision stays
    // reported while the two cars touch (the impulse separates them over
    // several frames), so the raw crash flag fired one violation per frame
    // and a single bump filled the awareness meter - "one crash and they
    // chase you", the owner's report. The rising edge counts once; the latch
    // re-arms after the contact has been clear for kCrashClearGrace.
    bool bTrafficCrashLatch = false;
    float trafficCrashClearTime = 0.0f;
    // The "too many traffic violations" warning (loc 2187 / 3158): shown once
    // per fill when the awareness enters the red band, re-armed when the meter
    // falls back into the low band.
    bool bViolationWarnShown = false;
    // The first witnessed violation opens the "Help: Chase Mode" popup
    // (once per profile, PlayerProfile::ChaseHelpSeen) after a short delay.
    bool bChaseHelpPending = false;
    float chaseHelpDelay = 0.0f;
    // The arrest sequence (the police pull-over): seconds into the scripted
    // shot (< 0: idle). The car brakes to a stop and the camera leaves it
    // while the cops close in beside the driver's window.
    float arrestShotTime = -1.0f;
    bool bEnvironmentAssigned = false;
    bool bLoaded = false;
    Matrix4x4f carWorld;
    // C_FIRST_OUTRO (the California leaving the garage): the cinematic drives
    // the car along the .sce PLAYER_CAR track with the physics frozen, so the
    // track velocity (outroVelocity, from FirstOutro::getCarPose) is integrated
    // into outroWheelSpin to roll the rims / tyres (updateWheels) and drives
    // the engine audio (outroRpm / outroGear, the VehicleDynamics laws).
    Vector3f outroVelocity;
    float outroWheelSpin = 0.0f; // rad, rolling angle during the outro
    float outroRpm = 0.0f;       // rad/s engine speed estimate of the track
    float outroSpeed = 0.0f;     // m/s along the track
    int32_t outroGear = 2;       // gearbox gear index (2 = 1st)
    int32_t lastSectorKey = -1;
    // Load-ahead (streamWorld): the previous stream position, for the heading.
    Vector3f lastStreamPosition;
    Vector3f streamHeading;
    bool bHasStreamPosition = false;
    bool bPreloadIdle = false; // the ring is complete until the next sector change
    int32_t framesInSector = 0; // preload waits a moment after a sector change (spawn / teleport)
    std::string shadowConfig;
    // The developer cheats (source/game/cheats.h): the wallet, the teleport
    // list (the island's spots) and the panel's requests.
    Cheats cheats;
    // -tpin x,z[,heading]: teleport once, right after the drive starts (the
    // in-game path, where -tp only moves the spawn).
    bool bTeleportIn = false;
    Vector3f teleportInTarget;
    float teleportInHeading = 0.0f;
    bool bTeleportInHeading = false;
    // The dealership delivery (CarDelivery): the shop whose car position the
    // bought car goes to (its DBCarShop `_spot_name`), and the purchase waiting
    // to be delivered (the menu closes first).
    std::string dealerSpotName;
    int32_t pendingDeliveryCarId = -1;

    // Wheel nodes of the car model (D_RIM_xx and the tire node after it).
    struct WheelNode {
        int32_t RimNode = -1;
        Matrix4x4f RimRest;
        Matrix4x4f TireRest;
        bool bLeft = true;
        bool bFront = true;
    };
    std::vector<WheelNode> wheelNodes;

    void setupVehicle();
    std::string hudFileName() const;
    void startDriving();
    void startKnock();
    void startWakeup();
    // Story step 2 -> 3: C_MEETING_TESS through the cinematic player, then
    // back here (StoryHandoff).
    void startDream();
    void finishWakeup();
    void backOnTrack();
    // The dealership's delivery (CarDelivery): a bought car is parked at the
    // shop's own car position outside and becomes the car the player drives, so
    // leaving the shop puts them in it. switchCar() is the runtime car change
    // (model, sounds, wheels, physics, chase camera) - what enter() does once.
    void deliverBoughtCar( int32_t carId );
    // The free-ride dealer visit through the SHOP mode (SHOPSCAR.vmf): fills
    // FreeRideShopRequest and switches modes; false when the spot is no shop.
    bool enterDealerShop( const std::string &spotBank, uint64_t spotId );
    bool switchCar( const CinematicCar::Desc &desc, const Vector3f &position, float heading );
    // The cheats' teleport: stream the world at the target, drop the car on the
    // surface below it and reset the dynamics / camera (what backOnTrack does
    // for the nearest road). bHasHeading: face that way, else keep the heading.
    void teleportTo( const Vector3f &target, float heading, bool bHasHeading );
    void updateWheels();
    // Wheel spray / dust and the tyre tracks (eden_surface_fx.h): feeds the
    // wetness of the ground from the weather and the contact of every wheel to
    // the surface effects. -nosurfacefx / -surfacefxscale / -surfacefxtest /
    // -logsurfacefx.
    void tickSurfaceFx( float deltaTime );
    void streamWorld( const Vector3f &position );
    void setShadowConfig( const std::string &name );
    Matrix4x4f outroCarWorld();
    // VehicleDynamics::gearRatio from the setup (Ratio[2+i] = Gears[i],
    // Ratio[9] = 1, x FinalDrive): the outro engine estimate runs the same
    // law without stepping the physics.
    float outroGearRatio( int32_t index ) const;
    bool surfaceHeight( float x, float z, float y, float &outHeight ) const;
    // World -> HUD canvas px (1280 x 720) under the current camera; false
    // behind the camera (the world spot markers, FreeRideHud::showEnterSpot).
    bool projectToCanvas( const Vector3f &world, float &outX, float &outY, float &outDistance ) const;
    // The world spot interaction layer of a Drive frame: the nearest spot's
    // enter element (shops / race starts / return to road) and the distant
    // markers, then the input that enters them.
    void tickWorldSpots( float deltaTime, const Vector3f &carPosition, float speedKmh );
};
