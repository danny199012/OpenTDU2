#pragma once

#include <string>
#include <vector>

// The level tables of the Game Progress skills, read from the database
// [DATA]: DBRacingLevel (_rlpneeded), DBCruisingLevel (_crlpneeded),
// DBCollectionLevel (_colpneeded), DBSocialLevel (_slpneeded) and the global
// DBPlayerLevel (_levelpointsneeded); the title is the localized _grade_hc.
// Skill order everywhere: 0 Racing (Competition), 1 Cruising (Discovery),
// 2 Collection, 3 Social.
struct SkillLevel {
    int32_t Level = 0;
    int32_t Points = 0; // points needed to reach it
    std::string Title;
    std::string Reward;     // _rewardname_hc (localized, may be empty)
    std::string RewardDesc; // _rewarddesc_hc
};

const std::vector<SkillLevel> &GetSkillLevels( int32_t skill );
const std::vector<SkillLevel> &GetGlobalLevels();

// Level reached with these points (0 below the first threshold): the highest
// row whose Points <= points; -1 index when none.
int32_t FindSkillLevelIndex( const std::vector<SkillLevel> &levels, int32_t points );

// DBRewardXP [DATA]: the points the game awards per event (one row per event,
// points for each of the four skills), with how many times it can be earned.
struct RewardXp {
    std::string Event;   // _event
    std::string Name;    // _name_hc (localized)
    int32_t Points[4] = { 0, 0, 0, 0 }; // racing, cruising, collection, social
    int32_t TimesAllowed = 0;
    int32_t SubType = 0;
};
const std::vector<RewardXp> &GetRewardXp();
// The row of an event name (case insensitive), null when none.
const RewardXp *FindRewardXp( const char *pEvent );

// Awards the points of a DBRewardXP event to the profile (once per
// _nbtimesallowed, counted in PlayerProfile::EventsDone) and tells the callback
// per skill that got points (the HUD shows the earn popup). Returns false when
// the event is unknown or used up.
#include <functional>
using SkillEventCallback = std::function<void( int32_t skill, const std::string &name, int32_t points )>;
void SetSkillEventCallback( SkillEventCallback callback );
bool AwardSkillEvent( const char *pEvent );
