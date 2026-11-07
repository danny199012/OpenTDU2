@echo off
rem Free roam Ibiza from the house street (story timer disabled for touring)
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm freeride -skipcine -storytime 86400
