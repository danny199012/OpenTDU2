#pragma once

// The RenderFile container WRITER (M5, docs/re/scene_editor.md 6): the write
// side of source/render/render_file.cpp - the common container of every Eden
// render resource (.2DB, .2DM, .3DD, .3DG). The scene export builds new
// payloads with it (a composed .3dd, patched .pmi); the roundtrip mode of
// otdu2_bnk_tool (`roundtripfile`) validates the layout law by rebuilding
// retail files from the parsed tree byte for byte.
//
// Container law (RenderFile::VisitSections is the read side):
//   Header (0x10): { u16 VersionMajor; u16 VersionMinor; u32 Flags;
//                    u32 Size; u32 Magic }
//   Sections, packed (no alignment): { u32 Type; u16 VersionMajor;
//       u16 VersionMinor; u32 Size; u32 DataSize } (0x10), then Size-0x10
//       payload bytes. When Size > max(DataSize, 0x10), the payload's tail
//       holds CHILD sections, parsed from DataSize to Size.
#include <cstdint>
#include <string>
#include <vector>

struct RenderSectionWrite {
    uint32_t Type = 0;
    uint16_t VersionMajor = 2;
    uint16_t VersionMinor = 0;
    // This section's own bytes (the payload BEFORE the child region - the
    // DataSize part of the layout).
    std::vector<uint8_t> Payload;
    // Child sections, appended after the payload inside the same section's
    // Size (the container's nesting).
    std::vector<RenderSectionWrite> Children;
    // Bytes between the last child and the section end (retail files have
    // none in practice; parse captures whatever is there so a roundtrip is
    // byte-exact).
    std::vector<uint8_t> Trailing;

    uint32_t getDataSize() const {
        return static_cast<uint32_t>( Payload.size() + Trailing.size() );
    }
};

// Builds the file bytes (header + the section tree) from `sections`.
std::vector<uint8_t> BuildRenderFile( uint32_t magic, uint16_t versionMajor, uint16_t versionMinor,
                                      uint32_t flags, const std::vector<RenderSectionWrite> &sections );

// Builds from one root node's Children (the shape the parse returns).
std::vector<uint8_t> BuildRenderFile( const RenderSectionWrite &root );

// Parses a file into the tree the builder consumes (payload / children /
// trailing split per section; `root.Children` = the top-level sections).
// False with `error` set when the bytes are not a RenderFile container of
// the expected law.
bool ParseRenderFileTree( const uint8_t *pFile, uint32_t fileSize, RenderSectionWrite &root, std::string &error );
