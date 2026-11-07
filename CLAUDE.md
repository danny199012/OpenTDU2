# CLAUDE.md — working in OpenTDU2

You are working in **OpenTDU2**, an open-source re-implementation of the engine of
*Test Drive Unlimited 2* (Eden Games, 2011) in C++17 / Vulkan / Jolt Physics.
The original game is **not** included: the code reads the *unpacked* files of a
legal copy, and reads the **readable** half of `TestDrive2.exe`.

This file is the briefing. The session handoff is **one file per agent** under
`docs/state/` (`main.md`, `glm.md`, `cline.md`) — read your own shard second,
then the other two, and **write only your own shard before you run out of
usage**. `docs/STATE.md` is the frozen deep history (up to session 62).

---

## 1. Read these, in this order

| File | What it gives you |
|---|---|
| `RULES.md` | **the workflow contract**: sync before work, build-id everywhere, WIP flagged, session start/end checklists, merge order. Read this first with `CLAUDE.md`. |
| `PROGRESS.md` | **the status board**: Done / WIP / Partial / Not started / Blocked / Open per feature, with owner, evidence and next step. The `WIP` block at the top is what is in flight right now. |
| `CLAUDE.md` (this) | how to build, run, verify, and not waste time |
| `docs/state/<branch>.md` | **the session handoff, one file per agent** (`main.md`, `glm.md`, `cline.md`): what works, what is open, the traps. Read **your own shard first** (newest entry at the top), then the other two. You write **only** your own shard. |
| `docs/STATE.md` | the deep history (everything up to session 62, 2026-11-01) plus the shared `## Where we are` block. **Frozen:** do not append to it |
| `docs/SURVEY.md` | how to tour the game and what to look at |
| `README.md` | what the port covers, with screenshots |
| `WINDOWS_SETUP.md` | the Windows build/run specifics |
| `WORKSPACES.md` | the three-workspace rules, `sync-workspace.ps1` and the shared deps cache (only needed if more than one agent runs at once) |
| `CLINE.md` | *(in the Cline workspace `C:\OpenTDU2-cline`)* that tree's branch, build dir, user-data folder and sync commands |
| `docs/re/*.md` | the reverse-engineering specs, one per subsystem |
| `docs/re/ps3/` | the PS3-build mining route (see §7) |
| `docs/re/CROSSBUILD_PLAN.md` | which of the four versions to use for which kind of problem |
| `docs/formats.md` | the game file formats |

---

## 2. Build (Windows)

The one-liner this workspace actually uses (the wrapper adds the `-glm`
exe suffix and the `build-glm` dir):

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File build-glm.ps1
```

The underlying cmake form:

```bat
cd C:\OpenTDU2-glm
set PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;%PATH%
cmake -S . -B build-glm -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" ^
      -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe"
cmake --build build-glm --parallel
```

* `build-glm\` is already configured — normally you only need `cmake --build build-glm --parallel`.
* **`CMakeLists.txt` uses `GLOB_RECURSE ... CONFIGURE_DEPENDS`**, so after you
  **add or remove a source file** you must re-run the `cmake -S . -B build-glm` line
  or Ninja fails with `-- GLOB mismatch!`.
* The Windows SDK `x64` bin dir must be on `PATH` for `rc.exe`.
* Outputs: `build-glm\bin\opentdu2-glm.exe`, `otdu2_bnk_tool.exe`, `otdu2_sob_tool.exe`.
* **If the link fails with `permission denied` writing `opentdu2.exe`, a copy is
  still running.** Stop **only your own tree's copy, by path** - never
  `Get-Process opentdu2 | Stop-Process` (it kills every workspace's runs and the
  owner's game):
  `Get-Process opentdu2* | Where-Object { $_.Path -like 'C:\OpenTDU2-glm\build-glm\*' } | Stop-Process`.
  Each workspace builds its own name (`opentdu2-glm.exe` here, `opentdu2-cline.exe`,
  `opentdu2-glm.exe`; WORKSPACES.md).

## 3. Run and verify

```bat
build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata
```

`C:\TDU2-Gamedata\TestDrive2` is the shared, read-only unpacked game data
(14.37 GB). `-userdata` keeps saves/settings out of the real `%APPDATA%` folder.

**Verification is headless and scripted — use it for everything.** The pattern is
a run that injects input and takes screenshots, then you read the log:

```bat
build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata ^
    -gm FREERIDE -skipcine -no3d -nopad ^
    -autoplay "200:press=map;420:shot=C:/OpenTDU2-glm/_x.ppm" -exitframe 460
```

* `-exitframe N` quits after N frames; `-no3d` skips the 3D pass (much faster,
  the 2D/Flash UI still renders); `-nopad` ignores a physically connected pad.
* `-autoplay` commands: `press=<action>`, `hold=<action>`, `release=<action>`,
  `shot=<path.ppm>`, `key=<SDL name>`, `text=<s>`, `mouse=dx,dy`, `look=x,y`,
  `wheel=<n>`. Actions include `validate back pause up down left right opt1
  accel brake steerleft steerright handbrake camera backontrack walk walkleft
  walkright radio volup voldown map backview carcamera`.
  **A bare `60:left` is silently dropped — it must be `60:press=left`.**
* Screenshots are `.ppm`; convert with Pillow (`python -c "from PIL import Image; ..."`).
* **Add `-fixedtimestep` to any run that drives a menu.** Without it the
  frame time is real time (the UI runs at 100-400 fps with `-no3d`), so a
  `press` at frame N lands at a different point of the menu animations each
  run and is dropped while a menu has `_root.Input` false.
* There are many `-log*` / `-dump*` flags — `grep` the source for
  `CmdLine::HasArg` to list them all. Useful ones: `-logcameras`, `-logpause`,
  `-logflashvars`, `-logmaterials`, `-logpad`, `-logdrive`, `-vehtest`,
  `-dumplabels`, `-dumphud`, `-dumpbuttons`.
* **When a run "hangs" and the log just stops**, read the *watchdog* log:
  `<log>-watchdog.log` (on by default, `-watchdog <s>` / `-nowatchdog`). The
  frame loop beats through `Watchdog::Beat(phase, frame)`
  (`source/core/watchdog.h`) at "loop", "update", "draw" and "present", so a
  stall is reported with the phase that stopped, the last frame, the duration
  and the worst frame gap of the run — and it is reported even when the main
  thread is stuck inside a log call. `-watchdogtest <ms>` freezes the loop once
  to check it. A `VK_ERROR_DEVICE_LOST` is now reported **once** (it used to be
  logged every frame: 639 identical lines in the owner's log) and the game
  exits cleanly on it (`RenderDevice::hasDeviceLost`).
* **Useful A/B flags** to isolate a rendering problem: `-nocull`,
  `-edenswapcull`, `-noareas`, `-noterrain`, `-noobjects`, `-noinstances`,
  `-no3d`, `-noshadows`, `-noocean`.

### PowerShell trap

**PowerShell treats a native command's stderr output as a terminating error and
aborts the rest of the command chain.** Prefix any chain that runs the game (or
`git`) with:

```powershell
$ErrorActionPreference='Continue'
```

…and redirect with `2>&1 | Out-File -Encoding utf8 _out.txt` rather than `*>`.

---

## 4. Conventions

* **The code mirrors the original's structure and naming**: `GS*` for game
  services (`GSInput`, `GSFlash`, `GSAudio`), `GM*` for game modes
  (`GMFreeRide`, `GMLogin`), `ed*` for Eden engine classes. Keep it that way.
* **Style**: 4-space indent, spaces inside parentheses — `if ( !loaded ) {`.
  Run `.clang-format` (it is at the repo root). Match the surrounding file.
* **Every claim in a doc is tagged**:
  `[CODE]` read from the executable · `[DATA]` read from the game files ·
  `[INF]` inferred or chosen, with the reason given · `[ENC]` in the SecuROM
  range of `TestDrive2.exe`, not readable. **Keep using these tags** — they are
  how the next session knows what to trust.
* **Never invent a format.** Decode it from the data, and say so in the doc.
* The `docs/re/*.md` specs name functions as `FUN_00xxxxxx` (the address in
  `TestDrive2.exe`); a VA below `0x6c1000` is `[ENC]`.
* **`origin` is the owner's GitHub** (`danny199012/Open-TestDrive-Unlimited-2-`).
  **Finish every session with all work committed and pushed** — `git push origin
  <branch>`; that is how the owner gets it to fetch and test. `upstream`
  (CamilleKMI), where a tree has it, stays read-only. Commits are still
  checkpoints: **commit often**, commit before anything risky.
* Scratch files are gitignored by the `_*` pattern — name run logs and
  screenshots `_something` and keep them out of the commits.
* **The session handoff is one file per agent** — `docs/state/main.md`,
  `docs/state/glm.md`, `docs/state/cline.md` — so two branches never edit the
  same handoff file. `docs/STATE.md` (history up to session 62) is frozen: do not
  append to it. This exists because the shared file was edited by both sides of
  **32 of 38 merges** and kept stopping the owner's `update-and-rebuild.bat`.
* **`docs/STATE.md`, `docs/state/*.md`, `docs/SURVEY.md` and `README.md` merge
  with the built-in `union` driver** (`.gitattributes`): the notes and the README
  CLI table are append/prepend documents, so a plain merge conflicted in the same
  hunk every time. Union keeps **both** sides — nothing is lost, so do not "clean
  up" a merged block to a single entry; several `Newest` lines in a row are the
  union, not an error. Everything else (code, specs, images) still conflicts like
  normal, on purpose — a conflict there needs a human. `git config rerere.enabled
  true` (set in the trees) then replays a resolution you already made.



## 5. The owner's priorities (2026-10-28)

Work in this order unless told otherwise. The first three are UI/Flash, which is
where the port is furthest behind the original.

1. **The world map** — it opens now (Select / Share) but its **content does not
   draw** (the chrome renders, the centre is black). `docs/STATE.md` session 9.
2. **The pause menu / options** — since session 13 **the movie drives its own
   menus** (MAIN.vmf's scripts run; the game sends SWF key events and answers
   the `opts:<page>:open|vali|canc|<cmd>:...` FSCommands). The **Audio** page
   works end to end (values, live mixer gains, saved to
   `<userdata>/options.ini`). The other pages (Video, Display, Controls, Game
   settings) open but the game does not answer their `opts:` commands yet —
   `docs/STATE.md` session 13 has the protocol and the next step.
3. **HUD elements** — the pause clock runs on the PC's time now (session 61 `glm`); the zone badge says
   "Ibiza Area 1/2" on every island (hardcoded in `freeride_hud.cpp`), and the
   minimap draws only Ibiza roads. The zone fix has a lead: `func_0017D6E4`
   (PS3) reads `DBAreaLevel` — see `docs/re/CROSSBUILD_PLAN.md`.
4. **Physics** — the car still rolls over too easily. `-vehtest` / `-logcorner`
   measure it; the remaining lever is the missing load sensitivity of the
   Pacejka `D`. **Read the readable PC ABS/aids tables first** (they are *not*
   encrypted: `ABSCONTROLACTIVATION` at `0xd6cb61`, `ABS_Slip_Ratio_HYPERSPORT`
   at `0xe18b16`), then the PS3 twins `0x155C20` / `0x15E678`.
5. **Controller** — the pad map is the game's own (`DeviceX360Pad.xmb`); the
   in-car menu is on D-pad left, the radio on D-pad right. Camera cycling, free
   look and the back view were finished in session 11.
6. **Performance** — see `docs/PERF_PLAN.md` (session 48: ~15 ms steady, the spikes are the problem). Measure with the
   `[perf]` lines the instance prints every 120 frames.

## 6. What is already known to work — do not rebuild it

* LOGIN, AVATAR_CHOICE (intro, character select, house walk), FIRST_WM (garage),
  FREERIDE (first story drive, radio, HUD, in-car menu, world map screen) and
  the WORLD_VIEWER tool.
* 116 original Eden shader libraries through the DXSO → SPIR-V path, the car DB
  (279 entries), 6,719 localization strings, Ibiza/Hawai/Casino streaming.
* The camera cycle (chase / cockpit / bumper), free look, look behind, the
  retractable roof and the side windows.
* **Not implemented**: online, casino/Hawai gameplay, races, traffic, night
  lighting, save games, dealership/tuning interiors, the casino floor.
* **`source/flash/flash_player.cpp` *is* the AVM1 runtime** — display list,
  timelines, variables, user functions, `random()`. The comment in
  `flash_movie.h` saying it is "not implemented yet" is **stale**. Do not
  rebuild it.

---

## 7. The four versions — use the right one per problem

There are four copies of this game on this machine, and they are good for
different things. **Pick by the kind of blocker, not by the feature.**
`docs/re/CROSSBUILD_PLAN.md` has the full table.

| Version | Path | Good for |
|---|---|---|
| **PC (Steam + TDU World)** | `C:\Program Files (x86)\Steam\steamapps\common\Test Drive Unlimited 2` | **all game data** and **62 % of the code**. `TestDrive2.exe` is 32-bit x86; its `.text` is **encrypted from `0x401000` to `0x6D1000`** (2.81 MiB) and readable after that. |
| **PS3** | `C:\TDU2-PS3-Recomp\game\EBOOT.ELF` | **the encrypted band, in the clear.** PPC64, 15.6 MB of code, no SecuROM, 19,842 function boundaries, 53,119 lifted functions. |
| **Xbox 360 (rexglue)** | `E:\Xbox 360 Games\Torrent\Minerva_Myrient\Redump\Microsoft - Xbox 360\Test-Drive-Unlimited-2-Recomp` | **a running, instrumented build** — the best *behavioural* oracle. Its `generated\TestDrive2_DLL\tdu2_funcs.*.h` are 108,672 function *declarations with no Eden names*, so it gives boundaries only. |
| **OpenTDU2** | `C:\OpenTDU2` | the target. Its own bugs are not in any other version. |

**Blocker types and where they belong:** code encrypted at rest → PS3 static;
data not decoded → PC game files; a law that behaves *wrong* → the running 360
build as an oracle, not more reversing; not implemented at all → scope work.

**The PS3 route** (`C:\TDU2-PS3-Recomp`, documented in `docs/re/ps3/`): the
tooling is Python and needs `port/out/*.json` built once by
`python tools/func_index.py`. Read any function's lifted C++ with
`python tools/get_func.py 0xADDRESS --out _g.cpp`. `docs/re/ps3/README.md` is
the entry point; `constant_pools.md` has the method and the traps;
`ps3_enc_twins.json` lists **69 PS3 functions that are the readable twins of PC
code encrypted at rest** — that is the reading list for the `[ENC]` items.

**SecuROM policy:** the encrypted part of `TestDrive2.exe` is **never decrypted
or bypassed**. Reading the PS3 build of the same engine is the sanctioned way to
see that code. Do not reach into a running process.

## 8. Traps — each of these already cost a session real time

1. **`.rodata` of the PS3 ELF lives inside the FIRST `PT_LOAD` segment**
   (`0x10000..0x10A0508`), not the second. A "data = segment 2" filter silently
   finds **nothing** — every pool (`0xf120f0`, `0xfb0000`, `0xf144d0`) is below
   it. Fixing that bound took the index from 3,256 to 20,871 addresses.
2. **PS3 tools:** `xref_addr.py` re-scans 1.8 GB per query and times out;
   `addr_refs.py` only looks for the TOC-relative form. Use **`abs_refs.py`**
   (indexed, one pass) and `find_const.py` / `find_floats.py`.
3. **`[DEAD]` The PS3/PC link-order shortcut** (`PS3_VA ≈ PC_VA − 0x3F0000`):
   tested against all 19,842 function starts — no exact match, deltas scattered
   +0x230…+0x2750. Do not spend time on a global offset.
4. **The PS3 `0x0Bxxxx–0x0D5xxx` cluster is *serializers*** (name-emitting
   reflection), not the physics. The code that reads the float constants is at
   **`0x15xxxx–0x17xxxx`**.
5. **The PS3 scalar pool `0xf11f00–0xf1214c` is reached as `base + offset`** —
   no direct materialization and no pointer slot, so a constant xref cannot find
   it. Find the base register instead.
6. **XMB "arrays" may be indexed names, not array nodes.** `camerasingame.xmb`
   stores the camera cycle as `nb_camera_switch` plus
   `p_array_camera_switch[0..4]`. Reading it with
   `getArraySize("p_array_camera_switch")` returns **0** and every car silently
   fell back to one camera. Check the string table before assuming a node type.
7. **When joining two builds, assert the key space.** A PC-address dict compared
   against PS3 addresses marked **991** functions as encrypted twins when the
   true number was **69**.
8. **Do not trust a constant table as "the law".** Two uniform 1/6 grids exist
   in the PS3 build (`0xf7a1e4`, `0xf42b40`); 1/6+2/3 adjacent is suggestive of
   RK4 but the grid also holds 1/3, 1/2, 5/6. Read the consumer first.
9. **A running `opentdu2.exe` locks the build output** (`permission denied`
   writing `bin\opentdu2.exe`). Stop **your own tree's copy, filtered by path**,
   before linking - never by name alone: a by-name `Stop-Process` killed other
   workspaces' runs for a whole session (exit -1, no exception). See WORKSPACES.md.
10. **`-- GLOB mismatch!`** after adding a source file: re-run the `cmake -S . -B build`
    configure step.
11. **`config/tdu2.toml`** (the 360 project) must be flat `key = value` with no
    `[sections]` — a sectioned file loads silently and applies nothing.
12. **The PC install is modded** (TDU World bigfiles). Fine as a *data* source;
    not a clean behavioural baseline.
13. **Texture corruption / device lost on long sessions = run
    `-vk_enable_validation_layer` first.** The validation layers name the exact
    object (destroyed view in a live set, null descriptor bind); eyeballing
    repro shots does not. The lifecycle rules that cost a session: a destroyed
    GPU resource is safe only after BOTH frame slots' descriptor pools reset —
    the eden renderer's pools reset at the next scene begin, *later* than
    `beginFrame`'s flush, so pending deletes rotate one extra recycle; the
    draw-set cache must clear where its pool resets (a frame that records
    without a scene reset on that slot re-binds reset sets); a cache keyed on a
    material's hashcode cannot see runtime slot swaps (sign faces, the police
    gyro lens) or a freed material's address coming back with different
    textures — key on the current slot hashes (`EdenMaterial::textureStamp()`).

14. **If vision starts failing, park the task and document it — do not grind.**
    The symptom: the first images of a session read fine, then later ones come
    back "invalid or exceeds size limit" (or silently empty) for files the same
    size that worked an hour earlier. It is the *transport*, not the file and not
    the model — the owner reproduced it on Cline Pass and on OpenRouter with the
    same model, and other users report an image count / size cap on the provider
    side, so it is not something a session can fix by trying harder. In order:
    (a) stop trying to see the frame — each retry burns usage and proves nothing;
    (b) finish with the evidence that needs no eyes: `-scantunnels` counts,
    pixel-diff means between two runs, the log line, the `vk` VUIDs (§9.3/§9.4
    accept these); (c) write down **which frame or spot is unverified** and the
    one command that checks it, so the next agent with working vision spends
    thirty seconds instead of re-deriving the diagnosis; (d) record it in your
    session entry and in `PROGRESS.md` (`Partial`, with the visual check as the
    next step). Parking with a named gap is a *complete* outcome; a guess shipped
    without the look is not — and nothing about it is the agent's fault.

---

## 9. How to work here (the project's own method)

This codebase was built by agents that document before they code. Follow the
same loop — it is what makes the work verifiable by the next session:

0. **Sync before any work**: `git fetch origin`, then merge `origin/<branch>`
   into your branch (a plain `git merge` — the repo's convention). Every agent
   pushes at its session end, so a branch not fetched from first is stale, and
   stale work doubles up and conflicts.

1. **Read the spec first.** Most subsystems already have a `docs/re/*_spec.md`.
   If one exists, read it before touching the code; if it does not, you are
   probably about to write it.
2. **Decode, do not guess.** A behaviour comes from the game data or from the
   readable code, and the doc says which (`[CODE]` / `[DATA]` / `[INF]` /
   `[ENC]`). If you must choose, say so and give the reason.
3. **Verify headlessly, and keep the evidence.** A run with `-autoplay` plus a
   screenshot and the log line is the standard proof. Numeric comparisons
   (mean pixel difference between two runs) are accepted evidence for visual
   changes; commit the screenshots under `docs/images/`.
4. **Prefer an A/B test to an argument.** When a rendering symptom has several
   candidate causes, run the `-no*` flags one at a time rather than reasoning
   about the code. That is how the casino terrain gap was narrowed in session 10
   (seven causes ruled out, one left).
5. **Write it down before you run out of usage** — in **your own shard**,
   `docs/state/<branch>.md` (`main.md`, `glm.md` or `cline.md`), newest session
   at the top: what you changed, what you verified, what you ruled out, and what
   is next. **Update `PROGRESS.md` in the same commit** (status, owner,
   evidence, next) and flag unfinished work `WIP` — `RULES.md` §3/§8. Do not
   append to `docs/STATE.md` and never edit another agent's shard: one writer
   per file is what keeps the three-branch merge
   (`update-and-rebuild.bat`) from stopping on the notes. Then **commit and
   push** — `git push origin <branch>`: the owner fetches and tests what is
   pushed, so an unpushed session did not happen.
6. **Say what you did not do.** A verified diagnosis with the next step named is
   worth far more than a speculative patch. Several `docs/re/*.md` sections exist
   precisely to record "this is `[ENC]`, here is why, here is how to get at it".

### Quick reference

```bat
:: build
cmake --build build-glm --parallel

:: build identity, staleness, sync state and the WIP block (RULES.md §2)
powershell -NoProfile -ExecutionPolicy Bypass -File check-build.ps1

:: headless check of the world map, with a screenshot (the image carries the
:: build stamp; {build} in the path expands to the number)
build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata ^
    -gm FREERIDE -skipcine -no3d -nopad -logmap ^
    -autoplay "200:press=map;420:shot=C:/OpenTDU2-glm/_map-{build}.ppm" -exitframe 460

:: the PS3 route
cd C:\TDU2-PS3-Recomp
python tools/get_func.py 0x223200 --out _g_input.cpp
python tools/abs_refs.py 0xf11e60
```

The owner plays with a **DualSense** and reports in terms of what they press, so
when you add or fix a control, state the button (and its keyboard equivalent) in
your shard's entry.

### The session-entry template (copy this into `docs/state/<branch>.md`)

Newest session goes at the top of **your shard** (`docs/state/main.md`,
`docs/state/glm.md` or `docs/state/cline.md`), directly under its rule. The
shape matters more than the length — a short entry in this shape is worth more
than a long one that is not:

```markdown
## Session <date> (<ordinal>, `win`): <the one-line result>

The owner's report / the task, quoted if they gave words for it.

**Reproduced** (if it is a bug) — the exact command, and the screenshot:
    build-glm\bin\opentdu2-glm.exe -game ... -gm FREERIDE -skipcine -nopad -autoplay "..." -exitframe N

### What changed
- `source/path/file.cpp` — what and why, naming the original function if known.

### Verified
- the command, the log line, or the measured number. Screenshots in `docs/images/`.

### Ruled out (with the test that ruled it out)
- `<flag>` gives an identical frame -> not <cause>.

### Not done / next
- what is still broken, and the single next step with the file and line to start at.
```

Two habits that make the difference: **write down what you ruled out** (that is
often the most valuable part, and it is invisible otherwise), and **name the next
step precisely enough to start from cold** — file, function, line.




