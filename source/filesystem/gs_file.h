#pragma once

#include "game/gs_base.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// Virtual file system. TDU2 normally reads its data from the "bigfiles"
// (bigfile_EU_*.map + TDU2v*.big). OpenTDU2 only supports the unpacked layout
// (the one the TDU2 Universal Launcher enables), i.e.:
//   <GameRoot>/Euro/Bnk/...      banks (.bnk), databases (.cpr), sounds...
//   <GameRoot>/Euro/Shaders/...  shader libraries (.sob)
//   <GameRoot>/*.cpr             configuration files
//
// Paths given to the VFS are the ones used by the original game (relative to
// the game root, '\' or '/' separators, any case). They are resolved case
// insensitively since the original data has inconsistent casing.
//
// Mod overlay: the mods folder (default '<user data>/mods', override with
// '-mods <dir>' or OTDU2_MODS_DIR, disable with -nomods) mirrors the game
// layout. A file present there shadows the game file with the same path for
// every read (fileExists / loadFile / listFolder), which unlocks texture,
// shader, audio, database and UI mods without touching the game data. The
// overlay is read-only: nothing is ever written into it.
class GSFile : public GameSystem {
public:
    inline const std::filesystem::path &getGameRoot() const {
        return gameRoot;
    }
    inline const std::filesystem::path &getUserDataFolder() const {
        return userDataFolder;
    }
    inline const std::filesystem::path &getModsRoot() const {
        return modsRoot;
    }
    // Mod packages (docs/re/dev_build_survey.md 4): '<mods>/packages/<name>/'
    // folders listed, in priority order, in '<mods>/enabled.txt' (one name per
    // line, '#' comments). They shadow the game exactly like the loose mods
    // folder, which keeps the highest priority.
    inline const std::vector<std::filesystem::path> &getModPackages() const {
        return modPackages;
    }

    // Root of the bank folder ("Euro/Bnk/"), as used by the format strings of
    // the original executable (e.g. "%sVehicules/High/%s.bnk").
    inline std::string getBankFolder() const { return "Euro/Bnk/"; }
    inline std::string getShaderFolder() const { return "Euro/Shaders/"; }

public:
    GSFile();
    ~GSFile();

    const char *getName() const override { return "Service : File"; }
    bool initialize( TestDriveGameInstance *pGameInstance ) override;
    void tick( float deltaTime ) override {}
    void terminate() override;

    bool fileExists( const char *pPath );
    std::optional<std::filesystem::path> resolvePath( const char *pPath );

    // Loads the whole file in memory (16 bytes aligned). Caller owns the
    // memory (free with TestDrive::FreeAligned).
    bool loadFile( const char *pPath, void **ppOutBuffer, uint32_t *pOutSize );
    bool loadFile( const char *pPath, std::vector<uint8_t> &outBuffer );

    std::vector<std::filesystem::path> listFolder( const char *pPath,
                                                   const char *pExtension );

    // RAM file cache with a background reader (2026-11-04). prefetchFile
    // queues a file; a worker thread reads it into memory, and the next
    // loadFile( path, vector ) of that path is a memcpy instead of a disk read.
    // Used by the world streaming: the sector banks ahead of the car are read
    // before they are needed (the world and the collision both load the same
    // sector bank). The entries stay until the budget evicts the least recently
    // used (-filecachemb <n>, default 768; 0 = off). The prefetch only reads
    // bytes - parsing stays with the caller on the main thread.
    void prefetchFile( const char *pPath );
    struct FileCacheStats {
        uint32_t Hits = 0;
        uint32_t Misses = 0;
        uint64_t Bytes = 0; // resident
        uint32_t Entries = 0;
    };
    FileCacheStats getFileCacheStats();

private:
    struct CachedFile {
        std::vector<uint8_t> Data;
        uint64_t LastUse = 0;
        bool bReady = false;
    };
    std::unordered_map<std::string, CachedFile> fileCache; // key: resolved path, lowercase
    std::deque<std::string> prefetchQueue;
    std::string inFlightKey; // the file the worker is reading now
    std::mutex fileCacheMutex;
    std::condition_variable fileCacheSignal;
    std::thread prefetchThread;
    bool bStopPrefetch = false;
    uint64_t fileCacheClock = 0;
    uint64_t fileCacheBytes = 0;
    uint64_t fileCacheBudget = 0;
    FileCacheStats fileCacheStats;
    void prefetchLoop();
    void evictFileCache(); // fileCacheMutex held
    void stopPrefetch();

private:
    std::filesystem::path gameRoot;
    std::filesystem::path userDataFolder;
    std::filesystem::path modsRoot;
    std::vector<std::filesystem::path> modPackages; // enabled packages, priority order
    std::unordered_map<std::string, std::filesystem::path> resolvedPathCache;
    std::mutex cacheMutex;

private:
    bool findGameRoot();
    // Walks 'base' following the '/' components of normalizedPath, accepting
    // any casing on every level. Used for both the game root and the mod
    // overlay root.
    std::optional<std::filesystem::path>
    resolveCaseInsensitive( const std::filesystem::path &base,
                            const std::string &normalizedPath );
};

extern GSFile *gpFile;
