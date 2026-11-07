#include "shared.h"
#include "eden_model.h"

#include "filesystem/bank.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

#include <cfloat>

namespace {
// Frustum culling of skinned items: margin (m) around the bind pose bounds.
constexpr float kSkinnedCullMargin = 2.0f;
// D3DDECLUSAGE_POSITION, index 0.
constexpr uint8_t kUsagePosition = 0;

// Position format of a vertex stream for a bottom level structure: only the
// formats an acceleration structure accepts and the Eden meshes actually use.
// The R32 heightfield stream is not one of them (it carries heights and the
// shader rebuilds the positions from the grid coordinates), so it returns
// VK_FORMAT_UNDEFINED and the mesh gets no BLAS.
VkFormat GetBlasPositionFormat( eVertexAttributeFormat format ) {
    switch ( format ) {
    case VAF_POSITION_R32G32B32_SFLOAT:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VAF_POSITION_R32G32B32A32_SFLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VAF_POSITION_R16G16B16A16_SFLOAT:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}
} // namespace

EdenModel::EdenModel() : pVertexBuffer( nullptr ), pIndexBuffer( nullptr ) {}

EdenModel::~EdenModel() { destroy(); }

void EdenModel::destroy() {
    // The bottom level structures go back to RayTracing, which owns them and
    // frees anything left over when it is destroyed first (shutdown order).
    if ( gpRender != nullptr && gpRender->getRayTracing().isReady() ) {
        for ( RayTracing::Blas *pBlas : blases ) {
            gpRender->getRayTracing().destroyBlas( pBlas );
        }
    }
    blases.clear();
    if ( gpRender != nullptr ) {
        gpRender->getDevice().destroyBuffer( pVertexBuffer );
        gpRender->getDevice().destroyBuffer( pIndexBuffer );
    }
    pVertexBuffer = nullptr;
    pIndexBuffer = nullptr;
    items.clear();
    materials.clear();
    collisionVertices.clear();
    collisionIndices.clear();
    surfaceCellStart.clear();
    surfaceCellTriangles.clear();
    surfaceGridWidth = surfaceGridHeight = 0;
    bSurfaceGridBuilt = false;
    nodeTransforms.clear();
    nodeVisible.clear();
    nodeItems.clear();
    nodeLodItems.clear();
}

bool EdenModel::load( EdenRenderer &renderer, const Bank &bank, const char *pBaseName,
                      const Render2DM *pFallbackMaterials, const char *pTextureOwner, const NodeFilter &filter,
                      const MaterialSetup &materialSetup, const GeometrySetup &geometrySetup ) {
    const BankEntry *pScene = nullptr;
    const BankEntry *pGeometry = nullptr;
    const BankEntry *pMaterials = nullptr;
    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( pBaseName != nullptr && StringCmpI( entry.Name.c_str(), pBaseName ) != 0 ) {
            continue;
        }
        if ( entry.Extension == "3dd" && pScene == nullptr ) {
            pScene = &entry;
        } else if ( entry.Extension == "3dg" && pGeometry == nullptr ) {
            pGeometry = &entry;
        } else if ( entry.Extension == "2dm" && pMaterials == nullptr ) {
            pMaterials = &entry;
        }
    }
    if ( pScene == nullptr || pGeometry == nullptr ) {
        return false;
    }
    return load( renderer, *pScene, *pGeometry, pMaterials, pFallbackMaterials, pTextureOwner, filter, materialSetup,
                 geometrySetup );
}

bool EdenModel::load( EdenRenderer &renderer, const BankEntry &sceneEntry, const BankEntry &geometryEntry,
                      const BankEntry *pMaterialEntry, const Render2DM *pFallbackMaterials, const char *pTextureOwner,
                      const NodeFilter &filter, const MaterialSetup &materialSetup,
                      const GeometrySetup &geometrySetup ) {
    destroy();
    Render3DG geometry;
    if ( !scene.parse( sceneEntry.pData, sceneEntry.Size, false ) ||
         !geometry.parse( geometryEntry.pData, geometryEntry.Size, false ) ) {
        OTDU_LOG_WARN( "EdenModel: failed to parse '%s'\n", sceneEntry.Filename.c_str() );
        return false;
    }
    if ( geometrySetup ) {
        geometrySetup( geometry );
    }
    if ( pMaterialEntry != nullptr ) {
        localMaterials.parse( pMaterialEntry->pData, pMaterialEntry->Size );
    }

    const std::vector<SceneNode> &nodes = scene.getNodes();
    nodeTransforms.clear();
    for ( const SceneNode &node : nodes ) {
        nodeTransforms.push_back( node.World );
    }
    nodeVisible.assign( nodes.size(), true );

    std::unordered_map<uint64_t, uint32_t> materialIndices;
    auto getMaterial = [&]( uint64_t hashcode ) -> int32_t {
        auto it = materialIndices.find( hashcode );
        if ( it != materialIndices.end() ) {
            return static_cast<int32_t>( it->second );
        }
        const MaterialDesc *pDesc = localMaterials.find( hashcode );
        if ( pDesc == nullptr && pFallbackMaterials != nullptr ) {
            pDesc = pFallbackMaterials->find( hashcode );
        }
        if ( pDesc == nullptr ) {
            materialIndices[hashcode] = ~0u;
            return -1;
        }
        EdenMaterial material;
        renderer.setupMaterial( *pDesc, material, pTextureOwner );
        if ( materialSetup ) {
            materialSetup( material );
        }
        materials.push_back( std::move( material ) );
        const uint32_t index = static_cast<uint32_t>( materials.size() - 1 );
        materialIndices[hashcode] = index;
        return static_cast<int32_t>( index );
    };

    std::vector<uint8_t> vertexData;
    std::vector<uint32_t> indexData;
    std::vector<uint32_t> itemMaxIndex; // ray tracing: highest index per item
    boundsMin = Vector3f( 1e9f, 1e9f, 1e9f );
    boundsMax = Vector3f( -1e9f, -1e9f, -1e9f );
    // Adds one INS. LOD geometry of a node: the primitive -> item path is the
    // same for every level (docs/re/pmi_spec.md 2.1). The body sits in a
    // lambda so the LOD0 path of the other models is unchanged.
    auto addGeometry = [&]( int32_t nodeIndex, uint32_t lod, const Geometry *pGeometry ) {
        const SceneNode &node = nodes[nodeIndex];
        for ( const GeometryPrimitive &primitive : pGeometry->Primitives ) {
            if ( primitive.NumRawVertex == 0 || primitive.Indices.empty() ) {
                continue;
            }
            const uint64_t materialHash =
                primitive.MaterialIndex < node.Materials.size() ? node.Materials[primitive.MaterialIndex] : 0;
            const int32_t materialIndex = getMaterial( materialHash );
            if ( materialIndex < 0 || materials[materialIndex].pProgram == nullptr ) {
                continue;
            }

            Item item;
            item.NodeIndex = nodeIndex;
            item.Lod = lod;
            item.BonePalette = primitive.BonePalette;
            item.MaterialIndex = static_cast<uint32_t>( materialIndex );
            EdenMesh &mesh = item.Mesh;
            mesh.NumStreams = 0;
            for ( uint32_t stream = 0; stream < 2; stream++ ) {
                if ( primitive.RawStrides[stream] == 0 ) {
                    continue;
                }
                const size_t offset = Align<size_t>( vertexData.size(), 16 );
                vertexData.resize( offset );
                vertexData.insert( vertexData.end(), primitive.RawStreams[stream].begin(),
                                   primitive.RawStreams[stream].end() );
                mesh.StreamOffsets[stream] = offset;
                mesh.Strides[stream] = primitive.RawStrides[stream];
                mesh.NumStreams = stream + 1;
            }
            mesh.Attributes = primitive.RawAttributes;
            // TD_Impostor (GUID 0x83078865caca336d): keep the quad on the CPU
            // for EdenWorld's per-material batches (see Item::ImpostorStreams).
            if ( materials[materialIndex].ShaderSetGUID == 0x83078865caca336dull && primitive.Vertices.size() <= 64 ) {
                for ( uint32_t stream = 0; stream < 2; stream++ ) {
                    item.ImpostorStreams[stream] = primitive.RawStreams[stream];
                }
                item.ImpostorIndices = primitive.Indices;
            }
            mesh.FirstIndex = static_cast<uint32_t>( indexData.size() );
            mesh.IndexCount = static_cast<uint32_t>( primitive.Indices.size() );
            item.NumVertices = static_cast<uint32_t>( primitive.Vertices.size() );
            mesh.VertexOffset = 0;
            indexData.insert( indexData.end(), primitive.Indices.begin(), primitive.Indices.end() );
            mesh.computeLayoutKey();
            uint32_t maxIndex = 0;
            for ( uint32_t index : primitive.Indices ) {
                maxIndex = Max( maxIndex, index );
            }
            itemMaxIndex.push_back( maxIndex );
            if ( bKeepCollision && lod == 0 ) {
                const uint32_t base = static_cast<uint32_t>( collisionVertices.size() );
                item.CollisionFirst = static_cast<uint32_t>( collisionIndices.size() );
                for ( const MeshVertex &vertex : primitive.Vertices ) {
                    collisionVertices.push_back( node.World.transformPoint(
                        Vector3f( vertex.Position[0], vertex.Position[1], vertex.Position[2] ) ) );
                }
                for ( uint32_t index : primitive.Indices ) {
                    collisionIndices.push_back( base + index );
                }
                item.CollisionCount = static_cast<uint32_t>( primitive.Indices.size() );
                bSurfaceGridBuilt = false; // the height grid is rebuilt over the new triangles
            }
            if ( bKeepItemIndices && lod == 0 ) {
                item.PrimitiveIndices = primitive.Indices;
                item.PrimitiveVertices.reserve( primitive.Vertices.size() );
                for ( const MeshVertex &vertex : primitive.Vertices ) {
                    item.PrimitiveVertices.emplace_back( vertex.Position[0], vertex.Position[1], vertex.Position[2] );
                }
            }

            item.BoundsMin = Vector3f( 1e9f, 1e9f, 1e9f );
            item.BoundsMax = Vector3f( -1e9f, -1e9f, -1e9f );
            for ( const MeshVertex &vertex : primitive.Vertices ) {
                const Vector3f p( vertex.Position[0], vertex.Position[1], vertex.Position[2] );
                item.BoundsMin = Vector3f( Min( item.BoundsMin.x, p.x ), Min( item.BoundsMin.y, p.y ),
                                           Min( item.BoundsMin.z, p.z ) );
                item.BoundsMax = Vector3f( Max( item.BoundsMax.x, p.x ), Max( item.BoundsMax.y, p.y ),
                                           Max( item.BoundsMax.z, p.z ) );
                const Vector3f w = node.World.transformPoint( p );
                boundsMin = Vector3f( Min( boundsMin.x, w.x ), Min( boundsMin.y, w.y ), Min( boundsMin.z, w.z ) );
                boundsMax = Vector3f( Max( boundsMax.x, w.x ), Max( boundsMax.y, w.y ), Max( boundsMax.z, w.z ) );
            }
            // -loguvrange <name substring | *>: the UV0 range of each matching
            // material's primitives with its slot samplers ('*' = every
            // material with a non-shadow clamp slot; add -edenclamp3 so the
            // address-3 slots count as clamp) - the survey behind the
            // 2DM address-3 rule (EdenRenderer AddressIndex).
            if ( const char *pUvMat = CmdLine::GetArgValue( "-loguvrange" ) ) {
                const EdenMaterial &m = materials[materialIndex];
                bool bClamp = false;
                for ( uint32_t k = 0; k < m.NumSlots; k++ ) {
                    const int si = static_cast<int>( m.SamplerIndex[k] ) - 5;
                    if ( m.TextureHashes[k] != 0 && si >= 0 && ( si / 3 == 2 || si % 3 == 2 ) &&
                         m.SlotTypes[k] != GetIdentifier64bit( "SHADOW" ) ) {
                        bClamp = true;
                    }
                }
                static std::unordered_set<std::string> seen;
                if ( ( pUvMat[0] == '*' ? bClamp : m.Name.find( pUvMat ) != std::string::npos ) &&
                     seen.insert( m.Name ).second ) {
                    std::string s;
                    for ( uint32_t k = 0; k < m.NumSlots; k++ ) {
                        s += IdentifierToString( m.SlotTypes[k] ) + ":" + std::to_string( m.SamplerIndex[k] ) + " ";
                    }
                    OTDU_LOG_INFO( "uvslots '%s' (%s): %s\n", m.Name.c_str(), m.pProgram->Name.c_str(), s.c_str() );
                    float mn[2] = { 1e9f, 1e9f }, mx[2] = { -1e9f, -1e9f };
                    for ( const MeshVertex &vertex : primitive.Vertices ) {
                        for ( int k = 0; k < 2; k++ ) {
                            mn[k] = Min( mn[k], vertex.Texcoord[k] );
                            mx[k] = Max( mx[k], vertex.Texcoord[k] );
                        }
                    }
                    OTDU_LOG_INFO( "uvrange '%s' %zu verts: u %.3f..%.3f v %.3f..%.3f\n",
                                   materials[materialIndex].Name.c_str(), primitive.Vertices.size(), mn[0], mx[0],
                                   mn[1], mx[1] );
                }
            }
            items.push_back( std::move( item ) );
        }
    };
    for ( int32_t nodeIndex = 0; nodeIndex < static_cast<int32_t>( nodes.size() ); nodeIndex++ ) {
        const SceneNode &node = nodes[nodeIndex];
        if ( node.ObjectHashcode == 0 || ( filter && !filter( scene, nodeIndex ) ) ) {
            continue;
        }
        const Geometry *pGeometry = geometry.find( node.ObjectHashcode );
        if ( pGeometry != nullptr ) {
            addGeometry( nodeIndex, 0, pGeometry );
        }
        if ( !bLoadLods ) {
            continue;
        }
        // LOD1/2 of the INS. table: the world library switches level by
        // distance (docs/re/pmi_streaming_spec.md 9 #3).
        const uint32_t numLods = static_cast<uint32_t>( Min<size_t>( node.LodObjectHashcodes.size(), 3 ) );
        for ( uint32_t lod = 1; lod < numLods; lod++ ) {
            if ( node.LodObjectHashcodes[lod] == 0 ) {
                continue;
            }
            const Geometry *pLodGeometry = geometry.find( node.LodObjectHashcodes[lod] );
            if ( pLodGeometry != nullptr ) {
                addGeometry( nodeIndex, lod, pLodGeometry );
            }
        }
    }
    if ( items.empty() ) {
        OTDU_LOG_DEBUG( "EdenModel '%s': nothing to draw\n", sceneEntry.Filename.c_str() );
        return false;
    }

    nodeLodItems.assign( nodes.size(), {} );
    for ( uint32_t i = 0; i < items.size(); i++ ) {
        const uint32_t lod = items[i].Lod < 3 ? items[i].Lod : 0;
        auto &range = nodeLodItems[items[i].NodeIndex][lod];
        if ( range.second == 0 ) {
            range.first = i;
        }
        range.second = i + 1 - range.first;
    }
    nodeItems.assign( nodes.size(), { 0u, 0u } );
    for ( uint32_t n = 0; n < nodes.size(); n++ ) {
        nodeItems[n] = nodeLodItems[n][0];
    }

    RenderDevice &device = gpRender->getDevice();
    // Ray tracing is opt in (-rtworld): the structures cost memory and load
    // time, so a default run allocates nothing for them.
    RayTracing &rayTracing = gpRender->getRayTracing();
    const bool bRayTracing = IsRayTracingWorldEnabled() && rayTracing.isReady();
    VkBufferUsageFlags vertexUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkBufferUsageFlags indexUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if ( bRayTracing ) {
        // A bottom level structure reads the mesh through its device address.
        vertexUsage |= RayTracing::getBuildInputUsage();
        indexUsage |= RayTracing::getBuildInputUsage();
    }
    pVertexBuffer = device.createBuffer( vertexData.size(), vertexUsage, vertexData.data() );
    pIndexBuffer = device.createBuffer( indexData.size() * sizeof( uint32_t ), indexUsage, indexData.data() );
    for ( Item &item : items ) {
        for ( uint32_t stream = 0; stream < item.Mesh.NumStreams; stream++ ) {
            item.Mesh.pStreamBuffers[stream] = pVertexBuffer;
        }
        item.Mesh.pIndexBuffer = pIndexBuffer;
    }

    if ( bRayTracing ) {
        // One structure per mesh. The meshes are sub ranges of the model's two
        // shared buffers: the address of a stream is the buffer address plus
        // the stream offset, and the indices start at FirstIndex.
        const VkDeviceAddress vertexBase = device.getBufferDeviceAddress( pVertexBuffer );
        const VkDeviceAddress indexBase = device.getBufferDeviceAddress( pIndexBuffer );
        blases.assign( items.size(), nullptr );
        uint32_t numBlas = 0;
        uint32_t numUnsupported = 0;
        for ( size_t i = 0; i < items.size(); i++ ) {
            // Only the LOD0 geometry enters the world TLAS: the distance
            // levels would double the structures for a mesh never drawn in
            // the main pass.
            if ( items[i].Lod != 0 ) {
                continue;
            }
            EdenMesh &mesh = items[i].Mesh;
            const RawVertexAttribute *pPosition = nullptr;
            for ( const RawVertexAttribute &attribute : mesh.Attributes ) {
                if ( attribute.Usage == kUsagePosition && attribute.UsageIndex == 0 ) {
                    pPosition = &attribute;
                    break;
                }
            }
            if ( pPosition == nullptr || pPosition->Stream >= EdenMesh::kMaxStreams ||
                 mesh.Strides[pPosition->Stream] == 0 ) {
                numUnsupported++;
                continue;
            }
            const VkFormat positionFormat = GetBlasPositionFormat( pPosition->Format );
            if ( positionFormat == VK_FORMAT_UNDEFINED ) {
                numUnsupported++;
                continue;
            }
            RayTracing::Blas *pBlas = rayTracing.createTriangleBlas(
                vertexBase + mesh.StreamOffsets[pPosition->Stream], mesh.Strides[pPosition->Stream], positionFormat,
                itemMaxIndex[i] + 1, indexBase + mesh.FirstIndex * sizeof( uint32_t ), mesh.IndexCount );
            if ( pBlas == nullptr ) {
                numUnsupported++;
                continue;
            }
            mesh.BlasAddress = pBlas->Address;
            blases[i] = pBlas;
            numBlas++;
        }
        OTDU_LOG_INFO( "Ray tracing: '%s': %u of %zu meshes have a BLAS (%u skipped)\n",
                       sceneEntry.Filename.c_str(), numBlas, items.size(), numUnsupported );
    }
    OTDU_LOG_DEBUG( "EdenModel '%s': %zu items, %zu materials, %zu KB vertices\n", sceneEntry.Filename.c_str(),
                    items.size(), materials.size(), vertexData.size() / 1024 );
    return pVertexBuffer != nullptr && pIndexBuffer != nullptr;
}

void EdenModel::clearPrimitiveData() {
    for ( Item &item : items ) {
        item.PrimitiveIndices.clear();
        item.PrimitiveIndices.shrink_to_fit();
        item.PrimitiveVertices.clear();
        item.PrimitiveVertices.shrink_to_fit();
    }
}

void EdenModel::draw( EdenRenderer &renderer, const Matrix4x4f &world, const EdenDrawParams *pParams ) const {
    for ( size_t itemIndex = 0; itemIndex < items.size(); itemIndex++ ) {
        const Item &item = items[itemIndex];
        if ( item.Lod != 0 || !nodeVisible[item.NodeIndex] ) {
            continue;
        }
        if ( pParams != nullptr ) {
            bool bForcedHidden = false;
            for ( const int32_t node : pParams->ForceHiddenNodes ) {
                if ( node == item.NodeIndex ) {
                    bForcedHidden = true;
                    break;
                }
            }
            for ( const int32_t hidden : pParams->ForceHiddenItems ) {
                if ( hidden == static_cast<int32_t>( itemIndex ) ) {
                    bForcedHidden = true;
                    break;
                }
            }
            if ( bForcedHidden ) {
                continue;
            }
        }
        Matrix4x4f itemWorld = world * nodeTransforms[item.NodeIndex];
        // OpenTDU2 shore swell: the sea (TD_Ocean) rises and falls with the
        // tide; the retail shore foam (TD_backwash `Ecume`) is moved along the
        // beach gradient (foamOffset) so the whole surf zone runs up and down
        // the sand. Only those two move; the sand and the shore stay.
        const uint64_t shader = materials[item.MaterialIndex].ShaderSetGUID;
        if ( shader == 0x0cea000000000002ull /* TD_Ocean */ && oceanYOffset != 0.0f ) {
            itemWorld = Matrix4x4f::Translation( Vector3f( 0.0f, oceanYOffset, 0.0f ) ) * itemWorld;
        } else if ( shader == 0x73078865ca31234dull /* TD_backwash (shore foam) */ &&
                    foamOffset.length() > 0.0001f ) {
            itemWorld = Matrix4x4f::Translation( foamOffset ) * itemWorld;
        }
        if ( !renderer.isBoxVisible( itemWorld, item.BoundsMin, item.BoundsMax ) ) {
            continue;
        }
        renderer.draw( item.Mesh, materials[item.MaterialIndex], itemWorld, pParams );
    }
}

void EdenModel::buildSurfaceGrid() const {
    bSurfaceGridBuilt = true;
    surfaceCellStart.clear();
    surfaceCellTriangles.clear();
    surfaceGridWidth = surfaceGridHeight = 0;
    if ( collisionIndices.size() < 3 || collisionVertices.empty() ) {
        return;
    }
    float minX = FLT_MAX, minZ = FLT_MAX, maxX = -FLT_MAX, maxZ = -FLT_MAX;
    for ( const Vector3f &vertex : collisionVertices ) {
        minX = Min( minX, vertex.x );
        minZ = Min( minZ, vertex.z );
        maxX = Max( maxX, vertex.x );
        maxZ = Max( maxZ, vertex.z );
    }
    // Cells of 16 m, at most 512 a side (a model wider than 8 km gets
    // coarser cells; the sector models are 768 m -> 48 x 48).
    const float cell = Max( kSurfaceCellSize, Max( maxX - minX, maxZ - minZ ) / 512.0f );
    surfaceCellSize = cell;
    surfaceGridMinX = minX;
    surfaceGridMinZ = minZ;
    surfaceGridWidth = static_cast<int32_t>( ( maxX - minX ) / cell ) + 1;
    surfaceGridHeight = static_cast<int32_t>( ( maxZ - minZ ) / cell ) + 1;
    const float inverse = 1.0f / cell;
    auto cellRange = [&]( size_t i, int32_t &x0, int32_t &z0, int32_t &x1, int32_t &z1 ) {
        const Vector3f &a = collisionVertices[collisionIndices[i]];
        const Vector3f &b = collisionVertices[collisionIndices[i + 1]];
        const Vector3f &c = collisionVertices[collisionIndices[i + 2]];
        x0 = static_cast<int32_t>( ( Min( a.x, Min( b.x, c.x ) ) - minX ) * inverse );
        z0 = static_cast<int32_t>( ( Min( a.z, Min( b.z, c.z ) ) - minZ ) * inverse );
        x1 = Min( static_cast<int32_t>( ( Max( a.x, Max( b.x, c.x ) ) - minX ) * inverse ), surfaceGridWidth - 1 );
        z1 = Min( static_cast<int32_t>( ( Max( a.z, Max( b.z, c.z ) ) - minZ ) * inverse ), surfaceGridHeight - 1 );
    };
    // Counting sort into cells: count, prefix sum, fill.
    const size_t numCells = static_cast<size_t>( surfaceGridWidth ) * surfaceGridHeight;
    surfaceCellStart.assign( numCells + 1, 0 );
    int32_t x0, z0, x1, z1;
    for ( size_t i = 0; i + 2 < collisionIndices.size(); i += 3 ) {
        cellRange( i, x0, z0, x1, z1 );
        for ( int32_t z = z0; z <= z1; z++ ) {
            for ( int32_t x = x0; x <= x1; x++ ) {
                surfaceCellStart[static_cast<size_t>( z ) * surfaceGridWidth + x + 1]++;
            }
        }
    }
    for ( size_t i = 0; i < numCells; i++ ) {
        surfaceCellStart[i + 1] += surfaceCellStart[i];
    }
    surfaceCellTriangles.resize( surfaceCellStart[numCells] );
    std::vector<uint32_t> fill( surfaceCellStart.begin(), surfaceCellStart.end() - 1 );
    for ( size_t i = 0; i + 2 < collisionIndices.size(); i += 3 ) {
        cellRange( i, x0, z0, x1, z1 );
        for ( int32_t z = z0; z <= z1; z++ ) {
            for ( int32_t x = x0; x <= x1; x++ ) {
                surfaceCellTriangles[fill[static_cast<size_t>( z ) * surfaceGridWidth + x]++] = static_cast<uint32_t>( i );
            }
        }
    }
}

bool EdenModel::getSurfaceCell( float x, float z, const uint32_t *&pBegin, const uint32_t *&pEnd ) const {
    if ( !bSurfaceGridBuilt ) {
        buildSurfaceGrid();
    }
    if ( surfaceGridWidth == 0 ) {
        return false;
    }
    const int32_t cx = static_cast<int32_t>( std::floor( ( x - surfaceGridMinX ) / surfaceCellSize ) );
    const int32_t cz = static_cast<int32_t>( std::floor( ( z - surfaceGridMinZ ) / surfaceCellSize ) );
    if ( cx < 0 || cz < 0 || cx >= surfaceGridWidth || cz >= surfaceGridHeight ) {
        return false;
    }
    const size_t index = static_cast<size_t>( cz ) * surfaceGridWidth + cx;
    pBegin = surfaceCellTriangles.data() + surfaceCellStart[index];
    pEnd = surfaceCellTriangles.data() + surfaceCellStart[index + 1];
    return pBegin != pEnd;
}

bool EdenModel::getCeilingHeight( float x, float z, float minHeight, float &outHeight ) const {
    // Same collision walk as getSurfaceHeight, but it keeps the LOWEST surface
    // ABOVE minHeight: the roof over the point (a tunnel bore, a building).
    bool bFound = false;
    const uint32_t *pBegin = nullptr, *pEnd = nullptr;
    if ( !getSurfaceCell( x, z, pBegin, pEnd ) ) {
        return false;
    }
    for ( const uint32_t *pTriangle = pBegin; pTriangle != pEnd; ++pTriangle ) {
        const size_t i = *pTriangle;
        const Vector3f &a = collisionVertices[collisionIndices[i]];
        const Vector3f &b = collisionVertices[collisionIndices[i + 1]];
        const Vector3f &c = collisionVertices[collisionIndices[i + 2]];
        if ( x < Min( a.x, Min( b.x, c.x ) ) || x > Max( a.x, Max( b.x, c.x ) ) || z < Min( a.z, Min( b.z, c.z ) ) ||
             z > Max( a.z, Max( b.z, c.z ) ) ) {
            continue;
        }
        const float denominator = ( b.z - c.z ) * ( a.x - c.x ) + ( c.x - b.x ) * ( a.z - c.z );
        if ( std::abs( denominator ) < 1e-8f ) {
            continue;
        }
        const float u = ( ( b.z - c.z ) * ( x - c.x ) + ( c.x - b.x ) * ( z - c.z ) ) / denominator;
        const float v = ( ( c.z - a.z ) * ( x - c.x ) + ( a.x - c.x ) * ( z - c.z ) ) / denominator;
        const float w = 1.0f - u - v;
        if ( u < 0.0f || v < 0.0f || w < 0.0f ) {
            continue;
        }
        const float y = u * a.y + v * b.y + w * c.y;
        if ( y >= minHeight && ( !bFound || y < outHeight ) ) {
            outHeight = y;
            bFound = true;
        }
    }
    return bFound;
}

bool EdenModel::getSurfaceHeight( float x, float z, float maxHeight, float &outHeight ) const {
    bool bFound = false;
    const uint32_t *pBegin = nullptr, *pEnd = nullptr;
    if ( !getSurfaceCell( x, z, pBegin, pEnd ) ) {
        return false;
    }
    for ( const uint32_t *pTriangle = pBegin; pTriangle != pEnd; ++pTriangle ) {
        const size_t i = *pTriangle;
        const Vector3f &a = collisionVertices[collisionIndices[i]];
        const Vector3f &b = collisionVertices[collisionIndices[i + 1]];
        const Vector3f &c = collisionVertices[collisionIndices[i + 2]];
        if ( x < Min( a.x, Min( b.x, c.x ) ) || x > Max( a.x, Max( b.x, c.x ) ) || z < Min( a.z, Min( b.z, c.z ) ) ||
             z > Max( a.z, Max( b.z, c.z ) ) ) {
            continue;
        }
        // Barycentric coordinates in the XZ plane.
        const float denominator = ( b.z - c.z ) * ( a.x - c.x ) + ( c.x - b.x ) * ( a.z - c.z );
        if ( std::abs( denominator ) < 1e-8f ) {
            continue;
        }
        const float u = ( ( b.z - c.z ) * ( x - c.x ) + ( c.x - b.x ) * ( z - c.z ) ) / denominator;
        const float v = ( ( c.z - a.z ) * ( x - c.x ) + ( a.x - c.x ) * ( z - c.z ) ) / denominator;
        const float w = 1.0f - u - v;
        if ( u < 0.0f || v < 0.0f || w < 0.0f ) {
            continue;
        }
        const float y = u * a.y + v * b.y + w * c.y;
        if ( y <= maxHeight && ( !bFound || y > outHeight ) ) {
            outHeight = y;
            bFound = true;
        }
    }
    return bFound;
}

namespace {
// Möller-Trumbore: distance along `direction` to the triangle (a ray, not a
// segment; the caller normalizes the direction so distances compare).
bool IntersectTriangle( const Vector3f &origin, const Vector3f &direction, const Vector3f &a, const Vector3f &b,
                        const Vector3f &c, float &outDistance ) {
    const Vector3f edge1 = b - a;
    const Vector3f edge2 = c - a;
    const Vector3f p = direction.cross( edge2 );
    const float determinant = edge1.dot( p );
    if ( std::abs( determinant ) < 1e-12f ) {
        return false;
    }
    const float inverse = 1.0f / determinant;
    const Vector3f t = origin - a;
    const float u = t.dot( p ) * inverse;
    if ( u < 0.0f || u > 1.0f ) {
        return false;
    }
    const Vector3f q = t.cross( edge1 );
    const float v = direction.dot( q ) * inverse;
    if ( v < 0.0f || u + v > 1.0f ) {
        return false;
    }
    const float distance = edge2.dot( q ) * inverse;
    if ( distance < 0.0f ) {
        return false;
    }
    outDistance = distance;
    return true;
}

// Slab test of an axis aligned box (nearest positive hit).
bool IntersectBox( const Vector3f &origin, const Vector3f &direction, const Vector3f &min, const Vector3f &max,
                   float &outDistance ) {
    float tMin = 0.0f;
    float tMax = 1e30f;
    const float o[3] = { origin.x, origin.y, origin.z };
    const float d[3] = { direction.x, direction.y, direction.z };
    const float lo[3] = { min.x, min.y, min.z };
    const float hi[3] = { max.x, max.y, max.z };
    for ( int32_t axis = 0; axis < 3; axis++ ) {
        if ( std::abs( d[axis] ) < 1e-12f ) {
            if ( o[axis] < lo[axis] || o[axis] > hi[axis] ) {
                return false;
            }
            continue;
        }
        const float inverse = 1.0f / d[axis];
        float t0 = ( lo[axis] - o[axis] ) * inverse;
        float t1 = ( hi[axis] - o[axis] ) * inverse;
        if ( t0 > t1 ) {
            const float swap = t0;
            t0 = t1;
            t1 = swap;
        }
        tMin = Max( tMin, t0 );
        tMax = Min( tMax, t1 );
        if ( tMin > tMax ) {
            return false;
        }
    }
    outDistance = tMin;
    return true;
}
} // namespace

bool EdenModel::raycast( const Vector3f &origin, const Vector3f &direction, float &outDistance,
                         int32_t &outNodeIndex ) const {
    float best = 1e30f;
    int32_t bestNode = -1;
    for ( const Item &item : items ) {
        if ( item.NodeIndex < 0 || item.NodeIndex >= static_cast<int32_t>( nodeTransforms.size() ) ) {
            continue;
        }
        if ( item.CollisionCount >= 3 ) {
            const uint32_t end = Min<uint32_t>( item.CollisionFirst + item.CollisionCount,
                                                static_cast<uint32_t>( collisionIndices.size() ) );
            for ( uint32_t i = item.CollisionFirst; i + 2 < end; i += 3 ) {
                float distance = 0.0f;
                if ( IntersectTriangle( origin, direction, collisionVertices[collisionIndices[i]],
                                        collisionVertices[collisionIndices[i + 1]],
                                        collisionVertices[collisionIndices[i + 2]], distance ) &&
                     distance < best ) {
                    best = distance;
                    bestNode = item.NodeIndex;
                }
            }
            continue;
        }
        // No collision copy: the node local item box, taken to the space of the
        // collision copy (node.World) as its axis aligned hull.
        const Matrix4x4f &node = nodeTransforms[item.NodeIndex];
        Vector3f boxMin( 1e30f, 1e30f, 1e30f ), boxMax( -1e30f, -1e30f, -1e30f );
        for ( int32_t corner = 0; corner < 8; corner++ ) {
            const Vector3f p( ( corner & 1 ) != 0 ? item.BoundsMax.x : item.BoundsMin.x,
                              ( corner & 2 ) != 0 ? item.BoundsMax.y : item.BoundsMin.y,
                              ( corner & 4 ) != 0 ? item.BoundsMax.z : item.BoundsMin.z );
            const Vector3f w = node.transformPoint( p );
            boxMin = Vector3f( Min( boxMin.x, w.x ), Min( boxMin.y, w.y ), Min( boxMin.z, w.z ) );
            boxMax = Vector3f( Max( boxMax.x, w.x ), Max( boxMax.y, w.y ), Max( boxMax.z, w.z ) );
        }
        float distance = 0.0f;
        if ( IntersectBox( origin, direction, boxMin, boxMax, distance ) && distance < best ) {
            best = distance;
            bestNode = item.NodeIndex;
        }
    }
    if ( bestNode < 0 ) {
        return false;
    }
    outDistance = best;
    outNodeIndex = bestNode;
    return true;
}

bool EdenModel::getCollisionBounds( Vector3f &outMin, Vector3f &outMax ) const {
    if ( collisionVertices.empty() ) {
        return false;
    }
    outMin = Vector3f( 1e30f, 1e30f, 1e30f );
    outMax = Vector3f( -1e30f, -1e30f, -1e30f );
    for ( const Vector3f &vertex : collisionVertices ) {
        outMin = Vector3f( Min( outMin.x, vertex.x ), Min( outMin.y, vertex.y ), Min( outMin.z, vertex.z ) );
        outMax = Vector3f( Max( outMax.x, vertex.x ), Max( outMax.y, vertex.y ), Max( outMax.z, vertex.z ) );
    }
    return true;
}

void EdenModel::drawNode( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world ) const {
    for ( const Item &item : items ) {
        if ( item.Lod == 0 && item.NodeIndex == nodeIndex ) {
            renderer.draw( item.Mesh, materials[item.MaterialIndex], world * nodeTransforms[item.NodeIndex] );
        }
    }
}

void EdenModel::drawNodeItems( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world,
                               const EdenDrawParams *pParams ) const {
    if ( nodeIndex < 0 || nodeIndex >= static_cast<int32_t>( nodeItems.size() ) ) {
        return;
    }
    const auto &range = nodeItems[nodeIndex];
    for ( uint32_t i = range.first; i < range.first + range.second; i++ ) {
        renderer.draw( items[i].Mesh, materials[items[i].MaterialIndex], world, pParams );
    }
}

void EdenModel::drawNodeItemsSkinned( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world,
                                      const std::vector<Matrix4x4f> &skinMatrices,
                                      const EdenDrawParams *pParams ) const {
    if ( nodeIndex < 0 || nodeIndex >= static_cast<int32_t>( nodeItems.size() ) || skinMatrices.empty() ) {
        return;
    }
    const auto &range = nodeItems[nodeIndex];
    EdenDrawParams params;
    if ( pParams != nullptr ) {
        // The caller's params (the S-list TD_Ground sectrOff / -s override,
        // the instance's neon blink AnimValues) merged with the per-item
        // skin palette below.
        params.Vectors = pParams->Vectors;
        params.TextureOverrides = pParams->TextureOverrides;
        params.MaterialVectors = pParams->MaterialVectors;
        params.bDeferBlended = pParams->bDeferBlended;
    }
    Matrix4x4f palette[32];
    for ( uint32_t i = range.first; i < range.first + range.second; i++ ) {
        const Item &item = items[i];
        if ( item.BonePalette.empty() ) {
            // Not a skinned primitive of the model: the instance matrix alone.
            renderer.draw( item.Mesh, materials[item.MaterialIndex], world );
            continue;
        }
        const uint32_t count = Min<uint32_t>( static_cast<uint32_t>( item.BonePalette.size() ), 32u );
        for ( uint32_t b = 0; b < count; b++ ) {
            const uint32_t bone = item.BonePalette[b];
            palette[b] = bone < skinMatrices.size() ? skinMatrices[bone] : Matrix4x4f::Identity();
        }
        params.pSkinMatrices = palette;
        params.NumSkinMatrices = count;
        renderer.draw( item.Mesh, materials[item.MaterialIndex], world, &params );
    }
}

bool EdenModel::getNodeBounds( int32_t nodeIndex, Vector3f &outMin, Vector3f &outMax ) const {
    if ( nodeIndex < 0 || nodeIndex >= static_cast<int32_t>( nodeItems.size() ) || nodeItems[nodeIndex].second == 0 ) {
        return false;
    }
    const auto &range = nodeItems[nodeIndex];
    outMin = Vector3f( 1e9f, 1e9f, 1e9f );
    outMax = Vector3f( -1e9f, -1e9f, -1e9f );
    for ( uint32_t i = range.first; i < range.first + range.second; i++ ) {
        const Item &item = items[i];
        outMin = Vector3f( Min( outMin.x, item.BoundsMin.x ), Min( outMin.y, item.BoundsMin.y ),
                           Min( outMin.z, item.BoundsMin.z ) );
        outMax = Vector3f( Max( outMax.x, item.BoundsMax.x ), Max( outMax.y, item.BoundsMax.y ),
                           Max( outMax.z, item.BoundsMax.z ) );
    }
    return true;
}

void EdenModel::drawSkinned( EdenRenderer &renderer, const Matrix4x4f &world,
                             const std::vector<Matrix4x4f> &skinMatrices, const EdenAvatarLighting *pLighting ) const {
    EdenDrawParams params;
    params.bDeferBlended = true;
    params.pAvatarLighting = pLighting;
    EdenDrawParams rigidParams;
    rigidParams.bDeferBlended = true;
    rigidParams.pAvatarLighting = pLighting;
    Matrix4x4f palette[32];
    for ( const Item &item : items ) {
        if ( item.Lod != 0 || !nodeVisible[item.NodeIndex] ) {
            continue;
        }
        if ( item.BonePalette.empty() ) {
            const Matrix4x4f itemWorld = world * nodeTransforms[item.NodeIndex];
            if ( renderer.isBoxVisible( itemWorld, item.BoundsMin, item.BoundsMax ) ) {
                renderer.draw( item.Mesh, materials[item.MaterialIndex], itemWorld,
                               pLighting != nullptr ? &rigidParams : nullptr );
            }
            continue;
        }
        // Skinned: bind pose bounds under the actor root; the animated pose
        // stays within a couple of metres of it [INF].
        if ( !renderer.isBoxVisible( world, item.BoundsMin, item.BoundsMax, kSkinnedCullMargin ) ) {
            continue;
        }
        const uint32_t count = Min<uint32_t>( static_cast<uint32_t>( item.BonePalette.size() ), 32u );
        for ( uint32_t i = 0; i < count; i++ ) {
            const uint32_t bone = item.BonePalette[i];
            palette[i] = bone < skinMatrices.size() ? skinMatrices[bone] : Matrix4x4f::Identity();
        }
        params.pSkinMatrices = palette;
        params.NumSkinMatrices = count;
        renderer.draw( item.Mesh, materials[item.MaterialIndex], world, &params );
    }
}

uint32_t EdenModel::bindUVAnimations( const Bank &bank ) {
    const std::vector<uint64_t> &ids = localMaterials.getUVAnimationIds();
    if ( ids.empty() ) {
        return 0;
    }
    uint32_t numBound = 0;
    for ( const BankEntry *pEntry : bank.findEntriesByExtension( "uva" ) ) {
        auto pAnimation = std::make_shared<EdenUVAnimation>();
        if ( !pAnimation->parse( pEntry->pData, pEntry->Size ) ||
             std::find( ids.begin(), ids.end(), pAnimation->Id ) == ids.end() ) {
            continue;
        }
        uint32_t slot = 0;
        std::vector<std::string> candidates;
        if ( !EdenUVAnimation::ParseName( pEntry->Name, slot, candidates ) || slot >= 4 ) {
            continue;
        }
        bool bFound = false;
        for ( const std::string &candidate : candidates ) {
            const uint64_t hashcode = GetIdentifier64bit( candidate.c_str() );
            for ( EdenMaterial &material : materials ) {
                if ( material.Hashcode == hashcode ) {
                    material.UVAnimations[slot] = pAnimation;
                    bFound = true;
                }
            }
            if ( bFound ) {
                break;
            }
        }
        if ( bFound ) {
            numBound++;
            OTDU_LOG_DEBUG( "EdenModel: UV animation '%s' (%.1f s) -> __UVA_%u\n", pEntry->Name.c_str(),
                            pAnimation->Duration, slot );
        }
    }
    return numBound;
}

namespace {
// "_MHA_01" -> part 'A', number 1; false for any other name.
bool ParseMyHouseMaterial( const std::string &name, char &outPart, int32_t &outNumber ) {
    if ( name.size() != 7 || name.compare( 0, 3, "_MH" ) != 0 || name[4] != '_' || !isdigit( (unsigned char)name[5] ) ||
         !isdigit( (unsigned char)name[6] ) ) {
        return false;
    }
    outPart = static_cast<char>( toupper( (unsigned char)name[3] ) );
    outNumber = ( name[5] - '0' ) * 10 + ( name[6] - '0' );
    return true;
}
} // namespace

int32_t EdenModel::setMyHouseColor( int32_t variant ) {
    int32_t moved = 0;
    for ( Item &item : items ) {
        if ( item.MaterialIndex >= materials.size() ) {
            continue;
        }
        char part = 0;
        int32_t number = 0;
        if ( !ParseMyHouseMaterial( materials[item.MaterialIndex].Name, part, number ) ) {
            continue;
        }
        for ( size_t m = 0; m < materials.size(); m++ ) {
            char otherPart = 0;
            int32_t otherNumber = 0;
            if ( ParseMyHouseMaterial( materials[m].Name, otherPart, otherNumber ) && otherPart == part &&
                 otherNumber == variant + 1 ) {
                if ( item.MaterialIndex != m ) {
                    item.MaterialIndex = static_cast<uint32_t>( m );
                    moved++;
                }
                break;
            }
        }
    }
    return moved;
}

int32_t EdenModel::countMyHouseColors() const {
    int32_t count = 0;
    for ( const EdenMaterial &material : materials ) {
        char part = 0;
        int32_t number = 0;
        if ( ParseMyHouseMaterial( material.Name, part, number ) && part == 'A' ) {
            count = Max( count, number );
        }
    }
    return count;
}
