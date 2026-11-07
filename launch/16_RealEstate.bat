@echo off
rem Free roam at the Ibiza real-estate agency (spot ERE_B_3336I). Drive up to
rem the building, stop (under 20 km/h), Enter to walk in, aim at the realtor
rem behind the desk and Enter again: the house menu (buy / sell). A bought
rem house is yours: its map icon turns into the home one, the Enter tile at it
rem walks you in (the living room, the garage door leads to the garage with
rem your cars). -home spawns at your home house's gate instead of the story.
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm freeride -tp 21071,23472 -snaproad 60 -storytime 86400 -money 2000000
