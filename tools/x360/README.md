# tools/x360 — reading the Xbox 360 prototypes' data

Small Python helpers used to compare the two Xbox 360 prototypes of TDU2 (Dec 3 2010 QAv.046 and
Aug 8 2011) with the retail PC data. Findings: `docs/re/CROSSBUILD_PLAN.md` (the prototypes section).
Paths at the top of `beta_list.py` point at this machine's copies; `TOOLS` is the TDU2 360 recomp's
`tools` folder (its `tdu2_bigfile.py` parses the `.map` files, its `xzxd.exe` decompresses the LZX payloads).

| Script | What it does |
|---|---|
| `beta_list.py [regex]` | indexes both prototypes' bigfiles, names entries from the Aug 2011 `FilelistFull.log` hashes, lists matches |
| `beta_extract.py [regex]` | extracts (and decompresses) the matching entries to `C:\TDU2-Proto\<beta>\...`; with no argument, the dialogue set (cutscene `__us` banks, US voices, the unnamed database entries) |
| `x360_bank.py <bank>` | the entries of a big-endian 360 bank: `{u64 hash; u32 block; u32 offset; u32 size}` records after the header (the table form the recomp's `tdu2_bank.py` misses) and their kind (3dd / 3dg / 2dm / 2db) |
| `x360_tex.py <bank> <outdir>` | decodes every 2DB of a 360 bank to PNG: header 0x1000 bytes (w/h u16 BE at 0x28, format 0x05 DXT1 / 0x06 DXT5 at 0x2E, name id at 0x20), mip 0 tiled (XGAddress2DTiledOffset) and 16-bit swapped |
| `eden_bank.py <pc\|360> <bank>` | a bank's entries on **either** platform: name (from the bank's own directory), size, kind. Port of `source/filesystem/bank.cpp` plus the 360 variant |
| `eden_2dm.py <file.2dm> [pc\|360]` | dumps a material library (name, hashcode, texture slots, shader) on either platform; port of `source/render/2dm.cpp`. `patch_texture` rewrites a slot's texture hashcode in place |
| `eden_3dd.py <file.3dd> [pc\|360]` | dumps a scene's node tree (name, object hash, LODs, material banks); port of `source/render/3dd.cpp` |
| `eden_3dg.py <file.3dg> [pc\|360]` | lists each geometry's primitive material indices (the PRIM headers are plain on both platforms even though the 360 vertex data is packed) |
| `eden_render_file.py <in> <out> <3dd\|2dm>` | byte-swaps a 360 `.3dd` / `.2dm` payload to the PC layout in place - the sizes are identical, only scalar fields differ (note: the `.2dm` MAT hashcodes are 8-slot identifiers in *memory order* on both platforms and must not be reversed; texture-slot u64s and parameter names are real numbers and are) |
| `build_bank.py --roundtrip <bank>` | **the PC bank writer** (`KNAB` container: header, table, types, directory tree, indexes, data). `--roundtrip` rebuilds an existing bank and compares entries + directory bytes; a rebuild with the original directory order is byte-identical except the CRCs (zlib here; the engine never verifies them) |
| `make_q7_inuit.py <userdata>` | assembles the cut Q7 Inuit interior (the walnut `WOOD_INUIT` trim + grey `Q7_INT2` dials) as a playable PC bank into `<userdata>/mods/Euro/Bnk/vehicules/interiors/q7_inuit_i.bnk`: the prototype's byte-swapped `.2dm`, the retail scene with the Inuit materials substituted by role (the geometry is retail's, so its primitive material indices are retail's), the two prototype textures as PC `.2db`. `--out <file>` writes elsewhere |
| `content_scan.py` | **the content scan**: every named prototype bank against retail, comparing the platform-invariant parts. `--selftest` (the Q7 evidence, no setup), `--bank <part>` (one bank, verbose), `--signatures` (phase 1, resumable), `--compare` (phase 2 + report), `--restore <bank part> --userdata <folder>` (write the prototype's textures into the game's restored-content folder) |
| `restore_texture.py` | one texture: a 360 prototype bank -> a PC `.2db` in `<userdata>/restored/<bank>/` (the file `TextureCache` loads in place of retail's). `build_pc_2db` is the shared writer `content_scan.py --restore` uses |

Comparing an asset across PC and 360: `.3dd` and `.2dm` keep the **same size** on both platforms for the
same asset (`q7_v12_i`, `california_i` checked), `.3dg` is ~0.63x on 360, textures are padded — so the
`.3dd` / `.2dm` sizes, the texture count and the decoded pixels are the comparators.

Never commit extracted game data; `C:\TDU2-Proto` holds it outside the repo.

## Facts about the 360 banks (measured 2026-10-29, `eden_bank.py` + `content_scan.py`)

* The 360 bank header is the PC one, big endian: magic `BANK` at 0x08, `NumEntries` at 0x28, section
  offsets at 0x30/0x34/0x38/0x3C, data at 0x44. Sections are prefixed `{u32 size; u32 crc}` and their
  content starts at the offset **+ 8** - the entry table included, so its records start at
  `SectionOffsetTable + 8`, stride 0x14.
* A 360 table record is `{u32 offset; u32 size; u64 unknown; u32 alignment}` - the offset/size pair sits
  first, which is why the recomp tool's scan (`{u64 hash; u32 block; u32 offset; u32 size}`) reads the
  right offsets/sizes from a start 12 bytes *earlier* while pairing each with the previous asset's hash.
  Entry *i* is record *i* on both platforms; the name comes from the directory, as on the PC.
* The directory tree and the index table are byte-oriented and identical on both platforms: parsing them
  from a 360 bank names every asset exactly as the PC bank does (`Euro/Bnk/vehicules/interiors/q7_inuit_i.bnk`
  -> `q7_inuit_i.3dd`, `wood_inuit.2db`, ...). Self-check: the u64 at a 2DB's 0x20 is
  `GetIdentifier64bit(name)` on **both** platforms (`q7_int2` -> `0x0032544E495F3751`).
* A 360 `.2db`: page-aligned header (texels at 0x1000, not 0x50), the same header fields (w/h at 0x28,
  format at 0x3C - in the build's own enum, 27 = DXT5 where the PC's sRGB variant is 55), and mip 0
  tiled + byte-swapped per **16-bit word**. That transform reproduces the retail bytes exactly for the
  three textures the Inuit prototype shares with retail, so the scan compares textures byte for byte.

