#pragma once

// Small Win32 / std helpers of the tester launcher (tools/launcher). No engine
// link: the launcher is a stand-alone GUI exe shipped beside the game exe.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Launcher {

// UTF-8 <-> UTF-16 (the settings hold UTF-8, the Win32 calls want UTF-16).
std::wstring Utf8ToWide( const std::string &text );
std::string WideToUtf8( const std::wstring &text );
std::string ToLowerAscii( std::string text );

// The launcher's own directory (no trailing separator) and the ini beside it.
std::wstring ExeDir();
std::wstring ExePath();
std::wstring IniPath();

// %APPDATA%\OpenTDU2\OpenTDU2 - SDL_GetPrefPath("OpenTDU2","OpenTDU2"), the
// game's default user data folder (saves, options.ini, mods).
std::wstring DefaultUserDataDir();

bool FileExists( const std::wstring &path );
bool DirectoryExists( const std::wstring &path );
uint64_t FileSizeOf( const std::wstring &path );
bool MakeDirs( const std::wstring &path );
std::wstring JoinPath( const std::wstring &a, const std::wstring &b );
std::wstring BaseName( const std::wstring &path );
std::wstring ParentDir( const std::wstring &path );

// "13.4 GB", "742 KB" - for the UI.
std::string FormatBytes( uint64_t bytes );

bool ReadTextFile( const std::wstring &path, std::string &outText );
bool WriteTextFile( const std::wstring &path, const std::string &text );

// Modern (IFileOpenDialog) pickers. Both return false when the user cancels.
bool BrowseFolder( HWND owner, const wchar_t *title, std::wstring &outPath );
bool BrowseFile( HWND owner, const wchar_t *title, const wchar_t *filter,
                 std::wstring &outPath );

bool OpenInExplorer( const std::wstring &path );
bool OpenUrl( const std::wstring &url );
bool RevealInExplorer( const std::wstring &path );

// Windows command line quoting (CommandLineToArgvW rules).
std::wstring QuoteArg( const std::wstring &arg );

// The game's command line (everything after the exe): -game, -playername,
// -tester (the watermark, when enabled and a name is set), -userdata,
// -skipintro, -onlineusername, -onlineserver. Shared by the launcher's PLAY
// button and the self test so the two can never drift.
struct GameLaunchOptions {
    std::string GameFolder;
    std::string PlayerName;
    std::string UserDataFolder;
    std::string SaveFile; // -savefile: one profile's profile.sav (the profile manager)
    std::string OnlineUsername;
    std::string OnlineServer;
    bool bWatermark = true;
    bool bSkipIntro = false;
};

std::wstring BuildGameArguments( const GameLaunchOptions &options );

struct ProcessResult {
    bool bStarted = false;
    bool bTimedOut = false;
    DWORD ExitCode = 0;
    std::string Out; // stdout + stderr, UTF-8 as the child wrote it
    std::string Error;
};

// Called with each raw chunk the child writes (its code page, mostly ASCII)
// while it runs - the "Update & rebuild" log streams through it. Null: the
// output only lands in ProcessResult::Out.
using ProcessOutputSink = std::function<void( const char *data, size_t size )>;

// Runs a process with CREATE_NO_WINDOW and captures its output. timeoutMs 0:
// wait forever. The child's stdin is the NUL device (a .bat's final "pause"
// returns at once), and when hJob is non-null the child joins that job object
// (kill-on-close: closing the job handle takes the whole tree down). outPid,
// when given, is published as soon as the child exists - while the run is
// still going (the rebuild's shutdown kill needs the pid then).
ProcessResult RunProcess( const std::wstring &commandLine,
                          const std::wstring &workingDir, uint32_t timeoutMs,
                          const ProcessOutputSink &sink = nullptr,
                          HANDLE hJob = nullptr,
                          std::atomic<uint32_t> *outPid = nullptr );

// A job object that kills every process in it when the last handle closes.
// The rebuild runner puts the bat's cmd tree in one, so closing the launcher
// cannot leave a half-finished update / build running.
HANDLE CreateKillOnCloseJob();

// Terminates 'pid' and every descendant (the snapshot's ParentProcessId walk).
// The rebuild runner's shutdown fallback for the job object above.
void KillProcessTree( DWORD pid );

// Finds git.exe (PATH, then the usual install folders). Empty when missing.
std::wstring FindGitExecutable();

// Looks for a .git directory walking up from 'startDir' (the launcher lives in
// <repo>\build-*\bin). Empty when the launcher is a distributed zip.
std::wstring FindRepositoryRoot( const std::wstring &startDir );

// The game exe a launch would use: an explicit override, then opentdu2*.exe
// beside the launcher (a distributed zip), then ..\build-*\bin (a checkout).
// Only opentdu2.exe / opentdu2-<workspace>.exe count - never the tools.
bool FindGameExecutable( const std::wstring &launcherDir,
                         const std::wstring &overridePath,
                         std::wstring &outPath, std::string &outError );

// GSFile::HasBankFolder's rule: a game folder contains Euro/Bnk.
bool IsValidGameFolder( const std::wstring &folder );

// The first tag of a .iso: "ISO 9660", "UDF", "" - a cheap sanity check before
// the full reader opens the image.
std::string ProbeIsoFilesystem( const std::wstring &isoPath );

uint32_t Fnv1a32( const void *data, size_t size );

} // namespace Launcher
