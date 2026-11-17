#pragma once

#include "render/render_api.h"

#include <cstdint>
#include <string>
#include <vector>

struct TranslatedShader;

// The Direct3D 12 shader path (docs/backends.md section 4, phase 3b).
//
// TDU2's own shaders are D3D9 bytecode; the port already translates them to
// SPIR-V for Vulkan (DXVK's DXSO, `render/shaders/dxso_translator.*`). The
// engine's own shaders are GLSL built to SPIR-V at build time and embedded as
// `uint32_t` arrays. Both inputs are SPIR-V, so both take this same route:
//
//      SPIR-V --(SPIRV-Cross)--> HLSL --(D3DCompile)--> SM 5.0 DXBC
//
// SM 5.0 DXBC is valid D3D12 input (the 2D layer already ships it), so no dxc
// and no DXIL is on the critical path. The HLSL text is kept: `-d3d12-dump-shaders`
// writes it out, which is what turns "the draw is black" into a readable shader.
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
//   s0..s15   their samplers (static samplers in the root signature)
struct D3D12ShaderStage {
    std::vector<uint8_t> Bytecode; // vs_5_0 / ps_5_0 DXBC
    std::string HLSL;
    std::string Error;
    bool bReady = false;
    eShaderType Type = eShaderType::ST_Invalid;
    uint32_t NumConstantBuffers = 0;
    uint32_t NumTextures = 0;
    uint32_t NumSamplers = 0;
    // The push constant block: bytes, and how many 32-bit root constants the
    // root signature has to reserve for it.
    uint32_t PushConstantsSize = 0;
    uint32_t RootConstants = 0;
};

class D3D12ShaderCompiler {
public:
    // Every emitted shader declares its D3D9 push constants at this register;
    // the root signature has one 32-bit-constants parameter for it. b13 is the
    // last constant buffer SM 5.0 has (b0..b13 - 14 slots, "maximum cbuffer
    // exceeded" is what b14/b15 produce), and the material blocks above start
    // at b0, so it is also the safest choice.
    static constexpr uint32_t kPushConstantRegister = 13;
    static constexpr uint32_t kMaxResources = 16;
    // SPIRV-Cross names the entry point `main`.
    static constexpr const char *kEntryPoint = "main";

    // Compiles one stage. On failure `outStage.Error` says why (the SPIRV-Cross
    // exception or the compiler's first line) - the caller logs it, one line
    // per shader, because a scene that silently loses a program is unreadable.
    static bool compile( const TranslatedShader &shader, D3D12ShaderStage &outStage, const char *pDebugName );

    // The semantic of a vertex input at a SPIR-V location: SPIRV-Cross writes
    // `TEXCOORD<location>`, and D3D12Scene's input layout says the same, so the
    // `dcl` slot a .sob declares and the layout agree without a lookup table.
    static const char *vertexSemantic( uint32_t location );
    // The TEXCOORD index the semantic above names.
    static uint32_t vertexSemanticIndex( uint32_t location ) { return location; }
};
