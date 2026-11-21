#pragma once

#include "core/math.h"
#include "render/eden/eden_renderer.h"

#include <string>
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

// The same build from in-memory OBJ / MTL text (procedural props, e.g. the pool ring).
bool BuildObjDraws( EdenRenderer &renderer, const char *pFolder, const char *pObjName, const char *pShaderName,
                    const std::string &objText, const std::string &mtlText, std::vector<ObjMeshDraw> &outDraws,
                    Vector3f *pBoundsMin = nullptr, Vector3f *pBoundsMax = nullptr, uint32_t *pTriangles = nullptr,
                    const char *pLogTag = "ObjMesh" );

// Destroys the device resources of the draws (buffers, textures) and clears
// the list. Null-safe: gpRender may already be gone at shutdown.
void UnloadObjDraws( std::vector<ObjMeshDraw> &draws );

// --- the parse half (the .3dg authoring of the composed bank build) ---------

// One material of a .mtl (the fields the port uses; `d` / `Tr` fold into Alpha).
struct ObjMaterialDesc {
    std::string Name;
    std::string Texture; // map_Kd, relative to the .obj folder
    Vector4f Diffuse = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
    float Alpha = 1.0f; // below 1 the material is alpha blended (glass)
};

// One `usemtl` group: the mesh already in the port's draw convention (X
// mirrored, winding reversed - the same law the OBJ draw route applies at
// load), one interleaved vertex per position/uv/normal triple, triangle list.
struct ObjVertex {
    float Position[ 3 ];
    float Normal[ 3 ];
    float UV[ 2 ];
};
struct ObjGroupDesc {
    std::string Material; // the .mtl material name ("" when the file had none)
    std::vector<ObjVertex> Vertices;
    std::vector<uint32_t> Indices;
};

// Parses the OBJ text (+ the .mtl text when it has one): one group per
// `usemtl`, outGroups in file order. False when there is no triangle at all.
bool ParseObjMeshes( const std::string &objText, const std::string &mtlText, std::vector<ObjGroupDesc> &outGroups,
                     std::vector<ObjMaterialDesc> &outMaterials );

// Convenience: reads <folder>/<name>.obj (+ its .mtl) through the VFS and
// parses it. False when the OBJ is missing or has no triangle.
bool LoadObjMeshes( const char *pFolder, const char *pObjName, std::vector<ObjGroupDesc> &outGroups,
                    std::vector<ObjMaterialDesc> &outMaterials );
