#include "shared.h"
#include "gs_imgui.h"

#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace {
// The base look the panels' layout constants were written for (the 13 px
// embedded bitmap font); everything scales relative to it.
constexpr float kBaseFontSize = 13.0f;
constexpr float kMinScale = 0.5f;
constexpr float kMaxScale = 4.0f;

// Common system fonts, one per line: the first file found is loaded so the
// glyphs can rasterize at any size (the embedded bitmap font cannot scale).
const char *const kFontCandidates[] = {
#ifdef _WIN32
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    "C:\\Windows\\Fonts\\calibri.ttf",
#elif defined( __APPLE__ )
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
#else
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
#endif
};
} // namespace

GSImGui *gpImGui = nullptr;

GSImGui::GSImGui()
    : bInitialized( false ), uiScale( 1.0f ), overrideScale( 0.0f ),
      bFontLoaded( false ) {
    gpImGui = this;
}

GSImGui::~GSImGui() { gpImGui = nullptr; }

bool GSImGui::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendRendererName = "OpenTDU2 Vulkan";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::StyleColorsDark();
    styleBase = ImGui::GetStyle();

    if ( const char *pScale = CmdLine::GetArgValue( "-imguiscale" ) ) {
        overrideScale = std::clamp( std::strtof( pScale, nullptr ), kMinScale, kMaxScale );
    }
    detectScale();

    ImGui_ImplSDL3_InitForVulkan( pInstance->getWindow() );
    bInitialized = true;
    return true;
}

void GSImGui::terminate() {
    if ( !bInitialized ) {
        return;
    }
    for ( ImTextureData *pTexture : ImGui::GetPlatformIO().Textures ) {
        if ( pTexture->TexID != ImTextureID_Invalid ) {
            gpRender->getDevice().destroyTexture( reinterpret_cast<GPUTexture *>( pTexture->TexID ) );
            pTexture->SetTexID( ImTextureID_Invalid );
            pTexture->SetStatus( ImTextureStatus_Destroyed );
        }
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    bInitialized = false;
}

void GSImGui::processEvent( const SDL_Event &event ) {
    if ( bInitialized ) {
        ImGui_ImplSDL3_ProcessEvent( &event );
    }
}

bool GSImGui::wantsKeyboard() const {
    return bInitialized && ImGui::GetIO().WantCaptureKeyboard;
}

bool GSImGui::wantsMouse() const { return bInitialized && ImGui::GetIO().WantCaptureMouse; }

void GSImGui::detectScale() {
    float scale = 1.0f;
    if ( overrideScale > 0.0f ) {
        scale = overrideScale;
    } else if ( pGameInstance != nullptr && pGameInstance->getWindow() != nullptr ) {
        // The OS content scale of the monitor the window is on (1 = 100 %).
        scale = SDL_GetWindowDisplayScale( pGameInstance->getWindow() );
        if ( scale <= 0.0f ) {
            scale = 1.0f;
        }
    }
    scale = std::clamp( scale, kMinScale, kMaxScale );
    if ( std::fabs( scale - uiScale ) > 0.01f ) {
        applyScale( scale );
    }
}

void GSImGui::setScale( float scale ) {
    // The slider wins over the OS scale (and the -imguiscale override) for
    // the rest of the session: store it as the override.
    overrideScale = std::clamp( scale, kMinScale, kMaxScale );
    if ( std::fabs( overrideScale - uiScale ) > 0.01f ) {
        applyScale( overrideScale );
    }
}

void GSImGui::applyScale( float scale ) {
    uiScale = scale;
    ImGuiStyle &style = ImGui::GetStyle();
    style = styleBase;
    style.ScaleAllSizes( scale );
    style.FontScaleMain = scale;
    // The panels' DpiScaled window sizes apply when the windows are created
    // (FirstUseEver keeps the size afterwards): a scale change re-scales the
    // fonts and the style immediately, the windows reflow in place.

    // The embedded bitmap font cannot resize; a TTF rasterizes at any size
    // (the dynamic fonts of ImGui 1.92), so load one once scale != 1.
    if ( scale > 1.0f + 0.01f && !bFontLoaded ) {
        const char *pFontPath = CmdLine::GetArgValue( "-imguifont" );
        if ( pFontPath == nullptr ) {
            for ( const char *pCandidate : kFontCandidates ) {
                std::error_code ec;
                if ( std::filesystem::exists( pCandidate, ec ) ) {
                    pFontPath = pCandidate;
                    break;
                }
            }
        }
        if ( pFontPath != nullptr ) {
            ImFont *pFont = ImGui::GetIO().Fonts->AddFontFromFileTTF( pFontPath, kBaseFontSize );
            if ( pFont != nullptr ) {
                ImGui::GetIO().FontDefault = pFont;
                bFontLoaded = true;
                OTDU_LOG_INFO( "ImGui: font '%s' (base %.0f px), UI scale %.2f\n", pFontPath,
                               kBaseFontSize, scale );
            } else {
                OTDU_LOG_WARN( "ImGui: failed to load font '%s'\n", pFontPath );
            }
        }
        if ( !bFontLoaded ) {
            OTDU_LOG_WARN( "ImGui: no scalable TTF found (see -imguifont), the text stays "
                           "at the bitmap font's size\n" );
        }
    }
}

void GSImGui::beginFrame( float deltaTime ) {
    // Follow the monitor the window is on (dragging between a 1x and a 2x
    // display re-scales).
    detectScale();
    ImGui_ImplSDL3_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    io.DeltaTime = deltaTime;
    ImGui::NewFrame();
}

void GSImGui::updateTexture( ImTextureData *pTexture ) {
    RenderDevice &device = gpRender->getDevice();
    if ( pTexture->Status == ImTextureStatus_WantDestroy ) {
        device.destroyTexture( reinterpret_cast<GPUTexture *>( pTexture->TexID ) );
        pTexture->SetTexID( ImTextureID_Invalid );
        pTexture->SetStatus( ImTextureStatus_Destroyed );
        return;
    }
    if ( pTexture->Status != ImTextureStatus_WantCreate &&
         pTexture->Status != ImTextureStatus_WantUpdates ) {
        return;
    }

    // Simple path: (re)create the whole texture on any change.
    if ( pTexture->TexID != ImTextureID_Invalid ) {
        device.destroyTexture( reinterpret_cast<GPUTexture *>( pTexture->TexID ) );
    }
    std::vector<uint8_t> rgba( static_cast<size_t>( pTexture->Width ) * pTexture->Height * 4 );
    const uint8_t *pPixels = static_cast<const uint8_t *>( pTexture->GetPixels() );
    if ( pTexture->Format == ImTextureFormat_Alpha8 ) {
        for ( size_t i = 0; i < static_cast<size_t>( pTexture->Width ) * pTexture->Height; i++ ) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = pPixels[i];
        }
    } else {
        memcpy( rgba.data(), pPixels, rgba.size() );
    }
    GPUTexture *pGPUTexture = gpRender->getTextureCache().createFromRGBA(
        static_cast<uint32_t>( pTexture->Width ), static_cast<uint32_t>( pTexture->Height ),
        rgba.data(), "ImGui" );
    pTexture->SetTexID( reinterpret_cast<ImTextureID>( pGPUTexture ) );
    pTexture->SetStatus( ImTextureStatus_OK );
}

void GSImGui::endFrame() {
    ImGui::Render();
    ImDrawData *pDrawData = ImGui::GetDrawData();
    if ( pDrawData->Textures != nullptr ) {
        for ( ImTextureData *pTexture : *pDrawData->Textures ) {
            if ( pTexture->Status != ImTextureStatus_OK ) {
                updateTexture( pTexture );
            }
        }
    }
    gpRender->get2DRenderer().drawImGui( pDrawData, nullptr );
}
