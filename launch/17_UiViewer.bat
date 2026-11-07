@echo off
rem UI viewer (UI_VIEWER): the frontend flash movies as data. Every element of
rem a movie's display list with its path, type, character, frame, visibility and
rem bounds; the movie drawn live beside it; a click selects the element under
rem the mouse. The names the owner writes go to docs/re/ui_elements.txt, keyed
rem on <movie> + <clip path>; -lintui checks them.
rem
rem Panels: movies (left top), display list (left bottom), element + movie
rem (right). -goto "/pause_menu:pause_stop" forces a menu state before a shot,
rem -dumpui logs the whole display list and exits, -movie hud opens another one.
rem The user data folder is workspace neutral, like the other tool launchers.
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-userdata -gm uiviewer
