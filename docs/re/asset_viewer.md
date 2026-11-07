# The asset viewer, and the asset labels (`docs/re/asset_labels.txt`)

Written 2026-10-29 (Cline, `cline`). The owner's ask: *"could a tool be made like
an asset viewer which I can view and leave hints to them maybe in some .txt ...
I load a casino but file naming maybe random and I can leave hints like casino
exterior, slot machine, Tess character model etc?"* — and, on scope: *"something
that can fully allow us to benefit from not just me but AI to also use ... as long
as it's accurate which I can test and confirm"*.

Tags as in `CLAUDE.md`: `[CODE]` readable executable · `[DATA]` game files ·
`[INF]` inferred or chosen.

---

## 1. The problem, and the thing that makes it smaller than it looks

The game names most assets by an opaque leaf: `fla_0047.2db`, `page_001.2db`,
`MESH_00A1`. But **every bank entry carries its original Eden authoring path**,
already parsed by the engine `[CODE: source/filesystem/bank.h]`:

```cpp
struct BankEntry {
    std::string Path;      // Original path, e.g. "D:\...\FrontEnd\intro\fla_0042.2db"
    std::string Filename;  // "fla_0042.2db"
    std::string Name;      // "fla_0042"
    std::string Extension; // "2db"
    int16_t Group;         // 3 = FrontEnd, 9 = Vehicle, 0xb = Physics, 0xe = Common
    int16_t SubType;
    uint32_t Size;
    int32_t Index;
};
```

so `fla_0047` is really
`D:\Eden-Prog\Games\TestDrive2\Resources\5Prepared\PC\EURO\FrontEnd\Cas_SlotMachine\fla_0047.2db`.
The viewer's first job is simply to **show that path** — most of the "random"
names then identify themselves. What is left is the *meaning* (which symbol is
the jackpot, which mesh is Tess), and that is what the labels are for.

## 2. The tool

`-gm assetviewer` (`launch/14_AssetViewer.bat`) — a game mode, like
`WORLD_VIEWER`, because it gets the renderer, the ImGui panels and every asset
reader for free. `source/game/assetviewer/gm_asset_viewer.{h,cpp}`.

| Panel | What it shows |
|---|---|
| **Banks** (left, top) | the label file in use, the counts, a filter, and every bank under `Euro/Bnk` (9,454 of them `[MEASURED]`) with its size and a `(labelled)` mark; the language copies (`__fr`, `__ge`...) are hidden until the checkbox is ticked |
| **assets / entities / search** (left, bottom) | three tabs — the loaded bank's **entries** (filename, size, group, a `*` when labelled, with the **type filter**: all / models / textures / movies / sounds / anims / cine / collision), the bank's **entities** (`entities.xmb`: class, name, position, and every field of the selected one), and the **cross-bank search** |
| **Preview + label** (right, top) | the identity (filename, bank, **authoring path**, the exact **key**, extension, size, group, subtype, index), the type's metadata, the label / note editor with Save (Enter) and Clear — directly under the identity, above the preview — and the preview itself beside it |
| **3D view** (right, bottom) | the bank's models on a **turntable**, the model counts, the picking (a click selects the asset that carries the mesh), a **free camera** (WASD / QE), **view from an entity camera**, the **collision wireframe** overlay and the **entity marker** |

The texture preview is an `ImGui::Image` of a `GPUTexture` — the project's ImGui backend
uses the texture pointer as `ImTextureID` `[CODE: renderer_2d.cpp drawImGui]`, so
a decoded 2db goes straight to the screen. The texture is created with
`TextureCache::createFrom2DB` and **destroyed by the viewer**: that call does not
register the texture with an owner, so `releaseOwner` would not free it.

A **cube map** (`Depth == 6`, flags `0x40`) previews as its **face 0** (the faces are
face major, so face 0's mip 0 is the start of the texel data): the panel shows that
flat image and says so, instead of refusing to show a cube.

### The cross-bank index, and the search

Without the index the tool can only answer "what is in *this* bank" — you have to know
the bank first. `-buildindex` (or the button in the Search tab) walks every bank once
and writes `<userdata>/asset_index.txt`, one line per asset entry and per `entities.xmb`
entity:

```
a<TAB><bank><TAB><asset filename><TAB><authoring path>
e<TAB><bank><TAB><entity class><TAB><entity name><TAB><x,y,z>
```

`[MEASURED]` 9,452 banks / 153,690 asset entries / 10,708 entities -> a 30 MB file in
**33 s** (one pass over the game data). The search reads the file back (a few seconds the
first time) and matches the query against the bank, the asset / entity name and the
authoring path, case insensitively; clicking a hit opens the bank and selects the asset
(or the entity, in the Entities tab). So `ferrari` (913 hits), `slot`, `cam_entree` or
`Hostess_area` find the bank that holds them.

### The entities panel

`entities.xmb` is the bank's own object list (`ExportType_Entity.ArrayOf_Entity__*`:
`Camera`, `Actor__Mesh`, `Volume__FpSelectable`, `Cinematic`, `Light`, `Fx`, `PNJ`...).
The engine reads them (`intro_cinematic.cpp`, `title_scene.cpp`), but no tool showed
their *values* — `otdu2_bnk_tool xmb` prints the schema only. The Entities tab lists them
by class (a class combo + a text filter), and the selected one shows **every field** of
the object, nested composites walked (`orient_pos_scale.pos.X`, `Fov`, `IDClass`,
`StartCamera`, ...): that is what the interiors work needed to log by hand. With "mark in
3D" on, the entity is marked in the 3D view at its position — a camera also draws its
**frustum** (Fov / Ratio from its own fields).

### The 3D view: cameras, free camera, overlays

A room bank (`interior/*.bnk`) previews as a box seen from outside: the **view from
camera...** combo sets the 3D view to one of the bank's `Entity__Camera`s (`cam_entree`
is in every shop interior) — position, rotation and Fov from the entity, the same
convention as the walk mode (`walk_mode.cpp`: the `orient_pos_scale` rotation *is* the
camera rotation). **free camera** starts from wherever the view is and walks it with
WASD / QE (Shift x4, the drag looks, the wheel changes the speed): an interior can be
walked through instead of looked at. The **collision** checkbox draws the bank's `.shk`
(`LoadHavokCollision`) as a wireframe (boxes + triangles, capped at 4,000 triangles) —
useful when a walk or a drive goes through a wall. Both overlays are projected with the
same view-projection the GPU drew with, so they cannot drift from the image.

### The 3D view (model preview, and click-to-label)

`drawOffscreen` loads every `.3dd` of the open bank with `EdenModel::load` (its `.3dg`
geometry and `.2dm` materials are the entries of the same name, the pattern of
`SpotExterior`, with `islands/<island>/level/commonworld.bnk` `library.2dm` as the
shared material fallback and the bank's textures registered under the bank path) and
draws them into an 800x600 render target; `drawUI` shows that target as an ImGui image.
The camera is the one `EdenSceneView::setupScene` builds — the island's game weather
(`islands/<island>/fx/game_weathers.xbm`, `14`) for the sun, the same
`EdenPerspectiveFovRH` projection — so a model previews under the light it has in the
game. `tick` turns the camera slowly (`kTurntableSpeed`, 20 deg/s) around the row the
models are laid out in (`layoutSceneModels`: each model's own bounds start at the
cursor, on y = 0, centred on z = 0, `kSceneGap` apart), which is what makes a bank's
models all reachable in one view.

**A click resolves to the asset, never to a pixel.** `pickScene` unprojects the click
through the **view-projection the GPU drew with** (near / far points, `sceneViewProjection`),
casts the ray against every model (`EdenModel::raycast`: exact triangles of the CPU
collision copy when `setKeepCollision` was set, the node box otherwise) and takes the
nearest hit; the model's `.3dd` row is then selected, so the label keys on the same
`<bank> + <asset filename>` identity as the lists. `applyPick` logs the asset, the mesh
node and the distance, and zooms the camera onto what was picked (`fit bank` goes back
to the row).

| Flag | Effect |
|---|---|
| `-gm assetviewer` | start in the viewer |
| `-bank <path>` | open a bank directly (e.g. `-bank euro/bnk/frontend/hires/cas_slotmachine.bnk`) |
| `-asset <filename>` | select an asset, so a run (and a screenshot) can open on it |
| `-pickat x,y` | pick a mesh at a pixel of the 3D view (a click, scriptable: the pixel is the one `AssetViewer: 3D view ...` logs) |
| `-label <text>` / `-note <text>` / `-savelabel` | write a label through the viewer's own save path (a test hook that drives the whole click-to-label flow) |
| `-playaudio` | play the selected `wav` asset |
| `-buildindex` | (re)build `<userdata>/asset_index.txt`, then exit |
| `-search <text>` | run a cross-bank search at startup (opens the Search tab) |
| `-entity <name>` / `-entitytab` | open the Entities tab on one entity (its fields, its 3D marker) |
| `-camera <name>` | start the 3D view in one of the bank's entity cameras |
| `-collision` | draw the collision wireframe at startup |
| `-noscene` | no 3D view and no model loads (the movie preview still works) |
| `-noturntable` | a still camera (what the pick tests use: the yaw must not move between the click and the screenshot) |
| `-island <name>` | the island the shared material library comes from (default `ibiza`) |
| `-pickcheck` | pick the **projected centre** of every model (the picking's own check: the ray must hit that model) |
| `-pickscan` | log a 64x32 grid of pick rays (where the models are, in the view) |
| `-logscenedraws` | log how many meshes the 3D view drew of how many items |
| `-labellint` | check every label key against the game data, then exit |
| `-labellabels` | log every label, then exit |

## 3. The label file

`docs/re/asset_labels.txt` — plain, sorted, tab separated, so two people (or an
agent and the owner) can merge it and anyone can grep it. The viewer writes it
from the repo root (the launchers `cd` there); an exe run from elsewhere falls
back to `<userdata>/asset_labels.txt`, and the panel says which is in use.

```
# <bank, relative to the game root, lowercase, '/'>  <asset filename>  <label>  <note>
euro/bnk/frontend/hires/cas_slotmachine.bnk	Cas_SlotMachine.vmf	slot machine movie (the reels)	the retail 5x3 video slot; its result came from the server
euro/bnk/islands/casino/exterior/ecas.bnk\tecas_sc_DB42CB20E8099A8F.3DD\tcasino exterior (the ECAS spot model set)\tthe ECAS spot largest .3dd (38 meshes, 27863 triangles): the casino building seen from outside...
```

The second line is the owner own example - *"I load a casino but file naming
maybe random and I can leave hints like casino exterior"* - written by clicking
the building in the 3D view and typing the label.

**The key is the identity, never a display name**: `<bank path> + <asset
filename>`, normalized to lowercase with `/`. That is what makes a label
*accurate*: it cannot drift onto another asset, and `-labellint` checks every key
against the game data — a bank that does not load, or a filename that is not in
it, is reported as a problem.

**Verified** `[MEASURED]` — the label file (7 labels, the casino movie banks and the
casino exterior):

```
AssetLabels: 7 label(s) from 'docs/re/asset_labels.txt'
AssetLabels lint: 7 label(s), 0 problem(s) - every key exists in the game data
```

and a viewer run:

```
AssetViewer: 9454 bank(s) under 'C:\TDU2-Gamedata\TestDrive2\Euro\Bnk'
AssetViewer: 'euro/bnk/frontend/hires/cas_slotmachine.bnk' - 13 entr(ies)
AssetViewer: 'page_001.2db' - 2048x1024, depth 1, 1 mip(s), format 55
```

Screenshots: `docs/images/asset_viewer.png` — the slot machine symbol atlas with its
authoring path above it and the label fields below.

### Verified — the previews and the picking (2026-10-29, Cline)

The commands are in `docs/STATE.md` (the session entry); the log lines they produce:

```
# the models of the casino exterior bank (euro/bnk/islands/casino/exterior/ecas.bnk)
AssetViewer: shared material library 'Euro/Bnk/islands/ibiza/level/commonworld.bnk' (439 material(s))
AssetViewer: 'euro/bnk/islands/casino/exterior/ecas.bnk' - 5 model(s), 43 mesh(es), 37999 vertices, 28777 triangles
AssetViewer: 3D view 488,394 - 837,656 (5 model(s), 43 mesh(es), 37999 vertices, 28777 triangles)
AssetViewer: scene 43 draw(s) of 43 item(s)

# -pickcheck: every model's projected centre picks that model (the picking's own check)
AssetViewer: check 'ecas_sc_DB42CB20E8099A8F.3DD' centre -> pixel 590.2,522.0: hit ecas_sc_DB42CB20E8099A8F.3DD
AssetViewer: check 'ecas_59E1C536C6C6FAD9.3DD' centre -> pixel 650.5,556.9: hit ecas_59E1C536C6C6FAD9.3DD

# -pickat 590,522: the click-to-label moment
AssetViewer: pick (-pickat) -> 'ecas_sc_DB42CB20E8099A8F.3DD' in 'euro/bnk/islands/casino/exterior/ecas.bnk' (mesh node 'ECAS', 386.45 m)
AssetViewer: picked 'ecas_sc_DB42CB20E8099A8F.3DD' (node 'ECAS', 386.4 m) - write the label and press Enter
AssetLabels: wrote 7 label(s) to 'docs/re/asset_labels.txt'
AssetViewer: saved 'casino exterior (the ECAS spot model set)' for ecas_sc_DB42CB20E8099A8F.3DD

# a movie (vmf): the Flash stage in the 3D view panel
Flash movie 'log_in2': 1280x960, 444 frames @ 30 fps, 798 tags, 8 bitmaps, 1 fonts
AssetViewer: movie 'LOG_IN2.vmf' (log_in2): 1280x960, 444 frames @ 30 fps, 798 tags, 8 bitmaps - drawn in the 3D view panel

# a sound (wav) and a cube map
AssetViewer: sound 'BEFORE_AD_05.wav': 291148 frame(s), 44100 Hz, 6.60 s (playing)
AssetViewer: 'cubemap_ecas.2db' - 256x256, depth 6, 7 mip(s), format 55 (cube map: face 0 shown flat)
```

| Screenshot | What it shows |
|---|---|
| `docs/images/asset_viewer_model.png` | the casino exterior bank's big model on the turntable (`-asset ecas_sc_DB42CB20E8099A8F.3DD`) |
| `docs/images/asset_viewer_pick.png` | the click-to-label moment: the picked asset selected in the list, its identity in the preview panel, the status line and the camera zoomed onto it |
| `docs/images/asset_viewer_movie.png` | `LOG_IN2.vmf` playing in the 3D view panel |
| `docs/images/asset_viewer_search.png` | the Search tab: `ferrari` -> 913 hit(s), the index counts above the list |
| `docs/images/asset_viewer_entities.png` | the Entities tab on `cam_entree` (the fields of the entity) and the 3D view from that camera |
| `docs/images/asset_viewer_interior.png` | the Ferrari showroom interior from `cam_entree` with the collision wireframe overlay |

### Verified - the feedback round (2026-10-29, Cline, second session)

The `win` session's feedback (`_feedback_asset_viewer.md`) asked for seven things; the log
lines of the runs that prove them:

```
# the index (one pass over the game data)
AssetIndex: 'C:\temp\otdu2-cline-userdata\asset_index.txt' written: 9452 bank(s), 153690 asset(s), 10708 entity(ies) in 33 s
AssetIndex: 164398 line(s) from '...asset_index.txt' (153690 asset(s), 10708 entity(ies))
AssetViewer: search 'ferrari' -> 913 hit(s) (display cut)

# an interior bank: entities, cameras, collision (item 2, 3, 7)
AssetViewer: 'euro/bnk/interior/icd_ferr.bnk' - 25 entity(ies) in 10 class array(s)
AssetViewer: 2 entity camera(s), first 'cam_entree' at (-8.19, 1.79, 1.77)
AssetViewer: view from 'cam_entree' (-8.19, 1.79, 1.77), fov 40.0 deg
AssetViewer: collision 'icd_ferr_sc_57E5C829CCD9F2C6.shk: 660 triangle(s), 42 box(es), 9 capsule(s), 61 body(ies)'
AssetViewer: entity 'cam_entree' (Camera) at (-8.19, 1.79, 1.77)

# a .cin (item 7)
AssetViewer: 'intro_lph_l3di_M_INTRO_TOTEM.cin' - cinematic: 52.6 s, 17 shot(s), 2 scene(s), 34 message(s), 4 fx event(s)

# the label file after the seeds the interiors work verified
AssetLabels lint: 12 label(s), 0 problem(s) - every key exists in the game data
```

**A caveat found while verifying** `[MEASURED]`: in `ecas.bnk` the four small `.3dd`
(one 2-triangle plane each, and a flat 908-triangle ground) are **hit by the rays and
recorded as draws** (`scene 43 draw(s) of 43 item(s)`) but produce almost no visible
pixels, while the 38-item building draws normally — their materials are the flat
planes of the set (signs / facades / reflection planes), so a preview shows them only
edge-on or not at all. A click on them still selects the right asset (they are
separate `.3dd` entries); what is missing is their *pixels*, not their identity.

## 4. How the agents use it

The file lives in `docs/re/`, which is where every session reads, and
`source/game/asset_labels.{h,cpp}` exposes it to the code:

| Call | Use |
|---|---|
| `FindAssetLabel( bank, asset )` | the label of an asset, or nullptr |
| `AssetLabelOrName( bank, asset )` | the label, or the asset name — what a log or a dump should print |
| `SetAssetLabel( bank, asset, label, note )` + `SaveAssetLabels()` | what the viewer calls on Save |
| `LintAssetLabels()` | the `-labellint` check |

So a log line can read the human name instead of the leaf (`AssetViewer` already
does: `'Cas_SlotMachine.vmf' = 'slot machine movie (the reels)'`), and a spec can
cite a label the owner wrote. **Rule for a session: before spending time decoding
an asset name, `grep` `docs/re/asset_labels.txt` — and when you do identify one
the hard way, write it down there** (a one-line edit, and it is then true for all
three workspaces).

## 5. What is not in it yet

| Piece | Note |
|---|---|
| **Click-to-label inside a streamed island / spot scene** | The picking works on the bank's models (the 3D view above): a click resolves to the `.3dd` that carries the mesh. Picking *inside the streamed world* (an island's sectors, areas and spot exteriors, the `WORLD_VIEWER` view) is not wired up: it needs an `EdenWorld` / `SpotExterior` accessor that hands out each drawn model with the bank path and the asset it was loaded from (the identity is `bank + "<name>-o.3dd"` for sectors, `bank + "<scene>.3dd"` for exteriors), then `EdenModel::raycast` per model. The rest (the ray, the resolution to an asset, the label) is already in place. |
| **Entity volumes as boxes** | The marker is a dot + the name (a camera also draws its frustum). A `Volume` carries `volume_mat` (local -> unit volume): its box needs the inverse of that matrix, which the fields panel shows but the 3D view does not draw yet. |
| **The index can go stale** | `asset_index.txt` is a snapshot: after a mod (or a game update) `-buildindex` must be run again. A hit whose asset is no longer in the bank says so ("index stale?"). A bank list of 9,454 also means the search *bank* column is the only place the answer is not clickable. |
| **Cube maps as a 3D view** | A cube map now previews as **face 0, flat**. Six faces in a cross, or a small lit cube, would be nicer for checking an environment map. |
| **Formats the engine cannot read** | `.cpr` needs the decryptor (`otdu2_bnk_tool decrypt`), `.sob` the shader tool: those are listed with their metadata only. |
| **Skinned models in the preview** | `draw()` (rigid) is what the 3D view calls: a skinned `.3dd` previews in its bind pose. `drawSkinned` with a static pose would be the next step (the avatars / actors of a bank). |
| **Lightmaps in the preview** | The 3D view lights a model with the weather's sun and ambient, but does not register the bank's lightmap (`lm_*`) materials (`EdenSceneView::registerLightmapMaterials` needs a view): a building previews a little flatter than it looks in the game. |
| **A pick that reports *which part* of a model** | The click reports the mesh's scene node name (`mesh node 'ECAS'`). Naming a *sub-part* of one `.3dd` (a slot machine inside a casino set) would need per-node labels — the key would have to grow a third column, which the file format deliberately avoids. |
| **`.cin` / `.sce` playback** | The panel reports the duration, the shots, the scenes and the actors; it does not play the cinematic (that is `CINVIEWER`'s job, and it needs the scene's models and its `.fxa` / `.fxe` actors). |
