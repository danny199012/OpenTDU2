#include "shared.h"
#include "watchdog.h"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
// The process exit code of a watchdog kill ('WATC' - greppable in the
// console's %errorlevel% and in crash reports).
#ifdef _WIN32
constexpr uint32_t kKillExitCode = 0x43544157u;
#else
constexpr int32_t kKillExitCode = 0x43544157;
#endif

std::thread gThread;
std::atomic<bool> gbStop( false );
std::atomic<bool> gbRunning( false );
std::atomic<int64_t> gStartMs( 0 );
std::atomic<int64_t> gLastBeatMs( 0 );
std::atomic<int64_t> gWorstGapMs( 0 );
std::atomic<uint64_t> gWorstGapFrame( 0 );
std::atomic<uint64_t> gLastFrame( 0 );
std::atomic<uint64_t> gBeats( 0 );
std::atomic<uint64_t> gStalls( 0 );
std::atomic<const char *> gpPhase( "start" );
std::atomic<int32_t> gMainLogDrops( 0 );
int32_t gIntervalMs = 5000;
// The kill guard: 0 = off. The default is set by SetKillThreshold's caller
// (-watchdogkill <s>, default 60 s).
std::atomic<int64_t> gKillThresholdMs( 60 * 1000 );

std::mutex gOwnMutex;
FILE *gpOwnFile = nullptr;
std::string gOwnPath;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch() )
        .count();
}

// The watchdog's own file first (nothing else can block it), then the game
// log when its lock is free.
void WriteLine( const char *pFormat, ... ) {
    char message[512];
    va_list args;
    va_start( args, pFormat );
    vsnprintf( message, sizeof( message ), pFormat, args );
    va_end( args );

    {
        std::lock_guard<std::mutex> lock( gOwnMutex );
        if ( gpOwnFile != nullptr ) {
            fprintf( gpOwnFile, "[%8.1f] %s\n",
                     ( NowMs() - gStartMs.load() ) / 1000.0, message );
            fflush( gpOwnFile );
        }
    }
    if ( Logger::WriteTagged( eLogLevel::LL_Info, "watchdog", "%s\n", message ) ) {
        gMainLogDrops.store( 0 );
    } else {
        gMainLogDrops.fetch_add( 1 );
    }
}

// A single collision with a normal log line is harmless; several in a row
// mean the main thread is stuck holding the log lock.
const char *LogLockHint() {
    return gMainLogDrops.load() >= 3
               ? " [the game log is locked: the main thread is inside a log call]"
               : "";
}

std::string DeriveOwnPath( const char *pLogPath ) {
    std::string path = pLogPath;
    if ( path.size() >= 4 && path.compare( path.size() - 4, 4, ".log" ) == 0 ) {
        path.resize( path.size() - 4 );
    }
    return path + "-watchdog.log";
}

void ThreadMain() {
    int64_t nextHeartbeatMs = NowMs() + gIntervalMs;
    int64_t nextStallReportMs = 0;
    bool bStalled = false;
    int64_t stallStartMs = 0;
    const char *pStalledPhase = "start";
    while ( !gbStop.load() ) {
        // 40 ms slices so that Shutdown() joins promptly.
        for ( int i = 0; i < 5 && !gbStop.load(); i++ ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 40 ) );
        }
        if ( gbStop.load() ) {
            break;
        }

        const int64_t nowMs = NowMs();
        const int64_t gapMs = nowMs - gLastBeatMs.load();
        const uint64_t frame = gLastFrame.load();
        const char *pPhase = gpPhase.load();

        if ( gapMs >= gIntervalMs ) {
            // The kill guard: the kill deadline counts from the last beat, so
            // a threshold shorter than the report cadence still lands within
            // one loop slice (~200 ms) of it.
            const int64_t killThresholdMs = gKillThresholdMs.load();
            if ( killThresholdMs > 0 && gapMs >= killThresholdMs ) {
#ifdef _WIN32
                if ( IsDebuggerPresent() ) {
                    // A breakpoint freezes every thread, the wall clock keeps
                    // running, and the gap after continuing is not a stall:
                    // re-arm from now for as long as a debugger is attached.
                    WriteLine( "kill suppressed: a debugger is attached "
                               "(gap %.1f s, phase '%s')",
                               gapMs / 1000.0, pPhase );
                    gLastBeatMs.store( NowMs() );
                    continue;
                }
#endif
                WriteLine( "KILL: no frame for %.1f s (last frame %llu, phase "
                           "'%s'), kill threshold %.0f s - terminating the "
                           "process (exit code 0x43544157)%s",
                           gapMs / 1000.0, static_cast<unsigned long long>( frame ),
                           pPhase, killThresholdMs / 1000.0, LogLockHint() );
                std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
#ifdef _WIN32
                TerminateProcess( GetCurrentProcess(), kKillExitCode );
#else
                _Exit( kKillExitCode );
#endif
            }
            if ( !bStalled ) {
                bStalled = true;
                stallStartMs = gLastBeatMs.load();
                pStalledPhase = pPhase;
                gStalls.fetch_add( 1 );
                nextStallReportMs = nowMs + Max<int64_t>( gIntervalMs, 1000 );
                WriteLine( "STALL: no frame for %.1f s (last frame %llu, phase "
                           "'%s')%s",
                           gapMs / 1000.0, static_cast<unsigned long long>( frame ),
                           pPhase, LogLockHint() );
            } else if ( nowMs >= nextStallReportMs ) {
                nextStallReportMs = nowMs + Max<int64_t>( gIntervalMs, 1000 );
                WriteLine( "STALL: still no frame - %.1f s now (last frame %llu, "
                           "phase '%s')%s",
                           gapMs / 1000.0, static_cast<unsigned long long>( frame ),
                           pPhase, LogLockHint() );
            }
            continue;
        }
        if ( bStalled ) {
            bStalled = false;
            WriteLine( "recovered: frame %llu after a %.1f s stall (phase '%s')",
                       static_cast<unsigned long long>( frame ),
                       ( nowMs - stallStartMs ) / 1000.0, pStalledPhase );
        }
        if ( nowMs >= nextHeartbeatMs ) {
            nextHeartbeatMs = nowMs + gIntervalMs;
            WriteLine( "alive: frame %llu, %.1f s in, %llu beats, last gap %lld ms, "
                       "worst gap %lld ms (frame %llu)",
                       static_cast<unsigned long long>( frame ),
                       ( nowMs - gStartMs.load() ) / 1000.0,
                       static_cast<unsigned long long>( gBeats.load() ),
                       static_cast<long long>( gapMs ),
                       static_cast<long long>( gWorstGapMs.load() ),
                       static_cast<unsigned long long>( gWorstGapFrame.load() ) );
        }
    }
}
} // namespace

void Watchdog::Initialize( const char *pLogPath, int32_t intervalSeconds,
                           const char *pOwnLogPath ) {
    if ( gbRunning.load() || intervalSeconds <= 0 ) {
        return;
    }
    if ( pLogPath == nullptr && pOwnLogPath == nullptr ) {
        // "-nolog" and no explicit watchdog file: nothing to write to.
        return;
    }

    gIntervalMs = intervalSeconds * 1000;
    gStartMs.store( NowMs() );
    gLastBeatMs.store( gStartMs.load() );
    gOwnPath = pOwnLogPath != nullptr ? pOwnLogPath : DeriveOwnPath( pLogPath );
    {
        std::lock_guard<std::mutex> lock( gOwnMutex );
        gpOwnFile = fopen( gOwnPath.c_str(), "w" );
        if ( gpOwnFile != nullptr ) {
            fprintf( gpOwnFile,
                     "[%8.1f] watchdog: heartbeat every %d s; kill threshold %.0f s; "
                     "the frame loop reports "
                     "through Watchdog::Beat (phase + frame)\n",
                     0.0, intervalSeconds, gKillThresholdMs.load() / 1000.0 );
            fflush( gpOwnFile );
        }
    }

    gbStop.store( false );
    gbRunning.store( true );
    gThread = std::thread( ThreadMain );

    if ( gpOwnFile != nullptr ) {
        OTDU_LOG_INFO( "Watchdog: frame loop watched every %d s, kill threshold %.0f s, own log '%s'\n",
                       intervalSeconds, gKillThresholdMs.load() / 1000.0, gOwnPath.c_str() );
    } else {
        OTDU_LOG_WARN( "Watchdog: cannot write '%s' - the game log only\n",
                       gOwnPath.c_str() );
    }
}

void Watchdog::SetKillThreshold( int32_t seconds ) {
    const int64_t previous = gKillThresholdMs.exchange( static_cast<int64_t>( seconds ) * 1000 );
    if ( previous != gKillThresholdMs.load() && gbRunning.load() ) {
        WriteLine( "kill threshold %d s (was %.0f s)",
                   seconds, previous / 1000.0 );
    }
}

void Watchdog::Shutdown() {
    if ( !gbRunning.exchange( false ) ) {
        return;
    }
    gbStop.store( true );
    if ( gThread.joinable() ) {
        gThread.join();
    }
    WriteLine( "stopped after %.1f s: %llu beats, %llu stalls, worst frame gap "
               "%lld ms (frame %llu)",
               ( NowMs() - gStartMs.load() ) / 1000.0,
               static_cast<unsigned long long>( gBeats.load() ),
               static_cast<unsigned long long>( gStalls.load() ),
               static_cast<long long>( gWorstGapMs.load() ),
               static_cast<unsigned long long>( gWorstGapFrame.load() ) );
    std::lock_guard<std::mutex> lock( gOwnMutex );
    if ( gpOwnFile != nullptr ) {
        fclose( gpOwnFile );
        gpOwnFile = nullptr;
    }
}

bool Watchdog::IsRunning() { return gbRunning.load(); }

void Watchdog::Beat( const char *pPhase, uint64_t frameIndex ) {
    if ( !gbRunning.load() ) {
        return;
    }
    const int64_t nowMs = NowMs();
    const int64_t previousMs = gLastBeatMs.exchange( nowMs );
    gpPhase.store( pPhase );
    gLastFrame.store( frameIndex );
    gBeats.fetch_add( 1 );
    if ( previousMs > 0 ) {
        const int64_t gapMs = nowMs - previousMs;
        if ( gapMs > gWorstGapMs.load() ) {
            gWorstGapMs.store( gapMs );
            gWorstGapFrame.store( frameIndex );
        }
    }
}

