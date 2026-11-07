#include "shared.h"
#include "car_shop.h"

#include <cmath>
#include <map>

#include "core/xmb.h"
#include "database/gs_database.h"
#include "system/cmd_line.h"

const std::vector<CarShop> &GetCarShops() {
    static std::vector<CarShop> shops;
    static bool bRead = false;
    if ( bRead || gpDatabase == nullptr ) {
        return shops;
    }
    bRead = true;
    XmbValue root = gpDatabase->getDocument().getRoot();
    if ( CmdLine::HasArg( "-logshops" ) ) {
        // The sticker catalog (the decal banks of the ESTICK_* shops) and the
        // per-action prices (tuning / paint): logged, not applied yet
        // [DATA: the tables; the shop code that reads them is ENC].
        for ( uint32_t i = 0; i < root.getArraySize( "DBStickersBanks" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBStickersBanks", i );
            const char *pName = gpDatabase->getString( static_cast<int32_t>( row["_name_hc"].asUInt64() ) );
            OTDU_LOG_INFO( "DBStickersBanks[%u] bank '%s' name '%s' type %d price $%d mono %d\n", i,
                           row["_bankfile"].asString().c_str(), pName != nullptr ? pName : "", row["_type"].asInt(),
                           row["_price"].asInt(), row["_monochrome"].asBool() ? 1 : 0 );
        }
        for ( uint32_t i = 0; i < root.getArraySize( "DBStickersPricing" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBStickersPricing", i );
            OTDU_LOG_INFO( "DBStickersPricing[%u] action '%s' price $%d\n", i, row["_actionname"].asString().c_str(),
                           row["_price"].asInt() );
        }
    }
    // Cars by hashcode.
    struct CarRow {
        int32_t CarId;
        int32_t PhysicsRow;
        std::string FileName, Model, Brand;
        int32_t Price;
    };
    std::map<uint64_t, CarRow> cars;
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        CarRow row;
        row.CarId = static_cast<int32_t>( car["_id_car"].asInt() );
        row.PhysicsRow = static_cast<int32_t>( i );
        row.FileName = car["_car_file_name"].asString();
        // The names carry "##" markers (a placeholder for the localized suffix) and no
        // separator between the model and its version.
        auto clean = []( std::string text ) {
            for ( size_t at = text.find( "##" ); at != std::string::npos; at = text.find( "##" ) ) {
                text.erase( at, 2 );
            }
            while ( !text.empty() && text.back() == ' ' ) {
                text.pop_back();
            }
            return text;
        };
        const std::string version = clean( car["_version_name"].asString() );
        row.Model = clean( car["_model_name"].asString() ) + ( version.empty() ? std::string() : " " + version );
        row.Brand = clean( car["_manufacturer_name"].asString() );
        row.Price = static_cast<int32_t>( car["_price"].asInt() );
        // The shop slots hold the _id_car of the car row [DATA: 371, 380, 355 ...].
        cars[static_cast<uint64_t>( row.CarId )] = row;
    }
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarShop" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBCarShop", i );
        if ( CmdLine::HasArg( "-logshops" ) ) {
            const char *pLabel = gpDatabase->getString( static_cast<int32_t>( row["_libelle_hc"].asUInt64() ) );
            OTDU_LOG_INFO( "DBCarShop[%u] valid %d spot '%s' clothesbrand 0x%llx label '%s'\n", i,
                           row["_isvalid"].asBool() ? 1 : 0, row["_spot_name"].asString().c_str(),
                           static_cast<unsigned long long>( row["_clothesshopbrand"].asUInt64() ),
                           pLabel != nullptr ? pLabel : "" );
        }
        if ( !row["_isvalid"].asBool() ) {
            continue;
        }
        CarShop shop;
        const char *pName = gpDatabase->getString( static_cast<int32_t>( row["_libelle_hc"].asUInt64() ) );
        shop.Name = pName != nullptr ? pName : "";
        shop.SpotName = row["_spot_name"].asString();
        shop.ClothesBrand = row["_clothesshopbrand"].asUInt64();
        for ( int32_t slot = 1; slot <= 9; slot++ ) {
            const uint64_t hashcode = row[( "_slot_" + std::to_string( slot ) ).c_str()].asUInt64();
            const auto found = cars.find( hashcode );
            if ( hashcode != 0 && found == cars.end() && CmdLine::HasArg( "-logshops" ) ) {
                OTDU_LOG_INFO( "   slot %d: car %llu not in DBCarPhysicsData\n", slot,
                               static_cast<unsigned long long>( hashcode ) );
            }
            if ( hashcode == 0 || found == cars.end() ) {
                continue;
            }
            ShopCar car;
            car.Hashcode = hashcode;
            car.CarId = found->second.CarId;
            car.PhysicsRow = found->second.PhysicsRow;
            car.FileName = found->second.FileName;
            car.Brand = found->second.Brand;
            car.Model = found->second.Model;
            car.Price = found->second.Price;
            car.bBuyable = row[( "_isbuyable_" + std::to_string( slot ) ).c_str()].asBool();
            shop.Cars.push_back( car );
        }
        shops.push_back( shop );
        if ( CmdLine::HasArg( "-logshops" ) ) {
            OTDU_LOG_INFO( "Shop %zu '%s': %zu cars\n", shops.size() - 1, shop.Name.c_str(), shop.Cars.size() );
            for ( const ShopCar &car : shop.Cars ) {
                OTDU_LOG_INFO( "   %s %s '%s' $%d %s\n", car.Brand.c_str(), car.Model.c_str(), car.FileName.c_str(), car.Price,
                               car.bBuyable ? "buyable" : "" );
            }
        }
    }
    return shops;
}

std::string FindCarDisplayName( int32_t carId ) {
    if ( gpDatabase == nullptr ) {
        return std::string();
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( static_cast<int32_t>( car["_id_car"].asInt() ) != carId ) {
            continue;
        }
        // The names carry "##" markers (a placeholder for the localized
        // suffix) and no separator between the model and its version.
        auto clean = []( std::string text ) {
            for ( size_t at = text.find( "##" ); at != std::string::npos; at = text.find( "##" ) ) {
                text.erase( at, 2 );
            }
            while ( !text.empty() && text.back() == ' ' ) {
                text.pop_back();
            }
            return text;
        };
        const std::string version = clean( car["_version_name"].asString() );
        const std::string model = clean( car["_model_name"].asString() ) +
                                  ( version.empty() ? std::string() : " " + version );
        return clean( car["_manufacturer_name"].asString() ) + ( model.empty() ? std::string() : " " + model );
    }
    return std::string();
}

int32_t FindCarPhysicsRow( int32_t carId ) {
    if ( gpDatabase == nullptr ) {
        return -1;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( static_cast<int32_t>( car["_id_car"].asInt() ) == carId ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

// The gauge ranges (gm_shop's fillCarVariables fed the same to SHOPSCAR).
const float kAccelGauge[2] = { 35.0f, 2.25f };    // _acceleration_0_100_kmh (inverted: lower = better)
const float kSpeedGauge[2] = { 70.0f, 470.0f };   // _max_theorical_speed
const float kBrakingGauge[2] = { 170.0f, 96.0f }; // _braking_100_0 (inverted)
const float kHandlingGauge[2] = { 0.0f, 100.0f }; // _handling

int32_t CarStatGauge( float value, const float range[2] ) {
    // FUN_00adea90 [CODE].
    const int32_t percent =
        static_cast<int32_t>( std::lround( ( value - range[0] ) / ( range[1] - range[0] ) * 100.0f ) );
    return static_cast<int32_t>( Min( static_cast<uint32_t>( percent ), 100u ) );
}

bool FillCarSpec( int32_t physicsRow, CarSpec &outSpec ) {
    if ( gpDatabase == nullptr || physicsRow < 0 ||
         physicsRow >= static_cast<int32_t>( gpDatabase->getDocument().getRoot().getArraySize( "DBCarPhysicsData" ) ) ) {
        return false;
    }
    XmbValue car = gpDatabase->getDocument().getRoot().getArrayElement( "DBCarPhysicsData",
                                                                        static_cast<uint32_t>( physicsRow ) );
    outSpec.Accel = car["_acceleration_0_100_kmh"].asFloat();
    outSpec.MaxSpeed = car["_max_theorical_speed"].asFloat();
    outSpec.Braking = car["_braking_100_0"].asFloat();
    outSpec.Handling = static_cast<float>( car["_handling"].asInt() );
    outSpec.Power = static_cast<int32_t>( car["_power_bhp"].asInt() );
    outSpec.Price = static_cast<int32_t>( car["_price"].asInt() );
    outSpec.DefaultRims = car["_default_rims"].asUInt64();
    outSpec.PowerRpm = static_cast<int32_t>( car["_power_rpm"].asInt() );
    outSpec.Torque = static_cast<int32_t>( car["_torque_nm"].asInt() );
    outSpec.TorqueRpm = static_cast<int32_t>( car["_torque_rpm"].asInt() );
    outSpec.Mass = static_cast<int32_t>( car["_mass"].asInt() );
    outSpec.Displacement = static_cast<int32_t>( car["_displacement"].asInt() );
    outSpec.NbGears = static_cast<int32_t>( car["_nb_gears"].asInt() );
    outSpec.Gearbox = static_cast<int32_t>( car["_gearbox"].asInt() );
    outSpec.EngineType = static_cast<int32_t>( car["_engine_type"].asInt() );
    outSpec.Difficulty = Clamp( static_cast<int32_t>( car["_difficulty"].asInt() ), 1, 5 );
    outSpec.QuarterMile = car["_quartermile_sec"].asFloat();
    outSpec.Width = static_cast<int32_t>( car["_width"].asInt() );
    outSpec.Length = static_cast<int32_t>( car["_length"].asInt() );
    outSpec.Height = static_cast<int32_t>( car["_height"].asInt() );
    return true;
}

const CarShop *FindShopBySpotId( uint64_t spotId ) {    if ( spotId == 0 ) {
        return nullptr;
    }
    for ( const CarShop &shop : GetCarShops() ) {
        if ( !shop.SpotName.empty() && GetIdentifier64bit( shop.SpotName.c_str() ) == spotId ) {
            return &shop;
        }
    }
    return nullptr;
}

const CarShop *FindCarShop( int32_t index ) {
    const std::vector<CarShop> &shops = GetCarShops();
    if ( index >= 0 && static_cast<size_t>( index ) < shops.size() && !shops[index].Cars.empty() ) {
        return &shops[index];
    }
    for ( const CarShop &shop : shops ) {
        if ( !shop.Cars.empty() ) {
            return &shop;
        }
    }
    return nullptr;
}
