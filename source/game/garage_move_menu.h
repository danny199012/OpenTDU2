#pragma once

#include <string>
#include <vector>

class Renderer2D;

// "Move a vehicle" between the garages of the owned houses. The retail screen was not found in the movies (the
// SHOPSCAR / Home garage pages only carry the "Select vehicle / Move vehicle" hint bar), so this is a native
// panel [INF]: pick a car of this house garage, then the house it goes to (a house with a free place).
// Opened from a house garage interior; closes by itself when done.
class GarageMoveMenu {
public:
    // fromHouse: the DBHouse._spotname of the garage the player stands in.
    void open( const std::string &fromHouse );
    void close();
    inline bool isOpen() const { return bOpen; }
    // True once after a car was moved (the garage room shows its cars again).
    inline bool takeMoved() { const bool b = bMoved; bMoved = false; return b; }
    // Up / down select, validate goes on / moves, back goes back / closes. Call every frame while open.
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );

private:
    enum class eStep { Car, House };
    struct HouseChoice {
        std::string Spot;
        std::string Name;
        int32_t Cars = 0;
        int32_t Capacity = 0;
    };
    void rebuild();

    bool bOpen = false;
    bool bMoved = false;
    eStep step = eStep::Car;
    std::string fromHouse;
    std::string fromName;
    std::vector<int32_t> carIds;
    std::vector<HouseChoice> houses;
    int32_t selected = 0;
    int32_t carSelected = 0;
    std::string message;
    float messageTimer = 0.0f;
};
