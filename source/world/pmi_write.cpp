#include "shared.h"
#include "pmi_write.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
// pmi.cpp's decode constants - the encode is their exact inverse.
constexpr float kPosition = 0.0156252384185791f;
constexpr float kPositionOffset = 128.0f;
constexpr float kScale = 6.103608757257462e-05f;
constexpr float kAngle = 9.587526437826455e-05f;
constexpr float kBox = 0.0625009536743164f;
constexpr float kBoxOffset = 1024.0f;
constexpr uint32_t kMobaHeaderSize = 0x1c0; // the MOBA data size (448 bytes: the cellCount[409] table inside;
                                            // the first MOB. section follows at file 0x1d0)
constexpr size_t kMobaAt = 0x10;            // the MOBA header's file offset
constexpr size_t kHeightMinAt = kMobaAt + 0x14;
constexpr size_t kHeightMaxAt = kMobaAt + 0x18;

uint32_t ReadU32( const uint8_t *p ) {
    uint32_t v;
    memcpy( &v, p, 4 );
    return v;
}

uint16_t ReadU16( const uint8_t *p ) {
    uint16_t v;
    memcpy( &v, p, 2 );
    return v;
}

float ReadF32( const uint8_t *p ) {
    float v;
    memcpy( &v, p, 4 );
    return v;
}

void PutU32( std::vector<uint8_t> &out, size_t offset, uint32_t value ) {
    memcpy( out.data() + offset, &value, 4 );
}

void PutF32( std::vector<uint8_t> &out, size_t offset, float value ) {
    memcpy( out.data() + offset, &value, 4 );
}

uint16_t EncodeU16( float decoded, float decodeScale, float decodeOffset ) {
    const float raw = ( decoded - decodeOffset ) / decodeScale;
    return static_cast<uint16_t>( Clamp( std::floor( raw + 0.5f ), 0.0f, 65535.0f ) );
}
} // namespace

PmiRecord EncodePmiRecord( const Vector3f &local, float absoluteY, const Vector3f &scale, float pitch, float yaw,
                           float roll, const Vector3f &boxRelMin, const Vector3f &boxRelMax ) {
    PmiRecord record;
    record.AbsoluteY = absoluteY;
    // The decode laws: x = w * kPosition - 128, rel = w * kBox - 1024 - the
    // offsets are SUBTRACTED on decode, so the encode adds them (the negated
    // offset below).
    record.W[ 0 ] = EncodeU16( local.x, kPosition, -kPositionOffset );
    record.W[ 2 ] = EncodeU16( local.z, kPosition, -kPositionOffset );
    record.W[ 3 ] = EncodeU16( scale.x, kScale, 0.0f );
    record.W[ 4 ] = EncodeU16( scale.y, kScale, 0.0f );
    record.W[ 5 ] = EncodeU16( scale.z, kScale, 0.0f );
    // The angles decode as w * kAngle in [0, 2pi): normalize the negatives
    // (a scene yaw of 200 deg arrives as -2.79 rad through the matrix).
    auto normalizeAngle = []( float a ) {
        a = std::fmod( a, 2.0f * 3.14159265f );
        if ( a < 0.0f ) {
            a += 2.0f * 3.14159265f;
        }
        return a;
    };
    record.W[ 6 ] = EncodeU16( normalizeAngle( pitch ), kAngle, 0.0f );
    record.W[ 7 ] = EncodeU16( normalizeAngle( yaw ), kAngle, 0.0f );
    record.W[ 8 ] = EncodeU16( normalizeAngle( roll ), kAngle, 0.0f );
    for ( int32_t i = 0; i < 3; i++ ) {
        record.W[ 10 + i ] = EncodeU16( ( &boxRelMin.x )[ i ], kBox, -kBoxOffset );
        record.W[ 13 + i ] = EncodeU16( ( &boxRelMax.x )[ i ], kBox, -kBoxOffset );
    }
    return record;
}

bool ParsePmiGroups( const uint8_t *pFile, uint32_t fileSize, std::vector<PmiGroup> &outGroups ) {
    outGroups.clear();
    if ( pFile == nullptr || fileSize < kMobaAt + kMobaHeaderSize || memcmp( pFile + 0x0c, ".PMI", 4 ) != 0 ||
         memcmp( pFile + kMobaAt, "MOBA", 4 ) != 0 ) {
        return false;
    }
    const uint32_t numMob = ReadU32( pFile + kMobaAt + 0x10 );
    uint32_t offset = kMobaAt + ReadU32( pFile + kMobaAt + 0x0c );
    for ( uint32_t mob = 0; mob < numMob; mob++ ) {
        if ( offset + 0x20 > fileSize || memcmp( pFile + offset, "MOB.", 4 ) != 0 ) {
            return false;
        }
        const uint32_t sectionSize = ReadU32( pFile + offset + 8 );
        const uint16_t count = ReadU16( pFile + offset + 0x18 );
        const uint16_t extraSize = ReadU16( pFile + offset + 0x1a );
        const uint32_t stride = 0x20 + ( ( extraSize + 15u ) & ~15u );
        if ( sectionSize < 0x20 + static_cast<uint32_t>( count ) * stride || offset + sectionSize > fileSize ) {
            return false;
        }
        PmiGroup group;
        memcpy( &group.Hashcode, pFile + offset + 0x10, 8 );
        group.Type = pFile[ offset + 0x1e ];
        memcpy( group.SectionHeader, pFile + offset, 0x20 );
        group.bHasSectionHeader = true;
        group.Records.reserve( count );
        for ( uint32_t i = 0; i < count; i++ ) {
            const uint32_t record = offset + 0x20 + i * stride;
            PmiRecord out;
            memcpy( out.W, pFile + record, sizeof( out.W ) );
            // The whole payload REGION (the padding carries data too).
            out.PayloadRegion.assign( pFile + record + 0x20, pFile + record + stride );
            group.Records.push_back( std::move( out ) );
        }
        outGroups.push_back( std::move( group ) );
        offset += sectionSize;
    }
    return true;
}

bool BuildPmi( const std::vector<PmiGroup> &groups, const uint8_t *pOriginal, uint32_t originalSize,
               std::vector<uint8_t> &out ) {
    std::vector<uint8_t> renderHeader( 0x10, 0 );
    std::vector<uint8_t> mobaHeader( kMobaHeaderSize, 0 );
    bool bHasOriginal = false;
    float originalMin = 0.0f, originalMax = 1.0f;
    if ( pOriginal != nullptr && originalSize >= kMobaAt + kMobaHeaderSize &&
         memcmp( pOriginal + 0x0c, ".PMI", 4 ) == 0 && memcmp( pOriginal + kMobaAt, "MOBA", 4 ) == 0 ) {
        renderHeader.assign( pOriginal, pOriginal + 0x10 );
        mobaHeader.assign( pOriginal + kMobaAt, pOriginal + kMobaAt + kMobaHeaderSize );
        originalMin = ReadF32( mobaHeader.data() + 0x14 );
        originalMax = ReadF32( mobaHeader.data() + 0x18 );
        bHasOriginal = true;
    }
    if ( originalMax <= originalMin ) {
        originalMax = originalMin + 1.0f;
    }

    // The y range: parsed records decode against the ORIGINAL range (and
    // re-encode byte-exact against it); new records' AbsoluteY widens it.
    float newMin = bHasOriginal ? originalMin : FLT_MAX;
    float newMax = bHasOriginal ? originalMax : -FLT_MAX;
    bool bAnyAbsolute = false;
    for ( const PmiGroup &group : groups ) {
        for ( const PmiRecord &record : group.Records ) {
            if ( std::isnan( record.AbsoluteY ) ) {
                continue;
            }
            bAnyAbsolute = true;
            newMin = Min( newMin, record.AbsoluteY );
            newMax = Max( newMax, record.AbsoluteY );
        }
    }
    if ( !bAnyAbsolute ) {
        newMin = originalMin;
        newMax = originalMax;
    } else {
        // A metre of headroom both ways (the retail ranges are generous).
        newMin -= 1.0f;
        newMax += 1.0f;
        if ( bHasOriginal ) {
            newMin = Min( newMin, originalMin );
            newMax = Max( newMax, originalMax );
        }
        if ( newMax <= newMin ) {
            newMax = newMin + 1.0f;
        }
    }
    const float heightScale = ( newMax - newMin ) / 65535.0f;

    // The MOB. sections. Each section's own header bytes ([4, 8) and the
    // reserved fields at [0x0c, 0x20) vary per section in the retail files)
    // are re-emitted from the captured ones; a NEW group falls back to the
    // first captured group's bytes.
    uint8_t fallbackTemplate[ 0x20 ] = {};
    bool bHasFallback = false;
    for ( const PmiGroup &group : groups ) {
        if ( group.bHasSectionHeader ) {
            memcpy( fallbackTemplate, group.SectionHeader, 0x20 );
            bHasFallback = true;
            break;
        }
    }
    if ( !bHasFallback && bHasOriginal ) {
        const uint32_t firstMob = kMobaAt + ReadU32( pOriginal + kMobaAt + 0x0c );
        if ( firstMob + 0x20 <= originalSize && memcmp( pOriginal + firstMob, "MOB.", 4 ) == 0 ) {
            memcpy( fallbackTemplate, pOriginal + firstMob, 0x20 );
            bHasFallback = true;
        }
    }

    size_t bodySize = 0;
    for ( const PmiGroup &group : groups ) {
        for ( const PmiRecord &record : group.Records ) {
            bodySize += 0x20 + record.PayloadRegion.size();
        }
        bodySize += 0x20; // the section header
    }

    out.clear();
    out.reserve( 0x10 + kMobaHeaderSize + bodySize );
    out.insert( out.end(), renderHeader.begin(), renderHeader.end() );
    out.insert( out.end(), mobaHeader.begin(), mobaHeader.end() );

    PutU32( out, kMobaAt + 0x10, static_cast<uint32_t>( groups.size() ) ); // numMOB (file 0x20)
    PutF32( out, kHeightMinAt, newMin );
    PutF32( out, kHeightMaxAt, newMax );
    PutU32( out, kMobaAt + 0x0c, kMobaHeaderSize ); // the MOBA data size (file 0x1c)

    for ( const PmiGroup &group : groups ) {
        const size_t sectionAt = out.size();
        const uint8_t *pTemplate = group.bHasSectionHeader ? group.SectionHeader : fallbackTemplate;
        out.insert( out.end(), pTemplate, pTemplate + 0x20 );
        memcpy( out.data() + sectionAt, "MOB.", 4 );
        memcpy( out.data() + sectionAt + 0x10, &group.Hashcode, 8 );
        const uint16_t count = static_cast<uint16_t>( Min<size_t>( group.Records.size(), 65535u ) );
        memcpy( out.data() + sectionAt + 0x18, &count, 2 );
        out[ sectionAt + 0x1e ] = group.Type;
        // The payload stride: the captured groups keep their own (their
        // records carry whole regions); a NEW group has no payload.
        uint16_t extraSize = 0;
        if ( group.bHasSectionHeader ) {
            memcpy( &extraSize, group.SectionHeader + 0x1a, 2 );
        } else {
            for ( const PmiRecord &record : group.Records ) {
                extraSize = static_cast<uint16_t>( Max<size_t>( extraSize, record.PayloadRegion.size() ) );
            }
        }
        memcpy( out.data() + sectionAt + 0x1a, &extraSize, 2 );
        for ( const PmiRecord &record : group.Records ) {
            uint16_t w[ 16 ];
            memcpy( w, record.W, sizeof( w ) );
            // The y against the (possibly widened) range: parsed records
            // decode through the original range first, then re-encode -
            // byte-exact when the range did not move.
            const float y = std::isnan( record.AbsoluteY )
                                ? ( bHasOriginal ? originalMin + ReadU16( reinterpret_cast<const uint8_t *>( record.W + 1 ) ) *
                                                          ( originalMax - originalMin ) / 65535.0f
                                                 : 0.0f )
                                : record.AbsoluteY;
            w[ 1 ] = static_cast<uint16_t>( Clamp( std::floor( ( y - newMin ) / heightScale + 0.5f ), 0.0f, 65535.0f ) );
            out.insert( out.end(), reinterpret_cast<const uint8_t *>( w ), reinterpret_cast<const uint8_t *>( w ) + 0x20 );
            // The payload region verbatim, sized to the section's stride.
            const size_t regionSize = ( ( extraSize + 15u ) & ~15u );
            if ( regionSize > 0 ) {
                std::vector<uint8_t> region = record.PayloadRegion;
                region.resize( regionSize, 0 );
                out.insert( out.end(), region.begin(), region.end() );
            }
        }
        const uint32_t sectionSize = static_cast<uint32_t>( out.size() - sectionAt );
        PutU32( out, sectionAt + 8, sectionSize );
    }

    // The RenderFile header's size field.
    PutU32( out, 8, static_cast<uint32_t>( out.size() ) );
    return true;
}
