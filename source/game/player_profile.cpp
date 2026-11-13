#include "shared.h"
#include "player_profile.h"

#include "filesystem/gs_file.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace {
    // Real time, not frame time: the notice must run while the game is paused.
    uint64_t noticeUntilMs = 0;

    std::filesystem::path ProfilePath() {
        return gpFile != nullptr ? gpFile->getUserDataFolder() / "profile.sav"
                                 : std::filesystem::path( "profile.sav" );
    }
} // namespace

PlayerProfile &GetPlayerProfile() {
    static PlayerProfile profile;
    return profile;
}

CineRequest &GetCineRequest() {
    static CineRequest request;
    return request;
}

TestDriveRequest &GetTestDriveRequest() {
    static TestDriveRequest request;
    return request;
}

FreeRideShopRequest &GetFreeRideShopRequest() {
    static FreeRideShopRequest request;
    return request;
}

void ProfileAddMoney( int32_t amount, bool bPending, int32_t reason ) {
    // FUN_00abe0e0 [CODE]: statistics +0xf05c[reason] += amount (reason <
    // 0x1c); bPending 0: money = max(money + amount, 0); bPending 1:
    // pending += amount. The millionaire checks are not implemented.
    PlayerProfile &profile = GetPlayerProfile();
    if ( reason >= 0 && reason < 28 ) {
        profile.MoneyEarned[reason] += amount;
    }
    if ( bPending ) {
        profile.PendingMoney += amount;
    } else {
        profile.Money = std::max( profile.Money + amount, 0 );
    }
}

void ProfileStartMoneyTransfer() {
    // FUN_00abd0d0: the HUD "money" element is shown with a 1.0 s timer [CODE].
    GetPlayerProfile().MoneyTransferTimer = 1.0f;
}

void ProfileTickMoney( float deltaTime ) {
    // FUN_00abcf60(money object, dt) [CODE].
    PlayerProfile &profile = GetPlayerProfile();
    if ( profile.MoneyTransferTimer < 0.0f ) {
        return;
    }
    int32_t rate = 0;
    if ( profile.MoneyTransferTimer != 0.0f ) {
        rate = static_cast<int32_t>( std::lround( static_cast<float>( profile.PendingMoney ) / profile.MoneyTransferTimer ) );
    }
    int32_t step = static_cast<int32_t>( std::lround( static_cast<float>( rate ) * deltaTime ) );
    if ( static_cast<float>( profile.PendingMoney ) >= 0.0f ) {
        step = std::max( std::min( step, profile.PendingMoney ), 0 );
    } else {
        step = std::max( profile.PendingMoney, std::min( step, 0 ) );
    }
    profile.Money = std::max( profile.Money + step, 0 );
    profile.PendingMoney -= step;
    profile.MoneyTransferTimer -= deltaTime;
    if ( profile.MoneyTransferTimer < 0.0f ) {
        profile.Money = std::max( profile.Money + profile.PendingMoney, 0 );
        profile.PendingMoney = 0;
        profile.MoneyTransferTimer = -1.0f;
    }
}

std::string GetStoryAvatarConfig( const std::string &introConfig ) {
    // The avatar choice configurations are AVT_<sex>[_<ethnic>]_Intro: the same
    // sex and ethnic group without "_Intro" (FUN_00b105d0 builds the name from
    // the profile sex and avatar index) [CODE + DATA].
    const std::string suffix = "_Intro";
    if ( introConfig.size() > suffix.size() &&
         introConfig.compare( introConfig.size() - suffix.size(), suffix.size(), suffix ) == 0 ) {
        return introConfig.substr( 0, introConfig.size() - suffix.size() );
    }
    return introConfig.empty() ? std::string( "AVT_Male" ) : introConfig;
}

void LoadProfile() {
    std::ifstream file( ProfilePath() );
    if ( !file ) {
        return;
    }
    PlayerProfile &profile = GetPlayerProfile();
    std::string line;
    while ( std::getline( file, line ) ) {
        const size_t equal = line.find( '=' );
        if ( equal == std::string::npos ) {
            continue;
        }
        std::string key = line.substr( 0, equal );
        key.erase( key.find_last_not_of( " \t" ) + 1 );
        std::string value = line.substr( equal + 1 );
        value.erase( 0, value.find_first_not_of( " \t" ) );
        while ( !value.empty() &&
                ( value.back() == '\r' || value.back() == ' ' ) ) {
            value.pop_back();
        }
        const int32_t number = atoi( value.c_str() );
        if ( key == "avatar_config" ) {
            profile.AvatarConfig = value;
        } else if ( key == "name" ) {
            profile.Name = value;
        } else if ( key == "story_step" ) {
            profile.StoryStep = number;
        } else if ( key == "money" ) {
            profile.Money = number;
        } else if ( key == "cars" ) {
            profile.NbCars = number;
        } else if ( key == "houses" ) {
            profile.NbHouses = number;
        } else if ( key == "points_racing" ) {
            profile.Points[0] = number;
        } else if ( key == "points_cruising" ) {
            profile.Points[1] = number;
        } else if ( key == "points_collection" ) {
            profile.Points[2] = number;
        } else if ( key == "points_social" ) {
            profile.Points[3] = number;
        } else if ( key == "cars_owned" ) {
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText ) {
                    break;
                }
                profile.OwnedCars.push_back( static_cast<int32_t>( id ) );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "car_paints" ) {
            // "<id_car>:<paint row>,..."
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText || *pEnd != ':' ) {
                    break;
                }
                const char *pPaint = pEnd + 1;
                const long paint = strtol( pPaint, &pEnd, 10 );
                if ( pEnd == pPaint ) {
                    break;
                }
                profile.OwnedCarPaints[static_cast<int32_t>( id )] = static_cast<int32_t>( paint );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "car_rims" || key == "car_interiors" ) {
            // "<id_car>:<rims hashcode | interior index>,..."
            const bool bRims = key == "car_rims";
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText || *pEnd != ':' ) {
                    break;
                }
                const char *pValue = pEnd + 1;
                const unsigned long long number = strtoull( pValue, &pEnd, 10 );
                if ( pEnd == pValue ) {
                    break;
                }
                if ( bRims ) {
                    profile.OwnedCarRims[static_cast<int32_t>( id )] = static_cast<uint64_t>( number );
                } else {
                    profile.OwnedCarInteriors[static_cast<int32_t>( id )] = static_cast<int32_t>( number );
                }
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "car_stickers" ) {
            // "<id>:<bank>;<index>;<u>;<v>;<scale>;<angle>;<tint>|...,..."
            // (the bank file name carries none of the separators).
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText || *pEnd != ':' ) {
                    break;
                }
                const char *pGroup = pEnd + 1;
                const char *pGroupEnd = strchr( pGroup, ',' );
                if ( pGroupEnd == nullptr ) {
                    pGroupEnd = pGroup + strlen( pGroup );
                }
                // One entry = seven ';'-separated fields, entries '|' apart.
                std::vector<StickerPlacement> entries;
                const char *p = pGroup;
                while ( p < pGroupEnd ) {
                    const char *pEntryEnd = strchr( p, '|' );
                    if ( pEntryEnd == nullptr || pEntryEnd > pGroupEnd ) {
                        pEntryEnd = pGroupEnd;
                    }
                    StickerPlacement save;
                    {
                        char bank[96] = {};
                        int flip = 0;
                        const int fields = sscanf( p, "%95[^;];%d;%f;%f;%f;%f;%u;%f;%d;%f", bank, &save.Index, &save.U,
                                                   &save.V, &save.Scale, &save.AngleDeg, &save.Tint, &save.Alpha, &flip,
                                                   &save.YStretch );
                        save.bFlip = flip != 0;
                        if ( fields >= 2 ) {
                            save.Bank = bank;
                            if ( fields < 3 ) save.U = 0.5f;
                            if ( fields < 4 ) save.V = 0.5f;
                            if ( fields < 5 ) save.Scale = 0.22f;
                            if ( fields < 6 ) save.AngleDeg = 0.0f;
                            if ( fields < 7 ) save.Tint = 0xffffffffu;
                            entries.push_back( save );
                        }
                    }
                    p = pEntryEnd < pGroupEnd ? pEntryEnd + 1 : pGroupEnd;
                }
                if ( !entries.empty() ) {
                    profile.OwnedCarStickers[static_cast<int32_t>( id )] = std::move( entries );
                }
                pText = *pGroupEnd == ',' ? pGroupEnd + 1 : pGroupEnd;
            }
        } else if ( key == "sticker_colors" ) {
            profile.StickerMyColors.clear();
            size_t at = 0;
            while ( at <= value.size() && profile.StickerMyColors.size() < 6 ) {
                size_t end = value.find( ',', at );
                if ( end == std::string::npos ) {
                    end = value.size();
                }
                profile.StickerMyColors.push_back( static_cast<uint32_t>( strtoul( value.substr( at, end - at ).c_str(), nullptr, 10 ) ) );
                at = end + 1;
            }
            profile.StickerMyColors.resize( 6, 0u );
        } else if ( key == "sticker_banks" ) {
            size_t at = 0;
            while ( at < value.size() ) {
                size_t end = value.find( ',', at );
                if ( end == std::string::npos ) {
                    end = value.size();
                }
                if ( end > at ) {
                    profile.OwnedStickerBanks.push_back( value.substr( at, end - at ) );
                }
                at = end + 1;
            }
        } else if ( key == "tests_passed" ) {
            // "<test>:<best seconds>,..."
            size_t at = 0;
            while ( at < value.size() ) {
                size_t end = value.find( ',', at );
                if ( end == std::string::npos ) {
                    end = value.size();
                }
                const std::string item = value.substr( at, end - at );
                const size_t colon = item.find( ':' );
                if ( colon != std::string::npos && colon > 0 ) {
                    profile.PassedTests[item.substr( 0, colon )] = static_cast<float>( atof( item.c_str() + colon + 1 ) );
                }
                at = end + 1;
            }
        } else if ( key == "clothes_owned" ) {
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText ) {
                    break;
                }
                profile.OwnedClothes.push_back( static_cast<int32_t>( id ) );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "house_intros_seen" ) {
            // The house entry cinematics already played (bank names, comma list).
            profile.SeenHouseIntros.clear();
            size_t at = 0;
            while ( at <= value.size() && !value.empty() ) {
                const size_t comma = value.find( ',', at );
                const std::string name =
                    value.substr( at, comma == std::string::npos ? std::string::npos : comma - at );
                if ( !name.empty() ) {
                    profile.SeenHouseIntros.push_back( name );
                }
                if ( comma == std::string::npos ) {
                    break;
                }
                at = comma + 1;
            }
        } else if ( key == "houses_owned" ) {
            // DBHouse _spotname list; replaces the default (the story house).
            profile.OwnedHouses.clear();
            size_t at = 0;
            while ( at <= value.size() && !value.empty() ) {
                const size_t comma = value.find( ',', at );
                const std::string name = value.substr(
                    at, comma == std::string::npos ? std::string::npos
                                                   : comma - at );
                if ( !name.empty() ) {
                    profile.OwnedHouses.push_back( name );
                }
                if ( comma == std::string::npos ) {
                    break;
                }
                at = comma + 1;
            }
        } else if ( key == "home_house" ) {
            profile.HomeHouse = value;
        } else if ( key == "car_tuning" ) {
            // "<id_car>:<accel>,<braking>,<speed>,..."
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText || *pEnd != ':' ) {
                    break;
                }
                TuningLevels levels;
                levels.Accel = static_cast<int32_t>( strtol( pEnd + 1, &pEnd, 10 ) );
                if ( *pEnd == ',' ) {
                    levels.Braking = static_cast<int32_t>( strtol( pEnd + 1, &pEnd, 10 ) );
                }
                if ( *pEnd == ',' ) {
                    levels.Speed = static_cast<int32_t>( strtol( pEnd + 1, &pEnd, 10 ) );
                }
                profile.OwnedCarTuning[static_cast<int32_t>( id )] = levels;
                while ( *pEnd != 0 && *pEnd != ',' ) {
                    pEnd++;
                }
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "car_rim_sizes" ) {
            // "<id_car>:<diameter inch>,..."
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText || *pEnd != ':' ) {
                    break;
                }
                const char *pDiameter = pEnd + 1;
                const long diameter = strtol( pDiameter, &pEnd, 10 );
                if ( pEnd == pDiameter ) {
                    break;
                }
                profile.OwnedCarRimSizes[static_cast<int32_t>( id )] = static_cast<int32_t>( diameter );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "outlaw_level" ) {
            profile.OutlawLevel = number;
        } else if ( key == "chase_escapes" ) {
            profile.NbChaseEscapes = number;
        } else if ( key == "chase_arrests" ) {
            profile.NbChaseArrests = number;
        } else if ( key == "casino_chips" ) {
            profile.CasinoChips = number;
        } else if ( key == "fuel_litres" ) {
            profile.FuelLitres = static_cast<float>( atof( value.c_str() ) );
        } else if ( key == "fuel_grade" ) {
            profile.FuelGrade = number;
        } else if ( key == "been_in_car_wash" ) {
            profile.BeenInCarWash = number != 0;
        } else if ( key == "chase_help_seen" ) {
            profile.ChaseHelpSeen = number != 0;
        } else if ( key == "driven_car" ) {
            profile.DrivenCarId = number;
        } else if ( key == "worn_clothes" ) {
            // "<parttype>:<id_num>,..."
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long part = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText || *pEnd != ':' ) {
                    break;
                }
                const long id = strtol( pEnd + 1, &pEnd, 10 );
                profile.WornClothes[static_cast<int32_t>( part )] = static_cast<int32_t>( id );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "furniture_choices" ) {
            // "<house>:<slot>=<bank>;..."
            size_t at = 0;
            while ( at < value.size() ) {
                size_t end = value.find( ';', at );
                if ( end == std::string::npos ) {
                    end = value.size();
                }
                const std::string item = value.substr( at, end - at );
                const size_t equals = item.find( '=' );
                if ( equals != std::string::npos ) {
                    profile.FurnitureChoices[item.substr( 0, equals )] = item.substr( equals + 1 );
                }
                at = end + 1;
            }
        } else if ( key == "car_wash_coating" ) {
            profile.CarWashCoating = number;
        } else if ( key == "casino_unlockables" ) {
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const unsigned long long id = strtoull( pText, &pEnd, 10 );
                if ( pEnd == pText ) {
                    break;
                }
                profile.CasinoUnlockables.push_back(
                    static_cast<uint64_t>( id ) );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "casino_points" ) {
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText ) {
                    break;
                }
                profile.CasinoPoints.push_back( static_cast<int32_t>( id ) );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "casino_seen" ) {
            size_t at = 0;
            while ( at <= value.size() && !value.empty() ) {
                const size_t comma = value.find( ',', at );
                const std::string flag = value.substr(
                    at, comma == std::string::npos ? std::string::npos
                                                   : comma - at );
                if ( !flag.empty() ) {
                    profile.CasinoSeen.push_back( flag );
                }
                if ( comma == std::string::npos ) {
                    break;
                }
                at = comma + 1;
            }
        } else if ( key == "casino_objectives" ) {
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText ) {
                    break;
                }
                profile.CasinoObjectives.push_back(
                    static_cast<int32_t>( id ) );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key == "casino_rewards" ) {
            profile.CasinoRewardsGranted = number;
        } else if ( key == "casino_roulenums" ) {
            profile.CasinoRouletteNumbers =
                strtoull( value.c_str(), nullptr, 10 );
        } else if ( key == "casino_car_won" ) {
            profile.CasinoCarWon = number != 0;
        } else if ( key == "casino_passes" ) {
            int32_t passes[4] = {};
            if ( sscanf( value.c_str(), "%d,%d,%d,%d", &passes[0], &passes[1], &passes[2], &passes[3] ) >= 1 ) {
                for ( int32_t i = 0; i < 4; i++ ) {
                    profile.CasinoRafflePasses[i] = passes[i];
                }
            }
        } else if ( key == "casino_drinks" ) {
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const long id = strtol( pText, &pEnd, 10 );
                if ( pEnd == pText ) {
                    break;
                }
                profile.CasinoDrinks.push_back( static_cast<int32_t>( id ) );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key.compare( 0, 7, "wrecks_" ) == 0 ) {
            // wrecks_<island> = index,index
            std::vector<uint32_t> &list = profile.FoundWrecks[key.substr( 7 )];
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const unsigned long id = strtoul( pText, &pEnd, 10 );
                if ( pEnd == pText ) {
                    break;
                }
                list.push_back( static_cast<uint32_t>( id ) );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key.compare( 0, 5, "done_" ) == 0 ) {
            // done_<EVENT> = times earned (DBRewardXP events)
            profile.EventsDone[key.substr( 5 )] = number;
        } else if ( key.compare( 0, 9, "explored_" ) == 0 ) {
            // explored_<island> = id,id,id
            std::vector<uint32_t> &list =
                profile.ExploredRoads[key.substr( 9 )];
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const unsigned long id = strtoul( pText, &pEnd, 10 );
                if ( pEnd == pText ) {
                    break;
                }
                list.push_back( static_cast<uint32_t>( id ) );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        } else if ( key.compare( 0, 11, "knownspots_" ) == 0 ) {
            // knownspots_<island> = u64 record id,u64,... (MapScreen Spot::Id)
            std::vector<uint64_t> &list = profile.KnownSpots[key.substr( 11 )];
            const char *pText = value.c_str();
            while ( *pText != 0 ) {
                char *pEnd = nullptr;
                const unsigned long long id = strtoull( pText, &pEnd, 10 );
                if ( pEnd == pText ) {
                    break;
                }
                list.push_back( static_cast<uint64_t>( id ) );
                pText = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
        }
    }
    // The owned-house count drives the Game Progress page (retail keeps the
    // counter in sync with the houses).
    if ( !profile.OwnedHouses.empty() ) {
        profile.NbHouses = static_cast<int32_t>( profile.OwnedHouses.size() );
    }
    OTDU_LOG_INFO( "Profile: loaded '%s' (name '%s', story step %d)\n",
                   ProfilePath().string().c_str(), profile.Name.c_str(),
                   profile.StoryStep );
}

bool SaveProfile( const char *pReason ) {
    const PlayerProfile &profile = GetPlayerProfile();
    // Nothing to save before a character exists (the login / character select
    // screens, and -gm test runs, have no name: they must not overwrite a
    // save).
    if ( profile.Name.empty() ) {
        return false;
    }
    // Write beside, then rename: a crash mid-write keeps the old save.
    const std::filesystem::path path = ProfilePath();
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream file( temp );
        if ( !file ) {
            OTDU_LOG_WARN( "Profile: cannot write '%s'\n",
                           temp.string().c_str() );
            return false;
        }
        file << "avatar_config = " << profile.AvatarConfig << "\n";
        file << "name = " << profile.Name << "\n";
        file << "story_step = " << profile.StoryStep << "\n";
        file << "money = " << profile.Money << "\n";
        file << "cars = " << profile.NbCars << "\n";
        file << "houses = " << profile.NbHouses << "\n";
        file << "points_racing = " << profile.Points[0] << "\n";
        file << "points_cruising = " << profile.Points[1] << "\n";
        file << "points_collection = " << profile.Points[2] << "\n";
        file << "points_social = " << profile.Points[3] << "\n";
        file << "cars_owned = ";
        for ( size_t i = 0; i < profile.OwnedCars.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.OwnedCars[i];
        }
        file << "\n";
        file << "car_paints = ";
        bool bFirstPaint = true;
        for ( const auto &paint : profile.OwnedCarPaints ) {
            file << ( bFirstPaint ? "" : "," ) << paint.first << ":" << paint.second;
            bFirstPaint = false;
        }
        file << "\n";
        file << "car_rims = ";
        bool bFirstRims = true;
        for ( const auto &rims : profile.OwnedCarRims ) {
            file << ( bFirstRims ? "" : "," ) << rims.first << ":" << static_cast<unsigned long long>( rims.second );
            bFirstRims = false;
        }
        file << "\n";
        file << "car_interiors = ";
        bool bFirstInterior = true;
        for ( const auto &interior : profile.OwnedCarInteriors ) {
            file << ( bFirstInterior ? "" : "," ) << interior.first << ":" << interior.second;
            bFirstInterior = false;
        }
        file << "\n";
        // "<id>:<bank>;<index>;<u>;<v>;<scale>;<angle>;<tint>|...,..."
        file << "car_stickers = ";
        bool bFirstSticker = true;
        for ( const auto &stickers : profile.OwnedCarStickers ) {
            if ( stickers.second.empty() ) {
                continue;
            }
            file << ( bFirstSticker ? "" : "," ) << stickers.first << ":";
            for ( size_t i = 0; i < stickers.second.size(); i++ ) {
                const StickerPlacement &p = stickers.second[i];
                file << ( i > 0 ? "|" : "" ) << p.Bank << ";" << p.Index << ";" << p.U << ";" << p.V << ";"
                     << p.Scale << ";" << p.AngleDeg << ";" << p.Tint << ";" << p.Alpha << ";" << ( p.bFlip ? 1 : 0 )
                     << ";" << p.YStretch;
            }
            bFirstSticker = false;
        }
        file << "\n";
        file << "sticker_colors = ";
        for ( size_t i = 0; i < profile.StickerMyColors.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.StickerMyColors[i];
        }
        file << "\n";
        file << "sticker_banks = ";
        for ( size_t i = 0; i < profile.OwnedStickerBanks.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.OwnedStickerBanks[i];
        }
        file << "\n";
        file << "tests_passed = ";
        bool bFirstTest = true;
        for ( const auto &test : profile.PassedTests ) {
            file << ( bFirstTest ? "" : "," ) << test.first << ":" << test.second;
            bFirstTest = false;
        }
        file << "\n";
        file << "clothes_owned = ";
        for ( size_t i = 0; i < profile.OwnedClothes.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.OwnedClothes[i];
        }
        file << "\n";
        file << "houses_owned = ";
        for ( size_t i = 0; i < profile.OwnedHouses.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.OwnedHouses[i];
        }
        file << "\n";
        file << "home_house = " << profile.HomeHouse << "\n";
        file << "house_intros_seen = ";
        for ( size_t i = 0; i < profile.SeenHouseIntros.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.SeenHouseIntros[i];
        }
        file << "\n";
        file << "car_tuning = ";
        bool bFirstTuning = true;
        for ( const auto &tuning : profile.OwnedCarTuning ) {
            file << ( bFirstTuning ? "" : "," ) << tuning.first << ":" << tuning.second.Accel << ","
                 << tuning.second.Braking << "," << tuning.second.Speed;
            bFirstTuning = false;
        }
        file << "\n";
        file << "car_rim_sizes = ";
        bool bFirstRim = true;
        for ( const auto &rim : profile.OwnedCarRimSizes ) {
            file << ( bFirstRim ? "" : "," ) << rim.first << ":" << rim.second;
            bFirstRim = false;
        }
        file << "\n";
        file << "outlaw_level = " << profile.OutlawLevel << "\n";
        file << "chase_escapes = " << profile.NbChaseEscapes << "\n";
        file << "chase_arrests = " << profile.NbChaseArrests << "\n";
        file << "casino_chips = " << profile.CasinoChips << "\n";
        file << "fuel_litres = " << profile.FuelLitres << "\n";
        file << "fuel_grade = " << profile.FuelGrade << "\n";
        file << "been_in_car_wash = " << ( profile.BeenInCarWash ? 1 : 0 ) << "\n";
        file << "chase_help_seen = " << ( profile.ChaseHelpSeen ? 1 : 0 ) << "\n";
        file << "driven_car = " << profile.DrivenCarId << "\n";
        file << "worn_clothes = ";
        bool bFirstWorn = true;
        for ( const auto &worn : profile.WornClothes ) {
            file << ( bFirstWorn ? "" : "," ) << worn.first << ":" << worn.second;
            bFirstWorn = false;
        }
        file << "\n";
        file << "furniture_choices = ";
        for ( const auto &furniture : profile.FurnitureChoices ) {
            file << furniture.first << "=" << furniture.second << ";";
        }
        file << "\n";
        file << "car_wash_coating = " << profile.CarWashCoating << "\n";
        file << "casino_unlockables = ";
        for ( size_t i = 0; i < profile.CasinoUnlockables.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.CasinoUnlockables[i];
        }
        file << "\n";
        file << "casino_points = ";
        for ( size_t i = 0; i < profile.CasinoPoints.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.CasinoPoints[i];
        }
        file << "\n";
        file << "casino_seen = ";
        for ( size_t i = 0; i < profile.CasinoSeen.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.CasinoSeen[i];
        }
        file << "\n";
        file << "casino_objectives = ";
        for ( size_t i = 0; i < profile.CasinoObjectives.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.CasinoObjectives[i];
        }
        file << "\n";
        file << "casino_rewards = " << profile.CasinoRewardsGranted << "\n";
        file << "casino_roulenums = " << profile.CasinoRouletteNumbers << "\n";
        file << "casino_car_won = " << ( profile.CasinoCarWon ? 1 : 0 ) << "\n";
        file << "casino_passes = " << profile.CasinoRafflePasses[0] << "," << profile.CasinoRafflePasses[1] << ","
             << profile.CasinoRafflePasses[2] << "," << profile.CasinoRafflePasses[3] << "\n";
        file << "casino_drinks = ";
        for ( size_t i = 0; i < profile.CasinoDrinks.size(); i++ ) {
            file << ( i > 0 ? "," : "" ) << profile.CasinoDrinks[i];
        }
        file << "\n";
        for ( const auto &island : profile.FoundWrecks ) {
            file << "wrecks_" << island.first << " = ";
            for ( size_t i = 0; i < island.second.size(); i++ ) {
                file << ( i > 0 ? "," : "" ) << island.second[i];
            }
            file << "\n";
        }
        for ( const auto &event : profile.EventsDone ) {
            file << "done_" << event.first << " = " << event.second << "\n";
        }
        for ( const auto &island : profile.ExploredRoads ) {
            file << "explored_" << island.first << " = ";
            for ( size_t i = 0; i < island.second.size(); i++ ) {
                file << ( i > 0 ? "," : "" ) << island.second[i];
            }
            file << "\n";
        }
        for ( const auto &island : profile.KnownSpots ) {
            if ( island.second.empty() ) {
                continue;
            }
            file << "knownspots_" << island.first << " = ";
            for ( size_t i = 0; i < island.second.size(); i++ ) {
                file << ( i > 0 ? "," : "" ) << island.second[i];
            }
            file << "\n";
        }
    }
    std::error_code ec;
    std::filesystem::rename( temp, path, ec );
    if ( ec ) {
        std::filesystem::remove( path, ec );
        std::filesystem::rename( temp, path, ec );
    }
    if ( ec ) {
        OTDU_LOG_WARN( "Profile: cannot replace '%s'\n",
                       path.string().c_str() );
        return false;
    }
    noticeUntilMs = SDL_GetTicks() + 2000;
    OTDU_LOG_INFO( "Profile: saved '%s' (%s)\n", path.string().c_str(),
                   pReason != nullptr ? pReason : "" );
    return true;
}

float GetSaveNoticeTime() {
    const uint64_t now = SDL_GetTicks();
    return now < noticeUntilMs
               ? static_cast<float>( noticeUntilMs - now ) / 1000.0f
               : 0.0f;
}
