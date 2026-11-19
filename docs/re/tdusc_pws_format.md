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

## 3. Tool

`tools/tdusc/pws_scan.py <file.PWS> [--out DIR] [--max-bytes N]` lists and decodes every embedded stream (pure Python, ~minutes for a 3 MB tile; a C++ loader would port
`gdeflate.py`). Verified: X75_Y26 and X57_Y58 -> `ok 1364 bad 0`.

## 4. Not known yet / next

* The float layout of the 12,861 / 21,053-byte blobs (vertex stride, which part is X/Z, how the 1,280 cells map to the tile's world rectangle). Start from
  `X57_Y58`, slot `0x30a10` (the biggest, 8,232 B compressed): plot the floats as a heightfield and compare with the real coast.
* What the *large* tiles (24 MB) hold beyond these 1,364 streams (the buildings / props are surely there: the 3 MB tile carries 1,364 streams, the 24 MB one will carry more or other kinds - run `--max-bytes` large).
* Tile grid: world position of `TILE_Xn_Ym` (steps of one tile = ?), needed to place anything.
* Not checked: whether the road / nav data is in the PWS or the packed exe / servers (see `tdusc_weather_physics.md`).
* Island mod plan: (1) this doc; (2) heightfield + static meshes to OBJ; (3) terrain-only `islands/hongkong` with `tools/mod/make_eden_island.py` as the template; (4) objects, roads, minimap.
