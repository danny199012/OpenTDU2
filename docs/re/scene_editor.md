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

## 4. Interiors and the walk (M4, implemented)

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

## 4. How a scene gets into the game (the routes)

- **Now**: prop placements via `commit_props` -> `<mods>/props.txt` (the
  proven ModProps route; solid/fuel columns available by editing the line).
- **In the editor**: everything draws immediately (the editor mode
  instantiates the document itself).
- **M3**: the game modes load `<mods>/scenes/*.scene.json` the same way they
  load `props.txt` — the scene becomes a runtime mod feature, not an editor
  artifact.
- **M5 (native)**: the BNK packer + the `.3dd/.3dg/.2dm/.2db` writers turn a
  scene into `exterior/<name>.bnk` + a real `spots.bin` record (the
  `add_spot.py` route in C++), and patch sector `.pmi` lists for
  library-node placements.

## 5. The editor UI

One ImGui window ("Scene Editor", F2 toggles): Scene (island radios, name,
new / open / save / undo / redo), Assets (search + kind tabs — library /
interior / spot / actor / prop; double-click places 8 m ahead of the camera,
snapped to the surface), Outliner, Properties (transform, attach-to combo, at
camera / walk inside (F6, interiors) / snap ground / duplicate / delete),
World pick / detach (the pick line + "hide this node" for what the cursor is
on), Roads, Export. Camera = the WORLD_VIEWER camera; LMB picks (drag on the
selection moves it on its ground plane), RMB looks, F6 walks (M4).

## 6. What we must not do (unchanged, docs/re/modding.md 4)

The editor writes only `<mods>/` (scenes, props.txt) and the screenshot /
export paths it is given. The game folder is never written; no game data
lands in the repository.
