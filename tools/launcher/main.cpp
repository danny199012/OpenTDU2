// otdu2_launcher - the OpenTDU2 launcher (tools/CMakeLists.txt). A Win32
// window hosting ImGui on Direct3D 11, deliberately with no otdu_engine link: a
// tester distribution is this exe + the game exe + the user's own game data,
// and it builds in seconds. It shares the generated build stamp (build_info.h)
// with the game.
//
// What it does (launcher_ui.cpp owns the pages):
//   - Play: the hero artwork, PLAY with -game/-playername/-tester/-userdata,
//     launch options and the watermark switch,
//   - Game data: point at an .iso (ISO 9660 + Joliet / UDF reader in
//     iso_archive.cpp) and extract the game, or pick an unpacked folder,
//   - Updates: the build stamp, the changelog (git commits or GitHub release
//     notes) and a one-click fast-forward / release download,
//   - Player: the name that becomes profile.sav's `name` and the avatar's name,
//   - Online: the account stored for the networking that is not implemented.
//
// Headless modes for verifying the launcher without a screen:
//   --selftest                 UTF-8/INI/JSON checks, writes a report
//   --iso-info <path>          list an image, read one file back
//   --check-update             run the updater check and print the changelog
//   --log <path>               where the report goes (default %TEMP%)

#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "build_info.h"
#include "game_installer.h"
#include "iso_archive.h"
#include "launcher_json.h"
#include "launcher_settings.h"
#include "launcher_ui.h"
#include "launcher_updater.h"
#include "launcher_util.h"

namespace {

ID3D11Device *gDevice = nullptr;
ID3D11DeviceContext *gContext = nullptr;
IDXGISwapChain *gSwapChain = nullptr;
ID3D11RenderTargetView *gRenderTargetView = nullptr;
UINT gResizeWidth = 0;
UINT gResizeHeight = 0;

void CreateRenderTarget() {
    ID3D11Texture2D *backBuffer = nullptr;
    gSwapChain->GetBuffer( 0, IID_PPV_ARGS( &backBuffer ) );
    if ( backBuffer != nullptr ) {
        gDevice->CreateRenderTargetView( backBuffer, nullptr, &gRenderTargetView );
        backBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if ( gRenderTargetView != nullptr ) {
        gRenderTargetView->Release();
        gRenderTargetView = nullptr;
    }
}

bool CreateDeviceD3D( HWND window ) {
    DXGI_SWAP_CHAIN_DESC description = {};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator = 60;
    description.BufferDesc.RefreshRate.Denominator = 1;
    description.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
        &description, &gSwapChain, &gDevice, &level, &gContext );
    if ( result == DXGI_ERROR_UNSUPPORTED ) {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
            &description, &gSwapChain, &gDevice, &level, &gContext );
    }
    if ( result != S_OK ) {
        return false;
    }
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if ( gSwapChain != nullptr ) {
        gSwapChain->Release();
        gSwapChain = nullptr;
    }
    if ( gContext != nullptr ) {
        gContext->Release();
        gContext = nullptr;
    }
    if ( gDevice != nullptr ) {
        gDevice->Release();
        gDevice = nullptr;
    }
}

} // namespace

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND window, UINT message,
                                                              WPARAM wParam, LPARAM lParam );

namespace {

LRESULT WINAPI WndProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam ) {
    if ( ImGui_ImplWin32_WndProcHandler( window, message, wParam, lParam ) ) {
        return 1;
    }
    switch ( message ) {
    case WM_SIZE:
        if ( wParam == SIZE_MINIMIZED ) {
            return 0;
        }
        gResizeWidth = static_cast<UINT>( LOWORD( lParam ) );
        gResizeHeight = static_cast<UINT>( HIWORD( lParam ) );
        return 0;
    case WM_SYSCOMMAND:
        if ( ( wParam & 0xfff0 ) == SC_KEYMENU ) {
            return 0; // no ALT menu
        }
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = reinterpret_cast<MINMAXINFO *>( lParam );
        info->ptMinTrackSize.x = 1000;
        info->ptMinTrackSize.y = 640;
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    default:
        break;
    }
    return DefWindowProcW( window, message, wParam, lParam );
}

} // namespace

namespace {

int RunSelfTest( int argc, wchar_t **argv ) {
    std::wstring isoPath;
    std::wstring logPath;
    bool bCheckUpdate = false;
    bool bLaunchTest = false;
    for ( int index = 1; index < argc; index++ ) {
        if ( wcscmp( argv[index], L"--iso-info" ) == 0 && index + 1 < argc ) {
            isoPath = argv[++index];
        } else if ( wcscmp( argv[index], L"--check-update" ) == 0 ) {
            bCheckUpdate = true;
        } else if ( wcscmp( argv[index], L"--launch-test" ) == 0 ) {
            bLaunchTest = true;
        } else if ( wcscmp( argv[index], L"--log" ) == 0 && index + 1 < argc ) {
            logPath = argv[++index];
        }
    }
    if ( logPath.empty() ) {
        wchar_t tempPath[MAX_PATH * 2] = L"";
        const DWORD length = GetTempPathW( MAX_PATH * 2, tempPath );
        logPath = Launcher::JoinPath( std::wstring( tempPath, length ),
                                      L"otdu2-launcher-selftest.log" );
    }

    int failures = 0;
    std::string report = "OpenTDU2 launcher selftest\n";
    report += std::string( "build: " ) + OTDU_BUILD_STRING + " - " + OTDU_BUILD_TIMESTAMP + "\n";
    report += std::string( "release: " ) +
              ( OTDU_RELEASE_TAG[0] != '\0' ? OTDU_RELEASE_TAG : "(no tag - a local build)" ) +
              "\n";
    report += "exe: " + Launcher::WideToUtf8( Launcher::ExePath() ) + "\n\n";
    const auto check = [&]( const char *name, bool ok ) {
        report += std::string( ok ? "[PASS] " : "[FAIL] " ) + name + "\n";
        if ( !ok ) {
            failures++;
        }
    };

    // 1. UTF-8 <-> UTF-16 (a player name may be non-ASCII).
    const std::string sampleName = "Daniel \xC3\x98 \xE2\x9C\x93";
    check( "utf8 roundtrip",
           Launcher::WideToUtf8( Launcher::Utf8ToWide( sampleName ) ) == sampleName );

    // 2. JSON (the GitHub answers).
    const std::string json =
        "{\"a\":[1,2,\"x\\u00d8\"],\"b\":true,\"c\":null,\"d\":{\"e\":\"f\"}}";
    Launcher::JsonValue value;
    std::string jsonError;
    const bool bParsed = Launcher::JsonValue::Parse( json, value, jsonError );
    check( "json parse", bParsed );
    if ( bParsed ) {
        const Launcher::JsonValue *array = value.Find( "a" );
        const bool bArray = array != nullptr && array->IsArray() && array->Size() == 3 &&
                            array->AsArray()[2].AsString() == "x\xC3\x98";
        check( "json array + \\u escape", bArray );
        const Launcher::JsonValue *object = value.Find( "d" );
        check( "json object", object != nullptr && object->GetString( "e" ) == "f" );
    }

    // 3. launcher.ini round trip (it must survive an update).
    const std::wstring iniPath = logPath + L".ini";
    DeleteFileW( iniPath.c_str() );
    Launcher::LauncherSettings saved;
    saved.GameFolder = "C:\\Games\\TDU2 \xC3\x98";
    saved.IsoPath = "D:\\TDU2.iso";
    saved.IsoDestination = "C:\\Games\\TDU2";
    saved.PlayerName = "Tester \xE2\x9C\x93";
    saved.OnlineUsername = "tester";
    saved.OnlinePassword = "secret";
    saved.OnlineServer = "play.opentdu2.net:4000";
    saved.bWatermark = false;
    saved.bSkipIntro = true;
    saved.bAutoCheckUpdates = false;
    saved.bCloseOnLaunch = true;
    saved.UpdateRepo = "owner/repo";
    saved.UpdateBranch = "main";
    saved.LastUpdateTag = "v0.1";
    saved.Save( iniPath );
    Launcher::LauncherSettings loaded;
    loaded.Load( iniPath );
    check( "ini roundtrip",
           loaded.GameFolder == saved.GameFolder && loaded.IsoPath == saved.IsoPath &&
               loaded.IsoDestination == saved.IsoDestination &&
               loaded.PlayerName == saved.PlayerName &&
               loaded.OnlineUsername == saved.OnlineUsername &&
               loaded.OnlinePassword == saved.OnlinePassword &&
               loaded.OnlineServer == saved.OnlineServer &&
               loaded.bWatermark == saved.bWatermark &&
               loaded.bSkipIntro == saved.bSkipIntro &&
               loaded.bAutoCheckUpdates == saved.bAutoCheckUpdates &&
               loaded.bCloseOnLaunch == saved.bCloseOnLaunch &&
               loaded.UpdateRepo == saved.UpdateRepo &&
               loaded.UpdateBranch == saved.UpdateBranch &&
               loaded.LastUpdateTag == saved.LastUpdateTag );
    DeleteFileW( iniPath.c_str() );

    // 4. The game folder rule (Euro\Bnk).
    check( "game folder rule", !Launcher::IsValidGameFolder( L"C:\\does\\not\\exist" ) );

    // 4b. The PLAY command line (the launcher's arguments, one source).
    Launcher::GameLaunchOptions launchOptions;
    launchOptions.GameFolder = "C:\\TDU2 Game Data\\TestDrive2";
    launchOptions.PlayerName = "Daniel";
    launchOptions.OnlineUsername = "daniel_online";
    launchOptions.OnlineServer = "play.opentdu2.net:4000";
    launchOptions.bWatermark = true;
    const std::string launchArgs =
        Launcher::WideToUtf8( Launcher::BuildGameArguments( launchOptions ) );
    report += "game arguments:" + launchArgs + "\n";
    check( "launch args -game",
           launchArgs.find( "-game \"C:\\TDU2 Game Data\\TestDrive2\"" ) !=
               std::string::npos );
    check( "launch args -playername",
           launchArgs.find( "-playername Daniel" ) != std::string::npos );
    check( "launch args -tester", launchArgs.find( "-tester Daniel" ) != std::string::npos );
    check( "launch args online",
           launchArgs.find( "-onlineusername daniel_online" ) != std::string::npos &&
               launchArgs.find( "-onlineserver play.opentdu2.net:4000" ) !=
                   std::string::npos );
    launchOptions.bWatermark = false;
    check( "launch args without watermark",
           Launcher::WideToUtf8( Launcher::BuildGameArguments( launchOptions ) )
                   .find( "-tester" ) == std::string::npos );


    // 5. An ISO: list it and read a file back.
    if ( !isoPath.empty() ) {
        report += "\nISO: " + Launcher::WideToUtf8( isoPath ) + "\n";
        Launcher::IsoArchive archive;
        std::string error;
        if ( archive.Open( isoPath, error ) ) {
            check( "iso open", true );
            report += "filesystem: " + archive.Volume().Filesystem + "\n";
            report += "label: " + archive.Volume().Label + "\n";
            report += "image: " + Launcher::FormatBytes( archive.Volume().ImageSize ) +
                      ", entries: " + std::to_string( archive.Files().size() ) +
                      ", file bytes: " + Launcher::FormatBytes( archive.TotalBytes() ) + "\n";
            std::string prefix;
            bool bPacked = false;
            const bool bGameRoot = archive.FindGameRoot( prefix, bPacked );
            report += "game root: " +
                      ( bGameRoot ? ( "'" + prefix + "'" )
                                  : ( bPacked ? "packed retail (bigfiles)" : "none" ) ) +
                      "\n";
            uint32_t shown = 0;
            for ( const Launcher::IsoFile &file : archive.Files() ) {
                if ( file.bDirectory ) {
                    continue;
                }
                report += "  " + file.Path + "  " + Launcher::FormatBytes( file.Size ) + "\n";
                if ( ++shown >= 20 ) {
                    break;
                }
            }
            const Launcher::IsoFile *smallest = nullptr;
            for ( const Launcher::IsoFile &file : archive.Files() ) {
                if ( file.bDirectory || file.Size == 0 || file.Size > 4u * 1024u * 1024u ) {
                    continue;
                }
                if ( smallest == nullptr || file.Size < smallest->Size ) {
                    smallest = &file;
                }
            }
            if ( smallest != nullptr ) {
                std::vector<uint8_t> data( static_cast<size_t>( smallest->Size ) );
                const uint32_t read = archive.ReadFile(
                    *smallest, 0, data.data(), static_cast<uint32_t>( data.size() ) );
                check( "iso file read", read == data.size() );
                char line[512];
                std::snprintf( line, sizeof( line ), "read: %s  %u bytes  fnv1a %08x",
                               smallest->Path.c_str(), read,
                               Launcher::Fnv1a32( data.data(), read ) );
                report += std::string( line ) + "\n";
                // The same bytes on disk, for a byte-for-byte comparison with
                // another extractor (7z etc.).
                if ( read > 0 ) {
                    const std::wstring dumped = logPath + L".bin";
                    FILE *out = _wfopen( dumped.c_str(), L"wb" );
                    if ( out != nullptr ) {
                        fwrite( data.data(), 1, read, out );
                        fclose( out );
                        report += "dumped: " + Launcher::WideToUtf8( dumped ) + "\n";
                    }
                }
            }
        } else {
            check( "iso open", false );
            report += "error: " + error + "\n";
        }
    }

    // 6. The updater (git checkout or the GitHub API).
    if ( bCheckUpdate ) {
        Launcher::LauncherSettings settings;
        settings.Load( Launcher::IniPath() );
        Launcher::UpdateState state;
        Launcher::CheckForUpdates( settings.UpdateRepo, settings.UpdateBranch,
                                   settings.LastUpdateTag, Launcher::ExeDir(), state );
        const Launcher::UpdateSnapshot snapshot = Launcher::Snapshot( state );
        report += "\nUPDATE CHECK\n";
        report += "source: " + snapshot.Source + "\n";
        report += "phase: " + snapshot.Phase + "\n";
        report += "summary: " + snapshot.Summary + "\n";
        report += "error: " + snapshot.Error + "\n";
        report += "hint: " + snapshot.Hint + "\n";
        report += std::string( "available: " ) + ( snapshot.bAvailable ? "yes" : "no" ) +
                  ", can apply: " + ( snapshot.bCanApply ? "yes" : "no" ) + "\n";
        for ( size_t index = 0; index < snapshot.Entries.size() && index < 15; index++ ) {
            const Launcher::UpdateEntry &entry = snapshot.Entries[index];
            report += "  " + entry.Id + "  " + entry.When + "  " + entry.Title + "\n";
        }
        if ( !snapshot.WhatsNew.empty() ) {
            report += "what's new since this build:\n" + snapshot.WhatsNew;
        }
        if ( !snapshot.Entries.empty() && !snapshot.Entries.front().Body.empty() ) {
            std::string body = Launcher::StripMarkdownNoise( snapshot.Entries.front().Body );
            if ( body.size() > 400 ) {
                body.resize( 400 );
                body += " (...)";
            }
            report += "latest release notes:\n" + body + "\n";
        }
    }

    // 7. End to end: start the game once with the launcher's own arguments.
    if ( bLaunchTest ) {
        report += "\nLAUNCH TEST\n";
        Launcher::LauncherSettings settings;
        settings.Load( Launcher::IniPath() );
        Launcher::GameLaunchOptions options;
        options.GameFolder = settings.GameFolder;
        options.PlayerName = settings.PlayerName;
        options.UserDataFolder = settings.UserDataFolder;
        options.OnlineUsername = settings.OnlineUsername;
        options.OnlineServer = settings.OnlineServer;
        options.bWatermark = settings.bWatermark;
        options.bSkipIntro = settings.bSkipIntro;
        std::wstring exePath;
        std::string exeError;
        if ( !Launcher::FindGameExecutable( Launcher::ExeDir(),
                                            Launcher::Utf8ToWide( settings.GameExe ), exePath,
                                            exeError ) ) {
            check( "launch test: game exe", false );
            report += exeError + "\n";
        } else {
            const std::wstring gameLog = logPath + L".game.log";
            DeleteFileW( gameLog.c_str() );
            std::wstring command = Launcher::QuoteArg( exePath ) +
                                   Launcher::BuildGameArguments( options ) +
                                   L" -exitframe 150 -nowatchdog -log " +
                                   Launcher::QuoteArg( gameLog );
            report += "command: " + Launcher::WideToUtf8( command ) + "\n";
            STARTUPINFOW startup = {};
            startup.cb = sizeof( startup );
            PROCESS_INFORMATION process = {};
            std::wstring mutableCommand = command;
            if ( !CreateProcessW( exePath.c_str(), mutableCommand.data(), nullptr, nullptr,
                                  FALSE, 0, nullptr, Launcher::ParentDir( exePath ).c_str(),
                                  &startup, &process ) ) {
                check( "launch test: CreateProcess", false );
            } else {
                const DWORD wait = WaitForSingleObject( process.hProcess, 180000 );
                DWORD exitCode = 0;
                GetExitCodeProcess( process.hProcess, &exitCode );
                CloseHandle( process.hThread );
                CloseHandle( process.hProcess );
                check( "launch test: game exited", wait == WAIT_OBJECT_0 );
                report += "game exit code: " + std::to_string( exitCode ) + "\n";
                std::string gameText;
                if ( Launcher::ReadTextFile( gameLog, gameText ) ) {
                    check( "launch test: tester watermark argument reached the game",
                           gameText.find( "Tester: '" + options.PlayerName + "'" ) !=
                               std::string::npos );
                    check( "launch test: online account reached the game",
                           options.OnlineUsername.empty() ||
                               gameText.find( "Online: account '" + options.OnlineUsername +
                                              "'" ) != std::string::npos );
                } else {
                    report += "game log missing: " + Launcher::WideToUtf8( gameLog ) + "\n";
                }
            }
        }
    }

    // 8. The staged-update apply script: its quoting must survive cmd.exe's
    // argv rules (the release it replaces failed exactly there - a "<dir>\"
    // destination that swallowed its own closing quote), and a run must copy,
    // log, back up and relaunch. The fake "launcher" is a copy of xcopy.exe: a
    // quick-exit console exe (the script's final `start` flashes its console
    // for a moment - a test artefact, the real target is the GUI launcher).
    {
        const std::wstring root = logPath + L".apply";
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
        const std::wstring install = Launcher::JoinPath( root, L"install" );
        const std::wstring staged = Launcher::JoinPath( root, L"staged" );
        Launcher::MakeDirs( install );
        Launcher::MakeDirs( staged );
        wchar_t sysDir[MAX_PATH * 2] = L"";
        const UINT sysLength = GetSystemDirectoryW( sysDir, MAX_PATH * 2 );
        const std::wstring fakeExe = Launcher::JoinPath( install, L"otdu2_launcher.exe" );
        if ( sysLength == 0 ||
             !CopyFileW( Launcher::JoinPath( std::wstring( sysDir, sysLength ), L"xcopy.exe" )
                             .c_str(),
                         fakeExe.c_str(), FALSE ) ||
             !CopyFileW( Launcher::JoinPath( std::wstring( sysDir, sysLength ), L"xcopy.exe" )
                             .c_str(),
                         Launcher::JoinPath( staged, L"otdu2_launcher.exe" ).c_str(), FALSE ) ) {
            check( "apply script: fixture", false );
        } else {
            Launcher::WriteTextFile( Launcher::JoinPath( install, L"old_file.txt" ), "old\r\n" );
            Launcher::WriteTextFile( Launcher::JoinPath( staged, L"new_file.txt" ), "new\r\n" );
            const std::wstring applyLog =
                Launcher::JoinPath( root, L"apply.log" );
            const std::string script = Launcher::BuildApplyScript(
                "999999" /* no such pid: the wait loop exits at once */, fakeExe, staged,
                install, applyLog );
            const std::string stagedUtf8 = Launcher::WideToUtf8( staged );
            const std::string installUtf8 = Launcher::WideToUtf8( install );
            const bool bQuoting =
                script.find( "\\\"\r\n" ) == std::string::npos &&
                script.find( "xcopy /E /I /Y \"" + stagedUtf8 + "\\*\" \"" + installUtf8 +
                             "\"" ) != std::string::npos;
            check( "apply script: quoting", bQuoting );
            const std::wstring scriptPath = Launcher::JoinPath( root, L"apply-update.cmd" );
            Launcher::WriteTextFile( scriptPath, script );
            const Launcher::ProcessResult run = Launcher::RunProcess(
                L"cmd.exe /c \"" + scriptPath + L"\"", root, 60000 );
            check( "apply script: run", run.bStarted && run.ExitCode == 0 );
            check( "apply script: staged file copied",
                   Launcher::FileExists( Launcher::JoinPath( install, L"new_file.txt" ) ) );
            check( "apply script: launcher replaced",
                   Launcher::FileSizeOf( fakeExe ) ==
                       Launcher::FileSizeOf(
                           Launcher::JoinPath( staged, L"otdu2_launcher.exe" ) ) );
            check( "apply script: backup made", Launcher::FileExists( fakeExe + L".bak" ) );
            std::string applyLogText;
            check( "apply script: log written",
                   Launcher::ReadTextFile( applyLog, applyLogText ) &&
                       applyLogText.find( "staged tree copied" ) != std::string::npos );
            check( "apply script: script removed", !Launcher::FileExists( scriptPath ) );
        }
        std::filesystem::remove_all( root, ec );
    }

    report += failures == 0 ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n";
    report += "log: " + Launcher::WideToUtf8( logPath ) + "\n";
    Launcher::WriteTextFile( logPath, report );

    if ( AttachConsole( ATTACH_PARENT_PROCESS ) ) {
        HANDLE output = GetStdHandle( STD_OUTPUT_HANDLE );
        DWORD written = 0;
        if ( output != INVALID_HANDLE_VALUE && output != nullptr ) {
            WriteFile( output, report.data(), static_cast<DWORD>( report.size() ), &written,
                       nullptr );
        }
        FreeConsole();
    }
    return failures == 0 ? 0 : 1;
}

} // namespace


int WINAPI WinMain( _In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int show ) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW( GetCommandLineW(), &argc );
    bool bHeadless = false;
    Launcher::Page initialPage = Launcher::Page::Play;
    for ( int index = 1; index < argc; index++ ) {
        if ( wcscmp( argv[index], L"--selftest" ) == 0 ||
             wcscmp( argv[index], L"--iso-info" ) == 0 ||
             wcscmp( argv[index], L"--check-update" ) == 0 ||
             wcscmp( argv[index], L"--launch-test" ) == 0 ) {
            bHeadless = true;
        } else if ( wcscmp( argv[index], L"--page" ) == 0 && index + 1 < argc ) {
            const wchar_t *page = argv[++index];
            if ( wcscmp( page, L"gamedata" ) == 0 ) {
                initialPage = Launcher::Page::GameData;
            } else if ( wcscmp( page, L"updates" ) == 0 ) {
                initialPage = Launcher::Page::Updates;
            } else if ( wcscmp( page, L"player" ) == 0 ) {
                initialPage = Launcher::Page::Player;
            } else if ( wcscmp( page, L"online" ) == 0 ) {
                initialPage = Launcher::Page::Online;
            }
        }
    }
    if ( bHeadless ) {
        const int result = RunSelfTest( argc, argv );
        if ( argv != nullptr ) {
            LocalFree( argv );
        }
        return result;
    }
    if ( argv != nullptr ) {
        LocalFree( argv );
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromPoint( POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY ) );
    if ( dpiScale <= 0.0f ) {
        dpiScale = 1.0f;
    }
    CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );

    WNDCLASSEXW windowClass = { sizeof( windowClass ),
                                CS_CLASSDC,
                                WndProc,
                                0L,
                                0L,
                                instance,
                                LoadIconW( instance, MAKEINTRESOURCEW( 1 ) ),
                                LoadCursorW( nullptr, MAKEINTRESOURCEW( 32512 ) ), // IDC_ARROW
                                nullptr,
                                nullptr,
                                L"OpenTDU2Launcher",
                                LoadIconW( instance, MAKEINTRESOURCEW( 1 ) ) };
    RegisterClassExW( &windowClass );

    RECT rect = { 0, 0, static_cast<LONG>( 1260.0f * dpiScale ),
                  static_cast<LONG>( 820.0f * dpiScale ) };
    AdjustWindowRect( &rect, WS_OVERLAPPEDWINDOW, FALSE );
    // Never open larger than the work area (a 225 % 4K desktop would otherwise
    // put the footer under the taskbar).
    MONITORINFO monitorInfo = { sizeof( monitorInfo ) };
    if ( GetMonitorInfoW( MonitorFromPoint( POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY ),
                          &monitorInfo ) ) {
        const int maxWidth =
            monitorInfo.rcWork.right - monitorInfo.rcWork.left - static_cast<int>( 40.0f * dpiScale );
        const int maxHeight =
            monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - static_cast<int>( 40.0f * dpiScale );
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if ( width > maxWidth || height > maxHeight ) {
            rect.right = rect.left + ( width > maxWidth ? maxWidth : width );
            rect.bottom = rect.top + ( height > maxHeight ? maxHeight : height );
        }
    }
    const std::wstring title =
        std::wstring( L"OpenTDU2 Launcher  -  " ) + Launcher::Utf8ToWide( OTDU_RELEASE_STRING );
    HWND window = CreateWindowW( windowClass.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                                 rect.bottom - rect.top, nullptr, nullptr, instance, nullptr );
    if ( window == nullptr || !CreateDeviceD3D( window ) ) {
        MessageBoxW( nullptr,
                     L"Direct3D 11 could not be initialized - the launcher needs a working "
                     L"GPU driver (WARP is tried as a fallback).",
                     L"OpenTDU2 Launcher", MB_ICONERROR | MB_OK );
        CleanupDeviceD3D();
        if ( window != nullptr ) {
            DestroyWindow( window );
        }
        UnregisterClassW( windowClass.lpszClassName, instance );
        CoUninitialize();
        return 1;
    }
    ShowWindow( window, show );
    UpdateWindow( window );

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; // no imgui.ini beside the exe
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init( window );
    ImGui_ImplDX11_Init( gDevice, gContext );

    Launcher::LauncherApp app;
    app.Initialize( window, gDevice, gContext, dpiScale );
    app.SetPage( initialPage );

    bool done = false;
    while ( !done ) {
        MSG message;
        while ( PeekMessageW( &message, nullptr, 0, 0, PM_REMOVE ) ) {
            TranslateMessage( &message );
            DispatchMessageW( &message );
            if ( message.message == WM_QUIT ) {
                done = true;
            }
        }
        if ( done ) {
            break;
        }
        if ( gResizeWidth != 0 && gResizeHeight != 0 ) {
            CleanupRenderTarget();
            gSwapChain->ResizeBuffers( 0, gResizeWidth, gResizeHeight, DXGI_FORMAT_UNKNOWN, 0 );
            gResizeWidth = 0;
            gResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.Frame();
        ImGui::Render();

        const float clearColor[4] = { 0.05f, 0.06f, 0.08f, 1.0f };
        gContext->OMSetRenderTargets( 1, &gRenderTargetView, nullptr );
        gContext->ClearRenderTargetView( gRenderTargetView, clearColor );
        ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
        gSwapChain->Present( 1, 0 );
        if ( app.WantsExit() ) {
            done = true;
        }
    }

    app.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow( window );
    UnregisterClassW( windowClass.lpszClassName, instance );
    CoUninitialize();
    return 0;
}

