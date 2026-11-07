# What the TDU2 modding community says about physics — mapped to this port

Source: a Discord thread (`Xarlith`, `Beeefy`, `.galopin`, 2026-10-30) shown to the
agent as a screenshot. Their work is a **data mod**: `physics.cpr` + per-car
statistics, on top of the retail exe. Nothing in it is code we can copy — but it is
a very useful map of *where the levers are*, and it confirms the two gaps this port
already has on its own list.

## 1. The claims, as made

- *"It's all still the same engine in the exe. Our changes are a mixture of the
  physics.cpr and then all of the vehicle statistics have been reworked to ensure
  they fit the edited physics and reflect their real world values."*
- *"I understand the original values on some vehicles Eden had implemented were
  incorrect so the vehicles didn't reflect their true handling in game."*
- *"The kicker with any recomp is you'd have to decompile and reimplement the Havok
  physics engine side of it, which isn't an easy task."*
- *"Our changes initially restricted to real-world accurate car data (torque,
  torque curves, BHP, measured top speed, braking and acceleration), for the
  in-game stats to be more meaningful."*
- *"I started research towards the Pacejka formulas with a goal to eliminate
  snap-oversteer, which ended with a big success. This was followed by removal of
  traction control in Hardcore mode, tweaking angular damping, and finally
  adjusting the controller settings so the speed-factor slider lets you achieve a
  full-lock turn even at high speeds (hardcore = no TC + full steering angle at any
  speed)."*
- *"We also tried to reduce flipping by tweaking physics.cpr, but that issue isn't
  completely solved yet."*
- *"Some physics variables aren't configurable in physics.cpr but are stored in the
  binary, which could yield easy gains, but SecuROM protects the binary, so I
  couldn't modify them."*

## 2. Claim by claim, against this port

| their claim | our state |
|---|---|
| "same engine, `physics.cpr` + vehicle statistics" | exactly the port's inputs: `Physics.cpr [Car]` and `DBCarPhysicsData` (`vehicle_physics_spec.md` §2.1/§2.2, `source/physics/vehicle_data.*`). Their *reworked values* are a mod we do not have. |
| "Eden's original values were wrong" | the port reads Eden's values verbatim (`gs_database.cpp findCarPhysics`), so it reproduces retail's handling by design. Fidelity vs. "true handling" is a decision, not a bug — except where a *law* is missing (next rows). |
| "you'd have to reimplement the Havok physics side" | not our problem: TDU2 does **not** drive its cars with the Havok vehicle SDK (spec §1.1) — `edVehicle` is Eden's own library, and Havok is only the chassis collision (`.dhk`). |
| "Pacejka formulas → killed snap-oversteer" | the port already implements the TDU2 Pacejka variant (spec §4.2). The open item is the **load sensitivity of `D`** (STATE.md physics item 4; `REA_ASSESSMENT.md` §5.1) — the same target they describe. |
| "removed traction control in Hardcore mode" | the port has `bTractionControl`, the TC slip-ratio curve and `AidLevel` (0 SECURE / 1 SPORT / 2 HYPERSPORT), but **the runtime aid switch is not read**: `-aids sport\|hypersport` is load-time only (`vehicle_data.cpp:408`) and `AidLevel` ships 0. "Hardcore" is unreachable. |
| "tweaking angular damping" | the three damping curves are parsed (`SecureAngularDamping` / `Sport` / `HyperSport`) but `CurveAngularDamping = SecureAngularDamping` for **every** car at build time (`vehicle_data.cpp:442`) — again the aid level. |
| "controller settings / the speed-factor slider, full lock at speed" | the steering curves (`SteeringMaxAngle`, `SteeringSensitivity`) are read from `Physics.cpr` and applied in `vehicle_dynamics.cpp:439-442`, but the **Controls options page is unanswered** in the port (`CLAUDE.md` item 2), so no player slider reaches them. |
| "some variables are in the binary, SecuROM blocks it" | **our advantage**: the same code is in the clear in the PS3 EBOOT (`CROSSBUILD_PLAN.md`), and the `[ENC]` list of spec §6.8 (suspension force law, solver, the ABS/TC predicates `FUN_0054eae0` / `FUN_0054eaa0`) is exactly that set — `docs/re/ps3/ps3_enc_twins.json` lists 69 twins. |

## 3. The gap they name that we can close cheapest: the aid level ("Hardcore")

The machinery is all in the port already — `VehicleSetup::AidLevel`,
`bAbs` / `bTractionControl`, `AbsSlipRatio` / `TractionSlipRatio`,
`AngularDamping` / `OffRoadAngularDamping`, `SteeringMaxAngle` /
`SteeringSensitivity` — but every one of them is frozen at the **SECURE** column.
Spec §8 lists it as an open point: *"Aid level of the player (game option, game car
+0x40) and the TC curve replacement per level."*

Leads, all readable (no encrypted band needed):

- `/:DrivingAssistance` — the frontend variable of the option screen
  (`docs/re/ps3/flash_globals.txt:264`): `pc_addr` 15648480, **`pc_site` 14038609
  (`0xD63BD1`) inside the readable band**, 3 sites, PS3 twin at `func_0021C4EC`
  (dumped as `_g_drivingassist.cpp`). That is the option the player sets.
- the PC aid tables are readable (`CROSSBUILD_PLAN.md` §7): `ABSCONTROLACTIVATION`
  `0xd6cb61`, `ABS_Slip_Ratio_HYPERSPORT` `0xe18b16`, `_abs_slip_ratio_sport`.
- the per-car columns are already parsed: `_abs_slip_ratio_secure/sport/hypersport`
  and `_traction_control_slip_ratio_secure/sport/hypersport` (`vehicle_data.cpp:414`).
- the game car's `+0x40` carries the level and the ABS curve's high-speed point is
  replaced from it (spec §6.6, tail of `FUN_00bacd50`).
- the modder's own definition is the acceptance test: *hardcore = no TC + full
  steering angle at any speed* → the level must switch **TC + the damping curve +
  the steering curves together**, not just TC.

## 4. What we should not take from the thread

- **Their values.** It is a mod we do not have; the port's rule is to decode from
  the game's own data.
- **"Eden's values were incorrect" as a bug list.** For this port that is a
  fidelity decision (reproduce retail) — the actionable part is only the missing
  *law* (load sensitivity), which is already item 1 on the handling list.
- **Their cures as proof.** "Snap-oversteer" should first be *reproduced and
  measured* here (`-vehtest` / `-logcorner`, spec §7.1) before any value is
  changed on their word.

## 5. Ordered next steps

1. **Aid level** (§3) — smallest real win: it unlocks "Hardcore" (no TC), the
   Sport/HyperSport damping curves and the steering change in one move.
2. **Pacejka `D` load sensitivity** — the port's own #1 handling item
   (`CLAUDE.md` physics item 4), the same target the thread names; start from the
   readable PC aid/Pacejka tables and the PS3 twins.
3. **Steering / the Controls page** — wire the options page (`CLAUDE.md` item 2) so
   the player's steering setting reaches `SteeringMaxAngle` / `SteeringSensitivity`.
4. **Fuel** — the thread's "Gas Station reimplementation": see
   `docs/re/fuel_gas_station_spec.md` (it is a data feature, not an exe patch).

