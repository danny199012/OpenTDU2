# PERF_PLAN — frame time hand-off (2026-10-30, session 48; GPU timing added session 50)

A self-contained brief for whoever picks up performance. Read `CLAUDE.md` first (build, headless runs, the
"stop only your own exe" rule), then this. Keep the doc tags: `[MEAS]` measured this session, `[CODE]` read from
our source, `[INF]` inferred.

## 0a. 2026-11-04 (`win`) — the 4K High baseline, the lost minimap fix, and the road to 60

The owner's goal: **4K 60 fps at max settings as the floor**, with head-room above it (the retail game under DXVK
at 4K max + 8x AA runs 100-140 fps on this PC, with grass). Command for every number below:
`-gm FREERIDE -skipcine -nopad -fixedtimestep -profile high -width 3840 -height 2160 -novsync -tp 21796.3,24188.1
-snaproad 150 -carlod 4 -logscenetimes -loggputimes -loghudtimes -autoplay "60:hold=accel" -exitframe 620`, frames 360-600.

- **Regression found and fixed `[MEAS]`**: the d92c85e merge (glm over cline) kept glm's satnav minimap and lost
  cline's session-73 scissor + ~20 Hz cache, so `drawMinimap` was Sutherland-Hodgman-clipping every road
  triangle each frame again: **HUD 10.0 ms at 4K and 9.5 ms at 720p** (it is not resolution-bound). Re-applied on
  the satnav rect (`FreeRideHud::MinimapCache`, `Renderer2D::setClipRect`): HUD **10.0 -> 1.8 ms** (the scissor
  alone 4.3), frame **39.0 -> 31.3 ms (26 -> 32 fps)**. `-nominimapcache` = the A/B (scissor kept, rebuilt every
  frame). Frame-400 screenshot vs the A/B: mean diff 0.008/255, 0.10 in the map box (the map lags <= 2 frames).
- **Where the 31 ms goes `[MEAS]`**: offscreen CPU 27.5 (ocean 5.8, prepass 6.2, shadows 7.3, opaque 6.1, env 1.3,
  blended 0.5), main 1.9, update 1.3. **GPU 4.1 ms** for the whole frame at native 4K.
- **AA / resolution are nearly free `[MEAS]`**: `render_scale = 200` at 4K (7680x4320 internal, 4x SSAA) puts the
  GPU at **8.7 ms** and the frame stays **31.5-32.1 ms**. The GPU crosses 16.6 ms only well past that. Never
  trade image quality for fps on this machine until the CPU side is fixed. (Trap: `-width/-height` without
  `-profile` is overridden by `video_width/height` in `options.ini`; check the `Swapchain created` line.)

**Session 2 of the day (same command, `win`) — 31.3 -> 21.9 ms (32 -> 46 fps) at 4K High, image identical `[MEAS]`.**
New tools: `-logrecordtimes` (a `[perf] record/pass:` line: per pass, the ms inside `EdenRenderer::recordDraw`
/ the pass total and the draws) and **`-sampleprof [<first>,<last>]`** (`source/core/sample_profiler.*`: a
main-thread stack sampler, report in `<log>-profile.txt`; no admin needed). Symbols need a PDB build dir:

    cmake -S . -B build-prof -G Ninja -DCMAKE_MAKE_PROGRAM=C:/Users/cuz_c/Downloads/ninja-win/ninja.exe -DOTDU_DEPS_DIR=C:/otdu2-deps -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe" "-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe" "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /Z7" "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /Z7" "-DCMAKE_EXE_LINKER_FLAGS=/machine:x64 /DEBUG /OPT:REF /OPT:ICF" -DOTDU_EXE_SUFFIX=-prof

(45 s full build with the shared deps cache; exe `build-prof\bin\opentdu2-prof.exe`).

| Step | Frame | What the profile / log showed |
|---|---|---|
| start | 31.3 ms | recordDraw 16.6 of 28 ms offscreen: ~1 us/draw = 0.50 command calls, 0.29 constants, 0.14 descriptor set |
| redundant-bind filter (`-nobindcache` A/B) | 30.1 | pipeline / vertex + index buffers / push constants skipped when equal to the last draw's; reset by any foreign `getCommandBuffer()` (touch count) |
| lazy per-instance `EdenDrawParams` + `FindSmall` | 23.7 | **EdenDrawParams ctor+dtor was 22.5 % of the main thread**: `drawInstances` built two (five `unordered_map`s each, which allocate even empty) per visible instance per pass; string hashing of constant names ~8 % |
| minimap road query cached (150 m margin) + `getPipeline` memo | **21.9** | `forEachRoadTriangle` ~5 %, pipeline hash ~2 % |
| **parallel command recording** (`-noparallelrecord` A/B, `-recordthreads <n>`) | **19.4** | the NVIDIA driver's command recording was ~31 % of the main thread; now on 6 workers |
| **TD_Impostor items not submitted** (then batched + lit, see below; `-noimpostors`) | **15.5 (64 fps)**; with the far trees drawn 15.3-16.0 | `-logdrawfamilies`: 3,875 of 25.5 k draws were TD_Impostor quads from 58 mesh/material pairs - and they drew nothing visible (below) |

**Parallel command recording — how it works** (`EdenRenderer::flushPendingRecords`, `RenderDevice::PendingRecordHook`):
inside an offscreen render target `recordDraw` resolves everything on the main thread (pipeline, constants
written to transient memory, descriptor set written — the material may be mutated right after, see the trap
below) into a `DrawPacket` and queues it. The device calls the hook before *anything* else records into the
frame's command buffer (`getCommandBuffer()`, render target begin / end / clear / copy, timestamps, main pass,
frame end), so the command order is exactly the call order. A flush of >= 192 packets ends the pass, begins it
again with LOAD ops and `SECONDARY_COMMAND_BUFFERS`, records contiguous slices on up to 7 threads (main + 6
workers, one command pool per slice index and frame slot, reset at the slot's first `beginScene`), executes them
in order and restarts the pass inline; smaller batches record inline. Secondaries inherit no dynamic state, so
the device now tracks the viewport / scissor (`setViewport` / `setScissor`; Eden's flipped scene viewport and
the 2D scissor go through it). Verified: FREERIDE 4K, the intro and the garage pixel-identical to
`-noparallelrecord` (0 differing pixels, same build); validation clean (FREERIDE, intro, garage); a 2400-frame
real-time 4K drive: 29.9 M draws in parallel, VRAM flat, **59-79 fps**, clean exit.

**The impostor finding `[SHADER]` `[MEAS]`.** `-logdrawfamilies` (new: draws per shader family and distinct
mesh/material pairs, every 120 frames) showed TD_Impostor as the most repeated family. Its vertex shaders
(`otdu2_sob_tool TD_Impostor.sob <dir>` + `spirv-cross`) have **no world matrix**: vs_0 is
`gl_Position = ViewProj * in_Position0`, vs_2 the same plus View/Proj/LastView outputs - the positions are world
space, the retail engine builds them per instance (`FUN_009969e0` -> `FUN_00996820`, pmi_streaming_spec 4). The port
drew the model-local quad per instance, i.e. every far tree ~30 km away at the world origin: `-hidematerial _H-I`
changes 0 pixels. They now draw the way the shader wants (`EdenWorld::addImpostor` / `drawImpostors`,
`EdenModel::Item::ImpostorStreams`; `-noimpostors` skips them, `-logimpostors` logs cards / UVs / sizes):
- **batched**: each visible instance's card transformed on the CPU into one transient vertex buffer per material and
  pass, one draw each (~20 per pass instead of ~800);
- **lit** `[CODE]`: retail keeps an ambient light object at renderer +0x45bc (vtable 0xf2c310, built at 0xd82170)
  whose colour `FUN_00d769e0` writes from the weather's `impostor_ambient` every frame (0xd769fb..0xd76a2e); the
  card's `__AmbLight_Ambient_0` = that colour (0.62 grey in the intro weather), `_1` = 0 `[INF]`;
- **camera-facing** `[CODE]` + `[DATA]`: `FUN_00996820` turns each impostor by the camera angle quantised to its
  view count (cos / sin at 0x9968f0 / 0x996912). The card is a YZ-plane square (front face -X) sized to the tree's
  bounding sphere (V_PPin_C crown top 24.3 m vs the LOD0 top 23.2 m); its yaw is replaced so -X faces the camera;
- **atlas view** `[DATA]`: the textures are a strip of N-1 side views + one top-down view (v_ppin_c_h-i.2db 512x64
  = 7 + 1, v_hpin_a_h-i.2db 2048x128 = 15 + 1; the card's U spans 1/N). The side view nearest the camera angle in the
  instance frame is selected `[INF: cell 0 = the camera on the card's front, counter-clockwise]`.
Cost: +0.4-0.5 ms at 4K High. The far trees then take distance haze like the hills behind them (a `-nohaze` A/B
removes it; it is the haze, not the cards). `docs/images/impostors_far_trees_intro.webp` (intro frame 1200, before /
after), `impostors_far_trees_zoom.png`. Open: the 0.33 -> 1 dissolve against LOD0 (ps_1 / ps_4,
`gDissolveTexSampler`) and the exact cell order.

Remaining profile before the parallel step (exclusive): NVIDIA driver command recording ~31 %, `recordDrawImpl` 10 %, `fillConstants` 9 %,
`drawInstances` 8 %, `EdenModel::getSurfaceHeight` 3.4 % (game-side), heap ~5 %. **Trap**: the per-draw state a
deferred recorder would read is NOT stable — 103 runtime material-mutation sites in 17 files (car paint, avatar
faces, traffic colours, sign faces) — so constants and descriptor sets must be resolved at capture time on the
main thread; only the command calls can move to workers.

**The plan to 60 at High, biggest first** — each is CPU-only, so none of them costs image quality:

1. **Record the passes in parallel.** ocean mirror, the 3 shadow slices, prepass and opaque are independent
   world submissions (~25 ms of the 27.5). Secondary command buffers per pass (or per slice) on a worker pool,
   executed in order on the main buffer. Needs `EdenRenderer::recordDraw` thread-safe: a per-thread transient
   constant allocator and a per-thread `drawSetCache` / descriptor pool. On the i9-10850K (10C/20T) the frame
   would be bound by the slowest pass (~7 ms) instead of their sum. **Largest single lever.**
2. **Instance the `.pmi` draws** (6,449 main-pass draws, mostly identical model/material instances,
   `EdenWorld::drawInstances`): one instanced draw per model+material+LOD per sector. Cuts every pass at once.
3. **Shadow caster slices at reduced rate**: slice 0 every frame, slices 1-2 alternating (static world casters
   barely move between frames; the car is in slice 0). ~3-4 ms.
4. **Drop the Z pre-pass re-submission** (~6 ms): give the shadow resolve the previous frame's depth or move it
   after opaque (`docs/re/quality_profiles.md` §6.4).
5. Ocean mirror every 2nd frame is already a setting (`ocean_reflections = 2`, -2.9 ms) if a step is still short.

1 + 2 together should land 4K High under 16 ms with the GPU still at ~4-9 ms — the room grass/far vegetation
(P3) will need.

## 0. Session 73 (2026-11-02, `cline`) — the FREERIDE HUD was 11.5 ms, and the quality profiles

Read `docs/re/quality_profiles.md` for the presets, the knobs and the full validation table. The head-line
findings, same machine/spawn as §1 (`-tp 21796.3,24188.1 -snaproad 150 -carlod 4`, 1280x720, `-fixedtimestep`,
frames 240-480):

- **The main pass was the minimap, not the Flash HUD.** `-loghudtimes` (new) splits `FreeRideHud::draw`:
  the HUD / GAUGES movies cost **0.4 ms**; `drawMinimap` cost **11.5 ms** because it ran Sutherland-Hodgman
  clipping over every road triangle inside a 570 m square each frame (20k `triangleCells` lookups + a vector
  allocation per triangle). Fix: `Renderer2D::setClipRect` (GPU scissor, same pixels) + the map vertex list
  rebuilt at ~20 Hz and cached (`drawMinimap`). **HUD 11.5 -> 0.6-1.7 ms**, FREERIDE `main` 11.0 -> 2.2 ms,
  the whole frame **41-42 -> 34.8 ms** (24 -> 29 fps). `-no3d` HUD-only frame 12.6 -> **2.9 ms**.
- **The opening scene's 31 PNJs are now culled per pass** (`EdenRenderer::isSphereVisible`,
  `IntroCinematic::drawSets`; the avatars and cars only — sets and props still draw). The intro's offscreen
  passes are ~10 % cheaper, and the fixed-timestep frame 400 screenshot is **pixel-identical** (mean 0, max 0)
  against the before-build image. The old "prepared and reverted" fix in `docs/STATE.md` is now verified.
- **Draw counts**: `-drawfrom 0 -drawcount 1000000` reports **6,449 draws in the main pass**, 330-817 in the
  blended / planar / env passes and ~4-5k in the prepass / shadow / ocean passes — ~25k submissions per frame.
  This is why High is still 35 ms at 4K: the GPU is idle (2.5 ms at 720p), the CPU records.
- **Ruled out**: shadow distance 50 % (shadows 6.7 -> 6.7 ms; the 3 slices, not the range); render scale as a
  CPU fix (only GPU time); the Flash HUD (0.4 ms).
- **Profiles** (`Options > Advanced` row + `-profile`): High / Medium / Low / **60 fps** / **Steam Deck** /
  **Legion Go**. The 60 fps preset at **3840x2160 native** measures **13.3-15.1 ms (66-75 fps)**; Low at 4K
  measures **10.9-11.1 ms (90-92 fps)** at 70 % render scale; Low at 720p **10.5 ms (96 fps)**. The handheld
  presets pin 1280x800 / 1920x1200 and cut draw distance to 50 %, render scale to 67 %. New knobs:
  `hud_quality`, `ocean_reflections`, `draw_distance`, `shadow_distance`; all are in `options.ini` too.

What is left for a 60 fps **High** is structural: fewer draws (instancing / merged static batches / the P3
LOD1+ impostors), shadow slices at half rate, and eventually removing the Z pre-pass's world re-submission. The
next agent should start at `docs/re/quality_profiles.md` §6 and `docs/PERF_PLAN.md` P2/P3.

## 1. Where it stands


Machine: RTX 5070 Ti, `-fixedtimestep`, default quality (LOD factor 4, 16 sectors), 1280x720 window.

**There is GPU timing now** (`-loggputimes`, session 50): one timestamp pair per pass, read back two frames
later with no host stall (the `-lumalog` trick), averaged over 120 frames, printed as a `[perf] gpu:` line next
to the CPU `[perf] scene` one (`-logscenetimes`). The head-line result `[MEAS]`: **the port is heavily
CPU-recording-bound** — the whole frame costs ~3 ms of GPU time against a ~42-46 ms CPU frame at the FREERIDE
`-tp` spawn below; the GPU idles through ~93 % of the frame. Every pass costs ~8-10x more to record than to
execute, which puts P2 (per-draw CPU cost) first and makes P3 affordable later (far vegetation is a GPU cost,
and the GPU has the head-room).

| Pass | CPU (`[perf] scene`) | GPU (`[perf] gpu`) | ratio |
|---|---|---|---|
| ocean | 7.7 ms | 0.90 ms | ~9x |
| prepass | 8.0 | 0.90 | ~9x |
| opaque | 8.0 | 0.94 | ~9x |
| shadows | 6.7 | 0.64 | ~10x |
| env | 1.4 | 0.22 | ~6x |
| planar / blended / post | 0.0-0.6 | 0.00-0.03 | — |
| main (blit + HUD/Flash) | (in `main` CPU split) | 0.02 | — |
| **frame total** | **42-46 ms** | **~2.9 ms** | **~15x** |

[MEAS] FREERIDE, `-tp 21796.3,24188.1 -snaproad 150 -carlod 4`, steady windows (frames 240-600), run
back-to-back twice — the two runs agree to ~5 %. At 4K the pixel-bound passes scale ~5.3x: still only ~15 ms
GPU — 60 fps at 4K is *possible*, the CPU frame is what stands in the way.

**How much RT stress the idle GPU can take** (the owner's question, 2026-10-30): `-rtworld -rtao <samples>`
traces ray-traced AO at ~0.12 ms per ray per pixel — 16 rays doubles the GPU work (1.8 ms), 64 quadruples it
(7.6 ms), 256 costs 30 ms and crosses the CPU frame: **the GPU stops being idle at ~220 rays/pixel**. Full
table + caveats in `docs/re/ray_tracing.md` §6.

CPU-side table (session 48 numbers re-measured on the current tree, same command):

| Scene | avg frame | split | spikes |
|---|---|---|---|
| FREERIDE drive (`-gm FREERIDE -skipcine -autoplay "60:hold=accel" -tp 21796.3,24188.1 -snaproad 150 -carlod 4`) | **42-46 ms** | scene above; `main` the rest | frame 1: 613 ms; sector loads `[hitch]` up to 380 ms |
| AVATAR_CHOICE (`-intro`, the house walk) | 4.5-7.3 ms | ocean 0.32 / prepass 0.30 / opaque 0.34 GPU — an interior, ~1.1 ms GPU | first window max 326 ms (load) |

[MEAS]. Session 48's "15.2 ms FREERIDE" was a lighter spawn and an older tree — **location changes the CPU cost
~3x; always quote the command with the numbers.**

Logs to read: `[perf] frame N` and `[perf] draw split` every 120 frames, `[perf] scene` / `[perf] gpu` with
`-logscenetimes -loggputimes`, `[perf] hud` with `-loghudtimes` (the FreeRideHud parts: flash / minimap / gauge /
zone+fuel / prompt / gauge tex / gauges / general), `[hitch]` lines (threshold `-hitchms <ms>`), `[perf] vram`.
**Never time with `-vk_enable_validation_layer`**: it turned 15 ms frames into 500-1000 ms (session 48). **The
game log is overwritten every run — pass a distinct `-log <path>` per run (or grep immediately), or a later run
quietly becomes the one you read** (this cost session 50 an hour: an intro run's numbers read as FREERIDE's).

## 2. The work, in order

### P0 — Measure the GPU — **done (session 50)**
- `RenderDevice::beginGpuPass` / `endGpuPass` (`source/render/vulkan/render_device.*`): a timestamp pair per
  named pass into a per-frame-slot `VkQueryPool` (2 slots, `vkCmdResetQueryPool` at command buffer start), read
  back in `beginFrame` when the slot's fence has been waited (the `-lumalog` pattern), averaged per name over
  120 readbacks and printed as `[perf] gpu:`. Gate: `-loggputimes`; off (and pool-free) otherwise, and off when
  the queue family has no timestamps. The eight offscreen passes are bracketed in `EdenSceneView::renderOffscreen`
  next to the CPU marks; `beginMainPass`/`endFrame` bracket the swapchain pass ("main": scene blit + Flash HUD).
- Trap for the next editor: `endFrame` sets `bFrameStarted = false` before its tail work — the `endGpuPass`
  there must run *before* that flag drops, or the last pair never gets its end timestamp and every readback
  returns VK_NOT_READY (all 17 other queries available, one missing kills the whole `vkGetQueryPoolResults`).
  That cost session 50 a debug cycle.
- Done: `[perf] gpu:` lines in FREERIDE and AVATAR_CHOICE, numbers in the table above.

### P1 — Kill the streaming hitches (the biggest felt win)
- **2026-11-04 (`win`) status `[MEAS]`**: texture uploads are batched (`RenderDevice::getUploadBatch`, one fence
  per batch, no host wait; `-syncuploads` = the A/B) - frame 1 went from 2,149 waits (320 ms) to 0, but the
  frame total did not move, and a running sector load only waited 3 ms: **the per-texture wait below was not
  the felt hitch**. What was: first-sight **pipeline compiles** (now saved in `<userdata>/pipeline_cache.bin`,
  frame 1 552 -> 156 ms with the driver cache off) and **road-sign faces composed on the main thread** (~20 ms
  each, now on a worker, `-syncsignfaces` = A/B). Left: the sector load itself, ~52 ms of update at
  `-tp 21796.3,24188.1` frame 1132 (`EdenWorld::loadSector` + `VehicleWorldCollision::loadSector`) - the second
  bullet below is the next step. `[hitch]` lines now print the frame's uploads / submits / waits.
- **2026-11-04 later (`win`) `[MEAS]`: the crossing hitch is gone** - load-ahead streaming (`GMFreeRide::streamWorld`,
  `EdenWorld::preloadOne`, hidden until the square reaches it; `-nopreload` A/B), a RAM file cache with a background
  reader (`GSFile::prefetchFile`, `-filecachemb`), and an XZ grid for the collision height queries (update
  1.2 -> 0.4 ms). Frame 1132: 76-91 ms -> under 33, pixel-identical. Left: a preload step is still main-thread
  work (4-14 ms); the worker decode in the second bullet below only matters if those frames are felt.
- **Every texture upload waits for the GPU**: `RenderDevice::createTexture…` → `endImmediateCommands`
  (`render_device.cpp:1617`) does `vkWaitForFences` per texture [CODE]. A sector bank has hundreds of 2DBs, so a
  sector load is hundreds of submit+wait round trips on the main thread — the 100-500 ms `[hitch]` frames at
  sector loads.
  Fix: batch a bank's uploads into one command buffer + one fence; better, a staging ring uploaded on a transfer
  queue and only waited on when the bank is first drawn.
- **Sector loading is synchronous** (`EdenWorld::loadAround` → `loadSector`, `source/world/eden_world.cpp`):
  move the file read + 2DB/3DG decode to a worker thread (gpFile is main-thread only today — read bytes on the
  main thread and decode on the worker, as `radio_player.cpp`'s preload does), and create GPU objects in budgeted
  slices per frame. The intro already looks 2 s ahead on the camera path (`-cinelookahead`), FREERIDE could do
  the same along the car velocity.
- Done when: a FREERIDE drive across 3+ sectors and the full C_INTRO show no `[hitch]` above ~33 ms after frame 60.

### P1b — Occlusion culling (measured 2026-11-04, `win`, `-occlusionprobe`) `[MEAS]`
- 76-80 % of the depth pre-pass draws are hidden behind nearer geometry (Ibiza town 79.8 %, the benchmark spot
  75.9 %, two drive frames 77 / 76 %), conservative 8x8-tile hierarchical-Z test on the same frame's depth.
  Pre-pass + opaque = ~5 ms CPU a frame: upper bound of the gain ~3.5-4 ms. Plan: previous-frame depth + that frame's
  view-projection, hidden twice in a row before skipping; or a CPU occluder rasterizer. Also: the ocean reflection
  pass draws ~2,000 draws (2.5 ms) in town with no sea on screen. Details in `docs/state/main.md`.
- **Built 2026-11-05** (`EdenOcclusion`, `-noocclusion`, `-nooceanskip`): town 15 -> 9.2 ms, drive 16 -> 10.5 ms at 720p, pixel-identical in the checks. Static world only; the moving objects are drawn after the depth copy and never culled.
- 4K High (2026-11-05): ~16.5 -> ~11.8 ms on the drive. The depth is reduced to tile maxima on the GPU (`depthmax.frag`) before the readback.

### P2 — CPU recording cost per draw — **the confirmed lever** (session 50: every pass records ~8-10x its GPU time)
- **Step 1 done (session 50)**: `EdenRenderer::recordDraw`'s descriptor set is now **cached per material /
  override combination per frame slot** (`drawSetCache`): the three transient constant buffers became
  `UNIFORM_BUFFER_DYNAMIC` bindings (shader-invisible), their offsets passed at `vkCmdBindDescriptorSets`, so
  the set is allocated and written once per material per slot instead of per draw. `-nodescriptorcache` is the
  A/B fallback. **Measured**: FREERIDE frames 38.8-42.5 ms with the cache vs 44.7-46.6 ms with
  `-nodescriptorcache` (~10 %, each pass -1.5-2 ms CPU, GPU identical), screenshot pixel diff mean 0.45/255
  (env-cube refresh timing noise).
- Next, cheapest first: dynamic UBO offsets are in — the remaining per-draw CPU is `fillConstants` (the
  register writes themselves), the transient alloc + memsets, push constants and buffer binds. Beyond that:
  cache the image-array set per material (textures do not change per draw — mind the per-frame overrides:
  shadow RT slot 15, projective / screen textures); then descriptor indexing (bindless) if needed.
- Multi-thread command recording per pass (shadow cascades are independent) once the per-draw cost is down.
- Done when: `[perf] scene` shadows/prepass/opaque each drop measurably on the FREERIDE run above.

### P3 — Far vegetation the affordable way (also fixes the intro's bare hills)
- Session 48 part 5 in `docs/STATE.md`: past lod[0] the port draws **full LOD0 meshes** where the game draws
  `TD_HardwareInstance` meshes and `TD_Impostor` quads (`docs/re/pmi_streaming_spec.md` §4, §9 row 3). Load the
  LOD1+ library objects and pick the LOD per instance in `EdenWorld::drawInstances`; instance the
  HardwareInstance draws (one draw per model per sector, not per tree).
- Then the draw range can grow to the game's (measure it on the 360 recomp, CLAUDE.md §7) without the ×2.3 cost.

### P4 — Smaller items worth checking
- Pipeline creation at first draw (`EdenRenderer::getPipeline`, `vkCreateGraphicsPipelines` ~1062): a
  `VkPipelineCache` exists (`render_device.cpp:456`) — check it is **saved to disk** between runs; a first-run
  frame-1 hitch of 300-540 ms suggests shader/pipeline compile.
- The ocean (2.2 ms CPU) and the env cube (re-rendered how often? `setEnvironmentCenter`) — confirm the env cube
  is not re-rendered every frame when the car is static.
- Frame pacing: `-novsync`/vsync behaviour at 4K — the owner's device-lost report had a 1036 ms draw at
  3840x2160 vsync (session cline 8).

## 3. Method rules (each cost a session before)
- One change at a time, A/B with the same command and `-fixedtimestep`; quote the `[perf]` lines before/after.
- **One `-log <path>` per run** — the default `opentdu2.log` is overwritten by the next run, and grepping a
  stale log reads the wrong experiment's numbers (session 50).
- Screenshots before/after for anything that could change the image (`-fixedtimestep` frames are reproducible;
  compare with a pixel diff, see STATE session 48 part 2).
- Stop only your own tree's exe by path (CLAUDE.md §2) — runs of other workspaces may be timing too.
- Update `docs/STATE.md` (session template, CLAUDE.md §9) and this file's table as numbers change.
