@echo off
rem Free roam at the used car dealer ECDC_EUR. The spot position is the
rem building's origin: -snaproad 150 parks the car on the nearest road outside
rem it (the dealer lot is ~61 x 102 m around the spot).
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm freeride -tp 9849.3,20230.8 -snaproad 150 -storytime 86400
