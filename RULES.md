# RULES — how OpenTDU2 work stays synced (single-tree edition, 2026-11-10)

This is the **workflow contract** for the owner and for every agent that works
in `C:\OpenTDU2` — vendor irrelevant, one agent at a time (see `WORKSPACES.md`).
The old three-branch / three-tree / integrator model is retired; with one
writer at a time there is nothing to converge, so these rules are about the
problems that survive: stale builds tested as if they were new, work-in-progress
that looked finished, reports without a build id, and sessions that end
unpushed.

Read this before starting. `CLAUDE.md` is how to build and verify; `PROGRESS.md`
is what state everything is in; `docs/state/main.md` is the session handoff;
`WORKSPACES.md` is the directory / repo / release mechanics.

---

## 1. The four rules that prevent almost every problem

1. **Sync before you look at a bug.** Never debug or test a tree that is
   behind `origin/main`. Run the session-start checklist (§2) first.
2. **Every report carries a build id.** The owner reports a bug, an agent
   reproduces it, a screenshot is attached — all of them name the build
   (`build N (hash) · branch`). It is now automatic: the game watermark, the
   window title, the log banner, every saved screenshot's bottom-right corner
   and the `Screenshot saved` log line. Quote it, every time.
3. **WIP is visible.** Any work that is not finished is a row in
   `PROGRESS.md` with an owner, the files touched and the single next
   step. Commits that leave work unfinished start with `wip:` (or sit on a
   `wip/*` branch, RULES §4). A feature does not move to `Done` until it is
   verified with evidence.
4. **A session that is not pushed did not happen.** Commit and push
   `origin main` before you run out of usage. The owner tests what is pushed
   (via the launcher's auto prereleases), not what is in a working tree.

---

## 2. Session start checklist (every agent, every session)

```powershell
cd C:\OpenTDU2
git pull --ff-only origin main
powershell -NoProfile -ExecutionPolicy Bypass -File .\check-build.ps1 -RequireSynced
```

- `check-build.ps1` must say **OK** (exe newer than sources) and show the build
  stamp. If it says `STALE`, run `build.ps1` before anything else. (The old
  `-RequireIntegrated` gate is retired with the integrator — there is no second
  line to be behind.)
- Read, in this order: your vendor's onboarding (`CLAUDE.md` / `CLINE.md`) →
  `docs/state/main.md` → `PROGRESS.md` (the `WIP` rows especially).
- If `git pull` does not fast-forward, someone pushed real commits — inspect
  `git log HEAD..origin/main` before deciding; never start from a tree with
  `MERGE_HEAD` present or unmerged files — finish or abort that merge first.

## 3. During the work

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
- **Scratch files are `_`-prefixed** (`_*`, `*.ppm`, logs) and stay out of the
  history. Never write into the shared game data `C:\TDU2-Gamedata\TestDrive2`.
- **Never create the archive-only RE files** (`docs/re/ps3/*`,
  `docs/re/savegame_schema_360.txt` — see `WORKSPACES.md` §4). They are
  deliberately absent from the public repo; if an RE task needs them, fetch
  them from the private repo's clone into a scratch location outside git.

## 4. Session end checklist

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\check-build.ps1
git status --short          # only intended files; scratch is gitignored
# write docs/state/main.md (newest entry at the top), update PROGRESS.md
git add -A ; git commit -m "<area>: <result> (session N)"
git push origin main        # publishes the auto prerelease the launcher sees
```

- The session entry follows the onboarding template: what changed, verified,
  **ruled out**, not done / next.
- `check-build.ps1` should end the session **OK** and the tree at
  `0 behind origin/main`.
- **Unfinished or unverified work goes to a `wip/<feature>` branch instead of
  `main`** — branch pushes do not trigger releases — and the handoff names the
  exact next step. A handoff to the next agent (any vendor, same directory)
  must leave the tree building.
- Do not leave a merge half-done. A tree with `MERGE_HEAD` is a blocked tree.

## 5. Integration

Retired with the integrator. There is one line (`main`) and one writer at a
time; work merges to `main` at the end of the session that did it (§4). The
conflict rules that still matter: resolve by **feature**, not by side, and
re-test the interaction; record the decision in the merge commit and in
`PROGRESS.md` if a status changes.

## 6. Build identity

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
| `check-build.ps1` | the exe's stamp, branch, sync state and the WIP rows |

A bug report without one of those is re-asked, not guessed at. When the owner
reports from a built exe, ask for the build id first; then test the same
scenario on the newest build before debugging.

## 7. Bug-report / handoff shape

```
Build:   build 430 (5c3e76d) · main         <- from the watermark or banner
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
| `WIP` | being worked now — owner + next step required |
| `Partial` | works for part of the scope; the gap is listed |
| `Not started` | known, unimplemented, nothing in the tree |
| `Blocked` | cannot proceed; the blocker is named |
| `Open` | a reported bug under investigation (may be unreproduced) |

Rules: update it in the feature's commit; the `WIP` block at the top is the
first thing every agent reads; a row cannot move to `Done` without evidence in
the same commit. The board is reviewed at session end (§4).

## 9. Owner commands (one-liners)

```powershell
# what am I running, is it current, what is in flight?
powershell -NoProfile -ExecutionPolicy Bypass -File .\check-build.ps1
```

`sync-workspace.ps1` and the per-tree `-Merge` / `-Integrate` flows are
retired with the three-tree model. The game always shows its build now, so
"which build was that?" is answered by the screenshot itself, not by a guess.
