# Island ambience, traffic sound, surround and the DualSense pad

Tags: **[DATA]** = read in the game files, **[CODE]** = read in the readable code of `TestDrive2.exe`
(VA >= 0x6c1000), **[INF]** = port inference (the retail logic is not readable), **[ENC]** = the retail logic
sits in the SecuROM-encrypted range (< 0x6c1000) and was not read.

Sources: `euro/bnk/islands/<island>/sound/ambience/ambiences.xmb` + its loose `*.wav`,
`euro/bnk/sound/audiosetup.xmb`, `euro/bnk/sound/vehicles/generic_high_high.bnk` (`CarVSTConfig.xmb`),
`islands/<island>/vehicules/traffic/*.bnk`, `docs/re/sound3d_spec.md`, `docs/re/traffic_spec.md`.

Files: `source/audio/ambience_audio.{h,cpp}`, `source/audio/traffic_audio.{h,cpp}`,
`source/input/gamepad_output.{h,cpp}`, `source/audio/vehicle_audio.*` (the player's horn),
`source/audio/gs_audio.*` (the output layout), `source/game/freeride/traffic.*` (the audio snapshot and
the honk events), `source/game/freeride/gm_freeride.*` (the wiring).

-----------------------------------------------------------------------------------
## 1. The ambience data (ambiences.xmb) [DATA]

`ASTExport { Environment[] Environments; ReverbPreset[] ReverbPresets; LightningExport[] Lightning;
RainResource[] Rain }` — `Rain[0]` is already used by `EdenRain` (docs/re/rain_spec.md).

`Environment` (69 bytes packed):

| field | used here |
|---|---|
| `Int32 TypeID` | the record id (0..15) |
| `String TypeName` | `Empty`, `Residential`, `Industrial`, `Plain`, `Airport`, `Sea`, `Cliff`, `Forest1`, `Forest2`, `Salines`, `Mountain`, `Desert`, `Tunnel`, `Harbor`, `River`, `City` |
| `Int32 ClassID`, `Int32 PriorityRank` | not used (the retail zone classes are [ENC]) |
| `Single PriorityFactor` | the zone's rank against the others: `0` = never picked (`Airport`, `Tunnel`), `Plain` 0.7, `Forest1` 0.9, `Cliff` 0.6, `Sea` 0.65, `City` 0.05, `Residential` 0.2 |
| `String Path_Resource_Day` / `Night` | the authoring path (informative) |
| `String Name_Resource_Day` / `Night` | the resource name; the wav is **`<folder>/sound/ambience/<name lowercase>.wav`** (the same rule `EdenRain` uses, [CODE] FUN_00cbf3e0) |
| `Single ClipNear` / `ClipFar` | the record's own distances (5 / 500, `Sea` 500, `River` 50, `City` 300 ...) |
| `Single Volume` / `Pitch` | the bed's gain / pitch (1.0, `Industrial` 0.801, `Cliff` 0.601, `Mountain` 0.6, `River` 0.498) |

Ibiza ships all 16 records (`a_daycity.wav`, `a_nightcityibiza.wav`, `a_daysandcoast.wav`,
`a_dayforest(02)`, `a_daymarsh`, `a_dayharbor`, `a_dayriver`, `a_daycliff`, `a_daydesert`, ...). The
`AmbienceAudio: '<path>': 16 records` line of the log names the ones that were found.

## 2. Which record plays [INF]

The retail ambience manager (the code that picks a record and crossfades it) is **[ENC]**. The port picks
with what the world can answer cheaply and openly, highest priority first:

| test | record |
|---|---|
| `-ambience <name\|id>` given | that record, always |
| under a roof / in a bore (`EdenWorld::isSheltered`) | the current record, ducked to 40 % (a tunnel keeps the storm / the sea through the walls) |
| shore within 140 m (leaving at 210 m: hysteresis) | `Sea` |
| ground > 160 m over the sea plane | `Mountain` |
| ground > 70 m over the sea plane | `Cliff` |
| 3 or more live traffic cars within 130 m | `City` |
| 1 or more | `Residential` |
| otherwise | `Plain` |

- **Shore distance**: 8 directions, a heightfield sample every 15 m up to 480 m; the first sample at or
  below the water plane (`EdenOcean::getWaterPlaneHeight()`, 0 on Ibiza / Casino, -1.8 on Hawai) is the
  shore of that direction, the smallest of the 8 wins. Re-probed every 0.6 s, not every frame.
- **Crossfade**: the new bed starts at 0 in slot 0 while the old one fades out in slot 1 over 2.5 s; a
  day <-> night change is a switch of the same record.
- **Day / night**: the game clock, 7 -> 20.5 is day; a record without a night wav falls back to its day wav
  (`Cliff`, `Mountain` and `River` ship only one).
- **The shore layer**: under any non-`Sea` bed, the `Sea` record plays as a second, quieter layer, its gain
  rising from 0 at 320 m to 0.55 at 25 m, so a coastal road always has waves. It is disabled while the
  `Sea` bed itself is active (two identical loops would comb-filter).
- **Client**: `AmbienceInternal` (bfx 10), the 2-input stereo pipeline: an island bed is not a point source.
- `-noambience` / `ambience = 0` in `options.ini` / `OTDU_AMBIENCE=0` turn it off; `-logambience` prints
  every switch with the shore distance, the cars around and the ground height.

Verified headless: `-gm FREERIDE -no3d -skipcine -exitframe 900 -logambience` logs
`bed 'Plain' (day, shore 165 m, 0 cars, ground 5 m)` then `Residential` / `City` as the traffic spawns;
`-ambience Sea` and `-clock 23.00 -ambience City` decode `a_daysandcoast.wav` / `a_nightcityibiza.wav`.

## 3. Traffic sound [DATA + INF]

The traffic **model** banks (`islands/<island>/vehicules/traffic/*.bnk`) carry geometry only, no wav.
The ambient cars are voiced from the generic car sound bank
`sound/vehicles/generic_high_high.bnk` (`CarVSTConfig.xmb` + `pagani_onlow_ST` / `pagani_onmid_ST` /
`pagani_onhigh_ST` / `pagani_idle_ST` + `Horn_class_1.wav`):

- **Engines**: one looped voice per nearby car (`OtherVehicles`, bfx 32, the 5-input spatial pipeline),
  nearest 10 within 150 m. `pagani_onmid_ST` is the loop, pitch `0.82 .. 1.38` and gain `0.32 .. 1.0`
  from the car's speed (0 .. 24 m/s), a police car 1.25x louder. Distance: full at 10 m, 0 at 150 m,
  half volume at 75 m (the mixer's own `ClipNear/ClipMid/ClipFar` curve does the rest, with Doppler from
  the car's velocity).
- **The player's horn** (`IA_CarHorn`): `CarVSTConfig` `sSampleName = "Horn"`, `PanelID 7` -> the
  `EngineFx` client, a loop held as long as the button is. The retail sample is `carhorn_loop_7_int` for
  the California and `Horn_class_1` in the generic bank.
- **Traffic horns**: a car the player blocks (within 16 m, in front of it, both nearly stopped) honks
  every 6-12 s (`Horn_class_1`, spatial, 3 one-shot slots). The retail trigger is [ENC]; this is the
  "get out of the way" case that reads on screen.
- **Not implemented**: the police **siren**. No siren sample exists in the readable banks checked
  (`sound/vehicles/*_police_high.bnk`, `islands/*/vehicules/traffic/*.bnk`, `sound/voices/*/police/*`,
  `fx/*.bnk`); the retail siren is most likely an SC emitter of the chase scene data, which is [ENC].
- `-notrafficaudio` turns both off.

## 4. The DualSense pad [DATA: SDL3 3.4 API]

`source/input/gamepad_output.*`, driven by `GMFreeRide` every frame; the pad comes from `GSInput`.

| feature | how | state |
|---|---|---|
| **rumble / haptics** | `SDL_RumbleGamepad(low, high, 220 ms)` re-issued every 100 ms (SDL cancels the previous effect on every call). Low = the road feel (speed 0..180 km/h x the surface: 0.16 road, 0.42 dirt, 0.55 grass, + the engine load), high = the surface texture + the pulse. A traffic crash calls `pulse(deltaVelocity / 12, 0.3 s)` | **works** |
| **light bar** | `SDL_SetGamepadLED(r, g, b)`. The police chase (`ChaseMode::eState::Chase`) flashes blue / red at 1.4 Hz; the idle colour is off (`-dsled r,g,b` sets another). A pad without a light bar answers false and the port stops trying | **works** (a DualSense is detected as `rumble yes`) |
| **speaker** | **not possible**: SDL3 3.4 writes the DualSense audio report fields internally (`ucSpeakerVolume`, `ucAudioEnableBits`, `SDL_hidapi_ps5.c`) but exposes no API to send PCM to the pad, and this port has no raw HID / hidapi path. `GamepadOutput::playSpeaker()` keeps the hook and logs once why it is a no-op | **blocked by SDL** |
| adaptive triggers | SDL3 only offers *trigger* rumble (`SDL_RumbleGamepadTriggers`, an Xbox feature), not the DualSense trigger resistance | **blocked by SDL** |

`-nods` / `dualsense = 0` in `options.ini` / `OTDU_GAMEPAD_EXTRAS=0` turn the extras off; pads without the
feature stay silent.


## 5. 5.1 / 7.1 output [DATA]

`docs/re/sound3d_spec.md` 2 already decodes the routing: `audiosetup.xmb` gives every pipeline three
matrices (`RoutingStereo`, `Routing51`, `Routing71`) and the mixer applies the one matching the device
(`GSAudio::mixLocked`). The `Sfx` family (5 inputs FL FR BL BR C) routes as

- 5.1 (edAudio order FL FR BL BR C LFE): each input to its own speaker at 0 dB, plus -12 dB into LFE;
- 7.1 (FL FR BL BR SL SR C LFE): each input to its own speaker, plus -6 dB into the side pair
  (FL/FR -> SL/SR, BL/BR -> SL/SR) and -12 dB into LFE.

so the surround support was already there; what this pass adds is the **control and the diagnosis**:

- `SetOutputNbSpeakers` (the retail `SystemPC.cpr` value, `GSAudio::initialize`) is now a setting:
  `-speakers 2|6|8`, `OTDU_AUDIO_CHANNELS` and `speaker_layout` in `options.ini` (0 = the device's own
  layout, 2 stereo, 6 = 5.1, 8 = 7.1). `GSAudio::setSpeakerLayout` re-opens the device on a change.
- The open log line names the layout (`48000 Hz, 6 channels, 5.1 (mixer)`), and a mix wider than the
  device is **warned about** instead of silently folded down:
  `Audio: 6-channel mix on a 2-channel device (set the Windows speaker configuration to 5.1 / 7.1 ...)`.
- Channel maps: edAudio `FL FR BL BR C LFE` / `FL FR BL BR SL SR C LFE` -> SDL `FL FR FC LFE BL BR` /
  `FL FR FC LFE BL BR SL SR` (`GSAudio::setupOutput`), unchanged but now verifiable from the log.

Verified headless on this machine: `-speakers 6` opens `48000 Hz, 6 channels, 5.1`, `-speakers 8` opens
`48000 Hz, 8 channels, 7.1`.

## 6. Command line

| option | effect |
|---|---|
| `-ambience <name\|id>` | force one `ambiences.xmb` record (the `TypeName`, case insensitive, or the `TypeID`) |
| `-noambience` | the island beds off (also `ambience = 0` in `options.ini`, `OTDU_AMBIENCE=0`) |
| `-logambience` | the `AmbienceAudio: bed '<name>' (day\|night, shore <m> m, <n> cars, ground <m> m)` line of every switch |
| `-notrafficaudio` | the ambient traffic engines / horns off |
| `-speakers 2\|6\|8` | mixer output layout (see 5) |
| `-nods` | the pad light bar / rumble off |
| `-dsled r,g,b` | the idle light-bar colour |

## 7. Still open

- The retail **zone lookup** (which `Environment` a spot belongs to) is [ENC]: the port's height / shore /
  traffic tests are [INF]. A decoded zone volume (an `Entity__*` region or a per-sector byte in the
  `.pmi`) would replace them one to one.
- `Forest1/2`, `Salines`, `Harbor`, `River`, `Desert`, `Industrial`, `Airport` have no test yet: they need a
  vegetation / building / dock density the world does not expose per point. `-ambience` can drive them for
  a shot today.
- The traffic **siren** and the **DualSense speaker** are blocked as described above.
- The reverb side of `ambiences.xmb` (`ReverbPreset`, `EarlyReflections`, `Reverberation` per environment)
  is not used: the mixer has no reverb bus.

