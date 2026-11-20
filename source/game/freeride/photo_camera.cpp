#include "shared.h"
#include "photo_camera.h"

#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "render/eden/eden_postfx.h"
#include "render/renderer_2d.h"
#include "game/freeride/chase_camera.h"
#include "input/gs_input.h"
#include "system/cmd_line.h"
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
    // -photoopt bloom,dofInt,dofDist,contrast,red,green,blue (0..1): preset Options values (test hook).
    if ( const char *pOpt = CmdLine::GetArgValue( "-photoopt" ) ) {
        sscanf( pOpt, "%f,%f,%f,%f,%f,%f,%f", &optionValue[0], &optionValue[1], &optionValue[2], &optionValue[3],
                &optionValue[4], &optionValue[5], &optionValue[6] );
    }
    bOpen = true;
    bLooking = false;
    bCaptured = false;
    prompt = "A / Enter: take the photo     B: back     (right stick / mouse: look)";
    OTDU_LOG_INFO( "PhotoCamera: open at (%.1f, %.1f, %.1f), photos in '%s' (%d so far), car (%.0f, %.0f)\n",
                   position.x, position.y, position.z, photoFolder.c_str(), photoCount, carPosition.x, carPosition.z );
}

void PhotoCamera::openAt( const Vector3f &carPosition, const Vector3f &eye, float yawDegrees, float pitchDegrees,
                          float fovDegrees ) {
    EdenSceneView::Camera camera;
    camera.Position = eye;
    camera.Fov = fovDegrees * kDegToRad;
    open( carPosition, camera );
    position = eye;
    yaw = yawDegrees * kDegToRad;
    pitch = Clamp( pitchDegrees * kDegToRad, -kMaxPitch, kMaxPitch );
    fov = Clamp( fovDegrees * kDegToRad, kFovMin, kFovMax );
}

void PhotoCamera::close() {
    setRelativeMouse( false );
    bOpen = false;
    bOptions = false;
    GetPhotoGrade().Active = false;
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
    {
        // The Options panel drives the photoshop pass (bloom / contrast / red, green, blue).
        PhotoGrade &grade = GetPhotoGrade();
        grade.Active = true;
        grade.Bloom = optionValue[0];
        grade.Contrast = optionValue[3];
        grade.Red = optionValue[4];
        grade.Green = optionValue[5];
        grade.Blue = optionValue[6];
    }
    // The previous frame's capture is done: the UI comes back.
    bCapturing = false;
    flashAlpha = Max( 0.0f, flashAlpha - deltaTime / kFlashTime );
    // Options panel (X): it owns the pad while it is open.
    if ( bOptions ) {
        tickOptions();
        return false;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuOption1 ) || gpInput->wasKeyPressed( SDL_SCANCODE_O ) ) {
        bOptions = true;
        optionRow = 0;
        setRelativeMouse( false );
        return false;
    }
    // Back: B / circle, Esc / START.
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        return true;
    }
    // Rotate: the right stick, or the mouse while a button is held (Tab keeps
    // it captured, like the world viewer).
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
    // Tilt: LB / RB (R / F) pitch the lens, as the retail bar says.
    auto key = []( SDL_Scancode a, SDL_Scancode b ) { return gpInput->isKeyDown( a ) || gpInput->isKeyDown( b ); };
    const float tilt = ( gpInput->isDown( eInputAction::IA_CarBackView ) || gpInput->isKeyDown( SDL_SCANCODE_R ) ? 1.0f : 0.0f ) -
                       ( gpInput->isDown( eInputAction::IA_CarCamera ) || gpInput->isKeyDown( SDL_SCANCODE_F ) ? 1.0f : 0.0f );
    pitch = Clamp( pitch + tilt * kLookSpeed * 0.6f * deltaTime, -kMaxPitch, kMaxPitch );
    // Move: the left stick, WASD / arrows move along the view (free flight -
    // the camera is not locked to any height or radius). Up / down: Q / E,
    // Ctrl / Space or the D-pad.
    float moveX = gpInput->getLeftStickX();
    float moveY = gpInput->getLeftStickY(); // up = -1
    moveY += ( key( SDL_SCANCODE_S, SDL_SCANCODE_DOWN ) ? 1.0f : 0.0f ) -
             ( key( SDL_SCANCODE_W, SDL_SCANCODE_UP ) ? 1.0f : 0.0f );
    moveX += ( key( SDL_SCANCODE_D, SDL_SCANCODE_RIGHT ) ? 1.0f : 0.0f ) -
             ( key( SDL_SCANCODE_A, SDL_SCANCODE_LEFT ) ? 1.0f : 0.0f );
    const float up = ( key( SDL_SCANCODE_E, SDL_SCANCODE_SPACE ) || gpInput->isDown( eInputAction::IA_MenuUp ) ? 1.0f : 0.0f ) -
                     ( key( SDL_SCANCODE_Q, SDL_SCANCODE_LCTRL ) || gpInput->isDown( eInputAction::IA_MenuDown ) ? 1.0f : 0.0f );
    const Vector3f forward( sinf( yaw ) * cosf( pitch ), sinf( pitch ), cosf( yaw ) * cosf( pitch ) );
    const Vector3f right( -forward.z, 0.0f, forward.x ); // Eden x points to the left of the screen
    const Vector3f move = forward * ( -moveY ) + right * moveX + Vector3f( 0.0f, up, 0.0f ); // stick / D right = right
    if ( move.length() > 1e-4f ) {
        const bool bFast = gpInput->isKeyDown( SDL_SCANCODE_LSHIFT ) || gpInput->isKeyDown( SDL_SCANCODE_RSHIFT );
        position = position + move.normalized() * ( kMoveSpeed * ( bFast ? kFastMultiplier : 1.0f ) * deltaTime );
    }
    // Zoom: LT / RT (Page Down / Page Up) and the wheel, continuous.
    const float zoomIn = gpInput->getAnalog( eInputAction::IA_CarAccel ) + ( gpInput->isKeyDown( SDL_SCANCODE_PAGEUP ) ? 1.0f : 0.0f );
    const float zoomOut = gpInput->getAnalog( eInputAction::IA_CarBrake ) + ( gpInput->isKeyDown( SDL_SCANCODE_PAGEDOWN ) ? 1.0f : 0.0f );
    fov = Clamp( fov * std::pow( kFovStep, ( zoomOut - zoomIn ) * 2.5f * deltaTime ), kFovMin, kFovMax );
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
    prompt.clear();
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

// ---------------------------------------------------------------------------
// The Options panel: Bloom intensity, Depth of field (intensity, distance),
// Contrast, Red / Green / Blue intensity (sliders) and Character (On / Off)
// [DATA: the 360 reference shots; the values are stored, not yet applied].
void PhotoCamera::tickOptions() {
    constexpr int32_t kRows = 8;
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        optionRow = ( optionRow + kRows - 1 ) % kRows;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        optionRow = ( optionRow + 1 ) % kRows;
    }
    const bool bLeft = gpInput->isDown( eInputAction::IA_MenuLeft );
    const bool bRight = gpInput->isDown( eInputAction::IA_MenuRight );
    if ( optionRow < 7 ) {
        const float delta = ( bRight ? 1.0f : 0.0f ) - ( bLeft ? 1.0f : 0.0f );
        optionValue[optionRow] = Clamp( optionValue[optionRow] + delta * 0.4f * ( 1.0f / 60.0f ), 0.0f, 1.0f );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) || gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        bCharacter = !bCharacter;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuBack ) ||
         gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        bOptions = false;
    }
}

namespace {
constexpr uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) {
    return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 );
}

// A ring segment (thick arc) from angle a0 to a1 (radians), as triangles.
void DrawArc( Renderer2D &r, float cx, float cy, float radius, float thickness, float a0, float a1, uint32_t color ) {
    const int32_t steps = Max( 2, static_cast<int32_t>( ( a1 - a0 ) / 0.15f ) );
    std::vector<Vertex2D> v;
    for ( int32_t i = 0; i < steps; i++ ) {
        const float t0 = a0 + ( a1 - a0 ) * i / steps, t1 = a0 + ( a1 - a0 ) * ( i + 1 ) / steps;
        const float ro = radius + thickness * 0.5f, ri = radius - thickness * 0.5f;
        const Vertex2D p0 = { cx + cosf( t0 ) * ro, cy + sinf( t0 ) * ro, 0, 0, color };
        const Vertex2D p1 = { cx + cosf( t0 ) * ri, cy + sinf( t0 ) * ri, 0, 0, color };
        const Vertex2D p2 = { cx + cosf( t1 ) * ro, cy + sinf( t1 ) * ro, 0, 0, color };
        const Vertex2D p3 = { cx + cosf( t1 ) * ri, cy + sinf( t1 ) * ri, 0, 0, color };
        v.insert( v.end(), { p0, p1, p2, p2, p1, p3 } );
    }
    r.drawTriangles( nullptr, v.data(), static_cast<uint32_t>( v.size() ) );
}

void DrawDisc( Renderer2D &r, float cx, float cy, float radius, uint32_t color ) {
    DrawArc( r, cx, cy, radius * 0.5f, radius, 0.0f, 6.2832f, color );
}

// A pad button glyph: a coloured disc with a letter.
void DrawButton( Renderer2D &r, const Font *pFont, float cx, float cy, const char *letter, uint32_t fill ) {
    DrawDisc( r, cx, cy, 24.0f, Rgba( 210, 210, 215 ) );
    DrawDisc( r, cx, cy, 20.0f, fill );
    r.drawText( pFont, letter, cx, cy - 13.0f, 26.0f, Rgba( 245, 245, 245 ), eTextAlign::TA_Center );
}

// A stick glyph: a grey cap over a base, with its LS / RS tag.
void DrawStick( Renderer2D &r, const Font *pFont, float cx, float cy, const char *tag ) {
    DrawDisc( r, cx, cy + 6.0f, 20.0f, Rgba( 120, 120, 125 ) );
    DrawDisc( r, cx, cy - 4.0f, 20.0f, Rgba( 205, 205, 210 ) );
    DrawDisc( r, cx, cy - 6.0f, 11.0f, Rgba( 235, 235, 238 ) );
    r.drawText( pFont, tag, cx + 2.0f, cy + 12.0f, 15.0f, Rgba( 190, 190, 195 ), eTextAlign::TA_Center );
}

// A shoulder / trigger glyph: a tab with its tag.
void DrawTab( Renderer2D &r, const Font *pFont, float x, float y, float w, float h, const char *tag ) {
    r.drawRect( Rect2D( x, y, w, h ), Rgba( 75, 78, 84 ) );
    r.drawRectOutline( Rect2D( x, y, w, h ), 1.5f, Rgba( 170, 172, 178 ) );
    r.drawText( pFont, tag, x + w * 0.5f, y + h * 0.5f - 9.0f, 17.0f, Rgba( 205, 207, 212 ), eTextAlign::TA_Center );
}
} // namespace

void PhotoCamera::drawUi( Renderer2D &renderer ) const {
    if ( !bOpen || bCapturing ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    renderer.begin( 1280.0f, 720.0f );
    const uint32_t kLabel = Rgba( 245, 245, 245 );
    // The top bar: a dark band with Move / Zoom / Tilt / Rotate.
    renderer.drawGradient( Rect2D( 0.0f, 0.0f, 1280.0f, 108.0f ), Rgba( 10, 20, 40, 215 ), Rgba( 10, 20, 40, 150 ) );
    renderer.drawRect( Rect2D( 0.0f, 106.0f, 1280.0f, 2.0f ), Rgba( 150, 155, 165, 200 ) );
    DrawStick( renderer, pFont, 196.0f, 62.0f, "LS" );
    renderer.drawText( pFont, "Move", 220.0f, 50.0f, 28.0f, kLabel );
    DrawTab( renderer, pFont, 430.0f, 46.0f, 30.0f, 38.0f, "LT" );
    DrawTab( renderer, pFont, 466.0f, 46.0f, 30.0f, 38.0f, "RT" );
    renderer.drawText( pFont, "Zoom", 505.0f, 50.0f, 28.0f, kLabel );
    DrawTab( renderer, pFont, 668.0f, 48.0f, 44.0f, 30.0f, "LB" );
    DrawTab( renderer, pFont, 714.0f, 48.0f, 44.0f, 30.0f, "RB" );
    renderer.drawText( pFont, "Tilt", 750.0f, 50.0f, 28.0f, kLabel );
    DrawStick( renderer, pFont, 938.0f, 62.0f, "RS" );
    renderer.drawText( pFont, "Rotate", 964.0f, 50.0f, 28.0f, kLabel );
    // The bottom bar: Take a Photo / Back / Options (the Options panel shows
    // OK / Back).
    renderer.drawGradient( Rect2D( 0.0f, 616.0f, 1280.0f, 104.0f ), Rgba( 10, 20, 40, 150 ), Rgba( 10, 20, 40, 215 ) );
    renderer.drawRect( Rect2D( 0.0f, 616.0f, 1280.0f, 2.0f ), Rgba( 150, 155, 165, 200 ) );
    if ( !bOptions ) {
        DrawButton( renderer, pFont, 295.0f, 650.0f, "A", Rgba( 70, 150, 50 ) );
        renderer.drawText( pFont, "Take a Photo", 330.0f, 636.0f, 28.0f, kLabel );
        DrawButton( renderer, pFont, 553.0f, 650.0f, "B", Rgba( 175, 45, 50 ) );
        renderer.drawText( pFont, "Back", 588.0f, 636.0f, 28.0f, kLabel );
        DrawButton( renderer, pFont, 786.0f, 650.0f, "X", Rgba( 40, 115, 190 ) );
        renderer.drawText( pFont, "Options", 821.0f, 636.0f, 28.0f, kLabel );
        // The aiming reticle: four arcs round a dot [DATA: the reference shot].
        const float cx = 640.0f, cy = 366.0f;
        for ( int32_t i = 0; i < 4; i++ ) {
            const float a = i * 1.5708f + 0.35f;
            DrawArc( renderer, cx, cy, 40.0f, 3.5f, a, a + 0.9f, Rgba( 235, 235, 235, 230 ) );
        }
        DrawDisc( renderer, cx, cy, 12.0f, Rgba( 235, 235, 235, 230 ) );
    } else {
        DrawButton( renderer, pFont, 745.0f, 650.0f, "A", Rgba( 70, 150, 50 ) );
        renderer.drawText( pFont, "OK", 780.0f, 636.0f, 28.0f, kLabel );
        DrawButton( renderer, pFont, 874.0f, 650.0f, "B", Rgba( 175, 45, 50 ) );
        renderer.drawText( pFont, "Back", 909.0f, 636.0f, 28.0f, kLabel );
        // The panel.
        const float px = 48.0f, py = 18.0f, pw = 376.0f, ph = 650.0f;
        renderer.drawRect( Rect2D( px, py, pw, ph ), Rgba( 14, 20, 30, 235 ) );
        renderer.drawRectOutline( Rect2D( px, py, pw, ph ), 2.0f, Rgba( 70, 75, 85 ) );
        renderer.drawRect( Rect2D( px + 8.0f, py + 8.0f, 56.0f, 56.0f ), Rgba( 40, 44, 52 ) );
        renderer.drawText( pFont, "Options", 148.0f, 38.0f, 32.0f, kLabel );
        static const char *kRowNames[8] = { "Bloom intensity", "Depth of field - Intensity", "Depth of field - Distance",
                                            "Contrast", "Red intensity", "Green intensity", "Blue intensity", "Character" };
        const float rowTop = 98.0f, rowH = 66.0f;
        for ( int32_t i = 0; i < 8; i++ ) {
            const float y = rowTop + i * rowH;
            const bool bSel = i == optionRow;
            if ( bSel ) {
                renderer.drawGradient( Rect2D( px + 4.0f, y, pw - 8.0f, rowH ), Rgba( 120, 22, 30, 230 ), Rgba( 70, 14, 20, 230 ) );
            }
            renderer.drawText( pFont, kRowNames[i], px + pw * 0.5f, y + 8.0f, 25.0f,
                               bSel ? Rgba( 235, 150, 150 ) : Rgba( 215, 215, 220 ), eTextAlign::TA_Center );
            if ( i < 7 ) {
                const float tx0 = px + 40.0f, tx1 = px + pw - 40.0f, ty = y + 44.0f;
                renderer.drawRect( Rect2D( tx0, ty - 2.0f, tx1 - tx0, 4.0f ), Rgba( 8, 10, 14, 220 ) );
                const float thumbX = tx0 + ( tx1 - tx0 ) * optionValue[i];
                renderer.drawRect( Rect2D( thumbX - 16.0f, ty - 9.0f, 32.0f, 18.0f ),
                                   bSel ? Rgba( 205, 90, 40 ) : Rgba( 110, 112, 118 ) );
                renderer.drawRectOutline( Rect2D( thumbX - 16.0f, ty - 9.0f, 32.0f, 18.0f ), 1.5f, Rgba( 30, 30, 34 ) );
                if ( bSel ) {
                    const uint32_t c = Rgba( 150, 150, 155 );
                    const Vertex2D l[3] = { { px + 16.0f, ty, 0, 0, c }, { px + 32.0f, ty - 12.0f, 0, 0, c }, { px + 32.0f, ty + 12.0f, 0, 0, c } };
                    const Vertex2D rr[3] = { { px + pw - 16.0f, ty, 0, 0, c }, { px + pw - 32.0f, ty - 12.0f, 0, 0, c }, { px + pw - 32.0f, ty + 12.0f, 0, 0, c } };
                    renderer.drawTriangles( nullptr, l, 3 );
                    renderer.drawTriangles( nullptr, rr, 3 );
                }
            } else {
                renderer.drawRect( Rect2D( px + 14.0f, y + 34.0f, pw - 28.0f, 26.0f ), Rgba( 6, 8, 12, 230 ) );
                renderer.drawRectOutline( Rect2D( px + 14.0f, y + 34.0f, pw - 28.0f, 26.0f ), 1.5f, Rgba( 90, 94, 104 ) );
                renderer.drawText( pFont, bCharacter ? "On" : "Off", px + pw * 0.5f, y + 35.0f, 24.0f, kLabel, eTextAlign::TA_Center );
            }
        }
    }
    renderer.end();
    if ( flashAlpha > 0.0f ) {
        renderer.begin();
        renderer.drawRect( Rect2D( -10000.0f, -10000.0f, 20000.0f, 20000.0f ),
                           0x00ffffffu | ( static_cast<uint32_t>( flashAlpha * 255.0f ) << 24 ) );
        renderer.end();
    }
}
