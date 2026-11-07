# The UI viewer, and the UI element labels (`docs/re/ui_elements.txt`)

Written 2026-11-02 (Cline, `cline`). The owner's ask: *"we're missing a lot of UI
elements so what I was thinking maybe we can make a tool similar to how asset
viewing etc... works but mostly for inspecting each UI element like pause screens
and in game menu's ... as I was thinking if we could do this we may be able to fix
a mass majority of UI stuff and down the line also maybe be able to recreate near
1:1 replicas of the UI"*. Scope agreed with them in the same conversation:
**Ibiza first** - Hawaii is not reachable yet and a lot of Ibiza is still open -
and **main.vmf first**, because the pause menu and the options pages are the one
frontend screen the port already drives by hand, so the tool can be checked
against known ground truth.

Companion of `docs/re/asset_viewer.md`: that tool names *assets*, this one names
*UI elements*. Same shape, same label-file trick, one level up.

Tags as in `CLAUDE.md`: `[CODE]` readable executable - `[DATA]` game files -
`[INF]` inferred or chosen.

---

## 1. The problem, and the thing that makes it smaller than it looks

The frontend is a set of Flash movies, and the port reads them as they are:

```
Euro/Bnk/FrontEnd/HiRes/   41 banks, one movie each
  main.bnk    -> MAIN.vmf     the pause menu + the options pages (root timeline)
  hud.bnk     -> HUD.vmf      the driving HUD
  gauges.bnk  -> GAUGES.vmf   the gauges
  general.bnk -> GENERAL.vmf  toasts, prompts, the `$$bitmaps$$` prompt sprites
  map.bnk, casino.bnk, cas_*.bnk (9 casino screens), log_in2.bnk, shopsacar.bnk,
  club.bnk, home.bnk, results.bnk, r_school.bnk, challeng.bnk, dlc.bnk, ...
```

`FlashPlayer` (`source/flash/flash_player.{h,cpp}`, 122 KB) is a real AVM1
runtime: a display list, timelines, a subset of ActionScript. So the *elements*
already exist in memory, with their names, their matrices and their frame:

```cpp
struct Clip {                       // a timeline
    const FlashTimeline *pTimeline; // Frames + Labels: map<string, uint32_t>
    Clip *pParent;                  // -> the full path
    Instance *pInstance;            // the owning display object
    uint32_t CurrentFrame;
    bool bPlaying;
    std::map<uint16_t, std::unique_ptr<Instance>> DisplayList; // depth -> object
    std::map<std::string, Value, CaseInsensitiveLess> Variables;
};
struct Instance {
    uint16_t CharacterId, Depth, Ratio, ClipDepth;
    std::string Name;               // "sq3", "menu_sq", "clock"
    FlashMatrix Matrix;
    FlashColorTransform ColorTransform;
    bool bVisible;
    std::unique_ptr<Clip> pClip;    // sprites
};
```

`FlashPlayer` already exposes exactly what a viewer needs - it is how the game
drives the menu by hand today:

| Needs | Already there |
|---|---|
| the whole tree | `getRoot()` |
| an element by name | `findClip( path )` |
| the element's own timeline labels | `Clip::pTimeline->Labels` |
| force a state | `gotoLabel( clipPath, label, play )`, `gotoFrame` |
| show one element alone | `drawClipOnly( renderer, path )` |
| read the game's own flags | `getVariable( "/:LockOptions" )`, `setVariable` |
| character metadata | `getDefinitions()` (`FlashDefinitions`: shapes, sprites, edit texts, buttons) |

So the work is **not** a new asset pipeline. It is a panel over a tree that is
already built, plus a label file - `docs/re/ui_elements.txt` - with the same
identity-keying rule that made `asset_labels.txt` work.

---

## 2. The tool

`-gm uiviewer` (or `-uiviewer`) - a game mode, like `ASSET_VIEWER` and
`WORLD_VIEWER`, because it gets the renderer, the ImGui panels and the movie
reader for free. `source/game/uiviewer/gm_ui_viewer.{h,cpp}`.
`launch/17_UiViewer.bat`.

| Panel | What it shows |
|---|---|
| **movies** (left, top) | the banks of `FrontEnd/HiRes`, filterable; the open movie's stage size, frame count and frame rate; the label file in use and its count; the last status line |
| **display list** (left, bottom) | every node of the open movie: the leaf name **indented by depth**, a `*` when it is labelled, a hover tooltip with the full path, the type, the character id, the depth, the frame and the visibility; a filter over path + type; `play` / `step` / `dump` |
| **element + movie** (right) | the selected element: its path, type, character, depth, frame, visibility, the **timeline labels of its clip as clickable buttons** (that is `gotoLabel`), the label/note editor with Save / Clear / Copy path - and under it the **movie drawn live**, with a click selecting the element under the mouse and a yellow box on the selection |

The **path** is the identity, and it is the same string the runtime takes:

```
/                             the root timeline (pause_in / pause_stop / ...)
/pause_menu
/pause_menu/menu_sq           the entry column, 52 frames
/pause_menu/menu_sq/sq3       the 4th entry square
/pause_menu/clock/time/disp   the clock's digit display
```

### How the click works

The stage is drawn by `FlashPlayer::draw` through a stage->canvas matrix of 1/20
(`flash_player.cpp`: `stageToCanvas.A = stageToCanvas.D = 1.0f / 20.0f`), and the
stage is mapped onto the panel rect with `setStageRect` + `beginRect`. So a click
is converted to **twips in root space** (`u * stageWidth * 20`) and tested against
each node with that node's composed matrix inverted - exactly the rule
`sendMouseToClip` already uses for buttons:

```cpp
world = parentMatrix * instance.Matrix;      // drawClip's composition rule
world.inverse().transform( rootX, rootY, x, y );
// a hit when (x, y) is inside the character's own bounds
```

Bounds come from the character, per type `[DATA]`:

| type | bounds |
|---|---|
| `CT_Shape` | `FlashShape::XMin..YMax` (authored) |
| `CT_EditText` | `FlashEditText::XMin..YMax` (authored) |
| `CT_Sprite` | the union of its children, each through its own matrix (recursive) |
| `CT_Button` | the union of the **hit records** (`States & 0x08`), through each record's matrix - Eden buttons are hit areas only |

Deeper wins (the more specific element); among siblings the first tested wins and
the list is walked front to back, so the topmost element is the one you get.
Bitmaps are **not** display list characters in this runtime - they are shape
fills - so there is no bitmap case to handle `[CODE]`.

### Command line

| flag | what |
|---|---|
| `-gm uiviewer` / `-uiviewer` | the tool |
| `-movie <name>` | open one: `main`, `main.vmf`, `euro/bnk/frontend/hires/main.bnk` all resolve |
| `-clip <path>` | select an element before the first frame (a screenshot of its panel) |
| `-goto <clip>:<label>[;...]` | jump clips to timeline labels first, in order: `-goto "/:pause_stop;/pause_menu/menu_sq:stop"`. Applied **after** the settle frames and retried, because a nested clip does not exist until its parent's state has run |
| `-dumpui` | log the whole display list **and exit** (the agent-usable output) |
| `-lintui` | check `docs/re/ui_elements.txt` against the movies, then exit |
| `-label <text>` / `-note <text>` | fill the editor (with `-clip`) |
| `-play` | let the timeline run (the default is paused) |
| `-nopanel` | hide the ImGui windows, so a shot is the movie alone, fitted with its own aspect |

The first 30 ticks always run at the movie's own frame rate even when paused, so
a "paused" viewer shows a **settled** screen: a Flash UI is built by the actions
of its first frames, and freezing at frame 0 would show an empty stage.

### The dump

`-dumpui` is the reason another agent can use this without a mouse:

```
UiViewer: --- main.vmf: 231 element(s) ---
UI  movie     path                    type    char  frame  state  bounds
UI  main.vmf  /pause_menu             sprite  141   0/1    vis    ...
UI  main.vmf  /pause_menu/menu_sq     sprite  205   0/52   vis    ...
UILabel  main.vmf  /pause_menu/menu_sq    default, open, wait, stop, close
UI  main.vmf  /pause_menu/menu_sq/sq0 sprite  212   0/5    vis    ...
```

`UILabel` lines are the **states each clip can be forced to**, i.e. the state
machine a native rebuild has to reproduce. That is the piece that makes the
inventory useful for a 1:1 replica rather than just a list of names.

---

## 3. The label file

`docs/re/ui_elements.txt`, TAB separated, sorted by movie then clip, exactly the
`asset_labels.txt` contract - one writer per line, mergeable, hand-editable:

```
<movie .vmf>	<clip path>	<label>	<note>
```

`source/game/ui_labels.{h,cpp}` (`GetUiElementLabels`, `FindUiElementLabel`,
`SetUiElementLabel`, `SaveUiElementLabels`), and `-lintui` is the accuracy check
that `-labellint` is for the asset labels.

`-lintui` settles the movie and then walks **every label of the root timeline**,
so a key is checked against every reachable screen rather than only the frame
the movie opens on. That matters here: `/pause_menu/*` does not exist until the
root is in `pause_stop`, so a lint that only looked at the opening frame would
report every pause-menu key as broken.

The file in this commit is seeded **only** with keys `game/pause_menu.cpp`
already resolves by name, so it starts from verified ground truth:

| key | where it comes from |
|---|---|
| `/` | the root timeline; its labels are the movie's states |
| `/pause_menu/menu_sq` | `findClip("/pause_menu/menu_sq")` in `PauseMenu::tick` |
| `/pause_menu/menu_sq/sq0..sq8` | `setVariable`/`gotoLabel("/pause_menu/menu_sq/sq" + n)` |
| `/pause_menu/clock/time/disp` | `findClip(kDisp)` in `refreshClock` |
| `/pause_menu/clock/time/day`, `/night` | `gotoLabel(..., "on"/"off")` |

Since the labels name states, every key above resolves only in the **pause**
state, so `-lintui` needs the state walk described above (it reports 0 problems).

---

## 4. Verified

```
-gm uiviewer -lintui
  UI lint: 16 label(s) over 1 movie(s), 0 problem(s) - every key is a display list path

-gm uiviewer -dumpui
  UiViewer: main.vmf: 1280x960 stage (px, 1 px = 20 twips), 210 frames @ 30 fps,
            5068 tag(s), 525 bitmap(s)
  UiViewer: --- main.vmf: 110 element(s) ---          (default state)

-gm uiviewer -goto "/:pause_stop" -dumpui
  UiViewer: jumped '/' to 'pause_stop'
  UiViewer: --- main.vmf: 965 element(s) ---          (the pause screen)

-gm uiviewer -goto "/:pause_stop;/pause_menu/menu_sq:stop" -clip /pause_menu/menu_sq/sq3 \
    -autoplay "70:shot=..." -exitframe 95
  UiViewer: selected '/pause_menu/menu_sq/sq3' (sprite, character 18565, depth 82,
            frame 0/208, visible, bounds known)
```

Screenshots: `docs/images/ui_viewer.png` (main.vmf, the pause state, sq3 selected
with its label `entry square 3` / note `photo` loaded from the label file) and
`docs/images/ui_viewer_log_in2.png` (log_in2.vmf playing, the display list tree
indenting `/`, `#1`, its child `#1`, `#3`).

What the 965-element dump already hands over, without any further work - these are
the states a native rebuild must model:

```
/pause_menu/menu_sq/sq8            close, close_lock, default, on, on_h, on_lock,
                                   on_lock_h, open, open_lock, out, out_h, out_lock,
                                   out_lock_h, stop, stop_lock, stop_on, stop_on_h,
                                   stop_on_lock, stop_on_lock_h, valid, valid_h
/pause_menu/menu_sq/looper          default, loop_dec, loop_inc
/pause_menu/clock                   close, default, open
/pause_menu/clock/time/day          default, off, on, stop_off, stop_on
/pause_menu/clock/time/disp/number0..3   change, default
/pause_menu/menu_sq/sq8/ic/icLoaded achievement, add_friend, audio, audiovideo,
                                   av_title, black_list, chat, controls, default,
                                   device, display, exit_game, game_progress,
                                   game_settings, gamma, gme, help_options, invite,
                                   join, language, leaderboards, leave_chat, link,
                                   moderate, new_game, options, options_title, photo,
                                   players, profiles, quit_cod, quit_race, recruit,
                                   relief3d, relief3dps3, remove_friend, restart,
                                   resume, resume_cod, save, save_title, stop_test,
                                   unblack_list, video, xbox_live_party
```

That `icLoaded` list `[DATA]` is the **menu icon set**: one label per icon the
pause squares can show. It is the cleanest evidence yet that the frontend is a
small, enumerable state machine rather than an open-ended amount of work.

---

## 5. What is next (for whoever picks this up)

Ordered by value per hour, and none of it needs the viewer rewritten.

1. **Walk the inventory.** `-dumpui` per movie is the fast way in (text beats the
   mouse for the bulk pass): `main`, `hud`, `gauges`, `general`, `map`,
   `shopsacar`, `shopscar`, `shopsavatar`, `club`, `home`, `results`, `r_school`,
   `challeng`, `gmeditor`, `orcscreen`, `spotviewer`, `viewer`, `tdmedia`, the 9
   `cas_*` casino screens, `log_in2`, `intro`, `dlc`. Then name the elements that
   matter in `docs/re/ui_elements.txt`. `-lintui` keeps it honest.
   **Ibiza first** (the owner's scope): Hawaii is not reachable yet.
2. **The recorder (`-logui`).** The viewer snapshots; what is missing is the
   *history*. Hook `FlashPlayer::placeObject` / `removeObject` / `gotoLabel`
   behind a flag and log `(frame, path, event)` while the real game runs (pause,
   garage, dealership, casino). That finds the elements that only exist on a
   later frame - the ones `-lintui` can only see when the state is reached.
3. **An options-screen pass.** `PauseMenu` opens the options pages inside the same
   MAIN.vmf root (`options` / `help_options` / `newOptions`). Those pages are
   where `GameOptions` already maps every retail row, so the dump gives a direct
   cross-check between the movie and `game/game_options.h`.
4. **Then the dial.** `FreeRideHud` already drives a 4-slot in-car menu
   (`/HUD/ig_menu`: Roof, Indicators right, Windows, Indicators left, with
   `/:HasHood` / `/:HasWindows` / `/:HasTurnSignals` locks) `[CODE:
   freeride_hud.h kCarMenuEntries]`. A radial dial is a **restyle of a working
   element plus more slots**, not a new system. Headlights and indicators already
   work end to end (`CinematicCar::setHeadlights` / `setDashboardLit` /
   `getIndicator`, `docs/re/car_lights_spec.md`).
5. **Engine start / stop is the one real engine gap.** What exists
   (`gm_freeride.cpp`: `ap.bEngineOn`) is an **audio duck** flag, not an ignition
   state. A real one gates the engine loop gain, the RPM needle, the throttle
   response and the key SFX. Small, but it is new state, not a restyle.

## 6. Known limits of this tool as it stands

- **The pause screen previews mostly black.** MAIN.vmf's pause screen is
  *game-driven*: `PauseMenu::layoutEntries()` writes the square row's `_x`/`_y`
  every frame, because the movie's own `setSquareMenu()` returns early
  (`pause_menu.h`). The viewer does not run that code, so the stage shows the
  movie's authored art but not the imposed row. The **display list is still
  complete and correct** - that is what the tool is for - but do not read the
  stage as a screenshot of the retail pause menu.
- The display list is rebuilt **every frame** (the timeline moves objects), so
  the selection is kept by *path*; an element that only exists on a frame you are
  not on is simply not in the list. `-goto` / `step` reach it - which is exactly
  what the recorder in step 2 would remove.
- The walk caps at 6000 nodes and 24 levels of sprite nesting (`kMaxElements`,
  `kMaxLevel`), so a pathological movie cannot stall the frame.
- A text field's *drawn* height depends on the resolved string, which the viewer
  does not evaluate, so a `CT_EditText` box is the authored box, not the wrapped
  text's.
- `-lintui` walks the root timeline's labels. A screen reachable only by a nested
  label (not a root one) still has to be reached with `-goto` before its keys
  validate.
- The panels are laid out in DPI-scaled points, and the display-list column is
  clamped to 45% of the display height; on a HiDPI 720p backbuffer the three
  panels are cramped. At 4K (the owner's setup) they are roomy.
- Nothing here reads Hawaii specially: the movies are the same set, so the same
  tool covers both islands once Hawaii is reachable.



