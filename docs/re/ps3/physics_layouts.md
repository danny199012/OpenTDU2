# edVehicle2 layouts recovered from the PS3 build (serializer evidence)

Eden serializes every class member by name (`write(name, value)`), so the
serializer functions double as struct-layout dumps. Each `(name, struct
offset, type)` triple below was read out of the lifted serializer function
named in the "Evidence" column (`python tools/get_func.py <addr>` in
`C:\TDU2-PS3-Recomp`), not inferred. Names resolve to strings in the shared
pool at `0xFB0000-0xFC7000`; pool addresses are given for re-verification.

## Engine (edVehicle2 engine member run)

Evidence: serializer `func_000D25E0` (materializes `PedalPosition`
`0xfb2fb8`; neighbours walk `ElapsedTime 0xfb2e88`, `StartingTime 0xfb2e98`,
`StallingTime 0xfb2f98`, `LimiterTime 0xfb2fa8`, `Position 0xfb2fc8`,
`IdlePosition 0xfb2fd8` via `+ (0x2E88)`-style offsets from the `0xFB0000`
base). `StarterState 0xfb2f88` serializes as an array.

Members (serialized order):

- `StarterState` (array)
- `ElapsedTime` (float)
- `StartingTime` (float)
- `StallingTime` (float)
- `LimiterTime` (float)
- `PedalPosition` (float)
- `Position` (float)
- `IdlePosition` (float)

This matches the PC spec's `[ENC]` engine state machine
(`vehicle_physics_spec.md`: "Position clamped to [IdlePosition, MaxPosition]",
stall detection via `FUN_00546910`, limiter timing) — the PS3 build has the
same class with all members visible.

## Gearbox / output shaft

Evidence: vehicle serializer `func_000D0544` (materializes `OutputShaft`,
`GearRatio 0xfb2f60`, `SqrGearRatioArray`); `GearBox` serializer
`func_000CFEE4`.

Members seen in the pool run `0xfb2e80-0xfb2f88`:

- `State`, `ElapsedTime`, `StartingTime`, `StoppingTime` (shared engine run)
- `ShaftEnd`, `Type`, `Impulse`, `Inertia`, `InvInertia`, `Velocity`
- `ShiftTimeDown 0xfb2f00`, `ShiftTimeUp 0xfb2f10`, `ReverseTime 0xfb2f20`,
  `DriveTime 0xfb2f30`, `MinGearRatio 0xfb2f40`, `MaxGearRatio 0xfb2f50`,
  `GearRatio 0xfb2f60`, `ShiftedGearRatio 0xfb2f70`
- `StarterState 0xfb2f88`, `StallingTime`, `LimiterTime`, `PedalPosition`,
  `Position`, `IdlePosition`

This matches the PC spec's gearbox `[ENC]` chain (shift completion
`FUN_00546f00`, reverse detection `FUN_0053fbd0`, drive re-engage).

## Wheel contact / extra-gravity helper

Evidence: serializer `func_000B5FD0` (read in full; accessor pairs decoded
below). Accessors: `func_00C8BEE0` = open object, `func_00C8B908` = write
named int, `func_00C8B758` = write named float, `func_00C8BAB8` = null-object.

| Struct offset | Name (pool addr) | Type |
|---|---|---|
| +0x04 | (`AckermanRatio` run start) | int |
| +0x10 | `ContactConstraintSolverThreshold` (0xfb2de0) | float |
| +0x14 | `ExtraGravityMin` (0xfb2e08) | float |
| +0x18 | `ExtraGravityMax` (0xfb2e18) | float |
| +0x1C | `ExtraGravityAccelDuration` (0xfb2e28) | float |
| +0x20 | `ExtraGravityAccelDelay` (0xfb2e48) | float |
| +0x24 | `NoWheelOnGroundElapsedTime` (0xfb2e60) | float |
| +0x28 | `State` (0xfb2e80) | int |
| +0x2C | `ElapsedTime` (0xfb2e88) | float |
| +0x30 | `StartingTime` (0xfb2e98) | float |
| +0x34 | `StoppingTime` (0xfb2ea8) | float |

## CarControl variants

Serializers: `RearWheelDriveCarControl` `func_000CBC00` / `func_000D18C8`,
`FrontWheelDriveCarControl` `func_000CBCC8` / `func_000D1990`,
`FourWheelDriveCarControl` `func_000CBD90` / `func_000D1A58` (two functions
per class: write + read paths). `AckermanRatio 0xfb2d18`, `NormalVelocity
0xfb2d28`, `NormalAngle 0xfb2d38`, `Default 0xfb2d48` sit at the head of the
pool run.

## Vehicle-level members (vehicle serializer `func_000D0544`)

Names at `0xfb3450`, `0xfb3530`, `0xfb3568` (vehicle-level member objects at
struct offsets +0x10, +0xC0, +0xE0 in the serializer's caller frame) plus
array members (`GearRatio`, `SqrGearRatioArray`, `DriveTrainFrequencyArray`
serializer `func_000D103C`).

## Car setup / tuning database fields (851 names)

`physics_names.txt` in this folder lists the full run at `0xf144d0`
(`_abs_slip_ratio_hypersport`, `_antirollbar_damping_front`,
`_brake_balance`, `_camber_front`, `_damper_rate_front`,
`_differential_type_*`, ...). These are the retail car-setup field names the
tuning shop and the physics database use — cross-check against what
`source/physics/` currently reads from the `.b1`/database entries; every name
not consumed today is a candidate for the physics port.

## The PacejkaConfig string

`PacejkaConfig 0xfb2dd0` sits between the Havok version strings
(`edVehicle2 0xfb2d90`, build date/time, `PS3`) and
`ContactConstraintSolverThreshold` — i.e. it is an `edVehicle2` member name.
Its only direct code xrefs found so far aim at the interior address
`0xfb2e08` (pool runs are referenced by base+offset), from serializer
`func_000B5FD0` and `func_000CF7A8`. The Pacejka *usage* (the curve
evaluation with load sensitivity — the open physics item) lives in the
edVehicle2 tyre update, to be located among the functions near the tyre/suspension
serializers (`func_000BEE70`, `func_000C78AC` — `WheelSpringJoint`,
`func_000CB3CC` — `WheelHub`).

## The vehicle update dispatcher (confirmed)

`func_000D4F3C` (6,564 bytes, largest function in the vehicle neighbourhood)
is the edVehicle2 update walk: it iterates the component list (a counted loop
on `ctr`), loads each component's vtable (`vm_read32(obj+0)`), picks the
update slot (`vm_read32(vtable+4)`) and dispatches it through
`ps3_indirect_call` — 86 such virtual accesses in one function. The engine /
gearbox / wheel updates are the callees behind those slots; to name one,
follow one component pointer from the serializer-paired constructors, or
match the state-machine shapes against the PC spec's `[ENC]` descriptions.
