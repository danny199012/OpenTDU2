#pragma once

// "Install the game from a .iso": inspect the image, find the game data, copy
// it out with progress and cancel. Runs on a worker thread of the launcher.

#include <atomic>
#include <mutex>
#include <string>

namespace Launcher {

struct IsoInspection {
    bool bOpened = false;
    std::string Error;
    std::string Filesystem;
    std::string Label;
    uint64_t ImageSize = 0;
    std::string GameRoot; // "" = the image root is the game root
    bool bHasGameData = false;
    bool bPackedRetail = false;   // bigfile_*.map / TDU2v*.big (not readable yet)
    bool bLooksLikeInstaller = false;
    uint32_t FileCount = 0;
    uint64_t TotalBytes = 0;
    std::string Note; // the one-line verdict the Game Data page shows
};

IsoInspection InspectIso( const std::wstring &isoPath );

struct InstallProgress {
    std::atomic<bool> bCancel{ false };
    std::atomic<bool> bRunning{ false };
    std::atomic<bool> bDone{ false };
    std::atomic<bool> bOk{ false };
    std::atomic<uint64_t> BytesDone{ 0 };
    std::atomic<uint64_t> BytesTotal{ 0 };
    std::atomic<uint32_t> FilesDone{ 0 };
    std::atomic<uint32_t> FilesTotal{ 0 };
    std::atomic<uint32_t> FilesSkipped{ 0 };

    void SetCurrentFile( const std::string &path ) {
        std::lock_guard<std::mutex> lock( mMutex );
        CurrentFile = path;
    }
    std::string GetCurrentFile() {
        std::lock_guard<std::mutex> lock( mMutex );
        return CurrentFile;
    }
    void SetError( const std::string &text ) {
        std::lock_guard<std::mutex> lock( mMutex );
        Error = text;
    }
    std::string GetError() {
        std::lock_guard<std::mutex> lock( mMutex );
        return Error;
    }

    std::mutex mMutex;
    std::string CurrentFile;
    std::string Error;
};

// Extracts every file under 'gameRoot' to 'destination' (so that
// <destination>\Euro\Bnk exists afterwards). Files already present with the
// same size are skipped, which makes a restarted extraction resume.
bool ExtractGameFromIso( const std::wstring &isoPath, const std::string &gameRoot,
                         const std::wstring &destination, InstallProgress &progress );

// Free bytes on the volume that holds 'path'.
uint64_t FreeSpaceOf( const std::wstring &path );

} // namespace Launcher
