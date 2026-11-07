#include "shared.h"
#include "traffic.h"

#include "core/crypto/cpr.h"
#include "core/xmb.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "physics/vehicle_collision.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_scene_view.h"
#include "render/gs_render.h"
#include "render/3dd.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {
Quaternionf Conjugate( const Quaternionf &q ) { return Quaternionf( -q.x, -q.y, -q.z, q.w ); }

// [INF] the retail density number (NB_TRAFFIC frequency) is vehicles per
// something the aiconfig does not say; this many cars per unit around the
// player fills the streets the way the retail city looks (was 1.6, which read
// as an empty island).
constexpr float kCarsPerUnit = 4.2f;
constexpr int32_t kMaxAgents = 56;
constexpr float kPi = 3.14159265f;
// Spawn ring around the player (m). A car appears behind the player or far
// ahead (setPlayerHeading), never in the middle of the screen.
constexpr float kSpawnMinFar = 150.0f, kSpawnMaxFar = 520.0f;
constexpr float kSpawnMinNear = 25.0f, kSpawnMaxNear = 110.0f;
// -trafficnear: a close ring, to see the cars in a test shot. Looked up on
// first use: a namespace level `static const bool = CmdLine::HasArg(...)` is
// initialised before the command line is parsed, so it was always false and
// the flag did nothing.
bool IsNearMode() {
    static const bool bNear = CmdLine::HasArg( "-trafficnear" );
    return bNear;
}
float SpawnMin() { return IsNearMode() ? kSpawnMinNear : kSpawnMinFar; }
float SpawnMax() { return IsNearMode() ? kSpawnMaxNear : kSpawnMaxFar; }
// A car may also appear this close when it is behind the player (the closest
// spawn of all: 40 m, never in view).
constexpr float kSpawnBehindMin = 40.0f;

// The traffic is drawn well inside the fog (the world draws to the far plane,
// the old 420 m cull made cars vanish in plain sight); a car is removed only
// beyond the despawn radius and never while it is drawn.
constexpr float kViewRadius = 620.0f, kDespawnRadius = 820.0f;
constexpr float kNearViewRadius = 190.0f, kNearDespawnRadius = 280.0f;
constexpr float kLaneOffset = 1.9f; // right-hand traffic: the driver's right lane [INF: lane width]
// Steering (the smooth turn): the cars are driven by a pure-pursuit target
// along the lane, with a lateral acceleration budget and a yaw rate limit.
// 5.5 m/s^2 is a comfortable road car; it turns a 90 degree corner at about
// 20 km/h instead of snapping the heading in one frame.
constexpr float kMaxLateralAccel = 5.5f;
constexpr float kLookAheadBase = 3.0f, kLookAheadPerSpeed = 0.55f;
constexpr float kLookAheadMin = 5.0f, kLookAheadMax = 12.0f;
// A car approaching a node yields to a traffic car already inside the
// junction box (no T-bone overlap at a crossing).
constexpr float kJunctionApproach = 14.0f, kJunctionBox = 6.5f, kJunctionStopLine = 3.5f;
// The red light stop line (m before the node): outside the junction box, so
// the cars on the green link pass in front of the waiting one.
constexpr float kRedStopLine = 5.5f;

// Wraps an angle to (-pi, pi].
float WrapPi( float angle ) {
    while ( angle > kPi ) {
        angle -= 2.0f * kPi;
    }
    while ( angle <= -kPi ) {
        angle += 2.0f * kPi;
    }
    return angle;
}

bool ReadAttribute( const std::string &line, const char *pName, std::string &out ) {
    const std::string key = std::string( pName ) + "=\"";
    const size_t start = line.find( key );
    if ( start == std::string::npos ) {
        return false;
    }
    const size_t from = start + key.size();
    const size_t end = line.find( '"', from );
    if ( end == std::string::npos ) {
        return false;
    }
    out = line.substr( from, end - from );
    return true;
}

// The traffic car of a database id: islands/<island>/vehicules/traffic/
// <_model_name><_version_name>.bnk, lower case ("Ibiza_" + "Police") [DATA].
// outRadius: _tire_radius with the database conversion (gs_database.cpp).
bool FindTraffic( int32_t carId, std::string &outName, float &outLength, float &outRadius ) {
    if ( gpDatabase == nullptr ) {
        return false;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( car["_id_car"].asInt( -2 ) != carId ) {
            continue;
        }
        std::string name = car["_model_name"].asString() + car["_version_name"].asString();
        for ( char &c : name ) {
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        }
        outName = name;
        outLength = car["_length"].asFloat( 4000.0f ) * 0.001f;
        outRadius = car["_tire_radius"].asFloat( 32.0f ) / 200.0f;
        return !name.empty();
    }
    return false;
}
} // namespace

Traffic::Traffic() : random( 20111 ) {
    const bool bNear = IsNearMode();
    viewRadius = bNear ? kNearViewRadius : kViewRadius;
    despawnRadius = bNear ? kNearDespawnRadius : kDespawnRadius;
}


Traffic::~Traffic() { unload(); }

bool Traffic::load( const std::string &island, const std::vector<Vector3f> &roadNodes,
                    const std::vector<std::pair<int32_t, int32_t>> &roadEdges,
                    const std::vector<std::vector<Vector3f>> &roadShapes, float gameHour ) {
    unload();
    if ( roadEdges.empty() ) {
        return false;
    }
    // The casino island has no ambient traffic (the owner's rule, and it has
    // no aiconfig.cpr either: 2 road nodes, 3 links, a race track around the
    // casino). Checked by island name so a future aiconfig cannot revive it.
    if ( ToLower( island ) == "casino" ) {
        OTDU_LOG_INFO( "Traffic: island '%s' has no ambient traffic\n", island.c_str() );
        return false;
    }
    hour = gameHour;

    // The roof light bars: off on patrol, on during a chase. -policebar turns
    // them on without one (a test shot of the sweep).
    bPoliceLights = CmdLine::HasArg( "-policebar" );
    static const bool bLogModels = CmdLine::HasArg( "-logtraffic" );
    nodes = roadNodes;
    edges = roadEdges;
    adjacency.assign( nodes.size(), {} );
    shapes.assign( edges.size(), EdgeShape() );
    for ( size_t i = 0; i < edges.size(); i++ ) {
        adjacency[edges[i].first].push_back( static_cast<int32_t>( i ) );
        adjacency[edges[i].second].push_back( static_cast<int32_t>( i ) );
        EdgeShape &shape = shapes[i];
        shape.Points = i < roadShapes.size() && roadShapes[i].size() >= 2 ? roadShapes[i]
                                                                           : std::vector<Vector3f>{ nodes[edges[i].first], nodes[edges[i].second] };
        float length = 0.0f;
        for ( size_t k = 0; k < shape.Points.size(); k++ ) {
            if ( k > 0 ) {
                const float dx = shape.Points[k].x - shape.Points[k - 1].x, dz = shape.Points[k].z - shape.Points[k - 1].z;
                length += std::sqrt( dx * dx + dz * dz );
            }
            shape.Cumulative.push_back( length );
        }
        shape.Length = length;
    }
    // islands/<island>/level/aiconfig.cpr -> XML [DATA]
    std::string xml;
    const std::string path = gpFile->getBankFolder() + "islands/" + island + "/level/aiconfig.cpr";
    if ( !LoadCPRText( path.c_str(), xml ) ) {
        OTDU_LOG_WARN( "Traffic: '%s' not available\n", path.c_str() );
        return false;
    }
    const bool bNight = hour < 6.0f || hour >= 20.0f; // [INF]
    // The zone of the player is not decoded [INF]: the density and the mix are
    // the average of the populated zones (a zone whose density never exceeds 1
    // is a rural / empty one and is left out).
    struct ZoneData {
        std::vector<float> Density;
        std::map<int32_t, float> Mix;
    };
    std::vector<ZoneData> zones;
    // TRAFFIC_RULES POLICE_CAR_ID: the police car among the traffic types
    // (the witness of a violation, docs/re/traffic_spec.md) [DATA].
    policeCarId = 0;
    if ( const size_t at = xml.find( "POLICE_CAR_ID" ); at != std::string::npos ) {
        size_t digits = at;
        while ( digits < xml.size() && !isdigit( static_cast<unsigned char>( xml[digits] ) ) ) {
            digits++;
        }
        policeCarId = digits < xml.size() ? atoi( xml.c_str() + digits ) : 0;
    }
    size_t position = 0;
    while ( position < xml.size() ) {
        size_t end = xml.find( '\n', position );
        if ( end == std::string::npos ) {
            end = xml.size();
        }
        const std::string line = xml.substr( position, end - position );
        position = end + 1;
        if ( line.find( "<ZONE>" ) != std::string::npos ) {
            zones.emplace_back();
        } else if ( !zones.empty() ) {
            std::string value;
            if ( line.find( "<NB_TRAFFIC" ) != std::string::npos && ReadAttribute( line, "frequency", value ) ) {
                zones.back().Density.push_back( static_cast<float>( atof( value.c_str() ) ) );
            } else if ( line.find( "<VEHICLE " ) != std::string::npos && ReadAttribute( line, "database_id", value ) ) {
                const int32_t id = atoi( value.c_str() );
                std::string day, night;
                ReadAttribute( line, "frequency_day", day );
                ReadAttribute( line, "frequency_night", night );
                const float weight = static_cast<float>( atof( ( bNight ? night : day ).c_str() ) );
                if ( weight > 0.0f ) {
                    zones.back().Mix[id] = weight;
                }
            }
        }
    }
    std::map<int32_t, float> mixSum;
    int32_t used = 0;
    for ( const ZoneData &zone : zones ) {
        if ( zone.Density.size() < 2 || *std::max_element( zone.Density.begin(), zone.Density.end() ) <= 1.0f ) {
            continue;
        }
        if ( density.empty() ) {
            density.assign( zone.Density.size(), 0.0f );
        }
        for ( size_t i = 0; i < density.size() && i < zone.Density.size(); i++ ) {
            density[i] += zone.Density[i];
        }
        for ( const auto &vehicle : zone.Mix ) {
            mixSum[vehicle.first] += vehicle.second;
        }
        used++;
    }
    if ( used == 0 || mixSum.empty() ) {
        OTDU_LOG_WARN( "Traffic: no populated zone in the aiconfig\n" );
        return false;
    }
    for ( float &frequency : density ) {
        frequency /= static_cast<float>( used );
    }
    std::vector<std::pair<int32_t, float>> mix;
    for ( const auto &vehicle : mixSum ) {
        mix.emplace_back( vehicle.first, vehicle.second / static_cast<float>( used ) );
    }
    OTDU_LOG_INFO( "Traffic: %d populated zones averaged, %zu vehicle ids\n", used, mix.size() );
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const std::string folder = gpFile->getBankFolder() + "islands/" + island + "/vehicules/traffic/";
    for ( const auto &entry : mix ) {
        Type type;
        type.Id = entry.first;
        type.Weight = entry.second;
        std::string name;
        float radius = 0.0f;
        if ( !FindTraffic( entry.first, name, type.Length, radius ) ) {
            OTDU_LOG_WARN( "Traffic: vehicle %d not in the database\n", entry.first );
            continue;
        }
        type.VehicleBank = std::make_unique<Bank>();
        std::string bankPath = folder + name + ".bnk";
        // A variant without a bank (Hawai minivan2) uses variant 1 of the same body.
        if ( !gpFile->fileExists( bankPath.c_str() ) && name.size() > 1 && isdigit( static_cast<unsigned char>( name.back() ) ) ) {
            name.back() = '1';
            bankPath = folder + name + ".bnk";
        }
        if ( !gpFile->fileExists( bankPath.c_str() ) || !type.VehicleBank->load( bankPath.c_str() ) ) {
            OTDU_LOG_WARN( "Traffic: vehicle %d: '%s' not available\n", entry.first, bankPath.c_str() );
            continue;
        }
        gpRender->getTextureCache().loadBankTextures( *type.VehicleBank, type.VehicleBank->getPath().c_str() );
        type.Model = std::make_unique<EdenModel>();
        if ( !type.Model->load( *pRenderer, *type.VehicleBank, name.c_str(), nullptr, type.VehicleBank->getPath().c_str() ) ) {
            OTDU_LOG_WARN( "Traffic: vehicle %d: model '%s' cannot be loaded\n", entry.first, name.c_str() );
            continue;
        }
        if ( bLogModels ) {
            // Model space bounds (the draw places the pivot on the road) and
            // the node tree (which nodes carry the lamp planes).
            const Vector3f &bmin = type.Model->getBoundsMin();
            const Vector3f &bmax = type.Model->getBoundsMax();
            OTDU_LOG_INFO( "Traffic: '%s' bounds min (%.3f, %.3f, %.3f) max (%.3f, %.3f, %.3f)\n", name.c_str(), bmin.x, bmin.y,
                           bmin.z, bmax.x, bmax.y, bmax.z );
            for ( const SceneNode &node : type.Model->getScene().getNodes() ) {
                OTDU_LOG_INFO( "Traffic:   node '%s' at (%.3f, %.3f, %.3f)\n", node.Name.c_str(), node.World.m[3][0],
                               node.World.m[3][1], node.World.m[3][2] );
            }
        }
        // The model pivot sits below the wheel contact plane: the WLR_xx wheel
        // centres are at model y 0.05..0.16 while the tire radius is ~0.30 m
        // (-logtraffic dump). Lifting by radius - wheelY puts the tires on the
        // road (the retail draw places the pivot on the road too, the cars ride
        // on their tires [INF: no traffic-specific offset found]).
        // Wheels: WLR_<FL|FR|RL|RR> (the wheel hub nodes) or DISC_<...> on the
        // models that carry no wheel node (the taxi, bus, van ...): the lift
        // and the rolling wheels of the draw use the same node.
        static const char *const kWheelNames[4] = { "WLR_FL", "WLR_FR", "WLR_RL", "WLR_RR" };
        static const char *const kDiscNames[4] = { "DISC_FL", "DISC_FR", "DISC_RL", "DISC_RR" };
        const std::vector<SceneNode> &sceneNodes = type.Model->getScene().getNodes();
        for ( int32_t w = 0; w < 4; w++ ) {
            int32_t wheelNode = type.Model->getScene().findNode( kWheelNames[w] );
            if ( wheelNode < 0 ) {
                wheelNode = type.Model->getScene().findNode( kDiscNames[w] );
            }
            if ( wheelNode >= 0 && static_cast<size_t>( wheelNode ) < sceneNodes.size() ) {
                type.WheelNodes[w] = wheelNode;
                type.WheelBase[w] = sceneNodes[static_cast<size_t>( wheelNode )].World;
            }
        }
        type.WheelRadius = radius > 0.05f ? radius : 0.32f;
        const int32_t liftNode = type.WheelNodes[0] >= 0 ? type.WheelNodes[0] : type.WheelNodes[2];
        if ( liftNode >= 0 && radius > 0.05f ) {
            type.GroundLift = radius - sceneNodes[static_cast<size_t>( liftNode )].World.m[3][1];
        }
        // The lamp glow sprites: nodes FA_<L|B|S|R|W>_<FL|FR|RL|RR>[_n] with
        // the TD_Sprite FRL_<x> materials ('light_fx' / 'light_fxred',
        // tinted by the vertex colour). Visibility is per draw — front
        // lights + rear position at night, stop sprites for a braking agent
        // (docs/re/car_lights_spec.md 2.5, the FA_ letter functions).
        for ( const EdenModel::Item &item : type.Model->getItems() ) {
            const SceneNode &node = type.Model->getScene().getNodes()[item.NodeIndex];
            int32_t function = -1;
            if ( node.Name.compare( 0, 3, "FA_" ) == 0 && node.Name.size() >= 6 ) {
                function = LampFunctionIndex( node.Name[3] );
            } else {
                // A glow quad on a differently named node (FX_LIGHT
                // materials on a few props): treated as a front light.
                const EdenMaterial &material = type.Model->getMaterials()[item.MaterialIndex];
                if ( material.Name.compare( 0, 8, "FX_LIGHT" ) == 0 ) {
                    function = 0;
                }
            }
            if ( function >= 0 ) {
                bool bKnown = false;
                for ( const int32_t known : type.LampNodes[function] ) {
                    bKnown = bKnown || known == item.NodeIndex;
                }
                if ( !bKnown ) {
                    type.LampNodes[function].push_back( item.NodeIndex );
                }
                // The turn indicators are also split by side (FA_W_FL/FR/RL/RR):
                // the AI blinks the side of the link it takes at a junction.
                if ( function == 4 && node.Name.size() >= 7 ) {
                    const char side = node.Name[6];
                    if ( side == 'L' || side == 'R' ) {
                        std::vector<int32_t> &group = type.IndicatorNodes[side == 'R' ? 1 : 0];
                        if ( std::find( group.begin(), group.end(), item.NodeIndex ) == group.end() ) {
                            group.push_back( item.NodeIndex );
                        }
                    }
                }
            }
        }
        // One bloom anchor per lamp sprite node (appendLampGlows places them
        // through the agent's world matrix).
        EdenCollectLampAnchors( *type.Model, type.LampGlows );
        {
            size_t total = 0;
            for ( const std::vector<int32_t> &group : type.LampNodes ) {
                total += group.size();
            }
            int32_t wheels = 0;
            for ( const int32_t wheelNode : type.WheelNodes ) {
                wheels += wheelNode >= 0 ? 1 : 0;
            }
            OTDU_LOG_INFO( "Traffic: '%s' ground lift %.3f m (radius %.3f, %d wheel node(s)), lamp sprites %zu (L %zu B "
                           "%zu S %zu R %zu W %zu)\n",
                           name.c_str(), type.GroundLift, radius, wheels, total, type.LampNodes[0].size(),
                           type.LampNodes[1].size(), type.LampNodes[2].size(), type.LampNodes[3].size(),
                           type.LampNodes[4].size() );
        }

        // The police roof light bar (the Gyro material + the FA_G ring quads;
        // docs/re/car_lights_spec.md 6). The model of a non-police car has
        // none and the object stays empty.
        type.PoliceBar.collect( *type.Model, gpRender != nullptr ? &gpRender->getTextureCache() : nullptr );
        // A bar alone does not make a police car: the Hawai fire van
        // (pompier1) carries one too, so the island's POLICE_CAR_ID decides
        // which type is the police car (it was picked as "the first bar" and
        // -trafficparkpolice parked the fire van).
        const bool bHasBar = type.PoliceBar.isPoliceBar();
        type.bPolice = bHasBar && policeCarId != 0 && type.Id == policeCarId;
        if ( bHasBar ) {
            OTDU_LOG_INFO( "Traffic: '%s' roof light bar: %u gyro material(s), rings A %zu B %zu G %zu R %zu\n", name.c_str(),
                           type.PoliceBar.getMaterialCount(), type.PoliceBar.getNodes( 3 ).size(),
                           type.PoliceBar.getNodes( 2 ).size(), type.PoliceBar.getNodes( 1 ).size(),
                           type.PoliceBar.getNodes( 0 ).size() );
            // The shader of the lens and of the gyro glow quads, for the
            // TD_Sprite billboard diagnosis.
            for ( const EdenMaterial &material : type.Model->getMaterials() ) {
                if ( material.Name == "Gyro" || material.Name == "FRL_G" ) {
                    OTDU_LOG_INFO( "Traffic: '%s' material '%s' shader '%s'\n", name.c_str(), material.Name.c_str(),
                                   material.pProgram != nullptr ? material.pProgram->Name.c_str() : "none" );
                }
            }
        }

        OTDU_LOG_INFO( "Traffic: vehicle %d '%s' weight %.2f length %.2f m\n", type.Id, name.c_str(), type.Weight, type.Length );
        if ( type.bPolice ) {
            OTDU_LOG_INFO( "Traffic: '%s' is the island police car (POLICE_CAR_ID %d)\n", name.c_str(), policeCarId );
        }
        types.push_back( std::move( type ) );
    }
    loadRivals();
    bLoaded = !types.empty();
    neutraliseGlassArt();
    buildComponents();
    OTDU_LOG_INFO( "Traffic: %zu vehicle types, %zu density points, hour %.1f -> %.1f cars, police id %d, %zu road graph "
                   "component(s), view %.0f m / despawn %.0f m\n",
                   types.size(), density.size(), hour, targetCount(), policeCarId, roadComponentCount, viewRadius,
                   despawnRadius );
    return bLoaded;
}

void Traffic::appendHeadlightCones( std::vector<EdenHeadlightCone> &cones, const Vector3f &cameraPosition,
                                    bool bNight, size_t maxCones ) const {
    if ( !bNight || CmdLine::HasArg( "-notrafficlights" ) ) {
        return;
    }
    struct Candidate {
        EdenHeadlightCone cone;
        float distanceSquared;
    };
    std::vector<Candidate> candidates;
    for ( const Agent &agent : agents ) {
        if ( !agent.bVisible ) {
            continue;
        }
        Vector3f forward = agent.World.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        forward.y *= 0.25f; // levelled, a slight down aim
        if ( forward.dot( forward ) <= 1e-6f ) {
            continue;
        }
        forward = forward.normalized();
        EdenHeadlightCone cone;
        cone.Position = agent.World.getTranslation() + forward * 2.0f + Vector3f( 0.0f, 0.62f, 0.0f );
        cone.Direction = forward;
        cone.Intensity = 1.0f;
        const Vector3f delta = cone.Position - cameraPosition;
        candidates.push_back( { cone, delta.dot( delta ) } );
    }
    std::sort( candidates.begin(), candidates.end(),
               []( const Candidate &a, const Candidate &b ) { return a.distanceSquared < b.distanceSquared; } );
    for ( const Candidate &candidate : candidates ) {
        if ( cones.size() >= maxCones ) {
            break;
        }
        cones.push_back( candidate.cone );
    }
}

void Traffic::appendBrakeCones( std::vector<EdenHeadlightCone> &rear, const Vector3f &cameraPosition, bool bNight,
                                size_t maxCones ) const {
    if ( !bLoaded || CmdLine::HasArg( "-notrafficlights" ) ) {
        return;
    }
    struct Candidate {
        EdenHeadlightCone cone;
        float distanceSquared;
    };
    std::vector<Candidate> candidates;
    for ( const Agent &agent : agents ) {
        if ( !agent.bVisible || agent.BrakeHold <= 0.0f ) {
            continue;
        }
        Vector3f backward = agent.World.transformVector( Vector3f( 0.0f, 0.0f, -1.0f ) );
        backward.y = 0.0f;
        if ( backward.dot( backward ) <= 1e-6f ) {
            continue;
        }
        backward = backward.normalized();
        EdenHeadlightCone pool;
        pool.Position = agent.World.getTranslation() + backward * 1.6f + Vector3f( 0.0f, 0.6f, 0.0f );
        pool.Direction = backward;
        pool.Range = 6.0f;
        pool.Intensity = bNight ? 0.55f : 0.22f;
        const Vector3f delta = pool.Position - cameraPosition;
        candidates.push_back( { pool, delta.dot( delta ) } );
    }
    std::sort( candidates.begin(), candidates.end(),
               []( const Candidate &a, const Candidate &b ) { return a.distanceSquared < b.distanceSquared; } );
    for ( const Candidate &candidate : candidates ) {
        if ( rear.size() >= maxCones ) {
            break;
        }
        rear.push_back( candidate.cone );
    }
}

void Traffic::appendLampGlows( std::vector<EdenLampGlow> &out, const Vector3f &cameraPosition, bool bNight ) const {
    if ( !bLoaded || CmdLine::HasArg( "-nolampglow" ) ) {
        return;
    }
    static const bool bNoTrafficLights = CmdLine::HasArg( "-notrafficlights" );
    const float night = bNight ? 1.0f : 0.0f;
    for ( const Agent &agent : agents ) {
        if ( !agent.bVisible ) {
            continue;
        }
        const Type &type = types[static_cast<size_t>( agent.Type )];
        const Vector3f delta = agent.Position - cameraPosition;
        if ( delta.dot( delta ) > 450.0f * 450.0f ) {
            continue;
        }
        // A rival: the real car's own glow anchors (CinematicCar's, the
        // ?A_ nodes and the light materials) on its fitted pose — a rival
        // has no FA_ sprites, so this comes before the anchor check.
        if ( type.Rival ) {
            AiCar *pRival = type.Rival.get();
            if ( pRival->getCar() != nullptr && pRival->getCar()->isLoaded() && surfaceFn ) {
                pRival->getCar()->appendLampGlows( pRival->pose( agent.Position, agent.Yaw, surfaceFn ), out );
            }
            continue;
        }
        if ( type.LampGlows.empty() ) {
            continue;
        }
        // The blink phase gate of Traffic::draw (the W sprites blink alone).
        const bool bBraking = agent.BrakeHold > 0.0f;
        const bool bBlink = !bNoTrafficLights && agent.IndicatorSide != 0 &&
                            std::fmod( agent.IndicatorPhase, 1.0f ) < 0.6f;
        Matrix4x4f world = agent.World;
        world.m[3][1] += type.GroundLift;
        const size_t glowsBefore = out.size();
        for ( const EdenLampAnchor &anchor : type.LampGlows ) {
            float value = 0.0f, dayShare = 0.0f, core = 1.0f, spread = 0.22f;
            Vector3f color;
            switch ( anchor.Function ) {
            case 0: // L: the front lights (the sprite gate: night only)
                if ( bNoTrafficLights || !bNight ) {
                    continue;
                }
                value = 1.0f;
                color = Vector3f( 1.00f, 0.93f, 0.82f );
                spread = 0.12f;
                break;
            case 1: // B: the rear position lights, night only
                if ( bNoTrafficLights || !bNight ) {
                    continue;
                }
                value = 0.7f;
                color = Vector3f( 1.00f, 0.09f, 0.04f );
                core = 0.0f;
                break;
            case 2: // S: the stop lamps, day and night (the sprite gate)
                if ( bNoTrafficLights || !bBraking ) {
                    continue;
                }
                value = 1.0f;
                color = Vector3f( 1.00f, 0.06f, 0.03f );
                dayShare = 0.55f;
                break;
            case 3: // R: never shown (the sprite gate)
            default:
                continue;
            case 4: // W: the blinker of the agent's indicating side
                if ( !bBlink || ( anchor.bLeft ? agent.IndicatorSide > 0 : agent.IndicatorSide < 0 ) ) {
                    continue;
                }
                value = 0.85f;
                color = Vector3f( 1.00f, 0.46f, 0.05f );
                dayShare = 0.70f;
                spread = 0.25f;
                break;
            }
            const float strength = value * ( dayShare + ( 1.0f - dayShare ) * night );
            if ( strength <= 0.01f ) {
                continue;
            }
            EdenLampGlow glow;
            glow.Position = world.transformPoint( anchor.Local );
            const Vector3f normal = world.transformVector( anchor.Normal );
            glow.Normal = normal.length() > 1e-4f ? normal.normalized() : Vector3f( 0.0f, 0.0f, 1.0f );
            glow.Color = color;
            glow.Size = anchor.Size * ( 0.75f + 0.25f * night );
            glow.Strength = Saturate( strength );
            glow.Core = core;
            glow.Spread = spread;
            out.push_back( glow );
        }
        // One-shot proof for a headless run (the -logcarlights pattern).
        static bool bLoggedGlows = false;
        if ( !bLoggedGlows && out.size() > glowsBefore ) {
            bLoggedGlows = true;
            OTDU_LOG_INFO( "Traffic: lamp glows drawn (%zu quads on a car this frame)\n", out.size() - glowsBefore );
        }
    }
}

void Traffic::unload() {
    rivalTypes = 0;
    rivalTimer = 0.0f;
    bLoaded = false;
    types.clear();
    agents.clear();
    hornEvents.clear();
    density.clear();
    nodes.clear();
    edges.clear();
    adjacency.clear();
    shapes.clear();
    component.clear();
    roadComponentCount = 0;
    playerComponent = -1;
    componentTimer = 0.0f;
    bEnvironmentAssigned = false;
    bPoliceLights = false;
    gyroClock = 0.0f;
}


float Traffic::targetCount() const {
    if ( density.size() < 2 ) {
        return 0.0f;
    }
    // NB_TRAFFIC points are every 2 hours, 0..24.
    const float h = Clamp( hour, 0.0f, 24.0f ) * 0.5f;
    const size_t index = Min<size_t>( static_cast<size_t>( h ), density.size() - 2 );
    const float t = h - static_cast<float>( index );
    const float frequency = density[index] * ( 1.0f - t ) + density[index + 1] * t;
    // A road with traffic at all keeps a minimum of cars around the player, so
    // a village street feels lived in instead of the 3-4 cars the raw
    // frequency gives at the quiet hours [INF].
    const float count = frequency <= 1.0f ? frequency * kCarsPerUnit : Max( frequency * kCarsPerUnit, 10.0f );
    return Min( count, static_cast<float>( kMaxAgents ) );
}

void Traffic::buildComponents() {
    // Flood fill over the links (a node joins the component of its neighbours).
    // A car never spawns on, or drives into, a component the player is not on:
    // an island's road graph can hold islets and separate networks (the
    // casino island next to Ibiza, Hawai's second island), and traffic there
    // used to exist with no player on it.
    component.assign( nodes.size(), -1 );
    roadComponentCount = 0;
    std::vector<int32_t> stack;
    for ( size_t start = 0; start < nodes.size(); start++ ) {
        if ( component[start] >= 0 || adjacency[start].empty() ) {
            continue;
        }
        const int32_t id = static_cast<int32_t>( roadComponentCount++ );
        component[start] = id;
        stack.clear();
        stack.push_back( static_cast<int32_t>( start ) );
        while ( !stack.empty() ) {
            const int32_t node = stack.back();
            stack.pop_back();
            for ( const int32_t link : adjacency[static_cast<size_t>( node )] ) {
                const std::pair<int32_t, int32_t> &edge = edges[static_cast<size_t>( link )];
                const int32_t other = edge.first == node ? edge.second : edge.first;
                if ( other == node || static_cast<size_t>( other ) >= component.size() || component[other] >= 0 ) {
                    continue;
                }
                component[other] = id;
                stack.push_back( other );
            }
        }
    }
    playerComponent = -1;
    componentTimer = 0.0f;
}

void Traffic::updatePlayerComponent( const Vector3f &player ) {
    if ( component.empty() ) {
        return;
    }
    float nearest = 1e18f;
    int32_t nearestNode = -1;
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        const float dx = nodes[i].x - player.x, dz = nodes[i].z - player.z;
        const float distance = dx * dx + dz * dz;
        if ( distance < nearest ) {
            nearest = distance;
            nearestNode = static_cast<int32_t>( i );
        }
    }
    if ( nearestNode >= 0 ) {
        const int32_t found = component[static_cast<size_t>( nearestNode )];
        if ( found != playerComponent ) {
            playerComponent = found;
            OTDU_LOG_INFO( "Traffic: player on road graph component %d (nearest node %d, %.1f m)\n", playerComponent,
                           nearestNode, std::sqrt( nearest ) );
        }
    }
}

int32_t Traffic::pickType() {
    float total = 0.0f;
    for ( const Type &type : types ) {
        total += type.Weight;
    }
    float roll = std::uniform_real_distribution<float>( 0.0f, total )( random );
    for ( size_t i = 0; i < types.size(); i++ ) {
        roll -= types[i].Weight;
        if ( roll <= 0.0f ) {
            return static_cast<int32_t>( i );
        }
    }
    // The last weighted type (the rivals at the end of the list weigh 0).
    for ( size_t i = types.size(); i-- > 0; ) {
        if ( types[i].Weight > 0.0f ) {
            return static_cast<int32_t>( i );
        }
    }
    return 0;
}

bool Traffic::isRed( int32_t node, int32_t edge ) const {
    const std::vector<int32_t> &links = adjacency[node];
    static const bool bNoLights = CmdLine::HasArg( "-notrafficlights" );
    if ( bNoLights || links.size() < 3 ) {
        return false;
    }
    // Direction of a link leaving the node (flat, unit).
    auto outward = [&]( int32_t link, float &x, float &z ) {
        const std::vector<Vector3f> &p = shapes[link].Points;
        const bool bStart = edges[link].first == node;
        const Vector3f &from = bStart ? p[0] : p[p.size() - 1];
        const Vector3f &to = bStart ? p[1] : p[p.size() - 2];
        x = to.x - from.x;
        z = to.z - from.z;
        const float length = std::sqrt( x * x + z * z );
        if ( length > 0.0001f ) {
            x /= length;
            z /= length;
        }
    };
    constexpr float kGreen = 7.0f, kSlot = 9.0f;
    const float t = clock + static_cast<float>( node ) * 3.7f;
    const float cycle = kSlot * static_cast<float>( links.size() );
    const float phase = std::fmod( t, cycle );
    if ( std::fmod( phase, kSlot ) >= kGreen ) {
        return true; // all red
    }
    // The links on the axis of the green one (both directions) go together.
    float gx, gz, mx, mz;
    outward( links[static_cast<size_t>( phase / kSlot )], gx, gz );
    outward( edge, mx, mz );
    return std::fabs( gx * mx + gz * mz ) < 0.7f;
}

void Traffic::chooseNextEdge( Agent &agent ) {
    const std::pair<int32_t, int32_t> &current = edges[static_cast<size_t>( agent.Edge )];
    const int32_t node = agent.bForward ? current.second : current.first;
    // Direction of travel leaving the current link at the node.
    float x, y, z, tx, tz;
    sampleEdge( agent.Edge, agent.bForward, shapes[static_cast<size_t>( agent.Edge )].Length, x, y, z, tx, tz );
    int32_t best = -1;
    bool bBestForward = agent.bForward;
    float bestScore = 1e30f;
    float bestAngle = 0.0f;
    float bestNx = tx, bestNz = tz;
    if ( node >= 0 && static_cast<size_t>( node ) < adjacency.size() ) {
        const std::vector<int32_t> &links = adjacency[static_cast<size_t>( node )];
        for ( const int32_t next : links ) {
            if ( next == agent.Edge && links.size() > 1 ) {
                continue; // not straight back unless it is a dead end
            }
            const std::pair<int32_t, int32_t> &edge = edges[static_cast<size_t>( next )];
            const bool bForward = edge.first == node;
            float nx, ny, nz, ntx, ntz;
            sampleEdge( next, bForward, 0.0f, nx, ny, nz, ntx, ntz );
            const float dot = Clamp( tx * ntx + tz * ntz, -1.0f, 1.0f );
            const float angle = std::acos( dot );
            // The straightest link wins, with a little noise so the traffic
            // does not take the same road every time; a U-turn (pi) only
            // remains for a dead end, where it is the single option. The old
            // random choice turned at every junction and produced phantom
            // traffic jams (every car slowed for every node, straight or not).
            const float score = angle + 0.35f * std::uniform_real_distribution<float>( 0.0f, 1.0f )( random );
            if ( score < bestScore ) {
                bestScore = score;
                best = next;
                bBestForward = bForward;
                bestAngle = angle;
                bestNx = ntx;
                bestNz = ntz;
            }
        }
    }
    if ( best < 0 ) {
        // A dead end: turn round on the same link.
        agent.NextEdge = agent.Edge;
        agent.bNextForward = !agent.bForward;
        agent.NextTurnAngle = 3.14159265f;
        agent.IndicatorSide = 0;
    } else {
        agent.NextEdge = best;
        agent.bNextForward = bBestForward;
        agent.NextTurnAngle = bestAngle;
        // Right of the current travel is forward x up = (-tz, tx): the
        // EdenSceneView camera is right handed (screen right is -X when the
        // forward is +Z, walk_controller.cpp / chase_camera.cpp), so the old
        // (tz, -tx) was the driver's LEFT. A positive dot with the next
        // direction means the car turns right.
        const float right = bestNz * tx - bestNx * tz;
        agent.IndicatorSide = right > 0.15f ? 1 : ( right < -0.15f ? -1 : 0 );
    }
    agent.IndicatorPhase = 0.0f;
    agent.IndicatorHold = 1.2f;
}

void Traffic::sampleEdge( int32_t edge, bool bForward, float distance, float &outX, float &outY, float &outZ, float &outTx,
                          float &outTz ) const {
    const EdgeShape &shape = shapes[edge];
    const float s = Clamp( bForward ? distance : shape.Length - distance, 0.0f, shape.Length );
    size_t k = 1;
    while ( k + 1 < shape.Points.size() && shape.Cumulative[k] < s ) {
        k++;
    }
    const Vector3f &a = shape.Points[k - 1];
    const Vector3f &b = shape.Points[k];
    const float span = Max( shape.Cumulative[k] - shape.Cumulative[k - 1], 0.001f );
    const float t = Clamp( ( s - shape.Cumulative[k - 1] ) / span, 0.0f, 1.0f );
    outX = a.x + ( b.x - a.x ) * t;
    outY = a.y + ( b.y - a.y ) * t;
    outZ = a.z + ( b.z - a.z ) * t;
    float tx = b.x - a.x, tz = b.z - a.z;
    const float length = std::sqrt( tx * tx + tz * tz );
    if ( length > 0.0001f ) {
        tx /= length;
        tz /= length;
    }
    outTx = bForward ? tx : -tx;
    outTz = bForward ? tz : -tz;
}

Vector3f Traffic::lanePosition( const Agent &agent, float &outYaw ) const {
    Vector3f point;
    pathAhead( agent, 0.0f, point, outYaw );
    return point;
}

void Traffic::pathAhead( const Agent &agent, float ahead, Vector3f &outPoint, float &outYaw ) const {
    const EdgeShape &shape = shapes[static_cast<size_t>( agent.Edge )];
    float x, y, z, tx, tz;
    sampleEdge( agent.Edge, agent.bForward, agent.Distance + Max( ahead, 0.0f ), x, y, z, tx, tz );
    // Right of the direction of travel: forward x up = ( -tz, tx ) in this
    // right-handed camera frame (screen right is -X at yaw 0). The lane is
    // the driver's right, i.e. the right-hand lane of a two-way road.
    outPoint = Vector3f( x - tz * kLaneOffset, agent.LastY, z + tx * kLaneOffset );
    outYaw = std::atan2( tx, tz );
    const float remaining = shape.Length - agent.Distance;
    if ( ahead <= remaining || agent.NextEdge < 0 ) {
        return;
    }
    // Past the node: the target follows the next link while the tangent and
    // the lane offset turn over the crossing, so the steering target sweeps an
    // arc through the junction instead of jumping to the other lane. This is
    // what makes a 90 degree corner a curve rather than a snap.
    const int32_t next = agent.NextEdge;
    if ( static_cast<size_t>( next ) >= shapes.size() ) {
        return;
    }
    const float over = ahead - remaining;
    float nx, ny, nz, ntx, ntz;
    sampleEdge( next, agent.bNextForward, over, nx, ny, nz, ntx, ntz );
    const float blend = Clamp( over / 4.0f, 0.0f, 1.0f );
    const float mixX = tx * ( 1.0f - blend ) + ntx * blend;
    const float mixZ = tz * ( 1.0f - blend ) + ntz * blend;
    const float mixLength = std::sqrt( mixX * mixX + mixZ * mixZ );
    float dirX = mixX, dirZ = mixZ;
    if ( mixLength > 0.0001f ) {
        dirX /= mixLength;
        dirZ /= mixLength;
    } else {
        dirX = 0.0f;
        dirZ = 1.0f;
    }
    outPoint = Vector3f( nx - dirZ * kLaneOffset, agent.LastY, nz + dirX * kLaneOffset );
    outYaw = std::atan2( dirX, dirZ );
}


bool Traffic::spawn( const Vector3f &player, const CinematicCar::SurfaceFunction &surface, int32_t forcedType ) {
    static int32_t shortEdge = 0, outOfRange = 0, noGround = 0, crowded = 0, spawned = 0, offComponent = 0,
                   inView = 0, outOfRing = 0;
    const bool bHasHeading = playerHeading.length() > 0.0001f;
    const Vector3f heading = bHasHeading ? playerHeading.normalized() : Vector3f();
    // Candidate links: the ones whose middle lies in the spawn ring, on the
    // road graph left to the player (the island he is on) and - when the
    // heading is known - behind the player or far ahead, so a car does not
    // appear in the middle of the screen.
    std::vector<size_t> candidates;
    std::vector<float> weights;
    candidates.reserve( edges.size() );
    weights.reserve( edges.size() );
    for ( size_t i = 0; i < edges.size(); i++ ) {
        const Vector3f &middle = shapes[i].Points[shapes[i].Points.size() / 2];
        const float mx = middle.x - player.x, mz = middle.z - player.z;
        const float distance = std::sqrt( mx * mx + mz * mz );
        // 40 m is the closest a car may appear (it is behind the player then).
        if ( distance < kSpawnBehindMin || distance > SpawnMax() + 60.0f ) {
            continue;
        }
        if ( playerComponent >= 0 && component.size() == nodes.size() ) {
            const size_t first = static_cast<size_t>( edges[i].first ), second = static_cast<size_t>( edges[i].second );
            if ( first >= component.size() || second >= component.size() || component[first] != playerComponent ||
                 component[second] != playerComponent ) {
                offComponent++;
                continue;
            }
        }
        // 0 across .. 1 straight ahead of the player.
        const float ahead = distance > 1.0f && bHasHeading ? ( mx * heading.x + mz * heading.z ) / distance : 0.0f;
        // In sight and close: a car there would pop in front of the player.
        if ( ahead > 0.2f && distance < SpawnMax() * 0.75f ) {
            inView++;
            continue;
        }
        // The spawn ring, plus a close window behind the player (a car that
        // comes up from behind makes the road feel used without ever being
        // seen to appear).
        const bool bInRing = distance >= SpawnMin();
        const bool bBehind = bHasHeading && ahead < -0.35f && distance >= kSpawnBehindMin;
        if ( !bInRing && !bBehind ) {
            outOfRing++;
            continue;
        }
        // Far links are accepted wherever they are; near ones preferably behind.
        candidates.push_back( i );
        weights.push_back( 1.0f + Max( 0.0f, -ahead ) * 2.0f );
    }
    if ( candidates.empty() ) {
        return false;
    }
    float totalWeight = 0.0f;
    for ( const float weight : weights ) {
        totalWeight += weight;
    }
    for ( int32_t attempt = 0; attempt < 12; attempt++ ) {
        float roll = std::uniform_real_distribution<float>( 0.0f, totalWeight )( random );
        size_t pick = 0;
        for ( ; pick + 1 < weights.size(); pick++ ) {
            roll -= weights[pick];
            if ( roll <= 0.0f ) {
                break;
            }
        }
        const size_t edgeIndex = candidates[pick];

        const bool bReverse = std::uniform_int_distribution<int>( 0, 1 )( random ) != 0;
        Agent agent;
        agent.Edge = static_cast<int32_t>( edgeIndex );
        agent.bForward = !bReverse;
        const float length = shapes[edgeIndex].Length;
        if ( length < 30.0f ) {
            shortEdge++;
            continue;
        }
        agent.Distance = std::uniform_real_distribution<float>( 0.0f, length )( random );
        float yaw = 0.0f;
        agent.LastY = player.y;
        Vector3f flat = lanePosition( agent, yaw );
        const float dx = flat.x - player.x, dz = flat.z - player.z;
        const float distance = std::sqrt( dx * dx + dz * dz );
        // The lane point may sit a little off the candidate's ring: the close
        // behind window is allowed too (40 m .. SpawnMin when behind).
        const bool bBehind = bHasHeading && ( dx * heading.x + dz * heading.z ) < -0.35f * Max( distance, 1.0f );
        if ( distance > SpawnMax() || distance < ( bBehind ? kSpawnBehindMin : SpawnMin() ) ) {
            outOfRange++;
            continue;
        }
        // Ground: the collision raycast reaches 60 m; try a few start heights.
        float ground = 0.0f;
        bool bGround = false;
        for ( const float offset : { 40.0f, 100.0f, -20.0f, -80.0f } ) {
            if ( surface( flat.x, flat.z, player.y + offset, ground ) ) {
                bGround = true;
                break;
            }
        }
        if ( !bGround ) {
            noGround++;
            continue;
        }
        // Not on top of another car.
        bool bFree = true;
        for ( const Agent &other : agents ) {
            const float ox = other.Position.x - flat.x, oz = other.Position.z - flat.z;
            if ( ox * ox + oz * oz < 20.0f * 20.0f ) {
                bFree = false;
                break;
            }
        }
        if ( !bFree ) {
            crowded++;
            continue;
        }
        agent.Type = forcedType >= 0 ? forcedType : pickType();
        agent.AudioId = nextAudioId++;
        agent.HornCooldown = std::uniform_real_distribution<float>( 2.0f, 9.0f )( random );
        agent.CruiseSpeed = std::uniform_real_distribution<float>( 11.0f, 19.0f )( random ); // 40..68 km/h [INF]
        if ( forcedType >= 0 ) {
            // A rival drives a little quicker than the traffic: 58..86 km/h [INF].
            agent.CruiseSpeed = std::uniform_real_distribution<float>( 16.0f, 24.0f )( random );
        }
        agent.Speed = agent.CruiseSpeed;
        agent.LastY = ground;
        agent.Yaw = yaw;
        agent.Position = Vector3f( flat.x, ground, flat.z );
        agents.push_back( agent );
        if ( ++spawned % 5 == 1 ) {
            OTDU_LOG_INFO( "Traffic: spawned %d (short %d, range %d, off ring %d, no ground %d, crowded %d, off "
                           "component %d, in view %d)\n",
                           spawned, shortEdge, outOfRange, outOfRing, noGround, crowded, offComponent, inView );
        }
        return true;
    }
    if ( ( noGround + outOfRange ) % 200 == 0 ) {
        OTDU_LOG_INFO( "Traffic: no spawn (short %d, range %d, off ring %d, no ground %d, crowded %d, off component "
                       "%d, in view %d)\n",
                       shortEdge, outOfRange, outOfRing, noGround, crowded, offComponent, inView );
    }
    return false;
}


void Traffic::tick( float deltaTime, const Vector3f &player, const CinematicCar::SurfaceFunction &surface ) {
    if ( !bLoaded ) {
        return;
    }
    // The roof light bar sweep runs even while the test cars are parked.
    gyroClock += deltaTime;
    surfaceFn = surface;
    tickRivals( deltaTime, player, surface );
    // -trafficpark (debug): pin the first cars right in front of the player
    // (9 / 15 / 21 m, right lane) at standstill, to inspect wheels and lights.
    static const bool bPark = CmdLine::HasArg( "-trafficpark" );
    // -trafficbrake (debug): every live agent holds its brake lamps lit, so a
    // screenshot can catch the stop sprites without racing the AI.
    static const bool bForceBrake = CmdLine::HasArg( "-trafficbrake" );
    const int32_t parkCount = bPark ? Min( static_cast<int32_t>( agents.size() ), 3 ) : 0;
    if ( bPark && parkForward.length() > 0.0001f && parkCount > 0 ) {
        const Vector3f fwd = parkForward.normalized();
        // Driver's right: forward x up = (-z, x) in the right-handed frame.
        const Vector3f right( -fwd.z, 0.0f, fwd.x );
        // -trafficparkface: 0 the first car faces the camera (default),
        // 1 all the cars face away (their rear lamps toward the camera).
        static const int32_t parkFace = CmdLine::GetArgInt( "-trafficparkface", 0 );
        // With -policebar, park the island's police car first so the bar can
        // be inspected in a shot. -trafficparkpolice parks it with the bar
        // off (the A/B reference for the sweep).
        static const bool bPolicePark =
            CmdLine::HasArg( "-policebar" ) || CmdLine::HasArg( "-trafficparkpolice" );
        for ( int32_t i = 0; i < parkCount; i++ ) {
            // Three columns at 12 m (one facing the camera), lateral -2.5 / 2 / 6.5 m.
            // The -policebar shot puts the police car in the clear right-hand
            // column instead of the left one, which sits inside the pavement
            // at the FREERIDE spawn.
            const float lateral = i == 0 ? ( bPolicePark ? 2.0f : -2.5f ) : ( i == 1 ? 2.0f : 6.5f );
            const float yaw = std::atan2( fwd.x, fwd.z ) + ( i == 0 && parkFace == 0 ? 3.14159265f : 0.0f );
            Agent &agent = agents[static_cast<size_t>( i )];
            if ( i == 0 && bPolicePark ) {
                // The island's POLICE_CAR_ID first (a bar can belong to a
                // fire van too), then any other bar-equipped type.
                int32_t policeType = -1;
                for ( size_t t = 0; t < types.size(); t++ ) {
                    if ( types[t].Id == policeCarId ) {
                        policeType = static_cast<int32_t>( t );
                        break;
                    }
                }
                if ( policeType < 0 ) {
                    for ( size_t t = 0; t < types.size(); t++ ) {
                        if ( types[t].bPolice ) {
                            policeType = static_cast<int32_t>( t );
                            break;
                        }
                    }
                }
                if ( policeType >= 0 ) {
                    agent.Type = policeType;
                }
            }
            agent.NextEdge = -1;
            agent.IndicatorSide = 0;
            agent.IndicatorPhase = 0.0f;
            agent.IndicatorHold = 0.0f;
            const Vector3f at = player + fwd * 12.0f + right * lateral;
            float ground = 0.0f;
            const bool bGround = surface( at.x, at.z, agent.LastY + 6.0f, ground ) || surface( at.x, at.z, player.y + 6.0f, ground );
            agent.Position = Vector3f( at.x, bGround ? ground : player.y, at.z );
            agent.LastY = agent.Position.y;
            agent.Yaw = yaw;
            agent.Speed = 0.0f;
            agent.Stun = 0.0f;
            agent.bVisible = true;
            agent.World = Matrix4x4f::Translation( agent.Position ) *
                          Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw ) );
            static bool bParkLogged = false;
            if ( i == 0 && !bParkLogged ) {
                bParkLogged = true;
                OTDU_LOG_INFO( "Traffic: parked agent 0 type %d (id %d) at y %.3f (player y %.3f, lift %.3f)\n", agent.Type,
                               types[agent.Type].Id, agent.Position.y, player.y, types[agent.Type].GroundLift );
            }
        }
        return;
    }
    spawnTimer += deltaTime;
    clock += deltaTime;
    // The road graph component (island) the player is on: cars only spawn and
    // survive there (2 Hz look-up, the graph has <1000 nodes).
    componentTimer -= deltaTime;
    if ( componentTimer <= 0.0f ) {
        componentTimer = 0.5f;
        updatePlayerComponent( player );
    }
    static float logTimer = 0.0f;
    logTimer += deltaTime;
    if ( logTimer > 3.0f ) {
        logTimer = 0.0f;
        int32_t stopped = 0, indicating = 0, turning = 0, atRed = 0, visible = 0;
        float worstYawRate = 0.0f, worstLaneError = 0.0f;
        for ( Agent &agent : agents ) {
            stopped += agent.Speed < 0.5f ? 1 : 0;
            indicating += agent.IndicatorSide != 0 ? 1 : 0;
            turning += agent.NextEdge >= 0 && agent.NextTurnAngle > 0.22f ? 1 : 0;
            visible += agent.bVisible ? 1 : 0;
            worstYawRate = Max( worstYawRate, std::fabs( agent.YawRate ) );
            // How far the car sits from its lane: the steering cuts a corner
            // by a metre or two, more is a wall (a tuning metric).
            Vector3f lanePoint;
            float laneYaw = 0.0f;
            pathAhead( agent, 0.0f, lanePoint, laneYaw );
            const float laneDx = agent.Position.x - lanePoint.x, laneDz = agent.Position.z - lanePoint.z;
            worstLaneError = Max( worstLaneError, std::sqrt( laneDx * laneDx + laneDz * laneDz ) );
            const int32_t endNode = agent.bForward ? edges[agent.Edge].second : edges[agent.Edge].first;
            const float remaining = shapes[agent.Edge].Length - agent.Distance;
            if ( remaining > 0.5f && remaining < 30.0f && isRed( endNode, agent.Edge ) ) {
                atRed++;
            }
        }
        OTDU_LOG_INFO( "Traffic: %zu cars (%d drawn), %d stopped, %d indicating, %d turning, %d at red, turn %.2f "
                       "rad/s, lane %.2f m, %d contacts, %d removed (despawn %d, no ground %d, off road %d), target "
                       "%.1f\n",
                       agents.size(), visible, stopped, indicating, turning, atRed, worstYawRate, worstLaneError,
                       contactCount, removedDespawn + removedGround + removedComponent, removedDespawn, removedGround,
                       removedComponent, targetCount() );
        contactCount = removedDespawn = removedGround = removedComponent = 0;
    }
    if ( spawnTimer > 0.15f ) {
        spawnTimer = 0.0f;
        // Fill the target quickly at load / after a long drive: up to three
        // cars per spawn step (the ring is wide, the checks are cheap).
        for ( int32_t spawned = 0; spawned < 3 && static_cast<float>( agents.size() ) < targetCount(); spawned++ ) {
            if ( !spawn( player, surface ) ) {
                break;
            }
        }
    }
    for ( size_t i = 0; i < agents.size(); ) {
        Agent &agent = agents[i];
        const Type &agentType = types[static_cast<size_t>( agent.Type )];
        const float dx = agent.Position.x - player.x, dz = agent.Position.z - player.z;
        const float playerDistance = std::sqrt( dx * dx + dz * dz );
        float limit = agent.Stun > 0.0f ? 0.0f : agent.CruiseSpeed;
        agent.Stun = Max( 0.0f, agent.Stun - deltaTime );
        agent.NearMissCooldown = Max( 0.0f, agent.NearMissCooldown - deltaTime );
        // The steering target: a point of the right-hand lane ahead on the
        // path, and the direction the car actually drives (agent.Yaw).
        const float look = Clamp( kLookAheadBase + kLookAheadPerSpeed * agent.Speed, kLookAheadMin, kLookAheadMax );
        Vector3f target;
        float targetYaw = 0.0f;
        pathAhead( agent, look, target, targetYaw );
        const float fx = std::sin( agent.Yaw ), fz = std::cos( agent.Yaw );
        // Brake for a car ahead in this lane, for the player and for the chase's
        // cop cars. The gap is measured bumper to bumper (a bus ahead needs
        // much more room than a hatchback: the old fixed 6 m put the follower
        // inside the bus / the van).
        auto brakeFor = [&]( const Vector3f &other, float otherLength, float sideLimit ) {
            const float rx = other.x - agent.Position.x, rz = other.z - agent.Position.z;
            const float ahead = rx * fx + rz * fz;
            const float side = std::fabs( rx * fz - rz * fx );
            const float reach = 14.0f + 0.5f * ( agentType.Length + otherLength );
            if ( ahead > 0.0f && ahead < reach && side < sideLimit ) {
                const float gap = ahead - 0.5f * agentType.Length - 0.5f * otherLength - 2.0f;
                limit = Min( limit, Max( 0.0f, gap * 1.1f ) );
            }
        };
        for ( size_t j = 0; j < agents.size(); j++ ) {
            if ( j != i ) {
                const Agent &other = agents[j];
                // 3.2 m of lateral reach covers the cars in the same lane that
                // are swinging through a corner; the oncoming lane sits 3.8 m
                // away, so it is still not braked for.
                brakeFor( other.Position, types[static_cast<size_t>( other.Type )].Length, 3.2f );
            }
        }
        brakeFor( player, 4.5f, 2.8f );
        // The chase's cop cars share the road (set by the game mode this frame).
        for ( const Vector3f &other : extraVehicles ) {
            brakeFor( other, 4.5f, 3.2f );
        }

        // The link chosen at the node ahead: decided early enough to set the
        // turn speed and to blink the indicator in time.
        const float length = shapes[agent.Edge].Length;
        const float remaining = length - agent.Distance;
        if ( agent.NextEdge < 0 && remaining < 40.0f ) {
            chooseNextEdge( agent );
        }
        if ( agent.IndicatorSide != 0 ) {
            agent.IndicatorPhase += deltaTime;
            if ( agent.NextEdge < 0 ) {
                // Past the node: the blink stays on a moment after the turn.
                agent.IndicatorHold -= deltaTime;
                if ( agent.IndicatorHold <= 0.0f ) {
                    agent.IndicatorSide = 0;
                }
            }
        }
        // Red signal at the end of the link: the stop line sits 5.5 m before
        // the node, i.e. before the junction box, so the crossing traffic (the
        // green link) drives through without hitting the waiting car.
        const int32_t endNode = agent.bForward ? edges[agent.Edge].second : edges[agent.Edge].first;
        if ( remaining > 0.5f && isRed( endNode, agent.Edge ) ) {
            limit = Min( limit, std::sqrt( Max( 0.0f, 2.0f * 6.0f * ( remaining - kRedStopLine ) ) ) );
        }
        // Yield to a traffic car already inside the junction box ahead (cars on
        // the crossing link do not see each other as "ahead"). A car that is
        // itself stuck there does not hold the others: the circle separation
        // below keeps them apart, so there is no deadlock.
        if ( agent.NextEdge >= 0 && remaining > kJunctionStopLine && remaining < kJunctionApproach ) {
            const Vector3f &node = nodes[static_cast<size_t>( endNode )];
            for ( size_t j = 0; j < agents.size(); j++ ) {
                if ( j == i ) {
                    continue;
                }
                const Agent &other = agents[j];
                if ( other.Speed < 0.6f ) {
                    continue;
                }
                const float ox = other.Position.x - node.x, oz = other.Position.z - node.z;
                if ( ox * ox + oz * oz < kJunctionBox * kJunctionBox ) {
                    limit = Min( limit, std::sqrt( Max( 0.0f, 2.0f * 4.0f * ( remaining - kJunctionStopLine ) ) ) );
                    break;
                }
            }
        }
        // A real turn is approached at a lower speed (feed forward); the
        // steering below limits it by the lateral acceleration as well, which
        // is what makes a 90 degree corner an arc instead of a heading snap.
        if ( agent.NextEdge >= 0 && remaining < 45.0f && agent.NextTurnAngle > 0.22f ) {
            const float turnSpeed = Clamp( 14.0f - 7.0f * ( agent.NextTurnAngle / 1.570796f ), 5.5f, 14.0f );
            const float brakeDistance = agent.Speed * agent.Speed / ( 2.0f * 6.0f ) + 4.0f;
            if ( remaining < brakeDistance ) {
                limit = Min( limit, turnSpeed );
            }
        }
        // Pure pursuit: the yaw error to the target point gives the curvature,
        // and the curvature the fastest speed the tires can hold through it.
        float yawRate = 0.0f;
        const float tx = target.x - agent.Position.x, tz = target.z - agent.Position.z;
        const float targetDistance = std::sqrt( tx * tx + tz * tz );
        if ( targetDistance > 0.25f && agent.Speed > 0.05f ) {
            const float error = WrapPi( std::atan2( tx, tz ) - agent.Yaw );
            const float curvature = std::fabs( error ) / targetDistance;
            limit = Min( limit, std::sqrt( kMaxLateralAccel / Max( curvature, 1e-4f ) ) );
            const float maxRate = Clamp( kMaxLateralAccel / Max( agent.Speed, 3.0f ), 0.4f, 2.6f );
            yawRate = Clamp( error * 2.5f, -maxRate, maxRate );
        }
        agent.YawRate = yawRate;
        const float accel = agent.Speed < limit ? 3.5f : 9.0f;
        agent.Speed += Clamp( limit - agent.Speed, -accel * deltaTime, accel * deltaTime );
        // The brake lamps: a decel this frame (limit below the speed) holds
        // the lamps 0.4 s so they do not flicker at the throttle bumps.
        agent.BrakeHold = bForceBrake || limit + 0.3f < agent.Speed ? 0.4f : Max( 0.0f, agent.BrakeHold - deltaTime );
        // Drive along the car's own heading, turning it at the steering rate.
        agent.Yaw = WrapPi( agent.Yaw + yawRate * deltaTime );
        const float travelled = agent.Speed * deltaTime;
        agent.Position.x += std::sin( agent.Yaw ) * travelled;
        agent.Position.z += std::cos( agent.Yaw ) * travelled;
        agent.WheelSpin += travelled / Max( agentType.WheelRadius, 0.1f );
        agent.Distance += travelled;
        // Horn: a car held up by the player (close, ahead of it, the player
        // barely moving) honks every few seconds. The retail trigger is [ENC];
        // this is the "get out of the way" case that reads on screen [INF].
        agent.HornCooldown -= deltaTime;
        if ( agent.HornCooldown <= 0.0f ) {
            const float hx = player.x - agent.Position.x;
            const float hz = player.z - agent.Position.z;
            const float distance = std::sqrt( hx * hx + hz * hz );
            const float ahead = hx * std::sin( agent.Yaw ) + hz * std::cos( agent.Yaw );
            if ( distance < 16.0f && ahead > -2.0f && agent.Speed < 4.0f && hornEvents.size() < 8 ) {
                HornEvent event;
                event.Position = agent.Position;
                event.Volume = Clamp( 1.15f - distance / 22.0f, 0.45f, 1.0f );
                hornEvents.push_back( event );
                agent.HornCooldown = std::uniform_real_distribution<float>( 6.0f, 12.0f )( random );
            } else {
                agent.HornCooldown = 0.5f; // re-test soon
            }
        }
        if ( agent.Distance >= length ) {
            // At the node: take the link chosen on the approach. The dead-end
            // case is the same link reversed (bNextForward was flipped).
            if ( agent.NextEdge < 0 ) {
                chooseNextEdge( agent ); // a very short link: decided just in time
            }
            const float over = agent.Distance - length;
            agent.Edge = agent.NextEdge;
            agent.bForward = agent.bNextForward;
            agent.NextEdge = -1;
            agent.Distance = Min( over, Max( shapes[static_cast<size_t>( agent.Edge )].Length - 0.01f, 0.0f ) );
            agent.IndicatorHold = 1.2f; // keep the blinker on past the turn
        }
        // Keep the progress at the point of the path closest to the car: a
        // corner is cut by a metre or two, and without this the target would
        // walk away from the car and the steering would oscillate.
        {
            Vector3f pathPoint;
            float pathYaw = 0.0f;
            pathAhead( agent, 0.0f, pathPoint, pathYaw );
            const float along = ( agent.Position.x - pathPoint.x ) * std::sin( pathYaw ) +
                                ( agent.Position.z - pathPoint.z ) * std::cos( pathYaw );
            agent.Distance = Clamp( agent.Distance + Clamp( along * 0.5f, -2.0f, 2.0f ), 0.0f,
                                    Max( shapes[static_cast<size_t>( agent.Edge )].Length - 0.01f, 0.0f ) );
        }
        // Ground under the car; lose it for 2 s -> remove.
        float ground = 0.0f;
        const bool bGround = surface( agent.Position.x, agent.Position.z, agent.LastY + 6.0f, ground );
        if ( bGround ) {
            agent.LastY = ground;
            agent.LostGround = 0.0f;
        } else {
            agent.LostGround += deltaTime;
        }
        agent.Position.y = agent.LastY;
        agent.bVisible = playerDistance < viewRadius;
        // Removed only beyond the despawn radius, and never while it is drawn
        // (the old 420 m cull pulled cars out of the world in plain sight),
        // nor off the road graph left to the player.
        const bool bOffComponent =
            playerComponent >= 0 && component.size() == nodes.size() &&
            ( static_cast<size_t>( edges[agent.Edge].first ) >= component.size() ||
              static_cast<size_t>( edges[agent.Edge].second ) >= component.size() ||
              component[static_cast<size_t>( edges[agent.Edge].first )] != playerComponent ||
              component[static_cast<size_t>( edges[agent.Edge].second )] != playerComponent );
        if ( agent.LostGround > 2.0f || bOffComponent || ( !agent.bVisible && playerDistance > despawnRadius ) ) {
            removedGround += agent.LostGround > 2.0f ? 1 : 0;
            removedComponent += bOffComponent ? 1 : 0;
            removedDespawn += !agent.bVisible && playerDistance > despawnRadius ? 1 : 0;
            agents.erase( agents.begin() + static_cast<std::ptrdiff_t>( i ) );
            continue;
        }
        if ( agent.bVisible ) {
            // The world matrix: the full four-sample orientation near the
            // player (pitch and roll on a slope), the heading only beyond
            // 180 m - a far car is a few pixels and the four extra ground
            // raycasts are wasted there.
            if ( playerDistance >= 180.0f ) {
                agent.World = Matrix4x4f::Translation( agent.Position ) *
                              Matrix4x4f::FromQuaternion(
                                  Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), agent.Yaw ) );
                i++;
                continue;
            }
            // Orientation from four ground samples (front / rear / right / left).
            const float half = 0.35f * agentType.Length;
            const float hfx = std::sin( agent.Yaw ), hfz = std::cos( agent.Yaw );
            const float rx = hfz, rz = -hfx;
            auto sample = [&]( float x, float z, float fallback ) {
                float height = 0.0f;
                return surface( x, z, agent.Position.y + 3.0f, height ) ? height : fallback;
            };
            const float y = agent.Position.y;
            const Vector3f front( agent.Position.x + hfx * half, sample( agent.Position.x + hfx * half, agent.Position.z + hfz * half, y ), agent.Position.z + hfz * half );
            const Vector3f rear( agent.Position.x - hfx * half, sample( agent.Position.x - hfx * half, agent.Position.z - hfz * half, y ), agent.Position.z - hfz * half );
            const Vector3f right( agent.Position.x + rx * 0.8f, sample( agent.Position.x + rx * 0.8f, agent.Position.z + rz * 0.8f, y ), agent.Position.z + rz * 0.8f );
            const Vector3f left( agent.Position.x - rx * 0.8f, sample( agent.Position.x - rx * 0.8f, agent.Position.z - rz * 0.8f, y ), agent.Position.z - rz * 0.8f );
            Vector3f forwardVector = front - rear;
            const Vector3f rightVector = right - left;
            Vector3f up = forwardVector.cross( rightVector );
            if ( up.length() < 0.0001f || up.y <= 0.0f ) {
                up = Vector3f( 0.0f, 1.0f, 0.0f );
            }
            up = up.normalized();
            forwardVector = forwardVector.normalized();
            const Vector3f rightAxis = up.cross( forwardVector ).normalized();
            Matrix4x4f world = Matrix4x4f::Identity();
            world.m[0][0] = rightAxis.x; world.m[0][1] = rightAxis.y; world.m[0][2] = rightAxis.z;
            world.m[1][0] = up.x;        world.m[1][1] = up.y;        world.m[1][2] = up.z;
            world.m[2][0] = forwardVector.x; world.m[2][1] = forwardVector.y; world.m[2][2] = forwardVector.z;
            world.m[3][0] = agent.Position.x;
            world.m[3][1] = 0.5f * ( front.y + rear.y );
            world.m[3][2] = agent.Position.z;
            agent.World = world;
        }
        i++;
    }
    // The traffic cars keep each other apart (no clipping through one another).
    resolveAgentCollisions();
    // -trafficcrashtest: drives the second car 1.5 m into the first at 8 m/s
    // and logs the centre distance / speeds the separation pass reaches after
    // a second (log only; the reference for "no clipping").
    static const bool bCrashTest = CmdLine::HasArg( "-trafficcrashtest" );
    if ( bCrashTest ) {
        static float crashTimer = -1.0f;
        if ( crashTimer < -0.5f ) {
            if ( agents.size() >= 2 ) {
                Agent &a = agents[0];
                Agent &b = agents[1];
                const float afx = std::sin( a.Yaw ), afz = std::cos( a.Yaw );
                b.Yaw = a.Yaw + kPi; // head on
                b.Position = a.Position + Vector3f( afx, 0.0f, afz ) * 1.5f;
                a.Speed = b.Speed = 8.0f;
                crashTimer = 0.0f;
                OTDU_LOG_INFO( "Traffic: crashtest start, %.2f m apart\n", 1.5f );
            }
        } else {
            crashTimer += deltaTime;
            if ( crashTimer > 1.0f ) {
                crashTimer = -1.0f;
                const float dx = agents[0].Position.x - agents[1].Position.x;
                const float dz = agents[0].Position.z - agents[1].Position.z;
                OTDU_LOG_INFO( "Traffic: crashtest after 1 s: %.2f m apart, speeds %.2f / %.2f m/s\n", std::sqrt( dx * dx + dz * dz ),
                               agents[0].Speed, agents[1].Speed );
            }
        }
    }
    extraVehicles.clear();
}

void Traffic::resolveAgentCollisions() {
    // Circle separation between the traffic cars (the same circle model the
    // player collision uses: one circle per 1.5 m of length, radius half the
    // width). They used to drive through one another at a junction and when a
    // car was rammed into a queue; now the overlap is pushed out along the
    // contact normal, split by mass, and the closing speed is absorbed.
    constexpr float kRestitution = 0.15f;
    for ( size_t i = 0; i < agents.size(); i++ ) {
        Agent &a = agents[i];
        const Type &typeA = types[static_cast<size_t>( a.Type )];
        const float radiusA = 0.5f * Clamp( 1.7f + 0.08f * ( typeA.Length - 4.0f ), 1.6f, 2.5f );
        const int32_t circlesA = Max( 2, static_cast<int32_t>( std::ceil( typeA.Length / 1.5f ) ) );
        const float afx = std::sin( a.Yaw ), afz = std::cos( a.Yaw );
        for ( size_t j = i + 1; j < agents.size(); j++ ) {
            Agent &b = agents[j];
            const Type &typeB = types[static_cast<size_t>( b.Type )];
            const float dx = b.Position.x - a.Position.x, dz = b.Position.z - a.Position.z;
            const float reach = 0.5f * ( typeA.Length + typeB.Length ) + 2.0f;
            if ( dx * dx + dz * dz > reach * reach || std::fabs( b.Position.y - a.Position.y ) > 2.5f ) {
                continue;
            }
            const float radiusB = 0.5f * Clamp( 1.7f + 0.08f * ( typeB.Length - 4.0f ), 1.6f, 2.5f );
            const int32_t circlesB = Max( 2, static_cast<int32_t>( std::ceil( typeB.Length / 1.5f ) ) );
            const float bfx = std::sin( b.Yaw ), bfz = std::cos( b.Yaw );
            float deepest = 0.0f, nx = 0.0f, nz = 0.0f;
            for ( int32_t ca = 0; ca < circlesA; ca++ ) {
                const float alongA =
                    -( 0.5f * typeA.Length - radiusA ) + ( typeA.Length - 2.0f * radiusA ) * ca / ( circlesA - 1 );
                const float ax = a.Position.x + afx * alongA, az = a.Position.z + afz * alongA;
                for ( int32_t cb = 0; cb < circlesB; cb++ ) {
                    const float alongB =
                        -( 0.5f * typeB.Length - radiusB ) + ( typeB.Length - 2.0f * radiusB ) * cb / ( circlesB - 1 );
                    const float bx = b.Position.x + bfx * alongB, bz = b.Position.z + bfz * alongB;
                    const float ox = bx - ax, oz = bz - az;
                    const float distance = std::sqrt( ox * ox + oz * oz );
                    const float penetration = radiusA + radiusB - distance;
                    if ( penetration > deepest && distance > 0.001f ) {
                        deepest = penetration;
                        nx = ox / distance;
                        nz = oz / distance;
                    }
                }
            }
            if ( deepest <= 0.0f ) {
                continue;
            }
            contactCount++;
            // -logtrafficcontacts: what overlaps (at most one line per pair per
            // second, deep contacts only) - the debug view of a jam.
            static const bool bLogContacts = CmdLine::HasArg( "-logtrafficcontacts" );
            if ( bLogContacts && deepest > 0.25f ) {
                static std::set<std::pair<int32_t, int32_t>> loggedPairs;
                if ( loggedPairs.insert( std::pair<int32_t, int32_t>( static_cast<int32_t>( i ), static_cast<int32_t>( j ) ) )
                         .second ) {
                    OTDU_LOG_INFO( "Traffic: contact %zu/%zu types %d/%d at (%.1f, %.1f) / (%.1f, %.1f) yaws %.2f/%.2f "
                                   "speeds %.1f/%.1f overlap %.2f\n",
                                   i, j, a.Type, b.Type, a.Position.x, a.Position.z, b.Position.x, b.Position.z, a.Yaw,
                                   b.Yaw, a.Speed, b.Speed, deepest );
                }
                if ( loggedPairs.size() > 32 ) {
                    loggedPairs.clear();
                }
            }

            const float massA = 350.0f * typeA.Length;
            const float massB = 350.0f * typeB.Length;
            const float mass = massA + massB;
            const float push = Min( deepest, 1.5f );
            a.Position.x -= nx * push * ( massB / mass );
            a.Position.z -= nz * push * ( massB / mass );
            b.Position.x += nx * push * ( massA / mass );
            b.Position.z += nz * push * ( massA / mass );
            const float relative = ( bfx * b.Speed - afx * a.Speed ) * nx + ( bfz * b.Speed - afz * a.Speed ) * nz;
            if ( relative < 0.0f ) {
                const float impulse = -( 1.0f + kRestitution ) * relative / ( 1.0f / massA + 1.0f / massB );
                a.Speed = Max( 0.0f, a.Speed - ( afx * nx + afz * nz ) * impulse / massA );
                b.Speed = Max( 0.0f, b.Speed + ( bfx * nx + bfz * nz ) * impulse / massB );
                // Both brake: the bump is visible through the stop lamps.
                a.BrakeHold = 0.6f;
                b.BrakeHold = 0.6f;
            }
        }
    }
}

float Traffic::nearestPoliceDistance( const Vector3f &position ) const {
    float nearest = -1.0f;
    if ( !bLoaded || policeCarId == 0 ) {
        return nearest;
    }
    for ( const Agent &agent : agents ) {
        if ( types[static_cast<size_t>( agent.Type )].Id != policeCarId ) {
            continue;
        }
        const float dx = agent.Position.x - position.x, dz = agent.Position.z - position.z;
        const float distance = std::sqrt( dx * dx + dz * dz );
        if ( nearest < 0.0f || distance < nearest ) {
            nearest = distance;
        }
    }
    return nearest;
}

void Traffic::neutraliseGlassArt() {
    // The traffic glass materials (Glass_F / Glass_ML / Glass_MR / Glass_R ...)
    // carry common_car's `glass_b` in their DETAIL2 slot: the retail *damaged
    // glass* art, a starred pane (decoded: a grey map with a bullet-hole star
    // and scratches; 256x128 DXT5, mean (76, 92, 101)). The CarPaint glass
    // shader lerps the base toward DETAIL2 by its alpha, so every traffic car
    // drew that cracked pane - the owner's "they all seem to have cracked like
    // windows". The player car's glass uses `g_i` (32x32 opaque black) there,
    // the clean glass base, and the traffic material has no damage state we
    // could drive [INF: the retail damage / dirt drive is in the encrypted
    // code]. g_i is bound instead, which gives the clean dark retail glass;
    // -trafficglassart keeps the authored cracked art for an A/B shot.
    static const bool bAuthored = CmdLine::HasArg( "-trafficglassart" );
    if ( bAuthored || gpRender == nullptr ) {
        return;
    }
    static const uint64_t kGlassBase = GetIdentifier64bit( "G_I" );
    static const uint64_t kDetail2 = GetIdentifier64bit( "DETAIL2" );
    static const uint64_t kCarPaint = 0x183157b81c6c7357ull;
    GPUTexture *pBlack = gpRender->getTextureCache().find( "g_i" );
    if ( pBlack == nullptr ) {
        return; // common_car.bnk is not loaded yet (retried from assignEnvironment)
    }
    uint32_t fixed = 0;
    for ( Type &type : types ) {
        if ( !type.Model ) {
            continue;
        }
        for ( EdenMaterial &material : type.Model->getMaterials() ) {
            if ( material.ShaderSetGUID != kCarPaint || material.Name.compare( 0, 5, "Glass" ) != 0 ) {
                continue;
            }
            for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
                if ( material.SlotTypes[i] == kDetail2 && material.TextureHashes[i] != kGlassBase ) {
                    material.pTextures[i] = pBlack;
                    material.TextureHashes[i] = kGlassBase;
                    fixed++;
                }
            }
        }
    }
    if ( fixed != 0 ) {
        OTDU_LOG_INFO( "Traffic: %u glass slot(s): the damaged-glass art of DETAIL2 replaced by the black glass base G_I\n",
                       fixed );
    }
}

void Traffic::assignEnvironment( EdenRenderer &, GPUTexture *pCube ) {
    if ( pCube == nullptr ) {
        return;
    }
    // common_car.bnk (the black G_I glass base) may only be loaded once the
    // player car is: retry the glass fix here.
    neutraliseGlassArt();

    // The dynamic environment cube of the view into the REFLECTION slot of the
    // car materials (CinematicCar::assignEnvironment = FUN_00d58a90). The
    // traffic models were drawn with the authored static cube only: their
    // glass showed the low resolution window art plus a mottled specular term,
    // which reads as cracked / soiled windows.
    static const uint64_t kDefaultCube = GetIdentifier64bit( "CUBEMAP" );
    static const uint64_t kSpecParm = GetIdentifier64bit( "SPECPARM" );
    static const uint64_t kRampPow = GetIdentifier64bit( "RAMPPOW" );
    uint32_t assigned = 0;
    for ( Type &type : types ) {
        if ( !type.Model ) {
            continue;
        }
        for ( EdenMaterial &material : type.Model->getMaterials() ) {
            const uint64_t family = material.ShaderSetGUID;
            const bool bCarFamily = family == 0x183157b81c6c7357ull || family == 0xca00000000000010ull ||
                                    family == 0x183157b81c6c5377ull || family == 0xdddddddd1c6c7357ull ||
                                    family == 0xb39c35015789e65cull || family == 0xca00000000000020ull ||
                                    family == 0x6638348331d66466ull || family == 0x1b38348331cacacaull ||
                                    family == 0x08041973fadeffffull;
            if ( !bCarFamily ) {
                continue;
            }
            bool bChanged = false;
            for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
                if ( material.TextureHashes[i] == kDefaultCube && material.pTextures[i] != pCube ) {
                    material.pTextures[i] = pCube;
                    bChanged = true;
                }
            }
            if ( !bChanged || material.Spec[3] == 0 ) {
                continue;
            }
            // S_USEREFLECTION: the cube alpha attenuation is off for the
            // dynamic cube (SPECPARM.z of TD_CarPaint, RAMPPOW.z of CarPaint2).
            const uint64_t parameter = family == 0x183157b81c6c5377ull || family == 0x183157b81c6ccacaull ? kRampPow
                                                                                                          : kSpecParm;
            auto it = material.Parameters.find( parameter );
            if ( it != material.Parameters.end() ) {
                it->second.z = 0.0f;
            }
            assigned++;
        }
    }
    bEnvironmentAssigned = true;
    OTDU_LOG_INFO( "Traffic: dynamic environment cube bound to %u car material(s)\n", assigned );
}

void Traffic::draw( EdenRenderer &renderer, const WeatherPreset *pWeather ) {
    if ( !bLoaded ) {
        return;
    }
    // The lamp sprites follow the clock: the weather names are times
    // ("14", "23.00"), the same gate the density mix uses at load. Front
    // lights (L) + rear position (B) at night; stop (S) for a braking agent,
    // day or night; reverse (R) never; the warning sprites (W) blink the side
    // of the link the car takes at the next node. The FA_ nodes stay visible
    // in the shared model; the per agent hidden list (EdenDrawParams) hides
    // the functions the agent's state does not show.
    static const bool bNoTrafficLights = CmdLine::HasArg( "-notrafficlights" );
    const bool bNight = pWeather != nullptr && ( pWeather->Hour < 6 || pWeather->Hour >= 20 );
    // The car lighting factors of the weather in ENVCOEFF (x reflection,
    // y specular, z / w fresnel), exactly what CinematicCar writes for the
    // player car: the traffic materials were drawn with their authored values,
    // which left the glass reflection off. Static cube unless the view's
    // dynamic cube was bound (assignEnvironment).
    {
        const Vector4f fresnel = pWeather != nullptr
                                     ? pWeather->getVector4( "car_fresnel_coeff_power_boost_specular",
                                                             Vector4f( 1.0f, 1.0f, 1.0f, 1.0f ) )
                                     : Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
        auto factor = [pWeather]( const char *pName, float defaultValue ) {
            return pWeather != nullptr ? pWeather->getFloat( pName, defaultValue ) : defaultValue;
        };
        const float specular = fresnel.w * factor( "car_sun_specular_factor", 1.0f );
        const float staticFactor = bEnvironmentAssigned ? 1.0f : factor( "car_envmap_factor_static_cubemap", 0.7f );
        const Vector4f envCoeff( factor( "car_envmap_factor", 1.0f ) * staticFactor, specular, fresnel.x, fresnel.y );
        static const uint64_t kEnvCoeff = GetIdentifier64bit( "ENVCOEFF" );
        for ( Type &type : types ) {
            if ( !type.Model ) {
                continue;
            }
            for ( EdenMaterial &material : type.Model->getMaterials() ) {
                auto it = material.Parameters.find( kEnvCoeff );
                if ( it != material.Parameters.end() ) {
                    it->second = envCoeff;
                }
            }
        }
    }

    // The police roof bars (the lens material is shared by the type, so this
    // is written once per draw, not per agent): GLOSSFCT lights the swept
    // ring of the dark gyro_of lens on a chase / with -policebar.
    // -policefullbar binds the lit gyro_on art instead (a bright test shot);
    // the accurate state is the ring on the off art.
    static const bool bFullBar = CmdLine::HasArg( "-policefullbar" );
    for ( const Type &type : types ) {
        // Every type with a bar (the fire van has one too): only the police
        // car sweeps, the others stay dark.
        if ( !type.Model || !type.PoliceBar.isPoliceBar() ) {
            continue;
        }
        if ( bPoliceLights && type.bPolice ) {
            type.PoliceBar.applyLens( renderer, *type.Model, bFullBar );
            type.PoliceBar.apply( *type.Model, gyroClock );
        } else {
            type.PoliceBar.applyLens( renderer, *type.Model, false );
            type.PoliceBar.applyOff( *type.Model );
        }
    }
    EdenDrawParams params;
    for ( Agent &agent : agents ) {
        if ( !agent.bVisible ) {
            continue;
        }
        params.ForceHiddenNodes.clear();
        const Type &type = types[agent.Type];
        const bool bBraking = agent.BrakeHold > 0.0f;
        if ( type.Rival ) {
            // A rival: the real car on its four tires.
            if ( surfaceFn && type.Rival->isLoaded() ) {
                type.Rival->draw( renderer, type.Rival->pose( agent.Position, agent.Yaw, surfaceFn ), pWeather );
            }
            continue;
        }
        // L (front), B (rear position), S (stop), R (reverse); the warning
        // sprites are split by side below.
        for ( int32_t function = 0; function < 4; function++ ) {
            const bool bShow = !bNoTrafficLights &&
                               ( function == 0 || function == 1 ? bNight : ( function == 2 && bBraking ) );
            if ( !bShow ) {
                params.ForceHiddenNodes.insert( params.ForceHiddenNodes.end(), type.LampNodes[function].begin(),
                                                type.LampNodes[function].end() );
            }
        }
        // The turn indicator: the side of the chosen link, at the retail
        // cadence (period 1.0 s, lit while t < 0.6; car_lights_spec 2.3).
        const bool bWarn = !bNoTrafficLights && agent.IndicatorSide != 0 &&
                           std::fmod( agent.IndicatorPhase, 1.0f ) < 0.6f;
        const std::vector<int32_t> &lit =
            type.IndicatorNodes[agent.IndicatorSide < 0 ? 0 : 1];
        for ( const int32_t node : type.LampNodes[4] ) {
            const bool bShow = bWarn && std::find( lit.begin(), lit.end(), node ) != lit.end();
            if ( !bShow ) {
                params.ForceHiddenNodes.push_back( node );
            }
        }
        // The roof light bar quads: the retail billboards are misplaced by the
        // port's sprite shader (they float off the roof), so they are hidden
        // by default - the lens art + GLOSSFCT sweep is the visible bar.
        // -gyrosprites shows the swept ring for debugging.
        static const bool bShowGyroSprites = CmdLine::HasArg( "-gyrosprites" );
        if ( type.PoliceBar.isPoliceBar() ) {
            if ( bPoliceLights && type.bPolice && bShowGyroSprites ) {
                type.PoliceBar.hideInactive( params, gyroClock );
            } else {
                type.PoliceBar.hideAll( params );
            }
        }
        Matrix4x4f world = agent.World;
        world.m[3][1] += type.GroundLift;
        // Rolling wheels: spin the type's wheel nodes about the axle through
        // their own hub (the model is shared, so the spin is written before
        // each agent's draw). -trafficwheel <deg> pins an angle for a test
        // shot of a parked car.
        static const int32_t forcedWheelAngle = CmdLine::GetArgInt( "-trafficwheel", -1 );
        for ( int32_t w = 0; w < 4; w++ ) {
            const int32_t node = type.WheelNodes[w];
            if ( node < 0 || static_cast<size_t>( node ) >= type.Model->getScene().getNodes().size() ) {
                continue;
            }
            const Matrix4x4f &base = type.WheelBase[w];
            const Vector3f hub( base.m[3][0], base.m[3][1], base.m[3][2] );
            const float angle = forcedWheelAngle >= 0 ? static_cast<float>( forcedWheelAngle ) * kDegToRad
                                                      : std::fmod( agent.WheelSpin, 2.0f * kPi );
            const Matrix4x4f spin = Matrix4x4f::Translation( hub ) *
                                    Matrix4x4f::FromQuaternion(
                                        Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), angle ) ) *
                                    Matrix4x4f::Translation( Vector3f( -hub.x, -hub.y, -hub.z ) );
            type.Model->setNodeTransform( node, spin * base );
        }
        type.Model->draw( renderer, world, &params );
        // Restore the authored node transforms (the parked car of the debug
        // shot and the other agents of the type draw from the same model).
        for ( int32_t w = 0; w < 4; w++ ) {
            if ( type.WheelNodes[w] >= 0 ) {
                type.Model->setNodeTransform( type.WheelNodes[w], type.WheelBase[w] );
            }
        }
    }
}

bool Traffic::resolveCollision( const Vector3f &position, const Vector3f &forward, const Vector3f &velocity, float width,
                                float length, float mass, Vector3f &outDeltaVelocity ) {
    outDeltaVelocity = Vector3f();
    if ( !bLoaded ) {
        return false;
    }
    const float playerRadius = 0.5f * width;
    const float playerOffsets[3] = { -( 0.5f * length - playerRadius ), 0.0f, 0.5f * length - playerRadius };
    bool bHit = false;
    for ( Agent &agent : agents ) {
        const float dxa = agent.Position.x - position.x, dza = agent.Position.z - position.z;
        const Type &type = types[agent.Type];
        if ( dxa * dxa + dza * dza > ( 8.0f + type.Length ) * ( 8.0f + type.Length ) || std::fabs( agent.Position.y - position.y ) > 2.5f ) {
            continue;
        }
        const float agentWidth = Clamp( 1.7f + 0.08f * ( type.Length - 4.0f ), 1.6f, 2.5f );
        const float agentRadius = 0.5f * agentWidth;
        const float afx = std::sin( agent.Yaw ), afz = std::cos( agent.Yaw );
        const int32_t circles = Max( 2, static_cast<int32_t>( std::ceil( type.Length / 1.5f ) ) );
        float deepest = 0.0f, nx = 0.0f, nz = 0.0f;
        for ( int32_t i = 0; i < circles; i++ ) {
            const float along = -( 0.5f * type.Length - agentRadius ) + ( type.Length - 2.0f * agentRadius ) * i / ( circles - 1 );
            const float ax = agent.Position.x + afx * along, az = agent.Position.z + afz * along;
            for ( const float offset : playerOffsets ) {
                const float px = position.x + forward.x * offset, pz = position.z + forward.z * offset;
                const float dx = px - ax, dz = pz - az;
                const float distance = std::sqrt( dx * dx + dz * dz );
                const float penetration = agentRadius + playerRadius - distance;
                if ( penetration > deepest && distance > 0.001f ) {
                    deepest = penetration;
                    nx = dx / distance; // from the traffic car to the player
                    nz = dz / distance;
                }
            }
        }
        if ( deepest <= 0.0f ) {
            continue;
        }
        bHit = true;
        const float agentMass = 350.0f * type.Length;
        const float relative = ( velocity.x - afx * agent.Speed ) * nx + ( velocity.z - afz * agent.Speed ) * nz;
        float impulse = 0.0f;
        if ( relative < 0.0f ) {
            constexpr float kRestitution = 0.2f;
            impulse = -( 1.0f + kRestitution ) * relative / ( 1.0f / mass + 1.0f / agentMass );
        }
        outDeltaVelocity.x += nx * ( impulse / mass );
        outDeltaVelocity.z += nz * ( impulse / mass );
        // separation: push the player out of the overlap
        outDeltaVelocity.x += nx * Min( deepest, 1.0f ) * 3.0f;
        outDeltaVelocity.z += nz * Min( deepest, 1.0f ) * 3.0f;
        agent.Speed = Max( 0.0f, agent.Speed + ( -nx * afx - nz * afz ) * impulse / agentMass );
        if ( agent.Stun <= 0.0f ) {
            OTDU_LOG_INFO( "Traffic: contact with %s: closing %.1f m/s, impulse %.0f\n", "a car", -relative, impulse );
        }
        agent.Stun = 2.5f;
    }
    return bHit;
}

void Traffic::selfTest() {
    if ( agents.empty() ) {
        return;
    }
    Agent &agent = agents.front();
    const float fx = std::sin( agent.Yaw ), fz = std::cos( agent.Yaw );
    // The player one metre behind the traffic car's centre, closing at 12 m/s.
    const Vector3f position( agent.Position.x - fx * 3.0f, agent.Position.y, agent.Position.z - fz * 3.0f );
    const Vector3f velocity( fx * ( agent.Speed + 12.0f ), 0.0f, fz * ( agent.Speed + 12.0f ) );
    const float before = agent.Speed;
    Vector3f dv;
    const bool bHit = resolveCollision( position, Vector3f( fx, 0.0f, fz ), velocity, 1.9f, 4.5f, 1500.0f, dv );
    OTDU_LOG_INFO( "Traffic: selftest hit %d, player dv (%.2f, %.2f) m/s, traffic speed %.1f -> %.1f\n", bHit ? 1 : 0, dv.x, dv.z, before, agent.Speed );
}

int32_t Traffic::findNearestAgent( const Vector3f &position, float radius, float &outDistance ) const {
    // The agent's Position is the type model's origin (roughly the car
    // centre): an XZ nearest-within-radius over the live agents.
    outDistance = radius;
    int32_t nearest = -1;
    for ( size_t i = 0; i < agents.size(); i++ ) {
        const Agent &agent = agents[i];
        if ( !agent.bVisible ) {
            continue;
        }
        const float dx = agent.Position.x - position.x;
        const float dz = agent.Position.z - position.z;
        const float d = std::sqrt( dx * dx + dz * dz );
        if ( d < outDistance ) {
            outDistance = d;
            nearest = static_cast<int32_t>( i );
        }
    }
    return nearest;
}

int32_t Traffic::detectNearMisses( const Vector3f &position, const Vector3f &forward, const Vector3f &velocity, float width,
                                   float length, float &outClosing, float &outGap ) {
    outClosing = 0.0f;
    outGap = 0.0f;
    if ( !bLoaded ) {
        return 0;
    }
    const float playerRadius = 0.5f * width;
    const float playerOffsets[3] = { -( 0.5f * length - playerRadius ), 0.0f, 0.5f * length - playerRadius };
    int32_t count = 0;
    for ( Agent &agent : agents ) {
        if ( agent.NearMissCooldown > 0.0f ) {
            continue;
        }
        const float dxa = agent.Position.x - position.x, dza = agent.Position.z - position.z;
        const Type &type = types[agent.Type];
        if ( dxa * dxa + dza * dza > ( 10.0f + type.Length ) * ( 10.0f + type.Length ) || std::fabs( agent.Position.y - position.y ) > 2.5f ) {
            continue;
        }
        const float agentRadius = 0.5f * Clamp( 1.7f + 0.08f * ( type.Length - 4.0f ), 1.6f, 2.5f );
        const float afx = std::sin( agent.Yaw ), afz = std::cos( agent.Yaw );
        const int32_t circles = Max( 2, static_cast<int32_t>( std::ceil( type.Length / 1.5f ) ) );
        float gap = 1e9f;
        for ( int32_t i = 0; i < circles; i++ ) {
            const float along = -( 0.5f * type.Length - agentRadius ) + ( type.Length - 2.0f * agentRadius ) * i / ( circles - 1 );
            const float ax = agent.Position.x + afx * along, az = agent.Position.z + afz * along;
            for ( const float offset : playerOffsets ) {
                const float dx = position.x + forward.x * offset - ax, dz = position.z + forward.z * offset - az;
                gap = Min( gap, std::sqrt( dx * dx + dz * dz ) - agentRadius - playerRadius );
            }
        }
        const float rvx = velocity.x - afx * agent.Speed, rvz = velocity.z - afz * agent.Speed;
        const float closing = std::sqrt( rvx * rvx + rvz * rvz );
        // The near-miss zone: the FRIM ESQUIVE DIST min, 2.3 m (frpoints.cpr) [DATA].
        if ( gap > 0.0f && gap < 2.3f && closing > 8.0f ) {
            outGap = count == 0 ? gap : Min( outGap, gap );
            count++;
            outClosing = closing;
            agent.NearMissCooldown = 4.0f;
        }
    }
    return count;
}

void Traffic::nearMissTest( float deltaTime ) {
    // A virtual player car (1.9 x 4.5 m) passes a live traffic car at 16 m/s
    // faster than it, on lateral offsets of 2.4 m (hugging the lane) and 5.5 m
    // (two lanes away): the first must count as a near miss, the second not
    // (the zone is the FRIM ESQUIVE DIST min, a 2.3 m gap: the next lane at
    // ~2 m counts, as in the data's 70..150 point range).
    // The sweep is anchored to the traffic car (its heading, a straight pass).
    static const float kOffsets[2] = { 2.4f, 5.5f };
    static int32_t pass = 0;
    static bool bActive = false;
    static float along = 0.0f;
    static int32_t detected = 0, contacts = 0;
    static float closingSeen = 0.0f;
    if ( pass >= 2 || agents.size() < 2 ) {
        return;
    }
    Agent &agent = agents[static_cast<size_t>( pass )];
    if ( !bActive ) {
        along = -30.0f;
        detected = contacts = 0;
        closingSeen = 0.0f;
        bActive = true;
        agent.NearMissCooldown = 0.0f;
    }
    const float fx = std::sin( agent.Yaw ), fz = std::cos( agent.Yaw );
    const float relativeSpeed = 16.0f;
    along += relativeSpeed * deltaTime;
    const Vector3f position( agent.Position.x + fx * along - fz * kOffsets[pass], agent.Position.y,
                             agent.Position.z + fz * along + fx * kOffsets[pass] );
    const float speed = agent.Speed + relativeSpeed;
    const Vector3f forward( fx, 0.0f, fz ), velocity( fx * speed, 0.0f, fz * speed );
    float closing = 0.0f, gap = 0.0f;
    const int32_t count = detectNearMisses( position, forward, velocity, 1.9f, 4.5f, closing, gap );
    if ( count > 0 ) {
        detected += count;
        closingSeen = closing;
    }
    Vector3f dv;
    if ( resolveCollision( position, forward, velocity, 1.9f, 4.5f, 1500.0f, dv ) ) {
        contacts++;
    }
    if ( along > 30.0f ) {
        OTDU_LOG_INFO( "Traffic: nearmiss test offset %.1f m: detected %d (closing %.1f m/s), contacts %d\n", kOffsets[pass], detected,
                       closingSeen, contacts );
        bActive = false;
        pass++;
    }
}

// Audio snapshot of the live cars (audio/traffic_audio.h). The velocity is the
// heading at the agent's speed (the Doppler term of the engine loop).
void Traffic::appendAudioAgents( std::vector<TrafficAudio::Agent> &out ) const {
    out.clear();
    out.reserve( agents.size() );
    for ( const Agent &agent : agents ) {
        TrafficAudio::Agent entry;
        entry.Id = agent.AudioId;
        entry.Position = agent.Position;
        entry.Speed = agent.Speed;
        entry.Velocity = Vector3f( std::sin( agent.Yaw ) * agent.Speed, 0.0f, std::cos( agent.Yaw ) * agent.Speed );
        const size_t type = static_cast<size_t>( agent.Type );
        entry.bPolice = type < types.size() && types[type].bPolice;
        out.push_back( entry );
    }
}

void Traffic::drainHornEvents( std::vector<HornEvent> &out ) {
    out.swap( hornEvents );
    hornEvents.clear();
}

int32_t Traffic::countNear( const Vector3f &position, float radius ) const {
    int32_t count = 0;
    const float squared = radius * radius;
    for ( const Agent &agent : agents ) {
        const float dx = agent.Position.x - position.x;
        const float dz = agent.Position.z - position.z;
        if ( dx * dx + dz * dz <= squared ) {
            count++;
        }
    }
    return count;
}


void Traffic::loadRivals() {
    rivalTypes = 0;
    if ( CmdLine::HasArg( "-norivals" ) ) {
        return;
    }
    const std::vector<AiDriver> &drivers = GetStoryDrivers();
    if ( drivers.empty() ) {
        return;
    }
    // A few drivers per session (each is a full car in memory) [INF]: three by
    // default, -rivals <n>; -rival <name> puts that driver first.
    const int32_t count =
        Clamp( CmdLine::HasArg( "-rivals" ) ? CmdLine::GetArgInt( "-rivals", 3 ) : 3, 0, static_cast<int32_t>( drivers.size() ) );
    std::vector<size_t> order( drivers.size() );
    for ( size_t i = 0; i < order.size(); i++ ) {
        order[i] = i;
    }
    std::shuffle( order.begin(), order.end(), random );
    if ( const char *pWanted = CmdLine::GetArgValue( "-rival" ) ) {
        const std::string wanted = ToLower( pWanted );
        for ( size_t i = 0; i < order.size(); i++ ) {
            if ( ToLower( drivers[order[i]].Name ).find( wanted ) != std::string::npos ) {
                std::swap( order[0], order[i] );
                break;
            }
        }
    }
    for ( size_t k = 0; k < order.size() && rivalTypes < count; k++ ) {
        const AiDriver &driver = drivers[order[k]];
        // The signature car when the driver has one (Miami's Mustang "Miami"),
        // else one of the garage [INF].
        const AiGarageCar *pCar = FindSignatureCar( driver );
        if ( pCar == nullptr && !driver.Garage.empty() ) {
            pCar = &driver.Garage[std::uniform_int_distribution<size_t>( 0, driver.Garage.size() - 1 )( random )];
        }
        if ( pCar == nullptr ) {
            continue;
        }
        Type type;
        type.Id = pCar->CarId;
        type.Weight = 0.0f;
        type.Rival = std::make_unique<AiCar>();
        if ( !type.Rival->load( pCar->CarId, pCar->ColorId ) ) {
            OTDU_LOG_WARN( "Traffic: rival %s: car %d not available\n", driver.Name.c_str(), pCar->CarId );
            continue;
        }
        type.Length = type.Rival->getStats().Length;
        type.RivalName = driver.Name;
        OTDU_LOG_INFO( "Traffic: rival %s in the %s (car %d%s%s)\n", driver.Name.c_str(),
                       type.Rival->getStats().DisplayName.c_str(), pCar->CarId, pCar->ColorId.empty() ? "" : ", ",
                       pCar->ColorId.c_str() );
        types.push_back( std::move( type ) );
        rivalTypes++;
    }
    rivalTimer = CmdLine::HasArg( "-rivalsoon" ) ? 2.0f : std::uniform_real_distribution<float>( 25.0f, 60.0f )( random );
}

void Traffic::tickRivals( float deltaTime, const Vector3f &player, const CinematicCar::SurfaceFunction &surface ) {
    if ( rivalTypes == 0 ) {
        return;
    }
    const size_t first = types.size() - static_cast<size_t>( rivalTypes );
    const bool bNight = hour < 6.0f || hour >= 20.0f;
    std::vector<bool> alive( static_cast<size_t>( rivalTypes ), false );
    int32_t live = 0;
    for ( const Agent &agent : agents ) {
        if ( agent.Type < 0 || static_cast<size_t>( agent.Type ) < first ) {
            continue;
        }
        alive[static_cast<size_t>( agent.Type ) - first] = true;
        live++;
        // The wheels: roll by the distance, steer by the yaw rate over the
        // speed (the path curvature) x a 2.7 m wheelbase.
        const float steer = Clamp( std::atan( 2.7f * agent.YawRate / Max( agent.Speed, 2.0f ) ), -0.6f, 0.6f );
        types[static_cast<size_t>( agent.Type )].Rival->animate( deltaTime, agent.Speed * deltaTime, steer,
                                                                 agent.BrakeHold > 0.0f, bNight );
    }
    rivalTimer -= deltaTime;
    static const int32_t maxLive = Max( 1, CmdLine::GetArgInt( "-rivalsmax", 1 ) );
    if ( rivalTimer > 0.0f || live >= maxLive ) {
        return;
    }
    std::vector<int32_t> free;
    for ( int32_t i = 0; i < rivalTypes; i++ ) {
        if ( !alive[static_cast<size_t>( i )] ) {
            free.push_back( static_cast<int32_t>( first ) + i );
        }
    }
    if ( free.empty() ) {
        return;
    }
    static const bool bFirstInOrder = CmdLine::HasArg( "-rival" );
    static bool bFirstSpawn = true;
    const int32_t type = bFirstInOrder && bFirstSpawn ? free.front()
                                                       : free[std::uniform_int_distribution<size_t>( 0, free.size() - 1 )( random )];
    if ( !spawn( player, surface, type ) ) {
        rivalTimer = 3.0f; // no room on the ring this time: try again soon
        return;
    }
    bFirstSpawn = false;
    // -rivalpark: the rival becomes agent 0, which -trafficpark parks in front
    // of the player (the test shot of the car).
    if ( CmdLine::HasArg( "-rivalpark" ) && agents.size() > 1 ) {
        std::rotate( agents.begin(), agents.end() - 1, agents.end() );
    }
    const Agent &agent = CmdLine::HasArg( "-rivalpark" ) ? agents.front() : agents.back();
    const float dx = agent.Position.x - player.x, dz = agent.Position.z - player.z;
    OTDU_LOG_INFO( "Traffic: rival %s appears in the %s at (%.0f, %.0f), %.0f m from the player, %.0f km/h\n",
                   types[static_cast<size_t>( type )].RivalName.c_str(),
                   types[static_cast<size_t>( type )].Rival->getStats().DisplayName.c_str(), agent.Position.x, agent.Position.z,
                   std::sqrt( dx * dx + dz * dz ), agent.CruiseSpeed * 3.6f );
    // The next one in 1..2.5 minutes [INF].
    rivalTimer = CmdLine::HasArg( "-rivalsoon" ) ? 10.0f : std::uniform_real_distribution<float>( 60.0f, 150.0f )( random );
}

void Traffic::assignRivalEnvironment( EdenRenderer &renderer, GPUTexture *pCube ) {
    for ( Type &type : types ) {
        if ( type.Rival ) {
            type.Rival->assignEnvironment( renderer, pCube );
        }
    }
}

bool Traffic::findNearestRival( const Vector3f &position, float radius, std::string &outDriver, std::string &outCar,
                                float &outDistance ) const {
    bool bFound = false;
    outDistance = radius;
    for ( const Agent &agent : agents ) {
        const Type &type = types[static_cast<size_t>( agent.Type )];
        if ( !type.Rival ) {
            continue;
        }
        const float dx = agent.Position.x - position.x, dz = agent.Position.z - position.z;
        const float distance = std::sqrt( dx * dx + dz * dz );
        if ( distance < outDistance ) {
            outDistance = distance;
            outDriver = type.RivalName;
            outCar = type.Rival->getStats().DisplayName;
            bFound = true;
        }
    }
    return bFound;
}
