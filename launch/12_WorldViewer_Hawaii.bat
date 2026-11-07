@echo off
rem World viewer at the Hawaii tuning shop ETUN_G
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm worldviewer -island hawai -spot ETUN_G
