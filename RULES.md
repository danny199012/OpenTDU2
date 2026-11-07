# RULES — how OpenTDU2 work stays synced (2026-11-03)

This is the **workflow contract** for the owner, Claude (hub), GLM and Cline.
It exists because three branches, one owner and frequent merges kept producing
the same problems: stale builds tested as if they were new, work-in-progress
that looked finished, reports without a build id, and merges that left a tree
half-integrated.

Read this before starting. `CLAUDE.md` is how to build and verify; `PROGRESS.md`
is what state everything is in; `docs/state/<branch>.md` is each agent's session
handoff; `WORKSPACES.md` is the three-directory mechanics.

---

## 1. The four rules that prevent almost every problem

1. **Sync before you look at a bug.** Never debug or test a branch that is
   behind `origin/<branch>`. Run the session-start checklist (§2) first.
2. **Every report carries a build id.** The owner reports a bug, an agent
   reproduces it, a screenshot is attached — all of them name the build
   (`build N (hash) · branch`). It is now automatic: the game watermark, the
   window title, the log banner, every saved screenshot's bottom-right corner
   and the `Screenshot saved` log line. Quote it, every time.
3. **WIP is visible.** Any work that is not finished is a row in
   `PROGRESS.md` with an owner, a branch, the files touched and the single next
   step. Commits that leave work unfinished start with `wip:`. A feature does
   not move to `Done` until it is verified with evidence.
4. **A session that is not pushed did not happen.** Commit and push
   `origin/<branch>` before you run out of usage. The owner tests what is
   pushed, not what is in a working tree.

---

## 2. Session start checklist (every agent, every session)

```powershell
cd <your workspace>                       # C:\OpenTDU2-win | C:\OpenTDU2-glm | C:\OpenTDU2-cline
git fetch origin
git merge origin/<your branch>            # your own pushed branch (a plain merge)
git merge origin/win                      # the integrated line; origin/main once main is ahead
powershell -NoProfile -ExecutionPolicy Bypass -File .\check-build.ps1 -RequireSynced -RequireIntegrated
```

- `check-build.ps1` must say **OK** (exe newer than sources) and show the build
  stamp. If it says `STALE`, build before anything else.
- `-RequireIntegrated` fails when the branch is behind **`origin/main` or
  `origin/win`** (the integrator's line). That is the convergence gate: merge
  whichever is ahead (usually `origin/win` right after an integration), then
  re-run. With it on every session, no agent starts from a stale line and the
  next integration has nothing to catch up on.
- Read, in this order: `CLAUDE.md` → `docs/state/<branch>.md` → the other two
  shards → `PROGRESS.md` (the rows owned by your branch and every `WIP` row) →
  `docs/STATE.md` when you need the deep history.
- If you cannot merge the integrated line cleanly, resolve the conflict **before**
  any work and say so in your session entry; never start from a tree with
  `MERGE_HEAD` present or unmerged files — finish or abort that merge first.

## 3. During the work

- **One agent = one directory = one branch.** See `WORKSPACES.md`.
- **Build before you verify.** A run from an exe older than the sources is not
  evidence.
- **Verify headlessly and keep the proof**: the exact command, the log line,
  the measured number or screenshot. The screenshot automatically carries the
  build id now — do not crop it off.
- **Prefer an A/B** (`-no*` flag or a second run) over reasoning. Write down
  what each test **ruled out**; that is often the most valuable part.
- **Update `PROGRESS.md` in the same commit as the feature**, not later:
  - move the row's status,
  - put the evidence (log line / screenshot path / measured number),
  - set `Owner` and `Next`.
- **Flag WIP immediately**: `wip:` commit prefix, a `PROGRESS.md` row with
  status `WIP`, and the session entry's "Not done / next" section names the one
  next step precisely (file + function + line).
- **Shared spine is serialised**: `source/tdu_instance.cpp`, `source/shared.h`,
  `CMakeLists.txt`, `cmake/*`. If you must touch them, say so in
  `PROGRESS.md`'s WIP block first, so the other agents know before they merge.
- **Scratch files are `_`-prefixed** (`_*`, `*.ppm`, logs) and stay out of the
  history. Never write into the shared game data `C:\TDU2-Gamedata\TestDrive2`.

## 4. Session end checklist

```powershell
# your own tree only:
powershell -NoProfile -ExecutionPolicy Bypass -File .\check-build.ps1
git status --short          # only intended files; scratch is gitignored
# write docs/state/<your branch>.md (newest entry at the top), update PROGRESS.md
git add -A ; git commit -m "<area> (<branch>): <result> (session N)"
git push origin <your branch>
```

- The session entry follows the `CLAUDE.md` template: what changed, verified,
  **ruled out**, not done / next.
- `check-build.ps1` should end the session **OK** and, if possible, the tree at
  `0 behind origin/<branch>`.
- Do not push `main`; integration is §5.
- A handoff to another agent must leave the tree building and the WIP row
  pointing at the exact next step.


## 5. Integration (main / the win tree)

- Agents push their own branch only. The integrator is **`C:\OpenTDU2-win`**
  (branch `win`): it merges **one branch at a time**, resolves conflicts, builds
  and runs the smoke checks below, then pushes `origin/win`. The owner's
  `update-and-rebuild.bat` merges main, win, glm and cline into his own tree.
- Merge order that keeps origin/main an ancestor of every branch:
  `origin/main` → `origin/glm` → `origin/cline` → push `origin/win`;
  `sync-workspace.ps1 -Integrate` runs exactly this sequence (fetch, then the
  three merges) in the win tree, stopping on the first conflict.
- **`origin/main` is the owner's test branch, not the integration source.**
  `E:\OpenTDU2-main` builds it, and its `update-and-rebuild.bat` takes main +
  win + glm + cline. After a verified integration the win tree may fast-forward
  it — `git push origin win:main` (fast-forward only; never force, and no other
  tree publishes main) — so GitHub and the owner's baseline show the same line.
  Agents never need it: they take `origin/win`.
- The clone-per-agent layout (2026-10-30) means no tree has a path remote into
  another; everything travels through the owner's GitHub (`origin`).
- Conflicts are resolved by **feature**, not by side: keep the working feature
  from the branch that owns it, and re-test the interaction. Record the
  decision in the merge commit and in `PROGRESS.md` if a status changes.
- Never leave a merge half-done. A tree with `MERGE_HEAD` is a blocked tree:
  `check-build.ps1` names it and `git merge --abort` is always allowed.

## 6. Build identity (new, 2026-11-03)

The build stamp is `build <commit count> (<short hash>[+dirty]) · <branch>`.

| Where | What it shows |
|---|---|
| In game, top right | the build id (and the player name when the launcher watermark is on) — always on; `-nowatermark` for pixel-identical A/B runs |
| Window title | the build id (taskbar / screen share) |
| Log banner + first lines | `Run: build N (hash) · branch (name built <date time>)` |
| **Every test/debug screenshot** | a 5x7 `BUILD N HASH BRANCH` stamp burnt into the bottom-right corner, independent of the UI setting (the photo camera's player photos stay clean) |
| `Screenshot saved ...` log line | the path **and** the build id |
| Screenshot path | may contain `{build}`, `{hash}`, `{branch}` — expanded at save |
| Launcher | the build stamp on its status line and Updates page |
| `check-build.ps1` | the exe's stamp, branch, sync state and the WIP rows for this branch |

A bug report without one of those is re-asked, not guessed at. When the owner
reports from a built exe, ask for the build id first; then test the same
scenario on the newest build before debugging.

## 7. Bug-report / handoff shape

```
Build:   build 406 (5c3e76d) · cline      <- from the watermark or banner
Where:   -tp x,y,z / spot / menu / file
What:    what happened, in the owner's words
Steps:   1..n, the exact buttons (pad names + keyboard)
Proof:   screenshot path / log line / measurement
```

Agents quote the owner's words verbatim in the session entry, then add what was
**reproduced**, what **ruled out** and the **single next step**. "Could not
reproduce on build X, tried A/B/C" is a valid, valuable result — it is not a
failure. Keep it in `PROGRESS.md` as `Open` with the next step.

## 8. Status board (`PROGRESS.md`)

One row per feature area. Allowed statuses:

| Status | Meaning |
|---|---|
| `Done` | verified end to end, evidence linked |
| `WIP` | being worked now — owner + branch + next step required |
| `Partial` | works for part of the scope; the gap is listed |
| `Not started` | known, unimplemented, nothing in the tree |
| `Blocked` | cannot proceed; the blocker is named |
| `Open` | a reported bug under investigation (may be unreproduced) |

Rules: update it in the feature's commit; the `WIP` block at the top is the
first thing every agent reads; a row cannot move to `Done` without evidence in
the same commit. The board is reviewed at every merge (§5).

## 9. Owner commands (one-liners)

```powershell
# what am I running, is it current, what is in flight?
powershell -NoProfile -ExecutionPolicy Bypass -File .\check-build.ps1

# where are the three trees, and what is in flight? (report only, no writes)
powershell -NoProfile -ExecutionPolicy Bypass -File .\sync-workspace.ps1

# agent tree: fetch origin, then merge origin/<your branch>
powershell -NoProfile -ExecutionPolicy Bypass -File .\sync-workspace.ps1 -Merge

# win tree (the integrator): fetch origin, then merge main -> glm -> cline
powershell -NoProfile -ExecutionPolicy Bypass -File .\sync-workspace.ps1 -Integrate
```

The game always shows its build now, so "which build was that?" is answered by
the screenshot itself, not by a guess.
