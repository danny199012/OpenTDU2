#include "shared.h"
#include "render/d3d12/d3d12_device.h"

// Non-Windows builds (Android/Linux/macOS) compile the Direct3D backend
// sources to empty translation units (OTDU_D3D12 is WIN32-only), but the
// engine still links against the class: gs_render.cpp owns a D3D12Device
// member and its runtime branches reference these methods. The stubs keep the
// static engine library linkable; the runtime never reaches them (Resolve()
// only picks Direct3D on Windows, docs/backends.md).
#if !defined( OTDU_D3D12 )

D3D12Device::D3D12Device() {}
D3D12Device::~D3D12Device() {}

bool D3D12Device::create( SDL_Window *, bool, bool ) { return false; }
void D3D12Device::destroy() {}
void D3D12Device::waitIdle() {}
bool D3D12Device::beginFrame( const ColorRGBA & ) { return false; }
void D3D12Device::endFrame() {}
bool D3D12Device::createTexture2D( uint32_t, uint32_t, uint32_t, uint32_t,
                                   const void *const *, const uint32_t *,
                                   Texture &, const char * ) {
    return false;
}
void D3D12Device::destroyTexture( Texture & ) {}

#endif
