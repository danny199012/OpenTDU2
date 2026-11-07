#pragma once

#include "core/math.h"
#include "render/eden/eden_renderer.h"

// Mod props: meshes of our own, authored outside the game (Blender -> OBJ + MTL
// + PNG) and placed from the **mods overlay** (docs/re/modding.md). This is the
// port-native asset route: nothing of the game's own model format is involved,
// and the game data is never written to.
//
// Layout, all under `<mods>/` (default `<user data>/mods`, `-mods <dir>`):
//
//   props.txt              the placement list, one prop per line:
//                              # island name x y z heading [scale] [shader]
//                              ibiza  EGAS_A  12665  10.5  16698  0
//   props/<name>.obj       the mesh (its .mtl and its textures beside it)
//   props/<name>.mtl
//   props/*.png            any format render/image_file.* reads (PNG/JPG/DDS)
//
// The mesh is drawn with a **stock Eden shader family** (`shader` column,
// default TD_Gouraud: Gouraud N.L lighting plus an optional colour texture), so
// a prop is lit like the rest of the world. A material of the .mtl becomes one
// draw: its `map_Kd` is bound to texture slot 0.
class ModProps {
public:
    struct Prop {
        std::string Name;
        Vector3f Position;
        float Heading = 0.0f;
        float Scale = 1.0f;
        // The `fuel` column of props.txt: this prop is a gas station (the player
        // can fill the tank next to it, source/game/freeride/fuel_menu.*).
        bool bFuelStation = false;
        Vector3f BoundsMin;
        Vector3f BoundsMax;
        std::vector<EdenMesh> Meshes;        // one per .mtl material
        std::vector<EdenMaterial> Materials; // parallel to Meshes
        std::vector<GPUBuffer *> VertexBuffers;
        std::vector<GPUBuffer *> IndexBuffers;
        std::vector<GPUTexture *> Textures;  // parallel to Materials (may be null)
    };

    // island: only the props of this island are loaded.
    bool load( EdenRenderer &renderer, const std::string &island );
    void unload();
    // Draws the props; call from the scene's draw callback exactly where the
    // spot exteriors are drawn (every pass but the projector actors).
    void draw( EdenRenderer &renderer ) const;

    inline bool isLoaded() const { return bLoaded; }
    inline size_t getCount() const { return props.size(); }
    inline const std::vector<Prop> &getProps() const { return props; }
    // The nearest fuel station prop within maxDistance of position (nullptr when
    // there is none): the caller offers the pump screen.
    const Prop *findFuelStation( const Vector3f &position, float maxDistance ) const;

    // --- the map editor (gm_world_viewer's placement panel) ---
    // The prop meshes in the overlay: <mods>/props/*.obj basenames (no
    // extension), sorted.
    static std::vector<std::string> listAvailableProps();
    struct PlacementLine {
        size_t FileLine = 0; // 0-based raw line index in props.txt
        std::string Island;
        std::string Name;
        Vector3f Position;
        float Heading = 0.0f;
        float Scale = 1.0f;
        bool bFuelStation = false;
        std::string ShaderName;
    };
    // Every placement line of props.txt (comments and blanks skipped).
    static std::vector<PlacementLine> listPlacements();
    // Appends one placement line to props.txt (the editor's commit).
    static bool appendPlacement( const std::string &island, const std::string &name, const Vector3f &position,
                                 float heading, float scale, bool bFuelStation, const std::string &shaderName );
    // Rewrites the raw props.txt line at fileLine (listPlacements' FileLine).
    static bool replacePlacementLine( size_t fileLine, const std::string &line );

private:
    std::vector<Prop> props;
    bool bLoaded = false;
};
