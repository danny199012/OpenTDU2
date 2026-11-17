#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Renderer2D;

// The cosmetic clinic's menu (ESUR_A / ESUR_CA, the surgeon at the desk;
// docs/re/surgery_spec.md). Native: the retail screen is the SHOPSAVATAR
// movie's surgeon panel (GAME_MODE_SURGERY), not driven yet. One row per
// DBSurgeryCost item the port can apply - the FaceFX sliders (the .avt
// FaceParamArray bytes, value / 100), the age (CustPos Older) and FRECKLES
// floats; EYE_COLOR / EYE_BROWS_TYPE / TINT need the EyeID / EyebrowsID /
// skin atlas the avatar does not read yet and are not offered. Left / right
// move a slider by one notch, validate pays for every changed row (_cost
// each) and saves the face, back leaves without buying.
class SurgeryMenu {
public:
    // The player's current face: the saved edit, else the preset .avt.
    void open( const std::string &shopName, const int8_t *pBaselineParams, float baselineAge, float baselineFreckle );
    void close();
    inline bool isOpen() const { return bOpen; }
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // Inside the shop: the panel at the right, the scene undimmed.
    void setSidePanel( bool bSide ) { bSidePanel = bSide; }

    // The retail gates (surgery_spec.md 1.5): true while the bandages are on.
    static bool IsBandaged();

private:
    struct Row {
        std::string Label;
        int32_t Cost = 0;
        int32_t Kind = 0;  // 0 face slider, 1 age, 2 freckles
        int32_t Index = 0; // FaceParamArray slot
    };
    int32_t rowValue( const Row &row ) const;
    int32_t rowBase( const Row &row ) const;
    int32_t totalPrice( int32_t *pChanged ) const;
    void buy();

    bool bOpen = false;
    bool bSidePanel = false;
    std::string shopName;
    std::vector<Row> rows;
    int8_t baseParams[49] = {};
    int8_t params[49] = {};
    float baseAge = 0.0f, baseFreckle = 0.0f;
    float age = 0.0f, freckle = 0.0f;
    int32_t selected = 0;
    std::string message;
    float messageTimer = 0.0f;
};
