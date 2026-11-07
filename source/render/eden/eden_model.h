#pragma once

#include <array>

#include "render/2dm.h"
#include "render/3dd.h"
#include "render/3dg.h"
#include "render/eden/eden_renderer.h"
#include "render/rt/ray_tracing.h"

class Bank;
struct BankEntry;

// Scene (.3DD + .3DG + .2DM) rendered with the original shaders, in the
// original D3D space.
class EdenModel {
public:
    struct Item {
        int32_t NodeIndex = -1;
        uint32_t Lod = 0; // INS. LOD level of the item (0 unless setLoadLods)
        uint32_t MaterialIndex = 0;
        EdenMesh Mesh;
        Vector3f BoundsMin;
        Vector3f BoundsMax;
        uint32_t NumVertices = 0;          // vertices of the primitive (the tool panels show it)
        std::vector<uint32_t> BonePalette; // skinned: skeleton bone of each palette entry
        // The primitive's own indices (into the item's vertex range of the
        // shared buffer), kept when setKeepItemIndices(true) was set before
        // load - the animated wipers split the parked wiper prim by them.
        std::vector<uint32_t> PrimitiveIndices;
        // The primitive's own vertex positions (primitive-local, the same
        // space the indices and the item bounds live in), kept with the
        // indices. Cleared by the consumer after the split (a body prim is
        // small, but a whole car of these would sit in memory for nothing).
        std::vector<Vector3f> PrimitiveVertices;
        // Triangles of this item in the CPU collision copy (setKeepCollision):
        // what raycast() walks. 0 when the copy is not kept.
        uint32_t CollisionFirst = 0; // first index (three per triangle)
        uint32_t CollisionCount = 0;
        // TD_Impostor items: the raw vertex streams and indices kept on the
        // CPU. The impostor vertex shader has no world matrix (vs_0 / vs_2:
        // gl_Position = ViewProj * in_Position0 [SHADER]) - its positions are
        // world space, so EdenWorld batches every instance's quad, transformed
        // on the CPU, into one draw per material and pass. A quad is 4 vertices.
        std::vector<uint8_t> ImpostorStreams[2];
        std::vector<uint32_t> ImpostorIndices;
    };

    using NodeFilter = std::function<bool( const Render3DD &scene, int32_t nodeIndex )>;
    using MaterialSetup = std::function<void( EdenMaterial &material )>;
    // Optional post-parse hook: signs use it to rewrite the plate UVs of
    // models whose plates stack on one atlas cell (sign_text.cpp).
    using GeometrySetup = std::function<void( Render3DG &geometry )>;

    inline bool isLoaded() const { return pVertexBuffer != nullptr; }
    inline const Render3DD &getScene() const { return scene; }
    inline const std::vector<Item> &getItems() const { return items; }
    // The [first, count) item range of a node (drawNodeItems / the neon
    // registration walk the same lists).
    std::pair<uint32_t, uint32_t> getNodeItemRange( int32_t nodeIndex ) const {
        if ( nodeIndex < 0 || nodeIndex >= static_cast<int32_t>( nodeItems.size() ) ) {
            return { 0u, 0u };
        }
        return nodeItems[nodeIndex];
    }
    // The [first, count) item range of one LOD of a node (LOD0 is the call
    // above). LOD1/2 exist only when setLoadLods(true) was set before load -
    // the world library model, whose V / O .pmi instances switch level by
    // distance (docs/re/pmi_streaming_spec.md 9 #3).
    std::pair<uint32_t, uint32_t> getNodeItemRange( int32_t nodeIndex, uint32_t lod ) const {
        if ( nodeIndex < 0 || nodeIndex >= static_cast<int32_t>( nodeLodItems.size() ) || lod >= 3 ) {
            return { 0u, 0u };
        }
        return nodeLodItems[nodeIndex][lod];
    }
    // Loads every INS. LOD geometry, not just LOD0 (off by default: cars,
    // props and actors draw their LOD0 items only).
    void setLoadLods( bool bEnable ) { bLoadLods = bEnable; }
    inline std::vector<EdenMaterial> &getMaterials() { return materials; }
    // The MyHouse colour variants (houses_spec.md 12): the items drawn with a
    // "_MH<part>_<nn>" material switch to "_MH<part>_<variant + 1>" of the same
    // library (every part A / B / C at once). Returns the number of items moved.
    int32_t setMyHouseColor( int32_t variant );
    // How many "_MHA_nn" colours the library holds (0: no colour parts).
    int32_t countMyHouseColors() const;
    inline const Vector3f &getBoundsMin() const { return boundsMin; }
    inline const Vector3f &getBoundsMax() const { return boundsMax; }
    inline const Matrix4x4f &getNodeTransform( int32_t nodeIndex ) const { return nodeTransforms[nodeIndex]; }
    void setNodeTransform( int32_t nodeIndex, const Matrix4x4f &transform ) { nodeTransforms[nodeIndex] = transform; }
    void setNodeVisible( int32_t nodeIndex, bool bVisible ) { nodeVisible[nodeIndex] = bVisible; }
    bool isNodeVisible( int32_t nodeIndex ) const {
        return nodeIndex < 0 || nodeIndex >= static_cast<int32_t>( nodeVisible.size() ) || nodeVisible[nodeIndex];
    }

public:
    EdenModel();
    ~EdenModel();

    // pFallbackMaterials: shared material library (e.g. commonworld library.2dm).
    bool load( EdenRenderer &renderer, const BankEntry &sceneEntry, const BankEntry &geometryEntry,
               const BankEntry *pMaterialEntry, const Render2DM *pFallbackMaterials, const char *pTextureOwner,
               const NodeFilter &filter = nullptr, const MaterialSetup &materialSetup = nullptr,
               const GeometrySetup &geometrySetup = nullptr );
    // Convenience: first 3dd/3dg/2dm of a bank (or the entries named pBaseName).
    bool load( EdenRenderer &renderer, const Bank &bank, const char *pBaseName, const Render2DM *pFallbackMaterials,
               const char *pTextureOwner, const NodeFilter &filter = nullptr,
               const MaterialSetup &materialSetup = nullptr, const GeometrySetup &geometrySetup = nullptr );
    void destroy();

    // Texture coordinate animations (.UVA entries of 'bank', see
    // EdenUVAnimation) of this model's materials: an animation is bound when
    // its id is listed in the UVAA section of the model's material library
    // and its file name resolves to one of the materials (by hashcode).
    // Returns the number of bound animations.
    uint32_t bindUVAnimations( const Bank &bank );
    inline const Render2DM &getLocalMaterials() const { return localMaterials; }

    // Keeps a CPU copy of the triangles (model space) for height queries
    // (set before load).
    inline void setKeepCollision( bool bKeep ) { bKeepCollision = bKeep; }
    // Keeps each item's primitive indices (set before load; the animated
    // wipers split the parked wiper prim by them).
    inline void setKeepItemIndices( bool bKeep ) { bKeepItemIndices = bKeep; }
    // Frees the kept primitive data (the consumer has split what it needed).
    void clearPrimitiveData();
    // OpenTDU2 shore swell: the sea (TD_Ocean) rises and falls by
    // oceanYOffset metres; the retail shore foam (TD_backwash) is moved along
    // the beach gradient by foamOffset so the surf zone runs up and down the
    // sand instead of floating in place (docs/re/ocean_spec.md 11.4).
    inline void setOceanYOffset( float offset ) { oceanYOffset = offset; }
    inline void setFoamOffset( const Vector3f &offset ) { foamOffset = offset; }
    // Highest surface below maxHeight at (x, z) (model space).
    bool getSurfaceHeight( float x, float z, float maxHeight, float &outHeight ) const;
    // Lowest surface above minHeight at (x, z) (model space): the ceiling over
    // a point, i.e. a tunnel bore's roof or a building's floor / overhang. The
    // falling rain uses it (through EdenWorld) to stop at the roof instead of
    // clipping through it.
    bool getCeilingHeight( float x, float z, float minHeight, float &outHeight ) const;
    // The CPU collision copy (model space, node transforms applied): read by
    // EdenWorld's carve ground (the tunnel / bridge floors the drive physics
    // stands on where the heightfield is carved away). Empty unless
    // setKeepCollision(true) was set before load.
    inline const std::vector<Vector3f> &getCollisionVertices() const { return collisionVertices; }
    inline const std::vector<uint32_t> &getCollisionIndices() const { return collisionIndices; }

    void draw( EdenRenderer &renderer, const Matrix4x4f &world, const EdenDrawParams *pParams = nullptr ) const;
    // Skinned draw: skinMatrices[bone] = bone world * inverse bind (model
    // space, column vectors); each primitive uploads its palette
    // (__Skin_Matrix, 32 bones x 3 rows). Non skinned items use their node.
    void drawSkinned( EdenRenderer &renderer, const Matrix4x4f &world,
                      const std::vector<Matrix4x4f> &skinMatrices,
                      const struct EdenAvatarLighting *pLighting = nullptr ) const;
    // Draws a single node subtree item list (instancing helpers).
    void drawNode( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world ) const;
    // Draws the items of one node with 'world' only (the node transform is
    // ignored: library models instanced by the .pmi lists).
    void drawNodeItems( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world,
                        const EdenDrawParams *pParams = nullptr ) const;
    // Skinned variant of drawNodeItems, for the .pmi S lists (guard rails): the
    // node's items are drawn with the instance's own bone palette (the payload
    // of the .pmi record, model space) and then 'world' - the same order the
    // vertex shader uses (v * Bone_i * World).
    void drawNodeItemsSkinned( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world,
                               const std::vector<Matrix4x4f> &skinMatrices,
                               const EdenDrawParams *pParams = nullptr ) const;
    // Model space bounds of the items of a node; false if it has none.
    bool getNodeBounds( int32_t nodeIndex, Vector3f &outMin, Vector3f &outMax ) const;

    // Picking (the asset viewer's click-to-label): nearest mesh hit along a ray
    // given in the space of the `world` matrix of draw(), i.e. transform the ray
    // by world.inverse() before calling (the collision copy stores the vertices
    // with their node transform already applied, which is exactly the space
    // draw()'s world matrix maps to the world). Exact on the triangles when the
    // collision copy was kept (setKeepCollision(true) before load), box level
    // (per node) otherwise. outNodeIndex: the scene node of the hit mesh
    // (getScene().getNodes()[outNodeIndex].Name). False when nothing is hit;
    // outDistance is then untouched.
    bool raycast( const Vector3f &origin, const Vector3f &direction, float &outDistance,
                  int32_t &outNodeIndex ) const;
    // Bounds of the CPU collision copy (the space of raycast's ray); false when
    // the model keeps none. The tool panels compare them with the drawn bounds:
    // a difference means the parsed vertices and the original streams disagree.
    bool getCollisionBounds( Vector3f &outMin, Vector3f &outMax ) const;

private:
    Render3DD scene;
    Render2DM localMaterials;
    std::vector<EdenMaterial> materials;
    std::vector<Item> items;
    std::vector<Matrix4x4f> nodeTransforms;
    std::vector<bool> nodeVisible;
    std::vector<std::pair<uint32_t, uint32_t>> nodeItems; // first item, count
    // The same range per INS. LOD level ([node][lod]); LOD0 == nodeItems.
    std::vector<std::array<std::pair<uint32_t, uint32_t>, 3>> nodeLodItems;
    bool bLoadLods = false;
    std::vector<Vector3f> collisionVertices;
    std::vector<uint32_t> collisionIndices;
    // XZ grid over the collision triangles (built at the first height query):
    // getSurfaceHeight / getCeilingHeight walked every triangle of the model on
    // each call - several calls a frame per loaded sector (the ambience shelter
    // test, the surface height), ~6 % of the main thread on a drive.
    static constexpr float kSurfaceCellSize = 16.0f;
    mutable std::vector<uint32_t> surfaceCellStart;     // gridWidth * gridHeight + 1
    mutable std::vector<uint32_t> surfaceCellTriangles; // first index of each triangle in collisionIndices
    mutable float surfaceCellSize = kSurfaceCellSize;
    mutable float surfaceGridMinX = 0.0f;
    mutable float surfaceGridMinZ = 0.0f;
    mutable int32_t surfaceGridWidth = 0;
    mutable int32_t surfaceGridHeight = 0;
    mutable bool bSurfaceGridBuilt = false;
    void buildSurfaceGrid() const;
    // The triangles whose XZ box may contain (x, z); false when none.
    bool getSurfaceCell( float x, float z, const uint32_t *&pBegin, const uint32_t *&pEnd ) const;
    bool bKeepCollision = false;
    bool bKeepItemIndices = false;
    float oceanYOffset = 0.0f;          // OpenTDU2 shore swell (setOceanYOffset)
    Vector3f foamOffset = Vector3f();   // OpenTDU2 surf run-up (setFoamOffset)
    GPUBuffer *pVertexBuffer;
    GPUBuffer *pIndexBuffer;
    // Ray tracing: one bottom level structure per item (empty when ray tracing
    // is off). Owned by RayTracing; released by destroy() while it is alive.
    std::vector<RayTracing::Blas *> blases;
    Vector3f boundsMin;
    Vector3f boundsMax;
};
