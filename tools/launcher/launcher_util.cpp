#include "launcher_util.h"

#include <shobjidl.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace Launcher {

std::wstring Utf8ToWide( const std::string &text ) {
    if ( text.empty() ) {
        return std::wstring();
    }
    const int count = MultiByteToWideChar( CP_UTF8, 0, text.data(),
                                           static_cast<int>( text.size() ), nullptr, 0 );
    std::wstring wide( static_cast<size_t>( count ), L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, text.data(), static_cast<int>( text.size() ),
                         wide.data(), count );
    return wide;
}

std::string WideToUtf8( const std::wstring &text ) {
    if ( text.empty() ) {
        return std::string();
    }
    const int count = WideCharToMultiByte( CP_UTF8, 0, text.data(),
                                           static_cast<int>( text.size() ), nullptr, 0,
                                           nullptr, nullptr );
    std::string utf8( static_cast<size_t>( count ), '\0' );
    WideCharToMultiByte( CP_UTF8, 0, text.data(), static_cast<int>( text.size() ),
                         utf8.data(), count, nullptr, nullptr );
    return utf8;
}

std::string ToLowerAscii( std::string text ) {
    for ( char &c : text ) {
        if ( c >= 'A' && c <= 'Z' ) {
            c = static_cast<char>( c - 'A' + 'a' );
        }
    }
    return text;
}

std::wstring ExePath() {
    wchar_t buffer[MAX_PATH * 2] = L"";
    const DWORD len = GetModuleFileNameW( nullptr, buffer, MAX_PATH * 2 );
    return std::wstring( buffer, len );
}

std::wstring ExeDir() {
    std::wstring path = ExePath();
    const size_t slash = path.find_last_of( L"\\/" );
    return slash == std::wstring::npos ? std::wstring() : path.substr( 0, slash );
}

std::wstring IniPath() { return ExeDir() + L"\\launcher.ini"; }

std::wstring DefaultUserDataDir() {
    wchar_t appData[MAX_PATH * 2] = L"";
    const DWORD len = GetEnvironmentVariableW( L"APPDATA", appData, MAX_PATH * 2 );
    if ( len == 0 ) {
        return L"";
    }
    return std::wstring( appData, len ) + L"\\OpenTDU2\\OpenTDU2";
}

bool FileExists( const std::wstring &path ) {
    if ( path.empty() ) {
        return false;
    }
    const DWORD attrs = GetFileAttributesW( path.c_str() );
    return attrs != INVALID_FILE_ATTRIBUTES && ( attrs & FILE_ATTRIBUTE_DIRECTORY ) == 0;
}

bool DirectoryExists( const std::wstring &path ) {
    if ( path.empty() ) {
        return false;
    }
    const DWORD attrs = GetFileAttributesW( path.c_str() );
    return attrs != INVALID_FILE_ATTRIBUTES && ( attrs & FILE_ATTRIBUTE_DIRECTORY ) != 0;
}

uint64_t FileSizeOf( const std::wstring &path ) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if ( !GetFileAttributesExW( path.c_str(), GetFileExInfoStandard, &data ) ) {
        return 0;
    }
    return ( static_cast<uint64_t>( data.nFileSizeHigh ) << 32 ) | data.nFileSizeLow;
}

bool MakeDirs( const std::wstring &path ) {
    std::error_code ec;
    std::filesystem::create_directories( std::filesystem::path( path ), ec );
    return DirectoryExists( path );
}

std::wstring JoinPath( const std::wstring &a, const std::wstring &b ) {
    if ( a.empty() ) {
        return b;
    }
    if ( b.empty() ) {
        return a;
    }
    std::wstring result = a;
    if ( result.back() != L'\\' && result.back() != L'/' ) {
        result += L'\\';
    }
    return result + b;
}

std::wstring BaseName( const std::wstring &path ) {
    const size_t slash = path.find_last_of( L"\\/" );
    return slash == std::wstring::npos ? path : path.substr( slash + 1 );
}

std::wstring ParentDir( const std::wstring &path ) {
    const size_t slash = path.find_last_of( L"\\/" );
    return slash == std::wstring::npos ? std::wstring() : path.substr( 0, slash );
}

std::string FormatBytes( uint64_t bytes ) {
    static const char *kUnits[] = { "B", "KB", "MB", "GB", "TB" };
    double value = static_cast<double>( bytes );
    int unit = 0;
    while ( value >= 1024.0 && unit < 4 ) {
        value /= 1024.0;
        unit++;
    }
    char buffer[64];
    std::snprintf( buffer, sizeof( buffer ), unit == 0 ? "%.0f %s" : "%.1f %s", value,
                   kUnits[unit] );
    return buffer;
}


bool ReadTextFile( const std::wstring &path, std::string &outText ) {
    FILE *file = _wfopen( path.c_str(), L"rb" );
    if ( file == nullptr ) {
        return false;
    }
    outText.clear();
    char buffer[8192];
    size_t read = 0;
    while ( ( read = fread( buffer, 1, sizeof( buffer ), file ) ) > 0 ) {
        outText.append( buffer, read );
    }
    fclose( file );
    return true;
}

bool WriteTextFile( const std::wstring &path, const std::string &text ) {
    FILE *file = _wfopen( path.c_str(), L"wb" );
    if ( file == nullptr ) {
        return false;
    }
    const size_t written = fwrite( text.data(), 1, text.size(), file );
    fclose( file );
    return written == text.size();
}

bool BrowseFolder( HWND owner, const wchar_t *title, std::wstring &outPath ) {
    IFileOpenDialog *dialog = nullptr;
    HRESULT hr = CoCreateInstance( CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS( &dialog ) );
    if ( FAILED( hr ) || dialog == nullptr ) {
        return false;
    }
    DWORD options = 0;
    dialog->GetOptions( &options );
    dialog->SetOptions( options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST );
    if ( title != nullptr ) {
        dialog->SetTitle( title );
    }
    bool bPicked = false;
    if ( SUCCEEDED( dialog->Show( owner ) ) ) {
        IShellItem *item = nullptr;
        if ( SUCCEEDED( dialog->GetResult( &item ) ) && item != nullptr ) {
            PWSTR path = nullptr;
            if ( SUCCEEDED( item->GetDisplayName( SIGDN_FILESYSPATH, &path ) ) && path != nullptr ) {
                outPath = path;
                CoTaskMemFree( path );
                bPicked = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return bPicked;
}

bool BrowseFile( HWND owner, const wchar_t *title, const wchar_t *filter,
                 std::wstring &outPath ) {
    IFileOpenDialog *dialog = nullptr;
    HRESULT hr = CoCreateInstance( CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS( &dialog ) );
    if ( FAILED( hr ) || dialog == nullptr ) {
        return false;
    }
    DWORD options = 0;
    dialog->GetOptions( &options );
    dialog->SetOptions( options | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST );
    if ( title != nullptr ) {
        dialog->SetTitle( title );
    }
    if ( filter != nullptr ) {
        // filter is a double-null terminated COMDLG_FILTERSPEC list: label\0pattern\0\0
        std::vector<std::wstring> parts;
        const wchar_t *cursor = filter;
        while ( *cursor != L'\0' ) {
            parts.emplace_back( cursor );
            cursor += parts.back().size() + 1;
        }
        std::vector<COMDLG_FILTERSPEC> specs;
        for ( size_t i = 0; i + 1 < parts.size(); i += 2 ) {
            COMDLG_FILTERSPEC spec = {};
            spec.pszName = parts[i].c_str();
            spec.pszSpec = parts[i + 1].c_str();
            specs.push_back( spec );
        }
        if ( !specs.empty() ) {
            dialog->SetFileTypes( static_cast<UINT>( specs.size() ), specs.data() );
            dialog->SetFileTypeIndex( 1 );
        }
    }
    bool bPicked = false;
    if ( SUCCEEDED( dialog->Show( owner ) ) ) {
        IShellItem *item = nullptr;
        if ( SUCCEEDED( dialog->GetResult( &item ) ) && item != nullptr ) {
            PWSTR path = nullptr;
            if ( SUCCEEDED( item->GetDisplayName( SIGDN_FILESYSPATH, &path ) ) && path != nullptr ) {
                outPath = path;
                CoTaskMemFree( path );
                bPicked = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return bPicked;
}

bool OpenInExplorer( const std::wstring &path ) {
    if ( path.empty() ) {
        return false;
    }
    const HINSTANCE result = ShellExecuteW( nullptr, L"open", path.c_str(), nullptr,
                                            nullptr, SW_SHOWNORMAL );
    return reinterpret_cast<INT_PTR>( result ) > 32;
}

bool RevealInExplorer( const std::wstring &path ) {
    if ( path.empty() ) {
        return false;
    }
    const std::wstring args = L"/select,\"" + path + L"\"";
    const HINSTANCE result = ShellExecuteW( nullptr, L"open", L"explorer.exe",
                                            args.c_str(), nullptr, SW_SHOWNORMAL );
    return reinterpret_cast<INT_PTR>( result ) > 32;
}

bool OpenUrl( const std::wstring &url ) {
    if ( url.empty() ) {
        return false;
    }
    const HINSTANCE result = ShellExecuteW( nullptr, L"open", url.c_str(), nullptr, nullptr,
                                            SW_SHOWNORMAL );
    return reinterpret_cast<INT_PTR>( result ) > 32;
}

std::wstring QuoteArg( const std::wstring &arg ) {
    if ( !arg.empty() && arg.find_first_of( L" \t\n\v\"" ) == std::wstring::npos ) {
        return arg;
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for ( const wchar_t c : arg ) {
        if ( c == L'\\' ) {
            backslashes++;
            continue;
        }
        if ( c == L'"' ) {
            quoted.append( backslashes * 2 + 1, L'\\' );
            quoted += L'"';
        } else {
            quoted.append( backslashes, L'\\' );
            quoted += c;
        }
        backslashes = 0;
    }
    quoted.append( backslashes * 2, L'\\' );
    quoted += L'"';
    return quoted;
}

std::wstring BuildGameArguments( const GameLaunchOptions &options ) {
    std::wstring arguments;
    const auto append = [&arguments]( const wchar_t *flag, const std::string &value ) {
        if ( value.empty() ) {
            return;
        }
        arguments += L" ";
        arguments += flag;
        arguments += L" ";
        arguments += QuoteArg( Utf8ToWide( value ) );
    };
    append( L"-game", options.GameFolder );
    append( L"-playername", options.PlayerName );
    if ( options.bWatermark && !options.PlayerName.empty() ) {
        append( L"-tester", options.PlayerName );
    }
    append( L"-userdata", options.UserDataFolder );
    if ( options.bSkipIntro ) {
        arguments += L" -skipintro";
    }
    append( L"-onlineusername", options.OnlineUsername );
    append( L"-onlineserver", options.OnlineServer );
    return arguments;
}

HANDLE CreateKillOnCloseJob() {
    HANDLE job = CreateJobObjectW( nullptr, nullptr );
    if ( job == nullptr ) {
        return nullptr;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if ( !SetInformationJobObject( job, JobObjectExtendedLimitInformation, &limits,
                                   sizeof( limits ) ) ) {
        CloseHandle( job );
        return nullptr;
    }
    return job;
}

void KillProcessTree( DWORD pid ) {
    if ( pid == 0 ) {
        return;
    }
    // The descendants first: the snapshot's parent links name the tree (a
    // died-along-the-way parent's children are re-attached to the root walk by
    // looping until nothing matches any more).
    for ( ;; ) {
        HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
        if ( snapshot == INVALID_HANDLE_VALUE ) {
            break;
        }
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof( entry );
        DWORD victim = 0;
        if ( Process32FirstW( snapshot, &entry ) ) {
            do {
                if ( entry.th32ParentProcessID == pid && entry.th32ProcessID != pid ) {
                    victim = entry.th32ProcessID;
                    break;
                }
            } while ( Process32NextW( snapshot, &entry ) );
        }
        CloseHandle( snapshot );
        if ( victim == 0 ) {
            break;
        }
        KillProcessTree( victim );
    }
    if ( HANDLE process = OpenProcess( PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid ) ) {
        TerminateProcess( process, 1 );
        WaitForSingleObject( process, 3000 );
        CloseHandle( process );
    }
}

ProcessResult RunProcess( const std::wstring &commandLine, const std::wstring &workingDir,
                          uint32_t timeoutMs, const ProcessOutputSink &sink, HANDLE hJob,
                          std::atomic<uint32_t> *outPid ) {
    ProcessResult result;

    SECURITY_ATTRIBUTES attributes = {};
    attributes.nLength = sizeof( attributes );
    attributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if ( !CreatePipe( &readPipe, &writePipe, &attributes, 0 ) ) {
        result.Error = "CreatePipe failed";
        return result;
    }
    SetHandleInformation( readPipe, HANDLE_FLAG_INHERIT, 0 );

    // The child's stdin is the NUL device: a .bat's final "pause" (the rebuild
    // script ends in one) reads an instant EOF instead of waiting forever, and
    // no child of this launcher is ever meant to prompt.
    HANDLE nullInput = CreateFileW( L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &attributes, OPEN_EXISTING, 0, nullptr );

    STARTUPINFOW startup = {};
    startup.cb = sizeof( startup );
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nullInput;

    PROCESS_INFORMATION process = {};
    std::wstring mutableCommand = commandLine;
    const BOOL started = CreateProcessW(
        nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        workingDir.empty() ? nullptr : workingDir.c_str(), &startup, &process );
    if ( nullInput != nullptr ) {
        CloseHandle( nullInput );
    }
    CloseHandle( writePipe );
    if ( !started ) {
        CloseHandle( readPipe );
        result.Error = "CreateProcess failed (" + std::to_string( GetLastError() ) + ")";
        return result;
    }
    if ( hJob != nullptr ) {
        AssignProcessToJobObject( hJob, process.hProcess );
    }
    if ( outPid != nullptr ) {
        outPid->store( process.dwProcessId );
    }
    result.bStarted = true;

    const ULONGLONG start = GetTickCount64();
    char buffer[4096];
    for ( ;; ) {
        DWORD available = 0;
        if ( !PeekNamedPipe( readPipe, nullptr, 0, nullptr, &available, nullptr ) ) {
            break; // the child closed its end
        }
        if ( available > 0 ) {
            DWORD read = 0;
            if ( !ReadFile( readPipe, buffer, sizeof( buffer ), &read, nullptr ) || read == 0 ) {
                break;
            }
            result.Out.append( buffer, read );
            if ( sink ) {
                sink( buffer, read );
            }
            continue;
        }
        if ( WaitForSingleObject( process.hProcess, 0 ) == WAIT_OBJECT_0 ) {
            DWORD read = 0;
            while ( ReadFile( readPipe, buffer, sizeof( buffer ), &read, nullptr ) && read > 0 ) {
                result.Out.append( buffer, read );
                if ( sink ) {
                    sink( buffer, read );
                }
            }
            break;
        }
        if ( timeoutMs != 0 && GetTickCount64() - start > timeoutMs ) {
            TerminateProcess( process.hProcess, 1 );
            result.bTimedOut = true;
            break;
        }
        Sleep( 10 );
    }

    WaitForSingleObject( process.hProcess, 5000 );
    DWORD exitCode = 0;
    GetExitCodeProcess( process.hProcess, &exitCode );
    result.ExitCode = exitCode;
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    CloseHandle( readPipe );
    return result;
}

std::wstring FindGitExecutable() {
    wchar_t found[MAX_PATH * 2] = L"";
    const DWORD len = SearchPathW( nullptr, L"git.exe", nullptr, MAX_PATH * 2, found, nullptr );
    if ( len > 0 && FileExists( found ) ) {
        return found;
    }
    const wchar_t *candidates[] = { L"C:\\Program Files\\Git\\cmd\\git.exe",
                                    L"C:\\Program Files (x86)\\Git\\cmd\\git.exe",
                                    L"C:\\Program Files\\Git\\bin\\git.exe" };
    for ( const wchar_t *candidate : candidates ) {
        if ( FileExists( candidate ) ) {
            return candidate;
        }
    }
    return std::wstring();
}

std::wstring FindRepositoryRoot( const std::wstring &startDir ) {
    std::wstring dir = startDir;
    for ( int depth = 0; depth < 8 && !dir.empty(); depth++ ) {
        if ( DirectoryExists( JoinPath( dir, L".git" ) ) || FileExists( JoinPath( dir, L".git" ) ) ) {
            return dir;
        }
        dir = ParentDir( dir );
    }
    return std::wstring();
}


namespace {

bool IsGameExeName( const std::wstring &fileName, std::wstring &outLower ) {
    outLower.clear();
    for ( const wchar_t c : fileName ) {
        outLower += ( c >= L'A' && c <= L'Z' ) ? static_cast<wchar_t>( c - L'A' + L'a' ) : c;
    }
    if ( outLower.size() <= 4 || outLower.compare( outLower.size() - 4, 4, L".exe" ) != 0 ) {
        return false;
    }
    // opentdu2.exe (the hub) or opentdu2-<workspace>.exe (a checkout). The
    // tools (opentdu2_bnk_tool.exe, otdu2_launcher.exe) never match.
    if ( outLower == L"opentdu2.exe" ) {
        return true;
    }
    return outLower.rfind( L"opentdu2-", 0 ) == 0;
}

void ScanDirectoryForGameExe( const std::wstring &dir, std::wstring &bestPath,
                              uint64_t &bestWriteTime ) {
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW( ( dir + L"\\opentdu2*.exe" ).c_str(), &data );
    if ( find == INVALID_HANDLE_VALUE ) {
        return;
    }
    do {
        std::wstring lower;
        if ( ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 ) {
            continue;
        }
        if ( !IsGameExeName( data.cFileName, lower ) ) {
            continue;
        }
        const uint64_t writeTime =
            ( static_cast<uint64_t>( data.ftLastWriteTime.dwHighDateTime ) << 32 ) |
            data.ftLastWriteTime.dwLowDateTime;
        if ( bestPath.empty() || writeTime > bestWriteTime ) {
            bestPath = JoinPath( dir, data.cFileName );
            bestWriteTime = writeTime;
        }
    } while ( FindNextFileW( find, &data ) );
    FindClose( find );
}

} // namespace

bool FindGameExecutable( const std::wstring &launcherDir, const std::wstring &overridePath,
                         std::wstring &outPath, std::string &outError ) {
    outPath.clear();
    outError.clear();

    if ( !overridePath.empty() ) {
        if ( FileExists( overridePath ) ) {
            outPath = overridePath;
            return true;
        }
        outError = "The game exe set in launcher.ini does not exist: " +
                   WideToUtf8( overridePath );
        return false;
    }

    uint64_t bestTime = 0;
    // A distributed zip: the game exe sits beside the launcher.
    ScanDirectoryForGameExe( launcherDir, outPath, bestTime );
    if ( !outPath.empty() ) {
        return true;
    }

    // A checkout: <repo>\build-*\bin\opentdu2*.exe (launch\resolve_exe.bat's
    // probes), from the launcher's folder and one level up.
    const std::wstring bases[] = { launcherDir, ParentDir( launcherDir ) };
    for ( const std::wstring &base : bases ) {
        if ( base.empty() ) {
            continue;
        }
        WIN32_FIND_DATAW data = {};
        HANDLE find = FindFirstFileW( ( base + L"\\build-*" ).c_str(), &data );
        if ( find == INVALID_HANDLE_VALUE ) {
            continue;
        }
        do {
            if ( ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 ) {
                ScanDirectoryForGameExe( JoinPath( JoinPath( base, data.cFileName ), L"bin" ),
                                         outPath, bestTime );
            }
        } while ( FindNextFileW( find, &data ) );
        FindClose( find );
        if ( !outPath.empty() ) {
            return true;
        }
    }

    outError = "No game exe found (opentdu2.exe beside the launcher, or "
               "build-*\\bin\\opentdu2*.exe). Build the game first.";
    return false;
}

bool IsValidGameFolder( const std::wstring &folder ) {
    return DirectoryExists( JoinPath( JoinPath( folder, L"Euro" ), L"Bnk" ) );
}

std::string ProbeIsoFilesystem( const std::wstring &isoPath ) {
    HANDLE file = CreateFileW( isoPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( file == INVALID_HANDLE_VALUE ) {
        return std::string();
    }
    LARGE_INTEGER offset = {};
    offset.QuadPart = 16 * 2048;
    SetFilePointerEx( file, offset, nullptr, FILE_BEGIN );
    char sector[32] = {};
    DWORD read = 0;
    const bool bRead = ReadFile( file, sector, sizeof( sector ), &read, nullptr ) && read >= 8;
    CloseHandle( file );
    if ( !bRead ) {
        return std::string();
    }
    const bool bIso = memcmp( sector + 1, "CD001", 5 ) == 0;
    const bool bUdf = memcmp( sector + 1, "BEA01", 5 ) == 0;
    if ( bIso && bUdf ) {
        return "ISO 9660 + UDF";
    }
    if ( bIso ) {
        return "ISO 9660";
    }
    if ( bUdf ) {
        return "UDF";
    }
    return std::string();
}

uint32_t Fnv1a32( const void *data, size_t size ) {
    const uint8_t *bytes = static_cast<const uint8_t *>( data );
    uint32_t hash = 2166136261u;
    for ( size_t i = 0; i < size; i++ ) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

} // namespace Launcher

