@echo off
rem Asset viewer (ASSET_VIEWER): browse the game's banks, see every asset's
rem original authoring path, search them all, preview textures / models /
rem movies / sounds, and write labels.
rem The labels go to docs/re/asset_labels.txt; -labellint checks them.
rem The user data folder is workspace neutral (it also caches the asset index
rem built by -buildindex, so one build serves every tree).
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-userdata -gm assetviewer
