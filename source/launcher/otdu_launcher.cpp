#include "shared.h"
#include "launcher/otdu_launcher.h"

#include "core/logger.h"
#include "core/watchdog.h"
#include "render/renderer_2d.h"
#include "render/texture_cache.h"
#include "render/vulkan/render_device.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include "build_info.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// The integrated launcher (Android first): boots the window + Vulkan device +
// 2D renderer + ImGui before any game service, then either hands the window to
// the engine init (PLAY) or exits the app. The UI ports the desktop launcher's
// theme (tools/launcher/launcher_ui.cpp: dark navy panels, orange accent,
// rounded cards) at phone density, with gamepad navigation enabled.
//
// It compiles on every platform so the flow is testable on the desktop
// (-launcher), but InitAndRun only calls it on Android.

namespace {

// --- Theme (ported from tools/launcher/launcher_ui.cpp) --------------------

const ImVec4 kText = ImVec4( 0.93f, 0.94f, 0.96f, 1.00f );
const ImVec4 kDim = ImVec4( 0.60f, 0.64f, 0.70f, 1.00f );
const ImVec4 kAccent = ImVec4( 1.00f, 0.47f, 0.13f, 1.00f );
const ImVec4 kAccentDark = ImVec4( 0.72f, 0.29f, 0.06f, 1.00f );
const ImVec4 kPanel = ImVec4( 0.085f, 0.095f, 0.125f, 0.96f );
const ImVec4 kPanelBorder = ImVec4( 0.20f, 0.23f, 0.29f, 0.85f );
const ImVec4 kGreen = ImVec4( 0.36f, 0.85f, 0.47f, 1.00f );
const ImVec4 kRed = ImVec4( 1.00f, 0.40f, 0.38f, 1.00f );
const ImVec4 kAmber = ImVec4( 1.00f, 0.76f, 0.30f, 1.00f );

const ColorRGBA kClearColor = ColorRGBA( 0.055f, 0.062f, 0.082f, 1.00f );

// Layout constants are logical (96 DPI) pixels, scaled by the phone's density.
float gUiScale = 1.0f;
float S( float value ) { return value * gUiScale; }

// --- Launcher state ----------------------------------------------------------

enum class Page { Play, GameData, Settings };

struct LauncherState {
    Page page = Page::Play;
    bool bDrawerOpen = false; // portrait: the slide-out menu
    std::string gameDir;    // selected + validated game folder ('' = none)
    std::string browserDir; // the folder browser's current directory
    float dpiScale = 1.0f;
    float uiScaleOverride = 0.0f; // 0 = follow the display (density)
    bool bBrowseDirty = true;     // re-scan browserDir next time the page draws
    std::vector<std::string> browseDirs;
    std::string browseError;
};

// <root>/Euro/Bnk exists (case-insensitive; mirrors GSFile::HasBankFolder).
bool ValidateGameRoot( const std::string &root ) {
    std::error_code ec;
    const std::filesystem::path path( root );
    if ( !std::filesystem::is_directory( path, ec ) ) {
        return false;
    }
    for ( const auto &entry : std::filesystem::directory_iterator( path, ec ) ) {
        if ( entry.is_directory() &&
             ToLower( entry.path().filename().string() ) == "euro" ) {
            for ( const auto &sub :
                  std::filesystem::directory_iterator( entry.path(), ec ) ) {
                if ( sub.is_directory() &&
                     ToLower( sub.path().filename().string() ) == "bnk" ) {
                    return true;
                }
            }
        }
    }
    return false;
}

// --- Android All-Files-Access ------------------------------------------------

#if defined( OTDU_ANDROID )
#include <jni.h>

// Environment.isExternalStorageManager() through the activity (the precise
// check for MANAGE_EXTERNAL_STORAGE); a filesystem probe as the fallback.
// SDL3's getters return void* (SDL_system.h stays jni.h-free): cast here.
bool HasAllFilesAccess() {
    JNIEnv *pEnv = reinterpret_cast<JNIEnv *>( SDL_GetAndroidJNIEnv() );
    jobject activity = reinterpret_cast<jobject>( SDL_GetAndroidActivity() );
    if ( pEnv != nullptr && activity != nullptr ) {
        jclass cls = pEnv->GetObjectClass( activity );
        jmethodID mid =
            pEnv->GetMethodID( cls, "hasAllFilesAccess", "()Z" );
        const bool bHas = mid != nullptr &&
                          pEnv->CallBooleanMethod( activity, mid );
        pEnv->DeleteLocalRef( cls );
        if ( mid != nullptr ) {
            return bHas;
        }
    }
    std::error_code ec;
    std::filesystem::directory_iterator it( "/storage/emulated/0", ec );
    return !ec && it != std::filesystem::directory_iterator();
}

// MainActivity.requestAllFilesAccess(): the system "All files access" page.
void RequestAllFilesAccess() {
    JNIEnv *pEnv = reinterpret_cast<JNIEnv *>( SDL_GetAndroidJNIEnv() );
    jobject activity = reinterpret_cast<jobject>( SDL_GetAndroidActivity() );
    if ( pEnv == nullptr || activity == nullptr ) {
        return;
    }
    jclass cls = pEnv->GetObjectClass( activity );
    jmethodID mid =
        pEnv->GetMethodID( cls, "requestAllFilesAccess", "()V" );
    if ( mid != nullptr ) {
        pEnv->CallVoidMethod( activity, mid );
    }
    pEnv->DeleteLocalRef( cls );
}

// MainActivity.setLandscapeOnly(): the launcher rotates freely (phone or tablet,
// portrait or landscape); PLAY pins the game to landscape.
void SetLandscapeOnly( bool bLandscapeOnly ) {
    JNIEnv *pEnv = reinterpret_cast<JNIEnv *>( SDL_GetAndroidJNIEnv() );
    jobject activity = reinterpret_cast<jobject>( SDL_GetAndroidActivity() );
    if ( pEnv == nullptr || activity == nullptr ) {
        return;
    }
    jclass cls = pEnv->GetObjectClass( activity );
    jmethodID mid = pEnv->GetMethodID( cls, "setLandscapeOnly", "(Z)V" );
    if ( mid != nullptr ) {
        pEnv->CallVoidMethod( activity, mid, static_cast<jboolean>( bLandscapeOnly ) );
    }
    pEnv->DeleteLocalRef( cls );
}
#else
bool HasAllFilesAccess() { return true; }
void RequestAllFilesAccess() {}
void SetLandscapeOnly( bool ) {}
#endif

// --- Settings (a tiny INI under SDL_GetPrefPath) -----------------------------

std::string SettingsPath() {
    char *pPref = SDL_GetPrefPath( "OpenTDU2", "OpenTDU2" );
    if ( pPref == nullptr ) {
        return std::string();
    }
    std::string path = std::string( pPref ) + "launcher_android.ini";
    SDL_free( pPref );
    return path;
}

std::string IniValue( const std::string &content, const char *pKey ) {
    const std::string needle = std::string( pKey ) + "=";
    size_t pos = 0;
    while ( ( pos = content.find( needle, pos ) ) != std::string::npos ) {
        // Only accept it inside the [launcher] section: a value line starts
        // the match (not a substring of another key).
        if ( pos != 0 && content[pos - 1] != '\n' ) {
            pos += needle.size();
            continue;
        }
        const size_t end = content.find( '\n', pos );
        return content.substr( pos + needle.size(),
                               end == std::string::npos ? end
                                                        : end - pos - needle.size() );
    }
    return std::string();
}

void LoadSettings( LauncherState &state ) {
    const std::string path = SettingsPath();
    std::error_code ec;
    if ( path.empty() || !std::filesystem::exists( path, ec ) ) {
        return;
    }
    FILE *pFile = fopen( path.c_str(), "rb" );
    if ( pFile == nullptr ) {
        return;
    }
    std::string content;
    char buffer[512];
    size_t read = 0;
    while ( ( read = fread( buffer, 1, sizeof( buffer ), pFile ) ) > 0 ) {
        content.append( buffer, read );
    }
    fclose( pFile );

    state.gameDir = IniValue( content, "game_folder" );
    state.browserDir = IniValue( content, "browser_dir" );
    const std::string scale = IniValue( content, "ui_scale" );
    if ( !scale.empty() ) {
        state.uiScaleOverride = std::strtof( scale.c_str(), nullptr );
    }
    if ( !state.gameDir.empty() ) {
        // The folder may have vanished between runs (the OS can clean
        // shared storage): keep it only when it still validates.
        if ( !ValidateGameRoot( state.gameDir ) ) {
            OTDU_LOG_WARN( "Launcher: the saved game folder no longer holds "
                           "Euro/Bnk: '%s'\n",
                           state.gameDir.c_str() );
            state.gameDir.clear();
        }
    }
    if ( state.browserDir.empty() ) {
#if defined( OTDU_ANDROID )
        state.browserDir = "/storage/emulated/0";
#else
        // Desktop (-launcher test runs): the current drive's root.
        std::error_code ec;
        state.browserDir = std::filesystem::current_path( ec ).root_path().string();
        if ( state.browserDir.empty() ) {
            state.browserDir = "/";
        }
#endif
    }
}

void SaveSettings( const LauncherState &state ) {
    const std::string path = SettingsPath();
    if ( path.empty() ) {
        return;
    }
    FILE *pFile = fopen( path.c_str(), "wb" );
    if ( pFile == nullptr ) {
        return;
    }
    fprintf( pFile, "[launcher]\n" );
    fprintf( pFile, "game_folder=%s\n", state.gameDir.c_str() );
    fprintf( pFile, "browser_dir=%s\n", state.browserDir.c_str() );
    fprintf( pFile, "ui_scale=%f\n", state.uiScaleOverride );
    fclose( pFile );
}

// --- ImGui plumbing (mirrors GSImGui's Vulkan path) ---------------------------

void ApplyTheme( float dpiScale ) {
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
    style.ScrollbarSize = 16.0f;
    style.GrabMinSize = 12.0f;

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
    colors[ImGuiCol_Button] = ImVec4( 0.16f, 0.18f, 0.23f, 1.00f );
    colors[ImGuiCol_ButtonHovered] = ImVec4( 0.22f, 0.25f, 0.32f, 1.00f );
    colors[ImGuiCol_ButtonActive] = ImVec4( 0.12f, 0.14f, 0.18f, 1.00f );
    colors[ImGuiCol_Header] = ImVec4( 0.16f, 0.17f, 0.21f, 1.00f );
    colors[ImGuiCol_HeaderHovered] = ImVec4( 0.17f, 0.19f, 0.24f, 1.00f );
    colors[ImGuiCol_HeaderActive] = ImVec4( 0.20f, 0.22f, 0.28f, 1.00f );
    colors[ImGuiCol_NavCursor] = kAccent;
    colors[ImGuiCol_Separator] = kPanelBorder;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentDark;
    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_ScrollbarBg] = ImVec4( 0.07f, 0.08f, 0.10f, 0.60f );
    colors[ImGuiCol_ScrollbarGrab] = ImVec4( 0.24f, 0.27f, 0.33f, 1.00f );
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4( 0.30f, 0.34f, 0.41f, 1.00f );
    colors[ImGuiCol_ScrollbarGrabActive] = kAccent;
    colors[ImGuiCol_TextSelectedBg] =
        ImVec4( kAccent.x, kAccent.y, kAccent.z, 0.35f );

    // The theme is written in logical pixels; the phone's density scales it.
    style.ScaleAllSizes( dpiScale );
    style.FontScaleMain = dpiScale;
}

// Loads a scalable TTF (Roboto ships with the OS). Without one the embedded
// bitmap font stays at 13 px - functional but tiny on a phone.
// Font sizes: PushFont() takes LOGICAL pixels - style.FontScaleMain (set to
// the display density in ApplyTheme) scales them; the S() helper must not be
// applied to font sizes (it would square the density).
ImFont *LoadFont( float dpiScale ) {
    static const char *const kFontCandidates[] = {
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/Roboto-Medium.ttf",
        "/system/fonts/NotoSans-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    const char *pFontPath = nullptr;
    std::error_code ec;
    for ( const char *pCandidate : kFontCandidates ) {
        if ( std::filesystem::exists( pCandidate, ec ) ) {
            pFontPath = pCandidate;
            break;
        }
    }
    if ( pFontPath == nullptr ) {
        OTDU_LOG_WARN( "Launcher: no scalable TTF found, text stays at the "
                       "bitmap font's size\n" );
        return nullptr;
    }
    ImFont *pFont =
        ImGui::GetIO().Fonts->AddFontFromFileTTF( pFontPath, 16.0f );
    if ( pFont != nullptr ) {
        ImGui::GetIO().FontDefault = pFont;
        OTDU_LOG_INFO( "Launcher: font '%s' (16 logical px, density %.2f)\n",
                       pFontPath, dpiScale );
    }
    return pFont;
}

void UpdateImGuiTexture( RenderDevice &device, TextureCache &cache,
                         ImTextureData *pTexture ) {
    if ( pTexture->Status == ImTextureStatus_WantDestroy ) {
        if ( pTexture->TexID != ImTextureID_Invalid ) {
            device.destroyTexture( reinterpret_cast<GPUTexture *>( pTexture->TexID ) );
            pTexture->SetTexID( ImTextureID_Invalid );
        }
        pTexture->SetStatus( ImTextureStatus_Destroyed );
        return;
    }
    if ( pTexture->Status != ImTextureStatus_WantCreate &&
         pTexture->Status != ImTextureStatus_WantUpdates ) {
        return;
    }
    // Simple path (like GSImGui::updateTexture): recreate the whole texture on
    // any change.
    if ( pTexture->TexID != ImTextureID_Invalid ) {
        device.destroyTexture( reinterpret_cast<GPUTexture *>( pTexture->TexID ) );
    }
    std::vector<uint8_t> rgba( static_cast<size_t>( pTexture->Width ) *
                               pTexture->Height * 4 );
    const uint8_t *pPixels = static_cast<const uint8_t *>( pTexture->GetPixels() );
    if ( pTexture->Format == ImTextureFormat_Alpha8 ) {
        for ( size_t i = 0;
              i < static_cast<size_t>( pTexture->Width ) * pTexture->Height;
              i++ ) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = pPixels[i];
        }
    } else {
        memcpy( rgba.data(), pPixels, rgba.size() );
    }
    GPUTexture *pGPUTexture = cache.createFromRGBA(
        static_cast<uint32_t>( pTexture->Width ),
        static_cast<uint32_t>( pTexture->Height ), rgba.data(), "LauncherImGui" );
    pTexture->SetTexID( reinterpret_cast<ImTextureID>( pGPUTexture ) );
    pTexture->SetStatus( ImTextureStatus_OK );
}

// --- UI widgets --------------------------------------------------------------

// A rounded card (the desktop launcher's CardBegin/End).
bool CardBegin( const char *pId, const ImVec2 &size, bool bScroll = false ) {
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
    ImGuiChildFlags childFlags = ImGuiChildFlags_Borders;
    if ( size.y <= 0.0f && !bScroll ) {
        childFlags |= ImGuiChildFlags_AutoResizeY;
    }
    return ImGui::BeginChild( pId, size, childFlags, flags );
}

void CardEnd() {
    ImGui::EndChild();
    ImGui::PopStyleVar( 3 );
    ImGui::PopStyleColor( 2 );
}

// Sidebar navigation item: an invisible button over a drawn accent bar, so
// touch taps, gamepad activation and the focus rectangle all work (the desktop
// launcher's NavItem, trimmed).
bool NavItem( const char *pLabel, bool bActive, float height ) {
    ImGui::PushID( pLabel );
    const ImVec2 size = ImVec2( ImGui::GetContentRegionAvail().x, height );
    const bool bPressed = ImGui::InvisibleButton( "##nav", size );
    const ImVec2 pos = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList *pDraw = ImGui::GetWindowDrawList();
    const ImU32 fill = bActive ? ImGui::ColorConvertFloat4ToU32( ImVec4( kAccent.x, kAccent.y, kAccent.z, 0.16f ) )
                               : ( ImGui::IsItemHovered()
                                       ? ImGui::ColorConvertFloat4ToU32( ImVec4( 1, 1, 1, 0.05f ) )
                                       : 0 );
    if ( fill != 0 ) {
        pDraw->AddRectFilled( pos, max, fill, S( 8.0f ) );
    }
    if ( bActive ) {
        pDraw->AddRectFilled( pos, ImVec2( pos.x + S( 3.0f ), max.y ),
                              ImGui::ColorConvertFloat4ToU32( kAccent ), 0.0f );
    }
    if ( ImGui::IsItemFocused() ) {
        pDraw->AddRect( pos, max, ImGui::ColorConvertFloat4ToU32( kAccent ),
                        S( 8.0f ), 0, S( 2.0f ) );
    }
    pDraw->AddText( ImVec2( pos.x + S( 14.0f ), pos.y + ( height - ImGui::GetFontSize() ) * 0.5f ),
                    ImGui::ColorConvertFloat4ToU32( bActive ? kText : kDim ),
                    pLabel );
    ImGui::PopID();
    return bPressed;
}

// The PLAY button: big enough for a thumb, orange when it can launch.
bool PlayButton( const char *pLabel, bool bEnabled, float width, float height ) {
    if ( bEnabled ) {
        ImGui::PushStyleColor( ImGuiCol_Button, kAccent );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 1.00f, 0.58f, 0.28f, 1.00f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, kAccentDark );
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.05f, 0.05f, 0.05f, 1.00f ) );
    } else {
        ImGui::BeginDisabled();
    }
    const bool bPressed = ImGui::Button( pLabel, ImVec2( width, height ) );
    if ( bEnabled ) {
        ImGui::PopStyleColor( 4 );
    } else {
        ImGui::EndDisabled();
    }
    return bPressed && bEnabled;
}

// --- Pages -------------------------------------------------------------------

void RefreshBrowse( LauncherState &state ) {
    state.browseDirs.clear();
    state.browseError.clear();
    std::error_code ec;
    for ( const auto &entry :
          std::filesystem::directory_iterator( state.browserDir, ec ) ) {
        if ( entry.is_directory( ec ) ) {
            state.browseDirs.push_back( entry.path().string() );
        }
    }
    if ( ec ) {
        state.browseError = "This folder cannot be read (storage access?)";
    }
    std::sort( state.browseDirs.begin(), state.browseDirs.end(),
               []( const std::string &a, const std::string &b ) {
                   return ToLower( a ) < ToLower( b );
               } );
    state.bBrowseDirty = false;
}

void DrawPlayPage( LauncherState &state, bool *pbPlay ) {
    const bool bReady = !state.gameDir.empty();
    ImGui::Spacing();
    ImGui::PushFont( ImGui::GetIO().FontDefault, 40.0f );
    ImGui::TextColored( kText, "OpenTDU2" );
    ImGui::PopFont();
    ImGui::TextColored( kDim, "Test Drive Unlimited 2 source port - %s",
                        OTDU_BUILD_FULL_STRING );
    ImGui::Spacing();
    ImGui::Spacing();

    if ( CardBegin( "##play_status", ImVec2( 0, 0 ) ) ) {
        if ( bReady ) {
            ImGui::TextColored( kGreen, "GAME DATA READY" );
            ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
            ImGui::TextWrapped( "%s", state.gameDir.c_str() );
            ImGui::PopFont();
        } else {
            ImGui::TextColored( kRed, "NO GAME DATA" );
            ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
            ImGui::TextWrapped(
                "Point the launcher at the unpacked TDU2 folder (the one with "
                "Euro/Bnk in it) on the GAME DATA page." );
            ImGui::PopFont();
        }
    }
    CardEnd();

    ImGui::Spacing();
    if ( PlayButton( "PLAY", bReady, S( 260.0f ), S( 62.0f ) ) ) {
        *pbPlay = true;
    }
    if ( ImGui::GetContentRegionAvail().x > S( 560.0f ) ) {
        ImGui::SameLine();
        if ( !bReady ) {
            ImGui::SetCursorPosY( ImGui::GetCursorPosY() + S( 18.0f ) );
        }
    } else if ( !bReady ) {
        ImGui::Spacing();
    }
    if ( !bReady ) {
        if ( ImGui::Button( "Choose game folder >", ImVec2( 0, S( 46.0f ) ) ) ) {
            state.page = Page::GameData;
        }
    }
}

void DrawGameDataPage( LauncherState &state ) {
    const bool bAccess = HasAllFilesAccess();
    const bool bValid = ValidateGameRoot( state.browserDir );

    if ( CardBegin( "##gamedata_current", ImVec2( 0, 0 ) ) ) {
        ImGui::TextColored( kDim, "Selected folder" );
        if ( !state.gameDir.empty() ) {
            ImGui::TextColored( kGreen, "%s", state.gameDir.c_str() );
        } else {
            ImGui::TextColored( kRed, "none" );
        }
    }
    CardEnd();
    ImGui::Spacing();

    if ( !bAccess ) {
        if ( CardBegin( "##gamedata_access", ImVec2( 0, 0 ) ) ) {
            ImGui::TextColored( kAmber, "Storage access missing" );
            ImGui::TextWrapped(
                "OpenTDU2 needs All-Files-Access to read the game data you "
                "copied over USB. Grant it once, then come back." );
            if ( ImGui::Button( "Open the storage settings", ImVec2( 0, S( 44.0f ) ) ) ) {
                RequestAllFilesAccess();
            }
        }
        CardEnd();
        return;
    }

    // Breadcrumb + browser.
    ImGui::TextColored( kDim, "%s", state.browserDir.c_str() );
    if ( CardBegin( "##gamedata_browser", ImVec2( 0, -S( 58.0f ) ), true ) ) {
        if ( state.bBrowseDirty ) {
            RefreshBrowse( state );
        }
        if ( !state.browseError.empty() ) {
            ImGui::TextColored( kRed, "%s", state.browseError.c_str() );
        }
        // "..": one level up (not at the FS root).
        if ( state.browserDir != "/" ) {
            std::error_code ec;
            std::filesystem::path parent =
                std::filesystem::path( state.browserDir ).parent_path();
            if ( parent.empty() ) {
                parent = "/";
            }
            if ( ImGui::Selectable( ".. (up one level)", false,
                                    ImGuiSelectableFlags_AllowOverlap,
                                    ImVec2( 0, S( 34.0f ) ) ) ) {
                state.browserDir = parent.string();
                state.bBrowseDirty = true;
            }
        }
        for ( const std::string &dir : state.browseDirs ) {
            const char *pDir = dir.c_str();
            const std::string name =
                std::filesystem::path( dir ).filename().string();
            ImGui::PushID( pDir );
            if ( ImGui::Selectable( name.c_str(), false,
                                    ImGuiSelectableFlags_AllowOverlap,
                                    ImVec2( 0, S( 34.0f ) ) ) ) {
                state.browserDir = dir;
                state.bBrowseDirty = true;
            }
            ImGui::PopID();
        }
        if ( state.browseDirs.empty() && state.browseError.empty() ) {
            ImGui::TextColored( kDim, "(no sub-folders)" );
        }
    }
    CardEnd();

    ImGui::Spacing();
    ImGui::BeginDisabled( !bValid );
    if ( PlayButton( "Use this folder", bValid, S( 220.0f ), S( 46.0f ) ) ) {
        state.gameDir = state.browserDir;
        SaveSettings( state );
        state.page = Page::Play;
    }
    ImGui::EndDisabled();
    if ( !bValid ) {
        ImGui::SameLine();
        ImGui::TextColored( kDim, "needs Euro/Bnk inside" );
    }
}

void DrawSettingsPage( LauncherState &state ) {
    if ( CardBegin( "##settings_scale", ImVec2( 0, 0 ) ) ) {
        ImGui::TextColored( kDim, "Interface scale" );
        float scale =
            state.uiScaleOverride > 0.0f ? state.uiScaleOverride : state.dpiScale;
        if ( ImGui::SliderFloat( "##uiscale", &scale, 1.0f, 4.0f, "%.2f" ) ) {
            state.uiScaleOverride = scale;
            gUiScale = scale;
            ApplyTheme( scale );
        }
        ImGui::SameLine();
        ImGui::TextColored( kDim, "%.0f%%",
                            ( state.uiScaleOverride > 0.0f
                                  ? state.uiScaleOverride
                                  : state.dpiScale ) * 100.0f );
        if ( ImGui::Button( "Reset to display density" ) ) {
            state.uiScaleOverride = 0.0f;
            gUiScale = state.dpiScale;
            ApplyTheme( state.dpiScale );
        }
    }
    CardEnd();
    ImGui::Spacing();

    if ( CardBegin( "##settings_storage", ImVec2( 0, 0 ) ) ) {
        ImGui::TextColored( kDim, "Storage" );
        if ( HasAllFilesAccess() ) {
            ImGui::TextColored( kGreen, "All-Files-Access granted" );
        } else {
            ImGui::TextColored( kAmber, "All-Files-Access missing" );
            if ( ImGui::Button( "Open the storage settings", ImVec2( 0, S( 44.0f ) ) ) ) {
                RequestAllFilesAccess();
            }
        }
    }
    CardEnd();
    ImGui::Spacing();

    if ( CardBegin( "##settings_help", ImVec2( 0, 0 ) ) ) {
        ImGui::TextColored( kDim, "Installing the game data" );
        ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
        ImGui::TextWrapped(
            "1. Copy the unpacked TDU2 folder (Euro/Bnk/... plus the .cpr "
            "config files) from the PC to the phone once over USB, e.g. to "
            "/storage/emulated/0/TDU2.\n"
            "2. GAME DATA: browse to it and tap 'Use this folder'.\n"
            "3. PLAY starts the game (title screen first)." );
        ImGui::PopFont();
    }
    CardEnd();
}

// --- Root window (sidebar + page + footer) -----------------------------------

void DrawRoot( LauncherState &state, bool *pbPlay ) {
    const ImGuiViewport *pViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( pViewport->Pos );
    ImGui::SetNextWindowSize( pViewport->Size );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( S( 10.0f ), S( 10.0f ) ) );
    ImGui::Begin( "##otdu2_launcher_root", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoBringToFrontOnFocus |
                      ImGuiWindowFlags_NoNav );
    ImGui::PopStyleVar( 3 );

    // Portrait (a phone held upright): the pages get the full width and the
    // three entries become a tab row on top; landscape keeps the sidebar.
    const bool bPortrait = pViewport->Size.x < pViewport->Size.y;
    const float sidebarWidth = Min( S( 210.0f ), pViewport->Size.x * 0.3f );

    if ( bPortrait ) {
        const char *titles[3] = { "PLAY", "GAME DATA", "SETTINGS" };
        const Page pages[3] = { Page::Play, Page::GameData, Page::Settings };
        const float barH = S( 52.0f );
        // Top bar: the hamburger button (left) + the current page's name.
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.10f, 0.11f, 0.15f, 1.0f ) );
        if ( ImGui::Button( "##burger", ImVec2( barH, barH ) ) ) {
            state.bDrawerOpen = !state.bDrawerOpen;
        }
        ImGui::PopStyleColor();
        {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const float w = barH * 0.46f, x0 = mn.x + ( barH - w ) * 0.5f;
            ImDrawList *pDraw = ImGui::GetWindowDrawList();
            const ImU32 col = ImGui::ColorConvertFloat4ToU32( kText );
            for ( int i = 0; i < 3; i++ ) {
                const float y = mn.y + barH * 0.30f + i * barH * 0.20f;
                pDraw->AddLine( ImVec2( x0, y ), ImVec2( x0 + w, y ), col, S( 3.0f ) );
            }
        }
        ImGui::SameLine();
        ImGui::SetCursorPosY( ImGui::GetCursorPosY() + ( barH - ImGui::GetFontSize() ) * 0.5f );
        ImGui::TextColored( kAccent, "%s", titles[static_cast<int>( state.page ) < 3 ? static_cast<int>( state.page ) : 0] );
        ImGui::BeginChild( "##main", ImVec2( 0, 0 ) );
        switch ( state.page ) {
        case Page::Play:
            DrawPlayPage( state, pbPlay );
            break;
        case Page::GameData:
            DrawGameDataPage( state );
            break;
        case Page::Settings:
            DrawSettingsPage( state );
            break;
        }
        ImGui::EndChild();
        ImGui::End();

        // The drawer: the sidebar, sliding over the left of the page. A tap on
        // the dimmed area (or the hamburger again) closes it.
        if ( state.bDrawerOpen ) {
            ImDrawList *pFg = ImGui::GetForegroundDrawList();
            pFg->AddRectFilled( pViewport->Pos,
                                ImVec2( pViewport->Pos.x + pViewport->Size.x, pViewport->Pos.y + pViewport->Size.y ),
                                IM_COL32( 0, 0, 0, 120 ) );
            const float drawerW = Min( S( 280.0f ), pViewport->Size.x * 0.8f );
            ImGui::SetNextWindowPos( pViewport->Pos );
            ImGui::SetNextWindowSize( ImVec2( drawerW, pViewport->Size.y ) );
            ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.06f, 0.07f, 0.095f, 1.0f ) );
            ImGui::Begin( "##drawer", nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav );
            ImGui::PopStyleColor();
            ImGui::TextColored( kAccent, "OpenTDU2" );
            ImGui::Separator();
            ImGui::Spacing();
            for ( int i = 0; i < 3; i++ ) {
                if ( NavItem( titles[i], state.page == pages[i], S( 52.0f ) ) ) {
                    state.page = pages[i];
                    state.bDrawerOpen = false;
                }
            }
            ImGui::PushFont( ImGui::GetIO().FontDefault, 11.0f );
            ImGui::TextWrapped( "%s", OTDU_BUILD_STRING );
            ImGui::PopFont();
            ImGui::End();
            // Dimmed area beside the drawer: a tap closes it.
            ImGui::SetNextWindowPos( ImVec2( pViewport->Pos.x + drawerW, pViewport->Pos.y ) );
            ImGui::SetNextWindowSize( ImVec2( pViewport->Size.x - drawerW, pViewport->Size.y ) );
            ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0, 0, 0, 0 ) );
            ImGui::Begin( "##drawerscrim", nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav );
            ImGui::PopStyleColor();
            if ( ImGui::InvisibleButton( "##scrim", ImGui::GetContentRegionAvail() ) ) {
                state.bDrawerOpen = false;
            }
            ImGui::End();
        }
        return;
    }

    // Sidebar.
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.06f, 0.07f, 0.095f, 1.00f ) );
    ImGui::BeginChild( "##sidebar", ImVec2( sidebarWidth, 0 ),
                       ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar );
    ImGui::PushFont( ImGui::GetIO().FontDefault, 15.0f );
    ImGui::TextColored( kAccent, "OpenTDU2" );
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if ( NavItem( "PLAY", state.page == Page::Play, S( 44.0f ) ) ) {
        state.page = Page::Play;
    }
    if ( NavItem( "GAME DATA", state.page == Page::GameData, S( 44.0f ) ) ) {
        state.page = Page::GameData;
    }
    if ( NavItem( "SETTINGS", state.page == Page::Settings, S( 44.0f ) ) ) {
        state.page = Page::Settings;
    }
    ImGui::SetCursorPosY( ImGui::GetCursorPosY() + S( 8.0f ) );
    ImGui::PushFont( ImGui::GetIO().FontDefault, 11.0f );
    ImGui::TextWrapped( "%s", OTDU_BUILD_STRING );
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::BeginChild( "##main", ImVec2( 0, 0 ) );
    switch ( state.page ) {
    case Page::Play:
        DrawPlayPage( state, pbPlay );
        break;
    case Page::GameData:
        DrawGameDataPage( state );
        break;
    case Page::Settings:
        DrawSettingsPage( state );
        break;
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace

namespace OTDULauncher {

bool Run( SDL_Window **ppOutWindow ) {
    LauncherState state;
    LoadSettings( state );

    // SDL + Vulkan + the window. The engine's createWindow() repeats the SDL
    // init (harmless) but skips its window when we hand one over.
    if ( !SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS ) ) {
        OTDU_LOG_ERROR( "Launcher: SDL_Init failed: %s\n", SDL_GetError() );
        return false;
    }
    if ( !SDL_Vulkan_LoadLibrary( nullptr ) ) {
        OTDU_LOG_ERROR( "Launcher: Vulkan failed to load: %s\n", SDL_GetError() );
        return false;
    }

    char title[256];
    snprintf( title, sizeof( title ), "OpenTDU2 - Test Drive Unlimited 2 - %s",
              OTDU_BUILD_FULL_STRING );
    SDL_Window *pWindow = SDL_CreateWindow(
        title, 1600, 900,
        SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY |
            SDL_WINDOW_FULLSCREEN );
    if ( pWindow == nullptr ) {
        OTDU_LOG_ERROR( "Launcher: SDL_CreateWindow failed: %s\n", SDL_GetError() );
        return false;
    }

    SetLandscapeOnly( false ); // the launcher follows the device orientation
    state.dpiScale = SDL_GetWindowDisplayScale( pWindow );
    if ( state.dpiScale <= 0.0f ) {
        state.dpiScale = 1.0f;
    }
    gUiScale = state.uiScaleOverride > 0.0f ? state.uiScaleOverride : state.dpiScale;

    // The renderer pieces the bootstrap needs - no game data involved.
    RenderDevice device;
    device.setRayTracingEnabled( false );
    if ( !device.create( pWindow, /*bVSync*/ true, /*bValidation*/ false ) ) {
        OTDU_LOG_ERROR( "Launcher: the Vulkan device failed to create\n" );
        SDL_DestroyWindow( pWindow );
        return false;
    }
    TextureCache textureCache;
    textureCache.initialize( &device );
    Renderer2D renderer2D;
    if ( !renderer2D.initialize( &device, textureCache.getWhiteTexture() ) ) {
        OTDU_LOG_ERROR( "Launcher: the 2D renderer failed to initialize\n" );
        device.destroy();
        SDL_DestroyWindow( pWindow );
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    // Touch taps arrive as mouse (SDL synthesizes them); the gamepad drives the
    // nav so the Gamesir can run the whole launcher.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard |
                      ImGuiConfigFlags_NavEnableGamepad;
    ApplyTheme( gUiScale );
    ImGui_ImplSDL3_InitForVulkan( pWindow );
    LoadFont( gUiScale );

    OTDU_LOG_INFO( "Launcher: bootstrap ready (density %.2f, scale %.2f)\n",
                   state.dpiScale, gUiScale );

    bool bPlay = false;
    bool bQuit = false;
    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>( SDL_GetPerformanceFrequency() );
    uint64_t frameIndex = 0;

    while ( !bQuit && !bPlay ) {
        Watchdog::Beat( "launcher", frameIndex );

        SDL_Event event;
        while ( SDL_PollEvent( &event ) ) {
            ImGui_ImplSDL3_ProcessEvent( &event );
            if ( event.type == SDL_EVENT_QUIT ||
                 event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                 event.type == SDL_EVENT_TERMINATING ) {
                bQuit = true;
            } else if ( event.type == SDL_EVENT_WINDOW_RESIZED ||
                        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ) {
                device.requestSwapchainRecreation();
            }
        }
        if ( device.hasDeviceLost() ) {
            OTDU_LOG_ERROR( "Launcher: the device was lost\n" );
            break;
        }

        const uint64_t counter = SDL_GetPerformanceCounter();
        const float deltaTime =
            Clamp( static_cast<float>( ( counter - lastCounter ) / frequency ),
                   0.0001f, 0.1f );
        lastCounter = counter;

        ImGui_ImplSDL3_NewFrame();
        io.DeltaTime = deltaTime;
        ImGui::NewFrame();
        DrawRoot( state, &bPlay );
        ImGui::Render();
        ImDrawData *pDrawData = ImGui::GetDrawData();

        if ( device.beginFrame( kClearColor ) ) {
            device.beginMainPass();
            if ( pDrawData->Textures != nullptr ) {
                for ( ImTextureData *pTexture : *pDrawData->Textures ) {
                    if ( pTexture->Status != ImTextureStatus_OK ) {
                        UpdateImGuiTexture( device, textureCache, pTexture );
                    }
                }
            }
            renderer2D.drawImGui( pDrawData, nullptr );
            device.endFrame();
        }
        frameIndex++;
    }

    SaveSettings( state );

    // Tear down: ImGui textures first (they own device objects), then the
    // backend + context, the renderer, and the device. The window survives on
    // PLAY - the engine init adopts it.
    ImGui::Render(); // flush any pending texture destroys
    ImDrawData *pDrawData = ImGui::GetDrawData();
    if ( pDrawData != nullptr && pDrawData->Textures != nullptr ) {
        for ( ImTextureData *pTexture : *pDrawData->Textures ) {
            if ( pTexture->TexID != ImTextureID_Invalid ) {
                device.destroyTexture(
                    reinterpret_cast<GPUTexture *>( pTexture->TexID ) );
                pTexture->SetTexID( ImTextureID_Invalid );
                pTexture->SetStatus( ImTextureStatus_Destroyed );
            }
        }
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    renderer2D.destroy();
    textureCache.destroy();
    device.waitIdle();
    device.destroy();

    if ( bPlay && !state.gameDir.empty() ) {
        OTDU_LOG_INFO( "Launcher: PLAY with game folder '%s'\n",
                       state.gameDir.c_str() );
        SDL_setenv_unsafe( "OTDU2_GAME_DIR", state.gameDir.c_str(), 1 );
        SetLandscapeOnly( true ); // the game is landscape only
        *ppOutWindow = pWindow;
        return true;
    }

    OTDU_LOG_INFO( "Launcher: exit (%s)\n",
                   bPlay ? "no game folder" : "user closed the app" );
    SDL_DestroyWindow( pWindow );
    SDL_Quit();
    return false;
}

bool HasBankFolder( const std::string &root ) {
    return ValidateGameRoot( root );
}

} // namespace OTDULauncher
