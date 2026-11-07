@echo off
rem World viewer at the casino (fly through the interior, no car)
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm worldviewer -island casino -spot ECAS
