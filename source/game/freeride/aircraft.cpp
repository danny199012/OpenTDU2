#include "shared.h"
#include "aircraft.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "render/eden/eden_renderer.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "world/eden_world.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265f;
// Resampling: a route point pair is split into this many samples (a 6 point
// loop of 30 km still curves smoothly with a Catmull-Rom spline through it).
constexpr int32_t kSubdivisions = 10;
// A path point is a ground point when its altitude is within this of the
// terrain below (the two runways are 20 m and 57 m up on Ibiza) [INF].
constexpr float kGroundTolerance = 60.0f;
// Planes per route: the data's own spacing, capped (a 120 km loop with a 3 km
// spacing would be 40 aircraft).
constexpr int32_t kMaxPlanesPerRoute = 3;
// Speed changes ramp (m/s^2) instead of snapping at a segment change.
constexpr float kAccel = 6.0f;
constexpr float kBrake = 8.0f;
// Bank / pitch limits of the visual attitude.
constexpr float kMaxBank = 32.0f * kDegToRad;
constexpr float kMaxPitch = 18.0f * kDegToRad;
// The attitude is taken this far ahead along the loop (m), so a corner is flown
// through instead of snapped at a sample.
constexpr float kLookAhead = 45.0f;

Matrix4x4f RotationY( float angle ) {
    return Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), angle ) );
}
Matrix4x4f RotationX( float angle ) {
    return Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), angle ) );
}
Matrix4x4f RotationZ( float angle ) {
    return Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 0.0f, 1.0f ), angle ) );
}

float WrapPi( float angle ) {
    while ( angle > kPi ) {
        angle -= 2.0f * kPi;
    }
    while ( angle <= -kPi ) {
        angle += 2.0f * kPi;
    }
    return angle;
}

// Uniform Catmull-Rom through p1 / p2 (p0 and p3 are the tangent handles).
Vector3f CatmullRom( const Vector3f &p0, const Vector3f &p1, const Vector3f &p2, const Vector3f &p3, float t ) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return ( p1 * 2.0f + ( p2 - p0 ) * t + ( p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3 ) * t2 +
             ( p1 * 3.0f - p0 - p2 * 3.0f + p3 ) * t3 ) *
           0.5f;
}

Vector3f ReadVector( XmbValue value ) {
    return Vector3f( value["X"].asFloat(), value["Y"].asFloat(), value["Z"].asFloat() );
}
} // namespace

Aircraft::Aircraft() = default;

Aircraft::~Aircraft() { unload(); }

void Aircraft::unload() {
    bLoaded = false;
    routes.clear();
    models.clear();
    planes.clear();
    takeoffCount = 0;
    logTimer = 0.0f;
    loggedPlanes = -1;
    loggedGround = -1;
}

int32_t Aircraft::getNumOnGround() const {
    int32_t count = 0;
    for ( const Plane &plane : planes ) {
        count += plane.bGround ? 1 : 0;
    }
    return count;
}

bool Aircraft::load( const std::string &island, EdenWorld &world, const SurfaceFunction &surface ) {
    unload();
    islandName = island;
    static const bool bLog = CmdLine::HasArg( "-logaircraft" );
    bPark = CmdLine::HasArg( "-aircraftpark" );
    const std::string folder = gpFile->getBankFolder() + "islands/" + island + "/scenarics/packs/";

    // The aircraft of each AircraftType: Entity__Actor__Aircraft records that
    // only name their model (ObjectInCommonRef), the geometry is an object of
    // the island's common library.
    Bank aircraftBank;
    const std::string aircraftPath = folder + "aircrafts.bnk";
    if ( gpFile->fileExists( aircraftPath.c_str() ) && aircraftBank.load( aircraftPath.c_str() ) ) {
        const BankEntry *pEntities = aircraftBank.findEntry( "entities.xmb" );
        XmbDocument entities;
        if ( pEntities != nullptr && entities.load( pEntities->pData, pEntities->Size ) ) {
            XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
            for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor__Aircraft" ); i++ ) {
                XmbValue actor = entityExport.getArrayElement( "ArrayOf_Entity__Actor__Aircraft", i );
                Model model;
                model.TypeId = actor["Type"].asInt();
                model.Hashcode = actor["ObjectInCommonRef"].asUInt64();
                model.Node = model.Hashcode != 0 ? world.findLibraryNode( model.Hashcode ) : -1;
                if ( model.Node >= 0 && world.getLibraryNodeBounds( model.Node, model.BoundsMin, model.BoundsMax ) ) {
                    model.Centre = ( model.BoundsMin + model.BoundsMax ) * 0.5f;
                    model.Length = Max( model.BoundsMax.x - model.BoundsMin.x, model.BoundsMax.z - model.BoundsMin.z );
                }
                OTDU_LOG_INFO( "Aircraft: type %d '%s' -> library node %d, length %.1f m, bounds (%.1f, %.1f, %.1f) - "
                               "(%.1f, %.1f, %.1f)\n",
                               model.TypeId, actor["__Name"].asString().c_str(), model.Node, model.Length,
                               model.BoundsMin.x, model.BoundsMin.y, model.BoundsMin.z, model.BoundsMax.x,
                               model.BoundsMax.y, model.BoundsMax.z );
                models.push_back( model );
            }
        }
    }
    aircraftBank.unload();
    if ( models.empty() ) {
        OTDU_LOG_WARN( "Aircraft: '%s' has no aircraft record\n", aircraftPath.c_str() );
        return false;
    }

    // The routes: Entity__AircraftPath loops over the Entity__Volume nodes.
    Bank pathBank;
    const std::string pathPath = folder + "world_aircrafts.bnk";
    if ( !gpFile->fileExists( pathPath.c_str() ) || !pathBank.load( pathPath.c_str() ) ) {
        OTDU_LOG_INFO( "Aircraft: '%s' has no world_aircrafts.bnk (no aeroplanes)\n", island.c_str() );
        return false;
    }
    const BankEntry *pPathEntities = pathBank.findEntry( "entities.xmb" );
    XmbDocument pathEntities;
    if ( pPathEntities == nullptr || !pathEntities.load( pPathEntities->pData, pPathEntities->Size ) ) {
        OTDU_LOG_WARN( "Aircraft: '%s' has no entities.xmb\n", pathPath.c_str() );
        return false;
    }
    std::unordered_map<uint64_t, Vector3f> volumes;
    {
        XmbValue volumeExport = pathEntities.getRoot()["ExportType_Volume"];
        for ( uint32_t i = 0; i < volumeExport.getArraySize( "ArrayOf_Entity__Volume" ); i++ ) {
            XmbValue volume = volumeExport.getArrayElement( "ArrayOf_Entity__Volume", i );
            volumes[volume["__Hc"].asUInt64()] = ReadVector( volume["orient_pos_scale"]["pos"] );
        }
    }
    XmbValue routeExport = pathEntities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < routeExport.getArraySize( "ArrayOf_Entity__AircraftPath" ); i++ ) {
        XmbValue entry = routeExport.getArrayElement( "ArrayOf_Entity__AircraftPath", i );
        Route route;
        route.Name = entry["__Name"].asString();
        route.TypeId = entry["AircraftType"].asInt();
        route.GroundSpeed = entry["SpeedOnGround"].asFloat( 15.0f );
        route.AirSpeed = entry["SpeedOnAir"].asFloat( 80.0f );
        route.MinSpacing = entry["MinDistBetween2Aircraft"].asFloat( 3000.0f );
        route.MaxSpacing = Max( entry["MaxDistBetween2Aircraft"].asFloat( 5000.0f ), route.MinSpacing );
        std::vector<Vector3f> points;
        std::vector<int32_t> types;
        for ( uint32_t j = 0; j < entry.getArraySize( "PathPoints" ); j++ ) {
            XmbValue point = entry.getArrayElement( "PathPoints", j );
            const uint64_t hashcode = point["positionVolume"].asUInt64();
            const auto it = volumes.find( hashcode );
            if ( it == volumes.end() ) {
                OTDU_LOG_WARN( "Aircraft: '%s' point %u: no volume 0x%016llx\n", route.Name.c_str(), j,
                               static_cast<unsigned long long>( hashcode ) );
                continue;
            }
            points.push_back( it->second );
            types.push_back( point["Type"].asInt() );
        }
        if ( points.size() < 3 ) {
            OTDU_LOG_WARN( "Aircraft: '%s' has %zu points, skipped\n", route.Name.c_str(), points.size() );
            continue;
        }
        buildRoute( route, points, types, surface, bLog );
        if ( route.Length < 100.0f ) {
            continue;
        }
        routes.push_back( std::move( route ) );
    }
    pathBank.unload();
    if ( routes.empty() ) {
        OTDU_LOG_WARN( "Aircraft: '%s' has no usable route\n", island.c_str() );
        return false;
    }

    // The planes: each route flown by as many aircraft as its own spacing
    // allows (Min/MaxDistBetween2Aircraft), capped.
    for ( size_t r = 0; r < routes.size(); r++ ) {
        Route &route = routes[r];
        const float spacing = Max( ( route.MinSpacing + route.MaxSpacing ) * 0.5f, 1.0f );
        const int32_t count = Clamp( static_cast<int32_t>( route.Length / spacing ), 1, kMaxPlanesPerRoute );
        // The model of the route's AircraftType (the first resolved one when
        // the type is not in aircrafts.bnk).
        int32_t modelType = -1;
        for ( size_t m = 0; m < models.size() && modelType < 0; m++ ) {
            if ( models[m].TypeId == route.TypeId && models[m].Node >= 0 ) {
                modelType = static_cast<int32_t>( m );
            }
        }
        for ( size_t m = 0; m < models.size() && modelType < 0; m++ ) {
            if ( models[m].Node >= 0 ) {
                modelType = static_cast<int32_t>( m );
            }
        }
        if ( modelType < 0 ) {
            continue;
        }
        for ( int32_t p = 0; p < count; p++ ) {
            Plane plane;
            plane.Route = static_cast<int32_t>( r );
            plane.ModelType = modelType;
            // Spread around the loop; the first plane starts on the runway
            // segment when the route has one, so there is always one landing /
            // taking off instead of only a distant speck.
            if ( p == 0 && route.RunwayStart >= 0 ) {
                const float runway = route.Distances[static_cast<size_t>( route.RunwayStart )];
                plane.Distance = bPark ? runway + 25.0f : runway * 0.5f;
            } else {
                plane.Distance = std::fmod( route.Length * static_cast<float>( p ) / static_cast<float>( count ) +
                                                route.MinSpacing * 0.5f,
                                            route.Length );
            }
            plane.Speed = route.AirSpeed;
            placePlane( plane );
            plane.Speed = plane.bGround ? route.GroundSpeed : route.AirSpeed;
            if ( bLog ) {
                OTDU_LOG_INFO( "Aircraft: '%s' plane %d starts at %.0f m (%.0f, %.0f, %.0f) %s\n", route.Name.c_str(),
                               p, plane.Distance, plane.Position.x, plane.Position.y, plane.Position.z,
                               plane.bGround ? "on the runway" : "in the air" );
            }
            planes.push_back( plane );
        }
    }
    bLoaded = !planes.empty();
    if ( bLoaded ) {
        OTDU_LOG_INFO( "Aircraft: %s: %zu routes, %zu aircraft types, %d planes (%d on a runway), view %.0f m\n",
                       islandName.c_str(), routes.size(), models.size(), getNumPlanes(), getNumOnGround(), kViewRadius );
    }
    return bLoaded;
}

void Aircraft::buildRoute( Route &route, const std::vector<Vector3f> &points,
                           const std::vector<int32_t> &pointTypes, const SurfaceFunction &surface,
                           bool bLog ) const {
    const size_t count = points.size();
    // A point is on the ground when the data marks it (Type 0) and its
    // altitude really is the terrain there: the 350 m "runway" anchors of the
    // high circuits are Type 0 too, and they are 300 m up in the air.
    std::vector<uint8_t> pointGround( count, 0 );
    for ( size_t i = 0; i < count; i++ ) {
        if ( pointTypes[i] != 0 ) {
            continue;
        }
        float height = 0.0f;
        const bool bGround = surface( points[i].x, points[i].z, points[i].y + 30.0f, height ) &&
                             std::fabs( height - points[i].y ) <= kGroundTolerance;
        pointGround[i] = bGround ? 1 : 0;
        route.NumGroundPoints += bGround ? 1 : 0;
        if ( bLog ) {
            OTDU_LOG_INFO( "Aircraft: '%s' point %zu type %d at (%.1f, %.1f, %.1f): terrain %s%.1f -> %s\n",
                           route.Name.c_str(), i, pointTypes[i], points[i].x, points[i].y, points[i].z,
                           bGround ? "" : "> ", height, bGround ? "ground" : "air" );
        }
    }
    // The loop, resampled: a ground segment is straight (a runway roll), an air
    // segment follows the Catmull-Rom spline through its neighbours.
    route.Points.clear();
    route.Distances.clear();
    route.Ground.clear();
    for ( size_t i = 0; i < count; i++ ) {
        const size_t next = ( i + 1 ) % count;
        const Vector3f &a = points[i];
        const Vector3f &b = points[next];
        const bool bGround = pointGround[i] != 0 && pointGround[next] != 0;
        if ( bGround && route.RunwayStart < 0 ) {
            route.RunwayStart = static_cast<int32_t>( route.Ground.size() );
        }
        const Vector3f &previous = points[( i + count - 1 ) % count];
        const Vector3f &after = points[( i + 2 ) % count];
        for ( int32_t k = 0; k < kSubdivisions; k++ ) {
            const float t = static_cast<float>( k ) / static_cast<float>( kSubdivisions );
            route.Points.push_back( bGround ? a + ( b - a ) * t : CatmullRom( previous, a, b, after, t ) );
            route.Ground.push_back( bGround ? 1 : 0 );
        }
    }
    // Cumulative distances (the loop closes on its first sample).
    route.Length = 0.0f;
    for ( size_t i = 0; i < route.Points.size(); i++ ) {
        route.Distances.push_back( route.Length );
        const Vector3f &p = route.Points[i];
        const Vector3f &q = route.Points[( i + 1 ) % route.Points.size()];
        route.Length += ( q - p ).length();
    }
    route.Ground.resize( route.Points.size(), route.Ground.empty() ? 0 : route.Ground.back() );
    // A runway segment is a short part of a 30 km loop: keep it reachable by
    // making it the last one when no ground pair was found at all.
    if ( route.RunwayStart < 0 ) {
        route.RunwayStart = 0;
    }
    if ( bLog ) {
        OTDU_LOG_INFO( "Aircraft: '%s' type %d: %zu points (%d on the ground), loop %.0f m resampled to %zu, "
                       "ground m/s %.0f, air m/s %.0f, spacing %.0f-%.0f m\n",
                       route.Name.c_str(), route.TypeId, count, route.NumGroundPoints, route.Length,
                       route.Points.size(), route.GroundSpeed, route.AirSpeed, route.MinSpacing, route.MaxSpacing );
    }
}

void Aircraft::sampleRoute( const Route &route, float distance, Vector3f &outPosition, float &outHeading,
                            int32_t &outSegment ) const {
    const size_t samples = route.Points.size();
    if ( samples < 2 ) {
        outPosition = Vector3f();
        outHeading = 0.0f;
        outSegment = 0;
        return;
    }
    float s = std::fmod( distance, route.Length );
    if ( s < 0.0f ) {
        s += route.Length;
    }
    // The sample whose segment holds 's'.
    size_t k = 0;
    while ( k + 1 < samples && route.Distances[k + 1] <= s ) {
        k++;
    }
    const size_t next = ( k + 1 ) % samples;
    const float span = next > k ? route.Distances[next] - route.Distances[k] : route.Length - route.Distances[k];
    const float t = span > 0.001f ? Clamp( ( s - route.Distances[k] ) / span, 0.0f, 1.0f ) : 0.0f;
    outPosition = route.Points[k] + ( route.Points[next] - route.Points[k] ) * t;
    outSegment = static_cast<int32_t>( k );
    // The attitude looks ahead: the tangent of the point kLookAhead further on,
    // so the aircraft turns before the corner instead of at it.
    Vector3f ahead;
    const float aheadDistance = s + kLookAhead;
    const float wrapped = aheadDistance >= route.Length ? aheadDistance - route.Length : aheadDistance;
    size_t j = 0;
    while ( j + 1 < samples && route.Distances[j + 1] <= wrapped ) {
        j++;
    }
    const size_t jNext = ( j + 1 ) % samples;
    const float jSpan = jNext > j ? route.Distances[jNext] - route.Distances[j] : route.Length - route.Distances[j];
    const float jt = jSpan > 0.001f ? Clamp( ( wrapped - route.Distances[j] ) / jSpan, 0.0f, 1.0f ) : 0.0f;
    ahead = route.Points[j] + ( route.Points[jNext] - route.Points[j] ) * jt;
    const Vector3f delta = ahead - outPosition;
    outHeading = std::atan2( delta.x, delta.z );
}

int32_t Aircraft::takeTakeoffs() {
    const int32_t count = takeoffCount;
    takeoffCount = 0;
    return count;
}

void Aircraft::updatePlane( Plane &plane, float deltaTime ) {
    const Route &route = routes[static_cast<size_t>( plane.Route )];
    const bool bWasGround = plane.bGround;
    if ( deltaTime > 0.0f ) {
        plane.Distance += plane.Speed * deltaTime;
        if ( plane.Distance >= route.Length ) {
            plane.Distance -= route.Length * std::floor( plane.Distance / route.Length );
        }
    }
    placePlane( plane );
    // The speed of the segment the aircraft is on (the data's two speeds), with
    // a ramp so the change reads as a run-up instead of a jump.
    const float target = plane.bGround ? route.GroundSpeed : route.AirSpeed;
    if ( deltaTime > 0.0f ) {
        const float rate = target < plane.Speed ? kBrake : kAccel;
        const float step = rate * deltaTime;
        plane.Speed += Clamp( target - plane.Speed, -step, step );
    } else {
        plane.Speed = target;
    }
    // A ground -> air change is a take-off, air -> ground a landing: counted
    // for the log and the headless verification (nothing plays on them).
    if ( plane.bGround != bWasGround && deltaTime > 0.0f ) {
        takeoffCount++;
    }
}

void Aircraft::placePlane( Plane &plane ) {
    const Route &route = routes[static_cast<size_t>( plane.Route )];
    Vector3f position;
    float heading = 0.0f;
    int32_t segment = 0;
    sampleRoute( route, plane.Distance, position, heading, segment );
    // The attitude: yaw from the loop, pitch from the climb of the next 60 m and
    // a bank from the turn rate (the aircraft leans into the corner).
    Vector3f ahead;
    float aheadHeading = 0.0f;
    int32_t aheadSegment = 0;
    sampleRoute( route, plane.Distance + 60.0f, ahead, aheadHeading, aheadSegment );
    const Vector3f flat( ahead.x - position.x, 0.0f, ahead.z - position.z );
    const float flatLength = flat.length();
    const bool bGround = route.Ground.empty() ? false : route.Ground[static_cast<size_t>( segment )] != 0;
    float pitch = flatLength > 1.0f ? std::atan2( ahead.y - position.y, flatLength ) : 0.0f;
    if ( bGround ) {
        pitch = 0.0f;
    }
    const float turn = WrapPi( aheadHeading - heading );
    // A coordinated turn: tan(bank) = v * yawRate / g over the 60 m of look
    // ahead (yawRate = v / 60 * turn), clamped to the airliner's 32 degrees.
    const float yawRate = flatLength > 1.0f ? plane.Speed / flatLength * turn : 0.0f;
    float bank = bGround ? 0.0f : std::atan( plane.Speed * yawRate / 9.81f );
    if ( bGround ) {
        bank = 0.0f;
    }
    plane.Position = position;
    plane.Heading = heading;
    plane.Pitch = Clamp( pitch, -kMaxPitch, kMaxPitch );
    plane.Bank = Clamp( bank, -kMaxBank, kMaxBank );
    plane.Segment = segment;
    plane.bGround = bGround;
}

void Aircraft::setParkPose( const Vector3f &position, const Vector3f &forward ) {
    parkPosition = position;
    parkForward = forward;
    bHasParkPose = forward.length() > 0.0001f;
}

void Aircraft::tick( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    // -aircraftspeed <x> (test): scales the flight clock, so a headless run of a
    // few seconds shows a landing and a take-off of a route that takes minutes.
    static const float timeScale =
        CmdLine::HasArg( "-aircraftspeed" ) ? Max( 1.0f, static_cast<float>( CmdLine::GetArgInt( "-aircraftspeed", 1 ) ) ) : 1.0f;
    deltaTime *= timeScale;
    if ( bPark && bHasParkPose ) {
        // -aircraftpark: the first two planes stand in front of the player, the
        // fuselage across the view, so a screenshot always has one in frame.
        const Vector3f forward = parkForward.normalized();
        const Vector3f right( forward.z, 0.0f, -forward.x );
        int32_t parked = 0;
        for ( Plane &plane : planes ) {
            if ( parked >= 3 ) {
                break;
            }
            // Two standing on the apron, and one in the air above the road, so
            // one shot shows the parked and the flying attitude.
            const float ahead = parked == 0 ? 45.0f : ( parked == 1 ? 80.0f : 70.0f );
            const float side = parked == 0 ? -14.0f : ( parked == 1 ? 26.0f : 6.0f );
            const float lift = parked == 2 ? 34.0f : 0.0f;
            plane.Position = parkPosition + forward * ahead + right * side + Vector3f( 0.0f, lift, 0.0f );
            plane.Heading = parked == 2 ? std::atan2( forward.x, forward.z ) + 0.9f
                                        : std::atan2( forward.x, forward.z ) + kPi * 0.5f;
            plane.Pitch = 0.0f;
            plane.Bank = parked == 2 ? 0.35f : 0.0f;
            plane.Speed = 0.0f;
            plane.bGround = parked != 2;
            parked++;
        }
        return;
    }
    if ( bPark ) {
        // -aircraftpark without a pose yet: frozen on the runway.
        for ( Plane &plane : planes ) {
            plane.Speed = 0.0f;
            placePlane( plane );
        }
        return;
    }
    for ( Plane &plane : planes ) {
        updatePlane( plane, deltaTime );
    }
}

void Aircraft::draw( EdenRenderer &renderer, EdenWorld &world, const Vector3f &cameraPosition ) const {
    if ( !bLoaded ) {
        return;
    }
    const float viewRadiusSq = kViewRadius * kViewRadius;
    for ( const Plane &plane : planes ) {
        const Model &model = models[static_cast<size_t>( plane.ModelType )];
        if ( model.Node < 0 ) {
            continue;
        }
        const Vector3f delta = plane.Position - cameraPosition;
        if ( delta.dot( delta ) > viewRadiusSq ) {
            continue;
        }
        // The model turns about its own centre; on the runway its lowest point
        // sits on the surface instead (the wheels of the library object are the
        // ground contact), in the air the centre is what the path's altitude
        // means [INF: the data gives one altitude per path point, no gear).
        const float offsetY = plane.bGround ? -model.BoundsMin.y : -model.Centre.y;
        const Matrix4x4f world4 = Matrix4x4f::Translation( plane.Position ) * RotationY( plane.Heading ) *
                                  RotationX( -plane.Pitch ) * RotationZ( -plane.Bank ) *
                                  Matrix4x4f::Translation( Vector3f( -model.Centre.x, offsetY, -model.Centre.z ) );
        // The shadow slices and the environment cube skip nothing here: the sun
        // shadows of a plane crossing the runway are the point of the low pass.
        world.drawLibraryNode( renderer, model.Node, world4 );
    }
}

void Aircraft::logStats( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    logTimer += deltaTime;
    if ( logTimer < 5.0f ) {
        return;
    }
    logTimer = 0.0f;
    const int32_t transitions = takeTakeoffs();
    const int32_t onGround = getNumOnGround();
    if ( onGround == loggedGround && getNumPlanes() == loggedPlanes && transitions == 0 ) {
        return;
    }
    loggedGround = onGround;
    loggedPlanes = getNumPlanes();
    OTDU_LOG_INFO( "Aircraft: %d planes on %zu routes, %d on the ground, %d take-off / landing since the last "
                   "line\n",
                   getNumPlanes(), routes.size(), onGround, transitions );
}
