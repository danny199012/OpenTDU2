#pragma once

#include <cstdint>
#include <random>
#include <string>

struct WeatherPreset;
class WeatherLibrary;

// The in-game clock and the weather it drives.
//
// The retail clock is in the encrypted part of the game (the pause screen's
// 12:36 / 18:14 came from there, docs/SURVEY.md), so the port runs its own:
// from the machine clock, 1 game second per real second by default, so the
// game's time of day is the player's (`-clocktime HH:MM` to start elsewhere,
// `-clockrate x` for a faster day/night cycle in a test run). The world is lit
// with the game_weathers.xmb preset nearest *below* that time - the file is a
// list of times of day ("00", "01.30", ... "23.20", docs/re/freeride_spec.md 5)
// - and the pause screen clock follows it.
//
// On top of the time, the file carries **intemperies** per weather: the Ibiza
// game_weathers.xmb has 7 for every one of its 24 times of the day - 0 is a
// short base block (16 tunnel / headlight values), 1 and 2 are the dry sets,
// 3, 5 and 6 are the rain / storm sets with rain_ground_global_intensity,
// _wet, _puddle_height, _ripple_* and their own clouds_cloud3dname (4 is the
// drying one: no wet ground, the ripples still on). The port used to read
// intemperies 0 and 1 only (eden_weather.cpp), i.e. it could never show rain,
// the wet ground or the puddles - which is why they were never seen. This class
// walks a small state machine over the full sets: a state lasts a few game
// minutes, dry weather longer than rain, and rain mostly decays into cloud
// before it clears. Everything it produces goes through the weather preset the
// renderer already reads (sky, sun, fog, clouds, wet ground), so nothing else
// needed a change.
//
// `-weather <name>` pins one preset (the old behaviour, `-intempery n` picks
// its variant), `-night` pins the darkest time. [INF: the transition weights and
// the durations - the retail scheduler is not decoded.]
class WeatherCycle {
public:
    struct Tick {
        const WeatherPreset *pPreset = nullptr; // what to light the world with
        bool bPresetChanged = false;            // a new preset: re-apply it
        bool bClockChanged = false;             // the clock's minute moved
    };

    // Reads the library (it must outlive the cycle: the presets are returned by
    // pointer) and the command line. Safe with an empty library.
    void load( const WeatherLibrary &library );

    Tick tick( float deltaTime );

    inline const WeatherPreset *getPreset() const { return pPreset; }
    int32_t getHour() const { return hour; }
    int32_t getMinute() const { return minute; }
    // True when the clock runs (the retail-driven tests pin a preset instead).
    inline bool isRunning() const { return !bPinned; }
    // `-weather` / `-night`: one preset stays, the clock stands still.
    void setPinned( const WeatherPreset *pPreset );

private:
    const WeatherLibrary *pLibrary = nullptr;
    const WeatherPreset *pPreset = nullptr;
    // Time of day in seconds (0..86400) and the minute it last showed.
    float secondsOfDay = 12.0f * 3600.0f;
    int32_t hour = 12, minute = 0;
    int32_t lastMinuteOfDay = 12 * 60;
    float rate = 1.0f;      // game seconds per real second
    bool bPinned = false;
    // The weather state (intempery of the current time preset) and when it ends
    // (seconds of day, so a clock rate changes nothing about the schedule).
    int32_t intempery = 0;
    float nextStateChange = 0.0f;
    std::mt19937 rng;
    // The preset the world is lit with right now (index into presets).
    int32_t presetIndex = -1;

    // The time preset at or before `minuteOfDay` (wrapping to the last one).
    int32_t findTimePreset( int32_t minuteOfDay ) const;
    // The preset for (time preset, intempery), falling back to a lower
    // intempery and then to the base when the island's file is short.
    const WeatherPreset *presetFor( int32_t index, int32_t wantedIntempery ) const;
    // Picks the next intempery, arms the timer and returns it.
    int32_t nextState();
};
