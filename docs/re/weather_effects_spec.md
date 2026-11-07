# Weather effects — falling rain, rain on the windscreen, the wipers, puddles, snow

Session 2026-11-01 (`glm`). The owner's ask, quoted:

> *"when raining or snowing (Might not be fully working yet) but have some of the rain and snow land on
> window and have window wipers working which should clear the rain drops or snow so window is visible?
> ... and have puddles etc on road form and dry up?"*

Everything below is live. Half of it is the retail system read out of the data and the readable exe /
PS3 build; the other half is port-invented where the retail game shipped nothing — each piece says
which. Builds on `rain_shadow_spec.md` (the wet road / droplets RE), `rain_spec.md` (upstream's rain
notes, `upstream/main`) and `surface_effects_spec.md` (the wetness model this feeds).

--------------------------------------------------------------------------------
## 1. The weather keys that gate everything

`game_weathers.xmb` holds each time-of-day preset several times over — one base block (clear) plus
**intemperies** (weather states). The port's loader merged only blocks 0+1 (clear + cloudy), so no
`rain_*` value was ever reachable and nothing in the port could rain. Now every block parses (0+1 keep
merging into the base map, 2..N each into their own overlay) and `WeatherPreset::get` resolves the
active overlay over the base; `getFloat`/`getVector3`/`getVector4` route through it. `-intempery <n>`
pins one block over whichever preset is picked (load pick and the cheats weather switch; the log line
prints the resolved rain keys). Ibiza preset "14" measured: **#3 is full rain** (`rain_alpha` 120/255,
`rain_ground_global_intensity` 1.0, `rain_droplets_max` 10), #5 pre-wets the ground with no visible
rain, #6 rains again, 2/4 are the drying states. `-intempery 3` is the rain test switch.

| key | drives |
|---|---|
| `rain_alpha` | the falling rain (blocks > 0.001, drops > 1e-4) and the droplet alpha law |
| `rain_tiling`, `rain_scroll_speed` | the rain-block texture |
| `rain_splash_max` / `_alpha` | the ground splashes |
| `rain_droplets_max` / `_life_time` / `_spawn_freq` / `_refraction_oncam` | the windscreen droplets |
| `rain_ground_global_intensity` / `_wet` / `_puddle_height` / `_ripple_*` | the wet roads (retail path, below) |

## 2. Falling rain — retail (`docs/re/rain_spec.md`, upstream's `EdenRain` ported)

The retail weather-FX manager draws three objects; the port now draws all three (ported from
`upstream/main`'s `source/render/eden/eden_rain.{h,cpp}`, rewired to this fork):

* **WFX_Rain**: 9 blocks of 75 m crossing quads on a 3x3 grid around the camera, `RAIN_FAR` scrolling
  down, material `RainGrp` TD_Rain, colour (200,200,200,(int)rain_alpha). The car-speed slant factor
  `DAT_0117ac84` has no readable writer → 0 (no streak slant), as upstream.
* **WFX_Drop**: 300 world-fixed 1.5 m streaks in a 40 m box 12 m ahead of the camera, `DROP` texture,
  TD_Gouraud.
* **WFX_Splash**: up to `rain_splash_max` ground splashes in 15 zones of a 3x5 grid, 9-frame
  `SPLASH` atlas.
* The rain **sound** (ambiences.xmb Rain[0], Rain_Mid.wav, client 13, volume = curve(rain_alpha))
  came with the port.

`-rain` forces it on a dry sky (the rainy intempery's rain_* / thunder_* keys overlay the picked
preset — the lighting stays); `-norain` turns the falling rain off while the wet ground keeps the same
resolved values. One activation log line (`EdenRain: raining (...)`).

## 3. Rain on the windscreen — retail effect, port tuning

The retail rain-on-glass is a **camera-space droplet** effect (`rain_shadow_spec.md` A.6):
`FUN_00aa2100` renders the droplet simulation into an offscreen RT (named `Droplets_Windshield` among
`Droplets_OnCam / _FBCopy`) and a `TD_Droplets_Oncam` material refracts the frame with it. The port:

* `EdenWindscreen` (new): a uv-space droplet sim — the retail alpha law
  `life/lifeTime · clamp(2·rain_alpha/255, 0, 1)`, drops stick then run down with a wobble, life
  `rain_droplets_life_time` — rendered as normal.xy + presence quads into a half-res
  `Droplets_Windshield` RT; the final blit (`windscreen.frag`, the upscale shader + the droplet
  layer) refracts the scene along the drop normals, the lens rims glint, and a **wet film** fogs the
  glass (the film is what the wiper clears). Gated on the inside views
  (`ChaseCamera::isInsideView`, near < 0.5).
* The retail cap is 10 live droplets at freq 0.15/s — an equilibrium of ~0 visible. `-dropletscale`
  (default 30) scales the cap and drives the spawn rate to approach it [INF: the retail rate law is
  not readable]; `-droplets 0` off, `-showdroplets` shows the RT raw, `-nodroplets` skips the module.
* **The glass swap**: the retail inside view swaps the car `Glass` material for the interior bank's
  `Glass_I` (TD_CarGlassDirt; `california_spec.md` §4, FUN_00c53d10 on the camera flag 0x40000).
  Implemented in `CinematicCar::draw` (`applyGlassSwap`) — the session-45 smoked glass is gone from
  the driver seat. A/B: `-keepglass`.

## 4. The wipers — port-invented driver over retail geometry

**The cars DO ship wiper geometry** (the owner was right): every car bakes its parked blades into the
body as one wide thin strip prim at the glass base (the Delta: a 1.88 x 0.11 x 0.05 m `Mirrors` prim
inside BODY_MR; the washer jets are separate `SPRG` nodes with the `WS_SPRG` material — both present
in the PC banks and the x360 retail bigfile). What the data lacks is any pivot node, animation or
driver: the retail feature is the camera droplets only, blades parked forever.

The port's driver (`CinematicCar`, session 65):

* `collectWipers` at car load (`setKeepItemIndices` keeps each item's primitive indices + vertex
  positions): the wide thin strip high on the body splits at x = 0 into the two blades, each half
  uploaded as its own index list over the item's vertex streams. At rest angle 0 the halves **are**
  the parked art.
* `updateWipers`: auto mode sweeps while it rains (1.1 s sine sweep to 89°, interval 7 s drizzle →
  1.2 s downpour [INF rates]); `fast` sweeps continuously; `off` parks. **F6** cycles, `-wipers
  off|on|fast` picks the start mode.
* `draw`: while swinging, the parked prim hides (new `EdenDrawParams::ForceHiddenItems`) and the
  halves re-draw rotated about pivots at their outer ends (axis = the Delta's 34° windscreen rake
  [INF: all cars]).
* The sweep **clears the glass**: the Opaque-pass callback projects each blade's pivot→tip segment to
  the screen uv and `EdenWindscreen::wipe` kills the drops near it and resets the film (it re-soaks).

## 5. Puddles that form and dry

* **Roads — retail, verified**: `EdenWorld::updateWetGround` (the `RainSpecs` path) now receives real
  rain values through the resolved preset; with `-intempery 3` the TD_Road materials switch to the
  rain permutation (`PUDDLES` texture + env-cube mirror + darkening, `rain_shadow_spec.md` A.4) and
  the tarmac draws dark, glossy and sky-reflecting.
* **Terrain — port-native [INF]** (the retail `TD_HeightMap` carries no rain constants at all):
  `EdenSurfaceFx::updatePuddles` samples a 5x5 grid of 5 m cells around the car every half second
  through the carve-aware `getHeight`; cells level with their neighbours within 8 cm are flat ground
  and pool (jittered, 1.4-3.2 m, corner heights sampled so the quad follows the slope). They draw
  through the existing decal pipeline (soft-shouldered quads, bluish water, alpha following the
  tarmac wetness) and live only while it is wet: rain spawns them, the drying states kill every one
  as the wetness falls past 0.25 (the retail road puddles switch off the same way — RainSpecs'
  intensity gates their permutation). Detection history: local-minimum rules never fired — the
  heightfield is so smooth the dealer delta measures a pure monotone 5.94..6.08 gradient. `-nopupdles`
  off, the count in the `-logsurfacefx` line.

## 6. Snow — port-invented [INF]

The retail data has **no snow at all**: no snow intempery (the six are Sunny / Cloudy / Rainny /
Before_rain / After_rain / Tempest), no snow textures (Ibiza and Oahu ship). `-snow` turns the falling
rain into a snow presentation: the drops fall at a third of the speed and draw as short white flakes
(0.25 m) instead of streaks, the ground splashes are off, and it enables the falling rain on a dry
preset (the rainy intempery feeds the values). The windscreen layer and the wipers carry over
unchanged. The `WFX_Rain` blocks keep their streaky `RAIN_FAR` art behind the flakes (documented
limit); the terrain does not whiten (no such terrain parameter exists).

## 7. Flags

| flag | what |
|---|---|
| `-intempery <n>` | pin one intempery block over the picked weather preset (3 = rain on Ibiza's presets) |
| `-rain` / `-norain` | force the falling rain on a dry sky / turn the falling rain off |
| `-snow` | the invented snow presentation |
| `-droplets 0` / `-nodroplets` | windscreen droplets off / the module skipped |
| `-dropletscale <n>` | droplet cap + spawn multiplier (default 30 [INF]; 1 = the retail A/B) |
| `-showdroplets` | show the Droplets_Windshield RT raw |
| `-wipers off\|on\|fast` / **F6** | the wiper mode / cycle it |
| `-keepglass` | A/B: keep the outside glass material in the inside views |
| `-nopupdles` | terrain puddles off |
| `-cameramode <n>` | start in one of the car's camera views (23 driver, 24 bumper, 20 chase) |

## 8. Not done — the honest list

* The wiper sweep has no 3D pivot arm art (the halves are the retail blade strip rotated — from the
  driver seat a blade reads as the thin line a real one does; a box-extruded blade body would read
  better from glancing angles).
* The droplets are screen space (as the retail effect is): they show from the inside views only, and
  do not parallax with head movement.
* The rain-block slant (car speed) stays 0: `DAT_0117ac84` has no readable writer.
* The snow presentation does not whiten the terrain (no terrain parameter for it) and the rain-block
  art stays streaky behind the flakes.
* The Glass_I dirt strength is the retail art as-is; a wet-glass darkening term on top is future work.
* `HardTop.anm` cars (the skinned roof path) have no draw params — their wipers stay parked while the
  roof path draws.
