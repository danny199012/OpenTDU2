#pragma once

#include <string>

class Renderer2D;

// The gas pump screen (native). The retail one is the disabled feature of
// docs/re/fuel_gas_station_spec.md: its whole text is the game's own
// localization (ids 1635..1645), read here with LocStr - "Gas station",
// "Shell basic", "Shell v-power", "Fill", "Bill", "Breakdown" - so the screen
// speaks the retail wording without shipping any of it.
//
// It opens in the player's house garage (the only gas pump model in the game is
// the gph_l2d garage one) and at a station placed from the mods overlay (a
// `fuel` prop, see docs/re/modding.md 3).
class FuelMenu {
public:
    // pWhere: where the player is filling ("your garage" / the prop's name).
    void open( const std::string &where );
    void close();
    inline bool isOpen() const { return bOpen; }

    // Keys: up / down pick the grade, validate fills (pays the bill), back closes.
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // The line the caller shows while the menu is closed ("Fill" / "Breakdown").
    const std::string &getHint() const { return hint; }

private:
    bool bOpen = false;
    int32_t selected = 0; // 0 Shell basic, 1 Shell v-power
    bool bConfirmReplace = false;
    std::string where;
    std::string hint;
    std::string message;
    float messageTimer = 0.0f;
    float lastBill = 0.0f;

    void fill();
};
