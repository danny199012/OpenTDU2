#pragma once

#include "audio/gs_audio.h"
#include "core/math.h"

#include <memory>
#include <random>
#include <string>
#include <vector>

struct WeatherPreset;

// Thunder and lightning of the storm weathers.
//
// The data [DATA]: the rainy intemperies of game_weathers.xmb carry thunder_off_on,
// thunder_time_spacing_min_max, thunder_lightning_duration_min_max, thunder_flicker_duration_min_max,
// thunder_flicker_frequency_min_max and thunder_flicker_intensity_min_max; the island's
// sound/ambience folder carries eclair_low / mid / high (.wav, 9 files on Ibiza) next to the
// ambiences.xmb whose LightningExport[] is the retail table. The retail scheduler is [ENC], so the
// sequencing here is [INF]: a strike every thunder_time_spacing seconds, a flash that flickers for
// thunder_lightning_duration with thunder_flicker_frequency steps of thunder_flicker_intensity, and
// the thunder arriving after distance / 343 m/s - a close strike a sharp crack (eclair_high), a far
// one a rumble (eclair_low) - through the AmbienceLightning bfx client (17).
//
// getFlash() (0..1) is applied by the caller: light( preset, flash ) lifts the ambient, the fog and the
// sky of a copy of the preset, so the whole scene (terrain, sky, clouds, water) flashes together.
// -lightning forces a storm on any sky, -nolightning / -nothunder turns it off, -logstorm prints strikes.
class LightningStorm {
public:
    LightningStorm() = default;
    ~LightningStorm() { unload(); }
    LightningStorm( const LightningStorm & ) = delete;
    LightningStorm &operator=( const LightningStorm & ) = delete;

    // islandFolder: "<bank folder>islands/<island>/".
    bool load( const std::string &islandFolder );
    void unload();

    // shelter 0..1: under a roof the thunder is muffled and the flash dimmer.
    void update( float deltaTime, const WeatherPreset *pWeather, float shelter );

    inline float getFlash() const { return flash; }
    inline bool isStorm() const { return bStorm; }
    inline int32_t getStrikeCount() const { return strikes; }
    // A copy of `source` lit by the flash (call only while getFlash() > 0).
    static void light( WeatherPreset &out, const WeatherPreset &source, float flash );

private:
    struct Thunder {
        float Delay = 0.0f;
        float Volume = 1.0f;
        int32_t Group = 0; // 0 high (close), 1 mid, 2 low (far)
    };
    float random( float lo, float hi );
    void startStrike( const WeatherPreset *pWeather, float shelter );

    std::vector<std::shared_ptr<AudioClip>> clips[3];
    std::vector<Thunder> pending;
    std::vector<float> steps; // flicker levels of the strike in progress
    std::mt19937 rng{ 0x7a11e5u };
    float nextStrike = 8.0f;
    float strikeTime = 0.0f;
    float strikeDuration = 0.0f;
    float strikeBrightness = 1.0f;
    float flash = 0.0f;
    int32_t strikes = 0;
    int32_t lastClip[3] = { -1, -1, -1 };
    bool bStorm = false;
    bool bLog = false;
    bool bForce = false;
    bool bLoaded = false;
};
