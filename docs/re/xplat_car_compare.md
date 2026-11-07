# One car asset, everything it carries, on all three platforms

Written 2026-10-30 (Cline, `cline`). The owner: *"I wanted you to just test 1 thing,
1 car asset with everything shader, interior etc... all loaded and can we compare
exact same as to a xbox360 and ps3 version I want to see if there's a size
difference or difference in poly count etc... is this something we can possibly
test?"*

**Yes — and it is done for one car.** `tools_probe/xplat_car_compare.py` measures the
same bank on the PC, on the Xbox 360 prototypes and on the PS3 and prints them side
by side. The measured answer on the Ferrari California: **the polygon count is the
same on all three platforms, the sizes and the packing are not.**

Tags as in `CLAUDE.md`: `[DATA]` game files · `[MEASURED]` verified this session ·
`[CODE]` readable executable · `[INF]` inferred.

---

## 1. What can be read, on which platform

| Platform | Where the data is | Reader | What is readable today |
|---|---|---|---|
| **PC** | `C:\TDU2-Gamedata\TestDrive2\Euro\Bnk\Vehicules\{high,interiors}\<car>.bnk` | the engine, GLM's `eden_bank.py` | **everything** — the asset list, every `.3dg`/`.3dd`/`.2dm`/`.2db` payload, the shaders (`Euro/Shaders/*.sob`), the collision (`.dhk`) |
| **Xbox 360** | the Aug 2011 / Dec 2010 prototype `bigfileN.{big,map}` | `tdu2_bigfile.py` + `xzxd.exe` (the recomp's tools) + `eden_bank.py` | **everything** — the payload is an LZX chain of blocks and `xzxd.exe` decodes it to a plain big-endian bank `[MEASURED]` |
| **PS3** | `C:\TDU2-PS3-Recomp\game\USRDIR\bigfileN.{big,map}` (the same container is inside the decrypted ISO) | the same map reader + `eden_bank.py` | the **bank metadata** (asset list, per-asset sizes) and every payload the packer stored **verbatim** — for a car that is the `.3dd` and the `.3dg`'s GEOA, i.e. the **prim/vertex/index/triangle counts**; the index buffers, textures and materials are inside compressed chunks `[MEASURED]` |

So the comparison is *complete* for PC and 360 today, and *counts + sizes* complete
for the PS3. Nothing about the game data is written; the tool only reads it.

## 2. The measured California (Ferrari California, `california.bnk`)

`python tools_probe/xplat_car_compare.py --car california --interior`

| | PC | Xbox 360 (Aug 2011) | PS3 |
|---|---|---|---|
| **body bank, as stored** | 2 713 600 B | 666 808 B (LZX in the archive) | 1 050 464 B (chunked) |
| **body bank, uncompressed** | 2 713 600 B | **2 306 048 B** | **2 002 944 B** |
| primitives | 126 | 125 | 131 |
| vertices | 30 589 | 30 532 | 32 363 |
| indices | 73 813 | 60 768 | 96 621 |
| **triangles** | **32 255** | **32 207** | **32 207** |
| textures | 11 = 1 065 824 B | 11 = 1 175 552 B | 11 = 738 800 B (sizes only) |
| **interior bank, uncompressed** | 2 617 344 B | 2 293 760 B | 2 144 256 B |
| interior prims / verts | 66 / 32 155 | 66 / 32 155 | 66 / 32 155 |
| interior indices | 80 240 | 67 141 | 114 726 |
| **interior triangles** | **38 242** | **38 242** | **38 242** |

What the numbers say `[MEASURED]`:

* **The polygon count is the same car.** Body 32 255 / 32 207 / 32 207 (the PC's +48
  is its own strip bookkeeping — see below) and the interior is **38 242 on all
  three**, exactly.
* **The vertex counts match primitive by primitive** — e.g. `BODY_HR` is 12 prims /
  6 759 verts / 6 728 triangles on every platform — with one real exception:
  `HANIM_S` (the convertible roof) is **14 prims / 6 957 verts on the PC and the 360
  but 20 prims / 8 788 verts on the PS3**. The extra primitives are vertex splitting
  (RSX has fewer vertex-shader registers), not new geometry.
* **The index buffers differ a lot**: the PC joins strips with degenerate triangles
  (41 306 skipped indices), the 360 breaks them with `0xFFFF` (9 437 restarts), and
  the PS3 uses **triangle lists** (type 3, no strip bookkeeping: 96 621 / 3 = 32 207
  exactly, and the interior's 114 726 / 3 = 38 242 exactly).
* **The textures are the same picture in different sizes**: the same 128x128 `.2db`
  is **21 920 B on the PC, 69 632 B on the 360, 21 968 B on the PS3**. The PS3's
  payloads are the PC's **+48 bytes** (a bigger header), while the 360's are ~3.2x
  the PC's with its own format enum (27 / 23 = DXT5 / DXT1 in the build's enum) and a
  tiled flag — the 360 stores each mip tiled and page-aligned `[INF: from the sizes
  and GLM's x360_tex.py]`.
* **The PS3's uncompressed banks are the smallest** (2.00 MB vs the 360's 2.31 MB vs
  the PC's 2.71 MB for the body): the same geometry, but its textures are stored the
  way the PC stores them instead of the 360's tiled form.

## 3. The PS3 container — what a PS3 payload actually is `[MEASURED]`

This is the one new thing the session had to decode, and it is why the PS3 column is
not empty. A PS3 `.big` payload is:

```
[ plain bank metadata ]        the BANK header, the entry table, the types, the
                               compressed path tree and the index table, exactly
                               like the 360's - byte-for-byte readable
[ 'HDR!' chunk header ]        { u32 headerSize (0x90 for the California body,
                                 0xa0 for its interior), u32 chunkSize=0x10000,
                                 u32 field=0x80, u32 magic 'HDR!' stored
                                 little-endian, u32 storedSize[N], u32 crc }
[ N chunks ]                   one per 64 KB of the bank image (31 for the body,
                               35 for the interior)
[ padding ]                    to a 256-byte boundary, filled with an ASCII
                               watermark ("...TOM.WAITS.RULES...")
```

Evidence `[MEASURED]`:

* At the data section the first four bytes are `00 00 00 40` (the header section
  size) and `BANK` sits at +8 — so the metadata is a plain bank; the table names all
  21 assets with the right sizes; the directory is readable.
* At `offset + comp` the **next** file's `BANK` header starts, so the record's
  `comp` really is the stored size (`parse_map` fits 100 % of the records inside
  both the PS3 and the 360 archives).
* The 31 chunk sizes sum to the stored bytes minus the header minus the 256-byte
  padding; the header's `chunkSize` is 0x10000 and `ceil(image / 0x10000)` = 31.
* A payload the packer stored verbatim sits at **image offset + headerSize** — the
  `.3dd` at 1392 (image 1248) and the `.3dg`'s `GEOA` at 36428 (image 36284). That
  is how the PS3's prim/vertex/index/triangle counts are read today: every
  primitive header lives in the GEOA, which is inside the first chunk.
* 3 403 of the PS3 archive's 6 476 payloads are stored whole (`comp == uncomp`, no
  chunk header at all — verified on a voice bank that parses as a complete plain
  bank) `[MEASURED]`.

**Not decoded yet:** the chunk *contents*. They are not LZX — rewrapping the chunks
into the 360's framing and running `xzxd.exe` fails (`lzxd_decompress ret=11`) — and
the first chunk is stored almost verbatim, so the compressor is a literal-run LZ
whose code stream has to be read out of the PS3 `EBOOT.ELF` (the project already has
`tools_probe/ps3/abs_refs.py` for that binary) or derived by diffing chunk 0's
stored bytes against the known image bytes. Until then the PS3's textures, index
buffers, materials and collision cannot be read, and its shaders are a separate
problem (the PS3 has its own shader binaries, not the PC's `.sob`).

## 4. What "everything loaded" would still need

| Piece | PC | 360 | PS3 |
|---|---|---|---|
| asset list, sizes | yes | yes | yes |
| geometry (`.3dg`) | yes | yes | counts only |
| scene / materials / textures | yes | yes (readers exist, GLM's `eden_2dm`/`eden_3dd`) | no |
| shaders | yes (`Euro/Shaders/*.sob`, DXVK's DXSO) | its own bytecode, undecoded | its own binaries, undecoded |
| **in the engine, rendered** | yes (`-gm ASSETVIEWER`, the game itself) | **no** — needs GLM's Phase 3 (BE banks in `bank.cpp`, 360 `.2db` decode) | no |

So "load one car with everything in the viewer" is a PC-only capability today; the
360/PS3 side of this session is an offline measurement, not a render. The next step
for the 360 is already planned by GLM (`docs/STATE.md`, the standalone asset studio:
Phase 3 = 360 support in C++); the PS3 additionally needs its chunk decompressor.

## 5. How to run it, and the traps

```powershell
cd C:\OpenTDU2-cline
python tools_probe\xplat_car_compare.py --selftest                  # no game data needed
python tools_probe\xplat_car_compare.py --car california --interior
python tools_probe\xplat_car_compare.py --car 458_italia --x360 "C:\...\Dec 3, 2010 prototype\2010-12-03_TDU2_X360_QAv.046"
```

`--pc`, `--ps3`, `--x360`, `--x360-tools`, `--recomp-tools` and `--work` override the
defaults; the tool only writes into `--work` (its two scratch files).

Traps this session paid for `[MEASURED]`:

1. **A PRIM's index-block offset is absolute in the `.3dg`**, and the index block is
   a 0x20-byte `DXIB` header (`numIndex` at +0x10) followed by u16 indices — reading
   the indices at the offset itself yields header bytes (and a max index of 22596).
2. **Never divide the index count by 3 to get triangles.** The PC's California body
   has 73 813 indices and 32 255 triangles (strips joined by degenerates), the 360's
   60 768 indices and the *same* 32 207 triangles (9 437 `0xFFFF` restarts), and the
   PS3's 96 621 indices and 32 207 triangles (lists). Walking the indices is the only
   count that agrees across platforms.
3. **The PS3's payload is shorter than the bank image**, and GLM's `eden_bank.py`
   drops any entry whose payload does not fit its buffer (offset and size become 0)
   — the image has to be padded to the map's `uncomp` before parsing, and the entry
   bytes then sliced from the *stored* buffer.
4. **The PS3's `+144` shift is per payload**, and it is validated by the payload's
   own fourcc; a payload inside a compressed chunk reads as garbage, so it is
   skipped rather than counted.
5. **`xzxd.exe` ignores the 0xFF framing's output-size field** (only the stored size
   delimits a chunk), which is why a 64 KB chunk can be rewrapped at all — but the
   PS3's chunks are not LZX, so that path still fails.
6. **The `M.WAITS.` byte sequence is padding, not a marker**: the payload tail is
   filled with an ASCII watermark, so a search for it finds ordinary padding.


