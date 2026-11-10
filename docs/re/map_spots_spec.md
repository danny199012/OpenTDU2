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
