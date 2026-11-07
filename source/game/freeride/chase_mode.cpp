#include "shared.h"
#include "chase_mode.h"

#include "core/crypto/cpr.h"
#include "core/xmb.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/player_profile.h"
#include "render/eden/eden_headlights.h"
#include "render/eden/eden_renderer.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

namespace {
// The cop car of the island's chaseconfig.cpr: CopCars CopCar level="Low"
// CarId (the DBCarPhysicsData _id_car). [DATA: the config decrypts with the
// shared .cpr XTEA.]
int32_t FindCopCarId( const std::string &island ) {
    if ( gpFile == nullptr ) {
        return 0;
    }
    std::string xml;
    if ( !LoadCPRText( ( gpFile->getBankFolder() + "islands/" + island + "/level/chaseconfig.cpr" ).c_str(), xml ) ) {
        return 0;
    }
    const size_t car = xml.find( "<CopCar" );
    if ( car == std::string::npos ) {
        return 0;
    }
    const size_t id = xml.find( "<CarId>", car );
    if ( id == std::string::npos ) {
        return 0;
    }
    return atoi( xml.c_str() + id + 7 );
}

// aiconfig.cpr TRAFFIC_RULES POLICE_CAR_ID (3009 on Ibiza) [DATA] - the
// traffic police car, which is also what a chase shows in retail.
int32_t FindPoliceTrafficId( const std::string &island ) {
    if ( gpFile == nullptr ) {
        return 0;
    }
    std::string xml;
    if ( !LoadCPRText( ( gpFile->getBankFolder() + "islands/" + island + "/level/aiconfig.cpr" ).c_str(), xml ) ) {
        return 0;
    }
    const size_t at = xml.find( "POLICE_CAR_ID" );
    if ( at == std::string::npos ) {
        return 0;
    }
    // Either <POLICE_CAR_ID>3009</POLICE_CAR_ID> or POLICE_CAR_ID="3009".
    size_t digits = at;
    while ( digits < xml.size() && !isdigit( static_cast<unsigned char>( xml[digits] ) ) ) {
        digits++;
    }
    return digits < xml.size() ? atoi( xml.c_str() + digits ) : 0;
}

// The traffic bank of a database id (Traffic's FindTraffic rule): islands/
// <island>/vehicules/traffic/<_model_name><_version_name>.bnk, lower case.
bool FindTrafficModel( int32_t carId, std::string &outName, float &outLength, float &outRadius ) {
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
        outLength = car["_length"].asFloat( 4500.0f ) * 0.001f;
        outRadius = car["_tire_radius"].asFloat( 32.0f ) / 200.0f;
        return !name.empty();
    }
    return false;
}

constexpr float kContactSpeedKmh = 15.0f; // a cop can only pull over a car this slow [INF]
constexpr float kCopLaneOffset = 1.9f;    // the traffic's right-hand lane (the driver's right)
// The arrest hold: long enough for the cops to close in on the stopped car
// and for the game mode's pull-over shot (the police come to your window).
// The retail hold is not readable in the encrypted band [INF]; 5 s fits the
// arrival, the handcuffs and the fine text.
constexpr float kArrestHoldSeconds = 5.0f;
// How close a cop noses in beside the stopped car during the arrest hold
// (the pull-over: he stops at the driver's window, not on the car) [INF].
constexpr float kArrestParkDistance = 4.0f;
// The FA_<letter>_<pos> glow sprite of the traffic police model (the same
// function letters as the ambient traffic, docs/re/car_lights_spec.md 2.5).
int32_t LampFunctionIndex( char c ) {
    switch ( c ) {
    case 'L': return 0; // front lights
    case 'B': return 1; // rear position
    case 'S': return 2; // stop
    case 'R': return 3; // reverse
    case 'W': return 4; // warning / turn indicator
    default: return -1;
    }
}


} // namespace

void ChaseMode::setRoads( const std::vector<Vector3f> *pRoadNodes, const std::vector<std::pair<int32_t, int32_t>> *pRoadEdges,
                          const std::vector<std::vector<Vector3f>> *pRoadShapes ) {
    pNodes = pRoadNodes;
    pEdges = pRoadEdges;
    pShapes = pRoadShapes;
    adjacency.clear();
    cumulative.clear();
    edgeLength.clear();
    if ( pNodes == nullptr || pEdges == nullptr || pShapes == nullptr || pEdges->empty() || pShapes->size() != pEdges->size() ) {
        pNodes = nullptr;
        pEdges = nullptr;
        pShapes = nullptr;
        return;
    }
    adjacency.assign( pNodes->size(), {} );
    cumulative.resize( pEdges->size() );
    edgeLength.assign( pEdges->size(), 0.0f );
    for ( size_t i = 0; i < pEdges->size(); i++ ) {
        const std::pair<int32_t, int32_t> &edge = ( *pEdges )[i];
        if ( edge.first >= 0 && static_cast<size_t>( edge.first ) < adjacency.size() ) {
            adjacency[static_cast<size_t>( edge.first )].push_back( static_cast<int32_t>( i ) );
        }
        if ( edge.second >= 0 && static_cast<size_t>( edge.second ) < adjacency.size() ) {
            adjacency[static_cast<size_t>( edge.second )].push_back( static_cast<int32_t>( i ) );
        }
        const std::vector<Vector3f> &points = ( *pShapes )[i];
        std::vector<float> &lengths = cumulative[i];
        lengths.assign( points.size(), 0.0f );
        for ( size_t k = 1; k < points.size(); k++ ) {
            const float dx = points[k].x - points[k - 1].x, dz = points[k].z - points[k - 1].z;
            lengths[k] = lengths[k - 1] + std::sqrt( dx * dx + dz * dz );
        }
        edgeLength[i] = points.empty() ? 0.0f : lengths.back();
    }
    OTDU_LOG_INFO( "ChaseMode: road graph %zu nodes, %zu links\n", pNodes->size(), pEdges->size() );
}

void ChaseMode::sampleEdge( int32_t edge, bool bForward, float distance, Vector3f &outPos, Vector3f &outTangent ) const {
    const std::vector<Vector3f> &points = ( *pShapes )[static_cast<size_t>( edge )];
    const std::vector<float> &lengths = cumulative[static_cast<size_t>( edge )];
    const float length = edgeLength[static_cast<size_t>( edge )];
    if ( points.size() < 2 ) {
        outPos = points.empty() ? Vector3f() : points[0];
        outTangent = Vector3f( 0.0f, 0.0f, 1.0f );
        return;
    }
    const float s = Clamp( bForward ? distance : length - distance, 0.0f, length );
    size_t k = 1;
    while ( k + 1 < points.size() && lengths[k] < s ) {
        k++;
    }
    const Vector3f &a = points[k - 1];
    const Vector3f &b = points[k];
    const float span = Max( lengths[k] - lengths[k - 1], 0.001f );
    const float t = Clamp( ( s - lengths[k - 1] ) / span, 0.0f, 1.0f );
    outPos = Vector3f( a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t );
    Vector3f tangent( b.x - a.x, 0.0f, b.z - a.z );
    tangent = tangent.length() > 0.0001f ? tangent.normalized() : Vector3f( 0.0f, 0.0f, 1.0f );
    outTangent = bForward ? tangent : tangent * -1.0f;
}

bool ChaseMode::nearestEdge( const Vector3f &point, int32_t &outEdge, float &outDistance, float &outGap ) const {
    if ( pEdges == nullptr ) {
        return false;
    }
    outEdge = -1;
    outDistance = 0.0f;
    outGap = 1e30f;
    for ( size_t i = 0; i < pEdges->size(); i++ ) {
        const std::vector<Vector3f> &points = ( *pShapes )[i];
        if ( points.size() < 2 ) {
            continue;
        }
        // Coarse reject on the link's middle point: a link cannot come closer
        // than (middle distance - half its length).
        const Vector3f &middle = points[points.size() / 2];
        const float mx = middle.x - point.x, mz = middle.z - point.z;
        const float reach = outGap + 0.5f * edgeLength[i];
        if ( mx * mx + mz * mz > reach * reach ) {
            continue;
        }
        for ( size_t k = 1; k < points.size(); k++ ) {
            const Vector3f &a = points[k - 1];
            const Vector3f &b = points[k];
            const float ex = b.x - a.x, ez = b.z - a.z;
            const float lengthSq = ex * ex + ez * ez;
            const float t = lengthSq > 0.0001f ? Clamp( ( ( point.x - a.x ) * ex + ( point.z - a.z ) * ez ) / lengthSq, 0.0f, 1.0f ) : 0.0f;
            const float px = a.x + ex * t - point.x, pz = a.z + ez * t - point.z;
            const float gap = std::sqrt( px * px + pz * pz );
            if ( gap < outGap ) {
                outGap = gap;
                outEdge = static_cast<int32_t>( i );
                outDistance = cumulative[i][k - 1] + std::sqrt( lengthSq ) * t; // along first -> second
            }
        }
    }
    return outEdge >= 0;
}

void ChaseMode::reset() {
    state = eState::Clean;
    awareness = 0.0f;
    sinceViolation = 0.0f;
    timerSeconds = 0.0f;
    bTimerVisible = false;
    escapeTime = 0.0f;
    arrestTime = 0.0f;
    escapedTime = 0.0f;
    bArrestedEvent = false;
    bEscapedEvent = false;
    despawnCops();
}

void ChaseMode::unload() {
    reset();
    cops.clear();
    copFileName.clear();
    policeModel.reset();
    policeBank.reset();
    policeGroundLift = 0.0f;
    PoliceBar = PoliceLightBar();
    gyroClock = 0.0f;
    for ( int32_t function = 0; function < kLampFunctions; function++ ) {
        LampNodes[function].clear();
    }
    LampGlows.clear();
    pNodes = nullptr;
    pEdges = nullptr;
    pShapes = nullptr;
    adjacency.clear();
    cumulative.clear();
    edgeLength.clear();
}

void ChaseMode::loadCopCar( const std::string &island ) {
    // The traffic police bank first: the chase shows the island's own police
    // car (ibiza_police.bnk). Before this the CopCar file name 'Dodge_Charger'
    // had no player-car bank and CinematicCar fell back to the California -
    // the cops looked like the player's car.
    policeModel.reset();
    policeBank.reset();
    policeGroundLift = 0.0f;
    const int32_t policeId = FindPoliceTrafficId( island );
    std::string policeName;
    float policeRadius = 0.0f;
    if ( policeId != 0 && gpRender != nullptr && gpFile != nullptr &&
         FindTrafficModel( policeId, policeName, policeLength, policeRadius ) ) {
        const std::string bankPath = gpFile->getBankFolder() + "islands/" + island + "/vehicules/traffic/" + policeName + ".bnk";
        if ( gpFile->fileExists( bankPath.c_str() ) ) {
            policeBank = std::make_unique<Bank>();
            if ( policeBank->load( bankPath.c_str() ) ) {
                gpRender->getTextureCache().loadBankTextures( *policeBank, policeBank->getPath().c_str() );
                policeModel = std::make_unique<EdenModel>();
                if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer();
                     pRenderer == nullptr || !policeModel->load( *pRenderer, *policeBank, policeName.c_str(), nullptr, policeBank->getPath().c_str() ) ) {
                    policeModel.reset();
                    policeBank.reset();
                }
            } else {
                policeBank.reset();
            }
        }
    }
    if ( policeModel != nullptr ) {
        // Traffic's rule: the pivot sits below the tire contact plane; lift by
        // radius - wheel centre height so the tires ride on the road.
        const int32_t wheelNode = policeModel->getScene().findNode( "WLR_FL" );
        if ( wheelNode >= 0 && policeRadius > 0.05f ) {
            policeGroundLift = policeRadius - policeModel->getScene().getNodes()[static_cast<size_t>( wheelNode )].World.m[3][1];
        }
        // The light glow sprites (FA_<L|B|S|R|W>_<pos>) and the roof light bar
        // (Gyro material + FA_G quads). They are hidden per draw through
        // EdenDrawParams: a cop on duty shows its headlights + rear position
        // day and night, the stop lamps while it brakes; the bar sweeps.
        PoliceBar.collect( *policeModel, gpRender != nullptr ? &gpRender->getTextureCache() : nullptr );
        for ( int32_t function = 0; function < kLampFunctions; function++ ) {
            LampNodes[function].clear();
        }
        for ( const EdenModel::Item &item : policeModel->getItems() ) {
            const SceneNode &node = policeModel->getScene().getNodes()[static_cast<size_t>( item.NodeIndex )];
            if ( node.Name.compare( 0, 3, "FA_" ) != 0 || node.Name.size() < 6 ) {
                continue;
            }
            const int32_t function = LampFunctionIndex( node.Name[3] );
            if ( function < 0 ) {
                continue;
            }
            std::vector<int32_t> &group = LampNodes[function];
            if ( std::find( group.begin(), group.end(), item.NodeIndex ) == group.end() ) {
                group.push_back( item.NodeIndex );
            }
        }
        OTDU_LOG_INFO( "ChaseMode: traffic police model '%s' (id %d, length %.2f m, lift %.3f)\n", policeName.c_str(), policeId,
                       policeLength, policeGroundLift );
        OTDU_LOG_INFO( "ChaseMode: police light sprites L %zu B %zu S %zu R %zu W %zu, roof bar %u material(s) / rings R %zu G %zu B %zu A %zu\n",
                       LampNodes[0].size(), LampNodes[1].size(), LampNodes[2].size(), LampNodes[3].size(), LampNodes[4].size(),
                       PoliceBar.getMaterialCount(), PoliceBar.getNodes( 0 ).size(), PoliceBar.getNodes( 1 ).size(),
                       PoliceBar.getNodes( 2 ).size(), PoliceBar.getNodes( 3 ).size() );
        // The same nodes as bloom anchors (the glows of appendLampGlows).
        EdenCollectLampAnchors( *policeModel, LampGlows );
    }
    const int32_t carId = FindCopCarId( island );
    copFileName.clear();
    if ( gpDatabase != nullptr && carId != 0 ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
            XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
            if ( static_cast<int32_t>( car["_id_car"].asInt() ) == carId ) {
                // The names carry "##" placeholders (car_shop's cleaner).
                copFileName = car["_car_file_name"].asString();
                for ( size_t at = copFileName.find( "##" ); at != std::string::npos; at = copFileName.find( "##" ) ) {
                    copFileName.erase( at, 2 );
                }
                while ( !copFileName.empty() && copFileName.back() == ' ' ) {
                    copFileName.pop_back();
                }
                break;
            }
        }
    }
    if ( copFileName.empty() ) {
        copFileName = "Dodge_Charger"; // the Ibiza police car of the capture [INF]
    }
    OTDU_LOG_INFO( "ChaseMode: cop car '%s' (CarId %d)\n", copFileName.c_str(), carId );
}

void ChaseMode::addViolation( float strength ) {
    if ( state == eState::Chase || state == eState::Arrested ) {
        return;
    }
    awareness = Min( awareness + strength, config.AwarenessMax );
    sinceViolation = 0.0f;
    // A violation puts the police on notice (Clean -> Noticed): the tailing
    // meter shows from awareness01 alone, and the awareness now decays after
    // DecayDelay. Without this the Noticed state was unreachable, so the decay
    // branch in tick() (guarded on Noticed) never ran and the meter stuck at
    // its last value until the next hit.
    if ( state == eState::Clean ) {
        state = eState::Noticed;
    }
}

void ChaseMode::startChase() {
    awareness = config.AwarenessMax;
    sinceViolation = 0.0f;
}

bool ChaseMode::takeArrested( float &outFine ) {
    if ( !bArrestedEvent ) {
        return false;
    }
    bArrestedEvent = false;
    outFine = arrestedFine;
    return true;
}

bool ChaseMode::takeEscaped( float &outReward ) {
    if ( !bEscapedEvent ) {
        return false;
    }
    bEscapedEvent = false;
    outReward = escapedReward;
    return true;
}

void ChaseMode::spawnCops( const Vector3f &playerPos, const SurfaceFunction &surface ) {
    despawnCops();
    if ( copFileName.empty() ) {
        return;
    }
    // The player's place on the road graph: the cops start on the same link,
    // 60 / 85 m behind, already driving toward him (retail cops arrive from
    // behind [INF]).
    int32_t playerEdge = -1;
    float playerAlong = 0.0f, playerGap = 0.0f;
    const bool bOnRoads = nearestEdge( playerPos, playerEdge, playerAlong, playerGap ) && playerGap < 60.0f;
    for ( int32_t i = 0; i < config.CopCount; i++ ) {
        Cop cop;
        if ( policeModel == nullptr ) {
            cop.car = std::make_unique<CinematicCar>();
            CinematicCar::Desc desc;
            desc.FileName = copFileName;
            if ( !cop.car->load( desc ) ) {
                OTDU_LOG_WARN( "ChaseMode: cop car '%s' not available\n", copFileName.c_str() );
                continue;
            }
        }
        const float behind = 60.0f + 25.0f * i;
        bool bPlaced = false;
        if ( bOnRoads ) {
            // Which way along the link is "toward the player"? Pick the
            // direction of travel that reaches the player's point, i.e. start
            // `behind` metres before it in that direction.
            const float length = edgeLength[static_cast<size_t>( playerEdge )];
            // Forward (first -> second) when there is room behind the player
            // on that side, else the other way.
            cop.Edge = playerEdge;
            cop.bForward = playerAlong >= behind || playerAlong > length - playerAlong;
            const float along = cop.bForward ? playerAlong : length - playerAlong; // in the direction of travel
            cop.Distance = Max( 0.0f, along - behind );
            Vector3f tangent;
            sampleEdge( cop.Edge, cop.bForward, cop.Distance, cop.Position, tangent );
            cop.Forward = tangent;
            // The traffic's right-hand lane: driver's right = forward x up.
            cop.Position = cop.Position + Vector3f( -tangent.z, 0.0f, tangent.x ) * kCopLaneOffset;
            bPlaced = true;
        }
        if ( !bPlaced ) {
            // No road graph: behind the player on a fan of headings.
            const float angle = 3.14159265f + ( i - ( config.CopCount - 1 ) * 0.5f ) * 0.5f;
            cop.Forward = Vector3f( std::sin( angle ), 0.0f, std::cos( angle ) );
            cop.Position = playerPos + cop.Forward * ( -behind );
            cop.Edge = -1;
        }
        float ground = playerPos.y;
        for ( const float offset : { 5.0f, 40.0f, -20.0f } ) {
            if ( surface( cop.Position.x, cop.Position.z, playerPos.y + offset, ground ) ) {
                break;
            }
            ground = playerPos.y;
        }
        cop.Position.y = ground;
        cop.LastY = ground;
        cop.Speed = 15.0f;
        cop.World = Matrix4x4f::Translation( cop.Position ) *
                    Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), std::atan2( cop.Forward.x, cop.Forward.z ) ) );
        cops.push_back( std::move( cop ) );
    }
    OTDU_LOG_INFO( "ChaseMode: chase started, %d cops (%s, %s)\n", static_cast<int32_t>( cops.size() ),
                   policeModel != nullptr ? "traffic police model" : copFileName.c_str(), bOnRoads ? "on the road graph" : "free pursuit" );
}

void ChaseMode::despawnCops() {
    if ( !cops.empty() && gpRender != nullptr ) {
        // The rule of the car switch (gm_freeride.cpp) and SpotExteriors::unload:
        // the cop models hold GPU resources, the shadow caster cache holds
        // their material pointers and the RT instances their BLAS addresses -
        // drain the device and drop the caches before they go (session 46's
        // "random unresponsiveness after driving a while" fired here, right
        // after the arrest despawn).
        gpRender->getDevice().waitIdle();
    }
    cops.clear();
    if ( gpRender != nullptr ) {
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pRenderer->clearShadowCasterCache();
            pRenderer->clearRtInstances();
        }
    }
}

void ChaseMode::tick( float deltaTime, const Vector3f &playerPos, float playerSpeedKmh, const SurfaceFunction &surface ) {
    surfaceFn = surface; // the draw fits the cop cars on the same ground
    gyroClock += deltaTime; // the roof light bar sweep
    if ( state == eState::Arrested ) {
        // The hold after the arrest (the handcuffs on screen); the game mode
        // closes the sequence (fine paid, cops despawn on Clean). The cops do
        // not freeze where the chase left them: they close in on the stopped
        // car and nose in beside it - the retail pull-over, the police coming
        // to your window. Beyond kArrestParkDistance a cop steers straight at
        // the car and bleeds speed with the gap, so the last metres are a
        // creep and he never rams the stopped car.
        arrestTime -= deltaTime;
        for ( Cop &cop : cops ) {
            if ( !cop.car && policeModel == nullptr ) {
                continue;
            }
            Vector3f toCar( playerPos.x - cop.Position.x, 0.0f, playerPos.z - cop.Position.z );
            const float distance = toCar.length();
            if ( distance <= kArrestParkDistance ) {
                cop.Speed = 0.0f; // parked at the driver's window
                continue;
            }
            const float target = Clamp( ( distance - kArrestParkDistance ) * 0.9f, 1.5f, 18.0f );
            const float previousSpeed = cop.Speed;
            if ( cop.Speed < target ) {
                cop.Speed = Min( cop.Speed + 12.0f * deltaTime, target );
            } else {
                cop.Speed = Max( cop.Speed - 20.0f * deltaTime, target );
            }
            cop.BrakeHold = Max( 0.0f, cop.BrakeHold - deltaTime );
            if ( cop.Speed + 0.35f < previousSpeed ) {
                cop.BrakeHold = 0.4f;
            }
            const Vector3f dir = toCar.normalized();
            const float turn = Clamp( 2.2f * deltaTime, 0.0f, 1.0f );
            cop.Forward = ( cop.Forward * ( 1.0f - turn ) + dir * turn ).normalized();
            cop.Position = cop.Position + cop.Forward * ( cop.Speed * deltaTime );
            float ground = cop.Position.y;
            if ( surface( cop.Position.x, cop.Position.z, cop.Position.y + 5.0f, ground ) ) {
                cop.Position.y = ground;
                cop.LastY = ground;
            }
            cop.World = Matrix4x4f::Translation( cop.Position ) *
                        Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), std::atan2( cop.Forward.x, cop.Forward.z ) ) );
        }
        if ( arrestTime <= 0.0f ) {
            reset();
        }
        return;
    }
    if ( state == eState::Escaped ) {
        escapedTime -= deltaTime;
        if ( escapedTime <= 0.0f ) {
            reset();
        }
        return;
    }

    // Awareness decay (Clean -> Noticed: the tailing meter shows).
    sinceViolation += deltaTime;
    if ( sinceViolation > config.DecayDelay && state == eState::Noticed ) {
        awareness = Max( 0.0f, awareness - config.AwarenessDecay * deltaTime );
        if ( awareness <= 0.0f ) {
            state = eState::Clean;
            OTDU_LOG_INFO( "ChaseMode: attention lost\n" );
        }
    }
    if ( state != eState::Chase ) {
        if ( awareness >= config.AwarenessMax ) {
            state = eState::Chase;
            timerSeconds = config.TimerSeconds;
            bTimerVisible = false;
            escapeTime = 0.0f;
            spawnCops( playerPos, surface );
        }
        return;
    }

    // ---- the chase: the cops pursue (kinematic [INF: the retail pursuit AI
    // is encrypted-band code]). With the road graph a cop drives the links
    // toward the player (pursueOnRoads); without it he steers straight at
    // him. Either way he speeds with the player and rubber-bands when left
    // behind.
    float nearest = 1e30f;
    bool bAllFar = true;
    for ( Cop &cop : cops ) {
        if ( !cop.car && policeModel == nullptr ) {
            continue;
        }
        const Vector3f toPlayer = playerPos - cop.Position;
        const float distance = toPlayer.length();
        nearest = Min( nearest, distance );
        bAllFar &= distance > config.EscapeRange;
        // Target speed: the player's with an edge; far cops push harder
        // (the chase must stay tense), close cops hold back (no ramming).
        float target = Max( playerSpeedKmh / 3.6f + 6.0f, 25.0f );
        if ( distance > 120.0f ) {
            target += 14.0f;
        } else if ( distance < 12.0f ) {
            target = Max( playerSpeedKmh / 3.6f * 0.9f, playerSpeedKmh < kContactSpeedKmh ? 0.0f : 4.0f );
        }
        const float previousSpeed = cop.Speed;
        if ( cop.Speed < target ) {
            cop.Speed = Min( cop.Speed + 14.0f * deltaTime, target );
        } else {
            cop.Speed = Max( cop.Speed - 22.0f * deltaTime, target );
        }
        // The stop lamps follow a real decel (a rubber-band slow-down is not
        // braking and must not light them).
        cop.BrakeHold = Max( 0.0f, cop.BrakeHold - deltaTime );
        if ( cop.Speed + 0.35f < previousSpeed ) {
            cop.BrakeHold = 0.4f;
        }
        if ( cop.Edge >= 0 && pEdges != nullptr ) {
            pursueOnRoads( cop, deltaTime, playerPos, surface );
        } else {
            Vector3f dir( toPlayer.x, 0.0f, toPlayer.z );
            dir = dir.normalized();
            const float turn = Clamp( 2.2f * deltaTime, 0.0f, 1.0f );
            cop.Forward = ( cop.Forward * ( 1.0f - turn ) + dir * turn ).normalized();
            cop.Position = cop.Position + cop.Forward * ( cop.Speed * deltaTime );
            float ground = cop.Position.y;
            if ( surface( cop.Position.x, cop.Position.z, cop.Position.y + 5.0f, ground ) ) {
                cop.Position.y = ground;
                cop.LastY = ground;
            }
            cop.World = Matrix4x4f::Translation( cop.Position ) *
                        Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), std::atan2( cop.Forward.x, cop.Forward.z ) ) );
        }
        // Contact arrest: a cop beside a *slow* player for ContactTime (the
        // retail pull-over: a car that keeps driving is only caught by the
        // countdown; before this a cop touching a moving car arrested it in
        // 1.5 s, and a parked player was arrested without ever being chased).
        if ( distance < 12.0f && playerSpeedKmh < kContactSpeedKmh ) {
            cop.ContactTime += deltaTime;
            if ( cop.ContactTime >= config.ContactTime ) {
                arrest( "contact" );
                return;
            }
        } else {
            cop.ContactTime = 0.0f;
        }
    }

    // -logpolice: the pursuit every second (distance, speed, link).
    static const bool bLogPolice = CmdLine::HasArg( "-logpolice" );
    if ( bLogPolice ) {
        static float logTimer = 0.0f;
        logTimer += deltaTime;
        if ( logTimer >= 1.0f ) {
            logTimer = 0.0f;
            for ( size_t i = 0; i < cops.size(); i++ ) {
                const Cop &cop = cops[i];
                const float distance = ( playerPos - cop.Position ).length();
                OTDU_LOG_INFO( "ChaseMode: cop %zu at %.0f m, %.1f m/s, link %d %s %.0f/%.0f m, player %.0f km/h, timer %s %.1f\n", i,
                               distance, cop.Speed, cop.Edge, cop.bForward ? "fwd" : "rev", cop.Distance,
                               cop.Edge >= 0 ? edgeLength[static_cast<size_t>( cop.Edge )] : 0.0f, playerSpeedKmh,
                               bTimerVisible ? "on" : "off", timerSeconds );
            }
        }
    }

    // The arrest countdown: armed while a cop is close, disarmed when the
    // cops fall back (the retail hint: "When the timer appears, your end is
    // near! ... If the timer reaches 0, you lose the chase!" loc 6614).
    if ( !bTimerVisible && nearest < config.TimerRange ) {
        bTimerVisible = true;
        timerSeconds = config.TimerSeconds;
    } else if ( bTimerVisible && nearest > config.TimerSafeRange ) {
        bTimerVisible = false;
        timerSeconds = config.TimerSeconds;
    }
    if ( bTimerVisible ) {
        timerSeconds -= deltaTime;
        if ( timerSeconds <= 0.0f ) {
            arrest( "timer" );
            return;
        }
    }
    // The escape: all cops far for EscapeTime.
    if ( bAllFar ) {
        escapeTime += deltaTime;
        if ( escapeTime >= config.EscapeTime ) {
            state = eState::Escaped;
            escapedTime = 3.0f;
            bTimerVisible = false;
            bEscapedEvent = true;
            PlayerProfile &profile = GetPlayerProfile();
            escapedReward = config.EscapeReward + 500.0f * profile.OutlawLevel;
            profile.Money += static_cast<int32_t>( escapedReward );
            profile.NbChaseEscapes++;
            profile.OutlawLevel = Min( profile.OutlawLevel + 1, 3 );
            OTDU_LOG_INFO( "ChaseMode: ESCAPED, reward $%d, outlaw level %d\n", static_cast<int32_t>( escapedReward ),
                           profile.OutlawLevel );
        }
    } else {
        escapeTime = 0.0f;
    }
}

void ChaseMode::arrest( const char *pReason ) {
    state = eState::Arrested;
    // The hold covers the pull-over: the cops close in on the stopped car
    // (the Arrested tick drives them), the game mode takes the controls away
    // and plays its shot, then the sequence closes (fine paid, cops despawn).
    arrestTime = kArrestHoldSeconds;
    bTimerVisible = false;
    bArrestedEvent = true;
    PlayerProfile &profile = GetPlayerProfile();
    arrestedFine = config.FineBase + config.FinePerLevel * profile.OutlawLevel;
    profile.OutlawLevel = 0;
    profile.NbChaseArrests++;
    for ( Cop &cop : cops ) {
        // The cops keep their speed: the Arrested tick steers them up to the
        // stopped car. Freezing them here left them wherever the chase ended,
        // so "the police come to your car" never happened.
        cop.BrakeHold = 0.4f;
    }
    OTDU_LOG_INFO( "ChaseMode: ARRESTED (%s), fine $%d\n", pReason, static_cast<int32_t>( arrestedFine ) );
}

void ChaseMode::pursueOnRoads( Cop &cop, float deltaTime, const Vector3f &playerPos, const SurfaceFunction &surface ) {
    // The player's place on the graph: the target link. Re-evaluated every
    // tick (cheap: the coarse reject keeps it to the links near the player).
    int32_t targetEdge = -1;
    float targetAlong = 0.0f, targetGap = 0.0f;
    const bool bTarget = nearestEdge( playerPos, targetEdge, targetAlong, targetGap );
    cop.TurnCooldown = Max( 0.0f, cop.TurnCooldown - deltaTime );
    const float length = edgeLength[static_cast<size_t>( cop.Edge )];

    // On the player's link: is he behind us? Then turn round (once per 4 s,
    // so a cop does not oscillate around a weaving player).
    if ( bTarget && targetEdge == cop.Edge && cop.TurnCooldown <= 0.0f ) {
        const float playerDir = cop.bForward ? targetAlong : length - targetAlong; // in our direction of travel
        if ( playerDir < cop.Distance - 15.0f ) {
            cop.bForward = !cop.bForward;
            cop.Distance = length - cop.Distance;
            cop.TurnCooldown = 4.0f;
        }
    }
    // Slow for the junction ahead (the target speed law in tick() already
    // holds a cop back when he is right behind the player).
    float speed = cop.Speed;
    if ( length - cop.Distance < 14.0f ) {
        speed = Min( speed, Max( 9.0f, speed * 0.6f ) );
    }
    cop.Distance += speed * deltaTime;
    if ( cop.Distance >= length ) {
        // At the node: the link whose far end is nearest to the player (a
        // greedy route [INF: the retail cops' path finding is encrypted]);
        // never straight back unless it is a dead end.
        const std::pair<int32_t, int32_t> &current = ( *pEdges )[static_cast<size_t>( cop.Edge )];
        const int32_t node = cop.bForward ? current.second : current.first;
        int32_t best = -1;
        float bestScore = 1e30f;
        if ( node >= 0 && static_cast<size_t>( node ) < adjacency.size() ) {
            const std::vector<int32_t> &links = adjacency[static_cast<size_t>( node )];
            for ( const int32_t next : links ) {
                if ( next == cop.Edge && links.size() > 1 ) {
                    continue;
                }
                const std::pair<int32_t, int32_t> &edge = ( *pEdges )[static_cast<size_t>( next )];
                const int32_t farNode = edge.first == node ? edge.second : edge.first;
                float score = 1e29f;
                if ( bTarget && next == targetEdge ) {
                    score = -1.0f; // the player's own link
                } else if ( farNode >= 0 && static_cast<size_t>( farNode ) < pNodes->size() ) {
                    const Vector3f &far = ( *pNodes )[static_cast<size_t>( farNode )];
                    const float dx = far.x - playerPos.x, dz = far.z - playerPos.z;
                    score = std::sqrt( dx * dx + dz * dz );
                }
                if ( score < bestScore ) {
                    bestScore = score;
                    best = next;
                }
            }
        }
        const float overshoot = cop.Distance - length;
        if ( best < 0 ) {
            cop.bForward = !cop.bForward; // dead end: turn round
            cop.Distance = Max( 0.0f, overshoot );
        } else {
            cop.Distance = Max( 0.0f, overshoot );
            cop.Edge = best;
            cop.bForward = ( *pEdges )[static_cast<size_t>( best )].first == node;
            cop.TurnCooldown = 2.0f;
        }
    }
    placeOnLane( cop, deltaTime, surface );
}

void ChaseMode::placeOnLane( Cop &cop, float deltaTime, const SurfaceFunction &surface ) {
    Vector3f onRoad, tangent;
    sampleEdge( cop.Edge, cop.bForward, cop.Distance, onRoad, tangent );
    // Driver's right lane (forward x up = (-tz, tx), the right-handed frame).
    const Vector3f lane = onRoad + Vector3f( -tangent.z, 0.0f, tangent.x ) * kCopLaneOffset;
    // Heading eases toward the lane tangent (no snapping at a node).
    const float turn = Clamp( 4.0f * deltaTime, 0.0f, 1.0f );
    cop.Forward = ( cop.Forward * ( 1.0f - turn ) + tangent * turn ).normalized();
    float ground = cop.LastY;
    if ( surface( lane.x, lane.z, cop.LastY + 6.0f, ground ) || surface( lane.x, lane.z, onRoad.y + 6.0f, ground ) ) {
        cop.LastY = ground;
    } else {
        cop.LastY = onRoad.y; // no streamed collision yet: the road curve's own height
    }
    cop.Position = Vector3f( lane.x, cop.LastY, lane.z );
    // Orientation from four ground samples (Traffic's rule): the car follows
    // the slope of the road instead of standing level on a hill.
    const float half = 0.35f * policeLength;
    const float fx = cop.Forward.x, fz = cop.Forward.z;
    // The right-hand ground-sample axis: forward x up = (-fz, fx).
    const float rx = -fz, rz = fx;
    auto sample = [&]( float x, float z ) {
        float height = 0.0f;
        return surface( x, z, cop.Position.y + 3.0f, height ) ? height : cop.Position.y;
    };
    const Vector3f front( cop.Position.x + fx * half, sample( cop.Position.x + fx * half, cop.Position.z + fz * half ), cop.Position.z + fz * half );
    const Vector3f rear( cop.Position.x - fx * half, sample( cop.Position.x - fx * half, cop.Position.z - fz * half ), cop.Position.z - fz * half );
    const Vector3f right( cop.Position.x + rx * 0.8f, sample( cop.Position.x + rx * 0.8f, cop.Position.z + rz * 0.8f ), cop.Position.z + rz * 0.8f );
    const Vector3f left( cop.Position.x - rx * 0.8f, sample( cop.Position.x - rx * 0.8f, cop.Position.z - rz * 0.8f ), cop.Position.z - rz * 0.8f );
    Vector3f forwardVector = front - rear;
    Vector3f up = ( right - left ).cross( forwardVector );
    if ( up.length() < 0.0001f || up.y <= 0.0f ) {
        up = Vector3f( 0.0f, 1.0f, 0.0f );
    }
    up = up.normalized();
    forwardVector = forwardVector.length() > 0.0001f ? forwardVector.normalized() : cop.Forward;
    const Vector3f rightAxis = up.cross( forwardVector ).normalized();
    Matrix4x4f world = Matrix4x4f::Identity();
    world.m[0][0] = rightAxis.x; world.m[0][1] = rightAxis.y; world.m[0][2] = rightAxis.z;
    world.m[1][0] = up.x;        world.m[1][1] = up.y;        world.m[1][2] = up.z;
    world.m[2][0] = forwardVector.x; world.m[2][1] = forwardVector.y; world.m[2][2] = forwardVector.z;
    world.m[3][0] = cop.Position.x;
    world.m[3][1] = 0.5f * ( front.y + rear.y );
    world.m[3][2] = cop.Position.z;
    cop.World = world;
}

void ChaseMode::appendLampGlows( std::vector<EdenLampGlow> &out, bool bNight, float deltaTime ) {
    const float night = bNight ? 1.0f : 0.0f;
    // The shared police bank carries the glows when it loaded (the drawn
    // model); the per-cop CinematicCar is only the fallback car (its anchors
    // sit on ITS body, not the police model's).
    const bool bBankGlows = policeModel != nullptr && !LampGlows.empty();
    for ( Cop &cop : cops ) {
        if ( bBankGlows ) {
            // Over the sprite nodes the draw's hide list shows: head + rear
            // position day and night (the sprite gate), stop while braking.
            Matrix4x4f world = cop.World;
            world.m[3][1] += policeGroundLift;
            for ( const EdenLampAnchor &anchor : LampGlows ) {
                float value = 0.0f, dayShare = 0.45f, core = 1.0f, spread = 0.22f;
                Vector3f color;
                switch ( anchor.Function ) {
                case 0: // L
                    value = 1.0f;
                    color = Vector3f( 1.00f, 0.93f, 0.82f );
                    spread = 0.12f;
                    break;
                case 1: // B
                    value = 0.7f;
                    color = Vector3f( 1.00f, 0.09f, 0.04f );
                    core = 0.0f;
                    break;
                case 2: // S
                    if ( cop.BrakeHold <= 0.0f ) {
                        continue;
                    }
                    value = 1.0f;
                    color = Vector3f( 1.00f, 0.06f, 0.03f );
                    dayShare = 0.55f;
                    break;
                default: // R never, W is the roof bar's business
                    continue;
                }
                const float strength = value * ( dayShare + ( 1.0f - dayShare ) * night );
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
            continue;
        }
        if ( !cop.car || !cop.car->isLoaded() ) {
            continue;
        }
        // The fallback: the light state driven here so the lens materials and
        // the glow anchors answer like the player car's.
        cop.car->setHeadlights( bNight );
        cop.car->setDashboardLit( bNight );
        cop.car->setBrakeLights( cop.BrakeHold > 0.0f );
        cop.car->setReverseLights( false );
        cop.car->updateLights( deltaTime );
        cop.car->appendLampGlows( cop.World, out );
    }
}

void ChaseMode::appendHeadlightCones( std::vector<EdenHeadlightCone> &cones, const Vector3f &playerPosition,
                                      size_t maxCones ) const {
    struct Candidate {
        EdenHeadlightCone cone;
        float distanceSquared;
    };
    std::vector<Candidate> candidates;
    for ( const Cop &cop : cops ) {
        Vector3f forward = cop.Forward;
        forward.y *= 0.25f; // levelled, a slight down aim
        if ( forward.dot( forward ) <= 1e-6f ) {
            continue;
        }
        forward = forward.normalized();
        EdenHeadlightCone cone;
        cone.Position = cop.Position + forward * 2.0f + Vector3f( 0.0f, 0.62f, 0.0f );
        cone.Direction = forward;
        cone.Intensity = 1.0f;
        const Vector3f delta = cone.Position - playerPosition;
        candidates.push_back( { cone, delta.dot( delta ) } );
    }
    std::sort( candidates.begin(), candidates.end(),
               []( const Candidate &a, const Candidate &b ) { return a.distanceSquared < b.distanceSquared; } );
    // maxCones caps the cops' own contribution (the registries already carry
    // the player's and the traffic's cones).
    size_t pushed = 0;
    for ( const Candidate &candidate : candidates ) {
        if ( pushed >= maxCones ) {
            break;
        }
        cones.push_back( candidate.cone );
        pushed++;
    }
}

void ChaseMode::assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube ) {
    for ( Cop &cop : cops ) {
        if ( cop.car && !cop.bEnvironment ) {
            cop.car->assignEnvironment( renderer, pCube );
            cop.bEnvironment = true;
        }
    }
}

void ChaseMode::draw( EdenRenderer &renderer, const WeatherPreset *pWeather ) {
    if ( !surfaceFn ) {
        return;
    }
    static const bool bNoLights = CmdLine::HasArg( "-notrafficlights" );
    static const bool bGyroSprites = CmdLine::HasArg( "-gyrosprites" );
    static const bool bFullBar = CmdLine::HasArg( "-policefullbar" );
    if ( policeModel != nullptr ) {
        // The roof light bar: a cop on duty shows the swept ring of the dark
        // gyro_of lens (the accurate state; -policefullbar binds the lit art
        // instead). The material is shared by the cops, so the state is
        // written once. The retail sprite quads are misplaced by the port's
        // sprite shader, so they stay hidden unless -gyrosprites.
        PoliceBar.applyLens( renderer, *policeModel, bFullBar );
        PoliceBar.apply( *policeModel, gyroClock );
    }
    for ( Cop &cop : cops ) {
        if ( policeModel != nullptr ) {
            // The traffic police model on its tires (Traffic's draw rule).
            // Headlights + rear position day and night, stop lamps on a real
            // decel; the roof bar shows the swept ring only (hide list).
            EdenDrawParams params;
            for ( int32_t function = 0; function < kLampFunctions; function++ ) {
                const bool bShow = !bNoLights &&
                                   ( function == 0 || function == 1 || ( function == 2 && cop.BrakeHold > 0.0f ) );
                if ( !bShow ) {
                    params.ForceHiddenNodes.insert( params.ForceHiddenNodes.end(), LampNodes[function].begin(),
                                                    LampNodes[function].end() );
                }
            }
            if ( bGyroSprites ) {
                PoliceBar.hideInactive( params, gyroClock );
            } else {
                PoliceBar.hideAll( params );
            }
            Matrix4x4f world = cop.World;
            world.m[3][1] += policeGroundLift;
            policeModel->draw( renderer, world, &params );
            continue;
        }
        if ( !cop.car ) {
            continue;
        }
        const float yaw = std::atan2( cop.Forward.x, cop.Forward.z );
        // An Eden quaternion (EdenRotationFromQuaternion conjugates it): the
        // yaw goes in negated, or the car is drawn mirrored about +z.
        const Quaternionf heading( 0.0f, -std::sin( yaw * 0.5f ), 0.0f, std::cos( yaw * 0.5f ) );
        const Matrix4x4f world = cop.car->fitOnGround( cop.Position, heading, surfaceFn );
        cop.car->draw( renderer, world, pWeather, cop.bEnvironment );
    }
}
