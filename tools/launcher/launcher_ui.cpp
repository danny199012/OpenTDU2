#include "launcher_ui.h"

#include "launcher_util.h"

#include "imgui.h"

#include "build_info.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Launcher {

namespace {

const ImVec4 kText = ImVec4( 0.93f, 0.94f, 0.96f, 1.00f );
const ImVec4 kDim = ImVec4( 0.60f, 0.64f, 0.70f, 1.00f );
const ImVec4 kAccent = ImVec4( 1.00f, 0.47f, 0.13f, 1.00f );
const ImVec4 kAccentDark = ImVec4( 0.72f, 0.29f, 0.06f, 1.00f );
const ImVec4 kPanel = ImVec4( 0.085f, 0.095f, 0.125f, 0.96f );
const ImVec4 kPanelBorder = ImVec4( 0.20f, 0.23f, 0.29f, 0.85f );
const ImVec4 kGreen = ImVec4( 0.36f, 0.85f, 0.47f, 1.00f );
const ImVec4 kRed = ImVec4( 1.00f, 0.40f, 0.38f, 1.00f );
const ImVec4 kAmber = ImVec4( 1.00f, 0.76f, 0.30f, 1.00f );

// Layout constants are written in logical (96-DPI) pixels and scaled once: the
// fonts are baked at the monitor DPI, so the boxes around them must be too.
float gUiScale = 1.0f;
float S( float value ) { return value * gUiScale; }

ImU32 ToU32( const ImVec4 &color ) {
    return ImGui::ColorConvertFloat4ToU32( color );
}

ImU32 ToU32( const ImVec4 &color, float alpha ) {
    ImVec4 copy = color;
    copy.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32( copy );
}

void DrawCoverImage( ImDrawList *drawList, const ArtworkImage &image, const ImVec2 &pos,
                     const ImVec2 &size, float rounding, float alpha ) {
    if ( !image.IsValid() || image.Width <= 0 || image.Height <= 0 ) {
        drawList->AddRectFilled( pos, ImVec2( pos.x + size.x, pos.y + size.y ),
                                 IM_COL32( 24, 27, 36, 255 ), rounding );
        return;
    }
    // Cover: fill the box, crop the longer side.
    const float scale = std::max( size.x / static_cast<float>( image.Width ),
                                  size.y / static_cast<float>( image.Height ) );
    const float visibleU = size.x / ( static_cast<float>( image.Width ) * scale );
    const float visibleV = size.y / ( static_cast<float>( image.Height ) * scale );
    const ImVec2 uv0( 0.5f - visibleU * 0.5f, 0.5f - visibleV * 0.5f );
    const ImVec2 uv1( 0.5f + visibleU * 0.5f, 0.5f + visibleV * 0.5f );
    drawList->AddImageRounded( reinterpret_cast<ImTextureID>( image.View ), pos,
                               ImVec2( pos.x + size.x, pos.y + size.y ), uv0, uv1,
                               IM_COL32( 255, 255, 255, static_cast<int>( alpha * 255.0f ) ),
                               rounding );
}

void DrawGradientOverlay( ImDrawList *drawList, const ImVec2 &pos, const ImVec2 &size ) {
    // Bottom-heavy darkening so white text stays readable over any capture.
    const ImVec2 bottomRight( pos.x + size.x, pos.y + size.y );
    drawList->AddRectFilledMultiColor( pos, bottomRight, IM_COL32( 8, 9, 13, 60 ),
                                       IM_COL32( 8, 9, 13, 40 ), IM_COL32( 8, 9, 13, 235 ),
                                       IM_COL32( 8, 9, 13, 235 ) );
}

bool CardBegin( const char *id, const ImVec2 &size, bool bScroll = false ) {
    ImGui::PushStyleColor( ImGuiCol_ChildBg, kPanel );
    ImGui::PushStyleColor( ImGuiCol_Border, kPanelBorder );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 12.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_ChildBorderSize, 1.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( S( 18.0f ), S( 15.0f ) ) );
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollWithMouse;
    if ( bScroll ) {
        flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
    } else {
        flags |= ImGuiWindowFlags_NoScrollbar;
    }
    return ImGui::BeginChild( id, size, ImGuiChildFlags_Borders, flags );
}

void CardEnd() {
    ImGui::EndChild();
    ImGui::PopStyleVar( 3 );
    ImGui::PopStyleColor( 2 );
}

} // namespace

namespace {

void SectionTitle( const char *text ) {
    ImGui::PushStyleColor( ImGuiCol_Text, kDim );
    ImGui::TextUnformatted( text );
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void Chip( const char *text, const ImVec4 &color ) {
    const ImVec2 textSize = ImGui::CalcTextSize( text );
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2( pos.x, pos.y - 1.0f ),
        ImVec2( pos.x + textSize.x + 16.0f, pos.y + textSize.y + 3.0f ),
        ToU32( color, 0.16f ), S( 6.0f ) );
    ImGui::SetCursorScreenPos( ImVec2( pos.x + 8.0f, pos.y + 1.0f ) );
    ImGui::PushStyleColor( ImGuiCol_Text, color );
    ImGui::TextUnformatted( text );
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos( ImVec2( pos.x, pos.y + textSize.y + 8.0f ) );
}

bool AccentButton( const char *label, const ImVec2 &size, bool bEnabled = true ) {
    ImGui::PushStyleColor( ImGuiCol_Button,
                           bEnabled ? kAccent : ImVec4( 0.25f, 0.27f, 0.31f, 1.0f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, kAccent );
    ImGui::PushStyleColor( ImGuiCol_ButtonActive, kAccentDark );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.09f, 0.07f, 0.05f, 1.0f ) );
    const bool bPressed = ImGui::Button( label, size );
    ImGui::PopStyleColor( 4 );
    return bPressed && bEnabled;
}

bool GhostButton( const char *label, const ImVec2 &size = ImVec2( 0, 0 ) ) {
    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.16f, 0.18f, 0.23f, 1.0f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.22f, 0.25f, 0.32f, 1.0f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.12f, 0.14f, 0.18f, 1.0f ) );
    const bool bPressed = ImGui::Button( label, size );
    ImGui::PopStyleColor( 3 );
    return bPressed;
}

bool NavItem( const char *label, Page page, Page current ) {
    const bool bSelected = page == current;
    ImGui::PushStyleColor( ImGuiCol_Header, bSelected ? ImVec4( 0.16f, 0.17f, 0.21f, 1.0f )
                                                      : ImVec4( 0.10f, 0.11f, 0.14f, 0.0f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( 0.17f, 0.19f, 0.24f, 1.0f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( 0.20f, 0.22f, 0.28f, 1.0f ) );
    ImGui::PushStyleColor( ImGuiCol_Text,
                           bSelected ? kText : ImVec4( 0.72f, 0.75f, 0.80f, 1.0f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_SelectableTextAlign, ImVec2( 0.06f, 0.5f ) );
    const bool bPressed = ImGui::Selectable( label, bSelected, 0, ImVec2( 0.0f, S( 42.0f ) ) );
    ImGui::PopStyleVar();
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    if ( bSelected ) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2( itemMin.x, itemMin.y + 8.0f ),
            ImVec2( itemMin.x + 3.0f, itemMax.y - 8.0f ), ToU32( kAccent ), 2.0f );
    }
    ImGui::PopStyleColor( 4 );
    return bPressed;
}



const char *PageTitle( Page page ) {
    switch ( page ) {
    case Page::Play: return "Play";
    case Page::GameData: return "Game data";
    case Page::Updates: return "Updates";
    case Page::Player: return "Player";
    default: return "Online";
    }
}

const char *PageSubtitle( Page page ) {
    switch ( page ) {
    case Page::Play: return "Test Drive Unlimited 2 - OpenTDU2";
    case Page::GameData: return "Install from a disc image or point at the unpacked game";
    case Page::Updates: return "Build stamp, changelog and updates";
    case Page::Player: return "The name in the profile and in every save";
    default: return "The account for the multiplayer that comes next";
    }
}

} // namespace


LauncherApp::~LauncherApp() { Shutdown(); }

void LauncherApp::ApplyThemeOnce() {
    if ( mThemeApplied ) {
        return;
    }
    mThemeApplied = true;
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.GrabRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.TabRounding = 8.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2( 10.0f, 9.0f );
    style.ItemInnerSpacing = ImVec2( 8.0f, 6.0f );
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 12.0f;
    style.SeparatorTextBorderSize = 1.0f;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kDim;
    colors[ImGuiCol_WindowBg] = ImVec4( 0.055f, 0.062f, 0.082f, 1.00f );
    colors[ImGuiCol_ChildBg] = kPanel;
    colors[ImGuiCol_PopupBg] = ImVec4( 0.08f, 0.09f, 0.12f, 0.98f );
    colors[ImGuiCol_Border] = kPanelBorder;
    colors[ImGuiCol_FrameBg] = ImVec4( 0.13f, 0.15f, 0.19f, 1.00f );
    colors[ImGuiCol_FrameBgHovered] = ImVec4( 0.17f, 0.19f, 0.24f, 1.00f );
    colors[ImGuiCol_FrameBgActive] = ImVec4( 0.20f, 0.23f, 0.29f, 1.00f );
    colors[ImGuiCol_TitleBg] = ImVec4( 0.07f, 0.08f, 0.10f, 1.00f );
    colors[ImGuiCol_TitleBgActive] = ImVec4( 0.09f, 0.10f, 0.13f, 1.00f );
    colors[ImGuiCol_Button] = ImVec4( 0.16f, 0.18f, 0.23f, 1.00f );
    colors[ImGuiCol_ButtonHovered] = ImVec4( 0.22f, 0.25f, 0.32f, 1.00f );
    colors[ImGuiCol_ButtonActive] = ImVec4( 0.12f, 0.14f, 0.18f, 1.00f );
    colors[ImGuiCol_Header] = ImVec4( 0.16f, 0.17f, 0.21f, 1.00f );
    colors[ImGuiCol_HeaderHovered] = ImVec4( 0.17f, 0.19f, 0.24f, 1.00f );
    colors[ImGuiCol_HeaderActive] = ImVec4( 0.20f, 0.22f, 0.28f, 1.00f );
    colors[ImGuiCol_Separator] = kPanelBorder;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentDark;
    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_ScrollbarBg] = ImVec4( 0.07f, 0.08f, 0.10f, 0.60f );
    colors[ImGuiCol_ScrollbarGrab] = ImVec4( 0.24f, 0.27f, 0.33f, 1.00f );
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4( 0.30f, 0.34f, 0.41f, 1.00f );
    colors[ImGuiCol_ScrollbarGrabActive] = kAccent;
    colors[ImGuiCol_PlotHistogram] = kAccent;
    colors[ImGuiCol_PlotHistogramHovered] = kAccent;
    colors[ImGuiCol_TextSelectedBg] = ImVec4( kAccent.x, kAccent.y, kAccent.z, 0.35f );

    // The theme above is written for 96 DPI; the widgets follow the monitor.
    style.ScaleAllSizes( mDpiScale );
}


void LauncherApp::LoadFonts( float dpiScale ) {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();

    const float scale = dpiScale > 0.0f ? dpiScale : 1.0f;
    const auto load = [&]( const char *path, float pixels ) -> ImFont * {
        ImFontConfig config;
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        return io.Fonts->AddFontFromFileTTF( path, pixels * scale, &config,
                                             io.Fonts->GetGlyphRangesDefault() );
    };

    mFontSmall = load( "C:\\Windows\\Fonts\\segoeui.ttf", 13.0f );
    mFontBody = load( "C:\\Windows\\Fonts\\segoeui.ttf", 16.0f );
    mFontBold = load( "C:\\Windows\\Fonts\\segoeuib.ttf", 16.0f );
    mFontTitle = load( "C:\\Windows\\Fonts\\segoeuib.ttf", 26.0f );
    mFontHero = load( "C:\\Windows\\Fonts\\segoeuib.ttf", 44.0f );
    if ( mFontBody == nullptr ) {
        mFontBody = io.Fonts->AddFontDefault();
    }
    if ( mFontSmall == nullptr ) {
        mFontSmall = mFontBody;
    }
    if ( mFontBold == nullptr ) {
        mFontBold = mFontBody;
    }
    if ( mFontTitle == nullptr ) {
        mFontTitle = mFontBold;
    }
    if ( mFontHero == nullptr ) {
        mFontHero = mFontTitle;
    }
    io.FontDefault = mFontBody;
}

bool LauncherApp::Initialize( HWND window, ID3D11Device *device, ID3D11DeviceContext *context,
                              float dpiScale ) {
    mWindow = window;
    mDevice = device;
    mContext = context;
    mDpiScale = dpiScale > 0.0f ? dpiScale : 1.0f;
    gUiScale = mDpiScale;
    mLauncherDir = ExeDir();
    mIniPath = IniPath();
    mIsGitCheckout = !FindRepositoryRoot( mLauncherDir ).empty();
    mSettings.Load( mIniPath );
    {
        std::lock_guard<std::mutex> lock( mUpdate.Mutex );
        mUpdate.CurrentBuild = OTDU_RELEASE_STRING;
    }
    // A staged release from a previous session (the apply never ran, or its
    // script failed): restore the "Restart to apply" state so the download is
    // not thrown away.
    if ( StagedDirHasFiles( UpdateStagingDir() ) ) {
        std::string stagedTag;
        ReadTextFile( JoinPath( UpdateRootDir(), L"tag.txt" ), stagedTag );
        while ( !stagedTag.empty() &&
                ( stagedTag.back() == '\n' || stagedTag.back() == '\r' ||
                  stagedTag.back() == ' ' ) ) {
            stagedTag.pop_back();
        }
        std::lock_guard<std::mutex> lock( mUpdate.Mutex );
        mUpdate.bStaged = true;
        mUpdate.Phase = "Release staged.";
        mUpdate.Summary = ( stagedTag.empty() ? std::string( "A release from the last session" )
                                              : "Release " + stagedTag ) +
                          " is still staged - restart to apply it.";
    }

    LoadFonts( mDpiScale );
    ApplyThemeOnce();
    LoadArtwork( device, mArtwork );
    SyncFieldsFromSettings();
    RefreshGameState();
    RefreshProfileSummary();
    SetStatus( "Launcher ready - " OTDU_RELEASE_STRING "." );
    return true;
}

void LauncherApp::Shutdown() {
    if ( mShuttingDown ) {
        return;
    }
    mShuttingDown = true;
    SaveSettings();
    mInstall.bCancel = true;
    mUpdate.bCancel = true;
    if ( mInstallThread.joinable() ) {
        mInstallThread.join();
    }
    if ( mUpdateThread.joinable() ) {
        mUpdateThread.join();
    }
    // A rebuild in flight: the kill-on-close job (where the system honours it)
    // and the pid-tree walk take the bat's whole process tree down - ninja
    // restarts cleanly next time, and a merge the bat was stopped in self-heals
    // at its next start. Then the worker can finish.
    if ( mRebuildThread.joinable() ) {
        {
            std::lock_guard<std::mutex> lock( mRebuild.Mutex );
            if ( mRebuild.hJob != nullptr ) {
                CloseHandle( mRebuild.hJob );
                mRebuild.hJob = nullptr;
            }
        }
        KillProcessTree( mRebuild.ChildPid.load() );
        mRebuildThread.join();
    }
    if ( mGameProcess != nullptr ) {
        CloseHandle( mGameProcess );
        mGameProcess = nullptr;
    }
    mArtwork.Release();
}

void LauncherApp::SyncFieldsFromSettings() {
    std::snprintf( mPlayerName, sizeof( mPlayerName ), "%s", mSettings.PlayerName.c_str() );
    std::snprintf( mOnlineUsername, sizeof( mOnlineUsername ), "%s",
                   mSettings.OnlineUsername.c_str() );
    std::snprintf( mOnlinePassword, sizeof( mOnlinePassword ), "%s",
                   mSettings.OnlinePassword.c_str() );
    std::snprintf( mOnlineServer, sizeof( mOnlineServer ), "%s",
                   mSettings.OnlineServer.c_str() );
    std::snprintf( mGameFolder, sizeof( mGameFolder ), "%s", mSettings.GameFolder.c_str() );
    std::snprintf( mGameExe, sizeof( mGameExe ), "%s", mSettings.GameExe.c_str() );
    std::snprintf( mUserData, sizeof( mUserData ), "%s", mSettings.UserDataFolder.c_str() );
    std::snprintf( mIsoPath, sizeof( mIsoPath ), "%s", mSettings.IsoPath.c_str() );
    std::snprintf( mIsoDestination, sizeof( mIsoDestination ), "%s",
                   mSettings.IsoDestination.c_str() );
    std::snprintf( mUpdateRepo, sizeof( mUpdateRepo ), "%s", mSettings.UpdateRepo.c_str() );
    std::snprintf( mUpdateBranch, sizeof( mUpdateBranch ), "%s", mSettings.UpdateBranch.c_str() );
}

void LauncherApp::SyncSettingsFromFields() {
    mSettings.PlayerName = mPlayerName;
    mSettings.OnlineUsername = mOnlineUsername;
    mSettings.OnlinePassword = mOnlinePassword;
    mSettings.OnlineServer = mOnlineServer;
    mSettings.GameFolder = mGameFolder;
    mSettings.GameExe = mGameExe;
    mSettings.UserDataFolder = mUserData;
    mSettings.IsoPath = mIsoPath;
    mSettings.IsoDestination = mIsoDestination;
    mSettings.UpdateRepo = mUpdateRepo;
    mSettings.UpdateBranch = mUpdateBranch;
}

void LauncherApp::SaveSettings() {
    SyncSettingsFromFields();
    mSettings.Save( mIniPath );
}

void LauncherApp::SetStatus( const std::string &text ) {
    mStatus = text;
    mStatusTime = GetTickCount64();
}


void LauncherApp::RefreshGameState() {
    const std::wstring folder = Utf8ToWide( mSettings.GameFolder );
    mGameFolderValid = IsValidGameFolder( folder );

    mResolvedGameExe.clear();
    mGameExeError.clear();
    FindGameExecutable( mLauncherDir, Utf8ToWide( mSettings.GameExe ), mResolvedGameExe,
                        mGameExeError );

    mEffectiveUserData = mSettings.UserDataFolder.empty()
                             ? DefaultUserDataDir()
                             : Utf8ToWide( mSettings.UserDataFolder );
    mProfilePath = JoinPath( mEffectiveUserData, L"profile.sav" );
}

void LauncherApp::RefreshProfileSummary() {
    mProfileName.clear();
    mProfileSummary.clear();
    mProfileExists = FileExists( mProfilePath );
    if ( !mProfileExists ) {
        return;
    }
    std::string text;
    if ( !ReadTextFile( mProfilePath, text ) ) {
        return;
    }
    std::string money;
    std::string story;
    std::string cars;
    size_t pos = 0;
    while ( pos < text.size() ) {
        const size_t newline = text.find( '\n', pos );
        std::string line = text.substr(
            pos, newline == std::string::npos ? std::string::npos : newline - pos );
        pos = newline == std::string::npos ? text.size() : newline + 1;
        while ( !line.empty() && ( line.back() == '\r' || line.back() == ' ' ) ) {
            line.pop_back();
        }
        const size_t equals = line.find( '=' );
        if ( equals == std::string::npos ) {
            continue;
        }
        std::string key = line.substr( 0, equals );
        std::string value = line.substr( equals + 1 );
        while ( !key.empty() && key.back() == ' ' ) {
            key.pop_back();
        }
        while ( !value.empty() && value.front() == ' ' ) {
            value.erase( 0, 1 );
        }
        if ( key == "name" ) {
            mProfileName = value;
        } else if ( key == "money" ) {
            money = value;
        } else if ( key == "story_step" ) {
            story = value;
        } else if ( key == "cars" ) {
            cars = value;
        }
    }
    mProfileSummary = "Story step " + ( story.empty() ? "0" : story ) + " - " +
                      ( money.empty() ? "0" : money ) + " money - " +
                      ( cars.empty() ? "1" : cars ) + " car(s)";
}


namespace {

void DrawChipAt( ImDrawList *drawList, const ImVec2 &pos, const char *text,
                 const ImVec4 &color ) {
    const ImVec2 size = ImGui::CalcTextSize( text );
    drawList->AddRectFilled( pos,
                             ImVec2( pos.x + size.x + 16.0f, pos.y + size.y + 4.0f ),
                             ToU32( color, 0.16f ), S( 6.0f ) );
    drawList->AddText( ImVec2( pos.x + 8.0f, pos.y + 2.0f ), ToU32( color ), text );
}

} // namespace

void LauncherApp::Frame() {
    // A finished extraction: join the worker and point the launcher at the result.
    if ( mInstallThread.joinable() && mInstall.bDone ) {
        mInstallThread.join();
        if ( mInstall.bOk ) {
            const std::wstring destination = Utf8ToWide( mIsoDestination );
            if ( IsValidGameFolder( destination ) ) {
                mSettings.GameFolder = mIsoDestination;
                std::snprintf( mGameFolder, sizeof( mGameFolder ), "%s",
                               mSettings.GameFolder.c_str() );
                RefreshGameState();
                RefreshProfileSummary();
                SetStatus( "Game data installed to " + mSettings.GameFolder + " (" +
                           std::to_string( mInstall.FilesDone.load() ) + " files)." );
                mPage = Page::Play;
            } else {
                SetStatus( "Extraction finished, but Euro\\Bnk is not in the destination." );
                mPage = Page::GameData;
            }
        } else {
            SetStatus( mInstall.GetError() );
            mPage = Page::GameData;
        }
    }

    // A finished rebuild: join the worker, then refresh what the rebuild
    // changed (the game exe and the changelog behind it).
    if ( mRebuildThread.joinable() && mRebuild.bDone.load() ) {
        mRebuildThread.join();
        const RebuildSnapshot rebuild = Snapshot( mRebuild );
        if ( rebuild.bSucceeded ) {
            RefreshGameState();
            SetStatus( "Update & rebuild finished." );
            if ( !mUpdateThread.joinable() ) {
                StartUpdateCheck(); // the changelog: what the rebuild pulled in
            }
        } else {
            SetStatus( rebuild.Phase );
        }
    }

    // The game process (the launcher stays open unless "close on launch").
    if ( mGameProcess != nullptr &&
         WaitForSingleObject( mGameProcess, 0 ) == WAIT_OBJECT_0 ) {
        CloseHandle( mGameProcess );
        mGameProcess = nullptr;
        mLaunching = false;
        SetStatus( "The game exited." );
        RefreshProfileSummary();
    }

    // One automatic update check per session.
    if ( mSettings.bAutoCheckUpdates && !mUpdate.bChecked && !mUpdateThread.joinable() ) {
        StartUpdateCheck();
    }

    // The check just finished: put an available update on the status line.
    if ( mCheckingForUpdates && !mUpdate.bBusy.load() ) {
        mCheckingForUpdates = false;
        const UpdateSnapshot update = Snapshot( mUpdate );
        if ( update.bAvailable && !update.bStaged ) {
            SetStatus( "Update available: " +
                           ( update.LatestTag.empty() ? update.LatestName : update.LatestTag ) +
                           " - the Updates page has the changelog." );
        }
    }

    // The download finished (possibly while another page was up): persist the
    // release tag here too - the Updates-page copy only runs when it is open.
    const bool bBusyNow = mUpdate.bBusy.load();
    if ( mUpdateWasBusy && !bBusyNow ) {
        const UpdateSnapshot update = Snapshot( mUpdate );
        if ( update.bStaged && !update.LatestTag.empty() ) {
            mStagedTagSaved = true;
            mSettings.LastUpdateTag = update.LatestTag;
            SaveSettings();
        }
    }
    mUpdateWasBusy = bBusyNow;

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( viewport->WorkPos );
    ImGui::SetNextWindowSize( viewport->WorkSize );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
    ImGui::Begin( "##otdu2_launcher_root", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus );

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    drawList->AddRectFilledMultiColor(
        origin, ImVec2( origin.x + available.x, origin.y + available.y ),
        IM_COL32( 11, 13, 18, 255 ), IM_COL32( 16, 19, 26, 255 ),
        IM_COL32( 22, 25, 34, 255 ), IM_COL32( 14, 16, 23, 255 ) );

    DrawSidebar();
    ImGui::SameLine( 0.0f, 0.0f );
    DrawMainArea();

    ImGui::End();
    ImGui::PopStyleVar( 2 );
}


void LauncherApp::DrawSidebar() {
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.050f, 0.055f, 0.070f, 1.0f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::BeginChild( "##sidebar", ImVec2( S( 278.0f ), 0.0f ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoScrollbar );

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 bannerPos = ImGui::GetCursorScreenPos();
    const ImVec2 bannerSize( S( 278.0f ), S( 162.0f ) );
    DrawCoverImage( drawList, mArtwork.Banner, bannerPos, bannerSize, 0.0f, 1.0f );
    DrawGradientOverlay( drawList, bannerPos, bannerSize );

    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 20.0f ), bannerPos.y + S( 20.0f ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, kAccent );
    ImGui::TextUnformatted( "OPEN SOURCE PORT" );
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 18.0f ), bannerPos.y + S( 46.0f ) ) );
    ImGui::PushFont( mFontHero );
    ImGui::TextUnformatted( "TDU2" );
    ImGui::PopFont();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 20.0f ), bannerPos.y + S( 112.0f ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, kDim );
    ImGui::TextUnformatted( "LAUNCHER  -  IBIZA & OAHU" );
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x, bannerPos.y + bannerSize.y + 14.0f ) );

    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( S( 16.0f ), S( 6.0f ) ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.0f, 4.0f ) );
    ImGui::BeginChild( "##nav", ImVec2( 0.0f, S( 262.0f ) ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoScrollbar );
    if ( NavItem( "Play", Page::Play, mPage ) ) {
        mPage = Page::Play;
    }
    if ( NavItem( "Game data", Page::GameData, mPage ) ) {
        mPage = Page::GameData;
    }
    if ( NavItem( "Updates", Page::Updates, mPage ) ) {
        mPage = Page::Updates;
    }
    if ( NavItem( "Player", Page::Player, mPage ) ) {
        mPage = Page::Player;
        RefreshProfileSummary();
    }
    if ( NavItem( "Online", Page::Online, mPage ) ) {
        mPage = Page::Online;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar( 2 );

    // The bottom block is pinned: watermark switch, build stamp, source.
    const float bottomY = ImGui::GetWindowHeight() - S( 178.0f );
    if ( ImGui::GetCursorPosY() < bottomY ) {
        ImGui::SetCursorPosY( bottomY );
    }
    ImGui::Indent( S( 18.0f ) );
    ImGui::PushStyleColor( ImGuiCol_Text, kDim );
    ImGui::TextUnformatted( "WATERMARK" );
    ImGui::PopStyleColor();
    ImGui::Checkbox( "Show my name in the watermark", &mSettings.bWatermark );
    ImGui::PushStyleColor( ImGuiCol_Text, kDim );
    ImGui::TextWrapped( "The build id is always stamped (screen corner + every screenshot)." );
    if ( mSettings.bWatermark ) {
        if ( mSettings.PlayerName.empty() ) {
            ImGui::TextWrapped( "Set a player name to stamp it too." );
        } else {
            ImGui::TextWrapped( "%s + build - in game top right", mSettings.PlayerName.c_str() );
        }
    } else {
        ImGui::TextWrapped( "Name off - build id stays." );
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushFont( mFontSmall );
    ImGui::TextUnformatted( OTDU_RELEASE_STRING );
    ImGui::TextUnformatted( OTDU_BUILD_TIMESTAMP );
    ImGui::TextUnformatted( mIsGitCheckout ? "git checkout" : "packaged build" );
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Unindent( S( 18.0f ) );

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}



void LauncherApp::DrawMainArea() {
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( S( 30.0f ), S( 24.0f ) ) );
    ImGui::BeginChild( "##main", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_None );

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 titlePos = ImGui::GetCursorScreenPos();
    const float contentRight = titlePos.x + ImGui::GetContentRegionAvail().x - S( 30.0f );

    const char *chipText = mGameFolderValid ? "GAME DATA READY" : "NO GAME DATA";
    const float chipWidth = ImGui::CalcTextSize( chipText ).x + 16.0f;
    DrawChipAt( drawList, ImVec2( contentRight - chipWidth, titlePos.y + 8.0f ), chipText,
                mGameFolderValid ? kGreen : kRed );

    ImGui::PushFont( mFontTitle );
    ImGui::TextUnformatted( PageTitle( mPage ) );
    ImGui::PopFont();
    ImGui::PushStyleColor( ImGuiCol_Text, kDim );
    ImGui::TextUnformatted( PageSubtitle( mPage ) );
    ImGui::PopStyleColor();
    ImGui::Dummy( ImVec2( 0.0f, 10.0f ) );

    ImGui::BeginChild( "##page", ImVec2( 0.0f, -S( 46.0f ) ), ImGuiChildFlags_None,
                       ImGuiWindowFlags_None );
    switch ( mPage ) {
    case Page::Play: DrawPlayPage(); break;
    case Page::GameData: DrawGameDataPage(); break;
    case Page::Updates: DrawUpdatesPage(); break;
    case Page::Player: DrawPlayerPage(); break;
    case Page::Online: DrawOnlinePage(); break;
    }
    ImGui::EndChild();

    DrawFooter();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void LauncherApp::DrawFooter() {
    ImGui::Separator();
    const uint64_t now = GetTickCount64();
    std::string status = "Ready.";
    if ( !mStatus.empty() && now - mStatusTime < 12000 ) {
        status = mStatus;
    }
    ImGui::PushStyleColor( ImGuiCol_Text, kDim );
    ImGui::TextUnformatted( status.c_str() );
    const std::string right =
        std::string( OTDU_RELEASE_STRING ) + "   -   " + OTDU_BUILD_TIMESTAMP;
    const float width = ImGui::CalcTextSize( right.c_str() ).x;
    ImGui::SameLine( ImGui::GetWindowWidth() - width - S( 30.0f ) );
    ImGui::TextUnformatted( right.c_str() );
    ImGui::PopStyleColor();
}

void LauncherApp::DrawPlayPage() {
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 heroPos = ImGui::GetCursorScreenPos();
    const ImVec2 heroSize( width, S( 272.0f ) );
    DrawCoverImage( drawList, mArtwork.Play, heroPos, heroSize, 14.0f, 1.0f );
    DrawGradientOverlay( drawList, heroPos, heroSize );

    ImGui::SetCursorScreenPos( ImVec2( heroPos.x + S( 26.0f ), heroPos.y + S( 40.0f ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, kAccent );
    ImGui::TextUnformatted( "TEST DRIVE UNLIMITED 2" );
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos( ImVec2( heroPos.x + S( 24.0f ), heroPos.y + S( 62.0f ) ) );
    ImGui::PushFont( mFontHero );
    ImGui::TextUnformatted( "OpenTDU2" );
    ImGui::PopFont();
    ImGui::SetCursorScreenPos( ImVec2( heroPos.x + S( 26.0f ), heroPos.y + S( 122.0f ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.86f, 0.88f, 0.91f, 1.0f ) );
    ImGui::TextUnformatted(
        "The Ibiza and Oahu you remember, rebuilt from the retail data on Vulkan." );
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(
        ImVec2( heroPos.x + S( 26.0f ), heroPos.y + heroSize.y - S( 88.0f ) ) );
    ImGui::PushFont( mFontBold );
    const bool bCanLaunch = mGameFolderValid && !mResolvedGameExe.empty() && !mLaunching;
    if ( AccentButton( mLaunching ? "GAME RUNNING..." : "PLAY", ImVec2( S( 230.0f ), S( 54.0f ) ),
                       bCanLaunch ) ) {
        LaunchGame();
    }
    ImGui::PopFont();
    ImGui::SameLine();
    if ( GhostButton( "Open game folder", ImVec2( S( 150.0f ), S( 54.0f ) ) ) ) {
        if ( !OpenInExplorer( Utf8ToWide( mSettings.GameFolder ) ) ) {
            SetStatus( "No game folder to open yet." );
        }
    }
    if ( !mGameFolderValid ) {
        ImGui::SetCursorScreenPos(
            ImVec2( heroPos.x + S( 26.0f ), heroPos.y + heroSize.y - 26.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Text, kAmber );
        ImGui::TextUnformatted(
            "No game data yet - open Game data to install it from a .iso." );
        ImGui::PopStyleColor();
    } else if ( mResolvedGameExe.empty() ) {
        ImGui::SetCursorScreenPos(
            ImVec2( heroPos.x + S( 26.0f ), heroPos.y + heroSize.y - 26.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Text, kAmber );
        ImGui::TextUnformatted( mGameExeError.c_str() );
        ImGui::PopStyleColor();
    }

    const char *kUnofficial =
        "Unofficial fan project - not affiliated with Nacon / Eden Games / Atari.";
    const ImVec2 legalSize = ImGui::CalcTextSize( kUnofficial );
    // the amber warning owns the bottom row when it shows - the legal line
    // moves one row up on the right so the two never collide on a narrow window
    const bool bWarnRow = !mGameFolderValid || mResolvedGameExe.empty();
    const float legalY = bWarnRow ? heroPos.y + heroSize.y - 26.0f - legalSize.y - 4.0f
                                  : heroPos.y + heroSize.y - 26.0f;
    ImGui::SetCursorScreenPos(
        ImVec2( heroPos.x + heroSize.x - legalSize.x - S( 26.0f ), legalY ) );
    ImGui::PushStyleColor( ImGuiCol_Text, kDim );
    ImGui::TextUnformatted( kUnofficial );
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos( ImVec2( heroPos.x, heroPos.y + heroSize.y + S( 18.0f ) ) );

    const float gap = 14.0f;
    const float cardWidth = ( width - gap * 2.0f ) / 3.0f;


    if ( CardBegin( "##status_game", ImVec2( cardWidth, S( 136.0f ) ) ) ) {
        SectionTitle( "GAME DATA" );
        Chip( mGameFolderValid ? "READY" : "NOT SET", mGameFolderValid ? kGreen : kRed );
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::PushFont( mFontSmall );
        ImGui::TextWrapped( "%s", mSettings.GameFolder.empty()
                                     ? "Choose the folder that holds Euro\\Bnk."
                                     : mSettings.GameFolder.c_str() );
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    CardEnd();
    ImGui::SameLine( 0.0f, gap );

    if ( CardBegin( "##status_player", ImVec2( cardWidth, S( 136.0f ) ) ) ) {
        SectionTitle( "PLAYER" );
        ImGui::PushFont( mFontBold );
        ImGui::TextUnformatted( mSettings.PlayerName.empty() ? "(system user name)"
                                                            : mSettings.PlayerName.c_str() );
        ImGui::PopFont();
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::PushFont( mFontSmall );
        ImGui::TextWrapped( mProfileExists
                                ? "profile.sav found - its name is this value."
                                : "No save yet - the first game writes profile.sav." );
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    CardEnd();
    ImGui::SameLine( 0.0f, gap );

    if ( CardBegin( "##status_build", ImVec2( cardWidth, S( 136.0f ) ) ) ) {
        SectionTitle( "BUILD" );
        ImGui::PushFont( mFontBold );
        ImGui::TextUnformatted( OTDU_RELEASE_STRING );
        ImGui::PopFont();
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::PushFont( mFontSmall );
        ImGui::TextWrapped( "%s - %s", OTDU_BUILD_TIMESTAMP,
                            mIsGitCheckout ? "git checkout" : "packaged build" );
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    CardEnd();

    ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );

    if ( CardBegin( "##launch_options", ImVec2( width, S( 214.0f ) ) ) ) {
        SectionTitle( "LAUNCH OPTIONS" );
        ImGui::Checkbox( "Show my name in the in-game watermark (build id is always shown)",
                         &mSettings.bWatermark );
        ImGui::Checkbox( "Skip the intro cinematic", &mSettings.bSkipIntro );
        ImGui::Checkbox( "Close the launcher when the game starts", &mSettings.bCloseOnLaunch );
        ImGui::Spacing();
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::TextUnformatted( "USER DATA (saves, options, mods)" );
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth( -S( 196.0f ) );
        ImGui::InputTextWithHint( "##userdata", WideToUtf8( DefaultUserDataDir() ).c_str(),
                                  mUserData, sizeof( mUserData ) );
        ImGui::SameLine();
        if ( GhostButton( "Browse", ImVec2( S( 86.0f ), 0.0f ) ) ) {
            std::wstring picked;
            if ( BrowseFolder( mWindow, L"Choose the user data folder", picked ) ) {
                std::snprintf( mUserData, sizeof( mUserData ), "%s",
                               WideToUtf8( picked ).c_str() );
                SyncSettingsFromFields();
                RefreshGameState();
                RefreshProfileSummary();
            }
        }
        ImGui::SameLine();
        if ( GhostButton( "Default", ImVec2( S( 86.0f ), 0.0f ) ) ) {
            mUserData[0] = '\0';
            SyncSettingsFromFields();
            RefreshGameState();
            RefreshProfileSummary();
        }
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::PushFont( mFontSmall );
        ImGui::TextWrapped( "Effective: %s", WideToUtf8( mEffectiveUserData ).c_str() );
        if ( !mResolvedGameExe.empty() ) {
            ImGui::TextWrapped( "Game exe: %s", WideToUtf8( mResolvedGameExe ).c_str() );
        } else if ( !mGameExeError.empty() ) {
            ImGui::TextWrapped( "%s", mGameExeError.c_str() );
        }
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if ( GhostButton( "Save settings" ) ) {
            SaveSettings();
            SetStatus( "launcher.ini saved." );
        }
        ImGui::SameLine();
        if ( GhostButton( "Open log" ) ) {
            const std::wstring logPath =
                JoinPath( ParentDir( mResolvedGameExe ), L"opentdu2.log" );
            if ( FileExists( logPath ) ) {
                RevealInExplorer( logPath );
            } else {
                SetStatus( "opentdu2.log does not exist yet - run the game once." );
            }
        }
    }
    CardEnd();
}


void LauncherApp::DrawGameDataPage() {
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    const ImVec2 bannerPos = ImGui::GetCursorScreenPos();
    const ImVec2 bannerSize( width, S( 104.0f ) );
    DrawCoverImage( drawList, mArtwork.GameData, bannerPos, bannerSize, 12.0f, 0.95f );
    DrawGradientOverlay( drawList, bannerPos, bannerSize );
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 22.0f ), bannerPos.y + S( 22.0f ) ) );
    ImGui::PushFont( mFontTitle );
    ImGui::TextUnformatted( "From a disc image" );
    ImGui::PopFont();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 22.0f ), bannerPos.y + S( 62.0f ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.86f, 0.88f, 0.91f, 1.0f ) );
    ImGui::TextUnformatted(
        "Point at a .iso: the launcher finds Euro\\Bnk inside it and extracts the game." );
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x, bannerPos.y + bannerSize.y + S( 16.0f ) ) );

    float isoCardHeight = S( 250.0f );
    if ( mIsoInspected ) {
        isoCardHeight += S( 140.0f );
    }
    if ( mInstall.bRunning.load() || mInstall.bDone.load() ) {
        isoCardHeight += S( 86.0f );
    }

    if ( CardBegin( "##iso_card", ImVec2( width, isoCardHeight ) ) ) {
        SectionTitle( "DISC IMAGE" );
        ImGui::SetNextItemWidth( -S( 108.0f ) );
        ImGui::InputTextWithHint( "##isopath", "C:\\path\\to\\TestDrive2.iso", mIsoPath,
                                  sizeof( mIsoPath ) );
        ImGui::SameLine();
        if ( GhostButton( "Browse...", ImVec2( S( 98.0f ), 0.0f ) ) ) {
            BrowseIso();
        }
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::TextUnformatted( "EXTRACT TO" );
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth( -S( 108.0f ) );
        ImGui::InputTextWithHint( "##isodest", "the game folder to create", mIsoDestination,
                                  sizeof( mIsoDestination ) );
        ImGui::SameLine();
        if ( GhostButton( "Browse...", ImVec2( S( 98.0f ), 0.0f ) ) ) {
            BrowseDestination();
        }
        const bool bBusy = mInstall.bRunning.load();
        ImGui::BeginDisabled( bBusy || mIsoPath[0] == '\0' );
        if ( GhostButton( "Inspect image" ) ) {
            InspectIsoFile();
        }
        ImGui::SameLine();
        if ( AccentButton( "Extract game data", ImVec2( S( 190.0f ), 0.0f ),
                           mIsoInspected && mIsoInspection.bHasGameData &&
                               mIsoDestination[0] != '\0' ) ) {
            ExtractIso();
        }
        ImGui::EndDisabled();
        if ( bBusy ) {
            ImGui::SameLine();
            if ( GhostButton( "Cancel" ) ) {
                mInstall.bCancel = true;
            }
        }

        if ( mIsoInspected ) {
            ImGui::Spacing();
            if ( !mIsoInspection.bOpened ) {
                ImGui::PushStyleColor( ImGuiCol_Text, kRed );
                ImGui::TextWrapped( "Cannot open the image: %s", mIsoInspection.Error.c_str() );
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor( ImGuiCol_Text, mIsoInspection.bHasGameData ? kGreen : kAmber );
                ImGui::TextWrapped( "%s", mIsoInspection.Note.c_str() );
                ImGui::PopStyleColor();
                ImGui::PushStyleColor( ImGuiCol_Text, kDim );
                ImGui::PushFont( mFontSmall );
                ImGui::TextWrapped( "%s  -  label '%s'  -  %s", mIsoInspection.Filesystem.c_str(),
                                    mIsoInspection.Label.c_str(),
                                    FormatBytes( mIsoInspection.ImageSize ).c_str() );
                if ( mIsoInspection.bHasGameData ) {
                    ImGui::TextWrapped( "Files to copy: %u  -  %s", mIsoInspection.FileCount,
                                        FormatBytes( mIsoInspection.TotalBytes ).c_str() );
                    const uint64_t freeBytes = FreeSpaceOf( Utf8ToWide( mIsoDestination ) );
                    if ( freeBytes != 0 ) {
                        ImGui::TextWrapped( "Free space at the destination: %s",
                                            FormatBytes( freeBytes ).c_str() );
                    }
                }
                ImGui::PopFont();
                ImGui::PopStyleColor();
            }
        }

        if ( bBusy || mInstall.bDone.load() ) {
            const uint64_t total = mInstall.BytesTotal.load();
            const uint64_t done = mInstall.BytesDone.load();
            const float fraction = total > 0 ? static_cast<float>( done ) /
                                                  static_cast<float>( total )
                                            : 0.0f;
            char overlay[160];
            std::snprintf( overlay, sizeof( overlay ), "%s / %s  -  %u of %u files",
                           FormatBytes( done ).c_str(), FormatBytes( total ).c_str(),
                           mInstall.FilesDone.load(), mInstall.FilesTotal.load() );
            ImGui::Spacing();
            ImGui::ProgressBar( fraction, ImVec2( -1.0f, S( 22.0f ) ), overlay );
            ImGui::PushStyleColor( ImGuiCol_Text, kDim );
            ImGui::PushFont( mFontSmall );
            const std::string current = mInstall.GetCurrentFile();
            if ( !current.empty() ) {
                ImGui::TextUnformatted( current.c_str() );
            }
            const std::string installError = mInstall.GetError();
            if ( !installError.empty() ) {
                ImGui::TextColored( kRed, "%s", installError.c_str() );
            } else if ( mInstall.bDone.load() ) {
                ImGui::TextColored( kGreen, "Done - %u files copied, %u already present.",
                                    mInstall.FilesDone.load(), mInstall.FilesSkipped.load() );
            }
            ImGui::PopFont();
            ImGui::PopStyleColor();
        }
    }
    CardEnd();


    if ( CardBegin( "##folder_card", ImVec2( width, S( 176.0f ) ) ) ) {
        SectionTitle( "OR POINT AT AN EXISTING GAME FOLDER" );
        ImGui::SetNextItemWidth( -S( 108.0f ) );
        ImGui::InputTextWithHint( "##gamefolder", "C:\\TDU2\\TestDrive2", mGameFolder,
                                  sizeof( mGameFolder ) );
        ImGui::SameLine();
        if ( GhostButton( "Browse...", ImVec2( S( 98.0f ), 0.0f ) ) ) {
            BrowseGameFolder();
        }
        if ( mGameFolder[0] != '\0' ) {
            Chip( mGameFolderValid ? "EURO\\BNK FOUND" : "NOT A GAME FOLDER",
                  mGameFolderValid ? kGreen : kRed );
        }
        if ( GhostButton( "Use this folder" ) ) {
            SyncSettingsFromFields();
            RefreshGameState();
            RefreshProfileSummary();
            SaveSettings();
            if ( mGameFolderValid ) {
                SetStatus( "Game data folder set." );
                mPage = Page::Play;
            } else {
                SetStatus( "That folder has no Euro\\Bnk." );
            }
        }
        ImGui::SameLine();
        if ( GhostButton( "Scan common places" ) ) {
            const std::wstring candidates[] = {
                L"C:\\TDU2-Gamedata\\TestDrive2",
                L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Test Drive Unlimited 2",
                L"C:\\Program Files\\Steam\\steamapps\\common\\Test Drive Unlimited 2",
                L"D:\\SteamLibrary\\steamapps\\common\\Test Drive Unlimited 2",
                L"E:\\SteamLibrary\\steamapps\\common\\Test Drive Unlimited 2",
            };
            bool bFound = false;
            for ( const std::wstring &candidate : candidates ) {
                if ( IsValidGameFolder( candidate ) ) {
                    std::snprintf( mGameFolder, sizeof( mGameFolder ), "%s",
                                   WideToUtf8( candidate ).c_str() );
                    SyncSettingsFromFields();
                    RefreshGameState();
                    RefreshProfileSummary();
                    SaveSettings();
                    SetStatus( "Found the game data in " + WideToUtf8( candidate ) );
                    bFound = true;
                    break;
                }
            }
            if ( !bFound ) {
                SetStatus( "No game data in the usual install folders." );
            }
        }
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::PushFont( mFontSmall );
        ImGui::TextWrapped(
            "OpenTDU2 reads the unpacked layout: Euro\\Bnk, Euro\\Shaders and the *.cpr files "
            "in the game root. A folder with bigfile_EU_*.map instead is the packed retail "
            "install and is not readable yet." );
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    CardEnd();
}


void LauncherApp::DrawUpdatesPage() {
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const UpdateSnapshot update = Snapshot( mUpdate );

    // The download finished (staged): remember the release tag - without it
    // the next start would offer the same release again even when this apply
    // succeeded (last_tag is the fallback when no build hash can be compared).
    if ( update.bStaged && !mStagedTagSaved && !update.LatestTag.empty() ) {
        mStagedTagSaved = true;
        mSettings.LastUpdateTag = update.LatestTag;
        SaveSettings();
    }

    const ImVec2 bannerPos = ImGui::GetCursorScreenPos();
    const ImVec2 bannerSize( width, S( 104.0f ) );
    DrawCoverImage( drawList, mArtwork.Updates, bannerPos, bannerSize, 12.0f, 0.95f );
    DrawGradientOverlay( drawList, bannerPos, bannerSize );
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 22.0f ), bannerPos.y + S( 22.0f ) ) );
    ImGui::PushFont( mFontTitle );
    ImGui::TextUnformatted( "Updates" );
    ImGui::PopFont();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 22.0f ), bannerPos.y + S( 62.0f ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.86f, 0.88f, 0.91f, 1.0f ) );
    ImGui::TextUnformatted(
        "The build stamp, the changelog since it was made, and one click to update." );
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x, bannerPos.y + bannerSize.y + 16.0f ) );

    if ( CardBegin( "##update_status", ImVec2( width, S( 212.0f ) ) ) ) {
        ImGui::PushFont( mFontBold );
        ImGui::TextUnformatted( OTDU_RELEASE_STRING );
        ImGui::PopFont();
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::PushFont( mFontSmall );
        ImGui::TextWrapped( "Built %s, %s", OTDU_BUILD_TIMESTAMP,
                            mIsGitCheckout ? "running from a git checkout"
                                           : "running from a packaged build" );
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if ( !update.Source.empty() ) {
            ImGui::PushStyleColor( ImGuiCol_Text, kDim );
            ImGui::TextUnformatted( update.Source.c_str() );
            ImGui::PopStyleColor();
        }
        if ( !update.Summary.empty() ) {
            ImGui::TextWrapped( "%s", update.Summary.c_str() );
        }
        if ( !update.Error.empty() ) {
            ImGui::PushStyleColor( ImGuiCol_Text, kRed );
            ImGui::TextWrapped( "%s", update.Error.c_str() );
            ImGui::PopStyleColor();
        }
        if ( !update.Hint.empty() ) {
            ImGui::PushStyleColor( ImGuiCol_Text, kDim );
            ImGui::TextWrapped( "%s", update.Hint.c_str() );
            ImGui::PopStyleColor();
        }
        if ( update.bBusy || update.DownloadTotal > 0 ) {
            const float fraction = update.DownloadTotal > 0
                                       ? static_cast<float>( update.DownloadDone ) /
                                             static_cast<float>( update.DownloadTotal )
                                       : 0.0f;
            char overlay[128];
            std::snprintf( overlay, sizeof( overlay ), "%s / %s",
                           FormatBytes( update.DownloadDone ).c_str(),
                           FormatBytes( update.DownloadTotal ).c_str() );
            ImGui::ProgressBar( fraction, ImVec2( -1.0f, S( 20.0f ) ), overlay );
        }
        ImGui::Spacing();
        ImGui::BeginDisabled( update.bBusy );
        if ( GhostButton( update.bBusy ? "Checking..." : "Check for updates" ) ) {
            StartUpdateCheck();
        }
        ImGui::EndDisabled();
        if ( update.bCanApply && !update.bBusy ) {
            ImGui::SameLine();
            if ( AccentButton( update.DownloadUrl.empty() ? "Fast-forward this checkout"
                                                          : "Download this release",
                               ImVec2( S( 224.0f ), 0.0f ) ) ) {
                ApplyUpdate();
            }
        }
        if ( update.bStaged ) {
            ImGui::SameLine();
            if ( AccentButton( "Restart to apply", ImVec2( S( 160.0f ), 0.0f ) ) ) {
                if ( StartStagedUpdate( mLauncherDir, mUpdate ) ) {
                    mExitRequested = true;
                } else {
                    SetStatus( Snapshot( mUpdate ).Error );
                }
            }
        }
        if ( !update.ReleasePage.empty() ) {
            ImGui::SameLine();
            if ( GhostButton( "Open on GitHub" ) ) {
                OpenUrl( Utf8ToWide( update.ReleasePage ) );
            }
        }
    }
    CardEnd();

    // What the update carries: the commit subjects between this build and the
    // release (the compare API; absent when no hash could be compared).
    if ( update.bAvailable && !update.WhatsNew.empty() ) {
        if ( CardBegin( "##whats_new", ImVec2( width, S( 236.0f ) ), true ) ) {
            SectionTitle( "WHAT'S NEW SINCE YOUR BUILD" );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.78f, 0.81f, 0.86f, 1.0f ) );
            ImGui::TextUnformatted( update.WhatsNew.c_str() );
            ImGui::PopStyleColor();
        }
        CardEnd();
    }

    // The git-checkout rebuild: the fast-forward above cannot apply here (the
    // tree carries its own merge commits) and a release download cannot build
    // - the repo's update-and-rebuild.bat merges every branch and rebuilds.
    if ( mIsGitCheckout ) {
        const RebuildSnapshot rebuild = Snapshot( mRebuild );
        const bool bHasLog = !rebuild.Log.empty();
        const float cardHeight =
            S( 128.0f ) + ( bHasLog ? ( rebuild.bRunning ? S( 272.0f ) : S( 182.0f ) ) : 0.0f );
        if ( CardBegin( "##rebuild", ImVec2( width, cardHeight ) ) ) {
            SectionTitle( "UPDATE & REBUILD" );
            ImGui::PushStyleColor( ImGuiCol_Text, kDim );
            ImGui::PushFont( mFontSmall );
            ImGui::TextWrapped(
                "Fetches origin, merges main + glm + cline and rebuilds the exe "
                "(update-and-rebuild.bat). A build takes a few minutes; the game must not run." );
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::BeginDisabled( rebuild.bRunning || update.bBusy || mGameProcess != nullptr );
            if ( AccentButton( rebuild.bRunning ? "Rebuilding..." : "Update & rebuild",
                               ImVec2( S( 224.0f ), 0.0f ) ) ) {
                StartUpdateRebuild();
            }
            ImGui::EndDisabled();
            if ( !rebuild.bRunning && rebuild.bDone ) {
                ImGui::SameLine();
                ImGui::PushStyleColor( ImGuiCol_Text, rebuild.bSucceeded ? kGreen : kRed );
                ImGui::TextUnformatted( rebuild.Phase.c_str() );
                ImGui::PopStyleColor();
            } else if ( rebuild.bRunning ) {
                ImGui::SameLine();
                ImGui::PushStyleColor( ImGuiCol_Text, kDim );
                ImGui::TextUnformatted( rebuild.Phase.c_str() );
                ImGui::PopStyleColor();
            }
            if ( bHasLog ) {
                ImGui::Spacing();
                ImGui::PushFont( mFontSmall );
                if ( ImGui::BeginChild( "##rebuild_log", ImVec2( 0.0f, 0.0f ),
                                        ImGuiChildFlags_Borders | ImGuiChildFlags_FrameStyle ) ) {
                    const bool follow =
                        ImGui::GetScrollY() + ImGui::GetWindowHeight() >=
                        ImGui::GetScrollMaxY() - S( 10.0f );
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.78f, 0.81f, 0.86f, 1.0f ) );
                    ImGui::TextUnformatted( rebuild.Log.c_str() );
                    ImGui::PopStyleColor();
                    if ( rebuild.bRunning && follow ) {
                        ImGui::SetScrollHereY( 1.0f );
                    }
                }
                ImGui::EndChild();
                ImGui::PopFont();
            }
        }
        CardEnd();
    }

    if ( CardBegin( "##changelog", ImVec2( width, S( 330.0f ) ), true ) ) {
        SectionTitle( "CHANGELOG" );
        if ( update.Entries.empty() ) {
            ImGui::PushStyleColor( ImGuiCol_Text, kDim );
            ImGui::TextWrapped( "Nothing yet - press Check for updates." );
            ImGui::PopStyleColor();
        } else {
            for ( size_t index = 0; index < update.Entries.size(); index++ ) {
                const UpdateEntry &entry = update.Entries[index];
                ImGui::PushID( static_cast<int>( index ) );
                ImGui::PushFont( mFontBold );
                ImGui::TextWrapped( "%s", entry.Title.c_str() );
                ImGui::PopFont();
                if ( !entry.Id.empty() || !entry.When.empty() ) {
                    ImGui::PushStyleColor( ImGuiCol_Text, kDim );
                    ImGui::PushFont( mFontSmall );
                const std::string meta = entry.Id + "    " + entry.When;
                ImGui::TextUnformatted( meta.c_str() );
                ImGui::PopFont();
                ImGui::PopStyleColor();
                }
                // Release notes are markdown; the launcher renders plain text.
                const std::string body = StripMarkdownNoise( entry.Body );
                if ( !body.empty() ) {
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.78f, 0.81f, 0.86f, 1.0f ) );
                    if ( entry.bDetailsCollapsed ) {
                        if ( ImGui::TreeNodeEx( "details", ImGuiTreeNodeFlags_SpanAvailWidth ) ) {
                            ImGui::PushFont( mFontSmall );
                            ImGui::TextWrapped( "%s", body.c_str() );
                            ImGui::PopFont();
                            ImGui::TreePop();
                        }
                    } else {
                        ImGui::TextWrapped( "%s", body.c_str() );
                    }
                    ImGui::PopStyleColor();
                }
                if ( !entry.Url.empty() ) {
                    if ( ImGui::SmallButton( "Open on GitHub" ) ) {
                        OpenUrl( Utf8ToWide( entry.Url ) );
                    }
                }
                ImGui::Separator();
                ImGui::PopID();
            }
        }
    }
    CardEnd();

    ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );

    if ( CardBegin( "##update_source", ImVec2( width, S( 148.0f ) ) ) ) {
        SectionTitle( "UPDATE SOURCE" );
        ImGui::SetNextItemWidth( -S( 210.0f ) );
        ImGui::InputTextWithHint( "##repo", "owner/repo", mUpdateRepo, sizeof( mUpdateRepo ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( S( 186.0f ) );
        ImGui::InputTextWithHint( "##branch", "main", mUpdateBranch,
                                  sizeof( mUpdateBranch ) );
        if ( ImGui::Checkbox( "Check when the launcher starts",
                              &mSettings.bAutoCheckUpdates ) ) {
            SaveSettings();
        }
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::PushFont( mFontSmall );
        ImGui::TextWrapped(
            "A git checkout updates from origin/<branch> and lists the commits in between. "
            "A packaged build updates from the GitHub releases of <owner/repo>: the release "
            "notes are the changelog and the first .zip asset is the package." );
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    CardEnd();
}

void LauncherApp::DrawPlayerPage() {
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    const ImVec2 bannerPos = ImGui::GetCursorScreenPos();
    const ImVec2 bannerSize( width, S( 104.0f ) );
    DrawCoverImage( drawList, mArtwork.Player, bannerPos, bannerSize, 12.0f, 0.95f );
    DrawGradientOverlay( drawList, bannerPos, bannerSize );
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 22.0f ), bannerPos.y + S( 22.0f ) ) );
    ImGui::PushFont( mFontTitle );
    ImGui::TextUnformatted( "Player" );
    ImGui::PopFont();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 22.0f ), bannerPos.y + S( 62.0f ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.86f, 0.88f, 0.91f, 1.0f ) );
    ImGui::TextUnformatted( "One name, everywhere: the in-game profile, the save and the "
                            "watermark name." );
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x, bannerPos.y + bannerSize.y + 16.0f ) );

    if ( CardBegin( "##player_name", ImVec2( width, S( 214.0f ) ) ) ) {
        // The retail "New profile" dialog, launcher edition: the same
        // nickname the game's own dialog (MAIN.vmf popup, first launch)
        // pre-fills.
        SectionTitle( "NEW PROFILE" );
        ImGui::SetNextItemWidth( -S( 210.0f ) );
        ImGui::InputTextWithHint( "##playername", "Nickname", mPlayerName,
                                  sizeof( mPlayerName ) );
        ImGui::SameLine();
        if ( GhostButton( "OK", ImVec2( S( 66.0f ), 0.0f ) ) ) {
            SyncSettingsFromFields();
            SaveSettings();
            RefreshProfileSummary();
            SetStatus( mSettings.PlayerName.empty() ? "Nickname cleared."
                                                    : "Nickname saved: " + mSettings.PlayerName );
        }
        ImGui::SameLine();
        if ( GhostButton( "Offline", ImVec2( S( 96.0f ), 0.0f ) ) ) {
            mPlayerName[0] = '\0';
            SyncSettingsFromFields();
            SaveSettings();
            RefreshProfileSummary();
            SetStatus( "Nickname cleared - the game falls back to the system user name." );
        }
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::PushFont( mFontSmall );
        ImGui::TextWrapped(
            "Passed to the game as -playername. The game shows this same dialog on first launch "
            "with the nickname pre-filled: OK there writes it to profile.sav and it prints in the "
            "story's 'Starring' card; the account fields on the Online page stay the online "
            "identity. With the name watermark on it is also the name beside the build id." );
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if ( GhostButton( "Open saves folder" ) ) {
            MakeDirs( mEffectiveUserData );
            if ( !OpenInExplorer( mEffectiveUserData ) ) {
                SetStatus( "The saves folder does not exist yet." );
            }
        }
        ImGui::SameLine();
        if ( GhostButton( "Open mods folder" ) ) {
            const std::wstring modsFolder = JoinPath( mEffectiveUserData, L"mods" );
            MakeDirs( modsFolder );
            OpenInExplorer( modsFolder );
        }
    }
    CardEnd();

    ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );

    if ( CardBegin( "##profile_card", ImVec2( width, S( 196.0f ) ) ) ) {
        SectionTitle( "THIS PROFILE" );
        if ( mProfileExists ) {
            ImGui::PushFont( mFontBold );
            ImGui::TextUnformatted( mProfileName.empty() ? "(unnamed)" : mProfileName.c_str() );
            ImGui::PopFont();
            ImGui::TextUnformatted( mProfileSummary.c_str() );
            ImGui::PushStyleColor( ImGuiCol_Text, kDim );
            ImGui::PushFont( mFontSmall );
            ImGui::TextWrapped( "%s", WideToUtf8( mProfilePath ).c_str() );
            ImGui::PopFont();
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted( "No profile.sav yet." );
            ImGui::PushStyleColor( ImGuiCol_Text, kDim );
            ImGui::PushFont( mFontSmall );
            ImGui::TextWrapped(
                "The first game writes it here: %s. Point the launcher's user data folder "
                "somewhere else on the Play page to keep two players apart.",
                WideToUtf8( mProfilePath ).c_str() );
            ImGui::PopFont();
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        if ( GhostButton( "Refresh" ) ) {
            RefreshProfileSummary();
            SetStatus( "Profile re-read." );
        }
        ImGui::SameLine();
        if ( GhostButton( "Open profile.sav" ) && mProfileExists ) {
            RevealInExplorer( mProfilePath );
        }
        ImGui::SameLine();
        if ( GhostButton( "Change user data folder" ) ) {
            mPage = Page::Play;
        }
    }
    CardEnd();
}


void LauncherApp::DrawOnlinePage() {
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    const ImVec2 bannerPos = ImGui::GetCursorScreenPos();
    const ImVec2 bannerSize( width, S( 104.0f ) );
    DrawCoverImage( drawList, mArtwork.Online, bannerPos, bannerSize, 12.0f, 0.95f );
    DrawGradientOverlay( drawList, bannerPos, bannerSize );
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 22.0f ), bannerPos.y + S( 22.0f ) ) );
    ImGui::PushFont( mFontTitle );
    ImGui::TextUnformatted( "Online" );
    ImGui::PopFont();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x + S( 22.0f ), bannerPos.y + S( 62.0f ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.86f, 0.88f, 0.91f, 1.0f ) );
    ImGui::TextUnformatted( "Your account, stored now, used when the servers arrive." );
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos( ImVec2( bannerPos.x, bannerPos.y + bannerSize.y + 16.0f ) );

    if ( CardBegin( "##online_account", ImVec2( width, S( 300.0f ) ) ) ) {
        SectionTitle( "ACCOUNT" );
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::TextUnformatted( "USERNAME" );
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputTextWithHint( "##onlineuser", "atari.com account (online play, not the nickname)",
                                  mOnlineUsername, sizeof( mOnlineUsername ) );
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::TextUnformatted( "PASSWORD" );
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputTextWithHint( "##onlinepass", "not transmitted yet",
                                  mOnlinePassword, sizeof( mOnlinePassword ),
                                  ImGuiInputTextFlags_Password );
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::TextUnformatted( "SERVER" );
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputTextWithHint( "##onlineserver", "e.g. play.opentdu2.net:4000",
                                  mOnlineServer, sizeof( mOnlineServer ) );
        ImGui::Spacing();
        if ( GhostButton( "Save account" ) ) {
            SyncSettingsFromFields();
            SaveSettings();
            SetStatus( "Online account saved to launcher.ini." );
        }
    }
    CardEnd();

    ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );

    if ( CardBegin( "##online_note", ImVec2( width, S( 168.0f ) ) ) ) {
        SectionTitle( "STATUS" );
        Chip( "NOT CONNECTED - NO NETWORKING YET", kAmber );
        ImGui::PushStyleColor( ImGuiCol_Text, kDim );
        ImGui::PushFont( mFontSmall );
        ImGui::TextWrapped(
            "The port has no network layer yet. These values are saved in launcher.ini "
            "([online] username / password / server) and passed to the game as "
            "-onlineusername and -onlineserver, so the day a server exists the account is "
            "already configured. This is the online identity only - the nickname the game "
            "uses lives on the Player page. Nothing is transmitted today - and until then, "
            "use a throwaway password." );
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    CardEnd();
}



void LauncherApp::LaunchGame() {
    SyncSettingsFromFields();
    SaveSettings();

    if ( !mGameFolderValid ) {
        SetStatus( "Choose the game data folder first (Euro\\Bnk must exist)." );
        mPage = Page::GameData;
        return;
    }
    std::wstring exePath;
    std::string exeError;
    if ( !FindGameExecutable( mLauncherDir, Utf8ToWide( mSettings.GameExe ), exePath,
                              exeError ) ) {
        SetStatus( exeError );
        mPage = Page::GameData;
        return;
    }

    std::wstring arguments = QuoteArg( exePath );
    GameLaunchOptions launchOptions;
    launchOptions.GameFolder = mSettings.GameFolder;
    launchOptions.PlayerName = mSettings.PlayerName;
    launchOptions.UserDataFolder = mSettings.UserDataFolder;
    launchOptions.OnlineUsername = mSettings.OnlineUsername;
    launchOptions.OnlineServer = mSettings.OnlineServer;
    launchOptions.bWatermark = mSettings.bWatermark;
    launchOptions.bSkipIntro = mSettings.bSkipIntro;
    arguments += BuildGameArguments( launchOptions );

    STARTUPINFOW startup = {};
    startup.cb = sizeof( startup );
    PROCESS_INFORMATION process = {};
    std::wstring mutableArguments = arguments;
    const std::wstring workingDirectory = ParentDir( exePath );
    if ( !CreateProcessW( exePath.c_str(), mutableArguments.data(), nullptr, nullptr, FALSE, 0,
                          nullptr, workingDirectory.c_str(), &startup, &process ) ) {
        SetStatus( "The game could not be started (" +
                   std::to_string( GetLastError() ) + ")." );
        return;
    }
    CloseHandle( process.hThread );
    if ( mGameProcess != nullptr ) {
        CloseHandle( mGameProcess );
    }
    mGameProcess = process.hProcess;
    mLaunching = true;
    mLaunchTime = GetTickCount64();
    SetStatus( "Game started - PID " + std::to_string( process.dwProcessId ) + "." );
    if ( mSettings.bCloseOnLaunch ) {
        mExitRequested = true;
    }
}

void LauncherApp::BrowseIso() {
    std::wstring picked;
    const wchar_t filter[] = L"Disc images\0*.iso;*.img;*.bin\0All files\0*.*\0\0";
    if ( !BrowseFile( mWindow, L"Choose the Test Drive Unlimited 2 disc image", filter,
                      picked ) ) {
        return;
    }
    std::snprintf( mIsoPath, sizeof( mIsoPath ), "%s", WideToUtf8( picked ).c_str() );
    if ( mIsoDestination[0] == '\0' ) {
        std::wstring stem = BaseName( picked );
        const size_t dot = stem.find_last_of( L'.' );
        if ( dot != std::wstring::npos ) {
            stem.resize( dot );
        }
        const std::wstring destination = JoinPath( ParentDir( picked ), stem );
        std::snprintf( mIsoDestination, sizeof( mIsoDestination ), "%s",
                       WideToUtf8( destination ).c_str() );
    }
    SyncSettingsFromFields();
    SaveSettings();
    mIsoInspected = false;
    InspectIsoFile();
}

void LauncherApp::BrowseDestination() {
    std::wstring picked;
    if ( BrowseFolder( mWindow, L"Choose a folder for the extracted game", picked ) ) {
        std::snprintf( mIsoDestination, sizeof( mIsoDestination ), "%s",
                       WideToUtf8( picked ).c_str() );
        SyncSettingsFromFields();
        SaveSettings();
    }
}

void LauncherApp::BrowseGameFolder() {
    std::wstring picked;
    if ( BrowseFolder( mWindow, L"Choose the game folder (the one with Euro\\Bnk)", picked ) ) {
        std::snprintf( mGameFolder, sizeof( mGameFolder ), "%s", WideToUtf8( picked ).c_str() );
        SyncSettingsFromFields();
        RefreshGameState();
        RefreshProfileSummary();
        SaveSettings();
        SetStatus( IsValidGameFolder( picked ) ? "Game data folder set."
                                               : "That folder has no Euro\\Bnk." );
    }
}


void LauncherApp::InspectIsoFile() {
    SyncSettingsFromFields();
    if ( mIsoPath[0] == '\0' ) {
        SetStatus( "Choose a .iso first." );
        return;
    }
    SetStatus( "Inspecting the disc image..." );
    mIsoInspection = InspectIso( Utf8ToWide( mIsoPath ) );
    mIsoInspected = true;
    SaveSettings();
    if ( !mIsoInspection.bOpened ) {
        SetStatus( "Cannot open the image: " + mIsoInspection.Error );
    } else {
        SetStatus( mIsoInspection.Note );
    }
}

void LauncherApp::ExtractIso() {
    SyncSettingsFromFields();
    if ( mInstall.bRunning.load() ) {
        return;
    }
    if ( mIsoPath[0] == '\0' || mIsoDestination[0] == '\0' ) {
        SetStatus( "Choose the disc image and the destination folder." );
        return;
    }
    if ( !mIsoInspected ) {
        InspectIsoFile();
    }
    if ( !mIsoInspection.bHasGameData ) {
        SetStatus( "This image holds no Euro\\Bnk to extract." );
        return;
    }
    if ( mInstallThread.joinable() ) {
        mInstallThread.join();
    }
    mInstall.bCancel = false;
    mInstall.bDone = false;
    mInstall.bOk = false;
    mInstall.BytesDone = 0;
    mInstall.BytesTotal = 0;
    mInstall.FilesDone = 0;
    mInstall.FilesTotal = 0;
    mInstall.FilesSkipped = 0;
    mInstall.SetError( std::string() );
    mInstall.SetCurrentFile( std::string() );
    mInstallStart = GetTickCount64();

    const std::wstring isoPath = Utf8ToWide( mIsoPath );
    const std::wstring destination = Utf8ToWide( mIsoDestination );
    const std::string gameRoot = mIsoInspection.GameRoot;
    SaveSettings();
    mInstallThread = std::thread( [this, isoPath, destination, gameRoot]() {
        ExtractGameFromIso( isoPath, gameRoot, destination, mInstall );
    } );
    SetStatus( "Extracting the game data..." );
}

void LauncherApp::StartUpdateCheck() {
    if ( mUpdate.bBusy.load() ) {
        return;
    }
    SyncSettingsFromFields();
    SaveSettings();
    if ( mUpdateThread.joinable() ) {
        mUpdateThread.join();
    }
    mUpdate.bChecked = false;
    mUpdate.bAvailable = false;
    mCheckingForUpdates = true;
    const std::string repo = mSettings.UpdateRepo;
    const std::string branch = mSettings.UpdateBranch;
    const std::string lastTag = mSettings.LastUpdateTag;
    const std::wstring launcherDir = mLauncherDir;
    mUpdateThread = std::thread( [this, repo, branch, lastTag, launcherDir]() {
        CheckForUpdates( repo, branch, lastTag, launcherDir, mUpdate );
    } );
    SetStatus( "Checking for updates..." );
}

void LauncherApp::StartAutomaticUpdateCheck() {
    if ( mSettings.bAutoCheckUpdates ) {
        StartUpdateCheck();
    }
}

void LauncherApp::ApplyUpdate() {
    SyncSettingsFromFields();
    const UpdateSnapshot update = Snapshot( mUpdate );
    if ( update.bBusy ) {
        return;
    }
    if ( update.DownloadUrl.empty() ) {
        SetStatus( "Fast-forwarding origin/" + update.Branch + "..." );
        if ( ApplyGitUpdate( mUpdate ) ) {
            const UpdateSnapshot after = Snapshot( mUpdate );
            SetStatus( after.Summary );
            RefreshGameState();
        } else {
            SetStatus( Snapshot( mUpdate ).Error );
        }
        return;
    }
    DownloadUpdate();
}

void LauncherApp::DownloadUpdate() {
    if ( mUpdate.bBusy.load() ) {
        return;
    }
    if ( mUpdateThread.joinable() ) {
        mUpdateThread.join();
    }
    mUpdateThread = std::thread( [this]() { DownloadReleasePackage( mUpdate ); } );
    SetStatus( "Downloading the update package..." );
}

void LauncherApp::StartUpdateRebuild() {
    if ( mRebuild.bRunning.load() || mUpdate.bBusy.load() ) {
        return;
    }
    if ( mGameProcess != nullptr ) {
        SetStatus( "The game is running - close it first (the rebuild relinks the exe)." );
        return;
    }
    const std::wstring repoRoot = FindRepositoryRoot( mLauncherDir );
    if ( repoRoot.empty() ) {
        SetStatus( "This launcher is not in a git checkout - the release download is the update path." );
        return;
    }
    if ( !FileExists( JoinPath( repoRoot, L"update-and-rebuild.bat" ) ) ) {
        SetStatus( "update-and-rebuild.bat is not in " + WideToUtf8( repoRoot ) + "." );
        return;
    }
    if ( mRebuildThread.joinable() ) {
        mRebuildThread.join();
    }
    mRebuild.bDone = false; // before the spawn: the stale flag must not read "finished"
    mRebuildThread = std::thread( [this, repoRoot]() { RunUpdateAndRebuild( repoRoot, mRebuild ); } );
    SetStatus( "Updating and rebuilding - the log is on the Updates page." );
}

} // namespace Launcher

