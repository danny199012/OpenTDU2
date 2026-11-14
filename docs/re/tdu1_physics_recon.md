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
on the 2x-static clamp (10457 N each) and the car tips at ~1.25 g. **Correction (same day):** an earlier draft said this was `MaxLateralFriction 1.8 x road 0.7`; wrong - `hit.Friction` is stored (`state.SurfaceFriction`) but never enters `EvaluateTyre`, so the harness's 0.7 plane does nothing and the tyre has ~1.8 g of grip (`D = MaxLateralFriction x Fz`). The H3 tips at ~1.25 g (CG height `hubY + 0.15` ~ 0.58 m, track ~1.6 m -> load transfer ~9.5 kN of 10.5 kN per side) long before the tyre saturates. `[INF]` Grip above the tipping limit tips a
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

## 6. PS3 tyre code: attempt and result (2026-11-14)

Not reached. Blockers found: the PS3 libm is statically linked with no names (the `atan` constants 2.414 / 0.414 are not in the image as floats, `EBOOT.imports.json` has no math imports); the scalar pool `0xf11f00-0xf1214c` has no xrefs (`constant_pools.md` result 3); the `*_enc_twins.json` / `pc_ps3_anchors.json` dumps are not on disk (uncommitted, `README.md` here). What the PC side already says `[CODE]`: the readable `FUN_00868dc0` / `FUN_00869090` / `FUN_00868a60` use `D = MaxFriction x Fz` (linear), so the PS3 twin of the tyre maths will match and **retail's tyre has no D load sensitivity**. The anti-flip behaviour of retail SUVs must come from elsewhere: the ENC suspension force law / roll stiffness, the CG height (`hubY + CgHeight` is `[INF]`), the `kMaxTyreLoadFactor` clamp (`[INF]`, invented), or an aid. So `-tyreloadsens` is a *stopgap probe*, not a recovered law. Next: locate the PS3 edVehicle2 suspension update (the `0x15xxxx-0x17xxxx` cluster) and compare it with our `[INF]` spring / stabiliser / bump-stop law; start with a base-register search for the scalar pool (`constant_pools.md` result 3).

### 6.1 PS3 suspension search (2026-11-14, second pass) - law not found, method and dead ends recorded

Method: scan the 53,119 lifted functions (`port/recompiled/*.cpp`, regex over `vm_read32(ctx->gpr[N] + 0xOFF)`,
`ppu_fmul` / `ppu_fmadd_core`) for the PC suspension layout (spec section 2: DampingArray +0x60/+0x64, Force +0x68,
MaxDamperForce +0x6c, ForceAtRest +0x70, StiffnessBias +0x74, StiffnessAtRest +0x78, stabiliser +0xb4/+0xb8, LowerStop +0xc8,
UpperStop +0xcc). Tool notes: float math in the lifted code is `ppu_fp_single(ppu_fmul|ppu_fadd|ppu_fsub|ppu_fdiv(...))`
and `ppu_fmadd_core(a, b, c, ...)`; **`gpr[1]` is the stack pointer, so exclude it** (the first scan "found" five wheel
candidates that were all stack offsets); on Windows use `cygpath -m` paths for Python.

| candidate | what it is | verdict |
|---|---|---|
| `0x187C48` / `0x188CD8` / `0x189B08` / `0x189C10` | huge frames, the "wheel offsets" were `sp+0x6a8...` | false positive |
| `0x46E700`, `0x46D7F8`, `0x8467A8`, `0x2FEC78`, `0x7C9EB0` | serializers / copy: read the fields, no arithmetic | not the law |
| `0x772A68`, `0x771844`, `0x772680` | struct copy / assignment of the class (fields up to +0x23c, 5 float groups) | **the class exists here** (`0x77xxxx`), useful for the layout, not the law |
| `0x770AF8` | jump-table (`ctr`) indexed property blend: `(a*t + b*(1-t)) / c` lerps | not the law |
| `0x40144C` | generic curve / vec3 Hermite-lerp evaluator (reads +0x60..+0x78 of a *curve* class) | false positive |

What it established `[INF]`: the PS3 suspension layout **does** match the PC one (the copy function `0x772A68` moves exactly the
+0x78 / +0xb4 / +0xb8 / +0xc8 / +0xcc float group), so offset fingerprints are valid; but a field-offset match also hits every
copy / serialize / blend function, so a leaf-function filter is not enough.

**Next, in order:** (1) the suspension serializer is `func_000CC09C` (the `StiffnessAtRest` name `0xfb39b8` is referenced
at `0xCCDDC`): map each name string to its struct offset (HI/LO materialisation then the `func_00C8B688/BFD0` write call),
which also confirms `MaxDamperForce +0x6c`; (2) find the class **vtable** - the constructor or copy owner in `0x77xxxx` stores
the vtable pointer; vtable slot `+0x24` (PC: `0x54bc70` / `0x54bac0`) is the force law; (3) read that function and compare with
`vehicle_dynamics.cpp` lines ~1270-1292: in particular whether `MaxDamperForce` caps the damper term (our port does not use
it) and what the real bump stop is (ours is an `[INF]` capped force).

### 6.2 Third pass (same day) - still not found; stop fingerprinting

| candidate | what it is | verdict |
|---|---|---|
| `0xCC09C` (949 lines, `0xCC09C-0xCCE5C`) | **not a serializer**: Havok-style constraint set-up - sums fields `+0xB4..+0x114`, divides by inverse-mass sums, flag bits `&1` / `&2`, float constants at `0xFB1B4C / 50 / 54 / 58` | probably the **contact / chassis constraint solver** (spec 6.8 `[ENC]` item), not the suspension; worth reading for that item |
| `0x10A0AE8` | a table of ~16 function descriptors (`0x75c798 ... 0x780624`) - a per-class registry (copy / serialize slots), **not** a vtable | the `0x77xxxx` class is registered here; no vtable slot `+0x24` |
| `0x41D3A0` / `0x41D5F0` | 985-line function; its `+0x6A0/+0x6AC/+0x6B0/+0x6B4` reads use base `0xF3<<16` = **`.rodata` globals**, not wheel fields | false positive (offset collision with data) |
| `0x86EA60`, `0x787CA8` | wheel set-up / serialization, no arithmetic | not the law |

Conclusion `[INF]`: a scan on field offsets collides with every copy / serialize / data-global access, and the law is probably reached
through a vtable call, so no function contains the offset set *and* the arithmetic in one place. Three passes of regex scans
(six functions read, eleven ruled out) have stopped converging. Two routes are left:

1. **A real decompiler on the PS3 ELF** (Ghidra / ReVa on `EBOOT.ELF`): recover the vtable of the wheel's suspension object from
   the constructor that stores `this+0x640` and read slot `+0x24`. This is the route the TDU1 port itself uses.
2. **The behavioural oracle**: the 360 build is runnable; measure the retail H3 / Q7 steady-state cornering (lateral g at the point the
   inner wheels lift) and fit our `[INF]` parameters (CG height, the 2x static load clamp, bump stop) to it, instead of reading the law.

### 6.3 The 360 oracle: state of play (2026-11-14) - not usable for numbers yet

Checked in `E:\...\Test-Drive-Unlimited-2-Recomp` (rexglue static recomp, native Windows, `out/build/win-amd64-perf/tdu2.exe`, built 2026-10-28):

- It **runs** and plays (F1 menu, money / DLC cheats, `[scene]` log, in-world ~14-16 fps), but it has **no car spawn / teleport / car picker and no input scripting**: a measurement run is a human driving.
- It has a **physics sampler** (`src/hooks/tdu2_physics_sampler.cpp`, `--tdu2_phys_csv <file>`, `HANDOFF.md` section 8): one CSV row per physics step - chassis position, rotation, velocity, angular velocity, per-wheel suspension length / spin / contact / slip, from Havok `hkpVehicleInstance` fields whose offsets come from the 360 binary's own reflection tables.
- **That sampler has never produced a row.** HANDOFF section 8: "Built and linked ... Not run"; the hook point `sub_8898D268` (inferred to be `hkpVehicleInstance::applyAction`) is unconfirmed; the only related line in 212 run logs is `[scene] ... no physics vehicle seen in the last 5 s`. Our own spec 1.1 says TDU2 cars are driven by Eden's `edVehicle`, not the Havok vehicle SDK, so the Havok vehicle may simply not exist at runtime; the chassis `hkpRigidBody` does.
- **Policy**: the PC `TestDrive2.exe` must not be hooked or read in a running process (CLAUDE.md, SecuROM policy), so the 360 recomp is the only sanctioned runnable oracle.

What a numeric oracle needs, in order (each is a session, not a step): (1) validate or replace the sampler hook (log the chassis `hkpRigidBody` instead, which exists for sure: position `+0x110`, rotation `+0xE0..`, velocities `+0x1A0/+0x1B0` on the entity); (2) get an SUV into the 360 game (money cheat + dealership: H3 / Q7 / Range Rover); (3) a steady circle by hand at ~60 / 100 km/h, then compare peak lateral g, roll angle and wheel lift with `-vehtest -logcorner`. The cheapest oracle is a **yes/no from a person who plays retail**: does a tall SUV roll over on a flat road at a normal steering input?

### 6.4 Retail answer and two candidate fixes (2026-11-14)

**Retail observation (owner, 360 recomp, Q7 V12, flat road):** turning hard and using the handbrake at speed, the SUV **slides and does not tip**. So the tall-car rollover in `-vehtest` (H3 / ML63 / Q7 / Range Rover / Pekin to Paris, 5 of 6 runs each) **is a bug of ours**, not retail behaviour. `[MEASURED]` (one car, one person, by hand).

Two independent switches, both default off, both remove every flip in the 12-car sweep (72 corner runs):

| switch | what | flips | side effects (`-vehtest`) |
|---|---|---|---|
| `-cgframe 1` | centre of mass y = `_gravity_center_height / 100` **literally** (spec 6.1: `vfunc 0xf4((0, GEO+0x88, 0))` `[CODE]`); today `hubY + h` (`[INF]`) | 0 of 72; roll max 0.5-2.1 deg | peak lat g 1.8-1.96; Enzo 0-100 3.42 -> 3.83 s, top 324 -> 314.6 km/h; 100-0 26 -> 23 m (Q7), 40 -> 33 m (Enzo): braking looks too short |
| `-tyreloadsens 0.2` | grip x clamp(1 - k (Fz/static - 1)) `[INF]` | 0 of 72 | peak lat g -10 %; 100-0 +4-5 %; Enzo 0-100 3.42 -> 3.75 s (DB 3.65) |

Evidence about the CG: the DB value is a tuning knob, not a height (Q7 0.08 m, H3 0.15 m, California / Enzo 0.18 m), while the chassis `.dhk` stores a realistic centre of mass (H3 0.542, Q7 0.525, California 0.480, Enzo 0.439 m, same frame). `-cgframe 2` (hub + h/2) still flips 2-4 of 6, so the cure needs a CG well below the hub line. Caveat `[INF]`: `-car X` in `-vehtest` keeps the California's hub positions (`hubY 0.339` for every car), so the SUV numbers are an approximation of the in-game cars.

**Open:** which of the two (or neither alone) matches retail feel for body roll, braking dive and top speed. Needs the DualSense: drive a sports car and an SUV with each flag and say which is closer to retail. A third lead stays untested: retail's anti-flip may live in the suspension law (still `[ENC]`, section 6.1-6.3).

## 7. Not done

- Their `havok/` (Havok->Jolt converter, 900 lines) not read: relevant only to the
  chassis-collision side.
- `physics_vehicle.cpp` read only by outline, not line by line.
- Steps 2 (PS3 twin of the tyre `D`) and 4 (aid level, friction ellipse, grip modulation).
- The flat-plane test does not reproduce kerb / bump rollovers from the game; check one
  of those in-game with `-tyreloadsens 0.2` before changing the default.
