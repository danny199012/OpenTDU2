# The world map's spot icons and their discovery (`map_spots_spec`)

What the world map screen (`MapScreen`, `source/game/map_screen.cpp`) draws for the
`spots.bin` buildings, where each dealer's pin logo comes from, and the discovery law
that hides every icon until the car has been near the building. Decoded 2026-11-10;
the owner's ask was *"some of the dealership icons on the map are missing"* and *"no
icons show on the map … once you've discovered them"*.

## 1. The spots and their icons (`loadSpots`)

`islands/<island>/level/spots.bin`: `".SPT"`, `"SPTA"` `{ u32 size }`, then 0x110-byte
records — +0 u64 record id (= `GetIdentifier64bit` of the building's full name,
`ECD_FERR_3337I`…), +8 the 8-char bank name, +0x10 the position, +0x100 the category
byte [DATA]. The category picks the icon (a frame label of MAP.vmf's
`pack/point/spot` clip) and the tab row the icon belongs to:

| cat | bank | icon label | tab |
|---|---|---|---|
| 0x00 | `EPH_*` houses (owned → `home`) | `estates` | 2 |
| 0x01 | `ECD_*` / `ECDC_*` car dealers | `cardealer` + brand logo | 3 |
| 0x03 | `EAS_*` clothes | `clothes` | 2 |
| 0x04 | `ETUN_*` tuning | `tuning` | 3 |
| 0x05 | `ECH_*` clubs | `club` | 2 |
| 0x06 | `EGAMHA` gambling halls | `gamblinghall` | 2 |
| 0x08 | `ECW_*` wash / `ESTICK_*` paint / `ENC_*` club / `ERE_*` estates | `carwash` / `carpaint` / `club` / `estates` | 3/3/2/2 |
| 0x08 | `EDI_B` diners, `EUC_*` building sites | *no icon* | — |
| 0x09 / 0x0b | `ERS_*` race schools / `EPHOT_B` photo studio | `rschool` / `photograph` | 0 |
| 0x0e | `ELANDCAA` the casino seaplane dock | `gamblinghall` | 2 |
| 0x10 / 0x11 / 0x13 / 0x16 | surgery / `ESTOL_B` used cars / `EHD_B` hair / airport | `surgery` / `stolencar` / `hair` / `airport` | 2/3/2/3 |
| 0x19 | `E_HOTELA` the story hotel | `home` | 2 |

Census: Ibiza 79 records → 75 icons (the 4 `EUC_*` sites), Hawai 144 → 115 (the 25
`EDI_B` diners + the 4 `EUC_*` sites). The diners staying
icon-less is **faithful**: MAP.vmf has no diner/food kind (its kind set is
`f_bikeDealer f_carDealer f_carDealerClassic f_carPaint f_carWash f_clothes f_club
f_estates f_hair f_home f_stolenCar f_surgery f_tuning` + the tab-0 kinds [DATA:
strings]). `bikeDealer` / `carDealerClassic` exist in the movie but no base-game
`spots.bin` record or `DBCarShop` row uses them (the bike dealers are the DLC hook;
bikes exist in `DBCarPhysicsData` only as traffic rows).

Tabs: 0 "Single Player Challenges", 2 avatar spots, 3 "Spots for Vehicles" — the
tab titles are the movie's own; `updateSpots` draws only `spot.Tab == currentTab`.
The movie reports the tab with the `bmap:fltr:icf<N>` FSCommand. `-maptab <n>` pins
the game's tab (a test hook; the movie's own lit row still follows its clicks).

## 2. The dealer pin's brand logo (the "missing dealership icons")

Every dealer pin shows **one brand logo**: a 64 px cell of the 1024x256
`brands_map` atlas (`AllRes/LogoTexturePage.bnk`, 16x4 cells in
`DBBrands._logo_global_index` order — verified cell-by-cell against the atlas
render). The map binds the cell through `pack/point/spot/ic/logo` frame
`_logo_global_index + 1` + `setBitmapOverride` on the `lure` quad; the free-ride
HUD's world markers do the same through `Spot/ic 'brands'` (`freeride_hud.cpp`).

The retail pick per dealer is **hand-authored** — not derivable from the stock
("British Cars" sells six Aston Martins and retail pins it Lotus; "Asian Cars" is
two Nissans and a Subaru, retail pins the Subaru; "U.S. Cars (north)" has two
Chevrolets and two Fords, retail pins HUMMER). The port reads the retail picks off
the owner's grabs of the "Spots for Vehicles" tab (both islands, the user provided
them 2026-11-10; positions matched to spots.bin by an affine fit on the
unambiguous pins — Mercedes/Ferrari/Ascari/Spyker all land within a few pixels):

| dealer bank | retail pin | `DBBrands` hash | logo cell |
|---|---|---|---|
| `ECD_AUDI` | Audi | 0x5 | 4 |
| `ECD_FERR` | Ferrari | 0xf | 12 |
| `ECD_MERC` | Mercedes-Benz | 0x1b | 24 |
| `ECD_BRI1` "British Cars" | **Lotus** | 0x17 | 20 |
| `ECD_BRI2` "British Cars (west/east)" | **Ascari** (2 of 8 cars) | 0x3 | 2 |
| `ECD_EUR2` "European Cars (west)" | **Spyker** (3 of 7) | 0x27 | 36 |
| `ECD_ITAL` "Italian Cars" | **Pagani** (4 Alfa / 4 Pagani) | 0x1f | 28 |
| `ECD_ASIA` "Asian Cars" | **Subaru** | 0x44 | 50 |
| `ECD_US1` "World classic" | **Chevrolet** | 0x8 | 7 |
| `ECD_US2` "U.S. Cars (south)" | **Dodge** (both cars) | 0xb | 9 |
| `ECDC_WLD` "U.S. Cars (north)" | **HUMMER** | 0x3f | 48 |

`ECD_EUR1` "European Cars (east)" (4 RUF / 2 VW), `ECDC_ITA` "Italian Classic"
(2 Ferrari / 1 Lancia) and `ECDC_EUR` "European Classic" (Lotus/Jaguar/300 SL) are
not settled by the grabs; they fall to the **stock law**: the most-represented
brand of the spot's `DBCarShop` cars (tie → the priciest car's brand) — RUF,
Ferrari, Mercedes-Benz [INF]. The brand resolves through
`DBCarPhysicsData._car_brand` (the `DBBrands` row hashcode — the
`_manufacturer_name` display strings are not id-safe: the Charger's manufacturer
name is "Charger"). Anything unresolved keeps the "Generic Car" cell (62).
Evidence: `docs/images/map_dealer_brands_retail_vs_port.webp` (retail grab over the
port's `-cheat allspots -maptab 3` run — the same pins).

The same 14 dealer banks exist on both islands with the same stock, so one table
serves both. The HUD world markers inherit the logos (same `Spot::LogoIndex`).

## 3. Discovery (`discoverNear`, the KnownSpots save)

The retail law the owner asked for: **a spot's icon shows on the map only once the
car drove within discovery range of the building.** The state is the profile's
`KnownSpots` (per island, the spots.bin record ids) — the port's stand-in for the
save's own undecoded `KnownSpots` section (`docs/re/savegame_spec.md` 34), saved as
`knownspots_<island> = id,id,…` in `profile.sav`, the `ExploredRoads` pattern.

- `GMFreeRide` calls `discoverNear(carPosition)` every drive frame (next to
  `trackCar`). Range 150 m [INF: the retail radius is not decoded; it covers the
  forecourt from the road]. Each new spot logs `MapScreen: discovered '<bank>'
  (<icon>)` and the profile saves (`map: spots discovered`).
- Auto-known (they draw from the start): the houses the player owns (their `home`
  icon) and the `home` spots — the story hotel [INF: the retail seeding of
  KnownSpots on a fresh profile is not decoded].
- `buildSpots` clones only the discovered spots (`N spot icons created (M
  undiscovered hidden)`); `updateSnap` skips undiscovered spots, so the cursor
  does not snap to them and fast travel cannot target them — the same refusal
  rule the undiscovered roads already had. The HUD's floating world markers and
  the enter prompts stay unfiltered (they are the proximity mechanism itself:
  driving past a shop is what discovers it).
- The cheats panel (F4) has an **All spots** delock row (the retail dev menu's
  shape; `MapScreen::unlockAllSpots`, the `KnownSpots` twin of ALL ROADS) and
  `-cheat allspots` does it from the command line; the panel's status line reads
  `spots: <discovered>/<total>`.

Evidence, all `build 12`: fresh profile `map_discovery_fresh.webp` (zero icons on
the vehicles tab; the old build drew stray tiles there), `map_discovery_one_pin.webp`
(teleported within range of the Ferrari dealer: `discovered 'ECD_FERR'` → its pin,
and only it, draws; the save round-trips — a later run reloads `3 discovered`).

## 4. Open / [ENC]

- The retail per-dealer logo table is hand-authored in the encrypted band; the
  grabs settle 11 of 14 dealer banks (the stock law covers the rest).
- The discovery radius, the fresh-profile seeding and whether retail dims (vs
  hides) undiscovered icons are [INF]/undecoded — the owner's grabs show only
  discovered pins, and the owner's own description is the hide law.
- `carDealerClassic` / `bikeDealer` kinds: in the movie, unused by the base data
  (the bike dealers are DLC data the port does not read).
- Hawai's pins were not screenshot-verified (no retail Hawai grab beyond the
  owner's one, which matches the same bank set); the law is island-independent.

## Bike dealerships and the map's OK menu (2026-11-15, `main`)
- **The bike dealerships are in the spot list as building sites**: `EUC_BDCL` (23119, 16659: the Ducati / Harley-Davidson showroom the owner found) and `EUC_BDAS` (31932, 10098), category 0x08 like the other `EUC_*`. The bank
  `euc_bdcl.bnk` is the building; the data also has the showroom interior `interior/ibd_cla.bnk` (`spots/bikedeal/bd_cla`, cinematics `C_LIFE_HOSTESS` / `C_INTRO_I` / `C_BUY_CAR_I`, 3 selectables: two exits and the hostess) and the exterior
  `ebd_cla.bnk`, both unused by the base spot list (the bike dealers are the DLC hook). `-logspots` lists the records without an icon. Now `EUC_BD*` get the `bikedealer` icon (a label the movie has) and
  `InteriorWalk::InteriorBankFor` maps them to `ibd_cla`, so the enter prompt works and the showroom opens (no bike stock - no DBCarShop row, the bikes are DLC data - so every podium D_BIKE_01..08 carries the one bike of the data, Default_Bike wearing the `diavel` model through `Desc::ModelBank`, docs/re/bikes_spec.md; verified: 8 of 8 placed, screenshot checked by eye; nothing can be bought). The satnav enter tile wears the car dealer tile
  (the movie's Spot tile has no bike art). NOT done: bike stock / buying, the map icon art for `bikedealer` (not checked on the map), `EUC_BDAS` has no interior of its own (it opens `ibd_cla` too).
- **OK on the map opens a menu** (`MapScreen::openMenu`, native panel, the retail wording from the localization): "Go there" (loc 424, only on a discovered road: the old F5 rule), "Target this spot" (1694) / "Remove target" (4304),
  "Bookmark it" (3314) / "Remove bookmark" (3914). Up / Down, OK runs the row, Back closes only the menu. Bookmarks are saved per island (`bookmarks_<island> = x:z,...`, at most 20 [INF]; loc 6853 "Bookmarks limit number reached",
  4059 "New favorite place added in this tab") and drawn as a yellow diamond. The retail movie has a bookmarks tab (`m_MapHsysTab5`, `m_Bookmarks_`) that is not driven; the GPS target logic is the old `toggleTarget`. `-locfind <word>` finds loc ids.
- Verified headless (rendering on): the menu rows, a bookmark added (saved + diamond on the map) and removed, a target set. NOT verified: "Go there" in a real session (it needs a discovered road).

## 5. The dev build's spot-kind code (2026-11-15, `main`) [CODE-dev]

`TestDrive2DEV.exe` (Apr 2012, `.text` not encrypted; `Spots.cpp`) holds the readable twin of the code that turns a spot into a map marker. Retail's copy is `[ENC]`.
Addresses are the dev exe's. Reproduce with `python tools/devbuild_disasm.py --dis 0x970030 40` (no `-I`: it needs the user-site pefile / capstone).

**Category byte -> kind name** (`0x970030`, `movzx eax, [spot+0x79]`, `cmp 0x19`, jump table `0x970158`). The 0..0x19 range is the `spots.bin` +0x100 category byte, and every row the port had decoded matches:

| cat | dev kind string | note |
|---|---|---|
| 0x00 | `home` / `forsale` | `home` if the player owns it, else `forsale` (calls `0x402261` on the owner lookup) |
| 0x01 | `carDealer` | |
| 0x02 | `bikeDealer` | the DLC hook (matches the `EUC_BD*` sites above) |
| 0x03 | `clothes` | |
| 0x04 | `tuning` | |
| 0x05 | `my_club` / `club` | `my_club` if it is the player's own club (a `0x636c616e` "clan" lookup), else `club`; `ECH_` prefix is also tested (`0x970000` region) |
| 0x06 | `gamblingHall` | |
| 0x08 | sub-kind on a second byte `[spot+0x78]`, table `0x9701c0` (below) | |
| 0x09 | `rschool` | |
| 0x0a / 0x0c / 0x0d | `mission_spot` | single-player challenge pins |
| 0x0b | `photograph` | |
| 0x0e | `casino` | the port's `ELANDCAA` seaplane dock |
| 0x10 | `surgery` | |
| 0x11 | `stolenCar` | |
| 0x12 | `gasStation` | no record in the base `spots.bin`; no icon in MAP.vmf |
| 0x13 | `hair` | |
| 0x14 | `race` | no base record |
| 0x16 | `airport` | |
| 0x17 | `ORC` | no base record |
| 0x18 | `auctionHall` | no base record |
| 0x19 | `hotel` | the port draws it with the `home` icon |
| 0x07 / 0x0f / 0x15 | default (`0x148a4fc`) | unused values |

Category 0x08 sub-kind (`[spot+0x78]` = record byte **+0x101**, see section 6): 0 `carWash`, 1 `carPaint`, 2 and 3 `carRental`, 4 `estates`, anything else = no kind. Checked against both islands' `spots.bin`: `ECW_*` = 0, `ESTICK_*` = 1, `ERE_*` = 4,
and `EDI_*` (diners), `EUC_*` (building sites incl. `EUC_BD*`) and `ENC_*` are all **9** -> no kind in the dev code. No base spot uses 2 / 3 (`carRental` is unused data). So the port's `club` icon for `ENC_*` has no support in the dev code:
the dev build gives it no pin. [CODE-dev + DATA]; the retail movie was not checked for what it shows there.

**Kind -> tooltip title** (`0xca7350`): the map movie reports the highlighted pin in the Flash variable `/:WhatSpot`; this function string-compares it with `bikeDealer`, `carDealer`, `carRental`, `carWash`, `estates`, ... and picks the title and the tab: "Bike Dealer", "Car Dealer", "Car Rental", "Car Wash", "Realtor", "goodies".
The `f_*` movie labels (`f_carDealer`, `f_carDealerClassic`, `f_bikeDealer`, ...) are separate frame names.

**What `SpotWorld.bin` is** (`euro/bnk/scenarics/spotworld.bin`, 921 128 bytes; the writer is `0x895890`, which opens `SpotWorld.bin` with `"wb"` and also writes a big-endian Xenon copy, logging to `SpotWorldErrors.log`): it is the **challenge** table, not the shop pins.
Header `SPTW` v3, then 33 `CONT` v1 blocks (one `LICE` position + `IGE2` records each); 532 records in all. Each record: a name (`RSC4T01`, ...), a world position (3 floats), an id, and a big-endian `IGE2`/`MDSC` property block
(91 keys, `key[4] 03 sub 00 size value`): `isla`/`leme` island and level, `mnme` mission name ("Mission Honolulu"), `DesC` ("OAHU"), `nblp` laps, `cops`, `Gmin`/`Gmax`, `FiMi`/`FiMa` (finish bounds), `CarB`/`CarM` car brand/model,
`CpTs` checkpoint array, `fee1` entry fee, `rewm` reward money. It feeds the tab-0 "Single Player Challenges" pins (`mission_spot`); a `spots.bin` record with cat 0x0a/0x0c/0x0d points at it. Only the first record was decoded by hand; no loader in the port yet.

**Ruled out:** the five readers of the `Level/%s/spots` string (`0x99f36c`, `0xa8bd90`, `0xac68d0`, `0xb099b1`, `0xbe17ee`) are cutscene / resource-bank loaders (`C_FIRST_OUTRO`, `C_OUTRO`, `C_TRUCK`), not marker placement.

**Still open:** the code that reads `spots.bin` itself into the +0x78 / +0x79 / +0x10 fields (the loader, then the discovery test), and the pin drawing in `HudMap` (`0xc9e584`..). The `SPTA` string at `0x96b084` is a start.

## 6. The dev build's spot loader, record layout and pin pool (2026-11-15, `main`) [CODE-dev]

**Loader** `0x96b050`: checks `.SPT` magic, version 2, a flag word (0 or 1), the `SPTA` chunk at +0x10, count = `(chunk size - 0x10) / 0x110`, records from +0x20; each record builds a 0x150-byte Spot object through `0x972880`
(allocator tag file `Spots.cpp` lines 0x2f3 / 0x309). Objects go in a list at `[mgr+4+i*4]`, count at `[mgr]`, in the file's record order.

**The 0x110-byte record** (`0x972880` copies it into the object):

| rec | -> obj | meaning |
|---|---|---|
| +0x00 u64 | +0x10 | spot id (the `GetIdentifier64bit` of the full name; this is what the save's `KnownSpots` holds [INF]) |
| +0x08 u64 | +0x70 | the 8-char bank name |
| +0x10..0x18 3 floats | +0x20 | position (w = 1.0 at +0x2c) |
| +0x1c..0x24 / +0x28..0x30 | +0x44.. / +0x50.. | two more vec3 (orientation / entrance; not decoded) |
| +0x34..0x37 4 bytes | +0x30 / +0x34 / +0x38 / +0x3c | four byte flags (unknown) |
| +0x38 float | +0x60 | a radius or scale (unknown) |
| +0x40 64 bytes | +0x7a | copied as a string but the data is binary (u16 pairs such as `12 01 13 01 23 01 ...` in the `ECD_ASIA` / `EAS_D` records: probably a list of ids) |
| +0x80..0xff 8 vec4 | +0xc0..0x13f | eight 4-float rows (entrance / parking transforms?; not decoded) |
| **+0x100 byte** | **+0x79** | **category**, the switch of section 5 |
| **+0x101 byte** | **+0x78** | **sub-kind**: only read for category 8. For the others it is a stable per-bank class index, identical on both islands (dealers: FERR 0, MERC 1, US2 4, ECDC_EUR 5, US1 6, ECDC_WLD 7, ITAL 9, BRI1 10, ASIA 11, EUR1 12, ECDC_ITA 13, AUDI 14, EUR2 16, BRI2 19; clothes `EAS_A..D` 15..18; tuning `ETUN_A` 0, `D` 1, `F` 5, `B` 6, `G` 7, `I` 8, `H` 17). Probably the join key to a DB row [INF] |
| +0x102 u16 | +0x40 | unknown |
| +0x104 byte | +0x42 | unknown |
| +0x105 byte | +0x141 | boolean (nonzero sets it); seen as 1 on the Ibiza `ECD_MERC` record |

**Discovery store.** The save component holds `KnownSpotsCount` (clamped to **0x1c2 = 450**), the 8-byte ids (`KnownSpotsHashcode`) and a `KnownSpotsClub` flag byte at +0xe14
(`0xa39ba0`..`0xa39cde`, a (de)serializer; `0x9ffa57` / `0xa0110b` are the same reflection). The port's `KnownSpots` matches that layout; the 450 cap is not enforced by the port.
The distance test that adds a spot is not found yet (it is game code, not these reflection functions).

**Pin pool** (`0xad94a0`, the map screen's per-frame update; the Flash clip array is at `[this+0x1c71e0]`, stride 8): the map movie has a **fixed pool of `/pack/point%02d` clips**, 0x33 = **51** by default, 0x7a = **122** when the byte `[0x1b1d351]`
is set. The screen keeps an item list (count `[this+0x1b6bd4]`, items at `[this+0x1b6be0]`, stride 0x80) and item i goes into clip i through `0x43567f` (item, clip, scale; the selected item gets one scale, the others 1.0).
`/pack/pointcha%02d` clips (`0xb0ce80`) are the challenge pins; `/pack/point%03d` + `pointfinish` (`0xb2c482`) belong to another screen.
So the dev map shows at most 51 (122) pins at a time, while the port clones every discovered spot. Hawai has 115 icon spots, so the 122 mode is likely the large map [INF: the meaning of `[0x1b1d351]` is not decoded].

**Not found yet:** the code that fills the item list from the spot list (tab filter, discovery distance test, the 450-cap writer) and what `[0x1b1d351]` is.

## 7. The pin popup, "new spot" and favourites (2026-11-15, `main`) [CODE-dev]

The Spot class vtable is `0x14d7cd8` (installed by the constructor `0x972880`). Slot 7 = the category -> kind switch of section 5 (reached only through this vtable, so no direct caller exists); slot 4 = type id (the base spot returns 1);
slot 3 / 10 / 14 are small getters; **slot 12 (`0x96ee20`) is `isNew`**: `category == 9` (race school) -> true when the player-data array at `[playerdata + 0x3e48 + sub*4]` is 0 (that race school, sub-kind 0..2 = the three `ERS_*` banks, has no progress yet); every other category -> false.

**The selected pin's popup** (`0xb1dc60`) pushes these Flash variables into the map movie for the highlighted spot, in this order: `/:reward_type`, `/:IsSpotLocked`, `/:NetworkErrorLock` (`ERROR`), `/:vehicle_restriction`
and `/:GroupRestriction01/02` (`empty` when none), `/:IsCarOK`, `/:IsRoadLocked`, `/:IsChaLocked`, `/:IsLevelOK`, `/:IsTeleportOK`, `/:IsTargetOK`, `/:IsNewSpot` (= `isNew` above), `/:CanAddMySpot`, `/:IsSpotFavorite`;
then it plays `spot/newspot/anim/bulle` (the "new" bubble) and sets `lightIcon`, `/:SpotName`, `/:ChallengeName`, `/:WhatSpot` (the kind string of section 5) and `/:WhatSpotType`.
- `CanAddMySpot` = the player's favourite count `< 0x20` (**32** "my spots") and the spot's type id is not 3.
- `IsSpotFavorite` = a lookup of the spot's u64 id (`[spot+0x10]`) in a player-data set.
- `0xab2d34..0xab3264` fills `/:SpotLockedText` and the lock conditions per kind: `/:IsCarOK`, `/:IsChaLocked`, `/:IsWeatherOK`, `/:IsDayperiodOK`, `/:isPhotoConditions` (the photo studio only opens in set weather / daypart), `/:m_SurgeryLocked2_`.
- A separate function (`0xb18a30`) feeds the vehicle strip (`/:SpotName`, `/:m_MapPlayerVehicle`, `/:MapPlayerVehicleGroup`, `/:MapPlayerVehicleUpg`, `/:IsRoadLocked`).

**Port today**: `map_screen.cpp` already sets `IsSpotLocked`, `IsTeleportOK`, `IsTargetOK`, `CanAddMySpot`, `IsSpotFavorite`, `WhatSpotType`, `IsLevelOK`, `IsCarOK`; it does **not** set `IsNewSpot`, `IsChaLocked`, `SpotLockedText`, `IsWeatherOK`, `IsDayperiodOK`, `isPhotoConditions`.
**Applied 2026-11-15:** the bookmark cap is now 32 (`kMaxBookmarks` in `map_screen.cpp`, was 20 `[INF]`) and `/:CanAddMySpot` is false once the island's list is full. Not applied: `IsNewSpot` (needs race-school progress, which the port does not keep), the type-3 exclusion (what type 3 is is not decoded), and the lock texts.

**Global flag `[0x1b1d351]`** (the 51 / 122 pin-pool switch): read from about 60 functions across the exe (input, camera, HUD, map), so it is a game-wide mode bool, not a map option. Not decoded.
**Still not found:** the map's item-list builder (the code that copies discovered spots into the 0x80-byte items), and the discovery distance test.
