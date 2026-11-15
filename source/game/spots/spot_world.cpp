#include "shared.h"
#include "spot_world.h"

#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

namespace {
uint32_t ReadBE32( const uint8_t *p ) {
    return ( static_cast<uint32_t>( p[0] ) << 24 ) | ( static_cast<uint32_t>( p[1] ) << 16 ) |
           ( static_cast<uint32_t>( p[2] ) << 8 ) | p[3];
}

uint32_t ReadLE32( const uint8_t *p ) {
    return p[0] | ( static_cast<uint32_t>( p[1] ) << 8 ) | ( static_cast<uint32_t>( p[2] ) << 16 ) |
           ( static_cast<uint32_t>( p[3] ) << 24 );
}

float ReadLEFloat( const uint8_t *p ) {
    const uint32_t bits = ReadLE32( p );
    float value = 0.0f;
    memcpy( &value, &bits, 4 );
    return value;
}

std::string Upper( std::string text ) {
    for ( char &c : text ) {
        c = static_cast<char>( toupper( static_cast<unsigned char>( c ) ) );
    }
    return text;
}

std::vector<SpotWorldEntry> Parse( const std::vector<uint8_t> &data ) {
    std::vector<SpotWorldEntry> entries;
    if ( data.size() < 0x60 || memcmp( data.data(), "WTPS", 4 ) != 0 ) { // "SPTW" written little endian
        return entries;
    }
    // Every record's blob starts with the big endian "IGE2" and its MDSC chunk 0x1c bytes later; the record header
    // (name, position) sits 0x20 bytes before the blob.
    for ( size_t o = 0x20; o + 0x30 < data.size(); o++ ) {
        if ( memcmp( data.data() + o, "IGE2", 4 ) != 0 || memcmp( data.data() + o + 0x1c, "MDSC", 4 ) != 0 ) {
            continue;
        }
        SpotWorldEntry entry;
        char name[9] = {};
        memcpy( name, data.data() + o - 0x20, 8 );
        entry.Name = Upper( name );
        entry.Position = Vector3f( ReadLEFloat( data.data() + o - 0x18 ), ReadLEFloat( data.data() + o - 0x14 ),
                                   ReadLEFloat( data.data() + o - 0x10 ) );
        const size_t chunk = o + 0x1c;
        const uint32_t count = ReadBE32( data.data() + chunk + 8 );
        size_t p = chunk + 24;
        for ( uint32_t i = 0; i < count && p + 8 <= data.size(); i++ ) {
            const uint32_t size = ( static_cast<uint32_t>( data[p + 6] ) << 8 ) | data[p + 7];
            if ( p + 8 + size > data.size() ) {
                break;
            }
            const std::string key( reinterpret_cast<const char *>( data.data() + p ), 4 );
            if ( size == 4 ) {
                const int32_t value = static_cast<int32_t>( ReadBE32( data.data() + p + 8 ) );
                if ( key == "isla" ) {
                    entry.Island = value;
                } else if ( key == "gmth" ) {
                    entry.GameMode = value;
                } else if ( key == "nblp" ) {
                    entry.Laps = value;
                } else if ( key == "cops" ) {
                    entry.Cops = value;
                }
            }
            p += 8 + size;
        }
        entries.push_back( entry );
        o += 0x2c; // past the header of this blob
    }
    return entries;
}
} // namespace

const std::vector<SpotWorldEntry> &SpotWorld::GetAll() {
    static std::vector<SpotWorldEntry> entries;
    static bool bLoaded = false;
    if ( !bLoaded && gpFile != nullptr ) {
        bLoaded = true;
        std::vector<uint8_t> data;
        if ( gpFile->loadFile( ( gpFile->getBankFolder() + "scenarics/spotworld.bin" ).c_str(), data ) ) {
            entries = Parse( data );
            OTDU_LOG_INFO( "SpotWorld: %zu challenge records\n", entries.size() );
        } else {
            OTDU_LOG_WARN( "SpotWorld: scenarics/spotworld.bin not found\n" );
        }
    }
    return entries;
}

const SpotWorldEntry *SpotWorld::Find( const std::string &name, int32_t island ) {
    const std::string wanted = Upper( name );
    for ( const SpotWorldEntry &entry : GetAll() ) {
        if ( entry.Name == wanted && ( island < 0 || entry.Island == island ) ) {
            return &entry;
        }
    }
    return nullptr;
}

int32_t SpotWorld::IslandIndex( const std::string &islandName ) {
    if ( islandName == "ibiza" ) {
        return 0;
    }
    if ( islandName == "hawai" ) {
        return 1;
    }
    return islandName == "casino" ? 2 : -1;
}
