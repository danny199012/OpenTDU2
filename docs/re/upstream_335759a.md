# CamilleKMI/OpenTDU2 `335759a` — the "MAJOR Update": what it is, why we did not merge it, and how to port it

The owner saw that `CamilleKMI/OpenTDU2` (the project this repository is a
descendant of) published a new commit and asked to merge it, naming three parts
as the interesting ones: **the used car dealer UI**, **Driving Tess**, and
**possibly the Solar Crown cutscene**. This file is the answer: what the commit
contains, what a merge would actually cost (measured, not guessed), and the
staged plan that lands the three features without regressing the 269 commits
this tree is ahead by.

## 1. The commit

| | |
|---|---|
| Repository | `CamilleKMI/OpenTDU2` (read-only upstream; `git fetch https://github.com/CamilleKMI/OpenTDU2 main:refs/remotes/upstream/main`) |
| Commit | `335759a21f77684e4353759b0dc5d72dc4cfedcf` — "MAJOR Update (lights, night, FX weather, clouds, ocean far fix, car options turn signals + roof, LOD trees)" |
| Date | 2026-10-29 19:51:50 +0100 |
| Size | **129 files, +16 811 / −410** |
| Our fork point | `3f67e58` ("tess lips moving + fix windows california + world viewer mode") — the merge base of `HEAD` and `upstream/main` |

It is one squashed commit, so the message lists the *rendering* work but not the
gameplay work it also contains. The new files are the real content:

**New game modes (the owner's picks)**
- `source/game/shop/gm_shop.{h,cpp}` — **GMShop, mode 3 SHOP**, 1 344 lines: the
  **used car dealer UI** (story step 7, Todd Bishop at the spot `ESTOL_B_1275I`):
  the `SHOPSCAR.vmf` movie, the cursor, the mini menu (Buy / Enter / Data /
  Compare), the price and car-data panels, the in-car look, the test drive hand-off.
- `source/game/shop/gm_testcar.{h,cpp}` — **GMTestCar, mode 7 TESTCAR**, 483 lines:
  the dealer's test drive (the 120 s HUD countdown, a passenger, the spot's car park pose).
- `source/game/mission/{gm_mission,ige_file,cinematic_props,event_reactions}.{h,cpp}`
  — **GMMission, mode 4 MISSION**, 1 322 lines: **story step 4 "Driving Tess"**
  (the `TESSEVEN` event, the `ig2/tesseven.ig2` IGE2 script, Tess' passenger
  reactions, the truck prop).
- `source/game/cine/gm_cine.{h,cpp}` — **GMCine, mode 0x1f CINE**, 414 lines:
  **the story cutscenes at a spot** — kind 1 Meeting Tess (`C_MEETING_TESS`),
  **kind 2 / 3 the Solar Crown exterior / interior** (`C_SOLAR_CROWN_E` /
  `C_SOLAR_CROWN_I`).

**Supporting classes the modes need** — `source/game/avatar/seated_avatar.*`
+ `ik_offsets.*` (an avatar in a car seat with the drive IK), `source/world/road_network.*`
(road snapping, the event start pose), `source/game/freedrive/*` (a FREE_DRIVE
sandbox tool), `source/input/game_key_map.*`, and the rendering half:
`render/eden/eden_particles.*`, `eden_rain.*`, `eden_neons.*`,
`world/eden_world_lod.cpp`, the `headlight.frag` / `headlightblit.frag` shaders.

**Docs** — `docs/re/story_spec.md` (564 lines: the whole story step machine,
steps 3..13, the shop data), `docs/re/spot_exterior_spec.md`,
`docs/re/accessories_spec.md`, `docs/re/car_lights_spec.md`,
`docs/re/night_lighting_spec.md`, `docs/re/particles_spec.md`,
`docs/re/rain_spec.md`, `docs/re/weather_spec.md`, `docs/re/neon_spec.md`,
`docs/re/lod_impostor_spec.md`, `docs/free_drive.md`, and five screenshots
(`used_car_dealer.png`, `driving_tess.png`, `meeting_tess.png`,
`meeting_tess_car.png`, `solar_crown.png`).

## 2. Where the two trees actually stand

They are **not** an ancestor / descendant pair any more, they are two parallel
evolutions of `3f67e58`:

| | this tree (`cline`) | upstream `335759a` |
|---|---|---|
| Commits since `3f67e58` | **269** | 1 |
| Lines the other side does not have | **120 308** | 16 811 |
| `source/game/` subdirectories | 11 (freeride, cinematic, casino, assetviewer, spots, …) | adds shop, cine, mission, freedrive |

And they have been implementing **the same features independently**: this tree's
`docs/STATE.md` already records animated windows, driven indicators, headlights
at night, brake / reverse lamps and the dashboard backlight
(`CinematicCar::updateWindows`, the in-car menu entries) — which is exactly what
`335759a`'s "car options turn signals + roof" and "lights, night" describe.
Both trees also wrote a `fade_freeride`, a `cinematic_car` extension and an
`intro_cinematic` shop / display-car layer. That is why the conflicts below are
mostly *add/add*, not clean merges.

## 3. The merge, measured

A trial merge (`git merge --no-commit --no-ff upstream/main`, then aborted) gave:

- **31 unmerged files, 103 conflict hunks** (~2 400 lines of our side, ~1 000 of
  theirs). The tree was restored with `git merge --abort`; `cline` was never touched.
- The shape is consistent: **our file is usually the superset and upstream added
  a small block next to it** (e.g. `freeride_hud.cpp`: 533 of our lines vs 90 of
  theirs; `intro_cinematic.cpp`: 423 vs 85; `player_profile.cpp`: 394 vs 72). The
  handful of genuine "both rewrote it" files are `intro_cinematic.cpp`,
  `cinematic_car.cpp`, `fade_freeride.*`, `eden_scene_view.cpp`, `eden_world.*`,
  `flash_player.cpp` (15 hunks) and `tdu_instance.cpp`.

| Conflicted file | Hunks | our lines | their lines |
|---|---|---|---|
| `source/game/freeride/freeride_hud.cpp` | 4 | 533 | 90 |
| `source/game/avatarchoice/intro_cinematic.cpp` | 10 | 423 | 85 |
| `source/game/player_profile.cpp` | 2 | 394 | 72 |
| `source/game/freeride/fade_freeride.cpp` | 9 | 208 | 57 |
| `source/game/player_profile.h` | 3 | 83 | 91 |
| `source/game/cinematic/cinematic_car.h` | 2 | 73 | 52 |
| `source/game/freeride/gm_freeride.h` | 5 | 108 | 12 |
| `source/flash/flash_player.cpp` | 15 | 64 | 52 |
| `source/game/avatarchoice/intro_cinematic.h` | 6 | 47 | 67 |
| `source/game/freeride/fade_freeride.h` | 2 | 75 | 33 |
| `source/game/worldviewer/gm_world_viewer.cpp` | 3 | 21 | 80 |
| `source/render/eden/eden_scene_view.cpp` | 4 | 77 | 9 |
| `source/tdu_instance.cpp` | 4 | 36 | 47 |
| `source/game/freeride/gm_freeride.cpp` | 5 | 50 | 31 |
| `source/world/eden_world.h` | 2 | 8 | 52 |
| `source/game/freeride/freeride_hud.h` | 2 | 9 | 32 |
| `source/world/eden_world.cpp` | 1 | 8 | 29 |
| `source/flash/flash_player.h` | 5 | 25 | 11 |
| `source/game/firstwalkmode/gm_first_walk_mode.cpp` | 3 | 26 | 10 |
| `source/render/eden/eden_scene_view.h` | 1 | 19 | 17 |
| `source/game/cinematic/cinematic_car.cpp` | 2 | 18 | 17 |
| `source/render/eden/eden_renderer.cpp` | 1 | 3 | 25 |
| `source/game/gm_base.h` | 1 | 15 | 13 |
| `source/game/spots/spot_exterior.cpp` / `.h` | 1 / 1 | 9 / 3 | 8 / 10 |
| `source/game/freeride/chase_camera.cpp` | 1 | 4 | 13 |
| `source/game/gm_base.cpp` | 1 | 8 | 7 |
| `source/game/cinematic/cinematic_scene.cpp` | 1 | 3 | 4 |
| `source/game/worldviewer/gm_world_viewer.h` | 1 | 2 | 2 |
| `README.md` | 4 | 33 | 34 |
| `docs/re/text_icons_spec.md` | 1 | 32 | 1 |

`source/game/spots/spot_exterior.*` is a **rename/modify** conflict: this tree
moved the file from `worldviewer/` to `spots/`, upstream modified it in place.

## 4. Why a wholesale merge is the wrong move

1. **It would drag in a parallel rendering rewrite** (weather / rain / particles /
   neons / LOD impostors / ocean / world streaming / `flash_player` / `gs_audio`)
   that this tree has already solved its own way. Resolving those "ours-first"
   silently drops upstream's work; resolving them "theirs-first" risks the 269
   commits of work the owner has already tested.
2. **It would not even compile.** The new modes call ~25 spine methods that do
   not exist in this tree (table in §5). A merge resolves *text* conflicts, not
   that gap — the same hand-porting work is needed either way, but on a tree that
   is now also carrying a half-merged renderer.
3. The features the owner wants are **new files**, which do not need a merge at
   all: `git checkout upstream/main -- <path>` brings them in exactly.

## 5. The spine gap (measured)

Each symbol below was searched for across `source/**/*.{h,cpp}`. **MISS** means
this tree does not have it and the named mode will not compile until it does.

| Spine API upstream's modes call | In this tree? | Needed by |
|---|---|---|
| `IntroCinematic::takeActorMessages()` / `ActorMessage` | **MISS** | CINE, SHOP, MISSION |
| `IntroCinematic::liftOntoFloor()` / `placeEntityAt()` | **MISS** (we have `Setup::bSceneAt` + `SceneAtPosition/Heading`, `getSurfaceHeight`) | CINE |
| `IntroCinematic::getGroundHeight()` | **MISS** (we have `getSurfaceHeight(x,z,maxY,h)`) | CINE |
| `Setup::bExteriorCollision` / `Setup::InteriorBank` | **MISS** | CINE kind 1 / 3 |
| `Setup::UiMovie` (`"shopscar"`) | **MISS** (we hard-code `intro`) | SHOP |
| `loadCar(desc, rest, cinCar, carActor)` — 4th arg | **MISS** (we have the 3-arg form) | CINE |
| `addShowCar` / `getShowCar` / `getShowCarWorld` / `setShowCarVisible` | **MISS** (we have `addDisplayCar` / `focusDisplayCar` / `getShopPosCams`) | SHOP |
| `addSelectable()` | **MISS** | SHOP |
| `setDialogBoxVisible()` / `setGeneralCommandCallback()` / `getGeneral()` | **MISS** (we have the `general` player, no accessor / callback) | SHOP |
| `getSceneBank()` / `getSceneHashcodes()` / `getCinProps()` | **MISS** (`getSceneOrigin` exists) | SHOP |
| `setSeatedShowCar()` / `getSeatedShowCar()` | **MISS** | SHOP |
| `IntroCinematic::SpotRecord.Position2 / Heading2` + `ReadSpot()` | **MISS** (our `SpotRecord` has only `Position`/`Heading`; `LoadSpotList` exists) | CINE, MISSION |
| `PlayerProfile::CineRequest` / `GetCineRequest()` | landed (§8) | CINE |
| `GetStoryAvatarConfig()` | landed (§8) | CINE |
| `ProfileAddMoney` / `ProfileStartMoneyTransfer` / `ProfileTickMoney` / `PendingMoney` / `MoneyTransferTimer` / `MoneyEarned` | landed (§8) | MISSION, SHOP |
| `PlayerProfile::OwnedCar` / `Garage` / `CurrentGarageSlot` / `LoanCar` | **MISS** | SHOP, TESTCAR |
| `TestDriveRequest` / `GetTestDriveRequest()` | **MISS** | SHOP, TESTCAR |
| `VehicleDynamics::takeImpactSpeed()` | **MISS** | MISSION |
| `FreeRideHud::setCountdown()` / `setTestDrive()` / `setTarget()` | **MISS** | TESTCAR, MISSION |
| `CinematicCar::applyColor()` | **MISS** | TESTCAR |
| `WalkMode::setCursorLabels()` / `setSelectionCallback()` | **MISS** | SHOP |
| `EdenSceneView::getEnvironmentCube()` / `setEnvironmentCenter()` / `getSceneParams()` / `setDrawCallback()` | present (used by `title_scene` / `intro_cinematic`) | MISSION, TESTCAR |
| `EdenWeather` / `WeatherLibrary` / `WeatherPreset` | present (`gm_world_viewer` weather panel) | MISSION, TESTCAR |
| `setPlayerSeated`, `getDoorAngle`/`setDoorAngle`, `showGoal`, `takeEndEvents`, `loadCar(3)`, `getShopPosCams`, `addDisplayCar`, `focusDisplayCar`, `general` (GENERAL.vmf) | present | — |

So the port is **~25 small API additions inside our divergent files** (most are
10–50 lines each; upstream's whole `intro_cinematic.cpp` delta is 369 lines),
plus the new mode files adapted to those APIs.

## 6. The plan (stages, each one builds)

CMake needs no edit: `CMakeLists.txt` uses
`file( GLOB_RECURSE OTDU_ENGINE_SRC CONFIGURE_DEPENDS ... )`, so a new `.cpp`
under `source/` is picked up on the next build.

**Stage 0 — the story spine. DONE (`1b187bc`, branch `upstream-335759a-port`).**
`eGameMode` gained `GM_Cine` / `GM_Mission` / `GM_Shop` / `GM_FreeDrive`;
`GetCineRequest()`, `GetStoryAvatarConfig()`, the money transfer
(`ProfileAddMoney` / `ProfileStartMoneyTransfer` / `ProfileTickMoney`,
`PendingMoney`, `MoneyTransferTimer`, `MoneyEarned[28]`), the previous-game-mode
field (`getPreviousGameMode()`, DAT_00fd5080 + 0x12c) and `-storystep n`.

**Stage 1 — the Solar Crown cutscene (GMCine).** The smallest of the four
(414 lines) and the spine of the chain: steps 3 / 5 / 6 are CINE and step 7
(SHOP) is reached *from* CINE kind 3.
- `git checkout upstream/main -- source/game/cine` and add
  `case eGameMode::GM_Cine: return std::make_unique<GMCine>();` to
  `TestDriveGameInstance::createGameMode`.
- Port the `IntroCinematic` additions it uses: `ActorMessage` /
  `collectActorMessages` / `takeActorMessages` (from upstream's `advance()`),
  `Setup::bExteriorCollision`, `Setup::InteriorBank`, `getGroundHeight`,
  `getGeneral()`, the 4th `loadCar` argument.
- Reuse our own mechanisms where they already exist: `Setup::bSceneAt` +
  `SceneAtPosition`/`SceneAtHeading` replaces `placeEntityAt` for kind 1
  (upstream's `liftOntoFloor` can be skipped), and `getSurfaceHeight` can back
  `getGroundHeight`.
- Extend `SpotRecord` with `Position2` / `Heading2` (spots.bin record +0x28 /
  +0x38) in `source/game/spots/spot_exteriors.cpp` and expose a
  `ReadSpot(name, out)` lookup.
- Verify: `-gm CINE -cinekind 2` (C_SOLAR_CROWN_E at `ECH_IA_1243I`) and
  `-cinekind 3` (interior bank `interior/ich_ib.bnk`), `-cinekind 1` (Meeting
  Tess), `-storystep 5`.

**Stage 2 — the used car dealer UI (GMShop) + its test drive (GMTestCar).**
- `git checkout upstream/main -- source/game/shop` plus the `IntroCinematic`
  showroom additions (`addShowCar` / `addSelectable` / `setSeatedShowCar` /
  `getSceneBank` / `getSceneHashcodes` / `getCinProps` / `Setup::UiMovie` /
  the GENERAL dialog-box callback) and `WalkMode::setCursorLabels` /
  `setSelectionCallback`.
- Profile: `OwnedCar` / `Garage` / `CurrentGarageSlot` / `LoanCar`,
  `TestDriveRequest` / `GetTestDriveRequest()`.
- Our `dealer_menu.cpp` / `spot_interiors_spec.md` already read `DBCarShop` and
  `Entity__ShopData`, and our `IntroCinematic` already has `getShopPosCams()` /
  `addDisplayCar()` / `focusDisplayCar()` — the SHOP movie layer is what is new.
- Verify: `-storystep 7` / `-gm SHOP` at `ESTOL_B_1275I`, then `-gm TESTCAR`.

**Stage 3 — Driving Tess (GMMission + IGE2).** The largest: the new files
(`gm_mission`, `ige_file`, `cinematic_props`, `event_reactions`,
`avatar/seated_avatar`, `avatar/ik_offsets`, `world/road_network`) plus
`VehicleDynamics::takeImpactSpeed`, `FreeRideHud::setTarget` and the HUD
countdown / test-drive elements, and `CinematicCar::applyColor`.
- Verify: `-storystep 4` / `-gm MISSION` (event `TESSEVEN`), then the story
  chain 3 → 4 → 5 → 6 → 7 in one run.

**Not in scope (recommended to leave alone):** upstream's render / weather /
world / input work (`eden_particles`, `eden_rain`, `eden_neons`,
`eden_world_lod`, the free-drive tool, `game_key_map`, the `flash_player`
changes) — this tree has its own, and pulling it in is what makes a merge
expensive. If a mode later turns out to *require* one of them, port that one
file at that point, on its own.

## 7. How to fetch the upstream tree for reference

```powershell
cd C:\OpenTDU2-cline
git fetch https://github.com/CamilleKMI/OpenTDU2 main:refs/remotes/upstream/main
git --no-pager diff --stat 3f67e58 upstream/main          # what the commit changes
git --no-pager diff 3f67e58 upstream/main -- <file>       # one file
git checkout upstream/main -- <new file or directory>     # bring a new file in
```

Never merge `upstream/main` into `cline` wholesale; cherry-pick by path.

## 8. What has landed in this tree

`1b187bc` (branch `upstream-335759a-port`, Stage 0): the story spine —
`eGameMode` modes, `GetCineRequest()`, `GetStoryAvatarConfig()`, the money
transfer, `getPreviousGameMode()` and `-storystep n`. No behaviour change to the
existing modes (the four new modes are not registered yet, so `-gm CINE` still
warns). Verified: `build-cline.ps1` exit 0; `-gm cine` logs `GAME MODE : CINE`
and then the pre-existing "not implemented" fallback; `-gm FREERIDE -skipcine`
still reaches `GAME MODE : FREERIDE` (exit 0); `-storystep 5` selects CINE
(kind 2).

