#pragma once

#include <string>

struct SDL_Window;

// The integrated launcher (the Windows one is a separate exe,
// tools/launcher/): on Android the game must offer a start screen before any
// game data exists, so this boots the Vulkan window + the 2D renderer +
// ImGui with no TDU2 files, then hands the window to the normal engine init
// (tdu_instance.cpp). The UI matches the desktop launcher's look (dark navy
// panels, orange accent) and works with touch (SDL touch -> mouse, density
// scaled) and with a gamepad (ImGui nav; the engine drives with the pad).
//
// Only the pages that make sense on a phone: Play, Game Data (folder browser
// over the shared storage, All-Files-Access aware) and Settings. Profiles,
// mods, ISO install and the updater stay desktop-launcher features.
namespace OTDULauncher {

// Runs the bootstrap screen. Returns:
//   true  -> PLAY: *ppOutWindow holds the SDL window to hand to the engine
//            init (it keeps it for the whole session) and OTDU2_GAME_DIR is
//            set to the validated game folder.
//   false -> the user backed out of the app; nothing to hand over.
bool Run( SDL_Window **ppOutWindow );

// <root>/Euro/Bnk exists (case-insensitive, mirrors GSFile::HasBankFolder).
bool HasBankFolder( const std::string &root );

} // namespace OTDULauncher
