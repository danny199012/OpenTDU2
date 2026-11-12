#pragma once

// The launcher's updater. Two sources, picked by where the exe lives:
//   - a git checkout (a .git above the launcher): "git fetch origin", the
//     commits between HEAD and origin/<branch> are the changelog, and a
//     fast-forward merge applies them when the tree is on that branch;
//   - a distributed zip (no .git): the GitHub releases API (or the branch's
//     commit list when there is no release), with the release notes as the
//     changelog and the first .zip asset as the package.
// All network/process work runs on a worker thread; the UI reads a snapshot.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Launcher {

struct UpdateEntry {
    std::string Id;    // short hash or tag
    std::string When;  // date
    std::string Title; // subject / release name
    std::string Body;  // release notes / the full commit subject
    std::string Url;
    std::string Commit;             // target_commitish: what the release was built from
    bool bDetailsCollapsed = false; // long commit message: shown behind "details"
};

struct UpdateState {
    mutable std::mutex Mutex;
    std::string Phase = "Not checked yet";
    std::string Source; // "git checkout" / "GitHub releases"
    std::string CurrentBuild;
    std::string LatestName;
    std::string LatestTag;
    std::string Summary;
    std::string Error;
    std::string Hint;
    std::string DownloadUrl;
    std::string ReleasePage;
    std::string RepoSlug;
    std::string Branch;
    std::string RepoRoot;
    std::string WhatsNew; // commit subjects between this build and the newest release
    bool bAvailable = false;
    bool bCanApply = false;
    std::atomic<bool> bChecked{ false };
    bool bStaged = false;
    std::vector<UpdateEntry> Entries;

    std::atomic<bool> bBusy{ false };
    std::atomic<bool> bCancel{ false };
    std::atomic<uint64_t> DownloadDone{ 0 };
    std::atomic<uint64_t> DownloadTotal{ 0 };

    void SetPhase( const std::string &text ) {
        std::lock_guard<std::mutex> lock( Mutex );
        Phase = text;
    }
    std::string GetPhase() {
        std::lock_guard<std::mutex> lock( Mutex );
        return Phase;
    }
};

struct UpdateSnapshot {
    std::string Phase;
    std::string Source;
    std::string CurrentBuild;
    std::string LatestName;
    std::string LatestTag;
    std::string Summary;
    std::string Error;
    std::string Hint;
    std::string DownloadUrl;
    std::string ReleasePage;
    std::string RepoSlug;
    std::string Branch;
    std::string RepoRoot;
    std::string WhatsNew;
    bool bAvailable = false;
    bool bCanApply = false;
    bool bChecked = false;
    bool bStaged = false;
    bool bBusy = false;
    uint64_t DownloadDone = 0;
    uint64_t DownloadTotal = 0;
    std::vector<UpdateEntry> Entries;
};

UpdateSnapshot Snapshot( UpdateState &state );

// Full check (blocking - call it on a worker thread).
void CheckForUpdates( const std::string &repoSlug, const std::string &branch,
                      const std::string &lastTag, const std::wstring &launcherDir,
                      UpdateState &state );

// git: "git merge --ff-only origin/<branch>". False with state.Error set.
bool ApplyGitUpdate( UpdateState &state );

// GitHub: downloads the release asset to %TEMP%\opentdu2-update\package.zip and
// unpacks it into %TEMP%\opentdu2-update\staged (tar.exe, shipped with Windows).
bool DownloadReleasePackage( UpdateState &state );

// Writes %TEMP%\opentdu2-update\apply-update.cmd (waits for this process to
// exit, backs the launcher exe up, copies the staged files over the launcher
// folder while logging every step to apply.log, and starts the launcher again)
// and runs it with CREATE_NO_WINDOW. The caller closes the launcher right
// after. The script survives a failed apply (and says why in the log); it
// deletes itself only when the copy succeeded.
bool StartStagedUpdate( const std::wstring &launcherDir, UpdateState &state );

// The apply-script generator (the selftest builds and executes its output):
// the cmd text that waits for 'pid' to exit, logs to 'logPath', backs
// 'launcherExe' up, xcopy's the 'staged' tree over 'installDir' and restarts
// the launcher.
std::string BuildApplyScript( const std::string &pid, const std::wstring &launcherExe,
                              const std::wstring &staged, const std::wstring &installDir,
                              const std::wstring &logPath );

// %TEMP%\opentdu2-update: the staging folder, tag.txt (the staged release's
// tag) and apply.log live under it.
std::wstring UpdateRootDir();

// %TEMP%\opentdu2-update\apply.log - every step the apply script runs.
std::wstring UpdateApplyLogPath();

// The staging folder (also shown in the UI when an update is staged).
std::wstring UpdateStagingDir();

// The staging folder actually holds files - a re-detected stale dir must not
// offer an empty "Restart to apply".
bool StagedDirHasFiles( const std::wstring &staged );

// GitHub release markdown as plain text: images dropped, links keep their
// text, bold/heading/backtick markers off - what the Updates page renders.
std::string StripMarkdownNoise( const std::string &markdown );

// ---------------------------------------------------------------------------
// "Update & rebuild" (a git checkout only): the fast-forward above cannot apply
// here (the tree carries its own merge commits), and neither can the release
// download - the new exe has to be BUILT. This runs the repo's
// update-and-rebuild.bat, which fetches origin, merges main + the agent
// branches and rebuilds build-<branch>\bin\opentdu2-<branch>.exe. The output
// streams into RebuildState::Log while it runs (a build takes minutes); the
// worker thread is joined by the UI when bDone turns true.
struct RebuildState {
    mutable std::mutex Mutex;
    std::string Phase = "Not run yet";
    std::string Log; // the streamed bat output, capped (the last kLogCap bytes)
    std::atomic<bool> bRunning{ false };
    std::atomic<uint32_t> ChildPid{ 0 }; // the bat's cmd, while the run is going
    int ExitCode = 0;
    std::atomic<bool> bDone{ false }; // a run finished since the launcher started
    bool bSucceeded = false;
    HANDLE hJob = nullptr;   // the bat's kill-on-close job (guarded by Mutex)

    void SetPhase( const std::string &text ) {
        std::lock_guard<std::mutex> lock( Mutex );
        Phase = text;
    }
    // Appends to Log; keeps the LAST kLogCap bytes (a build prints a lot).
    void AppendLog( const std::string &text ) {
        std::lock_guard<std::mutex> lock( Mutex );
        constexpr size_t kLogCap = 256 * 1024;
        Log += text;
        if ( Log.size() > kLogCap ) {
            static const char kCut[] = "(... earlier output cut ...)\r\n";
            Log = kCut + Log.substr( Log.size() - kLogCap );
        }
    }
};

struct RebuildSnapshot {
    std::string Phase;
    std::string Log;
    bool bRunning = false;
    bool bDone = false;
    bool bSucceeded = false;
    int ExitCode = 0;
};

RebuildSnapshot Snapshot( RebuildState &state );

// Blocking - call it on a worker thread. Runs "<repoRoot>\update-and-rebuild.bat"
// with the repo root as the working directory, streaming its output. The bat's
// process tree sits in a kill-on-close job (state.hJob): closing that handle
// (the launcher shutting down) takes it down. False when it could not start.
bool RunUpdateAndRebuild( const std::wstring &repoRoot, RebuildState &state );

} // namespace Launcher
