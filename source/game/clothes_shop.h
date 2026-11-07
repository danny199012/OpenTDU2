#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Renderer2D;

// The clothes of the database [DATA]: DBClothe rows (_clothename_hc the item
// name, _brand the DBBrands hashcode, _price, _parttype the slot of the body,
// _sex, _min_arealevel). The retail shop screen is the shopsavatar.bnk movie
// (not implemented); this is the port's native list in the dealer menu's
// shape [INF].
struct ClotheItem {
    int32_t Id = 0; // _id_num
    std::string Name;
    std::string Brand;
    uint64_t BrandHashcode = 0; // _brand (DBBrands _hashcode; 0 unbranded)
    int32_t Price = 0;
    int32_t PartType = 0;
    int32_t Sex = 0;            // _sex
};

const std::vector<ClotheItem> &GetClothes();
const ClotheItem *FindClothe( int32_t id );
// DBBrands _hashcode of a _manufacturer_id ("HAIR", "HIGHLIFE" ...), 0 if none.
uint64_t FindBrandHashcode( const char *pManufacturerId );
// The DBClothe._sex of the player's avatar (1 woman, 2 man).
int32_t PlayerClothesSex();

// The native clothes shop menu (opened at a clothes shop spot, like the car
// dealership's DealerMenu).
class ClothesMenu {
public:
    void open( const std::string &shopName );
    // One shop's stock: the items of a brand (DBClothe._brand) for a sex
    // (1 woman, 2 man; 0 both).
    void open( const std::string &shopName, uint64_t brandHashcode, int32_t sex );
    void close();
    inline bool isOpen() const { return pItems != nullptr; }
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );

private:
    void buySelected();
    const std::vector<ClotheItem> *pItems = nullptr;
    std::vector<ClotheItem> stock; // the filtered list of open( name, brand, sex )
    std::string shopName;
    int32_t selected = 0;
    std::string message;
    float messageTimer = 0.0f;
};
