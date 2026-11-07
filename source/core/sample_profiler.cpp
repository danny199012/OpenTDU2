#include "shared.h"
#include "sample_profiler.h"

#include "system/cmd_line.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment( lib, "dbghelp.lib" )
#pragma comment( lib, "winmm.lib" )

namespace {
constexpr uint32_t kMaxFrames = 32;

struct Sample {
    uint32_t NumFrames = 0;
    uint64_t Frames[kMaxFrames] = {};
};

HANDLE gMainThread = nullptr;
std::thread gThread;
std::atomic<bool> gbRunning{ false };
std::atomic<uint32_t> gFrame{ 0 };
uint32_t gFirstFrame = 240;
uint32_t gLastFrame = 600;
std::vector<Sample> gSamples;
std::string gReportPath;

// Unwinds the suspended thread's context. No allocation in here: the main
// thread may be holding the heap lock while it is suspended.
void Unwind( CONTEXT &context, Sample &sample ) {
    sample.NumFrames = 0;
    for ( uint32_t depth = 0; depth < kMaxFrames && context.Rip != 0; depth++ ) {
        sample.Frames[sample.NumFrames++] = context.Rip;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION pEntry = RtlLookupFunctionEntry( context.Rip, &imageBase, nullptr );
        if ( pEntry == nullptr ) {
            // A leaf function: the return address is at the top of the stack.
            if ( context.Rsp == 0 ) {
                break;
            }
            context.Rip = *reinterpret_cast<const DWORD64 *>( context.Rsp );
            context.Rsp += 8;
            continue;
        }
        PVOID pHandlerData = nullptr;
        DWORD64 establisherFrame = 0;
        RtlVirtualUnwind( UNW_FLAG_NHANDLER, imageBase, context.Rip, pEntry, &context, &pHandlerData,
                          &establisherFrame, nullptr );
    }
}

void SamplerThread() {
    timeBeginPeriod( 1 );
    Sample sample;
    while ( gbRunning.load() ) {
        Sleep( 1 );
        const uint32_t frame = gFrame.load();
        if ( frame < gFirstFrame || frame > gLastFrame ) {
            continue;
        }
        if ( SuspendThread( gMainThread ) == static_cast<DWORD>( -1 ) ) {
            continue;
        }
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_FULL;
        if ( GetThreadContext( gMainThread, &context ) ) {
            Unwind( context, sample );
        } else {
            sample.NumFrames = 0;
        }
        ResumeThread( gMainThread );
        if ( sample.NumFrames > 0 ) {
            gSamples.push_back( sample );
        }
    }
    timeEndPeriod( 1 );
}

std::string Symbolize( uint64_t address, std::unordered_map<uint64_t, std::string> &cache ) {
    auto it = cache.find( address );
    if ( it != cache.end() ) {
        return it->second;
    }
    char buffer[sizeof( SYMBOL_INFO ) + 512] = {};
    SYMBOL_INFO *pSymbol = reinterpret_cast<SYMBOL_INFO *>( buffer );
    pSymbol->SizeOfStruct = sizeof( SYMBOL_INFO );
    pSymbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    std::string name;
    if ( SymFromAddr( GetCurrentProcess(), address, &displacement, pSymbol ) ) {
        name = pSymbol->Name;
    } else {
        IMAGEHLP_MODULE64 module = {};
        module.SizeOfStruct = sizeof( module );
        char text[320];
        if ( SymGetModuleInfo64( GetCurrentProcess(), address, &module ) ) {
            snprintf( text, sizeof( text ), "%s+0x%llx", module.ModuleName,
                      static_cast<unsigned long long>( address - module.BaseOfImage ) );
        } else {
            snprintf( text, sizeof( text ), "0x%llx", static_cast<unsigned long long>( address ) );
        }
        name = text;
    }
    cache.emplace( address, name );
    return name;
}
} // namespace

void SampleProfiler::Start( const char *pReportPath ) {
    if ( gbRunning.load() ) {
        return;
    }
    if ( const char *pWindow = CmdLine::GetArgValue( "-sampleprof" ) ) {
        unsigned first = 0, last = 0;
        if ( sscanf( pWindow, "%u,%u", &first, &last ) == 2 && last > first ) {
            gFirstFrame = first;
            gLastFrame = last;
        }
    }
    DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &gMainThread,
                     THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0 );
    gReportPath = pReportPath != nullptr ? pReportPath : "opentdu2-profile.txt";
    gSamples.reserve( 1 << 16 );
    gbRunning = true;
    gThread = std::thread( SamplerThread );
    OTDU_LOG_INFO( "SampleProfiler: sampling the main thread over frames %u..%u -> %s\n", gFirstFrame, gLastFrame,
                   gReportPath.c_str() );
}

void SampleProfiler::SetFrame( uint32_t frame ) {
    gFrame.store( frame );
}

void SampleProfiler::Shutdown() {
    if ( !gbRunning.load() ) {
        return;
    }
    gbRunning = false;
    if ( gThread.joinable() ) {
        gThread.join();
    }
    CloseHandle( gMainThread );
    gMainThread = nullptr;

    SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
    SymInitialize( GetCurrentProcess(), nullptr, TRUE );
    std::unordered_map<uint64_t, std::string> nameCache;
    std::unordered_map<std::string, uint32_t> exclusive, inclusive;
    for ( const Sample &sample : gSamples ) {
        exclusive[Symbolize( sample.Frames[0], nameCache )]++;
        std::unordered_set<std::string> seen;
        for ( uint32_t i = 0; i < sample.NumFrames; i++ ) {
            const std::string name = Symbolize( sample.Frames[i], nameCache );
            if ( seen.insert( name ).second ) {
                inclusive[name]++;
            }
        }
    }
    SymCleanup( GetCurrentProcess() );

    auto sorted = []( const std::unordered_map<std::string, uint32_t> &counts ) {
        std::vector<std::pair<std::string, uint32_t>> list( counts.begin(), counts.end() );
        std::sort( list.begin(), list.end(), []( const auto &a, const auto &b ) { return a.second > b.second; } );
        return list;
    };
    const double total = static_cast<double>( Max<size_t>( gSamples.size(), 1 ) );
    std::string report;
    char line[640];
    snprintf( line, sizeof( line ), "SampleProfiler: %zu samples of the main thread, frames %u..%u\n\n",
              gSamples.size(), gFirstFrame, gLastFrame );
    report += line;
    report += "== exclusive (leaf) ==\n";
    const auto exclusiveList = sorted( exclusive );
    for ( size_t i = 0; i < exclusiveList.size() && i < 80; i++ ) {
        snprintf( line, sizeof( line ), "%6.2f%%  %6u  %s\n", 100.0 * exclusiveList[i].second / total,
                  exclusiveList[i].second, exclusiveList[i].first.c_str() );
        report += line;
    }
    report += "\n== inclusive (on the stack) ==\n";
    const auto inclusiveList = sorted( inclusive );
    for ( size_t i = 0; i < inclusiveList.size() && i < 160; i++ ) {
        snprintf( line, sizeof( line ), "%6.2f%%  %6u  %s\n", 100.0 * inclusiveList[i].second / total,
                  inclusiveList[i].second, inclusiveList[i].first.c_str() );
        report += line;
    }
    if ( FILE *pFile = fopen( gReportPath.c_str(), "wb" ) ) {
        fwrite( report.data(), 1, report.size(), pFile );
        fclose( pFile );
    }
    OTDU_LOG_INFO( "SampleProfiler: %zu samples, report written to %s\n", gSamples.size(), gReportPath.c_str() );
    gSamples.clear();
}

#else
void SampleProfiler::Start( const char * ) {}
void SampleProfiler::SetFrame( uint32_t ) {}
void SampleProfiler::Shutdown() {}
#endif
