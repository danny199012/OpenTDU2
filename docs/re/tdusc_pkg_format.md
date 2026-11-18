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

## 4. The masked table of contents [DATA / INF]

The first ~1.98 MB of every part has a steady byte entropy of 7.1 (not 8: it is not a cipher stream), the
text-like key `oL7bFfNRojSzr4y7t6s7wWM...` recurs at +0x1000 and +0x100000, and rows of **160 bytes (0xA0)**
repeat with ~26 % equal bytes. Read as u32, 325,002 of 495,616 values fall inside the file's own offset range,
so it is an **offset table obfuscated with a repeating printable key** [INF], not compressed data.
Not solved: the key period and the row layout. Without it, records have no names or object grouping; the
scene description (which buffers and textures make a car) is the next thing to find.

## 5. What it unlocks

* Textures: PATX BC1 -> DDS/PNG is a short job once the mip/format table is confirmed.
* Geometry: `PA__` buffers decode today; vertex buffers need the Head field meanings and a stride per type.
* Cars / clothes / islands as mods still need the TOC or the PWS scene data to know which buffer belongs where.

## 6. Ruled out

* "The PKGs are AES-encrypted": no, record payloads are plain LZ4 / raw texture data. The first sample was the
  masked TOC and a fill gap, not the content.
* A single repeating XOR key over the whole file: the self-similarity peaks at 0xA0 (row size), not at a key period.
