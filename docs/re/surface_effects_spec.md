# Surface effects — wheel spray, dust, tyre tracks and the wetness of the ground

Session 2026-11-01 (`cline`). The owner's ask, quoted:

> *"I was wondering if we can improve the water, sand, dirt etc... I believe it's just mostly a texture
> is it possible we can have a particle system made and we can leave decal trails etc... I was hoping
> when driving on sand, mud it would splash up a little bit under the wheels and make it more like a
> material so it can potentially be affected by rain and sun so mud would dry up and get wet?"*

Short answer: **yes, and the foundation is in.** The port now has the particle layer and the decal trail
that the earlier plans named as missing (`weather_clock_units_spec.md` §4.4, `docs/state/cline.md`
"Next"), plus a per-material wetness model that rain soaks and the sun dries. What is **not** done yet is
the half that changes the *terrain shader* itself (wet sand looking darker, snow cover): that is §5.

--------------------------------------------------------------------------------
## 1. What the original has, and what the port had

| Piece | Original | Port before this session |
|---|---|---|
| Ground materials | 3 tyre surfaces in the car data (`TyreSetup`: tarmac, dirt, grass, `vehicle_data.h`); sand / mud / snow are **textures** in the terrain colour maps (`plage.2db`, `D3_SAND`, the sector `-c` maps) | the same 3 surfaces in the physics; the terrain drawn with `TD_HeightMap` + the colour map |
| Wet road | `RainSpecs` / `RainRippleSpecs` + the PUDDLES / ENVMAP slots, switched by the weather preset (`EdenWorld::updateWetGround`, `rain_ground_*` parameters) | implemented, but **binary**: the preset switches it on and off in one frame |
| Particles | `.prt` files + a billboard system, not parsed by the port | **nothing** — no particle layer at all |
| Tyre tracks | a decal trail (the retail road-decal blend mode exists in `eden_material_handlers.cpp`) | nothing |

So the "material" the owner wants is exactly the missing middle: a port-side state for *what the ground
is* and *how wet it is*, which the effects and (later) the terrain shader both read.

--------------------------------------------------------------------------------
## 2. The ground materials and the wetness model

`source/render/eden/eden_surface_fx.h`

```cpp
enum class eGroundMaterial : uint8_t { Tarmac, Dirt, Sand, Grass, Snow, Count };

struct GroundWetness {
    float Value[Count];                            // 0 dry .. 1 soaked
    void update( float dt, float rain, float sunEnergy );
    float get( eGroundMaterial ) const;
    bool isMud( float threshold = 0.55f ) const;   // wet dirt
};
```

* **Rain** comes from the weather preset the scene is already lit with:
  `rain_ground_global_intensity` (the same parameter `EdenWorld::updateWetGround` uses for the wet road).
* **Sun** is the luminance of the preset's `sun_diffuse`.
* The law is a first-order soak and dry per material:

  ```
  w += ( 1 - w ) * soakRate * rain  * dt      // rain fills the ground
  w -= w * dryRate * ( 0.25 + 2 * sun ) * dt  // the sun (and a dry sky) empties it
  ```

  | material | soakRate | dryRate | reads as |
  |---|---|---|---|
  | tarmac | 0.90 | 0.030 | a road film, dries in about half a minute |
  | dirt | 0.35 | 0.006 | **mud**: soaks slowly, keeps its water ~3 min in full sun |
  | sand | 0.70 | 0.020 | damp beach |
  | grass | 0.45 | 0.012 | |
  | snow | 0.50 | 0.004 | melt |

  The rates are the port's own `[INF]`: the retail scheduler keeps no such state (its wet ground is a
  per-preset switch), so there is nothing to copy. The shape is what matters — dirt holds water, tarmac
  sheds it — and the table is in one place to tune.

Mud is **not** a separate material: it is dirt with `Wetness >= 0.55`, which the effects read to pick a
dark sticky splash and a treaded track instead of a pale dust cloud.

### Where the material of a contact comes from

`GMFreeRide::tickSurfaceFx` → `ClassifyGround()`:

1. `WheelState::Surface` (the car data's tyre surface: `Tarmac` / `Dirt` / `Grass`).
2. Grass (i.e. the terrain heightfield) **below 2.5 m above the sea** is the **beach**: `Sand`.
   The sand has no surface id of its own in the data — it is a texture — and the island's shoreline is
   the only place that low, so the height is the cheapest honest test `[INF]`.

--------------------------------------------------------------------------------
## 3. The particle layer and the decal trail

`EdenSurfaceFx` (`source/render/eden/eden_surface_fx.cpp`), owned by `GSRender` (`getSurfaceFx()`),
drawn by FREERIDE through the new `EdenSceneView::ePass::Effects` (inside the blended pass, after the
deferred blended draws and before the haze, so the haze and the post processing cover it like the rest
of the scene).

* **Particles**: a 3000-slot CPU pool, each one a camera-facing quad expanded in world space (the camera
  basis comes from the scene's view matrix), with gravity, drag, a size that grows as it ages and a
  quadratic alpha fade. Emission per wheel and per frame:

  ```
  rate = SplashRate * max( speed - MinSpeed, 0 ) * ( 1 + slip * SlipBoost ) * ( 1 + wetness * WetnessBoost )
  ```

  A dry road throws nothing (`rate *= wetness` for tarmac), sand throws the most, and the colour is the
  mix of the dry and the soaked colour by the wetness — dry sand is a pale cloud, mud is dark and heavy.
  Emission is capped at 24 per wheel per frame, so a teleport or a frame spike cannot empty the pool.
* **Tyre tracks**: a 1024-slot ring buffer of ground quads (position, surface normal, travel direction,
  half width / length, colour, tread amount, life). One is laid every `1 / DecalRate` metres the wheel
  rolls, lifted 3.5 cm along the normal (the depth test is `LESS_OR_EQUAL` and the terrain is drawn
  first), faded out over the last third of its life. Tarmac only keeps marks when it is wet or the tyre
  slips (skid marks) or is **braked hard** (§7); sand, mud and snow always take an imprint. Sand and mud
  differ by the `Tread` push constant (smooth vs lugs) and by the colour.
* **Shaders**: `resources/shaders/surfacefx.vert` (shared: `mat4 ViewProjection` + `vec4 Params`, both in
  push constants), `surfacefx.frag` (soft grainy puff) and `surfacedecal.frag` (shoulders + lugs + grain).
  Regenerate the embedded SPIR-V with
  `python tools/compile_shaders.py surfacefx.vert surfacefx.frag surfacedecal.frag` after editing them.
* **Geometry conventions**: `Matrix4x4f` is column major with `M * v`, which is exactly what GLSL's
  `mat4 * vec4` does, so `getViewProjection()` is uploaded as is and `Matrix4x4f::transformPoint` is the
  reference for the shader's transform.

--------------------------------------------------------------------------------
## 4. Verified (headless, this workspace)

| What | Command | Evidence |
|---|---|---|
| The pipelines come up | `... -gm FREERIDE -skipcine -nopad -no3d -logsurfacefx -exitframe 260` | `EdenSurfaceFx: ready (3000 particles, 1024 decals)` |
| Splashes draw under the wheels | `-surfacefxtest -logsurfacefx -autoplay "300:shot=.../_sfx.ppm" -exitframe 340` | `SurfaceFx: 193 particles`; `docs/images/surface_fx_splash.png` |
| Tracks draw behind the car | `-surfacefxtest -logsurfacefx -autoplay "40:hold=accel;330:release=accel;360:shot=..."` | `SurfaceFx: 183 particles, 543 decals, ground dirt`; `docs/images/surface_fx_tracks.png` |
| A dry road throws nothing, and a slipping tyre leaves a skid | `-logsurfacefx -autoplay "60:hold=accel;..."` on the plaza | `0 particles, 8 decals, ground tarmac` |
| `-nosurfacefx` really switches it off | `-nosurfacefx -surfacefxtest -logsurfacefx -autoplay "40:hold=accel"` | `0 particles, 0 decals, ground -` while driving |
| Rain soaks the ground | `-weather 14 -intempery 3 -logsurfacefx -exitframe 260` | tarmac `0.09 → 0.83`, dirt `0.03 → 0.54`, sand `0.76` in ~10 s |
| A dry sky dries it, mud last | `-surfacefxwet 100 -logsurfacefx -exitframe 900` | tarmac `1.00 → 0.66`, dirt `1.00 → 0.92 (mud)`, sand `0.75`, snow `0.95` |

Screenshots: `docs/images/surface_fx_splash.png`, `docs/images/surface_fx_tracks.png` (night preset, so
the spray reads bright against the road).

--------------------------------------------------------------------------------
## 5. Not done — the honest list, in the order it is worth doing

1. **The terrain shader does not see the wetness yet.** Wet sand / dark mud on the *ground itself* needs
   the `TD_HeightMap` permutation to take a `Wetness` parameter and darken + gloss the colour map, the
   way `RainSpecs` already does for the road materials. `GroundWetness` is the value to feed it
   (`EdenWorld::updateWetGround` is where that material loop already lives).
2. **Rain drops in the air** are still not drawn (`docs/state/cline.md` "Next"). The particle layer is
   now there: drops are a second emitter (a box around the camera, a short streak, no gravity) on the
   same pool — half a session.
3. **Snow cover** (`weather_clock_units_spec.md` §4.2): the same shader parameter as (1) with a white
   blend, plus the calendar. The track decals already have their snow profile.
4. **Puddle spray**: `rain_ground_puddle_height` of the preset is known; a wheel in a puddle could throw
   a much bigger, whiter splash. Cheap once (2) exists.
5. **The beach test is a height heuristic.** If a beach still sprays green, the next step is the sector
   colour map (`FindColorMap`) averaged per sector at load: sand is the pale warm band.
   `ClassifyGround()` in `gm_freeride.cpp` is the one place to change.
6. **The AI cars leave nothing** (`traffic.cpp` has its own transforms); they would need the same
   `emitWheel` calls from their own wheel state.

--------------------------------------------------------------------------------
## 6. Command line

| Flag | Meaning |
|---|---|
| `-nosurfacefx` | no spray, no tracks, no brake glow (the wetness model keeps running, so an A/B shows the ground only) |
| `-surfacefxscale <n>` | emission multiplier (0 none, 2 double) |
| `-surfacefxtest` | a synthetic contact under every wheel (front = sand, rear = dirt) at 12 m/s: proves the pipeline on any surface, headless |
| `-surfacefxwet <0..100>` | start the ground that wet (the drying law in a dry preset) |
| `-logsurfacefx` | a `SurfaceFx:` line when the pools or the wetness change, naming the classified ground |
| `-nobrakeheat` | the discs keep heating / cooling but no glow is drawn (the A/B of §7) |
| `-brakeheattest <0..100>` | start every disc that hot (0 = cold, 100 = 1000 C): the glow is visible from the first frame, no braking run needed |
| `-logbrakeheat` | a `BrakeHeat:` line per 20 C of the hottest disc: every wheel's temperature and glow |

--------------------------------------------------------------------------------
## 7. Hard braking — the black marks and the glowing discs

Session 2026-11-02 (`cline`). The owner:

> *"can we implement the feature while driving and braking hard causes tire marks and also causes
> break discs to light up from heat this was a feature in the original game"*

Both are **in**. The tyre marks reuse the decal trail of §3; the discs are new state and a new pass.

### 7.1 Why the slip ratio alone was not enough

`WheelState::SlipRatio` is what the ABS *holds down*: above the activation speed the controller trims
the pad position every tick to keep the slip near `CurveAbsControlSlipRatio`, so a full stop on dry
tarmac sits around 0.1..0.25 — under the 0.35 skid threshold of §3, and a hard stop left **no** mark on
a dry road. The brake's own work is the honest signal, so `WheelContact` gained the physics inputs
(`GMFreeRide::tickSurfaceFx` fills them):

```cpp
float BrakeTorque = 0.0f;  // N m, VehicleDynamics::getBrakeTorque(i)
float BrakeSlip   = 0.0f;  // 0..1, how much of the tyre's friction the brakes use
float WheelRadius = 0.34f; // m
bool  bFront      = true;  // the front discs are the bigger ones
```

```
brakeSlip = saturate( ( max( -LongitudinalForce, 0 ) / ( Load * SurfaceFriction ) - 0.45 ) / 0.40 )
```

i.e. the longitudinal force as a share of the friction limit: 45 % of it starts a mark, 85 % is a full
one. `VehicleDynamics::getBrakeTorque(i)` is new and returns the same per-axle limit the controller
uses — `BrakingPosition (+0x1c) x MaxBrakingAction x (front ? _brake_balance : 1 - _brake_balance)`
(`_brakes_power_front`, `updateBrakeControllers`, spec 6.4) — so a locked wheel (the handbrake, a
crash) still reads the pads that are actually applied.

The decal side (in `emitWheel`, tarmac only):

```
skid = max( slip, brakeSlip )
decalRate *= max( wetness, skid > 0.35 ? 1 : 0 )
if skid > 0.35: decalRate = max( decalRate, 3.0 * skid )   // a continuous black line
the mark's alpha is at least 0.80 * brakeSlip              // rubber on the road
```

### 7.2 The disc heat

`EdenSurfaceFx::updateBrakeDisc`, one `BrakeDisc` per wheel slot (position, axle + up axis, radius,
temperature, glow). Two terms, both `[INF]`:

```
heat in  = max( BrakeTorque, 0 ) * ( |v| / WheelRadius ) / Capacity       // the pads' power
heat out = ( T - ambient ) * ( 0.030 + 0.00007 * ( T - ambient ) ) * flow // Newton + a T^4 stand-in
flow     = 0.55 + 0.45 * saturate( |v| / 30 )                            // a parked disc cools slower
glow     = saturate( ( T - 260 ) / ( 780 - 260 ) )
```

| constant | value | why |
|---|---|---|
| `kBrakeAmbient` | 35 C | a warm day, the discs sit there |
| `kBrakeCapacityFront` / `Rear` | 700 / 300 J/K | a 380 mm vented front disc, a 330 mm rear one (the lighter rear heats faster per joule) |
| `kBrakeGlowStart` / `Full` | 260 / 780 C | the first visible red / a saturated orange |
| `kBrakeMax` | 1050 C | the pads would fade; the value is clamped |

So **one** hard stop from ~110 km/h ends with the front discs at ~740 C (glow 0.9) and the rear at
~530 C (glow 0.5); a second stop before they cool lights both fully, and a 200 km/h stop lights them in
one go. The rear is cooler than the front for the real reason: the 60/40 balance puts two thirds of the
pad torque on the front axle.

### 7.3 The drawing

`resources/shaders/brakedisc.frag` (regenerate with `python tools/compile_shaders.py brakedisc.frag`),
drawn in the same `Effects` pass, **blended, not additive** — an additive orange over the silver rim
adds up past white, which is exactly how the first cut looked. Two quads per hot wheel, in
`discScratch` / `haloScratch`:

* **the rotor face**: the wheel plane, 2.5 cm outboard of the hub, `0.63 x tyre radius`. The shader
  draws an **annulus**: the friction ring the pads sweep (`0.40 .. 0.965` of the radius, soft inner
  edge, tight outer lip) is what glows, and the hat in the middle (the aluminium bell, which no pad
  touches) stays at 10 % — a hot disc is a glowing ring, not a glowing plate (the owner's close-up of
  the first cut). The rim's spokes are depth-tested **in front of it** and are silhouetted against
  the orange, the original's look.
* **the spill**: the camera-facing quad in the wheel plane (2.0 cm outboard of the hub,
  `0.95 x radius`, an annulus too), alpha 0.30 of the face's. It sits **inboard of the spokes**, so
  the rim, the spokes and the caliper depth-cull it and it only shows through the rim's openings. The
  first cut drew a 0.62 m billboard `0.9 x radius` (19 cm) *outboard* of the hub — in front of the
  whole wheel — and that flat orange washed the spokes, the tyre and the caliper.

The grain is a few percent of angular streak plus a 160-cell speckle
(`0.05 * sin(angle * 17) + 0.03 * sin(angle * 41)` and `0.05 * hash(160 cells)`), not the first cut's
`16 x 16` hash: 16 cells across a 0.43 m rotor are ~27 px in a close-up and read as a checkerboard.

The colour is the heat's, never white: `(0.55,0.03,0.005)` red at the glow start through
`(1.0,0.16,0.01)` to `(1.0,0.38,0.04)` at the top, alpha `0.95 * glow^0.75`.

**The rotor and the caliper belong to the wheel, not to the body.** `CinematicCar::load` lifts each
`D_RIM_xx` (and its tyre child) to the physics tyre radius; the brake assembly hangs off the upright,
not off the rim (`FORK_xx > upright > DISC_xx > BRAKE_xx` + `D_RIM_xx`, with `STIR_xx` the caliper
beside `DISC_xx`), so it takes the same lift in the same loop, matched by name
(`BRAKE_<corner>` / `STIR_<corner>`). Without it the California's rotor and caliper sat 0.120 m under
the wheel centre — at the bottom of the rim — and the glow quad, drawn at the physics hub, did not
line up with the model's own disc.


### 7.4 Verified (headless, this workspace)

| What | Command | Evidence |
|---|---|---|
| The discs heat on a hard stop | `-logbrakeheat -fixedtimestep -autoplay "30:hold=accel;250:release=accel;260:hold=brake;380:release=brake" -exitframe 430` | `BrakeHeat: 35 C` -> `737 C/0.92 603 C/0.66 558 C/0.57 528 C/0.52`; after the release `716 -> 591 -> 556` |
| The glow draws | the same drive, shot at 378 | A/B against `-nobrakeheat`: wheel region mean 5.2, **5,302 px > 24**; `docs/images/surface_fx_brake_disc.png` |
| The colour reads orange, spokes silhouetted | `-brakeheattest 100` + `look=0.4,0` (an orbit of the chase camera) | `docs/images/surface_fx_brake_disc.png` |
| Hard braking leaves a mark on dry tarmac | `-logsurfacefx -autoplay "...;260:hold=brake;380:release=brake"` | `206 decals, ground tarmac`; A/B against `-surfacefxscale 0`: road region mean 8.1, **14,215 px > 24**; `docs/images/surface_fx_brake_marks.png` |
| Spray / tracks unchanged | `-surfacefxtest -logsurfacefx -autoplay "40:hold=accel;330:release=accel"` | `186 particles, 483 decals, ground dirt` (the §4 numbers) |
| The glow is a rotor ring, not a flood (s85) | `-gm FREERIDE -car California -skipcine -nopad -fixedtimestep -night -brakeheattest 100 -autoplay "8:look=1,0;45:shot=..." -exitframe 50`, A/B against the pre-fix binary at the same camera | wheel region mean 11.2, **3,716 px > 24**; the orange no longer covers the tyre, the arch or the spokes: `docs/images/surface_fx_brake_glow_before_after.png` |
| The glow still draws (A/B, s85) | the same run with `-nobrakeheat` | wheel region mean 5.0, **1,206 px > 24** (the disc ring only; 5,302 px was the old spill's whole-wheel flood) |
| The brake assembly follows the wheel (s85) | the same run, log | `CinematicCar: brake node 'BRAKE_FL' lifted 0.120 m with the wheel 'D_RIM_FL'` — 8 lines, the 4 rotors + the 4 calipers |

### 7.x The disc glow, second pass (session 2026-11-04, `win`) [INF]
The owner: the brake heat *"could also do with a little extra work"*. The face read as a flat orange plate
(one colour for the whole ring, angular streaks that stood still on a spinning disc, a camera-facing spill
that tinted the spokes at an angle). `brakedisc.frag` now gets the glow in the vertex alpha and works the
colour out per pixel: a temperature profile across the friction ring (hottest on the pad track's centre
line, 62 % at its edges), a blackbody ramp (cherry -> red -> orange -> a yellow-orange core when really
hot), concentric pad grooves and a radial speckle (rotation invariant), a slow 2.5 % shimmer, and the hot
core adds light on top of its coverage (premultiplied blend). The spill is now in the wheel plane (the
spokes cull it) and added, 1.2 x the rotor. Evidence in `docs/images/car_lamps_glow_brake_disc.png`
(`-brakeheattest 100` / `45`, night and midday).
