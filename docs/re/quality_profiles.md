# Quality profiles — the `Quality profile` row and `-profile <name>`

OpenTDU2 is **CPU-recording-bound**, not GPU-bound: the FREERIDE benchmark below renders the whole frame in
~2.5 ms of GPU time against ~35 ms of CPU time at default quality (RTX 5070 Ti / i9-10850K, 1280x720). A profile
therefore has to cut **draw submissions and passes**, not just resolution. The profiles below do that; `High`
keeps the retail look, `60 fps` and the handheld profiles trade shadows / water reflection / vegetation distance
for a 60 fps baseline.

## 1. The profiles

`Options > Advanced settings` (`F2`) → **Quality profile**, or `-profile <name>` for a run. Selecting a preset
overwrites every row below it; editing any row makes the profile **Custom**. The presets are in
`ApplyQualityProfile()` (`source/game/game_options.cpp`).

| Profile | `-profile` | Window | Render scale | Shadows | Water mirror | Draw distance | HUD | Other |
|---|---|---|---|---|---|---|---|---|
| **High** | `high` | current | 100 % | on, 100 % | every frame | 100 % | full | retail look (grading, haze, flare) |
| **Medium** | `medium` | current | 85 % | on, 50 % | every 2nd frame | 85 % | full | bicubic + sharpening |
| **Low** | `low` | current | 70 % | **off** | **once (static)** | 60 % | simple | no grading / haze / flare; 60 fps cap |
| **60 fps** | `60` | current | 100 % | **off** | **once (static)** | 75 % | full | keeps grading / haze / flare; 60 fps cap |
| **Steam Deck** | `steamdeck` | 1280x800 | 67 % | **off** | **once** | 50 % | simple | 60 fps cap, LOD 1 |
| **Legion Go** | `legiongo` | 1920x1200 | 67 % | **off** | **once** | 50 % | simple | 60 fps cap, LOD 2 |

Steam Deck and Legion Go pin the window to a 16:10 panel size; the others keep the current window (set it in
`Options > Video` or `-width/-height`). The 60 fps cap is ignored by `-fixedtimestep` tests.

## 2. What each knob does

| Setting (`options.ini`) | Range | Applied in | Effect |
|---|---|---|---|
| `render_scale` | 50..200 % | `EdenSceneView::ensureTargets` | 3D scene resolution relative to the window; the upscaler brings it back |
| `shadows` | 0/1 | `EdenSceneView::renderOffscreen` | the 3 PSSM slices + screen shadow resolve (3 world re-submissions) |
| `shadow_distance` | 50..150 % | `EdenShadows::renderCasters` | scales `SET_SHADOW_CONFIG_MAX_CAST_DIST` (FREERIDE 736 m). **Measured: no effect at the benchmark spawn** (see §4) |
| `ocean_reflections` | 0,1,2,4… | `EdenSceneView::renderOffscreen` | how often the water's mirror pass re-renders the world. 0 = once at the first frame |
| `draw_distance` | 50..150 % | `EdenWorld::drawInstances` | scales the `.pmi` instance draw distance; the strongest draw-call lever |
| `hud_quality` | 1/2 | `FreeRideHud::draw` | 1 = minimap + built-in gauge; 2 = the HUD and GAUGES Flash movies |
| `color_grading`, `haze`, `sun_flare` | 0/1 | post / scene | GPU-side look passes (cheap on the CPU, kept where possible) |

The single biggest CPU fix of this session is **not** a profile setting: the FREERIDE minimap used to re-clip
every road triangle on the CPU every frame (11.5 ms). It now uses a GPU scissor rect plus a ~20 Hz cached vertex
list (`Renderer2D::setClipRect`, `FreeRideHud::drawMinimap`), which costs ~0.6-1.7 ms. That is why `High` can
afford the full HUD and why `Hud detail` mostly matters on very weak CPUs.

## 3. Measured results (session 73)

Benchmark: FREERIDE on Ibiza, `-tp 21796.3,24188.1 -snaproad 150 -carlod 4`, `-skipcine`, `-fixedtimestep`,
`-autoplay "60:hold=accel"`, frames 240-480, one `-log` per run, RTX 5070 Ti / i9-10850K. Use the same command
for an A/B; the spawn matters (docs/PERF_PLAN.md §1).

| Run | Frame | Notes |
|---|---|---|
| default, 1280x720, **before** this session | 41-42 ms (24 fps) | `main` 11.0-11.2 ms, HUD minimap 11.5 ms, offscreen 29.5 ms |
| default, 1280x720, **after** | **34.8 ms (29 fps)** | `main` 2.2 ms, offscreen 31.1 ms; GPU 2.5 ms |
| `-profile medium`, 1280x720 | 23.0-28.2 ms (35-44 fps) | shadows on, mirror half rate |
| `-profile low`, 1280x720 | **10.5 ms (96 fps)** | prepass 3.7, opaque 3.1, ocean 0.1, shadows 0 |
| `-profile low`, **3840x2160** (70 % = 2688x1512) | **10.9-11.1 ms (90-92 fps)** | |
| `-profile 60`, **3840x2160** native | **13.3-15.1 ms (66-75 fps)** | two runs |
| `-profile steamdeck`, 1280x800 (67 %) | **9.6-10.2 ms (99-104 fps)** | this PC, not the Deck |
| `-profile legiongo`, 1920x1200 (67 %) | **11.5-12.8 ms (78-87 fps)** | this PC, not the Legion Go |
| opening scene (`-intro`), 1280x720 | 8.2 ms (121 fps) | culling the 31 PNJs per pass: pixel-identical frame, ~10 % off the offscreen passes |
| FREERIDE, `-no3d` (HUD only) | **2.9 ms** | was 12.6 ms before the minimap fix |

`-profile high` at 4K is still ~35 ms of CPU: full-quality FREERIDE submits ~6,449 draws in the main pass and
similar counts in the prepass / 3 shadow slices / ocean mirror — roughly 25,000 draw submissions per frame at
~1 µs each. Resolution does not change that; only fewer draws or cheaper recording does. `60 fps` is the preset
that reaches 60 by removing the passes, not by lowering the resolution.

## 4. Ruled out (with the test)

- **Shadow distance 50 %**: shadows 6.7 ms before, 6.7 ms after (`-shadowdistance 50`, same run). The three
  slices' caster submissions are the cost, not the far range. The setting is kept for other scenes/configs but
  is not the lever on this spawn.
- **Resolution / render scale as a CPU fix**: 100 % vs 70 % barely changes the CPU frame; it only changes GPU
  time. Do not expect a profile that only lowers resolution to fix a CPU-bound scene.
- **The Flash HUD**: `-loghudtimes` shows the HUD/GAUGES movies cost ~0.4 ms; the minimap was the 11.5 ms.
  `Hud detail` is therefore a minor knob, not the fix it looked like.

## 5. Validation commands

```bat
:: full frame, default
build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-cline-userdata ^
  -gm FREERIDE -skipcine -nopad -fixedtimestep -tp 21796.3,24188.1 -snaproad 150 -carlod 4 ^
  -autoplay "60:hold=accel" -width 1280 -height 720 -logscenetimes -loggputimes -exitframe 480 -log _perf.log

:: the 60 fps desktop preset at 4K
::   add: -profile 60 -width 3840 -height 2160

:: the HUD split (the minimap / Flash breakdown)
::   add: -no3d -loghudtimes

:: the opening scene, reproducible frame for a pixel diff
build-cline\bin\opentdu2-cline.exe ... -intro -nopad -fixedtimestep -width 1280 -height 720 ^
  -exitframe 420 -autoplay "400:shot=C:/OpenTDU2-cline/_intro.ppm" -log _intro.log
```

`-profile` is not saved; the Advanced settings row is. A profile run does not overwrite `options.ini` unless the
Advanced page is closed with OK.

## 5b. Why TDUWorld can run 4K at 130-160 fps while this port cannot

TDUWorld runs **the original Test Drive Unlimited 2 engine** (2011, D3D9) with community content/online patches.
Its renderer was built around hardware instancing, static batching, LOD/impostor meshes and a mature culling /
submission path, so at 4K on a 5070 Ti the frame is GPU-bound and 8x MSAA is just GPU work. This port is a
**re-implementation**: it draws the same assets but submits each `.pmi` instance / mesh as its own draw call
(~25,000 per frame across the prepass, 3 shadow slices, ocean mirror, environment cube and main pass), uses full
LOD0 for far vegetation, and records everything on one thread with per-draw constant fills. The measured result
is ~35 ms of **CPU** against ~2.5 ms of **GPU** — resolution and AA do not touch that. The profiles cut the
passes/draws; closing the rest of the gap is the instancing/batching/LOD work in §6, not a resolution change.

## 6. What is still on the table (for the next agent)

1. **Draw-call reduction is the remaining 60 fps lever for High.** 6,449 main-pass draws are mostly `.pmi`
   instances drawn one by one. Instancing identical model/material instances, or merging static sector geometry
   into one buffer per material, would cut this by an order of magnitude. The game data already has LOD1+ /
   `TD_HardwareInstance` / `TD_Impostor` meshes the port does not load (`docs/PERF_PLAN.md` P3).
2. **Shadow slices**: three full world submissions. A 2-slice mode or rendering the caster slice maps every
   other frame while resolving the screen shadow every frame would save ~2-3 ms with little visual difference
   (only moving casters lag one frame).
3. **Ocean mirror**: `OceanReflections = 2` halves it with no visible effect on the animated water; `0` is the
   handheld setting.
4. **Prepass**: the Z pre-pass re-submits the world to feed `gViewPos` to the shadow resolve. Removing it means
   changing how the shadow resolve gets receiver depth (a previous-frame depth or a deferred shadow pass); that
   is the structural way to delete ~7 ms.
5. **Handheld hardware validation**: the Steam Deck / Legion Go numbers above are from the desktop. Run the same
   commands on the devices; if they miss 60, lower `draw_distance` first (it is the CPU lever), then
   `render_scale` (GPU).


