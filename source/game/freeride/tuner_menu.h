#pragma once

#include "core/math.h"

#include <string>
#include <vector>

class CinematicCar;
class Renderer2D;
struct TunerPart;
struct TunerFinish;
struct TunerFlame;

// The tuner garage's menu (native; the reused tuning-shop interior's seller
// opens it at the mod's ETNR_* spots). The car you drove in with stands on the
// room's car place (the turntable camera); the panel is a stack of cards - the
// spoiler and its finish, the exhaust tips and their finish, the pop flames -
// each previewed live on the room car, and a validate buys / saves the one
// that is selected (the tuning-shop pricing [INF]). Parts are saved per owned
// car (PlayerProfile::OwnedCarTunerParts, slot -> part id; `<slot>_finish` and
// `exhaust_flames` ride in the same map) and applied to the real car by
// GMFreeRide::applyTunerParts (at spawn, on menu close, and on walking out of
// the shop).
class TunerMenu {
public:
    // pPreviewCar: the room car the previews land on (null: the menu sells
    // without a live preview - the change shows when you leave the shop).
    void open( int32_t newCarId, const std::string &name, CinematicCar *pPreviewCar );
    // The shop's name for the title bar ("Wangan Works" / "Rev Yard").
    inline void setShopName( const std::string &name ) { shopName = name; }
    void close();
    inline bool isOpen() const { return carId >= 0; }
    // Keys: up / down pick the card, left / right cycle its options (stock
    // first), validate buys / saves what is previewed, back closes. The
    // previews apply at once.
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // Inside the shop: the panel at the right, the scene undimmed.
    void setSidePanel( bool bSide ) { bSidePanel = bSide; }

private:
    enum class eKind : uint8_t {
        Part,    // a catalog part of a slot (priced)
        Finish,  // the look of the part fitted in the slot (free)
        Flames,  // the pop-flame style of the fitted exhaust (free)
    };
    // One card of the panel.
    struct SlotRow {
        eKind Kind = eKind::Part;
        std::string Slot; // "spoiler" / "exhaust" (a Finish row: the slot it dresses)
        std::vector<const TunerPart *> Parts; // Part: the car's catalog for the slot
        int32_t Preview = -1;                 // Part: previewed now (-1 = stock)
        int32_t Fitted = -1;                  // Part: what the profile wears
        // Finish / Flames: the options (finishes of the slot / the flame
        // styles). Preview / Fitted index them; -1 = the mesh's own material
        // (Finish) or flames off (Flames).
        std::vector<const TunerFinish *> Finishes;
        std::vector<Vector3f> Swatches; // one colour per option (Finish) or Body colour (Flames)
        std::vector<Vector3f> SwatchCores; // Flames: the hot centre of each style
        int32_t OptionPreview = -1;
        int32_t OptionFitted = -1;
    };
    // Previews a part of the row on the room car (-1 = stock). The slot's
    // finish row is re-applied on top.
    void preview( SlotRow &row, int32_t index );
    // Previews a finish on the room car's fitted part of the slot (-1 = the
    // mesh's own material). No tuner part on the car = nothing to do.
    void previewFinish( SlotRow &row, int32_t index );
    // The installed part of the profile for a slot, as a parts index (-1 =
    // stock / unknown id).
    int32_t installedIndex( const SlotRow &row ) const;
    // The installed finish of the row's slot, as an index into row.Finishes
    // (-1 = none / unknown id).
    int32_t installedFinish( const SlotRow &row ) const;
    // The installed flame style (index into the catalog's flames; -1 = off;
    // the first style when nothing is saved).
    int32_t installedFlame() const;
    // True when a Part row of the slot has a part fitted (saved).
    bool hasFitted( const char *pSlot ) const;
    // The row index of the Part row of a slot (rows.size() = none).
    size_t partRowOf( const char *pSlot ) const;

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
