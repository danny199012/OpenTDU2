# WORKSPACES — three agents, three directories (updated 2026-11-03)

The clone-per-agent layout the owner chose on 2026-10-30 is live. Each tree is an
ordinary `git clone` of the owner's private GitHub repository, each works on its
own branch, and **everything travels through `origin`** — there are no path
remotes between the trees any more.

| Agent | Directory | Branch | Build dir / exe |
|---|---|---|---|
| Claude (integrator) | `C:\OpenTDU2-win` | `win` | `build-win\bin\opentdu2-win.exe` |
| GLM | `C:\OpenTDU2-glm` | `glm` | `build-glm\bin\opentdu2-glm.exe` |
| Cline | `C:\OpenTDU2-cline` | `cline` | `build-cline\bin\opentdu2-cline.exe` |

`origin` in every tree is `https://github.com/danny199012/Open-TestDrive-Unlimited-2-`
(the owner's private repository). `win` is the **hub / integration branch** (the
win tree merges `origin/main` → `origin/glm` → `origin/cline` and pushes
`origin/win`). `main` is the **owner's test branch**, not the integration source
— the win tree may fast-forward it to a verified win tip (`git push origin
win:main`, fast-forward only; never force). The owner's test tree is
`E:\OpenTDU2-main` (branch `main`, exe `build-main\bin\opentdu2-main.exe`; its
`update-and-rebuild.bat` merges main + win + glm + cline); the older E: copies
(`E:\OpenTDU2`, `E:\OpenTDU2-{win,glm,cline}`) are kept as backups and no agent
edits them.

**The workflow contract is `RULES.md`** — sync before work, build id everywhere,
WIP visible in `PROGRESS.md`, commit + push at session end. The session-start
command for any tree is:

```powershell
git fetch origin
git merge origin/<your branch>
powershell -NoProfile -ExecutionPolicy Bypass -File .\check-build.ps1 -RequireSynced
```

The per-tree mechanics and the history of the three-directory setup follow.


## Never stop `opentdu2` by name (2026-10-29)

`Get-Process opentdu2* | Stop-Process -Force` kills **every** OpenTDU2 build on the machine — the other agents'
test runs and the owner's game included (they die with exit code -1 and no exception; it cost the `win`
session hours of crash hunting). Stop only your own workspace's process, by path:

```powershell
Get-Process opentdu2* -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like 'C:\OpenTDU2-cline\*' } | Stop-Process -Force   # your own tree's path
```

The owner's live run is `E:\OpenTDU2-main\build-main\bin\opentdu2-main.exe` — never stop that one.

## One executable name per workspace (2026-10-29, per-branch names since 2026-10-30)

Each tree builds its own name, so a process name never crosses trees:

| Tree | Build dir | Executable |
|---|---|---|
| `C:\OpenTDU2-win` (integrator) | `build-win\` | `build-win\bin\opentdu2-win.exe` |
| `C:\OpenTDU2-cline` | `build-cline\` | `build-cline\bin\opentdu2-cline.exe` |
| `C:\OpenTDU2-glm` | `build-glm\` | `build-glm\bin\opentdu2-glm.exe` |
| `E:\OpenTDU2-main` (the owner's) | `build-main\` | `build-main\bin\opentdu2-main.exe` |

`build.ps1` derives the build dir and the `-suffix` from the checked-out branch, so a fresh shell only needs
`powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1`. `build-cline.ps1` / `build-glm.ps1` /
`build-hub.ps1` are thin wrappers kept for habit (`build-hub.ps1` still passes `-PlainExe`, i.e. the legacy plain
`opentdu2.exe`). `CMakeLists.txt` option `OTDU_EXE_SUFFIX` sets the name; `build.ps1` reconfigures when the
cached suffix differs, so a renamed folder or branch still produces the right exe.
To stop your own run: `Stop-Process -Name opentdu2-cline` (or `-win` / `-glm`, or the owner's `-main`); anything
else is someone else's run.

## Keeping a workspace fresh: `sync-workspace.ps1` (clone model, 2026-11-03)

The script reports on all three trees and can sync the one you are standing in.
It has no path remotes to maintain — everything is `origin`.

```powershell
cd C:\OpenTDU2-cline                                   # or either other tree
powershell -NoProfile -ExecutionPolicy Bypass -File .\sync-workspace.ps1            # report only
powershell -NoProfile -ExecutionPolicy Bypass -File .\sync-workspace.ps1 -Merge     # fetch + merge origin/<branch>
powershell -NoProfile -ExecutionPolicy Bypass -File .\sync-workspace.ps1 -Integrate # win tree only: main -> glm -> cline
```

- **Report** (default): branch, HEAD, dirty state, and *behind/ahead* against
  `origin/<branch>` plus the `main/win behind` column — the lag behind
  `origin/main` and the integrator's `origin/win`. It reads the local remote
  refs, so it never touches the network unless you sync.
- **`-Merge`**: refuses a dirty tree or a running game from *this* workspace, runs
  `git fetch origin`, merges `origin/<your branch>`, and prints whether `source/`,
  `CMakeLists.txt` or `cmake/` changed (i.e. whether a rebuild is needed).
- **`-Integrate`**: the win tree's sequence — fetch, then merge `origin/main` →
  `origin/glm` → `origin/cline` in the RULES §5 order, stopping on the first
  conflict. Refuses in any other tree.

The read-only calls pass `--no-optional-locks`, so a report is safe to run while
another agent is committing in its own tree. A tree that is missing (a retired E:
copy, for example) is reported as missing instead of failing the script.

## Fast, offline configures: the shared dependency cache

`cmake/Dependencies.cmake` clones 9 repos from GitHub (SDL, Jolt, imgui, volk,
VMA, Vulkan-Headers, SPIRV-Headers, stb, earcut) every time a **new** build dir is
configured — minutes of network, per workspace. It also accepts a pre-cloned
folder through `-DOTDU_DEPS_DIR`; its own header comment documents this.

`C:\otdu2-deps` is that folder: 347 MB, one copy shared by all three workspaces,
outside every repo (so deleting it costs nothing but a re-clone — it is never a
source of truth). `build.ps1` (and the `build-*.ps1` wrappers) pass it
automatically when it exists. To (re)create it, run `seed-deps-cache.ps1` from
any tree (it copies the sources CMake already downloaded into an existing build
dir; folders that already exist are skipped). A configure with the cache makes
**no network access at all**.

Note that once a build dir uses the cache, its `_deps` holds only the build trees
(`*-build` / `*-subbuild`) — the sources stay in `C:\otdu2-deps` and are shared,
so nothing is duplicated per workspace. `seed-deps-cache.ps1` picks its source
from the first build dir that still has them (`build-win`, `build-cline`,
`build-glm`, `build`), so re-seeding works from any tree.

Each branch's build dir is configured on demand and needs no cache once seeded.
If one has to be recreated, use:

```powershell
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DOTDU_DEPS_DIR=C:/otdu2-deps `
      -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" `
      -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe"
```

## Merge rhythm that works

0. **The session handoff is one file per agent** — write to
   `docs/state/<your branch>.md` (`main.md` / `glm.md` / `cline.md`) and never to
   another agent's shard or to `docs/STATE.md` (frozen history to session 62).
   Before 2026-11-01 every session appended to the single `docs/STATE.md`, so
   two branches collided there in **32 of 38 merges** and it stopped the owner's
   `update-and-rebuild.bat`. The logs and the README still merge with `union`
   (`.gitattributes`), which is the safety net — one writer per file is the fix.
1. Each agent commits on its own branch (`win` / `glm` / `cline`) and **pushes
   it at session end**: `git push origin <branch>`.
2. The **win tree** integrates one branch at a time in the `RULES.md` §5 order:
   `git fetch origin`, then merge `origin/main` → `origin/glm` → `origin/cline`
   (`sync-workspace.ps1 -Integrate` runs exactly that). Resolve by feature,
   rebuild, then push `origin/win`.
3. The other trees take the integrated line on their next session
   (`git fetch origin` + `git merge origin/main`, or `origin/win` while it is
   ahead of main; `sync-workspace.ps1 -Merge` does the fetch + own-branch merge).
   A conflict is therefore resolved once, in the win tree.
4. Every tree's history then contains the others' tips; the owner's
   `update-and-rebuild.bat` merges `origin/win` into `E:\OpenTDU2-main`. The
   exact shas belong in `check-build.ps1`'s report, not here.

---

# Running three agents on OpenTDU2 at once

Setup for driving OpenTDU2 with three AI agents in parallel (Claude / GLM /
Cline) without them fighting over files, the build directory, or the save
folder.

## The three workspaces

| | `C:\OpenTDU2-win` (integrator) | `C:\OpenTDU2-glm` | `C:\OpenTDU2-cline` |
|---|---|---|---|
| Agent | Claude | GLM | Cline |
| Branch | `win` | `glm` | `cline` |
| Origin | the owner's GitHub (`origin`) | same | same |
| Build dir / exe | `build-win\` / `opentdu2-win.exe` | `build-glm\` / `opentdu2-glm.exe` | `build-cline\` / `opentdu2-cline.exe` |
| Build command | `build.ps1` | `build-glm.ps1` | `build-cline.ps1` |
| User data | `-userdata C:\temp\otdu2-win-userdata` | `-userdata C:\temp\otdu2-glm-userdata` | `-userdata C:\temp\otdu2-cline-userdata` |
| Integrated line | `origin/main` | `origin/main` | `origin/main` |

`origin` is the owner's private repository and is **writable for the agents' own
branches** — that is the workflow (`git push origin <branch>` at session end).
No tree has a path remote into another any more; the old `win` / `glm` / `cline`
remotes are gone. `upstream` (CamilleKMI/OpenTDU2, read only) remains only as
history where it exists — never push there.

Never `git push` into another workspace's directory either: the receiving branch
is checked out there, and `receive.denyCurrentBranch` refuses it. Merging is
always fetch + merge, in the tree that wants the work.

## The three things that actually collide

Two agents in one tree break on three things, and only the first is obvious:

1. **The source tree** — one agent saves over the other. Fixed by separate clones.
2. **The build directory** — Ninja/MSVC object files and the output `.exe` are
   locked while a build or a running game holds them. Fixed by `build-glm\`.
3. **The user-data (save/settings) folder** — *invisible until it bites.*
   `SDL_GetPrefPath()` returns a single per-user folder
   (`%APPDATA%\OpenTDU2\OpenTDU2\`) **shared by every copy of the game**, so both
   workspaces wrote saves and settings to the same place. Fixed by the
   `-userdata` / `OTDU2_USER_DIR` override added in `31aa09d`.

## Why the game data is NOT copied

`C:\TDU2-Gamedata\TestDrive2` is **14.37 GB in 10,805 files** and C: has ~69 GB
free. It is read-only input and it is **reproducible**: the 360 project's
`tools/tdu2_extract_pc.py` regenerates it from the retail bigfiles (see
`_extract.log` — 98.7% name resolution). Copying it would cost 14 GB for zero
benefit. Both workspaces share the one copy via `-game`.

> If a future BNK writer is ever pointed at this tree, make it write to an
> **output** directory, never in place — that is the one way to lose the data.

## Build

Each workspace needs its own build dir, and **one command does the configure + build in any of them**:
`build.ps1` adds `ninja.exe` and `rc.exe` to `PATH`, passes `OTDU_DEPS_DIR` and `CMAKE_MAKE_PROGRAM`, and derives the
build dir + exe suffix from the checked-out branch.

```powershell
# win tree (integrator)
cd C:\OpenTDU2-win
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1        # -> build-win\bin\opentdu2-win.exe

# glm tree
cd C:\OpenTDU2-glm
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-glm.ps1    # -> build-glm\bin\opentdu2-glm.exe

# cline tree
cd C:\OpenTDU2-cline
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-cline.ps1  # -> build-cline\bin\opentdu2-cline.exe

# the underlying cmake form (what the script runs; configure once)
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DOTDU_DEPS_DIR=C:/otdu2-deps `
      -DCMAKE_MAKE_PROGRAM="C:/Users/cuz_c/Downloads/ninja-win/ninja.exe" `
      -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" `
      -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe"
cmake --build build-win --parallel
```

A configure + full build is longer than a 30 s command timeout: run it in the
background and poll the log, e.g.
`Start-Process powershell -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','C:\OpenTDU2-win\build.ps1' -RedirectStandardOutput C:\OpenTDU2-win\_build.out -RedirectStandardError C:\OpenTDU2-win\_build.err -NoNewWindow`
then read `_build.out`.

## Run

```powershell
# win tree
C:\OpenTDU2-win\build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 `
    -userdata C:\temp\otdu2-win-userdata

# glm tree
C:\OpenTDU2-glm\build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 `
    -userdata C:\temp\otdu2-glm-userdata

# cline tree
C:\OpenTDU2-cline\build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 `
    -userdata C:\temp\otdu2-cline-userdata
```

`-exitframe N` quits after N frames — use it for headless verification runs.

## Merging (clone model)

```powershell
# the win tree: integrate, one branch at a time, in this order
cd C:\OpenTDU2-win
git fetch origin
git merge origin/main
git merge origin/glm
git merge origin/cline
# resolve by feature, rebuild, run the smoke checks, then:
git push origin win

# any agent tree: start from its own pushed branch ...
cd C:\OpenTDU2-cline
git fetch origin
git merge origin/cline
# ... and take the integrated line when it is ahead:
git merge origin/main        # or origin/win while the win tree is ahead of main

# the whole sequence above, scripted:
powershell -NoProfile -ExecutionPolicy Bypass -File .\sync-workspace.ps1 -Integrate   # win tree
powershell -NoProfile -ExecutionPolicy Bypass -File .\sync-workspace.ps1 -Merge       # agent tree
```

Merge **sequentially** — never let two agents merge at the same time, and never
merge into another tree's working directory. The `win` tree is the integrator:
land work there first, then let the other two take the integrated line, so a
change only has to be resolved once. `main` stays an ancestor of every branch by
the RULES.md §5 order.

## The owner's GitHub repository (2026-10-30)

The owner's **private** repository is the long-term home of the work:

    https://github.com/danny199012/Open-TestDrive-Unlimited-2-

`main` there is the **integrated** line. Each agent keeps its **own branch** —
`win` (the hub), `glm`, `cline` — and pushes that branch; whoever integrates
merges a branch into `main` and pushes `main`. **Do not have three trees push
`main` directly**: the second push is rejected as non-fast-forward and has to
`git pull --rebase` first. The branch-per-agent model (below) is what avoids
that, so keep it.

The remote names are **not the same in every tree** — check before you push:

| tree | the owner's repo | CamilleKMI/OpenTDU2 |
|---|---|---|
| `C:\OpenTDU2-cline` | `origin` | `upstream` (read only) |
| `C:\OpenTDU2-glm` | `origin` | *(none)* |
| `E:\OpenTDU2-main` (the owner's test tree, branch `main`) | `origin` | *(none)* |
| `C:\OpenTDU2-win` (the hub's C: clone) | `origin` | *(none)* |

**Session rules (the owner, 2026-10-30):**

* **Start of a session** — `git fetch origin`, then merge `origin/<branch>`
  into your branch before any work. Every agent pushes at its session end, so
  an unfetched branch is stale; stale work doubles up and conflicts.
* **End of a session** — everything committed AND pushed: `git push origin
  <branch>`. The owner fetches a branch and tests it; an unpushed session did
  not happen. `main` is still merged by whoever integrates, one branch at a
  time.

To add it, once per tree (done — every working tree now has `origin`):

```powershell
cd C:\OpenTDU2-cline
git remote add origin https://github.com/danny199012/Open-TestDrive-Unlimited-2-
git push -u origin cline

cd C:\OpenTDU2           # the hub: rename first, its `origin` is CamilleKMI
git remote rename origin upstream
git remote add origin https://github.com/danny199012/Open-TestDrive-Unlimited-2-
git branch --set-upstream-to=origin/main win

cd C:\OpenTDU2-glm
git remote add origin https://github.com/danny199012/Open-TestDrive-Unlimited-2-
git push -u origin glm
```

**Licence**: the repository's own code is **GPL-3.0-or-later** (`LICENSE`).
`NOTICE` scopes what that does *not* cover — the game and its data (Eden Games /
Atari), the reverse-engineering extracts and the in-game screenshots under
`docs/`, `third_party/dxvk`'s zlib, and the upstream authors' portions (upstream
OpenTDU2 carries no licence of its own). Read it before reusing anything.

**Safety net (2026-10-30)**: before the GitHub move, full `git bundle` backups of
all three trees were taken — `C:\otdu2-backup-20261030-0532` (hub / glm / cline,
~430 MB, each verified as a complete history), plus the loose uncommitted files
in its `uncommitted\`. Any tree can be rebuilt from its bundle, so the trees no
longer have to be treated as the only copy.

## The clone-per-agent layout (2026-10-30, owner's plan)

The target model, which the owner chose so that "it always goes to GitHub" and
the `.exe`-lock / remote-name traps disappear:

| tree | branch | notes |
|---|---|---|
| `C:\OpenTDU2-win` | `win` | the integrator (was `C:\OpenTDU2`, the hub) |
| `C:\OpenTDU2-cline` | `cline` | |
| `C:\OpenTDU2-glm` | `glm` | |

**Each is an ordinary `git clone` of the owner's repo**, so `origin` = the owner's
GitHub in all three and `git push` needs no setup. There are **no cross-tree path
remotes** and `sync-workspace.ps1` is no longer used — integration is
`git fetch origin` + `git merge origin/main` in the `win` tree, then
`git push origin main`.

**Building** is one branch-derived script, `build.ps1`: it takes its folder from
`$PSScriptRoot` and names the build dir and the exe after the checked-out branch,
so folder names and branch names can never drift apart and a tree can be renamed
freely:

```
branch win   -> build-win\bin\opentdu2-win.exe
branch cline -> build-cline\bin\opentdu2-cline.exe
branch glm   -> build-glm\bin\opentdu2-glm.exe
```

`build-cline.ps1` / `build-glm.ps1` / `build-hub.ps1` are thin wrappers over it
(`build-hub.ps1` passes `-PlainExe` for the legacy plain `opentdu2.exe`).
`launch\resolve_exe.bat` accepts the per-branch names and the legacy ones.

**The owner's own copy lives elsewhere** (`E:\OpenTDU2`, their personal testing
tree), which is the point of the rename: no agent tree owns the plain
`opentdu2.exe` any more, so the "never stop `opentdu2` by name" rule has nothing
left to protect.

**Migrating** is scripted: `migrate-to-github-clones.ps1` deletes and re-clones
the hub as `C:\OpenTDU2-win`, and **stages** the two trees a live agent is in
(`C:\OpenTDU2-cline2`, `C:\OpenTDU2-glm2`) rather than touching them — a folder
cannot be replaced while an agent has it as its working directory (Windows will
not even delete it). The script prints the two swaps to run when each agent is
idle:

```
rmdir /s /q C:\OpenTDU2-cline  &  move C:\OpenTDU2-cline2 C:\OpenTDU2-cline
rmdir /s /q C:\OpenTDU2-glm    &  move C:\OpenTDU2-glm2   C:\OpenTDU2-glm
```

**After a swap, build once before anything else.** CMake writes absolute paths
into `CMakeCache.txt`, so a build dir configured in the staged folder
(`C:\OpenTDU2-cline2`) breaks the renamed tree: the build fails on
`cmake.verify_globs` and `-Reconfigure` fails too (`The current CMakeCache.txt
directory ... is different than the directory ... where CMakeCache.txt was
created`). `build.ps1` now detects that itself and starts the build dir over, so
the recovery is one command — verified in the `cline` tree (session 12,
`docs/STATE.md`). The migration script cannot do it: it builds each tree while it
is still staged under the old name.

## Rules for the agents

**The workflow contract is `RULES.md`** (sync before work, build-id everywhere,
WIP flagged in `PROGRESS.md`, session start/end checklists). The list below is
the workspace-specific part of it.

1. **One agent = one directory.** Claude → `C:\OpenTDU2-win` (`win`), GLM →
   `C:\OpenTDU2-glm` (`glm`), Cline → `C:\OpenTDU2-cline` (`cline`). No
   exceptions — no agent edits, builds in, or runs the game from another
   directory (the owner's `E:\OpenTDU2-main` included).
2. **Never touch the other workspace's files**, including its `.git/config`.
3. **`tdu_instance.cpp`, `shared.h` and `CMakeLists.txt` are shared spine** —
   serialise them, or freeze them before forking agents. With three agents this
   matters more, not less: a shared-spine change is the one thing that reliably
   conflicts on every merge.
4. **Commit before starting anything risky**, so `git reset --hard` is always
   available. Commit often; commits are checkpoints, not publications.
5. **Tag known-good states** (`git tag win-build-ok`) so a broken build is a
   one-command bisect.
6. **Finish or revert a task before switching agents.** Half-done work in an
   unfamiliar style is the worst possible handoff.
7. **Do not run two git commands in the same repo concurrently** — they collide
   on `.git/index.lock`. This already happened once while setting this up. With
   three workspaces, also avoid merging in one tree while another agent is
   committing in the tree you are merging from.
8. Update **your own** `docs/state/<branch>.md` shard and `PROGRESS.md`
   **before** you run out of usage, not after (never another agent's shard,
   never `docs/STATE.md`).
9. **State the workspace and branch in every commit message** the way the log
   already does, and keep new session entries at the *top* of your shard — that
   is where the other agents look first.
10. **Scratch files are prefixed `_` and are gitignored** (`_*`, `*.ppm`,
    `opentdu2.log`) — run logs, screenshots, probes stay out of the history.

## Known traps

- `source/flash/flash_movie.h:14` says "the SWF display list / ActionScript
  runtime is not implemented yet". **That comment is stale.** `flash_player.cpp`
  (93 KB) *is* the AVM1 runtime — display list, timelines, variables, user
  functions, `random()`. Do not rebuild what is already there.
- `config/tdu2.toml` (360 project) must be flat `key = value` with no
  `[sections]` — a sectioned file loads silently and applies nothing.
- Windows PowerShell treats git's stderr output as a terminating error and
  aborts the rest of the command chain. Prefix command chains with
  `$ErrorActionPreference='Continue'`.
