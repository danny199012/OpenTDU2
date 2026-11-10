# Wheel force feedback — the port's law, TDU1-informed

Tags: `[CODE]` from the readable exe / our port, `[DATA]` game files, `[INF]`
chosen / inferred, `[ENC]` in the SecuROM band.

The owner's ask (2026-11-10, after GitHub issue #3: "TDU2 never had a proper
implementation for force feedback on wheels"): build FFB. TDU2's own FFB
device layer is [ENC] (the device services sit in the encrypted band, nothing
readable to cite), so the reference is TDU1's — which OpenTDU1 has in the
clear but **unlicensed** (`docs/re/opentdu1_reuse.md` §2.11): its variable
names and *shape* are documented there; **no code was copied** — the
composition below is ours, written against our own `VehicleDynamics` state.

## 1. What TDU1's device layer documents (the shape, not the code)

`opentdu1_reuse.md` §2.11 [CODE from the unlicensed repo, read-only]: the
torque is composed from piecewise curves — an overall factor, a
load-from-longitudinal-G curve, a slip-angle curve pair, a speed-factor
curve — plus an impact rumble, a slide rumble and an invert flag, applied to
the device as a constant effect + a damper through SDL haptics. The decay
rumbles ("kick") exist there too. That structure is what port follows:

| TDU1 device-layer concept | OpenTDU2 law here |
|---|---|
| overall factor (`FFB_FACTOR` config) | `-ffbgain <0.1..4>` (default 1.0) |
| invert flag (`bFFBInvert`) | `-ffbinvert` |
| constant torque on the steering axis | SDL `SDL_HAPTIC_CONSTANT`, `SDL_HAPTIC_INFINITY`, level ±0x7fff x gain |
| speed damper | SDL `SDL_HAPTIC_DAMPER` condition, coefficient = speed norm |
| impact / slide rumble | `WheelFFB::kick(strength)` — a decaying overlay on the torque (0.18 s) |
| device out (SDL haptic, constant + damper) | the same (SDL3, `SDL_OpenHapticFromJoystick` off the gamepad's joystick — 3.4.20 has no haptic-from-gamepad opener) |

The curve *values* (the tuning tables) are neither in the readable TDU2 exe
nor portable from TDU1's car set, so the composition uses fixed feel constants
below `[INF: chosen for feel, A/B-able with the flags]`. A config-curve
machinery (per-input piecewise tables like TDU1's `gFFBSpeedFactor[4][2]`) is
a later tuning pass if the owner wants that depth.

## 2. The composition (ours, from our physics)

Fed per frame from `GMFreeRide`'s pad-extras block (the same block that
drives `GamepadOutput`), composed from `VehicleDynamics` per-wheel state:

```
speedNorm = clamp(speedKmh / 180, 0, 1)
for each front wheel in contact:
    slipFalloff = 1 - 0.75 * saturate((|SlipAngle| - OptimalSlipAngle) / (2 * OptimalSlipAngle))
    latSum  += LateralForce * slipFalloff        // the rim "goes light" past the grip peak
    maxLat  += Load * SurfaceFriction            // the grip actually on offer
    gripSum += SurfaceFriction
speedGate = saturate((speedKmh - 2) / 8)         // no fight at a standstill
satTerm    = latSum / max(maxLat, 2000 N)        // self-aligning torque
centerTerm = clamp(steerAngle / 0.6 rad, -1, 1) * speedNorm * 0.35 * axleGrip
Torque = clamp((satTerm + centerTerm) * speedGate, -1, 1)
Damper = speedNorm
```

* `OptimalSlipAngle` is the car's own tyre data (`DBCarPhysicsData` ->
  `PacejkaConfig` +0xb4, ~0.055 rad; `vehicle.getSetup().TyreFront`) — the
  falloff is what makes the wheel light when the fronts slide (understeer
  washout, a spin) instead of pinning at full torque `[INF]`.
* The normalization by `Load x SurfaceFriction` is what makes the pull weaker
  on grass / dirt automatically, and saturate near the tyres' friction
  ellipse `[INF]`.
* The centreing term (0.35 weight, scaled by the axle's mean surface grip)
  is the "wheel wants to go straight at speed" pull `[INF]`.
* Sign: torque + = pull to the right. `LateralForce` is the tyre-frame force
  (`+` towards the wheel's +x); if a device maps the axis mirrored, flip the
  feel with `-ffbinvert` (TDU1 shipped the same escape hatch).
* The kick rides on the constant torque in the direction the force already
  pulls (a slide crash jolts the way the wheel is already loaded).
* Parked / menus / cinematics (`bActive` false): torque and damper go to 0 —
  the wheel is left free. `shutdown()` (exit, option off, unplug) stops the
  effects; the destructor never leaves the wheel torqued.

## 3. Device attach and gating

`source/input/wheel_ffb.{h,cpp}` — a `WheelFFB` beside `GMFreeRide`'s
`GamepadOutput`:

* Attaches to whatever gamepad `GSInput` has open **if its joystick exposes
  an SDL haptic device**. That is the wheel gate: DirectInput wheels do,
  XInput pads and DualSense (plain rumble) do not — a pad user sees nothing
  and no code path runs.
* Two effects are created once and updated in place: the constant (level =
  the torque) and the damper (coefficient = the speed). Updates are sent on
  a change (torque step 0.004 of full scale, damper step 0.02) plus a 0.25 s
  heartbeat — the device smooths, a full-rate stream is not needed.
* Unplugged / replaced wheel: dropped like `GamepadOutput` does its pad;
  the next `attach()` picks the new device up.

## 4. Flags, options, logs

| | |
|---|---|
| `-noffb` | FFB off (also the `options.ini` `ffb` row / the debug panel checkbox "wheel force feedback") |
| `-ffbgain <f>` | overall torque gain, clamped [0.1, 4] |
| `-ffbinvert` | flip the torque direction (the device-side escape hatch) |
| `-logffb` | attach line + a ~2 Hz state line (printed device or not — the headless evidence): `FFB: [attached\|no device], torque %+.3f, damper %.2f[, kick][, parked]` |

Attach: `FFB: '<device>' (id N) attached (constant yes, damper yes)`. A pad
without haptics logs once: `FFB: the pad has no haptic device (...) - no
wheel force feedback`. Option turned off while attached: `FFB: off`.

The `ffb` row lands in `options.ini` next to `dualsense` (the same
`GameOptions` field + save-table + debug-panel checkbox pattern).

## 5. Evidence (what a headless run proves and what it cannot)

The force is not a pixel, so the A/B evidence is the log law, not a
screenshot: `-logffb` on a headless autoplay drive prints the composition
line while the car rolls (the physics feed works end to end) and the no
-device attach line proves the gate. The feel itself needs the owner's
wheel: expect the rim to fight the turn and lighten when the fronts let go,
the damper to firm up with speed, a jolt on a crash, nothing in menus. If
the pull reads backwards on a given device, `-ffbinvert`.

## 6. Not done / next

* Config curves (TDU1's per-input piecewise tables) instead of the fixed
  feel constants — a tuning pass, only if the owner wants the depth.
* TDU1's impact/slide *rumble motors* path: wheels without LEFTRIGHT get the
  torque overlay kick only (ours).
* FFB in the other drive modes (GM_TestCar): only FREERIDE feeds a wheel
  today; the class is game-mode-agnostic and lifts out in one include + one
  update call.
* The retail TDU2 FFB (what the original shipped, [ENC]) stays unknown —
  this is the port's own feel, deliberately.
