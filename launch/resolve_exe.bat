@echo off
rem Sets OTDU_EXE to THIS tree's game executable. One exe name per workspace, so
rem a run in one tree can neither lock nor kill another's (WORKSPACES.md):
rem   branch win   -> build-win\bin\opentdu2-win.exe
rem   branch cline -> build-cline\bin\opentdu2-cline.exe
rem   branch glm   -> build-glm\bin\opentdu2-glm.exe
rem build.ps1 names the build dir and the exe after the checked-out branch; the
rem legacy names (build\bin\opentdu2.exe ...) are still accepted, so a tree built
rem with an older script keeps launching. Call it from the repo root (the
rem launchers cd there) - the paths are relative, so the folder may be renamed.
set "OTDU_EXE="
for %%E in (
    "build-main\bin\opentdu2-main.exe"
    "build-win\bin\opentdu2-win.exe"
    "build-cline\bin\opentdu2-cline.exe"
    "build-glm\bin\opentdu2-glm.exe"
    "build\bin\opentdu2.exe"
    "build\bin\opentdu2-main.exe"
    "build\bin\opentdu2-win.exe"
    "build\bin\opentdu2-cline.exe"
    "build\bin\opentdu2-glm.exe"
) do (
    if not defined OTDU_EXE if exist %%E set "OTDU_EXE=%%~E"
)
if not defined OTDU_EXE (
    echo No OpenTDU2 build in %CD%
    echo Build this tree first:  powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
    echo Looked for build-win\bin\opentdu2-win.exe, build-cline\bin\opentdu2-cline.exe,
    echo   build-glm\bin\opentdu2-glm.exe and build\bin\opentdu2.exe
    pause
    exit /b 1
)
exit /b 0

