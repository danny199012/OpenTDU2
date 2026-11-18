#include "shared.h"
#include "hair_ui.h"

#include "flash/flash_player.h"
#include "game/clothes_shop.h"
#include "game/player_profile.h"
#include "game/skill_levels.h"
#include "system/cmd_line.h"

namespace {
constexpr int32_t kPartHair = 3;
constexpr int32_t kPartBeard = 9;
constexpr int32_t kNumColors = 14; // HAIR_MAT palette entries (14 = keep the hair's own colours) [CODE]

std::string Lower( std::string text ) {
    for ( char &c : text ) {
        c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    }
    return text;
}

// The movie's icon groups [DATA: setHairMenu's icArray]. Men: short / long / afro;
// women: short / attached / afro. 0..2.
int32_t HairGroupOf( const ClotheItem &item ) {
    const std::string name = Lower( item.Name );
    if ( name.find( "afro" ) != std::string::npos ) {
        return 2;
    }
    if ( name.find( "long" ) != std::string::npos || name.find( "attached" ) != std::string::npos ||
         name.find( "half-length" ) != std::string::npos ) {
        return 1;
    }
    return 0;
}

// Facial hair kinds [DATA: pilosity icArray]: goatee / moustache / beard / shaved.
int32_t BeardGroupOf( const ClotheItem &item ) {
    const std::string name = Lower( item.Name );
    if ( name.find( "goatee" ) != std::string::npos ) {
        return 0;
    }
    if ( name.find( "mustache" ) != std::string::npos || name.find( "moustache" ) != std::string::npos ) {
        return 1;
    }
    if ( name.find( "beard" ) != std::string::npos ) {
        return 2;
    }
    return 3;
}

std::string FormatMoney( int32_t value ) {
    std::string digits = std::to_string( value < 0 ? -value : value );
    std::string out;
    for ( size_t i = 0; i < digits.size(); i++ ) {
        if ( i > 0 && ( digits.size() - i ) % 3 == 0 ) {
            out += ' ';
        }
        out += digits[i];
    }
    return "$ " + out;
}

int32_t GroupOfType( const std::string &type ) {
    // "short" / "w_short", "long" / "w_attached", "afro" / "w_afro"; "goatee", "moustache", "beard", "shave".
    if ( type == "short" || type == "w_short" || type == "goatee" ) {
        return 0;
    }
    if ( type == "long" || type == "w_attached" || type == "moustache" ) {
        return 1;
    }
    if ( type == "afro" || type == "w_afro" || type == "beard" ) {
        return 2;
    }
    return 3; // shave
}

const char *HairTypeId( bool bWoman, int32_t group ) {
    static const char *const kMan[] = { "short", "long", "afro" };
    static const char *const kWoman[] = { "w_short", "w_attached", "w_afro" };
    return ( bWoman ? kWoman : kMan )[Clamp( group, 0, 2 )];
}
} // namespace

void HairUi::bind( FlashPlayer *pNewUi ) {
    pUi = pNewUi;
    bWoman = PlayerClothesSex() == 1;
    // The square menu picks "manhaircut" / "womanhaircut" from /:isWoman [DATA].
    pUi->setVariableBool( "/:isWoman", bWoman );
    const PlayerProfile &profile = GetPlayerProfile();
    hair = Look();
    pilosity = Look();
    const auto wornHair = profile.WornClothes.find( kPartHair );
    hair.BaseId = wornHair != profile.WornClothes.end() ? wornHair->second : -1;
    hair.BaseColor = Max( profile.HairColor, 0 );
    hair.Color = hair.BaseColor;
    const auto wornBeard = profile.WornClothes.find( kPartBeard );
    pilosity.BaseId = wornBeard != profile.WornClothes.end() ? wornBeard->second : -1;
    pilosity.BaseColor = Max( profile.BeardColor, 0 );
    pilosity.Color = pilosity.BaseColor;
    // The type of what is worn now (else the first icon).
    int32_t hairGroup = 0, beardGroup = 3;
    if ( const ClotheItem *pWorn = FindClothe( hair.BaseId ) ) {
        hairGroup = HairGroupOf( *pWorn );
    }
    if ( const ClotheItem *pWorn = FindClothe( pilosity.BaseId ) ) {
        beardGroup = BeardGroupOf( *pWorn );
    }
    hair.Type = HairTypeId( bWoman, hairGroup );
    static const char *const kBeardTypes[] = { "goatee", "moustache", "beard", "shave" };
    pilosity.Type = kBeardTypes[beardGroup];
}

void HairUi::refillCuts( Look &look, int32_t part ) {
    look.Items.clear();
    const int32_t group = GroupOfType( look.Type );
    const int32_t sex = PlayerClothesSex();
    for ( const ClotheItem &item : GetClothes() ) {
        if ( item.PartType != part || item.Sex != sex || item.Price >= 999999 || !IsStockedAtAreaLevel( item.MinAreaLevel ) ) {
            continue;
        }
        const int32_t itemGroup = part == kPartHair ? HairGroupOf( item ) : BeardGroupOf( item );
        if ( itemGroup == group ) {
            look.Items.push_back( &item );
        }
    }
    look.Index = 0;
    // The worn cut first when it is of this type.
    for ( size_t i = 0; i < look.Items.size(); i++ ) {
        if ( look.Items[i]->Id == look.BaseId ) {
            look.Index = static_cast<int32_t>( i );
        }
    }
}

void HairUi::pushLook( const Look &look, const char *pCutVar, const char *pColorVar ) {
    std::string cut = look.Items.empty() ? std::string( "Clean shaven" ) : look.Items[static_cast<size_t>( look.Index )]->Name;
    pUi->setVariable( pCutVar, cut );
    pUi->setVariable( pColorVar, "Color " + std::to_string( look.Color + 1 ) );
}

int32_t HairUi::priceOf( const Look &look ) const {
    if ( !look.bChanged || look.Items.empty() ) {
        return 0;
    }
    const ClotheItem &item = *look.Items[static_cast<size_t>( look.Index )];
    return item.Id == look.BaseId && look.Color == look.BaseColor ? 0 : item.Price;
}

void HairUi::pushPrice() {
    const int32_t total = priceOf( hair ) + priceOf( pilosity );
    const PlayerProfile &profile = GetPlayerProfile();
    pUi->setVariable( "/:TotalPriceStr", FormatMoney( total ) );
    pUi->setVariable( "/:MyMoneyStr", FormatMoney( profile.Money ) );
    pUi->setVariableBool( "/:EnoughMoney", profile.Money >= total );
}

void HairUi::openPage( int32_t part ) {
    Look &look = part == kPartHair ? hair : pilosity;
    if ( look.Items.empty() ) {
        refillCuts( look, part );
    }
    if ( part == kPartHair ) {
        pUi->setVariable( "/:HairType", look.Type );
        pushLook( look, "/:HairCut", "/:HairColor" );
    } else {
        pUi->setVariable( "/:PilosityType", look.Type );
        pushLook( look, "/:PilosityCut", "/:PilosityColor" );
    }
    pushPrice();
}

void HairUi::previewLook() const {
    if ( !lookSink ) {
        return;
    }
    auto idOf = []( const Look &look ) {
        if ( look.bChanged ) {
            return look.Items.empty() ? -1 : look.Items[static_cast<size_t>( look.Index )]->Id;
        }
        return look.BaseId;
    };
    OTDU_LOG_INFO( "HairUi: preview hair %d colour %d, facial hair %d colour %d\n", idOf( hair ), hair.bChanged ? hair.Color : hair.BaseColor,
                   idOf( pilosity ), pilosity.bChanged ? pilosity.Color : pilosity.BaseColor );
    lookSink( idOf( hair ), hair.bChanged ? hair.Color : hair.BaseColor, idOf( pilosity ),
              pilosity.bChanged ? pilosity.Color : pilosity.BaseColor );
}

void HairUi::pay() {
    const int32_t total = priceOf( hair ) + priceOf( pilosity );
    PlayerProfile &profile = GetPlayerProfile();
    if ( ( !hair.bChanged && !pilosity.bChanged ) || profile.Money < total ) {
        return;
    }
    profile.Money -= total;
    if ( hair.bChanged && !hair.Items.empty() ) {
        hair.BaseId = hair.Items[static_cast<size_t>( hair.Index )]->Id;
        profile.WornClothes[kPartHair] = hair.BaseId;
        profile.HairColor = hair.Color;
        hair.BaseColor = hair.Color;
    }
    if ( pilosity.bChanged ) {
        if ( pilosity.Items.empty() ) {
            profile.WornClothes.erase( kPartBeard );
            pilosity.BaseId = -1;
        } else {
            pilosity.BaseId = pilosity.Items[static_cast<size_t>( pilosity.Index )]->Id;
            profile.WornClothes[kPartBeard] = pilosity.BaseId;
        }
        profile.BeardColor = pilosity.Color;
        pilosity.BaseColor = pilosity.Color;
    }
    hair.bChanged = pilosity.bChanged = false;
    // [CODE-dev 0xba0ef0] one haircut bought: counter + COLLECTION_HAIRCUTS_<n> (2 / 5 / 10 [DATA]).
    profile.NbHaircuts++;
    AwardCollectionMilestones( "COLLECTION_HAIRCUTS", profile.NbHaircuts );
    OTDU_LOG_INFO( "HairUi: paid $%d (hair %d colour %d, facial hair %d; money now $%d)\n", total, hair.BaseId, hair.BaseColor,
                   pilosity.BaseId, profile.Money );
    SaveProfile( "hairdresser" );
}

bool HairUi::onCommand( const std::string &command ) {
    if ( pUi == nullptr || command.compare( 0, 5, "hadr:" ) != 0 ) {
        return false;
    }
    const std::string rest = command.substr( 5 ); // "edit:maha:cuts:scne"
    if ( rest.compare( 0, 5, "edit:" ) != 0 ) {
        return true; // hadr:open, hadr:clos
    }
    const std::string edit = rest.substr( 5 );
    if ( edit == "open" ) {
        pushPrice();
        return true;
    }
    if ( edit == "vali" ) {
        pay();
        pushPrice();
        return true;
    }
    if ( edit == "canc" ) {
        hair.bChanged = pilosity.bChanged = false;
        hair.Items.clear();
        pilosity.Items.clear();
        hair.Color = hair.BaseColor;
        pilosity.Color = pilosity.BaseColor;
        previewLook();
        pushPrice();
        return true;
    }
    // "maha:open", "woha:open", "pilo:open", "<page>:<row>:scne|scpr"
    const size_t colon = edit.find( ':' );
    if ( colon == std::string::npos ) {
        return true;
    }
    const std::string page = edit.substr( 0, colon );
    const std::string tail = edit.substr( colon + 1 );
    const int32_t part = page == "pilo" ? kPartBeard : kPartHair;
    if ( page != "maha" && page != "woha" && page != "pilo" ) {
        return true; // eybr: and the page-level vali / canc: nothing to do
    }
    Look &look = part == kPartHair ? hair : pilosity;
    if ( tail == "open" ) {
        openPage( part );
        return true;
    }
    const bool bNext = tail.size() > 5 && tail.compare( tail.size() - 5, 5, ":scne" ) == 0;
    const bool bPrev = tail.size() > 5 && tail.compare( tail.size() - 5, 5, ":scpr" ) == 0;
    // The style row (icon box) sends a bare "<page>:type" - no scne / scpr - after writing the new type itself [DATA: log].
    const bool bTypeRow = tail.compare( 0, 4, "type" ) == 0;
    if ( !bNext && !bPrev && !bTypeRow ) {
        return true;
    }
    const int32_t step = bNext ? 1 : -1;
    const std::string row = tail.substr( 0, tail.find( ':' ) );
    const char *pTypeVar = part == kPartHair ? "/:HairType" : "/:PilosityType";
    if ( row == "type" ) {
        // The icon box wrote the new type into _root.<Type> itself.
        const std::string type = pUi->getVariable( pTypeVar );
        if ( !type.empty() ) {
            look.Type = type;
        }
        refillCuts( look, part );
        look.bChanged = true;
    } else if ( row == "cuts" && !look.Items.empty() ) {
        const int32_t count = static_cast<int32_t>( look.Items.size() );
        look.Index = ( look.Index + step + count ) % count;
        look.bChanged = true;
    } else if ( row == "colo" ) {
        look.Color = ( look.Color + step + kNumColors ) % kNumColors;
        look.bChanged = true;
    }
    if ( part == kPartHair ) {
        pushLook( look, "/:HairCut", "/:HairColor" );
    } else {
        pushLook( look, "/:PilosityCut", "/:PilosityColor" );
    }
    previewLook();
    pushPrice();
    return true;
}
