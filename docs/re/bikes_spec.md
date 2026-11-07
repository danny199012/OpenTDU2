# Motorbikes — what the data has, the drivable prototype, and what is left

Session 76 (`cline`). TDU2 shipped with a bike layer that was cut before
release: the UI, the licence classes, the dealer strings and a full Ducati
Diavel bank are in the PC data, but the `DBCarPhysicsData` table kept only one
generic bike row. This is the groundwork for bringing them back.

--------------------------------------------------------------------------------
## 1. The data

### 1.1 The only bike row

`DBCarPhysicsData` has 279 rows; exactly one is a bike:

| field | value | note |
|---|---|---|
| `_id_car` | 666 | |
| `_car_file_name` | `Default_Bike` | bank `vehicules/high/default_bike.bnk` |
| `_model_name` | `DEFAULTBIKE` | |
| `_car_version` | `SportBike` | |
| `_vehiclecategory` | 19 | **Ma2** (`licenses.h`: 18/19 Ma1/Ma2, 20/21 Mb1/Mb2, 22 MC) |
| `_car_body` | 1 | the body style of a bike |
| `_newdyna` | **False** | the retail row drives through the Havok vehicle SDK, not edVehicle |
| `_mass` | 200 kg | |
| `_length` / `_width` / `_height` | 2010 / 812 / 1176 mm | |
| `_wheelbase` | 1398 mm | |
| `_track_front` / `_track_back` | 0 / 0 | one wheel per axle |
| `_tire_radius` | 64 | 0.32 m (the DB scale / 200) |
| `_default_rims` | 129 | 120/70 R17 front, 190/55 R17 rear, `Mv-Agusta/Brutal_*` art |
| `_max_theorical_speed` | 332 km/h | |
| `_acceleration_0_100_kmh` | 4.0 s | |
| `_power_bhp` / `_power_rpm` | 184 / 11700 | |
| `_torque_nm` / `_torque_rpm` | 115 / 9500 | |
| `_rpm_min` / `_rpm_max` / `_red_line` | 500 / 13000 / 13000 | red line == limiter |
| `_nb_gears` / `_final_drive_ratio` | 6 / 2.53 | 2.94 / 2.06 / 1.52 / 1.18 / 1.03 / 0.91 |
| `_suspension_rate_front` / `_rear` | 11 / 68.45 | kN/m, a placeholder (see 3.3) |
| `_hud_file_name` | `##` | no gauge bank; the port draws its placeholder gauge |
| `_tires_type` | `##` | no Pacejka row; the port falls back to `Default` |
| `_traction_control` | False | |

### 1.2 The banks

`default_bike.bnk` (295 KB) is **not a bike**: it holds the generic
`cars/generic/compact/compact.3dd` + `.3dg` placeholder (see
`otdu2_bnk_tool.exe list`). The only real bike model is `diavel.bnk` (3 MB):

```
diavel.3dd / diavel.3dg / diavel.2dm / diavel.dhk
diavel_max.xmb / diavel_min.xmb      torque curves (8 / 3 points)
diavel_color.2dm                     paint
diavel_glass / _disk / _logos / _mirror / _wheel_mr
tire_b_ducati / tire_g_ducati / ts_tire_ducati   tyres
front_end/gauges/ducati/diavel/int_digital.2db   the bike's digital dash art
```

The model has exactly two wheel nodes, `D_RIM_F` and `D_RIM_R`, on the centre
line (`x = 0`) at z +0.833 / -0.774. `diavel_max.xmb` peaks at 127 Nm at
8000 rpm and falls to 0 at 11000 rpm.

The cut bikes named in `docs/re/CROSSBUILD_PLAN.md` (MV Agusta Brutale / F4S,
Ducati S4R / Desmosedici / Hypermotard, Kawasaki ZX-10R / KX450F / KFX450R,
Triumph Speed) have **no physics rows**; only Diavel / Desmosedici /
default_bike / fatboy have models in the PC data.

### 1.3 The UI that is already there

The localization (`db_us.cpr`) carries the whole bike layer: `Bikes only`,
`Car/Bike`, `Bike showroom`, `Sportbikes`, `Streetbikes`, `Select your helmet
and biker gear`, `You have no bike to sell`, `Acceleration (bike)`, the
"bike(s) for sale" lines, the bike statistics, and the licence classes Ma1 /
Ma2 / Mb1 / Mb2 / MC (`licenses.h`). `GMShop` already sets `/:IsBike` — it used
to be hard-coded false and is now derived from `_vehiclecategory` 18..22.

--------------------------------------------------------------------------------
## 2. The prototype (`-bike`)

```
build-cline\bin\opentdu2-cline.exe -game C:\TDU2-Gamedata\TestDrive2 ^
    -gm FREERIDE -skipcine -nopad -fixedtimestep -bike
```

What it does:

* `CinematicCar::Desc::ModelBank` (new) lets the **model** and the **physics
  row** be different banks: `FileName` stays `Default_Bike` (the only bike
  row), `ModelBank = "diavel"` loads the Ducati model. `-bike` also clears the
  profile's plus-size rims (a car feature).
* `VehicleSetup::bBike` is set from `_vehiclecategory` 18..22.
* `BuildDatabaseWheels` / `setupVehicle` build **two** wheels in line (the
  front / rear `D_RIM_` nodes, `x = 0`); the load / inertia share is per axle
  (one wheel gets the whole axle share, not half of it).
* `VehicleDynamics` has a **bike balance controller**: two wheels in line are an
  unstable rigid body, so the attitude is driven toward yaw + lean + zero pitch
  every tick. The lean comes from the steering angle and the speed
  (full lock ~41° at 43 km/h), the yaw stays the physics' own (steering still
  turns the bike) and the pitch assist suppresses wheelies. `[INF]`
* `CinematicCar::fitOnGround` fits the **two** tires on the road (the plane fit
  needs three wheel nodes; a bike was never placed and the ray suspension
  started above the road), and `startDriving` re-fits once the collision sectors
  are streamed.
* Bikes get a tight chassis box from the DB size and a 0.15 m collision sphere
  (the car-sized fallback box held the bike off its wheels).
* The bike row's suspension is normalized to a 6 cm static sag and its throttle
  to 100 % (the row runs 50/50, and 68.45 kN/m at the rear launched the bike off
  its own spring on spawn).

Verified: the Diavel model loads and draws (two `D_RIM_` wheels, `int_digital`
dash art, `Shadow_Biker` shadow box), the bike stands and drives on the road,
the chase camera follows it, and the HUD falls back to the placeholder gauge.
`-vehtest -bike` runs on the flat plane: stable, no wheelie, but **0-100 22 s /
top 197 km/h** — the placeholder row's first gear reaches 197 km/h at the
13 000 rpm limiter and 6th is never useful. This is a data limitation, not the
controller.

--------------------------------------------------------------------------------
## 3. What is left

1. **A real bike data row.** `Default_Bike` is the only row; the Diavel needs
   its own `DBCarPhysicsData` entry (mass ~239 kg, 162 hp, 6 gears, real
   suspension rates, its own `_hud_file_name = Ducati/Diavel`). Without it the
   bike's performance cannot match the 332 km/h / 4.0 s it claims.
2. **The drive train multiplier.** Adding the missing primary reduction
   (×1.8) made the bike *slower* (0-100 22 → 57 s): the port's gear-ratio path
   scales wheel torque the wrong way for this row. Re-read `driveTrainStage` /
   `gearRatio` before tuning the bike.
3. **Rider and lean animation.** The Diavel bank has no rider / lean animation;
   the bike leans as a rigid body. Retail bikes have a rider pose and a lean
   animation (the cut bikes' `.anm` files are gone).
4. **The bike's digital dash.** `front_end/gauges/ducati/diavel/int_digital.2db`
   is in the bank but `_hud_file_name` is `##`, so the port draws its
   placeholder gauge. The GAUGES movie needs a bike layout and the `int_digital`
   dial.
5. **Sounds.** `default_bike_high.bnk` does not exist; the Diavel bank has no
   sound bank either. A bike engine loop would have to reuse / author one.
6. **Shop, licence and showroom wiring.** `/:IsBike` is set, but the shops have
   no bike rows (`_car_body == 1`), the bike licences (Ma1/Ma2/Mb1/Mb2/MC) have
   no tests, and the dealership's bike showroom needs a `DBCarShop` row.
7. **Camera.** The row's `_camera` is 666; `camerasingame.xmb` has no car 666,
   so the port uses the default chase `Params` (fov 45/65). A bike camera would
   sit closer and follow the lean.

Files: `source/game/cinematic/cinematic_car.{h,cpp}` (`Desc::ModelBank`, wheel
hub lift, `fitOnGround` two-wheel fit), `source/game/freeride/gm_freeride.cpp`
(`-bike`, `BuildDatabaseWheels`), `source/physics/vehicle_dynamics.{h,cpp}`
(`bBike`, balance controller, axle share), `source/physics/vehicle_data.cpp`
(bike detection, suspension / throttle / Diavel curve fallback),
`source/game/shop/gm_shop.cpp` (`/:IsBike`).

