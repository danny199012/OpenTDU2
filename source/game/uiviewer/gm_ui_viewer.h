#pragma once

#include "game/gm_base.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

class FlashMovie;
class FlashPlayer;

// UIVIEWER: OpenTDU2's own tool (like ASSET_VIEWER / WORLD_VIEWER), not a TDU2
// mode.
//
// The TDU2 frontend is a set of Flash movies (Euro/Bnk/FrontEnd/HiRes/*.bnk:
// main.vmf carries the pause menu and the options pages, hud.vmf the driving
// HUD, gauges.vmf the gauges, general.vmf the toasts, map.vmf the map...), and
// every element on screen is a node of a movie's *display list*. This tool
// shows that list as data: the path of each node, its character, its timeline
// frame, whether it is visible, its bounds, and the timeline labels it can be
// jumped to - plus the movie itself, drawn live, with a click selecting the
// element under the mouse.
//
// It exists because the UI is where the port is thinnest: the pause menu is
// driven by hand from `PauseMenu` (its square list, its locks, its clock), and
// many frontend screens are not implemented at all. Naming every element once -
// in `docs/re/ui_elements.txt`, keyed on `<movie> + <clip path>` - is what turns
// "a lot of UI is missing" into a list that can be worked through, and what a
// 1:1 native rebuild would read its layout from.
//
// Panels: Movies (left, the 41 frontend banks of FrontEnd/HiRes), the display
// list (middle), the movie + the selected element (right). Same shape as the
// asset viewer, so the two tools feel alike.
//
// Command line: `-gm uiviewer` (or `-uiviewer`), `-movie <name>` to open one
// ("main", "hud", "main.vmf", a bank path), `-clip <path>` to select an element,
// `-goto <clip>:<label>` to jump one to a timeline label before the screenshot
// (scriptable states: `/pause_menu:pause_stop`), `-dumpui` to log the whole
// display list and exit (the agent-usable output), `-lintui` to check the label
// file against the movies and exit, `-label <text>` / `-note <text>` /
// `-savelabel` to write a label without a mouse, `-play` to let the timeline
// run (the default is paused, so a screenshot is stable), `-nopanel` to hide the
// ImGui windows (with `-shot` the movie alone is what lands in the image).
class GMUiViewer : public GameMode {
public:
    GMUiViewer();
    ~GMUiViewer();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void draw() override;
    void exit() override;
    void drawUI() override;

private:
    // One node of the display list, flattened in draw order (parents first).
    struct Element {
        std::string Path; // "/pause_menu/menu_sq/sq3"
        std::string Name; // "sq3" (the leaf, or "#<depth>" when unnamed)
        int32_t Level = 0;
        uint16_t Depth = 0;
        uint16_t CharacterId = 0;
        const char *pType = "?";
        uint32_t Frame = 0;
        uint32_t FrameCount = 0;
        bool bVisible = true;
        bool bHasClip = false;   // a sprite: it has its own timeline
        bool bLabelled = false;  // docs/re/ui_elements.txt has a label
        // Bounds in the element's *own* space (twips). bHasBounds is false when
        // the character has no measurable shape (an empty sprite, a button with
        // no hit record): such a node is listed but cannot be clicked.
        bool bHasBounds = false;
        float XMin = 0.0f, XMax = 0.0f, YMin = 0.0f, YMax = 0.0f;
        // The composed transform of this node (root space): what projects the
        // bounds onto the stage for the highlight rectangle.
        float WorldA = 1.0f, WorldB = 0.0f, WorldC = 0.0f, WorldD = 1.0f;
        float WorldTX = 0.0f, WorldTY = 0.0f;
    };

    void scanMovies();
    bool openMovie( const std::string &name );
    void unloadMovie();
    // Walks the display list into `elements` (keeps the selection by path).
    void rebuildElements();
    void selectElement( int32_t index );
    // The node under a point of the stage, given in normalized stage
    // coordinates (0..1, y down, non-letterboxed). -1 when nothing is there.
    int32_t hitTest( float u, float v );
    // Jumps the selected element (or `path`) to a timeline label of its clip.
    bool goToLabel( const char *pClipPath, const char *pLabel );
    void saveLabel();
    void dumpElements();
    int32_t lintLabels();
    void setStatus( const std::string &text );

    bool bShowPanel = true;
    bool bPaused = true;
    float playSpeed = 1.0f;

    // The movies of FrontEnd/HiRes, by bank stem ("main", "hud", ...).
    std::vector<std::string> movieNames;
    int32_t selectedMovie = -1;
    char movieFilter[64] = {};
    char clipFilter[128] = {};

    std::unique_ptr<FlashMovie> pMovie;
    std::unique_ptr<FlashPlayer> pPlayer;
    std::string movieKey; // "main.vmf" (the label file's key form)
    std::string movieInfo;
    float stageWidth = 0.0f; // stage pixels (twips = px * 20)
    float stageHeight = 0.0f;

    std::vector<Element> elements;
    int32_t selected = -1;
    std::string selectedPath; // kept across rebuilds
    // The path the label/note editor currently shows: a selection can also be
    // made from the command line (-clip), which has no click to load it.
    std::string editorPath;
    uint32_t numDrawnRows = 0;
    bool bRebuild = true;

    // The movie's rect on screen, normalized to the backbuffer (the 2D layer
    // draws before the panels, so it uses the panel layout of the last frame).
    float movieRect[4] = {};
    bool bMovieRectValid = false;

    char labelText[256] = {};
    char noteText[512] = {};
    std::string status;
    // `-goto` is applied after the settle frames: a nested clip (the pause
    // menu's own entry column) does not exist until the movie's root has run
    // its first frames. One semicolon-separated list, applied in order:
    //   -goto "/:pause_stop;/pause_menu/menu_sq:stop"
    std::vector<std::pair<std::string, std::string>> pendingGotos;
    int32_t gotoTries = 0;
    // `-dumpui` / `-lintui` run once, when the movie has settled.
    bool bDumpPending = false;
    int32_t framesSinceOpen = 0;
};
