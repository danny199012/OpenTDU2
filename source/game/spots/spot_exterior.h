#pragma once

#include "filesystem/bank.h"
#include "game/cinematic/cinematic_audio.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_scene_view.h"

// Exterior of a spot (islands/<island>/exterior/<spots.bin bank name>.bnk,
// e.g. spot EPH_L3DI_5067I -> eph_l3di.bnk, ECAS -> ecas.bnk), drawn at the
// spot frame T(position) R_y(heading) like FirstOutro (FREERIDE house):
// Scenery models, Entity__Actor__Mesh / __Interior in the bank frame,
// Entity__Actor at entityTRS * rootLocal^-1 (spec_pool_interior 6). Several
// spots may share one bank (same building type): one instance per frame.
//
// One bank = one building (a dealership, a shop, a house...). SpotExteriors
// (spot_exteriors.h) owns the streamed set of them and is what the world
// viewer and FREERIDE both use to place the buildings on an island.
struct HavokCollision;
// The car wash bank's Scenery set has no placement of its own and sits (-1.5, 0, -16.0) m away from the bank's
// collision (ecw_ai_sc_*.shk), the props and the washing scene: the .shk lot walls span local x -20.7..21.6,
// z -38.2..17.6, the set's render bounds x -19.3..23.1, z -21.9..33.9, and the 34 structural boxes of the .shk
// land on the set's geometry at that offset (mean distance 0.7 m; the 180 degree turn and both mirrors fit worse,
// 16..17 of 24 boxes vs 22) [DATA, measured: -dumpwashverts + -logspotcollbounds]. The collision is what the road,
// the wash place (1.47, -17.5: between the .shk bay walls at z -13.4 and -22) and the cameras share, so the set is
// drawn with this matrix on top of the spot frame: the building no longer overhangs the road (6 m clear), the
// planters meet the bushes and the entry / exit pockets meet the lot. -nowashrot = the old placement.
Matrix4x4f WashSetLocal();
bool IsWashBankPath( const std::string &path );

class SpotExterior {
public:
    SpotExterior() = default;
    ~SpotExterior();

    bool load( EdenSceneView &view, const std::string &bankPath );
    void unload();
    // Lightmap day / night blend of the models (call again after the view
    // lightmap list was cleared).
    void registerLightmaps( EdenSceneView &view );
    void draw( EdenRenderer &renderer ) const;

    inline const std::string &getPath() const { return path; }
    inline std::vector<Matrix4x4f> &getFrames() { return frames; }
    inline const std::vector<Matrix4x4f> &getFrames() const { return frames; }
    // The bank (its <bank>_sc_*.shk: the building's own collision, bank frame).
    inline const Bank &getBank() const { return bank; }
    // The up-facing triangles of the sets whose vertices all lie between minY and maxY (bank frame):
    // a building's drawn floor, for the places where the bank's own collision has none (the car wash
    // forecourt: the floor is 0.4 m above the road the car rests on).
    void collectFloorTriangles( float minY, float maxY, HavokCollision &out ) const;
    // The matrix the Scenery sets are drawn with on top of a frame (identity except the car washes).
    inline const Matrix4x4f &getSetLocal() const { return setLocal; }
    inline size_t getNumModels() const { return sets.size() + props.size(); }

private:
    struct Prop {
        std::unique_ptr<EdenModel> Model;
        Matrix4x4f Local;
    };

    std::string path;
    Matrix4x4f setLocal = Matrix4x4f::Identity();
    Bank bank;
    BankHashcodes hashcodes;
    std::vector<std::unique_ptr<EdenModel>> sets;
    std::vector<Prop> props;
    std::vector<Matrix4x4f> frames; // spot frames using this bank
};
