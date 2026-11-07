#include "shared.h"
#include "gps_guidance.h"

#include "game/map_screen.h"
#include "system/cmd_line.h"

namespace {
    constexpr float kNearGate =
        50.0f; // [+0x1cf4], set from 0xef01ec (FUN_00933d80)
    constexpr float kFarGate = 200.0f;       // 0xf4a258
    constexpr float kRightBelowDeg = 121.0f; // 0xf4aa70
    constexpr float kLeftAboveDeg = 241.0f;  // 0xf4aa68
    constexpr float kMetresToMiles =
        0.621504f; // 0xf4a200 (applied to metres: thresholds in 1/1000 mile)
    // The countdown thresholds (0xf4987c / 0xf4aa78 3000, 0xf4a190 2000,
    // 0xeddb24 / 0xf4a800 1500, 0xf49a38 1000, 0xf4a450 500) and their cues,
    // metric 9..5, imperial 14..10 [CODE].
    constexpr float kCountdown[5] = { 3000.0f, 2000.0f, 1500.0f, 1000.0f,
                                      500.0f };
    constexpr int32_t kMetricCue[5] = { 9, 8, 7, 6, 5 };
    constexpr int32_t kImperialCue[5] = { 14, 13, 12, 11, 10 };
    // Retail calls the turn law only when the next link belongs to another
    // road (0x9363bb, the road ids of FUN_00d3e710); the port's road graph has
    // no road ids, so the voice skips the junctions it would stay silent at
    // anyway (121..241 deg: within ~60 deg of straight on) and announces the
    // next one that turns [INF].
    // The HUD arrow shows the next junction within [+0x1cf0] = 200 m
    // (0xef0240, FUN_00933d40; the test at 0x937072) [CODE].
    constexpr float kArrowRange = 200.0f;
    constexpr float kWrongWayDelay = 2.0f; // [+0x84] > 2.0 (0xeddb40) [CODE]
    constexpr float kWrongWaySpeed = 3.0f; // m/s [INF]
    constexpr float kOffRouteDistance = 40.0f; // [INF]
    constexpr float kRecalcCooldown = 10.0f;   // [INF]

    // The retail threshold test (same shape in FUN_00934740 and FUN_00934a10):
    // the value went from above the gate to at / below it this frame, and
    // landed within the window of it (a jump across a gate - a teleport, a new
    // route - stays silent).
    bool Crossed( float previous, float current, float gate, float window ) {
        return previous > gate && gate >= current &&
               std::fabs( current - gate ) < window;
    }
} // namespace

bool GpsGuidance::load() {
    bImperial = CmdLine::HasArg( "-gpsmiles" );
    return voice.load();
}

void GpsGuidance::reset() {
    turnNode = -1;
    prevTurnDistance = -1.0f;
    prevRemaining = -1.0f;
    wrongWayTime = 0.0f;
    bWrongWaySaid = false;
    bOffRoute = false;
}

void GpsGuidance::say( int32_t cue, const char *pWhy, float value ) {
    OTDU_LOG_INFO( "GpsGuidance: %s (%.1f) -> cue %d\n", pWhy, value, cue );
    voice.play( cue );
}

void GpsGuidance::tickTurn( float angleDeg, float distance ) {
    if ( prevTurnDistance >= 0.0f ) {
        const bool bRight = angleDeg < kRightBelowDeg;
        const bool bLeft = angleDeg > kLeftAboveDeg;
        if ( Crossed( prevTurnDistance, distance, kNearGate,
                      prevTurnDistance ) ) {
            if ( bRight || bLeft ) {
                say( bRight ? GPSVoice::GPS_Right : GPSVoice::GPS_Left, "turn",
                     angleDeg );
            }
        } else if ( Crossed( prevTurnDistance, distance, kFarGate,
                             prevTurnDistance ) ) {
            if ( bRight || bLeft ) {
                say( bRight ? GPSVoice::GPS_NextRight : GPSVoice::GPS_NextLeft,
                     "next intersection", angleDeg );
            }
        }
    }
    prevTurnDistance = distance;
}

void GpsGuidance::tickCountdown( float remaining ) {
    const float scale = bImperial ? kMetresToMiles : 1.0f;
    const float current = remaining * scale;
    if ( prevRemaining >= 0.0f ) {
        const float previous = prevRemaining * scale;
        for ( int32_t i = 0; i < 5; i++ ) {
            if ( Crossed( previous, current, kCountdown[i], kNearGate ) ) {
                say( bImperial ? kImperialCue[i] : kMetricCue[i], "destination",
                     remaining );
                break;
            }
        }
    }
    prevRemaining = remaining;
}

void GpsGuidance::tick( const MapScreen &map,
                        const Vector3f &position,
                        const Vector3f &velocity,
                        bool bArrived,
                        float deltaTime ) {
    if ( bArrived ) {
        say( GPSVoice::GPS_Arrived, "arrived", 0.0f );
    }
    if ( !map.hasRoute() ) {
        arrow = kArrowEmpty;
        if ( bHadRoute ) {
            reset();
        }
        bHadRoute = false;
        return;
    }
    if ( !bHadRoute ) {
        reset();
        bHadRoute = true;
    }
    recalcCooldown = Max( 0.0f, recalcCooldown - deltaTime );

    float offset = 0.0f;
    Vector3f tangent;
    const float along = map.projectOnRoute( position, offset, tangent );
    if ( along < 0.0f ) {
        return;
    }

    // Leaving the route: MapScreen re-plans from the car every second.
    const bool bOff = offset > kOffRouteDistance;
    if ( bOff && !bOffRoute && recalcCooldown <= 0.0f ) {
        say( GPSVoice::GPS_Recalculating, "off the route", offset );
        recalcCooldown = kRecalcCooldown;
    }
    bOffRoute = bOff;

    // Wrong way: moving against the route direction.
    const Vector3f flatVelocity( velocity.x, 0.0f, velocity.z );
    const float speed = flatVelocity.length();
    if ( speed > kWrongWaySpeed &&
         flatVelocity.dot( tangent ) < -0.5f * speed ) {
        wrongWayTime += deltaTime;
        if ( wrongWayTime > kWrongWayDelay && !bWrongWaySaid ) {
            say( GPSVoice::GPS_WrongWay, "wrong way", wrongWayTime );
            bWrongWaySaid = true;
        }
    } else if ( speed > kWrongWaySpeed ) {
        wrongWayTime = 0.0f;
        bWrongWaySaid = false;
    }

    // The HUD arrow: the next junction, whatever it does.
    arrow = kArrowEmpty;
    for ( const MapScreen::RouteTurn &turn : map.getRouteTurns() ) {
        if ( turn.Along > along ) {
            if ( turn.Along - along < kArrowRange ) {
                arrow = turn.Arrow;
            }
            break;
        }
    }

    // The next junction ahead that turns.
    const MapScreen::RouteTurn *pNext = nullptr;
    for ( const MapScreen::RouteTurn &turn : map.getRouteTurns() ) {
        if ( turn.Along > along + 1.0f && ( turn.AngleDeg < kRightBelowDeg ||
                                            turn.AngleDeg > kLeftAboveDeg ) ) {
            pNext = &turn;
            break;
        }
    }
    if ( pNext != nullptr ) {
        if ( pNext->Node != turnNode ) {
            turnNode = pNext->Node;
            prevTurnDistance = -1.0f; // a new junction: no crossing from the
                                      // previous one's distance
        }
        tickTurn( pNext->AngleDeg, pNext->Along - along );
    } else {
        turnNode = -1;
        prevTurnDistance = -1.0f;
    }
    tickCountdown( Max( 0.0f, map.getRouteLength() - along ) );
}
