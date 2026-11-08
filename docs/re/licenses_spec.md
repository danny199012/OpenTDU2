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
