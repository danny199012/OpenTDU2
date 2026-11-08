# Cut content — the automatic bank scan (2026-10-29)

The plan from `docs/STATE.md` session 45 part 7: *"a content scan of every
prototype bank against retail (`.3dd` / `.2dm` sizes are platform-invariant,
texture count, decoded pixels) to find the rest of the cut content."* Built,
run unattended over both Xbox 360 prototypes, and this is what it found.

Tools: `tools/x360/eden_bank.py` (a bank reader for **both** platforms) and
`tools/x360/content_scan.py` (the scan). Report and signatures land in `_scan\`
(scratch, gitignored): `report_<beta>.txt` (readable), `diff_<beta>.json`
(machine readable), `<beta>.jsonl` (the resumable signature cache).

```powershell
cd C:\OpenTDU2-cline
python tools\x360\content_scan.py --selftest            # the Q7 evidence, 16 checks
python tools\x360\content_scan.py --bank q7_inuit_i     # one bank, verbose
python tools\x360\content_scan.py --signatures --jobs 8 # phase 1 (~1 min per prototype)
python tools\x360\content_scan.py --compare             # phase 2 (~1 min, then instant)
```

## What it compares, and why that is trustworthy

| Part | Comparison | Evidence |
|---|---|---|
| `.3dd` scene, `.2dm` material | byte size (identical on both platforms for the same asset) | a size difference means different content |
| `.2db` texture | **decoded mip 0 bytes**, byte for byte | the 360 mip 0 untiled + 16-bit-word-swapped equals the retail bytes exactly for the three textures the Inuit prototype shares with retail (`q7_int`, `q7_int_ts`, `q7_glass_i`), and differs in 19% of the bytes for the cut `q7_int2` |
| asset names | the bank's own directory tree, parsed on both platforms | the u64 at a 2DB's 0x20 is `GetIdentifier64bit(name)` on both |
| rename | a prototype-only asset whose pixels equal a retail-only one's | 151 renames proven by identical pixels, 11 by identical size |
| `.3dg` geometry | **not compared** (the 360 packs vertex attributes, ~0.63x) | sizes reported as information only |
| audio / `xmb` / `vmf` | name only | a 360 wav is not byte-identical to the PC one even for the same sound (measured) |
| texture formats not decoded | skipped and counted (`UNCHECKED`) | the format table is in `content_scan.py` |

Full run: 9,553 named `.bnk` per prototype (13.97 GB uncompressed, ~170 banks/s,
~1 min per prototype). 4 banks per prototype fail to LZX-decode
(`frontend/legal_*`, `frontend/tips_us` — xzxd's block framing breaks on them;
the same 4 in both prototypes, listed in `_scan/<beta>.errors.txt`).

## The result, in numbers (dec2010 / aug2011)

| Verdict | dec2010 | aug2011 |
|---|---|---|
| CUT (an asset exists on one side only) | 214 | 232 |
| CHANGED (`.3dd` / `.2dm` size or texture pixels) | 205 | 234 |
| RENAMED (only the id changed) | 5 | 5 |
| SIZE (only platform-dependent sizes: `.3dg`, audio, `xmb`) | 7,515 | 7,480 |
| same | 797 | 783 |
| prototype-only banks | 4 | 4 |

**7,937 banks are flagged by both prototypes** (only 2 dec2010-only, 14
aug2011-only): the differences are systematic — the whole game was revised after
the prototypes, they are not a quirk of one build. The two prototypes are
otherwise near-identical for content (the Q7 Inuit bank is byte-identical in
both, `--selftest` checks it).

## The findings

### 1. Car interiors — the Q7 Inuit is not the only one

| Bank | What changed |
|---|---|
| `vehicules/interiors/q7_inuit_i.bnk` | prototype-only `wood_inuit.2db`, `.3dd` 11,360 vs 11,136, `.2dm` 25,920 vs 29,904, `q7_int2.2db` pixels differ (the grey dials) — restored, see `CROSSBUILD_PLAN.md`. **2026-10-30: the wood trim + grey dials are playable in the port** — `tools/x360/make_q7_inuit.py` packs the hybrid bank (the prototype's byte-swapped `.2dm`, the retail scene with the Inuit materials substituted by role, the two prototype textures as PC `.2db`) into the mod overlay; `docs/images/q7-inuit-hybrid-zoom.webp` |
| `vehicules/interiors/r8_i.bnk` | the prototype's **R8** interior (`r8_i.3dd/.3dg/.2dm`, `audi_r8_int`, `audi_r8_int2`, `audi_r8_int_ts`, `audi_r8_glass_i`) is retail's **R8 V10** interior (`r8_v10_i.*`, `r8_v10_int*`) — a whole interior replaced, not renamed (nothing pairs by pixels) |
| `vehicules/interiors/mustang_v6_premium_i.bnk` | prototype has its own `mustang_v6_premium_i.3dd/.3dg/.2dm` + `couture_croix.2db` / `couture_croix_ts.2db`; retail's bank holds `mustang_gt_i.*` — the V6 Premium cabin and the couture-cross trim were cut |
| `vehicules/interiors/mustang_gt_i.bnk`, `default_i.bnk` | one glass texture's pixels differ each |

### 2. Prototype-only banks (assets with no retail counterpart at all)

* `vehicules/rim/chrysler/300_c_srt8_f_01.bnk`, `vehicules/rim/shelby/shelb_gt_f_01.bnk`,
  `vehicules/rim/ford/mustang_v6_premium_f_01.bnk` — wheel models.
* `vehicules/med/mustang_v6_premium_low.bnk` — the Mustang V6 Premium's low LOD.
* `avatar/clothes/eden/m_ed_t_preorder_audi_inuit.bnk` and `w_...` (dec2010 only)
  — the **Audi Inuit preorder clothes** for both avatars.

### 3. Islands — re-exported scenes and textures

* `.3dd` size changes: 25 Hawai + 21 Ibiza level banks (`.2dm`: 16 + 13).
* Texture pixel changes: **171 in Ibiza, 89 in Hawai** (island, area and sector banks).
* `islands/*/scenarics/wrecks/pack_wrecks.bnk`: Ibiza `+1 -24` with 7 changed
  textures, Hawai `+4 -15` with 14 — the wrecks were rebuilt.
* Prototype-only island assets: 6 textures + 5 `uva` (Ibiza), 6 + 5 (Hawai).

### 4. Frontend (Flash) — renames plus revised art

* 42–48 texture pixel changes per resolution (`hires` / `medres` / `lowres`),
  ~50 prototype-only texture ids per resolution (many proven renames, e.g.
  `fla_5289.2db` -> `fla_5352.2db`, same pixels).
* Whole-bank revisions: `challeng.bnk` (+17 -2), `map.bnk` (+8 -18), `hud.bnk`
  (+7 -56), `club.bnk` (+4 -13), `dlc.bnk` (+5 -11), `main.bnk` (+4 -3),
  `cas_common.bnk`, `cas_slotscreen.bnk`, `allres` (9 textures).
* `interior/icd_asia.bnk` (**aug2011 only**): 21 prototype-only textures — the
  largest single-bank cut candidate.

### 5. Vehicles

* `vehicules/high/r8.bnk` + `vehicules/med/r8_med.bnk`: the bank holds the plain
  **R8** in the prototype (`r8.3dd`, `audi_r8_*` textures) and the **R8 V10** in
  retail (`r8_v10.3dd`, `r8_v10_*`) — two textures are byte-identical across the
  rename (`*_breakparts`, `*_cock_lr`), the rest were re-exported.
* `vehicules/high/mustang_v6_premium.bnk` (+7 -7, 3 changed textures) and
  `vehicules/med/mustang_v6_premium_med.bnk` — the same V6-Premium → GT story.
* `vehicules/med/california_med.bnk` (+6 -0), `vehicules/common_car.bnk`
  (+2 -7, 9 changed textures, `.3dd` + `.2dm` sizes), `vehicules/rim/*` (10
  prototype-only textures), `vehicules/high` (15 changed textures).

### 6. Effects, packs, avatar

* `fx/vfx_car.bnk`: 9 prototype-only `.2dm` materials + 9 `.prt` + 2 textures.
* `fx/skidmarks.bnk` (3 textures), `fx/weatherfx.bnk` (2).
* `scenarics/packs/worldlifeset.bnk`: **15 prototype-only `.bas`** (the largest
  prototype-only non-audio set in one bank).
* `avatar/clothes` (9 changed textures), `avatar/common_bodies.bnk` (3),
  `avatar/common_bodies_light.bnk` (3), `avatar/config/pnj.bnk` (3 retail-only
  `.avt`).

### 7. Audio — repacking, not verified cut content

589 prototype-only `.wav` names (in `sound/vehicles/*`, one bank per car) and 357
retail-only ones. They are **not** cut-content evidence: the same bank carries a
different sample set in each build (e.g. `2_eleven_high.bnk` has `st_atom_*` in
the prototype and `st_370z_*` / `st_tw_*` in retail), and even wavs with the
**same name** are not byte-identical across platforms (0 of 34 shared names
match). Treat the audio rows as unverified.

## How to read the report

`_scan/report_<beta>.txt` is grouped CUT → CHANGED → RENAMED → SIZE →
UNCHECKED, with the per-asset detail under each bank:

```
CUT  euro/bnk/vehicules/interiors/q7_inuit_i.bnk
    + wood_inuit.2db                 2db 69632  (prototype only)
    q7_inuit_i.3dd                   3dd  retail 11136 -> prototype 11360
    q7_int2.2db                      pixels differ  512x512 mips 8 fmt 55 -> 512x512 mips 6 fmt 27
    ~ fla_5289.2db                   -> fla_5352.2db  (same pixels, renamed)
```

## Restoring what it found

`--restore` turns the scan into the game's existing restored-content pipeline: for
every prototype texture whose **name and dimensions** exist in retail, the
prototype's mip 0 is written as a PC `.2db` to
`<userdata>/restored/<bank>/<file>.2db` — the file `TextureCache::loadBankTextures`
loads in place of retail's (`-norestored` = retail). The retail file is the header
template (its dimensions, format, hashcode), the pixels come from the prototype,
and one mip is written (the 360 packs its mip tail). Textures that are byte-identical
to retail are skipped, as are the ones whose retail counterpart has different
dimensions (a reused Flash id, or a texture the prototype enlarged).

```powershell
# list first, write nothing
python tools\x360\content_scan.py --restore vehicules/interiors --dry-run `
    --userdata C:\temp\otdu2-cline-userdata
# then write them
python tools\x360\content_scan.py --restore vehicules/interiors `
    --userdata C:\temp\otdu2-cline-userdata
```

Verified round trip (`--selftest` covers the scan, this covers the writer):
`q7_int2.2db` comes out 262,224 bytes, `.2DB` magic, version 2, hashcode
`0x0032544E495F3751` (= `GetIdentifier64bit("q7_int2")`), 512x512, 1 mip, and its
decoded mip 0 is byte-identical to the prototype's (`9bea40581425f137`) while
retail's is not (`fb62acac5c1fcc55`). The single-texture
`restore_texture.py` writes the same bytes (checked by hash).

The vehicle interiors are a good demonstration: of 1,074 name-matching textures in
the 112 `vehicules/interiors` banks, **7 actually differ** — the Q7 dials plus six
interior **glass** textures (`mustang_v6_premium_i`, `mustang_gt_i`, `default_i`
(Delta Integrale), `tts_i`, `c12_zagato_i`, `mustang_shelby_gt500_i`). The rest of
the interiors are untouched by the prototypes.

Over the whole game (counted from the signature cache, same criteria as
`--restore`): **392 textures in dec2010 and 448 in aug2011** have a retail
counterpart of the same name and dimensions with different pixels — Ibiza 164 /
179, Hawai 76 / 84, the frontend 79 / 94, the vehicles ~20, `avatar/clothes` 9,
the interiors 4 / 7. Another 36,215 textures are byte-identical to retail, 84 / 87
have a counterpart with different dimensions (a reused Flash id or a texture the
prototype enlarged — not replaceable without the materials), and 375 / 445 have no
counterpart at all.

Two caveats the tool reports rather than hides:

* **The game keys the restored folder by the bank's *file name* only**
  (`texture_cache.cpp` strips the path), so two banks with the same stem collide:
  `frontend/{hires,medres,lowres}/gauges.bnk`, `.../gauges/superlight_r500.bnk`,
  and `islands/{ibiza,hawai}/level/area-N/area-N-M.bnk` (7 keys in dec2010, 9 in
  aug2011). `--restore` writes a key only when every claimant produces the same
  bytes; otherwise it reports it as ambiguous and skips it (a path-aware key in
  `texture_cache.cpp` would lift the limitation).
* When the two **prototypes** disagree on the same texture, the first prototype in
  `--betas` wins and the disagreement is printed (one case in the interiors:
  `mustang_gt_i`'s glass texture).

**Not restorable this way**: a prototype-only texture has no retail header template,
and a texture whose dimensions changed cannot replace retail's (the materials' UVs
would not fit). Those need the `.2dm` material (and `.3dg` geometry) conversion —
the cross-build converter of `CROSSBUILD_PLAN.md` tier 3.

## Limitations / next steps

* **A bank re-exported under a new naming scheme reads as CUT** (the `r8` /
  `r8_v10` case): the pixels differ *and* the names changed, so the pairing
  cannot be proven. The report shows both sides, which makes it readable; a
  name-similarity or shape pairing pass would classify these automatically.
* **The `UNCHECKED` verdict** counts textures in formats the scan does not
  decode (the format table covers DXT1-5, ATI2 and the uncompressed
  `eViewFormat` set); no flagged bank hit it in this run.
* **The mip tail is not compared** (the 360 packs it): a texture that differs
  only in its lower mips reads as identical.
* **Audio needs a decoder** (XMA or the 360's own wav variant) before the sound
  banks can be compared at all.
* The 4 undecodable `frontend` banks (`legal_*`, `tips_us`) are the only gap in
  the prototype side; their retail side is known.

