## What this changes

<!-- One or two lines; the branch and build id go below. -->

## Checklist (RULES.md)

- [ ] Branch synced with `origin/<branch>` before the work (`git merge origin/<branch>`)
- [ ] `check-build.ps1` says **OK** after the final build (exit 0)
- [ ] Headless verification: the exact command + the log line / screenshot (build stamp visible)
- [ ] `docs/state/<branch>.md` has a new session entry at the top (what changed / verified / ruled out / next)
- [ ] `PROGRESS.md` rows updated **in this PR** (status, owner, evidence, next)
- [ ] Any unfinished work is flagged `WIP` in `PROGRESS.md` and the commit title starts with `wip:`
- [ ] Shared spine (`tdu_instance.cpp`, `shared.h`, `CMakeLists.txt`, `cmake/*`) was announced in the WIP block if touched
- [ ] No game data / `_*` scratch files committed

## Build / evidence

- Build:
- Run:
- Screenshot(s):

## Ruled out / not done

<!-- The most valuable lines: what this does NOT fix, and the precise next step. -->
