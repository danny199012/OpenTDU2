# The launcher (`tools/launcher`)

A stand-alone Win32 + ImGui/Direct3D 11 application, `otdu2_launcher.exe`. It deliberately does **not** link
`otdu_engine`: a tester distribution is the launcher exe + the game exe + the user's own game data, and the launcher
builds in seconds. It shares the generated build stamp (`cmake/BuildInfo.cmake` -> `build_info.h`) with the game, and
it embeds its artwork and icon (`launcher.rc.in`) so a copied exe still looks like the game.

The launcher lives beside the game exe it starts (`build-<branch>\bin\` in a checkout, or the same folder in a
distributed zip). It finds the game exe as `opentdu2.exe` or `opentdu2-<workspace>.exe` beside itself, then in
`..\build-*\bin` - never the tools (`opentdu2_*_tool.exe`, `otdu2_launcher.exe`).

## Files

| File | Role |
|---|---|
| `main.cpp` | Win32 window, Direct3D 11 device/swap chain, ImGui host, and the headless `--selftest` / `--iso-info` / `--check-update` / `--launch-test` modes |
| `launcher_ui.cpp` | the five pages, the sidebar and the workers (extraction, update, game process) |
| `launcher_settings.*` | `launcher.ini` beside the exe (Win32 profile API, UTF-16 with a BOM so non-ASCII names survive) |
| `iso_archive.*` | the read-only ISO 9660 + UDF image reader |
| `game_installer.*` | "find the game data in the image" and the extraction loop (progress, cancel, resume) |
| `launcher_updater.*` | the git / GitHub changelog and apply paths |
| `launcher_http.*`, `launcher_json.*` | WinHTTP GET/download and a small JSON reader for the GitHub API |
| `launcher_artwork.*` | the RCDATA PNGs decoded through WIC into D3D11 textures |
| `launcher_util.*` | UTF-8/UTF-16, paths, exe probes, `CreateProcess` with captured output, and the one place that builds the game's command line |

## The five pages

- **Play** - the hero artwork, the state of the game data / player / build, **PLAY**, and the launch options (watermark
  switch, skip intro, close-on-launch, user data folder). The game starts with the exe's folder as its working
  directory, exactly like double-clicking it.
- **Game data** - the `.iso` picker, destination, *Inspect image*, *Extract game data*; and a manual game-folder row
  with the `Euro\Bnk` rule the game itself uses.
- **Updates** - the build stamp, *Check for updates*, the changelog and the apply button.
- **Player** - the player name, the `profile.sav` it found (`name`, story step, money, cars), the saves and mods
  folders.
- **Online** - username / password / server for the networking that does not exist yet.

## The ISO reader

Two filesystems, because the discs use both:

- **ISO 9660**, with the **Joliet** tree preferred (correct long names / case) and **Rock Ridge** `NM` names for a
  primary tree. Directory records and multi-extent files (a `.big` over 4 GB) are supported; `;1` versions are
  stripped.
- **UDF 2.01 / 2.02 / 2.60** with a plain physical partition: anchor at sector 256, main volume descriptor sequence,
  partition + logical volume descriptor, file set descriptor, file entries / extended file entries, `short_ad`,
  `long_ad` and embedded allocation descriptors, FID directory walking.

A hybrid image (the retail discs are ISO 9660 + UDF) is read through UDF only when its tree can be walked; otherwise
the ISO 9660 tree is used. Explicitly **not** supported, and reported as such rather than guessed at:

- **UDF with a Metadata Partition** (2.50, the Blu-ray layout) - the PS3 image's UDF tree lives in a metadata file
  this reader does not follow. Hybrid PS3 images still open through ISO 9660 (verified on the decrypted 9.6 GB PS3
  image: `TDU2` label, 35 entries, `Euro\Bnk` found at `PS3_GAME/USRDIR`).
- **UDF sparing tables / `extended_ad`** - refused, not misread.

`IsoArchive::FindGameRoot` looks for a folder that contains `Euro/Bnk` (any depth) - that folder becomes the game
root. If there is none, a `bigfile_*.map` anywhere marks the **packed retail** layout, which OpenTDU2 cannot read
yet; the page names it and stops. An installer disc (`setup.exe`, `data*.cab`, `*.msi` at the root) is recognised as
such.

Extraction copies the game root's children into the destination, so `<destination>\Euro\Bnk` exists afterwards. It
checks free space first, reports files/bytes, can be cancelled, and **resumes**: an existing file with the same size
is counted as done. Nothing is ever written into the image (it is opened `GENERIC_READ` only).

## The updater

Two modes, chosen by where the launcher lives:

- **A git checkout** (`.git` found above the exe, `git.exe` available): `git fetch origin`, then
  `git log HEAD..origin/<branch>` is the changelog (the first clause of each commit subject, with the whole subject
  behind a "details" toggle). *Fast-forward this checkout* runs `git merge --ff-only origin/<branch>` - only when the
  checkout is on that branch and clean; otherwise the page says exactly what to fix.
- **A packaged build**: the GitHub REST API. Releases first (`tag_name`, `name`, `body`, assets) - the release notes
  are the changelog and the first `.zip` asset the package. With no release, the branch's commits (`/commits`) are
  the changelog. *Download this release* streams the asset (progress, cancel) to
  `%TEMP%\opentdu2-update\package.zip`, unpacks it with the Windows `tar.exe` into `...\staged`, and *Restart to
  apply* writes `...\apply-update.cmd`: it waits for the launcher's PID to exit, backs up `otdu2_launcher.exe` to
  `.bak`, copies the staged tree over the launcher folder and restarts it.

The repo and branch are editable on the Updates page (`launcher.ini`: `[update] repo=owner/repo`, `branch=main`).

## Identity: the player name, the save, the watermark

- The Player page's name goes out as `-playername`. The game applies it **after** `LoadProfile()`, so it wins over
  the name stored in an older `profile.sav`; `SaveProfile` writes it back as `name`.
- The watermark is the game's `-tester <name>` (drawn top right with the build number, and in the window title) and
  `-nowatermark` (log line only). The launcher passes `-tester` only when its switch is on **and** a name is set; an
  empty name is a clean launch.
- The Online page's username / server are passed as `-onlineusername` / `-onlineserver`; the game logs
  `Online: account '...' (networking not implemented)` and nothing else happens. The password stays in
  `launcher.ini` and is never passed on the command line.

## `launcher.ini`

Written by the launcher (the file is recreated on every save):

```
[launcher]
game_folder=, game_exe=, userdata=, iso=, iso_destination=, player_name=,
watermark=1, skip_intro=0, auto_check_updates=1, close_on_launch=0
[online]
username=, password=, server=
[update]
repo=danny199012/Open-TestDrive-Unlimited-2-, branch=main, last_tag=
```

## Headless modes (the verification path)

```
otdu2_launcher.exe --selftest                UTF-8 round trip, JSON, launcher.ini round trip, Euro\Bnk rule,
                                             the PLAY command line (with and without the watermark)
otdu2_launcher.exe --iso-info <file.iso>     open the image, list entries, read the smallest file back
                                             (FNV-1a + a .bin dump for a byte comparison with 7-Zip)
otdu2_launcher.exe --check-update            the updater's changelog, git or GitHub
otdu2_launcher.exe --launch-test             start the game once with the launcher's exact arguments
                                             (-exitframe 150) and check its log for the watermark/online lines
--log <path>                                 where the report goes (default %TEMP%\otdu2-launcher-selftest.log)
```

The GUI itself has `--page play|gamedata|updates|player|online` for screenshots and shortcuts.

## Gotchas

- **DPI**: the fonts are baked at the monitor DPI, so every layout constant goes through `S()` (`gUiScale`) and
  `ImGuiStyle::ScaleAllSizes`. A new constant typed in raw pixels will look small at 150-225 %.
- The window is clamped to the monitor work area: 1260x820 *logical* at 225 % is 2835x1845 physical.
- **ImGui is compiled into the launcher target** (core + the Win32/DX11 backends); do not link `otdu_imgui`, it
  carries the SDL3 backend and links SDL.
- `WIN32_LEAN_AND_MEAN` is on in the launcher target - include `<shellapi.h>` / `<shobjidl.h>` explicitly - and
  `IsoArchive::ReadFile` shadows the Win32 `::ReadFile` inside that class (use `::ReadFile`).
- The packaged update needs `tar.exe` (Windows 10 1803+). Without it the page says so instead of half-applying.
- `launcher.ini` is fully rewritten on every save; do not keep hand-added keys there.
- The launcher's own log/report files are the `--log` output; the game's `opentdu2.log` is opened from the Play page.

