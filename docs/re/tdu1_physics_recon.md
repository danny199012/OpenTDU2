# TDU1 physics (OpenTestDriveUnlimited) vs. OpenTDU2 — recon and hybrid plan

Date: 2026-11-14. Source: `opentestdriveunlimited/OpenTestDriveUnlimited` (shallow clone,
`source/physics/`, ~23k lines, of which `vehicle/` ~9k). Tags: `[T1]` read from the TDU1
port's code (itself reconstructed from the TDU1 x86 exe, addresses are TDU1 `FUN_`s),
`[CODE]`/`[DATA]`/`[INF]` as in CLAUDE.md.

## 0. Licence / credit

The repo has **no LICENSE file**. The maintainer (`opentdu`) answered on the issue thread:
"Feel free to reuse the physics." That is written permission, not a licence.
- Record the thread link and the date in `README.md` credits (maintainer `opentdu` +
  contributors; ask them for the list of names).
- Until a LICENSE exists: **do not paste their code**. Re-implement from the equations and
  the TDU1 function addresses (the formulas are facts about the game, and we already read
  the same engine family). Their code is a *reading aid*, not a source file to merge.
- Their own advice: the TDU1 physics was Havok p-code; **Jolt did not map 1:1 to some Havok
  quirks**. We do not use the Havok vehicle SDK for the car (spec §1.1: `edVehicle` is Eden's
  own library), so this warning mostly affects chassis collision.

## 1. Same family, different layer

| | TDU1 port `[T1]` | OpenTDU2 |
|---|---|---|
| tyre | Pacejka, `FUN_0043cf50` (long.), `FUN_0043ceb0` (lat.), `FUN_0043d290` (normalise), `FUN_0043dbf0` (forces) | Pacejka, `FUN_00868dc0` / `FUN_00869090` / `FUN_00868a60` |
| engine / clutch / gearbox | own state machines | own state machines (`vehicle_dynamics.cpp`) |
| chassis | Jolt rigid body, per-wheel raycast + suspension force applied to the body | own integrator + Havok `.dhk` chassis collision |
| size | `vehicle_simulation.cpp` 1.8k + `physics_vehicle.cpp` 6k | `vehicle_dynamics.cpp` 1.65k + `vehicle_collision.cpp` 0.7k |

The tyre maths is the same *family* (B/C/D/E with `bcd = (b3·Fz²+b4·Fz)·exp(-b5·Fz)`, lateral
`sin(2·atan(Fz/a4))`). Eden carried it from TDU1 to TDU2.

## 2. The finding that matters: load sensitivity of D

- TDU1 `[T1]`: `D_lat = (L·C1 + C2)·L`, `D_long = L·C1 + C2`, `L` = load in per-mille.
  **D is a quadratic / affine function of load** — grip per newton falls as load grows.
  On top, per-tyre `PeakLat[32]` / `PeakLong[32]` tables over load
  (`LoadTableMaxLoad / LoadTableCount` steps) normalise the slip, and a friction-ellipse
  `Threshold` / `MinFactor` scales lateral force when longitudinal slip is high.
- OpenTDU2 `[CODE]`: `D = MaxFriction[surface] · Fz(kN) · FrictionScale`
  (`vehicle_dynamics.cpp:EvaluateTyre`). **Linear in Fz: zero load sensitivity.**
  That is exactly the lever `CLAUDE.md` §5 item 4 names for the rollover.
- Why it plausibly causes rolling over: with D linear in Fz, the loaded outside tyres
  gain grip in proportion to load, so the lateral force stays high until the car trips.
  Load-sensitive D saturates the loaded side, so the car slides instead. `[INF]` — **a
  hypothesis; test before trusting.**
- Caveat: TDU2's real D (inside the encrypted band) may differ from TDU1's. TDU2's data
  columns (`MaxLateralFriction`, `Lat[0..7]`) do not contain C1/C2 of the TDU1 form, so we
  cannot just drop TDU1's formula in. Candidates: use the PS3 twins of the ENC tyre code
  (`docs/re/ps3/ps3_enc_twins.json`) to find the real TDU2 D, and fall back to the TDU1
  shape with TDU2 data if PS3 shows the same.

## 3. Other differences

- TDU1 anti-roll: `gVehicleAntiRoll = false`, `kAntiRollFraction = 0.0` — **disabled in
  their port**, so it is no help for our roll problem.
- TDU1 has `updateGripModulation` (speed dependent grip), tyre temperature
  (`TireTemp`, `GripMult`), `FrictionEllipse*`, brake-grip blending by speed
  (`BrakeGripLatSlow/Fast`), landing-impact handling. Candidates for reading, not for
  copying: check each against the PS3 twin before adopting.
- Their `bike_*` code is TDU1-only (bikes). Ignore.
- `// TODO: FIXME: PLACEHOLDER Magic-Formula banks` in their `physics_vehicle.cpp` (line ~373):
  their tyre data is partly placeholder. Not a reference for tuning values.

## 4. Hybrid plan (keep TDU2 physics, inherit what TDU1 solves)

1. **Measure** the roll problem with `-vehtest` / `-logcorner` on 3 cars (baseline numbers
   saved to `_roll-baseline.txt`).
2. **Find TDU2's D.** Read the PS3 twin of the tyre evaluator (`get_func.py` on the
   functions in `ps3_enc_twins.json` that touch the Pacejka `D`) — decides between "TDU2
   has load sensitivity we are missing" and "TDU2 takes it from a table".
3. **Prototype** load-sensitive D behind a flag (`-tyreloadsens`), A/B on the baseline:
   lateral g at the roll threshold, `-logcorner` wheel lift.
4. **Then** the aid level (`physics_community_notes.md` §3), the friction ellipse and the
   grip modulation, one at a time, each A/B'd.
5. Keep every change behind a flag until the numbers beat the baseline.

## 5. Measurements (2026-11-14, `build-main`, `-vehtest`, flat plane, friction 0.7)

Sweep of 19 cars (`_vt_sweep_*.txt` in the session scratchpad), 6 steady-state corner
runs each (steer 0.25 / 0.5 / 1.0 x 60 / 100 km/h), `FLIPPED` = up axis y < 0.5.

**5.1 A harness artefact, fixed.** The corner test spawned every car at y = 0.12 m with no
settle. For big tyres (H3 r 0.427 m vs California 0.339 m) the wheels start ~0.3 m in the
road: first-tick loads read 10457 N = the 2x static clamp on all four wheels, and the
depenetration launch rolled the car. `gm_freeride.cpp` now spawns at
`0.12 + max(radius - 0.34, 0)` and pre-rolls 1 s straight (`-vehtestraw` = the old
behaviour). Effect: the 0.25@60 runs of H3 / Pekin / Q7 / Range Rover stopped flipping.

**5.2 The real rollover.** After the fix **H3, ML63 AMG, Q7 V12, Range Rover Sport,
Pekin to Paris still flip in 5 of 6 runs** (7 CSR 260 in 2 of 6); the sports cars, Touareg,
Buggy, HotRod, 2CV and Beetle never do. Mechanism from `-logcorner` (H3, 100 km/h, steer
0.25): in 0.17 s and 1.3 deg of roll the inner wheels unload to 0 N and the outer pair sits
on the 2x-static clamp (10457 N each); the car holds ~1.25 g = `MaxLateralFriction 1.8 x
road 0.7`, above its tipping limit (CG height `hubY + 0.15` ~ 0.58 m, track ~1.6 m
-> load transfer ~9.5 kN of 10.5 kN per side). `[INF]` Grip above the tipping limit tips a
tall car instead of letting it slide.

**5.3 Load-sensitive grip probe** (`-tyreloadsens k`, off by default,
`vehicle_dynamics.cpp` suspension loop): `mu x clamp(1 - k (Fz/static - 1), 0.5, 1.25)`.

| k | flips (6 cars x 6 runs, 12 cars checked) | California max lat g | Enzo | H3 |
|---|---|---|---|---|
| 0 (today) | 27 (H3 5, ML63 5, Q7 5, RR 5, Pekin 5, 7CSR 2) | 1.69 | 1.77 | 1.25 (flips) |
| 0.1 | 11 | 1.58 | 1.69 | flips |
| **0.2** | **0** | 1.50 | 1.63 | 1.34, no flip |
| 0.3 | 0 | 1.43 | 1.57 | 1.23 |

Side effect (k 0.2): California 0-100 4.52 -> 4.43 s, 100-0 35.8 -> 37.8 m; Enzo 0-100
3.42 -> 3.75 s (DB 3.65), 100-0 39.6 -> 41.4 m; H3 unchanged. So the knob does not wreck the
straight-line calibration.

**5.4 What this does and does not say.** `[INF]` Load sensitivity is *sufficient* to stop the
flips in this test; it is not proof that k = 0.2 is the retail law. The retail value must
come from the PS3 twin of the tyre code (plan step 2), and lateral g on a 0.7 plane (1.5 g
for sports cars) still needs an oracle number from the 360 build.

## 6. Not done

- Their `havok/` (Havok->Jolt converter, 900 lines) not read: relevant only to the
  chassis-collision side.
- `physics_vehicle.cpp` read only by outline, not line by line.
- Steps 2 (PS3 twin of the tyre `D`) and 4 (aid level, friction ellipse, grip modulation).
- The flat-plane test does not reproduce kerb / bump rollovers from the game; check one
  of those in-game with `-tyreloadsens 0.2` before changing the default.
