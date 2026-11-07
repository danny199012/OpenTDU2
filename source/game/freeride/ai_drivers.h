#pragma once

#include "game/cinematic/cinematic_car.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class EdenRenderer;
struct GPUTexture;
struct WeatherPreset;

// The game's AI drivers, as the database lists them (docs/re/opponents_spec.md 1) [DATA]:
//
//   DBPNJ      (240 rows)  the characters: _hashcode, _pnj_name, _configid (STO_Tess_Wintory ...).
//   DBPNJCar   (77 rows)   _pnj_id -> _car_id + _color_id: the garage of the 7 story drivers
//                          (Adam, Andrew Sullivan, Ryan Wilder, Chris Wilder, Miami Harris,
//                          Stuart Wintory, Tess Wintory), ~10 cars each, one per vehicle class.
//   DBBot      (81 rows)   named bots: _botname, _botcar, _color_id, _difficulty,
//                          _aggressiveness, _pnj (the body), _zoneindex (not decoded).
//   DBLicense  _bot_1..7   every championship's 7 opponents are the 7 story drivers (PNJ
//                          hashcodes), with _difficulty_bot_n / _aggressiveness_bot_n and the
//                          championship's _rival.
//   DBCarPhysicsData       the signature cars: rows that duplicate a base car (_duplicate) with
//                          the driver in the version name - 601 Mustang Fastback "Miami",
//                          602 Touareg "Ryan", 603 SLK 55 "Tess", 604 Q7 "Chris", 605 SLR
//                          "Stuart"; they share the base car's bank.
struct AiGarageCar {
    int32_t CarId = 0;
    std::string ColorId; // DBCarsAndColors _color_id_1 ("" = the model's own paint)
    int32_t VehicleClass = 0;
};

struct AiDriver {
    uint64_t Pnj = 0;     // DBPNJ _hashcode
    std::string Name;     // DBPNJ _pnj_name ("Tess Wintory")
    std::string ConfigId; // DBPNJ _configid ("STO_Tess_Wintory")
    std::vector<AiGarageCar> Garage;
};

struct AiBot {
    std::string Name;
    uint64_t Pnj = 0;
    int32_t CarId = 0;
    std::string ColorId;
    float Difficulty = 0.5f;
    float Aggressiveness = 0.5f;
    int32_t ZoneIndex = -1; // [DATA] value, meaning not decoded
};

// The performance of a car as the AI drives it (DBCarPhysicsData) [DATA].
struct AiCarStats {
    int32_t CarId = 0;
    std::string FileName;     // _car_file_name
    std::string DisplayName;  // _car_model + " " + _car_version
    int32_t VehicleClass = 0;
    float TopSpeed = 60.0f;   // m/s (_max_theorical_speed km/h)
    float Accel0To100 = 6.0f; // s (_acceleration_0_100_kmh)
    float Mass = 1500.0f;     // kg
    float Length = 4.5f;      // m (_length mm)
    float Width = 1.9f;       // m (_width mm)
};

// The 7 story drivers with a garage (DBPNJCar), in DBPNJ order.
const std::vector<AiDriver> &GetStoryDrivers();
const AiDriver *FindAiDriver( uint64_t pnj );
const std::vector<AiBot> &GetAiBots();
bool GetAiCarStats( int32_t carId, AiCarStats &outStats );
// A driver's car for a vehicle class (the championship's _vehiclecategory_1/2):
// the garage row of that class [INF: the retail pick is ENC; every story driver
// has one car per class], else the closest class.
const AiGarageCar *PickDriverCar( const AiDriver &driver, int32_t class1, int32_t class2 );
// The driver's signature car (a DBCarPhysicsData row whose version names the
// driver, "Miami" ...) when the garage has one, else nullptr.
const AiGarageCar *FindSignatureCar( const AiDriver &driver );

// A car driven by the AI, drawn with the real car (its rims and paint): the
// race opponents and the free ride rivals. Drawn from the bank the game ships
// for other drivers, vehicules/med/<car>_med.bnk (half-resolution maps, the
// same body; the only model LOD in the data - <car>_low.bnk is a sound bank)
// [DATA]; -ailod high pins the high bank. Far cars skip the shadow caster
// pass beyond kShadowDistance and the reflection pass [INF] (-aishadow <m>,
// -aireflect), and only the nearest kShadowBudget cast at all. The pose is set by the owner (position, yaw on the ground);
// the wheels roll by the distance
// driven and the front ones steer.
class AiCar {
public:
    bool load( int32_t carId, const std::string &colorId );
    void unload();
    inline bool isLoaded() const { return nearLevel.Car && nearLevel.Car->isLoaded(); }
    inline const AiCarStats &getStats() const { return stats; }
    inline CinematicCar *getCar() { return nearLevel.Car.get(); }
    // The camera of this frame (the LOD switch); set by the game mode before the draws.
    static void SetCameraPosition( const Vector3f &position );
    static constexpr float kShadowDistance = 60.0f;
    // Only the nearest kShadowBudget AI cars cast a shadow (each caster is a
    // whole car drawn into the 3 PSSM slices) [INF]; -aishadows <n>.
    static constexpr int32_t kShadowBudget = 3;
    AiCar() = default;
    ~AiCar();
    AiCar( const AiCar & ) = delete;
    AiCar &operator=( const AiCar & ) = delete;

    // Per frame: distance rolled this frame (m), the front wheel steer (rad,
    // positive left), braking, night (headlights).
    void animate( float deltaTime, float rolled, float steer, bool bBraking, bool bNight );
    // The world pose on the ground through the four tires (CinematicCar::fitOnGround).
    Matrix4x4f pose( const Vector3f &position, float yaw, const CinematicCar::SurfaceFunction &surface ) const;
    void assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube );
    void draw( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather );

private:
    struct Wheel {
        int32_t RimNode = -1;
        Matrix4x4f RimRest;
        Matrix4x4f TireRest;
        bool bFront = false;
    };
    struct Level {
        std::unique_ptr<CinematicCar> Car;
        std::vector<Wheel> Wheels;
        bool bEnvironment = false;
    };
    bool loadLevel( Level &level, const CinematicCar::Desc &desc, const std::string &colorId );
    void animateLevel( Level &level, float deltaTime, float steer, bool bBraking, bool bNight );
    Level nearLevel; // the drawn car (CinematicCar keeps the retail cockpit LOD: the interior within 5 m)
    AiCarStats stats;
    Vector3f lastPosition;       // where it was drawn last (the shadow budget)
    bool bRegistered = false;
    float spin = 0.0f;
    float tireRadius = 0.33f;
};
