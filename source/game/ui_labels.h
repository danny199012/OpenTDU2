#pragma once

#include <string>
#include <vector>

// Human labels for the frontend's flash UI elements, shared by the owner and
// the agents. The companion of game/asset_labels.h, one level up: the asset
// viewer names *assets* (a bank + a filename), this names *UI elements* (a
// movie + a display list path).
//
//   docs/re/ui_elements.txt
//
// The UI is a set of Flash movies (Euro/Bnk/FrontEnd/HiRes/*.bnk): the pause
// menu and the options pages are the root timeline of main.vmf, the driving
// HUD is hud.vmf, the gauges are gauges.vmf, the toasts are general.vmf. Every
// element on screen is a node of the movie's display list, and the runtime
// (`FlashPlayer`) addresses it by its path from the root:
//
//   /pause_menu/menu_sq/sq3        the 4th entry square of the pause menu
//   /pause_menu/clock/time          the clock's hour digits
//
// That path is the element's *identity* - it is what `gotoLabel()` and
// `findClip()` take - so the label file keys on it and a label can never drift
// onto another element. One element per line, TAB separated, sorted by movie
// then clip path so two people can merge it:
//
//   # comment
//   <movie, lowercase, e.g. main.vmf> TAB <clip path> TAB <label> TAB <note>
//
// Reached from the UI viewer (`-gm UIVIEWER`, source/game/uiviewer). That tool
// is the only writer; `-lintui` checks every key against the movie.
struct UiElementLabel {
    std::string Movie; // "main.vmf"
    std::string Clip;  // "/pause_menu/menu_sq/sq3"
    std::string Label; // "Options entry square"
    std::string Note;  // free text
};

// The file in use: `docs/re/ui_elements.txt` when the working directory has a
// `docs/re` (the launchers cd to the repo root), else
// `<userdata>/ui_elements.txt`.
const std::string &GetUiElementLabelPath();
const std::vector<UiElementLabel> &GetUiElementLabels();

// Reads the file once (idempotent); logs the path and the count.
bool LoadUiElementLabels();
// Writes the whole file back, sorted by movie then clip. True on success.
bool SaveUiElementLabels();

// The label of an element, or nullptr. Movie and clip are normalized, so
// callers may pass either separator or casing.
const UiElementLabel *FindUiElementLabel( const char *pMovie, const char *pClip );
// Sets a label (an empty label removes the entry). True when something changed.
bool SetUiElementLabel( const char *pMovie,
                        const char *pClip,
                        const std::string &label,
                        const std::string &note );

// Normalizes a clip path: lowercase, '/' separators, a single leading '/' and
// no trailing one ("/" for the root itself).
std::string NormalizeUiClip( const char *pClip );
// Normalizes a movie name to the label file's key form ("main.vmf").
std::string NormalizeUiMovie( const char *pMovie );
