#include "shared.h"
#include "skill_levels.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "game/player_profile.h"
#include "system/cmd_line.h"

#include <algorithm>

namespace {
struct Source {
    const char *pTable;
    const char *pPointsField;
};
constexpr Source kSources[4] = {
    { "DBRacingLevel", "_rlpneeded" },
    { "DBCruisingLevel", "_crlpneeded" },
    { "DBCollectionLevel", "_colpneeded" },
    { "DBSocialLevel", "_slpneeded" },
};

std::vector<SkillLevel> ReadTable( const char *pTable, const char *pPointsField ) {
    std::vector<SkillLevel> levels;
    if ( gpDatabase == nullptr ) {
        return levels;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( pTable ); i++ ) {
        XmbValue row = root.getArrayElement( pTable, i );
        SkillLevel level;
        level.Level = static_cast<int32_t>( row["_level"].asInt() );
        level.Points = static_cast<int32_t>( row[pPointsField].asInt() );
        const char *pTitle = gpDatabase->getString( static_cast<int32_t>( row["_grade_hc"].asUInt64() ) );
        level.Title = pTitle != nullptr ? pTitle : "";
        const char *pReward = gpDatabase->getString( static_cast<int32_t>( row["_rewardname_hc"].asUInt64() ) );
        const char *pRewardDesc = gpDatabase->getString( static_cast<int32_t>( row["_rewarddesc_hc"].asUInt64() ) );
        level.Reward = pReward != nullptr ? pReward : "";
        level.RewardDesc = pRewardDesc != nullptr ? pRewardDesc : "";
        // "§PERC§ discount on each vehicle": the percentage is the row's _percent.
        const std::string percent = std::to_string( row["_percent"].asInt( 0 ) ) + "%";
        for ( std::string *pText : { &level.Reward, &level.RewardDesc } ) {
            const std::string token = "\xc2\xa7PERC\xc2\xa7";
            for ( size_t at = pText->find( token ); at != std::string::npos; at = pText->find( token, at + percent.size() ) ) {
                pText->replace( at, token.size(), percent );
            }
        }
        levels.push_back( level );
    }
    std::sort( levels.begin(), levels.end(), []( const SkillLevel &a, const SkillLevel &b ) { return a.Level < b.Level; } );
    if ( CmdLine::HasArg( "-logskills" ) ) {
        for ( const SkillLevel &level : levels ) {
            OTDU_LOG_INFO( "Skills: %s level %d needs %d points, title '%s', reward '%s' / '%s'\n", pTable, level.Level, level.Points, level.Title.c_str(), level.Reward.c_str(), level.RewardDesc.c_str() );
        }
    }
    return levels;
}
} // namespace

const std::vector<SkillLevel> &GetSkillLevels( int32_t skill ) {
    static std::vector<SkillLevel> tables[4];
    static bool bRead = false;
    if ( !bRead && gpDatabase != nullptr ) {
        for ( int32_t i = 0; i < 4; i++ ) {
            tables[i] = ReadTable( kSources[i].pTable, kSources[i].pPointsField );
        }
        bRead = true;
    }
    return tables[skill & 3];
}

const std::vector<SkillLevel> &GetGlobalLevels() {
    static std::vector<SkillLevel> table;
    static bool bRead = false;
    if ( !bRead && gpDatabase != nullptr ) {
        table = ReadTable( "DBPlayerLevel", "_levelpointsneeded" );
        if ( CmdLine::HasArg( "-logskills" ) ) {
            ReadTable( "DBAreaLevel", "_alpneeded" );
        }
        bRead = true;
    }
    return table;
}

int32_t FindSkillLevelIndex( const std::vector<SkillLevel> &levels, int32_t points ) {
    int32_t found = -1;
    for ( size_t i = 0; i < levels.size(); i++ ) {
        if ( levels[i].Points <= points ) {
            found = static_cast<int32_t>( i );
        }
    }
    return found;
}

const std::vector<RewardXp> &GetRewardXp() {
    static std::vector<RewardXp> table;
    static bool bRead = false;
    if ( !bRead && gpDatabase != nullptr ) {
        bRead = true;
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBRewardXP" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBRewardXP", i );
            RewardXp xp;
            xp.Event = row["_event"].asString();
            const char *pName = gpDatabase->getString( static_cast<int32_t>( row["_name_hc"].asUInt64() ) );
            xp.Name = pName != nullptr ? pName : "";
            xp.Points[0] = static_cast<int32_t>( row["_racingpoints"].asInt() );
            xp.Points[1] = static_cast<int32_t>( row["_cruisingpoints"].asInt() );
            xp.Points[2] = static_cast<int32_t>( row["_collectionpoints"].asInt() );
            xp.Points[3] = static_cast<int32_t>( row["_socialpoints"].asInt() );
            xp.TimesAllowed = static_cast<int32_t>( row["_nbtimesallowed"].asInt() );
            xp.SubType = static_cast<int32_t>( row["_subtype"].asInt() );
            table.push_back( xp );
            if ( CmdLine::HasArg( "-logskills" ) ) {
                OTDU_LOG_INFO( "RewardXP: '%s' (%s) racing %d cruising %d collection %d social %d, times %d subtype %d\n", xp.Event.c_str(),
                               xp.Name.c_str(), xp.Points[0], xp.Points[1], xp.Points[2], xp.Points[3], xp.TimesAllowed, xp.SubType );
            }
        }
    }
    return table;
}

const RewardXp *FindRewardXp( const char *pEvent ) {
    for ( const RewardXp &xp : GetRewardXp() ) {
        if ( StringCmpI( xp.Event.c_str(), pEvent ) == 0 ) {
            return &xp;
        }
    }
    return nullptr;
}

namespace {
SkillEventCallback gSkillEventCallback;
}

void SetSkillEventCallback( SkillEventCallback callback ) {
    gSkillEventCallback = std::move( callback );
}

bool AwardSkillEvent( const char *pEvent ) {
    const RewardXp *pXp = FindRewardXp( pEvent );
    if ( pXp == nullptr ) {
        OTDU_LOG_WARN( "Skills: unknown event '%s'\n", pEvent );
        return false;
    }
    PlayerProfile &profile = GetPlayerProfile();
    int32_t &done = profile.EventsDone[pXp->Event];
    const int32_t allowed = pXp->TimesAllowed > 0 ? pXp->TimesAllowed : 1;
    if ( done >= allowed ) {
        return false;
    }
    done++;
    for ( int32_t skill = 0; skill < 4; skill++ ) {
        if ( pXp->Points[skill] > 0 ) {
            profile.Points[skill] += pXp->Points[skill];
            OTDU_LOG_INFO( "Skills: event '%s' (%s): skill %d +%d points (now %d)\n", pXp->Event.c_str(), pXp->Name.c_str(), skill,
                           pXp->Points[skill], profile.Points[skill] );
            if ( gSkillEventCallback ) {
                gSkillEventCallback( skill, pXp->Name, pXp->Points[skill] );
            }
        }
    }
    return true;
}
