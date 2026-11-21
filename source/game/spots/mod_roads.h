#pragma once

#include "core/math.h"
#include "game/scene_editor/scene_doc.h"
#include "game/spots/obj_mesh.h"
#include "render/eden/eden_renderer.h"

#include <string>
#include <vector>

class EdenWorld;
class VehicleWorldCollision;

// Mod roads: roads built from splines (the scene editor's road tool; the
// scene documents carry them, docs/re/scene_editor.md 3). The sibling of
// mod_props: the game data is never written, everything lives in the mods
// overlay — `<mods>/roads/<island>.json` (a `{"roads": [...]}` document, the
// same road shape a scene file nests) and the textures beside the props.
//
// A road renders as a ribbon (Catmull-Rom through the control points,
// resampled ~2 m, distance-based UVs) lit by a stock shader family
// (TD_Gouraud), registers its triangles as driveable surface
// (EdenWorld::addRoadSurfaceTriangles — getSurfaceHeight answers a scene
// road like a retail one) and as car collision
// (VehicleWorldCollision::addStaticTriangles), and suppresses the grass
// along itself (EdenWorld::appendRoadShapes).
class ModRoads {
public:
    struct Road {
        std::string Id;
        std::vector<Vector3f> Points; // the spline control points (world)
        float Width = 7.0f;
        int32_t Lanes = 2;
        bool bClosed = false;
        bool bCollide = true;
        std::string Texture; // props-folder PNG; empty = the default texture of the surface
        std::string Surface; // "" | tarmac | dirt | grass (SceneRoad::Surface)
        std::vector<float> Banks; // per control point, degrees (SceneRoad::Banks)
        // Runtime (rebuilt by load / rebuild):
        Vector3f BoundsMin, BoundsMax;
        std::vector<ObjMeshDraw> Draws;    // GPU (owned here)
        std::vector<Vector3f> SolidVertices; // world-space triangle soup (collision + surfaces)
        std::vector<uint32_t> SolidIndices;
        std::vector<Vector3f> Centerline;  // resampled (the grass shapes)
    };

    // `<mods>/roads/<island>.json` of the overlay (missing file: empty, the
    // normal case).
    bool load( EdenRenderer &renderer, const std::string &island, EdenWorld *pWorld = nullptr );
    // A scene document's roads (the editor's live rebuild; a mode that loads
    // scenes instead of the roads file). pWorld (optional): the samples that
    // dip under the heightfield between the control points lift to the
    // terrain (+5 cm; a ground road follows the rolling ground, a bridge's
    // explicit height stays).
    void loadDocument( EdenRenderer &renderer, const SceneDocument &document, EdenWorld *pWorld = nullptr );
    void unload();
    inline bool isLoaded() const { return bLoaded; }
    inline const std::vector<Road> &getRoads() const { return roads; }

    void draw( EdenRenderer &renderer ) const;
    // The collision soup into the vehicle world (FREERIDE; per road with
    // bCollide). Returns the triangle count added.
    uint32_t registerCollision( VehicleWorldCollision &collision ) const;
    // EdenWorld: the driveable-surface triangles + the grass shapes.
    void registerSurfaces( class EdenWorld &world ) const;

    // --- the mesh builder (shared with the editor and the bake export) ---
    // Catmull-Rom through the control points, ~spacing metres, closed-loop
    // aware; also the tangents (per resampled point).
    // pValues / pOutValues (optional): a per-control-point scalar (the bank) interpolated onto the samples.
    static void BuildCenterline( const std::vector<Vector3f> &points, bool bClosed, float spacing,
                                 std::vector<Vector3f> &outCenter, const std::vector<float> *pValues = nullptr,
                                 std::vector<float> *pOutValues = nullptr );
    // The ribbon as OBJ + MTL text in the AUTHORING space (the loader
    // mirrors X + reverses winding, the mod_props law): the in-memory route
    // (BuildObjDraws) for the live draw and the bake export both use it.
    // pBank (optional, one angle in degrees per centre sample): rolls the cross-section about the road's axis.
    static void BuildRoadObjText( const std::vector<Vector3f> &center, float width, const std::string &textureName,
                                  std::string &outObj, std::string &outMtl, const std::vector<float> *pBank = nullptr );
    // The world-space collision / surface soup of a road (two triangles per
    // segment quad, oriented up).
    static void BuildRoadSolid( const std::vector<Vector3f> &center, float width, std::vector<Vector3f> &outVertices,
                                std::vector<uint32_t> &outIndices, const std::vector<float> *pBank = nullptr );
    // The tyre-surface index of a surface name (0 tarmac, 1 dirt, 2 grass, 0xff = none / unknown).
    static uint8_t SurfaceIndex( const std::string &surface );

private:
    std::vector<Road> roads;
    bool bLoaded = false;
};

// The default asphalt texture of the roads (256², generated once into
// `<mods>/props/<name>` when the file is not there — a tool texture, our own
// art): returns the texture name to put in the MTL.
std::string ModRoadsDefaultTexture( const std::string &surface = "" );
