#pragma once

#include "render/eden/eden_scene_view.h"

// In-game car camera (docs: /tmp/claude-0/spec_freeride.md 3), parameters
// from Euro/Bnk/Database/CamerasInGame.xmb (the exe loads
// "%sDataBase/CamerasInGame.xmb" and holds every field name): per car
// (id_car = DBCarPhysicsData _camera) a list of cameras by mode; the
// IA_CarCamera cycle is p_array_camera_switch {21, 20, 23, 24, 22}. Mode 20
// is the chase camera behind the car (the California has 20, 23, 24 and
// their back views 40, 43, 44). The update math of the original is not
// identified [INF]: offsets along the flattened car heading, lerped with
// the speed between follow_speed_offset and follow_speed_max_offset (km/h),
// heading followed with a time constant follow_delay / 60 s, fov lerp
// fov_min -> fov_max over fov_speed_min -> fov_speed_max.
class ChaseCamera {
public:
    struct Params {
        int32_t Mode = 20;
        float FovDeg = 45.0f;
        bool bFovLerp = true;
        float FovMin = 45.0f, FovMax = 65.0f;
        float FovSpeedMin = 0.0f, FovSpeedMax = 150.0f;
        float Near = 0.8f, Far = 40000.0f;
        float PosOffset = -6.45f, PosOffsetH = 1.5f;
        float TargOffset = 1.5f, TargOffsetH = 0.91f;
        float SpeedOffset = 50.0f;
        float PosMaxOffset = -6.4f, PosMaxOffsetH = 1.6f;
        float TargMaxOffset = 1.6f, TargMaxOffsetH = 1.3f;
        float SpeedMaxOffset = 250.0f;
        float DelayOffset = 12.0f, DelayMaxOffset = 15.0f;
        float LeanFactorOffset = 1.6348f, LeanFactorMaxOffset = 0.4f;
        // fixed_pos / fixed_targ (car frame, x left): inside views (23 driver, 24 bumper, near 0.05).
        Vector3f FixedPos = Vector3f( 0.0f, 1.1f, 0.0f );
        Vector3f FixedTarget = Vector3f( 0.0f, 0.5f, 1.0f );
        // Speed / rpm shake (CamerasInGame.xmb shake_*, the retail sense of
        // speed). The magnitudes are the data's (centimetres [INF]); the periods
        // are the data's 150 ms [INF: the retail unit is not readable]. The
        // shake starts at shake_speed_slow and grows to shake_speed_fast, with a
        // second ramp shake_speed_max -> shake_speed_max2 and an rpm ramp from
        // shake_rpm_purcent % of the red line. shake_on_target offsets the aim
        // point instead of the eye.
        float ShakeSpeedSlow = 0.0f, ShakeSpeedFast = 150.0f;
        float ShakeRpmPercent = 70.0f;
        float ShakeSpeedMax = 150.0f, ShakeSpeedMax2 = 220.0f;
        float ShakeXMagnSlow = 0.1f, ShakeXPeriodSlow = 150.0f;
        float ShakeYMagnSlow = 1.0f, ShakeYPeriodSlow = 150.0f;
        float ShakeXMagnFast = 0.0f, ShakeXPeriodFast = 150.0f;
        float ShakeYMagnFast = 0.8f, ShakeYPeriodFast = 150.0f;
        bool bShakeOnTarget = false;
        // Speed radial blur (CameraInGame blur_int_speed_slow/fast, blur_int_slow/
        // fast, 0..255). 0 speed = no blur; the intensity grows with the speed
        // and is applied by the final scene blit (EdenSceneView::setSpeedBlur).
        float BlurSpeedSlow = 0.0f, BlurSpeedFast = 150.0f;
        float BlurIntSlow = 20.0f, BlurIntFast = 175.0f;
    };

public:
    // carCameraId: _camera of the car; modes: list in the cycle order.
    bool load( int32_t carCameraId );
    inline const std::vector<Params> &getCameras() const { return cameras; }
    // Cycles through p_array_camera_switch (only the modes of the car).
    void nextMode();
    inline int32_t getMode() const { return current < cameras.size() ? cameras[current].Mode : 20; }
    // Picks one mode of the car (a -cameramode test start): false when the car
    // has no such view.
    bool setMode( int32_t mode );
    // The camera sits inside the car (the fixed-pose views, near < 0.5) - the
    // windscreen droplets show from here.
    inline bool isInsideView() const { return bInsideView; }
    // Inside AND behind the glass (the driver / rear views). The bumper /
    // bonnet views are inside views too, but their pose is ahead of the
    // windscreen: the screen-space rain-on-glass composite must not fog them
    // (the owner's "inside cam and bonnet cam have this streak like filter").
    inline bool isBehindGlass() const { return bBehindGlass; }

    // Free look (the game's AVT_LOOK_* actions, which are the right stick —
    // see docs/STATE.md issue 13, "the in-car look"). Orbits the chase camera
    // around the car and turns the head in the inside views (23 / 24). Call
    // every frame: the offsets spring back to centre while the stick is
    // released, which is the retail behaviour. bBackView is the game's own
    // CAR_BACKVIEW (pad L1 / LB): a full look behind while it is held.
    void setLookInput( float stickX, float stickY, bool bBackView );
    // Radians, after smoothing (the tests read these).
    inline float getLookYaw() const { return lookYaw; }
    inline float getLookPitch() const { return lookPitch; }

    void reset( const Vector3f &carPosition, const Vector3f &carForward );
    // carWorld: car model origin -> world; ground: height under a point.
    // rpm / redLine drive the retail shake_rpm_purcent ramp (0 = no rpm shake).
    EdenSceneView::Camera update( float deltaTime, const Matrix4x4f &carWorld, float speedKmh,
                                  const std::function<bool( float x, float z, float &outHeight )> &ground,
                                  float rpm = 0.0f, float redLine = 8000.0f );
    // 0..1 speed blur of the last update, from the camera's blur_int_* data;
    // feed it to EdenSceneView::setSpeedBlur.
    inline float getSpeedBlur() const { return lastSpeedBlur; }

    // Camera rotation (Eden convention: forward = conj(q) (0, 0, 1)) looking
    // from eye to target.
    static Quaternionf LookRotation( const Vector3f &eye, const Vector3f &target );

private:
    std::vector<Params> cameras; // modes of the car in the switch order
    size_t current = 0;
    Vector3f heading = Vector3f( 0.0f, 0.0f, 1.0f );
    // The car's up axis, followed with the heading's time constant: the chase
    // eye sits along it, so the camera rides a slope with the body.
    Vector3f up = Vector3f( 0.0f, 1.0f, 0.0f );
    bool bUpInitialized = false;
    bool bInitialized = false;
    bool bInsideView = false; // the last update() ran an inside view (near < 0.5)
    bool bBehindGlass = false; // ... and its pose is behind the car's glass
    // Free look: target (from the stick) and the smoothed value used by the
    // view. Radians; yaw turns about the car's up axis, pitch about its right.
    float lookYaw = 0.0f, lookPitch = 0.0f;
    float lookTargetYaw = 0.0f, lookTargetPitch = 0.0f;
    // The shake clock (seconds): the periods of CamerasInGame.xmb are applied
    // to it, so the shove is a function of time and not of the frame rate.
    float shakeTime = 0.0f;
    float lastSpeedBlur = 0.0f;
};
