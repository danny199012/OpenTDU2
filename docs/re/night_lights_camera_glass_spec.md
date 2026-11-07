# Street lamps, the chase camera and the cockpit glass (and the car's light pools)

Session 2026-11-01 (`cline`), the owner's follow-up: *"speaking of lights I don't think cars actually
project light yet at night and street lights seem to also stay on during the day but with street lights
I believe there maybe 2 sources per 1 light … the california in 1 camera view a lot of it's detail seems
wrong or low poly but when you switch to in car cam it seems the true detail … in car view glass looks
dirty/dark and it's hard to see out … the cam doesn't sit correctly behind the car and when driving up
hills the cam gets lower"*.

--------------------------------------------------------------------------------
## 1. Street lamps **on during the day, two glows per lamp** — fixed

**What the world actually carries** (`-logmaterials`, Ibiza, day):

| material | shader | what it is |
|---|---|---|
| `SF_Light_Lamp` | **TD_Lamp** vs7/ps1, guid `c913741cbe17490d`, 1 slot `sf_light_c`, opaque | the lamp head's glow face — an *unlit* textured quad, so it is bright whatever the hour |
| `SF_Instc_B_Ligh` / `SF_Instc_B_Patc` | TD_Lamp | the same for the instanced lamp props (two more glow faces) |
| `FX_LIGHT#1` | TD_Sprite vs28/ps6, `light_fx`, additive | an additive halo sprite at the lamp |
| `SF_Light_D` / `SF_Light_N_Wind` | TD_Building | the post / the shop-window light (lightmapped, correct day and night) |

So every lamp has **two** additive glows (the TD_Lamp head *and* the light_fx sprite) and neither is
switched by the hour: that is both halves of the report. The retail engine has a lamp manager (the lights
of `islands/<island>/fx/streetlamps.xml` — `SF_LighA..J`, colour `#f8d6a6` + intensity in the alpha, near /
far, spot cone angles) which turns them on at dusk; the port has no lamp system at all.

**Fix (port rule, `[INF]` for the switch time):**

* `EdenRenderer::bNightLights` (new) is fed by `EdenSceneView::drawObjects` from the weather preset — the
  same day / night rule the car headlights already use: lamps lit when `Hour < 6 || Hour >= 20`.
* `EdenRenderer::draw` skips a **TD_Lamp** material while it is day, and skips the **`FX_LIGHT#…`** sprite
  always (the second glow at the same lamp post); `-lampflares` restores the sprite, `-lampday` keeps the
  lamps lit for an A/B. One log line per skipped material.

**Verified** (headless, same spot):

```
day  (-weather 14):    street lamp sprite 'FX_LIGHT#1' skipped (the double light source, -lampflares)
                       street lamp glow 'SF_Light_Lamp' skipped (day: the weather hour is 6..20)
                       street lamp glow 'SF_Instc_B_Ligh' skipped (day…) / 'SF_Instc_B_Patc' skipped (day…)
night (-weather 21.35): street lamp sprite 'FX_LIGHT#1' skipped (the double light source, -lampflares)
                       (no glow skipped: the lamp heads are lit)
```

## 2. The chase camera sinking on hills — fixed

`ChaseCamera::update` placed the eye at `carPosition + flatHeading * posOffset + worldUp * posH`. On a
slope the body pitches, its rear deck rises above the origin, and the camera stayed at a world-vertical
1.5 m — so the car climbed *over* the camera and the view read as "the cam gets lower".

Now the height component runs along the **car's own up axis**, followed with the same time constant as the
heading (so a bump does not shake it), and it is re-seeded on `reset()` (teleport / respawn):

```
up = normalized(lerp(up, carUp, blend))            // carUp = carWorld * (0,1,0)
eye = carPosition + lookHeading * posOffset + up * posH
target = carPosition + lookHeading * targOffset + up * (targH + lookPitch * pitchHeight)
```

`-logcameras` now prints `eye above car / ground clearance / up` once a second. Measured on flat ground:
`mode 20 eye above car 1.50 m, ground 1.69, up (0.00 1.00 -0.00)`; the offsets themselves are the car's
own `camerasingame.xmb` mode 20 (`pos -6.45 / 1.50`, `target 1.50 / 0.91`, fov 45→65 by speed), so the
framing is the data's — only the slope behaviour was the port's.

## 3. Cockpit glass dark / dirty — fixed with a port decision

The exterior `Glass` material carries `DETAIL California_Glass`, `DETAIL2 G_I` (opaque black base) and
**`DIRTCOLOR DIRT_SCRATCH`** (`docs/re/car_glass_spec.md` 1) — the scratch streaks visible from the
driver's seat in the port's cockpit view. Retail swaps that material for the interior bank's **`Glass_I`
carrier (TD_CarGlassDirt)** whenever the inside-view camera flag is set (`FUN_00c53d10`), and the port does
not instance `GLASS_I`.

Port rule: in the **inside views** (the existing `< 5 m` LOD test = cockpit / driver modes) the windscreen
and side-window glass nodes (`GLASS_F`, `WND_L1`, `WND_R1`; collected at load, 3 found on the California)
are hidden, so the view is clear; outside views keep them. `-keepglass` restores the authored glass for an
A/B. Verified: `CinematicCar: glass nodes 3`, and mode 23 reports `camera 1.13 m, LOD 4`.

**Next step (real fix):** instance `GLASS_I` (the interior bank's glass carrier, 7 `CUBEINT` + 2 `CUBEMAP`
slots) and swap it in exactly like `FUN_00c53d10` does, so the cockpit keeps a *clean* interior glass
instead of nothing.

## 4. Cars do not project light at night — **not fixed** (decoded but not implemented)

`CinematicCar` already collects the three projected light boxes (car slots 13 BREAKING, 14 REVERSING,
15 HEADLIGHT = `TD_ShadowBox` boxes, `headlightcaustics` / `breakcaustics` / `reversecaustics` of
`common_car.bnk`, the HEADLIGHT box 6 m ahead, 9.6 × 11.9 m) and writes their `INTENSITY`, but
`EdenRenderer::draw` **skips every TD_ShadowBox material** (`docs/re/car_lights_spec.md` 2.4): its pixel
shader projects the scene position through constants the original's shadow-box system writes every frame
(Var_VS reg 60 = the box's world-view-proj, Var_Scene_PS reg 0..7 = the ground projection) and the port
feeds none of them — `-shadowboxes` draws solid black.

Implementing the pools therefore means writing that constant block (world-view-proj is easy; the ground
projection and the depth test are the work) — a session of its own, in the renderer, not in the car. Until
then the car has its lamp glow meshes (the `?A_` caustics are hidden by the LOD code today — a cheaper
first step would be to show them at night) but no light on the road.

## 5. Night windows and the terrain lightmap — the frame `UserVector1` was hardcoded to day

Session 2026-11-02 (`cline`), after the owner's night drive: *"none of buildings seem to be lit, no
windows etc."* The weather was correct — at `-night` it logs `spot_exterior_lightmap_day_night (0.000 1.000)` —
so the day/night weights reached the CPU, but nothing on screen changed.

**The actual night switch is the frame user vector, not NIGHTLMAP.** `EdenSceneParams::UserVectors[1]` is
documented as `x = sun visibility, y = night lights factor` (`eden_scene_view.cpp`), but `setupScene` (and the
asset viewer's scene setup) hardcoded it to `(1, 0, 0, 0)` — every shader that reads the night factor saw
"day" at midnight.

Shader evidence (`otdu2_sob_tool <TD_Building.sob> -members`, then `spirv-dis`/`spirv-cross`):

* `TD_Building vs_92` reads `UserVector1`; it compares its **`.w`** against `cF0_def.z = 0.5` and writes the
  window output only when the test passes. With `.w = 0` the test is always false -> no window glow. `.w` is
  the "windows active" switch (day 0, night 1).
* The **`.y`** component feeds the terrain / heightmap night lightmap (`TD_HeightMap`: shadow map * x, night
  light map * 2 y), which is why the ground and building walls were also darker than retail.

**Fix:** `setupScene` and the asset viewer now build the vector from the weather:
`(1, nightWeight, 0, nightWeight)` with `nightWeight` = `spot_exterior_lightmap_day_night.y` (fallback: the
weather hour, lit 20:00-06:00). Day is unchanged: `(1, 0, 0, 0)`.

**Verified** (FREERIDE, `-night`, fixed timestep, frame 240):

* building spot `-tp 21796.3,24188.1`: the walls/ground gain the night bake and one window strip lights
  (`docs/images/night-fix-ab-s73.png`, panels: before / `.y` / `.y + .w`).
* tuning shop `-tp 12605.4,16698.1`: **rows of lit windows** on both sides of the street
  (`docs/images/night-windows-s73.png`).

### The NIGHTLMAP trail (what it is not)

The streamed island has **175 TD_Building materials with a NIGHTLMAP parameter** around one Ibiza spawn
(`SB_*_window`, `SB_*_wall`, ...), and none of them were registered for the day/night overwrite — only
cinematic sets and spot exteriors were. `EdenWorld` now has model load/unload callbacks and `EdenSceneView`
registers the streamed sector/area models (`-lognightlmaps` dumps them). But the building shaders **do not
read NIGHTLMAP** (their CTAB has only `MatParam`, `SpecParm`, `gWindowMapTexSampler`), so changing it is
pixel-identical — the registration is kept as the correct data plumbing, and `UserVector1` is what switches
the windows.

### Still open

* **Lamp colour**: the street lamp head is still the unlit `TD_Lamp` quad (`sf_light_c`) plus its baked
  lightmap; the retail `streetlamps.xml` colour `#f8d6a6` and the lamp light pools are not implemented. The
  owner's screenshot of "too yellow" was not received, so the exact mismatch is not measurable yet — re-share
  it (or say the spot + `-clocktime`) and compare against `docs/images/night-windows-s73.png`.
* **Car headlight / lamp ground pools** (`TD_ShadowBox`, spec §4 above) are still skipped: the constants are
  not written, so `-shadowboxes` draws solid black.

## 6. "The California looks low poly in the chase view"

Checked against the data: the port shows the **high resolution body** (`BODY_HR`, child of `D_CAR`); it
hides `BODY_MR` / `BODY_LR` (the medium / low bodies), `HARDTOP_MR/LR`, `HANIM_MR`, the `?A_` glow meshes
and the `Paint_LR` / `Plate_LR` materials — i.e. exactly retail LOD 3, which is what the retail uses
between 5 and 15 m (`docs/re/california_spec.md` 3). The interior visible through the windows *is* the low
resolution `COCK_LR` at that distance (retail LOD 3 does the same; LOD 4 = full `COCK_HR` only below 5 m,
`-carlod 4` forces it for an A/B).

**Correction 2026-11-05 (`win`): the `Paint_LR` / `Plate_LR` hide is per item, not per node.** The port
hid the *node* of every `Paint_LR` / `Plate_LR` item. The Q7 V12's `BODY_HR` carries one 136-vertex
`Paint_LR` prim by the head lamps (item 10 of 15, `-logcaritems BODY_HR`) [DATA], so the whole high
resolution shell was hidden. The bonnet, wings and pillars were gone, and only the doors, bumpers, glass and
lamps drew (the owner: "Q7 missing panels"). Now a node whose items are all low resolution paint still
hides; otherwise only those items are skipped (`EdenDrawParams::ForceHiddenItems`, and only for nodes the LOD
rule leaves visible: `BODY_MR` / `BODY_LR` are hidden whole anyway). The params set `bDeferBlended`:
without it a params draw records its blended items at once, out of the sorted pass, and the head lamp glass
darkened the lamps (that is also what the swinging wipers' params did). Measured against the old rule
(`-lrnodehide` = the A/B): Range Rover Sport, Mustang Fastback '68 and SLK 55 pixel-identical (mean diff 0.000),
Q7 whole (`docs/images/q7_body_before_after.png`).

So no LOD bug was found. The likely reading of "detail seems wrong" is the glass: the windows render as
near-black panels (the `G_I` black base plus the exterior reflection cube) and hide the cabin, while the
cockpit view shows the full interior model. If it still looks wrong with `-carlod 4` in the chase view,
the next things to compare are the paint / reflection binding (dynamic `CUBEMAP` vs the static cube,
`docs/re/car_glass_spec.md` 6-7), which is what "wrong detail" on a car body usually is in this port.

