# The retail tuning shop UI (SHOPSCAR.vmf's car_tuner page)

Written 2026-11-18 (`main`). The owner: *"please work on the tuning shop menu ui"* (earlier, 2026-11-17: the
shops are "not using the correct menus"). The native `TuningMenu` (`tuning_menu.*`) is now only the
`-notuneui` A/B; the ETUN_* rooms run the retail screen. Tags as in `CLAUDE.md`: `[DATA]` the AVM1 of
`shopscar.vmf` (`tools/vmf_disasm.py`), `[INF]` chosen, with the reason.

Screenshots: `docs/images/tuning_retail_ui.webp` (squares, Acceleration lines, Rims page).

## 1. How the room runs it (`InteriorWalk`, `bTuneUi`)

- `ETUN_*` spots set `UiMovie = "shopscar"` (the same movie as the dealers / sticker shop). `startWalk` goes to the
  root label **`after_mkt`** (f183): `WhatPage = "car_tuner"`, `FSCommand:afmk`, `UpgDone = false`, the cursor and the
  title bar open, then **`tuner_stop`** (f194, a `Stop`; it defines `B()` = the "Exit Shop?" question, DBXO `tun_out`).
- The seller (`Seller_area`, FpSelectable type 23 -> `npc_tuner`): the port's A is `ItemType('valid')`; the movie
  answers `StartButtonStatus`, sends `shou` + `camn` and calls `OpenTuner()` (the squares menu).
- `camn` / `wmof` switch the walk input off, `wmon` gives it back (as for the stickers). B in the walk calls the root `B()`
  (the movie sets `/:Clip` only after a page was up). `gofr` = leave the room.
- `dbxo` / `dbxc` open and answer the GENERAL dialog box (`DialogBoxSource` SHOPSCAR) exactly as the dealer does.

## 2. The FSCommand protocol and what the game answers (`TuneUi`, `freeride/tune_ui.*`)

The movie's variables are `_root`'s (`/:Name`). The page runs with `_root.fake` unset: every value comes from the game.

| command | sent by | the game answers |
|---|---|---|
| `afmk` | label `after_mkt` | binds (car = the one you drove in with), pushes the car sheet, money, square locks |
| `camn` | the tuner's A | pushes them again |
| `amtv` | a square was chosen, `/:UpgType` = `acceleration` / `top_speed` / `braking` (`rims` sends none; it goes to `setRimsMenu`) | pushes the four lines (below) |
| `kida` | focus moved to another line, `/:numUpl` (**0-based**: line 0 = "Level 1") | pushes the `new_*` gauges + `*_upg` sheet of that level |
| `evmo` | A pressed on a line / the rim | sets `/:EnoughMoney` (and `/:already_buyed` on the rims page) - the movie reads them right after |
| `dbxo` | the movie (case `tun_valid` "Buy this upgrade?", `rims_valid`, `tun_out`) | the shared dialog box |
| `kiva` | the buy dialog was confirmed, `/:UpgradeSelected` (0-based) | buys that level |
| `tune:rims:open` / `chop` / `tune:rims:vali` / `tune:rims:clos` | the Wheels page | see 4 |
| `tuci` | the menu closed after a purchase (retail plays `C_BUY_PART`) | not played: the game calls the movie's `BackToWM()` at once |

## 3. The variables

- Car sheet: `/:Current_car`, `/:cur_group` (the dealer's category table), `/:cur_difficulty`, `/:IsBike`,
  `/:isCarTunable`, `/:cur_upgrade` (`level0..5` badge = the levels bought, capped at 5 [INF]),
  gauges `/:cur_accel|cur_speed|cur_braking` and `/:new_*` (0..100, `CarStatGauge` on the dealer's ranges),
  the sheet texts `/:cur_max_speed_m|i`, `cur_0_100_m|i`, `cur_power_m|i` and their `_upg` twins (the previewed level).
- Money: `/:MyMoneyStr`, `/:money` (the retail `$§1§` text, `LocString 0x3a4`), `/:MyMoneyInt`.
- Squares (`setSquareMenu`): `/:LockUpgAcceleration|TopSpeed|Braking|Handling|Rims`, `/:isNoRims`. The four squares are
  Acceleration (gearbox icon), Speed (engine), Braking (disc), Wheels.
- Lines (`setUpgList`): four lines `level1..level4` read `/:LockUpgLevel<0..3>`, `/:UpgName<0..3>`, `/:UpgPrice<0..3>`.
  Lock state **0 = buyable, 1 = padlock, 2 = bought (wrench icon)** [DATA, confirmed on screen]. The port has levels 0..3
  (`PlayerProfile::OwnedCarTuning`, the DBRewardCar range): line n is level n; the next one is 0, the ones after are 1, the
  fourth is always a padlock with no price [INF]. Prices: $12,000 x level; rims $8,000 [INF, the native menu's].
- Rims page: `/:nb_items`, `/:num_item` (1-based, the installed size), the two text fields **`/:option_name`** (the tire label,
  e.g. `21" 245/30`) and **`/:cur_op_price`**. `Rims_Text` / `Rims_price` are *clips*, not text. The movie re-sends
  `tune:rims:open` after its confirm dialog - the choice stays.
- The gauges follow `ApplyTuningToSetup`: torque +8 % per accel level, top-end torque +10 % and the rev band +5 % per speed level,
  braking grip +10 % per braking level [INF - the port's own modulation].

## 4. Not done / next

- The Wheels page's big preview area stays empty (the retail page loads a rim picture per item - no source found).
- **Owner leads (2026-11-18):** (1) the Wheels page preview is probably a second camera view, as in the dealership (the movie leaves the area empty for a 3D view of the car), not a picture per rim - look at how `GMShop` drives the dealer's second camera. (2) The car should be on the lift inside the shop (`itun_a` has `C_BUY_PART` / `C_CAR_SELECT`); the owner will check the 360 recomp for how it is placed.
- **Owner confirmation from retail (360) screenshots, 2026-11-18** [INF: owner's observation, not yet traced in code]: (a) the car **is on the lift** in the tuning shop, behind the menus. (b) The **Rims** page left panel shows a live 3D close-up of the wheel on the car (rim fitted, "Already paid" label overlaid), i.e. the same second-camera technique as the dealership. (c) The preview camera **animates left-to-right and back in a loop** (a slow pan across the wheel); the dealership does the same for its interior shot. (d) For **car paint** the camera instead **orbits the car**. Build target: a preview camera mode per page (wheel pan loop / orbit), rendered into the movie's preview rectangle; the car is spawned on the lift at the room's lift locator.
- Handling has no square level; `C_BUY_PART` / `C_CAR_SELECT` are not played, the car is not on the lift (the car stays outside).
- The upgrade dialog shows only OK (the movie's cancel button needs `/:YesNo`; the shared dialog path is the dealer's).
- Retail has four levels per stat; the port's physics has three, so the fourth line is a padlock.
- Not checked: the Hawai tuning shops (same bank `itun_a`), the `ETNR_*` tuner-garage mod rooms (unchanged: they do not match `ETUN`).
