#pragma once

#include "game/avatarchoice/intro_cinematic.h"
#include "game/walkmode/walk_mode.h"
#include "physics/vehicle_collision.h"

#include <functional>
#include <string>
#include <vector>

struct HouseRow;

// Spot interiors (dealerships, clothes shops, hairdressers ...) walked in
// first person: docs/re/spot_interiors_spec.md.
//
// Every shop spot E<name> of spots.bin has an interior bank
// interior/i<name>.bnk (ECD_FERR -> icd_ferr, ECH_IA -> ich_ia, ESHOP_CA ->
// ishop_ca) [DATA: all 3 islands]. The bank has the same layout as the
// garage / house scene banks the walk modes already load: a Scenery set, a
// .shk collision, Entity__Camera, Entity__FirstPerson { StartCamera },
// FpSelectable volumes (Type 0 exit, 1 carseller, 29 hairdresser ...) and
// Entity__Cinematic (M_INTRO_I, M_LIFE_*). The interior is drawn as a closed
// room in its own frame (no island), like the FIRST_WM garage [INF: the
// retail transition is a fade; the interior's placement in the island is
// not needed to walk it].
class InteriorWalk {
public:
    enum class eResult {
        None,
        Leave, // B, or A on the exit selectable
        Shop,  // A on the seller (car seller, shop assistant, hairdresser ...)
    };

    // interior/i<name>.bnk relative to the bank folder, empty when the spot
    // bank has no interior bank (houses: HouseBankFor).
    static std::string InteriorBankFor( const std::string &spotBank );

    // The house banks of a house spot (EPH_L2E_0345I, or its truncated
    // spots.bin name EPH_L2E_): interior/lph_l2e.bnk (living, the house
    // frame) / interior/gph_l2e.bnk (garage, its own frame); empty when the
    // bank does not exist.
    static std::string HouseBankFor( const std::string &spotBank, bool bGarage );

    // Loads the interior of a spot bank (ECD_FERR ...) and starts its intro.
    // The island (ibiza / hawai / casino) places the room at its spot and
    // streams the island around it - the view out of the shop windows.
    bool enter( const std::string &spotBank, const std::string &island = std::string() );
    // Walks into a house (docs/re/houses_spec.md): the living room first
    // (C_INTRO), or straight into the garage (your cars on its display
    // slots). A on the Garage / Living selectables swaps the rooms (tick).
    // The island places the living room at its spot with the world around it
    // (the view out of the windows, the yard walkable to the Garage volume);
    // without it the room stays a closed box.
    bool enterHouse( const HouseRow &house, bool bToGarage = false, const std::string &island = std::string() );
    void exit();
    inline bool isActive() const { return bActive; }
    inline bool isHouse() const { return bHouse; }
    // Inside the garage room of a house (the gas pump of the retail game lives in
    // one of those, docs/re/fuel_gas_station_spec.md 4.1).
    inline bool isInGarage() const { return bInGarage; }
    // The A that swapped the rooms (Garage / Living) this tick: the caller's
    // own validate blocks must not fire on the same press (the garage fuel
    // pump would open the moment the room loads).
    bool tookRoomSwap();
    inline const std::string &getSpotBank() const { return spotBank; }
    inline const std::string &getHouseSpot() const { return houseSpot; }

    // A dealership's display cars: car i (DBCarPhysicsData _id_car, the
    // DBCarShop _slot_<i+1> order) on Entity__ShopData PosCams[i]. Returns
    // the number of cars placed.
    size_t placeDisplayCars( const std::vector<int32_t> &carIds );
    // The sticker shop: the player's car on the room's first car place (the
    // retail design studio shows it in the room, not outside). Returns the
    // room's car (nullptr: the room has no place for one).
    CinematicCar *placeStickerCar( int32_t carId );

    // The dealer menu's preview: the display car carId in paint row
    // paintIndex (-1: keep), seen from its PosCam camera. endPreview puts the
    // walk camera back.
    bool previewCar( int32_t carId, int32_t paintIndex );
    void endPreview();

    // The nearest display car within the radius of the walk position
    // (-1 when none): the near-car spec card in a showroom.
    int32_t findNearDisplayCar( float radius ) const;
    // The display car's _id_car (-1 past the end).
    int32_t getDisplayCarCarId( int32_t index ) const;

    // The walk-mode emotes (docs/re/emotes_spec.md): the owner mode's drawUI
    // draws the picker through these while the walk state is live.
    IntroCinematic &getScene() { return scene; }
    WalkMode &getWalk() { return walk; }
    inline bool isWalking() const { return state == eState::Walk; }

    // The sticker shop rooms (ESTICK_*) run the retail SHOPSCAR movie's own
    // "stickers" page (docs/re/stickers_ui_spec.md): the walk selects the
    // seller, the movie owns the menus, and its FSCommands (the stsh: family,
    // the dialog box, the walk on / off) come to the game through this sink
    // (GMFreeRide's StickerShopUi). -nostickerui keeps the native PaintMenu.
    inline bool isStickUi() const { return bActive && bStickUi; }
    using CommandSink = std::function<void( const std::string &, const std::string & )>;
    void setCommandSink( CommandSink sink ) { commandSink = std::move( sink ); }
    // The walk input is off while the movie's own pages are up (wmof / wmon).
    inline bool isWalkInputOn() const { return bWalkInput; }
    // The movie asked to leave the shop (its gofr after "Exit shop?" / Save).
    bool takeLeaveRequest();

    eResult tick( float deltaTime );
    // GENERAL "Tip" box over the interior.
    void showMessage( const std::string &text );
    // A shop menu is up over the interior: the scene keeps living, the
    // walk is frozen.
    void tickBackground( float deltaTime );
    void renderOffscreen();
    void draw();

private:
    enum class eState { Intro, Walk };

    // scene.load + the avatar + the intro / life cinematics + the walk; the
    // room of a house (its bank, the house spot when the room is in the
    // house frame, garage flag), a shop room (its spot bank name + island:
    // placed at the spot, the sectors streamed around it).
    bool loadRoom( const std::string &path, const std::string &houseSpotName, bool bGarageRoom,
                   const char *pWalkCamera = nullptr, const std::string &island = std::string() );
    void placeGarageCars();

    IntroCinematic scene;
    WalkMode walk;
    eState state = eState::Intro;
    std::string spotBank;
    std::string introName;
    bool bActive = false;
    bool bRoomSwapped = false; // the A that loaded the Garage / Living room
    // The streamed living room's yard collision (houses_spec.md 5.1): the
    // exterior bank's own triangles (the caravan shell, the garage building,
    // island space) and the sector statics around the spot (fences, trunks,
    // the camping ground). Both push the walk body; the ground comes from the
    // scene's world surface query.
    HavokCollision exteriorCollision;
    VehicleWorldCollision worldCollision;
    // The house rooms (enterHouse): the two banks and which one is loaded.
    bool bHouse = false;
    bool bInGarage = false;
    std::string houseSpot;
    std::string houseIsland; // the streamed world of the living room (the yard)
    std::string livingBank, garageBank;
    std::string walkCamera; // the door camera of a room swap (startWalk)

    void startWalk( const char *pPreferredCamera = nullptr );
    void applyTestAim(); // -walkaim
    // The emote key / slots / auto return (the FIRST_WM law, shared).
    void updateEmotes( float deltaTime );
    bool bEmoteWasPlaying = false;
    bool bAutoEmotePlayed = false;

    // The house rooms' retail UI (HOME.vmf): the page open over the walk
    // ("" = walking), its FSCommands, the pad routed to it.
    bool bHomeUi = false;
    // The sticker shop's retail UI (SHOPSCAR.vmf, the stickers page).
    bool bStickUi = false;
    bool bWalkInput = true;
    bool bLeaveRequested = false;
    CommandSink commandSink;
    std::string homePage;
    void onUiCommand( const std::string &command, const std::string &args );
    // The walk-in closet: the owned DBClothe ids of the open category.
    int32_t closetPartType = 0;
    std::vector<int32_t> closetItems;
    int32_t closetIndex = 0;
    bool myHouseMode = false;
    bool bDialogOpen = false; // the GENERAL dialog box (dbxo .. dbxc)
    // MyTDUHouse furniture (houses_spec.md 11): selectable index -> slot name,
    // and the slot being changed.
    std::map<int32_t, std::string> furnitureSelectables;
    struct FurnitureEdit {
        bool bActive = false;
        std::vector<std::string> Slots; // the slot, or its whole Group
        std::vector<std::pair<std::string, int32_t>> Originals; // bank + colour per slot (B reverts)
        std::string OriginalBank;
        int32_t OriginalColor = 0;
        std::vector<std::string> Banks;
        int32_t Index = 0;
        int32_t Color = 0;
        int32_t NumColors = 0;
        int32_t SurfaceType = 0; // 0 furniture, 1 floor, 2 walls (DBMaterial _type)
    } furnitureEdit;
    int32_t pendingSurfaceEdit = 0; // -surfaceedit
    static const char *SurfacePrefix( int32_t type );
    static const char *SurfaceKey( int32_t type ); // the FurnitureChoices slot ("#floor" / "#wall")
    void startSurfaceEdit( int32_t type );
    void tickSurfaceEdit();
    void addFurnitureSelectables();
    void startFurnitureEdit( int32_t selectable );
    void showFurnitureChoice();
    void tickFurnitureEdit();
    void updateClosetItem();
    void openHomePage( const std::string &type );
    void routeHomeInput();
};
