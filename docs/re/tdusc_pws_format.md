# TDUSC `.PWS` world tiles - container decoded (session 62, Hong Kong island project)

Tags: `[DATA]` read from the owner's own TDUSC install (`E:\SteamLibrary\steamapps\common\TDUSC\WIN32\LEVELS\HONGKONG\`, 589 tiles
`TILE_Xn\TILE_Xn_Ym\HONGKONG.PWS`, 1.4 MB - 24.8 MB), `[INF]` inferred. No TDUSC data is in the repo.

Goal: Hong Kong as a third island mod (owner 2026-11-19). This is stage 1 of that plan: what a PWS is.

## 1. The earlier note was wrong: it is not one opaque compressed body [DATA]

The "dense body, entropy 7.8-7.95, compression unidentified" of `tdusc_pkg_format.md` s.3 is a **container**. Measured entropy of the
smallest tile is ~6, with 36 % zero bytes. It holds **GDeflate TileStreams** (the same DirectStorage format `gdeflate.py`
already decodes for the meshes) sitting between plain structured data.

Header (tile `X75_Y26`, `X57_Y58` agree): `u32 0x00010017`, `u16 0`, then **three descending `u32` section offsets at +6, +10, +14**
(X75_Y26: `0x14d5f8` = file size - 4, `0xcc5f0`, `0xbf0c0`), `u32 0x18` at +18, `u16 0xABCD` at +22, then the body from `0x20`.

## 2. Body [DATA]

* `0x20 .. 0xbf0c0` (first section): **1,364 embedded GDeflate streams, in every tile examined** (2 tiles: 3.0 MB land, 1.4 MB sea).
  Stream header `04 fb <u16 tiles>` + `u32 {sizeIdx 1, lastTileSize<<2}` + `u32 tileOffsets[]`, here always **1 tile**.
  * **84 streams -> 21,053 bytes** (variable compressed size 3.5-7 KB), the first ones, packed back to back;
  * **1,280 streams -> 12,861 bytes**, each in a **256-byte slot** (stride `0x100`; a sea slot compresses to 244 bytes, a land slot up to 8.2 KB so the
    slot grows for land). Sea slots are identical, which is why the notes saw a period of 256.
* Both blob kinds start with the same header: `3x3` identity matrix, `(0.5, 0.5, 0.5)` twice, then **float data**. In a land tile this data is smooth
  and about -28 .. -38 (sea floor next to the coast) - **heights/vertices**, in a sea tile it is the constant `-10.0`.
  The tail is a repeating `05 ff 05 05 ff 05 ...` byte pattern. [INF: terrain height patches / per-cell vertex data; the 1,280 = a cell grid, the 84 = coarser
  patches. Not confirmed - the field layout is not decoded yet.]
* `0xbf0c0 .. 0xcc5f0` (13.8 KB): plain `u32` index arrays (`0x555`, runs of `1, n` pairs; `0x155, 0x2aa, 0x3ff` = 1/3, 2/3 of 1024 - a 32x32 lookup).
* `0xcc5f0 .. end`: plain data, in the sea tile almost entirely repeated `1f 84 1f 84 00 00 00 00` (RGB565-looking `0x841f`). [INF]

## 2b. The stream layout and the quadtree - DECODED (session 62) [DATA]

**A tile is a quadtree of 1,364 GDeflate streams in depth-first pre-order**: 4 cells of 256 m (order (0,0) (1,0) (0,1) (1,1), x fast), each followed by its
4 children, down to 16 m: 4 + 16 + 64 + 256 + 1,024 = 1,364. The k-th stream in file order is the k-th node, so **a stream's cell comes from its position, not from its box**.
Verified on `X63_Y42`: all 1,280 streams with a box centre lie within 0.6 S of their node's cell centre (median error 0.5 m).

Each stream = **256-byte header, 1 pad byte, then float32 data at offset 257** (not 4-aligned; sizes are `4n+1`):
* header floats: `[0..8]` 3x3 rotation, `[9..11]` centre xyz, `[12..14]` half extents (a tight oriented box, NOT the cell: do not use extents for the cell size), the same again at
  `[15..29]` (box 2), `[30..63]` 34 more floats. Centre (0,0) = the node has no data.
* **`[257..]`: a 33x33 float32 heightfield, 32 intervals per cell side (spacing S/32: 0.5 m at 16 m, 8 m at 256 m), rows stored TOP-DOWN in y** - flip the rows to get y up. Then
  more data (not decoded; zeros after the grid in the streams checked).
* **Verified:** every parent grid equals its children's grids decimated 2x - mean abs difference **0.0** at all four levels on `X63_Y42` (941/256/64/12 pairs). So the parents
  are redundant where all four children exist, and are the only data where a child is missing.
* World frame: x, y horizontal, z UP, metres.
* Multi-tile streams (tiles >= 2): decoded size `65536 * (tiles - 1) + last`; the stream ends at `8 + 4*tiles + offs[-1] + offs[0]`.
  `pws_scan.candidates` now skips a header-looking pattern that begins inside the previous stream (`X57_Y46` had one, giving 1,365).

## 2c. Tile placement and the whole island (session 62) [DATA]

* **Placement - fitted on 5 tiles, also consistent with `X27_Y38`:** `TILE_X<i>_Y<j>` has its SW corner at world **x = 256*i - 11008, y = 256*j - 11008** (m) and is 512 m square.
  X and Y pitch are both 256 m; a column only uses every 4th Y (odd columns offset by 2), so the tiles form a staggered brick pattern. 589 tiles -> x -9472..8960, y -8448..5376.
* `tools/tdusc/pws_island.py` decodes **all 589 tiles** (18 jobs, **43 min**) into 1024x1024 float32 maps at 0.5 m (2.4 GB), composed coarse-to-fine from the quadtree.
  Stitched at 4 m: **103 km2 covered, z -99..552 m, the island is unmistakably Hong Kong Island**: harbour coast on the north, the ridge system, the southern bays, Kowloon side, and the road
  network visible as lines in the hillshade. `X75_Y26` / `X39_Y58` have no data (open sea).
* **Retracted earlier claim:** "only 11 % covered, the 20-70 KB streams are another kind" was wrong. They are the coarser quadtree levels of the SAME heightfield; the blocky composite
  came from a flipped row order and from guessing the cell size out of the box extents. Both are fixed.

## 3. Tool

`tools/tdusc/pws_scan.py <file.PWS> [--out DIR] [--max-bytes N]` lists and decodes every embedded stream (pure Python, ~minutes for a 3 MB tile; a C++ loader would port
`gdeflate.py`). Verified: X75_Y26 and X57_Y58 -> `ok 1364 bad 0`.

## 4. Not known yet / next

* What follows the 33x33 grid in each stream (the data after float 1089: more floats / bytes; roads and props are visible in the hillshade, so surface detail is somewhere, maybe
  baked into the heights only). The `0xbf0c0..` index sections and the plain `0xcc5f0..end` section of the file are not decoded.
* Roads / nav graph / buildings / textures: not located. Roads exist in the heights as ridges; a real road graph for GPS would have to be traced from the heightfield or found elsewhere.
* Sea level and the water mask (z goes to -99 m: seabed is in the data).
* Island mod: writing Eden's terrain (`.3dg` 257x257 heightfield primitives per island/area/sector, 384 m / 24 m / 3 m grids, plus materials / colour map) from `C:/temp/hk_island_4m.npy`-style
  data. Template: `tools/mod/make_eden_island.py`, `docs/re/eden_island_spec.md`. The island is 18 x 14 km = about 3 x 3 Eden areas (6 km each).

## 2d. Roads are not in the PWS: they are level meshes in the PKG (session 62, `level_mesh.py`) [DATA]

* The heightfield tail of each PWS stream (66 KB in a dense 16 m cell) is mixed data: half-float blocks, a 17-point footprint polygon in the 34 header floats, an `0xff` pad.
  **Not decoded**; no road or building data was identified in it. [INF] it is per-cell detail / material layers.
* **The PKG TOC lists, per tile, `LEVELS/HONGKONG/TILE_Xi/TILE_Xi_Yj/{ROADBUILDSTEP,INTERSECTIONBUILDSTEP,SPLINEMESHSKINNERBUILDSTEP,SPLINESPAWNERBUILDSTEP}/*.P3D` + `.PSI` + `.PMA`,
  plus `IMPOSTERS/HONGKONG/...IMPOSTER_*_GENERIC|TERRAIN.P3D`** (384 road tiles, 589 `PSVO` voxel trees, 589 `.PLM`).
* `ROADBUILDS_*.P3D` = the car `SCENE___` container with **named** MESH entries (`u32 len, "ROADMESHLET", 10 floats bbox, nparts`), format `COLONORMPOS_UV0_UV1_UVY_`:
  `vpos` is **4 x unorm16 across the mesh bbox**, indices are **u8 in meshlets of <= 64 vertices** (each meshlet restarts at 0). Placement = tile origin + the `Actor_*` node translation.
  Verified on X63_Y42: 1,494 vertices, 2,174 triangles, z 85.8..94.1 m against 87..93 m in the decoded heightfield.
* **Open**: an island-wide raster of the decoded road triangles (273 tiles) is fragmentary and blocky: a P3D has several `Actor_*` nodes with their own translations
  (only the first is applied), and meshes whose meshlet split does not sum to `vcount` are skipped. `SPLINEMESH_*` P3Ds have many actors and break `parse_nodes`.
  Next: apply the translation per mesh (node -> mesh index), then rasterise into the terrain colour map / extract a centre-line graph. Tool: `tools/tdusc/level_mesh.py`.

### 2e. Road mesh decode, second pass (session 62) [DATA, UNRESOLVED]

* Several `Actor_*` nodes per P3D carry distinct translations (X56_Y36: 31 actors, 4 distinct) and `parse_nodes` cannot read them. `level_mesh.decode` now scans the actor
  records itself and picks, per mesh, the translation whose median |z - terrain| is smallest (`ground=` heightfield). On X56_Y36 most meshes fit at 0.1-0.4 m.
* `vpos` is **not** 4 x unorm16 (that gave axis-aligned slabs). A brute-force search over contiguous bit fields of the u64 (minimising median triangle edge) found
  **z = bits 0-15, y = bits 16-29, x = bits 30-43** (median edge 0.13-0.5 m on all six X63_Y42 meshes). **Not confirmed**: neighbouring vertices still differ in z by ~2 m
  and 10 % of edges span the whole mesh box, so either the fields are wider/other or the meshlet-local indices need a remap. Raster of X56_Y36 still shows slabs.
* `SPLINEMESH_*` (format `NORMPOS_UV0_`, no vcol) decodes to huge slabs with the same layout: its `vpos` packing is probably different. Not examined.
* Next: read the 20 high bits (44-63) and the `vnor`/`vuvy` streams for a per-vertex or per-meshlet scale; compare against a P3D with a known shape (a car part, the
  imposters) using the same packed format; the PS3/360 builds do not contain TDUSC so there is no twin to read.

### 2f. Road meshes, third pass (session 62) [DATA]

* **Actor -> mesh mapping is solved**: the `.PSI` beside each road P3D lists one `Actor_<id>_<n>` name per mesh PART in the P3D's part order (X56_Y36: 97 names, 97 parts) with its `.PMA`.
  Each actor appears **3 times = 3 LODs** (near-identical bboxes). The translation comes from the length-prefixed `Actor_*` node record (4x4 row-major after the name, translation = row 3; rotation is identity in
  every file looked at). Only 4 distinct translations in X56_Y36. `level_mesh.decode(d, origin, psi)` uses this and keeps the largest part of each actor (`lod0`).
* `vpos` packing z = bits 0-15, y = 16-29, x = 30-43 is **consistent** (median |z - terrain| 0.1-0.4 m per mesh, which would not hold with wrong xy on sloped ground; a per-axis smoothness
  search also isolates x at bits ~30-44 and y/z below 31). The "z noise" seen before was vertex order, not geometry; the "long edges" are real triangles across a piece.
* **Interpretation**: a piece is an axis-aligned rectangle (28 x 20 m, 37 x 13 m, ...): vertices on the perimeter plus a few diagonals. Rasterised, X56_Y36 shows a **staircase of such rectangles along the
  shoreline** and a block of them inland, i.e. the road runs diagonally and each piece is the bounding pad. Material = `ENGINE/EFFECTS/SURFACES/PBR/PROCEDURALROAD.PFX` with `Use AlphaTest`,
  world-space (`WS_NRM`, `WS_HM`) and two UV layers (`UV1`, `UV2`) of `GRAPH/ROADS/{TARMAC,CONCRETE,GRAVEL}/TEXTURES/HKI_*`, a tire-mark map, `vuvy`. [INF] the road outline is cut by the alpha / the
  procedural shader, **not** by the mesh, so the triangle mesh alone cannot give a road centre-line.
* Consequence: a usable road layer needs either (a) the alpha / UV maps of the `.PTX` set decoded and rasterised per piece, or (b) a centre-line traced from the terrain heightfield and snapped to these pads
  (the pads say *where* a road is, to ~30 m, and their z gives its height).

### 2g. Road mask traced from the terrain (session 62) [INF, LOW CONFIDENCE]

`tools/tdusc/road_mask.py` rasterises each tile's road PADS (LOD0, actor translations from the PSI) with interpolated z and keeps the pixels where |pad z - decoded terrain z| < 0.4 m
(388 tiles, 6 min, 8 jobs -> `C:/temp/hk_roadmask`, 0.5 m uint8). `make_hongkong_island.py --road-mask <dir>` paints it as asphalt into the colour maps (`RoadMask`, `shade_colour(road=)`).
* Good: on X56_Y36 the mask gives parallel-edged ribbons ~12 m wide (a long diagonal road at the bottom, blocks of streets inland).
* Bad: the total is **7.0 km2 = 7 % of the island** (a real road network is ~2-3 %: flat ground inside a pad passes too); the seafront strip never matches (pad z over sea floor); on X63_Y42 (hills) the mask is
  empty although roads exist (the baked heightfield carries no road there). A drive at a mask spot showed sand, not asphalt (spot not on a road).
* **Not enabled by default**; the shipped mod is built without `--road-mask`. Better inputs: the alpha textures of the road pads (route (a) above) or the PSVO voxel / `.PLM` files per tile (not examined).
