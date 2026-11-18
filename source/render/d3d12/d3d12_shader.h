#pragma once

#include "render/render_api.h"

#include <cstdint>
#include <string>
#include <vector>

struct TranslatedShader;

// One varying of a compiled stage as the DXBC really declares it (a reflection
// of the blob, not a guess). `Slot` is the *generic* slot - the position among
// the stage's parameters that are not system values - which is what D3D12's
// vertex/pixel linkage compares. The semantic index is D3D9's varying register
// and can be larger than the slot when a shader skips a register (a VS that
// writes oT0..oT4 and oT7 is six slots, not eight).
struct ShaderVarying {
    std::string Name;
    uint32_t Index = 0;
    uint32_t Slot = 0;
};

// The Direct3D 12 shader path (docs/backends.md section 4, phase 3b).
//
// TDU2's own shaders are D3D9 bytecode; the port already translates them to
// SPIR-V for Vulkan (DXVK's DXSO, `render/shaders/dxso_translator.*`). The
// engine's own shaders are GLSL built to SPIR-V at build time and embedded as
// `uint32_t` arrays. Both inputs are SPIR-V, so both take this same route:
//
//      SPIR-V --(SPIRV-Cross)--> HLSL --(D3DCompile)--> SM 5.1 DXBC
//
// SM 5.1 DXBC is valid D3D12 input (D3D12's floor is feature level 11_0 / SM
// 5.1), so no dxc and no DXIL is on the critical path. 5.1 rather than 5.0
// because DXSO emits `NonUniformResourceIndex` for D3D9's dynamically indexed
// samplers and SPIRV-Cross refuses that below 5.1. The HLSL text is kept:
// `-d3d12-dump-shaders` writes it out, which is what turns "the draw is black"
// into a readable shader.
//
// The register convention below is fixed for every program, so ONE root
// signature serves all of them (D3D12Scene):
//
//   b0        the stage's constant buffers, in the order they appear in the
//             SPIR-V (Eden uploads its 128-byte block here)
//   b13       the D3D9 push constants (render state, sampler dimensions): 32-bit
//             root constants in D3D12, the same 128-byte budget the Vulkan path
//             spends on `vkCmdPushConstants`
//   t0..t15   textures, in SPIR-V resource order
//   s0..s15   their samplers, in a sampler descriptor table (the DXSO sampler
//             set is a runtime array - see render/sampler_heap.h)
struct D3D12ShaderStage {
    std::vector<uint8_t> Bytecode; // vs_5_1 / ps_5_1 DXBC
    std::string HLSL;
    std::string Error;
    bool bReady = false;
    eShaderType Type = eShaderType::ST_Invalid;
    uint32_t NumConstantBuffers = 0;
    // The name of each cbuffer, in b0..bN order (the register RemapResources put
    // it at). DXSO's names are D3D9's: `cbuffer_t` is the stage's constant file,
    // `spec_state_t` the D3D9SpecializationInfo block, `D3D9SharedPS` / `clip_info_t`
    // the shared blocks; an Eden draw has to bind a different value to each, so
    // this is how it knows which is which (docs/backends.md section 3.8).
    std::vector<std::string> ConstantBufferNames;
    uint32_t NumTextures = 0;
    uint32_t NumSamplers = 0;
    // The push constant block: bytes, and how many 32-bit root constants the
    // root signature has to reserve for it.
    uint32_t PushConstantsSize = 0;
    uint32_t RootConstants = 0;
    // The stage's varyings, in generic-slot order (a vertex stage's outputs, a
    // pixel stage's inputs), reflected from the DXBC that was compiled. Empty
    // when reflection fails.
    std::vector<ShaderVarying> Varyings;
};

class D3D12ShaderCompiler {
public:
    // Every emitted shader declares its D3D9 push constants at this register;
    // the root signature has one 32-bit-constants parameter for it. SM 5.0 has
    // b0..b13 - 14 constant buffer slots - and the material blocks start at b0,
    // so the last slot is the one that stays clear of them; SM 5.1 does not
    // need to move it, and keeping one convention means a 5.0 fallback would
    // still line up.
    static constexpr uint32_t kPushConstantRegister = 13;
    // ...and in register space 1, because register space 0 is also the remap key
    // SPIRV-Cross looks up for every resource it rebinds. DXSO numbers its images
    // by D3D9 sampler slot (0..15), so a shader that samples through s13 has a
    // resource at (set 0, binding 13) - the exact key of the push constant block -
    // and to_resource_register() would take the *image's* unset cbv remap and
    // quietly move the root constants to b0 (where they collide with the
    // constant file: FXC error X4610, or worse, silently wrong constants).
    // A separate space cannot collide: D3D12 root parameters carry their own
    // register space, so this costs nothing. (Found by -d3d12-shadertest: 1146 of
    // 1470 dumped stages had b0 instead of b13.)
    static constexpr uint32_t kPushConstantSpace = 1;
    static constexpr uint32_t kMaxResources = 16;
    // SPIRV-Cross names the entry point `main`.
    static constexpr const char *kEntryPoint = "main";

    // Compiles one stage. On failure `outStage.Error` says why (the SPIRV-Cross
    // exception or the compiler's first line) - the caller logs it, one line
    // per shader, because a scene that silently loses a program is unreadable.
    static bool compile( const TranslatedShader &shader, D3D12ShaderStage &outStage, const char *pDebugName );

    // Compiles the pair and makes their varyings line up (docs/backends.md
    // section 3.9). D3D12 links a vertex and a pixel stage by *generic slot* -
    // the k-th non-system-value output against the k-th input - while HLSL
    // assigns those slots densely in declaration order. DXSO's locations are
    // D3D9's varying *registers*, which a .sob shader may skip (TD_LightBeam's
    // VS writes TEXCOORD0..4 and TEXCOORD7), so a pixel stage that reads
    // TEXCOORD0,1,2,7 puts TEXCOORD7 in slot 3 and the device refuses the
    // pipeline ("Semantic 'TEXCOORD' is defined for mismatched hardware
    // registers"). This pads the pixel stage's input struct with an unused
    // float4 per vertex output it does not read, which FXC keeps with an empty
    // `Used` mask, so slot k is the same semantic in both stages.
    static bool compilePair( const TranslatedShader &vertexShader,
                             const TranslatedShader &pixelShader,
                             D3D12ShaderStage &outVS,
                             D3D12ShaderStage &outPS,
                             const char *pDebugName );

    // Compiles HLSL text straight to DXBC with the same flags the generated
    // HLSL gets (SM 5.1, unbounded descriptor tables), and fills Bytecode /
    // HLSL / bReady. For the backends' own shaders - the 2D layer compiles its
    // pair this way, and -d3d12-samplertest needs a shader that indexes the
    // sampler heap, which no built-in SPIR-V does.
    static bool compileHlsl( const char *pHlsl,
                             eShaderType type,
                             D3D12ShaderStage &outStage,
                             const char *pDebugName );

    // The semantic of a vertex input at a SPIR-V location: SPIRV-Cross writes
    // `TEXCOORD<location>`, and D3D12Scene's input layout says the same, so the
    // `dcl` slot a .sob declares and the layout agree without a lookup table.
    static const char *vertexSemantic( uint32_t location );
    // The TEXCOORD index the semantic above names.
    static uint32_t vertexSemanticIndex( uint32_t location ) { return location; }

    // The two stages' varying lists as one line, for the log when a pipeline is
    // refused: "vs [TEXCOORD0@0 TEXCOORD1@1], ps [TEXCOORD0@0]" (section 3.9).
    static std::string describeVaryings( const D3D12ShaderStage &vs, const D3D12ShaderStage &ps );
};
