#pragma once

#include "core/math.h"
#include <cstdint>
#include <vector>

// The .pmi WRITER (M5, docs/re/scene_editor.md 6): the write side of
// pmi.cpp. The scene editor's export_pmi re-encodes a sector's instance
// lists with the scene's library / actor placements appended, and the
// `roundtrip-pmi` test of otdu2_bnk_tool re-encodes a retail list unchanged
// (byte-compare).
//
// The container is FLAT (not the RenderFile section tree):
//   [0x00 RenderFile header 0x10: ".PMI" magic at 0x0c]
//   [0x10 MOBA header 0x1d0: dataSize u32 at file 0x1c (= 0x1d0),
//        numMOB u32 at 0x20, heightMin/Max f32 at 0x24/0x28,
//        the quadtree cell table at 0x30..]
//   [numMOB x MOB. sections: magic "MOB." + 4 reserved bytes + u32 size,
//        u64 model hashcode at +0x10, u16 count at +0x18, u16 extraSize
//        at +0x1a, u16 ? at +0x1c, u8 type at +0x1e (1 static, 2 actor,
//        3 skinned static, 4 actor + payload),
//        count x { u16[16] record + payload (extraSize, stride padded to 16) }]
//
// The records keep the RAW u16[16] form: the rebuild of a parsed list is
// then byte-exact, which is what the round-trip test proves. New records
// carry their absolute y in AbsoluteY (the list's height range widens to
// cover them; parsed records re-encode against the original range and stay
// byte-exact).
struct PmiRecord {
    uint16_t W[ 16 ] = {};
    float AbsoluteY = NAN;        // NaN: W[1] as-is (a parsed record)
    // The record's whole payload REGION (the extraSize bytes AND the padding
    // to the 16-byte stride - the retail padding carries data). A parsed
    // record keeps it verbatim; a new record's region is zero padded (or a
    // clone of its group's, for the skinned statics' bone palettes).
    std::vector<uint8_t> PayloadRegion;
};

struct PmiGroup {
    uint64_t Hashcode = 0;
    uint8_t Type = 1;
    // The section's own 0x20 header bytes (the reserved fields at [4, 8) and
    // [0x0c, 0x20) vary per section in the retail files) - captured by
    // ParsePmiGroups; BuildPmi re-emits them verbatim. Empty: BuildPmi uses
    // the first captured group's bytes (new groups).
    uint8_t SectionHeader[ 0x20 ] = {};
    bool bHasSectionHeader = false;
    std::vector<PmiRecord> Records;
};

// One semantic instance (the scene placements): sector-LOCAL position, the
// rotation angles in radians, per-axis scale, the AABB RELATIVE to the
// position (decode: rel = w * 0.0625009537 - 1024, so +-64 m around it).
// AbsoluteY = the world y (the list's height range widens to cover it).
PmiRecord EncodePmiRecord( const Vector3f &local, float absoluteY, const Vector3f &scale, float pitch, float yaw,
                           float roll, const Vector3f &boxRelMin, const Vector3f &boxRelMax );

// Rebuilds a .pmi from `groups`. pOriginal (may be null for a fresh list)
// supplies the header bytes: the RenderFile header [0, 0x10) and the MOBA
// header [0x10, 0x1e0) with its quadtree cell table; numMOB / heightMin /
// heightMax are recomputed (the range widens to cover every AbsoluteY).
// Returns false when the original is not a .pmi of the expected law.
bool BuildPmi( const std::vector<PmiGroup> &groups, const uint8_t *pOriginal, uint32_t originalSize,
               std::vector<uint8_t> &out );

// Walks a .pmi into raw groups (the BuildPmi input; the mirror of
// ParsePmi's walk without the semantic decode). The payload bytes are
// copied. Returns false on any structural surprise.
bool ParsePmiGroups( const uint8_t *pFile, uint32_t fileSize, std::vector<PmiGroup> &outGroups );
