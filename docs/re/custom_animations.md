# Custom animation, held props, and lip sync for custom dialog

Tags as in the other specs: [CODE] read from the readable exe, [DATA] read from the game files,
[INF] inferred (reason given).

The owner's three asks (2026-10-31):

1. *"adding location names to their correct props"* — it may need vision; see §5.
2. *"could we make custom animations? there's petrol stations I found and would be cool if we can have
   our avatars holding the nozzle to petrol tank"* — §1-§3.
3. *"maybe a lip sync method could be added for custom dialog?"* — §4, **implemented this session**.

The short answer: **all three are possible with what is already in the tree**, none needs the
encrypted edAnim library, and the lip sync is no longer a plan — `-say "<text>"` works today.

---

## 1. What the port already has (the capability inventory)

These are the pieces that make the three asks cheap. None of them was written for this; they are
the intro cinematic's own machinery, reused.

| Piece | Where | What it gives |
|---|---|---|
| `EdenSkeleton` | `render/eden/eden_animation.h` | `.XSB` "SKEL" (avatar / prop skeletons): parents, bone hash4, bind pose, inverse bind. `findBone( hash4 )`, `computeSkinMatrices( locals, world, skin )`. |
| `EdenAnimation` | same | `.anm` "ANM!" clips: per bone tracks (rotation / translation / scale), sampled with slerp; `loopDuration` (the `dur2` tail) handled. |
| `BlendPoses`, `SolveTwoBoneIK`, `SolveAimIK` | same | pose blending and an analytic 2-bone IK + aim IK (the driver IK of `avatarsdriver.bnk driveiklayer0..6.bas`). |
| `EdenAvatar` / `EdenAvatarLibrary` | `game/avatar/eden_avatar.*` | DBPNJ -> `.avt` -> body + clothes, skinned on the man / woman skeleton; `setFacialCoefficients`. |
| `ModProps` | `game/spots/mod_props.*` | our own OBJ + MTL + PNG props placed from the mods overlay; already the owner's route for a station (`fuel` column). |
| `FaceFxActor/AnimSet/Evaluate` | `game/avatar/facefx.*` | the retail facial rig: 42 facial bones, compiled face graph, `Evaluate` over a synthesized or baked animation. |
| `FadeFreeride` | `game/freeride/fade_freeride.*` | **the only place an avatar is drawn and animated inside FREERIDE** — the template for any in-world avatar. |
| `CinematicCar` / `FuelMenu` | `game/cinematic/`, `game/freeride/fuel_menu.*` | the pump screen that already charges the player at a `fuel` prop. |

The key one for a *held* prop is `computeSkinMatrices`: it fills `worldBones`, one model-space
matrix per bone, in the same pose that is drawn. `world = avatarWorld * worldBones[handBone]` is
therefore the nozzle's transform, with no new engine support.

---

## 2. A custom `.anm` clip (the writer route)

The container is fully described in `docs/re/avatar_spec.md` §1 and matches
`EdenAnimation::parse` line for line [DATA + CODE]:

```
+0x00 "ANM!"
+0x18 "DATA"
+0x20 u32 numTracks
+0x24 f32 duration        (clamp for a non looping play)
+0x28 f32 dur2            (loop period; <= 0 -> duration)
+0x38 tracks[]:
        u16 kind          (1 rotation, 2 translation, 4 scale, 8 IK constraint)
        u16 0xffff
        u32 size          (whole track, including this 12-byte header)
        u32 boneHash4     (EdenNameHash4 of the bone name)
        channels, in kind order: u16 numKeys, u16 format, f32 subDuration, then
          format bit0: u16 normalized times (key 0 implicit at 0)  else f32 times
          format bit1/2: compressed values (16-bit)               else f32 xyzw
          format bit4: f32 rangeMin, f32 rangeExtent prefix
```

So **we can author clips ourselves**: `format = 0`, `kind = 3` (rotation + translation), float
times and float quaternions is the simplest legal clip, and `parse()` accepts it. A 30-line Python
writer converts "bone name -> [time, quaternion, translation]" (Blender / Mixamo export, or a
hand-authored table) into that file; `boneHash4` comes from the documented fold
(`b[i & 3] += c`, little endian, `docs/re/avatar_spec.md` 2.3).

Two traps: the engine quaternion convention is **conjugate** (`Local = T * FromQuaternion(conj q) * S`,
`eden_animation.h`), and the retail clips store the hands / fingers compressed (format 2/3) — a
float clip is legal but 4x bigger, which is irrelevant for us.

What a clip can *not* do is react: a nozzle in a tank is a contact, so it wants IK (§3), not a clip.

---

## 3. The petrol-pump nozzle: what is missing, and the plan

**What is missing today, in order of cost:**

1. **A nozzle mesh.** The retail data has no pump or nozzle asset except one:
   `gaspump_d.2db` lives in exactly one bank (`gph_l2d`, the house garage's pump), which is why
   `FuelMenu` can only fill in the garage and at our own `fuel` prop
   (`docs/re/fuel_gas_station_spec.md`, session 15/25). The `ECW_A` forecourt is a *car wash*, not a
   station (`docs/re/car_wash_spec.md` §0). So the nozzle is either extracted from `gaspump_d.2db`
   or authored as an OBJ — `ModProps` already reads OBJ + MTL + PNG, and a `props/nozzle.obj` is
   five minutes in Blender.
2. **A standing player avatar in FREERIDE.** There is no third-person avatar in free drive; the only
   in-world avatar in the mode is Tess in `FadeFreeride` (`fade_freeride.cpp:245-260`). That
   function *is* the pattern: `avatarLibrary.load()`, `EdenAvatar::load( config )`, fill `locals`
   from the bind pose, `anim.sample(...)`, `computeSkinMatrices( locals, worldBones, skin )`,
   `avatar->draw( renderer, world, skin, &lighting )`. A `FuelAttendant` / `FuelSelf` class copies
   it with `GetPlayerProfile().AvatarConfig`.
3. **The hand attachment.** New, but small: a prop mesh drawn with
   `world = avatarWorld * worldBones[hand]` where `hand` is
   `skeleton.findBone( EdenNameHash4( "Hand_R" ) )` (`eden_animation.h:13`, the fold of
   `docs/re/avatar_spec.md` 2.3). The man skeleton is 125 bones, the right arm chain is 84-110 and
   `Hand_R` is inside it; `IK_Right_Hand` is bone 90 if the hand itself is not wanted.
   `worldBones` is already filled every frame by the draw path, so this is a draw-time matrix, not
   an animation feature.
4. **The reach.** `SolveTwoBoneIK( skeleton, locals, Arm_R, ForeArm_R, Hand_R, goal, Hand_R )` with
   `goal` in **skeleton model space** (the tank filler, transformed back through the avatar's world
   matrix), and `SolveAimIK` for the nozzle's angle into the tank. Both already exist and are
   already used for the driver seat (`docs/re/driver_seat_spec.md`).
5. **The animation around it.** Not strictly needed: an idle stance + IK gives "holding the nozzle
   at the tank". A custom `.anm` (§2) only adds flavour (the reach, the trigger squeeze).

**The wiring** would mirror the fuel feature that already exists: the `fuel` prop's
`findFuelStation()` (mod_props.h) already decides when the player may fill; on validate, instead of
(or before) opening `FuelMenu`, spawn the avatar at the pump, solve the IK for ~6 s, and bill the
same way `FuelMenu::fill()` does.

**Cost estimate.** 1-2 sessions: the mesh is asset work, the avatar + attach is ~150 lines next to
`fade_freeride`, the IK is a call. The risk is placement, not technology — the pump prop's forecourt
pose has to be read out of `ModProps` and the car has to be parked within reach.

---

## 4. Lip sync for custom dialog — **implemented this session**

The retail game has no phoneme lip sync: every voice line ships a pre-baked FaceFX `.fxe`
(`docs/re/facefx_spec.md` §1). What the game *does* have, and the port reproduces, is a compiled face
graph whose curve names are the phoneme targets `open W ShCh PBM FV wide tBack tRoof tTeeth`
(plus `Blink`, action units, gaze). `FaceFxLibrary::Evaluate` reads a curve by name
(`facefx.cpp:547-552`) and pushes it through the graph into 42 bone poses.

So a *generated* animation is the same kind of object as a retail one. This session adds:

- `source/game/avatar/speech_synth.{h,cpp}` — `SpeechAnimation::Build( text, FaceFxAnim & )`:
  an English grapheme -> phoneme -> FaceFX target pass (digraphs `ch sh th ph ng` first, then
  vowels / bilabials / labiodentals / alveolars / velars / sibilants), fixed durations, per unit
  attack / release, sampled every 5 ms into `FxAnimCurve`s with linear interpolation, plus a
  `Blink` every 3.4 s. Ten curves, ~2.7 s for a seven word sentence.
- `IntroCinematic`: `-say "<text>"` builds it once at load and `applyFaceFx` evaluates it on the
  **scene time (looped)** instead of the `.sce` Morph track, so every avatar of the scene — PNJ and
  the player — speaks, and a still frame is reproducible. The look-at registers still apply.
  `-logfacefx` now prints a `FaceFX say:` line per avatar.

```bat
build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 ^
    -userdata C:\temp\otdu2-cline-userdata -gm AVATAR_CHOICE -skipintro -walkmode 1 -tess ^
    -say "hello there welcome to the car wash" -logfacefx ^
    -autoplay "120:shot=C:/OpenTDU2-cline/_sayA.ppm" -exitframe 130
```

**What it is and is not.** It is *text driven*, not *audio driven*: there is no forced alignment
from a `.wav`, no dictionary (so "read" and "reed" mouth the same), and English only. That is fine
for a placeholder line or a mod's typed dialog; a real voice line would want either the shipped
`.fxe` (already works) or an amplitude/formant analysis of the wav — the same `FaceFxAnim` can be
built from that instead, so the rest of the path does not change.

The motion is **retail scale**, not cartoon: the spec's own measurement of a real line is 1-3 mm and
up to ~7 degrees per lip bone (`docs/re/facefx_spec.md` 3.5), and the synthesized curves land in the
same range (the left lower lip translation moves 0.0512 -> 0.0487 in the model's units over 5 mm of
speech). At 1280x720 that is two or three pixels, which is why
`docs/images/lipsync-say-s26.png` is a 3x crop of the face rather than the full frame.

**What is still missing for custom *audio*:** the port has `AudioClip` / `GSAudio` and the
cinematics' `CinematicSounds`, but no "play a wav from the mods folder" route. That is the other
half of custom dialog and it is a separate, small job.

### 4.1 Audio driven (`-saywav "<file.wav>"`) — the path a microphone uses

`source/game/avatar/speech_audio.{h,cpp}` analyses a waveform instead of letters:

| measure | window | drives |
|---|---|---|
| RMS in dB over a -50 dB floor | 40 ms | `open` (the jaw), squared so quiet syllables barely open it |
| zero crossing rate + first-difference energy ratio | 40 ms | `wide` (front vowels) vs `W` (rounded) |
| the same high band ratio, gated by the level | 40 ms | `ShCh`, and half of it `FV` (sibilants: s, sh, ch, f) |

The values do **not** become an animation: `FaceFxLibrary::Evaluate` gained a
`FaceFxRegister[]` overload, which writes node registers *after* the curves — the same "replace"
operation the look-at already uses (`FUN_006ed2d0(node, 2, value)`, `facefx.cpp` §3.4). So a live
driver sets five floats per frame and the retail graph does the rest. That is exactly the shape a
microphone or a network mouth stream produces.

**Verified** on a synthetic PCM clip with a known mouth sequence (`_mouthtest.wav`: silence
0-0.6 s, 220 Hz vowel 0.6-1.4 s, white-noise sibilant 1.4-2.0 s, silence 2.0-2.6 s), Tess speaking,
per-frame `-logfacefx`:

| clip time | state | `open` |
|---|---|---|
| 0.0-0.6 s | silence | min **0.000** |
| 0.6-1.2 s | vowel | 0.906 - 0.958 |
| 1.4-2.0 s | sibilant | 0.604 - 0.783, `ShCh` 0.29 - 0.39 |
| 2.0-2.6 s | silence, looped | min **0.000** |

**Decoding is the engine's, so nothing is refused.** The game's own wavs are **MS ADPCM** (`fmt 2`,
4-bit, 22050 Hz stereo) — `_wav2/*.wav` and `_wav3/*.wav`, ten for ten, the same format
`docs/re/gps_voice_spec.md` records for the sat-nav voices, which *do* speak. `SDL_LoadWAV_IO`
decodes MS and IMA ADPCM, which is why the port's cutscene audio works; the first version of
`-saywav` hand-rolled a PCM-only RIFF parser and therefore refused them — **that was this module's
limitation, not a game bug**. `SpeechAudio::load` now goes through `GSAudio::loadClipFile` (RIFF via
SDL, everything else through `audio/audio_decoders`), so whatever the game can play the mouth can
follow. Measured on the retail mix: `c_meeting_tess_1-getout_CINEMATIC.wav` (77 526 B, `fmt 2`) ->
`22050 Hz 2 ch -> 27537 frames, 3.44 s at 8000 Hz`, and the mouth follows it (`open` 0.00 while the
clip fades in, up to **0.84** through the loud part, back down at the end).

Corollary for anything else that reads audio: do **not** parse the RIFF header by hand - the game's
files are all ADPCM, and `GSAudio::loadClipFile` is the decoder that already works.


---

## 4.2 Voice chat: "each player's avatar moves its mouth when they talk"

The mouth half of that is now the code above. The two halves that do **not** exist yet:

1. **There is no networking at all.** `source/` has zero references to sockets, WSA, ENet, Steam or
   any online service - the port is single-player by construction, so "when online happens" means a
   netcode project first (session/peer model, replication of car + avatar transforms, a way to hand
   an avatar a look). The *audio* dependency is already there though: `libogg`, `libopus` and
   `opusfile` are built for the radio (`cmake/Dependencies.cmake`), and Opus at 16-24 kbps is the
   standard voice-chat codec.
2. **There is no capture device.** `gs_audio.cpp` opens exactly one stream,
   `SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, ... )`. A microphone is the same
   SDL call with `SDL_AUDIO_DEVICE_DEFAULT_RECORDING` and a callback that appends to a ring buffer -
   the analysis in `SpeechAudio` already takes a plain mono buffer, so it would read that ring.

**Bandwidth note:** do not ship the audio to every client just to move a mouth. The classic trick is
to ship the *shape*: five bytes per frame per speaker (open, wide, W, ShCh, FV at 8 bits), ~300 B/s
at 60 Hz, and every client runs the same `FaceFxLibrary::Evaluate` registers locally. Send the Opus
voice only to the players who need to hear it (proximity), and the five bytes to everyone who can
see the avatar.

Order I would build it in: `-saywav` on a PCM wav (done) -> live capture into the same analysis
(SDL recording) -> a local "talking head" test where the player's own avatar mouths their mic ->
only then the network, because the mouth is the easy end.


---

## 5. "Location names on the correct props" — what the label file can and cannot say

The mechanism already exists (`docs/re/asset_viewer.md` §3, `game/asset_labels.*`):

- `docs/re/asset_labels.txt` is TAB separated, one asset per line, keyed by **bank + asset
  filename** — an identity, never a display name, so a label cannot drift onto another asset;
- the asset viewer writes entries (`-label`, `-note`, `-savelabel`) and `-labellint` checks every
  key against the game data (a missing bank or a filename not in it is reported);
- the 3D view renders one model on a turntable, and `-autoplay N:shot=…` writes a screenshot, so
  the loop "render it -> look at it -> name it -> lint it" is already automated. **That is the part
  that needed eyes**, and it works: a screenshot is an image the agent can read, exactly like a
  human looking at the viewer.

Two honest limits, both structural:

1. **A label is per asset, not per placement.** `ecw_a.bnk` is the same file on Ibiza and Hawai
   (3 spots + 5 spots, `docs/re/car_wash_spec.md`), so "the petrol station at 11660, 5.3, 15866"
   cannot be a label key. The *note* field is where a location goes ("the ECW_A forecourt, spots on
   both islands"), and that is what the existing entries do.
2. **Most world props are not bank assets at all.** A sector's placed objects are records *inside*
   one `.3dd` (`tools_probe/pmi_probe.py`: `sector-3-3-0-0-o.3dd`, 16 models, 2553 instances, named
   by hash — `'??ROCKSG'`, `'V_BUSH_D'`). The label file cannot name those; naming them needs the
   instance list itself (a `.pmi` side-car of names, or a viewer tab over the placed models).

So the reachable version of the ask is: **label the props the world actually loads as banks** —
the spot exteriors (`SpotExteriors`: 80 on Ibiza, 144 on Hawai), the interior sets, the mod props —
and note the location in the note field. 153,690 assets exist in total; the spot exteriors are the
few hundred that a player sees, so they are the set worth naming.

---

## 6. Next steps, in the order I would do them

1. ~~Lip sync~~ — done: `-say` (text) and `-saywav` (audio, the path a microphone uses). Next
   increment: a live capture ring (SDL recording) into the same `SpeechAudio::sample`, then the
   network mouth stream (§4.2).
2. **Labels** (§5): a `-labelprops` batch mode for the asset viewer — for every spot exterior bank
   of one island, render its largest `.3DD`, screenshot it, and leave the label for review. That
   turns a 300-run loop into one command plus a review pass.
3. **Nozzle** (§3): nozzle `.obj` (author or extract), a `FuelSelf` avatar reusing the
   `FadeFreeride` pattern, hand attach through `worldBones[Hand_R]`, `SolveTwoBoneIK` to the tank,
   then bill through `FuelMenu`.
4. ~~Custom `.anm`~~ — the writer in §2 is a 30-line script; only worth it once something needs a
   clip that IK cannot fake (a wave, a trigger squeeze).


