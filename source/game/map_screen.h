#pragma once

#include "flash/flash_player.h"
#include <array>
#include <functional>
#include <map>
#include "render/eden/eden_scene_view.h"

// World map screen of the original frontend: FrontEnd/HiRes/map.bnk
// (MAP.vmf, 3.7 MB — the biggest frontend bank after main).
//
// The retail game opens it from the free ride HUD with the MAP action, which
// Default_KB.xmb / Device*.xmb bind to the *back* button of the pad
// (SDL_GAMEPAD_BUTTON_BACK: Select on a DualSense, the Share/View button) and
// to the keyboard equivalent — see the "12. Pad: the D-pad mapping" note in
// docs/STATE.md, where MENU_PAUSE = START and MAP = BACK were decoded. Until
// now nothing in OpenTDU2 consumed IA_Map, so the button was dead.
//
// The movie owns the whole screen (island map, tabs, the player marker); the
// game only has to (a) push the /: variables it reads and (b) tell it to open
// and close. The variable names below come from the retail native<->SWF
// contract recovered from the PS3 build — docs/re/ps3/flash_globals.txt, the
// map/Map group:
//
//   /:IsMapAvailable        the screen refuses to open when false
//   /:MapMode               which map the screen shows (mode 2 = free ride)
//   /:MapTabObjective       the tab the screen starts on
//   /:MapCancel / :IsMapQuit      set by the movie when the player backs out
//   /:BackFromMap / :IsBackFromMap  the return trip
//   /:MapPlayerInfosLoaded  the player panel data is in
//   /:m_MapPlayerVehicle    localized name of the car the marker follows
//   /:minimap_x_offset / :minimap_y_offset   written by the movie (the marker
//                           position), read back by the game
//
// A game mode owns one instance, like PauseMenu: the mode freezes the world
// while isOpen() and unloads it on exit.
class MapScreen {
public:
    // Loads MAP.vmf and pushes the variables the screen reads.
    bool load();
    void unload();
    inline bool isLoaded() const { return player.isLoaded(); }
    inline bool isOpen() const { return bOpen; }

    // Plays the movie's open / close animation.
    void open();
    void close();

    // The championship ("contest") view of this movie (docs/re/championship_screen_spec.md):
    // the root's contest flow - /:IsContest, /:ContestName, /:ContestType,
    // /:ContestDirectAccess and the labels co_direct_enter / contest / contest_back.
    struct ContestInfo {
        std::string Name;        // "Championship: Sant Antoni"
        std::string Type;        // the /tool_tip/infos kind: asphalt, asphalt_h, dirt, dirt_h, classic, classic_h, cup
        std::string ClassLabel;  // the /championship/pages/cont/p/gp badge: "c4", "a7", "b4"
        std::string Rival;       // "Miami Harris"
        std::string Restriction; // the vehicle restriction text
        std::string Reward;      // "$50,000"
        std::string ChallDone;   // "0/6"
        struct Race {
            std::string Name;     // the .ig2 name
            std::string Title;    // "Race: Sant Antoni downtown"
            std::string Kind;     // the /tool_tip/infos label: race, timeattack, speedtrap, speed
            std::string Distance; // "1.6 mi"
            std::string Laps;     // "--" / "3"
            std::string Reward;   // "$2,000"
            std::string Rules;    // the rules text
            Vector3f Position;    // the start gate
            bool bEnterable = false;
        };
        std::vector<Race> Races;
    };
    // Opens the map and, once its screen is up, enters the contest view.
    void openContest( const ContestInfo &info );
    inline bool isContest() const { return bContest; }
    inline int32_t getContestSelected() const { return contestSelected; }
    inline const ContestInfo &getContest() const { return contest; }
    // Up / down through the championship's races: the race pop-up follows.
    void contestStep( int32_t direction );
    // The race the player entered (one shot, empty when none).
    bool takeContestRace( std::string &outRace );
    // Enter on the selected race: false when that race cannot be entered (locked / not run yet).
    bool enterContestRace();
    // OK on a race: the first press opens the extended pop-up (Enter / Close), the second enters.
    bool contestAccept();
    // Back: closes the extended pop-up first; false when the screen should close.
    bool contestBack();
    void tick( float deltaTime );
    // Back / cancel while the screen is up: closes the credits when they are
    // showing and returns true (the screen stays); false when the caller should
    // close the map.
    bool onBack();
    inline bool isCreditsOpen() const { return bCreditsOpen; }
    void draw( Renderer2D &renderer );

    // The marker the movie computed for the player (/:minimap_x_offset /
    // /:minimap_y_offset). False when the screen has not written them yet.
    bool getMarkerOffset( float &outX, float &outY );

    // The map is not a Flash picture: the movie only draws the chrome and a
    // cursor, and the island underneath is the 3D world seen by a map camera
    // [INF: from the PS3 build's MapRotation / MapInclination /
    // MapDistanceAddedToZoom / MapCenterCorrection parameters, the cursor
    // clip 'curseur' that stays fixed on screen and the headless run, where
    // the centre is just the world behind a transparent movie].
    // Starts over the car, pitched down; the cursor stays in the middle and
    // the camera moves under it.
    void resetCamera( const Vector3f &center );
    // Pan (left stick / arrows), zoom (triggers / wheel / W S), rotate and
    // tilt (right stick), all in the screen frame of the camera.
    void updateCamera( float deltaTime );
    void getCamera( EdenSceneView::Camera &outCamera ) const;
    inline Vector3f getCenter() const { return center; }
    // The retail map: the island-wide colour map (<island>-c.2db) drawn flat
    // under the Flash chrome, with the player arrow. extentX / extentZ are the
    // world metres the texture spans (u = x / extentX, v = z / extentZ).
    // Without it the 3D map camera above is used.
    void setIslandMap( GPUTexture *pTexture, float extentX, float extentZ );
    inline bool hasIslandMap() const { return pIslandTexture != nullptr; }
    // The island extents in metres (the dash sat-nav projects with them).
    inline float getExtentX() const { return extentX; }
    inline float getExtentZ() const { return extentZ; }
    static constexpr int32_t kNumZoomLevels = 4;
    // True while only the flat texture is shown: zoom level 0, transition done; the others are the
    // tilted 3D view of the streamed world.
    inline bool isFlat() const { return pIslandTexture != nullptr && zoomLevel < 1 && transition >= 1.0f; }
    // The flat view is the camera's level 0 (also while it fades in / out).
    inline bool isFlatLevel() const { return pIslandTexture != nullptr && zoomLevel < 1; }
    // 0 = the car's camera, 1 = the map camera; the camera flies between the two
    // when the screen opens and closes.
    inline float getTransition() const { return transition; }
    inline bool isUiShown() const { return transition >= 1.0f && !bClosing && !bCreditsOpen; }
    void setStartCamera( const EdenSceneView::Camera &carCamera ) { startCamera = carCamera; }
    // The camera of the current transition state (the map camera at 1).
    void getTransitionCamera( EdenSceneView::Camera &outCamera ) const;
    // Opacity of the flat map: it fades in over the end of the fly-out.
    float getFlatAlpha() const;

    // Route (GPS): OK sets / clears the destination at the cursor, the route
    // follows the road graph of the island (Ibiza_ROAD_MAP.IA).
    void loadRoads( const std::string &island );
    // Explored roads (Discovery skill): the road links the car has driven turn
    // blue on the map. Call every frame with the car position while driving;
    // returns the newly explored length in metres [INF: the retail rule - which
    // roads count, the width of a road - is not decoded, see docs/re/savegame_spec.md].
    float trackCar( const Vector3f &position, float deltaTime );
    // Percent of the island's road length explored (the Discovery "Roads" row).
    int32_t getExploredPercent() const;
    // The island road graph (nodes in world metres, links as node id pairs).
    inline const std::vector<Vector3f> &getRoadNodes() const { return roadNodes; }
    inline const std::vector<std::pair<int32_t, int32_t>> &getRoadEdges() const { return roadEdges; }
    // The curve of each link (same index as getRoadEdges, from edge.first to
    // edge.second, ends on the nodes) from <Island>_ROAD_DETAIL.IA [DATA].
    inline const std::vector<std::vector<Vector3f>> &getRoadShapes() const { return roadShapes; }
    // The class of each link (same index as getRoadEdges), the byte at 'DAOR'
    // +12 of the road map: 0..4 on Ibiza, up to 11 on Hawai [DATA]. The retail
    // map colours a road by its AIRL type (GamePC.cpr [HudMap], see
    // docs/re/freeride_spec.md 4: 0 highway 29/180/215 w4, 1 secondary
    // 0/255/255 w3, 2 road 0/255/255 w3, 3 offroad 243/196/6 w3, 4 circuit
    // 102/102/204 w4, a locked road 140/140/140). Which value of the road map
    // is the offroad one is not decoded (docs/re/roads_spec.md); the dirt class
    // below is the offroad slot of that table and -dirtclass <n> overrides it.
    static constexpr uint8_t kRoadClassDirt = 3;
    // The class drawn as a dirt track: kRoadClassDirt, or -dirtclass <n>.
    static uint8_t getDirtClass();
    inline const std::vector<uint8_t> &getRoadClasses() const { return edgeClass; }
    // True when that link has been driven (it is drawn in its class colour; an
    // undiscovered link stays grey like the retail, and it refuses fast travel).
    inline bool isRoadExplored( int32_t edge ) const {
        return edge >= 0 && static_cast<size_t>( edge ) < edgeExplored.size() && edgeExplored[edge] != 0;
    }
    // Cursor snapping: the shop icon and/or the road the reticle sticks to when
    // it comes close (-1: the cursor is free). The movie's reticle is placed on
    // the snapped point, and OK / fast travel use it.
    inline int32_t getSnappedSpot() const { return snapSpot; }
    inline int32_t getSnappedRoad() const { return snapRoad; }
    inline bool isSnapped() const { return snapSpot >= 0 || snapRoad >= 0; }
    // Fast travel (the map's travel button, F5 / pad option 2 in FREERIDE): the
    // point under the cursor when the road there has been discovered. False
    // when it is an undiscovered road - the retail refuses it.
    bool getFastTravelPoint( Vector3f &outPoint ) const;
    void toggleTarget();
    inline bool hasRoute() const { return routePoints.size() > 1; }
    inline const std::vector<Vector3f> &getRoutePoints() const { return routePoints; }
    // Metres of road left to the destination (0 without a route).
    inline float getRouteLength() const { return routeLength; }
    // The junctions of the route (road nodes with 3 or more links) and the
    // turn the route takes there, for the GPS voice (GpsGuidance).
    struct RouteTurn {
        int32_t Node = -1;     // road node index
        float Along = 0.0f;    // metres along routePoints from its first point
        // The retail junction angle (FUN_00935fd0): the heading of the link out
        // minus the heading of the link back to where the route comes from,
        // 0..360 - 90 = a right turn, 180 = straight on, 270 = a left turn.
        float AngleDeg = 180.0f;
        // The HUD arrow (/:ArrowDirection): 0 up, 1 rightup, 2 right,
        // 3 rightdown, 4 down, 5 leftdown, 6 left, 7 leftup, +8 at a roundabout.
        int32_t Arrow = 0;
    };
    inline const std::vector<RouteTurn> &getRouteTurns() const { return routeTurns; }
    // Metres along routePoints of the point of the route nearest to position
    // (-1 without a route); outDistance: how far position is from the route,
    // outTangent: the route direction there (flat, normalized).
    float projectOnRoute( const Vector3f &position, float &outDistance, Vector3f &outTangent ) const;
    // Sets the destination at the road node nearest to a world position (the
    // -gpsdrive test; the map's OK goes through toggleTarget).
    void setTarget( const Vector3f &position );
    // Roads discovered per cruising zone (area): the milestone events reached
    // since the last call, named like DBRewardXP rows
    // ("CRUISING_IBIZA_ROADS_AREA1_25") [DATA].
    std::vector<std::string> takeEvents();
    // A map spot of an icon kind ("cardealer", "estates"...) within a radius of a
    // world position: its building name (ECD_AUDI ...), false when none.
    // pOutPosition (optional) receives the spot's world position (the world
    // spot markers of the HUD anchor on it).
    bool findSpotNear( const Vector3f &position, const char *pIcon, float radius, std::string &outName,
                       Vector3f *pOutPosition = nullptr ) const;
    // The nearest spot of any icon kind that the filter accepts (by building
    // name and spots.bin record id — a house spot's ownership is per record);
    // outIcon is its icon kind, pOutId its spots.bin record id.
    bool findNearestSpot( const Vector3f &position, float radius,
                          const std::function<bool( const std::string &, uint64_t )> &filter, std::string &outName,
                          const char *&outIcon, Vector3f *pOutPosition = nullptr, uint64_t *pOutId = nullptr ) const;
    // Every spot with an icon kind (name, icon kind, world position, spots.bin
    // record id): the HUD's distant world markers over shops and houses.
    void forEachSpot(
        const std::function<void( const std::string &, const char *, const Vector3f &, uint64_t, int32_t )> &visit ) const;
    // True once after the car reached the destination.
    inline bool takeArrival() {
        const bool bArrived = bArrivedFlag;
        bArrivedFlag = false;
        return bArrived;
    }
    // Live route: call every frame while driving. Recomputes the route from the
    // car once a second and clears the destination on arrival (40 m) [INF].
    void tickRoute( const Vector3f &carPosition, float deltaTime );
    inline int32_t getZoomLevel() const { return zoomLevel; }
    void setPlayer( const Vector3f &position, float headingX, float headingZ );

    // The cheats (source/game/cheats.h) - the retail dev menu's road rows:
    // every link of the island counts as explored ("ALL ROADS"), and the debug
    // nav helper routes to the nearest link the player has not driven yet
    // ("TARGET NEAREST LOCKED ROAD"). Both work on the loaded road graph.
    void unlockAllRoads();
    bool targetNearestUnexplored( const Vector3f &from );
    inline int32_t getNumRoadEdges() const { return static_cast<int32_t>( roadEdges.size() ); }
    int32_t getNumExploredRoads() const;
    // The gas stations placed from the mods overlay (a `fuel` prop,
    // docs/re/modding.md 3): the map draws an icon for each (the movie has none -
    // its own pack/point/spot sprite carries 102 labels and not one is a station,
    // and no bank holds station icon art - docs/re/fuel_gas_station_spec.md).
    // The art is <mods>/icons/fuel.png when present, otherwise a built-in glyph;
    // -nofuelicons turns it off.
    void setFuelStations( const std::vector<Vector3f> &positions );

    // The map's points of interest: islands/<island>/level/spots.bin, one icon
    // per spot (clones of the movie's /pack/point). Call before the screen
    // opens; the icons are created once the movie is up.
    void loadSpots( const std::string &island );
    // Spot discovery (the retail law: a spot's icon only shows on the map
    // once the car drove within discovery range of the building; the port
    // keeps the state in PlayerProfile::KnownSpots). Call every frame with
    // the car position while driving; returns how many spots it newly
    // discovered (the caller saves the profile when that is > 0).
    int32_t discoverNear( const Vector3f &position );
    // The dev menu's "ALL SPOTS": every loaded spot counts as discovered.
    void unlockAllSpots();
    // Spots with an icon / how many of them are discovered (the cheats panel's
    // status line and the map-open log).
    inline int32_t getNumIconSpots() const { return static_cast<int32_t>( spots.size() ); }
    int32_t getNumDiscovered() const;
    // World position -> stage pixels of the movie (1280 x 960, the 16:9 band is
    // y 120..840) under the current view. False behind the camera.
    bool projectToStage( const Vector3f &world, float &outX, float &outY ) const;
    void drawFuelIcons( Renderer2D &renderer );
    // Header of the screen (the movie's infos clip): the car the player is in
    // and the money. Pushed to /:m_MapPlayerVehicle and /:money.
    void setPlayerInfo( const std::string &vehicleName, double money );
    // Sea, island and player arrow; drawn before the movie.
    void drawIsland( Renderer2D &renderer );

    // Streaming radius (in 768 m sectors) the view needs at the current zoom.
    int32_t getStreamRadius() const;

    // The world point the cursor is over, and whether it is a valid one (over
    // land, the "no" sign otherwise) - the same pair the destination setting
    // uses. The cheats' teleport ("go where I pointed") is the other caller.
    inline Vector3f getCursorWorld() const { return cursorWorld(); }
    inline bool isCursorOverLand() const { return isCursorValid(); }

private:
    void updateIslandView( float deltaTime );
    // One zoom step for the flat map: the mouse wheel notch, or the pad's
    // triggers. LT / RT are analog axes, so they never produce the digital
    // press edge wasPressed() reads: the step is taken when the axis crosses
    // the threshold and repeated while it stays there (kZoomRepeatDelay /
    // kZoomRepeatRate).
    int32_t takeZoomStep( float deltaTime );
    void openCredits();
    void closeCreditsState();
    void buildSpots();
    void applySpotLod();
    void updateSpots();
    void updateMarkers();
    // Cursor snapping: sticks the reticle to a shop icon or to a road when the
    // free cursor comes within a few pixels of it (hysteresis keeps it there).
    void updateSnap();
    void computeRoute();
    Vector3f cursorWorld() const;
    // The free (un-snapped) point under the cursor: what panning moves.
    Vector3f freeCursorWorld() const;
    // The link whose curve comes nearest to a world point, within maxDistance
    // metres of it; pOutPoint / pOutAlong receive the point on the curve and
    // the metres along it. -1 when none.
    int32_t findRoadNear( const Vector3f &point, float maxDistance, Vector3f *pOutPoint = nullptr,
                          float *pOutAlong = nullptr ) const;
    // The link nearest to a world point that has already been discovered (-1
    // when none): what fast travel and the shop rule test.
    int32_t findExploredRoadNear( const Vector3f &point, float maxDistance ) const;
    bool isCursorValid() const;
    void buildCredits();
    GPUTexture *pIslandTexture = nullptr;
    float extentX = 1.0f, extentZ = 1.0f;
    int32_t zoomLevel = 0;
    float mapScale = 0.0f; // virtual pixels per metre; 0 = fit the island on reset
    // Pad zoom state (takeZoomStep): the analog trigger hold.
    bool bZoomInHeld = false;
    bool bZoomOutHeld = false;
    float zoomHoldTime = 0.0f;
    float zoomRepeatTimer = 0.0f;
    Vector3f playerPosition;
    float playerHeadingX = 0.0f, playerHeadingZ = 1.0f;
    Vector3f center;
    float distance = 1400.0f;  // metres from the centre to the eye
    float yaw = 0.0f;          // radians about the vertical axis
    float pitch = 60.0f * kDegToRad; // 90 deg = straight down
    FlashPlayer player;
    bool bOpen = false;
    bool bClosing = false;
    bool bContest = false;
    bool bContestApplied = false;
    ContestInfo contest;
    void applyContest();
    void showContestRace();
    int32_t contestSelected = 0;
    bool bContestExtended = false;
    // The race pins (clones of /pack/point named cpin<N>, one per race) and the pin the highlight is on.
    std::vector<std::string> contestPins;
    int32_t contestPinLit = -1;
    void buildContestPins();
    std::string contestEntered;
    float closeTimer = 0.0f;
    bool bLabelsDumped = false;
    int32_t dumpTicks = 0;
    int32_t openTicks = 0;
    struct Spot {
        std::string Name;     // bank name of the building (ECD_AUDI, EPHOT_B...)
        uint64_t Id = 0;      // record id = GetIdentifier64bit of the spot name ("ECD_FERR_3337I")
        Vector3f Position;
        const char *pIcon;    // frame label of the movie's pack/point/spot clip
        int32_t Tab;          // tab row index the icon belongs to (-1: none)
        int32_t LogoIndex = -1; // DBBrands _logo_global_index for dealer tiles (-1: none)
        std::string Clip;     // name of the clone under /pack
    };
    std::vector<Spot> spots;
    // The island loadSpots read and, per spot, whether it has been discovered
    // (the profile's KnownSpots + the auto-knowns: owned houses, home spots).
    // An undiscovered spot draws no icon and the cursor does not snap to it.
    std::string spotsIsland;
    std::vector<uint8_t> spotDiscovered;
    // Fuel station icons (setFuelStations / drawFuelIcons): the positions of the
    // `fuel` props and the icon art (nullptr: the built-in glyph).
    std::vector<Vector3f> fuelStations;
    GPUTexture *pFuelIconTexture = nullptr;
    bool bFuelIconTried = false;
    float fuelIconAspect = 1.0f;
    bool bSpotsBuilt = false;
    // Free cursor (canvas px, 1280 x 720): moved by the left stick at the island
    // level, fixed in the middle of the screen at the closer levels.
    float cursorX = 640.0f, cursorY = 360.0f;
    // Cursor snapping state: the spot / link the reticle sticks to (-1: free),
    // the world point it is on and where it is drawn (canvas px).
    int32_t snapSpot = -1;
    int32_t snapRoad = -1;
    Vector3f snapPoint;
    float snapX = 640.0f, snapY = 360.0f;
    float transition = 0.0f;
    EdenSceneView::Camera startCamera;
    // Road graph and route
    std::vector<Vector3f> roadNodes;
    std::vector<uint8_t> roadNodeKinds; // EDON +10 (3 = roundabout, [CODE] [node+0xa] == 3 at 0x9362a6)
    std::vector<std::vector<std::pair<int32_t, float>>> roadLinks;
    // The same links as an edge list (index = save id) and their explored flag.
    std::vector<std::pair<int32_t, int32_t>> roadEdges;
    std::vector<uint8_t> edgeExplored;
    // The class byte of each link ('DAOR' +12); the map colours the road by it.
    std::vector<uint8_t> edgeClass;
    std::vector<std::vector<Vector3f>> roadShapes;
    std::vector<float> edgeLengths;
    std::vector<std::array<float, 4>> edgeBoxes; // min x, min z, max x, max z
    float exploredLength = 0.0f, totalRoadLength = 0.0f;
    float trackTimer = 0.0f;
    std::string roadIsland;
    bool bHasTarget = false;
    float routeTimer = 0.0f;
    // Per area (cruising zone polygon): total / explored road length and the
    // milestones (10 / 25 / 50 / 75 / 100 %) already awarded.
    bool bAreasBuilt = false;
    std::vector<int32_t> edgeArea;
    std::vector<float> areaTotal, areaExplored;
    std::vector<std::array<bool, 5>> areaAwarded;
    std::vector<std::string> pendingEvents;
    void buildAreas();
    void checkAreaMilestones( int32_t area );
    float routeLength = 0.0f;
    bool bArrivedFlag = false;
    std::map<std::pair<int32_t, int32_t>, int32_t> edgeBetween; // (node, node) -> shortest link
    Vector3f target;
    std::vector<Vector3f> routePoints; // smoothed polyline, world metres
    std::vector<float> routeAlong;     // metres along the polyline at each point
    std::vector<RouteTurn> routeTurns;
    int32_t numRouteBars = 0;
    int32_t currentTab = 0;
    // Credits (zooming out past the furthest level): the scroll is built from
    // FrontEnd/AllRes/credits.txt into clones of scroll/title0 and scroll/txt0.
    bool bCreditsOpen = false;
    bool bCreditsRequested = false;
    bool bCreditsBuilt = false;
    float creditsY = 0.0f;
    float creditsEndY = 0.0f;
};
