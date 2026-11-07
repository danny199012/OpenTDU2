@echo off
rem Free ride Ibiza with the developer cheats panel up (F4 toggles it; the pad
rem code is a circle of the D-pad three times with X mashed).
rem It starts with a full wallet, so cars and shops can be bought and tested.
rem Add -cheat allcars,clothes,buyable,houses,allroads to start with the retail
rem dev menu's delock rows applied instead (every car owned, every car buyable,
rem every road explored, the clothes and the houses) - see docs/re/cheats.md.
rem Drop -money to start from the profile's own wallet.
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm freeride -skipcine -storytime 86400 -cheatspanel -money 5000000
