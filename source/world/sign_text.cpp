#include "shared.h"
#include "sign_text.h"

#include "flash/gs_flash.h"
#include "filesystem/bank.h"
#include "render/2db.h"
#include "render/3dg.h"
#include "render/font.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"

#include <fstream>
#include <cstring>

// Direction-sign faces (docs/re/sign_text_spec.md): the retail plates are
// baked blank in the rs_maps atlas (commonworld.bnk) and each sign's text
// travels in the type-4 payload of the sector .pmi A list. The original
// renderer composed the face CPU-side (td_sign has no text constants); this
// cache recomposes it: full atlas copy + text stamped into the blank plate
// bands with the frontend font, CPU-encoded DXT1 + mips, uploaded through
// TextureCache::createFrom2DB. EdenWorld swaps it into the sign material's
// rs_maps slot per instance and restores the shared atlas right after.

namespace {
    // Blank plate bands in the rs_maps atlas, mip 0 pixels (probed,
    // sign_text_spec 1; the baked outlines stay clear of the text).
    constexpr int32_t kPlateGreen[4] = { 18, 21, 740, 62 }; // x0, y0, x1, y1
    constexpr int32_t kPlateWhite[4] = { 18, 222, 740, 294 };
    constexpr uint32_t kAtlasSize = 1024;
    constexpr uint32_t kAtlasMips = 9; // 1024..4, the original's mip count
    constexpr size_t kMaxFaces = 160;  // LRU cap (~110 MB of 1024x1024 DXT1)
    constexpr uint8_t kTextShade = 16; // the plate text is black

    bool decodeCharset( uint8_t byte, std::string &out ) {
        if ( byte == 0 ) {
            out += ' ';
        } else if ( byte <= 0x1a ) {
            out += static_cast<char>( 'A' + byte - 1 );
        } else if ( byte <= 0x24 ) {
            out += static_cast<char>( '0' + byte - 0x1b );
        } else if ( byte <= 0x2a ) {
            // 0x25..0x2a: 10..15, a 1 then the units digit (sign_text_spec 2).
            // The values were checked against the island: fitting each
            // destination's stated kilometres to the point whose straight-line
            // distance matches them gives a 15% median error with 10..15 and
            // 36% with tens (Aeropuerto 8% vs 31%, Dalt Vila 4% vs 41%).
            out += '1';
            out += static_cast<char>( '0' + byte - 0x25 );
        } else if ( byte <= 0x44 ) {
            out += static_cast<char>( 'a' + byte - 0x2b );
        } else if ( byte == 0x47 ) {
            out += "\xc3\xb1"; // ñ
        } else if ( byte == 0x50 ) {
            out += "\xc3\x80"; // À
        } else {
            return false;
        }
        return true;
    }

    bool isDigitCode( uint8_t byte ) { return byte >= 0x1b && byte <= 0x2a; }

    std::string decodeDistance( uint8_t byte ) {
        std::string out;
        if ( isDigitCode( byte ) ) {
            decodeCharset( byte, out );
        }
        return out;
    }

    // A name window: characters, 0x00 = space, zero padded; padding is trimmed.
    bool isIconCode( uint8_t byte ) { return byte >= 0x48 && byte <= 0x4b; }

    std::string decodeWindow( const uint8_t *pBytes, uint32_t size, uint8_t *pOutIcon = nullptr ) {
        std::string out;
        for ( uint32_t i = 0; i < size; i++ ) {
            // A pictogram byte (spec 5.1) starts the row: drawn as an icon.
            if ( isIconCode( pBytes[i] ) ) {
                if ( pOutIcon != nullptr && *pOutIcon == 0 ) {
                    *pOutIcon = pBytes[i];
                }
                continue;
            }
            if ( !decodeCharset( pBytes[i], out ) && IsSignTextLogging() ) {
                OTDU_LOG_INFO( "SignText: byte %02x in a name window is not in "
                               "the charset\n",
                               pBytes[i] );
            }
        }
        const size_t begin = out.find_first_not_of( ' ' );
        if ( begin == std::string::npos ) {
            return std::string();
        }
        const size_t end = out.find_last_not_of( ' ' );
        return out.substr( begin, end - begin + 1 );
    }

    // Plate text zones of a variant (docs/re/sign_text_spec.md 1): the quads of
    // the plate primitives sample sub-rects of the rs_maps blank plates; their
    // UV rects are where the text must land. Triangles of the converted list
    // merge through shared edges, side-by-side quads of one plate merge on
    // overlapping v bands, the front/back duplicates dedupe on identical UV
    // rects.
    SignLayout BuildSignLayoutImpl( const uint8_t *pData, uint32_t size, SignFamily family ) {
        SignLayout layout;
        Render3DG geometry;
        if ( !geometry.parse( pData, size, false ) ) {
            return layout;
        }
        struct RawQuad {
            float u0 = 1.0f, v0 = 1.0f, u1 = 0.0f, v1 = 0.0f;
            float top = -1e9f;
            float bottom = 1e9f;
            // How u runs along the model x (the reading direction of a plate
            // that faces -Z): the u at the plate's min / max x, and the mean
            // normal z (front faces only decide the direction).
            float minX = 1e9f, maxX = -1e9f;
            float uAtMinX = 0.0f, uAtMaxX = 0.0f;
            float normalZ = 0.0f;
            int32_t numVertices = 0;
        };
        std::vector<RawQuad> quads;
        for ( const auto &geometryPair : geometry.getGeometries() ) {
            for ( const GeometryPrimitive &prim : geometryPair.second.Primitives ) {
                const std::vector<MeshVertex> &verts = prim.Vertices;
                const std::vector<uint32_t> &indices = prim.Indices;
                if ( verts.empty() || indices.size() < 3 ) {
                    continue;
                }
                const uint32_t numTris = static_cast<uint32_t>( indices.size() / 3 );
                // DSU over triangles, joined on a shared vertex-index edge.
                std::vector<int32_t> parent( numTris );
                for ( uint32_t t = 0; t < numTris; t++ ) {
                    parent[t] = static_cast<int32_t>( t );
                }
                const auto findRoot = [&]( int32_t t ) {
                    while ( parent[t] != t ) {
                        parent[t] = parent[parent[t]];
                        t = parent[t];
                    }
                    return t;
                };
                std::unordered_map<uint64_t, int32_t> edges;
                const auto edgeKey = []( uint32_t a, uint32_t b ) {
                    return a < b ? ( static_cast<uint64_t>( a ) << 32 ) | b : ( static_cast<uint64_t>( b ) << 32 ) | a;
                };
                for ( uint32_t t = 0; t < numTris; t++ ) {
                    for ( int32_t k = 0; k < 3; k++ ) {
                        const uint64_t key = edgeKey( indices[t * 3 + k], indices[t * 3 + ( k + 1 ) % 3] );
                        auto it = edges.find( key );
                        if ( it != edges.end() ) {
                            const int32_t a = findRoot( it->second ), b = findRoot( static_cast<int32_t>( t ) );
                            if ( a != b ) {
                                parent[a] = b;
                            }
                        } else {
                            edges.emplace( key, static_cast<int32_t>( t ) );
                        }
                    }
                }
                std::unordered_map<int32_t, RawQuad> merged;
                for ( uint32_t t = 0; t < numTris; t++ ) {
                    RawQuad &quad = merged[findRoot( static_cast<int32_t>( t ) )];
                    for ( int32_t k = 0; k < 3; k++ ) {
                        const MeshVertex &vertex = verts[indices[t * 3 + k]];
                        quad.u0 = Min( quad.u0, vertex.Texcoord[0] );
                        quad.v0 = Min( quad.v0, vertex.Texcoord[1] );
                        quad.u1 = Max( quad.u1, vertex.Texcoord[0] );
                        quad.v1 = Max( quad.v1, vertex.Texcoord[1] );
                        quad.top = Max( quad.top, vertex.Position[1] );
                        quad.bottom = Min( quad.bottom, vertex.Position[1] );
                        if ( vertex.Position[0] < quad.minX - 1e-4f ) {
                            quad.minX = vertex.Position[0];
                            quad.uAtMinX = vertex.Texcoord[0];
                        }
                        if ( vertex.Position[0] > quad.maxX + 1e-4f ) {
                            quad.maxX = vertex.Position[0];
                            quad.uAtMaxX = vertex.Texcoord[0];
                        }
                        quad.normalZ += vertex.Normal[2];
                        quad.numVertices++;
                    }
                }
                for ( const auto &pair : merged ) {
                    quads.push_back( pair.second );
                }
            }
        }
        // Keep plate-sized rects. The rs_maps sheet carries several blank
        // plate bars, one per sign family (all measured on the atlas mip 0
        // and on the models' front quads):
        //   v 0.017..0.065  green arrow plate          (rs_dir b/c/d header)
        //   v 0.089..0.132  white plate, yellow tip    (rs_tdirib*)
        //   v 0.151..0.193  white plate                (rs_dist*)
        //   v 0.213..0.260  white plate                (rs_dir*, rs_dist*)
        //   v 0.585..0.635  white plate, red border    (rs_twnin/rs_twnout)
        //   v 0.303..0.644  tall highway panel        (rs_hwdist*, one row
        //                                                per payload row)
        // Drop the thin border slivers and everything else (baked art).
        std::vector<RawQuad> plates;
        for ( RawQuad &quad : quads ) {
            const float uSpan = quad.u1 - quad.u0;
            const float vSpan = quad.v1 - quad.v0;
            const bool bNarrowBand = ( quad.v0 >= 0.010f && quad.v1 <= 0.078f ) ||   // green
                                     ( quad.v0 >= 0.080f && quad.v1 <= 0.140f ) ||   // white, yellow tip
                                     ( quad.v0 >= 0.140f && quad.v1 <= 0.200f ) ||   // white
                                     ( quad.v0 >= 0.200f && quad.v1 <= 0.300f ) ||   // white
                                     ( quad.v0 >= 0.575f && quad.v1 <= 0.640f );     // white, red border
            const bool bNarrowPlate = bNarrowBand && vSpan > 0.006f && vSpan < 0.062f;
            // The highway panels map one tall quad over the whole face; it is
            // split into one sub-row per payload row at compose time.
            const bool bTallPlate = quad.v0 >= 0.295f && quad.v1 <= 0.655f && vSpan > 0.15f;
            if ( ( bNarrowPlate || bTallPlate ) && uSpan > 0.03f ) {
                plates.push_back( quad );
                if ( IsSignTextLogging() ) {
                    OTDU_LOG_INFO( "SignText:   plate quad u %.3f..%.3f v "
                                   "%.3f..%.3f y %.2f..%.2f x %.2f (u %.3f) .. "
                                   "%.2f (u %.3f) nz %.2f\n",
                                   quad.u0, quad.u1, quad.v0, quad.v1, quad.bottom, quad.top, quad.minX, quad.uAtMinX,
                                   quad.maxX, quad.uAtMaxX, quad.normalZ / Max( 1, quad.numVertices ) );
                }
            }
        }
        // Group into plate rows: quads whose v bands overlap belong to one row
        // (side-by-side splits of one plate); identical rects are the
        // front/back duplicates and dedupe here too. Rows have disjoint v
        // bands, so one pass over the v-sorted quads suffices.
        struct Zone {
            float u0, v0, u1, v1, top, bottom;
            // Quads whose u grows with model x (read straight) / shrinks
            // (read mirrored); both = a folded plate (see below).
            int32_t numWith = 0, numAgainst = 0;
            float foldX = 0.0f;
            bool bFoldPositiveX = true;
            float withCenterX = 0.0f, againstCenterX = 0.0f;
            float againstMinX = 0.0f, againstMaxX = 0.0f;
        };
        const auto addDirection = []( Zone &zone, const RawQuad &quad ) {
            const float du = quad.uAtMaxX - quad.uAtMinX;
            if ( std::abs( du ) < 0.25f * ( quad.u1 - quad.u0 ) || quad.maxX - quad.minX < 0.1f ) {
                return; // no clear direction (the tall highway panel)
            }
            const float centerX = ( quad.minX + quad.maxX ) * 0.5f;
            if ( du > 0.0f ) {
                zone.numWith++;
                zone.withCenterX = centerX;
            } else {
                zone.numAgainst++;
                zone.againstCenterX = centerX;
                zone.againstMinX = quad.minX;
                zone.againstMaxX = quad.maxX;
            }
        };
        std::sort( plates.begin(), plates.end(), []( const RawQuad &a, const RawQuad &b ) { return a.v0 < b.v0; } );
        std::vector<Zone> zones;
        for ( RawQuad &quad : plates ) {
            bool bMerged = false;
            for ( Zone &zone : zones ) {
                const float overlap = Min( zone.v1, quad.v1 ) - Max( zone.v0, quad.v0 );
                const float minSpan = Min( zone.v1 - zone.v0, quad.v1 - quad.v0 );
                // Same plate row: v bands overlap and the plates sit at the
                // same height (a split of one row, or its back face). Stacked
                // plates share their atlas rect but sit at different heights -
                // they stay separate zones so every payload row gets its own
                // plate.
                if ( overlap > 0.4f * minSpan && std::abs( zone.top - quad.top ) < 0.15f ) {
                    zone.u0 = Min( zone.u0, quad.u0 );
                    zone.v0 = Min( zone.v0, quad.v0 );
                    zone.u1 = Max( zone.u1, quad.u1 );
                    zone.v1 = Max( zone.v1, quad.v1 );
                    zone.top = Max( zone.top, quad.top );
                    zone.bottom = Min( zone.bottom, quad.bottom );
                    addDirection( zone, quad );
                    bMerged = true;
                    break;
                }
            }
            if ( !bMerged ) {
                Zone zone{ quad.u0, quad.v0, quad.u1, quad.v1, quad.top, quad.bottom };
                addDirection( zone, quad );
                zones.push_back( zone );
            }
        }
        // Rows read top down = biggest model-space Y first.
        std::sort( zones.begin(), zones.end(), []( const Zone &a, const Zone &b ) { return a.top > b.top; } );
        for ( const Zone &zone : zones ) {
            SignQuadLayout quad;
            quad.U0 = zone.u0;
            quad.V0 = zone.v0;
            quad.U1 = zone.u1;
            quad.V1 = zone.v1;
            quad.Top = zone.top;
            quad.Bottom = zone.bottom;
            // The stamp orientation per family, measured on the model
            // vertices (spec 4.9): rs_dir* and the town plates sample their
            // rect with u against the reading direction, the dist/tdirib/hw
            // plates with it; only the town plates read v bottom up.
            // The u direction is measured per zone on the plate vertices
            // (2026-11-05): u growing with model x reads straight (the plates
            // face +Z, the viewer's right is +x). rs_dirl* and the town plates
            // run against it, rs_dirr* / dist / tdirib with it - the session-61
            // family rule flipped every rs_dir plate and so mirrored all the
            // rs_dirr* signs. A zone with quads both ways is a FOLDED plate:
            // the rs_dist green header maps u 0.005 -> 0.166 from each end to
            // the middle (one half-plate of texels, mirrored), which cannot
            // carry text; RemapStackedPlateRows unfolds it.
            if ( zone.numWith > 0 && zone.numAgainst > 0 ) {
                quad.FlipU = false;
                quad.bFolded = true;
                quad.FoldU = zone.u1;
                quad.bFoldPositiveX = zone.againstCenterX > zone.withCenterX;
                quad.FoldX = quad.bFoldPositiveX ? zone.againstMinX : zone.againstMaxX;
            } else if ( zone.numWith > 0 || zone.numAgainst > 0 ) {
                quad.FlipU = zone.numAgainst > 0;
            } else {
                quad.FlipU = ( family == SignFamily::Unknown || family == SignFamily::Dir ||
                               family == SignFamily::Twn );
            }
            quad.FlipV = ( family == SignFamily::Twn );
            // The highway panel is a rotated cell: across the panel (model x)
            // runs +v, up the panel (model y) runs +u (rs_hwdist_c vertices:
            // x -2.36 -> v 0.303, x 1.24 -> v 0.643; y 0.365 -> u 0.859,
            // y 2.028 -> u 0.990), so its rows stamp rotated.
            quad.bRotated = family == SignFamily::Hw && zone.v1 - zone.v0 > 0.15f;
            quad.SrcV0 = zone.v0;
            quad.SrcV1 = zone.v1;
            layout.Quads.push_back( quad );
        }
        layout.bValid = !layout.Quads.empty();
        // The retail photos show distance digits only on the distance plates
        // (rs_dist/rs_hwdist) and the town signs ("EIVISSA 8"); the direction
        // and TdirIb families leave the payload's distance bytes undrawn.
        // The town plates are off too: their only "distance" is the layout
        // code byte, 8 on all 230 town signs of the island - a model code,
        // not kilometres (the per-family census, spec 6).
        layout.bDistanceRows =
            ( family == SignFamily::Dist || family == SignFamily::Hw || family == SignFamily::Unknown );
        layout.Family = family;
        return layout;
    }
} // namespace

SignLayout BuildSignLayout( const uint8_t *pData, uint32_t size, SignFamily family ) {
    return BuildSignLayoutImpl( pData, size, family );
}

namespace {
    // The uv0 attribute of a primitive's raw stream (float32 pairs - the sign
    // banks' format), or null.
    const RawVertexAttribute *findRawUv( const GeometryPrimitive &prim ) {
        for ( const RawVertexAttribute &attribute : prim.RawAttributes ) {
            if ( attribute.Usage == 5 /* D3DDECLUSAGE_TEXCOORD */ && attribute.UsageIndex == 0 &&
                 attribute.Format == VAF_TEXCOORD_R32G32_SFLOAT ) {
                if ( prim.RawStrides[attribute.Stream] > 0 &&
                     prim.RawStreams[attribute.Stream].size() >=
                         size_t( prim.NumRawVertex ) * prim.RawStrides[attribute.Stream] ) {
                    return &attribute;
                }
                return nullptr;
            }
        }
        return nullptr;
    }

    // A folded plate (BuildSignLayout: the rs_dist green header) samples its
    // atlas cell from both ends to the middle. The mirrored half is rewritten
    // to continue the cell instead (u' = 2 fold - u), so the plate maps one
    // continuous strip that can carry a row; the composer mirror-copies the
    // cell's texels over that strip so the plate itself looks unchanged.
    void UnfoldMirroredPlates( Render3DG &geometry, SignLayout &layout ) {
        for ( SignQuadLayout &quad : layout.Quads ) {
            if ( !quad.bFolded || quad.FoldU <= quad.U0 ) {
                continue;
            }
            int32_t numMoved = 0;
            for ( auto &geometryPair : geometry.getGeometries() ) {
                for ( GeometryPrimitive &prim : geometryPair.second.Primitives ) {
                    const RawVertexAttribute *pUv = findRawUv( prim );
                    for ( uint32_t vi = 0; vi < prim.Vertices.size() && vi < prim.NumRawVertex; vi++ ) {
                        MeshVertex &vertex = prim.Vertices[vi];
                        if ( vertex.Texcoord[0] < quad.U0 - 0.003f || vertex.Texcoord[0] > quad.FoldU + 0.003f ||
                             vertex.Texcoord[1] < quad.V0 - 0.003f || vertex.Texcoord[1] > quad.V1 + 0.003f ) {
                            continue;
                        }
                        const bool bMirrorSide = quad.bFoldPositiveX ? vertex.Position[0] > quad.FoldX + 0.002f
                                                                     : vertex.Position[0] < quad.FoldX - 0.002f;
                        if ( !bMirrorSide ) {
                            continue;
                        }
                        vertex.Texcoord[0] = 2.0f * quad.FoldU - vertex.Texcoord[0];
                        if ( pUv != nullptr ) {
                            const size_t rawOffset = size_t( vi ) * prim.RawStrides[pUv->Stream] + pUv->Offset;
                            memcpy( prim.RawStreams[pUv->Stream].data() + rawOffset, &vertex.Texcoord[0],
                                    sizeof( float ) );
                        }
                        numMoved++;
                    }
                }
            }
            quad.U1 = 2.0f * quad.FoldU - quad.U0;
            if ( IsSignTextLogging() ) {
                OTDU_LOG_INFO( "SignText: unfolded a mirrored plate at u %.3f "
                               "(%d vertices), zone u %.3f..%.3f\n",
                               quad.FoldU, numMoved, quad.U0, quad.U1 );
            }
        }
    }
} // namespace

void RemapStackedPlateRows( Render3DG &geometry, SignLayout &layout ) {
    UnfoldMirroredPlates( geometry, layout );
    // The blank white plate bars of rs_maps, in the order a stacked model
    // reads top down (v ranges measured on the atlas mip 0).
    struct PlateBar {
        float V0, V1;
    };
    static const PlateBar kBars[3] = { { 0.089f, 0.132f }, { 0.151f, 0.193f }, { 0.213f, 0.260f } };
    std::vector<bool> visited( layout.Quads.size(), false );
    for ( size_t i = 0; i < layout.Quads.size(); i++ ) {
        if ( visited[i] ) {
            continue;
        }
        const SignQuadLayout &first = layout.Quads[i];
        visited[i] = true;
        std::vector<size_t> members{ i };
        for ( size_t j = i + 1; j < layout.Quads.size(); j++ ) {
            if ( visited[j] ) {
                continue;
            }
            const SignQuadLayout &other = layout.Quads[j];
            if ( std::abs( other.U0 - first.U0 ) < 0.004f && std::abs( other.V0 - first.V0 ) < 0.004f &&
                 std::abs( other.U1 - first.U1 ) < 0.004f && std::abs( other.V1 - first.V1 ) < 0.004f ) {
                members.push_back( j );
                visited[j] = true;
            }
        }
        if ( members.size() < 2 || members.size() > 3 || first.V0 < 0.080f ) {
            continue;
        }
        std::sort( members.begin(), members.end(),
                   [&layout]( size_t a, size_t b ) { return layout.Quads[a].Top > layout.Quads[b].Top; } );
        const float sourceV0 = first.V0;
        // Keep the plate's look: a yellow-tip family (rs_tdirib*) takes the
        // yellow-tip bar first; plain white plates take the plain bars and only
        // fall back to the yellow-tip bar for a third row.
        const bool bYellowSource = first.V0 >= 0.080f && first.V0 <= 0.140f;
        static const int32_t kYellowOrder[3] = { 0, 1, 2 };
        static const int32_t kPlainOrder[3] = { 1, 2, 0 };
        const int32_t *pBarOrder = bYellowSource ? kYellowOrder : kPlainOrder;
        const float sourceSpan = Max( first.V1 - first.V0, 1e-4f );
        const float sourceU0 = first.U0 - 0.003f;
        const float sourceU1 = first.U1 + 0.003f;
        const auto inSourceWindow = [&]( const MeshVertex &vertex ) {
            return vertex.Texcoord[0] >= sourceU0 && vertex.Texcoord[0] <= sourceU1 &&
                   vertex.Texcoord[1] >= first.V0 - 0.003f && vertex.Texcoord[1] <= first.V1 + 0.003f;
        };
        // Not every stacked plate reads the cell the same way up: rs_tdirib_c's
        // middle plate maps it upside down (v 0.0886 at its bottom edge,
        // 0.1316 at its top; the other two plates the reverse - measured on
        // the model vertices). Per plate: the mean v of its top edge vs its
        // bottom edge. An inverted plate is un-flipped by the remap below, so
        // its row reads upright like the others.
        std::vector<bool> inverted( members.size(), false );
        {
            std::vector<double> topSum( members.size(), 0.0 ), bottomSum( members.size(), 0.0 );
            std::vector<int32_t> topCount( members.size(), 0 ), bottomCount( members.size(), 0 );
            for ( auto &geometryPair : geometry.getGeometries() ) {
                for ( const GeometryPrimitive &prim : geometryPair.second.Primitives ) {
                    for ( const MeshVertex &vertex : prim.Vertices ) {
                        if ( !inSourceWindow( vertex ) ) {
                            continue;
                        }
                        // A vertex on the edge of two plates (rs_dirl_d's
                        // adjoining plates) says nothing about either.
                        int32_t numEdges = 0;
                        for ( size_t k = 0; k < members.size(); k++ ) {
                            const SignQuadLayout &member = layout.Quads[members[k]];
                            numEdges += std::abs( vertex.Position[1] - member.Top ) < 0.004f ? 1 : 0;
                            numEdges += std::abs( vertex.Position[1] - member.Bottom ) < 0.004f ? 1 : 0;
                        }
                        if ( numEdges != 1 ) {
                            continue;
                        }
                        for ( size_t k = 0; k < members.size(); k++ ) {
                            const SignQuadLayout &member = layout.Quads[members[k]];
                            if ( std::abs( vertex.Position[1] - member.Top ) < 0.004f ) {
                                topSum[k] += vertex.Texcoord[1];
                                topCount[k]++;
                            } else if ( std::abs( vertex.Position[1] - member.Bottom ) < 0.004f ) {
                                bottomSum[k] += vertex.Texcoord[1];
                                bottomCount[k]++;
                            }
                        }
                    }
                }
            }
            for ( size_t k = 0; k < members.size(); k++ ) {
                if ( topCount[k] > 0 && bottomCount[k] > 0 ) {
                    inverted[k] = topSum[k] / topCount[k] > bottomSum[k] / bottomCount[k];
                }
            }
        }
        // Rewrite the texture coordinates per physical plate. The plate a
        // vertex belongs to is told apart by the plate's model-space Y span
        // ([Bottom, Top] of each member; nearest-Top alone tears the plates'
        // adjoining bevel rows), and the new v is written both into the
        // parsed vertex (bounds/collision) and into the RAW vertex stream -
        // the GPU draws from RawStreams, the parsed copy never reaches it.
        for ( auto &geometryPair : geometry.getGeometries() ) {
            for ( GeometryPrimitive &prim : geometryPair.second.Primitives ) {
                // The uv0 attribute of the raw layout: float32 pairs only (the
                // sign banks' format); anything else keeps the parsed copy.
                const RawVertexAttribute *pUv = nullptr;
                for ( const RawVertexAttribute &attribute : prim.RawAttributes ) {
                    if ( attribute.Usage == 5 /* D3DDECLUSAGE_TEXCOORD */ && attribute.UsageIndex == 0 &&
                         attribute.Format == VAF_TEXCOORD_R32G32_SFLOAT ) {
                        pUv = &attribute;
                        break;
                    }
                }
                const bool bPatchRaw = pUv != nullptr && prim.RawStrides[pUv->Stream] > 0 &&
                                       prim.RawStreams[pUv->Stream].size() >=
                                           size_t( prim.NumRawVertex ) * prim.RawStrides[pUv->Stream];
                for ( uint32_t vi = 0; vi < prim.Vertices.size() && vi < prim.NumRawVertex; vi++ ) {
                    MeshVertex &vertex = prim.Vertices[vi];
                    if ( !inSourceWindow( vertex ) ) {
                        continue;
                    }
                    size_t best = members[0];
                    float bestDistance = 1e9f;
                    for ( size_t k = 0; k < members.size(); k++ ) {
                        const SignQuadLayout &member = layout.Quads[members[k]];
                        // The plate each vertex belongs to: every member maps
                        // the SAME shared rect across its own [Bottom, Top]
                        // span, so the member whose linear v(y) matches the
                        // vertex's v is the owner (nearest-Top alone tears the
                        // adjoining bevel rows; the y spans overlap there).
                        const float span = Max( member.Top - member.Bottom, 1e-3f );
                        float fraction = ( member.Top - vertex.Position[1] ) / span;
                        if ( inverted[k] ) {
                            fraction = 1.0f - fraction;
                        }
                        const float expectedV = member.V0 + fraction * ( member.V1 - member.V0 );
                        float distance = std::abs( vertex.Texcoord[1] - expectedV );
                        // A plate owns only the vertices of its own height
                        // span.
                        if ( vertex.Position[1] > member.Top + 0.01f || vertex.Position[1] < member.Bottom - 0.01f ) {
                            distance += 1.0f;
                        }
                        if ( distance < bestDistance ) {
                            bestDistance = distance;
                            best = members[k];
                        }
                    }
                    size_t bar = 0;
                    while ( bar + 1 < members.size() && members[bar] != best ) {
                        bar++;
                    }
                    const PlateBar &target = kBars[pBarOrder[bar]];
                    float fraction = ( vertex.Texcoord[1] - sourceV0 ) / sourceSpan;
                    if ( inverted[bar] ) {
                        fraction = 1.0f - fraction; // read the bar upright
                    }
                    vertex.Texcoord[1] = target.V0 + fraction * ( target.V1 - target.V0 );
                    if ( bPatchRaw ) {
                        const size_t rawOffset = size_t( vi ) * prim.RawStrides[pUv->Stream] + pUv->Offset;
                        const float newV = vertex.Texcoord[1];
                        memcpy( prim.RawStreams[pUv->Stream].data() + rawOffset + 4, &newV, sizeof( newV ) );
                    }
                }
            }
        }
        for ( size_t k = 0; k < members.size(); k++ ) {
            layout.Quads[members[k]].V0 = kBars[pBarOrder[k]].V0;
            layout.Quads[members[k]].V1 = kBars[pBarOrder[k]].V1;
        }
        if ( IsSignTextLogging() ) {
            int32_t numInverted = 0;
            for ( bool bInverted : inverted ) {
                numInverted += bInverted ? 1 : 0;
            }
            OTDU_LOG_INFO( "SignText: remapped %zu stacked plate row(s) to bars 0.089/0.151/0.213 (%d upside down)\n",
                           members.size(), numInverted );
        }
    }
}

bool IsSignTextEnabled() {
    static const bool bEnabled = !CmdLine::HasArg( "-nosigntext" );
    return bEnabled;
}

bool IsSignTextLogging() {
    static const bool bLogging = CmdLine::HasArg( "-logsigns" );
    return bLogging;
}

void SignFaceCache::initialize( const BankEntry *pRsMapsEntryIn ) {
    pRsMapsEntry = pRsMapsEntryIn;
    if ( pRsMapsEntry == nullptr ) {
        OTDU_LOG_WARN( "SignText: no rs_maps entry, sign faces stay blank\n" );
        return;
    }
    if ( gpFlash == nullptr || gpFlash->getMainMovie() == nullptr ) {
        return; // headless / no frontend: faces stay blank
    }
    if ( !ensureFontPage() ) {
        return;
    }
    bEnabled = true;
    OTDU_LOG_INFO( "SignText: ready (font '%s', page %ux%u)\n", pFont->getName().c_str(), fontPageWidth,
                   fontPageHeight );
}

bool SignFaceCache::ensureFontPage() {
    if ( pFont != nullptr && !fontPage.empty() ) {
        return true;
    }
    // The retail sign lettering is a plain grotesque (the x360 photos); of the
    // shipped fonts the gauges movie's "Arial" (fla_0001, 1024 x 512, 155
    // glyphs incl. the accented capitals) is the closest. Berlin Sans FB of
    // the main movie is the fallback [INF].
    const Font *pSource = nullptr;
    const FlashMovie *pMovie = nullptr;
    if ( !CmdLine::HasArg( "-signfontberlin" ) ) {
        if ( FlashMovie *pGauges = gpFlash->getMovie( "gauges" ) ) {
            if ( const Font *pArial = pGauges->getFont( "Arial", true ) ) {
                std::vector<uint8_t> page;
                uint32_t width = 0, height = 0;
                if ( pGauges->getFontPagePixels( "Arial", page, width, height ) ) {
                    pSource = pArial;
                    pMovie = pGauges;
                }
            }
        }
    }
    if ( pSource == nullptr ) {
        pSource = gpFlash->getTitleFont();
        pMovie = gpFlash->getMainMovie();
    }
    if ( pSource == nullptr || pMovie == nullptr ) {
        return false;
    }
    fontCopy = *pSource;
    pFont = &fontCopy;
    if ( !pMovie->getFontPagePixels( pFont->getName().c_str(), fontPage, fontPageWidth, fontPageHeight ) ) {
        if ( !bLoggedFontFailure ) {
            bLoggedFontFailure = true;
            OTDU_LOG_WARN( "SignText: no CPU pixels for the '%s' glyph page\n", pFont->getName().c_str() );
        }
        pFont = nullptr;
        return false;
    }
    return true;
}

void SignFaceCache::ensureAtlasBase() {
    if ( !atlasBase.empty() || pRsMapsEntry == nullptr ) {
        return;
    }
    Texture2DB texture;
    if ( !texture.parse( pRsMapsEntry->pData, pRsMapsEntry->Size ) || !IsBlockCompressedFormat( texture.Format ) ||
         texture.Depth != 1 ) {
        OTDU_LOG_WARN( "SignText: rs_maps is not a plain compressed atlas\n" );
        return;
    }
    atlasBase.resize( static_cast<size_t>( texture.Width ) * texture.Height * 4 );
    DecodeBlockCompressed( texture.pTexels, texture.Width, texture.Height, texture.Format, atlasBase.data() );
}

void SignFaceCache::destroy() {
    stopWorker(); // before atlasBase / fontPage go: the worker reads them
    if ( gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        for ( auto &it : faces ) {
            device.destroyTexture( it.second.pTexture );
        }
    }
    faces.clear();
    atlasBase.clear();
    fontPage.clear();
    pFont = nullptr;
    bEnabled = false;
}

uint64_t
SignFaceCache::decodePayload( const uint8_t *pPayload, uint32_t size, std::vector<SignTextRow> &outRows ) const {
    outRows.clear();
    if ( pPayload == nullptr || size < 2 ) {
        return 0;
    }
    // FNV-1a 64: stable face key independent of the (sector bound) payload
    // memory.
    uint64_t hash = 0xcbf29ce484222325ull;
    for ( uint32_t i = 0; i < size; i++ ) {
        hash = ( hash ^ pPayload[i] ) * 0x100000001b3ull;
    }

    // The layout code + extraSize pick the row windows (sign_text_spec 2).
    const uint8_t code = pPayload[0];
    const uint8_t *pBody = pPayload + 1;
    const uint32_t bodySize = size - 1;
    const auto addRow = [&]( uint32_t offset, uint32_t window, uint32_t distanceOffset ) {
        if ( offset + window > bodySize ) {
            return;
        }
        SignTextRow row;
        row.Name = decodeWindow( pBody + offset, window, &row.Icon );
        if ( distanceOffset > 0 && distanceOffset < bodySize ) {
            row.Distance = decodeDistance( pBody[distanceOffset] );
            row.DistanceCode = pBody[distanceOffset];
        }
        if ( !row.Name.empty() ) {
            outRows.push_back( std::move( row ) );
        }
    };
    const auto applyCodeDistance = [&]() {
        // Faces without trailing distance bytes carry the distance in the
        // code itself [INF, sign_text_spec 2].
        // The code is the whole plate's distance: the x360 photo of the
        // Dist_D "Centre / Platja / Puerto" sign (code 0x1c) draws "1" on
        // all three rows. Drawn only where bDistanceRows (dist / hwdist);
        // the dir / town families' code is a model id (spec 6).
        if ( !isDigitCode( code ) ) {
            return;
        }
        for ( SignTextRow &row : outRows ) {
            if ( row.Distance.empty() ) {
                row.Distance = decodeDistance( code );
                row.DistanceCode = code;
            }
        }
    };
    switch ( size ) {
    case 19:
        addRow( 0, 18, 0 );
        applyCodeDistance();
        break;
    case 20:
        addRow( 0, 18, 18 );
        break;
    case 22:
        addRow( 0, 20, 20 );
        break;
    case 37:
        addRow( 0, 18, 0 );
        addRow( 18, 18, 0 );
        applyCodeDistance();
        break;
    case 39:
        addRow( 0, 18, 18 );
        addRow( 19, 18, 37 );
        break;
    case 43:
        addRow( 0, 20, 20 );
        addRow( 21, 20, 42 );
        break;
    case 55:
        addRow( 0, 18, 0 );
        addRow( 18, 18, 0 );
        addRow( 36, 18, 0 );
        applyCodeDistance();
        break;
    case 58:
        addRow( 0, 18, 18 );
        addRow( 19, 18, 37 );
        addRow( 38, 18, 56 );
        break;
    case 64:
        addRow( 0, 20, 20 );
        addRow( 21, 20, 42 );
        addRow( 42, 20, 63 );
        break;
    default: {
        // Unknown payload size: the whole body as one name.
        SignTextRow row;
        row.Name = decodeWindow( pBody, bodySize );
        if ( !row.Name.empty() ) {
            outRows.push_back( std::move( row ) );
        }
        break;
    }
    }
    // An empty distance byte repeats the row above: the x360 photo of the
    // Dist_D "Centre 1 / Platja / Puerto" sign (bytes 0x1c, 0, 0 - the only
    // payloads with those three names) draws "1" on all three rows [INF from
    // that photo]. The TdirIb arrow code follows the same rule.
    for ( size_t i = 1; i < outRows.size(); i++ ) {
        if ( outRows[i].Distance.empty() && outRows[i].DistanceCode == 0 ) {
            outRows[i].Distance = outRows[i - 1].Distance;
            outRows[i].DistanceCode = outRows[i - 1].DistanceCode;
        }
    }
    return hash;
}

GPUTexture *SignFaceCache::getFace( uint64_t payloadHash,
                                    const std::vector<SignTextRow> &rows,
                                    char variant,
                                    const SignLayout &layout ) {
    if ( !bEnabled || !IsSignTextEnabled() || rows.empty() ) {
        return nullptr;
    }
    // The layout is part of the key: the same payload can be placed on
    // different models (rs_dist vs rs_tdirib vs rs_twnin) whose rows sit in
    // different atlas rects; the variant letter alone would collide between
    // them (`b` exists in every family).
    uint64_t layoutHash = 1469598103934665603ull;
    for ( const SignQuadLayout &quad : layout.Quads ) {
        for ( float value : { quad.U0, quad.V0, quad.U1, quad.V1, quad.Top, quad.Bottom, quad.SrcV0, quad.SrcV1 } ) {
            uint32_t bits = 0;
            memcpy( &bits, &value, sizeof( bits ) );
            layoutHash = ( layoutHash ^ bits ) * 1099511628211ull;
        }
        // The per-family stamp orientation is part of the face: two families
        // whose zones land on the same floats must not share a composed
        // texture (the flip flags decide how the text is stamped).
        layoutHash = ( layoutHash ^ ( quad.FlipU ? 0x9E3779B9ull : 0x85EBCA6Bull ) ^
                       ( quad.FlipV ? 0xC2B2AE35ull : 0x27D4EB2Full ) ) *
                     1099511628211ull;
    }
    layoutHash = ( layoutHash ^ static_cast<uint64_t>( layout.Family ) ) * 1099511628211ull;
    const uint64_t key = payloadHash ^ ( static_cast<uint64_t>( static_cast<uint8_t>( variant ) ) << 56 ) ^
                         ( layoutHash << 17 ) ^ ( layoutHash >> 47 );
    const uint64_t now = ++faceClock;
    static const bool bSyncFaces = CmdLine::HasArg( "-syncsignfaces" );
    if ( !bSyncFaces ) {
        collectFinished();
    }
    auto it = faces.find( key );
    if ( it != faces.end() ) {
        it->second.lastUse = now;
        return it->second.pTexture;
    }
    Face face;
    face.Rows = rows;
    face.Variant = variant;
    face.Layout = layout;
    face.lastUse = now;
    if ( !bSyncFaces ) {
        // Queue it once; the plain plate draws until the result is uploaded.
        if ( pendingKeys.insert( key ).second ) {
            ensureAtlasBase(); // main thread: the worker only reads atlasBase
            if ( atlasBase.empty() ) {
                return nullptr;
            }
            std::lock_guard<std::mutex> lock( jobMutex );
            if ( !worker.joinable() ) {
                bStopWorker = false;
                worker = std::thread( [this]() { workerLoop(); } );
            }
            ComposeJob job;
            job.Key = key;
            job.PayloadHash = payloadHash;
            job.FaceData = std::move( face );
            jobs.push_back( std::move( job ) );
            jobSignal.notify_one();
        }
        return nullptr;
    }
    compose( face, payloadHash );
    if ( face.pTexture == nullptr ) {
        return nullptr;
    }
    return insertFace( key, std::move( face ) );
}

GPUTexture *SignFaceCache::insertFace( uint64_t key, Face &&face ) {
    if ( faces.size() >= kMaxFaces ) {
        // LRU eviction (linear scan; the cap is small).
        auto oldest = faces.begin();
        for ( auto candidate = faces.begin(); candidate != faces.end(); ++candidate ) {
            if ( candidate->second.lastUse < oldest->second.lastUse ) {
                oldest = candidate;
            }
        }
        if ( gpRender != nullptr ) {
            gpRender->getDevice().destroyTexture( oldest->second.pTexture );
        }
        faces.erase( oldest );
    }
    totalComposed++;
    if ( IsSignTextLogging() ) {
        OTDU_LOG_INFO( "SignText: face %016llx cached (%zu total)\n", static_cast<unsigned long long>( key ),
                       faces.size() + 1 );
    }
    return faces.emplace( key, std::move( face ) ).first->second.pTexture;
}

void SignFaceCache::workerLoop() {
    for ( ;; ) {
        ComposeJob job;
        {
            std::unique_lock<std::mutex> lock( jobMutex );
            jobSignal.wait( lock, [this]() { return bStopWorker || !jobs.empty(); } );
            if ( bStopWorker ) {
                return;
            }
            job = std::move( jobs.front() );
            jobs.pop_front();
        }
        composeEncoded( job.FaceData, job.PayloadHash, job.Encoded );
        std::lock_guard<std::mutex> lock( jobMutex );
        finished.push_back( std::move( job ) );
        numFinished.fetch_add( 1, std::memory_order_release );
    }
}

void SignFaceCache::collectFinished() {
    if ( numFinished.load( std::memory_order_acquire ) == 0 ) {
        return;
    }
    std::vector<ComposeJob> ready;
    {
        std::lock_guard<std::mutex> lock( jobMutex );
        ready.swap( finished );
        numFinished.store( 0, std::memory_order_relaxed );
    }
    for ( ComposeJob &job : ready ) {
        pendingKeys.erase( job.Key );
        if ( job.Encoded.empty() || faces.count( job.Key ) != 0 ) {
            continue;
        }
        Face &face = job.FaceData;
        face.lastUse = faceClock;
        upload( face, job.PayloadHash, job.Encoded );
        if ( face.pTexture != nullptr ) {
            insertFace( job.Key, std::move( face ) );
        }
    }
}

void SignFaceCache::stopWorker() {
    {
        std::lock_guard<std::mutex> lock( jobMutex );
        bStopWorker = true;
        jobs.clear();
    }
    jobSignal.notify_all();
    if ( worker.joinable() ) {
        worker.join();
    }
    finished.clear();
    numFinished.store( 0 );
    pendingKeys.clear();
}

namespace {
    // White coverage of a font pixel (the glyph page is white on black).
    inline uint8_t glyphCoverage( const uint8_t *pPage, uint32_t pageWidth, uint32_t pageHeight, float u, float v ) {
        const int32_t x = Min( static_cast<int32_t>( u * pageWidth ), static_cast<int32_t>( pageWidth ) - 1 );
        const int32_t y = Min( static_cast<int32_t>( v * pageHeight ), static_cast<int32_t>( pageHeight ) - 1 );
        if ( x < 0 || y < 0 ) {
            return 0;
        }
        return pPage[( static_cast<size_t>( y ) * pageWidth + x ) * 4];
    }

    void stampText( uint8_t *pPixels,
                    uint32_t atlasSize,
                    const Font *pFont,
                    const uint8_t *pPage,
                    uint32_t pageWidth,
                    uint32_t pageHeight,
                    const std::string &text,
                    int32_t x0,
                    int32_t baseline,
                    float size,
                    float alignCenter ) {
        if ( text.empty() || size < 6.0f ) {
            return;
        }
        float penX = x0;
        if ( alignCenter > 0.0f ) {
            penX += ( alignCenter - pFont->measureText( text.c_str(), size ) ) * 0.5f;
        }
        const char *p = text.c_str();
        while ( *p != '\0' ) {
            const uint32_t codepoint = Font::DecodeUTF8( p );
            const Font::Glyph *pGlyph = pFont->findGlyph( codepoint );
            if ( pGlyph == nullptr ) {
                penX += 0.26f * size;
                continue;
            }
            const float quadX0 = penX + pGlyph->X0 * size;
            const float quadY0 = baseline + pGlyph->Y0 * size;
            const float quadX1 = penX + pGlyph->X1 * size;
            const float quadY1 = baseline + pGlyph->Y1 * size;
            for ( int32_t y = static_cast<int32_t>( quadY0 ); y < static_cast<int32_t>( quadY1 ); y++ ) {
                if ( y < 0 || y >= static_cast<int32_t>( atlasSize ) ) {
                    continue;
                }
                const float v0 =
                    ( y + 0.5f - quadY0 ) / std::max( 1.0f, quadY1 - quadY0 ) * ( pGlyph->V1 - pGlyph->V0 ) +
                    pGlyph->V0;
                uint8_t *pRow = pPixels + static_cast<size_t>( y ) * atlasSize * 4;
                for ( int32_t x = static_cast<int32_t>( quadX0 ); x < static_cast<int32_t>( quadX1 ); x++ ) {
                    if ( x < 0 || x >= static_cast<int32_t>( atlasSize ) ) {
                        continue;
                    }
                    const float u0 =
                        ( x + 0.5f - quadX0 ) / std::max( 1.0f, quadX1 - quadX0 ) * ( pGlyph->U1 - pGlyph->U0 ) +
                        pGlyph->U0;
                    const uint32_t coverage = glyphCoverage( pPage, pageWidth, pageHeight, u0, v0 );
                    if ( coverage == 0 ) {
                        continue;
                    }
                    uint8_t *pPixel = pRow + static_cast<size_t>( x ) * 4;
                    for ( int32_t c = 0; c < 3; c++ ) {
                        pPixel[c] =
                            static_cast<uint8_t>( ( pPixel[c] * ( 255 - coverage ) + kTextShade * coverage ) / 255 );
                    }
                }
            }
            penX += pGlyph->Advance * size;
        }
    }

    // Stamps a text row into an RGBA scratch (coverage in alpha, color in
    // RGB); blitMirror puts it into the face.
    void stampTextRGBA( uint8_t *pScratch, uint32_t scratchWidth, uint32_t scratchHeight, const Font *pFont,
                        const uint8_t *pPage, uint32_t pageWidth, uint32_t pageHeight, const std::string &text,
                        int32_t x0, int32_t baseline, float size, float alignRight, uint8_t shade = kTextShade,
                        float boldTexels = 0.0f ) {
        if ( text.empty() || size < 6.0f ) {
            return;
        }
        float penX = static_cast<float>( x0 );
        if ( alignRight > 0.0f ) {
            penX += alignRight - pFont->measureText( text.c_str(), size );
        }
        const char *p = text.c_str();
        while ( *p != '\0' ) {
            const uint32_t codepoint = Font::DecodeUTF8( p );
            const Font::Glyph *pGlyph = pFont->findGlyph( codepoint );
            if ( pGlyph == nullptr ) {
                penX += 0.26f * size;
                continue;
            }
            const float quadX0 = penX + pGlyph->X0 * size;
            const float quadY0 = baseline + pGlyph->Y0 * size;
            const float quadX1 = penX + pGlyph->X1 * size;
            const float quadY1 = baseline + pGlyph->Y1 * size;
            // The faux bold spreads past the glyph's tight box (an "I" or an
            // "l" is a box as wide as its stem): pad the quad and treat the
            // page outside the glyph's UV box as empty (no neighbour bleed).
            const float texelToPixel = ( quadX1 - quadX0 ) / std::max( 1.0f, ( pGlyph->U1 - pGlyph->U0 ) * pageWidth );
            const float pad = boldTexels > 0.0f ? boldTexels * texelToPixel + 1.0f : 0.0f;
            const auto sampleGlyph = [&]( float u, float v ) -> uint32_t {
                if ( u < pGlyph->U0 || u > pGlyph->U1 || v < pGlyph->V0 || v > pGlyph->V1 ) {
                    return 0;
                }
                return glyphCoverage( pPage, pageWidth, pageHeight, u, v );
            };
            for ( int32_t y = static_cast<int32_t>( quadY0 - pad ); y < static_cast<int32_t>( quadY1 + pad ) + 1;
                  y++ ) {
                if ( y < 0 || y >= static_cast<int32_t>( scratchHeight ) ) {
                    continue;
                }
                uint8_t *pRow = pScratch + static_cast<size_t>( y ) * scratchWidth * 4;
                for ( int32_t x = static_cast<int32_t>( quadX0 - pad ); x < static_cast<int32_t>( quadX1 + pad ) + 1;
                      x++ ) {
                    if ( x < 0 || x >= static_cast<int32_t>( scratchWidth ) ) {
                        continue;
                    }
                    const float u0 = ( x + 0.5f - quadX0 ) / std::max( 1.0f, quadX1 - quadX0 ) *
                                         ( pGlyph->U1 - pGlyph->U0 ) +
                                     pGlyph->U0;
                    const float v0 = ( y + 0.5f - quadY0 ) / std::max( 1.0f, quadY1 - quadY0 ) *
                                         ( pGlyph->V1 - pGlyph->V0 ) +
                                     pGlyph->V0;
                    uint32_t coverage = sampleGlyph( u0, v0 );
                    if ( boldTexels > 0.0f ) {
                        // Faux bold: the max coverage of a small disc of the
                        // glyph page (the retail sign face is heavier than
                        // the regular UI fonts).
                        const float du = boldTexels / pageWidth, dv = boldTexels / pageHeight;
                        static const float kOffsets[8][2] = {
                            {1.0f,  0.0f },
                            {-1.0f, 0.0f },
                            {0.0f,  1.0f },
                            {0.0f,  -1.0f},
                            {0.7f,  0.7f },
                            {-0.7f, 0.7f },
                            {0.7f,  -0.7f},
                            {-0.7f, -0.7f}
                        };
                        for ( const auto &offset : kOffsets ) {
                            coverage = Max( coverage, sampleGlyph( u0 + offset[0] * du, v0 + offset[1] * dv ) );
                        }
                    }
                    if ( coverage == 0 ) {
                        continue;
                    }
                    uint8_t *pPixel = pRow + static_cast<size_t>( x ) * 4;
                    pPixel[0] = pPixel[1] = pPixel[2] = shade;
                    pPixel[3] = Max( pPixel[3], static_cast<uint8_t>( coverage ) );
                }
            }
            penX += pGlyph->Advance * size;
        }
    }

    // Stamps a filled shape into the scratch: inside(x, y) over the unit box
    // (y down) placed at [x0, x0 + w) x [y0, y0 + h), 4 x 4 supersampled.
    template <typename Inside>
    void stampShape( uint8_t *pScratch,
                     uint32_t scratchWidth,
                     uint32_t scratchHeight,
                     float x0,
                     float y0,
                     float w,
                     float h,
                     uint8_t shade,
                     Inside inside ) {
        if ( w < 2.0f || h < 2.0f ) {
            return;
        }
        for ( int32_t y = static_cast<int32_t>( y0 ); y < static_cast<int32_t>( y0 + h ) + 1; y++ ) {
            if ( y < 0 || y >= static_cast<int32_t>( scratchHeight ) ) {
                continue;
            }
            for ( int32_t x = static_cast<int32_t>( x0 ); x < static_cast<int32_t>( x0 + w ) + 1; x++ ) {
                if ( x < 0 || x >= static_cast<int32_t>( scratchWidth ) ) {
                    continue;
                }
                uint32_t hits = 0;
                for ( int32_t sy = 0; sy < 4; sy++ ) {
                    for ( int32_t sx = 0; sx < 4; sx++ ) {
                        const float u = ( x + ( sx + 0.5f ) / 4.0f - x0 ) / w;
                        const float v = ( y + ( sy + 0.5f ) / 4.0f - y0 ) / h;
                        if ( u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f && inside( u, v ) ) {
                            hits++;
                        }
                    }
                }
                if ( hits == 0 ) {
                    continue;
                }
                uint8_t *pPixel = pScratch + ( static_cast<size_t>( y ) * scratchWidth + x ) * 4;
                pPixel[0] = pPixel[1] = pPixel[2] = shade;
                pPixel[3] = Max( pPixel[3], static_cast<uint8_t>( hits * 255 / 16 ) );
            }
        }
    }

    // The TdirIb row arrow (the x360 photos: a bold black arrow in the yellow
    // tip). code 11 up, 12 left, 13 down, 14 right (spec 6; 12/14 photo
    // verified, 11/13 [INF]).
    bool insideArrow( float u, float v, int32_t code ) {
        // Unit box (y down) -> an arrow pointing +x over [-1, 1]^2.
        float x = u * 2.0f - 1.0f, y = v * 2.0f - 1.0f;
        if ( code == 12 ) {
            x = -x;
        } else if ( code == 11 ) { // up: screen -y becomes +x
            const float t = x;
            x = -y;
            y = t;
        } else if ( code == 13 ) { // down
            const float t = x;
            x = y;
            y = t;
        }
        const bool bShaft = x >= -0.95f && x <= 0.1f && std::abs( y ) <= 0.2f;
        const bool bHead = x >= 0.0f && x <= 0.95f && std::abs( y ) <= ( 0.95f - x ) * 0.95f;
        return bShaft || bHead;
    }

    // The row pictograms (spec 5.1), drawn after the x360 photos in a framed
    // square [INF: the original pictogram art is not in the banks searched].
    bool insideWaves( float u, float v ) {
        if ( u < 0.14f || u > 0.86f ) {
            return false;
        }
        for ( float row : { 0.70f, 0.80f, 0.90f } ) {
            if ( std::abs( v - ( row + 0.025f * std::sin( u * 6.2831853f * 3.0f ) ) ) < 0.022f ) {
                return true;
            }
        }
        return false;
    }

    bool insideIcon( float u, float v, uint8_t icon ) {
        // The frame.
        if ( u < 0.05f || u > 0.95f || v < 0.05f || v > 0.95f ) {
            return true;
        }
        if ( u < 0.09f || u > 0.91f || v < 0.09f || v > 0.91f ) {
            return false;
        }
        switch ( icon ) {
        case 0x4b: { // beach: an umbrella over the sea
            if ( insideWaves( u, v ) ) {
                return true;
            }
            const float poleX = 0.5f + ( 0.58f - v ) * 0.18f;
            if ( v > 0.30f && v < 0.62f && std::abs( u - poleX ) < 0.025f ) {
                return true;
            }
            const float dx = ( u - 0.5f ) / 0.32f, dy = ( v - 0.36f ) / 0.22f;
            return dy <= 0.0f && dx * dx + dy * dy <= 1.0f;
        }
        case 0x48: { // harbour: a ship over the sea
            if ( insideWaves( u, v ) ) {
                return true;
            }
            const bool bHull =
                v >= 0.48f && v <= 0.62f && u >= 0.16f + ( v - 0.48f ) * 0.6f && u <= 0.84f - ( v - 0.48f ) * 0.6f;
            const bool bCabin = v >= 0.36f && v < 0.48f && u >= 0.30f && u <= 0.66f;
            const bool bFunnel = v >= 0.20f && v < 0.36f && u >= 0.42f && u <= 0.56f;
            return bHull || bCabin || bFunnel;
        }
        case 0x4a: { // airport: a plane seen from above
            const bool bBody = std::abs( u - 0.5f ) < 0.06f && v > 0.14f && v < 0.86f;
            const bool bWings = v > 0.36f && v < 0.56f && std::abs( u - 0.5f ) < 0.40f - ( v - 0.36f ) * 0.8f;
            const bool bTail = v > 0.74f && v < 0.84f && std::abs( u - 0.5f ) < 0.16f;
            return bBody || bWings || bTail;
        }
        case 0x49: { // old town: a castle
            if ( u > 0.43f && u < 0.57f && v > 0.66f ) {
                return false; // the gate
            }
            const bool bWall = u > 0.22f && u < 0.78f && v > 0.46f && v < 0.88f;
            const bool bTowers = ( ( u > 0.16f && u < 0.36f ) || ( u > 0.64f && u < 0.84f ) ) && v > 0.28f && v < 0.88f;
            const bool bMerlons = v > 0.18f && v <= 0.28f &&
                                  ( ( u > 0.16f && u < 0.22f ) || ( u > 0.30f && u < 0.36f ) ||
                                    ( u > 0.64f && u < 0.70f ) || ( u > 0.78f && u < 0.84f ) );
            return bWall || bTowers || bMerlons;
        }
        default:
            return false;
        }
    }

    // Blits a scratch RGBA over the face. bMirror flips x: most plate quads
    // sample their atlas rect with u running against the plate's
    // left-to-right (the zone-edge marker test, sign_text_spec 4); the
    // dist/tdirib/hw plates run the other way. bFlipV flips y for the town
    // plates, which read their v bottom up (spec 4.9).
    void blitMirror( uint8_t *pPixels, uint32_t atlasSize, const uint8_t *pScratch, uint32_t scratchWidth,
                     uint32_t scratchHeight, int32_t destX, int32_t destY, bool bMirror = true,
                     bool bFlipV = false ) {
        for ( uint32_t y = 0; y < scratchHeight; y++ ) {
            const int32_t faceY =
                destY + static_cast<int32_t>( bFlipV ? scratchHeight - 1 - y : y );
            if ( faceY < 0 || faceY >= static_cast<int32_t>( atlasSize ) ) {
                continue;
            }
            for ( uint32_t x = 0; x < scratchWidth; x++ ) {
                const uint8_t *pSource = pScratch + ( static_cast<size_t>( y ) * scratchWidth + x ) * 4;
                if ( pSource[3] == 0 ) {
                    continue;
                }
                const int32_t faceX = destX + static_cast<int32_t>( bMirror ? scratchWidth - 1 - x : x );
                if ( faceX < 0 || faceX >= static_cast<int32_t>( atlasSize ) ) {
                    continue;
                }
                uint8_t *pPixel = pPixels + ( static_cast<size_t>( faceY ) * atlasSize + faceX ) * 4;
                const uint32_t coverage = pSource[3];
                for ( int32_t c = 0; c < 3; c++ ) {
                    pPixel[c] =
                        static_cast<uint8_t>( ( pPixel[c] * ( 255 - coverage ) + pSource[c] * coverage ) / 255 );
                }
            }
        }
    }

    // DXT1 encode of one mip (w and h are multiples of 4 on the 1024 chain).
    void encodeDXT1( const std::vector<uint8_t> &rgba, uint32_t width, uint32_t height, std::vector<uint8_t> &out ) {
        out.resize( static_cast<size_t>( width ) * height / 2 );
        uint8_t *pOut = out.data();
        for ( uint32_t blockY = 0; blockY < height; blockY += 4 ) {
            for ( uint32_t blockX = 0; blockX < width; blockX += 4 ) {
                uint8_t colors[16][3];
                uint32_t minLum = 0xffffffffu, maxLum = 0;
                uint32_t minIdx = 0, maxIdx = 0;
                for ( uint32_t p = 0; p < 16; p++ ) {
                    const uint32_t x = blockX + ( p & 3 );
                    const uint32_t y = blockY + ( p >> 2 );
                    const uint8_t *pPixel = rgba.data() + ( static_cast<size_t>( y ) * width + x ) * 4;
                    colors[p][0] = pPixel[0];
                    colors[p][1] = pPixel[1];
                    colors[p][2] = pPixel[2];
                    const uint32_t lum = pPixel[0] + pPixel[1] * 2 + pPixel[2];
                    if ( lum < minLum ) {
                        minLum = lum;
                        minIdx = p;
                    }
                    if ( lum > maxLum ) {
                        maxLum = lum;
                        maxIdx = p;
                    }
                }
                const auto pack565 = []( const uint8_t *pColor ) {
                    return static_cast<uint16_t>( ( pColor[0] >> 3 ) << 11 | ( pColor[1] >> 2 ) << 5 |
                                                  ( pColor[2] >> 3 ) );
                };
                const auto unpack565 = []( uint16_t c, uint8_t *pColor ) {
                    pColor[0] = static_cast<uint8_t>( ( ( c >> 11 ) & 31 ) * 255 / 31 );
                    pColor[1] = static_cast<uint8_t>( ( ( c >> 5 ) & 63 ) * 255 / 63 );
                    pColor[2] = static_cast<uint8_t>( ( c & 31 ) * 255 / 31 );
                };
                uint16_t c0 = pack565( colors[maxIdx] );
                uint16_t c1 = pack565( colors[minIdx] );
                if ( ( c0 & 0xffff ) <= c1 ) {
                    // Keep the 4-color palette: the decoder only interpolates
                    // when c0 > c1 (else color 3 is transparent black). Force a
                    // pair with c0 = c1 + 1.
                    c1 = static_cast<uint16_t>( c1 & ~1u );
                    c0 = static_cast<uint16_t>( c1 | 1 );
                }
                uint8_t palette[4][3];
                unpack565( c0, palette[0] );
                unpack565( c1, palette[1] );
                for ( int32_t p = 0; p < 3; p++ ) {
                    palette[2][p] = static_cast<uint8_t>( ( 2 * palette[0][p] + palette[1][p] + 1 ) / 3 );
                    palette[3][p] = static_cast<uint8_t>( ( palette[0][p] + 2 * palette[1][p] + 1 ) / 3 );
                }
                uint32_t indices = 0;
                for ( uint32_t p = 0; p < 16; p++ ) {
                    uint32_t best = 0;
                    uint32_t bestError = 0xffffffffu;
                    for ( uint32_t c = 0; c < 4; c++ ) {
                        const int32_t dr = colors[p][0] - palette[c][0];
                        const int32_t dg = colors[p][1] - palette[c][1];
                        const int32_t db = colors[p][2] - palette[c][2];
                        const uint32_t error = static_cast<uint32_t>( dr * dr + dg * dg + db * db );
                        if ( error < bestError ) {
                            bestError = error;
                            best = c;
                        }
                    }
                    indices |= best << ( 2 * p );
                }
                memcpy( pOut + 0, &c0, 2 );
                memcpy( pOut + 2, &c1, 2 );
                memcpy( pOut + 4, &indices, 4 );
                pOut += 8;
            }
        }
    }
} // namespace

void SignFaceCache::compose( Face &face, uint64_t payloadHash ) {
    ensureAtlasBase();
    std::vector<uint8_t> encoded;
    composeEncoded( face, payloadHash, encoded );
    upload( face, payloadHash, encoded );
}

void SignFaceCache::composeEncoded( const Face &face, uint64_t payloadHash, std::vector<uint8_t> &encoded ) const {
    if ( atlasBase.empty() ) {
        return;
    }
    std::vector<uint8_t> pixels = atlasBase;
    if ( IsSignTextLogging() ) {
        std::string text;
        for ( const SignTextRow &row : face.Rows ) {
            if ( !text.empty() ) {
                text += " / ";
            }
            text += row.Name;
            if ( !row.Distance.empty() ) {
                text += " " + row.Distance;
            }
        }
        OTDU_LOG_INFO( "SignText: layout variant %c: %zu plate row(s), rows '%s'\n",
                       face.Variant != 0 ? face.Variant : '?', face.Layout.Quads.size(), text.c_str() );
    }
    if ( face.Layout.bValid && !face.Layout.Quads.empty() ) {
        // Payload rows on the plate row zones, top plate first (name left,
        // distance right, sized from the zone's atlas height). The row is
        // stamped into a scratch and blitted x-flipped: the plate quads run
        // their u against the plate's left-to-right (zone-edge marker test,
        // sign_text_spec 4).
        //
        // A zone can carry several rows: the highway panels map one tall quad
        // over the whole face (rs_hwdist*, 3 rows), so that zone is split into
        // equal sub-rows. Narrow plate bars carry one row each.
        const uint32_t zoneCount = static_cast<uint32_t>( face.Layout.Quads.size() );
        const uint32_t rowCount = static_cast<uint32_t>( face.Rows.size() );
        const uint32_t rowsPerZone = ( rowCount + zoneCount - 1 ) / zoneCount;
        // A plate RemapStackedPlateRows moved onto a spare bar keeps its own
        // look: its source band (outline, the TdirIb yellow tip) is copied
        // over the spare bar with the same linear v map the vertices got, so
        // every stacked plate draws exactly like the first (the second row of
        // a TdirIb used to show the plain bar's outline, squashed).
        for ( const SignQuadLayout &zone : face.Layout.Quads ) {
            const float srcSpan = zone.SrcV1 - zone.SrcV0;
            if ( srcSpan <= 1e-4f ||
                 ( std::abs( zone.SrcV0 - zone.V0 ) < 0.002f && std::abs( zone.SrcV1 - zone.V1 ) < 0.002f ) ) {
                continue;
            }
            const float scale = ( zone.V1 - zone.V0 ) / srcSpan;
            const float margin = 0.006f; // the remap's +-0.003 vertex window, and the filter
            const int32_t y0 = static_cast<int32_t>( ( zone.V0 - margin * scale ) * kAtlasSize );
            const int32_t y1 = static_cast<int32_t>( ( zone.V1 + margin * scale ) * kAtlasSize ) + 1;
            const int32_t x0 = Max( 0, static_cast<int32_t>( zone.U0 * kAtlasSize ) - 8 );
            const int32_t x1 =
                Min( static_cast<int32_t>( kAtlasSize ), static_cast<int32_t>( zone.U1 * kAtlasSize ) + 8 );
            for ( int32_t y = Max( 0, y0 ); y < Min( static_cast<int32_t>( kAtlasSize ), y1 ); y++ ) {
                const float v = ( y + 0.5f ) / kAtlasSize;
                const float sourceV = zone.SrcV0 + ( v - zone.V0 ) / scale;
                const int32_t sy =
                    Clamp( static_cast<int32_t>( sourceV * kAtlasSize ), 0, static_cast<int32_t>( kAtlasSize ) - 1 );
                memcpy( pixels.data() + ( static_cast<size_t>( y ) * kAtlasSize + x0 ) * 4,
                        atlasBase.data() + ( static_cast<size_t>( sy ) * kAtlasSize + x0 ) * 4,
                        static_cast<size_t>( x1 - x0 ) * 4 );
            }
        }
        // An unfolded plate's strip past the fold shows the cell mirrored, as
        // the folded original did.
        for ( const SignQuadLayout &zone : face.Layout.Quads ) {
            if ( !zone.bFolded || zone.FoldU <= zone.U0 ) {
                continue;
            }
            const int32_t y0 = Max( 0, static_cast<int32_t>( ( zone.V0 - 0.006f ) * kAtlasSize ) );
            const int32_t y1 = Min( static_cast<int32_t>( kAtlasSize ),
                                    static_cast<int32_t>( ( zone.V1 + 0.006f ) * kAtlasSize ) + 1 );
            const int32_t foldX = static_cast<int32_t>( zone.FoldU * kAtlasSize );
            const int32_t x1 =
                Min( static_cast<int32_t>( kAtlasSize ), static_cast<int32_t>( zone.U1 * kAtlasSize ) + 8 );
            for ( int32_t y = y0; y < y1; y++ ) {
                for ( int32_t x = foldX; x < x1; x++ ) {
                    const int32_t sx = Max( 0, 2 * foldX - x - 1 );
                    memcpy( pixels.data() + ( static_cast<size_t>( y ) * kAtlasSize + x ) * 4,
                            atlasBase.data() + ( static_cast<size_t>( y ) * kAtlasSize + sx ) * 4, 4 );
                }
            }
        }
        std::vector<SignQuadLayout> used;
        uint32_t rowIndex = 0;
        for ( uint32_t z = 0; z < zoneCount && rowIndex < rowCount; z++ ) {
            const SignQuadLayout &zone = face.Layout.Quads[z];
            const uint32_t rowsHere = Min( rowsPerZone, rowCount - rowIndex );
            for ( uint32_t sub = 0; sub < rowsHere; sub++, rowIndex++ ) {
                SignQuadLayout quad = zone;
                if ( rowsHere > 1 && zone.bRotated ) {
                    // Rotated panel: the reading rows run down -u.
                    const float uSpan = zone.U1 - zone.U0;
                    quad.U1 = zone.U1 - uSpan * static_cast<float>( sub ) / static_cast<float>( rowsHere );
                    quad.U0 = zone.U1 - uSpan * static_cast<float>( sub + 1 ) / static_cast<float>( rowsHere );
                } else if ( rowsHere > 1 ) {
                    const float vSpan = zone.V1 - zone.V0;
                    quad.V0 = zone.V0 + vSpan * static_cast<float>( sub ) / static_cast<float>( rowsHere );
                    quad.V1 = zone.V0 + vSpan * static_cast<float>( sub + 1 ) / static_cast<float>( rowsHere );
                }
                // Stacked plates share one atlas cell (rs_dirl_d's two white
                // plates, rs_tdirib_b's two): the first row keeps the cell, a
                // second stamp would overwrite it on both plates.
                bool bShared = false;
                for ( const SignQuadLayout &other : used ) {
                    if ( std::abs( other.U0 - quad.U0 ) < 0.004f && std::abs( other.V0 - quad.V0 ) < 0.004f ) {
                        bShared = true;
                        break;
                    }
                }
                if ( bShared ) {
                    continue;
                }
                used.push_back( quad );
                const SignTextRow &textRow = face.Rows[rowIndex];
                // The plate in reading space (a rotated panel reads along v).
                const float readWidth = quad.bRotated ? quad.V1 - quad.V0 : quad.U1 - quad.U0;
                const float readHeight = quad.bRotated ? quad.U1 - quad.U0 : quad.V1 - quad.V0;
                const int32_t plateWidth = static_cast<int32_t>( readWidth * kAtlasSize );
                const int32_t plateHeight = static_cast<int32_t>( readHeight * kAtlasSize );
                if ( plateWidth <= 8 || plateHeight <= 8 ) {
                    continue;
                }
                // The source band tells the plate's look (a moved plate drew
                // its source band, see above): the v 0.089..0.132 bar ends in
                // the yellow arrow tip (atlas x 324..377, measured on mip 0),
                // the v 0.017..0.065 bar is the green plate (white lettering,
                // the x360 photos: "Centre 1", "SANT MIQUEL").
                const bool bYellowTip = quad.SrcV0 > 0.080f && quad.SrcV1 < 0.140f;
                const bool bGreen = quad.SrcV0 < 0.078f;
                const uint8_t shade = bGreen ? 236 : kTextShade;
                const float zoneX0 = quad.U0 * kAtlasSize;
                const float tipX0 = bYellowTip ? Clamp( 324.0f - zoneX0, 0.0f, static_cast<float>( plateWidth ) )
                                               : static_cast<float>( plateWidth );
                const float tipX1 = bYellowTip ? Clamp( 378.0f - zoneX0, 0.0f, static_cast<float>( plateWidth ) )
                                               : static_cast<float>( plateWidth );
                // The rs_dir plates are arrows: the pointed end (atlas u
                // 0.375..0.405) is the reading end of a rs_dirr plate and the
                // reading start of a mirrored rs_dirl one; keep the row off it.
                const float arrowTip = face.Layout.Family == SignFamily::Dir ? 0.034f * kAtlasSize : 0.0f;
                const float rightEdge =
                    bYellowTip ? tipX0 - plateHeight * 0.25f : plateWidth * 0.96f - ( quad.FlipU ? 0.0f : arrowTip );
                float size = Min( 44.0f, Max( 10.0f, plateHeight * 0.80f ) );
                std::vector<uint8_t> scratch( static_cast<size_t>( plateWidth ) * plateHeight * 4, 0 );
                float textX = plateWidth * 0.035f + ( quad.FlipU ? arrowTip : 0.0f );
                const float iconAdvance = textRow.Icon != 0 ? plateHeight * 1.10f : 0.0f;
                {
                    // A long name narrows to fit before the tip / distance
                    // (the x360 photos: "Hisenda Aldea Oest" fills its plate).
                    const bool bDistance = face.Layout.bDistanceRows && !textRow.Distance.empty();
                    const float distanceWidth =
                        bDistance ? pFont->measureText( textRow.Distance.c_str(), size ) + plateHeight * 0.4f : 0.0f;
                    const float available = rightEdge - textX - iconAdvance - distanceWidth;
                    const float width = pFont->measureText( textRow.Name.c_str(), size ) * 1.04f;
                    if ( width > available && available > 8.0f ) {
                        size = Max( 8.0f, size * available / width );
                    }
                }
                const int32_t baseline = plateHeight / 2 + static_cast<int32_t>( pFont->getAscent() * size * 0.42f );
                // The page's glyphs are ~2x the stamped size: ~1.1 texel of
                // spread per 10 px gives the retail stroke weight.
                const float bold = size * 0.065f;
                if ( textRow.Icon != 0 ) {
                    // The pictogram: a framed square before the name.
                    const float iconSize = plateHeight * 0.80f;
                    const float iconY = ( plateHeight - iconSize ) * 0.5f;
                    const uint8_t icon = textRow.Icon;
                    stampShape( scratch.data(), plateWidth, plateHeight, textX, iconY, iconSize, iconSize, shade,
                                [icon]( float u, float v ) { return insideIcon( u, v, icon ); } );
                    textX += iconAdvance;
                }
                stampTextRGBA( scratch.data(), plateWidth, plateHeight, pFont, fontPage.data(), fontPageWidth,
                               fontPageHeight, textRow.Name, static_cast<int32_t>( textX ), baseline, size, 0.0f, shade,
                               bold );
                if ( face.Layout.bDistanceRows && !textRow.Distance.empty() ) {
                    stampTextRGBA( scratch.data(), plateWidth, plateHeight, pFont, fontPage.data(), fontPageWidth,
                                   fontPageHeight, textRow.Distance, 0, baseline, size, rightEdge, shade, bold );
                }
                // TdirIb: the row's distance byte is its arrow (spec 6).
                if ( face.Layout.Family == SignFamily::Tdir && bYellowTip && tipX1 - tipX0 > 4.0f ) {
                    int32_t arrow = 0;
                    if ( textRow.DistanceCode >= 0x26 && textRow.DistanceCode <= 0x29 ) {
                        arrow = 11 + ( textRow.DistanceCode - 0x26 );
                    }
                    if ( arrow != 0 ) {
                        const float arrowSize = Min( tipX1 - tipX0, static_cast<float>( plateHeight ) ) * 0.78f;
                        const float arrowX = ( tipX0 + tipX1 - arrowSize ) * 0.5f;
                        const float arrowY = ( plateHeight - arrowSize ) * 0.5f;
                        stampShape( scratch.data(), plateWidth, plateHeight, arrowX, arrowY, arrowSize, arrowSize,
                                    kTextShade, [arrow]( float u, float v ) { return insideArrow( u, v, arrow ); } );
                    }
                }
                // Stamp per the zone's measured sample direction: rs_dir and
                // the town plates mirror x, the town plates also flip y (the
                // plate top samples the rect's high v); dist/tdirib/hw stamp
                // straight (spec 4.9).
                if ( quad.bRotated ) {
                    // Reading right = +v (atlas y), reading up = +u (atlas x).
                    const int32_t baseX = static_cast<int32_t>( quad.U0 * kAtlasSize );
                    const int32_t baseY = static_cast<int32_t>( quad.V0 * kAtlasSize );
                    for ( int32_t sy = 0; sy < plateHeight; sy++ ) {
                        for ( int32_t sx = 0; sx < plateWidth; sx++ ) {
                            const uint8_t *pSource =
                                scratch.data() + ( static_cast<size_t>( sy ) * plateWidth + sx ) * 4;
                            const int32_t ax = baseX + plateHeight - 1 - sy, ay = baseY + sx;
                            if ( pSource[3] == 0 || ax < 0 || ay < 0 || ax >= static_cast<int32_t>( kAtlasSize ) ||
                                 ay >= static_cast<int32_t>( kAtlasSize ) ) {
                                continue;
                            }
                            uint8_t *pPixel = pixels.data() + ( static_cast<size_t>( ay ) * kAtlasSize + ax ) * 4;
                            for ( int32_t c = 0; c < 3; c++ ) {
                                pPixel[c] = static_cast<uint8_t>(
                                    ( pPixel[c] * ( 255 - pSource[3] ) + pSource[c] * pSource[3] ) / 255 );
                            }
                        }
                    }
                    continue;
                }
                blitMirror( pixels.data(), kAtlasSize, scratch.data(), plateWidth, plateHeight,
                            static_cast<int32_t>( quad.U0 * kAtlasSize ), static_cast<int32_t>( quad.V0 * kAtlasSize ),
                            quad.FlipU, quad.FlipV );
            }
        }
    } else {
        // No plate layout (the .3dg could not be walked): fall back to the
        // blank plate bands, variant b / c / d carry the green plate above
        // the white one.
        const bool bGreen = face.Variant == 'b' || face.Variant == 'c' || face.Variant == 'd';
        uint32_t row = 0;
        if ( bGreen && !face.Rows.empty() ) {
            const SignTextRow &greenRow = face.Rows[0];
            const float size = 30.0f;
            const int32_t baseline = kPlateGreen[1] + ( kPlateGreen[3] - kPlateGreen[1] ) / 2 +
                                     static_cast<int32_t>( pFont->getAscent() * size * 0.5f );
            stampText( pixels.data(), kAtlasSize, pFont, fontPage.data(), fontPageWidth, fontPageHeight, greenRow.Name,
                       kPlateGreen[0] + 8, baseline, size, 0.0f );
            if ( !greenRow.Distance.empty() ) {
                const float width = pFont->measureText( greenRow.Distance.c_str(), size );
                stampText( pixels.data(), kAtlasSize, pFont, fontPage.data(), fontPageWidth, fontPageHeight,
                           greenRow.Distance, static_cast<int32_t>( kPlateGreen[2] - 8 - width ), baseline, size,
                           0.0f );
            }
            row = 1;
        }
        const uint32_t whiteRows = static_cast<uint32_t>( face.Rows.size() ) - row;
        if ( whiteRows > 0 ) {
            const int32_t plateHeight = kPlateWhite[3] - kPlateWhite[1];
            const uint32_t capped = Min( whiteRows, 3u );
            const float size = Min( 30.0f, static_cast<float>( plateHeight ) / capped - 8.0f );
            for ( uint32_t k = 0; k < capped; k++, row++ ) {
                const SignTextRow &textRow = face.Rows[row];
                const int32_t cellTop = kPlateWhite[1] + static_cast<int32_t>( plateHeight * k / capped );
                const int32_t cellHeight = plateHeight / capped;
                const int32_t baseline =
                    cellTop + cellHeight / 2 + static_cast<int32_t>( pFont->getAscent() * size * 0.5f );
                stampText( pixels.data(), kAtlasSize, pFont, fontPage.data(), fontPageWidth, fontPageHeight,
                           textRow.Name, kPlateWhite[0] + 8, baseline, size, 0.0f );
                if ( !textRow.Distance.empty() ) {
                    const float width = pFont->measureText( textRow.Distance.c_str(), size );
                    stampText( pixels.data(), kAtlasSize, pFont, fontPage.data(), fontPageWidth, fontPageHeight,
                               textRow.Distance, static_cast<int32_t>( kPlateWhite[2] - 8 - width ), baseline, size,
                               0.0f );
                }
            }
        }
    }

    // DXT1 + box-filter mips, uploaded in the atlas' own format (BC devices
    // keep BC1; the fallback decodes as for every bank texture).
    uint32_t width = kAtlasSize;
    uint32_t height = kAtlasSize;
    encoded.clear();
    encoded.reserve( 699050 ); // the original 1024 chain's total size
    std::vector<uint8_t> mip( pixels );
    std::vector<uint8_t> next;
    for ( uint32_t mipIndex = 0; mipIndex < kAtlasMips; mipIndex++ ) {
        std::vector<uint8_t> level;
        encodeDXT1( mip, width, height, level );
        encoded.insert( encoded.end(), level.begin(), level.end() );
        if ( mipIndex + 1 < kAtlasMips ) {
            next.resize( static_cast<size_t>( ( width / 2 ) ) * ( height / 2 ) * 4 );
            for ( uint32_t y = 0; y < height / 2; y++ ) {
                for ( uint32_t x = 0; x < width / 2; x++ ) {
                    for ( int32_t c = 0; c < 4; c++ ) {
                        const uint32_t sum =
                            static_cast<uint32_t>( mip[( static_cast<size_t>( y * 2 ) * width + x * 2 ) * 4 + c] ) +
                            mip[( static_cast<size_t>( y * 2 ) * width + x * 2 + 1 ) * 4 + c] +
                            mip[( static_cast<size_t>( ( y * 2 + 1 ) ) * width + x * 2 ) * 4 + c] +
                            mip[( static_cast<size_t>( ( y * 2 + 1 ) ) * width + x * 2 + 1 ) * 4 + c];
                        next[( static_cast<size_t>( y ) * ( width / 2 ) + x ) * 4 + c] =
                            static_cast<uint8_t>( sum / 4 );
                    }
                }
            }
            mip.swap( next );
        }
        width /= 2;
        height /= 2;
    }
    // -signfacesdump: the composed face before encoding (scratch PPMs).
    static const bool bDump = CmdLine::HasArg( "-signfacesdump" );
    if ( bDump ) {
        char path[64];
        snprintf( path, sizeof( path ), "_signface_%016llx_%c.ppm", static_cast<unsigned long long>( payloadHash ),
                  face.Variant != 0 ? face.Variant : 'x' );
        std::ofstream stream( path, std::ios::binary );
        if ( stream.is_open() ) {
            stream << "P6\n" << kAtlasSize << " " << kAtlasSize << "\n255\n";
            for ( uint32_t y = 0; y < kAtlasSize; y++ ) {
                for ( uint32_t x = 0; x < kAtlasSize; x++ ) {
                    const uint8_t *p = pixels.data() + ( static_cast<size_t>( y ) * kAtlasSize + x ) * 4;
                    stream.write( reinterpret_cast<const char *>( p ), 3 );
                }
            }
        }
    }
}

void SignFaceCache::upload( Face &face, uint64_t payloadHash, const std::vector<uint8_t> &encoded ) {
    if ( encoded.empty() || gpRender == nullptr ) {
        return;
    }
    Texture2DB texture;
    texture.Name = "signface";
    texture.Width = kAtlasSize;
    texture.Height = kAtlasSize;
    texture.Depth = 1;
    texture.NumMips = kAtlasMips;
    texture.Format = VF_SRGB_DXT1;
    texture.pTexels = encoded.data();
    texture.TexelsSize = static_cast<uint32_t>( encoded.size() );
    char name[56];
    // The composition counter keeps recomposed faces from reusing a destroyed
    // face's name: the draw-set cache keys on the slot hashes (the name hash),
    // and a fresh texture with an evicted face's exact name would hit the set
    // still holding the dead view (trap 13).
    snprintf( name, sizeof( name ), "signface_%016llx_%c_%u", static_cast<unsigned long long>( payloadHash ),
              face.Variant != 0 ? face.Variant : 'x', totalComposed + 1 );
    face.pTexture = gpRender->getTextureCache().createFrom2DB( texture, name );
}
