#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The house furniture catalogue: DBFurniture [DATA]. A row is one model of
// interior/myhouse/<_bank>.bnk; a house slot (Entity__MyHouseObject) takes the
// rows of its Type (pool_interior_spec.md 13: 1 chair, 4/8 sofa, 9 dining
// table, 11 low table, 15 bar stool ...). The MyTDUHouse mode of HOME.vmf
// swaps a slot's model for another row of the same type
// (docs/re/houses_spec.md 11).
struct FurnitureItem {
    int32_t Row = -1;   // DBFurniture row index
    std::string Bank;   // _bank (lower case, without .bnk)
    int32_t Type = 0;   // _type
    int32_t Price = 0;  // _price
    std::string Name;   // the DB text of the row's name, if any
    bool bCasino = false;
};

const std::vector<FurnitureItem> &GetFurniture();
// The catalogue rows of a slot type, in table order.
std::vector<const FurnitureItem *> FindFurnitureOfType( int32_t type );
const FurnitureItem *FindFurnitureByBank( const std::string &bank );

// The walls and floors: DBMaterial [DATA] (_materialname = an
// interior/myhouse bank, ground_01..25 _type 1 floors, wall_01..20 _type 2
// walls; _price, _level, _name_hc). docs/re/houses_spec.md 13.
struct RoomMaterialItem {
    std::string Bank;
    int32_t Type = 0; // 1 floor, 2 wall
    int32_t Price = 0;
    std::string Name;
};
const std::vector<RoomMaterialItem> &GetRoomMaterials();
std::vector<const RoomMaterialItem *> FindRoomMaterialsOfType( int32_t type );
const RoomMaterialItem *FindRoomMaterialByBank( const std::string &bank );
