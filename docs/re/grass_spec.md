# Tall grass — the ground-cover billboard layer

Scope: the retail tall-grass system — the grass tuft cards that cover the
meadows, crop fields and roadsides of the three islands (`TALLGRASS`). This is
*not* the `.pmi` vegetation (trees / bushes / flowers, `pmi_spec.md`, drawn
since session 88) and *not* the terrain detail layers (`TD_HeightMap`
`DETAIL1/2/3`). Every fact here is measured from the shipped data unless
tagged `[INF]` (inferred) or `[ENC]` (lives in the SecuROM band; PS3 route
noted). The `.flg` file is **not** grass — it is the sector minimap
(`formats.md`); the session-24 "grass grid" reading of it is dead.

## 1. The data

### 1.1 `islands/<island>/fx/tallgrass.bnk` `[DATA]`

Loaded by the retail path literal `"%sIslands\%s\FX\tallgrass.bnk"`
(`pc_ps3_anchors.json` 15928056 / PC 15692052). Ibiza bank 831 488 B, Hawai
399 360 B, Casino 311 296 B. Contents (Ibiza, extracted with `otdu2_bnk_tool`):

| Asset | What it is |
|---|---|
| `tallgrass.3dd` / `.3dg` | the grass card mesh: **two identical geoms** `PLANE_01` / `PLANE_02` (hashes `31305f454e414c50` / `3230...`, ascii "PLANE_01/02"), each **one 4-vertex horizontal quad** ±30 m in X/Z at Y = 0 (60 × 60 m), normals +Y, triangle strip (`PRIM type 0x0004`, no indices), layout `0x1040` = position + UV in stream 0 (stride 20), normal (4×f16) in stream 1. The 3dd hangs both planes under scaled nodes (X 0.5, Y 1, Z 0.25). The authored size is a template: the runtime builds per-tuft cards itself (the per-edge law, §3), so only the quad *shape* and UV mapping matter. |
| `tallgrass.2dm` | two materials, **`setgrass`** and **`setbush`** — their ascii names are their hashes (`0x5353415247544553`, `0x0048535542544553`). Both: COLOR slot → the matching `.2db` atlas, `FADEALFA` float4 param (the distance fade), states `0x02000101 0x02000100 0x00010605` (alpha-tested, cull none), and `ShaderSetGUID = 0x73078865ca37336d` at MAT +0x300 — **`kTD_Gouraud`**, rewritten to the TD_TallGrass family by the retail handler table (§4). |
| `setgrass.2db` | 2048 × 128 DXT5 (sRGB, format 55) atlas of **9 grass tuft cards** — side-view clumps (blades fanned up, alpha-faded at the roots): green grass, white flowers, dry grass, lavender sprigs, golden grass, blue flax, lavender bush, yellow broom, green variant. |
| `setbush.2db` | 1024 × 256 DXT5 atlas of **4 bush cards** (256² each). |
| `noise.2db` | 64² DXT1 noise texture, hash ascii `NOISE` — bound by the shader's own sampler from the owning bank. |
| `noise.rle` | 124 068 B RLE payload — the wind animation input (pairs with the weather params `wind_continuous_grass_amplitude/frequency`, `wind_whirl_grass_twist_speed`, §3). Not decoded yet. |

### 1.2 `islands/<island>/fx/grasstypes.xml` `[DATA]`

Path literal `"%sislands/%s/fx/grasstypes.xml"`. 9 914 B (Ibiza). **The shipped
file is malformed** — it ends mid-tag (`</GrassType` without `>`, line 99); a
strict XML parser rejects it, the retail loader evidently tolerates it. 24
`GrassType` entries, document order (the order the runtime array holds — the
`.pgr` cell value indexes THIS order, §2):

| # | Id | Name | Grass bitmap | Height | Width | Notes |
|---|---|---|---|---|---|---|
| 0 | 4 | Garrigue | 5 (var 8, p 0.8) | 0.63 | 2.12 | base scrub |
| 1 | 16 | Garrigue_OffRoad_3Small | 0 | 0.42 | 1.54 | roadside |
| 2 | 32 | PinsSauvages | 0 (var 7, p 0.15) | 0.67 | 2.21 | wild pines scrub |
| 3 | 48 | PinsSauvages_OffRoad_3Small | 0 | 0.46 | 1.87 | roadside |
| 4 | 64 | Feuillus | 8 (var 6, p 0.2) | 0.69 | 2.19 | deciduous floor |
| 5 | 80 | Feuillus_OffRoad_3Small | 0 | 0.45 | 1.92 | roadside |
| 6 | 96 | F-HautsPins-TR | 0 | 0.82 | 2.21 | high pines |
| 7 | 112 | F-HautsPins-TR_OffRoad_3Small | 0 | 0.33 | 1.92 | roadside |
| 8 | 128 | Garrigue_Blend | 2 (var 0, p 0.25) | 0.59 | 2.33 | blend variant |
| 9 | 144 | PinsSauvages_Blend | 3 (var 0, p 0.25) | 1.04 | 2.23 | |
| 10 | 160 | Feuillus_Blend | 1 (var 0, p 0.25) | 0.47 | 1.99 | |
| 11 | 176 | Desert | 2 | 0.65 | 2.04 | bush 3 |
| 12 | 192 | Thym - Champs | 5 | 0.67 | 2.12 | NoShadow=1 |
| 13 | 208 | Colza - Champs | 7 | 0.88 | 2.18 | NoShadow=1, rapeseed |
| 14 | 224 | Blé - Champs | 4 | 1.00 | 1.89 | NoShadow=1, wheat |
| 15 | 240 | Lavande - Champs | 6 | 1.00 | 2.15 | NoShadow=1, lavender |
| 16 | 12 | Garrigue_OffRoad_2Medium | 0 | 0.46 | 1.73 | |
| 17 | 8 | Garrigue_OffRoad_1High | 0 | 0.51 | 1.92 | |
| 18 | 44 | PinsSauvages_OffRoad_2Medium | 0 | 0.56 | 2.07 | |
| 19 | 40 | PinsSauvages_OffRoad_1High | 0 | 0.63 | 2.14 | |
| 20 | 76 | Feuillus_OffRoad_2Medium | 0 | 0.59 | 2.11 | |
| 21 | 72 | Feuillus_OffRoad_1High | 0 | 0.74 | 2.31 | |
| 22 | 108 | F-HautsPins-TR_OffRoad_2Medium | 0 | 0.48 | 2.02 | |
| 23 | 104 | F-HautsPins-TR_OffRoad_1High | 0 | 0.63 | 2.11 | |

Every entry: `Grass` + optional `Bush` sub-element (`BitmapIndex`,
`BlendMode`, `Height`, `Width`, `SizeDelta`), and on `Grass`:
`TypeVariation` + `VariationProbability` (roll a second bitmap), `BlendMode`
(1/2), `UseNoiseScale` + `ScalePower` + `ScaleClamp` (noise-driven size law),
`NoShadow`, `BlendColor`. Card sizes: **Width 1.5–2.3 m, Height 0.33–1.0 m**
— wide, short tufts. The `_OffRoad_1High → _3Small` ladder per family is the
roadside-distance variant set (grass shortened toward roads). `fx.ini
[GRASS]`: `NBGRASSBITMAPS 9`, `NBBUSHBITMAPS 4` (matches the atlases).

## 2. The `.pgr` grid — per-sector grass placement `[DATA]`

Every sector bank carries `Sector-<id>.pgr` (Ibiza samples 29–92 KB; the
archive has 5 extracted: `_flg/`). Container (same family as `.flg`):

```
0x00  u32 1, u32 0, u32 fileSize
0x0c  magic "RLE."
0x10  section "DATA"
0x14  u32 1 (version), u32 payloadSize, u32 0
0x20  payload:
      u16 gridW = 1024, u16 gridH = 1024      (u32 log2 = 10 at +4)
0x28  gridH × u32  row offsets (absolute file offsets)
      rows: RLE runs of (u8 count, u8 value); (00,00) pairs are no-ops —
      skip them; a run may overshoot the row end (clamp at gridW cells)
```

1024² cells over the 768 m sector = **0.75 m per cell**. Decoded
1024/1024 rows on all five samples (`tools_probe/grass_probe.py --render`);
`tools_probe/grass_probe.py` is the decoder + renderer.

Value semantics — measured histograms vs `grasstypes.xml`:

* `0xff` = no grass (sentinel; 268 k cells in the farm sector, dominant in none)
* `0x00` = none / bare (dominant in the arid airport sector 3-3-2-4, ~98 %)
* `0x02`, `0x04`, `0x06`, `0x0b`, `0x0d`, `0x0e`, `0x0f`, `0x10`, `0x18` =
  **grass class ids, mapped through the §1.2 table by document order**
  `[INF]` — evidence: the farm sector 3-4-1-0 is dominated by `0x02`
  (PinsSauvages scrub) with `0x0d/0x0e/0x0f` (Colza / Blé / Lavande) in
  patches exactly where crop fields belong, and the arid sector is ~all
  `0x00`. The mapping is verified visually in-engine (§6), not yet from code.
* stray values `0x59 / 0x5f / 0x60` (3-3-2-4, along one full-width row) and a
  full row of `0x9d` (3-3-3-3, row 391) — **road / path markers** `[INF]`
  (matches the exe anchor `GRASSASTARMAC`: grass interacts with the path
  network; the `_OffRoad_*` ladder is the roadside response). One full-width
  feature row per sector is consistent with a road crossing it.
* grid orientation vs the sector origin is **not settled from the files
  alone** [INF] — the per-sector colour maps look area-level (the four
  `sector-3-3-x-x-c.2db` are the same size), which broke the static
  correlation tests. Resolved in-engine: the sector origin is known where the
  grid is consumed, and a wrong flip/transpose is immediately visible against
  a retail capture.

## 3. The retail code anchors `[CODE]`

From `pc_ps3_anchors.json` (PC address → PS3 twin function):

| Anchor | PC addr | PS3 func |
|---|---|---|
| `GRASSENABLED` | 15641196 | 2162164 |
| `NBGRASSBITMAPS` | 15640764 | 2162164 |
| `MAX_GRASS_BILLBOARD_PER_EDGE` | 15643568 | 2162164 |
| `GRASSASTARMAC` | 15637672 | 2162164 |
| `TALL_GRASS_MAX_PER_EDGE` | 15829940 | **7211508** (3 sites) |
| `TallGrassBlock` | 15911264 | — |
| `SETGRASS` / `SETBUSH` | 15911112 / 15911124 | 9943520 |
| grasstypes.xml path | 15692084 | **4348128** |
| tallgrass.bnk path | 15692052 | **4352584** |
| wind params | `wind_continuous_grass_amplitude/frequency`, `wind_whirl_grass_twist_speed` | — |

The per-edge names (`TALL_GRASS_MAX_PER_EDGE`, `MAX_GRASS_BILLBOARD_PER_EDGE`)
say the retail law generates grass **per terrain-mesh edge**, capped, not one
card per grid cell. The PS3 twins are readable (`docs/re/ps3/README.md`,
`get_func.py 0x<addr>` — the functions above), but the lifted C++ is
register-level PPC; decoding the full generation law (cell → edge sampling,
the A* interaction, the noise-scale application, wind) is **deferred** — the
first read of `func_0x6e05b4`'s constructor shows config fields (0x3E8 = 1000,
0x32 = 50) but not the law. What the port implements below is the port-native
law tagged `[INF]`, structured so the retail law can replace it.

## 4. The shader `[CODE]`

`Euro/Shaders/TD_TallGrass.sob` — ShaderSet **`0xcafe8deca8c8caca`**, 32
permutations (8 VS + 8 PS). The material declares `kTD_Gouraud`
(`0x73078865ca37336d`, MAT +0x300); the retail handler table rewrites it to
the TallGrass family (same pattern as `37078865c0ca336e → TD_Vegetation`,
`title_scene.md`). The permutation a material resolves is `HashSpec` of its
48 spec bytes (2dm +0x310; byte 0 = the runtime texture count); HashSpec is
linear over GF(2), so the byte selector that reaches a shipped permutation
was solved and verified (`_grass/solve_spec3.py`, the method
`constant_pools.md` 4): bytes 1-8 carry an 8-byte selector, the rest match the
authored spec. The port resolves `TD_TallGrass vs0/ps0` with it (verified in
the log).

### 4.1 The lifted VS (`vs_0`, session 81) `[CODE]`

The bytecode is stock SM1; `tools_probe/dxso_dis.py` disassembles it (its
first draft numbered the opcodes from a half-remembered table and dropped
`SUB`, shifting every mnemonic from index 3 up — the fixed table follows the
vendored DXVK decoder, which is also what compiles these files at runtime)
and `tools_probe/dxso_lift.py` transcribes it to SSA with the CTAB struct
member names substituted (the full transcript: `_grass/vs0_lift.txt`).

Constant blocks (`CTAB`, all `set=2`):

| Registers | Block | Members |
|---|---|---|
| c0-c8 | `Var_Frame_VS` | `UserVector0`/`1` runtime (c0.x = the wind strength, c1 = the light gradient scale); `UserVector2-6` are def-inlined literals (the wind phases: c2 = (-0.4, 2.857, -0.5, 3.333), c3 = (0.1, 0.05, 0.1592 = 1/2π, 0.5), c4 = (6.283 = 2π, -3.142, 3.0, 0.004), c5 = (0.001302 = 1/768, -0.3, 0, 0), c6 = (1, 0, 2, -1)); `Time_` = c8 (unused by vs_0 — the wind clock rides in through `UserVector0`) |
| c9-c55 | `Var_Scene_VS` | the standard scene block (`ViewProj`, `View`, `Proj`, `LastView`, `InvViewProj`, **`InvView` c29-32**, `InvProj`, `FogColor`/`FogConfig`, the shadow block, `Shadow_SlicesDist` c54) |
| c60-c81 | `Var_VS` | `__World_View__` c60-63, `__Inv_World_View__` c64-67, `__World__` c68-71, `__DirLight_Dir_0` c72, `__AmbLight_Ambient_0` c73, `__DirLight_Diffuse_0` c74, **`alphaDist` c75** (the `.LPHADIS` of the first session: fade start / range / wind phase offset / sway amplitude), **`scaleDist` c76** (the `.CALEDIS`: the car-flatten falloff start / range / scale / -), **`sectOff` c77** (the sector origin for the noise uv), **`CarPos` c78**, **`CarFwd` c79**, **`CarRight` c80** (`.w` = the car-flatten radii), `Whirlwind` c81 (only vs_4-7 read it — the whirlwind variants; vs_0-3 are the plain family; no permutation reads `Time_`) |

**The draw runs with `World = identity`.** The law composes `InvView` with
`__World_View__` around the local corner (the final view position is
`World_View·(corner + InvView·delta)`, which only collapses back to
`viewPos + delta` for World = I), so the vertices carry **absolute world
positions**.

Vertex packing (what the CPU stream must carry):

* `v0` (position): the corner's world X/Z; **Y is authored 0** — the ground
  height travels in `v2.y`.
* `v1` (color): `x` = the corner u across the card (0/1; the shader maps it
  to ±1), `y` = the height fraction (0 root / 1 tip; also the atlas V as
  `1 - y`, roots at the image bottom), `z`/`w` = a hemisphere-encoded normal
  (`(z·2-1, sqrt(1-u²-v²), w·2-1)`) — the card's face normal, feeding the
  diffuse term.
* `v2` (texcoord): `x` = the atlas u (`+ v1.x·sectOff.y` — sectOff.y = 0 in
  practice), `y` = the **Y addend**: the corner renders at
  `v0.y + v3.x + v2.y`, so the ground height plus the corner height minus
  the lever lands here.
* `v3` (tangent): `x` = the blade-height **lever** — it drives the bend
  terms below AND adds into the corner Y; `y` = the per-card wind amplitude;
  `z` = the noise amount the PS lerps the noise tint with; `w` unused.

The position law (transcribed from the lift; everything renders through
`InvView`/`__World_View__` round-trips):

1. **The hemisphere normal** from `v1.zw` → view-space xy via
   `__World_View__` rows 0/1, and the diffuse factor
   `max(dot(normalize(H), -DirLight_Dir_0), 0)` → `oT5` (color0) as
   `diffuse·(DirDiffuse - Ambient)`.
2. **A view-X shear**: `±1/sqrt(2)` (the sign of the normal's view-X)
   scaled by `(2·v1.y - 1)·(v0.y + v3.x)` — tips lean one way, roots the
   other — un-projected to world by `InvView` and added.
3. **The car flatten**: `t = max(-0.5 - dot(CarPos-corner, CarRight)·CarRight.w, -0.4 - dot(CarPos-corner, CarFwd)·CarFwd.w)·(3.333 | 2.857) + 0.1`;
   its deltas replace the delta-vector's view Y slot and push the final view
   Z by `∓3·delta·sign(...)` — **per-corner magnitudes differ between root
   and tip by `2·(v0.y + v3.x)`, which shears the card in depth**, so the
   port ships the lever at 1/8 of the blade height (below). `t` also scales
   `oT2` (texcoord1) = the PS's light-gradient factor.
4. **The wind**: phase = `frac((v0.x - v0.z + alphaDist.z)/2π + 0.5)·2π - π`;
   sway = `(alphaDist.w·sin(phase) + 0.5)·UserVector0.x²·v1.y` added into
   view x/z (tips only) — plus a `∓3·delta` view-Z push like the flatten.
5. **Distance**: the view-space length of the corner drives `alphaDist`
   (`t = 1 - (len - alphaDist.x)/alphaDist.y`, a plain rcp — the fade),
   `scaleDist` (the same shape, scaling the flatten deltas), the fog factor
   (`oT6`), and the pop: `oT0.xyz *= (t >= 0.5)` — cards past the fade
   midpoint collapse to a degenerate point.
6. **Outputs**: `oT0` = position (Proj·viewPos), `oT1` = texcoord0
   (atlas u, `max(1 - v1.y, 0.004)`, and `(worldXZ - sectOff.xz)/768` — the
   sector uv, from the *un-bent* corner via `__Inv_World_View__`),
   `oT2` = texcoord1 (the light gradient), `oT3` = texcoord5 (the noise
   amount, the fade `t`), `oT5`/`oT6` = the light color and the fog.

### 4.2 The PS (`ps_0`) `[CODE]`

`_grass/ps0_lift.txt`. Samplers: s0 = the grass atlas (v0.xy), s1 = the
NOISE texture (v0.zw = the sector uv!), s15 = the screen shadow buffer
(sampled at `(0.5 + vPos)·RenderTargetSize` — vPos is declared with the
`position` semantic, `MiscType` register). Final color:

```
alpha = atlas.a · oT3.y (the fade t)
tint  = lerp(noise, 1, oT3.x (the noise amount))
color = atlas.rgb · tint · ((Ambient + oT5) · oT2.x + decode(s15.g, s15.b))
oC0   = color · (1 - fog) + FogColor · fog
```

**The shadow decode reads the buffer's G/B channels** (packed additional
lighting per the `pShadowRealTime` fallback's comment) — *not* the R sun
visibility `TD_Vegetation`'s PS uses; with the port's buffer those channels
are 0, so the shadow add-in is inert and the grass brightness rides on the
`(Ambient + oT5) · oT2.x` term. `alphaDist`/`scaleDist`/`CarPos`/`CarFwd`/
`CarRight`/`sectOff`/`UserVector*` ship **no defaults in the `.2dm`** (only
`FADEALFA`) — they are runtime-fed, values chosen by the port [INF].

## 5. The card model `[DATA]` + `[INF]`

The template's two `PLANE_*` quads are horizontal ground cards; the runtime
tuft card is a **vertical crossed pair** sized `Width × Height` from the
GrassType, UV-mapped to the type's atlas cell (9 cells over the 2048×128
strip, `u = [i/9, (i+1)/9]` `[INF]`), alpha-blended with the roots at v = 1
(the atlases fade them at the image bottom), `FADEALFA` = the distance fade
the retail handler drives per frame (the `.2dm` ships it 0 — transparent).
Verified in-engine: crossed vertical pairs at the GrassType sizes draw
correctly placed, sized and blended under the plain Gouraud shader
(`docs/images/grass_s84_on.webp`).

## 6. Implementation (this port)

* `EdenWorld::initialize` → `loadGrassAssets`: `grasstypes.xml` (tolerant
  attribute scan — the file is malformed) and `fx/tallgrass.bnk` (the card
  template + atlases; materials' `FADEALFA` forced to 1).
* `EdenWorld::loadSector` keeps the sector's `.pgr` payload compressed.
* `EdenWorld::drawGrass` (flushed after `drawImpostors`, every pass): the
  grid rows around the camera decode on the fly (the `.pgr` RLE, §2), one
  crossed-pair card per sampling cell (`-grassstride`, default 3 = 2.25 m)
  with hash jitter, Y from the heightfield, type = the cell value through
  the §1.2 document-order table `[INF]`, size from the GrassType law, atlas
  cell + variation roll from the GrassType. Transient upload, one draw per
  frame (the impostor-batch mechanics). Physics untouched.
* Two draw paths: the default renders the declared `kTD_Gouraud` with the
  template layout (5-float corners + f16 normals, session 81's baseline);
  **`-grasstallgrass` resolves `TD_TallGrass`** (the §4 spec-byte selector)
  and emits the §4.1 packing — one 64-byte stream (position/color/texcoord/
  tangent, 4 × f32), `World = identity`, and the per-draw constant feed
  (exact CTAB member names, case-sensitive):
  `CarPos` = the camera, `CarFwd`/`CarRight` = the camera basis with
  `.w = 0.12` (the flatten radii), `sectOff` = the camera's sector origin,
  `alphaDist` = `(dist, 2·dist, 0, 0.3)` — keeps `t ∈ [1, 1.5]`: the pop
  never fires inside the radius and the light gradient lands tips in
  `[1, 2]`, roots in `[-1, 0]` — `scaleDist` = `(0.3·dist, 0.6·dist, 0, 0)`
  (the flatten falloff inert at scale 0), `UserVector0` = `(0.7, 0, 0, 0)`
  (the wind strength), `UserVector1` = `(1, 1, 0, 0)`. The blade-height
  lever ships at **1/8 of the card height** — the law's root/tip depth
  shear is `2·lever`, which at full height tore the cards into streaks
  (measured: `_gtg_on2` iteration). The noise amount (v3.z) = 0.35; the
  noise texture's mean luminance is 0.392, so the tint averages ~0.6.
  Verified: `docs/images/grass_tallgrass_s81.webp` vs the Gouraud path at
  the same spot `grass_tallgrass_off_s81.webp` (farm sector,
  `-tpin 19584,24960 -clock 12`); validation layer 0 VUIDs during the
  frames (only the pre-existing shutdown `vkDestroyDevice` notice).
* **The road-cover suppression (the poke-through fix):** every sector
  indexes its paved surfaces at load (`BuildRoadIndex` — the carve-ground
  mechanics: items whose material resolves `kRoadShaderSet` **or** the
  architecture family `183157b81c6cff57` ('Village_Bandero',
  'SF_Wall_Stone', 'FU_TractA'... — the material census under `-loggrass`
  lists them), up-facing triangles only, 8 m XZ buckets over the
  sector-local collision soup). The emitter asks `SampleRoadSurface`
  whether a paved surface lies within `cardHeight + 0.25 m` above the
  heightfield (tolerance `-roadskiptol` below it, default 0.05 m) at the
  card's spot and skips the card when it does — the roads ride above the
  sheet, so a card rooted under them poked through the tarmac (the owner's
  report from the first Ferrari California test drive). Cards well below a
  road surface (under a bridge deck) stay; walls and roofs are filtered by
  the up-facing rule and the height window. `-noroadskip` is the A/B;
  `-loggrass` counts the skips. Roads arriving as `.pmi` instances (some
  parking lots) are not indexed; a card may still overlap a road edge by up
  to `w/2` — the suppression tests the card centre only.
* **The road-graph suppression (2026-11-10, the village cobble fix):** the
  village streets and plazas are **terrain paint** — no road or
  architecture geometry the index could find (the sector `-o` census proves
  it: only walls / buildings carry collision there). The suppression now
  also asks the **ROAD_MAP graph** (the `<Island>_ROAD_MAP.IA` /
  `_ROAD_DETAIL.IA` network the map screen and the traffic parse — the
  GRASSASTARMAC anchor's "grass interacts with the path network", §3):
  every road polyline segment goes into an XZ segment hash
  (`EdenWorld::GrassRoadIndex`, built once per island from the game mode),
  and a card within `-grassroadwidth` (default 5.5 m) of a centreline is
  skipped. The first-test-drive village renders clean cobbles
  (`docs/images/grass_village_cobble_before/after.png`); the farm keeps its
  density (20000 cards CAPPED, 685 graph-skipped at the farm teleport).
  Junction plazas wider than ~11 m still take grass in their middle — the
  width is a feel dial, not a pavement mask.
* **The out-of-class `.pgr` skip (2026-11-10):** the grid's values beyond
  the 24 `grasstypes.xml` classes used to clamp onto the last type and
  render — including the road / path-marker-looking cells (§2's `0x59 /
  0x5f / 0x60 / 0x9d` here, `0x7b / 0x7c` in sector-5-0-6-7). They skip
  now (187-188 cells at the village spawn), counted as "path-marker cells
  skipped" in `-loggrass`.
* **One emit per frame:** every pass that draws the world (the prepass,
  the shadow casters, the ocean mirror, the environment cube faces, the
  main pass) used to re-walk the grids and re-upload the batch — the emit
  cost ~4-8x per frame. `drawGrass` now re-draws the cached bindings for
  the pass cameras and rebuilds only when the frame rolled over or the
  pass camera moved > 2 m (the transient allocations live for the whole
  frame).
* **Near-first scan + denser defaults:** the sectors sort by their
  centre's distance to the camera and every row / column scan alternates
  outward from the camera's lattice lines, so the card cap truncates the
  faded far ring instead of an arbitrary list-order band — and the
  road-covered cells near the camera are always among the visited ones
  (before this, a capped dense sector never reached the road cells at
  all). The defaults went to stride 2 (1.5 m spacing, was 3) and a
  20000-card cap (was 9000): the scene CPU is ~flat in the card count now
  (measured, `-logscenetimes` at the farm teleport: 9000 capped vs 20000
  capped both ~10-13 ms total scene record). Evidence: the density A/B
  `docs/images/grass_density_before.webp` / `grass_density_after.webp`
  (same spot, old vs new defaults), the road A/Bs
  `grass_road_highway_off/on.webp` (the autovia embankment) and
  `grass_road_ferrari_off/on.webp` (the dealer road: 4957 cards + 206
  road-skipped with the skip, 5163 without — the counts reconcile).
* Flags: `-nograss` (off), `-grassdist <m>` (card radius, default 140),
  `-grassmax <n>` (per-frame cap, default 20000), `-grassstride <n>`
  (default 2), `-grassflip` (grid row order A/B), `-loggrass` (per-frame
  counts + road skips), `-grasstallgrass` (the TD_TallGrass path),
  `-noroadskip` (the road suppression A/B). Grid orientation vs the
  sector origin is still `[INF]` (world axes = grid axes; `-grassflip` is
  the A/B).

## 7. Open

* The car flatten is effectively inert in the port feed (the lever at 1/8
  and `scaleDist.z = 0` tame its depth shear); recovering the retail
  behaviour needs the per-frame `CarFwd.w`/`CarRight.w` law [ENC-adjacent —
  the PS3 twins of the grass generator are the route].
* The wind phase runs spatially (`sin` of world x - z) but carries no time
  — `UserVector0` is constant; wiring `wind_continuous_grass_amplitude` /
  `frequency` (and `noise.rle`, still undecoded) into it is the next
  increment.
* The PS's G/B shadow decode: what writes those channels retail-side
  (packed additional lighting — street lamps?) is untraced; the port's
  shadow buffer leaves them 0.
* Grid orientation + the road markers (`GRASSASTARMAC` interaction,
  `_OffRoad_*` ladder) — needs a retail A/B. The road *surfaces* are
  handled now (§6: the poke-through suppression + the ROAD_MAP graph
  width), but the roadside grass-shrinking ladder is not: a card can still
  overlap a road edge by up to `w/2` (centre-only test), and junction
  plazas wider than ~11 m still take grass in their middle.
* **The `.pgr` row-offset table is not settled (2026-11-10 deep probe).**
  Both probed sectors' tables are index-shuffled (~50 % ascending steps)
  and each carries one `0x1008` entry pointing back into the table itself.
  The table-order decode is organic on the farm sector (visually validated
  earlier) but produces full-width uniform bands on sector-5-0-6-7 (the
  first-test-drive village): whole rows of one value, class strips crossing
  the plaza — the bands the owner sees as grass on the streets. Sorted
  storage order, reversal and 10-bit row bit-reversal all score the same
  or worse on adjacent-row coherence (the metric saturates: one class
  holds > 50 % of both sectors' cells). Hypotheses not yet exhausted: the
  `(0, x)` RLE pairs as escapes (skipping them loses exactly the cells
  needed for 1024 rows: farm 97,280 short / 4,955 pairs ≈ 19.6 cells per
  pair; s5067 15,807 / 709 ≈ 22.3), a per-bank table variant, or the grid
  not being a terrain map at all (the retail anchors point at per-EDGE
  billboards: `MAX_GRASS_BILLBOARD_PER_EDGE`, `GRASSASTARMAC` — grass
  along the path graph, not over the terrain). The port's placement keeps
  the table-order decode + today's three suppressions; a real fix wants
  the PS3 twins or a retail capture A/B.
* Atlas cell layout precision (9 cells over 2048 px) and the `Bush` rule.
* Density/size tuning against a retail capture — the port-native defaults
  are stride 2 / 20000 cards (§6, the once-per-frame emit made the raise
  free); what the retail per-edge law actually generates
  (`TALL_GRASS_MAX_PER_EDGE`) is still unknown. Perf measured (s81,
  frame-240 steady state, farm teleport, 1280x720, the old per-pass emit):
  on = 16.8 ms, `-nograss` = 8.2 ms, `-grassdist 60` (2000 cards) =
  10.0 ms - ~0.9 us per card, linear: the radius/cap flags are the lever.
  After the once-per-frame cache the scene record is ~flat in the card
  count (§6).
* The whirlwind family (vs_4-7 reads `Whirlwind` c81) is unexplored — the
  spec-byte selector for those permutations, and what feeds it.

## 8. The pale blade bases (2026-11-13, owner: "grass has odd white parts at the bottom")

* `[DATA]` The `setgrass.2db` / `setbush.2db` atlases are authored with pale,
  silvery dry-stalk bases: the alpha-weighted mean RGB of the card rows climbs
  from ~(100,95,50) near the tips to ~(136,136,125) at the bottom row.
* `[CODE]` The retail `TD_TallGrass` PS multiplies the colour by
  `(Ambient + oT5) * oT2.x`, and `oT2.x` is the VS light gradient: tips land in
  [1, 2], roots in [-1, 0] (section 4.1/4.2) - the retail roots are in shade, the
  pale bases never show. The port's default draw is plain Gouraud (no gradient),
  so the bases rendered as bright white fringes (`-grasstallgrass` darkens them but
  still shears the cards near the camera, section 7).
* Fix: `TextureCache::loadBankTextures` bakes the same gradient into the DXT5
  colour endpoints of the two tallgrass atlases (all mips, alpha untouched; a
  smoothstep from 35 % of the card height to the bottom, bottom scale 0.5
  `[INF: the gradient shape and the 0.5]`). `-grassrootdark <s>` sets the bottom
  scale (1.0 = off, the A/B), `-nograssroots` disables it.
* Evidence (build 21, headless, `-tpin 19584,24960 -clock 12`, 0 VUIDs):
  `docs/images/grass_roots_before_after.webp` (top `-grassrootdark 1.0`, bottom the
  new default); the default spawn village is unchanged.

## 9. Card extent vs paths (2026-11-13, owner: "grass still sometimes is on pathways")

* The road-surface and road-graph suppressions tested only the card CENTRE, so a
  card rooted beside a road edge / plaza reached over the tarmac or cobbles by up
  to w/2 (cards are 1.5-2.3 m wide). They now test the centre AND the four ends of
  the two crossed quads (half a card width out). `-grasscentreonly` = the old test.
* Evidence (`-loggrass`, same spots, build 23): village spawn road-skips 374 -> 418
  (4838 -> 4738 cards); the road spot at `-tpin 21353,20172` 902 -> 998. The tufts
  that overhung the plaza edge beside the flowerbed are gone
  (`docs/images/grass_card_extent_village_before_after.webp`, left old, right new).
* Still open `[INF]`: off-graph dirt tracks / forest paths that are terrain paint
  are not detected (the grid orientation / decode is unsettled, section 7; the
  terrain colour-map layers are GPU-only and not read back). `-loggrassroads` logs
  sample road points for `-tpin` probes.

## 10. THE `.pgr` DECODE IS SOLVED (2026-11-13) - sections 2 and 7 above are superseded

* **Row offsets are relative to the payload start (file `0x20`)**, not the file start. `[DATA]` The sorted
  offsets begin at 4104 = `0x1008`, exactly the table end (`0x1028`) minus `0x20` - the "`0x1008` entry that
  points back into the table" of the earlier probe was the row-0-of-the-data offset read 32 bytes early.
* **A run is `count + 1` cells** (a `(0, v)` pair is ONE cell, not a no-op). `[DATA]` Every one of the 1024
  rows of the farm sector sums to exactly 1024 with this law (before: 949 of 1024 rows were short, by 10-30
  cells - the "(0,x) escape" puzzle of section 7), and the row-table order is NOT shuffled - the earlier
  "index-shuffled" reading was the same 32-byte misalignment.
* Proof by image (`_sx/probe2.py`-style decode vs `sector-3-4-1-0-c.2db`): the decoded grid reproduces the
  sector colour map - the dirt tracks and roads are `0xff` (no grass), the building pads are `0xff` with a
  class-2 halo, the golden wheat field is class 14 (`Blé - Champs`), the yellow-green field class 13
  (`Colza - Champs`) - so the document-order class table of section 1.2 is confirmed for those classes.
  `0xff` = no grass, `0x00` = bare. The grid axes are the world axes with row 0 at the sector origin: the
  road graph lands on `0xff` cells with the default orientation (road-skipped 1, graph-skipped 0 at the farm
  teleport) and on grass with `-grassflip` (799 / 719).
* The "road / path markers" `0x59 / 0x5f / 0x60 / 0x9d` / `0x7b / 0x7c` were decode garbage, not data.
* Engine: `drawGrass` decodes with the corrected law (`-pgrlegacy` = the old reading, the A/B);
  `tools_probe/grass_probe.py` too. With the real grid the village streets / plazas / dirt tracks carry no
  grass by themselves (`docs/images/grass_pgr_decode_village_before_after.webp`: top the legacy decode with
  the suppressions off, bottom the fixed decode with them off) - the road-surface / road-graph / card-extent
  tests of section 6/9 are now belt-and-braces, not the primary mechanism.
* Open: the `_OffRoad_*` height ladder is in the grid (classes 1,3,5,7,16-23) and drawn through the
  GrassType table as before; grass density / the per-edge law (`MAX_GRASS_BILLBOARD_PER_EDGE`) is still `[ENC]`.

## 11. Density law, emit cost (2026-11-13, owner: "yes, tune it")

* With the decoded grid (section 10) the emitter walks every 0.75 m cell (`-grassstride` defaults to 1) and keeps
  a cell with a probability instead of a fixed 1.5 m lattice `[INF: the retail per-edge law is [ENC]]`:
  ordinary classes `0.65 -> 0.18` over 10-60 m, minus 0.06 out to 140 m; the crop-field classes 12-15 (Thym /
  Colza / Ble / Lavande - Champs) `0.95 -> 0.40`, minus 0.15 - continuous fields, thinning with distance. The
  card cap is 24000 (was 20000). `-grassdensity <k>` scales it, an explicit `-grassstride` or `-grasslattice`
  restores the fixed lattice (the A/B: `-grassstride 2 -grassmax 20000` = the old default).
  Evidence: `docs/images/grass_density_law_wheat_before_after.webp` (the wheat field, left old lattice).
* The emit was the cost: ~10 ms of CPU per frame in a field. Fixed: (1) `getHeight` searched every loaded sector
  per card - the emitter now samples its own sector (`SampleSectorHeight`); (2) the card-end road probes (section 9)
  hit hash maps ~50 times per card - a per-sector 96x96 `RoadMask` and the graph's `dilated` bucket set now gate
  them (one read per card). Measured (`-tpin 23104,5721`, steady frame): old lattice before the fixes ~16 ms,
  new law 14.6 ms; the old lattice itself is 11.2 ms now. Field (`19576,24670`): 14.1 ms.
* The shadow / mirror / probe passes no longer draw the grass (`-grassshadows` puts it back): no measurable change
  in frame time, kept as it removes the pointless casters.
* Not done: Hawaii / Casino grids not looked at (the Hawaii run spawned in the void with Ibiza coordinates); the
  car-flatten (cards through the car body) and sizes per distance.
