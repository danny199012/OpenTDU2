#pragma once

#include <string>
#include <vector>

// The houses of the database [DATA]: DBHouse rows. The realtor sells them
// (rows grouped by _realtor, the full spots.bin name of an ERE_* building,
// e.g. "ERE_B_2236"), each one stands at the spot named by _spotname
// ("EPH_L3DI_5067I"; the spots.bin record id is GetIdentifier64bit of it,
// the rule FindShopBySpotId uses for the shops).
struct HouseRow {
    uint64_t Hashcode = 0;
    std::string Name;     // localized _housename_hc ("House L<n>" fallback)
    std::string Realtor;  // _realtor, the selling agency's spot name
    std::string SpotName; // _spotname, the house's spot name
    int32_t Level = 0;
    int32_t NbCarSlots = 0;
    int32_t BuyPrice = 0;
    int32_t SellPrice = 0;
    bool bWindowedGarage = false;
};

const std::vector<HouseRow> &GetHouses();
// The house of a spots.bin record (its id is GetIdentifier64bit of the spot
// name DBHouse._spotname holds); nullptr if none.
const HouseRow *FindHouseBySpotId( uint64_t spotId );
// The houses an agency sells (GetIdentifier64bit(realtor) == the ERE spot id).
std::vector<const HouseRow *> FindHousesByRealtor( uint64_t realtorId );
// Whether the profile owns the house standing at a spot id.
bool IsHouseOwned( uint64_t spotId );

// Hands the profile a house without a sale (the story's own grants: step 10
// "Home sweet home: Caravan" gives the caravan, DBHouse "Caravan"). bSetHome
// also makes it the home house (profile.HomeHouse). Idempotent: returns false
// when the profile already owns the house and the home does not move (the
// caller uses that to decide whether a save is needed); logs only the grant
// that happened.
bool GrantHouse( const char *pSpotName, bool bSetHome );
