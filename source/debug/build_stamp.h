#pragma once

#include <cstdint>
#include <string>

// The build stamp burnt into every saved screenshot (PNG / PPM): a tiny 5x7
// label at the bottom-right, e.g. "BUILD 406 5C3E76D CLINE". It is independent
// of the ImGui watermark (-nowatermark disables both) and of any graphics
// setting, so a screenshot the owner or an agent posts always says which build
// it came from - the workflow rule added 2026-11-03 ("watermark the runs with
// build ids ... see what build it was caught and how").
namespace BuildStamp {

// "<build N> <hash>[+dirty] <branch>", uppercased and reduced to the glyphs the
// 5x7 font has (A-Z, 0-9, space, +.()_-).
std::string Label();

// Burns the label into an RGB24 image (3 bytes per pixel, bBGR swaps R/B on
// write). No-op when the image is too small or `-nowatermark` is set.
void Burn( uint8_t *pRGB, int32_t width, int32_t height, bool bBGR );

} // namespace BuildStamp
