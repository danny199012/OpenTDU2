# Cockpit gauge needles + the camera speed cues (shake, radial blur)

Session 77 (`cline`). The owner: *"I noticed the needles weren't fully
implemented if you can do them things you mentioned about the needles and camera
data etc…"*. This file is the reference for the two pieces: the **3D interior
needles** (they were static at 0 in every cockpit) and the **camera speed data**
(`shake_*` / `blur_int_*` of CamerasInGame.xmb, unused until now).

--------------------------------------------------------------------------------
## 1. The 3D cockpit needles

### 1.1 Where they live

The needles are **model nodes of the interior bank**
(`Vehicules/Interiors/<car>_i.bnk`), not of the car model and not of the GAUGES
Flash movie. Two node layouts exist in the shipped data:

| car | pivots | poses | needle mesh |
|---|---|---|---|
| California | `D_ND_SPD` (0.442, 0.694, 0.250), `D_ND_RPM` (0.287, 0.696, 0.250) | `ND_SPD_1` / `ND_SPD_2`, `ND_RPM_1` / `ND_RPM_2` | separate `ND_SPD` (node 14, child of `ND_SPD_1`), `ND_RPM` (node 10, child of `ND_RPM_1`); box y −0.046..+0.016 |
| R8 V10 | `D_ND_SPD` (0.272, 0.667, 0.613), `D_ND_RPM` (0.442, 0.667, 0.613) | same names | the `_1` pose node itself carries the geometry (`ND_SPD_1` node 14, `ND_RPM_1` node 11; box z 0..0.241: long shaft + short blade) |

`-logcar` dumps every interior node whose name contains `ND_`, `AIG`, `SPD` or
`RPM`, with its parent, world position, rotation and item bounds. The node
indices above are for those two cars; the driver finds the names, not the
indices.

The retail code is `FUN_00c52240` (docs/re/california_spec.md §4): "D_ND_SPD /
D_ND_RPM rotation reset to identity, needle between ND_x_1 (value 0, rest pose
of ND_x) and ND_x_2". The port's `EdenModel` keeps **world** transforms, so a
pose parent cannot move its child at run time: the driver rotates the **mesh
node** itself.

### 1.2 The needle tables

`hud.ini` of the car's gauge bank (`FrontEnd/HiRes/Gauges/<hud_file_name>.bnk`)
carries `AddHud` lines. The values are `val_min val_max ori_min ori_max` plus the
intermediate `(value, angle)` pairs:

```
AddHud "interior_kmh" 0 300 0 308 20 26 320 282
AddHud "interior_mph" 0 210 0 308 200 281
AddHud "interior_rpm" 0 10000 0 265
AddHud "exterior_rpm" 0 10000 0 265
```

The `(320, 282)` point of `interior_kmh` is **outside** its 0..300 range (it is
the mph 200 mark of the dual scale), so the loader keeps only the points inside
`[val_min, val_max]` and sorts by value. The tables become:

* `interior_kmh`: (0, 0°), (20, 26°), (300, 308°)
* `interior_mph`: (0, 0°), (200, 281°), (210, 308°)
* `interior_rpm`: (0, 0°), (10000, 265°)

The speed table is chosen by the **player's unit system** (`interior_mph` in
imperial, `interior_kmh` metric), so the needle and the HUD digits agree. RPM is
unit independent. A car whose gauge bank has no `interior_*` table keeps its rest
pose (0°).

### 1.3 The driver

* `FreeRideHud::updateGauges()` maps the display speed (`SpeedForDisplay`) and
  `state.Rpm` through the tables and exposes
  `getInteriorSpeedNeedleAngle()` / `getInteriorRpmNeedleAngle()` (degrees).
* `GMFreeRide::tick` calls `car.setDashboardNeedles( speedDeg, rpmDeg )` after
  `hud.tick`.
* `CinematicCar::load` caches the mesh node and its rest transform (speed:
  `ND_SPD` when it has geometry, else `ND_SPD_1`; rpm likewise).
  `setDashboardNeedles` sets `rest * R_localZ(angle)` on the mesh: the needle
  sweeps clockwise from its 0 position, which is what the logs/dial show
  (`-needletest ND_SPD:z:90` moves the California needle from the bottom to
  9 o'clock).
* This is per frame and cheap (two matrix writes).

### 1.4 Calibration tools

| flag | what it does |
|---|---|
| `-logcar` | dumps the `ND_` interior nodes (and the car model's) with parent, transform and item bounds |
| `-needletest <node>:<x\|y\|z>:<deg>` | rotates one interior node about its local axis once, for finding the needle pivot / sweep axis on a new model |
| `-needlepose <pose>:<mesh>` | puts a needle mesh at one of its pose nodes (`ND_x_2` etc.), to compare the 0 / max poses |
| `-noneedles` | keeps the live driver off, so `-needletest` / `-needlepose` stay visible (A/B of the needles) |

Measured: the California pose `ND_SPD_1 -> ND_SPD_2` is an 84.5° rotation about
the dial normal, the R8's is 103°; they are the two unit / scale rest poses, not
the 0..308° sweep, so the port drives the mesh from its own 0 pose with the
table angle. If a car's `ND_x_2` turns out to be its imperial 0, the base pose
can be switched per unit later (the node is cached).

--------------------------------------------------------------------------------
## 2. Camera shake (`shake_*`)

`CamerasInGame.xmb` gives every camera mode a set of shake fields; the port now
parses and applies them (`ChaseCamera::Params` / `update`):

* `shake_speed_slow` -> `shake_speed_fast`: the primary speed ramp (a zero-length
  ramp, which several cameras ship as 0/0, is disabled instead of divided by 1);
* `shake_speed_max` -> `shake_speed_max2`: the high-speed ramp;
* `shake_rpm_purcent`: the rpm ramp starts at that percentage of the red line
  (the port passes `setup.Db.RedLine` in rpm, **not** the rad/s value of
  `VehicleSetup::RedLine`);
* `shake_x_magn_*` / `shake_y_magn_*` and `shake_x_period_*` /
  `shake_y_period_*`: a sinusoid of the eye (or of the aim point when
  `shake_on_target`), magnitudes taken as centimetres and periods as
  milliseconds `[INF: the retail units are not in the readable code]`.

`-logcameras` now prints `ChaseCamera: shake <0..1>, blur <0..1> (speed, rpm,
red line)` once a second. California chase mode 20 measured: 0.00 at rest,
0.14 at 21 km/h, 0.42 at 52, 0.49 at 73; its cockpit mode 23 ships **no** shake
data (0.00), which is why the cockpit feels still.

`slip_shake_*` (the slide shake: angle min, coefficient, frequency, three
magnitude/frequency bumps, `slip_shake_on_target`) is parsed by the data but not
applied yet.

--------------------------------------------------------------------------------
## 3. Speed radial blur (`blur_int_*`)

The retail camera data also carries `blur_dist_att_type`, `blur_int_speed_slow`
/ `fast`, `blur_int_slow` / `fast` (0..255), the horizontal / vertical
`blur_ha_*` / `blur_va_*` and `blur_*_fade`. The port applies a **radial blur**
at the final image (a new 8-tap branch in `resources/shaders/blit.frag` and
`upscale.frag`):

```
blurT      = clamp((kmh - blur_int_speed_slow) / (blur_int_speed_fast - slow), 0, 1)
intensity  = clamp((blur_int_slow + (blur_int_fast - blur_int_slow) * blurT) / 255, 0, 1)
strength   = blurT * intensity            // 0..1, EdenSceneView::setSpeedBlur
UV amount  = strength * 0.03              // max 3 % of the screen radius
```

The smear grows from 25 % at the screen centre to 100 % at the edges
(`blur_dist_att_type 1`), sampling 8 taps toward the centre. `-noblur` is the
A/B switch; the blur is disabled in every non-driving state (the tick resets it
to 0). The horizontal/vertical movement blur (`blur_ha_*` / `blur_va_*`) is not
implemented: it needs a velocity buffer, which the port does not have.

Measured California chase mode 20: blur 0.02 at 21 km/h, 0.10 at 52, 0.18 at 73
(strength 0.02 %..0.54 % of the screen at those speeds, max 2 % at 150 km/h).
On/off at the same frame: edge Laplacian variance 542 (on) vs 565 (off), i.e.
the edges are measurably softer; 103k pixels differ across the frame.

--------------------------------------------------------------------------------
## 4. Open

* `slip_shake_*` (the slide tremor).
* The horizontal / vertical motion blur of `blur_ha_*` / `blur_va_*` (velocity
  buffer); the `-rtao` mask blit has its own combine shader and bypasses the
  radial blur.
* The `ND_x_2` pose is not used: if a car's second pose is its imperial 0, the
  metric/imperial base poses can be switched per unit.
* The **digital** dash (DASHBRD movie / `int_digital.2db` of the Diavel) is a
  separate, still unrendered screen.
* The R8 cockpit is its only gameplay camera (modes 23/24): with the needles now
  driven it is at least alive; a true chase mode for it would need a camera
  entry in the data.

Files: `source/game/freeride/freeride_hud.{h,cpp}` (tables + interior angles),
`source/game/cinematic/cinematic_car.{h,cpp}` (needle nodes, setDashboardNeedles,
`-needletest` / `-needlepose`), `source/game/freeride/gm_freeride.cpp` (the per
frame call, `-noblur`), `source/game/freeride/chase_camera.{h,cpp}` (shake +
blur data), `source/render/eden/eden_scene_view.{h,cpp}` (setSpeedBlur),
`resources/shaders/blit.frag` and `upscale.frag` (+ the generated
`source/render/shaders/builtin/*.h`).
