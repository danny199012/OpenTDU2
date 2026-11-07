#pragma once

#include "game/avatarchoice/intro_cinematic.h"
#include "game/casino/casino_ranking_screen.h"
#include "game/walkmode/walk_mode.h"

#include <string>
#include <vector>

// The casino interior of the original (the casino island's resort, ECAS),
// walked in first person: docs/re/casino_spec.md 9.
//
// Retail: every room of the casino is its own bank under Euro/Bnk/Interior
// [DATA: the bank listing and the entities.xmb of each]:
//
//   icashal.bnk   the hall (50 MB): the cashier, the bar, the car on its
//                 socle, the rankings screens, and one FpSelectable per
//                 door: GoToShop "shop", GoToRoulette1 "crou",
//                 GoToPoker1 "cpok", GoToPokerArcade1 "sitgo",
//                 GoToSlotMachine1 "cslo", GoToVIP "cvip",
//                 GoToDressing "avat", GoToExit "outdoor"
//   icasslo1.bnk  the slot machines: 38 Entity__Actor__CasinoGame__SlotMachine
//                 of 6 types (SlotMachineCar, SlotMachineClothes,
//                 SlotMachine_Poker, SlotMachineHawai, SlotMachineHighLife,
//                 SlotMachineClassic), each with a SelectableZone volume and
//                 a SelectableString ("sm_car", "sm_clo", "sm_pok",
//                 "sm_haw", "sm_hl", "sm_cla")
//   icasrou1.bnk  the roulette room: 5 Entity__Actor__CasinoGameRoulette
//                 tables with their CasinoRouletteTypeDesc limits (5 / 20 /
//                 25 / 50 / 100) and the CASROU_* phase cameras
//   icaspok1.bnk  the poker room: 5 CasinoGamePoker tables and the
//                 CasinoPokerChairDescription seats
//   icasvip.bnk   the VIP room (VipAccess unlockable): a high stakes poker
//                 table and the VIP car (ActorVehiclePosition)
//   icasdr.bnk    the dressing room (the wardrobe cameras)
//   ishop_ca.bnk  the casino shop (DBCarShop._clothesshopbrand 0x57 "CASINO")
//
// The interior is a closed room in its own frame (no island streaming), like
// the other spot interiors (spots/interior_walk.cpp).
//
// Interactions come back as an eAction plus the selectable string the walk
// mode reported, so the game mode keeps the mapping to its own screens.
class CasinoInterior {
public:
    enum class eRoom : uint8_t {
        None = 0,
        Hall,
        Slots,
        Roulette,
        Poker,
        VIP,
        Dressing,
        Shop,
    };

    // What the player did this frame (at most one per frame).
    enum class eAction : uint8_t {
        None = 0,
        Leave,         // the hall's "outdoor" (or B in the hall)
        BackToHall,    // "chal" of a game room, or B in a game room
        Slots,         // "cslo"
        Roulette,      // "crou"
        Poker,         // "cpok"
        SitAndGo,      // "sitgo" (the poker arcade room)
        VIP,           // "cvip"
        Shop,          // "shop"
        Dressing,      // "avat"
        Cashier,       // the cashier selectable of the hall
        Bar,           // the bar selectable of the hall
        GameProgress,  // the rankings / score screens of the hall
        SlotMachine,   // a machine of the slots room ("sm_car" ...)
        RouletteTable, // a table of the roulette room
        PokerTable,    // a table of the poker room
    };

    struct Interaction {
        eAction Action = eAction::None;
        std::string Selectable; // the /:SelItemType string
        // The slot machine / table the player stood at: the retail entity
        // name ("SlotMachineCar"), its IDGame and its index in the room list.
        std::string EntityName;
        int32_t GameId = -1;
        int32_t Index = -1;
        int32_t Price = 0; // the machine / table CasinoTypeDesc Price
        // A slot machine's own data [DATA: CasinoGame__SlotMachine]: its game
        // camera (CameraGame -> Entity__Camera__CameraCasino), the number of
        // reels (Spinners), the reel symbol atlas (the spinner's MatName and
        // NbColumn/NbLineOfSymbolsByTexture) and the reel timing.
        std::string CameraGame;
        uint64_t CameraGameHash = 0;
        int32_t NumReels = 0;
        std::string ReelMaterial;
        int32_t AtlasColumns = 0;
        int32_t AtlasRows = 0;
        float TimeBeforeFirstStop = 0.0f;
        float TimeAfterPrevSpinner = 0.0f;
        int32_t SpinnerType = 0;      // the spinner's SpinnerType [DATA]
        // Each reel's own TimeAfterPrevSpinner, left to right [DATA].
        std::vector<float> ReelStopAfter;
        // The machine's Neons[] (Entity__Actor__NeonSlotMachine: the buttons and
        // the machine's lights, with their Inactive / Active / Win / Loose /
        // Jackpot states) [DATA].
        std::vector<uint64_t> Neons;
        std::vector<uint64_t> Combos; // ComboActors: the pay panel of each combination [DATA]
        float RotationSpeed = 0.0f;   // the spinner's RotationSpeed [DATA]
        // The machine's handle (Trigger -> Entity__Actor__SlotMachineTrigger,
        // its Anim clip rsc), its credit screen (SlotScreen ->
        // Entity__Actor__CasinoFlashScreen 'Cas_SlotScreen') and its lines
        // overlay (SlotLinesScreen, 0 on the one-line machines) [DATA].
        uint64_t TriggerHash = 0;
        uint64_t TriggerClip = 0;
        uint64_t SlotScreenHash = 0;
        uint64_t LinesScreenHash = 0;
        std::vector<uint64_t> Spinners;
    };

    // The hall's cashier and barman data [DATA: Entity__Actor__CasinoCashier
    // 'Sc_Cashier', Entity__Actor__CasinoBarman 'Sc_Bar' of icashal.bnk].
    struct DeskData {
        int32_t CashierGift = 0;           // AmountGivedToPlayer (200)
        int32_t TicketChance[4] = {};      // WinTicketLevel{One..Four}ChancePercentage (1/2/3/5)
        std::string DrinkCinematic;        // CinematicPlayerDrink (C_COCKTAIL_AVATAR)
        std::string DrinkWonCinematic;     // CinematicPlayerDrinkWonTicket (C_COCKTAIL_AVATAR_WIN)
        std::string PrepareCinematic;      // CinematicPrepareDrink (C_COCKTAIL_BARMAN)
    };
    inline const DeskData &getDeskData() const { return desk; }

    // The retail bank of a room (relative to the bank folder), empty for
    // eRoom::None.
    static std::string RoomBank( eRoom room );
    // "hall", "slots", "roulette", "poker", "vip", "dressing", "shop".
    static const char *RoomName( eRoom room );

    // Loads a room and starts its walk (the room's own Entity__FirstPerson
    // camera). The hall plays M_INTRO the first time; every room plays its
    // M_LIFE background cinematics.
    bool enter( eRoom room );
    // Swaps the loaded room (a door of the hall).
    bool changeRoom( eRoom room );
    void exit();
    inline bool isActive() const { return bActive; }
    inline eRoom getRoom() const { return room; }
    inline const Vector3f &getWalkPosition() const { return walk.getPosition(); }

    // The selectable the walk mode currently points at (empty if none).
    std::string getSelectedType() const {
        return bActive ? std::string( walk.getSelectedType( scene ) ) : std::string();
    }

    eAction tick( float deltaTime );
    // Test hook (-casinoselect <n>): the n-th game place of the loaded room
    // (a machine / a table) as if the player stood at it and pressed A.
    eAction selectPlace( int32_t index );
    // The details of the action tick() returned (the machine / table).
    inline const Interaction &getInteraction() const { return pending; }
    void tickBackground( float deltaTime );
    void drawOffscreen();
    void draw();
    // Looks through a machine's own game camera (CameraGame) while it is
    // played; false when the room has no such camera. releaseCamera() goes
    // back to the walk view.
    bool focusCamera( uint64_t cameraHash );
    void releaseCamera();
    inline bool isCameraFocused() const { return bCameraFocused; }
    inline IntroCinematic &getScene() { return scene; }
    // The reels of the machine `place` (an index of the room's places): the
    // TD_Casino_scroll constants of reel `reel`'s own material - scroll_uv0..2
    // (the UV1 offset of the three symbol columns of the strip) and
    // blur_blend_factor (the motion-blur strip while it turns) [DATA: the
    // td_casino_scroll CTAB; the decoded vertex shader is in casino_spec.md].
    bool setReelScroll( int32_t place, int32_t reel, float u, float v, float blur );
    // The three symbol slots of a reel (top, payline, bottom) each on its own
    // atlas cell: slot k's UV1 is the column u in (k s, (k + 1) s], s = one
    // atlas cell (UV_symbol_space.x), scrolled by scroll_uvk [DATA: the
    // decoded td_casino_scroll vertex shader].
    bool setReelSlots( int32_t place, int32_t reel, const float ( &offsets )[3][2], float cellU, float blur );
    int32_t getPlaceReels( int32_t place ) const;
    // The handle of the played machine at clip time t (t < 0: at rest); its
    // pull clip's length (0 when the clip is missing).
    bool setHandleTime( float time );
    // The played machine's neons to a state (IntroCinematic::eNeonState).
    void setMachineNeons( IntroCinematic::eNeonState state );
    // The played machine's pay panel of a combination flashes (a win).
    void flashCombo( int32_t combination, float seconds );
    int32_t getComboCount() const;
    float getHandleDuration();
    // Every machine's credit screen / lines overlay gets a texture (the
    // played machine's its own): the movies drawn into render targets.
    void setScreenTextures( GPUTexture *pIdleScreen, GPUTexture *pPlayedScreen, GPUTexture *pLines, uint64_t stamp );
    // GENERAL "Tip" box over the interior (messages).
    void showMessage( const std::string &text );
    // A room cutscene as the main cinematic (the slot jackpots, the car win,
    // the rankings tube): its own cameras until it ends, then the walk. false
    // when the room has no such cinematic.
    bool playCinematic( const std::string &name );
    // The same, anchored at a slot machine of the room (its *_totem.cin is
    // authored around the machine; placeIndex = Interaction::Index).
    bool playCinematicAtPlace( const std::string &name, int32_t placeIndex );
    inline bool isCinematicPlaying() const { return !playingCinematic.empty(); }
    inline void skipCinematic() { scene.skipMain(); }
    // true once after a playCinematic cutscene ended.
    inline bool takeCinematicEnded() {
        const bool bEnded = bCinematicEnded;
        bCinematicEnded = false;
        return bEnded;
    }
    inline bool hasCinematic( const std::string &name ) const {
        for ( const std::string &cinematic : scene.getCinematicNames() ) {
            if ( cinematic == name ) {
                return true;
            }
        }
        return false;
    }

private:
    struct GamePlace {
        std::string EntityName;
        // The machine entity's frame (orient_pos_scale, room space): where its
        // "totem" cutscenes are anchored.
        Matrix4x4f Anchor = Matrix4x4f::Identity();
        bool bHasAnchor = false;
        int32_t GameId = -1;
        int32_t Price = 0;
        eAction Action = eAction::None;
        std::string Selectable; // "sm_car" / "roulette" / "poker" ...
        std::string CameraGame;
        uint64_t CameraGameHash = 0;
        int32_t NumReels = 0;
        std::string ReelMaterial;
        int32_t AtlasColumns = 0;
        int32_t AtlasRows = 0;
        float TimeBeforeFirstStop = 0.0f;
        float TimeAfterPrevSpinner = 0.0f;
        int32_t SpinnerType = 0;      // the spinner's SpinnerType [DATA]
        // Each reel's own TimeAfterPrevSpinner, left to right [DATA].
        std::vector<float> ReelStopAfter;
        // The machine's Neons[] (Entity__Actor__NeonSlotMachine: the buttons and
        // the machine's lights, with their Inactive / Active / Win / Loose /
        // Jackpot states) [DATA].
        std::vector<uint64_t> Neons;
        std::vector<uint64_t> Combos; // ComboActors: the pay panel of each combination [DATA]
        float RotationSpeed = 0.0f;   // the spinner's RotationSpeed [DATA]
        // The machine's handle (Trigger -> Entity__Actor__SlotMachineTrigger,
        // its Anim clip rsc), its credit screen (SlotScreen ->
        // Entity__Actor__CasinoFlashScreen 'Cas_SlotScreen') and its lines
        // overlay (SlotLinesScreen, 0 on the one-line machines) [DATA].
        uint64_t TriggerHash = 0;
        uint64_t TriggerClip = 0;
        uint64_t SlotScreenHash = 0;
        uint64_t LinesScreenHash = 0;
        std::vector<uint64_t> Spinners; // the reels' Entity__Actor__SlotMachineSpinner __Hc, left to right
    };
    void fillInteraction( const GamePlace &place, int32_t index );
    bool bCameraFocused = false;

    bool loadRoom( eRoom newRoom );
    void startWalk();
    // The slot machines / the roulette and poker tables of the loaded room.
    void buildGamePlaces();
    // The cars of the room's Entity__Actor__ActorVehiclePosition (the hall's
    // socle, the VIP lounge).
    void placeShowCars();

    IntroCinematic scene;
    WalkMode walk;
    // The loaded room's entities.xmb (the slot machines, the roulette and
    // poker tables of the bank).
    XmbDocument entitiesDoc;
    eRoom room = eRoom::None;
    bool bActive = false;
    std::string playingCinematic;
    bool bCinematicEnded = false;
    // The walk camera drives the view only while walking: walk.update() pins
    // the camera to the walker, so it ran over every cutscene (the walker sat
    // at the room origin before the walk started - the C_INTRO "dunes").
    bool bWalking = false;
    // The hall's rankings tube behind the podium (casino_spec 9.11).
    CasinoRankingScreen rankings;
    bool bViewingRankings = false; // looking at the screen (RankingCamera)
    bool bWalkAfterCinematic = false;
    bool bRefocusAfterCinematic = false;
    uint64_t focusedCameraHash = 0;
    void handleEndEvents();
    std::string bankPath;
    std::vector<GamePlace> places;
    Interaction pending;
    DeskData desk;
};

