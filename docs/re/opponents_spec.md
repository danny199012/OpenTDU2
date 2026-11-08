# AI drivers: race opponents and the free ride rivals

Status (2026-11-04, `win`): **implemented**. The championship races (`R` files) run with the
championship's own 7 opponents in their own cars, a live race position, the movie's position HUD, and the
result decided by the finishing order. The story drivers also cruise the islands in their own cars.
Tags: `[DATA]` game files, `[CODE]` readable exe, `[INF]` chosen (the reason is given), `[ENC]` SecuROM band.

Code: `source/game/freeride/ai_drivers.{h,cpp}` (the tables, `AiCar`), `race_opponents.{h,cpp}` (the race
field), `traffic.{h,cpp}` (the rivals), `gm_freeride.cpp` (`startRaceOpponents` / `tickRaceOpponents`),
`freeride_hud.cpp` (`setRacePosition`). Research hook: `-dumpdbtables`, `-dumpdbtable <T> [-dumpdbcols a,b]`
(`gs_database.cpp`). It prints any `db_data.cpr` table; a wide row needs `-dumpdbcols`, because the log cuts long lines.

## 1. The tables [DATA]

| table | rows | what it holds |
|---|---|---|
| `DBPNJ` | 240 | every character: `_hashcode`, `_pnj_name`, `_configid` (`STO_Tess_Wintory`), clothes |
| `DBPNJCar` | 77 | `_pnj_id` -> `_car_id` + `_color_id`: the garages of the **7 story drivers** |
| `DBBot` | 81 | named bots: `_botname`, `_botcar`, `_color_id`, `_difficulty`, `_aggressiveness`, `_pnj` (the body), `_zoneindex` |
| `DBLicense` | 33 | per championship `_bot_1..7` (DBPNJ hashcodes), `_difficulty_bot_n`, `_aggressiveness_bot_n`, `_rival`, `_retriesbeforeeasier` |
| `DBEvent` | 106 | `_pnj1..3`, `_duel_low`: the duels / events with characters (not run yet) |
| `DBRewardCar` | 18 | duel prizes, e.g. row 6 `DUEL_WIN_C4` -> car 601 (Miami's Mustang), tuning 3/3/3, `_color 0` |

**The 7 story drivers** (`-logaidrivers`): Adam `0x18a`, Andrew Sullivan `0x18b`, Ryan Wilder `0x18c`,
Chris Wilder `0x18d`, Miami Harris `0x18f`, Stuart Wintory `0x190`, Tess Wintory `0x194`. Each has 11
`DBPNJCar` rows, **one car per vehicle class** (A1-A7, B3, B4, C3, C4: the classes the championships use).
Tess: Golf GTI (A7), TT S (A6), SLK 55 "Tess" (A5), DB9 (A4), 612 (A3), F430 Scuderia (A2), CCXR (A1),
Q7 (B3), H3 (B4), 308 GTS (C3), Esprit S3 (C4).

**Every championship's `_bot_1..7` is these 7 drivers** (all 27 rows). Only the difficulties (0.48 .. 0.70)
and the `_rival` change (Tess, Ryan, Miami, Chris or Stuart; 0 on the cups). `_aggressiveness_bot_n` is
0.5, with 0.6 on a few rows.

**Signature cars**: `DBCarPhysicsData` rows that duplicate a base car (`_duplicate`), with the driver in the
version name. They share the base car's bank:

| id | car | `_version_name` | base |
|---|---|---|---|
| 601 | Mustang_Fastback_68 | `Fastback "Miami"` | 371 |
| 602 | Touareg_V10_TDI | `V10 TDI "Ryan"` | 323 |
| 603 | SLK_55_amg | `55 AMG "Tess"` | 204 |
| 604 | Q7_V12 | `V12 "Chris"` | 348 |
| 605 | SLR_McLaren_722s_Roadster | `McLaren Roadster 722s "Stuart"` | 415 |
| 606 / 607-609 | CLK 63 "Outlaw" / Evora, 300 SL, CCXR "Solar Crown" | | |

Car 601 has **no `DBCarsAndColors` row**, so it draws in the model's own `.2dm` paint, a dark green
(`docs/images/freeride_rival_miami_mustang.webp`). The owner remembers "the pink mustang". No pink paint is
reachable from these tables, so none is invented. If retail shows pink, the source is elsewhere (a
livery, or an encrypted runtime pick `[ENC]`).

**`DBBot._zoneindex`** (0..30) is not decoded. It does not index `DBCruisingZones` (6 rows) or the aiconfig
zones (6). The bots are therefore not placed by zone yet.

**Todd** (Todd Bishop, `SPT_Todd_Bishop`) is a spot character in `DBPNJ`. He has no `DBPNJCar` row and no
`DBBot` row, so the data gives him no car to drive.

## 2. The race field (`RaceOpponents`)

- **Who** `[DATA]`: the race's championship (`findRaceChampionship`) gives `_bot_n`, `_difficulty_bot_n`,
  `_aggressiveness_bot_n` and `_rival`. The field size is the file's `NbIA` (7 on every championship race,
  3 on the rest). A race outside a championship takes the 7 drivers at difficulty 0.5 `[INF]`.
- **The car** `[INF]`: the driver's `DBPNJCar` car of the championship's `_vehiclecategory_1/2`. There is
  always one, which is the strongest hint that this is the rule; the retail pick is `[ENC]`. Otherwise the
  nearest class of the same family. Its `_color_id` is applied (`CinematicCar::applyColor`).
- **The course** `[INF]`: the start line, then every gate in passing order with the laps unrolled
  (`LicenseTest::getSequenceGate`). Each leg is routed over the island road graph: the shortest path
  between the nearest link points, joined straight if a gate is over 35 m from a road or the detour exceeds
  3x the gap + 300 m. The result is resampled every 3 m and lightly smoothed, with 70 m of grid before the
  start line and 250 m of run-out after the finish. The `.ig2` road-gate records carry link words
  (`0, 236, 2, 1, 0` + a float) that are **not** an index into the port's road graph `[DATA: checked
  against CHA5A04R]`, so they are not used.
- **The grid** `[INF]`: the rival on the front row beside the player (the duel), then two by two, 8 m per
  row, weakest first.
- **The driver law** `[INF]`; the retail AI is `[ENC]` (the `.ig2` `IASA..IASJ` keys are its settings, not
  decoded):
  - the curve speed is `sqrt(g (0.75 + 0.4 d) / curvature)`, then a backward braking pass at
    `7.5 + 3 d` m/s²;
  - the top speed is `_max_theorical_speed x (0.82 + 0.25 d)`;
  - the acceleration is `(27.8 / _acceleration_0_100_kmh) x (0.85 + 0.4 d) x 1.3`, tapering to 0 at the
    top speed;
  - here `d` is `_difficulty_bot_n`.
- **Racecraft** `[INF]`: each car keeps its own line across the road (±1.6 m). A slower car within
  12 m + 0.6 s ahead is passed on a free side (±2.6 m from it, at most 2.8 m off the line) when it is slower
  than the margin `3 - 3 x _aggressiveness` m/s; otherwise the car follows it. Car-to-car overlap moves
  the two apart, and the one behind takes the front car's speed. A finished car pulls to the roadside and
  blocks nobody.
- **Catch-up** `[INF]`: a car 80-380 m ahead of the player lifts by up to 12 %; one as far behind pushes by
  up to 8 %. The retail rubber band is not readable.
- **Contact with the player** `[INF]`: Traffic's circles and impulse. The opponent loses the impulse
  along its heading and is nudged off its line; above 2000 N·s it loses drive for 0.8 s. It is not a
  traffic violation.
- **Ranking**: the player's progress is the projection onto the course (250 m ahead, then 1.5 km). The
  order is: finished racers by time, then the rest by distance. **The race is won in 1st**; finishing
  behind is `Beaten` with the place ("Beaten  7th of 8") `[INF: the retail rule is ENC]`. If the whole
  field is home and the player is still out 60 s later, the result is `Beaten` in last place.
- **The clock** `[INF]`: with opponents, `CtDw` is **not** applied. CHA5A04R's `CtDw` is 240 s for 3 laps
  of a 3.6 km loop (10.8 km gate to gate), i.e. 160 km/h average; the AI needs 321-327 s. So `CtDw` cannot
  be this race's limit.
- **Lifetime**: loaded at the race start, kept 8 s after the result, then removed. The traffic brakes for
  them (`setExtraVehicles`).
- **Level of detail** (`AiCar`, 2026-11-04 second pass): the opponents and the rivals draw
  `vehicules/med/<car>_med.bnk` (`CinematicCar::Desc::Lod`). It holds the same body with half-resolution
  maps (`tts_med.3dg` 1.02 MB against `tts.3dg` 1.05 MB; `med_tts_*.2db`; the same `tts_color.2dm`)
  `[DATA]`. **`<car>_low.bnk` is not a model**: it is the car's low-quality engine sound bank
  (`TCT_*.wav`, `Horn_class_1.wav`, `CarVSTConfig.xmb`) `[DATA]`. So `_med` is the only model LOD shipped.
  The cockpit already follows the retail LOD table (`0x00f7e788`: the interior only within 5 m). The real
  cost was the shadow casters: every car is drawn into the 3 PSSM slices. Only the **3 nearest AI cars
  within 60 m cast** `[INF]`, and none draw in the reflection pass `[INF]`. Flags: `-ailod high`,
  `-aishadows <n>`, `-aishadow <m>`, `-aireflect`.

## 3. The HUD [DATA: HUD.vmf]

- `/HUD/position` (char 330): `open` f10; `cart/ic` mode icon `race` f8 (also `eliminator`, `speedtrap`,
  `hotspots`, `copilot_c`, `relay_c`); `/:Pos` the rank, `/:Exp` the suffix (`st` / `nd` / `rd` / `th`),
  `/:NbPlayers` the field (`/8`). `player_before` / `player` / `player_after` (labels `on` f2 / `off`) are
  not driven yet. They are probably the name rows of the cars around the player.
- The position takes the timer's slot (top right). `/HUD/time` has `up` f10 and `down` f40, and `down`
  puts it under the position `[DATA: the labels; INF: the pairing]`
  (`docs/images/race_position_hud.webp`).
- GENERAL.vmf's goal popup (top left, 30 s) closes on GO when there are opponents `[INF]`.

## 4. Verified

- `-island hawai -licensetest CHA5A04R`: the 7 drivers on the grid, each in a class A5 car (Tess's SLK 55
  "Tess" beside the player), all of them away at GO (`docs/images/race_opponents_field.webp`).
- `-licensewarp`: "Passed 1st of 8", the race prize paid, the field removed after the hold.
- `-licensewarp -licensewarpdelay 13 -storystep 8`: all 7 finish in 321-327 s; the player is "Beaten 7th
  of 8", and "Beaten 8th of 8" with the 60 s rule.
- CHA7A02R (A7: Golfs, Mitos, Breras), CHB4A01R (B4: Touareg "Ryan", H3s, Range Rovers).
- `-vk_enable_validation_layer` through start, finish, hold and removal: 0 VUIDs.
- Frame cost, `-opponentfollow 2`, two runs x 8 `[perf]` samples each: high bank with every shadow
  22.6 ms; `_med` + the 3-car shadow budget (the default) 21.5 ms; `_med` with no AI shadow 21.0 ms;
  `-noopponentdraw` 19.0 ms. The field went from 3.6 to 2.5 ms. `_med` and high look the same at 8 m
  (`docs/images/race_opponents_med_lod.webp`, top high / bottom med). 0 VUIDs through start -> removal with
  the rivals on.

## 5. The free ride rivals (`Traffic`)

`[INF]`: the retail spawn rule is not readable. At the traffic load, 3 of the 7 story drivers are picked
(`-rivals <n>`, `-rival <name>` first, `-norivals`), each in their signature car when they have one, else
a random garage car. One at a time (`-rivalsmax`) appears on the traffic's spawn ring 25-60 s in, then
every 1-2.5 min. It drives like the traffic at 58-86 km/h on its own car (wheels roll and steer, brake
and headlamps), and is removed by the traffic's own despawn rule. Within 30 m the HUD hint line shows
"Tess Wintory - SLK 55 AMG" once per meeting. The near-car spec card works on it as on any traffic car.
`-rivalsoon` (first after 2 s), `-rivalpark` (with `-trafficpark`: the rival parked in front).

## 6. Not done / next

- **The rest of the draw cost** (2.5 ms for 7 cars): their main and depth-prepass draws. Next lever: the
  number of items per car (rims and tires are separate models, 8 draws per car per pass).
- `player_before` / `player_after` name rows, the opponents on the minimap, the end-of-race standings
  screen (`/helpmore` `quit_rankings`, `array_sort`).
- The duels (`DBEvent _pnj1`, `_duel_low`, `DBRewardCar DUEL_WIN_*`) and the instant challenges (RaceEvents)
  with opponents; the speedtrap (`A`) races.
- `DBBot` placement (`_zoneindex`), `_retriesbeforeeasier`, the `.ig2` `IASA..IASJ` AI keys.
