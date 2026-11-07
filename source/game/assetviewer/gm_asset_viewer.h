#pragma once

#include "core/xmb.h"
#include "filesystem/bank.h"
#include "game/gm_base.h"
#include "physics/havok_collision.h"
#include "render/2db.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_weather.h"

#include <memory>
#include <string>
#include <vector>

struct GPUTexture;
struct RenderTarget;
struct AudioClip;
class FlashMovie;
class FlashPlayer;

// ASSET_VIEWER: OpenTDU2's own tool (like WORLD_VIEWER), not a TDU2 mode.
//
// It browses the game's banks and shows, for every asset, the three things that
// identify it - the *leaf* name the data uses (`fla_0047.2db`), the *original
// Eden authoring path* the bank carries (`BankEntry::Path`, e.g.
// `D:\Eden-Prog\...\EURO\FrontEnd\Cas_SlotMachine\fla_0047.2db`), and the size
// / type / group - plus a preview, and a label + note the owner writes. The
// labels go to `docs/re/asset_labels.txt` (see `game/asset_labels.h`), so the
// names the owner writes are what the agents' logs and specs use from then on.
//
// Panels: Banks (left, filterable), Assets (middle: filename, extension, size,
// group, and the label when there is one), Preview + label (right: the
// identity, the metadata, the preview and the editor) and the 3D view (the
// models of the bank).
//
// Previews:
//   - `2db` textures as an `ImGui::Image` of the decoded texture (one face of a
//     cube map: the flat image is what the panel can show);
//   - `3dd` / `3dg` / `2dm` models in a 3D view: every `.3dd` of the bank is
//     loaded with `EdenModel::load` and drawn into a panel sized render target
//     (`drawOffscreen`), the camera a slow turntable around the row they are
//     laid out in. Clicking a mesh **selects the asset that carries it**
//     (`EdenModel::raycast`, the click-to-label flow: the label keys on the
//     same `<bank> + <asset filename>` identity as the lists);
//   - `vmf` movies: `FlashMovie::load` + `FlashPlayer::draw` into a panel sized
//     stage (the 2D layer, `draw`);
//   - `wav` sounds: `GSAudio::loadClip` + `play` / `stop`.
//
// Command line: `-gm assetviewer` (or `-assets`), `-bank <path>` to open one
// directly, `-asset <filename>` to select one, `-pickat x,y` to pick a mesh at
// a pixel of the 3D view (a click, scriptable), `-noscene` to skip the 3D view
// and the model loads, `-island <name>` for the shared material library
// (default ibiza), `-buildindex` to (re)build the cross-bank asset index,
// `-search <text>` to run a search at startup, `-labellint` to check the label
// file against the game data and exit, `-labellabels` to log every label.
//
// The index (`<userdata>/asset_index.txt`) is what makes the tool usable
// without knowing the bank: every bank's entry filenames + authoring paths, and
// every `entities.xmb` entity class / name / position, so "ferrari", "slot" or
// "cam_entree" finds the bank that holds it.
class GMAssetViewer : public GameMode {
public:
    GMAssetViewer();
    ~GMAssetViewer();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;
    void drawUI() override;

private:
    struct BankInfo {
        std::string
            Path; // key form: lowercase, '/' (the label file's bank column)
        std::string Label; // the path as it is on disk, for display
        uint32_t Size = 0;
    };

    // One model of the open bank: the asset it *is* (`Asset` is the label key's
    // asset column, the `.3dd` entry of the bank) and where the 3D view places
    // it. A click that hits one of its meshes selects `RowIndex`.
    struct SceneModel {
        std::string Asset;
        std::string Name;
        int32_t RowIndex = -1;
        std::unique_ptr<EdenModel> Model;
        Matrix4x4f Transform; // translation only (the layout)
        Vector3f BoundsMin;   // world (Transform applied)
        Vector3f BoundsMax;
        uint32_t NumVertices = 0;
        uint32_t NumTriangles = 0;
        uint32_t NumMeshes = 0;
        uint32_t NumMaterials = 0;
    };

    // An `entities.xmb` entity of the open bank (the class arrays of
    // ExportType_Entity: cameras, actors, volumes, FpSelectable,
    // cinematics...).
    struct EntityEntry {
        std::string Class; // "Camera", "Actor__Mesh", "Volume__FpSelectable"...
        std::string Array; // the array it came from
        std::string Name;
        uint64_t Hashcode = 0;
        Vector3f Position;
        Quaternionf Rotation;
        // Every field of the object, as "name = value" lines (nested composites
        // are walked: what the other tools never show).
        std::vector<std::pair<std::string, std::string>> Fields;
    };

    // A hit of the cross-bank index (`<userdata>/asset_index.txt`).
    struct IndexHit {
        char Kind = 'a'; // 'a' asset entry, 'e' entity
        std::string Bank;
        std::string Name; // asset filename or entity name
        std::string Path; // authoring path (assets) or "Class name" (entities)
    };

    // ---- Filters ----
    // The asset list filter: the extension families of the game data.
    enum class eTypeFilter : int32_t {
        All = 0,
        Models,     // 3dd / 3dg / 2dm
        Textures,   // 2db
        Movies,     // vmf
        Sounds,     // wav
        Anims,      // anm / xan / uva
        Cinematics, // cin / sce / fxe / fxa
        Collision,  // shk / dhk
        Count
    };
    bool matchesTypeFilter( const BankEntry &entry ) const;
    eTypeFilter typeFilter = eTypeFilter::All;
    // The language copies of a bank (`<name>__fr.bnk`, `__ge`...): hidden by
    // default (they triple the bank list for interiors and frontend movies).
    bool bShowLanguageBanks = false;

    void scanBanks();
    bool openBank( const std::string &path );
    void rebuildRows();
    void selectAsset( int32_t index );
    // Destroys the preview texture (the cache does not own it: createFrom2DB
    // returns one the caller must free).
    void releasePreview();
    void saveLabel();
    void setStatus( const std::string &text );
    // Per-bank label counts for the bank list (recomputed when a label is
    // saved).
    void refreshBankLabelCounts();

    // ---- The middle panel: assets / entities / search ----
    void loadEntities();
    void buildEntityRows(); // what the panel lists (class filter + text filter)
    void collectFields(
        const XmbValue &value,
        const std::string &prefix,
        int32_t depth,
        std::vector<std::pair<std::string, std::string>> &out ) const;
    void loadAssetIndex();  // the cached index (lazily)
    bool buildAssetIndex(); // -buildindex: walk every bank once
    void runSearch();       // searchText -> searchHits
    void openHit( int32_t index );

    // ---- The 3D view (models of the open bank) ----
    void loadSceneModels();
    void unloadSceneModels();
    // The shared material library (`islands/<island>/level/commonworld.bnk`
    // library.2dm) and its textures: what the banks' materials fall back on.
    bool loadLibraryMaterials();
    void layoutSceneModels();
    void focusScene( int32_t rowIndex );
    EdenSceneParams buildSceneParams( uint32_t width, uint32_t height ) const;
    // A pixel of the 3D view panel (backbuffer pixels) -> the asset row and the
    // scene node of the nearest mesh. False when the ray hits nothing.
    bool pickScene( float pixelX,
                    float pixelY,
                    int32_t &outRow,
                    int32_t &outNode,
                    float &outDistance );
    void
    applyPick( int32_t row, int32_t node, float distance, const char *pSource );
    // The entity cameras of the open bank: `view from a camera` (an interior
    // previews as a box seen from outside otherwise).
    void buildCameraList();
    void useEntityCamera( int32_t index );
    // The free camera (`bFreeCamera`): WASD / QE while the 3D view has the
    // keyboard, the mouse drag still looks.
    void updateFreeCamera( float deltaTime );

    // ---- Movie (`vmf`) and audio (`wav`) previews ----
    void loadMoviePreview();
    void unloadMoviePreview();
    void toggleAudioPreview();
    void stopAudioPreview();

    // ---- `.cin` / `.sce` (shots, actors) and `.shk` (the collision overlay)
    // ----
    void loadCinematicPreview();
    void loadCollisionPreview();

    std::vector<BankInfo> banks;
    std::vector<int32_t> bankLabelCounts;
    char bankFilterText[128] = {};
    int32_t selectedBank = -1;

    Bank bank;
    std::string bankKey; // normalized path of the loaded bank
    std::vector<const BankEntry *> rows;
    // The assets of this bank that already have a label (so the list can mark
    // them without a lookup per row per frame).
    std::vector<std::string> labelledAssets;
    char assetFilterText[128] = {};
    int32_t selectedAsset = -1;
    // Which tab of the middle panel is open (0 assets, 1 entities, 2 search).
    int32_t middleTab = 0;
    // A tab the tool asks to open (a search hit): the tab bar selects it once.
    int32_t requestedTab = -1;

    // Entities of the open bank (`entities.xmb`).
    std::vector<EntityEntry> entities;
    std::string entitiesInfo;
    std::vector<std::string> entityClasses; // the classes present in the bank
    char entityFilterText[64] = {};
    int32_t entityClassFilter = 0; // 0 = all classes, else 1 + the class index
    std::vector<int32_t> entityRows;
    int32_t selectedEntity = -1;
    bool bMarkEntities = true; // draw the selected entity in the 3D view

    // The cross-bank index and the search.
    std::vector<std::string> indexLines;
    std::string indexPath;
    size_t indexAssetCount = 0;
    size_t indexEntityCount = 0;
    bool bIndexLoaded = false;
    bool bIndexMissing = false;
    char searchText[128] = {};
    std::string lastSearch;
    std::vector<IndexHit> searchHits;
    size_t searchHitCount = 0; // matches before the display cap
    bool bSearchTruncated = false;

    // Preview.
    Texture2DB texture;
    GPUTexture *pPreviewTexture = nullptr;
    bool bPreviewIsTexture = false;
    std::string previewInfo;
    std::string cinematicInfo;

    // Label editing (the text buffers are what ImGui writes into).
    char labelText[160] = {};
    char noteText[320] = {};
    std::string statusText;
    float statusTimer = 0.0f;
    bool bShowPanel = true;

    // 3D view.
    bool bScene = true; // models loaded and drawn (-noscene turns it off)
    std::vector<SceneModel> models;
    std::string sceneInfo;
    // The open bank's textures are registered with the cache under the bank
    // path (the models' materials look them up): released with the models.
    bool bBankTexturesLoaded = false;
    RenderTarget *pSceneTarget = nullptr;
    // The view-projection of the 3D view's last frame (what a click unprojects
    // with: the same matrix the GPU drew with).
    Matrix4x4f sceneViewProjection;
    Vector3f sceneCenter;
    float sceneDistance = 20.0f;
    float sceneYaw = 0.6f;
    float scenePitch = -0.3f;
    float sceneFov = 45.0f;
    bool bTurntable = true;
    float sceneTime = 0.0f;
    // Free camera (WASD / QE, the drag looks): where the camera stands, and
    // whether it is the orbit camera that is used.
    bool bFreeCamera = false;
    Vector3f freePosition;
    float freeSpeed = 8.0f;
    // The entity cameras of the bank ("view from a camera").
    std::vector<std::string> cameraNames;
    std::vector<Vector3f> cameraPositions;
    std::vector<Quaternionf> cameraRotations;
    std::vector<float> cameraFovs;
    int32_t selectedCamera = -1;
    // The camera the 3D view uses when it is not the orbit / free one: the pose
    // of an entity camera (a room bank previews as a box seen from outside
    // otherwise).
    bool bCameraOverride = false;
    Vector3f cameraOverridePosition;
    Quaternionf cameraOverrideRotation;
    float cameraOverrideFov = 60.0f;
    // The ImGui image rect of the last frame (backbuffer pixels) and whether
    // the mouse was over it: what a click picks with.
    float sceneImageMin[2] = {};
    float sceneImageMax[2] = {};
    bool bSceneHovered = false;
    // The image rect the last one was logged at (the -pickat pixels are read
    // from the log).
    float loggedSceneRect[4] = {};

    // The collision overlay (`.shk` of the open bank): a wireframe of the boxes
    // and triangles, projected into the 3D view.
    HavokCollision collision;
    bool bCollisionLoaded = false;
    bool bShowCollision = false;
    std::string collisionInfo;
    int32_t pickedRow = -1;
    int32_t pickedNode = -1;
    float pickedDistance = 0.0f;
    // `-pickat x,y`: a click, scriptable (the ImGui rect is only known after a
    // frame, so the pick runs on a later tick).
    bool bPickPending = false;
    float pendingPick[2] = {};
    int32_t pendingPickTries = 0;
    // `-pickscan`: a grid of rays over the 3D view, logged (where the models
    // are, in the view: a debug aid for the picking).
    bool bPickScan = false;
    // `-pickcheck`: projects every model's centre with the scene's
    // view-projection and picks the pixel it lands on (the picking's own check:
    // the ray must hit that model, at about that distance).
    bool bPickCheck = false;
    // `-savelabel`: write the label of the selected asset once the selection
    // has settled (after a `-pickat` pick).
    bool bSaveLabelPending = false;
    std::string pendingLabel;
    std::string pendingNote;

    // Shared material library + weather of the preview scene.
    Bank libraryBank;
    Render2DM libraryMaterials;
    bool bLibraryLoaded = false;
    std::string island = "ibiza";
    WeatherLibrary weathers;
    const WeatherPreset *pWeather = nullptr;

    // Movie preview (panel sized stage, drawn by `draw`).
    std::unique_ptr<FlashMovie> pMovie;
    std::unique_ptr<FlashPlayer> pPlayer;
    std::string movieInfo;
    float movieRect[4] = {}; // panel rect, normalized (last frame)
    bool bMovieRectValid = false;

    // Audio preview.
    std::shared_ptr<AudioClip> audioClip;
    uint32_t audioVoice = 0;
    bool bAudioPlaying = false;
    std::string audioInfo;
};
