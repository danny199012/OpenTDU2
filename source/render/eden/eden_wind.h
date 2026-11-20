#pragma once

// TDUSC's TimeCycleElementWind (docs/re/tdusc_weather_physics.md 3, the
// TILED_LEVEL template) as a shared per-frame wind state. The template
// defines the structure - a base Direction (1;0;0) + Strength, per-axis
// oscillators (Amplitude##B/F/T over Period##B/F/T and Wave Length##B/T) and
// a gust envelope (every Interval (s)##G a gust of Duration (s)##G, faded in
// over Fade In (s)##G = 1 and out over Fade Out (s)##G, multiplying the
// amplitude by "Amplitude during Gust") - with the values left at defaults.
// The numbers below are therefore the template structure + port [INF]
// constants, chosen so the clear-weather base matches the drift the clouds
// and fog already had (42/21 m/s x clouds_cloud3dSpeed).
//
// One update per frame (EdenSceneView::renderOffscreen, before the world and
// cloud work); the consumers read the same smoothed state:
//  - grass (EdenWorld::buildGrassBatch): the sway strength and the
//    travelling phase entry of the grass law (grass_spec.md 7: the phase
//    carries no time - v0.z is that entry);
//  - blade grass (EdenBladeGrass::Frame.WindAmplitude): x (1 + gustFactor);
//  - volumetric clouds + cirrus + height-fog noise (EdenClouds::Params
//    windX/windZ, applyHeightFog): the drift vector;
//  - ocean wave drift (EdenOcean::setWaveDrift): speed + azimuth, unless the
//    CLI flags (-wavespeed / -wavedir / -nowavedrift) claim it.
//
// -nowindgust gates the gust envelope (the base drift stays weather-driven).

#include "core/math.h"

struct WeatherPreset;

struct WindModel {
    Vector2f windXZ = Vector2f( 42.0f, 21.0f ); // m/s, smoothed world XZ drift
    float gustFactor = 0.0f;                    // 0..1, the gust envelope (grass amplifies with it)
    Vector2f driftXZ = Vector2f( 0.0f, 0.0f );  // m, the INTEGRATED drift (cloud/cirrus offset; wind*time rocked when the speed pulsed)
    float phaseAccum = 0.0f;                    // rad, the travelling phase (grass v0.z)

    void update( float deltaTime, const WeatherPreset *pWeather );
    float speed() const;
    float azimuthDeg() const; // compass-style for the ocean drift

  private:
    float clock = 0.0f;      // s, the model's own oscillator clock
    float gustTimer = 0.0f;  // s, towards the next gust
    float gustPhase = -1.0f; // s into the current gust, < 0 = none
};

Vector2f TduscWindBase( const WeatherPreset *pWeather ); // the clear-weather drift (42/21 x cloud speed)
