#include "shared.h"
#include "photo_camera.h"

#include "filesystem/gs_file.h"
#include "game/freeride/chase_camera.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "tdu_instance.h"

#include <SDL3/SDL.h>
#include <filesystem>

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kMaxPitch = 89.0f * kPi / 180.0f;
// Fly speeds: the base fly speed and the shift multiplier [INF: a photo tool,
// tuned so the car can be framed from 50 m in about four seconds].
constexpr float kMoveSpeed = 12.0f;
constexpr float kFastMultiplier = 2.0f;
// Look: the stick turns at this rate and the mouse this much per pixel (the
// world viewer's sensitivity).
constexpr float kLookSpeed = 2.0f;
constexpr float kLookSensitivity = 0.0025f;
// Field of view of the lens (retail photo mode zooms through the whole range).
constexpr float kFovMin = 25.0f * kPi / 180.0f;
constexpr float kFovMax = 90.0f * kPi / 180.0f;
constexpr float kFovStep = 1.15f;
// Shutter flash: white out, then fade.
constexpr float kFlashTime = 0.35f;
} // namespace

void PhotoCamera::open( const Vector3f &carPosition, const EdenSceneView::Camera &carCamera ) {
    position = carCamera.Position;
    // The car camera's view direction -> yaw / pitch (Eden convention:
    // EdenRotationFromQuaternion(q) maps (0,0,1) to the view direction).
    const Vector3f forward = EdenRotationFromQuaternion( carCamera.Rotation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    yaw = atan2f( forward.x, forward.z );
    pitch = Clamp( asinf( Clamp( forward.y, -1.0f, 1.0f ) ), -kMaxPitch, kMaxPitch );
    fov = Clamp( carCamera.Fov, kFovMin, kFovMax );
    flashAlpha = 0.0f;
    lastPhotoPath.clear();
    // <userdata>/photos: one PNG per shutter press, numbered across sessions.
    photoFolder.clear();
    photoCount = 0;
    if ( gpFile != nullptr ) {
        const std::filesystem::path folder = gpFile->getUserDataFolder() / "photos";
        std::error_code error;
        std::filesystem::create_directories( folder, error );
        photoFolder = folder.string();
        for ( const auto &entry : std::filesystem::directory_iterator( folder, error ) ) {
            const std::string name = entry.path().filename().string();
            if ( name.compare( 0, 6, "photo_" ) == 0 && entry.path().extension() == ".png" ) {
                photoCount = Max( photoCount, atoi( name.c_str() + 6 ) );
            }
        }
    }
    bOpen = true;
    bLooking = false;
    bCaptured = false;
    prompt = "A / Enter: take the photo     B: back     (right stick / mouse: look)";
    OTDU_LOG_INFO( "PhotoCamera: open at (%.1f, %.1f, %.1f), photos in '%s' (%d so far), car (%.0f, %.0f)\n",
                   position.x, position.y, position.z, photoFolder.c_str(), photoCount, carPosition.x, carPosition.z );
}

void PhotoCamera::close() {
    setRelativeMouse( false );
    bOpen = false;
    prompt.clear();
}

void PhotoCamera::setRelativeMouse( bool bEnable ) {
    if ( bEnable == bRelativeMouse || gpTestDriveInstance == nullptr || gpTestDriveInstance->getWindow() == nullptr ) {
        return;
    }
    bRelativeMouse = bEnable;
    SDL_SetWindowRelativeMouseMode( gpTestDriveInstance->getWindow(), bEnable );
}

bool PhotoCamera::tick( float deltaTime, const Vector3f &carPosition ) {
    if ( !bOpen ) {
        return false;
    }
    // The previous frame's capture is done: the UI comes back.
    bCapturing = false;
    flashAlpha = Max( 0.0f, flashAlpha - deltaTime / kFlashTime );
    // Back: B / circle, Esc / START.
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        return true;
    }
    // Look: the right stick, or the mouse while a button is held (Tab keeps it
    // captured, like the world viewer).
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_TAB ) ) {
        bCaptured = !bCaptured;
    }
    bLooking = gpInput->isMouseButtonDown( SDL_BUTTON_RIGHT ) || gpInput->isMouseButtonDown( SDL_BUTTON_LEFT );
    setRelativeMouse( bLooking || bCaptured );
    // Eden yaw grows towards +x, the screen's left: stick right turns right
    // (it was added, so left / right were swapped - the owner, 2026-11-05).
    yaw -= gpInput->getRightStickX() * kLookSpeed * deltaTime;
    pitch = Clamp( pitch - gpInput->getRightStickY() * kLookSpeed * deltaTime, -kMaxPitch, kMaxPitch );
    if ( bLooking || bCaptured ) {
        yaw -= gpInput->getMouseDeltaX() * kLookSensitivity;
        pitch = Clamp( pitch - gpInput->getMouseDeltaY() * kLookSensitivity, -kMaxPitch, kMaxPitch );
    }
    if ( yaw > kPi ) {
        yaw -= 2.0f * kPi;
    } else if ( yaw < -kPi ) {
        yaw += 2.0f * kPi;
    }
    // Move: the left stick, WASD / arrows; LT / RT (Q / E) go down / up. The raw
    // keys cover a keyboard without the retail key map (the pad never needs
    // them: its sticks are read directly).
    auto key = []( SDL_Scancode a, SDL_Scancode b ) { return gpInput->isKeyDown( a ) || gpInput->isKeyDown( b ); };
    float moveX = gpInput->getLeftStickX();
    float moveY = gpInput->getLeftStickY(); // up = -1
    moveY += ( key( SDL_SCANCODE_S, SDL_SCANCODE_DOWN ) ? 1.0f : 0.0f ) -
             ( key( SDL_SCANCODE_W, SDL_SCANCODE_UP ) ? 1.0f : 0.0f );
    moveX += ( key( SDL_SCANCODE_D, SDL_SCANCODE_RIGHT ) ? 1.0f : 0.0f ) -
             ( key( SDL_SCANCODE_A, SDL_SCANCODE_LEFT ) ? 1.0f : 0.0f );
    const float up = gpInput->getAnalog( eInputAction::IA_CarAccel ) -
                     gpInput->getAnalog( eInputAction::IA_CarBrake ) +
                     ( key( SDL_SCANCODE_E, SDL_SCANCODE_SPACE ) ? 1.0f : 0.0f ) -
                     ( key( SDL_SCANCODE_Q, SDL_SCANCODE_LCTRL ) ? 1.0f : 0.0f );
    const Vector3f forward( sinf( yaw ) * cosf( pitch ), sinf( pitch ), cosf( yaw ) * cosf( pitch ) );
    const Vector3f right( -forward.z, 0.0f, forward.x ); // Eden x points to the left of the screen
    const Vector3f move = forward * ( -moveY ) + right * moveX + Vector3f( 0.0f, up, 0.0f ); // stick / D right = right
    if ( move.length() > 1e-4f ) {
        const bool bFast = gpInput->isKeyDown( SDL_SCANCODE_LSHIFT ) || gpInput->isKeyDown( SDL_SCANCODE_RSHIFT );
        position = position + move.normalized() * ( kMoveSpeed * ( bFast ? kFastMultiplier : 1.0f ) * deltaTime );
    }
    // Zoom: LB / RB (the camera / back view buttons) and the wheel.
    if ( gpInput->wasPressed( eInputAction::IA_CarCamera ) ) {
        fov = Clamp( fov / kFovStep, kFovMin, kFovMax );
    }
    if ( gpInput->wasPressed( eInputAction::IA_CarBackView ) ) {
        fov = Clamp( fov * kFovStep, kFovMin, kFovMax );
    }
    const float wheel = gpInput->getMouseWheel();
    if ( wheel > 0.0f ) {
        fov = Clamp( fov / std::pow( kFovStep, wheel ), kFovMin, kFovMax );
    } else if ( wheel < 0.0f ) {
        fov = Clamp( fov * std::pow( kFovStep, -wheel ), kFovMin, kFovMax );
    }
    // Shutter: A / Enter.
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        takePhoto();
    }
    // The prompt shows how many photos are in the folder and the lens.
    prompt = "A / Enter: take the photo     B: back     (right stick / mouse: look, LB / RB: zoom)     " +
             std::to_string( photoCount ) + " photos, " +
             std::to_string( static_cast<int32_t>( fov * 180.0f / kPi ) ) + " deg";
    (void)carPosition; // the camera is free: the car is only the starting point
    return false;
}

void PhotoCamera::takePhoto() {
    if ( photoFolder.empty() || gpRender == nullptr ) {
        OTDU_LOG_WARN( "PhotoCamera: no photo folder (no -userdata?)\n" );
        return;
    }
    photoCount++;
    char name[32];
    snprintf( name, sizeof( name ), "photo_%03d.png", photoCount );
    lastPhotoPath = photoFolder + "/" + name;
    // The device captures the swapchain at the start of the next frame (the
    // frame the player framed is the one on screen now).
    gpRender->getDevice().saveScreenshot( lastPhotoPath.c_str() );
    flashAlpha = 1.0f;
    bCapturing = true; // the caller keeps the controls bar / flash out of this frame
    OTDU_LOG_INFO( "PhotoCamera: photo %d -> '%s'\n", photoCount, lastPhotoPath.c_str() );
}

void PhotoCamera::getCamera( EdenSceneView::Camera &outCamera ) const {
    const Vector3f forward( sinf( yaw ) * cosf( pitch ), sinf( pitch ), cosf( yaw ) * cosf( pitch ) );
    outCamera.Position = position;
    outCamera.Rotation = ChaseCamera::LookRotation( position, position + forward * 10.0f );
    outCamera.Fov = fov;
    outCamera.Near = 0.5f;
    outCamera.Far = 40000.0f;
}
