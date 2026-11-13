# Session handoff — `main` (the one tree)

One directory, one repo, one agent at a time (`WORKSPACES.md`): **every agent
appends its entry at the top of this file**, newest first, in the
`## Session 2026-11-13 (grass on paths + far-tree light, `main`): grass cards no longer overhang roads; far trees are no longer black

The owner: "I know grass still sometimes is on pathways and stuff you can do both of these" (both = grass placement and the world vegetation accuracy).

### What changed
- `source/world/eden_world.cpp` - the road-surface / road-graph suppressions probe the card centre + the four quad ends (`-grasscentreonly` = old). `-loggrassroads` logs sample road points (`gm_freeride.cpp`) for `-tpin` probes.
- `eden_scene_view.cpp` / `eden_world.*` - the far-tree impostors' `Ambient_1` = scene `sun_ambient` x `-impostoramb2` (default 1.0); the TD_Impostor VS adds both slots and the port had 0 in the second, so the far forest was near-black. `-logimpostors` also prints sun_ambient / sun_diffuse.

### Verified
- `-loggrass` counts: village 374 -> 418 road skips, road spot 902 -> 998; `docs/images/grass_card_extent_village_before_after.webp`.
- Far trees at `-tpin 5150,26192`: `-noimpostors` removes the dark forest (so it was the impostors), k sweep 0 / 0.5 / 0.8 / 1.1, 17:00 / 23:00 / 07:00 checked, `docs/images/far_trees_impostor_light_before_after.webp`. 0 VUIDs.

### Ruled out
- The dark forest is not the grass (`-nograss` unchanged), not the instances (`-noinstances` removes the forest entirely); `-noimpostors` removes it.

### Not done / next
- Grass on off-graph dirt tracks / forest paths (terrain paint): the colour-map layers are GPU-only; a CPU readback of the sector terrain material mask is the lead (`EdenWorld::drawGrass`, near line 3340).
- Slot 1's retail source (FUN_00d769e0 / FUN_00981600) is untraced; 1.0 x sun_ambient is a visual match, `[INF]`.
- Dawn (07:00) impostors are still dim; mid-distance LOD0 vs impostor brightness at the hand-over was matched by eye only.

## Session 2026-11-13 (grass white bases, `main`): the grass root fringes are shaded now (baked root gradient)

The owner: "fix the worlds vegetation it doesn't look accurate and grass has odd white parts at the bottom".

**Reproduced**: `build-main/bin/opentdu2-main.exe -game C:/TDU2-Gamedata/TestDrive2 -userdata C:/temp/otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -tpin 19584,24960 -clock 12 -autoplay "300:shot=..." -exitframe 330` - blade bases bright white/silver.

### What changed
- `source/render/texture_cache.cpp` - `loadBankTextures` bakes a root-darkening gradient into the DXT5 colour endpoints of the `tallgrass.bnk` atlases (cause: the atlas bases are authored pale; the retail TD_TallGrass PS shades roots via its light gradient, the port's Gouraud path has none). Flags `-grassrootdark <s>` (default 0.5, 1.0 = off), `-nograssroots`.
- grass_spec.md 8, README flags row, PROGRESS tall-grass row.

### Verified
- `docs/images/grass_roots_before_after.webp` (top off, bottom new default); default-spawn village unchanged; 0 VUIDs in both runs.

### Ruled out
- `-grasstallgrass` (the retail shader) also fixes the white but shears/streaks the cards near the camera (car flatten fed with the camera) - not a usable default.

### Not done / next
- "Vegetation accuracy" beyond the grass base is NOT assessed: the `.pmi` trees/bushes (pmi_spec.md) vs retail colour/density, the unverified grass placement grid (grass_spec 7). Next: take comparison shots of a forest + a meadow and name the specific difference with the owner.
- The 0.5 bottom scale and the gradient shape are `[INF]`; the owner's eye on the real build decides.

## Session <date> (<topic>): <one-line result>` shape of `CLAUDE.md`
§"The session-entry template".

* Read order for a fresh agent: your vendor's onboarding (`CLAUDE.md` /
  `CLINE.md`) → this file → `PROGRESS.md` (the `WIP` rows especially) →
  `docs/STATE.md` for the deep history (frozen up to session 62, 2026-11-01).
* `docs/state/glm.md` / `cline.md` are the retired three-tree model's shards,
  kept as history — do not append to them.
* A session that is not pushed did not happen: commit + push `main` at the end
  (RULES §4); a push publishes the auto prerelease the launcher reads.

---

## Session 2026-11-11 (the launcher's update flow, `main`): "Restart to apply" works again (four defects: the xcopy quoting, the self-delete, the console flash, the never-persisted last_tag that offered a fresh install the release it already runs) and updates carry changelogs - a WHAT'S NEW SINCE YOUR BUILD card from the compare API plus real git-log release bodies; CI stamps the release tag into the build

The owner's report (tested on `E:\opentdu2-windows-v0.2.15`, the latest release): *"I ran it and says it's
using build 15 when I download latest release it says restart to apply and upon clicking it closes and looks
like a terminal pops up and instantly closes and nothing else happens"* — and the ask *"is it possible that
changelogs or some of the changelogs can be shown for updates?"*.

### Root cause (four defects, one visible failure)
1. **The apply script's xcopy never copied.** `StartStagedUpdate` wrote `xcopy /E /I /Y "<staged>\*"
   "<dir>\" >nul` — the destination's trailing `\` escapes the closing quote under the Windows argv rules,
   xcopy receives a broken path, its stderr goes to `>nul`, no errorlevel check. The staged files were
   silently never applied.
2. **The self-delete killed the relaunch.** The script's last line was `del "%~f0"`; cmd reads a batch file
   line by line and a delete makes the next read fail — "The batch file cannot be found.", errorlevel 1.
   (The fixture test proved it: any `del "%~f0"` ends the script with exit 1 in this cmd; only the
   `(goto) 2>nul & del "%~f0"` idiom exits clean.)
3. **The terminal flash.** `CreateProcessW(..., DETACHED_PROCESS | CREATE_NO_WINDOW, ...)` — MSDN says
   CREATE_NO_WINDOW is ignored next to DETACHED_PROCESS, so a real console appeared with all the swallowed
   error text and died: the "terminal pops up and instantly closes" the owner saw.
4. **The phantom update offer.** `bAvailable = lastTag.empty() ? true : latestTag != lastTag` and nothing
   ever wrote `[update] last_tag` (read in launcher_settings.cpp:58, saved verbatim, never assigned) — a
   fresh packaged install was told an update exists and was offered *the same release it had just
   downloaded*. The "build 15" display is the commit-count stamp (`git rev-list --count HEAD`), which
   coincides with the tag number only while one release ≈ one commit (v0.2.15 = 4827bfd = commit #15); the
   launcher never showed which release it was.

### What changed
- `launcher_updater.cpp` — the script is generated by the pure `BuildApplyScript` (exported for the
  selftest): `findstr /C:"%PID%"` instead of PATH-resolved `find` (a Git-for-Windows PATH serves GNU find),
  the xcopy destination carries **no** trailing backslash, every step appends to
  `%TEMP%\opentdu2-update\apply.log`, the relaunch runs even on failure (a failed apply must not leave the
  user with nothing), `set OK=1` + `if not "%OK%"=="1" exit /b 1` keeps the script and exits non-zero on
  failure, and the self-delete is the `(goto) 2>nul &` idiom, reached only on success. Spawned with
  `CREATE_NO_WINDOW` alone. The relaunch target is `ExePath()` (a renamed launcher still returns), and
  `StartStagedUpdate` refuses an empty staging dir up front.
- The update decision (`FillFromGitHub`): `UpdateEntry` gains `Commit` (`target_commitish`); equal hash →
  "This launcher is the newest release" and `bAvailable = false`; else the compare API
  (`/compare/<buildHash>...<commit>`) decides via `ahead_by` and its commit list becomes
  `UpdateState.WhatsNew` (subjects capped at 140 chars, 30 max, best-effort — a 404/rate-limit falls back
  to the `last_tag` string compare). The UI persists `last_tag` when a release is staged, and a launcher
  that finds files in `...\staged` at startup re-offers *Restart to apply* (`tag.txt` names the release in
  the Summary) — the owner's current stuck state recovers without re-downloading.
- The Updates page: a **WHAT'S NEW SINCE YOUR BUILD** card under the offer, the check-completion status line
  announces "Update available: <tag> - the Updates page has the changelog.", release bodies render through
  `StripMarkdownNoise` (images dropped, links keep their text, bold/heading/backtick markers off).
- `cmake/StampBuildInfo.cmake` + `windows.yml`: CI sets `OTDU_RELEASE_TAG` (tag pushes stamp their tag,
  main pushes stamp the prerelease tag of that run) → `OTDU_RELEASE_STRING` ("v0.2.18 · build 15
  (4827bfd)") shown by the launcher's title / status line / sidebar / footer / Updates card;
  `tools/CMakeLists.txt` + `launcher.rc.in` parse the tag into the version resource (was a hardcoded
  0.1.0.0; local builds stay 0.2.0). Both release jobs now check out (`fetch-depth: 0`) and write the real
  changelog into the release body — stable: `git log --format='* %s' $(git describe --tags --abbrev=0
  "<tag>^")..<tag>`, prerelease: newest reachable tag..HEAD, capped at 50 lines, `generate_release_notes`
  still appends GitHub's auto block; the prerelease body says "CI run #N" (the third "build N" meaning is
  retired). `main.cpp`: the selftest reports the release tag and `--check-update` prints WhatsNew + the
  newest release notes.

### Verified (build 19 (155ec9f+dirty) · main)
- `--selftest` **PASS** — including the new apply-script end-to-end: the generated script's quoting has no
  `\"` at line ends, a fixture run copies the staged file, replaces the fake launcher exe, creates the
  `.bak`, writes "staged tree copied" into apply.log and self-deletes with exit 0. The failure branch was
  exercised by hand (read-only target exe): "apply FAILED" logged, script kept, exit 1, relaunch attempted
  and its failure logged too.
- `--check-update` A/B against the live GitHub API: the current build reports **"This launcher is the
  newest release: v0.2.18 … available: no"** (the phantom offer is gone — v0.2.18's `target_commitish` is
  this commit); a copy pointed at `ninja-build/ninja` (hash unknowable there) degrades gracefully to
  available-yes via the tag fallback; a **v0.2.15-era worktree build** (hash 4827bfd, my new code copied
  over it) reports available-yes and renders the WhatsNew digest — the FFB wheel, grass, eden island and
  video-settings commits of v0.2.18. `--check-update` on the git-checkout path unchanged ("Up to date with
  origin/main").
- The CI changelog commands run locally: `git describe --tags --abbrev=0 "v0.2.15^"` → v0.2.14; the
  `git log --format='* %s'` ranges produce the bullet bodies. YAML re-checked by reading (no parser on the
  box).

### Not done / next
- The apply flow was never exercised against a REAL install (a GUI click of Restart to apply with a real
  staged release); the fixture + hand-run cover the script mechanics, the owner's next update is the live
  confirmation.
- `bAvailable` for a build NEWER than the newest release (the owner's unpushed local builds): the compare
  resolves, `ahead_by == 0` → nothing offered — correct, but the case is undocumented in the UI.
- Windows: the selftest's fake-exe relaunch flashes a console for a moment (a test artefact; the real
  target is the GUI launcher, which opens no console).

## Session 2026-11-11 (video settings that stick, `main`): the Resolution row works in Borderless / Fullscreen now and the Display mode row shows the mode the window is really in - the Discord user's "windowed keeps reverting back to Borderless" + "dropping resolution … staying at 4k" (RTX 3050 @ 4K); F4 answered (cheats, FREERIDE only; F2 anywhere)

The owner's task (the Discord thread of a tester user, quoted): *"A user with
those specs has had fps issues and some reason windowed keeps reverting please
can we check that the settings are sticking he also mentioned earlier that
dropping resolution didn't look like it was dropping but staying at 4k but
unsure if it is or not"* — RTX 3050 8 GB, 16 GB RAM, Ryzen 5 5600X, 4K monitor.

### Root cause (two bugs + one expectation gap)
1. **The Resolution row was windowed-only.** `ApplyVideoOptions` skips
   `SDL_SetWindowSize` while the window carries `SDL_WINDOW_FULLSCREEN`
   (game_options.cpp), and the swapchain always follows the window's pixel
   size (render_device.cpp `capabilities.currentExtent`). In Borderless the
   window IS the desktop mode → the row changed its label only; in exclusive
   the display mode was picked only inside `ApplyWindowMode`, which early-outed
   on `mode == appliedMode` → the row changed its label only. Either way a 4K
   panel rendered 4K however low the row went — "unsure if it is or not": it
   was not.
2. **The Display mode row lied about `-1`.** A window created from a retail
   fullscreen `SystemPC.cpr` is born BORDERLESS (the `SDL_WINDOW_FULLSCREEN`
   creation flag without a display mode = the borderless fullscreen desktop,
   tdu_instance.cpp createWindow), but `WindowMode = -1` ("not chosen") was
   shown as "Windowed" (extraRowText default case) and its first arrow step
   initialized to EXCLUSIVE (both old fallbacks said fullscreen-exclusive for
   `SetWindowed=false`, contradicting the creation). So the user opened the
   page on "Windowed" (a lie — really Borderless), pressed an arrow, and the
   row next to that lie was "Borderless" — *"it reverts back to Borderless"*.
   (Their file can also hold `window_mode = 1` from any past Borderless
   validation; that boots Borderless forever, by design.)
3. F4 "doesn't do anything": it is the **cheats panel**, wired in FREERIDE and
   casino only (`gm_freeride.cpp:3408`, `gm_casino.cpp:2378`). The graphics
   panel is **F2 anywhere** (`AdvancedGraphics::drawUI` runs globally,
   tdu_instance.cpp:614) or Y / Triangle on the pause screen. No code change.

### What changed
- `source/game/game_options.{h,cpp}` — `EffectiveWindowMode()`: `-1` resolves
  to the mode the window was CREATED with (windowed / borderless desktop);
  `ApplyWindowMode` uses it, keys its no-op on mode AND fullscreen size (a
  Resolution step re-applies), and both fullscreen flavors now call
  `ApplyFullscreenMode` = the display mode closest to `video_width/height`
  (borderless rides a real mode too, so the Resolution row drives the scanout
  there; null mode = desktop only with no size chosen; the picked size is in
  the log line; exclusive with a failed enumeration degrades to the null mode
  and re-applies on the next step instead of latching).
- `source/game/pause_menu.cpp` — the Display mode row's label and first-step
  start from `EffectiveWindowMode()`, so the row never shows a mode the window
  is not in and one press lands on the true neighbour.

### Verified (headless, build 17 (bed87e8+dirty) · main, 4K desktop, scratch userdata per run)
- `window_mode=1` + 1280x720 → `Window mode: borderless fullscreen at 1280x720`
  + `Swapchain created (1280x720, 3 images)` — the row's size now drives
  Borderless (was impossible).
- `window_mode=2` + 1280x720 → `Window mode: exclusive fullscreen at 1280x720`
  + swapchain 1280x720.
- `window_mode=0` + 1280x720 → `Window mode: windowed` on every boot (sticking).
- `window_mode=1` with no size → `Window mode: borderless fullscreen (desktop
  resolution)` + `Swapchain created (3840x2160, 3 images)` — the user's
  Borderless state quantified: full-4K rendering (their FPS cost).
- Run logs `_vmode_{a,a2,b,c,d,e}.log` (scratch, gitignored).

### Ruled out
- The launcher forcing a mode: no `-borderless` / `-fullscreen` / `-w` anywhere
  in `tools/launcher` — the command line is not the revert.
- options.ini being written behind the user's back: a plain boot+quit writes
  no options.ini (verified: none appeared in the scratch userdata); every
  `SaveGameOptions` site serializes the live state (F2 close, units, audio OK,
  video OK/Test, three cheat checkboxes).
- A quality profile touching the mode: `ApplyQualityProfile` never writes
  `WindowMode`.

### Not done / next
- The movie menu stepping (open Video page, arrows on the Display mode row,
  Cancel restore) not driven live headless — the autoplay drive of the retail
  menu is fragile; label + step go through `EffectiveWindowMode` at one site
  each, and the boot matrix above exercises the same resolver. Next: the
  owner/user eyeballs the row on the next prerelease build; if a real revert
  is still seen in play, get the build id + their options.ini `window_mode`.
- The `-borderless` / `-fullscreen` flags force `WindowMode` on every load and
  a save during that run would persist the forced value (dev-flags-only quirk,
  users never pass them — noted, not fixed).
- The user's actual FPS: the fix gives the Resolution row teeth, but a 3050 at
  4K wants `-profile low` / the 60 fps preset or Render scale ~67 % too — say
  so in the reply.

## Session 2026-11-10 (grass on the village cobbles, `main`): three more grass suppressions - the architecture family in the paved index, the ROAD_MAP road-graph width test, the out-of-class .pgr skip - the first-test-drive village renders clean cobbles (docs/images/grass_village_cobble_before/after.png); the .pgr row-table shuffle probed and documented as open

The owner's task: "g_on1.png has grass where it shouldn't on road behind the
car are you able to try fix the grass issue" - the village cobble plaza of
the first test drive, still grassy after the morning's TD_Road suppression.

### Root cause (three layers, all fixed)
1. **Out-of-class `.pgr` values clamped onto a grass type**: the emit took
   any cell value, `Min(v, 24 types - 1)` - the road/path-marker-looking
   cells (`0x59/0x5f/0x60/0x9d` in the spec's sectors, `0x7b/0x7c` here)
   rendered as the last (tallest) family. They skip now, counted as
   "path-marker cells skipped" (187-188 at the village spawn).
2. **The village plaza is not road geometry**: the sector `-o` material
   census (new, `-loggrass`) shows the collision soup carries walls /
   buildings / 'Village_Bandero' in the architecture family
   `183157b81c6cff57` - the plaza streets are terrain paint. That family
   joins `BuildRoadIndex` (up-facing + height-window rules keep walls /
   roofs out automatically; the index grew 1445 -> 3213 triangles here, the
   skip count ~unchanged - the streets really are paint).
3. **The ROAD_MAP graph suppression** (the faithful GRASSASTARMAC shape):
   `EdenWorld::GrassRoadIndex` - the ROAD_DETAIL polylines the map screen
   already parses, as an XZ segment hash built once per island from
   GMFreeRide; a card within `-grassroadwidth` (default 5.5 m) of a
   centreline skips. The village renders clean
   (`docs/images/grass_village_cobble_before/after.png`, same pose); the
   farm keeps its cap (20000 CAPPED, 685 graph-skips, `-grassroadwidth
   5.5`); `-roadskiptol` exposes the below-sheet window (0.05 m default).

### The .pgr deep probe (documented in grass_spec.md 7, open)
The row-offset table is index-shuffled in both probed sectors (~50 %
ascending steps, one `0x1008` entry pointing back into the table) and the
table-order decode is organic on the farm sector but full-width uniform
bands on sector-5-0-6-7 - the bands read as grass stripes on the village.
Sorted storage / reversal / 10-bit bit-reversal score no better on
adjacent-row coherence (the metric saturates: one class > 50 % of both
sectors). The `(0, x)` RLE pairs as escapes fit the missing-cell counts
exactly (farm 97,280 short / 4,955 pairs = 19.6 cells; s5067 15,807 / 709 =
22.3) but decoding them that way breaks row completeness. The retail
anchors point at per-EDGE billboards (`MAX_GRASS_BILLBOARD_PER_EDGE`,
`GRASSASTARMAC`) - the grid may not be a terrain map at all. The port keeps
the table-order placement + today's suppressions; the true layout wants the
PS3 twins or a retail capture.

### Verified (headless, build 16, `-gm FREERIDE -skipcine -nopad
-fixedtimestep -clock 13:00 -loggrass`)
- Village (default spawn, the owner's repro): 482 road-skipped + 189
  graph-skipped + 187 marker cells skipped; the cobble plaza and street
  clean in the shot, planters / verge grass intact.
- Farm (`-tpin 19584,24960`): 20000 cards CAPPED, 615 road + 685
  graph-skips - the fields stay dense.
- 0 errors, 0 stalls on every run.

### Not done / next
- The `.pgr` layout research (grass_spec.md 7) - PS3 twins or a retail
  capture A/B; junction plazas wider than ~11 m still take grass in their
  middle (the width dial is not a pavement mask).
- The roadside `_OffRoad_` grass-shrinking ladder stays open.

## Session 2026-11-10 (wheel force feedback, `main`): `source/input/wheel_ffb.*` - the rim torque on a haptic steering wheel (front-axle lateral load, slip lightness, centring pull, speed damper, crash kick), the port's own composition documented in docs/re/ffb_spec.md; headless drive proves the feed end to end, the feel awaits the owner's wheel

The owner's task: "you can start the FFB work ... it's something we were aiming
on doing before" - the same-day follow-up to GitHub issue #3 ("TDU2 never had
a proper implementation for force feedback on wheels") and the OpenTDU1
licence ask (org #548; TDU1's device layer is the documented *shape*,
unlicensed upstream - **nothing copied**, the composition is ours).

### What changed
- `source/input/wheel_ffb.h/.cpp` (new) - `WheelFFB`, beside GMFreeRide's
  `GamepadOutput`: attaches the SDL haptic device off the open gamepad's
  joystick (SDL 3.4.20 has no haptic-from-gamepad opener -
  `SDL_GetGamepadJoystick` + `SDL_OpenHapticFromJoystick`; DirectInput wheels
  expose it, XInput / DualSense pads never do, which gates the feature
  naturally), drives a `SDL_HAPTIC_CONSTANT` effect (level ±0x7fff x gain,
  INFINITY, updated on a 0.004-of-scale change + a 0.25 s heartbeat) and a
  `SDL_HAPTIC_DAMPER` condition (coefficient = speed norm), a crash `kick()`
  (0.18 s decaying overlay in the torque's own direction), `shutdown()` stops
  the effects (exit, option off, unplug). Flags: `-noffb` (also the
  `options.ini` `ffb` row + the debug panel checkbox "wheel force feedback"),
  `-ffbgain <0.1..4>`, `-ffbinvert`, `-logffb` (attach line + a ~2 Hz state
  line printed device or not).
- `source/game/freeride/gm_freeride.cpp/.h` - the composition, in the
  pad-extras block (the same block that feeds `GamepadOutput`): from
  `VehicleDynamics`'s per-wheel state - the front axle's `LateralForce` (each
  contribution falling to 0.25 by 3x the tyre's own `OptimalSlipAngle` - the
  rim "goes light" past the grip peak), normalized by `Load x SurfaceFriction`
  (grip loss on grass is automatic), the centring term
  (steerAngle/0.6 rad x speedNorm x 0.35 x axle grip), no fight below 2 km/h
  (full at 10), `Damper = speedNorm`; `wheelFfb.kick()` beside the two crash
  `gamepad.pulse()` calls; the `ffb:` line + checkbox in the debug panel;
  `wheelFfb.shutdown()` at terminate. `docs/re/ffb_spec.md` (new) is the
  design record: the TDU1 shape table (§1), the composition (§2), the attach
  law (§3), flags/logs (§4), the evidence split (§5).
- `source/game/game_options.h/.cpp` - the `FFB` field + the `ffb` save row.

### Verified (headless, build 15 (4827bfd+dirty), `-gm FREERIDE -skipcine
-nopad -fixedtimestep -logffb`, accel f60, steer-left hold f280-330,
steer-right f360-410)
- The state line follows the script exactly: damper ramps 0 -> 0.48 with
  speed after the accel hold, torque jumps +0.707 -> the +1.00 rail on the
  steer-left hold (the centring/SAT pull against the turn), settles with
  small counter-swings (-0.135 / +0.216) as the steering recentres, parked
  frames print torque 0.000.
- `-noffb` A/B: zero FFB lines (fully silent); 0 errors, 0 stalls both runs.
- No wheel on the headless runs: every line reads "no device" - the *feel*
  is the owner's test (plug a wheel in; if the pull reads backwards,
  `-ffbinvert`). PROGRESS rows (bug table + feature board), README flags row.

### Not done / next
- Config curves (TDU1's per-input piecewise tables) instead of the fixed
  feel constants - only if the owner wants tuning depth after a first drive.
- FFB in GM_TestCar (only FREERIDE feeds a wheel today; the class lifts out
  in one include + one update call).
- The feel report from the owner's wheel (and any `-ffbinvert` outcome) ->
  `docs/re/ffb_spec.md` 6.
- The licence answers (org #548, Milli) still pending - recorded in this
  morning's docs session (4827bfd).

## Session 2026-11-10 (OpenTDU1 reference re-point + physics/FFB inventory, `main`): the reference clone now tracks the org repo (the fork was 278 commits behind) and docs/re/opentdu1_reuse.md gains §2.11 — upstream's handling physics is done on Jolt (5,874-line physics_vehicle, retail-Havok replay validation, a bike roll-limit constraint) and the full FFB chain is in the clear; both gated on the licence ask (org issue #548, filed by the owner)

The owner's task: the repoint behind issue #3 (silentvelour: drop TDU2
handling, implement TDU1 physics; take OpenTDU's camera; keep TDU2 bikes) and
the TDU Platinum question (can Milli's mod feed physics/camera/refuel).

### What changed
- `C:\temp\opentdu1-ref` re-cloned from the org repo
  `opentestdriveunlimited/OpenTestDriveUnlimited` (shallow, `5487449`) —
  CamilleKMI's personal fork sat 278 commits behind (stale since 2026-09-13);
  the doc now names the org repo as canonical.
- `docs/re/opentdu1_reuse.md`:
  - header: repo + maturity refreshed (FreeRide **done** upstream now;
    physics runs on Jolt — `third_party/JoltPhysics`, CMake
    `OTDU_USE_JOLT`); **licence status recorded**: the org repo ships no
    licence (GitHub `license: null`) — read-and-reimplement only, no
    verbatim port into this GPL-3 tree until org issue #548 (the owner's
    GPL/permission ask, filed 2026-11-10) is answered.
  - new §2.11: the handling inventory (physics_vehicle.cpp 5,874 lines /
    3 stubs; vehicle_simulation.cpp 1,795 / 0; gs_physics.cpp 6,754 / 11;
    physics_vehicle_replay.cpp replays retail Havok snapshots — header 0x10,
    per-vehicle record 0x43b0, wheel stride 0x450 — into Jolt to evaluate
    faithfulness), the bike roll-limit constraint (101 lines, a Jolt
    `TwoBodyConstraint`, `mRollLimit = 0.01f`), the complete FFB chain
    (`input/gs_device.h:132-141` externs; `config/device.cpp:168`
    `FFB_FACTOR`; `gs_physics.cpp` `EvalFFBCurve` 6441 + composition
    6481-6578; `input/gs_device.cpp:356-407` SDL haptic constant + damper),
    and the no-refuel finding (no `fuel`/`refuel` anywhere in OpenTDU1
    source — TDU1's gas stations are unimplemented upstream, so
    fuel_gas_station_spec.md stays the only code path; the Platinum mod's
    reworked refuel is UX reference at most, never code or data in this
    tree, NOTICE rule).
  - ranked list gains 10-12: FFB (gated on the licence), TDU1 handling as
    `-physicstdu1` behind a toggle (gated on the licence + a car-data
    conversion plan), retail-physics replay validation as our
    faithfulness-evidence pattern.

### Not done / next
- The physics / FFB / camera ports wait on org #548 (licence) — nothing else
  blocks them; FFB is the smallest first move, then chase-cam laws, then the
  handling epic with a car-data conversion plan.
- Owner outreach to Milli, no reply yet: the 2026-10-30 DM (still unread),
  the Turboduck forum message, and a Discord DM (2026-11-10, notes the
  earlier attempts and that she is credited regardless of outcome).
  Platinum files stay local-only reference regardless of the answer.

## Session 2026-11-10 (grass through roads + density, `main`): tufts no longer poke through the tarmac (each sector indexes its TD_Road triangles; the emitter skips covered cards) and the grass is 2.2x denser at ~the same frame cost (one emit per frame + a near-first scan + stride 2 / cap 20000)

The owner's task: "Grass/Vegetation needs work During some tests I seen grass
poking through roads noticed at when you can start your 1st test drive of
california also believe grass should be more dense in area's it seems patchy
as if there's not enough" (the California = the Ferrari California, the
first test drive at the Ferrari dealer on Ibiza).

### What changed
- `source/world/eden_world.h/.cpp`:
  - **The road index**: `Sector.RoadTriangles` + `RoadCells` (8 m XZ
    buckets, the carve-ground mechanics on the road ribbons);
    `BuildRoadIndex` at sector load — items whose material resolves
    `kRoadShaderSet` (the TD_Road ribbons of the sector -o, collision kept
    anyway), up-facing triangles only; `SampleRoadSurface` = the
    `SampleCarveGround` query on that index.
  - **The suppression** in the grass emitter: a card whose spot has a road
    surface within `cardHeight + 0.25 m` above the heightfield is skipped
    (the roads ride above the sheet — on embankments by metres — so a card
    rooted under a road poked through the tarmac). Cards well below a road
    (bridge decks) stay. `-noroadskip` = the A/B; `-loggrass` counts the
    skips. Cards can still overlap a road *edge* by up to w/2 (centre-only
    test — the retail roadside answer is the `.pgr` markers +
    `_OffRoad_` ladder, still open).
  - **One emit per frame**: `drawGrass` split into the emit
    (`buildGrassBatch`) and the draw; every pass that calls
    `EdenWorld::draw` (prepass, shadow casters, ocean mirror, env-cube
    faces, main — up to ~8x per frame) used to re-walk the `.pgr` grids
    and re-upload. The passes now re-draw the cached bindings; rebuild on
    frame rollover or > 2 m of camera movement. The upload-failure path
    zeroes the mesh (the copy carried the template's IndexCount).
  - **Near-first scan**: sectors sorted by centre distance, rows and
    columns alternating outward from the camera's lattice lines — the cap
    truncates the faded far ring instead of an arbitrary list-order band
    (before, a capped dense sector never reached the road cells at all:
    0 road-skipped at the farm highway).
  - **Denser defaults**: `-grassstride` 3 → 2 (2.25 m → 1.5 m spacing),
    `-grassmax` 9000 → 20000, streams reserved once per build. The scene
    record is ~flat in the card count now (9000 capped vs 20000 capped
    both ~10-13 ms `-logscenetimes` at the farm teleport).
- `docs/re/grass_spec.md` §6 (the three mechanics + evidence) + §7 (the
  poke-through item built; the `_OffRoad_` roadside ladder stays open).
- README flags row, PROGRESS.md tall-grass row (now `main`).

### Evidence (build 13+dirty, headless)
- `docs/images/grass_road_ferrari_off/on.webp` — the dealer road (the
  owner's repro area): 5163 cards with 0 skipped (poke-through) vs 4957 +
  206 road-skipped (clean tarmac; the counts reconcile exactly).
- `grass_road_highway_off/on.webp` — the autovia embankment: tufts crossed
  the guardrail line onto the shoulder in the off shot; the on shot keeps
  the embankment grass and the tarmac clean (159 skipped while capped).
- `grass_density_before/after.webp` — the farm field, old defaults vs new:
  the patchy dirt fills in.
- Regressions: `-grasstallgrass` renders through the same paths (guid
  cafe8deca8c8caca, 20000 cards, 591 skips), `-nograss` silent, no
  validation VUIDs, no stalls.

### Open / next
- The roadside `_OffRoad_*` ladder + the `.pgr` road markers (the retail
  way to *shrink* grass toward roads; the port deletes the covered cards).
- Retail-capture density A/B (what `TALL_GRASS_MAX_PER_EDGE` really
  generates), the wind time, the car flatten — the spec's §7 as before.

## Session 2026-11-10 (map icons + discovery, `main`): every dealer pin shows its brand (the retail-grabs law) and the map hides undiscovered spots — `KnownSpots` in the profile, All spots cheat, `-cheat allspots`

The owner's task: "Is it possible we can add the icons on map I believe some of the
dealership icons on the map are missing / also we need to also have it so no icons
show on the map as they normally show on map once you've discovered them", with two
retail grabs of the "Spots for Vehicles" tab attached (Ibiza + Hawai).

### What changed
- `docs/re/map_spots_spec.md` (new) — the spots.bin category→icon table, the dealer
  logo law + the retail pick table, the discovery law, the open [ENC]s.
- `source/game/map_screen.cpp/.h` — the dealer pin logo (the 3-prefix hack → the
  retail picks table + the stock law; the brand resolves through
  `DBCarPhysicsData._car_brand` → `DBBrands._logo_global_index`, both read at
  runtime), and the discovery state (`spotDiscovered` per island:
  `discoverNear` 150 m [INF], `buildSpots` clones only known spots,
  `updateSnap` skips unknown ones so fast travel cannot target them;
  `unlockAllSpots`; `-logspots` logs logo + state). `-maptab <n>` now actually
  pins the tab (the movie's open animation reset it before; a test hook).
- `source/game/car_shop.cpp/.h` — `ShopCar.BrandHash` (`_car_brand`; the
  `_manufacturer_name` display strings are not id-safe — the Charger's says
  "Charger").
- `source/game/player_profile.cpp/.h` — `KnownSpots` per island, saved as
  `knownspots_<island> = id,…` (the `ExploredRoads` pattern).
- `source/game/freeride/gm_freeride.cpp` — `discoverNear(carPosition)` beside
  `trackCar`; the All spots cheat handler.
- `source/game/cheats.cpp/.h` — the **All spots** delock row (the retail dev
  menu's shape), `spots: n/m` in the status line, `-cheat allspots`.
- `README.md` flags + feature rows; `PROGRESS.md` World map row.

### Verified (build 12, headless)
- All 20 Ibiza dealer pins resolve to distinct logos matching the owner's retail
  grab, position by position — `docs/images/map_dealer_brands_retail_vs_port.webp`
  (retail over port). Before: only Audi/Ferrari/Mercedes had brands, 17 pins were
  the generic car cell.
- Fresh profile: `75 spots with an icon …, 2 discovered` (the auto-knowns: the
  story house + the hotel), `2 spot icons created (73 undiscovered hidden)`, the
  vehicles tab draws nothing (`docs/images/map_discovery_fresh.webp`).
- Discovery: `-tp 21700,8,24188` (96 m from the Ferrari dealer) → `MapScreen:
  discovered 'ECD_FERR' (cardealer)`, the map shows exactly that pin
  (`docs/images/map_discovery_one_pin.webp`); 296 m out does not fire (the radius
  is real). `-playername` run: `knownspots_ibiza = …` lands in `profile.sav` and a
  later run reloads `3 discovered` (round-trip).
- `-cheat allspots`: `75 spot icons created (0 undiscovered hidden)`, profile
  saved (`cheat: all spots`).

### Ruled out / notes
- The retail per-dealer pick is hand-authored in the encrypted band — "British
  Cars" (6 Astons) shows Lotus, "Asian Cars" (2 Nissans) shows Subaru, "U.S. Cars
  (north)" shows Hummer. The grabs settle 11 of 14 dealer banks; the rest use the
  stock law (most-represented brand, tie → priciest car's brand).
- Hawai's 25 `EDI_B` diners (category 0x08, no prefix match) stayed icon-less
  **faithfully**: MAP.vmf has no diner/food kind. `bikeDealer` /
  `carDealerClassic` kinds exist in the movie but no base-game spot or DBCarShop
  row uses them (bike dealers = DLC data the port does not read).
- The HUD's floating world markers + enter prompts stay unfiltered on purpose:
  driving past a shop is what discovers it (retail shows the enter tile of a
  never-visited dealer).
- An existing profile (pre-feature save) starts with nothing discovered except
  owned houses / home — drive near spots to re-discover, or F4 → All spots.

### Not done / next
- Hawai's own pin layout not screenshot-verified (the owner's Hawai grab shows
  the same 14 dealer banks; the law is island-independent).
- The retail `carDealerClassic` tile art (if it differs from `cardealer`) is
  unused; the port draws `cardealer` + brand for the ECDC classics too.

## Session 2026-11-10 (walk-mode emotes, `main`): the 8 retail avatar emotes play in the self view — X / 1..8 / the EMOTES panel, `-emote <label>` for tests

The owner's task: "implement the characters emotes if possible" (after confirming
`origin` = `https://github.com/danny199012/OpenTDU2`).

### What changed
- `docs/re/emotes_spec.md` (new) — the feature spec: the 16 `em_*.anm` of
  `avatarsdriver.bnk` (`anims\emotes\em_xan`, 8 emotes x sex = the `animAvatar[8..16]`
  slots of `Entity__AvatarOnline`, the "hello(a)" of tess_garage_spec 4.2 is
  `em_helloa-w`), the retail trigger path refs, the port's [INF] choices, the [ENC] gaps.
- `source/game/avatarchoice/intro_cinematic.*` — the clip table (`getPlayerEmotes`, the
  retail slot order), `playPlayerEmote` / `startPlayerSelfView` / `stopPlayerEmote` /
  `setPlayerEmotePlace` + `updateEmotePlayer` (bind -> `co_stand` looped ->
  `BlendPoses` toward the clip, 0.2 s ease each way, the `SeatedAvatar::setOverride`
  pattern), called from `updatePnjs()` in the `bPlayerSeated` chain.
- `source/game/walkmode/walk_mode.*` — `startSelfView` / `endSelfView` / `updateSelfView`
  (the body parks, the camera glides 0.25 s to a 2.2 m / 1.75 m orbit aimed at floor
  + 1.2 m, feeds `setPlayerEmotePlace`; the retail type-10 camera law is [ENC]) and the
  shared `drawEmotePickerUi` (Dear ImGui; the retail wheel movie is undocumented).
- `source/game/firstwalkmode/gm_first_walk_mode.*`, `source/game/spots/interior_walk.*`,
  `source/game/casino/casino_interior.*` + `gm_casino.cpp` — the same emote law in the
  three walk hosts: `IA_Emote` (X) toggles the self view, 1..8 the slots, the view
  returns at the clip end (the retail state 7 -> 4), B in the self view goes back instead
  of leaving the room, A ignored; `-emote <label>` plays on the walk start.
- `source/input/gs_input.*` — `IA_Emote` / `EMOTE_MENU`, default X (the retail action
  0x22 binding is [ENC]; X collides with nothing in the walk contexts, rebindable via
  controls.ini).
- `source/tdu_instance.cpp` — autoplay action `emote` (the `press=` path; autoplay
  `key=X` injects the raw scancode and does not map through the action bindings — a
  real keyboard X does, that was the one test dead end).
- `README.md` flags row, `PROGRESS.md` Done row, `docs/re/casino_spec.md` §8 row (the
  emote half of "no cocktail or emote to *use*" is now built).

### Verified
- `powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1` exit 0 (build 11,
  509d6ce+dirty); check-build: exe newer than every source.
- FIRST_WM headless (`-allowskip -emote hello -logemotes`, autoplay skip + shots):
  `EMOTE: self view in, eye (-7.73, 1.69, 16.32)` -> `EMOTE: playing 'hello'
  (em_hello-m.anm, 4.92 s)` -> `EMOTE: self view out` at the clip end (~frame 417);
  screenshots `docs/images/emote_hello_garage.webp` (the wave, third person) and
  `emote_return_firstperson.webp` (after the return, the A/B).
- Trigger paths: `press=emote` toggles the self view (idle), then autoplay `key=3`
  played `funny` (`em_funny-m.anm, 4.16 s`) — `docs/images/emote_funny_garage.webp`.
- Casino hall (`-gm CASINO -casinoroom hall -skipcine -walkat ... -emote cheers`):
  `playing 'cheers' (em_cheers-m.anm, 4.32 s)` — `docs/images/emote_cheers_casino.webp`.

### Ruled out / notes
- The retail emote wheel is a Flash movie that never shipped readable (`emop`/`emcl`
  reach the manager, the movie is undocumented) — the ImGui panel is the stand-in.
- The retail avatar opacity fade (FUN_00b85460) needs skin-shader alpha the port has no
  path for; the camera glide carries the transition instead.
- The `Emote_11..33` -> slot mapping is not decoded; the unlockables stay inert and the
  emotes are not gated.
- The woman clips are in the bank and the per-sex branch is the SeatedAvatar law, but
  the runs played the default male profile (no CLI flag forces the avatar sex).

### Not done / next
- The cocktails (the other half of casino_spec §8's row) still need the prop work.
- If the owner wants the pad: the emote key is keyboard-only right now (a pad binding
  for IA_Emote wants the owner's button preference).

## Session 2026-11-10 (single-tree migration, `main`): the workflow moved to one directory, one public repo, one line

The owner's decision: agents run one at a time in one shared directory
(`C:\OpenTDU2`), rotating when the 5h / weekly quota runs out, and everything
publishes to the public repo. Done: cloned `origin` =
`https://github.com/danny199012/OpenTDU2` as `C:\OpenTDU2`; cherry-picked
glm's two unpushed commits (the docs/images WebP shrink 218 MB → 22 MB + the
README gallery links); captured the old win tree's uncommitted spot-holograms
feature as `wip/spot-holograms` (compiles clean on top of `main`, never run);
verified the win tip's shop/house world markers and the cline casino cashier
work were already in `main` (the shader-blob drift between the old branches
and `main` is stale, not missing work); `WORKSPACES.md` / `RULES.md` /
`PROGRESS.md` / this handoff rewritten to the single-tree model; pushed as
`4289a76` (the docs landed one push later than the images — the first push
went out before the docs commit was moved onto `main`; two auto prereleases
fired and the launcher sees the newest). Verified: `build-main\bin\opentdu2-main.exe`
exit 0. Not done / next: the old trees (`C:\OpenTDU2-win` / `-glm` / `-cline`)
are disposable — the owner deletes them when ready, never while an agent has
one as its cwd; the owner's `E:` copy still points `origin` at the private
archive (flip it with `git remote set-url origin
https://github.com/danny199012/OpenTDU2.git` when next opened); the private
repo stays frozen (optionally Archive it on GitHub); the RE data files
(`docs/re/ps3/*`, `savegame_schema_360.txt`) live only there and must never be
created in this tree. Next work: whatever the owner brings to the next agent;
the hologram WIP row in `PROGRESS.md` names its own next step.

## Session 2026-11-05 (shop markers, `win`): shops and houses get their retail world marker tile

The owner, with a retail capture of a red house tile floating over a home down the road: *"I'm hoping we can quickly
add these markers there's some for clothes and dealerships I think even champion ships have them etc... But look
slightly different."*

### What changed
- `gm_freeride.cpp` `tickWorldSpots` - every spot with a map icon within 300 m (`MapScreen::forEachSpot`, new) is
  pushed as a world marker, nearest first, after the race / gate / championship markers (8 max); owned houses
  (`EPH`, `IsHouseOwned`) show `home` (the red tile of the capture), the rest their own Spot art. Not during a race /
  licence test, not the spot whose enter element is up. Shop tiles: anchor +14 m, scale `300/d` clamped 150..230 %
  [INF: read off the one capture]; race markers keep their small law.
- `freeride_hud.cpp` `setWorldMarkers` - the clones hide the enter CARD (depths 4/5/9/10/13: every marker drew a
  dark "Enter" panel, the Spot tile sat ~280 px under it) and are offset so the TILE centre lands on the projected
  point (`kMarkerTileDX/DY` = 7 / 280, measured with `-markertest`). `<kind>+<halo>` turns one Spot halo on.
  Dealer tiles bind their brand: `Spot/ic` 'brands', `ic/logo` frame = logo + 1, the `lure` quad = the logo's cell of
  `brands_map` (the HUD movie now adds `LogoTexturePage.bnk` like the map).
- `flash_player.cpp` `drawShape` - a fully transparent SOLID fill takes a bound bitmap override like an empty bitmap
  fill: HUD.vmf's logo `lure` is the solid kind (MAP.vmf's is the bitmap kind), so the logos never drew.
- `MapScreen::forEachSpot` passes the dealer's `LogoIndex` (the map's own rule).

### Verified
- `-markertest home,estates,clothes,carDealer -markerlogo 12|4|62 -no3d`: red home tile, estates (house + hand),
  clothes (suit), dealer tile with the Ferrari / Audi / index-62 logo (`docs/images/shop_markers.webp`, top row).
- `-tp 7445,17060 -heading 0`: `marker 'estates' canvas (637, 296) distance 142 m` - the for-sale tile floats over
  EPH_L1BI (same image, bottom).

### Ruled out
- The capture's pink bracket + red arrow is not a Spot halo: target / hlight / glow / link / news / rank / favorite /
  done all rendered (`-markertest home+<halo>`), none is it (glow is a pink rim, the closest).

### Not done / next
- **The GPS-destination dressing** of the capture (the ring bracket around the tile with the red arrow on it) - most
  likely `/HUD/arrow_gps` (sprite 769: its `arrow` child places 767/744 'wrong' + the red disc 572) or a GPS-target
  state; start with `-dumpbot`'s placement walk on 769 and the map's route destination.
- Size / height are one-capture [INF]; championship starts already had markers (their own family tiles).
- Occlusion: tiles show through buildings (as the race markers always did).

## Session 2026-11-05 (return to road 2, `win`): the red X is the reset cooldown, not "on the road"

The owner: *"I Think the red cross version happens when you try return to the road and then try again straight away
after successfully returned to the road that's how I managed to capture the snippits"*.

### What changed
- `source/game/freeride/gm_freeride.cpp` - BACK_ON_TRACK (DualSense circle / keyboard R, held ~1 s) now works
  on the road too (it used to be refused there, so a car rolled over on a road could not be reset). A reset starts
  `kBackOnTrackCooldown` = 5 s [INF]; a new press during it is refused for that whole hold: the badge's red X
  (ic 'lock' + sndp:wrong), which the movie closes itself. The badge display moved into one place (the indicator
  block): passive off-road, the hold sweep, the refused X - the hold path no longer calls the HUD directly.
- `freeride_hud.cpp` - lock -> on re-settles BOT on 'stop' (the refused X's self-close leaves it on 'default').

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -autoplay "240:hold=backontrack;260:shot=..;330:release=backontrack;
  360:hold=backontrack;372:shot=..;420:shot=..;440:release=backontrack;700:hold=backontrack;720:shot=..;800:release=backontrack"`
  -> `docs/images/rtr_bot_cooldown_sequence.webp`: sweep, reset (`back on track (12.8 m)`), red X at f372 and f420,
  the sweep after the cooldown and a second reset (`back on track (0.3 m)`).

### Not done / next
- The 5 s is a choice; the retail timer is in the encrypted driver (PS3 twin not looked for). Say if it feels long.
- `PoliceParams.CanBackOnTrack` (the reset barred in a police chase) is a readable name in the exe - not wired.

## Session 2026-11-05 (return to road, `win`): the badge is the retail `/HUD/BOT` element - down arrow + road, both states match

The owner, with two retail captures (the blue badge, the red X badge): *"I hope you can find these I believe they
come from seperate places ... would hope it if we can try match how this works correctly"*.

### What changed
- `source/game/freeride/freeride_hud.cpp` `showBackOnTrack` - drives **`/HUD/BOT`** (char 821) instead of
  `/HUD/enter_hold` (char 828). The two are twin elements of `hud.vmf` at the same parking spot; BOT's `ic` draws
  **bitmap 812** (white ring + DOWN arrow + road with lane dashes, transparent interior over the sky sweep 808) -
  the captures' art - where enter_hold's draws 822 (the up-arrow the port showed since glm s70). The label is
  BOT's own field `/:m_BackOnTrack_` (loc 50); `/:IsBOT` is set (BOT's `open` script reads it).
  Two fixes on the way: a show that starts locked now makes the `ic` goto (the cache started on 'lock', so a locked
  first show drew the `on` art), and `lock` re-shows `ic` depth 1, the red disc (the `on` hide outlived the goto).
- `-dumpbot` also prints "who uses bitmap N" (shapes filling 812/822/808/571 -> the sprites placing them -> the root,
  with the placement matrix).
- `source/flash/flash_movie.cpp` / `gs_flash.cpp` - **`-dumpflashart all|<bank>,<bank>`**: every bitmap of every
  loaded UI movie (`all` also loads every `FrontEnd/HiRes/*.bnk`) to `_flashart/<movie>/<id>.png`.

### Verified
- `docs/images/rtr_bot_retail_vs_port.webp` - left the owner's captures, right the port: passive
  (`-gm FREERIDE -skipcine -nopad -fixedtimestep -offroadhud -autoplay "240:shot=..."`) and locked (the same run with
  `240:hold=backontrack;300:shot=...` - the car is on the road, so the hold is rejected). Ring, arrow, road, sky fill,
  red disc, X and label line up with the retail.
- `-botforce lock` at f70 / f100 / f160: the X flashes and the element closes itself (ic f40
  `_parent.gotoAndPlay("close")`, the movie's own law - a rejected press flashes ~0.5 s).

### Ruled out
- Runtime composition / a flip of 822 (the s70 theory): the composed look is authored as one bitmap, 812.
- The art in another bank: all 31 HiRes frontend banks dumped; the RMS match ranks hud 812 first for the blue capture.

### Not done / next
- The owner's `<mods>/flash/fla_0822.png` stand-in no longer shows (it changes only enter_hold) - can be deleted.
- `-dumpflashart all` stops after `log_in2` (the next bank load dies with no log line); `main`/`map`.. were dumped
  by name. Not chased - start at `GSFlash::initialize`'s bank loop with the watchdog log.
- The port keeps the locked badge up while the button is held on-road; retail plays `ic.gotoAndPlay("lock")`, which
  closes itself after ~32 frames. Watch it in game (DualSense circle / keyboard R, held on a road) and say if it
  should only flash once.

## Session 2026-11-05 (school desk, `win`): the racing school opens the retail R_SCHOOL.vmf screen

The owner: *"Yes, school screen"* (the retail school UI instead of the port's panel).

### What changed
- `source/game/freeride/school_screen.{h,cpp}` (new) - R_SCHOOL.vmf driven by its own protocol (licenses_spec 11):
  `open_school` + the tutor pick (`ItemType('valid')`), `rsch:stli` (licence card states / class badges), `rsch:dits`
  (the test list: name, type icon, place, weather, done), `rsch:dtup` (Test's objective + target), `rsch:tspr`
  (start the test), `wmon` / `rsch:exit` (back to the walk).
- `gm_freeride.cpp` - `openSchool()` for the desk, `-schoolmenu` and the result screen's Quit; `-nativeschool`
  keeps the old panel. HUD hidden while it is up.
- `ig2_challenge.*` - VEHI entities counted (the overtaking tests).
- `tools/vmf_disasm.py` - walks `DefineSpriteEden` (tag 127) sprites: R_SCHOOL's menus live there.

### Verified
- `-schoolmenu A -loglicenses`: card "Asphalt Racing School / Asphalt license" with A7 / A6 badges, A -> the 7 A7 tests
  with icons and the objective panel (`docs/images/licence_school_tests.webp`), Down + A -> `rsch:tspr` -> RSA7T02
  starts; B, B -> `camf`, `wmon` -> closed.
- `-licensetest RSA7T01 -licenseforce fail`, menu Quit -> inside ERS_A on the card (`licence_school_card.webp`).

### Not done / next
- The card's player fields (photo, name, number) are empty: trace their variables in R_SCHOOL.vmf (`licence`
  clip, `tools/vmf_disasm.py` output around `SetCurLicence`).
- Weather icon always `sun`; the type icon is [INF] (title words / course).
- Controls (DualSense): left / right on the card, up / down in the list, Cross OK, Circle back.

## Session 2026-11-05 (cones, `win`): there is no traffic cone in the game data - the barrel stays

The owner, after the slalom work: *"Yes, real cones"* (load Hawai's `SF_PLOTS` as the cone).

### What changed
- Nothing in the code (the cone loader was written, tested and reverted). `docs/re/licenses_spec.md` 10 and the
  previous entry's "next" corrected.

### Verified / ruled out
- `SF_PLOTS` (Hawai `commonworld.bnk`) is a material. The library nodes using it, `SF_PLOA` .. `SF_PLOQ`, were loaded
  (EdenModel node filter) and drawn as the RSA7T02 markers: green street **bollards** and bollard rows, not cones
  (`docs/images/licence_hawai_sf_plo_bollards.webp`: SF_PLOC, SF_PLOD, SF_PLOG). Bounds: posts 0.2 x 1.06 m, rows
  2-46 m long.
- All 649 Hawai library node names, the police pack and every non-frontend bank (name scan): no cone.
- So the racing-school barrel (`sf_barel_d`, `RS_Barel_*` materials, the checkpoint pack's only obstacle) stays as
  the slalom pole / cone-line marker - most likely the retail look [INF].

### Not done / next
- Confirm against a retail capture of a slalom test (the 360 build runs: `docs/re/CROSSBUILD_PLAN.md`) whether the
  markers are barrels; if they are something else, its model is not in the PC data.

## Session 2026-11-05 (licences, `win`): slaloms weave, cone lines + racing line drawn, the retail fail / win menu

The owner: *"i was hoping we can fix the licenses solomon is meant to have cones you weave in and out of and stuff
there's also meant to be a racing line which tells you when to slow down as well also can we try find the corrcet ui
menu's also upon failing there should be a menu that pops up with retry and if you win there's a menu for advancing
to next task or returning to the school."*

**Reproduced**: `-licensetest RSA7T02` ("Slaloms"): the airport apron was empty but for barrel pairs centred on a
straight line; holding the throttle straight through passed every gate; the end was a goal line, no menu.

### What changed
- `source/game/freeride/license_result_screen.{h,cpp}` (new) - the retail RESULTS.vmf racing school frames
  (`failed_test` / `success_test` -> A -> `menu_rs`): the game sets `TestName`, `Message`, `final_score`,
  `ref_score`, `LicenceName`, `nb_tests_done/total`, `IsLicensed`, `LockReplay`; the movie's own menu answers
  `rest` (Retry), `nets` (Next test), `quit` (Quit to the racing school); `rpin` (Video Replay) goes back to the
  panel (no replays). Decoded from the movie's scripts (licenses_spec 9).
- `gm_freeride.cpp` - `openLicenseResult` / `tickLicenseResult` / `returnToSchool`: a licence test's end opens the
  screen over the frozen world (HUD hidden); Retry restarts, Next test starts the next one of the licence (the school
  if none / not passed), Quit enters `ERS_<letter>` with the school menu on that licence (`SchoolMenu::openAtTest`).
  `buildCourseMarkers`: all slalom poles, the DEC2 cone lines (one marker / 3 m), the racing line strips and braking
  zones, built once per run. `-licenseforce <pass|fail>` test hook.
- `ig2_challenge.*` - `DEC2` decor parsed (`Ig2Decor`); `Ig2Gate::isSlalom` / `getPassCentre`: a kind 1 record is a
  slalom pole with its 7 m opening on side 1 (+x) / 2 (-x) of the gate frame.
- `license_test.cpp` - a slalom counts only when the car's centre crosses 1 m .. width + 1 m out on the open side
  (straight down the pole line = "Missed a gate"); `getNextGate` points at the opening.
- `race_gates.*` - loads the pack's `Trajectory` strip and `BrakingZone_01` target; draws `Marker`s (poles, cone
  markers, strips stretched along the line) and single-pole gates. `-logracegates` lists every pack actor.
- `tools/vmf_disasm.py` (new) - AVM1 disassembler for the .vmf (FMV) movies: `python tools/vmf_disasm.py in.vmf out.txt`.

### Verified
- Fail: `-licensetest RSA7T02 -licenseforce fail -loglicenses` -> `failed_test`, thumbs-down cart, A -> menu
  "Retry / Next test / Video Replay / Quit to the racing school" (`docs/images/licence_result_failed.webp`,
  `licence_result_menu.png`); Retry -> `FSCommand 'rest'` -> the test restarts.
- Win: `-licensetest RSA7T01 -licenseforce pass` -> the licence panel "A7 1 / 7 Successful / Total"
  (`licence_result_success.png`); Quit -> `FSCommand 'quit'` -> inside `ERS_A`, school menu on Slaloms
  (`licence_result_back_to_school.png`).
- Slalom: `-licensewarp` through the openings passes 12 / 12; `hold=accel` straight from the start ->
  "gate 1 crossed 3.6 m off centre ... Missed a gate" -> the fail screen.
- `FREERIDE: RSA7T02 course: 10 slalom pole(s), 11 cone line(s) / 94 marker(s), racing line 292 strip(s), 3 braking
  zone(s)`; frames `licence_slalom_racing_line.webp`, `licence_cone_lines.png`, `licence_braking_zone.png`.

### Ruled out (with the test that ruled it out)
- A cone mesh in the Ibiza data: case-insensitive scan of every non-frontend bank for cone / plot / pylon names ->
  only `SF_PLOTS` in **Hawai's** `commonworld.bnk` (+ random hits in compressed sector data). The checkpoint pack
  has the racing-school barrel only.
- A path in the .ig2 files: the 58 licence tests hold CHK2 / DEC2 / VEHI entities only; no spline record.
- Drawing the strips along +z: the chevrons pointed back at the car (`_c3z` crop); laid from the far end now.

### Not done / next
- ~~Real cones from Hawai's `SF_PLOTS`~~: those are bollards; the game data has no cone (next entry up).
- **The retail racing line's source** is unknown ([INF] curve through the openings, braking zone where it turns
  > 50 degrees in 20 m). Lead: `ig2/replays/<test>.rly` (XMB `ReplayBaseParams`, matrix chunks: full start position
  then packed deltas). The strips are white; the Arrow_Orange / Arrow_Red UVAs (colour by braking) are not used.
- **Road tests** (RSA7T04 ...) get no racing line (the road graph could carry one).
- **The school screen** is still the native panel; the retail one is `r_school.vmf` (disassemble it with
  `tools/vmf_disasm.py`, same method as section 9).
- The fail cart's text is empty (`openCarts()` sets `Message2 = 'empty'` itself) - check a retail capture.
- Owner check: weave RSA7T02 with the DualSense (Cross = OK on the result screen, Circle = back, D-pad up/down).

## Session 2026-11-05 (cameras, `win`): the photo camera's sticks turn the right way, the chase camera no longer sinks

The owner: "in pause the camera menu the left/right on both sticks seem to be flipped right being left and vice
versa also camera in game chase cam tends to sink sometimes making it hard to see above car".

### What changed
- `source/game/freeride/photo_camera.cpp` - Eden's yaw grows towards +x, the screen's left: the right stick
  now subtracts from the yaw (the mouse already did), and the left stick / A-D move along +right (it moved
  along -right). Right stick right = turn right, left stick right = move right.
- `source/game/freeride/chase_camera.cpp` - the back / forward offsets of the chase eye and target lie in the
  plane square to the (smoothed) car up axis. With the horizontal heading and the tilted up, a nose-down pitch
  took `posOffset x sin(pitch)` off the height. `-logcameras` now also logs every frame with the eye under
  1.2 m above the car ("LOW ...").

### Verified
- Chase: `-gm FREERIDE -tp 11635,13,17477 -heading 90 -fixedtimestep -logcameras -autoplay
  "60:hold=accel;600:release=accel;600:hold=brake;800:release=brake" -exitframe 900` - before: eye above car
  0.72..1.80 m, 12+ LOW frames (`eye y - car y` stayed 1.50 while the up tilted 0.12 forward); after: 1.50..1.53 m,
  0 LOW frames.
- Photo camera: the sign change only, built; **not driven with a pad** (no headless photo-mode hook was run).
  Owner: pause -> camera, push both sticks right.

### Not done / next
- The photo camera was not exercised headlessly; if a direction is still wrong, `photo_camera.cpp` lines 97 /
  124 are the two signs.

## Session 2026-11-05 (Q7 body, `win`): the Audi Q7 draws its whole body again

The owner: "Q7 missing panels" (seen on the wheel shots: bonnet, wings and pillars missing, the seats visible
through the front).

**Reproduced**: `-userdata C:\temp\otdu2-win-wheel-Q7_V12 -playername BuyTest -gm FREERIDE -clock 12.00 -tp
11635,13,17477 -heading 90 -autoplay "150:look=1,0;160:look=1,0;170:look=1,0;250:shot=..."` (a profile that drives
car 348). `-logcaritems BODY_HR`: the 15 BODY_HR items all `visible 0`.

### What changed
- `source/game/cinematic/cinematic_car.{h,cpp}` - the `Paint_LR` / `Plate_LR` hide: a node whose items are all low
  res still hides; a visible node that also has high res items keeps them and skips only the low res items
  (`lowResItems` -> `EdenDrawParams::ForceHiddenItems`, `bDeferBlended` set). The Q7's `BODY_HR` holds one
  `Paint_LR` prim (item 10), which hid the whole shell. `-lrnodehide` = the old rule; `-logcaritems [node]`
  dumps a car's items (node, LOD, material, shader, blend, visibility, box).
- `source/render/eden/eden_model.h` - `isNodeVisible`.

### Verified
- The Q7 whole, head lamp lit (`docs/images/q7_body_before_after.webp`); mean diff to the old rule 1.2.
- Range Rover Sport HSE, Mustang Fastback '68, SLK 55 AMG (the other cars with a mixed LR node): mean diff
  0.000 against `-lrnodehide`; each run deterministic (two runs 0.000).

### Ruled out (with the test that ruled it out)
- The doors swung open / mis-posed: DOOR_L / DOOR_R locals are plain translations (`-dumpcar`), as on the
  California.
- The cockpit LOD switch (`-carlod`): it only swaps the interior; the hole is identical at 0 / 1 / 2.
- A first version that always passed draw params darkened the Range Rover / Mustang head lamps (mean diff
  0.33): a params draw records blended items unsorted unless `bDeferBlended`.

### Not done / next
- The swinging wipers' params now defer their blended items too (the same flag): not looked at in rain.
- Other cars were not audited one by one; the per-item log line ("hidden in node ...") names any car that
  takes the new path.

## Session 2026-11-05 (wheels in their arches, `win`): cars sit at their ride height, the wheels no longer poke through the body

The owner: "some cars have oddities there was a zonda a audi and some cars wheels poke through the body work",
then "Wheels through body".

**Reproduced**: `CinematicCar::load` lifted every `D_RIM_` node to the tyre radius - Zonda F 0.093 m, R8 V10
0.171, TTS 0.159, S3 0.229, Q7 0.297 - with the body left where it was, so each wheel rode that far up into its
arch (`docs/images/wheels_s3_arch_before_after.webp`, left: the rim above the fender lip).

### What changed
- `source/game/cinematic/cinematic_car.{h,cpp}` - `getBodyLift()`: the model origin (the body underside, y = 0 on
  every model, `-loghubs`) sits the DB static ride height above the road (`_ride_height_front/_rear` mean;
  fallback tyre radius - authored hubs). The rim nodes move only to tyre radius - body lift (a few cm). The
  `-loghubs` dump (hub, body minimum, ride heights) stays; `-wheellift` = the old behaviour.
- `source/physics/vehicle_dynamics.{h,cpp}` - `setModelLift`: `getWorldMatrix()` is the model frame (body frame
  raised by the lift); `reset` / `getPosition` stay in the body frame.
- `source/game/freeride/gm_freeride.cpp`, `source/game/shop/gm_testcar.cpp` - the physics hub = model hub + lift,
  `setModelLift`; `startDriving` / `finishWakeup` (a model pose) subtract the lift; the brake disc centre converts.

### Verified
- Side shots (`-clock 12.00 -tp 11635,13,17477 -heading 90`, `look=1,0` x3) with test profiles driving the S3 / Q7 /
  Zonda F: the S3's front rim was above the fender lip, now inside the arch; the Q7 body sat on its wheels, now
  at its 25 cm ride height.
- Car wash cutscene (`-atwash`, validate): before, the rear wheel tucked up under the fender and the sill on the
  road; after, the car on its wheels (`docs/images/wheels_car_wash_before_after.webp`).
- Physics unchanged: `-logdrive`, `60:hold=accel`, 420 frames - S3 ends at x 11715.59, 91.5 km/h (old 91.4),
  4912 rpm, gear 3; Q7 11726.73, 101.7 / 101.7 km/h, 4389 rpm, gear 2.

### Ruled out (with the test that ruled it out)
- `-car <name>` is not a fair test: it keeps the California's rims and tyre (radius 0.339 for every car).
- Raising the whole car by tyre radius - authored hub: the S3 then rode 2.7 cm high (the models hang their wheels
  a little below the resting place) - the DB ride height is the anchor.
- A draw-time offset only: cutscenes and showroom cars place the model by its own origin and would float.

### Not done / next
- The Q7 draws with its front panels missing (both builds) - a separate model problem.
- The Zonda side shot came out in the in-car view (its default camera); a chase-view shot of a Zonda is still to
  take: `-userdata C:\temp\otdu2-win-wheel-Zonda_F -playername BuyTest -gm FREERIDE ... "120:press=camera;..."`.
- (verified) `-vehcheck`: 137 cars, top speed within 1 %: 113 (as recorded), 0-100 within 0.30 s: 70 (recorded 69).

## Session 2026-11-05 (dealer buy window, `win`): the hostess is at her desk and Buy lets you pick rims, paint and interior

The owner: "as for car dealership when buying a car now there's no sales assistant and you can't pick the
cars colour, interior or rims", then "Yes, do the dealer".

**Reproduced**: `-gm SHOP -shopspot ECD_AUDI_1276I` logged `free-ride visit, intro C_INTRO_I, loop ''` (empty
desk), and Buy bought on the spot: the port treated `buin` (open the buy window) as the purchase.

### What changed
- `source/game/shop/gm_shop.{h,cpp}` - `C_LIFE_HOSTESS` first in the free-ride loop list (all 14 dealer
  banks have it), `C_INTRO` as the intro fallback (icd_ferr). The buy window, protocol from the readable
  dealer handler `FUN_009f0840` (spot_interiors_spec 7): `openBuyWindow` (buin), `stepBuyWindow` (neop / prop,
  pages 1 car / 2 wheels / 3 paint / 4 interior / 5 confirm), `applyBuyItem` (chop: reads `/:num_item`),
  `writeBuyPrices` (op_price_1..3, cur_op_price, total), `paintBuySwatches`, `closeBuyWindow` (buou: the slot
  car gets its paint back). `computePrice` (car + rims + paint + the chosen interior). Purchase saves the rims
  and interior. `-logbuywindow` logs the clip tree and the arrow-key routing.
- `source/flash/flash_player.{h,cpp}` - `setClipColor` (Color.setRGB from the game, opaque);
  `gotoLabel(..., bRerunSameFrame)` (opt-in, used by the confirm jump only).
- `source/game/cinematic/cinematic_car.{h,cpp}` - `getDatabasePaintSwatch` / `FindLibrarySwatch` (the paint
  row's material `RAMPCLR1`).
- `source/game/player_profile.{h,cpp}` - `OwnedCarRims` / `OwnedCarInteriors`, saved as `car_rims` /
  `car_interiors`.
- `source/game/freeride/gm_freeride.cpp` - the driven car loads its ordered rims (`FindDatabaseDesc`).

### Verified
    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-buytest -playername BuyTest -gm SHOP -shopspot ECD_AUDI_1276I -shopaim 6 -money 500000 -nopad -fixedtimestep -autoplay "150:press=validate;450:press=validate;560:press=validate;720:press=right;820:press=validate;1100:press=right;1200:press=right;1320:press=validate;1470:press=right;1580:press=validate;1720:press=validate" -exitframe 2300
- `loop 'C_LIFE_HOSTESS'`; the hostess behind the desk (`docs/images/dealer_hostess_at_desk.webp`).
- `buy window for Audi R8 V10 FSI Quattro: 5 rims, 12 paints, 13 interiors, 147400`; rims item 2 -> 148280
  (+$880, "Optional 5-arm double-spoke design Rims"); paint 3 "Ice Silver with Apollo silver sideblade" ->
  145880, the podium car repainted, 12 squares in the real colours; interior 2 "Black Fine Nappa leather (red
  couture)"; "Buy this vehicle?" -> `eval` -> `buyc` -> `bought ... colour 2, rims 1, interior 1 for 145880`
  (`docs/images/dealer_buy_window_rims.webp`, `_paint.png`, `_interior.png`, `_confirm.png`).
- `profile.sav`: `car_paints = 442:2`, `car_rims = 442:389`, `car_interiors = 442:1`; the next FREERIDE:
  `driving the profile's car 442 'R8_V10', rims audi/r8_spyder_f_03 (ordered at the dealer)` (default
  `r8_v10_f_01`), `in its ordered paint 2`.

### Ruled out (with the test that ruled it out)
- `/:has_colors` as a boolean: the movie compares `=== 1` (decoded the AVM1 at shopscar.bnk 0x911fe).
- `invoke("/buy", "next")`: `next` / `confirm` are frame labels (the exe's vtable +0xdc is gotoAndPlay).
- `gotoAndPlay('next')` as the exe does it: the arrows of the page go dead in the port (`taken 0` on every
  press, `-logbuywindow`); without it all three pages take the arrows. Kept out, documented [INF].

### Not done / next
- The podium car does not swap rims while browsing; the interior is saved but not drawn.
- The per-page camera `FUN_009f2d80(car, page)` (the retail moves to the wheel / the cabin) - readable, next
  to read; the rims picture box stays empty.
- The interior squares are grey: `_interior_color_n` ids (`C_N_02`) are not in `colors.2dm` - find their
  library.
- Why the movie's own colours script (0x911fe) is not reached in the port: a flash_player frame-routing
  question, `-logbuywindow` dumps the tree.

## Session 2026-11-05 (exits at the door, `win`): leaving a house, a shop or the casino puts the car outside that building

The owner: "the 1st thing I want you to fix is the spawning issue like coming out the casino, dealership etc...
should spawn you just outside that location you exited from", then "Casino should spawn out side casino,
dealership out side the dealership and houses out side the house that you was in". Same report, not done yet:
holes near a roundabout by a motorway, wheels through the body on some cars (a Zonda, an Audi), and the dealer
has no sales assistant and no paint / interior / rims choice when buying.

**Reproduced**: a house or walk-in shop left the car wherever the enter prompt was taken (the prompt works from
tens of metres away); the casino flew you straight back to the Ibiza seaplane dock. The SHOPSCAR dealers were
already right (ECD_AUDI `gofr` -> `handoff at 'ECD_AUDI_1276I' entrance (11635.4, 13.2, 17477.4)`, on the road at
its door).

### What changed
- `source/game/freeride/gm_freeride.cpp` - `FindSpotEntranceById` (the spots.bin record by id; a record whose
  second point is 0,0 has no entrance); `InteriorWalk::eResult::Leave` teleports the car to the spot's road-side
  entrance (+0x28 / +0x38, the point the dealer and garage exits already use) [INF: the retail walk-out is ENC].
  On the casino island, near the dock: A = back into the casino (no flight), X / Square (MENU_OPTION_1) = the
  seaplane home (`RequestFlightHome` with the current car), with a hint once per approach.
- `source/game/casino/casino_trip.{h,cpp}` - `FindCasinoDoor`: the ECAS record's second point on the casino island
  (2749.1, 1.6, 1468.1), heading -2.30 [DATA].
- `source/game/casino/gm_casino.cpp` - `leaveCasino` hands FREERIDE the casino island at that door;
  `-casinoflyhome` keeps the old direct flight.

### Verified
- House: `-tp 36345,5,5765`, validate, back x4 -> `out of 'EPH_L3DI_5067I' -> the car at its entrance (36329.8, 5.2,
  5743.8)`; on the driveway facing out (`docs/images/exit_house_at_door.webp`).
- Clothes shop: `-tp 11530,4,16850` -> `teleport to (11543.7, 4.6, 16830.4)` (EAS_D's door).
- Casino: `-gm CASINO -casinoleave 120` -> `FREERIDE at the casino door`, `island 'casino'`, car on the road with 4
  wheel contacts (`docs/images/exit_casino_at_door.webp`); `760:press=validate` -> `back into the casino`;
  `760:press=opt1` -> `C_CASINO_DEPARTURE`, `C_IBIZA_ARRIVAL`, FREERIDE at the Ibiza dock (24003, 24520).
- Dealer: `docs/images/exit_dealer_at_door.webp` (unchanged code, re-checked).

### Not done / next
- The dealer's empty desk and missing paint / interior / rims pages: `gm_shop.cpp` logs `free-ride visit, intro
  C_INTRO_I, loop ''` - start at which actor / background loop the free-ride path drops versus the story shop.
- The holes and the wheels through the body need the owner's position / car names (PROGRESS.md Open rows).
- The casino-island zone badge still says "Ibiza Area 1" (the known hardcoded badge).
- Not felt in play with the pad.

## Session 2026-11-05 (casino desks + radio names, `win`): the bar and cashier run their retail movies, the cherry raffle is in, and a song name no longer stops the game

The owner: "cline had apparently added in the cashier buying and selling chips but their build isn't up to
date I would like you to commit and push that change ... hook the correct ui for it also implement the
barman ... buy drinks and in a drink you have a random chance for something". Then: the game "refused to
load" after adding songs and an extra station (Annual Hardstyle); removing the .opus files fixed it.

### What changed
- **Cline's uncommitted work** in `C:\OpenTDU2-cline` (desk volumes + the floor wallet re-reading
  `PlayerProfile::CasinoChips` for the F4 cheats) committed there as `a88e201`, pushed to `origin/cline`,
  merged here (fast-forward). Its sync reset `-casinochips` to the profile's 0: `-casinochips` now sets the
  profile too.
- `source/game/casino/casino_desk_hud.{h,cpp}` (new): the bar on `Cas_Common.vmf` and the cashier on
  `Casino.vmf /money_conv` - protocol, data and raffle in `docs/re/casino_spec.md` 9.12.
- `casino_interior.{h,cpp}`: the desks use their retail `SelectableZone` volumes and strings
  (`'cash'` / `'barman'`) and read `DeskData` (cashier gift 200, ticket chances 1/2/3/5 %, the cocktail
  cutscenes).
- `gm_casino.{h,cpp}`: `openCashier` / `openBar` / `buyCocktail` / `tickDeskHud`; barman ->
  avatar drink -> raffle popup; the old native screens stay behind `-nodeskhud`.
- `player_profile.{h,cpp}`: `CasinoRafflePasses[4]`, saved as `casino_passes`.
- **Radio (the load failure)**: `Hardstyle fm` holds `Da Tweekaz - ... Moana ＂How Far I'll Go＂ ...opus`
  whose quotes are **U+FF02** (fullwidth). `std::filesystem::path::string()` converts to the ANSI code page
  and **throws** on a character with no mapping; the station scan did that for every song, so one name
  stopped the game in `RadioPlayer::loadCustomStations` (the log ends right after the retail stations).
  It was never the .opus extension - the bad names happened to be .opus files. New `source/core/path_utf8.h`
  (`PathToUtf8` / `PathFromUtf8` / `PathToDisplay`, never throw); the radio keeps song / art paths in UTF-8
  and shows names in Windows-1252 with look-alikes folded (`＂` -> `"`); `audio_decoders.cpp`,
  `gs_audio.cpp` (log lines) and `freeride_hud.cpp` (station art) follow; each station folder's scan is in a
  `try` (a bad folder is skipped with a warning).

### Verified
- Bar: `-gm casino -casinoroom hall -casinoselect 1 -money 5000 -allcocktails -cherry -fixedtimestep -logdeskhud`
  -> `bar FSCommand 'buy cock 12'`, `'clco'`, `GMCasino: cocktail 12 (tier 2, $200) ... WIN (cherry)`,
  cutscenes `C_COCKTAIL_BARMAN`, `C_COCKTAIL_AVATAR_WIN`, the popup "You win - Level 2 free pass"
  (`docs/images/casino_bar_drink_and_raffle.webp`, `casino_bar_raffle_tutorial.webp`). Without the flags: 4 of 15
  open, a ¤50 cocktail rolls `45 < 1 % -> lose`.
- Cashier: `-casinoselect 0 -money 5000` -> `casi:chx1`, `slri` x3, `conv:maxv`, `conv:vali` -> `dollars->chips
  5000 -> 5000 chips, $0`; `-casinochips 300` chips -> dollars 3 -> 297 chips, $1003; rate text `$1 = 1 ¤`
  (`casino_cashier_converter.png`).
- Radio: a station holding only the U+FF02 file - before: the log stops after `station 3 'Roadtrack'` and the
  process hangs; after: exit 0, `playing Da Tweekaz - Da Tweekaz - Moana "How Far I'll Go" (Official Preview)
  (... 260.0 s)`. The owner's real `Annual Hardtyle` (108 songs) + `Hardstyle fm` (14) linked into a scratch
  stations folder: both listed with their icons, both play, exit 0.

### Not done / next
- The raffle popup's level icon reads "1" on a level 2 win (`tombola_popup/content/level`, the text is right).
- The walk reticle draws over the bar cutscenes (same as the jackpot ones).
- The passes are kept but nothing spends them (the Solar Crown tournament needs other players).
- The owner's folder is `Annual Hardtyle` (typo) - the station is named after the folder, so it shows as typed.

## Session 2026-11-05 (casino rankings, `win`): the rankings tube rises behind the podium and shows its retail page

The owner: "Yes, do rankings" (the screen "meant to show fastest times around casino island in the Audi").

**Reproduced**: the black box beside the podium = `FlashScreen_CRNK_TimeAttack` at its raw placement; no
`C_RANKINGS_*` cutscene ever played and no movie was on it.

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm CASINO -casinoroom hall -skipcine -nopad -fixedtimestep -walkat "0.54,1.2,-27.0,180" -autoplay "120:shot=C:/OpenTDU2-win/_r1.ppm;150:press=validate;230:shot=C:/OpenTDU2-win/_r2.ppm;260:press=back" -exitframe 340

### What changed
- `source/game/casino/casino_ranking_screen.{h,cpp}` (new) - the tube state machine on the four cutscenes,
  the `Cas_Ranking` movie (`time_attack_rank`) in a render target on the screen face.
- `source/game/casino/casino_interior.{h,cpp}` - attach in the hall, the `crnk` selectable (its
  SelectableZone), A = RankingCamera view, B = back to the walk; updates in tick / tickBackground / offscreen.

### Verified
- `docs/images/casino_rankings_tube.webp`: closed (only the hatch), raised as the walker comes near, the
  RankingCamera close-up. Log: `C_RANKINGS_OPEN` -> `C_RANKINGS_STAND_OPENED`, `looking through camera
  0x96a3ae63d18431c0`. `-rankingsclear` turned the face red -> the binding works; the movie draws the page.

### Ruled out
- The binding (red clear A/B): the black face was the movie's empty first frame until `time_attack_rank`.

### Not done / next
- The page says "Connecting to server": the movie's row protocol (`resu_rankings_array`, `../:pos`,
  `../:player_name`, `../:score`) is not decoded and there are no times offline (no casino time attack).
- The five other ranking screens (general, satellite, sit and go) have no movie.

## Session 2026-11-05 (casino cutscenes, `win`): slot jackpots play their cutscenes, the car keys drop and are picked up, C_WINCAR in the hall

The owner: "It's a screen that pops up it's meant to show fastest times around casino island in the Audi that's
on the podium and the car slot if you match all 3 symbols of the car the keys drop out for that car there should
be certain little cutscenes for these as well", then "The key dropped and avatar picked it up".

**Reproduced**: no cutscene played on a jackpot; `-casinocin C_JACKPOT_CAR_MONEY` showed sand and sky - the
walk camera (walker at the room origin) overrode every casino cutscene camera.

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata <a copy of the userdata> -gm CASINO -casinoroom slots -casinoselect 7 -casinoprize car -skipcine -nopad -fixedtimestep -autoplay "1520:shot=C:/OpenTDU2-win/_k.ppm" -exitframe 3150

### What changed
- `source/game/casino/casino_interior.{h,cpp}` - the walk camera only while walking (`bWalking`);
  `playCinematic` / `playCinematicAtPlace` (machine anchor from its `orient_pos_scale`), `handleEndEvents`
  back to the walk or the machine camera, A/B skip; `-casinocin <name>` / `-casinocinat <i>` test hooks.
- `source/game/casino/gm_casino.{h,cpp}` - `playSlotJackpot`: the machine kind's `C_JACKPOT_*` (`_W` for a
  woman) on a jackpot / prize; the first car then `C_WINCAR` in the hall; no shell / HUD over a cutscene;
  `-casinoprize` with `-casinoselect` goes through the same path. `-nocasinojackpotcin`, `-nocasinowincar`.
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` - `setCinematicOrigin`; the player actor also as
  `Avatar_`; a prop whose clip moves no bone draws on its track; the key ring is taken in `Hand_R` once
  dropped and within 0.12 m (`-nokeygrab`); `-logcincam`, `-logpropat <name>` probes.
- `source/game/cinematic/cinematic_scene.h` - `CinematicScene::setOrigin`.

### Verified
- `docs/images/casino_jackpot_keys_wincar.webp`: the car machine's jackpot shots, the key ring in the tray, the
  avatar standing with the keys in hand, then the hall ceremony (hostess + director at the podium, the R8).
- Log: `jackpot cutscene 'C_JACKPOT' at machine 7`, `'CF_KCar' taken in the hand at t 24.03 (0.11 m)`,
  `cinematic 'C_WINCAR'`; 0 stalls. Hall walk start vs the previous build: mean diff 0.05; walking moves.

### Ruled out
- Scene origin wrong: the camera already applied it (`-logcincam` printed nothing - the main camera never
  ran); the walk override was the cause.
- Key ring hash mismatch: `cf13cab40791bea5` matches; its Visibility track shows it from 20.66 s, and
  `-nopropanim` showed it dropping -> its skinned draw was the gap.

### Not done / next
- **The rankings tube** (the "black box" by the podium): `TubeEcransRankings` + `FlashScreen_CRNK_TimeAttack`
  rise / sink with `C_RANKINGS_OPEN` / `_CLOSE` (icashal). Needs its trigger and the
  `flashscreen_ranking_timeattack` screen movie with offline casino-island times in the podium Audi.
- The small `C_SLOT_WIN_0x` / `C_SLOT_LOOSE_0x` shots; the other machines' jackpots are wired but only the
  car's was watched.

## Session 2026-11-05 (casino hall, `win`): the red carpet and the podium under the R8 show

The owner (with a screenshot of the hall): "it's not 100% accurate there should be a red carpet and a podium
in the center with car on top".

**Reproduced**: the car stood on a flat olive-grey floor, no carpet runway, no podium:

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm CASINO -casinoroom hall -skipcine -nopad -fixedtimestep -walkat "0.54,0.6,-16,180" -autoplay "150:shot=C:/OpenTDU2-win/_hall.ppm" -exitframe 160

### What changed
- `source/world/eden_world.{h,cpp}` - `EdenWorld::setAreasEnabled`: the area tiles (terrain + area objects)
  can be switched off.
- `source/game/avatarchoice/intro_cinematic.cpp` (`IntroCinematic::load`) - a scene with no island
  (`bWorld = false`, no streaming) turns them off. `loadAround` loads every area tile whatever the radius and
  the closed room's frame sits at the Ibiza origin, so that tile cut through the hall floor (~-0.6 m) and hid
  the sunken carpet pit and the `Socle_Car` podium (both were loaded all along). `-closedroomareas` = A/B.

### Verified
- `docs/images/casino_hall_carpet_podium.webp`: from the entrance and close up - the red runway, the lit
  podium, the R8 on it.
- Other closed rooms with vs without `-closedroomareas`: FIRST_WM garage 0 px changed, casino slots 3 px,
  VIP lounge lighter overall (the Ibiza tile also shadowed it).

### Ruled out
- `-hidematerial Floor_gloss` (the alpha gloss layer over the floor): identical frame.
- `-noterrain`: identical frame. `-noareas`: the carpet and the podium appear -> the area tiles.

### Not done / next
- The hall's unlabelled black box right of the podium (visible in both builds) is not identified.

## Session 2026-11-05 (road signs, `win`): the Ibiza signs match the x360 photos

The owner: "I was hoping you could fix our road signs as there not accurate and some are broken in some way
or another" - four x360-recomp photos as the reference (TdirIb "Centre / Platja" and "Sant Rafael / Hisenda
Aldea Oest / Platja", Dist_D "Centre 1 / Platja 1 / Puerto 1", DirR_D "SANT MIQUEL / Residencial Nord /
Foradada"). The same message says the casino hall lacks the red carpet and the podium with the car on it
(not started this session - see Not done).

**Reproduced** (the photo signs, found with `tools_probe/sign_locate.py -match`; TdirIb faces look along +Z,
so stand on the far side of the probe's `-spawn`):

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -island ibiza -skipcine -nopad -fixedtimestep -clock 13:00 -tp 11847.5,5.5,16183.7 -heading 170.3 -width 1600 -height 900 -autoplay "300:press=camera;302:press=camera;360:shot=C:/OpenTDU2-win/_sg.ppm" -exitframe 370

(Sant Rafael TdirIb_C; Centre/Platja TdirIb_B `-tp 12305.2,7.5,16575.2 -heading 258.5`; Dist_D
`-tp 12110.2,5.4,16264.6 -heading 251.6`; DirR_D SANT MIQUEL `-tp 11649.7,4.5,16148.9 -heading 244.3`.)
Before: no arrows, no pictograms, only the first TdirIb row had a yellow tip, the middle plate of the 3-row
TdirIb squashed with the spare bar's outline, black text on green, SANT MIQUEL mirrored, the distance sign's
green header "Centre 1 | 1 entreC", the highway panel's rows rotated 90 degrees.

### What changed
- `source/world/sign_text.{h,cpp}` (sign_text_spec 6):
  - rows keep their pictogram byte (`Icon`) and raw distance byte (`DistanceCode`); the code distance goes on
    every row, a zero distance byte repeats the row above (the photo's "1 / 1 / 1").
  - TdirIb: the row byte is the arrow (14 right, 12 left - photo; 11 up, 13 down [INF]); a black arrow in the
    yellow tip, names narrow to fit before it.
  - stacked plates: each member's orientation is measured (`rs_tdirib_c`'s middle plate maps the cell upside
    down) and un-flipped by the remap; the member's source band (`SrcV0/1`) is copied over its spare bar in the
    face, so every row keeps the plate's look (yellow tip on all rows).
  - `FlipU` is measured per zone from the quads (u at min / max model x): `rs_dirr*` reads straight - the
    family rule mirrored every right-pointing direction sign; `rs_dist` green header is a folded mapping,
    unfolded in the geometry hook (`UnfoldMirroredPlates`) + mirror-copied texels; `rs_hwdist` is a rotated cell
    (`bRotated`); town signs drop the phantom "8" (a model id: the census, spec 6.1).
  - lettering: the gauges movie's "Arial" with a padded faux bold (white on the green bar); procedural framed
    pictograms (beach, harbour, airport, old town); the rs_dir row stays off the pointed end.
- `source/world/eden_world.cpp` - `-logsigns` names the model before its plate quads are logged.
- New flag: `-signfontberlin` (the old Berlin Sans FB lettering, A/B).

### Verified
- `docs/images/sign-faces-2026-11-05.webp`: the four photo signs + a DirL_D and the HwDist_C panel, all reading
  upright with the photo's arrows / pictograms / colours.
- `-logsigns`: `remapped 3 stacked plate row(s) ... (1 upside down)` on rs_tdirib_c; `unfolded a mirrored
  plate at u 0.166 (4 vertices), zone u 0.005..0.328` on rs_dist_b/d.
- After the reformat to the house 120-column style: the seven sign shots re-run, mean abs diff 0.00 on all
  channels (pixel-identical). 900-frame Ibiza town drive `-tp 12605.4,16698.1 -snaproad 150 hold=accel`:
  exit 0, 0 stalls.

### Ruled out (with the test that ruled it out)
- A sign font / arrow sheet in the data: a name scan of every non-sector bank (font, sign, arrow, picto,
  panneau...) finds only Hawai'i props, the frontend fonts and road-paint `arrows.2db` / `road_arrows.2db`
  (dumped: white paint arrows) - so arrows and pictograms are drawn procedurally [INF].
- The TdirIb "distance" being kilometres: 11/12/13/14 only, 1,832 rows; 12/14 sit back to back at one junction.

### Not done / next
- The casino hall (owner, same message): done in the next entry (the island tile through the floor).
- 11/13 arrows and the airport / old-town pictograms need an x360 shot (sector-3-3-4-7 "Barri antic 13").
- HwDist "EIVISSA ... 1 2": two digits inside the name window, meaning unknown (drawn as text).
- `.clang-format` says ColumnLimit 80 but the tree is written at 120 - formatting a whole file with it churns
  ~600 lines; format only your hunks (or a 120 copy of the config).

## Session 2026-11-05 (walls + floors in every house, `win`): all 21 living rooms checked

The owner: "Yes, check others".

### What changed
- Scanned the 21 `lph_*` material libraries: every one has `Mat_Floor1_<nn>`, 20 have `Mat_Wall1_<nn>`
  (not the caravan), l2e has a second floor `MAT_Floor2_23` (houses_spec.md 13.1 has the table).
- `intro_cinematic.cpp applyRoomMaterial`: the match is `mat_floor<d>_<n>` / `mat_wall<d>_<n>` (lph_l1bi's
  `MAT_FLOOR_03` is another surface).
- `gm_freeride.cpp`: `-enterhouse <DBHouse spot>` test hook (straight into any living room).

### Verified
- `-tp 22000,23000 -enterhouse <spot> -walkaim diary -surfaceedit floor|wall`: EPH_L2E_0345I 2 floor / 1 wall
  material(s), EPH_L1D_1455I 1 / 1, EPH_L0C_1254I 2 / 0; screenshots
  `docs/images/house_mytduhouse_floor_other_houses.webp` (l1d wood -> marble, l2e slate -> marble). Exit 0.

### Not done / next
- The other 17 banks were checked by their names only, not entered. The `_level` gate; HOME's own item menu.

## Session 2026-11-05 (MyTDUHouse walls + floors, `win`): the walls and the floor can be changed

The owner: "Yes, walls and floors".

### What changed
- `house_furniture.{h,cpp}`: the DBMaterial catalogue (`ground_nn` floors `_type` 1, `wall_nn` walls `_type` 2).
- `intro_cinematic.cpp`: `loadFurnitureModel` also reads Scenery-entry banks (the finish banks);
  `applyRoomMaterial( prefix, bank )` copies the finish's textures into the room's `Mat_Floor*` / `Mat_Wall*`
  materials slot by slot type, keeping the room shader and lightmap; originals kept for revert.
- `interior_walk.cpp`: in MyTDUHouse, A with nothing selected edits the floor (looking down) or the walls;
  Left / Right, A buy, B revert; saved as `#floor` / `#wall` choices, applied at load; `-surfaceedit`.
- Controls: in MyTDUHouse, **Cross / Enter** looking at the floor or a wall (no furniture under the reticle),
  then **D-pad left/right**, **Cross / Enter** buy, **Circle / Esc** cancel.

### Verified
- Floor: own -> Light Marble Tiling ($5000) -> Massaranduba Parquet ($4000) bought, saved `#floor=ground_02`,
  reloaded on the next run (`house_mytduhouse_floor.webp`). Walls: Tuscany-red -> Titanium-grey Concrete ->
  Yellow-saffron Plaster (`house_mytduhouse_walls.png`; 7.9 / 8.7 % of the pixels changed).

### Ruled out
- Copying the whole finish material: the floor drew flat white. The finish's lightmap slot ("...LIG",
  its FAKE_LM) was the cause; the slot copy now skips it.

### Not done / next
- The other houses' surface names (only lph_l3di read), the `_level` gate, HOME's own item menu.

## Session 2026-11-05 (MyTDUHouse colours, `win`): furniture colours and matching sets

The owner: "Yes, colours next".

### What changed
- `eden_model.{h,cpp}`: `setMyHouseColor( n )` re-points the items drawn with `_MH<part>_<nn>` at
  `_MH<part>_<n+1>` (A / B / C at once), `countMyHouseColors()`.
- `intro_cinematic.{h,cpp}`: the slot keeps its colour (`DefaultColor`, then the choice) and `Group`;
  `setFurnitureColor`; a model swap keeps the colour; choices are `<bank>#<colour>`; a suffixed slot reports
  its base bank.
- `interior_walk.{h,cpp}`: the edit has Up / Down for the colour, edits the whole Group (dining chairs,
  bar stools) together, B reverts all of them, colour-only changes are free.
- Controls: in the MyTDUHouse edit, **D-pad up/down / arrows** change the colour (left/right the model,
  **Cross / Enter** buys or applies, **Circle / Esc** cancels).

### Verified
- sofaL_A: colour 1/5 -> 2/5 -> 3/5, applied, `sofal_a#2` (`house_mytduhouse_colours.webp`).
- ChaiBarD_01 (group 3): "3 slot(s)", CONTEXT -> BUBBLE on all three, colour 2, $600
  (`house_mytduhouse_group_barstools.webp`). Reload: every slot logs its saved bank + colour. Exit 0.

### Not done / next
- Walls / floors (`MAT_WALL1_`, `MAT_FLOOR1_`, `ground_01..25`), the `_level` gate, HOME's own item menu.

## Session 2026-11-05 (MyTDUHouse furniture, `win`): the furniture of a house can be changed and bought

The owner: "Yes, furniture next" (the TDU Life / MyTDUHouse furniture left open by the entry below).

### What changed
- `source/game/spots/house_furniture.{h,cpp}` (new): the DBFurniture catalogue (`_bank`, `_type`, `_price`,
  `_name_hc`, `_iscasino`; `-logfurniture`). A slot's Type = row `_type` - 1 [DATA, three pairs].
- `intro_cinematic.cpp`: `loadFurniture` split into `loadFurnitureModel` (one slot from one myhouse bank),
  `getFurnitureSlots`, `replaceFurniture` (live swap; the old model is retired until unload),
  `setFurnitureChoices` (the saved choices applied at load).
- `interior_walk.cpp`: furniture selectables (MyTDUHouse only), the edit (Left / Right swap, A buy, B revert),
  `-furnaim <slot>`. `player_profile`: `FurnitureChoices` (saved `furniture_choices`).
- Controls (DualSense / keyboard): in MyTDUHouse, **Cross / Enter** on a piece starts the edit, **D-pad
  left/right / arrows** change the model, **Cross / Enter** buys, **Circle / Esc** cancels.

### Verified
- `-home -walkaim diary -furnaim sofaL_A` + Info -> MyTDUHouse: "13 furniture selectable(s)", "editing
  'sofaL_A' (type 8, sofal_a), 13 model(s)", right x2 -> sofal_b, sofal_e, A -> "$20000 (money 480000)",
  saved; the next visit logs "furniture 'sofaL_A' (type 8, bank sofal_e)"
  (`docs/images/house_mytduhouse_furniture_swap.webp`). Exit 0.

### Not done / next
- The colour variants inside a model (`DefaultColor`, `_MHA_/_MHB_/_MHC_xx` materials), walls / floors, the
  `_level` gate, HOME's own MyTDUHouse item menu (the hint box stands in). houses_spec.md 11.

## Session 2026-11-05 (dealers + house UI, `win`): the dealers are the retail SHOPSCAR screen, the house runs HOME.vmf (closet, garage pick, MyTDUHouse)

The owner: "I want you to try wire houses wardrobes, garages, tdu life (this allows changing furniture) lastly
dealerships can we plug correct ui believe uses same as the used car dealership".

### What changed
- **Dealers** - `source/game/shop/gm_shop.cpp/.h`: GMShop (the story used-car dealer, SHOP mode + SHOPSCAR.vmf)
  takes a `FreeRideShopRequest` (player_profile.h) for any DBCarShop spot: interior `i<bank>`, exterior
  `<bank>`, the spot's slots, page by spots.bin type (0x11 `scr_open`, else `car_dealer`), `C_INTRO_I`,
  no story step / loan car / tutorial. Buy -> `C_BUY_CAR_I` -> FREERIDE at the entrance in the new car;
  Back -> dialog -> `gofr` -> FREERIDE. `gm_freeride.cpp enterDealerShop` hands over at a `cardealer`
  door (`-nativedealer` = the old walk + native panels). spot_interiors_spec.md 6.
- **The driven car** - `PlayerProfile::DrivenCarId` (saved `driven_car`): FREERIDE loads it at enter
  (owned check), so a purchase / garage pick needs no runtime model swap (the known device wedge).
- **House UI** - `source/game/spots/interior_walk.cpp/.h`: the house rooms load HOME.vmf (`-nohomeui` = A/B),
  its FSCommands are answered: the walk-in closet (`clin` / `neit` / `prit` / `seit`, `/:BtValid`,
  `/:Nb_Items`, `/:ItemName`), the Diary's Info menu, MyTDUHouse open + the GENERAL quit dialog
  (`dbxo` / `dbxc`). houses_spec.md 10 has the full table.
- **Worn clothes** - `PlayerProfile::WornClothes` (saved `worn_clothes`), `eden_avatar.cpp` swaps the slot's
  DBPNJ row for the worn DBClothe row on the player's config (`-noworn` = A/B).
- **Garage pick** - `gm_freeride.cpp`: A within 3 m of an owned car in the house garage sets the driven car;
  leaving the house after a change re-enters FREERIDE at the house entrance in it.
- **Crash fix** - `gm_freeride.cpp/.h`: `static bool bPuddlesWired` (and the FRIM bank / skill-event wirings)
  captured the first GMFreeRide's `this`; the second FREERIDE of a session (back from a shop, a test drive,
  the car wash) crashed in `EdenSurfaceFx::updateLightPools -> EdenWorld::getHeight` (cdb + a linker
  `/MAP`). Now per instance, cleared in `exit()`.
- Tools: `-dumpbuttons` also dumps movies loaded later and the UI viewer's movie (`DumpMovieButtons`
  public); `-shopspot`, `-shopaim <n>`, `-shoppage`, `-walkaim <name|type|car>`;
  `IntroCinematic::hasCinematic`, `WalkMode::placeLooking`.
- Controls (DualSense / keyboard): unchanged bindings - **Cross / Enter** validates (Buy, pick a car, wear),
  **Circle / Esc** backs out (leave dialog, closet back, Quit MyTDUHouse), **D-pad / arrows** move in the
  menus and page the closet strip.

### Verified
- `-gm SHOP -shopspot ECD_FERR_3337I -shopaim 4 -money 900000`: the 458 Italia's price + spec panel, the
  Buy / Get into the car / Data / Compare / Test drive menu (`dealer_shopscar_car_dealer_menu.webp`);
  validate x2 -> `buin` -> "bought ... money 699000" -> `C_BUY_CAR_I` -> FREERIDE "driving the profile's
  car 391 '458_Italia'" outside the dealer (`dealer_shopscar_buy_drive_out.webp`).
- `-gm FREERIDE -tp 21803,24216`, validate at the Ferrari door -> SHOP; back -> dialog -> `gofr` ->
  FREERIDE at the entrance; exit 0 (exit 139 before the crash fix; `-no3d` survived, `-nointeriorworld`
  survived - the streamed island + a second instance).
- House (`-home`, profile with clothes 419 / 505 / 285 / 287 / 307 and cars 391 / 387):
  `-walkaim dressing` -> Walk-in closet -> Hats (3 items: No hat + 2) -> right -> OK -> "wearing 419
  'Cowboy hat'", saved `worn_clothes = 5:419`; Back -> `avou` -> walk (`house_closet_wear_hat.png`). The
  player then walks into the Ferrari dealer in the hat (`house_closet_hat_worn_in_shop.webp`).
  `-housegarage -walkaim car` -> A -> "garage: driven car -> 391", back -> FREERIDE in the 458 at the house
  entrance (`house_garage_pick_car.webp`). `-walkaim diary` -> Info -> MyTDUHouse -> Back -> "Quit
  MyTDUHouse?" -> OK -> `myhs:clos` (`house_mytduhouse_quit_dialog.png`).

### Ruled out
- The dealer screen's missing header on new-car dealers is the movie: `car_dealer` draws no title strip
  (UI viewer `-goto /:car_dealer`); `scr_open` is the "Used Car Dealer" one.
- `ECD_FERR_7841` is the **Hawai** Ferrari row (no `I` suffix), not a broken Ibiza spot.

### Not done / next
- MyTDUHouse furniture: nothing is selectable in the mode yet. Next: make the `Entity__MyHouseObject` slots
  walk selectables (`IntroCinematic::loadFurniture`) and log the FSCommands HOME sends on one.
- The closet camera (`came`) - the walk stays first person; the brand logo frames of `/:logo_Brand`.
- HOME's own `/garage` page (`change_place`, `list`); MyTDULife / My Photos / Leaderboards content; Media.
- The owner's in-play check of the dealers (the native `-nativedealer` path keeps the old "Get into the car").

## Session 2026-11-05 (police HUD, `win`): the traffic-violation gauge is the retail bar, and the Chase Mode help explains it

The owner: "fix the traffic violations - there's normally a ui element with a bar that fills up before police
will chase you, also there's some dialog explaining it".

**Reproduced** - the "meter" was `/HUD/tailing` (char 707), whose readout is `/:distance_KYD`: the online
**Keep Your Distance** tailing event, not the police. It drew a red car + "333999m" over the build stamp
(`-policeviol`, shot at 260). The Help: Chase Mode popup (`-policehelp`) drew nothing: the port hides
`/help_system` at load and never showed it again, and nothing routed the pad to it.

### What changed
- `source/game/freeride/freeride_hud.cpp` `setChaseHud` - drives the retail gauge `/HUD/gauge_content/gauge`
  (sprite 502) [DATA: `-dumphud -dumpactionbytes -dumpsprite 502,501,487,478`]: `gotoLabel open` (its f8
  script reads `/:cur_gm`, a free-ride mode plays on to `up` - the slide-in), `close` f282. The fill is
  `content` (sprite 501)'s frame: 1000 frames slide the bar 160 px under its mask, **frame 1 full, 1000 empty**.
  `content/ic` + `content/bar` take the mode label: `infringement` before the chase (icon = police badge,
  text EditText 467 bound to `/:m_Infraction` -> loc 763 "Traffic violation"), `escape` during it (the
  flashing car, `/:m_escape_` -> loc 1978 "Escape", fill = the break-away `ChaseMode::getEscape01()`;
  which mode the retail chase shows is [ENC], escape is [INF]). `_root.isNoCursor = true` hides the cursor
  (the `up` frame script). `gauge2` stays hidden. The old tailing code and its `copDistance` are gone; the
  chase timer no longer writes its digits into `/:m_escape_` (that is the gauge label).
- `openChaseHelp` shows `/help_system` and fills the two pages' texts `/:m_TutoChaseMode1..4` = loc
  6681 / 6682 / 6683 (with the tokens 8 / 35 [INF], as ChaseMode) / 6684 [DATA: hud.bnk EditText bindings,
  FR authoring defaults]. `routeChaseHelpInput` = the casino help protocol: validate -> `_root.Clip.A()`,
  back -> `B()` (both close), left / right -> `Move('left'|'right')` (pages). `tickModal` advances the movie;
  the clip is hidden 0.8 s after `chmo:hsys:clos`.
- `gm_freeride.cpp` - the popup is **modal** like the pause menu (world waits, engine audio ducks). It opens
  1.5 s after the **first violation a police car witnesses**, once per profile (`PlayerProfile::ChaseHelpSeen`,
  saved as `chase_help_seen`) [INF: the retail seen flag is ENC]. `-nochasehelp` suppresses it;
  `-policeviol` now behaves like a witnessed violation (goal line + help).
- Controls (DualSense / keyboard): **Cross / Enter** or **Circle / Esc** closes the help, **D-pad left/right /
  arrow keys** turn its pages.

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -policeviol` (fresh userdata copy): frame 200 the gauge reads
  "Traffic violation" with the badge, 55 % filled left->right green->yellow
  (`docs/images/police_violation_gauge.webp`); 290 the help page 1 (ranks table, `police_chase_help_p1.png`);
  `press=right` -> page 2 with "8 consecutive wins" / "35 victories" and the police photos
  (`police_chase_help_p2.png`); `press=validate` -> "Chase Mode help closed", the world runs again.
- `-police` (chase): the gauge switches to "Escape" with the car icon (`police_escape_gauge.png`).
- `-policeviol -nochasehelp -no3d`: the bar has drained at frame 400 and the gauge is gone by 1100.
- `-gm` test runs have no character name, so `SaveProfile` skips them by design - the seen flag was not
  checked on disk; it saves in a real profile.

### Not done / next
- The awareness numbers (34 per witnessed hit, 6/s decay after 3 s, 100 to start the chase) are still [INF];
  the retail laws are [ENC] - PS3 twin search for the `infringement` string (pc_ps3_anchors.json: ps3_func
  0x9C3504) is the start.
- `/HUD/chase_mode_target` / `cop_detector` (online cop-side widgets) untouched.

## Session 2026-11-05 (GLM's build-459 findings, `win`): casino rooms open inside, the Clothes reel reads in bounds, the off-road ring steps aside for the level-up popup

The owner: "Are you able to look into the other stuff and fix please". The list
is GLM's build-459 sweep (`C:\OpenTDU2-glm\docs\images\glm\`, uncommitted there).

### What changed
- `source/game/casino/casino_interior.cpp` `loadRoom` - **every** room skips its
  `C_INTRO` (was the hall only). The "roulette / poker lose the room, dune
  backdrop" and "VIP spawns misplaced, dressing under a sofa" reports were all the
  intros: each frames the dunes + casino facade from outside (needs the exterior
  bank). `-casinoroomintro` = A/B. GLM's roulette shots at 250/500/1050 were all
  inside the intro / the screen opened over it.
- `startWalk` - VIP: `cam_from_Hall` is at (0.70, 7.24, 23.19), outside the room;
  starts at `Cam_FirstPerson` (0.57, 5.74, -29.98). `CAM_RANKINGS` tried and
  rejected (faces the screen at 1 m). `-logcasino` lists every room camera +
  position; the "walking" line now prints the start position.
- `source/game/casino/casino_slots.{h,cpp}` - **real out-of-bounds read**: the
  strip is built from the machine's first N symbols (Clothes 4 -> 47 stops, Car 6
  -> 61) but every read wrapped at `kStripLength` 64 -> `strip[47..63]` past the
  vector (the Clothes middle reel's blank seam). All reads wrap at `stripSize()`.
- `source/game/freeride/gm_freeride.cpp` - the return-to-road ring /
  "Return to the road" is not shown while the level-up popup is up (it showed
  through the translucent panel; the enter-spot prompt and spec card already
  stepped aside).

### Verified
- Rooms at frame 100 (`-gm CASINO -casinoroom <r> -fixedtimestep`): roulette,
  poker, VIP inside (`docs/images/casino_room_start_{roulette,vip,poker}.png`);
  before: dunes in slots/roulette/poker/VIP, sofa in dressing; with the intro
  off, the dressing walk ('C_main') was already fine.
- Slots self-test 50000: RTP 98.26 %, balance 99921750 = expected - identical to
  GLM's 459 numbers (64-stop machines unchanged). Clothes machine
  (`-casinoroom slots -casinoselect 0`, spin at 300/950): the drum rests on a full
  avatar (`docs/images/casino_clothes_reel_fixed.webp`; GLM's settled2 had a blank top).

### Ruled out / not bugs
- **"Spins: 0 never increments"**: GLM's own result shot shows `Spins 1`; it
  increments at the settle, after the wheel time.
- **"Level UP! double-draws"**: the movie's zoom-echo intro - a larger fading
  copy over ~20 frames, gone by the hold (`-testlevelup`, shots every 8 frames
  272..392; `docs/images/levelup_echo_anim.webp`).
- **Podium head "inside-out" (`C_A5_PODIUM_TESS`), `sta_firstintro` /
  `isl_firstintro` underlit + floating car**: all CINVIEWER captures (the browser
  panel is in the shots) of cutscenes **no game mode plays** (`grep` finds no
  caller). The podium one is the camera inside Tess's head (the back of the hair
  seen from inside), i.e. the scene's placement without its race end-spot; not a
  mesh fault. They become real work when the race podium / race school story
  steps exist.

### Not done / next
- `C_MEETING_TESS` at ~62 s: a climbing plant on the pillar left of the door
  renders flat yellow (GLM `cine_tess459_62s.png`). Unverified cause - next:
  the Tess story cut with `-logmaterials` and a shot at 62 s, find the plant's
  material and its texture slots.
- "A second A press during a slot spin didn't register" - not looked at (likely
  the spin input lock by design).
- `car_dealer C_BUY` black in CINVIEWER (NaN camera, needs its showroom context);
  Solar Crown ext carpet near-plane clip at ~3 s - not looked at.

## Session 2026-11-05 (casino self-test watchdog, `win`): `-pokerselftest` was killed by the watchdog, not crashing

GLM's sweep on build 459 reported "`-pokerselftest` crashes the process (exit
87) after ~950-1000 hands; 600 hands passes, 1100 dies at ~953, 5000 at ~1001,
log cut mid-line".

**Diagnosis** [CODE]: `CasinoPoker::selfTest` scores the exact optimal hold per
hand (32 masks, the full discard alone is C(47,5) = 1.53 M draws), all on the
frame thread from `GMCasino` init, with no `Watchdog::Beat`. At the 60 s kill
threshold the watchdog terminates with `0x43544157` ('WATC'), whose low byte is
`0x57` = **87**. The "rare hand" was just elapsed time; the cut log line is
`TerminateProcess` mid-write.

### What changed
- `source/game/casino/casino_poker.cpp` - `Watchdog::Beat( "pokerselftest", h )` per hand.
- `casino_slots.cpp` / `casino_roulette.cpp` - the same beat every 1024 spins
  (fast today, but the same frame-thread loop shape).

### Verified
    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm CASINO -skipcine -no3d -nopad -log C:\OpenTDU2-win\_poker1100.log -pokerselftest 1100
- exit 0 after 90 s (it died at ~60 s before): `evaluate: 13 cases, 0 failed`;
  `1100 hands ... EV mean 95.43% +- 3.14% (1 SE)` (99.54 % reference inside 1.5 SE);
  `balance 99999480, expected 99999480 (ok)`; watchdog `1104 beats, 0 stalls`.

### GLM's other findings - worked the same session (next entry up)

## Session 2026-11-05 (occlusion at 4K, `win`): 4K High drive ~16.5 -> ~11.8 ms (61 -> 85 fps); the depth readback moved to a GPU tile reduction

The owner: "Yes, check 4K".

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -profile high -width 3840 -height 2160 -novsync -tp 21796.3,24188.1 -snaproad 150 -carlod 4 -loggputimes -logocclusion -autoplay "60:hold=accel" -exitframe 1250

### Found and fixed
- **The full depth copy cost 3.5 ms of GPU at 4K** (`-loggputimes` prepass 0.7 -> 4.2 ms: 33 MB a frame to host memory
  over PCIe). New `resources/shaders/depthmax.frag` (-> `depthmax_frag.h` via `tools/compile_shaders.py`): one R32
  texel per tile = the tile's farthest depth, drawn into `OcclusionTiles` (~160 x 90) between the static world and
  the moving objects; only that (~60 KB) is read back, the worker just unpacks it. GPU pre-pass 0.63 ms (0.62 without
  the cull). Same culled counts as the CPU reduction.
- **Far-tree impostor cards were culled with their instance box** (367 faint pixels on a tree line at 4K frame
  1140): the cards are sized to the bounding sphere and stick out of the box. `drawInstances` now tests the cube
  around the sphere. A 0.5 m + 0.2 %-of-distance margin was added to every box (`Grid::isHidden`) as well.

### Verified (4K High, same build, `-noocclusion -nooceanskip` vs default)
- Frame avg (120-frame windows 240..1080): 15.2-18.7 ms -> **10.9-13.7 ms** (~61 -> ~85 fps). 72-82 % of the tested
  boxes culled, grid in use 117-120 / 120 frames (the worker keeps up).
- Pixels: frame 1140 identical; frame 600: **2 pixels differ by 9/255** (16 x 17 area at 1936,923) - real (two
  `-noocclusion` runs are identical to each other), cause not found: the sphere cube for model items did not change
  it (and cut the cull to 76 %), so items keep their box.

### Not done / next
- The 2-pixel residual at 4K frame 600.
- The ocean reflection after turning from town to the sea (2-3 stale frames) is still unmeasured.

## Session 2026-11-05 (occlusion cull + ocean skip, `win`): the hidden world is no longer submitted - town 15 -> 11 ms, drive 16 -> 13 ms; the ocean mirror pass is skipped when no water shows (-2.5 ms more)

The owner: "Could do both" (the occlusion cull and the ocean-reflection skip) / "Please continue with those".

### What changed
- `source/render/eden/eden_occlusion.*` (finishes the WIP entry below): a pool of pre-pass depth readbacks, a
  worker thread that reduces a finished one to a tile-max grid (~160 tiles across), `Grid::isHidden` (the
  `-occlusionprobe` test, projected with the GRID's view-projection; off its screen / at its near plane = drawn),
  `Grid::isPlaneVisible` (does the water plane show in any tile: the plane's depth at the tile corners vs the tile's
  farthest depth), camera-cut guard (12 m / 25 degrees) and a **world epoch** guard (`EdenWorld::getDrawEpoch`, bumped
  when the drawn sector set changes: a grid whose depth still had the coarse area terrain a new sector replaced is
  not used - that was the 42-pixel difference after the sector crossing).
- `EdenRenderer::setOcclusionGrid` / `isOccluded`: `isBoxVisible` (model items) and `EdenWorld::drawInstances`
  (.pmi instances) drop the hidden boxes while a grid is set.
- `EdenSceneView::renderOffscreen`: the grid is set around `world.draw` of the pre-pass and of the opaque pass only
  (blended draws are deferred from that traversal; shadows / reflections / env never culled). **The moving objects
  (`drawObjects`: the player's car, traffic, people) are drawn after the depth copy and never culled**: the car
  moves with the camera, so in a 2-3 frame old depth its body stood where the world now is and parts behind it
  were culled against it (2,600 differing pixels behind the car before this split). Not in the map view.
- Ocean: `ocean.renderReflection` is skipped while the grid shows no water (`-nooceanskip` = the A/B).
- Flags: `-noocclusion`, `-nooceanskip`, `-logocclusion` (culled counts every 120 frames, ocean skip on/off).

### Verified (1280x720, `-fixedtimestep`, same build, with vs without)
| Run | frame avg off -> on | pre-pass / opaque CPU |
|---|---|---|
| Ibiza town `-tp 12605.4,16698.1` | 13.0-15.1 -> 11.4 ms (cull), -> **9.2 ms** with the ocean skip | 2.8 / 2.9 -> 1.6-1.8 / 1.6 |
| benchmark drive `-tp 21796.3,24188.1` | 15.8 -> 13.1 ms (cull), 13.0 -> **10.5 ms** with the skip | 3.0 / 3.1 -> 1.7-2.0 / 1.7-1.9 |
- `-logocclusion`: 80-85 % of the tested boxes culled, grid in use 117-120 of 120 frames.
- **Pop-in**: pixel diffs vs `-noocclusion`: town frame 300, drive 300 / 600 / 900 / 1140 identical (bbox None outside
  the build stamp); a hard left-right turn in town (frames 220-420): identical except a 3x3 spot under 8/255 at 300.
- Ocean skip: identical frames in town / the drive / the wall spot `-tp 36096,5760`; at the beach camera
  (`-gm WORLD_VIEWER -campos 36070,12,5960 -camyaw 90 -campitch -15`, sea in view) the skip never triggers and the
  frame is identical.
- Validation (1250-frame drive, cull + skip, a sector crossing): no VUIDs; the exit leak is the same 4 objects as before.

### Not done / next
- When water comes into view after a skip, the first 2-3 frames use the last reflection rendered (unmeasured: no
  scripted shot turns from town to the sea).
- Not measured at 4K (the readback is 33 MB a frame there, reduced on the worker; check the worker keeps up with
  `-logocclusion` "grid on N/120").
- Shadows are the largest pass left (3.6-4.3 ms) and cannot use this cull; PERF_PLAN 0a has the shadow ideas.

## Session 2026-11-04 (occlusion cull, `win`, WIP - stopped at the usage limit): the EdenOcclusion module is written and compiles, not wired in yet

The owner: "Could do both" (the occlusion cull and the ocean-reflection skip).

### What changed
- `source/render/eden/eden_occlusion.{h,cpp}` (new, **unused so far**): pool of pre-pass depth readbacks, a worker
  thread that reduces a finished one to a tile-max grid (~160 tiles across), `Grid::isHidden` (the
  `-occlusionprobe` test, projected with the GRID's own view-projection; off the old screen / near plane = visible),
  camera-cut guard (12 m / 25 degrees). `-noocclusion`.
- `RenderDevice::invalidateBuffer` (readback memory may be non-coherent).

### Next (in order)
1. `EdenRenderer`: `const EdenOcclusion::Grid *pOcclusionGrid` + `isOccluded( min, max )`; in `isBoxVisible` (world AABB,
   as the probe computes it) and `EdenWorld::drawInstances` (after the frustum test) skip a hidden box.
2. `EdenSceneView::renderOffscreen`: `occlusion.beginFrame( device, camera pos, forward )` before the pre-pass; set the
   grid for the pre-pass and opaque `world.draw` + `drawObjects` only (never shadows / reflection / env); after the
   pre-pass `occlusion.afterPrepass( device, pSceneTarget, renderer VP, ... )`; `destroy` with the view.
3. Measure: `-logrecordtimes` prepass/opaque with and without `-noocclusion`; pop-in = pixel diff of a
   `-fixedtimestep` drive at several frames vs `-noocclusion`; validation run.
4. Ocean skip: skip `ocean.renderReflection` when every ocean item box (island / area / sector) is hidden in the grid.

## Session 2026-11-04 (occlusion probe, `win`): 76-80 % of the depth pre-pass draws are hidden behind something - occlusion culling is worth building

The owner asked whether culling "so you only see what the camera sees" would help, then: "Yes, measure it".

Already in the port: frustum culling (per instance, per model item), per-instance LOD distances, small-object culling,
batched far-tree impostors. Missing: **occlusion** culling (what is behind a building / a hill / a tree).

### What changed
- `-occlusionprobe <f1,f2,...>` (`source/render/eden/eden_scene_view.cpp` `OcclusionProbe` /
  `EvaluateOcclusionProbe`, `EdenRenderer::beginOcclusionProbe` / `ProbeDraw`): on those scene frames every draw of
  the depth pre-pass records its object's world box (`isBoxVisible` sets it for model items, `drawInstances` for
  the .pmi instances; terrain and batches have none and count as drawn); the pre-pass depth is read back
  (`copyRenderTargetDepth`) and evaluated when its frame slot comes round. A box is **hidden** when its nearest depth
  is behind the farthest depth of the 8x8-pixel tiles it covers - the conservative hierarchical-Z test; an object can
  never hide itself. It measures; nothing is culled. Log line `[occlusion]`, picture `_occlusion_f<frame>.ppm`
  (depth, hidden boxes outlined red).

### Verified (1280x720, `-fixedtimestep`)

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -novsync -tp 12605.4,16698.1 -snaproad 150 -carlod 4 -occlusionprobe 300 -logscenetimes -logrecordtimes -exitframe 420

| Spot | pre-pass draws | hidden |
|---|---|---|
| Ibiza town, tuning-shop street (`-tp 12605.4,16698.1`) frame 300 | 2,422 | **1,933 (79.8 %)** |
| benchmark spot (`-tp 21796.3,24188.1`) frame 300 | 2,776 | 2,106 (75.9 %) |
| benchmark drive frame 600 / 1000 | 3,526 / 2,604 | 77.3 % / 76.2 % |

- The pictures (`docs/images/occlusion_probe_town.webp`, `occlusion_probe_benchmark.png`: frame | depth + hidden
  boxes) line up with the frames: the hidden boxes are the town behind the street-front buildings, the band of
  sector objects / instances behind the hedges and trees on the horizon, and the car's own inner parts behind its body.
- Per-pass CPU at the town spot (`-logrecordtimes`): ocean 2.5 ms (2,013 draws), prepass 2.5 (2,332), shadows 3.6
  (3,113), opaque 2.6 (2,172).

### What it means
- The pre-pass and the opaque pass submit the same world traversal: **~5 ms of CPU a frame at 720p (5.4 at 4K High,
  PERF_PLAN 0a), of which ~3/4 draws nothing visible.** Upper bound of the gain ~3.5-4 ms of a ~15 ms frame (the
  probe is an oracle: it uses the same frame's depth; a real cull sees less). Shadows cannot use it (their casters
  are off-screen by nature).
- Separately: the **ocean reflection pass draws ~2,000 draws (2.5 ms) in the middle of town** - the whole world from
  the mirrored camera, with no sea on screen. Skipping it when no ocean tile is visible is a smaller, simpler win.

### Not done / next
- Build the cull, cheapest first: test each box against the pre-pass depth of a frame ago (the readback this probe
  already does, at a downsampled size) with **that frame's** view-projection, and keep a box drawn unless it was
  hidden in two consecutive tests (objects coming out from behind a building may still appear a frame late - measure
  it with a fast camera turn). The latency-free alternative is a CPU occluder rasterizer (big building boxes +
  terrain) - more work. A/B flag and pixel diffs as usual.
- Skip the ocean reflection when no ocean tile passed the frustum test.

## Session 2026-11-04 (load-ahead streaming, `win`): the sector-crossing hitch is gone - the next ring loads one sector a frame, banks prefetched to RAM, height queries on a grid

The owner: "I want you to do sector loading but I was wondering if something like culling would work ... Or if we
can try maybe utilising more ram as a benefit?"

**Measured first** (`-logsectortimes`, new: per-stage ms of each sector load + the streamWorld split): the crossing
at frame 1132 of the benchmark drive was **world 31 ms (3 sectors x ~10: bank 6, heightfield 2, objects 2-6,
instances 1) + collision 24 ms** in one frame - streaming only ran when the car entered a new sector, and the
collision streamer re-read the same 3 MB sector bank the world had just read.

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -novsync -tp 21796.3,24188.1 -snaproad 150 -carlod 4 -hitchms 33 -logsectortimes -autoplay "60:hold=accel" -exitframe 1250

### What changed
- `source/game/freeride/gm_freeride.cpp` `streamWorld` - **load-ahead**: while the car stays in its sector, one
  step per frame loads the ring one beyond the streaming square (`radius + 1`), the sector nearest the heading
  (`streamHeading`, 600 m ahead) first; world first, then collision. Starts 30 frames after a sector change (a
  spawn / teleport absorbs its own loads first) and goes idle once the ring is complete. `-nopreload` = the old
  synchronous streaming, `-nopreloadcollision` = world only.
- `source/world/eden_world.*` - `preloadOne` / `prefetchAround`; a preloaded sector is `bPreloaded`: **not drawn,
  and not counted as loaded by the area mesh** (`updateAreaIndices`, `updateOceanVisibility` via the new
  `isSectorShown`) - otherwise the coarse area terrain / ocean tile would hide under a sector that does not draw.
  `loadAround` reveals the preloaded sectors of its square (and marks their areas dirty) - exactly when the old
  code would have loaded them, so **the drawn world is identical**.
- `source/physics/vehicle_collision.*` - `preloadOne` (the same, for the collision streamer).
- `source/filesystem/gs_file.*` - **RAM file cache + background reader**: `prefetchFile` queues a path, a worker
  resolves and reads it; `loadFile` copies a cached file out (waits only for the file the worker is reading right
  now). Sector banks read synchronously are kept too (the collision reads each one right after the world).
  LRU, `-filecachemb <n>` (default 768, 0 = off). Prefetch = the banks two rings out, issued with the first
  preload step (issued at the sector change it competed with the spawn's own reads).
- `source/render/eden/eden_model.*` - **XZ grid over the collision triangles** (16 m cells, built at the first
  query): `getSurfaceHeight` / `getCeilingHeight` walked every triangle of every loaded sector per call, several
  calls a frame (`EdenWorld::isSheltered` for the ambience, `getSurfaceHeight`) - ~6 % of the main thread
  (`-sampleprof`), and the preloaded sectors doubled it.

### Verified
- Crossing frame 1132: **76-91 ms (update 52-65) -> under 33 ms, not a hitch** (`-nopreload` vs default, same
  build, two runs each). The worst preload step seen: ~14 ms (one collision sector), frame 1140 at 33 ms.
- Steady update **1.2 -> 0.4 ms** a frame (the grid; both modes). Steady frame times equal with / without preload.
- **Pixel-identical**: frames 600 / 1100 / 1180 `-nopreload` vs preload (bbox None, including after the
  crossing); frame 1300 vs the pre-grid build: only the build stamp differs (physics / camera unchanged).
- Validation (`-vk_enable_validation_layer`, 1250 frames with preload, reveal and 4 unloads): no VUIDs; only the
  pre-existing `vkDestroyDevice` 4 leaked VkBuffers at exit.
- RAM (peak, 1300-frame drive): working set 1,083 -> 1,277 MB, private 2,800 -> 2,995 MB (+~200 MB: the extra
  loaded ring + ~115 MB of banks in the file cache).
- Start-up frames 1-3 are dominated by run-to-run noise (frame 1 160-290 ms either way); frame 1 update is
  lower with the cache (51-57 vs 71-84 ms).

### Ruled out
- `-gpsdrive` as a multi-sector benchmark: it moves the GPS marker, not the car (the car sits in neutral).
- The collision preload as the source of the +0.8 ms update it first showed: `-nopreloadcollision` gave the same
  number; the profile named `isSheltered` / `getSurfaceHeight` (fixed by the grid).

### Culling (the owner's question)
- Already in: per-instance frustum culling + per-instance LOD distances + small-object culling
  (`EdenWorld::drawInstances`), per-item frustum boxes for the sector / area models (`EdenModel::draw`,
  `isBoxVisible`), the impostor batching. Not in: **occlusion culling** (hiding what is behind a hill or a
  building). The GPU is ~4 ms of a 15 ms frame, so occlusion culling would only pay if it cuts CPU draw
  submissions - worth measuring in town (Ibiza city, the casino strip), not on the open roads; a software
  depth pre-test of building boxes is the cheap form. Not started.

### Not done / next
- The benchmark drive crosses one border (the car rolls near frame 1200): a longer straight drive with several
  crossings is still unmeasured. The heading-first order matters most at speed.
- A preload step is still main-thread work (~4-14 ms); moving the bank parse + 3DG/PMI decode to a worker is the
  remaining step if a 25-30 ms frame during a drive is felt.
- Collision never unloads (`VehicleWorldCollision::loadedSectors` only clears on an island change): RAM grows over
  a long drive - pre-existing, preload makes it grow one ring earlier.

## Session 2026-11-04 (hitches + SDL, `win`): pipeline cache on disk, sign faces off the main thread, uploads batched; SDL 3.4.20 + DualSense adaptive triggers

The owner: "the game is barely using the cpu or ram ... is there anything else we can do to help improve stuff
and ... can we benefit from upgrading to latest SDL for additional features for controllers" - then "do both,
fixing hitches 1st".

**Measured** (every run: `-gm FREERIDE -skipcine -nopad -fixedtimestep -novsync -tp 21796.3,24188.1 -snaproad 150
-carlod 4 -hitchms 33 -autoplay "60:hold=accel" -exitframe 2400`, 1280x720; `[hitch]` lines now carry the frame's
uploads / submits / host waits):

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -novsync -tp 21796.3,24188.1 -snaproad 150 -carlod 4 -hitchms 33 -autoplay "60:hold=accel" -exitframe 2400

### What changed
- `source/render/vulkan/render_device.cpp` - **the VkPipelineCache persists** in `<userdata>/pipeline_cache.bin`
  (9.3 MB; header checked against vendor / device / pipelineCacheUUID, written to `.tmp` and renamed at
  `destroy`; `-nopipelinecache` = start empty). `-sampleprof` on a fresh exe: a first-sight sector frame was
  295 ms with `nvGetCompilerInterface` 31 % and `EdenRenderer::getPipeline` 20 % of it.
- `source/world/sign_text.*` - **road-sign faces compose on a worker** (`SignFaceCache::workerLoop`): the 4 MB
  atlas copy + text + DXT1 of the 1024 chain was ~20 ms of the main thread per new sign. `getFace` queues a
  missing face and returns null (the plain plate draws, exactly as before a face exists); `collectFinished`
  uploads the results. `compose` = `composeEncoded` (CPU, const) + `upload` (main). `-syncsignfaces` = the A/B.
- `source/render/vulkan/render_device.cpp` - **texture copies join one open upload batch** (`getUploadBatch`),
  submitted with a fence and no wait before the frame's submit, before any immediate submit, or past 64 MB;
  the staging is freed when the fence signals (`retireUploads`). Same queue: submission order + the batch's
  own barriers make the textures ready for every later submit. `-syncuploads` = the old wait per texture.
- `source/tdu_instance.cpp` - `[hitch]` lines add `uploads N tex X MB, S submits, W waits T ms`.
- `cmake/Dependencies.cmake` - **SDL 3.4.12 -> 3.4.20** (bug-fix releases + controller DB). The shared cache
  `C:\otdu2-deps\SDL` was checked out at `release-3.4.20` too, so **the glm / cline trees build 3.4.20 on their
  next build** (patch level, API-compatible).
- `source/input/gamepad_output.*`, `gm_freeride.cpp` - **DualSense adaptive triggers** through
  `SDL_SendGamepadEffect` with the raw DS5 effects report (SDL has no dedicated API; the format is SDL's
  `testcontroller.c` one). While driving (engine on): **L2 brake** firm resistance (`01 24 150`), a buzz while
  any wheel's `bAbs` is set (`06 28 63 24`); **R2 throttle** light resistance (`01 0 45`), a buzz on wheelspin
  (a driven wheel `bTcs` or `SlipRatio > 0.25`: `06 38 40 16`). Free triggers otherwise and on exit / when
  the DualSense option is turned off. Only for `SDL_GAMEPAD_TYPE_PS5`; `-notriggers` = off, `-logtriggers`
  logs each change. The values are a chosen feel `[INF]` (the PC original has no DualSense support).

### Verified
- Upload batching: frame 1 had **2,149 fence waits = 320 ms** (`-syncuploads`) -> **0 waits, 3 submits**.
  The frame total did not move (508 vs 511 ms): the waits were not where the time went on this machine.
- Pipeline cache, NVIDIA's own disk cache off (`__GL_SHADER_DISK_CACHE=0`), `-nopipelinecache` vs loaded:
  frame 1 **552 -> 156 ms**, frame 3 84 -> 38, frame 12 91 -> under 33, the sector-load frame (1132)
  91 -> 78 (its draw 39 -> 25).
- Sign faces: the 33-43 ms frames at 900 / 912 / 924 / 1032 / 1104 (1-2 sign textures each) are gone; frame
  12 190 -> 77 ms. Steady drive 12-16 ms.
- **Pixel-identical**: FREERIDE frame 1300 `-syncuploads` vs batched, `-syncsignfaces` vs async (0 differing
  pixels each); garage `FIRST_WM` frame 400 `-syncuploads` vs batched (bbox None). SDL 3.4.20 build vs the
  3.4.12 one: only the build stamp differs.
- Validation (`-vk_enable_validation_layer`, 1200 frames): no VUIDs from the change. One pre-existing message:
  `vkDestroyDevice` - 2-4 VkBuffers leaked at exit; it is there with `-syncuploads -syncsignfaces` too.

### Ruled out
- "Every texture upload waits for the GPU" (PERF_PLAN P1, a `[CODE]` reading) as the cause of the running
  game's hitches: measured, the sector load at frame 1132 waited 3 ms for its 25 textures.

### Not done / next
- **The triggers are not felt yet** - no pad on the headless runs (`-nopad`). Owner check: plug the DualSense
  in, drive, brake hard (L2 should buzz when ABS cuts in), spin the wheels (R2 buzzes). `-logtriggers` prints
  every change to the log. If SDL refuses the effect (Bluetooth without enhanced mode) the log says
  `adaptive triggers refused` and the pad keeps rumble + light bar.
- The trigger state is only updated from FREERIDE's tick: in the pause menu the triggers keep the last
  driving effect until play resumes or the game mode leaves.
- **The remaining sector-load hitch (~52 ms of update at frame 1132)** is `EdenWorld::loadSector` +
  `VehicleWorldCollision::loadSector` on the main thread (bank read + 3DG / PMI decode + collision build).
  Next: PERF_PLAN P1 second bullet - read and decode on a worker, create the GPU objects in per-frame slices.
- The pipeline cache is written at exit only; a crash loses that session's new pipelines (the driver's own
  cache still has them on NVIDIA).
- SDL still has no controller audio (the DualSense speaker) in 3.4.20.

## Session 2026-11-04 (intro atmosphere, `win`): no haze or distance fog in the intro cinematic

The owner: "I think that scene originally shouldn't have any fog or haze I noticed it made the scene look weird".

### What changed
- `eden_scene_view.*` - per-scene switches `setHazeEnabled` (the full-screen Skydome_haze pass) and `setFogEnabled`
  (fog_max -> 0, the map view's rule); `-loghaze` logs the drawn weather's haze / fog inputs when it changes.
- `intro_cinematic.cpp` - the intro view turns both off; `-introhaze` / `-introfog` restore each.

### Verified
- `-loghaze`: every intro shot uses the `Intro` weather - Skydome_haze 0.700, fog 0..1200 m, max 0.500 [DATA]; so the
  haze is in the data, the owner's observation is that the original does not show it (postfx_spec 5 item 5 notes it).
- Intro frames 1200 (bay) and 2000 (party) with and without: `docs/images/intro_no_haze_bay.webp`,
  `intro_no_haze_party.png` (top = old, bottom = new) - the grey wash and the ghostly far trees are gone; the distance
  is darker and more saturated without the haze's added light.

### Not done / next
- The switch covers the whole intro cinematic view (the flyover, the party, the house walk that follows in the same
  view). Free ride keeps its haze. If the original turns out to show haze in some intro shots, make it per shot.

## Session 2026-11-04 (impostors, `win`): the far trees draw - batched, lit by impostor_ambient, camera-facing, atlas view by angle

The owner: "Yes, light the trees".

### What changed
- `eden_world.*` - the TD_Impostor cards draw by default (`-noimpostors` = off): `addImpostor` yaws each card to the
  camera (its -X front face; FUN_00996820 turns impostors by the camera angle [CODE]), picks the atlas side view of the
  camera angle in the instance frame (N-1 side views + a top view [DATA: the 2DB strips]) and transforms it to world
  space into the per-material batch; `drawImpostors` draws each batch with `__AmbLight_Ambient_0` = the weather's
  `impostor_ambient`, `_1` = 0. `-logimpostors`: card positions / UVs / plane, card vs LOD0 size.
- `eden_scene_view.cpp` - `impostor_ambient` read from the weather each frame (FUN_00d769e0 -> renderer +0x45c0, the
  light object at +0x45bc, vtable 0xf2c310, constructed at 0xd82170 [CODE]); the scene ambient without a weather.

### Verified
- Intro frame 1200: far trees on the hills, green, full width; `docs/images/impostors_far_trees_intro.webp` (before /
  after) + `impostors_far_trees_zoom.png`. FREERIDE 13:00: one far tree at the street end, nothing else changes.
- 4K High benchmark (quiet machine): 15.3 / 16.0 ms on, 14.8 / 15.6 ms with `-noimpostors` (+0.4-0.5 ms). `-noimpostors`
  is pixel-identical to the previous commit. Validation: FREERIDE + intro, only the existing shutdown VUID.

### Ruled out (each with its A/B)
- The grey translucent shapes behind some far trees: not a second card (all 19 items are vertical cards), not the
  shadow casters (`-noshadows` identical), not the size (crown top within 1 m of LOD0) - they are the distance haze
  (`-nohaze` removes them) on trees that really are far. The top-down atlas cell on a vertical card was a real bug
  (grey slanted bars): fixed, only the side views are used.

### Not done / next
- The 0.33 -> 1 dissolve against LOD0 (ps_1 / ps_4 `gDissolveTexSampler`, pmi_streaming_spec 4) - the switch is a
  hard cut today. The atlas cell order is [INF]: compare the intro bay against a retail screenshot.
- Measuring note: absolute frame times drifted 15.5 -> 17.4 ms while another process held the CPU at 66 %; quote A/Bs
  run back to back, and check `(Get-CimInstance Win32_Processor).LoadPercentage` first.

## Session 2026-11-04 (perf 3, `win`): the far-tree impostors were 3,900 invisible draws a frame - skipped, 4K High 19.4 -> 15.5 ms (64 fps)

The owner: "Instance the .pmi draws".

### What changed
- `eden_renderer.cpp` - `-logdrawfamilies`: draws per shader family + distinct mesh/material pairs every 120 frames.
  25.5 k draws / 6,451 pairs; TD_Impostor 3,875 draws from 58 pairs - the most repeated family.
- Found: the TD_Impostor vertex shaders have no world matrix (`gl_Position = ViewProj * in_Position0`, read with
  `otdu2_sob_tool <sob> <dir>` + `C:\VulkanSDK\1.4.350.0\Bin\spirv-cross.exe`). Ordinary families (TD_Building,
  TD_Ground...) read their world matrix from per-draw constants, so they cannot be GPU-instanced without new shaders.
- `eden_world.*`, `eden_model.*`, `render_device.h` - impostor items are skipped by default; `-impostors` batches them
  in world space (CPU-transformed quads, one transient vertex buffer + one draw per material and pass, mirrored
  instances re-wound). `RenderDevice::getTransientBuffer()`.

### Verified
- 4K High benchmark (same command as perf 2): **15.5 ms (64 fps)**, was 19.4; ocean 2.6 / prepass 2.7 / shadows 3.8 /
  opaque 2.7 ms. Frame identical to the previous build outside the stamp; intro frames 1200 and 2000 identical to the
  old per-item path. Validation clean with and without `-impostors`.
- `-hidematerial _H-I` (old code): 0 pixels differ -> the impostors never drew anything visible.

### Not done / next
- Make the impostors look right, then default them on (PROGRESS row "Far vegetation impostors"): feed
  `__AmbLight_Ambient_0/1` from `impostor_ambient` (night_lighting_spec 5), check FUN_00996820 for camera-facing quads
  and the dissolve; `docs/images/impostors_far_trees_intro.webp` is the current `-impostors` look (intro frame 1200).
- Real instancing of the other families needs per-instance world matrices in the shaders - a SPIR-V patch of the
  DXSO translation (read the World rows from an instance-rate stream) is the route; not started.

## Session 2026-11-04 (perf 2, `win`): 4K High 31 -> 19.4 ms at the benchmark spawn, 59-79 fps on a real drive — profiler, heap churn, parallel command recording

The owner: "Yes, start it" (the parallel pass recording of the 4K-60 plan).

### What changed
- `source/core/sample_profiler.*`, `tdu_instance.cpp` — **`-sampleprof [<first>,<last>]`**: an in-process stack sampler of
  the main thread (suspend + x64 unwind + DbgHelp), report `<log>-profile.txt`. No admin needed. Symbols need the
  `build-prof` dir (`docs/PERF_PLAN.md` §0a has the configure line; 45 s full build).
- `eden_scene_view.cpp`, `eden_renderer.*` — **`-logrecordtimes`**: `[perf] record/pass:` (ms in recordDraw / pass ms, draws).
- `eden_renderer.*`, `render_device.*` — the redundant-bind filter (`-nobindcache`), driven by a command-buffer
  touch count in `RenderDevice::getCommandBuffer()`.
- `eden_world.cpp` — `drawInstances` built two `EdenDrawParams` (five `unordered_map`s each) per visible instance per
  pass: **22.5 % of the main thread**. Now `std::optional`, built only for neon / merged instances.
- `eden_renderer.cpp` — `FindSmall` for the per-draw override maps; `getPipeline` one-entry memo.
- `freeride_hud.*` — the minimap's road query cached with a 150 m margin (re-queried every 75 m).
- `eden_renderer.*`, `render_device.*`, `renderer_2d.cpp` — **parallel command recording** (`-noparallelrecord`,
  `-recordthreads <n>`, default 6 workers): queued `DrawPacket`s, the device's `PendingRecordHook` flush before any
  other recording, pass restart with LOAD + secondary command buffers, per-slice command pools; the device tracks
  viewport / scissor (`setViewport` / `setScissor`). How it works: `docs/PERF_PLAN.md` §0a.

### Verified
- Same 4K command each step: 31.3 -> 30.1 (bind filter) -> 23.7 (EdenDrawParams) -> 21.9 (minimap query, memo) ->
  **19.4 ms (52 fps)**; GPU unchanged ~4.3 ms. Real-time 2400-frame 4K drive: 59-79 fps, VRAM flat, clean exit.
- Every step pixel-identical: frame-400 screenshots identical outside the build stamp across builds; parallel vs
  `-noparallelrecord` 0 differing pixels in FREERIDE 4K, the intro (frame 400) and the garage (frame 300).
- Validation: FREERIDE, intro and garage runs report only the existing shutdown `VUID-vkDestroyDevice-device-05137`.

### Ruled out
- Deferring constants / descriptor sets to the workers: 103 runtime material-mutation sites in 17 files (car
  paint, avatar faces, traffic colours, sign faces) - they must be resolved at capture on the main thread.
- ETW / xperf: the agent shell is not elevated.

### Not done / next
- The heavy spawn is 19.4 ms in `-fixedtimestep` (15.9-16.9 real time). What is left on the main thread: constant
  fill + descriptor resolution (~0.4 us/draw) and the traversal. Re-run `-sampleprof` on `build-prof` first; then
  `.pmi` instancing (`EdenWorld::drawInstances`) cuts every pass's draw count.
- The intro records inline (its batches are under the 192-packet threshold) - fine at 8 ms.

## Session 2026-11-04 (perf, `win`): the satnav minimap's lost 10 ms is back out — 4K High 39 -> 31 ms; the 4K-60 plan

The owner: "I want a base as 4k 60fps max settings on this pc with potential for higher fps ... the original game
with dxvk 4k max with 8x AA this pc gets over 100 - 140fps".

**Reproduced**: `-loghudtimes` at 4K and at 720p both read `[perf] hud: flash 9.5-10.0 ms`:
    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -profile high -width 3840 -height 2160 -novsync -tp 21796.3,24188.1 -snaproad 150 -carlod 4 -loghudtimes -logscenetimes -loggputimes -autoplay "60:hold=accel" -exitframe 620

### What changed
- `source/game/freeride/freeride_hud.{h,cpp}` — `drawMinimap` gets cline's session-73 fix back on the satnav map
  rect: a GPU scissor (`Renderer2D::setClipRect`) instead of per-triangle CPU clipping, and the road + route
  vertex list cached in `MinimapCache`, rebuilt at most every 3rd frame when the car moved > 0.75 m, turned, the
  route or the map rect changed. The d92c85e merge had kept glm's satnav and dropped it. `-nominimapcache` = A/B.
  The unused `ClipToRect` is removed.

### Verified
- HUD 10.0 -> 1.8 ms, frame 39.0 -> 31.3 ms (26 -> 32 fps) at 3840x2160 High; GPU unchanged at ~4.1 ms.
- Frame-400 screenshot vs `-nominimapcache`: mean 0.008/255 whole frame, 0.10 in the map box (<= 2 frame map lag);
  roads trimmed cleanly at the satnav edge (`docs/images/minimap_scissor_cache_4k.webp`).
- `render_scale = 200` at 4K: GPU 8.7 ms, frame unchanged (31.5-32.1 ms) — supersampling is free on this PC today.

### Ruled out
- Resolution as the HUD cost: 720p measured the same 9.5 ms -> CPU, not fill.

### Not done / next
- 4K High is still 31 ms: 27.5 ms is the CPU recording of ocean / prepass / shadows / opaque. The ordered plan is
  `docs/PERF_PLAN.md` §0a — start with parallel pass recording (`EdenSceneView::renderOffscreen`, make
  `EdenRenderer::recordDraw`'s transient allocator + `drawSetCache` per thread), then `.pmi` instancing in
  `EdenWorld::drawInstances`.
- Merge watch: a future glm/cline merge touching `drawMinimap` must keep `MinimapCache` + the scissor; check
  `-loghudtimes` stays under ~2 ms after merging.

## Session 2026-11-04 (AI car LOD, `win`): the opponents and rivals draw the `_med` banks, only the 3 nearest cast shadows

The owner picked "Yes, add the LOD" after the AI opponents session.

### What changed
- `cinematic_car.{h,cpp}` - `Desc::Lod` ("med": `vehicules/med/<car>_med.bnk`, model `<car>_med`), `Desc::bInterior`, `getLod()`.
- `ai_drivers.{h,cpp}` - `AiCar` loads `_med` (`-ailod high`), skips the reflection pass (`-aireflect`) and, in the
  shadow casters, all but the 3 nearest AI cars within 60 m (`-aishadows <n>`, `-aishadow <m>`);
  `AiCar::SetCameraPosition` (`gm_freeride.cpp`, before the traffic draw).

### Verified
- opponents_spec 2 / 4: two runs x 8 samples, 22.6 ms (high, all shadows) -> 21.5 ms (default) against 19.0 ms
  with no opponent draw; high / med look the same (`docs/images/race_opponents_med_lod.webp`); 0 VUIDs; Passed 1st of 8.

### Ruled out
- `<car>_low.bnk` as a far model: it is the engine sound bank (TCT_*.wav, CarVSTConfig.xmb).
- A cockpit-less far level: CinematicCar already draws the interior only within 5 m (table 0x00f7e788).
- `_med` alone and the reflection pass: within the run-to-run noise (~0.3 ms).

### Not done / next
- The remaining 2.5 ms is the cars' main + prepass draws; rims / tires are separate models (8 draws per car per pass).

---

## Session 2026-11-04 (AI opponents, `win`): the championship races have their 7 opponents and a race position; the story drivers cruise the islands

The owner: *"could we add race positions and opponents with AI logic for the opponents also around the world when
offline there's normally a chance to see tess and todd etc... driving around the world ... their respected vehicles
like the pink mustang"*.

Controls: none new (races start as before; the position shows on its own).

### What changed
- `database/gs_database.cpp` - `-dumpdbtables` / `-dumpdbtable <T>` / `-dumpdbcols a,b` (any db_data.cpr table).
- `game/freeride/ai_drivers.{h,cpp}` (new) - DBPNJ / DBPNJCar / DBBot / the signature cars, the class pick, `AiCar`
  (a CinematicCar with rolling / steering wheels, brake and headlamps).
- `game/freeride/race_opponents.{h,cpp}` (new) - the course (gates routed over the road graph), the per-driver speed
  profile, lanes / passing / following, contact, catch-up, ranking; `-logopponents`, `-opponentfollow <n>`,
  `-noopponentdraw`.
- `game/licenses.{h,cpp}` - `_bot_n`, `_difficulty_bot_n`, `_aggressiveness_bot_n`, `_rival`.
- `game/freeride/gm_freeride.cpp` - `startRaceOpponents` / `tickRaceOpponents`, the result ("Beaten  7th of 8"), the
  60 s rule, draw / collision / traffic yield; the rival hint; `-noopponents`, `-opponents <n>`,
  `-opponentdifficulty <f>`, `-licensewarpdelay <s>`.
- `license_test.{h,cpp}` - `eResult::Beaten`, `end()`, `setTimeLimit()` (CtDw off with opponents).
- `freeride_hud.{h,cpp}` - `setRacePosition` (`/HUD/position`, `/HUD/time` to `down`).
- `traffic.{h,cpp}` - rival types (an `AiCar`, weight 0), `loadRivals` / `tickRivals`, `findNearestRival`;
  `-norivals`, `-rivals <n>`, `-rival <name>`, `-rivalsoon`, `-rivalsmax`, `-rivalpark`.
- `chase_mode.cpp` - the CinematicCar cop fallback was drawn mirrored (fitOnGround takes an Eden quaternion).

### Verified
- docs/re/opponents_spec.md 4: CHA5A04R grid + GO (`docs/images/race_opponents_field.webp`), the position HUD
  (`race_position_hud.webp`), win 1st of 8, Beaten 7th / 8th of 8, all 7 finishing 321-327 s, CHA7A02R / CHB4A01R
  class picks, 0 VUIDs; the rival Mustang (`freeride_rival_miami_mustang.webp`).

### Ruled out
- The race gates' road words (`0, 236, 2, 1`) as a port road-graph link index: no match on CHA5A04R.
- CtDw as the race clock with opponents: 240 s for 10.8 km gate to gate; the AI needs 321 s.
- A pink paint for car 601: no DBCarsAndColors / DBPNJCar / DBRewardCar colour; it draws the dark green .2dm paint.
- Todd driving: Todd Bishop has no DBPNJCar / DBBot row.

### Not done / next
- `vehicules/med/<car>_med.bnk` for the opponents (CinematicCar::load reads `high/` only; 3.5-4.5 ms for 7 cars).
- Duels (DBEvent `_pnj1`, DBRewardCar DUEL_WIN_*), RaceEvents with opponents, minimap opponents, the standings
  screen, `player_before` / `player_after`. DBBot `_zoneindex` is undecoded.
- The "333999 m" chase meter shows when `-licensewarp` / `-opponentfollow` teleport into traffic (pre-existing).

---

## Session 2026-11-04 (minimap gate icons, `win`): the race gates show on the satnav with the movie's own icons

The owner picked "Minimap gate icons" after the race gates session.

### What changed
- `freeride_hud.{h,cpp}` - `setMinimapObjectives` drives `/HUD/minimap/objectif_01..08` (start / checkpoint /
  finish / radar_on frames) at world positions on the heading-up map, parked on the edge when off it;
  `canvasToMinimap` (the cursor placement now uses it too).
- `gm_freeride.cpp` - fed from `raceGates.getGates()` after the HUD tick, beside the world markers.

### Verified
- `-island hawai -licensetest CHA5A04R -licensepark` (countdown: green start + two orange checkpoints, one parked on
  the top edge; running: the next checkpoint) and `-licensepark last` (red finish):
  `docs/images/race_minimap_gate_icons.webp`. No icon outside a race (count 0 hides them).

### Not done / next
- `/HUD/position` needs opponents; the icons follow `-noracegates` (they read the gates drawn).

---

## Session 2026-11-04 (race gates + lamps, `win`): the races show their gates and the retail race HUD; the car lamps glow and the hot discs have a gradient

The owner: *"look at the checkpoint/finish line gates etc... for both the races and the licenses challenges we have
no ui for where a lap starts or ends ... none of the gates are used"*, and *"improve lights ... indicators, brake
lights, head lights don't seem to look accurate or even good also brake heat (disk brake glow) could also do with a
little extra work"*.

Controls: none new (indicators stay on the in-car menu, D-pad left; `-indicator left|right|hazard` for tests).

### What changed
- `game/freeride/race_gates.{h,cpp}` (new) - the island's `scenarics/packs/pack_cp.bnk` (Entity__Actor__CheckPoint
  types 1 start / 2 finish / 3 checkpoint / 4 radar / 6 barrel pair) loaded and drawn on the gates.
- `gm_freeride.cpp` `updateRaceGates` - which model on which gate (licenses_spec 8.2), ground snap, the start line 8 m
  ahead during the countdown; hooks `-noracegates`, `-logracegates`, `-licensepark [<step>|last]`, `-racestart <n>`.
- `license_test.{h,cpp}` - laps (`getLap`, `getGatesPerLap`), per-gate events with the split against the previous
  lap, the pass zone following the drawn arch (`getPassHalfWidth`); `ig2_challenge` keeps `bRoad` (0x7c records).
- `freeride_hud.{h,cpp}` - `setRaceHud` / `showCheckpointPassed` / `showLastLap` drive HUD.vmf's `/HUD/time`,
  `/HUD/laps`, `/HUD/checkpoints` (+ `cur_time`, `chck_box`, `last_lap`, loc 7429); the zone badge yields during a race.
- `cinematic_car.{h,cpp}` - `collectLampAnchors` / `appendLampGlows`, per-function lens weights, the blinker's bulb
  rise / fall, the headlamp giving way to its blinker. `eden_headlights` `EdenLampGlowFrame`; `eden_surface_fx` the
  `lampglow.frag` additive batch, the disc spill in the wheel plane; `brakedisc.frag` rewritten (surface_effects_spec 7.x).

### Verified
- Gates: `-licensetest RSA1T01 -licensepark` (barrel pairs), `-island hawai -licensetest CHA5A04R -licensepark` /
  `-licensepark last` (CHECKPOINT and FINISH arches), `-racestart 1` (START arch); `-licensewarp` through CHA5A04R:
  18 gates over 3 laps, splits +0.25 then ~0, "Last lap!" at gate 12, the finish model on the last step, Passed.
  `docs/images/race_gates_hud.webp`.
- Lamps / discs: night rear (blinker lit / off), front, `-brakeheattest 100` / `45`, midday -
  `docs/images/car_lamps_glow_brake_disc.webp` (first row = before). `-vk_enable_validation_layer`: 0 VUIDs.
  `-storystep 3`: exit 0, 0 stalls.

### Ruled out
- RSA1T03 / CHA1A04R gates "missing": the courses are on the other island (terrain -1 under them on Ibiza) - not the
  gate code. The first barrel shot showed nothing: the files' gate heights sit ~1 m under the road (snap added).

### Not done / next
- The minimap `objectif_0N` gate icons; `/HUD/position` (no opponents); off-road gate variants; `-licensetest` should
  switch to the course's island. The lamp glows are checked on the California only - the next car to look at is any
  other bank (the anchor rules are generic); traffic keeps its FA_ sprites; no headlamp beam in fog.

---

## Session 2026-11-04 (second chance, `win`): the Poker and Beach machines are two-step - spin, hold / lock, spin again

The owner: *"poker slot logic is different ... 1 spin and then you get to pick which reels to hold and get to spin
again, the 2nd spin is the outcome ... There's another slot also 3 reels, sun, surfboards ... same"*.

Controls (DualSense / keyboard) at the second chance: **D-pad left / right** = move the hand, **Square / X key** = lock /
unlock (the retail bar says "Lock/Unlock" on X; Triangle / Y works too), **Cross / Enter** = second turn, **Circle /
Backspace** = keep the hand as it is.

### What changed
- `gm_casino.{h,cpp}` - `eRelaunch` (Holding / Second), the poker deal / draw / Jacks or Better pay on the card reels,
  `autoHold`, `settleSecondChance`, the hold row; the poker deck seeded by `-casinoseed`.
- `casino_slots.{h,cpp}` - `respin( held, bPay )`, `setPayNextSpin`.
- `casino_slot_hud.{h,cpp}` - `enterRelaunch` / `leaveRelaunch` (the `relaunch_step` frame, the step-2 help bar), X =
  X_Lock at that step, `smof` reported.

### Verified
- See casino_spec 9.9: the Poker hold kept the 4s through the draw; auto-hold 01100 + Jacks or better on seeds 8 / 9;
  Beach lock kept reel 2; seeds 22 / 61 auto-lock all three reels and pay on the respin (`docs/images/casino_slot_second_chance.webp`).
- High Life flow unchanged (9 lines, the 600 win, quit); 0 VUIDs; `-storystep 3` exit 0.

### Not done / next
- The hand cursor over the machine itself (a native row stands in); the relaunch button neon states; the poker
  machine's measured return.

---

## Session 2026-11-04 (slot neons, `win`): the Fashion middle reel, per-reel stops, the decoded neon blink, the machines' light states and win flashes

The owner: *"middle reel on the clothes slot ... seems to miss some of its textures ... when spinning the middle reel is
only one that seems a little off"*, and yes to the neon blink / win lights.

### What changed
- `gm_casino.cpp` `updateReels` - horizontal drums spin along row 0, vertical drums only on full columns (the sheets'
  empty cells); per-reel stop times (`CasinoSlots::setReelStops`); the machine neons' states and the combination flash.
- `intro_cinematic.{h,cpp}` - the decoded neon light (`updateNeons`, `NeonCurve::evaluate`, the ExportType_Curve
  curves), `setNeonState`, `flashNeon`; `-logcasinoneons`.
- `casino_interior.{h,cpp}` - each machine's Neons[] / ComboActors[] / reel stop delays; `setMachineNeons`,
  `flashCombo`; `-logcasinoreels`; `-dumpcasinoentities 0x<hash>` / `root`.

### Verified
- Fashion spin: the chest drum shows a blurred jacket, no grey stripes (`docs/images/casino_fashion_chest_spin.webp`).
- Neon blink at a fixed eye (`-walkat 35.55,10.00,12.78,180`): frame diffs 5.7 / 4.7 / 5.7 / 7.3 / 7.0 / 4.9.
- States (`-logcasinoneons`): High Life `Title_Neon` / `SMHighLife_gain` -> Active on the machine, kept on the 600 win
  (NeonWin CanBeActivate 0), -> Inactive on leaving; Poker the same.
- Flashes over 6 seeds x 3 spins: Beach 4, Classic 4, Poker 1, TDU 2 1 found; High Life 4 missing (meshless panels).
- 0 VUIDs; `-storystep 3`, hall exit 0.

### Not done / next
- The owner's next request: the Poker and Beach machines are two-step (spin, hold / lock reels, re-spin; the poker
  hand pays). The movie has `relaunch_step`, X_Lock and the hand cursor; the buttons have the Relaunch states.
- The High Life pay panels (part of the sign model), the `Pok_Neon` card lights, the drum bone rotation.

---

## Session 2026-11-04 (slot machines, `win`): the reels spin down, the handle pulls, the screen is on the machine, the tops are lit

The owner: *"the reel spins weren't spinning down but more like across"* (all but the clothes machine), *"the
credit screen on slots is normally connected to the slot itself ... also a handle pull and some button press
animations"*, then a reference photo: *"missing textures at top of slots that should hide the reels"*.

### What changed
- `gm_casino.cpp` `updateReels` - SpinnerType 0 / 2 scroll down a sheet column, 1 (Fashion) across;
  `-reelspeedscale`. The handle clip plays from the spin (`handleTime`).
- `intro_cinematic.{h,cpp}` - SlotMachineTrigger, CasinoFlashScreen (+ Ranking / Jackpot), Entity__Actor__Neon and
  NeonSlotMachine load as props (the neons in the slots room only, lit from their IlluminationFactor);
  `setPropClip` (a game-driven clip; "BAS!" layers resolve to their .anm), `setPropTexture`, `setPropEmissive`.
- `casino_interior.{h,cpp}` - each machine's Trigger / its clip / SlotScreen / SlotLinesScreen; `setHandleTime`,
  `setScreenTextures`; `-dumpcasinoentities <substring>` dumps any entity array's fields.
- `casino_slot_hud.{h,cpp}` - Cas_SlotScreen (played + idle) and Cas_SlotLines drawn into render targets
  (`drawOffscreen`); `-slotscreenpanel`, `-slotscreenclear`.
- `renderer_2d.{h,cpp}` - `setTargetSize` (draw into a render target). `render_device.cpp` - the main pass has the
  render targets' two subpass dependencies (VUID-vkCmdDraw-renderPass-02684 otherwise).

### Verified
- Spin direction: `-reelspeedscale 0.04`, frames 20 apart: Classic / High Life symbols move down
  (`docs/images/casino_reels_spin_down.webp`); Fashion still across.
- Handle: TDU 2 machine, lever crop diff vs frame 309: 0.12 / 0.12 / 4.94 / 4.44 / 2.71 at +11 / +26 / +41 / +56 / +71
  frames - the pull and the return (`casino_slot_handle_pull.webp`), clip `cas_sm_spin-tri.anm` 1.21 s.
- Screens: 00 / bet / 1000 at rest, 990 (High Life) and 900 (TDU 2) after a spin, on the machine
  (`casino_slot_screen_on_machine.webp`); `-noprops` A/B: the black panel was the screen prop.
- Tops: walk view at the TDU 2 machine - the pay art (wheels, 1500 / 500) covers the drums; the room's machines have
  their lit tops (`casino_slot_tops_neons.webp`); all six in play (`casino_slot_machines_parts.png`).
- `-vk_enable_validation_layer`: casino spin / back / quit and FREERIDE map + pause, 0 VUIDs (11 before the pass fix).
- Regressions: `-storystep 3` exit 0, 0 stalls; hall and roulette rooms exit 0.

### Ruled out
- The screen binding: `-slotscreenclear` (red target) stayed black -> not the movie draw; the cause was the decal's
  emissive (WeatherEmissive 0 on unregistered props).

### Not done / next
- The neon blink (`TimeFactor`, `CurveFactor`) and the gain panels lighting on a win; the drum bone rotation /
  `AnimStop`; Cas_SlotLines shows no active lines; the button press is not checked separately (the trigger clip
  may move `Boutons`). The owner offered TDU World as a reference for these.

---

## Session 2026-11-04 (slot HUD, `win`): the slot machines are played through the retail HUD movies

The owner: *"Yes, do the HUD"* (the previous session's question: drive `Cas_SlotMachine.vmf`).

Controls at a slot machine (DualSense / keyboard): **Cross / Enter** = A (menu select, spin, validate the lines,
yes in a box), **Circle / Backspace** = B (back to the menu from play; on the menu, the "quit this machine?" box),
**D-pad up / down** = menu, **D-pad left / right** = rules pages / lines manager / bet level at play,
**Square / X key** = "Activate lines" (Classic, Beach, High Life only), **Triangle / Y key** = max bet.
(The cheats panel moved off Square while a machine is played; F4 still opens it.)

### What changed
- `source/game/casino/casino_slot_hud.{h,cpp}` (new) - `CasinoSlotHud`: loads Cas_SlotMachine.vmf + Cas_SlotScreen.vmf,
  sets the machine page / wallet / reels, routes the pad to `/:RootMenuPath` or `/:Clip` (the movie's own A / B /
  Move / Left / Right / X_Lines / X_Lock), answers `smon` `smpl` `smof` `sloo` `lini` `lino` `linc` `left` `right`
  `lock` `dbxo`, draws the DBXO box natively, drives the credit screen (`loss` / `gain` / `not_win`). Protocol:
  `docs/re/casino_spec.md` 9.6.
- `gm_casino.cpp` - the HUD opens at a machine and owns its input; `startSlotSpin` (the movie's money / lines error
  frames), `handleSlotHudEvents`; `-dumpcasinomovie` dumps the root timeline too.
- `casino_slots.{h,cpp}` - per-line stakes on the NbLines > 1 machines ("¤10 per activated line" [DATA]),
  `setActiveLines` / `getMaxLines`; each line pays paytable x bet x Price.
- `flash_player.cpp` - `_width` / `_height` get / set (content bounds; `-noclipextent` A/B), `invokeWithFlag`
  (a real boolean argument: `SetHelp(name, true)`), `-logtext` logs empty fields too.
- `vmf_file.cpp` - the vector-outline fonts' code table / metrics are found past the shapes (`-logfonts`).
- `gs_input.cpp` - `-logkeymap` (MENU_OPTION_1 = X, MENU_OPTION_2 = Y on the keyboard).

### Verified
- `-gm casino -casinoroom slots -casinoselect 6 -casinoseed 7 -casinochips 1000 -fixedtimestep -logslothud` with
  the autoplay of casino_spec 9.6: smon -> smpl ("spin 1 ... 10 chips"), lini / smli x3 / lino ("9 line(s)
  activated, 90 chips a spin"), the 600-chip win counted up 910 -> 1510, smof, sure_quit -> sloo ("back on the floor").
  `docs/images/casino_slot_hud_{menu,play,lines,win_countup,quit_box,rules,six_machines}.png`.
- All six types (`-casinoselect 0 / 4 / 5 / 7 / 22 / 6`): title, menu, play bar, a spin, exit 0. Poker + X: no `lini`.
- 0 chips: A -> the `error_money` box ("You don't have enough money to play...").
- `-noslothud` = the native text (spin 1 at 90 chips: High Life's 9 lines at ¤10 per line).
- Regressions: `-storystep 3` exit 0, 0 stalls; FREERIDE 700-frame drive exit 0, 0 stalls; `_width` A/B: map, HUD,
  options identical (diff 0.000), pause menu 5.2 (the player panel ~20 px lower - its script reads `_height` now).

### Ruled out
- A missing localization as the cause of the blank help bar labels: `m_select` / `m_Quit` resolve (`-logtext`); the
  field's font (Berlin Sans FB id 4) had ascent 6656 from the misparsed vector font.

### Not done / next
- Cas_SlotScreen is a 2D panel (top right), not on the machine's `CasinoFlashScreen`: needs a movie -> texture path.
- The second chance (hold) of Poker / Beach (`relaunch_step`, `X_Lock`, the hand on Left / Right) - the port uses
  those keys for the bet level / max bet [INF].
- The pause menu's player panel moved ~20 px down with `_width` / `_height`: check it against a retail capture
  (`-noclipextent` restores the old placement).
- The sit-in / `C_SLOT_WIN_0x` / `C_JACKPOT_*` cinematics (casino_spec 9.5).

---

## Session 2026-11-04 (casino, `win`): the real slot machines, the R8 on the hall's socle, the seaplane trip cutscenes, and collision on the spot buildings

The owner: *"finish the casino work please allowing the real slots to be used and casino car to shown in the hall then
some places on islands still have no collision lastly need the casino cutscene"*.

Controls: at a slot machine **Cross / Enter** spins, **Left / Right** the bet, **Triangle / Up** max bet, **Circle / Backspace**
back to the walk. To fly to the casino, stop at the casino spot (`EGAMHA`, or the seaplane dock `ELANDCAA` - now on the map
with the casino icon) and press **Cross / Enter**; the hall's exit door flies you home. **Circle / Esc** skips a trip scene.

### What changed
- **Real slot machines** (`casino_interior.cpp`, `gm_casino.cpp`, `casino_slots.cpp`, `intro_cinematic.cpp`): the played
  machine is seen through its own `CameraGame` (`Entity__Camera__CameraCasino`, looked up by `__Hc` - all 38 are `CAM_GAME`);
  its `Entity__Actor__SlotMachineSpinner` reels are loaded as props (they were never loaded: the windows showed the white
  backing) and driven through the decoded `TD_Casino_scroll` VS constants (`scroll_uv0..2` = the three symbol slots,
  `UV_symbol_space`, PS `blur_blend_factor`) with the material `StageConstants`. The engine takes the machine's reel count
  (3 / 5) and stop timing (`TimeBeforeFirstStop` 1.5 s). Spec: `casino_spec.md` 9.5 (the shader decode, the symbol sheets).
- **The hall car** (`CasinoInterior::placeShowCars`, `IntroCinematic` VehiclePosition + turntable): `icashal`'s
  `ActorVehiclePosition` says car 432 (the Audi R8), paint 12, 1 turn a minute [DATA]. `casino_spec.md` 9.4.
- **The seaplane trip** (`casino_trip.{h,cpp}` new, `gm_cin_viewer` `CinViewerRequest::After`, `-cinisland`): departure at
  the island dock -> `C_CASINO_ARRIVAL` -> CASINO; `leaveCasino` -> `C_CASINO_DEPARTURE` -> `C_<ISLAND>_ARRIVAL` -> FREERIDE.
  `casino_spec.md` 10, `cutscenes_inventory.md`.
- **Building collision** (`GMFreeRide::addSpotExteriorCollisions`): the streamed spot exteriors' `<bank>_sc_*.shk` go into
  the drive collision at each spot frame (122 of the 270 exterior banks have one). Before, only the home house had it.

### Verified
- `-gm casino -casinoroom slots -casinoselect 6 -casinoseed 7` + one spin: reels blur, stop in turn, the 66-chip win is
  three speed boats on the reels (`docs/images/casino_slot_highlife_spin.webp`); all six types:
  `casino_slot_machines_real_reels.webp`. Hall: `casino_hall_r8_socle.webp`.
- `-gm FREERIDE -island ibiza -casinotrip -casinoleave 200`: C_IBIZA_DEPARTURE 65.3 s -> C_CASINO_ARRIVAL 53.0 s -> CASINO
  hall -> C_CASINO_DEPARTURE; `-gm casino -casinoleave 60`: 37.0 s + C_IBIZA_ARRIVAL 52.5 s -> FREERIDE on Ibiza at the dock
  (`casino_trip_to_casino.webp`, `casino_trip_home.png`).
- Building collision A/B at the Ferrari dealer (`-tp 21796,14,24150 -heading 0`, accel held 8 s): the car hits the wall and
  stops after 61 m; `-nospotcollision`: straight through the building, 161 m (`building_collision_ferrari_dealer_ab.webp`).
- Regressions: `-storystep 3` exit 0, 0 stalls; `-casinoroom roulette -casinoselect 2` table minimum 25.

### Ruled out
- Unsupported Havok shape classes as the cause of the island collision gaps: a scan of all 4001 sector banks finds only
  classes the loader handles (the "missing" names are base classes in the class tables).

### Not done / next
- `Cas_SlotMachine.vmf` (the retail slot HUD) is not driven: the HUD is native text under the reels
  (`-dumpcasinomovie cas_slotmachine` dumps it). The avatar sit-in and the C_SLOT_ACTIVATE / WIN / JACKPOT beats are not
  played. The symbol-to-cell pairing and the spin speed are [INF].
- The Hawai trip legs are wired but were not played through. After the home arrival one run's chase camera ended in a hedge
  13 s after the handoff - check the parked pose (`gm_cin_viewer.cpp`, the StoryHandoff block).
- Collision: only the spot buildings were missing by measurement; if the owner still finds a spot, get its coordinates and
  run `OTDU_COLLISION_PROBE=x,z` (vehicle_collision.cpp) there.

---

## Session 2026-11-04 (later, `win`): the car wash plays the retail sequence with the fee after the drive-in, the car's state survives scenes, the Solar Crown studio has its TV overlays and the avatar out of the TV shots

The owner: *"cutscene fixing when arriving at solar crown and into the studio players avatar seems to be in the
way most of the time ... top right should show solar crown logo, at bottom right says tess wintery, and when the
camera points to avatar it should print players name"*; the car wash *"when you accept ... the car drives to the
center and then you see the girl put down the radio"* (reference <https://www.youtube.com/watch?v=wfTDjHH-tls>),
then the full beat (drive to the spot, ask to pay, the face, the radio girl, the other two, in-car view with the
hose girl in front and the two at the sides, they walk away), and *"the vehicles state and avatar ... must be
remembered when entering ... except the todd driving you to the used car dealer"*.

**Reproduced:**
    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -storystep 6 -nopad -fixedtimestep -playername Danny -autoplay "900:shot=...;1250:shot=...;2280:shot=..." -exitframe 3050
    build-win\bin\opentdu2-win.exe ... -gm FREERIDE -island ibiza -atwash -money 20000 -nopad -fixedtimestep -logcin -autoplay "120:press=validate;800:press=validate;..." -exitframe 3800

Controls: the car wash is accepted with **Cross / Enter** (validate) at the sponge tile; the fee box: **Cross /
Enter** pays, **Circle / Esc** (back) leaves without paying.

### What changed
- `source/game/avatarchoice/intro_cinematic.cpp/.h`: `applyLogoMessage` - the `.cin` messages `lgtv:open|clos`
  and `lgsc:open:<name>|clos` play GENERAL.vmf `/logo_TV` / `/logo_SC` (labels `open`/`close`) with
  `/:PNJName` (`-player` = `/:PlayerName`), drawn in `draw()` / `drawUi()`. `SampleVisibility`: a multi-key
  Visibility track now hides PNJs and the player too (the avatar of C_SOLAR_CROWN_I 11.41-36.4 / 39.48-58 s).
  `buildInCarFallback`: a NaN shot camera (`c_washing_02`) becomes an in-car view of the driver from the
  scene car's `DVRSEAT_LR`, else the last good camera - no more NaN basis / culling flood. `-dumpgeneral`,
  `-nocinevisibility`.
- `source/game/cinematic/gm_cin_viewer.{h,cpp}`: `CinViewerRequest::Then` (a chain of timelines), `PromptAfter`
  / `PromptLoop` / `Price` (the fee box, validate pays, back leaves), `InCar` / `InCarBackgrounds` (the car's
  mode 23 cockpit camera from camerasingame.xmb, the avatar seated, the background loops); the avatar `avm`
  messages seat the player as in GMCine; the browser / timeline only for the tool (`bTool = !bFromStory`);
  `VehicleSceneState` + `GetVehicleSceneState()`; the viewer no longer writes a default avatar into the profile.
- `source/game/freeride/car_wash.cpp`: the request is `C_INTRO_E_1` -> fee over `C_LOOP` -> `C_WASHING` ->
  `C_INCAR` (+ `C_BABE_01/02/03`) -> `C_OUTRO`; `-washonly` = the old single timeline.
- `source/game/freeride/gm_freeride.cpp`: accepting the wash no longer pays or cleans (the scene does, after the
  drive-in); `exit()` records `VehicleSceneState`, `enter()` restores roof / windows / CarDirt on the same car
  (`-noscenestate`).
- `source/game/cinematic/cinematic_audio.cpp`: `-logcinsound` prints 2D/3D, position, clip ranges, fx flags.

### Verified
- Solar Crown: "Tess Wintory" bottom right at 15 s, the player name ("Danny") on `cam_challenger` at 38 s, the
  TV logo top right; the avatar gone from the 21 / 25 / 50 s TV shots
  (`docs/images/solar_crown_i_avatar_visibility_before_after.webp`, `solar_crown_i_tv_logo_name_captions.png`).
  `-storystep 3` frames identical (diff 0.000 x4); `-storystep 5` differs only where the data hides actors;
  `-storystep 6` exit 0 -> SHOP, 0 stalls.
- Car wash: `accepted -> the drive-in`, `end of 'C_INTRO_E_1' -> the fee prompt ($10) over 'C_LOOP'`, `paid $10`,
  `end of 'C_WASHING' -> 'C_INCAR'`, `in-car part 'C_INCAR' with 3 loop(s), cockpit camera of the car`,
  `end of 'C_OUTRO'`, `FREERIDE: 'California' as it left: roof open, windows open, mud 0.00 dirt 0.00 wet 1.00`
  (with `-dirtseed 70`: the scene car `mud 0.70 dirt 0.42` before the fee). `docs/images/carwash_retail_sequence.webp`.
- The music: a real-time run with `OTDU_AUDIO_LOG=1` keeps `m_washing_intro_CINEMATIC` at gain 1.00 through all
  of `C_WASHING`, then `m_washing_CINEMATIC` in the in-car part.
- 700-frame FREERIDE drive exit 0, 0 stalls.

### Ruled out
- The "music cuts off as she places the radio" was the `-fixedtimestep` test runs (~32 fps: the scene took
  ~31 s of wall time, the 16.6 s track ran out half way) - not the mix (bus 5 is stereo, gains 1.00 every
  second), not the clip (MS-ADPCM decoded offline: continuous music), not distance. car_wash_spec 10.4.
- `-nocinevisibility` does not stop the return-to-FREERIDE crash -> the visibility change is not its cause;
  the committed tip crashes too (1/5).

### Not done / next
- **The pre-existing crash on the return to FREERIDE after the wash** (Open in PROGRESS): AV at
  `opentdu2+0x4246cd` (main thread, FREERIDE reload). Next: a `/Zi` RelWithDebInfo build and
  `cdb -G -o -c "sxe -c \"r;k 30;q\" av; g" <exe> <the wash command>` to name the function.
- `c_washing_02` (the face shot) is a seat-built [INF] framing; its 4-component Position/Heading and track
  `0x7055` are not decoded (car_wash_spec 10.3).
- The scene state is read by the car wash only; shops / house interiors keep their own paths (GMCine story
  scenes by design). No worn-clothes state exists beyond `AvatarConfig`.
- The `.cin` Fx record Z-up lead (car_wash_spec 10.4) - needs a positional cutscene voice to test.

## Session 2026-11-04 (`win`): the airport runway red stripes fixed (2DM address 3 = wrap), the Hawai igloo cutscene plays on its island and hands over to the drive

The owner: *"sync up ... -glm has work unfinished ... the issue is random texture issue at airport runway ... then
focus on cutscenes"*. Sync: `win` already held every origin tip (main = win, 0 behind glm / cline). glm's
unfinished work existed only as an **uncommitted diff in `C:\OpenTDU2-glm`** (2 files: an S_NBTEXTURES override
change + a `-nosectshadow` A/B). glm's local branch is also **32 commits ahead of origin/glm** (a cline merge,
never pushed); that is glm's to push.

**Reproduced:** `-gm FREERIDE -skipcine -nopad -fixedtimestep -tp 16600,19.0,27750 -clock 13.00` (red speckle
ground), `-tp 16860,20.5,27987` (runway: red smears + bleached surface).

### What changed
- `source/render/eden/eden_renderer.cpp`: `AddressIndex` now samples a 2DM slot address **3 (and 4) as wrap**,
  not clamp. The red ground is `Airport_lines` (TD_Road vs16/ps20). It is **not** glm's suspect `SetIndus_Paveme`,
  as `-hidematerial` showed. Its COLOR `beton_b_c` is 2DM (3,1), and the shader samples it with raw UV0
  (spirv-cross of `TD_Road_vs_16` / `ps_20`), which runs u 0.47..5.03. Clamp therefore painted the atlas's
  right-edge red/white kerb column over the whole apron. Survey (`-loguvrange '*' -edenclamp3`): slot-0 "clamp"
  materials whose UV0 leaves [0,1] on the clamped axis: airport 5/5, casino 9/13, car wash 7/11 (`SA_Stone_White`
  u 110..138, `Sector-2-4-6-5_` v 7.5..8.5). `-edenclamp3` = the A/B; `-edenwrapall` ignores all 2DM addressing.
  `-logmaterials` now prints the 2DM S_NBTEXTURES and every non-wrap slot sampler (`@sN`).
- `source/render/eden/eden_model.cpp`: `-loguvrange <substr|*>` (UV0 range + slot samplers per material).
- glm's S_NBTEXTURES patch was **tested and not taken**: the frame did not change at the airport, and it alters
  every material's permutation choice. Its comment also claimed a 2DM-0 fallback its code did not implement.
- `source/game/cine/gm_cine.{h,cpp}`: `Kind::pIsland`. Kind 0x2b (`C_IGLOO`) = `hawai` + exterior
  `islands/hawai/exterior/eph_l0a.bnk`. It used to stream **Ibiza** around a Hawai spot: floating houses, no hut.
  The end of 0x2b hands over to FREERIDE.
- `source/game/cinematic/gm_cin_viewer.h` `StoryHandoff::Island` / `EntranceSpot`; `gm_freeride.cpp` honours them
  (the island, then the spot's road-side entrance via `FindSpotEntrance`). The scene parks the car inside the hut,
  where the drive has no floor and the car tipped over.
- `source/tdu_instance.cpp`: `-storystep 13` = GM_CINE kind 0x2b.
- `source/flash/flash_movie.cpp`: the flash-art override probe checks the file exists first. It logged one
  `Image: cannot read .../mods/flash/fla_<id>.png` WARN per UI bitmap (hundreds per run); now 0.

### Verified
- Airport: runway mean diff 30.5 (before/after), apron 12-14, car wash 0.017 (unaffected), terminal front 0.0001.
  `docs/images/airport_runway_address3_before_after.webp`, `airport_apron_address3_before_after.png`.
- Cutscenes, 8 shots each: `-storystep 3/5/6` play with actors, sets and cuts. `-storystep 13`:
  `CINE: kind 43, C_IGLOO` -> `CINE: end of C_IGLOO -> FREERIDE on hawai` ->
  `FREERIDE: handoff at 'EPH_L0A_1276' entrance (12215.1, 1.0, 17067.7) on hawai`, drivable, 0 stalls
  (`docs/images/cine_igloo_hawai_before_after.webp`, `cine_igloo_handoff_freeride.png`).
- Regressions: 700-frame FREERIDE drive exit 0 / 0 stalls. `-storystep 3` frame identical to before (diff 0.007).

### Ruled out
- `-noobjects` removes the red, but `-noinstances` / `-noterrain` / `-noareas` do not -> a sector object.
  `-hidematerial SetIndus_Paveme` / `Set_` / `trotoir` leave it; `-hidematerial Airport_lines` removes it.
- The vertex bindings of `Airport_lines` are right (`-verbose` pipeline: uv0..3 at b1+8/16/24/32, normal b1+0).
- The 2DM U/V byte order: the avatar mirror cases (skin_zones_spec 1.7) fix +0x18 = U, and a swap leaves clamped
  axes out of range anyway.

### Not done / next
- The red/white striped building beside the apron (`SB_AirpoP_*` / `sb_airpo_big`) is unchanged; not checked
  against retail.
- Step 12 "Hawai First Freeride" (`isl_firstlanded`, the plane landing, `isl_firstintro`) has no kind entry:
  add one in `gm_cine.cpp` `kKinds` with `pIsland = "hawai"` and route `kStepModes[12]` like step 13.
- A few framings clip into an actor's shoulder (Solar Crown, `-storystep 5` / `6`, shots 6/3/8): compare with
  the 360 recomp before treating them as a bug.
- What the retail does with a 2DM address 3 is still [ENC]; wrap is the reading that matches the data.

## Session 2026-11-03 (prep, `win`): the integrator tree is synced to the newest line and rebuilt — cline s88 + glm s70-71b are in, the rules describe the clone layout

The owner asked for the `-win` workspace to be prepared for Claude's return:
*"make sure -win is all synced up with latest build and newest rules etc ... -glm
was stale but fixed now but -win (Claude) should be heavily behind"*. It was:
`win` sat at `9990ce6` (2026-10-30) while cline was 93 commits ahead and glm 68.

### What changed

- **Integrated, one branch at a time (RULES.md §5 order):**
  `origin/main` fast-forwarded `9990ce6 → 116597a` (the PR #3 merge of cline
  through session 82), then `origin/glm` (`4e0844e`, sessions 70-71b: the
  return-to-road badge decode, flash-art overrides, house walk outside, yard
  collision) and `origin/cline` (`6416eca`, sessions 83-88: story step 10 /
  house intros, brake glow, tunnel holes, map caches, carrier audio, foliage
  LOD1/2). `win` now contains every tip: 0 behind `origin/main`, `origin/glm`
  and `origin/cline` (102 commits ahead of the old `origin/win`).
- **Conflicts resolved by feature:**
  - `PROGRESS.md` — takes the newest board (cline s88, the same bytes glm had
    synced) and folds in glm's s70-71b rows: the island-around-the-living-room
    and the walk-out-to-the-garage rows move to **Done** (owner reports closed),
    the HUD row gains the retail return-to-road badge, the Modding row gains
    `<mods>/flash/fla_<id>.png`.
  - `CLAUDE.md` — this tree's build/run reference is `build-win` /
    `opentdu2-win.exe` (the glm merge had brought glm's paths into the shared
    file; the win tree is the one that must build its own name).
  - `docs/re/houses_spec.md` — keeps glm's §5.1/§5.2 rewrite and records the
    caravan report's three causes as closed (the entry-intro fix from cline s84
    is kept in the §9 history).
  - `source/game/spots/interior_walk.cpp/.h` — keeps glm's default island
    streaming (`-nointeriorworld` is the A/B) and reconciles the log line with
    the merged semantics; no behaviour from either side is dropped.
- **Rules/docs modernised for the clone-per-agent layout** (the files still
  described `C:\OpenTDU2`, the path remotes and the retired `sync-workspace.ps1`
  flow): `WORKSPACES.md` (current layout table, per-branch build dirs, the
  E:\OpenTDU2-main owner tree, `-Integrate`/`-Merge`), `RULES.md` (§2 paths +
  `-RequireSynced`, §5 names the win tree as integrator, §9 the new
  one-liners), `CLINE.md`'s workspace table, `CLAUDE.md`'s build/run/§7 paths,
  `check-build.ps1` (win's shard is `docs/state/main.md`, robust count parsing)
  and a rewritten `sync-workspace.ps1`: report / `-Merge` / `-Integrate`, no
  path remotes, missing trees reported instead of crashing.
- **Convergence gate (follow-up):** `check-build.ps1 -RequireIntegrated` fails
  when a tree is behind `origin/main` or `origin/win`. `RULES.md` §2 and
  `CLINE.md` now start a session with `git merge origin/win` (or `origin/main`
  when main is ahead) plus both gates, and the `sync-workspace.ps1` report shows
  the `main/win` lag for all three trees. Verified: the win tree passes both
  gates; the cline tree fails at `1 behind origin/main` until it merges the
  line (expected — that is the point).
- **`origin/main` published (2026-11-03):** fast-forwarded `116597a → 9a236c4`
  with `git push origin win:main`, so the owner's test branch and the hub now
  show the same line (main == win). `update-and-rebuild.bat` also merges
  `origin/win` from now on (main → win → glm → cline). The owner's local
  `E:\OpenTDU2-main` is ahead of that with its own test merges and its own
  unfinished merge, so it will simply merge the new main on its next run.

### Verified

- `git rev-list --left-right --count` against `origin/main`, `origin/glm`,
  `origin/cline`: all **0 behind**; `merge-base --is-ancestor` confirms both
  agent tips are in `HEAD`.
- Both PowerShell scripts parse (`Parser::ParseFile`) and
  `sync-workspace.ps1` reports all three trees.
- **Build 425 (`f93327e`) is clean** (`build.ps1` exit 0, exe
  `build-win\bin\opentdu2-win.exe`, 9,462,272 bytes): `check-build.ps1` says
  **OK**, stamp `build 425 (f93327e) · win`, 0 behind origin/win, 0 behind
  origin/main. Headless smoke: `-gm FREERIDE -skipcine -no3d -nopad -exitframe
  150` → exit 0, `600 beats, 0 stalls`; the 60-frame repeat exits 0 too. The
  game log banner reads
  `OpenTDU2 0.1.0 ... (build 425 (f93327e) · win)`; the only stderr lines are
  the expected missing `mods/flash/fla_*.png` overrides and one
  `attachMovie: no export 'specificHelp'`.

### Not done / next

- **Push** `origin/win` (`git push origin win`) once the build is verified; the
  owner and the other agents pick it up from GitHub.
- The owner's `E:\OpenTDU2-main` had its own in-progress merge (`UU CLAUDE.md`)
  when this was prepared — it is the owner's tree and was left untouched.
- Session number: this prep is not a numbered agent session; the next Claude
  session continues the numbering in this shard.


## Session 2026-11-11 (`main`): the "Eden island" bisected out of Hawaii into its own playable island

The owner: *"there's meant to be another island ... called eden island ... a small island off the hawaii
island ... possible to glitch there ... was in tdu 1 also and has a road etc ... waterfalls ... it would be
neat if we can use and expand it also be a nod to the creators"*. (Same session, earlier: the map-from-real-
locations discussion that `docs/re/modding.md` / the map_gen notes feed into.)

### What changed
- **`docs/re/eden_island_spec.md` (new)**: the islet measured out of the retail data — area 10-7, sectors
  10-7-3-1/3-2/4-2, ~600x650 m, peak 109 m, a **flooded crater** (interior -5.4 m inside a 73 m rim),
  ~16 waterfall meshes, the `SB_FBRID` footbridge, `FU_LINEA`/`FU_CATAM` ferry props, its road only as
  baked mesh + `-R.shk` (zero road-graph nodes within 1.2 km), painted on the retail minimap, **void at
  the island-level heightfield** (retail never LODed it in). Section 2 lists the ten verified loader facts
  the bisect relies on (name-fetched bank entries, index-derived sector positions, area-local Havok frames,
  3dd node names = hashcodes, the data-driven sea level, the XTEA-CBC worldconfig...).
- **`tools/mod/make_eden_island.py` (new)**: assembles `islands/eden/` into the mods overlay from the
  user's own game data — banks renamed (`hawai`->`eden`, `area-10-7`->`area-0-0`, sectors kept at 3-1/3-2/
  4-2 within area 0-0), the area `-o.3dd` sector-group hashcodes patched `Sector-10-7-*`->`Sector-0-0-*`
  with the non-shipped sectors' CITY impostors sunk -500 m, the area heightfield flattened outside the
  islet rect (drops O'ahu's severed coastline), the island heightfield flattened with the islet re-sampled
  from the area sheet, the map screen cropped + sea-masked by the area sheet, road IA / spots deliberately
  absent, `commonworld.bnk` / `worldactors.bnk` / `game_weathers.xmb` copied, and a fresh XTEA-CBC
  `worldconfig.cpr` (`NbColumns="1" NbRows="1"`; the encrypt side is the decipher's exact inverse,
  validated by decrypt->re-encrypt byte-identical on hawai's own file).
- **`tools/x360/build_bank.py` layout bug fixed**: payloads were written back-to-back while the table
  carried 16-aligned offsets — every entry after a payload of 4 mod 16 read garbage. It surfaced here
  because the all-DXT banks the old `--roundtrip` validated never had an unaligned payload. Now pads each
  payload to the alignment; round-trips re-verified on retail banks with .pmi/.shk/.uva (ibiza area-3-3,
  hawai sector-10-7-3-1: 0 problems).
- **`source/game/worldviewer/gm_world_viewer.{h,cpp}`**: `kIslands` gains `"eden"` (+ button label, F-key
  hint), the F5-F8 island-switch loop covers all four (was hard-bound to 3), and `-nopanel` starts with
  the F2 panel closed (headless screenshots).

### Verified (build 17, bed87e8+dirty, headless)
- XTEA self-test OK; the tool writes the full tree (~81 MB, mostly the verbatim commonworld copy) with a
  per-bank read-back self-check.
- `-gm FREERIDE -island eden -tp 2950,1350`: `eden: 1 x 1 areas (worldconfig.cpr)`, `1 areas loaded`,
  `island ocean bounds (-98304 -2 -98304) - (165888 -2 153600)` (the -2 m plane is Hawaii's copied water
  level — the crater floods at retail depth), sector 3-1 **75,042 triangles / 400 boxes / 370 capsules /
  523 instances**, 3-2 4,503/135, 4-2 13,409/44; the baked road triggers "Return to the road" in the
  flooded crater. The 61 missing pure-ocean sector banks warn-and-skip as designed; spots / race gates /
  aircraft / birds warn-and-continue.
- World viewer `-island eden`: aerials `docs/images/eden_island_aerial.webp` (islet + the moored
  catamaran) and `eden_island_crater.webp` (summit road, crater bridge, dock buildings, causeway); the
  drive shot `eden_island_crater_drive.webp`. Retail-map evidence `eden_island_minimap.webp`, flattened
  area sheet `eden_island_heightfield.webp`. 0 stalls, no validation VUIDs.

### Not done / next
- The map screen crop is blurry (94x114 px source; spec 7); the HUD zone label says "Ibiza: Area 1"; the
  shipped weather preset is Hawaii's.
- Expansion (spec 4): spots via `add_spot.py`, props via `props.txt`, an authored road graph (the `.IA`
  parsers take generated files), then the DEM+OSM generator on this 6x6 km canvas.
- `-island eden` FREERIDE boot without `-tp` not exercised (no house/outro flow on a third island by
  design); licences treat it as island 0 like any non-hawai name.

## Session 2026-11-13 (`main`): car stickers / vinyls — the retail decal packs, a per-car canvas and the sticker-shop editor

The owner: *"Can we dig into the car customizations with stickers/vinyls?"* (fallback quests were parked:
both proved feasible, the sticker feature won the owner's own priority). The two Explore probes + the bank
dumps answered feasibility in the plan phase: the packs are plain data, the render side needed one fact.

### What changed
- **The slot fact (spec 1)**: the car paint's sticker slot ships as **`STIKERS`** on the California's
  `Paint` / `Paint_SK` (TD_CarPaint2) — the data's own spelling; one_77's is the `USER_TEX` paint_spec 4
  recorded. Already counted into `S_NBTEXTURES` (the placeholder `user_tex` texture ships in the slot), so
  binding into it never re-permutates. Sampled with the body's UV0 — the whole-texture A/B
  (`docs/images/sticker_bind_ab.webp`) settles it empirically. `Paint_LR` (TD_CarPaint) has no slot: only
  the high LOD takes stickers.
- **`source/game/freeride/sticker_bank.{h,cpp}` (new)**: `StickerBankCatalog` (the 26 `DBStickersBanks`
  rows, localized names through `gpDatabase->getString`; banks load on demand — the art `.2db`s through
  `TextureCache::loadBankTextures` under `stickers/<bankfile>` owners, released at FREERIDE exit; the MAT_
  library's COLOR hashcodes resolve directly, verified 24/24 on `st_basic_nb`; `isBankSold` = `_isvalid`
  and not the casino/chase specials), `StickerPlacement` (bank, index, U/V centre, scale, angle, tint),
  `StickerCanvas` (1024² transparent target, the dash-GPS screen's `createRenderTarget(VK_FORMAT_UNDEFINED)`
  + `beginRenderTarget` + Renderer2D pattern; rotated tinted quads through `drawTriangles`, redrawn only
  when dirty), `StickerArt` / `StickerResolveArt` helpers.
- **`cinematic_car.{h,cpp}`**: `bindStickerTexture` — the slot-variant hunt (`USER_TEX`/`STIKERS`/
  `STICKERS`) on the paint materials (the same family filter `applyDatabasePaint` uses), pointer swap +
  `updateProgram`; `-logstickers` dumps each paint material's slots.
- **`paint_menu.{h,cpp}`**: two pages. PAINT unchanged; **STICKERS**: pack list (name + price +
  recolourable mark) → thumbnail grid (the art as its own thumbnail) → **place** (a new placement at the
  canvas centre, held Menu keys move at 0.35 canvas/s (a fifth with Start), accessories left/right scale,
  up/down rotate, X cycles the 11-colour tint palette on monochrome packs, A buys (the bank `_price` per
  sticker [INF]; a wallet that cannot pay drops the sticker), B cancels — a new sticker is dropped, an
  owned one reverts to its pre-edit values) → the placed list (A edits, Y removes without refund). The
  layout lives in `PlayerProfile::OwnedCarStickers` and is pushed to the canvas on every change.
- **`player_profile.{h,cpp}`**: `OwnedCarStickers` map + the `car_stickers` save/load line
  (`<id>:<bank>;<index>;<u>;<v>;<scale>;<angle>;<tint>|...`, the bank names carry none of the separators).
- **`gm_freeride.{h,cpp}`**: `applyOwnedStickers` beside `applyOwnedPaint` at both car-load sites (mode
  enter + the car swap); `drawOffscreen` composites the canvas when dirty and binds it once; the ESTICK
  flow hands the menu the driven car's canvas (`setStickerContext`); the **place-mode camera cut** — inside
  the walk-in shop, placing renders the freeride scene instead of the room (both draw paths), a 3/4
  rear-left pose taken once on entry (the retail editor's garage-car camera [INF]); exit tears the canvas
  + the catalog owners down. Flags: `-nostickers`, `-logstickers`, `-stickertest <bank>,<index>` (the
  M0/M1 probe, kept as the A/B hook), `-stickershop` (the menu over the freeride scene at spawn, so
  autoplay can drive the editor).
- **Docs**: `docs/re/stickers_spec.md` (new: the slot truth, the bank layout, the editor, the flags, the
  evidence, the open list), README flags row + the two FREERIDE feature rows, PROGRESS row + board header.

### Verified (build 20 (a4a50ba+dirty), headless, 0 VUIDs, 0 stalls)
- The bind A/B: with the slot swap the `st_basic_nb` white shapes appear on the body
  (`sticker_bind_ab.webp`, same pose before/after) — the retail shader does the rest.
- Second car + colour art: the Q7 in a full `st_fun01` green/white livery (`sticker_q7_livery.webp`),
  a vector art over the bonnet from the cockpit (`sticker_bonnet_art.webp`). (The R8's freeride model
  load is incomplete — a pre-existing quirk, not the sticker path; the Q7 stands in.)
- Persistence: a 16-sticker layout seeded into `profile.sav` comes back on the car at spawn
  (`sticker_spawn_layout.webp`).
- The editor driven by autoplay (`-stickershop`): place → move (U 0.5→0.908) → scale (0.22→0.52) →
  buy — "Sticker bought: $5,000" with the money exactly $5,000 lower
  (`sticker_editor_placing.webp` / `sticker_editor_menu.webp`), and the game's own save writing the
  17-entry `car_stickers` line. The tint-cycle press in that script used a wrong autoplay action name
  (`option1` vs the table's `opt1`) so the palette path ran only by code inspection — the owner's pad X
  exercises it.
- The owner's profile was restored from its pre-seed backup after the runs (the $5,000 test purchase
  rolled back with it).

### Not done / next
- The owner's in-play pass: the real ESTICK flow (walk in, the seller, place with the pad) and the feel
  of the keys; the tint cycle from a pad.
- Garage / house scenes' cars do not wear the saved layout yet (the driven car only — the scope
  `applyOwnedPaint` had at first).
- `DBStickersPricing` per-action prices; the retail stamping math is [ENC] (the port composites its own
  way); `FADEALFA` and the `IC_NB` icon question unexplored.
- The quests (driving-crazy score challenge, slow A-to-B, no-damage delivery) stay parked, feasibility
  in hand (FRIM scorer, GPS route + HUD, the damage metric to build; the `-switchcar` wedge shapes the
  "random car" flavour).
