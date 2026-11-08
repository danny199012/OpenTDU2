# Car lights (brake, reverse, blinkers, position lights, headlights) + California rear

Tags: **[CODE]** readable x86 (VA >= 0x6c1000) · **[DATA]** game files · **[SHADER]** .sob bytecode ·
**[INF]** inferred (reason given) · **[ENC]** SecuROM range (VA < 0x6c1000), not read.

Related: `california_spec.md` (node slots, LOD), `car_glass_spec.md`. The light driver shipped in
`source/game/cinematic/cinematic_car.{h,cpp}` (`collectLights` / `updateLights` / `applyLights`) is a port of
the upstream `335759a` implementation (`docs/re/upstream_335759a.md`), with the traffic half (section 6)
OpenTDU2's own.

## 1. Why the first cut (MATPARAM) was wrong

The lamp lens materials (TD_CarPaint_NM vs6-7/ps96 rear, ps24 front) carry 4 maps: DETAIL `california_lgt_of`
(lamp off), NORMAL `california_lgt_ts`, GLOSSLIGHT `california_lgt_g` (64x64 DXT5 masks) and TEXLIGHT
`california_lgt_on` (lamp on, bound to the shader's own TEXLIGHT sampler). ps96 [SHADER]:

```
m      = tex(GlossLight, uv) * (1,1,1,-1) + (0,0,0,1)        // (r, g, b, 1 - a)
emis   = tex(TexLight, uv) * dot(m * c17, 1)                // c17 = GlossFct (Var_PS c9..c17)
diffuse_term *= prod_i(1 - c17.i * m.i)                      // lit lamp: no diffuse / specular
```

**`GLOSSFCT` is the per frame light state** — not `MATPARAM`. The data ships `GLOSSFCT = (1, 1, 1, 1)`, i.e.
drawn as loaded *every lamp function is on at once* (round lamps glowing amber, lower triangles lit red /
white — what the first cut rendered, which read as "all lamps lit, brake lights on the wrong parts"). By day
with no pedal the game writes (0, 0, 0, 0) and the lamps show the dark red `lgt_of` art.

Channel meaning [DATA + INF]: from the masks of `california_lgt_g` and the TEXLIGHT colour under them
(`lgt_on`, mean RGB): **r** (421 px) red (185, 19, 18): lower red triangle + bottom strip → **brake**; **g**
(140 px) white (207, 196, 196): lower white triangle → **reverse**; **b** (499 px) headlamp reflector + rear
ring (191, 134, 133) → **position lights / headlights**; **1 - a** (80 px) amber (232, 154, 32): round lamp
centres → **blinkers**.

UV mapping of the California lens items (parsed from california.3dg, `tools_probe`-style walk): the round
rear clusters (LGT_RL / LGT_RR geometries, 140 verts) map onto the big pale lens at the top of `lgt_on` plus
a small piece at the amber dot; the third-brake-light strip (BODY_MR item of LGT_RL) maps to v 0.95..0.99 =
the red band at the texture's top edge; the front lens (LGT_FL geometry mat 2) maps to the white circle; the
side repeaters (BODY_HR / BODY_MR items) to the amber dot region. The big red disc of `lgt_on` is sampled by
the mid-LOD rear bumper lamps (BUMP_R_M / BUMP_R_L geometries with the LGT_RL / LGT_RR materials).

## 2. Light state -> materials

### 2.1 Light material groups [CODE + DATA]
`FUN_00c5ee20` (car setup): for every material whose name hashcode (8-byte additive fold) equals one of the 9
names of the table **0x00f7e700** — `LGT_FL, LGT_RL, LGT_RL_SK, LGT_FR, LGT_RR, LGT_RR_SK, LGT_R_S, LGT_L,
LGT_R` — it pushes (material, pointer to its GLOSSFCT) into the vector of group k at car +0x783c + k * 0xc.
California: LGT_FL, LGT_FR, LGT_RL, LGT_RR, LGT_RL_SK, LGT_RR_SK (the `_SK` variants ride the skinned roof
node HANIM_S — the rear lamps of the closed roof). Other cars carry whatever subset they have (F430 / 458:
4-5 materials; the driver collects by name, no per-model code).

### 2.2 Per frame logic FUN_00c588d0 (called from the car update FUN_00c62a70) [CODE]
`ctrl` = car +0x280 (driver / vehicle state). `table` = 0x01230500 + kind * 200, a table of member function
pointers filled by FUN_00e33810 with **encrypted** functions (0x452c00, 0x452f00, 0x4548e0, ... [ENC]); they
write the GLOSSFCT channels / node flags.

| call | argument |
|---|---|
| table+0x00 | 1 if `ctrl+0x1d0` (brake) and `ctrl->owner+0x79c8 != 0`, else 0 |
| table+0x38 | 1 if `ctrl+0x1d1` (reverse) else 0 |
| table+0x60 | headlights: mode 2 → flare + 1, mode 1 → flare, mode 0 / pop-up moving → 0 |
| table+0x68 | position lights: poslight (0 if < 0.05; 0 in mode 0); mode 2: the raw flare once the timer +0x1f24 > 0.2 s (0x00f49b58) |
| table+0x70 | 1 if `ctrl+0x1d9` (and mode != 0) — function not identified |
| table+0x28 / +0x30 | left / right blinker value (FUN_00c57ef0, §2.3) |

* `flare` = car_headlight_flare_intensity (DAT_00fd5234), 0 when < 0.05 (0x00f49838) or `ctrl+0x1d8`.
  `poslight` = car_poslight_intensity (DAT_00fd5244). Both from the weather (FUN_00c4e710 reads
  car_headlights / car_headlight_flare / car_caustics / car_poslight _intensity and their `tunnel_` variants
  every frame); the tunnel values are used when car +0x2acc >= 5.0 (0x00edf65c; the car is in a tunnel).
* `mode` = `ctrl+0x1d4`: 0 everything off (blinker value 0 too), 1 normal (weather driven), 2 headlights
  forced. Mode 2 with car_headlights_intensity > 0.1 starts the pop-up headlight animation (+0x1f1c /
  +0x1f1d, state +0x2524 = 3, FUN_00c67280: players +0xacf0 / +0xad60 sampled at +0x1f20, 0.2 s; the
  California has none).
* `ctrl+0x1dd` → car +0x178: police light bar (FUN_00c5a9a0: 4 lamps, phase timers +0x17c..+0x188 modulo
  +0x19c, writes GLOSSFCT of the bar material) — not a California part.
* `car+0x2adc < 1.0` hides every light node (flag 4) — fade / damage, not used.

Weather [DATA game_weathers.xmb, Ibiza, keys]: car_headlight_flare_intensity 1.0 from 21:00 to 05:45;
car_poslight_intensity 1.0 from 21:35 to 05:45; both 0.0 from 05:46 to 20:15 (keys 05.46 ... 21.30);
car_caustics_intensity 0.1 (21:35), 0.5 (22:00, 23:00), 1.0 (23:20 ... 04:00), 0.5 / 0.25 at dawn, 0 by day;
car_headlights_intensity 1.0 ... 2.0 at night, 0 by day; tunnel_car_* (global block): flare 1.0, poslight
1.0, caustics 0.65, headlights 2.2.

### 2.3 Blinkers FUN_00c57ef0(dt, value) [CODE]
value = 1 (0 in mode 0). Timer car +0x1f10: -1 (0x00ede404) while neither `ctrl+0x1db` (left) nor `+0x1dc`
(right); set to 0 when one starts (lamp lit at once); then `t = fmod(t + dt, 1.0)` (0xe2dc12, QWORD
0x00f44c70); lit while t < 0.6 (0x00ef2754): **period 1.0 s, 0.6 s on / 0.4 s off**. Left output x0 unless
+0x1db, right x0 unless +0x1dc, then table+0x28(left), table+0x30(right). Left / right are exclusive in the
player input (no hazard in readable code, `accessories_spec.md`).

### 2.4 Lamp glow meshes and projected light boxes
* Nodes `?A_<f>_<FL|FR|RL|RR>_..` (first character folded) with the additive Caustics materials (TD_CarPaint
  ps3: colour x gloss map, no intensity constant [SHADER]): f = **L** lights (Caustics_02 white, front),
  **S** stop (Caustics red, rear), **R** reverse (Caustics_02 white, rear), **W** warning (Caustics_04
  orange, front) [DATA]. Shown with their function [INF: their visibility is set by the [ENC] functions].
* Flare sprites `Fa_<c>_<pos>[n]` (TD_Sprite, FRL_<x> materials of light_fx / light_fxred) exist on 6 of the
  133 car banks (ac_427, charger_srt8_police, desmosedici, diavel, fatboy, viper_srt10_police) — the traffic
  models all have them (section 6).
* Car slots 13 BREAKING, 14 REVERSING, 15 HEADLIGHT (name table 0x00edef08) = TD_ShadowBox ps1 boxes (base
  texture breakcaustics / reversecaustics / headlightcaustics of common_car.bnk projected on the scene
  depth, additive, x `INTENSITY`) placed by the data (BREAKING behind the bumper, HEADLIGHT 6 m ahead,
  9.6 x 11.9 m) [DATA]. FUN_00c63a30 / FUN_00c638d0 [CODE]: v = car_caustics_intensity (tunnel_), 0 in mode
  0, +1.0 in mode 2; if v > 0.05 and +0x2524 == 0: BREAKING INTENSITY = v when `ctrl+0x1d0`, REVERSING when
  `ctrl+0x1d1` (FUN_00c5a4f0 writes INTENSITY of the slot materials, then the box is submitted); HEADLIGHT
  INTENSITY = v x (kind 1: 0.3, else 1).
  **OpenTDU2: the boxes are collected, kept visible per state and their INTENSITY is written, but the
  renderer still skips TD_ShadowBox** (the projection constants the original writes per frame — Var_VS reg
  60 = the box's world-view-proj, Var_Scene_PS reg 0..7 = the ground projection, decoded from the bytecode
  of TD_ShadowBox.sob vs_0/ps_1 — are not fed). Pools on the ground are future work.

## 3. Input -> state (OpenTDU2) [INF]
The writers of `ctrl+0x1d0 / +0x1d1` are not identified in readable code: brake = brake pedal > 0, reverse =
gear -1 (VehicleDynamics). Blinkers from the accessories (car_accessories.cpp, `ctrl+0x1db / +0x1dc`). Light
mode default 1 (ENC: the player value is not readable).

## 4. OpenTDU2
* `CinematicCar` (source/game/cinematic/cinematic_car.*): `setBrakeLights` / `setReverseLights` /
  `setIndicator(-1 off | 0 left | 1 right)`, `setHeadlights(bool)` (false leaves the mode weather driven,
  true forces it), `updateLights(dt)` (the blink timer 1.0 s / 0.6 on, the `-indicator left|right|hazard`
  and `-carlights 0|1|2` overrides, the high beam delay), `applyLights(pWeather)` at draw (GLOSSFCT =
  (brake, reverse, position, blinker of the side); front: brake / reverse 0, position = max with the
  headlights), `collectLights()` at load (the 9 material names, the `Fa_` / `FA_` glow nodes, the 3 light
  boxes). `-noflares` hides the flare sprites, `-nolightboxes` the boxes, `-logcarlights` dumps the
  materials at load and the state once a second. The first cut (session 11) wrote MATPARAM.x — that knob is
  gone; GLOSSFCT replaced it.
* FREERIDE (gm_freeride.cpp) feeds brake (pedal / handbrake) / reverse (gear) / headlights (the weather
  hour, `< 6 || >= 20`) every frame; the in-car menu's left / right entries blink the indicators; the night
  dashboard backlight follows the headlights.
* Traffic: section 6.

## 5. Results (FREERIDE, screenshots in the session scratch)
* Day, `-carlights 0`: the rear is the dark red `lgt_of` art (round clusters dark, lower lamps clear) —
  the first cut's always-lit look is gone.
* Night: the rear position rings lit (b channel), the front projector lamps + lens rings lit, amber side
  repeaters.
* Night + brake: the lower red triangles + the third brake light strip light (r channel).
* `-indicator left` (two `accleft` presses): the left round lamp's amber centre blinks 0.6 / 0.4, right
  dark, front + rear.
* Reverse (gear R): the white wedges (g channel), with the brake reds when both are asked.
* Traffic (night): front light + rear position sprites on every car; `-trafficbrake` lights the red stop
  sprites (day too), `-notrafficlights` off. `-trafficpark [-trafficparkface 1]` pins 3 cars in front.

## 6. Traffic lights (OpenTDU2)
The traffic models (islands/<island>/vehicules/traffic/*.bnk) carry no LGT_ materials — their lamp art is
baked into the body texture and the glow is the `FA_<L|B|S|R|W>_<FL|FR|RL|RR>[_n]` sprite nodes (TD_Sprite
FRL_F red / FRL_L, FRL_S, FRL_W white; the vertex colour tints S red and W orange). The shared EdenModel of
a type is drawn once per agent, so the per agent lamps go through `EdenDrawParams::ForceHiddenNodes` (a
per-draw hide list; EdenModel::draw skips those nodes): front lights (L) + rear position (B) at night, stop
(S) for a braking agent (a decel this frame holds the lamps 0.4 s), reverse (R) never; warning (W) blinks
the side of the link the AI takes at the next node (the retail cadence, 1.0 s with 0.6 on). The old
whole-type night toggle (`bLightsOn` / `FxNodes`) is gone; `-trafficbrake` forces the brake lamps for
screenshots, `-trafficparkface 1` turns the parked test cars away from the camera.

### 6.1 The police roof light bar (`Gyro`)
Only the police traffic bank has a roof bar: geometry `SP4` (0.92 x 0.30 m on the roof) with the material
`Gyro` (`TD_CarPaint`, GUID `183157b81c6c7357`) — DETAIL `gyro_of` (bar off: blue left, white centre, red
right), GLOSSLIGHT `gyro_g` (128x16 DXT5 mask) and TEXLIGHT `gyro_on` (128x16 DXT1) — and a `GLOSSFCT`
parameter. The mask's four channels are concentric rings around the two rotating reflectors of the bar:
**r** the centre, **g** the first ring, **b** the second, **a** the outer (the white patches of the
mask channels; the `gyro_on` colours under them are the blue / red lenses). The 16 additive glow quads
`FA_G_<A|B|G|R><1..4>` (material FRL_G) sit at the matching radii: **R** at the reflector centres, then
G, B, and **A** outermost. The data ships `GLOSSFCT = (1,1,1,1)`, i.e. every ring lit at once — the
"roof lights not accurate" look the port showed. `FUN_00c5a9a0` drives four lamps with staggered phase
timers [CODE]; the period itself is not in the readable data.

OpenTDU2: `PoliceLightBar` (`source/game/freeride/police_light_bar.h`) writes `GLOSSFCT = (R, G, B, A ring
intensities)` and hides the quads of the unlit rings per draw (`EdenDrawParams::ForceHiddenNodes`, the
shared model is drawn per agent). One ring at a time sweeps from the centre outward every 1.6 s [INF]
on the **dark `gyro_of` lens**, which is the retail wiring (the detail art is the unlit lens; GLOSSFCT
lights the moving reflector). `-policefullbar` binds the lit `gyro_on` art as the DETAIL instead (a
brighter test/demo look, not the decoded state). **The retail FA_G glow quads are not drawn**: the
port's TD_Sprite path places their billboards off the roof (blue/red blobs to the side of the car), so
`PoliceLightBar` hides them; `-gyrosprites` brings the swept quads back for debugging. Ambient police
cars keep the bar off and `Traffic::setPoliceLights` switches it on for a chase; the chase's cop model
sweeps while the cops exist. `-policebar` forces the bar on, `-trafficparkpolice` parks a police car
with the bar off (the A/B reference); `-trafficpark` plus `-policebar` parks the police car in front of
the camera with the sweep on.

## 7. The ground light pools (session 2026-11-02, port-native [INF])

The retail's visible headlight pool is the HEADLIGHT TD_ShadowBox
(headlightcaustics, 9.6 x 11.9 m, 6 m ahead - section 4 [DATA]); its renderer
path (Var_VS 60 WVP + Var_Scene_PS 0..7) is still untranslated. The s15 G/B
"headlight term" is NOT the dry-road mechanism (rain_shadow_spec B.7
correction: TD_Road ps_0 reads only R and A). The port draws port-native
pools instead: EdenSurfaceFx::updateLightPools renders one soft ground quad
per active cone, corners height-queried like the puddles, in the smooth decal
batch (warm white ahead, red behind on brake, white on reverse). The cones
come from the EdenHeadlightFrameCones / EdenRearLightFrameCones registries
(source/render/eden/eden_headlights.h): FREERIDE fills the player's cone from
carWorld when the lamps are on (intensity = weather car_headlights_intensity,
else 1.2) plus Traffic::appendHeadlightCones (the night gate of the FA_ front
lamps, nearest 8), the rear registry from areBrakeLightsOn/getReverseLights.
Verified: night lot A/B -noheadlights, pool region mean pixel diff 38.46
(docs/images/headlights_pool_s67.webp), brake hold A/B 8.40. Not done: the
TD_ShadowBox translation (retail art + per-function boxes), traffic brake
pools.

### 7.1 The pool falloff and the road offset (session 2026-11-02, owner report)

The owner on the merged build: *"headlight cast seems square and doesn't seem
to blend"*, and on Hawai *"not casted at all on road but fine on grass"*. Two
fixes:

- **The falloff**: the pools shared the tyre-track decal shader, whose
  straight per-axis shoulders + 48-cell hash grain read as a square stamp on a
  light quad. The pools now draw in their own batch with a falloff-mode push
  constant (`surfacedecal.frag` Params.z = 1): radial in the quad's own (u, v)
  space — an elliptical pool that reaches zero exactly at the rim, a gentle
  centre hotspot, no grain. The tracks keep their falloff (Params.z = 0).
- **The road offset**: the pool corners sample the heightfield
  (`EdenWorld::getHeight`), but the roads are sector-object meshes ABOVE it —
  on grass the two agree (the pool drew), on roads the pool was buried under
  the tarmac (Hawai, where the road embankments sit higher). FREERIDE now
  measures `getSurfaceHeight(car) - getHeight(car)` at the car every 0.15 s
  (the sector-collision walk is too heavy per corner), smooths it and feeds
  `EdenSurfaceFx::setRoadOffset`; every pool corner adds it. On grass the
  offset is ~0, under bridges it clamps to [-0.5, +4] m (the pools there stay
  imperfect). Island-agnostic (the same path runs on Ibiza / Hawai / casino).

### 7.2 The lamp emission and the indicator auto-cancel (session 2026-11-02)

- **-lampboost <f>** (default 1.4, 1.0 = the retail values): a multiplier over
  every GLOSSFCT lamp channel (brake, reverse, position, blinker) — the owner:
  *"can we tweak there emission ... indicators and stuff don't look bright
  enough"*. The channels are plain 0/1 gates and the lens read dimmer than the
  retail screenshots; the boost pushes them over 1.
- **The indicators auto-cancel** (the retail latch): indicating a side and
  steering that way (|steer| > 0.35 in the side's direction) arms the cancel,
  the steering back to centre (|steer| < 0.12) fires it — the real car's
  latching switch. The manual in-car-menu toggle is untouched; `-indicatortest`
 proves it headlessly (the blinker on at t = 1, a scripted left steer pulse
  2.5..4.5 s, the `CarLights: ... auto-cancelled` log line at ~4.5 s).

## 8. The lamp glows (session 2026-11-04, `win`, port-native [INF])

The owner: the indicators, brake lights and headlights *"don't seem to look accurate or even good"*. The lens
pass alone (GLOSSFCT x TEXLIGHT, section 2) gives a flat lit colour: the retail lamps also bloom, and the
port's post chain does not bloom them. Two changes:

- **The lens**: per function weights over `-lampboost` - stop x1.45, blinker x1.6, reverse x1.2 (the 21 W
  bulbs against the 5 W tail light) [INF].
- **The glow quads** (`CinematicCar::appendLampGlows` -> `EdenLampGlowFrame` -> `EdenSurfaceFx`, shader
  `lampglow.frag`): one camera-facing additive quad per lit lamp, a white-hot core and a tinted halo, strength
  falling off with the angle to the lamp's axis (nothing from behind, `Spread` edge on), a minimum apparent
  size far away. By day each function keeps a share (stop 0.55, blinker 0.7, reverse 0.5, headlamp 0.3,
  position 0) of its night strength. The anchors (`collectLampAnchors`, `-logcarlights` lists them):
  the glow nodes `?A_<L|S|R|W>_<pos>_` (headlamps, front blinkers, stop, reverse - mesh centre in node
  space) and the lamp items for what has no node (model space - the rear cluster node's pose would drop the
  rings onto the stop lamps): the rear cluster on its own node -> position ring + rear blinker, a wide rear
  strip on a body node -> the third stop light (a band of five halos), a tiny front lens on a body node -> a
  side repeater shining sideways. The headlamp glow of a side halves while its blinker is lit (the amber
  shares the cluster and was lost under the white).
- **The blinker** rises in ~35 ms and dies in ~110 ms (a bulb) - the retail 1.0 s / 0.6 s cadence is kept.

`-nolampglow` is the A/B. Evidence: `docs/images/car_lamps_glow_brake_disc.webp` (rows: night rear before,
after - blinker lit / off; night front and the hot discs; midday). 0 VUIDs with `-vk_enable_validation_layer`.
Not done: traffic cars keep their FA_ sprites only; the headlamp beam in fog / rain (a volumetric cone) and
the TD_ShadowBox translation are still open.
