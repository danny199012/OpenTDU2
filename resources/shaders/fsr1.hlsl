// FidelityFX Super Resolution 1.0 (EASU upscale + RCAS sharpen) for OpenTDU2.
// The algorithms are AMD's (MIT licence, third_party/fidelityfx-fsr1); this file
// is only the glue: two pixel shaders over a fullscreen triangle.
//   EASU: scene at render resolution -> window resolution (edge adaptive upscale)
//   RCAS: robust contrast adaptive sharpening of the EASU output
// Compiled to SPIR-V by tools/compile_shaders.py (dxc -spirv).
#define FFX_GPU 1
#define FFX_HLSL 1
#define FFX_HLSL_SM 62
#define FFX_FSR_EASU_FLOAT 1
#define FSR_RCAS_F 1

#include "gpu/ffx_core.h"

[[vk::combinedImageSampler]] [[vk::binding( 0, 0 )]] Texture2D<float4> gSource : register( t0 );
[[vk::combinedImageSampler]] [[vk::binding( 0, 0 )]] SamplerState gSampler : register( s0 );

struct Params {
    uint4 con0;
    uint4 con1;
    uint4 con2;
    uint4 con3;
};
[[vk::push_constant]] Params pc;

FfxFloat32x4 FsrEasuRF( FfxFloat32x2 p ) { return gSource.GatherRed( gSampler, p ); }
FfxFloat32x4 FsrEasuGF( FfxFloat32x2 p ) { return gSource.GatherGreen( gSampler, p ); }
FfxFloat32x4 FsrEasuBF( FfxFloat32x2 p ) { return gSource.GatherBlue( gSampler, p ); }

FfxFloat32x4 FsrRcasLoadF( FfxInt32x2 p ) { return gSource.Load( int3( p, 0 ) ); }
void FsrRcasInputF( inout FfxFloat32 r, inout FfxFloat32 g, inout FfxFloat32 b ) {}

#include "gpu/ffx_fsr1.h"

float4 EASU( float4 pos : SV_Position ) : SV_Target {
    FfxFloat32x3 color;
    ffxFsrEasuFloat( color, FfxUInt32x2( pos.xy ), pc.con0, pc.con1, pc.con2, pc.con3 );
    return float4( color, 1.0 );
}

float4 RCAS( float4 pos : SV_Position ) : SV_Target {
    FfxFloat32 r, g, b;
    FsrRcasF( r, g, b, FfxUInt32x2( pos.xy ), pc.con0 );
    return float4( r, g, b, 1.0 );
}
