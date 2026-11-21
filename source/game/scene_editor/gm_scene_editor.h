#pragma once

#include "game/gm_base.h"
#include "game/scene_editor/scene_doc.h"
#include "game/scene_editor/scene_objects.h"
#include "game/spots/mod_props.h"
#include "game/spots/mod_roads.h"
#include "game/spots/spot_exteriors.h"
#include "game/walkmode/walk_controller.h"
#include "physics/havok_collision.h"
#include "render/eden/eden_scene_view.h"
#include "render/eden/eden_weather.h"

#include <memory>
#include <string>
#include <vector>

class SceneCommandServer;

// "SCENE_EDITOR": the OpenTDU2 scene editor - a Blender-like but easy tool
// over the streamed island (WORLD_VIEWER skeleton: streaming, weather, free
// camera). It composes new buildings / interiors / props from what the game
// already ships (library nodes, actors, exterior banks, interior banks, OBJ
// props), detaches baked pieces (hide / material overrides), edits roads
// from splines (M3), walks the composed interiors first person (M4) and
// saves everything as a JSON scene in the mods overlay
// (`<mods>/scenes/<name>.scene.json`, docs/re/scene_editor.md).
//
// Two non-interactive entries share the command interpreter
// (scene_cmd.h):
//   -editorscript <file.json>   one-shot batch: commands run after the island
//                               loads (-screenshot / -exitframe apply)
//   -editorport <n> (-noserver) the localhost JSON command server (M2)
//
// Controls: the WORLD_VIEWER camera (RMB look / WSAD / wheel speed) plus:
//   LMB: pick (an object selects it; drag after pick-on-selection moves it on
//        its ground plane), Delete: remove the selection, Ctrl+Z / Ctrl+Y:
//        undo / redo, Ctrl+D: duplicate, F2: panel, F6: walk (M4).
class GMSceneEditor : public GameMode {
public:
    GMSceneEditor();
    ~GMSceneEditor();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;
    void drawUI() override;

    static constexpr const char *kIslands[] = { "ibiza", "hawai", "casino", "eden", "hongkong" };

    // --- state the panels and the command interpreter edit ---
    EdenSceneView view;
    WeatherLibrary gameWeathers;
    const WeatherPreset *pWeather = nullptr;
    std::vector<SpotRecord> spots; // the island's spots.bin (start spots)
    SpotExteriors exteriors;
    ModProps modProps;
    SceneDocument doc;
    SceneObjects sceneObjects;
    ModRoads modRoads;              // the doc's roads, tessellated (M3)
    std::string activeRoadId;       // the road the editor adds points to
    bool bExteriors = true;
    bool bGroundClamp = false; // the editor walks the ground: clamping fights placement
    std::string island;
    std::string pendingIsland;
    std::string statusText;
    bool bWorldReady = false;

    // Free-fly camera (the WORLD_VIEWER law)
    Vector3f position;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float baseSpeed = 50.0f;
    float currentSpeed = 0.0f;
    float lookSensitivity = 0.0025f;
    float fovDegrees = 60.0f;
    bool bLooking = false;
    bool bCaptured = false;
    bool bRelativeMouse = false;
    bool bShowPanel = true;
    int32_t sectorRadius = 2;
    int32_t lastSectorKey = INT32_MIN;
    int32_t selectedSpot = -1;
    int32_t selectedWeather = 0;

    // Editor state
    std::string selectedId;         // the selected scene object ("" = none)
    bool bSnapGround = true;        // new placements land on the heightfield
    float gridSnap = 0.0f;          // metres, 0 = off
    bool bRebuildPending = false;   // doc changed: re-apply next tick
    std::string spotSignature;      // scene spot set changed: re-set the streamer
    struct PickInfo {
        bool bSceneObject = false;  // hit a scene object (selectedId)
        std::string ObjectId;
        bool bWorld = false;        // hit the streamed world
        std::string NodeName;       // the world node under the cursor
        Vector3f Position;          // hit position (world)
        float Distance = 0.0f;
    };
    PickInfo hover;                 // last pick (the "inspect" line + override buttons)
    // Drag: moving the selection on its ground plane (LMB held on it)
    bool bDragging = false;
    Vector3f dragGrabOffset;
    bool bPrevLeftMouse = false;

    // Undo / redo (document snapshots; a mutation pushes before changing)
    std::vector<SceneDocument> undoStack;
    std::vector<SceneDocument> redoStack;

    // Asset browser cache (rebuilt when the island changes)
    struct AssetEntry {
        std::string Name;
        std::string Kind; // library | actor | spot | prop
    };
    std::vector<AssetEntry> assets;
    bool bAssetsStale = true;

    // Command server (scene_cmd.*): off with -noserver, -editorport <n>
    // (default 9377, 127.0.0.1 only)
    std::unique_ptr<SceneCommandServer> pCommandServer;
    bool bBatchRun = false;

    // Walk state (M4). The controller and the collision live in the BANK
    // frame of the walked interior (the .shk's own space); walkFrame is the
    // placement frame bank -> world (the SpotExteriors frame law), so the
    // camera renders from frame * eye and the streamed room draws exactly
    // where the walk happens. walkInteriorId "" = the scene grounds walk
    // (terrain / road floors, no walls).
    bool bWalkActive = false;
    WalkController walkController;
    HavokCollision walkCollision;
    std::string walkInteriorId;
    Matrix4x4f walkFrame;
    Vector3f walkEyeWorld;
    Vector3f walkForwardWorld = Vector3f( 0.0f, 0.0f, 1.0f );
    float walkFovDegrees = 60.0f;
    std::string walkNote;

    // --- the methods ---
    bool loadIsland( const std::string &name );
    void unloadIsland();
    void loadSpots();
    void teleport( const Vector3f &target, float heading, float height );
    void updateCamera( float deltaTime );
    void streamWorld( bool bForce, const Vector3f &center );
    void streamExteriors( const Vector3f &center );
    void setRelativeMouse( bool bEnable );
    Vector3f getForward() const;

    // The screen-space pick: a ray from the camera through the pixel (the
    // scene view's real View / Projection), then the scene objects and the
    // streamed world. Updates 'hover'.
    void pickAt( float x, float y );
    // The pixel -> world ray of the last frame's scene params.
    SceneRay screenRay( float x, float y ) const;

    // --- edit operations (the panels + the command interpreter call these) ---
    void pushUndo();
    void undo();
    void redo();
    void markSceneChanged(); // re-apply the doc (and the spot set) next tick
    std::string placeAsset( const std::string &kind, const std::string &ref, const Vector3f &atPosition,
                            bool bAtCamera );
    void deleteSelected();
    void duplicateSelected();
    void snapSelectedToGround();
    // The visibility overrides: the doc holds them, EdenWorld applies.
    void addOverride( const std::string &kind, const std::string &target );
    void applyOverrides();
    // Prop placements -> <mods>/props.txt (the route the game draws today).
    void commitPropsToGame();
    // Scene -> native export (M5): the placements append as real records to
    // the overlay's spots.bin (the add_spot.py route in C++); the referenced
    // interior banks copy to the overlay's exterior/ folder. FREERIDE
    // streams the scene with no engine change; re-running updates in place.
    void exportSpotsToGame();
    // M5 next slice: the library / actor placements append as .pmi records
    // to the landing sectors' banks (copied into the overlay).
    void exportPmiToGame();
    // Roads: the doc's splines -> <mods>/roads/<island>.json (what the game
    // modes load), one road baked as a props OBJ + placement line, and the
    // island's roads file imported into a fresh document (editable).
    void exportRoadsFile();
    void bakeRoadToProps( const std::string &roadId );
    int32_t importRoadsFile();
    void refreshAssets();

    // --- walk (M4): first person inside an interior placement ---
    // F6 / enter_walk: walks the given `interior` object (default: the
    // selection, else the nearest placement) on the bank's own .shk
    // collision, starting at the retail FirstPerson camera. Without any
    // interior placement it walks the scene grounds (terrain / road floors,
    // no walls). atBankLocal overrides the start eye (bank-local metres,
    // x = FLT_MAX: the bank's own start); yawLocalDegrees overrides the
    // start heading (bank-local degrees, FLT_MAX: the camera's / the fly
    // heading). exitWalk parks the fly camera at the walk eye.
    bool enterWalk( const std::string &interiorId = std::string(),
                    const Vector3f &atBankLocal = Vector3f( FLT_MAX, FLT_MAX, FLT_MAX ),
                    float yawLocalDegrees = FLT_MAX );
    void exitWalk();
    inline bool isWalkActive() const { return bWalkActive; }
    inline const std::string &getWalkInteriorId() const { return walkInteriorId; }
    inline const std::string &getWalkNote() const { return walkNote; }

private:
    void refreshSpotRecords();
    // The walk tick: controller input + the camera pose (tick's walk branch).
    void tickWalk( float deltaTime );
    // The grass clearings: every loaded interior bank's footprint rects
    // (scene rooms in fields would otherwise grow the tall grass through the
    // floor; the retail spots sit on cleared plots). Re-derived per stream
    // pass.
    void refreshGrassClearings();
    // The panels (scene_editor_ui.cpp): one drawUI member, one TU.
    void drawSceneUI();
};
