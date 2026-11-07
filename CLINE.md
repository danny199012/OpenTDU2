# CLINE.md — this workspace

You are the **Cline** agent, working in `C:\OpenTDU2-cline` on branch `cline`.

Read `RULES.md` and `CLAUDE.md` first (the workflow contract and the
build/verify conventions and the traps), then **`PROGRESS.md`** (the status
board — the WIP block is what is in flight), then **`docs/state/cline.md`**
(this branch's session handoff — newest session at the top; write **only** this
file, never `docs/state/main.md` / `glm.md`), then `docs/state/main.md` and
`docs/state/glm.md`, then `docs/STATE.md` for the deep history (frozen up to
session 62), then `WORKSPACES.md` (how the three agent directories relate).

## This workspace, in one table

| | |
|---|---|
| Directory | `C:\OpenTDU2-cline` — the only directory you edit |
| Branch | `cline` (created from `win` at `6d1a4ce`, 2026-10-29) |
| Build dir | `build-cline\` (never another tree's build dir) |
| Configure + build | `powershell -NoProfile -ExecutionPolicy Bypass -File C:\OpenTDU2-cline\build-cline.ps1` (or `build.ps1`, which derives the same folder from the branch) |
| Run | `C:\OpenTDU2-cline\build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-cline-userdata` |
| Remotes | `origin` → the owner's GitHub (`https://github.com/danny199012/Open-TestDrive-Unlimited-2-`), which holds every branch; `upstream` → CamilleKMI/OpenTDU2 (read only history, never push) |
| Sync | `git fetch origin` + `git merge origin/cline` at the start, `git push origin cline` at the end — `sync-workspace.ps1` reports all three trees and `-Merge` does the start sequence |
| Deps cache | `C:\otdu2-deps` — passed automatically by `build.ps1` / `build-cline.ps1`; makes a fresh configure offline and fast |

The game data (`C:\TDU2-Gamedata\TestDrive2`, 14.37 GB) is **shared and
read-only** — never write into it, and never point a BNK writer at it.

## Before you start

```powershell
cd C:\OpenTDU2-cline
git fetch origin
git merge origin/cline
git merge origin/win          # the integrated line (origin/main once main is ahead)
powershell -NoProfile -ExecutionPolicy Bypass -File .\check-build.ps1 -RequireSynced -RequireIntegrated
```

Every agent pushes its branch at its session end, so this is how `cline`
starts from what the others finished (a plain merge, the repo's convention).
If it conflicts, resolve before starting work. `-RequireSynced` fails when the
branch is behind `origin/cline`; `-RequireIntegrated` fails when it is behind
`origin/main` or the integrator's `origin/win` — merge whichever is ahead and
re-run. The same script prints the build stamp, the last session entry and the
current WIP block (`RULES.md` §2).

## Handing work back

Commit everything on `cline` and **push**:

```powershell
git push origin cline
```

The owner fetches the branch and tests it. `main` is merged by whoever
integrates, one branch at a time — do not push `main` from an agent tree.

## Two things that bite in this workspace specifically

- **Long commands time out at 30 s.** A configure or a full build always exceeds
  it — run it with `Start-Process ... -RedirectStandardOutput _build.out` and
  poll the file.
- **Scratch files must be prefixed `_`** (`_*`, `*.ppm`, `opentdu2.log` are
  gitignored). Run logs, screenshots and probes stay out of the history, and
  `git status` must be clean apart from the files you intend to commit.
- **Never stop the game by process *name*.** `Stop-Process -Name opentdu2` kills
  *another* tree's run: all three workspaces used to build the same
  `opentdu2.exe`, and the hub's game is the owner's. Since the hub's `02d77fb`
  each tree builds its own name — `opentdu2-cline.exe` here, `opentdu2-glm.exe`
  in GLM, the hub keeps `opentdu2.exe` — so stop yours with
  `Stop-Process -Name opentdu2-cline`. **The wildcard is not safe either** —
  `Get-Process opentdu2* | Stop-Process -Force` matches the hub's `opentdu2` and
  GLM's `opentdu2-glm` as well, so always use the exact name (or filter by
  `$_.Path`). This is not hypothetical: a by-name kill here was the cause of the
  owner's "intermittent exit -1 crashes" (hub session 44). In a tree that has not
  merged the per-workspace name yet, filter by path instead:
  `Get-Process opentdu2* | Where-Object { $_.Path -like 'C:\OpenTDU2-cline\*' } | Stop-Process -Force`.
  The per-workspace name is also what fixes the "a running game locks the build
  output" trap: only your own run can lock `build-cline\bin` now.

## When vision fails — park, don't grind (a known environment limit, not yours)

If the first images of a session read fine and later ones come back `invalid or
exceeds size limit` (or empty) — including files you already shrunk — the limit
is in the *transport*, not in the file and not in you. The owner has reproduced
it on Cline Pass **and** on OpenRouter with the same model; other users report an
image count / size cap on the supplier side. It is not fixable from inside a
session.

What that means here, in order:

1. **Stop retrying the frame.** Every attempt costs usage and teaches nothing.
2. **Finish on evidence that needs no eyes**: `-scantunnels` coverage counts, the
   mean pixel difference between two runs, the log line, the `vk` validation
   VUIDs (`CLAUDE.md` §9.3/§9.4 accept these as proof; §8 trap 13 is the
   validation-layer recipe).
3. **Name the gap** in your session entry and in `PROGRESS.md`: which frame or
   spot is unverified, the build, and the single command that checks it (e.g. the
   `-noprunecarve` A/B at `3-0-3-7`). That turns "unfinished" into a handoff.
4. **Do not feel bad about it, and do not paper over it with a guess.** Parking a
   task with a precisely named gap is a complete session (see `CLAUDE.md` §8 trap
   14); the next agent with working vision closes it in minutes.

Written down 2026-11-03 at the owner's request, after session 86 lost the image
reader halfway through the tunnel-hole verification and had to finish on numbers.
