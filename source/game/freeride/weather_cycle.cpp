#include "shared.h"
#include "weather_cycle.h"

#include "render/eden/eden_weather.h"
#include "system/cmd_line.h"

#include <ctime>

namespace {
// The intemperies of game_weathers.xmb: the base block (0) is only 16 tunnel /
// headlight values, the full sets start at 1. From the Ibiza file: 1 and 2 are
// dry (cloud), 3 / 5 / 6 carry rain_ground_global_intensity 1 and the wet ground
// / puddle values, 4 is the drying one (no wet, ripples still on) [DATA].
constexpr int32_t kFirstIntempery = 1;
// The weights of the next state [INF: the retail scheduler is not decoded].
// From a dry state the weather mostly stays dry, from a wet one it decays
// through cloud instead of jumping to sun.
constexpr int32_t kDryChoices[] = { 1, 1, 1, 1, 2, 2, 3, 4 };
constexpr int32_t kWetChoices[] = { 1, 2, 2, 4, 5, 6, 3, 3 };
constexpr int32_t kNumChoices = sizeof( kDryChoices ) / sizeof( kDryChoices[0] );
// How long a state holds, in game minutes [INF].
constexpr float kClearMinutesMin = 8.0f, kClearMinutesMax = 16.0f;
constexpr float kCloudMinutesMin = 4.0f, kCloudMinutesMax = 9.0f;
constexpr float kRainMinutesMin = 2.0f, kRainMinutesMax = 6.0f;

bool IsWetIntempery( int32_t value ) {
    return value == 3 || value >= 5;
}

bool IsCloudIntempery( int32_t value ) {
    return value == 1 || value == 2;
}

void ParseHourMinute( const char *pText, int32_t &outHour, int32_t &outMinute ) {
    outHour = 0;
    outMinute = 0;
    if ( pText == nullptr ) {
        return;
    }
    // "14" / "01.30" - the same shape the game_weathers names use.
    if ( sscanf( pText, "%d.%d", &outHour, &outMinute ) < 1 ) {
        outHour = 0;
        outMinute = 0;
    }
    outHour = Clamp( outHour, 0, 23 );
    outMinute = Clamp( outMinute, 0, 59 );
}
} // namespace

void WeatherCycle::load( const WeatherLibrary &library ) {
    pLibrary = &library;
    pPreset = nullptr;
    presetIndex = -1;
    intempery = 0;
    bPinned = false;
    rate = 1.0f;
    rng.seed( static_cast<uint32_t>( std::chrono::system_clock::now().time_since_epoch().count() ) );
    if ( const char *pRate = CmdLine::GetArgValue( "-clockrate" ) ) {
        rate = Clamp( static_cast<float>( atof( pRate ) ), 0.0f, 1000.0f );
    }
    // -clock HH.MM (the GLM freeze hook): start there and stand still - a
    // frozen clock never reaches the next weather state, so the whole run is
    // that time of day (a night test does not have to wait either).
    if ( CmdLine::HasArg( "-clock" ) ) {
        rate = 0.0f;
    }
    // The clock: the machine's local time, unless -clocktime HH:MM says where to
    // start (a night test does not have to wait for the evening).
    {
        int32_t startHour = 12, startMinute = 0;
        const std::time_t now = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
        std::tm local = {};
#if defined( _WIN32 )
        localtime_s( &local, &now );
#else
        localtime_r( &now, &local );
#endif
        startHour = local.tm_hour;
        startMinute = local.tm_min;
        ParseHourMinute( CmdLine::GetArgValue( "-clocktime" ), startHour, startMinute );
        ParseHourMinute( CmdLine::GetArgValue( "-clock" ), startHour, startMinute );
        secondsOfDay = static_cast<float>( startHour ) * 3600.0f + static_cast<float>( startMinute ) * 60.0f;
        hour = startHour;
        minute = startMinute;
        lastMinuteOfDay = startHour * 60 + startMinute;
    }
    const std::vector<WeatherPreset> &presets = library.getPresets();
    if ( presets.empty() ) {
        OTDU_LOG_WARN( "WeatherCycle: no weather presets\n" );
        return;
    }
    // -weather <name>: one preset for the whole run (the old behaviour, what the
    // launch bats' -weather 21 / -weather 12 use); -intempery picks its variant.
    const int32_t forcedIntempery = Clamp( CmdLine::GetArgInt( "-intempery", kFirstIntempery ), kFirstIntempery, 8 );
    if ( const char *pWeatherName = CmdLine::GetArgValue( "-weather" ) ) {
        for ( size_t i = 0; i < presets.size(); i++ ) {
            if ( StringCmpI( presets[i].Name.c_str(), pWeatherName ) == 0 ) {
                presetIndex = static_cast<int32_t>( i );
                break;
            }
        }
    } else if ( CmdLine::HasArg( "-night" ) ) {
        // The darkest time of the day (the old -night test hook).
        for ( size_t i = 0; i < presets.size(); i++ ) {
            if ( presetIndex < 0 || presets[i].Hour < presets[static_cast<size_t>( presetIndex )].Hour ) {
                presetIndex = static_cast<int32_t>( i );
            }
        }
    }
    if ( presetIndex >= 0 ) {
        // Pinned: the time of the preset, its intempery (clear unless asked).
        intempery = forcedIntempery;
        setPinned( presetFor( presetIndex, intempery ) );
        OTDU_LOG_INFO( "WeatherCycle: pinned '%s' intempery %d at %02d:%02d\n",
                       pPreset != nullptr ? pPreset->Name.c_str() : "(none)", intempery, hour, minute );
        return;
    }
    // Running: the preset of the current time, starting dry (intempery 1, the
    // file's default set), and the first change a minute or two in so the
    // weather moves while the player drives.
    presetIndex = findTimePreset( lastMinuteOfDay );
    intempery = kFirstIntempery;
    pPreset = presetFor( presetIndex, intempery );
    nextStateChange = secondsOfDay + 60.0f + static_cast<float>( rng() % 120u );
    OTDU_LOG_INFO( "WeatherCycle: clock %02d:%02d, weather '%s' intempery %d, rate %.2f\n", hour, minute,
                   pPreset != nullptr ? pPreset->Name.c_str() : "(none)", intempery, rate );
}

void WeatherCycle::setPinned( const WeatherPreset *pNewPreset ) {
    pPreset = pNewPreset;
    bPinned = true;
    if ( pPreset == nullptr ) {
        return;
    }
    hour = Clamp( pPreset->Hour, 0, 23 );
    minute = Clamp( pPreset->Minutes, 0, 59 );
    secondsOfDay = static_cast<float>( hour ) * 3600.0f + static_cast<float>( minute ) * 60.0f;
    lastMinuteOfDay = hour * 60 + minute;
}

WeatherCycle::Tick WeatherCycle::tick( float deltaTime ) {
    Tick result;
    result.pPreset = pPreset;
    if ( pLibrary == nullptr || pLibrary->getPresets().empty() || bPinned ) {
        return result;
    }
    // The clock: real time by default (rate 1), so the game's day is the
    // player's. It wraps at midnight.
    secondsOfDay += Max( deltaTime, 0.0f ) * rate;
    if ( secondsOfDay >= 86400.0f ) {
        secondsOfDay = std::fmod( secondsOfDay, 86400.0f );
    }
    const int32_t nowHour = static_cast<int32_t>( secondsOfDay ) / 3600;
    const int32_t nowMinute = ( static_cast<int32_t>( secondsOfDay ) / 60 ) % 60;
    if ( nowHour != hour || nowMinute != minute ) {
        hour = nowHour;
        minute = nowMinute;
        result.bClockChanged = true;
    }
    // The time preset changes with the clock: the sun, the ambient, the fog...
    const int32_t newPresetIndex = findTimePreset( hour * 60 + minute );
    if ( newPresetIndex != presetIndex ) {
        presetIndex = newPresetIndex;
        pPreset = presetFor( presetIndex, intempery );
        result.bPresetChanged = true;
    }
    // The weather state expires after the duration nextState() gave it.
    if ( secondsOfDay >= nextStateChange ) {
        intempery = nextState();
        pPreset = presetFor( presetIndex, intempery );
        result.bPresetChanged = true;
    }
    result.pPreset = pPreset;
    return result;
}

const WeatherPreset *WeatherCycle::presetFor( int32_t index, int32_t wantedIntempery ) const {
    const std::vector<WeatherPreset> &presets = pLibrary->getPresets();
    if ( index < 0 || index >= static_cast<int32_t>( presets.size() ) ) {
        return nullptr;
    }
    const std::string &name = presets[static_cast<size_t>( index )].Name;
    // A short file (Hawai) may have fewer intemperies: take the highest that
    // exists, then the preset itself.
    const int32_t available = pLibrary->getIntemperyCount( name.c_str() );
    for ( int32_t wanted = wantedIntempery; wanted >= kFirstIntempery; wanted-- ) {
        if ( wanted < available ) {
            if ( const WeatherPreset *pVariant = pLibrary->findVariant( name.c_str(), wanted ) ) {
                return pVariant;
            }
        }
    }
    return &presets[static_cast<size_t>( index )];
}

int32_t WeatherCycle::findTimePreset( int32_t minuteOfDay ) const {
    const std::vector<WeatherPreset> &presets = pLibrary->getPresets();
    int32_t best = -1, bestMinute = -1;
    int32_t last = 0, lastMinute = -1;
    for ( size_t i = 0; i < presets.size(); i++ ) {
        const int32_t presetMinute = presets[i].Hour * 60 + presets[i].Minutes;
        if ( presetMinute > lastMinute ) {
            lastMinute = presetMinute;
            last = static_cast<int32_t>( i );
        }
        // The latest preset at or before the clock (the file is not sorted).
        if ( presetMinute <= minuteOfDay && presetMinute > bestMinute ) {
            bestMinute = presetMinute;
            best = static_cast<int32_t>( i );
        }
    }
    // Before the first preset of the day (a file without "00"): the last one of
    // the previous day.
    return best >= 0 ? best : last;
}

int32_t WeatherCycle::nextState() {
    const int32_t *pChoices = IsWetIntempery( intempery ) ? kWetChoices : kDryChoices;
    int32_t next = pChoices[rng() % static_cast<uint32_t>( kNumChoices )];
    // Never repeat the state: a wet repeat reads as a bug and a dry one hides
    // that the weather moved at all.
    if ( next == intempery ) {
        next = IsWetIntempery( intempery ) || intempery == 1 ? 2 : 1;
    }
    const float roll = static_cast<float>( rng() % 1000u ) / 1000.0f;
    float minutes = 0.0f;
    if ( IsWetIntempery( next ) ) {
        minutes = kRainMinutesMin + roll * ( kRainMinutesMax - kRainMinutesMin );
    } else if ( IsCloudIntempery( next ) ) {
        minutes = kCloudMinutesMin + roll * ( kCloudMinutesMax - kCloudMinutesMin );
    } else {
        minutes = kClearMinutesMin + roll * ( kClearMinutesMax - kClearMinutesMin );
    }
    nextStateChange = secondsOfDay + minutes * 60.0f;
    OTDU_LOG_INFO( "WeatherCycle: weather state %d -> %d for %.0f min (clock %02d:%02d)\n", intempery, next, minutes,
                   hour, minute );
    return next;
}
