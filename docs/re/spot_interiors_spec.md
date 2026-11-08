# Spot interiors: walking into dealerships, clothes shops and the other shops

Status (2026-10-29, session 45 `win`): **implemented** for every shop spot of the three islands
(`source/game/spots/interior_walk.{h,cpp}`, hooked in `GMFreeRide`). Tags: `[DATA]` read from the game
files, `[CODE]` from the readable exe, `[INF]` chosen, `[ENC]` in the SecuROM band.

## 1. Which bank is the interior of a spot [DATA]

Every shop spot of `islands/<island>/level/spots.bin` named `E<name>` has an interior bank
`interior/i<name>.bnk` (lower case). Checked against all three spot lists: every shop matches, the
exceptions are the houses (`EPH_*` -> `lph_*` living + `gph_*` garage, pool_interior_spec.md 4.1), the
hotel `E_HOTELA`, the tuning shops `ETUN_B/D/F/G/H/I` (only `itun_a` exists), the night clubs `ENC_*`,
the landing pads `ELANDCA*`, `EDI_B` and the building sites `EUC_*`.

| spot (category) | interior | what it is (source path inside the bank) |
|---|---|---|
| `ECD_*`, `ECDC_*` (1) | `icd_*`, `icdc_*` | `spots\cardeal` car dealers |
| `EAS_A..E` (3), `ESHOP_CA` (3) | `ias_*`, `ishop_ca` | `spots\avatshp` **clothes shops** |
| `ECH_IA/HA` (5) | `ich_ia/ha` | `spots\clubhous` club houses |
| `ETUN_A` (4) | `itun_a` | tuning |
| `EGAMHA` (6) | `igamha` | `spots\gamblinghall` |
| `ECW_A` / `ESTICK_B` / `ERE_B` (8) | `icw_a` / `istick_b` / `ire_b` | car wash / paint / realtor |
| `ERS_A/B/C` (9) | `irs_*` | racing schools |
| `EPHOT_B` (0x0b) | `iphot_b` | photographer |
| `ESUR_A`, `ESUR_CA` (0x10) | `isur_*` | surgery |
| `ESTOL_B` (0x11) | `istol_b` | stolen cars |
| `EHD_B` (0x13) | `ihd_b` | `spots\hairdres` |
| `EAIR_A/B` (0x16) | `iair_*` | airport |
| `EUC_*` (8) | — | `spots\under_construction` (**building sites**, not shops) |

This corrected two rows of the map icon table (`MapScreen::loadSpots`): category 3 was "auctionhall"
(it is the clothes shops) and 5 was "carrental" (club houses); `EUC_*` carried the "clothes" icon, so
the FREERIDE clothes-shop prompt sat on building sites.

The path builder itself is not in readable code `[ENC]` (pool_interior_spec.md 7.4); the rule is the
data's own naming, true for every shop.

## 2. What an interior bank holds [DATA]

The same layout as the garage / house banks the walk modes already load (`IntroCinematic`):

- a Scenery set `i<name>_sc_349E…3DD` + lightmaps (`lm_<name>_night`, `lm_fake_day`), cube maps;
- the walk collision `i<name>_sc_57E5…shk` (ECD_FERR: 660 triangles, 42 boxes, 9 capsules);
- `Entity__Camera` and **`Entity__FirstPerson { StartCamera }`** — the walk start (`cam_entree` in
  every bank checked: icd_ferr, ias_a, ihd_b);
- `Entity__Volume__FpSelectable`: `Exit` / `ExitDoor` (Type 0 "exit"), the seller (`Hostess_area`
  Type 1 "carseller", `Seller_area` Type 22 "goodies", `Hairdresser_area*` Type 29 "hairdresser");
  the type names are the walk-mode table at 0x00edd580 `[CODE]` (walk_mode.cpp);
- `Entity__PNJ` (the hostess / seller), `Entity__ShopData`, lights;
- `Entity__Cinematic`: `C_INTRO_I` (the entry shot, the player avatar walks in), `C_LIFE*` / `C_LOOP`
  (the staff idle loops), the purchase scenes (`C_BUY_CAR_I`, `C_BUY_ITEM`, `C_BUY_HAIR`) and, in the
  clothes shops, one camera per item category (`C_tops`, `C_shoes`, `C_hats` ...).

## 3. What the port does

- FREERIDE: the Enter tile (HUD.vmf `/HUD/enter/Spot`, whose frame labels are the map icon names
  `[DATA: -dumpsprite 227]`) shows for the nearest spot **with an interior bank** within 60 m;
  **A / Enter** (under 20 km/h) loads the bank as a closed room in its own frame (`bWorld = false`, the
  FIRST_WM garage setup) `[INF: the retail transition is a fade; the island is not drawn]`.
- `C_INTRO_I` plays (A / B skips), the `LIFE` / `LOOP` cinematics loop in the background `[INF: which
  cinematics the spot code starts is ENC]`; then the walk starts at the FirstPerson camera with the
  existing `WalkMode` (collision, reticle, `/:SelItemType` labels from intro.vmf `wm_home`).
- **A on the seller**: car dealers open the DBCarShop dealer menu, clothes shops the DBClothe menu
  (both over the interior); every other shop shows the GENERAL Tip box "This shop is not open yet."
  **A on the exit, or B / Esc**: back to the car, where it was parked.
- `-nointeriors`: the old behaviour (the dealer / clothes menu opens from the car).
- `-nointeriorintro`: straight to the walk. `-logselect`: why each selectable is refused.

## 4. Which shop, which stock [DATA]

`DBCarShop` (62 rows) is the table of **every** shop with stock, not only the car dealers:

| column | meaning |
|---|---|
| `_spot_name` | the exact building: `ECD_FERR_3337I` (`I` = Ibiza, no suffix = Hawai). The spots.bin record id is `GetIdentifier64bit` of this name (the rule `IntroCinematic::findHouseSpot` already uses for `EPH_L3DI_5067I`), so a row maps to one building. |
| `_slot_1..9` / `_isbuyable_n` | the cars (DBCarPhysicsData `_id_car`) |
| `_clothesshopbrand` | the clothes shops' brand, a DBBrands `_hashcode`: 0x39 Hiro Hisaishi (`EAS_A`), 0x53 MYW Paris (`EAS_B`), 0x54 Practice Sportswear (`EAS_C`), 0x55 B-Miami (`EAS_D`), 0x57 Casino (`ESHOP_CA`) |
| `_libelle_hc` | the shop's name |

Also in the table: the sticker shops (`ESTICK_B`), the used car dealer (`ESTOL_B`) and the tuners (`ETUN_*`).

- **Dealer stock**: the row of the spot entered (`FindShopBySpotId`). The old name rule
  (`FindShopForSpot`, now only the fallback) was wrong for at least `ECD_US1` (it is "World classic", the
  rule said "U.S. Cars") and `ECDC_WLD` ("U.S. Cars (north)", the rule said "World classic").
- **Display cars**: car `_slot_<i+1>` on `Entity__ShopData.PosCams[i]` of the interior bank, at rest on the
  scene floor, lit by the bank's `CUBECAR` (icd_ferr: 8 cars on `D_CAR_01..08`; icd_us1: 2 cars of 8 places)
  `[INF: the order pairing; both lists are the shop's own, the placing code is ENC]`. Rims / tires from
  `_default_rims` -> DBRims (`CinematicCar::DescFromDatabase`). `-nodisplaycars` turns them off.
- **Clothes stock**: the DBClothe rows whose `_brand` is the shop's `_clothesshopbrand`, of the player's sex
  (`_sex` 2 man = `M_*` files, 1 woman = `W_*`), without the $999,999 defaults. Hiro Hisaishi: 21 men's items.
- **Hairdresser**: the `HAIR` brand rows (0x37, the haircuts, `_parttype` 3): 27 men's cuts. No DBCarShop
  row names the hairdressers `[INF: the brand is the shop]`.
- DBBrands `_manufacturer_name` is a plain string (`HIGHLIFE` -> "Hiro Hisaishi"); the clothes list read it
  as a localisation id, so every brand name was blank before.

### Display car paint (`CinematicCar::applyDatabasePaint`)

The DB colour path of paint_spec.md 3.2 / 3.3 [CODE], completed for cars that have a colour library:

- The car's `DBCarsAndColors` rows in table order (Ferraris 23-24 rows, the 16M / Enzo 3, Mustang '68 16,
  Corvette C1 7) [DATA]; row *n*'s `_color_id_1` is a **material** of the car bank's `<car>_color.2dm`
  (`Rosso_Corsa`, `Blu_Abu_Dhabi`, `Mustang_Fastback_Ext_Noir_01` ...) or of `vehicules/colors.bnk`.
- **Look it up by hash** (`GetIdentifier64bit(id)`, FUN_00c4c0f0): the MAT. name field holds only 15
  characters (`Nuovo_Grigio_In`), so `Spider_16M_Ext_Rouge_01` never matches by name [DATA].
- Copied: Diffuse / Ambient / Specular and MATPARAM, SPECPARM, SPECBLINN, FLAKEABNS, RAMPPOW, RAMPCLR1/2/3,
  RAMPFLAKE, BLINNCLRA/B (the FUN_00c5ff20 list).
- Targets [INF, the ENC paint state]: colour 1 onto `Paint`, `Paint_SK` (skinned roof), `Paint_LR` / `_MR`;
  colour 2 (`_color_id_2`, mode 10) onto `Paint2` / `Paint2_SK` (F430 Scuderia: `F430_Scuderia_Ext_Gris_09`).
  **Not** every TD_CarPaint2 / RAMPCLR1 material: glass, chrome and trims carry RAMPCLR1, and the 16M's
  `Carbon` / `blanc` / `rouge` / `Vert` and the Scuderia's `Bande_Noir` are liveries.
- Row 0 is the colour the car's .2dm already has (Raven Black Mustang, Venetian Red Corvette) — verified
  pixel-identical to the unpainted car. The showroom choice is ENC; the port puts row *i* on podium *i*
  (Ferrari: Rosso Corsa, Grigio Titanio, Giallo, Grigio Alloy, Blu Abu Dhabi, Bianco Avus, Nuovo Blu Pozzi,
  Giallo Modena) `[INF]`. `-paintindex <n>` forces one row; `-logpaint` traces the lookup.

### The colour picker (DealerMenu)

- Two steps: the car list (A: choose a colour), then the car's catalogue — the `_color_name` of its
  DBCarsAndColors rows with the row's `_price` as an extra (row 0 "included", the others +$1,000 on the
  California) [DATA]; A buys in that paint (car price + colour price [INF]), B returns to the cars.
- In a showroom the panel stands at the right, undimmed, and the selected car is shown: the display car is
  repainted live with the highlighted row, and the camera frames it. The PosCam `Camera` fields name cameras
  the interior bank does not hold [DATA], so the shot is the port's three-quarter front view (6.5 m, 35 deg
  off the nose, eye height) [INF]; the walk camera comes back when the menu closes.
- The choice is saved: `PlayerProfile::OwnedCarPaints`, `car_paints = <id_car>:<row>,...` in profile.sav;
  whenever FREERIDE loads that car (start, `-switchcar` delivery) it is painted with it.

### The retail dealer menu (2026-10-31, session 61 `glm`)

The native menu now follows the used-car-dealer screen (`shopscar.vmf`'s mini menu, FUN_009f2c90; the
movie itself is still not driven):

- **Cars step**: the shop list at the left, the selected car's **spec panel** at the right — the
  Acceleration / Speed / Braking bars on the retail gauge ranges (FUN_00adea90, the constants gm_shop's
  fillCarVariables feeds SHOPSCAR, now shared from `car_shop.h`: accel 35..2.25 inverted, speed
  70..470, braking 170..96 inverted, handling 0..100), the **difficulty** as 5 discs (native art; the
  retail wheel icons are the movie's), and the three numbers (top speed km/h, 0-100 sec., bhp). The
  stats come from the car's `DBCarPhysicsData` row (`ShopCar::PhysicsRow`, `FillCarSpec`).
- **A** opens the retail **entries**: *Buy* (the colour picker below), *Get into the car* (the podium
  camera alone, B back), *Data* (the full data sheet: power / torque @ rpm, weight, displacement,
  engine type, gearbox, gears, 0-100, top speed, braking 100-0, quarter mile, L/W/H — the same
  columns gm_shop's fillCarVariables decodes), *Compare* (FUN_00b95840's record: the selected car
  becomes the base the spec panel and the data sheet show the others against) and *Test drive*.
- **Test drive** fills `TestDriveRequest` (player_profile.h) with the car, its default rims
  (`_default_rims`) and the colour step's choice (`_color_id_1`), `ReturnMode = GM_FreeRide` and the
  dealer's own spot + island, and enters GM_TestCar. `gm_testcar` honours the ReturnMode (the story
  shop keeps -1 = SHOP): at the end the drive's start pose (the spot's street side) goes through
  `GetStoryHandoff()` and the borrowed profile fields (`LoanCar` / `CurrentGarageSlot`) are restored.
- **The near-car spec card** (FreeRideHud, bottom right; drawn over the drive HUD *and* over the
  showroom, where the drive HUD is hidden): the display car within 5 m of the walk position
  (`InteriorWalk::findNearDisplayCar`), or a traffic car within 9 m while slowed down
  (`Traffic::findNearestAgent`, lowest priority — only when no spot / race / wash is near and no race
  is up). Name + price (`--` when the DB has none, e.g. the police cars) + top speed / 0-100 ("n/a"
  when the row's `_acceleration_0_100_kmh` is 0, as on the traffic cars) + bhp, read once per car id
  (`CachedSpecCard`).

## 5. Not done

- Empty podiums where a shop lists fewer cars than it has places (World classic: 2 of 8): the data's own —
  no `_slot_n` of any shop names a car missing from DBCarPhysicsData (`-logshops`).
- Brake callipers (`_callipers`, FUN_00c5bb40) and the interior colours (`_interior_n`) are not applied.
- The purchase cinematics (`C_BUY_*`), the clothes-shop category cameras, and the retail shop UI
  (`shopscar.vmf`, `shopsavatar.vmf`): the native menus stand in — the dealer menu's *layout* follows
  the retail screen now (session 61 above), the movie itself is still not driven.
- Houses (`EPH_*` -> `lph_*` / `gph_*`) and the casino floor (its own mode): not entered here.
- The interior is not placed in the island (no view out of the windows into the real world); its own
  frame is enough to walk it. — **Shops: done** (2026-10-30, session 51 `glm`): `InteriorWalk::enter` carries the
  spot bank name + island, the room is placed at its spots.bin record (the id hashes the **full** name,
  `EPHOT_B_3337I` on Ibiza / `EPHOT_B_9830` on Hawai [DATA spots.bin], while the walk carries the record's 8 byte
  bank field `EPHOT_B` — `findHouseSpotIn` matches that field when the island is pinned) and the island streams
  radius 1 around it (`Setup::bStreamWorld`; `-nointeriorworld` is the closed-room A/B). Verified in the photo
  shop: the neighbour building renders through the window openings. Still open: the ocean stays off (keyed on
  `bWorld`), the house rooms keep the closed-room frame, and the entry is one synchronous 9-sector load (~1.9 s
  inside the spawn-frame update max — retail fades over it; async streaming would be the follow-up).

## 6. The dealers through SHOPSCAR (session 2026-11-05, `win`)

The owner: *"dealerships can we plug correct ui believe uses same as the used car dealership"*.
Right: every car dealer is the SHOP mode (mode 3) with SHOPSCAR.vmf - the story used-car
dealer (`gm_shop.cpp`) is one case of it. At a `cardealer` door FREERIDE fills
`FreeRideShopRequest` (DBCarShop `_spot_name`, spot bank, island) and switches to SHOP
(`GMFreeRide::enterDealerShop`; `-nativedealer` keeps the walk + native panels). GMShop
then loads `interior/i<bank>.bnk` + `islands/<island>/exterior/<bank>.bnk`, the spot's
DBCarShop slots, and opens the movie page by the spots.bin type: **0x11 -> `scr_open`**
(used, the "Used Car Dealer" header) [CODE], **1 / 2 -> `car_dealer`** [DATA: the buttons
test `/:WhatPage == "car_dealer"`; the choice for the new-car types is INF]
(`car_dealer_classic` exists, unused). `-shoppage <label>` is the A/B.
- Intro `C_INTRO_I`, the walk from the bank's FirstPerson camera, no tutorial, no goal.
- The car under the reticle: price, money, spec panel; validate: **Buy / Get into the car /
  Data / Compare / Test drive** (`docs/images/dealer_shopscar_car_dealer_menu.webp`).
- Buy -> `buin` -> `C_BUY_CAR_I` (the hostess) -> FREERIDE at the spot entrance **in the new
  car** (OwnedCars + paint + `DrivenCarId`, COLLECTION_CARS_DIF_n) - so "Get into the car"
  after a purchase is no longer the old dead end (`docs/images/dealer_shopscar_buy_drive_out.webp`).
- Back at the walk -> the "leave?" dialog -> `gofr` -> FREERIDE at the entrance.
- Test drive: the existing TESTCAR round trip (back to the same dealer).
- Hawai rows have no `I` suffix (`ECD_FERR_7841`); `-shopspot <prefix>` picks the island
  from it. Test hooks: `-gm SHOP -shopspot ECD_FERR_3337I -shopaim <n>`.

## 7. The buy window: wheels, paint, interior (session 2026-11-05, `win`)

The owner: *"when buying a car now there's no sales assistant and you can't pick the cars colour,
interior or rims"*.

**The hostess.** Every new-car dealer bank holds three cinematics: `C_INTRO_I` (Ferrari: `C_INTRO`),
`C_BUY_CAR_I` and **`C_LIFE_HOSTESS`**, the hostess's idle at her desk (14 of 14 `interior/icd*.bnk`,
`otdu2_cine_tool list`) [DATA]. The free-ride loop search did not know that name, so the walk ran with no
background loop and the desk was empty. It is first in the list now, and `C_INTRO` is the intro fallback.
`docs/images/dealer_hostess_at_desk.webp`.

**The window.** In SHOPSCAR, *Buy* does not buy. It sends `buin`, and the movie's `/buy/window_buy` then
asks the game page by page. The dealer FSCommand handler `FUN_009f0840` is readable [CODE]:

| command | handler | what it does |
|---|---|---|
| `buin` | `FUN_009f03a0` | open: page state `this+0x1fac` = 1, previous `+0x1fb0` = 0; `/:wheels`, `/:paint`, `/:material` and the three option prices |
| `neop` / `prop` | `FUN_009ef3a0(1 / 0)` | the page being left takes its item (its chop), then the next / previous page |
| `chop` | `FUN_009ef1e0` | the item of the page changed: page 2 `FUN_009f28a0` (rims), 3 `FUN_009f3300` (paint), 4 `FUN_009f37a0` (interior); then `/:isRimsChoice`, `/:isPaintChoice`, `/:isInteriorChoice` = more than one item |
| `buou` | `FUN_009edf40` | closed (selection `+0x1ba344` = -1) |
| `eval` / `buyc` | `FUN_009eeaa0(1 / 0)` | price check / the purchase, from the confirm page |

Page states (jump tables `0x9f0354`, `0x9f0368`, `0x9f037c`) [CODE]: **1** the car (back out:
`/buy/rewind_buy` gotoAndPlay `loop`, `/:rewind_buy` 1), **2** wheels, **3** paint, **4** interior
(only when `FUN_009ee090` says there is a choice; [INF] more than one interior for the colour),
**5** confirm. Next: 1->2, 2->3, 3->4 or 5, 4->5. Previous: 2->1, 3->2, 4->3, 5->4 or 3. Bikes
(`FUN_00d8fe40`) skip wheels and interior. The game writes, per page [CODE]:

- wheels: `/:num_option` 1, `/:has_colors` 0, `/:Option_title` = DB text 0x1df, `/:IsRimsFirst` /
  `/:IsRimsLast`, `/:num_item` (1-based), `/:nb_items`, `/:Option_name` = the DBRims `_displayname`
  ("n/a in DB" without a row), `/:has_text` 1;
- paint: `/:has_colors` 1, `/:num_option` 2, title 0x1e9, `/:num_item`, `colors/square` on the colour's
  frame, `/:nb_items`, `colors` on the count's frame, the `_color_name`, `/:has_text` 1;
- interior: `/:has_colors` 1, `/:num_option` 3, title 0x1ce, the item and the frames, `/:Option_name` and
  `/:material` = the DBInterior name, `/:has_text` 0;
- confirm: `/:NeededPrice`, `/buy` gotoAndPlay `confirm`, `/:Message` = DB text 0x150;
- then on every page: `/buy/window_buy/op_price_<1|2|3>/:op_price` (rims / paint / interior `_price`),
  `/:cur_op_price`, the total (`FUN_009f25f0`).

The data [DATA]: the rims are the car's `DBCarsAndRims` rows (`_rim` = DBRims `_hashcode`), the paints
its `DBCarsAndColors` rows in table order, the interiors the colour row's `_interior_1.._interior_15`
(DBInterior `_hashcode`; 1 = "None"). The price is `FUN_00d8d140`: car + rims + paint + interior.

**The colour squares.** `/buy/window_buy/colors/color1..24` are placed at 1 % alpha. The exe paints
them from native code (`FUN_009ee3c0`, a render callback keyed on the clip name `color<n>`) [CODE]. The
port tints them with `FlashPlayer::setClipColor`: paint = the colour material's `RAMPCLR1` from the
car's own colour library, then `colors.2dm`; interior = `_interior_color_1` in `colors.2dm`, which has
none of the `C_*` ids, so the interior squares are a neutral grey [INF].

**Two departures from the exe, both [INF]:**
- The movie shows `colors` with its own script (`this.window_buy.colors._visible = /:has_colors === 1`,
  in the constant pool at file 0x911fe of `frontend/hires/shopscar.bnk`) [DATA]. That script is not
  reached in the port, so the game sets `_visible` itself on the paint / interior pages.
- The exe also does `/buy` gotoAndPlay `next` on the wheels page going back, on the paint page, and going
  forward to the interior (vtable +0xdc). In the port that jump leaves the page's arrow keys dead
  (`-logbuywindow`: `taken 0` on every press after it), and the movie's own `A()` / `B()` already play the
  transitions, so it is not done. `FlashPlayer::gotoLabel( ..., bRerunSameFrame )` stays in for the
  `confirm` jump.

**Saved.** A purchase stores `PlayerProfile::OwnedCarRims` (`car_rims = <id>:<DBRims hash>`) and
`OwnedCarInteriors` (`car_interiors`) next to `car_paints`. FREERIDE loads the ordered rims
(`CinematicCar::FindDatabaseDesc`).

Verified (`-gm SHOP -shopspot ECD_AUDI_1276I -shopaim 6 -money 500000 -playername BuyTest`, presses in
`docs/state/main.md`): R8 V10, 5 rims / 12 paints / 13 interiors; rims 2 (+$880), paint 3 "Ice Silver
with Apollo silver sideblade" (the podium car repaints live), interior 2 "Black Fine Nappa leather (red
couture)"; confirm -> `eval` -> `buyc` -> `bought ... colour 2, rims 1, interior 1 for 145880`; the next
FREERIDE: `driving the profile's car 442 'R8_V10', rims audi/r8_spyder_f_03 (ordered at the dealer)`, in
paint 2. `docs/images/dealer_buy_window_rims.webp`, `_paint.png`, `_interior.png`, `_confirm.png`.

Not done: the podium car does not swap its rims while browsing (a live rim model swap is not built); the
interior is saved but not drawn (no cockpit material swap); the per-page camera `FUN_009f2d80(car, page)`
is not ported, so the view stays on the car as it was; the rims picture box of the window stays empty.
