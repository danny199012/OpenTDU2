#pragma once

// launcher.ini beside the launcher exe: everything the tester entered once.
// Win32 profile APIs (GetPrivateProfileStringW) - plain, hand-editable, and it
// survives an upgrade because the updater never touches it.

#include <string>

namespace Launcher {

struct LauncherSettings {
    // Game data
    std::string GameFolder;      // <game>\Euro\Bnk must exist
    std::string GameExe;         // optional override, empty = auto-detect
    std::string UserDataFolder;  // empty = the game's default (%APPDATA%\OpenTDU2\OpenTDU2)
    // ISO extraction
    std::string IsoPath;
    std::string IsoDestination;
    // Identity
    std::string PlayerName;      // -playername: the avatar + profile.sav's name (main save)
    std::string ActiveProfile;   // profiles\<id> to play; empty = the main save
    // Online (networking not implemented yet)
    std::string OnlineUsername;
    std::string OnlinePassword;
    std::string OnlineServer;
    // Updates
    std::string UpdateRepo;      // "owner/repo" of the GitHub releases
    std::string UpdateBranch;    // the branch a git checkout updates from
    std::string LastUpdateTag;   // the last release the launcher installed
    // Launch options
    bool bWatermark = true;      // pass -tester <name>: the in-game watermark
    bool bSkipIntro = false;     // -skipintro
    bool bAutoCheckUpdates = true;
    bool bCloseOnLaunch = false; // close the launcher when the game starts

    void Load( const std::wstring &iniPath );
    void Save( const std::wstring &iniPath ) const;
};

} // namespace Launcher
