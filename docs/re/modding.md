# Modding OpenTDU2 — the mods overlay, and how a new building gets into the world

Tags: `[CODE]` readable exe / port code, `[DATA]` game files, `[INF]` inferred.
The overlay is port code (`source/filesystem/gs_file.*`); everything below is
verified on the `cline` tree (session 14).

## 1. The overlay (already in the port)

`<user data>/mods` **mirrors the game layout** and a file there **shadows the game
file with the same path for every read** (`fileExists` / `loadFile` / `listFolder`).
`-mods <dir>` or `OTDU2_MODS_DIR` moves it, `-nomods` disables it. It defaults to
`<user data>/mods` so two copies with different `-userdata` do not share mods.

- The overlay is **read-only**: the port never writes into it, and the **game data
  folder is never written to at all** (it is the user's installation).
- Entries are **not hot-reloaded**; the log prints every hit:
  `Mod overlay: 'Euro/Bnk/...' -> '<file>'`, and the folder at boot.
- It covers textures, shaders (`.sob`), audio, databases (`.cpr`), UI movies and
  **whole banks** — anything read through the VFS.
- The overlay is a **merge, not a replacement**: a folder that exists in both places
  lists the overlay's files first (a same-named file wins) and then the game's. It
  used to list *only* the overlay copy when the folder itself was modded, so one
  file in `<mods>/Euro/Bnk/<folder>` hid every other file of that game folder — the
  asset index walk then saw 1 bank instead of 9,452 (`gs_file.cpp listFolder`,
  fixed session 15).

## 2. Adding a building: one spot record + one bank (verified, no engine change)

Two files, both in the overlay:

| file | what |
|---|---|
| `mods/Euro/Bnk/islands/<island>/level/spots.bin` | the game's own list + one extra 0x110-byte record |
| `mods/Euro/Bnk/islands/<island>/exterior/<name>.bnk` | the building itself (`<name>` = the record's `bank` field, lowercased) |

The record (`docs/re/spot_exteriors_spec.md` §1): `u64 id` at +0, `char bank[8]` at
+8, `vec3 position` at +0x10, `f32` at +0x1C, `f32 heading` at +0x20, the category
byte at +0x100. The list header holds the payload size at +0x14 (bytes after the
first 16), so appending a record means `+0x110` there too. The id is
`GetIdentifier64bit(spot name)` (`source/shared.h`: 8 byte slots, `uppercase(c)`
added into slot `i & 7`) — lossy, so any unique name works.

**Tool: `tools/mod/add_spot.py`** does all of it (it reads the user's own
`spots.bin`, never ships one):

```
python tools/mod/add_spot.py --name EGAS_A --x 12665 --z 16698 --heading 0 --category 4 \
       --bank C:\TDU2-Gamedata\TestDrive2\Euro\Bnk\islands\ibiza\exterior\etun_a.bnk
```

Verified end to end: the log shows
`Mod overlay: 'Euro/Bnk/islands/ibiza/level/spots.bin' -> '<mods>/...'`,
`MapScreen: 75 spots with an icon (of 80 in spots.bin)`,
`spot 'EGAS_A' icon 'tuning' at (12665, 16698)`,
`Mod overlay: '.../exterior/egas_a.bnk' -> '<mods>/...'` and
`SpotExteriors: EGAS_A at (12665, 16698): 2 models, 1 placements`, with
`SpotExterior: ...egas_a.bnk: 2 set models, 0 props, local bounds (-7.3, -0.4, -17.7)
- (36.0, 10.8, 23.9)`. Evidence: `docs/images/mod-station-placed-s14.png` — two
copies of the tuning-shop bank 60 m apart where retail has one (the left one is the
real `ETUN_A`, the right one is our `EGAS_A`).

## 3. Our own building: two routes

**Route A — a port-native prop: implemented** (`source/game/spots/mod_props.*`,
session 15). OBJ + MTL + PNG, placed by `<mods>/props.txt`, drawn by the Eden
renderer with a stock shader family:

```
# <mods>/props.txt
# island name x y z heading [scale] [shader]
ibiza FUEL1 11660 5.3 15866 0 fuel

<mods>/props/FUEL1.obj      (+ FUEL1.mtl and the PNGs it names, beside it)
```

- `fuel` marks the prop as a **gas station**: stopped within 35 m of it the HUD's
  prompt offers `Fill` and validate opens the pump screen
  (`source/game/freeride/fuel_menu.cpp`, the retail strings of
  `docs/re/fuel_gas_station_spec.md`). That is how extra station locations are added
  once a model exists (retail's own pump model is the `gph_l2d` garage one).
- A `fuel` prop also gets a **map icon**: `<mods>/icons/fuel.png` (any size, aspect
  kept - an icon made with any image tool drops straight in), or a built-in pump
  glyph when the file is absent (`MapScreen::drawFuelIcons`). Retail has no station
  icon at all: the map movie's `pack/point/spot` sprite carries 102 labels and not
  one is a station, and no bank holds station icon art.
- One draw per `usemtl` of the OBJ; the MTL's `map_Kd` becomes texture slot 0
  (`COLOR`) of a material built with `EdenRenderer::setupMaterial`, so the shader
  family can be any stock one (`shader` column; default `TD_Gouraud` = Gouraud N·L
  plus an optional colour texture).
- The mesh is one interleaved stream (position, normal, texcoord), mirrored on load
  (X + reversed winding) exactly like the game's own 3D geometry, so a prop authored
  in Blender lands the same way round as a retail building at the same heading.
- Textures are read with `render/image_file.*` (PNG/JPG/DDS) and created with
  `TextureCache::createFromRGBA` — no `.2db`, no bank, no game data touched.
- `-nomodprops` disables it; every prop logs its materials, triangles, bounds and
  shader family; `-logmaterials` prints each draw's permutation and its textures.
- `y` is the **ground height** and is not derived: a prop at the wrong `y` is buried
  (the first test looked "invisible" for exactly that reason — a top-down A/B then
  measured **28,650 changed pixels** between `-nomodprops` and the prop, mean
  |dR| 4.65, i.e. it was drawn all along).

Route B (below) stays the "native" alternative for props that should be lit by the
game's own lightmaps and cast shadows exactly like a retail building.

**Route B — the game's own pipeline (most native).** Blender → the Eden render
formats → packed bank:

| format | content | spec |
|---|---|---|
| `.3dd` | the scene: `MBNA/MBNK`, `OBJA/OBJ`, `INSA/INS` (4x4 matrix + material bank + object), `HIEA/HIE` (the node tree) | `formats.md` §3DD |
| `.3dg` | the geometry: `GEOA/GEOM/PRIM`, `VBA/DXVB`, `IBA/DXIB` | `formats.md` §3DG |
| `.2dm` | the materials: render states, 8 texture slots, parameters | `formats.md` §2DM |
| `.2db` | the textures: `BMAP` + DXT pixels | `formats.md` §2DB |

All four are **already read** by the port (and by `otdu2_bnk_tool xmb`, the asset
viewer), so the loop can be validated by round-tripping a real bank: extract
`ecd_ferr.bnk`, re-export our own building into that structure, and check it draws.

Two pieces are missing and are the real work:

1. **A BNK packer** — `tools/bnk_tool` only lists / extracts / decrypts / dumps.
   The format is in `formats.md` §BNK (`KNAB` at +8, the 0x40 header, 0x14-byte
   section descriptors, the prefix-tree directory), so a packer is a bounded tool —
   and it unlocks texture, material and UI mods in general, not just buildings.
2. **An exporter** (a Blender add-on, or a converter from glTF/OBJ).

A petrol station needs only a small subset: one static mesh (canopy + shop + two
pumps + a sign), one to three materials with a diffuse texture, a handful of
instance nodes. No skinning, no animation, no lightmap, no LOD.

## 4. What we must not do

- **Never write into the game folder** (it is the user's installation, and a BNK
  writer pointed at it is the one way to lose the data — `WORKSPACES.md`).
- **Never ship game data in this repository.** The overlay folder is user-local; a
  tool may *read* the user's own files (as `tools/mod/add_spot.py` does) but the
  repo must not contain a `spots.bin`, a game bank, a texture or a loc dump.
- **Trademarks**: a real Shell station's livery, logo and name are not ours — the
  new building uses **our own brand and our own art**. The game's own strings
  ("Shell basic", ...) stay in the game's localization and are only *read*.
- Our own assets are licensed with the project (GPL-3.0-or-later like the code, or
  a CC-BY-SA note in `NOTICE`) — decide when the first asset lands.

## 5. The petrol station, as the worked example

`docs/re/fuel_gas_station_spec.md` has the feature side (the loc text, the per-car
tank, the handler, what is missing). The asset side:

1. **Model** in Blender: canopy, shop box, two pumps, a price sign; a low-poly
   version is enough (retail buildings are 10-100 KB banks).
2. **Texture** it with our own art (a different brand, no Shell livery).
3. **Get it in**: Route A now, Route B once the packer + exporter exist.
4. **Place it**: `tools/mod/add_spot.py` (one or two per island, on a road; the
   record's `y` defaults to the nearest spot's terrain height).
5. **Fuel state**: `_gas_tank_size` / `_gas_cons_min/avg/max` are already read
   (`CarPhysicsData`), the interaction is the game's own string sequence
   (grade → `Fill` → `Bill`), and a `Breakdown` when empty and broke.
6. **Collision** (optional): boxes in `VehicleWorldCollision` — a Havok `.shk` is
   not needed for our own building.

## 5b. The real options menu's extra rows: `<user data>/menu_rows.ini`

The pause menu's Image settings / Video pages carry the port's own settings
rows (session 75/76). Their layout is data: a `[page]` section (`video` or
`gamma`) in `<user data>/menu_rows.ini` replaces that page's default rows,
one line per row in display order (the first line lands in the panel's first
game slot):

    [gamma]
    shze = haze, Haze
    rnsc = rndscale, Render scale
    ...

- `command` — the game's setting id; its value text and Left / Right step
  behaviour are code-owned (`extraRowText` / `stepExtraRow` in
  `pause_menu.cpp`). Known ids: `wnmd rnsc upsc shrp shdw shds fpsl shze
  snfl ocrf drds`. An unknown id draws a row with an empty value that does
  not step.
- `variable` — the root variable the row displays.
- `label` — the row's title (plain text).

Sections not in the file keep their defaults; the file is optional. The
panel shows 7 game rows (slot 0 is the page's own first row — the Video
page's Resolution / the Gamma page's Gamma slider); more rows scroll a
window under the focus (the treadmill, session 76).

## 6. Next steps, in order

1. **Route A loader** + a placement list — a visible station quickly, and a general
   mod-asset path.
2. **The BNK packer** (`tools/bnk_tool pack`) — also unlocks bank-level texture and
   material mods.
3. **The exporter** for the Eden formats (Blender add-on), validated by
   round-tripping a retail bank.
4. Then: collision, LOD banks, a second station per island, and the fuel HUD.

