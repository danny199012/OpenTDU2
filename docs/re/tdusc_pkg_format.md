# TDUSC `.PKG` / `.PWS` container format (session 58, first pass)

Tags: `[DATA]` read from the owner's own TDUSC install (Steam), `[INF]` inferred. **No TDUSC data is in
this repo**: `tools/tdusc/pkg_scan.py` only reads the owner's files and writes a CSV into a folder the
owner names. TDUSC is **not Unreal** (it is KT Racing's own engine): no `.pak` / `.utoc`, so FModel does
not apply. The install has no anti-cheat or DRM (owner, 2026-11-18; the `sard_*.sys` driver in the folder is a
leftover of a scrapped anti-cheat).

## 1. The install [DATA]

`WIN32/PKG/CHUNK_{0,1,2}/CHUNK_n_PARTxxxx.PKG` (23 parts, each capped just under 4 GiB, ~90 GB),
`WIN32/LEVELS/{Ibiza,HONGKONG}/TILE_Xnnn/TILE_Xnnn_Yyyy/<ISLAND>.PWS` (1,171 world tiles, ~16 GB),
`WIN32/Sounds/*.bank` (378 FMOD banks, ~17 GB), `WIN32/Movies`. `Common/` holds only GPU score tables,
FFB configs and option tables (plain text / Lua).

## 2. PKG layout [DATA]

* Every part starts with the same 16 bytes. The first ~1.98 MB of a part is a **table of contents that is
  masked** (see 4); the rest are records.
* Records are padded with **0x30** (`'0'`): a part is mostly fill (CHUNK_0 part 5: 343,699 of 416,130
  4 KiB blocks are all-0x30; 1,565 data islands, 297 MB of 1.7 GB), so records are found by scanning for the
  magic, not by walking.
* A record is `PKGB` (4 bytes) placed **immediately before a 4 KiB page boundary** (offsets end in `ffc` /
  `...c` / `...fc`: the payload then starts page-aligned), followed by one of two payload kinds:

### 2.1 `PKGB` + LZ4 frame -> RIFF `PA__` (geometry and buffers)

`04 22 4d 18` is a standard **LZ4 frame** (FLG 0x40: no checksums, BD 0x50: 256 KiB blocks, one or more
blocks, end mark). The decompressed bytes are a RIFF container:

```
"RIFF" u32 size  "PA__"
  "Head" u32 30    { u32 type = 3, u32 dataSize+20, f32/u8 ..., u16 flags 0x0100 0x0001, u16 x5, 0xABCD }
  "Data" u32 n     n raw bytes
```
`[DATA]` the first record of CHUNK_0 part 5 is a 225,656 byte **16-bit index buffer** (the values run 1, 8, 9,
10, 11, ...). All 156 records sampled in that part are `PA__` type 3 with a 30-byte Head; the five `u16`
after the flags are a quantised bounding box [INF], and `0xABCD` ends the Head (`cdab` in the dump).

### 2.2 `PKGB` + `PATX-SP2` (textures)

`PATX-SP2` `ENDI` u32 3 (version), 'PC' 0x0009 (platform and version), then a **mip table**; width and height are
`u32` at record +0x30 / +0x34. Mip entries step 4096 -> 2048 -> 1024 -> 512 ... with sizes `0x800000`,
`0x200000`, `0x80000`, `0x20000` = **width x height / 2 bytes = BC1 (DXT1)** [INF from the sizes; BC3/BC7
tables for other sizes unverified]. CHUNK_2 part 1, 99 records: 47 x 1024^2, 20 x 4096^2, 17 x 2048^2,
10 x 512^2, plus small LUTs (256x4, 64x4). The pixel data is **not** LZ4-framed.

## 3. PWS (world tiles) [DATA, header only]

`TILE_X100_Y124/IBIZA.PWS` (9.8 MB): u32 `0x00010017`?, then descending section offsets (0x9545f8, 0x8d35f0,
0x8c60c0) just under the file size (9,796,158), a `0xABCD` marker, dense body (entropy 7.8-7.95, i.e. compressed).
**The compression is not identified yet.**

## 4. The table of contents [DATA] - SOLVED (session 58)

The first ~2 MB of every part is a **name table masked with a 32-byte repeating text key**
`oL7bFfNRojSzr4y7t6s7wWMIA0w03zDz` (the same in all 23 parts; XOR from file offset 0, period 32). It was recovered
from the table itself: wherever the plaintext cell is zero the key shows through (`oL7bFfNR...` at every
32-byte cell: 9,623 hits in one part). After the XOR the table is plain: **`u32 pathLen`, the path
(`WIN32\GRAPH\CARS\...`), then an 88-byte tail** (entries are packed, not aligned):

```
u32 flag     u64 offset (absolute, the PKGB position)   u64 FILETIME
u64 frameBytes   u64 decodedBytes   u32 kind (2 = LZ4 frame, 0 = stored)
u8[16] hash (MD5-like, unverified)   u8[32] zero
```
Verified: for the first 300 entries of CHUNK_0 part 5 the offset points at `PKGB` (300/300) and `frameBytes` /
`decodedBytes` match the LZ4 frame and the decoded RIFF exactly (e.g. EMOTE_LAUGH.PAN 126,748 -> 225,714). One
entry (EMOTE_MICHELIN_FLAG.PAN) failed the pure-python LZ4 decode: probably a multi-block frame detail, open.
`pkg_scan.py --toc` decodes all 23 parts: **154,623 named files, 53.1 GB decoded**.

### 4.1 What is in the game (counts by extension / folder) [DATA]

`PTX` 69,755 textures (`PATX-SP2`, stored, `kind 0`), `PMA` 30,184 (materials), `PSI` 20,557, `P3D` 18,951 (models),
`PACLOG` 8,301, `PAN` 3,035 (animations: record type `PA__`), `PLM` 1,855, `PSVO` 1,239, `PSK` 395 (skeletons),
`PFX` 269, `PMC` 42, `PFT` 38, `PIM` 2. Largest folders: `GRAPH\LEVELS` 39,569, **`GRAPH\CARS` 22,176** (45
brand folders: AC ... WMOTORS, incl. **LAMBORGHINI**: Centenario, Diablo VT, Huracan, ...; 1,097 PMA, 360 PTX,
205 PSI, 151 P3D, 142 PAN for that brand), `TEXTURES\PROBES` 12,296, `LEVELS\HONGKONG` 10,336,
**`GRAPH\CHARACTERS` 8,114** (AVATAR\MALE 5,297, FEMALE 1,971, NPC 819), `GRAPH\PROPS` 8,016,
`LEVELS\IBIZA` 4,288 (per `TILE_Xnn`), `GUI\LIVERYEDITOR` 1,774, `GRAPH\INTERIOR` 3,516.

## 5. What it unlocks

* Textures: PATX BC1 -> DDS/PNG is a short job once the mip/format table is confirmed.
* Geometry: `PA__` buffers decode today; vertex buffers need the Head field meanings and a stride per type.
* Cars / clothes / islands as mods still need the TOC or the PWS scene data to know which buffer belongs where.

## 6. Ruled out

* "The PKGs are AES-encrypted": no, record payloads are plain LZ4 / raw texture data. The first sample was the
  masked TOC and a fill gap, not the content.
* A single XOR key over the WHOLE file: only the name table is masked, the records are not. (A 160-byte self-similarity peak was a multiple of the real 32-byte period, found later from the repeating `oL7bFfNR` cells.)

## 7. A car model: `P3D` / `PSI` / `PMA` (Huracan, session 58) [DATA]

`GRAPH\CARS\LAMBORGHINI\HURACAN` is 194 files: 13 `MESH\*.P3D` (LODs `UHR` / `HR` / `MR` / `LR`, the interior `_I_` set, six wheel
meshes `W_F_nn` / `W_R_nn`), 20 `PSI`, 92 `PMA\*` materials, 53 `TEXTURES\*.PTX`, 17 `ANIMATIONS\*.PAN` (doors, wipers, aero,
pilot poses). `P3D` and `PTX` are **stored** (`kind 0`); `PSI`, `PMA`, `PAN` are LZ4 frames.

* **`PSI`** = RIFF `PA__` with a `SCENINST` head: a plain table of `DATA:/GRAPH/CARS/.../MESH/<x>.P3D` followed by
  `DATA:/GRAPH/CARS/.../PMA/<set>/<material>.PMA` + a part name (`Body`, `GIBLOCKER`, ...): which mesh uses which materials.
* **`P3D`** = `SCENE___` `ENDI` container (v3, platform `PC` 9). A plain section (1.7 - 15.7 KB for a car LOD) holds the scene: per
  `MESH` a bounding box (min xyz, max xyz) + bounding sphere (10 floats), then one entry per part with the vertex format
  string (`TANBNORMPOS_UV0_UV1_`, `COLOTANB...`), the vertex count, the streams `vtan` / `vnor` / `vpos` / `vuvs` (+ `vcol`) with
  their byte offsets into the vertex blob (the Huracan LR body: 708 vertices, `vtan` at 0, `vnor` 0xb10, `vpos` 0x2130, `vuvs`
  0x4260 => 4, 8, 12 and 8 bytes per vertex), then index counts; then the node names (`D_CAR`, `D_Exhaust_Flash_L/R`, `D_Wiper`,
  `Fork_FL/RL`, `Engine`, `Convex_$PHYS_$CBLOCK_$CCAM_01` = the collision hull ...).
* **The vertex / index blob is DirectStorage GDeflate** (file offset = main header word 13): `u8 id = 4`, `u8 magic = 0xfb (id ^ 0xff)`,
  `u16 numTiles`, `u32 { tileSizeIdx : 2, lastTileSize : 18, reserved }` (tiles are 64 KiB), then `u32 tileEnd[numTiles]`
  (cumulative compressed end offsets), then the tile data. Uncompressed size = `(numTiles-1) * 65536 + lastTileSize` and
  equals main header word 18 (Huracan LR: 46,272; wheel W_R_03: 206,492 = 3 x 64 KiB + 9,884). Verified on 13 files.
  GDeflate is **not** plain deflate (raw deflate / zlib / LZ4 / zstd / brotli / lzma were all tried and fail); it is the format
  published with Microsoft's DirectStorage (github.com/microsoft/DirectStorage, `GDeflate/README.md`, `shaders/GDeflate.hlsl`,
  Apache-2.0): 32 interleaved bitstreams ("lanes") per tile decoded in rounds, Deflate64-style length / distance tables.
  **`tools/tdusc/gdeflate.py` is a pure-Python decoder written from that description - it decodes all 13 Huracan blobs**
  (LR: 27,236 -> 46,272 bytes in 0.07 s; HR: 1.2 MB -> 2.1 MB in ~4 s). Tile layout, from the reference
  `GDeflateDecompress.cpp`: tile 0 starts at data offset 0, tile i > 0 at `tileOffsets[i]`, and the LAST tile's compressed size is
  `tileOffsets[0]`. (The DirectStorage runtime route via ctypes returned `E_NOTIMPL` and was dropped.)

### 7.1 The decoded blob [DATA, Huracan]

Per part, offsets from the plain section index the decoded blob: `vtan` (4 B / vertex, packed tangent), `vnor` (**4 x half**, xyz
unit length, w = sign), `vpos` (**3 x float**, metres, **Z up, Y along the car, X across**), `vuvs` (**one u16 pair per UV set** = `UV0_UV1_` 8 B, `UV0_UV1_UV2_` 12 B; section 7.3), optional `vcol`; the **index buffer is `u16` triangle list** at `iboff` (right after the vertex streams, 16-byte
aligned), `ntri` triangles. Part entry in the plain section, after `MESH` + 10 floats (bbox min xyz, max xyz, sphere centre xyz, radius)
+ `u32 nparts`: `u32 1, u32 nWords, char fmt[nWords*4], u32 0, u32 0, u32 vcount, u32 nStreams, {char[4] tag, u32 offset} x nStreams,
u32 ntri, u32 iboff, f32 (a distance? e.g. 358.3)`.
**Verified**: the LR body is 708 + 36 + 488 vertices (+ small parts), its positions span exactly the header bounding box
(-1.095..1.097, -2.405..2.093, -0.003..1.013 m = 2.19 x 4.50 x 1.02) and Blender renders a recognisable low-poly Huracan Performante
(wing, side intakes); HR = 38 meshes / 90 parts / 55,217 triangles (body shell, wheel arches, suspension, interior, mirrors).
**The parts are in node-local space** until the scene graph (section 7.2) is applied; `tools/tdusc/p3d_to_obj.py` now applies it
(`--local` keeps the raw node-local output). `tools/tdusc/p3d_to_obj.py` writes the OBJ.

`tools/tdusc/tdusc_read.py` (`ls` / `info` / `strings` / `export`) reads any entry by its game path.

### 7.3 UVs (session 61) [DATA]

`vuvs` holds, per vertex, one `(u16, u16)` pair per `UVn` in the format string (`nUV * 4` bytes), **unorm16** (value / 65535, v = 0 at the
top like the DDS). Scored by the median UV length of a triangle edge over HR parts: unorm16 0.002-0.01 (smooth, islands intact), snorm16
0.003-0.015, half floats NaN / out of range garbage -> unorm16. The earlier 'reads as zeros' came from decoding LR with the wrong stride.
`p3d_to_obj.py` writes UV0 as `vt` (v flipped). Seen: LR body UVs sampling `BODY_LR` put the rear-lamp / diffuser detail on the rear
(local render `tdusc_huracan_lr_uv_bodylr.png`). `vtan` (4 B) still undecoded; `PMA` (which texture each part uses) is the next step.

### 7.4 PSI / PMA materials (session 61b) [DATA]

* **`PSI`** (next to the `MESH` folder, `<x>.PSI`): after the `DATA:/.../MESH/<x>.P3D` string a flat list of (node name, `DATA:/.../PMA/<set>/<name>.PMA`)
  pairs; **the k-th pair of a node name is part k of that node's mesh** (HR `Body` 14 pairs = 14 parts, `Door_L` 6 = 6, `Aero_Bumper_L` 1 = 1).
  LR also lists `GIBLOCKER` / `GIPORTAL` (glass-only GI helpers) and `Convex_$PHYS_$CBLOCK_$CCAM_nn` -> `PHYSICALMATERIAL/CBLOCK.PMC` (the collision hulls).
* **`PMA`** (LZ4 frame, RIFF `PA__`): shader `DATA:/EFFECTS/VEHICLECARPAINT.PFX` / `VEHICLECARMAT.PFX`, `Use ...` flags, then parameter names; each
  **texture parameter is followed by its `DATA:/.../*.PTX` path**: `DiffuseMap` / `DiffuseMapTile`, `NormalMap` / `NormalMapTile`, `StructureMap(Tile)`
  (the `_IOR` packed sheet), `OcclusionMap`, `DirtMask`, `ScratchMap`, `DamageNormalMap`. Colours and floats (`ExternalColor`, `Gloss1`, `Metalness`,
  `FlakeIntensity` ...) are binary and **not decoded yet**: the paint / metal materials have no diffuse texture, only colour.
* `tools/tdusc/psi_mtl.py` + `p3d_to_obj.py` write `usemtl` per part and a `.mtl` (`map_Kd`, `map_Bump`) with the textures converted to `tex/*.png` + `.dds`
  next to the OBJ (`--nomat` skips it). HR: 34 materials, 19 with a diffuse texture.
* **Verified** with a per-material textured render of HR: the EU licence plate sits on the rear plate, the tail-lamp roundels, the door label and the
  window tint land where they belong (local render `tdusc_huracan_hr_materials.png`). Paint colour is the one thing missing (binary parameters).

### 7.2 The scene graph (session 61) [DATA, Huracan HR / LR]

After the `MESH` entries comes the string `DEFAULT`, then **two tables joined by one id space**: `nDummy` dummy nodes (the word right
before the first header; HR 24, LR 11: `Root`, `D_CAR`, `D_Door_L`, `D_Wiper_FL_01`, `Fork_FL` ... no mesh) and, after a `u32 nMesh`
(HR 38, LR 1), the mesh nodes. Every record is

    u32 id, u32 nChildren, u32 nDescendants, u32 nameLen, char name[nameLen], f32 m[16]
    mesh nodes only, 21 more bytes:  00 00 00 04, 7f x 8, ff ff, 01 00, u32 meshIndex, 00

* `id` is the **pre-order index of the whole tree** (HR: 62 nodes, `Root` = 0, `D_CAR` = 1, `Body` = 2 ... `Glass_R_Int` = 33, then
  `D_Ref_CarAnim` = 34 ... 61). The tables are written in a different order, so the parent of id *k* is the deepest node whose range
  `(id, id + nDescendants]` holds *k* (`Root` has `nDescendants` = N - 1 = 61 for HR, 11 for LR: the check that the walk is right).
* `m` is a **row-major 4x4, row vectors, translation in row 3**, local -> parent: `world = m * parent.world`. All 62 HR matrices are pure
  rotation + translation (det = 1); the Mirror nodes carry the **negated door hinge** because the mirror vertices are stored in car space
  and hang under `Door_*` (hinge `D_Door_L` = (0.916, -0.862, 0.504)) -> world (0, -0.018, 0) = identity within 2 cm.
* `meshIndex` counts the `MESH` entries in file order (alphabetical by node name: `Aero_Bumper_L` 0, `Body` 2, `Door_L` 5 ... `Wnd_R1` 37).
  A mesh no node uses (LR meshes 1 and 2) is exported untransformed.
* Coordinates: **Z up, front = -Y, rear = +Y, left = +X** (`Fork_FL` (0.819, -1.310, 0.194), `Fork_RL` (0.819, +1.310, 0.194): wheelbase
  2.62 m, track half-width 0.819 m; `D_Exhaust_Flash_*` at y = +2.086). The `Fork_*` wheel anchors exist in the LR file only.

**Verified**: HR with the graph applied spans (-1.11, -2.41, 0.00)..(1.11, 2.11, 1.03) m, the LR header's own box (-1.095, -2.405, -0.003)..
(1.097, 2.093, 1.013) to 2 cm; with `--local` the doors / mirrors / windows drop to z = -0.43 and the box is wrong
(local renders `tdusc_huracan_hr_local.png` vs `tdusc_huracan_hr_nodes.png`, kept outside the repo like all TDUSC art, a plain z-buffer render, looking up from below).

## 8. Ruled out (model blob)

* The executable's strings say nothing (it is packed: no `PKGB` / `SCENE___` literal); not pursued.
* A second 32-byte text key on the blob: an artefact of overlapping hexdumps, not a mask.

## 9. `PTX` textures -> DDS (session 61) [DATA, Huracan, 53 files]

`tools/tdusc/ptx_to_dds.py` converts them (`--png` writes a preview, `--all <prefix>` a whole folder, DX10-header DDS, mip 0).

* Layout: `PATX-SP2` `ENDI` u32 3 ..., then 12-word entries from word 11: `u32 width, height, depth(1), fileOffset, 0, compressedSize,
  uncompressedSize, 0 x 5`. Entries are mip 0, mip 1, ..., and the **last is a packed tail** of the small mips (`width` 0x80, **0x5e10**
  bytes, always). A 4096^2 texture has 5 entries, a 512^2 one 3, `ALPHA` just 1.
* **Every entry is a DirectStorage GDeflate stream** (`04 fb numTiles ...`, `tools/tdusc/gdeflate.py`) of block-compressed pixels, so the
  old note "pixel data is not compressed / BC1 from the sizes" (section 2.2) was only the uncompressed-size arithmetic: the file holds it
  packed (BODY_LR 512^2 mip 0: 84,516 -> 262,144 bytes).
* **The header names no pixel format**; `uncompressedSize * 8 / (w * h)` gives 4 or 8 bits per pixel. Decoded and *looked at*:
  * 8 bpp `_DIFF` (and `_LGT_MSK`, `_ALPHA`) = **BC3** (DXGI 77): the logo sheet, dashboard, tail lamps and the BODY atlas decode clean; BC7
    decodes the same bytes to noise.
  * `_NRM` (8 bpp) = **BC5 SIGNED** (DXGI 84): flat normal = (0, 0); BC5 unorm and BC3 / BC7 give noise (`C:/temp/tdusc-evidence/tdusc_huracan_detail_nrm_bc5s.png`).
  * 4 bpp `_DIFF` / `_IOR` / `_EMI` / `_F_DETAIL` = **BC1** (the IOR sheets are packed channels, yellow / magenta in a viewer); `_AO` = BC4 [INF
    from the name: the 2048^2 wheel AO decodes as a clean greyscale wheel].
* **Unresolved: the dirt masks** (`BODY_MASK_DIRT`, `GLASS_MASK_DIRT`, `W0n_MASK_DIRT`, 8 bpp, 4096^2 / 2048^2): BC3, BC5, BC5S and BC7 all
  decode to structured noise (the wheel shape shows through, so the tiles are right and the block format is not). Not needed for the look.
* Result: **53 of 53** Huracan textures convert without an error (`C:/temp/tdusc-evidence/tdusc_huracan_textures.png`).
