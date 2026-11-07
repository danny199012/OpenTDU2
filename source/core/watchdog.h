#pragma once

#include <cstdint>

// Frame loop watchdog.
//
// The owner's report: "I drive for a while and the game goes unresponsive".
// A freeze inside the frame loop - a fence that never signals, a deadlock in
// a subsystem - writes nothing at all, so the log simply stops mid-frame and
// there is no trace of where it stopped. This is a small thread that watches
// the main loop's heartbeat: the loop calls Beat() as it moves through the
// frame and the thread turns silence into a report - which phase stopped, at
// which frame, and for how long - written both into the game log and into its
// own file, which a hung thread cannot block.
//
// On top of the reports it is also a kill guard (the owner asked for it after
// a full system freeze during testing): when no frame has run for the kill
// threshold (-watchdogkill <s>, default 60 s, 0 = off) the watchdog thread
// terminates the process instead of leaving a wedged app / a busy GPU on the
// machine (exit code 0x43544157). A debugger attached suppresses the kill
// (a breakpoint freezes every thread, the wall clock keeps running and the
// gap after continuing is not a real stall). Known-long jobs beat through it
// (the -scantunnels survey, the asset index build).
namespace Watchdog {
// pLogPath: the game log path (nullptr = "-nolog": the watchdog is then off
// unless pOwnLogPath is given). The watchdog's own file is derived from it
// ("opentdu2.log" -> "opentdu2-watchdog.log") unless pOwnLogPath overrides
// it. intervalSeconds: heartbeat period, and the silence that counts as a
// stall; 0 disables the watchdog.
void Initialize( const char *pLogPath, int32_t intervalSeconds,
                 const char *pOwnLogPath );
// The kill guard: terminate the process when the frame loop has been silent
// for this many seconds (0 = off). Safe to call before or after Initialize.
void SetKillThreshold( int32_t seconds );
void Shutdown();
bool IsRunning();

// The main loop reports its progress. pPhase must be a static string naming
// the section that is about to run ("update", "draw", "present", ...): a
// stall report names the code that hung. Two atomic stores - frame thread
// only. Long jobs on any thread may also beat (the survey, the index build):
// the phase name then tells the kill guard (and the reports) who is alive.
void Beat( const char *pPhase, uint64_t frameIndex );
} // namespace Watchdog
