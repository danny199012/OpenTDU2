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

## 2b. The stream layout - DECODED, terrain heights confirmed (session 62) [DATA]

Each decoded stream = **256-byte header, 1 pad byte, then float32 data at offset 257** (not 4-aligned; sizes are `4n+1`).
* Header floats: `[0..8]` 3x3 rotation, `[9..11]` **centre x, y, z**, `[12..14]` half extents - i.e. an oriented bounding box - then the same again at `[15..29]`
  (a second box); `[30..63]` 34 more floats (sample heights of the box).
* **World frame: x, y horizontal, z UP, metres.** Tile `TILE_X63_Y42` spans x 5120..5632, y -256..256 (a 512 m square); z 58..282 m.
* Floats at 257: **a 33x33 heightfield** (1,089 floats; read as `reshape(33,33).T`, then the first 32x32 are the cell), **16 m per cell, 0.5 m samples**.
  Leaf cells sit on a 16 m grid (centres 5128, 5144, ...; y -248, -232, ...), 32x32 = 1,024 cells per tile, in Z-order in the file. ~960 of 1,024 are present (the rest:
  other stream kinds, sea or neighbours' LOD). Larger streams (50-70 KB, multi-tile `04 fb 02 00`) are coarser parents with bigger boxes (half extents 23-66 m).
* After the 1,089 floats: more float data (vertex-like, y values near -250 in the first leaf) - not decoded.
* **Verified:** `tools/tdusc/pws_height.py` stitches 941 leaf cells of `X63_Y42` into a 1024x1024 map with **no seams between cells** (orientation and stride correct) and a smooth
  hillside falling ~220 m across the tile: a local render, not committed (TDUSC art stays out of the repo; regenerate with `pws_height.py --png`) (hillshade version looks like real terrain, not noise).
* Multi-tile streams (tiles >= 2): the decoded size is `65536 * (tiles - 1) + last`, and the stream ends at `8 + 4*tiles + offs[-1] + offs[0]`.
  A script that cut the stream at `offs[0]` alone hung the decoder - use `pws_scan.candidates` + that end.

## 3. Tool

`tools/tdusc/pws_scan.py <file.PWS> [--out DIR] [--max-bytes N]` lists and decodes every embedded stream (pure Python, ~minutes for a 3 MB tile; a C++ loader would port
`gdeflate.py`). Verified: X75_Y26 and X57_Y58 -> `ok 1364 bad 0`.

## 4. Not known yet / next

* Tile placement: `TILE_X63_Y42` = x 5120..5632, y -256..256; the mapping from the tile name to world x/y is not known (not 512 * index). Decode a second land tile's centres to fit it
  (`X57_Y58`, `X75_Y26` gave no non-zero centres in my first 400 leaf slots - check which slots are leaves there).
* The ~60 missing cells per tile and the 50-70 KB parent streams (do they hold different detail, roads, water?).
* The float data after the heightfield (vertices of the static meshes?) and the `0xbf0c0..` index sections.
* Where roads / nav / buildings live; whether the PWS carries them at all.
* Speed: `gdeflate.py` is pure Python (a 24 MB tile = a few minutes; fine for a one-off converter).
* Island mod plan: (1) done; (2) heightfield done for one tile -> all 589 tiles + meshes; (3) terrain-only `islands/hongkong`, template `tools/mod/make_eden_island.py` (needs the Eden
  `.3dg` heightfield format, `docs/formats.md`); (4) objects, roads, minimap.
