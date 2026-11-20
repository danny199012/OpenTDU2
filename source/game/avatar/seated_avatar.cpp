#include "shared.h"
#include "seated_avatar.h"

#include "game/cinematic/cinematic_car.h"
#include "render/eden/eden_weather.h"
#include "system/cmd_line.h"

bool SeatedAvatar::load( EdenAvatarLibrary &library, const char *pConfigId, eSeat newSeat, const Bank &driverBank ) {
    unload();
    seat = newSeat;
    if ( !avatar.load( library, pConfigId ) ) {
        return false;
    }
    // avatarsdriver.bnk [DATA]: Entity__AvatarPlayer layer 0 = stance_turn.xan
    // (driver); Entity__AvatarPassenger PassengerWoman animPassenger[0] =
    // shoppinggirlcarlayer0.bas -> SHO_stand_car.xan, PassengerMan
    // hitchhikerlayer0.bas -> hit_stand_car.xan (the .xan are .anm in the bank).
    const char *pClip = seat == eSeat::Driver ? "stance_turn.anm"
                        : avatar.isWoman()    ? "sho_stand_car.anm"
                                              : "hit_stand_car.anm";
    const BankEntry *pEntry = driverBank.findEntry( pClip );
    bBaseClip = pEntry != nullptr && baseClip.parse( pEntry->pData, pEntry->Size );
    if ( !bBaseClip ) {
        OTDU_LOG_WARN( "SeatedAvatar: base clip '%s' not found in avatarsdriver.bnk\n", pClip );
    }
    offsetsCar = -2;
    bDriveClips = false;
    if ( seat == eSeat::Driver && !CmdLine::HasArg( "-noclipdrive" ) ) {
        auto loadClip = [&]( EdenAnimation &clip, const char *pName ) {
            const BankEntry *pClipEntry = driverBank.findEntry( pName );
            return pClipEntry != nullptr && clip.parse( pClipEntry->pData, pClipEntry->Size );
        };
        const bool w = avatar.isWoman();
        bDriveClips = loadClip( turnClip, "turn.anm" ) && loadClip( handOpenClip, "hand_open.anm" ) &&
                      loadClip( gearSeqUp, w ? "gear_seq_up-w.anm" : "gear_seq_up.anm" ) &&
                      loadClip( gearSeqDown, w ? "gear_seq_down-w.anm" : "gear_seq_down.anm" ) &&
                      loadClip( gearLeverUp, "gear_lever_up.anm" ) && loadClip( gearLeverDown, "gear_lever_down.anm" );
        if ( !bDriveClips ) {
            OTDU_LOG_WARN( "SeatedAvatar: driving clips missing in avatarsdriver.bnk, procedural IK only\n" );
        } else if ( CmdLine::HasArg( "-logdriveclips" ) ) {
            const EdenSkeleton &sk = avatar.getSkeleton();
            const std::pair<const char *, const EdenAnimation *> list[] = { { "turn", &turnClip },
                                                                            { "hand_open", &handOpenClip },
                                                                            { "gear_seq_up", &gearSeqUp },
                                                                            { "gear_seq_down", &gearSeqDown },
                                                                            { "gear_lever_up", &gearLeverUp },
                                                                            { "gear_lever_down", &gearLeverDown } };
            for ( const auto &c : list ) {
                std::string bones;
                for ( size_t i = 0; i < c.second->getTrackCount(); i++ ) {
                    bones += std::to_string( sk.findBone( c.second->getTrackBone( i ) ) ) + ",";
                }
                OTDU_LOG_INFO( "DRIVECLIP %s dur %.3f loop %.3f tracks %zu bones %s\n", c.first,
                               c.second->getDuration(), c.second->getLoopDuration(), c.second->getTrackCount(),
                               bones.c_str() );
            }
            for ( const auto &c : list ) {
                if ( std::string( c.first ).find( "gear" ) != 0 ) {
                    continue;
                }
                for ( float f : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f } ) {
                    std::vector<BoneTransform> l( sk.getNumBones() );
                    std::vector<Matrix4x4f> sm, wm;
                    for ( uint32_t b = 0; b < sk.getNumBones(); b++ ) {
                        l[b] = sk.getBindLocal( b );
                    }
                    turnClip.sample( sk, 0.5f * turnClip.getDuration(), false, l );
                    c.second->sample( sk, f * c.second->getDuration(), false, l );
                    sk.computeSkinMatrices( l, wm, sm );
                    const Vector3f pr = wm[sk.findBone( EdenNameHash4( "Hand_R" ) )].getTranslation();
                    const Vector3f pl = wm[sk.findBone( EdenNameHash4( "Hand_L" ) )].getTranslation();
                    OTDU_LOG_INFO( "DRIVECLIP %s f=%.2f HandR (%.3f %.3f %.3f) HandL (%.3f %.3f %.3f)\n", c.first, f, pr.x, pr.y, pr.z, pl.x, pl.y, pl.z );
                }
            }
            const char *pHands[] = { "Hand_R", "Hand_L", "ForeArm_R" };
            const float fr[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
            for ( float f : fr ) {
                std::vector<BoneTransform> l( sk.getNumBones() );
                std::vector<Matrix4x4f> sm, wm;
                for ( uint32_t b = 0; b < sk.getNumBones(); b++ ) {
                    l[b] = sk.getBindLocal( b );
                }
                turnClip.sample( sk, f * turnClip.getDuration(), false, l );
                sk.computeSkinMatrices( l, wm, sm );
                for ( const char *h : pHands ) {
                    const Vector3f p = wm[sk.findBone( EdenNameHash4( h ) )].getTranslation();
                    OTDU_LOG_INFO( "DRIVECLIP turn f=%.2f %s model (%.3f %.3f %.3f)\n", f, h, p.x, p.y, p.z );
                }
            }
        }
    }
    return true;
}

void SeatedAvatar::unload() {
    avatar.unload();
    bBaseClip = false;
    pOverride = nullptr;
    bVisible = false;
}

void SeatedAvatar::update( const CinematicCar &car, const Matrix4x4f &carWorld,
                           const std::function<void( std::vector<BoneTransform> & )> &facial ) {
    bVisible = false;
    const bool bDriver = seat == eSeat::Driver;
    Matrix4x4f seatNode;
    if ( !avatar.isLoaded() || !car.getNodeModelTransform( bDriver ? "DVRSEAT_LR" : "PSGSEAT_LR", seatNode ) ) {
        return;
    }
    if ( offsetsCar != car.getIdCar() ) {
        offsetsCar = car.getIdCar();
        if ( !LoadCarIKOffsets( offsetsCar, avatar.isWoman(), offsets ) ) {
            OTDU_LOG_WARN( "SeatedAvatar: no IKOffsetDB record for car %d (offsets 0)\n", offsetsCar );
        }
    }
    const EdenSkeleton &skeleton = avatar.getSkeleton();
    std::vector<BoneTransform> locals( skeleton.getNumBones() );
    for ( uint32_t b = 0; b < skeleton.getNumBones(); b++ ) {
        locals[b] = skeleton.getBindLocal( b );
    }
    if ( bBaseClip ) {
        baseClip.sample( skeleton, 0.0f, true, locals );
    }
    // Layer 0 of driveiklayer0.bas: turn.xan weighted by the steering angle [DATA]. The clip is one lock
    // to lock sweep (1.04 s): t=0 right lock (right hand at the bottom of the rim), mid = straight
    // (= stance_turn), end = left lock; the rim moves +-90 deg over it [DATA, -logdriveclips]. How the
    // game maps the angle to the clip time is [INF]: linear, clamped at the clip's lock; beyond it the
    // hand IK below keeps the hands on the rim.
    const bool bClips = bDriveClips && seat == eSeat::Driver && !CmdLine::HasArg( "-noclipdrive" );
    steerFraction = Clamp( driveWheelAngle / 1.5708f, -1.0f, 1.0f );
    if ( bClips ) {
        turnClip.sample( skeleton, ( 0.5f + 0.5f * steerFraction ) * turnClip.getDuration(), false, locals );
    }
    if ( pOverride != nullptr && overrideWeight > 0.0f ) {
        std::vector<BoneTransform> clip = locals;
        pOverride->sample( skeleton, overrideTime, false, clip );
        if ( overrideWeight >= 1.0f ) {
            locals.swap( clip );
        } else {
            std::vector<BoneTransform> blended;
            BlendPoses( locals, clip, overrideWeight, blended );
            locals.swap( blended );
        }
    }

    // FUN_00b8ade0: seat group adjustment (0, y, z) then the seat node
    // position + R * offset (identity node rotation for the California
    // seats) [CODE]; FUN_0042b4f0's add-vs-set is [ENC]: added [INF].
    const Vector3f seatAdjust =
        bDriver ? Vector3f( 0.0f, offsets.driverSeatY(), offsets.driverSeatZ() )
                : Vector3f( 0.0f, offsets.passengerSeatY(), offsets.passengerSeatZ() );
    const int32_t seatIndex = bDriver ? CarIKOffsets::DvrSeat : CarIKOffsets::PsgSeat;
    const Matrix4x4f seatMatrix = Matrix4x4f::Translation( seatAdjust + offsets.Node[seatIndex] ) * seatNode;
    const Matrix4x4f seatInverse = seatMatrix.inverse();

    // The shift: after a gear change the right hand goes to the lever and back
    // (reach 0.2 s, hold 0.15 s, return 0.25 s) [INF: the clip layer 5 timings
    // (gear_seq_up / down) are not decoded].
    shiftTime += driveDelta;
    float shiftBlend = 0.0f;
    if ( bDriver && !bClips && !CmdLine::HasArg( "-noshift" ) ) {
        const float t = shiftTime;
        shiftBlend = t < 0.2f ? t / 0.2f : t < 0.35f ? 1.0f : t < 0.6f ? 1.0f - ( t - 0.35f ) / 0.25f : 0.0f;
        shiftBlend = shiftBlend * shiftBlend * ( 3.0f - 2.0f * shiftBlend );
    }
    // Layer 5 (driveiklayer5.bas): gear_seq_up / gear_seq_down, 1.0 s [DATA]. The clips only pose the
    // arm that shifts: seq_up the RIGHT arm, seq_down the LEFT one mirrored (hand x -0.55 / +0.55 m,
    // y 0.14 m, a 6 cm lift and back, -logdriveclips), i.e. the right-hand-drive twin - a California is
    // left-hand drive, so BOTH directions play seq_up [INF]. The hand position is left to the layer-6
    // IK (goal = the car's Gear node, FUN_00b7e940 [CODE]); the clip supplies the elbow / wrist / finger
    // pose and the lift, added to the goal. Ramp in / out 0.2 s [INF].
    // Test hook: -shifttest <+1/-1> plays that shift once after 60 updates.
    Vector3f shiftLift( 0.0f, 0.0f, 0.0f );
    if ( bClips ) {
        static const char *pShiftTest = CmdLine::GetArgValue( "-shifttest" );
        static int32_t shiftTestFrames = 0;
        if ( pShiftTest != nullptr && ++shiftTestFrames == CmdLine::GetArgInt( "-shifttestat", 60 ) ) {
            shiftDirection = atoi( pShiftTest ) >= 0 ? 1 : -1;
            shiftPlay = 0.0f;
        }
        if ( shiftPlay >= 0.0f ) {
            shiftPlay += driveDelta;
            const float dur = gearSeqUp.getDuration();
            if ( shiftPlay >= dur ) {
                shiftPlay = -1.0f;
            } else {
                const float t = shiftDirection > 0 ? shiftPlay : dur - shiftPlay; // down plays it reversed [INF]
                const float w = Clamp( std::min( shiftPlay, dur - shiftPlay ) / 0.2f, 0.0f, 1.0f );
                shiftBlend = w * w * ( 3.0f - 2.0f * w );
                std::vector<BoneTransform> clip = locals;
                gearSeqUp.sample( skeleton, t, false, clip );
                std::vector<BoneTransform> mixed;
                BlendPoses( locals, clip, shiftBlend, mixed );
                for ( size_t i = 0; i < gearSeqUp.getTrackCount(); i++ ) {
                    const int32_t b = skeleton.findBone( gearSeqUp.getTrackBone( i ) );
                    if ( b >= 0 ) {
                        locals[b] = mixed[b];
                    }
                }
                std::vector<BoneTransform> start = locals;
                gearSeqUp.sample( skeleton, 0.0f, false, start );
                std::vector<Matrix4x4f> wm0, wm1, sk0;
                skeleton.computeSkinMatrices( clip, wm1, sk0 );
                skeleton.computeSkinMatrices( start, wm0, sk0 );
                const int32_t hr = skeleton.findBone( EdenNameHash4( "Hand_R" ) );
                if ( hr >= 0 ) {
                    shiftLift = wm1[hr].getTranslation() - wm0[hr].getTranslation();
                }
                static const bool bLogShift = CmdLine::HasArg( "-logshift" );
                if ( bLogShift ) {
                    OTDU_LOG_INFO( "SHIFT %s t %.2f/%.2f blend %.2f lift %.3f\n", shiftDirection > 0 ? "up" : "down", shiftPlay, dur,
                                   shiftBlend, shiftLift.y );
                }
            }
        }
    }
    const bool bSteer = bDriver && driveAxis.dot( driveAxis ) > 0.5f && !CmdLine::HasArg( "-nosteerhands" );
    const Matrix4x4f wheelTurn = bSteer ? Matrix4x4f::Translation( drivePivot ) *
                                              Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( driveAxis, driveWheelAngle ) ) *
                                              Matrix4x4f::Translation( -drivePivot )
                                        : Matrix4x4f::Identity();
    auto goal = [&]( const char *pNode, int32_t index, bool bInSeatGroup, Vector3f &out, bool bOnWheel = false ) {
        Matrix4x4f node;
        if ( !car.getNodeModelTransform( pNode, node ) ) {
            return false;
        }
        Vector3f position = node.getTranslation() + node.transformVector( offsets.Node[index] );
        if ( bInSeatGroup ) {
            position += seatAdjust;
        }
        if ( bOnWheel ) {
            position = wheelTurn.transformPoint( position );
        }
        out = seatInverse.transformPoint( position );
        return true;
    };
    auto bone = [&]( const char *pName ) { return skeleton.findBone( EdenNameHash4( pName ) ); };
    Vector3f target;
    if ( goal( bDriver ? "BACK_LR_L" : "BACK_LR_R", bDriver ? CarIKOffsets::BackL : CarIKOffsets::BackR, true,
               target ) ) {
        SolveAimIK( skeleton, locals, bone( "Spine" ), bone( "IK_Back" ), target );
    }
    if ( bDriver ) {
        if ( goal( "HAND_LR_R", CarIKOffsets::HandR, false, target, true ) ) {
            Vector3f lever;
            if ( shiftBlend > 0.0f && goal( "GEAR_LR", CarIKOffsets::Gear, false, lever ) ) {
                target = target + ( lever - target ) * shiftBlend + shiftLift * shiftBlend;
            }
            SolveTwoBoneIK( skeleton, locals, bone( "Arm_R" ), bone( "ForeArm_R" ), bone( "IK_Right_Hand" ), target,
                            bone( "Hand_R" ) );
        }
        if ( goal( "HAND_LR_L", CarIKOffsets::HandL, false, target, true ) ) {
            SolveTwoBoneIK( skeleton, locals, bone( "Arm_L" ), bone( "ForeArm_L" ), bone( "IK_Left_Hand" ), target,
                            bone( "Hand_L" ) );
        }
    }
    if ( goal( bDriver ? "FOOT_LR_R" : "PSGFOOT_LR_R", bDriver ? CarIKOffsets::FootR : CarIKOffsets::PsgFootR, false,
               target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "UpLeg_R" ), bone( "Leg_R" ), bone( "IK_Right_Foot" ), target,
                        bone( "Foot_R" ) );
    }
    if ( goal( bDriver ? "FOOT_LR_L" : "PSGFOOT_LR_L", bDriver ? CarIKOffsets::FootL : CarIKOffsets::PsgFootL, false,
               target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "UpLeg_L" ), bone( "Leg_L" ), bone( "IK_Left_Foot" ), target,
                        bone( "Foot_L" ) );
    }
    if ( facial ) {
        facial( locals );
    }
    skeleton.computeSkinMatrices( locals, worldBones, skin );
    world = carWorld * seatMatrix;
    bVisible = true;
    static int32_t logSeat = CmdLine::HasArg( "-logseat" ) ? 2 : 0;
    if ( logSeat > 0 ) {
        logSeat--;
        const Vector3f p = world.getTranslation();
        OTDU_LOG_INFO( "SEAT %s car %d IKOffsetDB %d seat (%.3f %.3f) root (%.3f %.3f %.3f)\n",
                       bDriver ? "driver" : "passenger", offsetsCar, offsets.bValid ? 1 : 0, seatAdjust.y,
                       seatAdjust.z, p.x, p.y, p.z );
    }
}

Vector3f SeatedAvatar::getHeadPosition() const {
    const int32_t head = avatar.isLoaded() ? avatar.getSkeleton().findBone( EdenNameHash4( "Head" ) ) : -1;
    if ( head < 0 || static_cast<size_t>( head ) >= worldBones.size() ) {
        return world.transformPoint( Vector3f( 0.0f, 0.8f, 0.0f ) );
    }
    return world.transformPoint( worldBones[head].getTranslation() );
}

void SeatedAvatar::computeLighting( const WeatherPreset *pWeather, const EdenSceneParams &sceneParams ) {
    // IntroCinematic::computeAvatarLighting without scene lights (FUN_00a4da90
    // finds no Entity__Light outdoors): sun light, shadow influence; the in-car
    // factor of FUN_00b87aa0 (m) is [ENC]: 1 as out of a car [INF].
    auto getFloat = [pWeather]( const char *pName, float value ) {
        return pWeather != nullptr ? pWeather->getFloat( pName, value ) : value;
    };
    const Vector3f root = world.getTranslation();
    lighting.Shadow = getFloat( "avatar_shadow_influence", 1.0f );
    lighting.SpotAmbient = lighting.SpotDiffuse = lighting.SpotSpecular = Vector3f();
    lighting.SpotPosition = root + Vector3f( 0.0f, 1.0f, 0.0f );
    const float v = Clamp( 1.0f, getFloat( "avatar_lum_min", 0.0f ), getFloat( "avatar_lum_max", 1.0f ) );
    lighting.Ambient = sceneParams.Ambient * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_ambiant_factor", 1.0f ) );
    lighting.Diffuse = sceneParams.SunDiffuse * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_diffuse_factor", 1.0f ) );
    lighting.Specular =
        sceneParams.SunSpecular * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_specular_factor", 1.0f ) );
    lighting.Direction = sceneParams.SunDirection.normalized();
    lighting.Environment = getFloat( "avatar_car_envmap_factor", 0.7f ) *
                           ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_car_night_reflection_factor", 0.0f ) );
}

void SeatedAvatar::draw( EdenRenderer &renderer, bool bDrawHead ) const {
    if ( bVisible ) {
        avatar.draw( renderer, world, skin, &lighting, bDrawHead );
    }
}
