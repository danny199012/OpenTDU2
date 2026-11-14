# Car damage (decoded from the Apr 2012 dev prototype)

Tags: `[CODE-dev]` read from `TestDrive2DEV.exe` (see `dev_build_physics.md` for how it is read), `[DATA]` the retail game
files, `[INF]` inferred, `[ENC]` unreadable in retail. Addresses are dev-build VAs. The module is `DisplayVehicleDamage.cpp`.

## 1. Data [DATA]
`DBCarDamageData` (11 rows, `_id _param _value`) in `db_data.cpr` (`-dumpdbtable DBCarDamageData`):

| id | _param | value | use |
|---|---|---|---|
| 0 / 1 / 2 | `ShiftCar` / `ShiftFixedObject` / `ShiftMovingObject` | 0 | added to the scaled magnitude |
| 3 | `IntervalScratch` | 25000 | damage >= this: **scratched** |
| 4 | `IntervalDeform` | 40000 | **deformed** |
| 5 | `IntervalBreak` | 65535 | **broken** (also the 16-bit ceiling) |
| 6 / 7 / 8 | `ScaleCar` / `ScaleFixedObject` / `ScaleMovingObject` | 0.01 / 0.025 / 0.0025 | magnitude multiplier by what was hit |
| 9 | `LateralDamageFactor` | 1500 | lateral rule threshold |
| 10 | `LateralIntervalBreak` | 65535 | lateral break level |

Shaders in the retail data: `Euro/Shaders/TD_CarDeform_TS.sob` (vertex deformation, tangent space) and `TD_damagemap.sob` (writes the damage map).

## 2. Logic [CODE-dev]
- **Loader `0x751810`:** for i in 0..10 reads the DB row named by the name table `0x16619d8` (`_param`) and stores `_value` (float, row `+0x20`)
  into the damage object at `this + 0xfc + 4 i`; sets `this+0xf8 = 1`. So Shift = `+0xfc/+0x100/+0x104`, intervals `+0x108/+0x10c/+0x110`,
  scales `+0x114/+0x118/+0x11c`, lateral `+0x120/+0x124`. The same 11 names are debug-menu tweakers (`Damage State`, `Damage Controller`,
  `Full Damage ON/OFF`).
- **Per frame (`0x754eb0` loop):** the car owns a list of **impact records** (0x70 bytes, count at `owner+0x374`). A record whose
  magnitude is below **50** (double at `0x148e450`) is skipped; otherwise `AddImpact` runs and a result below 50 is skipped too; else vfunc `+4`
  of the damage object applies it.
- **`AddImpact 0x751ba0(record, &kindOut, ...)`:** `kind = record+0`; `0` -> no damage; `4` -> no damage; `1` (car) and `3` (moving) add the other
  object's weight-like value to the magnitude (`record+0x14`); `dmg = Scale[kind] x magnitude + Shift[kind]` (indices `0x110 + 4 kind` /
  `0xf8 + 4 kind`: kind 1 car, 2 fixed object, 3 moving object); `dmg <= 0` -> none. **Lateral rule:** if `dmg < LateralDamageFactor (1500)` and
  the hit zone is `0xf` or `0x10` (the two sides), the damage goes to a separate u16 and is checked against `LateralIntervalBreak`
  (break = state 3). Otherwise the part's u16 damage += `dmg` (setter `0x4407d7`, saturating at 65535) and `dmg` is returned.
- **State `0x751a50`:** `damage >= IntervalBreak -> 3`, `>= IntervalDeform -> 2`, `>= IntervalScratch -> 1`, else `0`
  (each interval is first clamped to a u16).
- **Visual side `0x753140 / 0x75319f`:** each car gets a render target `DamageMap%i` and a mini scene `DAMAGESCN` with passes `CarDmg_rl` and
  `CarDmg_Clear` and materials `CarDmgMat` / `CarDmgMatClear`. Impacts are painted into the map (`TD_damagemap`); the car shader
  `TD_CarDeform_TS` displaces vertices from it. `CrushedSuspensionFX` (`0x14dfb9c`) is the broken-suspension effect; `Smashable`
  (`0x14d541c`) marks breakable props; the six impact-type flags at `0x1668270-75` (car / road / fixed / dynamic ...) are a debug overlay that
  nothing in this build sets.
- **Not found:** how the record magnitude is produced (a Havok contact value), the hit-zone codes beyond `0xf / 0x10`, and the damage-map UV
  mapping of an impact point.

## 3. In the port (since 2026-11-14)
`source/physics/vehicle_data.{h,cpp}` `DamageParams` / `GetDamageParams()` (the 11 rows by `_param`, + the 50 minimum);
`vehicle_dynamics.{h,cpp}`: `damage` (0..65535), `getDamage()`, `getDamageState()`, `addImpactDamage( magnitude, kind )`.
`resolveChassisCollision` feeds every world contact as `kind = fixed object` with **magnitude = impulse / dt** `[INF]`:
the retail record magnitude is not decoded; this scale was chosen so that the DB constants behave plausibly (Enzo-class, 1500 kg:
~30 km/h wall hit -> scratched, ~50 -> deformed, ~100 -> broken; repeated scraping accumulates). The log shows state changes:
`Vehicle damage: +32634 -> 32634 (scratched), speed after impact 25 km/h`. **Nothing is drawn yet** and the lateral rule, car-car and
moving-object kinds (traffic) are not fed.

Verified: `-gm FREERIDE ... -autoplay "60:hold=accel;300:hold=steerleft;..." -exitframe 1300` -> scratched -> deformed -> broken in one crash sequence; a gentle
scrape run reaches 25059 by accumulation. No visual check (headless `-no3d`).

## 3b. Visible damage and the traffic feed (2026-11-14, same day)
- **Glass:** `CinematicCar::applyDamageState( state )` (called every frame by `gm_freeride.cpp` after `applyCarDirt`): from state 2 (deformed)
  the exterior `Glass*` materials (CarPaint family) bind `glass_b`, the retail starred-pane art of common_car, in their `DETAIL2` slot (the shader
  fades the base toward it, `traffic_spec.md`), and `g_i` (clean base) below. Log: `CinematicCar: damage state 2 -> cracked glass (5 slot(s))`.
  Evidence: `docs/images/damage_cracked_glass_windscreen.png` (windscreen after a ~100 km/h crash: bullet-hole star and scratch web).
- **Traffic:** a rising-edge player-traffic crash (the same event that feeds the police violation) calls `addImpactDamage( 0.5 m x 1.3 x speed / dt, 0 )`
  (kind 0 = `ScaleCar`) `[INF]` - compiled, **not yet exercised in play** (a test drive hit only scenery).
- Calibration seen: ~90 km/h scenery hit = +28,580 (scratched) in one impact; ~100 km/h crash sequences reach broken.

## 4. Next
1. Exercise the traffic feed in play; the lateral rule for side hits; AI-vs-AI impacts.
2. Remaining visuals: `DamageMap` render target + `TD_damagemap` splat + `TD_CarDeform_TS` on the car body (scratches at state 1, deformation at 2, the wreck look at 3), `CrushedSuspensionFX`. The glass crack is done.
3. Decode the record magnitude: the Havok contact listener that fills the records (the vehicle's `HavokVehicle.cpp` module).
