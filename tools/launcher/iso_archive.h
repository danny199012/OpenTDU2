#pragma once

// Read-only ISO image reader for the launcher's "install from a .iso" flow.
//
// Two filesystems, because the game's discs use both:
//   - ISO 9660 with Joliet (and Rock Ridge names when a disc has them): the
//     classic PC CD/DVD layout, multi-extent files included (a .big over 4 GB).
//   - UDF (2.01 / 2.02 / 2.60, a plain physical partition): the layout of the
//     later DVDs and of the game's Blu-ray images.
// A hybrid image (the retail TDU2 discs are ISO 9660 + UDF) prefers UDF only
// when it is readable; otherwise it falls back to the ISO 9660 tree.
//
// Not supported, reported with a clear message instead of a wrong answer:
// UDF with a Metadata Partition (Blu-ray UDF 2.50 - the PS3 image), UDF sparing
// tables, extended_ad allocation descriptors.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Launcher {

struct IsoExtent {
    uint64_t Offset = 0; // absolute byte offset in the image file
    uint64_t Size = 0;
};

struct IsoFile {
    std::string Path; // UTF-8, '/'-separated, no leading slash, "" = the root
    uint64_t Size = 0;
    bool bDirectory = false;
    std::vector<IsoExtent> Extents;
};

struct IsoVolume {
    std::string Filesystem; // "ISO 9660 (Joliet)", "UDF 2.01", ...
    std::string Label;
    uint64_t ImageSize = 0;
};

class IsoArchive {
public:
    IsoArchive() = default;
    ~IsoArchive();
    IsoArchive( const IsoArchive & ) = delete;
    IsoArchive &operator=( const IsoArchive & ) = delete;

    bool Open( const std::wstring &path, std::string &outError );
    void Close();
    bool IsOpen() const { return mFile != INVALID_HANDLE_VALUE; }

    const IsoVolume &Volume() const { return mVolume; }
    const std::vector<IsoFile> &Files() const { return mFiles; }
    uint64_t TotalBytes() const { return mTotalBytes; }
    const std::string &LastError() const { return mLastError; }

    // Exact entry lookup (path as printed by the listing).
    const IsoFile *FindEntry( const std::string &path ) const;
    bool FindDirectory( const std::string &path, std::string &outActualPath ) const;

    // The folder that contains Euro/Bnk: outPrefix is "" when the image root is
    // the game root, "TestDrive2" when the game sits in one folder. bOutPacked
    // is true when the image holds the retail packed layout instead
    // (bigfile_*.map + TDU2v*.big), which OpenTDU2 cannot read yet.
    bool FindGameRoot( std::string &outPrefix, bool &bOutPacked ) const;

    // Reads up to 'size' bytes of a file at a logical offset; returns the bytes
    // actually read (0 on error).
    uint32_t ReadFile( const IsoFile &file, uint64_t offset, void *buffer, uint32_t size );

private:
    bool ReadAt( uint64_t offset, void *buffer, uint32_t size );
    bool OpenIso9660( std::string &outError );
    bool OpenUdf( std::string &outError );

    HANDLE mFile = INVALID_HANDLE_VALUE;
    IsoVolume mVolume;
    std::vector<IsoFile> mFiles;
    uint64_t mTotalBytes = 0;
    std::string mLastError;
};

} // namespace Launcher
