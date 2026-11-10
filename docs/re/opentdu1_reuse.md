# OpenTDU1 (OpenTestDriveUnlimited) — what we can reuse for OpenTDU2

Tags: [CODE] read from the OpenTDU1 repository · [INF] inferred, reason given ·
[DATA] game files.

Repo: <https://github.com/opentestdriveunlimited/OpenTestDriveUnlimited> — the
**Test Drive Unlimited 1 (2006)** source port. The org repo is canonical;
CamilleKMI's personal fork (the author of our upstream `CamilleKMI/OpenTDU2`)
sat 278 commits behind it and is no longer tracked. A local clone lives at
`C:\temp\opentdu1-ref` (shallow, re-cloned 2026-11-10 at `5487449`; re-clone or
`git pull` there to update — it is **outside** this tree on purpose, do not
commit it). Everything below was read from that clone; `FUN_00xxxxxx` comments
in its code are the original TDU1 exe addresses, the same convention as ours.

**Licence status (2026-11-10): the org repo ships no licence** (no LICENSE
file; GitHub reports `license: null`). No licence = all rights reserved: read
it, extract laws, reimplement — but **no verbatim port into this GPL-3 tree
until that changes**. The owner filed org issue #548 (2026-11-10) asking for
GPL or explicit permission; revisit when it is answered.

Maturity (README progress table, 2026-11-10): **FreeRide done** — as are Map,
Home, Shop, Mission, Garage, Living, Sandbox, Dressing, Aftermarket,
CarShowCase, TestCar, InstantChallenge, Estates, Login, Intro, CinViewer,
VideoBumper, AITrainer; in progress: DriveIn, Club, OnlineReplay. Handling
physics exists and runs **on Jolt** (Havok swapped out;
`third_party/JoltPhysics`, CMake `OTDU_USE_JOLT`) — see §2.11. Same
layout as ours (`source/{render,game,world,flash,...}`, `tdu_instance.cpp`,
`gs_*` services), C++17/Vulkan.

**Why it matters:** TDU1 and TDU2 share the Eden engine lineage — the material
(.2DM) / bitmap (.2DB) / model (.3DD/.3DG) container family, the `.sob` shader
libraries **with the same shader family GUIDs** (TDU1 `0x183157b81c6c7357` =
our TD_CarPaint [CODE]), the streaming grid, the weather-preset system and the
cinematics. Where TDU2's readable code stops (SecuROM), TDU1 often has the
*same law* in the clear.

---

## 1. Direct answers to the two live bugs

### 1.1 The meeting_tess "black sail" / "camera causes culling" (session 45 diag)

What this session established (A/B evidence, `docs/STATE.md`):

- The sail is 3D depth-tested geometry in the first in-car shot (t 0..5.5 s
  only), survives hiding: world (areas/objects/instances/terrain/ocean/sky/
  clouds), cinematic sets, props, both avatars, the whole car model, rims,
  tires, windows, HANIM_S, hardtop panels, shadow boxes.
- Hiding `GLASS_F` (the windscreen node) **removes part of it**; what remains
  is an interior-cockpit mesh that draws only when the interior bank does
  (`bInterior`, camera < 5 m). The sail correlates with the glass panes seen
  from inside: **the glass renders as a near-opaque black wall from the wrong
  side** instead of clearing — the same root cause as the "dirty glass" of the
  car view below. [INF: A/B evidence; the exact shader term is still open.]
- The interior's **GPS dashboard screen quad** (material `GPS`, shader family
  `0x7dad16b47eb57b95`, interior nodes 5 and 31, both named `GPS`) draws
  unprojected. TDU1's equivalent (`vehicle_base.cpp:729` `setGPSTexture`,
  FUN_008b6740) **writes the runtime static params into that material**:
  `[0] = minimap viewport U`, `[1] = minimap viewport V`
  (`gpMngHud->getMinimapViewportU/V()`), `[2] = 3.0` (`kGPSScreenParamZ`),
  after rebinding the `satellitemap` bitmap into the slot. **OpenTDU2 writes
  none of these** — the raw quad floats across the scene whenever the
  interior draws. Fix: write the params (needs the minimap viewport) or hide
  the `GPS` subtrees of the interior model until the dash GPS exists. [CODE]

### 1.2 The car-view "dirty glass"

- Our glass math (ps_109, `docs/re/car_glass_spec.md`): base is lerped to
  **black** by `G_I` (the 32×32 all-zero DXT1 of `common_car.bnk`, sampled at
  TEXCRD1 → alpha 1 ⇒ base = black), `oC0.a = detail.a` (≈0.57 on the panes),
  reflection = `cube.rgb * (1 - cube.a * SPECPARM.z) * v5` with the fresnel
  floor from `ENVCOEFF.z` (0.5, weather `Meeting_Tess`/`Garage`) — at grazing
  angles the pane is a black band with a hard grey reflection streak. That is
  what the owner screenshots as "dirt".
- TDU1 reference points for the same family:
  - Glass materials are found **by name hash** `"GLASS"` / `"GLASS_I"`
    (`vehicle_base.cpp:3019-3020`) — name-based lookup, not slot index.
  - The interior glass material parameter array carries guid
    `0xdddddddd1c6c7357` — **the same family guid TDU2 uses for Glass_I**
    (`material_types.h` / `vehicle_controller.cpp:150`). TDU1 writes runtime
    PS params into it: type `0x3e`, `floats[2] = fresnel coef`,
    `floats[3] = fresnel power` (`vehicle_controller.cpp:149-162`).
  - **Windshield dirt law** (`vehicle_controller.cpp:129-146`):
    `gGlassDirtCoefStart 0.1, Max 0.8, Time 600 s, AlphaCoef 0.8,
    FresnelPower 3.0`; value ramps linearly with distance driven, packed
    `(value, 0, alphaCoef, 0)` into shader uniform slot 3, zeroed when the
    flag is off. Reset on spawn (`vehicle_car.cpp:240`). This is the TDU1
    twin of TDU2's DIRTCOLOR0..3 + DIRT_SCRATCH system — proof that the dirt
    term must be **driven by runtime values that start at zero**, and that a
    missing/always-on dirt drive is a classic "dirty glass" cause.
  - **Env factor law** (`vehicle_base.cpp:2715-2719`):
    `envFactor = ((luminosity - 1) * gReflectionFactor + 1) * gGlobalEnvValue`
    with defaults `gGlobalEnvValue = 0.7`, `gReflectionFactor = 0`
    (`gs_car_display.cpp:24,30`), per-frame from the weather config;
    back-face variant 0.1 (`vehicle_base.cpp:454`). TDU2's
    `car_envmap_factor` (1.4 in the Garage preset [DATA]) plays the same
    role; if our glass is too reflective, compare against this law before
    touching shaders.
- Next step for the glass (not done this session): dump what the *retail*
  TDU2 writes into the Glass material's `LAYRCLR1`/`ENVCOEFF` at runtime
  (the port currently only writes ENVCOEFF; `LAYRCLR1` stays at the authored
  `(1,1,1,1)`, `vehicle_base`-equivalent unknown [ENC]) and A/B
  `LAYRCLR1.w` → the base lerp factor `detail.a * c17.w` is the only term
  that can brighten the base.

### 1.3 The garage pan "torn wheel" (near plane)

The C_INTRO pan passes < 0.8 m from the rim; the port clips exactly like the
retail default would (near 0.8 / far 40000 confirmed in **both** games:
TDU1 `gs_render.cpp:241-242`, TDU2 `intro_spec.md` §7.3 [CODE]). Not a port
bug; document if the owner asks. TDU1's answer for *close free cameras* is a
**dynamic near plane**: `near = distanceToTarget * 0.07` clamped [0.18, 1.0],
far 5000 (`camera_showcase.cpp:114-120,195-199`); steady-cam:
`near = (fov - 60) * 0.9 / (45 - 60) + 0.1` clamped [0.1, 1]
(`camera_steady_cam.cpp:25-28,123-133`). If the owner wants the car-view
camera to be usable that close, this is the law to adopt. [CODE]

---

## 2. Subsystem inventory (what exists, where)

### 2.1 Vehicle pipeline (`source/game/vehicle/`)

- `bank_vehicle.{h,cpp}` — streamed car container: ≤0x40 `.2DB`s, UVAs, one
  `.3DG`, physics + torque binaries. Four resource sets per car: Exterior /
  Interior / FrontRims / RearRims (`vehicle_base.h:333-341`); rims load async
  from `rim/<rim>/<rimF|rimR>` (`vehicle_controller.cpp:233-257`).
- Fixed 30-node hierarchy name table `D_CAR, BODY_LR, DISC_FL..RR, WLR_*,
  FORK_*, SPRG_*, DOOR_L/R, COCK_LR, BODY_HR, COCK_HR, Int_HR_L/R,
  D_Int_HR_L/R, D_CAST_I` (`vehicle_car.cpp:21-26`) — TDU1's twin of our
  california.3dd node names; useful cross-check for node semantics.
- **LODs**: 5 levels, distances `{250, 21, 20, 12, 0}`
  (`vehicle_car.cpp:29-30`), `applyLOD()` (`vehicle_car.cpp:660-783`) swaps
  BODY_HR/LR, forks+springs vs WLR wheels, cockpits, doors, belts by node
  visibility flag 4 — same structure as our `CinematicCar` LOD switch.
- Doors/windows/lights: `windowRoll[2]` (speed 0.5/s), popup headlights with
  begin/end transforms, interior doors copy exterior matrices
  (`vehicle_car.cpp:208-224,1248-1275`). Showcase helpers
  `setDoorTranslateAndRotate` / `setWindowOpenLeft`.
- **Gauges**: needle nodes `ND_SPD_1/2, ND_RPM_1/2, ND_TRB_1/2`, slerp between
  two keyframe rotations, speed→angle 0.010358401/kmh with
  GraduationChangeSpeed/Ratio law (`vehicle_controller.cpp:276-286,621-665`).
- Flares: 60 flare nodes / 19 types (`vehicle_base.h:123-145`).
- Tire material cloned per instance into a 4096-byte buffer
  (`vehicle_car.cpp:1201-1246`).
- Traffic cars forced to env coeff 1.0 (`vehicle_base.cpp:1966-1994`).

### 2.2 Paint / colour system — `source/game/vehicle/paint.h` (whole file)

- `ePaintType`, `eCarColorPart`, `DeluxeSurfaceSettings {RampExp, SpecularExp,
  EnvMap[4]}` — solid `{1.0, 9.0, {0.6,2.0,0.3,1.0}}`, metal
  `{1.0, 4.0, {0.6,2.0,0.3,2.0}}`, `kDeluxeColorScale = 0.7`
  (`paint.h:53-70`); `ComputeDeluxeShades` (`paint.h:81-114`).
- Shader GUID predicates `IsPaintShaderGUID` / `IsPaintOrTwoTonesShaderGUID` /
  `IsCarInteriorShaderGUID` (`paint.h:116-151`).
- Texture-slot → flag semantics `RefreshClonedMaterialFlags`
  (`paint.h:175-228`) and interior remap pools (`paint.h:158-173`).
- Runtime param writers: `applyReflectionParamsToResNodes` (PS `0x3e`
  floats [0..3] = env coeffs), `applySpecularParamToResNodes` (PS `0x1b` [0] =
  specular exponent), `applyColorStripToResNodes` (PS `0x7c` width,
  `0x7a`/`0x7b` colours), `applyTwoTonesColorToResNodes` (PS `0xd5`),
  `applyRampExpToResNodes`, `applyGaragePaint(CarDesc&)`
  (`vehicle_base.cpp:1941-2079,2392,2531`). These are the TDU1 twins of our
  MATPARAM/SPECPARM/RAMPPOW/ENVCOEFF writers — the PS register ids
  (`0x1b` spec, `0x3e` env/ramp) are the constant slots the TDU2 shaders use
  under other names.

### 2.3 Environment cubemap (`source/render/mng_env_map.{h,cpp}`)

- Two runtime cubemaps `CUBEMAP` + `CUBEMAPB` (256²), created in
  `gs_mng_resources.cpp:1575,1598`; capture passes RP_EnvMap / RP_EnvMapBack /
  RP_EnvMapMini with a PostFX chain (TexSample → CubemapToSphere →
  EnvMapScene).
- Capture tuning (`mng_env_map.cpp:45-88`): Cockpit FOV 60° far 150; Hood far
  60 coeff 0.6; Exterior far 45 coeff 0.3; camFOV 140°, camHeight 0.64,
  heightBias 1.1, lookDistance 2.0, lumFactor 1.35, saturation 0.35. Gathers
  the 3 nearest sectors + 3 nearest vehicles (`mng_env_map.h:67-68`).
- `VehicleBase::setRealTimeEnvMap` (`vehicle_base.cpp:641-727`): walks the
  car's 2DMs and rebinds layers hashed `CUBEMAP`/`CUBEMAPB` on paint-shader
  GUID groups A `{0x183157b81c6c7357, 0xc288dfc455ae5ef8,
  0x1b38348331d3942a}` and B `{0xb39c35015789e65c, 0xcab12190a00d5c4e,
  0x69f07f72719bf862}`. **Group A's first guid is our TD_CarPaint** — the
  TDU1 twin of our `CinematicCar::assignEnvironment` family list; note TDU1
  has **no TD_Mirror entry** (wing mirrors are a TDU2 feature).

### 2.4 Weather (`source/world/weather/`, `source/config/weather.cpp`)

- `weather_config.h` is a complete preset field reference, including the car
  block (212-220): `carLuminanceRange, carGlobalFactor, carEnvFactor(=
  car_envmap_factor), carAmbiantFactor, carDiffuseFactor, carSpecularFactor,
  carReflectionFactor, carReflectionLuminosity, carCloudsFactor` + HDRI.
- Script command ids for every field (`config/weather.cpp`:
  `CARSPECULARFACTOR(668)`, `CARREFLECTIONFACTOR(676)`,
  `CARREFLECTIONLUM(684)`, `CARCLOUDSGROUNDFACTOR(692)`…), blending via
  `scaleBy(t)` / `addFrom(other)` (`weather_config.h:33-34`).
- `GSCarDisplay::tick` (`gs_car_display.cpp:257-280`) feeds all car factors
  from the active config each frame; `GSRender::onWeatherConfigChange`
  (`gs_render.cpp:443-459`) refills fog/lights everywhere.

### 2.5 Cinematics (`source/game/cinematic/`, `cinviewer/`, `scenaric/`)

- `cinematic_format.h`: `CIN!` version 0x96, `SCE!` version 0xfb, chunk layout,
  `CinematicHeader` 0x3c with every count — TDU1's `.cin/.sce` are
  structurally the format our `Cinematic`/`CinematicScene` parse; useful
  cross-check for our reader's edge cases.
- **ZNear/ZFar entity fields** (`entity_cinematic.cpp:477-481`, v≥9; sentinel
  −1 = `kCinematicPlaneUnset`): shotNear = 0.8 default / ZNear override,
  shotFar = 40000 / ZFar (`entity_cinematic.cpp:229-242`) — **byte-for-byte
  the law we implemented**; TDU1 also pushes them to "interior planes".
- `Micro` (v≥0xc) and `StreamCenter` (v≥0xa) entity refs parsed
  (`entity_cinematic.cpp:495-499`) — the fields our `-logentities` prints.
- Camera view publish: `CameraView{Position, Target, Up, Fov}` →
  `gpCamera->setCinematicCameraOverride` (`entity_cinematic.cpp:757-793`);
  camera shake defaults amplitude 0.002 / speed 0.2 (`cin_res_collection.h:27-35`).
- Messages: TDU1 has **no `zcvn/zcne/wmod/shad:` strings** — its message keys
  (typeHash 0x73d8ccae) carry REFS descriptors with music commands
  (`processCinematicMessages`, `cin_res_collection.cpp:429-491`;
  `cinematic_music_emitter.cpp:111-145`). So the TDU2 letterbox/vignette
  `zc*` law stays unrecoverable from TDU1 — the encryption wall stands [ENC].
- Event enum `CE_*` (0 requested, 2 stopping, 3 ended, 5 started,
  6 start-end-fade, 7 finished, 10 begun) matches our listener ids
  (`entity_cinematic.h:11-18`).
- Fade-before-end law: fade at `total − fadeTime_ms/1000`
  (`entity_cinematic.cpp:417-420`); skip protocol with `markSkipped()`
  (`mng_cinematics.cpp:107-145`).

### 2.6 Streaming / culling (`source/world/`)

- `sector_manager.h`: **7 sector slots** (`kNumSectors=7`), **4 area slots**,
  8-direction neighbour resolve — compare with our (2r+1)² ring + hysteresis.
- `streaming_manager.h` + `gs_world_streaming.h:34-39`:
  setStreamCenter / setStreamCenterPosition / resetStreamCenter — the
  StreamCenter entity-field consumer.
- Heightmap fade law: `hmapBeginFadeDist 100`, `hmapFadeLength 3000`,
  uniform[5] = `-1/len`, `(begin+len)/len` (`gs_render.cpp:239-240,387-389`).
- Impostor manager with per-material batches, alpha-fade copy, budget
  (`mng_impostor.h`).

### 2.7 Car showcase / garage (the "car view" recipe)

`gm_car_showcase.cpp` is a **complete indoor car-view recipe**:

- floor at −0.25, background `RGB(29,41,44)`, fog rewritten to the background
  colour with `DistanceStart 0, DistanceEnd -1` (fog off), physics infinite
  plane (81-110).
- **Planar floor reflection** `mngReflection.setPlanarCoords(up,
  floorHeight*2)` + car body/interior nodes registered to it (152-153,
  525-537); `MngReflection` (`mng_reflection.cpp`, 173 lines) is a complete
  reference: mirror camera = `camToWorld * Scale(n*-2 + (1,1,1))` + plane
  offset translation, FOV/aspect copied, oblique clip plane via
  inverse-transpose of ViewProj (`calculateClipPlane`, 159-173), reflection
  clear colour white, buckets mask `0xffffff7fffefbfff`.
- **Realtime cubemap** on the car (`SetRealTimeEnvMappedCar`, 476-478).
- `CameraShowcase` orbit/turntable + photo mode: zoom damping, pan clamp
  `(dist/max)*6+2`, roll ±0.6, ground avoidance
  `(radius-0.18)*1.2195122*1.5+0.5`, physics sphere-sweep collision rejecting
  up-facing car normals (`camera_showcase.cpp:304-415`).
- `MngShowCaseCarControl` — door/window animators for photo mode
  (`mng_showcase_car_control.h:59-68`).

### 2.8 GPS

- `VehicleBase::setGPSTexture` (`vehicle_base.cpp:729-806`, FUN_008b6740):
  material named `"GPS"`, shader guid `0x7dad16b47eb57b95`, rebinds
  `satellitemap`, writes static params `[0] = minimap viewport U`,
  `[1] = V`, `[2] = 3.0`. The dashboard screen samples the **same atlas
  sub-rect as the HUD minimap** — our map screen already knows that rect.
- `render/mng_cam_gps.{h,cpp}` — overhead-earth map camera (mostly
  `OTDU_UNIMPLEMENTED` stubs, but the header documents the field set).

### 2.9 Materials / shaders

- `material.h`: `Material` 0xd0, `MaterialParameter` 0x40 (Hashcode =
  shader guid), `MaterialLayer` 0x110 with 8 named layer textures
  (`"NMAP"`, `"COLOR"`, `"CUBEMAP"`…), `MaterialShaderParameterArray` 0x50
  with `getStaticParamDataPS(type)` where type = PS constant register.
- `2dm.h`: full LAYA/PARA/MATA section layout with `CalcSize` including the
  legacy `param_1*0x10 + 0x2d3 + param_2` form (line 84) — **a direct
  reference if we ever write .2DM patchers** (our phase-3 cine tool plans
  `.sce` round-trips; the same is possible for materials).
- `shaders/shader_table_*.inl` + `shader_constant_map.h` (129-457): the
  decompiled DX9 register→semantic tables — TDU1's answer to our CTAB
  auto-binding.

### 2.10 Render frame order

`gs_render.h:41-108` holds the **complete Eden render-pass enum** (Noise →
Avatar LR → HUDMap → Shadow(+Cockpit) → EnvMap{Back,Mini} → Sun lum/dazzle →
Occlusion → Back → PreNear → ReflectPrePass → Reflect → NearOpaque → Near →
CarAlpha chain → FakeDOF → CarPlayer → Cockpit(+Avatar/Helmet) → Sun marker →
Particles → Post2D). Useful as the authoritative ordering when we compare our
own pass order (env cube → ocean refl → Z prepass → shadows → planar →
opaque).

### 2.11 Handling physics + force feedback (new 2026-11-10, post re-clone)

The handling model the owner's issue #3 (silentvelour: "Drop TDU2 handling
completely, implement TDU1 physics") asks about is now substantially
implemented upstream — on **Jolt**, not Havok:

- `source/physics/vehicle/physics_vehicle.cpp` — 5,874 lines, 3 stubs;
  `vehicle_simulation.cpp` — 1,795 lines, 0 stubs; `gs_physics.cpp` — 6,754
  lines, 11 stubs. `physics_vehicle_replay.cpp` (610 lines, upstream calls it
  a throwaway tool) replays **retail Havok snapshots** (record header 0x10,
  per-vehicle record 0x43b0, wheel stride 0x450) into the Jolt rebuild "to
  evaluate the faithfulness of the physics model" — upstream validates its
  handling against captures of the shipped game, the same evidence bar we
  hold our own ports to.
- **Bike physics exists too**: `bike_roll_limit_constraint.cpp` (101 lines,
  0 stubs), a custom Jolt `TwoBodyConstraint` (`mRollLimit = 0.01f`) — the
  lean-limit law. Issue #3 wants TDU2 bikes kept; with this, a TDU1-bike A/B
  is on the table as well.
- Port consequences [INF]: a handling port either brings Jolt in as a
  dependency or translates the model onto our collision substrate (our
  `docs/re/havok_collision_spec.md`); the Havok-snapshot replay tool would
  not transfer either way — it exists to validate *their* Havok→Jolt swap.
- **Force feedback, complete chain** [CODE]:
  - tuning externs `input/gs_device.h:132-141`: `gFFBFactor`,
    `gFFBLoadFactor[3]`, `gFFBLoadFromAccel[3]`, `gFFBSlipAngle[3][2]`,
    `gFFBSpeedFactor[4][2]`; `config/device.cpp:168` registers `FFB_FACTOR`
    (FUN_009859b0), `device.cpp:215-217` parse the load-curve args.
  - composition `physics/gs_physics.cpp`: `EvalFFBCurve` (6441) piecewise
    curve eval; per-frame block ~6481-6578: impact rumble (`sImpactRumble`,
    `getRumbleMax` at 6504), speed-factor curve (6518-6522), load from
    longitudinal G (6524), slip-angle term (6548+), `bFFBInvert` (6541),
    slide rumble (6578+); debug graph at 3628 ("FFB graph (red = slip,
    blue = slip factor, magenta = torque)").
  - device out `input/gs_device.cpp:356-407`: SDL haptic constant + damper
    effects, `ToSDLHapticLevel` (79), `kFFBUnitsScale`.
  This is the whole issue-#3 FFB ask, written and exercised against the real
  game — the licence ask (org #548) is the only gate on porting it.
- **No refuel**: TDU1's gas stations are not implemented upstream (no
  `fuel`/`refuel` anywhere in `source/`), so refueling for OpenTDU2 has no
  OpenTDU1 code path — `docs/re/fuel_gas_station_spec.md` (TDU2's own
  disabled Shell feature) stays the only source; the TDU Platinum mod's
  reworked refuel is UX reference at most, never code or data in this tree
  (see NOTICE / the retail-grabs rule).

---

## 3. Ranked reuse list (what to port, in order)

1. **GPS dash quad** — write the `setGPSTexture` static params (or hide the
   interior `GPS` nodes) — kills the floating quad in every interior shot.
   `vehicle_base.cpp:729-806`.
2. **Glass runtime params** — TDU1 writes PS `0x3e` (fresnel coef/power) into
   the interior-glass family `0xdddddddd1c6c7357` per frame; find the TDU2
   equivalent writer [ENC] or A/B `LAYRCLR1` — the "dirty glass".
   `vehicle_controller.cpp:149-162`.
3. **`MngReflection`** — our planar reflections already exist
   (`eden_planar_reflections.cpp`); port the oblique-clip formulation and the
   white clear colour if pool/mirror reflections misbehave.
4. **`CameraShowcase` dynamic near law** — for the car view / photo camera
   (`near = dist*0.07` clamp [0.18, 1.0]).
5. **Weather car-factor set** — cross-check our `WeatherPreset` readers
   against `weather_config.h` field names (carEnvFactor,
   carReflectionLuminosity…).
6. **Env-cube capture tuning** — per-view far planes and coefficients
   (`mng_env_map.cpp:45-52`) for our dynamic cube quality.
7. **`paint.h` deluxe-shade laws** — when the paint colour UI comes.
8. **Gauge needle laws** (`vehicle_controller.cpp:621-665`) — for live
   dashboard gauges (our ND_RPM/ND_SPD nodes are static today).
9. **Streaming shape** — 7-sector/4-area slot manager + stream-center hooks,
   if our (2r+1)² ring ever shows pop-in.
10. **Force feedback** — the §2.11 chain (curve externs → composition →
    SDL haptic out), gated on the licence ask; benefits whichever handling
    model is live and is small enough to reimplement from the laws alone.
11. **TDU1 handling model as `-physicstdu1`** — issue #3's main ask; port
    behind a toggle on player cars (TDU2 model stays default; the bike
    question is decided separately), gated on the licence + a car-data
    conversion plan (TDU1 has ~125 cars, TDU2 100+, partial overlap — every
    ported car needs derived TDU1-style parameters; TDU2's
    `DBCarPhysicsData` does not map 1:1).
12. **Retail-physics replay validation** — upstream's idea, generalised:
    capture retail TDU2 car states, replay them through our physics, diff —
    the same faithfulness evidence for any handling work we do.

## 4. What TDU1 can NOT give us

- The TDU2 cinematic messages (`zcvn/zcne/zcde`, `wmod:`, `shad:`, `flsh:`,
  `phot`) — TDU1's protocol is different (music-tag strings); the letterbox /
  vignette law stays [ENC].
- Wing mirrors (`TD_Mirror`, the +0x2ab6 flag) — TDU1 has no wing-mirror
  render at all.
- TDU2's body dirt (DIRTCOLOR0..3 + DIRT_SCRATCH) — TDU1 only has windshield
  dirt; the accumulate law transfers, the shader terms do not.
- The California retractable roof (TDU1 has no convertible animation).
