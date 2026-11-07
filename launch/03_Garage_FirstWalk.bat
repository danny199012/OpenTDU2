@echo off
rem Tess's garage (first walk mode): the California, roof, cockpit
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm FIRST_WM
