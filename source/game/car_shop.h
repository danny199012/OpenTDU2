#pragma once

#include <string>
#include <vector>

// The car dealerships of the database [DATA]: DBCarShop rows (_libelle_hc the
// shop name, _slot_1..9 the hashcodes of DBCarPhysicsData rows, _isbuyable_n
// the buy flag of a slot) and the car rows (_price, _model_name, ...).
struct ShopCar {
    uint64_t Hashcode = 0;
    int32_t CarId = 0;
    // The car's DBCarPhysicsData row (FillCarSpec reads the stats from it).
    int32_t PhysicsRow = -1;
    std::string FileName;
    std::string Brand;
    std::string Model;
    int32_t Price = 0;
    bool bBuyable = false;
};

struct CarShop {
    std::string Name;
    // _spot_name: the spot (spots.bin building name + suffix) the shop is in.
    std::string SpotName;
    // _clothesshopbrand: the DBBrands hashcode of a clothes shop's brand (0 for a car shop).
    uint64_t ClothesBrand = 0;
    std::vector<ShopCar> Cars;
};

const std::vector<CarShop> &GetCarShops();
// The display name of a DBCarPhysicsData row (_manufacturer_name +
// _model_name [+ _version_name]); empty when the id is unknown.
std::string FindCarDisplayName( int32_t carId );
// The DBCarPhysicsData row of a car id (-1 when unknown): the traffic types
// and the CinematicCar descs carry _id_car, the stat reader wants the row.
int32_t FindCarPhysicsRow( int32_t carId );

// The retail stat gauges of the car screens (FUN_00adea90(value, min, max) =
// round((v - min) / (max - min) * 100), <= 100) [CODE + DATA .data
// 0x00f7d4f4..0x00f7d50c, 0x00edf3d8; ranges read from gm_shop's fillCarVariables].
extern const float kAccelGauge[2];    // _acceleration_0_100_kmh (lower is better)
extern const float kSpeedGauge[2];    // _max_theorical_speed (km/h)
extern const float kBrakingGauge[2];  // _braking_100_0 (m, lower is better)
extern const float kHandlingGauge[2]; // _handling (0..100)
int32_t CarStatGauge( float value, const float range[2] );

// The stats of a DBCarPhysicsData row as the retail car screens show them
// (gm_shop fillCarVariables / FUN_0099dbd0 read the same fields [CODE]).
struct CarSpec {
    float Accel = 0.0f;       // _acceleration_0_100_kmh (s)
    float MaxSpeed = 0.0f;    // _max_theorical_speed (km/h)
    float Braking = 0.0f;     // _braking_100_0 (m)
    float Handling = 0.0f;    // _handling (0..100)
    int32_t Power = 0;        // _power_bhp (bhp)
    int32_t Price = 0;        // _price ($)
    uint64_t DefaultRims = 0; // _default_rims (DBRims _hashcode)
    int32_t PowerRpm = 0;     // _power_rpm
    int32_t Torque = 0;       // _torque_nm
    int32_t TorqueRpm = 0;    // _torque_rpm
    int32_t Mass = 0;         // _mass (kg)
    int32_t Displacement = 0; // _displacement (cc)
    int32_t NbGears = 0;      // _nb_gears
    int32_t Gearbox = 0;      // _gearbox
    int32_t EngineType = 0;   // _engine_type
    int32_t Difficulty = 1;   // _difficulty clamped 1..5
    float QuarterMile = 0.0f; // _quartermile_sec (0: none)
    int32_t Width = 0;        // _width (mm)
    int32_t Length = 0;       // _length (mm)
    int32_t Height = 0;       // _height (mm)
};
// Fills the stats from the DBCarPhysicsData row (false: no database / row).
bool FillCarSpec( int32_t physicsRow, CarSpec &outSpec );
// The shop of a spots.bin record (its id is GetIdentifier64bit of the spot
// name that DBCarShop._spot_name holds, e.g. "ECD_FERR_3337I"); nullptr if none.
const CarShop *FindShopBySpotId( uint64_t spotId );
// A shop by index (or the first one with cars when the index is out of range).
const CarShop *FindCarShop( int32_t index );
