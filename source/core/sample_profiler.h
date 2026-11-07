#pragma once

#include <cstdint>

// In-process sampling profiler of the main thread (docs/PERF_PLAN.md 0a).
//
// ETW / xperf need an elevated shell, which the agent workspaces do not have,
// so this is the profiler that needs nothing: a thread suspends the main
// thread about once a millisecond, unwinds its stack with the image's own
// x64 unwind data (RtlVirtualUnwind - no frame pointers needed) and resumes
// it. At exit the addresses are symbolized with DbgHelp and a report lists
// the functions by exclusive samples (the leaf) and by inclusive samples (on
// the stack at all), so a pass's cost can be split down to its callees.
//
//   -sampleprof [<first frame>,<last frame>]   (default 240,600)
//
// Symbols need a PDB next to the exe: configure a separate build dir with
// /Z7 + /DEBUG (docs/PERF_PLAN.md 0a has the command); without one the
// report prints module offsets. Windows only; a no-op elsewhere.
namespace SampleProfiler {
// Call from the main thread (the thread that is sampled).
void Start( const char *pReportPath );
// The main loop's frame number: samples are kept only inside the window.
void SetFrame( uint32_t frame );
// Stops the sampler and writes the report (and its head into the game log).
void Shutdown();
} // namespace SampleProfiler
