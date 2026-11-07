@echo off
rem Free roam Ibiza at NIGHT: -night picks the darkest game_weathers preset
rem ("00"), so the position rings, the headlamps, the brake / reverse lights,
rem the indicators (in-car menu: D-pad left, then left / right; or
rem -indicator left|right|hazard) and the dashboard backlight are all
rem visible. -logcarlights writes the light state to the log once a second.
rem Force the lights with -carlights 0 (dark) / 1|2 (forced on); the retail
rem light driver (docs/re/car_lights_spec.md) switches the lamps' GLOSSFCT
rem channels, so by day every lamp is dark.
cd /d "%~dp0.."
call "%~dp0resolve_exe.bat" || exit /b 1
"%OTDU_EXE%" -game C:\TDU2-Gamedata\TestDrive2 -gm freeride -skipcine -storytime 86400 -night -logcarlights
