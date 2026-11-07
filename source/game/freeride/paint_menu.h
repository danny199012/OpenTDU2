#pragma once

#include "game/cinematic/cinematic_car.h"

#include <string>
#include <vector>

class Renderer2D;

// The sticker / paint shop menu (native; the retail one is the shopsca
// movie, not driven). Opens at a sticker shop (ESTICK_*, the seller
// selectable Type 24/25/28 "npc_stickers"), lists the car you drove in
// with's catalogue (its DBCarsAndColors rows, the dealer colour picker's
// data) and repaints it for the row's price. The chosen row is saved
// (PlayerProfile::OwnedCarPaints) like a dealer order.
class PaintMenu {
public:
    void open( int32_t carId, const std::string &carName );
    void close();
    inline bool isOpen() const { return carId >= 0; }
    // Keys: up / down select, validate buys the paint, back closes.
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // Inside the shop: the panel at the right, the scene undimmed.
    void setSidePanel( bool bSide ) { bSidePanel = bSide; }
    // The paint bought since the last call: the caller repaints the player's
    // car with the row (false when there is none).
    bool takeAppliedPaint( int32_t &outCarId, int32_t &outPaintIndex );

private:
    int32_t carId = -1;
    std::string carName;
    std::vector<CinematicCar::PaintInfo> paints;
    int32_t selected = 0;
    bool bSidePanel = false;
    int32_t appliedPaint = -1;
    std::string message;
    float messageTimer = 0.0f;
};
