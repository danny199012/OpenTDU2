#include "shared.h"
#include "scene_objects.h"

#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "world/eden_world.h"

#include <cmath>
#include <cstring>

// --- pick / matrix helpers -------------------------------------------------

bool SceneRayBoxIntersect( const SceneRay &ray, const Vector3f &boundsMin, const Vector3f &boundsMax,
                           float &outDistance ) {
    // Slab test; the ray direction components may be 0 (parallel to a slab).
    float tMin = 0.0f;
    float tMax = 1e30f;
    const Vector3f origins[ 3 ] = { ray.Origin, ray.Origin, ray.Origin };
    const Vector3f directions[ 3 ] = { ray.Direction, ray.Direction, ray.Direction };
    const float boxMin[ 3 ] = { boundsMin.x, boundsMin.y, boundsMin.z };
    const float boxMax[ 3 ] = { boundsMax.x, boundsMax.y, boundsMax.z };
    for ( int32_t axis = 0; axis < 3; axis++ ) {
        const float d = axis == 0 ? directions[ axis ].x : ( axis == 1 ? directions[ axis ].y : directions[ axis ].z );
        const float o = axis == 0 ? origins[ axis ].x : ( axis == 1 ? origins[ axis ].y : origins[ axis ].z );
        if ( std::fabs( d ) < 1e-8f ) {
            if ( o < boxMin[ axis ] || o > boxMax[ axis ] ) {
                return false;
            }
            continue;
        }
        float t0 = ( boxMin[ axis ] - o ) / d;
        float t1 = ( boxMax[ axis ] - o ) / d;
        if ( t0 > t1 ) {
            const float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }
        tMin = tMin > t0 ? tMin : t0;
        tMax = tMax < t1 ? tMax : t1;
        if ( tMin > tMax ) {
            return false;
        }
    }
    if ( tMax < 0.0f ) {
        return false;
    }
    outDistance = tMin;
    return true;
}

Matrix4x4f SceneMatrixInverse( const Matrix4x4f &m ) {
    // General 4x4 inverse, cofactor expansion over the 4 column vectors (the
    // layout m[column][row] of the engine's D3D row-vector matrices - the
    // algebra is convention free).
    const float *a = &m.m[ 0 ][ 0 ];
    float inv[ 16 ];
    inv[ 0 ] = a[ 5 ] * a[ 10 ] * a[ 15 ] - a[ 5 ] * a[ 11 ] * a[ 14 ] - a[ 9 ] * a[ 6 ] * a[ 15 ] +
               a[ 9 ] * a[ 7 ] * a[ 14 ] + a[ 13 ] * a[ 6 ] * a[ 11 ] - a[ 13 ] * a[ 7 ] * a[ 10 ];
    inv[ 4 ] = -a[ 4 ] * a[ 10 ] * a[ 15 ] + a[ 4 ] * a[ 11 ] * a[ 14 ] + a[ 8 ] * a[ 6 ] * a[ 15 ] -
               a[ 8 ] * a[ 7 ] * a[ 14 ] - a[ 12 ] * a[ 6 ] * a[ 11 ] + a[ 12 ] * a[ 7 ] * a[ 10 ];
    inv[ 8 ] = a[ 4 ] * a[ 9 ] * a[ 15 ] - a[ 4 ] * a[ 11 ] * a[ 13 ] - a[ 8 ] * a[ 5 ] * a[ 15 ] +
               a[ 8 ] * a[ 7 ] * a[ 13 ] + a[ 12 ] * a[ 5 ] * a[ 11 ] - a[ 12 ] * a[ 7 ] * a[ 9 ];
    inv[ 12 ] = -a[ 4 ] * a[ 9 ] * a[ 14 ] + a[ 4 ] * a[ 10 ] * a[ 13 ] + a[ 8 ] * a[ 5 ] * a[ 14 ] -
                a[ 8 ] * a[ 6 ] * a[ 13 ] - a[ 12 ] * a[ 5 ] * a[ 10 ] + a[ 12 ] * a[ 6 ] * a[ 9 ];
    inv[ 1 ] = -a[ 1 ] * a[ 10 ] * a[ 15 ] + a[ 1 ] * a[ 11 ] * a[ 14 ] + a[ 9 ] * a[ 2 ] * a[ 15 ] -
               a[ 9 ] * a[ 3 ] * a[ 14 ] - a[ 13 ] * a[ 2 ] * a[ 11 ] + a[ 13 ] * a[ 3 ] * a[ 10 ];
    inv[ 5 ] = a[ 0 ] * a[ 10 ] * a[ 15 ] - a[ 0 ] * a[ 11 ] * a[ 14 ] - a[ 8 ] * a[ 2 ] * a[ 15 ] +
               a[ 8 ] * a[ 3 ] * a[ 14 ] + a[ 12 ] * a[ 2 ] * a[ 11 ] - a[ 12 ] * a[ 3 ] * a[ 10 ];
    inv[ 9 ] = -a[ 0 ] * a[ 9 ] * a[ 15 ] + a[ 0 ] * a[ 11 ] * a[ 13 ] + a[ 8 ] * a[ 1 ] * a[ 15 ] -
               a[ 8 ] * a[ 3 ] * a[ 13 ] - a[ 12 ] * a[ 1 ] * a[ 11 ] + a[ 12 ] * a[ 3 ] * a[ 9 ];
    inv[ 13 ] = a[ 0 ] * a[ 9 ] * a[ 14 ] - a[ 0 ] * a[ 10 ] * a[ 13 ] - a[ 8 ] * a[ 1 ] * a[ 14 ] +
                a[ 8 ] * a[ 2 ] * a[ 13 ] + a[ 12 ] * a[ 1 ] * a[ 10 ] - a[ 12 ] * a[ 2 ] * a[ 9 ];
    inv[ 2 ] = a[ 1 ] * a[ 6 ] * a[ 15 ] - a[ 1 ] * a[ 7 ] * a[ 14 ] - a[ 5 ] * a[ 2 ] * a[ 15 ] +
               a[ 5 ] * a[ 3 ] * a[ 14 ] + a[ 13 ] * a[ 2 ] * a[ 7 ] - a[ 13 ] * a[ 3 ] * a[ 6 ];
    inv[ 6 ] = -a[ 0 ] * a[ 6 ] * a[ 15 ] + a[ 0 ] * a[ 7 ] * a[ 14 ] + a[ 4 ] * a[ 2 ] * a[ 15 ] -
               a[ 4 ] * a[ 3 ] * a[ 14 ] - a[ 12 ] * a[ 2 ] * a[ 7 ] + a[ 12 ] * a[ 3 ] * a[ 6 ];
    inv[ 10 ] = a[ 0 ] * a[ 5 ] * a[ 15 ] - a[ 0 ] * a[ 7 ] * a[ 13 ] - a[ 4 ] * a[ 1 ] * a[ 15 ] +
                a[ 4 ] * a[ 3 ] * a[ 13 ] + a[ 12 ] * a[ 1 ] * a[ 7 ] - a[ 12 ] * a[ 3 ] * a[ 5 ];
    inv[ 14 ] = -a[ 0 ] * a[ 5 ] * a[ 14 ] + a[ 0 ] * a[ 6 ] * a[ 13 ] + a[ 4 ] * a[ 1 ] * a[ 14 ] -
                a[ 4 ] * a[ 2 ] * a[ 13 ] - a[ 12 ] * a[ 1 ] * a[ 6 ] + a[ 12 ] * a[ 2 ] * a[ 5 ];
    inv[ 3 ] = -a[ 1 ] * a[ 6 ] * a[ 11 ] + a[ 1 ] * a[ 7 ] * a[ 10 ] + a[ 5 ] * a[ 2 ] * a[ 11 ] -
               a[ 5 ] * a[ 3 ] * a[ 10 ] - a[ 9 ] * a[ 2 ] * a[ 7 ] + a[ 9 ] * a[ 3 ] * a[ 6 ];
    inv[ 7 ] = a[ 0 ] * a[ 6 ] * a[ 11 ] - a[ 0 ] * a[ 7 ] * a[ 10 ] - a[ 4 ] * a[ 2 ] * a[ 11 ] +
               a[ 4 ] * a[ 3 ] * a[ 10 ] + a[ 8 ] * a[ 2 ] * a[ 7 ] - a[ 8 ] * a[ 3 ] * a[ 6 ];
    inv[ 11 ] = -a[ 0 ] * a[ 5 ] * a[ 11 ] + a[ 0 ] * a[ 7 ] * a[ 9 ] + a[ 4 ] * a[ 1 ] * a[ 11 ] -
                a[ 4 ] * a[ 3 ] * a[ 9 ] - a[ 8 ] * a[ 1 ] * a[ 7 ] + a[ 8 ] * a[ 3 ] * a[ 5 ];
    inv[ 15 ] = a[ 0 ] * a[ 5 ] * a[ 10 ] - a[ 0 ] * a[ 6 ] * a[ 9 ] - a[ 4 ] * a[ 1 ] * a[ 10 ] +
                a[ 4 ] * a[ 2 ] * a[ 9 ] + a[ 8 ] * a[ 1 ] * a[ 6 ] - a[ 8 ] * a[ 2 ] * a[ 5 ];
    const float det = a[ 0 ] * inv[ 0 ] + a[ 1 ] * inv[ 4 ] + a[ 2 ] * inv[ 8 ] + a[ 3 ] * inv[ 12 ];
    if ( std::fabs( det ) < 1e-20f ) {
        return Matrix4x4f::Identity();
    }
    const float invDet = 1.0f / det;
    Matrix4x4f out;
    for ( int32_t i = 0; i < 16; i++ ) {
        out.m[ 0 ][ i ] = inv[ i ] * invDet;
    }
    return out;
}

// --- the instances ----------------------------------------------------------

namespace {

// The Eden rotation of the engine's mirrored render space (see
// EdenRotationFromQuaternion in eden_scene_view.cpp): the conjugated
// quaternion.
Matrix4x4f EdenRotationMatrix( const Quaternionf &q ) {
    return Matrix4x4f::FromQuaternion( Quaternionf( -q.x, -q.y, -q.z, q.w ) );
}

Matrix4x4f HeadingMatrix( float yawDegrees, const Vector3f &position ) {
    const float rad = yawDegrees * 3.14159265f / 180.0f;
    const Quaternionf q( 0.0f, std::sin( rad * 0.5f ), 0.0f, std::cos( rad * 0.5f ) );
    return Matrix4x4f::Translation( position ) * EdenRotationMatrix( q );
}

void TransformBox( const Matrix4x4f &world, const Vector3f &boundsMin, const Vector3f &boundsMax,
                   Vector3f &outMin, Vector3f &outMax ) {
    outMin = Vector3f( 1e30f, 1e30f, 1e30f );
    outMax = Vector3f( -1e30f, -1e30f, -1e30f );
    for ( int32_t i = 0; i < 8; i++ ) {
        const Vector3f corner( ( i & 1 ) ? boundsMax.x : boundsMin.x, ( i & 2 ) ? boundsMax.y : boundsMin.y,
                               ( i & 4 ) ? boundsMax.z : boundsMin.z );
        const Vector3f transformed = world.transformPoint( corner );
        outMin = Vector3f( Min( outMin.x, transformed.x ), Min( outMin.y, transformed.y ),
                           Min( outMin.z, transformed.z ) );
        outMax = Vector3f( Max( outMax.x, transformed.x ), Max( outMax.y, transformed.y ),
                           Max( outMax.z, transformed.z ) );
    }
}

} // namespace

void SceneObjects::unload() {
    if ( gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        for ( Instance &instance : instances ) {
            if ( !instance.ObjDraws.empty() ) {
                UnloadObjDraws( instance.ObjDraws );
            }
        }
        (void)device;
    }
    instances.clear();
    numDrawn = 0;
}

void SceneObjects::applyDocument( const SceneDocument &document ) {
    unload();
    if ( pWorldRef == nullptr ) {
        return;
    }
    EdenWorld &world = *pWorldRef;
    std::vector<Matrix4x4f> worlds;
    document.resolveWorlds( worlds );

    for ( size_t i = 0; i < document.Objects.size(); i++ ) {
        const SceneObject &object = document.Objects[ i ];
        // group: transform only (the parent of attachments); spot: the mode's
        // SpotExteriors streamer draws it (one streaming path for buildings).
        if ( object.Kind == "group" || object.Kind == "spot" ) {
            continue;
        }
        Instance instance;
        instance.Id = object.Id;
        instance.Kind = object.Kind;
        instance.Ref = object.Ref;
        instance.bVisible = object.bVisible;
        instance.World = worlds[ i ];
        if ( !object.bVisible ) {
            instances.push_back( std::move( instance ) );
            continue;
        }

        if ( object.Kind == "library" ) {
            // Node by hashcode (GetIdentifier64bit of the 8 character name,
            // the libraryNodes map key), by name through the scene, or as
            // "hc:<hex>".
            int32_t nodeIndex = -1;
            const uint64_t hash = EdenWorld::OverrideHash( object.Ref );
            if ( hash != 0 ) {
                nodeIndex = world.findLibraryNode( hash );
            }
            if ( nodeIndex < 0 ) {
                const std::vector<SceneNode> &nodes = world.getLibraryModel().getScene().getNodes();
                for ( size_t n = 0; n < nodes.size() && nodeIndex < 0; n++ ) {
                    if ( StringCmpI( nodes[ n ].Name.c_str(), object.Ref.c_str() ) == 0 ) {
                        nodeIndex = static_cast<int32_t>( n );
                    }
                }
            }
            if ( nodeIndex < 0 ) {
                OTDU_LOG_WARN( "SceneEditor: library node '%s' not found in the island library\n", object.Ref.c_str() );
                continue;
            }
            instance.LibraryNode = nodeIndex;
            Vector3f localMin, localMax;
            if ( world.getLibraryNodeBounds( nodeIndex, localMin, localMax ) ) {
                TransformBox( instance.World, localMin, localMax, instance.BoundsMin, instance.BoundsMax );
            }
            instances.push_back( std::move( instance ) );
            continue;
        }

        if ( object.Kind == "actor" ) {
            // The entity name -> __Hc (worldactors.bnk entities.xmb), then the
            // model (loaded on demand like the .pmi A lists).
            uint64_t actorHash = EdenWorld::OverrideHash( object.Ref );
            bool bFound = false;
            for ( const auto &entry : world.getActorNames() ) {
                if ( StringCmpI( entry.second.c_str(), object.Ref.c_str() ) == 0 ) {
                    actorHash = entry.first;
                    bFound = true;
                    break;
                }
            }
            if ( !bFound && actorHash == 0 ) {
                OTDU_LOG_WARN( "SceneEditor: actor '%s' not found in worldactors.bnk\n", object.Ref.c_str() );
                continue;
            }
            EdenModel *pModel = world.getActorModelPtr( actorHash );
            if ( pModel == nullptr || !pModel->isLoaded() ) {
                OTDU_LOG_WARN( "SceneEditor: actor '%s' has no model\n", object.Ref.c_str() );
                continue;
            }
            instance.ActorHash = actorHash;
            instance.pActorModel = pModel;
            TransformBox( instance.World, pModel->getBoundsMin(), pModel->getBoundsMax(), instance.BoundsMin,
                          instance.BoundsMax );
            instances.push_back( std::move( instance ) );
            continue;
        }

        if ( object.Kind == "prop" || object.Kind == "obj" ) {
            // An OBJ from the overlay's props folder (the ModProps route).
            EdenRenderer *pRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr;
            if ( pRenderer == nullptr ) {
                continue;
            }
            const std::string shader = instance.ShaderName.empty() ? std::string( "TD_Gouraud" ) : instance.ShaderName;
            Vector3f boundsMin, boundsMax;
            uint32_t numTriangles = 0;
            if ( !LoadObjDraws( *pRenderer, "props/", object.Ref.c_str(), shader.c_str(), instance.ObjDraws, &boundsMin,
                                &boundsMax, &numTriangles, "SceneObjects" ) ) {
                OTDU_LOG_WARN( "SceneEditor: prop '%s' did not load from <mods>/props/\n", object.Ref.c_str() );
                continue;
            }
            TransformBox( instance.World, boundsMin, boundsMax, instance.BoundsMin, instance.BoundsMax );
            instances.push_back( std::move( instance ) );
            continue;
        }

        if ( object.Kind != "spot" ) {
            OTDU_LOG_WARN( "SceneEditor: unknown object kind '%s' (id %s)\n", object.Kind.c_str(), object.Id.c_str() );
        }
    }
    numDrawn = instances.size();
}

void SceneObjects::draw( EdenRenderer &renderer ) const {
    if ( pWorldRef == nullptr ) {
        return;
    }
    for ( const Instance &instance : instances ) {
        if ( !instance.bVisible ) {
            continue;
        }
        if ( instance.Kind == "library" ) {
            pWorldRef->drawLibraryNode( renderer, instance.LibraryNode, instance.World );
        } else if ( instance.Kind == "actor" && instance.pActorModel != nullptr ) {
            instance.pActorModel->draw( renderer, instance.World );
        } else if ( instance.Kind == "prop" || instance.Kind == "obj" ) {
            if ( !renderer.isBoxVisible( instance.World, instance.BoundsMin, instance.BoundsMax ) ) {
                continue;
            }
            for ( const ObjMeshDraw &draw : instance.ObjDraws ) {
                renderer.draw( draw.Mesh, draw.Material, instance.World );
            }
        }
    }
}

const SceneObjects::Instance *SceneObjects::findInstance( const std::string &id ) const {
    for ( const Instance &instance : instances ) {
        if ( instance.Id == id ) {
            return &instance;
        }
    }
    return nullptr;
}

bool SceneObjects::raycast( const SceneRay &ray, float &outDistance, std::string &outInstanceId ) const {
    bool bHit = false;
    for ( const Instance &instance : instances ) {
        if ( !instance.bVisible ) {
            continue;
        }
        float distance = 0.0f;
        if ( SceneRayBoxIntersect( ray, instance.BoundsMin, instance.BoundsMax, distance ) &&
             ( !bHit || distance < outDistance ) ) {
            outDistance = distance;
            outInstanceId = instance.Id;
            bHit = true;
        }
    }
    return bHit;
}
