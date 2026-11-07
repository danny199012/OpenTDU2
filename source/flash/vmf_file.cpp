#include "shared.h"
#include "system/cmd_line.h"
#include "vmf_file.h"

namespace {
template <typename T> T Read( const std::vector<uint8_t> &data, uint32_t offset ) {
    T value{};
    if ( offset + sizeof( T ) <= data.size() ) {
        memcpy( &value, data.data() + offset, sizeof( T ) );
    }
    return value;
}
} // namespace

bool VMFFile::load( const uint8_t *pBuffer, uint32_t size ) {
    data.assign( pBuffer, pBuffer + size );
    tags.clear();
    fonts.clear();
    bitmaps.clear();
    exports.clear();
    frameLabels.clear();

    if ( size < 0x13 || memcmp( pBuffer, "FMV", 3 ) != 0 ) {
        OTDU_LOG_WARN( "Invalid VMF header\n" );
        return false;
    }

    version = data[3];
    stageWidth = Read<uint16_t>( data, 0x0a ) / 20.0f;
    stageHeight = Read<uint16_t>( data, 0x0c ) / 20.0f;
    frameRate = Read<uint16_t>( data, 0x0e ) / 256.0f;
    frameCount = Read<uint16_t>( data, 0x10 );

    uint32_t offset = 0x13;
    if ( data[0x12] != 0 ) {
        uint32_t numPreload = Read<uint32_t>( data, offset );
        offset += 4 + numPreload * 4;
    }

    tagsStart = offset;
    if ( !parseTags( offset, size, tags ) ) {
        return false;
    }

    // Definition tags may live in Eden sprites too; fonts and bitmaps are only
    // at the root level in every file checked so far.
    for ( const FlashTag &tag : tags ) {
        switch ( tag.Code ) {
        case FT_DefineFontEden:
            parseFont( tag );
            break;
        case FT_DefineBitmapEden:
            parseBitmap( tag );
            break;
        case FT_ExportAssets:
            parseExports( tag );
            break;
        case FT_FrameLabel: {
            const char *pLabel = reinterpret_cast<const char *>( data.data() + tag.Offset );
            frameLabels.emplace_back( pLabel, strnlen( pLabel, tag.Length ) );
            break;
        }
        default:
            break;
        }
    }
    return true;
}

bool VMFFile::parseTags( uint32_t start, uint32_t end, std::vector<FlashTag> &outTags ) const {
    uint32_t offset = start;
    while ( offset + 2 <= end ) {
        uint16_t header = Read<uint16_t>( data, offset );
        offset += 2;
        FlashTag tag;
        tag.Code = header >> 6;
        tag.Length = header & 0x3f;
        if ( tag.Length == 0x3f ) {
            tag.Length = Read<uint32_t>( data, offset );
            offset += 4;
        }
        tag.Offset = offset;
        if ( offset + tag.Length > end ) {
            OTDU_LOG_WARN( "Truncated VMF tag %u\n", tag.Code );
            return false;
        }
        outTags.push_back( tag );
        if ( tag.Code == FT_End ) {
            break;
        }
        offset += tag.Length;
    }
    return true;
}

void VMFFile::parseFont( const FlashTag &tag ) {
    FlashFontDesc font;
    uint32_t offset = tag.Offset;
    const uint32_t end = tag.Offset + tag.Length;
    font.CharacterId = Read<uint16_t>( data, offset );
    font.TextureId = Read<uint16_t>( data, offset + 2 );
    font.Flags = data[offset + 4];
    offset += 5;

    const char *pName = reinterpret_cast<const char *>( data.data() + offset );
    font.Name.assign( pName, strnlen( pName, end - offset ) );
    offset += static_cast<uint32_t>( font.Name.size() ) + 1;

    const uint16_t numGlyphs = Read<uint16_t>( data, offset );
    const uint16_t numTexturedGlyphs = Read<uint16_t>( data, offset + 2 );
    offset += 4;

    auto readGlyphs = [&]( uint32_t count, std::vector<FlashGlyph> &outGlyphs ) {
        for ( uint32_t i = 0; i < count && offset + 26 <= end; i++ ) {
            FlashGlyph glyph;
            glyph.Code = Read<uint16_t>( data, offset );
            glyph.U0 = Read<float>( data, offset + 2 );
            glyph.U1 = Read<float>( data, offset + 6 );
            glyph.V0 = Read<float>( data, offset + 10 );
            glyph.V1 = Read<float>( data, offset + 14 );
            glyph.Width = Read<int16_t>( data, offset + 18 );
            glyph.Height = Read<int16_t>( data, offset + 20 );
            glyph.OffsetX = Read<int16_t>( data, offset + 22 );
            glyph.OffsetY = Read<int16_t>( data, offset + 24 );
            outGlyphs.push_back( glyph );
            offset += 26;
        }
    };
    readGlyphs( numTexturedGlyphs, font.Glyphs );
    if ( font.TextureId != 0 ) {
        readGlyphs( numGlyphs, font.SecondaryGlyphs );
    }
    // Layout table: shape offsets, codes, metrics and advances.
    offset += numGlyphs * 2u;
    uint32_t codesOffset = offset;
    offset += numGlyphs * 2u;
    // The fonts that carry vector outlines (no atlas: texture 0 and a tag of
    // tens of KB, e.g. Cas_SlotScreen's 'Digital', Cas_SlotMachine's 'Berlin
    // Sans FB' id 4) store each glyph's shape (int32 point records) between
    // the glyph records and the code table, so the fixed position above lands
    // inside the shapes and reads nonsense metrics (ascent -2 / 6656). The
    // shape records are not decoded; the code table is found instead: the
    // first run of numGlyphs strictly ascending character codes followed by
    // plausible ascent / descent / leading [DATA: the byte layout of fonts
    // 116 / 301, whose codes sit at the fixed position; the search is INF].
    const auto plausible = [&]( uint32_t at ) {
        const int16_t ascent = Read<int16_t>( data, at );
        const int16_t descent = Read<int16_t>( data, at + 2 );
        const int16_t leading = Read<int16_t>( data, at + 4 );
        return ascent > 0 && ascent <= 4096 && descent >= 0 && descent <= 4096 && leading >= -1024 && leading <= 4096;
    };
    if ( numGlyphs > 1 && ( offset + 6u > end || !plausible( offset ) ) ) {
        const uint32_t tableBytes = numGlyphs * 4u + 6u;
        for ( uint32_t at = codesOffset; at + tableBytes <= end; at++ ) {
            uint16_t previous = Read<uint16_t>( data, at );
            if ( previous < 0x20 || previous > 0x7f ) {
                continue;
            }
            // The ASCII head is ascending; the codes past it follow the
            // Windows-1252 order (0x20ac, 0x2122, 0x2c6, 0x2013...), so only
            // "no control code" is asked of them [DATA: font 4's table].
            const uint32_t head = Min<uint32_t>( numGlyphs, 32u );
            uint32_t i = 1;
            for ( ; i < numGlyphs; i++ ) {
                const uint16_t code = Read<uint16_t>( data, at + i * 2 );
                if ( code < 0x20 || ( i < head && code <= previous ) ) {
                    break;
                }
                previous = code;
            }
            if ( i == numGlyphs && plausible( at + numGlyphs * 2u ) ) {
                codesOffset = at;
                offset = at + numGlyphs * 2u;
                break;
            }
        }
    }
    if ( offset + 6u + numGlyphs * 2u <= end ) {
        font.Ascent = Read<int16_t>( data, offset );
        font.Descent = Read<int16_t>( data, offset + 2 );
        font.Leading = Read<int16_t>( data, offset + 4 );
        const uint32_t advancesOffset = offset + 6;
        std::unordered_map<uint16_t, int16_t> advances;
        for ( uint32_t i = 0; i < numGlyphs; i++ ) {
            advances[Read<uint16_t>( data, codesOffset + i * 2 )] = Read<int16_t>( data, advancesOffset + i * 2 );
        }
        for ( FlashGlyph &glyph : font.Glyphs ) {
            auto it = advances.find( glyph.Code );
            if ( it != advances.end() ) {
                glyph.Advance = it->second;
            }
        }
    }
    // -logfonts: the font records (name, glyph tables, metrics, the bytes left).
    if ( CmdLine::HasArg( "-logfonts" ) ) {
        std::string codes;
        for ( size_t i = 0; i < font.Glyphs.size() && i < 24; i++ ) {
            codes += static_cast<char>( font.Glyphs[i].Code < 128 && font.Glyphs[i].Code >= 32 ? font.Glyphs[i].Code : '?' );
        }
        OTDU_LOG_INFO( "VMF font %u '%s': texture %u flags %02x, %u glyphs (%u textured) '%s', ascent %d descent %d "
                       "leading %d, tag %u bytes, layout ends at %d of %u\n",
                       font.CharacterId, font.Name.c_str(), font.TextureId, font.Flags, numGlyphs, numTexturedGlyphs,
                       codes.c_str(), font.Ascent, font.Descent, font.Leading, tag.Length,
                       static_cast<int32_t>( offset + 6u + numGlyphs * 2u - tag.Offset ), tag.Length );
    }
    fonts.push_back( std::move( font ) );
}

void VMFFile::parseBitmap( const FlashTag &tag ) {
    if ( tag.Length < 22 ) {
        return;
    }
    FlashBitmapDesc bitmap;
    bitmap.CharacterId = Read<uint16_t>( data, tag.Offset );
    bitmap.Width = Read<uint16_t>( data, tag.Offset + 2 );
    bitmap.Height = Read<uint16_t>( data, tag.Offset + 4 );
    bitmap.U0 = Read<float>( data, tag.Offset + 6 );
    bitmap.V0 = Read<float>( data, tag.Offset + 10 );
    bitmap.U1 = Read<float>( data, tag.Offset + 14 );
    bitmap.V1 = Read<float>( data, tag.Offset + 18 );
    bitmaps.push_back( bitmap );
}

void VMFFile::parseExports( const FlashTag &tag ) {
    uint32_t offset = tag.Offset;
    const uint32_t end = tag.Offset + tag.Length;
    const uint16_t count = Read<uint16_t>( data, offset );
    offset += 2;
    for ( uint32_t i = 0; i < count && offset + 2 < end; i++ ) {
        const uint16_t characterId = Read<uint16_t>( data, offset );
        offset += 2;
        const char *pName = reinterpret_cast<const char *>( data.data() + offset );
        std::string name( pName, strnlen( pName, end - offset ) );
        offset += static_cast<uint32_t>( name.size() ) + 1;
        exports[name] = characterId;
    }
}

const FlashFontDesc *VMFFile::findFont( const char *pName ) const {
    for ( const FlashFontDesc &font : fonts ) {
        if ( StringCmpI( font.Name.c_str(), pName ) == 0 ) {
            return &font;
        }
    }
    return nullptr;
}

const FlashBitmapDesc *VMFFile::findBitmap( uint16_t characterId ) const {
    for ( const FlashBitmapDesc &bitmap : bitmaps ) {
        if ( bitmap.CharacterId == characterId ) {
            return &bitmap;
        }
    }
    return nullptr;
}

const FlashFontDesc *VMFFile::findFont( uint16_t characterId ) const {
    for ( const FlashFontDesc &font : fonts ) {
        if ( font.CharacterId == characterId ) {
            return &font;
        }
    }
    return nullptr;
}
