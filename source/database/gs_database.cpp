#include "shared.h"
#include "gs_database.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include "imgui.h"

GSDatabase *gpDatabase = nullptr;

GSDatabase::GSDatabase() : bLoaded( false ) { gpDatabase = this; }

GSDatabase::~GSDatabase() { gpDatabase = nullptr; }

bool GSDatabase::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );

    const std::string folder = gpFile->getBankFolder() + "Database/";
    if ( !LoadXmbFromFile( ( folder + "db_data.cpr" ).c_str(), database ) ) {
        OTDU_LOG_WARN( "Failed to load the game database (db_data.cpr)\n" );
    } else {
        bLoaded = true;
        OTDU_LOG_INFO( "Database loaded: %u car physics entries\n",
                       database.getRoot().getArraySize( "DBCarPhysicsData" ) );
    }

    std::string language = gpConfig != nullptr ? gpConfig->getLanguage() : "us";
    std::string localizationPath = folder + "db_" + language + ".cpr";
    for ( const char *pFallback : { "us", "fr", "ge", "sp", "it" } ) {
        if ( gpFile->fileExists( localizationPath.c_str() ) ) {
            break;
        }
        localizationPath = folder + "db_" + pFallback + ".cpr";
    }
    if ( LoadXmbFromFile( localizationPath.c_str(), localization ) ) {
        XmbValue root = localization.getRoot();
        const uint32_t numLines = root.getArraySize( "db_data" );
        for ( uint32_t i = 0; i < numLines; i++ ) {
            XmbValue line = root.getArrayElement( "db_data", i );
            strings[static_cast<int32_t>( line["HashCode"].asInt() )] = line["Text"].asString();
        }
        OTDU_LOG_INFO( "Localization '%s': %zu strings\n", localizationPath.c_str(),
                       strings.size() );
        // -dumploc <substring>: dump the matching entries (id = the db_data
        // line id used by LocStr / the flash /:m_* variables). "*": all.
        if ( const char *pDumpLoc = CmdLine::GetArgValue( "-dumploc" ) ) {
            const bool bAll = std::string( pDumpLoc ) == "*";
            for ( const auto &entry : strings ) {
                if ( bAll || entry.second.find( pDumpLoc ) != std::string::npos ) {
                    OTDU_LOG_INFO( "LOC %d = '%s'\n", entry.first, entry.second.c_str() );
                }
            }
        }
    } else {
        OTDU_LOG_WARN( "Failed to load localization '%s'\n", localizationPath.c_str() );
    }
    if ( bLoaded ) {
        dumpTables();
    }
    return true;
}

// -dumpdbtables: every table of db_data.cpr with its row count and columns.
// -dumpdbtable <name>: every row of one table, column = value (the research
// hook of docs/re/opponents_spec.md; strings and numbers as stored).
void GSDatabase::dumpTables() const {
    const char *pTable = CmdLine::GetArgValue( "-dumpdbtable" );
    const bool bList = CmdLine::HasArg( "-dumpdbtables" );
    const char *pColumns = CmdLine::GetArgValue( "-dumpdbcols" );
    if ( pTable == nullptr && !bList ) {
        return;
    }
    XmbValue root = database.getRoot();
    const XmbType *pRootType = root.getType();
    if ( pRootType == nullptr ) {
        return;
    }
    const auto valueText = [this]( const XmbValue &value ) -> std::string {
        const XmbType *pType = value.getType();
        if ( pType == nullptr || pType->bComposite ) {
            return "{}";
        }
        switch ( pType->Primitive ) {
        case eXmbPrimitive::XP_String: return "'" + value.asString() + "'";
        case eXmbPrimitive::XP_Float:
        case eXmbPrimitive::XP_Double: {
            char text[32];
            snprintf( text, sizeof( text ), "%g", value.asDouble() );
            return text;
        }
        case eXmbPrimitive::XP_UInt64: {
            char text[32];
            snprintf( text, sizeof( text ), "0x%llx", static_cast<unsigned long long>( value.asUInt64() ) );
            return text;
        }
        default: return std::to_string( value.asInt() );
        }
    };
    for ( const XmbType::Field &table : pRootType->Fields ) {
        if ( !table.bArray ) {
            continue;
        }
        const uint32_t rows = root.getArraySize( table.Name.c_str() );
        if ( bList ) {
            std::string columns;
            if ( rows > 0 ) {
                const XmbType *pRowType = root.getArrayElement( table.Name.c_str(), 0 ).getType();
                if ( pRowType != nullptr ) {
                    for ( const XmbType::Field &column : pRowType->Fields ) {
                        columns += " " + column.Name;
                    }
                }
            }
            OTDU_LOG_INFO( "DBTABLE %s rows %u:%s\n", table.Name.c_str(), rows, columns.c_str() );
        }
        if ( pTable == nullptr || table.Name != pTable ) {
            continue;
        }
        for ( uint32_t i = 0; i < rows; i++ ) {
            const XmbValue row = root.getArrayElement( table.Name.c_str(), i );
            const XmbType *pRowType = row.getType();
            if ( pRowType == nullptr ) {
                continue;
            }
            std::string line;
            for ( const XmbType::Field &column : pRowType->Fields ) {
                // -dumpdbcols a,b,c: only these columns (a wide table's line
                // is otherwise cut by the log).
                if ( pColumns != nullptr &&
                     ( "," + std::string( pColumns ) + "," ).find( "," + column.Name + "," ) == std::string::npos ) {
                    continue;
                }
                if ( column.bArray ) {
                    line += " " + column.Name + "=[" + std::to_string( row.getArraySize( column.Name.c_str() ) ) + "]";
                    continue;
                }
                std::string text = valueText( row[column.Name.c_str()] );
                // A *_hc column is a localization id: show the text beside it.
                if ( column.Name.size() > 3 && column.Name.compare( column.Name.size() - 3, 3, "_hc" ) == 0 ) {
                    if ( const char *pText = getString( static_cast<int32_t>( row[column.Name.c_str()].asInt() ) ) ) {
                        text += "(\"" + std::string( pText ) + "\")";
                    }
                }
                line += " " + column.Name + "=" + text;
            }
            OTDU_LOG_INFO( "DBROW %s[%u]%s\n", table.Name.c_str(), i, line.c_str() );
        }
    }
}

void GSDatabase::terminate() { strings.clear(); }

const char *GSDatabase::getString( int32_t hashcode ) const {
    auto it = strings.find( hashcode );
    return it != strings.end() ? it->second.c_str() : nullptr;
}

std::vector<std::string> GSDatabase::getCarFileNames() const {
    std::vector<std::string> names;
    if ( !bLoaded ) {
        return names;
    }
    XmbValue root = database.getRoot();
    const uint32_t numCars = root.getArraySize( "DBCarPhysicsData" );
    for ( uint32_t i = 0; i < numCars; i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( car["_isvalidpc"].asInt() != 0 ) {
            names.push_back( car["_car_file_name"].asString() );
        }
    }
    return names;
}

bool GSDatabase::findCarPhysics( const char *pFileName, CarPhysicsData &out ) const {
    if ( !bLoaded ) {
        return false;
    }
    XmbValue root = database.getRoot();
    const uint32_t numCars = root.getArraySize( "DBCarPhysicsData" );
    for ( uint32_t i = 0; i < numCars; i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( StringCmpI( car["_car_file_name"].asString().c_str(), pFileName ) != 0 ) {
            continue;
        }

        out.FileName = car["_car_file_name"].asString();
        out.ModelName = car["_model_name"].asString();
        out.BrandHashcode = car["_car_brand"].asUInt64();
        out.CarId = static_cast<int32_t>( car["_id_car"].asInt() );
        out.Mass = car["_mass"].asFloat( out.Mass );
        out.PowerBHP = car["_power_bhp"].asFloat( out.PowerBHP );
        out.PowerRPM = car["_power_rpm"].asFloat( out.PowerRPM );
        out.TorqueNm = car["_torque_nm"].asFloat( out.TorqueNm );
        out.TorqueRPM = car["_torque_rpm"].asFloat( out.TorqueRPM );
        out.RPMMin = car["_rpm_min"].asFloat( out.RPMMin );
        out.RPMMax = car["_rpm_max"].asFloat( out.RPMMax );
        out.RedLine = car["_red_line"].asFloat( out.RedLine );
        out.MaxSpeed = car["_max_theorical_speed"].asFloat( out.MaxSpeed );
        out.FinalDriveRatio = car["_final_drive_ratio"].asFloat( out.FinalDriveRatio );
        out.Drive = static_cast<int32_t>( car["_drive"].asInt( out.Drive ) );
        // The disabled fuel feature (docs/re/fuel_gas_station_spec.md).
        out.GasTankSize = car["_gas_tank_size"].asFloat( out.GasTankSize );
        out.GasConsMin = car["_gas_cons_min"].asFloat( out.GasConsMin );
        out.GasConsAvg = car["_gas_cons_avg"].asFloat( out.GasConsAvg );
        out.GasConsMax = car["_gas_cons_max"].asFloat( out.GasConsMax );

        const int32_t numGears = static_cast<int32_t>( car["_nb_gears"].asInt( 5 ) );
        out.GearRatios.clear();
        for ( int32_t gear = 1; gear <= Min( numGears, 11 ); gear++ ) {
            std::string field = "_gear_ratio" + std::to_string( gear );
            float ratio = car[field.c_str()].asFloat( 0.0f );
            if ( ratio > 0.0f ) {
                out.GearRatios.push_back( ratio );
            }
        }

        out.GravityCenterHeight = car["_gravity_center_height"].asFloat( 50.0f ) / 100.0f;
        out.GravityCenterLength = car["_gravity_center_length"].asFloat( 50.0f ) / 100.0f;
        out.TrackFront = car["_track_front"].asFloat( 1600.0f ) / 1000.0f;
        out.TrackRear = car["_track_back"].asFloat( 1600.0f ) / 1000.0f;
        out.Length = car["_length"].asFloat( 4500.0f ) / 1000.0f;
        out.Height = car["_height"].asFloat( 1300.0f ) / 1000.0f;
        out.TireRadius = car["_tire_radius"].asFloat( 32.0f ) / 200.0f;
        out.SuspensionLengthFront = car["_suspension_length_front"].asFloat( 100.0f ) / 1000.0f;
        out.SuspensionLengthRear = car["_suspension_lenght_rear"].asFloat( 100.0f ) / 1000.0f;
        out.SuspensionRateFront = car["_suspension_rate_front"].asFloat( out.SuspensionRateFront );
        out.SuspensionRateRear = car["_suspension_rate_rear"].asFloat( out.SuspensionRateRear );
        out.DamperRateFront = car["_damper_rate_front"].asFloat( out.DamperRateFront );
        out.DamperRateRear = car["_damper_rate_rear"].asFloat( out.DamperRateRear );
        out.AntiRollFront = car["_antirollbar_front"].asFloat( out.AntiRollFront );
        out.AntiRollRear = car["_antirollbar_rear"].asFloat( out.AntiRollRear );
        out.LateralFrictionFront = car["_max_lateral_friction_front"].asFloat( out.LateralFrictionFront );
        out.LateralFrictionRear = car["_max_lateral_friction_rear"].asFloat( out.LateralFrictionRear );
        out.LongitudinalFrictionFront = car["_max_longitudinal_friction_front"].asFloat( out.LongitudinalFrictionFront );
        out.LongitudinalFrictionRear = car["_max_longitudinal_friction_rear"].asFloat( out.LongitudinalFrictionRear );
        out.BrakePowerFront = car["_brakes_power_front"].asFloat( out.BrakePowerFront );
        out.BrakeBalance = car["_brake_balance"].asFloat( out.BrakeBalance );
        out.Cx = car["_cx"].asFloat( out.Cx );
        out.ShiftTimeUp = car["_shifttime_up"].asFloat( out.ShiftTimeUp );
        out.EngineInertia = car["_engine_inertia"].asFloat( out.EngineInertia );
        return true;
    }
    return false;
}

void GSDatabase::drawDebug() {
    if ( !ImGui::Begin( "Database" ) ) {
        ImGui::End();
        return;
    }
    ImGui::Text( "Localized strings: %zu", strings.size() );
    static char filter[64] = "";
    ImGui::InputText( "Filter", filter, sizeof( filter ) );
    if ( ImGui::BeginChild( "strings", ImVec2( 0, 300 ) ) ) {
        uint32_t shown = 0;
        for ( const auto &entry : strings ) {
            if ( filter[0] != '\0' && entry.second.find( filter ) == std::string::npos ) {
                continue;
            }
            ImGui::Text( "%6d  %s", entry.first, entry.second.c_str() );
            if ( ++shown > 500 ) {
                break;
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
