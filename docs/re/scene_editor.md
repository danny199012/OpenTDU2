# The scene editor (`-gm SCENE_EDITOR`) and its scene document

Tags: `[CODE]` port code, `[INF]` inferred / developer-tool values.

The scene editor composes new content from what the game already ships, over
the streamed island (the WORLD_VIEWER skeleton: streaming, weather, free
camera, `source/game/scene_editor/`). It saves one JSON scene per file under
`<mods>/scenes/`, applies it live in the editor, and the AI / headless
interface (`docs/studio_ai_api.md`) drives it with or without a window.

## 1. The document

`<mods>/scenes/<name>.scene.json` — plain JSON (the minimal parser of
`scene_json.{h,cpp}`; no third-party dependency):

```json
{
  "version": 1,
  "island": "ibiza",
  "name": "my_scene",
  "objects": [
    { "id": "shell", "kind": "group", "ref": "", "pos": [12630, 10.4, 16698], "yaw": 90 },
    { "id": "canopy", "kind": "library", "ref": "??MASSIF", "parent": "shell", "pos": [0, 0, 0] },
    { "id": "shop", "kind": "spot", "ref": "EAS_B", "pos": [12630, 10.4, 16698], "yaw": 0 },
    { "id": "chair", "kind": "actor", "ref": "FU_Chair_act", "pos": [12610, 10.4, 16705] },
    { "id": "pump", "kind": "prop", "ref": "FUEL1", "pos": [12620, 10.4, 16690], "scale": [1, 1, 1] }
  ],
  "overrides": [
    { "kind": "node", "target": "shutters", "hidden": true }
  ],
  "roads": []
}
```

Object kinds (the asset browser of the editor lists all five):

| kind | `ref` | drawn by |
|---|---|---|
| `group` | — | nothing; an attach parent (its children compose its transform) |
| `library` | a `commonworld.bnk` library node name (or `hc:<hex>`) | `EdenWorld::drawLibraryNode`, the shared model (nothing is uploaded) |
| `actor` | a `worldactors.bnk` entity name (`FU_Chair_act`, `RS_Dir*`...) | the actor's model loaded on demand (`EdenWorld::getActorModelPtr`) |
| `spot` | a building bank (`EAS_B`, `ECD_AUDI`...) | joins the island's `spots.bin` records as an extra `SpotRecord` (`SpotExteriors::appendSpots`) — one streaming path for every building |
| `interior` | a walkable room bank: the stem (`icd_ferr`), a shop spot ref (`ECD_FERR` → its `icd_ferr`), or a house ref (`EPH_L2E_0345I`, `:garage` suffix for the garage bank) | the same `SpotRecord` streaming path as the spots, with the bank resolved under `interior/` (`scene_interior.cpp`; M4) |
| `prop` | an OBJ name of `<mods>/props/` | `LoadObjDraws`, the ModProps route |

Fields per object: `pos` `[x, y, z]` (world metres), `yaw` / `pitch` / `roll`
(degrees; the D3D row-vector law `T * RotY * RotZ * RotX * S`, the same
composition the `.pmi` decode uses), `scale` `[x, y, z]` (default 1), `visible`
(default true), `parent` (another object's id; the transform composes on top
of the parent's — the attach mechanism).

## 2. Overrides (the detach / hide)

An override hides a baked piece of the streamed game everywhere:

- `kind: "node"` — every node whose hashcode matches (and its whole subtree):
  the port's own `nodeVisible` state per model.
- `kind: "material"` — every item drawn with that material: the port's
  `hiddenItems` state (the same mechanism the car wash forecourt plates use).

A `target` is a name (matched through `GetIdentifier64bit`, the game's own
8-slot hash; a long material name also matches verbatim, case-insensitive) or
`hc:<16 hex>` when the parser could not recover a name (the `???????O`-style
labels of the asset browser). The sets live in `EdenWorld`
(`addHiddenNode` / `addHiddenMaterial`), are applied at every sector / area /
library load, and the editor re-applies them to the streamed spot banks every
frame (`SpotExterior::applyVisibilityOverrides`). What the
`-lognodesnear x,z,r` / `-logstationmats` flags print is exactly what the
targets are.

## 3. Roads (M3, implemented)

A `roads` entry: `id`, `points` `[[x, y, z], ...]` (the spline control
points), `width` (m), `lanes`, `closed` (a loop), `collide` (the car stands
on it), `texture` (a props-folder PNG; empty = the generated
`asphalt_road.png`).

`source/game/spots/mod_roads.{h,cpp}` (the mod_props sibling) is the runtime:
Catmull-Rom resampled ~2 m, an 8 m ribbon with distance UVs built as OBJ text
in the authoring space (the loader mirrors X, the mod_props law) and built
with `BuildObjDraws`; the samples that dip under the heightfield lift to the
terrain (+5 cm, bridges keep their explicit height). Registered three ways:

- **draw** — the identity-frame ribbon in the scene view's callback (the
  editor and FREERIDE),
- **surfaces** — `EdenWorld::addRoadSurfaceTriangles` (world-space, up-facing,
  8 m XZ buckets): `getSurfaceHeight` / `getDriveHeight` answer a scene road
  like a retail one,
- **collision** — `VehicleWorldCollision::addStaticTriangles` (FREERIDE),
- **grass** — the centerlines merge into the grass road-suppression index
  (`EdenWorld::appendRoadShapes`).

FREERIDE loads `<mods>/roads/<island>.json` (with the editor's
`export_roads` output) unless `-nomodroads`. The editor imports the island's
roads file into a fresh document (`load_roads` re-imports), `bake_road`
writes `props/road_<id>.obj` + mtl + a `solid` props.txt line (the vanilla
route). Commands: `add_road`, `add_road_point` (snapped to the surface),
`set_road`, `remove_road_point`, `remove_road`, `export_roads`, `load_roads`,
`bake_road`. Traffic / GPS graph integration (ROAD_MAP writers) stays open.

## 4. How a scene gets into the game (the routes)

- **Now**: prop placements via `commit_props` -> `<mods>/props.txt` (the
  proven ModProps route; solid/fuel columns available by editing the line).
- **In the editor**: everything draws immediately (the editor mode
  instantiates the document itself).
- **M5 first slice (shipped)**: `export_spots` writes the placements as real
  `spots.bin` records through the mods overlay (the `add_spot.py` route in
  C++) and copies the referenced interior banks to the overlay's
  `exterior/` — FREERIDE streams the scene with no engine change (see 6).
- **M5 next**: the `.pmi` patch (library-node placements into the sector
  banks) and the composed `.3dd` bank build (a scene compiled into one new
  `exterior/<name>.bnk`, OBJ props authored to `.3dg`).

## 5. The editor UI

One ImGui window ("Scene Editor", F2 toggles): Scene (island radios, name,
new / open / save / undo / redo), Assets (search + kind tabs — library /
interior / spot / actor / prop; double-click places 8 m ahead of the camera,
snapped to the surface), Outliner, Properties (transform, attach-to combo, at
camera / walk inside (F6, interiors) / snap ground / duplicate / delete),
World pick / detach (the pick line + "hide this node" for what the cursor is
on), Roads, Export. Camera = the WORLD_VIEWER camera; LMB picks (drag on the
selection moves it on its ground plane), RMB looks, F6 walks (M4).

## 6. Native export (M5, first slice: bank writers + spots)

Two writers came out of `tools/x360/build_bank.py` (the Python bank writer)
into C++, both **validated by byte-compare round-trips**:

- **`source/filesystem/bank_write.{h,cpp}`** — the BNK packer. `BuildBank`
  packs entries into a PC (`KNAB`) bank; `otdu2_bnk_tool pack <folder>
  <out.bnk>` packs a folder tree, `otdu2_bnk_tool roundtrip <bank.bnk>`
  rebuilds a retail bank (entries, per-entry CRCs / dates, section CRCs, the
  raw directory tree, the per-file section / data / file alignments, the
  filler-filled gaps, the streamed tail) and compares **whole-file bytes**.
  Nine retail banks of every family rebuild byte-identical: frontend
  (Intro), commonworld (478 entries), an interior bank (icd_ferr), an
  exterior bank (eph_l3di), the worldactors pack (714 entries), ias_a, an
  area bank, a sector bank and a vehicle bank (250_gto). Known limitation:
  banks without a directory section (`avatar/config/pnj.bnk`) rebuild with
  one invented — the reader parses both.
- **`source/render/render_file_write.{h,cpp}`** — the RenderFile container
  writer (`.2dm / .3dd / .3dg / .2db / .xsb`): sections with payload /
  children / trailing split, DataSize measured from the section start.
  `otdu2_bnk_tool roundtripfile <file>` rebuilds from the parsed tree and
  byte-compares: 218 payloads of four banks (interior, exterior, vehicle —
  cube-map textures included) rebuild byte-identical.

**`export_spots`** (the editor command / Export panel button, the
`add_spot.py` route in C++, `source/game/scene_editor/scene_export.{h,cpp}`):
the scene's `spot` and `interior` placements append as **real 0x110-byte
records** to the mods overlay's
`<mods>/Euro/Bnk/islands/<island>/level/spots.bin` (a copy of the island's
own list; the size field keeps the file's own law, the 16-byte tail moves to
the end), and every referenced interior bank copies to
`<mods>/.../exterior/<bank field>.bnk` (the retail reader resolves every
exterior there; the field is the interior stem truncated to 8 chars).
Records are **upserts** (the record name is the scene object's id — moving a
building and re-exporting rewrites its record in place). Verified headless:
the editor exported 2 placements → FREERIDE (no editor, no engine change)
loaded the overlay list ("Mod overlay: ...spots.bin", "81 spots, 81 with an
exterior bank") and streamed the scene buildings through the retail records
(`ECD_AUDI ...: 3 placements` = the island's 2 + the scene's 1;
`icd_ferr at (12620, 16700): 4 models, 1 placements` from the copied bank).

## 7. Native export (M5 second slice: the .pmi patch)

**`source/world/pmi_write.{h,cpp}`** — the .pmi writer (the write side of
`pmi.cpp`): the flat container law (the RenderFile header, the 0x1c0-byte
MOBA header with its quadtree cell table copied verbatim — the pmi.cpp
comment's "0x1d0" is wrong, the retail data says 0x1c0), the MOB. sections
with each section's own reserved header bytes preserved (they vary per
section), the records' whole payload REGIONS (the 16-byte-stride padding
carries data in the type 3/4 lists), and the u16[16] record encode as the
exact inverse of the decode laws (`x = w*0.0156252384 − 128` — the offsets
ADD on encode; the angles normalize into [0, 2π); the height range widens
when a new instance's y falls outside, re-encoding every record's w[1]).
`otdu2_bnk_tool roundtrip-pmi <sector bank>` re-encodes a bank's four lists
unchanged and byte-compares: 20 sector banks of Ibiza and Hawai rebuild all
lists BYTE-IDENTICAL.

**`export_pmi`** (the editor command + Export panel button): the scene's
`library` placements append as type 1 records to the landing sector's `-o`
list, the `actor` placements as type 2 records to `-a` (the sector =
`EdenWorld::getSectorAt` of the composed placement; the bank = the overlay
copy or the retail one, so re-exports stack and same-model placements
within 0.5 m are skipped). The sector bank repacks through the BNK writer
(the captured directory tree + leaf order, the source alignments, the
stream tail) into `<mods>/Euro/Bnk/islands/<island>/level/...`. Verified end
to end headless (build 400): two `V_FOUG_A` ferns exported at (12630,
16720) yaw 30 deg and (12652, 16732) yaw 200 deg -> FREERIDE with no editor
loads the overlay sector bank and the instances resolve at the exact world
positions with the model's bounds as the AABB (`NEARINST type 1 V_FOUG_A
pos (12630.0 10.8 16720.0) box ...`), 0 parse failures in the whole sector.

Remaining M5: the composed `.3dd` bank build (a scene compiled into one new
exterior bank; OBJ props authored to `.3dg/.2dm/.2db`).

An `interior` object is one of the game's walkable rooms
(`euro/bnk/interior/<stem>.bnk`: the shops' `icd_*` / `ias_*` ..., the houses'
`lph_*` living + `gph_*` garage rooms — spot_interiors_spec.md 1). Three ref
forms, resolved by `SceneInterior::ResolveBankPath` (a bank only counts when
the file exists): the bank stem, the shop spot bank (the retail
`InteriorWalk::InteriorBankFor` rule), the house spot (`:garage` for the
garage room). The room draws through the same `SpotExteriors::appendSpots`
streaming path as a `spot` placement (its `ExteriorPath` just points into
`interior/`), so lightmaps, hide overrides and the streaming distances are
the retail machinery. Spot AND interior records resolve their parent chains
(`doc.resolveWorlds`): position + heading come out of the composed matrix
(the doc's own yaw; pitch / roll / scale do not survive a spot record), so
attaching a room — or a building — to a group composes.

Composition: furniture goes inside a room by attaching to it — the chair
actor, a library node, a prop placed at the room's bank-local transform
(child `pos` is the parent-local space, the same law as the groups). The
room's footprint joins the grass-suppression set (`EdenWorld`
`addGrassClearing`, re-derived from the loaded interior banks every stream
pass): the retail spots sit on cleared plots, a scene room in a field
otherwise grows the tall grass through its floor.

The walk (F6, or `enter_walk`): first person inside the selected / nearest
interior placement — the bank's own `.shk` collision and the retail
`Entity__FirstPerson` start camera (`cam_entree` in every bank checked) are
extracted straight from the bank (`SceneInterior::LoadWalkData`), and
`WalkController` drives bank-local with the `WalkMode::collide` law (the
0.3 m sphere pushed out of the walls, 0.4 m step / 2 m drop, the freeze
off the room's floors). The camera renders the streamed scene from
`frame * eye`, so the room and the world draw exactly where the walk
happens. Without any interior placement the walk covers the scene grounds
(terrain / road floors, no walls). Esc / F6 ends it; the fly camera parks at
the walk eye. This is a compose / feel tool — the retail walk (avatar,
cinematics, shop menus) stays the game modes' route. Commands: `enter_walk`
(`id` / bank-local `pos` + `yaw` overrides), `exit_walk`.

## 8. Polish (M6: the gizmo + the material colors)

**The 3-axis translate gizmo** (F3 toggles): the world-axis lines from the
selection's origin with grab handles and an origin square (the ground-plane
drag it already had). LMB on an axis drags the object along that axis: the
mouse ray's closest point on the fixed axis line (the ray / line closest
point), one undo entry per drag, the grid snap applied. `move_axis` (the
AI-drivable form) runs the same screen-delta math synthesized from the
screen centre — verified: an X drag of 150 px moved the object exactly along
X (y / z untouched), a Y drag lifted it and a second one brought it back.

Two render laws the gizmo surfaced:
- **draw() is OUTSIDE the ImGui frame** (tdu_instance.cpp draws the 3D pass
  before `pImGui->beginFrame`): background-draw-list commands added in
  draw() are cleared by the next NewFrame and never render — the M1
  selection box had never actually drawn. Both overlays moved into
  `drawSceneUI()` (inside `drawUI`, inside the frame).
- The ImGui draw-list space is **io.DisplaySize** (the window points at the
  DPI scale), not the framebuffer the scene params carry — the projection
  converts through `ImGui::GetIO().DisplaySize`.
- And the world→clip law: **`params.Projection * params.View`** (the
  renderer's own composition, `Projection.mulPrecise(View)`), NOT View *
  Projection — clip.w of the old order came out as the negative raw Z and
  everything behind the camera failed.

**The material color overrides** (M6): a scene override of kind `"color"`
(`target` = a material name / `hc:<hex>`, `color` = [r, g, b]) replaces the
matching materials' Diffuse.rgb everywhere (the same matching law as the
hides; the original saved on the first application so a cleared override
reverts; applied at every model load and to the streamed spot banks like
the hides). `set_material_color` (the command) and a recolor row per
material of the picked world node (the World pick / detach / recolor
section; the live swatches edit in place). Verified: `Concrete_stripe` of
the EAS_B bank recolored red — the A/B crops show the red band on the
facade; the override round-trips through the scene JSON.

## 8b. The composed bank build (M5 last: `export_bank`)

The scene compiles into ONE new exterior bank — the retail spot-bank shape,
every file authored by the port:

- **`source/core/xmb_write.{h,cpp}`** — the XMB writer (the write side of
  core/xmb.cpp). The serialization law, decoded against retail files: the
  sorted unique string table padded with 'A' bytes to the type table's
  8-boundary (retail tables hold ORPHAN strings no record references — a
  rebuild unions the file's own strings with the record names); type records
  (composite { hi=size/2, lo=fields<<4 } + one SWAPPED { name, typeword }
  record per field, every record holding a type-table slot, references
  forward-patchable); the data section as a packed image where ARRAY ELEMENTS
  and STRING BYTES are appended by a FIFO queue after the containing
  composite; the data of an 8-byte-element array (u64/double) starts
  8-aligned ('A'-padded — hashcodes.xmb's empty arrays prove it); VIRTUAL is
  an 8-byte slot in the retail tables (the port reader's 4 misaligns
  composites holding them); some retail tables have no "AAAAAAAA" terminator
  (a rebuild preserves its presence). `otdu2_bnk_tool roundtripxmb <file>`
  rebuilds and byte-compares: hashcodes.xmb and DeviceX360Pad.xmb rebuild
  BYTE-IDENTICAL; entities.xmb / Default_KB.xmb parse to the same values (the
  retail writers left unreferenced garbage — 'AAA ' placeholder blobs and a
  '##...KEYMAP##' trailer — the parse never sees).
- **`source/render/render_resources_write.{h,cpp}`** — the four render
  resource writers over the RenderFile container: `.2db` (RGBA8 -> VF_A8R8G8B8
  with a box-filter mip chain), `.2dm` (MATA { HASH, MAT. per material } — the
  retail 0x340 payload law), `.3dg` (GEOA { HASH, GEOM { PRIM } }, VBA.
  { DXVB }, IBA. { DXIB }: one interleaved stream pos f32x3 / normal f32x3 /
  uv f32x2, DXVB flags 0x1800, 16-bit DXIB), `.3dd` (MBNA { MBNK }, OBJA,
  INSA, HIEA — an identity root INS, an instance per placement, INS +0x50
  pointing at the OBJA HASH ENTRY (payload, not section)). `HashResourceName`
  (FNV-1a 64) is the rsc/geometry hash the two XMB files cross-reference.
- **`export_bank <name>`** (the command + the Export panel's name field +
  compile button): the scene's `prop` placements -> one `.3dd` (anchor = a
  `spot` object named <name>, else the first prop; locals are anchor-relative
  so a moved anchor re-exports in place), one merged `.2dm` (the .mtl
  materials as TD_Gouraud clones — the OBJ route's material law including the
  alpha<1 glass state), one `.2db` per map_Kd texture, the two XMB files, all
  packed with the bank writer into
  `<mods>/Euro/Bnk/islands/<island>/exterior/<name 8 lower>.bnk`. The anchor
  placement UPSERTS into the island's `spots.bin` (the "SN<name>" record).
  The parse-back check: the bank reloads, the rsc resolves to the `.3dd`, the
  geometry / materials / entities.xmb parse.
- Verified end to end (build 402): a two-prop scene exported; FREERIDE with
  no editor loaded the overlay bank through the retail spot path ("m5test at
  (12700, 16900): 1 models, 1 placements"), the spot items resolved with the
  authored bounds (24 verts), the spot frames carried the exact record
  position, and an A/B (the record moved +500 m) proved the drawn geometry is
  the composed bank. Screenshot docs/images/composed-bank-m5test.webp.
- **The physics test area** rides this: tools/mod/testarea/ (README) is a
  composed bank whose material names carry the tyre surface — see
  docs/re/scene_editor.md 10.

## 10. The physics test area (`testarea`, tools/mod/testarea/)

A composed bank for straight handling work: a 140 m pad (skidpad ring +
cross lines), 2/5/10/20/30 % hill ramps with a plateau and a -20 % descent,
and a 12 x 90 m dirt strip. The authored bank carries no `.shk`: its kept
model triangles ARE the drive collision (`SpotExterior::collectDriveTriangles`,
per item = per material), each triangle carrying an explicit tyre-surface
override (`HavokCollision::TriSurface` -> `VehicleWorldCollision::Triangle::
Surface` -> the raycast `Hit::Surface`): a material name containing "dirt"
maps to the dirt tyre (index 1), "grass" to grass (2), else tarmac (0). The
override also sets the hit's reference friction (tarmac 1.0, dirt 0.6, grass
0.4 — the value the assists / HUD read); the tyre grip itself is the car DB's
Pacejka `Config[surface]`. `Hit::eSurface` grew `Dirt` (SurfaceIndex maps it
to TyreSetup::Dirt). The bank's footprint joins the grass-suppression set on
load (the pad otherwise grows the tall grass through itself), and a car that
spawned inside the slab (the mode's fit probe runs before the bank streams)
is popped on top once ("test area refit" in the log). `-logsurface` prints
the per-wheel surface / contact / friction trace every 30 frames — the A/B
for handling changes. Install + rebuild + the tuning workflow:
tools/mod/testarea/README.md. Verified: the pad reads 0/1.00 on all wheels,
the dirt strip 1/0.60, the surrounding terrain 2/0.70 (the SURFACE trace
crossing all three).

## 11. The petrol station tool (`add_station`)

A scene's `stations` array (`SceneStation`: `id`, `name`, `pos`, `yaw`, `length` 34, `width` 24, `connect`, `max_link` 400)
is the source of truth for one station; its parts are GENERATED from it and rebuilt by `add_station` / `set_station`
(`source/game/scene_editor/scene_station.cpp`):

- `<id>_ground` - a short wide scene road (the "road ground" forecourt; the ordinary ModRoads ribbon + collision +
  driveable surface + grass suppression), long axis `(sin yaw, cos yaw)`.
- `<id>_link` - a 7 m drive from the slab end nearer the road to the nearest **retail** road point (the ROAD_MAP graph:
  the editor owns a `MapScreen` used only for `getRoadPointNear`), ending on that road at its own height.
  `yaw` omitted = auto: the slab axis points at the nearest road.
- `<id>_canopy` - a `prop` object, the generated `station_canopy.obj` (roof on four posts + two pump islands, flat-colour
  PNGs written to `<mods>/props` on first use: our own art, no game texture).

Commands: `add_station {id?, name?, pos?, yaw?, length?, width?, connect?, max_link?}`, `set_station {id, ...,
auto_yaw?}`, `remove_station {id}`, `export_stations`. The Roads panel has the buttons (new at the camera focus,
rebuild, remove, export). `export_stations` = the roads file (`export_roads`) + this scene's block in
`<mods>/stations.txt` (replaced, not appended: tagged `# otdu2-scene-stations: <scene>`) + the canopy line in
`props.txt` (once per name + position). That is everything the game needs: the pump screen, the repair row, the map
icon and the forecourt lights all come from the `stations.txt` entry (`service_stations.cpp`).

**Traps.** The surface height only answers where the terrain is streamed: a scripted run places stations near the
start spot (ETUN_A, 12605 / 16698); elsewhere the result carries a WARNING and the pos y is kept (fly the camera there
and `set_station`). The pump screen needs the fuel mod: `-fuelmod` (or the `fuel-system` package).
A station added on a spot that holds retail props (the container yard at 12650 / 16500) drives into them - the tool does
not check for obstacles. Traffic / GPS do not know the new roads (ROAD_MAP writers stay open).

Verified (headless, build 407): `add_station` at (12600, 16800) -> y 10.3, yaw 68.7, "drive 30 m to the retail road at
(12644, 16817)"; `export_stations`; FREERIDE `-fuelmod -tp "12600,25,16800"`: stations.txt 14 entries, the ribbons and
the canopy load, the prompt "Fill / Car repair: OTDU_Test_Station", validate opens the pump screen
(`docs/images/station-tool-pump-screen.webp`), the car drives slab -> drive on surface `0/1/70`
(`docs/images/station-tool-drive.webp`).

## 12. Editor polish round 2: rotation, snapped duplicate, road surface / grade / bank, undo, drive test

- **Rotation gizmo** (`gm_scene_editor.cpp` tickGizmo / drawGizmo): F4 (or `gizmo_mode {mode: "rotate"|"move"}`) turns the three
  axes into rings - green = yaw (Y), red = pitch (X), blue = roll (Z); drag a ring, Shift = 15 deg steps, one undo entry
  per drag. The drag sign is MEASURED on the real object matrix (`RotationSign`), not assumed. Pitch / roll rings are the
  world axes (the engine's order is RotY * RotZ * RotX, so with a yaw they only approximate the object's own axes).
  `rotate_axis {axis, degrees}` is the AI form. Verified: the rings draw (`docs/images/editor-rotate-gizmo.webp`) and
  `rotate_axis` changes yaw / pitch; **the mouse drag itself was not tried by hand** (no window input in a headless run).
- **Ground-snapped duplicate**: `duplicate` (Ctrl+D, the button) drops the copy onto the surface under its new place
  (`snap` false = keep y; `offset: [dx, 0, dz]`, default +2 m in X). Verified: a 30 / 25 m offset landed at y 11.06 on a hill.
- **Undo for roads**: every road command already pushed an undo entry (the undo stack copies the whole document, roads and
  stations included). Fixed gap: the road panel's SLIDERS (width, lanes, closed, collide, and the new surface / bank) edited
  the document with no undo entry; they now snapshot before the edit and push one entry per drag (`pushUndoSnapshot`).
  New `undo` / `redo` / `list_roads` commands make it testable; verified: grade + bank + surface edits undo and redo to the exact points.
- **Road surface** (`SceneRoad::Surface`: tarmac / dirt / grass, `set_road {surface}`, `add_road {surface}`, the panel combo,
  `add_station {surface}`): the tyre surface of the ribbon's collision triangles (`VehicleWorldCollision::addStaticTriangles(..,
  surface)` -> the same `Triangle::Surface` override the test area uses, so the car DB's Pacejka `Config[surface]` and the
  reference friction 1.0 / 0.6 / 0.4 apply) + a generated default texture (`dirt_road.png`, `grass_road.png`). A grass road does
  NOT suppress the grass. "" = the old behaviour (untouched). Verified: a dirt road reads `1/1/60` on all four wheels, the
  terrain beside it `2/1/70`.
- **Grade and bank**: the height profile is the control points' y (`road_grade {percent, from_y}` sets a constant slope;
  `set_road_point {index, pos|y|bank}`); `SceneRoad::Banks` is a bank angle per point (`road_bank {degrees}`, the panel
  slider). Positive = the driver's right edge up = banked for a left turn. The section rolls about its LOWER edge (a roll
  about the centre sank half the ribbon under the terrain and raised a 1 m wall on the other side: the first test stopped the
  car dead); give the ends bank 0 or the start is a lip. Verified: a 6 % dirt road climbs 12.3 -> 15.1 m over 46 m under the car
  (`docs/images/editor-dirt-road-grade-bank.webp`). The exported roads file carries `surface` and `bank`.
- **Drive test** (`scene_drivetest.cpp`; `drive_test {id?, launch?, args?}`, the Export panel button): exports the roads /
  stations / prop placements (idempotent) and starts the game as a SECOND process, FREERIDE on the island, the car dropped at
  the target (object, station, selection, the active road's start, else the camera focus) with `-fuelmod` when the scene has
  stations. `launch: false` only reports the command line. FREERIDE's `-heading h` faces (-sin h, -cos h) (measured on the
  flat pad), a scene yaw y faces (sin y, cos y): heading = yaw + 180. `commitPropsToGame` is now idempotent too.
  Verified: the child process starts and the command line reproduces the placement by hand. NOT included: spots / banks
  (`export_spots`, `export_bank` stay explicit steps).

## 9. What we must not do (unchanged, docs/re/modding.md 4)

The editor writes only `<mods>/` (scenes, props.txt) and the screenshot /
export paths it is given. The game folder is never written; no game data
lands in the repository.
