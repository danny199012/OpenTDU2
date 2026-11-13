# The retail sticker shop UI (SHOPSCAR.vmf's "stickers" page)

Written 2026-11-13 (`main`). The owner: *"Can you try implement the real ui for the
stickers/vinyl editor in the shop - it's only partially implemented with non real ui"*.
`stickers_spec.md` section 3 described the port's own ImGui-less panel (`PaintMenu`); this
is the retail movie that panel stood in for, now driven. Tags as in `CLAUDE.md`: `[CODE]`
readable executable - `[DATA]` game files (the AVM1 of `shopscar.vmf`, decoded with
`tools/vmf_disasm.py`) - `[INF]` chosen, with the reason - `[ENC]` not readable.

Screenshots: `docs/images/sticker_ui_*.webp`.

## 1. Where it lives [DATA]

`Euro/Bnk/FrontEnd/HiRes/shopscar.bnk` -> `shopscar.vmf` (1280x960, 408 frames). The same
movie is the dealers' (`car_dealer`, `scr_open`), the tuners' and the stickers' screen; the
page is picked by `/:WhatPage` and a root label:

| root label | frame | what |
|---|---|---|
| `st_open` | 68 | `WhatPage = "stickers"`, title `m_StickersShop_` ("Stickers Shop"), `CurrentAction = "walk_mode"`, the title bar and the cursor open |
| `st_stop` | 78 | the walk waits; `/:SelItemType` shows the prompt ("Stick & Paint") |
| `menu_in` / `menu_out` | 98 / 107 | the five-button menu |
| `st_close` | 116 | closes, then `FSCommand:sndp:driveout` + `FSCommand:gofr` |

The sticker shop interior `interior/istick_b.bnk` (spot `ESTICK_B`, two on Ibiza:
(18873, 26646) and (22684, 8500) [DATA: `-logspots`]) carries `C_INTRO_I`, `C_LIFE` and
**`C_NEW_CAR_DESIGN_I`** (cameras `cam_new_car_01..05`, a `CF_PCart` / `CF_Panel` rig and the
seller's `sti_carready` anims) - the retail editor's own car presentation. The port does not
play it: the car stays outside (section 5).

## 2. How the room runs it (`InteriorWalk`)

- `InteriorWalk::loadRoom`: a spot bank named `ESTICK*` sets `UiMovie = "shopscar"`
  (`bStickUi`; `-nostickerui` = the old native `PaintMenu`, the A/B). `startWalk` then
  `gotoLabel("/", "st_open")`.
- The walk selects the seller (`StickersShop_area`, FpSelectable Type 28 -> `npc_stickers`,
  the table at 0x00edd580 [CODE]) and drives `/:SelItemType` / `ItemType('over')` as for the
  dealers. **A on it is `ItemType('valid')`** [DATA: the `st_stop` buttons carry only the
  selection keys `0xca00` / `0xe600`; `sendKey(13)` does nothing here] -> `StickersMenu()`.
- While one of the movie's pages is up the walk is off: `camn` (the seller's camera) or
  `wmof` switch the walk input off, `wmon` gives it back. The pad is routed to the movie:

| port input | SWF key | movie |
|---|---|---|
| D-pad / stick left, right, up, down | 1, 2, 14, 15 | menu / grid focus |
| A (validate) | 13 | `A()` of the page |
| B (back) | `/:Clip.B()` | the page's `B()`; in the walk: the "Exit the shop?" dialog (`st_quit`) |
| L1 / R1 (Q / E) | 49, 51 | previous / next tool, previous / next pack (the arrows on the strip) |
| X / Y (Option1 / 2) | 120, 121 | `X()` (uniform scale, colours), `Y()` (symmetry) |
| Select / Map / H / Delete | 6 | the history menu |
| mouse click | `sendMouseRelease` | the buttons' release handlers |

  [DATA: the key conditions of the strip's buttons, `button 772 cond 6200 / 6600 / 1a00 /
  0c00`, `f000 / f200`]. The keys 49 / 51 / 120 / 121 are the pause menu's mapping too.
- `dbxo` / `dbxc` open and answer the GENERAL dialog box exactly as the dealer does; its
  buttons ignore keys for ~1 s while it animates in.
- `gofr` -> `InteriorWalk::takeLeaveRequest` -> `GMFreeRide` leaves the room (the car is put
  back at the shop's entrance, as for the dealers).
- **`FlashPlayer::setEnterFrameHandlers( true )`** for this movie: the pack grid's
  `initLoadingCategory` assigns `loading_anim.onEnterFrame = function ...` and waits for
  `/:CategoryLoaded`; the runtime had never run event handlers, so the spinner stayed up. The
  handler runs in the timeline that defined it (`Value::pClip` of a defined function), and
  only for movies that opt in (the other movies' handlers have never run and are left alone).

## 3. The FSCommand protocol (`stsh:`) [DATA] and what the game answers [INF]

`StickerShopUi` (`source/game/freeride/sticker_shop_ui.{h,cpp}`), bound to the movie when the
room loads, owner of the working layout; `GMFreeRide` forwards the commands
(`InteriorWalk::setCommandSink`).

| command | sent by | the game |
|---|---|---|
| `stsh:menu:enbl` / `dsbl` | `StartButtonStatus` | nothing (the buttons' state is the movie's) |
| `stsh:mcin` / `mcou` | menu / pages opening / closing | the car view on / off (section 5); `mcou` while the colour panel opens does **not** drop the sticker under the tools |
| `stsh:stic:list:open` | the grid page opens | loads the pack of `/:CurrentStickersCat` |
| `stsh:stic:list:prca` / `neca` | L1 / R1 on the grid | the movie has already moved `/:CurrentStickersCat` (wraps at `/:NbStickersCat`); loads that pack |
| `stsh:stic:list:vali` | A on a sticker (`/:SelectedSticker` = its index) | starts editing a new sticker of that pack at the canvas centre |
| `stsh:stic:list:canc` / `clos` | B | nothing |
| `stsh:stic:move` / `rota` / `scal` / `alph` / `colo` | `sendToolCmd` when the tool changes | the stick's meaning (section 4) |
| `stsh:stic:appl` | A in the editor | stamps (section 4) |
| `stsh:stic:flip` | the "Invert" button (mouse) | mirrors the sticker's art |
| `stsh:stic:uscn` / `uscf` | X on the scale tool | the movie keeps `IsUniformScale` |
| `stsh:stic:symn` / `symf` | Y | the movie keeps `UseSymmetry` |
| `stsh:stic:hist:open` / `undo` / `redo` / `canc` | the history menu | undo / redo the layout (snapshots) |
| `stsh:stic:colo:open` / `clos`, `myco:init` / `open` / `clos` / `clea` | the colour tool | section 6 |
| `stsh:stic:myco:vali` / `fill` / `eras` | A / X / Y on a "My Colors" slot | apply / save the picked colour / empty the slot (slot = `/stickers/colors/my_colors_box/:num_c`) |
| `stsh:save:open` | Valid & Pay | the invoice variables |
| `stsh:save:vali` | the "Validate and pay?" answer `a` | pays and saves |
| `stsh:clea` | the "Clear" answer | empties the layout |
| `stsh:canc` | the "Exit and cancel modifications?" answer | reverts to the saved layout; the movie then plays `st_close` -> `gofr` |
| `stsh:pain:*`, `stsh:pain:myco:*` | the Paint page | **not answered** (section 7) |

Variables the game writes (all `/:` unless a path is given) [DATA: read by the scripts]:

- at bind: `IsVehicleStickable` (DBCarPhysicsData `_isvalidsticker`), `IsVehiclePaintable`
  (false, section 7), `IsBike` (false), `UseSymmetry` / `IsUniformScale` (true), `CanUndo`,
  `CanColourSticker`, `HasModifiedVehicle` (the B in the menu asks "cancel modifications?"
  only when true), `EnoughMoney`.
- per pack: `NbStickersCat` (the sold packs), `CurrentStickersCat`, `NbStickersCurPage`
  (<= 24: every pack fits the 4 x 6 grid [DATA: 6..24 sticker banks]), `StickersCatStr` (the
  pack name), `PriceCat` (the pack's `_price`, money text), `IsCategoryBought` (the pack is in
  `PlayerProfile::OwnedStickerBanks`: the checkbox), `StickLocked` / `DLC_Locked` (false),
  `CategoryLoaded` (true when done), `CanColourSticker` (the pack is `_monochrome`).
- thumbnails: `setBitmapOverride( "/stickers/list/container/st<i>/lure", icon or art )` for
  i < 24 - the squares' `lure` is the placeholder the exe fills natively [DATA: a fully
  transparent shape, the same kind as the HUD dealer logo].
- tool values: `StickerXScaleStr`, `StickerYScaleStr` (100 % = a fresh sticker's width,
  0.22 of the canvas), `StickerRotateAngleStr`, `StickerAlphaStr`.
- money / invoice: `TotalPrice`, `TotalPriceStr`, `money`, `MyMoneyStr`, `StickersCostStr`,
  `PaintCostStr`, `LabourCostStr`, `EnoughMoney`.

## 4. The editor [INF: the retail stamping math and the stick mapping are `[ENC]`]

- The layout is `StickerPlacement` (+ `Alpha`, `bFlip`, `YStretch`; the profile line
  `car_stickers` gained the three trailing fields, old saves load). The working layout is the
  committed one plus this visit's stamps; `history` keeps snapshots for undo / redo.
- The sticker under the tools is an unapplied last entry of the layout (drawn live on the
  car through the existing `StickerCanvas` -> `STIKERS` slot chain). **A stamps it** (a copy
  stays under the tools, so a sticker can be stamped repeatedly); with symmetry on a mirrored
  twin lands at `U' = 1 - U` with the art flipped. **B drops the unapplied sticker.**
- Tool values: the left stick (also numpad 4 / 6 / 8 / 2, a left-button mouse drag): Move =
  U / V, Rotation = angle (+-140 deg/s), Scale = width (uniform) or width and height
  (non-uniform), Transparency = alpha. **The camera** ("Move camera" / "Zoom" in the help bar
  [DATA]) is the right stick, the D-pad, the mouse with the right button, and the wheel;
  an orbit around the car (`GMFreeRide::stickerCam*`).

## 5. The car view

While the movie is between `mcin` and `mcou` the room is not drawn: the freeride scene is
(the car parked outside), the movie over it (`IntroCinematic::drawUi`). The old cut never
actually showed the car from inside a room - the interior branch of `GMFreeRide::tick`
returned before `view.setCamera`; it sets the orbit camera now. The retail `C_NEW_CAR_DESIGN_I`
cameras are not used [INF].

## 6. Colours

- The colour tool opens the movie's `/stickers/colors`: a **Color Picker** (`color_box/
  color_picker`) and **My Colors** (`my_colors_box`, six slots).
- The picker's `lure_grad` is a placeholder; a generated 128 x 128 gradient (hue across, white
  -> colour -> black down) is bound to it, the stick moves the dropper (`cursor`; placed with
  `setClipTransform` at `57 + 212 u`, `59 + 212 v`, fitted against screenshots - the sprite's
  origin is not its tip) and the colour under it is the sticker's tint, live; `color1` shows it.
- Slot / picked cubes (`.../cN/cube/color`, `picked_color/color`) are fully transparent solid
  placeholders [DATA]: a white texture is bound to each and the clip colour transform tints it,
  re-applied every frame (the movie re-places the sprite as the box animates).
- The six slots are `PlayerProfile::StickerMyColors` (`sticker_colors` in `profile.sav`); a new
  profile starts with white / black / red / yellow / green / blue (the retail slots fill from a
  pipette that picks from the scene [INF]).
- Colour packs (`_monochrome` 0) keep their art: the tool is locked (`CanColourSticker`).

## 7. Money and the invoice [DATA + INF]

`DBStickersPricing` is `Labour $1000`, `Paint $1000`, `Erase $1000`. The invoice
(`Itemized invoice`: Paint Color / Stickers / Labor costs) is: **Stickers** = the pack price
of every pack the layout uses that the profile has not paid (`OwnedStickerBanks`: a pack is
paid once, for every car), **Labor** = $1000 when the layout changed (the erase fee when it
was emptied), **Paint** = $1000 when the paint changed (never, yet). `stsh:save:vali` pays,
adds the packs, writes `car_stickers` and saves. Backing out (`stsh:canc`, or leaving) puts
the saved layout back.

## 8. Not done / next

- **Paint** (`stsh:pain:*`): the base / highlight / stroke colours, the bicolour switch, the
  type picker and the "My Colors" slots of that page. The page reuses the same colour picker
  (`/paint/color_box`, `/paint/my_colors_box`). The car paint's custom colours need the
  TD_CarPaint2 colour parameters (`paint_spec.md`); until then the Paint entry shows its lock.
- The retail pipette (picks a colour from the scene) is the gradient picker here.
- The retail car presentation (`C_NEW_CAR_DESIGN_I`) and the seller's `sti_carready` anims.
- `HasModifiedVehicle` ignores paint; `EdCarSticked` / `NbStickersBought` (the diary's stat
  lines) are not written.
- Casino / chase packs (types 4 / 5) are not listed (no ownership data); the movie has the
  lock overlay (`StickLocked`) for them.
- DLC pack popup (`dlc_popup`, `DLC_Ok`) is off (`DLC_Locked` false).
- A pad is not at hand in headless runs: the stick / shoulder mapping above is the pause
  menu's; **to confirm with a DualSense**: L1 / R1 switch tool and pack, the left stick edits
  the tool, the right stick / D-pad orbit the camera, Select is the history.

## 9. Flags

| flag | what |
|---|---|
| `-nostickerui` | the native `PaintMenu` in the ESTICK room (the A/B) |
| `-logstickerui` | the colour cubes' transforms and the picker cursor |
| `-interior ESTICK_B -walkaim StickersShop_area -nointeriorintro` | straight into the shop, facing the seller (a scripted run presses `validate` on it) |

## 10. Evidence (headless, build 29-30, 0 stalls)

`docs/images/sticker_ui_shop_entry.webp` (the retail "Stickers Shop" title bar), `_menu` (the
five-button menu: Paint locked, Stick, Clear, Cancel, Valid & Pay), `_pack_grid` / `_pack_switch`
(real thumbnails, pack name, price, "3 / 15", L1 / R1), `_move_on_car`, `_scale_tool` (X / Y
scale strings, Invert), `_history` (Undo), `_my_colors` / `_colour_picker` (tinted cubes, the
gradient and dropper, the sticker recoloured live), `_invoice` ("Itemized invoice": Paint $0,
Stickers $5,000, Labor $1,000, total $6,000 of $100,000), `_owned_pack` (the checked pack after
paying, money $94,000). The pay round trip: `StickerShopUi: paid 6000 (4 sticker(s) on car
387), money now 94000` -> `profile.sav` `car_stickers` / `sticker_banks` / `money` -> the next
FREERIDE logs `'California' wears 4 sticker(s)`.
