# Quests / missions: survey (2026-11-26, `main`)

Owner request: *"focus on quests; each quest sometimes has a small cutscene, especially driving the person to
the off-road school, and the drive-car-from-A-to-B ones all have data of which car it should be; the dev build
may help."* This is the **survey** only: no code changed. Tags as in `CLAUDE.md`.

## 1. What exists in the data

### 1.1 The goal strings (the quest list, as the HUD shows it) [DATA, `-dumploc`]

| loc | text | meaning |
|---|---|---|
| 4032 | Drive Tess to the shooting location | story step 4 (**playable** in the port) |
| 4033 | Buy a car! | used dealer |
| 4034 | Bank the first level with the F.R.I.M. | tuto FROG |
| 4035 | Drive to your first house | go home / caravan |
| 4036 | Join Todd at the Classic Racing School | story |
| 4037 | Join Tess at the Asphalt Racing School | story |
| 4038 | Join Jane who needs some help | `EVENJANE` |
| 4039 | Drive to the airport to fly to Hawaii | |
| 6111 | On the road to "Solar Crown" | step 3 goal |
| **6112** | **Drop me at the driving school** | **the "take the person to the school" quest** (no `DBEvent` row names it; not wired anywhere in the port) |
| 3855 / 3856 / 3857 / 3858 / 3859 | I'm feeling sick / Drive it to my garage / Drive me home / Do not shake me! / Stop him! | in-car passenger / delivery / chase prompts |
| 2400 / 2401 | TOP MODEL / HITCHHIKER mission help texts | TDU1-style passenger missions (take her / him to a destination in the time limit) |
| 7780 / 7781 | Drive the car to the destination without getting damaged / within the time limit and avoid getting damaged | the A-to-B convoy missions (DLC "Exploration pack": Timed Convoy, Extreme Convoy; 8 Ibiza + 12 Hawai) |

### 1.2 `DBEvent` rows that are story quests, not races [DATA, `-dumpdbtable DBEvent`]

`_eventtype 11` = the 5 duels (`DUELC4A`, `DUELB4A`, `DUELA5A`, `DUELA2A`, `DUELB3A`, each with a `_pnj1`
challenger: Miami Harris, Ryan Wilder, Tess Wintory, Stuart Wintory, Chris Wilder; their cars are in `DBPNJCar`),
`_eventtype 12` = `EVENJANE` (Jane O, $3000), `_eventtype 13` = `TESSEVEN` (Tess, the step-4 drive).
Neither Jane nor Tess (`DBPNJ` 0x17e / 0x293) has a `DBPNJCar` row, so **the DB gives them no car**.

### 1.3 The `.ig2` mission files `euro/bnk/ig2/*.ig2` [DATA]

Format and keys: `docs/re/licenses_spec.md` section 2. By `gmth` (the game mode type) the 538 files split as
0 = licence tests / casino (106), 1 (31), 3 = races (205), 7 = `EVI` / `EVH` events + `EVENJANE` + `TESSEVEN`
(121), 10 = speed (33), **11 = `CMK*` "Know your destination" (20)**, **12 = `CMF*` "Follow the leader" (22)**.

**The car key is `VMod`** (an `_id_car`, `-1` = the player's own car). A first scan of every file (a crude
fourcc scan, **not** the real parser; `VMod`s of 101 / 102 occur everywhere and look like template defaults):

- `EVENJANE`, `TESSEVEN`: `VMod -1` (the player's car), 2 `CHK2` gates, `CtDw` 210 s, `timG` 160 s: the
  Tess drive's numbers the port already uses. **So Jane's and Tess's quests carry no car in the ig2.**
- `CMK*` (11): 9 of 20 are `-1`; `CMKIB09` is car 231; the rest read 102.
- `CMF*` (12): 19 of 22 are `-1`; `CMFIB02` is car 71.
- `EVI` / `EVH` events with a fixed car: `EVHA3SH1` 275, `EVIA1CE1` 654, `EVIA2CE2` 657.

[INF] Open: **which car a quest wants is NOT visibly in these files for the story quests.** Either it is
carried by the (encrypted, `[ENC]`) story code, by the cinematic pack's `entities.xmb`, or the `VMod` values
above are right and the story quests simply use the player's car. Must be settled with a proper ig2 parse
(`source/game/ig2_challenge.*` reads the real structure) before building.

### 1.4 The cutscenes [DATA, `docs/re/cutscenes_inventory.md`]

`scenarics/packs/pack_events01..08` (race event kinds), `pack_events12` = **`event_jane`**
(`M_INTRO_E12_JANE_totem_winloose`, `M_WIN_E12_JANE...`, `M_LOOSE_E12_JANE...`, anims `ev_jane*_npc`),
`pack_events13` = **`event_tess`** (+ Solar Crown props, a Hawai truck: the C_TRUCK props the PROGRESS
note says are missing), `pack_events14/15` = the convoy phone intro / win / lose (`ev_convoy*`, `a-phone`
prop). Pattern: **one intro, one win, one lose cinematic per event number**, selected by
`C_INTRO_E%02d_%s` / `C_WIN_E%02d_%s` / `C_LOOSE_E%02d_%s` (the dev build's name patterns,
`dev_build_survey.md` 3.1). The port already plays `C_INTRO_E_1` for the car wash and has `GMCine`.
`pack_events12`'s `entities.xmb` (19 KB) and `pack_events13`'s (26 KB) are the scene scripts to read next
(`otdu2_bnk_tool extract` pulls them; the `xmb` dump command printed nothing for `entities.xmb` this session:
the schema has to be fed to it).

## 2. What the dev build (`TestDrive2DEV.exe`, readable code) offers [CODE-dev]

Source paths present: **`GM_Mission.cpp`** (`GM_Mission::Refresh()`), **`MockMissionInfo.cpp`**,
**`AiVehPlanDrivingSchool.cpp`** (an AI vehicle plan for the school), `Wrecks.cpp`, `GM_GMEditor.cpp`.
Strings: `AI Received MissionMsg: type %d`, `BehaviorPassenger`, `@animPassenger`, `InstallAvatarPassenger`
(+ `PlacePassenger` / `Removepassenger` / `IsInCopilotPlace`), `EVENJANE` / `EVENT_JANE`, `C_CHOICE_TODD`,
`MISSION`, `CONVOY` / `EXTREMECONVOY`, `/:IsPassenger`, `/:IsRacingSchool`, `/:convoy_event`,
`/:ClubDestination`, `/:t_wm_destination`, `DRIVING_SCHOOL`, `DrivingSchoolReplay` / `Results`,
`DESTINATION INFO`, `DistanceDropOff`. So the **passenger-in-the-car** machinery and the mission message
type are readable there, and `GM_Mission::Refresh` is the function to disassemble
(`tools/devbuild_disasm.py`, run **without** `-I`). Retail's own copy of this is `[ENC]`.

## 3. What the port has [from `PROGRESS.md`]

Step 4 "Driving Tess" is playable (gate, countdown, Tess in the passenger seat). Steps 5-8 and 11-13 have no
mission. Licence tests, races, duels (partly), phone calls, car wash and the casino trips exist. Jane's event,
the school drop-off, hitchhiker / top-model, the convoys and the car-delivery prompts do **not**.

## 4. Proposed order (smallest verified step first)

1. Parse `EVENJANE` / `TESSEVEN` / one `CMF` / one `CMK` / one `EVI` with the **real** ig2 reader and write the
   exact key table (answers the "which car" question properly).
2. Read `pack_events12` / `13` `entities.xmb` to learn how a quest cutscene names its car, NPC and camera.
3. Disassemble `GM_Mission::Refresh` and the `MissionMsg` handler in the dev exe for the lifecycle
   (start, passenger placement, drop-off trigger, result).
4. Build **one** quest end to end: Jane's (`EVENJANE`, step goal 4038) with its intro / win / lose cinematics,
   then "Drop me at the driving school" (6112) once its data source is found.

Not done this session: no code, no run. The ig2 scan in section 1.3 is crude and is the first thing to redo.

## 5. Convoy files decoded (2026-11-26, `main`) [DATA, real MDSC key parse]

- **`CMK*` (gmth 11) are "Keep your distance! (A..J)"** (`dbna` loc 4979..4988) and **`CMF*` (gmth 12) are "Follow the leader! (A..)"** (`dbna` 4989..). Loc 7149 / 7152 describe the rule: *"The goal is to drive as a convoy. Each player must keep a certain distance between himself and the player ahead ... the score is the total distance driven by the players in the convoy"* [DATA]. Both are **multiplayer / club** challenges (`CRC_FOLLOW_THE_LEADER_STARTROUND` in the dev exe), not A-to-B missions. Their `.ig2`: `CMK` = one start gate and nothing else; `CMF` = 7-11 road gates (the leader's route). Header keys that differ from the template: `ChKD`, `trat` (3 / 2 / 5), `VBra` / `VBrM`, `mnme` (a name string: `KYD Ibiza Sud`, `Mission Honolulu`).
- **`VMod` is a real car**: `CMKIB01` 102 = Cobra_427 (class 15), `CMKIB09` 231 = 300C SRT8, `CMFIB02` 71 = Superlight R500, `EVHA3SH1` 275 = Alfa Brera, `EVIA1CE1` 654 = Zonda Tricolore, `EVIA2CE2` 657 = Q7 Inuit (off-road), the DLC `RMIDLCM1/4` 1513 = Desmosedici (a bike). `-1` = the player's own car.
- **The DLC "Exploration pack" files** (`RMHDLCM1..8`, `RMIDLCM1..7`, `AMI1DLC1`, `AMZ2DLC1`) are `gmth 3` race-style files with 5-12 gates and are **not in `spotworld.bin`** (no map position); the "Timed Convoy" / "Extreme Convoy" intros are `pack_events14/15` (the `a-phone` prop, `ev_convoy_phone*` anims).
- **The story A-to-B prompts** ("Drop me at the driving school" = Jane, done; "Drive it to my garage" 3856, "Drive me home" 3857, "Do not shake me!" 3858, "Stop him!" 3859) have **no ig2 / DBEvent source**: they sit in the encrypted story / mission code (`GM_Mission` in the dev exe is the lead).
