#pragma once

#include "game/house_shop.h"

#include <string>
#include <vector>

class Renderer2D;

// The real-estate agency menu (native; the retail one is the shopsestate
// movie, not driven). Opens at a realtor building (ERE_*, the seller
// selectable Type 7 "realtor"), lists the DBHouse rows the agency sells with
// their _buyprice, buys with the profile's money and sells owned houses back
// at their _sellprice.
class EstateMenu {
public:
    // realtorId: the spots.bin id of the ERE_* building (GetIdentifier64bit of
    // the full name, what DBHouse._realtor hashes to); the stock is its rows.
    void open( uint64_t realtorId );
    void close();
    inline bool isOpen() const { return !stock.empty(); }
    // Keys: up / down select, validate buys (or starts / confirms selling an
    // owned house), back closes. Call every frame while open.
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // Inside the agency: the panel at the right, the scene undimmed.
    void setSidePanel( bool bSide ) { bSidePanel = bSide; }

private:
    void buy( const HouseRow &house );
    void sell( const HouseRow &house );

    std::vector<const HouseRow *> stock;
    std::string realtorName;
    int32_t selected = 0;
    bool bConfirmSell = false;
    bool bSidePanel = false;
    std::string message;
    float messageTimer = 0.0f;
};
