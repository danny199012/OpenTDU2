#include "shared.h"
#include "ai_drivers.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <cmath>

namespace {
Matrix4x4f NoTranslation( Matrix4x4f m ) {
    m.m[3][0] = m.m[3][1] = m.m[3][2] = 0.0f;
    return m;
}

std::string Trimmed( std::string text ) {
    while ( !text.empty() && ( text.back() == ' ' || text.back() == '\t' ) ) {
        text.pop_back();
    }
    return text;
}

// "##" and "" both mean "no colour" in the tables [DATA].
std::string ColorOrEmpty( const std::string &color ) { return color == "##" ? std::string() : color; }
} // namespace

const std::vector<AiDriver> &GetStoryDrivers() {
    static std::vector<AiDriver> drivers;
    static bool bRead = false;
    if ( bRead || gpDatabase == nullptr ) {
        return drivers;
    }
    bRead = true;
    XmbValue root = gpDatabase->getDocument().getRoot();
    // DBPNJCar first: the drivers are the PNJs that own cars.
    std::vector<std::pair<uint64_t, AiGarageCar>> rows;
    for ( uint32_t i = 0; i < root.getArraySize( "DBPNJCar" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBPNJCar", i );
        AiGarageCar car;
        car.CarId = static_cast<int32_t>( row["_car_id"].asUInt64() );
        car.ColorId = ColorOrEmpty( row["_color_id"].asString() );
        AiCarStats stats;
        if ( GetAiCarStats( car.CarId, stats ) ) {
            car.VehicleClass = stats.VehicleClass;
        }
        rows.emplace_back( row["_pnj_id"].asUInt64(), car );
    }
    for ( uint32_t i = 0; i < root.getArraySize( "DBPNJ" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBPNJ", i );
        const uint64_t pnj = row["_hashcode"].asUInt64();
        AiDriver driver;
        for ( const auto &entry : rows ) {
            if ( entry.first == pnj ) {
                driver.Garage.push_back( entry.second );
            }
        }
        if ( driver.Garage.empty() ) {
            continue;
        }
        driver.Pnj = pnj;
        driver.Name = Trimmed( row["_pnj_name"].asString() );
        driver.ConfigId = row["_configid"].asString();
        drivers.push_back( driver );
    }
    if ( CmdLine::HasArg( "-logaidrivers" ) ) {
        for ( const AiDriver &driver : drivers ) {
            std::string garage;
            for ( const AiGarageCar &car : driver.Garage ) {
                AiCarStats stats;
                GetAiCarStats( car.CarId, stats );
                garage += " " + std::to_string( car.CarId ) + ":" + stats.FileName + "(c" + std::to_string( car.VehicleClass ) +
                          ( car.ColorId.empty() ? "" : "," + car.ColorId ) + ")";
            }
            OTDU_LOG_INFO( "AiDrivers: 0x%llx '%s' (%s), %zu cars:%s\n", static_cast<unsigned long long>( driver.Pnj ),
                           driver.Name.c_str(), driver.ConfigId.c_str(), driver.Garage.size(), garage.c_str() );
        }
    }
    OTDU_LOG_INFO( "AiDrivers: %zu story drivers with a garage (DBPNJCar)\n", drivers.size() );
    return drivers;
}

const AiDriver *FindAiDriver( uint64_t pnj ) {
    for ( const AiDriver &driver : GetStoryDrivers() ) {
        if ( driver.Pnj == pnj ) {
            return &driver;
        }
    }
    return nullptr;
}

const std::vector<AiBot> &GetAiBots() {
    static std::vector<AiBot> bots;
    static bool bRead = false;
    if ( bRead || gpDatabase == nullptr ) {
        return bots;
    }
    bRead = true;
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBBot" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBBot", i );
        AiBot bot;
        bot.Name = Trimmed( row["_botname"].asString() );
        bot.Pnj = row["_pnj"].asUInt64();
        bot.CarId = static_cast<int32_t>( row["_botcar"].asUInt64() );
        bot.ColorId = ColorOrEmpty( row["_color_id"].asString() );
        bot.Difficulty = row["_difficulty"].asFloat( 0.5f );
        bot.Aggressiveness = row["_aggressiveness"].asFloat( 0.5f );
        bot.ZoneIndex = static_cast<int32_t>( row["_zoneindex"].asInt( -1 ) );
        bots.push_back( bot );
    }
    return bots;
}

bool GetAiCarStats( int32_t carId, AiCarStats &outStats ) {
    if ( gpDatabase == nullptr ) {
        return false;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( car["_id_car"].asInt( -2 ) != carId ) {
            continue;
        }
        outStats.CarId = carId;
        outStats.FileName = car["_car_file_name"].asString();
        const std::string model = car["_car_model"].asString();
        const std::string version = car["_car_version"].asString();
        outStats.DisplayName = version.empty() || version == "##" ? model : model + " " + version;
        outStats.VehicleClass = static_cast<int32_t>( car["_vehiclecategory"].asInt() );
        outStats.TopSpeed = Max( 20.0f, car["_max_theorical_speed"].asFloat( 220.0f ) / 3.6f );
        outStats.Accel0To100 = Clamp( car["_acceleration_0_100_kmh"].asFloat( 6.0f ), 2.0f, 20.0f );
        outStats.Mass = Max( 600.0f, car["_mass"].asFloat( 1500.0f ) );
        outStats.Length = Clamp( car["_length"].asFloat( 4500.0f ) * 0.001f, 2.5f, 6.5f );
        outStats.Width = Clamp( car["_width"].asFloat( 1900.0f ) * 0.001f, 1.4f, 2.6f );
        return true;
    }
    return false;
}

const AiGarageCar *PickDriverCar( const AiDriver &driver, int32_t class1, int32_t class2 ) {
    const AiGarageCar *pBest = nullptr;
    int32_t bestScore = 1 << 30;
    for ( const AiGarageCar &car : driver.Garage ) {
        // Exact class first; then the same family (A 1..8, B 9..13, C 14..17)
        // by class distance [INF].
        const auto family = []( int32_t vehicleClass ) { return vehicleClass <= 8 ? 0 : vehicleClass <= 13 ? 1 : 2; };
        int32_t score = 1000;
        for ( const int32_t wanted : { class1, class2 } ) {
            if ( wanted <= 0 ) {
                continue;
            }
            const int32_t distance = std::abs( car.VehicleClass - wanted ) + ( family( car.VehicleClass ) != family( wanted ) ? 100 : 0 );
            score = Min( score, distance );
        }
        if ( score < bestScore ) {
            bestScore = score;
            pBest = &car;
        }
    }
    return pBest;
}

const AiGarageCar *FindSignatureCar( const AiDriver &driver ) {
    // The first word of the name ("Miami Harris" -> "Miami") quoted in the
    // car's version name (_version_name 'Fastback "Miami"') [DATA].
    const std::string first = driver.Name.substr( 0, driver.Name.find( ' ' ) );
    if ( first.empty() || gpDatabase == nullptr ) {
        return nullptr;
    }
    const std::string quoted = "\"" + first + "\"";
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( const AiGarageCar &car : driver.Garage ) {
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBCarPhysicsData", i );
            if ( row["_id_car"].asInt( -2 ) == car.CarId && row["_version_name"].asString().find( quoted ) != std::string::npos ) {
                return &car;
            }
        }
    }
    return nullptr;
}

namespace {
Vector3f gAiCameraPosition;
bool gbAiCameraKnown = false;
std::vector<const AiCar *> gAiCars; // the loaded ones (the shadow budget)
} // namespace

AiCar::~AiCar() { unload(); }

void AiCar::SetCameraPosition( const Vector3f &position ) {
    gAiCameraPosition = position;
    gbAiCameraKnown = true;
}

bool AiCar::loadLevel( Level &level, const CinematicCar::Desc &desc, const std::string &colorId ) {
    level = Level();
    level.Car = std::make_unique<CinematicCar>();
    if ( !level.Car->load( desc ) ) {
        level.Car.reset();
        return false;
    }
    if ( !colorId.empty() ) {
        level.Car->applyColor( colorId );
    }
    const EdenModel &model = level.Car->getModel();
    const std::vector<SceneNode> &nodes = model.getScene().getNodes();
    for ( size_t i = 0; i + 1 < nodes.size(); i++ ) {
        if ( nodes[i].Name.rfind( "D_RIM_", 0 ) != 0 ) {
            continue;
        }
        Wheel wheel;
        wheel.RimNode = static_cast<int32_t>( i );
        wheel.RimRest = model.getNodeTransform( wheel.RimNode );
        wheel.TireRest = model.getNodeTransform( wheel.RimNode + 1 );
        wheel.bFront = wheel.RimRest.getTranslation().z > 0.0f; // car frame: +z forward
        level.Wheels.push_back( wheel );
    }
    return true;
}

bool AiCar::load( int32_t carId, const std::string &colorId ) {
    unload();
    if ( !GetAiCarStats( carId, stats ) ) {
        OTDU_LOG_WARN( "AiCar: car %d not in the database\n", carId );
        return false;
    }
    CinematicCar::Desc desc;
    if ( !CinematicCar::DescFromDatabase( carId, desc ) ) {
        return false;
    }
    tireRadius = Max( 0.2f, desc.TireRadius );
    // The _med bank (-ailod high: the high one).
    static const bool bHigh = CmdLine::HasArg( "-ailod" ) && std::string( CmdLine::GetArgValue( "-ailod" ) ) == "high";
    CinematicCar::Desc nearDesc = desc;
    nearDesc.Lod = bHigh ? std::string() : std::string( "med" );
    if ( !loadLevel( nearLevel, nearDesc, colorId ) ) {
        return false;
    }
    gAiCars.push_back( this );
    bRegistered = true;
    OTDU_LOG_INFO( "AiCar: %s drawn from the %s bank\n", stats.FileName.c_str(),
                   nearLevel.Car->getLod().empty() ? "high" : nearLevel.Car->getLod().c_str() );
    return true;
}

void AiCar::unload() {
    if ( bRegistered ) {
        gAiCars.erase( std::remove( gAiCars.begin(), gAiCars.end(), this ), gAiCars.end() );
        bRegistered = false;
    }
    nearLevel = Level();
    spin = 0.0f;
}

void AiCar::animateLevel( Level &level, float deltaTime, float steer, bool bBraking, bool bNight ) {
    if ( !level.Car ) {
        return;
    }
    EdenModel &model = level.Car->getModel();
    for ( const Wheel &wheel : level.Wheels ) {
        const float wheelSteer = wheel.bFront ? steer : 0.0f;
        const Matrix4x4f motion = Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0, 1, 0 ), wheelSteer ) ) *
                                  Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 1, 0, 0 ), spin ) );
        model.setNodeTransform( wheel.RimNode,
                                Matrix4x4f::Translation( wheel.RimRest.getTranslation() ) * motion * NoTranslation( wheel.RimRest ) );
        model.setNodeTransform( wheel.RimNode + 1, Matrix4x4f::Translation( wheel.TireRest.getTranslation() ) * motion *
                                                       NoTranslation( wheel.TireRest ) );
    }
    level.Car->setBrakeLights( bBraking );
    level.Car->setHeadlights( bNight );
    level.Car->updateLights( deltaTime );
}

void AiCar::animate( float deltaTime, float rolled, float steer, bool bBraking, bool bNight ) {
    if ( !isLoaded() ) {
        return;
    }
    spin = std::fmod( spin + rolled / tireRadius, 2.0f * kPI );
    animateLevel( nearLevel, deltaTime, steer, bBraking, bNight );
}

Matrix4x4f AiCar::pose( const Vector3f &position, float yaw, const CinematicCar::SurfaceFunction &surface ) const {
    // fitOnGround takes an Eden (cinematic) quaternion: EdenRotationFromQuaternion
    // conjugates it, so the yaw goes in negated (forward = (sin yaw, 0, cos yaw)).
    const Quaternionf heading( 0.0f, -std::sin( yaw * 0.5f ), 0.0f, std::cos( yaw * 0.5f ) );
    return nearLevel.Car->fitOnGround( position, heading, surface );
}

void AiCar::assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube ) {
    if ( pCube == nullptr ) {
        return;
    }
    for ( Level *pLevel : { &nearLevel } ) {
        if ( pLevel->Car && !pLevel->bEnvironment ) {
            pLevel->Car->assignEnvironment( renderer, pCube );
            pLevel->bEnvironment = true;
        }
    }
}

void AiCar::draw( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather ) {
    if ( !isLoaded() ) {
        return;
    }
    // The extra passes of a far car [INF]: no shadow beyond kShadowDistance
    // (-aishadow <m>), none in the reflection pass (-aireflect keeps it).
    const bool bShadow = renderer.isShadowCasterMode();
    const bool bReflection = renderer.isReflectionPass();
    if ( bShadow || bReflection ) {
        static const float shadowDistance =
            CmdLine::HasArg( "-aishadow" ) ? static_cast<float>( atof( CmdLine::GetArgValue( "-aishadow" ) ) ) : kShadowDistance;
        static const bool bReflect = CmdLine::HasArg( "-aireflect" );
        if ( bReflection && !bReflect ) {
            return;
        }
        if ( bShadow && gbAiCameraKnown ) {
            const Vector3f delta = world.getTranslation() - gAiCameraPosition;
            const float distance2 = delta.dot( delta );
            if ( distance2 > shadowDistance * shadowDistance ) {
                return;
            }
            static const int32_t budget = Max( 0, CmdLine::HasArg( "-aishadows" ) ? CmdLine::GetArgInt( "-aishadows", kShadowBudget )
                                                                                 : kShadowBudget );
            int32_t nearer = 0;
            for ( const AiCar *pOther : gAiCars ) {
                if ( pOther != this ) {
                    const Vector3f other = pOther->lastPosition - gAiCameraPosition;
                    nearer += other.dot( other ) < distance2 ? 1 : 0;
                }
            }
            if ( nearer >= budget ) {
                return;
            }
        }
    }
    if ( !bShadow && !bReflection ) {
        lastPosition = world.getTranslation();
    }
    nearLevel.Car->draw( renderer, world, pWeather, nearLevel.bEnvironment );
}
