#pragma once

#include "core/math.h"
#include "render/eden/eden_renderer.h"

#include <vector>

// The shared OBJ route of the port's own assets (Blender -> OBJ + MTL + PNG,
// read through the mod overlay, docs/re/modding.md 3): one draw per `usemtl`
// of the file, lit with a stock Eden shader family. `mod_props` (world props)
// and the tuner-garage mod (car parts) both load this way; the parser used to
// live inside mod_props.cpp and was lifted out when the car parts needed it.
//
// Vertices are one interleaved stream (position, normal, texcoord, 32 bytes),
// X-mirrored with reversed winding exactly like the 3DG loader does to the
// game's own geometry (render/3dg.cpp parse(..., bMirrorX)) - a mesh authored
// in Blender (right handed, Y up, -Z forward) lands the same way round as a
// retail mesh.

// One draw: a mesh + its material (+ the buffers and texture it owns; they are
// destroyed by UnloadObjDraws, on the render device).
struct ObjMeshDraw {
    EdenMesh Mesh;
    EdenMaterial Material;
    GPUBuffer *pVertexBuffer = nullptr;
    GPUBuffer *pIndexBuffer = nullptr;
    GPUTexture *pTexture = nullptr;
};

// Loads <folder>/<objName>.obj (+ the .mtl its `mtllib` names, and the map_Kd
// textures, all paths relative to `folder` inside the mods overlay). The
// shader family is any stock one (`TD_Gouraud` is the default): the material
// gets the .mtl Kd as its diffuse / ambient colour and the map_Kd texture (a
// loose PNG/JPG/DDS, not a .2db) bound to the COLOR slot. The optional out
// parameters collect the bounds (for draw culling) and the triangle count.
// False when the file is missing or has no triangle (the caller decides
// whether that is worth a warning - the overlay not having the file at all is
// the normal case for optional mods).
bool LoadObjDraws( EdenRenderer &renderer, const char *pFolder, const char *pObjName,
                   const char *pShaderName, std::vector<ObjMeshDraw> &outDraws,
                   Vector3f *pBoundsMin = nullptr, Vector3f *pBoundsMax = nullptr,
                   uint32_t *pTriangles = nullptr, const char *pLogTag = "ObjMesh" );

// Destroys the device resources of the draws (buffers, textures) and clears
// the list. Null-safe: gpRender may already be gone at shutdown.
void UnloadObjDraws( std::vector<ObjMeshDraw> &draws );
