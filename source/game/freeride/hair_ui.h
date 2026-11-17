#pragma once

#include <cstdint>
#include <string>
#include <vector>

class FlashPlayer;
struct ClotheItem;

// The retail hairdresser screen: SHOPSAVATAR.vmf's hairdresser page (root labels
// hairdresser / hairdresser_stop / hairdresser_edit / close_hairdresser) driven
// by its own scripts; this class is the game's half of the `hadr:` FSCommand
// protocol [DATA: shopsavatar.vmf AVM1, docs/re/avatar_shops_spec.md]:
//   hadr:open                 the stylist's A (the movie then waits for openMenuHairDresser)
//   hadr:edit:open            the square menu is up (/:isWoman picks manhaircut / womanhaircut)
//   hadr:edit:<maha|woha|pilo>:open   a page opened: the rows read _root.HairType / HairCut /
//                             HairColor (PilosityType / Cut / Color)
//   hadr:edit:<page>:<type|cuts|colo>:scne|scpr   a choice box moved: the game answers by writing
//                             the new display text into that _root variable (the box reads it back)
//   hadr:edit:vali / canc     pay / leave
// Haircuts are the DBClothe hair rows (_parttype 3), beards / moustaches / goatees the facial hair
// rows (_parttype 9) [DATA]; the tint is a HAIR_MAT palette index.
class HairUi {
public:
    void bind( FlashPlayer *pNewUi );
    inline bool isBound() const { return pUi != nullptr; }
    // Returns true when the command was one of this page's.
    bool onCommand( const std::string &command );

private:
    // One editable look: hair (part 3) or facial hair (part 9).
    struct Look {
        std::string Type;             // the movie's type id ("short", "w_afro", "goatee" ...)
        std::vector<const ClotheItem *> Items; // the cuts of the current type (empty item list = "clean")
        int32_t Index = 0;
        int32_t Color = 0;
        bool bChanged = false;
        int32_t BaseId = -1;          // worn now (-1: unknown / none)
        int32_t BaseColor = 0;
    };
    void openPage( int32_t part );
    void refillCuts( Look &look, int32_t part );
    void pushLook( const Look &look, const char *pCutVar, const char *pColorVar );
    int32_t priceOf( const Look &look ) const;
    void pushPrice();
    void pay();

    FlashPlayer *pUi = nullptr;
    bool bWoman = false;
    Look hair, pilosity;
};
