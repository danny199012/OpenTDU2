#include "launcher_updater.h"

#include "launcher_http.h"
#include "launcher_json.h"
#include "launcher_util.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "build_info.h"

namespace Launcher {

namespace {

const char *kUserAgent = "OpenTDU2-Launcher/" OTDU_BUILD_NUMBER;

std::string Trim( std::string text ) {
    while ( !text.empty() && ( text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ) ) {
        text.pop_back();
    }
    const size_t start = text.find_first_not_of( " \t\r\n" );
    return start == std::string::npos ? std::string() : text.substr( start );
}

std::string FirstLine( const std::string &text ) {
    const size_t newline = text.find_first_of( "\r\n" );
    return Trim( newline == std::string::npos ? text : text.substr( 0, newline ) );
}

// "https://github.com/owner/repo(.git)" or "git@github.com:owner/repo.git"
std::string RepoSlugFromRemote( const std::string &remote ) {
    std::string text = Trim( remote );
    const size_t github = text.find( "github.com" );
    if ( github == std::string::npos ) {
        return std::string();
    }
    size_t start = github + std::string( "github.com" ).size();
    while ( start < text.size() && ( text[start] == '/' || text[start] == ':' ) ) {
        start++;
    }
    std::string slug = text.substr( start );
    if ( slug.size() > 4 && slug.compare( slug.size() - 4, 4, ".git" ) == 0 ) {
        slug.resize( slug.size() - 4 );
    }
    return Trim( slug );
}

} // namespace

UpdateSnapshot Snapshot( UpdateState &state ) {
    std::lock_guard<std::mutex> lock( state.Mutex );
    UpdateSnapshot snapshot;
    snapshot.Phase = state.Phase;
    snapshot.Source = state.Source;
    snapshot.CurrentBuild = state.CurrentBuild;
    snapshot.LatestName = state.LatestName;
    snapshot.LatestTag = state.LatestTag;
    snapshot.Summary = state.Summary;
    snapshot.Error = state.Error;
    snapshot.Hint = state.Hint;
    snapshot.DownloadUrl = state.DownloadUrl;
    snapshot.ReleasePage = state.ReleasePage;
    snapshot.RepoSlug = state.RepoSlug;
    snapshot.Branch = state.Branch;
    snapshot.RepoRoot = state.RepoRoot;
    snapshot.bAvailable = state.bAvailable;
    snapshot.bCanApply = state.bCanApply;
    snapshot.bChecked = state.bChecked.load();
    snapshot.bStaged = state.bStaged;
    snapshot.Entries = state.Entries;
    snapshot.bBusy = state.bBusy.load();
    snapshot.DownloadDone = state.DownloadDone.load();
    snapshot.DownloadTotal = state.DownloadTotal.load();
    return snapshot;
}

std::wstring UpdateStagingDir() {
    wchar_t tempPath[MAX_PATH * 2] = L"";
    const DWORD len = GetTempPathW( MAX_PATH * 2, tempPath );
    std::wstring dir( tempPath, len );
    return JoinPath( JoinPath( dir, L"opentdu2-update" ), L"staged" );
}

namespace {

void FillFromGit( UpdateState &state, const std::wstring &repoRoot, const std::wstring &gitPath,
                  const std::string &targetBranch ) {
    const std::wstring prefix = L"\"" + gitPath + L"\" -C \"" + repoRoot + L"\" ";
    const auto run = [&]( const std::wstring &arguments, uint32_t timeoutMs ) {
        return RunProcess( prefix + arguments, repoRoot, timeoutMs );
    };

    const ProcessResult branchResult = run( L"rev-parse --abbrev-ref HEAD", 15000 );
    const std::string currentBranch = Trim( branchResult.Out );

    const ProcessResult remoteResult = run( L"remote get-url origin", 15000 );
    const std::string slug = RepoSlugFromRemote( remoteResult.Out );

    const ProcessResult statusResult = run( L"status --porcelain", 30000 );
    const bool bDirty = !Trim( statusResult.Out ).empty();

    state.SetPhase( "Fetching origin..." );
    const ProcessResult fetchResult = run( L"fetch origin --prune --quiet", 180000 );
    if ( fetchResult.bTimedOut ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "git fetch timed out.";
        state.Summary = "Could not reach the update server.";
        return;
    }

    const std::wstring logArguments =
        L"log --date=short --pretty=format:%h%x09%ad%x09%s HEAD..origin/" +
        Utf8ToWide( targetBranch ) + L" -n 100";
    const ProcessResult logResult = run( logArguments, 30000 );

    std::vector<UpdateEntry> entries;
    size_t pos = 0;
    const std::string &logText = logResult.Out;
    while ( pos < logText.size() ) {
        const size_t newline = logText.find( '\n', pos );
        const std::string line = logText.substr(
            pos, newline == std::string::npos ? std::string::npos : newline - pos );
        pos = newline == std::string::npos ? logText.size() : newline + 1;
        const size_t firstTab = line.find( '\t' );
        const size_t secondTab =
            firstTab == std::string::npos ? std::string::npos : line.find( '\t', firstTab + 1 );
        UpdateEntry entry;
        if ( secondTab == std::string::npos ) {
            entry.Title = Trim( line );
        } else {
            entry.Id = line.substr( 0, firstTab );
            entry.When = line.substr( firstTab + 1, secondTab - firstTab - 1 );
            const std::string subject = Trim( line.substr( secondTab + 1 ) );
            // The repo's commit subjects are paragraphs; the list shows the
            // first clause and keeps the whole text behind "details".
            std::string title = subject;
            size_t cut = title.find( " - " );
            if ( cut == std::string::npos ) {
                cut = title.find( ": " );
            }
            if ( cut != std::string::npos && cut >= 8 ) {
                title.resize( cut );
            }
            if ( title.size() > 150 ) {
                title.resize( 147 );
                title += "...";
            }
            entry.Title = title;
            entry.bDetailsCollapsed = subject.size() > title.size();
            if ( entry.bDetailsCollapsed ) {
                entry.Body = subject;
            }
        }
        if ( !entry.Title.empty() ) {
            entries.push_back( entry );
        }
    }

    std::lock_guard<std::mutex> lock( state.Mutex );
    state.Source = "Git checkout";
    state.Branch = currentBranch.empty() ? targetBranch : currentBranch;
    state.RepoSlug = slug;
    state.RepoRoot = WideToUtf8( repoRoot );
    state.Entries = entries;
    state.bAvailable = !entries.empty();
    if ( !entries.empty() ) {
        state.LatestName = entries.front().Title;
        state.LatestTag = entries.front().Id;
    }
    if ( !state.RepoSlug.empty() ) {
        state.ReleasePage = "https://github.com/" + state.RepoSlug + "/commits/" + targetBranch;
    }

    if ( !fetchResult.bStarted ) {
        state.Error = fetchResult.Error;
        state.Summary = "git could not be started.";
        return;
    }
    if ( entries.empty() ) {
        state.Summary = "Up to date with origin/" + targetBranch + ".";
        state.Hint = "No new commits. The running build is " OTDU_BUILD_STRING ".";
        return;
    }

    state.Summary = std::to_string( entries.size() ) + " new commit" +
                    ( entries.size() == 1 ? "" : "s" ) + " on origin/" + targetBranch + ".";
    if ( bDirty ) {
        state.Hint = "The checkout has uncommitted changes. Commit or stash them first - "
                     "the updater only fast-forwards.";
    } else if ( currentBranch != targetBranch ) {
        state.Hint = "This checkout is on '" + currentBranch + "'. Switch to '" + targetBranch +
                     "' to let the launcher fast-forward it, or merge origin/" + targetBranch +
                     " yourself.";
    } else {
        state.bCanApply = true;
        state.Hint = "Ready to fast-forward '" + currentBranch + "'. Rebuild the game "
                     "afterwards (build.ps1).";
    }
}

} // namespace


namespace {

void FillFromGitHub( UpdateState &state, const std::string &repoSlug,
                     const std::string &branch, const std::string &lastTag ) {
    state.SetPhase( "Contacting GitHub..." );
    const std::string releaseUrl =
        "https://api.github.com/repos/" + repoSlug + "/releases?per_page=10";
    const HttpResponse response = HttpGet( releaseUrl, kUserAgent );

    std::vector<UpdateEntry> entries;
    std::string latestTag;
    std::string latestName;
    std::string downloadUrl;
    std::string releasePage;

    if ( response.bOk ) {
        JsonValue root;
        std::string parseError;
        if ( JsonValue::Parse( response.Body, root, parseError ) && root.IsArray() ) {
            for ( const JsonValue &release : root.AsArray() ) {
                if ( !release.IsObject() ) {
                    continue;
                }
                const JsonValue *draft = release.Find( "draft" );
                if ( draft != nullptr && draft->GetType() == JsonValue::Type::Bool &&
                     draft->AsBool() ) {
                    continue;
                }
                UpdateEntry entry;
                entry.Id = release.GetString( "tag_name" );
                entry.Title = release.GetString( "name", entry.Id );
                entry.Body = release.GetString( "body" );
                const std::string published = release.GetString( "published_at" );
                entry.When = published.size() >= 10 ? published.substr( 0, 10 ) : published;
                entry.Url = release.GetString( "html_url" );
                if ( entry.Id.empty() && entry.Title.empty() ) {
                    continue;
                }
                if ( latestTag.empty() ) {
                    latestTag = entry.Id;
                    latestName = entry.Title;
                    releasePage = entry.Url;
                    const JsonValue *assets = release.Find( "assets" );
                    if ( assets != nullptr && assets->IsArray() ) {
                        for ( const JsonValue &asset : assets->AsArray() ) {
                            const std::string name = asset.GetString( "name" );
                            const std::string url = asset.GetString( "browser_download_url" );
                            if ( url.empty() ) {
                                continue;
                            }
                            const bool bZip =
                                name.size() > 4 &&
                                ToLowerAscii( name.substr( name.size() - 4 ) ) == ".zip";
                            if ( downloadUrl.empty() || bZip ) {
                                downloadUrl = url;
                            }
                            if ( bZip ) {
                                break;
                            }
                        }
                    }
                }
                entries.push_back( entry );
            }
        }
    }

    if ( !entries.empty() ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Source = "GitHub releases (" + repoSlug + ")";
        state.Entries = entries;
        state.LatestTag = latestTag;
        state.LatestName = latestName;
        state.ReleasePage = releasePage;
        state.DownloadUrl = downloadUrl;
        state.bAvailable = lastTag.empty() ? true : latestTag != lastTag;
        state.bCanApply = !downloadUrl.empty();
        state.Summary = "Latest release: " + ( latestTag.empty() ? latestName : latestTag ) +
                        ( entries.front().When.empty() ? "" : " (" + entries.front().When + ")" );
        state.Hint = downloadUrl.empty()
                         ? "This release has no .zip asset - open the release page."
                         : "The launcher downloads the package, then applies it on exit.";
        return;
    }


    // No releases: the branch's commits are the changelog.
    state.SetPhase( "Reading the branch changelog..." );
    const std::string commitsUrl =
        "https://api.github.com/repos/" + repoSlug + "/commits?sha=" + branch + "&per_page=30";
    const HttpResponse commits = HttpGet( commitsUrl, kUserAgent );
    if ( !commits.bOk ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Source = "GitHub (" + repoSlug + ")";
        state.Error = response.bOk ? commits.Error : response.Error;
        state.Summary = "Could not read the update feed.";
        state.Hint = "Check the update source on the Updates page (launcher.ini: update.repo).";
        return;
    }
    JsonValue root;
    std::string parseError;
    if ( !JsonValue::Parse( commits.Body, root, parseError ) || !root.IsArray() ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Source = "GitHub (" + repoSlug + ")";
        state.Error = "The GitHub answer is not JSON: " + parseError;
        return;
    }
    std::vector<UpdateEntry> commitEntries;
    for ( const JsonValue &commit : root.AsArray() ) {
        UpdateEntry entry;
        entry.Id = commit.GetString( "sha" );
        if ( entry.Id.size() > 7 ) {
            entry.Id.resize( 7 );
        }
        const JsonValue *commitNode = commit.Find( "commit" );
        if ( commitNode != nullptr && commitNode->IsObject() ) {
            entry.Title = FirstLine( commitNode->GetString( "message" ) );
            const JsonValue *author = commitNode->Find( "author" );
            if ( author != nullptr && author->IsObject() ) {
                const std::string date = author->GetString( "date" );
                entry.When = date.size() >= 10 ? date.substr( 0, 10 ) : date;
            }
        }
        entry.Url = commit.GetString( "html_url" );
        if ( !entry.Title.empty() ) {
            commitEntries.push_back( entry );
        }
    }

    std::lock_guard<std::mutex> lock( state.Mutex );
    state.Source = "GitHub commits (" + repoSlug + "/" + branch + ")";
    state.Entries = commitEntries;
    state.ReleasePage = "https://github.com/" + repoSlug + "/commits/" + branch;
    state.bAvailable = false;
    state.bCanApply = false;
    if ( !commitEntries.empty() ) {
        state.LatestName = commitEntries.front().Title;
        state.LatestTag = commitEntries.front().Id;
        state.Summary = "No release is published yet - " +
                        std::to_string( commitEntries.size() ) + " recent commit" +
                        ( commitEntries.size() == 1 ? "" : "s" ) + " on " + branch + ".";
        state.Hint = commitEntries.front().Id == OTDU_BUILD_HASH
                         ? "This launcher is built from the newest commit."
                         : "A packaged build updates from releases; a checkout updates with git.";
    } else {
        state.Summary = "No commits in the feed.";
    }
}

} // namespace

void CheckForUpdates( const std::string &repoSlug, const std::string &branch,
                      const std::string &lastTag, const std::wstring &launcherDir,
                      UpdateState &state ) {
    state.bBusy = true;
    state.bCancel = false;
    state.DownloadDone = 0;
    state.DownloadTotal = 0;
    {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.CurrentBuild = OTDU_BUILD_STRING;
        state.Source.clear();
        state.LatestName.clear();
        state.LatestTag.clear();
        state.Summary.clear();
        state.Error.clear();
        state.Hint.clear();
        state.DownloadUrl.clear();
        state.ReleasePage.clear();
        state.RepoSlug = repoSlug;
        state.Branch = branch;
        state.RepoRoot.clear();
        state.Entries.clear();
        state.bAvailable = false;
        state.bCanApply = false;
        state.bChecked = false;
        state.bStaged = false;
    }
    state.SetPhase( "Checking for updates..." );

    const std::wstring repoRoot = FindRepositoryRoot( launcherDir );
    const std::wstring git = FindGitExecutable();
    if ( !repoRoot.empty() && !git.empty() ) {
        FillFromGit( state, repoRoot, git, branch );
    } else {
        if ( !repoRoot.empty() ) {
            state.SetPhase( "git.exe was not found - using the GitHub API..." );
        }
        FillFromGitHub( state, repoSlug, branch, lastTag );
    }

    {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.bChecked = true;
        state.Phase = "Check finished.";
    }
    state.bBusy = false;
}

namespace {

std::wstring FindTarExecutable() {
    wchar_t found[MAX_PATH * 2] = L"";
    if ( SearchPathW( nullptr, L"tar.exe", nullptr, MAX_PATH * 2, found, nullptr ) > 0 ) {
        return found;
    }
    if ( FileExists( L"C:\\Windows\\System32\\tar.exe" ) ) {
        return L"C:\\Windows\\System32\\tar.exe";
    }
    return std::wstring();
}

// A GitHub source zip nests everything under one folder; move its children up
// so the apply script copies the tree itself.
void FlattenSingleDirectory( const std::wstring &staged ) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> entries;
    for ( const fs::directory_entry &entry : fs::directory_iterator( staged, ec ) ) {
        entries.push_back( entry.path() );
    }
    if ( entries.size() != 1 || !fs::is_directory( entries[0], ec ) ) {
        return;
    }
    const fs::path inner = entries[0];
    std::vector<fs::path> children;
    for ( const fs::directory_entry &entry : fs::directory_iterator( inner, ec ) ) {
        children.push_back( entry.path() );
    }
    for ( const fs::path &child : children ) {
        std::error_code moveError;
        fs::rename( child, fs::path( staged ) / child.filename(), moveError );
    }
    std::error_code removeError;
    fs::remove( inner, removeError );
}

} // namespace

bool ApplyGitUpdate( UpdateState &state ) {
    const UpdateSnapshot snapshot = Snapshot( state );
    const std::wstring git = FindGitExecutable();
    if ( snapshot.RepoRoot.empty() || git.empty() || !snapshot.bCanApply ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "No fast-forward is available for this checkout.";
        return false;
    }
    state.SetPhase( "Merging origin/" + snapshot.Branch + "..." );
    const std::wstring repoRoot = Utf8ToWide( snapshot.RepoRoot );
    const std::wstring command = L"\"" + git + L"\" -C \"" + repoRoot +
                                 L"\" merge --ff-only origin/" +
                                 Utf8ToWide( snapshot.Branch );
    const ProcessResult result = RunProcess( command, repoRoot, 120000 );
    if ( result.bTimedOut || result.ExitCode != 0 ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = result.bTimedOut ? "git merge timed out." : Trim( result.Out );
        state.Phase = "Update failed.";
        return false;
    }
    std::lock_guard<std::mutex> lock( state.Mutex );
    state.Phase = "Updated.";
    state.bAvailable = false;
    state.bCanApply = false;
    state.Summary = "Fast-forwarded to origin/" + snapshot.Branch + ".";
    state.Hint = Trim( result.Out ) +
                 "\nRebuild the game (build.ps1) and restart the launcher to use the new code.";
    return true;
}


bool DownloadReleasePackage( UpdateState &state ) {
    const UpdateSnapshot snapshot = Snapshot( state );
    if ( snapshot.DownloadUrl.empty() ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "This release has no package to download.";
        return false;
    }
    state.bBusy = true;
    state.bCancel = false;
    state.DownloadDone = 0;
    state.DownloadTotal = 0;
    state.SetPhase( "Downloading " + snapshot.LatestTag + "..." );

    wchar_t tempPath[MAX_PATH * 2] = L"";
    const DWORD tempLength = GetTempPathW( MAX_PATH * 2, tempPath );
    if ( tempLength == 0 ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "No temporary folder.";
        return false;
    }
    const std::wstring root =
        JoinPath( std::wstring( tempPath, tempLength ), L"opentdu2-update" );
    MakeDirs( root );
    const std::wstring zip = JoinPath( root, L"package.zip" );
    DeleteFileW( zip.c_str() );

    std::string error;
    const bool bDownloaded = HttpDownloadToFile(
        snapshot.DownloadUrl, zip, kUserAgent,
        [&state]( uint64_t done, uint64_t total ) {
            state.DownloadDone = done;
            state.DownloadTotal = total;
            return !state.bCancel.load();
        },
        error );
    if ( !bDownloaded ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = error;
        state.Phase = "Download failed.";
        state.bBusy = false;
        return false;
    }

    const std::wstring staged = UpdateStagingDir();
    std::error_code ec;
    std::filesystem::remove_all( staged, ec );
    MakeDirs( staged );
    const std::wstring tar = FindTarExecutable();
    if ( tar.empty() ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "tar.exe was not found - Windows 10 1803 or newer is required.";
        state.bBusy = false;
        return false;
    }
    state.SetPhase( "Unpacking the release..." );
    const std::wstring extractCommand =
        L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + staged + L"\"";
    const ProcessResult extract = RunProcess( extractCommand, root, 300000 );
    if ( extract.bTimedOut || extract.ExitCode != 0 ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = extract.bTimedOut ? "Unpacking timed out." : Trim( extract.Out );
        state.Phase = "Unpacking failed.";
        state.bBusy = false;
        return false;
    }
    FlattenSingleDirectory( staged );

    {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.bStaged = true;
        state.bBusy = false;
        state.Phase = "Release staged.";
        state.Summary = "Release " + snapshot.LatestTag +
                        " downloaded and unpacked. It is applied when the launcher closes.";
    }
    return true;
}

bool StartStagedUpdate( const std::wstring &launcherDir, UpdateState &state ) {
    const std::wstring staged = UpdateStagingDir();
    if ( !DirectoryExists( staged ) ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "No staged update to apply.";
        return false;
    }
    wchar_t tempPath[MAX_PATH * 2] = L"";
    const DWORD tempLength = GetTempPathW( MAX_PATH * 2, tempPath );
    const std::wstring root =
        JoinPath( std::wstring( tempPath, tempLength ), L"opentdu2-update" );
    MakeDirs( root );
    const std::wstring script = JoinPath( root, L"apply-update.cmd" );

    char pidText[32];
    std::snprintf( pidText, sizeof( pidText ), "%lu", GetCurrentProcessId() );

    const std::wstring launcherExe = JoinPath( launcherDir, L"otdu2_launcher.exe" );
    const std::string exeUtf8 = WideToUtf8( launcherExe );
    const std::string stagedUtf8 = WideToUtf8( staged );
    const std::string dirUtf8 = WideToUtf8( launcherDir );

    std::string text;
    text += "@echo off\r\n";
    text += "setlocal\r\n";
    text += "set PID=" + std::string( pidText ) + "\r\n";
    text += "rem Wait for the launcher (PID %PID%) to close so its exe can be replaced.\r\n";
    text += ":wait\r\n";
    text += "tasklist /FI \"PID eq %PID%\" 2>nul | find \"%PID%\" >nul\r\n";
    text += "if not errorlevel 1 (\r\n";
    text += "    ping -n 2 127.0.0.1 >nul\r\n";
    text += "    goto wait\r\n";
    text += ")\r\n";
    text += "if exist \"" + exeUtf8 + "\" copy /Y \"" + exeUtf8 + "\" \"" + exeUtf8 +
            ".bak\" >nul 2>&1\r\n";
    text += "xcopy /E /I /Y \"" + stagedUtf8 + "\\*\" \"" + dirUtf8 + "\\\" >nul\r\n";
    text += "start \"\" \"" + exeUtf8 + "\"\r\n";
    text += "del \"%~f0\"\r\n";
    if ( !WriteTextFile( script, text ) ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "Cannot write the update script.";
        return false;
    }

    std::wstring commandLine = L"cmd.exe /c \"" + script + L"\"";
    STARTUPINFOW startup = {};
    startup.cb = sizeof( startup );
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};
    if ( !CreateProcessW( nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                          DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, root.c_str(), &startup,
                          &process ) ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "Cannot start the update script (" +
                      std::to_string( GetLastError() ) + ").";
        return false;
    }
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    return true;
}

// ---------------------------------------------------------------------------

RebuildSnapshot Snapshot( RebuildState &state ) {
    RebuildSnapshot snapshot;
    snapshot.bRunning = state.bRunning.load();
    std::lock_guard<std::mutex> lock( state.Mutex );
    snapshot.Phase = state.Phase;
    snapshot.Log = state.Log;
    snapshot.bDone = state.bDone;
    snapshot.bSucceeded = state.bSucceeded;
    snapshot.ExitCode = state.ExitCode;
    return snapshot;
}

bool RunUpdateAndRebuild( const std::wstring &repoRoot, RebuildState &state ) {
    if ( repoRoot.empty() ) {
        state.SetPhase( "Not a git checkout - there is nothing to rebuild." );
        state.bDone = true;
        return false;
    }
    const std::wstring bat = JoinPath( repoRoot, L"update-and-rebuild.bat" );
    if ( !FileExists( bat ) ) {
        state.SetPhase( "update-and-rebuild.bat is not in " + WideToUtf8( repoRoot ) + "." );
        state.bDone = true;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Log.clear();
        state.ExitCode = 0;
        state.bDone = false;
        state.bSucceeded = false;
        state.Phase = "Running update-and-rebuild.bat...";
    }
    state.bRunning = true;

    // The bat's own steps echo everything they do (the fetch, the three
    // merges, the build); cmd just has to run it where the repo is - the bat
    // cd's to its own folder anyway. Its process tree sits in a kill-on-close
    // job: closing the handle (the launcher shutting down) takes it down.
    HANDLE job = CreateKillOnCloseJob();
    {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.hJob = job;
    }
    const ProcessOutputSink sink = [&state]( const char *data, size_t size ) {
        state.AppendLog( std::string( data, size ) );
    };
    const ProcessResult result =
        RunProcess( L"cmd.exe /c \"\"" + bat + L"\"\"", repoRoot, 0, sink, job, &state.ChildPid );

    {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.hJob = nullptr;
        state.ChildPid = 0;
        state.ExitCode = static_cast<int>( result.ExitCode );
        if ( !result.bStarted ) {
            state.Phase = "Could not start the script: " + result.Error;
            state.AppendLog( result.Error + "\r\n" );
        } else if ( result.ExitCode == 0 ) {
            state.Phase = "Rebuilt OK - the new exe is in build-main\\bin.";
        } else {
            state.Phase = "Failed (exit " + std::to_string( static_cast<int>( result.ExitCode ) ) +
                          ") - read the log. On a merge conflict: resolve it, commit, "
                          "press the button again.";
        }
        state.bSucceeded = result.bStarted && result.ExitCode == 0;
        state.bDone = true;
    }
    if ( job != nullptr ) {
        CloseHandle( job );
    }
    state.bRunning = false;
    return result.bStarted;
}

} // namespace Launcher


