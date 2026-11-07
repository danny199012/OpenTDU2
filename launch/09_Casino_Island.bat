@echo off
rem Free roam the casino island: spawns on the beach road by the casino
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm freeride -island casino -tp 2763,1457.6 -heading 137 -storytime 86400
