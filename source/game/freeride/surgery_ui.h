#pragma once

#include <cstdint>
#include <string>

class FlashPlayer;

// The retail cosmetic clinic screen: SHOPSAVATAR.vmf's surgeon page (root labels
// surgeon / surgeon_stop / surgeon_edit / close_surgeon) driven by its own
// scripts. This class is the game's half of the protocol [DATA: the AVM1 of
// shopsavatar.vmf, docs/re/surgery_spec.md section 5]:
//   surg:cine:intr        the surgeon's A (the movie then waits for openMenuSurgeon)
//   surg:edit:open        the menu is up: the game pushes every slider's value,
//                         the money and the price
//   surg:edit:<part>:open a part page opened
//   surg:edit:chpa        a parameter got the focus (/:VarToFocus names it)
//   surg:edit:<part>:<code>:slid:updt   a slider moved: /:<VarToFocus> is its value
//   surg:edit:rand / canc / vali        Random / leave without paying / pay
// The slider variables are the movie's `_root.<Name>` roots; each maps to one of
// the .avt FaceParamArray slots (kFaceFxCustomNodes) or to the age / freckle float.
class SurgeryUi {
public:
    // pUi: the movie (null unbinds). The player's current face is the baseline.
    void bind( FlashPlayer *pNewUi, const int8_t *pBaselineParams, float baselineAge, float baselineFreckle );
    inline bool isBound() const { return pUi != nullptr; }
    // Returns true when the command was a surg: one.
    bool onCommand( const std::string &command );

private:
    void pushValues();
    void pushPrice();
    int32_t totalPrice( int32_t *pChanged ) const;
    void pay();
    int32_t sliderOf( int32_t byteValue ) const;
    int32_t byteOf( int32_t slider ) const;

    FlashPlayer *pUi = nullptr;
    int8_t baseParams[49] = {};
    int8_t params[49] = {};
    float baseAge = 0.0f, baseFreckle = 0.0f;
    float age = 0.0f, freckle = 0.0f;
};
