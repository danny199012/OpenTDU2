#pragma once

#include "audio/traffic_audio.h"
#include "filesystem/bank.h"
#include "game/freeride/ai_drivers.h"
#include "game/cinematic/cinematic_car.h"
#include "game/freeride/police_light_bar.h"
#include "render/eden/eden_headlights.h"
#include "render/eden/eden_model.h"

#include <memory>
#include <random>

struct WeatherPreset;

// Ambient traffic: cars that follow the island road graph
// (Ibiza_ROAD_MAP.IA) around the player. The mix and the density come from
// <island>/level/aiconfig.cpr (AICONFIG: VEHICLE_REPARTITION, 6 zones, hourly
// NB_TRAFFIC, VEHICLE database_id + day / night frequency) [DATA].
//
// The cars are steered (not warped onto the curve): a pure-pursuit target
// 'look' metres along the right-hand lane of the path, a limited yaw rate and
// a lateral-acceleration speed limit, so a junction is driven as an arc
// instead of the 90 degree snap the curve-following version did. Traffic cars
// keep each other apart (circle separation), yield to a car already inside a
// junction, brake for the player and the chase's cop cars. Cars only live on
// the road graph *component* the player is on (one island), the casino island
// has no traffic at all, and a car is only removed well outside the drawn
// radius so nothing vanishes in view. Junction signals (isRed), collision with
// the player (resolveCollision) and the police witness (nearestPoliceDistance)
// are in; the roof light bar of the police cars is driven by PoliceLightBar.
// Not done: lane changes / overtaking, the real zone lookup (zone 3 is used),
// siren audio.
class Traffic {
public:
    Traffic();
    ~Traffic();

    // hour: 0..24 (the game clock; selects the density and day / night mix).
    // shapes: the curve of each edge from edge.first to edge.second.
    bool load( const std::string &island, const std::vector<Vector3f> &nodes,
               const std::vector<std::pair<int32_t, int32_t>> &edges, const std::vector<std::vector<Vector3f>> &shapes,
               float hour );
    void unload();

    // The headlight cones of the agents whose front lamps are shown (the
    // Traffic::draw night gate), nearest to cameraPosition first, up to
    // maxCones (the renderer's uniform cap).
    void appendHeadlightCones( std::vector<EdenHeadlightCone> &cones, const Vector3f &cameraPosition,
                               bool bNight, size_t maxCones ) const;
    // The red pool behind a braking agent (the stop lamps' ground spill),
    // nearest to cameraPosition first, up to maxCones. Day and night, dimmer
    // by day (the FA_S stop sprites show day too).
    void appendBrakeCones( std::vector<EdenHeadlightCone> &rear, const Vector3f &cameraPosition, bool bNight,
                           size_t maxCones ) const;
    // The agents' lamp glows (EdenLampGlowFrame, car_lights_spec.md 8): the
    // bloom quads over the FA_ lamp sprite nodes — front lights and rear
    // position at night, the stop lamps while braking, the amber blinker of
    // the agent's indicating side on its phase. Agents within 450 m only
    // (EdenSurfaceFx culls at 400).
    void appendLampGlows( std::vector<EdenLampGlow> &out, const Vector3f &cameraPosition, bool bNight ) const;
    inline bool isLoaded() const { return bLoaded; }

    void tick( float deltaTime, const Vector3f &player, const CinematicCar::SurfaceFunction &surface );
    // Flat heading of the player car (set by the game mode each frame): the
    // spawn ring prefers the links behind the player and far ahead, so a car
    // does not appear in the middle of the screen.
    inline void setPlayerHeading( const Vector3f &forward ) { playerHeading = forward; }

    // Other road users the traffic brakes for (the chase's cop cars), set by
    // the game mode before tick(); cleared each tick.
    void setExtraVehicles( const std::vector<Vector3f> &positions ) { extraVehicles = positions; }
    // Distance to the nearest ambient police car (aiconfig POLICE_CAR_ID), or
    // a negative value when none is around: the witness of a violation.
    float nearestPoliceDistance( const Vector3f &position ) const;
    // Police car id of the island (aiconfig.cpr TRAFFIC_RULES POLICE_CAR_ID [DATA]).
    inline int32_t getPoliceCarId() const { return policeCarId; }
    // The roof light bar of every police car in the traffic: off on patrol,
    // the sweeping gyro during a chase (the game mode sets it). -policebar
    // forces it on for a test shot.
    inline void setPoliceLights( bool bOn ) { bPoliceLights = bOn; }
    inline bool arePoliceLightsOn() const { return bPoliceLights; }
    // -trafficpark (debug): heading the parked test cars face.
    inline void setParkForward( const Vector3f &forward ) { parkForward = forward; }
    // Radius (m) inside which the cars are drawn, and the radius a car is
    // removed at. The despawn radius is well beyond the draw radius and a car
    // that is drawn is never removed, so traffic does not vanish in view.
    inline float getViewRadius() const { return viewRadius; }
    inline float getDespawnRadius() const { return despawnRadius; }
    // The dynamic environment cube of the view, bound into the CUBEMAP slot of
    // the car materials (docs/re/car_glass_spec.md 3: FUN_00d58a90). Without
    // it the traffic glass keeps the authored static cube and its specular
    // term reads as a mottled "cracked" window.
    void assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube );
    void draw( EdenRenderer &renderer, const WeatherPreset *pWeather );

    inline int32_t getCount() const { return static_cast<int32_t>( agents.size() ); }

    // The rivals of the free ride (docs/re/opponents_spec.md 5): the story
    // drivers (DBPNJCar) cruising the island in their own cars - the
    // signature car when they have one (Miami's Mustang Fastback "Miami",
    // Tess's SLK 55 "Tess" ...). A few are picked per session and loaded with
    // the traffic; one at a time appears on the road graph now and then
    // (rivalTimer) and drives like the traffic, a little quicker [INF: the
    // retail spawn rule is not readable]. -norivals / -rivals <n> /
    // -rivalsoon (the first one after 2 s) / -logrivals.
    inline bool hasRivals() const { return rivalTypes > 0; }
    void assignRivalEnvironment( EdenRenderer &renderer, GPUTexture *pCube );
    // The nearest live rival within the radius: its driver and car names.
    bool findNearestRival( const Vector3f &position, float radius, std::string &outDriver, std::string &outCar,
                           float &outDistance ) const;
    // Audio (audio/traffic_audio.h): the drawn cars as the engine-sound
    // snapshot, one entry per agent (Id is stable for its lifetime).
    void appendAudioAgents( std::vector<TrafficAudio::Agent> &out ) const;
    // A honk of this frame: the car was blocked by the player (or the player
    // cut it off). Drained by the game mode once per frame.
    struct HornEvent {
        Vector3f Position;
        float Volume = 1.0f;
    };
    void drainHornEvents( std::vector<HornEvent> &out );
    // Live cars within 'radius' of a point (the ambience city / residential
    // test of AmbienceAudio::Context::NearbyCars).
    int32_t countNear( const Vector3f &position, float radius ) const;
    // The nearest agent within the radius (XZ plane, Position is live), -1 when
    // none: the near-car spec card. getAgentCarId resolves it to the
    // DBCarPhysicsData _id_car of its type (FindCarPhysicsRow / the specs).
    int32_t findNearestAgent( const Vector3f &position, float radius, float &outDistance ) const;
    int32_t getAgentCarId( int32_t agent ) const {
        if ( agent < 0 || static_cast<size_t>( agent ) >= agents.size() ) {
            return -1;
        }
        const int32_t type = agents[static_cast<size_t>( agent )].Type;
        return type >= 0 && static_cast<size_t>( type ) < types.size() ? types[static_cast<size_t>( type )].Id : -1;
    }
    // Collision of the player car (centre, flat forward, velocity, width, length,
    // mass) with the traffic: circles along both cars, an impulse on each [INF].
    // outDeltaVelocity is the velocity change of the player; true on contact.
    // Near misses of the player car with traffic this frame (a car passed within
    // 1.6 m without touching, closing faster than 8 m/s; once per car per 4 s).
    // outGap: the smallest gap of the near misses counted (m); the near-miss
    // zone is the FRIM ESQUIVE DIST min (2.3 m, frpoints.cpr) [DATA].
    int32_t detectNearMisses( const Vector3f &position, const Vector3f &forward, const Vector3f &velocity, float width,
                              float length, float &outClosing, float &outGap );
    // -nearmisstest: sweeps a virtual car past live traffic at two lateral offsets (log only).
    void nearMissTest( float deltaTime );
    // -trafficselftest: a player car rammed into the first traffic car (log only).
    void selfTest();
    bool resolveCollision( const Vector3f &position, const Vector3f &forward, const Vector3f &velocity, float width,
                           float length, float mass, Vector3f &outDeltaVelocity );

private:
    // The lamp function letters of the FA_<c>_<FL|FR|RL|RR>[_n] glow sprite
    // nodes (docs/re/car_lights_spec.md 2.5): 'L' front lights, 'B' rear
    // position, 'S' stop, 'R' reverse, 'W' warning (the blinker of the node
    // side). Indexed by this order.
    static constexpr int32_t kLampFunctions = 5;
    static int32_t LampFunctionIndex( char c ) {
        switch ( c ) {
        case 'L': return 0;
        case 'B': return 1;
        case 'S': return 2;
        case 'R': return 3;
        case 'W': return 4;
        default: return -1;
        }
    }
    struct Type {
        int32_t Id = 0;
        float Weight = 0.0f;
        float Length = 4.0f;  // metres (DBCarPhysicsData _length)
        float GroundLift = 0.0f; // model pivot below the tire contact (radius - WLR_FL y)
        // Lamp sprite nodes by function (LampFunctionIndex). Visibility is
        // per draw, through the shared model's per draw hidden list (the
        // model itself is shared by every agent of the type): front lights +
        // rear position at night, the stop sprites for a braking agent.
        std::vector<int32_t> LampNodes[kLampFunctions];
        // The turn-indicator sprites (FA_W_*), by side: 0 left, 1 right.
        // Indexed separately from LampNodes[4] so the side can blink alone.
        std::vector<int32_t> IndicatorNodes[2];
        // One lamp glow anchor per FA_ sprite node (model space, collected by
        // EdenCollectLampAnchors): the bloom quad the agent's world places
        // over the lens (appendLampGlows).
        std::vector<EdenLampAnchor> LampGlows;
        // The four wheels (WLR_FL/FR/RL/RR, DISC_FL/FR/RL/RR on the models
        // that have no wheel node) and their transform in the bank: the draw
        // spins them by the distance driven.
        int32_t WheelNodes[4] = { -1, -1, -1, -1 };
        Matrix4x4f WheelBase[4];
        float WheelRadius = 0.32f;
        // The roof light bar of a police car (Gyro material + FA_G quads).
        PoliceLightBar PoliceBar;
        bool bPolice = false;
        std::unique_ptr<Bank> VehicleBank;
        std::unique_ptr<EdenModel> Model;
        // A rival (no traffic bank, Weight 0: never picked by pickType): the
        // real car of a story driver, one agent at a time.
        std::unique_ptr<AiCar> Rival;
        std::string RivalName;
    };
    struct Agent {
        int32_t Type = 0;
        int32_t Edge = 0;      // link index
        bool bForward = true;  // travelling edge.first -> edge.second
        float Distance = 0.0f; // metres along the curve in the direction of travel
        float Speed = 0.0f;
        float CruiseSpeed = 14.0f;
        float LostGround = 0.0f;
        float Yaw = 0.0f;
        float YawRate = 0.0f;  // rad/s the steering asked for this frame (-logtraffic)
        float WheelSpin = 0.0f; // wheel rotation of the type's wheel nodes (rad)
        float Stun = 0.0f; // stopped after a collision (s)
        float NearMissCooldown = 0.0f;
        float BrakeHold = 0.0f; // the brake lamps stay lit this long after a decel (s)
        uint32_t AudioId = 0;   // stable id of the engine voice (TrafficAudio)
        float HornCooldown = 0.0f; // s until this car may honk again
        bool bVisible = false; // World is valid (within the draw distance)
        float LastY = 0.0f;
        // The link chosen at the node ahead, before the car reaches it (the
        // turn speed and the indicator need it in time).
        int32_t NextEdge = -1;
        bool bNextForward = true;
        float NextTurnAngle = 0.0f; // rad between the two directions at the node
        int32_t IndicatorSide = 0;  // -1 left, 1 right, 0 none
        float IndicatorPhase = 0.0f;
        float IndicatorHold = 0.0f; // seconds the blinker stays on after the turn
        Vector3f Position;
        Matrix4x4f World;
    };

    int32_t pickType();
    // forcedType >= 0: spawn that type (a rival) instead of a picked one.
    bool spawn( const Vector3f &player, const CinematicCar::SurfaceFunction &surface, int32_t forcedType = -1 );
    void loadRivals();
    void tickRivals( float deltaTime, const Vector3f &player, const CinematicCar::SurfaceFunction &surface );
    int32_t rivalTypes = 0;     // the rival types at the end of `types`
    float rivalTimer = 0.0f;    // s until the next rival may appear
    CinematicCar::SurfaceFunction surfaceFn; // the last tick's (the rivals' draw)
    // Position and heading on the curve of a link, distance along the direction of travel.
    void sampleEdge( int32_t edge, bool bForward, float distance, float &outX, float &outY, float &outZ, float &outTx, float &outTz ) const;
    Vector3f lanePosition( const Agent &agent, float &outYaw ) const;
    // A point of the right-hand lane 'ahead' metres in front of the agent's
    // progress, crossing the node into the next link when needed (the tangent
    // and the lateral offset are blended over the crossing so the target does
    // not jump at a junction). The steering target of the agent.
    void pathAhead( const Agent &agent, float ahead, Vector3f &outPoint, float &outYaw ) const;
    float targetCount() const;
    // Connected components of the road graph: a car only lives on the
    // component of the player (the island the player is on), so no traffic
    // runs on an island (or an islet) the player is not on.
    void buildComponents();
    void updatePlayerComponent( const Vector3f &player );
    // Keeps the traffic cars out of each other (circle separation along the
    // contact normal, an impulse on the two speeds): they bump instead of
    // clipping through one another.
    void resolveAgentCollisions();
    // The traffic glass: the DETAIL2 slot of the retail traffic glass carries
    // the damaged-glass art (common_car glass_b, a starred / cracked pane).
    // Bound to the black glass base G_I the player car's own glass uses, the
    // base lerp lands on the clean dark glass of the retail look.
    void neutraliseGlassArt();

    // Chooses the link the car takes at the end of its current one (stored on
    // the agent). Called once per approach: prefers the straightest option,
    // never a U-turn while another link exists, and records the turn angle
    // and the indicator side.
    void chooseNextEdge( Agent &agent );
    // Junction signals: a node with 3 or more links shows green to one link at a
    // time (8 s green, 2 s all red, per node phase) [INF: the retail lights are
    // not decoded; the game has no signal data in the road map].
    bool isRed( int32_t node, int32_t edge ) const;
    float clock = 0.0f;
    float gyroClock = 0.0f; // the police light bar sweep (s)

    bool bLoaded = false;
    float hour = 12.0f;
    std::vector<float> density; // frequency at hour 0, 2, ... 24 of the zone in use
    std::vector<Type> types;
    std::vector<Vector3f> nodes;
    struct EdgeShape {
        std::vector<Vector3f> Points;
        std::vector<float> Cumulative; // XZ length up to each point
        float Length = 0.0f;
    };
    std::vector<EdgeShape> shapes;
    std::vector<std::vector<int32_t>> adjacency; // node -> link indices
    std::vector<std::pair<int32_t, int32_t>> edges;
    std::vector<Agent> agents;
    std::vector<HornEvent> hornEvents;
    uint32_t nextAudioId = 1;
    std::mt19937 random;
    float spawnTimer = 0.0f;
    Vector3f parkForward;
    bool bEnvironmentAssigned = false;
    std::vector<Vector3f> extraVehicles; // the cop cars this frame
    int32_t policeCarId = 0;             // aiconfig POLICE_CAR_ID (3009 on Ibiza)
    bool bPoliceLights = false;          // the roof bars on (a chase / -policebar)
    std::vector<int32_t> component;      // node -> road graph component
    size_t roadComponentCount = 0;       // components of the road graph (log / spawn filter)
    int32_t playerComponent = -1;        // the component the player drives on
    float componentTimer = 0.0f;         // s until the player's component is looked up again
    Vector3f playerHeading;              // spawn bias (setPlayerHeading)
    float viewRadius = 620.0f;           // cars drawn inside this radius (m)
    float despawnRadius = 820.0f;        // removed only beyond it (and never while drawn)
    int32_t contactCount = 0;            // car-to-car contacts since the last log line
    int32_t removedDespawn = 0;          // removals since the last log line, by reason
    int32_t removedGround = 0;
    int32_t removedComponent = 0;
};
