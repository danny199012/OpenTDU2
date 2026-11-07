#pragma once

#include "render/eden/eden_scene_view.h"

// "Take photo" of the pause screen: the movie's free-roam entry list carries
// `photo`, pushed with /:LockSnapshot (retail m_TakeSnapshot_, see
// docs/STATE.md and the pause_menu note). It is a free camera around the car:
//
// Controls — pad (DualSense) / keyboard:
//   left stick, WASD / arrows      move along the view (forward, strafe)
//   right stick, mouse (held or Tab) look around
//   LT / RT, Q / E                 move down / up
//   LB / RB, Page Down / Page Up   zoom (field of view 25..90 deg)
//   wheel                          zoom
//   A / Enter                      take the photo (PNG + log line)
//   B / Backspace / Esc            back to the game
//
// The camera starts where the car camera is, so the first frame shows what the
// player sees; it never collides (a photo tool, like the world viewer).
class PhotoCamera {
public:
    // Places the camera where the car camera is (the first frame shows what the
    // player sees) and prepares <userdata>/photos (created if needed).
    void open( const Vector3f &carPosition, const EdenSceneView::Camera &carCamera );
    void close();
    inline bool isOpen() const { return bOpen; }

    // Fly + shutter. Returns true when the player asked to go back (the caller
    // closes it).
    bool tick( float deltaTime, const Vector3f &carPosition );
    void getCamera( EdenSceneView::Camera &outCamera ) const;

    inline const std::string &getPrompt() const { return prompt; }
    // White shutter flash, 1 -> 0 after a photo.
    inline float getFlashAlpha() const { return flashAlpha; }
    // True for the frame the photo is taken from: the device captures the
    // swapchain at the end of it, so the caller must not draw the controls bar
    // or the flash into it.
    inline bool isCapturing() const { return bCapturing; }
    inline int32_t getPhotoCount() const { return photoCount; }
    inline const std::string &getLastPhotoPath() const { return lastPhotoPath; }

private:
    void takePhoto();
    void setRelativeMouse( bool bEnable );

    Vector3f position;
    float yaw = 0.0f;              // forward = (sin yaw cos pitch, sin pitch, cos yaw cos pitch)
    float pitch = 0.0f;
    float fov = 60.0f * kDegToRad; // vertical, kFovMin..kFovMax
    bool bOpen = false;
    bool bLooking = false;         // the mouse button held: relative mouse
    bool bCaptured = false;        // Tab: the mouse is captured
    bool bRelativeMouse = false;
    float flashAlpha = 0.0f;
    bool bCapturing = false; // the current frame is the one being captured
    int32_t photoCount = 0;
    std::string photoFolder;
    std::string lastPhotoPath;
    std::string prompt;
};
