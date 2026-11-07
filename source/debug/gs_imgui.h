#pragma once

#include "game/gs_base.h"

#include "imgui.h"

union SDL_Event;
struct ImTextureData;

// Dear ImGui integration (debug menus, toggled with F1). Rendered by the 2D
// renderer (ImDrawVert matches Vertex2D).
//
// DPI aware: the style and the font scale with the window's display scale
// (SDL_GetWindowDisplayScale), so the panels stay readable on high-DPI
// monitors. -imguiscale <f> forces a scale, -imguifont <path> picks the TTF.
// At scale 1 the classic look is kept exactly (13 px bitmap font).
class GSImGui : public GameSystem {
public:
    GSImGui();
    ~GSImGui();

    const char *getName() const override { return "Service : ImGui"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void terminate() override;

    void processEvent( const SDL_Event &event );
    bool wantsKeyboard() const;
    bool wantsMouse() const;

    void beginFrame( float deltaTime );
    void endFrame();

    // The current UI scale (1 = the classic 13 px bitmap-font look). The
    // layout constants of the panels go through DpiScaled below.
    float getScale() const { return uiScale; }
    // Overrides the scale at runtime (the cheats panel's "UI scale" slider).
    void setScale( float scale );

private:
    bool bInitialized;
    ImGuiStyle styleBase;
    float uiScale;
    float overrideScale;
    bool bFontLoaded;

private:
    void updateTexture( ImTextureData *pTexture );
    void detectScale();
    void applyScale( float scale );
};

extern GSImGui *gpImGui;

// The current UI scale, safe to call before the ImGui service exists.
inline float DpiScale() { return gpImGui != nullptr ? gpImGui->getScale() : 1.0f; }
// Sizes/positions written for the 13 px base look, scaled with it.
inline ImVec2 DpiScaled( float x, float y ) {
    const float scale = DpiScale();
    return ImVec2( x * scale, y * scale );
}
