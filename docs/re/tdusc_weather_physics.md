# TDUSC weather + physics data (session 59, after the sound banks)

Tags: `[DATA]` from the owner's own TDUSC install (Steam, E:), `[INF]` inferred. Tools used read-only:
`tools/tdusc/pkg_scan.py --toc` (all 23 parts, 154,843 entries -> `E:/TDUSC Rips/pkg/scan-out/`) and
`tools/tdusc/tdusc_read.py` (export). Artifacts land in `E:/TDUSC Rips/dig/`.

## 1. Where things are [DATA]

The PKGs hold only art + engine assets (extensions: ptx/pma/psi/p3d/paclog/pan/plm/psvo/psk/pfx/pmc).
`Common/` is DUMMY stubs except two readable Lua FFB configs. `TDUSC.exe` (143 MB) is fully packed — no
plaintext strings at all (Gearbox/TorqueCurve/Suspension/WeatherType/TimeCycle: 0 hits). Per-tile world =
`WIN32/LEVELS/HONGKONG/TILE_Xnn/TILE_Xnn_Yyy/HONGKONG.PWS` (baked scene only).

**Car handling data (engine curves, gear ratios, tyre model) is not in any file we can read** — PKG TOCs,
PLMs, PSIs and PMCs checked; it must live in the packed exe or come from the servers (TDUSC is
always-online). Ruled out, don't look again without unpacking the exe.

## 2. `.PMC` — PhysX contact materials [DATA]

42 files in `WIN32/PHYSICALMATERIAL/` (+ a camera-collision copy set in `WIN32/ENGINE/PHYSICMATERIAL/`).
XMB-family container: `"UNDEFINDEFENDI" u32 3, u32 0 x3, u32 2, "PC", u32 2`, then 5 f32 + optional u32
flag, `u32 len` + `"$CCAM$CBLOCK$NAME"`, `0xABCD`. Everything is 74-80 bytes.

Field 1 varies per surface, fields 2-3 are 0.5/0.5 for surfaces (0.01/0.1 for the `META` stub):

| material | f1 | f2 | f3 |
|---|---|---|---|
| TARMAC (world copy) | 0.45 | 0.5 | 0.5 |
| GRASS / WETGRASS / ROCK / METAL | 0.45 | 0.5 | 0.5 |
| CARPET / GRAVEL / GRASS2 / TARMAC(ENGINE copy) | 0.05 | 0.5 | 0.5 |
| **WETTARMAC / WETTARMAC3 / WETWOOD / ICE / WATER / DIRT / SAND / MOSS / PAVE / WOOD / GLASS ...** | **0.0** | 0.5 | 0.5 |
| RUBBER | 0.05 | 0.01 | 0.9 (+u32 3, u32 1) |
| META (VEHICLE.PMC) | 0.001 | 0.01 | 0.1 (+u32 1) |

`[INF]` field 1 reads as a grip/friction scale (wet surfaces and ice = 0 — the engine scales tyre grip
down to nothing on wet tarmac, so wet grip lives in the tyre model, not the contact material). Wet variants
exist for TARMAC/GRASS/WOOD only; RUBBER is the odd one out (0.9 restitution — probably tyre-vs-ground bounciness).

## 3. `.PLM` — property-bag levels; the whole weather model is in the template [DATA]

PLMB files (`"PLMB" u32 0x13 ...`) serialize class names (`PlayAll::*`), NUL-terminated property names and
**text values** — either scalars (`"12"`, `"30"`), vectors (`"0.0000;0.0000;0.9200"`) or hour-keyed
animation tracks `[hour=[value];hour=[value]]` (`[SA;[min;max];v]` = sample-track entry).
`C:\temp\plm_dump.py` walks them into readable text.

Sweep of all 1,855 PLMs (`E:/TDUSC Rips/dig/plm_weather.csv`): exactly **one** weather block — in the
template `WIN32/ENGINE/LEVELS/TILED_LEVEL/TILED_LEVEL.PLM` (13.6 KB). No per-event weather on disk; the
runtime picks weather server/engine-side and drives these parameters. Clean dump:
`E:/TDUSC Rips/dig/TILED_LEVEL_timecycle.txt`.

### The TimeCycle (one node per element, all hour-keyed 0-24)

* `TimeCycleManager`: `CycleBlend`, `Default Hour` = 12, `Default Minute`, `Current/Next Cycle` — a
  cycle-crossfading manager, not a static skybox.
* `TimeCycleElementSun`: sun color keyframed at 5.65/6.5/6.7/19.7/19.9 h (night `107;144;255` ->
  day white), intensity ramps 0.5 -> **100** -> 0.5 across sunrise/sunset, god rays, lens flare color/scale,
  and a full **Polar Coordinates sun-position track** (elevation/azimuth per hour, 24 keys).
* `TimeCycleElementSky`: **Nubis-style volumetric clouds** — two cloud layers + cirrus, each with
  Coverage/Density/High+Low noise Intensity+Scale/LUT Factor, curl noise (scale 0.01), Mie anisotropy 0.8,
  Mie absorption `147;147;147` (scale 0.00769031), eccentricity 0.6, `Rainy Cloud`, cloud shadow bias/intensity,
  `Noise Speed`/`Speed`, zenith/horizon colors, sky alpha keyed to sunrise/sunset. Sky constants:
  `EarthRadius 6000`, `RadiusEarth 6e6`, `TopCloud 8000`, `DownCloud 1500`, `SizeUV 240000`,
  `AtmosphereThickness`, `AngularDiameter`.
* `TimeCycleElementFog`: Froxel fog/aerial perspective — Fog Color (night `8;96;255`), Fog Density 0 default,
  noise amount 0.4/scale 0.1, `Noise Wind Multiplier`, omni/spot fadeout distances (20000), second height
  density/falloff layer.
* `TimeCycleElementWeather` — the rain state:
  `Weather Type` = `rain` (string enum), `Rain Intensity`, `Puddles Intensity`, `Wet surface Intensity`,
  `Randomized Wind`, `Wind Influence` = 20, `Heavy rain distance` = 50, plus rain-particle config:
  speed min-max 45, width 0.05, length 3, alpha 0.2, `Spawn Rate`/`Particle Count` unset (engine).
* `TimeCycleElementWind`: full gust model — `Amplitude##B/F/T` (back/forward/top), `Amplitude during Gust`,
  `Direction` = (1;0;0), `Duration (s)##G`, `Fade In/Out (s)##G` = 1, `Interval (s)##G`, `Period (s)##B/F/T`,
  `Strength` — a procedural wind with gust envelope, per-axis.
* Also `Moon` (intensity/size), HDR, bloom (lens dirt), color grading (lift/gain/gamma/sat per tonal band),
  ambient cube (`Sky GI Multiplier`, `Car Reflection Multiplier`).

### Weather hooks on world objects [DATA]

* Terrain layers (`TerrainLayerComponent`): per-layer `Physics Material` (-> a .PMC) and **`Wet Absorption`**
  (how much water the ground soaks up — feeds puddle/wetness accumulation).
* Prop/mesh instances: **`Wet visibility factor`** (surfaces look different when wet), alongside
  MaxVisibleDistance.
* `RainOcclusionBox` components (9) — boxes that keep rain off interiors.
* Materials reference `SURFACEWEATHERING` PMA variants (dry vs wet-shader material pairs per terrain material).

## 4. `.PFX` — compiled effects [DATA]

Not config: each PFX is an XMB-family wrapper (`UNDEFINDENDI`) around **precompiled DXIL shaders** with
source-path strings (`E:\DEV2\TDUSC\Data\Assets\ENGINE\EFFECTS\LIGHTING\FOG\FOG.CG`,
`Lighting/Sky/AtmosphericSkyCommon.cg` — KT's `.cg` shader sources) and constant-buffer layouts.
`FOGDESC.PFX` declares `cbAtmosphericSkyParam`, `cbFroxelParam` (froxel volumetrics confirmed at the
shader level), plus `RAIN.PFX`, `VOLUMETRICCLOUDS.PFX`, `WINDSCREENWATERDROP(V2).PFX`.
`[INF]` disassembling the DXIL would recover the actual fog/cloud model constants if we ever want them.

## 5. Prop physics (no cars) [DATA]

PLMs carry `TDURigidBody` / `TDUBreakableRigidBody` / `MeshCollider` / `BoxCollider` (261/378/… instances)
with `AngularDamping`, `LinearDamping`, `CenterOfMassOffset`, `Child Mass`, `Break Speed (km/h)` = 30
(cones/signs), `Slow Down Factor`, physics-material refs. `PlayAll::GameServices::PhysicsSettings` exists
once (in the template). FFB Lua (`Common/Settings/Input{Hardware,Software}FFBConfig.cfg`): per-wheel-brand
force factors; software side collision vibration thresholds (`CollisionMaxImpactForce` 15000,
`CollisionMaxImpactMass` 300000, `FrictionMin/Max` 650/1200, flat-tire/ground-surface effects).

## 6. What we can reuse for OpenTDU2

1. The hour-keyed TimeCycle template = a ready-made weather/sky reference table (sun color+position per
   hour, cloud layer params, fog, rain/wind/puddles state machine) — matches our `eden_renderer.cpp`
   weather cycle work; the hour-keyframe format is trivially portable.
2. The wet-surface model: per-surface contact materials with dry/wet pairs + per-terrain-layer
   `Wet Absorption` + per-object `Wet visibility factor` + `RainOcclusionBox` — a clean checklist for
   our puddle/wetness pass (`-nopuddles` etc.).
3. Wind as a procedural gust model (per-axis amplitude/period/interval + fades) rather than a constant.
4. Prop physics damps/break speeds if we ever want TDUSC-style breakable props.

### 6.1 Ported (session 59, same day)

* **Second cloud layer + cirrus** into `EdenClouds` (`resources/shaders/volclouds.frag`,
  `volclouds_cirrus.frag`): the second slab derives from the main deck (+800 m above its top, sparser
  threshold, flatter noise) exactly because the template keeps `Coverage##CloudCoverageSecondLayer` at
  defaults; cirrus is a 2D band at 6500-7200 m like the template's 2D `CLOUDCIRRUS` texture, with the
  `NewCirrusCoverage` 0.3/0.6/0.1 scale and the `Rainy Cloud` darkness coupling. Forward HG lobe at
  TDUSC's Mie Anisotropy 0.8. Opt-outs `-nosecondlayer` / `-nocirrus`.
* **Height + noise fog** (`resources/shaders/tdfog.frag`, `EdenSceneView::applyHeightFog`): the
  `TimeCycleElementFog` second height layer (falloff 0.02 per metre) + fog noise (amount 0.4) drifting
  with the cloud wind, drawn over the finished image before the upscalers; amount driven by the retail
  `Skydome_haze` key. Opt-out `-notdfog`.
* Follow-ups from the same data: hour-keyed sun track, the `TimeCycleElementWind` gust model,
  wet-absorption puddle accumulation.
