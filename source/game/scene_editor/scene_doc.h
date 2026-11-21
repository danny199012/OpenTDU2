#pragma once

#include "core/math.h"
#include "game/scene_editor/scene_json.h"

#include <string>
#include <vector>

// The scene document (source/game/scene_editor/): the editable source of
// truth of the scene editor. One JSON file per scene in the mods overlay
// (`<mods>/scenes/<name>.scene.json`), human- and AI-editable; the editor
// saves it and re-instantiates it at load. Files are read through the VFS
// (the overlay shadows) and written under the mods root - the game data
// folder is never written (docs/re/modding.md 4).
//
// Shape:
//   {
//     "version": 1,
//     "island": "ibiza",
//     "name": "my_scene",
//     "objects": [
//       { "id": "canopy", "kind": "library", "ref": "SB_GAZCAN",
//         "pos": [x, y, z], "yaw": 90, "pitch": 0, "roll": 0,
//         "scale": [1, 1, 1], "visible": true, "parent": "" }
//     ],
//     "overrides": [
//       { "kind": "node", "target": "shutters", "hidden": true }
//     ],
//     "roads": [
//       { "id": "coast", "points": [[x, y, z], ...], "width": 7.0,
//         "lanes": 2, "closed": false, "collide": true, "texture": "asphalt.png" }
//     ]
//   }
//
// kinds: "group" (no draw, a parent for attaching), "library" (a node of the
// island's commonworld library), "actor" (worldactors.bnk entity by name),
// "spot" (a whole exterior bank: a retail building placed by a scene),
// "interior" (a walkable room bank: `icd_ferr`, a shop spot ref `ECD_FERR`,
// or a house ref `EPH_L2E_0345I` [+":garage"] — M4), "prop" (an OBJ from
// <mods>/props/). pos / yaw / pitch / roll are the object's own transform;
// "parent" composes it on top of the parent's (attach).
struct SceneObject {
    std::string Id;
    std::string Parent;   // "" = scene root, else the parent object's id
    std::string Kind;     // group | library | actor | spot | prop
    std::string Ref;      // node / actor / bank / OBJ name (see the kinds)
    Vector3f Position;
    float Yaw = 0.0f;     // degrees, the rotation about Y (the heading)
    float Pitch = 0.0f;   // degrees
    float Roll = 0.0f;    // degrees
    Vector3f Scale = Vector3f( 1.0f, 1.0f, 1.0f );
    bool bVisible = true;
};

// A piece of the streamed game that the scene edits (a baked sub-object, a
// .pmi library node, a material). "target" is a node / material name (what
// -lognodesnear / -logstationmats print) or "hc:<16 hex>" when the parser
// could not recover the name. Kind "color" replaces the material's
// Diffuse.rgb with Color (M6 material editing).
struct SceneOverride {
    std::string Kind;      // "node" | "material" | "color"
    std::string Target;    // name or hc:<hex>
    bool bHidden = true;
    Vector4f Color = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f ); // the "color" kind's RGB (A unused)
};

// A road drawn from a spline (M3; the document model ships with M1 so scenes
// are forward compatible).
struct SceneRoad {
    std::string Id;
    std::vector<Vector3f> Points;
    float Width = 7.0f;
    int32_t Lanes = 2;
    bool bClosed = false;
    bool bCollide = true;
    std::string Texture; // props-folder PNG; empty = the default texture of the surface
    // The tyre surface of the ribbon: "" (the old behaviour), "tarmac", "dirt" or "grass" - what the tyres feel
    // (the car DB's Pacejka Config per surface + the reference friction 1.0 / 0.6 / 0.4) and the default texture.
    std::string Surface;
    // Per control point (parallel to Points; missing entries = 0): the bank angle in degrees. Positive lifts the
    // driver's right edge (a ribbon heading +Z has its +X edge up) = banked for a LEFT turn; the section rolls about its lower
    // edge (the road never dips under the terrain). Give the ends bank 0 (a ramp-in) or the start is a lip. The height profile
    // (a grade) is just the points' y.
    std::vector<float> Banks;
};

// A petrol station (the station tool, docs/re/scene_editor.md 11): a forecourt "road ground" slab, a drive
// connecting it to the nearest map road, a canopy + pumps prop, and the stations.txt entry that makes the
// place work (the pump screen, the repair row, the map icon, the forecourt lights). The roads and the canopy
// object are GENERATED from this record (ids <Id>_ground, <Id>_link, <Id>_canopy) and rebuilt by add_station /
// set_station, so the record stays the source of truth.
struct SceneStation {
    std::string Id;
    std::string Name;        // the stations.txt name (one word)
    Vector3f Position;       // the forecourt centre (world; y = the ground)
    float Yaw = 0.0f;        // degrees: the slab's long axis is (sin yaw, cos yaw)
    float Length = 34.0f;    // m along the axis
    float Width = 24.0f;     // m across
    std::string Surface;     // the forecourt + drive surface ("" / tarmac / dirt / grass)
    bool bConnect = true;    // build the drive to the nearest map road
    float MaxLink = 400.0f;  // m: the search radius for that road
};

struct SceneDocument {
    int32_t Version = 1;
    std::string Island = "ibiza";
    std::string Name;
    std::vector<SceneObject> Objects;
    std::vector<SceneOverride> Overrides;
    std::vector<SceneRoad> Roads;
    std::vector<SceneStation> Stations;

    // VFS read (`scenes/<file>` resolves through the mods overlay).
    bool loadFromVfs( const std::string &fileName, std::string &outError );
    // Direct write under the mods root: `<modsRoot>/scenes/<fileName>`.
    bool saveToMods( const std::string &fileName, std::string &outError ) const;

    JsonValue toJson() const;
    bool fromJson( const JsonValue &root, std::string &outError );

    SceneObject *findObject( const std::string &id );
    const SceneObject *findObject( const std::string &id ) const;
    SceneRoad *findRoad( const std::string &id );
    SceneStation *findStation( const std::string &id );
    // id unique in the scene: base, base_2, base_3...
    std::string uniqueId( const std::string &base ) const;
    void removeObject( const std::string &id ); // and the children's Parent -> ""
    // The world matrix of an object (its own transform; the parent chain is
    // resolved by the caller - resolveWorld below).
    Matrix4x4f objectMatrix( const SceneObject &object ) const;
    // Composes the full world matrices of every object (parent chains
    // resolved, children after parents; cycle-safe: a cycle composes from the
    // scene root). outWorlds is parallel to Objects.
    void resolveWorlds( std::vector<Matrix4x4f> &outWorlds ) const;
};

// The scene files of the mods overlay ("scenes/" folder), sorted by name.
std::vector<std::string> ListSceneFiles();
