#pragma once

#include "render/3dg.h"
#include "render/d3d12/d3d12_scene.h"
#include "render/render_api.h"

#include <cstdint>
#include <vector>

// A `.3DG` primitive's vertex data, ready to upload (docs/backends.md 3.12,
// phase 3b step 2).
//
// The engine's own streams go straight to Direct3D 12 for every format it can
// describe (a `R32G32B32` position, a `R16G16` texture coordinate, a
// `B8G8R8A8` colour). Three storage formats have no DXGI equivalent and are
// *expanded*, which changes the layout: the A2B10G10R10 SNORM packings a normal
// or tangent is stored in (DXGI's R10G10B10A2 loses the sign) and D3D9's
// UBYTE4 bone index, which Vulkan reads as UScaled - the byte as a float - and
// Direct3D 12 has no format for. When any attribute needs that, the primitive is
// rebuilt into ONE interleaved stream of float formats; when none does, the raw
// streams are passed through untouched (the common case, and the cheap one).
struct D3D12MeshData {
    std::vector<uint8_t> Streams[D3D12Scene::Mesh::kMaxStreams];
    uint32_t Strides[D3D12Scene::Mesh::kMaxStreams] = {};
    uint32_t NumStreams = 0;
    // The declaration to build the input layout from: the primitive's own
    // attributes, or the rewritten float ones after an expansion.
    std::vector<RawVertexAttribute> Attributes;
    // The triangle list (empty for a heightfield, whose indices the renderer
    // generates while it walks the grid).
    std::vector<uint32_t> Indices;
    uint32_t NumVertices = 0;
    // How many attributes went through the expansion - 0 means the streams are
    // the primitive's own bytes.
    uint32_t NumExpandedAttributes = 0;
};

// True for the formats D3D12GetVertexFormat() can only approximate
// (A2B10G10R10 SNORM, UBYTE4 USCALED bone indices).
bool D3D12NeedsFormatExpansion( eVertexAttributeFormat format );

// Builds the upload-ready data for one primitive. Never fails on a format: an
// attribute whose storage format is unknown to this backend keeps its raw bytes
// and is reported in NumExpandedAttributes == 0 with the DXGI format 0 in the
// layout, which D3D12BuildVertexLayout treats as "not carried" (the fallback
// stream) - a wrong-looking vertex is better than a refused pipeline, and the
// log names it.
bool D3D12PrepareMesh( const GeometryPrimitive &primitive, D3D12MeshData &out );

// -d3d12-meshconvtest: the expansion checked without a device or game data
// (docs/backends.md 3.12). Builds primitives in every format the converter
// handles, runs them through D3D12PrepareMesh and compares the decoded floats
// with the values that were packed: a SNORM normal's -1 must come out as -1.0,
// not as 0.5 or 0, and a bone index's 255 as 255.0, not 1.0. Returns false on a
// single mismatch, naming it.
bool D3D12RunMeshConvertTest();
