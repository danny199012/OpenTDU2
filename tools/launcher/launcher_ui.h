#pragma once

// The launcher's window content: sidebar (Play / Game Data / Updates / Player /
// Online), the pages, and the workers behind them (extraction, update check,
// the game process). ImGui + D3D11, no engine link.

#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "game_installer.h"
#include "launcher_artwork.h"
#include "launcher_profiles.h"
#include "launcher_mods.h"
#include "launcher_settings.h"
#include "launcher_updater.h"

struct ImFont;
struct ImVec2;

namespace Launcher {

enum class Page { Play, Profiles, Mods, GameData, Updates, Online };

class LauncherApp {
public:
    LauncherApp() = default;
    ~LauncherApp();

    bool Initialize( HWND window, ID3D11Device *device, ID3D11DeviceContext *context,
                     float dpiScale );
    void Shutdown();
    void Frame();
    bool WantsExit() const { return mExitRequested; }

    LauncherSettings &Settings() { return mSettings; }
    void SaveSettings();
    void SetPage( Page page ) { SwitchPage( page ); }

    // Set from main.cpp when the launcher already parsed --check-update.
    void StartAutomaticUpdateCheck();

private:
    enum class Phase { Idle, Downloading, Staging, Applying };

    void ApplyThemeOnce();
    void LoadFonts( float dpiScale );
    void RefreshGameState();
    void RefreshProfileSummary();
    void RefreshProfiles();
    void SwitchPage( Page page );
    const ProfileInfo *ActiveProfile() const;
    void SelectProfile( const std::string &id );
    void DrawProfileStrip( float width );
    bool DrawProfileTile( const ProfileInfo &profile, bool bSelected, const ImVec2 &size );
    bool DrawNewProfileTile( const ImVec2 &size );
    void DrawProfileModals();
    void DrawProfilesPage();
    void DrawPlayHero( float width );

    void DrawSidebar();
    void DrawMainArea();
    void DrawFooter();
    void DrawPlayPage();
    void DrawGameDataPage();
    void DrawUpdatesPage();
    void DrawOnlinePage();
    void DrawModsPage();
    void RefreshMods();

    void LaunchGame();
    void BrowseIso();
    void BrowseDestination();
    void BrowseGameFolder();
    void InspectIsoFile();
    void ExtractIso();
    void StartUpdateCheck();
    void ApplyUpdate();
    void DownloadUpdate();
    void StartUpdateRebuild();

    void SetStatus( const std::string &text );
    void SyncSettingsFromFields();
    void SyncFieldsFromSettings();

    HWND mWindow = nullptr;
    ID3D11Device *mDevice = nullptr;
    ID3D11DeviceContext *mContext = nullptr;
    float mDpiScale = 1.0f;
    Artwork mArtwork;

    ImFont *mFontSmall = nullptr;
    ImFont *mFontBody = nullptr;
    ImFont *mFontBold = nullptr;
    ImFont *mFontTitle = nullptr;
    ImFont *mFontHero = nullptr;
    bool mThemeApplied = false;

    Page mPage = Page::Play;
    LauncherSettings mSettings;
    std::wstring mIniPath;
    std::wstring mLauncherDir;
    bool mIsGitCheckout = false;
    bool mExitRequested = false;
    bool mShuttingDown = false;

    // text fields (fixed buffers: ImGui keeps pointers into them while active)
    char mPlayerName[128] = "";
    char mOnlineUsername[128] = "";
    char mOnlinePassword[128] = "";
    char mOnlineServer[256] = "";
    char mGameFolder[MAX_PATH * 2] = "";
    char mGameExe[MAX_PATH * 2] = "";
    char mUserData[MAX_PATH * 2] = "";
    char mIsoPath[MAX_PATH * 2] = "";
    char mIsoDestination[MAX_PATH * 2] = "";
    char mUpdateRepo[160] = "";
    char mUpdateBranch[96] = "";

    // game / save state
    bool mGameFolderValid = false;
    std::wstring mResolvedGameExe;
    std::string mGameExeError;
    std::wstring mEffectiveUserData;
    std::wstring mProfilePath;
    std::string mProfileName;
    std::string mProfileSummary;
    bool mProfileExists = false;

    // profiles (launcher_profiles.h): one save per player / avatar
    std::vector<ProfileInfo> mProfiles;
    std::vector<BackupInfo> mBackups;
    std::string mBackupsFor;          // profile id the backup list belongs to
    char mNewProfileName[64] = "";
    char mRenameBuffer[64] = "";
    std::string mModalProfile;        // id the open modal acts on
    int mModalRequest = 0;            // 1 rename, 2 delete, 3 restore backup (set -> OpenPopup)
    int mBackupToRestore = -1;
    // mods (launcher_mods.h)
    std::vector<ModInfo> mMods;
    bool mModsLoaded = false;
    double mPageChangeTime = 0.0;     // ImGui time of the last page switch (fade-in)

    // launch
    HANDLE mGameProcess = nullptr;
    bool mLaunching = false;
    uint64_t mLaunchTime = 0;

    // ISO inspection + extraction
    IsoInspection mIsoInspection;
    bool mIsoInspected = false;
    InstallProgress mInstall;
    std::thread mInstallThread;
    uint64_t mInstallStart = 0;

    // updater
    UpdateState mUpdate;
    std::thread mUpdateThread;
    bool mCheckingForUpdates = false; // a check is out; announce its verdict once
    // Update-check rails (docs: GitHub allows 60 unauthenticated calls an hour): a short cooldown on the
    // button, and the "blocked until" time of a rate-limit answer (cached, re-read about once a second).
    double mLastUpdateCheckTime = -1000.0;
    long long mRateLimitedUntil = 0;
    double mRateLimitReadTime = -1000.0;
    bool mUpdateWasBusy = false;      // the worker's busy edge, for the finished hooks above
    bool mStagedTagSaved = false;     // last_tag persisted for this session's staged release
    // "Update & rebuild": update-and-rebuild.bat streamed onto the Updates page.
    RebuildState mRebuild;
    std::thread mRebuildThread;

    // status line
    std::string mStatus;
    uint64_t mStatusTime = 0;
};

} // namespace Launcher
