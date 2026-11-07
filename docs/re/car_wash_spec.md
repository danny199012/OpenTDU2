# The car wash (`ECW_A`) — retail geometry, the washing cutscene, and the wiring plan

Status: **wired** (2026-10-31, `glm`, session 58 — the 7-step plan of §4 executed and
verified; the scene's in-car camera is a separate, parked bug, see §8). Session 25
(`cline`) investigated and left the wiring as WIP on the owner's instruction. Tags as in
CLAUDE.md: `[DATA]` read from the game files, `[CODE]` from the readable exe / the port,
`[INF]` chosen or inferred, `[ENC]` in the SecuROM band.

## 0. The owner's question — "is this a custom petrol station or was this in game?"

> *"I need you to look into the map for me. Is this a custom petrol station or was this in
> game… that area has a carwash next to it and it hangs over the road a bit. I don't think
> car wash was meant to be there, which makes me doubt this petrol station."*

**It is all retail, and the "petrol station" *is* the car wash.** The findings:

1. **The car wash is a fuel-station-shaped building**: canopy over the bays, a shop with a
   red sign behind, a price-totem on a pole at the kerb, and the wash tunnel in front. That
   is `islands/<island>/exterior/ecw_a.bnk` (`Euro/Spots/Carwash/CW_A/…`) — 1 Scenery set
   model + 4 props, local bounds **(-19.3, -0.2, -21.9) – (23.1, 11.1, 33.9)** (42 x 56 m),
   **3 placements on Ibiza / 5 on Hawai** (spots.bin `ECW_A`). The Ibiza forecourt at
   (8129, 26222): `docs/images/carwash-forecourt-s25.png`.
2. **There are no fuel pumps in it** — the port's own asset index (153,690 assets, 9,452
   banks) has no `pump` / `pompe` / `gas` asset in `ecw_a.bnk`; the game's only gas-pump
   model, `gaspump_d.2db`, exists in **one** bank in the whole game, the player-house garage
   `interior/gph_l2d.bnk` (fuel_gas_station_spec.md §4.1). What reads as a station is the
   car wash's own forecourt, not a station.
3. **Our custom station is somewhere else entirely**: the mods-overlay OBJ prop `FUEL1`
   (`<mods>/props.txt`: `ibiza FUEL1 11660 5.3 15866 0 fuel`) and the spots.bin route copy
   `EGAS_A` at (12665, 16698). Neither is within 6 km of an `ECW_A` spot.
4. **The canopy over the road is retail's placement, not a mod's**: `SpotExteriors` draws a
   bank at `T(spot.Position) R_y(spot.Heading)` straight from spots.bin, and the record's
   position is the building's *origin* — the three Ibiza car washes sit at (8128.7, 26222.3)
   hdg 0.22, (19122.7, 18162.4) hdg 2.70, (27062.5, 11642.2) hdg -1.50, and the front canopy
   reaches over the pavement/road at all three (spot_exteriors_spec.md §2). Nothing to fix:
   it is what the shipped data places.

## 1. What the two car-wash banks hold [DATA]

`islands/ibiza/exterior/ecw_a.bnk` = `islands/hawai/exterior/ecw_a.bnk` (identical file,
5,853,184 B; **144 entries**):

| group | what |
|---|---|
| `ecw_ai_sc` set | `.3dd` 1,104 B + `.3dg` **821,424 B**, `entities.xmb` 14,832 B, `hashcodes.xmb`, `.2dm` 42,336 B — the forecourt, canopy, shop and tunnel mouth |
| props | `cf_spong` (sponge), `cf_hose` (hose), `cf_radio` (boombox) `.3dd/.3dg/.2dm`, plus two `vfx/spots/vfx_cinematics_*.2dm` |
| textures | 71 `.2db`: `dirt_floor_d`, `wallc1_d`, `concrete{a,b,d}_*`, `aluminium_*`, `neon_d`, `lightandcontrols_*`, `cubemap`, `t_waterdrop_01_da` (the droplets) … |
| animation | **18 `.anm`** under `Euro/anims/carwash/cw_xan/`: `cw_loop01tuyau` (hose), `cw_loop01sponge` / `cw_loop02sponge`, `cw_walksponge`, `cw_end_sponge01/02`, `cw_end_bucket`, `cw_radio` (the three "babes" NPC loops), `cw_intro-m/-w` (avatar) and the `props/` clips carrying the sponge / radio / hose for each |
| sound | 3 `.wav`: `m_washing_intro_CINEMATIC`, `m_washing_CINEMATIC`, `m_washing_outro_CINEMATIC` |
| cinematics | **10 `.cin` + 10 `.sce`** (§2) |
| collision | `ecw_ai_sc_57E5…shk` 45,600 B + `.dhk` 38,016 B |

`interior/icw_a.bnk` (546,816 B, **26 entries**) is the **wash tunnel itself**: `icw_a_sc`
`.3dg` 89,824 B + `.shk` 91,696 B and the machine textures `machinery_{d,g,n}`,
`metal_pipes_*`, `metal_blue_*`, `wire_{d,g,n}`, `meter_{d,g,n}` and **`rouleau`** (= roller,
5,536 B), with the lightmap `lm_icw_a`. It is the interior bank the spot-interior rule
predicts (`spot_interiors_spec.md` §1: `ECW_A` -> `icw_a`), but it holds **no**
`Entity__FirstPerson`, no `Entity__Cinematic` and no walk start — it is a set, not a shop
room.


## 2. The washing cutscene [DATA] — `otdu2_cine_tool dump …/ecw_a.bnk`

| timeline (`.cin`) | entity name (what the code looks up) | length | shots |
|---|---|---|---|
| `ecw_ai_sc_M_INTRO_E_1` | `C_INTRO_E_1` | 6.55 s | 3 |
| `ecw_ai_sc_M_INTRO_E_2` | `C_INTRO_E_2` | 3.15 s | 1 |
| **`ecw_ai_sc_M_WASHING`** | **`C_WASHING`** | **16.4 s** | **3** |
| `ecw_ai_sc_M_LOOP` | `C_LOOP` | 12.02 s | 3 |
| `ecw_ai_sc_M_OUTRO` | `C_OUTRO` | 6.84 s | 2 |
| `ecw_ai_sc_M_OUTRO_FERRARI` | `C_OUTRO_FERRARI` | 7.7 s | 2 |
| `ecw_ai_sc_M_INCAR` | `C_INCAR` | 20 s | 1 |
| `ecw_ai_sc_M_BABE_01/02/03_totem_Babe_01/02/03` | `C_BABE_01/02/03` | 20 s each | 1 |

`C_WASHING` = scene `M_WASHING_washing.sce`, 16.4 s, **3 cameras** (`c_washing_01`,
`c_washing_02` — the in-car shot, `c_washing_03` — the 185-key orbit) and **7 actors**:
`PLAYER_CAR` at (1.47, 0.13, -17.51), `Avatar`, `Babe_01_radio`, `Babe_02_hose`,
`Babe_03_sponge`, and the `CF_Eponge_02` / `CF_Radio_01` prop actors; one FX track (water,
world (2.13, -16.32, -5.33)); messages `zcde` / `zcvn` (scene-local zoom, unused by the
port). Scene origin (0, 0, 0) heading 0 — i.e. **the spot frame**, so the scene plays inside
the real forecourt, which is why the bank carries the building as a set.

Retail's own words for the feature are in the localization: **3913** *"Is your vehicle
dirty? You'll be able to get it washed here and really make it shine!"* and **3942** Todd's
tip *"…a good carwash in the area. They'll get your car looking good as new!"* There is
**no** pump-style screen for it (unlike fuel 1635..1645) `[DATA: -dumploc wash]`.

The retail save carries the state: `CarDirtData` (size 9) = 8 `Dirt Data` bytes +
**+8 `CarWashCoating`**, and `SCPhoneCallsSave` **+4 `BeenInCarWash`**
(`docs/re/savegame_schema_360.txt`). Dirt is **not** rendered by this port yet (no dirt
shader, no per-car dirt), so a wash today can only be the scene + the fee + the flag.

## 3. Verified: the port **already plays** the scene [CODE]

`GMCinViewer` + `IntroCinematic` are fully generic (spot frame, exterior bank, sets, PNJs,
props, scene car, FX, camera), so the retail washing cutscene runs **today** with no code
change:

```
build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 ^
    -userdata C:\temp\otdu2-cline-userdata -gm CINVIEWER ^
    -cinbank islands/ibiza/exterior/ecw_a.bnk -cin C_WASHING ^
    -cinspot 8129,26222 -cinexterior ecw_a -logcin -logentities
```

Log: `ENTITY cinematic 'C_WASHING' …`, `CINVIEWER: playing 'C_WASHING' of
'islands/ibiza/exterior/ecw_a.bnk'`, then `CINVIEWER: t 1.0 main 'C_WASHING' 1.0 camera
(8115.7, 149.6, 26210.0) heading 116.6 fov 0.66` — the first shot, camera on the forecourt.
Screenshot: `docs/images/carwash-scene-s25.png` (the three babes walking out to the car).

The bank is not in `otdu2_cine_tool list`'s default sweep (that walks
`scenarics/cinematics`); `otdu2_cine_tool dump <bank>.bnk <out>` reads a spot bank directly,
which is how §2 was produced.

## 4. The wiring (WIP — the plan a fresh session can execute)

The pattern is the fuel feature (STATE session 16): a small owner class, a HUD prompt, and a
mode switch on validate. For the car wash the "screen" is the cutscene itself.

1. **Carry the spot through the mode switch.** `CinViewerRequest`
   (`source/game/cinematic/gm_cin_viewer.h`) is what `GMFreeRide` fills; today the spot
   frame can only come from the command line (`-cinspot`, `-cinexterior`, `GMCinViewer::enter`,
   `gm_cin_viewer.cpp` ~184-197). Add to the request: `float PickX = NAN, PickZ = NAN;`
   (nearest spots.bin record within 60 m, the `HousePickX` mechanism of
   `IntroCinematic::findHouseSpotIn`), `std::string Island;` (the exterior path is
   `islands/ibiza/exterior/…` today, **hardcoded** — the car-wash bank exists on both
   islands, so this must follow the island) and `std::string ExteriorPrefix = "ecw_a";`.
   Apply them in `GMCinViewer::enter` right after the CmdLine block.
2. **The player's car in the scene.** `gm_cin_viewer.cpp:224` loads the bank's `D_CAR`
   PosCam with a default `CinematicCar::Desc()` (the California). Add
   `CinematicCar::Desc Car; bool bPlayerCar = false;` to the request and pass
   `request.bPlayerCar ? request.Car : CinematicCar::Desc()`; `GMFreeRide` hands over its own
   `carDesc` (`gm_freeride.h:93`). Note the scene car is **not** repainted
   (`applyOwnedPaint()` only touches the driven car) — a cosmetic gap to be honest about.
3. **`source/game/freeride/car_wash.{h,cpp}`** — the owner class, mirroring `fuel.*`:
   `load(island)` walks `LoadSpotList` (`spot_exteriors.h`) for the `ECW_*` records and keeps
   bank + position + heading; `findNear(carPosition, radius)`; `requestScene(spot)` fills
   `GetCinViewerRequest()` with `Bank = "islands/<island>/exterior/ecw_a.bnk"`,
   `Cinematic = "C_WASHING"`, `Next = eGameMode::GM_FreeRide` and `bFromStory = true` (the
   flag that makes the viewer write `StoryHandoff` from `getCarWorld` and ignore the CmdLine
   overrides), plus the spot position and the island. The full retail beat later:
   `C_INTRO_E_1` -> `C_WASHING` -> `C_OUTRO`; one timeline is enough for the first wiring.
4. **FREERIDE hook** (`source/game/freeride/gm_freeride.cpp`, the near-spot / enter block
   ~1255-1450): the HUD tile art already exists — `carwash` is one of the frame labels of
   `HUD.vmf` `/HUD/enter/Spot` (`freeride_hud.cpp` ~500). Show
   `hud.showEnterSpot( "carwash", action, … )` when the car is **stopped** (< 20 km/h, the
   existing `bSlow`) within ~35 m of an `ECW_*` spot, and on `IA_MenuValidate` charge the fee
   and call `pGameInstance->setNextGameMode( eGameMode::GM_CinViewer )`. Show loc **3913**
   once on approach (`hud.showHint`, like the race hint 2220).
5. **Return to the island.** `StoryHandoff` is applied in `GMFreeRide::enter` only when
   `islandName == "ibiza"` (`gm_freeride.cpp:183`) — the handoff must work on Hawai too
   (5 car washes there), so relax that condition (or add a second handoff field) and check
   that `bFromDream` / `bHouseFlow` still behave (they gate the Ibiza story flow only).
6. **State + money.** `PlayerProfile` gains `BeenInCarWash` (the retail
   `SCPhoneCallsSave+4` flag, for the phone-call logic) and `CarWashCoating` (retail
   `CarDirtData+8`, kept 0 until dirt exists); save them in `profile.sav` beside
   `fuel_litres` (`player_profile.cpp`). Price **$10** `[INF: retail's fee is not in the
   readable data]`, deducted the way `FuelMenu::fill()` does.
7. **Test hooks** (the house style): `-atwash` spawns the car 15-20 m off the first `ECW_*`
   spot of the island so the prompt can be scripted; `-nowash` turns the feature off;
   `-logwash` prints the approach distance once a second.

Verify headlessly the way the fuel feature was:
`-gm FREERIDE -island ibiza -atwash -fixedtimestep -autoplay "60:press=validate;300:shot=…"
-exitframe N` -> the log must show the request, the CINVIEWER's `playing 'C_WASHING'`, then
FREERIDE re-entering under the handoff, and the screenshot the babes at the car.


## 5. Traps found (cheap to avoid, expensive to discover)

- **The cinematic is looked up by its *entity* name** (`C_WASHING`) while the file is
  `ecw_ai_sc_M_WASHING.cin`. `-cin ecw_ai_sc_M_WASHING` loads the bank, plays **nothing** and
  logs `main '' 0.0` at a fixed camera — it looks like a missing cinematic.
- `C_WASHING`'s origin is the authored `(0, 0, 0)`: the frame **must** be the spot
  (`-cinspot`), or the babes wash the car at the island origin.
- `request.bFromStory = true` makes `GMCinViewer::enter` ignore `-cinbank` / `-cin` (by
  design: a story request wins), so a test hook must set the request, not the args.
- `ecw_a.bnk` is the *same file* on Ibiza and Hawai; nothing inside it says which island it
  is drawn on — the exterior path and the spot list must both follow `islandName`.
- The scene car pose: this bank has **no `Entity__PosCam` and no `Entity__CinCar`** at all
  (`-logentities` prints none; session 25's "D_CAR ... it is there" was wrong) — the car is
  animated purely by the `.sce` actor `PLAYER_CAR`, so the car-actor name must reach
  `loadCar` (`CinViewerRequest::CarActor`), or every car-pose lookup (the StoryHandoff's
  `computeCarWorld` among them) fails silently.

## 6. Not done

- ~~**Nothing is wired**~~ **wired in session 58, see §8.**
- The other nine timelines: `C_INTRO_E_1/E_2` (arrival), `C_LOOP`, `C_OUTRO`,
  `C_OUTRO_FERRARI`, `C_INCAR` and the three 20 s `C_BABE_0x` booth loops.
- The tunnel (`interior/icw_a.bnk`): the rollers / machinery are there, but nothing enters it
  and there is no drive-through.
- Dirt: the port has no per-car dirt and no dirt shader, so `CarWashCoating` has nothing to
  clean yet. `_cw` (the workspace's scratch dump of `ecw_a.bnk`) and `_cwdump/` (the cutscene
  dump) are the working files, both gitignored.
- The scene's in-car camera (shot 2) — **§9, parked on the owner's call.**

## 7. The owner's second ask — "exteriors around buildings like caravan and houses"

- **Houses are already wired.** Every spot of this game copy resolves its exterior bank
  (`SpotExteriors: 80 spots, 80 with an exterior bank` on Ibiza including the mods' record;
  144/144 on Hawai) and they stream around the car / the map camera
  (spot_exteriors_spec.md §2-3). The *interiors* are the separate `lph_*` / `gph_*` banks
  (houses_spec.md): a house you do not own shows its exterior only, by design.
- **"Caravan" is not an asset, a bank or a spot.** The index has no name containing
  `caravan` / `caravane` / `mobile home`; the only camping-family entries in the 153,690
  assets are **`camping.2db`** (a *panel texture*:
  `decors/ibiza/objects/specific_map/panel`, in
  `islands/ibiza/level/area-1/area-1-2/sector-1-2-5-4.bnk`) and **`sf_tente`** (tent street
  fitting, Hawai sectors 4-3-4-0, 4-7-2-5 and 6-2-5-6). So a caravan park is **sector
  decor** (PMI instances placed by the sector bank), not a spots.bin record — `SpotExteriors`
  can never draw it. Next step for GLM: find the sector the caravan park is in (fly the
  world viewer over it), then check that its instance list type is one the loader reads —
  session 24 found that type 3 (`-s`, skinned) was skipped entirely by
  `EdenWorld::loadInstances`, the same class of bug that would make a caravan vanish.

## 8. Session 58 (`glm`): the wiring, executed and verified

All seven steps of §4 are in, with the deviations the code taught:

1. **`CinViewerRequest`** gained `PickX`/`PickZ` (NaN = off), `Island`, `ExteriorPrefix`
   and `bStory` (the dream marks it; a mode's own scene returns to the plain drive),
   applied in `GMCinViewer::enter` after the `-cin*` command-line block so a request wins
   (with `bFromStory` it already wins for bank/cinematic). `Island` feeds
   `Setup::HouseIsland`, which is what makes the pick search the right island's spots.bin —
   the five Hawai washes pick their own `ECW_A`.
2. **The player's car**: `Car` + `bPlayerCar` on the request; the scene car is repainted
   with the owned paint through the existing `IntroCinematic::getCar()` /
   `CinematicCar::applyDatabasePaint` (the cosmetic gap §4 feared is closed). Plus
   `CarActor` (see the corrected §5 trap): the `.sce` actor that animates the car when the
   bank has no `Entity__CinCar`.
3. **`source/game/freeride/car_wash.{h,cpp}`** — `load(island)` keeps the `ECW_*` records
   of `LoadSpotList`, `findNear` is X/Z within a radius, `fillRequest` builds the
   `C_WASHING` request (`bFromStory`, `bStory = false`, the spot frame, the island's
   `ecw_a.bnk`, `CarActor = "PLAYER_CAR"`). `CarWash::kPrice = 10` [INF as planned].
4. **FREERIDE hook** (`tickWorldSpots`): the `ECW_*` spots are removed from the
   interior-walk enterable set (`ECW_A` resolves `icw_a`, so until now the wash offered a
   *walk into the tunnel*); the wash claims the empty prompt slot — it loses to spots /
   races / championships like the championship tile does — and shows the retail `carwash`
   tile (`HUD.vmf /HUD/enter/Spot`, the sponge-and-droplet art) with `Enter` (1703) when
   stopped within 35 m. Hint 3913 once per approach, `-logwash` prints the nearest wash
   once a second, `-nowash` turns the whole feature off, validate pays only when the
   wallet covers the fee (an unaffordable wash hints and refuses).
5. **The return**: `StoryHandoff` carries `bStory` now; `GMFreeRide::enter` applies a
   handoff on **any** island when it is not the story's (`islandName == "ibiza" ||
   !handoff.bStory`), and a non-story handoff suppresses the house flow (the return is a
   plain drive with the white fade in, not `bFromDream`'s Solar Crown goal). Verified: the
   scene-end pose `(8126.23, 146.52, 26204.90)` — the forecourt, ~1.5 m from the spawn,
   the scene car never enters the tunnel inside `C_WASHING`.
6. **State + money**: `PlayerProfile::BeenInCarWash` (the retail `SCPhoneCallsSave +4`) and
   `CarWashCoating` (`CarDirtData +8`, stays 0) saved as `been_in_car_wash` /
   `car_wash_coating` beside `fuel_litres`. The fee is deducted like `FuelMenu::fill` does
   (in-memory until the next profile save).
7. **Test hooks**: `-atwash` spawns 18 m in front of the first wash at the spot record's
   ground height (y from the record — a y 0 spawn parks under the island: the fit probe
   shoots DOWN), facing it; it yields to a handoff so the return is not re-spawned.

Verified headless (the fuel method): `-gm FREERIDE -island ibiza -atwash -logwash -money
20000 -no3d -nopad -fixedtimestep -autoplay "100:shot=…;120:press=validate;…;1300:shot=…"
-exitframe 1600` → `CarWash: 3 wash(es) on 'ibiza'` → prompt at 18.1 m (`carwash-prompt-s58.png`:
the retail 3913 hint and the sponge tile) → `car wash 'ECW_A': paid $10 (money 19990) ->
C_WASHING` → `CINVIEWER: playing 'C_WASHING'` (`carwash-scene-washing-s58.png`: the
California on the forecourt, the babes walking in — the scene renders, this shot is
mid-shot-1) → `end of 'C_WASHING' after 16.4 s` → `GAME MODE : FREERIDE` at the handoff
pose (`carwash-return-s58.png`). Hawai: `5 wash(es)`, the same chain with the `hawai` bank
path. `-nowash`: 0 prompts, the spawn still works. `-storystep 5` still exits 0 in CINE.
The two retail strings come straight from the localization: the tile action via
`LocStr(1703)`, the hint via `LocStr(3913)`.

## 9. Parked (the owner's call): the washing scene's in-car camera poisons the frame

`C_WASHING` shot 2 (2.03–4.24 s, camera `c_washing_02` — the in-car view of the retail
video) breaks the frame: the port's camera falls back to the **spot origin with a NaN
heading** (`-logcin`: `t 3.0 camera (8128.7, 146.4, 26222.5) heading -nan`), and a NaN
camera basis defeats every frustum / distance cull, so the shadow pass draws **every
instance of the streamed sectors individually** — measured with temporary per-consumer
instrumentation (reverted): from frame ~122 on, **411,696 draw calls per frame** demand
**1.55 GB** of per-draw constants (`eden-constants` tag) against the 512 MB transient
buffer (`V_TreeBranch_A` × 22.3k, `V_HPin_G_Spr` × 13.4k, `V_TreeBark_B` × 13.4k in the
shadow pass alone); the first ~131k draws get constants, everything after fails its
allocation and skips — the black screen the owner sees where the in-car animation should
be. `docs/images/carwash-incar-nan-s58.png` is the t=3.0 frame: the fallback camera inside
the character group instead of the car interior.

The `.sce` data shows why: `c_washing_02` has **4 tracks** (the others 3) and its position
track has **4 components** (the others 3) — it is parented (its rest position is scene
local `(0, -0.16, 0.25)`, i.e. the tunnel mouth frame) and its extra track `00007055`
(3 comps, 12 keys, exactly 2.02–4.24 s) is presumably the attach / look-at the port does
not decode yet. This is **not a regression**: Cline's own session-24 build reproduces it
byte for byte (same `-nan`, 25.5M overflow lines, 2 GB log on the same command) — session
25's verification screenshot was taken at t=1.0 s, one second before the cut, so the bug
was never seen until the owner drove past 2.03 s.

Two mitigations landed with the wiring:

- **The overflow log cap** (`render_device.cpp`): a flooded frame logged ~800k lines /
  second (2.6 GB per minute), which read as a hang. `allocateTransient` now logs the first
  failure per frame plus one total line (`Transient buffer overflow: 411696 allocation(s)
  skipped this frame`); the callers already skip the draw on the empty allocation, so the
  frame completes and the scene keeps playing (black from 2.03 s on) instead of wedging
  the machine.
- **The StoryHandoff `CarActor`** (§8 step 2): `computeCarWorld` could not resolve the car
  at the end event at all in this bank (no `Entity__PosCam`, no actor name), so the handoff
  silently kept the car at the spawn; with the actor name the return pose is the scene-end
  pose, overflow or not.

The real fix (parked): decode the camera parenting in `CinematicScene` / the camera
sampler (compose `c_washing_02` with `PLAYER_CAR`'s animated pose, or at minimum clamp a
NaN camera to the last good pose of the previous shot so one broken camera can never
defeat the culling), then re-check the shadow-caster volume. The other nine timelines of
§6 may carry the same parented-camera pattern (`C_INCAR`, `C_BABE_0x` are 20 s single-shot
booth loops), so the fix belongs in the sampler, not in this scene.

## 10. Session 2026-11-04 (`win`): the retail sequence, the fee after the drive-in, the scene-state rule

The owner's reference (<https://www.youtube.com/watch?v=wfTDjHH-tls>, retail PC) and words:
*"when you accept to go to the carwash it automatically drives you to a spot where then it should
ask if you want to pay; 1st scene ... avatars face with mouth open; 2nd ... 1 girl with radio she puts
it down; 3rd ... the other 2 girls are behind her; then it kinda goes in game in car view ... girl in
front with hose and the other 2 girls either side of the car; after a short while it ends then they
walk away"*, and *"the vehicles state and avatar such as clothes etc... must be remembered when
entering ... except the todd driving you to the used car dealer"*.

### 10.1 The sequence (what plays now)

| stage | timeline | what | tag |
|---|---|---|---|
| accept (FREERIDE) | - | the tile's validate; no fee yet (a wallet below $10 is refused here, without the drive-in) | [INF] |
| drive-in | `C_INTRO_E_1` 6.55 s | `PLAYER_CAR` (24.2, 0, 0.8) -> (1.47, 0.13, -17.51), the wash place = `C_WASHING`'s rest pose | [DATA] |
| fee | `C_LOOP` (looped) + the fee box | validate pays $10 (`BeenInCarWash`, the body washed: mud / dust / crust 0, wet 1), back leaves (no fee, the drive resumes) | [INF] box layout, no retail string asks it |
| wash | `C_WASHING` 16.4 s | shot 1 the car, shot 2 the avatar's face (see 10.3), shot 3 the radio girl putting the radio down, the two others behind her | [DATA] |
| in car | `C_INCAR` 20 s (NO_CAMERA) + `C_BABE_01/02/03` looped | the car's cockpit camera (`camerasingame.xmb` mode 23 of its `_camera`), the avatar seated, the hose girl in front, the sponges at the sides (the three loop origins sit beside `PLAYER_CAR`) | [DATA] camera + origins, [INF] one pass |
| outro | `C_OUTRO` 6.84 s | the girls walk away | [DATA] |
| drive | FREERIDE | the handoff pose, the car as it left (10.2) | |

`CinViewerRequest` carries it: `Then` (the chain), `PromptAfter` / `PromptLoop` / `Price`, `InCar` /
`InCarBackgrounds`. `-washonly` plays the old single `C_WASHING` (no fee asked). Evidence:
`docs/images/carwash_retail_sequence.png` (drive-in, fee box, car, face, radio, the girls, in-car x2,
outro, back on the road). The CINVIEWER browser and timeline are now the tool's only: a game or story
request (`bFromStory`) draws neither (`-cinehud` forces them).

### 10.2 The scene-state rule (`VehicleSceneState`, `gm_cin_viewer.h`)

`GMFreeRide::exit` records the driven car's live state (roof, windows, `CarDirt` mud / dust /
hardness / wetness) and the avatar config. A scene with the player's car (`bPlayerCar`) puts it on
its scene car; `GMFreeRide::enter` puts it back on the car it spawns when it is the same car
(`-noscenestate` = the old reset). Before this every scene -> FREERIDE switch built a new
GMFreeRide and reset all of it. The avatar is `PlayerProfile::AvatarConfig` everywhere (there is no
separate worn-clothes state in the port yet - the casino wardrobe only prints "Wearing:"), and the
viewer no longer writes a default config into the profile. The story scenes (GMCine, the Todd drive
to the dealer) keep their own story car and never read it, as the owner asked.

### 10.3 The in-car face shot (`c_washing_02`)

Still not decoded: 4-component Position and Heading (Heading component 0 is NaN, the others look like
degrees: 100.7, -6.08, -14.9 -> -42.1 over the shot) plus track `0x7055` (12 keys, ~(0.07, 0.998, 0.0),
an up vector?) - attached to something. `IntroCinematic::updateView` no longer lets a NaN camera
through (that was the black screen / 411k-draw flood of section 9): it builds the face shot from the
scene car's `DVRSEAT_LR` (eye on the passenger side, looking at the driver's head) [INF], else keeps
the last good camera. The framing matches the video's face shot closely enough; decoding the real
parent stays open.

### 10.4 Audio: what the owner heard, and a lead

The owner heard the music *"cut off just as she places the radio then return in car view"* - in the
headless test runs. Those use `-fixedtimestep` at ~32 fps in the wash shots, so the 16.4 s of
`C_WASHING` took ~31 s of wall time while the 16.6 s `m_washing_intro_CINEMATIC` played in real time
and ran out half way; `C_INCAR` then starts `m_washing_CINEMATIC`. A real-time run (`OTDU_AUDIO_LOG=1`,
no `-fixedtimestep`) logs the intro track at gain 1.00 every second of the scene (0.4 -> 15.8 s) and
the in-car track after it: no cut. The clips are continuous music (MS-ADPCM, decoded offline: RMS
3,000..7,400 throughout). **Do not judge cutscene audio from a `-fixedtimestep` run.**

Lead, not applied: the `.cin` Fx record translation looks Z-up. Over the 136 cinematics the third
component is ~0 for most records while the second spans +-16 m; read as (x, y, z), `C_WASHING`'s
music sits 16 m under the forecourt (y 130 vs 146.5), as (x, -c, b) 5.3 m up under the canopy beside
the car. These two tracks are on the stereo bus 5 (not spatialized), so nothing audible depends on
it here; a positional `.cin` voice of another scene would be the test. `-logcinsound` now prints 2D
/ 3D, the position, the clip ranges and the fx flags of every cinematic sound.

### 10.5 Not done

- A pre-existing intermittent **crash on the return to FREERIDE after the wash** (segfault while
  FREERIDE reloads - traffic / map / birds; the committed tip `af5afe5` does it too, 1 run in 5;
  today's builds 1 in 2..3). cdb: an access violation in our exe on the main thread
  (`opentdu2+0x4246cd`, `mov rcx,[rsi]` with a garbage `rsi`), no symbols. Next: a RelWithDebInfo
  build (`/Zi`) and the same cdb line, `-gm FREERIDE -island ibiza -atwash -money 20000 -nopad
  -fixedtimestep -autoplay "120:press=validate;800:press=validate" -exitframe 3800`.
- `c_washing_02`'s real parenting (10.3); the retail fee prompt (none readable); `C_INTRO_E_2`,
  `C_OUTRO_FERRARI`; the tunnel `icw_a`.
