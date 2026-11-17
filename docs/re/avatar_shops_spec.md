# The avatar shops on the retail SHOPSAVATAR movie (hairdresser, clothes)

Written 2026-11-17 (`main`, session 55) after the surgeon page was driven (`surgery_spec.md` section 5).
Tags as in `CLAUDE.md`. Disassembly: `python tools/vmf_disasm.py shopsavatar.vmf out.txt` (extract the movie with
`otdu2_bnk_tool extract .../FrontEnd/HiRes/shopsavatar.bnk`).

## 1. How a room runs it [DATA + port]
`InteriorWalk::avatarLabel`: `ESUR*` -> `surgeon`, `EHD*` (Ibiza `EHD_B`, interior `ihd_b`) -> `hairdresser`,
`EAS_A..E` (interior `ias_a..e`) -> `clothes_shop`. The seller selectables are walk types 29 `hairdresser`, 22 `goodies`
(clothes), 27 `surgeon`. **Surgeon: default, done.** Hair and clothes are **opt-in** (`-hairui`, `-clothesui`): the
native `ClothesMenu` stays the default until they work.

## 2. Hairdresser (`-hairui`) - reached, list page NOT working
- Title bar "Hairdresser" shows. A on the stylist sends `hadr:open`; answering with `openMenuHairDresser` opens the square
  menu (Hair / Eyebrows / [Pilosity] / Cancel / Valid): `hadr:edit:open`, then Hair -> `hadr:edit:maha:open`
  (`woha:` for women, `pilo:` facial hair, `eybr:` eyebrows).
- **The game must set `/:isWoman`** before the squares (they pick `manhaircut` / `womanhaircut` from it) - done in `HairUi::bind`.
- `setHairMenu(category)` builds three rows: `_root.HairType` (icon choice box: short / long / afro; women short / attached /
  afro), `_root.HairCut` (the big choice box, command `...:cuts`), `_root.HairColor` (`...:colo`); facial hair
  `PilosityType` (goatee / moustache / beard / shaved) / `PilosityCut` / `PilosityColor`; eyebrows `EyebrowsType` /
  `EyebrowsColor`. Choice boxes send `<prefix>:scne` / `:scpr` (next / previous) and read `_root.<Var>`.
- **Where it stops**: after `hadr:edit:maha:open` the hair page draws nothing (the whole menu is blank). Not yet found:
  the variables the page needs (`HairDLC` / `isHairDLC` / `popup_dlc` appear in the same function, `LockHair*`, `HairStyle`).
  Next: log `getVariable` of those on `maha:open` (`-loghair` prints the known ones), set them, and see which makes the
  rows appear; then answer `:scne/:scpr`, the price (`/:TotalPriceStr` / `EnoughMoney`) and `hadr:edit:vali` like `SurgeryUi`.
- The haircut data is the `HAIR` brand of DBClothe (`_parttype` 3) - the native menu sells it already.

## 3. Clothes shop (`-clothesui`) - title bar only
- `clothes_shop` label (frame 194) shows "Clothes Shop". The seller (`Seller_area`, type 22 `goodies`) was not selected by
  `-walkaim goodies` in the first run (`validate on ''`) - check the aim distance / that the selectable is reachable.
- Pages: `cl_in` 221, `cl_stop` 226, `item_selection_in` 240, `item_selection_stop` 256; commands seen in the movie: `clin`,
  `neit`, `prit`, `seit`, `nsit`, `buit`, `clre` (the house closet already answers `clin/neit/prit/seit` in
  `InteriorWalk::onUiCommand` - the same HOME-style dressing protocol; reuse it with the shop's price + DBCarShop brand stock).

## 4. The clothes-shop mirror (fixed)
Owner: "the mirror doesn't reflect the characters". The `MIRROR` GSReflection projector already drew the room (session 45) and
the seller, but the player is first person - no body was drawn. `IntroCinematic::setMirrorPlayer` (fed each walk frame
from `WalkMode::update`) poses the player's avatar on the stand idle at the walker's feet and `bPlayerMirrorOnly` draws it in
the projector passes only. `-nomirrorplayer` = the old frame. Verified `EAS_A`: pixel diff confined to the mirror quad,
`docs/images/clothes_mirror_player.webp`.
