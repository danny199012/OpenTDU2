#pragma once

#include <map>
#include <string>
#include <vector>

#include "game/freeride/sticker_bank.h"

// The tuning levels of a car (bought at a tuning shop, ETUN_*): 0..3 each,
// the DBRewardCar level range.
struct TuningLevels {
    int32_t Accel = 0;
    int32_t Braking = 0;
    int32_t Speed = 0;
};

// Cinematic request of the CINE game mode (0x1f), ported from
// CamilleKMI/OpenTDU2 335759a (docs/re/story_spec.md 3): FUN_00b9a1f0 stores
// it at DAT_01025608 + 0x9730 (kind), +0x9734 (the story callback
// FUN_00b104d0), +0x973c (callback argument); FUN_009b6df0 (CINE init) copies
// it into the mode object. Kinds 1..3 are the story steps 3 (Meeting Tess),
// 5 (Solar Crown exterior) and 6 (Solar Crown interior).
struct CineRequest {
    int32_t Kind = 0;
    bool bStoryCallback = false;
};

CineRequest &GetCineRequest();

// Test drive request of the SHOP mode (FUN_009f3b30 'tstk' fills it, mode 7
// TESTCAR reads it; DAT_00fd92a0 / DAT_00f77688..98, docs/re/story_spec.md
// 5.2.5). ReturnMode -1: back to SHOP at the end of the drive.
struct TestDriveRequest {
    bool bActive = false;
    int32_t IdCar = -1;
    int32_t Color = 0;         // DBCarsAndColors row index of the car
    std::string ColorId;       // the row's _color_id_1 (CinematicCar::applyColor)
    int32_t Rims = 0;          // DBCarsAndRims row index of the car
    int32_t Interior = 0;
    uint64_t RimsHashcode = 0; // DBRims _hashcode
    float Duration = 120.0f;   // DAT_00edf610
    int32_t ReturnMode = -1;   // eGameMode to enter at the end (-1: the story SHOP)
    std::string Spot;          // the dealer spot (ESTOL_B_1275I)
    std::string Island;        // the island of the spot ("" = ibiza, the story's)
    std::string PassengerConfig; // DBPNJ _configid of the passenger
};

TestDriveRequest &GetTestDriveRequest();

// A free-ride visit of a car dealer through the SHOP mode (the SHOPSCAR movie,
// the story used-car dealer's screen): FREERIDE fills it at the dealer door,
// GMShop reads it instead of the story step 7 set-up and hands the car back
// to FREERIDE at the spot entrance when the player leaves [INF: the retail
// dealer enter is FUN_00b30030 -> mode 3 for every car shop spot, CODE].
struct FreeRideShopRequest {
    bool bActive = false;
    std::string Spot;   // the DBCarShop _spot_name (ESTOL_B_1275I ...)
    std::string Bank;   // the spot bank name (ESTOL_B): interior/i<bank>, the exterior bank
    std::string Island; // ibiza / hawai
};

FreeRideShopRequest &GetFreeRideShopRequest();

// A garage car record (FUN_00abffd0 car record: _id_car, colour / rims /
// interior rows, the condition bytes, km; docs/re/story_spec.md 5.2).
struct OwnedCar {
    int32_t IdCar = -1;
    int32_t Color = 0;
    int32_t Rims = 0;
    int32_t Interior = 0;
    float Km = 0.0f;
};

// Player profile data shared between the game modes (profile+0x30 avatar
// configuration, profile+0xc1 name). Saved to <userdata>/profile.sav.
struct PlayerProfile {
    std::string
        AvatarConfig; // DBPNJ _configid (AVT_<Male|Female>[_Asia|_Afrik]_Intro)
    std::string Name;
    int32_t StoryStep = 0; // profile+0x58 (FUN_00b10da0)
    // Progression numbers the Game Progress pages show. Nothing earns them
    // yet; they are stored so the features that do can fill them in.
    int32_t Money = 0;
    int32_t NbCars = 1;
    int32_t NbHouses = 1;
    int32_t Points[4] = { 0, 0, 0, 0 }; // Racing, Cruising, Collection, Social
    // Explored road links per island (index into the island road graph, see
    // MapScreen::loadRoads); retail keeps this in PlayerLevels / RoadLock.
    std::map<std::string, std::vector<uint32_t>> ExploredRoads;
    // Times each DBRewardXP event was earned (see AwardSkillEvent).
    std::map<std::string, int32_t> EventsDone;
    // Wrecks found per island (index in DiscoveryPoints::getWrecks).
    std::map<std::string, std::vector<uint32_t>> FoundWrecks;
    // The map spots discovered per island (the spots.bin record ids, the same
    // values MapScreen's Spot::Id holds): a spot's icon only shows on the map
    // once the car drove within discovery range of the building - the retail
    // law (the save's own KnownSpots section, docs/re/savegame_spec.md).
    std::map<std::string, std::vector<uint64_t>> KnownSpots;
    // _id_car of the cars bought at a dealer (the starter car is not listed).
    std::vector<int32_t> OwnedCars;
    // The garage (FUN_00abffd0 records, docs/re/story_spec.md 5.2): a full
    // record per bought car (the profile+0x188 list). CurrentGarageSlot is
    // the index of the current car (-1: none, e.g. a loaned car); LoanCar the
    // _id_car of a story loan (the step 7 Dino 246 GTS, FUN_00aba280).
    std::vector<OwnedCar> Garage;
    int32_t CurrentGarageSlot = -1;
    int32_t LoanCar = -1;
    // The paint each bought car was ordered in: _id_car -> its DBCarsAndColors
    // row (CinematicCar::applyDatabasePaint).
    std::map<int32_t, int32_t> OwnedCarPaints;
    // The rims and the interior each bought car was ordered with at a
    // dealer's buy window (SHOPSCAR window_buy, gm_shop.cpp): _id_car -> its
    // DBRims _hashcode / its DBCarsAndColors _interior_<n> index (0-based).
    // A car without an entry keeps its _default_rims and interior 1.
    std::map<int32_t, uint64_t> OwnedCarRims;
    std::map<int32_t, int32_t> OwnedCarInteriors;
    // The stickers placed on each car at a sticker shop (ESTICK_*, the paint
    // menu's sticker page): _id_car -> the placements composited onto the
    // paint's sticker canvas (StickerCanvas -> the TD_CarPaint2 STIKERS slot).
    std::map<int32_t, std::vector<StickerPlacement>> OwnedCarStickers;
    // The custom paint each car got at a paint shop (the sticker shop's Paint
    // page, SHOPSCAR /paint): a finish (0 gloss, 1 matt, 2 metal, 3 carbon)
    // and one or two RGBA8 colours (MakeRGBA order); Colour2 is the bicolour's
    // second colour (the Paint2 parts), used only with Bicolour. A car without
    // an entry keeps its dealer paint (OwnedCarPaints / the model's own).
    struct CustomPaint {
        int32_t Type = 0;
        bool Bicolour = false;
        uint32_t Colour1 = 0xffffffffu;
        uint32_t Colour2 = 0xffffffffu;
    };
    std::map<int32_t, CustomPaint> OwnedCarCustomPaints;
    // The sticker packs paid for (DBStickersBanks._bankfile, lower case): a
    // pack is bought once, whichever car wears it (the SHOPSCAR invoice's
    // "Stickers" line charges the packs a layout newly uses).
    std::vector<std::string> OwnedStickerBanks;
    // The six "My Colors" slots of the sticker shop's colour tool (RGBA8,
    // MakeRGBA order; 0 = an empty slot). A new profile starts with a basic
    // palette - the retail slots fill from the pipette, which the port
    // replaces with a hue / brightness dial [INF].
    std::vector<uint32_t> StickerMyColors = { 0u, 0u, 0u, 0u, 0u, 0u };
    // Licence tests passed (the .ig2 name, RSA7T01 ...) -> the best time in
    // seconds; a licence is obtained when all its tests are (licenses.h).
    std::map<std::string, float> PassedTests;
    // DBClothe _id_num of the clothes bought at a clothes shop.
    std::vector<int32_t> OwnedClothes;
    // The DBHouse _spotname of the houses owned (bought at a realtor, ERE_*).
    // The story house is owned from the start.
    std::vector<std::string> OwnedHouses = { "EPH_L3DI_5067I" };
    // The DBHouse _spotname of the home house (the FREERIDE house-flow spawn).
    std::string HomeHouse = "EPH_L3DI_5067I";
    // The house entry cinematics already played, keyed by interior bank name
    // (lph_l0c, gph_l0c ...): the entry shot plays once per house per profile -
    // the owner: "everytime you enter it seems to replay the caravan scene"
    // (docs/re/houses_spec.md 9). Two houses of one level share the bank, so
    // they share the entry key too. Shop interiors keep replaying theirs.
    std::vector<std::string> SeenHouseIntros;
    // Tuning levels per owned car (bought at a tuning shop): _id_car -> levels.
    std::map<int32_t, TuningLevels> OwnedCarTuning;
    // Rim size bought at a tuning shop (the "rim size" mechanical option):
    // _id_car -> rim diameter in inches. Absent = the car's stock rims.
    std::map<int32_t, int32_t> OwnedCarRimSizes;
    // Chase mode (police): the outlaw rank 0..3 (Clean record .. Public
    // enemy, loc 5885 / 5887 / 5888 / 5889) and the counters of the Game
    // Progress pages. The cop rank (5890..5893) is not earned yet.
    int32_t OutlawLevel = 0;
    int32_t NbChaseEscapes = 0;
    int32_t NbChaseArrests = 0;
    // Casino (GMCasino): the chip balance and the unlockables that gate the
    // games. Retail keeps these in the CasinoSave section
    // (CasinoProgressionSave / CasinoUnlockable / CASINO_FORBID_SLOT_MACHINE
    // [DATA: PS3 anchors]); the unlockable list is stored so a game can be
    // locked, and an empty list means the casino is playable offline.
    int32_t CasinoChips = 0;
    std::vector<uint64_t> CasinoUnlockables;
    // Casino progression (GMCasino): points per DBCasinoLevel axis and the
    // retail seen-flags (_hasSeenCasinoProgressionTutorial,
    // _hasSeenCasinoVIPStatusCinematic, ...).
    std::vector<int32_t> CasinoPoints;
    std::vector<std::string> CasinoSeen;
    // DBCasinoObjectiveDetail counters (one per row, in table order) and how
    // many DBCasinoReward rows have been granted (the rewards are consumed in
    // _hashcode order as the levels are reached [INF]).
    std::vector<int32_t> CasinoObjectives;
    int32_t CasinoRewardsGranted = 0;
    // Roulette numbers seen (bit 0..36), for the "See 12 of the possible
    // numbers drawn" objective.
    uint64_t CasinoRouletteNumbers = 0;
    // The slot machine's car prize (DBRewardCar CASINO_CAR_1, the Audi):
    // once won the car is in OwnedCars and the line pays chips instead.
    bool CasinoCarWon = false;
    // The cocktails bought at the casino bar (DBCasinoUnlockable _hashcode).
    std::vector<int32_t> CasinoDrinks;
    // The Solar Crown Raffle passes won at the bar (the cherry in the glass),
    // per level 1..4 (the free passes of the VIP terrace poker tournament)
    // [DATA: m_tombo_desc3..6]. Saved as casino_passes = l1,l2,l3,l4.
    int32_t CasinoRafflePasses[4] = {};
    int32_t ExploredPercent = 0; // not saved: derived from the road graph
    // The tank of the current car (docs/re/fuel_gas_station_spec.md): the retail
    // save has no fuel field, so the port keeps its own (-1: not set yet).
    float FuelLitres = -1.0f;
    int32_t FuelGrade = 0; // 0 Shell basic, 1 Shell v-power
    // The car wash (docs/re/car_wash_spec.md): the retail save's flags, kept
    // beside the fuel state. BeenInCarWash is SCPhoneCallsSave +4 (the phone
    // calls read it); CarWashCoating is CarDirtData +8 - the port renders no
    // dirt yet, so it stays 0 until a dirt shader exists.
    bool BeenInCarWash = false;
    // The "Help: Chase Mode" popup has been shown (it opens by itself on the
    // first traffic violation the police see, once per profile) [INF: the
    // retail help-system seen flags are runtime data, ENC].
    bool ChaseHelpSeen = false;
    // The car the player drives out (_id_car): picked in a house garage or
    // bought at a dealer; -1 = the starter car (the FREERIDE default). FREERIDE
    // loads it at enter [INF: the retail current-car record is ENC].
    int32_t DrivenCarId = -1;
    // The clothes picked in a house's walk-in closet (HOME.vmf dressing):
    // DBClothe _parttype -> _id_num. They replace the DBPNJ slots of the
    // player's avatar config wherever it is built (EdenAvatar::load).
    std::map<int32_t, int32_t> WornClothes;
    // The MyTDUHouse furniture choices (houses_spec.md 11): "<house spot>:<slot
    // name>" -> the interior/myhouse bank (a DBFurniture _bank of the slot's
    // type) that replaces the authored model.
    std::map<std::string, std::string> FurnitureChoices;
    int32_t CarWashCoating = 0;
    // Money display / transfer (docs/re/story_spec.md 4.7, ported from
    // 335759a): ProfileAddMoney(bPending) accumulates here and the counter runs
    // the pending amount into Money over MoneyTransferTimer seconds
    // (ProfileTickMoney; -1 = nothing pending).
    int32_t PendingMoney = 0;
    float MoneyTransferTimer = -1.0f;
    // FUN_00abe0e0 statistics: money earned per reason (index < 28).
    int32_t MoneyEarned[28] = {};
};

PlayerProfile &GetPlayerProfile();

// FUN_00abe0e0: reason statistics; bPending false adds to the money (clamped
// >= 0), true to the pending amount (shown by the money counter).
void ProfileAddMoney( int32_t amount, bool bPending, int32_t reason );
// FUN_00abd0d0: money display + transfer timer 1.0 s.
void ProfileStartMoneyTransfer();
// Profile tick (FUN_00abd460 -> FUN_00abcf60(dt)): the pending money runs into
// the money over the transfer timer.
void ProfileTickMoney( float deltaTime );

// Story step 3 enter (FUN_00b105d0): the avatar configuration out of the
// dream, "AVT_<sex>[_<ethnic>]" (the avatar choice's "_Intro" suffix dropped).
std::string GetStoryAvatarConfig( const std::string &introConfig );

// <userdata>/profile.sav, plain "key = value" lines. The original saves
// without asking (pause, shops, dealerships, the map, houses); the port calls
// SaveProfile( reason ) from the same kind of places [INF: the retail trigger
// list is not decoded]. Returns true when the file was written.
void LoadProfile();
bool SaveProfile( const char *pReason );

// Seconds the "saving" notice stays visible after a save (0 when none).
float GetSaveNoticeTime();
