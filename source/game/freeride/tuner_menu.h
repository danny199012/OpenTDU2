#pragma once

#include "core/math.h"

#include <string>
#include <vector>

class CinematicCar;
class Renderer2D;
struct TunerPart;

// The tuner garage's menu (native; the reused tuning-shop interior's seller
// opens it at the mod's ETNR_* spots). The car you drove in with stands on the
// room's car place (the turntable camera); the panel cycles the catalog's
// parts of the spoiler slot - each previewed live on the room car - and a
// validate buys it (the tuning-shop pricing [INF]). Parts are saved per owned
// car (PlayerProfile::OwnedCarTunerParts, slot -> part id) and applied to the
// real car by GMFreeRide::applyTunerParts (at spawn, on menu close, and on
// walking out of the shop).
class TunerMenu {
public:
    // pPreviewCar: the room car the previews land on (null: the menu sells
    // without a live preview - the change shows when you leave the shop).
    void open( int32_t newCarId, const std::string &name, CinematicCar *pPreviewCar );
    // The shop's name for the title bar ("Wangan Works" / "Rev Yard").
    inline void setShopName( const std::string &name ) { shopName = name; }
    void close();
    inline bool isOpen() const { return carId >= 0; }
    // Keys: left / right cycle the spoiler (stock first), validate buys what
    // is previewed, back closes. The previews apply at once.
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // Inside the shop: the panel at the right, the scene undimmed.
    void setSidePanel( bool bSide ) { bSidePanel = bSide; }

private:
    // One row of the panel: a slot the car takes (spoiler, exhaust tips...).
    // The finish row (Slot "exhaust_finish", right under the exhaust row)
    // cycles the tip finishes instead of parts.
    struct SlotRow {
        std::string Slot;                     // "spoiler" / "exhaust" / "exhaust_finish"
        std::vector<const TunerPart *> Parts; // the car's catalog for the slot
        int32_t Preview = -1;                 // previewed now (-1 = stock)
        int32_t Fitted = -1;                  // what the profile wears
        // The finish row only: the index into the catalog's finishes, -1 =
        // the mesh's own material.
        int32_t FinishPreview = -1;
        int32_t FinishFitted = -1;
    };
    // Previews a part of the row on the room car (-1 = stock). The exhaust
    // row re-applies the finish row's previewed finish on top.
    void preview( SlotRow &row, int32_t index );
    // Previews a tip finish on the room car's fitted exhaust (-1 = the mesh's
    // own material). No tuner exhaust on the car = nothing to do.
    void previewFinish( SlotRow &row, int32_t index );
    // The installed part of the profile for a slot, as a parts index (-1 =
    // stock / unknown id).
    int32_t installedIndex( const SlotRow &row ) const;
    // The installed tip finish, as a finishes index (-1 = none / unknown id).
    int32_t installedFinish() const;

    std::vector<SlotRow> rows;
    size_t selected = 0; // the row up / down moved to
    int32_t carId = -1;
    std::string carName;
    std::string shopName = "Wangan Works";
    CinematicCar *pCar = nullptr; // the room car (borrowed; not owned)
    bool bSidePanel = false;
    std::string message;
    float messageTimer = 0.0f;
};
