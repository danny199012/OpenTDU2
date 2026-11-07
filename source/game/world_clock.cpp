#include "shared.h"
#include "world_clock.h"

#include "system/cmd_line.h"

#include <cstdio>
#include <ctime>

namespace {
// The -clock HH.MM override (tests / screenshots): parsed once, -1 when the
// flag is absent or malformed (the PC clock stands in).
bool FixedClock( int32_t &outHours, int32_t &outMinutes ) {
    static bool bRead = false;
    static int32_t hours = -1;
    static int32_t minutes = 0;
    if ( !bRead ) {
        bRead = true;
        if ( const char *pArg = CmdLine::GetArgValue( "-clock" ) ) {
            int32_t h = 0, m = 0;
            if ( sscanf( pArg, "%d.%d", &h, &m ) >= 1 ) {
                hours = Clamp( h, 0, 23 );
                minutes = Clamp( m, 0, 59 );
            }
        }
        if ( hours >= 0 ) {
            OTDU_LOG_INFO( "WorldClock: fixed time %02d:%02d (-clock)\n", hours, minutes );
        }
    }
    outHours = hours;
    outMinutes = minutes;
    return hours >= 0;
}

// The game clock (weather_cycle.h) publishes its time here every tick, so
// every reader (the pause menu, the traffic density hour, the weather pick of
// the modes without a cycle) follows the same running time.
int32_t gGameHours = -1, gGameMinutes = 0;

std::tm LocalTime() {
    const std::time_t now = std::time( nullptr );
    return *std::localtime( &now );
}
} // namespace

void SetWorldClock( int32_t hours, int32_t minutes ) {
    gGameHours = Clamp( hours, 0, 23 );
    gGameMinutes = Clamp( minutes, 0, 59 );
}

int32_t WorldClockHours() {
    int32_t hours = 0, minutes = 0;
    if ( FixedClock( hours, minutes ) ) {
        return hours;
    }
    if ( gGameHours >= 0 ) {
        return gGameHours;
    }
    return LocalTime().tm_hour;
}

int32_t WorldClockMinutes() {
    int32_t hours = 0, minutes = 0;
    if ( FixedClock( hours, minutes ) ) {
        return minutes;
    }
    if ( gGameHours >= 0 ) {
        return gGameMinutes;
    }
    return LocalTime().tm_min;
}

bool WorldClockIsDay() {
    // The pause clock's day/night icons [INF: 07:00..19:59 is day; the retail
    // threshold is the time of day system, ENC].
    const int32_t hours = WorldClockHours();
    return hours >= 7 && hours < 20;
}
