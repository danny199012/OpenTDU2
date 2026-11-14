# The game clock — day length and time of day (dev exe, readable)

Answer to the main session's brief (docs/state/main.md, "6 game clock"): the
initial premise does not survive the disassembly — `FORCED_OFFLINE_DAYLENGTH`
never touches `0x16867b4` and there is no index→seconds table anywhere. The real
day length is `SETDAYDURATION`'s qword: **10800 s (3 real hours per game day)**,
and the time of day is **derived from the OS wall clock, mod the day length** —
it is not stored in the save. Every claim below is from `TestDrive2DEV.exe`
(`tools/devbuild_disasm.py`; its `.text` is the unencrypted one). Tags:
[CODE-dev] read in the dev exe, [DATA] read from game files, [INF] chosen or
inferred, [ENC] SecuROM band of the retail exe.

## 1. The clock commands — corrected handler map [CODE-dev]

Registered through `0xf30ee0(name, {handlerThunk, 0, edi, ebx}, nargs=2)`; the
int arg arrives in the command context at `+0xe4`. One block per command, in
order:

| Command | Handler | Effect |
|---|---|---|
| `ENABLE_WEATHER_DEBUG_INPUTS` | 0x518390 | the weather-debug flags (the dumpers at 0x99b8e3 / 0x9c24eb / 0x9c3aeb / 0x9c692c print the weather table names) |
| `SUNENABLED` | 0x5154b0 | byte `0x1b24bf4` = (arg != 0) |
| `FORCED_OFFLINE_WEATHER` | 0x5154d0 | `[0x19e629c] = arg`, `[0x19e6208] = 0` |
| `FORCED_OFFLINE_DAYLENGTH` | 0x5154f0 | `[0x19e62a0] = arg`, `[0x19e621c] = 0`, `[0x19e6208] = 0` |
| `SETDAYDURATION` | 0x516900 → 0x533b70 | int64 qword `[0x164ec30/34] = arg` (sign-extended) |
| `SETTIMER` (string arg) | 0x512070 | `"REAL_TIME"` clears bit0, `"FIXED_TIME"` sets bit0 of byte `[System+0x8c]+0x4b`; logs `SystemSetTimer : %s` |

The brief's `0x16867b4` (init 4) is **`NBGRASSBITMAPS`** (its setter
0x515510), and `0x16867b8` (init 4) is **`NBBUSHBITMAPS`** (0x515530) — grass
render counts fed into the grass/environment setup at 0xc657a0 (which also
passes them with the 32.0/96.0/650 values into the `TallGrassBlock` /
`TGBlock_NoShad` object). Not clock state.

- `FORCED_OFFLINE_DAYLENGTH`'s value at `0x19e62a0` has **no reader** in the
  dev exe [CODE-dev] — the numeric argument is dead here. Its neighbours
  (`0x19e629c` = FORCED_OFFLINE_WEATHER, same) are only read through the
  weather debug dump: the flag byte `0x19e621c` gates a dump that prints
  `weather_name_table[eax*4 + 0x1664290]` (`0x1664290` = "Sunny, Cloudy,
  Rainny, Before_rain, After_rain, Tempest, Max_intempery"). So the "offline
  forced" pair belongs to the online weather/daylength sync (likely applied
  from the server in retail code, [ENC]/[INF]); it is not the offline clock.
- The weather-name table settles `FORCED_OFFLINE_WEATHER`'s init value: 4 =
  "After_rain" [INF].

## 2. The SystemTimer (edSys2) — where the clock actually lives [CODE-dev]

Singleton at `0x1905ae0` (BSS; ctor 0x533d30), vtable `0x1494214`
(base `0x14802a8`). Fields:

| Field | Meaning |
|---|---|
| `+0x18/+0x1C` (int64) | elapsed = `_ftime64` epoch **minus the ctor anchor** (the anchor int64 kept at `0x1986e38/3C`) |
| `+0x20/+0x24` (int64) | raw `_ftime64` epoch seconds |
| `+0x28` (float) | sub-second fraction = `millitm / 1000.0` |
| `+0x2C` (byte) | tick enable (1 = ctor default); the debug command at 0xc1ab0f toggles it → freezing the clock |

- The tick (vtable slot 4 = 0x533db0): `if (+0x2C) { _ftime64(&tb); +0x20 =
  tb.time; +0x28 = tb.millitm/1000; +0x18 = tb.time - anchor }` — i.e. the
  clock source is the **OS wall clock** (MSVCR90 `_ftime64`), and elapsed is
  anchored at the ctor's `_mktime64({tm_mon=0, tm_mday=1, tm_year=107})` =
  **2007-01-01 00:00 local time**.
- Day length qword `[0x164ec30/34]` = seconds per game day. Initial value in
  .data: **10800** (= 3 h). `SETDAYDURATION <seconds>` overwrites it (no
  range clamp seen).

## 3. The math [CODE-dev]

- `GetHMS(this, elapsed)` (0x533860, via `_alldvrm` 0x10daac0):
  `rem = elapsed mod day_dur`; `frac = (rem + subsecond) / day_dur`;
  `h = floor(frac*24)`, `m = floor(frac(h)*60)`, `s = ...` (ftol at 0x10da900;
  constants 24.0 @0x1488bb0, 60.0 @0x147f408). `GetHour` (0x533ba0) and
  `GetMinutes` (0x533bd0) return its outs, feeding it their own `+0x18`.
- `SetFromHMS(h, m, s)` (0x533c00):
  `elapsed = h*(dur/24) + m*(dur/1440) + s*(dur/86400)` (constants 24.0 /
  1440.0 @0x1481610 / 86400.0 @0x1494240) written back into `+0x18/+0x1C` —
  a jump to a time of day expressed in the day's own duration.
- So: **time of day = ((epoch − mktime(2007-01-01)) mod 10800) / 10800 × 24 h**
  with the default duration — 7.5 real minutes per game hour, 8 game days per
  real day, the phase locked to the wall clock (same real moment = same game
  time on every machine, modulo the local-time anchor).

## 4. Consumers [CODE-dev]

- **HUD clock string** 0x4687dc: formats a 32-char buffer from the timer
  (GetHour/GetMinutes path via 0x416abd) — the `hh:mm` the dashboard shows.
- **`day_time` Flash var**: registered at 0xcea440 (GFx, string at 0x150bf80);
  0xa955c0 feeds the Flash value from the timer's elapsed double. UI binding
  only.
- **Session bookkeeping** (0xa3a990 / 0xa3a9d0 / 0xa3b340 / 0xa3b4d0 /
  0xa3c500 ...): stores the raw epoch (`0x445e58` → 0x533ab0 returns
  `+0x20/+0x24`) as online session start/duration stamps.
- **Debug hour commands** 0xc1aa89-0xc1ac78: hour +1 / hour +N / minute
  (GetHour → SetFromHMS via 0x4293ed/0x441529), plus the freeze toggle.
- **Online gate** 0x46d759: checks the timer object among many others before
  some online step.
- Nothing in the dev exe's weather/sun code reads the SystemTimer — the sun
  positions come from the weather manager's own state. Whether retail's sun
  phase is exactly this wall-clock formula is [INF] until a retail screenshot
  at a known wall time is compared (the port's `WeatherCycle` currently
  mirrors the PC clock's own time of day 1:1 — retail scales it instead).

## 5. Is the time of day saved? — No [DATA]

- The save's `PlayerData` schema (dumped with `otdu2_bnk_tool xmbdump` from
  the owner's unencrypted 360 `PLAYERSAVE/DATA`,
  `C:/Users/cuz_c/Documents/tdu2/00090000002B21AD/49470804/00000001/`) has no
  clock field: `Environment CEnvironment` (+268) is `{ Variable[] Variables }`
  and this save holds exactly one variable, `RnkChkC = "13"` (a rank check).
  No `TimeOfDay` / `Hour` / weather-time name anywhere in the schema.
- Consistent with the model: the clock is a pure function of the wall clock,
  so saving it is unnecessary (and `SETTIMER REAL_TIME` is the natural
  default). The PC `DATA` is encrypted [ENC] so a second sample is not cheap;
  the 360 schema is the dev-era schema (the dev exe's own string table has the
  same `Variables`/`Environment`/`TimeBeforeRespawn` names around 0x10e2328).

## 6. Answers to the brief, and what the port should do

1. **Index → seconds**: no such table exists. `FORCED_OFFLINE_DAYLENGTH` is an
   online-sync setting whose value is unread in the dev exe; the real knob is
   `SETDAYDURATION` with default **10800 s**.
2. **Where the time of day is saved**: nowhere — it is derived from the OS
   clock (§5).
3. **Does the clock run in offline free roam**: yes, by construction — it is
   always a function of the wall clock (§2/§3); nothing mode-gates it. The
   only freeze path is the timer's `+0x2C` tick enable (debug).
4. **`REAL_TIME`**: the `SETTIMER` argument (vs `FIXED_TIME`) — bit0 of the
   edSys2 System settings byte `+0x4b` [CODE-dev]; its reader is not in the
   dev exe [INF: online/retail side]. The working offline mechanism the
   debug menu uses is SetFromHMS + the freeze toggle.

Port (`WeatherCycle`, currently the PC clock's own time of day): switch to the
retail formula — `phase = (time(NULL) − mktime(2007-01-01 local)) mod
day_duration`, `hours = phase / day_duration * 24`, `day_duration = 10800`
(overridable, e.g. `-dayduration <s>`), with `-clock hh.mm` acting as
FIXED_TIME (frozen phase). The 1:1 mirror can stay as an opt-in
(`-realtimeclock`) if wanted. [INF]
