# OpenTDU2 on Windows — setup notes (2026-10-27)

The README used to say "Windows should compile with MSVC but has not been tested." It does: both MSVC
(what `.github/workflows/windows.yml` builds) and clang-cl. This file records the first working Windows
setup, kept for the clang-cl + Ninja variant and the game-data extraction.

## Result

- **Build**: clean with CMake 4.3 + Ninja + clang-cl 23.1.2 (LLVM, using the
  VS2022/18 MSVC headers/libs). Exit 0, no source patches. Produces
  `build\bin\opentdu2.exe`, `otdu2_bnk_tool.exe`, `otdu2_sob_tool.exe`.
- **Run**: boots to the title screen with cinematics, Vulkan 1.4.357 on an
  RTX 5070 Ti, 116 original Eden shader libraries (`Euro/Shaders/*.sob` via
  DXSO), car DB (279 entries), 6,719 localization strings, Ibiza streaming
  (30 areas), DualSense gamepad detected. No Vulkan SDK needed on Windows
  (volk loads the driver's `vulkan-1.dll`).

## Build

### MSVC (the CI build, `.github/workflows/windows.yml`)

Needs Visual Studio 2022+ with the *Desktop development with C++* workload and CMake ≥ 3.24. The Visual
Studio generator finds the toolchain by itself, so any prompt works (no vcvars/Developer Command Prompt):

```bat
cd C:\OpenTDU2
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

Produces `build\bin\Release\opentdu2.exe`, `build\bin\Release\otdu2_bnk_tool.exe`,
`otdu2_sob_tool.exe`, `otdu2_cine_tool.exe` (multi-config generators add the `Release` folder). The CI job
uploads that folder as the `opentdu2-windows` artifact. `build.ps1` builds the same tree with clang-cl and
names the exe after the branch (`build-<branch>\bin\opentdu2-<branch>.exe`).

### clang-cl + Ninja

```bat
cd C:\OpenTDU2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" ^
      -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe"
cmake --build build --parallel
```

(Put the Windows SDK `x64` bin dir on PATH first if `rc.exe` is not found:
`C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64`.)

### "The updater said it was fine but launch\*.bat runs an older build"

Two things cause this, and `check-build.ps1` (repo root) tells you which one you
are looking at - it builds nothing and changes nothing:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File check-build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File check-build.ps1 -Tree E:\OpenTDU2
```

It prints the branch and HEAD, **any merge the updater left unfinished** (in
that case the updater stopped before its build step, so the exe was never
rebuilt), which exe `launch\resolve_exe.bat` would actually pick (the same
order, plus the other exes lying around), and whether that exe is older than the
newest source file. `build.ps1` now prints the exe path and timestamp at the end
of every build for the same reason.

1. **A merge conflict stops `update-and-rebuild.bat` before the build step** -
   the exe stays as it was. `git status` shows the files; resolve them, then
   `git add -A`, `git commit --no-edit`, and run the updater again.
2. **More than one tree** - `E:\OpenTDU2`, `E:\OpenTDU2-main`, `E:\OpenTDU2-win`
   ... each has its own build, so the `launch\*.bat` of the folder you
   double-click decides the build you get. Launch from the folder the updater
   rebuilt (or delete the stale `build*\bin\opentdu2*.exe` of the others).

## Game data

OpenTDU2 wants the unpacked `Euro/Bnk` + `Euro/Shaders` tree, not the `.big`
archives. The Steam install only ships `Euro/Bnk/Video`; the content lives in
`bigfile_EU_1-5`. Extracted with the 360 project's tooling
(`tools/tdu2_extract_pc.py` there) into:

```
C:\TDU2-Gamedata\TestDrive2\
  euro\bnk\{avatar,database,frontend,fx,ig2,interior,islands,physics,
            scenarics,sound,vehicules,...}   <- 10,518 named files
  euro\Shaders\   <- copied from the Steam install
  euro\bnk\Video\ <- copied from the Steam install
  *.cpr *.xmb BuildVersion.dat <- copied from the Steam root (config)
  _unresolved\    <- 141 entries that resolve to no name (dumped by hash,
                     manifest.txt alongside)
```

Facts learned on the way (correcting `TDUWORLD_PC_REFERENCE.md` in the 360
project):

1. **Retail PC maps are BIG-endian, 360-layout** (version bytes `01 01 00 00`,
   offsets BE). The little-endian PC maps are the TDU World **mod packs**.
   The auto-detect in `tdu2_bigfile.py` was right; the doc's complaint about
   `bigfile_EU_1` being rejected predates the current parser.
2. **Every retail PC payload is stored, not LZX**: `comp = 0` in all 10,659
   records; `sum(uncomp)` equals the `.big` size exactly. On disk the payload
   bytes are XOR'd with `D7 A8 E2 D4` (360 redumps store plain).
3. **Name resolution works**: `tools/FileNames.list` resolves 5,136/5,268
   entries on `bigfile_EU_1` (98.7%; 10,518/10,659 = 98.7% overall). The
   "resolves 0 of 5268" claim in the 360 project's doc is outdated — the
   hash-variant handling in `load_names` fixes it.

## Run

```bat
C:\OpenTDU2\build\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2                    (clang-cl build)
C:\OpenTDU2\build\bin\Release\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2           (MSVC build)
```

Useful options (from the README): `-fullscreen -width N -height N`, `-lang fr`,
`-skipintro`, `-gm FREERIDE` / `-gm WORLD_VIEWER`, `-island casino|hawai`,
`-envquality 0..3`. F2 toggles the ImGui panel in the world viewer.

## When the whole PC freezes while testing (2026-10-31)

The owner lost a machine to a hard freeze during testing (Win key and
Ctrl+Alt+Del dead, had to ride it out to a reboot). What that freeze was:
**Ctrl+Alt+Del dead means the kernel / GPU driver was stuck** - no process
(ours or anyone's) can run code during that, so no in-app guard can fire
*while* it happens. The defenses are on three levels:

1. **Windows' own GPU recovery (TDR)** - the OS detects a hung GPU and resets
   it within a couple of seconds (the screen blinks, the driver recovers or
   the app gets `DEVICE_REMOVED`). If freezes last minutes instead of seconds,
   check that TDR is at its defaults:
   `HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers` must **not**
   have `TdrLevel` set to `0` (some "tweaks" disable it) and `TdrDelay` should
   be small (default 10 s). An up-to-date GPU driver is the other half of
   this - the hang itself is a driver-level bug.
2. **The app-level kill guard** (new): the frame-loop watchdog now terminates
   the process when no frame has run for `-watchdogkill <s>` (default 60 s,
   `0` = off; exit code `0x43544157`). The phase that hung is in
   `opentdu2-watchdog.log` (`STALL` / `KILL` lines) - send that file with any
   freeze report. A debugger attached suppresses the kill (breakpoints freeze
   all threads); long jobs beat through it (`-scantunnels`, the asset index
   build). `-watchdogtest 65000` demonstrates the kill end to end.
3. **Testing hygiene** - the cheap ways to lower the trigger odds: test
   windowed or with VSync on (the present then throttles instead of pushing
   thousands of frames per second at the driver), keep the frame limit on in
   the advanced settings, and prefer `-no3d` for the headless world scans.

## Status of the port itself

Early but real: LOGIN, AVATAR_CHOICE (intro, character select, house walk),
FIRST_WM (garage), FREERIDE (first story drive in Ibiza) and the WORLD_VIEWER
tool work. Not yet: online, casino/Hawai gameplay, races, traffic, night
lighting, save games. ~16 commits, one developer — a good place for
contributions, especially with the container/format knowledge from the 360
project (BANK internals, name tables, hash lists).
