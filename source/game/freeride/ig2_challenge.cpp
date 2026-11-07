#include "shared.h"
#include "ig2_challenge.h"

#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

namespace {
uint32_t ReadU32( const uint8_t *p ) {
    return ( static_cast<uint32_t>( p[0] ) << 24 ) | ( static_cast<uint32_t>( p[1] ) << 16 ) |
           ( static_cast<uint32_t>( p[2] ) << 8 ) | p[3];
}

float ReadF32( const uint8_t *p ) {
    const uint32_t bits = ReadU32( p );
    float value = 0.0f;
    memcpy( &value, &bits, 4 );
    return value;
}

std::string LocText( int64_t id ) {
    const char *pText = id > 0 && gpDatabase != nullptr ? gpDatabase->getString( static_cast<int32_t>( id ) ) : nullptr;
    return pText != nullptr ? pText : "";
}
} // namespace

bool Ig2Challenge::load( const std::string &challengeName ) {
    name = challengeName;
    values.clear();
    gates.clear();
    decors.clear();
    numVehicles = 0;
    std::string file = challengeName;
    for ( char &c : file ) {
        c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
    }
    std::vector<uint8_t> data;
    if ( gpFile == nullptr || !gpFile->loadFile( ( gpFile->getBankFolder() + "ig2/" + file + ".ig2" ).c_str(), data ) ||
         data.size() < 0x1c || memcmp( data.data(), "IGE2", 4 ) != 0 ) {
        OTDU_LOG_WARN( "Ig2Challenge: '%s' not found or not an IGE2 file\n", challengeName.c_str() );
        return false;
    }
    size_t offset = ReadU32( data.data() + 4 );
    while ( offset + 8 <= data.size() ) {
        const uint8_t *pChunk = data.data() + offset;
        const uint32_t size = ReadU32( pChunk + 4 );
        if ( size < 8 || offset + size > data.size() ) {
            break;
        }
        const uint8_t *pBody = pChunk + 8;
        const size_t bodySize = size - 8;
        if ( memcmp( pChunk, "MDSC", 4 ) == 0 && bodySize >= 16 ) {
            // Keys; a few unnamed u32 words sit between the named ones (skipped).
            size_t p = 16;
            while ( p + 8 <= bodySize ) {
                const uint8_t *pKey = pBody + p;
                const bool bNamed = isprint( pKey[0] ) && isprint( pKey[1] ) && isprint( pKey[2] ) && isprint( pKey[3] );
                if ( !bNamed ) {
                    p += 4;
                    continue;
                }
                const uint16_t valueSize = static_cast<uint16_t>( ( pKey[6] << 8 ) | pKey[7] );
                if ( p + 8 + valueSize > bodySize ) {
                    break;
                }
                const std::string key( reinterpret_cast<const char *>( pKey ), 4 );
                if ( valueSize == 4 ) {
                    values[key] = static_cast<int32_t>( ReadU32( pKey + 8 ) );
                } else if ( valueSize == 8 ) {
                    values[key] = ( static_cast<int64_t>( ReadU32( pKey + 8 ) ) << 32 ) | ReadU32( pKey + 12 );
                }
                p += 8 + valueSize;
            }
        } else if ( memcmp( pChunk, "ENTI", 4 ) == 0 && bodySize >= 4 ) {
            const uint32_t count = ReadU32( pBody );
            size_t p = 4;
            for ( uint32_t i = 0; i < count && p + 8 <= bodySize; i++ ) {
                const uint8_t *pEntity = pBody + p;
                const uint32_t entitySize = ReadU32( pEntity + 4 );
                if ( entitySize < 8 || p + entitySize > bodySize ) {
                    break;
                }
                if ( memcmp( pEntity, "CHK2", 4 ) == 0 && ( entitySize == 0x68 || entitySize == 0x7c ) ) {
                    // The matrix follows the version word (0x68) or the road
                    // link prefix (0x7c: 6 words) [DATA].
                    const uint8_t *pMatrix = pEntity + 8 + ( entitySize == 0x68 ? 4 : 24 );
                    float m[12];
                    for ( int32_t k = 0; k < 12; k++ ) {
                        m[k] = ReadF32( pMatrix + k * 4 );
                    }
                    const uint8_t *pTail = pMatrix + 48 + 16; // vec3 + scale
                    Ig2Gate gate;
                    gate.Position = Vector3f( m[3], m[7], m[11] );
                    gate.Forward = Vector3f( m[2], 0.0f, m[10] );
                    const float length = gate.Forward.length();
                    gate.Forward = length > 1e-4f ? gate.Forward * ( 1.0f / length ) : Vector3f( 0.0f, 0.0f, 1.0f );
                    gate.Kind = static_cast<int32_t>( ReadU32( pTail ) );
                    gate.Side = static_cast<int32_t>( ReadU32( pTail + 4 ) );
                    gate.Width = ReadF32( pTail + 8 );
                    gate.bRoad = entitySize == 0x7c;
                    gates.push_back( gate );
                } else if ( memcmp( pEntity, "VEHI", 4 ) == 0 ) {
                    numVehicles++;
                } else if ( memcmp( pEntity, "DEC2", 4 ) == 0 && ( entitySize == 0x54 || entitySize == 0x68 ) ) {
                    // Same matrix placement as CHK2: after the version word or
                    // the road link prefix; the type is the second-last word.
                    const uint8_t *pMatrix = pEntity + 8 + ( entitySize == 0x54 ? 4 : 24 );
                    Ig2Decor decor;
                    decor.Position = Vector3f( ReadF32( pMatrix + 12 ), ReadF32( pMatrix + 28 ), ReadF32( pMatrix + 44 ) );
                    decor.Right = Vector3f( ReadF32( pMatrix ), 0.0f, ReadF32( pMatrix + 32 ) );
                    const float length = decor.Right.length();
                    decor.Right = length > 1e-4f ? decor.Right * ( 1.0f / length ) : Vector3f( 1.0f, 0.0f, 0.0f );
                    decor.Type = static_cast<int32_t>( ReadU32( pEntity + entitySize - 8 ) );
                    decors.push_back( decor );
                }
                p += entitySize;
            }
        }
        offset += size;
    }
    if ( CmdLine::HasArg( "-logig2" ) ) {
        OTDU_LOG_INFO( "Ig2Challenge: %s '%s': mode %d, target %d ms, limit %d ms, car %d, %zu gate(s), %s, %d lap(s)\n",
                       name.c_str(), getTitle().c_str(), getMode(), getTargetMs(), getLimitMs(), getCarId(), gates.size(),
                       isCircuit() ? "circuit" : "point to point", getLaps() );
        for ( const Ig2Gate &gate : gates ) {
            OTDU_LOG_INFO( "Ig2Challenge:   gate (%.1f, %.1f, %.1f) fwd (%.2f, %.2f) kind %d side %d width %.1f\n",
                           gate.Position.x, gate.Position.y, gate.Position.z, gate.Forward.x, gate.Forward.z, gate.Kind,
                           gate.Side, gate.Width );
        }
    }
    return !gates.empty(); // a speed challenge has its start only
}

int64_t Ig2Challenge::getValue( const char *pKey, int64_t fallback ) const {
    auto it = values.find( pKey );
    return it != values.end() ? it->second : fallback;
}

std::string Ig2Challenge::getTitle() const { return LocText( getValue( "dbna" ) ); }

std::string Ig2Challenge::getDescription() const { return LocText( getValue( "dbd1" ) ); }
