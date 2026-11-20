#include "shared.h"
#include "eden_wind.h"

#include "eden_weather.h"
#include "system/cmd_line.h"

#include <cmath>

// The template's structure with port [INF] constants (the TimeCycleElementWind
// values are defaults): the per-axis oscillators fold into one horizontal
// sway, the gust envelope uses the template's Fade In/Out = 1 s.
namespace {
constexpr float kBaseAlong = 42.0f; // m/s scale of the along-wind drift (the clouds' old value)
constexpr float kBaseCross = 21.0f; // m/s scale of the cross drift (the clouds' old value)
constexpr float kSwayPeriod = 7.0f;      // s, the oscillator period (template Period##B = 1 s reads as
                                         // a grass-scale flutter; the shared drift smooths over this)
constexpr float kSwayAmount = 0.18f;     // fraction of the base, the oscillator amplitude
constexpr float kGustInterval = 7.0f;    // s between gusts (template Interval (s)##G, unset)
constexpr float kGustDuration = 3.5f;    // s of gust (template Duration (s)##G, unset)
constexpr float kGustFade = 1.0f;        // s, template Fade In/Out (s)##G = 1
constexpr float kGustBoost = 1.6f;       // the gust multiplier ("Amplitude during Gust")
constexpr float kPhaseRate = 0.12f;      // rad per metre of drift (the grass phase entry)
} // namespace

Vector2f TduscWindBase( const WeatherPreset *pWeather ) {
    // clouds_cloud3dSpeed (0.6..1.6 in the data) is the retail scroll rate the
    // clouds and the fog drift already used; keep it as the base strength.
    const float speed = pWeather != nullptr ? Clamp( pWeather->getFloat( "clouds_cloud3dSpeed", 1.0f ), 0.0f, 4.0f )
                                            : 1.0f;
    return Vector2f( kBaseAlong * speed, kBaseCross * speed );
}

void WindModel::update( float deltaTime, const WeatherPreset *pWeather ) {
    if ( deltaTime <= 0.0f ) {
        return;
    }
    // Base drift + the template's oscillator (one sway axis; the two template
    // axes map to along/cross here).
    const Vector2f base = TduscWindBase( pWeather );
    clock += deltaTime;
    const float sway = sinf( clock * ( 6.2831853f / kSwayPeriod ) ) * kSwayAmount;
    // The gust envelope: after kGustInterval a gust ramps in over kGustFade,
    // holds kGustDuration, ramps out - "Amplitude during Gust" times the sway.
    static const bool bNoGust = CmdLine::HasArg( "-nowindgust" );
    gustTimer += deltaTime;
    if ( !bNoGust && gustPhase < 0.0f && gustTimer >= kGustInterval ) {
        gustTimer = 0.0f;
        gustPhase = 0.0f;
    }
    float gust = 0.0f;
    if ( gustPhase >= 0.0f ) {
        gustPhase += deltaTime;
        const float total = kGustFade * 2.0f + kGustDuration;
        // 0 -> 1 over the fade in, 1 through the hold, -> 0 over the fade out.
        const float t = gustPhase < kGustFade
                            ? gustPhase / kGustFade
                            : gustPhase > total - kGustFade ? ( total - gustPhase ) / kGustFade : 1.0f;
        gust = Clamp( t, 0.0f, 1.0f ) * kGustBoost;
        if ( gustPhase >= total ) {
            gustPhase = -1.0f;
        }
    }
    gustFactor = gust;
    // The target drift: the base swayed, stretched by the gust (the gust
    // blows the drift up to ~2.6x before the smoothing).
    const Vector2f target( base.x * ( 1.0f + sway + gust ), base.y * ( 1.0f + sway + gust ) );
    // Smooth: the template's oscillators are grass-scale; the shared drift
    // (clouds, fog, ocean) must not jitter at 7 s period - tau ~ 3 s.
    const float shape = 1.0f - expf( -deltaTime / 3.0f );
    windXZ.x += ( target.x - windXZ.x ) * shape;
    windXZ.y += ( target.y - windXZ.y ) * shape;
    driftXZ.x += windXZ.x * deltaTime;
    driftXZ.y += windXZ.y * deltaTime;
    phaseAccum += speed() * kPhaseRate * deltaTime;
}

float WindModel::speed() const {
    return sqrtf( windXZ.x * windXZ.x + windXZ.y * windXZ.y );
}

float WindModel::azimuthDeg() const {
    return atan2f( windXZ.y, windXZ.x ) * ( 180.0f / 3.14159265f );
}
