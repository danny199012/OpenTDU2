#include "shared.h"
#include "licenses.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "game/player_profile.h"
#include "system/cmd_line.h"

#include <algorithm>

const std::vector<LicenseInfo> &GetLicenses() {
    static std::vector<LicenseInfo> licenses;
    static bool bRead = false;
    if ( bRead || gpDatabase == nullptr ) {
        return licenses;
    }
    bRead = true;
    XmbValue root = gpDatabase->getDocument().getRoot();
    const auto locString = []( uint64_t id ) {
        const char *pText = gpDatabase->getString( static_cast<int32_t>( id ) );
        return std::string( pText != nullptr ? pText : "" );
    };
    for ( uint32_t i = 0; i < root.getArraySize( "DBLicense" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBLicense", i );
        LicenseInfo license;
        license.Hashcode = row["_hashcode"].asUInt64();
        license.Name = locString( row["_championshipname_hc"].asUInt64() );
        license.Description = locString( row["_championshipdesc_hc"].asUInt64() );
        license.bChampionship = row["_isachampionship"].asBool();
        license.Island = static_cast<int32_t>( row["_island"].asInt() );
        license.Order = static_cast<int32_t>( row["_order"].asInt() );
        license.PlayerLevel = static_cast<int32_t>( row["_playerlevel"].asInt() );
        license.VehicleCategory[0] = static_cast<int32_t>( row["_vehiclecategory_1"].asInt() );
        license.VehicleCategory[1] = static_cast<int32_t>( row["_vehiclecategory_2"].asInt() );
        license.RaceMoney = static_cast<int32_t>( row["_racemoneyreward"].asInt() );
        license.ChampMoney = static_cast<int32_t>( row["_champmoneyreward"].asInt() );
        for ( int32_t n = 1; n <= 8; n++ ) {
            const std::string file = row[( "_challengefile_" + std::to_string( n ) ).c_str()].asString();
            if ( !file.empty() ) {
                license.Challenges.push_back( file );
            }
        }
        for ( int32_t n = 0; n < 7; n++ ) {
            const std::string index = std::to_string( n + 1 );
            license.Bots[n] = row[( "_bot_" + index ).c_str()].asUInt64();
            license.BotDifficulty[n] = row[( "_difficulty_bot_" + index ).c_str()].asFloat( 0.5f );
            license.BotAggressiveness[n] = row[( "_aggressiveness_bot_" + index ).c_str()].asFloat( 0.5f );
        }
        license.Rival = row["_rival"].asUInt64();
        // "##" marks an unused slot [DATA].
        license.Challenges.erase( std::remove( license.Challenges.begin(), license.Challenges.end(), std::string( "##" ) ),
                                  license.Challenges.end() );
        licenses.push_back( license );
        if ( CmdLine::HasArg( "-loglicenses" ) ) {
            std::string files;
            for ( const std::string &file : license.Challenges ) {
                files += " " + file;
            }
            OTDU_LOG_INFO( "DBLicense[%u] hc 0x%llx %s island %d order %d level %d classes %s/%s race $%d champ $%d '%s' (%s):%s\n", i,
                           static_cast<unsigned long long>( license.Hashcode ), license.bChampionship ? "CHAMP" : "licence",
                           license.Island, license.Order, license.PlayerLevel, VehicleClassName( license.VehicleCategory[0] ),
                           VehicleClassName( license.VehicleCategory[1] ), license.RaceMoney, license.ChampMoney, license.Name.c_str(),
                           license.Description.c_str(), files.c_str() );
        }
    }
    return licenses;
}

std::vector<const LicenseInfo *> GetSchoolLicenses( char schoolLetter, int32_t island ) {
    std::vector<const LicenseInfo *> result;
    const std::string prefix = std::string( "RS" ) + schoolLetter;
    for ( const LicenseInfo &license : GetLicenses() ) {
        if ( !license.bChampionship && license.Island == island && !license.Challenges.empty() &&
             license.Challenges[0].compare( 0, prefix.size(), prefix ) == 0 ) {
            result.push_back( &license );
        }
    }
    std::stable_sort( result.begin(), result.end(),
                      []( const LicenseInfo *a, const LicenseInfo *b ) { return a->Order < b->Order; } );
    return result;
}

std::string LicenseClasses( const LicenseInfo &license ) {
    std::string classes;
    for ( const std::string &test : license.Challenges ) {
        // RS + class (letter + digit) + T + number.
        if ( test.size() < 5 ) {
            continue;
        }
        const std::string cls = test.substr( 2, 2 );
        if ( classes.find( cls ) == std::string::npos ) {
            classes += ( classes.empty() ? "" : " / " ) + cls;
        }
    }
    return classes;
}

const char *VehicleClassName( int32_t category ) {
    static const char *const kNames[] = { "",    "A1",  "A2",  "A3", "A4", "A5", "A6", "A7", "A8", "B1",
                                          "B2",  "B3",  "B4",  "B5", "C1", "C2", "C3", "C4", "Ma1", "Ma2",
                                          "Mb1", "Mb2", "MC",  "R1", "R2", "R3", "R4", "R5", "R6" };
    return category >= 0 && category < static_cast<int32_t>( sizeof( kNames ) / sizeof( kNames[0] ) ) ? kNames[category] : "";
}

bool IsTestPassed( const std::string &test ) {
    const PlayerProfile &profile = GetPlayerProfile();
    return profile.PassedTests.find( test ) != profile.PassedTests.end();
}

bool IsLicenseObtained( const LicenseInfo &license ) {
    for ( const std::string &test : license.Challenges ) {
        if ( !IsTestPassed( test ) ) {
            return false;
        }
    }
    return !license.Challenges.empty();
}

std::vector<const LicenseInfo *> GetChampionships( int32_t island ) {
    std::vector<const LicenseInfo *> result;
    for ( const LicenseInfo &license : GetLicenses() ) {
        if ( license.bChampionship && license.Island == island && !license.Challenges.empty() ) {
            result.push_back( &license );
        }
    }
    return result;
}

std::string ChampionshipFamily( const LicenseInfo &championship ) {
    const int32_t category = championship.VehicleCategory[0];
    std::string family = "cup";
    if ( category >= 1 && category <= 8 ) {
        family = "asphalt";
    } else if ( category >= 9 && category <= 13 ) {
        family = "dirt";
    } else if ( category >= 14 && category <= 17 ) {
        family = "classic";
    }
    return championship.Order >= 2 ? family + "_h" : family;
}

char RaceModeLetter( const std::string &race ) {
    return race.empty() ? '?' : static_cast<char>( toupper( static_cast<unsigned char>( race.back() ) ) );
}

const char *RaceModeName( const std::string &race ) {
    switch ( RaceModeLetter( race ) ) {
    case 'T': return "Time attack";
    case 'R': return "Race";
    case 'A': return "Speedtrap";
    case 'S': return "Speed";
    default: return "Challenge";
    }
}

bool IsRaceRunnable( const std::string &race ) { return RaceModeLetter( race ) != 'S'; }

bool IsChampionshipComplete( const LicenseInfo &championship ) {
    bool bAny = false;
    for ( const std::string &race : championship.Challenges ) {
        if ( !IsRaceRunnable( race ) ) {
            continue;
        }
        bAny = true;
        if ( !IsTestPassed( race ) ) {
            return false;
        }
    }
    return bAny;
}

bool IsChampionshipOpen( const LicenseInfo &championship, std::string *pOutReason ) {
    static const bool bAll = CmdLine::HasArg( "-allchamps" );
    if ( bAll ) {
        return true;
    }
    const int32_t category = championship.VehicleCategory[0];
    if ( category == 0 ) {
        for ( const LicenseInfo *pOther : GetChampionships( championship.Island ) ) {
            if ( pOther->VehicleCategory[0] != 0 && !IsChampionshipComplete( *pOther ) ) {
                if ( pOutReason != nullptr ) {
                    *pOutReason = "Complete every championship of this island first";
                }
                return false;
            }
        }
        return true;
    }
    bool bLicensed = false;
    for ( const LicenseInfo &license : GetLicenses() ) {
        if ( !license.bChampionship && ( license.VehicleCategory[0] == category || license.VehicleCategory[1] == category ) &&
             IsLicenseObtained( license ) ) {
            bLicensed = true;
        }
    }
    if ( !bLicensed ) {
        if ( pOutReason != nullptr ) {
            *pOutReason = std::string( "You need the " ) + VehicleClassName( category ) + " licence";
        }
        return false;
    }
    if ( championship.Order >= 2 ) {
        for ( const LicenseInfo *pOther : GetChampionships( championship.Island ) ) {
            if ( pOther->VehicleCategory[0] == category && pOther->Order < championship.Order && !IsChampionshipComplete( *pOther ) ) {
                if ( pOutReason != nullptr ) {
                    *pOutReason = "Complete " + pOther->Name + " first";
                }
                return false;
            }
        }
    }
    return true;
}
