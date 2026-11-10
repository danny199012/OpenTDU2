#pragma once

#include "core/math.h"

#include <string>
#include <vector>

// Developer cheats, for the owner's tests (buying cars, touring the shops,
// checking the casino). A panel, toggled with F4 (or the pad's option button)
// and opened at startup with `-cheatspanel`, holding
//
//   - the **wallet**: the profile's money (what DealerMenu::buySelected spends)
//     and the casino chips, set / added with one click and saved to the
//     profile right away;
//   - **teleport**: the island's spots (spots.bin, the buildings: dealerships,
//     shops, houses), raw coordinates, or the point the player picked on the
//     map (F5 while the map is open, see GMFreeRide);
//   - the **weather** preset, the **story step** and the **back on track**
//     reset (the same code the pad's BACK_ON_TRACK uses).
//
// The mode owns one, calls drawUI() every frame and polls the requests with
// take*(): the panel itself never touches the world, the vehicle or the view.
//
// `-money <n>` / `-chips <n>` set the wallet at startup (any mode), which is
// what the headless tests use.
class Cheats {
public:
    // Opens the panel right away when -cheatspanel was given (applyCommandLine
    // runs before the modes are built, so every mode's panel obeys it).
    Cheats();

    struct Spot {
        std::string Name; // the building's bank name (ECD_FERR, EPH_L3DI...)
        Vector3f Position;
        float Heading = 0.0f; // radians
    };

    // A named quick destination of the teleport row: a landmark of the
    // island's own spot list, found by its bank prefix in setSpots (the
    // airport EAIR_A, the gambling hall EGAMHA, the dealers ECD_*...). A
    // prefix the island does not have is simply left out, so the row works
    // on Ibiza, Hawai and the casino island alike.
    struct Destination {
        std::string Name;      // "Airport", "Ferrari dealer"...
        int32_t SpotIndex = -1; // index into the spot list
    };

    // Filled by the mode (the island's spots, the weather preset names).
    void setSpots( std::vector<Spot> spots, const std::string &island );
    void setWeathers( std::vector<std::string> names, int32_t current );

    inline bool isOpen() const { return bOpen; }
    void setOpen( bool bValue ) { bOpen = bValue; }
    void toggle() { bOpen = !bOpen; }

    // What the panel asked for; each call clears the request.
    bool takeTeleport( Vector3f &outTarget, float &outHeading, bool &outHasHeading );
    bool takeWeather( int32_t &outIndex );
    bool takeStoryStep( int32_t &outStep );
    bool takeBackOnTrack();
    // The world side of the delock rows (the map owns the roads).
    bool takeUnlockAllRoads();
    bool takeUnlockAllSpots();
    bool takeTargetNearestLockedRoad();
    // The mode feeds the road status back for the panel's line.
    void setRoadStatus( int32_t percent, int32_t explored, int32_t total );
    // The map-spot discovery status (the panel's line; the ALL SPOTS row
    // marks every spot of the island discovered).
    void setSpotStatus( int32_t discovered, int32_t total );

    // The pad code (the retail dev-feature way in): a circle of the D-pad (up,
    // right, down, left) three times with the option button (X / square) mashed
    // - [INF]: the community's description of the retail code ("spam your D-pad
    // in circles while mashing Square or X"), the exact sequence is not decoded.
    // The mode feeds the D-pad / option states each frame; F4 is the port's own
    // hotkey. The circle is also what opens the accessories wheel in the game,
    // so this only watches the order of the presses.
    void feedPadCode( bool bUp, bool bRight, bool bDown, bool bLeft, bool bOption );

    // The panel: call from the game mode's drawUI() (every frame).
    void drawUI( const char *pModeName );

    // ---- The delock rows of the retail dev menu (the mod's CHEAT MENU) ----
    //
    // The retail dev features are the save's own flags (RoadLock, the owned
    // cars / houses, ClassicCarStock, Rewards - docs/re/savegame_spec.md), so
    // each row here writes the port's equivalent of one of them:
    //   ALL CARS            PlayerProfile::OwnedCars, from the DBCarShop table
    //   ALL CLOTHES         PlayerProfile::OwnedClothes, from the DBClothe table
    //   ALL HOUSES          PlayerProfile::NbHouses (the island's EPH_* spots)
    //   ALL ROADS           MapScreen::unlockAllRoads (ExploredRoads + areas)
    //   ALL SPOTS           MapScreen::unlockAllSpots (KnownSpots - the map icons)
    //   CARS ON STOCK       every DBCarShop row buyable (DealerMenu's check)
    //   ALL CAR DEALERS     nothing to unlock in the port: the shops are spots
    //   ACHIEVEMENTS        every DBRewardXP event awarded once
    static void unlockAllCars();
    static void unlockAllClothes();
    static void setNumHouses( int32_t count );
    static void setAllCarsBuyable( bool bEnable );
    static bool isAllCarsBuyable();
    static void awardAllAchievements();
    // `-cheat <row>[,<row>]`: the same rows from the command line, so a
    // scripted run can start with them applied.
    static void applyCommandLine();

    // The wallet: the profile, saved (the dealer menu and the casino read it).
    static void setMoney( int32_t value );
    static void addMoney( int32_t delta );
    static void setChips( int32_t value );
    static void addChips( int32_t delta );

private:
    bool bOpen = false;
    // The pad code's state (feedPadCode below).
    int32_t padCircleStep = 0; // 0 up, 1 right, 2 down, 3 left
    int32_t padCircles = 0;
    bool bPadOptionSeen = false;
    // The delock rows' state.
    bool bPendingAllHouses = false;
    bool bPendingAllRoads = false;
    bool bPendingAllSpots = false;
    bool bPendingNearestLockedRoad = false;
    int32_t roadPercent = 0, roadExplored = 0, roadTotal = 0;
    int32_t spotsDiscovered = 0, spotsTotal = 0;
    std::vector<Spot> spots;
    std::string island;
    std::vector<Destination> destinations;
    char spotFilter[64] = {};
    int32_t selectedSpot = -1;
    // The one teleport path of the panel: the spot list, the destination row
    // and the coordinate fields all set the same request.
    void teleportToSpot( int32_t index );
    std::vector<std::string> weathers;
    int32_t selectedWeather = 0;
    int32_t moneyInput = 1000000;
    int32_t chipsInput = 100000;
    float teleportX = 0.0f;
    float teleportZ = 0.0f;
    float teleportHeading = 0.0f;
    bool bUseHeading = false;
    int32_t storyStepInput = 3;

    // The requests.
    bool bTeleport = false;
    Vector3f teleportTarget;
    float teleportRequestHeading = 0.0f;
    bool bTeleportRequestHeading = false;
    bool bWeather = false;
    int32_t weatherIndex = 0;
    bool bStoryStep = false;
    int32_t storyStep = 0;
    bool bBackOnTrack = false;
};
