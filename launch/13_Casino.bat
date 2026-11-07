@echo off
rem The offline casino (GMCasino): slot machine, roulette, chip wallet.
rem -casinochips 5000 starts with a test balance; drop it to use the profile's.
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-cline-userdata -gm casino -casinochips 5000