#pragma once

#include "core/xmb.h"
#include "game/gs_base.h"


// Car physics/description entry (DBCarPhysicsData).
struct CarPhysicsData {
    std::string FileName; // Bank name in Euro/Bnk/Vehicules/High
    std::string ModelName;
    uint64_t BrandHashcode = 0;
    int32_t CarId = 0;
    float Mass = 1500.0f;          // kg
    float PowerBHP = 300.0f;
    float PowerRPM = 6000.0f;
    float TorqueNm = 400.0f;
    float TorqueRPM = 4000.0f;
    float RPMMin = 900.0f;
    float RPMMax = 7000.0f;
    float RedLine = 6500.0f;
    float MaxSpeed = 250.0f;       // km/h
    float FinalDriveRatio = 3.5f;
    std::vector<float> GearRatios; // forward gears
    int32_t Drive = 1;             // 1: RWD, 2: FWD, 4: AWD (TDU2 values)
    float GravityCenterHeight = 0.5f;   // m (from cm in the database)
    float GravityCenterLength = 0.5f;   // front weight distribution (0..1)
    float TrackFront = 1.6f;       // m
    float TrackRear = 1.6f;
    float Length = 4.5f;
    float Height = 1.3f;
    float TireRadius = 0.32f;      // m
    float SuspensionLengthFront = 0.1f;
    float SuspensionLengthRear = 0.1f;
    float SuspensionRateFront = 80.0f;
    float SuspensionRateRear = 80.0f;
    float DamperRateFront = 8.0f;
    float DamperRateRear = 8.0f;
    float AntiRollFront = 16.0f;
    float AntiRollRear = 16.0f;
    float LateralFrictionFront = 1.8f;
    float LateralFrictionRear = 1.8f;
    float LongitudinalFrictionFront = 1.8f;
    float LongitudinalFrictionRear = 1.8f;
    float BrakePowerFront = 15000.0f;
    float BrakeBalance = 60.0f;
    float Cx = 0.32f;
    float ShiftTimeUp = 0.1f;
    float EngineInertia = 20.0f;

    // Fuel: the data of a feature the shipped game leaves disabled. Every car
    // carries its tank and its consumption in the database (DB row +252..+264,
    // between `_final_drive_ratio` and `_gear_ratio1`), and the gas station's
    // whole UI text is in the localization (ids 1635..1645: "Gas station",
    // "Shell basic", "Shell v-power", "Fill", "Bill", "Breakdown") — see
    // docs/re/fuel_gas_station_spec.md.
    float GasTankSize = 0.0f; // _gas_tank_size (litres [INF: the value reads like one])
    float GasConsMin = 0.0f;  // _gas_cons_min
    float GasConsAvg = 0.0f;  // _gas_cons_avg
    float GasConsMax = 0.0f;  // _gas_cons_max
};

// Database service: Euro/Bnk/Database/db_data.cpr (game data) and
// db_<lang>.cpr (localization). Both are encrypted XMB documents.
class GSDatabase : public GameSystem {
public:
    inline const XmbDocument &getDocument() const { return database; }

public:
    GSDatabase();
    ~GSDatabase();

    const char *getName() const override { return "Service : Database"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void terminate() override;
    void drawDebug() override;

    const char *getString( int32_t hashcode ) const;
    bool findCarPhysics( const char *pFileName, CarPhysicsData &outData ) const;
    std::vector<std::string> getCarFileNames() const;

private:
    void dumpTables() const;

    XmbDocument database;
    XmbDocument localization;
    std::unordered_map<int32_t, std::string> strings;
    bool bLoaded;
};

extern GSDatabase *gpDatabase;

// Shortcut for localized strings.
inline const char *LocStr( int32_t hashcode, const char *pFallback ) {
    if ( gpDatabase != nullptr ) {
        const char *pString = gpDatabase->getString( hashcode );
        if ( pString != nullptr ) {
            return pString;
        }
    }
    return pFallback;
}
