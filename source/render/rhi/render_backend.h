#pragma once

#include <cstdint>

// Graphics backend of this run (docs/backends.md).
//
// The port grew up on Vulkan, one backend, with every renderer (eden, the 2D
// batches, the texture cache) talking to RenderDevice directly. The Direct3D
// backends are added behind this switch, newest first (12 -> 11 -> 9), so the
// Vulkan path stays exactly what it is while the others land.
enum class eRenderBackend : uint8_t {
    Vulkan = 0, // the complete renderer; the default
    D3D12,      // device + swapchain + clears + screenshots (docs/backends.md)
    D3D11,      // scaffolding only, not implemented yet
    D3D9,       // scaffolding only, not implemented yet
    Count,
    Invalid = 0xff
};

namespace RenderBackend {
// Whether the backend was compiled in (CMake options OTDU_ENABLE_D3D12 / ...).
bool IsCompiledIn( eRenderBackend backend );

// The backend this run uses: "-renderer <name>" / "-renderer=<name>", then the
// OTDU_RENDERER environment variable, then Vulkan. A name that is unknown, or
// that was not compiled in, is a warning and a fall back to Vulkan: a typo must
// never stop the game from starting.
eRenderBackend Resolve();

// "vulkan" / "d3d12" / "dx12" / "d3d11" / "dx11" / "d3d9" / "dx9", case
// insensitive. eRenderBackend::Invalid for anything else.
eRenderBackend FromName( const char *pName );
const char *GetName( eRenderBackend backend );

// True for the Direct3D backends.
bool IsDirect3D( eRenderBackend backend );

// True when the backend can draw the game (scene, 2D UI, Flash). Only Vulkan
// can, today: the Direct3D backends present the swapchain alone until the
// shader and binding port of docs/backends.md lands.
bool CanRenderScene( eRenderBackend backend );

// One line for the log: what was asked for, what is compiled in, what runs.
void LogResolved( eRenderBackend backend );

// -renderer-selftest: creates a window and the chosen Direct3D device, presents
// a test pattern for a few frames, optionally writes a .ppm/window screenshot
// and returns whether the backend works. Runs before the game (no game data,
// no scene), so the backend can be proven on any machine - this is the check
// docs/backends.md asks for.
bool RunSelfTest();
} // namespace RenderBackend
