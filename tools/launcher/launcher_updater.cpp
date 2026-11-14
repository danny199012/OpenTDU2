#include "launcher_updater.h"

#include "launcher_http.h"
#include "launcher_json.h"
#include "launcher_util.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "build_info.h"

namespace Launcher {

namespace {

const char *kUserAgent = "OpenTDU2-Launcher/" OTDU_BUILD_NUMBER;

// "What's new" caps the compare-API commit list (the release page has the rest).
constexpr int kWhatsNewMax = 30;
constexpr size_t kWhatsNewSubjectMax = 140;

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
    snapshot.WhatsNew = state.WhatsNew;
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

std::wstring UpdateRootDir() {
    wchar_t tempPath[MAX_PATH * 2] = L"";
    const DWORD len = GetTempPathW( MAX_PATH * 2, tempPath );
    return JoinPath( std::wstring( tempPath, len ), L"opentdu2-update" );
}

std::wstring UpdateStagingDir() {
    return JoinPath( UpdateRootDir(), L"staged" );
}

// --- GitHub API rails: ETag cache + rate-limit memory ----------------------------------------------------
std::wstring ApiCacheDir() {
    return JoinPath( UpdateRootDir(), L"cache" );
}

std::wstring ApiCacheKey( const std::string &url ) {
    // FNV-1a: stable across runs (std::hash is not guaranteed to be).
    uint64_t hash = 1469598103934665603ull;
    for ( const unsigned char c : url ) {
        hash = ( hash ^ c ) * 1099511628211ull;
    }
    char text[32];
    std::snprintf( text, sizeof( text ), "%016llx", static_cast<unsigned long long>( hash ) );
    return Utf8ToWide( text );
}

long long NowEpoch() {
    return static_cast<long long>( std::time( nullptr ) );
}

std::wstring RateLimitFile() {
    return JoinPath( ApiCacheDir(), L"ratelimit.txt" );
}

void RememberRateLimit( long long resetEpoch ) {
    MakeDirs( ApiCacheDir() );
    WriteTextFile( RateLimitFile(), std::to_string( resetEpoch ) );
}

std::string RateLimitMessage( long long until ) {
    const long long minutes = ( until - NowEpoch() + 59 ) / 60;
    return "GitHub is limiting update checks (60 an hour without signing in). Nothing is wrong with the repository. "
           "The launcher will not ask again until " + FormatClockTime( until ) + " (in about " +
           std::to_string( minutes < 1 ? 1 : minutes ) + " min).";
}

// Every GitHub API call goes through here.
HttpResponse GitHubGet( const std::string &originalUrl ) {
    // OTDU2_GITHUB_API=<base> replaces https://api.github.com (a GitHub Enterprise host, or a local test server).
    std::string url = originalUrl;
    static const std::string kApiBase = "https://api.github.com";
    if ( const char *pBase = std::getenv( "OTDU2_GITHUB_API" ); pBase != nullptr && pBase[0] != '\0' &&
                                                               originalUrl.compare( 0, kApiBase.size(), kApiBase ) == 0 ) {
        url = std::string( pBase ) + originalUrl.substr( kApiBase.size() );
    }
    const long long blockedUntil = UpdateRateLimitedUntil();
    if ( blockedUntil > 0 ) {
        HttpResponse blocked;
        blocked.Status = 403;
        blocked.bRateLimited = true;
        blocked.RateLimitReset = blockedUntil;
        blocked.Error = RateLimitMessage( blockedUntil );
        return blocked; // no network call at all
    }
    MakeDirs( ApiCacheDir() );
    const std::wstring key = ApiCacheKey( url );
    const std::wstring etagFile = JoinPath( ApiCacheDir(), key + L".etag" );
    const std::wstring bodyFile = JoinPath( ApiCacheDir(), key + L".body" );
    std::string etag;
    std::string cached;
    ReadTextFile( etagFile, etag );
    ReadTextFile( bodyFile, cached );
    etag = Trim( etag );
    if ( cached.empty() ) {
        etag.clear(); // an ETag without its body cannot answer a 304
    }
    HttpResponse response = HttpGet( url, kUserAgent, 30000, etag );
    if ( response.Status == 304 && !cached.empty() ) {
        response.bOk = true;
        response.Status = 200;
        response.Body = cached;
        response.bFromCache = true;
        response.Error.clear();
        return response;
    }
    if ( response.bOk && !response.ETag.empty() ) {
        WriteTextFile( etagFile, response.ETag );
        WriteTextFile( bodyFile, response.Body );
    }
    const bool bLimited =
        response.Status == 429 ||
        ( response.Status == 403 && ( response.RateLimitRemaining == 0 || response.Body.find( "rate limit" ) != std::string::npos ||
                                      response.Body.find( "Rate limit" ) != std::string::npos ) );
    if ( bLimited ) {
        long long reset = response.RateLimitReset;
        if ( reset <= NowEpoch() ) {
            reset = response.RetryAfterSeconds > 0 ? NowEpoch() + response.RetryAfterSeconds : NowEpoch() + 3600;
        }
        RememberRateLimit( reset );
        response.bRateLimited = true;
        response.RateLimitReset = reset;
        response.Error = RateLimitMessage( reset );
    }
    return response;
}

bool StagedDirHasFiles( const std::wstring &staged ) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for ( const fs::directory_entry &entry : fs::directory_iterator( staged, ec ) ) {
        if ( entry.is_regular_file( ec ) ) {
            return true;
        }
    }
    return false;
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
        state.Hint = "No new commits. The running build is " OTDU_RELEASE_STRING ".";
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
    const HttpResponse response = GitHubGet( releaseUrl );

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
                entry.Commit = release.GetString( "target_commitish" );
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
        // The honest comparison: this binary's commit hash against the newest
        // release's target_commitish, resolved through the compare API (it
        // answers "is the release ahead of this build?" instead of guessing
        // from tag strings). last_tag - which the UI persists when a release
        // is staged - is only the fallback when no hash is known. Without one
        // of these a fresh install is offered the release it already runs.
        const std::string &latestCommit = entries.front().Commit;
        const std::string buildHash = OTDU_BUILD_HASH;
        const bool bHashKnown = !latestCommit.empty() && buildHash != "unknown";
        const bool bSelf = bHashKnown && ToLowerAscii( latestCommit ).find(
                                              ToLowerAscii( buildHash ) ) == 0;
        std::string whatsNew;
        bool bCompareResolved = false;
        bool bAhead = false; // the release carries commits this build lacks
        if ( bHashKnown && !bSelf ) {
            state.SetPhase( "Reading the changelog since this build..." );
            const std::string compareUrl = "https://api.github.com/repos/" + repoSlug +
                                           "/compare/" + buildHash + "..." + latestCommit;
            const HttpResponse compare = GitHubGet( compareUrl );
            JsonValue compareRoot;
            std::string compareError;
            if ( compare.bOk && JsonValue::Parse( compare.Body, compareRoot, compareError ) &&
                 compareRoot.IsObject() ) {
                bCompareResolved = true;
                const JsonValue *ahead = compareRoot.Find( "ahead_by" );
                bAhead = ahead != nullptr && ahead->GetType() == JsonValue::Type::Number &&
                         ahead->AsNumber() > 0.0;
                const JsonValue *commits = compareRoot.Find( "commits" );
                if ( commits != nullptr && commits->IsArray() ) {
                    int count = 0;
                    for ( const JsonValue &node : commits->AsArray() ) {
                        if ( count >= kWhatsNewMax ) {
                            whatsNew += "... and the rest on the release page.\n";
                            break;
                        }
                        const JsonValue *commitNode = node.Find( "commit" );
                        if ( commitNode == nullptr || !commitNode->IsObject() ) {
                            continue;
                        }
                        const std::string subject =
                            FirstLine( commitNode->GetString( "message" ) );
                        if ( subject.empty() ) {
                            continue;
                        }
                        // This repo's commit subjects run long; the card is a
                        // digest, the release page has the full text.
                        if ( subject.size() > kWhatsNewSubjectMax ) {
                            whatsNew += "\xC2\xB7 " +
                                        subject.substr( 0, kWhatsNewSubjectMax ) + "...\n";
                        } else {
                            whatsNew += "\xC2\xB7 " + subject + "\n";
                        }
                        count++;
                    }
                }
            }
        }
        const bool bAvailable =
            bSelf ? false
                  : ( bCompareResolved ? bAhead
                                       : ( lastTag.empty() ? true : latestTag != lastTag ) );

        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Source = "GitHub releases (" + repoSlug + ")";
        state.Entries = entries;
        state.LatestTag = latestTag;
        state.LatestName = latestName;
        state.ReleasePage = releasePage;
        state.DownloadUrl = downloadUrl;
        state.WhatsNew = whatsNew;
        state.bAvailable = bAvailable;
        state.bCanApply = !downloadUrl.empty();
        state.Summary = bAvailable ? "Latest release: " +
                                         ( latestTag.empty() ? latestName : latestTag ) +
                                         ( entries.front().When.empty() ? ""
                                                                        : " (" + entries.front().When + ")" )
                                   : "This launcher is the newest release: " +
                                         ( latestTag.empty() ? latestName : latestTag );
        state.Hint = !bAvailable
                         ? "New builds of main publish automatically - check again later."
                         : downloadUrl.empty()
                               ? "This release has no .zip asset - open the release page."
                               : "The launcher downloads the package, then applies it on exit.";
        return;
    }


    // No releases: the branch's commits are the changelog.
    state.SetPhase( "Reading the branch changelog..." );
    const std::string commitsUrl =
        "https://api.github.com/repos/" + repoSlug + "/commits?sha=" + branch + "&per_page=30";
    const HttpResponse commits = GitHubGet( commitsUrl );
    if ( !commits.bOk ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Source = "GitHub (" + repoSlug + ")";
        state.Error = response.bOk ? commits.Error : response.Error;
        if ( response.bRateLimited || commits.bRateLimited ) {
            // Not an error of the repo or the settings: say what it is and when it lifts.
            const long long until = UpdateRateLimitedUntil();
            state.Error.clear();
            state.Summary = "GitHub is limiting update checks until " + FormatClockTime( until ) + ".";
            state.Hint = "GitHub allows 60 update checks an hour per connection without signing in; this launcher has used them. "
                         "Nothing is wrong with the repository or your install. The launcher makes no further calls until the "
                         "limit lifts, and repeat checks afterwards are free while nothing has changed.";
        } else {
            state.Summary = "Could not read the update feed.";
            state.Hint = "Check the update source on the Updates page (launcher.ini: update.repo).";
        }
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

long long UpdateRateLimitedUntil() {
    std::string text;
    if ( !ReadTextFile( RateLimitFile(), text ) ) {
        return 0;
    }
    const long long until = std::atoll( Trim( text ).c_str() );
    return until > NowEpoch() ? until : 0;
}

std::string FormatClockTime( long long epochSeconds ) {
    const std::time_t value = static_cast<std::time_t>( epochSeconds );
    std::tm local = {};
    if ( localtime_s( &local, &value ) != 0 ) {
        return "later";
    }
    char text[16];
    std::strftime( text, sizeof( text ), "%H:%M", &local );
    return text;
}

void CheckForUpdates( const std::string &repoSlug, const std::string &branch,
                      const std::string &lastTag, const std::wstring &launcherDir,
                      UpdateState &state ) {
    state.bBusy = true;
    state.bCancel = false;
    state.DownloadDone = 0;
    state.DownloadTotal = 0;
    {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.CurrentBuild = OTDU_RELEASE_STRING;
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
    const std::wstring root = UpdateRootDir();
    MakeDirs( root );
    // The staged release's tag, for the "Restart to apply" a later session
    // re-detects (outside staged\ so the apply never copies it into the game).
    WriteTextFile( JoinPath( root, L"tag.txt" ), snapshot.LatestTag );
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
                        " downloaded and unpacked. It is applied when the launcher closes - "
                        "if the relaunch does not appear, " +
                        WideToUtf8( UpdateApplyLogPath() ) + " tells why.";
    }
    return true;
}

bool StartStagedUpdate( const std::wstring &launcherDir, UpdateState &state ) {
    const std::wstring root = UpdateRootDir();
    const std::wstring staged = UpdateStagingDir();
    if ( !StagedDirHasFiles( staged ) ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "No staged update to apply - download the release again.";
        return false;
    }
    MakeDirs( root );
    const std::wstring script = JoinPath( root, L"apply-update.cmd" );
    const std::wstring logPath = UpdateApplyLogPath();

    char pidText[32];
    std::snprintf( pidText, sizeof( pidText ), "%lu", GetCurrentProcessId() );

    // The real exe path, not a name: a renamed launcher must still come back.
    const std::wstring launcherExe = ExePath();

    DeleteFileW( logPath.c_str() ); // a fresh log per attempt
    const std::string text =
        BuildApplyScript( pidText, launcherExe, staged, launcherDir, logPath );
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
    // CREATE_NO_WINDOW alone: MSDN - it is ignored next to DETACHED_PROCESS,
    // and the mix produced the console flash a tester saw on "Restart to
    // apply". The log carries whatever the script would have printed.
    if ( !CreateProcessW( nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process ) ) {
        std::lock_guard<std::mutex> lock( state.Mutex );
        state.Error = "Cannot start the update script (" +
                      std::to_string( GetLastError() ) + ").";
        return false;
    }
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    return true;
}

std::string BuildApplyScript( const std::string &pid, const std::wstring &launcherExe,
                              const std::wstring &staged, const std::wstring &installDir,
                              const std::wstring &logPath ) {
    const std::string exeUtf8 = WideToUtf8( launcherExe );
    const std::string stagedUtf8 = WideToUtf8( staged );
    const std::string dirUtf8 = WideToUtf8( installDir );
    const std::string logUtf8 = WideToUtf8( logPath );

    std::string text;
    text += "@echo off\r\n";
    text += "setlocal\r\n";
    text += "set PID=" + pid + "\r\n";
    text += "set LOG=" + logUtf8 + "\r\n";
    text += "set OK=0\r\n";
    text += "rem Wait for the launcher (PID %PID%) to close so its exe can be replaced.\r\n";
    text += "echo [%date% %time%] waiting for the launcher to close >> \"%LOG%\"\r\n";
    text += ":wait\r\n";
    // findstr, not find: find resolves over PATH and a Git-for-Windows PATH
    // serves GNU find, which wants a filename - the wait would break exactly
    // while the launcher is still alive.
    text += "tasklist /FI \"PID eq %PID%\" 2>nul | findstr /C:\"%PID%\" >nul\r\n";
    text += "if not errorlevel 1 (\r\n";
    text += "    ping -n 2 127.0.0.1 >nul\r\n";
    text += "    goto wait\r\n";
    text += ")\r\n";
    // The destination carries NO trailing backslash: "<dir>\" escapes the
    // closing quote under the Windows argv rules, xcopy reads a broken path
    // and the update silently never copies (the failure this replaces).
    text += "if exist \"" + exeUtf8 + "\" copy /Y \"" + exeUtf8 + "\" \"" + exeUtf8 +
            ".bak\" >> \"%LOG%\" 2>&1\r\n";
    text += "xcopy /E /I /Y \"" + stagedUtf8 + "\\*\" \"" + dirUtf8 + "\" >> \"%LOG%\" 2>&1\r\n";
    text += "if errorlevel 1 (\r\n";
    text += "    echo [%date% %time%] apply FAILED - the old install is kept and this "
            "script stays for diagnosis >> \"%LOG%\"\r\n";
    text += ") else (\r\n";
    text += "    echo [%date% %time%] staged tree copied >> \"%LOG%\"\r\n";
    text += "    set OK=1\r\n";
    text += ")\r\n";
    // The relaunch happens either way: a failed apply must not leave the user
    // with nothing on screen.
    text += "start \"\" \"" + exeUtf8 + "\"\r\n";
    text += "if errorlevel 1 echo [%date% %time%] relaunch FAILED >> \"%LOG%\"\r\n";
    // Success reaches the self-delete, failure keeps the script for diagnosis
    // and exits non-zero. The delete uses the `(goto) 2>nul &` idiom because
    // it is the only one that exits clean: a plain `del "%~f0"` (the old
    // script's last line) makes cmd read past the deleted file and end with
    // "The batch file cannot be found." and errorlevel 1 - the selftest
    // caught that too.
    text += "if not \"%OK%\"==\"1\" exit /b 1\r\n";
    text += "(goto) 2>nul & del \"%~f0\"\r\n";
    return text;
}

std::wstring UpdateApplyLogPath() {
    return JoinPath( UpdateRootDir(), L"apply.log" );
}

std::string StripMarkdownNoise( const std::string &markdown ) {
    std::string out;
    out.reserve( markdown.size() );
    for ( size_t i = 0; i < markdown.size(); ) {
        // An image ![alt](url) disappears; a link [text](url) keeps its text.
        if ( markdown.compare( i, 2, "![") == 0 ) {
            const size_t close = markdown.find( ')', i );
            if ( close == std::string::npos ) {
                break;
            }
            i = close + 1;
            continue;
        }
        if ( markdown[i] == '[' ) {
            const size_t mid = markdown.find( "](", i );
            const size_t close =
                mid == std::string::npos ? std::string::npos : markdown.find( ')', mid );
            if ( mid != std::string::npos && close != std::string::npos ) {
                out += markdown.substr( i + 1, mid - i - 1 );
                i = close + 1;
                continue;
            }
        }
        if ( markdown.compare( i, 2, "**" ) == 0 || markdown.compare( i, 2, "__" ) == 0 ) {
            i += 2;
            continue;
        }
        if ( markdown[i] == '`' ) {
            i++;
            continue;
        }
        if ( markdown[i] == '#' ) {
            while ( i < markdown.size() && markdown[i] == '#' ) {
                i++;
            }
            continue;
        }
        out += markdown[i];
        i++;
    }
    return Trim( out );
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


