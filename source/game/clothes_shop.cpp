#include "shared.h"
#include "clothes_shop.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"

#include <algorithm>

const std::vector<ClotheItem> &GetClothes() {
    static std::vector<ClotheItem> items;
    static bool bRead = false;
    if ( bRead || gpDatabase == nullptr ) {
        return items;
    }
    bRead = true;
    XmbValue root = gpDatabase->getDocument().getRoot();
    const uint32_t count = root.getArraySize( "DBClothe" );
    for ( uint32_t i = 0; i < count; i++ ) {
        XmbValue row = root.getArrayElement( "DBClothe", i );
        if ( !row["_isvalid"].asBool() ) {
            continue;
        }
        ClotheItem item;
        item.Id = static_cast<int32_t>( row["_id_num"].asInt() );
        const char *pName = gpDatabase->getString( static_cast<int32_t>( row["_clothename_hc"].asUInt64() ) );
        item.Name = pName != nullptr ? pName : "";
        // _brand is a DBBrands hashcode (0: unbranded).
        const uint64_t brandHc = row["_brand"].asUInt64();
        if ( brandHc != 0 ) {
            for ( uint32_t b = 0; b < root.getArraySize( "DBBrands" ); b++ ) {
                XmbValue brand = root.getArrayElement( "DBBrands", b );
                if ( brand["_hashcode"].asUInt64() == brandHc ) {
                    // A plain string (HIGHLIFE -> "Hiro Hisaishi"), not a loc id; the
                    // technical rows (DEFAULT, HAIR, PNJ, EDEN, HELMET) are not shown.
                    const std::string name = brand["_manufacturer_name"].asString();
                    const std::string id = brand["_manufacturer_id"].asString();
                    if ( id != "DEFAULT" && id != "HAIR" && id != "PNJ" && id != "EDEN" && id != "HELM" ) {
                        item.Brand = name;
                    }
                    break;
                }
            }
        }
        item.BrandHashcode = brandHc;
        item.Price = static_cast<int32_t>( row["_price"].asInt() );
        item.PartType = static_cast<int32_t>( row["_parttype"].asInt() );
        item.Sex = static_cast<int32_t>( row["_sex"].asInt() );
        if ( CmdLine::HasArg( "-logshops" ) ) {
            OTDU_LOG_INFO( "DBClothe id %d brand 0x%llx '%s' part %d sex %d $%d '%s' file '%s'\n", item.Id,
                           static_cast<unsigned long long>( brandHc ), item.Brand.c_str(), item.PartType, item.Sex,
                           item.Price, item.Name.c_str(), row["_file"].asString().c_str() );
        }
        if ( !item.Name.empty() ) {
            items.push_back( item );
        }
    }
    OTDU_LOG_INFO( "Clothes: %zu valid DBClothe items\n", items.size() );
    return items;
}

uint64_t FindBrandHashcode( const char *pManufacturerId ) {
    if ( gpDatabase == nullptr ) {
        return 0;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t b = 0; b < root.getArraySize( "DBBrands" ); b++ ) {
        XmbValue brand = root.getArrayElement( "DBBrands", b );
        if ( brand["_manufacturer_id"].asString() == pManufacturerId ) {
            return brand["_hashcode"].asUInt64();
        }
    }
    return 0;
}

int32_t PlayerClothesSex() {
    // DBClothe._sex: 2 man (M_* files), 1 woman (W_*) [DATA]; the avatar
    // configuration names the sex (AVT_Male_* / AVT_Female_*).
    const std::string &config = GetPlayerProfile().AvatarConfig;
    return config.find( "Female" ) != std::string::npos ? 1 : 2;
}

const ClotheItem *FindClothe( int32_t id ) {
    for ( const ClotheItem &item : GetClothes() ) {
        if ( item.Id == id ) {
            return &item;
        }
    }
    return nullptr;
}

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

std::string FormatMoney( int32_t value ) {
    std::string digits = std::to_string( value < 0 ? -value : value );
    std::string out;
    for ( size_t i = 0; i < digits.size(); i++ ) {
        if ( i > 0 && ( digits.size() - i ) % 3 == 0 ) {
            out += ',';
        }
        out += digits[i];
    }
    return ( value < 0 ? "-$" : "$" ) + out;
}
} // namespace

void ClothesMenu::open( const std::string &newShopName ) {
    shopName = newShopName;
    pItems = &GetClothes();
    selected = 0;
    message.clear();
    messageTimer = 0.0f;
}

void ClothesMenu::open( const std::string &newShopName, uint64_t brandHashcode, int32_t sex ) {
    // The shop's stock: the items of its brand (DBCarShop._clothesshopbrand =
    // DBClothe._brand) for the player's sex, the buyable ones (the defaults
    // carry $999999) [DATA; the retail filter itself is ENC].
    stock.clear();
    for ( const ClotheItem &item : GetClothes() ) {
        if ( item.BrandHashcode == brandHashcode && ( sex == 0 || item.Sex == sex ) && item.Price < 999999 ) {
            stock.push_back( item );
        }
    }
    std::stable_sort( stock.begin(), stock.end(),
                      []( const ClotheItem &a, const ClotheItem &b ) { return a.PartType < b.PartType; } );
    OTDU_LOG_INFO( "Clothes: '%s' brand 0x%llx sex %d: %zu item(s)\n", newShopName.c_str(),
                   static_cast<unsigned long long>( brandHashcode ), sex, stock.size() );
    shopName = newShopName;
    pItems = &stock;
    selected = 0;
    message.clear();
    messageTimer = 0.0f;
}

void ClothesMenu::close() {
    pItems = nullptr;
}

void ClothesMenu::buySelected() {
    if ( pItems == nullptr || selected < 0 || static_cast<size_t>( selected ) >= pItems->size() ) {
        return;
    }
    const ClotheItem &item = ( *pItems )[selected];
    PlayerProfile &profile = GetPlayerProfile();
    if ( std::find( profile.OwnedClothes.begin(), profile.OwnedClothes.end(), item.Id ) != profile.OwnedClothes.end() ) {
        message = "You already own this item";
    } else if ( profile.Money < item.Price ) {
        message = "Not enough money";
    } else {
        profile.Money -= item.Price;
        profile.OwnedClothes.push_back( item.Id );
        message = "Bought: " + item.Name;
        OTDU_LOG_INFO( "Clothes: bought '%s' (id %d) for $%d (money now $%d)\n", item.Name.c_str(), item.Id, item.Price,
                       profile.Money );
        SaveProfile( "clothes bought" );
    }
    messageTimer = 3.0f;
}

void ClothesMenu::tick( float deltaTime ) {
    if ( pItems == nullptr ) {
        return;
    }
    messageTimer = Max( 0.0f, messageTimer - deltaTime );
    const int32_t count = static_cast<int32_t>( pItems->size() );
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        selected = ( selected + count - 1 ) % count;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        selected = ( selected + 1 ) % count;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        buySelected();
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        close();
    }
}

void ClothesMenu::draw( Renderer2D &renderer ) {
    if ( pItems == nullptr ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    const PlayerProfile &profile = GetPlayerProfile();
    renderer.begin( 1280.0f, 720.0f );
    renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, 150 ) );
    const float x0 = 280.0f, y0 = 110.0f, w = 720.0f, h = 500.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 22, 26, 34, 235 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 34, 52, 84, 255 ) );
    renderer.drawText( pFont, shopName.c_str(), x0 + 20.0f, y0 + 10.0f, 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 12.0f, 24.0f, Rgba( 120, 255, 120 ),
                       eTextAlign::TA_Right );
    float y = y0 + 70.0f;
    const float bottom = y0 + h - 60.0f;
    const int32_t count = static_cast<int32_t>( pItems->size() );
    // Scroll window over the list (the table is bigger than the panel).
    const int32_t first = Clamp( selected - 8, 0, Max( 0, count - 9 ) );
    for ( int32_t i = first; i < count && y < bottom; i++ ) {
        const ClotheItem &item = ( *pItems )[i];
        const bool bSelectedItem = i == selected;
        const bool bOwned = std::find( profile.OwnedClothes.begin(), profile.OwnedClothes.end(), item.Id ) != profile.OwnedClothes.end();
        if ( bSelectedItem ) {
            renderer.drawRect( Rect2D( x0 + 10.0f, y - 4.0f, w - 20.0f, 40.0f ), Rgba( 40, 70, 130, 255 ) );
        }
        const std::string name = item.Brand.empty() ? item.Name : item.Brand + " " + item.Name;
        renderer.drawText( pFont, name.c_str(), x0 + 24.0f, y, 22.0f, bOwned ? Rgba( 150, 150, 150 ) : Rgba( 255, 255, 255 ) );
        const std::string price = bOwned ? std::string( "Owned" ) : FormatMoney( item.Price );
        const uint32_t priceColor = bOwned ? Rgba( 150, 150, 150 ) : ( profile.Money >= item.Price ? Rgba( 255, 255, 255 ) : Rgba( 255, 110, 110 ) );
        renderer.drawText( pFont, price.c_str(), x0 + w - 24.0f, y, 22.0f, priceColor, eTextAlign::TA_Right );
        y += 44.0f;
    }
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 90.0f, 24.0f, Rgba( 255, 210, 80 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, "A / Enter: buy      B / Esc: leave", x0 + w * 0.5f, y0 + h - 44.0f, 20.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.end();
}
