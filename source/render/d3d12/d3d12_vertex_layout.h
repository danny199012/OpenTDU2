#pragma once

#include "render/d3d12/d3d12_scene.h"
#include "render/render_api.h"

#include <cstdint>
#include <vector>

struct RawVertexAttribute;
struct TranslatedShader;

// The vertex input of one program, as Direct3D 12 needs it (docs/backends.md
// section 3.10, phase 3b step 2).
//
// On Vulkan EdenRenderer::getPipeline builds this list per pipeline from two
// sources: the vertex shader's dcl declarations (which semantics it reads) and
// the mesh's own .3DG declaration (where those semantics live - stream, storage
// format, byte offset). Direct3D 12 wants the same list plus one stride per
// stream, and it wants it *complete*: a shader input with no element in the
// input layout is refused by CreateGraphicsPipelineState ("Input signature is
// not compatible with the input layout"), so a dcl the mesh does not carry
// still needs a slot. The Vulkan path solves that with a synthetic stream
// (eden_renderer.cpp kDefaultAttributeBinding); this does the same with one
// float4 per unmatched dcl in a stream of its own, which Direct3D 12 reads as
// zero when no vertex buffer is bound there.
struct D3D12VertexLayout {
    std::vector<D3D12Scene::Attribute> Attributes;
    // Strides[stream] is the stride to pass to IASetVertexBuffers for that
    // stream (a stream nothing binds still needs a stride in the layout).
    std::vector<uint32_t> Strides;
    uint32_t NumStreams = 0;
    // dcls the mesh did not carry: they read zero in a draw.
    uint32_t NumFallbackAttributes = 0;
    // Storage formats with no DXGI equivalent (the two A2B10G10R10 SNORM
    // packings): mapped to R10G10B10A2_UNORM so a layout exists, but a mesh
    // that really uses them has to expand the stream at upload or its normals
    // lose their sign (docs/backends.md section 6).
    uint32_t NumExpandedFormats = 0;

    inline bool isEmpty() const { return Attributes.empty(); }
};

// VAF_* -> DXGI_FORMAT as a plain integer (this header keeps no dxgi.h), 0
// (DXGI_FORMAT_UNKNOWN) when the format has no Direct3D 12 equivalent at all.
uint32_t D3D12GetVertexFormat( eVertexAttributeFormat format );

// Builds the layout of 'vs'.
//
//  * with a mesh's declaration (pAttributes != nullptr): every dcl is matched
//    by usage *and* usage index, and its stream / format / offset are the
//    .3DG's; a dcl without a match goes to the fallback stream described above.
//  * without one (pAttributes == nullptr and no streams - the pipeline tally,
//    or a program drawn before its mesh is known): each dcl gets a plausible
//    format in stream 0, in declaration order.
//
// Returns false only when the vertex stage declares no dcl at all (a
// screen-space program, whose pipeline has an empty input layout and no
// vertex buffer).
bool D3D12BuildVertexLayout( const TranslatedShader &vs,
                             const RawVertexAttribute *pAttributes,
                             uint32_t numAttributes,
                             const uint32_t *pMeshStrides,
                             uint32_t numMeshStreams,
                             D3D12VertexLayout &out );
