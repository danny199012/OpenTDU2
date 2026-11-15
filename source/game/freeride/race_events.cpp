#include "shared.h"
#include "race_events.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "game/spots/spot_world.h"
#include "ig2_challenge.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <random>

namespace {
// The start spread: starts far apart cover the island (8 of them [INF]).
constexpr int32_t kNumStarts = 8;
constexpr float kMinStartDistance = 3500.0f; // between two starts, metres
constexpr float kRouteLength = 3500.0f;      // point to point length [INF]
constexpr float kCheckpointRadius = 22.0f;   // pass distance of a gate [INF]
} // namespace

// Every DBEvent row of the EV* family (_eventtype 1..8) is listed in scenarics/spotworld.bin with its world start
// position [DATA, docs/re/map_spots_spec.md section 12]. The duels and the story events (types >= 10) are not instant
// challenges.
std::vector<RaceEvents::RaceStart> RaceEvents::GetRealStarts( const std::string &island ) {
    std::vector<RaceStart> result;
    const int32_t islandIndex = SpotWorld::IslandIndex( island );
    if ( islandIndex < 0 || gpDatabase == nullptr ) {
        return result;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    const uint32_t count = root.getArraySize( "DBEvent" );
    for ( uint32_t i = 0; i < count; i++ ) {
        XmbValue row = root.getArrayElement( "DBEvent", i );
        const int32_t eventType = static_cast<int32_t>( row["_eventtype"].asInt() );
        if ( !row["_isvalid"].asBool() || eventType < 1 || eventType > 8 ) {
            continue;
        }
        if ( const SpotWorldEntry *pEntry = SpotWorld::Find( row["_eventfile"].asString(), islandIndex ) ) {
            RaceStart real;
            real.Position = pEntry->Position;
            real.Reward = static_cast<int32_t>( row["_moneyreward"].asInt() );
            real.Name = pEntry->Name;
            real.TimeLimitMs = pEntry->TimeLimitMs;
            if ( const char *pTitle = pEntry->NameLoc > 0 ? gpDatabase->getString( pEntry->NameLoc ) : nullptr ) {
                real.Title = pTitle;
            }
            result.push_back( real );
        }
    }
    return result;
}

bool RaceEvents::load( const std::string &island, const std::vector<Vector3f> &nodes,
                       const std::vector<std::pair<int32_t, int32_t>> &edges ) {
    unload();
    roadNodes = nodes;
    adjacency.assign( nodes.size(), {} );
    for ( const auto &edge : edges ) {
        if ( edge.first < 0 || edge.second < 0 || static_cast<size_t>( edge.first ) >= nodes.size() ||
             static_cast<size_t>( edge.second ) >= nodes.size() ) {
            continue;
        }
        adjacency[edge.first].push_back( edge.second );
        adjacency[edge.second].push_back( edge.first );
    }
    // The DBEvent rows (the retail events): their count and rewards shape the
    // starts; the table itself is logged with -logevents (its eventtype /
    // moneyreward / eventfile columns [DATA], the event files themselves are
    // not readable [ENC]).
    std::vector<int32_t> rewards;
    const std::vector<RaceStart> realStarts = CmdLine::HasArg( "-oldraces" ) ? std::vector<RaceStart>() : GetRealStarts( island );
    if ( gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        const uint32_t count = root.getArraySize( "DBEvent" );
        for ( uint32_t i = 0; i < count; i++ ) {
            XmbValue row = root.getArrayElement( "DBEvent", i );
            if ( !row["_isvalid"].asBool() ) {
                continue;
            }
            if ( CmdLine::HasArg( "-logevents" ) && rewards.size() < 40 ) {
                OTDU_LOG_INFO( "DBEvent %u: type %d reward %d file '%s'\n", i,
                               static_cast<int32_t>( row["_eventtype"].asInt() ),
                               static_cast<int32_t>( row["_moneyreward"].asInt() ), row["_eventfile"].asString().c_str() );
            }
            rewards.push_back( static_cast<int32_t>( row["_moneyreward"].asInt() ) );
        }
        OTDU_LOG_INFO( "RaceEvents: %u valid DBEvent rows\n", static_cast<unsigned>( rewards.size() ) );
    }
    if ( !realStarts.empty() ) {
        starts = realStarts;
        OTDU_LOG_INFO( "RaceEvents: %zu real starts on '%s' (scenarics/spotworld.bin)\n", starts.size(), island.c_str() );
        for ( size_t i = 0; i < starts.size(); i++ ) {
            OTDU_LOG_INFO( "RaceEvents: start %zu '%s' at (%.0f, %.0f) reward $%d\n", i, starts[i].Name.c_str(), starts[i].Position.x,
                           starts[i].Position.z, starts[i].Reward );
        }
        return true;
    }
    // Starts: greedily spread road nodes (high degree preferred: junctions
    // are plazas [INF]).
    std::vector<int32_t> candidates;
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        if ( adjacency[i].size() >= 3 ) {
            candidates.push_back( static_cast<int32_t>( i ) );
        }
    }
    if ( candidates.empty() ) {
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            candidates.push_back( static_cast<int32_t>( i ) );
        }
    }
    unsigned seed = 0;
    for ( const char &c : island ) {
        seed = seed * 31 + static_cast<unsigned>( c );
    }
    std::mt19937 rng( seed ); // deterministic per island: the starts stay put
    std::shuffle( candidates.begin(), candidates.end(), rng );
    for ( const int32_t node : candidates ) {
        if ( static_cast<int32_t>( starts.size() ) >= kNumStarts ) {
            break;
        }
        const Vector3f &position = nodes[node];
        bool bFar = true;
        for ( const RaceStart &start : starts ) {
            const Vector3f d( start.Position.x - position.x, 0.0f, start.Position.z - position.z );
            if ( d.length() < kMinStartDistance ) {
                bFar = false;
                break;
            }
        }
        if ( !bFar ) {
            continue;
        }
        RaceStart start;
        start.Position = position;
        start.Reward = rewards.empty() ? 500 : rewards[starts.size() % rewards.size()];
        start.Name = "Instant challenge";
        starts.push_back( start );
    }
    OTDU_LOG_INFO( "RaceEvents: %zu starts on '%s'\n", starts.size(), island.c_str() );
    for ( size_t i = 0; i < starts.size(); i++ ) {
        OTDU_LOG_INFO( "RaceEvents: start %zu at (%.0f, %.0f) reward $%d\n", i, starts[i].Position.x, starts[i].Position.z,
                       starts[i].Reward );
    }
    return !starts.empty();
}

void RaceEvents::unload() {
    starts.clear();
    adjacency.clear();
    stage = eStage::Idle;
    checkpoints.clear();
}

int32_t RaceEvents::findStartNear( const Vector3f &position, float radius ) const {
    int32_t best = -1;
    float bestDistance = radius;
    for ( size_t i = 0; i < starts.size(); i++ ) {
        const Vector3f d( starts[i].Position.x - position.x, 0.0f, starts[i].Position.z - position.z );
        const float distance = d.length();
        if ( distance < bestDistance ) {
            bestDistance = distance;
            best = static_cast<int32_t>( i );
        }
    }
    return best;
}

void RaceEvents::buildRoute( int32_t startNode ) {
    // A random walk on the road graph until kRouteLength, checkpoint at
    // every turn node (greedy, no revisit of the immediately previous node).
    checkpoints.clear();
    int32_t current = startNode;
    int32_t previous = -1;
    float travelled = 0.0f;
    std::mt19937 rng( startNode * 7919 + static_cast<int32_t>( starts.size() ) );
    Vector3f lastCheckpoint = roadNodes[startNode];
    auto pushCheckpoint = [&]( int32_t node ) {
        const Vector3f segment = roadNodes[node] - lastCheckpoint;
        travelled += segment.length();
        lastCheckpoint = roadNodes[node];
        checkpoints.push_back( roadNodes[node] );
    };
    int32_t guard = 0;
    while ( travelled < kRouteLength && guard++ < 4096 ) {
        const std::vector<int32_t> &next = adjacency[current];
        if ( next.empty() ) {
            break;
        }
        int32_t pick = next[rng() % next.size()];
        if ( next.size() > 1 && pick == previous ) {
            pick = next[( rng() + 1 ) % next.size()];
        }
        if ( adjacency[pick].size() >= 3 ) {
            pushCheckpoint( pick );
        }
        previous = current;
        current = pick;
    }
    if ( checkpoints.empty() || ( checkpoints.back() - roadNodes[current] ).length() > 50.0f ) {
        pushCheckpoint( current );
    }
    nextCheckpoint = 0;
}

bool RaceEvents::start( int32_t startIndex ) {
    if ( startIndex < 0 || static_cast<size_t>( startIndex ) >= starts.size() || stage != eStage::Idle ) {
        return false;
    }
    // The start node: the road node nearest to the start marker.
    int32_t bestNode = 0;
    float bestDistance = 1e30f;
    for ( size_t i = 0; i < roadNodes.size(); i++ ) {
        const Vector3f d( roadNodes[i].x - starts[startIndex].Position.x, 0.0f, roadNodes[i].z - starts[startIndex].Position.z );
        const float distance = d.length();
        if ( distance < bestDistance ) {
            bestDistance = distance;
            bestNode = static_cast<int32_t>( i );
        }
    }
    activeStart = startIndex;
    // The event's own file: gate 0 is the start, the others the real destination [DATA: EV* .ig2 have 2-3 gates];
    // the file's CtDw is the time limit and its title the rule ("No accident!", "Smoothly", ...). Road-graph walk
    // otherwise (-oldraces, or no readable file).
    checkpoints.clear();
    nextCheckpoint = 0;
    limitSeconds = 0.0f;
    bFailed = false;
    rule.clear();
    Ig2Challenge file;
    if ( !CmdLine::HasArg( "-oldraces" ) && file.load( starts[startIndex].Name ) && file.getGates().size() >= 2 ) {
        for ( size_t i = 1; i < file.getGates().size(); i++ ) {
            checkpoints.push_back( file.getGates()[i].Position );
        }
        limitSeconds = static_cast<float>( file.getLimitMs() ) / 1000.0f;
        rule = file.getTitle();
        float length = 0.0f;
        Vector3f previous = starts[startIndex].Position;
        for ( const Vector3f &point : checkpoints ) {
            length += Vector3f( point.x - previous.x, 0.0f, point.z - previous.z ).length();
            previous = point;
        }
        OTDU_LOG_INFO( "RaceEvents: '%s' rule '%s': %zu gate(s), %.0f m straight, limit %.0f s\n", starts[startIndex].Name.c_str(),
                       rule.c_str(), checkpoints.size(), length, limitSeconds );
    } else {
        buildRoute( bestNode );
    }
    stage = eStage::Countdown;
    countdownTime = 0.0f;
    raceTime = 0.0f;
    OTDU_LOG_INFO( "RaceEvents: countdown at start %d '%s', %zu checkpoints, reward $%d\n", startIndex,
                   starts[startIndex].Name.c_str(), checkpoints.size(), starts[startIndex].Reward );
    return true;
}

int32_t RaceEvents::getCountdownNumber() const {
    if ( stage != eStage::Countdown ) {
        return -1;
    }
    // 3 seconds of countdown: 3, 2, 1, then 0 ("GO!").
    if ( countdownTime < 1.0f ) {
        return 3;
    }
    if ( countdownTime < 2.0f ) {
        return 2;
    }
    if ( countdownTime < 3.0f ) {
        return 1;
    }
    return 0;
}

void RaceEvents::tick( float deltaTime, const Vector3f &carPosition ) {
    if ( stage == eStage::Countdown ) {
        countdownTime += deltaTime;
        if ( countdownTime >= 4.0f ) {
            stage = eStage::Running;
            raceTime = 0.0f;
            OTDU_LOG_INFO( "RaceEvents: GO\n" );
        }
        return;
    }
    if ( stage != eStage::Running ) {
        return;
    }
    raceTime += deltaTime;
    if ( limitSeconds > 0.0f && raceTime > limitSeconds ) {
        stage = eStage::Finished;
        finishTime = raceTime;
        bFailed = true;
        bFinishEvent = true;
        OTDU_LOG_INFO( "RaceEvents: time limit %.0f s reached\n", limitSeconds );
        return;
    }
    if ( nextCheckpoint < static_cast<int32_t>( checkpoints.size() ) ) {
        const Vector3f d = checkpoints[nextCheckpoint] - carPosition;
        if ( std::sqrt( d.x * d.x + d.z * d.z ) < kCheckpointRadius ) {
            OTDU_LOG_INFO( "RaceEvents: checkpoint %d/%d at %.1f s\n", nextCheckpoint + 1,
                           static_cast<int32_t>( checkpoints.size() ), raceTime );
            nextCheckpoint++;
            if ( nextCheckpoint >= static_cast<int32_t>( checkpoints.size() ) ) {
                stage = eStage::Finished;
                finishTime = raceTime;
                bFinishEvent = true;
                OTDU_LOG_INFO( "RaceEvents: finished in %.2f s\n", raceTime );
            }
        }
    }
}

bool RaceEvents::getNextCheckpoint( Vector3f &outPosition, int32_t &outIndex ) const {
    if ( stage != eStage::Running || nextCheckpoint >= static_cast<int32_t>( checkpoints.size() ) ) {
        return false;
    }
    outPosition = checkpoints[nextCheckpoint];
    outIndex = nextCheckpoint;
    return true;
}

bool RaceEvents::takeFinished( float &outTime, int32_t &outReward, bool &outSuccess ) {
    if ( !bFinishEvent ) {
        return false;
    }
    bFinishEvent = false;
    outTime = finishTime;
    outSuccess = !bFailed;
    outReward = activeStart >= 0 && outSuccess ? starts[activeStart].Reward : 0;
    if ( activeStart >= 0 && outSuccess ) {
        starts[activeStart].bUsed = true;
    }
    stage = eStage::Idle;
    return true;
}

void RaceEvents::abandon() {
    if ( stage == eStage::Idle ) {
        return;
    }
    OTDU_LOG_INFO( "RaceEvents: race abandoned\n" );
    stage = eStage::Idle;
    checkpoints.clear();
}
