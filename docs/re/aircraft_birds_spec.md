# Aeroplanes and birds

Written 2026-11-01 (Cline, `cline`). The owner: *"I want the world to be more
alive could we add airplane logic and random birds etc... flying in the skies
and stuff the assets should be somewhere best check the asset tool I want planes
to be flying into the airport landing and taking off and stuff and if airport has
terminals have a airplane parked up to a terminal so when we add the airport it
looks more realistic as I believe there may be cutscenes"*.

Tags as in `CLAUDE.md`: `[DATA]` game files · `[CODE]` readable executable ·
`[INF]` inferred or chosen, with the reason.

---

## 1. The data, and where it lives

Found with the asset tool's bank list (`otdu2_bnk_tool list`) and the authoring
paths every bank entry carries (`docs/re/asset_viewer.md` 1):

| File | What it is |
|---|---|
| `islands/<island>/scenarics/packs/world_aircrafts.bnk` | **the flight routes**: `entities.xmb` with 4 `Entity__AircraftPath` loops over 25 `Entity__Volume` path nodes `[DATA]` |
| `islands/<island>/scenarics/packs/aircrafts.bnk` | **which aircraft flies which route**: 2 `Entity__Actor__Aircraft` records, no geometry of their own `[DATA]` |
| `islands/<island>/exterior/eair_a.bnk` | the airport spot (`EAIR_A`, Ibiza International at 16780, 27512) — the terminal, the aprons and a **parked airliner**, plus the `M_LANDED` / `M_TAKE_OFF` / `C_INTRO_E_1` / `C_INTRO_E_2` / `C_FIRST_TAKE_OFF` cinematics `[DATA]` |
| `islands/<island>/fx/details/birds.bnk` | **the birds**: `birds.2dm` (5 sprite materials) and 5 64 x 64 DXT5 sprites, no geometry `[DATA]` |
| `level/commonworld.bnk` -> `library.3dd` | **the aircraft geometry**: the library scene has the nodes `FU_AIR_B`, `FU_AIR_C` and `CW_PLANE` (INSA hashes; `GetIdentifier64bit` of those names) `[DATA]` |

### 1.1 `world_aircrafts.bnk` `[DATA]`

`Entity__AircraftPath` fields: `PathPoints[] { positionVolume, Type }`,
`AircraftType`, `MinDistBetween2Aircraft`, `MaxDistBetween2Aircraft`,
`SpeedOnGround`, `SpeedOnAir`, `SoundOnGround[]`, `SoundOnAir[]`, `cine`.
`positionVolume` is the `__Hc` of an `Entity__Volume` in the same bank, whose
`orient_pos_scale.pos` is the world position of that path node.

Ibiza's four loops:

| Route | Type | Points (x, y, z) | Loop | Speeds | Spacing |
|---|---|---|---|---|---|
| `Path01` | 0 | RunWayStart (17285, **20.1**, 27757) -> RunWayEnd (16435, **20.1**, 28217) -> T1 (15809, 163) -> T3 (17255, 200) -> T5 (19627, 250) -> T6 (18150, 213) | 10.4 km | 15 / 85 m/s | 4-6 km |
| `Path_04` | 0 | RunWayStart_03 (30430, **60.7**, 11072) -> RunWayEnd_03 (31582, **57.1**, 10978) -> P9 -> P6 -> P4 -> P3 -> P1 | 17.7 km | 15 / 80 m/s | 3-5 km |
| `Path02` | 1 | RunWayStart_A (42529, **350**, 32104) -> RunWayEnd_A (353, 350, 35698) -> A1 -> A2 -> A3 -> A4 | 114 km | 15 / 90 m/s | 5-9 km |
| `Path03` | 1 | RunWayStart_02 (42780, **350**, 10120) -> RunWayEnd_02 (16974, 350, 36635) -> Path1Pt_00/03/07/08 | 106 km | 15 / 90 m/s | 5-9 km |

The airport's runway is 967 m long at 20 m altitude - the `EAIR_A` spot is at
19.8 m, so the two `Path01` ground points really are the runway. `Path_04`'s
"runway" is at 57-60 m on the other side of the island (a second airfield the
spot list does not have), and the `Path02` / `Path03` "runway" anchors are at
**350 m**, i.e. in the air: `Type` alone does not say "on the ground" `[INF]`.

### 1.2 `aircrafts.bnk` `[DATA]`

| `__Name` | `Type` | `ObjectInCommonRef` |
|---|---|---|
| `FU_Air_B` | 0 | `0x425f5249415f5546` = `"FU_AIR_B"` |
| `FU_Air_C` | 1 | `0x435f5249415f5546` = `"FU_AIR_C"` |

`ObjectInCommonRef` is an 8 character name packed in a u64 (the same
`GetIdentifier64bit` the rest of the engine hashes names with), and those are
**instance nodes of `commonworld.bnk`'s `library.3dd`** - the gallery scene the
`.pmi` V / O lists instance. `EdenWorld` already keeps `libraryNodes`
(`node hashcode -> node index`), so the aircraft is drawn with
`libraryModel.drawNodeItems()`, exactly like a parked car or a tree, and nothing
is uploaded per plane.

Measured bounds: `FU_AIR_B` 14.1 x 3.6 x 10.4 m (a **private jet**, the green /
white Paradise Airline livery of `fu_airb.2db`), `FU_AIR_C` 68.4 x 19.8 x 68.5 m
(a **747-size airliner**, `fu_airc.2db`).

### 1.3 `birds.bnk` `[DATA]`

`birds.2dm` has five materials - `noir` (crow), `faucon` (falcon), `mouette`
(seagull), `pigeon`, `moineau` (sparrow; its texture file is `moino.2db`) - each
with a `COLOR` texture slot and a `FADEALFA` parameter, and nothing else: the
birds are **sprites**. Decoded with `_tex2png.py`, `mouette.2db` is a seagull in
flight, `faucon.2db` a falcon. Nothing in the data says how many birds there are
or where they fly (`fx.ini` holds the grass counts, not the birds;
`modifiers.xmb` does not name them) `[DATA]`.

---

## 2. What the port does

### 2.1 The aeroplanes - `game/freeride/aircraft.{h,cpp}`

* `load()` reads the two banks, resolves each `AircraftType` to a library node
  (`EdenWorld::findLibraryNode`, `getLibraryNodeBounds`, `drawLibraryNode`, three
  small accessors added for this), and builds one **closed loop** per route.
* A path point is a **ground point** when the data marks it `Type 0` *and* its
  altitude is within 60 m of the terrain below it (one `surfaceHeight` query per
  point at load) `[INF]`: that is what separates `Path01`'s runway (terrain
  20.1 = point 20.1) from the 350 m anchors of the high circuits.
* The loop is resampled: **10 samples per point pair**, straight on a ground
  segment (a runway roll) and a uniform **Catmull-Rom** spline elsewhere, so a
  6 point, 114 km circuit is flown as a curve and not as a hexagon `[INF]`.
* A plane advances `Speed x dt` along the loop: **`SpeedOnGround` while its
  segment joins two ground points, `SpeedOnAir` otherwise**, ramped at 6 / 8
  m/s^2 instead of snapping. Wrapping the loop therefore reads as *landing*
  (the last air point descends onto the runway), *runway roll*, *take-off* (the
  runway climbs back into the air) - the shape the data was authored with.
* Heading from the tangent 45 m ahead (it turns *before* the corner); pitch from
  the climb of the next 60 m, clamped to 18 deg; **bank** from the coordinated
  turn law `tan(bank) = v * yawRate / g`, clamped to 32 deg, both zero on the
  ground.
* Planes per route: `loopLength / ((Min+MaxDist)/2)`, clamped to 1..3 (a 114 km
  loop with a 7 km spacing would be 16 aircraft). The first plane of a route
  **starts on its runway segment**, so an airport always has one landing or
  taking off rather than a distant speck.
* Drawn with the island's shared library model inside **9 km** (the high
  circuits are 350 m up and visible island-wide). The model turns about its
  bounds centre; on the ground its lowest point sits on the surface instead (the
  library object's wheels are the contact) `[INF]`.
* `-noaircraft`, `-logaircraft`, `-aircraftpark`, `-aircraftspeed <x>`.
* The engine sounds of the bank (`boeing_747_idle.wav` / `_in_flight.wav`,
  referenced by `SoundOnGround` / `SoundOnAir`) are **not** played yet.

### 2.2 The parked plane at the terminal - already there

`eair_a.bnk` (the airport spot) is streamed by `SpotExteriors` like any other
building, and its `Entity__Actor` `CF_Plane` is drawn by `SpotExterior` as a
prop (the same `plane_8F55FCC8EA60E834.3dd`, 975 KB of 3dg). **Nothing had to be
added**: `docs/images/airport_apron_parked_planes.png` is the Ibiza apron with
two Paradise Airline airliners standing at the terminal, taken with
`-noaircraft` (so they are the bank's own, not the flying system's).

### 2.3 The birds - `game/freeride/birds.{h,cpp}` + `resources/shaders/bird.*`

* `load()` parses `birds.2dm`, takes each material's `COLOR` slot hashcode,
  finds the `2db` whose name hashes to it, and keeps the five species with a
  wingspan (1.35 m seagull .. 0.28 m sparrow, `[INF]`, only the on-screen scale).
* Five flocks of 5..9 birds wander 25..70 m above the terrain around the player:
  a heading whose turn rate drifts (a flock circling a thermal), a ground
  re-query every 2 s (a flock climbs over a hill) and a recycle when it leaves
  1100 m - **always spawned 250..700 m away and never in front of the camera**,
  so a flock is never seen to appear `[INF]`.
* Each bird mills around its flock centre at its own rate with a slow bob and a
  flap phase; `-birdsnear` (close ring, low) and `-birdsfront` (in front of the
  camera) are the test hooks.
* Drawn as **camera-facing quads** of the sprite, wingspan-scaled, narrowed as
  the wings come up (`0.45 + 0.55 |cos(flap)|`), faded over the last third of
  the 800 m draw radius, in the scene's **blended pass** (`ePass::Effects`,
  next to the surface effects) with the port's own `bird.vert` / `bird.frag` -
  one draw per species, one texture binding each.
* Heavy rain grounds them (`rain_ground_global_intensity > 0.55`) and the sky's
  brightness tints them, so a flock at dusk is a dark silhouette.
* `-nobirds`, `-logbirds`.

### 2.4 The three traps this cost a session (worth knowing)

1. **`params.CameraDirection` and `EdenRotationFromQuaternion(camera.Rotation) * (0,0,1)`
   are both the *opposite* of the drawn view direction.** The only reliable look
   direction is `-row 2` of `params.View` (`-view.m[0][2], -view.m[1][2],
   -view.m[2][2]`), which is also what the surface effects use for their
   billboard basis. The birds take their spawn direction from the last **drawn**
   frame (`Birds::draw`) for exactly that reason.
2. **A float angle in this engine is a yaw only under `(x = sin, z = cos)`**,
   the pair of `atan2(dir.x, dir.z)` the traffic steers by. Mixing
   `(cos, sin)` for one offset and `atan2(x, z)` for the heading put every flock
   exactly 90 deg off the view, which reads as "the birds never appear".
3. **A pipeline is bound to one `VkRenderPass`.** The `Effects` pass draws into
   the scene render target, not the swapchain, so `PipelineDesc::RenderPass`
   must be `device.getCurrentRenderPass()` and not the default. `EdenSurfaceFx`
   still uses the default - its wheel spray is submitted every frame but may
   never reach the target; that is worth an A/B in a later session.

---

## 3. Verified (headless, `build-cline`)

* `-logaircraft` at the airport:
  `Aircraft: type 0 'FU_Air_B' -> library node 617, length 14.1 m` /
  `type 1 'FU_Air_C' -> library node 609, length 68.4 m`;
  `Path01 point 0 ... terrain 20.1 -> ground`, `point 1 ... ground`;
  `Path02/03 point 0 ... -> air`;
  `Aircraft: ibiza: 4 routes, 2 aircraft types, 11 planes (1 on a runway), view 9000 m`.
* Landings and take-offs: `-aircraftspeed 40` (a 12 s run = 8 game minutes of
  flight, `-fixedtimestep`) ->
  `Aircraft: 11 planes on 4 routes, 2 on the ground, 5 take-off / landing since the last line`.
* The model is drawn: `-aircraftpark` against `-aircraftpark -noaircraft`, same
  frame -> **mean pixel difference 30.0, 237,667 pixels of 921,600 above 8/255**
  (the 747 fills a quarter of the frame). `docs/images/aircraft_parked_terminal.png`.
* The parked plane of the airport bank:
  `SpotExterior: eair_a.bnk: 2 set models, 1 props` and
  `SpotExteriors: EAIR_A at (16780, 27512): 3 models, 1 placements`;
  `docs/images/airport_apron_parked_planes.png`.
* The birds: `Birds: species 'noir' -> 'noir.2db'` ... `'moineau' -> 'moino.2db'`,
  `Birds: ibiza: 5 species, up to 96 in 5 flocks of 5..9, draw 800 m`,
  `Birds: 35 drawn of 35, 5 flocks, rain 0.00, sun 1.00`; against `-nobirds`,
  same frame, `-birdsfront` -> **5302 pixels above 8/255, clustered in the sky**
  (a 40 x 40 block map of the difference shows isolated birds, not a rectangle).
  `docs/images/birds_sky.png`.
* Cost: 11 planes + 35 bird quads did not move the frame time measurably
  (`[perf] frame 120: avg 18.3 ms` with and without them).

## 4. Not done / next

* **The airport cinematics.** `eair_a.bnk` carries `eair_a_sc_M_LANDED`,
  `eair_a_sc_M_TAKE_OFF`, `eair_a_sc_M_INTRO_E_1`, `eair_a_sc_M_INTRO_E_2` and a
  `C_FIRST_TAKE_OFF` entity, with `cf_plane_takeoff.anm` / `cf_landed.anm`, the
  `M_LANDED` / `M_TAKE_OFF` `.sce` scenes and `m_landed_CINEMATIC.wav` /
  `m_take_off_CINEMATIC.wav`. They are spot cinematics like the car wash's
  (`docs/re/car_wash_spec.md`): the `CINVIEWER` mode and `CinematicScene` can
  already play a bank's `.cin` - the next step is to point them at this bank and
  its `M_LANDED` / `M_TAKE_OFF` entries, which is what the retail airport
  boarding flow uses. `otdu2_cine_tool` dumps the `.sce` shots.
* **The engine sounds.** `world_aircrafts.bnk` has `boeing_747_idle.wav` and
  `boeing_747_in_flight.wav` and every route names them in `SoundOnGround` /
  `SoundOnAir` (`rsc` + `rscType 10 rscSubType 6`); `GSAudio` + a 3D voice per
  plane, idle on the ground and in flight in the air, is a contained next step.
* **The aircraft's own moving parts.** The library object is rigid: no landing
  gear, no flaps, no spinning turbines. `FU_AIR_B/C` are single nodes, so per
  node transforms (found by name) would be needed.
* **A third aircraft type.** `aircrafts.bnk` has only `Type 0` and `Type 1`, and
  `Path02/03` (the 350 m circuits) use the 68 m airliner at 90 m/s; the
  `AircraftType` numbering is not a wider airliner mix, so a route's aircraft is
  exactly what the data says and nothing more.
* **Birds over water.** A flock can spawn over the sea (its ground query returns
  the sea floor) with a sparrow in it; a species / altitude bias by biome would
  fix that.
* **`EdenSurfaceFx` and the render-pass trap (2.4.3)** - check whether its
  quads actually land in the scene target; if not, the same one-line fix.


