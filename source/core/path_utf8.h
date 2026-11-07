#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// File names that do not fit the ANSI code page.
//
// std::filesystem::path::string() converts the native (UTF-16) name to the
// active ANSI code page on Windows and THROWS std::system_error ("No mapping
// for the Unicode character exists in the target multi-byte code page") when
// one character has no mapping - a song named with U+FF02 fullwidth quotes
// ("Moana ＂How Far I'll Go＂") stopped the game in the radio's station scan
// (2026-11-05). These helpers never throw:
//
//   PathToUtf8 / PathFromUtf8  - a lossless round trip, for paths kept in a
//                                std::string and for the logs
//   PathToDisplay              - Windows-1252 bytes for the UI (the Flash text
//                                and the string tables are Latin-1), with the
//                                common look-alikes folded to ASCII
//
// C++17: path::u8string() is a std::string of UTF-8 bytes and u8path() reads one.

inline std::string PathToUtf8( const std::filesystem::path &path ) {
    try {
        return path.u8string();
    } catch ( ... ) {
        return std::string();
    }
}

inline std::filesystem::path PathFromUtf8( const std::string &utf8 ) {
    try {
        return std::filesystem::u8path( utf8 );
    } catch ( ... ) {
        return std::filesystem::path();
    }
}

// One Unicode code point to a Windows-1252 byte (0 = no equivalent).
inline unsigned char CodePointTo1252( uint32_t c ) {
    if ( c < 0x80 || ( c >= 0xA0 && c <= 0xFF ) ) {
        return static_cast<unsigned char>( c );
    }
    // The fullwidth ASCII block (U+FF01..U+FF5E) is ASCII shifted by 0xFEE0.
    if ( c >= 0xFF01 && c <= 0xFF5E ) {
        return static_cast<unsigned char>( c - 0xFEE0 );
    }
    switch ( c ) {
    case 0x3000: return ' ';                  // ideographic space
    case 0x2018: case 0x2019: case 0x201B: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201F: case 0x2033: return '"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return '-';
    case 0x20AC: return 0x80;                 // euro sign (cp1252)
    case 0x2026: return 0x85;                 // ellipsis (cp1252)
    case 0x2122: return 0x99;                 // trade mark (cp1252)
    default: return 0;
    }
}

inline std::string PathToDisplay( const std::filesystem::path &path ) {
    const std::string utf8 = PathToUtf8( path );
    std::string out;
    out.reserve( utf8.size() );
    for ( size_t i = 0; i < utf8.size(); ) {
        const unsigned char lead = static_cast<unsigned char>( utf8[i] );
        uint32_t c = lead;
        size_t length = 1;
        if ( lead >= 0xF0 ) {
            c = lead & 0x07u;
            length = 4;
        } else if ( lead >= 0xE0 ) {
            c = lead & 0x0Fu;
            length = 3;
        } else if ( lead >= 0xC0 ) {
            c = lead & 0x1Fu;
            length = 2;
        }
        if ( i + length > utf8.size() ) {
            break;
        }
        for ( size_t k = 1; k < length; k++ ) {
            c = ( c << 6 ) | ( static_cast<unsigned char>( utf8[i + k] ) & 0x3Fu );
        }
        i += length;
        const unsigned char mapped = CodePointTo1252( c );
        if ( mapped != 0 ) {
            out.push_back( static_cast<char>( mapped ) );
        } else if ( c >= 0x1F000 ) {
            // emoji and pictographs: dropped
        } else {
            out.push_back( '?' );
        }
    }
    return out;
}
