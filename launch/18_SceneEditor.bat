@echo off
rem Scene editor (assets, placement, detach, roads; -editorport for the AI driver)
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm sceneeditor
