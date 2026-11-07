# Units, odometer, the in-game clock and the weather — and what a seasonal event needs

Session 2026-11-01 (`cline`). Three of the owner's asks, one file: the
metric / imperial readouts, the odometer that read wrong, and the weather that
never changed (no cloud, no rain). The last section is the honest answer to the
snow / Christmas / fireworks idea.

--------------------------------------------------------------------------------
## 1. Metric vs imperial

The retail game ships **both** unit systems in its own data:

* GAUGES has `/:SpeedUnit` (the unit table of FUN_00965810) and **two** needle
  tables per car bank, `interior_kmh` and `interior_mph` (California: km/h
  0..320 mapped to 0..308 degrees, mph 0..210 mapped to 0..308 —
  `docs/re/freeride_spec.md` 4b).
* The per-frame snapshot converts with `v * 3.6` (km/h) or `v * 2.2374` (mph)
  and writes the digits, `/:Km` and `/:Centaine`.
* DASHBRD carries its own `/:Distance`.

Which one is used is not a menu row in the readable code; the retail build picks
it from the game's language profile [INF: US / UK builds show miles]. The port
therefore makes it a setting:

| | |
|---|---|
| Storage | `GameOptions::Units` (0 metric, 1 imperial), `<userdata>/options.ini` key `units` |
| Command line | `-imperial` / `-metric` (not saved: a test run must not rewrite the player's pick) |
| In game | **Pause menu → Options → Audio & Video → Video (or Controls) → Y / Triangle → Advanced settings → Speed units** (the native row added in session 76; left / right flips it, applied live, saved when the page closes like its other rows); also the cheats panel (F4) row **mph / miles** and F2 for the same page |
| Code | `source/game/units.h` / `units.cpp` — every readout goes through it |

The retail options movies have fixed rows and no readable unit row [INF: the
pick is the language profile], so the row lives in the port's **native
Advanced settings page** (`source/game/advanced_graphics.cpp`), the page the
pause menu opens with Y / Triangle from the Video or Controls page. It is row 2
(`kUnitsRow`) and excluded from the quality-profile reset. It writes
`options.Units` live and logs the change; `AdvancedGraphics::close()` saves the
file like it does for every other row of the page (saving from the row itself
would also persist retail page rows the player has not confirmed yet). The
retail page's **Cancel** keeps the unit (`pause_menu.cpp`: the three
`options = optionsBackup` restore sites preserve `Units`), because the native
page has already written it; without that the live readouts went back to metric
while `options.ini` stayed imperial.

Applied to:

* the GAUGES movie: the speed digits (`SpeedForDisplay`), `/:SpeedUnit` (now set
  at all — `"km/h"` / `"mph"`), `/:Km` + `/:Centaine` (miles and tenths of a
  mile in imperial);
* the port's placeholder gauge (`drawGauge`, drawn when the car's gauge bank is
  missing): digits, the unit under them, the odometer;
* the GPS route line and the wreck detector line under the minimap
  (`FormatDistance`: `850 m` / `12.3 km` / `2,800 ft` / `7.6 mi`);
* the fuel bar's range (`%.0f L  <distance>`).

**Not converted (on purpose, next step):** the tank itself stays in litres and
the consumption in l/100 km — an imperial build would show US gallons and mpg;
the DASHBRD movie is not rendered by the port at all (it needs the cockpit
render target, `docs/re/freeride_spec.md` 4b).

--------------------------------------------------------------------------------
## 2. The odometer

Old: `odometer += velocity.length() * h` per 1/90 s physics tick
(`vehicle_dynamics.cpp`, tick end). That is the *speed of the body through
space*, so it counted

* the vertical bounce of a bump and the drop of a jump,
* the sideways slide of a spin in place (handbrake turns added "distance"),
* the body's motion while airborne (a real odometer stops when the wheels do),
* and any chassis depenetration push the collision solver applied.

New (this session):

```cpp
const Vector3f travelled = position - tickStartPosition;
const float ground = sqrt( travelled.x^2 + travelled.z^2 );
if ( ground > 0.0f && ground < kMaxOdometerStep ) odometer += ground;  // 5 m / tick
```

i.e. the **horizontal distance the car actually covered**, which is what a
wheel-based counter measures, with a 5 m-per-tick (450 m/s) guard so a teleport,
a spawn or a `reset` cannot book a jump of kilometres.

The value is still **per session** (the `VehicleDynamics` object). The retail
profile keeps a per-car `Km` (`OwnedCar::Km`, `docs/re/savegame_spec.md`, the
"km" of the FUN_00abffd0 car record), which would be the natural place to
persist it — not done yet (it needs the profile's car-record wiring in
`gm_freeride` / the garage, and a decision on which car record the driven car
maps to).
--------------------------------------------------------------------------------
## 3. The in-game clock and the weather

### 3.1 What the data actually has

`euro/bnk/islands/ibiza/fx/game_weathers.xmb` (978 KB) is **not** just a list of
times of day — that is only the outer records. Each of the 24 weathers ("00",
"01.30", … "23.20") carries **7 intemperies**:

| intempery | parameters | what it is |
|---|---|---|
| 0 | 16 | a short base block (tunnel / headlight values only) |
| 1 | 152 | the full dry set (`rain_ground_*` all 0) |
| 2 | 152 | the second dry set (own `clouds_cloud3dname`, different fog / sun) |
| 3 | 152 | **rain**: `rain_ground_global_intensity 1`, `_wet 0.6`, `_puddle_height 0.62`, ripples |
| 4 | 152 | drying: wet 0, ripples still on |
| 5 | 152 | **rain**: wet 0.7, `_ripple_scale 0.1`, own `clouds_cloud3dname` |
| 6 | 152 | **rain**: wet 0.6, the splashes (`rain_splash_max`, `rain_droplets_max`) |

Verified with a scratch dump of the file (`_wx_dump.py`, mirrors `XmbDocument` /
`eden_weather.cpp`); the parameter names are also the ones the renderer already
reads: `sundirection`, `sun_diffuse/specular/ambient`, `fog_color/start/end/max`,
`Skydome_*`, `clouds_cloud3dname / _color / _color2 / _cloud3dAlphaBlend`,
`ocean_*`, `rain_ground_*` (the wet ground of `EdenWorld::updateWetGround`),
`photoshop_*` (the post pass).

`eden_weather.cpp` read **intemperies 0 and 1 only**
(`j < numIntemperies && j < 2`), so the port's weather was always the dry set: no
rain state was ever applied, no wet ground, no puddles, and the cloud name never
changed. That is the "I never saw a cloud or rain" answer.

### 3.2 What this session changed

* `WeatherLibrary` now keeps **every** intempery: `presets` still holds the old
  merge (base + set 1), so the title screen, the world viewer and the test drive
  look exactly as before, and a new `variants` list holds each full set
  (`findVariant(name, n)`, `getIntemperyCount(name)`).
* New `source/game/freeride/weather_cycle.{h,cpp}` — the in-game clock and the
  weather state machine:

| | |
|---|---|
| Clock | the machine's local time at start (`-clocktime HH:MM` to start elsewhere), 1 game second per real second (`-clockrate x`), wraps at midnight |
| Time preset | the `game_weathers` entry nearest **below** the clock (a file without "00" wraps to its last entry); the sun, the ambient, the fog and the clouds follow it |
| Weather state | one intempery at a time, at least `kFirstIntempery` (1). Dry sets last 8-16 game minutes, cloud 4-9, rain / storm 2-6; a wet state decays through cloud before it clears (weights and durations `[INF]`: the retail scheduler is not decoded) |
| Applied | `pWeather` is re-pointed and every consumer follows it — `view.setWeather` (called every frame), the car / traffic light drivers (night headlights read `pWeather->Hour`), `EdenWorld::updateWetGround` (rain / puddles / ripples), `EdenSceneView::drawClouds` (the cloud subtree per intempery) |
| Clock display | the pause screen clock (`PauseMenu::setClock`) is re-sent when its minute moves |
| Log | `WeatherCycle: clock HH:MM, weather 'NN' intempery N` at load, `FREERIDE: clock …, weather '…' intempery N (rain x.xx)` on every change, `WeatherCycle: weather state a -> b for M min` |
| Pinned | `-weather <name>` (the launch bats) and `-night` (darkest time) as before; `-intempery n` picks the variant. The cheats panel's weather combo pins too |

**Falling rain itself is still not drawn**: the rain *drops* / splashes are shader
work (`rain_droplets_*`, `rain_splash_*`, `rain_tiling`) that the port never
implemented, and there is no particle layer. What the rain states do give is the
wet road, the puddles, the ripples and the darker sky / clouds — i.e. a
recognisably rainy world, just without drops in the air.


--------------------------------------------------------------------------------
## 4. Seasonal events: snow, Christmas, fireworks — what it would take

Short answer: **the calendar and the scheduling are cheap; the visible parts are
three separate pieces of engine work that do not exist yet.** Nothing in the
game data supports them — there is no `snow_*` parameter anywhere in
`game_weathers.xmb` / `cinematic_weathers.xmb` (Ibiza and Hawai never snow in
retail) and the port has no particle layer and no port-native prop mesh path. So
these are features the port would *add*, not data it would switch on.

### 4.1 The calendar (small, no blockers)

`WeatherCycle` already owns a time of day. The next step is a date beside it:
`std::chrono` local date at start, `-date YYYY-MM-DD` for a test, and a tiny
`SeasonalCalendar` that answers `isDecember()`, `isChristmasEve/Day()`,
`isFireworksNight()` (5 November), `isHalloween()` (31 October). That part is
half a session and unblocks all of the scheduling below.

### 4.2 Snow on the ground + tyre tracks **[NEW renderer work]**

* **Ground cover.** There is no snow layer. The cheapest honest path copies the
  mechanism that already works: `EdenWorld::updateWetGround` swaps a PUDDLES
  texture slot and writes `RainSpecs` / `RainRippleSpecs` into every road
  material of the loaded sectors, and it already handles on/off transitions. A
  snow version would (a) add a `SNOW` parameter + a white blend / normal in the
  TD_HeightMap and road shader permutations, or (b) a separate thin "snow shell"
  mesh over the road triangles of the loaded sectors. (a) is the better look and
  is one session of shader + material work; (b) avoids shaders but pops at
  sector borders.
* **Tyre tracks.** Needs a persistent decal trail: spawn one blended quad under
  each wheel every few metres, keep them in a per-sector ring buffer, fade them
  out after N seconds. The road-decal blend mode already exists
  (`eden_material_handlers.cpp`, "Road decals: blended on the road, no depth
  write"), so the missing part is the owner (spawn / recycle / stream) — one
  session, and it is the same system that would give skid marks on tarmac.
* **Only while it snows.** Snow that "lasts a few days" means the state is
  stored, not just drawn: a `SnowDepth` per island in the profile, grown while
  the snow state runs and decayed over game days — cheap once the two above
  exist.

### 4.3 Christmas decorations and lights **[blocked on props]**

The port places *game* banks (`mod_props`, `spot_exteriors`); it cannot author a
mesh yet (README "Modding": no OBJ/glTF path, no BNK packer, no `.3dd`
exporter). So a Christmas tree / strings of lights would need either

* an existing game prop reused (the `scenarics` packs, the street-lamp models —
  `fx/streetlamps.xml` is in the data), placed at fixed spots in December, or
* the port-native OBJ prop renderer first (that is a session of its own and
  unlocks user mods too).

The *lights* half is cheaper than the meshes: the car-light driver already swaps
`GLOSSFCT` channels and emissive parameters on named materials, so "the street
lamps are warm white / coloured in December, on all night" is a material
parameter override driven by the calendar — no new geometry.

### 4.4 Fireworks on 5 November **[NEW particle layer]**

Nothing in the port draws particles (`.prt` files are not parsed, there is no
billboard system). The smallest useful version is a CPU particle pool that draws
camera-facing additive quads (a built-in shader + one vertex buffer): spawn a
burst at a fixed world point every few seconds between 20:00 and 23:00 on 5 Nov,
each particle a short-lived streak with gravity and a fade. That is one session
for the layer, and the layer is then reusable for exhaust smoke, dust and the
rain *drops* that are still missing (§3.2).

### 4.5 Recommended order

1. Calendar + the `-date` test hook (half a session).
2. The particle layer (fireworks, rain drops, dust) — one session.
3. Snow cover via the wet-ground pattern (shader + material) — one session.
4. Tyre tracks / skid marks (the decal trail) — one session.
5. Christmas: calendar-driven lamp material override first, then props once the
   OBJ path exists.

Halloween cosmetics ride on 5 (it is the same prop + material work, only the art
differs).

