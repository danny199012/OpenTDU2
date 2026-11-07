@echo off
rem Free roam Hawaii (Oahu) by a house spot ECH_HA
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm freeride -island hawai -tp 11241.4,32482.8 -heading 237 -storytime 86400
