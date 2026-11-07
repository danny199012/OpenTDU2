@echo off
rem Free roam at the Ferrari dealership (spot ECD_FERR, Ibiza north shore).
rem The spot position is the building's origin: -snaproad 150 parks the car on
rem the nearest road outside it (the showroom is ~60 x 47 m around the spot).
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm freeride -tp 21796.3,24188.1 -snaproad 150 -storytime 86400
