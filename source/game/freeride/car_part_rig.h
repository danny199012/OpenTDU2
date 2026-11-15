#pragma once

#include "core/math.h"
#include "render/eden/eden_model.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The detachable-looking parts of a car model - the separate BUMP_F_* / BUMP_R_* nodes and the
// DOOR_L / DOOR_R subtrees - driven by damage by zone (front, rear, left, right; 0..65535 each,
// docs/re/damage_spec.md 3c). A model shared by many agents (the traffic) gets the damaged pose
// written before one agent's draw and restored after it.
class CarPartRig {
public:
    // Finds the parts of the model (once, at load).
    void gather( const EdenModel &model );
    inline bool hasParts() const {
        return bGathered && ( !bumpers[0].Nodes.empty() || !bumpers[1].Nodes.empty() || !hoods[0].Nodes.empty() ||
                              !hoods[1].Nodes.empty() || hasDoor( 0 ) || hasDoor( 1 ) );
    }

    // What gather found: bumper nodes front / rear, door nodes left / right (log text).
    std::string summary() const;
    // Writes the damaged pose of the parts into the model.
    // bDoors false leaves the doors to the caller (the player car also moves its interior panels).
    void apply( EdenModel &model, float front, float rear, float left, float right, bool bDoors = true );
    // Puts the authored transforms back.
    void restore( EdenModel &model );

private:
    struct Bumper {
        std::vector<std::pair<int32_t, Matrix4x4f>> Nodes;
        Vector3f Pivot;
    };
    struct Door {
        int32_t Node = -1;
        Matrix4x4f Rest;
        std::vector<std::pair<int32_t, Matrix4x4f>> Subtree;
    };
    inline bool hasDoor( int32_t side ) const { return doors[side].Node >= 0; }

    bool bGathered = false;
    Bumper bumpers[2]; // [0] front, [1] rear
    Bumper hoods[2];   // [0] the bonnet (HOOD_F), [1] the boot lid (HOOD_R): popped up about their cabin-side edge
    Door doors[2];     // [0] left, [1] right
    bool bApplied = false;
};
