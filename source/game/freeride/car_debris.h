#pragma once

#include "core/math.h"
#include "game/freeride/car_part_rig.h"
#include "render/eden/eden_model.h"

#include <cstdint>
#include <functional>
#include <vector>

// Parts that came off a car (a bumper, a bonnet, a boot lid, a door - CarPartRig::lostMask): the
// part keeps the car's speed, tumbles through the air, bounces on the ground and stays where it
// stops. The part is drawn from its own car model (the nodes of the part at the pose they had when
// they came off, every other node hidden for that draw), so the paint, the dirt and the lightmaps
// are the car's own. docs/re/damage_spec.md 3e.
class CarDebris {
public:
    // The height of the ground under a point: casts down from 'from'; false when nothing is hit.
    using GroundFn = std::function<bool( const Vector3f &from, float &outHeight )>;

    inline void setGround( GroundFn fn ) { ground = std::move( fn ); }
    // A part leaves 'model' (the car drawn at vehicleWorld, moving at vehicleVelocity).
    void spawn( EdenModel &model, const CarPartRig::Detached &part, const Matrix4x4f &vehicleWorld,
                const Vector3f &vehicleVelocity );
    void update( float deltaTime, const Vector3f &viewer );
    void draw( EdenRenderer &renderer );
    // Forget every piece (the owning model is going away).
    inline void clear() { pieces.clear(); }
    inline size_t getCount() const { return pieces.size(); }

private:
    struct Piece {
        EdenModel *Model = nullptr;
        std::vector<std::pair<int32_t, Matrix4x4f>> Nodes; // the part's nodes at the pose it left with
        std::vector<int32_t> HideOthers;                    // every other mesh node (ForceHiddenNodes)
        Vector3f Center;                                    // model-space centre of the part (the body origin)
        float Radius = 0.3f;
        Vector3f Position;                                  // world, of the centre
        Matrix4x4f Rotation;                                // world orientation (no translation)
        Vector3f Velocity;
        Vector3f Spin;                                      // rad/s, world axes
        bool bSettled = false;
        float Age = 0.0f;
    };

    GroundFn ground;
    std::vector<Piece> pieces;
};
