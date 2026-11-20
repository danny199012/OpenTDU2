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

Object kinds (the asset browser of the editor lists all four):

| kind | `ref` | drawn by |
|---|---|---|
| `group` | — | nothing; an attach parent (its children compose its transform) |
| `library` | a `commonworld.bnk` library node name (or `hc:<hex>`) | `EdenWorld::drawLibraryNode`, the shared model (nothing is uploaded) |
| `actor` | a `worldactors.bnk` entity name (`FU_Chair_act`, `RS_Dir*`...) | the actor's model loaded on demand (`EdenWorld::getActorModelPtr`) |
| `spot` | a building bank (`EAS_B`, `ECD_AUDI`...) | joins the island's `spots.bin` records as an extra `SpotRecord` (`SpotExteriors::appendSpots`) — one streaming path for every building |
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
- **M3**: the game modes load `<mods>/scenes/*.scene.json` the same way they
  load `props.txt` — the scene becomes a runtime mod feature, not an editor
  artifact.
- **M5 (native)**: the BNK packer + the `.3dd/.3dg/.2dm/.2db` writers turn a
  scene into `exterior/<name>.bnk` + a real `spots.bin` record (the
  `add_spot.py` route in C++), and patch sector `.pmi` lists for
  library-node placements.

## 5. The editor UI

One ImGui window ("Scene Editor", F2 toggles): Scene (island radios, name,
new / open / save / undo / redo), Assets (search + kind tabs; double-click
places 8 m ahead of the camera, snapped to the surface), Outliner, Properties
(transform, attach-to combo, at camera / snap ground / duplicate / delete),
World pick / detach (the pick line + "hide this node" for what the cursor is
on), Export. Camera = the WORLD_VIEWER camera; LMB picks (drag on the
selection moves it on its ground plane), RMB looks.

## 6. What we must not do (unchanged, docs/re/modding.md 4)

The editor writes only `<mods>/` (scenes, props.txt) and the screenshot /
export paths it is given. The game folder is never written; no game data
lands in the repository.
