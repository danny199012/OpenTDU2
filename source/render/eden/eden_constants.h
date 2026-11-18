#pragma once

#include "core/math.h"
#include "render/shaders/dxso_translator.h"

#include <string>
#include <unordered_set>
#include <vector>

// The D3D9 constant file a stage reads (`int4 c_i[16]; float4 c_f[N];` plus the
// bool bitmask - DXSO's `cbuffer_t`), and the D3D9SpecializationInfo block every
// stage reads as `spec_state_t`. Shared by the Vulkan renderer and the Direct3D
// 12 scene path: a size that differed between them would be a silently wrong
// read, not a compile error.
inline constexpr uint32_t kEdenIntRegistersSize = 16 * 16;
inline constexpr uint32_t kEdenVSFloatRegisters = 256;
inline constexpr uint32_t kEdenPSFloatRegisters = 224;
inline constexpr uint32_t kEdenBoolBitmaskSize = 16;
inline constexpr uint32_t kEdenVSConstantsSize =
    kEdenIntRegistersSize + kEdenVSFloatRegisters * 16 + kEdenBoolBitmaskSize;
inline constexpr uint32_t kEdenPSConstantsSize =
    kEdenIntRegistersSize + kEdenPSFloatRegisters * 16 + kEdenBoolBitmaskSize;
inline constexpr uint32_t kEdenSpecDwords = 16;

// Where the push constant block (b13 space1 on D3D12, `vkCmdPushConstants` on
// Vulkan) carries what is not a constant file: the D3D9 alpha reference every
// DXSO pixel stage tests against, and the sixteen sampler-heap indices the
// material's slots name (`EdenMaterial::SamplerIndex`). Two backends writing
// these at different offsets would sample with the wrong state and still look
// plausible, so the offsets live here.
inline constexpr uint32_t kEdenPushAlphaRefOffset = 24;
inline constexpr uint32_t kEdenPushSamplerOffset = 64;


struct EdenMaterial;
struct EdenSceneParams;
struct EdenDrawParams;

// Filling a stage's D3D9 constant file (docs/backends.md phase 3b step 2).
//
// What an Eden shader reads is not a backend detail: the same register holds the
// same number on Vulkan and on Direct3D 12, and a drift between the two would be
// a pixel difference nobody can explain. So the rules live here, once, as a free
// function - `EdenRenderer::fillConstants` is the wrapper that hands it the
// renderer's scene, and a Direct3D 12 caller with no EdenRenderer at all (the
// `-d3d12-edentest` probe) passes its own.
//
// The state the filler needs that used to be the renderer's own: the scene
// parameters, the derived view-projection, the precise (double) variant when the
// caller has one, the "scene epoch" the per-member lookup cache is keyed by, and
// the verbose-mode warning set (one line per material parameter with no value).
struct EdenConstantsContext {
    const EdenSceneParams *pScene = nullptr;
    const Matrix4x4f *pViewProjection = nullptr;
    const Matrix4x4d *pPreciseViewProjection = nullptr;
    uint64_t SceneEpoch = 0;
    std::unordered_set<std::string> *pWarned = nullptr;
};

// Writes 'members' into the register file of one stage: pRegisters points at the
// *float* registers of the block (16 int4 registers come before it, the bool
// bitmask after it - EdenRenderer's kVSConstantsSize / kPSConstantsSize layout,
// which a D3D9 `cbuffer cbuffer_t { int4 c_i[16]; float4 c_f[N]; }` reads).
void FillEdenConstants( const EdenConstantsContext &context,
                        const std::vector<ShaderConstantMember> &members,
                        float *pRegisters,
                        uint32_t numRegisters,
                        const EdenMaterial &material,
                        const Matrix4x4f &world,
                        const EdenDrawParams *pParams,
                        const char *pProgramName,
                        bool bPixel );

// The D3D9SpecializationInfo block every stage's `spec_state_t` cbuffer reads
// (16 dwords; dword 1, bits 21..23, is the alpha compare op). The renderer and a
// backend's own copy have to agree on these, so the write is here too.
void FillEdenSpecConstants( uint32_t pSpec[16], bool bAlphaTest );
