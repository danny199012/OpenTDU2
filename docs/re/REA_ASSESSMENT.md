# Can we use REA (morluto/rea) on TestDrive2.exe? — assessment

Written 2026-10-28. Question from the project owner: *"could we use
<https://github.com/morluto/rea> on the PC version of TDU2 to help fix a lot of
the open OpenTDU2 stuff — pause menu, physics, controller?"*

Tags used here: `[MEASURED]` = produced by the probes in this session, `[DOC]` =
from the REA project documentation, `[VERDICT]` = judgement.

---

## 1. What REA is, and what it is on Windows [DOC]

REA is an MCP server + CLI (MIT, npm `rea-agents`, latest **6.8.0**, 2026-10-25)
that connects an AI agent to reverse-engineering tooling: static analysis of
native binaries via **Ghidra / IDA / Hopper**, runtime capture and Frida
instrumentation, .NET, Electron/JS, APK, GPU code objects. The agent gets tools
such as `open_binary`, `list_procedures`, `analyze_function` (decompile),
`search_bytes`, `search_strings`, `read_bytes`, `inspect_native_load_image`,
`compare_functions`, and keeps an evidence bundle per session.

**The Windows boundary is narrower than the README suggests** and is what
decides this question:

| Item | Requirement [DOC, `docs/windows-ghidra-p0.md`] |
|---|---|
| Status | "experimental Windows x64 support for the read-only P0 boundary" |
| Host | Windows 10+ x64, fixed local NTFS |
| Node | `^22.23.4 \|\| ^24.22.1 \|\| >=26.12.1` (23/25 unsupported) |
| Analysis engine | **operator-installed Ghidra 12.x (>= 12.1, verified 12.1.4)** + 64-bit JDK 21+ |
| Admitted targets | PE **x86, x86-64, ARMNT, ARM64** EXE/DLL, or raw NMOS 6502 |
| Operations | **26 read-only** (inventory, memory, function analysis). No GUI state, no annotation writes, no persistent DB mutation |
| Excluded on Windows | "other target architectures and formats … remain unavailable through this provider" |
| 32-bit note | "Native x86 (32-bit) PE support is available on repository main; npm releases through 5.0.0 admit x86-64 PE targets on Windows" |

REA installs **nothing** for you: no Ghidra, no Java, no Node. It also states it
does not decrypt anything, and that its licence covers only analysis of software
you own or are authorised to examine.

## 2. Local prerequisites — two are missing [MEASURED]

| Requirement | This machine | OK? |
|---|---|---|
| Node `^22.23.4 \|\| ^24.22.1 \|\| >=26.12.1` | **v24.15.0** | **NO** — below 24.22.1, must upgrade |
| 64-bit JDK 21+ | **Java 26.0.2** | yes |
| Ghidra 12.x installed | not installed (only a stray `C:\Users\cuz_c\Ghidra Projects\vulkan-1.dll.gzf`) | **NO** |
| Linux/macOS host (needed for non-PE targets, see §6) | no WSL (`wsl --status` → not installed) | no |

## 3. The target, measured [MEASURED]

`C:\Program Files (x86)\Steam\steamapps\common\Test Drive Unlimited 2\TestDrive2.exe`

| Field | Value |
|---|---|
| Size | 21,056,512 B (20.08 MiB) |
| Machine | **0x014C — x86, 32-bit PE32**, GUI subsystem, image base 0x400000 |
| TimeDateStamp | 2012-07-10 15:10:44 UTC (the v097 patch era) |
| Sections | 10: `.text` `.rdata` `.data` `.rsrc` **`Stext` `Sitext` `Srdata` `Sdata` `Sidata` `.securom`** |
| Entry point | RVA 0x1AF2B60 → VA 0x1EF2B60, **inside `Sitext`** (SecuROM's own stub, not the game) |
| Debug directory | 1 × type 2 (CodeView): `RSDS` GUID `33deec31-c61a-3e41-b837-4f76e0f8bbe7` age 46, path **`D:\Eden-Prog\Games\Testdrive2\Branches\MC_PC\Final\PC\TestDrive2_PC_Retail.pdb`** |
| Rich header | absent |
| Import directory | **only 520 bytes / 25 DLLs** — d3d9, DSOUND, WS2_32, MSVCR90, WINMM, dbghelp, VERSION, IPHLPAPI, **XINPUT1_3**, SHLWAPI, CrashHook, CRYPT32, imagehlp, WINTRUST, KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, ole32, OLEAUT32, d3dx9_40, **DINPUT8**, COMCTL32, WSOCK32 |

Two consequences for any static tool, REA included: there is **no PDB and no
symbol table** (only the RSDS record proving a `MC_PC` build), and the import
graph is nearly empty because SecuROM rebuilds the IAT in memory — so a fresh
Ghidra import starts with very few named API calls.

## 4. Where the code actually is: the entropy map [MEASURED]

64 KiB block entropy over the raw sections (`#` > 7.9, `+` > 7.0, `.` ≤ 7.0):

```
.text  VA 0x0401000  #+###########################################+..................
       VA 0x0801000  ................................................................
       VA 0x0C01000  ....................................
Stext  VA 0x14D6000  ################################################################
       VA 0x18D6000  ################+
Sdata  VA 0x1F7E000  #################
.securom VA 0x22C9000  .+++++++++++++.++.++......
```

* **`.text` VA 0x401000 – 0x6D1000 (2.81 MiB) is entropy ≈ 8.0 → SecuROM-encrypted
  at rest.** This matches `vehicle_physics_spec.md`'s definition of `[ENC]`
  ("in the SecuROM range, VA < 0x6c1000") exactly, and every `[ENC]` address in
  that spec (0x46xxxx, 0x53xxxx, 0x54xxxx) lands inside this band.
* `.text` **0x6D1000 – 0xE407AB is normal entropy → readable.** This is where all
  the code OpenTDU2 *has* decompiled lives: `FUN_00865750` (brake/ABS/TC),
  `FUN_00869c30` (tyre update), `FUN_008866a0` (gearbox), `FUN_0088a410`
  (engine), `FUN_00bdb970` (car factory), `FUN_00bd…`, `FUN_00c5…`.
* `Stext` (5.29 MiB raw / 10.59 MiB virtual) and `Sdata` (1.10 MiB raw) are
  entropy 8.0 end to end — SecuROM's packed payload.
* Readable bytes in total: **12,957,696 of 21,056,512 (≈ 62 %)**.

**So the wall is real, and it is ~2.8 MiB of the first third of `.text`.** REA
does not decrypt SecuROM; nothing static does. Everything OpenTDU2 has marked
`[ENC]` (233 occurrences across `docs/re/*.md`) stays `[ENC]` for any static
tool.

## 5. What *is* readable, and how it lines up with the open issues [MEASURED]

The strings that matter are in the readable 62 %. This is the part REA can work
on, and it is a lot more than expected.

### 5.1 Physics

The **entire Havok `hkpVehicle` RTTI family** is present, including its source
paths (`.\World\hkpWorld.cpp`, `.\Dispatch\hkpCollisionDispatcher.cpp`,
`.\Memory\hkThreadMemory.cpp`):

```
hkpVehicleInstance, hkpVehicleData, hkpVehicleDataWheelComponentParams,
hkpVehicleDefaultSuspension (+WheelSpringSuspensionParameters),
hkpVehicleDefaultEngine, hkpVehicleDefaultTransmission, hkpVehicleDefaultBrake
(+WheelBrakingProperties), hkpVehicleDefaultSteering, hkpVehicleDefaultAerodynamics,
hkpVehicleDefaultVelocityDamper, hkpVehicleDefaultAnalogDriverInput,
hkpVehicleRaycastWheelCollide, hkpVehicleWheelCollide,
hkpVehicleFrictionDescription, hkpVehicleDriverInput(+AnalogStatus),
hkpTyremarksInfo, hkpTyremarksWheel
```

> **Correction to §1.1 of `vehicle_physics_spec.md`.** That section says "not the
> Havok vehicle SDK" because the cars are driven by Eden's `edVehicle`. The
> Havok vehicle classes are nevertheless *linked into the binary* (53 `hkpVehicle`
> hits). Whether they are live is now a decidable question, and it matters:
> `hkpVehicleFrictionDescription` is Havok's load-sensitive tyre-friction model —
> precisely the missing piece named in STATE.md ("the missing load sensitivity of
> the Pacejka `D`"). Either it is used (and answers the open item outright) or it
> is dead code (and closes the question for good).

Game-side data field names, all readable — these are the knobs of the open
handling items:

| Group | Names found |
|---|---|
| Tyres | `PacejkaConfig`, `Tires_Physics`, `Tires_Type`, `Tires_Thickness`, `Tire_Radius`, `Max_Lateral/Longitudinal/Normal_Friction_Front/Rear`, `LateralFriction`, `LongitudinalFriction`, `NormalFriction`, `MaxLatFriction`, `MaxLngFriction`, `MaxNrmFriction`, `FrictionScale`, `WetFrictionScale`, `WET_Friction_Percent` |
| Suspension | `Suspension_Rate_Front/_Rear` (+`_Bias`), `Suspension_Length_Front`, `Suspension_Lenght_Rear` (sic), `currentSuspensionLength`, `CrushedSuspensionFX` |
| Brakes / aids | `Brakes_Power_Front/Rear`, `Brakes_Dim_Front/Rear`, `Brakes_Characteristics_*Localized`, `Braking_Friction_Percent_Front/Rear`, `Dynamic_Brake_Balance_Coeff`, `BrakingFrictionScale`, `Brake_Balance`, `BrakeLinearity`, `CurveHandBrakeSlipRatio`, `Hand_Brake_Slip_Ratio` |
| Drive train | `Engine_Brake_Nm`, `Gearbox_Inertia`, `Gearbox_Damping`, `ClutchLinearity`, `Differential_Type/Value_Front/Rear/Central`, `FrontDifferential`, `RearDifferential` |
| Steering / FFB | `SteeringControl`, `SteeringControlImpulse`, `SteeringDamping`, `SteeringLinearity`, `CommandFilterSteeringSetup`, `HasPowerSteering`, `FeedbackFrictionCoeff*`, `FeedbackFrictionMaxVelocity` |
| Off-road | `Offroad_DIRT/GRASS_{Lateral,Longitudinal,Normal}_Friction_Percent`, `Offroad_*_Tires_Type` |

Also present: `Aerodynamic(s)`, `GearBox`, `Clutch`, `SteeringAidState`,
`DBCarPhysicsData`, `PhysVehicle`, `ArrayPhysVehicles`, `GSVehicleSound`,
`edVehicle`, `edLiveInterface`, and gameplay diagnostics strings
("Vehicle has encountered an invisible collision on the road", "Vehicle is
jumping", "Vehicle is falling under the sea level…").

### 5.2 Flash / menus (the pause menu's native half)

Retail's Flash runtime is Eden's own: `.\Sources\edFlashLib.cpp`, `edFlash`,
`edFlashBankLoader`, `edFlashListScrollbar`, `FlashMovieList`,
`FlashRender_edRender`, `FlashWidgets`, `FlashKeyboard_Win32`,
`MngFlashCounter/DashBoard/Localize/RenderTarget`, `SCFlashScreen*`,
`ASFlashList`, plus `[Flash]` / `[Movie]` log tags and the VMF version line.

And the decisive artefact: **1835 `/:` global variable names** — the complete
contract between the exe and the SWFs, e.g. `/:IsInOptions`, `/:PauseBack`,
`/:PauseQuit`, `/:nb_options`, `/:num_option`, `/:option_name`, `/:LockOptions`,
`/:WhatGameMode`, `/:WhatPlatform`, `/:IsFlashOK`, `/:mode3d`, `/:resolution`,
`/:skipCine`, `/:vibration`, `/:mouse_sensitivity`, `/:input_key_01..04`, plus the
`/pause_menu/…` clip paths and `/adv_controls/dpad`.

### 5.3 Input / controller

`XINPUT1_3.dll` + `DINPUT8.dll` (static imports), `edDeviceX360PadPC`,
`MngDPadMenu`, `IsX360PadIconsUsed`, `#bitmap:x360_DPad_Up/Down/Left/Right#`,
`X360 Pad [%d] has been connected/disconnected`, `minPedalInputToBlock`,
`wheelsTimeSinceMaxPedalInput`, `InputAxisAngle`, `InputAxisVelocity`,
`DriverInput`, `CollisionInput`.

## 6. The better target than the PC exe: the PS3 build [MEASURED]

`C:\TDU2-PS3-Recomp\game\EBOOT.ELF` (already decrypted by this machine's own
`tools/self_decrypt.py`) is the same game with **no SecuROM**:

| Field | Value |
|---|---|
| Size | 18,144,136 B, `ELF64`, big-endian, `e_machine` 21 (PPC64), `e_type` EXEC |
| Sections | 33, names stripped, **no `.symtab`** |
| Code+rodata | section [2] PROGBITS at 0x10230, **15,657,212 B — fully readable** |
| Strings | **the same ones**: 1698 `/:` globals, `hkpVehicle` ×53, `hkpVehicleFrictionDescription`, `Pacejka`, `edFlashLib`, `edVehicle`, `Suspension_Rate_Front`, `Max_Lateral_Friction_Front`, `Tires_Physics`, `WhatGameMode`, `/pause_menu/…` |
| Caveat | it is a **different revision**: 1698 vs 1835 `/:` names, and some fields are spelled differently (`FeedbackFrictionCoeffAtStop` on PS3 vs `FeedbackFrictionCoefficientAtStop` on PC) |

Function boundaries are known too: `port\recompiled\` holds 53,107 lifted
functions and a guest→host `function_table[]` (`tools/which_func.py` maps an
address to its function).

**This is the way around the 2.8 MiB encrypted band** — decompile the PPC64 code
where the PC build is opaque, match by string xref, port the C++.

**But REA on Windows cannot do it**: PE-only admission (§1). That route needs
REA on a Linux/macOS host (WSL is not installed here) or plain Ghidra headless,
which the project already uses (`/tmp/claude-0/*/gh` + `dec.sh`).

### 6.1 Does the broken PS3 *runtime* matter? No. [MEASURED]

The PS3 recomp currently "renders nothing", which is worth stating precisely
because it does **not** affect the static route above. From `NOTES.md` §5 and
`_run4.log` / `_run4.err`:

* it gets all the way through SDK init — `cellSpurs` (5 SPUs), `cellAudio`,
  `cellVideoOut` at 1920x1080, `_cellGcmInitBody`, `SetFlipMode`, FlipThread,
  the RSX FIFO walked, and **D3D12 really executes draws** (8 `draw_arrays` +
  8 `draw_indexed`, textures bound, shaders set, render target / blend / depth
  state programmed, 1280x720 window open);
* then it **deadlocks**: the watchdog shows all 12 guest threads parked in
  `sys_lwcond_wait` (`0x2A6D9D51`) at the 45 s and 110 s samples, with a thread
  spinning on `event_queue_receive(q=2, timeout=0)`.

So it is not a rendering failure — it is a **stall in the ps3recomp runtime/HLE
layer**. Three consequences:

1. **Static analysis is unaffected.** Ghidra reads `EBOOT.ELF` off disk. Whether
   the game boots, deadlocks or draws is irrelevant to decompiling its 15.6 MB
   of PPU code + rodata. The deadlock is in the *host* runtime, not in Eden's
   code.
2. **Verification of a ported law does not need the PS3 build either.** The
   reference is retail PC TDU2 (the Steam install, which runs) plus OpenTDU2's
   own instrumentation (`-vehtest`, `-logdrive`, `-logcorner`, `-logpause`).
3. **If PS3 runtime observation is ever wanted, use RPCS3, not the recomp.** It
   is already on this machine
   (`E:\Downloads\rpcs3-v0.0.37-18154-29108e1c_win64_msvc\rpcs3.exe`, saves and
   storage under `E:\Emulation\`), and `PS3_ROUTE.md` records that RPCS3 runs
   TDU2 well. The recomp's purpose is the online/port endgame, not observation.

**Bonus the deadlock did not spoil:** the failed recomp still produced the two
artefacts the static route wants, both derived from the ELF alone —

* `port\out\EBOOT.functions.json` (1.98 MB) — the stage-1 ppu_loader result:
  24,005 OPD descriptors → **19,842 unique function boundaries**;
* `port\recompiled\*\function_table[]` — 53,119 lifted functions with
  guest→host addresses (`tools/which_func.py` queries it).

A stripped ELF with no `.symtab` is the worst case for a decompiler's
auto-analysis. Feeding those boundaries into Ghidra up front (a script defining
functions from the JSON before analysis) removes most of the guesswork — and
that is available today, independently of REA.

**One honest caveat:** the PS3 build is a different revision and a different
compiler, so this route yields *semantics to reimplement*, not code to paste.
The `/:` name drift (1698 vs 1835) is the visible symptom; inlining and
instruction selection differ throughout.

## 7. Verdict per pain point [VERDICT]

| Area | Fit | Why |
|---|---|---|
| **Physics** | **Good, partly** | The tyre/axle/chassis code (`FUN_0086xxxx`, `FUN_0088xxxx`) and every data field name are readable → the open items (rollover, Pacejka `D` load sensitivity, brake balance, ABS/TC level) are within reach. The `edVehicle` *sub*-functions at 0x46xxxx–0x54xxxx are encrypted → those stay `[ENC]` unless the PS3 route is used. |
| **Controller** | **Good** | Small surface, all in readable sections: XInput/DInput, pad class, deadzone/pedal thresholds, FFB coefficients, `/:vibration`, `/adv_controls/dpad`. |
| **Pause menu** | **Partial, and not where you think** | The pause menu is SWF data + **OpenTDU2's own AVM1** (`source/flash/flash_player.cpp`). REA cannot see OpenTDU2's source, so it cannot fix the ActionScript/rendering bugs. What it *can* do is hand over the retail contract — the 1835 `/:` names and the native function that writes each (`/:IsInOptions`, `/:num_option`, `/:nb_options`, the `/:WhatGameMode` gate, the entry locks) — i.e. the game-side wiring that STATE.md still lists as missing. |

**Overall:** worth doing, with a clear-eyed scope. It is a **workflow upgrade,
not a new capability**: the project already decompiles with Ghidra
(`/tmp/claude-0/*/gh` + `dec.sh` + scratch `.c`). REA's marginal value is
orchestration — a session that retains evidence, resolves xrefs, recovers structs
and searches bytes, driven by the agent instead of by hand — across a corpus of
226 `[INF]` and 233 `[ENC]` markers.

What it will **not** do: decrypt SecuROM, read OpenTDU2's own source, or fix
SWF-internal layout.

## 8. Suggested plan

**Stage 0 — decide the scope.** Take physics + controller first (readable, bounded,
high value); treat the pause menu as "the native `/:` contract" only.

**Stage 1 — install (no project changes).**
1. Upgrade Node to ≥ 24.22.1.
2. Install Ghidra 12.1.4 (operator step; REA will not).
3. `npx rea-agents@latest setup` → register the Cline MCP; `rea doctor` for readiness.
4. Verify **32-bit x86 PE admission** before anything else (npm 6.8.0 should
   include it — the doc only guarantees it on `main`; test with a small x86 PE
   first, e.g. `Readme.exe`).

**Stage 2 — first import.** Point it at a **copy** of `TestDrive2.exe` (never the
Steam install). Expect the import to be slow and RAM-heavy: 10.7 MiB of `.text`
with 2.8 MiB of entropy-8.0 garbage. Do not run whole-program auto-analysis first;
instead drive it by the addresses OpenTDU2 already knows:
`FUN_00869c30`, `FUN_00865750`, `FUN_008866a0`, `FUN_0088a410`, `FUN_00bdb970`,
vtable 0xf39e80.

**Stage 3 — the four concrete questions worth asking it.**
1. Is `hkpVehicleFrictionDescription` / `hkpVehicleDefaultSuspension` live? Find
   the vtables and their xrefs from `edVehicle` → settles §5.1 and possibly the
   load-sensitivity item outright.
2. `search_strings "PacejkaConfig"` → xref → read the curve evaluation and the
   `D` term's load dependence.
3. `search_strings "FeedbackFrictionCoeff"` → the FFB curve constants.
4. `search_strings "/:IsInOptions"` and `"/:num_option"` → the writers → the
   pause-menu options contract.

**Stage 4 — the PS3 route (separate, higher payoff).** For the 2.8 MiB band,
decompile `EBOOT.ELF` (PPC64) — plain Ghidra headless now, or REA on a Linux host
(install WSL) — and match functions by the shared strings. This is the only path
that turns the remaining `[ENC]` markers into readable code.

## 9. Open questions for the owner

1. **SecuROM policy.** REA supports Frida-based runtime instrumentation, which
   could read the decrypted code *in memory*. The README says the encrypted part
   "is never decrypted or bypassed". Attaching to a running process is not
   decryption, but it is adjacent — is that in scope, or out? (Recommendation:
   keep it out; use the PS3 build instead, which is clean.)
2. **Which revision** is the PS3 build relative to the PC one (v097)? The `/:`
   name drift (1698 vs 1835) needs pinning before trusting a port.
3. Whether to run REA on Windows only (PE) or set up WSL for the ELF/PPC64 lane.
