#pragma once

#include <cstdint>

// The D3D9 sampler set every Eden shader indexes (docs/backends.md section 4).
//
// DXSO turns a D3D9 `sampler2D s<N>` into a *runtime descriptor array*
// (`sampler_heap[] : register(s0, space0)`) and the shader reads the entry index
// out of the D3D9 push constant block (EdenRenderer::fillConstants writes each
// material slot's index at byte 64). The numbers are therefore game data, not a
// backend detail: the only way a material samples the same texels on two
// backends is for both backends to expose this table in this order - Vulkan as a
// descriptor set of samplers (`EdenRenderer::samplerHeap`), Direct3D 12 as a
// sampler descriptor heap (`D3D12Scene`) - and for both to build each entry from
// the same state. They live here, once, so a new entry cannot be added to one
// backend only.
enum eSamplerHeapIndex : uint8_t {
    SH_LinearWrap = 0,
    SH_LinearClamp,
    SH_PointWrap,
    SH_PointClamp,
    SH_LinearMirror,
    // 2DM per-slot addressing (MaterialTextureSlot::AddressU / V): linear,
    // SH_LinearUV + 3 * u + v with 0 wrap, 1 mirror, 2 clamp.
    SH_LinearUV,
    SH_Count = SH_LinearUV + 9
};

// One entry of the heap: the filter, and the addressing of U and V as the 2DM
// slot byte maps them - 0 wrap, 1 mirror, 2 clamp (eden_renderer.cpp's
// AddressIndex turns the raw D3D9 value into exactly these three).
struct SamplerHeapState {
    bool bPoint = false;   // SF_Point / D3D12_FILTER_*_POINT, else linear
    uint8_t AddressU = 0;  // 0 wrap, 1 mirror, 2 clamp
    uint8_t AddressV = 0;
};

inline constexpr uint32_t kSamplerHeapSize = SH_Count;

// The state of entry 'index'. Out of range (an engine shader naming s14/s15, or
// a material that stored a stale index): D3D9's default, linear wrap.
inline SamplerHeapState GetSamplerHeapState( uint32_t index ) {
    SamplerHeapState state;
    switch ( index ) {
    case SH_LinearClamp:
        state.AddressU = 2;
        state.AddressV = 2;
        break;
    case SH_PointWrap:
        state.bPoint = true;
        break;
    case SH_PointClamp:
        state.bPoint = true;
        state.AddressU = 2;
        state.AddressV = 2;
        break;
    case SH_LinearMirror:
        state.AddressU = 1;
        state.AddressV = 1;
        break;
    default:
        if ( index >= SH_LinearUV && index < SH_Count ) {
            const uint32_t mode = index - SH_LinearUV;
            state.AddressU = static_cast<uint8_t>( mode / 3 );
            state.AddressV = static_cast<uint8_t>( mode % 3 );
        }
        break;
    }
    return state;
}
