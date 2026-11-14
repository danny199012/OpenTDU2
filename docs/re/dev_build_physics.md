# The Apr 2012 dev prototype as a physics reading source

`E:\Documents\TDU2+Dev+Build\TDU2 Dev Build\TestDrive2DEV.exe` (Hidden Palace, "Apr 6, 2012 prototype";
used by Eden for physics / damage testing). Tags: `[CODE-dev]` read from this exe, `[CODE]` retail exe,
`[INF]` inferred. Tool: `tools/devbuild_disasm.py`.

## 1. What it is [CODE-dev]
- PE timestamp 2012-04-08 (retail: 2012-07-10). 19.8 MB, `.text` 16.6 MB, entropy 5.89: **not encrypted, no
  SecuROM sections** (retail `.text` 7.24 + `Stext` 8.00). The whole PC engine is readable, including the
  `0x401000-0x6D1000` band that is `[ENC]` in retail. The SecuROM policy is untouched: nothing is decrypted.
- 278 source paths `...\Branches\MC_PC\Dev\Sources\*` (carphysics, havokvehicle, vehicletdu, displayvehicledamage,
  mngcartireskid, gsphysics, aidebugmenu, gsdebug...), 709 RTTI classes (Havok), build string `PC OI v2.4.2 Jun 15 2011`.
- Ships its own ~15 GB bigfile set; launch: `TestDrive2DEV.exe -mode offline`. Layout differs from retail, so
  **no address carries over**: match by string, constant or vtable slot index.
- Dev-only content: debug menu strings ("Full Damage ON/OFF", `CheatTeleportForward`), `AiDebugMenu.cpp`.
  `"Lateral grip cheat"` (0x1497e64, loader 0x552420) is **AI opponent tuning** (Tuning level / aggressiveness /
  Max turning speed factor / Lateral grip cheat / Rear pressure ...), *not* a player tyre knob.

## 2. Tyre: same law as retail [CODE-dev, partly read]
`0x100d010` (longitudinal, `B = 99999` guard at `0x14c68cc`), fast atan with 0.28 (`0x14d45d0`, users `0xfe7820`),
`D = MaxFriction[surface] (esi+0x90+4*surf) x Fz x ...`, `E` quadratic in Fz (coefficients +0x54/+0x58/+0x5c).
Same family as retail `FUN_00868dc0` (spec 4.2). **Conclusion: Pacejka `D` is linear in Fz in TDU2 as well, so
"the missing load sensitivity of D" (CLAUDE.md 5.4) is not in the tyre evaluator.** The lever is elsewhere
(suspension, contact, weight transfer, the unread `0x100cbc0`/`0x100d730` siblings).

## 3. Suspension: the `[ENC]` suspension vfunc 0x24 is a rest-length solve [CODE-dev]
Spec 6.8 lists "suspension force law (suspension vfunc 0x24 = 0x54bc70 / 0x54bac0)" as encrypted. In the dev
build the vtable `0x155a630` (the class with the stabilizer-bar fields, serializers `0x1011be0` / `0x1011ce0`)
has **slot 9 (+0x24) = `0x1012930`**, and the base `0x1559a20` slot 9 = `0x1001dd0`. Both compute
`EmptyLength (+0x84)` from `ForceAtRest +0x70`, `StiffnessBias +0x74`, `StiffnessAtRest +0x78` (k > 0 else 0):
- `0x1001dd0`: `|b| > 1e-5`: `x = (sqrt(k^2 + 4 k |b| F0) - k) / (2 k |b|)`, else `x = F0 / k`. Solves `k x (1 + b x) = F0`.
- `0x1012930` (car class): `m = k |b| / UpperStop (+0xcc)`; `x = (k - sqrt(k^2 + 4 m F0)) / (2 m)`, else `x = -F0 / k`
  (negative compression axis). I.e. the bias is **normalised by UpperStop** here.
- So 0x54bc70 / 0x54bac0 are most likely these two (the *rest length*, not the per-frame force) `[INF]`.
- `0x1002680` / `0x1002710`: force at `LowerStop` / `UpperStop` = `k x (1 + b x)` with `x = stop - EmptyLength`
  (stiffness factor clamped at 0), used by nothing readable (no callers / vtable refs).

## 4. Suspension: per-frame force `0x10122f0` [CODE-dev]
(called through wrappers `0x10001c0`, `0x1002050`; which class the cars instantiate is **not yet established**.)
- `x = Offset (+0x88) - EmptyLength (+0x84)`; `stiff = max(k (1 + bias x), 0)`; `spring = stiff x`. **No UpperStop
  normalisation here** (so it matches the port's `k x (1 + b x)`; it disagrees with the rest solve above - open).
- Damper `0x1012840`: `F = -c clamp(v, -V, V)`, `c = DampingArray[1]` (+0x64) if `v > 0` else `[0]` (+0x60),
  `v = +0x7c`, `V = +0x80`. **The damping velocity saturates at V.** `+0x80` is a transient written by the
  length update `0x1012130` (`0x7c` / `0x80` / `0x6c`); in the "no clamp" branch `0x80 = 0x6c = [0x1481d44]`.
  If bit 1 of `+0x90` is set and the owner state `[+0x5dc] != 3`, a different damper path (`0x1012800`, x 0.25) runs.
- Stabilizer bar (when the opposite wheel `+0xe0` exists): `+0xc0 = Offset_this - Offset_other`,
  `+0xbc = v_this - v_other`, `arb = (K (+0xb8) d0xc0 - C (+0xb4) d0xbc) x w` stored at `+0xb0`, added to the force;
  `w` = wheel vfunc `+0x40` (`0x1011460`), also scales the **total** force. Force vector = axis (+0xa0..+0xac) x total.
- `+0x6c MaxDamperForce` is **computed** each frame by `0x1012130` as `|m_body x (axis . ...)|`, not a static value.

## 4b. `0x1012130` decoded (the per-frame update) [CODE-dev]
Field names from the serializer `0x1011be0`: `+0xa0 GlobalNormal` (the axis, 4-vector), `+0xb0 StabilizerBarImpulse`,
`+0xb4 / +0xb8 StabilizerBarDamping / Stiffness`, `+0xbc StabilizerBarVelocity`, `+0xc0 StabilizerBarOffset`,
`+0xc4 PreviousOffset`, `+0xc8 LowerStop`, `+0xcc UpperStop`, `+0xd0 StopOffset`; `+0x7c Velocity`, `+0x88 Offset`.
`this+0x10` is the array of the two bodies (`0x10111d0` = array getter, body 0 = chassis); body `+0x150` = its state.
`0xfd1d30(state, r)` = **velocity of a body point** (linear `+0x80` + `r x w` with `w = +0x90..+0x98`).
- `axisVel = GlobalNormal . pointVelocity(GlobalAnchor)` in m/s.
- Update path (flag true): `Velocity (+0x7c) = (PreviousOffset - Offset) x Frequency`, Frequency = owner vfunc `0x44`
  (the retail spec's "vfunc 0x44 INF = Frequency" is confirmed). So **`v = -dOffset/dt`**.
- **`V (+0x80) = 2 |axisVel|`**, the damper's velocity limit; **`MaxDamperForce (+0x6c) = |-m_chassis (axis . v_COM) x Frequency|`**
  (the force that would stop the chassis along the axis in one step). `0x10122f0` does not read `+0x6c`;
  no consumer was found in this class (it is serialized only) - treat as a diagnostic.
- Flag false: `+0x7c = axisVel`, `+0x6c = +0x80 = [0x1481d44]` (no clamp).
- **Signs resolved:** damper `F = -c clamp(v, +-V) = +c clamp(dOffset/dt, +-V)` (same orientation as the port's
  `compressionRate`); stabilizer bar `K dOffset - C dv` = port's `K dx + C dv`. The port's signs are right.

## 5. Differences from `source/physics/vehicle_dynamics.cpp` (not changed yet)
| Item | Port | Dev build | Size of effect |
|---|---|---|---|
| spring | `k x (1 + b x)` (line 1288) | same | none - confirmed |
| damper | `c clamp(rate, +-0.5 m/s)` (`kMaxCompressionRate`) | `c clamp(rate, +-V)`, `V = 2 abs(axis . v_anchor)` | **`-devdamper` A/B: no effect on rollover** (section 7) |
| stabilizer bar | `(dx K + dv C)` on both wheels, only if both contact | `(K dx - C dv) x w`, `w` = wheel vfunc 0x40 | signs agree; `w` still unknown |
| rest compression | linear `F0/k` (line 311) | solves the bias (4 above) | small: bias x 0.01 per metre |
| `MaxDamperForce` | not modelled | per-frame momentum limit, **not read by the force function** | none found |

## 6. `w` resolved, and the Tire's SetNormalForce [CODE-dev]
- **`w` = dt, not a hidden factor.** `1011460` / `1011470` forward to `bodies[0]` vfuncs `+0x40` / `+0x44`; the car classes (4 vtables,
  e.g. `0x15580c0`) implement them as `fld [car+0x5a4]` / `fld [car+0x5a8]`. `0xff4480` sets `+0x5a8 = (int)[+0x5a0] x [+0x18]`
  (Frequency) and `+0x5a4 = 1 / +0x5a8` (**dt**). So the summed suspension force x `w` is just the impulse `F dt` handed to the solver
  (vfunc `+0x6c`, apply at `GlobalAnchor`), and the stabilizer bar's `+0xb0 StabilizerBarImpulse = arb x dt` is a record (the force
  that is summed is the un-scaled `arb`). The port's `force x h` is the same thing. Nothing to add.
- **Flagged damper path** (`flags +0x90` bit 1 and `[car+0x5dc] != 3`, only when the clamped `v < 0`): `F = 0.25 x Frequency x
  (-m x axis . v_body)` (`0x1012800` -> `0x10126b0`), a "cancel 25 % of the chassis axial motion per step" mode. The car class ctor
  `0x1011fe0` writes `+0x90 = 0`; only the constructors `0x1001a37` / `0x1001f71` OR bit 1 in. `[INF]` the cars use the normal
  `c clamp(v, +-V)` damper.
- **Tire SetNormalForce `0x1002a80`:** `Fz = (arg > T) ? 1e5 : arg` (`0x1499164` = 1e5 N), stores `NormalForce (+0x18)`, runs the
  Pacejka update `0x100cbc0`, then writes **effective friction coefficients**
  `LateralFriction (+0x44) = min(|LateralForce (+0x68) / Fz|, MaxLatFriction[surf] (+0x84 + 4 surf) x 0.001 x FrictionScale (+8))`,
  likewise `LongitudinalFriction (+0x48)` from `+0x6c` / `+0x90 + 4 surf` and `NormalFriction (+0x4c)` from `+0x74` / `+0xa8`.
  These are what the constraint solver would use as friction limits `[INF]`: the limit is `mu_eff x N_solver`, not
  `Pacejka(Fz_spring)` alone.
- Still open: the rest-length solve of the car class is normalised by `UpperStop` while the runtime spring is not (the Offset
  coordinate may be normalised; unresolved).

## 6b. Next (in order)
1. Find who reads Tire `+0x44 / +0x48 / +0x4c` (the solver) and with which normal force `N`: if the friction limit is `mu_eff x N`
   with `N` from the **constraint** (not the spring force), the port's tyre load path differs in exactly the place weight transfer
   is decided (the port's `kMaxTyreLoadFactor = 2` stand-in lives there). Candidates: `0x10012f0`, `0x100c2e0`, `0x100d370`.
2. Then prototype behind a flag and A/B with `-vehtest` (baseline: H3 / Q7_V12 / ML63_AMG 5 of 6 flip).

## 7. A/B: `-devdamper` (2026-11-14, `-vehtest`, flat plane)
H3 / Q7_V12 / ML63_AMG, 6 corner runs each, baseline vs `-devdamper`: **5 flips of 6 in every case, both ways**; H3 steer 0.25
@ 60 km/h lateral 12.29 -> 12.30 m/s2 (1.25 g both). The damper limit is real but is **not** the rollover lever
(it only changes the transient). Flag stays off by default. Command:
    build-mainin\opentdu2-main.exe -game C:/TDU2-Gamedata/TestDrive2 -userdata C:/temp/otdu2-win-userdata -gm FREERIDE -skipcine -no3d -nopad -fixedtimestep -car H3 -vehtest [-devdamper] -exitframe 5

## 8. The wheel pipeline and the contact solver (2026-11-14) [CODE-dev]
Wheel class vtable `0x1559b48`; update = slot 23 = **`0x10001c0`**; serializer `0xffd8a0` names the members:
`Tire (+0x270 data / +0x450)`, `ContactNormalConstraint (+0x4a0)`, `ContactTangentialConstraint (+0x510)`, `WheelJoint (+0x580)`,
suspension joint `+0x640` (= the class of section 4), `WheelHub (+0x830)`, contact frame matrix `+0x840..0x87c`
(lateral +0x840, normal +0x850, longitudinal +0x860), `ContactGlobalPose (+0x880)`, `ContactNormalImpulse (+0x890)`,
`ContactNormalImplicitImpulse` / `ContactTangentialImplicitImpulse (+0x8a0)`. Wheel vfuncs `+0x40` / `+0x44` forward to the car's
dt / Frequency (`0xffd880`, `0xffd890` -> `0xffc4c0` = owner car).
Per step (`0x10001c0`, then `0xffe7f0`):
1. `0x10122f0` suspension force (section 4).
2. `arb = K dOffset - C dv` (recomputed from the joint), `F_sd = Force - arb`;
   **tyre `NormalForce = max(0, n_contact . (axis F_sd dt)) x Frequency` = `F_sd (n . axis)`, clamped only at 1e5 N** (`0x1002a80`).
   The `+0x890..0x8ac` extras are added only when `[car+0x5dc] == 3` (a car state, meaning not decoded).
3. Tire Pacejka update (`0xfffa70`) with the contact-point velocity (`0xfd1d30` at `+0x4c0`).
4. `0xffe7f0`: **normal impulse `N n dt` -> `+0x890`; friction impulse `(lateral Fy + longitudinal Fx) dt` -> `+0x8a0`**
   (`Fy = tire +0x68`, `Fx = +0x6c`); both summed and applied by wheel vfunc `+0x6c(impulse, 2, point = wheel +0x4c0)`.
   `+0x4c0` is `ContactNormalConstraint +0x20` = the constraint's local anchor (the contact point) `[INF]`, the same lever as the port.
5. The Tire also exports effective friction limits `+0x44/+0x48/+0x4c = min(|F/Fz|, mu_max FS)`; their reader is **not** in the wheel
   code (no reads at wheel `+0x2b4..0x2bc` in `0xfe0000-0x1020000`), so it is the edPhys constraint solver `[INF]`.
   The tangential part is therefore a *constraint* (limit `mu_eff x N_solver`), with the Pacejka force as its implicit impulse.

**A/B (`-vehtest`, flat plane, 6 corner runs per car):** port flags `-devtyreload` (exclude the stabilizer bar and project on the
contact normal, per step 2) and `-devnocap` (drop the 2x static cap):
| | H3 | Q7_V12 | ML63_AMG | Enzo (control) |
|---|---|---|---|---|
| baseline | 5 flips, 1.25 g | 5, 1.22 | 5, 1.22 | 0, 1.77 |
| `-devtyreload` | 5, 1.25 | 5, 1.23 | 5, 1.22 | 0, 1.74 |
| `-devnocap` | 5, 1.25 | 5, 1.22 | 5, 1.22 | 0, 1.77 |
| both | 5, 1.25 | 5, 1.23 | 5, 1.22 | 0, 1.74 |
**None of the suspension law (section 4), the damper limit (7), the tyre load source or the load cap moves the rollover.** The wheel pipeline
the port already has matches the dev build in structure; the difference left is the **solver**: friction as a constraint limited by
`mu_eff x N_solver` and the contact normal constraint, versus the port's direct Pacejka force at `Fz = spring force`.

## 8b. The solver is not the lever; the wheel is a body [CODE-dev]
- The contact "constraints" (`ContactNormalConstraint +0x4a0`, `ContactTangentialConstraint +0x510`) are generic 1-D constraints
  (struct serializer `0x10111f0`: `GlobalAnchorArray +0x10`, `GlobalVector +0x30`, `MaxImpulse +0x40`, `MaxErrorImpulse +0x44`,
  `VelocityOffset +0x48`, `DistanceOffset +0x4c`, `Impulse +0x50`, `ErrorImpulse +0x54`).
- The tangential one (`0xfffb20`: direction = negated tangential contact-point velocity, `MaxImpulse ~ sqrt(a^2+b^2) x Force(+0x6a8) x dt`,
  scaled by `FrictionScale`) is built only when **car flag bit 19** is set. The car update `0xffbca0` sets bit 19 when
  **speed <= `TangentialConstraintSolverThreshold`** (a low-speed stabiliser; bits 15 / 17 are "stopped" / "< 1 m/s"). At 60-100 km/h the tyre
  force goes straight in as impulses (section 8). So the solver does **not** limit cornering grip at speed.
- **The wheel is its own rigid body** (motion state at wheel `+0x150`; `MaxContactGlobalNormalAngle +0x830`). `0x1002dd0` = SetContactNormal
  (clamps the angle, builds the contact frame at `+0x840/+0x850/+0x860`); `0xfff970` = SetContactPoint with `p = -n x LoadedRadius`
  (relative to the wheel body, i.e. from the hub), written to the anchors `+0x4c0` / `+0x530` and the global `+0x870`. The tyre impulses
  are applied (wheel vfunc `+0x6c`) **to the wheel body** at that point; the chassis receives them only through the suspension / wheel
  joint. `[INF]` the joint carries the force at the hub, not the contact patch.
- The port instead applied the lateral force at the ground contact and papered over the resulting tip-over with `kRollCentreFactor`
  (25 % shorter lever, `[INF]`).

**A/B (2026-11-14, `-vehtest`, flat plane, 6 corner runs per car), new flags `-devhubforce` (lateral + longitudinal at hub height =
contact point + wheel radius along the suspension axis) and `-devhublat` (lateral only):**
| car (DB mu) | baseline flips / max lat g / roll | `-devhubforce` | `-devhublat` |
|---|---|---|---|
| H3 (1.80) | 5 / 1.25 / 90 deg | 0 / 1.71 / 1.3 | **0 / 1.63 / 1.3** |
| Q7_V12 (1.90) | 5 / 1.22 / 90 | 0 / 1.81 / 1.5 | **0 / 1.74 / 1.5** |
| ML63_AMG (2.00) | 5 / 1.22 / 90 | 0 / 1.90 / 1.5 | **0 / 1.82 / 1.5** |
| Enzo (1.85-1.90) | 0 / 1.77 / 1.3 | 0 / 2.13 / 0.8 | 0 / 2.07 / 0.8 |
| California | 0 / 1.69 | - | 0 / 1.72 / 1.0 |
Whole roster `-vehcheck` (105 comparable cars): baseline mean 0-100 error vs DB +0.33 s (46 within 0.3 s); **`-devhubforce` worsens it to +0.44 s
(40 within)** because moving the *longitudinal* force changes the traction load transfer the port's drive model was calibrated with;
**`-devhublat` leaves the roster identical to baseline (70 / 137 within 0.3 s, same as before)**. So the lateral lever is the part that
matters for the rollover and it can be changed without touching the straight-line calibration. Lateral g stays below each car's DB friction.
Retail cross-check: the owner saw a Q7 SUV slide, not tip, on the 360 build (`docs/re/tdu1_physics_recon.md` 5.2).

## 9. Next
1. Decide whether `-devhublat` becomes the default (it removes all 27 baseline flips; sports-car cornering grip rises 3-17 %, still under
   the DB friction). The `kRollCentreFactor` fudge becomes dead code when it does.
2. The longitudinal force at the hub is architecturally right but needs the drive model re-calibrated (0-100 +0.11 s mean) - separate task.
3. Cornering *feel* check by the owner (DualSense) and an oracle number from the 360 build for peak lateral g.
4. Open: `[car+0x5dc] == 3`; the rest-length solve normalisation; the wheel joint constraint that carries the hub force (`WheelJoint +0x580`, `0x1004f90`).
