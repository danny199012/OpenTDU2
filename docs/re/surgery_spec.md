# Surgery (the cosmetic clinic): face editing and the bandages

Status (2026-10-30, session 53 `glm`): **investigated, not implemented yet**. Every
retail piece is identified and the port already walks into the clinic. This file is
the implementation brief. Tags: `[DATA]` read from the game files, `[CODE]` from the
readable exe (or the port), `[INF]` chosen, `[ENC]` in the SecuROM band.

## 1. What retail had

### 1.1 The buildings and interiors [DATA]

Three clinics, all present in `spots.bin` (the record layout: id hash `+0x08`,
**bank name `+0x10`, world position `+0x18`** — the `+0x08/+0x10` offsets in
STATE.md's session-9 notes are 8 bytes off):

| island | record | bank | position | interior bank |
|---|---|---|---|---|
| Ibiza | 71 | `ESUR_A` | (4500, 37, 19113) | `interior/isur_a.bnk` (+6 localized `isur_a__xx`) |
| Hawai | 139 | `ESUR_A` | (17227, 6, 18501) | `interior/isur_a.bnk` |
| Casino | 1 | `ESUR_CA` | (5742, 116, 399) | `interior/isur_ca.bnk` |

A second interior set `interior/bsur_a.bnk` (+6 localized) exists under the same
authoring folder `Euro/spots/surgery/` [DATA asset index]. Its maps (`head_chart`,
operating-room plastics, `toile_noire`) say it is the **operation room** [INF: "b"
= *bloc (opératoire)*]. Retail ran surgery as a **game mode**:
`GAME_MODE_SURGERY = 33` (0x21), and `GamePC.cpr` binds the mode's Flash movie:
`AddFlashMovie "SHOPSAVATAR" 33 // GAME_MODE_SURGERY` (plus `CAS_COMMON` 33 for the
casino island). The mode is in the pool-water-enabled list right beside
`AVATAR_CHOICE` and `HAIRDRESSER` [DATA pool_interior_spec.md 2.1] — i.e. the same
family of indoor avatar modes. So retail's surgery screen is the **shopsavatar
movie's surgeon panel**, opened over the clinic/operation-room interior.

### 1.2 The story hooks [DATA]

`DBPhoneCall` rows 19/20: `SURGERY_HAWAI` (npc 399, text 3937, timer 0) and
`SURGERY_IBIZA` (npc 399, text 3938, timer 75, `_nbrepeats` 2, `_yesnoanswer`).
The NPC "Miami" calls you: *"There's a cosmetic clinic on Ibiza. Pay them a visit!
A makeover might be just what you need!"* (line 3938; 3937 is the Hawai wording).

### 1.3 The price table [DATA]

`DBSurgeryCost`: 46 rows `{ u32 _cost; string _surgname }` — one row per editable
item. Prices: every morph slider **$100**; `EYE_COLOR` **$200**; `FRECKLES` **$200**;
`EYE_BROWS_TYPE` **$300**; `TINT` **$400**. The 46 names:

- 39 `C_*` morph sliders (`C_Nose_Tall_or_Small` … `C_Eyebrow_size`), plus
  `CustPos Older`, `CustPos Jaw thin`, `CustPos Nose pushed`;
- `EYE_COLOR`, `EYE_BROWS_TYPE`, `FRECKLES`, `TINT`.

They map onto the avatar face data as follows: the `C_*`/`CustPos` names are
**exact members of the port's `kFaceFxCustomNodes[49]`** (`source/game/avatar/facefx.cpp:9`,
the FUN_00b770b0 order), i.e. indexes into the `.avt` **FaceParamArray** (49 × i8,
key 0x9d93). `CustPos Older` is the `.avt` **age** float (key 0x8d6d), `FRECKLES`
the freckle float (0x3f89), `EYE_COLOR` the **EyeID** (0x82b9), `EYE_BROWS_TYPE`
the **EyebrowsID** (0xc881) [INF: the remaining `.avt` face fields; TINT is the
skin tint, SkinID/SkinDiffuse family — pick on implementation]. The five
lip/eyelid morphs (`C_Lower_lip_thickness` … `C_LowerEyeLid_Up`) and the two
ethnicity morphs (`CustPos Asian/Afric`) are **not** in DBSurgeryCost [DATA].

### 1.4 The menu wiring [CODE exe strings]

The readable exe carries the Flash contract of the surgeon screen:
`surgeon.openMenuSurgeon`, `_root.TotalPriceStr`, `/:NbNotches` (the changed-slider
count), `_root.LockSurgeryValid`, `/:surgery_locked`, `/:m_SurgeryLocked2_`,
`COLLECTION_SURGERY`, `EYE_COLOR`, `EYE_BROWS_TYPE`, `FRECKLES`, `TINT`,
`_root.MyMoneyStr`, and the cinematics `C_OBSERVE` / `C_HEAD` / `C_360` (present in
`isur_a.bnk` too — see §2). The shopsavatar `.vmf` itself is not decoded; the port
does not drive it (its native menus stand in everywhere else either).

### 1.5 The bandages [DATA + CODE]

Save fields (readable exe strings; the 360 save schema
`docs/re/savegame_schema_360.txt` +319 gives the order): `NbSurgeries`,
`NbHaircuts`, `BandageTime` (u64), `Bandage` (bool), `NeedBandageNews` (bool),
`HelmetBandage`, `BikeHelmetBandage` (in the exe strings, missing from the 360
schema list), `GlassesBandage`, `BeardBandage`, `HairBandage` (u16 each — the worn
item ids to restore), `HelmetColorBandage`, `BikeHelmetColorBandage`,
`GlassesColorBandage`, `BeardColorBandage`, `HairColorBandage` (u8 colours),
`MultiChallengesWithBandages`, `_time_spent_in_surgery`, `BeenInSurgeryShop`.

The bandage itself is a **clothes item on the head slot**: `DBClothe` id 313
`W_HE_BANDAGE` / id 314 `M_HE_BANDAGE` (`_parttype` 5, `_brand` 77, `_price`
999999 = not buyable, banks `avatar/clothes/helm/{w,m}_he_bandage.bnk` with
`decal1/2` maps) [DATA]. The exe also carries the empty-slot meshes used while
bandaged: `M/W_NO_HAIR`, `M/W_NO_BEARD`, `W_NO_HELMET`, `W_NO_GLASSES`, and the
hair-state names `DEFAULT`/`BANDAGE`/`NONE` [CODE]. Localization:
3373 "Surgery bought", 4405 "You cannot go back into surgery until you have your
bandages removed.", 5756 "You still have your bandages!", 7169 "You cannot go to
the hairdresser until you have your bandages removed.", 7472 "Your bandages have
been removed!" [DATA db_us.cpr].

The removal timing (what `BandageTime` is compared against, and how many
game-hours it lasts) is in the encrypted PC band [ENC]; the PS3 save serializer
`func_0059CA78` confirms the field block but the consumer was not located yet.

## 2. What the port already has (verified this session)

- **The clinic is walkable today.** `-interior ESUR_A` loads
  `interior/isur_a.bnk` through the session-45 interior walk:
  `InteriorWalk: 'ESUR_A' (interior/isur_a.bnk): cinematics C_LIFE_FISH C_INTRO_I
  C_LIFE C_360_W C_OBSERVE C_360 C_HEAD; start camera 'cam_entree'; 2 selectables`
  — **`Hostess_area` type 27** (the walk-mode table names 27 "surgeon",
  `walk_mode.cpp:63`) and `Exit` type 0. Screenshot:
  `docs/images/surgery_isur_a.webp` (surgeon at the desk under the reticle,
  operating chair, diplomas, x-ray light box).
- **A on the surgeon** reaches `GMFreeRide::openSpotShop` with no `surgery`
  branch and shows the generic "This shop is not open yet." tip
  (`gm_freeride.cpp:2031`). The map icon (`surgery`, category 0x10, tab 2) and the
  `f_surgery` filter already exist (session 18).
- **The face-morph pipeline is live.** `EdenAvatar` parses the FaceParamArray
  (`eden_avatar.cpp:337`); `FaceFxLibrary::Evaluate` feeds the 49 `C_*` registers
  through the compiled graph to **face-bone poses** — the sliders drive the face
  bones 17-60 of the head skeleton (avatar_spec.md §3.2/§4), there are no vertex
  morphs — and the static path (no animation) evaluates them
  (`intro_cinematic.cpp:2603`). Overriding the 49 bytes after load changes the
  rendered face [CODE]. `applySkin` already reads age/freckle into the TD_Skinface
  material (`eden_avatar.cpp:457`).
- The player avatar is `profile.AvatarConfig` (an `AVT_*_Intro` preset .avt);
  the port's `profile.sav` is its own key-value file (`player_profile.cpp`) —
  face overrides have nowhere to persist yet.

## 3. Implementation plan (proposed, three phases)

1. **The surgeon menu** (the ClothesMenu pattern, native): a `kind == "surgery"`
   branch in `openSpotShop` opens a menu built from `DBSurgeryCost` (name, price,
   current value → target). A slider item buys ±N on its FaceParamArray slot
   (name → index via `kFaceFxCustomNodes`); `EYE_COLOR`/`EYE_BROWS_TYPE` cycle
   EyeID/EyebrowsID; `FRECKLES`/`CustPos Older`/`TINT` adjust their floats. Each
   purchase charges `_cost`, shows 3373, and live-updates the avatar (the shop
   cinematics `C_360`/`C_HEAD` in `isur_a.bnk` are the preview shots).
2. **Persist + bandage**: `face_params` (49 ints) + `eye_id`, `brows_id`, `age`,
   `freckle` (+ tint choice) in `profile.sav`, applied over the preset .avt after
   load; the bandage fields of §1.5 in the port's own shape: on the first purchase
   set `Bandage`, `BandageTime = game clock + duration` [INF: 24 in-game hours;
   the retail rule is ENC], swap the head slot to DBClothe 313/314 and hair/beard
   to the `NO_*` items; on expiry show 7472 and restore. While bandaged: A on the
   surgeon answers 5756, entering the hairdresser answers 7169 (the retail gates).
3. **The bandage on the player**: equip through the clothes pipeline (the worn
   head item of eden_avatar). First check: how the port equips a DBClothe item on
   the player avatar today (the `.avt` ClotheIDArray is preset clothing; the
   ClothesMenu buys but does not dress the player yet) — that is the one port-side
   unknown left.

## 4. Open retail questions

- The bandage duration rule [ENC — PS3 consumer of save +0x40/+0x48].
- Whether `TINT` is SkinID or the SkinDiffuse RGB [INF above].
- The shopsavatar surgeon screen layout (the port will not need it with a native
  menu; only if the owner wants the real movie driven).
