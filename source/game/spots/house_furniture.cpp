#include "shared.h"
#include "house_furniture.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <cctype>

namespace {
// -logfurniture: every field of the first DBFurniture rows (the field names
// are the table's own [DATA]).
void DumpRow( const XmbValue &row, uint32_t index ) {
    const XmbType *pType = row.getType();
    if ( pType == nullptr ) {
        return;
    }
    std::string text;
    for ( const XmbType::Field &field : pType->Fields ) {
        if ( field.bArray ) {
            continue;
        }
        text += field.Name + "=" + row[field.Name.c_str()].asString() + "/" +
                std::to_string( row[field.Name.c_str()].asInt() ) + " ";
    }
    OTDU_LOG_INFO( "DBFurniture[%u]: %s\n", index, text.c_str() );
}
} // namespace

const std::vector<FurnitureItem> &GetFurniture() {
    static std::vector<FurnitureItem> items;
    static bool bRead = false;
    if ( bRead || gpDatabase == nullptr ) {
        return items;
    }
    bRead = true;
    static const bool bLog = CmdLine::HasArg( "-logfurniture" );
    XmbValue root = gpDatabase->getDocument().getRoot();
    if ( bLog && root.getType() != nullptr ) {
        std::string tables;
        for ( const XmbType::Field &field : root.getType()->Fields ) {
            tables += field.Name + "(" + std::to_string( root.getArraySize( field.Name.c_str() ) ) + ") ";
        }
        OTDU_LOG_INFO( "Database tables: %s\n", tables.c_str() );
    }
    const uint32_t count = root.getArraySize( "DBFurniture" );
    for ( uint32_t i = 0; i < count; i++ ) {
        XmbValue row = root.getArrayElement( "DBFurniture", i );
        if ( bLog && i < 6 ) {
            DumpRow( row, i );
        }
        FurnitureItem item;
        item.Row = static_cast<int32_t>( i );
        item.Bank = ToLower( row["_bank"].asString() );
        if ( item.Bank.size() > 4 && item.Bank.compare( item.Bank.size() - 4, 4, ".bnk" ) == 0 ) {
            item.Bank.resize( item.Bank.size() - 4 );
        }
        item.Type = static_cast<int32_t>( row["_type"].asInt() );
        item.Price = static_cast<int32_t>( row["_price"].asInt() );
        item.bCasino = row["_iscasino"].asInt() != 0;
        for ( const char *pField : { "_name_hc", "_libelle_hc", "_furniturename_hc", "_name" } ) {
            const int64_t id = row[pField].asInt();
            if ( id != 0 ) {
                const char *pName = gpDatabase->getString( static_cast<int32_t>( id ) );
                if ( pName != nullptr && *pName != 0 ) {
                    item.Name = pName;
                    break;
                }
            }
        }
        if ( item.Name.empty() ) {
            item.Name = item.Bank;
        }
        if ( !item.Bank.empty() ) {
            items.push_back( item );
        }
    }
    OTDU_LOG_INFO( "DBFurniture: %zu row(s) with a bank\n", items.size() );
    return items;
}

std::vector<const FurnitureItem *> FindFurnitureOfType( int32_t type ) {
    std::vector<const FurnitureItem *> result;
    for ( const FurnitureItem &item : GetFurniture() ) {
        if ( item.Type == type && !item.bCasino ) {
            result.push_back( &item );
        }
    }
    return result;
}

const FurnitureItem *FindFurnitureByBank( const std::string &bank ) {
    const std::string lower = ToLower( bank );
    for ( const FurnitureItem &item : GetFurniture() ) {
        if ( item.Bank == lower ) {
            return &item;
        }
    }
    return nullptr;
}

const std::vector<RoomMaterialItem> &GetRoomMaterials() {
    static std::vector<RoomMaterialItem> items;
    static bool bRead = false;
    if ( bRead || gpDatabase == nullptr ) {
        return items;
    }
    bRead = true;
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBMaterial" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBMaterial", i );
        RoomMaterialItem item;
        item.Bank = ToLower( row["_materialname"].asString() );
        item.Type = static_cast<int32_t>( row["_type"].asInt() );
        item.Price = static_cast<int32_t>( row["_price"].asInt() );
        const char *pName = gpDatabase->getString( static_cast<int32_t>( row["_name_hc"].asInt() ) );
        item.Name = pName != nullptr && *pName != 0 ? pName : item.Bank;
        if ( !item.Bank.empty() && row["_isvalid"].asInt( 1 ) != 0 ) {
            items.push_back( item );
        }
    }
    OTDU_LOG_INFO( "DBMaterial: %zu wall / floor row(s)\n", items.size() );
    return items;
}

std::vector<const RoomMaterialItem *> FindRoomMaterialsOfType( int32_t type ) {
    std::vector<const RoomMaterialItem *> result;
    for ( const RoomMaterialItem &item : GetRoomMaterials() ) {
        if ( item.Type == type ) {
            result.push_back( &item );
        }
    }
    return result;
}

const RoomMaterialItem *FindRoomMaterialByBank( const std::string &bank ) {
    const std::string lower = ToLower( bank );
    for ( const RoomMaterialItem &item : GetRoomMaterials() ) {
        if ( item.Bank == lower ) {
            return &item;
        }
    }
    return nullptr;
}
