#pragma once

#include <string>

struct CarPhysicsData;

// The car's tank. The shipped game leaves the feature disabled, but the data is
// all there (docs/re/fuel_gas_station_spec.md): every DBCarPhysicsData row has
// `_gas_tank_size` and `_gas_cons_min/avg/max`, the pump screen's strings are the
// localization ids 1635..1645, and the only gas pump model of the whole game is
// in the `gph_l2d` house garage. The port therefore lets the player fill the tank
// **at home** (in the house garage, where that pump is) and at any station placed
// from the mods overlay (`mod_props`, the `fuel` column of props.txt).
struct FuelTank {
    float Litres = -1.0f;     // < 0: not set yet - a car starts full
    float Capacity = 0.0f;    // L, DBCarPhysicsData _gas_tank_size
    int32_t Grade = 0;        // 0 Shell basic (loc 1636), 1 Shell v-power (1637)
    float Consumption = 0.0f; // l/100 km of the last burn (instantaneous)
    // The distance-weighted average of `Consumption` (a ~1.5 km time constant).
    // The range is read off this, not off the instantaneous value: at a
    // standstill the instantaneous law sits at its best-case end, which used to
    // print a 70,000 km range for a full tank.
    float AverageConsumption = 0.0f;
    float RangeKm = 0.0f;     // what is left at AverageConsumption
};

// l/100 km at a speed [INF: the retail law is not decoded]. The DB gives three
// numbers per car - `_gas_cons_min` / `avg` / `max` - and the curve is
//   avg x 1.2 at 0 km/h (stop-go), avg at 60, min at 90 (the best case), max at
//   200 (top speed).
// `_gas_cons_min` is a placeholder on 266 of the 278 rows (0.1 l/100 km, which
// no car does), so a min below a quarter of `avg` is treated as unset and
// replaced by `avg x 0.7`; `_gas_cons_max` is 0 on one row and then falls back
// to `avg x 2.5`.
float FuelConsumption( const CarPhysicsData &car, float speedKmh );

// The grade's effect on the burn: v-power is 3 % leaner than basic [INF: the
// retail grades have no decoded effect; the pump screen sells v-power "for
// better performances", so the port gives it the one number it can honour
// without touching the engine model].
float FuelGradeConsumptionFactor( int32_t grade );

// $ per litre [INF: the retail prices are not in the data - the price sign of the
// port's own station shows 1.49].
float FuelPricePerLitre( int32_t grade );

// The bill for `litres` at a grade, to the cent.
float FuelBill( int32_t grade, float litres );

// The player's tank (PlayerProfile::FuelLitres / FuelGrade, so it survives a
// reload: the retail save has no fuel field at all).
FuelTank &GetFuelTank();

// Fills a new tank from the car's row (full) and remembers the capacity.
void InitFuelTank( const CarPhysicsData &car );

// Burns `distanceKm` (the odometer delta) at `speedKmh`. Returns true when the
// tank just went empty: the caller cuts the throttle and shows "Breakdown"
// (loc 1644).
bool BurnFuel( const CarPhysicsData &car, float distanceKm, float speedKmh );

// Fills to capacity at a grade (the caller pays FuelBill first). The capacity
// comes from the tank (InitFuelTank set it from the car's row).
void FillTank( int32_t grade );

// Litres needed to top the tank up, and the bill for that.
float FuelLitresToFill();
