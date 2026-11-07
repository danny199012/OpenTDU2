@echo off
rem Free roam at the tuning shop ETUN_A. The spot position is the building's
rem origin: -snaproad 150 parks the car on the nearest road outside it.
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm freeride -tp 12605.4,16698.1 -snaproad 150 -storytime 86400
