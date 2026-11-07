#include "shared.h"
#include "walk_mode.h"

#include "game/avatarchoice/intro_cinematic.h"
#include "physics/havok_collision.h"
#include "system/cmd_line.h"

namespace {
// FUN_00952410 (walk-mode manager per frame, docs/re/tess_garage_spec.md
// 4.1): a selectable is a candidate when its position (wrapper vf08 =
// entity +0x40, the orient_pos_scale pos) is in front of the eye, nearer
// than the wrapper radius vf2c (FUN_00953d50 = DAT_00edd270 = 10.0 m, the
// same for every wrapper vtable) and nearer than the best candidate so far;
// then the reticle ray must hit it (vf04).
// (The 50 m of FUN_00952410, DAT_00f4a040, is the /cursor depth raycast
// FUN_00bd2f80 -> cursor vf+0x100, not a selection distance.)
constexpr float kSelectRadius = 10.0f;
constexpr float kSelectionCooldown = 0.2f; // FUN_0094f440
// Walk camera (camera type 9, ctor FUN_00c823e0, docs/re/tess_garage_spec.md
// 4.3b) [CODE]: FUN_00c825a0 casts a ray 10 m (DAT_00edd270) down from the
// camera and creates the Havok character proxy (FUN_00bcea00: capsule radius
// 0.3 (DAT_00ede42c), height 1.2 (DAT_00edf24c, vertices +-0.6)) with its
// centre at hit + 0.3 (DAT_00f4a178) + 0.6 (DAT_00f49ea0); FUN_00c82840 sets
// the eye offset camera+0x3c0 = (0, 0.7 (DAT_00eddb08), 0) above the proxy
// centre, and FUN_00950470 puts the avatar at eye - 0.7 - 0.9 (the floor).
// Eye = floor + 0.3 + 0.6 + 0.7 = 1.6 m, the same for man and woman (no
// avatar size term in this code).
constexpr float kProxyCenterHeight = 0.3f + 0.6f;
constexpr float kEyeOffset = 0.7f;
constexpr float kEyeHeight = kProxyCenterHeight + kEyeOffset;
// Near plane of the first person walk camera [INF: the cinematic default
// (0.8 m) clips the walls and objects the player stands next to; the garage
// cockpit camera of the same game data uses 0.05].
constexpr float kWalkNearPlane = 0.05f;
constexpr float kStartRayLength = 10.0f;
// Body sphere = the proxy radius [CODE]; step height: the character proxy
// step/slope handling is encrypted (FUN_006929b0, max slope arg 80) [ENC/INF].
constexpr float kRadius = 0.3f;
constexpr float kStepUp = 0.4f;
constexpr float kMaxDrop = 2.0f;

// FpSelectable Type -> /:SelItemType: FUN_00953350 (wrapper vf0c) looks the
// Type (entity +0x170) up in the table at 0x00edd580 ({type, name, icon,
// ...}, 16 bytes, ends at -1); Type 57 (0x39) uses the wrapper vf3c instead
// (encrypted [ENC]; [INF] the SelectableString, "car" for FST_EnterCar).
struct SelectableTypeName {
    int32_t Type;
    const char *pName;
};
constexpr SelectableTypeName kSelectableTypeNames[] = {
    { 0, "exit" },          { 1, "carseller" },      { 4, "stolencar" },     { 2, "player" },
    { 3, "carrenter" },     { 5, "garage" },         { 6, "living" },        { 54, "club" },
    { 7, "realtor" },       { 8, "photographer" },   { 9, "diary" },         { 10, "dressing" },
    { 49, "game_progress" }, { 50, "bar" },          { 51, "office" },       { 52, "photos" },
    { 53, "photo_expo" },   { 12, "billboard" },     { 13, "billboard" },    { 14, "billboard" },
    { 55, "billboard" },    { 15, "billboard" },     { 16, "billboard" },    { 33, "billboard" },
    { 34, "billboard" },    { 35, "billboard" },     { 36, "billboard" },    { 37, "billboard" },
    { 38, "billboard" },    { 39, "billboard" },     { 40, "billboard" },    { 41, "billboard" },
    { 42, "billboard" },    { 43, "billboard" },     { 44, "billboard" },    { 45, "billboard" },
    { 46, "billboard" },    { 47, "billboard" },     { 48, "billboard" },    { 17, "npc" },
    { 18, "npc" },          { 19, "npc" },           { 20, "tutor" },        { 21, "npc_receiver" },
    { 22, "goodies" },      { 23, "npc_tuner" },     { 24, "npc_stickers" }, { 25, "npc_stickers" },
    { 26, "npc" },          { 27, "surgeon" },       { 28, "npc_stickers" }, { 29, "hairdresser" },
    { 30, "npc" },          { 31, "npc" },           { 32, "npc_airport_hostess" }, { 56, "jacuzzi" },
};

const char *ItemTypeOf( const IntroCinematic::Volume &volume ) {
    if ( volume.SelectableType == 57 ) {
        return volume.SelectableString.c_str();
    }
    for ( const SelectableTypeName &entry : kSelectableTypeNames ) {
        if ( entry.Type == volume.SelectableType ) {
            return entry.pName;
        }
    }
    return ""; // the table sentinel: "exit"'s icon, empty name
}
} // namespace

bool WalkMode::start( IntroCinematic &scene, const std::string &cameraName, float newFov ) {
    fov = newFov;
    selected = -1;
    cooldown = 0.0f;
    Vector3f position;
    Quaternionf heading;
    if ( !scene.getEntityCameraLocal( cameraName, position, heading ) ) {
        OTDU_LOG_WARN( "WalkMode: camera %s not found\n", cameraName.c_str() );
        return false;
    }
    // Test shortcut: -walkat x,y,z,yawDegrees (scene-local eye position) - the
    // FIRST walk of the run only (the room the script opens): the later room
    // swaps keep their own cameras (the garage's cam_entree stands on the
    // garage floors; the pinned one would not).
    static bool bWalkatUsed = false;
    if ( !bWalkatUsed ) {
        if ( const char *pAt = CmdLine::GetArgValue( "-walkat" ) ) {
            float yawDegrees = 0.0f;
            if ( sscanf( pAt, "%f,%f,%f,%f", &position.x, &position.y, &position.z, &yawDegrees ) == 4 ) {
                const Quaternionf conjugate = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yawDegrees * kDegToRad );
                heading = Quaternionf( -conjugate.x, -conjugate.y, -conjugate.z, conjugate.w );
                bWalkatUsed = true;
            }
        }
    }
    controller.setCollide(
        [this, &scene]( const Vector3f &from, const Vector3f &to ) { return collide( scene, from, to ); } );
    // FUN_00c825a0: the proxy is dropped on the floor under the start camera
    // (ray 10 m down), the eye is 1.6 m above it (see kEyeHeight). No floor
    // hit: the original uses camera+0x330 + 2 m [CODE, field not traced];
    // OpenTDU2 then keeps the start camera height.
    eyeHeight = kEyeHeight;
    float floorY = 0.0f;
    bool bFloor = FindHavokFloor( scene.getCollision(), position, 0.1f, kStartRayLength, floorY );
    // Off the room's floors (the house yard spawn: the Living swap lands at
    // cam_entree_garage, outside the room's .shk): the world hook answers the
    // ground and pushes the body clear of the yard's obstacles.
    if ( !bFloor && worldHook ) {
        Vector3f body( position.x, position.y - eyeHeight + kStepUp + kRadius, position.z );
        if ( worldHook( body, body, floorY ) ) {
            position.x = body.x;
            position.z = body.z;
            position.y = floorY + eyeHeight;
            bFloor = true;
        }
    }
    if ( bFloor ) {
        OTDU_LOG_INFO( "WalkMode: %s y %.3f, floor %.3f -> eye %.3f\n", cameraName.c_str(), position.y, floorY,
                       floorY + eyeHeight );
        position.y = floorY + eyeHeight;
    }
    controller.reset( position, heading );
    resume( scene );
    return true;
}

void WalkMode::resume( IntroCinematic &scene ) {
    scene.setLocalCamera( controller.getPosition(), controller.getHeading(), fov, kWalkNearPlane );
}

Vector3f WalkMode::collide( const IntroCinematic &scene, const Vector3f &from, const Vector3f &to ) const {
    // Scene collision (.shk): the body sphere is pushed out of the walls,
    // the feet follow the floor (step up / drop).
    const HavokCollision &collision = scene.getCollision();
    if ( collision.Indices.empty() && collision.Boxes.empty() ) {
        return Vector3f( to.x, from.y, to.z );
    }
    Vector3f feet( to.x, from.y - eyeHeight, to.z );
    Vector3f body = feet + Vector3f( 0.0f, kStepUp + kRadius, 0.0f );
    body = ResolveSphereHavokCollision( collision, body, kRadius );
    // The people (avatars, fun bots): vertical capsules at the PNJ scene
    // positions (radius 0.3 m, head at 1.7 m — the same proxy dimensions the
    // original gives the walk character, walk_mode kRadius / kEyeHeight).
    for ( size_t i = 0; i < scene.getNumPnjs(); i++ ) {
        Vector3f other;
        if ( !scene.getPnjScenePosition( i, other ) ) {
            continue;
        }
        const Vector3f delta( body.x - other.x, 0.0f, body.z - other.z );
        if ( delta.dot( delta ) > 3.0f * 3.0f ) {
            continue;
        }
        const Vector3f closest = ClosestPointOnSegment( body, other + Vector3f( 0.0f, 0.6f, 0.0f ),
                                                        other + Vector3f( 0.0f, 1.1f, 0.0f ) );
        const Vector3f distance = body - closest;
        const float d = distance.length();
        const float minDist = kRadius + 0.3f;
        if ( d < minDist && d > 1e-6f ) {
            body += ( body - closest ) * ( ( minDist - d ) / d );
        }
    }
    // The scene car (FIRST_WM): sphere vs the oriented body bounds.
    {
        Matrix4x4f carLocal;
        Vector3f boundsMin, boundsMax;
        if ( scene.getCarLocal( carLocal ) && scene.getCar().getBodyBounds( boundsMin, boundsMax ) ) {
            const Vector3f localCenter = ( boundsMin + boundsMax ) * 0.5f;
            const Vector3f center = carLocal.transformPoint( localCenter );
            const Vector3f axes[3] = { carLocal.transformVector( Vector3f( 1.0f, 0.0f, 0.0f ) ),
                                       carLocal.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) ),
                                       carLocal.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) };
            const Vector3f halfExtents = ( boundsMax - boundsMin ) * 0.5f;
            const float *extent = &halfExtents.x;
            const Vector3f rel = body - center;
            float local[3];
            bool bInside = true;
            for ( int k = 0; k < 3; k++ ) {
                local[k] = rel.dot( axes[k] );
                if ( fabsf( local[k] ) > extent[k] + kRadius ) {
                    bInside = false;
                }
            }
            if ( bInside ) {
                // Closest point of the OBB to the sphere centre (outside:
                // clamped; inside: the nearest face).
                Vector3f closest = center;
                bool bSphereInside = true;
                for ( int k = 0; k < 3; k++ ) {
                    if ( fabsf( local[k] ) > extent[k] ) {
                        bSphereInside = false;
                    }
                    closest += axes[k] * Clamp( local[k], -extent[k], extent[k] );
                }
                if ( bSphereInside ) {
                    int axis = 0;
                    float minPen = FLT_MAX;
                    float sign = 1.0f;
                    for ( int k = 0; k < 3; k++ ) {
                        const float pen = extent[k] + kRadius - fabsf( local[k] );
                        if ( pen < minPen ) {
                            minPen = pen;
                            axis = k;
                            sign = local[k] >= 0.0f ? 1.0f : -1.0f;
                        }
                    }
                    body += axes[axis] * ( sign * minPen );
                } else {
                    const Vector3f d = body - closest;
                    const float len = d.length();
                    if ( len < kRadius && len > 1e-6f ) {
                        body += d * ( ( kRadius - len ) / len );
                    }
                }
            }
        }
    }
    // The world outside the room (the house yard): the sector statics and the
    // exterior shell push the body out, the hook also answers the ground where
    // the room's .shk has none (the floor fallback below).
    bool bWorldFloor = false;
    float worldFloorY = 0.0f;
    if ( worldHook ) {
        const Vector3f bodyPrevious = from - Vector3f( 0.0f, eyeHeight - ( kStepUp + kRadius ), 0.0f );
        bWorldFloor = worldHook( body, bodyPrevious, worldFloorY );
    }
    feet = Vector3f( body.x, feet.y, body.z );
    // Floor under the body footprint (the body is not a point: the house
    // floor meshes have seams, e.g. a 0.2 m gap at the living room door
    // x 9.3..9.5): the centre, else the highest floor within the radius.
    float floorY = 0.0f;
    bool bFloor = FindHavokFloor( collision, feet, kStepUp, kMaxDrop, floorY );
    for ( int32_t i = 0; i < 8 && !bFloor; i++ ) {
        const float angle = i * ( 2.0f * kPI / 8.0f );
        float y = 0.0f;
        if ( FindHavokFloor( collision, feet + Vector3f( std::cos( angle ) * kRadius, 0.0f, std::sin( angle ) * kRadius ),
                             kStepUp, kMaxDrop, y ) ) {
            floorY = y;
            bFloor = true;
        }
    }
    // Off the room's floors (the house yard): the streamed island under the
    // plot (interior_walk.cpp hook). Same step / drop bounds as the .shk
    // search above; false there keeps the freeze law (return from).
    if ( !bFloor && bWorldFloor ) {
        floorY = worldFloorY;
        bFloor = true;
    }
    if ( !bFloor ) {
        return from;
    }
    feet.y = floorY;
    return feet + Vector3f( 0.0f, eyeHeight, 0.0f );
}

const char *WalkMode::getSelectedType( const IntroCinematic &scene ) const {
    const auto &selectables = scene.getSelectables();
    if ( selected < 0 || selected >= static_cast<int32_t>( selectables.size() ) ) {
        return "";
    }
    return ItemTypeOf( selectables[selected] );
}

void WalkMode::clearSelection( IntroCinematic &scene ) {
    if ( selected >= 0 ) {
        if ( bCursorLabels ) {
            scene.getUi().gotoLabel( "/cursor", "off", true );
        } else {
            scene.getUi().invoke( "/", "ItemType", { "out" } );
        }
    }
    selected = -1;
}

void WalkMode::updateSelection( IntroCinematic &scene, float deltaTime ) {
    // FUN_00952410: per selectable (wrapper list +0xb80), d = position - eye
    // (eye = camera +0x310):
    //   dot(forward, d / |d|) > 0, |d|^2 < vf2c()^2 (10 m), |d|^2 < best,
    //   vf04(eye, forward) (FUN_009533b0 -> FUN_00953d80 for a box: the ray
    //   meets the volume and the scene collision ray (FUN_00bd2f80) up to the
    //   far side is free or hits inside the volume);
    // the nearest position wins; FUN_0094f440 SetSelected.
    cooldown -= deltaTime;
    const Vector3f origin = controller.getPosition();
    const Vector3f direction = controller.getForward();
    const auto &selectables = scene.getSelectables();
    const HavokCollision &collision = scene.getCollision();
    int32_t best = -1;
    float bestDistance2 = kSelectRadius * kSelectRadius;
    // -logselect: why each selectable is refused (once per 0.5 s).
    static const bool bLogSelect = CmdLine::HasArg( "-logselect" );
    static float logSelectTimer = 0.0f;
    logSelectTimer -= deltaTime;
    const bool bLogNow = bLogSelect && logSelectTimer <= 0.0f;
    if ( bLogNow ) {
        logSelectTimer = 0.5f;
    }
    for ( size_t i = 0; i < selectables.size(); i++ ) {
        if ( selectableFilter && !selectableFilter( static_cast<int32_t>( i ) ) ) {
            continue;
        }
        const IntroCinematic::Volume &volume = selectables[i];
        const Vector3f toCenter = volume.Center - origin;
        const float distance2 = toCenter.dot( toCenter );
        if ( toCenter.dot( direction ) <= 0.0f || distance2 >= bestDistance2 ) {
            if ( bLogNow ) {
                OTDU_LOG_INFO( "SELECT: '%s' behind or too far (%.2f m)\n", volume.Name.c_str(), std::sqrt( distance2 ) );
            }
            continue;
        }
        float tEnter = 0.0f;
        float tExit = 0.0f;
        if ( !volume.raycast( origin, direction, kSelectRadius * 2.0f, tEnter, &tExit ) ) {
            if ( bLogNow ) {
                OTDU_LOG_INFO( "SELECT: '%s' not under the reticle (%.2f m)\n", volume.Name.c_str(), std::sqrt( distance2 ) );
            }
            continue;
        }
        float tHit = 0.0f;
        if ( RaycastHavokCollision( collision, origin, direction, tExit, tHit ) &&
             !volume.contains( origin + direction * tHit ) ) {
            if ( bLogNow ) {
                OTDU_LOG_INFO( "SELECT: '%s' occluded at %.2f m (volume %.2f..%.2f m)\n", volume.Name.c_str(), tHit,
                               tEnter, tExit );
            }
            continue; // occluded (FUN_00ccb960: the hit point is not in the volume)
        }
        best = static_cast<int32_t>( i );
        bestDistance2 = distance2;
    }
    if ( best == selected || ( cooldown > 0.0f && best != -1 ) ) {
        return;
    }
    FlashPlayer &ui = scene.getUi();
    if ( selected >= 0 && !bCursorLabels ) {
        ui.invoke( "/", "ItemType", { "out" } );
    }
    selected = best;
    cooldown = kSelectionCooldown;
    if ( selectionCallback ) {
        selectionCallback( best );
    }
    if ( bCursorLabels ) {
        if ( best >= 0 ) {
            ui.setVariable( "/:SelItemType", ItemTypeOf( selectables[best] ) );
        }
        ui.gotoLabel( "/cursor", best >= 0 ? "on" : "off", true );
    } else if ( best >= 0 ) {
        ui.setVariable( "/:SelItemType", ItemTypeOf( selectables[best] ) );
        ui.invoke( "/", "ItemType", { "over" } );
    }
}

void WalkMode::update( IntroCinematic &scene, float deltaTime, bool bInputEnabled ) {
    controller.update( deltaTime, bInputEnabled );
    resume( scene );
    // -logwalk: eye position + PNJ proximity every 0.5 s (body collisions).
    static const bool bLogWalk = CmdLine::HasArg( "-logwalk" );
    if ( bLogWalk ) {
        static float logTimer = 0.0f;
        logTimer += deltaTime;
        if ( logTimer >= 0.5f ) {
            logTimer = 0.0f;
            const Vector3f &p = controller.getPosition();
            float nearest = 1e9f;
            for ( size_t i = 0; i < scene.getNumPnjs(); i++ ) {
                Vector3f other;
                if ( scene.getPnjScenePosition( i, other ) ) {
                    nearest = Min( nearest, Vector3f( p.x - other.x, 0.0f, p.z - other.z ).length() );
                }
            }
            OTDU_LOG_INFO( "WALK: eye (%.2f, %.2f, %.2f), nearest person %.2f m\n", p.x, p.y, p.z,
                           nearest < 1e8f ? nearest : -1.0f );
        }
    }
    if ( bInputEnabled ) {
        updateSelection( scene, deltaTime );
    }
}

void WalkMode::placeLooking( IntroCinematic &scene, const Vector3f &eye, const Vector3f &target ) {
    const Vector3f forward = ( target - eye ).normalized();
    const float yaw = std::atan2( forward.x, forward.z );
    const float pitch = std::asin( Clamp( forward.y, -1.0f, 1.0f ) );
    // The walk heading law (WalkController::getHeading): q* = Ry(yaw) Rx(-pitch).
    const Quaternionf conjugate = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw ) *
                                  Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), -pitch );
    controller.reset( eye, Quaternionf( -conjugate.x, -conjugate.y, -conjugate.z, conjugate.w ) );
    resume( scene );
}
