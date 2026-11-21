#pragma once

#include <string>

struct GPUTexture;

// The licence photo: the camera shot of the racing school's win scene, shown on the pause menu's ID card
// (docs/re/licenses_spec.md 12) [CODE: the retail card loads "Licence00.jpg" (GSNetworkPictures) into
// .../idcard/avatar_photo/lure; INF: the picture is the frame at the scene's "phfl" cue; the port stores a PNG
// because its screenshot writer does]. <userdata>/Licence00.png.
namespace LicencePhoto {

std::string Path();
bool Exists();
// Asks the device to save the frame on screen (written at the start of the next frame).
bool Capture();
// The photo as a texture (loaded once, reloaded after a capture); nullptr when there is none.
GPUTexture *GetTexture();
// A capture is pending or just written: GetTexture reloads.
void Invalidate();

} // namespace LicencePhoto
