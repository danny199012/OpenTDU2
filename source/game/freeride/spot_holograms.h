#pragma once

#include "core/math.h"
#include "filesystem/bank.h"
#include "render/eden/eden_model.h"

#include <memory>
#include <string>
#include <vector>

class EdenRenderer;
class EdenSceneView;

// The spot holograms: the retail 3D markers floating over the shops, houses
// and events (scenarics/packs/pack_spots.bnk [DATA]: "spots_holograms_*"
// scenes, the ico_* icon textures and the Arrow_* / Circle_* / Spot_Pulse_*
// .UVA scrolls). Its entities.xmb holds ArrayOf_Entity__Actor__SpotMesh and
// ArrayOf_Entity__Actor__SpotMeshAddOn; each record's Type is a condition on
// the spot's structure (StructHouse / StructClub / StructCarDealer /
// StructTuning / StructSchool / StructDriveIn / StructChallenge / StructGoodies
// / StructOther, with a SubType) - the hologram of each kind of spot.
class SpotHolograms {
public:
    struct Model {
        std::string Name;       // the entity's __Name
        std::string Scene;      // the 3DD's bank name
        bool bAddOn = false;    // SpotMeshAddOn (the pulse / circle / arrow parts)
        uint32_t TypeRaw[4] = {}; // the Type condition's raw words (decode in progress)
        std::unique_ptr<EdenModel> Mesh;
        Matrix4x4f Local;       // the scene root's inverse
        Vector3f BoundsMin, BoundsMax;
    };
    struct Placement {
        int32_t ModelIndex = -1;
        Vector3f Position;
        float Heading = 0.0f;
        float Scale = 1.0f;
        bool bMirror = false; // flip across the plate's vertical axis (the backplate's pointer side)
    };

    ~SpotHolograms() { unload(); }

    bool load( EdenSceneView &view );
    void unload();
    inline bool isLoaded() const { return bLoaded; }
    inline const std::vector<Model> &getModels() const { return models; }
    int32_t findModel( const char *pName ) const;

    // The holograms to draw this frame.
    void setPlacements( const std::vector<Placement> &newPlacements ) { placements = newPlacements; }
    void draw( EdenRenderer &renderer ) const;

private:
    bool bLoaded = false;
    Bank bank;
    std::vector<Model> models;
    std::vector<Placement> placements;
};
