# Skills, points, levels and the progress popups

Tags as in CLAUDE.md. Code: `source/game/skill_levels.{h,cpp}`, `pause_menu.cpp` (pages), `freeride_hud.cpp` (popups).

## Database `[DATA]`
- **Level tables**: `DBRacingLevel._rlpneeded`, `DBCruisingLevel._crlpneeded`, `DBCollectionLevel._colpneeded`,
  `DBSocialLevel._slpneeded` - 16 rows each (levels 0-15; 0, 125, 300, 525, 825, 1200, 1650, 2175, 2795, 3510, 4320,
  5225, 6225, 7355, 8615, 10000); `_grade_hc` the localized title (changes at levels 3 / 7 / 12), `_rewardname_hc` /
  `_rewarddesc_hc` the reward of the title (`§PERC§` = the row's `_percent`).
- **Global**: `DBPlayerLevel` (levels 0-56, title only, a new one every 10) - the Global level is the sum of the four.
- **`DBAreaLevel`**: 18 rows (4 areas, levels 1-5): the exploration level of an area; Discovery's 14500 points / 18 levels
  on the owner's pages include it `[INF]`; not wired.
- **`DBRewardXP`** (191 rows): the points per event - `_event` (name), `_name_hc` (label), `_racingpoints`,
  `_cruisingpoints`, `_collectionpoints`, `_socialpoints`, `_nbtimesallowed`. Families: `RACING_CHAMP_<GOLD|SILVER|BRONZE>_<class>`
  (25-190), `RACING_LICENCE_*` (100-200), `RACING_DUEL_*`, `RACING_CUP_*`, `RACING_INSTCHALL` / `MULTICHALL` (20, x50),
  `CRUISING_<ISLAND>_ROADS_AREA<n>_<10|25|50|75|100>` (40 / 50 / 90 / 120 / 150 per area), `CRUISING_*_EVENT_AREA*` (25-40),
  `CRUISING_WRECK_PART_*`, `CRUISING_PHOTOS_*` (40-50), `COLLECTION_CARS_DIF_<n>` (75 at 1 car ... 1350 at 50),
  `COLLECTION_HOUSES_<n>`, `_FURNITURES_`, `_CLOTHES_`, `_HAIRCUTS_`, `_STICKERS_`, `_SURGERY`, `SOCIAL_FRIENDS`,
  `_CLUBMEMBER` (100), `_CLUB_LVL2/3` (200 / 500), `_CHASEMODE_*`, `_COOP_*`, `_GAMBLING_HALL_*`; plus `TUTO_*` rows that are only
  goal texts ("Bank the first level with the F.R.I.M.", "Enjoy!", ...). The roads are therefore **milestones per area**, not
  points per metre.

## What the port does
- `AwardSkillEvent( name )`: adds the row's points (counted in `PlayerProfile::EventsDone`, saved as `done_<EVENT> = n`,
  limited by `_nbtimesallowed`) and calls the HUD (earn popup). Wired: the road milestones (`MapScreen`: per cruising-zone
  polygon, area totals Ibiza 355 / 552 km; milestones already reached when loading are not paid again). Everything else
  waits for the feature that triggers it (races, shops, house / car purchase, club, chase mode ...). `-awardevent <NAME>`
  pays one for a test.
- Pause pages: levels, titles, next level / next title from the tables (`ComputeSkill`).

## Which DBRewardXP events the port pays (2026-11-15) and which are still dark
`AwardCollectionMilestones( family, count )` (`skill_levels.cpp`) pays every `<family>_<n>` row with n <= count; each row is once-only, so it is called after every purchase.
- **Paid**: `CRUISING_<ISLAND>_ROADS_AREA<n>_<pct>` (map roads), `CRUISING_WRECK_PART_*` (wrecks), `COLLECTION_CARS_DIF_<n>` (dealer + shop, `NbCars`), `COLLECTION_HOUSES_<n>` (estate menu, `NbHouses`),
  `COLLECTION_CLOTHES_<n>` (clothes shop, owned clothes), `COLLECTION_STICKERS_<n>` (sticker shop pay; the count is the **packs** owned [INF - the row says "Stickers bought"]),
  `COLLECTION_FURNITURES_<n>` (MyTDUHouse furniture; the count is the distinct furniture models placed [INF]), `RACING_LICENCE_<classes>` (a licence's last test, `A7 / A6` -> `A7A6`),
  `RACING_CUP_<IBIZA|HAWAI><order>` (a cup championship completed [INF: order]).
- **Dark (no trigger exists yet)**: `RACING_CHAMP_*` now paid on championship completion (main 61; rank always 1st), `RACING_DUEL_*`, `RACING_INSTCHALL` / `MULTICHALL`, `CRUISING_PHOTOS_*` (the 2 "panorama nodes" of photos.bnk are a scene root and a plane at the origin, NOT viewpoints; the 20 viewpoints are unlocated - likely the DBPhotograph photo spots, see main 63),
  `COLLECTION_HAIRCUTS_*` / `COLLECTION_SURGERY` (no hairdresser / surgeon flow), `SOCIAL_*` (club, friends, chase mode, co-op, gambling hall), `CRUISING_*_EVENT_AREA*`, the `TUTO_*` rows (goal texts).
- `DBAreaLevel` (area exploration levels, `NewAreaTitle`) is still not wired.
- Tests: `-awardevent <NAME>`, `-awardmilestone <FAMILY>,<n>` (`COLLECTION_CLOTHES,12` pays 5 and 10), `-logskills` lists the 191 rows. Licence: a profile with the first five C4 tests in `tests_passed=` plus
  `-licensetest RSC4T06 -licenseforce pass` logs `Skills: event 'RACING_LICENCE_C4' ... skill 0 +100` and saves `done_RACING_LICENCE_C4 = 1`.

## Popups (GENERAL.vmf `db_progress`, `_root.dbProgress( action )`) `[DATA]`
Root variables `WhatGrade`, `WhatDbProgress` (`earn_grade_points`, `earn_cruising_points`, `grade_title`, `global_title`,
`chase`), `WhatArea` (`i1` / `h2`), `WhatTuto`, `IsLevelUp`, `NewGradeTitle`, `NewGlobalTitle`, `NewAreaTitle` (**must be 0**, not
undefined), `ProgressCasinoOn`, `<Skill>LevelCount|Title|Points` (Points = gauge ring percent), `GlobalLevel*`, the texts
`TitleDbProgress` + `NbPointsWon` (header: the event name and "+N Pts"), `m_NextLevel_` (`§POINTS§`), `m_NewTitleGrade_`
(`§GRADE§`), `NewTitle`, `WhatReward` (`<grade>_<tier 1..3>`, Discovery `cruising`), `reward_name`, `db_reward_desc`,
`PlayerTitle`. Actions: `open_mini` -> `extend` -> (A: `sendKey( 13 )` pages: gauges, grade title with the reward, global title)
-> `close_extd`. The port queues the popups (`showEarn`, `showLevelUp`) and auto-advances with A every 3.5 s (or the
validate button). Not driven: `NewAreaTitle` (area levels), `chase_reward`, `open_tuto`.
