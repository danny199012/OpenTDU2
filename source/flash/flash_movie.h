#pragma once

#include "filesystem/bank.h"
#include "flash/vmf_file.h"
#include "render/font.h"
#include "render/renderer_2d.h"

#include <map>

struct GPUTexture;

// A frontend bank (Euro/Bnk/FrontEnd/<Res>/<Name>.bnk): flash movie (.vmf),
// its materials (.2dm) and textures (.2db: glyph atlases, standalone bitmaps
// "fla_<characterId>" and atlas pages "page_<n>").
//
// The SWF display list / ActionScript runtime is not implemented yet: game
// modes currently compose their screens from the movie resources (bitmaps,
// fonts) natively.
class FlashMovie {
public:
    struct BitmapRef {
        GPUTexture *pTexture = nullptr;
        Rect2D UV;
        float Width = 0.0f;
        float Height = 0.0f;
        inline bool isValid() const { return pTexture != nullptr; }
    };

    inline const VMFFile &getVMF() const { return vmf; }
    inline const std::string &getName() const { return name; }
    inline bool isLoaded() const { return bLoaded; }

public:
    FlashMovie();
    ~FlashMovie();

    // pName: "intro", "log_in", "main"... pResolution: "HiRes" / "MedRes" / "LowRes"
    bool load( const char *pName, const char *pResolution = "HiRes" );
    void unload();

    // Adds the textures of another bank to the pages the bitmaps are matched
    // against (the map's brand logos live in AllRes/LogoTexturepage.bnk, not in
    // map.bnk). Must be called before a FlashPlayer builds its fills; a bank
    // already added is skipped. pBankPath: "FrontEnd/AllRes/LogoTexturePage.bnk"
    // relative to the bank folder.
    bool addExtraPages( const char *pBankPath );
    // A page texture of the bank or of an extra bank by name ("brands_map").
    GPUTexture *findPage( const char *pName ) const;

    BitmapRef getBitmap( uint16_t characterId ) const;
    // Raw texture of the bank ("fla_0012", "page_001"...).
    BitmapRef getTexture( const char *pTextureName ) const;

    // Fonts: "Berlin Sans FB", ... (bRegular selects the regular rendition
    // when the font ships both).
    const Font *getFont( const char *pFontName, bool bRegular = true ) const;
    const Font *getDefaultFont() const;
    // CPU pixels of a font's glyph atlas page (fla_<CharacterId>.2db of this
    // bank, mip 0, decoded to RGBA8): for stamping text into generated
    // textures (the world's road-sign faces, sign_text.cpp). False when the
    // font is unknown or its page cannot be decoded.
    bool getFontPagePixels( const char *pFontName, std::vector<uint8_t> &outRGBA, uint32_t &outWidth,
                            uint32_t &outHeight ) const;
    // CPU pixels of a movie's bitmap character (the fla_<id>.2db standalone,
    // decoded to RGBA8): the UI art inspector (-dumpbot writes the badge arts
    // out as PNGs). False when the bitmap is an atlas cell or cannot be
    // decoded (the atlas pages go through getBitmap's page pick instead).
    bool getBitmapPixels( uint16_t characterId, std::vector<uint8_t> &outRGBA, uint32_t &outWidth,
                          uint32_t &outHeight ) const;
    // The same for an ATLAS CELL bitmap: the page is picked by getBitmap's
    // own size-ratio rule over the bank's page_*.2db files, decoded and
    // cropped to the cell's UV rectangle.
    bool getAtlasCellPixels( uint16_t characterId, std::vector<uint8_t> &outRGBA, uint32_t &outWidth,
                             uint32_t &outHeight ) const;

private:
    struct FontEntry {
        std::string Name;
        std::unique_ptr<Font> pRegular;
        std::unique_ptr<Font> pBold;
    };
    // A mods-folder flash-art override: '<mods>/flash/fla_<id>.png' replaces
    // the retail bitmap (the icon remake route — the runtime-composed UI
    // icons get hand-made stand-ins). The texture is owned here.
    struct ModOverride {
        GPUTexture *pTexture = nullptr;
        float Width = 0.0f, Height = 0.0f;
    };
    GPUTexture *findModOverride( uint16_t characterId ) const;

    std::string name;
    std::string owner;
    Bank bank;
    VMFFile vmf;
    std::vector<FontEntry> fonts;
    mutable std::map<uint16_t, ModOverride> modOverrides;
    std::vector<std::pair<std::string, GPUTexture *>> pages;
    std::vector<std::unique_ptr<Bank>> extraBanks;
    std::vector<std::string> extraOwners;
    bool bLoaded;

private:
    void createFonts();
    GPUTexture *findTexture( const std::string &textureName ) const;
};
