#include "shared.h"
#include "units.h"

#include "game/game_options.h"

#include <cstdio>

namespace {
constexpr float kMilesPerKm = 0.621371192f;
constexpr float kFeetPerMetre = 3.280839895f;
} // namespace

bool IsImperialUnits() {
    return GetGameOptions().Units == 1;
}

void SetImperialUnits( bool bImperial ) {
    GameOptions &options = GetGameOptions();
    const int32_t units = bImperial ? 1 : 0;
    if ( options.Units == units ) {
        return;
    }
    options.Units = units;
    SaveGameOptions();
    OTDU_LOG_INFO( "Units: %s (%s)\n", bImperial ? "imperial" : "metric", bImperial ? "mph / mi" : "km/h / km" );
}

float SpeedForDisplay( float speedKmh ) {
    return IsImperialUnits() ? speedKmh * kMilesPerKm : speedKmh;
}

float KmForDisplay( float km ) {
    return IsImperialUnits() ? km * kMilesPerKm : km;
}

float MetresForDisplay( float metres ) {
    return IsImperialUnits() ? metres / 1000.0f * kMilesPerKm : metres / 1000.0f;
}

const char *SpeedUnitLabel() {
    return IsImperialUnits() ? "mph" : "km/h";
}

const char *DistanceUnitLabel() {
    return IsImperialUnits() ? "mi" : "km";
}

void FormatDistance( char *pBuffer, size_t size, float metres ) {
    if ( pBuffer == nullptr || size == 0 ) {
        return;
    }
    const float distance = std::abs( metres );
    if ( IsImperialUnits() ) {
        const float miles = distance / 1000.0f * kMilesPerKm;
        if ( miles >= 1.0f ) {
            snprintf( pBuffer, size, "%.1f mi", miles );
        } else {
            const int32_t feet = static_cast<int32_t>( distance * kFeetPerMetre + 0.5f );
            snprintf( pBuffer, size, "%d ft", feet );
        }
        return;
    }
    if ( distance >= 1000.0f ) {
        snprintf( pBuffer, size, "%.1f km", distance / 1000.0f );
    } else {
        snprintf( pBuffer, size, "%d m", static_cast<int32_t>( distance / 10.0f ) * 10 );
    }
}
