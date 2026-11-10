# WORKSPACES — the single-tree model (2026-11-10)

This supersedes the clone-per-agent layout (`C:\OpenTDU2-win` / `-glm` /
`-cline`, one branch per agent, a win-tree integrator, `sync-workspace.ps1`).
That layout is retired: from here on there is **one directory, one repo, one
branch, one agent at a time**.

---

## 1. The tree and the repo

| | |
|---|---|
| Directory | `C:\OpenTDU2` — the only working tree |
| Remote | `origin` = `https://github.com/danny199012/OpenTDU2` — the public repo, the only remote |
| Line | `main` — work lands here (short-lived `wip/*` branches merge into it when done) |
| Releases | every push to `main` triggers CI → auto-tagged prerelease `v0.2.<run number>` (newest 3 kept); manual `v*` tags stay the stable channel. This is the channel the launcher's update check reads (`/releases/latest`) |

Why one of each: agents now run **sequentially** — the owner starts one agent
in `C:\OpenTDU2`, and when its 5-hour or weekly quota runs out, the next agent
(vendor irrelevant) takes over **in the same directory**. Parallel agents were
the only reason the per-agent branches, trees and the integrator existed.
The owner's own checkout (`E:\OpenTDU2-main` and the older `E:` copies) stays
theirs; no agent edits it.

## 2. Session mechanics

- **Session start:** `git pull --ff-only origin main`, then
  `check-build.ps1 -RequireSynced` (RULES §2). No merge chains — there is
  nothing to integrate with.
- **Session end:** commit, `git push origin main`, update
  `docs/state/main.md` (the one handoff file) and `PROGRESS.md`.
  Pushing `main` publishes an auto prerelease — that is expected and wanted;
  it is how the owner's launcher finds the newest build.
- **WIP that must not release yet:** keep it on a `wip/<feature>` branch
  (branch pushes do not trigger releases) and merge to `main` when verified.
  Current local branches: `wip/spot-holograms` — the hologram feature
  captured from the old win tree, untested; resume it there.
- Do not leave the tree with `MERGE_HEAD` or unmerged files; finish or abort.

## 3. Build

`build.ps1` derives everything from the checkout: branch `main` builds
`build-main\bin\opentdu2-main.exe`. The old wrappers (`build-cline.ps1`,
`build-glm.ps1`, `build-hub.ps1`) still work but are pointless on a `main`
checkout — use `build.ps1`. `launch\resolve_exe.bat` picks the per-branch exe
up by name. Game data stays at `C:\TDU2-Gamedata\TestDrive2`, never written by
the tree.

### Never stop `opentdu2` by name

`Get-Process opentdu2* | Stop-Process -Force` kills **every** OpenTDU2 build on
the machine, including the owner's live run
(`E:\OpenTDU2-main\build-main\bin\opentdu2-main.exe`) — they die with exit code
-1 and no exception; it once cost a session hours of crash hunting. Stop only
your own tree's process, by path:

```powershell
Get-Process opentdu2* -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like 'C:\OpenTDU2\*' } | Stop-Process -Force
```

## 4. The frozen archive (private repo)

`https://github.com/danny199012/Open-TestDrive-Unlimited-2-` (private) is the
**retired** home of the per-agent branches (`win` / `glm` / `cline`). It is
kept frozen — no pushes, ever. Two things live only there:

- the old per-agent history;
- the **RE data files that are deliberately not published**: `docs/re/ps3/*`
  (`pc_ps3_anchors.json`, `menu_strings.txt`, `ps3_enc_twins.json`,
  `flash_globals.txt`, `classes.txt`, `physics_names.txt`) and
  `docs/re/savegame_schema_360.txt`. These must **not** be created in this
  public tree. If RE data is ever needed, fetch it from a clone of the private
  repo — or from the local git bundles at `C:\otdu2-backup-20261030-0532`.

Do not confuse the archive with the upstream project repo (`CamilleKMI`'s
OpenTDU2 — configured as `upstream` where present, never pushed to).

## 5. The retired trees

`C:\OpenTDU2-win`, `C:\OpenTDU2-glm`, `C:\OpenTDU2-cline` are archival.
Everything of value in them is merged: verified 2026-11-10 — the win tip's
shop/house world-marker feature and the cline tip's casino cashier work are in
public `main` (evolved further there), the glm tree's two unpushed commits
(WebP image shrink + gallery links) were cherry-picked, and the win tree's
uncommitted hologram feature was captured as `wip/spot-holograms`. They may be
deleted when ready — **not** while an agent session has one as its cwd.
Safety net if anything ever looks missing: the git bundles under
`C:\otdu2-backup-*` and the frozen private repo itself.

## 6. Docs map

- `RULES.md` — the workflow contract (single-tree edition)
- `CLAUDE.md` / `CLINE.md` — per-vendor onboarding; whichever agent is active
  reads its own
- `PROGRESS.md` — the status board; `docs/state/main.md` — the session
  handoff (`cline.md` / `glm.md` are historical)
