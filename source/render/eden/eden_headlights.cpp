#include "shared.h"

#include "render/eden/eden_headlights.h"

#include "render/eden/eden_model.h"

std::vector<EdenHeadlightCone> &EdenHeadlightFrameCones() {
    static std::vector<EdenHeadlightCone> cones;
    return cones;
}

std::vector<EdenLampGlow> &EdenLampGlowFrame() {
    static std::vector<EdenLampGlow> glows;
    return glows;
}

std::vector<EdenHeadlightCone> &EdenRearLightFrameCones() {
    static std::vector<EdenHeadlightCone> cones;
    return cones;
}

void EdenCollectLampAnchors( EdenModel &model, std::vector<EdenLampAnchor> &out ) {
    out.clear();
    if ( !model.isLoaded() ) {
        return;
    }
    // The quad size per function [INF: by eye against the retail captures —
    // the headlamps the widest, the rear position lights the smallest].
    static const float kGlowSize[5] = { 0.17f, 0.10f, 0.13f, 0.12f, 0.11f };
    auto functionOf = []( const std::string &name ) -> int32_t {
        if ( name.compare( 0, 3, "FA_" ) == 0 && name.size() >= 6 ) {
            switch ( name[3] ) {
            case 'L': return 0;
            case 'B': return 1;
            case 'S': return 2;
            case 'R': return 3;
            case 'W': return 4;
            default: return -1;
            }
        }
        return -1;
    };
    std::vector<int32_t> known;
    for ( const EdenModel::Item &item : model.getItems() ) {
        if ( item.NodeIndex < 0 ) {
            continue;
        }
        const SceneNode &node = model.getScene().getNodes()[static_cast<size_t>( item.NodeIndex )];
        int32_t function = functionOf( node.Name );
        if ( function < 0 ) {
            // A glow quad on a differently named node (FX_LIGHT materials on a
            // few props): treated as a front light.
            const EdenMaterial &material = model.getMaterials()[static_cast<size_t>( item.MaterialIndex )];
            if ( material.Name.compare( 0, 8, "FX_LIGHT" ) == 0 ) {
                function = 0;
            }
        }
        if ( function < 0 ) {
            continue;
        }
        bool bKnown = false;
        for ( const int32_t n : known ) {
            bKnown = bKnown || n == item.NodeIndex;
        }
        if ( bKnown ) {
            continue;
        }
        known.push_back( item.NodeIndex );
        EdenLampAnchor anchor;
        anchor.Local = Vector3f( node.World.m[3][0], node.World.m[3][1], node.World.m[3][2] );
        anchor.Size = kGlowSize[function];
        anchor.Function = function;
        if ( node.Name.size() >= 7 ) {
            anchor.bLeft = node.Name[6] == 'L';
            anchor.Normal = Vector3f( 0.0f, 0.0f, node.Name[5] == 'F' ? 1.0f : -1.0f );
        } else {
            anchor.Normal = Vector3f( 0.0f, 0.0f, function == 0 ? 1.0f : -1.0f );
        }
        out.push_back( anchor );
    }
}

void EdenAppendHeadBeams( std::vector<EdenHeadlightCone> &cones, const Vector3f &leftLamp, const Vector3f &rightLamp,
                          const Vector3f &forward, float intensity ) {
    const Vector3f side( forward.z, 0.0f, -forward.x ); // the driver's left
    const Vector3f middle = ( leftLamp + rightLamp ) * 0.5f;
    // The two dipped beams: a pool each, beginning at the lens, splayed a hair
    // outward so they do not stack into one blob.
    const Vector3f lamps[2] = { leftLamp, rightLamp };
    for ( int32_t i = 0; i < 2; i++ ) {
        EdenHeadlightCone cone;
        cone.Position = lamps[i];
        const float splay = i == 0 ? 0.03f : -0.03f;
        const Vector3f direction = forward + side * splay;
        cone.Direction = direction.normalized();
        cone.Intensity = intensity * 0.62f;
        cone.PoolAhead = 6.5f; // 0.5 .. 12.5 m ahead of the lamp
        cone.PoolAlong = 6.0f;
        cone.PoolAcross = 2.9f;
        cones.push_back( cone );
    }
    // The long throw: a faint narrow pool reaching 5 .. 31 m.
    EdenHeadlightCone spill;
    spill.Position = middle;
    spill.Direction = forward;
    spill.Intensity = intensity * 0.42f;
    spill.PoolAhead = 18.0f;
    spill.PoolAlong = 13.0f;
    spill.PoolAcross = 3.4f;
    cones.push_back( spill );
}
