# STATE — handoff

**Update this before you run out of usage.** A fresh agent starts with zero
memory of previous sessions; this file is the only thing that carries context
across them.

> **Read `CLAUDE.md` at the repo root first** if you have not: it has the build,
> the headless verification recipe, the conventions, the four game versions, and
> the list of traps that have already cost real time.

> **New session entries do NOT go in this file any more (2026-11-01).** They go
> in the shard of the branch you are working on:
>
> | branch | shard |
> |---|---|
> | `main` (the hub / the owner's integration tree) | `docs/state/main.md` |
> | `glm` | `docs/state/glm.md` |
> | `cline` | `docs/state/cline.md` |
>
> One file per agent means two branches never edit the same handoff file, so the
> three-branch merge in `update-and-rebuild.bat` cannot conflict on session
> notes. It did: `docs/STATE.md` was edited by both sides of **32 of the last 38
> merges** (every session prepends to the same block) and that is what kept
> stopping the update.
>
> **Read order for a fresh agent:** `CLAUDE.md` → your own shard (newest entry
> first) → the other two shards → **this file** for the deep history (up to
> session 62, 2026-11-01) and the `## Where we are` block below. This file is
> otherwise frozen: do not append to it and do not renumber its entries.

## Where we are

- Newest (2026-11-03, `glm`, session 70): **the origin/cline merge (the launcher, the ocean, the traffic + police, the UI viewer, the weather cycle, sessions 63-78) landed and the return-to-road indicator was rebuilt onto the decoded retail element — the work is parked one delta short of the captures, with the decode written up for the next session.** The owner asked to look at the "Return to the road" UI against their retail captures and pointed at cline's UI tools. **The merge** (21 conflicts resolved: the surface-fx falloff stays glm's superset, the satnav minimap stays glm's with cline's `bSimpleHud` gates, the weather library feeds both the intempery overlays AND the cycle variants, the scene-view lightmap materials carry owners for the streaming unregister, the `-clock HH.MM` freeze freezes the cycle (rate 0) and the world clock follows the cycle per tick so the pause menu / traffic hour / weather pick share one clock; follow-up compile fixes committed). **The return-to-road decode** (`docs/re/backontrack_ui_findings.md` rewritten): the captures' badge = `/HUD/enter_hold` (the circular ring) — its states are the movie's OWN AVM1 actions (element f9 'open' runs `ic.gotoAndStop("on")`, ic f8 'lock' plays `FSCommand:sndp:wrong`, f40/f120 auto-close), the fill sweep is ic/anim's 30 frames sliding the sky art (bitmap 808) through the nested stencil mask (which WORKS now — cline's parked blocker is gone), the layout is the movie's own parking position (ring centre stage x 639.95 exact, y nudged -24 px to the capture) with the label needing no re-anchor; the goto traps fixed in the driver: the forward-goto leaks the 'lock' art (hidden after 'on'), fresh sprites play their own timeline (the sweep parked via gotoFrame + stopSubtree), and the first show must force the ic goto. The grass tile (Spot 'default' + offroad 'on' = bitmap 4754, the grass-over-road circle) is a DIFFERENT presentation — decoded and kept, unused by this flow. **The indicator now**: off-road > 2 s (or the on-road rejected press) shows the ring at the fixed retail position with loc 50 under it (`docs/images/rtr_ring_passive_s70.webp`), the hold sweeps the fill and resets (`rtr_ring_hold_s70.webp`), the on-road hold rejects (ic 'lock' + the badge shows immediately, no 2 s wait). **The UI tool the owner asked about**: `FlashMovie::getBitmapPixels` / `getAtlasCellPixels` — the engine's own decoder writes any flash bitmap (standalone or atlas cell) as a PNG; `-dumpbot` dumps the whole 4740..4830 badge cluster (the ground truth art sheet). **Parked one delta short**: the captures show the arrow pointing DOWN with the sky filling the whole ring; our render has the arrow UP with the fill partial — the composition/orientation is the retail driver's (a port-side flip breaks the stencil mask). The next tool is the owner's suggested x360-recomp texture dumper (dump the retail atlas + the runtime-composed badge), with the template-remake fallback the owner offered. Verified headless: the passive + hold ring shots, a 700-frame drive exit 0 (0 stalls, the 2 load hitches only), build exit 0. Test hooks: `-offroadhud`, `-botforce on|lock`, `-botframe <n>`, `-dumpbot`. **Follow-up (same session, the owner's art inspection)**: the owner compared the dumped arts against the retail captures — 808 (the sky) matches the retail interior, 822 (the grey up-arrow) does NOT match the retail arrow (the retail composes/orients it at runtime), so the port gained the icon-remake route: `<mods>/flash/fla_<id>.png` replaces any flash bitmap (`FlashMovie::findModOverride`, bound over the standalone / atlas sources in `getBitmap`, owned by the movie, destroyed on unload; verified with the sky crop as `fla_0822.png` — the `bitmap 822 overridden` log line + the sky rendering in the ring). The owner's Photoshop stand-in (white ring + white down-arrow, transparent interior) drops in as `fla_0822.png`; the x360-recomp texture dumper stays the tool to recover the TRUE retail art. README Modding row. STATE session 70.
- Newest (2026-11-02, `glm`, session 69): **the car lights fixes from the owner's merged-build test: the headlight pool blends, it casts on roads (Hawaii), the lamps got an emission knob, and the indicators auto-cancel.** The owner: *"headlight cast seems square and doesn't seem to blend"*, *"the light seems to work in hawaii but not casted at all on road but fine on grass"*, *"can we tweak there emission at all indicators and stuff don't look bright enough"*, plus the auto-cancel ask. **The square**: the pools shared the tyre-track decal shader (straight per-axis shoulders + a 48-cell hash grain = a square stamp on a light quad); they draw in their own batch now with a falloff-mode push constant (`surfacedecal.frag` Params.z = 1 → radial in the quad's own uv space, an elliptical pool reaching zero at the rim, a gentle centre hotspot, no grain; tracks keep theirs at 0). **The road cast**: the pool corners sample the heightfield but roads are sector meshes ABOVE it — grass agreed (drew), roads buried the pool (Hawaii's embankments). FREERIDE measures `getSurfaceHeight(car) - getHeight(car)` every 0.15 s (one collision walk, not per corner), smooths, clamps [-0.5, +4] m, `EdenSurfaceFx::setRoadOffset` feeds every pool corner — island-agnostic, ~0 on grass. **The emission**: `-lampboost <f>` (default 1.4, 1.0 = retail) over every GLOSSFCT channel. **The auto-cancel**: indicating a side and steering that way (|steer| > 0.35) arms, the steering back to centre (|steer| < 0.12) cancels — the real latch; menu toggle + `-indicator` untouched; `-indicatortest` proves it headlessly (blinker at t = 1, a scripted left pulse 2.5–4.5 s → `CarLights: the left indicator auto-cancelled` in opentdu2.log: indicator 0 x4 → cancel → -1 x37). Verified (Ibiza / Hawai / casino, the owner asked for the island pairs): Ibiza village cobbles pool soft-edged (`docs/images/pool_cobbles_s69.webp`); Hawai road node 31058,37150 (extracted from the island bank's ROAD_MAP.IA 'EDON' records with python — the freeride spawns there are off-road) casts +5.36 mean RGB vs `-noheadlights` over the wash region (`pool_hawaii_road_s69.png` vs `_nohl`); stars + moon on both; casino -tp 2777,1493 loads/renders with the lamp glow (the -tp hedge landing is the teleport, not the lights); the offset's one walk per 0.15 s costs nothing measurable — 82–97 fps, 2 load hitches, 0 VUIDs; build exit 0. car_lights_spec 7.1 (falloff + offset) / 7.2 (boost + auto-cancel), README CLI row. STATE session 69.
- Newest-1 (2026-11-02, `glm`, session 68): **the night lighting landed — street lamps pool on the ground, buildings get lit windows, the moon and stars are in the sky, and the shop neons switch with the weather** (the owner's TDU-World reference screenshot: *"street lights looking a bit yellow and buildings having no lights on at night ... not sure if we have the moon or stars at night"*). All three "whys" were the port feeding zeros into mechanisms the retail shaders already decode: **(1) street lights** — the retail bakes lamp light into each sector's `-s.2db` G channel and the terrain / road / ground shaders draw `G * 2 * UserVector1.y`, but `setupScene` hardcoded `UserVectors[1] = (1,0,0,0)`; now the weather feeds `UserVector1 = (1, outside_street_lightmap, 1, building_nightwindow_frequency)` (1.5 / 1.0 at night, 0 by day — `-lognight` prints it) + `UserVector2 = building_shop_front_back_lighting` (`eden_scene_view.cpp` setupScene, the asset viewer too), and `EdenWorld::loadSector` does the retail `FUN_00982af0` slot law: TD_Road every SHADOW slot = the `-s` map, TD_Ground/_HM slot 2 + `sectrOff = sector origin` (linear clamp), the `.pmi` O / S instances through the new `EdenDrawParams::TextureOverrides` (TD_Ground SHADOW = the sector map, mirror sampler) + `sectrOff` w = 768 (`GroundInstanceParams[3]` per sector, `libraryNodeHasGround` cache, `drawNodeItemsSkinned` takes params now). **(2) windows** — the same `UserVector1.w` gate opens the TD_Building slot-6 night-window term; verified +50% facade luminance at night vs `-nostreetlights`. **(3) moon + stars** — the moon is the verbatim MatMoon port (`td_moon.sob` + `fx/weatherfx.bnk` "MOON", `PosMoon = -sundirection*100000` with `Skydome_moonPercent` alpha, `drawMoon` after the dome in the main view and the env cube, `-nomoon`); the stars are port-native (`resources/shaders/starfield.*` → builtin headers, 420 seeded points on the upper hemisphere, additive, alpha = `Skydome_starsPercent` with the hour rule fallback, `-nostars`) because retail's star stream is [ENC]. **Neons** ported verbatim (`eden_neons.{h,cpp}`, `registerNeonMaterials`/`updateNeons` in the scene view, `EdenWorld::RegisterNeons` + per-instance `InstanceBlinks`/`InstanceBlinkStart` through the new `EdenDrawParams::MaterialVectors` per-material override, `building_neon_strength`, `-noworldneons`). **Spot exteriors** re-registered after the session-45 no-op (the decoded night law lets the blend return): the exterior NIGHTLMAP overwrite only applies when the weather points at the night lightmap (`y > 0`) so the authored day look survives (the deliberate deviation, in the code comment), plus the retail OcclusionDayNight sprites and TD_Lamp lamp materials (`__Instance_Color.w = y`). **The lamp glow heads switch with the hour** (glm-native `EdenWorld::setNightLights`, the TD_Lamp items of the SF_Ligh* library models skipped by day in `drawInstances`, `-lampday` forces on — deliberately not in the renderer where cline's unmerged 25059ec sits). Verified headless (WORLD_VIEWER Ibiza town 21962,11,23675, `-weather 00`): night facade band 30.0 vs 19.9 mean RGB with `-nostreetlights`, the pavement pools visible (`docs/images/street_pool_night_day_s68.webp` night vs day, `night_town_s68.png`), moon + stars in `night_sky_moon_stars_s68.png` (111 star px > 60 at 4K); day preset "14" vs the old `build/` exe: mean diff 2.27/765, the changed pixels only the weather-gated sign/lamp emissives + the retail `UserVector1.z = 1` facade strips; 360-frame `-vk_enable_validation_layer` drive: **0 VUIDs** (the 170 ms/frame hitches are the layer's own overhead — without it 2400 frames, 1 hitch, 48-52 fps); build exit 0. Known limits (documented): the dry-night TD_Road perm still doesn't decode the `-s` G (rain_shadow_spec B.7) so tarmac roads stay dark where retail's pools come from the pavement / terrain maps — the port-native `.lmp` lamp pools stay the pre-planned plan B; the `.lmp` dynamic cones (§4) and light beams (§5) not ported. New specs from upstream `335759a` with glm port notes on top: `docs/re/night_lighting_spec.md`, `weather_spec.md`, `neon_spec.md`. README CLI row. STATE session 68.
- Newest-1 (2026-11-02, `glm`, session 67): **headlights light the ground now, the
  descriptor/render-pass bugs behind the sky artifacts are fixed, and the WORLD_VIEWER has a map editor
  panel** (the owner: *"noticed a odd sky oddity ... looks like road signs/gps turn glyphs printed in the
  skybox ... do we have a proper map editor ... petrol stations but theres no true functionality ... can we
  try fix car headlights etc... they glow but there doesn't seem to be any light projection, would bloom or
  something help"*). Three commits:
  - **The sky glyphs** (not reproduced headless in five configs, but the disease is real and treated): the
    brightened screenshot shows the rs_maps sign-atlas content (chevrons, plates, route numbers) as giant
    faint depth-tested shapes in the sky. The proven bugs of the trap-13 family, all fixed: (1)
    `EdenPostFx::pass` swapped material slots without TextureHashes - the two Gaussian passes shared one
    descriptor set, the vertical blur sampled the UN-blurred quarter target (also the three targets were all
    named "PfxTmp": identical name hashes - now distinct names + hashes from the bound texture); (2)
    `updateWetGround` swapped RIPPLE frames without the hash (ripple animation frozen per frame slot); (3)
    composed sign faces reused their exact name after LRU eviction (recomposition could hit the cached set
    holding the evicted face's view - names now carry the composition counter); (4) **the first
    render-pass mismatch the validation layer ever caught**: EdenSurfaceFx built its pipelines against the
    swapchain pass (initialize runs before any scene pass) and drew them in the offscreen pass -
    VUID-vkCmdDraw-renderPass-02684, 10 reports in the first 50 frames of every drive, now pipelines are
    created lazily per render pass. Verified: the 2000-frame night drive with
    `-vk_enable_validation_layer` goes from 10 VUID reports to zero (only the pre-existing 2-buffer
    shutdown leak remains). **If the owner still sees the glyphs**: run `-nodescriptorcache` (A/B) and
    `-vk_enable_validation_layer`, send the log.
  - **The headlights**: the retail s15 G/B "headlight term" (rain_shadow_spec B.7) turned out to be
    rain-permutation-only - disassembling the shipped TD_Road.sob shows ps_0 (the dry night shader) reads
    only R and A; the visible retail pool is the TD_ShadowBox caustics projection, which the port collects
    but the renderer skips. Port-native pools shipped instead: `EdenSurfaceFx::updateLightPools` draws one
    soft ground quad per cone (warm ahead, red brake / white reverse behind), corners on the carve-aware
    ground via the puddle height query; cones from the new `eden_headlights.h` registries filled in
    FREERIDE (player from carWorld + `car_headlights_intensity`, traffic nearest-8 behind the FA_ night
    gate). `-noheadlights` off. Verified: pool region mean pixel diff 38.46 on/off
    (docs/images/headlights_pool_s67.webp), brake A/B 8.40; pools follow terrain, vanish under bridges. The
    bloom answer for the owner: retail has no true bloom pass (it is part of the photoshop LUT) and bloom
    cannot light the road - the pools do. Not done: the TD_ShadowBox translation (retail art), traffic
    brake pools, the ps_2 G/B decode law.
  - **The map editor MVP** (WORLD_VIEWER panel, "Map editor" section): places `<mods>/props/*.obj` props
    with position + heading (deg) + scale + shader family + the `fuel` checkbox (a working gas station),
    "at camera" / "snap Y" via the ground heightfield, commits by appending/rewriting the exact
    `props.txt` line (ModProps::listPlacements / appendPlacement / replacePlacementLine, new
    mod_props_editor.cpp) and reloads the props in place; the placed-lines list edits any existing line.
    This is the mods-overlay asset route (docs/re/modding.md); retail bank placement (EGAS_A-scale
    buildings) stays with tools/mod/add_spot.py and is the next editor step. Verified: a test kiosk prop
    placed through the same file format loads and renders in the viewer (the ModProps log line + the
    drawn box); the interactive loop is ImGui and needs the owner's hands. Not done: live preview while
    dragging (needs the ModProps upsert refactor), gizmos, retail-bank placement UI.
  - README: the map editor paragraph in the WORLD_VIEWER section, the `-noheadlights` CLI cell, the pools
    in the FREERIDE row. rain_shadow_spec B.7 correction + car_lights_spec 7 (the pools).
- Date: 2026-10-27 (seventh session; **the workspaces are consolidated —
  the project lives in `C:\OpenTDU2` only now**, see WORKSPACES.md. The
  glm folder is a parked mirror; work here, on branch `win`. The `win`
  workspace's fifth session (radio, in-car menu, back-on-track hold) and
  the glm sessions six to eight (wake-up cinematic, HUD radio / Accessories)
  are all merged in this tree)
- Workspaces: **three concurrent agents** — `C:\OpenTDU2` (Claude, branch `win`,
  the merge hub), `C:\OpenTDU2-glm` (GLM, `glm`), `C:\OpenTDU2-cline` (Cline,
  `cline`); all three built and verified. `sync-workspace.ps1` reports and merges
  between them, `C:\otdu2-deps` makes configures offline — see `WORKSPACES.md`
- Last known-good tag: `win-verified` (stale — see the 2026-10-27 commits below)
- Newest (2026-11-01, `glm`, session 66): **the tester launcher + the name/build watermark — the
  distribution path for outside testers** (the owner: *"I want some people to test it ... if we made
  a launcher and they can enter there profile name and with it can we water mark the game with there
  name and Build number? The launcher most have them point to there version of the game"*). Three
  pieces, all port-invented UI glue (nothing retail behind it):
  - **The build stamp** (`cmake/BuildInfo.cmake` + `StampBuildInfo.cmake`): the repo's first version
    mechanism. `build <git count> (<hash>)[+dirty]` written to `<build>/generated/build_info.h`
    (`OTDU_BUILD_*`) — at BUILD time, not configure time (`build.ps1` only reconfigures when the
    cache is missing, so a configure-time stamp would go stale): the `build_info` custom target
    re-probes git on every ninja invocation and rewrites the header copy-if-different, so a no-op
    rebuild recompiles nothing while a new commit recompiles just the includers. The compile
    timestamp is `__DATE__/__TIME__` (`OTDU_BUILD_TIMESTAMP`), correct for as long as the file
    stands. No git / not a repo (a tester's zip): builds anyway with "unknown". Consumers: the
    startup banner, the tester window title, the watermark, the launcher's title.
  - **The watermark** (`source/debug/watermark_overlay.{h,cpp}`, a `CoordsOverlay` clone pinned
    top-right — the coords readout owns the top-left): `-tester <name>` draws
    `<name> · <build string>` every frame in a NoInputs translucent ImGui window; `-nowatermark`
    suppresses the draw (the log keeps the `Tester:` line). Without `-tester` nothing draws — the
    owner chose testers-only, normal runs stay clean. The window title carries it too
    (`SDL_CreateWindow` title in `createWindow`): `- LauncherTest · build 354 (…)`.
  - **The launcher** (`tools/launcher/main.cpp`, `otdu2_launcher`, WIN32 — the repo's first GUI exe,
    deliberately no `otdu_engine` link: a tester distribution is this exe + the game exe + their own
    game data): the game-data folder with the `Euro\Bnk` validation (the `GSFile::HasBankFolder`
    rule), the tester's name, Launch — it finds `opentdu2*.exe` beside itself or in
    `..\build-*\bin` (the `launch\resolve_exe.bat` probes) and starts it with `-game <folder>` and
    the name as both `-playername` (the avatar's, the existing `gm_avatar_choice.cpp` `PlayerName`
    path) and `-tester` (the watermark). Both fields remembered in `launcher.ini` beside the
    launcher; empty name = a plain clean launch; the game runs with the exe's folder as its working
    directory, like double-clicking it.
  - Verified: build exit 0, the no-op rebuild re-stamps nothing; headless runs 0 stalls —
    `-tester Daniel` draws `Daniel · build 354 (ce292c4+dirty)` top-right
    (docs/images/watermark_s66.webp), no `-tester` draws nothing,
    `-nowatermark` logs the tester without the overlay; the launcher opened idle (no auto-launch,
    three probes), launched from the saved ini (the spawned game's title carried the name + build)
    and rewrote `launcher.ini` on close. README: the `-tester`/`-nowatermark` CLI row, the tools
    tree + a tester paragraph. Not done: packaging (a zip with the two exes is manual until a
    script exists), `-playername` in the avatar-choice screen is the pre-existing path (its own
    session), the launcher's folder dialog is the legacy `SHBrowseForFolder` look.
- Newest (2026-11-01, `glm`, session 64): **the tunnel carve is true now — the dirt sheet holds no
  ground, the car rides the bore roads** (the owner: *"we kinda fixed it but ... may of just made it
  invisible to some degree and not truly fixed the issue, I was driving and in a tunnel it looked clear
  but car lifted as if was on a ramp, and theres a tunnel which is still filled with the dirt"*). Session
  56's carve was render-only: `EdenWorld::getHeight` sampled the raw sheet, so inside every tunnel / bridge
  the wheels still rode the invisible dirt ramp (reproduced headless: a bore spawn in the session-42
  underpass climbs y 31.8 -> 35.2 over 80 m). Three pieces:
  - **Lead closed first**: the filed-open sector `.flg` 'FLAGDATA' container decoded — it is the sector's
    **minimap resource** (a 128x128 8-bit indexed top-down image + palette + contour/road centerline
    polylines the exe averages into 0x29c-byte records at 0xa2cde0), not a carve mask
    (docs/formats.md 'FLG'). And **no `hkpHeightFieldShape` exists in any sector `.shk`** (the static
    files are extended meshes / boxes / capsules only, rural sectors too) — retail's terrain ground is
    [ENC]/runtime; the port's heightfield callback design stands (docs/re/freeride_spec.md).
  - **The physics carve**: `getHeight` refuses the carved cells (the same whole-quad rule the render
    split uses — any carved corner disables the bilinear sample), so the wheels fall through to the
    sector's Havok road triangles, which do ship under the bores. `getHeight` callers all benefit
    (drive, spawn fits, cinematic ground, camera clamp).
  - **The carve ground**: where the .shk has no triangles (off-road shoulders, the slot between the
    cutting's two carriageways) the car needs the bore floor: new `EdenWorld::getGroundHeight(x, z,
    fromY)` — the `VehicleWorldCollision::HeightFunction` callback gains the query origin `fromY` —
    serves carved cells the topmost up-facing triangle **inside the tunnel / bridge footprints** of the
    sector's own meshes (`BuildCarveGround`, 8 m XZ buckets; the footprints matter, not the keyword
    nodes' own meshes: the cutting shell carries 513 triangles, the bore roads and floors inside its
    footprint 2690). The terrain branch of the wheel rays loses to a triangle hit within 0.25 m (the
    carve ground and the .shk roads are the same surface — the road keeps its tarmac tyre). `-nocarve`
    restores the raw sheet for A/B (render and physics together now).
  - Verified (docs/images/tunnel-ground-*.png + `-logdrive` pose logs): underpass bore spawn lands on
    the road triangles at y 26.2 (was 31.8 on the sheet) and drives through flat (25.2 -> 25.5, was the
    31.8 -> 35.2 climb); the roundabout cutting 3-3-3-5 drives its descending bore road 16.7 -> 11.6 m
    clean and exits onto the surface road at 165 km/h; the interchange 3-3-4-5 and the dealer bridge
    BP_PONTA clean; drop probes: the two carriageway roads hold, the between-tube slot voids fall
    (recover: hold back-on-track — the wedge skirt cells stay solid, so the user's dirt ramp is no
    longer reachable); the bore interiors are identical to `-noterrain` from the road, and the
    roundabout surface above shows no carve holes. Regressions: the house-street 700-frame drive
    (0 stalls), `-storystep 5` / `-storystep 7`, `-gm TESTCAR`, `-atwash`, casino spawn, `-nocarve`
    A/B — all exit 0. README `-scantunnels` / `-nocarve` row + the World row, docs/formats.md FLG.
    A headless-only crash found on the way: the carve-ground slots were stored as flat offsets and
    read as triangle ordinals (`slot * 3`) — out-of-bounds at the first bucket query past triangle 0,
    deterministic at the cutting's east half; fixed by storing `size() / 3`.
    Not done: the skirt wedge visible through the cutting's open slot is left as session 56's verified
    render (unreachable by car now, identical to `-noterrain` from the road); the off-road mouth-rim
    voids (back-on-track recovers); retail's real terrain-collision source stays [ENC].
- Newest (2026-11-01, `glm`, session 63): **the cutscene tool landed — the manifest, the CINVIEWER
  upgrades and authored captions** (the owner: *"we should have a dedicated tool that you's can also use
  headless ... I can hook the dialogs up and the scene it's self ... refs dumped for the scenes given
  location/coords, assets, characters, props, voicelines. UI pop ups"*; subtitles requested for
  accessibility). Phase 2 of the cine_tool roadmap (docs/STATE.md session 2026-10-29: dump -> viewer ->
  editor). Two pieces:
  - **`otdu2_cine_tool manifest`** (tools/cine_tool/manifest.cpp + the shared helpers moved to
    cine_common.h): dump plus the resolution layers — the .sce REFS -> the bank's .anm/.fxa (via
    BankHashcodes), the objects -> entities.xmb entities, Entity__PNJ -> the DBPNJ row + .avt body + the
    10 outfit slots from db_data.cpr (read offline through LoadXmbFromFile, the GSDatabase decrypt), the Fx
    events -> .bfx SND nodes -> per-language wav names (the -lang bank <stem>__<lang>.bnk; verified us vs
    fr flip), the message protocol (flsh:/shad:/weat:/wmod:/snd: + the actor codes known, zcvn/zcne/zcde/phot
    flagged [ENC]), the story kind placements and the -avatar ruling (DBPNJ outfit + the _W variant). Full
    tree: 136/136 JSON parse, 142 voice cues, 84 subtitle skeletons, 426 character rows. The skeletons are
    named by the DEFINITION (C_MEETING_TESS.csv.skeleton) so the filled file drops straight into
    <userdata>/cine_subtitles/.
  - **The CINVIEWER tool layer + captions** (gm_cin_viewer.{h,cpp}, cine_subtitles.{h,cpp},
    intro_cinematic.{h,cpp}, cinematic_audio isVoiceRoot): the cinehud (shot timeline with cut points +
    voice-tick marks, the shot/time/camera readout; -cinehud 0 off), pause (the camera key, press=camera
    headless) + 0.5 s steps + the browser's scrub slider through the new IntroCinematic::seekMain (the
    -cinetime seek path; -cinetime now seeks CINVIEWER too), -cinepaths (the camera Position keys, actor
    paths and scene origin axes projected through the active camera; pairs with -cintopview), the ImGui
    browser (the bank's cinematics click-to-play, a bank/cinematic pair to load), and the **authored
    captions** — <userdata>/cine_subtitles/<C_NAME>.csv (start,end,"text") drawn bottom-center in
    IntroCinematic::draw for every scene user, -subtitles 0 off, -cinesubdump writes the voice-cue skeleton
    (the retail data ships NO subtitle text for the cutscene speech — the text is authored). Verified
    (docs/images/*-s63.png): the caption on screen at t=2.6 inside the 1.75-4.79 cue and absent with
    -subtitles 0; press=camera paused at 21.07 s and press=right stepped exactly +0.5 s (PAUSED badge, the
    camera frozen); the paths at eye level and over -cintopview; -cinesubdump = 2 cues = the manifest's
    skeleton. Regressions: -storystep 5 exit 0, -gm CINE -cinekind 1 exit 0, -atwash exit 0, 700-frame
    FREERIDE drive exit 0 (0 stalls). README tools + CLI rows, docs/re/cutscenes_inventory.md the
    manifest/viewer sections. Not done: phase 3 (.cin/.sce write-back), zcvn/zcne/phot decode (stays
    [ENC]), retail subtitle styling (there is none to copy).
- Newest (2026-11-01, `main`, airport textures): **two texture bugs behind the owner's airport report
  ("in the map screen airport looks ok but then the runways look like this ... some buildings also had a
  graffiti looking texture ... a lot of map is possibly not got the buildings on"), one fixed, one pinned to a
  precise open question.** Repro: `-tp 16780.2,19.8,27512.4` (the EAIR_A spot) + `-clock 13.00`, map zoom with
  the autoplay wheel; in-game at `-tp 16600,19.0,27750`.
  (1) **Fixed: the identifier round trip is not idempotent.** `setupMaterial` looked textures up by
  `find(owner, IdentifierToString(hashcode))` — and re-hashing the string folds slot bytes that land in
  'a'..'z' a second time (GetIdentifier64bit lowercases before accumulating), so the derived hash differs from
  the one textures register under: a per-name coin flip which textures resolve. Caught as `Eden material
  'Type1_F_01': texture 'û«tè_DIR' (slot 6) not found` under `-verbose` (the library ground-plane material;
  the mojibake is the accumulated slot bytes of a >8-char name). `TextureCache::find(owner, hashcode)` (new)
  looks up the numeric hash + the resource-hash alias directly; both scans of all 9454 banks confirm that
  texture genuinely ships nowhere (a legit null now, white fallback).
  (2) **Fixed: the sector buildings drew their 64x64 LOD bakes at every distance.** The airport's stripes are
  byte-for-byte the `sb_airpo_lodc.2db` atlas (red/cream runway rows at its bottom = the owner's screenshot),
  and the red speckle ground near the containers is its industrial patch: the retail swaps these LOD bakes
  (sb_airpo_lodc, sb_gazs_a_lodc, sb_farma_lod_c, ...) for the full atlas when the camera closes in, and the
  port had no such swap — "the change mostly happens the closer you get" = the mip filter resolving the LOD
  mush into stripes. `loadBankTextures` now aliases every LOD bake to its same-folder full sibling (longest
  shared name prefix, largest size: sb_airpo_lodc → sb_airpo_big, sb_gazs_a_lodc → sb_gazs_a) unless
  `-uselodtextures`; 7 aliases land in the airport sector alone.
  (3) **Open, evidence lined up: the spot-scenery ground still stripes** (the runway bands and the red
  speckle survived both fixes). The airport spot's materials are `TD_Building` with the atlas at slot 0
  (`lm_eair_a` — NOT a lightmap: decoding it shows the full airport albedo atlas, 'Ibiza' signage and runway
  strips included) × albedo at slot 1, the meshes carry `Texcoordx2` (both UV sets reach the pipeline as
  TEXCOORD0/1, the 3dg parser increments the usage index per sub-attribute, all shader constants have values).
  The remaining suspect is which UV channel the translated `TD_Building vs68/ps112` wires to which sampler —
  a dxso-translator decode, next session. Also verified along the way: the area terrain atlases are aerial
  photos (area-2-4-c shows the whole airport), the terrain materials bind COLOR only, the map's zoomed view
  shows no buildings at level 1 *by design* (spot exteriors stream at level ≥ 2 — the owner's "buildings not
  on" impression), and the flat map is the retail bake so it always looks right. Regression: `-storystep 5`
  exit 0, no stalls.
- Newest (2026-10-31, `main`, GPU descriptor lifecycle): **the owner's graffiti-texture roads, the map's
  "massive corruption" and the 10 s stall → device lost were three Vulkan descriptor-lifecycle bugs, named by
  the validation layers and fixed** (the owner: *"all road texture wasn't correct and looked like grafitti
  after I had checked the map with select and there was massive corruption"* — their report described the
  window of my own `-storystep 5` verification run, which died at frame 76036 in a `vkWaitForFences` -4 after
  the map was opened on it: the first ~76 k-frame play this engine ever had, every agent verification before
  being ≤ a few thousand frames, which is why three engine-level bugs read as a regression of the merge).
  **Recipe: `-vk_enable_validation_layer` + the autoplay script reproduces the churn, and the violations name
  the objects.** 68 violations in one validated 12 k-frame run; three bugs:
  (1) `RenderDevice::flushPendingDeletes` ran at `beginFrame` *before* that slot's descriptor pools reset —
  and the eden renderer's cached sets live in pools that reset later still (at the next scene begin, guarded
  by `framePoolNumbers`), so a sector unload whose textures were drawn one frame earlier destroyed views and
  buffers that live sets still held (`VUID-vkDestroyImageView-imageView-01026`): the GPU sampled freed memory
  — the graffiti textures — and the poisoned state eventually wedged the device. The pending queues now
  rotate one extra slot recycle (`PendingTextureDeletesOld`): a destroy lands only after *both* slots' pools
  have reset; `destroy()` flushes both batches.
  (2) `FullscreenPass::draw` bound `allocateTextureSet`'s `VK_NULL_HANDLE` unchecked (renderer_2d checks,
  postfx didn't) — the 8192-set frame pool exhausted on heavy 4K frames and postfx drew with null descriptors,
  which reads as the whole-frame corruption. Guarded + logged once (`FullscreenPass: descriptor pool
  exhausted`), cap raised to 16384 (a ceiling, not a preallocation).
  (3) the eden draw-set cache (P2 step 1, `7d1059a`) keyed on {program, Hashcode, overrides} — but the
  runtime swaps a slot's content behind a stable material: the direction signs' composed faces, the police
  gyro lens, the sector SHADOW setup; and the zoomed map's `loadAround`+`unloadFar` unloads and loads sectors
  in the *same frame*, so a freed material's address and hashcode came back with different textures. The key
  now carries the material's current slot-hash stamp (`EdenMaterial::textureStamp()`), and the cache clears
  at the scene-begin pool reset instead of lazily at the first draw — a frame that records without a scene
  reset on that slot (the flat map skips the 3D scene) re-bound reset sets, which the second validated run
  caught at the map interaction ("not a valid VkDescriptorSet") and the third run's clear-at-reset fixed.
  Also fixed by (3): signs of one variant showing the first sign's face (same material pointer, different
  payloads — never verified because `-atsign` spawns one sign). Verified: the same scenario under validation
  went 68 → **0** violations (12 k frames, map + zooms + drive; the only message left is a shutdown-time
  6-buffer leak note at `vkDestroyDevice`); a 90 k-frame soak (story chain, longer than the 76 k crash) exit 0,
  **0 stalls**, VRAM flat at 2434 MB; the dealership/showroom renders clean. The bugs are engine-level (the
  flush dates to the original upload, the cache to P2), not the merge.
- Newest (2026-10-31, `cline`, twenty-seventh): **the map cursor snaps to shops and to roads, fast travel
- Newest (2026-11-01, `cline`, session 61): **the traffic drives instead of warping - smooth corner arcs,
- Newest (2026-11-01, `cline`, session 62): **the three-branch merge stopped wedging on the session logs -
  docs/STATE.md, docs/SURVEY.md and README.md now use the built-in `union` merge driver, and
  `update-and-rebuild.bat` self-heals an unfinished merge.** The owner: *"I ran E:\OpenTDU2-main\update-and-rebuild.bat ...
  error: Merging is not possible because you have unmerged files ... is there something that can be done to prevent
  the conflict issues"*. Root cause: every session **prepends** its handoff entry to `docs/STATE.md` (and
  `docs/SURVEY.md`) and **appends** a row to the README CLI table, so merging two agent branches always produced one
  conflict hunk at the same place - and a run killed at that point leaves `.git/MERGE_HEAD` behind, which makes every
  later `git merge` fail with "Merging is not possible because you have unmerged files" (that is the message he saw,
  the conflict was `docs/STATE.md` alone). Fixes: `.gitattributes` (`docs/STATE.md`, `docs/SURVEY.md`, `README.md` =
  `merge=union`; the union driver keeps BOTH sides, so no entry is lost and the merge continues - verified: the
  merged log holds main's and glm's blocks, the README holds `-offroadhud`, `-clock` and `-trafficnear`, 0 conflict
  markers) and `update-and-rebuild.bat` (new: aborts a merge left half-finished by an earlier run at the start,
  writes the same three union rules into `.git/info/attributes` so an older checkout works too, and on a real
  conflict prints the conflicting files + the `git add` / `git commit --no-edit` recovery instead of a bare
  warning). Verified end to end in the owner's tree: `[0/5]` abort -> steps 2-4 "Already up to date"/clean ->
  `build exit: 0` -> `SUCCESS: OpenTDU2-main is up to date and rebuilt` (`build-main\bin\opentdu2-main.exe`), and in
  a throwaway 3-branch repo (bare origin + main/glm/cline + a deliberately wedged merge): the stale merge is
  aborted, the logs union with both entries, and a genuine content conflict still stops the script with the file
  named. Real code/doc conflicts are deliberately NOT auto-merged.
- Previous (2026-11-01, `cline`, session 61): **the traffic drives instead of warping - smooth corner arcs,
  28 cars instead of 11, cars that bump instead of clipping, clean glass, a 620 m draw radius with the
  despawn outside it, road-graph-component gating and no casino traffic.** The owner: *"traffic's logic
  last i tested cars kinda didn't turn naturally but they just quick turned 90 degree ... i also want
  possibly more traffic density i want roads to feel lived in also cars must have collision and not clip
  through 1 another ... they all seem to have cracked like windows ... i also don't want cars to disappear
  in front of you it's ok if you had a set radius around player to despawn cars but anything in the draw
  distance ... don't have cars running on a island that you're not actually on and casino Island should
  never have traffic"* (`docs/re/traffic_spec.md` "Steering, density, collisions, glass"). **Turns**
  (`traffic.cpp`): the cars were placed on the link curve every frame, so the yaw snapped at a node. Now
  `pathAhead()` gives a **pure-pursuit target** up to 12 m along the right-hand lane (crossing into the
  next link with the tangent and lane offset blended over 4 m), the yaw turns at a rate limited by a
  5.5 m/s^2 lateral budget (0.4-2.6 rad/s) and the speed by `sqrt(5.5 / curvature)`, and the progress is
  re-anchored to the closest path point. Measured: turn <= 0.96 rad/s (0.016 rad/frame vs the old 1.57),
  worst lane offset 2.8 m. **Density**: 4.2 cars per aiconfig unit (was 1.6), max 56 (was 28), a minimum
  of 10 cars on a road with traffic, a spawn from 40 m when it is **behind** the player
  (`setPlayerHeading`) - 28 cars at 14:00 instead of 10.8, update 0.4-0.6 ms, 67-71 fps. **Collisions**:
  `resolveAgentCollisions()` (circle separation split by mass + a restitution 0.15 impulse, the same circle
  model as the player collision), a junction **yield** to a moving car inside the box, the red stop line
  moved from 3 m to **5.5 m** (outside the box) and the follow gap measured bumper to bumper - the queue
  overlap fell from 181 contacts / 3 s to 0-5 (`-trafficcrashtest` -> 1.5 m into a car at 8 m/s comes out
  **5.0-6.9 m apart**). **Cracked windows**: the traffic glass materials carry common_car's `glass_b` (a
  **starred / cracked pane**, decoded) in DETAIL2, and the CarPaint shader lerps the base into it;
  `neutraliseGlassArt()` binds the player glass's black `g_i` base there (`-trafficglassart` for the A/B,
  windshield patch grey 78.4 -> 53.2). Also `assignEnvironment` is no longer a stub (the dynamic env cube
  + the weather's ENVCOEFF, like `CinematicCar`), rolling wheels (`WLR_*` / `DISC_*`, `-trafficwheel`) and
  the taxi's missing ground lift (0.000 -> 0.220 m). **No pop-out**: draw radius 420 -> **620 m**, despawn
  **820 m** and only when the car is *not* drawn (counted in the log). **Islands**: `casino` returns from
  `load()` at once, and `buildComponents()` keeps the cars on the **connected road-graph component of the
  player** (Ibiza 1 component; the casino island's 3 links sit outside Ibiza's road extent). Ruled out:
  the shader (an exact permutation), the shadows (`-noshadows`) and the cube (`-nocube2db`) leave the
  cracked pattern identical - it is the DETAIL2 art. Fixed in passing: `-trafficnear` was **never active**
  (a namespace-scope `static bool = CmdLine::HasArg(...)` runs before the command line is parsed). Verified
  headless: 1600 frames of driving (29 cars, 0 contacts, 0 removals), `-trafficnear` (2 despawns counted),
  `-trafficcrashtest`, `-island casino` ("has no ambient traffic"), the glass A/B grey numbers; shots
  `docs/images/traffic-town-s61.webp`, `traffic-glass-ab-s61.png`, `traffic-night-s61.png`.
  refuses a road that has not been discovered, and the road network is drawn the retail way (discovered
  tarmac blue, discovered dirt yellow, undiscovered grey).** The owner: *"i need map behaviour improving
  the cursor that's used is correct but there is a thing where it should snap on to shops etc... when
  close and it kinda also snaps on to the road where if it's discovered you can automatically fasttravel
  to but if undiscovered it doesn't allow also dirt roads and roads use 2 different colours yellow is for
  discovered dirt roads and blue is for normal"*. **Snapping** (`MapScreen::updateSnap`, called from
  `tick` before the markers): the reticle sticks to a shop icon of the current tab within 26 px and
  otherwise to the nearest road curve point within 18 px, with 8 px of hysteresis so it does not flicker;
  the movie's `/curseur` is placed on the snapped point and `cursorWorld()` (OK / fast travel) returns it.
  Radii are canvas px, converted to metres with `mapScale`, so the reach is the same on screen at every
  zoom level. `freeCursorWorld()` is the un-snapped point (panning and zoom-to-cursor use it). **Fast
  travel** (`MapScreen::getFastTravelPoint`, F5 / pad option 2 in FREERIDE, replacing the old cheat
  teleport): the snapped road must be one the player has driven - `isRoadExplored` - and a shop goes
  through the road in front of it (120 m); refused otherwise, and `/:IsTeleportOK` (false until now) is
  what the movie reads. The map closes on a successful travel. **Road colours** (`drawIsland`): every
  link is drawn now, in the retail [HudMap] palette of `docs/re/freeride_spec.md` 4 - discovered tarmac
  cyan 0/255/255 (highway 29/180/215, circuit 102/102/204), discovered dirt 243/196/6, undiscovered
  140/140/140 - with a per-pixel decimation of the projected curves so the whole island stays cheap.
  The class comes from the `DAOR` +12 byte (`edgeClass`, `getRoadClasses()`), which is `0..4` on Ibiza
  (728/246/184/97/95 links) and `0..11` on Hawai; **which value is the dirt one is still not decoded**
  (`docs/re/roads_spec.md`): `kRoadClassDirt = 3` is the offroad slot of the retail table, `-dirtclass
  <n>` overrides it and `-logroads` prints the histogram. Scratch overlay `_dirt_classes.png` (the island
  map with one colour per class) is the way to confirm it by eye. Verified headless: `-logroads` ->
  `0:728 1:246 2:184 3:97 4:95`; a fresh profile (nothing discovered) draws 6702 grey pixels and 0
  coloured ones and refuses F5 (`fast travel refused - that road has not been discovered`); the explored
  profile draws 4705 cyan / 1267 highway / 207 circuit / 222 dirt / 168 grey pixels, OK logs
  `destination on a road at (7422, 17742), 635 m from the cursor` and F5 logs `fast travel to (6952.6,
  36.7, 18167.9)`.
- Previous (2026-10-31, `cline`, twenty-sixth): **the traffic AI takes the straightest link, stops for
  reds in time and blinks before a turn; the police roof light bar is wired.** The owner: *"work on
  traffic logic also fix police lights on top of there cars please they seem to no be properly wired
  or not accurate"*. **Traffic logic** (`source/game/freeride/traffic.*`): `chooseNextEdge` decides
  the next link ~40 m before the node (straightest direction + 0..0.35 rad noise; no U-turn while
  another link exists; the dead-end case reverses on the same link) and stores `NextEdge` /
  `bNextForward` / `NextTurnAngle` / the indicator side on the agent. Only a **real turn** slows the
  car now (turn angle -> 4.5..13 m/s, approached with `v^2/(2*6)`); the old rule dropped every car to
  7 m/s at every node within 18 m, straight through or not, which made phantom jams. A **red signal**
  stops 3 m before the node with `sqrt(2*6*(remaining-3))` instead of the old fixed 18 m zone that a
  19 m/s car could reach before stopping. The **turn indicators** (`FA_W_*`, split per side) blink
  the side of the chosen link at the retail cadence (1.0 s period, 0.6 s on) from the decision until
  1.2 s after the node; `-notrafficlights` still hides every lamp. The 3 s traffic log now reports
  `%zu cars, %d stopped, %d indicating, %d turning, %d at red`; verified over 1150 frames
  (`-trafficnear -logtraffic`, 11 cars, 0 stalls, exit 0; 1-2 indicators and 1 turner every sample,
  3 stopped queued behind the stationary player; no signalized node near the spawn, so the at-red
  path is code-reviewed and will show on a real junction). **Police roof bar**: the traffic police
  bank's `Gyro` lens material (TD_CarPaint, DETAIL `gyro_of`, GLOSSLIGHT `gyro_g`, TEXLIGHT
  `gyro_on`, `GLOSSFCT`) plus 16 `FA_G_<A|B|G|R><1..4>` glow quads were drawn with the data's
  `GLOSSFCT=(1,1,1,1)` and every quad visible - the wrong roof the owner saw. `PoliceLightBar` (new
  `source/game/freeride/police_light_bar.h`) maps the mask's four rings (r centre .. a outer) to the
  four quad groups (R centre .. A outer), writes GLOSSFCT per ring and sweeps one ring at a time
  (1.6 s per turn [INF]) on the dark `gyro_of` lens - the retail wiring (the detail art is the unlit
  lens; GLOSSFCT lights the moving reflector). `-policefullbar` binds the lit `gyro_on` art as the
  DETAIL instead, a brighter test/demo look that is not the decoded state. The retail FA_G billboards
  are **misplaced by the port's TD_Sprite path** (they float beside the car), so they are hidden;
  `-gyrosprites` brings the swept quads back for debugging. Ambient police bars stay off and
  `Traffic::setPoliceLights` switches them on for the chase; the chase cop model (`ChaseMode`) collects
  the same bar + the FA_ lamp sprites and shows headlights, rear position and stop-on-decel (reverse /
  warning hidden). Test hooks: `-policebar` (force on; with `-trafficpark` parks a police car in front
  of the camera), `-trafficparkpolice` (parked with the bar off - the A/B reference) and
  `-policefullbar`. The shader dump shows the lens `TD_CarPaint vs30/ps84` and the glow quads
  `TD_Sprite vs176/ps18`. Verified: day + night park A/B with the default ring sweep (the changed
  pixels are the bar strip, 51 px in a 35x24 box, no side blobs; blue pixels 1 -> 11 and red
  114 -> 127 on the bar), `-police` starts two cops on the road graph with the bar material and
  R/G/B/A rings logged, 0 stalls. Docs: `docs/re/car_lights_spec.md` 6.1,
  `docs/re/traffic_spec.md` "Junction logic"; README CLI row. **Open**: the TD_Sprite billboard
  placement itself (the FA_G quads) and the retail ring-timer period are not fixed/decoded.

- Newest (2026-10-31, `glm`, session 62): **the satnav is the retail screen now, the white bar is gone, and the popups
  actually close** (the owner: *"find the true UI stuff in the game most we have but some stuff isn't accurate back to
  road icon still not found or used correctly, we also have issues of some UI elements not removing them selves after
  so long or not popping up correctly also near the satnav theres a random white bar unsure what that is and the
  satnav's ui isn't accurate as if the ui is underneath the satnav screen"*). Everything below is decoded from the HUD
  movie itself (`-dumphud` extended with shape bounds, fills and sprite matrices; docs/re/freeride_spec.md 7a written):
  - **The satnav**: the movie's `/HUD/minimap/mmap_lure` is ONE solid cyan quad — the placeholder the retail engine
    renders the live map into. The port used to draw its map at a hardcoded (115, 454, 190x190) with a white outline
    square over the movie art — roads ran past the screen edges and the outline read as white bars. Now
    `computeMapRect()` takes the lure box out of the movie (mapped through the same screen-ratio rect `beginStage`
    draws with), the quad is hidden, and the native map (roads, route, detector) draws BEFORE `hud.draw()` — the
    movie's bezel corners, inner frame, bottom band (`#10`) and status icons (`ic_status` red offline, `nb_players`
    car) overlay the map exactly like the retail layering. The white player-arrow triangle is gone: the movie's own
    `cursor` clip is the retail marker, parked on the map centre. The GPS route colour is the [HudMap] 255/102/0
    orange. The distance readout is the movie's own `gps_distance` texts (`/:obj_Distance` + `/:gps_Unit`, the
    FUN_009365a0 protocol: "" with no target) — the port's native "GPS 4.2 km" bar below the map is deleted.
  - **The white bar**: `/HUD/enter` carries an UNNAMED copy of the minimap-content sprite (char 231) at depth 1,
    parked at (-459.6, 229.5) by the movie's own `on` timeline — off stage while parked, but the enter element moved
    to the screen centre for every prompt and dragged it along (the beige bar bottom-left, visible in the session-58
    wash shot too). New `FlashPlayer::setDepthVisible` hides it (and the clones' copies).
  - **The road icon (return to road)**: the tile's `Spot/offroad` child `on` art is the real retail presentation — a
    grass tuft over the road + the blue "A..." hold button (the old port guess "asphalt + arrow" showed the tile
    dead-centre). It now anchors on the nearest road's projected screen position (the same `projectToCanvas` the
    distant markers use, scale by distance), and `-offroadhud` forces it for headless shots. The retail driver is
    encrypted; loc 50 "Return to the road" + the element are [DATA].
  - **Pop-ups / pop-ins**: three port bugs fixed. (1) The `/HUD` clip carries the `16_9`/`4_3` ratio arrangements and
    the port left it PLAYING — every element re-placed twice a second, so the enter tile's prompt/plate/button
    blinked off and back; the game stops the clip on the ratio's label (the GAUGES WhatFormat choice) — it does now
    too. (2) The enter element's kind goto re-places child clips that PLAY their own timelines (the plate's `shape`
    child alone has a `lod` frame that erases the plate) — new `FlashPlayer::stopSubtree` parks them; the prompt is
    stopped deterministically on 'ok'. (3) The big one: `closeGoal`/`closeHint` guarded on `goalTimer >= 0` but the
    auto-close timers call them AFTER the timer went negative — **the goal bar and the hint box never auto-closed**
    (30 s / 7 s, verified dead since session 18). `drawClipOnly` also respects the clip's own `_visible` now (the
    movie's close animation self-hides). Verified: the goal bar closes at ~30 s (`close_popup 1`, gone at frame 2000),
    the wash hint closes after 7 s, the level-up popup still opens and closes.
  - The enter tiles follow the spot: `spotCanvas` projects the spot (race start, championship, wash) with the
    markers' scale formula (clamped 25..100%) instead of the fixed screen centre. The dealer tile's engine-driven
    brand logo (`ic/logo` cyan placeholder) is hidden — the plate alone is the single-player look (the masked car art
    is authored alpha 0 outside multiplayer [DATA]); the brand logos are a not-done. The gradient-mask stencil fix
    (masks mark by geometry alone) is in flash_player.cpp for any gradient mask.
  - Verified headless: satnav shots (`_final_route`, the orange route inside the screen, "2.1 km" in the movie's
    readout), the offroad tile (`_final_offroad`, grass icon + hold button + "Return to the road"), the dealer tile
    stable at frames 100/400 (`_series4` — the 90 s story re-entry at frame 1600 is the mode's own flow), the goal/
    hint closes. Regressions: `-storystep 5` exit 0, `-storystep 7` exit 0, `-gm TESTCAR` enters TESTCAR exit 0,
    700-frame FREERIDE drive exit 0 (0 stalls), `-testlevelup` popup opens + closes.
  - Not done: the dealership brand logos in the enter tiles (engine-rendered), the retail `wrck` metal-detector
    element (the port's native bar below the map stays), `ic_status`/`nb_players` online states (the movie's own
    offline art shows), the retail drivers behind `close_popup`'s ten arguments (the port passes none; it works).
- Newest (2026-10-31, `glm`, session 61): **the pause clock runs, the icons are clear of the player card, and the dealership got the retail UI** (the owner: *"ask if in pause menu the clock/time is accurate and updates properly ... fix pause menu ... in dealerships have this UI hooked up ... when near cars this ui should show up and show there specs"*). The three answers, all verified headless with the shots in `docs/images/*-s61.png`:
  - **The clock**: it was a one-shot snapshot — `GMFreeRide::enter` parsed the static weather preset's *name* once into `PauseMenu::setClock`, `refreshClock` ran once per opening (`bClockShown`), and the port had no running game clock at all (the retail one is `[ENC]`; casino / garage / avatar-choice read 00:00). The owner picked **real PC time**: new `source/game/world_clock.{h,cpp}` (`WorldClockHours/Minutes/IsDay` off `std::localtime`, `-clock HH.MM` freezes it in the weather names' own `%d.%d` shape), `PauseMenu::refreshClock` reads it every tick while open and re-runs the movie's `setHour()` only when the minute changed (the digits roll via the movie's `changeNumber`), the day/night icon follows. The weather preset pick now takes the preset whose hour is nearest the clock at load (wrap-around; `-weather` / `-night` still win), and the traffic density hour comes from the clock too. Verified: `-clock 02.30` shows 02:30 + night moon + the `02.30` preset picked; the digits ticked 13:38 → 13:42 between runs.
  - **The icons**: session 17's `kTabRowLift = 18` still left the card's top edge touching the icon bottoms (measured y239 vs y240 on the 720p capture; retail keeps ~10 px of daylight, `docs/images/retail-pause-freeride.webp`). Raised to **30** → a 13 px gap, `pause_after-s61.png`. Plus the card's money: the movie prints `/:Money` raw, so the port now pushes the retail `"$1,234,567"` shape (a string; the movie renders it) and `refreshPlayerPanel` refreshes money / cars / houses every tick while open (earned money used to need a mode re-enter).
  - **The dealership**: the native DealerMenu follows the used-car-dealer screen (`shopscar.vmf`'s mini menu; the movie itself stays undriven). Shared stat code moved to `car_shop.{h,cpp}`: `ShopCar::PhysicsRow` (the loader stores the row index), `CarSpec` + `FillCarSpec` (the DBCarPhysicsData columns gm_shop decodes), and the retail gauge ranges + `CarStatGauge` (gm_shop now includes them — one copy of FUN_00adea90's math). The Cars step draws the spec panel beside the list (Acceleration 94 / Speed 63 / Braking 74 bars, difficulty 2/5 discs, 320 km/h / 4.3 sec. / 532 bhp on the 612 Scaglietti); **A** opens the retail entries *Buy / Get into the car / Data / Compare / Test drive*: Buy = the colour picker (unchanged), Get into the car = the podium camera alone, Data = the full data sheet (power / torque @ rpm, weight, displacement, engine, gearbox, gears, 0-100, top speed, braking 100-0, quarter mile, L/W/H — the row *labels* are literals, the first cut's guessed loc ids hit challenge texts), Compare = FUN_00b95840's record (the spec panel and data sheet show the base beside every other car), **Test drive** = `TestDriveRequest` with the car, `_default_rims`, the colour choice's `_color_id_1`, `ReturnMode = GM_FreeRide`, the dealer's spot + island (`TestDriveRequest::Island` is new — gm_testcar's world/weather paths used a hardcoded "ibiza"). `gm_testcar::endTestDrive` honours the ReturnMode: the story shop keeps -1 → SHOP (verified `-gm TESTCAR`), the freeride dealer returns to **FREERIDE at the drive's start pose through `GetStoryHandoff()`** and restores the borrowed `LoanCar` / `CurrentGarageSlot`. Verified end to end: `-dealer ECD_FERR` → Test drive → "TESTCAR: end of the test drive (time out) after 8.0 s ... -> FREERIDE at the dealer (12298.7, 6.0, 16374.6)" (the spot's own street pose, not the story ESTOL_B one) → GAME MODE : FREERIDE.
  - **The near-car spec card**: FreeRideHud's compact panel (name, price, top speed / 0-100 / bhp), bottom right, drawn over the drive HUD **and** over the showroom walk (where the drive HUD is hidden — the draw is public and GMFreeRide calls it in the interior branch; it opens its own `begin/end` because the gauge cluster's canvas mapping is active after the gauges draw — the first version rendered 120 px high). Street: `Traffic::findNearestAgent` (XZ over the live agent positions) at ≤ 9 m while slowed, lowest priority in the `tickWorldSpots` cascade (nothing else near, no race / popup); showroom: `InteriorWalk::findNearDisplayCar` (≤ 5 m of the walk position, `IntroCinematic::getDisplayCarInfo` hands out the scene-local position — the display car worlds are island space). Stats per car id once (`CachedSpecCard`): name `FindCarDisplayName`, price (the police cars' 0 → `--`), specs via `FillCarSpec` (`FindCarPhysicsRow` resolves a `_id_car` to its row for the traffic types). Verified: the Enzo at its podium (`showroom2-s61.png`, `$675,000`) and an `Ibiza_Police` car after the test-drive return (`dealer_aftertd-s61.png`, 220 km/h / n/a / 200 bhp — the traffic rows' `_acceleration_0_100_kmh` is 0, shown as n/a).
  - Regressions: `-storystep 5` exit 0, `-storystep 7` exit 0, story `-gm TESTCAR` → SHOP, 700-frame FREERIDE drive exit 0 (0 stalls). README Flash UI + FREERIDE rows, CLI `-clock` row, `docs/re/spot_interiors_spec.md` 4½ (the retail dealer menu) + Not-done reworded, CLAUDE.md priority 3 clock clause.
- Newest (2026-10-31, `glm`, session 60): **the car lights are the retail driver now — GLOSSFCT, not MATPARAM** (the
  owner: *"brake lights, indicators, headlights on the player car then traffic cars — the california has them but last I
  tried weren't accurate, the lights seemed too low and not hooked to correct parts"*). The first cut (session 11) raised
  the lens materials' MATPARAM.x, which only scales a level while the data ships `GLOSSFCT = (1,1,1,1)` — *every lamp
  function lit at once*, which is exactly what the shots showed (amber round-lamp centres always on, brake lighting the
  bumper inserts only, no blink). `docs/re/car_lights_spec.md` (written; the lens shader math, the GLOSSFCT channels
  brake/reverse/position/blinker, the FUN_00c588d0 table, the blink cadence 1.0 s with 0.6 on, the `Fa_` glow nodes and
  the BREAKING/REVERSING/HEADLIGHT ground-pool boxes; the core is a port of upstream `335759a`'s driver, the traffic
  half is ours). `CinematicCar::collectLights/updateLights/applyLights` replaced the `lamps[4]`/MATPARAM mechanism
  (`-lampparam` gone): the 9 light-material names (LGT_FL/FR/RL/RR + the `_SK` roof variants + LGT_L/R/R_S), GLOSSFCT =
  (brake, reverse, position, blinker of the side) per frame — front lamps take the headlights over the position light and
  carry no brake/reverse; the weather keys `car_headlight_flare/_poslight/_caustics/_headlights_intensity` drive it (0 by
  day, 1 at night; the `tunnel_` variants hooked through `setInTunnel`, not fed yet); the `Fa_`/`FA_<L|B|S|R|W>_<pos>`
  glow quads show with their function (`-noflares` off). **Traffic**: the FA_ sprite nodes are parsed by function letter
  (L front lights + B rear position at night, S stop for a *braking* agent — a decel holds the lamps 0.4 s — R/W never,
  the AI does not reverse or change lanes); the shared per-type model draws per agent through a new
  `EdenDrawParams::ForceHiddenNodes` hide list (the old whole-type `bLightsOn` toggle is gone); `-trafficbrake` /
  `-trafficparkface 1` test hooks, `-notrafficlights` kept. `-carlights 0|1|2` (0 dark, 1/2 forced on),
  `-indicator left|right|hazard`. Verified on screen (every state a screenshot, `docs/STATE.md` session 60): day rear is
  the dark `lgt_of` art; night = red position rings + lit projector lamps + amber side repeaters; night+brake = red
  triangles + third-light strip; indicator left = left amber centre blinking (lit/lit/off frames at 0.33/0.5/0.94 s of
  the cycle); reverse = white wedges; traffic = headlight sprites at night, red brake sprites day and night with
  `-trafficbrake`, none by day otherwise. Regressions: `-storystep 5` exit 0, a night drive exit 0 (0 stalls). **Not
  done**: the three ground-pool boxes are collected, visibility + INTENSITY kept right, but the renderer still skips
  TD_ShadowBox (the projection constants decoded from the bytecode — Var_VS reg 60 = box world-view-proj, Var_Scene_PS
  0..7 = the ground projection — are not fed); the headlight beam / projected headlight (upstream sections 7-9) and the
  tunnel detection are not ported.
- Newest (2026-10-31, `glm`, session 59): **road names on signs — the Ibiza direction signs show their destination names** (the
  owner's ask; nothing overlaps `cline`'s active police-chase / speech work). The port loaded the sign actors and drew them with the
  shared blank plates: each sign's text travels in a type-4 payload of the sector `.pmi` A lists that nothing consumed (`pmi_spec.md`
  1.5 had the charset). New `source/world/sign_text.{h,cpp}` + `tools_probe/sign_probe.py`: the payload grammar is `extraSize -> row
  windows` (9 shapes, `docs/re/sign_text_spec.md` 2 — name windows of 18/20 chars zero-padded, trailing distance bytes, distance
  charset `0x1b..0x24` = 0..9 and `0x25..0x2a` = 10..15 [INF]); the blank plates are two bands of the `rs_maps` atlas (commonworld.bnk,
  green arrow plate `v` 0.017-0.065, white arrow plate `v` 0.213-0.291); per variant model (`rs_dir[lr]_a..e`) `BuildSignLayout`
  extracts the plate text zones from the `.3dg` (converted triangle list -> shared-edge quads -> v-band/height grouping); a face =
  full 1024x1024 atlas copy with one row stamped per zone (frontend 'Berlin Sans FB' page `fla_0116` pixels + `Font::findGlyph`
  metrics through the new `FlashMovie::getFontPagePixels` / `GSFlash::getMainMovie`), CPU-encoded DXT1 + box-filter mips, uploaded via
  `createFrom2DB` (same sRGB format as the original - no gamma shift); `EdenWorld::drawInstances` swaps it into the model's `rs_maps`
  slot for the draw and restores right after (shadow-map mechanism), cache keyed payload-hash x variant, LRU 160, freed at
  `EdenWorld::destroy`. `-nosigntext` / `-logsigns` / `-signfacesdump` / `-atsign [m]` (spawn scan like `-atwash`; the faces look
  along -Z). Verified: Ibiza spawn at sector-3-0-5-6 `RS_DirL_D_act` (22977, 5259) - faces compose from the streamed payloads
  ('SANT VINCENT 4 / Platja / Balasant'...), plates readable in the 2560x1440 shot (`docs/images/sign-text-s59.webp`, composed green
  plate `sign-face-composed-s59.png`); A/B vs `-nosigntext` same pose: sign region mean diff 5.24, controls 0.0 (deterministic);
  `-storystep 5` exit 0; 700-frame FREERIDE drive exit 0, VRAM flat; Hawai'i exit 0 - **its signs carry no payloads at all**
  (2859 A lists, 0 type-4 [DATA]: the US-style `rs_arn*` models sample the atlas' pre-baked lower half and were never blank - the
  fix is Ibiza-only by data). Full entry below.
- Newest (2026-10-31, `glm`, session 58): **the car wash is wired — the retail washing cutscene plays from the driver's seat, for the fee, on both islands** (finishing `cline` session 25's WIP handoff,
  `docs/re/car_wash_spec.md`). New `source/game/freeride/car_wash.{h,cpp}` keeps the island's `ECW_*` spots (3 Ibiza / 5 Hawai); stopped
  within 35 m the FREERIDE enter element shows the retail `carwash` tile (the sponge-and-droplet art of `/HUD/enter/Spot`) with `Enter`,
  the retail hint 3913 fires once per approach, and validate pays the $10 fee [INF] and switches to CINVIEWER with `C_WASHING` at the
  spot frame, the player's car in its owned paint, `islands/<island>/exterior/ecw_a.bnk` by the request's new `Island` field; FREERIDE
  resumes under the StoryHandoff where the scene left the car — on any island now (`StoryHandoff::bStory` separates the dream's return
  from a mode's own; `bFromHandoff` also suppresses the house flow), and the scene car's pose resolves because the request carries the
  `.sce` actor name (`CarActor = "PLAYER_CAR"` — the ecw bank has no `Entity__PosCam`/`CinCar` at all, so every car-pose lookup failed
  silently before). `CinViewerRequest` gained `PickX/Z` / `Island` / `ExteriorPrefix` / `Car` / `bPlayerCar` / `CarActor` / `bStory`;
  the profile saves `been_in_car_wash` (retail `SCPhoneCallsSave +4`) and `car_wash_coating` beside `fuel_litres`; `-nowash` / `-atwash`
  (spawn 18 m in front at the spot's ground height, yields to a handoff) / `-logwash`. Verified headless end to end: prompt at 18.1 m
  (`carwash-prompt-s58.webp` — the 3913 hint + the tile) → `paid $10 (money 19990)` → `C_WASHING` 16.4 s (`carwash-scene-washing-s58.png`)
  → FREERIDE at the scene-end pose (`carwash-return-s58.png`); Hawai's chain, `-nowash`, and `-storystep 5` all verified. **Plus one
  render-device guard the scene exposed**: a single broken camera (`c_washing_02`, the in-car shot — parked, spec §9, *not a regression*,
  the session-24 build reproduces it) NaNs the cinematic camera, the shadow pass then draws every streamed instance individually
  (411,696 draws / 1.55 GB of constants per frame) and floods the 512 MB transient buffer — logging each failure was 2.6 GB of log per
  minute, which read as a hang; `allocateTransient` now logs the first failure + one total per frame.
- Newest (2026-10-31, `cline`, twenty-fifth): **the "petrol station" next to the car wash *is* the car wash — retail geometry — and the retail washing cutscene already plays in the port; the FREERIDE wiring is documented as WIP for `glm` (the owner's call, not a blocker).** The owner: *"Is this a custom petrol station or was this in game… that area has a carwash next to it and it hangs over the road a bit… which makes me doubt this petrol station."* Answered from the data, not by eye: `islands/<island>/exterior/ecw_a.bnk` (`Euro/Spots/Carwash/CW_A/…`) is one Scenery set + 4 props (sponge / hose / boombox / 2 vfx), local bounds (-19.3, -0.2, -21.9)-(23.1, 11.1, 33.9) = 42 x 56 m, **3 placements on Ibiza and 5 on Hawai** — a fuel-station-shaped forecourt (canopy, shop, price totem, tunnel mouth) with **no pump asset at all** in the asset index (`gaspump_d.2db` exists in exactly one bank, the `gph_l2d` house garage), and the canopy over the road comes from the spots.bin position/heading that `SpotExteriors` applies. Our own stations are elsewhere (`FUEL1` prop at 11660, 5.3, 15866 and the `EGAS_A` spot copy at 12665, 16698). The bank also carries the **whole retail washing scene**: 10 `.cin`/`.sce` timelines, 18 NPC/avatar `.anm`, 3 washing `.wav`, and the tunnel interior bank `icw_a.bnk` (rollers, machinery). **Verified the port plays it today**: `-gm CINVIEWER -cinbank islands/ibiza/exterior/ecw_a.bnk -cin C_WASHING -cinspot 8129,26222 -cinexterior ecw_a` -> `CINVIEWER: playing 'C_WASHING' … camera (8115.7, 149.6, 26210.0)`, screenshot `docs/images/carwash-scene-s25.webp`. What is *not* done is the trigger: `CinViewerRequest` cannot carry a spot/island/car (only the command line sets them), `StoryHandoff` returns the car on Ibiza only, and there is no `car_wash.*`, no HUD tile and no profile flag. The 7-step plan, the traps (the cinematic is `C_WASHING`, not the `.cin` name — with the file name it plays nothing) and the caravan answer (no such asset: a caravan park is sector decor, `camping.2db` / `sf_tente`) are in **`docs/re/car_wash_spec.md`**.
- Newest (2026-10-31, `cline`, twenty-fourth): **the road barriers were never loaded at all — the `Sector-*.S.pmi` list (type 3, skinned static guard rails) was skipped by every loader; it is now drawn, verified against the exporter's own AABBs (median 0.47 m) and on screen.** The owner: *"road barriers aren't fully showing up and I think some world assets may not be hooked up like grass and stuff"*. `EdenWorld::loadInstances` iterated `-v`/`-o`/`-a` only and its type dispatch dropped type 3 on the floor, so 12-14 of the 24 `SF_GLISS`/`SF_GLISO` rails around the spawn in Hawaii (and 103 in one Ibiza sector) were never drawn — and `VehicleWorldCollision` had the same `-v`/`-o` list. Both now read `-s` — but `Library.dhk` (864 bodies / 461 models) holds **no** `SF_GLISS`/`SF_GLISO` entry, so the physics addition adds 0 instances today and the rails stay non-collidable until the sector `.shk` is read (see the session entry). The type-3 payload is one bone matrix per palette entry (`extraSize / 64`; the 7 bones of `SF_GLISS` and 8 of `SF_GLISO` match the library PRIM's type high byte and its `BONE` section exactly), applied **before** the instance matrix as the vertex shader does (`v * Bone_i * World`); the record's own AABB is the *un-skinned* rotated box, so the world box is rebuilt from the model bounds through the palette (that is what the numbers below measure). Screenshot: `docs/images/` — the barrier runs on both shoulders of the coastal road. Next: the islands' *grass* is the terrain `SHADOW` slot of TD_HeightMap (the `-s.2db` map doubles as the grass layer, `eden_renderer.cpp` 545) and `updateProgram` still guesses the detail layers from slot indices 2/4/6 while the shipped material puts SHADOW at 2, DETAIL1 at 4 and an unnamed slot at 6 — see the session entry's "Not done".
- Newest (2026-10-31, `cline`, twenty-third): **the owner's remaining crashes are not a stale build — they are a driver fault that arrived with the ray tracing work, and the hub carries a build directory that can never be rebuilt.** The owner asked whether the instability in `E:\openTDU2` came from running old builds. Checked: the hub is exactly at `origin/win` (`9990ce6`, 0 behind, 0 ahead), `build-win\bin\opentdu2-win.exe` (30/10 23:32) is newer than every source file and every fix commit, and the fixes are in the history (`a6593df` borrowed textures outlive their bank, `bafbe5c` the chase-arrest texture owner, `86bf99e` the watchdog + the one clear device-lost report) — the watchdog is demonstrably running in the owner's own log. So the build is neither stale nor the variable: the Windows Application log holds **21 `nvoglv64.dll+0xecf46c` access violations** (`0xc0000005`, once `+0xecf4cc`) since **10-28 19:27**, *the same fault offset in every binary* — `opentdu2.exe` x11, `opentdu2-cline.exe` x6, `opentdu2-win.exe` x3, `opentdu2-glm.exe` x1 — 1-5 minutes into a session, on a non-main thread, and **no other application on the machine faults in nvoglv64** anywhere in the log window. The first one is the same day as the ray tracing commits (`2621725` step 2, the acceleration structures; `96b736f` step 3, the world's own geometry; `dd4fd13` the Advanced graphics page — all 10-28); before that, zero. The owner's `options.ini` has `ray_tracing = 1` **and** `ray_tracing_world = 1` (whose default is 0) at 3840x2160, and their log shows the world TLAS rebuilt every frame (3551-12626 instances; the offscreen pass is 21.5 ms of a 23.4 ms frame). The five dumps in `%LOCALAPPDATA%\CrashDumps` put the fault inside `nvoglv64.dll` (base `0x7ffcd6260000` + `0xecf46c`) with `nvgpucomp64.dll` (the driver's shader JIT) and two third-party implicit Vulkan layers loaded (CapFrameX `cfx_osd_vklayer`, Aurora/CheatHappens) — the layers are not the whole story, since only opentdu2 crashes. New tool: **`tools_probe/dump_info.py <dump> [--stack]`** — the exception, the module list and the faulting thread's call chain as `module+offset` (the app-side frame in the dump is `opentdu2-win.exe+0x6d8900`; the build produces no PDB, so a function cannot be named). A/B tests for the owner (an agent must not run the hub's build): `-nort`, `ray_tracing_world = 0` in `options.ini`, `-rttest`, a lower resolution, `DISABLE_CAPFRAMEX_VULKAN_LAYER=1` + `DISABLE_CH_LAYER=1`, and the driver version (32.0.16.2101). **The genuine stale-build problem found** (not the crash cause): `launch\{02_Intro_Party - Copy, 08_Hawaii - Copy, 09_Casino_Island - Copy}.bat` hardcode `build\bin\opentdu2.exe` (30/10 04:22 — no `-logcarlights`), and `build\CMakeCache.txt` still says `CMAKE_HOME_DIRECTORY=C:/OpenTDU2`, a path that no longer exists, so that build directory can never be reconfigured (the documented CMakeCache trap) — every numbered bat goes through `resolve_exe.bat` -> `build-win\bin\opentdu2-win.exe` and is fine. All builds share one userdata folder (`%APPDATA%\OpenTDU2\OpenTDU2`), so old and new binaries read and write the same `profile.sav` / `options.ini`. Proposed, not done (`tdu_instance.cpp` is the shared spine, serialise it): a startup line carrying the git commit, so "am I on an old build?" is never a guess again. **Follow-up (same session): the hub's branch is why it looks old.** `E:\openTDU2` is on `win` @ `9990ce6`, which is **26 commits behind `origin/cline` and 36 behind `origin/glm`** (`git rev-list --count HEAD..origin/cline` / `..origin/glm`), and neither branch is an ancestor of its HEAD (`git merge-base --is-ancestor` says NO; the reflog has no glm merge since `ef2602c`, 10-30 03:18). Its source therefore has no `fuel.cpp` / `fuel_menu.cpp` / `mod_props.cpp` (cline) and no `gm_cine.cpp` / `gm_shop.cpp` / `seated_avatar.cpp` / `ik_offsets.cpp` (glm), while the older cline work (`gm_asset_viewer.cpp`, `asset_labels.cpp`) *is* there — so the owner's hub build is consistent with its own source, it simply never received the agents' newer commits. `git fetch` only updates the `origin/*` refs and never merges, and the owner's own `update-and-rebuild.bat` merges only `origin/<checked-out branch>` (= `origin/win`, already equal to HEAD), so it correctly reports "nothing to rebuild" and the game never changes; it also *skips the rebuild* when a merge was done by hand first (it compares HEAD before and after its own merge), so a manual merge needs `build.ps1` directly. The C:→E: move is not the cause (the history is intact and `build-win` is configured for `E:/OpenTDU2`), but it left the orphaned `build\` and the three `*- Copy.bat` launchers described above. New tool: **`tools_probe/exe_probe.py <exe> [<exe> ...]`** — the feature strings an exe contains plus a differences table, which is how this was pinned down: the hub's `build-win\bin\opentdu2-win.exe` has `TESTCAR`, `IKOffsetDB` and `-logcarlights` but not `SHOPSCAR`, `SeatedAvatar`, `-storystep`, `-fuel` or `-rtao`; glm's `build-glm\bin\opentdu2-glm.exe` has all of them; the hub's stale `build\bin\opentdu2.exe` (30/10 04:22) lacks `-logcarlights` too.
- Newest (2026-10-30, `cline`, twenty-second): **one car asset measured on PC, Xbox 360 and PS3 — the polygon count is identical, the packing is not.** The owner asked whether one car "with everything shader, interior etc. all loaded" could be compared against the 360 and PS3 versions for size / poly-count differences. It can, and it is done for the Ferrari California: `tools_probe/xplat_car_compare.py` (new, `--selftest` needs no game data) extracts the same bank from the PC data, the 360 prototype bigfiles (LZX-decoded with the recomp's `xzxd.exe`) and the PS3 bigfiles, then prints the asset list, the geometry and the textures side by side. Measured, body / interior: uncompressed bank **2 713 600 / 2 617 344 B (PC), 2 306 048 / 2 293 760 B (360), 2 002 944 / 2 144 256 B (PS3)**; primitives 126 / 66, 125 / 66, 131 / 66; vertices 30 589 / 32 155, 30 532 / 32 155, 32 363 / 32 155; **triangles 32 255 / 38 242, 32 207 / 38 242, 32 207 / 38 242** — i.e. the same car, with the *index buffers* packed differently (the PC joins strips with 41 306 degenerate indices, the 360 breaks them with 9 437 `0xFFFF` restarts, the PS3 uses triangle lists) and one genuine difference: `HANIM_S`, the convertible roof, is 14 prims / 6 957 verts on the PC and the 360 but **20 prims / 8 788 verts on the PS3** (vertex splitting for the RSX's register budget). The textures are the same picture in different sizes: a 128x128 `.2db` is 21 920 B (PC), 69 632 B (360, its own format enum + tiling), 21 968 B (PS3 = the PC's +48). **New format work: the PS3 payload container** — `[plain BANK metadata][a 'HDR!' chunk header: headerSize, chunkSize 0x10000, field 0x80, the per-chunk stored sizes, a CRC][64 KB chunks][256-byte padding with an ASCII watermark]`, so the PS3's asset list, per-asset sizes and (because the GEOA lands in the first, verbatim chunk) the prim/vertex/index/triangle counts are readable today; the chunk contents are *not* LZX (`xzxd` fails, `ret=11`) and need the decompressor out of `EBOOT.ELF`. In-engine rendering of a 360/PS3 asset is still the 360 side of GLM's asset-studio Phase 3. Full numbers, evidence, the traps and the run commands: **`docs/re/xplat_car_compare.md`**; verified `--selftest` passed and `--car california --interior` printed all three columns.
- Newest (2026-10-30, `cline`, twenty-first): **the bridge — a contact is now resolved through the face the car entered.**
 The owner: *"i managed to eventually clip through a bridge and back on road"*. Three defects in the driving collision, measured rather than guessed: (1) `sphereContact` built a triangle's normal from the closest-point offset, so a sphere centre *below* an up-facing road triangle got a **downward** normal — the position correction pushed the car further down through the deck until the contact was lost and the wheel rays (which only look down) could no longer see it; the sphere's previous centre now picks the side and a crossing in one tick is pushed back out through the **entered** face (the same test now covers a thin box crossed in one tick, a 0.05 m railing plate); (2) the depenetration cap (0.05 m x 2 iterations) was smaller than one landing tick (~0.2 m at 90 Hz) — now `min(depth x 0.8, 0.25 m)` x 4; (3) a **`.pmi` instance scale never reached the boxes** (the triangles and capsule ends are transformed with it, the box half extents were not): **~90 % of the placed library instances are scaled, up to 2.01x** [DATA, per-sector log line], so the collision body of a scaled railing / planter / bridge part was smaller than the model drawn with the same matrix. Plus swept suspension rays (cast from where the wheel was at the start of the previous tick). Verified: 8 new `VEHTEST bridge` self-test checks all PASS (`-vehtest`), the two contact checks FAIL on the pre-fix build (`normal (0,-1,0) / depth 0.100`), and the same 20 s drive on the island is unchanged (max delta 0.69 m, same end state). `docs/re/havok_collision_spec.md` §8, session entry at the top.- Newest (2026-10-31, `glm`, session 55): **the ImGui panels are DPI aware** — the owner asked for bigger
  text in the cheats menu, the advanced settings and the asset tool ("text is very small and hard to read").
  Root cause: every panel used the embedded 13 px bitmap font and an unscaled style, and this machine's
  Windows content scale is 225 %. The ImGui service now follows the window's display scale
  (SDL_GetWindowDisplayScale): a system TTF (Segoe UI on Windows) loads once at a 13 px base and
  `style.FontScaleMain` rasterizes it at any size (ImGui 1.92 dynamic fonts), `ScaleAllSizes` scales the
  style, and the panels' hardcoded sizes go through new `DpiScaled`/`DpiScale` helpers. `-imguiscale <f>`
  forces a scale (1 = the classic look, unchanged), `-imguifont <path>` picks the font, and the cheats
  panel has a live **UI scale** slider. Verified headless: F4 / F2 / asset viewer / world viewer / coords
  shots at 2.25x plus the forced-1x regression (docs/images/ui-dpi-*.png). Full entry below.
- Newest (2026-10-30, `glm`, session 54): **the story modes are playable - the Solar Crown cutscenes (CINE),
  the used car dealer (SHOP) and its test drive (TESTCAR) with the avatars seated in the cars** - upstream 335759a
  stages 1 + 2 ported onto the merged story spine (stage 3 'Driving Tess' / MISSION deliberately not). The chain
  `-storystep 5` runs end to end headless: CINE kind 2 (C_SOLAR_CROWN_E) -> kind 3 (C_SOLAR_CROWN_I, interior bank
  ich_ib) -> story step 7 -> SHOP (Todd Bishop's dealer, ESTOL_B_1275I type 0x11, DBCarShop[25], 3 cars painted at
  their PosCams, SHOPSCAR live) -> `-gm TESTCAR` drives the Mustang with the driver AND the passenger (SeatedAvatar)
  and the 120 s countdown. Kind 1 (Meeting Tess) keeps the owner-tuned cin_viewer frame (verified 1:1); the avatar
  wears the valet livery after the avatar loader's W_/M_ sex-prefix fix. Full entry below.
- Newest (2026-10-30, `glm`, session 53): **plastic surgery investigated for the owner — it is all there**.
  Three clinics (`ESUR_A` Ibiza/Hawai, `ESUR_CA` casino), the `isur_a` interior walks today (surgeon
  selectable type 27, C_HEAD/C_360 preview cinematics), retail's flow is fully in readable data
  (`DBSurgeryCost` 46 prices, `SHOPSAVATAR` surgeon panel = GAME_MODE_SURGERY 33, the Miami phone calls,
  the bandage DBClothe items 313/314 + save fields + gate messages), and the face-morph pipeline it would
  drive (FaceParamArray → face bones through FaceFx) is already live in the port. Implementation brief:
  `docs/re/surgery_spec.md`. New tool: `tools_probe/db_probe.py` (dump any DB table / search localization).
- Newest (2026-10-30, `cline`, twentieth): **CamilleKMI's "MAJOR Update" (`335759a`) read, measured and staged — not merged.** The owner asked to merge it and named the used car dealer UI, Driving Tess and the Solar Crown cutscene. The two trees are not an ancestor/descendant pair any more: both descend from `3f67e58`, this one is **269 commits / 120 308 lines** ahead, upstream's one commit adds **16 811 lines** — and both trees built the *same* features independently (night lights, windows, indicators). A trial merge gives **31 unmerged files / 103 conflict hunks**, and even resolved it would not compile: the new modes call ~25 spine methods this tree does not have. **The decision: port feature by feature, not merge** — the features are *new files*, which `git checkout upstream/main -- <path>` brings in exactly. Landed: **Stage 0, the story spine** (`1b187bc`, branch `upstream-335759a-port`) — `GM_Cine` / `GM_Mission` / `GM_Shop` / `GM_FreeDrive` in `eGameMode`, `GetCineRequest()`, `GetStoryAvatarConfig()`, the money transfer (`ProfileAddMoney` / `ProfileStartMoneyTransfer` / `ProfileTickMoney`, `PendingMoney`, `MoneyTransferTimer`, `MoneyEarned[28]`), the previous-game-mode field (DAT_00fd5080 + 0x12c) and `-storystep n`; build exit 0, no behaviour change (the modes are not registered yet). Full evidence, the conflict table, the per-mode spine gap and the staged plan: **`docs/re/upstream_335759a.md`**.
- Newest (2026-10-30, `cline`, nineteenth): **the garage upgrades bite — the tuning levels reach the physics** (the owner's pick from "garage with vehicle upgrades, and the stickers … or improve traffic logic and hopefully fix police logic"). `ApplyTuningToSetup` (`physics/vehicle_data.cpp`) modulates a car's setup from its `OwnedCarTuning` levels [INF: retail's tuning tables are encrypted] — accel = engine torque (+8 %/level), braking = the tyres' braking friction (+10 %), speed = the engine's top-end torque (+10 % above 70 % of the rev range) with the matching RPM band; `GMFreeRide::setupVehicle` applies them at spawn and `applyTuning()` when the player gets back in the car (from a copy of the base setup, so a re-apply cannot compound). Measured on the California, level 3 each: 0-100 4.52 → 3.27 s, top 309.7 → 337.6 km/h, 100-0 34.4 → 31.9 m. Plus **the police awareness decays again** (`ChaseMode::addViolation` enters the previously unreachable `Noticed` state, so the decay branch runs: `attention lost`). **Stickers and broad traffic benched** (see the session entry).
- Newest (2026-10-30, `glm`, session 52): **the map after Cline's icons — the LOD the port never drove, the
  credits easter egg that reads as a hang, and the fuel icons that never loaded in FREERIDE** — the owner tested
  session 18's map icons and reported "some icons look a little too big and eventually the game is unresponsive".
  Three real bugs behind it: (1) the retail spot icons carry a per-zoom LOD (`zoom_default` / `zoom_lod` /
  `dezoom_lod` / `dezoom_lod2` frame labels on every point clone) that the port never re-applied after
  `buildSpots` — at the whole-island zoom every icon sat on its full-size ~64 px art; `applySpotLod` now follows
  the zoom step (and the fuel icon tracks the LOD sizes 24/44/54/64). (2) The zoom-out-past-level-0 credits roll
  left the map dimmed and textless for its ~12 min run and Back-out used to close the map onto a black stage —
  the root timeline has a `credits_back` label, which now restores the map (and `buildSpots` re-clones the icons;
  the natural trigger logs). (3) `ModProps` (so the fuel-station list with it) loaded in `enter()` behind
  `getEdenRenderer() != nullptr`, which is still null there — the props never loaded in FREERIDE at all;
  `ensureModPropsLoaded` retries in tick. (4) The owner's actual freeze, corrected mid-session: **panning the
  map at the zoomed levels** re-keyed the world streamer on every sector-boundary crossing and loaded the whole
  (2r+1)² square synchronously — radius caps at 4 at the two far levels, up to 81 bank loads in one frame
  (reproduced with a new `move=` stick injection: fps 120 → 19, update spikes 141/184/223 ms); the map now
  streams one sector per frame (`loadAround`/`unloadFar` gained a budget, drive unchanged at 0 = unlimited) —
  update max during a pan sweep is ~20 ms. Verified headless with screenshots at all four zoom levels, the
  credits round trip (trigger → Back → map restored with icons) and 0-stall soaks. Full entry below.
- Newest (2026-10-30, `glm`, session 51): **the photo shop trio — the interior near plane "all round", the lit
  shop NPCs, and the world outside the shop windows** — every interior entry cinematic now takes the walk near
  plane (0.05 m; the session-48 rule needed a car, the shops have none), an interior without a weather message
  falls back to `weat:Avatar` for the avatar light (the near-black seller NPC: shadow 1.0 → 0.20, the shop's own
  4 spot lights gather; `-logavatarlight` added), and the shop rooms are placed at their real spots.bin spot with
  the island streamed around them (`EPHOT_B_3337I` at (21288, 11, 24320); the neighbour building renders through
  the window openings; `-nointeriorworld` is the A/B). New `-interior <spot>` test hook (the `-housegarage`
  pattern). Full entry below.
- Newest (2026-10-30, `glm`, session 50): **GPU timing, the first P2 win, and the first RT consumer** —
  `[perf] gpu:` lines (`-loggputimes`) showed the port **CPU-recording-bound ~15x** (~2.9 ms GPU in a 42-46 ms
  FREERIDE frame); P2 step 1 (cached descriptor sets, `-nodescriptorcache`) took ~10 % off the CPU frame; and
  `-rtworld -rtao <samples>` traces ray-traced ambient occlusion against the world TLAS — ~0.12 ms per ray per
  pixel, the GPU stops being idle at ~220 rays/pixel (numbers in `docs/re/ray_tracing.md` §6 and PERF_PLAN).
- Newest (2026-10-30, `glm`, session 49, **WIP paused**): the dashboard sat-nav — the map render target renders,
  binds and logs end to end, but the GPS quad is **not visible yet**: the TD_GPS vertex shader positions the quad
  from `UVOffset` and the right values are still unknown (TDU1's exact ones are computable, formula in the entry).
  Default **off** (`-gps 1` to test). Full state + pickup recipe below; the menu/shop audio plan (researched, not
  started) is filed there too.
- Newest (2026-10-30, `cline`, seventeenth): **the HUD fuel gauge** (the owner asked for it). Neither retail side has one — the car's dashboard carries only
  `ND_SPD` / `ND_RPM` (`-dumpcar`) and the HUD movie has no fuel element (no `/:` global) — so `FreeRideHud::drawFuel` draws it natively in the HUD's own style beside
  the zone badge: the label (`LocStr(5821, "Fuel")`), the fill, the litres and the km left, green / amber under a quarter / red under a tenth; `-nofuelhud` hides it.
  Verified by pixel: `-fuel 20` (26 %) → `(90, 200, 90)`, `-fuel 2` (2.6 %) → `(235, 110, 30)`.
- Newest (2026-10-30, `cline`, sixteenth): **the fuel is wired** (the owner's "a and b"). (a) `source/game/freeride/fuel.{h,cpp}` + `fuel_menu.{h,cpp}`: the tank
  (`_gas_tank_size`, the `_gas_cons_*` l/100 km law), the burn on the odometer delta, empty = the throttle cut + "Breakdown", and a pump screen whose every string is
  the **game's own** (`LocStr` 1635..1645: Gas station / Shell basic / Shell v-power / Fill / Bill / Breakdown / SHELL pays the bill), opened in **your house
  garage** (retail's only pump model is the `gph_l2d` garage one) and at a station prop; `-fuel` / `-logfuel` / `-nofuel`, saved in `profile.sav`. (b) `mod_props`
  gained the `fuel` column: stopped within 35 m of our FUEL1 the HUD offers `Fill` and validate opens the screen, so future locations are one line of `props.txt`
  each. Verified: tank 78 L (0.1/13.1/0.0 l/100 km), litres falling while driving (0.1 → 11.7 l/100 km), `tank empty after 95 m - Breakdown`, and
  `filled 73.0 L Shell basic for $108.77` at the station. `docs/re/fuel_gas_station_spec.md` 6, `docs/re/modding.md` 3.
- Newest (2026-10-30, `cline`, fifteenth): **our own petrol station works, and the asset index found the retail pumps**. The owner asked about the car washes and
  suggested the asset tool — the index (`-gm ASSETVIEWER -buildindex`, 9,452 banks / 153,690 assets) searches *inside* the banks and Eden is French, so:
  **`gaspump_d.2db` exists in exactly one bank of the game — a player-house garage** (`interior/gph_l2d.bnk`, `Euro/spots/playhous/…`), which is where the
  `Gas station` / `Shell basic` / `Shell v-power` / `Fill` / `Bill` / `Breakdown` strings and the `gasStation` handler belong (you refuel at home); the racing
  schools' `pompe_gas` / `old_gas` pumps are decoration and the car washes have none. **Route A is implemented** (`source/game/spots/mod_props.*`): OBJ + MTL +
  PNG in the mods overlay, placed by `<mods>/props.txt`, drawn with a stock shader family (default `TD_Gouraud`), one draw per `usemtl`, textures via
  `image_file` + `TextureCache::createFromRGBA` — verified with a station of our own (132 triangles, 4 PNGs, top-down A/B **28,650 changed pixels** vs
  `-nomodprops`). Also fixed on the way: `GSFile::listFolder` **replaced** a modded folder's listing instead of merging it (the index walk saw 1 bank instead
  of 9,452). `docs/re/modding.md` §3, `docs/re/fuel_gas_station_spec.md` §4.1.
- Newest (2026-10-30, `cline`, fourteenth): **a building we place ourselves**. The owner's *"what about we make a
  petrol station in blender … it's our code, we should be able to do that?"* — the **placement** half needs no
  engine change: `tools/mod/add_spot.py` appends one 0x110-byte record to a **copy** of the island's `spots.bin` in
  the mods overlay and drops a bank beside it, and the port picks both up (`Mod overlay: … spots.bin` /
  `… egas_a.bnk`, `MapScreen: … of 80 in spots.bin`, `SpotExteriors: EGAS_A at (12665, 16698): 2 models, 1
  placements`; evidence `docs/images/mod-station-placed-s14.webp` — two copies of the tuning-shop bank 60 m apart).
  `docs/re/modding.md` has the overlay, the two routes for our **own** mesh (a port-native OBJ/glTF prop, or the
  Eden `.3dd/.3dg/.2dm/.2db` + a BNK packer + an exporter) and the rules (never write the game folder, never ship
  game data, our own brand and art).
- Newest (2026-10-30, `cline`, thirteenth): **the community's physics thread read back — and the gas station it
  names is in the data**. Their "Hardcore mode" (no traction control + full steering lock at any speed) is the
  port's own open item: the whole aid machinery exists but is frozen at SECURE (`-aids` is load-time only), and the
  lead `/:DrivingAssistance` is readable on PC. Their *"Gas Station reimplementation … parts of it exist but are
  disabled"* is real cut content: the complete UI text is in the retail `db_us.cpr`, **every car carries its tank**
  (`_gas_tank_size` 12.4..133 L, the California 78 L — now read by the port) and the `gasStation` handler is
  readable — but no station was ever placed, so it needs no exe patch. `docs/re/physics_community_notes.md`,
  `docs/re/fuel_gas_station_spec.md`.
- Newest (2026-10-30, `cline`, twelfth): **the workspace reset tested — and its one real casualty found and fixed**.
  After the GitHub-clone swap (`C:\OpenTDU2-cline2` → `C:\OpenTDU2-cline`) the tree **could not build at all**: CMake
  stores absolute paths, so `build-cline\CMakeCache.txt` still named the staged folder and both the build and
  `-Reconfigure` refused (`... is different than the directory ... where CMakeCache.txt was created`). `build.ps1` now
  checks `CMAKE_HOME_DIRECTORY` against its own folder and starts the build dir over, so a renamed tree is a
  one-command recovery. Verified after that: clean 713-target build, a 2D map run and a 3D `-night -carlights 1` run
  (0 stalls, screenshots written), `launch\resolve_exe.bat` resolves `opentdu2-cline.exe`. Session entry below.
- Newest (2026-10-30, `glm`, session 48): **synced with the hub** — the near plane, the C_FIRST_OUTRO stream centre,
  the camera-path lookahead and the rest of the hub's 19 commits merged over the real-estate session; the `-home`
  block re-seated before the hub's locate()-then-streamCentre order; verified headless (`merge-night-s48.webp`).
  Entries below.
- Newest (2026-10-30, `glm`, session 46): **real estate + houses wired up** — the `ERE_*` realtor sells the 76
  `DBHouse` houses (buy / sell, `houses_owned` + `home_house` in the save), your houses are walkable (`lph_*`
  living room with the myhouse furniture, `gph_*` garage with your cars on its slots, the map's home icon,
  `-home` spawns at the home house), and the two other data-backed shops got menus: **tuning** (three levels
  per car) and **paint / sticker** (the car's colour catalogue, live repaint): `docs/re/houses_spec.md`.
- Newest (2026-10-30, `cline`, eleventh): **the car lights, the windows and the night dashboard** — the owner's
  *"I want Headlights, Indicators, Windows also satnav (should show incar view in some cars) ... when press left
  then down they don't seem to animate ... headlights not sure if work so you may need to spawn night time also
  dashboards don't seem to have lighting"*. Found by running the game headless with a new `-logcarlights`
  diagnostic (`-dumpcar` dumps the node tree, the new flag dumps the lamp / dashboard materials with their shader
  constants and parameters): the California's four lamps are the materials **`LGT_FL` / `LGT_FR` / `LGT_RL` /
  `LGT_RR`** (`TD_CarPaint_NM`, the lit art `california_lgt_on` bound to their own `gTexLightTexSampler`), the
  dashboard is the interior **`HUD` / `HUD_01`** material (`TD_DashBoard`, which reads `IlumParam` / `IlumColor` /
  **`HeadlightsIntensity`**). So: **windows now animate** (`CinematicCar::updateWindows` blends WND_x1 from its
  rest onto the WND_x2 rest over 1.2 s instead of snapping), the **indicators are driven** (the in-car menu's
  left / right entries blink their own side's pair, cadence 1 s), the **headlights** come on at night (the
  weather preset hour, `< 6 or >= 20` — the rule the traffic already uses), the **brake / reverse lights** follow
  the pedals, and the **dashboard backlight** is raised with the headlights (its `HEADLIGHTSINTENSITY` ships 0,
  which zeroes the whole backlight term — that is why the dials were dark). `-night` spawns the darkest
  `game_weathers` preset, `-carlights 0|1` forces the state, `-logcarlights` logs it once a second. Verified
  headlessly: the window factor steps 1.00 → 0.64 → 0.00, and the brake lamps brighten the car's rear by up to
  +174 per pixel (1623 pixels > 20) while the dashboard backlight changes 664 pixels in the cockpit's instrument
  cluster. **The in-car satnav (the interior `GPS` node, `TD_GPS`, one `satellitemap` texture) is NOT done** — it
  needs a map render target bound to that quad, see the session entry. Testing in person the owner found the
  **brake lamps glow white instead of red** and the emission low: every lever was then pixel-measured — only
  **`MATPARAM.x`** moves a lamp (a *level*), while `.y/.z/.w`, the material `Diffuse`/`Ambient` and `GlossFct` do
  **not** tint it, so the lit colour lives in the **`california_lgt_on` texture** (a red rear lamp needs a tinted
  texture, or the encrypted retail driver). **`-lampparam x,y,z,w`** is the knob kept for that.
- Newest (2026-10-30, `cline`, tenth): **the California's wheels froze in C_FIRST_OUTRO** — the owner's *"after meeting
  tess to drive the california for 1st time after the party the camera pans the car ... but as you rev the car the
  speedometer needle doesn't move and wheels don't spin in that scene"*: `updateWheels()` returned early for
  `eState::Outro` and the physics is not stepped while the cinematic owns the car, so the rims / tyres kept their last
  pose while the .sce drove the car 350 m out of the garage. The cinematic's own track velocity (`FirstOutro::getCarPose`)
  is now integrated into a rolling angle (`outroWheelSpin`) and the wheels roll from it (`source/game/freeride/gm_freeride.cpp`).
  Session entry at the top; the needle half of the report and the `glm`-only fixes are filed there too.
- Newest (2026-10-30, `cline`, ninth): **the pause menu's dead entries** — the owner's *"some pause menu stuff is
  broken in some menu's"*: in the movie-driven pause menu `setEntryEnabled()` cleared **every** entry's lock
  variable (`/:LockPlayers`, `/:LockProfiles`, `/:LockEditor`, `/:LockSnapshot`, `/:LockGameProgress` = false)
  while its own comment said the entries whose features do not exist keep their retail lock — so **Players** was
  selectable and opened the online **TDU2 Friends** screen (empty: there is no online here) and **Editor** (`gme`)
  a "Link to player" page stuck on **Loading…**, both dead ends. The lock variables now follow each entry's own
  state, so those three render with a **padlock**, the movie plays its own `sndp:wrong` on validate and **nothing
  opens**; Game Progress is unaffected (its `bEnabled` was a stale `false` from before session 24 built the pages
  — it is `true` now and its lock variable is still `false`). Session entry at the top; the rest of the pause
  screen (the **HAWAII** badges on Ibiza, the casino / garage clock at **00:00**, the missing objective header,
  the unformatted money) is filed in `docs/SURVEY.md` item 10.
- Newest (2026-10-30, `cline`, eighth): **the frame loop watchdog, and the device lost report** — the owner's
  *"I drive for a while and the game goes unresponsive"*: the hub's `opentdu2.log` (2026-10-30 00:43) **was**
  left behind and holds **639 identical `vkQueueSubmit` `VK_ERROR_DEVICE_LOST` (-4)** lines (the driver reset
  the GPU — a GPU hang / timeout, and the `[perf]` lines right before it show a 1036 ms max draw at 3840x2160
  vsync), then the log simply stops. `source/core/watchdog.*` now beats through the frame loop ("loop",
  "update", "draw", "present") and reports a stall with the phase that stopped, the last frame, the duration
  and the worst frame gap of the run, into the game log **and** `opentdu2-watchdog.log` (which a thread hung
  inside a log call cannot block — `Logger::WriteTagged`); the device loss is reported **once** instead of once
  per frame, the frame fence wait is sliced so it can no longer freeze silently, and the game leaves cleanly on
  a lost device (`RenderDevice::hasDeviceLost`). Session entry at the top.
- Newest (2026-10-30, `cline`): **the car radio's stations** — every station has its own
  song list (the owner's *"the song stayed the same when I skipped station"* is fixed: the
  retail 65 are dealt round robin over the three) and **every subfolder of
  `<Music>/OpenTDU2` is a custom station**: the folder name is the station name, its
  mp3 / ogg / opus / flac / wav / bnk files are its songs (the format is sniffed from the
  content — the owner's ".mp3" was an **Ogg Opus**) and its `logo.png` / `logo.dds` is the
  art, drawn in the movie's own logo box (`source/audio/radio_player.*`,
  `source/audio/audio_decoders.*`, `source/render/image_file.*`; new deps dr_libs / ogg /
  opus / opusfile). Session entry at the top.
- Newest (2026-10-29, `cline`): **the cut-content scan** — every bank of both Xbox 360 prototypes compared
  against the retail PC data on the parts that are platform invariant (`.3dd` / `.2dm` byte sizes, texture
  mip 0 pixels, the asset list), unattended and resumable: `tools/x360/content_scan.py`,
  `tools/x360/eden_bank.py`, findings in `docs/re/cut_content_scan.md` (7,937 banks flagged by **both**
  prototypes; the car interiors, the islands, the frontend, `fx/vfx_car`, the R8→R8 V10 and V6-Premium→GT
  renames, four prototype-only banks incl. the Audi Inuit preorder clothes). Before that (`cline`):
  **the developer cheats** — a panel (**F4**, or the retail
  dev-feature pad code: a D-pad circle three times with X mashed) with the profile's **money** and
  the casino chips, a **teleport** (the island's 79 spots, raw coordinates, or **F5 on the map** to
  the point the cursor is over), the weather / story step, and the retail dev menu's **delock rows**
  (all cars, houses, roads, cars on stock, achievements, nearest locked road): `docs/re/cheats.md`,
  `launch/15_Freeride_Cheats.bat`. Before that (`cline`): **the asset viewer answers the feedback** — **search every bank at once**
  (`-buildindex`: 153,690 assets + 10,708 entities in 33 s, then "ferrari" / "slot" / "cam_entree" finds the bank),
  an **Entities tab** showing every field of a bank's `entities.xmb` objects, a **view from the bank's own entity
  camera** (an interior is no longer a box seen from outside), a **free camera**, a **collision wireframe** overlay,
  a **type filter**, hidden language banks and a layout that fits 1280x720: `docs/re/asset_viewer.md`, session entry
  at the top.
- Newest (2026-10-30, `glm`, session 47): **the "random unresponsiveness" fixed** — the chase-arrest cop despawn
  released the shared texture owner (the player's California samples destroyed image views → device lost; the
  validation layer names it): per-instance CinematicCar texture owners + the despawn drain; the radio's next-track
  decode moved to a background preload; SpotExteriors' streaming drop drains; `-hitchms` stall capture. Merged to
  `win` here. Session entry at the bottom of the newest block.
- Newest (2026-10-29, `win`, session 45): **walk into the shops** — A at any dealership / clothes shop /
  hairdresser ... loads its `interior/i<name>.bnk` and you walk it in first person; the seller opens the dealer or
  clothes menu, B walks you back to the car (`docs/re/spot_interiors_spec.md`). Before that (`cline`):
  **the developer cheats** — a panel (**F4**, or the retail dev-feature pad code: a D-pad circle three times with
  X mashed) with the profile's **money** and the casino chips, a **teleport** (the island's 79 spots, raw
  coordinates, or **F5 on the map** to the point the cursor is over), the weather / story step, and the retail dev
  menu's **delock rows** (all cars, houses, roads, cars on stock, achievements, nearest locked road):
  `docs/re/cheats.md`, `launch/15_Freeride_Cheats.bat`; and **the asset viewer answers the feedback** — **search
  every bank at once** (`-buildindex`: 153,690 assets + 10,708 entities in 33 s), an **Entities tab**, a **view
  from the bank's own entity camera**, a **free camera**, a **collision wireframe** overlay, a **type filter**:
  `docs/re/asset_viewer.md`.
- Newest (2026-10-29, `glm`, session 45): **the two owner reports diagnosed** — the car-view "dirty
  glass" and the meeting_tess "camera culling" are the same root cause (the glass panes render
  near-opaque black), plus a second real bug found on the way: the interior **GPS dashboard quad
  draws unprojected** (TDU1's `setGPSTexture` writes its runtime params, we don't). Full reuse
  inventory of CamilleKMI's **OpenTDU1** cloned to `C:\temp\opentdu1-ref`:
  `docs/re/opentdu1_reuse.md`. New A/B tooling: `-hidenode a;b` (car+interior subtree hide),
  `-noprops`, `-nosky`, `-noclouds`, `-drawfrom/-drawcount/-logdrawwindow` (main-pass draw bisect),
  `-logskin` (skinned palettes + GPS mesh locator). Part 2: **the shops were dark** — the spot
  exteriors keep their authored NIGHTLMAP (the dealer showroom authors 0.44 day + lamps on; the
  exterior weather blend (1,0) at midday switched the lamps off). Entries below.
- Newest (2026-10-29, `glm`, session 44b): **the world-spot UI layer** (shops / races / police chase / hints / countdown on the retail HUD.vmf elements) and **the tree now carries the launch/stop system** — the build is `build-glm\bin\opentdu2-glm.exe` (`build-glm.ps1` passes `-DOTDU_EXE_SUFFIX=-glm`; configure without it and the exe is `opentdu2.exe` again), `launch\resolve_exe.bat` finds this tree's own exe, and a run is stopped **only by this tree's path / its own exe name — never `Stop-Process opentdu2` by name** (WORKSPACES.md "Never stop opentdu2 by name"). The win + cline sessions 44 merged in (story, Meeting Tess cinematics, sat-nav voice + turn arrow, spot exteriors, pad support, photo camera; the casino floor + wallet, the asset viewer). Before that (`win`, session 44): **the sat-nav speaks** (retail GPS voice laws, `docs/re/gps_voice_spec.md`);
- Newest (2026-10-29, `cline`): **the offline casino** — the floor, the slot machine, roulette, video poker, the
  chip wallet, and the whole progression from the game's own data (145 objectives, the 4 `DBCasinoLevel` axes, the
  rewards / unlockables, the VIP / ACE ladder): `docs/re/casino_spec.md`, session entry at the top. Before that
  (`win`, session 44): **the sat-nav speaks** (retail GPS voice laws, `docs/re/gps_voice_spec.md`);  `glm` 42-43 and `cline` merged into `win` at `1c88b79`. Before that (`cline`): **the island buildings are in FREERIDE** (spot exteriors: dealerships, shops,
  houses), the pad zooms / navigates the map and the pause screen, and "Take photo" opens a native photo camera
  that writes PNGs — see the session entry at the top
- Upstream base: `3f67e58` — CamilleKMI/OpenTDU2 `main`, unshallowed (16 commits). Upstream has since published `335759a` ("MAJOR Update", 2026-10-29, 129 files / +16 811): read, measured and **deliberately not merged** — it is ported feature by feature, see `docs/re/upstream_335759a.md` and the session entry at the top.
- Game data: `C:\TDU2-Gamedata\TestDrive2` (14.37 GB, shared, read-only input)
## Session 2026-11-01 (`cline`, twenty-ninth): the sign families read correctly - per-family stamp orientation, distances only where retail draws them, and the stacked-plate UV rewrite that actually reaches the GPU

The owner checked session twenty-eighth's build on screen and the composed signs were broken: the
EIVISSA town sign read upside down, and they sent four x360-recomp photos as the reference (dir /
dist / tdirib faces, upright, distances only on the distance plates, the row pictograms). Measured
before changing anything (per-vertex uv<->position over every family model, `_twn_probe.py`):

- **The families do not share one UV convention.** `rs_dir*` and the town plates sample their atlas
  rect with u against the plate's reading direction (u low at model +x); `rs_dist*`/`rs_tdirib*`/
  `rs_hwdist*` run with it (u low at -x) - the blanket x-flip of sessions 59/60 (calibrated on
  rs_dir only) mirrored every dist/tdirib face on screen (the sector-1-2-7-5 `RS_Dist_D_act`
  "Centre/Platja/Puerto" sign read mirrored from its text side).
- **The town plates alone read their v bottom up** (v0 = the plate's bottom edge): stamped like the
  dir families, the text lands 180 degrees rotated.
- **Retail draws distance digits only on the distance plates** (photo: `rs_dist` "Centre 1 / Platja 1
  / Puerto 1"); the dir and TdirIb families leave the payload's distance bytes undrawn (photo: the
  `RS_TdirIb_C_act` "Sant Rafael / Hisenda Aldea Oest / Platja" sign has no digits) - the port drew
  phantom "14"s on every TdirIb row.
- The 20-char payload window was re-verified against the raw bytes of the photo-matching records:
  `[20-char zero-padded name incl. the leading pictogram byte][1 distance byte]` x3 (size 64 =
  1 + 3x21); Cline's decode was already right, and `0x4b`/`0x48` land inside the name window where
  the photos show the umbrella/anchor.

Fixes (`sign_text.h/cpp`, `eden_world.cpp`): `BuildSignLayout` takes the family (the model-name
prefix, `SignFamily`), sets `SignQuadLayout::FlipU/FlipV` per zone (dir/twn flip x; twn also flips
y) plus `SignLayout::bDistanceRows` (dist/hw/twn only), the zone gains `Bottom` and the face-cache
key hashes all of it; `compose` passes the flags to a vertical-flip-capable blit and gates the
distance stamp. The session-twenty-eighth stacked-plate rewrite (`RemapStackedPlateRows`) was
mutating `primitive.Vertices` while **the GPU draws `primitive.RawStreams`** - the parsed copy never
reaches the upload - so both stacked plates kept drawing the first row's cell ("Platja"/"Platja"
instead of Platja/Puerto). It now writes the new v into the raw float32 UV stream as well, and the
plate a vertex belongs to is picked by the member whose linear v(y) over its own [Bottom, Top]
matches the vertex's v (nearest-Top alone tears the adjoining bevel rows; the y spans overlap).

Verified on screen (scratch `-userdata`, `-autoplay` shots): the photo-2 `RS_Dist_D_act` sign reads
"Centre | Platja | Puerto" upright, each row its own text; the photo-1 `RS_TdirIb_C_act` sign reads
"Sant Rafael / Hisenda Aldea Oest / Platja" upright with no phantom digits and the yellow tip on
row 1 (rows 2/3 land on plain bars - the known limitation); the EIVISSA town sign reads upright
(the red border + diagonal remain - see Open). Regressions: `-storystep 5` plays out and advances
to step 6; 700-frame FREERIDE drive (see below).

**Open:** (1) the green header of dist/dirl renders its text twice from off-axis angles - the model
carries two coplanar same-winding half-quads each mapping the FULL rect, mirrored (the render
literally shows "Centre 11 ertneC"); retail's photo shows a single clean text spanning the whole
plate, so retail's material/shader must treat these UVs specially (the sign shader's sob bytecode
is the next stop). (2) The town sign's red border + diagonal: the diagonal comes from the model's
edge-wall triangles whose UVs streak across the whole atlas (rust chevron -> ZONA 30 rings -> the
town cell); the same geometry in retail must draw the same streak, but no x360 screenshot of a
town sign exists yet - the owner is checking their recomp; if retail shows the strike too, this is
faithful and stays. (3) Row pictograms (0x48-0x4b) are still dropped, so "Platja" lacks the
umbrella retail draws.

Note: the session's early test captures ran WITHOUT `-userdata` and hit the real `%APPDATA%`
profile (teleports, odometer/fuel drift only); every later run uses `C:	emp\otdu2-cline-userdata`
per CLAUDE.md.

## Session 2026-11-01 (`cline`, twenty-eighth): every Ibiza road-sign family is composed, and stacked plates get their own atlas cell

The owner asked for "all signs in the world" to show their text and sent four
Xbox 360 sign photos (white plates with a yellow arrow tip, a green/white
3-row panel, a green-headed town sign). **The photos did not reach the agent
as attachments** - only their text did; the agent can, however, read `.ppm`/
`.png` files from disk.

**Measured before changing anything:** of the 2247 Ibiza type-4 sign records,
only the ~684 whose model was named `rs_dir*` were composed; `rs_tdirib*` (919),
`rs_dist*` (367), `rs_twnin/out` (230) and `rs_hwdist*` (31) were left alone by
the `rfind("rs_dir",0)` filter, so they drew the blank plate art.

### What changed
- `source/world/eden_world.cpp` - any actor model with an `rs_maps` material
  slot is handed to the composer (no name filter); the payload still decides
  whether there is text. Layout is built before `EdenModel::load` and a
  `GeometrySetup` hook rewrites stacked plate UVs during the load.
- `source/world/sign_text.cpp` - `BuildSignLayout` knows all blank bars of
  `rs_maps` (green `v .017..065`, yellow-tip `v .089..132`, white
  `v .151..193`, white `v .213..260`, town red-border `v .585..635`, tall
  highway panel `v .303..644`). Yellow-tip rows keep their text left of the
  arrow. A tall panel is split into one sub-row per payload row.
  `RemapStackedPlateRows` gives each physical plate of `rs_tdirib_b/c` and
  `rs_dirl_d/e` its own blank bar (they share one atlas cell in the model).
  The face cache key now includes a layout hash.
- `source/render/eden/eden_model.*`, `source/render/3dg.h` - the optional
  `GeometrySetup` post-parse hook and a mutable geometry accessor.
- `docs/re/sign_text_spec.md` 4.6-4.8 - the corrected account (the session-60
  claim that the panels were baked art was wrong: their plate quads are in the
  bands the old code ignored).
- `tools_probe/sign_locate.py` - committed (session-60 probe).

### Verified
- `-tpin 23769.78,23584.83,184` at the `RS_TwnOut_A_act` "EIVISSA 8" sign:
  the world now reads **EIVISSA 8** on the red-border town plate (was blank);
  the earlier `_twn6` capture is the proof.
- `-atsign` at the known `RS_DirL_D_act`: `-signfacesdump` shows the green row
  and **three distinct white rows** (SANT VINCENT 7 / Platja / Balasant) after
  the remap; exit 0, no crash.
- `-logsigns` now reports `remapped N stacked plate row(s)` for
  `rs_tdirib_b/c` and `rs_dirl_d/e`, and plate zones for every family.

### Ruled out (with the test)
- The TdirIb/town plates are **not** baked art: their C++-parsed quads are
  `u .009..0.371 v .089..0.132` (and `u .557..0.837 v .585..0.635` for the
  town plate) - blank atlas bars the old band list never matched.
- The TwnOut plate is **not** u-normal: its vertices put model `-x` at the
  bar's high u, so the same x-flipped stamp as `rs_dir` reads correctly.

### Not done / next
- A yellow-tip family's 2nd/3rd stacked row lands on a plain bar (only the
  first keeps the arrow tip); per-plate dedicated bar art would fix that.
- `rs_hwdist*` reuses one tall quad; check on a road sign whether its 3
  sub-rows land where retail put them (only 31 signs; needs a good pose).
- The photos should be re-attached, or dropped into the tree as `_*.png`, to
  compare against the retail faces.

## Session 2026-10-31 (Cline, `cline`, twenty-sixth): custom animation, a held nozzle, and lip sync for custom dialog — from text (`-say`) and from **audio** (`-saywav`, the microphone path)

The owner: *"could we make custom animations? there's petrol stations I found and would be cool if we can
have our avatars holding the nozzle to petrol tank and maybe a lip sync method could be added for custom
dialog? if these are to much let me know?"* — plus a third ask about *"adding location names to their
correct props"* that they thought needed vision.

Answer: **all three are possible with what is already in the tree**, nothing needs the encrypted edAnim
library, and the lip sync is no longer a plan. `docs/re/custom_animations.md` is the spec: the capability
inventory, the `.anm` writer route, the nozzle plan (step by step with the bone names), what the label
file can and cannot say, and the ordered next steps.

### What changed
- `source/game/avatar/speech_synth.{h,cpp}` (new) — `SpeechAnimation::Build( text, FaceFxAnim & )`:
  English grapheme -> phoneme -> the actor's own FaceFX targets (`open W ShCh PBM FV wide tBack tRoof
  tTeeth` + `Blink`), digraphs first, fixed durations, per unit attack/release, sampled every 5 ms into
  `FxAnimCurve`s with linear interpolation. It is the *same object* a retail `.fxe` is, so
  `FaceFxLibrary::Evaluate` drives the 42 facial bones through the retail graph.
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` — `-say "<text>"` builds it once at load and
  `applyFaceFx` evaluates it on the **scene time (looped)** instead of the `.sce` Morph track, for every
  avatar of the scene (PNJ and the player), with the look-at registers still applied. `-lognodes` dumps
  both face graphs' node names (the accuracy check for a target name); `-logfacefx` now prints a
  `FaceFX say:` line per avatar.
- `docs/re/custom_animations.md` (new) — the spec above.
- `source/game/avatar/speech_audio.{h,cpp}` (new) — `SpeechAudio`: RIFF WAVE (PCM 8/16, mono/stereo,
  any rate) -> 8 kHz mono, then per-frame RMS in dB, zero crossing rate and a first-difference energy
  ratio -> `open / wide / W / ShCh / FV`. `-saywav "<file.wav>"` in the same cinematic path as
  `-say`.
- `source/game/avatar/facefx.{h,cpp}` — `FaceFxRegister` and an `Evaluate` overload that writes live
  node registers after the curves (operation 2 = replace, the look-at's own mechanism), so a driver
  can move a face with five floats a frame and no animation asset at all.

### Verified
- `build-cline.ps1` exit 0 (87 targets, `opentdu2-cline.exe` relinked).
- `-gm CINVIEWER -cinbank islands/ibiza/exterior/ecw_a.bnk -cin C_WASHING -cinspot 8129,26222
  -cinexterior ecw_a -say "hello there welcome to the car wash" -logfacefx` ->
  `IntroCinematic: -say ... -> 10 curves, 2.69 s` and then one `FaceFX say:` line per avatar per frame
  (`Avatar`, `Babe_01_radio`, `Babe_02_hose`, `Babe_03_sponge`), the left lower lip moving with the say
  time and back (Babe_01_radio `q.x` 0.0578 -> 0.1304 -> 0.0752 at t 1.44 / 1.61 / 1.68 s).
- `-lognodes` (`-gm AVATAR_CHOICE -skipintro -walkmode 1`): female 445 + male 448 nodes, and **all ten
  target names exist in both graphs** — the curves are not silently ignored.
- A screenshot of Tess mid-line with the mouth open: `docs/images/lipsync-say-s26.webp` (a 3x crop of
  the 1280x720 frame; the lip motion is retail scale, 1-3 mm, so the full frame shows two pixels).
- `-gm AVATAR_CHOICE -skipintro -walkmode 1 -tess -say ...` runs clean (600 beats, 0 stalls).

### Ruled out (with the test that ruled it out)
- **The `Transient buffer overflow` flood is not from this change.** The same CINVIEWER command *without*
  `-say` produces it too (`_ctl.err`: 104 993 lines in ten seconds), and so does dropping `-cinexterior`
  (84 748). It starts at `EdenWorld: ibiza: 7 x 6 areas` and the frame never completes afterwards, so
  **that scene hangs** in this build; the walk-mode scene has zero. Session 25's run of the same scene
  logged none, so it is a regression somewhere between that build and HEAD (or the driver state).
  Next step: `-noocean` / `-noprops` / `-noshadow` bisect on
  `build-cline\bin\opentdu2-cline.exe -gm CINVIEWER -cinbank islands/ibiza/exterior/ecw_a.bnk -cin
  C_WASHING -cinspot 8129,26222`, and the caller of `RenderDevice::allocateTransient`
  (`render/vulkan/render_device.cpp:1874` returns an *empty* allocation and the draw goes on).
- **The pixel diff of the two texts is inconclusive here**, not negative: the reachable C_TESS shots move
  the camera between frames (forehead diff 40 vs mouth 42), so the mouth cannot be isolated. The retail
  motion is millimetre/degree scale by design (`docs/re/facefx_spec.md` 3.5), so a visual check needs a
  static camera and a close-up.

### Not done / next
- **Audio**: `-say` is text driven (no wav alignment, English spelling only). The *audio* half now
  exists as `-saywav "<file.wav>"` (`game/avatar/speech_audio.*`): RMS + zero-crossing + high-band
  ratio -> `open / wide / W / ShCh / FV`, written as node **registers** through the new
  `FaceFxLibrary::Evaluate( ..., FaceFxRegister[], ... )` overload, so no animation asset is built
  and a microphone or a network mouth stream can drive it directly. Verified twice: a synthetic PCM
  clip with a known sequence (silence -> `open` 0.000, vowel -> 0.906-0.958, sibilant -> 0.604-0.783
  with `ShCh` 0.29-0.39, silence again 0.000), and a **retail MS-ADPCM wav** out of
  `meeting_tess.bnk` (decoded 22050 Hz 2ch -> 3.44 s, `open` 0.00 in the fade-in up to 0.84 in the
  loud part). I first wrote a PCM-only RIFF parser and reported the game's wavs as unreadable -
  **that was wrong**: they are MS ADPCM (`fmt 2`) and `SDL_LoadWAV_IO` decodes MS/IMA ADPCM, which is
  why the cutscene and sat-nav audio already works. `SpeechAudio::load` now uses
  `GSAudio::loadClipFile`, so there is no third decoder in the tree. (The owner confirmed the port's
  scene audio plays; that is what caught it.)
- **Voice chat** (`docs/re/custom_animations.md` §4.2): the mouth is done, the rest is not - the
  port has *no networking at all* (zero socket/ENet/Steam references) and *no capture device*
  (`gs_audio.cpp` opens `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK` only). Opus/ogg/opusfile are already
  built for the radio, and five bytes per frame of mouth shape is the bandwidth answer.

- **The nozzle** (§3 of the spec): no nozzle mesh exists in the data (`gaspump_d.2db`, `gph_l2d` only),
  there is no standing player avatar in FREERIDE (`FadeFreeride` is the pattern), and the attach is
  `world = avatarWorld * worldBones[ Hand_R ]` + `SolveTwoBoneIK` — 1-2 sessions, mesh work mostly.
- **Labels** (§5): labels are keyed by bank + asset, so a *placement* name goes in the note field, and
  most world props are records inside a sector `.3dd`, not bank assets. The vision loop itself works
  (render a model, read the PNG, write the label, `-labellint`).

## Session 2026-10-31 (Cline, `cline`, twenty-fifth): the car wash — "that petrol station is the car wash", and the washing scene already plays (wiring left as WIP for `glm`)


The owner: *"I need you to look into the map for me. Is this a custom petrol station or was
this in game, we have a asset viewer which may help. But that area has a carwash next to it
and it hangs over the road a bit — I don't think car wash was meant to be there, which makes
me doubt this petrol station. Then after that I would like you to try wire car wash scene up
and hopefully some of the exteriors around buildings like caravan and houses."*

Then, part-way through: *"due to vision limitations please just document it as wip and will
have glm finish it off"* — so this session **investigates and hands over**, it does not wire.

### 1. The answer to the asset question: retail, and the station is the car wash

Nothing in that area is ours. `ecw_a.bnk` (both islands, identical 5,853,184 B file) is one
Scenery set (`ecw_ai_sc`, `.3dg` 821,424 B) + 4 props (`cf_spong`, `cf_hose`, `cf_radio`,
2 `vfx_cinematics`) with **local bounds (-19.3, -0.2, -21.9)-(23.1, 11.1, 33.9)** and
**3 placements on Ibiza / 5 on Hawai** — a canopy, a shop and a price totem, i.e. a
fuel-station *shape* with the wash tunnel in front. The asset index has **no**
`pump`/`pompe`/`gas` asset inside it (`gaspump_d.2db` exists in one bank in the whole game,
the `gph_l2d` house garage). The canopy over the road is the spots.bin record: `T(position)
R_y(heading)`, position = the building origin (spot_exteriors_spec.md §2).

Our own two stations are elsewhere: the `FUEL1` OBJ prop (`<mods>/props.txt`
`ibiza FUEL1 11660 5.3 15866 0 fuel`) and the `EGAS_A` spots.bin copy at (12665, 16698).

Evidence: `-gm WORLD_VIEWER -island ibiza -spot ECW_A` ->
`SpotExterior: Euro/Bnk/islands/ibiza/exterior/ecw_a.bnk: 1 set models, 4 props, local
bounds (-19.3, -0.2, -21.9) - (23.1, 11.1, 33.9)`, `SpotExteriors: ECW_A at (8129, 26222):
5 models, 3 placements`; screenshot `docs/images/carwash-forecourt-s25.webp` (the forecourt,
the canopy reaching the kerb, the `POLICIA LOCAL` block behind).

### 2. The washing scene exists and **plays today**

The bank carries the full retail beat: 10 `.cin` + 10 `.sce` (`C_INTRO_E_1/E_2`, **`C_WASHING`**
16.4 s / 3 shots, `C_LOOP`, `C_OUTRO`, `C_OUTRO_FERRARI`, `C_INCAR`, `C_BABE_01/02/03`),
18 `.anm` (the three babes' sponge / hose / radio / bucket loops), 3 `m_washing_*.wav`, and
the tunnel interior `interior/icw_a.bnk` (rollers `rouleau`, machinery) — plus the retail
strings 3913 / 3942 and the save fields `CarWashCoating` / `BeenInCarWash`.

`GMCinViewer` + `IntroCinematic` are generic enough that no code is needed to *play* it:

    build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-cline-userdata ^
        -gm CINVIEWER -cinbank islands/ibiza/exterior/ecw_a.bnk -cin C_WASHING ^
        -cinspot 8129,26222 -cinexterior ecw_a -logcin -logentities

    ENTITY cinematic 'C_WASHING' …      CINVIEWER: playing 'C_WASHING' of 'islands/ibiza/exterior/ecw_a.bnk'
    CINVIEWER: t 1.0 main 'C_WASHING' 1.0 camera (8115.7, 149.6, 26210.0) heading 116.6 fov 0.66

`docs/images/carwash-scene-s25.webp` is that first shot: the three babes walking out to the
car on the forecourt. `otdu2_cine_tool dump <bank>.bnk _cwdump` gave the scene / camera /
actor tables (`docs/re/car_wash_spec.md` §2).

### 3. What is missing (the WIP handoff)

All of it is listed as a 7-step plan in **`docs/re/car_wash_spec.md` §4**; the short form:

- `CinViewerRequest` must carry the **spot** (`PickX`/`PickZ`), the **island** (the exterior
  path is Ibiza-hardcoded in `gm_cin_viewer.cpp` ~196) and the **player's car**
  (`gm_cin_viewer.cpp:224` always loads the California `D_CAR`). `bFromStory = true` is what
  makes the viewer hand the car pose back through `StoryHandoff`.
- `StoryHandoff` is applied only when `islandName == "ibiza"` (`gm_freeride.cpp:183`) — 5 of
  the 8 car washes are on Hawai, so that must be relaxed.
- No `source/game/freeride/car_wash.{h,cpp}`, no prompt in the near-spot block
  (`gm_freeride.cpp` ~1255-1450, where the `carwash` HUD tile art already exists), no
  `BeenInCarWash` / `CarWashCoating` in the profile, no fee.

Trap worth the sentence: the cinematic is looked up by **entity name** — `C_WASHING`. With
`-cin ecw_ai_sc_M_WASHING` (the `.cin` file name) the bank loads, the mode logs
`main '' 0.0` at a static camera and *nothing plays*, which reads as "the cinematic is
broken".

### 4. The caravan / houses half of the ask

- Houses: already streamed — `SpotExteriors: 80 spots, 80 with an exterior bank` (Ibiza, incl.
  the mods record), 144/144 on Hawai; the `lph_*`/`gph_*` interiors are separate.
- Caravan: there is **no** such asset in the 153,690-asset index — the camping family is
  `camping.2db` (a *panel texture*, sector-1-2-5-4) and `sf_tente` (tent street fitting, three
  Hawai sectors). A caravan park is therefore **sector decor** (PMI instances), which
  `SpotExteriors` never draws; the next step is to find its sector and check the instance
  type is one `EdenWorld::loadInstances` reads (session 24: type 3 was silently skipped).

### Not done / next

- Wire the 7 steps above (start at `gm_cin_viewer.h`'s `CinViewerRequest`, then the FREERIDE
  prompt) — verified the way the fuel feature was (`-atwash` hook, log line, screenshot).
- The 9 other timelines (`C_INTRO_E_1` -> `C_WASHING` -> `C_OUTRO` is the retail beat) and
  the `icw_a` tunnel (rollers/machinery, no drive-through yet).
- Dirt: no per-car dirt and no dirt shader in the port, so `CarWashCoating` has nothing to
  clean; the wash today can only be the scene + fee + flag.


## Session 2026-10-31 (Cline, `cline`, twenty-fourth): the road barriers were never loaded at all



The owner: *"road barriers aren't fully showing up and I think some world assets
may not be hooked up like grass and stuff could we start doing that thanks"*.
(GLM is on the tunnels/bridges terrain while this runs.)

**Reproduced / diagnosed** — no barrier of the S list has ever been drawn. The
cause is not a rendering bug, it is a missing loader: every sector bank carries a
`Sector-…-S.pmi` (bank group 5, subtype 50) and `EdenWorld::loadInstances` only
iterated `{ "-v", "-o", "-a" }`, while the type dispatch handles 1 (static), 2/4
(actors) and `continue`s on anything else — type 3 is exactly the S list. Same
list, same omission, in `VehicleWorldCollision::addLibraryInstances`
(`{ "-v", "-o" }`), so the barriers had no collision either.

The data (`[DATA]`, read with `otdu2_bnk_tool list/extract` + `tools_probe`):

| sector | S.pmi | content |
|---|---|---|
| `ibiza/area-3/area-3-3/sector-3-3-0-0` | 49 936 B | 103 × `SF_GLISS`, type 3, extraSize 448 |
| `ibiza/area-3/area-3-3/sector-3-3-0-4` | 2 672 B | 4 × `SF_GLISO`, type 3, extraSize 512 |
| `hawaii/…/sector-5-0-5-6` | — | 10 `SF_GLISO` + 14 `SF_GLISS` (logged) |

`extraSize / 64` = the number of bone matrices, and it matches the model exactly:
`commonworld.bnk` `library.3dg`'s `SF_GLISS` PRIM has `PrimType` high byte **7**
and a `BONE` child with palette `(0,1,2,3,4,5,6)`; `SF_GLISO` is **8**
`(0..7)`. Geometry: 20 m × 1.56 m × 0.42 m (`SF_GLISS`), 24 m × 2.46 m × 0.61 m
(`SF_GLISO`) — a rail, so the parse is right (the wrong stride gives a 21 m cube).

### What changed
- `source/world/pmi.h` / `pmi.cpp` — `PmiInstance::Bones`: the type-3 payload is
  read as `extraSize / 64` D3D row-major `Matrix4x4f` (the same memory layout the
  renderer already writes), model space.
- `source/render/eden/eden_model.h` / `.cpp` — `drawNodeItemsSkinned()`, the
  skinned twin of `drawNodeItems()`: each item's `BonePalette` (0..n-1 here)
  indexes the instance's palette into `EdenDrawParams::pSkinMatrices`, and the
  instance matrix stays `world` — the shader's own order, `v * Bone_i * World`.
- `source/world/eden_world.cpp` — `-s` added to the PMI suffix list; type 3
  resolves its model through the same `libraryNodes` path as type 1 (LODs 1, so
  never distance culled); the record's stored AABB is the **un-skinned** rotated
  box, so `drawInstances` uses a world box rebuilt from
  `libraryModel.getNodeBounds()` through every bone (`instance.World * bone`,
  `Matrix4x4f::operator*` applies its right operand first) and the 8 corners.
  `-logsinstances` prints both boxes.
- `source/physics/vehicle_collision.cpp` — `-s` added, and `addCollision` gets
  `instance.World * instance.Bones.front()` so the collision of a rail lands where
  the rail is drawn (the Havok bodies are the bind pose, i.e. 90° off the placed
  pose). **No-op today**: `Library.dhk` is 864 bodies / 461 models and carries no
  `SF_GLISS`/`SF_GLISO` hash at all (`b'SF_GLISS' in Library.dhk` is False), so
  `libraryModels.find()` misses every rail and the sector count is unchanged
  (“sector-5-0-5-6: 2329 library instances” before and after). The rails' bodies
  would have to come from the sector `.shk`, which is not read into this library.
- `tools_probe/pmi_probe.py` (new) — dumps a `.pmi`'s model/type/count table with
  the per-record position/scale/rotation/stored AABB and the type-3 bone count,
  and checks the `0x20 + count × stride` arithmetic, so the "what is placed here"
  question is answerable without running the game:
  `otdu2_bnk_tool extract <sector.bnk> <dir>` then
  `python tools_probe/pmi_probe.py <dir>/Sector-…-S.pmi -v`.

### Verified
- `build-cline.ps1` exit 0 (7 targets, `opentdu2-cline.exe` relinked).
- `-logsinstances` run, spawn (36382, 5730) Hawaii:
  **24 S instances — 14 `SF_GLISS` with 7 bones, 10 `SF_GLISO` with 8**.
  Rebuilt world box against the exporter's own box: **centre offset median
  0.47 m, 90th 2.41 m, max 6.01 m** (the residual is the exporter's padding; the
  wrong composition is 24 m, see below).
- A/B on the composition, same 4 `SF_GLISO` records, transforming the real
  vertex cloud (`_vertex_check.py`): bone-then-world **4.95 m**, world-then-bone
  5.28 m, bone-then-world with the rotation order reversed 24.59 m, no bones at
  all 47.75 m. So "the bone first" as the spec says is the right one, and
  `pmi.cpp`'s `scale * RotationX * RotationZ * RotationY` is already correct —
  its local `D3DMatrix::operator*` is the *standard* product while
  `Matrix4x4f::operator*` puts the right operand first, which is the trap.
- On screen: teleported to a logged rail
  (`-tp 35883.7,12.0,6869.0 -heading 90`, sector-5-1-6-0) and shot frame 260 —
  the crash barrier now runs along **both shoulders** of the coastal road,
  horizontal, at road height, following the curve. `docs/images/` has the frame
  (`-no3d` is fine for the log checks; the shot needs the 3D pass).

### Ruled out
- *The model is skinned in the wrong order*: the vertex-shader order is fixed by
  the avatar path (`drawSkinned` + `computeSkinMatrices`); bone-first is the only
  composition that puts the rail where the exporter says, so the data agrees with
  the shader.
- *A 3DG vertex-layout bug for these models* (`PRIM` type 0x240, semantics
  0,1,4,7,8): the existing `bSkinInStream0`/`bTexcoordsInStream0` masks give
  stride 20, which is the stride that reproduces a rail (12 + 4 + 4); stride 12
  or 16 gives the 21 m cube. `getNodeBounds()` matching the exporter's box at
  0.47 m is the proof, not an argument.

### Not done / next
- **CORRECTION (session 26, Cline): the terrain layer detection below is RIGHT; do
  not "fix" it.** The "slot 6 unnamed type / SHADOW at 2" reading came from
  `_mat_probe.py`, which walks the slots at a 16-byte stride; `2dm.cpp` (and the
  file) use 0x20 per slot. Re-read at 0x20 on `sector-3-3-0-0.2dm`: 0 COLOR,
  1 SHADOW, 2 DETAIL1 (`46469c9786ae83a3`), 3 DETAIL1 normal, 4 DETAIL2
  (`D2_EARTH`), 5 DETAIL2 normal, 6 DETAIL3 (empty) - exactly what
  `updateProgram` assumes (`spec[38] = 3`, spec[0] = 6). **Real grass** is a
  separate system, none of it read yet: `islands/<island>/fx/tallgrass.bnk`
  (`tallgrass.3dg/.3dd/.2dm` = the TD_TallGrass quad, `setgrass.2db` 9 blade
  bitmaps + `setbush.2db` 4 bush bitmaps, `noise.rle`), `fx/grasstypes.xml`
  (`GrassType Type=<id> Name=`: `Grass`/`Bush` BitmapIndex, Height, Width,
  SizeDelta, UseNoiseScale, ScalePower/Clamp, NoShadow; `fx.ini [GRASS]
  NBGRASSBITMAPS=9 NBBUSHBITMAPS=4`) and the per-sector `Sector-*.flg`
  (`FLAGDATA`, 256x256, RLE rows - probably the per-cell GrassType id; not
  decoded: `.pgr` is `RLE.DATA` with the same shape). Next step: decode the
  `.flg` rows against the Type ids (0x04,0x10,0x20 ... 0xF0 in grasstypes.xml),
  then draw instanced TD_TallGrass quads near the camera.
- **`.flg` layout as measured so far (Sector-3-3-0-0.flg, 20 380 B) `[DATA]`, format NOT
  decoded:** 0x0c `FLAGDATA`; 0x14 `01 00 01 00`; 0x18 u32 0x7d8 (= end of the first
  block); 0x20 u16 256, u16 256 (the grid); 0x24 u32 1285; 0x28 u32 384; then a table of
  ~491 u32 up to 0x7d8, whose values (264, 778, 966 ... 1654, 1664) look like byte offsets
  into the table region itself, i.e. a per-row / per-run RLE index. The stream at 0x620
  is 2-byte pairs (`ff 06`, `1e 06`, `00 01` ...) - `ff 06` repeats for the empty cells.
  At 0x7d8 a second block starts, `SCOL` (u32 1, u32 0x4420 size, 0, then 0x80, 0x80,
  1, 0x100 and RGBx bytes `1c 18 11 ff ...`): a 128x128 per-sector colour table, the
  tint the grass takes from the ground. `.pgr` is the same container (`RLE.DATA`, size
  0x4420, 0x7d8 -> 0x4430). `tools_probe` has no probe for it yet. Not drawn: no grass
  renderer exists, and the TD_TallGrass shader permutation/sampler layout is unchecked.
  The previous "traffic" work is in the tree too: another session has UNCOMMITTED
  edits in traffic.{h,cpp}, chase_mode.*, gm_freeride.cpp (police / chase) - I did not
  touch them; traffic signals / lane changes / Casino traffic are still open
  (`docs/re/traffic_spec.md` "Not done" is out of date: headlight glow quads at night
  are already in, glm `30eadaf`).
- **Grass (old note, superseded by the correction above).** The islands' grass is *not* a separate model list: it is the terrain
  material's `SHADOW` slot of `TD_HeightMap` (the sector `-s.2db`, which is a
  1024² map, doubles as the grass/night-light layer —
  `source/render/eden/eden_renderer.cpp` 544-549). The terrain material does
  reference it: `_mat_probe.py` on `sector-3-3-0-0.2dm` shows slot 0 `COLOR` =
  `-c.2db`, slot 2 `SHADOW` = `-s.2db`, slot 4 `DETAIL1`, slot 6 an unnamed type
  — but `updateProgram()` guesses the detail layers from **indices 2/4/6**
  (`bDetail1 = TextureHashes[2] != 0`) as if the layout were COLOR, SHADOW,
  DETAIL1, DETAIL1n, DETAIL2…, so on the shipped material it reads SHADOW as
  DETAIL1 and enables all three layers (`spec[38] = 7`). The single next step is
  to make that detection slot-*type* driven (the way `assignTexture()` already
  matches `SlotTypes[i] == GetIdentifier64bit("DETAIL1")`), then A/B the
  permutation visually: same `-tp`/`shot` recipe over a grass field, mean pixel
  difference of the two frames, and check the `TD_HeightMap` shader's
  sampler→layer mapping in `Euro/Shaders` before changing which slot feeds which
  layer. I did **not** change it: a wrong permutation index is a terrain-wide
  visual regression and the shader's layer order is not decoded yet.
- **The rails still have no collision.** `VehicleWorldCollision` now asks for the
  S list, but its body library (`Library.dhk`, from `commonworld.bnk`) has no
  `SF_GLISS`/`SF_GLISO` model, so nothing is added. The sector banks do carry
  `Sector-…-R.shk` (197 312 B in 3-3-3-2) and `Sector-….shk` (401 360 B) — the
  next step is to parse the sector Havok packfile for the S models and add those
  bodies, then a drive-along-the-barrier test (`-tp` onto a rail) to confirm.
- `Sector-3-3-0-0`'s S.pmi holds 103 **byte-identical** records at the sector
  origin (pos ≈ (0.17, 0.8, 0.0), all at the sector corner). That is what the
  file says, not a parse error — worth a look at whether the retail exporter
  collapsed a rail spline there (every other sector has distinct records).
- `F.pmi` is 464 bytes (empty) in every sector checked; nothing to do.

## Session 2026-10-30 (Cline, `cline`, twenty-first): the bridge — a contact is resolved through the face the car entered



The owner: *"i managed to eventually clip through a bridge and back on road needs
it's logic improving please"*.

### What was wrong (measured, not guessed)

Three defects in the driving collision (`source/physics/vehicle_collision.cpp`,
`vehicle_dynamics.cpp`), all on the same failure: a car on a bridge deck ends up
**below** it, and once below, nothing can see it any more (the suspension rays
only look down, the chassis spheres only reach 0.3 m).

1. **The contact normal pointed the wrong way through a surface.** For a
   triangle, `sphereContact` built the normal from the closest-point offset, so a
   sphere centre *below* an up-facing road triangle got a **downward** normal: the
   position correction pushed the car further down through the deck, and the
   contact was lost once the centre was more than the collision radius below it.
   The old code had a `signedDistance < 0 -> fallbackNormal` branch for exactly
   this, but the triangle path always passed a positive distance, so it was dead
   code. Now the sphere's **previous** centre (`pPrevious`, already passed for the
   box path) decides the side: same side -> unchanged; crossed the plane in one
   tick -> pushed fully back out through the face it **entered**. The same
   "entered face" test now also covers a thin box crossed in one tick (a 0.05 m
   railing plate: the closest point is on the *far* face, the previous centre was
   on the entry side).
2. **The depenetration cap was smaller than one landing tick.** The push-out was
   capped at 0.05 m per iteration x 2. At 90 Hz a landing penetrates ~0.2 m per
   tick, so the body sank a little more every tick until the contact was lost
   entirely. Now `min(depth x 0.8, 0.25 m)` x 4 iterations.
3. **A `.pmi` instance scale never reached the boxes.** The triangles and capsule
   ends are transformed with the placement matrix, the box half extents and the
   capsule radius were not - so the collision body of a scaled library model
   (a railing, a planter, a **bridge part**) was smaller than the model the
   renderer draws with the same matrix. **Measured: ~90 % of the placed library
   instances are scaled, up to 2.01x** (per-sector log line). `addCollision` now
   divides the scale out of the box transform (keeping it rigid) and multiplies it
   into the half extents; the capsule radius uses the mean factor.

Plus one robustness fix: the suspension ray is now **swept** - cast from where the
wheel was at the start of the previous tick (extended by the tick's drop) - so a
fast fall can no longer start the ray below the deck, where it would find no
floor at all and the car would free fall through the bridge.

### The test

`-vehtest` now runs `RunBridgeCollisionTest`: a synthetic bridge (a 4 m high deck
of road triangles with the heightfield 4 m below it, a 0.05 m railing plate, an
approach ramp) and **8 `VEHTEST bridge` checks**. The two contact-rule checks and
the drive runs are the verification; the rule checks fail on the pre-fix code.

### Verified

- `-vehtest` (`-gm FREERIDE -skipcine -no3d -nopad -exitframe 120`), all 8
  `VEHTEST bridge` lines PASS: the two contact rules (`above->below` normal
  `(0,+1,0)`, depth 0.500; `below->above` `(0,-1,0)`), drive, landing, hard
  landing (25 deg nose down at 113 km/h), railing, ramp, fast ramp. **The same
  build without the three fixes** (stashed): the two contact checks report
  `normal (0,-1,0) / depth 0.100 -> FAIL`.
- No regression on the island: the same drive
  (`-fixedtimestep -logdrive -autoplay "60:hold=accel" -exitframe 1200`, the
  village street, 120 DRIVE samples) with the fixes vs. without: max position
  delta **0.69 m** (during the crash into the tree at t 13 s), the end state the
  same spot (`35 926.10, 4.62, 5 784.23` vs `35 926.14, 4.62, 5 784.22`), the
  sector collision counts identical.
- `build-cline.ps1` exit 0.

### Not done / next

- The owner's *exact* bridge is not reproduced: I do not know which one, at what
  speed and attitude. The rule is fixed and unit-tested; the drive runs are
  guards. Worth a manual pass on a real bridge (the deck camber / expansion joint
  is the likely trigger: the deck's plane steps there, which is the crossing case).
- The `.pmi` scale fix changes prop collision everywhere (trees' trunk capsules
  and railing boxes grow up to 2x): a visual pass over the village and a bridge is
  wanted. If a prop now blocks the car where it visibly should not, the per-axis
  approximation for a rotated box inside a non-uniformly scaled instance is the
  first suspect (§8.4).
- `ResolveSphereHavokCollision` (the walk modes' library collision,
  `havok_collision.cpp`) still ignores the instance scale - same defect, other
  code path.

## Session 2026-11-01 (sixty-third, `glm`): the cutscene tool — the manifest, the CINVIEWER upgrades, the authored captions

The owner asked for the cutscene tooling they can use to help wire scenes
(*"we should have a dedicated tool that you's can also use headless ... refs
dumped for the scenes given location/coords, assets, characters, props,
voicelines. UI pop ups"*) and asked for subtitles for accessibility
(*"Can we do subtitles I don't think the game ever had any but If we can then
It would be a big help"*). Phase 2 of the cine_tool roadmap (the 2026-10-29
session's dump -> viewer -> editor plan). Planned with the owner: both parts
in one session, the viewer extends GM_CinViewer (STATE's own phase-2 note),
subtitles wanted.

### What changed

- `tools/cine_tool/cine_common.h` (new): the dump helpers (formatting,
  JsonOut, bank discovery, the shared .cin/.sce preamble) moved out of
  main.cpp so the manifest reuses them; `main.cpp` dispatches `manifest`
  (`-lang`, `-avatar` valued flags skipped by the positional loop);
  `manifest.cpp` (new) is the command.
- The manifest adds over dump: the scene REFS -> bank assets
  (`BankHashcodes::find(hashcode, 0, 8)` = the .anm, the runtime's own
  resolution); the objects -> entities.xmb (class/__Name/position; PNJ ->
  ConfigName); the characters from db_data.cpr (`LoadXmbFromFile`, the
  GSDatabase decrypt — DBPNJ `_sex`/`_fileconfigid` -> the .avt BodyID
  0xcd26 (pnj.bnk) -> the kBodies names, the 10 `_clothe_<slot>` -> DBClothe
  `_file` with the M_<->W_ swap + `_color` — the EdenAvatar::load walk);
  the audio: Fx events -> Fx__Root -> Fx__SourceData (Fx[0].rsc = .bfx,
  SoundList) -> the SND node walk (ParseBfx's documented layout) -> wav names
  via the bank (10, 6) hashcodes and the -lang language bank; the message
  protocol tables (cin-level + actor-level with the port's handler notes,
  [ENC] flags); the story kinds (the kKinds copy) and the -avatar ruling.
  The JSON also carries the scene-level Fx (the cin-level audio array alone
  missed the voices — they live on the .sce objects, e.g. Tess's lines).
- `cine_subtitles.{h,cpp}` (new): the captions CSV
  (`<userdata>/cine_subtitles/<C_NAME>.csv`, `start,end,"text"`, '#' comments,
  '""' escapes), `textAt` for the playback time, `draw` (the title font,
  bottom-center band on the 1280x720 canvas), `DumpSkeleton` (one empty row
  per voice cue: the .cin Fx events + the .sce scene Fx tracks whose root
  `isVoiceRoot`, scene times mapped through the shots, deduped — the same
  root fired from two objects lands once).
- `cinematic_audio.{h,cpp}`: `isVoiceRoot` (the LSGL tag check on the root's
  .bfx, no clips loaded, const).
- `intro_cinematic.{h,cpp}`: `seekMain` (the start() seek path), 
  `getMainCinematic`, the `subtitles` member (loaded per main cinematic in
  `start`, drawn at the end of `draw` so every scene user gets captions),
  `-subtitles 0|1`, `-cinesubdump` in start(), `drawDebugPaths` (the camera
  Position keys + actor paths + origin axes projected through the active
  camera into the 2D canvas).
- `gm_cin_viewer.{h,cpp}`: `-cinehud 0|1` (default on), `-cinepaths`,
  pause (IA_CarCamera — the autoplay `press=camera`) with 0.5 s steps
  (IA_MenuLeft/Right) and the frozen `cinematic.tick` while paused,
  `drawHud` (the timeline, cut points, Fx ticks amber for voices, the
  shot/time/camera readout, the PAUSED badge), `drawUI` (the browser: the
  bank's cinematics click-to-play, bank/cinematic fields + load (the mode
  re-enters), the scrub slider), `-cinetime` passed to playMain (the viewer
  ignored it — the auto-seek only applied to C_INTRO).
- `tools/CMakeLists.txt`: manifest.cpp in the target. README tools block +
  CLI rows, docs/re/cutscenes_inventory.md the manifest/viewer sections.

### Verified

- Manifest full tree: `otdu2_cine_tool manifest C:/TDU2-Gamedata/TestDrive2
  _out -json` exit 0, **136/136 JSON parse** (python json.load over all),
  142 voice cues in the JSON, 84 skeletons, 5 story-kind matches, 426
  character rows. Spot checks on meeting_tess: Tess = `STO_Tess_Wintory` ->
  woman, .avt `STO_Tess`, body 1 (woman_body/woman_head_occ), the outfit
  rows (W_PN_D_Dress_TESS, W_PN_S_Shoes_TESS, W_PN_A_Jewels_TESS); the
  animation keys resolve to the bank .anm with clip lengths/crossfades; the
  LSGL voices list all 8 language nodes with the US wav resolving from the
  bank and the rest "(wav not in this bank)" — and `-lang fr` flips it (the
  FR wav resolves from `meeting_tess__fr.bnk`); `flsh:generik4` -> "UI:
  INTRO.vmf gotoLabel 'generik4'" in new_intro; zcvn/zcne/phot -> "[ENC]".
- Captions: `-cinesubdump` wrote C_MEETING_TESS.csv.skeleton with 2 cues
  (1.75-4.79 = Tess's line 02 at its scene Fx time, 23.16-34.35 = the phone
  line mapped through the shot window — the manifest's own skeleton writes
  the same numbers); two lines authored by hand into C_MEETING_TESS.csv ->
  the caption "Tess: Hey! Are you awake in there?" on screen at t=2.6
  (docs/images/cine-subtitle-s63.webp), absent with `-subtitles 0`
  (cine-subtitle-off-s63.png, same pose band crop).
- Pause/scrub: `-cinetime 20 -autoplay "30:press=camera;45:shot=a;60:
  press=right;90:shot=b"` — the log "CINVIEWER: paused at 21.07 s", the HUD
  reads t 21.1 PAUSED then t 21.6 PAUSED (exactly the step), the camera
  position frozen and the pose re-derived at the new time
  (cine-hud-paused-s63.png). The cinehud timeline + readout + the ImGui
  browser in cine-hud-browser-s63.png.
- Paths: `-cinepaths` draws the camera/actor trails and the origin axes at
  eye level (cine-paths-s63.png) and over `-cintopview`
  (cine-paths-top-s63.png).
- Regressions: `-storystep 5` exit 0, `-gm CINE -cinekind 1` exit 0,
  `-atwash` exit 0 (the wash found, spawn logged), 700-frame FREERIDE drive
  exit 0 (0 stalls).

### Ruled out (with the test that ruled it out)

- The skeleton CSV naming: the manifest first wrote
  `<cin-entry>.csv.skeleton` (cin_meeting_tess_M_MEETING_TESS_totem...) while
  the engine loads by the DEFINITION name — fixed to `pDef->Name`
  (C_MEETING_TESS.csv.skeleton), re-run matches -cinesubdump byte for byte.
- The track-value integer reads: the .sce Animation/Message/Fx key fields are
  raw dwords bit-cast to float by the parser — `static_cast<uint32_t>`
  returned 0 for the small indexes; the BitsOf memcpy (the engine's keyRef)
  is the correct read.

### Not done / next

- Phase 3: the .cin/.sce write-back editor (the manifest JSON is its feed).
- `zcvn` / `zcne` / `zcde` / `phot` stay [ENC]-flagged (the manifest lists
  them; the retail handler is encrypted).
- The captions are a plain readable band — no retail styling to copy (retail
  ships none for the cutscene speech).
- The browser's bank load re-enters the mode (a full reload) — an in-place
  bank swap would need IntroCinematic re-load plumbing.

## Session 2026-10-31 (sixtieth, `glm`): the car lights are the retail driver — GLOSSFCT channels, glowing lamps per function, traffic brake lights

The owner's ask, verbatim: *"look into something it's mostly brake lights,
indicators, headlights please on a player car then traffic cars as the
california has them but last I tried weren't accurate the lights seemed to low
compared and not hooked to correct parts. I'm not sure if this needs to be a
thing that every model is different or not ... please don't do quick tests
without checking as readback on a log may tell you 1 thing but what's happening
may be something different so screenshots for these tests to check what's lit
and where"*. So: every state checked on screen, not just in the log.

### 1. What the first cut actually did wrong (measured, then read in the bytecode)

Session 11's driver raised the lens materials' `MATPARAM.x` (0.2 -> 1) as a
"lamp level" — session 11's own pixel tests had "proven" that was the only
lever. The baseline screenshots this session took (night, brake off / held,
indicator phases, same pose on the handbrake) showed: the round tail-lamp
centres glowing **amber** with the headlights on, braking brightening only the
**bumper inserts**, the third-light strip glowing whenever the lights were on,
and **no visible blink**. The real lever is in the shader: TD_CarPaint_NM ps96
(cross-checked against the upstream `335759a` tree's `car_lights_spec.md`,
which documents the same formula) is

```
m    = tex(GlossLight, uv) * (1,1,1,-1) + (0,0,0,1)      // california_lgt_g
emis = tex(TexLight,  uv) * dot(m * GLOSSFCT, 1)          // california_lgt_on
diffuse *= prod_i(1 - GLOSSFCT.i * m.i)
```

`GLOSSFCT` ships `(1, 1, 1, 1)` in the .2DM — drawn as loaded, **every lamp
function is on at once** (amber centres = blinkers, red triangles + strip =
brake, white wedges = reverse, rings + reflector = position). The retail
driver writes it per frame; session 11 never touched it. The art allocation
was confirmed by rasterising each lamp item's UV triangles (own .3dg parser)
over the `lgt_on` art: clusters -> the pale lens + amber dot, strip -> the red
band at v 0.95..0.99, front lens -> the white circle, side repeaters -> the
amber dot.

### 2. The driver (ported; `cinematic_car.{h,cpp}`)

`collectLights()` / `updateLights(dt)` / `applyLights(pWeather)` replace the
`lamps[4]` + MATPARAM mechanism (`-lampparam` deleted): the 9 light-material
names (LGT_FL/FR/RL/RR, LGT_RL_SK/RR_SK on the skinned roof, LGT_L/R/LGT_R_S
for the other cars' naming), GLOSSFCT = (brake, reverse, position, blinker of
the side) — front lamps take `max(position, headlight)` and carry no
brake/reverse; the blink timer is retail's 1.0 s period, lit while t < 0.6
(FUN_00c57ef0); the values come from the weather keys
`car_headlight_flare_intensity` / `car_poslight_intensity` /
`car_caustics_intensity` / `car_headlights_intensity` (all in
game_weathers.xmb, 0 by day, 1 at night; the `tunnel_` switch sits behind
`setInTunnel`, not fed yet). The `Fa_`/`FA_<L|B|S|R|W>_<pos>[_n][_C]` glow
quads and flare sprites show with their function (`-noflares`); the BREAKING /
REVERSING / HEADLIGHT boxes (car slots 13/14/15, TD_ShadowBox, base textures
breakcaustics / reversecaustics / headlightcaustics of common_car.bnk) get
their visibility + INTENSITY per FUN_00c63a30 — **but the renderer still skips
TD_ShadowBox**, so no ground pools yet (the projection constants were decoded
from TD_ShadowBox.sob's bytecode: Var_VS reg 60 = the box's world-view-proj,
Var_Scene_PS 0..7 = the ground projection; feeding them is the next step).
`-carlights 0|1|2` (0 = dark even at night, 1/2 = forced on) and
`-indicator left|right|hazard` are parsed in `updateLights`; `setHeadlights`
without a flag follows the caller's hour rule. `setBrakeLights` /
`setReverseLights` / `setIndicator` keep their names (gm_freeride's call sites
unchanged); the `-logcarlights` line grew the blink state.

### 3. Traffic (`traffic.{h,cpp}`, the half upstream does not have)

The traffic models carry no LGT_ materials — their glow is the `FA_<c>_<pos>`
sprite nodes (TD_Sprite FRL_F red / FRL_L, FRL_S, FRL_W white, vertex-coloured
red / orange). The old code toggled every FRL_/FX_LIGHT-material node as one
night group (`bLightsOn`), i.e. brake sprites always on at night, on every
car of the type. Now: the nodes are parsed by function letter; a type's
EdenModel is shared by its agents, so the per-agent lamps go through a new
`EdenDrawParams::ForceHiddenNodes` hide list honoured by `EdenModel::draw`
(by value — blended draws copy the params when deferred). L (front lights) +
B (rear position) at night, S (stop) for a braking agent — a decel this frame
(`limit + 0.3 < Speed`) holds the lamps 0.4 s so they do not flicker — R / W
never (the AI does not reverse or change lanes). Test hooks: `-trafficbrake`
(every live agent holds the brake lamps), `-trafficparkface 1` (the parked
cars face away, rears to the camera), `-notrafficlights` kept.

### 4. Verified on screen (the owner's rule: screenshots, not logs)

All at 3840x2160, fixed timestep, the car parked on the handbrake so two
states differ only in the lamps; crops in the session scratch (repo root
`_n*.ppm`/`_t*.ppm`):

- **Day, lights off**: the rear is the dark red `lgt_of` art — clusters dark,
  lower lamps clear plastic, no strip, no amber. (The first cut's always-lit
  look is gone.)
- **Day + brake**: the lower red triangles lit.
- **Night** (`-night`, weather '00'): red position rings on the clusters, lit
  projector lamps + lens rings + amber side repeaters at the front — the
  night look the owner asked for ("headlights not sure if work").
- **Night + brake**: triangles + the third-light strip red over the rings.
- **Indicator left**: the left cluster's amber centre lit at cycle t = 0.33
  and 0.5, **dark at 0.94** (0.6/0.4 cadence), right dark throughout.
- **Reverse** (gear R): the white wedges lit, with the brake reds when both
  are asked.
- **Traffic, night**: front-light sprites on every car (parked minivan, oncoming
  ambulance), rear position sprites on cars driving away.
- **Traffic, brake** (`-trafficbrake`): red stop sprites at the rear, day and
  night; **traffic, day, no brake**: no sprites at all.
- Regressions: `-storystep 5` exit 0; a night drive (700 frames, accel held)
  exit 0, 0 stalls, tail rings lit while driving.

### 5. Per-model (the owner's "is every model different?")

No per-model code: the driver collects by material/node NAME, and every car
carries its own. Scanned banks: f430_scuderia (4 LGT_ + the three boxes, no
glow quads), 458_italia (5 LGT_ + boxes), ac_427 (6 LGT_ + 9 FA_ glow nodes,
no boxes), california (6 LGT_ incl. the roof `_SK` pair + 8 glow nodes + all
three boxes). Whatever subset a car has, the driver uses.

### 6. Not done / next

- **The ground pools**: feed TD_ShadowBox's projection constants (Var_VS 60 =
  box world-view-proj per draw — the box nodes are car children so the
  transform is at hand; Var_Scene_PS 0..7 = the ground projection through the
  scene depth — needs the viewPos target the scene view already builds). That
  lights up `breakcaustics` / `reversecaustics` / `headlightcaustics` behind /
  ahead of the car — the retail brake-light "glow on the road".
- The tunnel detection (`getCarTunnelDistance`) and the `tunnel_*` weather
  keys; the projected headlight + light beams (upstream car_lights_spec 7-9).
- Traffic turn signals would need lane changes first.

## Session 2026-10-31 (fifty-ninth, `glm`): road names on signs — the Ibiza direction signs show their destination names, composed from the payloads nothing read

The owner's ask, verbatim: *"I hope you can fix road names on signs"* — with the
note that `cline` is working on "a ton of stuff" and nothing should overlap it
(it doesn't: cline's live WIP is `chase_mode.*` + speech; its reserved next steps
are the eden_renderer grass slots and the `.shk` rails — none touched here).

### What was actually broken (`tools_probe/sign_probe.py`, `docs/re/sign_text_spec.md`)

The sector `.pmi` A lists place the road-sign actors and, on Ibiza, every
direction sign carries its text in a **type-4 payload** — captured since the
`.pmi` reader (`pmi.cpp` 121) and never consumed; the plates drew the shared
blank plates of the `rs_maps` atlas. The probe (bank parser ported from
`bank.cpp` + the formats.md docs) established, from the shipped data:

- The blank plates are two bands of `rs_maps.2db` (commonworld.bnk, 1024x1024
  DXT1 sRGB, 9 mips): green arrow plate `v` 0.017-0.065, white arrow plate
  `v` 0.213-0.291; everything else on the sheet serves other sign actors
  (speed circles, STOP, chevrons — their faces are pre-baked).
- The payload grammar (2247 records / 1062 uniques on Ibiza): byte 0 = layout
  code, then fixed row windows per extraSize — 19/20/22 = one row (18/18/20-char
  name + distance byte in two of the shapes), 37/39/43 = two rows, 55/58/64 =
  three rows (`sign_text_spec.md` 2 has the table). Name windows are zero
  padded, `0x00` = space; distance bytes extend the digit charset
  `0x1b..0x24` = 0..9 and (single bytes for two digits) `0x25..0x2a` =
  10..15 [INF].
- Hawai'i: **2859 A lists, zero type-4 records** [DATA] — its US-style
  `rs_arn*` models sample the atlas' pre-baked lower half (`v` 0.25..1.0) and
  were never blank. The feature is Ibiza-only by data.
- `td_sign.sob` has no text constants: retail composed the faces CPU-side
  before the draw. The original's rasterizer/font is not in the banks we
  searched; the port uses the frontend "Berlin Sans FB" page (`fla_0116`,
  white glyphs on black, digits + accented Latin incl. `ñ`).

### The fix (`source/world/sign_text.{h,cpp}`, hooks in `eden_world.{h,cpp}`, `gm_freeride.cpp`)

- `SignFaceCache::decodePayload` — the grammar above, FNV-1a keyed (the
  payload pointers live only as long as the sector).
- `BuildSignLayout` — once per variant model: the `.3dg` parsed with
  `Render3DG` (converted triangle list, CPU UVs), triangles merged through
  shared edges (DSU), plate-sized quads inside the two bands grouped into
  rows (v overlap + model-space height within 0.15 m — `rs_dirl_d`'s two
  white plates share ONE atlas rect at heights 1.08 / 0.64 and stay separate
  zones; the pole prim has no UVs and its zero-UV quads drop out on their
  own). Zones: top plate first, name left / distance right, size = zone
  height x 0.75 capped 40 px, black.
- The face = full 1024x1024 copy of the decoded `rs_maps` mip 0 with the
  rows stamped in (the untouched texels stay, so every other sample of the
  material is unchanged), CPU-encoded DXT1 (4-color palette forced with
  `c0 > c1`) + box-filter mips, uploaded through a `Texture2DB` built in
  memory (`TextureCache::createFrom2DB` — the same path bank textures take,
  same sRGB format, no gamma shift). Cached per payload-hash x variant, LRU
  160, dropped at `EdenWorld::destroy`.
- `EdenWorld::getActorModel` records per direction-sign model the variant
  letter (`rs_dirl_<v>_...`) and the material slot whose `TextureHashes` is
  `rs_maps`; `drawInstances` swaps the composed face into that slot for the
  one draw and restores the original right after (the sector shadow-map
  mechanism — the material is shared by every instance of the actor). Zero
  changes to `eden_model` / `eden_renderer`.
- `FlashMovie::getFontPagePixels` (CPU pixels of a font's glyph page, bank
  entry -> `Texture2DB` -> `DecodeBlockCompressed`) + `GSFlash::getMainMovie`
  feed the rasterizer; `-nosigntext` / `-logsigns` / `-signfacesdump`
  (composed faces as scratch PPMs) / `-atsign [m]` (spawn scan: first type-4
  `RS_Dir*` record of the island's sector banks, spawn 15 m in front on the
  face side, `gm_freeride.cpp`, mirrors `-atwash`).

### The false turn worth remembering

The plate quads sample their atlas rect with **u running against the plate's
left-to-right** (the model mirror flips the geometry; the sector shadow-map
style texture swap inherits it). That was proven twice — once with zone-edge
markers painted into the face (the zone's u0 edge lands at the plate's
right), then lost when a "the rect samples as it is" re-reading of an angled
screenshot dropped the pre-flip; the A/B on the final build settled it: the
composed text is stamped into a scratch and blitted x-flipped into the zone,
and only then does the world read it normally. Lesson: when a render reads
mirrored at one angle and fine at another, decide with a single controlled
high-resolution A/B, not with angled screenshots of a sign cluster.

### Verified

- Ibiza `-atsign` spawn at sector-3-0-5-6 `RS_DirL_D_act` (22977, 5259):
  faces compose from the streamed payloads ('SANT VINCENT 4 / Platja /
  Balasant', ... — 4 uniques in view), plates readable in the 2560x1440
  screenshot (`docs/images/sign-text-s59.webp`; the composed green plate is
  `sign-face-composed-s59.png`).
- A/B vs `-nosigntext` at the same pose: sign-region mean abs diff 5.24,
  control regions 0.0 (the render is deterministic; only the sign's pixels
  change).
- `-storystep 5` exit 0; a 700-frame FREERIDE drive exit 0 with flat VRAM
  (1648 MB); Hawai'i `-atsign` exit 0 with a clean "no direction-sign
  actors" log and its baked signs untouched.

### Not done / next

- Rows beyond the variant's plate count are dropped (a 3-row payload on the
  2-zone variant d shows its first two rows; the second white plate repeats
  the first white row — both share the atlas cell). Retail may rewrite plate
  UVs per row; cracking that needs the retail UV-rewrite path
  (`FUN_00960f60` vtbl+0x58) — [INF] parked.
- The distance-charset reading `0x25..0x2a` = 10..15 is [INF]; screenshots
  confirm digits render there, not that the values are right.
- The font is a stand-in [INF] (Berlin Sans FB); retail's sign font is not
  in the banks we searched.

## Session 2026-10-31 (fifty-eighth, `glm`): the car wash wired - the washing cutscene plays from the driver's seat, and the broken in-car camera that hid behind a log flood

The owner's handoff, verbatim intent: *"I may need you to finish clines work on car wash
please"* — `cline` session 25 had investigated (`docs/re/car_wash_spec.md`, committed here
with its two evidence shots after the `origin/cline` merge) and left the 7-step wiring as
WIP, the owner having told it *"due to vision limitations please just document it as wip
and will have glm finish it off"*. The session started by merging `origin/cline`
(sessions 21-24: the barriers, the crash/exe probes) and carrying the three uncommitted
session-25 files over; the STATE conflict was resolved keeping both agents' blocks.

### The wiring (spec §4 -> §8)

`source/game/freeride/car_wash.{h,cpp}` (new): the island's `ECW_*` spots from
`LoadSpotList` (3 Ibiza / 5 Hawai / 0 casino), `findNear` X/Z, `fillRequest` (the
`C_WASHING` request at the spot frame, the island's `ecw_a.bnk`, the player's car,
`CarActor = "PLAYER_CAR"`), `kPrice = 10` [INF]. FREERIDE: the `ECW_*` spots leave the
interior-walk enterable set (they resolved `icw_a`, so the wash offered a tunnel walk
instead of a wash), the `carwash` tile + hint 3913 + validate = fee + `GM_CinViewer`
(`gm_freeride.cpp` tickWorldSpots), `CarWash carWash` loaded in `enter` unless `-nowash`;
`-atwash` (18 m in front, the spot record's ground y — a y-0 spawn parks under the island
because the fit probe shoots down; yields to a handoff so the return is not re-spawned),
`-logwash`. `CinViewerRequest`: `PickX/Z`, `Island` (feeds `Setup::HouseIsland`, so Hawai
picks Hawai's record), `ExteriorPrefix`, `Car`/`bPlayerCar` (the scene car in its owned
paint via `IntroCinematic::getCar()`), `CarActor`, `bStory`. `StoryHandoff::bStory`:
`GMFreeRide::enter` now applies a handoff on any island when it is not the dream's
(`islandName == "ibiza" || !handoff.bStory`) and a non-story handoff suppresses the house
flow — the Solar Crown goal stays the dream's alone. Profile: `BeenInCarWash` /
`CarWashCoating` saved as `been_in_car_wash` / `car_wash_coating` (`player_profile.*`).
The full map with deviations is **spec §8**.

### The two bugs the verification dug up

1. **The handoff silently failed** — the return logged the spawn position, not the
   scene-end pose. Root cause, found by measuring (temporary per-consumer render
   instrumentation, reverted before the commit): this bank has **no `Entity__PosCam` and
   no `Entity__CinCar`**, and the CINVIEWER called `loadCar` without the `.sce` car-actor
   name, so `computeCarWorld` (the handoff's pose source) failed while the visible car
   still followed the scene (the render path has its own `PLAYER_CAR` fallback,
   `intro_cinematic.cpp` ~2478). Fix: `CinViewerRequest::CarActor` through to `loadCar`;
   `gm_cine.cpp` had already been passing `"PLAYER_CAR"` for its own scenes — same
   mechanism. Session 25's spec claimed the bank "has" the `D_CAR` PosCam; corrected in
   the spec (`-logentities` prints none).
2. **The scene's in-car camera poisons every frame past 2.03 s** — the owner's report:
   *"it still plays but blackscreens normally there's more animation and it puts you in
   inside car view"*. `c_washing_02` (shot 2, 2.03-4.24 s, the in-camera) falls back to
   the **spot origin with a NaN heading** (`-logcin`: `t 3.0 ... heading -nan`); a NaN
   camera basis defeats every cull, the shadow pass draws **every streamed instance
   individually** (measured: 411,696 draws, 1.55 GB of per-draw `eden-constants` against
   the 512 MB transient buffer; top casters `V_TreeBranch_A` x22.3k, `V_HPin_G_Spr`
   x13.4k, `V_TreeBark_B` x13.4k), the first ~131k draws win and the rest skip — the black
   screen. The `.sce` shows the cause: 4 tracks where the healthy cameras have 3, a
   4-component position track, and an extra `00007055` track spanning exactly the shot —
   a parenting/look-at the sampler does not decode. **Not a regression**: `cline`'s own
   session-24 build reproduces it byte for byte (same `-nan`, 25.5M overflow lines, 2 GB
   log) — session 25's screenshot was taken at t=1.0 s, one second before the cut, so the
   bug had never been seen. **Parked on the owner's call** ("There's other animations if
   you want park this for now") — spec §9 holds the evidence and the fix sketch (decode
   the camera parenting, or at minimum clamp a NaN camera to the previous shot's pose so
   one broken camera can never defeat the culling).

What did land for it: **the overflow log cap** (`render_device.cpp` + `FrameData::
TransientOverflows`) — a flooded frame logged ~800k lines/s (2.6 GB/minute, twice
measured on real runs: the cancelled first attempt left a 2.65 GB log), which read as a
hang; now the first failure + one total line per frame, the callers already skip the draw
on the empty allocation, so the scene plays through (black from 2.03 s) instead of
wedging the machine.

### Verified

- Ibiza chain, headless (`-atwash -logwash -money 20000`, autoplay validate at frame 120,
  exit 1600): `CarWash: 3 wash(es) on 'ibiza'` -> prompt at 18.1 m ->
  `car wash 'ECW_A': paid $10 (money 19990) -> C_WASHING` -> `CINVIEWER: playing
  'C_WASHING'` -> `end of 'C_WASHING' after 16.4 s` -> `GAME MODE : FREERIDE` driving from
  the **scene-end pose** `(8126.23, 146.52, 26204.90)`. Shots: `docs/images/carwash-prompt-s58.webp`
  (the 3913 hint + the sponge tile), `carwash-scene-washing-s58.png` (the California on
  the forecourt, the babes walking in), `carwash-return-s58.png` (the HUD back up).
- Hawai: `5 wash(es)`, prompt 18.2 m, `-> C_WASHING` with the `hawai` bank path.
- `-nowash`: 0 prompts; `-atwash` still spawns (test hook).
- `-storystep 5` still reaches `GAME MODE : CINE`, exit 0.
- The overflow cap: a flooded run logs 2 lines/frame (133 total) instead of millions.

### Not done / next

- The parked camera (spec §9) — the in-car shot, and the same pattern likely in
  `C_INCAR` / the booth loops.
- The other nine timelines (`C_INTRO_E_1 -> C_WASHING -> C_OUTRO` is the retail beat).
- The fee/hint survive a reload only with the next profile save (as fuel does).
- The caravan sector hunt (spec §7's next step for GLM).


## Session 2026-10-31 (fifty-seventh, `glm`): the watchdog is a kill guard - an unresponsive app terminates itself instead of wedging the machine

Owner's report, after losing a machine to a hard freeze during testing ("Win key, ctrl, alt del etc... nothing
was working"): *"I want if app un responsive to kill/terminate the app."* The frame-loop watchdog
(source/core/watchdog.h, session 8's tool) already reported stalls into its own unblockable log; it only ever
wrote lines. Now it acts.

### What changed
- **`-watchdogkill <s>`** (default 60 s, `0` = off): when no frame has run for that long, the watchdog *thread*
  (independent of the stuck main thread, 200 ms slices) logs `KILL: no frame for ... phase '...'` into
  `opentdu2-watchdog.log` + the game log, then terminates the process (Windows `TerminateProcess` /
  POSIX `_Exit`) with exit code **`0x43544157`** ('WATC' - greppable in `%errorlevel%` and crash reports). The
  kill check runs every ~200 ms slice against the last beat, so a threshold below the 5 s report cadence still
  lands within one slice.
- **A debugger attached suppresses the kill** (`IsDebuggerPresent`): a breakpoint freezes every thread while the
  wall clock runs on, and the giant gap after continuing is not a real stall - the watchdog re-arms from now for
  as long as the debugger stays and logs `kill suppressed`.
- **Known-long jobs beat through it**: `-scantunnels` beats per sector (minutes of frame silence otherwise), the
  asset viewer beats its bank scan every 128 banks and the `-buildindex` walk per bank (also its Search-tab
  path). `Watchdog::Beat` is documented as safe from any thread now (two atomic stores).
- `-watchdogkill` is read in tdu_instance right after `Watchdog::Initialize` via `SetKillThreshold`; the
  startup line and the watchdog's own log header carry the active threshold.

### Verified
- **Kill end to end** (`-watchdogtest 65000`, frame 30 frozen in phase 'present'): STALL lines every 5 s, at
  60.0 s `KILL: ... terminating the process (exit code 0x43544157)`, the process ends itself at ~64 s wall,
  console exit code 87 (= 0x57, the low byte).
- `-watchdogkill 0` logs `kill threshold 0 s (was 60 s)` and the run exits normally; a normal 200-frame run
  shows the 60 s threshold in the startup line, no stalls, clean exit.
- **Long job under the guard**: the full Ibiza `-scantunnels` (minutes, 1135 sectors) ran to its `done.` line
  with the default 60 s kill armed - the beats kept it fed.
- Build exit 0.

### Honest limits (WINDOWS_SETUP.md gains a section on this)
Ctrl+Alt+Del dead means the kernel / GPU driver was stuck - **no process can act while that freeze lasts**; the
in-app kill guarantees the app does not stay wedged *after* the machine comes back (or when only the app is
wedged, which is the common case). The section documents the three levels: Windows TDR (check
`HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers` for `TdrLevel 0` / `TdrDelay` tweaks - TDR is what
rescues a real GPU hang; an up-to-date driver is the other half), the new kill guard, and testing hygiene
(windowed / VSync / frame limit while testing, `-no3d` for headless scans).

## Session 2026-10-31 (fifty-sixth, `glm`): the tunnels and bridges are open — the heightfield cells inside them are carved out of the opaque terrain (the owner's tunnel-sink report, the whole class surveyed and fixed)

Owner's report (screenshot, an arched Ibiza tunnel with a queue of traffic, "near where you take the car for a
ride"): *"terrain sinking into the tunnel ... this also looks like terrain may sink at bridges and maybe other
tunnels so please check - I did find it at a bridge before and Claude fixed that one but the reoccurrence feels
maybe more."* The earlier fix was session 22 (`bbc7407`): the terrain used to draw alpha-blended without depth
write and was sorted per mesh origin corner (see-through hills, floating sheets), so it was forced opaque with
depth write — which is exactly what makes the heightfield **fill** any tunnel / bridge volume it crosses: the
sheet is opaque geometry now, and depth order cannot hide it behind the tunnel shell.

### 1. The whole-island survey (`-scantunnels`) — the class measured, not guessed

`EdenWorld::scanTunnelTerrain()` loads every sector bank of the island one at a time (frees each after its scan),
picks the nodes named like a tunnel / bridge (`tun` / `pont` / `viad` / `bridge` on the node or its parent — the
`-logtunnels` rule plus the bridge spellings), samples the sector heightfield inside each node's footprint and
logs `band / below / above` counts (`band` = heights inside the node's y range = terrain crossing the object's
volume):

- **Ibiza: 1135 banks, 235 keyword nodes, 17 hits / ~12 sites** — the session-42 underpass (sector-3-3-3-4),
  the sunken road cutting under the roundabout (3-3-3-5, 1699/1736 samples inside!), the interchange (3-3-4-5),
  SB_TUNLA/B (3-3-4-7), four more `..._W_T` tunnels in the 3-3 / 3-4 hills, two coastal ones (3-0-2-7 / 3-0-3-7),
  and **`BP_PONTA` + `???PASSA`/`???PBOTC` at sector-1-2-6-5 (x 11117..11194, z 16100..16171) — the bridge ~1.1 km
  from the used car dealer `ESTOL_B_1275I` (12255, 16370)**, i.e. right where the owner takes the test-drive car.
  So yes: every tunnel cluster on the island and that dealer-side bridge carry the bug.
- **Hawai: 2860 banks, 1516 keyword nodes, 1 hit** (`TUNSOR`, sector-8-5-4-6).
- **Casino: 6 banks, 1 node, 0 hits.**

### 2. The retail side decoded (what the port should have been doing)

- The **layer table at 0x00f7f604** [DATA, pc_disasm] names the draw order: BUILDING 1, GROUND_OPAQUE 2, CAR 3,
  ROAD 4, ROAD_ALPHATEST 5, **HEIGHTMAP 6**, VEGETATION 7, ROAD_PATCH 8, ... OPAQUE 18 ... — the sector handler
  `TD_HeightMap` (0x00d716f0) returns **bucket 5**, and the layer index is the bucket: the terrain is blended
  **without depth write**, in the middle of the opaque pass.
- **`TD_Road` / `TD_Tunnel` share handler 0x00d71340** [CODE]: opaque road/tunnel surfaces get bucket 3/4, blended
  ones bucket 7 (ROAD_PATCH). The tunnel walls at the session-42 site are `TD_Building` bucket 0 (`CW_TunA`),
  the tunnel roads `TD_Tunnel` bucket 3 (`RoadTunnelMater`), the tunnel-hill mesh `TD_Ground_HM` bucket 0x11 —
  so in retail the terrain sheet that slices a tunnel never writes depth and the tunnel geometry paints over it.
- Session 42's A/B had already proven the behaviour (`-terrainblend -terrainearly` = `-noterrain` at the mouth);
  reproduced in this tree (`_ab42_*.png`): default fills the underpass black and puts dirt mounds over the
  retaining wall; `-terrainblend -terrainearly` opens it. Making *that* the default was session 42's stated next
  step, but full-footprint early terrain would also let objects paint over hills that occlude them (the A/B only
  ever checked four ground-level spots, never a panorama or the hills-behind-houses case).
- **Filed open: the `.flg` files.** Every sector bank carries `Sector-*.flg`, a RenderFile container whose single
  section is magic `FLAGDATA` (`02 00 00 00 ... 'FLAGDATA' 01 00 01 00 <u32 size>`), 19.8..26.9 KB per sector,
  payload ≈ 17.8-18.3 KB + a variable head, u16 values ≤ 65446 ≤ 66049 = 257². Smells like a per-cell flag grid
  (possibly retail's own terrain carve mask — which would explain retail's clean mouths at any draw order), but
  the layout did not decode in this session; no `.flg`/`FLAG` literals in the readable half of the exe. Next
  session can diff a flat sector's against a tunnel sector's cell by cell.

### 3. The fix: carve the crossing cells, keep everything else opaque (`-nocarve` for the A/B)

`EdenWorld::MarkCarvedCells` (at sector load, after the object bounds exist): every heightfield cell whose height
lies inside a tunnel / bridge node's y range within the node's footprint (+ one cell of margin) is marked; whole
quads with a marked corner go to a separate index buffer (`updateCarveBuffers`, built once, lazily at the first
draw) that **draws first without depth write** (cloned material, `bDepthWrite = false`) — the tunnel / bridge
geometry paints over it, exactly the proven `-terrainearly` mechanism, applied only where the sheet really
crosses the object. Every other cell keeps the opaque depth-writing draw: hills still hide the buildings, roads
and cars behind them (session 22's guarantees), cells above a tunnel roof keep their hill silhouette, and
carve-less islands / sectors take the untouched path (`Carved` stays empty). `unloadFar` and `-scantunnels`
free the new buffers. README CLI rows: `-scantunnels` / `-nocarve`.

### 4. Verified (`docs/images/tunnel-carve-*.png`)

- **Session-42 underpass mouth** (tp 21229.31,30.03,22262.84 heading 147.9): before = black-filled opening +
  `-nocarve` matches; **after = lit interior, lamps, pillar — identical to `-noterrain`** (`-underpass-before/
  -after.png`).
- **Roundabout cutting 3-3-3-5** (the survey's heaviest crossing, 1699/1736): before (`-nocarve`) the whole
  sunken road is rock-filled wall-to-wall; after it is fully open with walls, lamps and the glass canopy
  (`-roundabout-before/-after.png`). Same at the interchange 3-3-4-5 (`-interchange-before/-after.png`).
- **Controls unchanged**: casino spawn (2642,1588) continuous terrain, no floating sheets (`-casino-control.png`;
  casino has 0 crossings so it takes the untouched path by construction); dealer area and the 3-4-0-1 boulevard
  clean; the beach cells carved by `BP_PONTA` show no holes.
- Hawaii's single crossing (`TUNSOR`) is covered by the same code path (not separately rendered — filed for the
  next Hawaii pass).
- The dirt mounds above the retaining walls at the session-42 junction are **left in place on purpose**: the
  heightfield there caps at 36.0 m over a ~30 m wall top — real embankment terrain above the wall, visible in
  `-noterrain` too (i.e. data, not the draw bug). `-terrainearly` hid them only by letting the wall paint over
  nearer terrain; chasing that would need the full retail layer order (BUILDING/CAR/ROAD before the heightmap),
  a much bigger frame restructure across every mode. If the owner sees them in retail at that junction, the
  carve rule can take the wall nodes too (the keyword list is one array).

### Not done / next
- The `.flg` / FLAGDATA decode (section 2) — if it is retail's carve mask, swap the derived carve for the data's.
- The exact retail layer order (pre-terrain buckets 0..4) as the long-term shape; the owner's arched portal
  tunnel is one of the 3-3 / 3-4 cluster mouths fixed here — a drive-by in game will pick it out.
- `-terrainblend` / `-terrainearly` keep their material-state A/B roles; `-terrainearly`'s draw branch is gone
  (replaced by the carve).

## Session 2026-10-30 (Cline, `cline`, twenty-first): the bridge — a contact is resolved through the face the car entered

The owner: *"i managed to eventually clip through a bridge and back on road needs
it's logic improving please"*.

### What was wrong (measured, not guessed)

Three defects in the driving collision (`source/physics/vehicle_collision.cpp`,
`vehicle_dynamics.cpp`), all on the same failure: a car on a bridge deck ends up
**below** it, and once below, nothing can see it any more (the suspension rays
only look down, the chassis spheres only reach 0.3 m).

1. **The contact normal pointed the wrong way through a surface.** For a
   triangle, `sphereContact` built the normal from the closest-point offset, so a
   sphere centre *below* an up-facing road triangle got a **downward** normal: the
   position correction pushed the car further down through the deck, and the
   contact was lost once the centre was more than the collision radius below it.
   The old code had a `signedDistance < 0 -> fallbackNormal` branch for exactly
   this, but the triangle path always passed a positive distance, so it was dead
   code. Now the sphere's **previous** centre (`pPrevious`, already passed for the
   box path) decides the side: same side -> unchanged; crossed the plane in one
   tick -> pushed fully back out through the face it **entered**. The same
   "entered face" test now also covers a thin box crossed in one tick (a 0.05 m
   railing plate: the closest point is on the *far* face, the previous centre was
   on the entry side).
2. **The depenetration cap was smaller than one landing tick.** The push-out was
   capped at 0.05 m per iteration x 2. At 90 Hz a landing penetrates ~0.2 m per
   tick, so the body sank a little more every tick until the contact was lost
   entirely. Now `min(depth x 0.8, 0.25 m)` x 4 iterations.
3. **A `.pmi` instance scale never reached the boxes.** The triangles and capsule
   ends are transformed with the placement matrix, the box half extents and the
   capsule radius were not - so the collision body of a scaled library model
   (a railing, a planter, a **bridge part**) was smaller than the model the
   renderer draws with the same matrix. **Measured: ~90 % of the placed library
   instances are scaled, up to 2.01x** (per-sector log line). `addCollision` now
   divides the scale out of the box transform (keeping it rigid) and multiplies it
   into the half extents; the capsule radius uses the mean factor.

Plus one robustness fix: the suspension ray is now **swept** - cast from where the
wheel was at the start of the previous tick (extended by the tick's drop) - so a
fast fall can no longer start the ray below the deck, where it would find no
floor at all and the car would free fall through the bridge.

### The test

`-vehtest` now runs `RunBridgeCollisionTest`: a synthetic bridge (a 4 m high deck
of road triangles with the heightfield 4 m below it, a 0.05 m railing plate, an
approach ramp) and **8 `VEHTEST bridge` checks**. The two contact-rule checks and
the drive runs are the verification; the rule checks fail on the pre-fix code.

### Verified

- `-vehtest` (`-gm FREERIDE -skipcine -no3d -nopad -exitframe 120`), all 8
  `VEHTEST bridge` lines PASS: the two contact rules (`above->below` normal
  `(0,+1,0)`, depth 0.500; `below->above` `(0,-1,0)`), drive, landing, hard
  landing (25 deg nose down at 113 km/h), railing, ramp, fast ramp. **The same
  build without the three fixes** (stashed): the two contact checks report
  `normal (0,-1,0) / depth 0.100 -> FAIL`.
- No regression on the island: the same drive
  (`-fixedtimestep -logdrive -autoplay "60:hold=accel" -exitframe 1200`, the
  village street, 120 DRIVE samples) with the fixes vs. without: max position
  delta **0.69 m** (during the crash into the tree at t 13 s), the end state the
  same spot (`35 926.10, 4.62, 5 784.23` vs `35 926.14, 4.62, 5 784.22`), the
  sector collision counts identical.
- `build-cline.ps1` exit 0.

### Not done / next

- The owner's *exact* bridge is not reproduced: I do not know which one, at what
  speed and attitude. The rule is fixed and unit-tested; the drive runs are
  guards. Worth a manual pass on a real bridge (the deck camber / expansion joint
  is the likely trigger: the deck's plane steps there, which is the crossing case).
- The `.pmi` scale fix changes prop collision everywhere (trees' trunk capsules
  and railing boxes grow up to 2x): a visual pass over the village and a bridge is
  wanted. If a prop now blocks the car where it visibly should not, the per-axis
  approximation for a rotated box inside a non-uniformly scaled instance is the
  first suspect (§8.4).
- `ResolveSphereHavokCollision` (the walk modes' library collision,
  `havok_collision.cpp`) still ignores the instance scale - same defect, other
  code path.
## Session 2026-10-31 (fifty-fifth, `glm`): the ImGui panels are DPI aware — the cheats, the advanced settings and the asset tool readable on a high-DPI screen

The owner's task: "make the imgui throughout our project more dpi aware if possible - cheats menu and
advanced settings text is very small and hard to read for me also asset tool etc ... same situations".
Root cause: every ImGui panel used the embedded ProggyClean 13 px **bitmap** font and an unscaled style,
and this machine's Windows content scale is **225 %** — a 13 px font there is physically tiny. The render
side was already DPI-correct (the window is created with `SDL_WINDOW_HIGH_PIXEL_DENSITY` and `drawImGui`
maps ImGui coordinates through the draw data's FramebufferScale, renderer_2d.cpp), so only the font, the
style and the hardcoded panel sizes needed work. The vendored ImGui is 1.92, whose **dynamic fonts**
rasterize a TTF at any size on demand — the clean way in.

### What changed
- `source/debug/gs_imgui.{h,cpp}` — the service tracks `uiScale`: the `-imguiscale <f>` override wins,
  else `SDL_GetWindowDisplayScale` of the window (re-checked in every `beginFrame`, so dragging the
  window onto another monitor re-scales; guarded <= 0 -> 1; clamped 0.5..4). At init the fresh
  StyleColorsDark style is kept unscaled in `styleBase`; `applyScale` copies it back,
  `ScaleAllSizes(scale)`s and sets `style.FontScaleMain = scale`. When the applied scale is > 1 a system
  TTF is loaded once at the 13 px base (`-imguifont <path>` overrides; per-OS candidates: Windows
  segoeui/arial/calibri, macOS Arial/Helvetica, Linux DejaVuSans) and set as `FontDefault` — the embedded
  bitmap font cannot resize, a TTF + `FontScaleMain` rasterizes crisply at any size. At scale 1 nothing
  loads: the classic look is unchanged. The header gains `getScale()` / `setScale()` and the inline
  `DpiScale()` / `DpiScaled(x, y)` helpers (null-safe through `gpImGui`).
- The panels' hardcoded sizes go through the helpers: `cheats.cpp` (the 480x620 window at 20,90 plus the
  item widths and the spots child height), `advanced_graphics.cpp` (the 720 px width cap + the 40 px
  margin), `gm_asset_viewer.cpp` (the four panels' pos+size, the grid stays coherent),
  `gm_world_viewer.cpp` (400-wide window), `coords_overlay.cpp` (the 12,12 margin). The advanced
  settings' `SetWindowFontScale(1.3)` is relative and stays.
- `cheats.cpp` — a live **UI scale** slider (0.75..3.0) at the bottom of the F4 panel:
  `GSImGui::setScale` stores it as the session override (it wins over the OS scale and `-imguiscale`).
  Text and widgets rescale immediately; window sizes apply at the next open — a window's FirstUseEver
  size applies once, ImGui strips the condition from the window's allow-flags after the first
  application (imgui.cpp `SetWindowPos`), so there is no public way to re-derive them in place.
- `README.md` — the `-imguiscale` / `-imguifont` row of the command line options.

### Verified
- build-glm.ps1 exit 0.
- Every run logs `ImGui: font 'C:\Windows\Fonts\segoeui.ttf' (base 13 px), UI scale 2.25`;
  `-imguiscale 1` logs no font line and renders the classic bitmap UI (docs/images/ui-dpi-cheats-forced-1x.webp).
- Headless renders (docs/images/ui-dpi-*.png): the cheats panel (F4), the advanced settings (F2), the
  asset viewer's four panels, the world viewer, the coords overlay (`-coords`; note its toggle is F2 in
  tdu_instance.cpp, shared with the advanced settings — in the F2 shot it draws under that panel), and
  the forced-1x regression.

### Notes for the next sessions
- A mid-session scale change (the slider, or a monitor move) re-scales fonts, widgets and style
  immediately; already-open windows keep their pixel size until reopened (they reflow, and are
  resizable) — stated in the panel's hint line.
- If a machine has none of the candidate fonts, the style still scales but the text stays at the bitmap
  font's size; a warning logs and `-imguifont` is the fix.
- `-imguiscale 1` keeps every other agent's headless screenshots identical to before this session.

## Session 2026-10-30 (fifty-fourth, `glm`): the Solar Crown cutscenes + the used car dealer + its test drive - the story modes are playable, avatars seated in cars

The owner's task: cline's plan (docs/re/upstream_335759a.md), "just work on all this please - used car
dealership and the Solar Crown cutscene ... can we hook avatars into cars as well". Stage 0 (the story
spine, cline `1b187bc`) was merged into `glm` first (the merge conflict was docs/STATE.md only - both
agents prepend; both sides kept).

### What changed
- `source/game/avatar/ik_offsets.{h,cpp}` (new, upstream verbatim) - the IKOffsetDB.bin reader with the
  27 node table and the driver/passenger seat groups (r1..r5).
- `source/game/avatar/seated_avatar.{h,cpp}` (new, upstream verbatim) - an avatar in a car seat: base
  clip (driver stance_turn / passenger sho|hit_stand_car), seat group + IKOffsetDB root
  (FUN_00b8ade0), Spine aim + arms/legs two-bone IK on the car's LR nodes, optional override clip and
  facial callback.
- `source/game/cine/gm_cine.{h,cpp}` (new, upstream, adapted) - mode 0x1f; kinds 1/2/3 (+0x2b igloo).
  Kind 1 **keeps the owner-tuned frame** (gm_cin_viewer.cpp: bSceneAt (11103, 4.6, 17252) heading 0 +
  snapSceneToGround + the 14 m bush hide) instead of upstream's spots.bin-second-transform guess -
  `-cinespotplace` is the upstream comparison A/B (placeEntityAt / liftOntoFloor). -cinekind /
  -cinetime; the story callback chains 3->4 (MISSION: **not ported yet**, the mode falls to the
  "not implemented" warning), 5->6, 6->7 (SHOP).
- `source/game/shop/gm_shop.{h,cpp}`, `gm_testcar.{h,cpp}` (new, upstream, adapted) - the dealer UI
  (SHOPSCAR.vmf via the new `Setup::UiMovie`), car data / price / compare / buy (garage record +
  money), enter-car (cockpit camera from camerasingingame.xmb mode 23, seated show car), test-drive
  hand-off (TestDriveRequest); TESTCAR: FREERIDE-style driving rig, HUD countdown (setTestDrive /
  setCountdown), driver + passenger SeatedAvatars.
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` - the upstream additions ported into our
  divergent file: Setup::bExteriorCollision / InteriorBank / UiMovie; playMain startTime; the GENERAL
  command callback + dialog box (dbxo/dbxc) + getGeneral(); loadCar 4th arg (carActor: PLAYER_CAR) +
  the computeCarWorld actor-rotation conjugate fix; ActorMessage collect/take (Message tracks);
  liftOntoFloor / placeEntityAt / SpotRecord(+0x28/+0x38/+0x100) / ReadSpot / getGroundHeight;
  fitOnSceneFloor (when the scene frame is off the spot frame the car floor goes through the world:
  spot collision, else terrain); addShowCar family + addSelectable + getSceneBank/Hashcodes/CinProps/
  SceneOrigin; setSeatedShowCar (updateSeatedPlayer seats the player in a show car); drawUi (-no3d);
  bSceneAt now sets bSceneMoved (its floor queries were scene-local against the spot collision).
- `source/game/avatar/eden_avatar.cpp` - **the clothes sex-prefix fix**: AVT_Male's top/bottom/shoes
  point at the W_ED_*_Groom_01 rows (DBClothe _sex 1); the loader now swaps the W_/M_ prefix by
  avatar sex for body garments (head wear as-is) when the paired bank exists. Without it the male
  body wears the woman's shirt mesh and pokes through it.
- `source/game/cinematic/cinematic_car.{h,cpp}` - FindDatabaseDesc(idCar, rimsHashcode) (upstream) and
  applyColor(colorId) on top of our applyDatabasePaint (row lookup by _color_id_1).
- `source/game/cinematic/cinematic_scene.{h,cpp}` - getOriginRotation (columns -> quaternion).
- `source/game/walkmode/walk_mode.{h,cpp}` - setCursorLabels (the /cursor "on"/"off" labels,
  SHOPSCAR's cursor) + setSelectionCallback.
- `source/game/freeride/freeride_hud.{h,cpp}` - setTestDrive (/:IsTest + /HUD/time_cd "open") and
  setCountdown (/:MinCd/:SecCd/:CentCd, FUN_00a11120).
- `source/game/player_profile.{h,cpp}` - OwnedCar/Garage/CurrentGarageSlot/LoanCar +
  TestDriveRequest/GetTestDriveRequest (the legacy OwnedCars/OwnedCarPaints stay).
- `source/tdu_instance.cpp` - createGameMode cases GM_Cine / GM_Shop / GM_TestCar (GM_Mission and
  GM_FreeDrive stay unregistered).

### Verified
- Build exit 0 (722/722).
- The chain, headless: `-storystep 5 -no3d -exitframe 16000` - CINE kind 2 -> "end of C_SOLAR_CROWN_E
  -> story step 6" -> CINE kind 3 -> "end of C_SOLAR_CROWN_I -> story step 7 'Used Car Dealer' (SHOP)"
  -> SHOP enter (spot type 0x11, DBCarShop[25], Mustang 371 / Esprit 380 / Delta 355 painted, loan
  car 0x146, 'Used Car Dealer' 3287, 3 cars, SHOPSCAR FSCommands sndp:mopen/mclose live).
- `-storystep 6 -cinetime 100` reaches the same SHOP hand-off quickly (the seek path).
- 3D renders (docs/images/story-*.png): Solar Crown exterior (red carpet arrival, TV-set area);
  Meeting Tess (tuned frame, +0.25 m snap == CINVIEWER's numbers exactly; car 11094.99, 3.60 -> 3.85);
  the dealer walk ("Buy a car!" goal 4033, reticle, 3 cars at D_CAR_01..03); TESTCAR (countdown
  00:18 ticking, driver + Jude visible in the cabin, -carcam A/B).
- `-gm TESTCAR -testtime 25` drives (4 wheels, 1322 kg, countdown, end -> SHOP).

### Ruled out (with the test that ruled it out)
- The Meeting Tess sink (-1.17 m, "the character is sunk too far down"): the new
  Setup::bExteriorCollision loaded the hotel .shk and fitOnSceneFloor sampled it scene-locally
  (bSceneAt did not set bSceneMoved); with the tuned frame the staging must not load the exterior
  collision at all (kind 1 sets it only for -cinespotplace) - the snap is +0.25 m again, byte-equal
  pose with CINVIEWER.
- The white party shirt vs the valet livery: AVT_Male_Intro (raw, CINVIEWER) draws the party
  t-shirt; the owner confirmed the valet outfit (the GetStoryAvatarConfig AVT_Male row,
  W_ED_*_Groom_01) is correct - kept.

### Not done / next
- **The valet shirt's overlay layer renders wrong**
  (docs/images/story-cine-meeting-tess-shirt-overlay-artifact.webp vs the retail frame: clean shirt,
  just the chest badge): our render shows scrambled gold patches at the placket / collar / cuffs.
  The garment's 2dm has TWO materials - the base (shader d5013ed0dba9daca) and the overlay with
  LAYER1='BUTTON' / DETAIL_G slots (shader d5013ed0db79aeae) drawing the second 3dd mesh
  (groom_logo_c.2db). Next: how the LAYER slots and that second shader are meant to draw (alpha /
  offset), starting with `-logmaterials` on the ASSETVIEWER repro `-gm ASSETVIEWER -bank
  Euro/Bnk/avatar/clothes/eden/m_ed_t_shirt_groom_01.bnk -asset m_ed_t_shirt_groom_01.3dd`.
- The story step 3 -> 4 hand-off needs GMMission (upstream stage 3: gm_mission, ige_file,
  cinematic_props, event_reactions, road_network, takeImpactSpeed, HUD target). GM_FreeDrive (the
  sandbox tool) also not ported, deliberately.
- SHOP: the walk-selection -> buy -> C_EXPLAIN_FROG -> FREERIDE step 8 chain implemented but not yet
  exercised end to end headless (the FSCommands route; next session drives a buy with -autoplay keys).

## Session 2026-10-30 (fifty-third, `glm`): the plastic surgery clinic investigated — the buildings, interiors and face system are already wired; the feature is data-complete and ready to build

The owner's task: *"can we look into possibly adding the plastic surgery feature I know there's face bandages
that appear on face for a while after having it done I'm not sure if the location, buildings, interiors are
wired you should have a asset tool which you can use to help finding these things"*. **Brief: `docs/re/surgery_spec.md`** (new).
Answer: nothing is missing — three clinics exist, the clinic interior is walkable in the port right now,
the face-morph pipeline the surgery would drive is live, and the whole retail flow (prices, phone calls,
bandage items, save fields, UI strings) is in readable data.

### What was found (all `[DATA]` unless noted)

- **The buildings**: `ESUR_A` Ibiza spots.bin rec 71 at (4500, 37, 19113), `ESUR_A` Hawai rec 139 at
  (17227, 6, 18501), `ESUR_CA` casino rec 1 at (5742, 116, 399). NOTE: the spots.bin record layout is
  **bank name +0x10, position +0x18** — the `+0x08/+0x10` in the session-9 notes are 8 bytes off (verified
  against the `ESUR_A` ASCII in the file). Interiors `interior/isur_a.bnk` (+6 localized), `isur_ca.bnk`,
  plus `bsur_a.bnk` (+6) — the operation room (`Euro/spots/surgery/`, `head_chart` map) `[INF: "b" = bloc opératoire]`.
- **Retail's mode**: `GAME_MODE_SURGERY = 33` (0x21), `GamePC.cpr`: `AddFlashMovie "SHOPSAVATAR" 33` —
  the shopsavatar movie's surgeon panel, opened over the clinic (pool-interior mode list beside
  AVATAR_CHOICE/HAIRDRESSER, pool_interior_spec 2.1). Exe strings give the Flash contract
  (`surgeon.openMenuSurgeon`, `_root.TotalPriceStr`, `/:NbNotches`, `_root.LockSurgeryValid`,
  `EYE_COLOR`, `EYE_BROWS_TYPE`, `FRECKLES`, `TINT`, `/:surgery_locked`).
- **Prices**: `DBSurgeryCost`, 46 rows — every morph slider $100, EYE_COLOR $200, FRECKLES $200,
  EYE_BROWS_TYPE $300, TINT $400. The names are exact members of the port's `kFaceFxCustomNodes[49]`
  (the .avt FaceParamArray order) plus the .avt fields age/freckle/EyeID/EyebrowsID.
- **Bandages**: `DBClothe` 313 `W_HE_BANDAGE` / 314 `M_HE_BANDAGE` (parttype 5, $999999, banks
  `avatar/clothes/helm/*_he_bandage.bnk`); save fields `BandageTime/Bandage/NeedBandageNews/`
  `{Helmet,BikeHelmet,Glasses,Beard,Hair}Bandage` + colour variants (the 360 schema misses
  BikeHelmet; the readable exe strings have it); gates 4405/7169 ("cannot go back into surgery / to the
  hairdresser until bandages removed"), 5756 "You still have your bandages!", 7472 "Your bandages have
  been removed!". Story: `DBPhoneCall` SURGERY_IBIZA/HAWAI — npc "Miami" invites you (texts 3937/3938).
- **New tool**: `tools_probe/db_probe.py` — decrypts the `.cpr` database and dumps any table
  (`dump DBSurgeryCost`) or searches localization (`loc bandage`); the generic XMB walker the DB
  needed (python mirror of `source/core/xmb.cpp`, XTEA key per `cpr.cpp`).

### Verified (the port today)

    build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-userdata ^
        -gm FREERIDE -skipcine -nopad -interior ESUR_A -logselect -nointeriorintro -fixedtimestep ^
        -autoplay "200:shot=C:/OpenTDU2-glm/_surgery_entry.ppm" -exitframe 320

- `InteriorWalk: 'ESUR_A' (interior/isur_a.bnk): cinematics C_LIFE_FISH C_INTRO_I C_LIFE C_360_W
  C_OBSERVE C_360 C_HEAD; start camera 'cam_entree'; 2 selectables` — `Hostess_area` **type 27**
  ("surgeon", walk_mode.cpp:63) + `Exit`. Screenshot `docs/images/surgery_isur_a.webp` (surgeon at the
  desk under the reticle, operating chair, diplomas). A on the surgeon falls to the generic
  "This shop is not open yet." (`gm_freeride.cpp:2031`).
- The face morphs the menu would edit are live: FaceParamArray parsed (`eden_avatar.cpp:337`) →
  `FaceFxLibrary::Evaluate` drives the face **bones** (no vertex morphs) through the static path
  (`intro_cinematic.cpp:2603`); age/freckle already reach the skin material (`applySkin`,
  `eden_avatar.cpp:457`). Overriding the 49 bytes post-load changes the rendered face `[CODE]`.

### Not done / next

- Implementation plan in `docs/re/surgery_spec.md` §3 (three phases: native surgeon menu over
  DBSurgeryCost; face overrides + bandage in `profile.sav`; bandage worn through the clothes pipeline —
  the one port-side unknown: how the player avatar equips a DBClothe item today).
- The retail bandage duration is `[ENC]` (PS3 consumer of save +0x40/+0x48 not located;
  the serializer `func_0059CA78` confirms the field block); the port can start with an
  `[INF]` 24 in-game hours.

## Session 2026-10-30 (fifty-second, `glm`): the map after Cline's icons — LOD per zoom, the credits trap, and the props that never loaded in FREERIDE

The owner's report on session 18 (Cline's map fuel icon): *"the map itself needs work — some icons looked a
little too big and eventually caused the game to be unresponsive"*. Cline could not look at the screen (image
cap), so this session reproduced everything headless and read the screenshots.

**Reproduced** — three separate real bugs, all behind the two symptoms:
    build\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-icontest ^
        -gm FREERIDE -skipcine -nopad -fixedtimestep ^
        -autoplay "200:press=map;300:wheel=1;..." -exitframe N
(took a copy of Cline's test userdata as `C:\temp\otdu2-glm-icontest`; note `press=accel` does NOT step the map
zoom — the wheel (`wheel=1`) or the analog triggers do).

### What changed
- `source/game/map_screen.cpp` `applySpotLod` (new, called from `buildSpots` and on every zoom step) — the exe
  drives one frame label per zoom step on every point clone: `zoom_default` / `zoom_lod` / `dezoom_lod` /
  `dezoom_lod2` [CODE: the exe's strings, session 25]; the port stopped at `zoom_default` (buildSpots:998), so at
  the whole-island zoom every icon sat on its full-size art (~64 px tiles on a ~470 px island). Now level 0 =
  `dezoom_lod2` … level 3 = `zoom_default`.
- `drawFuelIcons` — the fuel icon has one art, so it now tracks the LOD ladder per zoom: 24 / 44 / 54 / 64 px
  [INF: measured off the LOD arts — dezoom_lod2 ≈ 25 px, dezoom_lod ≈ 46, zoom_default ≈ 64].
- `GMFreeRide::ensureModPropsLoaded` (new; `enter` + every `tick` until it lands) — `ModProps::load` ran in
  `enter()` behind `getEdenRenderer() != nullptr`, but the Eden renderer readies with the first world draw,
  AFTER enter — so in FREERIDE **the mod props and the fuel-station map list never loaded at all** (the owner
  never saw the fuel icon; Cline's verified lines were a run where it happened to be ready). Loads on the first
  tick the renderer exists; `bModPropsTried` guards it.
- The credits easter egg (zoom out past level 0): the natural trigger now logs
  (`MapScreen: zoom out at level 0 -> the credits roll (Back exits)`), `onBack` → `closeCreditsState` plays the
  root's own **`credits_back`** label [DATA: MAP.vmf labels] instead of letting Back close the map onto the
  credits frame (black stage), then `buildSpots` re-clones the icon set (the frame change had dropped them) and
  re-applies the LOD. `-mapcredits` now goes through the real `openCredits()` (it used to invoke the movie
  directly, which left `bCreditsOpen` false and made every Back close the map — that faked the "Back is broken"
  readings for most of the session).

### Verified
- LOD: screenshots at all four zoom levels (`_fx_z0..3`, `_fy_*`): whole-island view icons drop ~64 px → ~25 px
  (the movie's own `dezoom_lod2` art), `dezoom_lod` ≈ 46 px at level 1; the fuel icon rides the same ladder
  (24 px at level 0, visible on the west coast in `_fin3_back`).
- Fuel icons in FREERIDE: `ModProps: 'FUEL1' on ibiza at (11660.0, 5.3, 15866.0)` + `MapScreen: fuel station
  icon '...icons/fuel.png' (64x64)` + `1 fuel station icon(s) on the map, first at (471, 288)` — from a plain
  `-gm FREERIDE -skipcine` run, which never logged them before.
- Credits round trip (natural trigger, no `-mapcredits`): `zoom out at level 0 -> the credits roll` → the roll
  opens → Back → `credits_back` restores the map undimmed **with the full chrome and all 75 spot icons rebuilt**
  (`75 spot icons created` twice), fuel icon still in place (`_fin3_back`, `_fin3_z1`).
- Soaks post-fix: 7200 beats (open/close/zoom/pan cycles) and 5280 beats (credits round trip) — 0 stalls, worst
  frame gap is the known spawn frame.

### Ruled out (with the test that ruled it out)
- A texture/handle leak in the icon path — `MapScreen` is a by-value member of `GMFreeRide` (loads once,
  `bFuelIconTried` guards) and VRAM is flat across the soaks (`[perf] vram`).
- A hard hang anywhere on the map path — every soak reports 0 stalls through the watchdog; the "unresponsive"
  is the credits state (dimmed, textless, ~12 min at the [INF] 45 px/s roll; Back-exit used to land on a black
  stage), not a freeze.
- `-no3d` runs showing no fuel icon was not the icon code — with `-no3d` the props could not load pre-fix
  (renderer null at enter AND the old one-shot); post-fix they load as soon as the renderer readies, even under
  `-no3d`.

### Not done / next
- The credits **text never renders**: the probe (`-mapcredits`, tick 120) reads `/credits/scroll` alpha 100,
  visible, y scrolling, and `txt0` carrying `#color:999999#Executive Producer#nocolor#` — the roll is a live,
  filled scroll that draws nothing. The markup is the game's own text-engine syntax; the port's flash text path
  needs it decoded (next step: dump how `/credits/scroll/txt0`'s field renders vs a plain-string field).
- Cosmetic residue after a credits round trip: the tab-help bubble (`?` circle, top-left) and the no-entry
  cursor sign can show right after `credits_back`+rebuild; one more label (`_rootmenu_setup`?) probably clears
  them.
- The roll duration is the [INF] 45 px/s (~12 min over 31,568 px); retail's `CreditsPixelPerSec` is not decoded.
- Map panning is stick-analog only (`getLeftStickX/Y`) — autoplay's `press=left` cannot drive it (use `look=`,
  untested), which is why the level-2 LOD size is interpolated, not measured.

### The owner's correction (same session): the freeze was the pan, not the credits

*"my hang wasn't the credits — moving around the map while at certain zoom levels caused fps oddities which led
to the freeze of unresponsiveness."* Reproduced once the map could be panned headlessly:

- **Why the soaks missed it**: `press=left` is a digital action and the map pans off the analog left stick —
  no soak ever panned. `GSInput::setInjectedLeftStick` + the autoplay `move=x,y` command (the `look=` twin)
  close that gap; `move=0,0` releases.
- **The law**: at zoom ≥ 1 the map block streamed on `SectorKey(mapCenter) * 8 + radius` changing — i.e. on
  every sector-boundary crossing while the stick is held, `unloadFar(r+1)` + `loadAround(r)` ran synchronously.
  `getStreamRadius` = ceil(distance·1.2/768) capped [1,4]: **levels 1 and 2 both cap at radius 4 = up to 81
  sector loads in one frame**. Measured (level 1, continuous sweep across all four directions): fps 120 → 19-23,
  `[perf] update max` 141 / 184 / 223 ms, draw max ~95 ms. Level 0 never streams (the flat branch returns
  first) — matching "at certain zoom levels".
- **The fix**: `EdenWorld::loadAround` / `unloadFar` take a `maxSectors` budget (0 = unlimited, the drive's
  callers unchanged); the map calls them **every frame** with 1 load + 1 unload (the loaded/failed-sector guards
  make settled frames a cheap no-op scan). The key gate (`lastMapStreamKey`) is gone. One sector ≈ 20 ms, so a
  far-zoom square (~81 sectors) fills over ~1.4 s of ~20 ms update frames instead of one 223 ms frame.
- **Verified** (same `move=` sweep as the reproduction): `update max` 21.7 / 19.0 / 22.6 ms during the sweep
  (was 141/184/223), 0 stalls, and panning at level 2, level 1 and the flat map all stay smooth
  (`[perf]` windows in the run log); the drive re-stream around the car on zoom-to-flat / map close still
  hitches once (129 / 254 ms single frames) — pre-existing drive-side behaviour, untouched.
- **Still open**: the zoomed map view itself draws ~28 ms while panning (CPU command recording for the big
  streamed square — the port's known record-bound draw, PERF_PLAN P2 territory; the GPU is not the constraint).

## Session 2026-10-30 (fifty-first, `glm`): the photo shop trio — near plane all round, lit shop NPCs, the world outside the windows

Owner report: *"cinema camera need that fix all round ... I entered the photoshoot shop and camera has that
clipping issue which got fixed in other scenes"*; *"unsure if he has a bit of texture corruption or not"* (the
photographer); *"world isn't rendered outside"*. One root per symptom, all in the shared interior path
(`InteriorWalk` → `IntroCinematic`), so every shop benefits.

### What changed

- **Near plane (the clipping)** — the session-48 rule (`53c36a8`) keyed on the car body bounds; the shop entry
  cinematics have no car, so they kept the 0.8 m cinematic default and anything within 0.8 m of the lens clipped
  open. `Setup::bInterior` (`intro_cinematic.h`, set by `InteriorWalk::loadRoom`) makes `updateView` take the
  walk near plane 0.05 m for the whole shot — the WalkMode / dealer-PosCam value. Exterior cinematics keep the
  car rule (a global 0.05 costs depth precision at the 40 km far plane).
- **The near-black seller NPC (no texture corruption)** — `-logmaterials` shows **no** missing textures in the
  shop; the darkness was the lighting path: the shop cinematics carry no weather message → `setWeather(nullptr)`
  → `computeAvatarLighting` fell back to no spot gathering, `Shadow = 1.0` (the roof kills the sun term) and no
  camera light, while the room is lit by baked lightmaps the NPC never samples (avatar_lighting_spec.md 4b/4c/4d).
  Fix: an interior with no weather falls back to **`weat:Avatar` for the avatar light only** (the scene weather
  stays unset, the room's lmap is untouched) — sio (0.5, 0), camera light 0.25, shadow influence 0.4 [DATA
  cinematic_weathers]. `computeAvatarLighting` dropped `const` (`view.getWeathers()` is non-const). New
  `-logavatarlight` dump (once a second: position, amb/diff, shadow, spot colour, light count, weather).
- **The world outside the windows** — interiors loaded with `bWorld = false` → sector radius −1 → zero sectors,
  room parked at the island origin (spot_interiors_spec.md §5 "not done"). Now `InteriorWalk::enter` carries the
  spot bank name + island (`ibiza`/`hawai`/`casino`, the freeride `islandName`), the room is placed at its
  spots.bin record and **radius 1 (3×3 sectors) streams around it** (`Setup::bStreamWorld`; ocean stays keyed on
  `bWorld`, the house rooms keep the closed-room frame). `-nointeriorworld` restores the closed room (A/B).
  Spot lookup: the record id hashes the **full** name (`EPHOT_B_3337I` Ibiza, `EPHOT_B_9830` Hawai) while the
  walk carries the 8 byte bank field (`EPHOT_B`), so `findHouseSpotIn` matches that field when the island is
  pinned (the short names repeat across the islands); `view.initialize` takes the island for the streamed world.
  New `-interior <spot>` test hook in GMFreeRide (straight into a shop from the spawn, the `-housegarage`
  pattern) — this session's repro everywhere.

### Verified (`-interior EPHOT_B`, headless screenshots + `opentdu2.log`)

- `-listweathers` prints the avatar keys now: `Avatar` carries sio (0.50, 0.00) cam 0.25 shadinf 0.40 [DATA
  matches the spec table]. With the fallback: `AvatarLight: ... shadow 0.20 spot (0.17, 0.16, 0.17) lights 4
  weather Avatar` (was shadow 1.0, spot (0,0,0), weather none); 166 lines over one run, stable.
- Screenshots: the photographer and the player avatar lit like the room (was near-black); the entry cinematic
  and walk shots show no clipped-open geometry; through the window openings the **neighbour building renders**
  (was the tan `GroundColor` void). `-nointeriorworld`: no `house EPHOT_B at` line, no interior sectors (the old
  path, by the log).
- Perf (`[perf]`): entry = one synchronous 9-sector + bank load inside the spawn frame max (1853 ms update max in
  the first 120-frame bucket, spawn and enter together); after it, 5.9–16 ms frames with the sectors resident —
  no per-frame streaming cost. Caveat: in normal play the enter hitch (~1–2 s, one frame) is the shop's price
  today; retail fades over it — async streaming is the follow-up.
- Release build has no validation layer; the changes touch no Vulkan objects directly (one more world init on the
  existing `EdenSceneView`).

### Not done / next

- Ocean off in the streamed interiors (coastal shops would show it); the house rooms could join (`bStreamWorld`
  is shop-only by choice — their frame is verified and the win is smaller).
- The entry hitch → async/streamed load with the retail fade.
- `-logavatarlight` numbers for C_FIESTA_INTRO shot 2 / C_TESS (the spec §5 check), and the CARINTEG/ATTEFACT
  half of avatar_lighting_spec 4 for the paths that keep a weather.

## Session 2026-10-30 (fiftieth, `glm`): GPU timing + the first P2 cut + ray traced AO (`-rtao`)

Owner: *"i want to try boosting performance without cutting graphical fidelity we need to work on frame times
and pacing really"*, then *"gpu is pretty much idle while cpu works is there anything to make gpu work harder?"*
and *"I had ray tracing implemented ... see if it stress my GPU a little extra even if you use more rays"*.

### What changed
- `source/render/vulkan/render_device.{h,cpp}` — `beginGpuPass(name)` / `endGpuPass()`: a
  `VK_QUERY_TYPE_TIMESTAMP` pair per named pass into a per-frame-slot query pool (`kMaxGpuPasses` 16 pairs),
  reset at command buffer start (`vkCmdResetQueryPool` after `vkBeginCommandBuffer`), read back in `beginFrame`
  right after `waitForFrameFence` — the slot's results are then two frames old and complete, no host stall (the
  `-lumalog` pattern). Averages per name over 120 readbacks, printed as one `[perf] gpu:` line. Gate
  `-loggputimes`; when off there are no pools and the calls are no-ops; also off if the queue family has no
  timestamps. Pools destroyed in `destroy()`.
- `source/render/eden/eden_scene_view.cpp` — the eight offscreen passes bracketed next to the CPU
  `gSceneTimes.mark` calls (env, ocean, prepass, shadows, planar, opaque, blended, post); `render_device`'s
  `beginMainPass`/`endFrame` bracket the swapchain pass as "main" (scene blit + Flash HUD). Timestamps are
  written outside render passes only (vkCmdWriteTimestamp is illegal inside one).
- **P2 step 1**: `source/render/eden/eden_renderer.{h,cpp}` — `recordDraw`'s descriptor set is **cached per
  material / override combination per frame slot** (`drawSetCache`); the three transient constant buffers (VS,
  PS, spec) became `UNIFORM_BUFFER_DYNAMIC` bindings (shader-invisible), offsets passed at
  `vkCmdBindDescriptorSets`, so a draw allocates + writes 7 descriptors only when it is the first of its
  combination in the slot. Pool sizes split static/dynamic. `-nodescriptorcache` is the A/B fallback.
- **Ray traced AO** (`-rtao <samples>`, docs/re/ray_tracing.md step 6): `resources/shaders/rt_ao.comp` (ray
  query compute: world position from the scene depth + camera basis push constants, normal from two neighbour
  taps, `<samples>` cosine-weighted hemisphere rays vs the TLAS, any hit within 3 m = occluded) writes an R8
  mask (`rtAO` target, storage image; render target colours gained `VK_IMAGE_USAGE_STORAGE_BIT`); the mask
  multiplies on in `rt_ao_combine.frag` in place of `blit.frag` (plain-blit path only). `RayTracing::
  dispatchAO` records it inside the frame command buffer (no own submit, no readback), bracketed as the "rtao"
  `[perf] gpu` pass. `RenderDevice::getSampler` supplies the depth sampler; one descriptor set per frame slot.
  `-showaomask` views the raw mask. Requires `-rtworld`.
- `source/render/gs_render.{h,cpp}` — `getRtTlas()`; and the frame TLAS is **retired for two frames** on
  capacity growth instead of destroyed in flight (`pRtTlasRetired`) — the AO dispatch was the first in-frame
  TLAS consumer and the validation layer flagged the old destroy-while-recorded.

### Verified
- **GPU timing**: FREERIDE `-tp 21796.3,24188.1 -snaproad 150 -carlod 4`, twice back-to-back (~5 % spread):
  **frame 42-46 ms CPU, ~2.9 ms GPU total** — ocean 7.7 CPU / 0.90 GPU, prepass 8.0 / 0.90, opaque 8.0 / 0.94,
  shadows 6.7 / 0.64, env 1.4 / 0.22, main 0.02. **Every pass costs ~8-10x more to record than to execute.**
- **P2 step 1**: 38.8-42.5 ms cached vs 44.7-46.6 ms with `-nodescriptorcache` (~10 %), GPU identical,
  screenshot pixel diff mean 0.45/255 (env-cube refresh timing noise).
- **RT AO**: `-rtworld -rtao N -vk_enable_validation_layer` — **zero validation errors** after two fixes (the
  depth barrier's stencil aspect; the AO descriptor sets' allocate used one layout pointer for a count of 2 —
  garbage second set, segfault). Pixel readback (the `-shadowprobe` pattern): road depth 0.831 (4.7 m), road AO
  0.83 / walk AO 0.90, stable across frames with per-frame seeds. Composed frame at 64 rays: contact shadows
  under the car, along kerbs and hedges. **Cost table** (see ray_tracing.md §6): 4 rays 0.46 ms, 16 → 1.82,
  64 → 7.57, 256 → 30.14 ms GPU — linear ~0.12 ms/ray/pixel; **the GPU stops being idle at ~220 rays/pixel**;
  the CPU frame is untouched by the ray count.

### Ruled out (with the test that ruled it out)
- "Readback needs a wait": no — `vkGetQueryPoolResults` without `VK_QUERY_RESULT_WAIT_BIT` returns SUCCESS every
  frame once the fence is waited; the read is free.
- "The flag isn't parsed": a one-time log at the decision point showed `bGpuTiming` set correctly; the real bug
  was elsewhere.
- "The AO mask is broken in the dark regions": pixel readback gave sane AO (0.83/0.90) — the black areas in the
  mask screenshot were the Flash HUD cards drawn over it, plus genuinely heavy occlusion under tree canopy.

### The trap (cost the session a debug cycle)
- `endFrame` set `bFrameStarted = false` **before** the tail where the "main" end timestamp is written — the
  guard in `endGpuPass` early-returned, query 17 was never written, and ONE unavailable query makes the whole
  `vkGetQueryPoolResults` return VK_NOT_READY (the availability dump showed `111111111111111110`). The flag now
  drops after `endGpuPass`.
- Second one, same class: a `vkAllocateDescriptorSets` with `descriptorSetCount = 2` but a single layout pointer
  — the second set was garbage and the first dispatch segfaulted. Validation layer found it in one run.

### Not done / next
- **P2 continues**: the remaining per-draw CPU is `fillConstants` (the register writes), the transient alloc +
  memsets, push constants and buffer binds. Then multi-threaded recording (the shadow cascades are independent).
- **P1** (streaming hitches: per-texture `endImmediateCommands` fence waits; synchronous `loadSector`) is still
  the felt-spikes fix and is untouched.
- **RT AO polish if it stays**: distance falloff + a strength clamp (`mix(1, ao, 0.6)`) for the heavy canopy
  shading; the upscaler path does not sample the mask; the terrain in the TLAS would fix ground contact.
- **RT sun shadows (step 4)** remains the pass that would *replace* CPU work — needs the terrain.
- The sat-nav pickup (session 49's recipe) is unchanged and paused.


## Session 2026-10-30 (forty-ninth, `glm`, **WIP paused**): the dashboard sat-nav — the render target path works end to end; the quad's screen-space placement is the open question. Owner called a pause mid-hunt (major task), pickup recipe at the bottom.

Owner: *"can we get satnav rendering in the cars and also I think a lot of audio from pause menu and dialogs for car
dealership etc... is missing"*, later *"maybe it works mirror a camera pointing to satnav and render to texture"*.

### What is built (committed, `-gps 1` to enable; default off until it renders)
- `source/game/cinematic/gps_screen.{h,cpp}` — `GpsScreen`: a 512x256 `RenderTarget` (swapchain format, so the 2D
  pipeline's pass stays compatible — the asset viewer's trick), redrawn car-centred north-up from the map data the
  HUD minimap already uses: `VehicleWorldCollision::forEachRoadTriangle` roads, the GPS route polyline, the island
  colour map, a heading arrow (`MapScreen::projectToStage`'s convention). Rate limited (15 frames minimum, then
  8 m moved / route change / 300-frame keep-alive). `GMAssetViewer::drawOffscreen` is the dispatch recipe;
  `GMFreeRide::drawOffscreen` drives it.
- `cinematic_car.{h,cpp}` — the interior's TD_GPS materials (`kGpsShaderGuid 0x7dad16b47eb57b95`) and GPS node
  subtrees captured at load (the load-time hide list now excludes **every** node named "GPS" — there are two:
  interior node 5 parent 1 and node 31 parent 0); `setGpsTexture(renderer, pTexture, uvOffset)` binds the texture
  and writes `UVOFFSET`, then re-shows the subtrees. One attempt per load (`bGpsBound`, no per-frame retry).
- `gm_freeride.{h,cpp}` + `map_screen.h` — the `-gps 0|1` and `-gpsuv u,v,z` flags (default (1,1,3,0)), extent
  getters, the drive/bind block.

### What is proven
- The bind works end to end: `CinematicCar: dash GPS bound, 1 material(s), 2 node(s), uv (...)` in the log; the
  slot type is **`COLOR`** (`assignTexture(material, "COLOR", ...)`; hash 000000524f4c4f43 — the .2dm's single
  slot ships the generic type, "satellitemap" is only the texture's file name; `SATELLITEMAP`/`satellitemap` do
  NOT match).
- The RT renders and samples: a temporary solid-red fill (reverted) bound with no other change produced **no**
  red anywhere on screen in any config — see the negatives below.
- Ruled out: `updateProgram` nulling the program (unknown families only set `S_NBTEXTURES`, eden_renderer.cpp:556);
  node visibility (re-shown, same mechanism the load-time hide uses); frustum culling (the dash is on screen);
  the material program (`TD_GPS vs0/ps0` with `UVOffset` at VS reg 67, ships `UVOFFSET = (1,1,2,0)`).

### The open question: where does the quad go?
- Session 45's sighting — unprojected, it "floats across the scene as a black sail" — plus every red-fill test
  showing nothing points at the **TD_GPS vertex shader positioning the quad in screen space from `UVOffset`**
  (with (1,1) it would span the whole screen: the sail). Our (1,1,3,0)/(1,1,2,0)/(1,1,0,0)/(0,0,2,0) sweeps put
  it somewhere invisible (off-screen / degenerate / clipped depth).
- TDU1's `setGPSTexture` (opentdu1-ref `vehicle/vehicle_base.cpp:729-806`) writes `[0]=ViewportU, [1]=ViewportV,
  [2]=3.0` where (`mng_hud.cpp:1014-1024`) `ViewportU = min(screenWidth * viewportRect.Width * kHudMapTexelSize, 1)`,
  `ViewportV` likewise, `kHudMapTexelSize = 1/256` (`mng_hud.cpp:352`), `kGPSScreenParamZ = 3.0`
  (`vehicle_base.cpp:28`), and `viewportRect` is the Flash HUD minimap clip's (`mmap_lure`) property viewport.
  Those U/V are therefore ~minimap-pixels/256 — small fractions, exact values recoverable from the HUD movie clip.

### Pickup recipe (in order)
1. Compute TDU1's exact U/V: read the `mmap_lure` clip viewport out of TDU1's HUD data (or TDU2's `HUD.vmf`, the
   movie the HUD already loads) and apply the formula above; then `-gpsuv "<u>,<v>,3"` headless:
   `build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-gps -gm freeride
   -skipcine -nopad -fixedtimestep -tp 21796.3,24188.1 -snaproad 150 -gpsdrive "24500,25300,20" -carlod 4 -gps 1
   -gpsuv "<u>,<v>,3" -autoplay "120:press=carcamera;380:shot=C:/temp/gps.ppm" -exitframe 420` (in-car camera; a
   body-hidden variant `-hidenode "BODY_HR;GLASS_F;GLASS_I"` shows the dash from outside). Sweep around the
   computed values if the first guess misses.
2. If the sweep stays empty: dump the TD_GPS VS — the .sob's DXBC goes through
   `source/render/shaders/dxso_translator.cpp`; a temporary token dump of the VS (or reading the tokens around
   constant reg 67) names the law exactly. `IdentifierToString` turns constant names back to text.
3. The owner's suggestion as the fallback: the projector path the wall mirror uses (`ProjectorScene`,
   `setProjectiveTexture`, `EdenPlanarReflections` for the RT + camera pattern) — draw the map onto the dash via
   a projector surface instead of the TD_GPS path. Or, last resort, screen-space: draw the sat-nav as a 2D element
   over the dash like `FreeRideHud::drawMinimap` (not in-world, but shippable).
4. When it renders: flip the `-gps` default back to 1, screenshot for evidence, then the RT content polish
   (zoom levels, rotating mode, destination arrow).

### The audio plan (researched, not started)
- The retail UI bank is `Euro/Bnk/frontend/sounds/fe_snd.bnk` (178 wavs: `valid01/02/04/06`, `back`, `blocked`,
  `toolTip02`, `menuOn01-03/menuOff01`, `shift01`, `move04`, the `Med_*` family — `Med_menu_on_01`,
  `Med_valide_01`, `Med_wrong_02`, `Med_retour_01`, `Med_stop_01`, `Med_cancel_01`... — `in_game_menu_*`, `rd_*`
  radio panel, phone/poker/roulette `S_*`; entry 0 is `sounds.ini`, retail's name registry, `otdu2_bnk_tool
  extract` can dump it). The port never loads it.
- Movies already fire `FSCommand:sndp:<name>` (`flash_player.cpp:1578-1592` FSCommand interception; known cues
  `mopen mclose ttip valid wrong stop back cancel shot`) — **nothing listens**; the pause menu's callback
  (`pause_menu.cpp:714`) logs and drops them. Plan: a `FrontEndSounds` helper (GPSVoice is the pattern: bank +
  name map + bfx **client 7 "FrontEnd"**, unused so far) + a global `sndp:` branch in the Flash player + explicit
  plays in the C++ validate/back paths (the native entry squares bypass the movie scripts, session cline-9) + the
  same 3-4 lines at each native menu's `IA_Menu*` branches (dealer/clothes/estate/tuning/paint/map — no shared
  base class).
- Shop music: retail plays loose wavs per place (`Euro/Bnk/sound/musics/cardealer.wav`, `clothesshop01/02`,
  `gamblinghall`, `hairdresser`); interiors also carry `*_FRONTEND_MUSIC.wav` in their banks. Play on client 8
  "FrontEndMusic" while an `InteriorWalk` is in a shop, stop on exit. Optional: `setGameMode("Menu")` on pause
  (the audiosetup preset that ducks gameplay clients).
- Seller **voice** lines: no DB table found (`_soundfilename` exists only on DBTutoMess, whose lines already play
  via the C_VOICE cinematics; car_dealer.bnk carries no wav/bfx). Candidates to check with `otdu2_bnk_tool list`:
  `voices/<lang>/hs/*.bnk` (124 banks), the unexplained RS/SC path patterns in the exe's voice-path function. If
  nothing identifies as seller speech, record the data gap — no fabricated audio.
## Session 2026-10-30 (Cline, `cline`, twentieth): CamilleKMI's "MAJOR Update" — read, measured, and ported rather than merged

The owner: *"this got a new update can you try and I would say merge the
differences but may break things in our build but the used dealership ui, driving
tess and possibly solar crown cutscene maybe worth digging into"*
(`https://github.com/CamilleKMI/OpenTDU2`, commit `335759a`).

### What the update is

One squashed commit, **2026-10-29, 129 files, +16 811 / −410**. Its message
("lights, night, FX weather, clouds, ocean far fix, car options turn signals +
roof, LOD trees") lists the rendering half; the new *files* are the gameplay
half, and they are exactly the owner's three picks plus one more:

| New mode | File(s) | What it is |
|---|---|---|
| **SHOP** (3) | `source/game/shop/gm_shop.*` (1 344 l) | **the used car dealer UI** — story step 7, Todd Bishop at `ESTOL_B_1275I`: `SHOPSCAR.vmf`, the cursor, the mini menu (Buy / Enter / Data / Compare), the price and car-data panels, the in-car look |
| **TESTCAR** (7) | `source/game/shop/gm_testcar.*` (483 l) | the dealer's test drive: the 120 s HUD countdown, a passenger, the spot's car-park pose |
| **MISSION** (4) | `source/game/mission/{gm_mission,ige_file,cinematic_props,event_reactions}.*` (1 322 l) | **story step 4 "Driving Tess"** — the `TESSEVEN` event, the `ig2/tesseven.ig2` IGE2 script, Tess' passenger reactions, the truck prop |
| **CINE** (0x1f) | `source/game/cine/gm_cine.*` (414 l) | **the story cutscenes at a spot** — kind 1 Meeting Tess, **kind 2 / 3 the Solar Crown exterior / interior** |

Plus the classes they need (`avatar/seated_avatar`, `avatar/ik_offsets`,
`world/road_network`, `freedrive/*`, `input/game_key_map`, `eden_particles`,
`eden_rain`, `eden_neons`, `eden_world_lod`, two headlight shaders) and
`docs/re/story_spec.md` (564 l: the whole story step machine, steps 3..13, and
the shop's data model).

### Why it is not merged

Measured, not guessed:

- `git merge-base HEAD upstream/main` = **`3f67e58`** — the two trees are two
  *parallel evolutions*, not an ancestor/descendant pair. This tree is **269
  commits / 120 308 lines ahead**; upstream's single commit adds 16 811.
- A trial merge (`git merge --no-commit --no-ff upstream/main`, then
  `git merge --abort`) gives **31 unmerged files and 103 conflict hunks**
  (~2 400 lines of ours vs ~1 000 of theirs). `cline` was never touched.
- The shape is mostly *"our file is the superset, upstream added a small block"*
  (`freeride_hud.cpp` 533 vs 90, `intro_cinematic.cpp` 423 vs 85,
  `player_profile.cpp` 394 vs 72) — but `intro_cinematic.cpp`,
  `cinematic_car.cpp`, `fade_freeride.*`, `eden_scene_view.cpp`, `eden_world.*`
  and `flash_player.cpp` (15 hunks) are genuine rewrites on both sides.
- **Both trees implemented the same features independently**: this tree's own
  sessions already did animated windows, driven indicators, headlights at night,
  brake / reverse lamps and the dashboard backlight — which is `335759a`'s
  "car options turn signals + roof" and "lights, night". Merging would put a
  parallel renderer (rain, particles, neons, LOD impostors, ocean, world
  streaming, `flash_player`, `gs_audio`) on top of ours.
- **And a merge would not compile anyway**: the new modes call ~25 spine methods
  this tree does not have (`takeActorMessages`, `addShowCar`, `addSelectable`,
  `setSeatedShowCar`, `Setup::UiMovie`, `getGroundHeight`, `WalkMode::setCursorLabels`,
  `FreeRideHud::setCountdown/setTestDrive`, `VehicleDynamics::takeImpactSpeed`,
  `CinematicCar::applyColor`, `TestDriveRequest`, …). Git resolves *text*
  conflicts, not that gap — the same hand-porting is needed either way, but on a
  tree that is also carrying a half-merged renderer.

**Decision: port feature by feature.** The features are *new files* — no merge
needed: `git checkout upstream/main -- <path>`. Full evidence, the per-file
conflict table, the per-mode spine gap and the staged plan are in
**`docs/re/upstream_335759a.md`**.

### What changed

Stage 0, the story spine every one of the four modes needs (branch
`upstream-335759a-port`, commit `1b187bc`):

- `source/game/gm_base.{h,cpp}` — `GM_Cine` ("CINE", 0x1f), `GM_Mission`
  ("MISSION", 4), `GM_Shop` ("SHOP", 3) and `GM_FreeDrive` are in `eGameMode`
  and in the name table again (the names are the retail ones, so `-gm cine`
  resolves).
- `source/game/player_profile.{h,cpp}` — `CineRequest` + `GetCineRequest()`
  (FUN_00b9a1f0: kind + the story callback), `GetStoryAvatarConfig()`
  (AVT_`<sex>`[_`<ethnic>`]`_Intro` → the story's AVT_`<sex>`), and the money
  transfer: `PendingMoney` / `MoneyTransferTimer` / `MoneyEarned[28]` with
  `ProfileAddMoney` (FUN_00abe0e0), `ProfileStartMoneyTransfer` (FUN_00abd0d0)
  and `ProfileTickMoney` (FUN_00abcf60).
- `source/tdu_instance.{h,cpp}` — the previous-game-mode field
  (DAT_00fd5080 + 0x12c) and `-storystep n` (the step → mode + cine kind table
  of FUN_00b10da0: 0 AVATAR_CHOICE … 3 CINE kind 1, 4 MISSION, 5 CINE kind 2,
  6 CINE kind 3, 7 SHOP, 8 FREERIDE).

### Verified

- `build-cline.ps1` → **exit 0** (87/87 targets, `opentdu2-cline.exe` relinked).
- **The mode names resolve and the fallback is unchanged**: `-gm cine` (lower
  case, so `GetGameModeFromName` is exercised) logs
  `GAME MODE : CINE` then `Game mode 'CINE' is not implemented yet` — the
  pre-existing fallback path, i.e. no behaviour change.
- **No regression in the modes that do exist**: `-gm FREERIDE -skipcine -no3d
  -nopad -exitframe 200` → exit 0 and `GAME MODE : FREERIDE`.
- **`-storystep` works**: `-storystep 5 -no3d -nopad -exitframe 150` → exit 0
  and `GAME MODE : CINE` (step 5 = CINE kind 2, the Solar Crown exterior — the
  mode it selects is right even though the mode itself is not ported yet).
- The upstream fetch was `git fetch https://github.com/CamilleKMI/OpenTDU2
  main:refs/remotes/upstream/main` (no remote added, nothing pushed upstream).

### Not done / next

The port stages (each one builds on its own; §6 of `docs/re/upstream_335759a.md`
has the per-file detail):

1. **Stage 1 — the Solar Crown cutscene (`GMCine`)**: `git checkout upstream/main
   -- source/game/cine` + the `IntroCinematic` additions it uses
   (`ActorMessage` / `collectActorMessages` / `takeActorMessages`,
   `Setup::bExteriorCollision`, `Setup::InteriorBank`, `getGroundHeight`,
   `getGeneral()`, the 4th `loadCar` argument) + `SpotRecord.Position2/Heading2`
   (spots.bin +0x28 / +0x38) and a `ReadSpot(name, out)`. Our `Setup::bSceneAt` +
   `SceneAtPosition/SceneAtHeading` replaces upstream's `placeEntityAt`, and
   `getSurfaceHeight` can back `getGroundHeight`, so kind 1 needs no new
   placement code. Verify `-gm CINE -cinekind 1|2|3` and `-storystep 5`.
2. **Stage 2 — the used car dealer UI (`GMShop`) and its test drive
   (`GMTestCar`)**: `source/game/shop` + the `IntroCinematic` showroom layer
   (`addShowCar` / `addSelectable` / `setSeatedShowCar` / `getSceneBank` /
   `getSceneHashcodes` / `getCinProps` / `Setup::UiMovie` / the GENERAL dialog
   box) + `WalkMode::setCursorLabels` / `setSelectionCallback` + the profile's
   `OwnedCar` / `Garage` / `CurrentGarageSlot` / `LoanCar` / `TestDriveRequest`.
   Our `dealer_menu.cpp` and `IntroCinematic::getShopPosCams` / `addDisplayCar`
   / `focusDisplayCar` already cover the data side.
3. **Stage 3 — Driving Tess (`GMMission` + IGE2)**: the mission files,
   `avatar/seated_avatar`, `avatar/ik_offsets`, `world/road_network`,
   `VehicleDynamics::takeImpactSpeed`, `FreeRideHud::setTarget` + the countdown
   elements, `CinematicCar::applyColor`.

Deliberately **not** in scope: upstream's render / weather / world / input work
(`eden_particles`, `eden_rain`, `eden_neons`, `eden_world_lod`, `freedrive`,
`game_key_map`, the `flash_player` changes) — port one of those only if a mode
turns out to require it.

## Session 2026-10-30 (Cline, `cline`, nineteenth): the garage upgrades bite, and the police awareness decays again

The owner, offering three jobs and letting me pick: *"I want you to work on the
garage with vehicle upgrades, and the stickers please but if this is to much I
want you to improve traffic logic and hopefully fix police logic I will let you
pick which of them tasks you want to do and which you want to bench"*.

**Picked: the vehicle upgrades** (the garage's tuning levels, which were stored
but did nothing — the one documented gap in the garage) **and the police logic**
(a real bug found on the way). **Benched: the stickers** (real decals are the
`DBStickersBanks` banks applied to the model's `USER_TEX` sampler — TD_CarPaint2
s5, paint_spec.md 6 — which needs the sticker art and a visual check; the paint
shop already covers the colour catalogue) **and broad traffic** (the road graph,
junction signals, player collision, near misses and night lights are already in;
the rest — lane changes, the real zone lookup, horns, damage — are their own
features). Both benched items are named in "Not done / next" below.

### What changed
- `source/physics/vehicle_data.{h,cpp}` — `ApplyTuningToSetup( setup, accel, braking, speed )`,
  the port's modulation of a `VehicleSetup` from the tuning levels [INF: retail's
  tuning tables are in the encrypted band]. Accel scales the engine torque curve
  (+8 %/level); braking the tyres' braking friction (+10 %/level); speed the
  engine's top-end torque (+10 % above 70 % of the rev range) with the matching
  RPM band. Levels clamp to 0..3 and a zero level leaves its value untouched.
- `source/physics/vehicle_dynamics.h` — `setParams` (assign the parameters
  without resetting the running state: the live upgrade; only the fields the
  tuning writes differ, the mass / inertia / geometry / gear ratios derived in
  `setup()` stay).
- `source/game/freeride/gm_freeride.{h,cpp}` — `setupVehicle` applies the levels
  to a *copy* of the base setup before `vehicle.setup`; `applyTuning()`
  re-applies them to the live car (`vehicle.setParams`) and is called when the
  player leaves a shop interior ("back in the car"). The base `setup` member is
  never modified, so a re-apply cannot compound.
- `source/game/freeride/chase_mode.cpp` — `addViolation` now enters `Noticed`
  (Clean -> Noticed). `Noticed` was unreachable, so the decay branch in `tick()`
  (guarded on `state == Noticed`) never ran: the awareness never decayed and the
  tailing meter stuck at its last value until the next hit.
- `source/game/freeride/tuning_menu.h`, `docs/re/houses_spec.md` — the "hooking
  them into the physics is not done yet" comments/spec updated to the mapping
  and the measured numbers.

### Verified (headless, `-no3d -vehtest`, fresh userdata profiles; the car is the California, `_id_car` 387)
    build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata <profile> ^
        -gm FREERIDE -skipcine -no3d -nopad -vehtest -exitframe 200
- `car_tuning = 387:3,3,3` vs none: **0-100 4.52 → 3.27 s**, 0-200 13.87 → 9.03 s,
  **402 m 12.60 → 10.93 s**, **top 309.7 → 337.6 km/h**, **100-0 34.4 → 31.9 m**.
  The axes isolated: speed-only (`0,0,3`) top 309.7 → 337.0; braking-only
  (`0,3,0`) 100-0 34.4 → 31.3 m.
- The live re-apply: `-tp 12605.4,16698.1 -snaproad 150` -> **A** into ETUN_A
  (`itun_a.bnk`, 3 selectables) -> **○/B** out logs
  `FREERIDE: 'California' tuning applied live (accel 3, braking 3, speed 3)` then
  `FREERIDE: back in the car`.
- Police: `-policeviol` (a 55 point violation at frame 150) logs
  `ChaseMode: attention lost` ~12.5 s later (3 s `DecayDelay` + 55 / 6 per
  second) — the old code could never reach that branch. `-police` still logs
  `ChaseMode: chase started, 2 cops (Dodge_Charger)` (no regression).

### Ruled out (with the test that ruled it out)
- **Braking via the brake torque** (`MaxBrakingAction`): the wheels lock (front
  slip −1.000 by t 0.5 s) and the port's ABS caps the torque, so 100-0 got
  *worse* (34.4 → 40.6 m). The tyre braking friction is the lever.
- **Speed via the RPM band alone** (`0,0,3`: limiter 8500 → 9775 rpm): top speed
  unchanged (309.7 → 309.8 km/h); and **via a taller final drive** (4.44 → 3.86):
  top speed slightly *down* (308.2) and 0-100 slower. The car is drag / torque
  limited, not rev limited — so speed scales the top-end torque instead.

### Not done / next
- **Stickers (benched)**: real decals = the `DBStickersBanks` banks applied to
  the car model's `USER_TEX` sampler (TD_CarPaint2 s5); `car_shop.cpp` already
  logs `DBStickersBanks` under `-logshops`. Needs the sticker art and a visual
  check. `paint_menu.*` stays the colour catalogue (`DBCarsAndColors`).
- **Traffic (benched)**: lane changes / overtaking, the real per-node zone
  lookup (`cruisingzones.bin`), horns / audio, traffic damage, Hawaii / Casino
  traffic — `docs/re/traffic_spec.md` "Not done".
- The tuning **price** is still the [INF] $12k × next level (DBStickersPricing
  names no tuning actions). The retail tuning *tables* (which would replace the
  [INF] mapping) are encrypted.
- The live re-apply is exercised by leaving the shop; a purchase-while-open was
  not scripted (the same code path, `applyTuning()`, runs either way).

## Session 2026-10-30 (Cline, `cline`, eighteenth): the map icon of a gas station — retail has none, so the port draws one

The owner: *"do we have an icon for the petrol stations for in map? if not we can
use Image AI tools to generate 1"*.

**Retail has no petrol station icon.** The map's icon sprite `/pack/point/spot`
carries **102 labels** (`-logmapspots`): `cardealer`, `tuning`, `carwash`,
`carpaint`, `clothes`, `club`, `estates`, `home`, `photograph`, `rschool`,
`stolenCar`, `surgery`, `airport`, `gamblinghall`, `watches`, `drivein`,
`auctionhall`, `bikedealer`, `carrental`, `forsale`, the whole challenge set
(`asphalt` … `eliminator_m_dlc`), `multi`, `hotspots`, `friend`, `player` … and
**not one station / fuel / petrol / gas label**. No bank holds station icon art
either (the asset index has none). So the feature was cut before an icon was made,
like the rest of it — and the `gasStation` string the executable references is a
*type* name, not a movie label.

**So the port draws it** (`MapScreen::drawFuelIcons`, `source/game/map_screen.cpp`,
called from `drawIsland` over the island and under the route):

- the art is **`<mods>/icons/fuel.png`** — any size, aspect kept, so an icon
  generated with any image tool drops straight in (the mods overlay resolves it);
- without that file a small **built-in pump glyph** is drawn instead (white body,
  amber window, dark nozzle and base), so a station is visible with no assets;
- one icon per `fuel` prop (the positions come from `mod_props` through
  `setFuelStations`); `-nofuelicons` turns it off.

Verified: the icon loads and projects —
`MapScreen: fuel station icon '…\mods\icons\fuel.png' (64x64)` and
`MapScreen: 1 fuel station icon(s) on the map (icons/fuel.png), first at (500, 323)`.
**Not yet confirmed on screen**: a pixel probe of the shot found no icon colour at
that spot, and the 1:4 view of the map is too small to tell — the map's
stage→screen mapping (the route draws at `projectToStage() - 120` in the same
canvas) needs one visual check before this can be called done. Next step: open the
map at 1:1 with a station in view and look at (500, 323) — if the icon is elsewhere,
the offset is the thing to fix (the glyph is drawn the same way as the route, which
is known good, so a wrong offset would move both).

## Session 2026-10-30 (Cline, `cline`, seventeenth): the HUD fuel gauge

The owner: *"as for no HUD fuel gauge could you add that"*. Done natively, because
neither retail side has one:

- **The car has no fuel needle.** `-dumpcar` on the California lists `D_ND_RPM` /
  `ND_RPM_1/2` / `D_ND_SPD` / `ND_SPD_1/2` and nothing else — the dashboard carries
  speed and revs only.
- **The HUD movie has no fuel element** (there is no `/:` fuel global in
  `docs/re/ps3/flash_globals.txt`), so there is nothing to drive in the movie.

`FreeRideHud::drawFuel` (`source/game/freeride/freeride_hud.cpp`, called from
`draw()` next to `drawZone`) therefore draws a small bar in the HUD's own style,
**beside the zone badge** at the top right (x 792..1012, y 112..136 — the zone bar
starts at x 1022, the gauge cluster is bottom right, so nothing overlaps): the
retail label (`LocStr(5821, "Fuel")`), the fill, the litres and the km left. The
fill is green, amber under a quarter, red under a tenth (and at 0 the engine is cut
and the Breakdown line shows). `-nofuelhud` hides it; the values come from
`GMFreeRide`'s per-frame `hud.setFuel(tank.Litres, tank.Capacity, tank.RangeKm,
tank.Grade)`.

Verified by pixel (headless, `-no3d`, a shot at frame 200): at `-fuel 20` (26 % of
the 78 L tank) the bar's fill reads **(90, 200, 90)** — the green — and at `-fuel 2`
(2.6 %) it reads **(235, 110, 30)** — the amber.

### Not done

- The grades still have no effect on performance, and the prices are the port's own.
- A needle on the *dashboard* would need the retail needle driving (the older
  "speedometer needle" item: `ND_SPD_1/2` / `ND_RPM_1/2` are still at their data
  rest pose).

## Session 2026-10-30 (Cline, `cline`, sixteenth): the fuel is wired — the tank burns, the pump screen fills it at home and at our own station

The owner: *"we can do a and b for now and then will have some models made up for
extra locations"* — (a) the fuel feature at the **house garage pump** and (b) our
**own station** on the map, with future locations being one line of `props.txt`.

### (a) Fuel, and the pump screen (retail strings, by id)

`source/game/freeride/fuel.{h,cpp}`: the tank (`Capacity` from `_gas_tank_size`,
`Grade`, `Consumption`, `RangeKm`), the consumption law (`_gas_cons_min` at 0,
`avg` at 90, `max` at 200 km/h, `avg` where a row leaves `max` 0 `[INF: the retail
law is not decoded]`), `BurnFuel` on the odometer delta, and the prices (basic
$1.49 / v-power $1.79 `[INF: not in the data]`).

`source/game/freeride/fuel_menu.{h,cpp}`: the pump screen, every string of it read
from the **game's own localization** with `LocStr` — 1635 "Gas station", 1636/1637
"Shell basic" / "Shell v-power", 1640/1641 the "another grade in this tank /
replace?" confirm, 1642 "Fill", 1643 "Bill", 1644 "Breakdown", 1645 the SHELL-pays
line. Two grade rows with their bill, the tank state, the money.

Refuel points, both opening it: **your house garage**
(`interior.isHouse() && interior.isInGarage()` — retail's only pump model is the
`gph_l2d` garage one, session 15 §4.1) and **any station from the mods overlay** (a
`fuel` prop). Empty: the throttle is cut and "Breakdown" shows. `-fuel <l>`,
`-logfuel`, `-nofuel`; the tank is saved in `profile.sav` (`fuel_litres` /
`fuel_grade` — the retail save has no such field, so it is the port's own `[INF]`).

### (b) Our own station is a fuel station

`mod_props` gained the `fuel` column (`props.txt`), `findFuelStation()` and the
prompt: stopped within 35 m of FUEL1 the HUD offers `Fill: FUEL1` and validate opens
the screen. Extra locations = one line each, and the model can be swapped for a
Blender one without touching the code.

### Verified headless

- tank from the DB: `Fuel: 'California' tank 78.0 L, consumption 0.1/13.1/0.0 l/100 km`
- burn: `-tp 12605,16698 -fuel 0.06 -autoplay "60:hold=accel"` — the litres fall and
  the consumption follows the speed curve (`0.1 → 4.1 → 11.7 l/100 km`)
- empty: `-fuel 0.005` → `Fuel: tank empty after 95 m - Breakdown`
- the station: `-tp 11660,15866 -fuel 5 -autoplay "120:press=validate;300:press=validate"`
  → `ModProps: 'FUEL1' … shader TD_Gouraud, fuel station`, `Fuel: pump screen at
  FUEL1 (tank 5.0 / 78.0 L, grade 0)`, `Fuel: filled 73.0 L Shell basic for $108.77`

### Not done / next

- No HUD gauge (there is no `/:` global for fuel in the movie; a native readout is
  the option), and the grades do nothing to performance yet.
- The house-garage prompt is on **any** owned house garage; the pump *model* only
  exists in `gph_l2d`'s (the others would need a model — the owner's Blender work).

## Session 2026-10-30 (Cline, `cline`, fifteenth): our own petrol station — the Route A prop loader works, and the asset index finds the retail pumps

The owner: *"there's car washes, do they not have petrol stations at them? … we have
an asset tool, maybe we can check the textures"* (with a r/tdu3 post showing a TDU2
petrol station). Two answers came out of it: **the car washes have no pumps**, and
**the retail pumps are elsewhere in the data** — found with the port's own asset
index, exactly as he suggested.

### 1. The pumps in the data (docs/re/fuel_gas_station_spec.md §4.1)

File names are not the whole story: the index
(`-gm ASSETVIEWER -buildindex`, 9,452 banks / 153,690 assets) searches **inside**
the banks, and Eden is French (`pompe` = pump):

- **`gaspump_d.2db` exists in exactly one bank of the whole game**:
  `euro/bnk/interior/gph_l2d.bnk` — a **player-house garage** interior
  (`Euro/spots/playhous/ph_l2d/gph_l2d/maps/gaspump_d.2db`). So the *functional* gas
  pump of retail is **in your house garage** — where the `Gas station` / `Shell
  basic` / `Shell v-power` / `Fill` / `Bill` / `Breakdown` strings (§1) and the
  `gasStation` handler (§3) belong: you refuel at home, not on the road.
- The **racing schools** (`islands/<island>/exterior/ers_b.bnk`, both islands, and
  the offroad cinematic `stb_b4.bnk`) carry `d_pompe_gas` / `g_pompe_gas` /
  `d_old_gas` / `g_old_gas` — decorative pumps.
- 21 `gph_*` garages exist and only `gph_l2d` has the pump: the feature was cut
  after one house got one `[INF]`.
- The car washes (`ecw_a.bnk`, `Euro/spots/carwash/cw_a/…`) have no pump asset at
  all — the owner's guess was reasonable, the data says no.
- Verified: `-gm ASSET_VIEWER -bank euro/bnk/interior/gph_l2d.bnk` previews the
  garage (3 models, 45 meshes, 32,319 vertices); `-gm WORLD_VIEWER -island ibiza
  -spot ERS_B` streams the racing school with its pumps.

### 2. Route A is implemented: `source/game/spots/mod_props.*`

OBJ + MTL + PNG in the mods overlay, placed by `<mods>/props.txt`
(`island name x y z heading [scale] [shader]`, default shader `TD_Gouraud` = Gouraud
N·L + one colour texture), one draw per `usemtl`, textures through
`render/image_file.*` + `TextureCache::createFromRGBA`, mesh mirrored like the
game's own 3D data. Wired into FREERIDE and WORLD_VIEWER, `-nomodprops` off switch.
`docs/re/modding.md` §3 documents it; the owner's Blender model drops straight in.

Verified with a station of our own (a canopy + shop + two pumps + a price sign, 132
triangles, four PNGs, generated by `_make_station_asset.py` — the owner's Blender
export replaces it):

    ModProps: 'FUEL1' on ibiza at (11660.0, 5.3, 15866.0): 6 materials, 132 triangles, bounds (-9.1, 0.0, -6.2)-(7.0, 5.7, 9.0), shader TD_Gouraud
    Eden material 'FUEL1_wall' (TD_Gouraud vs0/ps0) idx 72 slots 1 blend 2/1 cull 3 bucket 0x11: props/fuel_wall.png

and the A/B (same camera pose, top-down, `-nomodprops` vs with it): **28,650 changed
pixels**, mean |dR| 4.65. Two earlier shots looked empty for a mundane reason worth
recording: `y` in `props.txt` is the ground height and is not derived, so the first
test was **buried** (y 10.5 where the ground is 13.5 — read from the car's own spawn
`DRIVE pos` line), and the second was inside a **tree**.

### 3. Found on the way: the mods overlay replaced a folder instead of merging it

`GSFile::listFolder` resolved the folder through `resolvePath`, which returns the
overlay copy when the folder itself is modded — so it listed **only** the overlay's
files: one file in `<mods>/Euro/Bnk/islands/ibiza/exterior` hid all 178 game banks
of that folder, and the asset index walk wrote **1 bank instead of 9,452** (it
overwrote the 30 MB index with a 16 KB one — rebuilt with `-nomods`, then again
after the fix). Now the overlay is listed first (same-named file wins) and the game
folder adds the rest (`gs_file.cpp`).

### Not done / next

- The station's placement is a test position (11660, 5.3, 15866, next to racing
  school C); where the real stations go is the owner's call — and `gph_l2d`'s
  garage pump is the retail-faithful answer (fuel at home).
- The prop material has no lightmap and no dynamic sun (TD_Gouraud is vertex-lit);
  Route B (Eden `.3dd/.3dg/.2dm/.2db` + a BNK packer) is still the "native" route.
- The fuel state itself (`_gas_tank_size` / `_gas_cons_*` are read) is not wired to
  anything yet.

## Session 2026-10-30 (Cline, `cline`, fourteenth): a building we place ourselves — the mods overlay carries a new spot and its own bank (verified in game)

The owner: *"What about we make a petrol station in blender and we can use the same
design maybe change its name and make a few textures and have a real petrol station
I mean it's our code we should be able to do that?"* — yes. The **placement** half
needs no engine change at all, and that is now proven in game; the **asset** half is
the work (two routes, one of which is a small loader).

### Verified: a new spot + its own bank, through the overlay

Two files in the mods folder, nothing else:

| file | what |
|---|---|
| `mods/Euro/Bnk/islands/ibiza/level/spots.bin` | the game's own list + one 0x110-byte record |
| `mods/Euro/Bnk/islands/ibiza/exterior/egas_a.bnk` | the building (a stand-in copy of the tuning-shop bank) |

New tool `tools/mod/add_spot.py` writes the record (name, x/z, height from the
nearest spot, heading, category byte, and an optional bank to copy in) — it *reads*
the user's own `spots.bin` and never ships one. Run:

    python tools/mod/add_spot.py --name EGAS_A --x 12665 --z 16698 --heading 0 --category 4 \
           --bank C:\TDU2-Gamedata\TestDrive2\Euro\Bnk\islands\ibiza\exterior\etun_a.bnk

Log (`-gm WORLD_VIEWER -island ibiza -spot EGAS_A`):

    Mod overlay: 'Euro/Bnk/islands/ibiza/level/spots.bin' -> '<mods>/...'        (gs_file.cpp:227)
    MapScreen: 75 spots with an icon (of 80 in spots.bin)      spot 'EGAS_A' icon 'tuning' at (12665, 16698)
    Mod overlay: 'Euro/Bnk/islands/ibiza/exterior/egas_a.bnk' -> '<mods>/...'
    SpotExterior: ...egas_a.bnk: 2 set models, 0 props, local bounds (-7.3, -0.4, -17.7) - (36.0, 10.8, 23.9)
    SpotExteriors: EGAS_A at (12665, 16698): 2 models, 1 placements

Evidence `docs/images/mod-station-placed-s14.webp`: **two copies of the tuning-shop
bank 60 m apart where retail has one** (the left building is the real `ETUN_A`, the
right one is our `EGAS_A`) — `spots.bin: 80 spots` in the viewer panel.

### What an *own* building needs — `docs/re/modding.md`

- **Route A (fastest, all our code)**: Blender → OBJ/glTF + PNG, loaded from the
  overlay and drawn with the Eden renderer (needs a small loader + a placement list;
  `render/image_file.*` already reads PNG/DDS).
- **Route B (most native)**: Blender → `.3dd`/`.3dg`/`.2dm`/`.2db` → packed `.bnk`,
  all four documented in `formats.md` and already *read* by the port, so the loop can
  be validated by round-tripping a retail bank. The two missing pieces are a **BNK
  packer** (`tools/bnk_tool` only lists/extracts today) and an **exporter**.
- The doc also fixes the rules: never write into the game folder, never ship game
  data in the repo, our own brand/art (no Shell livery), assets under the project's
  licence.

### Not done / next

- No mesh of our own yet — the demo bank is a retail copy, used only to prove the
  placement path.
- Order: (1) the Route A loader, (2) the BNK packer, (3) the Eden-format exporter;
  then collision (boxes, not a Havok `.shk`), LOD, a second station, the fuel state
  (`docs/re/fuel_gas_station_spec.md` §5).

## Session 2026-10-30 (Cline, `cline`, thirteenth): the community's physics thread read back — and the gas station it names is in the data, not an exe patch

The owner, with a screenshot of a Discord thread (`Xarlith` / `Beeefy` /
`.galopin`): *"this screenshot … is very important and yes will help us with physics
and maybe other missed content"*. The thread is a **data mod** (`physics.cpr` +
reworked vehicle statistics) whose author names, in order: real-world car data,
**Pacejka formulas** (to kill snap-oversteer), **traction control removed in Hardcore
mode**, **tweaked angular damping**, the **speed-factor slider** (hardcore = no TC +
full lock at any speed), flipping reduced via `physics.cpr`, and the one thing they
say *needs an exe patch*: **"the Gas Station reimplementation, parts of it exist but
are disabled"**.

### The physics half — `docs/re/physics_community_notes.md` (new)

Claim by claim against the port. The two that matter:

- **"removed traction control in Hardcore mode" / "tweaking angular damping"** — the
  port has the whole aid machinery (`AidLevel` 0 SECURE / 1 SPORT / 2 HYPERSPORT,
  `bTractionControl`, the ABS/TC slip-ratio curves, the three damping curves) but it
  is **frozen at SECURE**: `-aids sport|hypersport` is load-time only
  (`vehicle_data.cpp:408`, "the runtime aid switch is not read") and
  `CurveAngularDamping = SecureAngularDamping` for every car
  (`vehicle_data.cpp:442`). Spec §8 already lists it ("Aid level of the player (game
  option, game car +0x40)"). The lead is `/:DrivingAssistance` — `pc_site` 14038609
  (`0xD63BD1`), **readable on PC** — plus the readable aid tables
  (`ABSCONTROLACTIVATION` `0xd6cb61`, `ABS_Slip_Ratio_HYPERSPORT` `0xe18b16`).
- **"Pacejka formulas → no snap-oversteer"** — the same target as the port's #1
  handling item, the missing load sensitivity of the Pacejka `D` (`CLAUDE.md` physics
  item 4, `REA_ASSESSMENT.md` §5.1).

Their SecuROM blocker ("some physics variables … are stored in the binary, SecuROM
protects the binary") is the port's *documented route*: the PS3 EBOOT has that code
in the clear. Their "you'd have to reimplement the Havok physics side" is not our
problem either — TDU2 drives its cars with Eden's `edVehicle`, not the Havok vehicle
SDK (spec §1.1).

### The content half — `docs/re/fuel_gas_station_spec.md` (new)

The gas station is **real cut content, and it is all in the data**:

- the **entire UI text** is in the retail `db_us.cpr` (one contiguous block at
  `0x17ae6`, ids 1635..1645): "Gas station", "Shell basic", "Shell v-power", "Select
  basic gasoline", "Select special V-POWER gasoline for better performances!",
  "Currently, there is another gas type in this tank.", "Replace with this gas type
  ?", "Fill", "Bill", "Breakdown", "As you don't have enough money, SHELL is glad to
  pay the bill..." — two grades, one grade per tank, a bill, a breakdown;
- **every car carries its tank**: `DBCarPhysicsData` `_gas_cons_avg/max/min` and
  `_gas_tank_size` (DB row +252..+264), in the shipped `db_data.cpr` — **measured
  now**: 279 cars, tank 12.4..133 L, the California's **78.0 L** (the real car's
  tank), consumption 0.1..30 l/100 km with `max` left 0 on many rows;
- a **`gasStation` handler**: the string is in the readable PC exe (`pc_site`
  11713384 / `0xB2B468`) and on PS3 `func_00633CD0` (dumped, `_g_gasstation.cpp`: an
  object at `this+0x54`, a byte `+0x170` and an int `+0x190` compared with `+0x404`,
  producing a gas type 0/1/2 — the "another grade in the tank?" test);
- **but no station was ever placed**: the `-logspots` census is 79 spots on Ibiza and
  144 on Hawai with no gas station, and no `*gas*` / `*fuel*` / `*shell*` bank exists
  — the feature was cut at the world stage, which is why the community believes it
  needs an exe patch. It does not.

### What changed in the code

- `source/database/gs_database.{h,cpp}` — `CarPhysicsData` gained `GasTankSize` /
  `GasConsMin` / `GasConsAvg` / `GasConsMax`, read in `findCarPhysics`.
- `source/physics/vehicle_data.cpp` — `-dumpcars` now prints `gas min/avg/max tank`
  per row (that is how the survey above was measured).

### Verified

- `build-cline\bin\opentdu2-cline.exe … -gm FREERIDE -skipcine -no3d -nopad
  -fixedtimestep -dumpcars -exitframe 150` — 279 `CARS` lines, e.g.
  `CARS 'California' … gas 0.1/13.1/0.0 tank 78.0`, `'Veyron_Sang_Bleu' … tank 100.0`;
  build exit 0, `stopped after 4.6 s: 600 beats, 0 stalls`.
- `otdu2_bnk_tool.exe xmb db_data.cpr` — the shipped schema really has the four
  fields at +252/+256/+260/+264; `decrypt db_us.cpr` + a byte search finds the whole
  gas-station block at `0x17ae6`.
- `-logspots` on both islands: Ibiza 79 / Hawai 144 spots, no gas station.

### Not done / next

- Fuel is **not implemented** — the spec's §5 lists what it needs (state, a place to
  refuel, the HUD gauge check, the save decision); retail has no station, so where
  they go is a decision for the owner.
- The aid level ("Hardcore") is the cheapest physics win and is unstarted; the
  Pacejka load sensitivity is unchanged.

## Session 2026-10-30 (Cline, `cline`, twelfth): the workspace reset tested — the swapped-in clone could not build until its build dir was started over

The owner: *"reset is done please test"*. The GitHub-clone reset of WORKSPACES.md
(the hub re-cloned as `C:\OpenTDU2-win`, the two live trees staged as `-cline2` /
`-glm2` and swapped by hand while the agent is idle) **is** done for this tree:
`C:\OpenTDU2-cline` is a fresh clone of `origin`
(`danny199012/Open-TestDrive-Unlimited-2-`, branch `cline`; `git reflog` = clone →
`checkout cline`), `third_party/dxvk` is the vendored subset (the repo has **no**
submodules), and the previous session's uncommitted island-badge diagnostic in
`source/game/pause_menu.cpp` survived the swap (as `migrate-to-github-clones.ps1`
promises).

### Found: a renamed tree cannot build (fixed)

`build-cline\` had been configured **before** the swap, in the staged folder:

    CMAKE_HOME_DIRECTORY:INTERNAL=C:/OpenTDU2-cline2
    CMAKE_CACHEFILE_DIR:INTERNAL=c:/OpenTDU2-cline2/build-cline

so the build failed with `[code=1] cmake.verify_globs`, and `-Reconfigure` failed
too — CMake refuses to reuse a cache whose source path moved:

    CMake Error: The current CMakeCache.txt directory C:/OpenTDU2-cline/build-cline/CMakeCache.txt
      is different than the directory c:/OpenTDU2-cline2/build-cline where CMakeCache.txt was created
    CMake Error: The source "C:/OpenTDU2-cline/CMakeLists.txt" does not match the source
      "C:/OpenTDU2-cline2/CMakeLists.txt" used to generate cache.

`build.ps1` (and so `build-cline.ps1` / `build-glm.ps1` / `build-hub.ps1`) now
reads `CMAKE_HOME_DIRECTORY` from an existing cache and, when it names another
folder, warns, **removes the stale build dir** and configures fresh; if the dir
cannot be removed it says so and exits 1 (a running `opentdu2-<branch>.exe` is the
only thing that locks it). This is the one step the migration script could not do:
it builds each tree while it is still staged under its old name.

### Verified (headless, rebuilt `build-cline\bin\opentdu2-cline.exe`, 713 targets, exit 0)

- 2D/UI: `… -gm FREERIDE -skipcine -no3d -nopad -fixedtimestep -logpause -autoplay "200:press=map;420:shot=…/_reset_map.ppm" -exitframe 460`
  — FREERIDE weather '14', the map streams (74 spot icons, home icon), screenshot written, `stopped after 6.7 s: 1840 beats, 0 stalls`.
- 3D, the newest feature: `… -gm FREERIDE -skipcine -nopad -fixedtimestep -night -carlights 1 -logcarlights -autoplay "250:shot=…/_reset_night.ppm" -exitframe 400`
  — weather '00', `CarLights: night 1 headlights 1 indicator -1 brake 0 reverse 0 dash 1 windows 1.00`, screenshot
  written, `stopped after 10.5 s: 1600 beats, 0 stalls`; the frame is 80 % lit (mean 108/255) — the 3D world really renders.
- Pause menu: `… -no3d -logpause -autoplay "200:press=pause;600:shot=…/_reset_pause.ppm" -exitframe 700` — the movie
  opens and the diagnostic dumps: `/pause_menu/data_extd/map/island` = 35 frames, children `location` (1),
  `i2` (53), `i1` (53), all at frame 0 — i.e. the badge art really does live inside the 53-frame `i1` / `i2` clips.
- `launch\resolve_exe.bat` run from the repo root sets `OTDU_EXE=build-cline\bin\opentdu2-cline.exe`.
- Only the known warnings: Havok `packfile magic not found` (absent `Sector-*.shk`) and
  `attachMovie: no export 'specificHelp'`. No device lost, no stall, no `[perf]` anomaly.

### Not done / next

- `C:\OpenTDU2-glm2` will hit the same wall when GLM swaps it in only if a build dir is moved with it — it has no
  `build-glm\` yet, so it configures fresh; `build.ps1` covers it either way.
- The island badge itself (`docs/SURVEY.md` item 10) is untouched: the `-logpause` dump above is the previous
  session's uncommitted work, still uncommitted and unread — the next step is to read `i1` / `i2`'s frame labels
  from that dump and point `refreshEntries()`'s `gotoLabel` at the right one.

## Session 2026-10-30 (forty-eighth, `glm`): synced with the hub — the near plane, the outro stream centre, the camera-path lookahead in; both trees' newest work kept whole

- `3b172a7` committed the real-estate session first (it was the only glm-only content: everything already
  committed on `glm` was merged at the hub), then `git merge win/win` (`67052b8`): the hub's 19 commits — the
  in-car / close cinematic **0.05 m near plane** (the meeting_tess black sail + the garage pan tear,
  `-forcenear`), the **C_FIRST_OUTRO stream centre read after `outro.locate()`** (the outro drove over the bare
  island LOD), the **intro camera-path lookahead** (`-cinelookahead`, the sea-sweep pop-in), the
  instance-draw-distance diagnosis (`-cineradius` / `-instancedistmul`), the borrowed-texture lifetime fix + the
  outro wheel roll, the car lights / animated windows / night dashboard, the radio stations, the pause-menu
  locks, the watchdog, the x360 cut-content tools.
- Three conflicts, all kept whole: `docs/STATE.md` (both newest blocks: the cline 11-7 + win 48-part entries
  above glm 47/46), README's FREERIDE row (both feature sets in one row), and **`gm_freeride.cpp`'s `enter()`** —
  the only hunks-overlap spot in the merge: the `-home` block now sits before the hub's locate()-then-streamCentre
  order, so the stream centre still follows a `-home` spawn (`intro_cinematic.cpp` auto-merged; its hunks are
  disjoint — the hub changed tick/updateView, glm's session changed load/unload/draw/loadBankModels).
- Verified headless (clean configure + 289 targets, exe `opentdu2-glm.exe`): plain `-tp` freeride enters clean
  (no device lost, no invalid imageViews); `-home` resolves the saved home house `EPH_L3DI_5067I` to its entrance
  (`gm_freeride.cpp:217`); the house flow runs `outro.locate()` → C_FIRST_OUTRO → DRIVE (the car 350 m out of the
  garage, 0 stalls); `-night -carlights 1 -logcarlights` — weather '00' (the darkest preset), 4 lamp + 2
  dashboard materials found, and the night shot reads: street lamps lit, tail lamps on, HUD / GAUGES / minimap /
  zone badge drawn (`docs/images/merge-night-s48.webp`).

## Session 2026-10-30 (Cline, `cline`, eleventh): the car lights, the animated windows, the night dashboard; the sat-nav quad is mapped

The owner: *"I Know your vision is spent so for what i'm asking I may have to be your eyes just ask me each run
so I want Headlights, Indicators, Windows also satnav (should show incar view in some cars) as for windows when
press left then down they don't seem to animate it's what i want implementing thanks. As for headlights not sure
if work so you may need to spawn night time also dash boards don't seem to have lighting"*.

### 1. What the car actually has (found headless, no eyes needed)

`-dumpcar` prints the node tree; a new **`-logcarlights`** prints every lamp / dashboard / sat-nav material with
its shader program, its texture slots, its shader constants and its `.2DM` parameters. On the California
(`vehicules/high/california.bnk`):

| what | node(s) | material | shader | the switch |
|---|---|---|---|---|
| head / tail lamps | `LGT_FL`, `LGT_FR`, `LGT_RL`, `LGT_RR` | `LGT_FL` / `LGT_FR` / `LGT_RL` / `LGT_RR` | `TD_CarPaint_NM` | slot 0 = `california_lgt_of`, slot 5 = **`california_lgt_on`** bound to the shader's own `gTexLightTexSampler`; the material's **`MATPARAM`** |
| headlight pool | `??EADLI` (`HEADLIGHT`) | `headlight` | `TD_ShadowBox` | `headlightcaustics`; **still skipped** (`-shadowboxes` draws it, the projection constants are not implemented) |
| dashboard / dials | `HUD`, `ND_SPD`, `ND_RPM` (interior) | `HUD`, `HUD_01` | `TD_DashBoard` | `IlumParam`, **`IlumColor`**, **`HeadlightsIntensity`** |
| needles | `ND_SPD_1/2`, `ND_RPM_1/2`, `D_ND_SPD`, `D_ND_RPM` | `HUD` | `TD_DashBoard` | not driven yet (the older "speedometer needle" item) |
| sat-nav | `GPS` (interior, a mesh at the root **and** under `COCK_HR`) | `GPS` | `TD_GPS` | one slot, `satellitemap`, plus a `UVOFFSET` |

`MATPARAM` is the lamp intensity: the interior **`Lamp`** material, a lamp that is always lit, ships
`MATPARAM = (1, 1, 1, 5)` while the four exterior lamps ship `(0.2, 1, 0, 0)` [INF, but the brake-lamp pixel test
below settles it]. The dashboard ships `HEADLIGHTSINTENSITY = 0` — and `TD_DashBoard` multiplies its whole
backlight term by it, which is exactly why "dashboards don't have lighting".

### 2. What was implemented

`source/game/cinematic/cinematic_car.{h,cpp}`:

- **`updateWindows(dt)`** — `setWindowsOpen()` now only *requests* the state; the glass is blended from
  `RaisedRest` to `LoweredRest` over `kWndAnimDuration` = 1.2 s [INF: `FUN_00c66150(f)` takes a 0..1 factor but
  the readable code has no duration]. The two poses differ by a translation **and** a small rotation (WND_L2 is
  turned ~17° about Z), so the blend decomposes both matrices into (rotation, translation, scale), slerps and
  rebuilds (`BlendRigid`, with local `QuaternionFromMatrix` / `Slerp` — the math header has neither).
  `updateWindows` is called from `GMFreeRide`, `GMFirstWalkMode` (before `updateDoor`, the door pose is model
  space and must win while it moves) and `IntroCinematic::tick`.
- **The light driver** — `setHeadlights`, `setIndicator(side)`, `setBrakeLights`, `setReverseLights`,
  `updateLights(dt)`. The four lamp materials are captured at load with their `MATPARAM`; `updateLights` writes
  `MATPARAM.x = 1` for a lit lamp and back to the shipped value when it goes out. The indicator blinks its own
  side's pair (FL+RL left, FR+RR right) at a 1 s period [INF].
- **`setDashboardLit(bOn)`** — writes `ILUMCOLOR` (warm amber) **and** `HEADLIGHTSINTENSITY = 1` on `HUD` /
  `HUD_01`, restoring both at load values when off.
- `source/game/freeride/gm_freeride.cpp`: the in-car menu's `CME_Left` / `CME_Right` entries now drive the
  indicators (the old "indicators are not implemented" warning is gone); a per-frame block after `vehicle.step`
  sets headlights / dashboard from the weather hour (`< 6 or >= 20`, the traffic rule), the brake lights from
  `Brake` / the handbrake and the reverse lights from `getGear() < 0`.
- New flags: **`-night`** (the darkest `game_weathers` preset of the island — the names are times, so this is
  the "spawn night time" the owner asked for), **`-carlights 0|1`** (force the state), **`-logcarlights`** (the
  material dump at load, then one state line a second).



### 3. Verified headless (the owner's eyes were not needed for these)

- Windows: `-autoplay "200:press=accleft;400:press=accdown"` → `CarLights: ... windows 1.00` … `0.64` … `0.00`.
- `-night` → `FREERIDE: weather '00'`; `CinematicCar: headlights on` / `dashboard lit`; 4 lamp + 2 dashboard
  materials found.
- Indicators: `accleft` twice → `CinematicCar: indicator left`, `CarLights: ... indicator 0`.
- Brake / reverse: `hold=brake` → `brake 1 reverse 1`.
- **The lamps really light**: two runs identical but for the handbrake (which holds the car still, so the only
  change is the lamps), `-carlights 1`, shot at frame 250 — the car's rear gains up to **+174** per pixel and
  **1623 pixels** brighten by more than 20.
- **The dashboard really lights**: `press=carcamera` (mode 23, near 0.05 = in-car), `-carlights 0` vs `1` —
  **664 pixels** brighten by > 20, all inside the instrument cluster (image rows 5-6 of 8, columns 3-4).

### 4. Not done

- **The in-car sat-nav.** The interior `GPS` quad is a real mesh with the `TD_GPS` shader and a single
  `satellitemap` texture, and it has a `UVOFFSET` constant — retail clearly renders the island map into it (and
  the `glm`-only note "the interior GPS dashboard quad is still drawn unprojected" is the same surface). Making
  it show the live map needs a render target the car material samples (the map screen already draws the island
  from a camera — `MapScreen::getCamera`), i.e. a texture the `GPS` material's slot 0 is pointed at per frame.
  That is an `EdenRenderer` / `MapScreen` job, not a `CinematicCar` one. **Next step.**
- The headlight ground pool (`headlight`, `TD_ShadowBox`) is still skipped: its projection constants are not
  implemented (`-shadowboxes` draws it) — the pre-existing `TD_ShadowBox` item, not this session's.
- The needles (`ND_SPD_1/2` …) are still at their data rest pose (the older "speedometer needle" item).
- The window blend's duration and the indicator cadence are `[INF]`.

### 5. The lamp colour: what was measured, and the dead ends

The owner, testing in person: *"the emission was to low down"* and *"what looked like brake lights were glowing
white instead of red"*. Every candidate lever was A/B pixel-measured on the car's rear (handbrake held so the car
does not move, so the lamps are the only change; the lit rear lamp region reads **~(109, 84, 91)** — white-pink):

| lever | result |
|---|---|
| `MATPARAM.x` (0.2 → 1) | **works** — the lamp lights (1623 px > +20, max +174). It is a *level*. |
| `MATPARAM.y` / `.z` / `.w` (1,1,1,5 vs 1,1,0,0) | **no effect at all** (byte-identical screenshots) |
| `material.Diffuse` = red | ~no effect (394 px, max 25) |
| `material.Ambient` = red | ~no effect (same) |
| `GLOSSFCT` = red | dims the lamp (1356 px, max 194) but does **not** tint it — it is a gloss/level, not a colour |
| the shader (`TD_CarPaint_NM`) constants | PS has only `SpecParm` / `GlossFct`; no emissive constant at all, so there is nothing else to write |

**Conclusion: the lit colour is in the `california_lgt_on` texture**, not in any material parameter — a red brake
lamp needs a tinted replacement texture (or the retail driver, which is in the encrypted half). The working
knob that was kept is **`-lampparam x,y,z,w`** (default `1,1,1,5`, the always-lit interior `Lamp` material's own
values), so the level can be dialled from a screenshot without a rebuild. The trial `-lampcolor` knob was removed
rather than left in misleading.

### 6. Landed on the hub (this branch merged `win/win`)

`cline` was 4 ahead / 15 behind when this session finished, so the hub's work was merged **in** first
(`win` sessions 48 parts 1-5: the borrowed-texture lifetime behind the device lost, the garage-exit cutscene
stream centre, the in-car 0.05 m near plane — the meeting_tess "black sail" and the garage pan tear — the intro
preload along the camera path, the sparse-hills diagnosis, `docs/PERF_PLAN.md`, the dealer mirror / NIGHTLMAP
fixes). **Two conflicts, both kept whole**: `docs/STATE.md` (both sides added session sections at the top — the
cline sessions 11 / 10 / 9 stay above the win session-48 parts, matching the bullet order) and
`source/game/freeride/gm_freeride.cpp` (the `C_FIRST_OUTRO` case: this branch's `outroWheelSpin` now runs
*before* the hub's `streamWorld(carWorld.getTranslation())` fix, so the wheels roll **and** the sectors follow
the car). The merged tree builds clean and the night light state, the title and `FIRST_WM` were re-verified
headless. The result was then landed on the hub's `win` as a **fast-forward** (`git -C C:\OpenTDU2 fetch cline;
git -C C:\OpenTDU2 merge --ff-only cline/cline` — the hub was idle with no modified tracked files), so both
trees are at the same commit; **the hub's `build\` is now stale and needs a rebuild** before its
`opentdu2.exe` matches the source.

### 7. The licence and the GitHub repository (owner's request)

The owner created a private GitHub repository and asked for the work to be pushed there with a licence that makes
forks **credit and share alike**: `https://github.com/danny199012/Open-TestDrive-Unlimited-2-`.

**Chosen: GPL-3.0-or-later** (`LICENSE`, the canonical text) — copyleft, which is exactly "keep the notices, and
publish your version under the same licence with its source". Alternatives, if it is ever revisited: AGPL-3.0
(adds the network-use clause), MPL-2.0 (file-level copyleft, friendlier for reuse), or no licence at all
(all rights reserved — the *most* restrictive, and what the tree had until now).

**`NOTICE` scopes it honestly, and that scoping is the important part**: the port's own code is GPL-3.0+;
*Test Drive Unlimited 2* and its data belong to Eden Games / Atari and **none is included**; the
reverse-engineering extracts and the in-game screenshots under `docs/` are derived from the game and are **not**
covered by the GPL; `third_party/dxvk` stays **zlib** and the other dependencies keep their own licences; and
this tree continues **CamilleKMI/OpenTDU2**, which carries no licence of its own, so the GPL covers this
repository's own contributions only. README's `to be defined by the repository owner` placeholder is replaced
with a summary of all of this.

**How the push was done** (from this workspace — `origin` is new in `cline` and points at the owner's repo;
`upstream` still points at CamilleKMI/OpenTDU2 and stays read-only):

> **Watch the remote names.** They are not the same in every tree: in **`cline`** `origin` = the owner's GitHub
> and `upstream` = CamilleKMI; in the **hub** `origin` = **CamilleKMI/OpenTDU2**, so `git push origin` there would
> aim at someone else's public repository — never do that. Push from `cline`, or name the remote explicitly.

```
git remote add origin https://github.com/danny199012/Open-TestDrive-Unlimited-2-
git push --progress origin cline:main      # ~115 MiB pack, full history; auth came from Git Credential Manager
```

`main` is the repository's default branch and the tip is `3e175d2`; the hub's `win` was fast-forwarded to the
same commit. **No game data is in the history** (`.bnk` / `.cpr` / `.big` are gitignored and were checked). Note
that the push went out from `cline` **before GLM's real-estate commits landed on the hub**, so those three
commits are not on GitHub yet — re-push after they land (`git push origin <tip>:main`).

### Test recipes (all `-fixedtimestep`, `-game C:\TDU2-Gamedata\TestDrive2`)

```
-night -logcarlights -autoplay "200:press=accleft;400:press=accdown" -exitframe 700                 # windows animate
-night -logcarlights -autoplay "200:press=accleft;400:press=accleft;700:hold=brake" -exitframe 1000 # indicators
-night -carlights 1 -autoplay "250:shot=C:/OpenTDU2-cline/_a.ppm" -exitframe 260                    # lamps off / on
-night -carlights 1 -carlod 4 -autoplay "100:press=carcamera;400:shot=.../_b.ppm" -exitframe 420     # dashboard
```

`carcamera` must be written `press=carcamera` — a bare `100:carcamera` is parsed as a command with no argument
and does nothing (it is logged as `Autoplay frame 100: carcamera=`, which is how that was caught).

## Session 2026-10-30 (Cline, `cline`, tenth): the California's wheels froze in C_FIRST_OUTRO


Owner (the Tess → first-drive scene): *"after meeting tess to drive the california for 1st time after the party the
camera pans the car and a lot of oddities are there of clipping or culling like ... but as you rev the car the
speedometer needle doesn't move and wheels don't spin in that scene and when garage opens and it slides outside the
world looked destroyed like low poly not sure why it never happened before."*

### Reproduced

    build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-cline-userdata ^
        -gm FREERIDE -nopad -exitframe 1260 ^
        -autoplay "80:shot=.../_c1.ppm;180:..._c2.ppm;...;1180:..._c12.ppm"

(The story flow plays C_FIRST_OUTRO; `-skipcine` skips it.) With `-logdrive` on the same run the car is
driven down the .sce `PLAYER_CAR` track (pos moves 36340 → 36327 …) while `spin 0.0 w 0.0` (the physics is
not stepped) and **`rimY` never changed** — the rims and tyres were frozen while the car slid out of the
garage.

### What changed

- `source/game/freeride/gm_freeride.{h,cpp}` — `updateWheels()` returned early for `eState::Outro` (and the
  physics is not stepped during the cinematic either), so the rims / tyres kept their last pose.
  `FirstOutro::getCarPose()` already computes the track velocity and `outroCarWorld()` threw it away: it is
  now kept in `outroVelocity`, the Outro tick integrates it
  (`outroWheelSpin += (velocity · car forward) / carDesc.TireRadius * dt`) and `updateWheels()` rolls the
  wheels from that angle while the cinematic owns the car (steer 0). The Drive / Knock / Fade states are
  unchanged — there `spin` still comes from `WheelState.SpinAngle`.
- `gm_freeride.cpp` — the `-logdrive` line moved out of the `Drive / Knock / Fade` case to just after
  `updateWheels()`, so it covers **every** state (the Outro included). That is what makes this fix
  verifiable from the log alone.

### Verified

- `-gm FREERIDE -nopad -logdrive -exitframe 900`: `rimT (0.83 0.22 1.34)` stays on the hub while `rimY`
  rotates (0.00 0.61 1.08 → 0.00 -1.10 0.56 → 0.00 -0.41 -1.17 → 0.00 1.24 -0.10 → …) and `spin 0.0` —
  the rolling comes from the track speed only, at the physical rate (~106 rad/s at the cinematic's ~36 m/s).
- The Drive state is unchanged: `-gm FREERIDE -skipcine -tp 27574.8,8303.3 -heading -33.8 -logdrive
  -autoplay "60:hold=accel"` climbs 0 → 63 km/h with `spin` / `w` from the physics and `rimY` following it.
- Owner confirmed: *"wheels moved in the cutscene and while driving it all seems fine."*

### Ruled out (with the test that ruled it out)

- The camera "clipping / culling" in the same scene is the **authored** one, not a cull bug:
  `otdu2_cine_tool dump .../eph_l3di.bnk` shows the 4 shots (`c_first_outro_01..04`, fov 35-45) with the
  camera at wheel height (`c_first_outro_01` y 0.42, `_02` y 0.41, `_03` 0.59 → 2.6 scene-local) passing
  < 0.8 m from the rim. Same conclusion as `glm` session 45 (retail clips the same way).
- The world is **not** upside-down / destroyed in that scene: the 12-frame sweep renders the island, the
  house and the terrain normally. (A first pass read the frames as flipped — the scratch PPM→PNG converter
  was flipping the rows, not the game: the PPM's own row 0 is sky, row 719 is ground.)
- The drivetrain is fine: the same build drives off a road spot to 63 km/h.

### Not done / next

- **The speedometer needle (the second half of the report).** During the Outro the HUD is hidden on purpose
  (`hud.setVisible( state == Drive || Knock || Fade )`) and `hudState.SpeedKmh` / `Rpm` are forced to `0` /
  `RPMMin` (`gm_freeride.cpp` ~2415), so there is no gauge to move there. The **car's own 3D dashboard
  needles** (`D_ND_SPD` / `D_ND_RPM`, `docs/re/california_spec.md` §4) are **not driven at all**:
  `cinematic_car.cpp` has no `ND_` handling, the needles stay at their data rest pose. Wiring them
  (rpm → the `ND_x_1` .. `ND_x_2` angle, FUN_00c52240 / FUN_00968fe0) is the next step.
- **This tree has none of the `glm` session-45 fixes** (verified: `glm/glm` is not an ancestor of `cline`,
  and `win/win..cline` is the pause-menu commit alone). The meeting_tess / garage **glass panes still render
  near-opaque black** (the "black sail"), the interior **GPS dashboard quad is still drawn unprojected**, the
  **shop wall mirror does not reflect** and the dealer showroom still loses its **NIGHTLMAP** — fixed on
  `glm/glm` (`a67b464`, `3bcd0d1`, `6d16906`). `glm` also carries `bafbe5c` (**the player's car drawing with
  destroyed image views → `VK_ERROR_DEVICE_LOST`**), which is the "Vulkan corruption" family the owner
  reports. Merging `glm/glm` into `win` is the next move for all of those.
- Observation, not yet diagnosed: with `-skipcine` the car spawns at the C_FIRST_OUTRO **end pose**
  (`FREERIDE: driving from (36008.46, 5.44, 5814.22)`), and a scripted `hold=accel` drives it 88 m and then
  leaves it sitting at ~0.2 km/h at 8000 rpm (all four wheels in contact). The owner does not see it in
  normal play, so it is filed rather than fixed.

## Session 2026-10-30 (Cline, `cline`, ninth): the pause menu's dead entries — Players opened the online screen, Editor hung on Loading

The owner: *"I know some pause menu stuff is broken in some menu's ... I will let you decide what you want to
work"* — the other two workspaces are down for resets, and the owner was testing from the hub.

### What was actually broken

The pause menu is **movie-driven** in freeride (`PauseMenu::tick` logs "the movie drives the menu
(menu_sq.isMenuH)"): the movie owns the squares, the selection and the pages, and the game only routes the
keys. The locks it reads are the `/:Lock*` root variables. `setEntryEnabled()` set three of them from its
arguments and then **cleared the other five to `false`** — i.e. unlocked:

    player.setVariableBool( "/:LockGameProgress", false );
    player.setVariableBool( "/:LockPlayers", false );
    player.setVariableBool( "/:LockSnapshot", false );
    player.setVariableBool( "/:LockEditor", false );
    player.setVariableBool( "/:LockProfiles", false );

That contradicts the function's own comment ("The other entries keep their retail lock flags (features not
built yet)") and the `kEntries` flags, so the three entries whose features do not exist were selectable:

| entry | what validating it opened |
|---|---|
| `players` (sq2) | the online **TDU2 Friends** screen — empty, no rows, "Search for a player" with no online to search |
| `gme` / Editor (sq4) | a **"Link to player"** page with a modal **"Loading…"** spinner that never resolves |
| `profiles` (sq6) | the same online family (empty) |

Reproduced headless (`_pv1_players.ppm` … `_pv6_profiles_val.ppm`): `-autoplay
"200:press=pause;500:press=right;600:press=right;700:shot=...;800:press=validate;1000:shot=..."` — the log
shows the validate producing `FSCommand 'sndp:valid'` then `'pcrd:open'` / `'goed'` / `'wmon'`, and the
screenshot the online screen.

### The fix (`source/game/pause_menu.cpp`)

- `setEntryEnabled()` now drives every lock variable from the entry's own state, so a dead entry stays locked:

      player.setVariableBool( "/:LockGameProgress", !bEntryEnabled[kEntryGameProgress] );
      player.setVariableBool( "/:LockPlayers",      !bEntryEnabled[kEntryPlayers] );
      player.setVariableBool( "/:LockSnapshot",     !bEntryEnabled[kEntryPhoto] );
      player.setVariableBool( "/:LockEditor",       !bEntryEnabled[kEntryEditor] );
      player.setVariableBool( "/:LockProfiles",     !bEntryEnabled[kEntryProfiles] );

- `kEntryPlayers = 2` / `kEntryProfiles = 6` added (only the indices the modes touched were named before).
- `kEntries[kEntryGameProgress].bEnabled` **false → true**: the flag was set in session 6, *before* session 24
  built the Game Progress pages, and was never flipped — the pages were reachable only because the lock
  variable was being cleared. With the new rule the flag is what keeps the page reachable, so it must be true.
  Net effect on Game Progress: **none** (its lock variable is `false` before and after).

### Verified (headless, `build-cline\bin\opentdu2-cline.exe`)

- **Locked entries stay closed.** `-autoplay "200:press=pause;500:press=right;600:press=right;800:press=validate;1000:shot=...;1100:press=right;1200:press=right;1400:press=validate;1600:shot=...;1700:press=right;1800:press=right;2000:press=validate;2200:shot=..."` (Players, then Editor, then Profiles): the log now shows **`FSCommand 'sndp:wrong'` three times** — the movie's own invalid-entry sound — and **no** `pcrd:open` / `goed` / `wmon`; every screenshot (`_v2_players_after.png`, `_v4_editor_after.png`, `_v6_profiles_after.png`) is the pause menu **still open**, the entry's tooltip showing a **padlock** where it used to show the A prompt.
- **The live entries are untouched.** `_fx2_progress.png`: "Progress & Objectives" selected with the A prompt, and validating it still opens the Completion page (73 levels, Global level 41/73, the four skill gauges); `_fx6_optionspage.png`: the Options page opens from sq5. The casino's own list is unchanged (`_c1_casino.png`: 6 entries, no photo / gme; `_c2_casino_gp.png`: its Game Progress unlocked, with the A prompt).
- The movie does **not** skip a locked square — it moves onto it and shows the padlock (retail's own locked presentation). The game's fallback path (`refreshEntries` / `validateSelected`) does skip them, but that path is not the one running.

### Found, not fixed (the rest of the pause screen — `docs/SURVEY.md` item 10)

Against the owner's retail captures `docs/images/retail-pause-freeride.webp` / `retail-pause-casino.png`:

- **The map panel's island badges read HAWAII on Ibiza.** Retail: blue **IBIZA 7** (top left) / **IBIZA 2** (bottom right); port: green **HAWAII** / **HAWAII 7**. `refreshEntries()` does `gotoLabel( "/pause_menu/data_extd/map/island/i1", "i1", false )` and the sprite **is** there (`-logpause` display list: `map` → `island` → `i1` / `i2`, both at frame 0), so the frame simply is not changing — either the label is not on that sprite or `gotoLabel`'s return value is being ignored (it is). Root cause not established; the badge's frame labels still need a dump.
- **The casino / garage / avatar-choice pause clock reads 00:00** (retail 18:14 / 12:36): only `GMFreeRide` calls `setClock()`, and from the weather preset's name (`14:00`); the port has **no in-game clock** at all (no time-of-day symbol in `source/`). Needs a decision, not a guess.
- **The info card's objective header is missing** (retail "Obtain C4 license" — `pause_menu.cpp` already notes the missing header is unexplained).
- **The money is unformatted** (retail `$31,410`, port `5000000`).

### Housekeeping

- `opentdu2-watchdog.log` (written by the watchdog added in session 8) was not gitignored — added beside `opentdu2.log`, so a run no longer leaves an untracked file in the tree.

## Session 2026-10-30 (forty-eighth, `win`, part 5): the intro's sparse hills are the instance draw distance — diagnosed, not fixed

The owner: *"on my 360recomp in that scene theres a lot more vegetation and trees also no pop in so wonder if
camera has a vision limit?"* (C_INTRO sea sweep, the TDU2-logo beach shot).

### Ruled out (with the test)
- **Far plane**: 40 km — not it.
- **Streaming radius**: `-cineradius 5` (3.8 km) vs the default 2 → the frame-1200 beach shot is identical.
- **Missing data**: the V (vegetation) / O PMIs are loaded; `-noinstances` strips exactly the trees and houses,
  so the hill trees are PMI instances.

### Found
- **The per-instance draw distance is the lever**: `-instancedistmul 2` puts trees on the ridge lines behind the beach
  that the default leaves bare (`docs/images/port-intro-hills-default-s48.webp` vs `-dist2x-`; same frame, `-fixedtimestep`). Our distance is
  `sqrt( lods.back() * 3.160494 * max(f,1) )` (FUN_00989d10 / FUN_00df6e60) with the per-camera terms
  `cam[+0x180]` and `*(DAT_00fd51c0+0x220)` taken as 1 — their writers are `[ENC]` (`pmi_streaming_spec.md` §3/§5).
- **Cost**: ×2 = 36.5 → 83.5 ms/frame on that shot, because past lod[0] the port draws **full LOD0 meshes** where the
  game draws the cheap `TD_HardwareInstance` meshes / `TD_Impostor` quads (spec §9 row 3). ×4 blacked the frame.
  Not enabled by default.

### What changed
- A/B flags only: `-cineradius <n>` (`intro_cinematic.cpp`), `-instancedistmul <x>` (`eden_world.cpp`
  `loadInstances`).

### Not done / next
1. **Measure the retail range on the 360 recomp** (the behavioural oracle, CLAUDE.md §7): the same beach shot at
   C_INTRO frame ~1200 — at what distance do the hill trees stop? That gives the missing camera factor without
   reading `[ENC]` code.
2. **Draw the low LODs** (spec §9 row 3): load LOD1+ library objects (the last MBNK materials:
   TD_HardwareInstance meshes, TD_Impostor quads, e.g. `V_PALM_A` LOD1 = `415f4d4c8a7da7b5`) and pick
   `lod = first k with d² < dist2[k]·F` in `EdenWorld::drawInstances` (`eden_world.cpp` ~330). That is what makes a
   longer range affordable — and the impostor 0.33→1.0 blend (§4) is what hides the pop-in on the 360.

## Session 2026-10-30 (forty-eighth, `win`, part 4): the intro preloads along the camera path — the sea-sweep pop-in

The owner: *"could this issue also possibly cause popin and no shadows on the 1st intro the sea sweep and just
before the character select?"* — no: the near plane only touches the first 0.8 m. **No shadows** is data (weather
`Intro`, `sun_shadow_opacity` 0, session 36's issue 4). **Pop-in**: `IntroCinematic` streamed only around the
camera's current position, so the fast sea flight loaded sectors on arrival.

### What changed
- `source/game/avatarchoice/intro_cinematic.cpp` — the world also streams around the camera **2 s ahead on the
  authored path** (`Cin.getCamera( t + 2 )`; a cut ahead preloads the next shot). `-cinelookahead <s>` (0 = off).
  A load still needed at the current camera is logged `IntroCinematic: pop-in load at the camera` (the measure).
- `source/world/eden_world.*` — `unloadFar( position, ahead, radius )` keeps sectors near either point (a preloaded
  sector is not dropped the next frame); `loadAround` returns whether it loaded.

### Verified
- `-intro -nopad -fixedtimestep -exitframe 5400`, `-cinelookahead 0` vs default: pop-in loads **3** (C_INTRO t 0.02,
  7.32, 18.25) → **1** (t 0.02, the first frame — unavoidable). Run time 117.0 s vs 116.3 s (no cost).

### Not done / next
- Object draw-range fade-in (the ~960 m LOD ranges, `spec_pmi_streaming` 9.7) is a separate pop-in source this does
  not touch; if the owner still sees things appear *close* to the camera, that is the LOD selection, not streaming.
- No before/after screenshots of the sea flight were taken (the metric above is the evidence).

## Session 2026-10-30 (forty-eighth, `win`, part 3): the meeting_tess "black sail" and the garage pan tear are the near plane — fixed

The owner: *"tess meeting cutscene and the california car pan/sweep some stuff doesn't render correctly in them
I had GLM looking into this but could never fix it"* (glm session 45 below: the sail blamed on the glass, the pan
tear filed as "retail would clip the same way").

**Reproduced**: `-gm CINVIEWER -nopad -fixedtimestep -autoplay "250:shot=…"` (meeting_tess, the in-car shot) and
`-gm FIRST_WM -nopad -fixedtimestep -autoplay "790:shot=…"` (C_INTRO wheel pan).

### Ruled out (with the test that ruled it out) — the bisect
- **Glass**: `-hidematerial GLASS` (every glass material) → the sail stays. glm's lead was a dead end.
- **Skinning**: `-hidematerial _SK` → stays. The cockpit door panels `-hidenode INT_HR_L;INT_HR_R` → stays.
- Body set (`California;Black;Chrome;…`) removes only a thin red-edged spike; cockpit set removes the sail;
  narrowed to **`Leather`** alone, which `-logskin` puts on interior `COCK_HR` and `STEER_HR` (rigid, palette 0).
- **`-forcenear 0.05` removes the sail and shows the authored shot**: the camera sits at the dashboard looking back
  at the driver through the steering wheel. The 0.8 m cinematic near plane cut away the wheel, the dash and most
  of the windscreen header; the header sliver left over was the "sail". `-forcenear 2.0` cuts the whole car.

### What changed
- `source/game/avatarchoice/intro_cinematic.cpp` `IntroCinematic::updateView` — an authored camera within 0.8 m of
  the car body bounds (`CinematicCar::getBodyBounds`, in car space) uses the walk camera's 0.05 m near plane;
  elsewhere the 0.8 m default stays. `[INF]` the retail handling of these shots is not readable; a global 0.05
  would cost depth precision (standard Z, 40 km far plane).
- New A/B flags: `-hidematerial a;b` (skip materials by name substring, every pass — `eden_renderer.cpp`
  `recordDraw`) and `-forcenear <m>` (every camera, `EdenSceneView::setCamera`). `-logskin` also prints the
  nodes carrying `Leather`.

### Verified
- meeting_tess frames 250 / 330: the sail is gone, the wheel / dash / header draw
  (`docs/images/port-tess-sail-before-s48.webp` → `-after-`).
- Garage pan frames 790 / 820 / 850: intact rim, disc and caliper; with `-forcenear 0.8` (the old behaviour) the
  same frames are torn open to the spring — 16.7 / 11.4 / 3.0 % pixels differ (>40)
  (`port-garage-pan-before-s48.png` → `-after-`).

### Not done / next
- `FirstOutro` and `FadeFreeride` set `Near = 0.8` themselves (`first_outro.cpp:281`, `fade_freeride.cpp:240`)
  and do not get the rule; none of their shots looked cut, but the same three lines apply if one does.
- The milky veil over Tess in the first frames of the shot is the authored 4 s white fade-in, not a bug.

## Session 2026-10-30 (forty-eighth, `win`, part 2): the garage-exit cutscene streamed the world at (0,0)

The owner: *"cinema camera for cutscenes seems to have clipping or culling issues would swapping the cam with the
game cam work"* and *"the cutscene leaving the garage before freeride the map looks funky … kind low poly"*.

**Reproduced**: `-gm FREERIDE -nopad -fixedtimestep -autoplay "450:shot=…;800:shot=…" -exitframe 820` — from
frame 450 the California drives over the bare island LOD: no road, no sector models
(`docs/images/port-first-outro-before-a-s48.webp`, `-before-b-`). The log tried `sector-0-0-0-0.bnk` (not found).

### What changed
- `source/game/freeride/gm_freeride.cpp` `GMFreeRide::initialize` — the stream centre read
  `outro.getHousePosition()` **before** `outro.locate()` set it: the world was streamed around (0,0), the empty
  corner of the map. `locate()` now runs first → `EdenWorld: 9 sectors loaded around (36382, 5730)`.
- `eState::Outro` now calls `streamWorld( car position )` every frame (the Wakeup state already did): the sectors
  follow the car down the road.

### Verified
- Same command: the road, the village (BIENVENIDOS banner), the vegetation, the bay boats draw
  (`port-first-outro-after-a-s48.png`, `-after-b-`).

### Ruled out
- **Culling**: the same run with `-nocull` differs by mean 0.00-0.08 / ≤0.04 % pixels >40 at all five shots →
  the frustum cull with the cinematic camera hides nothing. Swapping in the game camera would not help: both feed
  the same `view.setCamera` path. The "clipping / culling" was the unstreamed world.

### Not done / next
- Close-up shots use the retail near plane 0.8 (`FirstOutro::getCamera`, FUN_00b3eba0) — if the owner still sees
  near clipping in a specific shot, get the frame number (`-cinetime <s>`) and compare.
- Other cutscenes (the intro party, C_FADE_FREERIDE) were not checked for the same stream-centre class of bug.

## Session 2026-10-30 (forty-eighth, `win`): borrowed textures outlive their bank — the class of bug behind the device lost

The owner: *"vulkan is hindering the project … I had a lot of UI elements on screen and eventually caused the car
to have graphical corruption"*, with a screenshot of the glm build after a police arrest:
`VUID-vkCmdDrawIndexed-None-08114 … variable "t0_2d"/"t3_2d"/"t6_2d" is using imageView 0x0 that is invalid or
has been destroyed`, then 870-905 ms `[hitch]` frames.

**Diagnosis**: not Vulkan — a use-after-free of our own. `EdenRenderer::setupMaterial` looks a material's texture
up in its own bank, and when it is not there falls back to the **global** `TextureCache::find(hash)`, which returns
the newest copy from *any* owner and stores the raw pointer in the material. When that other owner is released
(`releaseOwner`: ~20 call sites — sectors, Flash movies, cars, HUD), the textures are destroyed (frame-delayed, so
the in-flight frame is safe) but the borrowing material keeps drawing with them → the VUID, corruption, and at
worst `VK_ERROR_DEVICE_LOST`. glm's `bafbe5c` (per-instance CinematicCar owners) fixed one instance (the chase
cops) — the fallback stayed. Switching graphics API would not change any of this.

### What changed
- `source/render/texture_cache.*` — `findForOwner( borrower, hash )` (the global lookup on behalf of an owner)
  records each cross-owner borrow (borrower → lender → count). `releaseOwner` drops the released owner's own
  borrows, then a lender that is still borrowed is moved to a `retired` list (no longer findable) instead of being
  destroyed; a retired owner is destroyed once its last borrower is released. `[INF]` the rule assumes a bank's
  models go with its textures — true at every `releaseOwner` caller read. Debug logs name each borrow once
  (`Texture cache: 'X' borrows from 'Y'`) and each retire / late destroy (`-verbose`).
- `source/render/eden/eden_renderer.cpp` — `setupMaterial`'s fallback uses `findForOwner( pTextureOwner, … )`.
- `source/game/cinematic/cinematic_car.cpp` — the owner names were built **before** the banks loaded (empty
  paths), so common_car + the car bank were one owner `"#<id>"` while `unload()` released `"<path>#<id>"`:
  **the car textures were never freed** (a leak per car load — the chase cops, dealer cars). Named after the load.
- Merge: `glm/glm` (`bafbe5c` etc.) into `win` — the radio_player conflict resolved (`takePreload( station,
  index, … )`), the stray STATE marker removed (`ef2602c`).

### Verified
- Run: `-gm freeride -skipcine -nopad -tp 21796.3,24188.1 -snaproad 150 -hitchms 100 -police
  -vk_enable_validation_layer -verbose -exitframe 1500` (glm's chase repro, with validation).
- The borrow log shows the hazard live: **every traffic type (police, taxi, bus, van …) borrows from the
  player car's common owner** (`cubemap` etc.), the area banks borrow from `commonworld.bnk`, the worldactors
  from `commonworld.bnk`. Before this change, a player car swap would have freed textures under all traffic.
- Run 1 (to frame 1500, before the cinematic_car rename): `ChaseMode: ARRESTED (contact)` at ~frame 188, then
  **0** `08114` VUIDs and **0** `VkResult -4` in 1500 frames. The retire path fired on a latent case:
  `common_clothes.bnk released while borrowed - kept until its borrowers go` → `retired … destroyed`.
- Run 2 (to frame 400, with the rename): **0** VUIDs, **0** device lost; the cops' textures are now really freed
  at the despawn — `common_car.bnk#2` / `#3` *released while borrowed* (their own car / rim banks) → *retired …
  destroyed* once those went. `#1` (the player) is retired at exit because traffic borrows it — as intended.
- Not run: the owner's own "lots of UI, then corruption" drive (Flash movies are owner-local lookups, so they
  were not borrow sources in these logs; the corruption is most likely this same car-material path).

### Not done / next
- `assignEnvironment` hands cube maps (`CUBECAR` of the intro bank, the env cube) to car materials directly — not
  tracked as borrows; if the intro bank is released while a car still holds the cube, the same VUID returns.
  Route it through `findForOwner` or hold the cube renderer-side.
- The 900 ms `[hitch]` frames are `offscreen` with the validation layer on — measure without validation before
  treating them as a bug (`[perf] draw split`).
- A retired lender stays alive as long as any borrower lives (traffic banks for the session) — bounded, safe;
  watch the VRAM if many player-car swaps happen in one session.

## Session 2026-10-30 (Cline, `cline`, eighth): the frame loop watchdog — "the game goes unresponsive"

Task (the owner): *"I was driving, all was well, then after a short while the game gets unresponsive.
Unsure if any log was left — and if not, could you attach some logger please; even before the latest
changes I would drive for a while and the game would go unresponsive."*

### 1. A log *was* left: the GPU was reset

`C:\OpenTDU2\opentdu2.log` (the hub's last run, 2026-10-30 00:43, 1219 lines) ends with **639 identical**
`render_device.cpp:1492: vkQueueSubmit( graphicsQueue, 1, &submitInfo, frame.Fence ) failed (VkResult -4)`
lines. `-4` is `VK_ERROR_DEVICE_LOST`: the driver **reset the device** (a GPU hang / Windows TDR
timeout, or the driver running out of memory). The `[perf]` lines just before it are the smoking gun:
3840x2160 vsync, `draw avg 36-47 ms / max 1027-1036 ms` (**a full second in one frame**), offscreen
25-27 ms, present 9-19 ms, 21-28 fps — and the log then stops with no shutdown line. After the first
loss every present fails, so the window stays on the last image: the game *looks* hung while the loop
keeps running. That is the owner's "unresponsive".

### 2. The watchdog (`source/core/watchdog.*`)

A thread that watches the frame loop's heartbeat. `TestDriveGameInstance::mainLoop` calls
`Watchdog::Beat( phase, frameIndex )` at "loop", "update", "draw" and "present", and the thread turns
silence into a report — the phase that stopped, the last frame, the duration, and the worst frame gap
of the run:

```
[INFO] STALL: no frame for 1.1 s (last frame 30, phase 'present')
[INFO] STALL: still no frame - 3.1 s now (last frame 30, phase 'present')
[INFO] recovered: frame 82 after a 4.1 s stall (phase 'present')
[INFO] stopped after 8.1 s: 800 beats, 2 stalls, worst frame gap 4001 ms (frame 31)
```

It writes to the game log **and** to its own file (`<log>-watchdog.log`, `opentdu2-watchdog.log` by
default) so that a hang cannot take the report down with it: the main-log write goes through the new
non-blocking `Logger::WriteTagged` (a `try_to_lock`; a thread hung *inside* a log call used to be able
to block any writer). On by default, `-nowatchdog` off, `-watchdog <s>` period (default 5 s),
`-watchdoglog <path>` its file; `-watchdogtest <ms>` freezes the loop once, mid-frame, so the report is
verifiable (that is how the lines above were produced). The `[perf]` block also logs
`[perf] vram: <used> MB / <budget> MB` now (a device lost after a long drive is often the driver
running out — the run here: 1489 MB / 52000 MB with `-no3d`).

### 3. Device lost: reported once, not 639 times, and no silent freeze

* `RenderDevice::reportDeviceLost` sets `bDeviceLost`, logs **one** clear error naming the call, and
  `endFrame` stops submitting (the old `OTDU_VK_CHECK` logged the same line every frame).
* `beginFrame` returns early on a lost device, so the `vkWaitForFences` that used to be
  `UINT64_MAX` can no longer block the loop forever — the wait is now sliced at 1 s and logs
  `vkWaitForFences: the frame fence is still unsignalled after N s` when the GPU stops making
  progress (that silent fence wait is the *other* way to hang with nothing in the log).
* `TestDriveGameInstance::mainLoop` sees `hasDeviceLost()` and leaves with an ERROR line, so the run
  ends cleanly and the log has a proper tail instead of stopping mid-frame.

Verified: `-devicelosttest` (temporary hook, removed) produced exactly one render error, the loop's
exit line, and the watchdog's summary; `-watchdogtest 4000` produced the stall/recovery lines above;
a plain `-gm FREERIDE -skipcine -no3d -nopad -exitframe 300` run is unchanged apart from the watchdog
lines.

### 4. Where the loss came from, and a repro attempt that did not fire

The owner's `options.ini` (`%APPDATA%\OpenTDU2\OpenTDU2`) has **`ray_tracing_world = 1`**, i.e. the
Advanced option whose own help text is *"Put the world's meshes into acceleration structures: memory
and load time, nothing is lit by them yet"*. The first loss lands **immediately after the car streamed
into new sectors**: `EdenWorld: 3 sectors unloaded, 9 loaded`, a BLAS built for every mesh of
`sector-5-0-3-6/7-o.3dd` and `sector-5-1-3-0-o.3dd`, the collision streamed for the same sectors
(10,713 triangles), then `VK_ERROR_DEVICE_LOST`. That is "drive for a while, then it hangs".

Reproduction attempt (`cline`, 2026-10-30, RTX 5070 Ti, the owner's ini settings incl. RT world on):
`-gm FREERIDE -skipcine -nopad -autoplay "60:hold=accel" -exitframe 3000` at 3840x2160 vsync →
**no loss**: 60 fps, `offscreen 10.8 ms`, and only **one** sector load event (the scripted car never
left the spawn's sector set; the owner's session had 3 loads + 1 unload and `offscreen 25-27 ms`), so
the streaming + BLAS path was never exercised the way the owner's drive exercised it. A watchdog
"worst gap 3762 ms (frame 0)" shows in every run: the boot stall, real but not the problem.

Next: an A/B with the new log in hand — turn **Ray traced world off** (Options > Advanced, or
`ray_tracing_world = 0`; restart to apply) and drive the same route, then `-novsync` and a lower
resolution (3840x2160 was the failing run), then `-noshadows` / `-noocean`. Watch `[perf] vram` and the
watchdog's "worst gap" for the frame before the loss, and send both logs.


## Session 2026-10-30 (Cline, `cline`, seventh): the car radio's stations — per-station playlists, and custom stations from folders in Music

Task (the owner): *"the game creates a folder in Music called OpenTDU2 and in there we
create a folder Hardstyle fm (example); that folder is like a playlist station and the
radio can load and play the files just like a true radio station — Hariba and Kalua have
an icon, so drop our custom radio art as .dds/.png and it shows up. The station = folder
name + icon + music."* Plus the bug they hit: *"when I skipped station the song stayed the
same but the art changed."*

### 1. The bug: one playlist for three stations

`RadioPlayer` had a hardcoded 3-entry `kStations` table and **one** `tracks` vector
(`load()` enumerated every `Euro/Bnk/sound/radio/music/*.bnk`), and `nextStation()` only
did `station = (station + 1) % 3`: the HUD swapped the logo and the name (exactly what the
owner saw) while the audio voice kept playing. The per-station split of retail is not in
the readable data (`setup.xmb` is one flat list), so the 65 songs are now dealt **round
robin** over the three stations [INF] (22 / 22 / 21) and a station change calls `play()` on
the new station's own song.

### 2. Custom stations: `<Music>/OpenTDU2/<Station>/`

`RadioPlayer::loadCustomStations()` (source/audio/radio_player.cpp) enumerates the
subfolders of the stations folder — default `SDL_GetUserFolder(SDL_FOLDER_MUSIC) +
"/OpenTDU2"` (created at load, with a `readme.txt` explaining the layout),
`-radio <dir>` / `OTDU2_RADIO_DIR` overrides it, `-noradiofolders` keeps the retail three.
The folder name is the station name, its music files (sorted by name) are its songs, and
`logo.* / station.* / icon.*` (.png / .jpg / .dds) is its art. Custom stations follow the
retail three, sorted by name. A station with no art simply shows no logo.

### 3. The music: the owner's ".mp3" is an Ogg Opus

`SDL_LoadWAV_IO` decodes WAV only, so a decoder stack was added and the format is now
**sniffed from the magic bytes**, never the extension: `RIFF/WAVE` (SDL), `fLaC`
(dr_flac), `OggS` → `OpusHead` / `\x01vorbis` (opusfile / stb_vorbis), `ID3` or a frame
sync (dr_mp3), `ftyp` = MP4/M4A (reported, no decoder here). The owner's own file
`Music/OpenTDU2/Hardstyle fm/Da Tweekaz - BLACKPINK - JUMP (Da Tweekaz Edit).mp3` is
`OggS` + `OpusHead`: without the Opus path it could never have played. New dependencies
(`cmake/Dependencies.cmake`): dr_libs (dr_mp3 / dr_flac), ogg, opus, opusfile; libopus is
configured with `OPUS_DISABLE_INTRINSICS` — its CMakeLists passes `-msse4.1` only for
non-MSVC compilers, and clang-cl reports `MSVC = 1`, so its SSE4.1 sources did not build.

### 4. The art: drawn in the movie's logo box

The retail logo is the Flash sprite `/HUD/radio/radio_logo` (three authored labels) and
`FlashPlayer::setBitmapOverride` only fills *placeholder* bitmap fills, so a custom station
hides the sprite and `FreeRideHud::drawRadioIcon` draws the station's texture over the
panel instead: `radioLogoBox()` unions the shapes of the sprite's current frame (display
list × matrix chain, twips / 20 — the gauge dial recipe) and the art is fitted inside it,
aspect kept. PNG / JPG / BMP / TGA go through stb_image, DDS through the Eden DXT decoder
(`DecodeBlockCompressed`; uncompressed DDS through the pixel-format masks):
`source/render/image_file.{h,cpp}`.

### Verified (headless, `-no3d -nopad -fixedtimestep`)

- `-logradio`: `Radio: station 4 'Hardstyle fm': 1 song(s)` then `station 'Hardstyle fm':
  playing Da Tweekaz - BLACKPINK - JUMP (Da Tweekaz Edit) (voice 43, 1 of 1, 134.3 s)` —
  the owner's Opus file, decoded and played through the Radio client.
- The station change changes the song: `'Hariba Radio': playing A Astronauts - Classic`,
  `'Kalua': ... A Astronauts - Oblivion`, `'Roadtrack': ... Acrylics - Innocence`.
- Art: a scratch station with a DXT1 `logo.dds` and one with a 256x64 `logo.png` both draw
  in the logo box (`radio logo box 92.1,313.9 146.0x67.9 -> art 92.1,329.6 146.0x36.5`);
  screenshots `docs/images/radio_station_dds.webp` / `radio_station_png.png` /
  `radio_station_hardstyle.png` (no art), and the retail logos are unchanged
  (`radio_station_hariba.png` / `radio_station_kalua.png`).
- The folder is created when missing (`Radio: created the station folder ...` +
  `readme.txt`), `-radio <dir>` points elsewhere, `-noradiofolders` leaves the retail three.

### Not done

- ID3 / Vorbis-comment tags: artist and title still come from the file name
  ("Artist - Title", the rule of the retail banks).
- MP4 / M4A (AAC) and WMA have no decoder (a log line names the file and the formats that work).
- The retail stations' jingles / DJ comments / ads (`sound/radio` ROADROCK_* / ADS-* /
  TIME_*) are still not played.

## Session 2026-10-29 (forty-fifth, `cline`, part 8): the cut-content scan — every prototype bank against retail

The plan at the end of part 7 below ("a content scan of every prototype bank against retail"). Built and
run unattended over both Xbox 360 prototypes. Findings: **`docs/re/cut_content_scan.md`**.

### What changed
- `tools/x360/eden_bank.py` (new) — a `.bnk` reader for **both** platforms (port of
  `source/filesystem/bank.cpp` + the 360 variant). It names every asset of a 360 prototype bank from the
  bank's own directory, which is what makes the scan able to say *which* asset changed.
- `tools/x360/content_scan.py` (new) — the scan. `--selftest` (16 checks, the Q7 evidence),
  `--bank <part>` (one bank, verbose), `--signatures` (phase 1: decompress + index, resumable),
  `--compare` (phase 2: diff + `report_<beta>.txt` + `diff_<beta>.json`), and `--restore <bank part>
  --userdata <folder>` — the findings, made actionable: every prototype texture whose name *and*
  dimensions exist in retail is written to `<userdata>/restored/<bank>/<file>.2db` (the retail file as
  the header template, the prototype's mip 0 as the pixels, one mip), which is what `TextureCache`
  loads in place of retail's; identical textures are skipped, `-norestored` switches it off.
- `tools/x360/restore_texture.py` — the header/pixel writer factored into `build_pc_2db`, shared with
  `--restore` (the single-texture path is byte-identical to before, checked).
- `tools/x360/beta_list.py` — `XZXD` moved here (one definition; `beta_extract.py` uses it).
- `tools/x360/README.md` — the two new tools and the measured 360 bank facts.

### The format facts the scan rests on `[MEASURED]`
- The 360 bank is the PC header big endian (`BANK` at 0x08, `NumEntries` 0x28, section offsets 0x30-0x3C),
  sections prefixed `{u32 size; u32 crc}` with content at **+8** — the entry table included, so records
  start at `SectionOffsetTable + 8`, stride 0x14, layout `{u32 offset; u32 size; u64 unknown; u32
  alignment}`. Entry *i* is record *i* on both platforms (`x360_bank.py`'s scan reads the same offsets
  from 12 bytes earlier and pairs them with the previous asset's hash).
- The directory tree + index table are byte-oriented and identical across platforms, so a 360 bank's
  assets get the PC names. Self-check: a 2DB's u64 at 0x20 is `GetIdentifier64bit(name)` on both.
- A 360 `.2db`: texels at 0x1000 (page aligned), the same header (w/h 0x28, format 0x3C, 27 = DXT5 where
  retail's sRGB variant is 55), mip 0 tiled + **16-bit-word swapped**. That transform reproduces the
  retail bytes *exactly* for the three textures the Inuit prototype shares with retail (q7_int /
  q7_int_ts / q7_glass_i) and differs in 19% of the bytes for the cut q7_int2 — so textures are compared
  byte for byte, and renames are proven by identical pixels.
- A 360 wav is **not** byte-identical to the PC one (0 of 34 same-named wavs matched), so audio is
  compared by name only and its rows are unverified.

### Verified
- `python tools\x360\content_scan.py --selftest` → 16/16 (both prototypes' q7_inuit_i.bnk byte-identical,
  wood_inuit prototype-only, q7_int2 pixels differ, the three shared textures identical).
- Full run: 9,553 named banks per prototype (13.97 GB uncompressed) in ~1 min at ~170 banks/s (numpy
  vectorized tiling; 4 banks/prototype fail to LZX-decode: `frontend/legal_*`, `frontend/tips_us`).
  Compare: 1 min then instant. Result — dec2010 CUT 214 / CHANGED 205 / RENAMED 5 / SIZE 7,515 / same
  797, aug2011 CUT 232 / CHANGED 234 / RENAMED 5 / SIZE 7,480 / same 783; **7,937 banks flagged by both**
  (2 dec2010-only, 14 aug2011-only).
- `--restore` verified end to end: `q7_int2.2db` is written as 262,224 bytes (`.2DB` magic, version 2,
  hashcode `0x0032544E495F3751` = `GetIdentifier64bit("q7_int2")`, 512x512, 1 mip) whose decoded mip 0
  equals the prototype's byte for byte (`9bea40581425f137`; retail's is `fb62acac5c1fcc55`), and the
  standalone `restore_texture.py` writes the same bytes (sha256 compared). Across the 112
  `vehicules/interiors` banks: 1,074 name-matching textures, **7 differ** (the Q7 dials + six interior
  glass textures: mustang_v6_premium_i, mustang_gt_i, default_i, tts_i, c12_zagato_i,
  mustang_shelby_gt500_i).
- The restore is collision-safe: the game keys the restored folder by the bank's **file name only**, so
  `hires/medres/lowres` (gauges, spotviewer) and `ibiza/hawai area-N-M` share a key — `--restore` stages
  everything, writes a key only when all claimants agree byte for byte, and reports the rest as ambiguous
  (7 keys in dec2010, 9 in aug2011); a prototype disagreement is reported and resolved by `--betas` order.

### Not done / next
- **The audio decoder** (XMA / the 360 wav variant) — the only way to compare the sound banks.
- A shape/name-similarity pairing pass: a bank re-exported under a new naming scheme (the R8 → R8 V10
  case, `r8.bnk` + `r8_med.bnk`, `mustang_v6_premium*`) reads as CUT because the names *and* the pixels
  changed; the report shows both sides so it is readable, but the tool cannot prove it.
- The porting candidates the scan turned up (all in `docs/re/cut_content_scan.md`): the R8 (not V10)
  interior, the Mustang V6 Premium cabin + `couture_croix` trim, the `mustang_v6_premium*` low LOD and
  rim banks, the Audi Inuit preorder clothes banks, `fx/vfx_car.bnk`'s 9 cut materials, the 15 `.bas` of
  `scenarics/packs/worldlifeset.bnk`, and `interior/icd_asia.bnk`'s 21 textures.

## Session 2026-10-30 (forty-seventh, `glm`): the "random unresponsiveness" — the chase arrest despawn killed the device

Owner: *"random unresponsiveness after driving for a short while ... twice I have also encountered a oddity which
looked like texture corruption or shader corruption that mostly occured when I spawned outside ferrari dealership
and then left"*; later *"i think the issue came from police chases"* — both right.

**Reproduced** (headless, parked at the dealer so traffic crashes stack violations):
    build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-hitch
        -gm freeride -skipcine -nopad -tp 21796.3,24188.1 -snaproad 150 -hitchms 100 -police -exitframe 1500
    (`-police` starts the chase at frame 150 deterministically; a plain drive into a chase + arrest reproduces it)
- At the arrest: `vkQueueSubmit ... failed (VkResult -4)` = **VK_ERROR_DEVICE_LOST**, a 9.8 s wedge, then every
  submit fails forever (frozen frame = the report). With `-vk_enable_validation_layer` the same moment names it:
  `vkCmdDrawIndexed(): the sampled image descriptor [...] is using imageView VKImageView 0x0 that is invalid or
  has been destroyed` — draws sampling destroyed textures (the "texture/shader corruption" look).

### The mechanism
- The chase cops load through `CinematicCar::load` — a second California. `CinematicCar` registered its bank
  textures under the **bank path** owner name, so the player's California and the cop Californias shared ONE
  `TextureCache` owner entry.
- The arrest despawn (`ChaseMode::despawnCops` → `CinematicCar::unload`) called `releaseOwner("...california.bnk")`,
  destroying **the player car's textures with the cops'**; ~3 frames later (the deferred-delete latency) the player
  car sampled destroyed image views → GPU page fault → device lost. The cline watchdog's 639 × `-4` log (2026-10-30
  00:43) is the same death on the owner's machine; the max-draw frames before it were the cop load + despawn churn,
  not the cause.
- Wider exposure of the same class: `EdenRenderer` binds materials' textures by raw pointer, falling back to the
  NEWEST owner's instance of a hashcode across ALL banks (`eden_renderer.cpp:465`) — any bank unloading under a
  live consumer can corrupt (the dealer-interior/exterior banks, sector banks while driving).

### What changed (glm `bafbe5c`, merged here)
- `cinematic_car.{h,cpp}` — per-instance texture owners: every loaded CinematicCar registers its banks as
  `<bank path>#<id>`; unloading one car can no longer pull the textures another car draws with.
- `chase_mode.cpp` — `despawnCops` follows the documented release rule: `waitIdle`, then
  `clearShadowCasterCache()` + `clearRtInstances()` after the clear.
- `radio_player.{h,cpp}` — the next song decodes in a background thread starting 20 s before the current one ends
  (bank bytes still read on the main thread: gpFile is not thread safe; custom-station files decode on the worker
  too — SDL IO only). The song-end frame no longer carries the whole bank read + decode. Test hook `-radioskip <s>`
  seeks the voice (StartTime) so a run reaches the song end in seconds.
- `spot_exteriors.cpp` — the tick() unload branch drains before erasing (like its own unload()) and logs the drop.
- `tdu_instance.cpp` — `-hitchms <ms>` (default 100, 0 off): one `[hitch]` log line per long frame with the
  update/draw split; update = game logic/streaming, draw minus its sections = the present/fence wait.
- `gm_freeride.cpp` (glm working tree, uncommitted there): the violation popup, loc 763 'Traffic violation'.

### Verified
- Post-fix A/B (glm): `-police` runs with and without validation pass chase → arrest → despawn → exit with zero
  destroyed-imageView VUIDs and zero `-4` (pre-fix: 10 VUIDs / device lost, every run).
- Radio: `ended after 218.58 s` → `'Oblivion' was preloaded (no decode stall)` → next song, no stall frame.

### Ruled out
- The radio as the *wedge* trigger (runs wedged identically without it — it only supplied a decode stall).
- The sector-unload caster cache (FREERIDE already clears it) and RT world (off by default).

### Not done / next
- The systemic fix: `TextureCache` has no cross-owner refcount and materials keep raw GPUTexture pointers; the
  global fallback can still dangle when a bank unloads under a live consumer (sector banks while driving). Proper
  fix = one shared refcounted store (or owner-scoped binding only).
- The violation flow stays shallow: popup + meter + [INF] thresholds; the retail fine/escalation data is not in
  the readable files.
- One unexplained 2.3 s update stall (parked run, frame 977, no log line around it) — `-hitchms` will catch it in
  live play; synchronous sector loads still stall the update by seconds when a new ring streams.

## Session 2026-10-30 (forty-sixth, `glm`): the realtor sells the houses, you walk into them, tuning and paint shops

Owner: *"on map there's auction house icons is there anything else around this interiors or anything ... i want
real estate and houses being wired up"*.

### What the data says
- The map's "auction house" icons are the **estates** icons = the **`EPH_*` houses themselves**
  (category 0x00, 19 records on Ibiza) and the `ERE_*` real-estate agencies (inside 0x08); `EGAMHA` is the
  gambling hall. The spot census is in `houses_spec.md` §1.
- **`DBHouse` (76 rows) is the whole market**: `_buyprice` $28,000..$7,000,000, `_sellprice`, localized
  `_housename_hc` ("Cala Moli residence"...), `_level` 0..5, `_nbcarslots` 0..8, `_realtor` (the agency's
  **full spots.bin name** `ERE_B_2236.._3336I` — its hash is the spot record id, the `FindShopBySpotId` rule),
  `_spotname` (the house's full spot name, same rule). `-loghouses` dumps the table (moved from
  gm_cin_viewer's private read into `house_shop.cpp`).
- **`interior/ire_b.bnk` (the realtor) is a standard shop bank**: `Realtor_area` selectable **Type 7
  "realtor"** (already in walk_mode's table), Exit 0, `C_INTRO_I` / `C_LIFE*` / `C_BUY_HOUSE`, PNJ Realtor.
- The house banks follow the shop layout with two rooms: **`lph_<level>` living** (in the house spot's frame,
  y 31..36 m — `HouseSpot` must stay set) + **`gph_<level>` garage** (own frame); living selectables
  `ExitDoor(0) Media(57) Garage(5) Diary(9) Dressing(10)`, garage `Living(6) ExitDoor(0) Billboard(14)` +
  **Entity__ShopData `D_GarageData`** (the car places `D_CAR_01..`); the furniture is
  **`Entity__MyHouseObject`** slots whose model is `interior/myhouse/<slot name>.bnk` (suffixed slots use the
  base bank; lph_l3di: 13 slots placed).

### What changed
- `source/game/house_shop.{h,cpp}` (new) — `GetHouses` / `FindHouseBySpotId` / `FindHousesByRealtor` /
  `IsHouseOwned`.
- `source/game/freeride/estate_menu.{h,cpp}` (new) — the realtor panel: the agency's houses with level /
  slots / price (red when unaffordable), **A buys** (`houses_owned` in profile.sav, `SaveProfile("house
  bought")`), **A twice on an owned row sells** at `_sellprice` (the last house is kept; the home moves).
- `source/game/spots/interior_walk.{h,cpp}` — `HouseBankFor` (the `l`/`g` bank rule, probed), `enterHouse`
  (living in the house frame via `HouseSpot`, garage closed-room; the intro is `C_INTRO`, the loops `C_LIFE*`
  — not `C_MYLIFE`), **room swaps**: A on 'garage' loads the garage + places your cars on its ShopData slots,
  A on 'living' comes back through `cam_entree_garage` (the door camera; `loadRoom` takes a preferred walk
  camera now); Diary / Dressing / Media / Billboard say "not wired yet". The intro-end path keeps the camera.
- `source/game/freeride/gm_freeride.{h,cpp}` — the Enter tile at a house only when **owned**
  (`findNearestSpot`'s filter receives the record id now); unowned → "This house is for sale..." hint; the
  `estates` / `tuning` / `carpaint` branches of `openSpotShop` (the estate menu side-panel inside the
  agency); `-home` (spawn at the home house's spots.bin **second point** — the road-side entrance, the field
  the Solar Crown target uses).
- `source/game/player_profile.{h,cpp}` — `OwnedHouses` (default: the story house `EPH_L3DI_5067I`),
  `HomeHouse`, `OwnedCarTuning` (accel/braking/speed 0..3); sav keys `houses_owned`, `home_house`,
  `car_tuning`; `NbHouses` follows the list.
- `source/game/map_screen.{h,cpp}` — owned `EPH_*` spots carry the **home** icon on the map (logged).
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` — `findHouseSpot` walks ibiza → hawai → casino spots.bin
  (the Hawai houses); `loadFurniture` (the `Entity__MyHouseObject` slots → myhouse banks placed like Actor
  props; `-nofurniture`).
- `source/game/freeride/tuning_menu.{h,cpp}`, `paint_menu.{h,cpp}` (new) — the tuning shop's three levels
  ($12,000 × next level [INF: DBStickersPricing names no tuning actions; `-logshops` dumps it], per owned
  car) and the paint shop's colour catalogue (the car's `DBCarsAndColors` rows, live
  `applyDatabasePaint`, saved to `OwnedCarPaints`). `car_shop.cpp` logs `DBStickersBanks` /
  `DBStickersPricing` under `-logshops`.

**Controls** (same as the shops): drive to a realtor / your house, **✕ / Enter** under 20 km/h; in the agency
aim at the realtor and **✕** for the house menu; in your house the garage door swaps to the garage (your cars
on the platforms) and the living door back; **○ / Esc** walks out. `-housegarage` enters the garage directly
(test).

### Verified (headless, `docs/re/houses_spec.md`, docs/images/*-s46.png)
- ERE_B_3336I (Ibiza, `-tp 21071,23472 -snaproad 60`): walked in, A on 'realtor' → **10 houses**; bought
  "Cala Moli residence" $283,300 → money $1,716,700, `houses_owned = EPH_L2E_0345I` written; the row reads
  "Yours"; estate-menu / estate-buy shots.
- Story house (owned by default): Enter tile → living room walked, **13 furniture slots**, lightmaps and the
  indoor pond (house-living-s46); `-housegarage` → the garage with the 3 seeded owned cars on its slots
  (house-garage-s46); A on 'living' → `interior/lph_l3di.bnk` again (log).
- Unowned house: the tile never shows (probe); with `-nointeriors` the hint fires.
- Tuning (ETUN_A): A on 'npc_tuner' → the panel, accel levels 1 and 2 bought ($12k / $24k, saved,
  tuning-menu-s46). Paint (ESTICK_B): 23 catalogue rows, 'Rosso Scuderia' applied **live** and saved
  (paint-menu-s46). Map: `'EPH_L2E' is home: home icon`. `-home`: "entrance at (36329.8, 5.2, 5743.8)".
- Regression: the Ferrari dealer path unchanged (8 display cars, A on 'carseller' → the dealer menu).

### Not done / next (houses_spec.md §8)
- The purchase cinematics (`C_BUY_HOUSE`, `C_BUY_PART`, `C_NEW_CAR_DESIGN_I`) and the retail estate /
  tuning / sticker movies; club houses (`ERE_CLUBS`); car storage per house; the MyHouseObject colours; the
  tuning levels' physics; decal banks; the hotel.

## Session 2026-10-29 (forty-fifth, `glm`, part 3): merged the hub's shop interiors; both shop paths verified

Owner: *"I think you need to update from hub as interiors are working now spawn outside and enter"*.

- `git merge win/win` (316f3c4): the shop interiors (`spot_interiors_spec.md`, `interior_walk.*`),
  the dealer display cars / colour picker / stock, schools + championships, the cline cheats + asset
  viewer, the x360 prototype tools. One conflict: STATE.md (both newest blocks kept). Reconfigured +
  clean build.
- Verified headless (`-tp <spot> -snaproad 150` + `press=accel` to the tile, `press=validate`):
  **ECD_FERR**: spawn on the street, Enter tile, A -> `icd_ferr.bnk` loads (collision 660 tris,
  `C_INTRO_I`, start camera `cam_entree`, selectables Exit + Hostess_area, display cars 1027/387/
  396/369/391/317/434/1030 on D_CAR_01..08) — bright showroom, hostess, glass doors showing the
  street (`_in_sheet.png`). **EAS_A** (the real clothes shop — the map 'clothes' icon spots named
  `EUC_*` are building sites, corrected by the hub): walk-in works the same, lit boutique with
  mannequins / podiums (`_cs_sheet.png`).
- The glm NIGHTLMAP fix survived the merge and still holds for the **exterior shells**
  (`_ab_ferr_merged.png`); the interiors carry authored lightmaps and register none (correct).
- Not done: interior mirrors (none seen in icd_ferr/ias_a frames; the retail FREERIDE has no
  GSReflection), building collision for the exterior shells.

## Session 2026-10-29 (forty-fifth, `glm`, part 4): the clothes-shop mirror reflects

Owner: *"When you was inside the clothes shop straight ahead as you enter is a mirror I didn't see
reflection"* (+ *"I think it uses render to texture or some hackery ... may even be camera that is
rendering to it from its position"* — exactly right).

### What was wrong
The shop mirror (`Mirrors_01`, TD_Furnitures `d5013ed0db79afaf`, **U7 = 1**, empty REFLECTION slot)
is a GSReflection **actor-layer** projector (`MIRROR`, n (-1,0,0) d -8.29; the entity's
`AvatarReflectlayername1` names it). The port's actor layers rendered **only the avatars** into the
reflect target — the mirror showed the player-at-most on black (A/B `-noactorreflect`: a 25x25 px
patch changed, nothing else). A horizontal diff of the planar law kept the room out of the target.

### What changed
- `eden_planar_reflections.{h,cpp}` — `DrawActors` now receives the projector; `render()` unchanged
  otherwise.
- `eden_scene_view.{h,cpp}` — new `ePass::ProjectorScene`; the projector callback routes **vertical
  actor layers** (`|Normal.y| < 0.5` — wall mirrors) to `drawObjects(ProjectorScene)`, which draws
  the whole scene (sets, props, avatars) from the mirror camera, clipped to the mirror plane.
  Horizontal actor layers keep the pool law (the avatars only — the pool water must not receive
  the mirrored room in its refraction sample).
- `eden_planar_reflections.cpp` — the projector log now lists each projector's object-node count.

### Verified
Walk into EAS_A headless: the mirror straight ahead now reflects the shop — the shoe podium, the
room, the ceiling (`docs/images/port-shop-mirror-s45.webp`; before: `port-shop-mirror-before-s45.png`;
the before/after diff bounding box is exactly the mirror quad).

### Not done / next
- The reflection reads slightly dim (MATPARAM.y 0.05 / the mirror_d tint) — compare against a
  retail grab and tune if the owner wants it brighter.
- The dealer interior (icd_ferr) had no wall mirror in the frames checked; other interiors may
  carry the same `Mirrors` pattern — the fix is generic (any vertical actor layer).

## Session 2026-10-29 (forty-fifth, `win`, part 7): cut content in the 360 prototypes — the Q7 Inuit interior, restored dials

A player who played the Dec 2010 prototype reported the Audi Q7 Inuit interior differs from launch. Confirmed and
partly restored — details in `docs/re/CROSSBUILD_PLAN.md` (prototypes section), tools in `tools/x360/`.

- Retail `q7_inuit_i` = the Q7 V12's cabin. Both prototypes (identical) carry the Inuit's own scene / geometry /
  materials, a grey-dial `Q7_INT2` and a `WOOD_INUIT` trim. Packaged for the owner: `C:\TDU2-Proto\q7_inuit_beta\`.
- **Restored content** (`texture_cache.cpp`): `<userdata>/restored/<bank>/<texture>.2db` replaces that bank's texture;
  `tools/x360/restore_texture.py` writes one from a prototype bank. `-norestored` = retail.
  Verified: `-car Q7_Inuit`, cockpit camera, `TextureCache: 'q7_int2.2db' of q7_inuit_i restored ...`, grey dials
  (`docs/images/q7-inuit-restored-dials.webp`).
- **Next**: (1) the full Inuit interior (360 `.3dg` vertex formats -> PC, `.3dd` / `.2dm` byte order, a PC bank);
  (2) a content scan of every prototype bank against retail (`.3dd` / `.2dm` sizes are platform-invariant, texture
  count, decoded pixels) to find the rest of the cut content.

## Session 2026-10-29 (forty-fifth, `win`, part 6): championships from the game's own race files

Owner: *"Yes, championships"*. Spec: `docs/re/licenses_spec.md` §5.

### What the data says
- The 27 DBLicense championships (9 Ibiza, 18 Hawai) list their races as `.ig2` files. The race mode is the name's
  last letter = the file's `gmth` = the title's prefix: **T** time attack (0, a real target `timG`), **R** race (3,
  7 opponents), **A** speedtrap (1 — the title says "Speedtrap:", not an eliminator), **S** speed (10).
- `lcpi` 0 = a circuit of `nblp` laps, 1 = point to point [INF from the course geometry].

### What changed
- `freeride/championship_menu.{h,cpp}` (new) — a championship's races, modes, targets / limits, laps, best times.
- `licenses.{h,cpp}` — `GetChampionships`, `ChampionshipFamily` (the HUD tile), `RaceModeLetter/Name`,
  `IsRaceRunnable` (not speed), `IsChampionshipComplete`, `IsChampionshipOpen` (+ reason).
- `freeride/ig2_challenge.{h,cpp}` — `getMode`, `isCircuit`, `getLaps`; a 1-gate file loads (speed challenges).
- `freeride/license_test.{h,cpp}` — a gate *sequence* (laps: the gates then the start line), the pass rule per mode,
  speed challenges refused.
- `gm_freeride.{h,cpp}` — `loadChampionshipSpots` (start = the first race's start gate, open / locked logged), the
  enter tile + markers, the menu, race prizes and the championship prize; `-championship <name part>`, `-allchamps`.

**Controls:** drive to a championship start (the asphalt / dirt / classic / cup tile shows within 40 m), stop, **✕ /
Enter**; ↑/↓ + **✕** to race; **○ / Esc** to leave.

### Verified
- Lock states from the log, profile with the A7 licence only: Ibiza city / West area / North coast open, Santa Eulària
  "Complete Championship: North coast first", B4 / C4 "You need the ... licence", the Cup "Complete every championship".
- "Championship: Ibiza city" through `-licensetest <race> -licensewarp`: Time: Old town, Race: Salt marsh challenge,
  Race: Ibiza beaches, Speedtrap: Seaport run, Race: City center loop (circuit, 7 gates x 2 laps = 16) -> `race ... won`
  (+$2,500 each) and `championship 'Championship: Ibiza city' complete (+$75000)`; the speed race is listed "Not available
  yet". `docs/images/championships-s45.webp` (the high championship's tile, the race list, a circuit race "gate 1/16").

### Ruled out
- 'A' = eliminator (my first guess from `NbIA` 7): the races' own titles are "Speedtrap: ...".

### Not done / next
- **AI opponents** (races have no placing), the speedtrap score (`SP2R`), the speed mode (`SPPG`), the race car (`VMod`),
  championship cinematics (`sta_firstintro`, `sta_winloosepodium`), map icons. Then the `EVI`/`EVH` events (`gmth` 7).

## Session 2026-10-29 (forty-fifth, `win`, part 5): racing schools and licences, from the game's own test files

Owner: *"can we try plumming in the driving schools and licenses"*. Spec: `docs/re/licenses_spec.md`.

### What the data says
- `DBLicense`: 7 licences (A7 / B4 / C4 on Ibiza; A5-A4, A3-A2, A1, B3, C3 on Hawai) + 27 championships /
  cups; each row lists its tests / races as `_challengefile_n`.
- **The challenge files are readable**: `Euro/Bnk/ig2/<name>.ig2` (538 files, big endian `IGE2` / `MDSC` keys /
  `ENTI` entities). A test has its target (`timG`), limit (`CtDw`), car (`VMod`), name / description loc ids
  (`dbna` / `dbd1`) and its gates (`CHK2`, two layouts). Championships (`CH*`) and events (`EVI`/`EVH`) use it too.
- Each school spot (`ERS_A/B/C`) stands beside its own tests: the letter maps the school to `RS<letter>…`.

### What changed
- `game/licenses.{h,cpp}` (new) — DBLicense, school licences, obtained / passed helpers; `-loglicenses`.
- `freeride/ig2_challenge.{h,cpp}` (new) — the `.ig2` reader; `-logig2`.
- `freeride/license_test.{h,cpp}` (new) — the run: countdown, gates by plane crossing, limit / target.
- `freeride/school_menu.{h,cpp}` (new) — licences then tests, locks, best times.
- `gm_freeride.{h,cpp}` — "rschool" spots open the school menu at the instructor; `startLicenseTest` (out of
  the school, onto the start gate), `placeCar`, `tickLicenseTest` (HUD countdown / timer / gate markers, result,
  save); the race block no longer hides the countdown under a licence test; `-licensetest`, `-licensewarp`,
  `-schoolmenu`.
- `player_profile.{h,cpp}` — `PassedTests` (`tests_passed = name:best,...`).

**Controls:** drive to a racing school (map tab 0), **✕ / Enter** to walk in, walk to the instructor, **✕** at
him; ↑/↓ + **✕** pick a licence then a test; the car is put on the start line: 3-2-1-GO, drive through the
gates. **○ / Esc** backs out of the menus.

### Verified
    build\bin\opentdu2.exe -game ... -userdata C:\temp\otdu2-win-painttest -gm FREERIDE -skipcine -nopad -fixedtimestep
        -tp 18173.6,27306.0 -snaproad 100 -nointeriorintro -walkat "-17.0,7.2,6.7,-90"
        -autoplay "160:press=validate;200:look=0,0.25;204:look=0,0;230:press=validate;300:press=validate;360:press=validate"
- `InteriorWalk: A on 'tutor'`, `SchoolMenu: school A, island 0: 1 licence(s)`, the A7 tests by name,
  `LicenseTest: RSA7T02 'Slaloms' - 12 gate(s)` from its start gate.
- All 7 A7 tests passed through `-licensewarp` (both gate layouts; `RSA7T04` 9 road gates) -> `FREERIDE: licence A7
  obtained`, profile `tests_passed` with 7 entries; Hawai school A: 3 licences, the 2nd / 3rd Locked.
- Real driving: throttle held straight down the slalom -> `Missed a gate` at 7.1 s.
- `docs/images/racing-school-licences-s45.webp` (instructor, test list, countdown, timer + gate count, the result
  and "Licence A7 obtained!", Obtained / Locked lists, the start line).

### Ruled out
- `RSA7T03` "missed" gate 13 under `-licensewarp`: the warp's straight hop cut a corner gate (it faces the
  corner's bisector), not the gate law — the hook now approaches from the car's own side and the test passes.
- The countdown stuck on "3": the race block hid the countdown every frame and the licence re-showed it,
  restarting the label — the race block now leaves the HUD to an active licence test.

### Not done / next
- **Championships / events from the `.ig2` files** (the `CH*` races of DBLicense, the `EVI`/`EVH` events) — the
  reader is ready; RaceEvents' synthesized routes could be replaced. Start at `RaceEvents::load`.
- The test car (`VMod`, needs `-switchcar`), `DEC2` cones, the school cinematics (`C_WIN_LICENSE`), rewards / fees,
  licence gating of events and cross-island requirements.

## Session 2026-10-29 (forty-fifth, `win`, part 4): the dealer's colour picker

Owner: *"Yes, colour picker"* (choose the paint when buying a car). Spec: `docs/re/spot_interiors_spec.md`
§4 "The colour picker".

### What changed
- `dealer_menu.{h,cpp}` — a colour step (the car's DBCarsAndColors names + `_price` extras), buy in a paint,
  `takePreview` (the car / colour to show), `setSidePanel` (right-hand panel, scene undimmed, in a showroom).
- `intro_cinematic.{h,cpp}` — `focusDisplayCar( carId, paint )`: repaint the podium car, frame it (PosCam Camera
  if the bank has it, else a three-quarter view [INF]); PosCam -> Camera names read.
- `interior_walk.{h,cpp}` — `previewCar` / `endPreview`.
- `cinematic_car.{h,cpp}` — `DatabasePaints( carId )` (name + price per row).
- `player_profile.{h,cpp}` — `OwnedCarPaints`, saved as `car_paints = id:row,...`.
- `gm_freeride.{h,cpp}` — the preview each frame the dealer menu is up, the walk camera back on close,
  `applyOwnedPaint()` after every car load (enter, `switchCar`).

**Controls:** at the dealer: ↑/↓ (D-pad / arrows) pick a car, **✕ / Enter** opens its colours, ↑/↓ repaints
the car on its podium, **✕ / Enter** buys it in that colour, **○ / Esc** back to the cars, again to leave.

### Verified (profile with a name: `-gm` runs without one do not save)
    build\bin\opentdu2.exe -game ... -userdata C:\temp\otdu2-win-painttest -gm FREERIDE -skipcine -nopad -fixedtimestep
        -tp 21796.3,24188.1 -snaproad 150 -nointeriorintro -walkat "0.5,1.6,5.5,66"
        -autoplay "160:press=validate;230:press=validate;300:press=down;360:press=validate;380:press=down;390:press=down;400:press=down;460:press=validate;510:press=back"
- `CarPaint: California ... row 0 'Rosso Scuderia' / row 1 'Grigio Titanio' / row 2 'Nuovo Grigio Ingrid' / row 3
  'Grigio Alloy'` (the live preview), `Dealer: bought 'California' paint 3 ' in Grigio Alloy' for $191000`,
  profile.sav `car_paints = 387:3`; the next run with `-car California`: `'California' in its ordered paint 3
  'Grigio Alloy'` — `docs/images/dealer-colour-picker-s45.webp` (612 / California framed, the colour list, bought,
  red stock car vs the Grigio Alloy one on the road).

### Not done / next
- The bought car is not the driven car until the runtime model change works (`-switchcar`, car_delivery.md);
  the paint is applied whenever the car is loaded, so it shows once that lands. FREERIDE does not yet pick the
  driven car from the profile (only `-car`).
- The walk reticle stays on screen during the preview; the colour list has no swatch (names only).

## Session 2026-10-29 (forty-fifth, `win`, part 3): the display cars in their catalogue colours

Owner: *"Car colours"*. Spec: `docs/re/spot_interiors_spec.md` §4 "Display car paint" (the paint path of
`docs/re/paint_spec.md`, which no code implemented until now).

### What changed
- `cinematic_car.{h,cpp}` — `applyDatabasePaint( index )`: the car's DBCarsAndColors row -> its `_color_id_1` /
  `_color_id_2` material in `<car>_color.2dm` or `colors.2dm` (**by hash**: names are cut to 15 characters) ->
  the paint parameters onto `Paint` / `Paint_SK` / `Paint_LR` (and `Paint2` for colour 2); `CountDatabasePaints`;
  `-logpaint`.
- `intro_cinematic.{h,cpp}` — `addDisplayCar( desc, posCam, paintIndex )`.
- `interior_walk.cpp` — podium i gets paint row i [INF]; `-paintindex <n>` forces one row.
- `car_shop.cpp` — `-logshops` reports a slot whose car is missing (none is).

### Verified
- Ferrari showroom: `612_Scaglietti painted 'Rosso Corsa'`, `California 'Grigio Titanio'`, `16M 'Giallo'
  (Spider_16M_Ext_Jaune_01)`, `F430_Scuderia 'Grigio Alloy' + F430_Scuderia_Ext_Gris_09`, `458 'Blu Abu Dhabi'`,
  `599 GTB 'Bianco Avus'`, `599 GTO 'Nuovo Blu Pozzi'`, `Enzo 'Giallo Modena'` — `docs/images/car-paint-s45.webp`.
- World classic with `-paintindex 5`: Mustang 'Wimbledon White', Corvette 'Polo White' (both visibly white);
  `-paintindex 0` is pixel-identical to the unpainted cars (row 0 = the .2dm's own colour).

### Ruled out
- Painting every RAMPCLR1 material: that includes glass / chrome / trims (27 materials on the 612). Painting every
  TD_CarPaint2: that includes carbon and liveries (16M stripes, Scuderia `Bande_Noir`).
- Missing DLC cars behind the empty podiums: no shop slot names an unknown car.

### Not done / next
- Callipers (`_callipers` -> STIR materials, FUN_00c5bb40) and interior colours (`_interior_n`); the dealer menu
  could offer the colour list (the rows' `_color_name`, `_price`) and buy the car in the chosen paint.





## Session 2026-10-29 (Cline, `cline`, sixth): the dealership delivery - the car is parked outside (the runtime car change hangs, opt-in)

The owner: *"buying vehicles is broken - the cashier is meant to have a dialog
with keys and then spawn outside with that vehicle"*. The purchase itself was
fine (`DealerMenu`: money, `OwnedCars`, the `COLLECTION_CARS_DIF_n` event, all
saved); what was missing is the **delivery** - nothing appeared outside.

### What changed
- **`source/game/freeride/car_delivery.{h,cpp}` (new)**: the shop's own car
  positions, the `Entity__CarPos` entities of the **exterior** bank
  (`islands/<island>/exterior/<bank>.bnk`), which the port never read. `[MEASURED]`
  on Ibiza: `ECD_FERR` has one, `PosCarParking` (type **4**, player **1** - the
  player's own spot); `ECD_AUDI` has it among four `PosCarTraffic*` (type 5,
  player **2001**); `ECD_US1` / `ECD_ITAL` / `ECDC_ITA` / `ECD_MERC` (which also
  has `BrandCar`) / `ECD_BRI1` / `ECD_ASIA` have only traffic / brand positions.
  The pose is transformed by the spot frame (`T(pos) R_y(heading)`, the spot
  exteriors' rule). `-carpos <spot>[,<spot>]` dumps them.
- **`DealerMenu::takeBoughtCar` + `GMFreeRide::deliverBoughtCar`**: when the
  menu closes, the bought car goes to the shop's parking (type 4 → `*Parking*` →
  the first → the spot frame), the ground under it from the collision ray
  (Havok first, `EdenWorld::getSurfaceHeight` second), and the player's car is
  put there - "buy, leave, the car is outside". Verified: `-dealer ECD_FERR`
  (the flag now takes a spot name too) + a purchase logs
  `delivering '612_Scaglietti' to 'PosCarParking' (ECD_FERR_7841) at (21803.0, 11.3, 24210.8) heading -76 deg`
  and the game keeps running (perf frames continue, screenshot taken).
- **The runtime car change is opt-in (`-switchcar`), because it wedges the
  device.** Unloading the driven car's model and loading the new one (with the
  HUD gauges, the vehicle sounds, `setupVehicle`, the chase camera) stops the
  game responding a few frames later - CPU idle, a GPU wait, reproduced twice
  (`-dealer ECD_FERR` + one purchase). `waitIdle()`, `clearShadowCasterCache()`
  and `clearRtInstances()` were all tried; none cured it. Bisected with
  `-switchparts model,audio,hud`: **the model swap is the culprit** (pose only is
  stable). The default therefore moves the car the player already drives to the
  delivery spot (stable), and the doc says so (`docs/re/car_delivery.md`).
- Still not done (as before): the cashier's purchase cinematic (`C_BUY_CAR_I`,
  the hostess's "give the key" FaceFX) and the `shopscar.vmf` dialog - the
  native menu stands in. That belongs with the hub's interiors work
  (`spot_interiors_spec.md` §5).

## Session 2026-10-29 (Cline, `cline`, fifth): the developer cheats - money, teleport, the retail dev menu's delock rows

The owner asked for a cheats menu to test buying cars and to be able to pick a
place on the map and go there, after the community pointed at the retail build's
**dev features** (the PC mod "DevLogicModding" cheat menu: DELOCK / MONEY /
ACHIEVEMENTS / PROGRESSION tabs, the rows ALL CARS, ALL HOUSES, ALL ROADS, ALL
CLOTHES, POSITION/SPOT, CARS ON STOCK, ALL CAR DEALERS, and "TARGET NEAREST
LOCKED ROAD"; the way in is a pad code, "spam your D-pad in circles while
mashing Square or X"). **The port had none of it** - the retail menu lives in
the retail build (and the mod re-enables it). What the port *does* have is the
data those rows write: `docs/re/savegame_spec.md` names `RoadLock.Locks`,
`ClassicCarStock`, `AreaRoadsUnlocked` / `AreaTotalRoads`, `MyHouse`, `Rewards`,
`AirportUnlocked`, and the port already models the equivalents
(`PlayerProfile::OwnedCars` / `NbHouses` / `ExploredRoads`, the casino
unlockables, `DealerMenu`'s stock, `EventsDone`). New: `source/game/cheats.{h,cpp}`
+ `docs/re/cheats.md`.

### What changed
- **The panel** (`Cheats::drawUI`, drawn from `GameMode::drawUI` by FREERIDE and
  CASINO, so it works while the map or the pause screen is up): the wallet
  (money / casino chips: set, +1M, +10M, +100k, saved to the profile at once -
  this is the wallet `DealerMenu::buySelected` spends, so a car can be bought
  right after), the teleport, the weather preset, the story step, "back on
  track", and the delock rows. **F4** toggles it, `-cheatspanel` opens it at
  startup.
- **The pad code** (`Cheats::feedPadCode`): the retail dev-feature way in - a
  circle of the D-pad (up, right, down, left) three times with the option button
  (X / square) pressed during it. `[INF]`: the community's description of the
  retail code, the retail sequence itself is not decoded; the D-pad circle also
  opens the accessories wheel, so the code only watches the order of the
  presses. Verified with the autoplay (`press=accup;press=accright;...;press=opt1`
  x3): "Cheats: pad code: 3 circle(s)" -> "Cheats: panel open (pad code)".
- **Teleport** (`GMFreeRide::teleportTo`): the world streams at the target
  (`streamWorld`), the surface under it comes from the Havok collision ray
  first and `EdenWorld::getSurfaceHeight` second (a point in the open sea keeps
  the target height - that is how the retail "teleport to Dubai" freeze
  happens), then `VehicleDynamics::reset` + `ChaseCamera::reset` +
  `lastSectorKey` / `lastMapStreamKey` cleared so the next tick re-streams.
  Three ways in: the island's `spots.bin` buildings (79 on Ibiza, a text
  filter, double click = go), raw x / z / heading, and **F5 on the map** -
  `MapScreen::getCursorWorld()`, the world point the cursor is over (the same
  value the GPS destination uses; `getCursorWorld` / `isCursorOverLand` are new
  public accessors). `-tpin x,z[,heading]` exercises the in-game path headlessly
  (`-tp` only moves the spawn).
- **The delock rows**: ALL CARS (`OwnedCars` from `GetCarShops()`, 83 on
  Ibiza, `NbCars = owned + 1` - the starter car is not in the list), ALL HOUSES
  (the island's `EPH_*` spots, 20), ALL ROADS (`MapScreen::unlockAllRoads`:
  every link of the road graph explored, the per-area counters moved with it so
  the area milestone events fire, `ExploredRoads` saved), CARS ON STOCK
  (`DealerMenu` treats every `DBCarShop` row as buyable), ALL ACHIEVEMENTS
  (every `DBRewardXP` event awarded once, 189 of 191), TARGET NEAREST LOCKED
  ROAD (`MapScreen::targetNearestUnexplored`: the closest link the player has
  not driven, routed with the map's own `setTarget`). The two rows the retail
  menu has that have nothing to unlock here (ALL CLOTHES, ALL CAR DEALERS) are
  documented as such in the panel and in `docs/re/cheats.md`.
- **Command line**: `-money N` / `-chips N` (the wallet at startup, any mode,
  applied once in `tdu_instance.cpp` after `LoadProfile`), `-cheatspanel`,
  `-cheat <row>[,<row>]` (`allcars`, `houses`, `allroads`, `buyable`,
  `achievements`), `-tpin x,z[,heading]`;
  `launch/15_Freeride_Cheats.bat` starts FREERIDE with the panel up, a full
  wallet and the delock rows applied.

### Verified (headless, `C:\temp\otdu2-cline-userdata`)
- `-money 5000000` + `-tpin 1000,2000`: `Cheats: money = $5,000,000`,
  `Cheats: 79 spot(s) on 'ibiza' for the teleport list`,
  `FREERIDE: teleport to (1000.0, 0.6, 2000.0), heading -110 deg` (a sea point:
  the surface ray finds nothing, so the target height is kept), and
  `profile.sav` carries `money = 5000000`.
- `-tpin 36000,5800` (inland, next to the spawn): `teleport to (36000.0, 6.0,
  5800.0)` - the ray found the road at y 5.4, so the drop is measured, not
  guessed.
- **The map flow** (`-autoplay "150:press=map;270:key=F5;300:shot=..."`):
  `FREERIDE: teleport to (21504.0, 63.6, 18432.0)` - the car went to the world
  point under the cursor, 63.6 m being the terrain height there - and the
  screenshot shows the map with the car marker exactly at the cursor.
- `-cheat allcars,houses,allroads,buyable,achievements`:
  `Cheats: all cars: 83 added, 83 owned, NbCars 84`,
  `Cheats: all cars buyable = on`,
  `Cheats: achievements: 189 of 191 event(s) awarded`, `Cheats: houses = 20`,
  `MapScreen: all roads unlocked: 0 -> 1350 of 1350 link(s), 906758 m, 100%`
  (+ `Profile: saved (cheat: all roads)`).
- The casino panel (`-gm casino -chips 250000 -cheatspanel`): the chips reach
  the floor screen (`250000 chips`), and the teleport section hides itself (no
  world). Screenshots: `docs/images/cheats_panel.webp` (FREERIDE),
  `docs/images/cheats_panel_casino.webp`.

### Notes
- The panel is the port's answer to "enable the dev features": the retail dev
  menu is not in the port (it is a retail-build feature the mod re-enables); the
  port's version is a native ImGui panel plus command line flags, so every row
  is testable headlessly.
- `-cheat allroads` awards the area milestone events exactly like driving the
  roads would; that is the point (testing the road / Discovery features).
- `clang-format` was not run over the tree (it is not clean on it, see the
  earlier sessions); `source/game/cheats.*` follows the neighbouring files'
  style.


The `win` session tested the merged viewer on a bank it had not seen
(`euro/bnk/interior/icd_ferr.bnk`) and left seven items in
`_feedback_asset_viewer.md` (untracked in the hub; copied to `_feedback_asset_viewer.md`
here). All seven are done, plus the labels the interiors work had verified by hand.

### What changed
- **Search across all banks (item 1).** `-buildindex` walks every bank once and writes
  `<userdata>/asset_index.txt` (`a<TAB>bank<TAB>asset<TAB>authoring path` and
  `e<TAB>bank<TAB>entity class<TAB>name<TAB>pos`): `[MEASURED]` 9,452 banks / 153,690
  asset entries / 10,708 entities in **33 s**, a 30 MB file. The Search tab loads it back
  and matches the query (bank, name or authoring path, case insensitive) - `ferrari` gives
  913 hits - and a click opens the bank and selects the asset (or the entity). The bank
  list's language copies (`__fr`, `__ge`...) are hidden until the checkbox is ticked
  (item 6). *A follow-up fix the same session: an entity hit read one field past the
  vector (an intermittent 0xC0000409) - see "Ruled out".*
- **An Entities tab (item 2).** `entities.xmb`'s `ExportType_Entity.ArrayOf_Entity__*`
  objects by class (a class combo + a text filter), and the selected one shows **every
  field** with nested composites walked (`orient_pos_scale.pos.X`, `Fov`, `IDClass`,
  `StartCamera`...) - `XmbValue::getDocument()` was added to `core/xmb.h` so a tool can
  walk a document generically. The selected entity is marked in the 3D view (a dot + its
  name; a camera also draws its **frustum** from its own Fov / Ratio).
- **Cameras inside room banks (item 3).** The 3D view has a **view from camera...** combo
  (the bank's `Entity__Camera`s: `cam_entree` is in every shop interior) - position,
  rotation and Fov from the entity, the walk-mode convention - and a **free camera**
  (WASD / QE, Shift x4, drag looks, wheel changes the speed) that starts where the view
  is. `-camera <name>` and `-entity <name>` drive both headlessly.
- **Layout at 1280x720 (item 4).** The right column is the preview + label panel (identity
  and the label editor in the left column, the preview beside it) over a much taller 3D
  view: the 3D image went from 349x262 to ~500x380 for the same window, and the label
  editor is above the fold (it used to be cut off).
- **A type filter on the assets list (item 5)**: all / models / textures / movies / sounds
  / anims / cine / collision, two rows of radio buttons.
- **Small things (item 7)**: `launch/14_AssetViewer.bat` uses a workspace neutral
  `-userdata C:\temp\otdu2-userdata` (which is also what caches the index, so one build
  serves every tree); `.cin` / `.sce` show their duration, shots, scenes and actors
  (`Cinematic::parse` / `CinematicScene::parseScene`); `.shk` / `.dhk` draw the collision
  as a wireframe overlay (`LoadHavokCollision`: boxes + triangles, capped at 4,000
  triangles, projected with the view-projection the GPU used).
- `docs/re/asset_labels.txt`: **+5 labels** the interiors work verified (Ferrari showroom,
  clothes shop A, hairdresser, **club house (not the car rental)**, building site) -
  written by the viewer, so the file stays TSV + sorted.

### Verified
    build-cline\bin\opentdu2-cline.exe ... -assets -buildindex                      # 33 s, 153690 assets
    ... -assets -search ferrari -exitframe 40                                        # 913 hit(s)
    ... -assets -bank euro/bnk/interior/icd_ferr.bnk -entity cam_entree -camera cam_entree -collision
    ... -assets -bank euro/bnk/scenarics/cinematics/intro/new_intro.bnk -asset intro_lph_l3di_M_INTRO_TOTEM.cin
    ... -assets -labellint                                                          # 12 labels, 0 problems

- the index: `AssetIndex: ... written: 9452 bank(s), 153690 asset(s), 10708 entity(ies) in
  33 s`; `AssetIndex: 164398 line(s) ... (153690 asset(s), 10708 entity(ies))`;
  `AssetViewer: search 'ferrari' -> 913 hit(s)`.
- the interior: `25 entity(ies) in 10 class array(s)`, `2 entity camera(s), first
  'cam_entree' at (-8.19, 1.79, 1.77)`, `view from 'cam_entree' ..., fov 40.0 deg`,
  `collision 'icd_ferr_sc_57E5C829CCD9F2C6.shk: 660 triangle(s), 42 box(es), 9 capsule(s),
  61 body(ies)'`.
- the cinematic: `cinematic: 52.6 s, 17 shot(s), 2 scene(s), 34 message(s), 4 fx event(s)`.
- `-labellint` -> `12 label(s), 0 problem(s)`.
- screenshots: `docs/images/asset_viewer_search.png` (the Search tab), `asset_viewer_entities.webp`
  (the Entities tab + the 3D view from `cam_entree`), `asset_viewer_interior.webp` (the
  showroom from inside, collision wireframe on).

### Ruled out (with the test that ruled it out)
- **The `-search` crash was a real bug, not the leftover processes.** `runSearch` read `fields[4]` on a four
  element vector for every **entity** hit (`e<TAB>bank<TAB>class<TAB>name<TAB>pos`): an out of bounds
  `std::string` read that raised 0xC0000409 *intermittently* (it depended on what the allocator had left
  there, which is why the same command sometimes worked). Fixed to `fields[2]` (the name) and `fields[3]`
  (the position); `-search cam_entree` (140 entity hits) and `-search ferrari` (913) both exit 0. Leftover
  `opentdu2-cline.exe` instances from killed runs are a separate, real problem (they hold the log files and
  burn CPU): kill them by exact name and keep one run at a time.
- **A run that outlives the shell's 30 s is killed mid-frame** (no screenshot, a truncated log): keep the runs
  short (`-exitframe` <= 45, the shot ~10 frames before the exit) or start them with `Start-Process` and poll
  the log.
- **`.3dd` authoring paths are not bank paths**: the viewer's `-bank` wants the path relative to the *game
  root* (`euro/bnk/scenarics/...`), not the `Euro/...` form the authoring path shows.

### Not done / next
- Click-to-label inside a streamed island / spot scene (needs an `EdenWorld` /
  `SpotExterior` accessor for the drawn models' identities), entity **volumes as boxes**
  (`volume_mat` inverse), cube maps as a cross or a small lit cube
  (`docs/re/asset_viewer.md` §5).
- The index is a snapshot: after a mod, `-buildindex` again (a stale hit says so).


## Session 2026-10-29 (forty-fifth, `win`, part 2): each shop's own stock — cars on the podiums, clothes by brand

Owner: *"continue with adding cars to the correct dealerships and clothes to correct stores"*. Spec:
`docs/re/spot_interiors_spec.md` §4.

### What the data says
- `DBCarShop._spot_name` names the exact building of every shop row (`ECD_FERR_3337I`); the spots.bin record id
  is `GetIdentifier64bit` of it. `_clothesshopbrand` gives the clothes shops their brand (Hiro Hisaishi, MYW Paris,
  Practice, B-Miami, Casino), matching `DBClothe._brand`. The interior bank's `Entity__ShopData.PosCams[]` are the
  podiums. The old name-guessing picked the wrong shop for `ECD_US1` and `ECDC_WLD`.

### What changed
- `car_shop.{h,cpp}` — `CarShop::SpotName` / `ClothesBrand`, `FindShopBySpotId`; `-logshops` logs every row.
- `clothes_shop.{h,cpp}` — the brand name read as a string (it was always blank), `BrandHashcode` / `Sex` per item,
  `ClothesMenu::open( name, brand, sex )` (one shop's stock), `FindBrandHashcode`, `PlayerClothesSex`.
- `cinematic_car.{h,cpp}` — `CinematicCar::DescFromDatabase( carId )` (car file + default rims + tire).
- `intro_cinematic.{h,cpp}` — `Entity__ShopData` PosCams, `addDisplayCar` (rest on the PosCam, the bank's CUBECAR).
- `interior_walk.{h,cpp}` — `placeDisplayCars`; `-nodisplaycars`.
- `map_screen.{h,cpp}` — the spot record id in `Spot`, out of `findNearestSpot`; `-logbrands` prints the hashcode.
- `gm_freeride.{h,cpp}` — `findSpotCarShop` (by id, name rule as fallback) and `openSpotShop` (dealer / clothes /
  **hairdresser**, which now sells the HAIR items instead of "not open yet").

### Verified
- Ferrari (`-tp 21796.3,24188.1 -snaproad 150`): `8 display car(s) of 8, 8 place(s)` — 612 Scaglietti, California,
  430 Scuderia Spider 16M, 430 Scuderia, 458 Italia, 599 GTB, 599 GTO, Enzo on `D_CAR_01..08`.
- World classic (ECD_US1, `-tp 18059.9,9716.5 -snaproad 100`): Mustang Fastback '68 + Corvette C1 (its 2 cars).
- Hiro Hisaishi (EAS_A): `brand 0x39 sex 2: 21 item(s)`; hairdresser (EHD_B): `brand 0x37 sex 2: 27 item(s)`.
- `docs/images/shop-stock-s45.webp`; entering the Ferrari showroom is one ~0.8 s frame (8 cars), 15 ms/frame inside.

### Not done / next
- Shops with fewer cars than places (World classic 2 of 8) leave podiums empty — check `DBSpot_DLC` / DLC rows.
- Display cars in the default paint (`DBCarsAndColors` not read); clothes shown as a list, not on mannequins /
  the player (the `C_tops` / `C_shoes` category cameras are the retail try-on shots).

## Session 2026-10-29 (forty-fifth, `win`): walk into the dealerships, clothes shops and every other shop

Owner: *"look into interior walk mode for buildings this being dealerships, clothstores etc... should be able to
enter the buildings and walk around them"*. Spec: `docs/re/spot_interiors_spec.md`.

### What the data says
- Every shop spot `E<name>` has an interior bank `interior/i<name>.bnk` (ECD_FERR -> icd_ferr, EAS_A -> ias_a,
  EHD_B -> ihd_b, ESHOP_CA -> ishop_ca), all three islands [DATA]. The banks are the garage / house layout the
  walk modes already load: Scenery set, `.shk` collision, `Entity__FirstPerson { StartCamera }` (`cam_entree`),
  FpSelectables (Exit type 0, the seller type 1 / 22 / 29), the staff PNJ, `C_INTRO_I` + `C_LIFE*` cinematics.
- **The map's spot categories 3 and 5 were wrong** [DATA: source paths inside the banks]: 3 = `spots\avatshp`
  (the real clothes shops, was "auctionhall"), 5 = `spots\clubhous` (was "carrental"), and `EUC_*` (which
  carried the "clothes" icon and the FREERIDE clothes prompt) are `spots\under_construction` building sites.

### What changed
- `source/game/spots/interior_walk.{h,cpp}` (new) — `InteriorWalk`: loads the interior bank through
  `IntroCinematic` as a closed room (`bWorld = false`, the FIRST_WM setup), plays `C_INTRO_I` (A / B skip) and
  loops the `LIFE` / `LOOP` cinematics, then `WalkMode` from the FirstPerson camera; A on the seller -> `Shop`,
  A on the exit or B -> `Leave`. `InteriorBankFor()` caches the file probe.
- `source/game/freeride/gm_freeride.{h,cpp}` — the Enter tile now shows for the nearest spot **with an interior**
  (any kind; the tile art per kind is HUD.vmf's own); A walks in; inside, the island / car are frozen and the
  engine muted, the interior replaces the 3D view and the HUD; the seller opens the **dealer menu** (car dealers)
  or the **clothes menu** (clothes shops) over the interior; other shops say "This shop is not open yet."
  `-nointeriors` restores the menu-from-the-car behaviour; `-nointeriorintro` skips the entry shot.
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` — `getFirstPersonCamera()` (Entity__FirstPerson ->
  camera name by `__Hc`), `getCinematicNames()`.
- `source/game/map_screen.{h,cpp}` — category 3 -> `clothes`, 5 -> `club`, `EUC_*` no icon;
  `findNearestSpot( position, radius, filter, ... )`.
- `source/game/walkmode/walk_mode.cpp` — `-logselect` logs why each selectable is refused.

**Controls** (DualSense / keyboard): drive up to a shop under 20 km/h, **✕ / Enter** to walk in (✕ / Esc skips
the entry shot); **left stick / WASD** walk, **right stick / mouse** look; aim the reticle at the seller (look
slightly down: the volumes sit at counter height) and **✕ / Enter** for the shop menu; **○ / Esc** (or ✕ on the
door) to walk out — you are back in the car.

### Verified
    build\bin\opentdu2.exe -game ... -gm FREERIDE -skipcine -nopad -fixedtimestep -tp 21796.3,24188.1 -snaproad 150 ^
        -storytime 86400 -autoplay "160:press=validate;260:press=validate;340:hold=accel;420:release=accel" -logwalk
- Ferrari (ECD_FERR): `InteriorWalk: 'ECD_FERR' (interior/icd_ferr.bnk): cinematics C_LIFE_HOSTESS C_INTRO_I
  C_BUY_CAR_I; start camera 'cam_entree'; 2 selectables`, `WalkMode: cam_entree ... -> eye 1.600`, the eye moves
  on accel; with `-walkat "0.5,1.6,5.5,66"` A -> `A on 'carseller'` -> the Ferrari list over the showroom; B, B ->
  `FREERIDE: back in the car`.
- Clothes shop (EAS_A, `-tp 21755.6,23579.2 -snaproad 60`): entry shot, walk, `A on 'goodies'` -> `Clothes: 327
  valid DBClothe items` menu (with `-walkat "2.5,2.5,5.0,48"` and `look=0,0.3` for 4 frames).
- Hairdresser (EHD_B, `-tp 9624.6,17036.9 -snaproad 60`): 3 chairs + ExitDoor, `A on 'hairdresser'` -> the Tip box.
- `docs/images/interior-walk-s45.webp`: Ferrari entry / walk / hostess / dealer menu; clothes entry / walk / menu;
  hairdresser hint / entry / walk; the Enter tiles outside (clothes and dealer art).

### Ruled out
- The seller "not selectable" in the clothes shop was **not** occlusion: `-logselect` showed `not under the
  reticle` with a level gaze (eye 1.6 m above the floor, `Seller_area` centre 0.78 m above it); a slight look
  down selects it. Same law as the readable selection code, so not changed.

### Not done / next
- **No display cars on the dealer podiums**: placed from `Entity__ShopData` / DBCarShop by `[ENC]` shop code —
  start at the PS3 twin of the shop code (`docs/re/ps3/`), the entity is `Entity__ShopData` in `icd_ferr.bnk`.
- Hair / surgery / club / tuning / car wash / paint / realtor / racing school / photo / airport / stolen car:
  walkable, no menu. Purchase cinematics (`C_BUY_*`), clothes category cameras and the retail `shopscar.vmf` /
  `shopsavatar.vmf` UIs are not driven.
- Houses (`lph_*` / `gph_*`) are not entered from FREERIDE; the interior is not placed in the island.
- Staff seen from across the showroom look very dark (fine up close) — PNJ lighting in a closed room, not
  investigated.

## Session 2026-10-29 (Cline, `cline`, third): the asset viewer previews the assets, and a click labels the one you point at

Owner: *"could a tool be made like an asset viewer which I can view and leave hints to them maybe in some .txt ... I
load a casino but file naming maybe random and I can leave hints like casino exterior, slot machine, Tess character
model etc?"* — the tool existed (`docs/re/asset_viewer.md`, follow-up 3 of the previous session); this session
finished what it was missing (§5 of that spec): **model previews** and **click-to-label**, plus movie / audio / cube
map previews.

### What changed
- `source/render/eden/eden_model.{h,cpp}` — `Item::NumVertices` and `Item::CollisionFirst/Count` (the triangle range
  of each item in the CPU collision copy), `getCollisionBounds()`, and **`raycast(origin, direction, distance,
  node)`**: Möller-Trumbore over the collision triangles (the space of `draw`'s `world` matrix), a node box fallback
  when `setKeepCollision` was not set. Nothing else calls it, so no mode changed.
- `source/game/assetviewer/gm_asset_viewer.{h,cpp}` — the **3D view**: `loadSceneModels` (every `.3dd` of the open
  bank through `EdenModel::load`, the `SpotExterior` pattern: the `.3dg` / `.2dm` of the same name, the island's
  `commonworld.bnk` `library.2dm` as the material fallback, the bank's textures registered under the bank path),
  `layoutSceneModels` (a row, each model on its own bounds), a **turntable** camera (`buildSceneParams`: the
  `EdenSceneView::setupScene` camera and the island's game weather), an 800x600 render target drawn in
  `drawOffscreen` and shown as an `ImGui::Image`; **`pickScene`** unprojects a click through the *view-projection the
  GPU drew with* and selects the `.3dd` that carries the nearest mesh hit (`applyPick`), so a label keys on the same
  `<bank> + <asset filename>` identity as the lists. Cube maps now preview as **face 0, flat**; a `vmf` loads a
  `FlashMovie` + `FlashPlayer` and plays in the 3D view panel; a `wav` gets a Play / Stop button (`GSAudio`).
  Test hooks: `-pickat x,y` (a click), `-label` / `-note` / `-savelabel` (the whole click-to-label flow),
  `-playaudio`, `-noscene`, `-noturntable`, `-island`, `-pickcheck`, `-pickscan`, `-logscenedraws`.
- `source/flash/flash_player.{h,cpp}` — `setStageRect(rect)` / `clearStageRect()`: the destination rect of the stage,
  so a tool can play a movie inside its own panel (default: the screen ratio rect, unchanged).
- `docs/re/asset_viewer.md` (§2 the 3D view, §3 the evidence, §5 what is left), `README.md` (the flags and the
  ASSET_VIEWER row), `docs/re/asset_labels.txt` (**+1 label**, written by the viewer: the casino exterior).



### Verified
    build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-cline-userdata
        -assets -nopad -bank euro/bnk/islands/casino/exterior/ecas.bnk -noturntable -pickcheck -exitframe 40
    ... -pickat 590,522 -label "..." -savelabel -exitframe 40        # the click-to-label flow, writes the label
    ... -asset LOG_IN2.vmf / -asset BEFORE_AD_05.wav -playaudio / -asset cubemap_ecas.2db
    ... -labellint

- the models: `'...ecas.bnk' - 5 model(s), 43 mesh(es), 37999 vertices, 28777 triangles`, `3D view 488,394 -
  837,656`, `scene 43 draw(s) of 43 item(s)` (every item is drawn, the frustum is right).
- the picking is **self-checked**: `-pickcheck` projects each model's centre with the scene's view-projection and
  picks that pixel — all five hit their own model (`check 'ecas_sc_DB42CB20E8099A8F.3DD' centre -> pixel
  590.2,522.0: hit ecas_sc_DB42CB20E8099A8F.3DD`).
- the click: `pick (-pickat) -> 'ecas_sc_DB42CB20E8099A8F.3DD' in 'euro/bnk/islands/casino/exterior/ecas.bnk'
  (mesh node 'ECAS', 386.45 m)`, then `AssetLabels: wrote 7 label(s)` and `saved 'casino exterior (the ECAS spot
  model set)' for ecas_sc_DB42CB20E8099A8F.3DD` — the label the owner asked for, written by the tool.
- the movie: `Flash movie 'log_in2': 1280x960, 444 frames @ 30 fps, 798 tags` drawn in the panel (visible in the
  screenshot); the sound: `sound 'BEFORE_AD_05.wav': 291148 frame(s), 44100 Hz, 6.60 s (playing)`; the cube map:
  `'cubemap_ecas.2db' - 256x256, depth 6, 7 mip(s), format 55 (cube map: face 0 shown flat)`.
- `-labellint` → `7 label(s), 0 problem(s) - every key exists in the game data`.
- screenshots: `docs/images/asset_viewer_model.png` (the turntable), `asset_viewer_pick.png` (the click-to-label
  moment), `asset_viewer_movie.png` (`LOG_IN2.vmf` playing), `asset_viewer.png` (refreshed).

### Ruled out (with the test that ruled it out)
- **The first ray math was wrong** (a hand-built camera basis: fov / aspect / handedness): the `-pickscan` grid hit
  geometry where nothing was drawn. Unprojecting through `sceneViewProjection` (the matrix the GPU draws with) fixed
  it, and `-pickcheck` now proves it per model. **Do not rebuild a ray from yaw / pitch when the view-projection is
  in hand.**
- **The `-pickat` pixel must come from the log** (`AssetViewer: 3D view x0,y0 - x1,y1`): the panel rect moves with
  the layout, and the pick happens on the *first* frames, so a `-pickat` with a stale pixel misses (it logs `hit
  nothing`). Use `-noturntable` too: the turntable moves the camera between the click and the screenshot.
- **The four small `.3dd` of `ecas.bnk` draw but show almost no pixels** (one 2-triangle plane each + a flat 908
  triangle ground): they are hit by the rays and counted as draws, so this is their material / geometry (facade,
  reflection planes), not the picking. A click on them still selects the right asset.
- **`clang-format` (23.1.2) is not clean on this tree**: running it on the touched files rewrote ~1,700 unrelated
  lines (`flash_player.cpp` 1,358 of them). It was kept for `gm_asset_viewer.*` (the file is mine) and **reverted**
  for `eden_model.*` / `flash_player.*`, whose edits are a few dozen lines; the new code follows the file's style.

### Not done / next
- **Click-to-label inside a streamed island / spot scene** (the `WORLD_VIEWER` view): needs an `EdenWorld` /
  `SpotExterior` accessor that hands out each drawn model with its bank path and the asset it was loaded from
  (sector: `bank + "<name>-o.3dd"`, exterior: `bank + "<scene>.3dd"`), then `EdenModel::raycast` per model — the
  ray, the identity resolution and the label side are already in place (`docs/re/asset_viewer.md` §5).
- Skinned models preview in their bind pose (`draw()` is rigid); the bank's `lm_*` lightmaps are not registered in
  the preview (the sun / ambient of the island's weather is), so a building is a little flatter than in the game.
- Cube maps: face 0 flat; a cross or a small lit cube would be nicer.

## Session 2026-10-29 (forty-fourth, `win`, part 7): a Button prompts option, and Xbox icons for Xbox pads

The owner: "add a in game option to select which glyphs we use … with xbox 360 also for people using xbox
controllers".

### What changed
- `game_options.{h,cpp}` — `ButtonPrompts` (options.ini `button_prompts`): 0 Auto, 1 Keyboard & mouse,
  2 Xbox, 3 PlayStation.
- `gs_input.{h,cpp}` — `getPadStyle()`: SDL PS3/PS4/PS5 → PlayStation, any other pad → Xbox; `setPadStyle`
  for tests.
- `tdu_instance.cpp` — the prompt platform from the option (Auto = keyboard art, or the pad's family);
  autoplay `device=ps|xbox`.
- `command_resolver.cpp` — `#command:` Xbox form (the retail x360 bitmaps) beside the PS3 one.
- `advanced_graphics.{h,cpp}` — the page is now **Advanced settings**, "Button prompts" is its first row.
- `pause_menu.{h,cpp}` — Y / Triangle opens it from the **Controls** page too (`bOnControlsPage`, from
  `opts:cont:*:open`), with the corner hint.

**Controls:** Pause → Options → Controls (or Video) → **Y / Triangle** (keyboard: the MENU_OPTION_2 key, or
F2 anywhere) → "Button prompts", Left / Right to change, OK / Back saves.

### Verified
- `docs/images/button_prompts_option.webp`: map bar with keyboard / Xbox / PlayStation art; the page over
  the Controls screen with "Button prompts < Xbox >" — the Controls page's own prompts switched to Xbox
  (Y Save, LB / RB, B Back) live, on the keyboard, and `button_prompts = 2` was written.

### Not done / next
- Not tried with a real Xbox pad: the detection is `SDL_GetGamepadType`, untested on hardware here.
- The retail way (a controller chosen on the Controls page sets `/:IsPC_X360`) needs the binding page
  (`opts:cont:bind:open`, unanswered) — the option lives on the native page until then.

## Session 2026-10-29 (forty-fourth, `win`, part 6): button prompts follow the device — PC art on keyboard/mouse, PS art on a pad

The owner: "the UI may show controller buttons if using controller and keyboard and mouse buttons if using
them" — and asked for PlayStation icons on the pad (DualSense). No PS3/360 build assets were needed: the PC
movies already carry `x360` / `pc` / `ps3` frames on every glyph sprite and GENERAL's `$$bitmaps$$` holds 19
PS3_* text icons [DATA].

### What changed
- `source/flash/flash_player.{h,cpp}` — `SetPromptPlatform` / `GetPromptPlatform`, a registry of loaded
  players, `refreshPlatformGlyphs` (live switch), `PS3BitmapName`; a glyph sprite's own read of
  `…WhatPlatform` gets the prompt platform, everything else keeps the movie's value.
- `source/game/command_resolver.cpp` — `#command:` pad form from DeviceX360Pad.xmb with the PS3 icons.
- `source/tdu_instance.cpp` — drives it from `GSInput::getLastDevice()` each frame; autoplay `device=pad|keyboard`.
- `source/input/gs_input.{h,cpp}` — `setLastDevice`; the mouse wheel counts as keyboard/mouse.
- Spec: `docs/re/text_icons_spec.md` §6 "Device-following prompts".

**Controls:** touch any DualSense button / push a stick past half → PS icons (✕ ○ □ △, L2/R2, R stick…);
press any key, click or scroll → the PC icons (check, star, cross, mouse). Switches live, even inside a menu.

### Verified
- `docs/images/prompts_keyboard_vs_pad.webp`: pause menu (✓ Resume → ✕ Resume, all 8 squares both ways),
  map bar (✓ OK / ★ Filters / mouse Camera / mouse Zoom / ✗ Back → ✕ OK / □ Filters / R Camera / L2 R2 Zoom /
  ○ Back), FRIM BANK (✓ → ✕). Live switch inside an open pause menu and an open map, both directions.
- `#command:` resolution checked for 8 actions (spec §6).

### Ruled out
- Setting the movies' `/:WhatPlatform` to 2: MAIN's root scripts read `_root.WhatPlatform` and build the PS3
  pause menu (6 squares, no profile / Exit). Hence the glyph-only read.

### Not done / next
- Xbox art for an Xbox pad (`SetPromptPlatform( 0 )` from the SDL gamepad type, `SDL_GetGamepadType`) is a
  one-line choice in `TestDriveGameInstance::pumpEvents`; not wired.
- Prompts the game draws itself (C++ `drawText` prompts such as `prompt_` in freeride_hud.cpp) do not switch.
- No `#command:` text is on screen in FREERIDE now (the return-to-road tip uses the retail spot tile), so the
  inline icons were checked through the resolver, not a screenshot.

## Session 2026-10-29 (forty-fourth, `win`, part 5): Flash masks work — the main depth buffer had no stencil

The owner: "the bar seems to extend past the UI in our version compared to the official game" (the FRIM
gauge fill ran left past the level badge, under the "Enjoy!" goal strip).

**Reproduced**:
    build\bin\opentdu2.exe -game ... -gm FREERIDE -skipcine -nopad -fixedtimestep -riskdemo -riskevent ^
        -autoplay "900:shot=C:/OpenTDU2/_fix900.ppm" -exitframe 910

### Diagnosis
- The fill is `/HUD/FRG/FR/fr_gge/black_gge/color` (shape 861, x -5362..0 twips, right-anchored; the
  movie slides `black_gge` right with the progress). It is masked by `fr_gge/#46` (shape 859,
  x 661..6022, ClipDepth 63) [DATA: hud.bnk, `-logfrim` + `dumpClips`]. The mask geometry is right; the
  overflow length was always exactly bar length − visible fill, i.e. **no clipping at all**.
- `FindDepthFormat` tried `VK_FORMAT_D32_SFLOAT` first — no stencil bits — so every
  `SM_Increment` / `SM_TestEqual` in `FlashPlayer::drawClip` was a no-op. **Every Flash mask in every
  movie has been drawn unclipped since the start.**

### What changed
- `source/render/vulkan/render_device.cpp` — depth format order is now D32S8, D24S8, D32, D16; the main
  depth view and the `copyRenderTargetDepth` barrier use depth|stencil aspects (`DepthStencilAspect`);
  `copyRenderTargetDepth` accepts D32S8 (the depth-aspect copy is still 4-byte float).
- `freeride_hud.cpp` `dumpClips` prints Depth and ClipDepth (masks are visible in `-dumphud`).

### Verified
- `docs/images/frim_gauge_mask_before_after.webp` (top before, bottom after, frame 900 bank state):
  nothing left of the badge. Frames 600 / 2300 (levels 1 and 4) clean too.
- 3D unaffected: frame 900 before/after, mean pixel diff 0.0 over the 3D band (y 200..520).
- World map and pause menu render as before (no validation errors in the log).

### Ruled out
- Hiding `fr_gge/bank`, `hit`, `engine`, `lvl` leaves the overflow; hiding `black_gge` removes it and the fill.
- Not the GENERAL.vmf goal strip: it draws after the HUD, and the overflow is the bar's own extent.

### Not done / next
- Other movies with masks (MAIN.vmf pages, the map, the gauges) now clip for the first time — worth a
  look for anything that relied on the unclipped draw. The world-map centre (priority 1) may be worth a
  re-check with masks live.

## Session 2026-10-29 (forty-fifth, `glm`, part 2): the shops were dark — the NIGHTLMAP overwrite killed the lamp bake

Owner: *"I think mirrors need fixing I was in clothes shop and mirror didn't look like was working
and I think lights in buildings aren't working car dealership ferrari was pretty dark also wasn't
rendering out side world this may be the case for most shops tbh"*.

### Reproduced
`-gm FREERIDE -tp 21796.3,24188.1 -heading 103 -storytime 86400` (inside the ECD_FERR showroom):
dim, grey, no lamp glow (`_ab_ferr_fixed.png` top).

### Root cause
The spot-exterior banks author their own NIGHTLMAP day/lamp blend — the dealer's two materials
(`Spot_V_Lods_FER`, `Lod_interior` in `ecd_ferr_….2DM`) carry **NIGHTLMAP (0.44, 1.0)**: 44% day
bake + the lamp bake fully on. `SpotExterior::registerLightmaps` handed every material to the
**Exterior** weather blend, and at midday weather '14' writes `spot_exterior_lightmap_day_night =
(1, 0)` — the lamp bake switched off, the day bake alone indoors = the dark showroom.
`-nonightlmap` (the authored values) renders it bright and warm [A/B].

### What changed
- `source/game/spots/spot_exterior.cpp` — `registerLightmaps` no longer registers the spot
  exteriors for the NIGHTLMAP overwrite: the authored blend stays (documented: the retail
  exterior writer FUN_00980e50 is [ENC]; the blend must come back for night, with a per-material
  exterior/interior law). `-nonightlmap` remains the A/B.
- `source/game/freeride/gm_freeride.cpp`, `intro_cinematic.cpp` — the weather log lines now print
  the `spot_exterior/interior_lightmap_day_night` values (FREERIDE midday: `ext (1.000 0.000) int
  (0.300 0.600)`).

### Verified
- Dealer inside + outward after the fix: bright ceiling / walls / lamp-lit floor, the world
  visible through the window glass (`docs/images/port-dealer-lmfix-s45.webp`,
  `port-dealer-out-lmfix-s45.png`).
- **Ruled out**: sRGB texture sampling — `GetVulkanFormat` drops the SRGB_ flag (lightmaps are
  `VF_SRGB_DXT3`); mapping them to the `_SRGB` block formats made the dealer **darker** (the
  pipeline is gamma-mixed and every verified scene matches with gamma sampling) — reverted.
  Kept as a documented experiment.

### Not done / next
- **Shop interiors are not loaded** (spot_exteriors_spec §4): the retail flow swaps to the
  `interior/<bank>.bnk` scene with its own lights and mirrors — that is where the clothes-shop
  mirrors live (FREERIDE has no GSReflection per the retail mode table, `first_outro.cpp:147`).
  The port shows the exterior shell only; the mirrors the owner saw are shell materials.
- Building collision (the -tp spawn put the car under the clothes shop).
- The glass tint (the ps109/G_I law, see part 1) still dims the panes.

## Session 2026-10-29 (forty-fifth, `glm`): the "dirty glass" + the meeting_tess "black sail" — one root cause, and the OpenTDU1 reuse map

Owner: *"in car view window seems like glass has dirt/or something on it"*; *"fix cinematic camera
behaviours in cutscenes camera causes some sort of culling issue the scene after you meet tess at
the party and get introduced to the california is a good place for bug testing it"* (+ a full-res
screenshot of the garage pan); then *"maybe https://github.com/CamilleKMI/OpenTestDriveUnlimited
is there anything we can potentially use from this … I want you to document everything we can
possibly use from this"*.

### Reproduced
- Garage pan (FIRST_WM, C_INTRO cam_intro_03, frames 780-860 at `-fixedtimestep`): the wheel area
  tears with translucent shards — the near plane (0.8 m, the retail default [CODE:
  `intro_spec.md` §7.3 + TDU1 `gs_render.cpp:241`]) as the authored camera path passes < 0.8 m
  from the rim. Retail would clip the same way; **not a port bug**.
- meeting_tess shot 0 (`-gm CINVIEWER`, frames 30-330): the black "sail" + the milky veil over the
  whole view (owner's "culling like issues").

### What the sail is (A/B, `-hidenode`/`-noprops`/`-nopnj`/`-carlod`/`-no*` — see the new flags)
- It is 3D, depth-tested, only in the shots where the camera sits inside/next to the car, and it
  survives hiding the world (-noareas/-noobjects/-noinstances/-noterrain/-noocean/-nosky/
  -noclouds), the sets, props, avatars, the whole car (`-hidenode D_CAR`), rims/tires/windows/
  HANIM_S/hardtop/shadow boxes, the interior GPS, and the planar-reflection actors.
- Hiding **GLASS_F (the windscreen node) removes part of it**; the rest is an interior-cockpit
  mesh that draws only with the interior bank. The sail correlates with the **glass panes seen
  from inside**: ps_109 Glass gives base = black (G_I, alpha 1) + a grazing reflection with a
  0.5 fresnel floor — a near-opaque black wall from the inside, and the black band + grey streak
  of the owner's garage screenshot from outside. **One root cause for both reports; the exact
  term to change is still open** (candidates: LAYRCLR1.w runtime write, G_I slot sampling,
  ENVCOEFF).
- Second, independent real bug: the interior **GPS dashboard quad** (material `GPS`, family
  `0x7dad16b47eb57b95`, interior nodes 5/31) draws unprojected — TDU1's `setGPSTexture`
  (`vehicle_base.cpp:729`) writes its static params ([0]=minimap U, [1]=V, [2]=3.0) and we write
  none. It floats across every interior shot.

### What changed
- `source/game/cinematic/cinematic_car.{h,cpp}` — `-hidenode a[;b…]` (car **and** interior subtree
  hide, remembered across the per-frame cockpit LOD re-show), `-logskin` (skinned palettes,
  full node dumps, GPS mesh locator by shader guid).
- `source/game/avatarchoice/intro_cinematic.cpp` — `-noprops`, `-noblackstripes`.
- `source/render/eden/eden_scene_view.cpp` — `-nosky`, `-noclouds`.
- `source/render/eden/eden_renderer.{h,cpp}` — `-drawfrom/-drawcount/-logdrawwindow` (main-pass
  draw-call bisect; the window keys on the sky-dome first draw and does **not** gate the depth
  prepass — gating it blackens the frame) + per-pass draw-count log.

### Ruled out (with the test)
- Near plane config (0.8 = retail [DATA+CODE]); car LOD, roof, windows, HANIM_S palettes (max
  bone 14 < 15 matrices), shadow boxes, springs, steering, weather modifier (`-cineweather`),
  black stripes, actor reflections, ocean, sky, clouds, terrain areas (only `-noareas` gates
  them — `-noterrain` is sectors only, `eden_world.cpp:851-881`), props, sets, PNJs, driver
  avatar.

### Fixed (same session, after the diagnosis)
- `cinematic_car.cpp` — the interior `GPS` subtrees are excluded from the drawn set (next to
  BELT_L/R) until the dash GPS exists: the TD_GPS material draws **0** times now (was the only
  blended material of the sail frames). The real fix is writing TDU1's `setGPSTexture` static
  params ([0]=minimap U, [1]=V, [2]=3.0) once the in-car GPS screen is implemented.
- `cinematic_car.cpp` `assignEnvironment` — **TD_Mirror (0x08041973fadeffff) added to the car
  family list**: the garage wing mirror now reflects the static CUBECAR (garage interior:
  `port-mirror-cubecar-s45.png`; log: Mirror slots `... cubecar`) instead of the outdoor palms.
  Cutscene mirrors get the dynamic cube (the -logmaterials line still prints the pre-bind
  'cubemap' — the material is logged once at first draw). The original gates this behind the car
  env +0x2ab6 flag [ENC]; enabled unconditionally [INF].

### Not done / next
- The glass term (the actual "dirt" look, both reports): A/B `LAYRCLR1.w` (the base lerp factor
  `detail.a * c17.w`) and the G_I slot sampling — see `docs/re/opentdu1_reuse.md` §1.2 for the
  TDU1 laws (interior-glass family `0xdddddddd1c6c7357` gets runtime PS `0x3e` fresnel params per
  frame; env factor law `((lum-1)*gReflectionFactor+1)*gGlobalEnvValue`).
- The fender-intake "red petals" (the owner's screenshot, also in the port) — material not yet
  identified; use `-edenchecker` or the draw bisect on that spot.
- The OpenTDU1 ranked reuse list (§3) — GPS projection (with the minimap viewport), glass runtime
  params, showcase near law, weather car factors.

## Session 2026-10-29 (forty-fourth, `glm`): the world-spot UI layer — shops, races, police chase, hints, countdown

The owner asked for *"return to road, police, clothes shops, races, and
dealership UI stuff"* with 7 retail captures (the shop tile over a clothes
shop, the challenge hint + race markers, the centred "Return to the road",
the "Traffic violation" meter, the handcuffs + escape timer, and the two
"Help: Chase Mode" pages).

### The movie protocols, decoded from HUD.vmf's own bytecode (`_vmf_avm1.py`)
- **`/HUD/enter`** (char 251, the world-spot prompt) holds **`Spot`** (char
  227, 751 frames) — one frame label per spot kind (`clothes`, `carDealer`,
  `event`, `estates`, `rschool`... `-dumpsprite 227` lists them all); the
  return-to-road art is the `Spot/offroad` child ("default"/"on"); the
  prompt reads `/:m_Enter`; labels off/on/stop/locked/end/out. The movie's
  own script drives the art from `/:WhatSpot` (the exe sets it; the port
  gotoLabels the Spot directly, same frames).
- **`/HUD/tailing`** = the chase awareness meter: the `cars` child (384
  frames) has the states `loop_far` / `loop_warning` / `alert` / `red` /
  `too_far`..., the metres readout is `/:distance_KYD` ("333│999m").
- **`/HUD/chase_timer`** = the arrest countdown: labels default/open/stop/
  change/failure/success; its 5-slot `numbers` digit feed is **not decoded**
  (the mm'ss"cc text goes to `/:m_escape_` / `/:Xtimer`, the box shows its
  first slot — open item).
- **The help popup** is `/help_system` (nav arrows, page squares sq0..9,
  title bar): the HUD root functions are `OpenChaseModeHsys` (`_root.Clip =
  this; _root.hsysID = "f_chasemode"; openHelpSystem(0)`), `openHelpSystem`
  (`hsysTitle = m_HelpSytem_ + ": " + m_ChaseMode_; nbPages = 2`), and the
  close fires FSCommand **`chmo:hsys:clos`** (helpSystemClosed). The page
  texts + rank art are authored in the movie.
- **`/_3_2_1_go`** = the race countdown (labels 3_go/2_go/1_go/go); **`/go`**
  the bare GO flash.
- GENERAL.vmf `/pop_tutorial` (open_tuto, /:TutText, close_popup) is the
  Hint box — the same element the walk mode drives; FREERIDE just never drew
  it (added to the draw list).

### What changed
- `source/game/freeride/freeride_huid.{h,cpp}` (freeride_hud) — showEnterSpot
  / closeEnterSpot (the /HUD/enter element: parent-level setClipTransform —
  per-child placement fights the timeline placements, the parent transform
  survives; halo children hidden per frame), setWorldMarkers (up to 8
  duplicate() clones of the whole element, prompt hidden), showHint /
  closeHint (/pop_tutorial, drawn now), showCountdown (/_3_2_1_go),
  setChaseHud (tailing states + distance_KYD + chase_timer + IsUnderArrest;
  IsPolice stays off — the movie shows its online chase_target marker on
  it), openChaseHelp (the decoded OpenChaseModeHsys protocol; chmo:hsys:clos
  closes it), setRaceTimer (placeholder text — the retail /HUD/time protocol
  is not decoded), the frgp: FSCommands actually route to bankCommandCallback_
  now (it was stored but never invoked).
- `source/game/freeride/chase_mode.{h,cpp}` (new) — the police chase state
  machine: violations (traffic hits) fill the awareness, full = chase (2
  Dodge_Charger cops — chaseconfig.cpr CopCars level Low CarId 3009, name
  all "##" in the DB [INF fallback]), kinematic pursuit with rubber-banding,
  the arrest countdown arms < 40 m / disarms > 70 m, arrest = timer 0 or
  1.5 s contact (fine + outlaw rank reset, loc 5202/5749), escape = all cops
  > 260 m for 8 s (reward + rank up, loc 5201/5747). Values [INF]; the
  retail laws are [ENC]. Hooks: -police (chase now), -policeviol,
  -policehelp, -logpolice.
- `source/game/freeride/race_events.{h,cpp}` (new) — instant challenges: 8
  starts spread on the road graph (deterministic per island), DBEvent rows
  read for the rewards (106 valid rows; -logevents dumps the table; the
  event files themselves are [ENC]), A at a start = 3-2-1-GO countdown +
  3.5 km random-walk route with checkpoints (22 m gates) + money + racing
  points. -racetest warps the car through the gates headlessly.
- `source/game/clothes_shop.{h,cpp}` (new) — DBClothe read (327 valid items:
  name/brand/price) + the native menu in the dealer menu's shape; buys into
  profile.OwnedClothes. -clothesshop opens it for tests.
- `source/game/freeride/gm_freeride.cpp` — tickWorldSpots (new): the enter
  element for the nearest spot (clothes > carDealer > race start, A enters /
  starts; a spot suppresses the off-road indicator), the world markers (race
  starts + next checkpoint within 300 m, projected by projectToCanvas with
  the chase camera), the challenge hint (loc 2220) beside a start, the
  return-to-road element (loc 50 + the offroad art) when off the network
  > 2 s — replaces session 43's goal-bar prompt; the police tick + HUD, the
  race flow (countdown/gates/finish), the clothes menu freeze; player
  profile gains OwnedClothes / OutlawLevel / chase counters (saved).
- `source/game/map_screen.cpp` — findSpotNear takes an optional out-position;
  -logspots dumps the spot table.

### Verified (headless, `-fixedtimestep` everywhere — frame-counted timers!)
- Clothes shop spot: the retail t-shirt tile + "Enter (A)" prompt
  (`port-enter-clothes-s44.png`); return-to-road: loc 50 + the offroad art
  (`port-enter-offroad-s44.png`); hint box: loc 2220 with the resolved key
  (`port-hint-challenge-s44.png`).
- Chase: the meter top-right with the live cop distance, states by distance
  (`port-chase-meter-s44.png`); the Help: Chase Mode popup with the authored
  rank art + Change page footer (`port-chase-help-s44.png`); arrest on
  contact (parked) and while driving (log); -racetest-style full race:
  countdown 3-2 (`port-race-countdown-s44.png`), gates, finish 2.05 s +
  $5000 (`port-race-gates-s44.png`).
- Clothes menu: 327 real items with prices (`port-clothes-menu-s44.png`).

### Deviations from the retail captures (all [INF], next steps named)
- The enter prompt anchors bottom-centre instead of floating over the spot
  (the per-child placement that would follow the spot loses to the timeline
  placements; the parent transform is what survives). The tile art, the
  prompt and the (A) pill are the movie's own.
- The chase_timer digits: the box shows one placeholder digit; the retail
  5-slot digit feed is not decoded (next step: dump the `numbers` child's
  per-frame placements with -dumpsprite 924).
- The tailing meter's art at top-right is the movie's `alert` section (red
  car + distance), not the retail "Traffic violation" bar — the retail bar
  may be another label of the same child (the states list has
  `red_to_green` / `green` / `alert_in_green` untried).
- Races: starts/routes are synthesized on the road graph ([INF]), the retail
  DBEvent event files are not readable.

### The pull (after the feature work): win + cline merged in, launch/stop system
- `git merge win/win` (sessions 44: the Solar Crown story, Meeting Tess at the
  hotel, the sat-nav voice + turn arrow, the island's spot exteriors, pad
  support in the map/pause screens, the photo camera) and `git merge
  cline/cline` (the casino floor + chip wallet + Game Progress page, the
  asset viewer, CLINE.md). Conflicts resolved: STATE.md (both entries kept),
  freeride_hud.h (win's D-pad accessories wheel protocol + this session's
  methods), gm_freeride.{h,cpp} (both module sets; the casino A-enter kept,
  the dealer prompt stays in tickWorldSpots), player_profile.{h,cpp} (the
  clothes/chase fields + the casino fields both in the save).
- **The exe is now `build-glm\bin\opentdu2-glm.exe`**: `build-glm.ps1`
  configures with `-DOTDU_EXE_SUFFIX=-glm` (CMakeLists.txt). Re-configure with
  the script after a merge — a manual configure without the suffix silently
  builds `opentdu2.exe` again (this session hit it). Launchers resolve their
  tree's own exe via `launch/resolve_exe.bat`; stop a run only by this tree's
  path or the `opentdu2-glm` name (WORKSPACES.md).
- Verified on the merged tree: `build-glm.ps1` clean, the chase run shows the
  full set (meter + distance, the timer box, the loc 6614 hint) —
  `docs/images/port-chase-timer-hint-s44.webp`.

## Session 2026-10-29 (glm): otdu2_cine_tool — cutscene dump tool, committed

The owner asked for cutscene-making tooling ("the files are there, it's just
the locations"). Phase 1 of 3 (dump → viewer → editor) built and verified:
a new offline tool that walks `Euro/Bnk/scenarics/cinematics` and emits one
Markdown report (+ optional JSON with every raw key) per `.cin` timeline.

- `tools/cine_tool/main.cpp` + `tools/CMakeLists.txt` target
  `otdu2_cine_tool`. Commands: `list <game root|folder|bank>` and
  `dump <...> [out] [-json] [-variants]`. Reuses the game's own parsers
  (`Cinematic` / `CinematicScene`) through the `otdu_engine` static lib —
  no new format decoding. Language-variant banks (`__us`...) skipped unless
  `-variants`.
- **The "locations" question is answered by the data**: every `.sce` carries
  an authored D3D origin matrix (position + heading) at DATA+0x28
  (`CinematicScene::getOrigin()`); the reports print it per scene along with
  each camera's position/yaw/fov key table (yaw derived, marked as such) and
  each actor's path/anims. Runtime-anchored scenes (the dream plays where the
  player parked) get their world transform from game code — the authored
  origin is the authoring-level placement.
- Output: `_cine_dump/<bank>/<cin>.md|.json` + `index.md`. Verified on the
  real data: 42 banks, 136 base cinematics, all 136 JSON parse; meeting_tess
  = 66.67 s / 16 shots (matches the inventory), per-shot
  `wmod:Meeting_Tess_02..16` + `shad:*` cues listed, shot 0 fades in from
  white 4 s (the "white flash, dream" beat), baked 30 Hz camera paths
  (733 keys on c_meet_11) are strided in MD, complete in JSON.
- Traps hit (all fixed): JsonOut needed a per-scope "await value" state
  (three rewrite attempts — the comma/newline logic ate separators); NaN
  quaternion components in unused Heading tracks → `JsonNum` emits `null`
  (data honesty: several cameras carry garbage/NaN keys, they are not used
  by any shot); Latin-1 bytes in data strings → `\uXXXX` escapes.
- **Next (phase 2)**: CINVIEWER upgrades — cutscene browser, on-screen shot
  timeline + active camera, camera path / scene-origin debug draw,
  pause/scrub. **Phase 3**: `.sce`/`.cin` patch writer (round-trip, unknown
  bytes preserved) verified by playing edited scenes headless. The JSON in
  `_cine_dump` was designed as the phase-3 editing view.
## Session 2026-10-29 (Cline, `cline`, second): the offline casino (`source/game/casino/`)

Owner: *"I Had seen you mentioned casino floor was never started is it possible I can get you to start the casino
process and all it's features slots etc... use to require online server work but I'm hoping we can maybe allow
some offline functionality in casino."*

Started, and it plays: the **floor**, the **slot machine** and **roulette**, offline, with a chip wallet in the
profile. Spec: `docs/re/casino_spec.md` (the whole retail contract, tagged).

### What the data gave us (all `[DATA]`)

- The casino frontend is five movies, all present: `FrontEnd/HiRes/casino.bnk` -> `Casino.vmf` (the shell) and
  `cas_slotmachine` / `cas_slotlines` / `cas_slotscreen` / `cas_roulette` / `cas_poker`. Plus the shaders
  `TD_ChipsStack.sob` / `TD_Roulette.sob` / `TD_Casino_scroll.sob` and the arrival / departure cinematics
  (`spt_casino_arrival.bnk` 10.4 MB, `spt_casino_departure.bnk` 8.8 MB).
- The PS3 anchors carry the retail contract: the `CasinoGame` class, the save sections (`CasinoSave`,
  `CasinoProgressionSave`, `CasinoRankingsSave`, `Casino*StatisticsSaveDesc`), the roulette phases
  (`CASROU_PlaceYourBet` / `BetsAreDone` / `RienNeVaPlus` / `Winner`), the bet names (`BET_RED` / `BET_BLACK` /
  `BET_1TO18` / `BET_19TO36`), the slot beats (`C_SLOT_ACTIVATE` / `_WIN` / `_LOOSE`), the locks
  (`CASINO_FORBID_SLOT_MACHINE`, `CasinoUnlockable`) and the flash globals (`/:IsCasino`, `/:HasCasino`,
  `/:IsCasinoStandAlone`, `/:IsCasinoSaveData`, `/:CasinoAvatar`, `/:isProgressCasino`, `/:UseCasinoRankingList`).
- `db_data.cpr` has a full casino meta-game: `DBCasinoLevel` / `DBCasinoObjectiveDetail` / `DBCasinoReward` /
  `DBCasinoUnlockable` (+ `DBFurniture._iscasino`, `DBLoc._casino`). Dumped with
  `otdu2_bnk_tool xmb .../db_data.cpr`.
- **Why it needed a server**: the casino was invite / DLC gated (`"<INVITE> Invite rejected : Casino locked"`),
  the jackpot was shared (`C_SLOT_SHARED_JACKPOT_BACKGROUND`), the ranks were online (`CasinoRankingsSave`), and
  the games were resolved by the backend (the client only kept the statistics messages
  `CRC_*_STATS_*_CASINO_SLOT_1`). Replacing all of that is what the offline mode is.

### What changed

- `source/game/casino/gm_casino.{h,cpp}` (new) — `GMCasino`: the floor menu (slot machine / roulette / poker
  [locked] / buy chips / leave), the wallet, the flash globals, the casino pause list, ESC, native rendering.
- `source/game/casino/casino_slots.{h,cpp}` (new) — 5 reels x 3 rows, 9 paylines, per-line bet 1..10, one shared
  64-stop strip (closed-form RTP), the reel-stop animation, `-slotselftest`.
- `source/game/casino/casino_roulette.{h,cpp}` (new) — single-zero wheel, 49 cells (numbers + RED / BLACK /
  ODD / EVEN / 1-18 / 19-36 / dozens / columns), the retail phase machine on a timer, a 2D cursor,
  `-rouletteselftest`.
- `source/game/gm_base.{h,cpp}`, `source/tdu_instance.cpp` — `GM_Casino` ("CASINO") + the factory case.
- `source/game/player_profile.{h,cpp}` — `CasinoChips` / `CasinoUnlockables` (`casino_chips` /
  `casino_unlockables` in `profile.sav`).
- `source/game/pause_menu.{h,cpp}` — `setCasinoMode( true )`: the retail 6-entry casino list (photo + gme
  locked), which this file's session 18 had deferred to "when the casino mode exists".
- `source/game/freeride/gm_freeride.cpp` — the `gamblinghall` spot (ECAS): "A / Enter: enter the casino" ->
  `GM_Casino` (the dealer prompt is unchanged; the two are exclusive).
- `launch/13_Casino.bat`; `docs/re/casino_spec.md`.

### Verified (headless)

- **Slot machine** — `-gm casino -slotselftest 200000`: 200 000 spins, wagered 1 800 000, won 1 741 360,
  **measured RTP 96.74% vs analytic 96.16%**, 43 439 winning spins (21.7%), best win 1 900, and
  `balance 941360, expected 941360 (ok)` — the wallet arithmetic is exact.
- **Roulette** — `-gm casino -rouletteselftest 5000`: all 13 bet types within the sampling error of the
  theoretical **36/37 = 97.30%** (straight 93.60% ... 19-36 100.60%). A live run settled end to end:
  `CasinoRoulette: 33 black, payout 0 chips, balance 4990`.
- **Playable** — `-gm casino -casinochips 5000 -fixedtimestep -autoplay "..."`: the floor, a spin (9 chips, 5 000
  -> 4 991), back to the floor, the roulette, a chip placed, `X` spins, the winner highlighted, the history
  strip. Screenshots `docs/images/casino_floor.webp`, `casino_slots.png`, `casino_roulette.png`,
  `casino_roulette_winner.png`.
- **`Casino.vmf` loads** — `Flash movie 'casino': 1280x960, 135 frames @ 30 fps, 1636 tags, 206 bitmaps,
  2 fonts` — but **drawing it breaks the native screens** (the header disappears, the glyphs come out as solid
  blocks). It is therefore opt-in (`-casinomovie`); `-dumpcasinolabels` only loads it to dump its timelines.
  Prime suspect: the flash display list leaves the 2D mask / scissor state set (spec §5).

### Next

Poker (`Cas_Poker.vmf`, `C_POKER_YOU_BET`), then the casino progression from the decoded `DBCasino*` tables
(levels / objectives / rewards: cars, cocktails, cinematics, room levels), the VIP / ACE ladder, and the
arrival / departure cinematics. The casino pause list is in, but its `game_progress` / `players` / `profiles`
entries stay locked (the same features the free-roam list locks).

### Follow-up (same session): the rest of the casino — poker, the progression, the objectives

Owner: *"I want you to wire up as much as possible for the casino thanks"*.

Everything below is wired to the game's **own data** — the tables were dumped with
`-logcasino` and the ids come from them.

- `source/game/casino/casino_poker.{h,cpp}` (new) — **video poker**, 9/6 Jacks or
  Better, hold / draw. The royal flush pays the **max-bet bonus** (800 per coin at
  5, 250 below): that is the difference between the published 99.54% (max bet) and
  98.37% (one coin). `-pokerselftest` verifies the evaluator on 13 fixed hand
  classes (including the A-2-3-4-5 wheel — all ok) and reports the return as a
  **sampled mean with its standard error** (the per-deal optimal EV is far too
  spread for a tight estimate to be cheap: 120 hands gave 93.57% +- 7.47%, so
  99.54% is inside one SE; the evaluator test carries the exactness instead).
- `source/game/casino/casino_progression.{h,cpp}` (new) — the retail casino
  meta-game, from `db_data.cpr`: **10 `DBCasinoLevel` rows over 4 axes (32 games /
  33 variety / 34-35 social), 145 `DBCasinoObjectiveDetail` objectives, 60
  `DBCasinoReward` rows and 54 `DBCasinoUnlockable` rows** (`Cocktail_00..24`,
  `Emote_11..33`). The games report events (`ReportCasinoEvent`); an objective
  whose category + filters match accumulates and pays its `_pointsawarded` to its
  axis; the level moves and grants the next reward rows.
- Roulette: `rebet()` (the retail Rebet button, `R`) and the **distinct-numbers**
  mask, for the "Use the Rebet button" / "See 12 of the possible numbers drawn"
  objectives; the winning bets are kept so the bet-type objectives
  (`_filterb` 36 straight-up, 43 column, 44 black/red, 45 even/odd, 46 half,
  47 dozen, 48 zero) fire.
- Poker objectives: `MoneyWon` / `HandsWon` / `WinStreak` on the cash game
  (`_filtera` 17) and the hand types (`_filterb` 60 one pair ... 65 full house).
- Status ladder from the objectives' own text ("Get the VIP status (level 5)"):
  VIP at total level 5, ACE at the table maximum (4+2+2+2 = 10);
  `/:isProgressCasino` is `true` now, and every casino screen's header shows the
  status, level, points to the next level and the objective count.
- Profile: `CasinoPoints` / `CasinoObjectives` / `CasinoRewardsGranted` /
  `CasinoSeen` / `CasinoRouletteNumbers` (`casino_points`, `casino_objectives`,
  `casino_rewards`, `casino_seen`, `casino_roulenums`), so the progression
  survives a save.
- `-logcasino` now also dumps the objective progress.

**Verified** (headless):

- `-pokerselftest 120` — 13/13 evaluator cases ok, wallet exact, the RTP estimate
  with its SE (above).
- **The progression, live**: a run of 45 slot spins at the max bet completed
  `'Play 3 types of slot machine'` (+35, axis 33), `'Play each type of slot
  machine'` (+10), `'Win ¤2500 at slot machines'` (+125, axis 32) and `'Play 40
  times with 9 lines activated at slot machine'` (+10), then
  **`Casino: axis 32 level 1 reached (574 points, objective)`** and
  **`Casino: unlockable 5 granted (casino level reward) 'Cocktail_10'`** — the
  level-up, the reward and the save all fired (`casino_unlockables = 5`,
  `casino_rewards = 1`, `casino_points = ...574,55,...` in `profile.sav`).
- A false positive found and fixed: "Play N **types** of slot machine" was
  counting plays, not distinct types (the port has one machine); it is now
  reported once per machine via the seen-flags.

**Still not wired** (documented in `docs/re/casino_spec.md` §8): the online casino
modes (Sit & Go, tournaments, private tables, friends, cocktails with other
players, the race — their objectives load and are shown but cannot complete
offline), the 3D casino island interior / terrace / cashier, the arrival and
departure cinematics, the dealer avatars, and the four retail game movies.

### Follow-up 2 (same session): the Game Progress page, and the hub merge

Merged the hub (`2ff3a17`, 16 commits: per-workspace exe names, `resolve_exe.bat`,
Meeting Tess, the Solar Crown drive, the sat-nav) — one conflict, `docs/STATE.md`
(both sides added session entries at the top; resolved by keeping both, newest
first). Rebuilt as **`build-cline\bin\opentdu2-cline.exe`** (the per-workspace
name; the stale `opentdu2.exe` is deleted) and re-verified the casino on the
merged tree. `launch/13_Casino.bat` now uses the hub's `resolve_exe.bat`.

Then the page the progression was missing — `casino_objectives_screen.{h,cpp}`:
**the Game Progress page**, one axis at a time (32 games / 33 variety / 34 social
/ 35 collection), every `DBCasinoObjectiveDetail` row with its counter against
its target, its `_pointsawarded`, and DONE once met. The Flash markup in the
texts is stripped. Reachable from the floor's new **`Game progress`** entry
(verified: `CasinoObjectives: open, 4 axis(es), 73 objectives on axis 32`,
screenshot `docs/images/casino_objectives.webp`) and wired to the pause menu's
retail `game_progress` entry (`eResult::Progress`, enabled by `setCasinoMode`) —
that entry is wired, but the movie-driven menu did not take the down press in the
automated run, so the floor entry is the verified route. `-objectives` opens the
page directly for tests. The floor's entry list is now 6 rows (relaid out so the
hint lines do not collide).

### Follow-up 3 (same session): the asset viewer and the asset labels

Owner: *"could a tool be made like an asset viewer which I can view and leave hints
to them maybe in some .txt ... I load a casino but file naming maybe random and I
can leave hints like casino exterior, slot machine, Tess character model etc?"* —
and on scope: *"something that can fully allow us to benefit from not just me but
AI to also use ... as long as it's accurate which I can test and confirm"*.

- **`source/game/asset_labels.{h,cpp}`** (new) — the shared vocabulary. Every
  bank entry carries its **original Eden authoring path** (`BankEntry::Path`,
  already parsed: `D:\Eden-Prog\...\EURO\FrontEnd\Cas_SlotMachine\fla_0047.2db`),
  so the labels key on the *identity* — `<bank path> + <asset filename>`,
  normalized — never a display name. Plain TSV in `docs/re/asset_labels.txt`
  (sorted, merge-friendly, greppable); `FindAssetLabel` / `AssetLabelOrName` /
  `SetAssetLabel` / `SaveAssetLabels` / `LintAssetLabels`, plus `-labellint` (every
  key must exist in the game data) and `-labellabels`.
- **`source/game/assetviewer/gm_asset_viewer.{h,cpp}`** (new) — `GM_AssetViewer`
  (`ASSET_VIEWER`, `-assets`), a game mode like WORLD_VIEWER: Banks panel (9,454
  banks under `Euro/Bnk`, filterable, labelled ones marked), Assets panel
  (filename, size, group, `*` when labelled; the filter matches the name *or* the
  authoring path), and a Preview + label panel showing the identity (bank,
  filename, **authoring path**, size, group, subtype, index) with a **pixel
  preview** for `2db` (an `ImGui::Image` of the decoded texture) and the label /
  note editor. `-bank <path>` and `-asset <name>` make it scriptable.
- `launch/14_AssetViewer.bat`; `docs/re/asset_viewer.md` (the spec);
  `docs/re/asset_labels.txt` seeded with the six casino movie banks.

**Verified**: `-labellint` → `6 label(s), 0 problem(s) - every key exists in the
game data`; a viewer run → `9454 bank(s)`, `'...cas_slotmachine.bnk' - 13
entr(ies)`, `'page_001.2db' - 2048x1024, depth 1, 1 mip(s), format 55`, and the
screenshot shows the **slot machine symbol atlas** with its authoring path above
it and the label fields below (`docs/images/asset_viewer.png`).

Not in it yet at the time (`docs/re/asset_viewer.md` §5): model / movie / audio
previews, and click-to-label inside a loaded scene — **finished in the session at the
top of this file** (the 3D view: turntable, picking, movie / sound / cube map previews).
## Session 2026-10-29 (forty-fourth, `win`, part 4): Meeting Tess car doors; FRIM on the game's own tuning file

Owner: *"car doors never opened so characters get in and out with the doors ever opening and lastly Frim needs a
little work and to be more accurate"*.

### What changed
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` - `updateCarDoors`: the scene car's doors / windows follow
  its .sce Message tracks (`opdl/opdr/opda`, `cldl/...`, `opw*/clw*`, argument = duration) with the retail door
  object law (garage_car_spec.md 2.2: `-/+ _doormaxangle x ease(f)`, ease FUN_00a48310), derived from the scene
  time; falls back to the `PLAYER_CAR` object. Meeting Tess: driver door 5.15-12.1 s (you get out), passenger
  77.77 s and driver 78.85 s (both get in), closed by 82.3 s. `-logdoors`, `-logshots`.
- **FRIM**: `scenarics/frpoints.cpr` is the game's FRIM tuning (rate curve, prize tables, level targets, trick
  scores) [DATA], copied verbatim by PS3 `0x7c9eb0`; the laws read from the readable PC code (`0xa769e0` level
  up / down, `0xa752fc` tricks feed the gauge, `0xa76e50` bank, `0xa76bd0` hit) and PS3 `0x7c8d24` (HUD vars) /
  `0x7cae18` (rate curve). Full spec: `docs/re/frim_spec.md`. `risk_driving.{h,cpp}` rewritten:
  levels now accumulate (the session-43 port banked and reset at every level), the gauge drains below
  ~40 % of top speed and a level can drop, tricks fill the gauge (near miss by gap 2.3 m / closing speed, jump
  by height + length, **drift** new), the bank is the player's (A) and pays `PA_TARGET[level]`, a hit costs
  nothing. `traffic.cpp`: near-miss zone 2.3 m (was 1.6) and the gap reported. HUD PALevel caps at 10.

### Verified
- Doors: `-logdoors` angles (left -1.0 open at 9.87 s, closing 11.72-12.37; right 0.138 / 1.0 at 77.87 / 78.39,
  left -0.504 / -1.0 at 79.42 / 80.42); 3D frames `docs/images/port-meeting-tess-doors-s44.webp`.
- `-riskselftest`: config read (PP 300..1200, PA $100..$4000, DEC_SPEED -30/0/3/8); 40 s at 240 km/h + a near miss
  every 2 s -> level 5, bank $750; crash keeps level 5; 20 s at 30 km/h (rate -21/s) -> level 7 -> 6.
- `-riskdemo -riskevent` 3D: badge climbs 2 -> 5 with the money readout.

### Not done / next
- FRIM: `PA_TARGET2` selector [ENC] (port always `PA_TARGET`), `PTS_CHAIN` (combo steps) unused, the jump badge
  should show metres (`%dm`), wheeling (bikes) absent. The `-nearmisstest` 5.5 m pass was hit by another live
  traffic car (closing 41.5 m/s) - the test needs an isolated agent.

## Session 2026-10-29 (forty-fourth, `win`, part 3): the story chain - knock / white -> Meeting Tess -> white flash -> drive to the Solar Crown

Owner: *"Wire the story"* (and earlier: *"should flash white and then you on a quest where you take her to the shoot"*).

### What changed
- `source/game/freeride/gm_freeride.{h,cpp}` - story step 2: when the white fade ends, `startDream()` hands the game to
  CINVIEWER (`GetCinViewerRequest`: meeting_tess.bnk, `C_MEETING_TESS` or `C_MEETING_TESS_W` when the profile avatar
  is a `*Female*` config, `StoryStepAfter 3`, `bFromStory`, next FREERIDE). `-nodream` keeps the old cut (and
  `-wakeup` still plays the knock first). Back from the dream (`StoryHandoff`): spawn at the car's end pose (the
  hotel drive), fade in from white (1.5 s), goal loc 6111 'On the road to "Solar Crown"', GPS destination
  `kSolarCrownTarget`.
- `source/game/cinematic/gm_cin_viewer.{h,cpp}` - a story cutscene ends with a 1 s white flash, then saves the step
  and switches; `StoryHandoff` carries the car pose.

### Verified
- Fresh user data, `-gm FREERIDE -skipcine -nopad -fixedtimestep -storytime 3`: `3 s elapsed -> fade to white`,
  `white fade done -> C_MEETING_TESS`, CINVIEWER at the hotel, `end of 'C_MEETING_TESS' after 66.7 s`, FREERIDE
  `from the dream: story step 3, GPS to the Solar Crown (12327, 16680), route 2515 m`, driving from (11094.98, 3.58,
  17215.65). 3D frames: white fade-in, goal bar, red route, GPS arrow (`docs/images/port-story-step3-s44.webp`).

### Not done / next
- **`kSolarCrownTarget` resolved**: the owner's 360 grabs of the shoot place (walled villa on a beach headland,
  gate on crazy paving) = the **club house** `ECH_IA` spot at (9874.5, 10.95, 14885.0) ("CLUB HOUSE" sign at its gate
  in the port); the target is the record's second point, the road outside the gate (9938.1, 8.95, 14834.8) [DATA].
  Route from the hotel 3983 m. The recomp's F6 world marks (hotel -1158.8 / 133.1, shoot -683.1 / 218.0) do not map
  to the port with one offset - do not use them as world coordinates.
- **The "intermittent exit -1 crashes" were not crashes**: the Cline agent runs `Get-Process opentdu2 | Stop-Process
  -Force` before its builds, killing every opentdu2.exe on the machine (found in its session log; cdb saw no exception
  and no in-process exit). Rule added to `WORKSPACES.md`; run long tests under a copied exe name meanwhile.
- No arrival trigger yet (Solar Crown ext / int cinematics), Tess is not seated as the passenger during the drive,
  and a relaunch at story step 3 still enters FREERIDE through the house flow.

## Session 2026-10-29 (forty-fourth, `win`, part 2): Meeting Tess plays at the seafront hotel with the phone and the keys

Owner: *"For that meeting it's wrong location and the phone and the keys are missing and the camera seems to cause
culling like issues"*; *"towards the end theres a audio that plays which shouldn't"*; then 360 recomp grabs of the
real place (a hotel with an arcade and semicircular fountains on the seafront).

**Reproduced**: `build\bin\opentdu2.exe -game ... -gm CINVIEWER -nopad -fixedtimestep -autoplay "1250:shot=..."`
(the viewer's default request is C_MEETING_TESS): played at the party house, car 0.43 m under the ground, no
phone / keys, line 10 heard twice at the end.

### What changed
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` - **animated props**: a prop with its own .XSB (meeting_tess:
  `key_ring_ferrari`, `a-phone`) is posed by its .sce Animation track (the `cf_kcar_*` / `cf_phon_*` clips, shared
  sampler `sampleAnimationTrack` with the avatars) and drawn skinned with avatar lighting; the **Visibility** track
  (multi-key only, props only) shows the phone 17.02..44.26 s and the keys from 14.10 s [DATA]. **Cinematic frame
  apart from the spot frame** (`Setup::bSceneAt`, `spotOrigin` for the exterior bank's sets / props),
  `offsetSceneOrigin`, `snapSceneToGround`.
- `source/game/cinematic/cinematic_scene.cpp` - byte-valued tracks (Visibility 0xd2df4b2c: 1 byte per key) were
  parsed as 0 components; now 1 component = the byte.
- `source/game/cinematic/cinematic_audio.{h,cpp}` - an Fx root still playing is not restarted (meeting_tess fires
  line 06 from `Stuart_Phone` and `Fx_Synch` at the same time and line 10 twice on Tess, 56.42 and 65.74 s: the
  second copy was the "audio at the end") [INF: one sound instance per Fx resource]. `-logcinsound`.
- `source/game/cinematic/gm_cin_viewer.cpp` - C_MEETING_TESS defaults to the hotel placement (see
  `docs/re/cutscenes_inventory.md` "RESOLVED"); `-cinsceneat x,y,z,h`, `-cintopview`, `-logentities` prints
  Znear / StreamCenter, `-logcin` prints the camera heading in the recomp's convention.

### Verified
- Default CINVIEWER run: `cinematic frame at (11103.00, 4.60, 17252.00) heading 0`, `snapped ... -0.14 m`; frames at
  the arcade with feet on the paving (`docs/images/port-meeting-tess-hotel-s44.webp`), keys in Tess's hand
  (`port-meeting-tess-keys-s44.png`), phone at her ear, key toss; top view `port-meeting-tess-topview-s44.png`.
- `-logcinsound`: 16 clips, `root ... still playing, not restarted` for 06 and 10.
- 6 runs x 75 s (`-no3d -noaudio`, with / without `-nopropanim`) all exit 0.

- **Grounding (owner: car and legs sunk)**: `-logground` measured the tyres 0.03..0.22 m and the feet 0.21 m
  under the surface. Two causes: the ground query saw only the island (the hotel's drive is a mesh of the e_hotela
  exterior bank the cinematic loads itself, 0.25 m above the terrain), and a single-point snap. Now
  `IntroCinematic::getSurfaceHeight` also hits the exterior meshes (kept on the CPU), and `snapSceneToGround` fits
  a plane under the four tyres and lifts / tilts the whole cinematic frame onto it (car, seated driver and
  standing actors stay together) [INF]. Result for all 68 s: hubs 0.34 m = the tyre radius, feet -0.03 m
  (`docs/images/port-meeting-tess-grounded-s44.webp`).

### Ruled out
- Near plane: every cinematic entity of new_intro / fade_freeride / meeting_tess has Znear -1 -> 0.8 (the retail
  default) [DATA]. Car LOD (`-carlod 3/4`) and roof (`-hardtop 0/1`) do not change the in-car shards.
- Candidate places: party house EPH_L3DI (36382 / 22210), the E_HOTELA spot frame itself (0.9 m sunk, side terrace),
  ech_ia/ib/ic (unplaced variants), six spots near roundabouts (top-down shots).
- Earlier `exit -1` crashes (3 of ~8 CINVIEWER runs) did not reproduce in 6 later runs; cause unknown
  (no exception under cdb). Watch for it.

### Not done / next
- **The black shards in the first in-car shot** (t 0..5.5 s): not the near plane, LOD or roof; next A/B: hide Tess
  (her hair cards) vs the car, and dump the skinned item palettes of the California HANIM_S at that shot.
- **Line 11** (`IntroNew-E_Meet_Tess_11`, VOICE_AVATAR, the player's reply) never plays: the .sce Avatar object
  carries no Fx for it; find which object / bank holds it (male.fxa morph ref 0306 exists).
- ~~Story hook-up~~ **done (part 3 below)**.
- Recomp `[bank]` hook (owner's idea, not started): log every bank the 360 engine opens out of the .big files
  from `src/hooks/tdu2_scene_log.cpp`, to see asset swaps per scene.

## Session 2026-10-29 (forty-fourth, `win`): the sat-nav speaks — retail GPS voice laws decoded, route starts on the nearest road link

Owner: *"could the sat nav audio be hooked up"* (after the merge of `glm` sessions 42-43 and `cline`
into `win`, commit `1c88b79`).

### Decoded (readable PC code + data; full spec `docs/re/gps_voice_spec.md`)
- `sound/voices/<lang>/gps.bnk`: 17 unnamed MS-ADPCM clips [DATA]; transcribed (faster-whisper on
  the decoded PCM): 0 wrong way, 1/3 "at the next intersection turn right/left", 2/4 "turn
  right/left", 5..9 destination 500 m..3 km, 10..14 half a mile..3 miles, 15 arrived, 16 recalculating.
- `GPSVoice::PlaySound` FUN_00d84950 (FlashSounds.cpp), client `VoiceGPS`; busy -> cue dropped [CODE].
- Turn law FUN_00934740: gates **200 m** (far line) and **50 m** (near line); the angle is heading out
  minus heading of the link BACK (90 right, 180 straight, 270 left): right if < 121, left if > 241,
  silent between — so only bends of ~60 deg+ speak [CODE]. (The first version of this session read
  it as the deviation from straight and announced 30 deg bends; fixed in the arrow commit.) Countdown FUN_00934a10: 3000/2000/1500/1000/500
  within a 50 m window, x0.621504 for miles [CODE]. Wrong way at 0x936fb8 (2 s), arrival 0xa574c2 [CODE].
- Not readable/not found: the caller of "Recalculating" (none in the readable half), how `[+0x84]`
  (wrong-way time) is measured, the road ids the turn law compares -> [INF] port rules in the spec.
- **The road links leave junctions on a 30 deg grid** (every turn on 3 Ibiza routes is a multiple of 30) [DATA].

### What changed
- `source/audio/gps_voice.{h,cpp}` (new) — `GPSVoice`: loads gps.bnk (gpConfig language, fallback
  us), plays a cue on the VoiceGPS client (bfx 5), drops a cue while one plays (retail log line).
- `source/game/freeride/gps_guidance.{h,cpp}` (new) — `GpsGuidance`: the laws above, ticked after
  `MapScreen::tickRoute`; `-gpsmiles` switches to the imperial lines [INF until the gps_Unit option].
- `source/game/map_screen.{h,cpp}` — `RouteTurn` list (junction nodes with 3+ neighbours, clockwise
  angle 15 m before/after), `projectOnRoute`, `setTarget`; **the route now starts on the nearest
  road link** (both nodes seeded with the distance along its curve) instead of the nearest node,
  which could be on another road and sent the route around; `tickRoute` stores the car position
  even without a target.
- `source/game/freeride/gm_freeride.{h,cpp}` — loads/ticks the guidance; `-gpsdrive x,z[,speed]`
  test harness (virtual car moved along the route; negative speed = backwards).

### Verified
- `-tp 35976,5805 -logroute -gpsdrive 31000,9000,40 -no3d -fixedtimestep -exitframe 14000`:
  cue 2 (30 deg junction at 100 m), 3 then 4 at junction 713 (270 deg), 1 then 2 at 714 (120 deg),
  countdown 9/8/7/6/5 at 2999.6/1999.4/1499.5/999.5/499.5 m, destination reached -> cue 15.
  (Two near cues logged "Can't play ... playing": -no3d runs the sim ~5x faster than the audio
  clock, a test artifact.)
- `-gpsdrive 31000,9000,-15` (backwards): "wrong way (2.0) -> cue 0".
- Load log: `17 of 17 samples, client 0 (VoiceGPS, pipeline 0), FREERIDE mix gain 1.00`.
- Left/right convention: 3D frame at junction 713 (`docs/images/port-gps-junction-s44.webp`) — the
  road leaves up-left both in the view and on the minimap, so the minimap's right vector (used for
  the turn angle) is not mirrored.

### Ruled out
- "Off the route (448 m / 3 km)" false recalculations: the route started at the road node nearest
  the car, on another road -> fixed by the link-seeded start; the last one was the story fade
  (state != Drive) feeding the real car to the harness -> harness keeps the virtual position.

### HUD turn arrow (second commit of the session)
- FUN_00935fd0 junction classifier ported (bands table in the spec): `up rightup right rightdown down
  leftdown left leftup` + `_ra` at roundabouts (EDON byte +10 == 3, retail `[node+0xa] == 3`) — the
  string table 0xf75fd8 equals the labels of `/HUD/arrow_gps/arrow` [CODE + DATA]. Shown for the next
  junction within 200 m (`[+0x1cf0]` = 0xef0240); `/:ArrowDirection`, `/:IsRoundAbout`, then
  `open` / `change` / `close` on `/HUD/arrow_gps` (0x937693..0x9377cc) [CODE].
- `MapScreen::RouteTurn::Arrow` (+ `roadNodeKinds`), `GpsGuidance::getArrow`, `FreeRideHud::showGpsArrow`.
- Verified: same `-gpsdrive` route — 855 (30 deg bend, angle 150) -> arrow `up`, no voice; 713 (angle
  270) -> `left` + "turn left"; 714 (angle 60) -> `rightdown` + "turn right". 3D frames:
  `docs/images/port-gps-arrow-up-s44.webp`, `port-gps-arrow-left-s44.png` (the movie's own arrow art).

### Not done / next
- **Not heard by a person yet** — the owner should drive a route: set a destination on the map
  (Select/Share -> cursor -> Cross/A), and listen at junctions. Keyboard: the map key, Enter.
- The `_ra` set is verified in the log only (`-tp 25000,21000 -gpsdrive 20500,24500`: Ibiza Town
  nodes 15 / 22 / 34 -> arrow 8 `up_ra`), not on screen; the clip call vtable+0x100 with 10.0 before
  the label (0x9376ff) is unknown.
- Passenger "GPS Live" voice (hs banks, FUN_009fae80 + the byte map 0x933ef8) once Tess rides along.
- The gps_Unit option on the Game settings page (`pause_menu.cpp`, the `opts:` protocol) -> `GpsGuidance::setImperial`.
- The map's "You have reached your destination" goal text is still the [INF] string; the voice line is retail.
## Session 2026-10-29 (forty-third, `glm`): the FRIM gauge is the retail movie element; return-to-road prompt; traffic tires and lights

Owner: *"plumb in the UI for return to road, and frim also … the values are also
wrong in our version … I believe the ibiza icons are missing some parts …
traffic tires … clip through the road … the traffic's car lights … slightly a
little further forward or behind the vehicles"* (with a retail FRIM grab:
badge "1", gauge bar, three perf icons, "$100", "F.R.I.M.").

### The retail FRIM, decoded (subagent pass over the readable PC half)
`FRIM` is the game's own name (hud.bnk strings, and the config flag
`DisplayFRIM` next to `DisplaySpeedometer` at 0xf18a3f [CODE]). Manager
singleton `[0xfd92d8]`, UI driver 0xa74000..0xa77000, game-side detectors
0x8c3xxx. The flow [CODE]:
- the gauge fills **while driving fast**: `progress += rate(speed) * dt`
  (0xa767c0, rate curve 0xa75920 — runtime data of the encrypted band [ENC]);
- a full gauge is a **level**: level++, play the bank animation
  (`gotoLabel("on", "HUD/FRG/FR/fr_gge/bank")` 0xa76aad); the movie asks the
  game with **FSCommand `frgp:bank`** and the game pays `moneyTable[level]`
  (`AddMoney(amount, 0, 0x14)` 0xa76e50/0xa76f45) and resets the whole chain
  (0xa767f0). The movie shows an **[A] "BANK" prompt** — banking is
  player-confirmed (verified live in the port, `port-frim-bank-s43.png`);
- **a hit does NOT take money**: it sets a chain-dead flag for 2 s and plays
  "hit" (0xa76bd0 — types 0 and 3 of the collision list are ignored, i.e.
  scrapes don't count [INF]); the level is kept;
- the perf events (dodge / jump / drift / wheeling) are **badges**, not pay:
  their points go to `/:LastPts`, `/:PerfNb` and the profile bests
  (+0xf454 best jump m, +0xf45c best count, +0xf460 best single, +0xf464 best
  bank). Dodge: pass speed >= 5 m/s, per-car 2 s dedup, multiplier ladder
  {2,5,7} nearby cars [CODE file defaults]. Wheelie gates 0.1° start / 5° keep
  [CODE]. Drift gate 0.1 rad [CODE]. Jump is fully in the encrypted band [ENC]
  (its badge shows the air distance, "%dm" format 0xf1e46c);
- HUD vars (0xa764a0): `/:PerfPts` = progress, `/:PerfPtsTarget` = the level
  threshold (movie default 1000), `/:PALevel` = level+1 (the badge),
  `/:PACurLevel` = moneyTable[level] (**the "$100" of the owner's grab ->
  moneyTable[0] = 100**), `/:PANextLevel` = moneyTable[level+1],
  `/:PATotal` = chain total, `/:FRIMLevel10` at level 10.
- The money tables (2 x 11 floats, day/night pick by an index of encrypted
  0x445780 [INF: hour]) and the 10 level thresholds are injected at runtime by
  the SecuROM band into BSS blocks 0x11C751C / 0x11C767C — **not recoverable
  from the readable exe, not in the database either** (db_data.cpr has no FRIM
  table; verified with the xmb tool schema dump).

### What changed
- `source/game/freeride/risk_driving.{h,cpp}` — rewritten to the retail model:
  speed-rate gauge -> level -> [A] BANK (validate) -> pay `kMoneyTable[level]`
  -> reset; hits lock the chain 2 s (no money lost); dodge/jump publish badge
  points. The values are [INF] in the retail shape (table {100, 200, 400, ...},
  threshold 1000, rate 0.5 pts/s/kmh over 60): **the retail tables are [ENC]**.
  `-riskselftest` rewritten (20 s at 180 -> level 1, bank pays $200, resets).
- `source/game/freeride/freeride_hud.{h,cpp}` — `updateFrim` drives the REAL
  movie: FRG_on/FRG_off on /HUD/FRG, then FR "on"+"start" (the slot-position
  script f10 and the slide-in f27..f45; each label section ends in a Stop
  [DATA sprite 909]); vars PerfPts/PerfPtsTarget/LastPts/PtsType/PALevel/
  PACurLevel/PANextLevel/PATotal; per event new_pts + the icon's "on"
  (/HUD/FRG/FR/dodge|jump), on crash "hit", on level-up fr_gge/bank "on";
  `playFrimBank` (A) jumps fr_gge/bank to its "bank" pay segment; frgp:*
  FSCommands route to `RiskDriving::onBankFSCommand` via
  `setBankCommandCallback` (fallback: pay 5 s after a level-up if the movie
  never asks, logged). The old text HUD (drawRisk) is removed.
- `source/game/command_resolver.{h,cpp}` (new) — the Default_KB.xmb
  '#command:<ACTION>#' resolver moved out of the first walk mode (plus
  `GetActionBindings` for its ACCESSORIES_LEFT keys); FREERIDE sets it on
  enter and clears on exit.
- `source/game/freeride/gm_freeride.cpp` — return-to-road prompt: off the road
  network (no road triangle within 12 m at the car's height) for > 2 s shows
  loc 2216 "Press and hold #command:BACK_ON_TRACK# to put the car back on the
  road" in the goal bar ([INF] carrier; the markup resolves to "[B]" via the
  resolver, `port-return-to-road-s43.png`); back on a road it closes. The
  FRIM bank wiring (setOnBank -> Money + save; validate banks while
  `risk.isBanking()`); `-riskevent` (scripted near miss every 4 s, for
  headless HUD checks).
- `source/game/freeride/traffic.{h,cpp}` — **tires**: the model pivot sits
  below the contact plane (WLR_FL wheel centres at model y 0.05..0.16, tire
  radius ~0.30): `GroundLift = _tire_radius/200 - WLR_FL y` per type
  (0.16..0.27 m, the cars sat that deep in the road); applied to the draw
  matrix. **Lights**: the FRL_* quads (TD_Sprite additive 'light_fx' /
  'light_fxred') are the head/tail glows and glowed white in daylight; they
  are hidden by day and shown at night, gated per draw on the weather hour
  (< 6 or >= 20, the same rule the density mix uses). Debug: `-trafficpark`
  (pins 3 cars in front of the camera) and `-logtraffic` (bounds + node dump;
  namespace-scope CmdLine statics are NOT reliably initialized — use a
  function-local static, the isRed pattern).
- `source/game/map_screen.cpp` — `-logmapspots` (the spot-timeline section
  dump that pinned the icon structure: one frame-swapped art shape on depth 26
  per label section, children news/shape/glow/target/hlight/link/rank/lock/
  forbidden/favorite/done/offroad).

### Verified (headless)
- `-riskselftest`: level up at 180 km/h, crash keeps the level, bank pays
  $200 and resets (log).
- `-riskdemo -logfrim`: FRG_on tween runs (frames 1->9), FR plays on+start
  (stops at 26/45), the gauge draws (`port-frim-gauge-s43.png`); `-riskevent`:
  bar fill + dodge badge + "Combo!" (`port-frim-event-s43.png`); the level-up
  shows the [A] BANK prompt with PACurLevel $200 (`port-frim-bank-s43.png`).
- Return to road: `-tp 11464,8962` (2.5 km from the nearest road node; found
  by scanning Ibiza_ROAD_MAP.IA offline) -> prompt after 2 s, "[B]" rendered.
- Traffic: `-trafficpark` day -> no glow quads, tires on the road
  (`port-traffic-day-s43.png`); `-weather 23.00` night -> glows on the lamps
  (`port-traffic-night-s43.png`).

### Ruled out
- Hiding the spot status children ("resetIconStatus" guess): `shape` is the
  tile art and the offroad child draws the bottom-left tab — the icons lose
  their tile/tab (compared against the session 25 grab). Reverted; the note
  stays in buildSpots.
- The map spot icons being clipped: pixel-measured the tile runs — identical
  to the session 25 grab; the icon art is complete.

### Not done / next (start here)
- **FRIM values**: the retail money tables / thresholds / rate curve are
  runtime data of the encrypted band (blocks 0x11C751C / 0x11C767C). Route:
  PS3 twins (the port's `ps3_enc_twins.json` method) or live 360 oracle.
  The port's numbers are placeholders in the retail shape.
- **FRIM polish**: the "$" readout of the gauge shows the PACurLevel value
  only inside the bank animation in our runs (retail's grab shows it steady);
  drift / wheeling detectors do not exist yet (the port has no drift angle or
  bike); the jump badge shows points, retail shows metres ("%dm").
- **"Ibiza icons missing some parts"** — needs the owner's word on WHICH
  icons. Verified complete: map spot tile art + glyphs + tab (pixel-compared).
  Best candidates: (1) the **event / challenge icons** — retail tab 0 is full
  of DBEvent race icons, the port places only spots.bin shops/schools (session
  26 "no event/challenge spots"); (2) the rank medals (the rank child has
  bronze/silver/gold labels, never driven); (3) the zoom LOD switch
  (zoom_default/zoom_lod labels, `navigator.setZoom` never called).
- The goal bar truncates the return-to-road text ("...on the ro…"): retail
  carries it in a wider element or wraps [open].
- Traffic: no headlight light pools on the road at night (the 'headlight'
  TD_ShadowBox is skipped: "projection constants not implemented").


## Session 2026-10-29 (forty-second, `glm`): the flicker freeze-proof lands — animated casters drive it (115 -> 13 cells)

Continuation of session 41 in this workspace. Owner: *"if this bug is 100% squashed..."*.

### What changed
- `source/game/freeride/gm_freeride.cpp` — `-hideinstances x,z,r` (radius only: flat distance of
  the instance origin): passes `EdenWorld::setInstanceHide` after `view.initialize` (the
  cinematic hide plumbing reused; logs `FREERIDE: instances hidden within ...`).

### Verified (session 41's wiggle, same metric: 40x40-cell lit/shadow state transitions vs `-noshadows`)
| Run | Flickering cells |
|---|---|
| baseline (session 41) | 115 |
| `-hideinstances 35976,5805,30` | 48 — and the banner-region cells left the top list |
| `-hideinstances 35976,5805,90` | **13** — residue: a band on the (now hidden) right building's wall position x 1040-1200 y ~200, the far hillside, the car shadow edges |

Scene evidence with the hide on (`_hf210.png`, `_k90f210.png`): the left building and the right
building are **instances** (the right one only fell to the 90 m radius: its instance ORIGIN sits
far from its visible extent), palms/trees/planters are instances; **the BIENVENIDOS banner and
the bunting are NOT instances** — they stay (area-bank scene graph) and keep waving.

### Established
- Removing animated/popping casters removes the flicker of the surfaces their light rays feed,
  region-wide and at a fixed camera. The mechanism class of session 41 is confirmed.
- The caster pass does NOT draw the same vertex stage as the depth prepass: vegetation uses its
  own family's `S_PASS_SHADOWCAST` permutation, everything else `TD_ShadowCaster`
  (`eden_renderer.h:187`). `Time_` is per-frame (eden_scene_view.cpp:270) and copied into the
  caster scene — both passes see the same clock, so the phase is NOT the difference; the
  difference must be the vertex stage itself.

### Refined theory of the own-caster mismatch [INF]
The banner's own-cell margin ping-pongs +-4 cm at a fixed camera while both passes share
scene.Time. If `TD_ShadowCaster` (or the shadow permutation) does not apply the wind/cloth
displacement exactly like the full vertex shader, the map holds the banner displaced
differently than gViewPos — cm-scale, oscillating as the wave travels. Downstream surfaces
whose light rays graze the banner flip whole-region when the nearest-caster depth crosses
their own. Instance pop (session 40's "culling <= 212 px" note) is the second driver.

### Not done / next (start here)
1. **The verdict experiment for the fix**: dump the disassembly/SPIR-V of the banner's full VS
   (its material family, likely TD_Building or a cloth family) vs `TD_ShadowCaster` and diff
   the displacement code (Time_ + wind constants). If the caster variant lacks it, porting the
   displacement into the caster path (or per retail: verifying what TD_ShadowCaster does — it
   may apply a SIMPLIFIED wind that the port's constant mapping zeroes) is the fix. Check the
   constants the caster draws receive (`UserVector*`, wind phases are material parameters).
2. Then re-run this wiggle: target is 115 -> ~0 without `-hideinstances` (the flag is debug
   only; the car-shadow-edge residue may remain — bias/slope, a separate smaller instance).
3. The house-walk floor flips (session 40) and character-select avatar flips: re-test after
   the fix; they may share the animated-caster cause (party decorations) or not.
4. `-hideinstances` stays as a debug flag (documented above); it hides instances from ALL
   passes (setInstanceHide is world-level), so it is not a fix by itself.

## Session 2026-10-29 (Cline, `cline`): the island buildings, the pad in the map / pause screens, the photo camera

Owner: *"add the dealerships, shops and other stuff to the islands ... I tried in launch to a dealership the plot
was there but no dealership building ... controller support in the map and pause screen — camera tool. Last I
tried I couldn't look around to take a pic and the map seemed I couldn't zoom in or out with controller and map
needs a little bit of fixing up"*.

**Reproduced** (the plot with no building):

    build-cline\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-cline-userdata ^
        -gm FREERIDE -skipcine -nopad -tp 21796.3,24188.1 -heading 103 -storytime 86400 ^
        -autoplay "120:shot=C:/OpenTDU2-cline/_before.ppm" -exitframe 200

The spot's plot was empty (the prompt "A / Enter: enter the dealership" was up: `findSpotNear` found ECD_FERR) —
FREERIDE drew no spot exterior at all. After this session the same command shows the Ferrari showroom
(`docs/images/freeride-dealer-building.webp`).

### What changed

- `source/game/spots/spot_exterior.{h,cpp}` — **moved** from `game/worldviewer/` (unchanged apart from logging
  the models' local bounds: the footprint of the building in the spot frame).
- `source/game/spots/spot_exteriors.{h,cpp}` (new) — `SpotRecord` + `LoadSpotList(island)` (spots.bin) and
  `SpotExteriors`: the streamed set of exterior banks around a position (1500 m in / 2000 m out, 10 m retest
  granularity), the lightmap / shadow-caster rebuild when a bank is dropped, `excludeBank()` for a bank another
  owner draws, `draw()`.
- `source/game/worldviewer/gm_world_viewer.{h,cpp}` — the duplicated spots.bin reader and exterior streaming
  replaced by the shared `LoadSpotList` / `SpotExteriors` (the panel, `-spot`, `-noexteriors` unchanged).
- `source/game/freeride/gm_freeride.{h,cpp}` — **the buildings are on the islands**: `spotExteriors.load(island)`
  at enter, `tick` around the car every frame (and around the map camera while the 3D map view is up), drawn
  from the scene draw callback in every pass except `ProjectorActors` (lit, shadow casting, reflected);
  `-noexteriors`; the house bank of C_FIRST_OUTRO excluded (its texture owner / lightmaps belong to
  `FirstOutro`, which now has a public `registerLightmaps` and is re-registered after a bank is dropped).
- `source/game/freeride/photo_camera.{h,cpp}` (new) — **"Take photo"** (the pause entry `photo` / `phmo:phin`):
  a free camera at the car (world live, car input off, HUD hidden), look with the right stick / mouse, move with
  the left stick / WASD, LT / RT down / up, LB / RB zoom (25-90 deg), A takes a **PNG** into
  `<userdata>/photos/photo_NNN.png` (the frame the shutter fires in is drawn without the controls bar or the
  flash), B back. `RenderDevice::captureSwapchainImage` writes PNG through `stb_image_write` when the path ends
  in `.png` (PPM otherwise, the headless runs unchanged).
- `source/game/pause_menu.{h,cpp}` — the `photo` entry is unlocked and reports `eResult::Photo`; the movie-driven
  menu's `phmo:phin` FSCommand is the retail hook and does the same (`-logpausesq` now dumps the timeline of
  every square, which is where the command was found); the **left stick navigates** (movie SWF keys 1/2/14/15,
  and the game-driven row) with a hold repeat — the pad's D-pad was the only navigation before.
- `source/game/map_screen.{h,cpp}` — **the pad zoomed nothing**: `updateIslandView` read
  `wasPressed(IA_CarAccel / IA_CarBrake)`, and LT / RT are *analog axes*: they never produce a press edge.
  `takeZoomStep()` now steps on the axis crossing the threshold and repeats while it is held (0.35 s, then every
  0.30 s); the wheel is unchanged. Also **the sea was invisible** on the flat map: the rect's colour word is
  AABBGGRR and the literal `0x002e0e00` has alpha 0 — it is `a8 | 0x002e0e00u` now
  (`docs/images/map-flat-sea.webp`).
- `source/game/gm_base.cpp` — `GetGameModeFromName` ignored underscores: `-gm worldviewer` (as
  `launch/10_WorldViewer.bat` passes it) matched nothing and dropped the run into LOGIN. Names are compared on
  their alphanumerics now, so all three world viewer launchers work.
- `launch/05..07` (dealership / tuning shop / used car dealer) — `-snaproad 150` instead of a heading: the
  spot position is the **building's origin**, so the car used to spawn inside the building (it is parked on the
  nearest road outside it now; the footprints are in the log).
- `docs/re/spot_exteriors_spec.md`, `docs/re/photo_camera_spec.md` (new specs).

### Verified

- Buildings: the Ferrari spot (above) draws the showroom with the display cars; the log reads
  `SpotExteriors: ibiza: 79 spots, 79 with an exterior bank`, `ECD_FERR at (21796, 24188): 2 models,
  2 placements`, 6 banks loaded around the car. Hawai: `hawai: 144 spots, 144 with an exterior bank`,
  `ECH_HA ... 4 models, 10 placements` at `-island hawai -tp 11241.4,32482.8`. House flow (ibiza, no `-tp`):
  `SpotExteriors: 'Euro/Bnk/islands/ibiza/exterior/eph_l3di.bnk' excluded (owned by the mode)`, the car spawns
  and drives as before, no error. `-noexteriors`: the list is read, no bank is loaded. The world viewer
  (`-gm worldviewer -spot ETUN_A`, the launcher's own lowercase spelling) streams 5 banks around the spot.
- **Merged `win` after the work** (sessions 41-42: the D-pad accessories wheel / radio, the Ibiza tunnel
  terrain): one conflict, `docs/STATE.md` (both sides added a session entry — both are kept); the merge is
  committed and the whole verification above was re-run on the merged tree (the dealership, the map zoom, the
  photo camera and the house flow all still behave; only the pre-existing `specificHelp` Flash warnings).
- Map zoom: `-autoplay "60:press=map;150:hold=accel;200:release=accel;240:shot=_map1.ppm;270:hold=accel;
  320:release=accel;360:shot=_map2.ppm"` — level 0 (the flat map, 62 % of the frame the sea colour 0,14,46)
  -> level 1 -> level 2, i.e. a *hold* on the analog axis steps the zoom (it did nothing before).
- Photo camera: `-autoplay "60:press=pause;100:press=right;130:press=right;160:press=right;200:press=validate;
  300:press=validate"` — the log shows the movie firing `phmo:phin`, `PhotoCamera: open at (21794.8, 13.7,
  24191.7)`, `PhotoCamera: photo 1 -> '<userdata>/photos/photo_001.png'` (523 KB PNG, no HUD, no bar — the bar
  region of the capture frame is the scene, 90 vs 53 in the screenshot with the bar) and the controls bar
  renders in the screenshot (`docs/images/freeride-photo-camera.webp`).

### Ruled out

- The empty plot was **not** a sector / bank problem: the spot's exterior bank exists and loads in the world
  viewer (`-gm worldviewer -spot ECD_FERR`), and the same bank loads in FREERIDE now — FREERIDE simply never
  streamed or drew any spot exterior.
- The house flow was **not** broken by the streamer: with the bank excluded there is no second texture-cache
  owner and the lightmap list is re-registered.

### Not done / next

- **The buildings have no collision** (drive-through); the exterior banks may carry `.shk` like the house one
  (`FirstOutro` loads `eph_l3di_sc_*.shk`) — check `ecd_ferr.bnk` first, then add it to the streamer.
- The retail **photo mode UI** (`photomode_menu` / `photomode_console`, `phop:*` filters / contrast / exposure,
  `phsa:*` save dialog, `sndp:shot`) is not driven — the native camera replaces it; see
  `docs/re/photo_camera_spec.md` §3.
- The map's 3D levels (1-3) still end in blocky sector edges beyond the streamed radius (session 23's cap);
  the flat map's sea colour is a guess at the texture's own blue `[INF]`.
- `_low` exterior banks are unused (no exterior LOD).
## Session 2026-10-29 (forty-second, `win`): D-pad accessories wheel / radio decoded from retail code; the Ibiza tunnel "full of dirt" is the sector heightfield, diagnosed

Task (from the coordinating session, the owner's wishes): *D-pad LEFT opens the in-car menu
with all 4 directions like retail, and while it is open RIGHT must not open the radio; D-pad
RIGHT with nothing open opens the radio ONLY (today it skips the song instantly); in the radio
RIGHT = next station, UP / DOWN = volume.* Plus: *a tunnel on Ibiza renders filled with dirt.*

### 1. Accessories wheel + radio panel: the retail controller, read from the readable PC code

The strings `HUD/ig_menu`, `ValidIGM`, `clil`/`clir`/`hood`/`wind`, `RadioAutoHideDuration`,
`AccAutoHideDuration` sit at 0x00f2f984.. and every reference is in **readable** code
(0x009284e0..0x00929d2b), so this is `[CODE]`, not inferred:

- **FUN_00929be0** (per-frame): reads actions **0x34..0x37 = `ACCESSORIES_LEFT/RIGHT/UP/DOWN`**
  (the action-name table at 0x00f7f100, 16-byte entries) and switches on the state at +0x18:
  - **0 (nothing open)**: RIGHT -> FUN_009284e0 (radio `open`, state 2, timer 3 s);
    else LEFT -> FUN_00928c10 (wheel `open`, state 1, timer 4 s).
  - **1 (wheel)**: FUN_009293f0(up, down, left, right): UP -> entry `hood`, DOWN -> `wind`,
    LEFT -> `clil`, RIGHT -> `clir`. Available: FUN_00928e70 calls the movie's
    `ValidIGM(label)` and sets state 0. Not available: "wrong" sound, timer re-armed.
  - **2 (radio)**: FUN_009297c0(up, down, left, right): UP -> volume +0.1 (FUN_00929660),
    DOWN -> -0.1 (FUN_009295c0), clamped [0, 1] (step = the double at 0x00f49af0);
    LEFT -> radio `close`, state 0; RIGHT -> FUN_00928690 = the station change (same call
    0x00aea2c0 as RADIO_CHANNEL).
  - Auto-hide: the timer counts down, FUN_00929100 closes at 0. Defaults
    `RadioAutoHideDuration` 3.0 / `AccAutoHideDuration` 4.0 (0x00f75f14 / 0x00f75f18,
    FUN_00929150; no override found in the unpacked data).
- **FUN_00929710**: the direct `RADIO_CHANNEL / VOL_LESS / VOL_MORE` (0x29..0x2b) handler —
  opens the radio and, on the same press, steps the station / volume. It only runs while the
  byte at 0x00fd8fef is clear. `DeviceX360Pad.xmb` binds those three to the **same D-pad**
  as ACCESSORIES_* `[DATA]`, which is exactly the owner's "right skips the song instantly".
- **The movie side** (`hud.bnk`, sprite 1028 `/HUD/ig_menu`, frame 0 scripts, AVM1
  disassembled with `-dumphud -dumpsprite 1028 -dumpactionbytes`) `[DATA]`:
  `SetIcons()` places **icon_1 (up) = hood, icon_2 (right) = clir, icon_3 (down) = wind,
  icon_4 (left) = clil** (the icon positions at `stop`: up (3747,909), right (4723,1918),
  down (3747,2903), left (2732,1918)), shows an icon's red `lock` when `/:HasHood` /
  `/:HasWindows` is false, sets `IG_Title = _root.m_Accessories`; the `open` timeline calls
  it at frame 10 and holds on `stop`. `ValidIGM(label)` plays `valid_icon` `valid` with the
  entry glyph + name, then `close`. The game side (FUN_00928840) sets `/:HasHood`,
  `/:HasWindows`, `/:HasTurnSignals` before `open`.
- **Volume bar**: `/HUD/radio/volume` is an 11-frame clip; the game stops it on frame
  `2 + trunc(9 * volume)` (frame 1 when muted) — FUN_009297c0 tail `[CODE]`.

### Buttons (DualSense / keyboard)

| What is open | D-pad (keyboard numpad) | Does |
|---|---|---|
| nothing | **Right** (6) | opens the radio panel only (starts the radio if off) |
| nothing | **Left** (4) | opens the Accessories wheel |
| radio | **Up / Down** (8 / 2) | volume +/- 0.1 |
| radio | **Right** (6) | next station (Hariba -> Kalua FM -> Roadtrack) |
| radio | **Left** (4) | closes the panel |
| wheel | **Up** (8) | roof (open / close) |
| wheel | **Down** (2) | windows (down / up) |
| wheel | **Left / Right** (4 / 6) | left / right indicator (applies, closes; no light driver yet) |
| either | **Circle** (Backspace / Esc) | closes it `[INF: not in the retail controller]` |
| either | (wait) | auto-hides after 3 s (radio) / 4 s (wheel) since the last press |

Keyboard **1 / 2 / 3** (`RADIO_CHANNEL / VOL_LESS / VOL_MORE`, keyboard only now) open the
radio, then 1 changes the station and 2 / 3 the volume. Autoplay: `accleft accright accup
accdown` (new), `radio volup voldown` (the keyboard radio keys).

### What changed
- `source/input/gs_input.*` — new actions `IA_AccessoriesLeft/Right/Up/Down`
  (`ACCESSORIES_*`, D-pad from the pad map, numpad 4/6/8/2 from Default_KB.xmb; defaults
  added). `loadPadMap` skips the pad's `RADIO_*` entries (same D-pad; see FUN_00929710 above)
  `[INF: retail gates them with 0x00fd8fef]`.
- `source/game/freeride/gm_freeride.cpp/.h` — the radio / car-menu block replaced by the
  FUN_00929be0 state machine (constants `kRadioAutoHideDuration`, `kAccAutoHideDuration`,
  `kRadioVolumeStep`). The old up/down-select + validate scheme is gone. A locked entry (car
  without hardtop / windows) keeps the wheel up. The keyboard channel key no longer skips on
  the press that opens the panel `[INF]`.
- `source/game/freeride/freeride_hud.cpp/.h` — `showCarMenu(visible, hasHood, hasWindows)`
  now sets the three flags and plays `open` (the movie's SetIcons lays out the icons — the
  old code jumped to `stop` and placed them itself in the wrong order: the left-arrow
  indicator was on top); `validateCarMenu(entry)` invokes `ValidIGM`. `kCarMenuEntries` is in
  icon order (`CME_Up/Right/Down/Left`). Volume bar = the retail frame formula.
  `-dumpactionbytes` (with `-dumphud -dumpsprite N`) prints a sprite's action bytes;
  `-logradiovol` logs the volume clip's display list.
- `source/game/cinematic/cinematic_car.h` — `hasHardtop()`, `hasWindows()`.
- `source/audio/radio_player.cpp` — the "N of 65" log printed garbage (int passed to `%zu`).

### Verified (`-gm FREERIDE -skipcine -no3d -nopad -fixedtimestep`)
    -autoplay "200:press=accleft;260:shot=a1;300:press=accright;304:shot=a2;320:shot=a3;400:shot=a4;
               450:press=accright;500:shot=b1;520:press=accup;530:press=accup;560:shot=b2;
               580:press=accright;620:shot=b3;640:press=accdown;660:press=accleft;720:shot=b4;
               760:press=accleft;800:press=accup;840:shot=c1" -exitframe 860
- `docs/images/incar_accessories_radio_dpad.webp` (a1..c1): the wheel with all four entries
  (roof up, indicators left/right, windows down, no locks on the California); RIGHT in the
  wheel shows "Right indicator" and closes — **no radio**; RIGHT with nothing open opens the
  panel on song 1 (`Radio: playing A Astronauts - Classic (… 1 of 65 …)`, no skip); RIGHT in
  the panel -> Kalua FM; LEFT closes; wheel UP -> "Roof".
- Volume: `-logradiovol` shows 0.80 -> 0.40 -> 0.00 and the clip frame / fill scale
  following (`depth 4 char 773 clip 6 sx 0.383` at 0.4); the panel auto-hides ~3 s after the
  last press.

### Not done / next (task 1)
- **The volume bar does not visibly change** although the clip frame does: depth 4 of
  `/HUD/radio/volume` is a **mask** (`clipDepth 6`) over depth 5 (the bright bars, alpha 0.8;
  depth 3 = the dim bars, alpha 0.4), and hiding depth 5 removes the bright bars **entirely**
  even at volume 0 -> the stencil mask does not clip in this HUD draw. Start at
  `FlashPlayer::drawClip` (`source/flash/flash_player.cpp` ~2270, the SM_Increment /
  SM_TestEqual path) and check that the 2D pass has a stencil attachment / the pipeline
  enables the stencil test (`render_device.cpp` ~1941). Same for `-no3d` and with 3D.
- Indicators: no light driver (the wheel entry applies and closes, logs a warning).
- The pad-only gating of RADIO_* is `[INF]` (what sets 0x00fd8fef: FUN_00d65d10, a callback).

### 2. The Ibiza tunnel filled with dirt — reproduced, cause found, fix candidate, NOT on by default

**Reproduced** at the owner's F6 mark #1 (`_marks.txt` 19:58:44), the north mouth of the
tunnel in sector 3-3-3-4 (node `TUNNEL` / its `…W_T` child: x 21159..21244, y 22..35,
z 22112..22244 — `-logtunnels` lists every tunnel node of the loaded sectors):

    build\bin\opentdu2.exe -game ... -gm FREERIDE -skipcine -nopad -fixedtimestep ^
        -tp 21229.31,30.03,22262.84 -heading 147.9 -autoplay "150:shot=_t1.ppm" -exitframe 160

### Ruled out / narrowed (`docs/images/ibiza_tunnel_terrain_ab.webp`)
- `-noareas` -> identical (dark wall) -> not the area / island layers.
- `-noterrain` -> **the tunnel opens** (lit interior, lamps, pillar) -> it is the **sector**
  heightfield.
- Hole encoding in the data: none. Heights over the tunnel are 33-35 m (the road enters at
  ~30, the roof ~35), so the heightfield runs **inside** the tunnel volume, and at the mouth it
  drops 33 -> 25 m within one 3 m cell (z 22239 -> 22248): that slope is the "dirt" across the
  opening. The 8-byte stream 1 is half-float normal data (byte 3 = 0x3b/0x3c, nY ~ 1), no
  flag; no NaN / sentinel heights (`-dumpheights`).
- Alpha: forcing an alpha test on the opaque terrain cuts nothing at the mouth -> not a hole
  painted in alpha.
- `-terrainblend` (the retail 2DM state: blend 5/6, no depth write, but deferred with the
  blended draws) -> the tunnel shows only faintly through.
- **`-terrainblend -terrainearly`** (blended, no depth write, drawn in submission order
  **before** the sector objects — the retail TD_HeightMap handler FUN_00d716f0 returns
  **bucket 5**, ahead of the opaque buckets) -> **the tunnel renders like `-noterrain`**, and
  the terrain spikes that poke through the retaining walls at this junction (the "dirt
  mounds") vanish too. Other spots compared (BIENVENIDOS street, a wooded hill): mean pixel
  difference < 1.

So the cause is the port's **opaque, depth-writing sector terrain** (the session-era fix for
the floating area sheets): the original draws the heightfield without depth write before
the objects, so object geometry (tunnels, walls) paints over terrain that crosses it.

### Not done / next (task 2)
- **Why it is not the default yet**: with no depth write and no sort, a farther sector's
  terrain drawn after a nearer one can paint over it (the old "floating sheets" class), and
  objects behind a terrain crest are no longer hidden by it. Neither showed in the four
  spots compared, but no wide hill panorama was tested. Next step: decode the retail draw
  order for bucket 5 (the layer table at 0x00f7f604 / FUN_00d70cf0, quoted in
  `eden_renderer.cpp` for the water buckets) — whether the terrain is in the ZPREPASS layer
  and how the sectors are sorted — then make `-terrainearly` the default for sector terrain
  (sorted front-to-back or with a terrain-only depth pass), keeping the area layer opaque.
  Code: `EdenWorld::draw` (`source/world/eden_world.cpp`, the `bTerrainEarly` comment) and
  `loadHeightfield` (the `-terrainblend` block).
- Mark #2 (35976, 6.98, 5804, 01:33:17) is the BIENVENIDOS street, not a tunnel.

## Session 2026-10-29 (Cline, `cline`): the third workspace, plus the tooling that keeps it from going stale

Setup work, not gameplay. The owner asked for a third agent directory:
`C:\OpenTDU2-cline` is a `git clone --local` of the hub on branch `cline`, with its
own `build-cline\` and `-userdata C:\temp\otdu2-cline-userdata`, remotes `win`
(hub), `glm` and `upstream` (read only). GLM objected — a third tree costs a clone,
a build dir and one more thing that goes stale — so the answer is the two tools
below rather than discipline.

### What changed
- `sync-workspace.ps1` (works from any of the three trees): the report prints every
  workspace's branch, HEAD, dirty state and how far ahead/behind the hub it is;
  `-Merge` fetches the hub, merges `win/win` into the current branch, then says
  whether `source/`, `CMakeLists.txt` or `cmake/` changed (i.e. whether a rebuild is
  needed). Refuses a dirty tree or its own running game, and never merges *into* the
  hub. Read-only calls use `--no-optional-locks`, so a report is safe while another
  agent is committing.
- `seed-deps-cache.ps1` + `C:\otdu2-deps`: `cmake/Dependencies.cmake` clones 9 repos
  (SDL, Jolt, imgui, volk, VMA, Vulkan-Headers, SPIRV-Headers, stb, earcut) for every
  new build dir; `build-cline.ps1` and `build-glm.ps1` now pass
  `-DOTDU_DEPS_DIR=C:/otdu2-deps` when the cache exists, so a fresh configure makes
  **no network access at all**.
- `CLINE.md` (new: that workspace's branch/build/run/sync commands), `WORKSPACES.md`
  (three-workspace tables, the sync tool, the cache, the merge rhythm), one updated
  row in `CLAUDE.md`.

### Verified
- `build-cline` wiped and rebuilt from scratch through `build-cline.ps1` with the
  cache: configure 8 s, `build exit 0`, no `Cloning into` lines in the log; headless
  LOGIN run (`-exitframe 90`) with 0 errors/warnings.
- `sync-workspace.ps1`: report, `-Merge` ("already up to date"), the dirty-tree
  refusal, and the hub's copy run from another tree (bootstrap path) all checked.

### State
- Hub and `cline` are at this commit. `glm` was live during the session (uncommitted
  `gm_freeride.cpp` work) and was deliberately left alone — it picks the tooling up
  on its next `sync-workspace.ps1 -Merge`, or by running the hub's copy:
  `cd C:\OpenTDU2-glm; powershell -File C:\OpenTDU2\sync-workspace.ps1 -Merge`.
- The cline tree still carries a local branch `win` at `6d1a4ce`, a leftover from the
  clone. Never check it out there — `win` lives in the hub.

## Session 2026-10-29 (forty-first, `glm`): the flicker has a prime suspect — animated cloth casters sweep the shadow map

Continued session 40's hunt in this workspace (win WIP committed as `c7d79fb` first,
then merged both ways; both trees at `6d1a4ce` + this). Owner: *"track this bug down"*.

### What changed
- `source/render/eden/eden_scene_view.cpp` — `-shadowprobe x,y`: per-frame texel readback
  at one pixel of the encoded gViewPos depth (B/A decode as in shadowslice.frag) against
  the raw D32 depth, logged as `ShadowProbe: f <frame> raw <depth> viewZ <m>`; per frame-slot
  readback buffers consumed `kFramesInFlight` frames later (EdenSun pattern). The logged
  frame number is the frame the data belongs to, so it lines up with `-autoplay` shots.

### Verified (FREERIDE spawn, session 40's wiggle, `-width 1280 -height 720`)
- The flicker metric refined (state-transition count of `|with - without|` per 40x40 cell,
  threshold 25): **115 flickering cells**, hottest on the right building wall (920,360)
  and the top-right corner — matches session 40's map.
- **The gViewPos encode quantizes exactly as predicted** (probe at (940,380), wall ~16 m):
  decoded viewZ pins to discrete codes `16.2980 / 16.3059 / 16.3137` (steps = viewZ/2040,
  i.e. 1 ulp of the ~11-bit mantissa: 0.8 cm at 16 m, 1.5 cm at 30 m, 3 cm at 60 m) while
  the raw D32 moves smoothly underneath. Real, but **too small to be the primary cause**:
  the margin swing is ~4 cm, the encode step at the probe distances is 0.5-0.8 cm.
- **The margin map decomposition is the decisive result** (`-ownshadowpass 1 -shadowmargin 20
  -showshadowbuffer -shadowslice 0`, per-frame shots): at a fixed camera the cobblestone
  ground cell margin is **constant (82/255 for all 40 frames)** — sun, light matrices and the
  receiver path are stable — while the **banner cell oscillates frame-by-frame between ~+1.5 cm
  and ~-2.5 cm** (`53,1,1,0,36,55,11,...`), the same ~4 cm swing as session 40's house floor.
- **The banner moves**: diffing consecutive shots at the banner region shows ~19,000 pixels
  changing by >30 per frame pair in the margin map (and ~9,700 in the scene shots) while the
  ground region is 0.0-1.1 — it is **waving cloth**, animated at a fixed camera.

### The mechanism that unifies the flip sites [INF: strongly supported, not yet frozen-proven]
The banner is its own nearest caster along the light rays feeding the surfaces behind/under
it. As the cloth waves, its texels in the shadow map (MIN blend) sweep back and forth, so a
whole flat region's stored depth jumps between "nearest caster = cloth" and "nearest caster
= the surface itself" — the region flips lit/shadowed at once, at a fixed camera, biased or
not. This explains the FREERIDE wall flips (the banner's cast shadow sweeps them) and
probably the house terrace (the party decorations are cloth too). The car-shadow-edge cells
may be a second, smaller instance (bias band vs sloped cast shadow), and the encode
quantization a third contributor at larger distances.

### Not done / next (start here)
1. **Freeze-prove it**: hide the banner's caster contribution and the flips must vanish.
   Cheapest: a temporary flag to skip animated/cloth models in `drawObjects( ePass::ShadowCasters )`
   (or hide that one instance — session 39's hide-sphere plumbing), then re-run the wiggle
   (transitions should drop from 115 cells to ~0 in the banner-fed region). Find how the
   banner animates first (`.pmi` instance / vertex-shader time constant: does the caster pass
   bind a different animation time than the depth prepass? — that would ALSO explain a lagging
   map even for non-cloth animated casters).
2. If confirmed, the fix direction is retail's: session 40's `[BANKS]`/stencil path draws
   caster geometry with the SAME animation time as the scene — check the original
   FSShadowMatStencil design (session 40 note) and whether the original restricts the slice
   to the receiving geometry (the stencil) — i.e. self-shadowing only from co-present casters.
   Alternative pragmatic fix: clamp the map-vs-receiver swing for animated casters (retail's
   `SlicePhaseScale`/`SliceUvOffset` vectors are zeroed [INF] — they may be exactly this).
3. The house-walk floor patch and the character-select avatar flips: re-test after 1-2.
4. Session 40's step 1 (shader-reconstructed world position vs CPU unprojection) is
   **superseded**: the margin at a fixed camera is stable on static surfaces, so the
   reconstruction is right; the error is in the map content at animated casters.

## Session 2026-10-29 (fortieth, `win`): the sun-shadow flicker — hunted, narrowed, NOT fixed (full test log)

Owner: *"camera has tons of oddities ... every time it's always been when camera moves"*; *"this flickering isn't here it's in a lot of places ... I think it may be the biggest bug we have"*. Seen in the house walk (terrace floor, walls), at the FREERIDE spawn with only the car (the building walls and the BIENVENIDOS banner, "I had to wiggle the camera a bit"), and on the character-select avatar once real-time shadows switch on after the flash. **Whole flat sunlit surfaces flip between lit and shadowed at once when the camera moves slightly.**

**Reproduced** (deterministic, `-fixedtimestep`; the measure is the mean of a fixed screen patch, or 40x40 px cells of `shadowed / -noshadows` luminance flipping by > 0.25 between frames):
    :: house walk, floor patch [600:700, 500:700], dark at frames 13-14 and 22-23 of 24 (one every 3 frames from 150)
    build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -nopad -intro -walkmode 1 -fixedtimestep -nopnj -cineweather 12 -autoplay "140:look=0.04,0;150:shot=...;153:shot=..." -exitframe 225
    :: FREERIDE spawn (owner's F6 mark: camera (35976.1, 7.0, 5804.5), heading 77), wiggle: look +-0.12 every 6 frames from 200, a shot per frame 200..239
    build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -nopad -gm freeride -skipcine -storytime 86400 -fixedtimestep -autoplay "200:look=0.12,0;206:look=-0.12,0;...;236:look=0,0;200:shot=...;..." -exitframe 242
FREERIDE: 341 flipping cells over 39 steps, bursts of 52-58 cells (the right building's whole sunlit wall, the left background walls, the banner, the car shadow edges). A plain slow orbit around the car shows almost none — it needs the small back-and-forth.

### What changed (kept: correct, verified harmless, and part of the way forward)
- `source/core/math.h` — `Matrix4x4f::mulPrecise` (product summed in double), `Matrix4x4f::inverse()` computes its cofactors in double, new `Matrix4x4d` (same layout and product convention; `FromFloat`, `toFloat`, `RigidInverse`). Island coordinates are 36 km at the house: float spacing ~4 mm, and the float view x world / light x view products cancelled to 2-4 cm (the size of the shadow bias).
- `source/render/eden/eden_renderer.{h,cpp}` — `viewProjection`, world-view-projection, world-view (and its inverses) are formed with `mulPrecise`; `setPreciseViewProjection( const Matrix4x4d * )`: while set (the shadow caster pass) the WVP constant is computed in double from the light matrix.
- `source/render/eden/eden_shadows.{h,cpp}` — each slice keeps `LightViewProjectionD`, built in double with the texel snap applied exactly (`x = (s.p - snapped) / r`, so a world point's texel phase cannot change between frames); the casters use it, the resolve composes it with `Matrix4x4d::RigidInverse( scene.View )`.
- Effect, measured: the first band of floor flips in the house walk is gone (bias-300 pan: 10 dark frames of 24 -> 4); FREERIDE frame time 17 ms, intro party 33 ms, no visual regression (FREERIDE frame at 400 vs the previous build: mean 3.5 levels, shadow edges only).

### New debug tools (kept)
- `-ownshadowpass <0|1>`: the RT SHADOW 2D slice passes run OpenTDU2's GLSL twin (`resources/shaders/shadowslice.frag`, same inputs and law as edrender_pfx_shadow ps_36 / ps_18 / ps_12) instead of the translated originals; 0 = the original's PCF taps (weights `fract(uv N)`), 1 = texel-centre bilinear. **Mode 0 is bit-for-bit the original's result** (same 341 flips frame for frame).
- `-shadowmargin <k>` (with `-ownshadowpass` and `-showshadowbuffer -shadowslice N`): the slice pass paints `(stored map depth - receiver depth) x k` per metre (0.5 grey = equal): the per-pixel picture of the comparison.
- `-cineweather <preset>` / `-listweathers` (intro and walk): one weather for every shot; falls back to the island's `game_weathers.xmb` ("12", "01.30" ... night presets). `-listweathers` logs each preset's sun direction, diffuse and `sun_shadow_opacity`.
- `-vkdevice <name substring>`: force the Vulkan device (logs every device found). Tested with Mesa lavapipe: `set VK_DRIVER_FILES=C:\temp\mesa-lavapipe\x\x64\lvp_icd.x86_64.json` + `-vkdevice llvmpipe` (mesa3d-26.2.4-release-msvc.7z from pal1000/mesa-dist-win; seconds per frame, the window reports "not responding" — normal).
- `RenderDevice::copyRenderTargetColor` (the colour twin of `copyRenderTargetDepth`) — read back any 4-byte texel of a render target (used on the R32F slice maps and the RGBA8 gViewPos).

### Established (test -> result)
| Question | Test | Result |
|---|---|---|
| Shadow system or z-fighting? | same pan with `-noshadows` | **shadow system**: without shadows the floor is steady |
| Time / weather drives it? | camera still, time running 72 frames | floor 61 and view depth 3.0304 m in every frame: **only camera moves** |
| Sun angle? | `-cineweather` Avatar / 12 / 18 / 06 / 01.30 / SS_Arche | frames 13-14, 22-23 dark at noon, 18, Avatar, SS_Arche; 06 floor in real shadow throughout; 01.30 (moon from the other side) 0 flips |
| GPU driver? | Mesa lavapipe on the CPU | same dark floor: **not NVIDIA** |
| DXVK translation of the slice shader? | `-ownshadowpass 0` | identical flips: **not the translation** |
| PCF tap convention? | `-ownshadowpass 1` | 944 flips vs 341: worse — the result depends on texel phase, but fixing the phase (double light matrix, exact snap) did not change the flips |
| Precision of matrices at 36 km | `mulPrecise`, double `inverse`, double light matrix | removes part of the flips (see above); the 13-14 / 22-23 frames stay |
| Bias | `-shadowbias` 300 / 500 / 800 / 5000 | 800 clears all but 13-14 / 22-23; 5000 clears those too (so the bad frames are a depth comparison ~5-30 cm off), but shadows detach — not a fix |
| Snap / fit | `-shadowfit` (tight per frame) | same flips |
| My culling | `-noobjcull` | same flips (<= 212 px) |
| Pre-pass materials | temporary `-nozpassmat` (main materials in the Z pre-pass) | same |
| Vertex shader mismatch caster vs camera pass | temporary `-tmpzcaster`: Z pre-pass drawn with the TD_ShadowCaster VS | **identical** sequences: both passes place the floor the same way |
| Pipeline / vertex layout mix-up | read: the pipeline key includes `mesh.LayoutKey` (streams, strides, formats, offsets) | ruled out |
| Avatars / player / shadow proxies | `-nopnj`; temporary `-tmpnoplayer`; temporary `-tmpnoproxycast` (TD_ShadowCaster_Only materials) | same (and no proxy material is drawn in the walk) |
| World layers | `-noterrain -noareas -noobjects -noinstances -noexteriors` | same; the floor is set 2 (house exterior, node `EPH_L3DI`, material `beton`, TD_Building vs12/ps42, 90 triangles, local y 31.02..31.41 m) |
| Other per-frame systems | `-staticcubemap -noprojectors -noocean -nosunflare -nowet -nophotoshop` | same |
| Sync / barriers | validation layer with `VK_KHRONOS_VALIDATION_VALIDATE_SYNC` | no hazard reported |
| fp16 (`_pp`) | SPIR-V of the caster and slice shaders | no `RelaxedPrecision` |
| Encoded view depth (gViewPos, 11-bit mantissa) | raw D32 depth vs decoded B/A at one pixel | identical (3.0231 / 3.0235 m): the encoding is not it |
| Camera jitter | camera position / quaternion logged per frame | position fixed to 0.1 mm, pure smooth yaw |
| **Shadow map stable?** | D32 and R32F readback at a **fixed world point** vs its analytic light depth, every frame | **stable: -1.0 +- 0.2 cm in all frames, bad ones included** |
| Margin map | `-ownshadowpass 0 -shadowmargin 5` frames 12 vs 13 | floor patch margin +1.4 cm (lit) -> **-2.6 cm (dark)**, uniformly over the patch and the walls |

**So:** the light matrices and the shadow map are right and stable; in the bad poses, the receiver depth reconstructed from the camera's depth buffer comes out ~4 cm behind the map over whole surfaces. The camera is steady, the encoding is exact, both vertex shaders agree. At the probe pixel the decoded floor point jumps between heights (y 37.028 / 36.992 / 37.055) with plateaus of 6-12 frames during a smooth yaw — partly real floor relief (the slab varies 39 cm and the probe sat near an edge, so single-pixel numbers were misleading), but the margin map shows the shift is region-wide.

### Not done / next (start here)
1. **Compare the two reconstructions of the same pixel**: the resolve rebuilds the view position from gViewPos and `-1/P00, -1/P11` with `ndc = (u*2-1, 1-v*2)`; the screen texcoord of the fullscreen pass vs the viewport flip (D3D y-up, Vulkan viewport flipped) is the one geometric input never cross-checked numerically. Test: with `-ownshadowpass`, output the reconstructed **world** position (inverse view applied in the shader, double inverse uploaded) for a pixel and compare with the CPU unprojection of the same pixel from the raw D32 depth (both readbacks exist: `copyRenderTargetDepth` on `pSceneTarget`, `copyRenderTargetColor` on the slice screen).
2. If they agree, write the margin for **all four PCF taps** (`-shadowmargin` shows only the nearest) — a uniform ~4 cm shift could also be the taps landing on a neighbour texel row of a steep surface (the map texel 0.9 cm, the depth slope of the wall/floor seen from the sun).
3. Character select: the first avatar has no shadow before the flash (shadow config / weather switch) and flickers after — re-test there once 1-2 are understood (`-shadowconfig`, `-cineweather Avatar`).
4. Feet sinking / disappearing: the dancers stand at authored heights while the visible floor slab rises 39 cm across the terrace — compare each PNJ root height with the floor under it (not measured yet).
5. Garage car pan (owner screenshot: wheel-arch / suspension parts not rendered as the camera pans) — not looked at.
6. `C:\temp\mesa-lavapipe` (lavapipe) and `build\bin\_opentdu2_running.exe` / `_opentdu2_old.exe` can be deleted.

## Session 2026-10-29 (thirty-ninth, `win`): CPU frame time halved (culling, constants), opening-scene shadows / DJ decks fixed

Owner: *"try fix performance issues and help improve the physics"*, then *"leave physics for now ... fix the opening scene"*: (1) hair clips through some faces, (2) DJ decks have an odd black texture instead of a shadow, (3) pool: slight camera movements cause odd shadow behaviour, (4) no shadows from the characters until the character select screen, (5) small camera moves -> shadow oddity (a "major shadow like flickering issue").

**Repro positions** (new: `-walkat x,y,z,yaw` is scene-local, `-logprops` prints every prop's scene-local position):
    build\bin\opentdu2.exe -game ... -intro -walkmode 1 -walkat "4.6,32.9,-8.4,215" -fixedtimestep ...   (DJ decks)
    build\bin\opentdu2.exe -game ... -intro -walkmode 1 -walkat "5,32.7,0,270" -fixedtimestep ...        (pool)
    build\bin\opentdu2.exe -game ... -intro -walkmode 1 -walkat "3.6,32.7,1.0,250" -fixedtimestep ...    (avatar close-up)
Add `-showshadowbuffer` (screen sun shadow) or `-showslicemap N` (light-space depth of slice N).

### What changed
- `source/render/eden/eden_renderer.{h,cpp}`, `eden_model.cpp` — **frustum culling of EdenModel items per pass** (`isBoxVisible`: bounding sphere vs the side + far planes of the current `viewProjection`; no near plane, so casters behind a light slice still cast; skinned items get a 2 m margin around the bind-pose bounds). Before, the sets, props and all 31 party avatars were recorded in *every* pass (ocean reflection, Z pre-pass, 3 shadow slices, planar reflection, opaque). `-noobjcull` turns it off.
- `eden_renderer.cpp` `fillConstants` + `dxso_translator.h` — each shader constant member is **classified once** (`ShaderConstantMember::Kind / AvatarKind`, enums in eden_renderer.cpp) and the draw loop switches on it, instead of ~40 `std::string` compares per member and, for avatar draws, `ToUpper( StripSuffix( name ) )` (3 allocations) per unmatched member.
- `eden_scene_view.cpp` — `-logscenetimes`: CPU ms of each offscreen step (env, ocean, prepass, shadows, planar, opaque, blended, post) every 120 frames.
- `intro_cinematic.cpp` — **DJ decks**: cf_plat.2DM authors its `patch` contact-shadow decal (`patch_occlu_01`, SRCALPHA/INVSRCALPHA) in **TD_Skin2** (`d5013ed0dbcdcdcd`), a family whose 4 VS are all skinned and whose PS lights skin [DATA, `otdu2_sob_tool`]: on the static quad it painted solid black rectangles. A skin-family `patch` now takes the TD_Building `patch` material of the bank that samples the same texture (`TD_Building vs2/ps2`, bucket 0x21, like every other prop's patch) [INF].
- `eden_renderer.cpp` `draw` (caster branch) — **water does not cast** (buckets 0x17–0x19, 0x2e): the pool water was a caster and shadowed itself — solid black up to the first split (7 m with DEFAULT), acne beyond, and the split line swept across the pool with every camera move = owner issues 3 / 5 [INF: the original's caster layers are not decoded].
- `eden_renderer.cpp` `getCasterMaterial` — **skinned (avatar) casters render their back faces** (cull 2 <-> 4): the self-shadow bands over faces / necks / shoulders are gone. Open geometry keeps its faces (flipping everything lost the house-wall and deck shadows — tried, see Ruled out). `-casterfrontfaces` restores.
- `eden_shadows.cpp` — the slice "texel snap" was computed in a view centred on the slice centre itself (always 0, i.e. no snap): now snapped in a light space that only rotates with the sun, depth range from the stable sphere. Small effect at these texel sizes (0.9 cm), but correct.
- Debug flags: `-shadowconfig <name>` (one shadow config for every intro shot), `-showslicemap N`, `-logshadows` now also prints the config / splits / caster draws per slice / the weather `sun_shadow_opacity`; `-car <_car_file_name>` (FREERIDE / `-vehtest` any DB car; rims stay the California's).

### Verified
- Intro party (frames 1200 / 2000 / 2800, `-fixedtimestep`): culling and the constant classification are **pixel identical** (0 px over the noise floor). Intro frame time **55–80 ms -> 8–37 ms** (`[perf]`); `fillConstants` **3.0 -> 0.67 µs/draw** (temporary timers, removed). FREERIDE driving **~14.7 ms/frame** (18–20 with `-noobjcull`).
- DJ decks: `docs/images/intro-dj-decks-before.webp` / `-after.png` — black rectangles gone.
- Pool: `docs/images/intro-pool-shadowbuffer-before.webp` / `-after.png` (left beauty, right `-showshadowbuffer`): the water is lit, the girl / ring shadow lands on it; the water is also brighter turquoise (it was self-shadowed), like the owner's second pool screenshot.
- Avatar acne: `docs/images/intro-avatar-acne-before-after.webp` (shoulder / neck banding gone).
- FREERIDE regression: shadowed fraction 26.3 % -> 25.7 %, beauty frame differs only at shadow edges.

### Ruled out (with the test)
- Issue 4 is **data, not a bug**: every intro cinematic shot sets weather `Intro`, whose `sun_shadow_opacity` is **0** (`-logshadows`: 5600 frames 'Intro' 0.000; walk / character select use 'Avatar' 1.000). FUN_00978790 uses it as `Shadow_SlicesDist.w` when mgr+0x431 is set [CODE, rain_shadow_spec B.6]. A capstone sweep of the readable .text for byte writes to +0x431: 0x8c8cc0 (=0, but in the fixed-centre mode that also zeroes +0x470 -> still no shadow), 0x8c8cee / 0x8cbab4 / ctor 0x977935 (=1). So the readable code gives no real-time sun shadows in the intro cinematics. Not overridden. (On the way: `-shadowconfig DEFAULT` -> still none; casters are drawn, 400–900 per slice; `-tmpnear` / quaternion length / set bisect were dead ends — the slice maps were fine, the opacity scaled them away.)
- My culling is not behind the pool / shadow oddity: pool frames with and without `-noobjcull` over 5 small camera moves differ by <= 212 px.
- Raising `-shadowbias` 300 -> 1200: speckle 0.58 -> 0.31 % but shadows shrink (peter-panning) — not the lever.
- Back-face casting for *all* materials: the house wall and the deck in front of the pool lose their shadows (open geometry).

### Not done / next
- **Issue 1, hair through faces: not reproduced yet.** Hair is drawn as blended AVATAR_FULLALPHA layers (TD_Hair buckets 0x25–0x28, blend 5/6) over the opaque face (0xc); the opaque hair layer is bucket 0xd. Candidates: low-alpha hair texels without the original's alpha test, the facial animation (`-nofacefx` A/B) moving the forehead through the hair cap, or the hair's own shadow on the forehead. Needs a frontal face close-up: owner, press **F6** next to an affected face (writes `_marks.txt`; world position — convert with the scene origin or add a world-space `-walkat`).
- Remaining acne speckle on the decking / stone (0.4 % of pixels) — a slope-scaled or normal-offset bias in the resolve would need the receiver normal (not in gViewPos).
- Perf next: per draw ~1.8 µs remains (descriptor alloc+update 0.45, binds 0.6); the ocean reflection pass is the largest single pass in the wide intro shots (7.5 ms).
- Physics was parked by the owner (the mod-pack data may give the per-car suspension values). `-car` is in for the `-vehtest` sweep across cars.
- `build\bin\_opentdu2_running.exe` / `_opentdu2_old.exe`: the owner's running game was renamed so the linker could write a new exe; delete them once that game is closed.

## Session 2026-10-28 (twenty-fifth, `win`): the profile is saved and loaded (`<userdata>/profile.sav`)

Owner: saving does not work / is not visible. Retail auto-saves on events (pause, shops, dealerships, map, houses), with a save icon, and has no manual save.

### What changed
- `source/game/player_profile.{h,cpp}` - `PlayerProfile` gains Money, NbCars, NbHouses, Points[4] (Racing, Cruising, Collection, Social). `LoadProfile()` / `SaveProfile( reason )` read and write plain `key = value` lines (`profile.sav`, written to `.tmp` then renamed). A profile with no name is never written, so `-gm` test runs cannot overwrite a save.
- Loaded at startup (`tdu_instance.cpp`, after `LoadGameOptions`).
- Saves on: pause open and close, map close, story step 1 (garage chosen) and step 3 (`gm_avatar_choice.cpp`, `gm_freeride.cpp`). `[INF]` - the retail trigger list is not decoded; shops, dealerships and houses do not exist in the port yet and must call `SaveProfile` when they do.
- `gm_freeride.cpp` `enter()` now only **raises** the story step (it forced 2, which would undo a loaded save).
- Pause panel and Game Progress pages read money / cars / houses / points from the profile. Levels stay 0: the level thresholds are not decoded `[INF]`.

### Verified
- Seeded `C:\temp\otdu2-save-test\profile.sav` (name, step 3, 31410, 3 cars, 2 houses), paused: log "Profile: loaded ... story step 3", "saved ... (pause)", file rewritten with step still 3. Screenshot `docs/images/port-progress-saved-s18.webp` shows the name, $, houses and cars from the file.

### Not done / next
- **The save icon** is shown (session 25 follow-up): GENERAL.vmf `/saving_logo` (labels open / close, `displaySavingLogo` `[DATA]`) is opened by `FreeRideHud::tick` while `GetSaveNoticeTime()` > 0 (2 s of real time after each save) and drawn with `/pop_goal`; clip path and position are `[INF]`. Evidence `docs/images/port-saving-logo-s18.webp` (map close while driving: spinner, then the logo, top right under the zone badge). Known gap: the pause menu does not tick the HUD, so a save made by opening the pause shows no icon until it closes.
- Explored roads, car ownership and options are not part of the file (options stay in `options.ini`).
- A new game does not ask before overwriting an existing save.

## Session 2026-10-28 (thirty-eighth, `win`): wrecks as Discovery events; the car dealership (native menu)

Owner: "both please" (wrecks / panoramas as Discovery events; the used car dealer).

### Wrecks `[DATA]`
- `source/game/freeride/discovery_points.{h,cpp}` (new): `islands/<island>/scenarics/wrecks/wrecks.bnk` -> `entities.xmb`: `ExportType_Entity.ArrayOf_Entity__Wreck[]` with `orient_pos_scale.pos` (X Y Z), `ID` (the class 1 / 2 / 3), `__Name` (`WreckB_05`...): **28 wrecks on Ibiza** (`-logdiscovery` lists them); `scenarics/wrecks/wrecks_config.cpr` (XML): `max_detector_range` 250 m, bips 1.35 s (far) to 0.20 s (near), `min_activation_distance` 10 m.
- `GMFreeRide`: HUD "Detector n m" bar under the minimap for the nearest unfound wreck within 250 m; within 10 m it is found: `AwardSkillEvent( "CRUISING_WRECK_PART_<ISLAND>_AREA<n>" )` (area from the cruising-zone polygon; 25 points on Ibiza, `times 10` per area), saved as `wrecks_<island> = index,...` and `done_<EVENT>`. Verified at `-tp 20533,6874` (WreckB_05): "found (1 of 28)", +25. The detector needs no unlock here (retail: the Racing level 3 "Metal detector" reward) `[INF]`; no bip sound.
- **Panoramas not done**: `photos.bnk` is a billboard model (20 photo textures, 2 scene nodes), not a list of viewpoints; the viewpoint positions are not in the files found (not in `spots.bin` / DB). `CRUISING_PHOTOS_<ISLAND>` stays unwired.

### Car dealership
- `source/game/car_shop.{h,cpp}` (new): `DBCarShop` (60 shops, `_libelle_hc` name, `_slot_1..9` = **`_id_car`** of the cars, not hashes, `_isbuyable_n`) joined with `DBCarPhysicsData` (`_price`, `_model_name` + `_version_name`, `_manufacturer_name`; names carry `##` markers). The story's **Used Car Dealer** (shops 24 / 25) sells exactly the owner's three cars: Mustang Fastback '68 $23,000, Esprit S3 $15,000, Delta Integrale Evo 2 $20,000. Ferrari $190,000 (California) - $675,000 (Enzo); Audi 7 cars; `-logshops` lists all.
- `source/game/freeride/dealer_menu.{h,cpp}` (new, native 2D menu): stopped (< 20 km/h) within 40 m of a `cardealer` map spot the HUD prompts "A / Enter: enter the dealership"; up / down select, A buys (checks owned / not for sale / money), B leaves. A purchase: `Money -= Price`, `OwnedCars` (saved `cars_owned = id,...`), `NbCars++`, `COLLECTION_CARS_DIF_<n>` events for 1 / 3 / 6 / 10 / 15 / 20 / 25 / 30 / 40 / 50 cars, saved at once. The building name picks the shop (`FindShopForSpot`: AUDI -> Audi, FERR -> Ferrari, MERC, ITAL -> Italian Cars, ECDC_* -> classics, EUR / US / ASIA / BRI) `[INF]`. `-dealer <shop index>` opens one directly.
- Verified: used car dealer: Esprit S3 bought, money 31,410 -> 16,410, "4 cars", `COLLECTION_CARS_DIF_1` +75 and `_3` +175 (profile cars started at 3), Collection 250; "You already own this car", "Not enough money" for the $20,000 car; Ferrari list (`docs/images/port-dealer-menu-s18.webp`); real entry next to ECD_AUDI at `-tp 11665,17437` (`port-dealer-prompt-s18.png`).
- Also: risk driving ignores the first 3 s after a spawn (the drop paid air time).

### Not done / next
- The retail shop (`shopscar.vmf`, 1.3 MB, ~100 FSCommands: showroom 3D car, colours, rims, test drive, DLC) is not driven; no 3D preview, no colour / rim choice, no price discount from the Collection reward ("2% discount"), no selling or garage list; the bought car cannot be driven (the port has one car) and `NbCars` is the only count.
- The story gating (the first car after the Solar Crown drive, `TUTO_BUY_A_CAR`) is not wired; the dealer is open from the start.

## Session 2026-10-28 (thirty-seventh, `win`): popup A flow (reward + global title pages), DBRewardXP events, road milestones

Owner: "both please" (the A-button pages of the level-up popup; Racing / Collection / Social earning points).

### Popup
- A button (`validate`, or auto every 3.5 s): Level UP panel -> all gauges -> new title page (`WhatReward <grade>_<tier>`, `NewTitle`, `reward_name`, `db_reward_desc`, header `TitleDbProgress`) -> global title page (gold, the ID card, `PlayerTitle`). Bug found: `NewAreaTitle` must be set to 0, undefined takes the wrong branch. Popups are queued (`showEarn`, `showLevelUp`). Details in `docs/re/skills_spec.md`. Evidence `docs/images/port-levelup-title-page-s18.webp`, `port-levelup-global-title-s18.png`, `port-earn-popup-s18.png`.

### Points: `DBRewardXP` (191 events) `[DATA]`
- `AwardSkillEvent( name )` (`skill_levels.cpp`): points per skill from the table, `_nbtimesallowed` counted in `PlayerProfile::EventsDone` (`done_<EVENT> = n` in `profile.sav`), earn popup ("Roads dicovered +40 Pts" with the gauges). `-awardevent <NAME>` pays one.
- **Roads are milestones per area**, not 1 point per 50 m (that guess is gone): `MapScreen` builds the area of every link from the cruising-zone polygons (Ibiza area 1 = 355 km, area 2 = 552 km) and pays `CRUISING_<ISLAND>_ROADS_AREA<n>_<10|25|50|75|100>` = 40 / 50 / 90 / 120 / 150. Milestones already reached when the save loads are not paid again.
- Verified: `-awardevent CRUISING_IBIZA_ROADS_AREA1_10` -> "+40 points (now 40)", popup; all links explored except 3 near the spawn, driven over -> "CRUISING_IBIZA_ROADS_AREA2_100 +150 points".

### Not done / next
- Racing, Collection and Social points: the events exist in the table but nothing in the port triggers them (races, championships, licences, shops, car / house purchase, clothes, club, chase, co-op). Hook `AwardSkillEvent( "COLLECTION_CARS_DIF_1" )` etc. where those features land. Wrecks, panoramas, events (Discovery) likewise.
- Hawai has 3 usable area polygons (the 4th block of `cruisingzones.bin` is missing) while the table has areas 1-4; `NewAreaTitle` / area levels (`DBAreaLevel`) not driven.

## Session 2026-10-28 (thirty-sixth, `win`): the retail level-up popup is driven; near misses verified on live traffic

Owner: "both please" (decode the retail db_progress popup; test a real near miss and tune).

### Retail level-up popup `[DATA]`
- Found with `-logdbprogress` (new; dumps the GENERAL clips and root functions). The popup is **`_root.dbProgress( action )`** (2364 bytes): the game sets the root variables `WhatGrade` (racing / cruising / collection / social: `ic` frame), `WhatDbProgress` (`earn_grade_points`, `earn_cruising_points`, `grade_title`, `global_title`, `chase`: `disp` frame), `WhatArea` (the zone icon of the Discovery popup, `i1` / `h2` = `DBCruisingZones._flashlabel`), `WhatTuto`, `NewGradeTitle`, `NewGlobalTitle`, `IsLevelUp`, `ProgressCasinoOn`, per skill `<Prefix>LevelCount` / `Title` / `Points` (the gauge ring percent) and `GlobalLevel*`, then calls `dbProgress( 'open_mini' )`; the actions are `open_mini`, `extend` (-> `open_extd`), `close_mini`, `close_extd`, `open_tuto`, `chase_reward`; the A button advances (`A_first`, `A_next`, `set_reward`, FSCommands `dbpg:opbg|clos|skgg|nwti|nwgr`).
- `FreeRideHud::showLevelUp( grade, reward, bNewTitle )` + `setProgress( snapshot )`: mini bar 3.5 s, `extend` (the "Level UP!" panel with the grade gauge, Global level n/73 and "Next level in N pts", the `§POINTS§` token of `m_NextLevel_`), 6 s, `close_extd`. The reward text goes to the goal bar (the retail reward page needs `WhatReward` and the A sequence, not driven) `[INF]`. `docs/images/port-levelup-retail-s18.webp` (mini bar -> panel -> closed). `-testlevelup` forces one.

### Near misses (risk driving)
- `Traffic::nearMissTest` (`-nearmisstest`, log only): a virtual car passes a live traffic car at +16 m/s anchored to its heading: lateral offset 2.4 m (gap 0.55 m) -> detected 1 (closing 16.0 m/s), 0 contacts; 3.8 m (the next lane, gap 1.95 m) -> 0 detected. Oncoming passes in the normal lanes do not count `[INF]`.
- Tuning: the speed rate is $0.10 per s per km/h over 120 (was 0.15): 180 km/h earns $6 per s; a near miss $25 + closing speed (about $41); air time $30 per s. Not driven by hand (headless).

### Not done / next
- The reward page and A-button flow of the popup, the global title popup (`NewGlobalTitle`) and the Discovery area-level popup (`NewAreaTitle`) are not triggered.
- The retail frim rates remain unknown; no real driven near miss with a pad.

## Session 2026-10-28 (thirty-fifth, `win`): level-up popup with the reward; risk-driving money (near miss, speed, air time)

Owner: "both please" (the level-up popup with the reward name; skills earning beyond exploring - the owner described the retail "frim": risks earn free money, then you bank it).

### Level-up popup
- `skill_levels`: rows now carry `Reward` / `RewardDesc` (`_rewardname_hc` / `_rewarddesc_hc`, `§PERC§` replaced by the row's `_percent`). Examples `[DATA]`: Racing level 3 "Metal detector", Discovery level 3 "New items available", Collection level 3 "x% discount on each vehicle", Social level 3 "Earn more money in club".
- `GMFreeRide` watches the four skills (`FindSkillLevelIndex`); a crossed threshold calls `FreeRideHud::showLevelUp( skill, "n/15", title, reward )` -> the goal popup "Discovery 3/15 - Tourist - New items available". Verified by exploring from 524 Discovery points (`docs/images/port-levelup-popup-s18.webp`); `-testlevelup` forces one.
- **Ruled out**: the retail popup, GENERAL.vmf `db_progress` (root vars `WhatGrade` / `WhatDbProgress` / `IsLevelUp` / `NewGradeTitle`, labels `open_mini` / `open_extd` / `close_*`, FSCommands `dbpg:opbg|clos|skgg`): setting those and jumping to `open_mini` shows an empty bar with the racing icon and a play glyph; the full sequence (icon frame by grade, text, reward page) needs more of its scripts. Next step: `-dumpclips` on `/db_progress` and read its `disp` / `title` frames.

### Risk driving (`source/game/freeride/risk_driving.{h,cpp}`, new) `[INF]` rates
- Fast driving above 120 km/h pays $0.15 per s per km/h over; a near miss with traffic ($25 + the closing speed in m/s); air time $30 per s (paid on landing, 0.5 s minimum). A multiplier grows 0.25 per event up to x3 and resets 5 s after the last one. Standing still 1.5 s (< 8 km/h) **banks** the pending money into `PlayerProfile::Money` (saved at once); any contact with a traffic car loses it ("Crash! Money lost").
- `Traffic::detectNearMisses`: circles gap < 1.6 m with no contact, closing faster than 8 m/s, once per car per 4 s.
- HUD (`FreeRideHud::setRisk / drawRisk`): "RISK $n xM", the last event line (3 s), "Banked +$n" (4 s) under the goal bar, top left.
- Verified: `-riskselftest` (5 s at 180 km/h + 2 near misses + 1 s air = $191 at x1.75, banked on stopping, crash -> $0) and `-riskdemo` (forces 180 km/h) shows the HUD (`docs/images/port-risk-hud-s18.webp`). Not driven through a real near miss with traffic (a car must pass within 1.6 m at speed).

### Not done / next
- The retail "frim" rates, rules and banking trigger are not decoded (the owner's description only); the points skills (Racing / Collection / Social) still earn nothing; Discovery earns only from new roads; no jump / drift / slipstream events; no sound; the pause menu money field shows the banked total.

## Session 2026-10-28 (thirty-fourth, `win`): skill levels from the database; GPS readout and arrival message

Owner: "do both" (skill level thresholds; GPS distance readout and arrival message).

### Skill levels `[DATA]`
- `source/game/skill_levels.{h,cpp}` (new): the level tables of the database - `DBRacingLevel._rlpneeded`, `DBCruisingLevel._crlpneeded`, `DBCollectionLevel._colpneeded`, `DBSocialLevel._slpneeded` (16 rows each, levels 0-15: 0, 125, 300, 525, 825, 1200, 1650, 2175, 2795, 3510, 4320, 5225, 6225, 7355, 8615, 10000), the title is the localized `_grade_hc` (Trainee / Driver / Pilot / Champion; Homebody / Tourist / Digger / Traveller; ... Wealthy; ... Charmer) and `DBPlayerLevel` (56 rows, title only: Inglorious 0-9, Someone 10-19, Popular 20-29, New Star 30-39, Celebrity 40-..., Superstar). The owner's captures match exactly (125 Pts to level 1 from 0, "Next title: Level 3", Collection 380 points = level 2 with 145 to level 3). `-logskills` dumps the tables.
- `DBAreaLevel` (18 rows, 4 areas, levels 1-5, 0/200-250/600-680/1000-1500/1600-1700) is the exploration area level; the Discovery page's 14500 points and 18 levels are the cruising table plus these `[INF]` (not wired: the Discovery points are the profile's only).
- `pause_menu.cpp`: `ComputeSkill` -> level, title, points to the next level, first level of the next title; the Global level is the sum of the skill levels, titled from `DBPlayerLevel`; refreshed on every `gapr:open`; the Completion page keeps its larger counts (19 / 20 / 17 / 17, with the casino levels) and the skill pages "n/15" (Discovery "n/18") via `gapr:part:*`. The detail percent rows are 0 (placeholders removed) except the explored roads.
- Verified: profile 700 / 40 / 380 / 100 points -> Competition 3/15 "Driver", "Next level: 125 Pts - Next title: Level 7", Collection 2/17, Global 5/73 (`docs/images/port-progress-levels-s18.webp`).

### GPS readout and arrival
- `MapScreen::getRouteLength()` (road left, from the route points) and `takeArrival()`; the HUD draws "GPS 53.0 km" (or "850 m") under the minimap while a route exists; on arrival (40 m) the goal popup shows "You have reached your destination" `[INF]` (no such string in the tables). Verified: Hawai route 53.0 km readout; Ibiza spawned on the destination node -> popup (`docs/images/port-gps-readout-arrival-s18.webp`).

### Not done / next
- Where the 14500 / 18 of Discovery come from (area levels); points are only earned by exploring (1 per 50 m); no other skill earns points (races, shops...); the level-up notification / reward (`_rewardname_hc`) is not shown; the GPS readout is km only (no unit setting).

## Session 2026-10-28 (thirty-third, `win`): live GPS route and the route on the HUD minimap

Owner: "Live route update".

### What changed
- `MapScreen::tickRoute( carPosition, dt )` (called from `GMFreeRide` every drive frame): recomputes the route from the car once a second while a destination is set; clears it on arrival within 40 m `[INF]` (log "destination reached"). The search start is trimmed to the point of the route nearest to the car (within 60 m) so it no longer backtracks to a node behind it. The `(node, node) -> link` table is built once in `loadRoads` (`edgeBetween`). `-logroute` prints each recompute.
- `FreeRideHud::setRoute` / `drawMinimap`: the route (red, 4 px, clipped to the minimap) over the road triangles.

### Verified
- Hawai: destination set on the map, map closed, 800 frames of driving: route recomputed each second (3644 -> 3638 points, 53.0 km), red line from the car along the road on the minimap (`docs/images/port-minimap-route-s18.webp`).

### Not done / next
- The red line sits a few metres beside the white road triangles of the minimap: the ROAD_DETAIL curve is not exactly the road centre (the curve starts ~10 m from the nodes) `[INF]`; not measured.
- Arrival was not driven to (logic only); no turn-by-turn, distance readout or "recalculating"; no off-route logic beyond the nearest-point trim.

## Session 2026-10-28 (thirty-second, `win`): Hawai map zooms and GPS tested; the route follows the road curves

Owner: "Test Hawai zooms/GPS".

### Tested (Hawai, `-tp 36148,8922 -snaproad`, autoplay `hold=up/down/right`, `wheel=1`, `press=validate`)
- Zoom levels 0-3: all four work, roads (blue) and the spot icons are placed at each level; `docs/images/port-map-hawai-zooms-s18.webp`. At level 1 the streamed terrain ends in blocky sector edges beyond the streamed radius (the known cap, session 23), black / dark sea beyond.
- GPS: OK on the cursor sets the destination, "route 204 points, 47.4 km" across the island, drawn red.
- **Found**: the route cut across bays (Dijkstra over node chords + Chaikin smoothing).

### What changed
- `MapScreen`: link weights are the curve length; `computeRoute` expands every node step into the curve of the shortest link between them (`roadShapes`, oriented) and no longer smooths. Hawai now 3644 points, 53.0 km, red on top of the roads (`docs/images/port-map-hawai-route-s18.webp`); Ibiza regression (25.7 km, 1274 points) fine.

### Not done / next
- Route recompute while driving (not recomputed as the car moves), the `MapInclination` values of `worldconfig.cpr` for Hawai (tilt 20 / 30 / 40 / 50), and the Casino map (3 road nodes, no texture).

## Session 2026-10-28 (thirty-first, `win`): Hawai flat island map

Owner: "Hawai flat map".

### What changed
- `gm_freeride.cpp`: the flat map is fitted for **any** island with a colour map: the world extent is the `<Area NbColumns= NbRows=>` grid of `worldconfig.cpr` x 6144 m (Ibiza 7 x 6 = 43008 x 36864 as before; Hawai 11 x 9 = 67584 x 55296 `[INF]` same rule). Hawai uses `hawai-c.2db` (loaded by `EdenWorld` as `<island>-c`). Casino has no map texture.
- HUD island label "HAWAI" -> "HAWAII".

### Verified
- `-island hawai -tp 36148,8922`, map at level 0: the Oahu texture fills the view, the seeded explored links (blue) follow the coast and the roads with none in the sea; the car and the Racing School / photo icons are placed (`docs/images/port-map-hawai-flat-s18.webp`). Fit judged by eye on the road overlay, not measured.

### Not done / next
- Zoom levels 1-3 and the GPS route on Hawai are untested; the `MapInclination` / `MapCenterCorrection` values of `worldconfig.cpr` (Hawai Island 0, Far 20, Medium 30, Close 40, Very Close 50 degrees) are not used (the Ibiza rescale from the owner's grabs is used for both).

## Session 2026-10-28 (thirtieth, `win`): Hawai roads, explored roads and traffic

Owner: "Hawai roads".

### Found
- `MapScreen::loadRoads` already read Hawai (`Hawai_ROAD_MAP.IA`: 1779 nodes, 2827 links) and Casino (3 nodes, 1 link); the real gap was traffic (0 cars: zone 3 is nearly empty on Hawai) and the missing curve / explored checks there.

### What changed
- `Traffic::load` no longer uses a fixed zone: it averages every **populated** `aiconfig` zone (max density > 1) for density and vehicle weights `[INF]` (Ibiza: 4 zones, 12 vehicle ids; Hawai: 4 zones, 20 loaded). Hawai vehicles are `islands/hawai/vehicules/traffic/<_model_name><_version_name>.bnk` (compact1..3, suv1..6, taxi1, police1, truck1..3, bus1 ...); a missing variant (minivan2) falls back to variant 1.
- Casino has no `aiconfig.cpr`: no traffic there (and 3 road nodes).

### Verified
- Hawai `-tp 36148,8922 -snaproad -trafficnear`: 13 cars (target 12.8), HUD "Hawai 2 / Hawaii Area 2", exploring recorded `explored_hawai = 137,166,168`, `points_cruising = 19`. Map with 1400 seeded links: blue roads on the 3D map (`docs/images/port-map-explored-hawai-s18.webp`, `port-traffic-hawai-s18.png`).

### Not done / next
- The Hawai **flat island map** (texture + world extent like Ibiza's `setIslandMap( ..., 43008, 36864 )`) is not fitted: the Hawai map still uses the 3D camera; the route / explored overlay works on it.
- The HUD island label reads "HAWAI" (the folder name).
- Hawai traffic uses US vehicles; the aiconfig zone of the player is still unknown.

## Session 2026-10-28 (twenty-ninth, `win`): Game Progress pages complete; real road curves; traffic follows them and obeys junction signals

Owner: "do both, wire up as much as possible" (progress page gaps; traffic zones and lights).

### What changed
- **Game Progress**: found with the new `-logprogress` script dump. `<Prefix>Points` drives the gauge ring (`maskLevel.gotoAndStop( focusPoints )`, so it is a **percentage**), `<Prefix>LevelCount` is the "n/max" text (a string now), the points line is a text bound to `_root.GPTitle` (set per page on `gapr:part:*`), `NextLevelCount` carries "125 Pts". Retail level maxima 19 / 20 / 17 / 17 (= 73, the `NB_LVL`). `docs/images/port-progress-pages-s18.webp`.
- **Road curves**: `Ibiza_ROAD_DETAIL.IA` `ATED` records are the curve of each road (`docs/re/roads_spec.md`). `MapScreen::loadRoads` builds `roadShapes` per link; explored tracking and the blue drawing follow the real roads (`docs/images/port-map-explored-curves-s18.webp`); a stray-point filter (250 m jump) removes bad triples.
- **Traffic**: agents move along the curve (`Agent::Edge / bForward / Distance`, `sampleEdge`); junction signals (`Traffic::isRed`, `-notrafficlights`) `[INF]`; collision from session 28 unchanged.

### Verified
- Progress pages: Competition "points: 0 / 10000 (0%)", gauges "0/19", Global "0/73", "Next level: 125 Pts".
- Map: curves drawn for 900 seeded links, no stray lines.
- Traffic: 15 cars on the curves, `-trafficselftest` still correct; 3600-frame run: 1-5 cars stopped with signals, 0-1 without.

### Ruled out
- A zone field in `ROAD_MAP.IA` nodes / roads or `worldconfig.cpr` for the 6 traffic zones: none found.
- A signal list in the road files: none found, so the signals are invented `[INF]`.

### Not done / next
- The 6 traffic zones (still zone 3); real lights (models, timing); lane count / speed limit from the `ATED` flag words; `FTED` / `ETED` records; Hawai / Casino road data (no `loadRoads` for them on the map yet).

## Session 2026-10-28 (twenty-eighth, `win`): real cruising zones for the HUD badge; traffic collides with the player

Owner: "please do both" (traffic collision, HUD zone badge from the real zones).

### What changed
- `source/game/freeride/cruising_zones.{h,cpp}` (new): parses `islands/<island>/level/cruisingzones.bin` (`CRZODATA`, closed polygons in world metres `[DATA]`, layout in the header). Ibiza 2 zones, Hawai 3 usable of a header count 4, Casino none. `FreeRideHud::tick` finds the polygon under the car; the badge number, the name ("Ibiza Area 1/2", from DBCruisingZones `_name_hc` in file order `[INF]`) and the island label (was hardcoded "IBIZA") follow it. Outside every polygon the last zone stays.
- Traffic collision: `Traffic::resolveCollision` + `VehicleDynamics::addVelocity`, called after `vehicle.step` in `GMFreeRide`. Details in `docs/re/traffic_spec.md`.

### Verified
- `-tp 20445,25588` -> badge "2 / Ibiza Area 2", `-tp 15000,12000` -> "1 / Ibiza Area 1" (`docs/images/port-hud-zone-s18.webp`). Hawai log: 3 zones "Hawaii Area 1..3".
- `-trafficselftest`: player dv ~10 m/s against a 12 m/s ram; the rammed car speeds up 18.9 -> 26.2 m/s. Not tested by driving into a car (the headless drive has no steering); no pad run.

### Ruled out
- The block `index` field of Hawai (5, 2, 4) is not the DB row order (h1..h3): names are assigned in file order, which may be wrong for Hawai `[INF]`.

### Not done / next
- The 6 `aiconfig.cpr` traffic zones are not the 2 cruising zones; traffic still uses zone 3 (the mapping is unknown, perhaps a per-node field of the road graph).
- Angular response, damage and sound on a hit; traffic lights.

## Session 2026-10-28 (twenty-seventh, `win`): ambient traffic on Ibiza (first version)

Owner: "then add in traffic". Spec: `docs/re/traffic_spec.md`.

### What changed
- `source/game/freeride/traffic.{h,cpp}` (new): reads `aiconfig.cpr` (XML, hourly density + vehicle weights by zone), loads the 12 Ibiza traffic models (`islands/ibiza/vehicules/traffic/ibiza_*.bnk` as plain `EdenModel`s, name = `_model_name` + `_version_name` of DB ids 3001-3014), and moves agents along the road graph with braking, right-hand lane, ground-aligned orientation.
- `MapScreen::getRoadNodes / getRoadEdges`; `GMFreeRide` loads (`-notraffic` disables), ticks (next to the HUD), draws in the scene pass. `-trafficnear` = close spawn ring for test shots.

### Verified
- `-tp 20445,25588 -snaproad -trafficnear`: log "12 vehicle types ... hour 14.0 -> 14.4 cars", 15 cars alive; `docs/images/port-traffic-s18.webp` shows a police car and a taxi in the lanes (3 frames). +11 ms / frame in the headless run (62 -> 73 ms).

### Not done / next
- See "Not done" in `docs/re/traffic_spec.md` (collision with the player, lights, zone lookup, Hawai).
- Game Progress: the `NB_LVL` token of the Completion text is now replaced by 73 (`flash_localization.cpp`, `[INF]` the retail capture value). Still missing: the points line, the "/max" gauge counts, the "Pts" suffix.
- Found along the way: the **PC save `DATA` does not decrypt with the `.cpr` XTEA key** (`otdu2_bnk_tool decrypt` gives noise), so it uses another key; the 360 save is plain XMBF (`docs/re/savegame_spec.md`).

## Session 2026-10-28 (twenty-sixth, `win`): explored roads (blue on the map, Discovery points, saved); saving logo in the pause menu

Owner: fix the pause icon, then explored roads, then traffic; "go for it" while away.

### What changed
- **Pause icon**: `FreeRideHud::tickSavingLogo / drawSavingLogo`, called by `GMFreeRide` while the pause menu or map is up (the HUD is frozen there). Evidence `docs/images/port-saving-logo-pause-s18.webp`.
- **Explored roads** (`map_screen.cpp`): `loadRoads` now keeps the link list (`roadEdges`, index = save id) and `edgeExplored`; `trackCar( position, dt )` (every 0.25 s, called from `GMFreeRide`) marks a link explored when the car is within 18 m of its segment `[INF]`. `drawIsland` draws explored links as blue ribbons (`0xffc8821e`, 2.5-9 px by zoom level) under the red GPS route. `getExploredPercent()` feeds the Discovery "Roads" row (`/:PercentRoad`).
- **Discovery points**: 1 point per 50 m of newly explored link `[INF]`, into `PlayerProfile::Points[1]`.
- **Saved**: `profile.sav` line `explored_<island> = id,id,...` (`PlayerProfile::ExploredRoads`). The ids are indices into the island road graph, deterministic for a given game data set.

### Verified
- `-tp 20445,25588 -snaproad`, hold accel 600 frames: 3 links explored, `points_cruising = 10`, `explored_ibiza = 61,299,300` after the map close (log "saved ... (map closed)").
- Seeded 900 links: `docs/images/port-map-explored-s18.webp` (blue network on the flat map).

### Not done / next
- Ibiza graph only (Hawai / Casino have no `loadRoads` graph on the map). Links are straight node-to-node lines, so curves are cut `[INF]`; the retail map draws the real road shapes (`HudMapRoads`).
- Skill levels from points (thresholds not decoded) and the "Explored 75%" tooltip counters.
- The retail rule for what counts as explored (side roads, a speed or distance condition) is unknown.

## Session 2026-10-28 (twenty-fourth, `win`): Game Progress pages open from the pause menu, fed with placeholder data

Owner: "Progress pages" (Completion / Competition / Collection / Discovery / Social), reference captures of the retail pages, and "explored" = roads driven for the first time turn blue on the map and give Discovery points (answers the open question in session 23).

**Reproduced** (opens the movie's own pages):
    build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -no3d -nopad -fixedtimestep -autoplay "200:press=pause;320:press=right;380:press=validate;520:key=e;640:shot=C:/OpenTDU2/_b.ppm" -exitframe 680

### What changed
- `source/game/pause_menu.cpp` - `pushProgressData()` sets the root variables MAIN.vmf reads: `<Racing|Cruising|Collection|Social><Points|LvlPoints|LevelCount|Title>`, `Global*`, `Percent*` (the per-objective rows), `GP<Skill>Title`, locks. Racing = Competition, Cruising = Discovery `[DATA]` (variable names). Values are the owner's capture `[INF]` (`kSkills` / `kPercents`) - no save game yet, replace there.
- `onProgressCommand()` answers `gapr:part:raci|crui|coll|soci` (sent when the pages open) with `NextLevelCount` / `NextTitleCount` `[DATA]`.
- LB / RB (keyboard **Q / E**) send SWF keys **49 / 51**, which button 17227 maps to `this.Move('left'|'right')` `[DATA, -logpausekeys]`. Left/Right arrows do not switch tabs. The pad -> flash key map is still untraced `[INF]`.

### Verified
- Screenshots `docs/images/port-progress-completion-s18.webp`, `port-progress-competition-s18.png`: tab row, header "Competition (Trainee)", next level 125 / next title Level 3, the objective rows with the captured percentages, all five tabs reachable with E.

### Not done / next
- Points line "Competition points: 0 / 10000 (0%)" missing; gauge shows "0", retail "0/15" (the movie builds `gradeLevelCount + "/" + /:LevelCount`, set to 15, still not shown); "Pts" after "125"; `NB_LVL` token in the Completion text is never replaced (retail: 73); avatar portrait, flag and the money/houses/cars row use defaults. Start at the constant pool near `RacingLvlPoints` in MAIN.vmf (offset ~374835) and `initGauges`.
- Explored roads (blue on the map, Discovery points) needs a driven-roads record - not started.

## Session 2026-10-28 (twenty-third, `win`): the world map has its island - it was a missing map CAMERA, not missing Flash content

The owner asked for the missing features (races, cutscenes, shops, map). Started with the map (priority 1).

**The session-9 diagnosis was wrong.** "The map centre does not draw" was seen in `-no3d` runs, which skip the 3D pass. With 3D on, the centre is simply the normal world behind a transparent movie. MAP.vmf draws chrome + a fixed cursor clip (`curseur`, `setCursorSize`, `displayCursor`) and **no island** `[DATA]`; the island is the 3D world seen by a map camera `[INF]`. Evidence: PS3 strings `MapRotation`, `MapInclination`, `MapDistanceAddedToZoom`, `MapCenterCorrection`, `HudMapRoads`, `MAP_LOD_SELECTION_SCALE` (0xf540d0..0xf54168, 0xf77e50) `[CODE]`. `AllRes/MapScreens/*` are per-event thumbnails, not the island.

### What changed
- `source/game/map_screen.{h,cpp}` - orbit camera (`resetCamera`, `updateCamera`, `getCamera`, `getStreamRadius`): starts over the car at 1400 m, pitch 60 deg; pan = left stick / arrows, zoom = triggers (accel/brake keys W/S) / mouse wheel, rotate+tilt = right stick. Tuning constants are `[INF]`.
- `source/game/freeride/gm_freeride.cpp` - while the map is open: apply the map camera, stream sectors around the camera centre (radius 1-4 by zoom), `view.tick`; re-stream around the car on close.

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -autoplay "300:press=map;480:shot=...;500:hold=brake;560:release=brake;700:shot=..."` - screenshots `docs/images/world_map_3d_default.webp`, `world_map_3d_zoomout.png`: island, roads, coast render under the Flash chrome.

### Follow-up (same session, after the owner's retail screenshot)
The owner's TDU World screenshot shows the retail map is **the whole island seen flat**, dark sea, not a streamed 3D view. The source is `islands/ibiza/level/ibiza.bnk` -> `maps/ibiza-c.2db` `[DATA]`: 1024x1024 DXT1, **pixel data at file offset 0x58**, wrap it in a DDS header to view it. Mapping to world metres, fitted by overlaying the road-graph nodes of `Ibiza_ROAD_MAP.IA` (`EDON` records, x/y/z floats at +12 of the tag) on it: **u = x / 43008, v = z / 36864** `[INF]` (the nodes cluster on Ibiza Town, San Antonio, the coast roads; the z extent is *not* 43008). Orientation: u right, v down, unflipped.
- `MapScreen::setIslandMap / setPlayer / drawIsland / updateIslandView`: sea fill (0,14,46 = the texture's own colour), the texture panned/zoomed, a red player arrow along the car heading. Pan = left stick/arrows, zoom = triggers (W/S)/wheel. `GMFreeRide` skips the 3D pass and HUD while it is up; Hawaii/Casino keep the 3D map camera until their extents are fitted.
- `EdenWorld::getIslandColorMap()` accessor (+ the ocean mesh bounds, which are NOT the texture extent: -98304..141312).
- Screenshots: `docs/images/world_map_flat_ibiza.webp`, `world_map_flat_zoomed.png`.
- Still missing vs the retail screenshot: title ("Single Player Challenges") and car name / money in the header, the mission/shop icons, the red GPS route, the cursor reticle, the sea shimmer. Spawn is NE (36008, 5814).

### Second follow-up: what the owner's six retail grabs say about MAP.vmf (new `-dumpclips <depth>`)
The tabs, title, header, icons, route line, car silhouette and tooltip cards are all **drawn by the movie** `[DATA]`; the game only feeds data. `-dumpclips N` (new, `FlashPlayer::dumpClips`, runs 90 ticks after the map opens) prints the clip tree. Root clips `[DATA]`: `strokes_pack` (stroke_01..20 = the red route, 20 segments), `pack` (the spot layer), `player` (car silhouette), `curseur` (+`cible`), `help`, `presets` (the tab row: `ic_0..ic_5`, `bt_0..5`, `txt` = title), `tool_tip` / `infos` (the card with picture + counters), `lure_chall`, `lure_zoed`.
- **`pack/point`** is the per-spot template: `spot` (856 frames, icon art by frame), `locator`, `fleche`; inside `spot`: `news lock forbidden favorite done offroad rank link hlight glow target shape`. The PS3 build addresses `/pack/point%03d` and `/pack/pointfinish` (strings 0xf6a738..0xf6a758) and calls `resetIconStatus setIconNews setLockStatus setDoneStatus setObjectiveStatus setTargetStatus setRankStatus setIconFavorite setPlayedStatus setForbiddenStatus` on them `[CODE]`. So the native does `duplicateMovieClip` (supported: `A_CloneSprite`) per spot, places it, picks the `spot` frame by type, then sets statuses. Not implemented.
- Frame labels the native calls: `zoom_default zoom_lod dezoom_lod dezoom_lod2` (icon LOD per zoom) `[CODE]`; filters `f_asphalt f_dirt f_classic f_cup f_event f_driveIn f_speed_m f_speedtrap_m f_race_m f_eliminator_m f_hotspots_m f_kyd f_chase_m f_ftl f_copilot_c f_carDealer f_carDealerClassic f_bikeDealer f_carWash f_carPaint f_tuning f_carRental f_stolenCar f_auctionHall f_estates f_clothes f_home f_forsale f_club f_watches f_hair f_surgery f_airport` (the Filters screen) `[CODE]`.
- Tooltip variables `[CODE]`: `/:SpotName /:WhatSpot /:WhatSpotType /:CurrentSpotType /:IsSpotLocked /:IsSpotFavorite /:NbPhotosTaken /:NbPhotosTotal /:nb_licences /:nb_licences_done /:AreaName /:AreaEventsDone ...`; the card pictures load from `FrontEnd/AllRes/MapScreens/<Spots|Contest|ig2>/<name>.bnk` (e.g. `spots/eair_a_2453.bnk`, `contest/as-1-1.bnk`, `ig2/duela2a.bnk`).
- **Header, done**: `/:player_car` (= car name; the car name shows), `/:money` + `/:MapPlayerMoney`. The PS3 build never uses the brand-hash as a string key (`getString(BrandHashcode)` returned an unrelated string: the key space is different). The retail name "Audi RS5 Coupe" needs the DB's full name key, not decoded; the header uses `_model_name` ("California"). Money is 0: there is no money system in the port. The movie functions `setPlayerInfos`, `setTitleScroll` exist in its constant pool but are not defined on `/`, `/infos`, `/infos/car` (invoke did nothing).
- **Title ("Single Player Challenges") and the selected-tab red highlight are missing**: `presets` runs `LightIc(1)`/`ChangeTxt()` but its `txt` stays undefined, `slct` undefined. Likely needs the tab selection event (`MapTabObjective`, `/presets` `setObjective`) - next thing to try.
- **Retail map is probably a 3D view**, not flat: PS3 has `MapRotation`, `MapInclination`, `MapDistanceAddedToZoom`, `MapCenterCorrection`, `RT HUD MAP`, and the retail legend says "Camera: right stick". The flat texture is the same picture at the default (straight-down) view; the 3D tilt is not reproduced.

### Third follow-up: four discrete zoom levels (owner's zoom grabs)
Retail has **4 zoom levels**; each steps to the cursor; the furthest shows the whole island and zooming out past it opens the credits; the two closest are a tilted 3D view (trees, fields, roads as grey ribbons) `[DATA: owner's grabs]`.
- `MapScreen::updateIslandView`: LT/RT (keyboard accel/brake keys) or the wheel step one level. Levels 0 and 1 are the flat `ibiza-c` texture (level 1 = 10.7x level 0: measured on the grab, San Antonio -> Ibiza Town is ~10 m per grab pixel `[INF]`); levels 2 and 3 are the tilted 3D camera (distance 3200 m / 1100 m, pitch 55 deg `[INF]` guesses) with sectors streamed around the centre. `isFlat()` selects the path; `GMFreeRide` skips the 3D pass and HUD only for the flat levels, and the HUD is hidden at every level. The map stays centred on the cursor, so a step zooms to the cursor.
- Bug found on the way: `drawIsland` painted the flat texture at every level, magnified 30x over the 3D view (a blur). It now only draws when `isFlat()`.
- `-mapatcar` starts the map centred on the car instead of the island centre (diagnostic).
- Screenshots: `docs/images/world_map_zoom1_flat.webp`, `world_map_zoom2_3d.png`, `world_map_zoom3_3d.png`.
- Not done: **credits** past the furthest level (`credits` / `credits_back` labels; text `FrontEnd/AllRes/credits.txt` into `/credits/scroll/title%d`, `txt%d`); the **free cursor** at level 0 (retail moves the cursor over the island; here the map pans under a fixed centre cursor); **haze** washes out level 2; roads are the world's own meshes, not the grey ribbon lines of `HudMapRoads`; the 3D distances are guesses until compared with more grabs.

### Fourth follow-up: zoom scales and tilt from four grabs of one spot (CORRECTS the third follow-up)
The owner's four same-spot grabs (levels L1..L4, retail screen 2000 px wide) give `[DATA]`: **47 / 11 / ~3 / <1 m per grab pixel**, so each level is **~4x** the last (the earlier "10.7x" was a unit mix-up: I inverted a virtual-pixel conversion; the measured 10.8 m/px grab was L2, not L1). L2 is crisp (airport, runway) and nearly top-down, so it is **not** the upscaled 1024 texture: L2..L4 are one 3D camera that tilts as it comes in, about 75 / 60 / 45 deg `[INF]` (read off the perspective in the grabs); retail is **free of haze** at every level and its sea is dark with sparkles.
- `MapScreen::updateIslandView`: level 0 = flat texture (unchanged); levels 1..3 = 3D camera, distance 10500 / 2700 / 700 m and pitch 75 / 60 / 45 deg `[INF]`. `isFlat()` is now level 0 only.
- `EdenSceneView::setMapView()`: no fog (`fog_max` 0), no haze pass, no sky / sun halo, deep-sea clear colour (0,14,46). `GMFreeRide` turns it on only while the map camera drives the view.
- Screenshots: `docs/images/world_map_L2_3d.webp`, `_L3_3d.png`, `_L4_3d.png` (`-mapatcar`).
- Still off vs retail: colours are warmer / more saturated and the sea is turquoise (retail: darker, desaturated, grey road ribbons from `HudMapRoads`); beyond ~3 km (sector radius cap 4) the view falls back to the island-level terrain, visible at L2 as a lower-detail band; the closest level may need a lower pitch; the cyan track outlines and the thick red route in the grabs are not drawn (route = `strokes_pack`).

### Fifth follow-up: tab row + title, credits, and a real `duplicateMovieClip`
- **Found by dumping the movie's own members** (`-dumpclips 1 -dumppath /presets`, new `-dumppath`, `-dumpafter N`; the dump now prints root/clip functions, text-field variable names, font, alpha) `[DATA]`: `_root.Clip` = `navigator` (functions `A B Y setZoom onJoinEnded`; **no `Move`**), `/presets` has `Move Clic LightIc ChangeTxt setObjective showHsys filtersDisplay sendCmdIc showQuickMatch`, root has `openCredits onCreditsEnded setCursorSize displayMapCursor`, `/credits` has `Open Close beginScroll onClosed B`.
- **Tabs + title**: LB/RB (keyboard **Q / E**) call `/presets.Move('left'|'right')`; the movie lights the tab and sets the title itself ("Multiplayer Challenges", "Spots for Avatar" match the retail grabs). The row opens on tab 0 with nothing lit and no title; `MapScreen::tick` steps it right then left (ticks 40 / 55 after opening) to light it `[INF]`: "Single Player Challenges" + red-lit tab 0, as retail. `/:MapTabObjective` (`-maptab N`) only moves the red "objective" dot. No SWF key code (1..255) switches tabs (probed).
- **Credits**: zooming out past the furthest level (LT/brake at level 0) calls `_root.openCredits()` -> `credits.Open()`, `beginScroll()`, FSCommand `cred:open`. The game's job (MngCredits, PS3 strings `/credits/scroll/title%d`, `txt%d`, `empty`, `CreditsPixelPerSec`) is done in `MapScreen::buildCredits`: parses `FrontEnd/AllRes/credits.txt` (`%Title`, `[[Role]]`, `((names))`, `/` gap), fills `title0`/`txt0` and clones of them (variable `txt`, markup `#color:64c8ff#` titles, `#color:999999#` roles, `#color:FFFFFF#` names), lays them out, and scrolls `/credits/scroll:_y` up at 45 stage px/s `[INF: speed, spacing, start]`. **The movie leaves `scroll` at alpha 0** (found in the dump) so the game sets `_alpha` 100. BACK -> `credits.B()` + `Close()`, then the map returns. 16 titles + 230 text blocks, 31568 px.
- **`FlashPlayer::duplicateClip` / `duplicate()`**: `duplicateMovieClip` was a no-op (the `A_CloneSprite` opcode just popped its arguments). Now implemented for the opcode, the `duplicateMovieClip` method and a game-side `duplicate(path, newName, depth)`. The spot icons need exactly this (`/pack/point%03d`).
- Evidence: `docs/images/world_map_tabs_titles.webp` (tab 0 -> 1 -> 2 -> 1), `docs/images/world_map_credits.webp`.
- Not done: credit layout vs retail (position / speed unverified: please send a credits grab), `Clic`/`A` on a tab or spot, `navigator.setZoom(level)` (the movie's own icon LOD hook; not called), the spot icons, route, cursor reticle.

### Sixth follow-up: spot icons (from `spots.bin`)
- **`spots.bin` format** `[DATA]` (`islands/<island>/level/spots.bin`, Ibiza = 79 records): `.SPT`, `SPTA`, `u32 size`, then 0x110-byte records. `+0x08` building bank name (8 chars: `ECD_AUDI`, `EPH_L3E`, `ETUN_A`...), `+0x10` world position (3 floats; x/z = the map axes), `+0x100` **category byte**. Categories decoded by matching building names `[INF]`: `00` houses (`EPH_*`) `01` car dealers (`ECD*`) `03` auction (`EAS_*`) `04` tuning (`ETUN_*`) `05` car rental (`ECH_IA`) `06` gambling hall `08` mixed (`ECW` car wash, `ESTICK` paint, `ENC` club, `ERE` estates, `EUC` clothes) `09` racing school (`ERS_*`) `0b` photo studio `10` surgery `11` stolen car `13` hair `16` airport `19` home. The record bytes `+0x34..0x37` (e.g. 1,2,7,5) are the sector address.
- **How the movie draws them** `[DATA]`: `pack/point` is the template; `spot` (856 frames) shows the icon by frame label (`asphalt dirt cup speed race rschool photograph cardealer tuning carwash carpaint carrental stolencar auctionhall airport estates clothes hair surgery club home gamblinghall bookmark player friend ...`); `spot/ic/logo` has 121 frames (brand logo of a dealer tile). `MapScreen::buildSpots` clones `/pack/point` per spot (`point001..`), sets the label, hides `fleche`/`locator`; `updateSpots` projects each spot with `projectToStage` (flat map: x/z * scale; 3D view: the same pinhole as `getCamera`) and shows only the ones of the current tab.
- **Tab -> icons**: the tab row sends `FSCommand bmap:fltr:icf<N>` (N 0..5) when the tab changes `[DATA: -logmaptab]`; `currentTab` follows it. Spots are assigned to tab 0 (racing schools, photo), 2 (houses, clothes, hair, surgery, club, estates, casino), 3 (dealers, tuning, wash, paint, rental, stolen, auction, airport). Tab 1 (multiplayer), 4 (players), 5 (my spots) have none: the server is down in retail too.
- The 3D camera now looks to -z (yaw pi) so north is up like the flat map.
- **Evidence**: `docs/images/world_map_spots_tab0.webp` (3 racing schools + photo studio), `_tab2.png` (houses, hair, clothes, surgery, club), `_3d.png` (icons placed in the tilted view).
- **Dealer tiles are blank** (the brand logo does not show): tried and ruled out `[DATA]`: the `brands_map` atlas (`AllRes/LogoTexturePage.bnk`, 1024x256 DXT5, 64 px cells, `DBBrands._logo_global_index`) is loaded into the movie's pages (`FlashMovie::addExtraPages`, `findPage`); `spot/ic/logo` frame set (found, 121 frames); its `lure` shape has a **bitmap fill with no bitmap** (`tex 0`), so the game must bind the atlas cell: `FlashPlayer::setBitmapOverride` does that for the shape's clip (new) but nothing changes on screen - maybe the clip is in `ic` at alpha 0 until the movie's own call, or the bitmap id differs from the page. Next step: `-dumpclips 6 -dumppath /pack/point006/spot -dumpshapes` after calling whatever sets `lure`'s alpha.
- Not done: Ibiza-only (Hawai/Casino keep the 3D map without icons until their texture extents are fitted); no selection / `Clic`; no event/challenge spots (`ibiza` events come from `DBEvent`, not `spots.bin`), no route.

### Seventh follow-up: camera fly-out / fly-back, cursor, GPS route
- **Camera transition** (owner's note: retail flies out from the car and back): `MapScreen::transition` 0..1 over 1 s (rises on open, falls on close; `getTransitionCamera` = smoothstep of the car camera saved at open -> the map camera, nlerp rotation). While it runs the 3D world renders (`isFlat()` is now level 0 *and* transition done); the flat texture fades in over its last 45 % (`getFlatAlpha`). Level 0's map camera is a straight-down camera at `(360 / mapScale) / tan 30 deg` (~43 km) so the flat texture is its picture. The first version washed out white mid-climb (aerial haze at altitude): `EdenSceneView::setMapView( bEnable, bKeepSky )` now turns fog/haze off from the first frame and keeps the sky until `transition` 0.55. Streaming: car sectors during the fly, the cursor's sectors at levels 1..3. Evidence: `docs/images/world_map_transition.webp` (open: car view -> climb -> island; close: island -> back). Not verified: retail timing (1 s `[INF]`) and path.
- **Cursor** (`curseur`, parked off-screen at x=1399 by the movie until the game places it `[DATA]`): free at level 0 (left stick / arrows, 420 canvas px/s), fixed in the middle at levels 1..3; **a zoom step goes to the cursor** (centre := point under the cursor; zooming out keeps the cursor over the point the view was centred on). Valid target (`isCursorValid`: within 2.5 km of a road node `[INF]`) drives `/curseur/anim` `on` / `stop` + `/curseur/cible` `on` / `off` (the "no" sign) and `/:IsTargetOK`. The car is the movie's own `player` + `target__` clips at its map position (my red arrow is gone); `/target` at the destination.
- **GPS route**: OK (Enter / A) on a valid cursor sets the destination (nearest road node), OK on it again clears it. Road graph decoded `[DATA]` from `ibiza.bnk` -> `Ibiza_ROAD_MAP.IA`: `EDON` nodes (40 B: `+4` id, `+12` x, `+16` y, `+20` z) and `DAOR` roads (48 B: `+28`/`+32` node ids): 856 nodes, 1350 links resolve. Dijkstra from the nearest node to the car, Chaikin-smoothed (2 passes), drawn in `drawIsland` as red quads (3.5 / 7 / 10 / 13 px by level) under the movie's chrome. **The movie's own `strokes_pack` bars are `DefineMorphShape` objects (tag 46), which `FlashPlayer` does not play** (found by cloning them: nothing draws at any frame) - drawing them is the clean follow-up if exact retail line art matters. Evidence: `docs/images/world_map_route_flat.webp`, `world_map_route_3d.png`.
- New runtime bits: `_rotation` (get/set) and `FlashPlayer::setClipTransform` (x, y, degrees, scales); `-dumpclips` prints `cd=` (clip depth) and fill colours.
- Also hidden (not in the retail grabs): `/pack/clue` (the dashed ring with "?"), `lure_*`, quick/club match and other mode clips.
- **Not done**: "explored" (see below), the route is not recomputed as the car moves, no spot selection / OK menu on a spot, no cyan track outlines of the grabs, the cursor shows the ring but not the white crosshair of the island-level retail grab, Hawai/Casino have no flat map / roads / route.
- **"Explored"**: not implemented because I do not know what it refers to. Candidates: (a) darkening of undiscovered areas of the island (the `DBAreaLevel` / `AreaUnlockedRoads` / `AreaEventsDone` variables exist in the movie, PS3 `HudMapRoads` colours roads locked vs unlocked) - needs a save game / driven-roads record that the port has none of; (b) the "Explored 75%" area counters of the tooltip card. Ask the owner.

### Not done / next
- Aerial haze washes out the far zoom; the far sector ring is visible (streaming radius cap 4). Turn off fog / use area LOD for the map.
- No player marker / objective icons / filters: the movie's `bmap:fltr:*`, `bmap:ok`, `maok` FSCommands are unanswered (`MapScreen::load` FSCommand callback). The marker needs world->stage projection (depends on the ScreenRatio rect in `FlashPlayer::beginStage`).
- Not started: races, cutscenes beyond the existing ones, shops. Each is a large subsystem needing its own spec (races: `docs/re` has none; shops: dealership/tuning interiors).

## Session 2026-10-28 (twenty-second, `win`): see-through terrain FIXED - the terrain was drawn alpha-blended without depth write; collision island fix

Owner-verified on Ibiza, Hawaii and Casino (no more holes / floating sheets).

### What changed
- `source/world/eden_world.cpp` `loadHeightfield` - terrain materials (island / area / sector, `TD_HeightMap`, states blend 5/6, no depth write) are now forced opaque with depth write. They were deferred into the blended list and sorted per mesh by the mesh *origin corner*, so a 6 km area mesh could paint over 768 m sector meshes. `-terrainblend` restores the old draw for A/B. [INF cause; confirmed by the owner's A/B with the earlier `-terrainopaque` flag]
- `source/game/freeride/gm_freeride.cpp` - `collision.setIsland( islandName )`; the collision object defaulted to Ibiza, so Hawaii/Casino drove on Ibiza's sector files (trees, poles, walls had no collision). Owner-verified fixed on Hawaii and Casino.
- `-dumpheights` (with any world load) writes `_heights_<name>.bin` + `.s1.bin`.

### Ruled out
- The session 21 "area heights are wrong" claim: on Ibiza area-5-0 vs its loaded sectors the heights agree to 0.00-0.01 m with the plain origin mapping (placement and decoding are fine). The casino dumps misled it (sea-floor plateau -11 m vs the -100 clamp).

### Not done / next
- Party scene shadow bug (pool darkens / hard polygon shadow on water with a tiny camera move; shadow flickers behind the character at selection). Not reproduced headless (the `-intro` autoplay timing lands in the ocean cinematic). Live repro: dark <-> bright pool between nearly identical camera positions. Lead: `source/render/eden/eden_shadows.cpp` `renderCasters` fits each cascade tightly to the camera frustum every frame with no texel snapping and a depth range from `-boundsMax.z - MaxCastDistance`; a receiver outside the slice depth range or a cascade switch would shade whole surfaces. A/B with `-noshadows`, then `-logshadows`.

## Session 2026-10-28 (twenty-first, `win`): c9377d0 REVERTED — the area heightfield data is wrong, not the draw design; hand-off note for a screen-sharing session

The owner played c9377d0 ("full coarse grid, no carve") and it was **worse**: on Ibiza Area 1 the coarse sand sheet floated over the beach road and the lagoons, covering them (their screenshots: a beige band swallowing the ground with palm crowns poking above it, and dark road / water patches on the sand). The commit was reverted (`aac8e53`) — the tree is back at the pre-session behaviour (carve + the original holes).

### The hard measurement this session left behind [MEASURED, `-dumpheights`]
Compare the area-0-0 heightfield (24 m grid) against the loaded sectors' heightfields (3 m grid) at the same world points — they disagree grossly. Sample (world x,z): sector 2-1 says the ground is **−11.18 m** across a whole plateau ((2400,1056), (2304,1152), (2208,1248)); the area grid reads **−88 .. −100 m** there. Sector 3-2 says −16..−6 m at (2592..2784, 1824..2016); the area reads −52..−100 m. The mismatches are big flat −100 patches *inland* — not a smooth offset and not LOD smoothing.

That single fact reframes everything:
- The **band / see-through holes** and the **floating sheets** are the same bug: the coarse surface is drawn at the wrong heights (or the wrong world rect), so it (a) is absent/low where it should close the gap against the sectors and (b) rises over roads and water where it shouldn't be at all.
- Session 10's "the area data is genuinely sparse" table and session 20's "the original draws the full coarse grid [INF]" were both reasoned *around* data nobody had validated. **The very next step is to find where the area heightfield's real data lives or how it is placed.**

### Next step (precise, for whoever picks this up — screen-sharing session or not)
1. Cross-correlate to find the true placement: take `_heights_sector-0-0-2-1.bin` (257×257 f32, 3 m grid, origin (1920, 768)) and slide it over `_heights_area-0-0.bin` (257×257 f32, 24 m grid, assumed origin (0, 0)) at 24 m shifts in a ±1024 m window, scoring correlation where both are > −99 (skip the −100 void/sea clamp). A sharp peak at a nonzero shift = the area grid's real origin differs from `area.Origin` (suspect the same **brick stagger** sectors have: odd rows/columns shifted by half a unit, `getSectorOrigin` vs `getSectorAt`; also check whether the area grid scan order is column-major). The dumps are reproducible with `-logareas -dumpheights` (a casino run writes `_heights_*.bin` for the island, area-0-0 and the loaded sectors).
2. If no shift correlates: decode the area 3dg heightfield properly instead of trusting the first 257×257 primitive (it is type 7, 66049 verts, strides 4/8, stream0 264196 bytes — the *only* heightfield-size primitive in the bank [LOG]; same shape as the sectors). Check `Render3DG`'s vertex-order assumption for the area banks against the sectors (maybe rows run the other way, or stream 0 of the area banks is not plain R32 heights: the −100 / −87.50 / −11.18 repeats smell like quantised or clamped values, and −11.18 = the also-everywhere −1.00×11.18?). A sector-vs-area heightfield byte dump comparison of one bank pair answers this fast.
3. Only after the heights draw the same world as the sectors: re-apply the session 20 draw change (full grid + the `SortBias` terrain ordering, commit `c9377d0` shows the complete diff) and re-test the same spots (`-tp 2642,1588` band; the owner's Ibiza beach-club spot for the sheets).
4. Keep in the picture: the sector heights themselves carry odd constants (−11.18 and −1.00 repeated across miles) — validate them too before trusting either layer (retail capture or the 360 build as oracle).

### Follow-up measurements (session 22, `win`) [MEASURED on the saved `_heights_*.bin` dumps]
- Owner screenshot on Ibiza (game was running; screen-shared via computer use): water sheet cutting through a cliff wall, sand-coloured horizontal slivers floating across the cliff face, flat dark ground with no detail near spawn. Same bug as above.
- **Ruled out: a simple placement error of the area grid.** Sliding sectors 2-1 / 3-1 / 3-2 / 4-2 (all 8 transpose/flip combos, 24 m cell shifts over the whole 257x257 area; then bilinear shifts of +-240 m in 12 m steps, land samples only) gives **no sharp peak** — best mean |dh| is 10-47 m and the best shift differs per sector. So not a shift, flip, transpose or brick stagger.
- **Caveat on the "-11.18 vs -100" finding:** the sector plateau at -11.18 is probably the *shallow sea floor* and -100 the area's "no ground" clamp, so those samples are not evidence of misplacement. Only land samples count (sector h > 0): there the area is the same order of magnitude but not equal (e.g. 3-2: area 5..10 vs sector 1..3; 4-2: area up to -41 where the sector reads +3..26).
- Open question for the next session: the area stream 0 may carry *quantised / LOD-reduced* heights (min -156, max 70 on area-0-0) rather than plain R32 metres; dump one area bank's raw 3DG stream 0 beside its sector's and compare byte patterns before assuming placement.

### Also this session
- `-dumpheights` flag (with `-logareas`): writes `_heights_<name>.bin` (257×257 f32) for every heightfield loaded — kept in the reverted tree? No: the revert restored the pre-session source, so the flag is gone; re-add it from commit `c9377d0` (or this entry) when needed.
- Vulkan validation layer (`-vk_enable_validation_layer`) runs clean on the whole pipeline — no API misuse behind any of these artefacts.
- The noise floor for pixel-diff A/Bs: two identical runs differ by ~5e5 sum-diff / ~360 px at 1280×720 (mostly the animating "Enjoy!" banner). Anything below that is not a change.

## Session 2026-10-28 (twentieth, `win`): see-through terrain attempt — full-grid area draw ([SUPERSEDED by the twenty-first entry above: reverted, the area heights themselves are wrong])

The owner's report: *"it's as if I can see through certain terrain — casino island is best place for these issues but some of this also happens in ibiza as well"* (screenshot: a sky-coloured band cutting through a hill on Ibiza). This is the family behind the open items of sessions 17 part 11/12 (the "dark teal cut-out shapes", the "teal strip between road and slope", the "white sparkles on a dirt slope") — through every gap you see the ocean plane (teal) or the sky / sun (white).

**Reproduced** (and fixed in the same session):
    build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata ^
        -gm FREERIDE -skipcine -nopad -island casino -tp 2642.0,1588.1 -heading 137 ^
        -autoplay "300:shot=C:/OpenTDU2/_cas_new.ppm" -exitframe 320
`docs/images/` not saved this session; the A/B evidence below is numeric (mean pixel diff between runs) plus the driver's own play test.

### Root cause [CODE, with the A/B that proves it]
- The world draws three heightfield layers: island (384 m), area (24 m, `area-X-Y.bnk`), sector (3 m, streamed). The port **carved every 24 m cell covered by a loaded sector out of the area mesh** (`EdenWorld::updateAreaIndices`, the `-logmaterials` idx 368640 = full grid − 4 casino sectors × 1024 cells; the design note said "the 3 m sector heightfield is drawn instead").
- The carve is *byte-exact* — the bug is the idea, not the bytes. Between the fine surface (sector mesh, in the carved cells) and the coarse surface (area mesh, in the kept cells) there is a **silhouette gap**: the 24 m surface rides over the cliff top while the 3 m surface drops into the valley behind it, and rays through that wedge hit neither mesh → sky / ocean show through. Size scales with the height mismatch — a metres-wide band at a cliff, a sparkle on a slope, a teal strip at a shore cut.
- Proof: drawing the area mesh **without the carve** (`-areanocarve`, then made permanent) closes the band at the repro spot, and the interpenetrating surfaces show **no visible z-fighting** — the deferred blended flush sorts far-to-near, and the sector (fine) draws land after the area (coarse). The original's sector VS even carries the hook for this design: `add o0.z, r0.x, c7.y` (Var_Frame_VS reg 7 — a depth nudge for the fine layer; the port leaves c7 at 0) [CODE `_hm_shaders/TD_HeightMap_vs_0.dxso`].

### What changed
- `source/world/eden_world.{h,cpp}` — the area draw now uses the **shared full-grid index buffer** (`pGridIndices`, which `loadHeightfield` already puts in the mesh); `updateAreaIndices` / `markAreasDirty` / `Area::{pIndexBuffer,NumIndices,bIndicesDirty}` are deleted (they existed only for the carve). The island layer's covered-quad carve (`updateIslandIndices`) is kept — area-vs-island interpenetration is a different pairing and has shown no artefact.
- `source/render/eden/eden_renderer.{h,cpp}` — `EdenRenderer::draw` takes a `sortBias` float, stored on `DeferredDraw` and **added** to Distance in the two far-to-near sorts: `+20000` for the island/area (coarse) draws, `-20000` for sector draws, so the coarse surface can never paint over the fine one regardless of where each mesh origin sits relative to the camera.
- `-logareas` (new debug flag): one line per terrain layer with the bound permutation, spec-match quality and the visibility-relevant render states; kept for the next terrain investigation.

### Verified
- Casino repro spot: band gone; frame diff vs the pre-fix baseline is the filled terrain (542k px changed), and a 5-shot autoplay drive from the owner's own spawn (`launch/09_Casino_Island.bat`, `-tp 2763,1457.6`) shows continuous terrain down the beach road and a correct sea floor under water.
- Ibiza hillside (`-tp 7035.8,27062.8 -heading 200` + drive): distant mountains (pure area terrain) render continuously; no seams around the streamed sectors.
- Perf: casino 15-17 ms/frame (59-66 fps) — no regression from drawing the full coarse grid.

### Ruled out (with the test that ruled it out) — the short version of a long hunt
- **The area mesh was never "invisible"** (session 10's framing). `-noareas` looked identical to baseline at the spawn only because the carved area contributed no pixel that the sectors didn't already cover from that camera; at a spot inside kept cells (`-tp 2400,480`) baseline vs `-noareas` differs by 2.2e8 sum-diff — it draws (dark navy: `ps56` lights the single colour map, no shadow slot).
- Buffer / content red herrings, all measured against a **noise floor** (two identical baseline runs differ by ~5e5 sum-diff, ~360 px): per-area index buffer vs shared grid vs freshly created buffers with identical bytes (`memcmp` + per-bind CRC all match, memory type 4 devLocal+hostVis+coherent for both); padding the buffer; single-quad draws. The probes that "worked" all drew the *full* grid (whose footprint cells' coarse silhouette covers the gap) and the probes that "failed" drew the *kept* cells — the content distinction was the bug itself, wearing a disguise.
- Alpha: `area-0-0-c` is DXT1 with zero transparent texels (decoded and counted); `ps56` outputs alpha = tex.a = 1, blend 5/6 is a no-op.
- The Z pre-pass, haze, shadows, ocean draw order: each isolated with its `-no*` flag — none changes the band (only `-noshadows` relights the scene and hides it behind the cliff shadow, which is what the owner's screenshots actually show: the band sits inside the shadowed rock).

### Not done / next
- **Owner play test** (their request): they drive casino + Ibiza with `launch/09_Casino_Island.bat` / `04_Ibiza_Ferrari_Dealership.bat` and send screenshots or a video; anything still see-through gets a fresh repro position via `-logdrive` (last `DRIVE pos` line of `build\bin\opentdu2.log`).
- The thin **terrain-vs-road cut seams** (session 17 part 12) may be the same silhouette mechanism at the road cut-outs — re-check the old screenshots' spots with this fix in.
- The cyan rectangle outline near the casino panorama (top-right in the repro shots) is a separate, pre-existing artefact — untouched.
- `c7.y` (Var_Frame_VS reg 7) is fed as 0; the original's value is [ENC]. If order artifacts ever show on interpenetrating slopes, that bias (or the sector terrain writing depth in the colour pass) is the lever.
- `updateIslandIndices` still carves area-covered quads [INF] — if holes ever show at the island/area boundary, same fix pattern applies.

## Session 2026-10-28 (twentieth, `glm`): the ray tracing settings are on the F2 page

The owner's brief: *"while claude is working and I have you in the glm version folder could you
possibly add the ray tracing settings into the F2/advanced graphic options as i didn't see any
options"*.

### What changed
- `source/game/game_options.{h,cpp}` — `ray_tracing` (default 1) and `ray_tracing_world`
  (default 0), saved in `options.ini` with the other Advanced page values.
- `source/game/advanced_graphics.{h,cpp}` — two settings rows (**Ray tracing**, **Ray traced
  world**) and one **action** row (**Run ray tracing self test**, Right runs it and the verdict
  is shown on the row). The action is not part of the row table (it has no value), so the page
  gained `kSelfTestRow` / `kNumSelectableRows`. The footer now reports what is *live*
  (`Ray tracing: active (VK_KHR_ray_query)`, `World structures: on / off`) and adds
  `Saved setting not in effect yet: restart to apply` when the saved value differs.
- `source/render/vulkan/render_device.{h,cpp}` — the `-nort` check moved out of the device:
  `setRayTracingEnabled()` / `wantsRayTracing()`, so the render layer needs no game-option
  dependency and the log says `off (Options > Advanced / -nort)`.
- `source/render/gs_render.{h,cpp}` — `IsRayTracingWorldEnabled()` (one place: the device setup
  and every model load have to agree) and `isRayTracingWorldActive()` for the readout.
- `source/render/eden/eden_model.cpp` — uses `IsRayTracingWorldEnabled()`.

### Verified
- The page: `docs/images/ray_tracing_options.webp` (F2, `-no3d`) — the rows read
  `Ray tracing  On`, `Ray traced world  Off`, `Run ray tracing self test  > run`, with
  `Ray tracing: active (VK_KHR_ray_query)` / `World structures: off` underneath.
- The action works: pressing Right on it logs `Ray tracing self test: PASSED` and
  `Advanced graphics: ray tracing self test PASSED`.
- The settings persist: setting **Ray traced world** to On and closing wrote
  `ray_tracing_world = 1` to `options.ini`, and the next run **with no command line flag**
  built the structures (`'library.3dd': 1539 of 1539 meshes have a BLAS`).
- The whole matrix: ini `ray_tracing = 0` → `Ray tracing: off (Options > Advanced / -nort)` and
  **0** BLAS lines even with `ray_tracing_world = 1`; `-nort` overrides the ini; `-rtworld`
  overrides the ini; the default path (rt on, world off) builds nothing.
- Default frame unchanged: 0.50 mean abs against the pre-RT baseline, against a 0.54 same-build
  control in the same map-screen region.

### Not done / next
- Still no pass is *lit* by the structures. The terrain heightfield is still not in them (it has
  no position stream), which is the first thing RT sun shadows need.
- The page cannot apply either setting live; that is deliberate and stated on the page rather
  than faked.

## Session 2026-10-28 (nineteenth, `win`): the ray tracing work is merged into `win`

The owner's brief: *"My usage is up now with claude if you want to merge the changes"*.

### What changed
- Merged `glm` into `win` as `2bc21f6` (4 commits: the ray tracing device gate, the
  acceleration structures, and the world's geometry in the structures — `docs/re/ray_tracing.md`
  is the spec).
- Only `docs/STATE.md` conflicted: both sides had added session entries at the top, so
  **both blocks were kept** (win's first, then the three glm entries below).
  `source/render/eden/eden_renderer.{cpp,h}` changed on **both** sides — Claude's
  pipeline-key / scene-epoch / constant-cache performance work and the ray tracing instance
  capture — and git merged them without a conflict.
- `WORKSPACES.md` updated: the merge is done, `glm` is a mirror again and needs a re-sync
  before a second agent uses it.

### Verified on the merged tree (rebuilt `build\` after the merge)
- `-rttest` with the validation layer: hit ray t 5.000, miss ray hit 0, rotated ray
  t 5.000, **PASSED**, no VUID at all.
- `-rtworld` in FREERIDE: 1925 instances, TLAS built in 0.37 ms, down ray 1.62 m, up ray
  miss, forward ray 5.43 m — the same numbers as on `glm`, so the performance merge did not
  disturb it.

### Not done / next
- The ray tracing state is in the three `glm` entries below. The first thing RT sun shadows
  need is the **terrain heightfield in the structures** (it has no position stream: one R32
  height per vertex, positions rebuilt in the shader).
- `C:\OpenTDU2-glm` sits at `96b736f` and must be re-synced before a second agent uses it.

## Session 2026-10-28 (seventeenth, `win`): retail reference attempt blocked (TDU World ignores injected input); pause menu diff list; no code changed

The owner's brief: use the running TDU World as a reference for the pause menu, car handling and graphics (computer use, TDU World window only).

### What I could and could not do
- Computer use was granted for the TDU World window (`testdrive2.exe`; the Start-menu name alone granted nothing visible, the exe basename was needed). Screenshots work: the retail game was in Ibiza in a red **Audi RS5** (DB row `CARS 'RS5'`: mass 1725, 4649 x 1860 x 1366, wheelbase 2751, track 1565/1552, cgh 20, susp 130/125, arb 25/17 [DATA, `-dumpcars`]).
- **Keyboard input does not reach the retail game.** `Escape` (key and 0.3 s hold), `W` and `Up` (1.5 s holds) after focusing the window all did nothing (the car did not move, no pause menu), although the game was running (traffic moved). The tool also reported that Claude's own window kept keyboard focus until a click in the viewport. Likely cause [INF]: the game reads DirectInput / raw input and drops synthetic events; not tested further.
- Therefore **not measured**: retail 0-100, 100-0, steady corner, hard steer, Video/Audio option pages. No retail-*.png was added. Nothing in the handling code was changed (no evidence).

### Pause menu: port vs the existing retail capture (`docs/images/retail-pause-freeride.webp`; port: `docs/images/port-pause-freeride-s17.webp`, `-no3d` so the background is black)
Differences seen, with the likely owner:
1. Retail: the icon row floats above the info card with a gap; port: the card's top edge runs **behind the icon row** and a stray red dot sits under the Close icon (935,245).
2. Retail: the selected icon (Resume) is **red** with a red frame; port: grey/white highlight, other icons washed out rather than dark-framed.
3. Retail has the objective line ("Obtain C4 license" + red dot) in the card header; the port shows none.
4. Zone badges say Hawaii 1 / Hawaii 1 in the port, Ibiza 1 / Ibiza 2 in retail (known open item, HUD priority 3).
5. Glyphs: X / B (Xbox) in retail vs star / cross (PS) in the port. [INF: the port picks the owner's DualSense glyphs on purpose.]
6. Clock 14:00 vs 12:36 and name / money / houses / cars differ: game state, not layout.
Items 1-3 come from the movie (`MAIN.vmf`) and its variables, not from `pause_menu.cpp`, so nothing was patched. Next: find which `_root` variable the retail card uses for item 2 (selected tint) and item 3 (objective text) with `-logflashvars`.

### Port numbers (California only; `-vehtest`, unchanged from session 16)
0-100 4.52 s (DB 4.10), top 309.7 km/h, 100-0 34.4 m in 3.20 s (DB `_braking_100_0` 100 / `_brakingdist` 60: the unit of the DB value is unchecked), corners 1.34-1.69 g with roll 1.7-2.3 deg. `-vehtest` always uses the California; there is no car switch, so the RS5 was not run [not done].

### Not done / next
- Retail measurements need a working input path: try the owner's DualSense / a virtual pad, or the owner drives and I read the screen.
- Graphics comparison (task 3) needs the same spot in both; not started. Retail screenshot shows: crisp long shadows, strong sky and haze, baked car grime and plate. Compared with ours at this spot: not done.

## Session 2026-10-28 (seventeenth, part 12, `win`): terrain / road seam artefacts - read from the owner's screenshots, not reproduced yet

The owner sent four screenshots (Hawaii coast road x2, Hawaii hillside road, Ibiza hillside): "mostly on casino island but this kind of stuff also happens in Ibiza".

### What the screenshots show [DATA, owner's captures]
- A **thin bright white streak and dark-teal band along the line where the road meets the verge / grass**, in all four (road edge on the beach road, a white curve on the grass far left of the road, a teal strip between the road and the slope, small white sparkles on a dirt slope). The teal is the colour of the sea.
- In the hillside shot the car body is partly **below the road surface** (clip through the road).

### Hypothesis [INF, untested]
- Cracks between the terrain (cut out around the road) and the road mesh: through the gap you see what is behind the terrain, the ocean plane (teal) or the sky / bright sun (white), and the car wheels ride the collision surface that is lower than the drawn road near the edge. To test: at a spot with the seam, `-noocean` (teal strip should vanish or turn to sky colour), `-noterrain` (the seam disappears), `-noroads` if present.

### What I did
- Tried 8 coastal and 6 road-snapped Hawaii spawns: none shows the seam, because the building spots are not on the road and the snapped ones put the car on its roof (see below). **A real position is needed**: run with `-logdrive`, drive to the seam, quit from the pause menu and send the last `DRIVE pos (x y z)` line of `build\bin\opentdu2.log`.
- New touring aid `-snaproad [radius m]` (`gm_freeride.cpp`, `startDriving`): with `-tp` moves the spawn to the nearest road triangle (-R.shk) and heads along its longest edge. Result: spawns land on roads but often on a steep shoulder / upside down (heading from the longest edge of a triangle is unreliable, the centroid height is not the lane); fine for a quick look, not for a clean repro.

### Next
- With a position: A/B `-noocean`, `-noterrain`, `-noareas`, `-nocull` at that spot and compare.

## Session 2026-10-28 (seventeenth, part 11, `win`): owner's open graphics / camera reports (4 screenshots), not yet worked

Kept here so the next session can start on any one of them. No code changed for these. Screenshots were attached to the chat (not saved in the repo).

1. **See-through geometry, Hawaii coast road** (California on the beach road): dark teal cut-out shapes in the grass / terrain at the top-left of the frame (holes through the terrain to the water / sky layer). Lead: `-noterrain`, `-noareas`, `-noinstances` A/B on the same spot; casino terrain gap work of session 10 left "one cause" open.
2. **Character hair clipping at the start** (avatar intro / house scene, spiky hair and the fringe of the waving character): hair cards poking through the head / forehead and a dark hair-card triangle. Lead: STATE issue "Hair clips through faces" (`-logfacefx`, hair cards alpha / sort order).
3. **DJ decks at the party** (start): black rectangles around the turntables and mixer on the table (and a vest / head mesh problem on the DJ). Lead: those quads look like missing or black decal / projective textures (`-logmaterials` on the deck materials; compare with a retail screenshot of the same spot).
4. **Party lighting / shadow bug that changes with a slight camera move** (the group by the pool): view-dependent flicker of the character shading. Lead: shadow cascade fitting / bias per camera (`-noshadows` A/B, `-logshadows`).
5. **Casino island: the car sometimes clips through the road.** Lead: heightfield / road collision for Casino (STATE: casino terrain streaming, session 10); needs a repro position (`-tp`).
6. **Camera-related** items the owner mentioned: not specified; ask for the exact situation.

## Session 2026-10-28 (seventeenth, part 10, `win`): pause menu - the tab row now leaves a gap above the card

The owner: "there should be a slight gap above the players card and the icons".

### Measured [DATA, retail OBS frame vs port frame at 1280x720]
- Retail: tab row bottom to card top = ~10 px gap; port: the card's top edge ran through the lower part of the icons (~-8 px). Tab row x range and clock position are identical in both; the row / tooltip are ~45 px lower in the port than retail, the card top ~38 px lower (retail's card also has an objective header strip "Obtain C4 license" that the port card does not show).

### What changed
- `source/game/pause_menu.cpp` - `kTabRowLift` = 18 stage px: once `/pause_menu/menu_sq` has settled (not playing), its matrix `TY` is lowered by 18 px (row and tooltip together), giving a ~10 px gap like retail. The matrix is not locked: when the movie moves the row itself (options screen, pause_out) the lift is dropped and re-applied when it settles at its own baseline again (`fTabRowBaseTY`). A first attempt that locked the matrix (a flag on the instance) kept the row visible on top of the options screen; reverted.
- `source/game/pause_menu.h` - `bTabRowLifted`, `fTabRowBaseTY`.

### Verified
- `docs/images/port-pause-gap-s17.webp`: (1) gap above the card, the objective red dot now sits in the card's header corner; (2) the Options screen is clean (no row left over); (3) after Back the row returns with the lift and the gear tab selected.

### Not done / next
- The row is still ~27 px lower than retail relative to the clock because the card header strip is missing; the cause (a card variant tied to an objective variable) is not found: `data_extd` frames 1-4 look the same. Selected tab still not red / enlarged (anim_on, session 6).

## Session 2026-10-28 (seventeenth, part 9, `win`): retail cornering clip (`C:\Users\cuz_c\Videos\2026-10-28 05-33-16.mp4`) - qualitative only

Audi RS5, night, driving aids **Hardcore (no ABS / TC)**, 1920x1080 @ 60 fps, 68 s; mph HUD. Not a controlled test (road with traffic, no steady circle), so **no steady-state g could be measured**.

### Read from the clip [DATA, speedometer at 0.25 s steps]
- **No visible body roll anywhere**, including a long left bend at **88 mph (142 km/h)** (46-49 s) and the slides below. The car stays flat and upright.
- **Slides do not roll the car over.** 36-38.5 s: 87 mph (140 km/h) -> 3 mph in ~2.5 s with a hard right turn: the car rotates ~90 deg on the cobbles with tyre smoke, no roll; 49.25-50 s at the end of the bend the car gets sideways at the road edge and onto the verge, no roll.
- **Speed scrub in a slide:** 83 -> 76 -> 69 -> 60 mph over 0.75 s (49.0 -> 49.75 s) = ~8 mph per 0.25 s = **~1.45 g** total deceleration while sliding. The 36.5-37.25 s drop (70 -> 26 mph in 0.75 s, ~2.7 g) includes kerb / cobble contact and is not a tyre number.

### Comparison with the port (California, `-vehtest`, flat plane)
- Port lateral limit **1.34-1.69 g**, roll max 1.7-2.3 deg, full lock at 100 km/h holds 93 km/h (1.34 g). Retail's ~1.45 g sliding decel is between those: **no evidence to move `kRollCentreFactor`** (0.25 stays), and the "no visible roll, no rollover in slides" side of the retail behaviour is what the roll-centre change gave. Different cars (RS5 vs California) and a different measure (slide decel vs steady corner), so this is consistent, not proof.
- Not compared: oversteer character of the slide (the port was not driven in a handbrake / power slide); needs a port `-autoplay` slide with `-logcorner`.

### Next
- If grip level matters: a retail steady circle on open flat ground at a fixed stick (this clip has none).
- Otherwise the handling is at the point where more evidence costs more than it returns; the owner's pick of area (HUD, pause menu gap/red tab, graphics diff) is next.

## Session 2026-10-28 (seventeenth, part 8, `win`): the PS3 build is a different revision of the vehicle classes; PS3 twin still not found

### Findings
- **PS3 and PC are not verbatim twins for the wheel / suspension code.** The PC wheel serializer (next to the wheel update FUN_008720e0) names `Gyroscope`, `ContactNormalConstraint`, `ContactTangentialConstraint`, `MaxContactGlobalNormalAngle`, `ContactGlobalPose`, `ContactNormalImpulse`, `ContactNormalImplicitImpulse`, `ContactTangentialImplicitImpulse`: **none of these strings exist in the PS3 EBOOT**. The PS3 has `GyroscopicImpulse`, `ContactConstraint`, `ContactGlobalNormal / Binormal`, `ContactImplicitImpulse`, `PreviousMotion`, `UpperStopImplicitImpulse`, `UpperStopErrorImpulse`, `UpperStopErrorCoeff`, which the PC does not have; both have `StabilizerBar{Impulse,Stiffness,...}`, `LowerStop`, `StopOffset`, `WheelHub`, `WheelJoint`. So the PS3 solver is a **different revision** (an older or newer contact / stop formulation): a PS3 law may differ from what the PC build runs, and any value read there is an [INF] for PC.
- **Correction to part 5:** the dump of "the reflection table around `0x10c5780`" was read with file offset == virtual address; addresses `>= 0x10A0000` map to file offset `va - 0x10A0000 + 0x1090000` (second PT_LOAD). The 24-byte record claim is therefore unreliable and should be disregarded; only the OPD -> slot search (which used the proper mapping) stands.
- `func_000A2640` (PS3, 149 lines) zeroes the object members at +0x100..+0x120 (the layout of part 5), a 10-sample ring buffer at +0x90 and three virtual calls on slot +0x28: it is the constructor / reset of that suspension-like object, not its force law. It has no direct caller and no OPD slot in the first image segment.

### Ruled out
- Contiguous-OPD vtable search with the PC slot signature (slot +0x00 == +0x28, triple default stubs): the only two hits are uniform tables of one stub (false positives). The PS3 vtable is not matchable by the PC slot pattern because the revisions differ.
- Offset-set scans for the stabiliser / stop members (+0x100..+0x120), now with exclusions for matrix offsets: only `func_000A2640` (above) and unrelated matrix code remain.

### Decision / next
- Reading PS3 for the suspension law is a high-cost, uncertain-payoff route given the revision difference. The cheapest evidence for `kRollCentreFactor` is a retail fixed-steer corner (60 and 100 km/h) plus the existing OBS data. Static digging remains useful for **non-suspension** encrypted laws that did not change between revisions (engine / gearbox `0x155C20`, aids `0x15E678`).

## Session 2026-10-28 (seventeenth, part 7, `win`): capstone is set up; the PC suspension vtable and the suspension update call are identified

### Tool
- `tools/pc_disasm.py` (new, `pip install capstone pefile`): disassemble the readable half of `TestDrive2.exe`, dump a vtable with code / ENC tags, find dword references. It reads the file bytes only; the SecuROM band (0x401000..0x6D1000) is not decrypted or bypassed (those bytes disassemble to garbage, by design).

### Found [CODE, PC; the targets are [ENC]]
- **Suspension class vtable = `0xf3ad6c`** (found by the spec's `0x54bc70`: `python tools/pc_disasm.py --xref-dword 0x54bc70` -> `0xf3ad90`, which is slot +0x24). Next to the brake controller vtable `0xf3ad44`. Slot map (`--vtable 0xf3ad6c 40`): +0x00 and +0x28 = `0x54bc00` [ENC], +0x24 = `0x54bc70` [ENC], +0x4c = `0x54bac0` [ENC], +0x5c `0x5466f0`, +0x64 `0x543850`, +0x90/+0x94 `0x54bdb0` / `0x54bdc0` [ENC]; the other slots are readable accessors / serializers at `0x865a20..0x866b40` and `0x866210..0x866570`.
- **The wheel update FUN_008720e0 calls the suspension code directly, not through the vtable.** At `0x872772..0x872778`: `ecx = &wheel + 0x640` (the suspension), `xmm2 = [esp+0x14]` (set from `[wheel + 0x70c]` = UpperStop, or a value from `0x54b4a0` / `0x4764b0`), `call 0x54b560`. So **FUN_0054b560 is the suspension update** (this = suspension, one float argument); `0x54b4a0` (this `&wheel+0x580`, the wheel joint) and `0x54b620` (this wheel, arg `&wheel+0x270` = tyre) are its siblings; `0x54a8b0` is called from the same function. All four are [ENC] on PC, so their PS3 twins are the target.
- Why the PS3 twin is not found yet: its layout differs (PS3 stabiliser at object +0x100 vs PC +0xB4 from the suspension base), so PC offsets cannot be used as a signature.

### Next (one step)
- Find the PS3 twin of the wheel update FUN_008720e0 (it is the caller of four consecutive suspension / joint / tyre calls and of the readable `0x871fd0` / `0x86a990`), then the callee corresponding to `0x54b560`. Method: lift candidates among the PS3 functions near `0x15xxxx` that read the PS3 suspension object (+0x100..+0x120) **and** make 3+ direct calls, filter with the scan in this session (`_scan*.py` pattern: per-function offset sets) plus "writes a float to the object and returns", or follow the PS3 constructor that calls the serializer `0xC0360`.

## Session 2026-10-28 (seventeenth, part 6, `win`): the "TDU 2 Unpacked" folder, and the 19000.0 lead is dead

### `C:\Users\cuz_c\Pictures\TDU 2 Unpacked` adds nothing [DATA]
- Its `TestDrive2.exe` has the **same SHA1** as the Steam one (`fecaead42b713f224ac01ee579e8443c625bb257`, 21,056,512 bytes): the same SecuROM-encrypted `.text`, so it does not give the encrypted band. Its data is the same game files (11,511 vs 11,479 files; the difference is case-only path spelling plus a few launcher files / videos). Nothing to switch to; `C:\TDU2-Gamedata\TestDrive2` stays the data source.

### 19000.0 is not the anti-roll default [CODE, PS3]
- `constant_pools.md` guessed `19000.0` (`0xf12134`) for the stabiliser stiffness. Scanning all 42 lifted files for a read of `0xF10000 + 0x2134` finds exactly two functions: `func_00171A38` and `func_00171D78` (`ppu_recomp_000.cpp:409282`, `ppu_recomp_009.cpp:58587`). `0x171D78` reads: allocate objects of 0x180 / 0x78 / 0x300 bytes, fill a record of floats from `0xf12110..0xf1214c` (sample defaults), then walk a collection through indirect calls (a list / graph widget fed 5 floats per row). That is a UI or debug-graph initialiser, not the suspension. **Do not use 19000.0 as an ARB value.**

### Where the static hunt stands
- Known: PS3 suspension object layout (part 5). Not known: the function that applies it. PC has no local disassembler set up in this tree (docs mention a Ghidra recipe in `docs/windows-ghidra-p0.md`), so the PC vtable route needs capstone or Ghidra first.
- Highest-yield next step for the handling: the retail fixed-steer corner clip (60 / 100 km/h, constant light stick) to judge 1.44 g vs 1.69 g, because the static route is a multi-session effort with an uncertain end.

## Session 2026-10-28 (seventeenth, part 5, `win`): suspension hunt in the PS3 image - layout found, the law not yet

### Found [DATA, PS3 EBOOT]
- Member names of the suspension / stabiliser object live at `0xFB39B8..0xFB3AD8` (`StiffnessAtRest`, `StabilizerBar{Impulse,Damping,Stiffness,Velocity,Offset}`, `LowerStop`, `UpperStop`, `UpperStopErrorCoeff`, `StopOffset`, `UpperStopImplicitImpulse`, `UpperStopErrorImpulse`, `DampingArray`). The model is **impulse based** (stop impulses, a stabiliser-bar impulse with its own stiffness / damping / velocity / offset), not a plain spring-damper force.
- Serializers: read `func_000C0360` (deserialises into the object), write `func_000BDF08`, plus `func_000CD1D4`. From `func_000C0360` the **PS3 object layout**: StabilizerBarImpulse +0x100, Damping +0x104, Stiffness +0x108, Velocity +0x10C, Offset +0x110, LowerStop +0x114, UpperStop +0x118, UpperStopErrorCoeff +0x11C, StopOffset +0x120; three vec4 at +0xD0, +0xE0, +0xF0. (Retail PC offsets differ: stabiliser +0x6f4.. inside the wheel, spec 4.)
- The reflection table around `0x10c5780` is 24-byte records `[handler OPD, 0, flags, member offset, 0, name ptr]` (the read serializer `0xC0360` is the record at `0x10c58b0`), **not** the class vtable.

### Ruled out
- Scanning all 42 lifted files for functions that touch the offset set {0x100..0x120} (reads, writes, with and without float math) returns only noise: those offsets are also 4x4 matrix / vec4 fields in dozens of Havok and math functions (`0x536CA8`, `0x5464A8` read them as matrices). An offset signature cannot find the law.
- `abs_refs.py` on the name pool / float pools: nothing (base + offset addressing, constant_pools.md Result 3).

### Next (one step)
- Get the real **vtable**: on PC (readable) take the wheel suspension constructor that stores the suspension vtable (wheel +0x640, spec line 253 area) and its PS3 twin found by the strings it uses, then read vtable slot 0x24 (PC `0x54bc70`). Or sidestep: the running 360 build (CROSSBUILD_PLAN tier 3) gives the behaviour (body roll in a fixed corner) without reading code; the retail OBS clip needs a fixed-steer corner for that.
- Until then `kRollCentreFactor = 0.25` stays a [INF] guess (1.69 g vs 1.44 g before it; see part 4).

## Session 2026-10-28 (seventeenth, part 4, `win`): PS3 read for the car physics - what it can and cannot give

The owner asked to use the PS3 build for the car physics. Bounded first pass, no code change.

### Established
- **The tyre law is readable on PC and has no load sensitivity of D.** [CODE, `vehicle_physics_spec.md` 4.2, `EvaluateTyre` in `vehicle_dynamics.cpp:97`] FUN_00869090 / FUN_00868dc0: `D = MaxFriction x Fz(kN) x FrictionScale` (linear in Fz); Fz only enters through B (BCD) and E. The CLAUDE.md item "the remaining lever is the missing load sensitivity of the Pacejka D" therefore points at nothing the original code does: do **not** add a load-dependent D to the tyre. Load sensitivity, if the original has it, comes from the friction inputs or from the load transfer (suspension / anti-roll), which is in the encrypted band. The PS3 twin of the tyre code would only repeat this.
- **What PS3 can give is the suspension force law, the anti-roll bar and the chassis solver** (spec 6.8 [ENC]: suspension vfunc 0x24 = 0x54bc70 / 0x54bac0, solver 0x544xxx). These set body roll and load transfer, i.e. exactly what my `kRollCentreFactor = 0.25` replaces with a guess.
- **Evidence the guess is not neutral:** before session 16 `-vehtest` gave 1.44 g at 100 km/h, steer 0.25 (spec 7.1 "after" column); with the roll-centre lever it is **1.69 g** (session 16 log). So the change did not only stop the rollover, it added ~17 % lateral grip. Retail comparison is still missing (the OBS clip has no fixed-steer corner).

### Ruled out / dead ends here
- `find_floats.py 57.29578` finds the pool values (0xf728a4, 0xfb0f88, 0xfb0fbc) but `abs_refs.py` has no reference to any of them (scalar pool reached as base + offset, constant_pools.md Result 3): the tyre function cannot be found by that constant.
- `func_000C78AC` (`WheelSpringJoint` serializer, lifted to `_g_c78ac.cpp`, 656 lines) is a serializer: it names members (strings 0xFB2EC8, 0xFB3B18 ... 0xFB3B68) but is not the force law.

### Not done / next (single step)
- Find the suspension class vtable in the PS3 image (the class `WheelSpringJoint`/suspension: its serializer at `0xC78AC` and the constructors that store the vtable pointer), read the vtable slot that corresponds to PC vfunc 0x24, and lift that function with `get_func.py`. Compare spring / damper / stabiliser law with `VehicleDynamics` suspension code. Then re-tune `kRollCentreFactor` only if the law shows a roll centre.
- Record a retail fixed-steer corner (60 and 100 km/h, constant light stick) so 1.44 vs 1.69 g can be judged.

## Session 2026-10-28 (seventeenth, part 3, `win`): pause tabs no longer greyed; gap / red selection still open

The owner: "there should be a slight gap above the players card and the icons ... also most look greyed out?" (retail screenshot: all 7 tabs dark and coloured, selected Resume red and larger, a gap between the tab row and the card).

### What changed
- `source/game/pause_menu.cpp` - `LockGameProgress / LockPlayers / LockSnapshot / LockEditor / LockProfiles` are now always false. They were set from `bEntryEnabled` (deliberately locked in session 6 "until their features exist"), which is why five of seven tabs were grey. Retail shows them unlocked [DATA: owner's screenshot + OBS clip]. Validating them still does nothing in code; arrow navigation now lands on them (`docs/images/port-pause-unlocked-s17.webp`).

### Verified
- Frames at 300 / 420 / 700 / 1000 of the pause menu are identical, so the missing gap and the white (not red) selected tab are **not** a slow animation: the movie's selected-state animation is not reaching its end state. [Evidence `_a.png`, not committed.]

### Not done / next
- Gap between tab row and card, enlarged red selected tab: the same unresolved item as session 6 ("selected square renders black, retail red: `anim_on` sprite 18524 ... needs its internal timeline driven"). Start at `-logpause` and the `anim_on` placement.
- Owner question: can the PS3 build help the car physics? Yes, see the reply; not started.

## Session 2026-10-28 (seventeenth, part 2, `win`): Audio & Video now lists Image settings (Gamma) and Stereo like retail

### What changed
- `source/game/pause_menu.cpp` (init, next to `LockLanguage`) - `/:GammaEnabled` true, `/:LockGamma` false, `/:is3DAllowed` true, `/:LockRelief3d` true. [DATA, MAIN.vmf `set square menu GAMMA`: pushes `gamma` on `GammaEnabled`, `relief3d` on `is3DAllowed`; `LockMode3d` / `LockResolution3d` guard the stereo rows.] Stereo 3D is not implemented, so its icon is greyed [INF; retail has it unlocked].

### Verified
- `-autoplay "200:press=pause;415..475:press=right x5;520:press=validate;710..750:press=down x4;800:press=validate;..."`: four icons (`docs/images/port-options-audiovideo-4icons.webp`); the third opens the "Image settings" page with the Gamma slider, retail text and TDU2 logo (`port-options-gamma.png`, compare `retail-options-gamma.png`). Left x3 moved the slider and the log shows `options gamma 'gams:slid:updt': 9 / 8 / 7 (post gamma 0.83 / 0.86 / 0.89)`. So the pause-menu gamma handler is now tested through the UI (it was not before).
- The old session 14 claim "Gamma page not reachable from the PC menus" is wrong: it was only these two flags.

### Not verified / next
- OK / Cancel persistence of the gamma value through `options.ini` via the page (only the live slider updates were checked).
- Stereo page (`opts:...` `stmo`, `/:mode3d`) is not answered; it is locked. Retail's Gamma default and slider-to-gamma curve are unknown [INF in `GetPostGamma`].
- Still open from part 1: `-vehtest` 100-0 distance (34.4 m, should be ~43 m); cornering clips.

## Session 2026-10-28 (seventeenth, addendum `win`): the owner's OBS recording gives retail numbers (`C:\Users\cuz_c\Videos\2026-10-28 04-56-54.mp4`, 1920x1080, 60 fps, 56.8 s, car = **Audi RS5**, HUD in **mph**)

Frames were read with OpenCV (`pip install opencv-python`; contact sheets, then 0.25 s speedometer crops). Retail frames saved: `docs/images/retail-pause-tab-progress.webp`, `retail-options-audiovideo.webp`, `retail-options-gamma.png`, `retail-options-video-opening.png`; port: `port-options-audiovideo-s17.png`.

### Handling [DATA: speedometer digits, 0.25 s steps; accuracy about +-0.1 s]
- **0-100 km/h (62.1 mph): about 4.6 s** (first movement ~22.75 s, 62 mph at ~27.35 s; ~19 mph/s in 1st, a ~0.25 s flat at 40 mph = the 1-2 shift). Port California 4.52 s (DB 4.10) - same order; the RS5 itself is not run in the port.
- **100-0: 3.05 s** (62 mph at 29.55 s, 0 at ~32.6 s). Deceleration in the middle of the stop 7 mph / 0.25 s = **~1.27 g**. Port `-vehtest` brake: 10.8 km/h / 0.25 s = **~1.22 g**, 100-0 in 3.20 s. **Braking is within ~4 % - leave it.**
- **Port bug candidate:** `-vehtest` reports 100-0 as **34.4 m**, but 100 km/h to 0 in 3.2 s needs >= ~44 m (retail estimate ~43 m from the speed curve). The distance accumulation or its start point is wrong; check `gm_freeride.cpp` near line 317. [INF]
- Cornering / hard steer: the recording is a winding road, not a controlled test. Seen: small visible body roll in the 40-47 s bends at 35-55 mph (no rollover-like lean), and at ~49.5 s (30 mph, hard turn) the rear steps out and the car slides, no spin, no roll. Not enough for a number; `kRollCentreFactor` left at 0.25. A fixed-steering-angle clip (see below) is still needed.

### Pause menu / options: new facts
1. **The retail PC "Audio & Video" page has FOUR entries**: Audio, Video, **Image settings (the Gamma page)** and **Stereo (3D)**. The port shows only Audio and Video. This corrects the session 14 line "the Gamma page is not reachable from the PC menus": retail reaches it (`retail-options-gamma.png`: title "Image settings", a Gamma slider, text "Adjust the Gamma ... make sure the TDU2 logo is barely visible", logo block below). Our `opts:gama:` handler (pause_menu.cpp ~679) therefore matters. Next: find which `_root` variable / Lock flag shows the 3rd and 4th icons in the `audiovideo` clip (I did not find it; my search of MAIN.vmf strings failed) and set it in `PauseMenu` init (~line 434-446 area, next to `/:WhatPlatform`).
2. Retail Video page lists "Resolution 3840x2160" first; the frame caught it mid-animation, so the other rows are not read.
3. Retail pause: the selected tab is a **red** raised button with a bubble ("Progress & Objectives" / "Resume"); unselected tabs are dark. The card has the objective line top right. The world behind is dimmed, not black (our `-no3d` shot is black by construction). Retail shows 7 tabs + Close with a gap above the card, as before.

### Next
- Gamma + Stereo icons on Audio & Video (above). Then the 100-0 distance bug. For cornering, record: constant 60 km/h with a fixed light stick deflection on a flat curve, and a 100 km/h full-lock flick on the empty straight (the straight in the first clips works).

## Session 2026-10-28 (sixteenth, `win`): draw submission 48 -> 33 ms, and the car no longer rolls over in slides

The owner's brief: *"fix some graphical issues, get performance better, make the car physics a lot closer to the original"*.

### What changed
- `source/render/eden/eden_renderer.cpp` (`recordDraw`, `fillConstants`, `getPipeline`), `eden_renderer.h`,
  `eden_shader_system.h`, `shaders/dxso_translator.h`: measured with temporary timers (removed again):
  per draw `fillConstants` was 30 of 55 ms and `getPipeline` 10 ms. Fixes: pipeline cache key is a plain
  6-word struct instead of a formatted string (10 -> 0.6 ms); per-member caches in `ShaderConstantMember`
  (material parameter hash, "is this a fixed name" flag, scene-map lookup per `beginScene` epoch); empty-map
  guards; constant buffers (write-combined memory) are cleared only up to the highest register the shader's
  table names (`EdenProgram::VS/PSUsedRegisters`) instead of ~8 KB per draw.
- `source/physics/vehicle_dynamics.cpp` - the lateral tyre force now acts at a lever shortened by
  `kRollCentreFactor` (0.25) of the CoM height [INF: the suspension geometry is ENC].

### Verified
- Opening scene: avg frame 52 -> 35 ms (19 -> 29 fps); offscreen pass 48 -> 33 ms. Screenshot at frame 480
  with `-fixedtimestep` is **pixel identical** before and after (mean abs diff 0.0).
- `-vehtest` (California): the two FLIPPED cornering cases (100 km/h, steer 0.25 and 0.5, roll 87-90 deg) now
  hold speed with roll max 2.3 deg; all six cases are clean (roll 1.7-2.3 deg, 1.34-1.69 g). 0-100 4.52 s,
  top 309.7 km/h, braking unchanged.

### Ruled out
- Caching the material parameter hash alone: 48.2 -> 47.9 ms, not the cost; the cost was map lookups, the
  string chain and the constant-buffer clears.

### Not done / next
- Remaining frame cost: sun shadows ~13 ms and ocean reflection ~12 ms (both re-submit the world; `-noshadows`
  / `-noocean` A/B), main pass ~20 ms. Next: cull the shadow / reflection passes harder or reuse them.
- Physics: only the California was tested. 1.69 g at 100 km/h is high. The roll-centre factor is a chosen
  law, not the original's; compare against the 360 oracle (CROSSBUILD_PLAN tier 3). Other cars untested.
- Graphical issues: not attempted this session (nothing specific named); owner please say which.
## Session 2026-10-28 (eighteenth, `glm`): ray tracing step 3 — the world's own geometry in the structures

The owner's brief: *"I think claude fixed some of the games performance ... I will let you
continue please"*.

### What changed
- `source/render/eden/eden_model.{h,cpp}` — a BLAS per mesh at load, when `-rtworld` is on:
  the meshes are sub ranges of the model's two shared buffers, so the address of a stream is
  the buffer device address plus the stream offset and the indices start at `FirstIndex`.
  The position attribute and its format come from the mesh's own attributes
  (`GetBlasPositionFormat`); a mesh with no usable position stream is skipped and counted.
  The vertex / index buffers only get the build input usage under `-rtworld`, so a default
  run allocates nothing new.
- `source/render/eden/eden_renderer.{h,cpp}` — `EdenMesh::BlasAddress`; the frame's drawn
  instances (`RtInstance`, captured in `recordDraw`) plus the camera of the scene that
  produced them; `setRtInstanceCapture` / `clearRtInstances`.
- `source/render/gs_render.{h,cpp}` — `-rtworld`: clears the instance list once per frame,
  builds the TLAS in `endFrame`, traces three probe rays from the camera and logs the
  instance count, the TLAS size, the build time and the hits.
- `source/render/rt/ray_tracing.{h,cpp}` — the structures are now registered in the
  `RayTracing` object, so it frees anything a caller still holds if it is destroyed first
  (a model can be unloaded after the renderer).

### Verified
- `-rtworld` in FREERIDE (Ibiza), with `-vk_enable_validation_layer`: **no VUID / error /
  warning at all** with ~2000 BLASes, a TLAS per frame and the probe dispatches.
- `Ray tracing: 'library.3dd': 1539 of 1539 meshes have a BLAS (0 skipped)` — every model
  mesh in the sector test, both position formats.
- `Ray tracing world: 1925 instances, TLAS 2631 KB, built in 0.53 ms, camera (36014.5, 7.0, 5816.5)`,
  then `down ray -> hit 1, t 1.62 m`, `up ray -> hit 0`, `forward ray -> hit 1, t 5.43 m,
  instance 0, primitive 34`. Self consistent (the camera is 1.62 m above the car, nothing
  above it, a mesh 5.43 m ahead) and stable frame to frame, at a real world position.
- **Cost**: `-rtworld` adds +2.2 s, and the *same* +2.2 s at 25 frames as at 60, so it is the
  one-off BLAS build at load, not per frame. The per-frame TLAS build is 0.5-0.6 ms.
- Default run (no `-rtworld`) is unchanged: no BLAS, no capture, frame diff 0.50 against a
  0.54 same-build control.

### Ruled out (with the test that ruled it out)
- An empty instance list was **not** a missing flag or a missing BLAS: instrumenting the gate
  showed the draws had valid addresses but were tagged `reflection 1`. Cause: `beginScene` is
  called **many times per frame** (environment cube, 3 shadow slices, ocean mirror, planar
  reflections, post FX), so clearing the list there wiped every capture. Clearing it in
  `GSRender::beginFrame` fixed it.
- The zeroed camera was **not** a missing `CameraPosition`: `getSceneParams()` returns the
  *last* scene, which is a post FX pass. The camera is now taken from the scene that produced
  the instances.

### Not done / next
- **The terrain heightfield is not in the structures** and this is the biggest remaining item:
  `eden_world.cpp` `loadHeightfield` keeps one R32 height per vertex and the shader rebuilds
  the positions, so there is no position stream a BLAS can read
  (`VAF_POSITION_R32_SFLOAT` -> skipped). A downward ray therefore hits only model geometry.
  Fix by generating an XYZ position buffer from the heights (they are already there) or by
  per-cell AABBs (`VK_GEOMETRY_TYPE_AABBS_KHR`).
- Then RT sun shadows (step 4 of `docs/re/ray_tracing.md`) — and it needs the terrain, or the
  shadows are wrong wherever the ground should be the occluder.
- The probes still each do their own submit + fence wait; when a real pass consumes the
  structures they should share the frame's command buffer.

## Session 2026-10-28 (seventeenth, `glm`): ray tracing step 2 — the acceleration structures, and a ray really traced

The owner's brief: *"you can continue on"* (after step 1, the device gate).

### What changed
- `source/render/rt/ray_tracing.{h,cpp}` (new) — `RayTracing`: one BLAS per triangle range
  (a sub range of a shared vertex / index buffer, which is how `EdenModel` stores its
  primitives), a TLAS from an instance list, both built on the GPU through a one time
  submit, plus a ray query compute probe and the self test.
- `resources/shaders/rt_probe.comp` (new) + `builtin/rt_probe_comp.h` — the first compute
  shader in the project and the first ray query.
- `tools/compile_shaders.py` — handles `.comp` now, and takes file names so one shader can
  be rebuilt (with no arguments it rewrites every header, which the previous session
  flagged as a trap). `.comp` uses `--target-env spirv1.4`: **`vulkan1.2` would emit
  SPIR-V 1.5, which the Vulkan 1.1 device rejects**, and `vulkan1.1` cannot express a ray
  query at all.
- `source/render/vulkan/render_device.{h,cpp}` — `getBufferDeviceAddress`,
  `createDeviceBuffer` (device local: AS storage and scratch), `createReadbackBuffer`,
  `writeBuffer`, `readBuffer`, `executeImmediate`, the scratch alignment query, and
  **`VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`** (VMA refuses a buffer with
  `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` without it).
- `source/render/gs_render.{h,cpp}` — owns the `RayTracing` object; `-rttest`.

### Verified
- `-rttest` with `-vk_enable_validation_layer`: `hit ray -> hit 1, t 5.000`,
  `miss ray -> hit 0`, `rotated ray -> hit 1, t 5.000`, `PASSED`, and **no VUID / error /
  warning in the log at all**. The distances are exact, not approximate.
- The rotated ray is the load bearing one: it only hits if the instance transform
  conversion is right. `VkTransformMatrixKHR` is row major with the translation in the
  last column, the Eden `Matrix4x4f` is column major (`m[column][row]`), so the
  conversion is the transpose `vk.matrix[i][j] = eden.m[j][i]`; a wrong transpose turns
  the rotation into its inverse and this ray misses.
- `Ray tracing limits: scratch alignment 128, max primitives 536870911` — the scratch
  offset is aligned from the queried property, not assumed.
- `-nort -rttest` skips the test (`Ray tracing: disabled (-nort)`), and a plain run
  (no `-rttest`) traces nothing.
- Default frame unchanged: 0.38 mean abs against the pre-change build, against a 0.54
  same-build control in the same map-screen region.

### Ruled out
- Not an extension / feature problem, not a VMA flag problem, not an alignment problem,
  not a descriptor problem, not a shader problem: the self test exercises all of them and
  passes with the validation layer silent.

### Not done / next
- **Real geometry is not wired up**: `EdenModel::load` creates no BLASes and nothing calls
  `buildTlas` per frame. Next: add `RayTracing::getBuildInputUsage()` to the model's
  vertex / index buffer usage, build one BLAS per `EdenMesh` (they are sub ranges: pass
  the buffer device address plus the stream offset and `FirstIndex`), then a TLAS per
  frame from `EdenRenderer::deferredDraws`, and trace a ray from the camera to log the hit
  distance. Watch the two position formats (`VAF_POSITION_R32G32B32_SFLOAT`,
  `VAF_POSITION_R16G16B16A16_SFLOAT`) and the device address alignment rules.
- Then RT sun shadows (step 3 of `docs/re/ray_tracing.md`), the only part with a measured
  reason to exist.

## Session 2026-10-28 (sixteenth, `glm`): ray tracing step 1 — the device gate

The owner's brief: *"while claude is working would ray tracing/path tracing be a possibility?"*
and then *"start on this but in the opentdu2 GLM and you can merge all changes from this
directory"*. So: the analysis, the first step of it, and this workspace resynced to `win`.

### What changed
- `docs/re/ray_tracing.md` (new) — the analysis. What the renderer already gives a ray
  traced pass (full resolution depth already sampled, upload-once meshes -> one BLAS
  per mesh, CPU side instance transforms, and the `pShadowRealTimeOverride` /
  `ProjectiveSlot` hooks that let a ray traced buffer feed the **unmodified** Eden
  shaders), what blocks it (DXSO cannot emit rays, no G-buffer, skinned meshes skin in
  the vertex shader, MoltenVK has no RT), why RT sun shadows are the only part with a
  measured reason to exist (the frame is CPU bound and `-noshadows` is ~22 ms of CPU),
  and the order of work. Path tracing: not planned, with the reasons.
- `source/render/vulkan/render_device.{h,cpp}` — `supportsRayTracing()`. The extension
  chain is enabled only when the driver reports **all** of it, and `-nort` forces it off.
  **`VK_KHR_spirv_1_4` requires `VK_KHR_shader_float_controls`** — the app asks for
  Vulkan 1.1, so the 1.2 features come in through their KHR aliases
  (`acceleration_structure`, `ray_query`, `spirv_1_4`, `shader_float_controls`,
  `buffer_device_address`, plus `deferred_host_operations` when present, which is an
  interaction rather than a requirement). The device feature chain is now built by a
  small head/tail `VkBaseOutStructure` helper, so `createInfo.pNext` still serves the
  DXSO case.
- `source/game/advanced_graphics.cpp` — a read-only "Ray tracing:" readout. No option
  row yet: the row arrives with the pass that has something to switch.

### Verified
- `Using GPU: NVIDIA GeForce RTX 5070 Ti`, `Ray tracing: available (VK_KHR_ray_query)`;
  `-nort` -> `Ray tracing: disabled (-nort)`.
- **The chain is legal**: with `-vk_enable_validation_layer` the log has no VUID /
  error / warning at all, and the layer really loads
  (`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` + `VK_LOADER_DEBUG=layer` ->
  `Loading layer library ...VkLayer_khronos_validation.dll`, exit 0). This is the
  evidence that the extension set is right — a missing dependency would have failed
  `vkCreateDevice` outright.
- Frame unchanged: default run against the pre-change build is 0.36 mean abs per
  channel, and the **control** (the same build run twice) is 0.54 in the same region,
  so that difference is pre-existing map-screen Flash nondeterminism and not this
  change. Region: x 78..542, y 60..141 at 1280x720.
- `docs/images/ray_tracing_capability.webp` — the readout on the page itself
  (`Ray tracing: available (no pass yet)`), F2 with `-no3d`.

### Not done / next
- **No ray is traced yet.** Next is step 2 of the doc: acceleration structures — one
  BLAS per `EdenMesh` (they are upload-once, so one build each), a TLAS per frame from
  `EdenRenderer::deferredDraws`, with `VK_KHR_deferred_host_operations` so the build
  stays off the CPU path. Start in `render_device.cpp`: `GPUBuffer` needs
  `VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR` + `SHADER_DEVICE_ADDRESS`.
- `tools/compile_shaders.py` is pinned to `--target-env vulkan1.1`; a ray query module
  needs SPIR-V 1.4. Fix it with the first `.comp` shader.
- **This workspace is active again** (see WORKSPACES.md): `glm` was fast-forwarded to
  `win` at `1abc31c` (identical tree, empty diff) and now carries the ray tracing work,
  while `C:\OpenTDU2` (`win`) stays Claude's. Merge direction when this is ready:
  `glm` -> `win`.

## Session 2026-10-28 (fifteenth, `win`): the Advanced graphics page (render scale, upscale filter, sharpening, FXAA-like, effect toggles, fps limit)

The owner's brief: *"add a new page advanced ... implement as much as possible ... at some point look at FSR, DLSS"*.

### What changed
- `source/game/advanced_graphics.{h,cpp}` (new) � native ImGui page, opened from the Video page with
  **Y / Triangle** (hint drawn there) or **F2** anywhere (in the world the game keeps taking keys too).
  Saved in `options.ini`. Design, FSR / DLSS requirements and the order of work: `docs/re/advanced_graphics.md`.
- `resources/shaders/upscale.frag` (+ `builtin/upscale_frag.h`, built with `tools/compile_shaders.py` � it
  rewrites the other headers with LF; revert those, only the new one is needed).
- `source/render/eden/eden_scene_view.cpp` � scene targets sized by `GetRenderScale()`; final pass uses the
  upscale shader unless everything is default (then the old blit, frame identical); the `-no*` effect flags
  are also runtime options.
- `source/tdu_instance.cpp` � frame rate limiter, `-autoplay opt2`, page drawn after the mode's UI.
- `source/game/pause_menu.cpp` � Y on the Video page opens the page, movie input is paused while it is up.

### Verified
- `options.ini` variants, frame diff vs default (mean abs, /255): scale 50 12.2, 50 + bicubic + sharpen +
  edge smoothing 12.4, 150 6.7, effects off 32.3 (visibly: no grading / haze / flare / shadows).
- F2 and the Video-page Y both open the page; Left/Right changed `upscaler` / `sharpness`, closing saved them.
- **Render scale gives no speed-up** (52.1 / 50.8 / 50.3 ms at 100 / 50 / 150 %): not pixel bound.

### Not done / next
- FSR 1.0 not added (needs AMD's MIT headers vendored: owner's call); FSR 2/3 and DLSS need jitter + motion
  vectors first (see the doc). The FXAA-like pass is simple; a real FXAA / SMAA would be better.
- Pad nav of the page uses the menu actions; mouse is not supported. Shadow resolution is not a live option.
- First step for speed: find where the 50 ms go (draw submission), then re-measure render scale.

## Session 2026-10-28 (fourteenth, `win`): the Video options page works (resolution and VSync live, quality levels saved)

The owner's brief: *"work on the video settings in the game"*. Options > Audio & Video > Video
(`opts:vide:`) opened with eight labelled rows and **empty values**; the game answered nothing.

**Reproduced** (before): `docs/images/` none; the rows showed empty boxes.

    build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata ^
        -gm FREERIDE -skipcine -no3d -nopad -fixedtimestep -autoplay "200:press=pause;415:press=right;430:press=right;445:press=right;460:press=right;475:press=right;520:press=validate;700:press=down;715:press=down;730:press=down;745:press=down;810:press=validate;1000:press=down;1030:press=validate;1300:shot=C:/OpenTDU2/_v.ppm" -exitframe 1310

### Protocol [DATA, MAIN.vmf options/video + choicebox]
- Rows `[clip, label, 'choicebox', variable, cmd, lock]`: Resolution `prrs` `/:resolution`, Refresh
  rate `prfr` `/:frequency` (`/:LockFrequency`), VSync `vsyn` `/:VSync` (`/:LockVSync`), Anti-aliasing
  `praa` `/:anti_alias`, Environment `prlo` `/:lod`, Car and opponent `caop` `/:car_opponent`,
  Texture filtering `txfl` `/:texture`, FX `fxqu` `/:fx_qual`.
- A **choice box shows the text of its variable** (`updateText`: `param_txt = _root[VarToFocus]`).
  Left / Right fire `opts:vide:<cmd>:scpr` / `:scne`, then re-read the variable: the game owns the value list.
- `A()` (OK) sends **only `opts:vide:test`**, then reads `/:IsVideoChanged` / `/:IsVideoChangeOk`:
  changed raises a 'keep these settings?' dialog (`change_video`, 10 s timer), unchanged does nothing.
  There is no `vali` from A() on this page. `B()` sends `opts:vide:quit` and closes the page itself.

### What changed
- `source/game/pause_menu.cpp` � `onVideoOptionsCommand`: values on `open`, step on `scpr`/`scne`, `canc`
  restores, `test` = keep + save + send Back (B()) from the next tick (`bPendingVideoClose`). No
  confirmation dialog [INF: changes apply live and Cancel reverts, the dialog needs DBXO input].
- `source/game/game_options.{h,cpp}` � video fields in `options.ini`, `GetVideoResolutions()` (SDL display
  modes >= 800x600 plus the current one), `GetDisplayRefreshRate()`, `ApplyVideoOptions()`.
- `source/config/gs_config.{h,cpp}` � loads `options.ini` before the window exists (saved size / VSync /
  environment level; command line still wins), `setEnvironmentQuality()` recomputes `GlobalLodFactor`.
- `source/render/vulkan/render_device.h` � `setVSync()` (rebuilds the swapchain), `getWindow()`.
- `source/flash/flash_localization.cpp` � `-loclike <text>`: every movie string whose name contains it.

### Verified
- Resolution Left: `1280x720 -> 1176x664`, the window and the swapchain really resize, the whole UI scales
  (`docs/images/options_video_ok_back.webp`). OK saves `options.ini` (`video_width = 1176` ...) and returns to
  Audio & Video. Next start: `Video: 1176x664 windowed`, swapchain created at 1176x664.
- All eight rows show retail-localized text (`docs/images/options_video_page.webp`); the locked Refresh row is
  greyed and skipped by Up/Down (shows the display's rate).

### Addendum (same session): texture filtering is real, gamma has a hook

- **Texture filtering row -> sampler anisotropy**: Low = trilinear, Medium = 4x, High = 16x (clamped to
  the device limit). `RenderDevice::setMaxAnisotropy` (the sampler cache key includes the level, old handles stay
  valid), `EdenRenderer::refreshSamplers()` rewrites the static sampler descriptor set (`fillSamplerHeap` /
  `writeSamplerSet`). `ApplyVideoOptions()` also runs at start-up. Evidence: `docs/images/aniso_low.webp` vs
  `aniso_high.png` (cobblestones; mean abs diff 4.1 / 255). **The default is now High (16x), so the default
  look changed** (sharper at grazing angles); Gamma-default frame is pixel-identical to before.
- **Gamma**: `EdenPostFx` LUT pass `Gamma` constant (was a hardcoded 0.8) now comes from
  `GetPostGamma()` = 1.1 - 0.6 * slider/20 [INF: retail curve unknown; middle = the old 0.8].
  `opts:gama:` is answered (`/:gamma`, `gams:slid:updt`), BUT **the Gamma page is not reachable from the PC
  menus** (Audio & Video lists only Audio and Video; it looks console-only). Verified through `options.ini`
  (`gamma = 20` mean luminance 95 -> 128, `gamma = 0` -> 74; `gamma_20.png` / `gamma_0.png`). The pause-menu
  handler is therefore untested through the UI.

### Not done / next
- **Applied only as a saved number**: Anti-aliasing (no MSAA in the renderer), Car/opponent, Texture
  filtering, FX quality (no tiers exist). Environment quality is live for LOD / sector count, but already
  loaded content is not reloaded. (texture filtering is done, see above).
- No fullscreen toggle (the retail page has none either) and no 'keep these settings?' countdown.
- `Gamma` page (`opts:gama:`, `/:gamma` slider) is the next video-related page; same recipe.
- The Display and Game settings pages still answer nothing (`opts:disp:` / `opts:gase:`).

## Session 2026-10-28 (thirteenth, `win`): the options screen works — the movie now drives its own menus; the Audio page is live

The owner's ask: *"Options pages"* (priority 2).

**Reproduced** (before): validating Options showed a gear and one wrong icon,
no column, no labels, the pause panel still up (`docs/images/pause_menu_options.webp`).

    build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata ^
        -gm FREERIDE -skipcine -no3d -nopad -fixedtimestep ^
        -autoplay "200:press=pause;415:press=right;430:press=right;445:press=right;460:press=right;475:press=right;520:press=validate;690:shot=C:/OpenTDU2/_o.ppm" -exitframe 700

### The finding that changed the approach

With the session-12 runtime fixes plus the ones below, **MAIN.vmf's own menu
scripts run end to end** — `openMenu -> setSquareMenu -> openMenuH ->
ActionLoop... -> lightSquare(1)`, and on Options `ValidMenu -> closeMenu ->
LaunchOptions -> options.setSquareMenu`. Earlier sessions concluded the movie's
glue was "unusable" and rebuilt it in C++ (`refreshEntries` / `layoutEntries`);
that was the runtime bugs, not the movie. `-logflashcalls` (new) shows the
whole call chain. So the game now **only routes input and answers commands**;
the C++ square driving stays as the fallback when `menu_sq.isMenuH` never
turns true.

**Input protocol [DATA, MAIN.vmf]**: the menus own 201 `on(keyPress)` buttons
(`-logpausekeys`): Up/Down/Left/Right -> the owning menu's `Move('up'...)`,
Enter -> `A()`, Backspace -> `B()`, `x`/`y` -> `X()`/`Y()`; each checks
`_root.Input` itself. The PS3 exe has **no `Move` string** (strings.json), so
the exe sends keys, not calls. `PauseMenu::tickMovieInput` sends SWF key codes
(Left 1, Right 2, Backspace 8, Enter 13, Up 14, Down 15, 'x' 120, 'y' 121);
Back on the entry row closes the menu (the game owns resume).

**Options page protocol [DATA, MAIN.vmf]**: `ValidMenu` in options does
`options.gotoAndPlay(BtValid)` (`language`, `controls`, `game_settings`,
`display`, `audiovideo`, then `audio` / `video` / `gamma`...). A page's
`initParameters` fires `opts:<page>:open` **before** it reads its values (the
game answers synchronously from the FSCommand callback), builds
`setParam[i] = [clip, label, type, variable, command, mute]`, and calls
`initSlider(cmd, value, 20, ...)` / `initChoiceBox`. A slider writes its value
back to the variable, then fires `<cmd>:slid:updt`; A() fires
`opts:<page>:vali`, B() `opts:<page>:canc`. Audio (`opts:audi:`):

| row | label | variable | cmd |
|---|---|---|---|
| Master volume | `m_MasterV_` | `/:master_volume` | `mavo` |
| Music | `m_music_` | `/:music_volume` | `muvo` |
| Vehicles | `m_Vehicles_` | `/:vehicules_volume` | `vevo` |
| Voices | `m_voices_` | `/:voices_volume` | `vovo` |
| SFX | `m_GeneralSFX_` | `/:sfx_volume` | `fxvo` |
| Micro (choice box) | `m_Mike_` | `/:mike` | `mike` |
| Voices IP | `m_Voices_IP` | `/:voiceIP_volume` | `voip` |

The sliders map one-to-one onto **audiosetup.xmb mixers** (`-logaudiomixers`,
new): `Master` (Radio + every group), `Music`, `Vehicles`, `Voices`,
`VoicePlayers`, `SFX` [DATA].

### What changed
- `source/flash/flash_player.{h,cpp}` — AVM1 runtime, all general:
  1. **Arrays**: `push/pop/shift/unshift/join/slice`, an index write grows
     `length` (`Object::bArray`). The menus build their lists with
     `new Array()` + `push`.
  2. **`resolveTarget` turned `..` into `//`** (its dot->slash conversion), so
     `../:x_pos` read the **root**: every square of `openMenuH` landed at x 0
     and `lightSquare`'s `../:frameToPlay` never lit the selection.
  3. **A script-set transform survives the parent timeline**
     (`Instance::bTransformedByScript`, set by `_x/_y/_xscale/_yscale/_alpha`;
     the Flash rule, Ruffle's `transformed_by_script`). Before, `menu_sq`'s own
     timeline moves undid `openMenuH`'s layout every frame — which is why the
     C++ `layoutEntries` "re-imposes the row every frame".
  4. **Text-field variables resolve from the owning clip** (`pDrawOwner`), not
     the root: the square tooltips bind `../:txt`.
  5. **Identifiers are case-insensitive** (`Clip::Variables` / `globals` use a
     case-insensitive map), as in AVM1 up to SWF 6: the movie defines
     `getSquarePath` and calls `GetSquarePath` (every tooltip read the last
     label), and reads `m_music_` / `m_voices_` for the database's `m_Music_` /
     `m_Voices_` (two blank audio rows).
  6. `invokeMethod` / `getClipVariable` (call a method on the clip a variable
     holds), `-logflashtrace` (the movies' own `trace()`), `-logflashcalls`
     (every user function call: name, clip, arguments).
- `source/game/pause_menu.{h,cpp}` — movie-driven mode (`bMovieDriven`,
  `tickMovieInput`), `onOptionsCommand` (the Audio page: values in on `open`,
  read back and applied live on every update, saved on `vali`, restored on
  `canc`). MAIN.vmf's `SetBDD()` is invoked after load and `sbdd` applies the
  localization (the pause menu never had its `m_*` strings: "Options" title,
  Details / Back / Quit, square names, "Inglorious"). `/:WhatGameMode` is now
  **1** (the movie's free-roam list; 2 builds the race list restart /
  quit_race — the old 2 was a guess that only mattered once the scripts ran).
  `/:LockExitGame` is set (the list reads it; the code wrote `LockQuit` only).
  `/:LockLanguage` true (retail shows it locked). Diagnostics:
  `-logpausekeys`, `-logoptpage`, `-logpausesq`.
- `source/game/game_options.{h,cpp}` (new) — `GameOptions` (volumes 0..20,
  default 20 = the mix as it was [INF]), `<userdata>/options.ini`,
  `ApplyAudioOptions` (linear 0..20 -> gain [INF: the retail curve is not
  identified]); loaded and applied at startup (`tdu_instance.cpp`).
- `source/audio/gs_audio.{h,cpp}` — `setMixerVolume(name, gain)`: a gain per
  mixer, multiplied down the mixer tree into a per-client gain in
  `computeVoice`; plain-stereo voices take Master.
- `source/game/freeride/gm_freeride.cpp` — the HUD is not drawn while the pause
  menu is open (the retail pause / options shots show no HUD) [DATA].
- `source/flash/flash_localization.cpp` — `-logloc <name>`: a string's text and
  categories (how the m_music_ case mismatch was found).

### Verified (all with `-fixedtimestep`)
- Pause row laid out and animated by the movie, free-roam icons, a tooltip per
  square ("Options", "Challenge Editor" with its padlock) —
  `docs/images/pause_row_movie_driven.webp`.
- Options: title, the five categories in a column, tooltip, Quit bar; side by
  side with retail in `docs/images/options_vs_retail.webp`
  (`options_categories.png`). Back from Options returns to the row with
  Options still selected (`opou`, then the row's `mopn`).
- Audio page: all seven labelled rows, values from the game; three `left`s log
  `options audio 'mavo:slid:updt': master 19 / 18 / 17`, OK logs
  `GameOptions: saved ...options.ini` (`master_volume = 17`) —
  `options_audio_page.png`, `options_audio_master_slider.png`.
- Regressions against the session-12 shots: free-ride HUD 0.015, in-car menu
  0.014, radio identical once its fade-in finishes, the LOGIN front end renders
  with its dialog. **Changed**: the world map's filter bar now shows six
  different category icons with the selection enlarged (it was six identical
  flags) and its LB/RB hints are gone — most likely the map's own scripts now
  run; not compared with retail yet.

### Ruled out
- `_root.Clip.Move()` as the input path: the options screen registers
  `_root.Clip = options`, which has no `Move`; the menus take keys through
  their buttons, and the exe has no `Move` string.
- A missing loc string for Music / Voices: `-logloc m_music_` finds `m_Music_`
  ('Music', categories hud main) — a case mismatch, not a missing string.

### Not done / next
- **The other pages answer nothing yet** (`PauseMenu: options command
  'opts:disp:open' not handled yet` in the log). Same recipe as Audio: open the
  page with `-logoptpage`, read its `initParameters` table (variables,
  commands, slider max), answer `opts:<page>:open` in
  `PauseMenu::onOptionsCommand` (`pause_menu.cpp`). Video is the obvious next
  one (`/:resolution`, `/:VSync`, `/:gamma`... fed from `VideoSettings`).
- The **Micro** choice box shows "0": `initChoiceBox(cmd, var, small)` wants a
  list of choices — read it in the audio page dump.
- The world map filter-bar change above: compare with a retail map shot.
- The C++ fallback square driving (`refreshEntries` / `layoutEntries` /
  `kEntries` icX/icY) is dead code while the movie drives; remove it once the
  owner has played the movie path on a real pad.
- Mouse: `tickMovieInput` only sends keys; the squares' rollover / `Clic(id)`
  are not fed from the mouse yet.

**Controls (DualSense / keyboard)**: pause = Options / Esc; move = D-pad /
arrows; validate = Cross / Enter; back = Circle / Backspace. On the Audio page
Up / Down pick a row, Left / Right change its slider, Cross = OK, Circle =
Cancel.

## Session 2026-10-28 (twelfth, `win`): the pause clock works (00:00 fixed) + three AVM1 runtime bugs that broke frame-swapped art

The owner's brief: features first; *"HUD elements are mostly broken and pause
menu is kinda there"*. The "HUD clock stuck at 00:00" (issue 10/14) is **not in
HUD.vmf** — it is the pause screen's `/pause_menu/clock` sprite (MAIN.vmf, top
left of the pause screen).

**Reproduced**:

    build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata ^
        -gm FREERIDE -skipcine -no3d -nopad -autoplay "200:press=pause;400:shot=C:/OpenTDU2/_p.ppm" -exitframe 410

### How the clock works [DATA, MAIN.vmf; names CODE PS3]

`-logpause -logclock` (new) disassembles the clock subtree.
`clock/time/disp` frame 0 defines `setHour()`:
`TimeArray = new Array(_root.HDecade, _root.HUnit, _root.MDecade, _root.MUnit)`,
then for each `i`: `/:nextValue2 = TimeArray[i]; tellTarget(number<i>) { if
(currentValue != nextValue2 || currentValue === undefined) { currentValue =
nextValue2; changeNumber(currentValue); } }`. Each `number<i>` plays `change`,
whose frame 15 does `num.gotoAndStop(currentValue + 1)` / `num_r.gotoAndStop(...)`
(`num_r` is the reflection). The four names are retail native->SWF globals
(`flash_globals.txt`: `/:HDecade 0xf54bb8`, `/:HUnit 0xf54bc8`, `/:MDecade
0xf54bd0`, `/:MUnit 0xf54be0`). The day / night icons are `clock/time/day` and
`clock/time/night` (labels `on` / `off`). The movie's own test path (`if
_root.fake == true` -> 17:29) is skipped because the exe blanks `fake`.

### What changed
- `source/game/pause_menu.{h,cpp}` — `PauseMenu::setClock(h, m)` + `refreshClock()`:
  pushes the four digits and invokes `disp.setHour()` once per opening (retries
  until the frame-0 actions have defined it), sets the day/night icon
  (07:00-19:59 = day, `[INF]`). `-logclock` dumps the clock subtree (with
  `-logpause`) and the digit clips' state 3 s after the roll.
- `source/game/freeride/gm_freeride.cpp` — the clock shows the time of the
  **weather preset** the world is lit with (`game_weathers` names are times:
  "14", "01.30", "23.20"; `-weather` picks one). There is no running game clock:
  the time-of-day system is `[ENC]` `[INF]`.
- `source/flash/flash_player.cpp` — three AVM1 runtime bugs, all general:
  1. **`new Array(a, b, ...)` dropped its arguments** (`A_NewObject` popped and
     discarded them): no elements and no `length`, so `setHour`'s loop never ran.
     Now `new Array(n)` / `new Array(a, b, ...)` build the same layout as
     `InitArray`.
  2. **`CallFunction` inside `tellTarget` ignored the target**: it resolved and
     ran the function in the frame's own clip, so `changeNumber()` (defined on
     each digit clip) was never found. Now a function owned by the current
     target is called on the target; otherwise the old behaviour.
  3. **A replace-move dropped the transform** (`placeObject`: PlaceObject2 with
     move + a new character created a fresh instance with an identity matrix).
     The SWF rule (and Ruffle's `replace_with`) keeps the depth's matrix /
     colour transform / ratio / name / clip depth unless the tag sets them. The
     clock's `num` sprite swaps glyphs 3..9 with no matrix, so 3-9 drew at
     (0,0) — shifted right and down — while 0/1/2 (which carry a matrix) were fine.
- `source/game/pause_menu.cpp` — the hand-tuned `icX/icY` icon residuals
  (resume −28/−26, players −22/−19, photo 9/−21, exit −30/−28) were
  **compensating for bug 3**; with it fixed they double-corrected, so all are 0.
  The `-logpause` disassembler's opcode name table was one entry short at 0x19
  (it printed `SetVariable` for `GetVariable`, `CallMethod` for `Add2`...): fixed,
  the dumps are now readable.

### Verified
- The clock at four presets (`-weather 23.20 / 18 / 01.30 / 14`) reads 23 20,
  18 00, 01 30, 14 00 with reflections; log `PauseMenu: clock 14:00 (day)`.
  `docs/images/pause_clock_times.webp`, `docs/images/pause_clock_and_icons.webp`.
- Side effects of the runtime fixes, A/B against the stashed tree (same command,
  frame for frame):
  - pause entry row: **the mystery blue box above the row is gone** and the
    star / players / camera icons, blank before, now draw, all centred
    (`docs/images/pause_icons_before_after.webp`);
  - radio panel (`press=radio`, frame 330): before, the panel had **no text**;
    now station / artist / title / volume bar show
    (`docs/images/radio_panel_before_after.webp`);
  - in-car menu (`press=left`): identical (mean diff 0.02, the goal box fade);
  - world map (`press=map`): identical (0.06, a strip of the bottom bar);
  - free-ride HUD: identical (0.015).
- Runs are real-time-dt, so two runs of the same command can catch the pause
  open animation at slightly different points (mean diff ~1.5 on the whole
  frame) — compare crops, not whole frames, for this screen.

### Ruled out
- The clock is not in HUD.vmf (`-dumphud`: no clock clip; `time` / `time_cd` /
  `chase_timer` are race timers fed by `/:Min /:Sec /:Cent`).
- Not a missing variable: after the push, `number<i>:currentValue` was right
  (1, 4, 0, 0) and the clips sat on frame 0 — that is what isolated bug 2, and
  the live display list (child at (0,0) vs (-200,-220)) isolated bug 3.

### Not done / next
- **A running game clock**: the time only follows the static weather preset.
  The retail time-of-day system is `[ENC]`; its PS3 twin is the way in
  (search the PS3 build for the `HDecade` consumer: `python tools/abs_refs.py
  0xf54bb8` in `C:\TDU2-PS3-Recomp`). With it, the sky and the clock can advance
  together.
- **The HUD zone badge** is still drawn by `FreeRideHud::drawZone`
  (`freeride_hud.cpp:549`) with "IBIZA" hardcoded and the `area X >= 4` guess
  (`:427`). Lead unchanged: `func_0017D6E4` (PS3) reading `DBAreaLevel`.
- **The minimap** is the engine placeholder (`drawMinimap`); the HUD movie's own
  `/HUD/minimap` frame draws around it.
- **The options pages** (priority 2) still push none of the `/:Option_title`,
  `/:nb_options`... variables. Re-dump the `options` subtree with the fixed
  disassembler first: the old dumps had shifted opcode names, so any reading of
  `setParameters` / `getParamVar` from them should be redone.
- Other movies may have carried workarounds for bug 3 (hand offsets on
  frame-swapped sprites); grep for `residual` / hand-tuned `_x`/`_y` writes when
  something now looks double-offset.

## Session 2026-10-28 (eleventh, `win`): camera views (the cycle was dead), free look, and the car features

Owner's asks: *other camera views*, *looking around the car*, and *car features
(windows, roof, indicators)*. Two of the three had a real defect behind them.

### 1. The camera cycle was completely dead — root cause found and fixed

`ChaseCamera::nextMode()` and the `IA_CarCamera` binding (pad **R1 / RB**) have
existed for a while, but pressing the button changed nothing. `-logcameras` (new)
shows why:

```
ChaseCamera: car 387 modes: 20 23 24 43 44 40 | p_array_camera_switch:      <- EMPTY
ChaseCamera: car camera 387: 6 cameras of the car, 1 in the switch cycle
```

`camerasingame.xmb` stores the cycle as **indexed names with an explicit count** —
`nb_camera_switch` plus `p_array_camera_switch[0]` … `[4]` (the XMB string table
carries those literal names). The code read it with
`getArraySize("p_array_camera_switch")`, which returns **0**, so `cameras` fell
back to a single default `Params()`. Every car in the game has been stuck on one
camera.

Fixed in `chase_camera.cpp::load`: count from `nb_camera_switch`, index the names
`p_array_camera_switch[i]`, and fall back to the car's own cameras in file order
if the cycle is empty. Now:

```
ChaseCamera: car 387 modes: 20 23 24 43 44 40 | p_array_camera_switch: 21 20 23 24 22
ChaseCamera: car camera 387: 6 cameras of the car, 3 in the switch cycle
```

**3 views rotate** for the California — mode 20 (chase), 23 (driver / cockpit,
`nearProjPlane` 0.05 → the fixed inside pose) and 24 (bumper) — in the retail
order. The 43/44/40 back views are the same poses with the view reversed and are
not in the cycle (they are what `CAR_BACKVIEW` is for). Verified: pressing
`carcamera` changes the frame by a mean of **62.77 / 255** against the chase view.
Evidence: `docs/images/camera_cockpit_view.webp`.

### 2. Free look — implemented (it did not exist)

`AVT_LOOK_*` (the right stick) was never read; `docs/STATE.md` listed it as
issue 13, "the in-car look". Now `ChaseCamera::setLookInput(stickX, stickY,
bBackView)`, called every frame from `GMFreeRide::tick`:

- **outside views**: the camera orbits about the car's up axis by the look yaw
  (a plain XZ rotation of the smoothed `heading`), and the aim point rises with
  the pitch;
- **inside views (23 / 24)**: a head turn — the fixed target swings about the
  seat instead of the car;
- the offsets **spring back to centre** when the stick is released
  (`kLookSpringDelay` 0.12 s), which is the retail behaviour;
- **`CAR_BACKVIEW` (pad L1 / LB)** is a full look behind while held — it was
  bound but consumed by nothing; it now drives `lookTargetYaw = kPI`.

Limits and speeds are `[INF]` (the retail look speeds are not identified):
`kLookYawMax` 2.6 rad (~149°) each way, `kLookPitchMax` 0.5 rad (~29°).
Verified: stick 0.9 orbits the camera ~134° around the car
(`docs/images/freelook_orbit.webp`), releasing it returns the camera behind the
car, and the back view changes 70.6 % of the pixels on a parked car.

### 3. The car features: roof and windows already worked, indicators do not

Verified rather than assumed:

- **Roof** — works. In-car menu (D-pad left), entry 2, validate →
  `CinematicCar: 'California' _car_body 4, hardtop open (Hardtop.anm t = 17.00 /
  17.00)`, i.e. the retractable roof ran the full clip.
- **Windows** — wired: menu entry 3 → `setWindowsOpen()`, which moves `WND_x1`
  onto the `WND_x2` rest (FUN_00c66150). Implemented; not re-verified this
  session beyond the code path.
- **Indicators** — still stubs (menu entries 0 / 1 log "not implemented"). What
  they need is a **light driver**, not new plumbing: the emissive path already
  exists (`eden_renderer.cpp` writes `__DirLight_Emissive_0` /
  `__Material_Emissive` from `material.Emissive`, and the shader variant flag is
  `S_USEEMISSIVE`), so an indicator is (a) identifying the car's lamp materials
  by name, (b) a per-frame emissive override for them, and (c) a left/right
  blink state with the retail cadence. `-logmaterials` did not show a
  light-named car material in the freeride run, so (a) is the first task.

### New test tooling (all used above)

- `-logcameras` — dumps the car's camera modes and `p_array_camera_switch`
  (`chase_camera.cpp`); the flag that found the dead cycle.
- `-autoplay look=x,y` — injects the right stick (the runs use `-nopad`), so the
  free look is testable headlessly; `look=0,0` releases it.
- `-autoplay backview` / `carcamera` — `IA_CarBackView` and `IA_CarCamera` were
  missing from the autoplay action table.
- `GSInput::setInjectedRightStick` / `clearInjectedRightStick`.


The owner's report: *"some geometry I can see through it looks like holes but
there not holes as I don't sink in them I can see things through hills and
stuff"*, with the extra context that they are exploring with `teleport.bats` in
places the game does not intend you to drive — **casino island** in particular.

**Reproduced**, and it is dramatic on casino island:

```bat
build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 ^
    -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad ^
    -island casino -tp 2642.0,1588.1 -heading 137 ^
    -autoplay "300:shot=C:/OpenTDU2/_cas.ppm" -exitframe 320
```

(`ECAS`, the casino resort. Screenshot `docs/images/casino_terrain_gap.webp`.)
The casino building **floats in mid-air** with the sea/sky visible *through*
where the terrain should be, and the ground the car sits on ends in a hard edge
about a sector away. The Ibiza control shot at the same frame is
`docs/images/ibiza_hills_control.webp` — continuous terrain, no gap.

### The data on casino island is genuinely sparse [MEASURED]

| | casino | ibiza |
|---|---|---|
| sector banks (`sector-*.bnk`) | **6** (`0-0`, `2-1`, `3-1`, `3-2`, `4-2`, `7-0`) | 1,135 |
| areas with a bank | **1** (`area-0-0`, 11 × 9 declared in `worldconfig.cpr`) | 30 |
| `cruisingzones.bin` | 48 B (empty) | — |
| `roadinspection.pth` | 16 B (empty) | — |

At the spawn the log shows **4 sectors loaded** and five neighbours missing
(`sector-0-0-4-1`, `-2-2`, `-2-3`, `-3-3`, `-4-3`). So most of what you see on
casino depends on the **coarse 24 m area terrain**, not the 3 m sectors.

### What I ruled out, with the test that ruled it out

1. **Culling.** `-nocull` (which forces `cullMode = 1`, no culling) gives a
   visually identical frame at both the village and the hillside. Not culling.
2. **The coarse area terrain is not the layer we see.** `-noareas` (which skips
   the area loop in `EdenWorld::draw`) gives an **identical** image — so the
   coarse terrain contributes nothing visible, and the ground we do see is the
   4 loaded fine sectors.
3. **The area terrain is nonetheless submitted.** `-logmaterials` reports
   `'Area-0-0' (TD_HeightMap vs8/ps56) idx 368640` — 368,640 indices = 61,440
   quads, i.e. the full 256 × 256 grid minus exactly the 4 loaded sectors'
   4,096 cells. So `updateAreaIndices` and `getSectorAt` (brick stagger and all)
   are **correct**: the mesh is built and drawn, and yet is invisible.
4. **Not a shader-permutation override issue.** The area material resolves to a
   *different* `TD_HeightMap` permutation than the sectors — `vs8/ps56` with a
   single colour map (`area-0-0-c`, all other slots `NONE`) versus the sectors'
   `vs0/ps48` with 8 texture slots. Forcing the sector permutation on everything
   (`-hmdebugps 48`) changes nothing.
5. **Not missing assets.** No heightfield, material or texture warning in the
   casino run.
6. **Not a far-plane clip.** `camera.Far = 40000`, `Near = 0.8`.
7. **Not the Ibiza LOD/streaming radius.** At a hillside spot
   (`-tp 7035.8,27062.8 -heading 200`) the terrain is continuous at radius 1 —
   the gap is casino-specific, not a general sector-radius artefact.

### The open question, and where to look next

The coarse area terrain is built, submitted with a valid program, and invisible.
The asymmetry to chase is in `EdenWorld::loadHeightfield` (`eden_world.cpp`
~517-530): the **island** branch (called from line 121) sets
`HeightfieldGrid = 2`, calls `updateProgram`, and then **checks
`pProgram == nullptr`** and falls back by clearing the materials. The **area**
branch (line 525-528, `pBaseName` starting with `"area"`) sets
`HeightfieldGrid = 1` and calls `updateProgram` with **no null check**, and
`loadArea` (line 622) **ignores `loadHeightfield`'s return value**. So an area
whose permutation fails to resolve is added to `areas` with a dead program and
is then drawn every frame as a no-op.

Next step: log `area.Terrain.Materials[0].pProgram`, `HeightfieldGrid` and
`NumIndices` for casino's `area-0-0` inside `EdenWorld::draw`, and compare the
`vs8/ps56` program against the island's (`HeightfieldGrid = 2`) program in
`EdenShaderSystem`. If the area permutation is missing from the translated
shader set, the fix is either to resolve it correctly or to fall back to the
sector program the way the island branch does.

Note this is **not** the same as the casino collision gap the owner reported
earlier ("terrain that I can drop through") — that one is about
`VehicleWorldCollision::streamAround`, and with only 6 sectors on the island
both the render and the collision gaps have the same root: sparse casino data.


The owner's report: *"I can't view the world map as far as I can tell as it
should show when I hit select on controller/share button on dualsense."* Correct
— and the cause was simple: `IA_Map` existed and was already bound
(`gs_input.cpp` binds it to `SDL_GAMEPAD_BUTTON_BACK`), but **nothing consumed
it**, so the button did nothing. `docs/STATE.md`'s own pad decode had already
recorded `MAP = BACK` (item 12); it was never wired.

**New: `source/game/map_screen.{h,cpp}`** — the world map of the original
frontend, `FrontEnd/HiRes/map.bnk` → `MAP.vmf` (3,676,160 B, the largest
frontend bank after main.bnk). `GSFlash::getMovie("map")` loads it exactly like
`main` / `hud` / `gauges`. `GMFreeRide` owns one instance (like `PauseMenu`):
MAP opens it, BACK / MAP again / ESC closes it, the world freezes and the engine
ducks while it is up, and it is drawn over the HUD.

**The movie's labels, dumped with the new `-logmap`** (26 of them) — the
candidates a first attempt would have guessed at (`open`, `map_in`) are **wrong**:

```
map_open -> map_stop      the open animation, then the interactive resting state
map_back                  the back transition
close / close_test / close__ / close_free    four close variants, one per context;
                          close_free is the free ride one
map_clic, map_tutorial, contest_preview, contest, contest_back, results_in,
rootmenu_setup, _rootmenu_stop, credits, credits_back, spots_stop,
datas_off(_stop), datas_on(_stop), co_direct_enter, close_test, init, default
```

So `MapScreen::open()` plays `map_open` and `close()` plays `close_free`, each
with a fallback list in case a re-authored bank differs.

**Variables pushed** (names from the retail native↔SWF contract recovered from
the PS3 build, `docs/re/ps3/flash_globals.txt`, the `Map*` group):
`/:IsMapAvailable` (the gate), `/:WhatPlatform` 1, `/:WhatGameMode` 2,
`/:BackFromMap`, `/:IsBackFromMap`, `/:IsMapQuit`, `/:MapCancel`,
`/:MapPlayerInfosLoaded`; `ApplyFlashLocalization` runs on the movie's `sbdd`
command and after the load (the `FreeRideHud` pattern). The movie writes
`/:minimap_x_offset` / `/:minimap_y_offset` back — `MapScreen::getMarkerOffset`
reads them.

**Verified headless** (`-gm FREERIDE -skipcine -no3d -nopad -logmap`,
`-autoplay "200:press=map;420:shot=…;520:press=back;700:shot=…"`; screenshots
`docs/images/world_map_screen.webp` / `world_map_closed.png`):

- `map_open` plays, the movie runs its script — its FSCommands are
  `pvoi:1:HSMAP_01`, `ston`, `bmap:fltr:icf`, `bmap:ok`, `maok` — and its own UI
  renders (the `OK / Filters / Camera / Zoom / Back` bar across the bottom).
- BACK plays `close_free` and the HUD comes back clean.
- `-logflashvars` reports **no** undefined `Map*` / `minimap` variable, so the
  screen is not asking for data the game fails to provide.

**Still open — the map CONTENT does not draw.** The centre of the screen stays
black: the movie's chrome renders but its island map (and the player marker) do
not. It is *not* a missing variable (`-logflashvars` is clean) and *not* a
missing asset (no texture or export warning from the map movie; the HUD's own
minimap does draw roads, so the 2D renderer handles map content). Candidates for
the next session: the map clip's own visibility/matrix, a mask the runtime does
not implement, or a clip that the game is expected to `attachMovie` (the movie
warns `attachMovie: no export 'specificHelp'` — but that one comes from
MAIN.vmf, before the map loads). `-logmap` dumps the labels and exports, which
is the starting point.

Also this session: **`-autoplay` gained a `map` action** (`tdu_instance.cpp`
`kActions`), without which the screen could not be driven headlessly.


Picked up the PS3-build work GLM started (it hit its 5 h limit mid-session). The
work lives in **`C:\TDU2-PS3-Recomp`** (tools + `port\out\`) and is documented in
**`docs/re/ps3/`** — `README.md` (overview + resume points), `physics_layouts.md`
(the serializers' member layouts) and the new **`constant_pools.md`**.

State of the route: the decrypted PS3 `EBOOT.ELF` is the readable counterpart of
the PC build's 2.8 MiB SecuROM band, and GLM's infrastructure works —
`func_index.json` (53,119 lifted functions), `strings.json` (68,589 strings),
the `docs/re/ps3/*.txt` catalogs (1,710 `/:` Flash globals, 851 physics names,
642 menu strings, 312 RTTI classes), and `get_func.py` to dump any function's
lifted C++.

What this session added:

1. **`tools/abs_refs.py`** — a single-pass index of every address
   materialization (absolute `addis rT,r0,HI` + `addi/ori`/d-form load, and
   TOC-relative) in the whole text segment: **20,871 addresses**, each with its
   referencing function. It replaces per-query rescans (GLM's `xref_addr.py`
   times out on the 1.8 GB lifted tree) and supersedes `addr_refs.py`, which only
   looked for the TOC-relative form. Plus **`find_const.py`** (one level of
   pointer indirection) and **`find_floats.py`** (locate the named constants).
2. **The trap that cost the most time, now documented:** `.rodata` lives *inside
   the first PT_LOAD segment* (`0x10000..0x10A0508`, after the code), not the
   second. A "data = second segment" filter silently drops **every** constant
   pool the project queries (`0xf120f0`, `0xfb0000`, `0xf144d0`); fixing the bound
   took the index from 3,256 to 20,871 addresses.
3. **Constant pools mapped** (`constant_pools.md`): the shared scalar pool
   `0xf120a0–0xf1214c` (1e-5, 1/6, 1/3, 0.5, 1.0, 0.01, 0.026, 0.93, 0.88, 1.5,
   1.1, **19000.0**, ...), the `hkVector4` constants at `0xf12150–0xf121a0`, and a
   uniform 1/6 grid at `0xf7a1e4–0xf7a1f8`. `19000.0` appears exactly **once** in
   the image — a strong candidate for the default anti-roll bar stiffness
   (STATE.md logs `arb 16000/16000`). `-9.81` (world gravity) is at
   `0xf3ff84` / `0xf40134` / `0xf795c4`.
4. **Correction to GLM's resume plan:** the `0x0Bxxxx–0x0D5xxx` neighbourhood it
   mapped is *serializers* (name-emitting reflection), not the physics. The code
   that actually **reads** the float constants is a **second cluster at
   `0x15xxxx–0x17xxxx`** (`0x15cb48`, `0x15cec0`, `0x15e678`, `0x1603a0`,
   `0x1664e8`, `0x1674b4`, `0x169764`, ...). Negative result to go with it: the
   scalar sub-range `0xf11f00–0xf1214f` has **no** direct materialization and no
   pointer slot, so it is reached as `base + offset` — a constant xref cannot
   find it, the base register has to be found instead.
5. **Hypothesis recorded, not asserted:** if the PS3 and PC builds share a link
   order (`PS3_VA ≈ PC_VA − 0x3F0000`), every PC `[ENC]` address of
   `vehicle_physics_spec.md` §6.8 — the ABS/TC tests `FUN_0054eaa0` /
   `FUN_0054eae0`, the engine predicates `FUN_00546xxx` — lands inside that
   `0x15xxxx` cluster. It has to be calibrated per region (the two `.text`
   starts differ by `0xF230`), so it is a test, not a result.

Also written this session: **`docs/re/REA_ASSESSMENT.md`** — the evaluation of
<https://github.com/morluto/rea> for this project (REA is a workflow upgrade,
not a SecuROM unlock; on Windows its Ghidra provider admits **PE only**, so it
cannot take the PS3 ELF at all; the PC target is 32-bit x86 PE with 2.81 MiB of
`.text` encrypted at rest, and the readable 62 % does carry the full `hkpVehicle`
RTTI family, the `/:` Flash contract and the input/FFB surface).

And **`docs/re/CROSSBUILD_PLAN.md`** — the recommended plan for the next
multi-agent session: what each of the four versions (PC+mod, PS3, 360 recomp,
OpenTDU2) is good for, a prioritised work list with a first command per task, the
ten traps already paid for, and the two-agent split. Its centrepiece is
**`tools_probe/match_builds.py`**, which joins the PC and PS3 builds on shared
strings and produced a validated **[ENC] locator**: **69 PS3 functions that are
the readable twins of PC code encrypted at rest** (`docs/re/ps3/ps3_enc_twins.json`),
plus 8,895 PC↔PS3 string anchors (`docs/re/ps3/pc_ps3_anchors.json`). The twins
cover input/controller (`func_00223200`), Flash/AVM1 events
(`func_00B1C058`/`0xB1C258`), HUD zone names (`func_0017D6E4`), camera blur,
car damage, 3D sound, PNJ/traffic and casino stats — i.e. the reading list for
the open features, already located.


Nothing was built or run in `C:\OpenTDU2` this session: the deliverables are
tooling and documentation for the PS3 route.


## Session 2026-10-27 (seventh, `glm`): issue 9 fixed — the first drive wakes up (`bd5c4bb`)

Pulled the two `win` physics commits first (`c4b471f` suspension,
`3a4acf3` engine sound + wheel diagnostics; fast-forward). Then issue 9
("first drive ends frozen on white"):

**The white screen was the un-implemented hand-off**, and the wake-up scene
is **not an FMV** — it is an in-engine cinematic, same family as
C_FIRST_OUTRO (user asked; spec `freeride_spec.md` §1.5 + this session's
data facts). After the 90 s timer retail plays **C_FADE_FREERIDE**
(`scenarics/cinematics/intro/fade_freeride.bnk`, 15 s) in the *house spot
frame* (its `.shk` is the house spot collision hash): fixed camera
"NO_CAMERA" (house frame (5.90, 1.22, −5.01) fov 35), fade in from white
2.5 s, Tess (DBPNJ `STO_Tess_Wintory`, entity WOMAN at (1.429, 0, 0))
sits on the ground at the driver door, animated by the cinematic additional
anim `ev_tessincar_toplayer01_npc.anm` (85 tracks / 29.2 s, pelvis fixed —
knock `s_toctoc` + first Meet-Tess voices = the Fx at 2.68 s
`FxIntroNew-E_Meet_Tess_02_00`, resolved through the language bank
`fade_freeride__<lang>.bnk`).

- New `source/game/freeride/fade_freeride.{h,cpp}` (FirstOutro-pattern:
  bank + Cinematic/CinematicScene + CinematicSounds + one EdenAvatar; the
  anim is sampled directly, no scene Animation track — the scene's is the
  "none" clip). GMFreeRide gains `eState::Wakeup`: at fade end the car is
  parked beside Tess under the white [INF pose: house (1.79, 0, 1.50),
  heading +90°, door plane ~0.55 m in front of her], physics holds, engine
  ducked, HUD hidden; at CinEnd 3 the freeride resumes from the parked pose
  with the step-3 goal text **loc 6111 'On the road to "Solar Crown"'**
  [INF] — the user is right: retail's next mission is driving Tess to the
  Solar Crown (its dialogue is the 16 `IntroNew-E_Meet_Tess_*` Fx roots in
  this bank; the mission logic itself is [ENC] and not implemented).
- `Cinematic::parse` fix: 0x3c shot records *without* a transition at
  +0x14 hold it at **+0x28** (1, 2.5 s, RGB white) — read with a sanity
  check so C_FIRST_OUTRO's no-fade records are untouched
  (cinematic_scene.cpp; C_INTRO/C_FIESTA_INTRO fades at +0x14 unchanged).
- The anim's last second is the climb into the car (the step-3 cinematics
  continue through it): the port holds the seated pose at 14.2 s
  (`kEndPoseTime`) instead of freezing the mid-climb pose.
- New flags: `-storytime <s>` (timer override for tests), `-skipwakeup`.
- Verified headless (`-gm freeride -skipcine -storytime 3 -fixedtimestep`,
  shots in `docs/images/wakeup_*.png`): white → fade-in → Tess at the door
  → drive resumes with the goal box. Audio path run clean (no -noaudio).
- **Next for step 3** (user asked): the Solar Crown drive — needs the Solar
  Crown target position (find in spots.bin / DB), a GPS/map goal, ideally
  Tess seated in the passenger seat (reuse the `setPlayerSeated` driver-seat
  machinery from IntroCinematic with a passenger offset), arrival detection
  + the next cinematic — which is `meeting_tess.bnk` (see below, fully
  mapped this session; nothing needs re-animation, it is all authored data).

**Follow-up (same session): the scenes are CUT from the default flow.** The
user provided retail footage ("Test Drive Unlimited 2 - Wake up!",
youtube.com/watch?v=Y5W7BUlGSz0, kept at `_retail_wakeup.mp4` in the
workspace, untracked) and we restructured to the retail order (knock scene
over the gameplay BEFORE the flash, then the flash, then the wake-up
scene) — but the user then decided to cut the scenes: the flash is the
whole story beat and the freeride resumes from where the car stopped with
the step-3 goal text. The built knock scene + wake-up placeholder stay in
the code behind **`-wakeup`** (verified working; `_wk6_0300.png`-style
shots showed Tess at the driver window of the stopped car with the HUD up,
exactly like retail). The data map below is the revisit path.

Retail sequence per the footage (for the revisit):
1. 90 s drive → car stops where it is (HUD up) → Tess knocks at the driver
   window + first lines over the live game view (C_FADE_FREERIDE, no
   camera of its own) — ~15 s;
2. white flash (~1.5 s) hides the car parked back at the mansion courtyard;
3. C_MEETING_TESS "thedream" (66.7 s, 16 shots, fade in from white 4 s):
   in-car interior, the player asleep (meet_sleep) wakes up, Tess opens the
   door, they talk, SHE GETS A PHONE CALL, more talking, the keys hand-over
   (key_ring_ferrari prop), then both get in — the player drives, Tess
   passenger (the user: "drive her to the Solar Crown").

## Step 3 data map (session 7, for the next implementation)

The user remembered the retail beats (knock → wake up → she gets a phone
call → more talking → she gives you the keys). **All of it exists as data**
in `scenarics/cinematics/intro/meeting_tess.bnk` (same house spot frame:
`.shk`/`.dhk` hash 57E5C829CCD9F2C6 again):

- `C_MEETING_TESS` = Entity__Cinematic, `cin_meeting_tess_M_MEETING_TESS_totem.cin`
  **66.67 s, 16 shots** (camera hashes 0x95ccd3f9.. / 0x96ccd3f9.. = the
  c_meeting_tess_0x cameras of `M_MEETING_TESS_thedream.sce`, 54.9 KB — the
  scene is called "**thedream**"). Shot starts at 0 / 5.56 / 7.4 / 9.45 /
  12.86 / 15.85 / 19.3 / 25.67 / 34.13 / 39.62 / 41.23 / 43.2 / 45.38 /
  55.02 / 56.99 / 61.98 s. Shot 0 fades in (1, 4 s — at +0x14 this time).
  Messages: `weat:Meeting_Tess`, `wmod:Meeting_Tess_02..16` (16 weather
  modifiers, one per beat!), `shad:GP/PA/PR/PG/PM/CINE_VERY_NEAR`,
  `zcvn`/`zcne`/`phot`. Fx events at 0 / 2.06 / 5.4 / 10.91 / 36.22 /
  59.23 / 62.04 s (voices + the cue wavs below).
  **Female avatar variant**: `cin_meeting_tess_M_MEETING_TESS_W_totem.cin`
  + `M_MEETING_TESS_W_thedream.sce` + `M_MEETING_TESS_W_thedream_woman.sce`
  (pick by the player avatar's sex).
- Sound cues (names = the beats, `sound/cinematiks/intro/`):
  `s_knock_CINEMATIC.wav`, `c_meeting_tess_1-getout`, `_2-phone`,
  `_3-get_keys`, `_4-submitkeys`, `_7-open2`, `c_meeting_tess_CINEMATIC`
  (1.5 MB music). The 16 voice wavs live in `fade_freeride__<lang>.bnk`
  (`intronew-e_meet_tess_01..12` + `_b` alt takes, 0.3–2.9 s each; **11 is
  `VOICE_AVATAR`** = the player's line); the same `intronew-e_*` .bfx
  hashes are in both banks.
- Animations (`anims/meet_tess/meet_xan/`): npc (Tess)
  `intro_meet_tess_01/02/03/04a/04b/06_npc` + `meet_gocar_tess`;
  avatar (player) `intro_meet_tess_02/03/04a/04b/05/06-m/-w` +
  `meet_sleep-m/-w` (asleep in the car!) + `meet_gocar-m/-w` +
  `meet_upcar-m/-w` (walk to the car, get in); **props**
  `cf_kcar_intro_meet_tess_03/04a/04b` (the Ferrari key ring hand-over! the
  prop model `key_ring_ferrari` with `ferrari_key_ring.2db` is in the bank)
  and `cf_phon_intro_meet_tess_03_npc` (the phone prop `a-phone` — the
  call). Interior materials: `garage_int_*`, `california_int2.2db`,
  `cubeint.2db` (in-car interior shots).
- Implementation order when picked up: load meeting_tess.bnk in the
  FadeFreeride/Wakeup continuation, play `C_MEETING_TESS` (M/W by avatar
  sex) with the FirstOutro-style player (16 shots + per-shot wmod weather
  modifiers + Fx cues), the player avatar seated asleep (meet_sleep) and
  Tess on her npc anims, the two props animated by their cf_ tracks; at the
  end (gocar/upcar) the freeride resumes → the Solar Crown drive mission.
  The .cin Fx/message tracks give every beat time; no video needed for the
  build — a retail recording would only serve as visual QA of the result.
- `win` should merge `glm` when convenient (the user's game runs from
  `win\build` — do not rebuild it while playing).

## Session 2026-10-27 (fifth, `win`): user report triage — audio, wheels, physics, back-on-track

The user played the `win` build and reported ten issues (list below, plus
"car audio outside cutscene", "wheels don't spin", "tips too easily, as if the
car had no mass"). What this session found, each item verified headless with
`-gm FREERIDE -skipcine -exitframe N` (the game logs to `opentdu2.log` in the
working directory; 500-1500 frames take ~20-40 s at ~18 fps):

1. **Car audio outside the cinematics — was silent, fixed** (`3a4acf3`).
   `VehicleAudio::update` gates *every* layer gain and the engine load ramp on
   `Params::bEngineOn` (the pause menu already used it to duck the engine) and
   `GMFreeRide::tick` never set it: all engine / rolling / skid layers stayed at
   gain 0. One line (`ap.bEngineOn = true`). Verified: the new `-logdrive` tail
   prints `audio 1.80..1.94 load 76..105` while driving (0 before).
2. **Wheels do spin** (`3a4acf3`, no behaviour change). The renderer input was
   measured in game (`-logdrive` now prints `rimT` / `rimY`, the wheel node
   translation and its y axis): the hub stays at the modelled place and the rim
   y axis rotates with `w 75..113 rad/s` at 90..130 km/h, matching `v / r`. What
   reads as "not spinning" is aliasing: 80 rad/s at 60 fps is 76 deg per frame,
   i.e. a 5 spoke rim appears to crawl (76 - 72 deg). A wheel *blur* would be
   the retail style fix. Real bug found next to it: the front / rear rim test
   was a plain `find("_R")`, which matches every `D_RIM_xx` name, so all four
   wheels used the rear rim and diameter (fixed).
3. **Car tipped over / felt massless — the suspension was launching it**
   (`c4b471f`). `-vehtest` gained a rollover metric (`roll max` / `FLIPPED`,
   plus `-logcorner` for the trace) and proved it on the *flat* plane: a step
   steer of 0.25 at 100 km/h reached 14-23 kN on one wheel (3.5-6x the static
   load), the inner wheels lifted, the body reached 36-81 deg of roll and the
   car ended airborne. Three non physical spikes, all [INF] laws: the damper
   rate was the one tick compression difference (a terrain step or a reappearing
   contact fed it several m/s -> 72 kN); the bump stop was capped at the *whole
   car weight* per wheel (34 kN); the tyre force used the raw spring load, so a
   5x load spike became a 5x lateral force (mu 1.85, no load sensitivity). Now:
   damper rate clamped to 0.5 m/s, bump stop capped at the static load of the
   corner, tyre force load clamped to 2x the static load (the chassis still
   takes the full spring). Result: 4 of 6 steady state corners are clean with
   roll maxima 2.3 / 9.1 / 2.5 / 2.9 deg (lateral 1.36 / 1.22 / 1.47 / 1.45 g)
   where 4 of 6 used to roll over. The two remaining rollovers are 7.5 deg of
   steer at 100 km/h (a 3.8 g demand, beyond the tyre limit): the next lever is
   the missing load sensitivity of the Pacejka `D` (grip does not fall off with
   load) and/or a wheel blur. `arb 16000/16000` is in the vehicle log now - the
   anti-roll bars *are* loaded (they were a suspect).

4. **BACK_ON_TRACK is a hold with the retail element**. `-dumphud` found the
   movie element `/HUD/ig_menu/backontrack` (sprite 1019, labels `default`
   frame 0 / `load` frame 7, the fill runs to frame 17). `FlashPlayer` gained a
   public `gotoFrame(clipPath, frame, bPlay)` (same shape as `gotoLabel`) and
   `FreeRideHud::showBackOnTrack(progress)` drives the fill from the hold
   progress; `GMFreeRide` accumulates a 1 s hold (`kBackOnTrackHoldTime`, [INF])
   and resets once per hold. Verified with `-autoplay
   "300:hold=backontrack;305:release=backontrack;360:hold=backontrack;430:release=backontrack"`:
   the 5 frame hold does not reset, the long one does (`FREERIDE: back on
   track`), and the element is hidden at rest (a warning is logged if the clip
   is missing).
5. **#9 "white screen after the drive" is a cinematic, not a video bumper.**
   `scenarics/cinematics/intro/fade_freeride.bnk` is the `meeting_tess/fade_blanc`
   scene: `entities.xmb`, `fade_blanc_M_FADE_FREERIDE.cin` + `_life.sce`, the
   `ev_tessincar_toplayer01_npc.anm` (Tess in the car), the knock
   `s_toctoc_CINEMATIC.wav` and the `meet_tess_01..12` voice lines in
   `fade_freeride__us.bnk`, with the 4.4 MB `meeting_tess.bnk` next to it. So
   story step 3 is a *playable* cinematic: the white screen is the
   unimplemented step, and the path is a `FirstOutro`-like player on that bank
   (the machinery - Cinematic / CinematicScene / CinematicSounds / entities.xmb
   prop loading - already exists in `first_outro.cpp`).
6. **New diagnostics** (all committed): `-dumpcars` (one line per
   DBCarPhysicsData row: mass, geometry, ride heights, gravity center height /
   length, track, suspension / damper / anti-roll rates, wheelbase - the survey
   that showed `cgh` 2..50 cm across the 279 cars and the anti-roll rates);
   `-logcorner` (vehtest attitude / load trace); the anti-roll rates in the
   vehicle log; audio gain / load and `rimT` / `rimY` in `-logdrive`; and
   `-dumphud` also dumps sprite labels (`-dumpsprite <id>`).
7. **HUD (#14) status**: `-dumphud` lists 1355 clip instances (saved to
   `_hudclips.txt` this session, `_*` is gitignored). There is **no clock clip
   in HUD.vmf** (`chase_timer` / `checkpoints/cur_time` are race timers), so the
   user's 00:00 clock is in another movie - ask which corner of the screen it is
   before touching it. The unused HUD elements (checkpoints, eliminator,
   ig_menu...) are placed *off stage* by the movie, which is why they do not
   show.
8. **Left for the next session** (best first): the look-around camera in the
   car (#13: `ChaseCamera` has no look state; the pattern to copy is
   `walk_controller.cpp` - mouse delta + right stick, `kCamSpeedMax 2.8 rad/s`,
   clamp the pitch), the two remaining vehtest rollovers (load sensitivity), the
   OPTIONS screen widgets (#4), the meeting_tess cinematic (#9), the quit
   confirmation (#2: the FSCommand protocol `exit:open/vali/canc` is decoded),
   and the HUD clock (#14, needs the movie identified).

9. **Pad: reverse -> forward was impossible with the pedals** (`<fix>`).
   `-vehtest` now runs a reverse cycle (brake from rest -> 3 s of reverse ->
   accelerator) and reproduced the report exactly: the car braked to -5.5 km/h
   and then stayed in reverse *forever* (wheel spin oscillating between +46 and
   -4.7 rad/s, the idle creep of the reverse gear balancing the brake), because
   leaving reverse needed `forwardSpeed > -0.5 m/s` and 1st from neutral needed
   `>= -0.3 m/s` - speeds the creep never reaches (that is why the hand brake
   was the only way to stop). Reverse is now left (and 1st engaged) below
   `kReverseForwardSpeed` 2.5 m/s [INF] when the accelerator is held (it is the
   brake pedal in reverse). Verified: -38.7 km/h in reverse -> -13.5 -> -4.5
   -> gear 1 -> 11 -> 39 -> 66 km/h on the accelerator alone.
10. **Pad: the D-pad in-car menus (windows / indicators / roof, radio) are not
   implemented.** The HUD movie has the retail elements (`-dumphud`):
   `/HUD/ig_menu` (55 frames, `icon_1..4`, `valid_icon`, `backontrack`) and
   `/HUD/radio` (43 frames, `volume`, `texts/radio_track`, `texts/radio_singer`,
   `radio_logo`). The pad map already binds the D-pad to `MENU_LEFT/RIGHT/UP/
   DOWN`, i.e. the same actions the pause menu uses - retail interprets them in
   the drive state as "open the car menu / the radio", so the work is a menu
   state machine in `GMFreeRide` (D-pad left = ig_menu, right = radio, up/down =
   entry, validate = apply, back = close) driving those clips, plus the actions:
   windows (the model has WND_x1/x2, `FUN_00c66150` lowers them - see
   `CinematicCar`), roof (the Hardtop.anm player, currently only set at load),
   indicators (no light control in the port yet) and the radio (no radio player
   at all: the plan is in the third-session notes; `sound/radio/setup.xmb` +
   the Radio mixer client + the RADIO_CHANNEL key bindings already exist).

11. **Car radio works** (`<radio>`): `RadioPlayer` (source/audio/radio_player.*)
   enumerates `Euro/Bnk/sound/radio/music/*.bnk` (65 songs, the file name is
   "Artist - Title", the bank holds one wav, MS ADPCM 44.1 kHz stereo which SDL
   decodes), decodes one song on demand and plays it through the **Radio**
   client of audiosetup.xmb (bfx index 9), so it follows the game mode mix
   preset and the ducking. `-autoplay press=radio / volup / voldown` drives it
   headless: `Radio: playing a astronauts - classic (voice 40, 1 of 65, 218.6 s)`.
   The HUD panel is the movie's own `/HUD/radio` element: it is **parked off
   stage** in the default frame, so setting the clip visible alone showed
   nothing - the game has to play its own labels (`-dumpsprite 806`: `default`
   f0, `open` f8, `stop` f25, `close` f33). `FreeRideHud::showRadio` now drives
   `open` / `close`, writes the track / singer texts and sets the volume bar
   (the unnamed fill instance at depth 4 of `/HUD/radio/volume`).
   Not done: the retail station flow of `sound/radio/setup.xmb` (ROADROCK_*
   jingles, DJ comments, ads between the songs) - the songs are one playlist.
12. **Pad: the D-pad mapping is the game's own, and it is a *dual* binding**
   (`DeviceX360Pad.xmb`, dumped this session): `MENU_UP/DOWN/LEFT/RIGHT` are the
   four D-pad directions **and** `RADIO_VOL_MORE/LESS` are the same up / down
   channels; `MENU_VALIDATE` = A, `MENU_BACK` = B/circle, `MENU_PAUSE` = START,
   `MENU_OPTION_1/2` = X / Y, `MAP` = BACK, `CAR_HBRAKE` = X (BUTTON_2),
   `CAR_LIGHT` = A (BUTTON_0), `CAR_GUP/`CAR_GDOWN`` = Y / B, `BACK_ON_TRACK` =
   B, `CAR_CAMERA` = RB, `CAR_BACKVIEW` = LB, `CAR_HORN` = right stick click,
   `CAR_ACCEL/BRAKE` = RT / LT, and **`AVT_LOOK_*` = the right stick** (which
   OpenTDU2 does not implement: that is issue 13, the in-car look). Because the
   volume and the menu share the D-pad up / down, the radio volume must be read
   from `IA_RadioVolMore/Less` only (reading `IA_MenuUp/Down` as well fired both
   and made the buttons look "mixed up"): D-pad up / down = volume, D-pad left /
   right = the song while the radio panel is up, D-pad right = open it, circle =
   close. `-logpad` logs every pad press edge (button -> action).
   WARNING for future sessions: a connected pad feeds *every* running instance,
   so a headless test run also receives the player's button presses (that is
   what "skipped" the radio songs in the first test runs).

## Session 2026-10-27 (eighth, `glm`): merge of `win` + the radio / Accessories HUD

Merged the `win` fifth-session work (radio player, in-car menu, back-on-track
hold, reverse fix; conflicts only in STATE.md / gm_freeride.cpp constants).
Then the user's two HUD complaints with retail PC screenshots:

- **Radio panel showed file naming** — fixed: artist / title in their
  original spelling come from `sound/radio/setup.xmb` (ProjectExport/Tracks,
  "Dmitry Fyodrov - Wolf Brigade.wav") matched case-insensitively to the
  bank file names; the panel lines are /:RadioTitle = the STATION name
  ("▶ Hariba Radio"), /:RadioSinger = artist, **/:RadioTrack** = title (the
  radio_track scroller). The station LOGO is the /HUD/radio/radio_logo
  sprite driven by its labels **'hariba' / 'kalua' / 'roadtrack'** — three
  stations cycle with RADIO_CHANNEL (all share the song playlist [INF: the
  per-station song split is not in the readable data]).
- **Car menu icons inaccurate / menu invisible** — decoded: /HUD/ig_menu's
  icon_1..icon_4 sprites have the entry labels **'clil' / 'clir' (indicators
  left / right), 'hood' (roof), 'wind' (windows)**; the menu title field is
  **/:IG_Title** ("Accessories"), the entry names go in /:IG_<label> shown
  by valid_icon/valid_txt. The menu must be shown with
  **gotoLabel('stop', false)** — playing 'open' runs past 'stop' into
  'close' and ends hidden (that was the invisible menu). Entries: indicators
  left/right (not implemented — no light driver), roof, windows.
- **-nopad**: headless runs ignore the gamepad (a connected pad feeds every
  running instance; the user's presses were toggling the test panels).
- TRAP: autoplay presses must be written `60:press=left` — a bare `60:left`
  parses as an unknown command and is **silently dropped** (cost an hour of
  "invisible panels"; the panels seen in the first test were the user's own
  pad presses on the running instance).
- Verified headless (-no3d -nopad): the panel matches the retail screenshot
  (logo / "▶ Hariba Radio" / artist / title / volume), station cycling swaps
  the logo + name (Kalua FM), the Accessories wheel renders with the four
  glyphs + title. Retained from the merge: songs step with D-pad right,
  volume up/down, back-on-track hold.

## User test report 2026-10-27 evening (`win` build at `4c6fc80`): ten issues for triage

The user played intro → house → first drive on the `win` build and listed what
is broken. Numbers below are the user's — keep them when referring to issues
("issue 3" = the pause menu). Screenshots are committed under `docs/images/`
(`user-report-*.png` = the user's run; `pause_menu_row.webp` /
`pause_menu_options.webp` = headless repro, glm workspace).

1. **Hair clips through faces.** Dark hair-card triangles overlap the
   forehead/brow of the party avatars, stair-step aliasing on the edges
   (`docs/images/user-report-party-hair-clipping.webp`,
   `user-report-avatar-closeup.png`). Hair is an avatar slot item
   (`source/game/avatar/eden_avatar.cpp`: `kSlots` "hair",
   `kHairShaderSet 0xd5013ed0dbdedede`, HAIR_MAT palette :177). Suspects:
   hair cards skinned against the animated head lag it (bind pose vs pose),
   no alpha-to-coverage, wrong draw order vs the face. Repro: intro pool
   party, camera near any dancing avatar's head.
2. **Quit = quit to menu, silently.** Quit goes straight to `GM_Login` with
   no confirmation (`gm_first_walk_mode.cpp:583`, `gm_freeride.cpp:545`,
   `gm_avatar_choice.cpp:255`). The movie's confirm protocol is decoded
   (FSCommands `exit:open` / `exit:vali` / `exit:canc`, see the -logpause
   root-function dump): retail opens an exit-confirm panel. Drive that
   sprite, or a minimal game-side confirm, before the mode switch.
3. **Pause menu semi-broken** — what is left after `f8b456c` + `4c6fc80`
   (repro: `docs/images/pause_menu_row.webp`):
   - Resume/Quit icons attach at the bottom-right corner of their square
     (~+45,+45 px), Options' gears are centred: the offset lives in the
     per-frame art placement of the `specificIcons` frames ('resume' /
     'exit_game'), attached in `refreshEntries()` →
     `gotoLabel(sqPath + "/ic/icLoaded", icon)` (pause_menu.cpp).
   - The per-square tooltip (`ttip`: 9-slice box + text field id 18537 bound
     to `../:txt` + button glyph, authored scale 0.0614) opens on the wrong
     side, oversized, label clipped ("Re…") — its state animation was
     authored for the vertical column. Position it beside the selected
     square from `layoutEntries()`.
   - Every button glyph (`pc_btn` inside the `a_` / `b_` hint sprites) shows
     the X360 red-circle-X: those sprites pick their platform frame from
     `/:WhatPlatform` (the game sets 1 = PC) through the movie glue, which
     is dead in this runtime. Seen on the bottom `help2` bar (with its
     `/:m_Ok` / `/:m_Cancel_` texts), the panel's bottom ✲/X pair, the red
     dot at the panel's top-right corner, the one on the clock. Fix: drive
     the `pc_btn` clips to the PC frame from the game (instances are in the
     -logpause display list dump).
   - The movie fires `sndp:*` (mopen/ttip/valid), nothing plays them —
     audio hook wanted.
4. **Options screen looks like the Editor** (user report + repro
   `docs/images/pause_menu_options.webp`): validating Options renders a
   top-left fragment only — a big gear glyph and a pen/path glyph below it
   (reads as the TDU2 course-editor icons), no labels, no page content, and
   the pause panel stays visible underneath. The `/options` sprite settles
   on frame 22 ('stop') with 5 objects: the gamma panel (`gamma_board`,
   `lure`, `m_GammaTuto_`, a `looper`) and `menu` at (70,130) px with
   `bt_0..bt_2` (each `txt` bound to `../:txt_bt`). Next: drive those
   buttons like the entry squares, then the subpages (`save / audiovideo /
   help / credits / language / controls / game_settings / display / audio /
   video / gamma / device`; 316 frames). The category icons are sprites
   stuck on their authored default frames (same family as the pc_btn
   problem in 3). `attachMovie('specificHelp')` has no export in the PC
   table — buttons render without their icon, probably retail-faithful.
5. **Lighting/shadow camera bug.** Driving shot: a hard-edged darker
   rectangle lies over the road ahead of the car — the shadow camera's
   frustum boundary is visible
   (`docs/images/user-report-drive-shadow-rectangle.webp`). Garage close-up:
   striped banding across the tire under the fender (acne/bias or a cascade
   boundary, `user-report-garage-tire-banding.png`). Code:
   `source/render/eden/eden_renderer.{h,cpp}` — `pShadowRealTime`,
   `setShadowCasterMode()`, `setShadowRealTimeTexture()`, caster cache
   (cleared by sector streaming since `d1b543e`). First step: visualize/log
   the shadow frustum per frame and check it follows the view; then bias /
   the TD_ShadowBox skip.
6. **Opening screen fps slowish.** Measured (perf session below): opening
   88.6 ms/frame default; avatars dominate (-nopnj 86 → 24 ms); -noshadows
   66.9; -noprojectors 60.7; all three 31.7. Next: the skinning + caster
   cost of the two party avatars and the mirror/refraction passes they feed.
7. **Car physics tips over too easily.** `source/physics/vehicle_dynamics.cpp`
   + `vehicle_data.h` (defaults: `StabilizerBarStiffness` 16000 N/m,
   `Mass` 1500, `HavokCenterOfMass` from the vehicle data). Likely too-high
   centre of mass / weak anti-roll. Verify the real per-car values from the
   XMB data (the CarVSTConfig pipeline from the audio work is the pattern
   for that extraction).
8. **Back-on-track: instant, no UI.** A single press teleports
   (`gm_freeride.cpp:582` → `backOnTrack()` :384). Retail: hold the button,
   a radial progress element fills, then the reset. Gate on ~1 s hold and
   draw the radial (Renderer2D arc pattern already in
   `freeride_hud.cpp:277`).
9. **First drive ends frozen on white.** In code: after 90 s of driving
   FREERIDE enters `eState::Fade` and logs "story step 3 'Meeting Tess' not
   implemented" (`gm_freeride.cpp:597`); nothing follows → the white window
   (`docs/images/user-report-white-screen-after-drive.webp`). **FIXED in the
   seventh session (`bd5c4bb`)** — retail's C_FADE_FREERIDE is an in-engine
   cinematic, not a video; it now plays (see the seventh-session section
   above).
10. **Misc graphical oddities.** From the same shots: the HUD clock is stuck
    at 00:00 with a stray glyph at its top-left (game time / day phase is
    not fed to the clock movie); the shadow frustum rectangle + banding
    (see 5); hair aliasing (see 1). "Some camera related" — no specifics
    yet: get repro screenshots from the user before touching the chase
    camera (`chase.update` / `nextMode()` in gm_freeride).

Headless repro recipe (glm workspace, `build-glm`; do NOT rebuild
`win\build` while the user's game runs from it — the exe is locked anyway):

    build-glm/bin/opentdu2.exe -game C:/TDU2-Gamedata/TestDrive2 \
      -userdata C:/temp/otdu2-glm-userdata -intro -walkmode 1 -fixedtimestep \
      -logpause -exitframe 700 \
      -autoplay "300:press=pause;380:press=down;420:press=validate;600:shot=C:/OpenTDU2-glm/_x.ppm"

Drop `380:press=down;420:press=validate` for the plain menu shot. PPM → PNG:
`python -c "from PIL import Image; Image.open('_x.ppm').save('_x.png')"`.
The log lands in `opentdu2.log` (CP1252: use `grep -a`).

## Session 2026-10-27 (sixth, `glm`): the retail freeride entry row

The user asked for the entries they remember from retail ("editor and
camera"): both are in the movie. `setSquareMenu` (4343 bytes; its
disassembly is in any -logpause run) switches on `/:WhatGameMode` — mode 2
(freeride / explore) builds resume / game_progress / players /
[xbox_live_party, X360 only] / photo / gme / options / [profiles, PC only] /
exit_game, each pushed with its lock variable (LockResume, LockGameProgress,
LockPlayers, LockSnapshot, LockEditor, LockOptions, LockProfiles,
LockExitGame). 'photo' is the camera (m_TakeSnapshot_), 'gme' the editor —
its specificIcons frame is the gear the user recognised on the options
screen (issue 4). Race modes use another list (stop_test / restart /
quit_race — decoded the same way when those modes exist).

- `kEntries` (pause_menu.cpp) now lists all eight with the retail locks:
  game_progress / players / photo / gme / profiles render **locked**
  (open_lock grayed states) until their features exist; the modes still
  control resume / options / exit through `setEntryEnabled`. Validating a
  locked entry does nothing.
- `/:WhatGameMode = 2` is set at load (the movie's own switch input).
- Left / right navigate the row now (up / down kept); locked entries are
  skipped by both keyboard and mouse hover.
- Loc ids via -dumploc: "Game progress" 605, "Players" 3868, "Take photo"
  957, "Editor" 129, "Profiles" 1386, "Exit game" 1605.
- Verified headless: 8 squares across the top (stage x 353.6..926.4, 82 px
  pitch), every icon renders (offsets of issue 3 excepted), selection skips
  the locked entries, the selected square's tooltip follows ("Exit game"
  renders complete; short labels still clip left in the ttip box).
- Non-selected squares' ttip boxes open empty (the 'open' state animation
  expands them): candidate fix — clear txt / park ttips of unselected
  squares in refreshEntries.

**Follow-up, same session (after the user posted retail PC screenshots —
`docs/images/retail-pause-*.png` are the reference now):** the square state
machine is fully decoded (sq0 timeline, 208 frames): default(0, stop) /
open(8) → stop(15) / on_h(20) → stop_on_h(32) / valid_h(42) → stop_on_h /
out_h(62) → stop / on(68) → stop_on(80) / valid(90) / out(109) → stop /
close(115) → default / open_lock(123) → stop_lock(132) / on_lock_h(140) /
on_lock(168) → stop_on_lock(181) / out_lock(192) → stop_lock / close_lock(198)
→ default. The runtime already plays label → stop correctly.

- **Centred icons**: the corner-offset glyphs were a STALE TRANSFORM left on
  `icLoaded` by the movie's own scripts (the place tags read (0,0) while the
  instances carried e.g. (−770,−770) — script-set, so a gotoFrame rebuild
  never resets them). Writing `icLoaded:_x/_y` — even with 0 — resets it.
  `kEntries` carries small per-art px residuals (`icX` / `icY`) that centre
  each glyph; tuned against `_zc_pm_ship.png` vs the retail shot.
- **Tooltip on the selected square only**: unselected ttips are hidden and
  parked at x 2000 (their 'open' state animation expands the box without the
  movie's scripts to fill/hide it). The selected bubble renders with its
  `a_` glyph ✓.
- Still open on the row, evidence preserved for the next session:
  - the **blue panel** above sq0/sq1 (stage ~377..545 × 137..180): NOT the
    sqN ttips (parked at x 2053, verified live), NOT character 18536 (that is
    the tile art of the 'open' state — hiding it removes the tile), NOT
    /menus. It predates the entry-row work. Probe: `-logpause` "instances in
    the box region" (DumpWorldRegion) + the placement-matrix dump.
  - the **selected square renders black, retail red** — `anim_on` (sprite
    18524, placed by the 'on' state at frame 68, scale (0.52, 1.58)) probably
    needs its internal timeline driven like the ttips, or its colour
    transform is misapplied.
  - **sq0's tile goes black** after select-then-deselect: stale colour
    transform across the backward rebuild (same family as the icon offsets —
    the rebuild resets placements but not script-set properties).
  - short tooltip labels clip one character ("Options" → "ptions").
- Casino pause (retail screenshot `retail-pause-casino.png`): 6 entries —
  resume / game_progress / players / options / profiles / exit_game, no
  photo / gme. Matches the IsCasino branch of setSquareMenu; implement when
  the casino mode exists.

## Session 2026-10-27 (fifth, `win`): merge from `glm`, pause menu options + icon row

**Merge** (`430cd68`): `git fetch glm; git merge glm/glm` brought the mods/
overlay (`5910c2f`), the working pause-menu entries (`f8b456c`) and the glm
session notes into this workspace. Auto-merged `docs/STATE.md`, clean build.

The user's pause-menu report: *"the menu icons should run across the top, the
pause menu should look similar to the xbox version without the xbox party
thing"* — plus "functionality first, performance later".

1. **The entry icons are a row across the top now.** MAIN.vmf authors the
   squares as a *vertical column*: `sq0..sq8` at `menu_sq` local
   (48, 48.5 + 96.3 n) px, `menu_sq` at stage (640, 343) scale 0.85 → the
   entries sat at x≈681, y 264 + 82 n, so sq6..sq8 fell below the 16:9 band.
   The console frontend (what the user is comparing against) has them in a row
   at the top, so `PauseMenu::layoutEntries()` imposes one:
   `sqN._x = (n - 1) * 96.3`, `sqN._y = -197.5` in menu_sq local px, re-applied
   **every frame** (the squares' state animations write their own matrix back).
   Verified numerically with `-logpause`: entries at stage (558.2, 175.2) /
   (640.0, 175.2) / (721.8, 175.2) → window y 55.2, centred, 82 px apart.
   Tuning constants are `kRowPitch` / `kRowY` in `layoutEntries()`.
2. **The Xbox Live party entry is not part of this build.** The movie's own
   `setSquareMenu` (disassembled) builds
   `resume / game_progress / players / xbox_live_party / snapshot / achievement
   / leaderboards / options / profiles / exit_game` per game mode, and adds
   `xbox_live_party` only on the X360 path. This build has no online features,
   so the menu keeps the three entries that do something (Resume / Options /
   Quit, ids 213 / 466 / 519); the others need progression / online data.
3. **The Options entry opens the options screen** (it did nothing before: the
   movie fires the `opin` FSCommand, which was not mapped). `LaunchOptions`
   (disassembled) does `_root.IsInOptions = true; _root.gotoAndStop('pause_stop');
   FSCommand('opin'); _root.options.gotoAndPlay('open')` — the game now drives
   the same steps directly (`openOptions()`), because the movie's own menu glue
   is unusable in this runtime. Verified: the `/options` sprite settles on
   **frame 22** ('stop') with 5 objects, BACK returns to the entry row
   (`closeOptions()` → `/options` plays 'close' and the movie fires `opou`),
   ESC closes the whole menu. `opin` / `opou` are also handled in the
   FSCommand callback now.
   The options page's content: a `gamma` panel (`gamma_board`, `lure`,
   `m_GammaTuto_`, a `looper`) and `menu` at (70, 130) px with the buttons
   `bt_0`..`bt_2`, each holding a `txt` field bound to `../:txt_bt`.
   **Next for the options screen**: drive `bt_0..bt_2` (labels + states) like
   the entry squares, then the subpages (`save`, `audiovideo`, `audio`,
   `video`, `gamma`, `controls`, `language`, `display`, `device`... — the
   labels of `/options`, 316 frames).
4. **New `-logpause` tooling** (this is what decoded the above): the dump now
   disassembles *every* root user function with its constant pool resolved
   (`FlashPlayer::getFunctionBody` / `getFunctionConstants`), and
   `DumpSubtrees` dumps any subtree by name filter. The FSCommand vocabulary
   of the frontend is now readable: `opin` / `opou` (options in / out),
   `mopn` / `mcls` (menu open / close), `sndp:<name>` (sounds: mopen, ttip,
   valid...), `cloc:open`, `dbxo`, `mcou`, `exit:vali`, `exit:canc`,
   `exit:open`, `paou`, `reco`, `rcod`, `goed`, `prof`, `fain`, `rest`,
   `pdel`, `egam`, `caed`, `vtke`, `sbdd`, `casi:ldbd`, `casi:achi`,
   `phmo:phin`.
5. Known gap on the options page: the movie calls
   `attachMovie('specificHelp')`, which is **not in the PC export table**
   (`specificIcons` is) → the options buttons render without their icon,
   probably the same in the retail PC build.
6. Still open for a "100% pause menu": the options widgets above, the entry
   click sounds (`sndp:*` — the movie fires them, nothing plays them yet),
   the objective line (`/:m_Objectif...`, needs progression data), the profile
   avatar photo (a 3D portrait render), and `eResult::Options` is now unused
   (the options screen is internal to the pause menu, so the game modes never
   see it — which also removes the "game modes ignore Options" problem).

## Session 2026-10-27 (fourth, `glm`): mods/ overlay + pause menu entries — committed

**Mods overlay** (`5910c2f`): see the section below (kept for the merge flow).

**Pause menu works** (`f8b456c`): the entry column renders, navigates and
validates, in the second workspace after merging `win` (`10b32b9`).

- Root cause of the dead entries: sq0..sq8 are 208-frame state machines
  authored to `stop()` at frame 0 (alpha-0 content) waiting for menu_sq's
  ActionLoop `tellTarget` glue; the root's `setSquareMenu()` (frame 35)
  returns early in this runtime (`numSq` never set; guard not chased) and
  `root.Input()` is *never defined by the movie* — the old
  `invoke("/", "Input", …)` + `sendKey` fallback could never work.
- The game now owns the menu: `PauseMenu::refreshEntries()` drives each
  square with `gotoAndPlay('on'|'open'|'on_lock'|'open_lock')` (the labels
  decoded from the sq timelines), parks/hides unused squares, picks the
  per-entry icon frame of the attached `specificIcons` symbol (`resume`,
  `options`, `exit_game`) and sets the tooltip text via the square's `txt`
  variable (+ `/:txt`). Validate plays `valid` and reports the result;
  Options also invokes the root's `LaunchOptions` — **the movie attaches its
  OPTIONS screen and the `opin` FSCommand fires** (the options UI itself
  renders only partially: more AVM1 work, see below).
- Map badges: the i1/i2 sprites under `data_extd/map/island` have island
  frames (`h1..h4`, `h`, `i1`, `i2`, `i`, `casino`); gotoAndStop `i1`/`i2`
  → the pause map reads **IBIZA 1 / IBIZA 2** (was the authored HAWAI
  default the user reported).
- Mouse: new `FlashPlayer::getClipStagePosition` (world matrix of a clip
  chain, stage px) — hover selects, click validates (stage y 120..840 band).
- `-logpause` keeps the dumps and gained an **AVM1 disassembler**
  (`DumpActions`: constant pools, Push, DefineFunction, GetURL/FSCommand,
  jumps) — that is what decoded the whole protocol; keep it for the rest of
  the frontend work.
- Verified headless in FIRST_WM (`-intro -walkmode 1` + autoplay pause/
  down/validate): icons+tooltips render, selection moves (`sndp:ttip` fires
  from the square scripts), options screen opens, ESC closes, resume/reopen
  clean.
- Left (next session, best first): the OPTIONS screen widgets (the sprite
  renders partially; its per-page scripts need the same treatment), the
  profile avatar photo (3D portrait texture), the objective line
  ("Win C4 championship" needs progression data), entry click sounds via
  `sndp:valid`/`sndp:wrong` (getURL FSCommands the square scripts fire when
  the movie drives them — our C++ driving bypasses those paths), and the
  game modes ignore `eResult::Options` (FREERIDE/FIRST_WM switches).

**Feature assessments for the user (same session, not implemented):**

- *Custom radio stations* (`Radios/<Station>/` folders with music + logo):
  feasible, medium size. Ground truth: the original radio is
  `sound/radio/setup.xmb` (ProjectExport/Tracks: station-prefixed ROADROCK_*,
  Hariba jingles, ADS-*, TIME_*/WEATHER_* IDs with durations/markers) +
  `music/*.bnk` songs; the mixer already has a **"Radio" client**, the
  keymap already binds RADIO_CHANNEL / RADIO_VOL_LESS / RADIO_VOL_MORE
  (keys 1/2/3), but **no radio playback exists in this build yet**. Plan:
  (1) a small RadioPlayer playing wavs through the Radio client with
  channel cycling on the existing actions, (2) custom stations enumerated
  from `mods/Radios/<Name>/*.wav` (+ `logo.png`/`.dds`, needs stb_image or
  a DDS/DXT loader ~200 lines) added to the cycle with name/logo on the HUD,
  (3) later: drive the original stations from setup.xmb (dj/ads/jingles).
- *Graphics settings*: most knobs already exist as variables/flags —
  GlobalLodFactor (Quality_Settings.cpr → eden_world culling), FOV
  (EdenSceneView::Camera + live ImGui slider in the world viewer), shadow
  configs table + `-noshadows`, ocean/reflection toggles (`-noocean`,
  `-noreflclip`, `-noactorreflect`…), `-cubemapinterval`/`-staticcubemap`,
  `-novsync`, `-sectorradius`, `-envquality`, window size/fullscreen.
  Packaging: (a) read a settings file at boot and apply (small),
  (b) live ImGui "graphics" panel like the world viewer's (small-medium),
  (c) the original Flash options screen wired to real settings (larger;
  blocked on the OPTIONS widget work above).

## Session 2026-10-27 (fourth, `glm`): mods/ overlay — committed as `5910c2f`

The next-actions item "feat(filesystem): mods/ overlay" from the third
session, done in the second workspace after merging `win` (`10b32b9`).
Everything a mod needs now has one choke point: all game reads go through
`GSFile::resolvePath` (`source/filesystem/gs_file.{h,cpp}`), and writes are
separate (logger, screenshots, shadercache).

- A file at `<modsRoot>/<same path as the game file>` shadows that game file
  for every read: `fileExists`, both `loadFile` overloads and `listFolder`.
  `modsRoot` = `<user data>/mods` by default (so two copies with different
  `-userdata` do not share mods), `-mods <dir>` or `OTDU2_MODS_DIR` overrides,
  `-nomods` disables. Paths resolve case insensitively inside the overlay
  exactly like the game tree (`resolveCaseInsensitive` now takes the base
  root). Overlay hits are logged: `Mod overlay: '<game path>' -> '<file>'`.
- `listFolder` merges overlay entries with name dedup (overlay replaces the
  game file of the same name, extra files are appended) — the shader library
  enumeration (`eden_shader_system.cpp`, the only listFolder caller) picks up
  modded `.sob` libraries. Mods folder entries are NOT hot-reloaded: results
  are cached in `resolvedPathCache` like any resolution, so changes need a
  restart.
- Read-only by construction: nothing writes into the overlay.
- **Verified in-game** (headless, `-exitframe 150`, glm `-userdata`): baseline
  decodes `sound/musics/login.wav` and plays it; a 200-byte truncated
  `login.wav` at `mods/Euro/Bnk/sound/musics/` is resolved through the overlay
  (hit logged) and FAILS to decode — the content came from the stub, not the
  game file; `-nomods` with the stub still present decodes the game file
  again. Test stub removed afterwards.
- Untracked scratch of this session: `_mod_*.out/.err`, `_mod_build.out/.err`
  (covered by the `_*` .gitignore).

Not done (natural follow-ups): a `mods/` README or first mod as example, mod
load-order/named-mod subfolders (e.g. `mods/<modname>/Euro/...` merged in
order), listing mods in the log at boot.

## Session 2026-10-27 (third): user report sweep — all committed on `win`

User complaints → what was done (each item its own commit, build + in-game
verified):

1. **Baseline**: the previous session's uncommitted rendering/pause work
   committed as `d568c60` (skinned shadow casters, shadow config table, water
   refraction/props, sector shadow-tunnel skip, pause menu + AVM1 scope chain).
   NOTE: `source/game/pause_menu.{h,cpp}` had been *untracked* — always
   `git status` untracked files before assuming a feature is committed.
2. **Audio — main menu music + car audio (were silent)**: `769ad27`. The mixer,
   decoder, device were all implemented; only the callers were missing.
   LOGIN plays `Euro/Bnk/sound/musics/login.wav` (loose wav, FrontEndMusic
   client 8). `VehicleAudio` (source/audio/vehicle_audio.{h,cpp}) parses
   `CarVSTConfig.xmb` from `sound/vehicles/<car>_high.bnk` (57 samples on the
   California) and plays engine Load/Unload layers (RPM volume/pitch + load
   curves, load scale ≈ 470/485 per car), start one shot, transmission/aero/
   rolling loops (speed axis), skid burn/slide (wheel slip axes 26..29 /
   34..37, road/dirt/grass families), gear shifts. `tools_probe/carvst_probe.py`
   dumps the config values. FREERIDE sets `setGameMode("FREERIDE")`.
3. **Gamepad — most of it was dead**: `f4c97e5`. GSInput now loads
   `DeviceX360Pad.xmb` from the game root (26 bindings; DInput X360 channels
   BUTTON_0..11 / STICK_0/1_* / POV / dpad → SDL; keyboard channels in the same
   file are skipped). Original semantics restored: X/square = handbrake (was
   wrongly B), B/circle = BACK_ON_TRACK + menu back, RB = camera, LB = back
   view, avatar move = left stick, look = right stick. BACK_ON_TRACK had *no
   consumer at all*: GMFreeRide::backOnTrack teleports onto the closest
   streamed road triangle (<=150 m, ±30 m vertical, longest-edge heading,
   5 m/s) — bound to pad B/circle and keyboard R; verified in game (12.8 m
   hop onto a road, aligned). autoplay gained `backontrack`.
4. **Walk through people/car — fixed**: same commit. WalkMode::collide now
   also pushes the body sphere out of PNJ capsules (scene-local positions via
   `IntroCinematic::getPnjScenePosition`, `sceneOrigin.inverse()`) and of the
   scene car (sphere vs oriented BODY_HR bounds). `-logwalk` logs eye +
   nearest-person distance. ClosestPointOnSegment exported from
   havok_collision.h (vehicle_collision.cpp had its own copy — ambiguity).
5. **Car physics — concrete fixes**: `06b824c`. sphereContact now detects
   spheres up to 1 m under the heightfield (was 0.3 m — the car sank through
   hills), wheel rays starting under the terrain report t=0 contact. The
   drivetrain itself is fine (-vehtest: 0-100 in 4.52 s vs DB 4.10, top
   309.7 vs DB 310); the remaining "feel" work is the [INF] suspension/chassis
   solver in vehicle_dynamics.cpp (see its header note).
6. **Pool reflection smudge — fixed**: `4ee43bf`. All GSReflection layers
   render into ONE screen-sized RT and the TD_Water shader samples the whole
   RT as its refraction (85 %). The actor-layer mirrors (GROUND2 floor, at
   house-local y=37.49) drew AFTER the refraction and overwrote it wherever
   the floor-mirrored actors covered the pool on screen → the giant displaced
   smear in the water. Order flipped (actors first, REFLECT 2D last). Verified
   on C_INTRO frames 2600/3000 (the pool party shots; the intro montage is
   ~3200 frames at -fixedtimestep — use -autoplay "N:shot=..." for many
   frames in one run; ALWAYS pass -exitframe or the game never quits).
   Registered projectors (seen in the log): eph_l3di has ?_REFLEC mirror +
   REFRACT (no actor layers: the original skips actor layers for EPH/ECAS
   banks); new_intro has GROUND2 = actor layer (dance floor).
7. **Lighting flicker — env cube made coherent**: `568fea8`. The one-face-
   per-frame cube mixed six instants; the car reflections shimmered every
   frame while driving. Now all 6 faces render together, only when the car
   moved >0.5 m / camera turned >10°, at most every 200 ms (500 ms parked).
   Same average frame cost (freeride 52 vs 54 ms). The second suspect from
   the previous session (sun-visibility readback, eden_sun.cpp) was reviewed
   and looks correct — not touched.
8. **Pause menu**: `b8cf5d0` + `990c509`. `-dumpbuttons` (gs_flash.cpp) dumps
   every DefineButton2 + the Push strings of the action bytecode (Eden VMF
   opcode map: Push = 0x96, GetMember 0x1c / SetMember 0x4e / Pop 0x17 are
   standard SWF). Decoded: the root nav BUTTON 183 handles key codes
   **14 up / 15 down / 13 validate / 8 back** (the old 9/10 were guesses).
   The sq0..sq8 entries are NOT buttons: AVM1 sprite clips; the movie
   protocol is `_root.Input(code)` (its own round buttons call it),
   `_root.savedSq` selection, `Clic(id)` handlers, `OpenMenu`, and the
   options screen is the exported sprite `OPTIONS` (749) attached by script.
   PauseMenu now calls `invoke("/", "Input", ...)` with the corrected codes.
   The player-title text field is bound (`/:m_PlayerTitle` = "") so the panel
   no longer shows the literal `O-:/m_PlayerTitle`.
   **Left (next session, best first)**: the AVM1 interaction layer — the
   entries react to Input() only if the runtime executes their frame scripts
   (savedSq / Clic / OpenMenu / attachMovie("OPTIONS")). Verify whether
   invoke("/", "Input", "15") reaches a Function (getMember root "input");
   the sq clips may need their instance scripts run + clip event handlers
   (onPress/onRelease/onRollOver assigned by script) triggered from mouse /
   Input. The visible pause screen today = profile panel + map + the two
   round buttons; the entry column (menu_sq, x≈688 px, y 271 + 96·n) is not
   visibly drawn (two instances: depth 108 visible, 105 not).
9. **Input injection**: autoplay `hold/release=mouse_left` now produces the
   wasMouseClicked edge (gs_input tick); injected clicks are usable for UI
   tests. **The pause entries do not react to clicks at (688,271/367/464)** —
   they are not DefineButton2s (sendMouseToClip traces with
   OTDU_FLASH_MOUSELOG=1: it only found anim/timer/looper/pause_menu
   buttons).
10. `47d91ab` .gitignore for the `_*` scratch; `c292dc0` tools_probe scripts.

## Session 2026-10-27 (fourth): the opening scene (user report)

User report: "oddities with ... camera related, mostly shadows, odd geometry
pop ins, body parts disappearing, this mostly all happens in the opening
scene; when you leave the party the car is semi not rendered but as it pans
around what wasn't there starts showing; the opening scene seems very low fps
and once you leave the fps jumps massively."

1. **Committed the pending work first** (`10b32b9`): the planar-reflection
   mirror-plane clip + the `-nosunflare` / `-logshadows` switches.
2. **Sectors were never unloaded** in the intro or in freeride (only the world
   viewer called `EdenWorld::unloadFar`). Fixed: `IntroCinematic::updateView`
   and `GMFreeRide::streamWorld` now drop the ring one sector beyond the load
   radius (hysteresis) and call `clearShadowCasterCache()` when something was
   dropped (the caster / Z-pass caches hold material pointers into the sector
   objects). Verified: `EdenWorld: 5 sectors unloaded, 21 loaded` during
   C_INTRO; 27 loaded where the old build reached 32.
   NOTE the intro's growth is *bounded* (the three camera centres of C_INTRO
   are sector (46,7) / (46,9) / (47,8), so a radius+1 = 3 window covers all of
   them) - the 9 -> 24 -> 26 -> 32 growth is the union of three 5x5 load
   blocks, not a leak. Freeride *is* unbounded (a long drive keeps every
   sector ever visited).
3. **`updateAreaIndices` was O(cells x loaded sectors)**: a 255 x 255 cell scan
   calling `isSectorLoaded` (a linear walk) per cell, re-run for every area
   marked dirty - and `loadSector` marks areas dirty, i.e. every streaming
   step. Now hashes the loaded sector keys once (same output).
4. **Log spam**: `EdenRenderer::draw` logged "shadow caster material ...
   skipped" once per *name change*, and the intro house has two of those
   materials drawn by every pass -> 21,300 of the 21,452 lines of a 3,500
   frame run (3.1 MB log, one synchronous write each). Both that skip and the
   TD_ShadowBox one now log once per name per run.
5. **Measured the opening scene** (RTX 5070 Ti, 1280x720, `-intro
   -fixedtimestep -exitframe 400`, avg of frames 240-400):

   | build | frame | note |
   |---|---|---|
   | default | 88.6 ms | 9 sectors, C_INTRO "generique" |
   | `-noshadows` | 66.9 ms | 3 slices = ~22 ms |
   | `-noocean` | 72.0 ms | ocean reflection = ~17 ms |
   | `-noprojectors` | 60.7 ms | GSReflection = ~28 ms |
   | all three off | 31.7 ms | = the title scene's 30 ms |

   So the intro is not one broken pass: it is the *sum* of the optional world
   passes, each of which re-draws the whole world + the house + the 31 skinned
   avatars (1557 draws in the main opaque pass alone; `-showdraws`). The
   title scene (LOGIN) never enables GSReflection, which is exactly the 28 ms
   difference. The largest single lever left is the two GSReflection actor
   passes (a full props+PNJ+car re-draw each, no projector culling).
6. **Frame determinism (needed for any A/B screenshot work)**: two runs with
   identical flags and `-fixedtimestep` produce *byte-identical* frame 900
   (`Get-FileHash` equal). BUT the PNJ avatars of the party are *randomly
   generated per run* (intro_spec 0.4: every WOMAN_xx / MAN_xx gets a random
   avatar), so A/B images differ by ~16 % of the pixels on the *people* alone.
   Compare regions without avatars, or use `-nopnj`.

7. **Not reproduced** (no evidence found, see the notes above): the
   "body parts disappearing" and "the car is semi not rendered / appears when
   the camera pans" reports. A first read of a party frame looked like the
   dancer's forearm was cut off; at 3x zoom the arm is complete (the sleeve /
   shadowed skin boundary), and the two "different" frames were the random
   avatars. Still to check for the car: the C_FIRST_OUTRO camera orbit with
   `-cinetime` (`CinematicCar::draw` LOD 4 under 5 m: `cockpitLowNodes` hidden
   + the `interior` model drawn; `bHardtop` -> `drawSkinned(hardtopSkin)`),
   and `-showshadowbuffer` / `-showviewpos` for the shadow side.

## Verified

- **Build**: `cmake --build build --parallel` → all three exes relink, no errors.
- **Run**: `build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -exitframe 150`
  → reaches `GAME MODE : LOGIN` then `AVATAR_CHOICE`, exits cleanly.
- **`-userdata`** confirmed in the log (`gs_file.cpp:120`) *and* by the real
  `shadercache\` folder it created under the override path.
- **Second workspace**: `build-glm` configured + built → `build exit: 0`, three
  exes produced, `opentdu2.exe` the same size as the primary's.
- **Simultaneous run**: both workspaces launched at the same time, each with its
  own `-userdata` → both reached `GAME MODE : AVATAR_CHOICE`, and each wrote its
  own `shadercache\` under its own path. No collision.
- **Merge flow**: exercised in both directions; `win` → `glm` merged cleanly as
  `b7786b9`.

## Not verified / unknown

- Nothing known-broken in the workspace setup itself.
- Anything past AVATAR_CHOICE in this build. FIRST_WM, FREERIDE and
  WORLD_VIEWER were verified in earlier sessions, not since these commits.

## In flight

Nothing - the fourth session's opening-scene fixes are committed (see the
fourth-session list above).

Follow-ups found on the way (not done):
- **The party avatars are the opening scene's real cost**: `-nopnj` takes
  frames 240-400 from 86 ms to **24 ms** (41 fps). 31 skinned avatars x 4
  parts x ~10 items are drawn in *every* pass (Z pre-pass, 3 shadow slices,
  ocean reflection, 2 GSReflection passes, main) with no per-pass culling at
  all. The prepared (and reverted, unverified) fix: `EdenRenderer::isSphereVisible`
  (the 6 frustum planes of the pass being recorded) + `EdenAvatar::getBounds`
  (union of the parts' bind-pose bounds, extended for the animated poses:
  -0.3 .. 2.4 m in Y, +-0.4 m in XZ) -> cull each PNJ in
  `IntroCinematic::drawSets`. Verify visually before shipping: a too small
  sphere makes avatars vanish. `-nopnj` is the A/B reference.
- `VehicleWorldCollision::streamAround` (source/physics/vehicle_collision.cpp)
  still only *adds* sectors: `loadedSectors` and the flat triangle / box /
  capsule vectors grow all session while driving. Queries are cell-hashed, so
  it is memory, not query time, but it needs per-sector ranges to unload.
- The two GSReflection actor passes re-draw every prop, PNJ and the car with
  no projector culling (~28 ms of the intro's 88 ms). The projector already
  knows its model nodes; culling the actors against the projector's world AABB
  is the next perf lever for the party.
- `-showdraws` = 1557 draws in the main opaque pass alone for the party: the
  intro draws the world in 6 passes (Z pre-pass, 3 shadow slices, ocean
  reflection, main), each re-walking every instance of every loaded sector.
- Sector banks are loaded **synchronously**: the intro shows one-off update
  stalls of 1.1 s (frame 120) and 3.3 s (frame 360) when a camera cut loads a
  new ring. Async streaming is the real fix.

New debug flags (cumulative): `-showsearefl`, `-noreflclip`, `-norefractactors`,
`-noactorreflect`, `-dumploc <substr|*>`, `-dumplabels`, `-dumpbuttons`,
`-shadowboxes`, `-shadowcasters`, `-logpause`, `-logmaterials`,
`-logflashvars`, `-logwalk`, `-logdrive`, `-backontrack` autoplay action,
`OTDU_FLASH_MOUSELOG=1` (flash button hit tracing). Filesystem:
`-mods <dir>` / `OTDU2_MODS_DIR` / `-nomods` (see the fourth session above).

Still open from the second session: **ocean reflection** — the SEAREF_N
buffer is *not* empty (verified with the new `-showsearefl` blit: the
mirrored village band is there) and the oblique near-plane clip is *not* the
cause (`-noreflclip` gives the same image), so the "flat dark water" look
comes from the fresnel / `reflMat` weighting, not from a missing render.

## Next actions, in priority order

0. **Pause menu — implemented** (`source/game/pause_menu.{h,cpp}`, hooked in
   FREERIDE, FIRST_WM and AVATAR_CHOICE (the walk mode of the intro party —
   the mode the first play session used, which is why ESC "did nothing"
   before): `PauseMenu::load()` in enter, ESC (`IA_MenuPause`) opens / closes,
   the world is frozen while `isOpen()`, quit returns to LOGIN. It drives the
   *root* timeline of `FrontEnd/HiRes/main.bnk` → `MAIN.vmf` (`-dumplabels`):
   `pause_in` → stops at `pause_stop`, `pause_out` / `pause_out_direct` to
   close (the world stays frozen for `kCloseDuration` = 0.5 s for that
   animation). Entries are the root variables `/:m_Pause`, `/:m_Resume`,
   `/:m_Options`, `/:m_Quit` (set from the database) and `/:LockResume`,
   `/:LockOptions`, `/:LockQuit`; `/:IsInOptions` for the options screen.
   Verified with `-logpause`: the root plays `pause_in`, stops at **frame 39
   with 14 display objects** (the menu is up), ESC closes it — in FREERIDE,
   FIRST_WM and AVATAR_CHOICE. Localisation ids (`db_us.cpr`, `-dumploc`):
   Pause 51, Resume 213, Options 466, Quit 519.
   Also in this pass: the **AVM1 scope chain** (`flash_player.cpp`,
   `lookupVariable`: a plain name read from a nested clip now resolves in its
   parents up to the root — the frontend reads the game variables both as
   `/:m_Resume` and as a plain name) and the pause screen's player panel
   variables (`/:AvatarName`, `/:Money`, `/:NbCars`, `/:NbHouses`,
   `/:ClubLevel`, `/:state_online`, `/:voice_on`).
   **Left**: the FSCommand names of the three entries (opening only sends
   `sndp:mopen`, `cloc:open`, `mopn`; every FSCommand is logged, so one click
   on an entry tells which command to map to Resume / Options / Quit), the
   button key codes (up / down / back are [INF]: 9 / 10 / 14), the options
   screen (`pause_back`, `options`, `LaunchOptions`), and the `db_*` /
   `m_PlayerTitle1` / `CasinoAvatar` variables the frontend expects from the
   database (still undefined: `-logflashvars` lists them).
   `-logpause` also dumps the pause screen's display list, which gives its
   structure: root `[187] 'pause_menu'` at (0, 2400) twips (= stage y 120, the
   top of the 16:9 window) with `bkgd`, `mask_data` (mask 106), `data_extd`
   (area / map / casino / clock), `clock` and **`menu_sq`** → `sq0`..`sq8`
   (the entry squares, 96 px apart from stage y 271; `sq6`..`sq8` fall below
   the 16:9 window, y > 840). Each entry is `ttip` (shape + `a_`) + `ic`
   (the icon, `ic.attachMovie('specificIcons', 'icLoaded', 0)` — that export
   exists, so the icons load; only `specificHelp` is missing from the export
   table, `-dumplabels` prints it). The entry *labels* are what still needs a
   look: with `-no3d` (UI only) the panel shows the profile (name/money), the
   clock and content in the entry area, but the label text of the entries has
   not been verified.
1. **"2 black quads under the DJ decks" — found and skipped.** They are the
   two `<sector>Shadow_tunn` meshes of the streamed world (`5067Shadow_tunn`,
   `5160Shadow_tunn` in the intro party — sector 5067 is the house), drawn with
   the **TD_ShadowCaster_Only** material (`ac78df32bc4589df`, handler
   0x00d70f30 → bucket **0x38**). Those are the *sector shadow map* casters:
   the original never draws them in the visible scene (bucket 0x38 is not a
   render layer) — one of them has no texture at all, hence the solid black
   quads. `EdenRenderer::draw` now skips them in the scene (the PSSM caster
   pass builds its own material and is unaffected); `-shadowcasters` draws
   them again. Confirmed in the log: `Eden: shadow caster material
   '5067Shadow_tunn' skipped in the scene`. (The earlier TD_ShadowBox skip —
   `ShadowBox` / `ShadowBox_01` of the title/freeride car — is unrelated but
   the same kind of bug.)
   Identification tool: `-logmaterials` prints every distinct material drawn
   (name, permutation, index count, blend, bucket, slots and their textures;
   `NONE` = missing texture).
3. **Shadows of the intro party (user report: "the characters have no shadow
   until the select screen").** Verified: the avatars *do* get the skinned
   caster during C_INTRO (`-logmaterials` + the new caster log:
   `Eden caster 'EYES' -> TD_ShadowCaster vs7/ps1`), and the intro's shots
   apply their own configs (`-logmaterials` is not needed: the new log prints
   `IntroCinematic: shadow config 'CINE_DEFAULT' / 'PG' / 'PR'` — PG is
   1.6/90 m with bias 2.0/2.0/2.0/2.0 and 150 m cast distance, PR is 0.8/20 m,
   DEFAULT is 0.8/32 m). So the difference between the wide party shots and the
   select screen is the *config* (PG/PR vs CINE_DEFAULT): with PG the shadow
   map covers 90 m with a 4x larger bias, which washes out small casters.
   Next: compare a frame of the same moment with `-noshadows` using
   `-fixedtimestep` (the two runs must be frame locked; a plain A/B of two
   runs diverges and is useless) and check `-showshadowbuffer` (the sun
   visibility buffer shows the avatar silhouettes or not).
4. **"Camera cuts objects" (first person walk modes) — fixed.** The walk
   camera inherited the *cinematic* near plane (0.8 m, `FUN_00b3eba0`), so
   every wall / object within 80 cm of the eye was clipped open when standing
   close to it. `IntroCinematic::setLocalCamera` now takes a near plane and
   `WalkMode::resume` passes `kWalkNearPlane = 0.05` (the same value the game
   data uses for the garage cockpit camera).
5. **Lighting flickering (not reproduced yet).** Prime suspect: the dynamic
   environment cube now renders **one face per frame** (`-cubemapinterval`,
   default 1: `EdenSceneView::renderEnvironment`), so the car's paint/glass
   reflections are 1..6 frames stale and change every frame. Before that perf
   commit it rendered all six faces (`bAllFaces`). Compare
   `-cubemapinterval 6` / `-staticcubemap` with the default. Second suspect:
   the sun-visibility readback (`EdenSun::updateVisibility`, a 16x16 depth
   probe read `kFramesInFlight` later) driving the halo/flare alpha. Shadow
   shimmer is *not* a bug: the original has no texel snapping either
   (rain_shadow_spec B.4).
2. **"Camera cuts objects" (not reproduced yet).** Near planes come from the
   data (`CameraInGame.xmb` `nearProjPlane`: 0.05 inside views, 0.8 cinematic
   default) so it is probably not the near plane. Left: the Z pre-pass depth
   function vs the main pass (`setDepthOnly`, coplanar roads/decals), the
   heightfield LOD (`eden_world.cpp`), the far plane. Needs a repro: which
   mode, what gets cut.

3. `feat(config): -fov / -lodfactor / -sectors`. `GlobalLodFactor` already comes
   from `SET_GLOBAL_LOD_FACTOR` in `Quality_Settings.cpr` (`gs_config.cpp:44`)
   and drives culling in `eden_world.cpp:267`. `Fov` lives in
   `EdenSceneView::Camera`; the world viewer already has a live ImGui slider
   (20–110°) and ultrawide aspect correction exists in `eden_scene_view.cpp:230`.
4. `feat(filesystem): mods/ overlay` in `gs_file` — check an overlay directory
   before the bank path in `fileExists` / `loadFile`. Highest-leverage single
   change: it unlocks texture, shader, audio and UI mods at once.
5. `tools`: a **BNK writer**. `otdu2_bnk_tool` can `list` / `extract` /
   `decrypt` but has **no repack**, so nothing can be written back into a `.bnk`.
6. Only then: the offline casino (`source/game/casino/` + a local chip balance,
   driving `Cas_SlotMachine.vmf` through the existing AVM1 runtime).

## Traps — do not rediscover these

- Screenshots: `-screenshot <file.ppm> -screenshotframe N` or
  `-autoplay "650:shot=C:/x.ppm"`. **The intro's cinematic time advances with
  deltaTime**, so two runs at the same frame number show *different* moments
  unless `-fixedtimestep` is passed (then 1/60 s per frame, and two runs are
  byte-identical - verified); the title scene accepts `-cinetime N` instead.
  **The party PNJ avatars are random per run** (intro_spec 0.4), so an A/B of
  two runs still differs on every person (~16 % of the pixels): compare
  regions without avatars or pass `-nopnj`.
  Screenshots are binary PPM (P6): convert with
  .NET `System.Drawing`, and keep the JPEG/PNG **under ~100 KB** or it cannot
  be attached to a conversation.
- `EdenRenderer::getCasterMaterial` / `getZPassMaterial` cache per material
  *pointer* + Hashcode; changing a material's spec after the first lookup needs
  `clearShadowCasterCache()`.
- The `-show*` debug blits (`-showviewpos`, `-showshadowbuffer`, `-showenvface`,
  `-showsearefl`) replace the whole frame; the Flash UI is drawn on top, so the
  blit looks like a UI screen with a background.
- `source/flash/flash_movie.h:14` is **stale**. It claims the SWF display list /
  ActionScript runtime is not implemented. `flash_player.cpp` (93 KB) *is* the
  AVM1 runtime — timelines, variables, user functions, `random()`.
- Two git commands in the same repo at the same time collide on
  `.git/index.lock`. Serialise git; this already bit us once.
- Windows PowerShell aborts the rest of a command chain when a native command
  writes to stderr. Start chains with `$ErrorActionPreference='Continue'`.
- `SDL_GetPrefPath()` is shared by every copy of the game — always pass
  `-userdata` when more than one workspace can run at once.
- `cmake --build` can exceed a 30 s command timeout. Launch it with
  `Start-Process` redirecting to a log, then poll.
- **Ninja is not on PATH.** It lives at
  `C:\Users\cuz_c\Downloads\ninja-win\ninja.exe` (1.13.2). The primary build
  works only because its `CMakeCache.txt` holds the absolute path — a *fresh*
  configure fails with "unable to find a build program corresponding to Ninja".
  `build-glm.ps1` adds the directory to PATH and passes `-DCMAKE_MAKE_PROGRAM`.

## Session 2026-10-30 (`glm`): the Q7 Inuit wood trim is playable — committed

The owner was sent a press photo of the Audi Q7's white/wood "Inuit" interior by someone asking
about the prototype content; the ask: is the asset tool makeable as a standalone tool with 360
support, and what is the wooden trim? Answer for the trim: it is `WOOD_INUIT` (256x256 DXT1) in
`q7_inuit_i.bnk` of **both** 360 prototypes, alongside grey-dial `Q7_INT2` and the Inuit material
set (`Wood`, `Inuit` paint, `Plastic_White`, `Moquette`, `Red`, `Aluminium_02` — none in retail).
Decoded PNGs since 2026-10-29 at `C:\TDU2-Proto\q7_inuit_beta\`. **Now it is playable**:
`python tools\x360\make_q7_inuit.py C:\temp\otdu2-glm-userdata` packs the hybrid bank into the mod
overlay and the game loads it (`-car Q7_Inuit`, or the asset viewer on the bank).

- **The hybrid bank** (CROSSBUILD_PLAN "not ported" -> ported, minus the prototype geometry): the
  prototype's `.2dm` byte-swapped to PC (`eden_render_file.swap_to_pc`) + the **retail** scene with
  its MBNK material hashes re-pointed by role (`Carbon`→`Wood`, `Paint`→`Inuit`, `C1_L`/`C2_L`→
  `Leather`, `Roof`/`A_C3_Mo`→`Moquette`, `A_C1_Al`→`Aluminium`, `A_C1_P`→`Plastic_White`,
  `Dot`/`Plastic_Bruit`→`Plastic`) + retail `.3dg` + retail `q7_int`/`q7_int_ts`/`q7_glass_i` +
  the two prototype textures as PC `.2db` (`WOOD_INUIT` as sRGB DXT1 51, grey `Q7_INT2` in the
  retail header, mip 0 only). Retail scene because the geometry indexes materials in the retail
  order; the geometries differ per primitive (20 vs 18 prims on COCK_HR), so there is no
  per-primitive join (`eden_3dg.py` reads the PRIM material indices on both platforms).
- **The first PC bank writer** (`tools/x360/build_bank.py`): header/table/types/directory tree/
  indexes/data per `docs/formats.md` §BNK; `--roundtrip` rebuilds two retail banks with 0 problems
  and byte-identical directory trees (CRCs are zlib; the engine never verifies them).
- **New readers**: `eden_2dm.py` (materials + in-place texture-slot patch), `eden_3dd.py` (node
  tree), `eden_3dg.py` (primitive material indices), `eden_render_file.py` (the 360->PC swap).
- **The swap's trap**: `.2dm` MAT hashcodes are 8-slot identifiers in *memory order* on both
  platforms (char 0 first — `Mirror_I` holds "MIRROR_INT"'s fold) and must NOT be reversed, while
  texture-slot u64s, parameter names and the shader GUID are real numbers and are; the `.3dd`
  hashes are numbers everywhere. Getting this wrong binds nothing (the 3dd's MBNK `WOOD` hash is
  the folded `0x444F4F57`; the reversed one matches nothing).
- **Verified**: `bnk_tool list` parses the packed bank (8 entries); `-gm assetviewer -bank ...`
  renders the cream cabin with the walnut dash (`docs/images/q7-inuit-hybrid-viewer.webp`,
  `...-hybrid-zoom.png`; retail comparison in `q7-inuit-restored-dials.webp`); freeride
  `-car Q7_Inuit` loads it through the mod overlay; `-labellint` 15 labels 0 problems (3 new
  labels for the bank). Self-checks inside `make_q7_inuit.py`: every scene material hash resolves
  in the swapped `.2dm`, `Wood`.DETAIL == `GetIdentifier64bit("wood_inuit")`.
- **Next sessions (the standalone tool, owner-approved plan)**: Phase 2 — `tools/asset_studio`
  standalone exe (slim init: no physics/database/profile; window + `GSRender::createDevice` +
  ImGui + `GMAssetViewer` loop; game-dir picker, folder tree, thumbnails). Phase 3 — 360 support
  in C++: BE banks in `source/filesystem/bank.cpp`, 360 `.2db` decode (untile + word swap,
  `x360_tex.py` is the spec), `-protodir`. Phase 4 (filed): packed `.3dg` vertex decode (the real
  per-primitive Inuit assignment), `bnk_tool pack`, bigfile/LZX.

## Session 2026-11-01 (`glm`): rain lands on the windscreen, the wipers sweep it clear, puddles pool and dry — committed

The owner's ask: rain / snow landing on the windows, working wipers that clear them so the window is
visible, and puddles on the road that form and dry up. Six commits, all verified headless
(`docs/re/weather_effects_spec.md` is the written-up spec).

- **surface fx cherry-pick (5e3c1e1)**: cline's a2ad158 (EdenSurfaceFx: 3000 particles, 1024 decals,
  GroundWetness, ePass::Effects) into glm; README takes only the surface-fx row, `docs/state/` shard
  dropped.
- **weather (10bcdfc)**: the loader merged only intemperies 0+1 (`j < numIntemperies && j < 2`), so
  no `rain_*` value was reachable and NOTHING could ever rain. All blocks parse now (0+1 merge into
  the base, 2..N into overlays resolved by `WeatherPreset::get`; the accessors route through it —
  they read `Values` directly before, which is why the first probe showed all-zero rain on rainy
  blocks). `-intempery <n>` pins one (applyForcedIntempery at the load pick + the cheats switch).
  Ibiza "14": 7 blocks, #3 full rain (alpha 120, ground 1.0, droplets max 10), #5 pre-wets, 2/4 dry.
- **rain (c17a96c)**: upstream's EdenRain ported (WFX_Rain 9 blocks, WFX_Drop 300 streaks, WFX_Splash
  500, the Rain_Mid sound); materials from the weatherfx.bnk textures through setupMaterial;
  `-rain` overlays the preset's own rainy intempery onto a dry sky (replaces upstream's World-Viewer
  checkbox), `-norain` off. Owner confirmed the rain visible on their run.
- **windscreen (da29924)**: EdenWindscreen — uv-space droplets (the retail alpha law) into a
  half-res `Droplets_Windshield` RT; the final blit becomes `windscreen.frag` (refraction + rim
  glints + the wet film) while active; gated on the inside views (`chase.isInsideView`). **The
  retail Glass_I inside swap** (`applyGlassSwap`, FUN_00c53d10 / california_spec 4) — the session-45
  smoked glass is gone from the driver seat (`-keepglass` A/B). The RT gate had a deadlock: the pass
  checked `pDropletTarget->bInitialized`, which only sets by entering that same pass — first frame
  initializes now. Setters: `-droplets 0`, `-dropletscale` (default 30 [INF]; the retail 10-drop cap
  at freq 0.15/s has an equilibrium of ~0 visible), `-showdroplets`, `-nodroplets`.
- **wipers (5f62008)**: the owner's pushback was right — the cars DO carry wiper geometry: each body
  bakes its parked blades as one wide thin strip prim (Delta: 1.88x0.11x0.05 `Mirrors` prim in
  BODY_MR; SPRG/WS_SPRG washer jets; PC banks and the x360 bigfile agree; the earlier "no wipers in
  the data" call came from grepping full names — node names are 8-byte folds, and the blades are body
  prims, not nodes). No pivot/animation/driver exists anywhere (retail = camera droplets only), so
  the driver is port-invented over the retail art: `collectWipers` splits the prim at x=0 into two
  halves (`setKeepItemIndices` keeps the primitive indices + positions; `clearPrimitiveData` frees),
  `updateWipers` sweeps (auto: while it rains, 1.1 s sine to 89°, 7 s→1.2 s by intensity [INF];
  F6 / `-wipers`), draw hides the parked prim (`EdenDrawParams::ForceHiddenItems`, new) and re-draws
  the halves about pivots at their ends (axis = the Delta's 34° rake [INF]); the Opaque callback
  projects each blade to screen uv and `EdenWindscreen::wipe` clears the drops + film. Verified:
  driver-view ribbon mid-sweep + the wiped band; exterior both blades as silver arms on the glass.
- **puddles (f40f95c)**: roads — the retail RainSpecs permutation verified live (dark glossy
  sky-reflecting tarmac at `-intempery 3`, zero permutation warnings). Terrain — port-native
  [INF] (TD_HeightMap has no rain constants): 5x5 grid of 5 m cells through the carve-aware
  `getHeight` every 0.5 s; flat cells (within 8 cm of their neighbours) pool. Detection history:
  minima rules never fired — the heightfield is that smooth (dealer delta = a monotone
  5.94..6.08 gradient; printed grids under `-logsurfacefx` until it made sense). Puddles draw as
  bluish soft-shouldered quads through the decal pipeline, alpha = wetness², corner heights sampled
  (slope-conforming). Lifecycle verified: 7→13 pool in rain; `-intempery 4 -surfacefxwet 100` dries
  0.93→0.05 and every puddle dies; `-nopupdles` A/B.
- **snow (234def5)**: `-snow` [INF entirely — the data has no snow]: drops fall at 1/3 speed as
  0.25 m white flakes, splashes off, enables the falling rain on a dry sky; windscreen + wipers
  carry over.

Regressions: 13 configs exit 0 with 0 stalls — the 700-frame drive, `-storystep 5/7`, `-gm TESTCAR`,
`-atwash`, `-nocarve`, `-nosurfacefx`, `-rain`, `-norain -intempery 3`, `-droplets 0`, `-nodroplets`,
`-snow`, `-keepglass -cameramode 23`. Screenshots: `docs/images/rain_forced_s65.webp`,
`droplets_night_s65.png`, `droplets_show_s65.png` (the raw RT), `wipers_mid_s65.png`,
`wipers_ext_s65.png`, `road_wet_s65.png`, `puddles_s65.png`, `snow_s65.png`. README Rendering row +
two CLI rows, `docs/re/weather_effects_spec.md` new (RE refs + the invented parts + flags + limits).
Not done: the wiper arm has no extruded blade body (thin from glancing angles); droplets are screen
space (inside views only, no head parallax — the retail effect is too); the rain-block slant stays 0
(`DAT_0117ac84` unwritable); snow whitens nothing and the rain-block art stays streaky behind the
flakes; `HardTop.anm` cars keep their wipers parked (the skinned path has no draw params).
