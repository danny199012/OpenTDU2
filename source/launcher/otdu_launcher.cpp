#include "shared.h"
#include "launcher/otdu_launcher.h"
#include "launcher/otdu_launcher_data.h"
#include "system/cmd_line.h"

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

enum class Page { Play, Profiles, Mods, GameData, Updates, Online, Settings };
enum class UpdateStage { Idle, Checking, Available, Downloading, Downloaded, UpToDate, Failed };

// GitHub repository the Android updater reads releases from (the owner's fork).
constexpr const char *kRepoSlug = "danny199012/OpenTDU2";

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

    // user data + profiles + mods
    std::string userData;
    std::string profileId; // selected profile ("*main*" = the legacy main save)
    std::vector<OTDULauncher::ProfileInfo> profiles;
    bool bProfilesDirty = true;
    std::string profileMessage;
    std::string renameTarget, removeTarget;
    char renameBuffer[64] = {};
    char newNameBuffer[64] = {};
    std::vector<OTDULauncher::ModInfo> mods;
    bool bModsDirty = true;

    // launch options + online (saved, passed to the game)
    bool bSkipIntro = true;
    bool bWatermark = true;
    bool bShowFps = true;
    int quality = 0; // 0 Phone, 1 Low, 2 Medium, 3 High
    char onlineUser[64] = {};
    char onlineServer[128] = {};

    // updates
    UpdateStage updateStage = UpdateStage::Idle;
    std::string updateMessage, updateUrl, updateAsset;
    int updateBuild = 0;
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

// --- Java helpers for the Updates page and the user data folder ---------------
static jobject ActivityObj() { return reinterpret_cast<jobject>( SDL_GetAndroidActivity() ); }
static JNIEnv *Env() { return reinterpret_cast<JNIEnv *>( SDL_GetAndroidJNIEnv() ); }

int JavaIntCall( const char *pName, const char *pSig ) {
    JNIEnv *pEnv = Env();
    jobject activity = ActivityObj();
    if ( pEnv == nullptr || activity == nullptr ) {
        return 0;
    }
    jclass cls = pEnv->GetObjectClass( activity );
    jmethodID mid = pEnv->GetMethodID( cls, pName, pSig );
    jint value = 0;
    if ( mid != nullptr ) {
        value = pEnv->CallIntMethod( activity, mid );
    }
    pEnv->DeleteLocalRef( cls );
    return value;
}
long long JavaLongCall( const char *pName ) {
    JNIEnv *pEnv = Env();
    jobject activity = ActivityObj();
    if ( pEnv == nullptr || activity == nullptr ) {
        return 0;
    }
    jclass cls = pEnv->GetObjectClass( activity );
    jmethodID mid = pEnv->GetMethodID( cls, pName, "()J" );
    jlong value = 0;
    if ( mid != nullptr ) {
        value = pEnv->CallLongMethod( activity, mid );
    }
    pEnv->DeleteLocalRef( cls );
    return value;
}
std::string JavaStringCall( const char *pName ) {
    JNIEnv *pEnv = Env();
    jobject activity = ActivityObj();
    std::string out;
    if ( pEnv == nullptr || activity == nullptr ) {
        return out;
    }
    jclass cls = pEnv->GetObjectClass( activity );
    jmethodID mid = pEnv->GetMethodID( cls, pName, "()Ljava/lang/String;" );
    if ( mid != nullptr ) {
        jstring text = static_cast<jstring>( pEnv->CallObjectMethod( activity, mid ) );
        if ( text != nullptr ) {
            const char *pChars = pEnv->GetStringUTFChars( text, nullptr );
            if ( pChars != nullptr ) {
                out = pChars;
                pEnv->ReleaseStringUTFChars( text, pChars );
            }
            pEnv->DeleteLocalRef( text );
        }
    }
    pEnv->DeleteLocalRef( cls );
    return out;
}
void JavaStringArgCall( const char *pName, const std::string &arg ) {
    JNIEnv *pEnv = Env();
    jobject activity = ActivityObj();
    if ( pEnv == nullptr || activity == nullptr ) {
        return;
    }
    jclass cls = pEnv->GetObjectClass( activity );
    jmethodID mid = pEnv->GetMethodID( cls, pName, "(Ljava/lang/String;)V" );
    if ( mid != nullptr ) {
        jstring text = pEnv->NewStringUTF( arg.c_str() );
        pEnv->CallVoidMethod( activity, mid, text );
        pEnv->DeleteLocalRef( text );
    }
    pEnv->DeleteLocalRef( cls );
}
int JavaHttpState() { return JavaIntCall( "getHttpState", "()I" ); }
std::string JavaHttpResult() { return JavaStringCall( "getHttpResult" ); }
long long JavaHttpDone() { return JavaLongCall( "getHttpDone" ); }
long long JavaHttpTotal() { return JavaLongCall( "getHttpTotal" ); }
void JavaStartGet( const std::string &url ) { JavaStringArgCall( "startHttpGet", url ); }
void JavaStartDownload( const std::string &url ) { JavaStringArgCall( "startDownload", url ); }
bool JavaInstallApk() {
    JNIEnv *pEnv = Env();
    jobject activity = ActivityObj();
    if ( pEnv == nullptr || activity == nullptr ) {
        return false;
    }
    jclass cls = pEnv->GetObjectClass( activity );
    jmethodID mid = pEnv->GetMethodID( cls, "installDownloadedApk", "()Z" );
    const bool bOk = mid != nullptr && pEnv->CallBooleanMethod( activity, mid );
    pEnv->DeleteLocalRef( cls );
    return bOk;
}
std::string PlatformUserData() { return JavaStringCall( "getExternalFilesPath" ); }
#else
bool HasAllFilesAccess() { return true; }
void RequestAllFilesAccess() {}
void SetLandscapeOnly( bool ) {}
int JavaHttpState() { return 0; }
std::string JavaHttpResult() { return std::string(); }
long long JavaHttpDone() { return 0; }
long long JavaHttpTotal() { return 0; }
void JavaStartGet( const std::string & ) {}
void JavaStartDownload( const std::string & ) {}
bool JavaInstallApk() { return false; }
std::string PlatformUserData() { return std::string(); }
#endif

// The user data folder (saves, profiles, mods, backups): the app's external
// files folder on Android (reachable over USB, no permission), the SDL pref
// path on desktop. Always ends without a separator.
std::string UserDataDir() {
    std::string dir = PlatformUserData();
    if ( dir.empty() ) {
        char *pPref = SDL_GetPrefPath( "OpenTDU2", "OpenTDU2" );
        if ( pPref != nullptr ) {
            dir = pPref;
            SDL_free( pPref );
        }
    }
    while ( !dir.empty() && ( dir.back() == '/' || dir.back() == '\\' ) ) {
        dir.pop_back();
    }
    return dir;
}

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
    state.profileId = IniValue( content, "profile" );
    if ( !IniValue( content, "skip_intro" ).empty() ) {
        state.bSkipIntro = IniValue( content, "skip_intro" ) == "1";
    }
    if ( !IniValue( content, "quality" ).empty() ) {
        state.quality = std::max( 0, std::min( 3, atoi( IniValue( content, "quality" ).c_str() ) ) );
    }
    if ( !IniValue( content, "show_fps" ).empty() ) {
        state.bShowFps = IniValue( content, "show_fps" ) == "1";
    }
    if ( !IniValue( content, "watermark" ).empty() ) {
        state.bWatermark = IniValue( content, "watermark" ) == "1";
    }
    snprintf( state.onlineUser, sizeof( state.onlineUser ), "%s", IniValue( content, "online_user" ).c_str() );
    snprintf( state.onlineServer, sizeof( state.onlineServer ), "%s", IniValue( content, "online_server" ).c_str() );
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
    fprintf( pFile, "profile=%s\n", state.profileId.c_str() );
    fprintf( pFile, "skip_intro=%d\n", state.bSkipIntro ? 1 : 0 );
    fprintf( pFile, "watermark=%d\n", state.bWatermark ? 1 : 0 );
    fprintf( pFile, "show_fps=%d\n", state.bShowFps ? 1 : 0 );
    fprintf( pFile, "quality=%d\n", state.quality );
    fprintf( pFile, "online_user=%s\n", state.onlineUser );
    fprintf( pFile, "online_server=%s\n", state.onlineServer );
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

const OTDULauncher::ProfileInfo *ActiveProfile( LauncherState &state );

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
    const OTDULauncher::ProfileInfo *pProfile = ActiveProfile( state );
    if ( CardBegin( "##play_profile", ImVec2( 0, 0 ) ) ) {
        ImGui::TextColored( kDim, "PLAYING AS" );
        if ( pProfile != nullptr ) {
            ImGui::TextColored( kText, "%s", pProfile->Name.c_str() );
            if ( pProfile->bHasSave ) {
                ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
                ImGui::TextColored( kDim, "%s - $%lld", OTDULauncher::StoryStepLabel( pProfile->StoryStep ).c_str(),
                                    static_cast<long long>( pProfile->Money ) );
                ImGui::PopFont();
            }
        } else {
            ImGui::TextColored( kDim, "No profile yet - a new game starts" );
        }
        if ( ImGui::Button( "Manage profiles", ImVec2( 0, S( 40.0f ) ) ) ) {
            state.page = Page::Profiles;
        }
        ImGui::TextColored( kDim, "GRAPHICS" );
        const char *kQuality[4] = { "Phone (fastest)", "Low", "Medium", "High (PC look)" };
        for ( int q = 0; q < 4; q++ ) {
            if ( ImGui::RadioButton( kQuality[q], state.quality == q ) ) {
                state.quality = q;
            }
        }
        ImGui::Checkbox( "Skip the intro cinematic", &state.bSkipIntro );
        ImGui::Checkbox( "Show my name in the in-game watermark", &state.bWatermark );
        ImGui::Checkbox( "Show the FPS counter (top right, with the watermark)", &state.bShowFps );
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

    if ( CardBegin( "##settings_pad", ImVec2( 0, 0 ) ) ) {
        // Controller test: what SDL makes of the pad. If a stick is dead in the
        // game, this shows whether it moves here as a mapped axis, only as a
        // raw axis (a missing / wrong mapping), or not at all.
        ImGui::TextColored( kDim, "Controller test" );
        static SDL_Gamepad *pPad = nullptr;
        static SDL_JoystickID padId = 0;
        int count = 0;
        SDL_JoystickID *pIds = SDL_GetGamepads( &count );
        if ( count == 0 ) {
            ImGui::TextColored( kAmber, "No gamepad detected (connect it, then press a button)" );
            if ( pPad != nullptr ) {
                SDL_CloseGamepad( pPad );
                pPad = nullptr;
            }
        } else {
            if ( pPad == nullptr || padId != pIds[0] ) {
                if ( pPad != nullptr ) {
                    SDL_CloseGamepad( pPad );
                }
                padId = pIds[0];
                pPad = SDL_OpenGamepad( padId );
            }
            if ( pPad != nullptr ) {
                char guid[64] = {};
                SDL_GUIDToString( SDL_GetGamepadGUIDForID( padId ), guid, sizeof( guid ) );
                ImGui::PushFont( ImGui::GetIO().FontDefault, 12.0f );
                ImGui::TextWrapped( "%s", SDL_GetGamepadName( pPad ) );
                ImGui::TextColored( kDim, "GUID %s", guid );
                char *pMapping = SDL_GetGamepadMapping( pPad );
                ImGui::TextColored( kDim, "Mapping: %s", pMapping != nullptr ? "found" : "none (generic)" );
                if ( pMapping != nullptr ) {
                    ImGui::TextWrapped( "%s", pMapping );
                    SDL_free( pMapping );
                }
                ImGui::PopFont();
                static const char *kAxisNames[6] = { "Left X", "Left Y", "Right X", "Right Y", "L trigger", "R trigger" };
                for ( int a = 0; a < 6; a++ ) {
                    const float v = SDL_GetGamepadAxis( pPad, static_cast<SDL_GamepadAxis>( a ) ) / 32767.0f;
                    char overlay[48];
                    snprintf( overlay, sizeof( overlay ), "%s  %+.2f", kAxisNames[a], v );
                    ImGui::ProgressBar( ( v + 1.0f ) * 0.5f, ImVec2( -1.0f, S( 20.0f ) ), overlay );
                }
                SDL_Joystick *pJoy = SDL_GetGamepadJoystick( pPad );
                if ( pJoy != nullptr ) {
                    ImGui::TextColored( kDim, "Raw: %d axes, %d buttons, %d hats", SDL_GetNumJoystickAxes( pJoy ),
                                        SDL_GetNumJoystickButtons( pJoy ), SDL_GetNumJoystickHats( pJoy ) );
                    for ( int a = 0; a < SDL_GetNumJoystickAxes( pJoy ) && a < 8; a++ ) {
                        const float v = SDL_GetJoystickAxis( pJoy, a ) / 32767.0f;
                        char overlay[48];
                        snprintf( overlay, sizeof( overlay ), "raw axis %d  %+.2f", a, v );
                        ImGui::ProgressBar( ( v + 1.0f ) * 0.5f, ImVec2( -1.0f, S( 16.0f ) ), overlay );
                    }
                }
            }
        }
        SDL_free( pIds );
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

// The newest release that carries an APK named ...-b<number>.apk; compares the
// number with this build's. GitHub's JSON is scanned as text (no parser needed).
void ParseRelease( LauncherState &state, const std::string &json ) {
    const std::string key = "\"browser_download_url\"";
    size_t pos = 0;
    int bestBuild = -1;
    std::string bestUrl, bestName;
    while ( ( pos = json.find( key, pos ) ) != std::string::npos ) {
        const size_t q1 = json.find( '"', json.find( ':', pos ) );
        const size_t q2 = q1 == std::string::npos ? q1 : json.find( '"', q1 + 1 );
        if ( q2 == std::string::npos ) {
            break;
        }
        const std::string url = json.substr( q1 + 1, q2 - q1 - 1 );
        pos = q2;
        if ( url.size() < 5 || url.compare( url.size() - 4, 4, ".apk" ) != 0 ) {
            continue;
        }
        const size_t slash = url.rfind( '/' );
        const std::string name = slash == std::string::npos ? url : url.substr( slash + 1 );
        const size_t b = name.rfind( "-b" );
        const int build = b == std::string::npos ? 0 : atoi( name.c_str() + b + 2 );
        if ( build > bestBuild ) {
            bestBuild = build;
            bestUrl = url;
            bestName = name;
        }
    }
    const int current = atoi( OTDU_BUILD_NUMBER );
    if ( bestBuild < 0 ) {
        state.updateStage = UpdateStage::UpToDate;
        state.updateMessage = "No Android build has been published yet.";
    } else if ( bestBuild > current ) {
        state.updateStage = UpdateStage::Available;
        state.updateBuild = bestBuild;
        state.updateUrl = bestUrl;
        state.updateAsset = bestName;
    } else {
        state.updateStage = UpdateStage::UpToDate;
        state.updateMessage = "You are up to date (build " + std::to_string( current ) + ").";
    }
}

// --- Profiles / Mods / Updates / Online pages ----------------------------------

namespace Data = OTDULauncher;

void RefreshProfiles( LauncherState &state ) {
    state.profiles = Data::ScanProfiles( state.userData );
    state.bProfilesDirty = false;
}

// The profile the PLAY button uses: the selected id, else the first one.
const Data::ProfileInfo *ActiveProfile( LauncherState &state ) {
    if ( state.bProfilesDirty ) {
        RefreshProfiles( state );
    }
    for ( const Data::ProfileInfo &profile : state.profiles ) {
        if ( profile.bMain ? state.profileId == "*main*" : profile.Id == state.profileId ) {
            return &profile;
        }
    }
    return state.profiles.empty() ? nullptr : &state.profiles.front();
}

void SelectProfile( LauncherState &state, const Data::ProfileInfo &profile ) {
    state.profileId = profile.bMain ? "*main*" : profile.Id;
}

void DrawProfilesPage( LauncherState &state ) {
    if ( state.bProfilesDirty ) {
        RefreshProfiles( state );
    }
    ImGui::Spacing();
    ImGui::TextColored( kText, "Who is playing" );
    ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
    ImGui::TextColored( kDim, "One save per profile, in %s/profiles", state.userData.c_str() );
    ImGui::PopFont();
    ImGui::Spacing();

    const Data::ProfileInfo *pActive = ActiveProfile( state );
    std::string action; // set by a button; applied after the loop (the list may change)
    int actionIndex = -1;
    for ( size_t i = 0; i < state.profiles.size(); i++ ) {
        const Data::ProfileInfo &p = state.profiles[i];
        const bool bActive = pActive == &p;
        ImGui::PushID( static_cast<int>( i ) );
        if ( CardBegin( "##profile", ImVec2( 0, 0 ) ) ) {
            ImGui::TextColored( bActive ? kAccent : kText, "%s%s", p.Name.c_str(), p.bMain ? "  (main save)" : "" );
            ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
            if ( p.bHasSave ) {
                ImGui::TextColored( kDim, "%s%s%s - $%lld - %d cars", p.Avatar.c_str(), p.Avatar.empty() ? "" : " - ",
                                    Data::StoryStepLabel( p.StoryStep ).c_str(), static_cast<long long>( p.Money ), p.Cars );
                const std::string played = Data::FormatLastPlayed( p.LastPlayed );
                if ( !played.empty() ) {
                    ImGui::TextColored( kDim, "Played %s", played.c_str() );
                }
            } else {
                ImGui::TextColored( kDim, "New - no progress yet" );
            }
            ImGui::PopFont();
            if ( bActive ) {
                ImGui::TextColored( kGreen, "ACTIVE" );
            } else if ( ImGui::Button( "Use this profile", ImVec2( 0, S( 42.0f ) ) ) ) {
                action = "use";
                actionIndex = static_cast<int>( i );
            }
            if ( p.bHasSave ) {
                if ( ImGui::Button( "Back up", ImVec2( 0, S( 36.0f ) ) ) ) {
                    action = "backup";
                    actionIndex = static_cast<int>( i );
                }
                ImGui::SameLine();
            }
            if ( ImGui::Button( "Duplicate", ImVec2( 0, S( 36.0f ) ) ) ) {
                action = "dup";
                actionIndex = static_cast<int>( i );
            }
            if ( !p.bMain ) {
                ImGui::SameLine();
                if ( ImGui::Button( "Rename", ImVec2( 0, S( 36.0f ) ) ) ) {
                    action = "rename";
                    actionIndex = static_cast<int>( i );
                }
                ImGui::SameLine();
                if ( ImGui::Button( "Remove", ImVec2( 0, S( 36.0f ) ) ) ) {
                    action = "remove";
                    actionIndex = static_cast<int>( i );
                }
            }
            const std::vector<Data::BackupInfo> backups = Data::ListBackups( state.userData, p );
            if ( !backups.empty() ) {
                ImGui::PushFont( ImGui::GetIO().FontDefault, 12.0f );
                ImGui::TextColored( kDim, "%d backup%s, newest %s", static_cast<int>( backups.size() ),
                                    backups.size() == 1 ? "" : "s", backups.front().Label.c_str() );
                ImGui::PopFont();
                if ( ImGui::Button( "Restore newest backup", ImVec2( 0, S( 36.0f ) ) ) ) {
                    action = "restore";
                    actionIndex = static_cast<int>( i );
                }
            }
        }
        CardEnd();
        ImGui::PopID();
        ImGui::Spacing();
    }

    if ( actionIndex >= 0 ) {
        const Data::ProfileInfo p = state.profiles[actionIndex];
        std::string newId, error;
        bool bOk = true;
        if ( action == "use" ) {
            SelectProfile( state, p );
        } else if ( action == "backup" ) {
            state.profileMessage = Data::BackupProfile( state.userData, p ) ? "Backed up." : "Nothing new to back up.";
        } else if ( action == "dup" ) {
            bOk = Data::DuplicateProfile( state.userData, p, newId, error );
            state.profileMessage = bOk ? "Duplicated as \"" + newId + "\"." : error;
        } else if ( action == "rename" ) {
            state.renameTarget = p.Id;
            snprintf( state.renameBuffer, sizeof( state.renameBuffer ), "%s", p.Id.c_str() );
        } else if ( action == "remove" ) {
            state.removeTarget = p.Id;
        } else if ( action == "restore" ) {
            const std::vector<Data::BackupInfo> backups = Data::ListBackups( state.userData, p );
            if ( !backups.empty() ) {
                bOk = Data::RestoreBackup( state.userData, p, backups.front(), error );
                state.profileMessage = bOk ? "Restored the backup from " + backups.front().Label + "." : error;
            }
        }
        state.bProfilesDirty = true;
    }

    if ( !state.renameTarget.empty() ) {
        if ( CardBegin( "##rename", ImVec2( 0, 0 ) ) ) {
            ImGui::TextColored( kDim, "Rename \"%s\" to" , state.renameTarget.c_str() );
            ImGui::SetNextItemWidth( -1.0f );
            ImGui::InputText( "##rename_in", state.renameBuffer, sizeof( state.renameBuffer ) );
            if ( ImGui::Button( "Rename", ImVec2( 0, S( 42.0f ) ) ) ) {
                for ( const Data::ProfileInfo &p : state.profiles ) {
                    if ( !p.bMain && p.Id == state.renameTarget ) {
                        std::string newId, error;
                        if ( Data::RenameProfile( state.userData, p, state.renameBuffer, newId, error ) ) {
                            if ( state.profileId == p.Id ) {
                                state.profileId = newId;
                            }
                            state.profileMessage = "Renamed to \"" + newId + "\".";
                        } else {
                            state.profileMessage = error;
                        }
                    }
                }
                state.renameTarget.clear();
                state.bProfilesDirty = true;
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 0, S( 42.0f ) ) ) ) {
                state.renameTarget.clear();
            }
        }
        CardEnd();
        ImGui::Spacing();
    }
    if ( !state.removeTarget.empty() ) {
        if ( CardBegin( "##remove", ImVec2( 0, 0 ) ) ) {
            ImGui::TextColored( kAmber, "Remove \"%s\"?", state.removeTarget.c_str() );
            ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
            ImGui::TextWrapped( "The folder moves to profiles/.trash, so it can still be recovered by hand." );
            ImGui::PopFont();
            if ( ImGui::Button( "Remove it", ImVec2( 0, S( 42.0f ) ) ) ) {
                for ( const Data::ProfileInfo &p : state.profiles ) {
                    if ( !p.bMain && p.Id == state.removeTarget ) {
                        std::string error;
                        state.profileMessage = Data::DeleteProfile( state.userData, p, error ) ? "Removed." : error;
                    }
                }
                state.removeTarget.clear();
                state.bProfilesDirty = true;
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Keep it", ImVec2( 0, S( 42.0f ) ) ) ) {
                state.removeTarget.clear();
            }
        }
        CardEnd();
        ImGui::Spacing();
    }

    if ( CardBegin( "##newprofile", ImVec2( 0, 0 ) ) ) {
        ImGui::TextColored( kDim, "New profile" );
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputTextWithHint( "##newname", "Nickname", state.newNameBuffer, sizeof( state.newNameBuffer ) );
        if ( ImGui::Button( "Create profile", ImVec2( 0, S( 46.0f ) ) ) ) {
            std::string newId, error;
            if ( Data::CreateProfile( state.userData, state.newNameBuffer, newId, error ) ) {
                state.profileId = newId;
                state.newNameBuffer[0] = '\0';
                state.profileMessage = "Created \"" + newId + "\" and selected it.";
                state.bProfilesDirty = true;
            } else {
                state.profileMessage = error;
            }
        }
    }
    CardEnd();
    if ( !state.profileMessage.empty() ) {
        ImGui::Spacing();
        ImGui::TextColored( kAmber, "%s", state.profileMessage.c_str() );
    }
}

void DrawModsPage( LauncherState &state ) {
    if ( state.bModsDirty ) {
        state.mods = Data::ScanMods( state.userData );
        state.bModsDirty = false;
    }
    ImGui::Spacing();
    ImGui::TextColored( kText, "Mods" );
    ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
    ImGui::TextWrapped( "A mod is a folder in %s/mods/packages/<name>/ with a mod.json; tick it to enable it. Changes apply the next time you press PLAY.",
                        state.userData.c_str() );
    ImGui::PopFont();
    ImGui::Spacing();
    {
        // Loose files directly in mods/ (the Hong Kong island mod, props.txt, shader
        // overrides ...) are always active; count them so an "empty" page is explained.
        int loose = 0;
        std::error_code ec;
        const std::filesystem::path modsDir = std::filesystem::path( state.userData ) / "mods";
        for ( const auto &entry : std::filesystem::recursive_directory_iterator( modsDir, ec ) ) {
            const std::string rel = entry.path().lexically_relative( modsDir ).generic_string();
            if ( entry.is_regular_file( ec ) && rel.rfind( "packages/", 0 ) != 0 && rel != "enabled.txt" ) {
                loose++;
            }
        }
        if ( CardBegin( "##modsfolder", ImVec2( 0, 0 ) ) ) {
            ImGui::TextColored( kDim, "Mods folder" );
            ImGui::PushFont( ImGui::GetIO().FontDefault, 12.0f );
            ImGui::TextWrapped( "%s", modsDir.string().c_str() );
            ImGui::PopFont();
            if ( loose > 0 ) {
                ImGui::TextColored( kGreen, "%d loose file%s in this folder are always active", loose, loose == 1 ? "" : "s" );
            } else {
                ImGui::TextColored( kDim, "No loose files. Copy your PC mods folder contents here over USB." );
            }
            if ( ImGui::Button( "Rescan", ImVec2( 0, S( 40.0f ) ) ) ) {
                state.bModsDirty = true;
            }
        }
        CardEnd();
        ImGui::Spacing();
    }
    if ( state.mods.empty() ) {
        if ( CardBegin( "##nomods", ImVec2( 0, 0 ) ) ) {
            ImGui::TextColored( kDim, "No mods installed" );
            ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
            ImGui::TextWrapped( "Copy a mod folder to the phone (USB) into the packages folder above, then reopen this page." );
            ImGui::PopFont();
        }
        CardEnd();
    }
    bool bChanged = false;
    for ( size_t i = 0; i < state.mods.size(); i++ ) {
        Data::ModInfo &mod = state.mods[i];
        ImGui::PushID( static_cast<int>( i ) );
        if ( CardBegin( "##mod", ImVec2( 0, 0 ) ) ) {
            if ( mod.bNeedsImport ) {
                ImGui::BeginDisabled();
            }
            if ( ImGui::Checkbox( mod.Name.c_str(), &mod.bEnabled ) ) {
                bChanged = true;
            }
            if ( mod.bNeedsImport ) {
                ImGui::EndDisabled();
            }
            ImGui::PushFont( ImGui::GetIO().FontDefault, 12.0f );
            ImGui::TextColored( kDim, "%s%s%s%s%d file%s", mod.Version.c_str(), mod.Version.empty() ? "" : " - ",
                                mod.Author.c_str(), mod.Author.empty() ? "" : " - ", mod.FileCount,
                                mod.FileCount == 1 ? "" : "s" );
            if ( !mod.Description.empty() ) {
                ImGui::TextWrapped( "%s", mod.Description.c_str() );
            }
            if ( mod.bNeedsImport ) {
                ImGui::TextColored( kAmber, "Needs files imported from your own game copy - use the PC launcher once." );
            }
            ImGui::PopFont();
        }
        CardEnd();
        ImGui::PopID();
        ImGui::Spacing();
    }
    if ( bChanged ) {
        Data::SaveEnabledMods( state.userData, state.mods );
        state.bModsDirty = true;
    }
}

void DrawUpdatesPage( LauncherState &state ) {
    ImGui::Spacing();
    ImGui::TextColored( kText, "Updates" );
    ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
    ImGui::TextColored( kDim, "This build: %s", OTDU_BUILD_STRING );
    ImGui::PopFont();
    ImGui::Spacing();

    // Poll the Java worker.
    if ( state.updateStage == UpdateStage::Checking || state.updateStage == UpdateStage::Downloading ) {
        const int http = JavaHttpState();
        if ( http == 2 && state.updateStage == UpdateStage::Checking ) {
            ParseRelease( state, JavaHttpResult() );
        } else if ( http == 2 && state.updateStage == UpdateStage::Downloading ) {
            state.updateStage = UpdateStage::Downloaded;
            state.updateMessage = "Downloaded. Tap Install to continue.";
        } else if ( http == 3 ) {
            state.updateStage = UpdateStage::Failed;
            state.updateMessage = "Failed: " + JavaHttpResult();
        }
    }

    if ( CardBegin( "##update", ImVec2( 0, 0 ) ) ) {
#if !defined( OTDU_ANDROID )
        ImGui::TextColored( kDim, "In-app updates are an Android feature." );
#else
        switch ( state.updateStage ) {
        case UpdateStage::Idle:
        case UpdateStage::Failed:
        case UpdateStage::UpToDate:
            if ( !state.updateMessage.empty() ) {
                ImGui::TextColored( state.updateStage == UpdateStage::Failed ? kRed : kGreen, "%s",
                                    state.updateMessage.c_str() );
            }
            if ( ImGui::Button( "Check for updates", ImVec2( 0, S( 48.0f ) ) ) ) {
                state.updateStage = UpdateStage::Checking;
                state.updateMessage.clear();
                JavaStartGet( std::string( "https://api.github.com/repos/" ) + kRepoSlug + "/releases?per_page=10" );
            }
            break;
        case UpdateStage::Checking:
            ImGui::TextColored( kDim, "Checking..." );
            break;
        case UpdateStage::Available:
            ImGui::TextColored( kAccent, "Build %d is available (you have %d)", state.updateBuild, atoi( OTDU_BUILD_NUMBER ) );
            ImGui::PushFont( ImGui::GetIO().FontDefault, 12.0f );
            ImGui::TextColored( kDim, "%s", state.updateAsset.c_str() );
            ImGui::PopFont();
            if ( ImGui::Button( "Download", ImVec2( 0, S( 48.0f ) ) ) ) {
                state.updateStage = UpdateStage::Downloading;
                JavaStartDownload( state.updateUrl );
            }
            break;
        case UpdateStage::Downloading: {
            const long long done = JavaHttpDone(), total = JavaHttpTotal();
            ImGui::TextColored( kDim, "Downloading... %lld / %lld MB", done >> 20, total >> 20 );
            ImGui::ProgressBar( total > 0 ? static_cast<float>( done ) / static_cast<float>( total ) : 0.0f,
                                ImVec2( -1.0f, S( 24.0f ) ) );
            break;
        }
        case UpdateStage::Downloaded:
            ImGui::TextColored( kGreen, "%s", state.updateMessage.c_str() );
            if ( ImGui::Button( "Install", ImVec2( 0, S( 48.0f ) ) ) ) {
                if ( !JavaInstallApk() ) {
                    state.updateMessage = "Allow OpenTDU2 to install apps in the settings page that opened, then tap Install again.";
                }
            }
            break;
        }
#endif
    }
    CardEnd();
    ImGui::Spacing();
    ImGui::PushFont( ImGui::GetIO().FontDefault, 12.0f );
    ImGui::TextColored( kDim, "Builds come from the project's GitHub releases (an APK named ...-b<number>.apk). If none is published yet the check just says so." );
    ImGui::PopFont();
}

void DrawOnlinePage( LauncherState &state ) {
    ImGui::Spacing();
    ImGui::TextColored( kText, "Online" );
    ImGui::Spacing();
    if ( CardBegin( "##online", ImVec2( 0, 0 ) ) ) {
        ImGui::TextColored( kAmber, "Not available yet" );
        ImGui::PushFont( ImGui::GetIO().FontDefault, 13.0f );
        ImGui::TextWrapped( "The networking layer does not exist in the port yet. The fields are saved and passed to the game so they are ready, same as the PC launcher." );
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::TextColored( kDim, "Username" );
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputText( "##onuser", state.onlineUser, sizeof( state.onlineUser ) );
        ImGui::TextColored( kDim, "Server" );
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputText( "##onserver", state.onlineServer, sizeof( state.onlineServer ) );
    }
    CardEnd();
}

void DrawPage( LauncherState &state, bool *pbPlay ) {
    switch ( state.page ) {
    case Page::Play: DrawPlayPage( state, pbPlay ); break;
    case Page::Profiles: DrawProfilesPage( state ); break;
    case Page::Mods: DrawModsPage( state ); break;
    case Page::GameData: DrawGameDataPage( state ); break;
    case Page::Updates: DrawUpdatesPage( state ); break;
    case Page::Online: DrawOnlinePage( state ); break;
    case Page::Settings: DrawSettingsPage( state ); break;
    }
}

const char *const kPageTitles[7] = { "PLAY", "PROFILES", "MODS", "GAME DATA", "UPDATES", "ONLINE", "SETTINGS" };
const Page kPages[7] = { Page::Play, Page::Profiles, Page::Mods, Page::GameData, Page::Updates, Page::Online, Page::Settings };

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
        const char *const *titles = kPageTitles;
        const Page *pages = kPages;
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
        ImGui::TextColored( kAccent, "%s", titles[static_cast<int>( state.page )] );
        ImGui::BeginChild( "##main", ImVec2( 0, 0 ) );
        DrawPage( state, pbPlay );
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
            for ( int i = 0; i < 7; i++ ) {
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
    for ( int i = 0; i < 7; i++ ) {
        if ( NavItem( kPageTitles[i], state.page == kPages[i], S( 44.0f ) ) ) {
            state.page = kPages[i];
        }
    }
    ImGui::SetCursorPosY( ImGui::GetCursorPosY() + S( 8.0f ) );
    ImGui::PushFont( ImGui::GetIO().FontDefault, 11.0f );
    ImGui::TextWrapped( "%s", OTDU_BUILD_STRING );
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::BeginChild( "##main", ImVec2( 0, 0 ) );
    DrawPage( state, pbPlay );
    ImGui::EndChild();

    ImGui::End();
}

} // namespace

namespace OTDULauncher {

bool Run( SDL_Window **ppOutWindow ) {
    LauncherState state;
    LoadSettings( state );
    state.userData = UserDataDir();

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
        // The Android build has no command line: hand the choices over as
        // arguments (CmdLine::AddArg), the same flags the PC launcher passes.
        if ( !state.userData.empty() ) {
            CmdLine::AddArg( "-userdata", state.userData.c_str() );
        }
        if ( const OTDULauncher::ProfileInfo *pPlay = ActiveProfile( state ) ) {
            if ( !pPlay->bMain ) {
                CmdLine::AddArg( "-savefile", pPlay->SavePath.c_str() );
            }
            CmdLine::AddArg( "-playername", pPlay->Name.c_str() );
            if ( state.bWatermark ) {
                CmdLine::AddArg( "-tester", pPlay->Name.c_str() );
            }
        }
        if ( state.bSkipIntro ) {
            CmdLine::AddArg( "-skipintro", nullptr );
        }
        if ( state.bShowFps ) {
            CmdLine::AddArg( "-showfps", nullptr );
        }
        switch ( state.quality ) {
        case 0: // Phone: the Low profile at half scene resolution, near draw distance, 30 fps cap
            CmdLine::AddArg( "-profile", "low" );
            CmdLine::AddArg( "-renderscale", "50" );
            CmdLine::AddArg( "-drawdistance", "50" );
            CmdLine::AddArg( "-fpslimit", "30" );
            break;
        case 1:
            CmdLine::AddArg( "-profile", "low" );
            break;
        case 2:
            CmdLine::AddArg( "-profile", "medium" );
            break;
        default:
            CmdLine::AddArg( "-profile", "high" );
            break;
        }
        if ( state.onlineUser[0] != '\0' ) {
            CmdLine::AddArg( "-onlineusername", state.onlineUser );
        }
        if ( state.onlineServer[0] != '\0' ) {
            CmdLine::AddArg( "-onlineserver", state.onlineServer );
        }
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
