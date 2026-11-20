#pragma once

#include "core/math.h"
#include "game/scene_editor/scene_doc.h"
#include "game/spots/obj_mesh.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_renderer.h"

#include <memory>
#include <string>
#include <vector>

class EdenWorld;
struct ObjMeshDraw;

// A pick ray (world space): origin + normalized direction.
struct SceneRay {
    Vector3f Origin;
    Vector3f Direction;
};

// Ray / axis-aligned box: nearest positive hit distance (false when the box
// is behind the ray origin or missed).
bool SceneRayBoxIntersect( const SceneRay &ray, const Vector3f &boundsMin, const Vector3f &boundsMax,
                           float &outDistance );

// General 4x4 inverse (D3D row-vector layout - the same numbers invert under
// either convention): used by the editor's pick / gizmo math.
Matrix4x4f SceneMatrixInverse( const Matrix4x4f &m );

// The scene document instantiated for drawing: one Instance per document
// object (library nodes, actors, OBJ props; groups resolve transforms only).
// The editor owns one and re-applies the document on every edit; a game mode
// that only wants to DRAW a scene (WORLD_VIEWER / FREERIDE later) uses the
// same class with a load-only flow.
//
// Spots are not instantiated here: a scene spot placement joins the mode's
// SpotExteriors streamer as an extra SpotRecord (one streaming path for the
// retail buildings and the scene's copies of them).
class SceneObjects {
public:
    struct Instance {
        std::string Id;
        std::string Kind;
        std::string Ref;
        bool bVisible = true;
        Matrix4x4f World;             // resolved (parent chains composed)
        Vector3f BoundsMin, BoundsMax; // world AABB (picking + draw culling)
        // library: the node index in the world's library model.
        int32_t LibraryNode = -1;
        // actor: the loaded model (owned by EdenWorld) + its entity hashcode.
        uint64_t ActorHash = 0;
        EdenModel *pActorModel = nullptr;
        // prop: the OBJ draws (GPU buffers owned here, unloaded with the
        // instance) + the draw's transform baked into Instance.World.
        std::vector<ObjMeshDraw> ObjDraws;
        std::string ShaderName;
    };

    void setWorld( EdenWorld *pWorld ) { pWorldRef = pWorld; }
    // Destroys the OBJ GPU resources and rebuilds from the document.
    void unload();
    // Re-instantiates the document (full rebuild: props reload, cheap at the
    // scale a scene holds). Returns the number of instances that drew.
    void applyDocument( const SceneDocument &document );
    void draw( EdenRenderer &renderer ) const;
    // Nearest instance hit by the ray (AABB level: exact for placement
    // clicking). False when nothing is hit.
    bool raycast( const SceneRay &ray, float &outDistance, std::string &outInstanceId ) const;

    inline const std::vector<Instance> &getInstances() const { return instances; }
    const Instance *findInstance( const std::string &id ) const;
    inline size_t getNumDrawnKinds() const { return numDrawn; }

private:
    EdenWorld *pWorldRef = nullptr;
    std::vector<Instance> instances;
    size_t numDrawn = 0;
};
