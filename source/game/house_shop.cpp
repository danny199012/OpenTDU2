#include "shared.h"
#include "house_shop.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "game/player_profile.h"
#include "system/cmd_line.h"

const std::vector<HouseRow> &GetHouses() {
    static std::vector<HouseRow> houses;
    static bool bRead = false;
    if ( bRead || gpDatabase == nullptr ) {
        return houses;
    }
    bRead = true;
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBHouse" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBHouse", i );
        HouseRow house;
        house.Hashcode = row["_hashcode"].asUInt64();
        const char *pName = gpDatabase->getString( static_cast<int32_t>( row["_housename_hc"].asUInt64() ) );
        house.Name = pName != nullptr ? pName : "";
        house.Realtor = row["_realtor"].asString();
        house.SpotName = row["_spotname"].asString();
        house.Level = row["_level"].asInt();
        house.NbCarSlots = row["_nbcarslots"].asInt();
        house.BuyPrice = row["_buyprice"].asInt();
        house.SellPrice = row["_sellprice"].asInt();
        house.bWindowedGarage = row["_windowedgarage"].asBool();
        houses.push_back( house );
        if ( CmdLine::HasArg( "-loghouses" ) ) {
            OTDU_LOG_INFO( "DBHouse[%u] '%s' spot '%s' realtor '%s' level %d buy $%d sell $%d slots %d\n", i,
                           house.Name.c_str(), house.SpotName.c_str(), house.Realtor.c_str(), house.Level,
                           house.BuyPrice, house.SellPrice, house.NbCarSlots );
        }
    }
    return houses;
}

const HouseRow *FindHouseBySpotId( uint64_t spotId ) {
    if ( spotId == 0 ) {
        return nullptr;
    }
    for ( const HouseRow &house : GetHouses() ) {
        if ( !house.SpotName.empty() && GetIdentifier64bit( house.SpotName.c_str() ) == spotId ) {
            return &house;
        }
    }
    return nullptr;
}

std::vector<const HouseRow *> FindHousesByRealtor( uint64_t realtorId ) {
    std::vector<const HouseRow *> stock;
    if ( realtorId == 0 ) {
        return stock;
    }
    for ( const HouseRow &house : GetHouses() ) {
        if ( !house.Realtor.empty() && GetIdentifier64bit( house.Realtor.c_str() ) == realtorId ) {
            stock.push_back( &house );
        }
    }
    return stock;
}

bool IsHouseOwned( uint64_t spotId ) {
    const HouseRow *pHouse = FindHouseBySpotId( spotId );
    if ( pHouse == nullptr ) {
        return false;
    }
    PlayerProfile &profile = GetPlayerProfile();
    for ( const std::string &owned : profile.OwnedHouses ) {
        if ( owned == pHouse->SpotName ) {
            return true;
        }
    }
    return false;
}

bool GrantHouse( const char *pSpotName, bool bSetHome ) {
    // The story's own grant: no money moves and the house does not have to be
    // on a realtor's books (the caravan is not sold by ERE_B_1264I's stock
    // list - the owner buys it in the story). A name that is not a DBHouse row
    // is a data bug, so say so once instead of silently owning nothing.
    if ( pSpotName == nullptr || pSpotName[0] == '\0' ) {
        return false;
    }
    PlayerProfile &profile = GetPlayerProfile();
    const HouseRow *pHouse = nullptr;
    for ( const HouseRow &house : GetHouses() ) {
        if ( house.SpotName == pSpotName ) {
            pHouse = &house;
            break;
        }
    }
    bool bOwned = false;
    for ( const std::string &owned : profile.OwnedHouses ) {
        if ( owned == pSpotName ) {
            bOwned = true;
            break;
        }
    }
    const bool bHomeChanged = bSetHome && profile.HomeHouse != pSpotName;
    if ( !bOwned ) {
        profile.OwnedHouses.push_back( pSpotName );
        profile.NbHouses = static_cast<int32_t>( profile.OwnedHouses.size() );
    }
    if ( bSetHome ) {
        profile.HomeHouse = pSpotName;
    }
    if ( !bOwned ) {
        if ( pHouse != nullptr ) {
            OTDU_LOG_INFO( "House: granted '%s' (spot '%s', %d slot(s))%s, %d house(s)\n", pHouse->Name.c_str(),
                           pHouse->SpotName.c_str(), pHouse->NbCarSlots, bSetHome ? " as the home house" : "",
                           profile.NbHouses );
        } else {
            OTDU_LOG_WARN( "House: granted spot '%s' as a home - it has no DBHouse row (check the name)\n", pSpotName );
        }
    } else if ( bHomeChanged ) {
        OTDU_LOG_INFO( "House: '%s' is now the home house\n", pSpotName );
    }
    // True when the profile changed: the caller saves the profile then (the
    // story grant must survive the session, not only live in memory).
    return !bOwned || bHomeChanged;
}
