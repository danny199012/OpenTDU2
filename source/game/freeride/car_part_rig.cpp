#include "shared.h"
#include "car_part_rig.h"

#include "core/logger.h"
#include "render/3dd.h"

#include <cmath>
#include <string>

void CarPartRig::gather( const EdenModel &model ) {
    bGathered = true;
    const Render3DD &scene = model.getScene();
    const std::vector<SceneNode> &nodes = scene.getNodes();
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        const int32_t node = static_cast<int32_t>( i );
        const std::string &name = nodes[i].Name;
        // The first glyph of some names is lost in the 3dd (?UMP_F_H): match the tail.
        const int32_t end = name.find( "UMP_F" ) != std::string::npos   ? 0
                            : name.find( "UMP_R" ) != std::string::npos ? 1
                                                                         : -1;
        if ( end >= 0 && nodes[i].ObjectHashcode != 0 ) {
            bumpers[end].Nodes.emplace_back( node, model.getNodeTransform( node ) );
        }
        // The bonnet (HOOD_F) and the boot lid (HOOD_R) of the traffic models.
        const int32_t panel = name.compare( 0, 6, "HOOD_F" ) == 0 ? 0 : name.compare( 0, 6, "HOOD_R" ) == 0 ? 1 : -1;
        if ( panel >= 0 && nodes[i].ObjectHashcode != 0 ) {
            hoods[panel].Nodes.emplace_back( node, model.getNodeTransform( node ) );
        }
        const int32_t side = name.find( "DOOR_L" ) != std::string::npos   ? 0
                             : name.find( "DOOR_R" ) != std::string::npos ? 1
                                                                           : -1;
        // The door node itself (the first match of the side that is a mesh or has a mesh child).
        if ( side >= 0 && doors[side].Node < 0 && name.compare( 0, 5, "DOOR_" ) == 0 ) {
            doors[side].Node = node;
        }
    }
    // The hinge line of every end part: the top edge. A bumper hinges on its car side (the inner end);
    // the bonnet on its cabin side (rear edge), the boot lid on its cabin side (front edge).
    auto findPivot = [&]( Bumper &part, bool bLowZ ) {
        if ( part.Nodes.empty() ) {
            return;
        }
        Vector3f mn, mx;
        if ( !model.getNodeBounds( part.Nodes.front().first, mn, mx ) ) {
            part.Nodes.clear();
            return;
        }
        Vector3f lo( 1e30f, 1e30f, 1e30f ), hi( -1e30f, -1e30f, -1e30f );
        for ( int32_t corner = 0; corner < 8; corner++ ) {
            const Vector3f point = part.Nodes.front().second.transformPoint(
                Vector3f( ( corner & 1 ) ? mx.x : mn.x, ( corner & 2 ) ? mx.y : mn.y, ( corner & 4 ) ? mx.z : mn.z ) );
            lo = Vector3f( Min( lo.x, point.x ), Min( lo.y, point.y ), Min( lo.z, point.z ) );
            hi = Vector3f( Max( hi.x, point.x ), Max( hi.y, point.y ), Max( hi.z, point.z ) );
        }
        part.Pivot = Vector3f( 0.5f * ( lo.x + hi.x ), hi.y, bLowZ ? lo.z : hi.z );
    };
    findPivot( bumpers[0], true );  // the front bumper's inner end is its low z
    findPivot( bumpers[1], false ); // the rear bumper's inner end is its high z
    findPivot( hoods[0], true );    // the bonnet's cabin side is its low z
    findPivot( hoods[1], false );   // the boot lid's cabin side is its high z
    for ( int32_t side = 0; side < 2; side++ ) {
        Door &door = doors[side];
        if ( door.Node < 0 ) {
            continue;
        }
        door.Rest = model.getNodeTransform( door.Node );
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            const int32_t node = static_cast<int32_t>( i );
            if ( node == door.Node || scene.isDescendantOf( node, door.Node ) ) {
                door.Subtree.emplace_back( node, model.getNodeTransform( node ) );
            }
        }
    }
}

void CarPartRig::apply( EdenModel &model, float front, float rear, float left, float right, bool bDoors ) {
    if ( !bGathered ) {
        return;
    }
    // The doors: loose from ~6000 of side damage, up to ~0.2 rad (left swings out on a negative angle).
    const float sides[2] = { left, right };
    for ( int32_t side = 0; side < 2; side++ ) {
        const Door &door = doors[side];
        if ( !bDoors ) {
            continue;
        }
        const float ajar = 0.2f * Clamp( ( sides[side] - 6000.0f ) / 40000.0f, 0.0f, 1.0f );
        if ( door.Node < 0 || ajar <= 0.0f ) {
            continue;
        }
        const Matrix4x4f rotation = Matrix4x4f::FromQuaternion(
            Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), side == 0 ? -ajar : ajar ) );
        const Matrix4x4f delta = door.Rest * rotation * door.Rest.inverse();
        for ( const auto &rest : door.Subtree ) {
            model.setNodeTransform( rest.first, delta * rest.second );
        }
        bApplied = true;
    }
    // The bumpers: sag from ~12000 on their end, dangling at ~48000 (pitched about the top
    // edge on the car side, dropped and pushed out).
    const float ends[2] = { front, rear };
    for ( int32_t end = 0; end < 2; end++ ) {
        const float hang = Clamp( ( ends[end] - 12000.0f ) / 36000.0f, 0.0f, 1.0f );
        if ( bumpers[end].Nodes.empty() || hang <= 0.0f ) {
            continue;
        }
        const float pitch = ( end == 0 ? 1.0f : -1.0f ) * 0.9f * hang;
        const Vector3f pivot = bumpers[end].Pivot;
        const Matrix4x4f about = Matrix4x4f::Translation( pivot ) *
                                 Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), pitch ) ) *
                                 Matrix4x4f::Translation( -pivot );
        const Matrix4x4f drop = Matrix4x4f::Translation( Vector3f( 0.0f, -0.3f * hang, ( end == 0 ? 0.25f : -0.25f ) * hang ) );
        for ( const auto &node : bumpers[end].Nodes ) {
            model.setNodeTransform( node.first, drop * about * node.second );
        }
        bApplied = true;
    }
    // The bonnet and the boot lid pop up about their cabin-side edge from ~20000 on their end,
    // up to ~0.55 rad (latched ajar, not flung open) [INF].
    for ( int32_t end = 0; end < 2; end++ ) {
        const float pop = 0.55f * Clamp( ( ends[end] - 20000.0f ) / 20000.0f, 0.0f, 1.0f );
        if ( hoods[end].Nodes.empty() || pop <= 0.0f ) {
            continue;
        }
        // The bonnet's free edge is at +z: raising it is a negative pitch; the boot's is at -z: positive.
        const float pitch = end == 0 ? -pop : pop;
        const Vector3f pivot = hoods[end].Pivot;
        const Matrix4x4f about = Matrix4x4f::Translation( pivot ) *
                                 Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), pitch ) ) *
                                 Matrix4x4f::Translation( -pivot );
        for ( const auto &node : hoods[end].Nodes ) {
            model.setNodeTransform( node.first, about * node.second );
        }
        bApplied = true;
    }
}

void CarPartRig::restore( EdenModel &model ) {
    if ( !bApplied ) {
        return;
    }
    bApplied = false;
    for ( const Bumper &bumper : bumpers ) {
        for ( const auto &node : bumper.Nodes ) {
            model.setNodeTransform( node.first, node.second );
        }
    }
    for ( const Bumper &hood : hoods ) {
        for ( const auto &node : hood.Nodes ) {
            model.setNodeTransform( node.first, node.second );
        }
    }
    for ( const Door &door : doors ) {
        for ( const auto &rest : door.Subtree ) {
            model.setNodeTransform( rest.first, rest.second );
        }
    }
}

std::string CarPartRig::summary() const {
    return "bumpers F " + std::to_string( bumpers[0].Nodes.size() ) + " R " + std::to_string( bumpers[1].Nodes.size() ) + ", bonnet " +
           std::to_string( hoods[0].Nodes.size() ) + " boot " + std::to_string( hoods[1].Nodes.size() ) + ", doors L " +
           std::to_string( doors[0].Subtree.size() ) + " R " + std::to_string( doors[1].Subtree.size() ) + " node(s)";
}
