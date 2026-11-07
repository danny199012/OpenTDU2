#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The casino progression of the original, read from `db_data.cpr` [DATA]:
//
//   DBCasinoLevel            _axis, _level, _pointsneeded
//   DBCasinoObjectiveDetail  _axis, _category, _filtera/b/c, _hashcode,
//                            _objectivevalue, _pointsawarded, _subaxis,
//                            _text_hc
//   DBCasinoReward           _carid, _casinounlockable, _cinematicname,
//                            _cocktailid, _hashcode, _iseffectinstant,
//                            _rewardtype, _roomlevel, _roomtype, _timer
//   DBCasinoUnlockable       _hashcode, _unlockabledesc
//
// plus the save sections the retail client wrote (CasinoProgressionSave /
// CasinoUnlockable) and the status cinematics (_hasSeenCasinoACEStatusCinematic
// / _hasSeenCasinoVIPStatusCinematic) [DATA: PS3 anchors]. Online the points
// came from the casino server; offline the games award them through
// AwardCasinoPoints(). `-logcasino` dumps the four tables.
struct CasinoLevelRow {
    int32_t Axis = 0;
    int32_t Level = 0;
    int32_t Points = 0; // points needed to reach it
};

struct CasinoObjective {
    int32_t Axis = 0;
    int32_t Category = 0;
    int32_t FilterA = 0;
    int32_t FilterB = 0;
    int32_t FilterC = 0;
    int32_t Hashcode = 0;
    int32_t ObjectiveValue = 0;
    int32_t PointsAwarded = 0;
    int32_t SubAxis = 0;
    std::string Text; // _text_hc, localized
};

struct CasinoReward {
    int32_t CarId = 0;
    uint64_t Unlockable = 0;
    std::string CinematicName;
    int32_t CocktailId = 0;
    int32_t Hashcode = 0;
    bool Instant = false;
    int32_t RewardType = 0;
    int32_t RoomLevel = 0;
    int32_t RoomType = 0;
    float Timer = 0.0f;
};

struct CasinoUnlockable {
    uint64_t Hashcode = 0;
    std::string Description; // _unlockabledesc
};

// The tables (read once from the database).
const std::vector<CasinoLevelRow> &GetCasinoLevels();
const std::vector<CasinoObjective> &GetCasinoObjectives();
const std::vector<CasinoReward> &GetCasinoRewards();
const std::vector<CasinoUnlockable> &GetCasinoUnlockables();
// The number of progression axes (max _axis + 1, at least 1).
int32_t GetCasinoAxisCount();
// -logcasino: dump every table (how the data was read).
void DumpCasinoTables();

// The profile's points / level for an axis.
int32_t GetCasinoPoints( int32_t axis );
int32_t GetCasinoLevel( int32_t axis );
// The level reached at these points (-1 below the first threshold).
int32_t FindCasinoLevelIndex( int32_t axis, int32_t points );
// Points still needed for the next level (-1 when the axis is maxed).
int32_t GetCasinoPointsToNextLevel( int32_t axis );

// The games call this: adds the points to the profile, logs a level up, grants
// the rewards of the levels reached, and reports to the callback (the mode
// shows the popup). Returns the points actually awarded (0 for an unknown
// axis).
using CasinoProgressCallback = std::function<void(
    int32_t axis, int32_t level, int32_t points, const std::string &reward )>;
void SetCasinoProgressCallback( CasinoProgressCallback callback );
int32_t AwardCasinoPoints( int32_t axis, int32_t points, const char *pReason );

// Unlockables: the retail gates (CASINO_FORBID_SLOT_MACHINE,
// CASINO_FORBID_CLOTHES_SHOP) and the reward payloads.
bool IsCasinoUnlockableOwned( uint64_t hashcode );
bool GrantCasinoUnlockable( uint64_t hashcode, const char *pReason );
const CasinoUnlockable *FindCasinoUnlockable( uint64_t hashcode );
// GetIdentifier64bit of a name ("CASINO_FORBID_SLOT_MACHINE").
uint64_t CasinoUnlockableHash( const char *pName );

// --- Objectives (DBCasinoObjectiveDetail) ---------------------------------
//
// The category ids [DATA]: 1 money won, 4 all the bets placed, 5 a bet /
// combination won, 6 a win streak, 9 hands won, 13 a jackpot, 14 a game feature
// used, 43 a type / table played, 54 a roulette number seen, 57/59 a UI feature
// used. The games report events through ReportCasinoEvent(); an objective whose
// category and filters match accumulates the amount, and when it reaches
// _objectivevalue it pays _pointsawarded to its axis.
enum class eCasinoEvent : int32_t {
    MoneyWon = 1,
    BetPlaced = 4,
    BetWon = 5,
    WinStreak = 6,
    HandsWon = 9,
    Jackpot = 13,
    FeatureUsed = 14,
    PlayedType = 43,
    NumberSeen = 54,
    UiUsed = 57,
};

// _filtera [DATA]: 1 any game, 2 roulette, 3 a poker table, 4 slot machines,
// 17 the poker cash game.
enum class eCasinoGame : int32_t {
    Any = 1,
    Roulette = 2,
    PokerTable = 3,
    Slots = 4,
    PokerCash = 17
};

// Reports one event. `subType` is _filterb: the bet type on roulette, the hand
// type at poker, the slot machine type. A _filterb of 1 matches any. For
// eCasinoEvent::WinStreak the amount is the *current* streak (0 resets it).
void ReportCasinoEvent( eCasinoEvent event,
                        eCasinoGame game,
                        int32_t subType,
                        int32_t amount );

// The objective counters (PlayerProfile::CasinoObjectives, one per row).
int32_t GetCasinoObjectiveCounter( const CasinoObjective &objective );
bool IsCasinoObjectiveDone( const CasinoObjective &objective );
// The objectives of an axis: completed / total (the progress screen).
void GetCasinoObjectiveProgress( int32_t axis,
                                 int32_t &outCompleted,
                                 int32_t &outTotal );
// -logcasino: the objective counters of the profile.
void DumpCasinoObjectiveProgress();

// VIP / ACE status: the retail ladder is driven by the total casino level
// (_hasSeenCasinoACEStatusCinematic / _hasSeenCasinoVIPStatusCinematic). The
// objectives name the VIP level explicitly ("Get the VIP status (level 5)").
enum class eCasinoStatus : int32_t { Player = 0, VIP, ACE, Count };
eCasinoStatus GetCasinoStatus();
const char *GetCasinoStatusName( eCasinoStatus status );
int32_t GetCasinoTotalLevel();
int32_t GetCasinoStatusLevel();

// The seen-flags of the retail client (tutorials / status cinematics):
// PlayerProfile::CasinoSeen. Returns true the first time (and records it).
bool MarkCasinoSeen( const char *pFlag );
bool HasCasinoSeen( const char *pFlag );
