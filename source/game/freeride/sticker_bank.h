#pragma once

// The retail sticker packs (DBStickersBanks + Euro/Bnk/scenarics/packs/
// stickers/<_bankfile>.bnk): a bank holds one art texture per sticker
// (maps/<bank>_NN.2db), one material per sticker in its own MAT_ library
// .2dm (MAT_01.., the art on the COLOR slot, an icon texture on IC_NB, a
// FADEALFA parameter, two-sided) and the unit quad the retail editor stamped
// onto the car paint's USER_TEX canvas (TD_CarPaint2 samples s5 "stickers"
// over the paint, paint_spec.md 4 / 6). The retail shop code that drove them
// is [ENC] (car_shop.cpp: the tables are read, the shop itself is not); the
// port reads the packs directly: the art goes onto a per-car USER_TEX canvas.
//
// DBStickersBanks [DATA]: _bankfile, _name_hc, _type (1 = the shop packs,
// 4 casino / 5 chase = specials that no shop sells), _price (the pack),
// _monochrome (the art is recolourable), _isvalid (0 = shipped-but-off:
// Fantastic, Pinky, Elements, Racing, Animals, Vegetal 2, the Club pack).

struct GPUTexture;
class Bank;
struct RenderTarget;
class RenderDevice;
class Renderer2D;

struct StickerBankInfo {
    std::string BankFile;     // _bankfile ("st_basic_nb")
    std::string Name;         // _name_hc resolved through the localization
    int32_t Type = 0;
    int32_t Price = 0;
    bool bMonochrome = false;
    bool bValid = false;
};

struct StickerEntry {
    std::string Name;            // the MAT_%02d material name ("MAT_01")
    GPUTexture *pArt = nullptr;  // the COLOR slot texture (the decal art)
    GPUTexture *pIcon = nullptr; // the IC_NB slot texture (the shop icon)
    int32_t Width = 0;
    int32_t Height = 0;
};

// One placed sticker on a car's USER_TEX canvas. U / V = the sticker's
// centre in the canvas' UV space, Scale = its width as a fraction of the
// canvas, AngleDeg = the rotation in the canvas plane, Tint = the RGBA8
// multiply colour (MakeRGBA order; monochrome packs take the colour, colour
// packs ignore it).
struct StickerPlacement {
    std::string Bank;    // DBStickersBanks._bankfile ("st_fun01")
    int32_t Index = 0;   // the entry in the bank (the MAT_ library order)
    float U = 0.5f, V = 0.5f;
    float Scale = 0.22f;
    float AngleDeg = 0.0f;
    uint32_t Tint = 0xffffffffu;
};

// The per-car sticker canvas: a 1024x1024 transparent render target the
// placed stickers composite into (the dash GPS screen's RTT pattern), bound
// to the car paint materials' sticker slot (CinematicCar::bindStickerTexture,
// the TD_CarPaint2 "STIKERS" sampler s5 - paint_spec.md 4/6). The retail
// editor composited its stamps the same way [INF: the art ships per-sticker
// with a quad; the shader lerps one texture over the paint UVs].
class StickerCanvas {
public:
    ~StickerCanvas();
    // The layout to composite (placements + their art textures, parallel;
    // empty = no canvas). Marks the canvas dirty.
    void setLayout( const std::vector<StickerPlacement> &layout, const std::vector<GPUTexture *> &art );
    // Composites when dirty (call before the car draws; a clean canvas is a
    // no-op - the car samples the target, not this call).
    void render( RenderDevice &device, Renderer2D &renderer );
    inline bool hasLayout() const { return !layout.empty(); }
    GPUTexture *getTexture();
    void destroy();

private:
    bool ensureTarget( RenderDevice &device );

    RenderTarget *pTarget = nullptr;
    std::vector<StickerPlacement> layout;
    std::vector<GPUTexture *> artTextures;
    bool bDirty = false;
};

class StickerBankCatalog {
public:
    static StickerBankCatalog &get();

    // Reads the DBStickersBanks rows (once; -logstickers dumps them).
    void load();
    inline bool isLoaded() const { return bLoaded; }
    inline size_t getBankCount() const { return banks.size(); }
    inline const StickerBankInfo &getBank( size_t index ) const { return banks[index]; }
    // A bank whose _isvalid is 0 is listed but never loaded/sold; the type 4
    // (casino) / 5 (chase) specials no shop sells either.
    bool isBankSold( size_t index ) const;

    // The art + icon textures of one bank, loaded once into the TextureCache
    // (owner "stickers/<bankfile>", released by shutdown()). Null when the
    // bank is missing or fails to load (entries stay empty).
    const std::vector<StickerEntry> *openBank( size_t index );

    // -nostickers: the whole feature off (the shop menu hides the page and
    // spawn-time application is skipped).
    static bool isEnabled();

    void shutdown();

private:
    StickerBankCatalog() = default;

    std::vector<StickerBankInfo> banks;
    struct LoadedBank {
        Bank *pBank = nullptr; // released at shutdown (TextureCache borrow)
        std::vector<StickerEntry> entries;
        bool bFailed = false;
    };
    std::vector<LoadedBank> loaded; // parallel to banks
    bool bLoaded = false;
};

// The art texture of one placement (loads the bank through the catalog,
// cached; null when the bank / entry is gone). StickerResolveArt resolves a
// whole layout in order.
GPUTexture *StickerArt( const StickerPlacement &placement );
void StickerResolveArt( const std::vector<StickerPlacement> &layout, std::vector<GPUTexture *> &outArt );
