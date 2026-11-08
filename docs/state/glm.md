# Session handoff — `glm` (GLM workspace, branch `glm`)

**Only the GLM agent writes this file.** The other agents have their own shard
(`docs/state/main.md`, `docs/state/cline.md`), so two branches never edit the
same handoff file — `docs/STATE.md` used to be edited by both sides of 32 of
the last 38 merges (every session prepends to the same block), which is what
kept stopping `E:\OpenTDU2-main\update-and-rebuild.bat`.

## Session 2026-11-08 (81, `glm`): the TD_TallGrass retail shader lifts and draws - `-grasstallgrass` now renders the real grass path (car-anchored feed, hemisphere-normal lighting, noise tint, distance pop), not the Gouraud stand-in

Continuing the open list from the grass commit (3a6d385): "the TallGrass VS
lift, noise.rle wind, density vs retail, grid orientation, perf line". The VS
lift was the headline.

### What changed
- `tools_probe/dxso_dis.py` — the disassembler's opcode table was wrong from
  index 3 up (it dropped `SUB`, shifting every mnemonic: its "rcp" was MUL,
  its "pow" was DCL, its "m4x4" was FRC). The fixed table follows the vendored
  DXVK decoder (`third_party/dxvk/src/dxso/dxso_enums.h` — `Cnd = 80, Def = 81`,
  dcl usage in bits 0-3 / index in 16-19, CTAB offsets relative to the struct
  after the fourcc), and it now dumps CTAB struct member → register maps.
  The bytecode was stock SM1 all along; the instruction stream of session 81's
  predecessor was trustworthy, only the names were shifted.
- `tools_probe/dxso_lift.py` (new) — symbolic SSA lift of any SM1 shader with
  the CTAB member names substituted; transcripts in `_grass/vs0_lift.txt` /
  `_grass/ps0_lift.txt`.
- `source/world/eden_world.{h,cpp}` — `loadGrassAssets` builds the TallGrass
  vertex layout when `-grasstallgrass` (one 64-byte stream: position/color/
  texcoord/tangent as 4 × f32, usage 0/10/5/6); `drawGrass` branches the emit
  and the draw. The old `CARPOS`/`.LPHADIS` spellings were wrong twice over:
  the CTAB names are `CarPos` / `alphaDist` / `scaleDist` / `sectOff`
  (`.LPHADIS` = alphaDist, `.CALEDIS` = scaleDist), and fillConstants matches
  case-sensitively.

### The lifted law (grass_spec.md 4.1/4.2, the full constant map is there)
- vs_0-3 = the plain family, vs_4-7 read `Whirlwind` (c81); NO permutation
  reads `Time_` — the wind rides in through `UserVector0` (frame block).
- The draw runs with **World = identity** and absolute world-space vertices:
  the law composes `InvView` with `__World_View__` around the corner
  (`WV·(corner + InvView·delta)`), which only cancels for World = I.
- Vertex packing: v0 = corner world XZ (Y=0), v1 = (corner u, height fraction,
  hemisphere face normal in zw), v2 = (atlas u, Y addend = ground + corner
  height − lever), v3 = (blade lever, wind amp, noise amount, -). The corner
  renders at `v0.y + v3.x + v2.y`.
- Position law: a view-X shear at ±1/√2 (the hemisphere normal's view-X sign,
  scaled by lever·(2h−1)); the car flatten `t` from CarPos projections with
  per-corner deltas that **shear the card in depth by 2·lever** (root vs tip
  magnitudes always differ by exactly the lever — structural, measured: full
  height tore cards into screen-edge streaks); spatial wind phase
  `sin(frac((x−z+phase)/2π)·2π−π)` with tips-only sway; the fade
  `t = 1 − (len−a.x)/a.y` (plain rcp) plus the pop `oT0.xyz *= (t ≥ 0.5)`
  collapsing far cards to a degenerate point; the sector uv
  `(worldXZ − sectOff)/768` computed from the *un-bent* corner.
- ps_0: atlas × noise tint × `((Ambient + oT5)·oT2.x + decode(s15.G,B))`, fog
  lerp, `alpha = atlas.a·t`. **The shadow decode reads G/B** (packed
  additional lighting) — TD_Vegetation's PS reads R; the port's buffer has
  G/B = 0, so brightness rides the lit term (why the first runs rendered
  black).

### Verified
- `-grasstallgrass -tpin 19584,24960 -clock 12` (farm sector), build 510:
  `docs/images/grass_tallgrass_s81.png` (the TD_TallGrass path: green-yellow
  crossed-card tufts with the noise patch tint) vs
  `docs/images/grass_tallgrass_off_s81.png` (the default Gouraud path at the
  same build — unchanged regression, same 9000 cards, guid 73078865ca37336d).
- Validation layer (`-vk_enable_validation_layer`, same teleport): 0 VUIDs in
  the frames; the one shutdown `vkDestroyDevice-device-05137` notice also
  fires without the grass flag (pre-existing, not grass-related).
- **Perf line** (`[perf]` frame 240, steady state, same teleport, 1280×720):
  grass on = 16.8 ms avg (draw 16.4), `-nograss` = 8.2 ms (draw 7.9),
  `-grassdist 60` (1998 cards / 8k tris vs 9000 / 36k) = 10.0 ms — ~0.9 µs
  per card, linear in card count: the radius/card-cap flags are the perf
  lever, no fixed overhead.
- Feed values that matter (all [INF], the .2dm ships none of them):
  `alphaDist = (dist, 2·dist, 0, 0.3)` keeps t ∈ [1, 1.5] — no pop inside the
  radius, tips lit 1-2×, roots dark; `UserVector1 = 1`; `UserVector0.x = 0.7`;
  the blade lever at **1/8 card height**; noise amount 0.35 (noise texture
  mean luminance 0.392 → the tint averages ~0.6).

### Ruled out (with the test that ruled it out)
- The empty first render: the pop (`t ≥ 0.5`) killed every card — my first
  alphaDist read assumed a sqrt(rsp) normalization in the fade; the fixed
  decode shows a plain rcp, so a = (dist−900, 900) put t ≈ 0.15 everywhere.
- The streak render: not the shear bend (±0.35 m) — hand-decoding the
  sign-extraction block (`slt r0.z, -r1.y, r1.y` etc., offsets 0xa04-0xa34)
  showed the car-flatten's per-corner depth push `∓3·delta·sign(BC)` with
  `BB = min(−viewX·0.05, 1)` amplifying the right screen edge 8× at far
  scaleDist; shrinking the lever scales the whole chain down.
- The dark render: not the atlas/UV (cells and V convention match the
  Gouraud baseline), not the noise alone (tint ≥ 0.35) — the 4× UserVector1
  probe blew the tufts out white, pinning the brightness on the lit term and
  the G/B shadow decode.

### Not done / next
- The wind carries no time (`UserVector0` is constant) — wire
  `wind_continuous_grass_amplitude`/`frequency` from the weather into it;
  `noise.rle` (124 KB, RLE wind frames) is still undecoded
  (`source/world/eden_world.cpp`, the `UserVector0` feed in `drawGrass`).
- The car flatten is inert by design in the port feed (lever 1/8 +
  `scaleDist.z = 0`); recovering it needs the retail per-frame
  `CarFwd.w`/`CarRight.w` law — the PS3 generator twins
  (`get_func.py 0x6e05b4` area) are the route.
- What writes the shadow buffer's G/B channels retail-side (street lamps?)
  is untraced.
- The whirlwind permutations (vs_4-7, `Whirlwind` c81) — selector + feed —
  unexplored. Density/size vs a retail capture, grid orientation A/B, and
  the perf line stay open from the grass commit.

## Session 2026-11-07 (80, `glm`): cars ride in the dynamic environment cube (retail's WITH_CARS variant) - they show in the mirrors / paint / glass reflections now, and the cube render no longer samples the image it writes

The owner asked: *"can we improve the game's car reflections and car mirror reflections they look ok but
look like theres a slight update delay and also cars also don't seem to show up in mirrors"*. Two findings:
**(1)** the cars were excluded from the Environment pass entirely (`bScenePass` in the FREERIDE draw
callback), so the cube that the mirrors / paint / glass sample never contained any car; **(2)** the update
delay is the env-cube refresh throttle (200 ms driving / 500 ms parked, `renderOffscreen` — left alone, see
Not done). Retail evidence for (1): `postfx_spec.md` 1.5's layer table has
`ENVMAP_* (6 faces x {plain, NO_CARS, WITH_CARS})` — a WITH_CARS cube variant existed; the port only ever
rendered the plain one.

### What changed
- `source/game/freeride/gm_freeride.cpp` draw callback: traffic / aircraft / police / opponents also draw on
  `ePass::Environment` now. The player car stays out — it is the cube's centre (`setEnvironmentCenter`), its
  own faces would fill with itself — and the planar Reflection pass keeps the scene-only rule.
- `source/render/eden/eden_scene_view.{h,cpp}`: the cube faces render into a new scratch cube target
  (`CarEnvironmentScratch`) and are copied to the sampled one afterwards (`RenderDevice::copyTexture`) — a
  sampled image must not also be the render target of the pass drawing the cars that appear in it. This also
  fixes the **wet ground** (puddles' slot-7 cube, `eden_world.cpp` `updateWetGround`) sampling the cube while
  the faces render — pre-existing, never flagged because the sampling view and the attachment views are
  different `VkImageView`s. The sampled cube is rendered into directly only for the first refresh (nothing
  samples it yet). Retail ping-pongs two cube RTs instead (DAT_011c9ef0: cube 1 + cube 2,
  `rain_shadow_spec.md` A.5); the copy is the same idea without the one-refresh staleness [INF].
- `source/render/vulkan/render_device.{h,cpp}`: new `copyTexture` (image→image recorded into the frame's
  command buffer, per-layer regions, barriers covering **all 6 layers**), and `createCubeRenderTarget` gains
  `TRANSFER_SRC | TRANSFER_DST` usage.
- `-showenvscratch` joins `-showenvface`: reads the scratch cube — tells a stale copy from an empty face.

### Verified
- A/B cube faces, headless (`-trafficpark -showenvface F -fixedtimestep`, shot at frame 240/400): new build —
  the parked ambulance + car in face 5 (`docs/images/env_cube_cars_face5_s80.png`) and the teal car in face 4
  (`docs/images/env_cube_cars_face4_s80.png`); old build (git stash rebuild) — the same face 5 street with no
  cars (`docs/images/env_cube_cars_face5_old_s80.png`). The faces are world-aligned, so which face holds the
  parked trio varies with the spawn yaw run to run (the one trap of this session: face 5 had cars in the first
  run and not in the next three — the cars had moved to face 4, the copy was innocent).
- Validation layer (`-vk_enable_validation_layer -trafficnear`, 300 frames): **0 VUIDs**. It caught two real
  bugs mid-session: the cube images lacked the TRANSFER usages (66 VUIDs), and the copy barrier covered layer
  0 only — a cube face's render pass transitions only its own layer to the final layout, so the barrier needs
  all six (22 VUIDs).
- `[perf]` (`-trafficnear`, 720 frames, no validation): old 8.4 / 8.3 ms avg vs new 8.3 / 8.2, offscreen
  7.3 / 7.2 — the copy is free.
- Cockpit: the interior rear-view mirror samples the cube live (`docs/images/mirror_cockpit_s80.png` — the
  road behind while parked; `docs/images/parked_traffic_main_s80.png` shows the parked trio the cube now
  sees). Catching a car inside the small mirror on a headless shot is shot roulette (4 attempts missed); the
  slot law is the garage mirror's CUBECAR binding (session 45), the same TD_Mirror path.

### Ruled out (with the test that ruled it out)
- The copy dropping the cars: `-showenvscratch` read the scratch cube directly — same content; probes in
  `Traffic::draw` showed it runs at width 256 with 3 visible agents per face (the empty face was the cars
  landing in another face, not a lost draw).
- Occlusion / frustum culls eating the car draws: `isBoxVisible`'s occlusion grid is cleared before
  `drawObjects` (`eden_scene_view.cpp` ~1201 "the moving objects are never culled") and the frustum planes
  rebuild per `beginScene` — not the gate.

### Not done / next
- **The owner's "slight update delay"** is the refresh throttle (200 ms moving / 500 ms parked,
  `eden_scene_view.cpp` `renderOffscreen`, the coherence fix 568fea8). `PERF_PLAN.md` already lists "confirm
  the env cube cost" as open: measure the env pass (the `gSceneTimes` Env bracket / `-logscenetimes`), then
  tighten the gap as far as the CPU frame allows — the 6 faces are full scene draws on a CPU-bound frame.
- Retail's WITH_CARS vs NO_CARS vs plain distinction (which consumer got which cube) is untraced [ENC]; the
  port uses one cube with cars everywhere.

* **Append the new entry at the top**, directly under the rule, newest first, in
  the `## Session <date> (<ordinal>, `glm`): <one-line result>` shape of
  `CLAUDE.md` §"The session-entry template" (the shape matters more than the
  length).

## Session 2026-11-07 (79, `glm`): aspect-ratio support (4:3 -> ultrawide) - one unified UI viewport mapping (Stretch default / Fit band), a global FOV scale option, the Screen ratio auto-derivation with a menu row, and the world-marker projection now exact at every window aspect

The owner asked: *"is it possible we can add different formats for the game to
support 4:3, 16:9, 16:10 etc... with widescreen and ultra widescreen and can we
have FOV Scaling?"*. Until now only exactly-16:9 windows behaved: the native
1280x720 canvas layers letterboxed (`Renderer2D::begin` Min-scale) while the
Flash movies stretched to the window (`kRects` retail bands), so at 16:10 the
minimap content drifted out of its bezel and the fuel bar left the odometer;
`GMFreeRide::projectToCanvas` / `MapScreen::projectToStage` hardcoded
`tanHalf * (16/9)`; the retail `SetScreenRatio` value was read but never
auto-derived; no FOV option existed anywhere.

### The design (decision: "Stretch (fill)" default per the owner's "I want it to fill"; the planned third "Extend" mode was DROPPED - see the trap below)

- **One window mapping, two modes** (`options.ini ui_layout`, 0 default):
  **0 Stretch** = the retail mapping everywhere (the canvas covers the window
  like the movies always did - `Renderer2D::begin` takes the fill path for the
  backbuffer, offscreen targets keep their own fit); **1 Fit** = every 2D layer
  into a centered band of its own aspect, zero distortion, the 3D world stays
  full-bleed behind (bars contain world, not black).
- **The five copy-pasted `kRects` tables** collapsed into
  `FlashRetailStageRect` (the retail table, window-independent - the
  canvas<->stage conversions still use it through the new
  `FlashCanvasToStage` / `FlashStageToCanvas`, which compose canvas -> window
  -> stage through both mappings) and `FlashStageRect` (the mode-aware rect
  `beginStage` / `FreeRideHud::beginStageRect` draw with; its Fit path fits the
  stage's *visible band*, not the overflowing retail rect - mode 1's rect is
  y0 -1/6 h 4/3, a square if taken literally).
- **FOV scale** (`fov_scale` 50..150 %, `-fov <pct>`): one choke point,
  `EdenEffectiveFov(fov, fovScale, aspect)` in eden_scene_view.cpp - the
  camera's own scale (new `EdenSceneView::Camera::FovScale`, negative = the
  global option) times the retail narrow-aspect law, applied in `setupScene`
  before the projection; global per the owner's pick (cinematics and photo
  mode included). `MapScreen` pins 1.0 (its framing math assumes the authored
  60 deg lens) and its flat-map distance now follows the effective lens. The
  RT AO frustum reads the new `effectiveFov` member instead of raw
  `camera.Fov`.
- **Markers**: `projectToCanvas` / `projectToStage` project with
  `EdenEffectiveFov` at the *window* aspect, then map NDC -> window fraction
  -> canvas/stage through the UI rects - identical to the old math at 16:9
  Stretch 100 %, exact everywhere else. The HUD movie placements (`canvasY +
  120` sites: enter tile, world markers, chase timer) and the map's
  `stageY - 120` conversions all go through the composing converters (x stays
  1:1 in every reachable mode).
- **Screen ratio** (`screen_ratio` 0 Auto / 1 = 16:9 / 2 = 4:3): auto derives
  from the window aspect (>= 3:2 reads the `16_9` arrangements, narrower the
  `4_3` ones); `FreeRideHud::applyScreenRatio` applies the `/HUD` label + the
  GAUGES `/:WhatFormat` at load and re-runs them live when the row changes
  (the GAUGES replay of root `on` re-places the dials). The stage rect and
  the arrangements read the same `GetEffectiveScreenRatio()` so they cannot
  disagree.
- **Menu rows**: Screen ratio / UI layout / FOV scale on the Image settings
  page (`kDefaultRows`, `extraRowText`, `stepExtraRow` - they do NOT clear the
  quality profile) and in the native Advanced page (`kRows` + the
  `kScreenRatioRow/kUiLayoutRow/kFovScaleRow` exemptions). Pause-menu mouse
  hit-testing now converts window px -> canvas -> stage (was exact only at
  1280x720).

### Files
- `source/game/game_options.{h,cpp}` - UiLayout / FovScale / ScreenRatioOverride,
  kFields, `-fov`, `GetEffectiveScreenRatio`, `GetUiCanvasRect`.
- `source/render/renderer_2d.{h,cpp}` - mode-aware `begin` (backbuffer only),
  target-size accessors.
- `source/flash/flash_player.{h,cpp}` - `FlashRetailStageRect` / `FlashStageRect`
  / `FlashCanvasToStage` / `FlashStageToCanvas`; `stageRect` / `beginStage`
  mode- and ratio-aware.
- `source/render/eden/eden_scene_view.{h,cpp}` - `Camera::FovScale`,
  `EdenEffectiveFov`, `effectiveFov` member, AO frustum.
- `source/game/freeride/freeride_hud.{h,cpp}` - `applyScreenRatio`, the
  converter rewrites, `hudStageWidth/Height`.
- `source/game/freeride/gm_freeride.cpp`, `source/game/map_screen.cpp` -
  marker projection, map FOV pin, canvas/stage conversions.
- `source/game/pause_menu.cpp`, `source/game/advanced_graphics.cpp` - the
  three rows.

### Verified (headless, `-fixedtimestep`, autoplay shots in docs/images/*_s79)
- 1280x720 baseline: unchanged look (ui_aspect_base_720_s79.png).
- **1920x1200 Stretch**: minimap content inside its bezel, fuel bar under the
  odometer, badge top-right - the layers fill together (ui_aspect_w10_stretch_s79.png).
- **1920x1200 Fit**: the fuel bar measures y 1104..1113 vs Stretch 1160..1171 -
  expected 1102.5 / 1158 from the band math (ui_aspect_w10_fit_s79.png; the
  "bars" hold the full-bleed 3D world by design).
- **1280x960** (auto -> 4:3 arrangement + mode-2 rect): the taller bezel with
  its chin, content aligned (ui_aspect_43_stretch_s79.png). **2560x1080**: HUD
  at the true window edges (ui_aspect_219_stretch_s79.png).
- **FOV** `-fov 150` / `-fov 50`: wide / telephoto, HUD untouched
  (ui_aspect_fov150_s79.png, ui_aspect_fov50_s79.png). Forced ini
  (fov 150 + ratio 2 + fit) drives all three keys at once.
- **Round-trip**: the Advanced page shows the three rows
  (ui_aspect_advanced_rows_s79.png); Left on FOV scale -> 95 %, close saves
  `fov_scale = 95` and the other two keys (kFields loop); load re-applies.

### Traps / open
- **The planned "Extend" (ultrawide edge-anchored HUD) was dropped**: the HUD
  is mostly Flash art with no 21:9 arrangement, so Extend would stretch the
  movies anyway and re-drift the native partners - Stretch *is* edge-to-edge
  (elements land at the true edges), Fit is the no-distortion escape. Two
  modes shipped.
- Forced 4:3 (`screen_ratio = 2`) on a 16:9 window shows the 4_3
  arrangement's own side rails (retail-faithful for that combination; auto
  never picks it above 3:2). Screen ratio 0 (the cpr "other" mode, full stage
  + black stripes) is unreachable through the new row - cpr 0 auto-maps to the
  aspect's mode; intro_cinematic's stripes gate still reads the raw cpr value
  (harmless: the stripes land outside the band).
- Under Fit, a forced-4:3 movie band (1.667) and the fixed 16:9 canvas band
  letterbox slightly differently (~24 px at 4:3) - the accepted niche; Stretch
  is exact for every ratio.
- `casino_slot_hud`'s offscreen panel and the `-loghudregion` debug dump keep
  their own assumptions (offscreen targets are mode-independent; the dump is
  mode-1-only by design).

## Session 2026-11-07 (78, `glm`): the garage test-drive scene works - the rev needle sweeps live with the throttle (the interior_rpm gauge table), the C_OUT close-up carries the retail ND_RPM sweep (the garage scene's 5-key heading track transferred onto the car's interior needle), the wheels roll during the C_OUTRO drive-out (the track velocity integrated into a new CinematicCar::setWheelSpin), and the drive-away got its audio back (the engine now runs during C_FIRST_OUTRO at the track's rpm/gear; the t=0 music bed fires - its position/clip question is open)

The owner's report (playing on the Legion Go, build 500): *"when enter the car
your told to rev it and the speedometer needle doesn't move also the speed
ometer seemed to of popped out from it's position being a little higher up
during the scene in the car upon exiting the garage before the test drive of
the car the wheels don't spin during exiting the garage but do after"*, then a
follow-up: *"there wasn't any engine sounds or anything after you exit the
garage there's short scene before you enter the free roam test drive that
scene had no audio"*. **None of it is Legion Go specific** - every symptom is
a mode-logic gap that reproduces on the desktop runs (16:10 only narrows the
horizontal FOV; the cockpit is 3D geometry at data-authored positions, the
garage shows no 2D HUD).

### The three garage symptoms (data + code)

- **Rev needle dead**: `CinematicCar::setDashboardNeedles` (the s77 needle
  driver) was only called from `GMFreeRide::tick` - the FIRST_WM seated state
  drove nothing. On top of that the retail `C_OUT` close-up's sweep is a
  **5-key Heading track on the garage scene's `ND_RPM` actor**
  (M_OUT_out.sce: hold 2.54..5.96 s, sweep to 8.64 s, `[DATA]` via
  otdu2_cine_tool) which `IntroCinematic::updateProps` ignores - a static
  prop (no skeleton) never follows its .sce Position/Heading tracks (it keeps
  its entity placement, which for AAA/ND_RPM is an underground editor totem -
  that is why the port shows the car's own interior bank everywhere).
- **"Speedometer popped out, a little higher up"**: the garage cockpit the
  port draws is the car interior bank (cluster pivot y 0.696 in car space,
  needles_spec 1.1); the retail garage scene carries its own baked cockpit
  (`AAA` + the `ND_RPM` needle actor, needle pivot 0.80 above the car origin)
  which the C_OUT close-up was framed for. With the sweep now transferred the
  close-up reads like retail (`_s78/cout1990/cout2020`); if the owner still
  sees a height offset against retail, the next step is swapping the visible
  cockpit to the scene's AAA at the C_OUT pose (needs the retail capture to
  A/B).
- **Wheels frozen on the garage exit**: `IntroCinematic::computeCarWorld`
  poses CIN_CAR by the 135-key .sce position track but nothing rolled the
  wheels - gm_freeride had already solved the identical case for
  C_FIRST_OUTRO (`outroWheelSpin`).

### The fixes

- `CinematicCar::setWheelSpin(radians)` (cinematic_car.{h,cpp}): the D_RIM_xx
  nodes + their tyre sibling rotate about +x of the car frame from rest
  transforms captured on the first call (hubs pinned; unload clears them) -
  the gm_freeride updateWheels cinematic law, reusable by every cinematic
  car. `GMFirstWalkMode` integrates the C_OUTRO track velocity per frame
  (`updateOutroWheels`) and resets at the C_OUT -> C_OUTRO hand-off.
- `GMFirstWalkMode::updateRevNeedle`: the seated throttle (with a first-order
  lag, rev up 6.5/s faster than fall 2.2/s `[INF]` engine inertia) through
  the car's `interior_rpm` hud.ini table (`loadRevNeedle`: gauge bank
  `frontend/hires/gauges/<_hud_file_name>.bnk`, the same AddHud law as
  FreeRideHud::loadGaugeBank) and `setDashboardNeedles(0, angle)`; `_rpm_min`
  900 / `_red_line` 8000 rpm from the California's DBCarPhysicsData row
  (`_red_line` is an **rpm in the DB** - the first read treated it as rad/s
  and produced a 76394 rpm red line; LoadVehicleSetup converts).
- `IntroCinematic::getNeedleSweep(outDegrees)` + the `ND_RPM` entity rest
  captured at prop load: the playing cinematic's ND_RPM Heading track sampled
  per frame, each key's rotation from the track's first key (the needle at
  rest) decomposed about the constant sweep axis -> the dial angle the mode
  feeds `setDashboardNeedles` while C_OUT plays; needle back to 0 at the
  C_OUT end. `-logneedle` prints `ND_RPM sweep t .. -> .. deg` (verified:
  0 -> 64 deg about a constant axis, t 2.54..8.21).
- The C_FIRST_OUTRO audio (the follow-up report): the engine never ran
  during the outro (`vehicleAudio.startEngine` was only in startDriving /
  finishWakeup) - now the Outro tick starts it and drives `outroRpm` /
  `outroGear` from the track velocity through VehicleDynamics' own laws (the
  gear-for-speed rule of vehicle_dynamics.cpp 721, rpm = |speed| / radius *
  ratio clamped, 396; `outroGearRatio` rebuilds the ratio from
  `setup.Gears` x `FinalDrive`), and the shared VehicleAudio update block has
  an Outro branch (Throttle 0.8 `[INF]` pull-away load). The music bed:
  `CinematicSounds::update` never fires an Fx event at **t = 0** (`bCrossed`
  needs `event.Time > previousTime`, `bStart` needs `previousTime < 0`, and
  FirstOutro::tick started from previous = 0) - `FirstOutro` now keeps
  `previousTime` (init -1, the IntroCinematic::Playing law) so
  `m_first_outro_CINEMATIC.wav` starts with the scene (verified
  `CinematicSounds: play 'm_first_outro_CINEMATIC' (14.32 s, client 22)`.
  Open: the bed is spatialized at the record position (36407.5, -5.9, 5747.6)
  = ~57 m from the cameras, beyond its 4/40/40 clip -> likely still quiet;
  the record's frame does not reconcile with any conversion tried (the Z-up
  car_wash_spec 10.4 artifact class) - needs the owner's retail A/B.

### Test shortcuts (the -fwmseated family)

`-fwmrev <0..1>` forces the seated throttle (needle A/B without input),
`-fwmout` jumps to C_OUT, `-fwmoutro` straight to C_OUTRO.

### Verified (all `-fixedtimestep`, autoplay shots in `_s78/`)

- Seated rev: `-fwmrev 1.0` frame 1830 - the tach needle at the red zone
  (9-10), the speedo at 0 (`docs/images/garage_rev_needle_s78.jpg`).
- C_OUT close-up: frames 1990 / 2020 - the needle sweeps ~5500 -> ~7800 rpm
  across the shot (`docs/images/garage_cout_sweep_early_s78.jpg`, `garage_cout_sweep_late_s78.jpg`), matching
  the logged 36.5 -> 64 deg.
- C_OUTRO wheels: the spoke pattern rotates between frames while the camera
  holds (`docs/images/garage_wheels_roll_a_s78.jpg` vs `garage_wheels_roll_b_s78.jpg`, 0.167 s apart at ~2.2 m/s) and the
  moving-camera pair agrees (`outro_wheel_400/410`).
- Outro audio: `CinematicSounds: .cin Fx at 0.00 ... play
  'm_first_outro_CINEMATIC'` now in the log (silent before - zero
  CinematicSounds lines), the engine runs from the outro start.
- The whole garage flow end-to-end at throttle 1.0 (run A): seated -> rev
  timer -> C_OUT -> C_OUTRO -> story step 2 FREERIDE, no stalls.

## Session 2026-11-07 (77, `glm`): the Driving Controls page works - the binding list fills with the real bindings (glyph + "Button N" / "Stick 1 left" + the device column, the retail texts), Up / Down navigates and scrolls the 13-action list, Enter opens the bind dialog and the next raw key / pad button rebinds the row live, Delete unbinds, Y saves to `<user data>/controls.ini` and Back restores - the retail chrome (LB / RB badges, the colored bottom bar, the focused-row tint) renders because the page's own machinery runs once the game feeds it

The owner's report: *"it seems there is it's in the controller settings which i
don't think we have implemented properly is it possible you could do this?"*
with the retail Driving Controls capture. The port opened the page
(`opts:cont:bind:open` fired, unanswered) and showed an empty list over a
loading spinner.

### What the movie says (all `[DATA]`: the S17243 page + the S17156 list
sprite + the line / binder_key clips, and the button map)

- The Controls page (`/options/controls_menu`) switches on the selected
  device and lands on the default branch: `_root.WhatListe = 'driving'`,
  `content_menu.pc.gotoAndPlay('loading')`, FSCommand `opts:cont:bind:open`.
  The list page (`content_menu/pc`) creates `list = new ASFlashList(3, 9,
  'controls_list', this)` on its loading frame and stops (the spinner is its
  own `loading_anim` child).
- The 9 view lines (`line1..9`) read their texts from **their own variables**
  (`control_name` / `control_bind` / `control_device` - the row fields are
  variable-bound to them, the gamma rows' param_title law) and carry a
  `lock` flag (an UNSET lock reads as locked: the page's PlayAnimValidate
  answers sndp:wrong and never opens the binder) and an `empty` flag (the
  tail rows play the movie's 'empty' state). The pad glyph is the icon clip
  at `line<i>/content/command_bind/icon` (frames a / b / x / y / lb / rb /
  lt / rt / select / start / up / down / left / right / the LS-RS stick
  arrows / clic_ls / clic_rs / horn).
- The page's key button (17153, placed on the loading AND stop frames)
  handles Up / Down (a `line_view` focus, 1..nb_lines with WRAP - no scroll
  of its own), Enter (A -> PlayAnimValidate -> the binder), Backspace (B ->
  the DBXO "Save control option?" confirm -> FSCommand `dbxo`), x (Detect
  Device - gated OFF on PC by WhatPlatform == 1, the retail PC path is the
  mouse), y (Save: `opts:cont:vadt`) and Delete (SWF key 6 ->
  DeleteBind -> `opts:cont:bind:delt`). **The Up / Down / Enter body only
  runs while `_root.fake` is truthy** - the retail exe drives this page with
  the flag set (the game-progress fakeList checks it the other way, so the
  port scopes it to this page and restores it on close).
- The binder: LineValidate plays 'binder_in' (f34, resting at '_stop' f43)
  and shows `binder_key` ("Bind a key for the command named below" - its
  texts bind to ITS OWN control_name / control_bind / control_device);
  'binder_out' (f51) re-shows the lines. The exe captures the next raw input
  in between.
- The retail "Button N" texts are the DirectInput channel numbers of
  DeviceX360Pad.xmb (X = BUTTON_2 -> 'Button 2', LT = BUTTON_6 -> 'Button
  6', RT = BUTTON_7 -> 'Button 7' - the owner's capture), which the SDL
  button enum matches (WEST = 2, NORTH = 3) and the trigger axes offset by
  two (SDL axes 4 / 5 = channels 6 / 7). The sticks read 'Stick 1 left' ...
  like retail. A keyboard row shows the bare key letter and no glyph.

### What changed

- `source/input/gs_input.{h,cpp}` - the bindings became a live, savable
  state: `CapturedBinding` + `beginBindCapture` / `takeCapturedBinding`
  (the next new key press / pad button press / pad push past the threshold
  since the arm - the pad is edge-scanned against the arm-time snapshot, so
  the A that opened the dialog never captures itself); `setPadBinding` /
  `clearPadBinding` / `setKeyboardBinding` / `clearKeyboardBinding`; and the
  whole state as text (`SerializeControls` / `RestoreControls` /
  `SaveControls` / `LoadControls`, `<user data>/controls.ini`:
  `kb_<ACTION> = <scancodes>` / `pad_<ACTION> = b<n> | a<n>+/- | none`,
  loaded over the defaults at initialize).
- `source/game/pause_menu.{h,cpp}` - `opts:cont:` answered:
  - `bind:open` -> the page goes interactive (`/:fake` true for this page),
    and one tick later `fillControlsList` writes the 13 driving actions
    (Acceleration, Brake, Left, Right, Handbrake, Clutch, Gear up, Gear
    down, Change camera, Look back, Lights, Horn, Back on track; the
    retail bike rows have no port action) into the 9 view lines with the
    binding / device / glyph per row, then the retail hooks the exe ran:
    `endLoading()` (plays 'open' AND clears isLoading - without it Enter
    never reached the binder) and the row-animation looper kick (the page's
    own open flow stops at its f25 in our AVM1; the looper's callbacks
    finish the open and land the page on its activating 'stop' frame).
  - `tickControlsPage` per tick: the scroll (the movie wraps line_view
    9->1 / 1->9 at the view edges; the game shifts a 13-row window one row
    under the highlight and re-feeds the lines), and the bind dialog (the
    page resting at f43 = waiting: the capture takes the next raw input,
    the row is rebound live - the new binding replaces the old one on
    whichever device it came from, the retail one-binding-per-row look -
    and 'binder_out' closes the dialog; Escape / Backspace cancels).
  - `bind:delt` (the movie's Delete / Unbind) unbinds the focused row;
    `vadt` saves controls.ini; `canc` and the `dbxo` confirm (no dialog UI
    in the port, the bindings apply live) restore the opening snapshot and
    close; the raw Delete key is forwarded to the page as SWF key 6.
  - The Controls page no longer opens the native Advanced settings page
    (its Y is the retail Save now; the Advanced hint stays Video-only).
  - The `-logcontrols` probe dumps the page's display list, its state and
    the icon frames (the reference for the next pass on this page).

### Verified (headless, build 500, `-fixedtimestep`; shots in `docs/images/`)

- The filled list: `controls list filled (13 actions)`, the page shows the
  retail layout - Acceleration / RT / Button 7 / Xbox 360 controller, Brake
  / LT / Button 6, Left / Stick 1 left, Handbrake / X / Button 2, Clutch /
  Left Ctrl / Keyboard (no glyph), Gear up / Y / Button 3 ...
  (`controls-list-filled-s77.png`); LB / RB badges and the colored bottom
  bar (Y Save / Bind / Unbind / X Detect Device / B Back) render with it.
- The rebind: Down x3 to Right, Enter -> the binder dialog
  (`controls-binder-s77.png`), key G -> `controls bind 'Right' rebound`,
  the row shows **G / Keyboard** and the old pad binding is replaced
  (`controls-list-rebind-s77.png`); Y (Save) -> `GSInput: controls saved`,
  the ini reads `kb_CAR_RIGHT = 10` (the G scancode) and
  `pad_CAR_RIGHT = none`.
- The unbind: the focused row + Delete -> `controls unbind 'Brake'`, the
  row renders empty (`controls-unbound-s77.png`).
- The scroll: 12 Downs -> one wrap -> `controls list scrolled down (rows
  2..10 of 13)`, the list shows Brake..Look back with the highlight walking
  (`controls-scroll-s77.png`); the earlier oscillation (each wrap read as
  both directions) fixed by following the movie's own law: the highlight
  wraps to the top line and the data scrolls under it.
- Back: `dbxo` -> `controls page back (dbxo: bindings restored)`, the icon
  column returns (`controls-back-s77.png`) and the page re-opens cleanly
  (two opens logged in the probe run).
- The loading spinner (`pc/loading_anim`) hides when the list fills; the
  page's demo-fill glyphs (stick arrows on every line) are overwritten by
  the real ones and keyboard rows hide their icon.
- **The persistence round-trip**: a fresh user data folder -> bind G to
  Right -> Y (Save) -> `kb_CAR_RIGHT = 10` in the new controls.ini -> the
  next boot loads it (`GSInput: controls loaded`) and the list shows Right /
  G / Keyboard. The first load was a silent no-op: the ini keys were cut
  with `substr(3)` while `pad_` is four characters (`_CAR_RIGHT` never
  matched) - and the first "fix" used `substr(4)`, which broke `kb_` (three
  characters); the cut is now per prefix. The value trim also strips the
  CRLF `
` (an `a0-
` token read its sign wrong).

### Ruled out (with the test that ruled it out)

- **The empty list was not a rendering bug**: the page's own f7 script ran
  (the ASFlashList + the scrollBar child exist at runtime) - the game had
  simply never fed the rows nor run the exe's endLoading / InitLoop hooks.
- **`line_view` undefined** (the Down handler NaNs silently): the retail
  exe initializes it; the game sets it to 1 at the fill.
- **Enter dead with the rows filled**: two gates in sequence - the page's
  `isLoading` (endLoading clears it) and the rows' unset `lock` (reads as
  locked, sndp:wrong). Both fixed; Enter opens the binder.
- **The navigation body never ran with the port's fake = ""**: the page's
  Up / Down / Enter handlers gate on `_root.fake` being TRUTHY - the exe's
  glue runs this page with the flag set; the port sets it for this page
  only (the game-progress demo fills check it the other way).

### Not done / next

- **The Gear up glyph drops out after a scroll** (one row's icon renders
  empty while its text updates; the icon clip reports frame 12, visible,
  parent visible - the art inside the icon's display list is the suspect,
  most likely the backward rebuild after the demo fill parked it deep).
  `-logcontrols` dumps the icon frames; the next step is a display-list
  probe of the icon clip itself.
- **Detect Device** (x / the bottom-bar button): the pad path is PC-gated
  off by the movie itself and the mouse path opens the device_select page,
  whose choicebox (WhatDevice values) is not fed - the page opens and backs
  out; feeding it is the same recipe as the binding list.
- The mouse pages (`opts:cont:mous:` - sensitivity / invert) and the wheel
  page (`opts:cont:weel:`) are unwired (no WhatDevice branch reaches them
  in the port's flows yet).
- The `-logcontrols` probe code stays in for the next pass (all of it
  flag-gated).

## Session 2026-11-06 (76, `glm`): the Image settings page takes the keyboard — the game drives the page's Move because retail's gamma key button has NO Up / Down conditions (the page never moves focus), the owner's treadmill scroll works with four new graphics rows overflowing the panel (Haze, Sun flare, Ocean reflections, Draw distance), and the row layout moved to `<user data>/menu_rows.ini` — plus the resume entry actually resumes now ('reco' was never handled)

The s75 known gap: *on the Image settings page opened via LB/RB, Up / Down /
Left / Right still land on the Gamma slider instead of the new rows*. The
owner's standing asks behind it: a scroll / treadmill for the pages, and "a
tool to alter the UI".

### What the movie says (all `[DATA]`: the full MAIN.vmf disassembly +
`_s76_keys.py` — every DefineButton2's keyPress conditions mapped to the
sprite that places it)

- **Every parameter page carries its own key-press button, placed on the
  page's own stop frame (f21)**: video = button **17261** in S17262 (also its
  sub-pages S17270/S17276), audio = **17291** in S17293, game settings =
  **17247** in S17248 (S17254 — the s75 note "the video page's is 17247" was
  wrong), gamma = **16593** in S16594 — and 16593's conditions are
  **Enter / Backspace / Escape only**. Retail's Image settings page is ONE
  slider row: there is nothing to move the focus to, so its button simply
  does not listen for Up / Down. The page's Move exists but nothing calls it.
- The page's `Move(key)` law (S16594/S17262/S17293/S17248 all identical):
  `if (_root.Input !== true) return; lightParam(0); numParam: up = -1 with
  wrap to lastParam, down = +1 with wrap to 0; lightParam(1);
  setVarToFocus(numParam); _root.CurrentParam = numParam; sndp:move`. (The
  s75 note "Down decrements numParam, the index counts bottom-up" was
  backwards — Down increments.) `lightParam(action)` plays 'out' (action 0)
  or 'on' (action 1) on BOTH `bkgd<numParam>` and `p<numParam>`.
- **The row focus scoping is spatial, not coded**: the generic row sprite's
  state machine is default(f0, `_buttons=false`) / open(f10) / stop(f22) /
  on(f28, `_buttons=true` + updateText) / **stop_on(f35 — the Left / Right
  arrow buttons 17464/17462/17465 are placed on THIS frame)** /
  change_left(f43→f48 fires sendUpdateCommand('prev')) / change_right(f55→f60)
  / out(f67) / close(f75). A row rests at `stop` (unfocused — the arrows are
  not in that frame's display list) or `stop_on` (focused — arrows live).
  The row's own Move gates only on the looser `_root.Input != false` and has
  no focus guard — it never needs one.
- The gamma page's `initParameters`: `setParam[0] = ['luminosity', m_Gamma_,
  'slider', '_root.gamma', 'opts:gama:gams', false]` (the [0] element is an
  id string), `numParam = 0`, `setParameters(0, setParam.length)`.
- **Nothing else consumes key 14 / 15 while a page is up** — the s75 guess
  "the tab squares consume the keys, hide / deactivate them" is ruled out by
  the button map: the tab-row button (17227) listens only to keys 49 / 51,
  and every other Up / Down button belongs to a page that is hidden.

### What changed

- `source/game/pause_menu.{h,cpp}` —
  - **`moveOptionsFocus(direction)`**: on an open page with game rows the
    game intercepts Up / Down (keyboard keys 14 / 15 and the stick) and runs
    the page's own law game-side: `lightParam(0)` on the old slot (it reads
    `numParam` — the game writes it first), wrap over the panel slots
    0..visible, then `setVarToFocus(slot)` + `lightParam(1)` +
    `_root.CurrentParam`. On the Video page while its rows fit the panel the
    keys still go to the movie (`sendKey`) — its own button drives Move,
    retail-identical; the game takes over only when rows overflow.
  - **The treadmill**: the page's game rows scroll a window
    (`windowFirst`) through the panel's game-row slots p1..p7 (`kRowSlots`;
    slot 0 stays the page's own first row — the Video page's Resolution /
    the Gamma page's slider, which never scrolls). A focus that walks off
    the visible rows shifts the window one row and `writeOptionsWindow`
    rewrites `setParam[1..7]` + re-runs the page's `setParameters(1, last)`
    (every slot row rebinds: label, id, cmd, varToFocus), parks all slot
    rows at `stop` (a stale `stop_on` keeps its arrow buttons live) and
    re-lights the focused one.
  - **Four new graphics rows** (all read live by the renderer every frame):
    Haze (`shze`), Sun flare (`snfl`), Ocean reflections (`ocrf`), Draw
    distance (`drds`, 50..150 %) — the Image settings page now carries 10
    game rows, 7 visible, so the treadmill is real. All step
    `QualityProfile = 0` (the profiles set them).
  - **`menu_rows.ini`** (`<user data>`, beside options.ini): a `[page]`
    section ('video' / 'gamma') replaces that page's default rows, one line
    per row `command = variable, label` in display order. The command ids
    and their value / step behaviour stay code-owned (extraRowText /
    stepExtraRow) — the file is the layout tool. Without the file the
    defaults run.
  - The resume entry resumes: the movie's `resumeGame()` fires **'reco'**
    (freeride list) / **'rcod'** (race list) — neither was handled, so the
    validated resume square played the movie's pause_out and the game stayed
    paused with no menu and no result (found by the discovery run; found
    while scripting the verification, not owner-reported).
- The discovery needed the entry-row map: the movie-driven entry row reads
  **Left / Right** (not Up / Down), does NOT skip locked entries (5 rights
  from Resume = Options; the lock only greys), and the options icon column
  is Language → Controls → Game settings → Interface → Audio & Video (4
  downs), whose own tab column is Audio → Video → Image settings (2 downs).

### Verified (headless, build 499, `-fixedtimestep`; shots in `docs/images/`)

- The focus fix: Down on the opened Image settings page → `options gamma
  focus slot 1 (game rows 1..7 of 10)`, Right → `opts:gama:rnsc:scne` →
  `options row 'rnsc' -> 105 %` (was: the slider stepped gamma itself).
  `docs/images/options-gamma-focus-rows-s76.png` — Render scale focused with
  its arrows, the Gamma slider unfocused (grey handle, no arrows).
- The treadmill: the 8th Down → `focus slot 7 ... [window scrolled]` (game
  rows 2..8 of 10), Right on it → `options row 'snfl' -> Off` while
  scrolled; the ups walk back and the up at slot 1 shifts the window back.
  `docs/images/options-gamma-treadmill-s76.png` (Upscaling filter at the
  top, Sun flare — Off focused at the bottom),
  `options-gamma-treadmill-back-s76.png` (Render scale back at the top).
- OK saves: Enter on the page → `options gamma 'vali'` → `GameOptions:
  saved` with `render_scale = 105`, `haze = 0`, `quality_profile = 0` in
  `<userdata>/options.ini`.
- `menu_rows.ini`: a test file that relabels (Render scale → "Resolution
  scale"), reorders (Haze first) and drops (Sharpening) → `menu_rows.ini: 10
  rows from ...`, the page shows exactly that and Right on the first row
  steps it (`options row 'shze' -> Off`),
  `docs/images/options-menu-rows-ini-s76.png`.
- The resume entry: validate on Resume now closes the menu and hands
  `eResult::Resume` to the mode (the close flow completes; previously the
  world stayed frozen with no menu).
- Regressions: the Video page through LB (the page's own button still drives
  Move — Down to Display mode, Right → `Window mode: borderless fullscreen
  desktop`), the audio page untouched (sliders + its own focus),
  `docs/images/options-audio-page-s76.png`; the slider refocuses with its
  retail look (orange handle + arrows) when the focus walks back to slot 0.

### Ruled out (with the test that ruled it out)

- **A second consumer of the keys while a page is up** (the s75 next-step
  guess): the static button map (`_s76_keys.py` on MAIN.vmf) shows every
  keyPress button placed on its owning page's stop frame; the sibling pages
  are hidden at runtime and `sendKeyToClip` skips invisible instances. There
  is nothing to scope away — the gamma page simply has no Up / Down button.
- **Hiding / deactivating the tab squares while a page is open** (the s75
  guess): the tab-row button listens only to keys 49 / 51; it never consumes
  14 / 15. Not the cause.
- **The menu_rows.ini loader on a std::vector<std::string>**: the first run
  garbled the parsed rows (`options row 'Upscaling filter'` for a Haze
  step) — the short (SSO) strings' `c_str()` dangled on the vector's first
  reallocation. A `std::deque<std::string>` (stable references) fixed it;
  the second run parsed and stepped the same file correctly.

### Not done / next

- **A scrollbar indicator**: not built — the retail parameter pages have no
  scrollbar art and the window law needs none; if the owner wants one it is
  a game-side progress tick (there is no movie art to borrow).
- The Display page (`opts:disp:`) and Game settings (`opts:gase:`) remain
  unwired — the same recipe as the gamma page, and their key buttons
  (17247 etc.) already carry Up / Down, so only the rows are missing.
- The tab path fires the page 'open' twice (two `sndp:valid` from one
  validate — the tab's own script re-plays it); the extension re-runs onto
  the existing slots harmlessly. Not chased.

## Session 2026-11-06 (75, `glm`): the real options menu gains the port's graphics settings — windowed / borderless / fullscreen on the Video page (the dead Frequency slot), six graphics rows on the Image settings page (placed instances of the generic choicebox row), LB / RB cycle the pages, and the window mode actually switches live

The owner's ask: *"in pause menu can we try add some extra graphics options in
to the real menu rather than in the imgui? … I also wanted to have a option
for windowed, borderless & fullscreen etc"* — then, after seeing the first
build: rows "run past" the panel, could we have a scroll / treadmill, a tool
to alter the UI, and *"same template different settings reached by L1 / R1 or
L2 / R2 to go left and right in the pages"*.

### What the movie says (all `[DATA]`, `tools/vmf_disasm.py` on
`C:/TDU2-Gamedata/TestDrive2/euro/bnk/frontend/hires/main.bnk` → MAIN.vmf, plus
`dumpClips` at runtime)

- The options pages are **child clips of /options** (`audio`, `video`, `gamma`,
  `display`, `game_settings`…), all placed on the frame the tabs page rests on
  (/options frame 222); each page is the SAME template (labels
  default / open / stop / close) holding its own row instances **p0..pN** (the
  generic choicebox sprite, ~48 px pitch) behind `bkgd<N>` strips.
- A page's `initParameters` builds `setParam` rows **fresh on every entry** —
  shape (the NewObject args pop in reverse): `[variable, label, type,
  '_root.<var>', 'opts:<page>:<cmd>', lock]` — then calls
  `setParameters(firstIndex, lastIndex)` with the second bound EXCLUSIVE. It
  fires `opts:<page>:open` BEFORE its own pushes, so the game's extension must
  run one tick later.
- `setParameters` drives everything generically: `SetTarget('p' + i)`, writes
  the row's `cmd` (→ the arrows fire `opts:<page>:<cmd>:scpr/scne`),
  `param_title` (the label field), calls `initChoiceBox` / `initSlider`. The
  row's value text field is bound to `_parent.param_txt`, the label field to
  `../:param_title`; `updateText` reads `_root[_root.VarToFocus]`.
- **The panels are fixed 8-slot grids, no scrollbar** — the movie's scroll
  machinery (`ASFlashList`, `initScrollBar`, `scrollBar`) belongs to the LIST
  pages (game progress, players). Gated retail pages just leave empty slots
  (that is why Game settings shows 3 rows).
- The page's `Move('down')` gates on **`_root.Input === true`** (strict) —
  without the flag the row focus never moves (rows' own left/right pass with
  the looser `!= false`, which is why the arrows worked while Up/Down did
  not).
- The Video page's **Frequency row is a dead retail setting** in the port
  (locked '60 Hz'; the refresh rate follows the display) — its slot is free
  for a real setting.

### What changed

- `source/game/game_options.{h,cpp}` — `WindowMode` (-1 unset / 0 windowed /
  1 borderless desktop / 2 exclusive fullscreen), `window_mode` ini key,
  `-borderless` / `-fullscreen` / `-w` overrides, `ApplyWindowMode()` inside
  `ApplyVideoOptions()` (SDL3 `SDL_SetWindowFullscreen` +
  `SDL_SetWindowFullscreenMode(nullptr = borderless desktop)`; only a change
  acts; a `-1` after Cancel derives the created-with mode so the restore
  walks the window back) + `requestSwapchainRecreation()`.
- `source/game/pause_menu.{h,cpp}` — the options pages extended game-side:
  - **Video page**: the Frequency slot (p1) becomes **Display mode**
    (`window_mode`, Windowed / Borderless / Fullscreen; `LockFrequency` now
    pushed false) — inside the panel, no overflow; the **Anti-aliasing row is
    the port's Edge smoothing** now (`AntiAlias` itself stays unused).
  - **Gamma / Image settings page**: Render scale, Upscaling filter,
    Sharpening, Sun shadows, Shadow distance, Frame rate limit — placed
    instances of the generic choicebox row sprite + bkgd strip clones (the
    page's own art has only the slider row); the retail gamma hint board
    hides while the rows are up. Cancel / OK / Quit keep working through the
    movie's own flows; Quit on the Video page now **restores** the values
    (retail only applies through OK, the port applies live so Quit walks it
    back).
  - **LB / RB cycle the pages** (audio → video → gamma; keyboard Q / E, pad
    L1 / R1 = `IA_CarBackView` / `IA_CarCamera`): the next page's 'open' plays
    and the three sibling pages' visibility is set game-side — the retail tab
    glue would close the page just opened.
  - `_root.Input = true` on every `opts:<page>:open` (the page Move gate).
  - `extraRowText` / `stepExtraRow` (steps mutate `GameOptions`, mark the
    quality profile Custom, re-push the value text; the display mode applies
    at once).
- `source/flash/flash_player.{h,cpp}` — game-side movie APIs, all general:
  `writeArrayRows` (array rows at absolute indices), `duplicateIn` (clone into
  a chosen parent), `placeCharacter` (attachMovie of a sprite definition),
  `invokeWithNumbers` (AVM1 compares two strings as strings — loop bounds must
  arrive as numbers).
- `source/flash/flash_definitions.{h,cpp}` — `findSpriteByLabels(labels,
  minFrameCount)`: the generic row sprite found by its label set (it is not
  exported; the 80-frame floor rejects the 'small' twin, character 1225 vs
  16614).

### Verified (headless, `-fixedtimestep`; shots in `docs/images/`)

- Video page: `options video extended with 1 game rows (setParam 1..1)`;
  Display mode focused shows Windowed with arrows
  (`options-display-mode-row.png`), Right → `opts:vide:wnmd:scne` →
  `options row 'wnmd' -> Borderless` + **`Window mode: borderless fullscreen
  desktop`** (the window really switches — the next shot is the desktop's own
  1920×1080), B/Quit → `opts:vide:quit` → **`Window mode: windowed`**
  (`options-quit-restore.png`). OK writes `window_mode = 1` into
  `<userdata>/options.ini`.
- Image settings: RB from the Video page → `options page -> gamma (LB / RB)`
  → `opts:gama:open` → `options gamma extended with 6 game rows (setParam
  1..6)`; the page shows Gamma + the six rows with live values
  (`options-image-settings-rows.png`); LB walks back to Video and the Video
  values are still there.
- Startup flags: `-borderless` / `-fullscreen` log the mode line at startup
  (the ini value or the flag seeds `WindowMode`).

### Ruled out (with the test that ruled it out)

- **Cloning rows onto the Video page** (the first approach): the panel art
  ends one pitch below FX quality — the p8 + bkgd8 clone rendered outside the
  panel (the owner's "run past"). The dead Frequency slot is the right home.
- **Gamma's own idle slots**: the page holds ONE row (the slider, p0) — the
  earlier "gamma has p0..p7" read was the Video page's dump; the graphics
  rows must be placed instances.
- **A scrollbar**: the parameter pages have none — the movie's scroll
  machinery belongs to the list pages. The retail answer to "more rows than
  slots" is page templates + gated rows, not scrolling.
- **The label-set finder without a frame floor** picked character 1225 (the
  'small' row twin, same nine labels) — the 80-frame floor returns 16614.

### Not done / next

- **The Gamma page's Up / Down + Left / Right on the placed rows**: the page
  opened through LB / RB keeps its focus on the Gamma slider (a Right stepped
  gamma itself, `opts:gama:gams:slid:updt`, instead of the Render scale row).
  The retail tab flow deactivates the other responders while a page is up;
  next step: dump which clip owns the gamma page's Down button
  (`-logpausekeys`, the video page's is button 17247) and what else consumes
  key 15 while the page is up, then scope it the same way (likely hide /
  deactivate the tab square container while a page is open).
- **The treadmill scroll** (the owner's ask): the machinery is proven —
  generic rows + a `setParameters(first, last)` window + game-side
  `numParam` readback — a scroll shifts the 8 visible setParam entries when
  the focus walks off the window edge (note the page's Down **decrements**
  `numParam`, the index counts bottom-up). Start at `PauseMenu::tick`'s
  `sendKey(14/15)` path.
- **A scrollbar indicator**: the ASFlashList `scrollBar` clip could be cloned
  onto the page, or a simple progress tick drawn game-side.
- **A data file for the rows** (the owner's "tool"): `kExtraRows` is the
  editable table already — moving it to a small config file would make new
  rows a data change, no code.
- The Display page (`opts:disp:` — retail: AI names / unit system /
  speedometer / GPS) and Game settings (`opts:gase:`) remain unanswered (the
  same recipe; the unit system would move to its retail home).

## Session 2026-11-06 (74, `glm`): the property enter-marker anchored and fed — the tile anchor now compensates the arrangement's offset for every kind (the property kinds drew it ~448 px below the projection, off screen), the Spot subtree parks after its kind goto (the movie's loop rewind was erasing the icon slot), the offroad child's "invisible placeholder" is really a glass plate and hides for the property kinds, and the distant world markers finally carry the map spots (dealers, shops, owned homes as 'home') — the tile art itself still does not composite into the card window (fully traced, see the delta)

**The owner's report**: the picture they sent (`docs/images/enter_marker_owner_reference.png`)
is the marker that stands over properties — houses, the caravan, clothes shops,
dealerships — "roughly a marker which when near allows you to enter its
destination"; the latest changes had also fixed the return-to-the-road badge
(their confirmation in play, `docs/images/rtr_badge_s74.png` is the port's
render of it, matching their reference).

**What the data says** (all from `-dumpbot` / `-dumphud -dumpsprite 251,227,<children>`
+ the decoded atlas page, `docs/images/hud_atlas_page001_marker_arts_s74.png`):
- `/HUD/enter` (char 251) holds the Spot tile (char 227, 751 frames, 94 kind
  labels — home f391, estates f349, cardealer f413, clothes f356, carwash f469,
  tuning f462 …), the prompt (enter_txt) and the card groups. Each kind frame
  re-places one unnamed icon child at depth 26 (fresh at f7, then move-only:
  4806 → … → 4935 estates → 4947 home → 4965 cardealer 'ic') and one 'shape'
  plate at depth 9 (4799 default, 4916, 4950 home). The icon/plate arts are
  atlas cells on page_001 (4934 = the house-in-hand, 4912 = the glass plate,
  4946 = the home house) and resolve fine.
- The 'stop' arrangement (the settled card state the port drives) holds the
  Spot at (+23.6, +328) of the element origin — **origin-relative**, so every
  unshifted placement threw the tile ~448 px below the projected spot: the
  property kinds showed the prompt card but never the marker (the offroad
  branch has compensated its badge centre since s70; the property kinds never
  did).

### What changed
- `source/game/freeride/freeride_hud.cpp` — `showEnterSpot`: the tile anchor
  now shifts the origin back by (-23.6, -328)·s for **every** kind (the
  offroad branch keeps its extra label re-anchor); the non-offroad kind goto
  parks the Spot subtree (`stopSubtree`, the law the world-marker clones
  already used — the kind frames carry a play action, and the 751-frame
  timeline's loop rewind rebuilds the display list from f0, which erases the
  icon slot: f0 never places depth 26); the offroad child parks at 'default'
  AND hides for the property kinds (its "invisible placeholder" quad is
  really a large glass plate — visible in the 'on' A/B) and re-shows for the
  offroad kind; `setWorldMarkers` clones get the same tile-anchor shift.
- `source/game/map_screen.{h,cpp}` — `findSpotMarkers(position, radius,
  skipId, out, max)`: the nearest-K map spots in radius with the map's own
  house law (an owned EPH house is 'home', the ones on sale 'estates'), one
  record skipped (the near-enter spot's own element covers it).
- `source/game/freeride/gm_freeride.cpp` — the distant marker feed pushes the
  spot markers next to the race/championship ones (the old comment claimed
  "race starts and shops" but only races were fed); the near path applies the
  same home law to `pSpotIcon` (the owned caravan showed the for-sale
  'estates' art before). One-shot proof line `FREERIDE: spot world markers fed`.

### Verified (headless, build 488+)
- The Caravan approach (`-tp 10513.7,7.4,15743.2 -heading 180`, the s71
  userdata): the enter card renders at the spot's projection with the tile
  anchored to it (`docs/images/enter_marker_caravan_s74.png`); the element no
  longer throws the tile off screen. The 'on' state was tried and rejected —
  its f6 only removes d24/d25, the card groups (#4/#5/#9/#10/#13) stay placed
  and draw as loose floating panels (documented in the code comment).
- The off-road badge unchanged: `-offroadhud` renders the ring + label exactly
  as s70 left it (`docs/images/rtr_badge_s74.png`) — no regression from the
  freeride_hud changes; the owner confirms the badge in play.
- The default-spawn probe: `FREERIDE: spot markers fed 0 (300 m)` with the
  600 m probe `home@383m` — the finder and the home law work; the spawn is
  just spot-sparse. Denser-spot verification of the distant clones is part of
  the delta below.

### Not done / next (the honest delta)
- **The tile art compositing — RESOLVED by the origin/win merge (252e378, same day).** Win's
  edba39a ("a transparent solid placeholder takes a bitmap override") is the answer: the
  placeholder solid binds the art as an override and draws — the merged build renders the
  red home plate + white house in the near card (the owner's reference icon) and the real
  `/HUD/BOT` badge (f0162df, bitmap 812, the down arrow + road) replaced the borrowed
  enter_hold up-arrow. My `findSpotMarkers` was dropped for win's `forEachSpot` feed (the
  superset: dealer brand-logo overrides, nearest-first, `-noshopmarkers`); the near-element
  fixes (the tile anchor, the stopSubtree park, the offroad-child hide) all survived the
  merge and compose with win's marker work. The GPS-destination ring + arrow of the owner's
  capture is win's remaining not-done.

## Session 2026-11-05 (73, `glm`): the car-lights round-up — the one-frame light lag fixed (cones / pools / glows rode the previous frame's pose), the pool corners sample the road surface now (the flicker on cuts / embankments, the uphill vanishing, tunnels), and traffic + cops got their lamps — headlight glows, red tail / brake pools, the amber blinker, the player's white reverse pool and the indicator's amber ground patch

**The owner's report**: *"traffic headlights and tail lights tend to lag in front or behind cars and there's a weirdness with the headlights having a flickery issue depending on parts of the road ... going up hill lights didn't project on the road"*; the standing ask to enhance headlights / brakelights / indicators on cars and traffic.

**The lag** (two stale reads, both one frame behind the drawn bodies):
- `gm_freeride.cpp` tick: the light block built the player's cones, rear pools and lamp glows from `carWorld` ~500 lines *before* `carWorld = vehicle.getWorldMatrix()` re-synced it (line ~4694 pre-session) — the physics pose was this frame's, the lights the previous frame's. The sync now sits right after `vehicle.step`, so the lights, the cops and the chase camera all read this frame's pose (nothing between the step and the old sync read `carWorld` — checked the whole span).
- The traffic cones were appended in the light block, *before* `traffic.tick` rebuilt every `agent.World` at its end — the pools led / trailed the moving cars. The traffic + cop appends now run right after `traffic.tick` (same frame's matrices; the consumer `EdenSurfaceFx::updateLightPools` only reads at draw).

**The flicker / the uphill vanishing**: the pool corners sampled the **terrain heightfield** (`getHeight`) lifted by one `roadOffset` measured *at the car* — wherever the road mesh's height above the heightfield varies along the road (cuts, embankments, a climb — and tunnels, whose carved cells `getHeight` refuses outright), corners sank under the tarmac and the pool depth-clipped out. New `EdenWorld::getDriveHeight(x, z, fromY, headroom)` answers the **highest drive-on surface in the corner's own sector** (sector objects = road meshes, bridge decks, tunnel bores; one 8 m surface-grid lookup, not the every-sector walk of `getSurfaceHeight`), wired as `EdenSurfaceFx::setSurfaceQuery` — corners sit on the tarmac they draw on, heightfield + roadOffset stays as the fallback, the +4.5 m headroom above the lamp covers the road climbing ahead.

**Traffic + cops** (the enhancement ask): `EdenCollectLampAnchors` (shared, `eden_headlights.cpp`) collects one bloom anchor per `FA_<L|B|S|R|W>_<FL|FR|RL|RR>` sprite node of a shared model — traffic types and the police bank collect at load, `Traffic::appendLampGlows` places the quads through `agent.World` (+ `GroundLift`): front lights + rear position at night, stop while braking, the amber blinker of the agent's side on its phase (the exact FA_ sprite gates of `Traffic::draw`); rivals (no FA_ nodes) light their own CinematicCar anchors via `pose()`; braking traffic gets red ground pools (`Traffic::appendBrakeCones`, day-dimmed); the cops join with `ChaseMode::appendLampGlows` (bank anchors or the fallback CinematicCar's light state) and up to 2 cop headlight cones. One-shot log `Traffic: lamp glows drawn (N quads)` proves it headlessly.

**The player's own**: per-cone `Tint` + pool-shape overrides on `EdenHeadlightCone` — the reverse pool is **white** now (it shared the red rear tint), and the indicators cast a small **amber patch** beside the car on the blink phase (`isIndicatorLampOn`). The pool falloff (`surfacedecal.frag` Params.z = 1) is a **beam** now: brightest just ahead of the lamps, dying toward the far rim (v = 0 is the near edge — checked `appendQuad`'s uv order), instead of the even blob; regenerated the builtin SPIR-V.

**Verified** (headless, `-night -snaproad 100`, build 487):
- Hawai road node 31058,37150 (session 69's embankment road): road-region mean brightness **+17.2** vs `-noheadlights` parked, **+9.6** mid-drive (`60:hold=accel`, speed 116 — same car spot in both runs, the drive is deterministic) — `light_pool_hawaii_road_s73.png`; the cut + wall spot 19942,24566 casts clean up the slope.
- Traffic at night (`-trafficpark`): oncoming sedan with white headlight glows + its own ground pool, two parked SUVs with red tail glows + red brake pools on the tarmac — `traffic_lamps_night_s73.png`.
- Indicators (`-indicatortest`, t = 3.2 s): amber lens glow + the amber ground patch beside the car, correct side — `indicator_pool_s73.png`. Reverse (hold=brake at standstill → gear R): white reverse lamps + the white pool behind the car — `reverse_pool_s73.png`.
- `Traffic: lamp glows drawn (4/6/12 quads)` in every night log; perf unchanged (79 fps avg, update 0.6 ms — one surface-grid lookup per pool corner); 0 VUIDs, 0 errors.
- Build: `build exit 0`, exe 2026-11-05 20:03, `build-glm\bin\opentdu2-glm.exe`.

**Not done / next**: the indicator ground patch and brake pools are tuned by eye ([INF]); the s15 G/B shadow decode (the retail per-pixel beam, rain_shadow_spec B.7) is still open; the cop glow path is code-reviewed only (no chase was staged headlessly); the pool's `ahead` distance does not shorten on crests (the corner heights follow the ground, the reach stays fixed).

## Session 2026-11-05 (72, `glm`): origin/win merged (the wheels-in-arches fix, the dealer buy window, the casino cutscenes …) and the owner's "1 of the Zondas has an odd rim oddity in the dealership" turned out to be that wheel-lift bug on their pre-merge build — all four dealer Zondas verified correct on the merged build; -shopaim frames the car from its retail PosCam view (the slot nearest the door used to draw black)

**The merge** (8c8b042): 24 commits of `origin/win` (a9cdf3b wheels in their arches,
e0bd6f8 the dealer buy window, the casino cutscenes / hall / rankings, the Q7 body, the
cameras, the perf work). One conflict, `docs/re/houses_spec.md` §9: glm's detailed
9.1-9.3 caravan subsections kept, win's patio `.shk` answer folded into 9.3, win's
§10-13 (HOME.vmf, the furniture / colours / walls) follow.

**The owner's report** ("check the zonda's 1 of them seem to have some odd rim oddity in
the dealership"): the Zondas sit in **ECD_ITAL_1207I**, the Italian-cars dealer (the DB's
only dealer stocking them; buyable slots 5-8 = C12S Roadster 115, Cinque 413, Zonda F 368,
F Roadster 414 — the Tricolore 654 is not on sale). Reproduced with `-wheellift`
(the pre-merge behaviour): the F's rim rides **0.094 m** up into its arch
(cinematic_car.cpp:1187, `docs/images/dealer_zonda_f_wheellift_oddity.png`) — the same
Zonda F that anchored the owner's earlier "wheels poke through the body" report, fixed on
`win` in a9cdf3b. On the merged build all four sit and size correctly
(`dealer_zonda_f_wheels_in_arches.png`, `dealer_zonda_cinque_wheels.png`), and the F
additionally side-on in daylight through the car wash with a bought example
(`zonda_f_rims_side_daylight.png`; buy flow `-gm SHOP -shopaim 6 -money 800000`, the
purchase saves `car_rims` and FREERIDE drives `rims pagani/zonda_f_f_01`).

**Data checked while chasing it** (all consistent — the draw was never wrong):
- `DBRims` for all six Zondas: the Cinque's and Tricolore's `_rsc_file_name_rear` point at
  the **_F_01 bank** (dedicated `_r_01` banks exist on disk but the retail DB draws the
  front design on the rear, rescaled by the node) — retail data, left as-is.
- Every Pagani rim mesh is normalized (bead 0.212 m, flange 0.2495 m —
  `_rim_mesh_scan.py` on the extracted banks); the Cinque F/R 3dg differ by 128 bytes
  only.
- The six car models' `D_RIM_*` nodes all bake 1.240 / 1.300 (= 19" / 20" at
  (inch·0.0254/2 + lip)/0.212); two mirror conventions on the right wheels (the F /
  Tricolore flip Z, the others flip Y) — size-neutral; the world chains agree.
- No rim multiplier fires on any dealer Zonda (bakes match the DB diameters; the C12S's
  18" row rescales, as designed).

**-shopaim fixed** (`gm_shop.cpp`, `intro_cinematic.{h,cpp}`): the old law placed the eye
4 m from the car towards the walk start — for the slot nearest the door that lands at the
door glass, behind it, and the run draws black (the ECD_ITAL Zonda F; `cam_entree` stands
outside). Now the retail view: the PosCam's own Camera entity when the bank has one
(new `IntroCinematic::getPosCamViewLocal`), else `focusDisplayCar`'s 6.5 m / 35°
three-quarter law. The rim multiplier log is per car now (the shared static re-logged for
every showroom car: 9,928 → 4 lines in a dealer run).

**Not done**: the buy window's live rim swap on the podium car (the documented gap,
unchanged); the PosCam camera path is exercised only where a dealer bank ships Camera
entities (ECD_ITAL's don't — the three-quarter law covered all four shots).

## Session 2026-11-03 (71c, `glm`): the owner's workflow contract synced into this workspace — RULES.md + PROGRESS.md land, CLAUDE.md / WORKSPACES.md take the hub's rule update (sync-before-work, push-at-session-end, build-id everywhere), glm paths adjusted

The owner: *"I tried making sure everyone had rules sync before making changes
and to always push the changes I need to make sure you have the correct docs"*.

This clone's docs predated the hub's rule update (`9990ce6` on the hub): no
`RULES.md`, no `PROGRESS.md`, CLAUDE.md still saying "update docs/STATE.md".
Synced verbatim from the hub's main (`E:\OpenTDU2-main`, the tree the owner
rebuilt with the session-71 merge): **RULES.md** (the workflow contract: sync
before work, build id in every report via the watermark, WIP visible,
unpushed = did not happen), **PROGRESS.md** (the status board), and the
CLAUDE.md / WORKSPACES.md rule updates. The glm-specific bits adjusted, not
dropped: the build block now shows `build-glm.ps1` + the `build-glm` dir and
`opentdu2-glm.exe` outputs, the run/verify examples and the
`Stop-Process`-by-path guard point at `C:\OpenTDU2-glm`. Also this session
(pre-sync): the hub's `E:\OpenTDU2-main` update-and-rebuild had stopped on the
three files cline's house WIP (`68e29ac`, their §9 caravan report) and my
session-71 commits both touched — resolved keeping BOTH (my always-on island
streaming supersedes the `-houseworld` opt-in, their once-per-profile intro
`SeenHouseIntros` + `-replayhouseintro` kept, §9 annotated with the
resolutions), the script re-run through the cline merge to the rebuild, exit 0
(`build-main\bin\opentdu2-main.exe`, hub main ahead 21).
* **Do not** edit `docs/state/main.md` or `docs/state/cline.md`, and **do not**
  append to `docs/STATE.md`: that file is the frozen history up to session 62
  (2026-11-01) plus the shared `## Where we are` block.
* Read order for a fresh agent: `CLAUDE.md` → this file → `docs/state/main.md` →
  `docs/state/cline.md` → `docs/STATE.md` for the deep history.

---

*No entries yet since the split (2026-11-01) — the sessions up to 62 are in
`docs/STATE.md`. The sessions 63-70 are in `docs/STATE.md`'s `## Where we are`
block.*

## Session 2026-11-03 (71, `glm`): the houses walk outside — the living room loads in its island (exterior shell + streamed world + ocean) and the plot is walkable to the garage door, which is where the Caravan's garage selectable actually lives

The owner: *"I'm hoping if we can have houses be able to walk outside. Last I
tried the caravan when inside couldn't walk out towards the garage. Also some
world outside wasn't rendering fully."*

**Reproduced** — "the caravan" is a house: `EPH_L0C_1254I`, the DBHouse row
whose localized name (loc 6756) is literally **Caravan** (the owner's
`houses_owned` has it; `house_intros_seen = lph_l0c` in profile.sav). Two
root causes, both in the data:
    build-glm\bin\opentdu2-glm.exe -gm FREERIDE -skipcine -nopad -fixedtimestep -logselect -nointeriorintro ^
        -tp 10513.7,7.4,15743.2 -heading 180 -walkat "6.37,1.98,1.46,-90" -autoplay "400:press=validate;..." -exitframe N
- **The garage is 14 m out across the yard.** lph_l0c's `Garage` FpSelectable
  sits at scene-local (-10.87, 1.16, -1.71) — outside the 10 m select radius
  from the walk start (17.55 m), so it can only be reached on foot. But the
  room's .shk carries floors for the caravan interior only (39 triangles +
  33 boxes, the yard has none), and `WalkMode::collide`'s
  `FindHavokFloor` failure froze the walk at the front door (`return from`):
  the scripted walk stopped after ~3 m, Garage stuck at 14.42 m.
- **Houses streamed no world.** `enterHouse` passed no island, so
  `setup.bStreamWorld` stayed false and the interior view loaded zero sectors
  (`loadAround(pos, -1)`) — out of the windows only the area-level backdrop
  drew (smooth ground, distant buildings, no sector detail, no ocean).

### What changed
- `source/game/spots/interior_walk.{h,cpp}` — `enterHouse(house, bToGarage,
  island)`: the living room loads with the retail party-house setup —
  `setup.bWorld = true` (per-frame streaming around the camera, the ocean) and
  the spot exterior bank `islands/<island>/exterior/eph_<level>.bnk` drawn
  around the room (the caravan shell, the deck, the garage building, the plot;
  missing → warn + bare room). The garage room keeps its closed frame. The
  garage→Living swap passes the island back; `-nointeriorworld` restores the
  old closed-room A/B. The A that swaps the rooms sets `bRoomSwapped` and
  `tookRoomSwap()` consumes it for the caller.
- `source/game/walkmode/walk_mode.{h,cpp}` — `setWorldFloorHook`: where the
  room's .shk has no floor, the hook supplies the ground (same 0.4 m step /
  2 m drop bounds; false keeps the freeze law). InteriorWalk's hook maps the
  scene feet through `getSceneOrigin()` and queries
  `IntroCinematic::getSurfaceHeight` (terrain + roads + the exterior bank's
  own floors — the deck).
- `source/game/freeride/gm_freeride.cpp` — `enterHouse(..., islandName)`; the
  garage fuel-pump block skips the tick that swapped the rooms (the pump menu
  used to open on the very press that loaded the garage).
- `source/game/walkmode/walk_mode.cpp` — `-walkat` pins the **first** walk
  only (the later room swaps keep their own cameras; pinning the garage's
  cam_entree to the living room's coordinates froze it off the garage floors).

### Verified
- The Caravan loop headless: through the front door (the retail start pose
  cam_entree yaw -92.3° aims through the 0.85 m doorway gap of the .shk wall
  plane x 3.5 — the straight line to the garage clips its frame, the walkout
  needs no steering), across the yard on the island ground (17.55 → 0.41 m in
  the SELECT log), the `Garage` prompt live at the garage door
  (docs/images/house_caravan_yard_garage_s71.png), A → gph_l0c with the three
  owned cars on the D_CAR slots (Enzo / R8 Spyder / Q7,
  house_caravan_garage_cars_s71.png), the garage's `Living` walks back and the
  living room reloads at `cam_entree_garage` **in the yard** looking at the
  caravan — shell, deck, awning, pool, streamed hotels, sea
  (house_caravan_backyard_s71.png).
- The story house (-home, EPH_L3DI): the glass wall now shows the terrace
  pool, the sea, the coast (house_story_glass_s71.png); 13 furniture slots
  unchanged.
- Regressions: the dealership interior walk unchanged (icd_ferr, its 9
  sectors); `-nointeriorworld` streams 0 sectors around the house (the A/B
  intact); 700-frame FREERIDE drive exit 0, 0 stalls; build exit 0.

### Ruled out (with the test that ruled it out)
- A broken Garage volume: the volume, its type 5 and the swap code were fine —
  the walk geometry never reached it (the scripted walk stopped 14.42 m short
  with the door-frame graze, and the yard has no .shk floor at all).
- The .shk being truncated: parsed lph_l0c's Havok packfile by hand
  (`%TEMP%/_caravan/shk_dump.py`, 5 bodies → 33 boxes / 5 capsules / 39 mesh
  triangles, bbox x -11.3..7.1) — the .shk covers the caravan only, by design.

### Not done / next
- The interior-walk enter keeps loading the sectors synchronously (the enter
  hitch grew with the 20-sector bWorld ring AND the collision streamAround);
  spreading it like the drive's streaming would hide it.

## Session 2026-11-03 (71b, `glm`): the yard got its collision — the exterior bank's own triangles (the caravan shell, the garage building, the plot walls) and the sector statics around the spot (fences, trunks, the campsite) push the walk body

The owner: *"could you implement the colision etc... please thanks"* (the 5.1
"Not done" item).

### What changed
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` —
  `collectExteriorCollisionTriangles`: the exterior-bank models already
  `setKeepCollision(true)` (the height queries), so their CPU copies are
  gathered as-is (model space = node transforms = the spot frame = island
  space); `getWorld()` accessor for the height callback.
- `source/game/walkmode/walk_mode.{h,cpp}` — the hook grew into
  `setWorldHook(body&, bodyPrevious, outFloorY&)`: it pushes the body out of
  the world's obstacles and answers the ground; `collide` runs it after the
  .shk / PNJ / car pushes (the feet follow the pushed body), `start` uses it
  for the yard spawn (the Living swap's cam_entree_garage).
- `source/game/spots/interior_walk.{h,cpp}` — the hook maps through
  `getSceneOrigin`, resolves `VehicleWorldCollision::sphereContact` (island +
  `setHeightFunction` = the carve-aware `getGroundHeight` +
  `streamAround(spot, 1)`, deepest contact × 4 rounds) and
  `ResolveSphereHavokCollision` on the exterior triangles; both cleared on
  exit. No road filter needed: the sphere bottom rides kStepUp above the
  feet, so every surface within the step height (roads, kerbs, the deck) is
  floor and never touches the sphere.

### Verified
- The walk into the garage building freezes at the shell's inner wall
  (0.66 m at the volume → 5.85 m past it, two identical SELECT readings =
  stopped; docs/images/house_caravan_garage_shell_block_s71.png — before: it
  sailed through everything).
- The walk to the campsite's exit freezes at the barrier gate / kerb line
  (16.6 m east from the yard, docs/images/house_caravan_site_barrier_s71.png).
- The full Caravan loop still passes (no false blocks on door → yard →
  garage → Living back), the cars on the slots, no fuel-menu leak.
- The story house walk unchanged (13 furniture slots, the glass view; 6448
  exterior + 108592 sector triangles), the 700-frame drive exit 0 (0
  stalls), the walk update avg 1.4 ms at 90-116 fps. houses_spec 5.2 +
  Not-done reworded.
