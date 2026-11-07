@echo off
rem World viewer (free fly camera; F1 panel: islands, spots, weather)
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm worldviewer
