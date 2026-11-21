# Racing schools and licences

Status (2026-10-29, session 45 `win`): **implemented** — the three schools of each island, their licences,
every licence test driven from the game's own challenge files. Tags: `[DATA]` game files, `[CODE]` readable
exe, `[INF]` chosen, `[ENC]` SecuROM band.

## 1. The licences: `DBLicense` (34 rows) [DATA]

| column | meaning |
|---|---|
| `_challengefile_1..8` | the tests / races, `.ig2` names (`RSA7T01` ...); `##` = unused slot |
| `_isachampionship` | 0: a licence (7 rows), 1: a championship / cup (27 rows) |
| `_island` | 0 Ibiza, 1 Hawai |
| `_order` | order within the island (1, 2, 3) |
| `_vehiclecategory_1/2` | the classes taught (A7 = 7 ... A1 = 1; B and C classes 11/12, 16/17) |
| `_championshipname_hc` / `_desc_hc` | championship names ("Championship: Ibiza city"); licences have none |
| `_racemoneyreward` / `_champmoneyreward` | championships only ($2,500 / race, $75,000 ...) |
| `_bot_n`, `_difficulty_bot_n`, `_aggressiveness_bot_n`, `_rival` | the championship opponents |

The licences (`-loglicenses`):

| school | Ibiza (island 0) | Hawai (island 1) |
|---|---|---|
| A (`ERS_A`, tests `RSA…`) | A7 (7 tests) | A5 / A4 (7), A3 / A2 (8), A1 (8) |
| B (`ERS_B`, `RSB…`) | B4 (6) | B3 (8) |
| C (`ERS_C`, `RSC…`) | C4 (6) | C3 (8) |

**Vehicle classes** (`_vehiclecategory`, the cars' `DBCarPhysicsData._vehiclecategory` too) — a community list
(the owner's Discord), checked against the data: every licence's classes match its tests' (A7/A6 RSA7, A5/A4,
B4 12, B3 11, C4 17, C3 16), and the cars per class (retail, 201 rows) are A1 43, A2 36, A3 18, A4 18, A5 14,
A6 7, A7 8, **B1 2 (the cut Quattro S1 / Race Touareg, invalid)**, B2 1, B3 4, B4 8, C1 7, C2 7, C3 10, C4 8,
Ma1 2, Ma2 8 (the bikes, all invalid but `Default_Bike`); A8, B5, Mb1/Mb2, MC and R1-R6 have no car.
`VehicleClassName()` in `licenses.h`.

| id | class | id | class | id | class |
|---|---|---|---|---|---|
| 1-7 | A1-A7 | 13 | B5 (unused) | 20, 21 | Mb1, Mb2 (unused; dirt bikes?) |
| 8 | A8 (unused) | 14-17 | C1-C4 | 22 | MC (unused) |
| 9 | B1 (cut cars only) | 18, 19 | Ma1, Ma2 (bikes) | 23-28 | R1-R6 (race, unused) |
| 10-12 | B2-B4 | | | | |

**School ↔ licence** [DATA]: every school spot stands beside its own tests (Ibiza `ERS_A` at
(18174, 27306), the A7 tests start at (18321, 27287); `ERS_B` (17164, 18079) / `RSB4T01` (16971, 18048);
`ERS_C` (11660, 15891) / `RSC4T01` (11902, 16032)), so the spot's letter is the tests' `RS<letter>`.

## 2. The challenge files: `Euro/Bnk/ig2/<name>.ig2` — readable [DATA]

538 files, **not encrypted** (the race events spec assumed the event files were): `RSA/RSB/RSC` licence
tests (58), `CHA/CHB/CHC/CI/CH1/CH2` championship races (169), `EVI`/`EVH` events (119), `RMI/RMZ/RMH`,
`CMF/CMK` missions, `DUE`, `CAS` .... Each has a replay `ig2/replays/<name>.rly` (not read).

Big endian:

    "IGE2"  u32 0x1c (header size)  u32 0x306 ...
    "MDSC"  u32 size, u32 count, u32 0, u32 4, u32 0x306, then keys
            { char fourcc[4]; u8 3; u8 type; u16 size; value } (a few unnamed u32 words in between)
    "ENTI"  u32 size, u32 count, then entities { char tag[4]; u32 size; body }

Keys read so far (meaning [INF] from the values unless stated):

| key | RSA7T01 | meaning |
|---|---|---|
| `timG` | 26000 | target time, ms (the pass time) |
| `CtDw` | 26000 / 240000 | the time limit, ms (= timG for the manoeuvre tests, 4 min for the road tests) |
| `VMod` | 635 | the test's car (`_id_car` space) |
| `dbna` / `dbd1` | 3815 / 3816 | **loc ids of the test's name / description** [DATA: "Braking and control", "Slaloms", "Urban test", "90° bends", "Hairpin turns", "How to overtake", "Mixed challenge"] |
| `isla` | 0 | island |
| `NbCp`, `nblp` | 3, 2 | not used yet |
| `fee1`, `rewm` | 50, 20000 | an entry fee and a reward? not paid [INF] |

`ENTI` entities:

- **`CHK2`, 0x68 bytes** (manoeuvre tests): `u32 1; float m[12]` (rows `(r0, tx), (r1, ty), (r2, tz)`; the
  third column is the gate's forward / the start heading); `vec3; f scale; i32 kind; i32 side; f width;
  16 x 0xff`. Kind 0 start / wide gate (20-22 m), 1 slalom gate (7-8 m, side 1/2 alternating), 2 gate (10 m),
  3 finish.
- **`CHK2`, 0x7c bytes** (road tests `RSA7T04`, `T07`, `RSC4T06`): `u32 0; u32 road link; u32 x3; f along
  the link;` then the same matrix and tail; every gate 7 m, in route order 150-600 m apart.
- **`DEC2`** (0x54 / 0x68): decor (cones, barriers) with a type id — not placed yet.

Gates are in file order: `gates[0]` is the start (the car's pose), the last the finish.

## 3. The port (`licenses.*`, `ig2_challenge.*`, `license_test.*`, `school_menu.*`, `GMFreeRide`)

- A at a racing school walks into its interior (`irs_a/b/c`); the instructor (`NPC_area`, FpSelectable
  Type 20 "tutor") opens the **school menu**: the school's licences on this island in `_order`, then a
  licence's tests (name, target, best). Tests open in order and licences open in order [INF].
- Taking a test leaves the school and puts the car on the start gate; the HUD's 3-2-1-GO, the timer with
  "gate n / N", markers on the next two gates. A gate counts when the car crosses its plane within half its
  width + 1 m; crossing it further out (within 25 m) fails "Missed a gate"; the limit `CtDw` fails "Out of
  time"; the finish under `timG` passes, over it "Too slow" [INF: the retail rules are ENC].
- A pass saves the best time (`profile.sav tests_passed = RSA7T01:23.4,...`); the last test of a licence
  shows "Licence A7 obtained!".
- The test is driven in the player's car: the test car `VMod` needs the runtime car change
  (`-switchcar`, car_delivery.md).
- Hooks: `-licensetest <name>` (drive one now), `-licensewarp` (through the gates in hops), `-schoolmenu
  <A|B|C>`, `-loglicenses`, `-logig2`.

## 4. Not done / next

- **Events from the same files**: the `EVI`/`EVH` events (`gmth` 7) and the Hawai missions (`CMF`/`CMK`,
  `gmth` 12 / 11) are this format; RaceEvents' synthesized routes could become the real ones.
- The test car (`VMod`), the replays (`.rly`), the school cinematics
  (`C_LICENSES_L`, `C_WIN_LICENSE`), rewards / fees (the result screen: section 9; the school desk: section 11).
- Licence requirements across islands (does Hawai's A5/A4 need Ibiza's A7?) and the class gating of
  events / cars by licence are not applied.

## 5. Championships (`championship_menu.*`, `GMFreeRide`)

### The data [DATA]

The 27 `DBLicense` rows with `_isachampionship`: 9 on Ibiza (A7, A7 high, A6, A6 high, B4, B4 high, C4, C4 high,
Cup area 1), 18 on Hawai. Each lists 6-8 races (`_challengefile_n`, `.ig2`), `_racemoneyreward` ($2,000-10,000)
and `_champmoneyreward` ($50,000-900,000; cups $1-5 M).

The race modes — the name's last letter, the file's `gmth` and the race's own title agree in every file:

| letter | `gmth` | files | title prefix | in the port |
|---|---|---|---|---|
| T | 0 | 43 | "Time: Old town" | **target time `timG`** (138-286 s) to beat |
| R | 3 | 85 | "Race: Salt marsh challenge" | finish inside the limit `CtDw` [INF: no opponents yet; `NbIA` 7 = 7 opponents] |
| A | 1 | 18 | "Speedtrap: Seaport run" | finish inside the limit [INF: the trap score, `SP2R` 45,000-66,650, is not decoded] |
| S | 10 | 15 | "Speed: Ibiza speedway" | **not run** (one start gate and a points goal `SPPG` 1.1-1.7 M: scoring not decoded) |

`lcpi` 0 = a circuit of `nblp` laps (the course closes on its start: first-to-last gate 150-1,100 m against a
span of 450-2,600 m), 1 = point to point (first-to-last = span) [INF from the geometry; a few circuits end
far from their start (`CHA4B01R`, `CHA1A08R`)]. A circuit lap is every gate then the start line again.

### The port

- Each championship is entered at its first race's start gate (the HUD's own tile, `asphalt` / `dirt` /
  `classic` / `cup`, `_h` for the high one; a marker within 300 m) [INF: the retail map positions are not
  decoded]. A (slow) opens the race list.
- Unlocks [INF]: the licence of its class (a licence's `_vehiclecategory_1/2`), the high one after the first of
  its class, the cup after every other championship of the island. `-allchamps` opens all.
- Races run in order with the licence runner (countdown, gates, laps, HUD "gate n / N"); the first win pays
  `_racemoneyreward`, the last one `_champmoneyreward` too ("<name> complete!"). Best times are saved with the
  licence tests (`tests_passed`); a championship is complete when its runnable races are won.
- Driven in the player's car: the race car (`VMod`) and the class requirement are not applied.
- Hooks: `-championship <part of the name>` opens a race list, `-licensetest <race>` / `-licensewarp` drive one.

### Not done

- **Opponents** (`NbIA`, `DBLicense._bot_n` / `_difficulty_bot_n` / `_aggressiveness_bot_n`, `_rival`), so races
  have no placing; the speedtrap score; the speed mode; the start / podium cinematics
  (`scenarics/cinematics/asphalt/sta_firstintro`, `sta_winloosepodium` ...); the map icons of the championships.

## 8. The race gates and the race HUD (session 2026-11-04, `win`)

### 8.1 The checkpoint pack [DATA]
`Euro/Bnk/islands/<island>/scenarics/packs/pack_cp.bnk` (Ibiza and Hawai both ship one) holds the gate models.
Its `entities.xmb` (`ExportType_Entity`) has `ArrayOf_Entity__Actor__CheckPoint`, one record per kind of
gate, with `CheckPointType`, `OffRoadCheckpoint` (0 road, 1 off road, 2 event), `Actor1` / `Actor2`,
`ActivationZone`, `CheckPointPassedGood` / `Bad` (empty), `HangingPoints`:

| type | records | model |
|---|---|---|
| 1 | Start_line, Start_line_Off | the blue "START" hologram arch (dammier checker band, Line_Pulse_Blue .UVA) |
| 2 | Finish_line, Finish_line_Off, Finishline_Event | the red "FINISH" arch |
| 3 | Check_point, CP_OffRoad | the orange "CHECKPOINT" arch |
| 4 | Radar | the speedtrap radar |
| 5 | SF_Barel_D_act | one striped barrel |
| 6 | Gate_Main | no mesh: Actor1 / Actor2 = the barrels SF_Barel_D_act_left / _right |
| 7 | BrakingZone_01 | ActivationZone = the BrakingZone volume |

The arches are 24.5 m across and 6.5 m tall at scale 1 (bounds -12.27..12.24 x 0..6.48), the barrel
0.72 x 1.10 m. `ExportType_Volume` has SLPlan / FLPlan (start / finish planes), ZoneRadar, BrakingZone.

### 8.2 Which model where [INF: the race code that places them is ENC]
`RaceGates` (`game/freeride/race_gates.*`) loads the road variants (OffRoad 0). `GMFreeRide::updateRaceGates`
draws the gate to pass and the one after (the start line too during the countdown, 8 m ahead of the car's pose
so it reads in front of the chase camera):
- gate 0: Start_line; a circuit's last step (the lap closing on gate 0 for the last time): Finish_line;
- the last gate of a point to point, and a manoeuvre gate of kind 3: Finish_line;
- the manoeuvre gates (0x68 records) of kind 1 (slalom) and 2: the barrel pair at +-width/2;
- everything else (the road gates, 0x7c, and the wide kind 0 manoeuvre gates): Check_point.
Road arches draw at their authored 24.5 m (the files' road gates are 7 m); the manoeuvre gates scale the arch
across to their width. The pass zone follows what is drawn: `LicenseTest::getPassHalfWidth` = max(the gate's
half width + 1 m, the drawn arch's half width) [INF]. Every gate is snapped to the drivable surface (the files'
heights sit up to a metre under the road: a barrel is only 1.1 m tall). The instant challenges
(`RaceEvents`, road-graph routes) get the same arches, facing along the route.

### 8.3 The HUD readouts [DATA: HUD.vmf clips, labels and text variables; INF: the feed]
| clip | labels | variables | fed with |
|---|---|---|---|
| `/HUD/time` | open f6, up, down | `/:Min` `/:Sec` `/:Cent` | the race time |
| `/HUD/laps` | open f10; `cart` anim | `/:NbTurns` of `/:TotalTurns` | lap n / N (circuits) |
| `/HUD/checkpoints` | open f10, stop f35, chekpoint f41; `cart` anim | `/:NbCheckpoints` of `/:TotalCheckpoints` | checkpoint n / N of the lap (point to point: the lap counter takes that slot on a circuit) |
| `/HUD/checkpoints/cur_time` | gap; child red / green / empty | `/:CheckTime` | the time at each gate passed |
| `/HUD/checkpoints/chck_box` | red / green / empty | `/:Gap` | the split against the same gate of the previous lap (green ahead) |
| `/HUD/checkpoints/last_lap` | open f8, close f67 | `/:m_LastLap_` = loc 7429 "Last lap!" | the last lap starting |

The minimap `objectif_01..08` icons (hidden at rest; frames `checkpoint` f1, `finish` f10, `start` f17, `radar_on`
f24, `shop`, `challenge`, `cop`, `GPStarget`, `GPSObjective` [DATA]) show the gates the world shows (start green,
checkpoint orange, finish red), placed on the heading-up map with the satnav's own scale (3 m / px) and parked on the
map edge towards a gate beyond it [INF] (`FreeRideHud::setMinimapObjectives`;
`docs/images/race_minimap_gate_icons.webp`). Not driven yet: `/HUD/position` (`/:Pos` of `/:NbPlayers`, no opponents). The zone badge yields its corner
to `/HUD/time` while a race runs.

### 8.4 Hooks
`-noracegates`, `-logracegates` (the gates drawn, once a second, with the terrain under them),
`-licensepark [<step>|last]` (parks the car 25 m before that step's gate), `-racestart <n>` (instant race n
started at its marker). Note: `-licensetest` does not switch island - a Hawai course run on Ibiza has no
ground under it (`CHA5A04R` "Haleiwa loop" needs `-island hawai`).

## 9. The result screen: RESULTS.vmf, racing school frames (session 2026-11-05, `win`)

The owner: *"upon failing there should be a menu that pops up with retry and if you win there's a menu for
advancing to next task or returning to the school."*

The retail screen is `FrontEnd/HiRes/results.bnk` (RESULTS.vmf, 797 frames). Every script was read with
`tools/vmf_disasm.py` (an AVM1 disassembler for the FMV container; `python tools/vmf_disasm.py results.vmf out.txt`)
[DATA]:

- root labels for the school: `failed_test` (f119), `failed_re` (f129), `success_test` (f139), `success_re` (f150),
  `menu_rs` (f160), `replay_test` (f175), `back_r_test` (f190);
- `failed_test` / `success_test` send `FSCommand:resu`, set `_root.IsFailed` 1 / 0, `FSCommand:gaof`, register
  `_root.Clip = this` and call `openTitle()` (`_root.Title = _root.TestName`, `WhatPage = 'rschool'`);
  `failed_re` shows the thumbs-down cart (`msg_top/icon:loose`, `sndp:resuloos`), `success_re` the licence panel
  `results_success` (`/:LicenceName`, `/:nb_tests_done` / `/:nb_tests_total`, "Successful / Total") with
  `sndp:resuwin`; both stop on an A (OK) prompt with `_root.Input = true`;
- A (button 374, Enter) runs the frame's `A()`: `IsNext` 0, `IsRetry` 1, `gotoAndPlay('menu_rs')`;
  `SetRootMenuRS()` fills `menu_rs.setBt[n] = [label, id, '', lock]`:

  | state | rows |
  |---|---|
  | `IsFailed == 1` | Retry (`retrytest`), Next test (`nexttest`), Video Replay (`replaytest`, lock `LockReplay`), Quit to the racing school (`quittest`, lock `IsReward`) |
  | `IsLicensed == 0` | Next test, Retry, Replay, Quit |
  | `IsLicensed == 1` | Quit, Next test, Retry, Replay |
  | `IsLicensed == 2` | Quit, Replay |

- `ValidMenu()` answers on `_root.BtValid`: `retrytest` -> `FSCommand:rest`, `nexttest` -> `FSCommand:nets`,
  `quittest` -> `FSCommand:quit`, `replaytest` -> the movie's own `replay_test` (`FSCommand:rpin`, the replay
  player); B (button 388, Backspace) on the menu goes back to `failed_re` / `success_re`.

The port (`game/freeride/license_result_screen.*`, `GMFreeRide::openLicenseResult` / `tickLicenseResult`): a
licence test (an `RS*` file, not a championship race) that ends Passed / Too slow / Missed a gate / Out of time
opens the screen over the frozen world, HUD hidden. Variables set: `TestName`, `TestDesc`, `Message`,
`final_score` / `ref_score` ("03 : 22 : 12", the placeholders' format [DATA]), `LicenceName`, `nb_tests_done` /
`nb_tests_total`, `IsLicensed` (1 the licence was just obtained, 2 the last test, else 0 [INF: from the menu each
value builds]), `LockReplay` 1, `IsReward` 0. Keys go in as SWF key events (Enter 13, Backspace 8, Up 14, Down 15)
like the pause menu. Retry restarts the test; Next test starts the licence's next test (back to the school when
there is none or this one is not passed: tests open in order); Quit walks into the school (`ERS_<letter>`) with the
school menu on that licence's tests (`SchoolMenu::openAtTest`). Video Replay: no replay is recorded, so `rpin`
sends the movie back to its panel. The fail cart has no text: `openCarts()` itself sets `Message2 = 'empty'`
[DATA] (the retail fail cart may be icon-only; not checked against a retail capture).

Evidence: `docs/images/licence_result_failed.webp`, `licence_result_menu.png`, `licence_result_success.png`,
`licence_result_back_to_school.png`. Hook: `-licenseforce <pass|fail>` (`-licenseforceafter <s>`, default 2) ends
a running test so the screen can be checked without driving.

## 10. The course furniture: slalom poles, cone lines, racing line (session 2026-11-05, `win`)

The owner: *"the licenses slalom is meant to have cones you weave in and out of ... there's also meant to be a
racing line which tells you when to slow down."*

**Slalom poles** [DATA: the layout; INF: the side sign]. The `CHK2` kind 1 records of a test stand on **one
straight line** (RSA7T02: five ~30 m apart on the start heading, five more on the way back) with `side`
alternating 1 / 2 in the gate's own frame (a record whose forward is reversed keeps the alternation in the world).
So a kind 1 record is the editor's "Slalom Object": the pole is at the position and the opening (`width`, 7 m) is
on one side of it. The port had drawn a barrel pair centred on the pole, so the course was a straight line with no
weave. Now (`Ig2Gate::isSlalom` / `getPassCentre`, `LicenseTest::tick`): one pole, and the gate counts when the
car's centre crosses the plane between `kSlalomClear` (1 m) and `width + 1 m` out on the open side; anywhere else
within 25 m is "Missed a gate". Side 1 opens towards the frame's +x (`(fwd.z, -fwd.x)`), side 2 towards -x [INF:
the retail rule is ENC; the sign only decides which way the first weave goes]. Checked: `-licensewarp` through
the openings passes all 12 gates; holding the throttle straight down the pole line fails at gate 1 (3.6 m off).

**DEC2 decor** [DATA]: 0x54 bytes `{u32 1; 3x4 matrix; vec3; f scale; u32 type; u32 0}` (0x68 with the road-link
prefix). Types over the 58 tests: 1 / 2 (66 each), 4 / 5 (289 each), 0 (1). A type 1 / 4 record is always followed
by its 2 / 5, and the two are the ends of a straight 15-50 m line: the editor's "Cone line" (`cone_start` /
`cone_finish`, gmeditor.vmf). The 1 -> 2 lines flank the start and finish boxes, the 4 -> 5 lines wall the
manoeuvre areas (RSA7T02's turnaround box). Drawn as one marker every 3 m [INF].

**There is no traffic cone in the game data** [DATA, negative; follow-up session 2026-11-05]: no Ibiza bank names a
cone. `SF_PLOTS` in Hawai's `commonworld.bnk` looked like one ("plots") but is a *material*: the library objects
using it, `SF_PLOA` .. `SF_PLOQ` (17 nodes, names cut to 8 characters), are green street **bollards** and bollard
rows (1.06 m posts, 0.7 m posts, strips of posts 2-46 m long; `docs/images/licence_hawai_sf_plo_bollards.webp`,
SF_PLOC / SF_PLOD / SF_PLOG drawn as the RSA7T02 markers). The rest of the Hawai library (649 nodes, listed with
`-logcone` in that session) and the police pack (helicopter, EMP) have no cone either. The only course obstacle
shipped is the checkpoint pack's own racing-school barrel (`sf_barel_d`, materials `RS_Barel_Metal` /
`RS_Barel_Pulse`, 0.72 x 1.10 m), so the barrel is the most likely retail look of the slalom poles and cone lines
[INF], and the port draws it. The editor's "Cone" / "Cone line" are names of tools, not of models.

**The racing line** [DATA: the models; INF: the path]. `pack_cp.bnk` has a plain actor `Trajectory`
(`trajectory_BD736E66FF20BE19`, a 1 x 1 m flat quad, texture `trajectory.2db` = white chevrons, UVAs
`Arrow_Orange_3` / `Arrow_Red_2`) and the type 7 checkpoint `BrakingZone_01` (mesh `sf_target`, 16 x 16 x 6.9 m, a
"stop" hand target; `ActivationZone` = the `BrakingZone` volume). The editor lists "Driving Spline" and "Braking
Zone" tools, but **no .ig2 entity holds a path** (CHK2 / DEC2 / VEHI only), so the retail line's source is not
found. Lead: the per-test replays `ig2/replays/<test>.rly` (an XMB `ReplayBaseParams` header, then car / matrix /
input chunks; the matrix chunk starts with the full start position, then packed deltas; not decoded). The port
draws a Catmull-Rom curve from the start pose through every gate opening of one lap, cut into 2.5 m `Trajectory`
strips (1.2 m wide, chevrons pointing along the drive), and a `BrakingZone_01` 25 m before every place the line
turns more than 50 degrees within 20 m (one per 40 m at most). Road tests (gates on road links) get no line: a
straight chord would cross buildings. `-noracingline` turns it off.

Evidence: `docs/images/licence_slalom_racing_line.webp` (the weave beside the pole line),
`licence_cone_lines.png` (the RSA7T02 turnaround box), `licence_braking_zone.png`.

## 11. The school desk: R_SCHOOL.vmf (session 2026-11-05, `win`)

The owner: *"can we try find the correct ui menus"*. The retail desk is `FrontEnd/HiRes/r_school.bnk`
(R_SCHOOL.vmf). Its sprites are `DefineSpriteEden` (tag 127, an extra u32 before the tags); `tools/vmf_disasm.py`
walks them now. The protocol [DATA: the movie's scripts]:

- root `open_school` (f1): `SetBDD`, `FSCommand:rsin`, then the walk-mode cursor (`school_wm`). Picking the tutor
  there is `ItemType('valid')` with `_root.SelItemType = 'tutor'`: `gotoAndStop('school_part')`, `Open()`, `camn`.
  The port's interior walk already picks the tutor, so the game makes that call itself.
- the licence card / carousel (`licences`): `rsch:stli` asks for the licences: `_root.li<n>_state` (`done` /
  `current` / `locked`, the card's state frames), `li<n>_gp1` / `li<n>_gp2` (the class badge frames `A1` .. `C4`
  or `empty`), `nb_licences`, `num_licence`, `/:LicenceDate` ("25SEP1986" in the movie's own test data, cut into
  day / month / year). `rsch:dili` reports the licence under the cursor. Left / right step it; A opens its tests;
  B is `BackToWM()`: `camf`, `wmon` (back to the walk).
- the test list (`/tests`): `rsch:dits` asks for `nb_tests_total`, `num_test` and per line `line<i>/:t_name`,
  `:type` (icon frames `turn turn90 turn180 acceleration braking circuit mixed overtaking slalom obstacles jump manual
  drift`), `:meteo` (`sun rain night`), `:place` (`r_school` = the cone, `road` = the road sign), `:done` (the
  checkbox). `rsch:dtup`: a line is highlighted, the game sets `_root.TestDesc` (the "Test's objective" panel).
  A plays the line's `valid` and the list sends `rsch:tspr` with `_root.num_test`: start that test. B goes back
  to the card.

The port (`game/freeride/school_screen.*`, `GMFreeRide::openSchool`): the desk, `-schoolmenu <A|B|C>` and the result
screen's Quit open R_SCHOOL.vmf (`-nativeschool`: the old native panel, also the fallback when the movie is missing).
Filled: the licences of the school (GetSchoolLicenses; state done = obtained, current = the previous one obtained
[INF: SchoolMenu's rule]), the class badges from `_vehiclecategory_1/2`, the date = today [INF]; each test's title
(dbna), place (road gates -> `road`), weather `sun` [INF: no field], done = passed, and the description (dbd1) plus
the target time. The `.ig2` files have no test type: the icon comes from the title's words, then the course (VEHI
traffic cars -> overtaking, slalom poles -> slalom, road gates -> circuit) [INF]. Any test of the list can be
taken (the movie locks none; the port's in-order rule was [INF] and is dropped here).

Not filled: the card's player fields (photo, name, number - their variables are not traced yet); the help system
page (`openHelpSystem`, `FirstTime`).

Evidence: `docs/images/licence_school_card.webp` (the card after the result screen's Quit), `licence_school_tests.webp`
(the A7 test list). Checked: `-schoolmenu A -loglicenses`: `rsch:stli`, `dili`, `dits`, `dtup`, Down, A ->
`rsch:tspr` (num_test 1) -> RSA7T02 starts; B, B -> `camf`, `wmon` -> closed.

## 12. What the dev build (Apr 2012, readable code) says about tests, card and photo (2026-11-21, `main`)

Source: `TestDrive2DEV.exe` read with `tools/devbuild_disasm.py` (addresses are the dev build's, not retail's; the dev
package data is byte-identical to retail, so the `.ig2` files are already the real test data). Tags: `[CODE]` read.

**The test result carries a penalty, a speed-gauge test type and a time-out flag** [CODE: function at `0xb5c83d`
fills the result movie; strings `0x10f3cc4..0x10f3eac`]. Variables set: `/:basetime`, `/:penalty` (text built with a
leading `+`, only for a scored test), `/:IsTimeOut`, `/:IsTestScore`, `/:IsRacingSchool`, `/:ref_score`,
`/:time_out_rs`; the HUD has `/HUD/reach_speed` / `HUD/speed_gauge` with `/:ReachSpeed`, `/:speed_min`,
`/:m_SpeedUnit`. So the retail final score is **base time + penalty** (the port has none), and some tests are
"reach this speed" tests with a gauge (the port's gate/timer model has no such type). The message text is picked from
loc ids `0x659` / `0x65a` / `0x65b` (1625-1627) by the failed / time-out flags [CODE]. Next: read how the penalty is
accumulated (calls near `0xb5cafc`, the object at `[ebp+0x14]+0x70`) and what raises it (barrels / cones hit?).

**The licence card** (R_SCHOOL.vmf, function `0xb915c0`) [CODE]: `/:LicenceNumber` is the **constant text
`411 14 38 42 6116`** for every player (not derived); `/:LicenceDate` is the licence's obtained timestamp (FILETIME at
licence+0x30) formatted `%d%s%d` = day, month name (table `0x1674fa0`), year (+1900), a single space when the licence
is not obtained; `/:LicenceTxt` is `"%28s %d"`; `/:LicenceName`, `/:LicenceLevel`, `/:LicencePlayerName` come from the
licence / avatar records; the card shows one of `/licences/licence/state_a|b|c/lure` by the licence's group (1 -> a,
4 -> b, 2 -> c [INF: school A/B/C]). `nb_tests_done` is the count of passed tests, `num_test` 0.

**The photo** [CODE]: the retail-era picture system is `GSNetworkPictures.cpp` / `NetworkPicture.cpp`. A function
(`0xa6c2f0`) reads **`Licence00.jpg`** from disk into a buffer (up to 0x7d000 bytes) and hands it on as the avatar
picture; the ID card shows it at `pause_menu/data_extd/idcard/avatar_photo/lure` (also `gameprogress_menu/.../idcard/
avatar_photo/lure`, `db_progress/.../player_IDCard/avatar_photo/lure`; `:IsPhotoLoaded`). Photographer shop screenshots
use `Photo/%02d%02d%02d%02d%02d%02d%c.JPG` (a separate path). Cinematic `C_WIN_LICENSE` is loaded by the card
function's owner (`0xb915c0` looks it up by name). **Not found yet**: what writes `Licence00.jpg` (the camera shot
taken in the win scene) and when `C_WIN_LICENSE` is triggered; start at `--callers 0x40453b` (the caller of the
loader) and the writer of the `Licence%02d` name, and at the `GMScreenShots` / `PhotoDisplay` code.

### 12.1 The win scene and the photo (follow-up, same session)

- **The scene is real data and already extractable** [DATA]: each school interior bank has `irs_<a|b|c>_sc_M_WIN_LICENSE`
  (A 15.51 s / 6 shots, B 12.25 s / 3, C 16.29 s / 6; `otdu2_cine_tool list <interior>/irs_a.bnk`). The interior's own
  entity list names it (`IRS_A` -> `C_WIN_LICENSE`, beside `C_LIFE`, `Marco_Luzzini`, `D_RacingSchool`). Only the `M_`
  (male) timeline exists; there is no female variant in the data.
- **Its messages** [DATA, irs_a]: `play` t0; `zcne` 0-12.6 and 1.97-15.6; `zcde` 0-1.97; **`phfl` at 12.6 s (open end)
  and 15.12 s (0.37 s)**. `phfl` (read as "photo flash" [INF]) is the camera-flash cue: the shot is taken at 12.6 s on
  the cam_win_03 / cam_win_04 close-ups (fov 20), and the 0.37 s flash at 15.12 s ends the scene.
- **The game code reacts to the cue** [CODE]: the school movie's message handler (dev `0xb920b6`) compares the message
  to `'phfl'` (`0x7068666c`), looks up the licence being awarded and stores a state (`+0x1bba58 = 1`,
  `+0x1bba5c` = an id from the licence); the card then reads the saved picture (`Licence00.jpg`, `0xa6c2f0`). The
  write of the JPEG itself was **not** found: the screenshot writers present are `ScreenShotTaker.cpp` / `ScreenShotLoader.cpp`
  (`%s\%d.jpg`, `0xa7679c`) and the map-screenshot manager, not an obvious `Licence` writer [INF: the `phfl` flash
  captures the frame to the profile photo].
- **Built in the port (session 2026-11-21, `main`)**: a licence just obtained (the pass that completes its tests) walks
  into the school (`ERS_<letter>`), plays `C_WIN_LICENSE` with the player's avatar and Marco Luzzini (it cannot be skipped),
  captures the frame at the first `phfl` to `<userdata>/Licence00.png` (`licence_photo.*`; the retail file is a JPG [CODE]),
  and opens the school desk when the scene ends. The pause menu's profile card binds the photo to
  `/pause_menu/data_extd/idcard/avatar_photo/lure` (the clip exists only after the page has been shown, so it is bound on the
  first panel refresh; cropped to the middle 53 % of the 16:9 frame for the portrait slot [INF]). Hooks: `-winscene <A|B|C>`
  (plays it 120 frames in, no test needed), `-nowinscene`, `-dumpidcard <depth>` (the pause clip tree).
  Evidence: `docs/images/licence_win_scene.webp`, `licence_photo_taken.webp`, `licence_photo_idcard.png` (headless:
  `-gm FREERIDE -fixedtimestep -winscene A`, log `LicencePhoto: frame -> .../Licence00.png`, `FREERIDE: win scene ended (photo 1)`).
  Not done: the retail result screen is skipped for a new licence (the scene replaces it [INF]); the licence card in
  R_SCHOOL does not show the photo (its player fields are untraced); the second `phfl` (15.12 s, 0.37 s) is not used;
  only a male scene exists, so a female avatar plays the male timeline's avatar actor [INF]; and the scene's start is
  frozen at the world, not at the player's car pose.
- **Penalty, what the dev build says** [CODE]: the result movie's `/:penalty` is a **time**: the formatter at `0xb5cabf`
  builds it like the other times (a minutes/seconds string, then `strchr(.., ''')` and a `+` written over the first
  character), so the final score is **base time + penalty seconds**; `/:IsTimeOut` comes from the test object's
  `+0x2ba0` method. The HUD side has `PenaltyTimeLevel%02d` / `PenaltySpeedLevel%02d` / `PenaltyScoreLevel%02d` (+
  `PtsCnt...`) config keys and `OFF_ROAD : State / Penalty:%.2f/%.2f`: penalties come in three kinds (time, speed, score)
  by level, and the off-road one is a running value. **What adds to a licence test's penalty is still not decoded**.
- **A lead on the props** [DATA]: `ChallengeIDCard.cpp` (the challenge editor) names the actors **`RS_Separ_A_act`**,
  **`RS_Separ_B_act`** and `SF_Stud_act` beside the checkpoint types "Checkpoint Gate / Cone / Normal"; `RS_Separ_A/B_act`
  exist in both islands' `worldactors.bnk` (model `RS_SEPAR`, material `RS_Separ`). These are the racing school's
  separators, so they, not the pack_cp barrel, are the likely retail slalom pole / cone-line piece (section 10 said no
  cone exists: it looked in the wrong bank). Next: load `RS_Separ_A_act` like the scene editor's "actor" objects do
  (`scene_objects.cpp` resolves a name through worldactors' `entities.xmb`), look at its size, and swap it in for the
  barrel in `GMFreeRide::buildCourseMarkers`.

### 12.2 The 360 reference clips (2026-11-21, `reference/licence_school_360/`)
- **The result cart text is the game's loc** [DATA + capture]: 1625 "You failed the test. Try again! With practice, you will
  master it.", 1626 "Well done! You've succeeded! Let's go on to the next test.", 1628 "Well done, you've obtained the
  §LICENSENAME§ license. You can now access new championship(s)." (dev code picks 0x659 / 0x65a / 0x65b by the flags).
  The movie's `openCarts` shows `message2` only when `_root.IsRacingSchool` is true and `Message2 != 'empty'`; the port never
  set `IsRacingSchool`, which is why the fail cart was blank (spec 9's "icon-only" guess was wrong). Now set, with
  `IsTestScore`, and the text from the loc ids (`license_result_screen.cpp`). Retail's fail cart shows only "Final time";
  the port still shows "Ref. score" beside it (label is static in the clip) - not fixed.
- **The licence card shows the player's portrait** (the same fixed per-avatar picture as the pause card) with name,
  "Tests 6/6", "License approved", the stamp and `411 14 38 42 6116` (clip 01) - so the photo is not generated at the win
  scene's flash; the port's frame grab (12.1) is the wrong source. The rest of the findings are in the clips' `NOTES.md`.

### 12.3 The test files' school keys, cones, weather and cars (2026-11-21, `main`)
The six C4 files (`rsc4t01..06.ig2`) differ in these MDSC keys, and the 360 capture agrees on every one [DATA]:

| key | T01 .. T06 | meaning |
|---|---|---|
| `rswh` | 0 0 0 1 0 2 | the test's weather: 0 sun, 1 rain, 2 night (the school list's `meteo` icons) |
| `rsl1` | 0 0 1 0 1 1 | the place icon: 0 the school grounds (cone), 1 the road (sign) |
| `rsl2` | 7 3 0 7 4 9 | the type icon: 0 turn, 3 slalom, 4 overtaking, 7 braking, 9 circuit (the other ids follow the icon strings' order [INF]) |
| `VMod` | 500 500 501 501 502 502 | the school's own car (Delta / Esprit / Mustang) |
| `PeLv` / `offR` / `OrMd` | 0 / 0 / 0 ... 1 / 1 / 4 on T06 only | the **off-road penalty**: T06 (the road circuit) adds time for leaving the road (the HUD's "Penalty" readout, dev `OFF_ROAD : State / Penalty`) [INF from the keys; the amounts (`PeCr` 100) are not decoded] |

Built in the port: the list icons read these keys (`school_screen.cpp`); a school test pins the weather (`rswh`: clear 15:00 / rain block 3 at 15:00 / 23:00,
`applyTestWeather`, `-notestweather`) and releases it at the school; **a pole or cone under the car's footprint fails the test at once**
("Hit a cone"; the slalom poles and DEC2 cones, `carHitsCoursePole`, `-nocones`; the owner's capture: a cone hit is an instant fail; the footprint is
[INF]); and a test whose `VMod` differs from the loaded car **reloads FREERIDE with it** (the StoryHandoff's `CarId` / `StartTest`: the model cannot be
swapped at runtime, car_delivery.md, and retail loads the world per test), leaving the school in a school car reloads with the player's own car at
the door (`-noschoolcar` keeps the old one-car behaviour). Evidence: `-licensetest RSC4T02` driven straight: "a cone was hit at 7.0 s"; `-licensewarp`
through the openings still passes; RSC4T04 rain and RSC4T06 night (3D shots); RSC4T01 loads the green Delta (`Delta_Integrale_Evo_2`) after one reload.
Not done: the schools' liveries (retail's Delta wears red / white stripes, roundels and "Classic Racing School" text; the port shows the plain
model), the leave-the-school reload and the Esprit / Mustang rows were not run end to end, the off-road penalty itself, the afternoon / dusk times
of tests 1, 3 and 5 (the files carry no time of day), and the barrel for the cone model (`RS_Separ_A_act`, 12.1).

### 12.4 Follow-up: the leave reload, the liveries, and what else the data holds (2026-11-21, `main`)
- **Leave reload verified**: `-gm FREERIDE -licensetest RSC4T01 -schoolleavetest` reloads into the school's Delta, walks into the school, and out: "the world
  reloads with the player's car at 'ERS_C'", the red California on the school's road, 0 stalls, no device wedge. `-schoolleavetest` is the test hook.
- **No livery data was found** [DATA, negative]: cars 500 / 501 / 502 share the files of the ordinary Delta Integrale Evo 2 / Esprit S3 / Mustang Fastback 68
  (`DBCarPhysicsData` rows 256 / 255 / 257, category 17); `DBCarsAndColors` has **no rows** for `_car` 500-502 (the Delta's rows belong to id 355 / 0x163);
  the three car banks hold only the stock maps; `colors.bnk`, `common_car.bnk` and the 26 `DBStickersBanks` banks have nothing school-named. The
  retail stripes / roundels / "Classic Racing School" text (360 clips) must come from a sticker layout or a paint the shipped tables do not list - not
  invented here; the port shows the plain car.
- **A bank the port does not use**: `scenarics/packs/pack_racingschool.bnk` (+ `__us`) holds the **end-of-test cinematics** (`M_CLASSIC_/DIRT_/ASPHALT_WIN_nn`
  and `_LOOSE_nn`, `totem_WinLoose`), the NPC animations (Marco `rsa_*`, Jane `rsb_*`, Todd `rsc_wintest_todd` ...), the avatar's win / lose
  animations (`rsa_wintest-m/-w`, `rsa_loosetest-m/-w`), FaceFX (`rsc-m/-w`) and the voice lines `sound/voices/us/rs{a,b,c}/rs?_endtest_win|loose_nn`.
  Retail plays a cinematic with the instructor's line after a test; the port goes straight to the cart. Next: read the pack's entities.xmb / the
  `.cin` timelines (`otdu2_cine_tool list` it) and play one under the result cart.
- **A bug seen on the way**: `-schoolmenu C` opens the desk with the **Asphalt** card (A7 / A6) although `GetSchoolLicenses('C', 0)` has the C4 licence
  (`DBLicense[3]`); the card also has no photo / name / "Tests n/6" yet (retail: "Classic License", C4 badge, the portrait, the stamp). Start at
  `SchoolScreen::fillLicences` and the movie's `li0_gp1` / `LiGpFrame` frame names.

### 12.5 The school card, and the instructors' end-of-test lines (2026-11-21, `main`)
- **Why school C showed the Asphalt card** [CODE: the movie's own script + the port's AVM1]: r_school's `SetCurLicence` takes the card variant from the first letter of
  `li<n>_gp1` (`StringExtract( gp1, 0, 1 )` -> `licence/state_<letter>`). SWF 4 `substring` is 1-based and the Flash player clamps an index below 1
  to the first character; the port's `A_StringExtract` returned "" for index 0, so every school fell back to the default `state_a` art (A looked right
  by accident). Fixed in `flash_player.cpp` (an AVM1 law, so other movies that extract with index 0 change too). `-schoolmenu C` now opens
  "Classic License" with the C4 badge.
- **The card fields** (`SchoolScreen::fillCard`, [CODE-dev 0xb915c0]): `/:LicencePlayerName` (the profile name), `/:LicenceNumber` (`411 14 38 42 6116`),
  `/:nb_tests_done` / `/:nb_tests_total`, `/:LicenceDate` (today's when the licence is obtained - the port stores no obtainment date - blank before) and
  the portrait bound to `licence/state_<a|b|c>/lure` when `Licence00.png` exists (the photo source is still the open question, 12.1). Verified:
  Danny, "Tests 2 / 6", "License in progress". `-dumpschool <depth>` dumps the movie's clip tree at the first card.
- **The instructors' line**: `school_voice.*` plays `rs<letter>_endtest_win_01..02` / `_loose_01..04` of `pack_racingschool__<lang>.bnk` under the result cart
  (Marco A, Jane B, Todd C; 2.3-3.1 s clips; one picked at random [INF]); `-noschoolvoice` skips it. Verified by log (load + play), not by ear.
- **Not done: the end-of-test cinematic itself.** The pack's `M_<SCHOOL>_WIN_nn` / `_LOOSE_nn` (7.4 s, 2 shots) and the 8 s `_LOOP` ones are anchored to the
  car (cameras within a metre of the origin, three actors: the avatar and the instructor in the cabin - the 360 fail clip shows the windscreen shot
  with two figures inside, then the loop under the menu). Playing them needs a car-anchored scene player in the world (the avatar + the NPC rig in the
  car's seats, `loadOccupants`, the `.sce` placed at the car pose) - the garage / car wash scenes run in their own frame instead. Start at
  `IntroCinematic::setCinematicOrigin` with the car's pose and the NPC animations `rs?_wintest*_<npc>.anm` of the same pack.

### 12.6 The end-of-test cinematic, first version (2026-11-21, `main`)
Built (`school_end_scene.*`, `GMFreeRide::startEndOfTestScene`): under the result cart the pack's camera takes over, anchored to the car (the scene origin = the car
world matrix; `cam_<school>_03` is in that frame: 4.4 m ahead, 2.5 m to the passenger side, 1 m up, yaw -160 deg - verified: the shot frames the car from its front
left), the school's instructor rides as the seated passenger (`SPT_Marco_Luzzini` / `SPT_Jane_Omahan` / `SPT_Todd_Bishop`, DBPNJ rows 66 / 64 / 72), the 7.4 s main
cinematic plays once (`M_<SCHOOL>_<WIN|LOOSE>_nn`, one of the numbered ones at random [INF]) and the 8 s `_LOOP` repeats until the menu answers. `-noendscene` skips it.
What differs from retail: the cabin close-ups (`cam_*_loose_01/02`, `win_01`...) have a **NaN x, sit 0.8 m below the car origin and carry an identity heading** - keyed to
an actor's head bone [INF] - so those shots fall back to the loop's exterior camera; the cinematic's actors (`Avatar`, `Instructor_*`, animations
`rsa_loosetest-m/-w`, `rsc_loosetest_todd` ... and the FaceFX / voice Fx) are not played - the port's seated driver and passenger stand still; and the `.sce` hides the
car's seat meshes (`Exclude ... DrvSeat_HR / PsgSeat_HR`), not done. Next: animate the two actors from the pack's `.anm` files (the scene's `Animation` track values index its
REFS; `FadeFreeride::draw` is the sampling template) and resolve the head-bone parenting of the close-up cameras.

### 12.7 The cabin close-ups are look-at cameras (2026-11-21, `main`)
The "NaN x" of 12.6 was a parser bug, not a head-bone parenting [DATA]. In these scenes (`cam_<school>_loose_01/02`, `_win_01/02`) a **Position key is 4 floats - a leading
flag dword (0, 1, 0xffffffff, a NaN pattern) then x y z** - and was read as 3, shifting every value by one; the **Heading key is a look-at record: a small integer
(1, 2) then a 3-float offset**, not a quaternion [INF, from the values]. The integer is the 1-based index of the actor in the scene's actor order (1 `Avatar`, 2
`PLAYER_CAR`, 3 `Instructor_*`) and the offset is in the target's space: `(0.07, 0.76, -0.09)` on the Avatar = the seated driver's head, ~0 on the car. So
`cam_*_loose_02` sits at (-0.88, 0.81, 2.30) [scene frame] looking at the driver's head through the windscreen and `cam_*_loose_01` at (-0.80, 0.88, 4.07) looking at the car.
`CinematicScene` now repacks both (`Track::LookAtActor`) and `Cinematic::getCamera` builds the look-at rotation (yaw about Y, then pitch; the pitch sign was checked by
eye). Evidence: `docs/images/school_end_scene_windscreen.png` (two figures behind the glass), `school_end_scene_loop.png`. Other scenes with 4-float position keys are repacked
the same way (no NaN positions remain in a dump of every cinematic).
The actors now play the pack's clips: `SchoolEndScene::getActorClip` reads the actor's `Animation` track as `IntroCinematic::sampleAnimationTrack` does (key fields 0 ref,
2 clip start, 7 blend-in), looks the clip up in `pack_racingschool.bnk` by the REFS name, and `GMFreeRide` feeds it to `SeatedAvatar::setOverride` under the result cart
(`Avatar` -> driver, `Instructor_` -> passenger). Verified: the driver and instructor move between the 3.2 s and later frames. Not done: the blend between keys, FaceFX / voice sync,
the seat meshes (`Exclude ... DrvSeat_HR / PsgSeat_HR`), a win-scene look, and `-logendscene` shows one key whose clip is named `none`. `-logendscene` dumps the objects and the heading keys.
Test setup note: the shared `C:/temp/otdu2-win-userdata` profile has an empty `avatar_config`, so no driver / instructor is seated there; use a copy with `avatar_config = AVT_Male_Intro`.

### 12.8 The school penalty (2026-11-21, `main`)
What the data says, and what the port does with it:
- **Rules** [DATA, `scenarics/events_config.cpr` (decrypts with `tools/x360/licence_dump.py`)]: an `EVENTS` `PENALTY` mode with `complete_offroad_coeff` 2.0, `partial_offroad_coeff` 1.5,
  `collision_coeff` 0.05, `road_collision_coeff` 0.025, `max_collision_points` 10. **Levels** [DATA, `DBEnvironnement`]: `PenaltyTimeLevel01..03` = 10 s,
  `PtsCntPenaltyLevel01..03` = 100 / 55 / 25 points (and the Speed / Score twins: 20 and 500). The file's `PeLv` picks the level, `offR` > 0 turns the off-road rule on (T06: 1 / 1).
- **The model** [INF]: points accrue per second at the coefficient (some wheels off the tarmac = 1.5, all of them = 2.0); the time penalty = points / `PtsCntPenaltyLevel0n` * `PenaltyTimeLevel0n`
  seconds. It fits the 360 capture (T06 penalty 0.41 s = ~4.1 points, i.e. a few seconds of partial off-road). Collisions (`collision_coeff`) are not accrued: the port has no impact magnitude to feed them.
- **Built** (`LicenseTest::addPenaltyPoints` / `getPenaltySeconds` / `getFinalTime`, `GMFreeRide::tickLicenseTest`): per-wheel `Surface != 0` (dirt / grass) counts as off the road; the final score
  is base + penalty (the time-attack target compares against it); the result cart shows `/:BaseTime`, `/:penalty` ("+00 : 01 : 20") and `/:IsPenalty` (its own "Basic time / Penalty / Final time" rows,
  like the 360 T06 cart); the HUD shows the generic gauge in its `penalty` mode ("Penalty" + hand icon + bar, the value in the cursor text `/:Score_driving`, `FreeRideHud::setPenaltyGauge`).
  Evidence: `docs/images/school_penalty_hud.png`; `-licensetest RSC4T06 -penaltyrate 2 -licenseforce fail -licenseforceafter 6`: Basic time 6.00 + Penalty 1.20 = Final 7.20 (12 points / 100 * 10 s).
  `-penaltyrate <points/s>` is the test hook (any surface). Not done: collisions, the HUD value sign / format against retail beyond "-00'41", the bar fill direction.

### 12.9 The pre-test presentation (2026-11-21, `main`; owner: "a banner in top left ... a run being made ... blackscreens to loop again")
[DATA, the 360 clip `02_RSC4T01_First_step.mp4`]: after the test loads, the screen shows a banner top left (the test icon, the title, the objective text, the place and weather icons) and the
"Help / A OK / B Cancel" bar, over a **demonstration**: the school car drives the course along the racing line (cuts: behind the car at the start, the cockpit, the chase view, a wide shot
over the barriers), the picture fades to black at the end of the run and the demonstration starts again, until A (the test starts: fade, start line, 3-2-1) or B (cancel).
Built (`school_test_intro.*`, `GMFreeRide::tickTestIntro`): the car is moved along the start pose and the gates of one lap (smoothed heading, ~17 m/s), camera cuts behind-start / chase / a low side shot / a
wide high shot, a black fade at both ends of the ~12 s loop, the banner drawn natively (title, place / weather text, the objective wrapped), the bottom bar; `LicenseTest::setHold` holds the countdown;
A (`IA_MenuValidate`) starts, B (`IA_MenuBack`) abandons. Evidence: `docs/images/school_test_intro.png`; headless `-licensetest RSC4T01 -introstart` (starts after the first loop) reaches "3 2 1".
Flags: `-nointro` skips it; the scripted hooks (`-licenseforce`, `-licensewarp`, `-licensepark`) skip it unless `-testintro`; `-introstart` starts after one loop.
Differences from retail: the banner is plain rectangles and text (the retail toast has the test's icon and weather / place icons - the school list's icons could be reused); the demo car is the player's own car
placed along the gate path (the retail run follows the racing line and shows the school car with its liveries); the retail cockpit shot is replaced by a side shot; the cuts and the times are the port's own.

### 12.10 The real pre-test banner is RESULTS.vmf `preview_in` (2026-11-21, `main`)
[CODE-dev] `GMCompDrivingSchool`'s event handler (`0xb5d730`) goes to the results movie's frame `preview_in` (string `0x14f55a4`) when the school state machine reaches its intro
(`EventIntro` / `DrivingSchoolResults` / `DrivingSchoolReplay` are in the state-name table `0xb42a50`); the key events `0xc0c0 / 0xc0c1 / 0xc0c2` are OK / Cancel / Help. [DATA] The
movie's `/desc` clip is the banner of the 360 clip: `/:TestName`, `/:TestDesc`, and three icon clips `type` (103 frames), `place` (26), `meteo` (22) whose frame labels are the school list's
(`braking`, `r_school` / `road`, `sun` / `rain` / `night`), plus the Help / OK / Cancel bar; `hsysID = preview` is the Help page. `LicenseResultScreen::openPreview` drives it
(`-nativebanner` keeps the old native rectangles); the icon names are shared with the list (`SchoolTestIconNames`). Evidence: `docs/images/school_test_banner_retail.png` (brake disc, cone, sun) -
matches the 360 frame. The demonstration itself is a **replay**: strings `GMCompDrivingSchool replay trajectory` (`0xb5c210`) and `DrivingSchoolReplay`, the handler compares the replay time to the
replay length (`[edi+0x44]`) to restart it, and `ig2/replays/rsc4t0n.rly` exist for every test - so the demo run (and the retail racing line) come from those files, not from the gate path the port drives now.
Inspector flags added: `-dumpflashlabels` / `-dumpflashactions` (every loaded movie's root labels / action strings), `-resultlabel <label> -dumpresults <depth>` (open any RESULTS.vmf label on a result).

### 12.11 The test replays (`ig2/replays/<test>.rly`) and the retail demonstration (2026-11-21, `main`)
[DATA, decoded from `rsc4t01.rly`, same layout in the other 58 files]: `u32 version (18)`, `u32 end of the recording data (31953)`, `u32 XMB size`, the XMB `ReplayBaseParams` (chunk sizes / rates / counts, `FrameCount` 1681,
`VehicleInfo[0].Id` = the car id 500), `u32 car chunk size (1011)` + the car chunk, `MatrixChunkNumberUsed` (94) matrix chunks of `MatrixChunkSize` (105) bytes - one per `MatrixChunkRate` (18) frames - then
`InputChunkNumberUsed` (561) input chunks of 11 bytes (one per 3 frames), then `FrameCount` timestep chunks of 8 bytes: **`float dt = 1/90 s` and `float time of day = 54000` (15:00)**. 1681 frames * 1/90 = **18.68 s**, the length of the
360 demonstration. A matrix chunk: 4 header bytes (`01 f4 01 00`), then **at +9 the car's x y z as unaligned floats** (T01: 11902.4 5.72 16040.1 = the start line, matching the port's world) and **at +25 three 8-byte rows of int16 / 32767 = the car's
x, y, z axes** (z = forward: -0.10, -0.01, -0.995 at the start). So the keyframes give the whole drive at 5 Hz; the in-between frames (the packed deltas, 105 - 45 bytes) and the input chunks (steering / pedals) are not decoded. The
35 920 bytes after the recording data (offset `hdr[1]`) are another section (starts `03 00 00 00 31 02 00 00 ...` then a position + 1.0) - not read; a lead for the retail racing line.
Built: `TestReplay` (`test_replay.*`) reads the keyframes; `SchoolTestIntro::startReplay` drives the car from them (`-noreplaydemo` falls back to the gate path), the loop is the recording + the black fade. Evidence: log
"TestReplay: ... 94 keyframe(s), 18.68 s, car 500, clock 54000 s"; the car brakes into the zone at the end like the capture (`docs/images/school_test_replay_demo.png`). Tools: `tools/rly/rly_probe.py`, `rly_ts.py`.
Not done: the retail camera cuts (the clip's cuts are not in the .rly - likely the test's camera data), the racing line from the second section, the Help page (`hsysID = preview`), the per-frame deltas (the demo is 5 Hz linear).

### 12.12 The school HUD and camera differ by place (2026-11-21, `main`, from the 360 clips 02 / 03 / 06 / 07 and the dev build)
[DATA, 360 clips] **Pad tests** (`rsl1` 0: T01 / T02 / T04) are driven **from the cockpit** (the driver view with the hands on the wheel) with a minimal HUD: the flag counter `n / N` top left, the red **hourglass
timer counting the time left down** top right (00'25"00 ... on T02), the gate split under the counter (0'10"40), the speed as digits (mph) bottom right - no minimap, no gauge, no goal pill. **Road tests** (`rsl1` 1: T03 / T05 / T06) keep
the chase camera, the minimap and the speedometer, a count-up timer with a stopwatch icon and **the target time under it (the laurel, 00'37"00 on T05)**, and the Penalty gauge on T06. The rule that picks the timer: a test whose target
equals its limit (`timG` == `CtDw`: T01 23 / 23, T02 25 / 25, T04 33 / 33) is limit-driven (hourglass countdown), the others count up with the target shown [INF: fits all six clips].
Built: `GMFreeRide::applySchoolCamera` / `restoreSchoolCamera` (driver view 23 for pad tests, given back at the end), `FreeRideHud::RaceHud::Limit / Target / bHideMinimap` (the HUD's `/HUD/time_cd` countdown, `/HUD/ref_score` target, minimap
hidden), no goal pill for a school test; `-noschoolhud` turns all of it off. Evidence: pad test T01 in the cockpit with the hourglass counting down, road test T05 with the target under the timer.
Dev build ([CODE] `0xb5c210`): the school mode records the player's own run in a replay object named "GMCompDrivingSchool replay trajectory" (the result menu's Video Replay; locked in the 360 capture) and finds the world's `Trajectory` entity
(the racing line) by name, setting two flags on it.
**Still not implemented** (seen in the clips, not built): the in-test "TIME REMAINING" banner and the 6..1 seconds count (`/HUD/time_out`, `/:time_out_rs`), the per-gate split time + green / red gap row for the school (the HUD rows exist; the
port shows them but the clip's layout - split under the counter - is not compared), Video Replay (the player's run playback), the school cars' liveries (not in the data, 12.4), traffic in the T05 demonstration (the replay's car chunk holds the traffic cars [INF]),
the instructor / driver standing at the car before the tests, the retail camera cuts of the pre-test presentation and the racing line from the second `.rly` section (12.11).

### 12.13 The school liveries applied; the overtaking traffic (2026-11-21, `main`)
**The livery sheets fit the PC models' UVs.** The three 1024x1024 sheets found in the 360 texture dumps (`docs/re/x360_texture_dump.md`) are the body's *sticker layer*: bound on the paint materials' USER_TEX slot (the TD_CarPaint2 s5 slot the
sticker shop uses, `CinematicCar::bindStickerTexture`) they land exactly on the PC Delta / Esprit / Mustang bodies - the winged "Classic Racing School" logo on the bonnet, the stripes, the roundel (`docs/images/school_livery_delta_applied.webp`,
`school_livery_mustang_applied.webp`; Esprit checked the same way). Built: `GMFreeRide` binds `<userdata>/school_liveries/<car id>.png` (500 Delta, 501 Esprit, 502 Mustang) on a school car, `-liverytex <png>` tests one. The sheets come from the owner's 360 capture,
not from the PC game data, so they are not in the repository: copy `delta_07.png`, `esprit_70.png`, `mustang_71.png` from `E:\TDU2-texdump\school_liveries\` to `<userdata>\school_liveries\500.png / 501.png / 502.png`.
**Test 5's traffic is decoded but not built.** Each `VEHI` entity of `rsc4t05.ig2` (3 of them, 304 bytes) holds a 3x4 matrix (position x y z = 13291 110 20253, 13010 112 20193, 12896 117 20022; the first rows are the car's axes, heading ~ -0.96 / -0.5 / -0.94 on z)
and keys `Trty 1`, `Stch 1 / 2 / 3` (the order), **`Spmx 22223 / 25001 / 27778` (mm/s: 80 / 90 / 100 km/h)**, `VSpe 2`, `VSpg 69`, **`VSph 500` = the car id: the Lancia Delta (the school car, with its livery)**, `Firf 2001`. So the overtaking
tests' traffic are three Delta 500s with the 07 livery driving ahead of the player at 80 / 90 / 100 km/h (owner: "the 5th test uses the Lancias with the Lancia's livery for the overtake cars"). The port reads the count only (`Ig2Challenge::numVehicles`) and
spawns nothing. Next: a small scripted-traffic class (load three car-500 models, drive them along the gates at `Spmx`, fail the test on contact) - `RaceOpponents` is a grid-race AI and does not fit.

### 12.14 Test 5's traffic built (2026-11-21, `main`)
`Ig2Challenge::getVehicles()` reads the VEHI records (position from the 3x4 matrix at +32, heading from its z axis, `VSph` car id, `Spmx` mm/s). `RaceOpponents::startTraffic / tickTraffic / findTrafficContact / assignSticker` (a second instance,
`GMFreeRide::schoolTraffic`) puts the three car-500 Lancia Deltas on the course (routed over the island road graph through the gates like the race AI): they stand during the countdown, then drive at 80 / 90 / 100 km/h from where the
files place them (144 / 442 / 666 m along the course in the port's routing) with the Delta livery as their sticker layer. A contact (a 4.4 x 1.9 m box test against the player's car) fails the test ("Hit a car", `eResult::HitCar`). `-notraffic` turns it off,
`-trafficgap <m>` (test) puts them right ahead of the start. Not verified live: the contact failure (the scripted drive left the road first), the retail lane behaviour (the cars keep the lane the file places them in, no lane changes), and their paint (the school Delta's first
DBCarsAndColors colour).
Owner note: later in the game the Mustang appears with a front lip and rear lips (e.g. Miami Harris's pink Mustang with white stripes), i.e. more liveries and body variants exist in the 360 data; capture them with the texture dump during the championship races.

### 12.15 Test 5 has no barrels (2026-11-21, `main`)
Owner (retail test 5): no barrels at the start; just overtake the 3 cars and reach the finish. `rsc4t05.ig2` holds one DEC2 cone line (1 -> 2) at the start box; on a test with traffic (VEHI) the port no longer draws the DEC2 lines (`-overtakecones` restores them). Verified headless: `-licensetest RSC4T05 -licensepark` logs 0 markers. [INF] the 3D look and a driven run are not checked.
