#include "shared.h"
#include "build_stamp.h"

#include "build_info.h"
#include "system/cmd_line.h"

#include <cctype>

namespace {

// A 5x7 bitmap font, one byte per row, bit 4 = leftmost pixel. Only the glyphs
// the label needs (A-Z, 0-9, space, '+', '.', '(', ')', '-'); anything else is
// drawn as a space.
struct Glyph {
    char Character;
    uint8_t Rows[7];
};

constexpr Glyph kGlyphs[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E } },
    { '6', { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
    { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
};

const Glyph *FindGlyph( char c ) {
    for ( const Glyph &glyph : kGlyphs ) {
        if ( glyph.Character == c ) {
            return &glyph;
        }
    }
    return &kGlyphs[0];
}

void BlendPixel( uint8_t *pPixel, bool bBGR, const float color[3], float alpha ) {
    const int32_t r = bBGR ? 2 : 0;
    const int32_t b = bBGR ? 0 : 2;
    pPixel[r] = static_cast<uint8_t>( pPixel[r] * ( 1.0f - alpha ) + color[0] * alpha );
    pPixel[1] = static_cast<uint8_t>( pPixel[1] * ( 1.0f - alpha ) + color[1] * alpha );
    pPixel[b] = static_cast<uint8_t>( pPixel[b] * ( 1.0f - alpha ) + color[2] * alpha );
}

} // namespace

namespace BuildStamp {

std::string Label() {
    std::string label = "BUILD " OTDU_BUILD_NUMBER " " OTDU_BUILD_HASH OTDU_BUILD_DIRTY;
    if ( OTDU_BUILD_BRANCH[0] != '\0' && std::string( OTDU_BUILD_BRANCH ) != "unknown" ) {
        label += " ";
        label += OTDU_BUILD_BRANCH;
    }
    for ( char &c : label ) {
        c = static_cast<char>( std::toupper( static_cast<unsigned char>( c ) ) );
    }
    return label;
}

void Burn( uint8_t *pRGB, int32_t width, int32_t height, bool bBGR ) {
    if ( pRGB == nullptr || width <= 0 || height <= 0 || CmdLine::HasArg( "-nowatermark" ) ) {
        return;
    }
    const std::string label = Label();
    // One screen pixel per font pixel up to 720p, then 2x / 3x; capped so the
    // stamp never eats the frame.
    const int32_t scale = Clamp( height / 720, 1, 3 );
    const int32_t glyphW = 5 * scale, gap = scale;
    const int32_t pad = 2 * scale;
    const int32_t textW = static_cast<int32_t>( label.size() ) * ( glyphW + gap ) - gap;
    const int32_t boxW = textW + pad * 2, boxH = 7 * scale + pad * 2;
    const int32_t x0 = width - boxW - 4 * scale;
    const int32_t y0 = height - boxH - 4 * scale;
    if ( x0 < 0 || y0 < 0 ) {
        return;
    }

    const float black[3] = { 0.0f, 0.0f, 0.0f };
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    for ( int32_t y = y0; y < y0 + boxH; y++ ) {
        uint8_t *pRow = pRGB + static_cast<size_t>( y ) * width * 3;
        for ( int32_t x = x0; x < x0 + boxW; x++ ) {
            BlendPixel( pRow + x * 3, bBGR, black, 0.45f );
        }
    }
    int32_t penX = x0 + pad;
    for ( const char c : label ) {
        const Glyph *pGlyph = FindGlyph( c );
        for ( int32_t row = 0; row < 7; row++ ) {
            const uint8_t bits = pGlyph->Rows[row];
            for ( int32_t column = 0; column < 5; column++ ) {
                if ( ( bits & ( 1u << ( 4 - column ) ) ) == 0 ) {
                    continue;
                }
                for ( int32_t sy = 0; sy < scale; sy++ ) {
                    const int32_t y = y0 + pad + row * scale + sy;
                    uint8_t *pRow = pRGB + static_cast<size_t>( y ) * width * 3;
                    for ( int32_t sx = 0; sx < scale; sx++ ) {
                        BlendPixel( pRow + ( penX + column * scale + sx ) * 3, bBGR, white, 0.9f );
                    }
                }
            }
        }
        penX += glyphW + gap;
    }
}

} // namespace BuildStamp
