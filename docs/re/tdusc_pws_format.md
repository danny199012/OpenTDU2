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

### 2h. "Road alpha" ruled out (session 62) [DATA]

* The 4th `vcol` byte (what an alpha test would read) is **255 for > 90 % of the vertices** (X56_Y36 LOD0: percentiles 0/10/50/90/100 = 97/255/255/255/255), so interpolated vertex alpha does not cut a road out of a pad
  (rasterised: uniform rectangles).
* `PROCEDURALROAD` materials carry **no mask texture**: only IOR / NRM / HM / DIFF maps in world space and two UV layers, a tire-mark map, `BlueNoiseMap`, `WorldSandMask`, and scalars
  (`BorderEasePower`, `WorldSpaceTiling`, `LocalSpaceTilingY`, `HeightMap*Scale`, `bMap1/2InWorldSpace`). `Use AlphaTest` is a flag, not a texture.
* All 31 `Actor_*` matrices of X56_Y36 have **identity rotation** (translation only), so the axis-aligned pads are not rotated road ribbons.
* The pads have 3-5 m of height range and perimeter-dense vertices with corner duplicates (split normals / UVs): [INF] they are box-like parallax / trenching volumes the shader ray-marches
  (`EnableParallax`, `TrenchingColor`), so **the road outline is not in the mesh, the vertex colours or a mask texture**. The shipped road location data must be elsewhere (spline spawner
  P3D / `.PSVO` voxel trees / `.PLM` per tile, none examined).
* Next for roads: read `SPLINESPAWNERBUILDSTEP/SPLINESPAW_*.P3D` (229 tiles) and `OCTREEVOXEL_0_0.PSVO` (589) - the spline spawner is the likeliest source of centre-lines.

### 2i. Spline spawner, PSVO, imposters: no road data (session 62) [DATA]

* `SPLINESPAWNERBUILDSTEP/SPLINESPAW_*.P3D` (229 tiles, 1,896 physics meshes in all): the `MESH` entries are **plain-float collision hulls** (`MESH`, `u32 0x10`, `u32 1`, `u32 6`, `u32 nVerts`, `u32 nTris`, `float3[nVerts]`, `u8 idx[nTris*3]`),
  physical material `PHYSICALMATERIAL/CCAMCBLOCKMETAL.PMC` (metal blocks: barriers / posts), vertices in **HK world metres** (actor translation cancels the tile origin). Mapped over the island they are
  **scattered small hulls (30-60 m)**, not continuous road lines: a barrier / prop spawner, not the road network. One mesh per file fits the terrain z (352 vs 351 m), another does not (z 17-21 m vs 294 m).
* `OCTREEVOXEL_0_0.PSVO` (1.5 MB per tile, header floats = tile centre +/- 256 m, z centre): a sparse voxel octree (`PSVO-WIN`, `VX`), [INF] lighting / GI occupancy, not examined further.
* `IMPOSTER_TILE_*_TERRAIN.PTX` is a **128 x 4** strip (a palette / LUT, not a top-down colour image); `IMPOSTERLIGHT_*.PTX` is a 31 x 31 x 9 volume (light probe grid). Neither carries terrain colour or roads.
  (`ptx_to_dds.py` fails on both: their header has width / height / depth in a different place than the car textures.)
* `GRAPH/WORLDMAP/HONGKONG` is **one mesh** (`HONGKONG_WORLDMAP.P3D`, 77 KB) plus 14 district `GP_*.PMA` materials, no image.
* Remaining places for a road graph / centre-lines: the PWS sections after the quadtree (`0xbf0c0..`, `0xcc5f0..`, 598 KB tail in X63_Y42, not decoded), the `.PLM` / `.PACLOG` per tile, and the
  `PROCEDURALROAD` volumes themselves. **Buildings are a better next target**: `PARCELBUILDSTEP_*.P3D` (2,184 files), `IMPOSTER_*_BUILDING.P3D` (224) and `GRAPH/BUILDINGS/HONGKONG` (1,266) are ordinary meshes.

### 2j. Buildings (session 62, first pass) [DATA]

* **Where**: `GRAPH/LEVELS/HOUDINI/HONGKONG/TILE_Xi_Yj/PARCELBUILDSTEP_<hash>.P3D` (+ `.PSI`) - **5,250 files** (the earlier count of 2,184 missed these), 0.3-1.3 MB each, only some tiles
  (the road tiles are `LEVELS/HONGKONG/TILE_Xi/TILE_Xi_Yj/`; parcels sit under `GRAPH/LEVELS/HOUDINI`). Also `GRAPH/BUILDINGS/HONGKONG` (1,266), `IMPOSTERS/.../IMPOSTER_*_BUILDING|GENERIC|ROAD|TERRAIN.P3D`.
* **A parcel P3D** is the same `SCENE___` family but its header word at 0x34 is NOT the blob offset (`level_mesh.decode` fails on it): the first GDeflate stream (`04 fb`) after the plain head is the vertex +
  index blob, two more small streams follow. Example `TILE_X19_Y38/PARCELBUILDSTEP_2889716754.P3D`: **28 `MESHLETMESH` entries, 115 parts** in the same `COLONORMPOS_UV0_UV1_` format
  (named meshes: `0b 00 00 00 "MESHLETMESH"`, bbox min/max +/- 11-14 m, parts = materials/LODs), **5 `Building_<n>` nodes** (`Building_3/4/6/12/13`) with 4x4 matrices whose translations are **parcel-local**
  (-32..+8 m), identity rotation. The PSI maps `Building_n` to `GRAPH/HOUDINI_MODULES/MAT/PMA/{FILLERS,CONCRETE,GLASS,INTERIOR_MAPPING}/...` materials (facade modules).
* **The parcel's own world placement is not in the P3D** (bbox is local, no translation). It must be in the level scene: `HONGKONG.PLM` (5.7 MB) / `TILE_*.PLM`.
* **`.PLM`** = `PLMB`, a scene-entity dump: a string table of class / property names (`PlayAll::GameServices::Mesh`, `ResourceID`, `Visible`, `MaxVisibleDistance`, ...), then entity records.
  The tile PLM lists only the road build-step actors (`RoadBuildS_0_1_73730Actor`, `IntersectionBuildStep`, `SplineMeshSkinnerBuildStep`, `SplineSpawnerBuildStep` + `DATA:/LEVELS/.../*.PSI` ResourceIDs). The island PLM holds the
  Houdini inputs: **1,205 `Parcel_REGIONS/ R_<DISTRICT>_<n>` polygons, 724 `..._ManualBuildingPoly` (building footprints), `_exclusion`, `_exclusion_sidewalk`** - each as
  `u32 nameLen, name, u32 nPts, u32 pointId[nPts], c0 85 47 9b ...`. The point table the ids refer to (`0x41f1`, `0x495a`, ...) was **not located** (a plain-float scan finds no point array; ids recur as object ids).
* `pkg_scan.lz4_frame` now handles **linked-block LZ4 frames** (FLG bit 5 clear; blocks match into the previous output): the `.PLM` / `.PSVO` reads that raised `IndexError` work.
* **Next**: (1) decode the PLM entity records (object id -> class, name, transform), which gives parcel placement and the footprint points; (2) then place `PARCELBUILDSTEP` buildings
  (mesh decode as in `level_mesh.py` with the first-stream blob), or extrude `ManualBuildingPoly` footprints to boxes as a fallback; (3) convert the facade PMA/PTX modules to Eden materials.

### 2k. PLM scene dump, second pass (session 62) [DATA, PARTLY UNRESOLVED]

* **`HONGKONG.PLM` holds a global point table**: **56,850 packed `float3` (x, y, z) world metres** in one contiguous run, `0x453555 .. 0x4f9e2d` (4 mod 12 = unaligned, 1 mod 4: a 1-byte field precedes it), found by
  walking plausible triples at stride 12 (`|x| < 10000, |y| < 9000, -200 < z < 700`). 53,102 of them have **z == 0** (2-D polygon points); the rest are runs with a constant z (e.g. 427.96, 22.76: spline / curve points).
  Total extent x -7844..7368, y -6984..4433 (the island).
* **Polygon records**: `u32 nameLen, name, u32 n, u32 pointId[n], u32 typeHash, u32 0, u32 hash, u32 1` (the 4-byte class hash after the ids is `c0 85 47 9b` for the first
  parcel records, other values later, so do not match on it). Found **4,176** records: 724 `*_ManualBuildingPoly`, `_exclusion` (zones where the Houdini generator must not build),
  `_exclusion_sidewalk`, `Parcel_REGIONS/ R_<DISTRICT>_<n>`, `LMK_*` landmarks, `*_BUILDING_*`, `*_TOWER_*`. Point ids run 16,849 .. 49,086.
* **UNRESOLVED: point id -> coordinate.** Indexing the table with `id` directly (and +-3) gives self-intersecting polygons and the Aberdeen district polygons stretch across the whole island
  (two clusters 10 km apart); 4-point polygons form rectangles no more often than chance (12-16 %); a per-polygon search for an index shift that makes the polygon compact (+-30,000 on 300 records)
  finds **no common offset** (the most frequent shift occurs 3 times). So the table is not indexed by the polygon ids; [INF] either the ids are Houdini point numbers into a different (per-primitive) array,
  or the polygon list stores several sub-polygons. Not examined: the section header at 0x30 (`aa e3 00 00 | 12 01 00 00 | 10 4d 00 00 | ...`) that probably holds section sizes / offsets,
  and the records after each id list (`hash`, `1`, then the next name), which may carry a point-range base.
* The tile `.PLM` (`TILE_X56_Y36.PLM`, 0.9 MB) has many 52-byte records `float(~40), u32 0x8000xxxx, 3 x float x2 ...` (spline control points with tangents, ids `0x8000031c ...`), not parcel placements.
* Net: **building footprints and parcel placement are still not recoverable**; the parcel meshes (2j) are decodable but cannot be placed. Fallback that needs neither: place the PARCELBUILDSTEP buildings
  by matching each parcel's local bbox against the terrain / the `exclusion` footprints once the id mapping is solved, or use the distant `IMPOSTER_*_BUILDING` meshes (224 tiles; in the Y-up
  imposter frame, packing validated only on edge lengths, not on placement) as low-detail buildings.

### 2l. Imposter meshes: decode NOT validated (session 62) [DATA, negative]

* `IMPOSTERS/HONGKONG/TILE_Xi/TILE_Xi_Yj/IMPOSTER_*_{TERRAIN,GENERIC,GENERICWORLDMAP,BUILDING,ROAD}.P3D`: one `MESHLETMESH` each, `COLONORMPOS_UV0_UV1_` (no `vuvy`), Y-up bbox
  (TERRAIN X63_Y42: x 0..512, y -51..282 = **absolute heights, max equals the decoded heightfield max 281.96**, z -512..0), same u8 meshlet indices (meshlet sum == vertex count), `vpos` 8 B.
* The 16-bit low field is constant (`0xd3af`) over the first vertices (flat sea floor), consistent with it being the height as in the road meshes. But **no combination validates against the terrain**:
  (a) per-vertex height correlation with the decoded tile heightfield over all 6 axis permutations x 8 flips = 0.15-0.27; (b) over widths 8-22 bits x field orders x flips = max 0.27;
  (c) with the 16-bit field as height and the two 14-bit fields as x / z (4 swaps / flips) = 0.05; (d) the same with a free tile offset +-768 m (16 m steps, both z signs) against the island
  raster = 0.14. A terrain imposter must correlate > 0.9, so **the imposter `vpos` packing / frame is not (14, 14, 16) in this form** (or the mesh is not in the tile frame).
  The edge-length metric used earlier only showed the packing is locally consistent, not placed.
* So the imposters cannot give buildings yet. What would settle it: a `.P3D` whose shape is known (a car LOD or a prop) decoded with the SAME `vpos` code path as the imposters (its `vpos` is `float3`,
  so the 8-byte packed form seems specific to the level pipeline), or finding the dequantisation scale/offset in the plain head (the 10 floats after the name may be a quantisation box different from the bbox).
* Summary of the Hong Kong data hunt (2d-2l): terrain solved; roads, buildings and footprints located but each blocked on one unknown (road outline = shader-side; parcel placement = PLM id mapping;
  imposter packing). Everything needed to continue is in `tools/tdusc/{pws_island,level_mesh,road_mask,pkg_scan}.py`.

### 2m. PLM point-id mapping SOLVED (session 62d) [DATA]

* The point ids of 2k **are direct indices** into one packed `float3` table, but the table starts earlier than the run found in 2k: **entry 0 is at `0x452e59`**
  (`0x452d81 + 18 * 12`; the 2k walk had filtered out entries with |x| > 10,000, and the id 0 record `GLMK_VIC_01` lists ids 0..6). Found by a boundary test: adjacent polygons
  (next first id = previous last id + 1) must jump between their last and first point; the median jump over 121 pairs peaks at shift +18 (77.9 m vs < 48 m for every other shift in -60..60).
* Check: **97.9 % of the 514 four-point polygons are rectangles**; polygons are closed (first point repeated); `ManualBuildingPoly` points have z 0 or a constant (16.3, 18.0, 18.5: [INF] a base / top height).
* Overlaid on the decoded heightfield (`Eden x = HK x + 9472, z = 6912 - HK y`) the 4,525 polygons follow the real city: the dense strip is the Central / Wan Chai / Causeway Bay waterfront, Aberdeen, Stanley, Cyberport.
  Of them 724 `ManualBuildingPoly`, 2,815 `exclusion`, 512 `exclusion_sidewalk`; also `Taxi_n_Start/End` (24 pairs), `FX_Forest_*`, `ZE_LAKE_*`, `SND_CITY_LIGHT*` zones.
* **Gap**: the downtown strip sits where the terrain raster of the mod has **no data** (black in the overlay), so buildings there would float over the sea fill; tile coverage must be fixed first.
* Tool: `tools/tdusc/plm_footprints.py` -> JSON of `{name, kind, pts}` in HK metres (no TDUSC data in the repo).
* **Next**: (1) fix the downtown terrain gap; (2) extrude footprints to boxes as mod props (`props.txt` route, `source/game/spots/mod_props.h`; one OBJ per ~512 m tile); (3) parcel meshes
  (`PARCELBUILDSTEP`, 2j) still have no world placement - try matching each parcel's `Building_n` bbox to a `manual` footprint of its tile; (4) roads: still no source (the `Taxi_n_Start/End` points are the only route-like data found).

### 2n. Road hunt, session 62d [DATA, negative]

* **Tile `.PLM`** (e.g. `TILE_X56_Y36.PLM`, 0.9 MB, 2,007 strings): the `RDA_InsideLane_nn_<float>` (203 in that tile) / `RDA_LeftBorder` / `RDA_RightBorder` names are **decal component names** (road-marking decals next to
  `Oil_Spot`, `Dirt_*`, `TDU_DIRT_01.PSK`), not geometry. Entity records are packed **unaligned** (fields 1-3 bytes off 4-byte boundaries, scale 1,1,1 + ids + flags); no (x, y, z) triple was found near a lane
  name in either HK-world or tile-local range, so decal transforms are not decoded. A decoded lane-decal position set would be the best remaining road centre-line source (203 per dense tile).
* **No traffic / nav / GPS data in the PKGs**: a TOC search (`nav|gps|traffic|pathfind|roadnet|lane|route`) over all 154,623 names finds only textures and prop meshes. The install's `Common/` (Archetypes, FX, Settings, Tuning)
  was not read.
* The PWS tail after the quadtree (X63_Y42, from `0xcc5f0`) is **high entropy** (no float / int structure in 256-byte windows): compressed or encrypted, not a plain road table.
* Conclusion unchanged: **roads only exist as shader-side ribbons over the road pad meshes (2f-2h) plus baked heightfield ridges**. Options left: (a) decode the PLM decal transforms; (b) trace ridges; (c) author a road
  graph by hand / from OSM for Hong Kong (the `docs/re/eden_island_spec.md` item 3 route: `EDON`/`DAOR`/`ATED` nodes are accepted from generated files).
