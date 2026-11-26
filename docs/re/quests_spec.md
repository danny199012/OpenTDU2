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

## 6. The dev build's mission code, read (2026-11-26, `main`) [CODE-dev]

- **`GM_Mission::Refresh` (0xb33140) is the online mission lobby**, not story logic: its strings are `Starting Synchronisation`, `Synchronization success / failed!`, `Car Grid ready`, `Synchronization & Grid order status:`, per-player `Lobby / Playing / Spectator / Synching / Timeout`, `Ping [%4d ms]`, `MngCarGridPosition`. It is the multiplayer challenge mode (Keep your distance / Follow the leader / convoys): consistent with quests_spec 5. There is **no single-player A-to-B mission code to lift from it**.
- **The story quests are phone-call driven.** `EVENT_JANE` (the xref at 0x9a265b) sits in the `SCPhoneCalls` name table beside `NEW_ASPHALT_CAR`, `RS_ASPHALT`, `CLOTHES_CALL`, `HAIR_DRESSER`, `SURGERY_IBIZA`, `STICKERS`, `CAR_WASH`, `HAWAI_CALL`, `TUNER_CALL`, `HAWAI_DIRT`, `HAWAI_CLASSIC`, `*_CHAMP`, `PHOTOGRAPH_END_*` (a name -> handler chain, `phone_calls_spec.md`). The call is **Todd's** (`DBPhoneCall` 5, npc 0x187, `PC_07_Help_Jane`: "A friend of mine needs some help. If you could catch up to her, she'll explain everything"), second in the retail trigger order after `CALL_C4_NOT_PASSED` (the rule reads three state objects, meaning [INF]).
- **Quest chain, so**: Todd rings (EVENT_JANE) -> the goal "Join Jane who needs some help" (4038) -> the EVENJANE spot -> intro scene (Jane explains) -> goal "Drop me at the driving school" (6112) -> the Dirt school (ERS_B) -> win scene.

Built: `PhoneCalls::checkNudges` rings `EVENT_JANE` 2 minutes into the free drive at story step >= 10 until `PhoneState["JaneDone"]` is set [INF timing]; accepting it sets goal 4038 and the GPS to the start gate; winning sets `JaneDone`, so the call stops and the event does not restart across sessions. Verified: `-storystep 10 -phonecall EVENT_JANE` + validate: `ring 'EVENT_JANE'`, `accepted`, `GPS to the Jane event start (13069.8, 17091.0)`.
Not done: the other state-driven quests (`TUTO_RESCUE_JANE`, `RACING_LICENCE_A7A6`, `TREASURE_HUNT`), the retail rule's exact flags, the `GM_Mission` `[ENC]` retail twin.

## 7. The other call quests (2026-11-26, `main`)

`DBPhoneCall` has 33 calls; the port already rang 15 of them (`phone_calls.cpp` checkNudges). Now added:
- **`RIVAL_C4 / B4 / A5 / A2 / B3`** (the duel challenges, `DBEvent` type 11, caller = the rival, "I'll put my car at stake!"): `Context::OpenDuels` (the duels of the island that are open and not won; open = a championship of the class is complete, the spot's existing [INF] rule, now `GMFreeRide::isDuelOpen`) -> the call rings; accepting sets goal "Duel against <rival>" and the GPS to the duel's start (`DUELC4A` (11576, 15490) ...). Verified: `-phonecall RIVAL_C4` + validate -> `accepted -> GPS to the duel DUELC4A (11576, 15490)`.
- **The duel prize car** (`GMFreeRide::grantDuelCar`): winning pays `DBRewardCar` `DUEL_WIN_<class>`: `_carunlocked` + tuning 3/3/3 (C4 -> car 601, Miami's Mustang; loc 3852 "You win the challenger's car!"). Verified: `-duelnow C4 -licenseforce pass`: `duel prize 'DUEL_WIN_C4': car 601 added to the garage, tuning 3/3/3`, `cars_owned = 601`, `car_tuning ... 601:3,3,3`. `-duelnow <class>` starts a duel without walking to its spot. The duel's test car is the ig2 `VMod` (DUELC4A: car 102, the Cobra 427 [DATA]).
- Still not rung / played: `WRECKS_CALL` (the wreck reward car: `DBRewardCar WRECKS_*`), `CONVOY_WIN_CALL` / `CONVOY_LOOSE_CALL` (the DLC convoy's "thank you" / "incompetent" calls, npc 0x12a; the DLC strings are not in this install's loc), `CHASE_INTRO_IBIZA / HAWAI` (police chase offers), `HAWAI_CALL`, `SURGERY_HAWAI`. The duel cinematics (`C_DUEL_*`, `stb_duels` ...) are not played.

## 8. The wreck reward (2026-11-26, `main`) [DATA + INF]

- The wreck points (`islands/<island>/scenarics/wrecks/wrecks.bnk`, `Entity__Wreck.ID`) are **parts of wreck cars**: on Ibiza **ID 1 = `WreckB` + `_01.._09` (10 parts), ID 2 = `WreckB2` (10), ID 3 = `WreckB5` (8)** (28 points; the cruising "area" of a point is only where it lies, not the group). `DBRewardCar` has `WRECKS_IBIZA_1` (car 422 Beetle_1973), `WRECKS_IBIZA_2` (314 Buggy1) and `WRECKS_HAWAI_1..4` (0x1a5 / 0xd8 / 0x184 / 0x66): one per ID, **ID 3 has none** (the Exploration DLC's wreck: loc 7800, "8 on Ibiza"). Loc 2009 / 4875: *"You found all the wrecks! A new vehicle is waiting for you ... Jude (used car dealer)"*; the call `WRECKS_CALL` (Jude, npc 0x17f): *"I finished assembling those parts you found and your car's waiting for you at the warehouse"*.
- Built: when the last part of an ID is found, `GMFreeRide` grants `WRECKS_<ISLAND>_<ID>` through the new `grantRewardCar` (car into the garage + its tuning; the duel prize uses the same), shows the loc 2009 pop-up and rings `WRECKS_CALL` (accept -> GPS to the nearest `cardealer` [INF]). Once per car (`PhoneState["WRECKS_<ISLAND>_<ID>"]`).
- Verified: a profile with 9 of the 10 `WreckB` parts found, `-tp` onto the last (`WreckB_02`): `wreck 'WreckB_02' found`, `reward car 'WRECKS_IBIZA_1': car 422 added`, `ring 'WRECKS_CALL'`, `cars_owned = 422`.
- Not done: the car is in the garage at once (retail: it waits at Jude's / the dealer and the pop-up offers a GPS target; the pop-up's text says so), the HUD's wreck page (`/:Wreck%d_carName`, `_nbPartsFound` ...), the Hawai ids were not run.

## 9. The chase offers (2026-11-26, `main`) [DATA + INF]

- `CHASE_INTRO_IBIZA` / `CHASE_INTRO_HAWAI` (`DBPhoneCall` 17 / 18, npc 0x151 = the dispatcher, `PC_11/12_Chase_Intro_*`): *"A reckless driver has been spotted. Do you want to chase him?"* / *"A dangerous driver is being pursued by the police. Do you want to join the chase ..."* (`_yesnoanswer 0`, `_nbrepeats 1`, `_timer 0`). The chase itself is the **cop side of "Chase mode"**: loc 6606 *"Stop the outlaw within 2 minutes 30"*, loc 6613 *"When the timer appears, you have nearly caught the outlaw. Stay close to them. If the timer reach 0, you win the chase!"*; `DBRewardXP` `SOCIAL_CHASEMODE_COP` (play 20 times as a cop), `SOCIAL_CHASEMODE_WIN` (win 20 chases); `DBRewardCar` `COP_CAR` 0x19c, `OUTLAW_CAR` 0x25e (606). The existing `ChaseMode` is the other side (the police chase the player).
- Built: the call rings once, 12 minutes into the free drive [INF], with Yes / No (the text asks a question). Accepting starts `GMFreeRide::startOutlawChase`: goal 6606, a 150 s countdown, the outlaw (car 606) placed on a road node ~700 m away and driven along the road graph to a far node by `RaceOpponents::startTraffic` (26 m/s [INF]), the minimap target follows it. Within 50 m the catch timer (8 s [INF]) replaces the countdown; 0 = win (`SOCIAL_CHASEMODE_COP` + `SOCIAL_CHASEMODE_WIN` XP, "Outlaw stopped!"); the clock out = "The outlaw got away." (`SOCIAL_CHASEMODE_COP` only). Test hooks: `-outlawtime s`, `-outlawdist m`, `-outlawspeed m/s`, `-outlawrange m`.
- Verified headless: `-phonecall CHASE_INTRO_IBIZA` + validate -> `outlaw starts 714 m away ... 12 s` -> `outlaw chase lost`; with `-outlawdist 20 -outlawspeed 1 -outlawrange 100` -> `outlaw chase won with 142.0 s left`, `Skills: event 'SOCIAL_CHASEMODE_COP'` / `'SOCIAL_CHASEMODE_WIN'`.
- **Not verified / not done:** the outlaw car drawn on the road (the 3D check put the player off-road, the car was not in view); the outlaw does not flee from the player, traffic or crash, a money / rank reward (`COP_CAR` / `OUTLAW_CAR` unlock ranks, loc 6682 "four ranks ... for both outlaw and cop") and the Hawai variant were not built; all the numbers above are [INF].

## 10. The Hawai calls (2026-11-26, `main`) [DATA + INF]

- **`HAWAI_CALL`** (Tess, npc 0x194, `PC_05_Hawai`: *"We're leaving today! Meet us at the airport! As soon as you get there, we're off to Hawaii! Woohoo!"*): the retail rule is "not online, not tutorial, a progress counter >= 10, Hawaii not visited" [CODE-dev 0x9a2c30]. Built: on Ibiza, Hawaii never visited (`PhoneState["PlayTimeWhenArrivingOnHawai"]` absent, set by the first arrival), 20 minutes of play [INF counter / delay]. Accepting shows the goal 4039 "Drive to the airport to fly to Hawaii" and the GPS to the nearest airport (`EAIR_A`). The flight itself is the existing airport travel.
- **`SURGERY_HAWAI`** (Miami, npc 0x18f, `PC_25_Surgery_Hawai`: the cosmetics clinic on Hawaii): rings on Hawaii 5 minutes after the arrival while the Hawaii clinic was not visited (`PhoneState["BeenInSurgeryHawai"]`, now set instead of `BeenInSurgeryShop` when the surgery opens on Hawaii) [INF: delay]; accepting sets the GPS to the nearest `surgery` spot (`ESUR_A`).
- Verified: `-phonecall HAWAI_CALL` + validate (Ibiza): `accepted -> GPS to 'EAIR_A' (airport), 11172 m`; `-island hawai -phonecall SURGERY_HAWAI`: `accepted -> GPS to 'ESUR_A' (surgery)`. Not run: the natural rings (delays), the goal's visual, the flight after the call.
- With these, **every `DBPhoneCall` row but `CONVOY_WIN/LOOSE_CALL` (DLC convoys) has a trigger** in the port; `HAWAI_CALL` / `SURGERY_HAWAI` / `*_CHAMP` thresholds are [INF].

## 11. The duel cutscenes (2026-11-26, `main`) [DATA + INF]

- Banks: `cinematics/asphalt/sta_duels` (Stuart A2, Tess A5), `offroad/stb_duels` (Ryan B4, Chris B3), `classic/stc_duels` (Miami C4). Cinematic names (the entities' `__Name`, the viewer's): **`C_DUEL_<class>H_INTRO / _WIN / _LOOSE / _WIN_LOOP / _LOOSE_LOOP`** (the dev build's `C_DUEL_%sH_...` pattern); the PNJ actors carry their configs (`STO_Miami_Harrys`, `STO_Ryan_Tyler` ...). Intro 7-8 s (2 shots), win 4.4-9.5 s, lose 6.4-7.5 s.
- The scenes are gameplay-anchored and carry **two cars**: `PLAYER_CAR` (the player's, beside the origin, x +1.67) and **`DUEL_CAR`** (the rival's, drives in from z -7.4 to the grid in the intro, x -1.68); the rival walks up in the win scene (`Miami` at (-0.4, 0, -2.3)). New `IntroCinematic::loadRivalCar` / `computeRivalCarWorld` (a second scene car driven by a named `.sce` actor, drawn + env-bound like the main car), `CinViewerRequest::RivalCarId` / `RivalActor` (`-cinrivalcar <_id_car>`).
- Flow (`GMFreeRide::enterDuel` / `playDuelScene`): the duel spot / `-duelnow` -> the intro at the duel's grid (gate 0 of `DUEL*.ig2`) with the rival's DBPNJCar car of the class (`PickDriverCar`; C4: 601 Miami's Mustang) -> the race starts (`StoryHandoff::StartQuest "duel:<class>"`) -> win / lose -> reward -> `C_DUEL_<class>H_WIN` / `_LOOSE` where the car stands -> the drive. `-noduelcine` = the old straight start.
- Verified: `-duelnow C4 -licenseforce pass`: `duel scene 'C_DUEL_C4H_INTRO' (rival car 601)` -> `end after 7.7 s` -> `duel DUELC4A started` -> `Passed` -> `reward car 'DUEL_WIN_C4'` -> `C_DUEL_C4H_WIN` -> `end after 5.8 s`. The win scene by eye (viewer): Miami in red walking up beside his dark Mustang, `docs/images/duel_win_scene_c4.webp`.
- Not done / open: the intro's second shot (`c_duelmiami_intro_02`, 175 position keys, 1 heading key) renders **rolled ~90 degrees** (the road on its side) - a camera-orientation case the Jane scenes did not hit, not investigated; the rival's paint (`DBPNJCar _color_id`) is not applied; the other four duels' scenes were not viewed; `*_LOOP` scenes; the scene weather / time of day is the cinematic's own.

### 11.1 The rolled camera, fixed (2026-11-26) [DATA + INF]

`-logcamtrack` (new) prints a camera's heading track: `c_duelmiami_intro_01` is a normal key (-0.062, 0.764, -0.099, 0.634), |q| = 1.000; **`c_duelmiami_intro_02` is (0, 0.040, 0.746, -0.203), |q| = 0.774** - not a rotation. Read as one it is a ~210 degree roll about Z: the sideways road. The track is a **look-at**: the first dword is 0 (no actor index; the school cabin cameras carry 1..255) and the other three are the target offset (0.04, 0.75, -0.20) - a head height over the scene origin. `CinematicScene` now takes "first dword 0 and |q| not 1" as a look-at on the **scene origin** (`kLookAtOrigin`), `Cinematic::getCamera` aims at `origin + offset`. Verified by eye: the shot is upright (buildings above the road, `docs/images/duel_intro_camera_fixed.webp`) and now sits very close to the dark Mustang arriving on the grid. Open: whether the retail target is really the origin (it frames the car's flank close up; unconfirmed against the 360 video), and the other cameras with a non-unit heading key, if any, were not scanned.

### 11.2 Camera scan of every cinematic (2026-11-26) [DATA]

`otdu2_cine_tool camscan <game root>` (new) lists every camera whose heading track is not a plain unit quaternion. **680 cameras: 619 unit quaternions, 23 look-at-an-actor (index 1..255: the school cabin cameras, the dealer's `c_buy` / `v_paint` ...), 32 look-at-the-origin (first dword 0, |q| != 1: the new rule), 6 other.**
- The 32 are almost all the **duel scenes** (all five duels' intro / win / lose and their `*_LOOP` cameras: `sta_duels`, `stb_duels`, `stc_duels`) plus `sta_a5` `NOUVELLE_VOITURE` (`CAM_Plan_06_TEST`), the `sta_winloosepodium` / `stc_winloosepodium` lose scenes (`CAM_LAS_Plan_01/02`, `c_loose_miami_01/04`), `stb_b4` `B5_CAM_Plan_03` and the dealer's **bike** scenes (`car_dealer_M_BUY_BIKE` / `_PAINT_V_BIKE`: `c_buy`, `v_paint`). Before the fix all of them read garbage quaternions (rolled / spinning cameras).
- The offsets are **not** all head heights: animated ones run to (-113.6, 0.9, 2.1) over 614 keys, (60, -13, -72) ... i.e. **a target point in scene space (distant for the panning shots)**, which is what the fix aims at (`origin + offset`); the duel intro's (0.04, 0.75, -0.20) is the near case. Alternative reading, not excluded: a direction from the camera (then that near case would look straight up - it does not look right).
- The 6 "other": `car_dealer` `v_interior` (|q| 0.979, five shots) and `stb_duels` `CAM_DuelIntroChris_Plan_02` (|q| 1.030): slightly off-unit real rotations, left as they were.
- Not verified by eye beyond the C4 intro (upright and near the car): the A5 intro's cameras (the first frame shows the horizon, the later ones a black close-up - a camera inside / against the rival's car body is possible), the dealer's bike scenes, the podium lose scenes. The retail footage for comparison exists only for the intro story and the licence school (`reference/`).
