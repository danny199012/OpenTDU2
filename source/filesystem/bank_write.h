#pragma once

// The Eden resource bank (.bnk) WRITER - the C++ port of
// `tools/x360/build_bank.py` (M5, docs/re/scene_editor.md 6): the engine and
// the tools only read banks; scenes exported natively (a building bank, a
// patched sector) need the write side in the engine, not in Python.
//
// Layout (the reader's mirror, source/filesystem/bank.h):
//   0x00 { u32 headerSize (0x40); u32 CRC }
//   0x08 BankHeader (0x40): magic `KNAB`, flags, version, type, sizes,
//        alignments, NumEntries, creation date, section offsets
//   0x48 "STNICC2000 RULEZPADDING " filler (0x18)
//   sections, each prefixed { u32 size; u32 CRC } (content at +8),
//   32-aligned: Table (NumEntries + 1) x 0x14, Types 4 bytes per entry,
//   Directory (compressed path tree), Indexes u8/u16/u32 per entry
//   (directory order -> entry index)
//   Data: the payloads, 16-aligned.
//
// CRCs are zlib crc32 - the engine never verifies them and Eden's own
// algorithm is not reproduced (the same caveat as build_bank.py). A REBUILD
// of an existing bank can copy every source CRC instead, which makes the
// rebuild byte-identical to the retail file when the layout matches
// (otdu2_bnk_tool roundtrip proves it per bank).
#include <cstdint>
#include <string>
#include <vector>

struct BnkWriteEntry {
    std::string Path; // decoded bank path `dir\name.ext` (the reader's law)
    std::vector<uint8_t> Data;
    int16_t SubType = 0;
    int16_t Group = 0;
    int32_t Date = 0; // per-entry date (0 = the bank's creation date)
    // The payload CRC: computed (zlib crc32) unless copied from a source
    // bank (a whole-file-identical rebuild; retail CRCs are Eden's own
    // algorithm, not zlib, and their uint32 values flip negative as int32 -
    // hence the explicit flag, no sentinel).
    bool bCopyCrc = false;
    uint32_t Crc = 0;
};

// A captured directory tree node (the roundtrip's source form): retail trees
// may list the same directory name twice (commonworld's `objects` holds
// `vegetations` twice, interleaving its leaves), so a rebuild captures the
// node structure verbatim instead of rebuilding it from the leaf paths.
struct BnkDirectoryNode {
    std::string Name;
    bool bLeaf = false;
    std::vector<BnkDirectoryNode> Children;
};

struct BnkWriteOptions {
    int32_t CreationDate = 0;
    int16_t BankType = 0;
    // The retail layout is per file: section / data / file alignment live in
    // the header (icd_ferr packs sections 4-aligned with no filler, the
    // frontend banks 32-align with the "STNICC2000 RULEZPADDING " filler,
    // every bank pads the file to a 2048-byte multiple). The defaults are
    // the common writer's values; a rebuild copies its source's.
    uint32_t SectionAlignment = 0x20;
    uint32_t DataAlignment = 0x10;
    uint32_t FileAlignment = 0x800;
    // The paths in the original tree order (parseDirectory's order of a
    // source bank): the rebuilt directory bytes come out identical. Empty:
    // the entries' own order builds the tree.
    std::vector<std::string> DirectoryOrder;
    // The captured raw tree (see BnkDirectoryNode): when set it is encoded
    // verbatim (byte-identical rebuilds even for the duplicate-named trees);
    // DirectoryOrder is then only the leaf order fallback. Empty: the tree
    // builds from the paths.
    BnkDirectoryNode DirectoryTree;
    bool bHasDirectoryTree = false;
    // Copy the section CRCs from a source bank (whole-file-identical
    // rebuilds): header, table, types, directory, indexes - all five or
    // none (a rebuild knows them all; a fresh pack computes all).
    bool bCopySectionCrcs = false;
    uint32_t HeaderCrc = 0;
    uint32_t TableCrc = 0;
    uint32_t TypesCrc = 0;
    uint32_t DirectoryCrc = 0;
    uint32_t IndexesCrc = 0;
    // The bank's stream region: everything after the LAST entry's payload
    // (the retail files carry streamed texture tails there; the header's
    // SizeWithoutPadding points into the middle of the last big texture -
    // its streamed mips - so the region is copied verbatim and the header
    // field copied from the source, not recomputed).
    std::vector<uint8_t> StreamTail;
    // The region between the indexes section and the data: some banks (the
    // streamed sector / avatar ones) carry an extra per-entry stream table
    // there, which the reader never touches. Copied verbatim on a rebuild;
    // empty for fresh packs.
    std::vector<uint8_t> ExtraSection;
    int32_t SizeWithoutPadding = -1; // -1: the end of the payloads
};

// Packs the entries into a PC bank (`KNAB`). Throws nothing; an empty entry
// list is a valid (empty) bank.
std::vector<uint8_t> BuildBank( const std::vector<BnkWriteEntry> &entries,
                                const BnkWriteOptions &options = BnkWriteOptions() );

// The compressed path tree for `paths` (the directory section's content,
// without the { size, crc } prefix) - exposed for tests and tools.
std::vector<uint8_t> BuildBankDirectory( const std::vector<std::string> &paths );

// The same encoder over a captured tree (the roundtrip's form; the same
// node law, so one encoder is validated by both routes).
std::vector<uint8_t> BuildBankDirectory( const BnkDirectoryNode &root );

// Captures the raw tree of a bank's directory section (the reader law of
// Bank::parseDirectory, structure kept: child order, duplicate names, empty
// directories). The leaf paths decode into `outLeaves` in tree order.
bool CaptureBankDirectoryTree( const uint8_t *pDirectory, const uint8_t *pDirectoryEnd,
                               BnkDirectoryNode &outRoot, std::vector<std::string> &outLeaves );

// GetIdentifier64bit of a spot name is taken; here the whole-file rebuild's
// helper: zlib crc32 (the standard table, no dependency).
uint32_t BnkCrc32( const uint8_t *pData, size_t size );
inline uint32_t BnkCrc32( const std::vector<uint8_t> &data ) {
    return BnkCrc32( data.data(), data.size() );
}
