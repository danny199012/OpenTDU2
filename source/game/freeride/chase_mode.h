#pragma once

#include "filesystem/bank.h"
#include "game/cinematic/cinematic_car.h"
#include "game/freeride/police_light_bar.h"
#include "render/eden/eden_model.h"

#include <functional>
#include <memory>
#include <vector>

class EdenRenderer;
struct WeatherPreset;

// Police chase mode of the free ride (the retail "Chase Mode" HUD: the
// tailing meter "Traffic violation", the arrest countdown, IsPolice /
// IsUnderArrest [DATA: HUD.vmf]). Traffic violations build the police
// awareness; a full meter starts the chase (cop cars pursue), the arrest
// countdown runs while the cops are close; zero (or a cop holding contact)
// is the arrest (fine + outlaw rank reset, loc 5202 / 5749), breaking away
// long enough is the escape (reward + rank up, loc 5201 / 5747). The ranks
// are the retail names (loc 5885..5893); the laws behind them (fines, win
// counts of the help pages: 8 consecutive wins, 35 cop victories, loc 6683)
// are encrypted-band runtime data [ENC] — the values here are [INF] in the
// retail UI shape.
class ChaseMode {
public:
    enum class eState { Clean, Noticed, Chase, Arrested, Escaped };

    struct Config {
        float AwarenessMax = 100.0f;   // violations needed for the chase [INF]
        float AwarenessDecay = 6.0f;   // per second without a violation [INF]
        float DecayDelay = 3.0f;       // seconds after the last violation [INF]
        int32_t CopCount = 2;          // pursuit cars of a chase [INF]
        float TimerSeconds = 15.0f;    // the arrest countdown [INF]
        float TimerRange = 40.0f;      // a cop closer than this arms the timer [INF]
        float TimerSafeRange = 70.0f;  // all cops beyond this disarm it [INF]
        float EscapeRange = 260.0f;    // all cops beyond this builds the escape [INF]
        float EscapeTime = 8.0f;       // seconds of break away to escape [INF]
        float ContactTime = 1.5f;      // seconds of cop contact to arrest [INF]
        float FineBase = 500.0f;       // + rank * FinePerLevel [INF: loc 6684]
        float FinePerLevel = 750.0f;
        float EscapeReward = 1000.0f;  // + rank * 500 [INF]
    };

    // The cop ranks of DBCopCar levels (chaseconfig.cpr CopCars [DATA]):
    // the cops wear the car of their level (the port spawns the "Low" one).
    // A cop drives the island road graph like the ambient traffic (Edge /
    // bForward / Distance are its place on the link curve); Position and
    // Forward are derived from it each tick.
    struct Cop {
        std::unique_ptr<CinematicCar> car; // the player-car fallback (no traffic police bank)
        Vector3f Position;
        Vector3f Forward = Vector3f( 0.0f, 0.0f, 1.0f );
        float Speed = 0.0f;             // m/s
        bool bEnvironment = false;
        float ContactTime = 0.0f;       // seconds holding contact with the (slow) player
        float BrakeHold = 0.0f;         // stop lamps stay lit after a decel (s)
        int32_t Edge = -1;              // road link (-1: free pursuit, no graph)
        bool bForward = true;           // travelling edge.first -> edge.second
        float Distance = 0.0f;          // metres along the curve in the direction of travel
        float TurnCooldown = 0.0f;      // seconds before the next U-turn is allowed
        float LastY = 0.0f;
        Matrix4x4f World;
    };

public:
    void reset();
    void unload();

    // A traffic violation of the player (a hit; scrapes less). The game mode
    // calls this from its traffic collision handling.
    void addViolation( float strength );

    using SurfaceFunction = CinematicCar::SurfaceFunction;
    void tick( float deltaTime, const Vector3f &playerPos, float playerSpeedKmh, const SurfaceFunction &surface );

    // The cop car model of the island (chaseconfig.cpr CopCars -> the
    // DBCarPhysicsData file name); called once by the game mode on enter.
    // The drawn model is the island's traffic police bank (aiconfig.cpr
    // POLICE_CAR_ID -> islands/<island>/vehicules/traffic/<model>.bnk, the
    // same car the ambient traffic shows [DATA]); the CopCar file name is the
    // fallback when the traffic bank is missing.
    void loadCopCar( const std::string &island );
    // The island road graph the cops drive (MapScreen's nodes / links /
    // curves; the vectors must outlive this object). Without it the cops
    // pursue in a straight line (the pre-session behaviour).
    void setRoads( const std::vector<Vector3f> *pNodes, const std::vector<std::pair<int32_t, int32_t>> *pEdges,
                   const std::vector<std::vector<Vector3f>> *pShapes );

    inline eState getState() const { return state; }
    inline float getAwareness01() const { return Clamp( awareness / config.AwarenessMax, 0.0f, 1.0f ); }
    // The break-away progress of a chase (0..1 of EscapeTime with every cop
    // beyond EscapeRange): the gauge's "escape" mode fills with it.
    inline float getEscape01() const { return Clamp( escapeTime / config.EscapeTime, 0.0f, 1.0f ); }
    inline bool isTimerVisible() const { return bTimerVisible; }
    inline float getTimerSeconds() const { return timerSeconds; }
    inline bool isArrested() const { return state == eState::Arrested; }
    inline const std::vector<Cop> &getCops() const { return cops; }
    // One-shot results for the game mode (fine / reward texts, rank changes).
    bool takeArrested( float &outFine );
    bool takeEscaped( float &outReward );
    // -police: a chase starts now (test hook); -policeviol: one violation.
    void startChase();

    void draw( EdenRenderer &renderer, const WeatherPreset *pWeather );
    void assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube );

    // The cops' light contributions to the frame registries (called by the
    // game mode after traffic.tick, so cop.World is current): the lamp glow
    // quads over the police model's FA_ sprite nodes (headlights + rear
    // position day and night like the sprite gate, stop while braking) and
    // the headlight cones of up to maxCones cops (night).
    void appendLampGlows( std::vector<EdenLampGlow> &out, bool bNight, float deltaTime );
    void appendHeadlightCones( std::vector<EdenHeadlightCone> &cones, const Vector3f &playerPosition,
                               size_t maxCones ) const;

private:
    void spawnCops( const Vector3f &playerPos, const SurfaceFunction &surface );
    void despawnCops();
    // The road graph pursuit: the place on the curve of a link, the nearest
    // link to a world point, the link to take at a node toward the player.
    void sampleEdge( int32_t edge, bool bForward, float distance, Vector3f &outPos, Vector3f &outTangent ) const;
    bool nearestEdge( const Vector3f &point, int32_t &outEdge, float &outDistance, float &outGap ) const;
    void pursueOnRoads( Cop &cop, float deltaTime, const Vector3f &playerPos, const SurfaceFunction &surface );
    void placeOnLane( Cop &cop, float deltaTime, const SurfaceFunction &surface );
    void arrest( const char *pReason );

    Config config;
    // The road graph (not owned): MapScreen's nodes / links / curves.
    const std::vector<Vector3f> *pNodes = nullptr;
    const std::vector<std::pair<int32_t, int32_t>> *pEdges = nullptr;
    const std::vector<std::vector<Vector3f>> *pShapes = nullptr;
    std::vector<std::vector<int32_t>> adjacency;   // node -> link indices
    std::vector<std::vector<float>> cumulative;    // per link: XZ length up to each curve point
    std::vector<float> edgeLength;
    // The traffic police model (islands/<island>/vehicules/traffic/<police>.bnk).
    std::unique_ptr<Bank> policeBank;
    std::unique_ptr<EdenModel> policeModel;
    float policeGroundLift = 0.0f;
    float policeLength = 4.5f;
    // The traffic police model's lamp glow sprites, by function (L front,
    // B rear position, S stop, R reverse, W warning): a per-draw hide list
    // shows only the ones the cop's state calls for.
    static constexpr int32_t kLampFunctions = 5;
    std::vector<int32_t> LampNodes[kLampFunctions];
    // The same nodes as bloom anchors (EdenCollectLampAnchors): the glow
    // quads of appendLampGlows.
    std::vector<EdenLampAnchor> LampGlows;
    // The roof light bar (Gyro material + FA_G quads), sweeping on duty.
    PoliceLightBar PoliceBar;
    float gyroClock = 0.0f;
    eState state = eState::Clean;
    float awareness = 0.0f;         // violation points (0..AwarenessMax)
    float sinceViolation = 0.0f;
    float timerSeconds = 0.0f;
    bool bTimerVisible = false;
    float escapeTime = 0.0f;
    float arrestTime = 0.0f;        // Arrested: hold the state before Clean
    float escapedTime = 0.0f;
    SurfaceFunction surfaceFn;      // captured each tick, used by draw
    std::string copFileName;
    std::vector<Cop> cops;
    bool bArrestedEvent = false;
    float arrestedFine = 0.0f;
    bool bEscapedEvent = false;
    float escapedReward = 0.0f;
};
