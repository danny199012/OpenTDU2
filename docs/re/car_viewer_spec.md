# The car viewer, the OBJ exporter and the tuner authoring flow (session 48)

The ASSET_VIEWER tool gained a **Cars panel** (`-gm assetviewer -car <name>`, or the
panel's own list): the 279 `DBCarPhysicsData` rows with their display names and tuner
badges, a loaded car drawn in the 3D view (turntable orbit, drag / wheel, WASD free
camera, `EdenModel::raycast` click names the hit node), an **OBJ exporter** for the whole
assembly, and the tuner authoring loop (fit parts, edit the hide boxes with an overlay,
nudge offsets, copy the `tuner.txt` lines). This doc records the pieces a part author and
the next session need: the flags, the export format and its round-trip law, and the
workflow that replaces the `-dumptunermask` dump-and-guess loop.

## 1. Flags and panels [CODE]

| Flag | What it does |
|---|---|
| `-car <file_name>` | open the Cars panel on that `DBCarPhysicsData` row (loads the car into the 3D view) |
| `-cartab` | open the empty Cars panel |
| `-exportcar <name|*>` | load + export OBJ, then exit (`*` = every car; headless) |
| `-objroundtrip <path.obj>` | after `-exportcar`: re-import the file through the game's OBJ loader and compare bounds with the car model (the mirror law's proof, log `ObjRoundtrip: ... PASS`) |

While a car is loaded the other viewer panels hide (the Cars panel + the 3D view are the
car workflow); "unload" or clicking a bank brings them back. The car draws at the origin,
the space everything below (export, anchors, hide boxes) is expressed in.

## 2. The loaded car [CODE]

`selectCar` builds a `CinematicCar::Desc` with `DescFromDatabase` (the DB row's default
rims) and two tool flags:

- `bKeepPrimitiveData` — the car model keeps its primitive vertices / indices past the
  wiper split, so the tuner masks can be **re-collected live** when the viewer edits a
  hide box (`CinematicCar::refreshTunerMasks`).
- `bKeepParsedGeometry` — every model keeps its parsed `.3dg` (`EdenModel::setKeepParsedGeometry`,
  session 48): the exporter reads the real `MeshVertex` streams instead of GPU buffers.

The car paint samples an environment cube; the panel assigns the intro garage's static
`CUBECAR` (`scenarics/cinematics/intro/garage.bnk`, the `IntroCinematic` pattern) — without
it the paint draws near black in the tool's scene. The camera fits the **BODY_HR bounds**
(the raw model bounds carry junk far off the body - flame / shadow planes - that would
frame empty space; the GTR's raw bounds reach z +12.4 [DATA, `-dumpcar`]).

## 3. The exporter and the space law [CODE]

`ExportCarObj` (`game/assetviewer/car_obj_export.*`) writes `<userdata>/export/<car>/`:
`<car>_ext.obj` / `.mtl`, `_interior`, `_rim_front`, `_rim_rear`, `_tire`, and
`anchors.txt`. GTR, measured headless (build 276):

```
ext 129 items, 29978 triangles | interior 68 items, 39479 | rims 4 + 4 items, 1216 each | tire 78 items, 19427
```

**The mirror law.** The engine keeps the cars' `.3dg` authored (left-handed D3D) data
unmirrored (`EdenModel::load` parses with `bMirrorX=false`), and the OBJ part route mirrors
on import (`obj_mesh.cpp` negates x and reverses tri faces). The exporter therefore writes
**x' = −x** and every face reversed **(a, c, b)** — the exact involution — so a Blender
open looks like an authored mesh (right handed, Y up, −Z forward) and re-importing lands
the same triangles. Positions are car space (the node's world-at-load transform applied,
the raycast / collision space). Proof: `-objroundtrip` re-imports the export through
`LoadObjDraws` and compares bounds — `delta 0.0000 -> PASS` on the GTR (2026-11-18).

Groups: one `o` + `usemtl` per engine material — the `usemtl` split is also how the part
loader turns a re-imported OBJ into draws. LOD0 items only; vertices are written per
primitive (no dedup; a car is a few tens of MB).

**anchors.txt** carries the attach numbers: the space law note, tire radius, body lift,
body + rear bounds, the `BODY_HR` / `BODY_MR` / `BUMP_R_H` / `EXHAUST` / `SPOILER` node
translations, the car's `tuner.txt` hide boxes (ITEM space!), tri lists, slot offsets, and
the whole part catalog. **The hide boxes are item (primitive) space** — the space
`collectTunerMasks` tests centroids in — while the OBJ files' x is negated relative to the
engine numbers: convert a Blender-measured box with `x_engine = -x_obj` before writing
`hide_<slot>` into `tuner.txt`.

## 4. The tuner authoring loop [CODE]

For tuner cars the panel lists each slot (spoiler / exhaust) with its catalog parts:

- **Fit live** — a click calls `TunerParts::applyPart` on the viewer car (the garage
  menu's preview path); Stock clears the slot. The car's own EXHAUST node hides while an
  exhaust part is on, exactly as in game.
- **Hide boxes with an overlay** — the car's `hide_<slot>` boxes draw as wireframes over
  the 3D view (orange spoiler / cyan exhaust; item space, through the BODY_HR node
  transform), and the **masked triangles** highlight on request (same test the mask
  collection runs, recomputed CPU-side so a live edit updates both the highlight and the
  real mask). Boxes are edited with drag fields through the runtime override
  (`TunerParts::setRuntimeHideBoxes`), each change re-runs the masks; "copy tuner.txt hide
  line" puts the `hide_<slot> = ...` line on the clipboard and in the log
  (`TunerViewer:`). "revert to file" drops the override.
- **Offset nudge** — drag fields on the fitted part's offset (runtime override in
  `TunerParts::offsetFor`), live re-fit, "copy tuner.txt offset line" prints
  `<slot>_offset = ...` in the same convention the file uses (for `align = rear` parts the
  y/z are relative to the car's rear corner).
- Live edits never touch the save or the repo: they are copies the author moves into the
  mod's `tuner.txt` by hand (the panel says so).

Evidence: `docs/images/car_viewer_gtr_maskbox.webp` — the GT-R in the viewer with the
`hide_spoiler` box wireframed exactly over the stock boot lip.

## 5. Exact triangle masks in tuner.txt [CODE, session 48]

`hide_tris_<slot> = <item index>: <tri>,<tri>,...` (repeat the key for more lines) hides
**exact triangle numbers of one body item**, where a box would catch neighbour triangles
(the GTR lip box was widened twice for exactly this). The item index is the item's index
in the model's item list (`-logcaritems` prints it); triangle numbers are that item's
primitive triangles in index order. `collectTunerMasks` groups by box **or** tri list;
verified headless: GTR `hide_spoiler` box = 656 maskable triangles, adding
`hide_tris_spoiler = 7: 0,1,2` on item 7 ('Paint') -> 659.

## 6. The workflow for a new spoiler / exhaust on a new car (the answer to "modular")

1. Open the car in the viewer (`-car <name> -tunermod`), Export OBJ, open `<car>_ext.obj`
   in Blender (the space is authored-ready; anchors.txt has the node positions and bounds).
2. Model the part against the real deck / bumper; name its material; export it as
   `tuner/parts/<id>.obj` (+ `.mtl` / textures) in the mod package.
3. Add `[part:<id>]` to `tuner.txt` and the car to (or its slots in) the `[car:<file>]`
   block; fit it in the viewer, nudge the offset, copy the `<slot>_offset` line back.
4. If the car carries a stock spoiler as body triangles: draw the box over the lip (or
   pick exact triangles), copy the `hide_<slot>` line back. The stock part returns when
   the player unfits — nothing is ever destroyed (the mask is an index-range swap at draw
   time, `collectTunerMasks` / `applyTunerMasks`).

## 7. Open items

- Triangle-level click picking (paint a mask instead of editing boxes) — the overlay and
  the runtime-override plumbing are ready for it; needs a triangle-exact raycast on the
  kept primitive vertices.
- Textures are not exported (the art stays in the banks); the MTL has flat Kd colours.
- The car list shows duplicate catalog rows for cars that ship more than one physics row
  (the GTR twice) — the file name is the identity, either row loads the same car.
- `-exportcar *` loops every PC-valid row: cars sharing a file name export repeatedly.
  Harmless, just slow.
