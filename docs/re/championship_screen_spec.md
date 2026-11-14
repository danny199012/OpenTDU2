# The championship screen (`championship_screen_spec`)

Which movie draws the championship screens of the owner's 360-recomp captures
(2026-11-14: "Championship: Sant Antoni" with the Information panel, and the
"Race: Sant Antoni downtown" pop-up with Enter / Close), and what its pieces are.
Tags as in `CLAUDE.md`: `[DATA]` game files, `[INF]` inferred, `[CODE]` readable exe.

## 1. The movie: **`MAP.vmf`** (`Euro/Bnk/FrontEnd/HiRes/map.bnk`, the world map's movie) [DATA]

Not `CHALLENG.vmf` (the online challenge browser: `/:m_ChallMulti_`, club ranks,
`NbChampionshipsDone` counters only) and not `RESULTS.vmf`. The championship view is
a mode of the **world map screen**: same movie, same frame labels
(`-dumplabels`, 26 labels), the map behind it, the pins on it. The port already loads
this movie (`MapScreen`, `source/game/map_screen.cpp`) and today hides the pieces it
does not drive: `"/quick_match", "/club_match", "/championship", "/join_player",
"/cheatmenu", "/mini_menu", "/lure_chall", "/lure_zoed", ..."` (`map_screen.cpp:338`,
`:2607`). `/championship` is the clip below.

Evidence (strings of `map.vmf`, and the clip tree from `-gm FREERIDE -dumplabels
-dumpclips 6 -autoplay "200:press=map"`): the movie's own text variables carry the
screens' wording - `m_VehicleRestriction` "Vehicle restriction", `m_ChallDone_`
"Challenges done", `m_ChampRivalF/M` "Rival: #color:FFFFFF#§RIVAL§#nocolor#",
`m_GiveUpHelp` "Give up this championship", `m_GiveUpHelp_q` "Give up this
championship?", `m_LeaveChampionship`, `m_ResultsHelp` "Results of the current
championship", `m_MapChampHelp_`, `m_MapVehicleRestriction_` "Your vehicle doesn't match
the requirements", `m_Race`, `m_Reward_`, `m_Enter`, `m_Close_`. Every phrase on the
owner's two captures is in this movie.

## 2. The pieces

### 2.1 `/championship` - the Information panel (clip id 468, 93 frames, hidden by default, depth 1559, at (-91, 158)) [DATA]
- `/championship/pages` (id 464, 43 frames) holds `cont/p` (id 458, 20 frames): the left
  panel of capture 2.
  - text `/:ChampRival` - the rival line (`m_ChampRivalM/F` with `§RIVAL§` replaced
    by the rival's name, "Miami Harris" on Sant Antoni [DATA: the string
    `_Rival : Miami Harrys` is the authoring placeholder]).
  - text `/:m_VehicleRestriction` + a restriction icon (the `C4` badge) - the class badge.
  - `../../:reward` + `/:m_Reward_` ("Reward $50,000" - the **championship** money,
    `LicenseInfo::ChampMoney`).
  - `../../:challDone` + `_root.m_ChallDone_` - "Challenges done 0/6".
  - `picture` (id 408) with `lure_c` - the rival / intro picture slot (the engine binds a texture to
    the `lure_c` quad, like the map's logo `lure`) [INF].
  - `gp` (id 415, 162 frames) - a group / standing icon.
  - `r0..r7` (id 450, 14 frames each; `ic`, `pName`, `pPts`) - eight ranking rows
    (the "Results" tab: player names and points) [INF: the owner's capture shows the
    Information tab; `m_ResultsHelp` says this list is the results].
  - `txt_help` - the help text (`m_MapChampHelp_`).
- Header: the class diamond + "Championship: Sant Antoni" is the map movie's top-left
  title (the same strip the map's tab titles use) - the class diamond is the
  `C`-letter + number art; the hologram plates `Championship_<family>_0N` show the
  same glyph [DATA: pack_spots.bnk; class numbers 4 / 7 / 4 for the Ibiza series].
- Bottom bar in capture 2: "OK" (A), "Give up this championship" (Y), Camera (RS),
  Zoom (LT / RT), Back (B): the movie's own button bar with `m_GiveUp*`; give-up is
  gated by `/:LockGiveUp` [DATA: the variable exists; its meaning is the obvious one].

### 2.2 `/tool_tip` - the race pop-up (clip id 1040, 168 frames, depth 607) [DATA]
Pop-up of capture 1 / the bubble of capture 2:
- `maskInfos`, `infos` (id 997, 546 frames) with `menu` (`bt3`... the Enter / Close
  buttons), a `stats` row (`title` + `stats`, e.g. "21.9 yd - 76.6 yd" / the lap and
  distance rows), and `restriction` (id 615, 47 frames) with `car` (+ scrolling
  `car_name_scroll`, so a long list of allowed cars scrolls) - the "Vehicle
  restriction" row.
- Text variables: `/:Distance`, `/:NbLaps`, `/:GoldReward`, `/:GameRules`
  (the rules text, "Be the first to cross the finish line."), `/:item_name`
  (the title "Race: ..."), `rules_ic_0..8` (the rules icons), `event_rules`.
- The pop-up is driven by FSCommands `bmap:ttip:info|extd|mecl|meop|redu|real:chpg`
  (info / extended info / menu close / menu open / reduce / real: championship page)
  and `bmap:ok` [DATA: strings]; functions `SetInfos`, `setChallenge`, `setPlayerInfos`,
  `displayChampionship`, `OpenExtendedInfos` / `CloseExtendedInfos` [DATA: strings].
- Pins: `f_race`, `f_race_m` (single / multi race), `chall_solo`, `chall_1`, `chall_2`,
  `chall_locked`, `chall0..5_state/type` (up to six challenge pins per championship, matches
  "0/6"), `player_challpos` / `rival_challpos` / `rival_globalpos` (the player and
  rival markers along the championship), `f_challDone` / `f_challUndone`.

## 3. Gaps of the port against it (see the owner-facing list, 2026-11-14)
Our `ChampionshipMenu` (`source/game/freeride/championship_menu.cpp`, 166 lines) is a
native Renderer2D list; none of 2.1 / 2.2 is used. The data the movie wants that we
already have: `LicenseInfo` (name, class, `RaceMoney`, `ChampMoney`, challenge files, `Rival`
bot hashcode -> DBPNJ name), the race titles ("Race: ..." strings), `Ig2Challenge` (laps,
gates -> distance), `PassedTests` (done count). Not available: the loading-screen picture of each race, the
`lure_c` rival picture (the cutscene still), the per-race reward (only the flat race
money), the rules text per mode (only the title prefix).

## 4. Implemented 2026-11-14 (`MapScreen::openContest`, `GMFreeRide::openChampionshipScreen`) and what is left

Driving the championship start (or `-championship "<name part>"`) now opens the world map in the contest view
(`docs/images/championship_screen_map.webp`; `-oldchampmenu` = the native list):
- The retail entry, **disassembled from the movie's own AVM1** (`-dumpmapfuncs`, `-dumpclipfuncs`, `-dumpclabels <paths>`):
  `displayChampionship(true)` = `ContestName = SpotName`, `ContestType = WhatSpot`, `SetHelp('map_championship')` (the OK / Give up /
  Camera / Zoom / Back bar), `IsContest`, `championship.Open(0)`; `Open` reads `nb_chall_done` / `nb_chall` ("0/6"), `GoldReward`
  (the championship reward), `ContestType` (the diamond's `title.ic` frame) and `ContestGP` (`title.num_contest`, the class number: "4").
  The port sets those root variables (`MapScreen::applyContest`), hides `/presets` (the tab row) and calls it with `invokeWithNumbers(1)`
  (AVM1 `Equals2` takes 1 == true). The rival line uses the movie's own `m_ChampRivalM` template. The restriction row takes
  `RestrictionType = 'vehicle'` + `GroupRestriction = 'C4'`.
- The race bubble is `tool_tip.SetInfos()` with the root spot variables it reads: `WhatSpot` (`race` / `timeAttack` / `speedtrap` / `speed`),
  `SpotName` (the race title), `GoldReward`, `Distance`, `NbLaps`, `GameRules` (loc 197 race, 2212 time attack, 2244 speed trap [DATA]).
  Up / down (D-pad, arrows) steps through the races, OK enters the selected one (locked ones - the previous race not passed - refuse),
  Back leaves; the world map restores its tab row on the next open.
- Verified headless (build with the validation layer: 0 VUIDs): the screen with the C4 diamond, "Rival: Miami Harris", the restriction
  badge, "Reward $50,000", "Challenges done 0/6", the bubble "Time: Sant Antoni hill" (3.6 km), Down -> "Race: Gracio bay" (8.8 km, 2 laps),
  OK -> `LicenseTest: CHC4A01T 'Time: Sant Antoni hill'` and the race starts; Back, then the map key, shows the normal tab map again.

Update (same day): the **extended pop-up** works - the first OK calls `tool_tip.Extend(1)` (SetInfos already built the menu with `SetMenu('chall_solo')`), which gives the picture slot, Enter, Close, restriction, reward, distance and the Rules box (the rules text is `/:GMDescription`); the second OK enters, Back calls `Extend(0)`. The **race pins** are clones of `/pack/point` (`cpin<N>`, art = the kind's `pack/point/spot` label, the target ring on the selected one, the regular spots hidden); the view centres on the selected race.

Still left: ~~(a) the **extended pop-up**~~ (picture, rules, Enter / Close): `tool_tip.Extend(1)` + `SetMenu(Type)` build it; their arguments
(`Type`, the `setBt` button arrays) are not decoded - OK enters directly meanwhile; (b) the race **pins** on the map and the bubble
at the pin (the bubble sits at its default place; `player_challpos` / `rival_challpos` / `f_race` are the pin clips); (c) the rival
picture slot (`picture/lure_c`, hidden), the Results tab (`r0..r7`) and LB / RB page switching (`championship.Move`); (d) give up
(Y, `LockGiveUp`, `FSCommand:giup`) and the per-race loading-screen shots; (e) distance is the gate-to-gate polyline (the retail
number is the route) in the units option (km / mi); (f) cups use `ContestType 'cup'` with the five restriction badges
(`CupRestriction0..4`) - not exercised.
