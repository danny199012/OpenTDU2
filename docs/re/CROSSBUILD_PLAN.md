# Cross-build plan — getting features into OpenTDU2 from the four versions

Written 2026-10-28, for the next multi-agent session (GLM + Claude at 4am).
Owner's brief: *"whatever you recommend we can do to help get most features
brought over, if it means we use ps3/xbox360 recomps to get there, by all
means — we have 4 versions available."*

Tags: `[MEASURED]` = verified this session, `[DEAD]` = tested and disproved,
`[NEXT]` = the recommended action.

---

## 1. What each of the four versions is actually good for

This is the whole strategy in one table. Do not use one version for everything.

| Version | Path | What it is good for | What it is NOT good for |
|---|---|---|---|
| **PC (Steam + TDU World)** | `C:\Program Files (x86)\Steam\steamapps\common\Test Drive Unlimited 2` | **All game data** (`.bnk`/`.cpr`/`.xmb`, 10,518 named files, extracted to `C:\TDU2-Gamedata\TestDrive2`) and **62 % of the code** — VA `0x6D1000–0xE407AB` plus `.rdata`/`.data`/`.rsrc`. This is where the file formats live. | The 2.81 MiB encrypted band (`0x401000–0x6D1000`) `[MEASURED]`. Also the running build is **modded** (TDU World bigfiles), so it is not a clean behavioural oracle. |
| **PS3 (EBOOT.ELF)** | `C:\TDU2-PS3-Recomp\game\EBOOT.ELF` | **The encrypted band, in the clear.** PPC64 BE, 15.6 MB of code, no SecuROM, 19,842 function boundaries, 53,119 lifted functions. Best *static* source. | No symbol table; different revision (1,698 vs 1,835 `/:` names). Not runnable here (the recomp deadlocks — but that is irrelevant to reading it). |
| **Xbox 360 (rexglue recomp)** | `E:\Xbox 360 Games\...\Test-Drive-Unlimited-2-Recomp` | **A running, instrumented build.** It boots, has an F1 menu, cheats, DLC, file logging and an instrumented-diagnosis patch. Best *behavioural* oracle — and the project already has a patch/logging workflow. | `generated\TestDrive2_DLL\tdu2_funcs.*.h` has **108,672** function declarations but **zero Eden names** `[MEASURED]` (Vehicle/Pacejka/Flash/Suspension all 0 hits) — so it gives boundaries, not names. |
| **OpenTDU2** | `C:\OpenTDU2` | The target. Its own bugs (AVM1, rendering) are **not** in any other version. | — |

### The two Xbox 360 prototypes — checked 2026-10-29, nothing to port `[MEASURED]`

`C:\Users\cuz_c\Downloads\Test Drive Unlimited 2 (Dec 3, 2010 prototype)` (QAv.046) and
`...\Test_Drive_Unlimited_2_(Aug_8,_2011_prototype)`. Do not re-survey them:

- **The `.xdb` symbol files (Dec 2010) are full PDBs (MSF 7.00, unstripped, with types) — but SDK only.** All
  705 modules are `Dev\Libs\Xenon\System\Lib\*.lib` (xonline, d3dx9, xgraphics, xWMA ...); 58,853 / 66,262
  public + global names, **zero Eden functions or types** (no `GS*` / `GM*` / `ed*` / vehicle / flash class).
  The game code was linked without debug info. `llvm-pdbutil dump -summary/-modules/-publics/-types` shows it.
- **The executables are console-encrypted XEX2** (AES + basic / LZX compression; all 8 files): no strings
  readable as stored. `tools/xex_dump.py` of the 360 recomp only handles plain images. Unpacking would need the
  recomp's runtime XEX loader (`rexglue-sdk ... xex_module`). Only possible gain: the Dec 2010
  `TestDrive2_360_OI_DLL.xex` is **29 MB** (retail 9 MB) — perhaps a less optimised build with more strings.
  Low priority: the PS3 build already has the encrypted band in the clear.
- **The data is retail's.** Aug 2011 `FilelistFull.log`: 12,115 files vs PC 11,479; the 948 "prototype only"
  files are all `__pl` / `__ru` localisations; the PC-only ones are the PC shaders and the TDU World mod. No cut
  cars, islands, cinematics or missions.
- **`49470804\00000002` (Dec 2010)**: Xbox LIVE packages "Mandatory 01-03" (content updates) and "Harley
  Davidson Fatboy Lo" (a 57 KB unlock) — the PC data already has `fatboy.bnk` and its rims.

**Contents compared (2026-10-29, second pass — "unpack, then search")**: the bigfile payloads are XMemCompress
LZX (the recomp's `tools/xzxd.exe` decodes them; its `tdu2_bigfile.py` parses the maps); 878 entries were
extracted to `C:\TDU2-Proto\<dec2010|aug2011>` (cutscene `__us` banks, `sound/voices/us`, the unnamed big0
entries). Xbox banks are big-endian with hashed entries (no path tree), audio XMA (`0x165`).

- **Dialogue: none different.** All 40 cutscene voice banks and all 203 US voice banks have the same number of
  lines as retail with the same lengths (XMA length from data / PsuedoBytesPerSec vs the retail ADPCM `fact`).
  Cutscene speech has no subtitles in `db_us` (only phone calls do), so audio is where it would show.
- **Text**: the betas' language databases are **plain XMB** (retail `.cpr` is encrypted; big0 entries
  `8483D579158D60FE` = English, `D6BF7DACA2172216` = `db_data`). English vs retail: Dec 2010 14 wording changes,
  Aug 2011 48 (+ whitespace) — all interface text ("Press MENU_VALIDATE to challenge", "It will be available in
  the final version of TDU2", "Hypersport" / "Driving Aid" for "Hardcore" / "Full assistance"); retail adds 288+
  lines (DLC / TDU World). Tables: `C:\TDU2-Proto\loc_us.json`, `loc_real_changes.txt`.
- **The Aug 2011 data is older than retail** despite its date: the Ducati is spelled `Demosdici`, `Buggy1` is B4
  (retail B2), the Zonda C12S Roadster A1 (retail A2), older UI wording. The Dec 2010 car table equals retail.
- **The file names match but the contents can differ — the Audi Q7 Inuit interior is cut content** (third pass,
  prompted by a player who saw it in the Dec 2010 build). Retail `q7_inuit_i.bnk` is the Q7 V12's cabin (the same
  `.3dd` 11,136 / `.3dg` 1,327,664 bytes and the V12's four `q7_v12\maps` textures). The prototypes' (both builds,
  byte-identical) has its own scene (`.3dd` 11,360), geometry and materials (`.2dm` 25,920) and 5 textures: a
  **different `Q7_INT2`** (grey dial faces and surround) and **`WOOD_INUIT`** (wood trim, no retail counterpart),
  plus the V12's `Q7_INT`, `Q7_INT_TS`, `Q7_GLASS_I` (identical pixels). The exterior / rims match retail.
  Decoded textures, the raw bank and a README: `C:\TDU2-Proto\q7_inuit_beta\`; tools: `tools/x360/`.
  **So the earlier "no cut content" conclusion only held for file names**: the content scan (per bank, the
  `.3dd` / `.2dm` sizes and texture count against retail — sizes are platform-invariant for those two) was
  run on 2026-10-29 and found a great deal more: **`docs/re/cut_content_scan.md`** (tools
  `tools/x360/eden_bank.py` + `tools/x360/content_scan.py`). 7,937 banks are flagged by **both**
  prototypes; the highlights are the R8→R8 V10 and Mustang-V6-Premium→GT renames (car banks *and*
  interiors), four prototype-only banks (three rim sets, the V6 Premium low LOD, the Audi Inuit preorder
  clothes), `fx/vfx_car.bnk`'s 9 cut materials, 15 `.bas` in `scenarics/packs/worldlifeset.bnk`,
  `interior/icd_asia.bnk`'s 21 textures, and the island / frontend re-exports. Audio is name-only: a 360
  wav is not byte-identical to the PC one even for the same sound. **Restored in OpenTDU2 (the grey
  dials)**: `tools/x360/restore_texture.py` writes the prototype's
  `Q7_INT2` as a PC `.2db` (retail header as the template, mip 0 only) to `<userdata>/restored/q7_inuit_i/
  q7_int2.2db`, and `TextureCache::loadBankTextures` uses any `<userdata>/restored/<bank>/<texture>` in place of
  the bank's own (`-norestored` = retail) — `docs/images/q7-inuit-restored-dials.webp` (retail black dials | restored
  grey). Game data stays out of the repo: the file is generated on the player's machine. **Restored 2026-10-30
  (the full Inuit cabin, wood trim included)**: `tools/x360/make_q7_inuit.py` packs a hybrid PC bank into the mod
  overlay — the prototype's `.2dm` byte-swapped to the PC layout (`eden_render_file.swap_to_pc`; the material
  hashcodes turn out to be 8-slot identifiers in *memory order* on both platforms — they must not be reversed —
  while the texture-slot u64s and parameter names are real numbers and are), the **retail** scene with its MBNK
  material banks re-pointed by role (`Carbon`→`Wood`, `Paint`→`Inuit`, `C1_L`/`C2_L`→`Leather`, `Roof`/`A_C3_Mo`→
  `Moquette`, `A_C1_Al`→`Aluminium`, `A_C1_P`→`Plastic_White`, `Dot`/`Plastic_Bruit`→`Plastic`) because the
  geometry — also retail — indexes materials in the retail order, and the two prototype-only textures as PC
  `.2db` (`WOOD_INUIT` 256x256 sRGB DXT1, the grey `Q7_INT2`). The first PC bank writer in the repo
  (`tools/x360/build_bank.py`) packs it; round-trips two retail banks byte-comparably (`--roundtrip`).
  Verified in the port: `-gm assetviewer -bank euro/bnk/vehicules/interiors/q7_inuit_i.bnk` renders the cream
  Inuit cabin with the walnut dash — `docs/images/q7-inuit-hybrid-viewer.webp` (retail for comparison:
  `docs/images/q7-inuit-restored-dials.webp`), freeride `-car Q7_Inuit` loads it through the mod overlay.
  **Still not ported**: the prototype's own `.3dg` (packed 360 vertex formats, 0.63x the size) — so the hybrid
  uses retail geometry and a role-based material map, not the prototype's exact per-primitive assignment
  (`tools/x360/eden_3dg.py` reads the primitive material indices on both platforms; the geometries differ,
  20 vs 18 prims on COCK_HR, so there is no per-primitive join). The restored textures' mips (the 360 packed
  mip tail) are not decoded either.
- **Cut cars**: the car table (all three versions) holds the cut **Audi Quattro S1** and **VW Race Touareg** (class
  B1, invalid) and the cut bikes (Ma1 / Ma2: MV Agusta Brutale / F4S, Ducati S4R / Desmosedici / Hypermotard,
  Kawasaki ZX-10R / KX450F / KFX450R, Triumph Speed). Of these only `desmosedici` (and `default_bike`, `fatboy`,
  the Diavel's med / low LODs) has a model — in the PC data and the betas alike. No cut model is in the betas only.

**The rule that follows from this:** pick the source per blocker *type*, not per
feature. There are four types and they have different answers:

| Blocker type | Example open items | Right source |
|---|---|---|
| **A. Code encrypted at rest** | physics sub-functions, ABS/TC tests, Havok IK, sound spatializer, CinScene, cockpit branch | **PS3 static** (360 second) |
| **B. Data not decoded** | minimap roads, radio jingles, zone names, interiors | **PC data files**; console code only says *which* file/field |
| **C. Wrong inference** (`[INF]` laws behaving wrong) | rollover, ocean fresnel, shadow rectangle | **Running 360 recomp + retail PC as an oracle** — measure, don't read |
| **D. Not implemented at all** | casino floor, traffic, online, save games | Scope work; console code shows structure |

The single biggest mistake available here is spending a session on type C with
static analysis, or on type B with console binaries.

---

## 2. The multiplier: a verified [ENC] locator

Built and validated this session. It answers "where is the readable twin of this
encrypted PC code?" — which is the question behind all 233 `[ENC]` markers.

**`C:\OpenTDU2\tools_probe\match_builds.py`** joins the two builds on the strings
they share:

```
PS3 abs_refs.json (addr -> referencing function)  x  PS3 strings.json
                     |
                     joined on string text
                     v
PC TestDrive2.exe  (.rdata strings, absolute-VA immediates scanned in readable .text)
```

Outputs:
* `docs/re/ps3/pc_ps3_anchors.json` — **8,895 anchors**, each a shared string with
  its PC code site and its PS3 function.
* `docs/re/ps3/ps3_enc_twins.json` — **69 PS3 functions** whose strings have **no
  readable PC reference**, i.e. the PC code that references them is inside the
  SecuROM band. These are the readable twins of the encrypted code.

**Validated** `[MEASURED]`: for eight test strings (`CLUTCH`, `ACCELERATOR`,
`onEnterFrame`, `onDragOut`, `blur_ha_fade`, `LateralDamageFactor`, `DBAreaLevel`,
`Pipeline Spatial`) the PC address exists in `.rdata` and the readable `.text` has
**zero** references to it, while the PS3 twin is named. `ABS_Slip_Ratio_HYPERSPORT`
and `PedalPosition` by contrast *do* have readable PC sites, so they are correctly
excluded.

The 69 twins are the highest-value reading list anyone has produced for this
project. The top of it, with the OpenTDU2 item each one serves:

| PS3 twin | Strings (sample) | Serves |
|---|---|---|
| `func_00223200` | `ACCELERATOR`, `CLUTCH`, `BUTTON_14`, `BUTTON_15` | **controller / input actions** |
| `func_00B1C058`, `func_00B1C258` | `onData`, `onDragOut`, `onEnterFrame`, `onDragOver` | **Flash / AVM1 events → pause menu buttons** |
| `func_0017D6E4` | `DBAreaLevel`, `DBBrands`, `DBCarDamageData` | **HUD zone names** ("Ibiza Area 1/2" hardcoded) |
| `func_00348A84` | `blur_dist_att_type`, `blur_ha_fade`, `blur_ha_max_mov/pos` | postfx / **shadow rectangle** |
| `func_003F4204` | `IntervalBreak`, `IntervalDeform`, `LateralDamageFactor` | **car damage** (not implemented) |
| `func_0016A694` | `Pipeline Spatial`, `Pipeline5_1 Buffer`, `PipelineMono Buffer` | **3D sound** / radio routing |
| `func_005DF66C` | `IdAnim`, `RandomDurationToWaitMin/Max`, `RandomSpeedMax` | **PNJ / traffic** |
| `func_00046C8C` | `(ptr->FxResIndex>=0)&&(...)`, `(ratio>=0.f)&&(ratio<=1.f)` | asset / animation manager asserts |
| `func_007597DC`, `func_0075A21C`, `func_0075A590` | `_best_win_in_a_row`, `_money_spent`, `_money_won` | **casino stats** |
| `func_00C875EC` | `AUTOGEN_CLASS_%d`, `CLASS_BOOL`, `CLASS_DOUBLE` | Eden reflection type registry |

To read one: `cd C:\TDU2-PS3-Recomp` then
`python tools/get_func.py 0x223200 --out _g_input.cpp`.

---

## 3. Prioritised work list

Each row is a session-sized task with a first command. Ordered by
(value ÷ effort), not by feature glamour.

### Tier 1 — do these first (unblocked, small, high value)

1. **Input / controller actions** — `[NEXT]`
   `python tools/get_func.py 0x223200 --out _g_input.cpp` in `C:\TDU2-PS3-Recomp`.
   11 strings (`ACCELERATOR`, `CLUTCH`, `BUTTON_14/15`) — this is the action→
   device binding table the PC spec could not read. Feeds `source/input/`.
   *Payoff: the "controller stuff" item.*
2. **Flash / AVM1 event names** — `[NEXT]`
   `get_func.py 0xB1C058` and `0xB1C258` (`onData`, `onDragOut`, `onEnterFrame`,
   `onDragOver`). These are the handler names retail dispatches into the SWF —
   directly the pause menu's button/`onEnterFrame` behaviour that
   `source/flash/flash_player.cpp` approximates.
   *Payoff: pause menu + any interactive Flash screen.*
3. **HUD zone names** — `[NEXT]`
   `get_func.py 0x17D6E4` (`DBAreaLevel`, `DBBrands`). The HUD bug is that zone
   names are Ibiza-hardcoded in `freeride_hud.cpp`; `DBAreaLevel` is the area/zone
   table the retail code reads. Pair with the PC data (`db_data.cpr`, already
   decoded).
   *Payoff: HUD zone badge on hawai/casino.*
4. **Camera blur / postfx params** — `get_func.py 0x348A84` (86 strings:
   `blur_dist_att_type`, `blur_ha_fade`, `blur_ha_max_mov/pos`).
   *Payoff: postfx spec + the shadow-rectangle look.*
5. **3D sound pipeline** — `get_func.py 0x16A694` (`Pipeline Spatial`,
   `Pipeline5_1 Buffer`, `PipelineMono Buffer`). The PC spec says the edAudio
   spatializer is encrypted; this is its twin.
   *Payoff: sound3d spec, radio routing.*

### Tier 2 — the physics path (owner's stated focus)

6. **Read the two candidates located last session** —
   `get_func.py 0x155C20` (contains the whole `FUN_00546xxx` engine / gearbox
   predicate cluster) and `get_func.py 0x15E678` (contains both aids tests
   `FUN_0054eaa0` TC / `FUN_0054eae0` ABS, and references three `.rodata`
   constants). Confirm each **by shape** against
   `vehicle_physics_spec.md` §6.3–§6.6, never by address.
7. **ABS is NOT encryption-blocked — exploit that.** `[MEASURED]`
   `ABSCONTROLACTIVATION` (PC `0xd6cb61`), `ABSCONTROLSLIPRATIO` (`0xd6cbca`),
   `ABSCONTROLVELOCITYMAX/MIN` all have **readable** PC sites, and so do
   `ABS_Slip_Ratio_HYPERSPORT` (`0xe18b16`) and `_abs_slip_ratio_sport`
   (`0xe01f65`). So the aids curve tables and the car-setup DB are readable today
   — read those functions on the PC side before touching the PS3 at all.
8. **Engine / gearbox state machine** — the members are confirmed from the PS3
   serializers (`physics_layouts.md`); the update functions are `0x155C20` +
   the `0x15xxxx` cluster. Port the state machines into
   `source/physics/vehicle_dynamics.cpp`.

### Tier 3 — use the running builds as an oracle (type C blockers)

9. **Rollover / damping / Pacejka feel.** Do **not** reverse these further.
   Instead: run the 360 recomp (`play.bat`) with the file logger, and OpenTDU2
   with `-vehtest -logcorner`, and compare the same manoeuvre. The 360 project
   already has `rexglue-sdk-instrumented-diagnosis.patch` and
   `src\hooks\tdu2_frame_trace.cpp` — the instrumentation pattern exists.
10. **Shadow rectangle + ocean fresnel.** Same method: retail PC screenshot vs
    OpenTDU2 screenshot at a matched camera, using the world viewer for a fixed
    pose. `docs/images/retail-*.png` shows the project already does this.

### Tier 4 — data / format items (console builds do NOT help)

11. Minimap roads (sector `.rd` splines), radio jingle/DJ/ad banks, interior and
    shop logic. These are PC data problems: the files are in
    `C:\TDU2-Gamedata\TestDrive2\Euro\Bnk`. Use the console code only to confirm
    *which* file/field is read, via the 69-twin list (`func_00AB58C0` with
    `%sInterior/iair_b.bnk` etc. is the interior loader).

---

## 4. Traps — each of these cost real time. Do not repeat them.

1. **`.rodata` is inside the FIRST PT_LOAD segment** (`0x10000..0x10A0508`), not
   the second. A "data = segment 2" filter finds **nothing** — every constant
   pool the project queries (`0xf120f0`, `0xfb0000`, `0xf144d0`) is below it.
   Fixing this bound took `abs_refs.py` from 3,256 to 20,871 addresses.
2. **`tools/xref_addr.py` times out** — it re-scans the 1.8 GB lifted tree per
   query. Use `tools/abs_refs.py` (indexed, one pass).
3. **`tools/addr_refs.py` is built on the wrong assumption** — it only looks for
   TOC-relative pairs (`addis rT, r2, HA`), but the build materializes addresses
   **absolutely** (`addis rT, r0, HI` + `addi/ori` or a d-form load). Its output
   (14 KB) is therefore near-empty.
4. **`[DEAD]` The link-order shortcut.** `PS3_VA ≈ PC_VA − 0x3F0000` does not
   hold: tested against all 19,842 PS3 function starts, **no exact match**, deltas
   scattered from `+0x230` to `+0x2750`. Do not spend time on a global offset.
5. **The `0x0Bxxxx–0x0D5xxx` PS3 neighbourhood is *serializers*** (name-emitting
   reflection), not the physics. The code that reads the float constants is a
   second cluster at **`0x15xxxx–0x17xxxx`**.
6. **The scalar pool `0xf11f00–0xf1214c` is reached as `base + offset`** — it has
   no direct materialization and no pointer slot, so a constant xref cannot find
   it. Find the base register instead.
7. **Key-space bug in `match_builds.py`** (fixed, but the shape recurs): the
   `sites` dict is keyed by **PC** address, while PS3 addresses come from
   `abs_refs.json`. Comparing the two silently marked **991** functions as
   orphans when the true number is **69**. Any time two builds are joined, assert
   the key space.
8. **Do not call the `1/6` tables "the RK4".** Two uniform 1/6 grids exist
   (`0xf7a1e4`, `0xf42b40`); 1/6+2/3 adjacent is suggestive but the grid also
   contains 1/3, 1/2, 5/6, so a 6-sample blend fits equally well. Read the
   consumer before believing it.
9. **The 360 recomp's `tdu2_funcs.*.h` are not names** — 108,672 declarations,
   zero Eden identifiers. It gives boundaries only.
10. **The PC install is modded** (TDU World bigfiles, `paul.dll` + `.securom`
    section present). It is fine as a *data* source and for readable code, but not
    as a clean behavioural baseline — for that use a stock install or the 360
    recomp.

---

## 5. Division of labour for two agents at 4am

Both work in `C:\OpenTDU2` on branch `win` (per `WORKSPACES.md`), or clone a
second tree if they must run concurrently — `tdu_instance.cpp`, `shared.h` and
`CMakeLists.txt` are the shared spine and must be serialised.

| | Agent A (physics) | Agent B (UI / Flash) |
|---|---|---|
| First | Tier 2 item 7 — read the **readable** PC ABS tables (`0xd6cb61`, `0xe18b16`) | Tier 1 item 2 — `get_func.py 0xB1C058` / `0xB1C258`, AVM1 events |
| Then | Tier 2 item 6 — `0x155C20` / `0x15E678` by shape | Tier 1 item 3 — `0x17D6E4`, `DBAreaLevel` → HUD zone names |
| Then | Tier 2 item 8 — port the state machines into `source/physics/` | Walk `docs/re/ps3/flash_globals.txt` (1,710 `/:` names) against `source/flash/` |
| Deliverable | a `[CODE]`-tagged physics spec section replacing `[ENC]` | the pause-menu / HUD wiring diffs |

Both: **update `docs/STATE.md` before running out of usage**, commit often, and
add every new finding to the `docs/re/ps3/` catalogs rather than in a scratch file.

**Do not** put both agents on the same file, and do not let either one start
Tier 3 (behavioural comparison) — it needs a running game and is best left to a
single focused pass.



