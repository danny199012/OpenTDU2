#pragma once

#include <cstdint>
#include <random>
#include <string>

struct WeatherPreset;
class WeatherLibrary;

// The in-game clock and the weather it drives.
//
// The retail clock [CODE-dev, edSys2 SystemTimer in the dev build]: a game day is 10800 real
// seconds (3 h, SETDAYDURATION's initial value), the time of day is
// ((wall clock - 2007-01-01 00:00 local) mod 10800) / 10800 x 24 h - 8 game days per real
// day, the same real moment is the same game time everywhere, and nothing is saved.
// (`-clocktime HH:MM` starts elsewhere, `-clockrate x` / `-clock` leave the wall clock for a
// test run.) The world is lit
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

    // The weather SCHEDULE: on the retail clock (wall clock, 3 h game day) the intempery is a pure function of the
    // game-time block (20 game minutes) and the island, like the clock itself, so the house TV's forecast
    // (TDMEDIA.vmf) is the weather the player then drives in. [INF: the retail scheduler is not decoded; the
    // transition weights are ours.]
    static constexpr double kBlockGameSeconds = 20.0 * 60.0;
    // The block number at `gameSecondsAhead` game seconds from now (retail clock: game seconds = real x 8).
    static int64_t RetailBlock( double gameSecondsAhead = 0.0 );
    // The intempery (1 clear, 2 cloud, 3 / 5 / 6 rain / storm, 4 drying) of a block on an island (0 Ibiza, 1 Hawai).
    static int32_t ScheduledIntempery( int32_t island, int64_t block );
    // The hour of day at `gameSecondsAhead` game seconds from now on the retail clock.
    static int32_t RetailHour( double gameSecondsAhead = 0.0 );
    void setIsland( int32_t islandIndex ) { island = islandIndex; }
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
    float rate = 8.0f;      // game seconds per real second: the retail day is 10800 s [CODE-dev]
    // True when the time of day is read from the wall clock like the retail's edSys2 SystemTimer
    // (docs/state/main.md): no -clock / -clocktime / -clockrate given.
    bool bWallClock = false;
    float weatherClock = 0.0f; // game seconds since the run began (the weather schedule; never wraps)
    bool bPinned = false;
    int32_t island = 0;
    int64_t lastBlock = -1;
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
