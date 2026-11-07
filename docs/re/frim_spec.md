# FRIM (Free-Ride Instant Money) - spec

Port: `source/game/freeride/risk_driving.{h,cpp}` (the laws), `freeride_hud.cpp` (`/HUD/FRG`), `traffic.cpp`
(near misses). Session 44 (2026-10-29) replaced the [INF] numbers of session 43 with the game's tuning file and
the decoded laws below.

## 1. The tuning file - `scenarics/frpoints.cpr` [DATA]

Encrypted XML (`LoadCPRText`). Every FRIM number of the game:

| Tag | Values | Use |
|---|---|---|
| `CALCUL_PTS Value` | 1.0 | scale of every point gain |
| `DEC_SPEED val1..4 / pts1..4` | 0 / 0.4 / 0.7 / 1.0 -> -30 / 0 / 3 / 8 | gauge points per second vs speed (x the car's top speed) |
| `PA_TARGET lvl0..10` | 0 100 200 350 500 750 1000 1500 2000 2500 4000 | $ banked at each level |
| `PA_TARGET2 lvl0..10` | 0 200 350 600 950 1500 2200 3500 5000 7500 10000 | the second prize table |
| `PP_TARGET lvl0..10` | 0 300 350 400 500 600 700 800 900 1000 1200 | gauge points from level n-1 to n |
| `PTS_CHAIN palier1..8` | 2 5 10 15 0 ... | chain steps (not used by the port yet) |
| `ESQUIVE` (near miss) | `DIST` 2.3 -> 0 m = 70 -> 150 pts; `DIFF_SPEED` 30 -> 200 km/h = x1 -> x1.5; `SPEED` 90 | |
| `SAUT` (jump) | `HAUTEUR` 0.5 -> 4 m = 10 -> 100; `LONGUEUR` 5 -> 60 m = 20 -> 300; `SPEED` 30 | |
| `DERAPAGE` (drift) | `DIST` 5 -> 50 m = 30 -> 230; `SPEED` 25; `SLIP_ANGLE` 0.4 rad | |
| `WHEELING` | as DERAPAGE, `MIN_LEAN` 0.1 | bikes (not in the port) |

The PS3 build copies it verbatim [CODE: PS3 `0x7c9eb0`]: 9 floats (`CALCUL_PTS` + `DEC_SPEED`) to globals
`0x14ddbe0` / `0x14ddb58..`, then three 11-float tables to the FRIM object: `PA_TARGET` -> +0x1b0,
`PA_TARGET2` -> +0x1dc, `PP_TARGET` -> +0x20c. On the PC the copy is in the encrypted band; the readable PC
code uses the same object offsets.

## 2. The laws [CODE]

FRIM object (PC singleton `[0xfd92d8]`): +0x240 dt, +0x244 speed (vehicle +0x1f0 x 3.6), +0x254 gauge, +0x258
level, +0x25c session total, +0x260 best level, +0x264 bank available, +0x208 -> the active prize table.

- **Rate** (PC `0xa767c0` -> `0xa75920`; PS3 `0x7cae18`): gauge += rate(speed) x dt; rate is piecewise linear
  between `val_i x vehicle+0x254` (the car's top speed; port: `_max_theorical_speed` [INF: field]) and `pts_i`.
- **Tricks** (PC `0xa752fc`): a trick's points are added to the gauge (below level 10).
- **Level up** (PC `0xa769e0`): while gauge >= `PP_TARGET[level+1]`: gauge -= it, level + 1; a level above the
  best so far sets the best and the bank prompt (`fr_gge/bank` "on", +0x264 = 1); level 10 sets
  `/:FRIMLevel10` and the gauge stops.
- **Level down** (PC `0xa76ae4`): gauge < 0, level > 0, no bank pending -> level - 1, gauge +=
  `PP_TARGET[level+1]`; at level 0 the gauge is clamped to 0 and the HUD plays "end".
- **Bank** (PC `0xa76e50`, FSCommand `frgp:bank`): total += `table[level]`, `AddMoney(amount, 0, 0x14)`, best
  bank stat, then the reset `0xa767f0` (gauge, level, best level = 0). `frgp:end_bank` clears +0x264;
  `frgp:end_chain` ends the trick combo object (+0x14), not the money.
- **Hit** (PC `0xa76bd0`): a contact of type != 0 / 3 plays the HUD "hit" and a 2 s state (0xeddb40). No level,
  gauge or money is lost.
- **Prize table** (reset `0xa767f0`): `PA_TARGET2` when an encrypted selector (`0x445780`, counted against a
  0x28-byte table) gives >= 12, else `PA_TARGET` [ENC]. The port always uses `PA_TARGET` [INF].

## 3. HUD variables - PS3 `0x7c8d24` [CODE]

Level L: `/:PALevel` = L + 1 (L <= 9) or 10; `/:PerfPts` = gauge or 1 (L = 10); `/:PerfPtsTarget` =
`PP_TARGET[L+1]` or 1; `/:PACurLevel` = `table[L]`; `/:PANextLevel` = `table[L+1]` or 0; `/:PATotal` = total.

## 4. Port specifics [INF]

- Jump: height = peak above the take-off point, length = take-off to landing (flat); the two scores are added.
- Drift: body slip angle (flat velocity vs heading) >= `SLIP_ANGLE`; the distance is scored when the slide ends.
- Near miss: the traffic detector counts gaps below 2.3 m (was 1.6 m) and reports the gap.
- The player's A banks (`requestBank` / the movie's `frgp:bank`).

## 5. Test

`-riskselftest` (FREERIDE): 40 s at 240 km/h with a near miss every 2 s -> level 5, bank $750; a crash keeps
the level; 20 s at 30 km/h drains the gauge (-21 /s) and drops a level.
