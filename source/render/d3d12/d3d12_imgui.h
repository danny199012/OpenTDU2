#pragma once

struct SDL_Window;

// Dear ImGui on the Direct3D 12 backend (docs/backends.md, phase 2).
//
// imgui_impl_dx12 ships its shaders as precompiled DXBC bytecode, so the debug
// overlay, the build watermark and the Options panels draw on D3D12 without any
// shader toolchain work. The seams here keep d3d12.h and the ImGui backend
// headers out of GSImGui: only this translation unit includes them.
//
// The device it draws into is GSRender's D3D12Device (getD3D12Device()).
namespace D3D12ImGui {
bool initialize( SDL_Window *pWindow );
void shutdown();
void newFrame();
// ImDrawData*, drawn into the frame's command list (between begin and endFrame).
void render( void *pDrawData );
// ImTextureData*: the font atlas and any UI textures, uploaded by the backend.
void updateTexture( void *pTextureData );
} // namespace D3D12ImGui
