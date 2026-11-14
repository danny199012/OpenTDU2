#pragma once

#include "game/freeride/sticker_bank.h"
#include "game/player_profile.h"

#include <cstdint>
#include <string>
#include <vector>

class FlashPlayer;
class CinematicCar;

// The game's half of the retail sticker shop (docs/re/stickers_ui_spec.md).
//
// The shop UI is SHOPSCAR.vmf's own "stickers" page: its scripts run the menu
// (Paint / Stick / Clear / Cancel / Valid & Pay), the category grid, the tool
// strip (Move / Rotation / Scale / Transparency / Colour), the history and the
// invoice, and report to the game through the stsh: FSCommand family. This
// class answers them: it owns the working layout of the car's stickers, writes
// the variables the movie reads (the category, the prices, the tool values),
// feeds the grid thumbnails into the squares' `lure` placeholders, drives the
// sticker being edited from the pad / mouse and pays / saves / cancels.
//
// The movie's commands are [DATA] (the AVM1 of shopscar.vmf, decoded); what
// the retail game did on each is [ENC], so the behaviour here is [INF].
class StickerShopUi {
public:
    // The shop room is up: the movie `pUi` shows the stickers page, `pCanvas`
    // is the car's sticker canvas (the working layout composites into it), the
    // profile layout of `carId` is the committed state.
    // `pCar` (may be null) is the car the Paint page recolours live.
    void bind( FlashPlayer *pUi, StickerCanvas *pCanvas, int32_t carId, CinematicCar *pCar = nullptr );
    // The shop room is gone: an unpaid layout reverts to the committed one.
    void unbind();
    inline bool isBound() const { return pUi != nullptr; }

    // A stsh: command of the movie ("stsh:stic:list:vali" ...). Other FSCommands
    // are the caller's; returns true when this class took it.
    bool onCommand( const std::string &command );
    void tick( float deltaTime );

    // The movie's menu camera is "in" (stsh:mcin .. stsh:mcou): the car is the
    // view (the editor's garage-car camera) instead of the shop room.
    inline bool wantsCarView() const { return bCarView; }

private:
    enum class eTool { Move, Rotate, Scale, Alpha, Colour };

    // The category (pack) list: the DBStickersBanks rows the shop sells.
    void buildCategories();
    void loadCategory( int32_t category );
    void bindThumbnails();
    // The sticker being edited (the movie's put_stickers page).
    void beginEdit( int32_t index );
    void endEdit( bool bKeep );
    void applyEdit();
    void pushToCanvas();
    void pushHistory();
    void undo();
    void redo();
    void clearAll();
    // The money / invoice variables of the movie.
    int32_t stickerCost() const;
    void refreshPrices();
    void writeEditStrings();
    void writeModifiedFlags();
    void pay();
    void revert();
    // The colour tool's "My Colors" box (the movie's /stickers/colors).
    void writeMyColours();
    uint32_t pickedColour() const;
    void tintCube( const std::string &clipPath, uint32_t colour );
    // The picker's gradient (hue across, white -> colour -> black down) the
    // movie's lure_grad shows, and the colour under the cursor.
    static uint32_t GradientColour( float u, float v, float val );
    static float BrightnessInput();
    float activeValue() const;
    void bindGradient();
    void placeCursor();
    void applyPickedToSticker();
    // The colour tool's movie root: /stickers/colors (the sticker's colour)
    // or /paint (the Paint page's own box, the same clips).
    std::string colourRoot() const;
    static void CursorFromColour( uint32_t colour, float &u, float &v, float &val );
    // The Paint page (SHOPSCAR /paint, docs/re/stickers_ui_spec.md 7): the
    // body colour, the optional second (bicolour) colour and the finish type
    // recolour the car live; Valid & Pay keeps them.
    bool onPaintCommand( const std::string &command );
    void tickPaint( float deltaTime, float ax, float ay );
    void applyPaintToCar();
    void writePaintType();
    bool paintDiffers() const;
    uint32_t activePaintColour() const;
    int32_t selectedSlot() const;
    std::string formatMoney( int32_t value ) const;
    bool getFlag( const char *pVariable ) const;

    FlashPlayer *pUi = nullptr;
    StickerCanvas *pCanvas = nullptr;
    int32_t carId = -1;

    std::vector<StickerPlacement> committed; // the profile layout when the shop opened
    std::vector<StickerPlacement> working;   // committed + this visit's applied stickers
    std::vector<std::vector<StickerPlacement>> history;
    int32_t historyAt = 0; // history[historyAt] is the current layout

    std::vector<size_t> categories; // catalog indices of the sold banks
    int32_t category = 0;
    bool bListOpen = false;

    // The sticker under the tools (index into `working`, -1 = none).
    int32_t editIndex = -1;
    eTool tool = eTool::Move;
    bool bCarView = false;
    bool bThumbsBound = false;
    int32_t thumbCategory = -1;

    // The colour tool: the box is open, and the dial that replaces the
    // pipette (hue / brightness of the colour about to be saved).
    bool bMyColoursOpen = false; // the colour tool's panel is up (colo:open .. colo:clos)
    float cursorU = 0.5f, cursorV = 0.0f, cursorVal = 1.0f; // the picker cursor in the gradient (0..1)

    // The Paint page.
    CinematicCar *pCar = nullptr;
    bool bPaintOpen = false;    // pain:colo:open .. pain:colo:clos
    bool bPaintCustom = false;  // the working paint is a custom one (else the dealer's)
    PlayerProfile::CustomPaint paintWork;
    bool bCommittedCustom = false;
    PlayerProfile::CustomPaint paintCommitted;
    bool bBackupCustom = false; // the state when the page opened (B restores it)
    PlayerProfile::CustomPaint paintBackup;
    float paintU[2] = { 0.0f, 0.0f }, paintV[2] = { 0.0f, 0.0f }, paintVal[2] = { 1.0f, 1.0f }; // each colour's picker cursor

    // The invoice of this visit.
    int32_t labourCost = 0;
    int32_t paintCost = 0;
    int32_t eraseCost = 0;
    bool bPaintChanged = false;
};
