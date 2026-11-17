# Car lighting in the Apr 2012 dev prototype: the per-frame lamp functions, the light data, the dashboard

> 2026-11-17 (`main`). Owner brief: *"dig the dev build see if you find any car
> lighting data head lights, tail lights, interior dash etc"*. This is that dig.
> Companion docs: `car_lights_spec.md` (the retail-side lamp law, its §2.2
> per-frame functions were `[ENC]` — this file decodes them), `dev_build_survey.md`
> / `dev_build_physics.md` (how the dev exe is read), `needles_spec.md` (the
> needle geometry), `night_lighting_spec.md` (the world's night lights).
> Tags: **[CODE-dev]** read from `TestDrive2DEV.exe` (`tools/devbuild_disasm.py`),
> **[DATA]** game files, **[SHADER]** .sob bytecode, **[INF]** inferred.

Everything here is the dev twin of what `car_lights_spec.md` marks `[ENC]`: the
whole car-light driver of retail sits in one dev module at **0x71f000-0x72a000**
plus the setup functions at **0x719f00-0x71a800**. Addresses do not carry over
from retail (§1 of `dev_build_physics.md`) — the anchors below are matched by
string / constant / structure, then cross-checked against the retail spec.

## 1. The DB light data is real and ships with every car [DATA]

`DBCarPhysicsData` (279 rows, `Euro/Bnk/Database/db_data.cpr`, decrypted with
`_lights_dig/xmb.py`) carries **14 light columns** — the retail log dump of
`-dumpdbtables` truncates at ~2 KB and hides them all but the front five:

| column | value |
|---|---|
| `_frontheadlightprojectionangle` | 46 |
| `_frontheadlightapertureanglewidth` | 176 |
| `_frontheadlightapertureangleheight` | 63 |
| `_frontheadlightactivatecaustics` | 1 (TT_RS_Cab = 0) |
| `_rearbreaklightprojectionangle` | 69 |
| `_rearbreaklightapertureanglewidth` | 180 |
| `_rearbreaklightapertureangleheight` | 86 |
| `_rearbreaklightactivatecaustics` | 1 (GTS400_Supercharged = 0) |
| `_rearreverselightprojectionangle` | 69 |
| `_rearreverselightapertureanglewidth` | 141 |
| `_rearreverselightapertureangleheight` | 86 |
| `_rearreverselightactivatecaustics` | 1 |
| `_headlightsorientation` | 10 (every car) |
| `_islightcarwashable` | 1 for 30 cars (below) |

Every car ships the same angles: **the projection / aperture values are global
defaults, not per-car art.** "Projection angle" = the TD_ShadowBox light-box
cone of `car_lights_spec.md` §2.4 (the BREAKING / REVERSING / HEADLIGHT slots).
`_headlightsorientation` = 10 everywhere, so the pop-up headlight decision is
NOT in the DB (the pop-up cars must carry it in the model/animation data; the
collector reads it as a byte at `+0x1f7` of the car row in the dev reader
0x4a4a90 [CODE-dev]).
`_islightcarwashable` = 1 marks the Ferrari-brand cars (250 GTO, 288 GTO,
308 GTS, 365 GTB4, 430/458/599/612/F40/F50/FXX..., plus CLK_63_BS) — the
"light car" of the car-wash spots (`car_wash_spec.md`), nothing to do with
lamps.

The dev exe reads the same DB through one giant row reader **0x4a4a90**
(every `_name` key → struct offset; the light keys land at +0x180..+0x1f7 and
the rear block at +0x33c). Three more light-ish keys exist only as **physics
ini keys** in the dev exe (`_headlightsorientation`, `_frontheadlight*`,
`_rearreverselight*`, `_rearbreaklight*` at 0x1484614-0x148502c) — the same
names, read by the vehicle-physics loader.

## 2. The headlight volume: three config distances [CODE-dev]

`HeadLightFrontDistance / HeadLightBackDistance / HeadLightLateralDistance`
(0x14ecf64 / 0x14ecf48 / 0x14ecf28) are read once from the config into globals
`0x1673464 / 0x1673468 / 0x167346c` with defaults **120 / 30 / 30 (meters)**
(function 0xabbc40 region, cached by the flag at 0x1b1d1a4). The consumer
(function around 0xaba878) is a point test: `|x| <= LateralDistance`,
`z <= FrontDistance`, `z >= -BackDistance` — **the box in front of a car in
which head-light receivers are tested** (120 m ahead, 30 m behind, 30 m to each
side). This is the volume test of the HEADLIGHTS BLIT system (§4): what falls
inside gets lit by the projected head-light buffer.

## 3. The per-frame lamp functions, decoded [CODE-dev]

Retail `car_lights_spec.md` §2.2: the per-frame driver calls
`table(0x01230500 + kind*200) + K` where every entry is an encrypted function.
**The dev table is 0x19ca470, filled by the static initializer at 0x1462410,
~50 slots of 8 bytes, all readable.** The first slots are the lamp functions:

| retail call (spec §2.2) | dev slot | dev fn | writes |
|---|---|---|---|
| table+0x00 = brake | 0x19ca470 | **0x71fdd0** | GLOSSFCT **+0 (r)** |
| table+0x28 = blinker left | 0x19ca498 | **0x720a50** | GLOSSFCT **+0xc (1-a)** |
| table+0x30 = blinker right | 0x19ca4a0 | **0x720c40** | GLOSSFCT **+0xc (1-a)** |
| table+0x38 = reverse | 0x19ca4a8 | **0x720e50** | GLOSSFCT **+4 (g)** |
| table+0x60 = headlights | 0x19ca4d0 | **0x721ad0** | GLOSSFCT **+8 (b)** |
| table+0x68 = position | 0x19ca4d8 | **0x721dc0** | GLOSSFCT **+8 (b)** |
| table+0x70 = unknown fn (`ctrl+0x1d9`) | 0x19ca4e0 | **0x7220b0** | node params via helpers |

This **confirms the channel law** guessed in `car_lights_spec.md` §1 from the
masks: r = brake, g = reverse, b = head/position, 1-a = blinker. Each function
has the same shape (brake 0x71fdd0 shown):

```
groups = {1, 0, 3, 4, 6, 2, 5}                      // order of the 7 light groups
for g in groups:
    pairs  = car + g*12 + 0x783C                    // the group vector (retail +0x783C confirmed)
    end    = car + g*12 + 0x7840                    // (0x7840 = pairs+4)
    for (material, channelPtr) in pairs:
        channelPtr[0] = value                       // the float argument, one GLOSSFCT channel
state = 1 or 2 (value > 0); if state == car+0xb0 and !force: return   // the state cache
car+0xb0 = state                                     // per-function cached state
```

- `0x1660800` is a byte "force every light function every frame" debug flag
  (0 = normal). The brake / headlight / position functions check it.
- The state cache offsets: brake +0xb0, headlight-family +0xb4 (read at
  0x7200d0, the same cache id for the whole +0x60/+0x68 family).
- The functions after slot +0x70 (0x722310, 0x722570, ... up to ~0x728910) are
  the rest of the 50-slot table — mostly small (they write the `?A_*` glow-node
  visibility / the `Fa_` flare sprite scale through helper calls 0x4235f1 /
  0x449bb6). **Not decoded one by one yet** — that is the remaining work; the
  port already reproduces their visible effect (collectLights/applyLights).
- The dev twin of the retail collector FUN_00c5ee20 is **0x719f87** (string
  `LGT_FL` table 0x1660808 = the 9 names LGT_FL, LGT_RL, LGT_RL_SK, LGT_FR,
  LGT_RR, LGT_RR_SK, LGT_R_S, LGT_L, LGT_R; it scans each material's map for
  "GLOSSFCT" and pushes (material, GLOSSFCT pointer) into car+0x783C + g*12).
  Constants right behind the name table (0x1660850..): 1.0, **0.6** (the blinker
  on-fraction of spec §2.3), 0.02, 0.8, **5.0**, **10.0, 10.0**, 5.0 — the 5.0
  is the tunnel threshold (`car +0x2acc >= 5.0`), used at 0x7181b6.
- The blinker timer function is **0x711150** (reads the 0.6 constant).

## 4. The retail head-light projection system: "HEADLIGHTS BLIT" + HeadLightsRT [CODE-dev + SHADER]

`rain_shadow_spec.md` B.7 guessed that s15's G/B is "a log-encoded light term
(headlights)". The dev exe confirms the system exists and names it:

- **"HEADLIGHTS BLIT"** (0x148c888) and **"HeadLightsRT"** (0x148ce58, with the
  log line "HeadLights RT Texture format = %s", 0x148ce68): the main-target
  setup function 0x4fd240 creates the RT chain (MainTarget / MainDepthBuffer /
  FullscreenDepthTarget names around it) and a config flag "HeadLightsRT"
  selects the format. So the head-light buffer is a real render target the
  scene shaders then sample through s15 (`gTexSamplerShadowRealTime`).
- **Debug toggles** (0x150e17c..0x150e638): "HeadLight beam", "HeadLight
  Reverse Caustics", "HeadLight Break Caustics", "HeadLight Front Caustics",
  "HeadLight Spot light", "Debug/Activate ... caustics" x3, and the panel
  "HeadLightAttributes". **"HEADLIGHTS SWITCH DISTANCE"** (0x14abe4c) is used
  by 0x735130 — the weather-driven head-light on/off switch (the function that
  blends car_headlights_intensity through the day keys; it reads the 5.0/10.0
  constants at 0x1660870/74).
- The light-box **texture ids**: function 0x716b77 registers id 13 =
  `breakcaustics`, id 14 = `reversecaustics`, id 15 = `headlightcaustics` (the
  common_car.bnk textures of spec §2.4). Ids 1/13/14/15 are also reset by the
  dirt/damage function 0x71ab30 (`car +0x2ae4 != -1`, 0x78 slots at +0x2164) —
  **the caustics boxes and the dirt share the car's texture-slot table**.
- The wheel-flare registration function 0x736ac0 (nearby) registers weather
  keys "FLARE SCALE LENGTH [METERS]" / "FLARE SCALE START [METERS]" /
  "MIN WHEEL ATT." / "WHEEL ATT." / "LOD FACTOR" — the wheel dust/flare
  attachments, not lamps, listed here because it sits in the same module.

Port relevance: OpenTDU2 draws the light pools with its own additive quads
(`car_lights_spec.md` §6 and the traffic half) because the renderer skips
TD_ShadowBox. The retail pipeline is a **projected light buffer** (blit the
head/brake/reverse caustics textures of every car in range into one RT, scene
shaders add it through s15 G/B) — the same shape as the port's shadow-box
projection work already started (`projectors`), and the RT AO/shadow work
(`ray_tracing.md`) could eventually replace the blit with ray-marched beams.

## 5. The interior dashboard light [SHADER + CODE-dev + DATA]

The cockpit dash is **material-driven**, and the mechanism is now named:

- `TD_DashBoard.sob` (guid `caca000000000010`, 96 permutations) and
  `TD_flashdashboard.sob` (guid `7dad16b47eb51337`, 4 permutations) both carry
  these `Var_PS` constants: **`IlumParam`, `IlumColor`,
  `HeadlightsIntensity`**, plus `gTexSamplerShadowRealTime` (s15 — the dash
  also receives the sun shadow AND the head-light blit of §4).
- The dev collector **0x71a680** walks a model's materials, accepts exactly
  those two shader GUIDs, looks up the **HEADLIGHTSINTENSITY** parameter
  (id 0x9b958ea092e3ece5) and pushes its pointer into **car+0x77e8** — the car
  keeps the list of dashboard-material intensity slots. (ILUMCOLOR /
  ILUMPARAM appear as parameter-table writes elsewhere: 0x73d5f0 finds a scene
  object **named "DASHBOARD"** (id 0x48534188_52414f42) and writes its
  ILUMCOLOR param — 8 interior banks carry such an object: 599xx_i, a10_i,
  atom_3_500_i, desmosedici_i, diavel_i, fatboy_i, fxx_evoluzione_i, rs6_i
  [DATA].)
- `mng_dashboard` (the module name string, 0x148b514) manages the **Flash
  GAUGES movie** (the 2D gauge overlay; its debug dump prints ENABLE / PAUSE /
  DRAW / INPUT flags and "Flash modifiers"). The debug actions "Flash
  Dashboard", "Screenshot of All Dashboards", "Screenshot of Current
  Dashboard" sit there.
- **OpenTDU2 today:** the port's materials already carry ILUMCOLOR / ILUMPARAM
  / HEADLIGHTSINTENSITY (the `-logcar` dump prints them, cinematic_car.cpp
  1312), and the freeride night dash follows the headlights port-natively —
  but the collected-value path (write `HeadlightsIntensity` = the car's light
  mode × `car_headlights_intensity` into every TD_DashBoard material, and the
  flash variant for the transition) is **not wired**: the port currently draws
  the dash art at its authored brightness. The dev collector + list
  (car+0x77e8) is the shape to port.

## 6. What the dev exe names that retail hides (strings sweep)

All in `TestDrive2DEV.exe`, none readable in retail: `HEADLIGHTS`,
`HEADLIGHTS BLIT`, `HeadLightsRT`, `HEADLIGHTS SWITCH DISTANCE`, `All cars
make a headlights call`, `All cars toggle headlights`, the HeadLight caustics
debug block, `mng_dashboard`, `td_dashboard.sob` / `td_flashdashboard.sob` /
`td_headlightcaustics.sob` / `td_lamp.sob` / `td_flare.sob` (the shader
libraries, registered in one table at 0x509969), `Flash Dashboard`,
`GAUGES`, `HeadLightAttributes`, the weather keys
`car_headlight_flare_intensity` / `car_poslight_intensity` /
`car_caustics_intensity` / `car_headlights_intensity` (+ the four `tunnel_`
variants) and `avatar_car_night_reflection_factor` (0x14de4b0 — the avatar is
lit by the car at night, an unported weather key).

## 7. Next steps (in dig order)

1. Decode the remaining ~40 table slots (0x722310..0x728910): the glow-node /
   flare writers. Each is small; the port wants the pop-up headlight one
   (retail FUN_00c67280's dev twin) for the cars that fold their lamps.
2. Find the per-frame writer of car+0x77e8 (the dashboard intensity list) and
   the flash trigger; then port the retail dashboard light into
   `CinematicCar` (write HEADLIGHTSINTENSITY on the TD_DashBoard /
   TD_flashdashboard materials instead of the port-native hack).
3. The "DASHBOARD" scene object's ILUMCOLOR writer (0x73d5f0 caller chain) —
   what drives the interior-room dashboards of the 8 banks that have it.
4. `HeadLightsRT` chain (0x4fd240 callers) — the full retail head-light blit
   pass; pairs with the port's TD_ShadowBox projection gap (spec §2.4).
