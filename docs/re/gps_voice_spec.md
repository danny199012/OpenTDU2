# GPS voice (sat-nav) — spec

The spoken sat-nav guidance of free ride. Everything here is in the **readable**
half of `TestDrive2.exe` (VAs above `0x6D1000`) or in the game data. Port:
`source/audio/gps_voice.{h,cpp}` (the player), `source/game/freeride/gps_guidance.{h,cpp}`
(the cue laws), `MapScreen::getRouteTurns` / `projectOnRoute` (the route side).

## 1. The bank [DATA]

`Euro/Bnk/sound/voices/<lang>/gps.bnk` (`us` 1.18 MB; also `fr ge it ja sp`).
17 RIFF WAVs `VFGPSVoice001_.wav` .. `017_`, MS-ADPCM (format 2), 44.1 kHz mono,
1.0–3.5 s. **The samples carry no names**; the cue is the sample's rank. The
lines below are a speech-to-text transcription of the US bank (faster-whisper
`small.en` on the decoded PCM) [DATA, transcribed]:

| cue | sample | line |
|---|---|---|
| 0 | 001 | "You are driving in the wrong direction. Turn around when possible." |
| 1 | 002 | "At the next intersection, turn right." |
| 2 | 003 | "Turn right." |
| 3 | 004 | "At the next intersection, turn left." |
| 4 | 005 | "Turn left." |
| 5..9 | 006..010 | "Your destination is 500 meters / 1 km / 1.5 km / 2 km / 3 km away." |
| 10..14 | 011..015 | "Your destination is half a mile / 1 / 1.5 / 2 / 3 miles away." |
| 15 | 016 | "You have reached your destination." |
| 16 | 017 | "Recalculating." |

The transcription agrees with the code (§3): the cue numbers the code picks for
each threshold are exactly the matching lines.

## 2. The player: `GPSVoice` (FlashSounds.cpp) [CODE]

- Strings: `"%sSound/Voices/%s/GPS.bnk"` 0xee6d5c (loader at 0xd85781),
  `"VoiceGPS"` 0xee6d78 (the audiosetup client; bfx client index 5 of the table
  at 0xf90ac0), `"GPSVoice::PlaySoundA() in ...FlashSounds.cpp"` 0xee6cf8.
- `GPSVoice::PlaySound` = **FUN_00d84950** (`eax` = this, `ecx` = cue): needs the
  bank loaded (`[this+0x184] == 2`) and the source idle (`[src+0xd4] == 0`);
  sample table at `this+0x188`, 12 bytes per cue. Volume 1.0 (0xedf668). If the
  source is busy the cue is **dropped** and logged
  `"[GPS] Can't play GPS sound (state was:%s) (asked:%d) (sample:%s)"` 0xee6cb0.
- `FUN_00933df0` (`ecx` = cue, `edx` = GPS manager) routes a cue: with a
  passenger (`[mgr+0x1cfc]` and `[mgr+0x1d00]`) it goes to the passenger's
  "GPS Live" voice (`FUN_009fae80`, hostess banks `voices/<lang>/hs/*.bnk`) via
  the byte map 0x933ef8 `{0,1,2,3,4,5,8,8,8,8,5,8,8,8,8,6,7}`; otherwise to
  `GPSVoice::PlaySound`. Game states `[[0x1025608]+0x9ca0]` 1 and 2 mute it.
  **Port: no passenger, the GPS voice always speaks** [INF].

## 3. The laws (GPS manager, 0x933xxx–0x937xxx) [CODE]

Manager fields: `+0x1cdc` previous route length left, `+0x1ce0` previous
distance to the junction, `+0x1ce4` wrong-way timer, `+0x1cf4` near gate
(**50 m**, set from 0xef01ec in FUN_00933d80 / 0x9336d0), `+0x1cf8` / `+0x1cf9`
said-flags, `+0x84` wrong-way time, `+0x88` wrong-way said.

**The crossing test** (same shape everywhere): the value went from above the
gate to at/below it this frame, and `|value - gate|` is inside a window — 50 m
for the countdown, the previous distance for the turns. A jump across a gate (a
teleport, a new route) stays silent.

### Turn cues — FUN_00934740 (`angle`, `distance`)

Called from FUN_009362d0 (0x9363f1) when the next link of the route belongs to
**another road** (the two road ids of FUN_00d3e710 differ, 0x9363bb) and the
junction is within `[+0x1cf0]` (**200 m**, 0xef0240). `angle` = heading of the
link out − heading of the link **back** to where the route comes from, wrapped
to 0..360 (FUN_00935fd0): **90 = right, 180 = straight on, 270 = left**;
`distance` = metres to the junction. So the voice speaks only for bends of
about 60 deg or more (a < 121: right, a > 241: left).

- crossing **50 m** → `angle < 121` (0xf4aa70): cue 2 "Turn right";
  `angle > 241` (0xf4aa68): cue 4 "Turn left"; 121..241: silent.
- else, `[+0x1cf8] == 0` and crossing **200 m** (0xf4a258) → cue 1 / cue 3
  "At the next intersection, turn right / left" (same bands).

### HUD turn arrow — FUN_00935fd0 → 0x937693..0x9377cc

FUN_00935fd0 classifies the next junction into an arrow type (→ `[mgr+0xb8]`),
with `a` as above and, over **all** the links `j` of the node (`r_j` = their
angle in the same convention, the link back is 0):
`near` = links within 91 deg of the link taken; bands
A = r in (44, 91), B = (89, 136), C = (224, 271), D = (269, 316).

| a | arrow |
|---|---|
| < 61 | 3 rightdown |
| 61..121 | a < 89 and B: 3 rightdown; a > 91 and A: 1 rightup; else 2 right |
| 121..151 | a ≤ 149 or near > 1: 1 rightup; else 0 up |
| 151..209 | 0 up |
| 209..241 | a < 211 and near ≤ 1: 0 up; else 7 leftup |
| 241..299 | a < 269 and D: 7 leftup; a > 271 and C: 5 leftdown; else 6 left |
| ≥ 299 | 5 leftdown |

(constants 0xf4b2d0..0xf4b31c, 0xf4ac18, 0xf4aa68/70). **+8 when the node kind
`[node+0xa] == 3`** (roundabout) — in the file that is the `EDON` byte +10: 11
Ibiza nodes of 4–7 links in the towns carry it [DATA]. The type indexes the
string table **0xf75fd8**: `up rightup right rightdown down leftdown left
leftup`, the same 8 with `_ra`, and `empty` (16) — exactly the frame labels of
the HUD movie's `/HUD/arrow_gps/arrow` [DATA].

The update (0x937664..): the arrow is the next junction's type while it is
within `[+0x1cf0]` = 200 m, else 16; GPS off (`[mgr+0xb4] == 0`) → 16. On a
change: `/:IsRoundAbout` = type 8..15, `/:ArrowDirection` = the string, then
`HUD/arrow_gps` plays **`close`** (to empty), **`open`** (from empty) or
**`change`** [CODE]. (Mode `[0xfe5028] == 2` only allows `down` / `empty`.)
Port: `MapScreen::RouteTurn::Arrow`, `GpsGuidance::getArrow`,
`FreeRideHud::showGpsArrow` (`docs/images/port-gps-arrow-up-s44.png`,
`port-gps-arrow-left-s44.png`).

### Destination countdown — FUN_00934a10 (`remaining`)

Called at 0x937595 with the route length left. Units: `[[0x10017f0]+0x14] == 1`
is imperial: the length is multiplied by **0.621504** (0xf4a200) and compared to
the same thresholds (thousandths of a mile).

| gate | metric cue | imperial cue |
|---|---|---|
| 3000 (0xf4987c / 0xf4aa78) | 9 (3 km) | 14 (3 mi) |
| 2000 (0xf4a190) | 8 | 13 |
| 1500 (0xeddb24 / 0xf4a800) | 7 | 12 |
| 1000 (0xf49a38) | 6 | 11 |
| 500 (0xf4a450) | 5 | 10 (half a mile) |

### Wrong way — the tick at 0x936fb8..0x937032

While the route is valid: when `[+0x84] > 2.0` (0xeddb40) the timer `+0x1ce4`
accumulates the frame time; at ≥ 10 s (0xedd270) and `[+0x88] == 0` → cue 0,
timer 0, `[+0x88] = 1`. Back on course: `[+0x84] = 0`, `[+0x88] = 0`, and the
timer is preset to 10 (0x937096), so the next wrong-way episode speaks as soon
as `[+0x84]` passes 2. **How `[+0x84]` is measured is not decoded** — the port
counts the seconds spent moving (> 3 m/s) against the route direction [INF].

### Arrival — 0xa574c2

`[mgr+0xb5]` (voice on) → cue 15. **Recalculating (cue 16) has no caller in the
readable code** (no `mov ecx, 0x10` before a call to 0xd84950/0x933df0); the port
says it when the car gets > 40 m off the route, at most every 10 s [INF].

## 4. Port specifics [INF]

- **Junctions** are the route's road nodes with ≥ 3 distinct neighbours
  (`<Island>_ROAD_MAP.IA`). The port graph has no road ids, so "another road" is
  replaced by: the voice looks ahead to the next junction outside the silent
  121..241 band. The angles are measured 15 m from the node (route polyline for
  in / out, the link curves for the other branches).
  **The links of ROAD_DETAIL.IA leave the junctions on a 30° grid** — every
  measured turn is an exact multiple of 30° (0, 30, 60, 120, 210, 240, 270...)
  [DATA, 3 routes on Ibiza].
- **Left / right**: retail angle = 180 − the clockwise angle from the direction
  of travel, with `right = (-forward.z, 0, forward.x)`,
  the minimap's convention; checked on a 3D frame at junction 713
  (`docs/images/port-gps-junction-s44.png`: the road leaves up-left in the view
  and on the minimap).
- **Route start**: `MapScreen::computeRoute` now starts on the road *link*
  nearest to the car (projection on its curve, both end nodes seeded with their
  distance along it) instead of the nearest node, which could be on another road
  and made the route (and the voice) detour.
- **Units**: metric unless `-gpsmiles`; the retail switch is the `gps_Unit`
  option (Game settings page, not answered by the port yet).
- **Overlap**: like retail, a cue asked for while one plays is dropped.

## 5. Test

```bat
build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata ^
    -gm FREERIDE -skipcine -no3d -nopad -fixedtimestep -tp 35976,5805 -logroute ^
    -gpsdrive 31000,9000,40 -exitframe 14000
```

`-gpsdrive x,z[,speed]` routes to x,z and moves a virtual car along the route at
`speed` m/s (negative: backwards → the wrong-way cue); the real car stays put.
`-logroute` lists the junctions and their angles. Note that with `-no3d` the
simulation runs several times faster than the audio clock, so a near cue that
follows a far cue at 40 m/s gets dropped as "still playing" — a test artifact;
at real time 150 m at 150 km/h is 3.6 s, longer than the 2.3 s far line.
