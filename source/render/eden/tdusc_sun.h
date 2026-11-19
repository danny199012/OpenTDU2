#pragma once

// TDUSC's hour-keyed sun path (docs/re/tdusc_weather_physics.md 3, the
// TimeCycleElementSun of the TILED_LEVEL template), ported for the
// `-tdsun` opt-in that replaces the retail per-preset `sundirection` step
// path (weather_spec.md:104: "the sun has no astronomical model") with a
// continuous one.
//
// Directly ported [DATA]:
//  - the azimuth track (Polar Coordinates component 2): 89.167 deg at 6.645 h
//    sweeping to 360 deg at 24 h (~15 deg / h, the solar hour angle) - the sun
//    rises east, sets west;
//  - the sunrise / sunset hours from the Intensity keyframes (0.5 -> 0 at
//    6.5/6.65, -> 10/100 at 6.75/6.8; -> 100 -> 0.5 at 19.7/19.9): rise
//    6.65, set 19.9;
//  - the Color keyframes: night (107;144;255)/255, day white, blended at the
//    same 5.65/6.7/19.7/19.9 hours;
//  - the Intensity track normalized to a 0..1 multiplier (0.5 night, 1 day).
// Not ported as-is [INF]: the Polar Coordinates component 1 (12.7..15.4,
// peaking at 14 h) does not decode to a plausible degree elevation, so the
// elevation is a sine between the ported rise/set hours with a peak set by
// `-tdsunpeak` (degrees above the horizon at noon, default 62 - a Hong
// Kong-like summer noon).

#include <cmath>

#include "core/math.h"

namespace TduscSun {

struct Sample {
    float dirX, dirY, dirZ;       // the direction the light travels (retail convention: y < 0 when the sun is up)
    float colorR, colorG, colorB; // multiplier applied on the preset's sun_diffuse
    float elevationDeg;
    float azimuthDeg;
};

static inline float clamp01( float v ) {
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

static inline float min2( float a, float b ) {
    return a < b ? a : b;
}

// hour: 0..24. false = night (the track says no sun; keep the retail value).
inline bool evaluate( float hour, float peakElevationDeg, Sample &out ) {
    constexpr float kRise = 6.65f;
    constexpr float kSet = 19.9f;
    if ( hour < kRise || hour > kSet ) {
        // Night: the template's Color keyframes hold the night value until
        // 5.65 / past 19.9, the intensity drops to 0.5 - the moonlight look.
        out.dirX = 0.0f;
        out.dirY = -0.3f; // the retail night keys keep y = -0.3 (low moonlight)
        out.dirZ = 0.0f;
        out.colorR = ( 107.0f / 255.0f ) * 0.5f;
        out.colorG = ( 144.0f / 255.0f ) * 0.5f;
        out.colorB = 1.0f * 0.5f;
        out.elevationDeg = 0.0f;
        out.azimuthDeg = 0.0f;
        return false;
    }
    // Azimuth: linear between the ported keys (6.645 h -> 89.167 deg, 24 h -> 360 deg).
    const float azimuthDeg = 89.167f + ( hour - 6.645f ) * ( ( 360.0f - 89.167f ) / ( 24.0f - 6.645f ) );
    // Elevation: the sine day arc between rise and set (the template's own
    // elevation component does not decode to degrees, see the header comment).
    const float dayT = clamp01( ( hour - kRise ) / ( kSet - kRise ) );
    const float elevationDeg = sinf( dayT * 3.14159265f ) * peakElevationDeg;
    const float el = elevationDeg * ( 3.14159265f / 180.0f );
    const float az = azimuthDeg * ( 3.14159265f / 180.0f );
    out.dirX = cosf( el ) * sinf( az );
    out.dirY = -sinf( el );
    out.dirZ = cosf( el ) * cosf( az );
    // Color: night blue (107;144;255) -> day white across the template's 5.65
    // / 6.7 hours, times the intensity track (0.5 at the edges, 1.0 mid-day).
    const float dayColor = clamp01( min2( ( hour - 5.65f ) / ( 6.7f - 5.65f ), ( 19.9f - hour ) / ( 19.9f - 19.7f ) ) );
    const float intensity = clamp01( min2( ( hour - 6.65f ) / ( 6.8f - 6.65f ), ( 19.9f - hour ) / ( 19.9f - 19.7f ) ) );
    out.colorR = ( ( 107.0f + ( 255.0f - 107.0f ) * dayColor ) / 255.0f ) * intensity;
    out.colorG = ( ( 144.0f + ( 255.0f - 144.0f ) * dayColor ) / 255.0f ) * intensity;
    out.colorB = 1.0f * intensity;
    out.elevationDeg = elevationDeg;
    out.azimuthDeg = azimuthDeg;
    return true;
}

} // namespace TduscSun
