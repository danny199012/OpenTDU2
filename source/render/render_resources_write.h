#pragma once

// The Eden render-resource WRITERS (M5 last, docs/re/scene_editor.md 8): the
// composed exterior bank build authors .2db textures, .2dm materials, .3dg
// geometry and .3dd scenes over the RenderFile container (render_file_write).
// The laws are the readers' mirrors (render/2db.cpp 2dm.cpp 3dg.cpp 3dd.cpp),
// each field position proven against the retail files of the extracted banks;
// the layout is validated by `otdu2_bnk_tool roundtripfile` on retail payloads
// and the composed bank itself parses through the retail load path.
#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"
#include "render/2dm.h"

// The rsc hash of the authored files (entities.xmb / hashcodes.xmb reference
// each other by value; the retail values are Eden's own path hash, so the
// authoring picks its own stable one): FNV-1a 64.
uint64_t HashResourceName( const char *pName );

// --- .2db (render/2db.h): one BMAP section, Texture2DBHeader (0x30), texels.

// Encodes RGBA8 pixels as VF_A8R8G8B8 (the D3D9 byte order: B, G, R, A - the
// texture cache uploads it natively on Vulkan and D3D12 alike). The mip chain
// (box filter, down to 1x1) is generated when bGenMips; hashcode should be
// GetIdentifier64bit of the texture name (the bank texture cache's key).
bool Build2dbFromRGBA( const char *pName, const uint8_t *pRGBA, uint32_t width, uint32_t height, bool bGenMips,
                       std::vector<uint8_t> &outBytes );

// --- .2dm (render/2dm.h): MATA { HASH, MAT. per material }.

// Serializes the parsed form back. The MAT. payload is the retail 0x340 base
// (+ 0x10 per parameter): hash, name[32], render states at 0x30, colors at
// 0xb0, NumTextureSlots at 0xfc, 16 slots x 0x20 at 0x100, shader GUID at
// 0x300, shader version at 0x30a, the 48 spec bytes at 0x310, parameters from
// 0x340. The material hashcodes must be the ones the .3dd's MBNK references.
bool Build2dm( const std::vector<MaterialDesc> &materials, std::vector<uint8_t> &outBytes );

// --- .3dg (render/3dg.h): GEOA { HASH, GEOM { PRIM } }, VBA. { DXVB }, IBA.
//     { DXIB }.

// One primitive: a triangle list in the port's draw convention (the OBJ parse
// already mirrors X and reverses the winding), one interleaved vertex stream:
// position f32x3, normal f32x3, texcoord f32x2 (32 bytes; DXVB layout flags
// 0x1800 - the same semantics the OBJ draw route binds).
struct AuthoredPrimitive {
    uint16_t MaterialIndex = 0; // index into the instance's MBNK material list
    std::vector<float> Positions; // xyz per vertex
    std::vector<float> Normals;   // xyz per vertex
    std::vector<float> UVs;       // uv per vertex
    std::vector<uint32_t> Indices;
};

// One geometry = one OBJA object (one .3dg can hold several; the composed
// bank build authors one per distinct prop OBJ).
struct AuthoredGeometry {
    uint64_t Hashcode = 0;
    std::vector<AuthoredPrimitive> Primitives;
};

bool Build3dg( const std::vector<AuthoredGeometry> &geometries, std::vector<uint8_t> &outBytes, std::string &outError );

// --- .3dd (render/3dd.h): MBNA { MBNK }, OBJA { HASH, OBJ. }, INSA { HASH,
//     INS. }, HIEA { HASH, HIE. }.

// One instance = one scene node under the root (a placement of a prop).
struct AuthoredInstance {
    std::string Name;              // node name (hashed, the editor's overrides match through it)
    uint64_t GeometryHashcode = 0; // the OBJA object's hashcode (the .3dg lookup)
    Matrix4x4f Local;              // the composed placement (m[c][r] written row-major)
    std::vector<uint64_t> Materials; // the instance's material hashcodes (its MBNK)
};

bool Build3dd( uint64_t rootHashcode, const std::vector<AuthoredInstance> &instances, std::vector<uint8_t> &outBytes,
               std::string &outError );
