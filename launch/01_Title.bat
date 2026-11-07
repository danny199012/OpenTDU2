@echo off
rem Title / login screen
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2
