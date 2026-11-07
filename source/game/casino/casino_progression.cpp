#include "shared.h"
#include "casino_progression.h"

#include "database/gs_database.h"
#include "game/player_profile.h"
#include "system/cmd_line.h"

#include <algorithm>

namespace {
    // The four tables, read once.
    std::vector<CasinoLevelRow> levels;
    std::vector<CasinoObjective> objectives;
    std::vector<CasinoReward> rewards;
    std::vector<CasinoUnlockable> unlockables;
    int32_t axisCount = 1;
    bool bRead = false;

    // A localized string of a _*_hc hashcode, empty when unknown.
    std::string Localized( XmbValue &row, const char *pField ) {
        if ( gpDatabase == nullptr ) {
            return "";
        }
        const char *pText = gpDatabase->getString(
            static_cast<int32_t>( row[pField].asUInt64() ) );
        return pText != nullptr ? pText : "";
    }

    void ReadTables() {
        if ( bRead || gpDatabase == nullptr ) {
            return;
        }
        bRead = true;
        XmbValue root = gpDatabase->getDocument().getRoot();

        for ( uint32_t i = 0; i < root.getArraySize( "DBCasinoLevel" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBCasinoLevel", i );
            CasinoLevelRow level;
            level.Axis = static_cast<int32_t>( row["_axis"].asInt() );
            level.Level = static_cast<int32_t>( row["_level"].asInt() );
            level.Points = static_cast<int32_t>( row["_pointsneeded"].asInt() );
            levels.push_back( level );
            axisCount = Max( axisCount, level.Axis + 1 );
        }
        for ( uint32_t i = 0;
              i < root.getArraySize( "DBCasinoObjectiveDetail" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBCasinoObjectiveDetail", i );
            CasinoObjective objective;
            objective.Axis = static_cast<int32_t>( row["_axis"].asInt() );
            objective.Category =
                static_cast<int32_t>( row["_category"].asInt() );
            objective.FilterA = static_cast<int32_t>( row["_filtera"].asInt() );
            objective.FilterB = static_cast<int32_t>( row["_filterb"].asInt() );
            objective.FilterC = static_cast<int32_t>( row["_filterc"].asInt() );
            objective.Hashcode =
                static_cast<int32_t>( row["_hashcode"].asInt() );
            objective.ObjectiveValue =
                static_cast<int32_t>( row["_objectivevalue"].asInt() );
            objective.PointsAwarded =
                static_cast<int32_t>( row["_pointsawarded"].asInt() );
            objective.SubAxis = static_cast<int32_t>( row["_subaxis"].asInt() );
            objective.Text = Localized( row, "_text_hc" );
            objectives.push_back( objective );
            axisCount = Max( axisCount, objective.Axis + 1 );
        }
        for ( uint32_t i = 0; i < root.getArraySize( "DBCasinoReward" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBCasinoReward", i );
            CasinoReward reward;
            reward.CarId = static_cast<int32_t>( row["_carid"].asInt() );
            reward.Unlockable = row["_casinounlockable"].asUInt64();
            reward.CinematicName = row["_cinematicname"].asString();
            reward.CocktailId =
                static_cast<int32_t>( row["_cocktailid"].asInt() );
            reward.Hashcode = static_cast<int32_t>( row["_hashcode"].asInt() );
            reward.Instant = row["_iseffectinstant"].asBool();
            reward.RewardType =
                static_cast<int32_t>( row["_rewardtype"].asInt() );
            reward.RoomLevel =
                static_cast<int32_t>( row["_roomlevel"].asInt() );
            reward.RoomType = static_cast<int32_t>( row["_roomtype"].asInt() );
            reward.Timer = row["_timer"].asFloat();
            rewards.push_back( reward );
        }
        for ( uint32_t i = 0; i < root.getArraySize( "DBCasinoUnlockable" );
              i++ ) {
            XmbValue row = root.getArrayElement( "DBCasinoUnlockable", i );
            CasinoUnlockable unlockable;
            unlockable.Hashcode = row["_hashcode"].asUInt64();
            unlockable.Description = row["_unlockabledesc"].asString();
            unlockables.push_back( unlockable );
        }
        // Sort the level table so the per-axis lookups are ordered.
        std::sort( levels.begin(), levels.end(),
                   []( const CasinoLevelRow &a, const CasinoLevelRow &b ) {
                       return a.Axis != b.Axis ? a.Axis < b.Axis
                                               : a.Level < b.Level;
                   } );
    }
} // namespace

const std::vector<CasinoLevelRow> &GetCasinoLevels() {
    ReadTables();
    return levels;
}

const std::vector<CasinoObjective> &GetCasinoObjectives() {
    ReadTables();
    return objectives;
}

const std::vector<CasinoReward> &GetCasinoRewards() {
    ReadTables();
    return rewards;
}

const std::vector<CasinoUnlockable> &GetCasinoUnlockables() {
    ReadTables();
    return unlockables;
}

int32_t GetCasinoAxisCount() {
    ReadTables();
    return axisCount;
}

void DumpCasinoTables() {
    ReadTables();
    OTDU_LOG_INFO( "Casino: %d level rows, %zu objectives, %zu rewards, %zu "
                   "unlockables, %d axes\n",
                   static_cast<int32_t>( levels.size() ), objectives.size(),
                   rewards.size(), unlockables.size(), axisCount );
    for ( const CasinoLevelRow &level : levels ) {
        OTDU_LOG_INFO(
            "Casino: DBCasinoLevel axis %d level %d needs %d points\n",
            level.Axis, level.Level, level.Points );
    }
    for ( const CasinoObjective &objective : objectives ) {
        OTDU_LOG_INFO( "Casino: objective axis %d sub %d category %d filters "
                       "%d/%d/%d hash %d value %d points %d '%s'\n",
                       objective.Axis, objective.SubAxis, objective.Category,
                       objective.FilterA, objective.FilterB, objective.FilterC,
                       objective.Hashcode, objective.ObjectiveValue,
                       objective.PointsAwarded, objective.Text.c_str() );
    }
    for ( const CasinoReward &reward : rewards ) {
        OTDU_LOG_INFO( "Casino: reward hash %d car %d unlockable %llu "
                       "cinematic '%s' cocktail %d instant %d type %d "
                       "room %d/%d timer %.2f\n",
                       reward.Hashcode, reward.CarId,
                       static_cast<unsigned long long>( reward.Unlockable ),
                       reward.CinematicName.c_str(), reward.CocktailId,
                       reward.Instant ? 1 : 0, reward.RewardType,
                       reward.RoomLevel, reward.RoomType, reward.Timer );
    }
    for ( const CasinoUnlockable &unlockable : unlockables ) {
        OTDU_LOG_INFO( "Casino: unlockable %llu '%s' (%s)\n",
                       static_cast<unsigned long long>( unlockable.Hashcode ),
                       unlockable.Description.c_str(),
                       IdentifierToString( unlockable.Hashcode ).c_str() );
    }
}

namespace {
    CasinoProgressCallback progressCallback;

    std::vector<int32_t> &ProfilePoints() {
        PlayerProfile &profile = GetPlayerProfile();
        const size_t needed =
            static_cast<size_t>( Max( 1, GetCasinoAxisCount() ) );
        if ( profile.CasinoPoints.size() < needed ) {
            profile.CasinoPoints.resize( needed, 0 );
        }
        return profile.CasinoPoints;
    }

    // [INF] The reward <-> level join. DBCasinoReward carries no axis/level and
    // its _hashcode is a small counter, so the reward rows are consumed in
    // _hashcode order: each level gained grants the next `count` rows of the
    // table (their _casinounlockable is the DBCasinoUnlockable row, e.g.
    // 'Cocktail_00', 'Emote_11'). -logcasino prints both tables.
    void GrantNextRewards( int32_t count ) {
        PlayerProfile &profile = GetPlayerProfile();
        std::vector<CasinoReward> ordered = GetCasinoRewards();
        std::sort( ordered.begin(), ordered.end(),
                   []( const CasinoReward &a, const CasinoReward &b ) {
                       return a.Hashcode < b.Hashcode;
                   } );
        for ( int32_t n = 0;
              n < count && profile.CasinoRewardsGranted <
                               static_cast<int32_t>( ordered.size() );
              n++ ) {
            const CasinoReward &reward =
                ordered[profile.CasinoRewardsGranted++];
            if ( reward.Unlockable != 0 ) {
                GrantCasinoUnlockable( reward.Unlockable,
                                       "casino level reward" );
            }
            if ( reward.CarId > 0 ) {
                std::vector<int32_t> &owned = GetPlayerProfile().OwnedCars;
                if ( std::find( owned.begin(), owned.end(), reward.CarId ) ==
                     owned.end() ) {
                    owned.push_back( reward.CarId );
                    OTDU_LOG_INFO( "Casino: reward car %d granted\n",
                                   reward.CarId );
                }
            }
            if ( !reward.CinematicName.empty() ) {
                OTDU_LOG_INFO( "Casino: reward cinematic '%s' (not played: the "
                               "player is a game mode)\n",
                               reward.CinematicName.c_str() );
            }
        }
    }

    std::vector<int32_t> &ProfileObjectives() {
        PlayerProfile &profile = GetPlayerProfile();
        const size_t needed = GetCasinoObjectives().size();
        if ( profile.CasinoObjectives.size() < needed ) {
            profile.CasinoObjectives.resize( needed, 0 );
        }
        return profile.CasinoObjectives;
    }
} // namespace

void SetCasinoProgressCallback( CasinoProgressCallback callback ) {
    progressCallback = std::move( callback );
}

int32_t GetCasinoPoints( int32_t axis ) {
    if ( axis < 0 || axis >= GetCasinoAxisCount() ) {
        return 0;
    }
    std::vector<int32_t> &points = ProfilePoints();
    return axis < static_cast<int32_t>( points.size() ) ? points[axis] : 0;
}

int32_t FindCasinoLevelIndex( int32_t axis, int32_t points ) {
    const std::vector<CasinoLevelRow> &table = GetCasinoLevels();
    int32_t found = -1;
    for ( size_t i = 0; i < table.size(); i++ ) {
        if ( table[i].Axis == axis && table[i].Points <= points ) {
            found = static_cast<int32_t>( i );
        }
    }
    return found;
}

int32_t GetCasinoLevel( int32_t axis ) {
    const int32_t index = FindCasinoLevelIndex( axis, GetCasinoPoints( axis ) );
    return index >= 0 ? GetCasinoLevels()[index].Level : 0;
}

int32_t GetCasinoPointsToNextLevel( int32_t axis ) {
    const int32_t points = GetCasinoPoints( axis );
    for ( const CasinoLevelRow &row : GetCasinoLevels() ) {
        if ( row.Axis == axis && row.Points > points ) {
            return row.Points - points;
        }
    }
    return -1;
}

int32_t AwardCasinoPoints( int32_t axis, int32_t points, const char *pReason ) {
    if ( points <= 0 || axis < 0 || axis >= GetCasinoAxisCount() ) {
        return 0;
    }
    const int32_t before = GetCasinoLevel( axis );
    ProfilePoints()[axis] += points;
    const int32_t after = GetCasinoLevel( axis );
    if ( after > before ) {
        GrantNextRewards( after - before );
        OTDU_LOG_INFO( "Casino: axis %d level %d reached (%d points, %s)\n",
                       axis, after, GetCasinoPoints( axis ),
                       pReason != nullptr ? pReason : "" );
    }
    if ( progressCallback ) {
        progressCallback( axis, after, points,
                          pReason != nullptr ? pReason : "" );
    }
    return points;
}

uint64_t CasinoUnlockableHash( const char *pName ) {
    return GetIdentifier64bit( pName );
}

const CasinoUnlockable *FindCasinoUnlockable( uint64_t hashcode ) {
    for ( const CasinoUnlockable &unlockable : GetCasinoUnlockables() ) {
        if ( unlockable.Hashcode == hashcode ) {
            return &unlockable;
        }
    }
    return nullptr;
}

bool IsCasinoUnlockableOwned( uint64_t hashcode ) {
    const std::vector<uint64_t> &owned = GetPlayerProfile().CasinoUnlockables;
    return std::find( owned.begin(), owned.end(), hashcode ) != owned.end();
}

bool GrantCasinoUnlockable( uint64_t hashcode, const char *pReason ) {
    if ( hashcode == 0 || IsCasinoUnlockableOwned( hashcode ) ) {
        return false;
    }
    GetPlayerProfile().CasinoUnlockables.push_back( hashcode );
    const CasinoUnlockable *pRow = FindCasinoUnlockable( hashcode );
    OTDU_LOG_INFO( "Casino: unlockable %llu granted (%s) '%s'\n",
                   static_cast<unsigned long long>( hashcode ),
                   pReason != nullptr ? pReason : "",
                   pRow != nullptr ? pRow->Description.c_str()
                                   : IdentifierToString( hashcode ).c_str() );
    return true;
}

int32_t GetCasinoObjectiveCounter( const CasinoObjective &objective ) {
    const std::vector<CasinoObjective> &table = GetCasinoObjectives();
    for ( size_t i = 0; i < table.size(); i++ ) {
        if ( &table[i] == &objective ) {
            std::vector<int32_t> &counters = ProfileObjectives();
            return i < counters.size() ? counters[i] : 0;
        }
    }
    return 0;
}

bool IsCasinoObjectiveDone( const CasinoObjective &objective ) {
    return objective.ObjectiveValue > 0 &&
           GetCasinoObjectiveCounter( objective ) >= objective.ObjectiveValue;
}

void GetCasinoObjectiveProgress( int32_t axis,
                                 int32_t &outCompleted,
                                 int32_t &outTotal ) {
    outCompleted = 0;
    outTotal = 0;
    for ( const CasinoObjective &objective : GetCasinoObjectives() ) {
        if ( objective.Axis != axis ) {
            continue;
        }
        outTotal++;
        if ( IsCasinoObjectiveDone( objective ) ) {
            outCompleted++;
        }
    }
}

void DumpCasinoObjectiveProgress() {
    const std::vector<CasinoObjective> &table = GetCasinoObjectives();
    const std::vector<int32_t> &counters = ProfileObjectives();
    int32_t done = 0;
    for ( size_t i = 0; i < table.size(); i++ ) {
        const int32_t counter = i < counters.size() ? counters[i] : 0;
        if ( table[i].ObjectiveValue > 0 &&
             counter >= table[i].ObjectiveValue ) {
            done++;
        }
    }
    OTDU_LOG_INFO( "Casino: %d of %zu objectives complete\n", done,
                   table.size() );
    for ( int32_t axis = 0; axis < GetCasinoAxisCount(); axis++ ) {
        int32_t completed = 0;
        int32_t total = 0;
        GetCasinoObjectiveProgress( axis, completed, total );
        if ( total > 0 ) {
            OTDU_LOG_INFO( "Casino: axis %d objectives %d/%d\n", axis, completed,
                           total );
        }
    }
    for ( size_t i = 0; i < table.size(); i++ ) {
        const int32_t counter = i < counters.size() ? counters[i] : 0;
        if ( counter > 0 && counter < table[i].ObjectiveValue ) {
            OTDU_LOG_INFO( "Casino: in progress '%s' %d/%d\n",
                           table[i].Text.c_str(), counter,
                           table[i].ObjectiveValue );
        }
    }
}

void ReportCasinoEvent( eCasinoEvent event,
                        eCasinoGame game,
                        int32_t subType,
                        int32_t amount ) {
    if ( amount < 0 ) {
        return;
    }
    std::vector<CasinoObjective> &table =
        const_cast<std::vector<CasinoObjective> &>( GetCasinoObjectives() );
    std::vector<int32_t> &counters = ProfileObjectives();
    const int32_t category = static_cast<int32_t>( event );
    const int32_t gameId = static_cast<int32_t>( game );
    for ( size_t i = 0; i < table.size(); i++ ) {
        const CasinoObjective &objective = table[i];
        if ( objective.Category != category || objective.ObjectiveValue <= 0 ) {
            continue;
        }
        if ( objective.FilterA != static_cast<int32_t>( eCasinoGame::Any ) &&
             objective.FilterA != gameId ) {
            continue;
        }
        if ( objective.FilterB != 1 && objective.FilterB != subType ) {
            continue;
        }
        // _filterc is the table / variant id of the online rooms (49,
        // 50, 54...): offline there is one table per game, so only the "any"
        // rows match [INF].
        if ( objective.FilterC != 1 ) {
            continue;
        }
        if ( counters[i] >= objective.ObjectiveValue ) {
            continue; // already done
        }
        if ( event == eCasinoEvent::WinStreak ) {
            counters[i] = Max( 0, amount ); // the current streak, not a sum
        } else {
            counters[i] += amount;
        }
        if ( counters[i] >= objective.ObjectiveValue ) {
            counters[i] = objective.ObjectiveValue;
            OTDU_LOG_INFO(
                "Casino: objective done '%s' (+%d points, axis %d)\n",
                objective.Text.c_str(), objective.PointsAwarded,
                objective.Axis );
            AwardCasinoPoints( objective.Axis, objective.PointsAwarded,
                               "objective" );
        }
    }
}

int32_t GetCasinoTotalLevel() {
    int32_t total = 0;
    for ( int32_t axis = 0; axis < GetCasinoAxisCount(); axis++ ) {
        total += GetCasinoLevel( axis );
    }
    return total;
}

int32_t GetCasinoStatusLevel() {
    // [DATA] The objectives name the ladder: "Get the VIP status (level 5)".
    // The level is the total over the axes (the table's own maximum is the ACE
    // level, 4 + 2 + 2 + 2 = 10).
    return GetCasinoTotalLevel();
}

eCasinoStatus GetCasinoStatus() {
    constexpr int32_t kCasinoVipLevel = 5; // [DATA: the objective text]
    // The ACE level is the sum of the per-axis maxima.
    int32_t aceLevel = 0;
    for ( int32_t axis = 0; axis < GetCasinoAxisCount(); axis++ ) {
        int32_t axisMax = 0;
        for ( const CasinoLevelRow &row : GetCasinoLevels() ) {
            if ( row.Axis == axis ) {
                axisMax = Max( axisMax, row.Level );
            }
        }
        aceLevel += axisMax;
    }
    const int32_t level = GetCasinoStatusLevel();
    if ( aceLevel > 0 && level >= aceLevel ) {
        return eCasinoStatus::ACE;
    }
    if ( level >= kCasinoVipLevel ) {
        return eCasinoStatus::VIP;
    }
    return eCasinoStatus::Player;
}

const char *GetCasinoStatusName( eCasinoStatus status ) {
    switch ( status ) {
    case eCasinoStatus::Player:
        return "Player";
    case eCasinoStatus::VIP:
        return "VIP";
    case eCasinoStatus::ACE:
        return "ACE";
    default:
        return "?";
    }
}

bool HasCasinoSeen( const char *pFlag ) {
    const std::vector<std::string> &seen = GetPlayerProfile().CasinoSeen;
    return std::find( seen.begin(), seen.end(), pFlag ) != seen.end();
}

bool MarkCasinoSeen( const char *pFlag ) {
    if ( HasCasinoSeen( pFlag ) ) {
        return false;
    }
    GetPlayerProfile().CasinoSeen.push_back( pFlag );
    OTDU_LOG_INFO( "Casino: seen '%s'\n", pFlag );
    return true;
}
