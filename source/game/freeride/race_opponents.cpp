#include "shared.h"
#include "race_opponents.h"

#include "render/eden/eden_weather.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace {
constexpr float kStep = 3.0f;         // course resampling (m)
constexpr float kGridBack = 70.0f;    // course before the start line (the grid)
constexpr float kRunOut = 250.0f;     // course past the finish (the cars slow down there)
constexpr float kRouteSnap = 35.0f;   // a gate further than this from a road is joined straight [INF]
constexpr float kMaxLateral = 2.8f;   // m either side of the line [INF: a two-lane road is ~7 m]
constexpr float kHomeLateral = 1.6f;
constexpr float kRowSpacing = 8.0f;   // grid rows (m) [INF]

float WrapAngle( float angle ) {
    while ( angle > kPI ) {
        angle -= 2.0f * kPI;
    }
    while ( angle < -kPI ) {
        angle += 2.0f * kPI;
    }
    return angle;
}

float FlatDistance( const Vector3f &a, const Vector3f &b ) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt( dx * dx + dz * dz );
}

const char *OrdinalSuffix( int32_t n ) {
    if ( n % 100 >= 11 && n % 100 <= 13 ) {
        return "th";
    }
    switch ( n % 10 ) {
    case 1: return "st";
    case 2: return "nd";
    case 3: return "rd";
    default: return "th";
    }
}
} // namespace

void RaceOpponents::setRoads( const std::vector<Vector3f> *pRoadNodes,
                              const std::vector<std::pair<int32_t, int32_t>> *pRoadEdges,
                              const std::vector<std::vector<Vector3f>> *pRoadShapes ) {
    pNodes = pRoadNodes;
    pEdges = pRoadEdges;
    pShapes = pRoadShapes;
    adjacency.clear();
    cumulative.clear();
    edgeLength.clear();
    if ( pNodes == nullptr || pEdges == nullptr || pShapes == nullptr ) {
        return;
    }
    adjacency.assign( pNodes->size(), {} );
    cumulative.assign( pEdges->size(), {} );
    edgeLength.assign( pEdges->size(), 0.0f );
    for ( size_t i = 0; i < pEdges->size(); i++ ) {
        const auto &edge = ( *pEdges )[i];
        if ( edge.first < 0 || edge.second < 0 || static_cast<size_t>( edge.first ) >= pNodes->size() ||
             static_cast<size_t>( edge.second ) >= pNodes->size() ) {
            continue;
        }
        adjacency[static_cast<size_t>( edge.first )].push_back( static_cast<int32_t>( i ) );
        adjacency[static_cast<size_t>( edge.second )].push_back( static_cast<int32_t>( i ) );
        const std::vector<Vector3f> &points = i < pShapes->size() ? ( *pShapes )[i] : std::vector<Vector3f>();
        float length = 0.0f;
        std::vector<float> &cum = cumulative[i];
        for ( size_t k = 0; k < points.size(); k++ ) {
            if ( k > 0 ) {
                length += FlatDistance( points[k], points[k - 1] );
            }
            cum.push_back( length );
        }
        edgeLength[i] = length;
    }
}

RaceOpponents::EdgePoint RaceOpponents::nearestEdgePoint( const Vector3f &point ) const {
    EdgePoint best;
    if ( pShapes == nullptr ) {
        return best;
    }
    for ( size_t e = 0; e < pShapes->size() && e < cumulative.size(); e++ ) {
        const std::vector<Vector3f> &points = ( *pShapes )[e];
        for ( size_t k = 0; k + 1 < points.size(); k++ ) {
            const float ax = points[k].x, az = points[k].z;
            const float bx = points[k + 1].x - ax, bz = points[k + 1].z - az;
            const float length2 = bx * bx + bz * bz;
            if ( length2 < 1e-6f ) {
                continue;
            }
            const float t = Clamp( ( ( point.x - ax ) * bx + ( point.z - az ) * bz ) / length2, 0.0f, 1.0f );
            const float px = ax + bx * t - point.x, pz = az + bz * t - point.z;
            const float gap = std::sqrt( px * px + pz * pz );
            if ( gap < best.Gap ) {
                best.Gap = gap;
                best.Edge = static_cast<int32_t>( e );
                best.Along = cumulative[e][k] + std::sqrt( length2 ) * t;
            }
        }
    }
    return best;
}

void RaceOpponents::appendEdgeCurve( int32_t edge, float fromAlong, float toAlong, std::vector<Vector3f> &out ) const {
    const std::vector<Vector3f> &points = ( *pShapes )[static_cast<size_t>( edge )];
    const std::vector<float> &cum = cumulative[static_cast<size_t>( edge )];
    if ( points.size() < 2 ) {
        return;
    }
    const auto at = [&]( float along ) {
        along = Clamp( along, 0.0f, cum.back() );
        size_t k = 0;
        while ( k + 2 < cum.size() && cum[k + 1] < along ) {
            k++;
        }
        const float span = Max( 1e-4f, cum[k + 1] - cum[k] );
        const float t = Clamp( ( along - cum[k] ) / span, 0.0f, 1.0f );
        return points[k] + ( points[k + 1] - points[k] ) * t;
    };
    out.push_back( at( fromAlong ) );
    if ( fromAlong <= toAlong ) {
        for ( size_t k = 0; k < points.size(); k++ ) {
            if ( cum[k] > fromAlong && cum[k] < toAlong ) {
                out.push_back( points[k] );
            }
        }
    } else {
        for ( size_t k = points.size(); k-- > 0; ) {
            if ( cum[k] < fromAlong && cum[k] > toAlong ) {
                out.push_back( points[k] );
            }
        }
    }
    out.push_back( at( toAlong ) );
}

void RaceOpponents::routeLeg( const Vector3f &from, const Vector3f &to, std::vector<Vector3f> &out ) const {
    const float straight = FlatDistance( from, to );
    if ( adjacency.empty() ) {
        out.push_back( to );
        return;
    }
    const EdgePoint a = nearestEdgePoint( from );
    const EdgePoint b = nearestEdgePoint( to );
    if ( a.Edge < 0 || b.Edge < 0 || a.Gap > kRouteSnap || b.Gap > kRouteSnap ) {
        out.push_back( to );
        return;
    }
    // Shortest path over the nodes from both ends of a's link to both ends of b's.
    const auto &edgeA = ( *pEdges )[static_cast<size_t>( a.Edge )];
    const auto &edgeB = ( *pEdges )[static_cast<size_t>( b.Edge )];
    const float lengthA = edgeLength[static_cast<size_t>( a.Edge )];
    const float lengthB = edgeLength[static_cast<size_t>( b.Edge )];
    std::vector<float> cost( pNodes->size(), 1e30f );
    std::vector<int32_t> via( pNodes->size(), -1 ); // the link that reached the node
    using Item = std::pair<float, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    cost[static_cast<size_t>( edgeA.first )] = a.Along;
    open.push( { a.Along, edgeA.first } );
    if ( lengthA - a.Along < cost[static_cast<size_t>( edgeA.second )] ) {
        cost[static_cast<size_t>( edgeA.second )] = lengthA - a.Along;
        open.push( { lengthA - a.Along, edgeA.second } );
    }
    while ( !open.empty() ) {
        const Item item = open.top();
        open.pop();
        if ( item.first > cost[static_cast<size_t>( item.second )] ) {
            continue;
        }
        for ( const int32_t link : adjacency[static_cast<size_t>( item.second )] ) {
            const auto &edge = ( *pEdges )[static_cast<size_t>( link )];
            const int32_t other = edge.first == item.second ? edge.second : edge.first;
            const float next = item.first + edgeLength[static_cast<size_t>( link )];
            if ( next < cost[static_cast<size_t>( other )] ) {
                cost[static_cast<size_t>( other )] = next;
                via[static_cast<size_t>( other )] = link;
                open.push( { next, other } );
            }
        }
    }
    const float viaFirst = cost[static_cast<size_t>( edgeB.first )] + b.Along;
    const float viaSecond = cost[static_cast<size_t>( edgeB.second )] + ( lengthB - b.Along );
    const float graphLength = Min( viaFirst, viaSecond );
    const float directLength = a.Edge == b.Edge ? std::fabs( b.Along - a.Along ) : 1e30f;
    const float best = Min( graphLength, directLength );
    // A detour far longer than the gap between the gates is not the course
    // (two gates across a junction the graph does not join) [INF].
    if ( best > 3.0f * straight + 300.0f ) {
        out.push_back( to );
        return;
    }
    if ( directLength <= graphLength ) {
        appendEdgeCurve( a.Edge, a.Along, b.Along, out );
        out.push_back( to );
        return;
    }
    // The node path, from b's entry node back to a's link.
    const int32_t entry = viaFirst <= viaSecond ? edgeB.first : edgeB.second;
    std::vector<int32_t> links; // traversed links, entry side first
    std::vector<int32_t> nodesOnPath{ entry };
    int32_t node = entry;
    for ( size_t guard = 0; via[static_cast<size_t>( node )] >= 0 && guard < pNodes->size(); guard++ ) {
        const int32_t link = via[static_cast<size_t>( node )];
        const auto &edge = ( *pEdges )[static_cast<size_t>( link )];
        node = edge.first == node ? edge.second : edge.first;
        links.push_back( link );
        nodesOnPath.push_back( node );
    }
    std::reverse( links.begin(), links.end() );
    std::reverse( nodesOnPath.begin(), nodesOnPath.end() );
    // nodesOnPath[0] is the node of a's link the path leaves from.
    const int32_t exitNode = nodesOnPath.front();
    appendEdgeCurve( a.Edge, a.Along, exitNode == edgeA.first ? 0.0f : lengthA, out );
    for ( size_t i = 0; i < links.size(); i++ ) {
        const auto &edge = ( *pEdges )[static_cast<size_t>( links[i] )];
        const float length = edgeLength[static_cast<size_t>( links[i] )];
        const bool bFromFirst = edge.first == nodesOnPath[i];
        appendEdgeCurve( links[i], bFromFirst ? 0.0f : length, bFromFirst ? length : 0.0f, out );
    }
    appendEdgeCurve( b.Edge, entry == edgeB.first ? 0.0f : lengthB, b.Along, out );
    out.push_back( to );
}

void RaceOpponents::buildCourse( const std::vector<Vector3f> &waypoints, float startHeading ) {
    course.clear();
    curvature.clear();
    const Vector3f forward( std::sin( startHeading ), 0.0f, std::cos( startHeading ) );
    std::vector<Vector3f> raw;
    raw.push_back( waypoints[0] - forward * kGridBack );
    raw.push_back( waypoints[0] );
    // Leave the start line straight ahead for a few metres (the start gate's
    // heading): the nearest link point may lie a little behind it.
    raw.push_back( waypoints[0] + forward * 12.0f );
    for ( size_t i = 1; i < waypoints.size(); i++ ) {
        routeLeg( i == 1 ? raw.back() : waypoints[i - 1], waypoints[i], raw );
    }
    const size_t finishRaw = raw.size() - 1;
    {
        Vector3f tail = raw.size() >= 2 ? raw[raw.size() - 1] - raw[raw.size() - 2] : forward;
        tail.y = 0.0f;
        tail = tail.length() > 1e-3f ? tail * ( 1.0f / tail.length() ) : forward;
        raw.push_back( raw.back() + tail * kRunOut );
    }
    // Drop the points that double back (a link point just behind a gate):
    // a segment turning more than ~120 degrees against the previous one.
    std::vector<Vector3f> clean;
    size_t cleanFinish = 0;
    for ( size_t i = 0; i < raw.size(); i++ ) {
        if ( !clean.empty() && FlatDistance( clean.back(), raw[i] ) < 0.5f ) {
            if ( i == finishRaw ) {
                cleanFinish = clean.size() - 1;
            }
            continue;
        }
        if ( clean.size() >= 2 && i != finishRaw && i + 1 < raw.size() ) {
            const Vector3f a = clean.back() - clean[clean.size() - 2];
            const Vector3f b = raw[i] - clean.back();
            const float la = std::sqrt( a.x * a.x + a.z * a.z ), lb = std::sqrt( b.x * b.x + b.z * b.z );
            if ( la > 1e-3f && lb > 1e-3f && ( a.x * b.x + a.z * b.z ) / ( la * lb ) < -0.5f && lb < 40.0f ) {
                continue;
            }
        }
        if ( i == finishRaw ) {
            cleanFinish = clean.size();
        }
        clean.push_back( raw[i] );
    }
    // Resample every kStep metres along the flat length.
    float total = 0.0f;
    float finishAt = 0.0f;
    std::vector<float> cum{ 0.0f };
    for ( size_t i = 1; i < clean.size(); i++ ) {
        total += FlatDistance( clean[i], clean[i - 1] );
        cum.push_back( total );
        if ( i == cleanFinish ) {
            finishAt = total;
        }
    }
    size_t k = 0;
    for ( float s = 0.0f; s <= total; s += kStep ) {
        while ( k + 2 < cum.size() && cum[k + 1] < s ) {
            k++;
        }
        const float span = Max( 1e-4f, cum[k + 1] - cum[k] );
        const float t = Clamp( ( s - cum[k] ) / span, 0.0f, 1.0f );
        course.push_back( clean[k] + ( clean[k + 1] - clean[k] ) * t );
    }
    // A light smoothing of the polyline corners (XZ only; the ends stay).
    for ( int32_t pass = 0; pass < 3; pass++ ) {
        std::vector<Vector3f> smooth = course;
        for ( size_t i = 1; i + 1 < course.size(); i++ ) {
            smooth[i].x = 0.5f * course[i].x + 0.25f * ( course[i - 1].x + course[i + 1].x );
            smooth[i].z = 0.5f * course[i].z + 0.25f * ( course[i - 1].z + course[i + 1].z );
        }
        course.swap( smooth );
    }
    // Signed curvature (yaw change per metre, positive = turning to +yaw),
    // averaged over +-4 points.
    std::vector<float> raw_k( course.size(), 0.0f );
    for ( size_t i = 1; i + 1 < course.size(); i++ ) {
        const Vector3f a = course[i] - course[i - 1];
        const Vector3f b = course[i + 1] - course[i];
        const float yawA = std::atan2( a.x, a.z ), yawB = std::atan2( b.x, b.z );
        raw_k[i] = WrapAngle( yawB - yawA ) / kStep;
    }
    curvature.assign( course.size(), 0.0f );
    for ( size_t i = 0; i < course.size(); i++ ) {
        float sum = 0.0f;
        int32_t n = 0;
        for ( int32_t d = -4; d <= 4; d++ ) {
            const int64_t j = static_cast<int64_t>( i ) + d;
            if ( j >= 0 && j < static_cast<int64_t>( course.size() ) ) {
                sum += raw_k[static_cast<size_t>( j )];
                n++;
            }
        }
        curvature[i] = n > 0 ? sum / static_cast<float>( n ) : 0.0f;
    }
    startDistance = kGridBack;
    finishDistance = finishAt > startDistance ? finishAt : total - kRunOut;
}

void RaceOpponents::sample( float distance, Vector3f &outPoint, Vector3f &outTangent ) const {
    if ( course.size() < 2 ) {
        outPoint = course.empty() ? Vector3f() : course[0];
        outTangent = Vector3f( 0.0f, 0.0f, 1.0f );
        return;
    }
    const float f = Clamp( distance / kStep, 0.0f, static_cast<float>( course.size() - 1 ) - 1e-3f );
    const size_t i = static_cast<size_t>( f );
    const float t = f - static_cast<float>( i );
    outPoint = course[i] + ( course[i + 1] - course[i] ) * t;
    // The tangent over a few points either side (no kink at each sample).
    const size_t a = i >= 2 ? i - 2 : 0;
    const size_t b = Min( course.size() - 1, i + 3 );
    Vector3f tangent = course[b] - course[a];
    tangent.y = 0.0f;
    const float length = tangent.length();
    outTangent = length > 1e-4f ? tangent * ( 1.0f / length ) : Vector3f( 0.0f, 0.0f, 1.0f );
}

float RaceOpponents::curvatureAt( float distance ) const {
    if ( curvature.empty() ) {
        return 0.0f;
    }
    const size_t i = static_cast<size_t>( Clamp( distance / kStep, 0.0f, static_cast<float>( curvature.size() - 1 ) ) );
    return curvature[i];
}

void RaceOpponents::buildProfile( Racer &racer ) const {
    // [INF] the driver law (the retail one is ENC): grip ~0.95 g at difficulty
    // 0.5 (+0.4 g per unit), braking 7.5 m/s^2 + 3 per unit, the top speed
    // 0.82 .. 1.07 of the car's.
    const float difficulty = Clamp( racer.Info.Difficulty, 0.0f, 1.0f );
    const float lateralGrip = 9.81f * ( 0.75f + 0.4f * difficulty );
    racer.Brake = 7.5f + 3.0f * difficulty;
    racer.TopSpeed = racer.Car->getStats().TopSpeed * ( 0.82f + 0.25f * difficulty );
    racer.Accel = ( 100.0f / 3.6f ) / racer.Car->getStats().Accel0To100 * ( 0.85f + 0.4f * difficulty ) * 1.3f;
    racer.Profile.assign( course.size(), racer.TopSpeed );
    for ( size_t i = 0; i < course.size(); i++ ) {
        const float k = Max( std::fabs( curvature[i] ), 1e-4f );
        racer.Profile[i] = Min( racer.TopSpeed, std::sqrt( lateralGrip / k ) );
    }
    for ( size_t i = course.size() - 1; i-- > 0; ) {
        const float next = racer.Profile[i + 1];
        racer.Profile[i] = Min( racer.Profile[i], std::sqrt( next * next + 2.0f * racer.Brake * kStep ) );
    }
}

float RaceOpponents::profileAt( const Racer &racer, float distance ) const {
    if ( racer.Profile.empty() ) {
        return 0.0f;
    }
    const size_t i = static_cast<size_t>( Clamp( distance / kStep, 0.0f, static_cast<float>( racer.Profile.size() - 1 ) ) );
    return racer.Profile[i];
}

bool RaceOpponents::start( const std::vector<Vector3f> &waypoints, float startHeading, const std::vector<Entry> &entries,
                           const CinematicCar::SurfaceFunction &surface ) {
    stop();
    if ( waypoints.size() < 2 || entries.empty() ) {
        return false;
    }
    surfaceFn = surface;
    buildCourse( waypoints, startHeading );
    if ( course.size() < 8 ) {
        return false;
    }
    // The grid [INF]: the rival beside the player on the front row, then the
    // others two by two behind, the strongest further back.
    std::vector<Entry> order = entries;
    std::stable_sort( order.begin(), order.end(), []( const Entry &a, const Entry &b ) {
        if ( a.bRival != b.bRival ) {
            return a.bRival;
        }
        return a.Difficulty < b.Difficulty;
    } );
    int32_t slot = 1;
    for ( const Entry &entry : order ) {
        Racer racer;
        racer.Info = entry;
        racer.Car = std::make_unique<AiCar>();
        if ( !racer.Car->load( entry.CarId, entry.ColorId ) ) {
            OTDU_LOG_WARN( "RaceOpponents: %s: car %d not available\n", entry.Name.c_str(), entry.CarId );
            continue;
        }
        buildProfile( racer );
        // slot 1: the front row beside the player (right); 2, 3: the second
        // row left / right ...
        const int32_t row = slot / 2;
        const float lateral = slot == 1 ? 3.0f : ( slot % 2 == 0 ? -kHomeLateral : kHomeLateral );
        racer.Distance = startDistance - kRowSpacing * static_cast<float>( row );
        racer.Lateral = racer.TargetLateral = lateral;
        racer.HomeLateral = Clamp( lateral, -kHomeLateral, kHomeLateral );
        OTDU_LOG_INFO( "RaceOpponents: grid %d %s%s in the %s (car %d%s%s), difficulty %.2f, aggressiveness %.2f, top %.0f "
                       "km/h, accel %.1f m/s2\n",
                       slot, entry.Name.c_str(), entry.bRival ? " (rival)" : "", racer.Car->getStats().DisplayName.c_str(),
                       entry.CarId, entry.ColorId.empty() ? "" : ", ", entry.ColorId.c_str(), entry.Difficulty,
                       entry.Aggressiveness, racer.TopSpeed * 3.6f, racer.Accel );
        racers.push_back( std::move( racer ) );
        slot++;
    }
    playerDistance = startDistance;
    playerLateral = 0.0f;
    bPlayerFinished = false;
    Vector3f point, tangent;
    sample( startDistance, point, tangent );
    playerGrid = point;
    OTDU_LOG_INFO( "RaceOpponents: %zu opponent(s), course %.0f m (%zu points, %zu waypoints)\n", racers.size(),
                   finishDistance - startDistance, course.size(), waypoints.size() );
    return !racers.empty();
}

void RaceOpponents::stop() {
    if ( !racers.empty() ) {
        OTDU_LOG_INFO( "RaceOpponents: race over, %zu opponent(s) removed\n", racers.size() );
    }
    racers.clear();
    course.clear();
    curvature.clear();
    bPlayerFinished = false;
}

float RaceOpponents::projectPlayer( const Vector3f &position, float &outLateral ) const {
    // Search a window around the last progress (a circuit passes the same
    // place every lap): 40 m back, 250 m ahead; when the car is off that part
    // (a shortcut, a reset onto the road, -licensewarp), up to 1.5 km ahead.
    float lateral = 0.0f;
    const float near = projectWindow( position, playerDistance - 40.0f, playerDistance + 250.0f, lateral );
    if ( near >= 0.0f ) {
        outLateral = lateral;
        return near;
    }
    const float far = projectWindow( position, playerDistance - 40.0f, playerDistance + 1500.0f, lateral );
    if ( far >= 0.0f ) {
        outLateral = lateral;
    }
    return far;
}

float RaceOpponents::projectWindow( const Vector3f &position, float from, float to, float &outLateral ) const {
    from = Max( 0.0f, from );
    float bestGap = 1e9f, bestDistance = playerDistance;
    for ( size_t i = static_cast<size_t>( from / kStep ); i + 1 < course.size() && static_cast<float>( i ) * kStep <= to; i++ ) {
        const float ax = course[i].x, az = course[i].z;
        const float bx = course[i + 1].x - ax, bz = course[i + 1].z - az;
        const float length2 = Max( 1e-6f, bx * bx + bz * bz );
        const float t = Clamp( ( ( position.x - ax ) * bx + ( position.z - az ) * bz ) / length2, 0.0f, 1.0f );
        const float px = ax + bx * t - position.x, pz = az + bz * t - position.z;
        const float gap = px * px + pz * pz;
        if ( gap < bestGap ) {
            bestGap = gap;
            bestDistance = ( static_cast<float>( i ) + t ) * kStep;
            // right = (-tz, 0, tx) of the segment
            const float length = std::sqrt( length2 );
            outLateral = ( ( position.x - ax ) * ( -bz / length ) + ( position.z - az ) * ( bx / length ) );
        }
    }
    return std::sqrt( bestGap ) < 45.0f ? bestDistance : -1.0f;
}

void RaceOpponents::playerFinished( float time ) {
    if ( !bPlayerFinished ) {
        bPlayerFinished = true;
        playerFinishTime = time;
        playerDistance = Max( playerDistance, finishDistance );
        OTDU_LOG_INFO( "RaceOpponents: the player finishes %d of %d (%.2f s)\n", getPlayerPosition(), getRacerCount(), time );
    }
}

void RaceOpponents::tick( float deltaTime, bool bRunning, float raceTime, const Vector3f &playerPosition,
                          const CinematicCar::SurfaceFunction &surface ) {
    if ( racers.empty() || course.size() < 2 ) {
        return;
    }
    surfaceFn = surface;
    deltaTime = Clamp( deltaTime, 0.0f, 0.1f );
    // The player's progress on the course.
    const float lastPlayerDistance = playerDistance;
    if ( !bPlayerFinished ) {
        float lateral = 0.0f;
        const float projected = projectPlayer( playerPosition, lateral );
        if ( projected >= 0.0f ) {
            playerDistance = projected;
            playerLateral = lateral;
        }
    }
    const float playerSpeed = deltaTime > 1e-4f ? Clamp( ( playerDistance - lastPlayerDistance ) / deltaTime, 0.0f, 120.0f ) : 0.0f;

    // The bodies a car looks out for: the other racers and the player.
    struct Body {
        float Distance, Lateral, Speed;
        bool bPlayer;
        bool bFinished; // home: pulls aside, blocks nobody
    };
    std::vector<Body> bodies;
    bodies.reserve( racers.size() + 1 );
    for ( const Racer &racer : racers ) {
        bodies.push_back( { racer.Distance, racer.Lateral, racer.Speed, false, racer.bFinished } );
    }
    bodies.push_back( { playerDistance, playerLateral, playerSpeed, true, bPlayerFinished } );

    for ( size_t r = 0; r < racers.size(); r++ ) {
        Racer &racer = racers[r];
        float target = 0.0f;
        racer.bBraking = false;
        if ( bRunning ) {
            target = profileAt( racer, racer.Distance + Max( 4.0f, racer.Speed * 0.35f ) );
            target = Min( target, profileAt( racer, racer.Distance ) + 2.0f );
            // The catch-up [INF]: up to 12 % slower 80..380 m ahead of the
            // player, up to 8 % faster as far behind (not after the finish).
            if ( !bPlayerFinished && !racer.bFinished ) {
                const float gap = racer.Distance - playerDistance;
                if ( gap > 80.0f ) {
                    target *= 1.0f - 0.12f * Clamp( ( gap - 80.0f ) / 300.0f, 0.0f, 1.0f );
                } else if ( gap < -80.0f ) {
                    target *= 1.0f + 0.08f * Clamp( ( -gap - 80.0f ) / 300.0f, 0.0f, 1.0f );
                }
            }
            if ( racer.bFinished ) {
                target = 0.0f;
            }
            if ( racer.Stun > 0.0f ) {
                racer.Stun -= deltaTime;
                target *= 0.6f;
            }
            // The car ahead on this line: pass it or follow it.
            const float lookAhead = 12.0f + racer.Speed * 0.6f;
            int32_t blocker = -1;
            float blockerGap = 1e9f;
            for ( size_t b = 0; b < bodies.size(); b++ ) {
                if ( b == r || bodies[b].bFinished ) {
                    continue;
                }
                const float gap = bodies[b].Distance - racer.Distance;
                if ( gap > 0.5f && gap < lookAhead && std::fabs( bodies[b].Lateral - racer.TargetLateral ) < 2.1f && gap < blockerGap ) {
                    blocker = static_cast<int32_t>( b );
                    blockerGap = gap;
                }
            }
            if ( blocker >= 0 && !racer.bFinished ) {
                const Body &ahead = bodies[static_cast<size_t>( blocker )];
                // Aggressiveness 0.5: pass a car 2 m/s slower; 1: anything slower.
                const float margin = 3.0f - 3.0f * Clamp( racer.Info.Aggressiveness, 0.0f, 1.0f );
                bool bPassing = false;
                if ( ahead.Speed + margin < target ) {
                    float bestSide = 0.0f;
                    float bestScore = 1e9f;
                    for ( const float side : { -2.6f, 2.6f } ) {
                        const float lane = ahead.Lateral + side;
                        if ( std::fabs( lane ) > kMaxLateral ) {
                            continue;
                        }
                        bool bFree = true;
                        for ( size_t b = 0; b < bodies.size(); b++ ) {
                            if ( b != r && b != static_cast<size_t>( blocker ) &&
                                 std::fabs( bodies[b].Distance - racer.Distance - blockerGap * 0.5f ) < 9.0f &&
                                 std::fabs( bodies[b].Lateral - lane ) < 2.0f ) {
                                bFree = false;
                            }
                        }
                        const float score = std::fabs( lane - racer.Lateral );
                        if ( bFree && score < bestScore ) {
                            bestScore = score;
                            bestSide = lane;
                        }
                    }
                    if ( bestScore < 1e8f ) {
                        racer.TargetLateral = bestSide;
                        bPassing = true;
                    }
                }
                if ( !bPassing ) {
                    // Follow: close up to ~8 m and hold the speed of the car ahead.
                    target = Min( target, ahead.Speed + ( blockerGap - 8.0f ) * 0.6f );
                }
            } else if ( !racer.bFinished ) {
                // Back to the own line once clear (slowly).
                racer.TargetLateral += Clamp( racer.HomeLateral - racer.TargetLateral, -0.4f * deltaTime, 0.4f * deltaTime );
            }
            if ( racer.Speed < target ) {
                const float ratio = Clamp( racer.Speed / Max( racer.TopSpeed, 1.0f ), 0.0f, 1.0f );
                racer.Speed = Min( target, racer.Speed + racer.Accel * ( 1.0f - ratio * ratio ) * deltaTime );
            } else {
                const float brake = racer.bFinished ? 4.0f : racer.Brake;
                racer.bBraking = racer.Speed - target > 0.5f;
                racer.Speed = Max( target, racer.Speed - brake * deltaTime );
            }
        } else {
            racer.Speed = 0.0f;
        }
        // -logopponents: a car standing still in the race (what holds it).
        static const bool bLogStuck = CmdLine::HasArg( "-logopponents" );
        static float stuckLog = 0.0f;
        if ( bLogStuck && bRunning && !racer.bFinished && racer.Speed < 0.5f && ( stuckLog -= deltaTime ) <= 0.0f ) {
            stuckLog = 2.0f;
            OTDU_LOG_INFO( "RaceOpponents: %s held at %.1f m (lateral %.2f -> %.2f, target %.1f m/s, stun %.1f); player at %.1f m "
                           "lateral %.2f speed %.1f\n",
                           racer.Info.Name.c_str(), racer.Distance, racer.Lateral, racer.TargetLateral, target, racer.Stun,
                           playerDistance, playerLateral, playerSpeed );
        }
        const float lateralBefore = racer.Lateral;
        racer.Lateral += Clamp( racer.TargetLateral - racer.Lateral, -1.8f * deltaTime, 1.8f * deltaTime );
        racer.Lateral = Clamp( racer.Lateral, -kMaxLateral - 0.5f, kMaxLateral + 0.5f );
        const float maxDistance = static_cast<float>( course.size() - 1 ) * kStep - 1.0f;
        racer.Distance = Min( maxDistance, racer.Distance + racer.Speed * deltaTime );
        if ( bRunning && !racer.bFinished && racer.Distance >= finishDistance ) {
            racer.bFinished = true;
            racer.FinishTime = raceTime;
            // Out of the way of the cars still racing: the roadside [INF].
            racer.TargetLateral = racer.Lateral >= 0.0f ? kMaxLateral + 0.5f : -kMaxLateral - 0.5f;
            int32_t place = 1;
            for ( const Racer &other : racers ) {
                place += &other != &racer && other.bFinished ? 1 : 0;
            }
            place += bPlayerFinished ? 1 : 0;
            OTDU_LOG_INFO( "RaceOpponents: %s finishes %d%s (%.2f s)\n", racer.Info.Name.c_str(), place, OrdinalSuffix( place ),
                           raceTime );
        }
        // The pose: the course point, the lateral offset to the right, the
        // heading along the course plus the lateral drift.
        Vector3f point, tangent;
        sample( racer.Distance, point, tangent );
        const Vector3f right( -tangent.z, 0.0f, tangent.x );
        const float lateralSpeed = deltaTime > 1e-4f ? ( racer.Lateral - lateralBefore ) / deltaTime : 0.0f;
        racer.Yaw = std::atan2( tangent.x, tangent.z ) - std::atan2( lateralSpeed, Max( racer.Speed, 2.0f ) );
        racer.Position = point + right * racer.Lateral;
        float ground = 0.0f;
        bool bGround = false;
        for ( const float offset : { 4.0f, 30.0f, -15.0f } ) {
            if ( surface && surface( racer.Position.x, racer.Position.z, point.y + offset, ground ) ) {
                bGround = true;
                break;
            }
        }
        if ( bGround ) {
            racer.GroundY = ground;
            racer.bGround = true;
        } else if ( !racer.bGround ) {
            racer.GroundY = point.y;
        }
        racer.Position.y = racer.GroundY;
        racer.Steer = Clamp( std::atan( 2.7f * curvatureAt( racer.Distance ) ), -0.6f, 0.6f );
        racer.Car->animate( deltaTime, racer.Speed * deltaTime, racer.Steer, racer.bBraking, bNight );
    }

    // Car to car: two cars overlapping on the course move apart across it and
    // the one behind drops to the speed of the one ahead.
    for ( size_t a = 0; a < racers.size(); a++ ) {
        for ( size_t b = a + 1; b < racers.size(); b++ ) {
            Racer &ra = racers[a];
            Racer &rb = racers[b];
            const float length = 0.5f * ( ra.Car->getStats().Length + rb.Car->getStats().Length );
            const float dd = rb.Distance - ra.Distance;
            const float dl = rb.Lateral - ra.Lateral;
            if ( std::fabs( dd ) >= length || std::fabs( dl ) >= 2.0f ) {
                continue;
            }
            const float push = 0.5f * ( 2.0f - std::fabs( dl ) );
            const float sign = dl >= 0.0f ? 1.0f : -1.0f;
            rb.Lateral = Clamp( rb.Lateral + sign * push, -kMaxLateral - 0.5f, kMaxLateral + 0.5f );
            ra.Lateral = Clamp( ra.Lateral - sign * push, -kMaxLateral - 0.5f, kMaxLateral + 0.5f );
            rb.TargetLateral = rb.Lateral;
            ra.TargetLateral = ra.Lateral;
            Racer &behind = dd >= 0.0f ? ra : rb;
            const Racer &front = dd >= 0.0f ? rb : ra;
            if ( !front.bFinished ) {
                behind.Speed = Min( behind.Speed, front.Speed );
            }
        }
    }
}

bool RaceOpponents::resolveCollision( const Vector3f &position, const Vector3f &forward, const Vector3f &velocity, float width,
                                      float length, float mass, Vector3f &outDeltaVelocity ) {
    outDeltaVelocity = Vector3f();
    bool bHit = false;
    const float playerRadius = 0.5f * width;
    const float playerOffsets[3] = { -( 0.5f * length - playerRadius ), 0.0f, 0.5f * length - playerRadius };
    for ( Racer &racer : racers ) {
        const AiCarStats &stats = racer.Car->getStats();
        const float dx0 = racer.Position.x - position.x, dz0 = racer.Position.z - position.z;
        if ( dx0 * dx0 + dz0 * dz0 > ( 8.0f + stats.Length ) * ( 8.0f + stats.Length ) ||
             std::fabs( racer.Position.y - position.y ) > 2.5f ) {
            continue;
        }
        const float radius = 0.5f * stats.Width;
        const float fx = std::sin( racer.Yaw ), fz = std::cos( racer.Yaw );
        const int32_t circles = Max( 2, static_cast<int32_t>( std::ceil( stats.Length / 1.5f ) ) );
        float deepest = 0.0f, nx = 0.0f, nz = 0.0f;
        for ( int32_t i = 0; i < circles; i++ ) {
            const float along = -( 0.5f * stats.Length - radius ) + ( stats.Length - 2.0f * radius ) * i / ( circles - 1 );
            const float ax = racer.Position.x + fx * along, az = racer.Position.z + fz * along;
            for ( const float offset : playerOffsets ) {
                const float px = position.x + forward.x * offset, pz = position.z + forward.z * offset;
                const float dx = px - ax, dz = pz - az;
                const float distance = std::sqrt( dx * dx + dz * dz );
                const float penetration = radius + playerRadius - distance;
                if ( penetration > deepest && distance > 0.001f ) {
                    deepest = penetration;
                    nx = dx / distance; // from the opponent to the player
                    nz = dz / distance;
                }
            }
        }
        if ( deepest <= 0.0f ) {
            continue;
        }
        bHit = true;
        const float relative = ( velocity.x - fx * racer.Speed ) * nx + ( velocity.z - fz * racer.Speed ) * nz;
        float impulse = 0.0f;
        if ( relative < 0.0f ) {
            constexpr float kRestitution = 0.2f;
            impulse = -( 1.0f + kRestitution ) * relative / ( 1.0f / mass + 1.0f / stats.Mass );
        }
        outDeltaVelocity.x += nx * ( impulse / mass ) + nx * Min( deepest, 1.0f ) * 3.0f;
        outDeltaVelocity.z += nz * ( impulse / mass ) + nz * Min( deepest, 1.0f ) * 3.0f;
        // The opponent: the impulse along its heading changes its speed, the
        // part across the course nudges it off its line.
        racer.Speed = Max( 0.0f, racer.Speed + ( -nx * fx - nz * fz ) * impulse / stats.Mass );
        const float across = -( nx * -fz + nz * fx ); // the push along the opponent's right
        racer.Lateral = Clamp( racer.Lateral + across * Min( deepest, 1.0f ) * 0.6f, -kMaxLateral - 0.5f, kMaxLateral + 0.5f );
        racer.TargetLateral = racer.Lateral;
        if ( impulse > 2000.0f && racer.Stun <= 0.0f ) {
            racer.Stun = 0.8f;
            OTDU_LOG_INFO( "RaceOpponents: contact with %s, closing %.1f m/s\n", racer.Info.Name.c_str(), -relative );
        }
    }
    return bHit;
}

std::vector<RaceOpponents::Standing> RaceOpponents::getStandings() const {
    std::vector<Standing> standings;
    for ( const Racer &racer : racers ) {
        Standing standing;
        standing.Name = racer.Info.Name;
        standing.bFinished = racer.bFinished;
        standing.Time = racer.FinishTime;
        standing.Progress = racer.Distance;
        standings.push_back( standing );
    }
    Standing player;
    player.bPlayer = true;
    player.bFinished = bPlayerFinished;
    player.Time = playerFinishTime;
    player.Progress = playerDistance;
    standings.push_back( player );
    std::stable_sort( standings.begin(), standings.end(), []( const Standing &a, const Standing &b ) {
        if ( a.bFinished != b.bFinished ) {
            return a.bFinished;
        }
        if ( a.bFinished ) {
            return a.Time < b.Time;
        }
        return a.Progress > b.Progress;
    } );
    return standings;
}

float RaceOpponents::getLastFinishTime() const {
    float last = -1.0f;
    for ( const Racer &racer : racers ) {
        if ( !racer.bFinished ) {
            return -1.0f;
        }
        last = Max( last, racer.FinishTime );
    }
    return last;
}

int32_t RaceOpponents::getPlayerPosition() const {
    const std::vector<Standing> standings = getStandings();
    for ( size_t i = 0; i < standings.size(); i++ ) {
        if ( standings[i].bPlayer ) {
            return static_cast<int32_t>( i ) + 1;
        }
    }
    return getRacerCount();
}

bool RaceOpponents::getFollowPose( int32_t index, float behind, Vector3f &outPosition, float &outYaw ) const {
    if ( index < 0 || static_cast<size_t>( index ) >= racers.size() ) {
        return false;
    }
    const Racer &racer = racers[static_cast<size_t>( index )];
    Vector3f point, tangent;
    sample( Max( 0.0f, racer.Distance - behind ), point, tangent );
    outPosition = point + Vector3f( -tangent.z, 0.0f, tangent.x ) * racer.Lateral;
    outPosition.y = racer.GroundY + 0.5f;
    outYaw = std::atan2( tangent.x, tangent.z );
    return true;
}

void RaceOpponents::appendPositions( std::vector<Vector3f> &out ) const {
    for ( const Racer &racer : racers ) {
        out.push_back( racer.Position );
    }
}

void RaceOpponents::assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube ) {
    for ( Racer &racer : racers ) {
        racer.Car->assignEnvironment( renderer, pCube );
    }
}

void RaceOpponents::draw( EdenRenderer &renderer, const WeatherPreset *pWeather ) {
    bNight = pWeather != nullptr && ( pWeather->Hour < 6 || pWeather->Hour >= 20 );
    if ( !surfaceFn ) {
        return;
    }
    static const bool bNoDraw = CmdLine::HasArg( "-noopponentdraw" );
    if ( bNoDraw ) {
        return;
    }
    for ( Racer &racer : racers ) {
        const Matrix4x4f world = racer.Car->pose( racer.Position, racer.Yaw, surfaceFn );
        racer.Car->draw( renderer, world, pWeather );
    }
}
