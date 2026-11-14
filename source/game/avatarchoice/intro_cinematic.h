#pragma once

#include "filesystem/bank.h"
#include "flash/flash_player.h"
#include "game/avatar/eden_avatar.h"
#include "game/avatar/facefx.h"
#include "game/avatar/speech_audio.h"
#include "render/eden/eden_animation.h"
#include "game/cinematic/cinematic_audio.h"
#include "game/cinematic/cinematic_car.h"
#include "game/cinematic/cine_subtitles.h"
#include "game/cinematic/cinematic_scene.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_scene_view.h"
#include "physics/havok_collision.h"

// Real time cinematics of the new game in the Ibiza party house
// (Scenarics/Cinematics/intro/new_intro.bnk, docs/re/intro_spec.md and
// docs/re/select_character_spec.md): every Entity__Cinematic of the bank
// (C_INTRO "The Dream", C_AVATAR, C_AV_xx, C_xTOy, C_FIESTA_INTRO_xx,
// C_WALKMODE, C_TESS...). Like the AVATAR_CHOICE game mode, two cinematics
// can run at once: the main one (camera, weather, flash and sound messages,
// listener event 7 at its natural end) and a background one (CinProps bit 4:
// no camera; its actors and its Fx only).
class IntroCinematic {
public:
    // Entity__Volume / Entity__Volume__FpSelectable (house-local).
    struct Volume {
        std::string Name;
        uint32_t OfType = 0;            // 0 plane, 1 sphere, 2 box, 3 cylinder, 4/5 cone (FUN_00b40e50)
        int32_t SelectableType = -1;    // FpSelectable Type (5 = garage door)
        std::string SelectableString;
        float Matrix[4][4] = {};        // volume_mat (D3D rows): house-local -> unit volume
        Vector3f Center;                // orient_pos_scale pos
        Vector3f toUnit( const Vector3f &p ) const;
        Vector3f toUnitDirection( const Vector3f &d ) const;
        bool contains( const Vector3f &p ) const;
        // Ray hit with the unit box (OfType 2): entry distance (and exit).
        bool raycast( const Vector3f &origin, const Vector3f &direction, float maxDistance, float &outT,
                      float *pOutExit = nullptr ) const;
    };

    // Listener events (FUN_00da8040).
    struct EndEvent {
        std::string Name;
    };

    IntroCinematic();
    ~IntroCinematic();

    // Scene banks: the AVATAR_CHOICE party house (new_intro.bnk at the
    // EPH_L3DI_5067I spot, with the eph_l3di exterior and the island) or
    // the FIRST_WM garage (garage.bnk, a closed room: no island streaming).
    struct Setup {
        std::string Bank = "scenarics/cinematics/intro/new_intro.bnk";
        std::string HouseSpot = "EPH_L3DI_5067I";
        // Pick the house spot by position instead (nearest record within 60 m of x, z); NaN = off.
        float HousePickX = NAN, HousePickZ = NAN;
        // Added to the spot heading (radians): the scene frame turned about the spot (pi = the opposite side).
        float HouseHeadingOffset = 0.0f;
        std::string ExteriorBank = "islands/ibiza/exterior/eph_l3di.bnk";
        std::string ExteriorPrefix = "eph_l3di";
        bool bWorld = true;
        // A closed-room interior (InteriorWalk): the entry cameras stand
        // inside the room with no car to key on, so the near plane is the
        // walk one (0.05 m) all along the shot, not the 0.8 m cinematic
        // default that cut the room open within 0.8 m of the lens.
        bool bInterior = false;
        // A closed-room interior that streams the island around its spot (the
        // view out of the shop windows): the room is placed at its spot
        // (HouseSpot) and radius 1 (3x3 sectors) loads around it. The shops
        // walk this law (-nointeriorworld puts the room back into its own
        // frame at the island origin); the houses load with bWorld instead
        // (the per-frame streaming + the ocean + the exterior bank, the
        // party-house setup, houses_spec.md 5.1).
        bool bStreamWorld = false;
        // The island of HouseSpot (ibiza / hawai / casino): the one searched
        // when non-empty (shop spot names repeat across the islands), and the
        // streamed world's island.
        std::string HouseIsland;
        // The cinematic's own frame, when it is not the spot's (C_MEETING_TESS
        // plays in front of the E_HOTELA exterior): island position + heading
        // (radians). The spot (HouseSpot) still places the exterior bank.
        bool bSceneAt = false;
        Vector3f SceneAtPosition;
        float SceneAtHeading = 0.0f;
        // The scene bank has no collision: the spot exterior collision
        // (<spot>_sc_*.shk, same frame) is the floor of the scene car
        // (CINE, meeting_tess at e_hotela) [INF].
        bool bExteriorCollision = false;
        // Interior bank of the spot (CINE kind 3: "%sInterior/ich_ib.bnk",
        // FUN_00b9ff50 in FUN_009b6df0) drawn in the spot frame with its
        // interior meshes [CODE + INF frame].
        std::string InteriorBank;
        // Frontend movie of the walk mode / labels: "intro" (INTRO.vmf) or
        // the shop movie of the SHOP mode ("shopscar", SHOPSCAR.vmf: the used
        // car dealer cursor, mini menu, car data and price panels).
        std::string UiMovie = "intro";
        uint32_t GameModeId = 0x20; // AVATAR_CHOICE (0x23 FIRST_WM)
    };

    bool load();
    bool load( const Setup &setup );
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    // FUN_00da7fd0: plays a cinematic as the main one (bStopPrevious: the
    // current main one is stopped, no event 7).
    // startTime >= 0: start (seek) time (-cinetime tests).
    bool playMain( const std::string &name, bool bStopPrevious = true, float startTime = -1.0f );
    // Background cinematic (no camera: FUN_00da8040 / FUN_00da8d90, the
    // FIRST_WM ambient and voice cinematics); several can run.
    bool playBackground( const std::string &name, bool bLoop = true );
    void stopMain();
    void stopBackground();
    void stopBackground( const std::string &name );
    bool isBackgroundPlaying( const std::string &name ) const;
    const std::string &getMainName() const;
    // Every Entity__Cinematic of the bank (M_INTRO_I, M_LIFE_HOSTESS ...).
    std::vector<std::string> getCinematicNames() const;
    // Moves every scene of a cinematic to a new origin (cinematic-local
    // frame: the room frame of an interior). The casino's slot machine
    // cutscenes (*_totem.cin) are authored around one machine and play at
    // the machine the player sits at. false when there is no such cinematic.
    bool setCinematicOrigin( const std::string &name, const Matrix4x4f &origin );
    bool hasCinematic( const std::string &name ) const {
        for ( const std::string &cinematic : getCinematicNames() ) {
            if ( _stricmp( cinematic.c_str(), name.c_str() ) == 0 ) {
                return true;
            }
        }
        return false;
    }
    // Entity__FirstPerson StartCamera (the Entity__Camera name), empty if
    // the bank has none.
    inline const std::string &getFirstPersonCamera() const { return firstPersonCamera; }
    float getMainTime() const { return main.Time; }
    // C_INTRO skip (CinEnd 3: end pose, event 7).
    void skipMain();
    // Restarts the main cinematic at a time (the -cinetime seek: the state
    // messages before it are applied). The scrub of the CINVIEWER.
    void seekMain( float time );
    // The main cinematic's .cin (shots, messages, fx events) while one plays,
    // nullptr otherwise (the CINEHUD timeline).
    const Cinematic *getMainCinematic() const;
    inline const CineSubtitles &getSubtitles() const { return subtitles; }
    inline bool isSubtitlesEnabled() const { return bSubtitles; }

    // Natural ends since the last call (main: event 7; non looping
    // background: event 3).
    std::vector<std::string> takeEndEvents();

    // intro.vmf (credits, "Select your character", walk mode labels).
    inline FlashPlayer &getUi() { return ui; }
    // FSCommands of the INTRO movie (sbdd is handled here).
    void setCommandCallback( FlashPlayer::FSCommandCallback callback ) { commandCallback = std::move( callback ); }
    // FSCommands of GENERAL.vmf other than sbdd (dialog box "dbxc", "wmof"...).
    // GENERAL /dialog_box drawn (between the "dbxo" and "dbxc" FSCommands).
    void setDialogBoxVisible( bool bVisible ) { bDialogBoxVisible = bVisible; }
    void setGeneralCommandCallback( FlashPlayer::FSCommandCallback callback ) {
        generalCommandCallback = std::move( callback );
    }
    CinematicSounds &getSounds() { return sounds; }

    // Player avatar (the .sce actor "Avatar" of C_FIESTA_INTRO_xx): DBPNJ
    // configuration (AVT_<Male|Female>[_Afrik|_Asia]_Intro).
    bool setPlayerAvatar( const std::string &configId );
    // Entity camera pose (Entity__Camera, e.g. cam_entree_01), house frame
    // -> island.
    bool getEntityCamera( const std::string &name, EdenSceneView::Camera &outCamera ) const;
    // The Entity__Camera / CameraCasino name of an entity hashcode ("" if none).
    std::string getEntityCameraName( uint64_t hashcode ) const;
    bool getEntityCamera( uint64_t hashcode, EdenSceneView::Camera &outCamera ) const;
    // Walk mode camera in house-local coordinates (Heading convention).
    void setLocalCamera( const Vector3f &position, const Quaternionf &heading, float fov, float nearPlane = 0.0f );
    bool getEntityCameraLocal( const std::string &name, Vector3f &outPosition, Quaternionf &outHeading ) const;
    // The car presentation camera of an Entity__PosCam (scene-local frame):
    // the shot that frames the car standing on it, false when the PosCam has
    // no Camera entity in the bank.
    bool getPosCamViewLocal( const std::string &posCamName, Vector3f &outPosition, Quaternionf &outHeading ) const;
    const Volume *findVolume( const std::string &name ) const;
    inline const std::vector<Volume> &getSelectables() const { return selectables; }
    // House collision (intro_lph_l3di_*.shk, house-local frame).
    inline const HavokCollision &getCollision() const { return collision; }
    // Car of the scene (FIRST_WM: the current car, 387 California): rests at
    // an Entity__PosCam (D_CAR), driven by the .sce actor of an
    // Entity__CinCar (CIN_CAR) while a cinematic animates it.
    // carActor: .sce actor name animating the car when it is not an
    // Entity__CinCar (meeting_tess: "PLAYER_CAR", the player car, the
    // object of the scene without entity).
    bool loadCar( const CinematicCar::Desc &desc, const std::string &restEntity, const std::string &cinCarEntity,
                  const std::string &carActor = std::string() );
    // Showroom cars (SHOP mode: the shop scene slots, FUN_00a4fea0 /
    // FUN_00a4fd50): a car resting at an Entity__PosCam of the scene bank
    // (Entity__ShopData PosCams[slot]), on the scene collision floor.
    int32_t addShowCar( const CinematicCar::Desc &desc, const std::string &restEntity );
    inline size_t getShowCarCount() const { return showCars.size(); }
    CinematicCar &getShowCar( size_t index ) { return *showCars[index].Car; }
    bool getShowCarWorld( size_t index, Matrix4x4f &outWorld ) const;
    void setShowCarVisible( size_t index, bool bVisible );
    // Extra walk-mode selectable (scene-local volume), e.g. the box of a
    // showroom car (SelItemType "vehicle"). Returns its selectable index.
    int32_t addSelectable( const Volume &volume );
    // The house furniture slots (MyTDUHouse, houses_spec.md 11).
    struct FurnitureSlotInfo {
        std::string Slot;
        std::string Bank;
        int32_t Type = -1;
        int32_t Group = 0;
        int32_t Color = 0;     // the colour variant (0 = _01)
        int32_t NumColors = 0; // the _MHA_nn colours of the model
        Matrix4x4f ModelToScene; // the model's space -> the scene (house) frame
        Vector3f BoundsMin, BoundsMax;
    };
    std::vector<FurnitureSlotInfo> getFurnitureSlots() const;
    // A slot's model swapped for another myhouse bank (live).
    bool replaceFurniture( const std::string &slotName, const std::string &bankName );
    // A slot's colour variant (0..NumColors-1) applied live.
    bool setFurnitureColor( const std::string &slotName, int32_t color );
    // The room's walls / floors (houses_spec.md 13): the house set materials
    // whose name starts with pPrefix ("mat_floor" / "mat_wall") take the
    // material of interior/myhouse/<bank>.bnk; an empty bank restores the
    // authored ones. Returns the number of materials changed (-1: bank failed).
    int32_t applyRoomMaterial( const char *pPrefix, const std::string &bankName );
    // Slot -> bank overrides applied by the next load (the profile's choices).
    void setFurnitureChoices( std::unordered_map<std::string, std::string> choices ) {
        furnitureChoices = std::move( choices );
    }
    // The scene bank (Setup::Bank) and its hashcodes, e.g. for entities the
    // game mode reads itself (Entity__ShopData of a shop interior).
    inline const Bank &getSceneBank() const { return introBank; }
    inline const BankHashcodes &getSceneHashcodes() const { return introHashcodes; }
    // CinProps of a cinematic of the scene bank (0 if absent).
    uint32_t getCinProps( const std::string &name ) const;
    // Scene frame (cinematic scene space -> island).
    inline const Matrix4x4f &getSceneOrigin() const { return sceneOrigin; }
    // Entity__ShopData PosCams (the car places of a dealership), in order;
    // an entry is empty when its PosCam is not in the bank.
    inline const std::vector<std::string> &getShopPosCams() const { return shopPosCams; }
    // A car parked at rest on an Entity__PosCam (the dealership display cars),
    // painted with its DBCarsAndColors row paintIndex (-1: the .2dm paint).
    bool addDisplayCar( const CinematicCar::Desc &desc, const std::string &posCam, int32_t paintIndex = -1 );
    // Entity__Actor__ActorVehiclePosition: a car shown on a turntable (the
    // casino hall's socle) [DATA]. The placement is also in entityPlacements,
    // so addDisplayCar( desc, Name ) parks a car on it.
    struct VehiclePosition {
        std::string Name;
        int32_t VehicleId = -1;
        int32_t PaintId = -1;
        int32_t RimIndex = -1;
        int32_t InteriorIndex = -1;
        float TurnsPerMinute = 0.0f;
        Vector3f Position;
    };
    inline const std::vector<VehiclePosition> &getVehiclePositions() const { return vehiclePositions; }
    // Turns the last added display car about its rest point (radians per
    // second): the socle's NbTurnByMinute.
    void spinLastDisplayCar( float radiansPerSecond );
    // The model of an Entity__Actor prop by its entity __Hc (the slot
    // machines' spinners), nullptr if none.
    EdenModel *findPropModel( uint64_t entityHashcode );
    // Game-driven skeletal clip of a prop (the slot machines' handles: the
    // Entity__Actor__SlotMachineTrigger Anim): the clip rsc of the scene bank
    // sampled at `time`; time < 0 puts the prop back on its static pose.
    bool setPropClip( uint64_t entityHashcode, uint64_t clipRsc, float time );
    float getPropClipDuration( uint64_t clipRsc );
    // The texture of a material slot of a prop (a movie drawn into a texture:
    // the machines' screens). `hash` replaces the slot hash so the draw-set
    // cache (keyed on the slot hashes) rebinds. All materials named `pMaterial`
    // (nullptr: every material).
    bool setPropTexture( uint64_t entityHashcode, const char *pMaterial, uint32_t slot, GPUTexture *pTexture,
                         uint64_t hash );
    // A TD_NEONDECAL prop lit as a screen: AnimValue on glow channel 0 and
    // WeatherEmissive = strength (the neon manager never registers props).
    bool setPropEmissive( uint64_t entityHashcode, float strength );
    // The casino neons' states (Entity__Actor__NeonSlotMachine: NeonInactive,
    // NeonActive, NeonWin, NeonLoose, NeonJackpot, NeonWinRelaunch,
    // NeonLooseRelaunch) [DATA: the entity fields]; a state whose
    // CanBeActivate is 0 leaves the neon as it is.
    enum class eNeonState : uint8_t { Inactive = 0, Active, Win, Loose, Jackpot, WinRelaunch, LooseRelaunch, Count };
    bool setNeonState( uint64_t entityHashcode, eNeonState state );
    // A neon blinks bright / dim for `seconds` (a winning combination's pay
    // panel) [INF: the retail win light is the exe's].
    bool flashNeon( uint64_t entityHashcode, float seconds );
    inline size_t getNumDisplayCars() const { return displayCars.size(); }
    // A display car's position in the walk frame (scene-local) and its
    // _id_car (the near-car spec card): false past the end.
    bool getDisplayCarInfo( size_t index, Vector3f &outPosition, int32_t &outCarId ) const;
    // The display car's model (nullptr past the end).
    inline CinematicCar *getDisplayCar( size_t index ) { return index < displayCars.size() ? displayCars[index].Car.get() : nullptr; }
    // The dealer's presentation of one display car (by _id_car): repaints it
    // with its DBCarsAndColors row paintIndex (-1: keep) and puts the camera
    // on its PosCam's Camera. False when no display car has that id.
    bool focusDisplayCar( int32_t carId, int32_t paintIndex );
    // PNJ count and scene-local position (the walk mode frame; the PNJ
    // matrices are island world space), for the walk mode body collisions.
    size_t getNumPnjs() const { return pnjs.size(); }
    bool getPnjScenePosition( size_t index, Vector3f &outPosition ) const;
    // GENERAL pop_goal (FUN_00d61790 / FUN_00d61700).
    void showGoal( const std::string &text );
    void closeGoal();
    // GENERAL pop_tutorial ("Tip" box, FUN_00d62ed0 / FUN_00d61600): /:TutText
    // then goto "open_tuto" (/:IsWalkmode false: the non-WM animation);
    // close_popup().
    void showTutorial( const std::string &text );
    void closeTutorial();
    // The scene car (FIRST_WM: the California): world / scene-local pose.
    inline CinematicCar &getCar() { return car; }
    inline const CinematicCar &getCar() const { return car; }
    bool getCarWorld( Matrix4x4f &outWorld ) const;
    // The rev-counter needle sweep a playing cinematic keys on the garage
    // scene's ND_RPM actor (garage.bnk C_OUT: a 5-key Heading track, the
    // retail rev sweep of the cockpit close-up). Returns the signed dial
    // angle in degrees relative to the track's first key (the needle at
    // rest), false when no playing cinematic keys the needle (the caller
    // keeps its own pose - the live rev needle of the seated state).
    bool getNeedleSweep( float &outAngleDeg ) const;
    // CINE scenes played relative to the player car (FUN_009b5210, no spot
    // slot: the cinematic transform cine+0x120 is built from the driver
    // matrix through encrypted helpers): the cinematic frame is the spot
    // frame lifted so that the rest entity (D_CAR) stands on the spot
    // exterior floor; the exterior sets and collision stay at the spot
    // [INF]. Returns the lift (m), 0 when no floor is found.
    float liftOntoFloor( const std::string &restEntity );
    // Alternative [INF]: the cinematic frame moved so that the rest entity
    // lands at a world pose (position, heading in the spots.bin convention:
    // world = T + R_y(heading) local); the exterior stays at the spot.
    void placeEntityAt( const std::string &restEntity, const Vector3f &worldPosition, float worldHeading );
    // spots.bin record of a spot (spot object ctor FUN_00b2eb80): +0x10
    // position, +0x1c / +0x20 / +0x24 Euler angles (rt +0x44..), +0x28 second
    // position (rt +0x50), +0x38 second heading (rt +0x60) [CODE + DATA].
    struct SpotRecord {
        Vector3f Position;
        float Heading = 0.0f;
        Vector3f Position2;
        float Heading2 = 0.0f;
        uint8_t Type = 0;    // rt+0x79 <- rec+0x100 (shop type: 1 / 2 car dealer, 4 after market, 8 car wash /
                             // rental, 0x11 used car dealer "scr_open"; FUN_009f3050 / FUN_009f23e0)
    };
    static bool ReadSpot( const char *pName, SpotRecord &out );
    // Island terrain height (heightfield of the loaded sectors / areas).
    bool getGroundHeight( float x, float z, float &outHeight ) const;
    // Hides the streamed world instances (bushes ...) within a radius of a world point.
    // World surface height (terrain + static objects) at x, z below maxY.
    // Also the meshes of the exterior bank the cinematic loads itself (the hotel's drive
    // stands above the island terrain): the highest surface below maxY.
    bool getSurfaceHeight( float x, float z, float maxY, float &outHeight );
    void setInstanceHide( const Vector3f &center, float radius ) { view.getWorld().setInstanceHide( center, radius ); }
    // The streamed world (the interior walk's yard collision height callback).
    EdenWorld &getWorld() { return view.getWorld(); }
    // The exterior bank's kept collision triangles (model space with the node
    // transforms, i.e. the spot frame = island space for a house living room;
    // EdenModel::setKeepCollision is set for the exterior loads): the walk
    // builds its yard obstacles from them (the caravan shell, the garage
    // building, the plot walls), houses_spec.md 5.1.
    void collectExteriorCollisionTriangles( std::vector<Vector3f> &outVertices,
                                            std::vector<uint32_t> &outIndices ) const;
    bool getCarLocal( Matrix4x4f &outLocal ) const;
    // Player avatar seated at the car driver seat (DVRSEAT_LR) with the
    // avatarsdriver.bnk drive pose (anims\car\stance_turn.xan) and the
    // driveiklayer6 IK (hands / feet / back on the car nodes); replaces the
    // .sce "Avatar" actor while set (C_OUTRO message avm4:CIN_CAR) [INF].
    bool setPlayerSeated( bool bSeated );
    // Player emotes (docs/re/emotes_spec.md): the retail walk-avatar emote
    // clips of avatarsdriver.bnk (anims\emotes\em_xan, em_*.anm, one per sex)
    // played on the player avatar, standing at a walk-mode place (the
    // self-view camera type 10 law, docs/re/facefx_spec.md 6.3.3 states 6/7).
    struct PlayerEmote {
        const char *pLabel; // picker / -emote name ("hello")
        const char *pMan;   // anims\emotes\em_xan\avatar clip (.anm in the bank)
        const char *pWoman; // anims\emotes\em_xan\avatrice clip
    };
    // The retail emote slots, tess_garage_spec.md 4.2 animAvatar[8..16] order
    // (names [DATA]; slot 8 repeats followme in the dump, the 8 unique kept).
    static const std::vector<PlayerEmote> &getPlayerEmotes();
    static int32_t findPlayerEmote( const char *pLabel );
    // Plays the emote on the player avatar at the place set by
    // setPlayerEmotePlace (bind pose -> co_stand idle -> the clip, 0.2 s ease
    // each way [INF: the retail blend weights are ENC]); false without the
    // avatar / the bank / the clip.
    bool playPlayerEmote( int32_t index );
    // Self-view idle: the avatar stands at the place without a clip (the
    // retail state 6 look-at-your-avatar view before an emote is chosen).
    bool startPlayerSelfView();
    void stopPlayerEmote();
    inline bool isPlayerEmoteActive() const { return bPlayerEmote; }
    // True while the clip is under way (the caller's "emote ended -> leave
    // the self view" test, the retail state 7 -> 4 return).
    inline bool isPlayerEmotePlaying() const {
        return bPlayerEmote && playerEmoteClip != nullptr && playerEmoteTime < playerEmoteClip->getDuration();
    }
    inline int32_t getPlayerEmoteIndex() const { return playerEmoteIndex; }
    float getPlayerEmoteDuration() const;
    // The walk-mode floor place of the avatar (island world position of the
    // feet, heading in radians); fed every frame while the self view is on.
    void setPlayerEmotePlace( const Vector3f &floorPosition, float headingY );
    // SHOP "Enter" (FUN_00a519b0): the player avatar on the driver seat of a
    // showroom car (index of addShowCar) instead of the scene car; -1 = the
    // scene car (default).
    void setSeatedShowCar( int32_t showCarIndex );
    inline int32_t getSeatedShowCar() const { return seatedShowCar; }
    // Overrides the camera (walk mode); nullptr: cinematic camera.
    void setCameraOverride( const EdenSceneView::Camera *pCamera );
    // Moves the cinematic frame (not the spot / exterior) by delta, island
    // space: the ground snap of a scene placed with Setup::bSceneAt.
    void offsetSceneOrigin( const Vector3f &delta );
    // -logground (debug): wheel hubs and the lowest bone of each visible actor
    // against the world surface below them.
    void logGroundContacts( float time );
    // Puts the cinematic's car rest (D_CAR) on the surface below it: the
    // scene ground is the car's local y [INF]. False without car / ground.
    bool snapSceneToGround();
    inline const EdenSceneView::Camera &getCamera() const { return camera; }
    // Look-at request of a PNJ (docs/re/facefx_spec.md 6.3): FUN_00b858f0
    // (avatar +0x1d70 = target, +0x1d81 = 1, +0x1d68 = 0), the call of the
    // lkon message and of the walk-mode state machine (0x9520b9, the player
    // avatar in the self-view camera, 6.3.3); stopLookAt = lkof (+0x1d68 = 1).
    // The target stays until the next request / lkof; FUN_00b89730 applies it
    // while it is inside the 0.2 cone. False if no PNJ has that name.
    bool setLookAtTarget( const std::string &pnjName, const Vector3f &worldTarget );
    bool stopLookAt( const std::string &pnjName );
    // -say "<text>": procedural lip sync (game/avatar/speech_synth.*). The
    // synthesized FaceFX animation replaces the .sce "Morph" track's baked
    // animation for every avatar, looped on the scene time, so dialog the game
    // never recorded still moves the 42 facial bones through
    // FaceFxLibrary::Evaluate. Empty when the flag is off.
    inline bool isSaying() const { return bSay; }
    const std::string &getSaidText() const { return sayText; }
    // Entity__Fx emitter (S_TimeStart...).
    bool playEmitter( const char *pName );
    // Stops the 4 S_WALKMODE_Music emitters (C_INTRO start, event 0).
    void stopMusic();
    // GENERAL.vmf (pop_goal, black_stripes; /:PlayerName of the CINE mode
    // listener event 0).
    inline FlashPlayer &getGeneral() { return general; }

    // .sce actor messages (object "Message" tracks, e.g. the car door
    // messages opdl:0.64 of meeting_tess PLAYER_CAR) of the main cinematic
    // crossed since the last call, in time order.
    struct ActorMessage {
        std::string Actor;
        std::string Message;
    };
    std::vector<ActorMessage> takeActorMessages();

    void tick( float deltaTime );
    void renderOffscreen();
    void draw();
    // The frontend movies only (tests without the 3D view, -no3d).
    void drawUi();
    // Debug overlay (-cinepaths): the camera Position keys and the actor
    // paths of the main cinematic's scenes projected through the active
    // camera into the 2D canvas (scene placement, the CINVIEWER).
    void drawDebugPaths( Renderer2D &renderer );

private:
    struct Definition {
        std::string Name;
        Cinematic Cin;
        std::vector<CinematicScene> Scenes;
        uint32_t CinProps = 0;
        std::string ShadowConfig;
        // additionalsStyleAnims: base anim rsc -> style anim rsc ("m", "w").
        std::unordered_map<uint64_t, uint64_t> StyleAnims[2];
    };
    struct Playing {
        const Definition *pDef = nullptr;
        float Time = 0.0f;
        float PreviousTime = -1.0f;
        bool bLoop = false;
        bool bNoCamera = false;
        bool bEnded = false; // natural end reached (end pose kept)
        std::vector<GSAudio::VoiceHandle> Voices;
    };
    struct Prop {
        std::string Name;
        uint64_t EntityHashcode = 0;
        Matrix4x4f Local;                // orient_pos_scale (house frame)
        Matrix4x4f RootInverse;          // inverse of the 3DD root node local
        std::unique_ptr<EdenModel> Model;
        // Animated props (meeting_tess: the Ferrari key ring, the phone): the
        // model's own .XSB skeleton, posed by the .sce Animation track (the
        // cf_* clips) and shown by its Visibility track.
        std::unique_ptr<EdenSkeleton> Skeleton;
        bool bAnimated = false; // posed this frame (Skin / World valid)
        bool bVisible = true;
        // Taken in the player's right hand (the casino key ring after its
        // drop, see updateProps): drawn at the hand from then on.
        bool bHandHeld = false;
        Matrix4x4f HandOffset;   // ring relative to the hand bone at the grab
        Matrix4x4f HeldWorld;    // this frame's world placement in the hand
        const void *pHeldCinematic = nullptr;
        Matrix4x4f World;
        bool bExterior = false; // an actor of the exterior bank (spot frame)
        std::vector<Matrix4x4f> WorldBones, Skin;
        EdenAvatarLighting Lighting;
        const EdenAnimation *pDrivenClip = nullptr; // setPropClip
        // A house furniture slot (Entity__MyHouseObject): its name, Type and the
        // myhouse bank its model came from.
        std::string FurnitureSlot;
        std::string FurnitureBank;
        int32_t FurnitureType = -1;
        int32_t FurnitureColor = 0; // the slot's DefaultColor, then the player's choice
        int32_t FurnitureGroup = 0; // Entity__MyHouseObject Group (a set that changes together)
        float DrivenTime = 0.0f;
        int32_t NeonIndex = -1; // into neonAnims (a casino neon)
    };
    // A casino neon's light (FUN_00b44500 / FUN_00b44350 / FUN_00b43940
    // [CODE]): per state three channels (R, G, B -> AnimValue.xyz) of
    // TimeFactor, IlluminationFactor and an optional ExportType_Curve curve.
    struct NeonCurve {
        std::vector<Vector2f> Keys, TangentIn, TangentOut;
        Vector2f MinScale = Vector2f( 0.0f, 0.0f ), MaxScale = Vector2f( 1.0f, 1.0f );
        float evaluate( float x ) const;
    };
    struct NeonChannel {
        float TimeFactor = 1.0f;
        float Illumination = 1.0f;
        const NeonCurve *pCurve = nullptr;
    };
    struct NeonStateParams {
        bool bCanActivate = false;
        bool bDefined = false;
        float Duration = -1.0f;
        NeonChannel Channels[3];
    };
    struct NeonAnim {
        NeonStateParams States[static_cast<int32_t>( eNeonState::Count )];
        int32_t State = 0;
        float StateTime = 0.0f;
        float TimeOffset = 0.0f; // UseDecalRandTime: a random start in [0, 3)
        float FlashTime = 0.0f;  // flashNeon
        size_t PropIndex = 0;
    };
    std::vector<NeonAnim> neonAnims;
    std::unordered_map<uint64_t, NeonCurve> neonCurves; // ExportType_Curve by __Hc
    float neonClock = 0.0f;
    void loadNeonCurves( const XmbDocument &entities );
    void readNeonState( const XmbValue &params, NeonStateParams &out );
    void updateNeons( float deltaTime );
    // Avatars: Entity__PNJ (DBPNJ), animated by the .sce actor Animation
    // tracks (.anm of the cinematic bank); the player avatar ("Avatar").
    struct Pnj {
        std::string Name;
        uint64_t EntityHashcode = 0;
        std::unique_ptr<EdenAvatar> Avatar;
        bool bVisible = false;
        Matrix4x4f World;
        std::vector<Matrix4x4f> WorldBones;
        std::vector<Matrix4x4f> Skin;
        EdenAvatarLighting Lighting;
        bool bFaceFxUsed = false; // facialAnimCoef written once (FUN_00b77b20)
        // Look-at (docs/re/facefx_spec.md 6): lkon / lkof message state
        // (avatar +0x1d70 target, +0x1d81 on, +0x1d68 lkof) and the FaceFX
        // registers LookAt_T2_side / LookAt_T2_vertical of this frame.
        const void *pLookMessageKey = nullptr; // last lkon / lkof key seen
        bool bLookOn = false;
        Vector3f LookTarget;
        bool bLookAt = false;
        float LookAt[2] = {};
    };
    // Entity__Light of the scene (FUN_00a4c7a0 light entry), world space.
    struct Light {
        Vector3f Position;
        Vector3f Direction;
        Vector3f Ambient, Diffuse, Specular; // x Intensity
        float CosOuter = 1.0f;               // cos(AngularCutOff)
        float ConeScale = 1.0f;              // 1 / (cos inner - cos outer)
        float ConeBias = 0.0f;
        float DistanceA = 1.0f;              // cut / (cut - drop) [INF]
        float DistanceB = 0.0f;              // 1 / (cut - drop) [INF]
        bool bInterior = true;
    };

    Bank introBank;
    BankHashcodes introHashcodes;
    std::unordered_map<uint64_t, std::unique_ptr<EdenAnimation>> propClips; // setPropClip
    const EdenAnimation *getPropClip( uint64_t clipRsc );
    std::vector<std::unique_ptr<Definition>> definitions;
    Playing main;
    std::vector<std::unique_ptr<Playing>> backgrounds;
    Setup setup;
    std::vector<std::string> endEvents;
    CinematicSounds sounds;
    std::vector<GSAudio::VoiceHandle> musicVoices; // S_WALKMODE_Music emitters
    EdenSceneView view;
    FlashPlayer ui;
    FlashPlayer::FSCommandCallback commandCallback;
    FlashPlayer::FSCommandCallback generalCommandCallback;
    bool bDialogBoxVisible = false;
    // The TV-show overlays were cued once (drawn from then on; the clips
    // park themselves off screen at the end of their close label).
    bool bLogoTv = false;
    bool bLogoSc = false;
    FlashPlayer general;            // GENERAL.vmf (black_stripes letterbox)
    Matrix4x4f sceneOrigin;         // cinematic scene space -> island
    float collisionLift = 0.0f;     // cinematic-local y + lift = collision (spot) local y
    bool bSceneMoved = false;       // sceneOrigin moved off the spot frame (liftOntoFloor / placeEntityAt)
    size_t interiorSetStart = 0;    // sets[interiorSetStart..) are the Setup::InteriorBank models (spot frame)
    Quaternionf sceneRotation;      // rotation of sceneOrigin (Eden convention)
    Bank exteriorBank;              // islands/ibiza/exterior/eph_l3di.bnk
    Bank interiorBank;              // Setup::InteriorBank
    std::vector<std::unique_ptr<Bank>> furnitureBanks; // interior/myhouse/*.bnk (the house slots' models)
    std::vector<std::unique_ptr<EdenModel>> sets; // house (D_SCENE) and exterior
    std::vector<bool> setIsExterior;             // same index: drawn at spotOrigin
    Matrix4x4f spotOrigin;                       // the spot (exterior bank) frame
    std::vector<EdenModel *> reflectSets; // Entity__Actor__Mesh D_REFLECT
    std::vector<Prop> props;
    EdenAvatarLibrary avatarLibrary;
    FaceFxLibrary faceFx; // avatar_face_fx.bnk actors + the .fxe of the bank
    // -say (custom dialog): the synthesized speech animation and its length.
    bool bSay = false;
    std::string sayText;
    FaceFxAnim sayAnim;
    // -saywav (audio driven): the waveform's mouth shapes per frame.
    bool bSayWav = false;
    SpeechAudio sayWav;
    std::vector<Pnj> pnjs;
    Pnj player;
    std::unordered_map<std::string, std::pair<Vector3f, Quaternionf>> entityCameras; // Entity__Camera orient_pos_scale
    std::unordered_map<std::string, float> entityCameraFovs;       // CameraCasino Fov
    std::unordered_map<uint64_t, std::string> entityCameraHashcodes; // __Hc -> name
    struct HashCamera {
        Vector3f Position;
        Quaternionf Rotation;
        float Fov = 0.0f;
    };
    std::unordered_map<uint64_t, HashCamera> entityCamerasByHash;
    std::string firstPersonCamera; // Entity__FirstPerson StartCamera
    std::unordered_map<uint64_t, std::unique_ptr<EdenAnimation>> animations;
    HavokCollision collision;
    std::unordered_map<std::string, std::pair<Vector3f, Quaternionf>> entityPlacements; // PosCam, CinCar
    CinematicCar car;
    struct ShowCar {
        std::unique_ptr<CinematicCar> Car;
        std::string RestEntity;
        bool bVisible = true;
        bool bEnvironment = false;
    };
    std::vector<ShowCar> showCars;
    int32_t seatedShowCar = -1; // setSeatedShowCar
    struct DisplayCar {
        std::unique_ptr<CinematicCar> Car;
        Matrix4x4f World;
        bool bEnvironment = false;
        int32_t CarId = -1;
        std::string PosCam;
        // The turntable: World = sceneOrigin * T(pivot) * Ry(angle) * T(-pivot) * RestLocal.
        Matrix4x4f RestLocal;
        Vector3f Pivot;
        float SpinRate = 0.0f;
        float SpinAngle = 0.0f;
    };
    std::vector<DisplayCar> displayCars;
    std::vector<VehiclePosition> vehiclePositions;
    std::vector<std::string> shopPosCams;
    std::unordered_map<std::string, std::string> posCamCameras; // PosCam name -> its Camera entity
    std::string carRestEntity;
    uint64_t carEntityHashcode = 0;
    std::unordered_map<std::string, uint64_t> cinCarHashcodes;
    std::string carActorName;
    Quaternionf needleActorRest;   // ND_RPM entity placement heading (dial 0)
    bool bNeedleActorRest = false;
    std::vector<ActorMessage> actorMessages;
    void collectActorMessages( const Playing &playing, float from, float to );
    bool bCarEnvironment = false;
    // Static car cube of the scene bank (CUBECAR, garage.bnk): FUN_009aa2c0
    // -> FUN_00a4baf0; the dynamic cube is then not used.
    bool bCarStaticCube = false;
    std::vector<Light> lights;
    Vector3f cameraForward = Vector3f( 0.0f, 0.0f, 1.0f );
    std::vector<Volume> volumes;
    std::vector<Volume> selectables;
    float goalTimer = -1.0f;
    Bank driverBank;               // scenarics/avatars/avatarsdriver.bnk
    EdenAnimation driverPose;
    bool bDriverPose = false;
    // IKOffsetDB.bin record of the car for the player's sex
    // (docs/re/driver_seat_spec.md): seat group adjustment (y, z) and the
    // offsets of the 27 cockpit nodes (DvrSeat, Hand_R, Hand_L, Foot_R,
    // Foot_L, Radio, Back_L, ...) in their node frame.
    struct DriverIKOffsets {
        bool bValid = false;
        float SeatY = 0.0f, SeatZ = 0.0f;
        Vector3f Node[27];
    } driverIK;
    bool bDriverIKLoaded = false;
    bool loadDriverIKOffsets( int32_t idCar, bool bWoman );
    bool bPlayerSeated = false;
    void updateSeatedPlayer();
    // The walk-mode emote / self-view state (playPlayerEmote).
    bool bPlayerEmote = false;      // avatar posed at the emote place
    int32_t playerEmoteIndex = -1;  // -1: self-view idle (no clip)
    float playerEmoteTime = 0.0f;
    float playerEmoteFade = 0.0f;   // emote pose weight over the stand idle
    Vector3f playerEmoteFloor;
    float playerEmoteHeading = 0.0f;
    std::unique_ptr<EdenAnimation> playerEmoteClip;
    EdenAnimation standClip;        // co_stand-<sex>, the idle base pose
    bool bStandClip = false;
    void updateEmotePlayer();
    CineSubtitles subtitles;        // the authored captions of the main cinematic
    bool bSubtitles = true;         // -subtitles 0|1 (default on when a file exists)
    std::string weatherName;
    std::string modifierName;
    EdenSceneView::Camera camera;   // last camera
    Vector3f previousListener;
    float lastDeltaTime = 0.0f;
    float seekTime = -1.0f;
    bool bPendingUiLocalization = false;
    EdenSceneView::Camera cameraOverride;
    bool bCameraOverride = false;
    bool bLoaded = false;

private:
    const Definition *findDefinition( const std::string &name ) const;
    void loadDefinitions( const XmbDocument &entities );
    void start( Playing &playing, const Definition *pDef, bool bLoop, bool bNoCamera, float startTime );
    void stop( Playing &playing );
    void advance( Playing &playing, float deltaTime, bool bMain );
    void processMessages( const Playing &playing, float from, float to, bool bMain );
    void updateFx( Playing &playing, float from, float to );
    void updateView();
    void drawSets( EdenRenderer &renderer, EdenSceneView::ePass pass );
    bool findHouseSpot( Vector3f &outPosition, float &outHeading ) const;
    // The record scan of one island's spots.bin (findHouseSpot walks the
    // islands).
    bool findHouseSpotIn( const std::vector<uint8_t> &data, Vector3f &outPosition, float &outHeading ) const;
    void loadSets( const XmbDocument &entities );
    // The house furniture slots (Entity__MyHouseObject -> interior/myhouse):
    // models placed at the slot transforms (docs/re/houses_spec.md).
    void loadFurniture( const XmbDocument &entities );
    bool loadFurnitureModel( const std::string &bankName, Prop &prop );
    std::vector<std::unique_ptr<EdenModel>> retiredFurniture; // replaced slot models (freed at unload)
    // The authored room materials replaced by applyRoomMaterial: (set index,
    // material index) -> the original.
    std::map<std::pair<size_t, size_t>, EdenMaterial> roomMaterialOriginals;
    std::unordered_map<std::string, std::string> furnitureChoices; // slot -> bank (setFurnitureChoices)
    void loadBankModels( const Bank &bank, const BankHashcodes &hashcodes, const XmbDocument &entities,
                         bool bSkipInteriorLods );
    void applyShadowConfig( const std::string &name );
    // lgtv: / lgsc: messages -> GENERAL.vmf /logo_TV, /logo_SC (C_SOLAR_CROWN_I).
    void applyLogoMessage( const std::string &message, bool bSeekSkipped );
    // A shot camera that does not decode (NaN): the in-car view from the scene
    // car's driver seat. False: no car / seat, the camera keeps its last pose.
    bool buildInCarFallback();
    void loadPnjs( const XmbDocument &entities );
    void loadVolumes( const XmbDocument &entities );
    const EdenAnimation *getAnimation( uint64_t hashcode );
    // The .sce object of an entity / a name in the current scene of a
    // playing cinematic (scene time out).
    const CinematicScene::Object *findActor( const Playing &playing, uint64_t entityHashcode, const char *pName,
                                             const CinematicScene *&outScene, float &outSceneTime ) const;
    void animatePnj( Pnj &pnj, const Playing &playing, const CinematicScene &scene,
                     const CinematicScene::Object &object, float sceneTime );
    // The .sce Animation track of an actor sampled on a skeleton (key k = last
    // T_k <= t, crossfade with the previous key); false without the track.
    bool sampleAnimationTrack( const EdenSkeleton &skeleton, bool bWoman, const Playing &playing,
                               const CinematicScene &scene, const CinematicScene::Object &object, float sceneTime,
                               std::vector<BoneTransform> &locals );
    void updatePnjs();
    void updateProps();
    void updateHandHeld( Prop &prop );
    // The scene car's doors / windows from its .sce Message tracks (opdl / opdr /
    // opda / cldl / ... : door object FUN_00dc5a40, garage_car_spec.md 2.2).
    void updateCarDoors();
    float carDoorMaxAngle = -1.0f; // DBCarPhysicsData _doormaxangle (-1: not read yet)
    // .sce actor track "Morph": FaceFX animation of the facial bones.
    void applyFaceFx( Pnj &pnj, const CinematicScene &scene, const CinematicScene::Object &object, float sceneTime,
                      std::vector<BoneTransform> &locals );
    // FUN_00b871a0 / FUN_00b89730 cone test: LookAt_T2 registers of the
    // frame (pnj.bLookAt / pnj.LookAt) from the property LookAtTargetFaceFx
    // or the lkon message.
    void computeLookAt( Pnj &pnj, const CinematicScene &scene, const CinematicScene::Object &object, float sceneTime,
                        const std::vector<BoneTransform> &locals, const Matrix4x4f &world );
    void loadLights( const XmbDocument &entities );
    void computeAvatarLighting( const Vector3f &rootPosition, EdenAvatarLighting &outLighting );
    bool computeCarWorld( Matrix4x4f &outWorld ) const;
    // The car / a show car fitted on the scene floor: the scene collision,
    // or when the scene frame moved off the spot frame (liftOntoFloor /
    // placeEntityAt) the spot collision through the world, else the terrain.
    Matrix4x4f fitOnSceneFloor( const CinematicCar &fitCar, const Vector3f &position,
                                const Quaternionf &rotation ) const;
    std::vector<const Playing *> playingOrder() const;
    void drawFade();
};
