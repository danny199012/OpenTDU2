# Ambient traffic

Tags as in CLAUDE.md. First version: session 26 (`source/game/freeride/traffic.{h,cpp}`).

## Data
- `euro/bnk/islands/<island>/level/aiconfig.cpr` `[DATA]` decrypts with the existing XTEA `.cpr` code
  (`LoadCPRText`) to **XML** (`<AICONFIG version="8">`):
  - `VEHICLE_REPARTITION nb_zones="6"`: per `<ZONE>` a `<DENSITY>` of 13 `<NB_TRAFFIC hour= frequency=/>`
    points (hours 0, 2 ... 24; Ibiza zone 0 = none, 1 = 1-8, 2 = 0-1, 3 = 1-10, 4 = 1-9, 5 = 2-9) and a
    `<VEHICLE database_id= frequency_day= frequency_night=/>` weight per vehicle (ids 2001-2034 and
    3001-3014; only 3001-3014 have weights on Ibiza).
  - `TRAFFIC_RULES/CanTurnOnRightWithRedLight = false`, `POLICE_CAR_ID = 3009`.
- Vehicle ids 3001..3014 are `DBCarPhysicsData` rows with `_car_file_name = "##"` (placeholder); the
  model is `islands/<island>/vehicules/traffic/<_model_name><_version_name>.bnk` lower case, e.g.
  `Ibiza_` + `Police` -> `ibiza_police.bnk` (`.3dg` + `.3dd` + `.2dm` + maps, no player-car node layout).
  3001 low_a, 3002 ambulance, 3003 bus, 3004 firetruck, 3005 high_a, 3006 medium_a, 3007 medium_b,
  3008 offroad, 3009 police, 3012 van, 3013 low_b, 3014 taxi; 3010 / 3011 have no row on Ibiza.
  Hawai (ids 2001-2034): `islands/hawai/vehicules/traffic/<_model_name><_version_name>.bnk` (compact1-3, suv1-6, taxi1, police1, truck1-3, bus1, pompier1 ...), wired in session 30.

## What the port does `[INF]` unless noted
- Cars follow the island road graph (`Ibiza_ROAD_MAP.IA`) along the real curve of each link (`ROAD_DETAIL.IA`,
  `docs/re/roads_spec.md`): a link, then the straightest other link at the node (chosen 40 m ahead; a U-turn
  only at a dead end); right-hand lane (1.9 m); 40-68 km/h; only a real turn slows the car; brake for a car,
  the player or a chase cop ahead. **Session 61 drives the cars by pure pursuit instead of placing them on
  the curve** (see "Steering ... 2026-11-01" below).
- Density: hourly `NB_TRAFFIC` averaged over the populated aiconfig zones x **4.2** (was 1.6), max **56**
  (was 28); the mix is the zone's weights, day / night by hour (night < 6 or >= 20). Spawn 150-520 m from
  the player (or from 40 m behind him) on road links with ground, drawn inside **620 m**, removed only
  beyond **820 m** and never while drawn.
  `-trafficnear` uses a 25-110 m ring and a 190 / 280 m draw / despawn radius for test shots (`-notraffic`
  turns the whole system off).
- Orientation from four ground samples below 180 m (heading only further out); origin offset of the models
  is taken as the ground.
- Cost: update 0.4-0.6 ms for 29 cars (RTX 5070 Ti, 67-71 fps); the old headless software setup measured
  about +11 ms per frame with 15 cars.

## Steering, density, collisions, glass (cline, 2026-11-01)


## Junction logic (cline, 2026-10-31)
- `Traffic::chooseNextEdge` chooses the next link **~40 m before the node** (stored on the agent):
  the straightest option (smallest angle between the travel direction leaving the current link and the
  direction leaving the node on the candidate), plus 0..0.35 rad of noise so the traffic does not take the
  same road every time. A U-turn is excluded while another link exists and only remains at a dead end
  (the same link reversed). The old code picked a random link at the node, so cars turned at every junction.
- **Only a real turn slows the car**: the turn angle gives 4.5 m/s (90 deg) .. 13 m/s (13 deg), approached
  with `v^2/(2*6)`; straight-through cars keep their cruise speed. The old rule slowed every car to 7 m/s
  whenever any link end was within 18 m (green, straight or not), which produced phantom traffic jams.
- **Red signals stop in time**: the stop line is 3 m before the node and the permitted speed follows
  `sqrt(2*6*(remaining-3))`, so a car at 19 m/s starts braking early enough instead of reaching the line
  at speed (the old fixed 18 m zone could not stop it). **Since 2026-11-01 the stop line is 5.5 m**, i.e.
  outside the junction box, so the crossing (green) link passes in front of the waiting car.
- **Turn indicators**: the `FA_W_*` sprites blink on the side of the chosen link (`IndicatorNodes[0/1]`,
  left/right) at the retail cadence (1.0 s period, lit while t < 0.6), from the decision until 1.2 s after
  the node. `-notrafficlights` still turns every lamp off.
- **Police roof light bar**: `PoliceLightBar` (docs/re/car_lights_spec.md 6.1). Ambient police cars keep it
  off; `Traffic::setPoliceLights(true)` (the chase / `-policebar`) writes the GLOSSFCT ring sweep on the
  dark `gyro_of` lens (`-policefullbar` binds the lit `gyro_on` art instead, for a bright test shot). The
  retail FA_G billboards are misplaced by the port's TD_Sprite path and stay hidden (`-gyrosprites` shows
  them for debugging). `-trafficparkpolice` parks a police car with the bar off; `-trafficpark` +
  `-policebar` puts it in front of the camera with the sweep on.
- **Which type is the police car**: `TRAFFIC_RULES POLICE_CAR_ID` of the island's `aiconfig.cpr`
  (`Type::bPolice = bHasBar && policeCarId != 0 && type.Id == policeCarId`). A Gyro bar alone is not
  enough: Hawai's fire van (`pompier1`, id 2016) carries one too, and until session 78 the first
  bar-equipped type was taken as the police car (so `-trafficparkpolice` parked the fire van and a chase
  lit its bar). Bar-equipped non-police types keep the lens dark (`applyOff`) and their FA_G quads hidden.
  Session 78 on Hawai: `POLICE_CAR_ID = 2032` -> `police1`; Ibiza: 3009 -> `ibiza_police`.


## Not done
- Real traffic lights (models, the retail signal timing), lane changes,
  overtaking, the real zone lookup (`cruisingzones.bin`), horns / audio,
  damage, Casino traffic (by design: the island has no traffic at all) and
  a per-car glass damage state. Wheel spin is in since 2026-11-01
  (`-trafficwheel`); the front wheels still do not steer with the yaw rate.

## Collision with the player (session 28)
`Traffic::resolveCollision`: circles along the player car (3) and each traffic car (one per 1.5 m, radius
half the width, width 1.6-2.5 m from the length) `[INF]`; on overlap a restitution 0.2 impulse along the
contact normal between the player (`VehicleSetup::Mass`) and the traffic car (350 kg per metre of length)
plus a 3 m/s separation push; the player gets `VehicleDynamics::addVelocity`, the traffic car changes its
speed along its lane and stops for 2.5 s. 2D only: no angular response, no damage, no sound.
`-trafficselftest` rams the first car every 240 frames and logs the result (player dv about 10 m/s for a
12 m/s closing speed, the rammed car 18.9 -> 26.2 m/s).

## Junction signals (session 29) `[INF]`
No signal data was found in the road files. A node with 3+ links runs a rotating phase (9 s slot per link, 7 s
green, 2 s all red, per-node offset); the links on the axis of the green one (|cos| >= 0.7, both directions) go
together; a car on a red link stops 3 m before the node. `-notrafficlights` turns it off. Measured: 1-5 of 15
cars stopped at any time (without signals, 0-1). Nothing is drawn for the signals yet.

## Zones (open)
The 6 `aiconfig` zones are not the 2 cruising-zone polygons and no per-node / per-road zone field was found in
`ROAD_MAP.IA` (node words 1-9 and road words 1-11 checked) nor in `worldconfig.cpr`; zone 3 stays in use.

## Steering, density, collisions, glass (cline, 2026-11-01)

The owner: *"traffic's logic last i tested cars kinda didn't turn naturally but they just quick turned
90 degree ... i also want possibly more traffic density ... cars must have collision and not clip
through 1 another ... they all seem to have cracked like windows ... i also don't want cars to
disappear in front of you ... don't have cars running on a island that you're not actually on and
casino Island should never have traffic"*.

### Smooth turns: pure pursuit instead of the curve warp `[INF]`
The cars used to be *placed* on the link curve every frame (`lanePosition`), so at a node the yaw jumped
from the incoming tangent to the outgoing one - a 90 degree snap in one frame. Now the car is **driven**:

- `Traffic::pathAhead(agent, ahead)` is the steering target: the right-hand lane point `ahead` metres
  along the path, and past the node it continues on the chosen next link with the tangent and the lane
  offset **blended over 4 m**, so the target sweeps an arc through the junction.
- The look-ahead is `3.0 + 0.55 * speed`, clamped 5..12 m. The yaw error to the target gives a curvature
  and a yaw rate, limited by a 5.5 m/s^2 lateral budget (`maxRate = 5.5 / max(speed, 3)`, clamped
  0.4..2.6 rad/s); the speed is capped at `sqrt(5.5 / curvature)`.
- The car integrates `Position += forward(yaw) * speed * dt`, and its `Distance` progress is re-anchored to
  the path point closest to the car (gain 0.5, +-2 m per frame) - a corner is cut by a metre or two, and
  without the re-anchor the target would walk away and the steering would oscillate.
- The feed-forward stays: a real turn (`NextTurnAngle > 0.22 rad`) is approached at 5.5..14 m/s, and the
  dead-end U-turn still reverses on the same link.

Measured (29 cars, 3 s log): turn <= 0.96 rad/s (a 90 degree corner takes ~2 s; one frame is 0.016 rad
vs the old 1.57), worst distance from the lane 2.8 m.

### Lane side: the driver's right, not the camera's +X `[INF]`
The Eden camera is **right handed**: `LookAt` in `source/core/math.h` uses `s = forward x up`, and both
`walk_controller.cpp` ("Screen right: forward x up (-X at yaw 0)") and `chase_camera.cpp` ("screen right at -X for
forward +Z") state the consequence. Right of a heading `(tx, tz)` is therefore **`(-tz, tx)`**; the old lane code used
`(tz, -tx)`, i.e. the driver's left, so every traffic car drove the left lane of a right-hand-traffic island (the
owner's "cars are driving wrong side of the road"). Fixed in `traffic.cpp` (`pathAhead` lane offset, the right-turn
indicator test, `-trafficpark`'s columns, the near-miss probe) and the chase cops (`chase_mode.cpp`
`kCopLaneOffset`; its ground-sample axis follows the same correction). Measured after the fix: worst lane error
**0.20 m** (Ibiza, `-trafficnear`, 11 cars) / **0.08 m** (Hawai), 0 contacts, 0 stalls.

### Junction crossing: no clipping, no deadlock `[INF]`
- **Yield**: a car within 14 m of the node yields to a *moving* car (speed > 0.6 m/s) inside the 6.5 m
  junction box ahead, stopping 3.5 m before it; a car that is itself stuck there does not hold the others.
- **Red stop line** moved from 3 m to **5.5 m** before the node: the crossing (green) link drives through
  the box in front of the waiting car instead of into it.
- **Bumper-to-bumper follow distance**: `gap = ahead - (lenA + lenB)/2 - 2 m`, `limit = gap * 1.1`. The
  old fixed 6 m from the leader's *centre* put a follower inside a bus / van (the queue overlapped every
  frame: 181 contacts / 3 s measured before, 5 after).
- **Car-to-car separation** (`resolveAgentCollisions`, after the movement loop): the same circle model as
  the player collision (one circle per 1.5 m, radius = half the width, 1.6..2.5 m). The deepest overlap is
  pushed out along the contact normal split by mass (350 kg/m), plus a restitution 0.15 impulse on the two
  speeds and 0.6 s of brake lamps. `-trafficcrashtest` puts car 2 1.5 m into car 1 at 8 m/s: **5.0-6.9 m
  apart after 1 s** (they push apart instead of passing through); `-logtrafficcontacts` logs deep contacts.

### Density `[INF]`
`kCarsPerUnit` 1.6 -> **4.2**, `kMaxAgents` 28 -> **56**, and a road with traffic keeps at least 10 cars
around the player. Hour 14 (the average of the populated aiconfig zones, 6.75) is **28 cars** instead of
10.8; the spawn step fills up to 3 cars per 0.15 s. The ring is `[150, 520] m`, and a car may also appear
from **40 m when it is behind the player** (`setPlayerHeading`, the game mode passes the car's forward), so
the 52-75 % band straight ahead is never used (no car popping in across the screen). Cost with 29 cars on
the RTX 5070 Ti: update 0.4-0.6 ms (was 0.3), 67-71 fps.

### Draw radius vs despawn radius: nothing vanishes in view `[INF]`
The world draws to the camera far plane (40 km, fog to 2 km) but the traffic was culled at **420 m**, so
cars disappeared in plain sight. Now `viewRadius = 620 m` (drawn), `despawnRadius = 820 m`, and a car is
removed only when it is **not drawn** and beyond the despawn radius (counted in the 3 s log:
`removed (despawn N, no ground N, off road N)`). `-trafficnear` uses 190 / 280 m.

### Island gating `[DATA]`
- `casino` never has traffic: `Traffic::load` returns at once for that island (it has no aiconfig.cpr
  either - 2 road nodes, 3 links around the casino).
- `buildComponents()` labels the **connected components** of the road graph (flood fill); cars only spawn
  on, and are removed from, the component of the node nearest the player (looked up at 2 Hz). A second
  network in an island's graph (an islet; the casino island next to Ibiza sits at x ~2750 / z ~1470 while
  Ibiza's roads span 3294..37258 / 2043..31757) therefore never gets traffic. Ibiza: 1 component.

### The cracked windows: the damaged-glass art in DETAIL2 `[DATA+INF]`
The traffic glass materials (`Glass_F / Glass_ML / Glass_MR / Glass_R`, TD_CarPaint) carry **`glass_b`**
(common_car.bnk, 256x128 DXT5) in their **DETAIL2** slot: decoded, that texture *is* a starred / scratched
pane (a bullet-hole star, mean (76, 92, 101)) - the retail **damaged glass** art. The CarPaint glass shader
lerps the base toward DETAIL2, so every traffic car drew a cracked windshield. The player car's own glass
uses **`g_i`** (32x32 DXT5 opaque black) there, the clean glass base. `Traffic::neutraliseGlassArt()` binds
`g_i` into the DETAIL2 slot of those materials (`-trafficglassart` keeps the authored art for the A/B).
Measured on the same parked car: windshield patch mean grey **78.4 -> 53.2** (darker, clean). The retail
damage drive that would fade DETAIL2 in is in the encrypted code, so a per-car "some cars are damaged"
state is not wired.

`Traffic::assignEnvironment` is implemented now (it was an empty stub): the view's **dynamic environment
cube** goes into the CUBEMAP slot of the car families with `SPECPARM.z` / `RAMPPOW.z = 0`
(= `CinematicCar::assignEnvironment`, FUN_00d58a90, `docs/re/car_glass_spec.md` 3), and `draw()` writes the
weather's **ENVCOEFF** (`car_envmap_factor`, `car_sun_specular_factor`,
`car_fresnel_coeff_power_boost_specular`) exactly as `CinematicCar` does - the traffic glass had the
authored values, i.e. no reflection at all. 101 traffic car materials get the cube on Ibiza.

### Rolling wheels and the taxi lift `[INF]`
`WLR_FL/FR/RL/RR` (or `DISC_*` where the model has no wheel node - the taxi, bus, van) are collected per
type with their authored transform; `draw()` spins them about the axle through their own hub by
`distance / radius` (`-trafficwheel <deg>` pins an angle for a shot: the pixel diff of the two shots is the
rim rings only). The same node now gives the **ground lift** when there is no `WLR_FL`: the taxi had
`GroundLift 0.000` (riding 0.22 m low), now 0.220 m like the other cars.

### `-trafficnear` never worked `[CODE]`
`static const bool bNear = CmdLine::HasArg("-trafficnear")` at namespace scope is initialised **before**
`CmdLine::Initialize` runs, so it was always false - every "near ring" test shot since session 26 actually
used the far ring. Replaced by `IsNearMode()` (function-local static) / `SpawnMin()` / `SpawnMax()`.

### Verified
`-gm FREERIDE -skipcine -nopad -fixedtimestep -autoplay "60:hold=accel" -exitframe 1600` ->
`Traffic: 29 cars (29 drawn), 2 stopped, 6 indicating, 6 turning, 0 at red, turn 0.96 rad/s, lane 2.78 m,
0 contacts, 0 removed ..., target 28.3`; `-trafficnear` -> `2 removed (despawn 2)` (the despawn counter);
`-trafficcrashtest` -> `crashtest after 1 s: 5.01 m apart, speeds 0.14 / 3.56 m/s`; `-island casino` ->
`Traffic: island 'casino' has no ambient traffic`; `-trafficpark` with / without `-trafficglassart` ->
windshield grey 78.4 / 53.2. Screenshots: `docs/images/traffic-town-s61.webp`,
`traffic-glass-ab-s61.png`, `traffic-night-s61.png`.

### Still open
- Lane changes / overtaking (a car stuck behind a stopped one waits), horns / engine audio, wheel steering
  angle (the front wheels do not turn with the yaw rate), a per-car damage state for the glass, and the
  real `aiconfig` zone lookup.

