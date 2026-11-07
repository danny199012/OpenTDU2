#include "shared.h"
#include "chase_camera.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

namespace {
// Rotation matrix (columns x, y, z) -> quaternion.
Quaternionf QuaternionFromBasis( const Vector3f &x, const Vector3f &y, const Vector3f &z ) {
    const float m00 = x.x, m11 = y.y, m22 = z.z;
    const float trace = m00 + m11 + m22;
    Quaternionf q;
    if ( trace > 0.0f ) {
        const float s = std::sqrt( trace + 1.0f ) * 2.0f;
        q = Quaternionf( ( y.z - z.y ) / s, ( z.x - x.z ) / s, ( x.y - y.x ) / s, 0.25f * s );
    } else if ( m00 > m11 && m00 > m22 ) {
        const float s = std::sqrt( 1.0f + m00 - m11 - m22 ) * 2.0f;
        q = Quaternionf( 0.25f * s, ( y.x + x.y ) / s, ( z.x + x.z ) / s, ( y.z - z.y ) / s );
    } else if ( m11 > m22 ) {
        const float s = std::sqrt( 1.0f + m11 - m00 - m22 ) * 2.0f;
        q = Quaternionf( ( y.x + x.y ) / s, 0.25f * s, ( z.y + y.z ) / s, ( z.x - x.z ) / s );
    } else {
        const float s = std::sqrt( 1.0f + m22 - m00 - m11 ) * 2.0f;
        q = Quaternionf( ( z.x + x.z ) / s, ( z.y + y.z ) / s, 0.25f * s, ( x.y - y.x ) / s );
    }
    return q;
}

float Lerp1( float a, float b, float t ) { return a + ( b - a ) * t; }

// Free look [INF: the retail look speeds are not identified; these are chosen
// to match the pad feel]. The stick range maps to this yaw / pitch and the
// offsets spring back to centre with this time constant when it is released.
constexpr float kLookYawMax = 2.6f;       // ~149 deg each way
constexpr float kLookPitchMax = 0.5f;     // ~29 deg
constexpr float kLookSpringDelay = 0.12f; // s
constexpr float kLookPitchHeight = 3.0f;  // m the aim point rises at full pitch
} // namespace

Quaternionf ChaseCamera::LookRotation( const Vector3f &eye, const Vector3f &target ) {
    // Eden camera: EdenRotationFromQuaternion(q) maps (0, 0, 1) to the view
    // direction, x to the left of the screen (cameraWorld = T R diag(-1, 1,
    // -1), EdenSceneView::setupScene).
    Vector3f z = ( target - eye ).normalized();
    if ( z.length() < 0.5f ) {
        z = Vector3f( 0.0f, 0.0f, 1.0f );
    }
    Vector3f x = Vector3f( 0.0f, 1.0f, 0.0f ).cross( z ).normalized();
    if ( x.length() < 0.5f ) {
        x = Vector3f( 1.0f, 0.0f, 0.0f );
    }
    const Vector3f y = z.cross( x );
    const Quaternionf r = QuaternionFromBasis( x, y, z );
    return Quaternionf( -r.x, -r.y, -r.z, r.w );
}

bool ChaseCamera::load( int32_t carCameraId ) {
    cameras.clear();
    current = 0;
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( ( gpFile->getBankFolder() + "database/camerasingame.xmb" ).c_str(), data ) ) {
        OTDU_LOG_WARN( "ChaseCamera: database/camerasingame.xmb not found\n" );
        cameras.push_back( Params() );
        return false;
    }
    XmbDocument document;
    if ( !document.load( data ) ) {
        cameras.push_back( Params() );
        return false;
    }
    XmbValue root = document.getRoot()["CameraInGame"];
    // The cycle is stored as p_array_camera_switch[0..n-1] with an
    // nb_camera_switch count, NOT as an XMB array node: camerasingame.xmb's
    // string table carries the indexed names. Reading it as an array
    // (getArraySize("p_array_camera_switch")) returns 0, which left every car
    // with a single camera and made the camera button do nothing.
    std::vector<int32_t> cycle;
    const int32_t numSwitch = static_cast<int32_t>( root["nb_camera_switch"].asInt( 0 ) );
    for ( int32_t i = 0; i < numSwitch; i++ ) {
        XmbValue mode = root[( "p_array_camera_switch[" + std::to_string( i ) + "]" ).c_str()];
        if ( mode.isValid() ) {
            cycle.push_back( static_cast<int32_t>( mode.asInt( 0 ) ) );
        }
    }
    const int32_t numVehicles = static_cast<int32_t>( root["nb_camera_vehicles"].asInt( 0 ) );
    std::vector<Params> carCameras;
    for ( int32_t v = 0; v < numVehicles + 1 && carCameras.empty(); v++ ) {
        XmbValue car = root[( "Car " + std::to_string( v ) ).c_str()];
        if ( !car.isValid() || car["id_car"].asInt( -1 ) != carCameraId ) {
            continue;
        }
        const int32_t numCameras = static_cast<int32_t>( car["nb_cameras"].asInt( 0 ) );
        for ( int32_t c = 0; c < numCameras; c++ ) {
            XmbValue camera = car[( "Camera " + std::to_string( c ) ).c_str()];
            if ( !camera.isValid() ) {
                continue;
            }
            Params p;
            p.Mode = static_cast<int32_t>( camera["mode"].asInt( 20 ) );
            p.FovDeg = camera["fov_deg"].asFloat( p.FovDeg );
            p.bFovLerp = camera["fov_is_lerp"].asInt( 1 ) != 0;
            p.FovMin = camera["fov_min"].asFloat( p.FovMin );
            p.FovMax = camera["fov_max"].asFloat( p.FovMax );
            p.FovSpeedMin = camera["fov_speed_min"].asFloat( p.FovSpeedMin );
            p.FovSpeedMax = camera["fov_speed_max"].asFloat( p.FovSpeedMax );
            p.Near = camera["nearProjPlane"].asFloat( p.Near );
            p.Far = camera["farProjPlane"].asFloat( p.Far );
            p.PosOffset = camera["follow_pos_offset"].asFloat( p.PosOffset );
            p.PosOffsetH = camera["follow_pos_offset_H"].asFloat( p.PosOffsetH );
            p.TargOffset = camera["follow_targ_offset"].asFloat( p.TargOffset );
            p.TargOffsetH = camera["follow_targ_offset_H"].asFloat( p.TargOffsetH );
            p.SpeedOffset = camera["follow_speed_offset"].asFloat( p.SpeedOffset );
            p.PosMaxOffset = camera["follow_pos_max_offset"].asFloat( p.PosMaxOffset );
            p.PosMaxOffsetH = camera["follow_pos_max_offset_h"].asFloat( p.PosMaxOffsetH );
            p.TargMaxOffset = camera["follow_targ_max_offset"].asFloat( p.TargMaxOffset );
            p.TargMaxOffsetH = camera["follow_targ_max_offset_h"].asFloat( p.TargMaxOffsetH );
            p.SpeedMaxOffset = camera["follow_speed_max_offset"].asFloat( p.SpeedMaxOffset );
            p.DelayOffset = camera["follow_delay_offset"].asFloat( p.DelayOffset );
            p.DelayMaxOffset = camera["follow_delay_max_offset"].asFloat( p.DelayMaxOffset );
            p.LeanFactorOffset = camera["follow_lean_factor_offset"].asFloat( p.LeanFactorOffset );
            p.LeanFactorMaxOffset = camera["follow_lean_factor_max_offset"].asFloat( p.LeanFactorMaxOffset );
            p.FixedPos = Vector3f( camera["fixed_pos_x"].asFloat( 0.0f ), camera["fixed_pos_y"].asFloat( 1.1f ),
                                   camera["fixed_pos_z"].asFloat( 0.0f ) );
            p.FixedTarget = Vector3f( camera["fixed_targ_x"].asFloat( 0.0f ), camera["fixed_targ_y"].asFloat( 0.5f ),
                                      camera["fixed_targ_z"].asFloat( 1.0f ) );
            // Speed / rpm shake of the camera (the retail sense of speed).
            p.ShakeSpeedSlow = camera["shake_speed_slow"].asFloat( p.ShakeSpeedSlow );
            p.ShakeSpeedFast = camera["shake_speed_fast"].asFloat( p.ShakeSpeedFast );
            p.ShakeRpmPercent = camera["shake_rpm_purcent"].asFloat( p.ShakeRpmPercent );
            p.ShakeSpeedMax = camera["shake_speed_max"].asFloat( p.ShakeSpeedMax );
            p.ShakeSpeedMax2 = camera["shake_speed_max2"].asFloat( p.ShakeSpeedMax2 );
            p.ShakeXMagnSlow = camera["shake_x_magn_slow"].asFloat( p.ShakeXMagnSlow );
            p.ShakeXPeriodSlow = camera["shake_x_period_slow"].asFloat( p.ShakeXPeriodSlow );
            p.ShakeYMagnSlow = camera["shake_y_magn_slow"].asFloat( p.ShakeYMagnSlow );
            p.ShakeYPeriodSlow = camera["shake_y_period_slow"].asFloat( p.ShakeYPeriodSlow );
            p.ShakeXMagnFast = camera["shake_x_magn_fast"].asFloat( p.ShakeXMagnFast );
            p.ShakeXPeriodFast = camera["shake_x_period_fast"].asFloat( p.ShakeXPeriodFast );
            p.ShakeYMagnFast = camera["shake_y_magn_fast"].asFloat( p.ShakeYMagnFast );
            p.ShakeYPeriodFast = camera["shake_y_period_fast"].asFloat( p.ShakeYPeriodFast );
            p.bShakeOnTarget = camera["shake_on_target"].asInt( 0 ) != 0;
            // Speed radial blur (the final blit applies it).
            p.BlurSpeedSlow = camera["blur_int_speed_slow"].asFloat( p.BlurSpeedSlow );
            p.BlurSpeedFast = camera["blur_int_speed_fast"].asFloat( p.BlurSpeedFast );
            p.BlurIntSlow = camera["blur_int_slow"].asFloat( p.BlurIntSlow );
            p.BlurIntFast = camera["blur_int_fast"].asFloat( p.BlurIntFast );
            carCameras.push_back( p );
        }
    }
    // Switch order, modes of the car only.
    for ( int32_t mode : cycle ) {
        for ( const Params &p : carCameras ) {
            if ( p.Mode == mode ) {
                cameras.push_back( p );
                break;
            }
        }
    }
    if ( cameras.empty() ) {
        // No cycle (or none of its modes belongs to this car): fall back to the
        // car's own cameras in file order, so the button still cycles.
        cameras = carCameras;
    }
    if ( cameras.empty() ) {
        cameras.push_back( Params() );
    }
    // -logcameras: the modes the car actually has and the cycle we matched them
    // against (a mismatch here is why the camera button can appear dead).
    if ( CmdLine::HasArg( "-logcameras" ) ) {
        std::string carModes, cycleModes;
        for ( const Params &p : carCameras ) {
            carModes += " " + std::to_string( p.Mode );
        }
        for ( int32_t m : cycle ) {
            cycleModes += " " + std::to_string( m );
        }
        OTDU_LOG_INFO( "ChaseCamera: car %d modes:%s | p_array_camera_switch:%s\n", carCameraId, carModes.c_str(),
                       cycleModes.c_str() );
    }
    OTDU_LOG_INFO( "ChaseCamera: car camera %d: %zu cameras of the car, %zu in the switch cycle (first mode %d, fov %.0f, "
                   "pos %.2f / %.2f, target %.2f / %.2f)\n",
                   carCameraId, carCameras.size(), cameras.size(), cameras[0].Mode, cameras[0].FovDeg,
                   cameras[0].PosOffset, cameras[0].PosOffsetH, cameras[0].TargOffset, cameras[0].TargOffsetH );
    return !carCameras.empty();
}

void ChaseCamera::nextMode() {
    if ( !cameras.empty() ) {
        current = ( current + 1 ) % cameras.size();
        if ( CmdLine::HasArg( "-logcameras" ) ) {
            OTDU_LOG_INFO( "ChaseCamera: camera mode %d (of %zu, near %.2f)\n", cameras[current].Mode,
                           cameras.size(), cameras[current].Near );
        }
    }
}

bool ChaseCamera::setMode( int32_t mode ) {
    for ( size_t i = 0; i < cameras.size(); i++ ) {
        if ( cameras[i].Mode == mode ) {
            current = i;
            if ( CmdLine::HasArg( "-logcameras" ) ) {
                OTDU_LOG_INFO( "ChaseCamera: camera mode %d picked (-cameramode)\n", mode );
            }
            return true;
        }
    }
    return false;
}

void ChaseCamera::setLookInput( float stickX, float stickY, bool bBackView ) {
    if ( bBackView ) {
        // CAR_BACKVIEW (pad L1 / LB): a full look behind while held.
        lookTargetYaw = kPI;
        lookTargetPitch = 0.0f;
        return;
    }
    // The right stick: x right = look right, y up (SDL reports -1) = look up.
    // OpenTDU2's camera frame puts screen right at -X for forward +Z
    // (EdenSceneView: right-handed), so turning right increases the yaw.
    lookTargetYaw = Clamp( stickX, -1.0f, 1.0f ) * kLookYawMax;
    lookTargetPitch = -Clamp( stickY, -1.0f, 1.0f ) * kLookPitchMax;
}

void ChaseCamera::reset( const Vector3f &carPosition, const Vector3f &carForward ) {
    (void)carPosition;
    heading = Vector3f( carForward.x, 0.0f, carForward.z ).normalized();
    bInitialized = heading.length() > 0.5f;
    // A teleport / respawn re-seeds the pitch follow too (the next update
    // writes the car's real up).
    bUpInitialized = false;
}

EdenSceneView::Camera ChaseCamera::update( float deltaTime, const Matrix4x4f &carWorld, float speedKmh,
                                           const std::function<bool( float x, float z, float &outHeight )> &ground,
                                           float rpm, float redLine ) {
    const Params &p = cameras.empty() ? Params() : cameras[current];
    const Vector3f carPosition = carWorld.getTranslation();
    const Vector3f carForward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    Vector3f flat = Vector3f( carForward.x, 0.0f, carForward.z ).normalized();
    if ( flat.length() < 0.5f ) {
        flat = heading;
    }
    const float kmh = std::abs( speedKmh );
    // Speed radial blur of the final image (blur_int_* data): 0 at rest, the
    // full 0..1 strength at blur_int_speed_fast.
    const float blurT = p.BlurSpeedFast > p.BlurSpeedSlow
                            ? Clamp( ( kmh - p.BlurSpeedSlow ) / ( p.BlurSpeedFast - p.BlurSpeedSlow ), 0.0f, 1.0f )
                            : 0.0f;
    const float blurIntensity =
        Clamp( ( p.BlurIntSlow + ( p.BlurIntFast - p.BlurIntSlow ) * blurT ) / 255.0f, 0.0f, 1.0f );
    lastSpeedBlur = blurT * blurIntensity;
    const float t = Clamp( ( kmh - p.SpeedOffset ) / Max( p.SpeedMaxOffset - p.SpeedOffset, 1.0f ), 0.0f, 1.0f );
    if ( !bInitialized ) {
        heading = flat;
        bInitialized = true;
    }
    // Heading follow: time constant follow_delay / 60 s [INF].
    const float delay = Lerp1( p.DelayOffset, p.DelayMaxOffset, t ) / 60.0f;
    const float blend = delay > 0.0f ? 1.0f - std::exp( -deltaTime / delay ) : 1.0f;
    heading = ( heading + ( flat - heading ) * blend ).normalized();
    if ( heading.length() < 0.5f ) {
        heading = flat;
    }

    // Free look: the offsets spring back to centre while the stick is released.
    const float lookBlend = kLookSpringDelay > 0.0f ? 1.0f - std::exp( -deltaTime / kLookSpringDelay ) : 1.0f;
    lookYaw += ( lookTargetYaw - lookYaw ) * lookBlend;
    lookPitch += ( lookTargetPitch - lookPitch ) * lookBlend;
    // The chase camera orbits about the car's up axis by the look yaw (heading
    // is flat, so a plain XZ rotation is enough).
    Vector3f lookHeading = heading;
    if ( std::abs( lookYaw ) > 1e-4f ) {
        const float c = std::cos( lookYaw ), s = std::sin( lookYaw );
        lookHeading = Vector3f( heading.x * c - heading.z * s, 0.0f, heading.x * s + heading.z * c ).normalized();
        if ( lookHeading.length() < 0.5f ) {
            lookHeading = heading;
        }
    }

    EdenSceneView::Camera camera;
    camera.Near = p.Near;
    camera.Far = p.Far;
    float fov = p.FovDeg;
    if ( p.bFovLerp ) {
        const float ft = Clamp( ( kmh - p.FovSpeedMin ) / Max( p.FovSpeedMax - p.FovSpeedMin, 1.0f ), 0.0f, 1.0f );
        fov = Lerp1( p.FovMin, p.FovMax, ft );
    }
    camera.Fov = fov * kDegToRad;

    Vector3f eye, target;
    const bool bInside = p.Near < 0.5f; // inside views (near 0.05): fixed pose in the car frame
    bInsideView = bInside;
    // The windscreen effect belongs to the views whose eye sits behind the
    // glass. The bumper / bonnet pose is ahead of the windscreen (car +z
    // forward), so the screen-space droplets and their wet film would fog a
    // view that has no glass in front of it (the bonnet-cam streak report).
    bBehindGlass = bInside && p.FixedPos.z <= 0.5f;
    if ( bInside ) {
        // fixed_pos / fixed_targ (car frame, x left). The free look turns the
        // head: the aim point swings about the seat instead of the car.
        const Vector3f t = p.FixedTarget - p.FixedPos;
        const float c = std::cos( lookYaw ), s = std::sin( lookYaw );
        const Vector3f turned( t.x * c - t.z * s, t.y, t.x * s + t.z * c );
        eye = carWorld.transformPoint( p.FixedPos );
        target = carWorld.transformPoint( p.FixedPos + turned + Vector3f( 0.0f, lookPitch * kLookPitchHeight, 0.0f ) );
    } else {
        const float posOffset = Lerp1( p.PosOffset, p.PosMaxOffset, t );
        const float posH = Lerp1( p.PosOffsetH, p.PosMaxOffsetH, t );
        const float targOffset = Lerp1( p.TargOffset, p.TargMaxOffset, t );
        const float targH = Lerp1( p.TargOffsetH, p.TargMaxOffsetH, t );
        // The car's own up axis rather than world up: on a slope the chase
        // camera rides with the body, so climbing a hill no longer sinks it
        // below the rear deck (the owner's "the cam gets lower up hills"). The
        // pitch is followed with the same time constant as the heading, so a
        // bump does not shake the view.
        Vector3f carUp = carWorld.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) ).normalized();
        if ( carUp.length() < 0.5f ) {
            carUp = Vector3f( 0.0f, 1.0f, 0.0f );
        }
        if ( !bUpInitialized ) {
            up = carUp;
            bUpInitialized = true;
        }
        up = ( up + ( carUp - up ) * blend ).normalized();
        if ( up.length() < 0.5f ) {
            up = Vector3f( 0.0f, 1.0f, 0.0f );
        }
        // The back / forward offsets lie in the plane square to that up axis.
        // The horizontal heading with the tilted up made the eye sink whenever
        // the car pitched nose down (downhill, braking, a dip): 6.45 m back x
        // a 7 degree pitch took 0.78 m off the 1.5 m height, the view dropped
        // to the rear deck (the owner, 2026-11-05: "chase cam tends to sink
        // sometimes making it hard to see above car"; -logcameras: "eye above
        // car 0.72 m") [INF].
        Vector3f planeHeading = lookHeading - up * lookHeading.dot( up );
        planeHeading = planeHeading.length() > 0.1f ? planeHeading.normalized() : lookHeading;
        eye = carPosition + planeHeading * posOffset + up * posH;
        target = carPosition + planeHeading * targOffset +
                 up * ( targH + lookPitch * kLookPitchHeight );
        // Keep the eye above the ground [INF].
        float height = 0.0f;
        if ( ground && ground( eye.x, eye.z, height ) && eye.y < height + 0.5f ) {
            eye.y = height + 0.5f;
        }
        static const bool bLogCamera = CmdLine::HasArg( "-logcameras" );
        if ( bLogCamera && ( eye - carPosition ).dot( up ) < 1.2f ) {
            OTDU_LOG_INFO( "ChaseCamera: LOW eye above car %.2f (posH %.2f posOffset %.2f t %.2f), eye y %.2f car y %.2f "
                           "ground %.2f, up (%.2f %.2f %.2f), heading (%.2f %.2f %.2f)\n",
                           ( eye - carPosition ).dot( up ), posH, posOffset, t, eye.y, carPosition.y, height, up.x, up.y,
                           up.z, lookHeading.x, lookHeading.y, lookHeading.z );
        }
        if ( bLogCamera ) {
            static float logTimer = 0.0f;
            logTimer += deltaTime;
            if ( logTimer >= 1.0f ) {
                logTimer = 0.0f;
                OTDU_LOG_INFO( "ChaseCamera: mode %d eye above car %.2f m, ground %.2f, up (%.2f %.2f %.2f)\n", p.Mode,
                               ( eye - carPosition ).dot( up ), eye.y - height, up.x, up.y, up.z );
            }
        }
    }
    // Retail speed / rpm shake (CameraInGame shake_*): a small periodic shove
    // of the eye, growing from shake_speed_slow to shake_speed_fast (with the
    // second shake_speed_max -> shake_speed_max2 ramp) and from
    // shake_rpm_purcent % of the red line. The data's magnitudes are taken as
    // centimetres and its periods as milliseconds [INF: neither unit is in the
    // readable code; the values give a 1 cm / 0.15 s shove, which reads as the
    // retail high speed tremor]. Slip shake (slip_shake_*) is not applied yet.
    shakeTime += deltaTime;
    // Several cameras ship shake_speed_slow == shake_speed_fast == 0 (only the
    // max / max2 ramp is used); a zero-length ramp must be disabled, not
    // divided by the 1 m/s guard (that made the shake 1.0 at 1 km/h).
    const float speedShake =
        p.ShakeSpeedFast > p.ShakeSpeedSlow
            ? Clamp( ( kmh - p.ShakeSpeedSlow ) / ( p.ShakeSpeedFast - p.ShakeSpeedSlow ), 0.0f, 1.0f )
            : 0.0f;
    const float speedShake2 =
        p.ShakeSpeedMax2 > p.ShakeSpeedMax
            ? Clamp( ( kmh - p.ShakeSpeedMax ) / ( p.ShakeSpeedMax2 - p.ShakeSpeedMax ), 0.0f, 1.0f )
            : 0.0f;
    const float rpmShake =
        p.ShakeRpmPercent > 0.0f && redLine > 0.0f
            ? Clamp( ( rpm - redLine * p.ShakeRpmPercent * 0.01f ) / Max( redLine * 0.25f, 1.0f ), 0.0f, 1.0f )
            : 0.0f;
    const float shakeAmount = Max( Max( speedShake, speedShake2 ), rpmShake );
    if ( CmdLine::HasArg( "-logcameras" ) ) {
        static float shakeLogTimer = 0.0f;
        shakeLogTimer += deltaTime;
        if ( shakeLogTimer >= 1.0f ) {
            shakeLogTimer = 0.0f;
            OTDU_LOG_INFO( "ChaseCamera: shake %.2f, blur %.2f (speed %.0f km/h, rpm %.0f, red line %.0f)\n",
                           shakeAmount, lastSpeedBlur, kmh, rpm, redLine );
        }
    }
    if ( shakeAmount > 1e-4f ) {
        const float xMagn = Lerp1( p.ShakeXMagnSlow, p.ShakeXMagnFast, shakeAmount ) * 0.01f * shakeAmount;
        const float yMagn = Lerp1( p.ShakeYMagnSlow, p.ShakeYMagnFast, shakeAmount ) * 0.01f * shakeAmount;
        const float xPeriod = Max( p.ShakeXPeriodSlow, 1.0f ) * 0.001f;
        const float yPeriod = Max( p.ShakeYPeriodSlow, 1.0f ) * 0.001f;
        const float x = xMagn * std::sin( shakeTime * 2.0f * kPI / xPeriod );
        const float y = yMagn * std::sin( shakeTime * 2.0f * kPI / yPeriod );
        Vector3f shakeUp = carWorld.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) ).normalized();
        if ( shakeUp.length() < 0.5f ) {
            shakeUp = Vector3f( 0.0f, 1.0f, 0.0f );
        }
        Vector3f direction = ( target - eye ).normalized();
        if ( direction.length() < 0.5f ) {
            direction = Vector3f( 0.0f, 0.0f, 1.0f );
        }
        const Vector3f right = direction.cross( shakeUp ).normalized();
        const Vector3f shake = right * x + shakeUp * y;
        if ( p.bShakeOnTarget ) {
            target += shake;
        } else {
            eye += shake;
        }
    }

    camera.Position = eye;
    camera.Rotation = LookRotation( eye, target );
    return camera;
}
