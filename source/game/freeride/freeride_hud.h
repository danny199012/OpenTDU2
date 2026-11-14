#pragma once

#include "audio/radio_player.h"

#include "filesystem/bank.h"
#include "flash/flash_definitions.h"
#include "flash/flash_player.h"
#include "render/renderer_2d.h"
#include <deque>
#include <functional>
#include "game/car_shop.h"
#include "game/freeride/risk_driving.h"

class Renderer2D;
class VehicleWorldCollision;

// FREERIDE overlay (docs: /tmp/claude-0/spec_freeride.md 4).
//   - HUD.vmf (frontend/hires/hud.bnk, GamePC.cpr AddFlashMovie "HUD" 1):
//     the root `default` frame hides every HUD clip; the game shows the
//     root clip HUD and its minimap frame in FREERIDE [INF: the visibility
//     calls are encrypted].
//   - GENERAL.vmf pop_goal: /:GoalTxt = loc 5038 "Enjoy!", goto "open"
//     (FUN_00d61790, 30 s auto close; FUN_00d61700 close_popup).
//   - GAUGES.vmf (frontend/hires/gauges.bnk) + the car gauge bank
//     frontend/hires/gauges/<_hud_file_name>.bnk (hud.ini Style / AddHud,
//     ext_rpm / int_* dial textures, needle.2db), fed like FUN_00965810 /
//     FUN_00967f10 (docs: /tmp/claude-0/spec_freeride.md 4b).
//   - Minimap content: drawn by the engine with GamePC.cpr [HudMap] colours
//     ([ENC] drawing, road geometry [INF]): here the road surfaces of the
//     sector -R.shk files, heading up.
//   - Zone badge: DBCruisingZones i1 / i2 -> loc 3336 / 3337 ("Ibiza Area
//     1 / 2"); the zone lookup is [ENC] (spec 4) [INF: zone 2 east of area 4].
class FreeRideHud {
public:
    struct State {
        float SpeedKmh = 0.0f;
        float Rpm = 0.0f;
        float RedLine = 8000.0f;
        float RpmMax = 8500.0f;
        int32_t Gear = 0; // -1 R, 0 N
        float OdometerKm = 0.0f;
        float CarX = 0.0f, CarZ = 0.0f;
        float HeadingX = 0.0f, HeadingZ = 1.0f; // car forward on the ground
        int32_t CameraMode = 20; // CamerasInGame mode (camera +0x2e8)
    };

public:
    // hudFileName: DBCarPhysicsData _hud_file_name (gauge bank).
    bool load( const std::string &hudFileName );
    void unload();

    void showGoal( const std::string &text );
    void closeGoal();
    // BACK_ON_TRACK hold indicator: the HUD movie's own element
    // /HUD/ig_menu/backontrack (sprite 1019: "default" frame 0, the "load" fill
    // frames 7..17). progress in [0, 1] shows the element and fills it, a
    // negative value hides it (the retail element fills while the button is
    // held; the reset itself is done by the game mode).
    // The return-to-road indicator (/HUD/enter_hold, the circular ring): the
    // fill sweep 0..1 from the hold progress, < 0 hides; bLocked drives the
    // ic 'lock' state (the red X) instead of 'on' (the blue fill).
    void showBackOnTrack( float progress, bool bLocked = false );
    // GPS turn arrow (the HUD movie's /HUD/arrow_gps; the retail update at
    // 0x937693..0x9377cc): arrow 0..15 = /:ArrowDirection 'up' 'rightup'
    // 'right' 'rightdown' 'down' 'leftdown' 'left' 'leftup' (+8: the '_ra'
    // roundabout set, /:IsRoundAbout), 16 = 'empty'. On a change the clip
    // plays 'open' (from empty), 'change', or 'close' (to empty).
    void showGpsArrow( int32_t arrow );
    // Car radio panel (the HUD movie's own /HUD/radio element): line 1 the
    // station name (/:RadioTitle), lines 2 / 3 the artist and the title (the
    // radio_singer / radio_track scrollers), the station logo and the volume
    // bar (the #4 fill). The logo is the movie's own radio_logo sprite for the
    // retail stations (its 'hariba' / 'kalua' / 'roadtrack' labels, one per
    // station) or, for a custom folder station, its art (Station::IconPath,
    // drawn over the panel in the sprite's box while the sprite is hidden).
    // bVisible false slides it out.
    void showRadio( bool bVisible, const RadioPlayer::Station &station, const std::string &artist,
                    const std::string &title, float volume );
    // In-car "Accessories" menu (the HUD movie's /HUD/ig_menu, driven like
    // the retail controller FUN_00928c10 / FUN_00928e70): one entry per D-pad
    // direction. The movie's own SetIcons (ig_menu frame 0 script, run by the
    // 'open' timeline) places them [DATA]: icon_1 up = 'hood' (roof), icon_2
    // right = 'clir' (right indicator), icon_3 down = 'wind' (windows),
    // icon_4 left = 'clil' (left indicator); an icon's red 'lock' shows when
    // /:HasHood / /:HasWindows is false. kCarMenuEntries is in that order
    // (eCarMenuEntry).
    struct CarMenuEntry {
        const char *pLabel; // the icon sprite's label (the movie protocol)
        const char *pName;  // display name (the valid_txt bar)
    };
    enum eCarMenuEntry : int32_t { CME_Up = 0, CME_Right, CME_Down, CME_Left };
    static constexpr CarMenuEntry kCarMenuEntries[4] = { { "hood", "Roof" },
                                                         { "clir", "Indicators right" },
                                                         { "wind", "Windows" },
                                                         { "clil", "Indicators left" } };
    // Opens (/:HasHood, /:HasWindows, /:HasTurnSignals then the 'open'
    // label, FUN_00928840 + FUN_00928c10) or closes ('close') the menu.
    void showCarMenu( bool bVisible, bool bHasHood = true, bool bHasWindows = true );
    // An entry was applied: the movie's ValidIGM(label) shows the entry over
    // valid_icon and slides the menu out (FUN_00928e70).
    void validateCarMenu( int32_t entry );
    // The world spot element (HUD.vmf /HUD/enter, the retail spot icon over
    // spots and entrances): the icon tile is the Spot child, one frame label
    // per spot kind ("clothes", "carDealer", "event", "estates", "offroad"...
    // 751 frames of authored art [DATA: -dumphud timeline]); the prompt under
    // it reads /:m_Enter. canvasX / canvasY place the element at the spot's
    // projected screen position (canvas px, 1280 x 720), scalePercent shrinks
    // it with the distance. text empty shows the tile alone.
    void showEnterSpot( const char *pSpotKind, const std::string &text, float canvasX, float canvasY, float scalePercent,
                        bool bLocked = false );
    void closeEnterSpot();
    // The satnav's ENTER state (the retail capture beside a spot): the map
    // closes in, a header band reads ENTER; the spot tile + button glyph sit in
    // the bottom strip (showEnterSpot, placed by the caller at minimapEnterStrip).
    void setMinimapEnter( bool bEnter, const std::string &label = std::string() );
    // The strip's tile anchor in canvas px (the tile centre) and its scale.
    void getMinimapEnterStrip( float &outX, float &outY, float &outScalePercent ) const;
    // Distant markers: clones of the whole element with the prompt hidden
    // (retail shows the same tile above spots down the road). The markers are
    // re-described every frame (kind + projected position); up to 8.
    struct WorldMarker {
        const char *pSpotKind;
        float CanvasX, CanvasY;
        float ScalePercent;
        int32_t LogoIndex = -1; // a dealer tile's brand (DBBrands _logo_global_index)
    };
    static constexpr int32_t kMaxWorldMarkers = 8;
    void setWorldMarkers( const WorldMarker *pMarkers, int32_t count );
    // The Hint box (GENERAL.vmf /pop_tutorial, the retail tutorial popup
    // FUN_00d62ed0 also used by the walk mode): /:TutText + "open_tuto",
    // closed with the movie's close_popup. autoHide < 0 stays until
    // closeHint.
    void showHint( const std::string &text, float autoHide = -1.0f );
    void closeHint();
    inline bool isHintActive() const { return hintTimer_ >= 0.0f; }
    // The race countdown (HUD.vmf /_3_2_1_go, the retail big digits): 3, 2, 1
    // then 0 = "GO!". A negative number hides it.
    void showCountdown( int32_t number );
    // Chase mode HUD (police): the infringement gauge
    // /HUD/gauge_content/gauge (sprite 502, the retail bar that fills with
    // the traffic violations: icon + bar in the 'infringement' mode, label
    // /:m_Infraction; in a chase the same gauge in its 'escape' mode fills
    // with the break-away), the arrest countdown /HUD/chase_timer ("open",
    // digits updated per frame) and the root switches /:IsPolice /
    // /:IsUnderArrest.
    void setChaseHud( bool bChase, float awareness01, float escape01, bool bTimerVisible, float timerSeconds,
                      bool bArrested );
    // The Chase Mode help popup (the retail "Help: Chase Mode" pages): the
    // HUD root function OpenChaseModeHsys with /:m_HelpSytem_ / /:m_ChaseMode_
    // and the page texts /:m_TutoChaseMode1..4 set; the movie closes itself
    // (FSCommand chmo:hsys:clos). While it is open the game routes the pad to
    // it (routeChaseHelpInput) and the world waits (tickModal).
    void openChaseHelp();
    inline bool isChaseHelpOpen() const { return bChaseHelpOpen_; }
    // The help system's controls: validate -> _root.Clip.A(), back -> B(),
    // left / right -> Move('left' | 'right') (the casino help protocol).
    void routeChaseHelpInput();
    // Advances the HUD movie alone (a modal popup is up, the world waits).
    void tickModal( float deltaTime );
    // The race timer readout (top centre): the running seconds and the
    // checkpoint count. The retail race drives the /HUD/time element (the
    // InitTimer / Xtimer strings of the movie); its protocol is not decoded,
    // the text here is the placeholder [INF].
    void setRaceTimer( bool bVisible, float seconds, int32_t checkpoint, int32_t count );
    // The retail race readouts of HUD.vmf [DATA: the clips, their labels and
    // their text variables; the feed itself is ENC, INF which value goes
    // where]: /HUD/time ("open"; /:Min /:Sec /:Cent), /HUD/checkpoints
    // ("open" / "chekpoint" / "stop"; /:NbCheckpoints of /:TotalCheckpoints,
    // the split /:CheckTime, the gap box chck_box "red" / "green" with /:Gap,
    // the "Last lap" banner last_lap "open" / "close" with /:m_LastLap_) and
    // /HUD/laps ("open"; /:NbTurns of /:TotalTurns).
    struct RaceHud {
        bool bVisible = false;
        float Seconds = 0.0f;
        int32_t Checkpoint = 0; // 1-based, the one being driven to
        int32_t Checkpoints = 0;
        int32_t Lap = 0;        // 1-based; Laps <= 1 hides the lap counter
        int32_t Laps = 0;
    };
    void setRaceHud( const RaceHud &race );
    // The race position (HUD.vmf /HUD/position: 'open', the cart's mode icon
    // 'race', /:Pos + /:Exp the rank and its ordinal suffix, /:NbPlayers the
    // field) [DATA: the movie's elements]. position <= 0 hides it.
    void setRacePosition( int32_t position, int32_t racers );
    // A checkpoint was passed: its race time, and the gap to the previous
    // lap's time at the same gate when there is one (negative = ahead).
    void showCheckpointPassed( float seconds, bool bHasGap, float gap );
    void showLastLap();
    // The minimap's own objective icons (/HUD/minimap/objectif_01..08, hidden
    // at rest; frames 'checkpoint' f1, 'finish' f10, 'start' f17, 'radar_on'
    // f24 ... [DATA]): placed on the heading-up map at world positions, an
    // icon beyond the map edge parked on the edge towards it [INF]. Call
    // after tick() (the movie tick re-applies its own placements).
    struct MinimapObjective {
        Vector3f Position;
        const char *pLabel = "checkpoint";
    };
    static constexpr int32_t kMaxMinimapObjectives = 8;
    void setMinimapObjectives( const MinimapObjective *pObjectives, int32_t count );
    void tick( float deltaTime, const State &state );
    void draw( Renderer2D &renderer, const VehicleWorldCollision *pCollision );
    // Saving logo while the pause menu / map hide the HUD.
    void tickSavingLogo( float deltaTime );
    // Risk driving state shown while money is pending / just banked. Consumes
    // the one-shot event flags of the source (one HUD jump per event).
    void setRisk( RiskDriving &risk );
    // FSCommand2 frgp:* of the HUD movie (the FRIM bank animation asks the
    // game to pay with "bank"); wired to RiskDriving::onBankFSCommand.
    inline void setBankCommandCallback( std::function<void( const std::string & )> callback ) {
        bankCommandCallback_ = std::move( callback );
    }
    // The player confirmed the bank (A): play the pay segment of the
    // fr_gge/bank animation (its label "bank", frame 160 [DATA]).
    void playFrimBank();
    // Debug (-logdbprogress): log the timelines of the GENERAL clips matching a name.
    void dumpGeneral( const char *pFilter );
    // Level-up popup (GENERAL.vmf db_progress): grade 0..3 = racing / cruising /
    // collection / social, the new level text "n/15" and the title.
    // The four skills (racing / cruising / collection / social) as the popup
    // gauges read them, plus the global level.
    struct ProgressSnapshot {
        std::string LevelCount[4]; // "n/15"
        std::string Title[4];
        int32_t Percent[4] = { 0, 0, 0, 0 }; // gauge ring 0..100
        int32_t NextLevelIn[4] = { 0, 0, 0, 0 }; // points to the next level
        int32_t GlobalLevel = 0;
        std::string GlobalTitle;
        std::string GlobalLevelCount; // "n/73"
    };
    void setProgress( const ProgressSnapshot &snapshot ) { progress_ = snapshot; }
    // The retail popup: GENERAL.vmf _root.dbProgress( action ) with the
    // WhatGrade / WhatDbProgress / NewGradeTitle ... variables [DATA]; mini bar,
    // then extended with the gauges (and the new title + reward when bNewTitle).
    // newTitle / tier (1..3): the title reached and its reward tier (the reward
    // icon frame <grade>_<tier>), used by the new-title page.
    // Popups are queued: one at a time, in order (an earn popup, then its level up).
    void showLevelUp( int32_t grade, const std::string &reward, bool bNewTitle, const std::string &newTitle = std::string(),
                      int32_t tier = 1, const std::string &rewardDesc = std::string(),
                      const std::string &globalTitle = std::string() );
    // "Roads discovered  +40 Pts": the points popup (the header TitleDbProgress and
    // NbPointsWon of the same movie) with the current gauges.
    void showEarn( int32_t grade, const std::string &eventName, int32_t points );
    // The A button (validate): advances the popup page now.
    void acceptLevelUp();
    inline bool isLevelUpActive() const { return levelUpStage_ > 0; }
    // The GPS route (world metres) drawn on the minimap; null = none.
    // Metal detector readout: metres to the nearest unfound wreck (< 0: out of range).
    // A one-line prompt at the bottom of the screen ("" hides it).
    inline void setPrompt( const std::string &text ) { prompt_ = text; }
    // Whether a prompt is up: the world-spot layer clears the line every frame,
    // so a status line (the fuel Breakdown) has to be re-set after it.
    inline bool hasPrompt() const { return !prompt_.empty(); }
    // The fuel bar's values (drawFuel): the tank in litres, its capacity, what is
    // left in km and the grade (0 basic, 1 v-power).
    inline void setFuel( float litres, float capacity, float rangeKm, int32_t grade ) {
        fuelLitres = litres;
        fuelCapacity = capacity;
        fuelRangeKm = rangeKm;
        fuelGrade = grade;
    }
    inline void setDetector( float meters, float range ) {
        detectorMeters_ = meters;
        detectorRange_ = range;
    }
    inline void setRoute( const std::vector<Vector3f> *pRoute, float lengthMeters = 0.0f ) {
        pRoute_ = pRoute;
        routeLength_ = lengthMeters;
    }
    void drawSavingLogo( Renderer2D &renderer );
    void setVisible( bool bVisible ) { bShown = bVisible; }
    // The near-car spec card (a traffic car stopped next to / a showroom
    // display car walked up to): the compact retail panel - name, price and
    // the three numbers (top speed / 0-100 / bhp). closeCarSpecCard hides it.
    // drawCarSpecCard is public: the interior walk draws it over the showroom
    // (the drive HUD itself is hidden inside the interiors).
    void showCarSpecCard( const std::string &name, int32_t price, const CarSpec &spec );
    inline void closeCarSpecCard() { bCarSpecCard_ = false; }
    void drawCarSpecCard( Renderer2D &renderer );
    // Test drive (mode 7 TESTCAR, FUN_00a12300(1) / FUN_00a11120): /:IsTest
    // and the /HUD/time_cd countdown clip ("open"), digits /:MinCd /:SecCd
    // /:CentCd "%02d" ms -> min / s / cent [CODE].
    void setTestDrive( bool bEnable );
    void setCountdown( float remaining );
    // A completely clean frame for the photo camera ("Take photo"): nothing of
    // the HUD is drawn, not even the goal bar / progress clip that survive
    // setVisible(false) (they are the /pop_goal and /db_progress clips of the
    // GENERAL movie).
    inline void setHidden( bool bHide ) { bHidden = bHide; }
    // The 3D cockpit needle angles (degrees) of the last tick, mapped from the
    // car's `interior_kmh` / `interior_mph` / `interior_rpm` tables; 0 when the
    // gauge bank has no interior table (the model then keeps its rest pose).
    inline float getInteriorSpeedNeedleAngle() const { return interiorSpeedNeedleAngle; }
    inline float getInteriorRpmNeedleAngle() const { return interiorRpmNeedleAngle; }

private:
    FlashPlayer hud;
    FlashPlayer general;
    bool bSavingLogo = false;
    bool bHidden = false;
    State state;
    // Fuel (set by GMFreeRide from the tank of the car).
    float fuelLitres = 0.0f;
    float fuelCapacity = 0.0f;
    float fuelRangeKm = 0.0f;
    int32_t fuelGrade = 0;
    float goalTimer = -1.0f;
    bool bShown = false;
    bool bBackOnTrackShown = false;
    bool bBackOnTrackLocked = true; // the ic 'lock' state (the red X) shown —
                                    // true so the first show forces the 'on' goto
                                    // (the 'default' frame hides the fill)
    bool bRadioShown = false;
    int32_t gpsArrow_ = 16; // [+0xbc], 16 = empty
    bool bCarMenuShown = false;
    std::string zoneShort;
    std::string zoneName;
    const std::vector<Vector3f> *pRoute_ = nullptr;
    float routeLength_ = 0.0f;
    std::string prompt_;
    float detectorMeters_ = -1.0f, detectorRange_ = 250.0f;
    // Near-car spec card (showCarSpecCard).
    bool bCarSpecCard_ = false;
    std::string carSpecName_;
    int32_t carSpecPrice_ = 0;
    CarSpec carSpec_;
    // World spot element + markers.
    std::string enterSpotKind_;
    bool bEnterShown_ = false;
    struct MarkerClone {
        bool bMade = false;
        std::string Kind; // the art frame the clone shows ("" = none yet)
        int32_t LogoIndex = -2; // the brand logo bound (-1 none, -2 not set yet)
    };
    MarkerClone markerClones_[kMaxWorldMarkers];
    bool bMarkersVisible_ = false;
    // Hint box / countdown / chase UI.
    float hintTimer_ = -1.0f;
    int32_t countdownNumber_ = -1;
    bool bViolationGaugeShown_ = false;
    std::string violationGaugeMode_;  // 'infringement' / 'escape' (the label the icon + bar show)
    uint32_t violationGaugeFrame_ = ~0u;
    float violationGaugeHide_ = 0.0f; // seconds until the closed gauge clip is hidden
    float chaseHelpHide_ = 0.0f;      // seconds until the closed help popup clip is hidden
    bool bChaseTimerShown_ = false;
    float chaseTimerHide_ = 0.0f; // seconds until the closed chase_timer clip is hidden
    std::string chaseTimerText_;
    bool bArrestedShown_ = false;
    bool bChaseHelpOpen_ = false;
    bool bRaceTimerShown_ = false;
    std::string raceTimerText_;
    bool bRaceHudShown_ = false;
    bool bRaceLapsShown_ = false;
    std::string raceTimeText_;
    int32_t raceCheckpointShown_ = -1;
    int32_t raceLapShown_ = -1;
    int32_t racePositionShown_ = -1;
    int32_t raceRacersShown_ = -1;
    const char *minimapObjectiveLabel_[kMaxMinimapObjectives] = {};
    bool bMinimapObjectiveShown_[kMaxMinimapObjectives] = {};
    // A canvas point (1280 x 720) to the /HUD/minimap clip's local space.
    bool canvasToMinimap( float canvasX, float canvasY, float &outX, float &outY );
    ProgressSnapshot progress_;
    int32_t levelUpStage_ = 0; // 0 none, 1 mini, 2 extended
    float levelUpTimer_ = 0.0f;
    std::string levelUpReward_;
    bool levelUpNewTitle_ = false;
    int32_t levelUpPresses_ = 0;
    std::string levelUpTitle_;
    std::string levelUpGlobalTitle_;
    std::deque<std::function<void()>> popupQueue_;
    void startLevelUp( int32_t grade, const std::string &reward, bool bNewTitle, const std::string &newTitle, int32_t tier,
                       const std::string &rewardDesc, const std::string &globalTitle, bool bLevelUp, const std::string &header,
                       const std::string &pointsText );
    // FRIM gauge (hud.bnk /HUD/FRG, the retail protocol FUN_00a75df0 /
    // FUN_00a74d90 [CODE]): FRG_on / FRG_off on /HUD/FRG, the root vars
    // PerfPts / PerfPtsTarget / LastPts / PALevel..., new_pts on /HUD/FRG/FR
    // per event, "on" on /HUD/FRG/FR/<type> (drift / wheeling / dodge / jump).
    int32_t frimLevel_ = 0;
    int32_t frimProgress_ = 0;
    int32_t frimThreshold_ = 1000;
    int32_t frimLastPoints_ = 0;
    int32_t frimMoney_ = 0;    // moneyTable[level]: what banking now pays
    int32_t frimNextMoney_ = 0;
    int32_t frimTotal_ = 0;    // the chain total (PATotal)
    bool bFrimBanking_ = false; // a level-up bank animation is due
    bool bFrimBankPending_ = false;
    std::string frimPerfType_;
    bool bFrimShown_ = false;
    bool bFrimAvailable_ = false;
    bool bFrimNewPerf_ = false; // one events copied by setRisk (it consumes them)
    bool bFrimHit_ = false;
    int32_t frimProbeFrames_ = 0;
    std::function<void( const std::string & )> bankCommandCallback_;
    void updateFrim();
    int32_t zoneIndex = 0;

    void dumpClips( const FlashPlayer::Clip &clip, const std::string &path, int32_t depth );
    // GAUGES.vmf and the car gauge bank.
    struct NeedleTable { // AddHud: piecewise linear value -> angle (FUN_00968fe0)
        std::vector<float> Values, Angles;
        float map( float value ) const;
    };
    FlashPlayer gauges;
    FlashDefinitions gaugeDefinitions; // shape geometry of the needle / dial frame
    Bank gaugeBank;
    std::string gaugeStyle;
    std::unordered_map<std::string, NeedleTable> needleTables;
    GPUTexture *pDialTexture = nullptr;   // ext_rpm.2db
    GPUTexture *pNeedleTexture = nullptr; // needle.2db
    float needleAngle = 0.0f;
    // Angles (degrees) of the 3D cockpit needles, mapped from the car's
    // `interior_kmh` / `interior_mph` / `interior_rpm` tables. GMFreeRide feeds
    // them to CinematicCar::setDashboardNeedles: the interior model's needle
    // meshes are real nodes (docs/re/needles_spec.md).
    float interiorSpeedNeedleAngle = 0.0f;
    float interiorRpmNeedleAngle = 0.0f;
    bool loadGaugeBank( const std::string &hudFileName );
    void updateGauges();
    void drawGaugeTextures( Renderer2D &renderer, bool bDial );
    void beginGaugeStage( Renderer2D &renderer ) const;
    // The stage destination rect of a movie's screen ratio mode: the gauge dial
    // and the radio station art draw their own textures over a Flash stage.
    void beginStageRect( Renderer2D &renderer, const FlashPlayer &player ) const;
    // The custom station art of the radio panel (loaded from the station's
    // IconPath, drawn in the movie's logo box).
    void setRadioIcon( const std::string &path );
    void releaseRadioIcon();
    void drawRadioIcon( Renderer2D &renderer );
    // The box a sprite occupies, in the stage's own units (twips / 20): the
    // union of the shapes of its current frame (the radio logo box, the
    // minimap's map surface).
    bool clipBox( FlashPlayer &player, const std::string &path, float &outX, float &outY, float &outWidth,
                  float &outHeight );
    // The box /HUD/radio/radio_logo occupies, in the stage's own units
    // (twips / 20): the union of the shapes of the sprite's current frame.
    bool radioLogoBox( float &outX, float &outY, float &outWidth, float &outHeight );
    GPUTexture *pRadioIcon = nullptr; // station art (owned here)
    std::string radioIconPath_;
    float radioIconAspect_ = 1.0f; // width / height
    bool clipMatrix( FlashPlayer &player, const std::string &path, FlashMatrix &outMatrix, uint16_t &outCharacter );
    bool clipMatrix( const std::string &path, FlashMatrix &outMatrix, uint16_t &outCharacter );
    void drawGauge( Renderer2D &renderer );
    // The GENERAL movie's own clips (goal bar, hint box, level-up popup, saving
    // logo). drawClipOnly ignores a clip's visibility, so each one is gated on
    // the state that opens it.
    void drawGeneralClips( Renderer2D &renderer );
    // The minimap (the satnav): the map content renders UNDER the HUD movie,
    // into the movie's own map surface (/HUD/minimap/mmap_lure — the solid
    // quad the retail engine draws its map into; hidden here, the movie's
    // bezel / band / status icons draw over the map).
    void drawMinimap( Renderer2D &renderer, const VehicleWorldCollision *pCollision );
    // The map surface rect in canvas px: the mmap_lure box mapped through the
    // stage's screen-ratio rect (the same mapping beginStage draws with).
    bool computeMapRect();
    float mapMetersPerPixel() const;
    bool bMinimapEnter_ = false;
    std::string enterLabel_;
    float minimapZoom_ = 1.0f;
    float mapRectX = 115.0f, mapRectY = 454.0f, mapRectW = 190.0f, mapRectH = 190.0f;
    // Stage dimensions of the HUD movie (the FlashCanvasToStage target).
    float hudStageWidth() const;
    float hudStageHeight() const;
    // The screen-ratio arrangements of the HUD / GAUGES movies (the /HUD
    // label and the GAUGES /:WhatFormat), applied at load and re-run when the
    // Screen ratio option changes (appliedScreenRatio_ tracks the live one).
    void applyScreenRatio();
    int32_t appliedScreenRatio_ = 0;
    // The minimap's road + route vertex list, rebuilt at ~20 Hz when the car,
    // heading, route or map rect changed (session 73 `cline`, re-applied after
    // the d92c85e merge dropped it: the per-frame rebuild was ~9 ms).
    struct MinimapCache {
        std::vector<Vertex2D> Vertices;
        uint32_t Frame = 0, BuildFrame = 0;
        float CarX = 0.0f, CarZ = 0.0f, HeadingX = 0.0f, HeadingZ = 0.0f;
        float Zoom = 1.0f;
        float RectX = 0.0f, RectY = 0.0f, RectW = 0.0f, RectH = 0.0f;
        size_t TriangleCount = static_cast<size_t>( -1 );
        const VehicleWorldCollision *pCollision = nullptr;
        const std::vector<Vector3f> *pRoute = nullptr;
        size_t RouteSize = 0;
        float RouteLength = -1.0f;
        bool bValid = false;
        // The road triangles (world space, 3 points each) of the last
        // VehicleWorldCollision query, made with a margin so the car can
        // travel half of it before the query (a cell walk + dedupe set,
        // ~5 % of the main thread when run every rebuild) runs again.
        std::vector<Vector3f> RoadTriangles;
        float QueryX = 0.0f, QueryZ = 0.0f, QueryRadius = 0.0f;
        size_t QueryTriangleCount = static_cast<size_t>( -1 );
        const VehicleWorldCollision *pQueryCollision = nullptr;
    } minimapCache_;
    void drawZone( Renderer2D &renderer );
    // Fuel (docs/re/fuel_gas_station_spec.md): a native bar beside the zone badge -
    // the car has no fuel needle of its own (its dashboard is ND_SPD / ND_RPM only)
    // and the HUD movie has no fuel element (there is no `/:` global for it), so
    // the port draws it. -nofuelhud hides it.
    void drawFuel( Renderer2D &renderer );
    void updateSavingLogo( float deltaTime );
};
