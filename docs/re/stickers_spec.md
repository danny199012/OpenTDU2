# Car stickers / vinyls: the retail packs and the port's editor

Tags as in `CLAUDE.md`: `[CODE]` readable executable · `[DATA]` game files ·
`[SHADER]` D3D9 bytecode · `[INF]` inference · `[ENC]` SecuROM-encrypted (not
analysed).

The retail sticker system in one sentence: each `DBStickersBanks` row names a
bank of decal textures; the shop editor stamps them onto a canvas that the
car paint's own shader lerps over the body UVs, and the port reproduces that
with its own editor (`paint_menu.cpp`'s sticker page) writing the same canvas
(`sticker_bank.h` `StickerCanvas`) into the same shader slot.

## 1. Where the stickers render [SHADER + DATA]

The TD_CarPaint2 pixel shader (the car body paint, full-feature permutation)
samples a sticker texture on **s5** and lerps it over the paint
(`paint_spec.md` 4 / 6):

```
paint = lerp(base, detail * LayrClr1, detail.a * LayrClr1.w); stickers lerp
```

s5 is the paint material's decal slot. The slot's type string in the data is
not one name: `one_77.2dm` ships it as `USER_TEX` (paint_spec.md 4), the
California's as **`STIKERS`** [DATA: the data's own spelling, verified in the
port's slot dump: `STIKERS='user_tex'`]. The port accepts
`USER_TEX` / `STIKERS` / `STICKERS` (`CinematicCar::bindStickerTexture`).
Every probed car carries the slot on its `Paint` material (`Paint_SK` too,
the skinned roof part); `Paint_LR` (TD_CarPaint, the low-res body) has none -
only the high LOD takes stickers. The slot ships populated with a placeholder
texture named `user_tex` (common_car.bnk), so the slot already counts into
the shader permutation's `S_NBTEXTURES`; binding another texture into it
never re-permutates [DATA + EXP].

The sticker texture is sampled with the body's UV0 [EXP: a bound art texture
appears where its alpha is, across the whole body - bonnet, deck, bumper -
at the places its own pixels occupy in UV space]. No second UV set, no
projection: the "canvas" model - composite the stickers into one texture,
the shader spreads it over the body - is the retail's own [INF: consistent
with s5 + UV0 + one texture per car].

## 2. The pack banks [DATA]

`DBStickersBanks` (26 rows): `_bankfile`, `_name_hc`, `_type` (1 = the shop
packs, 4 casino / 5 chase = event-only specials), `_price` (the pack),
`_monochrome` (the art recolours), `_isvalid` (0 = shipped-but-off: Fantastic,
Pinky, Elements, Racing, Animals, Vegetal 2). `DBStickersPricing` names per
action prices; the retail shop logic that consumed both is [ENC]
(`car_shop.cpp` reads the tables, not the shop).

Each bank is `Euro/Bnk/scenarics/packs/stickers/<_bankfile>.bnk`:

| content | what it is |
|---|---|
| `maps/<bank>_NN.2db` | one 512² art texture per sticker (the decal) |
| `<bank>_<hash>.2dm` | a material library, one `MAT_NN` per sticker |
| `<bank>_<hash>.3dd / .3dg / .shk` | the unit quad the retail editor stamped with |
| `hashcodes.xmb`, `entities.xmb` | the bank's file references and its scenery entry |

The `MAT_NN` materials are two-sided (cull 3, like glass / flash), carry the
art on a `COLOR` slot and a `FADEALFA` parameter; their texture hashcodes are
the .2db's own header hashcodes (the runtime `TextureCache::find(owner,
hash)` resolves them directly - verified: all 24 of `st_basic_nb` resolved;
the earlier "IC_NB" strings in the file are byte runs inside those hashes,
not a second slot).

## 3. The port's editor [CODE]

- `sticker_bank.{h,cpp}`: `StickerBankCatalog` (the rows, the banks loaded on
  demand into the TextureCache under `stickers/<bankfile>` owners, released at
  FREERIDE exit), `StickerPlacement` (bank, index, U/V centre, scale,
  angle, tint), `StickerCanvas` (a 1024² transparent render target, the dash
  GPS screen's RTT pattern, composited only when dirty).
- `cinematic_car.cpp` `bindStickerTexture`: swaps the canvas into the paint
  materials' sticker slot (the slot-variant hunt above) + `updateProgram`.
- `paint_menu.{h,cpp}`: the shop's two pages. PAINT as it was; STICKERS:
  pack list (`isBankSold`: `_isvalid` and not type 4/5) → thumbnail grid →
  place (the live editor) → the placed list (move / remove). The monochrome
  packs recolour through the tint palette (the art is white-on-alpha, the
  canvas draws it tinted); colour packs ignore the tint. Price = the bank's
  `_price` per sticker [INF: the retail per-sticker pricing is not decoded].
  The placed list and the buy write `PlayerProfile::OwnedCarStickers`
  (`car_stickers` line in `profile.sav`, the same text form as `car_paints`).
- `gm_freeride.cpp`: the ESTICK shop flow hands the menu the driven car's
  canvas (`openSpotShop` "carpaint" + `setStickerContext`); the spawn path
  applies the saved layout (`applyOwnedStickers`, `applyOwnedPaint`'s twin at
  both car-load sites) and `drawOffscreen` composites + binds once. While a
  sticker is being placed inside the walk-in shop, the view cuts outside to
  the car (the retail editor's garage-car camera [INF]): the freeride scene
  renders instead of the room (`paintMenu.isStickerPlacing()` branches in
  `drawOffscreen` / `draw`), a 3/4 rear-left camera pose taken once on entry.

## 4. Flags

| flag | what it does |
|---|---|
| `-nostickers` | the whole feature off (no page, no spawn application) |
| `-logstickers` | the bank rows, the per-bank art/icon resolution, the car's paint slots and the bind count |
| `-stickertest <bank>,<index>` | bind one pack art texture straight onto the sticker slot at spawn (the shader probe; renders the whole 512² over the body UVs) |
| `-stickershop` | the shop menu over the freeride scene at spawn (autoplay can drive the editor; the retail flow walks into an ESTICK interior first) |

## 5. Evidence (headless, build 20 (a4a50ba+dirty), 0 VUIDs, 0 stalls)

- `docs/images/sticker_bind_ab.webp` - before / after of the first bind: the
  white decal shapes of `st_basic_nb` appear on the body only with the bind
  (the shader-slot proof).
- `docs/images/sticker_q7_livery.webp` - the Q7 in a full green/white
  `st_fun01` livery (the second car; the R8's freeride model load is
  incomplete - a separate, pre-existing quirk).
- `docs/images/sticker_bonnet_art.webp` - a coloured vector art over the
  bonnet (the cockpit view; the art lerps straight over the paint).
- `docs/images/sticker_spawn_layout.webp` - the 16-sticker test layout from
  `profile.sav` back on the car at spawn (the persistence → canvas → bind
  chain).
- `docs/images/sticker_editor_placing.webp` / `sticker_editor_menu.webp` -
  `-stickershop` driven by autoplay: the place-mode hint strip, then the
  "On the car" list with the new `st_basic_nb #1` and "Sticker bought:
  $5,000" (the money exactly $5,000 lower), the game's own save writing the
  17-entry `car_stickers` line with the moved / scaled values.

## 6. Open / not done

- The retail editor's exact stamping math (quad → canvas) is [ENC]; the port
  composites its own way (rotated tinted quads through the 2D pipeline).
- `DBStickersPricing` per-action prices are not applied (the pack `_price`
  stands in per sticker).
- The shop interiors' seller NPC dialogs (the retail shopscar movie) stand
  in as with the paint shop.
- Garage / house scenes do not wear the saved layout yet (only the driven
  car; the same scope `applyOwnedPaint` had at first).
- The `FADEALFA` parameter and the `IC_NB` icon-vs-art question (these banks
  ship no separate icon slot; the thumbnails are the art) are unexplored.
