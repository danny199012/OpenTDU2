@echo off
rem =============================================================================
rem OpenTDU2 - Main tree: pull ALL branch changes and rebuild
rem =============================================================================
rem This script fetches all branches from origin and merges origin/main (the
rem owner's test baseline), origin/win (the integrator's line - it contains the
rem agents' work that has already been agreed on), origin/glm and origin/cline
rem into this workspace, then rebuilds. It merges the agent branches themselves,
rem not only main, so work that has not been integrated yet still arrives - the
rem old script kept rebuilding an unchanged tree while new sessions sat on
rem origin/glm, and it never took origin/win at all.
rem
rem Conflicts: docs/STATE.md, docs/SURVEY.md and README.md are declared
rem merge=union (.gitattributes) because every session PREPENDS its handoff
rem entry to the logs and APPENDS rows to the README CLI table - union keeps
rem both sides, so those never stop the merge. Any other conflict is a real
rem content / code conflict and still stops the script on purpose: resolve it,
rem git add, git commit --no-edit, re-run.
rem A merge left half-finished by an earlier run (the "Merging is not possible
rem because you have unmerged files" error) is aborted at the start.
rem =============================================================================

setlocal enabledelayedexpansion

cd /d "%~dp0"

rem --- abort a merge an earlier run left unresolved ---------------------------
if exist ".git\MERGE_HEAD" (
    echo [0/6] An unfinished merge was found - aborting it first...
    git merge --abort
    if errorlevel 1 (
        echo ERROR: could not abort it - run "git status", resolve by hand, retry.
        pause
        exit /b 1
    )
)

rem --- union merge for the append-only documents (also in .gitattributes; this
rem     keeps a checkout that predates it working) ----------------------------
if not exist ".git\info\attributes" type nul > ".git\info\attributes"
findstr /C:"docs/STATE.md merge=union" ".git\info\attributes" >nul 2>&1
if errorlevel 1 (
    echo docs/STATE.md merge=union>> ".git\info\attributes"
    echo docs/SURVEY.md merge=union>> ".git\info\attributes"
    echo README.md merge=union>> ".git\info\attributes"
    echo   - union merge enabled for the session logs and the README
)

echo [1/6] Fetching all branches from origin...
git fetch origin
if errorlevel 1 (
    echo ERROR: git fetch failed
    pause
    exit /b 1
)

echo [2/6] Merging origin/main (the test baseline) into this tree...
call :merge origin/main
if errorlevel 1 goto :conflict

echo [3/6] Merging origin/win (the integrator's line)...
call :merge origin/win
if errorlevel 1 goto :conflict

echo [4/6] Merging origin/glm (GLM agent work)...
call :merge origin/glm
if errorlevel 1 goto :conflict

echo [5/6] Merging origin/cline (Cline agent work)...
call :merge origin/cline
if errorlevel 1 goto :conflict

echo [6/6] Rebuilding OpenTDU2...
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
if errorlevel 1 (
    echo ERROR: build failed
    pause
    exit /b 1
)

echo.
echo SUCCESS: this tree is up to date and rebuilt.
git --no-pager log --oneline -1
echo Exe: build-win\bin\opentdu2-win.exe  (build.ps1 names it from the checked-out branch;
echo      on the owner's main tree that is build-main\bin\opentdu2-main.exe)
pause
exit /b 0

:merge
git merge %1 --no-edit
exit /b %errorlevel%

:conflict
echo.
echo WARNING: the merge stopped on conflicts in:
git diff --name-only --diff-filter=U
echo.
echo   Resolve each file, then "git add" it and "git commit --no-edit",
echo   then re-run this script. docs/STATE.md, docs/SURVEY.md and README.md
echo   never stop it (merge=union); a conflict there means conflict markers
echo   were committed by hand - check "git diff" on that file.
pause
exit /b 1
