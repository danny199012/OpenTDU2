# The gas station and fuel — a feature the shipped game left disabled, still in the data

Tags: `[DATA]` read from the game files, `[CODE]` from the readable exe, `[INF]`
chosen / inferred, `[ENC]` in the SecuROM band.

Where this came from: the TDU2 modding community (`Xarlith` / `Beeefy` / `Team`,
2026-10-30) — *"The only related thing that would require patching the exe for is
the Gas Station reimplementation, parts of it exist but are disabled."* They are
right about the parts, and they are wrong about the exe: everything below is in
the **data**, and the code that drives it is readable in the PC exe and in the
clear in the PS3 build. Nothing has to be patched.

## 1. The whole UI text is in the retail localization

`Euro/Bnk/Database/db_us.cpr` (decrypted with `otdu2_bnk_tool.exe decrypt`),
offset `0x17ae6` — one contiguous NUL-separated block, i.e. one screen authored in
one go. The prototype's `loc_us.json` gives the ids:

| id | text |
|---|---|
| 1635 | Gas station |
| 1636 | Shell basic |
| 1637 | Shell v-power |
| 1638 | Select basic gasoline |
| 1639 | Select special V-POWER gasoline for better performances! |
| 1640 | Currently, there is another gas type in this tank. |
| 1641 | Replace with this gas type ? |
| 1642 | Fill |
| 1643 | Bill |
| 1644 | Breakdown |
| 1645 | As you don't have enough money, SHELL is glad to pay the bill... |

So: **two grades** (Shell basic / v-power), a tank that holds **one** grade at a
time (with the "replace?" confirmation), a **Fill** action that produces a
**Bill**, and a **Breakdown** state when the tank is empty and the player cannot
pay. This is a complete, designed feature — not a stub.

## 2. Every car carries its tank and its consumption [DATA]

`Euro/Bnk/Database/db_data.cpr`, table `DBCarPhysicsData` (dump the shipped file
with `build\bin\otdu2_bnk_tool.exe xmb <db_data.cpr>`), row offsets:

| offset | field |
|---|---|
| +252 | `Single _gas_cons_avg` |
| +256 | `Single _gas_cons_max` |
| +260 | `Single _gas_cons_min` |
| +264 | `Single _gas_tank_size` |

They sit between `_final_drive_ratio` (+235) and `_gear_ratio1` (+268). The same
four names, plus the capitalised `Gas_Cons_Avg` / `Gas_Cons_Max` / `Gas_Cons_Min`
/ `Gas_Tank_Size`, are in the PS3 name pool (`docs/re/ps3/pc_ps3_anchors.json`,
`pc_addr` 15603752..15603800 and 15608080..15608128), read by the DB decoder
(`ps3_func` 1648668) — i.e. the decoder is where the runtime fields are filled.

In the decoded in-memory record of `vehicle_physics_spec.md` §2.1 they therefore
fall in the gap between `_final_drive_ratio` +0x17c and `_gear_ratio1` +0x1a0
(the four headlight-aperture fields take +0x180..+0x18c, so the tank block is
about +0x190..+0x19c) `[INF: the offset list in §2.1 was never extended for them]`.

**Values (measured, all 279 cars, `-dumpcars`):** tank **12.4 .. 133 L**, and the
California's is **78.0 L** — the real Ferrari California's tank, so the unit is
litres and the data is authored with real numbers. Consumption reads like l/100 km
(California `0.1 / 13.1 / 0.0`, Veyron `9.0 / 30.0 / 90.0`) — note `max` is **0**
on a good number of rows: the field is unset there, so a port must fall back to
`avg` `[INF]`.

The port reads them now: `CarPhysicsData::GasTankSize / GasConsMin / GasConsAvg /
GasConsMax` (`source/database/gs_database.{h,cpp}`, printed by `-dumpcars`).

## 3. The station itself: `gasStation` and its native handler

- The string `gasStation` is in the readable PC exe: `pc_addr` 15800672
  (`0xF11CE0`), used at **`pc_site` 11713384 (`0xB2B468`) — inside the readable
  band** (`0x6D1000`..`0xE407AB`), so the PC code that handles a gas station can be
  read without touching the encrypted part. On PS3 the same string is referenced
  by `func_00633CD0` (`ps3_func` 6505244 → site `0x633D5C`).
- What the PS3 handler does (lifted with `python tools/get_func.py 0x633CD0`,
  work file `_g_gasstation.cpp` in `C:\TDU2-PS3-Recomp`): it takes an object
  (`this+0x54`), reads a byte at `+0x170` and an int at `+0x190`, compares that int
  with another object's `+0x404`, and produces/returns a **gas type 0 / 1 / 2**
  (written to a caller-provided byte; 1 = basic, 2 = v-power in the two branches),
  calling `0x6C6208` / `0x6C7B78` (UI helpers) with the string at `0x1224FD8`.
  That is the "which grade is in the tank, do we have to ask to replace it" test of
  the strings above.
- There is **no `/:` frontend global** for fuel (`docs/re/ps3/flash_globals.txt`
  has none — compare `/:DrivingAssistance`, `/:vibration`, ...), so the station
  screen is **native**, not a movie `[INF]`.

## 4. What is *not* in the shipped world

- **No station was ever placed.** `-logspots` census of the two islands:
  Ibiza **79 spots** (estates 21, cardealer 20, club 7, tuning 5, clothes 4,
  carwash 3, rschool 3, hair 2, gamblinghall 2, carpaint 2, airport / photograph /
  stolencar / surgery / home 1 each) and Hawai **144 spots** (estates 46,
  cardealer 20, club 10, ...) — **no gas station spot on either island**.
- **No building bank**: no `*gas*`, `*fuel*`, `*petrol*`, `*station*`, `*pump*`
  or `*shell*` file under `Euro/Bnk` (the only `shell` hits are `sleeveless`
  clothes).

### 4.1 The pumps that *are* in the data (found with the port's own asset index)

File names are not the whole story: the port's asset index
(`-gm ASSETVIEWER -buildindex`, 9,452 banks / 153,690 assets) searches **inside**
the banks, and Eden is French — `pompe` = pump, `gaine`/`tuyau` = hose:

| asset | bank | authoring path |
|---|---|---|
| **`gaspump_d.2db`** | `euro/bnk/interior/gph_l2d.bnk` — **the only bank in the game with it** | `Euro/spots/playhous/ph_l2d/gph_l2d/maps/gaspump_d.2db` |
| `d_pompe_gas.2db`, `g_pompe_gas.2db`, `d_old_gas.2db`, `g_old_gas.2db` | `islands/ibiza/exterior/ers_b.bnk`, `islands/hawai/exterior/ers_b.bnk` (racing school B) and `scenarics/cinematics/offroad/stb_b4.bnk` | `Euro/spots/racschoo/rs_b/ers_b/maps/…` |

- `gph_l2d` is a **player-house garage interior** (`Euro/spots/playhous/…`, the
  port's own `gph_*` walkable garages). So the *functional* gas pump of retail is
  **in your house garage**, which is exactly where the `Gas station` / `Shell basic`
  / `Shell v-power` / `Fill` / `Bill` / `Breakdown` screen (§1) and the
  `gasStation` handler (§3) belong: you refuel at home, not on the road.
- The `pompe_gas` / `old_gas` pumps at the **racing schools** are decoration (the
  bank also carries the school building; nothing in the readable code drives them).
- 21 `gph_*` house garages exist (`l0a` … `l5y`) and **only `gph_l2d` has a gas
  pump** — the pump was authored for one house and the feature was cut before the
  others got one `[INF]`.
- Verified in the port: `-gm ASSET_VIEWER -bank euro/bnk/interior/gph_l2d.bnk`
  previews the garage (3 models, 45 meshes, 32,319 vertices, 27,532 triangles);
  `-gm WORLD_VIEWER -island ibiza -spot ERS_B` streams the racing school with its
  pumps (2 models, bounds (-24, -0.6, -27.3)-(16.3, 10, 23.4)).


So the shipped game has the strings, the per-car numbers and the handler, but no
station on the road: the feature was cut at the world/data stage, which is exactly
why the community concludes it needs an exe patch. It does not — a reimplementation
just has to place the stations itself (or wire the pump that already exists in the
`gph_l2d` garage, §4.1).

## 5. What the port would need

1. **Fuel state** — litres + grade (0 basic / 1 v-power) on the player car; burn =
   `_gas_cons_*` (l/100 km) x distance; empty = the `Breakdown` string, and with no
   money the `1645` line.
2. **A place to refuel** — retail placed none, so this is a design choice: the
   `gasStation` map icon (the movie's own icon name — check its clip list with
   `-dumpclips 1 -dumppath /map`) plus hand-placed coordinates, or a cheat/panel
   row. The interaction is the string sequence of §1: pick a grade → `Fill` →
   `Bill`.
3. **The HUD** — unknown: no fuel `/:` global exists, and the HUD movie's own
   elements were never surveyed for a fuel gauge; check that first before promising
   a gauge.
4. **Save** — the profile schema (`docs/re/savegame_schema_360.txt`) has **no fuel
   field**, so retail never saved the tank either; the port must decide (its own
   profile data, or a full tank at load).

## 6. What the port does now (implemented, session 16)

- **`source/game/freeride/fuel.{h,cpp}`** - the tank (`Litres`, `Capacity` from
  `_gas_tank_size`, `Grade`, `Consumption`, `AverageConsumption`, `RangeKm`),
  `BurnFuel` on the odometer delta, `FillTank`, and the prices (basic $1.49 /
  v-power $1.79 per litre `[INF: not in the data]`).
- **The consumption law** (session 75, `[INF: the retail law is not decoded]`).
  The DB gives three numbers per car and the curve is
  `avg x 1.2` at 0 km/h (stop-go), `avg` at 60, `min` at 90 (the best case),
  `max` at 200 (top speed). Two of the three fields are unreliable and are
  repaired from `avg`:
  - `_gas_cons_min` is **0.1 on 266 of the 278 rows** (`-dumpcars`) - a
    placeholder, not a figure any car reaches. A min below a quarter of `avg` is
    treated as unset and replaced by `avg x 0.7`. The old law used the raw min at
    0 km/h, which is how a full 78 L tank came to read a **71,435 km** range.
  - `_gas_cons_max` is 0 on one row; it then falls back to `avg x 2.5`.
  The California now reads `city 15.7, 60 13.1, 90 9.2, 200 32.8 l/100 km`
  (logged at load).
- **The range is smoothed**: `RangeKm` is read off a distance-weighted average of
  the instantaneous consumption (a 1.5 km time constant), not off the frame's
  value, so it does not swing by hundreds of km between frames; it is capped at
  5000 km against a data accident.
- **The grade burns differently**: v-power is 3 % leaner than basic
  (`FuelGradeConsumptionFactor`) `[INF: the retail grades have no decoded effect;
  the pump screen sells v-power "for better performances", so the port gives it
  the one number it can honour without touching the engine model]`.
- **`source/game/freeride/fuel_menu.{h,cpp}`** - the pump screen, all of its text
  read from the **game's own localization** with `LocStr` (1635 title, 1636/1637 the
  grades, 1640/1641 the "another grade in this tank / replace?" confirm, 1642
  `Fill`, 1643 `Bill`, 1644 `Breakdown`, 1645 the SHELL-pays line): the two grade
  rows with their bill, the tank state, the money, the key hints. A grade change in
  a tank that holds another grade asks first; a bill the player cannot pay is filled
  anyway with the retail 1645 line (no debt system `[INF]`).
- **Two refuel points**, both opening the same screen: **your house garage**
  (`interior.isHouse() && interior.isInGarage()` - where retail's only pump model
  is, 4.1) and **any station placed from the mods overlay** (a `fuel` prop, the
  `props.txt` column of `docs/re/modding.md` 3; within 35 m the HUD prompt offers
  `Fill`, and validate opens the screen **once the car is under 20 km/h** - the
  pump is a stop, like the shops, so rolling past at speed does not open it).
- **Empty**: the throttle is cut and the `Breakdown` line shows until the player
  fills up. The line is set **after** `tickWorldSpots` (that layer owns the prompt
  and clears it every frame, so the old call never reached the screen) and only
  when no spot prompt is up - a station prompt wins, because that is the way out
  of the breakdown. `-fuel <litres>` forces a level for a test, `-logfuel` prints
  the tank once a second, `-nofuel` turns it off.
- **The HUD fuel bar** (`FreeRideHud::drawFuel`) sits **under the odometer box of
  the gauge** (canvas 1030..1175 x 686..712, centred on the odometer's 1040..1165
  x 662..684), so the tank reads as part of the instrument cluster instead of
  floating beside the zone badge at the top right where it used to be. It shows
  the fill bar, the litres and the km left, green / amber under a quarter / red
  under a tenth. The car itself has **no fuel needle** - its dashboard carries
  `ND_SPD` and `ND_RPM` only (`-dumpcar`) - and the HUD movie has no fuel element
  (no `/:` global), so the port draws it; `-nofuelhud` hides it. Verified by pixel:
  `-fuel 20` (26 %) paints `(90, 200, 90)` at the bar's fill, `-fuel 2` paints
  `(235, 110, 30)`.
- **Save**: `profile.sav` `fuel_litres` / `fuel_grade` (the retail save has no such
  field - the port's own `[INF]`).
- Verified headless (session 75): the California's tank (78 L, row 0.1/13.1/0.0 ->
  `city 15.7, 60 13.1, 90 9.2, 200 32.8 l/100 km`); a full tank reads **497 km**
  (was 71,435); `-fuel 0.02` driving - the consumption follows the curve
  (15.7 -> 10.0 -> 16.4 l/100 km) and `Fuel: tank empty after 149 m - Breakdown`,
  with the `Breakdown` line on screen; at the FUEL1 station prop `A` twice -
  `Fuel: pump screen at FUEL1 (tank 20.0 / 78.0 L, grade 0)` (the screen shows
  `20.0 / 78.0 L  127 km`, `Fill: 58.0 L`, `$86`) and `Fuel: filled 58.0 L Shell
  basic for $86.42 (money 4996305)`.

Not done: no HUD gauge (no `/:` global exists for one), the grades have no
performance effect yet (v-power only burns 3 % leaner), and the prices are the
port's own.

