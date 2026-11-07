#pragma once

#include <cstdint>
#include <string>

// "Advanced settings" page: settings that the retail options do not have
// (the **speed units** — km/h / mph, game/units.h — the button prompts, render
// scale, upscaling filter, sharpening, FXAA-like edge smoothing, effect
// toggles, frame rate limit). The retail pages are
// Flash movies whose rows are fixed, so this page is native (Dear ImGui) and
// is opened from the Video or Controls page with Y / Triangle
// (MENU_OPTION_2), or with F2. Left / Right
// change a row, Up / Down pick one, Back / OK close it and save.
// Upscaler slots: bilinear and bicubic exist; FSR 1.0 / FSR 2+ / DLSS are the
// next entries (docs/re/advanced_graphics.md).
class AdvancedGraphics {
public:
    bool isOpen() const { return bOpen; }
    void open();
    void close();
    // The Video page is up: draw a one line hint for the shortcut.
    void setVideoPageHint( bool bShow ) { bHint = bShow; }
    // Called from the UI pass every frame (input + drawing).
    void drawUI();

private:
    // Runs RayTracing::runSelfTest() and keeps the verdict for the page.
    void runSelfTest();

    bool bOpen = false;
    bool bHint = false;
    int32_t selected = 0;
    std::string selfTestResult; // empty until it has been run from the page
};

AdvancedGraphics &GetAdvancedGraphics();
