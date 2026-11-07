#include "shared.h"
#include "fuel.h"

#include "database/gs_database.h"
#include "game/player_profile.h"
#include "system/cmd_line.h"

namespace {
// The two grades of the retail pump screen (loc 1636 / 1637).
constexpr float kPricePerLitre[2] = { 1.49f, 1.79f }; // [INF: not in the data]
// The curve's knots (km/h): stop-go, the typical speed, the best case, top speed.
constexpr float kSpeedCity = 0.0f;
constexpr float kSpeedTypical = 60.0f;
constexpr float kSpeedBest = 90.0f;
constexpr float kSpeedTop = 200.0f;
// The city penalty over `avg` at a standstill, and the fallbacks for the two
// fields the data leaves unset (see fuel.h).
constexpr float kCityFactor = 1.2f;
constexpr float kMinFallbackFactor = 0.7f;
constexpr float kMaxFallbackFactor = 2.5f;
// A min below this share of avg is the 0.1 placeholder, not a real figure.
constexpr float kMinPlaceholderShare = 0.25f;
// The range's smoothing: the distance over which the average moves 63 % of the
// way to the instantaneous consumption.
constexpr float kConsumptionAverageKm = 1.5f;
// A range above this is a data accident, not a tank (the largest tank is 133 L).
constexpr float kMaxRangeKm = 5000.0f;

float Lerp( float a, float b, float t ) {
    return a + ( b - a ) * Clamp( t, 0.0f, 1.0f );
}
} // namespace

float FuelConsumption( const CarPhysicsData &car, float speedKmh ) {
    float avg = car.GasConsAvg;
    float min = car.GasConsMin;
    float max = car.GasConsMax;
    if ( avg <= 0.0f ) {
        // Unset rows (some cars leave _gas_cons_avg 0): fall back to the largest
        // of the two, then to a sane 12 l/100 km.
        avg = Max( min, max );
    }
    if ( avg <= 0.0f ) {
        avg = 12.0f;
    }
    // `_gas_cons_min` is 0.1 on 266 of the 278 rows - a placeholder, not a
    // figure any car reaches. Treat it as unset and derive the best case from
    // the average instead (the old law used it at 0 km/h, which is how a full
    // tank came to read a 70,000 km range).
    if ( min <= 0.0f || min < avg * kMinPlaceholderShare ) {
        min = avg * kMinFallbackFactor;
    }
    if ( max <= 0.0f ) {
        max = avg * kMaxFallbackFactor;
    }
    if ( max < min ) {
        max = min;
    }
    const float speed = std::abs( speedKmh );
    if ( speed <= kSpeedTypical ) {
        // Stop-go: the worst per-100-km figure of the low range.
        return Lerp( avg * kCityFactor, avg, ( speed - kSpeedCity ) / ( kSpeedTypical - kSpeedCity ) );
    }
    if ( speed <= kSpeedBest ) {
        // The cruise: the car's best case.
        return Lerp( avg, min, ( speed - kSpeedTypical ) / ( kSpeedBest - kSpeedTypical ) );
    }
    return Lerp( min, max, ( speed - kSpeedBest ) / ( kSpeedTop - kSpeedBest ) );
}

float FuelGradeConsumptionFactor( int32_t grade ) {
    // v-power burns 3 % less than basic [INF].
    return grade == 1 ? 0.97f : 1.0f;
}

float FuelPricePerLitre( int32_t grade ) {
    return kPricePerLitre[Clamp( grade, 0, 1 )];
}

float FuelBill( int32_t grade, float litres ) {
    return std::floor( FuelPricePerLitre( grade ) * Max( litres, 0.0f ) * 100.0f + 0.5f ) / 100.0f;
}

FuelTank &GetFuelTank() {
    static FuelTank tank;
    // The profile holds the numbers (a port decision: the retail save has no fuel
    // field, docs/re/fuel_gas_station_spec.md 5).
    PlayerProfile &profile = GetPlayerProfile();
    tank.Litres = profile.FuelLitres;
    tank.Grade = profile.FuelGrade;
    return tank;
}

void InitFuelTank( const CarPhysicsData &car ) {
    FuelTank &tank = GetFuelTank();
    const float capacity = car.GasTankSize > 0.0f ? car.GasTankSize : 60.0f;
    if ( tank.Litres < 0.0f || tank.Capacity != capacity ) {
        // A new car (or a different one) starts full, like a delivered car.
        tank.Litres = capacity;
        tank.Grade = 0;
    }
    tank.Capacity = capacity;
    tank.Litres = Clamp( tank.Litres, 0.0f, capacity );
    // The average is per car: a new tank starts from the car's own curve.
    tank.AverageConsumption = 0.0f;
    tank.Consumption = FuelConsumption( car, 0.0f ) * FuelGradeConsumptionFactor( tank.Grade );
    tank.RangeKm = tank.Consumption > 0.01f ? Min( tank.Litres / tank.Consumption * 100.0f, kMaxRangeKm ) : 0.0f;
    PlayerProfile &profile = GetPlayerProfile();
    profile.FuelLitres = tank.Litres;
    profile.FuelGrade = tank.Grade;
    if ( const char *pLitres = CmdLine::GetArgValue( "-fuel" ) ) {
        tank.Litres = Clamp( static_cast<float>( atof( pLitres ) ), 0.0f, capacity );
        profile.FuelLitres = tank.Litres;
        OTDU_LOG_INFO( "Fuel: tank forced to %.1f / %.1f L (-fuel)\n", tank.Litres, capacity );
    }
    // The raw row and the curve the port actually uses (the min is a 0.1
    // placeholder on most rows, so the derived best case is the number to read).
    OTDU_LOG_INFO( "Fuel: '%s' tank %.1f L, row %.1f/%.1f/%.1f l/100 km -> city %.1f, 60 %.1f, 90 %.1f, 200 %.1f, "
                   "grade %d\n",
                   car.FileName.c_str(), capacity, car.GasConsMin, car.GasConsAvg, car.GasConsMax,
                   FuelConsumption( car, 0.0f ), FuelConsumption( car, 60.0f ), FuelConsumption( car, 90.0f ),
                   FuelConsumption( car, 200.0f ), tank.Grade );
}

bool BurnFuel( const CarPhysicsData &car, float distanceKm, float speedKmh ) {
    FuelTank &tank = GetFuelTank();
    if ( tank.Capacity <= 0.0f || CmdLine::HasArg( "-nofuel" ) ) {
        return false;
    }
    tank.Consumption = FuelConsumption( car, speedKmh ) * FuelGradeConsumptionFactor( tank.Grade );
    // The range follows a distance-weighted average, not the instantaneous
    // figure: a car that just pulled away from a light would otherwise show a
    // range that swings by hundreds of km between frames.
    if ( tank.AverageConsumption <= 0.0f ) {
        tank.AverageConsumption = tank.Consumption;
    } else {
        const float alpha = 1.0f - std::exp( -Max( distanceKm, 0.0f ) / kConsumptionAverageKm );
        tank.AverageConsumption += ( tank.Consumption - tank.AverageConsumption ) * Clamp( alpha, 0.0f, 1.0f );
    }
    const float used = tank.Consumption * distanceKm / 100.0f;
    const float before = tank.Litres;
    tank.Litres = Max( 0.0f, tank.Litres - used );
    tank.RangeKm = tank.AverageConsumption > 0.01f
                       ? Min( tank.Litres / tank.AverageConsumption * 100.0f, kMaxRangeKm )
                       : 0.0f;
    GetPlayerProfile().FuelLitres = tank.Litres;
    return before > 0.0f && tank.Litres <= 0.0f;
}

void FillTank( int32_t grade ) {
    FuelTank &tank = GetFuelTank();
    tank.Grade = Clamp( grade, 0, 1 );
    tank.Litres = tank.Capacity;
    // The range follows the new grade at once (the average is kept: the tank
    // was filled, not reset).
    if ( tank.AverageConsumption > 0.01f ) {
        tank.RangeKm = Min( tank.Litres / tank.AverageConsumption * 100.0f, kMaxRangeKm );
    }
    PlayerProfile &profile = GetPlayerProfile();
    profile.FuelLitres = tank.Litres;
    profile.FuelGrade = tank.Grade;
}

float FuelLitresToFill() {
    const FuelTank &tank = GetFuelTank();
    return Max( 0.0f, tank.Capacity - tank.Litres );
}
