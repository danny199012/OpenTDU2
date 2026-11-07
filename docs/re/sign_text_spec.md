# Road-sign text (direction signs, RS_Dir*)

Why the port's direction signs draw blank faces, and what the data provides to
fix it. Probed with `tools_probe/sign_probe.py` (session 59); the runtime fix
lives in `source/world/sign_text.*`. `tools_probe/sign_locate.py` answers the
other direction - *where* is the sign that says X (Â§5).


## 0. The pieces

* The sector `.pmi` **A lists** place sign actors (docs/re/pmi_spec.md 2.2).
  Type-4 records carry the sign text payload (pmi_spec.md 1.5 decoded the
  charset; nothing consumed it - that was the bug).
* Actor models: `worldactors.bnk` `rs_dirl_<hc>.3dg` / `rs_dirr_<hc>.3dg`,
  variants `a`..`e` (five face layouts each direction). Each model has a plate
  primitive (material 0, textured) and pole/metal primitive (material 1, no
  UVs).
* Face textures: `commonworld.bnk` `rs_maps.2db` (1024 x 1024, DXT1 **sRGB**,
  fmt 51, 9 mips, hash `0x005350414d5f5352`), borrowed by the worldactors sign
  materials into the COLOR slot of the `td_sign` shader set
  (`7dad16b47eb5caca`). `rs_maps_g.2db` (512 x 512 DXT1) is its gloss map.

## 1. rs_maps atlas [DATA]

Top rows hold the **blank plates** the direction signs sample (everything else
on the sheet serves other actor signs: speed circles, STOP, chevrons,
triangles, the blue AUTOVIA panel...):

| atlas region (pixels, mip 0) | content |
| --- | --- |
| x 0..810, y 17..66 (`v` 0.017..0.065) | green arrow plate, blank, black outline |
| x 10..800, y 218..298 (`v` 0.213..0.291) | white arrow plate, blank, black outline, arrow tip right |

The direction-sign plate quads sample exactly these two bands (plus 2-3 px
border slivers); nothing between them (`v` 0.065..0.213 grey background) is
sampled by the plate prims.

Per variant (prim-level UV bands):

* `rs_dirl_a` / `rs_dirl_e` (+ `rs_dirr_*`): white plate band only.
* `rs_dirl_b` / `c` / `d`: green plate band **and** white plate band.

## 2. Type-4 payload grammar [DATA] (2247 records, 1062 uniques on Ibiza)

Hawai'i has **no type-4 records at all** (2859 A lists scanned, 0 payloads
[DATA]): its US-style arrow signs (`rs_arn*` worldactors models sampling
`v` 0.25..1.0) use the **pre-baked faces** of the atlas' lower half and were
never blank. The text problem is Ibiza-only.

Byte 0 = layout code, then fixed-width row windows (verified on every
extraSize of the island census; `|` below = zero byte, charset per
pmi_spec.md 1.5 with `0x00` = space):

| extraSize | code(s) seen | structure |
| --- | --- | --- |
| 19 | `0x1d 20 23 25 27` | `[code][18-char name window]` (code = distance, see below) |
| 20 | `0x1a` | `[code][18-char name][distance byte]` |
| 22 | `0x24` | `[code][20-char name][distance byte]` |
| 37 | `0x1e 21 26 28` | `[code][18][18]` - 2 rows (or 2 columns of one row) |
| 39 | `0x1b` | `[code]([18][dist]) x2` |
| 43 | `0x02 0x2b` | `[code]([20][dist]) x2` |
| 55 | `0x1f 0x22` | `[code][18][18][18]` |
| 58 | `0x1c` | `[code]([18][dist]) x3` |
| 64 | `0x03 0x2c` | `[code]([20][dist]) x3` |

* Name windows: characters + zero padding; interior `0x00` = space. The name
  may sit left / centered / right inside its window (leading zeros) - we
  re-layout text ourselves, so the offset is only evidence, not a requirement.
* Distance bytes extend the digit charset: `0x1b`..`0x24` = `0..9`, then
  `0x25`..`0x2a` = `10..15` (a `1` then the units digit). Code bytes in the
  `0x02`/`0x03` family are not distances.
  The `10..15` reading was checked against the island: for every destination
  with >= 5 signs, the point whose straight-line distance best matches the
  stated kilometres gives a **15% median relative error** with `10..15` and
  **36%** with the tens reading (`10,20..60`) - Aeropuerto 8% vs 31%, Dalt
  Vila 4% vs 41%, Cala Tarida 12% vs 47%, Ses Salines 12% vs 42% [DATA].
  `sign_text.cpp` emitted the tens reading until session 60 (the code
  contradicted its own comment); fixed there, and the probe below follows it.
* Examples: extra 64 `0x03`: `"CAN JORDI 4 / SANT VINCENT 5 / SANT MIQUEL 23"`;
  extra 22 `0x24`: `"Cala Comte 1"`.
* Semantics of the code byte beyond row sizing are unresolved [INF]; face
  variant selection comes from the model, not the payload. All 930 payloads of
  the distance-less layouts (19/37/55) carry a digit code, so whatever else it
  means it does double as row 1's distance [DATA].

## 3. How retail drew the text [INF]

`td_sign.sob` has only lighting constants (no text/UV-offset), the plates are
baked blank, and the payload is per-instance - so the original composed faces
CPU-side and bound a per-sign texture (or rewrote plate UVs). The original's
text rasterizer and font are not in the banks we searched: `rs_maps` holds no
glyph sheet. The port substitutes the frontend font (GSFlash "main" movie,
"Berlin Sans FB" bold, glyph page `fla_0116.2db` 1024 x 512 DXT1, white
glyphs on black - luminance = coverage, digits + accented Latin incl. `Ã±`).

## 4. The port's fix (session 59)

`source/world/sign_text.*`:

1. Decode the payload into rows `{ name, distance }` per the table above.
2. Build the **plate layout** of the variant model once per actor model
   (`BuildSignLayout`): the .3dg's converted triangle list merges through
   shared edges into quads; quads inside the two blank bands group into plate
   rows (v overlap + model-space height within 0.15 m - `rs_dirl_d`'s two
   white plates share ONE atlas rect at different heights). Zones come out
   top plate first.
3. Per **(payload, variant)** compose a full 1024 x 1024 copy of the rs_maps
   mip 0 with one row stamped into each zone (name left / distance right,
   size = zone height x 0.75 capped 40 px, black). The text is stamped into a
   scratch and blitted **x-flipped** into the zone: the plate quads run their
   u against the plate's left-to-right (proven with zone-edge markers painted
   into the face - the zone's u0 edge lands at the plate's right - and by A/B
   on the rendered word), so the pre-flipped face reads normally in the
   world.
4. CPU-encode DXT1 + box-filter mips, upload through a `Texture2DB` built in
   memory (`createFrom2DB`, same sRGB format as the original).
5. `EdenWorld::drawInstances` swaps the texture into the sign material's
   `rs_maps` slot for the draw and restores it right after (the sector
   shadow-map mechanism, eden_renderer `assignTexture` fields); the composed
   texture replaces only the text zones - the rest of the atlas copy is the
   original texels, so every other sampling of the material is unchanged.
6. Faces are cached lazily (first draw, key = payload hash x variant x
   **layout hash**), LRU-capped at 160, dropped with the world. The layout is
   part of the key because the same payload is placed on different models
   (`b` exists in rs_dirr, rs_tdirib, rs_dist...) whose rows sit in different
   atlas rects.
7. **Every road-sign family is composed now (session 60):** `EdenWorld` no
   longer filters on the `rs_dir` name. It hands any actor whose material
   samples `rs_maps` to the composer, and `BuildSignLayout` knows all the
   blank plate bars of the sheet, not just the two `rs_dir` ones:
   `v 0.017..0.065` green, `v 0.089..0.132` white with the **yellow arrow
   tip** (`rs_tdirib*`), `v 0.151..0.193` white (`rs_dist*`),
   `v 0.213..0.260` white (`rs_dir*`), `v 0.585..0.635` white with the red
   border (`rs_twnin/twnout`) and the tall `v 0.303..0.644` highway panel
   (`rs_hwdist*`, split into one sub-row per payload row). A model with no
   plate zone (STOP, speed discs, the `rs_dead*`/`rs_forb*` baked art) keeps
   its baked face.
   The yellow-tip bar's text is kept left of the arrow (right edge 83%).
8. **Stacked plates get their own cell (session 60):** `rs_tdirib_b/c` and
   `rs_dirl_d/e` draw 2-3 physical plates with the *same* atlas rect. A
   per-model geometry hook (`EdenModel::GeometrySetup` ->
   `RemapStackedPlateRows`, sign_text.cpp) rewrites those vertices' v into
   the spare blank bars (yellow-tip families B/C/D, plain white C/D/B), and
   the layout's rects follow, so each physical plate carries its own row.
   This closes the session-59 open gap (previously row 2 was dropped).
   Limitation: a yellow-tip family's second/third row lands on a plain bar,
   so only its first row keeps the arrow tip.
9. **Per-family stamp orientation and distances (session 61):** the plate
   quads do not all sample their atlas rect the same way, measured on the
   model vertices (per-vertex uv<->position, `_twn_probe.py`):

   | family | u vs model x | v vs model y | stamp |
   | --- | --- | --- | --- |
   | `rs_dir*` | u low at +x (against) | v0 at plate top | x-flipped |
   | `rs_dist*` / `rs_tdirib*` / `rs_hwdist*` | u low at -x (with) | v0 at plate top | straight |
   | `rs_twnin/out` | u low at +x (against) | **v0 at plate bottom** | x-flipped + y-flipped |

   The blanket x-flip of sessions 59/60 was calibrated on rs_dir only and
   mirrored every dist/tdirib face (verified on screen: the sector-1-2-7-5
   `RS_Dist_D_act` "Centre/Platja/Puerto" sign read mirrored from its text
   side). The town plates' inverted v additionally rotated the text 180
   degrees. `BuildSignLayout` now takes the family (the model-name prefix,
   `sign_text.h` `SignFamily`), sets `SignQuadLayout::FlipU/FlipV` per zone
   and `SignLayout::bDistanceRows`; the face-cache key hashes the flags.
   **Distances:** the retail photos show distance digits only on the
   distance plates (`rs_dist` "Centre 1 / Platja 1 / Puerto 1") - the dir and
   TdirIb families leave the payload's distance bytes undrawn (photo: the
   `RS_TdirIb_C_act` "Sant Rafael / Hisenda Aldea Oest / Platja" sign has no
   digits). `bDistanceRows` gates the distance stamp accordingly (dir/tdir
   off; dist/hw/twn on).
   Not yet retail-verified: the town sign's corrected orientation is inferred
   from the other families (all read correctly); the x360 recomp screenshot
   of an `EIVISSA` town sign is the pending check.
Flags: `-nosigntext` off, `-logsigns` (payload decodes, plate zones, cache
stats), `-signfacesdump` (composed faces as scratch PPMs), `-atsign [m]`
test spawn (mirrors `-atwash`, `gm_freeride.cpp`; the faces look along -Z).
Hawai'i: `-atsign` reports no direction-sign actors and leaves the island's
baked signs alone.

Verified (session 59): Ibiza `-atsign` spawn at the sector-3-0-5-6
`RS_DirL_D_act` (22977, 5259), faces composed from the streamed payloads
('SANT VINCENT 4 / Platja / Balasant' ...), plates readable in the 2560x1440
screenshot (docs/images/sign-text-s59.png, composed green plate
sign-face-composed-s59.png); A/B vs `-nosigntext` at the same pose: sign
region mean abs diff 5.24, control regions 0.0 (deterministic); `-storystep
5` exit 0; a 700-frame FREERIDE drive exit 0, VRAM flat.

## 5. Locating a sign by its text: `tools_probe/sign_locate.py`

The row text and the placement of a sign are the **same type-4 record**, so the
island can be indexed by destination:

    python tools_probe/sign_locate.py [C:\TDU2-Gamedata\TestDrive2]
                                      [-island ibiza] [-match Centre,Platja]
                                      [-spawn [m]] [-csv _signs.csv] [-quiet]

Every record prints as `[actor] (x, y, z) yaw <deg> <sector>` plus its decoded
rows (1135 sector banks, 2247 records, the Â§2 census); `-match` filters on the
row text (accent-blind substring), `-spawn` adds the FREERIDE
`-tp x,z -heading deg` that parks the car `m` metres in front of the plate
facing it (the `-atsign` rule: `sign - (sin yaw, cos yaw) * m`).

The actor name is resolved through `worldactors.bnk` `entities.xmb`
(`__Hc` -> `__Name`, the map `EdenWorld::findSignSpawn` uses); its
`RS_DirL_D_act` shape gives the side (L/R) and the plate variant letter.
**The coordinates agree with the port to the centimetre**: `-atsign` logs
`RS_DirL_D_act (22977.4, 10.5, 5259.3)` where the probe prints
`22977.40, 10.47, 5259.31` [DATA].

### 5.1 Row icons [DATA]

Some rows start with a byte outside the charset: a **pictogram**. Over the
whole Ibiza census each code is the first byte of a row and prefixes exactly
one destination word:

| byte | icon | the word it prefixes (records) |
| --- | --- | --- |
| `0x48` | harbour | Puerto (103) |
| `0x49` | old town | Dalt Vila / Catedral de Ibiza (56) |
| `0x4a` | airport | Aeropuerto (114) |
| `0x4b` | beach umbrella | Platja (286) |

`0x46`, `0x4f` and `0x58` occur too (4 / 14 / 6) and never start a row; the
`0x58` samples sit in a distance slot (size 58, `body[18]`), so they look like
distance values beyond the `0x1b..0x2a` range - unresolved [INF].

Until 2026-11-05 the port's `decodeCharset` dropped these bytes, so a composed
face drew "Platja" where retail drew the umbrella + "Platja". The probe prints them as `[beach] Platja`, `[airport] Aeropuerto`, ...


**2026-11-05 (`win`):** the pictograms are drawn now (`SignTextRow::Icon`, a
framed square before the name, §6.4).

## 6. 2026-11-05 (`win`): matching the x360 photos

The owner's four x360-recomp photos (TdirIb "Centre / Platja" and "Sant Rafael /
Hisenda Aldea Oest / Platja", Dist_D "Centre 1 / Platja 1 / Puerto 1", DirR_D
"SANT MIQUEL / Residencial Nord / Foradada") against the port at the same signs
(`tools_probe/sign_locate.py -match ... -spawn`; the TdirIb faces look along +Z,
so their `-spawn` puts the car behind the plate - use the opposite side).
Result: `docs/images/sign-faces-2026-11-05.jpg`.

### 6.1 The code / distance byte is a layout id on dir / twn / tdir [DATA]

The per-family census of the row numbers (all 2247 records) is not a distance
spread on three families:

| family | value | records |
| --- | --- | --- |
| DirL_A / B / C / D / E | 10 / 2 / 3 / 4 / 11 | every record |
| DirR_A / B / C / D / E | 12 / 5 / 6 / 7 / 13 | every record |
| TwnIn / TwnOut | 8 | all 230 |
| TdirIb (per row) | 11 / 12 / 13 / 14 | 32 / 872 / 22 / 906 |
| Dist / HwDist | 0..9 spread | real distances |

So the dir and town code byte is the **model id** (the town sign's "8" was a
phantom: `bDistanceRows` is off for `Twn` now), and the TdirIb row byte is the
row's **arrow**: 14 = right, 12 = left (photo: "Platja 12" draws a left arrow,
its two rows "14" right arrows). 11 and 13 occur next to both 12 and 14 on one
sign ("Suburbis 11 / Centre 11 / Platja 12", "Puerto 12 / ... / Dalt Vila 13"),
so they are neither; they are drawn **up / down** (an up-left-down-right
enum) [INF] - the check is an x360 shot of the sector-3-3-4-7 sign
"Catedral de Ibiz 12 / Centre 12 / Barri antic 13".

Distances: the code distance of the 19/37/55 layouts goes on **every** row, and
a zero distance byte repeats the row above - the photo's Dist_D sign is the
payload "Centre 1 / Platja / Puerto" (the only one with those names) and draws
"1" on all three rows [INF from that photo].

### 6.2 The u direction is measured per zone, not per family [DATA]

`-logsigns` prints every plate quad's u at its min / max model x. The front
faces all have normal +Z, so u growing with x reads straight:

| model | plate u vs x |
| --- | --- |
| `rs_dirl_*` (green + white) | against (u 0.405 at x -1.5, 0.007 at x +1.2) |
| `rs_dirr_*` (green + white) | **with** |
| `rs_dist_*` white, `rs_tdirib_*` | with |
| `rs_twnin/out_a` | against |
| `rs_dist_b/d` green header | **folded**: two half-quads, u 0.005 -> 0.166 from each end to the middle |
| `rs_hwdist_c` panel | **rotated**: v runs along x (0.303 -> 0.643), u up the panel (0.859 -> 0.990) |

The session-61 family rule flipped every `rs_dir` zone, so every right-pointing
direction sign (`DirR`, ~860 rows) read mirrored ("JEUQIM TNAS"); the folded
green header drew "Centre 1 | 1 entreC"; the highway rows stood on end.
`BuildSignLayout` now sets `FlipU` from the zone's quads; a folded zone is
unfolded by the geometry hook (`UnfoldMirroredPlates`: the mirrored half's u
becomes `2 * 0.166 - u` in the parsed and the raw stream, the zone widens to
0.005..0.328) and the composer mirror-copies the cell past the fold so the
plate looks as before; a rotated zone stamps its rows turned (`bRotated`).

### 6.3 Stacked plates: upside-down members and their look [DATA]

`rs_tdirib_c`'s middle plate maps the shared cell **upside down** (v 0.0886 at
its bottom edge y 0.833, 0.1316 at its top y 1.219; the other two the reverse),
which the v(y) ownership test of the remap could not follow: the middle row
showed the spare bar's outline with the text squashed. `RemapStackedPlateRows`
now measures each member's orientation (mean v of its top edge vs its bottom
edge, ignoring vertices shared by two plates), un-flips an inverted member while
remapping, and records the member's source band (`SrcV0/SrcV1`). The composer
copies the source band over the spare bar with the same linear map, so every
stacked plate looks like the first - the TdirIb yellow tip on every row (the
session-61 limitation is gone).

### 6.4 The face [INF, from the photos]

* **Font**: the gauges movie's "Arial" (fla_0001, 1024 x 512) with a faux bold
  (max coverage over a small disc of the page, the quad padded so a thin "I"
  widens too); "Berlin Sans FB" stays the fallback (`-signfontberlin`). No sign
  font was found in the banks (a name scan of every non-sector bank: only the
  Hawai'i `sf_*` props and road-paint `arrows.2db` / `road_arrows.2db`).
* **Colours**: white lettering on the green bar (`SrcV0 < 0.078`), black
  elsewhere.
* **TdirIb arrow**: a bold black arrow centred in the yellow tip (atlas x
  324..377, measured), the name kept left of it; long names narrow to fit.
* **Dir plates**: the row stays off the pointed end (atlas u 0.375..0.405).
* **Pictograms** (0x48 harbour, 0x49 old town, 0x4a airport, 0x4b beach): a
  framed square of 0.8 x the plate height before the name, the shapes drawn
  procedurally after the photos (umbrella + waves, ship + waves; the airport
  and old-town shapes have no photo yet) - the original art is not in the
  banks searched.

Not done: the HwDist "EIVISSA ... 1 2" row carries two digits inside its name
window (meaning unknown, drawn as text); the 11/13 arrows; the airport / old
town pictograms against a photo.
