#include "launcher_settings.h"

#include "launcher_util.h"

#include <windows.h>

#include <cstdio>

namespace Launcher {

namespace {

std::string ReadString( const wchar_t *section, const wchar_t *key, const wchar_t *fallback,
                        const std::wstring &iniPath ) {
    wchar_t buffer[2048] = L"";
    GetPrivateProfileStringW( section, key, fallback, buffer, 2048, iniPath.c_str() );
    return WideToUtf8( buffer );
}

int ReadInt( const wchar_t *section, const wchar_t *key, int fallback,
             const std::wstring &iniPath ) {
    return static_cast<int>(
        GetPrivateProfileIntW( section, key, fallback, iniPath.c_str() ) );
}

void WriteString( const wchar_t *section, const wchar_t *key, const std::string &value,
                  const std::wstring &iniPath ) {
    WritePrivateProfileStringW( section, key, Utf8ToWide( value ).c_str(), iniPath.c_str() );
}

void WriteInt( const wchar_t *section, const wchar_t *key, int value,
               const std::wstring &iniPath ) {
    wchar_t buffer[32];
    std::swprintf( buffer, 32, L"%d", value );
    WritePrivateProfileStringW( section, key, buffer, iniPath.c_str() );
}

} // namespace

void LauncherSettings::Load( const std::wstring &iniPath ) {
    GameFolder = ReadString( L"launcher", L"game_folder", L"", iniPath );
    GameExe = ReadString( L"launcher", L"game_exe", L"", iniPath );
    UserDataFolder = ReadString( L"launcher", L"userdata", L"", iniPath );
    IsoPath = ReadString( L"launcher", L"iso", L"", iniPath );
    IsoDestination = ReadString( L"launcher", L"iso_destination", L"", iniPath );
    PlayerName = ReadString( L"launcher", L"player_name", L"", iniPath );
    bWatermark = ReadInt( L"launcher", L"watermark", 1, iniPath ) != 0;
    bSkipIntro = ReadInt( L"launcher", L"skip_intro", 0, iniPath ) != 0;
    bAutoCheckUpdates = ReadInt( L"launcher", L"auto_check_updates", 1, iniPath ) != 0;
    bCloseOnLaunch = ReadInt( L"launcher", L"close_on_launch", 0, iniPath ) != 0;

    OnlineUsername = ReadString( L"online", L"username", L"", iniPath );
    OnlinePassword = ReadString( L"online", L"password", L"", iniPath );
    OnlineServer = ReadString( L"online", L"server", L"", iniPath );

    UpdateRepo = ReadString( L"update", L"repo", L"danny199012/OpenTDU2", iniPath );
    UpdateBranch = ReadString( L"update", L"branch", L"main", iniPath );
    LastUpdateTag = ReadString( L"update", L"last_tag", L"", iniPath );
}

void LauncherSettings::Save( const std::wstring &iniPath ) const {
    // The Win32 profile APIs keep a file's encoding: a UTF-16 BOM makes every
    // later write Unicode, so a player name outside the ANSI code page (and a
    // game folder in a non-Latin path) survives the round trip. The launcher
    // owns every key it writes, so the file is recreated from the BOM.
    DeleteFileW( iniPath.c_str() );
    FILE *file = _wfopen( iniPath.c_str(), L"wb" );
    if ( file != nullptr ) {
        const unsigned char bom[2] = { 0xFF, 0xFE };
        fwrite( bom, 1, 2, file );
        fclose( file );
    }

    WriteString( L"launcher", L"game_folder", GameFolder, iniPath );
    WriteString( L"launcher", L"game_exe", GameExe, iniPath );
    WriteString( L"launcher", L"userdata", UserDataFolder, iniPath );
    WriteString( L"launcher", L"iso", IsoPath, iniPath );
    WriteString( L"launcher", L"iso_destination", IsoDestination, iniPath );
    WriteString( L"launcher", L"player_name", PlayerName, iniPath );
    WriteInt( L"launcher", L"watermark", bWatermark ? 1 : 0, iniPath );
    WriteInt( L"launcher", L"skip_intro", bSkipIntro ? 1 : 0, iniPath );
    WriteInt( L"launcher", L"auto_check_updates", bAutoCheckUpdates ? 1 : 0, iniPath );
    WriteInt( L"launcher", L"close_on_launch", bCloseOnLaunch ? 1 : 0, iniPath );

    WriteString( L"online", L"username", OnlineUsername, iniPath );
    WriteString( L"online", L"password", OnlinePassword, iniPath );
    WriteString( L"online", L"server", OnlineServer, iniPath );

    WriteString( L"update", L"repo", UpdateRepo, iniPath );
    WriteString( L"update", L"branch", UpdateBranch, iniPath );
    WriteString( L"update", L"last_tag", LastUpdateTag, iniPath );
}

} // namespace Launcher
