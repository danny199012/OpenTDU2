#include "shared.h"
#include "hair_ui.h"

#include "flash/flash_player.h"
#include "game/clothes_shop.h"
#include "game/player_profile.h"
#include "system/cmd_line.h"

void HairUi::bind( FlashPlayer *pNewUi ) {
    pUi = pNewUi;
    // The square menu picks "manhaircut" / "womanhaircut" from /:isWoman [DATA].
    pUi->setVariableBool( "/:isWoman", PlayerClothesSex() == 1 );
}

bool HairUi::onCommand( const std::string &command ) {
    if ( pUi == nullptr ) {
        return false;
    }
    const bool bHair = command.compare( 0, 5, "hadr:" ) == 0;
    // The hair pages' choice boxes send "<cmdPrefix>:scne" / ":scpr" (next / previous).
    const bool bChoice = command.size() > 5 && ( command.compare( command.size() - 5, 5, ":scne" ) == 0 ||
                                                  command.compare( command.size() - 5, 5, ":scpr" ) == 0 );
    if ( !bHair && !bChoice ) {
        return false;
    }
    if ( CmdLine::HasArg( "-loghair" ) ) {
        OTDU_LOG_INFO( "HairUi: %s\n", command.c_str() );
        for ( const char *pName : { "HairType", "HairCut", "HairColor", "PilosityType", "PilosityCut", "PilosityColor",
                                    "EyebrowsType", "EyebrowsColor", "VarToFocus", "paramCmd", "focusValue", "isWoman",
                                    "CategorySelected", "numHair", "BtValid", "Nb_Items" } ) {
            OTDU_LOG_INFO( "HairUi:   %s = '%s'\n", pName, pUi->getVariable( std::string( "/:" ) + pName ).c_str() );
        }
    }
    return true;
}
