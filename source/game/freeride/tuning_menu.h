#pragma once

#include <string>

class Renderer2D;

// The tuning shop menu (native; the retail one is the shopsca movie, not
// driven). Opens at a tuning shop (ETUN_*, the seller selectable Type 23
// "npc_tuner"), sells the three tuning levels of the car you drove in with
// (accel / braking / speed, 0..3 each - the DBRewardCar level range) and its
// rim size (the mechanical "plus sizing": a bigger rim keeps the car's
// rolling radius and gets the matching, thinner tire sidewall). The levels
// are stored per owned car (PlayerProfile::OwnedCarTuning) and modulate the
// car's physics (ApplyTuningToSetup; gm_freeride's applyTuning applies them
// at spawn and when the player gets back in the car after a purchase); the
// rim size is stored in PlayerProfile::OwnedCarRimSizes and applied to the
// model / the tire meshes by GMFreeRide (no reload).
class TuningMenu {
public:
    void open( int32_t carId, const std::string &carName );
    void close();
    inline bool isOpen() const { return carId >= 0; }
    // Keys: up / down select, left / right change the rim size, validate buys
    // the next level or fits the rim size, back closes.
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // Inside the shop: the panel at the right, the scene undimmed.
    void setSidePanel( bool bSide ) { bSidePanel = bSide; }

private:
    int32_t carId = -1;
    std::string carName;
    int32_t selected = 0;
    bool bSidePanel = false;
    std::string message;
    float messageTimer = 0.0f;
    // The rim size row: the diameters the shop offers (0 = the car has no
    // DBRims row), the one being previewed and the one fitted (0 = stock).
    int32_t rimMin = 0;
    int32_t rimMax = 0;
    int32_t rimPreview = 0;
    int32_t rimInstalled = 0;
};
