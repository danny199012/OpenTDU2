#include "shared.h"
#include "gs_file.h"

#include "system/cmd_line.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <unordered_set>

GSFile *gpFile = nullptr;

namespace {
std::string NormalizeGamePath( const char *pPath ) {
    std::string normalized;
    normalized.reserve( strlen( pPath ) );
    for ( const char *p = pPath; *p != '\0'; p++ ) {
        char c = ( *p == '\\' ) ? '/' : *p;
        if ( c == '/' && !normalized.empty() && normalized.back() == '/' ) {
            continue;
        }
        normalized += c;
    }
    while ( normalized.size() > 2 && normalized[0] == '.' &&
            normalized[1] == '/' ) {
        normalized.erase( 0, 2 );
    }
    return normalized;
}

bool HasBankFolder( const std::filesystem::path &root ) {
    std::error_code ec;
    if ( !std::filesystem::is_directory( root, ec ) ) {
        return false;
    }
    for ( const auto &entry : std::filesystem::directory_iterator( root, ec ) ) {
        if ( entry.is_directory() &&
             ToLower( entry.path().filename().string() ) == "euro" ) {
            for ( const auto &sub :
                  std::filesystem::directory_iterator( entry.path(), ec ) ) {
                if ( sub.is_directory() &&
                     ToLower( sub.path().filename().string() ) == "bnk" ) {
                    return true;
                }
            }
        }
    }
    return false;
}
} // namespace

GSFile::GSFile() { gpFile = this; }

GSFile::~GSFile() { gpFile = nullptr; }

bool GSFile::findGameRoot() {
    std::vector<std::filesystem::path> candidates;

    const char *pGameDirArg = CmdLine::GetArgValue( "-game" );
    if ( pGameDirArg != nullptr ) {
        candidates.push_back( pGameDirArg );
    }

    const char *pGameDirEnv = SDL_getenv( "OTDU2_GAME_DIR" );
    if ( pGameDirEnv != nullptr ) {
        candidates.push_back( pGameDirEnv );
    }

    std::error_code ec;
    candidates.push_back( std::filesystem::current_path( ec ) );

    const char *pBasePath = SDL_GetBasePath();
    if ( pBasePath != nullptr ) {
        std::filesystem::path basePath( pBasePath );
        for ( uint32_t i = 0; i < 5 && !basePath.empty(); i++ ) {
            candidates.push_back( basePath );
            basePath = basePath.parent_path();
        }
    }

    for ( const auto &candidate : candidates ) {
        if ( HasBankFolder( candidate ) ) {
            gameRoot = std::filesystem::absolute( candidate, ec );
            return true;
        }
    }
    return false;
}

bool GSFile::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );

    if ( !findGameRoot() ) {
        OTDU_LOG_ERROR(
            "Could not find the TDU2 game folder (a folder containing "
            "Euro/Bnk). Use '-game <path>' or set OTDU2_GAME_DIR.\n" );
        return false;
    }
    OTDU_LOG_INFO( "Game root: '%s'\n", gameRoot.string().c_str() );

    // -userdata <path> / OTDU2_USER_DIR: override the save and settings folder.
    // SDL_GetPrefPath() returns a single folder per user, shared by every copy
    // of the game, so two working trees would overwrite each other's saves.
    std::error_code ec;
    const char *pUserDataArg = CmdLine::GetArgValue( "-userdata" );
    const char *pUserDataEnv = SDL_getenv( "OTDU2_USER_DIR" );
    const char *pUserData =
        pUserDataArg != nullptr ? pUserDataArg : pUserDataEnv;
    if ( pUserData != nullptr ) {
        userDataFolder = std::filesystem::absolute( pUserData, ec );
    } else {
        char *pPrefPath = SDL_GetPrefPath( "OpenTDU2", "OpenTDU2" );
        if ( pPrefPath != nullptr ) {
            userDataFolder = pPrefPath;
            SDL_free( pPrefPath );
        } else {
            userDataFolder = gameRoot / "OpenTDU2";
        }
    }
    std::filesystem::create_directories( userDataFolder, ec );
    OTDU_LOG_INFO( "User data folder: '%s'\n",
                   userDataFolder.string().c_str() );

    // Mod overlay: '<modsRoot>/<game path>' shadows the game file with the
    // same path on every read. Defaults to '<user data>/mods' so two game
    // copies with different -userdata do not share mods; '-mods <dir>' or
    // OTDU2_MODS_DIR overrides it, -nomods disables the overlay entirely.
    if ( !CmdLine::HasArg( "-nomods" ) ) {
        const char *pModsArg = CmdLine::GetArgValue( "-mods" );
        const char *pModsEnv = SDL_getenv( "OTDU2_MODS_DIR" );
        const char *pMods = pModsArg != nullptr ? pModsArg : pModsEnv;
        if ( pMods != nullptr ) {
            modsRoot = std::filesystem::absolute( pMods, ec );
        } else {
            modsRoot = userDataFolder / "mods";
        }
        ec.clear();
        std::filesystem::create_directories( modsRoot, ec );
        if ( !std::filesystem::is_directory( modsRoot, ec ) ) {
            OTDU_LOG_WARN( "Mod overlay folder '%s' is unusable, disabled\n",
                           modsRoot.string().c_str() );
            modsRoot.clear();
        } else {
            OTDU_LOG_INFO( "Mod overlay folder: '%s'\n",
                           modsRoot.string().c_str() );
        }
    }
    return true;
}

void GSFile::terminate() {
    stopPrefetch();
    resolvedPathCache.clear();
}

std::optional<std::filesystem::path>
GSFile::resolveCaseInsensitive( const std::filesystem::path &base,
                                const std::string &normalizedPath ) {
    std::filesystem::path current = base;
    size_t start = 0;
    std::error_code ec;
    while ( start < normalizedPath.size() ) {
        size_t end = normalizedPath.find( '/', start );
        if ( end == std::string::npos ) {
            end = normalizedPath.size();
        }
        std::string component = normalizedPath.substr( start, end - start );
        start = end + 1;
        if ( component.empty() || component == "." ) {
            continue;
        }

        std::filesystem::path direct = current / component;
        if ( std::filesystem::exists( direct, ec ) ) {
            current = direct;
            continue;
        }

        const std::string lowerComponent = ToLower( component );
        bool bFound = false;
        for ( const auto &entry :
              std::filesystem::directory_iterator( current, ec ) ) {
            if ( ToLower( entry.path().filename().string() ) ==
                 lowerComponent ) {
                current = entry.path();
                bFound = true;
                break;
            }
        }
        if ( !bFound ) {
            return std::nullopt;
        }
    }
    return current;
}

std::optional<std::filesystem::path> GSFile::resolvePath( const char *pPath ) {
    if ( pPath == nullptr || *pPath == '\0' ) {
        return std::nullopt;
    }

    std::filesystem::path asIs( pPath );
    std::error_code ec;
    if ( asIs.is_absolute() && std::filesystem::exists( asIs, ec ) ) {
        return asIs;
    }

    std::string normalized = NormalizeGamePath( pPath );
    std::string key = ToLower( normalized );
    {
        std::lock_guard<std::mutex> lock( cacheMutex );
        auto it = resolvedPathCache.find( key );
        if ( it != resolvedPathCache.end() ) {
            return it->second;
        }
    }

    // Mod overlay first: a file with the same game path inside the mods
    // folder replaces the game file. The result is cached like any other
    // resolution, so files added to the mods folder after a path was already
    // resolved (or looked up and missed) are only seen after a restart.
    if ( !modsRoot.empty() ) {
        auto modded = resolveCaseInsensitive( modsRoot, normalized );
        if ( modded.has_value() ) {
            {
                std::lock_guard<std::mutex> lock( cacheMutex );
                resolvedPathCache[key] = *modded;
            }
            OTDU_LOG_INFO( "Mod overlay: '%s' -> '%s'\n", normalized.c_str(),
                           modded->string().c_str() );
            return modded;
        }
    }

    auto resolved = resolveCaseInsensitive( gameRoot, normalized );
    if ( resolved.has_value() ) {
        std::lock_guard<std::mutex> lock( cacheMutex );
        resolvedPathCache[key] = *resolved;
    }
    return resolved;
}

bool GSFile::fileExists( const char *pPath ) {
    auto resolved = resolvePath( pPath );
    std::error_code ec;
    return resolved.has_value() &&
           std::filesystem::is_regular_file( *resolved, ec );
}

bool GSFile::loadFile( const char *pPath,
                       void **ppOutBuffer,
                       uint32_t *pOutSize ) {
    *ppOutBuffer = nullptr;
    *pOutSize = 0;

    auto resolved = resolvePath( pPath );
    if ( !resolved.has_value() ) {
        OTDU_LOG_DEBUG( "File not found: '%s'\n", pPath );
        return false;
    }

    std::ifstream stream( *resolved, std::ios::binary | std::ios::ate );
    if ( !stream.is_open() ) {
        OTDU_LOG_WARN( "Failed to open '%s'\n", resolved->string().c_str() );
        return false;
    }

    std::streamsize size = stream.tellg();
    stream.seekg( 0, std::ios::beg );

    void *pBuffer = TestDrive::AllocAligned( static_cast<size_t>( size ) + 16, 16 );
    if ( pBuffer == nullptr ) {
        return false;
    }
    if ( !stream.read( static_cast<char *>( pBuffer ), size ) ) {
        TestDrive::FreeAligned( pBuffer );
        return false;
    }
    memset( static_cast<uint8_t *>( pBuffer ) + size, 0, 16 );

    *ppOutBuffer = pBuffer;
    *pOutSize = static_cast<uint32_t>( size );
    return true;
}

namespace {
std::string FileCacheKey( const std::filesystem::path &resolved ) {
    return ToLower( resolved.string() );
}

bool ReadWholeFile( const std::filesystem::path &path, std::vector<uint8_t> &outBuffer ) {
    std::ifstream stream( path, std::ios::binary | std::ios::ate );
    if ( !stream.is_open() ) {
        return false;
    }
    const std::streamsize size = stream.tellg();
    stream.seekg( 0, std::ios::beg );
    outBuffer.resize( static_cast<size_t>( size ) );
    return static_cast<bool>( stream.read( reinterpret_cast<char *>( outBuffer.data() ), size ) );
}
} // namespace

void GSFile::prefetchFile( const char *pPath ) {
    if ( fileCacheBudget == 0 ) {
        static const int32_t budgetMb = CmdLine::GetArgInt( "-filecachemb", 768 );
        if ( budgetMb <= 0 ) {
            return;
        }
        fileCacheBudget = static_cast<uint64_t>( budgetMb ) << 20;
    }
    // The path is resolved by the worker (resolvePath is locked): the first
    // lookup of a path walks the folders case-insensitively, ~0.3 ms each,
    // which on the main thread was 6.5 ms per ring of 25 banks.
    const std::string request = ToLower( pPath );
    std::lock_guard<std::mutex> lock( fileCacheMutex );
    if ( std::find( prefetchQueue.begin(), prefetchQueue.end(), request ) != prefetchQueue.end() ) {
        return;
    }
    prefetchQueue.push_back( request );
    if ( !prefetchThread.joinable() ) {
        bStopPrefetch = false;
        prefetchThread = std::thread( [this]() { prefetchLoop(); } );
    }
    fileCacheSignal.notify_one();
}

void GSFile::prefetchLoop() {
    for ( ;; ) {
        std::string request;
        {
            std::unique_lock<std::mutex> lock( fileCacheMutex );
            fileCacheSignal.wait( lock, [this]() { return bStopPrefetch || !prefetchQueue.empty(); } );
            if ( bStopPrefetch ) {
                return;
            }
            request = std::move( prefetchQueue.front() );
            prefetchQueue.pop_front();
        }
        const auto resolved = resolvePath( request.c_str() );
        if ( !resolved.has_value() ) {
            continue;
        }
        const std::string key = FileCacheKey( *resolved );
        {
            std::lock_guard<std::mutex> lock( fileCacheMutex );
            auto it = fileCache.find( key );
            if ( it != fileCache.end() ) {
                it->second.LastUse = ++fileCacheClock; // wanted again: keep it from eviction
                continue;
            }
            fileCache[key].LastUse = ++fileCacheClock;
            inFlightKey = key;
        }
        std::vector<uint8_t> data;
        const bool bRead = ReadWholeFile( std::filesystem::path( key ), data );
        std::lock_guard<std::mutex> lock( fileCacheMutex );
        inFlightKey.clear();
        auto it = fileCache.find( key );
        if ( it != fileCache.end() ) {
            if ( bRead ) {
                fileCacheBytes += data.size();
                it->second.Data = std::move( data );
                it->second.bReady = true;
                evictFileCache();
            } else {
                fileCache.erase( it );
            }
        }
        fileCacheSignal.notify_all(); // a loadFile may be waiting for this one
    }
}

void GSFile::evictFileCache() {
    while ( fileCacheBytes > fileCacheBudget ) {
        auto oldest = fileCache.end();
        for ( auto it = fileCache.begin(); it != fileCache.end(); ++it ) {
            if ( it->second.bReady && ( oldest == fileCache.end() || it->second.LastUse < oldest->second.LastUse ) ) {
                oldest = it;
            }
        }
        if ( oldest == fileCache.end() ) {
            return;
        }
        fileCacheBytes -= oldest->second.Data.size();
        fileCache.erase( oldest );
    }
}

void GSFile::stopPrefetch() {
    {
        std::lock_guard<std::mutex> lock( fileCacheMutex );
        bStopPrefetch = true;
        prefetchQueue.clear();
    }
    fileCacheSignal.notify_all();
    if ( prefetchThread.joinable() ) {
        prefetchThread.join();
    }
    fileCache.clear();
    fileCacheBytes = 0;
}

GSFile::FileCacheStats GSFile::getFileCacheStats() {
    std::lock_guard<std::mutex> lock( fileCacheMutex );
    FileCacheStats stats = fileCacheStats;
    stats.Bytes = fileCacheBytes;
    stats.Entries = static_cast<uint32_t>( fileCache.size() );
    return stats;
}

bool GSFile::loadFile( const char *pPath, std::vector<uint8_t> &outBuffer ) {
    outBuffer.clear();
    auto resolved = resolvePath( pPath );
    if ( !resolved.has_value() ) {
        OTDU_LOG_DEBUG( "File not found: '%s'\n", pPath );
        return false;
    }
    if ( fileCacheBudget != 0 ) {
        // A prefetched file is copied out; the entry stays for the next reader
        // (the collision loads the same sector bank right after the world).
        const std::string key = FileCacheKey( *resolved );
        std::unique_lock<std::mutex> lock( fileCacheMutex );
        auto it = fileCache.find( key );
        if ( it != fileCache.end() && !it->second.bReady ) {
            // Only the file the worker is reading right now has an entry that
            // is not ready: its read is the quickest way to the bytes.
            fileCacheSignal.wait( lock, [&]() { return inFlightKey != key; } );
            it = fileCache.find( key );
        }
        if ( it != fileCache.end() && it->second.bReady ) {
            it->second.LastUse = ++fileCacheClock;
            outBuffer = it->second.Data;
            fileCacheStats.Hits++;
            return true;
        }
        fileCacheStats.Misses++;
        lock.unlock();
        if ( !ReadWholeFile( *resolved, outBuffer ) ) {
            outBuffer.clear();
            return false;
        }
        // A sector bank is kept for its next reader (the world reads it, then
        // the collision). Only those: copying every start-up file into the
        // cache cost ~30 ms on frame 1 for files nobody reads twice.
        constexpr size_t kMaxCachedFile = 32u << 20;
        const std::string fileName = ToLower( resolved->filename().string() );
        const bool bSectorBank = fileName.rfind( "sector-", 0 ) == 0 && fileName.size() > 4 &&
                                 fileName.compare( fileName.size() - 4, 4, ".bnk" ) == 0;
        if ( bSectorBank && outBuffer.size() <= kMaxCachedFile ) {
            lock.lock();
            if ( fileCache.count( key ) == 0 ) {
                CachedFile &entry = fileCache[key];
                entry.Data = outBuffer;
                entry.bReady = true;
                entry.LastUse = ++fileCacheClock;
                fileCacheBytes += entry.Data.size();
                evictFileCache();
            }
        }
        return true;
    }
    return ReadWholeFile( *resolved, outBuffer );
}

std::vector<std::filesystem::path> GSFile::listFolder( const char *pPath,
                                                       const char *pExtension ) {
    std::vector<std::filesystem::path> files;
    const std::string normalized = NormalizeGamePath( pPath );
    const std::string lowerExtension = pExtension != nullptr ? ToLower( pExtension ) : "";
    std::error_code ec;
    std::unordered_set<std::string> seen;

    // Overlay entries with the same name replace (not duplicate) the game ones,
    // so the overlay is listed first and the game folder adds the rest.
    if ( !modsRoot.empty() ) {
        auto moddedFolder = resolveCaseInsensitive( modsRoot, normalized );
        if ( moddedFolder.has_value() ) {
            for ( const auto &entry : std::filesystem::directory_iterator( *moddedFolder, ec ) ) {
                if ( !entry.is_regular_file() ) {
                    continue;
                }
                if ( !lowerExtension.empty() &&
                     ToLower( entry.path().extension().string() ) != lowerExtension ) {
                    continue;
                }
                seen.insert( ToLower( entry.path().filename().string() ) );
                files.push_back( entry.path() );
            }
        }
    }

    // The game folder is resolved against the game root, **not** through
    // resolvePath: when the folder itself is modded, resolvePath returns the
    // overlay copy, and the game's own entries were then never listed at all -
    // one file in `<mods>/Euro/Bnk/<folder>` hid every other file of that game
    // folder (the asset index walk found 1 bank instead of 9,454).
    auto gameFolder = resolveCaseInsensitive( gameRoot, normalized );
    if ( gameFolder.has_value() ) {
        for ( const auto &entry : std::filesystem::directory_iterator( *gameFolder, ec ) ) {
            if ( !entry.is_regular_file() ) {
                continue;
            }
            if ( !lowerExtension.empty() &&
                 ToLower( entry.path().extension().string() ) != lowerExtension ) {
                continue;
            }
            if ( !seen.insert( ToLower( entry.path().filename().string() ) ).second ) {
                continue;
            }
            files.push_back( entry.path() );
        }
    }

    std::sort( files.begin(), files.end() );
    return files;
}
