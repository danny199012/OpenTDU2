#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The licences and championships of the racing schools [DATA]: DBLicense rows
// (docs/re/licenses_spec.md). A row is one licence (C4, C3 ... the order on
// its island) or, with _isachampionship, one championship; its tests are the
// _challengefile_1..8 (the challenge files of the island).
struct LicenseInfo {
    uint64_t Hashcode = 0;
    std::string Name;        // _championshipname_hc (localized)
    std::string Description; // _championshipdesc_hc
    bool bChampionship = false;
    int32_t Island = 0;      // _island
    int32_t Order = 0;       // _order
    int32_t PlayerLevel = 0; // _playerlevel
    int32_t VehicleCategory[2] = { 0, 0 };
    int32_t RaceMoney = 0;   // _racemoneyreward
    int32_t ChampMoney = 0;  // _champmoneyreward
    std::vector<std::string> Challenges; // _challengefile_n (non empty)
    // The opponents of the races (docs/re/opponents_spec.md 1): _bot_1..7 are
    // DBPNJ hashcodes (0 = none; every championship lists the 7 story drivers),
    // with their _difficulty_bot_n / _aggressiveness_bot_n; _rival is one of
    // them (the championship's rival) [DATA].
    uint64_t Bots[7] = {};
    float BotDifficulty[7] = {};
    float BotAggressiveness[7] = {};
    uint64_t Rival = 0;
};

const std::vector<LicenseInfo> &GetLicenses();
// The licences a racing school teaches on an island (0 Ibiza, 1 Hawai):
// the non championship rows whose tests are RS<letter>... (ERS_A -> 'A'),
// in _order.
std::vector<const LicenseInfo *> GetSchoolLicenses( char schoolLetter, int32_t island );
// "A7", "A5 / A4" ...: the classes of a licence's tests (RS<class>T<nn>).
std::string LicenseClasses( const LicenseInfo &license );
// The name of a vehicle class id (DBCarPhysicsData._vehiclecategory,
// DBLicense._vehiclecategory_n): 1-8 A1-A8, 9-13 B1-B5, 14-17 C1-C4, 18/19
// Ma1/Ma2 (bikes), 20/21 Mb1/Mb2, 22 MC, 23-28 R1-R6 [community list,
// checked against the data: docs/re/licenses_spec.md 1]. "" when unknown.
const char *VehicleClassName( int32_t category );
bool IsTestPassed( const std::string &test );
bool IsLicenseObtained( const LicenseInfo &license );

// ---- championships (DBLicense rows with _isachampionship; docs/re/licenses_spec.md 5) ----
// The championships of an island, in table order.
std::vector<const LicenseInfo *> GetChampionships( int32_t island );
// The HUD spot / map icon label of a championship: "asphalt", "dirt",
// "classic" by its class letter, "cup" for the cups (class 0), "_h" for the
// second ("high", _order 2) one [DATA: HUD.vmf /HUD/enter/Spot labels].
std::string ChampionshipFamily( const LicenseInfo &championship );
// The race's mode from its name's last letter (T time attack, R race, A
// speedtrap, S speed) [DATA: matches gmth in every file; the names are the
// races' own titles, "Speedtrap: Seaport run", "Speed: Ibiza speedway"].
char RaceModeLetter( const std::string &race );
const char *RaceModeName( const std::string &race );
// Speed challenges are not run (their scoring is not decoded): a
// championship is complete when its other races are done.
bool IsRaceRunnable( const std::string &race );
bool IsChampionshipComplete( const LicenseInfo &championship );
// Open: the licence of its class is obtained, the "high" one (_order 2)
// needs the first of its class complete, a cup needs every other championship
// of its island complete [INF]. -allchamps opens them all (tests). The reason
// when closed.
bool IsChampionshipOpen( const LicenseInfo &championship, std::string *pOutReason = nullptr );
