# What the Apr 2012 dev prototype gives us (survey, 2026-11-14)

Source: `TestDrive2DEV.exe` + its package (Hidden Palace, "Apr 6 2012 prototype"). Companion docs: `dev_build_physics.md` (physics),
`damage_spec.md` (damage), `dev_build_modules.md` (the 279 source modules it names). Tags as in the other specs.

## 1. The package carries no extra game data [DATA]
All 59 comparable `.big` / `.map` / `.cpr` / `.xmb` files are **byte-size identical** to the Steam retail install (the five `bigfile_EU_*` and every DLC /
patch archive, including `TDU2DLC2v025..031` and `TDU2v082..097`). So there are **no new assets** in the package: anything "cut" has to be code paths in
the exe, or data that retail also ships but does not load. Two files differ (below). `Euro/` holds only the 116 shaders.

## 2. Physics.cpr / GamePC.cpr: the retail install is the TDU World mod [DATA]
`cpr.py`-style decryption (XTEA, same key as `source/core/crypto/cpr.cpp`) of both versions:
- `GamePC.cpr`: dev = Eden's own (`TDU2 v12.04.25 PC`, `pc-proxy1..8.testdriveunlimited2.com`, `NetworkDebugInfo = ON`, `ALLOWSKIPCINE = 0`);
  retail = `TDU2 World Edition PC V1.0`, `tduwpc.tduworld.com`. Only version strings / servers / dev switches, nothing for gameplay.
- **`Physics.cpr`: the dev file is the original Eden tuning; the retail one is TDU World's retune.** Differences (retail -> dev):
  `Secure / Sport / HyperSportAngularDamping` 0 .. 0.6 -> **0.4..2.0 / 0.15..1.0 / 0..0.8** (strong speed-dependent yaw damping);
  `SteeringMaxAngle` 30.2/30/30/30/24.4 -> **30/13/10/8/6.5** (lock shrinks with speed); `SteeringVelocity` 380 flat -> 380..340;
  `AbsControlActivation` 1 -> 0, `AbsControlSlipRatio` -0.35 -> -0.2, `TractionControl*` 5/5/50/0.4 -> 10/10/70/0.5, `HandBrakeActivation` 1 -> 0,
  `ExtraGravityMin/AccelDuration/Delay` 0.01/0.05/0.001 -> 0.1/0.1/0.01, `GyroscopicEffect`, `MaxLeanVelocity`, steering feedback friction / spring.
- The port already parses the whole file (`vehicle_data.cpp` `ReadIniCurve` / `cprFloat`), so the original tuning works today as an **opt-in
  mod with no code**: copy `<dev build>/Physics.cpr` to `<user data>/mods/Physics.cpr` (log: `Mod overlay: 'Physics.cpr'`).
  Measured with `-vehtest` (H3 / Enzo / California): flips 0, lateral g 1.63 / 2.07 / 1.70 (vs 1.63 / 2.07 / 1.72), 100-0 within 0.5 m: the harness barely
  sees it; the difference is in steering lock and yaw damping *feel* (owner check).

## 3. The exe: code readable only here [CODE-dev]
`.text` is not encrypted. Only **46 of 279** module names also appear in the retail exe's readable bytes, so for nearly every subsystem this is the only
readable source. Subsystems of interest and where they live (full list: `dev_build_modules.md`):

| Subsystem | Modules | Port today | Notes |
|---|---|---|---|
| Car damage | `DisplayVehicleDamage.cpp`, DB `DBCarDamageData`, shaders `TD_CarDeform_TS` / `TD_damagemap` | **accumulator in (this session)**, no visuals | `damage_spec.md` |
| Tall grass | `MngTallGrass.cpp`; names `Car_Grass1..4(_Low)` (use not read yet) | grass drawn from the V/O `.pmi` data | the manager (density / LOD / car interaction) is now readable |
| Traffic | `AiPoolTraffic.cpp`, `AiSimBubble.cpp`, `AiRoadGraph/Map/Shape/Buckets.cpp`, `AiSeparationBehavior.cpp`, `AiVehBhv*` | `traffic.cpp` (lane following, no response) | the retail driver is `[ENC]`; here it is readable |
| Police | `AiCopMng.cpp`, `AiNavPolice.cpp`, `AiPoliceTactics.cpp`, `AiCopVoicesMng.cpp`, `AiPlanChased/HotSpot/FullCoverage.cpp` | none | |
| Race AI | `AiNavOpponent2.cpp`, `AiNavRaceBase.cpp`, `AiRaceTactics.cpp`, `AiTrajOptimizer.cpp`, `AiOptimizedTrajectories.cpp` | `opponents_spec.md` (partial) | |
| Road signs / blocked roads | `MngBlockedRoads.cpp`, `MngElectricalNetwork.cpp`, `RoadPhantom.cpp`, `edRoadShape.cpp` | signs via the scenery data | no dedicated sign module: signs are world objects; the lights network is `MngElectricalNetwork` (readable in retail too) |
| Wrecks | `Wrecks.cpp` | discovery points (28 wrecks) | |
| Avatar customisation | `GMDressing.cpp`, `GMHairdresser.cpp`, `GMSurgery.cpp`, `GSClothSystem.cpp`, `AvatarBnk*.cpp`, DB `DBClothe` / `DBRewardClothe` | creation + garage walk only | strings `RandomMotoDresses / RandomMotoGloves / RandomGlasses / Clothes / accessory / _reward_clothes_won` |
| Bikes (DLC2) | `HST_DLC_TUTO_BIKES`, `MotorBike`, `D_BIKE`, `Default_Bike` physics row, `MaxLeanVelocity` / `GyroscopicEffect` in Physics.cpr | bike code path exists (`Default_Bike` in the roster) | the bike archives are in the retail install (DLC2) |
| Clubs / lobby / online | `GMClub.cpp`, `GMLobby.cpp`, `MngClubGroup.cpp`, `MngClubVsClub.cpp`, `edLive*` | none (out of scope) | |
| Debug | `AiDebugMenu.cpp`, `GSDebug.cpp`, `DbgAvatarEditor.cpp`, `DbgScenaric.cpp`, `MngDbgReplay` | `-dump*` flags | menu strings: `Full Damage ON/OFF`, `Lateral grip cheat` (AI), `CheatTeleportForward`, `CHEAT WRECKS`, `RESET WRECKS`, `DISPLAY SC WRECKS INFOS` |

**"Cut content" claim:** not verifiable from the package. Dev-only *strings* prove only that a name is not readable in the retail exe (whose data section is
encrypted); the data archives are identical to retail's. Candidates worth checking in the shared data: `DBClothe` rows with no shop entry, the
`HST_STICKER_SHOP_*` / `MapShop` strings, `Car_Grass*`. Running the dev exe itself (`TDU2_No_menu.bat` = `TestDrive2DEV.exe -mode offline`, with its debug menu)
would show the cut features directly; I have not run it (untrusted download), the owner can.

## 4. Mods: what can and cannot be hosted
- **Cannot be hosted by this project:** Eden / Atari assets (the dev exe, its menus, outfits, accessories, any retail or dev data). Opt-in does not change that:
  hosting them is redistribution (`README.md`: "No game data is distributed"). A mod must contain only **our own / community-authored** files, or point at files the
  user already owns.
- **Can be an opt-in mod without redistribution:** (a) *"Original physics tuning"* = a mod that, on install, copies `Physics.cpr` **from the user's own dev
  build folder** (the launcher asks for the folder) into the mods overlay; (b) code features behind flags (damage, hub-height lateral force) that are ours;
  (c) community content packs with a manifest, hosted by their authors.
- **Launcher today:** a single `Open mods folder` button (`tools/launcher/launcher_ui.cpp:1263`); the game overlay is one directory (`<user data>/mods`, `-mods`, `-nomods`).
- **Mods page (built 2026-11-14):** `tools/launcher/launcher_mods.{h,cpp}` + `Page::Mods` in `launcher_ui.cpp`; the game reads `<mods>/enabled.txt` and the folders `<mods>/packages/<id>/` as overlay layers after the loose mods folder (`gs_file.cpp`). Bundled import mod `original-physics` (mod.json only; "Import from my copy" copies `Physics.cpr` from the user's own Dev Build folder). Logic verified headless (9 checks + the engine resolving the package file); the page itself was not looked at on screen. Original proposal: `<mods>/<name>/mod.json` (`name`, `version`, `description`, `author`, `requires`, `files`), the launcher lists them with an
  enable toggle and a "Import from my copy" action for mods that need the user's own files, writes `<user data>/mods.json` (enabled list, priority), and the game
  merges the enabled folders in that order into its overlay. Needs: a multi-root overlay in `gs_file.cpp` (today one root), a manifest parser (the launcher already
  has `launcher_json`), and the page in `launcher_ui.cpp`.

## 5. Suggested order
1. Damage visuals (needs the damage-map render pass) - `damage_spec.md` 4.
2. Police and traffic response from the readable AI modules (largest gameplay gap).
3. Mods page + the "Original physics tuning" import mod.
4. Tall-grass manager / car interaction; avatar customisation modes (`GMDressing` / `GMHairdresser` / `GMSurgery`).
