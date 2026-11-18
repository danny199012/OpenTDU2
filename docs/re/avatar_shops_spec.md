# The avatar shops on the retail SHOPSAVATAR movie (hairdresser, clothes)

Written 2026-11-17 (`main`, session 55) after the surgeon page was driven (`surgery_spec.md` section 5).
Tags as in `CLAUDE.md`. Disassembly: `python tools/vmf_disasm.py shopsavatar.vmf out.txt` (extract the movie with
`otdu2_bnk_tool extract .../FrontEnd/HiRes/shopsavatar.bnk`).

## 1. How a room runs it [DATA + port]
`InteriorWalk::avatarLabel`: `ESUR*` -> `surgeon`, `EHD*` (Ibiza `EHD_B`, interior `ihd_b`) -> `hairdresser`,
`EAS_A..E` (interior `ias_a..e`) -> `clothes_shop`. The seller selectables are walk types 29 `hairdresser`, 22 `goodies`
(clothes), 27 `surgeon`. **Surgeon: default, done.** Hair and clothes are **opt-in** (`-hairui`, `-clothesui`): the
native `ClothesMenu` stays the default until they work.

## 2. Hairdresser (default; `-nohairui` = native menu) - DONE (Hair page; Pilosity same code; Eyebrows not)
- Title bar "Hairdresser"; the stylist's A sends `hadr:open`, answered with `openMenuHairDresser` (`InteriorWalk`); square menu
  `hadr:edit:open` needs **`/:isWoman`** (picks `manhaircut` / `womanhaircut`). Pages: `maha:` men's hair, `woha:` women's, `pilo:`
  facial hair, `eybr:` eyebrows; each page's `<page>:open` is where the game writes the row values.
- **The blocker was the AVM1 runtime**: `GetProperty(_target)` (property 11) returned undefined, so `setHairMenu`'s
  save-the-target / `SetTarget2` returned to the root and `GoToLabel('open')` never reached `hair_menu` (blank page).
  `FlashPlayer::getProperty` now returns the clip's slash path.
- **Rows** (`HairType` icon box; `HairCut` big box `...:cuts`; `HairColor` `...:colo`; `PilosityType/Cut/Color`): a choice box
  shows the text of the `_root` variable `VarToFocus` names, read back after it sends `<page>:<row>:scne|scpr` - so the game answers
  by writing the new display text into `/:HairCut` / `/:HairColor`. The icon box writes `/:HairType` itself (`short|long|afro`,
  women `w_short|w_attached|w_afro`; facial `goatee|moustache|beard|shave`).
- Data: haircuts = DBClothe `_parttype` 3 of the player's `_sex` (grouped by name: Afro / Long+Attached+Half-length / Short+Shaved),
  facial hair = `_parttype` 9 (men) - grouped Goatee / Mustache / Beard; price = the row's `_price` (one price when a look changes;
  a colour-only change is free [INF]); tint = HAIR_MAT palette index 0..13 (`DBPNJ _hair_color` column). Saved as
  `worn_clothes` 3 / 9 + `hair_color` / `beard_color` (`PlayerProfile::HairColor/BeardColor`), applied by `EdenAvatar::load`.
- Flow verified: Hair -> Style / Haircuts ("Shaved head 2/3") / Tint ("Color 3") -> `$ 250` -> square menu Save -> the dialog "Make
  changes to your look?" -> `hadr:edit:vali` -> `paid $250` (`docs/images/hairdresser_retail_ui.webp`).
- Not done: Eyebrows page (`EyebrowsType/Color` need the avatar's eyebrow atlas), live preview (no avatar in view), the `C_BUY_HAIR`
  cinematic, DLC hair (`HairDLC`), the casino salon (`EHAIR_CA`, same code path - untested).

## 3. Clothes shop (default; `-noclothesui` = native) - browse + buy work
- Walk aim: `-walkaim Seller_area` (the type-name aim `goodies` found nothing). A on the seller sends `csin`, `camn`, `came`, `clin`.
- The movie runs the HOUSE closet's dressing protocol: the category squares (Hats, Glasses, Tops, Bottoms, Shoes, Gloves) send
  `clin` (the key in `/:BtValid`), the item card pages with `neit` / `prit` and shows `n / total` from `/:Nb_Items`
  (`InteriorWalk::onUiCommand`, shared with `bHomeUi`); in the shop `closetItems` is the DBCarShop brand's stock of the part
  (verified: Hats 1/7, 2/7 -> the shop's brand items). Screenshots in the session log only.
- **The A refusal was missing variables**: the shop page's `A()` (page `!= dressing_ic`) needs `/:Solded` == false (owned -> true),
  `/:DLC_Locked` == false and `/:IsEnoughMoney`; then `TestBuy` opens the `buy_item` dialog (or `cant_buy_item` when poor) and the
  movie sends `buit`. `InteriorWalk::updateClosetItem` sets them (+ `/:CurrentPrice`), `buit` shares the `seit` code: charge the DBClothe `_price`,
  add to `OwnedClothes`, wear. Verified: A -> dialog -> `buit` -> `bought ... (money now ...)`, wear, back to the categories.
- **Now the default** (`-noclothesui` = native menu). Open: the card shows the brand tag only (name / price text fields not found), the
  `C_BUY_ITEM` cinematic, seeing the item on the avatar (no body in view; the mirror now shows the player), DLC items.

## 4. The clothes-shop mirror (fixed)
Owner: "the mirror doesn't reflect the characters". The `MIRROR` GSReflection projector already drew the room (session 45) and
the seller, but the player is first person - no body was drawn. `IntroCinematic::setMirrorPlayer` (fed each walk frame
from `WalkMode::update`) poses the player's avatar on the stand idle at the walker's feet and `bPlayerMirrorOnly` draws it in
the projector passes only. `-nomirrorplayer` = the old frame. Verified `EAS_A`: pixel diff confined to the mirror quad,
`docs/images/clothes_mirror_player.webp`.
