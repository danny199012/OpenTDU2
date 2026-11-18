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
// The collection milestones: pays every DBRewardXP event "<family>_<n>" with n <= count
// (COLLECTION_CARS_DIF / _HOUSES / _FURNITURES / _CLOTHES / _HAIRCUTS / _STICKERS). Each event is
// once only (_nbtimesallowed), so calling it after every purchase is safe. [DATA]
void AwardCollectionMilestones( const char *pFamily, int32_t count );

// The area levels (DBAreaLevel: Stranger / Familiar / Buddy / Friend, per island; -1 is the default table). The
// area level points (ALP) of a cruising zone are the Cruising points of the DBRewardXP events of that area
// ("CRUISING_<ISLAND>_..._AREA<n>..." : roads, wrecks, events won) times the times each was earned [INF: DBAreaLevel's
// 200..1700 scale matches the ~1200 points those events pay per area; the dev exe loader is 0x49a1c0].
struct AreaLevel {
    int32_t Island = -1; // _island: -1 default, 0 Ibiza, 1 Hawai
    int32_t Level = 0;
    int32_t Alp = 0; // _alpneeded
    std::string Title, Reward, RewardDesc;
};
const std::vector<AreaLevel> &GetAreaLevels();
// ALP of area n (1-based) of an island ("IBIZA" / "HAWAI"), from the profile's EventsDone.
int32_t GetAreaPoints( const std::string &island, int32_t area );
// The level row an ALP total reaches on an island (the island's own table, else the default one); null when none.
const AreaLevel *FindAreaLevel( int32_t islandIndex, int32_t alp );
// The area level (1 Stranger .. 5) of the zone the player is in, kept by the free ride each frame; -1 until it is
// known. The tuners, hairdressers and clothes shops stock the items whose DBClothe._min_arealevel is <= it
// [DATA: the field; the DBAreaLevel reward text "New items available in tuners, hairdressers, clothes shops"].
void SetCurrentAreaLevel( int32_t level );
int32_t GetCurrentAreaLevel();
// True when an item needing minLevel is stocked at the current area level (always when the level is unknown).
bool IsStockedAtAreaLevel( int32_t minLevel );
