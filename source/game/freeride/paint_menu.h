#pragma once

#include "game/cinematic/cinematic_car.h"
#include "game/freeride/sticker_bank.h"

#include <string>
#include <vector>

class Renderer2D;

// The sticker / paint shop menu (native; the retail one is the shopscar
// movie, not driven). Opens at a sticker shop (ESTICK_*, the seller
// selectable Type 24/25/28 "npc_stickers"). Two pages:
//   PAINT    - the car you drove in with's catalogue (its DBCarsAndColors
//              rows, the dealer colour picker's data), repainted for the
//              row's price; the chosen row is saved
//              (PlayerProfile::OwnedCarPaints) like a dealer order.
//   STICKERS - the retail decal packs (DBStickersBanks): browse a pack's
//              stickers, place them on the car you drove in with (the view
//              cuts outside to it - the retail editor's garage-car camera),
//              move / scale / rotate live, recolour the monochrome packs,
//              buy per sticker (the pack's _price [INF: the retail
//              per-sticker pricing is not decoded]) and remove placed ones.
//              The layout persists per car (PlayerProfile::OwnedCarStickers)
//              onto the paint's sticker canvas (StickerCanvas -> the
//              TD_CarPaint2 STIKERS slot).
// Keys: Left / Right switches pages (when not editing); Up / Down selects;
// A validates / places; B goes back, at the shop root it leaves; X / Y are
// the sticker editor's colour cycle / remove (the retail shopscar's own
// action names are not in the readable data).
class PaintMenu {
public:
    void open( int32_t newCarId, const std::string &name );
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
    // The sticker side: the canvas of the car the shop edits (its layout is
    // the shop's stock of placed stickers) and the car's id for the profile.
    void setStickerContext( StickerCanvas *pCanvas, int32_t stickerCarId );
    // True while a sticker is being placed (the live editor): the view cuts
    // to the car (the freeride scene) and the panel shrinks to a hint strip.
    inline bool isStickerPlacing() const { return isOpen() && page == Page::Stickers && state == StickerState::Place; }

private:
    enum class Page { Paint, Stickers };
    enum class StickerState { BankList, Grid, Place, Placed };

    // The sticker page's shared plumbing: the profile layout of the car.
    std::vector<StickerPlacement> &stickerLayout();
    void resolveArt( const std::vector<StickerPlacement> &layout, std::vector<GPUTexture *> &outArt ) const;
    // One art texture of a placement (the catalog's openBank, cached).
    GPUTexture *artOf( const StickerPlacement &placement ) const;
    void pushLayoutToCanvas();
    void saveStickers( const char *pReason );
    void tickStickers( float deltaTime );
    void drawStickers( Renderer2D &renderer, float x0, float y0, float w, float h, float textSize );

    int32_t carId = -1;
    std::string carName;
    std::vector<CinematicCar::PaintInfo> paints;
    int32_t selected = 0;
    bool bSidePanel = false;
    int32_t appliedPaint = -1;
    std::string message;
    float messageTimer = 0.0f;

    Page page = Page::Paint;
    StickerCanvas *pStickerCanvas = nullptr;
    int32_t stickerCarId = -1;
    StickerState state = StickerState::BankList;
    std::vector<size_t> soldBanks;   // the catalog indices the shop sells
    size_t bankSelected = 0;
    size_t gridSelected = 0;         // the sticker in the open bank
    size_t placedSelected = 0;       // the row in the placed list
    int32_t editIndex = -1;          // the placement being placed (-1 = none)
    StickerPlacement editBackup;     // the values before this edit (B cancels)
    bool bEditNew = false;           // a sticker being placed is a new buy
    size_t tintIndex = 0;            // the palette position (monochrome packs)
};
