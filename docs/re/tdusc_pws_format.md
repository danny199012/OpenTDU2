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
