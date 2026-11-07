#include "shared.h"
#include "license_test.h"

namespace {
constexpr float kCountdown = 3.0f;
// A crossing further than this outside the gate is not an attempt at it (the
// road tests pass other roads' gates' planes far away) [INF].
constexpr float kAttemptDistance = 25.0f;
constexpr float kGateSlack = 1.0f; // metres beyond the half width still counted [INF: the car's width]
constexpr float kSlalomClear = 1.0f; // a slalom pole: the car's centre at least this far on the open side [INF: half a car]
} // namespace

const char *LicenseResultText( LicenseTest::eResult result ) {
    switch ( result ) {
    case LicenseTest::eResult::Passed: return "Passed";
    case LicenseTest::eResult::TooSlow: return "Too slow";
    case LicenseTest::eResult::MissedGate: return "Missed a gate";
    case LicenseTest::eResult::OutOfTime: return "Out of time";
    case LicenseTest::eResult::Abandoned: return "Abandoned";
    case LicenseTest::eResult::Beaten: return "Beaten";
    default: return "";
    }
}

bool LicenseTest::start( const std::string &challengeName ) {
    if ( !challenge.load( challengeName ) || challenge.getGates().size() < 2 ) {
        return false;
    }
    bTimeLimit = true;
    if ( challenge.getMode() == Ig2Challenge::ModeSpeed ) {
        OTDU_LOG_WARN( "LicenseTest: %s is a speed challenge: its scoring is not decoded\n", challengeName.c_str() );
        return false;
    }
    sequence.clear();
    const size_t numGates = challenge.getGates().size();
    for ( int32_t lap = 0; lap < challenge.getLaps(); lap++ ) {
        for ( size_t i = 1; i < numGates; i++ ) {
            sequence.push_back( i );
        }
        if ( challenge.isCircuit() ) {
            sequence.push_back( 0 ); // the start / finish line closes the lap
        }
    }
    gatesPerLap = static_cast<int32_t>( numGates - 1 ) + ( challenge.isCircuit() ? 1 : 0 );
    lapStart = 0.0f;
    lapStarts.assign( 1, 0.0f );
    visualHalfWidth.assign( numGates, 0.0f );
    passTimes.clear();
    events.clear();
    step = 0;
    stage = eStage::Countdown;
    result = eResult::None;
    countdown = kCountdown;
    time = 0.0f;
    bHasPrevious = false;
    bResultPending = false;
    OTDU_LOG_INFO( "LicenseTest: %s '%s' - mode %d, %zu gate(s) x %d lap(s) = %zu, target %.2f s, limit %.0f s\n",
                   challengeName.c_str(), challenge.getTitle().c_str(), challenge.getMode(), numGates - 1,
                   challenge.getLaps(), sequence.size(), challenge.getTargetMs() / 1000.0f, challenge.getLimitMs() / 1000.0f );
    return true;
}

int32_t LicenseTest::getGateAfterNext() const {
    return step + 1 < sequence.size() ? static_cast<int32_t>( sequence[step + 1] ) : -1;
}

Vector3f LicenseTest::getStartPosition() const {
    return challenge.getGates().empty() ? Vector3f() : challenge.getGates()[0].Position;
}

float LicenseTest::getStartHeading() const {
    if ( challenge.getGates().empty() ) {
        return 0.0f;
    }
    const Vector3f &forward = challenge.getGates()[0].Forward;
    return std::atan2( forward.x, forward.z );
}

int32_t LicenseTest::getCountdownNumber() const {
    if ( stage != eStage::Countdown ) {
        return -1;
    }
    return static_cast<int32_t>( std::ceil( countdown ) );
}

bool LicenseTest::getNextGate( Vector3f &outPosition, int32_t &outIndex ) const {
    if ( stage != eStage::Running || step >= sequence.size() ) {
        return false;
    }
    outPosition = challenge.getGates()[sequence[step]].getPassCentre();
    outIndex = static_cast<int32_t>( sequence[step] );
    return true;
}

void LicenseTest::finish( eResult newResult ) {
    result = newResult;
    stage = eStage::Finished;
    bResultPending = true;
    OTDU_LOG_INFO( "LicenseTest: %s %s in %.2f s (target %.2f s)\n", challenge.getName().c_str(),
                   LicenseResultText( newResult ), time, challenge.getTargetMs() / 1000.0f );
}

void LicenseTest::end( eResult newResult ) {
    if ( stage == eStage::Countdown || stage == eStage::Running ) {
        finish( newResult );
    }
}

void LicenseTest::abandon() {
    if ( stage == eStage::Countdown || stage == eStage::Running ) {
        finish( eResult::Abandoned );
    }
}

void LicenseTest::tick( float deltaTime, const Vector3f &carPosition ) {
    if ( stage == eStage::Countdown ) {
        countdown -= deltaTime;
        if ( countdown <= 0.0f ) {
            stage = eStage::Running;
            previous = carPosition;
            bHasPrevious = true;
        }
        return;
    }
    if ( stage != eStage::Running ) {
        return;
    }
    time += deltaTime;
    const int32_t limitMs = bTimeLimit ? challenge.getLimitMs() : 0;
    if ( limitMs > 0 && time * 1000.0f > static_cast<float>( limitMs ) ) {
        finish( eResult::OutOfTime );
        return;
    }
    const std::vector<Ig2Gate> &gates = challenge.getGates();
    if ( bHasPrevious && step < sequence.size() ) {
        // Crossing of the gate's plane between the last frame and this one
        // (either direction: the slalom gates face both ways [DATA]).
        // Measured from the middle of the opening: a slalom pole's opening is
        // beside the pole (Ig2Gate::getPassCentre), so driving straight down
        // the pole line misses it.
        const Ig2Gate &gate = gates[sequence[step]];
        const Vector3f centre = gate.getPassCentre();
        const Vector3f a( previous.x - centre.x, 0.0f, previous.z - centre.z );
        const Vector3f b( carPosition.x - centre.x, 0.0f, carPosition.z - centre.z );
        const float da = a.dot( gate.Forward ), db = b.dot( gate.Forward );
        if ( ( da < 0.0f ) != ( db < 0.0f ) && std::fabs( da - db ) > 1e-5f ) {
            const float t = da / ( da - db );
            const Vector3f hit = a + ( b - a ) * t;
            const Vector3f right( gate.Forward.z, 0.0f, -gate.Forward.x );
            const float lateral = std::fabs( hit.dot( right ) );
            bool bPassed = lateral <= getPassHalfWidth( sequence[step] );
            if ( gate.isSlalom() ) {
                // From the pole towards the opening: the car's centre has to
                // clear the pole by half a car (kSlalomClear) and stay inside
                // the opening (+ the slack) [INF].
                const float fromPole = hit.dot( right ) * ( gate.Side == 1 ? 1.0f : -1.0f ) + gate.Width * 0.5f;
                bPassed = fromPole >= kSlalomClear && fromPole <= gate.Width + kGateSlack;
            }
            if ( bPassed ) {
                // The lap / split readouts (HUD /HUD/checkpoints, /HUD/laps).
                GateEvent event;
                event.Step = static_cast<int32_t>( step );
                event.Time = time;
                // The split: this gate's time into the lap against the same
                // gate's time into the previous lap (negative = faster).
                if ( gatesPerLap > 0 && step >= static_cast<size_t>( gatesPerLap ) ) {
                    const size_t lap = step / gatesPerLap;
                    const float thisLap = time - lapStarts[lap];
                    const float previousLap = passTimes[step - gatesPerLap] - lapStarts[lap - 1];
                    event.Split = thisLap - previousLap;
                    event.bHasSplit = true;
                }
                passTimes.push_back( time );
                if ( challenge.isCircuit() && gatesPerLap > 0 && ( step + 1 ) % gatesPerLap == 0 ) {
                    event.LapTime = time - lapStart;
                    lapStart = time;
                    lapStarts.push_back( time );
                    event.bLastLapStarts = static_cast<int32_t>( ( step + 1 ) / gatesPerLap ) == challenge.getLaps() - 1;
                }
                events.push_back( event );
                step++;
                OTDU_LOG_INFO( "LicenseTest: gate %zu / %zu at %.2f s (%.1f m off centre)\n", step, sequence.size(), time,
                               lateral );
                if ( step >= sequence.size() ) {
                    // Time attack / licence test: the target time; race /
                    // speedtrap: finished inside the limit (no opponents, the
                    // trap score is not decoded) [INF].
                    const int32_t targetMs = challenge.getTargetMs();
                    const bool bTimed = challenge.getMode() == Ig2Challenge::ModeTimeAttack;
                    finish( !bTimed || targetMs <= 0 || time * 1000.0f <= static_cast<float>( targetMs ) ? eResult::Passed
                                                                                                          : eResult::TooSlow );
                    return;
                }
            } else if ( lateral <= kAttemptDistance ) {
                OTDU_LOG_INFO( "LicenseTest: gate %zu crossed %.1f m off centre (half width %.1f) from (%.1f, %.1f) to (%.1f, %.1f)\n",
                               step + 1, lateral, gate.Width * 0.5f, previous.x, previous.z, carPosition.x, carPosition.z );
                finish( eResult::MissedGate );
                return;
            }
        }
    }
    previous = carPosition;
    bHasPrevious = true;
}

int32_t LicenseTest::getLap() const {
    if ( gatesPerLap <= 0 ) {
        return 1;
    }
    return Min( static_cast<int32_t>( step ) / gatesPerLap + 1, challenge.getLaps() );
}

float LicenseTest::getPassHalfWidth( size_t gateIndex ) const {
    const std::vector<Ig2Gate> &gates = challenge.getGates();
    if ( gateIndex >= gates.size() ) {
        return 0.0f;
    }
    // The gate's own half width plus the car (kGateSlack), or the drawn arch
    // when it is wider (the road gates are 7 m in the files but the arch the
    // player sees spans the road: passing under it must count) [INF]. A
    // slalom opening ends at its pole: no slack on the pole side would be
    // asymmetric, so the slack is halved [INF].
    if ( gates[gateIndex].isSlalom() ) {
        return gates[gateIndex].Width * 0.5f + kGateSlack * 0.5f;
    }
    return Max( gates[gateIndex].Width * 0.5f + kGateSlack, visualHalfWidth[gateIndex] );
}

void LicenseTest::setVisualHalfWidth( size_t gateIndex, float halfWidth ) {
    if ( visualHalfWidth.size() < challenge.getGates().size() ) {
        visualHalfWidth.resize( challenge.getGates().size(), 0.0f );
    }
    if ( gateIndex < visualHalfWidth.size() ) {
        visualHalfWidth[gateIndex] = halfWidth;
    }
}

void LicenseTest::debugSkipTo( size_t newStep ) {
    if ( sequence.empty() ) {
        return;
    }
    newStep = Min( newStep, sequence.size() - 1 );
    while ( step < newStep ) {
        passTimes.push_back( time );
        if ( challenge.isCircuit() && gatesPerLap > 0 && ( step + 1 ) % gatesPerLap == 0 ) {
            lapStarts.push_back( time );
            lapStart = time;
        }
        step++;
    }
    bHasPrevious = false;
}

bool LicenseTest::takeGateEvent( GateEvent &outEvent ) {
    if ( events.empty() ) {
        return false;
    }
    outEvent = events.front();
    events.erase( events.begin() );
    return true;
}

bool LicenseTest::takeResult( eResult &outResult, float &outTime ) {
    if ( !bResultPending ) {
        return false;
    }
    bResultPending = false;
    outResult = result;
    outTime = time;
    stage = eStage::Idle;
    return true;
}
