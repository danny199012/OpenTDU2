# Dev build content mod: survey

Written 2026-11-16. Tags as in `CLAUDE.md` section 4. This is a first survey: what the Apr 2012 dev
prototype (`E:\Documents\TDU2+Dev+Build\TDU2 Dev Build`, `TestDrive2DEV.exe`) holds, and what the port
could use from it as an opt-in mod. Nothing is implemented.

## The mod idea (owner's decision)

An optional launcher setting, "Dev build folder". The engine reads extra content from that folder at
runtime. **Nothing from it is copied into the repo, bundled or linked to.** Off by default; active
only when the folder is set and valid. Same opt-in pattern as `FuelModEnabled()`.
Owner's position: the build is publicly hosted on Hidden Palace; the project gives no links to it.

## What is in the folder [DATA]

- `TestDrive2DEV.exe` (19 MB, unencrypted `.text`) and the same bigfile set as the Steam install:
  `bigfile_EU_1..5`, `TDU2v082/083/086/097`, `TDU2DLC2v025/026/028/031`, `TDU2DLCEXPLOv017/023/024/027`,
  plus the `.cpr` config files. About 17 GB in total.
- Two sibling folders from other agents' work: `_assetcheck` (asset location scripts and
  `REPORT.md`) and `_re` (reverse-engineering notes on map, satnav, pause menu, F.R.I.M.).

## Findings

### 1. The bikes are already in our game data [DATA]
`_assetcheck/REPORT.md` (2026-11-16): the three rideable bikes sit in `TDU2DLC2v025.big`:
Ducati Desmosedici, Ducati Diavel, Harley-Davidson Fat Boy, with LODs, interiors, rims, engine
sounds and cockpit gauges (25 bike files). All 32 bike entries that exist in the unpacked data match
it byte for byte (SHA-256).
I confirmed the three `high` banks are present in `C:\TDU2-Gamedata\TestDrive2\Euro\Bnk\Vehicules\High`
(`desmosedici.bnk`, `diavel.bnk`, `fatboy.bnk`, plus `default_bike.bnk`).
**So the dev build is not needed to get the bike models.** The gap is elsewhere, see next point.

### 2. The missing piece for bikes is physics rows, not files [DATA]
`docs/re/bikes_spec.md` section 1: `DBCarPhysicsData` has one bike row, `Default_Bike` (id 666), and
the port already wears the Diavel model on it. The Desmosedici, Fat Boy and the other cut bikes have
models but **no physics row**. The Diavel bank carries torque curves (`diavel_max.xmb` /
`diavel_min.xmb`); I have not checked whether the Desmosedici and Fat Boy banks do.
Open: does the dev build's database add bike rows? Its `GamePC.cpr` and `Physics.cpr` differ from
the Steam copy (below), which is the first place to look.

### 3. `.cpr` files: decoded, and the difference is large [DATA]
Decoded with `otdu2_bnk_tool decrypt` (the port's own XTEA reader; so the earlier "Physics.cpr is
obfuscated" claim was simply a missing decrypt step). MD5, dev folder against the Steam install:
- identical: `Audio`, `DevicesPC`, `Language`, `Quality_Settings`, `Replay`, `Shadows`, `SystemPC`,
  `BuildVersion.dat`
- **different: `Physics.cpr`, `GamePC.cpr`, `Default_KB.xmb`**

**The game data the port reads is the TDU World file, not stock.**
`C:\TDU2-Gamedata\TestDrive2\Physics.cpr` has the same MD5 as the Steam copy (`9dde931f`), and
`GamePC.cpr` matches too (`b4606a81`). The dev build's copy (version string `TDU2 v12.04.25 PC`) is
the original Eden tuning. [INF] It is Apr 2012, so retail (Jul 2012) may differ slightly; still much
closer to stock than TDU World.

`Physics.cpr`, stock (dev) against TDU World (what the port runs now):
| key | dev (stock) | TDU World |
|---|---|---|
| `SecureAngularDamping` | 0.4 / 0.4 / 0.6 / 1.0 / 2.0 | 0 / 0.1 / 0.2 / 0.3 / 0.6 |
| `SportAngularDamping` | 0.15 / 0.3 / 0.6 / 1.0 / 1.0 | 0 / 0 / 0.1 / 0.15 / 0.3 |
| `HyperSportAngularDamping` | 0 / 0.2 / 0.4 / 0.6 / 0.8 | all 0 |
| `OffRoadAngularDamping` | ... 0.1 / 1.0 at 300 / 400 | all 0 |
| `SteeringMaxAngle` | 30 / 13 / 10 / 8 / 6.5 deg at 0 / 100 / 200 / 300 / 400 | 30.2 / 30 / 30 / 30 / 24.4 |
| `SteeringVelocity` | 380 falling to 340 | flat 380 |
| `ExtraGravityMin` / `AccelDuration` / `AccelDelay` | 0.1 / 0.1 / 0.01 | 0.01 / 0.05 / 0.001 |
| `AbsControlSlipRatio` | -0.2 (-0.2 / -0.4 / -0.6 in the bike block) | -0.35 (-0.25 bike) |
| `AbsControlActivation` / `VelocityMin` | 0 / 1 | 1 / 5 |
| `TractionControlActivation` / `VelocityMin` / `Max` | 10 / 10 / 70 | 5 / 5 / 50 |
| `HandBrakeActivation` / `VelocityMax` / `SlipRatio` | 0 / 50 / -0.4..-0.6 | 1 / 500 / -0.25 |
| `*SteeringFeedback*` | low | 400 to 800 (force feedback) |
| bike `GyroscopicEffect`, `MaxLeanVelocity` | milder | stronger / 5.0 at standstill |

Why it matters: `CLAUDE.md` priority 4 says the car "still rolls over too easily". Stock angular
damping and the narrow steering angle at speed (13 deg at 100) both resist rolling and twitchiness;
the World file removes them. [INF] This is a strong lead, not a proven cause. It also bears on the
parked-car report, since stock `ExtraGravityMin` differs.

`GamePC.cpr`: the differences are version strings, `ALLOWSKIPCINE`, `CASINO_FORCE_DLC`,
`NetworkDebugInfo`, `CheatPhysicChecksEverywhere` and the online server entries (stock points at the
original EA hosts, World at `tduwpc.tduworld.com`). Nothing gameplay-relevant for the port.

### 4. Earlier note: package data matches retail [DATA]
Memory note from 2026-11-14: dev package data is byte-identical to retail, no extra map or cut data.
Together with finding 1, this suggests the dev build's content value is small. Its value is the
readable code, not the assets.

### 5. The dev exe is a code source, not a content source [DATA]
`TestDrive2DEV.exe` has readable code, 278 source paths, debug menu strings ("Full Damage ON/OFF",
"Lateral grip cheat", teleport cheats) and a `USENEWDYNAMICS` switch with a per-car `_newdyna`
column. The physics report (pasted by the owner, from a string and symbol search) adds that the build
has no hill-hold system and has a `hkpVehicleDefaultVelocityDamper` class. That report lists what
exists; it does not prove what runs. Detail in `docs/re/dev_build_physics.md`.

## What the port could use, as a mod

| Candidate | Source | Verdict |
|---|---|---|
| Desmosedici, Fat Boy, Diavel models | already in retail data | no dev folder needed; needs physics rows and wiring |
| Bike physics rows | maybe in dev `Physics.cpr` / `GamePC.cpr` | **unverified**, first thing to check |
| Debug / cheat menu behaviour | dev exe code | reimplement from the readable code; no data needed |
| Physics reference | dev exe code | a reading source for the port, not a runtime dependency |
| Extra maps, cars, cutscenes | none found | memory note says byte-identical |

## Recommended next steps

1. **A/B the stock `Physics.cpr`** (the dev copy) against the World copy with `-vehtest` and
   `-logcorner`, on the roll-over and steering complaints. Needs a way to point the port at an
   alternative `Physics.cpr`; not built. As a mod this is a "stock handling" option, and needs no
   dev-folder setting beyond that one file.
2. Check the Desmosedici and Fat Boy banks for torque curves and `.dhk` files, as the Diavel has.
3. If the dev build adds no content beyond retail, drop the launcher setting. Make the bike work
   (physics rows plus wiring, opt-in) with data we already read, and keep the dev build as a
   reference.
4. Only build the "Dev build folder" setting if step 1 finds content that retail lacks.

## A/B result: stock `Physics.cpr` against the TDU World file (2026-11-16)

New flag `-physicscpr <absolute path>` (`source/config/gs_config.cpp`) loads an alternative
`Physics.cpr`; without it nothing changes. Run on the Enzo, flat ground, `-vehtest`:

    opentdu2-glm.exe -game C:/TDU2-Gamedata/TestDrive2 -userdata C:/temp/otdu2-win-userdata -gm FREERIDE \
      -skipcine -no3d -nopad -fixedtimestep -car Enzo -vehtest -logcorner \
      [-physicscpr "E:\Documents\TDU2+Dev+Build\TDU2 Dev Build\Physics.cpr"] -exitframe 30

| test | World | stock (dev) |
|---|---|---|
| top speed, 0 to 300 | 307.5 km/h | 306.9 km/h (straight line unchanged) |
| 100-0 km/h | 39.6 m, 3.70 s | identical |
| steady corner, full lock, 100 km/h | 1.70 g, radius 44.4 m, wheel angle 30.0 deg | **1.98 g, radius 38.8 m, wheel angle 13.2 deg** |
| steady corner, full lock, 60 km/h | 1.72 g, radius 15.9 m, 30.1 deg | 1.88 g, radius 14.8 m, 19.9 deg |
| steady corner, quarter lock, 100 km/h | 2.07 g, radius 37.3 m, 7.5 deg | 1.53 g, radius 50.9 m, 3.3 deg |
| max body roll, any corner | 0.5 to 0.8 deg | 0.3 to 0.8 deg |

Reading [INF]:
- Stock caps the wheel angle at speed, so full lock at 100 km/h no longer plows (World 1.70 g against
  stock 1.98 g). Stock steering is also gentler near centre (quarter lock gives 1.53 g against 2.07 g).
- **Roll-over is not reproduced by either file.** Roll stays under 1 deg in this steady-state test, so the
  angular-damping difference (a transient effect) is not exercised. The "rolls over too easily" lead stays
  open; a sudden full-lock or lane-change test at 150 km/h or more is the next test, not written yet.
- Acceleration and braking come out the same, so the stock file changes steering and aid feel, not
  straight-line performance.

### Transient test (2026-11-16)

`RunVehicleTest` (`source/game/freeride/gm_freeride.cpp`, `-vehtest`) now also runs three manoeuvres at
100 / 150 / 200 km/h on flat ground: `jturn` (full lock held 2 s), `lane` (full lock 0.5 s, the other way
1 s, back to centre) and `liftoff` (half lock, throttle dropped and brake tapped at 2 s). Output lines
start `VEHTEST transient`. "Peak lateral" there is yaw rate times speed, so it spikes in a spin and is
**not** a true lateral g; read roll, `FLIPPED` and the end speed.

Same command as above with `-car Enzo` and `-car H3` (tall SUV), World against stock:
- **Roll-over is not reproduced by either file.** Peak roll is 0.7 to 0.8 deg (Enzo) and 1.1 to 1.4 deg
  (H3) in every manoeuvre, no `FLIPPED`. Tyre grip on flat ground does not roll these cars. [INF] The
  reported roll-overs probably need a tripping event the flat harness lacks (kerbs, terrain edges,
  collisions, uneven ground); the next test would be a sloped or kerbed surface, not a tyre table change.
- **Stock is more stable in spins.** Enzo World `lane` at 200 km/h ends at -19 km/h (spun round) with
  yaw 3.64 rad/s; stock ends at 109 km/h with yaw 2.27. Enzo `liftoff` at 100 km/h: World 36 km/h,
  stock 100 km/h; at 200 km/h: World -32 km/h, stock 97 km/h. That is the angular damping the World
  file removes. H3 differs little (both file settings stay controlled).
- Net: the stock file is the better handling feel (fewer spin-outs, sane steering angle at speed) but it
  does not fix roll-over; those are separate problems.
