#include "shared.h"
#include "school_test_intro.h"
#include "render/font.h"
#include "render/renderer_2d.h"

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }
constexpr float kWait = 2.0f;   // s the car waits at the start
constexpr float kHold = 1.4f;   // s at the end before the fade
constexpr float kFade = 0.8f;   // s of black at each end of the loop
constexpr float kSpeed = 17.0f; // m/s of the demonstration
constexpr float kShot = 4.5f;   // s per camera shot

// yaw about Y then pitch, the convention the cinematic cameras use (the look-at of the school close-ups, checked by eye).
Quaternionf LookAtRotation( const Vector3f &from, const Vector3f &to ) {
    const Vector3f d = to - from;
    const float flat = std::sqrt( d.x * d.x + d.z * d.z );
    const float yaw = std::atan2( d.x, d.z );
    const float pitch = std::atan2( d.y, flat );
    const Quaternionf qy( 0.0f, std::sin( -yaw * 0.5f ), 0.0f, std::cos( -yaw * 0.5f ) );
    const Quaternionf qx( std::sin( pitch * 0.5f ), 0.0f, 0.0f, std::cos( pitch * 0.5f ) );
    return qy * qx;
}
} // namespace

bool SchoolTestIntro::start( const std::vector<Vector3f> &newPath, const Info &newInfo ) {
    stop();
    if ( newPath.size() < 2 ) {
        return false;
    }
    path = newPath;
    info = newInfo;
    cumulative.assign( 1, 0.0f );
    for ( size_t i = 1; i < path.size(); i++ ) {
        cumulative.push_back( cumulative.back() + ( path[i] - path[i - 1] ).length() );
    }
    length = cumulative.back();
    if ( length < 1.0f ) {
        return false;
    }
    runTime = Clamp( length / kSpeed, 6.0f, 22.0f );
    loopTime = kWait + runTime + kHold + kFade;
    time = 0.0f;
    loops = 0;
    bActive = true;
    tick( 0.0f );
    return true;
}

bool SchoolTestIntro::startReplay( const std::string &testName, const Info &newInfo ) {
    stop();
    if ( !replay.load( testName ) ) {
        return false;
    }
    info = newInfo;
    bReplay = true;
    runTime = replay.getDuration();
    loopTime = runTime + kHold + kFade;
    time = 0.0f;
    loops = 0;
    bActive = true;
    tick( 0.0f );
    return true;
}

void SchoolTestIntro::stop() {
    bActive = false;
    bReplay = false;
    path.clear();
    cumulative.clear();
}

Vector3f SchoolTestIntro::pointAt( float distance, Vector3f &outForward ) const {
    distance = Clamp( distance, 0.0f, length );
    size_t i = 1;
    while ( i + 1 < path.size() && cumulative[i] < distance ) {
        i++;
    }
    const float span = Max( cumulative[i] - cumulative[i - 1], 1e-3f );
    const float f = Clamp( ( distance - cumulative[i - 1] ) / span, 0.0f, 1.0f );
    Vector3f forward = path[i] - path[i - 1];
    forward.y = 0.0f;
    outForward = forward.length() > 1e-3f ? forward.normalized() : Vector3f( 0.0f, 0.0f, 1.0f );
    return path[i - 1] + ( path[i] - path[i - 1] ) * f;
}

void SchoolTestIntro::tick( float deltaTime ) {
    if ( !bActive ) {
        return;
    }
    time += deltaTime;
    if ( time >= loopTime ) {
        time = std::fmod( time, loopTime );
        loops++;
    }
    if ( bReplay ) {
        Vector3f forward, up;
        replay.sample( Min( time, runTime ), carPosition, forward, up );
        carHeading = std::atan2( forward.x, forward.z );
        return;
    }
    const float run = Clamp( ( time - kWait ) / runTime, 0.0f, 1.0f );
    // Ease in / out so the car leaves and arrives like a driven one.
    const float eased = run * run * ( 3.0f - 2.0f * run );
    Vector3f forward;
    carPosition = pointAt( eased * length, forward );
    // Look a little ahead so the heading follows the bends.
    Vector3f ahead;
    pointAt( Min( eased * length + 6.0f, length ), ahead );
    forward = ( forward + ahead ).normalized();
    carHeading = std::atan2( forward.x, forward.z );
}

bool SchoolTestIntro::getCamera( EdenSceneView::Camera &outCamera ) const {
    if ( !bActive ) {
        return false;
    }
    const Vector3f forward( std::sin( carHeading ), 0.0f, std::cos( carHeading ) );
    const Vector3f right( forward.z, 0.0f, -forward.x );
    const Vector3f up( 0.0f, 1.0f, 0.0f );
    Vector3f position, target;
    // Shots: behind the car at the start, then chase / cockpit / wide, repeating.
    const int32_t shot = time < kWait + 2.0f ? 0 : 1 + static_cast<int32_t>( ( time - kWait - 2.0f ) / kShot ) % 3;
    switch ( shot ) {
    case 0:
        position = carPosition - forward * 6.0f + right * 1.8f + up * 1.7f;
        target = carPosition + up * 0.8f;
        outCamera.Fov = 45.0f * kDegToRad;
        break;
    case 1:
        position = carPosition - forward * 7.5f + up * 2.6f;
        target = carPosition + forward * 4.0f + up * 1.0f;
        outCamera.Fov = 55.0f * kDegToRad;
        break;
    case 2:
        // A low shot from the side ahead of the car (the retail cockpit shot needs the driver's eye point, not placed here).
        position = carPosition + right * 2.6f + forward * 4.5f + up * 0.9f;
        target = carPosition + up * 0.7f;
        outCamera.Fov = 50.0f * kDegToRad;
        break;
    default:
        position = carPosition + right * 22.0f + forward * 6.0f + up * 12.0f;
        target = carPosition + up * 0.5f;
        outCamera.Fov = 40.0f * kDegToRad;
        break;
    }
    outCamera.Position = position;
    outCamera.Rotation = LookAtRotation( position, target );
    outCamera.Near = 0.1f;
    outCamera.Far = 40000.0f;
    return true;
}

float SchoolTestIntro::getFade() const {
    if ( !bActive ) {
        return 0.0f;
    }
    if ( time < kFade ) {
        return 1.0f - time / kFade;
    }
    if ( time > loopTime - kFade ) {
        return Clamp( ( time - ( loopTime - kFade ) ) / kFade, 0.0f, 1.0f );
    }
    return 0.0f;
}

void SchoolTestIntro::draw( Renderer2D &renderer, const Font *pFont ) const {
    if ( !bActive ) {
        return;
    }
    renderer.begin( 1280.0f, 720.0f );
    if ( !bNativeBanner ) {
        const float fadeOnly = getFade();
        if ( fadeOnly > 0.0f ) {
            renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, static_cast<uint32_t>( fadeOnly * 255.0f ) ) );
        }
        renderer.end();
        return;
    }
    // The banner: a dark band top left with the title, then the objective wrapped under it and the place / weather chips.
    const float x0 = 28.0f, y0 = 26.0f, w = 700.0f;
    std::vector<std::string> lines;
    {
        std::string line, word;
        const std::string &text = info.Description;
        for ( size_t i = 0; i <= text.size(); i++ ) {
            if ( i == text.size() || text[i] == ' ' ) {
                const std::string trial = line.empty() ? word : line + " " + word;
                if ( pFont != nullptr && pFont->measureText( trial.c_str(), 16.0f ) > w - 110.0f && !line.empty() ) {
                    lines.push_back( line );
                    line = word;
                } else {
                    line = trial;
                }
                word.clear();
            } else {
                word += text[i];
            }
        }
        if ( !line.empty() ) {
            lines.push_back( line );
        }
    }
    const float h = 58.0f + 22.0f * static_cast<float>( lines.size() );
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 10, 22, 40, 215 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 3.0f ), Rgba( 120, 170, 230, 255 ) );
    renderer.drawRect( Rect2D( x0 + 14.0f, y0 + 14.0f, 46.0f, 46.0f ), Rgba( 40, 90, 150, 255 ) );
    renderer.drawRectOutline( Rect2D( x0 + 14.0f, y0 + 14.0f, 46.0f, 46.0f ), 2.0f, Rgba( 220, 230, 245, 255 ) );
    if ( pFont != nullptr ) {
        renderer.drawText( pFont, info.Title.c_str(), x0 + 76.0f, y0 + 10.0f, 22.0f, Rgba( 255, 255, 255 ) );
        const char *pPlace = info.Place == 1 ? "On the road" : "School grounds";
        const char *pWeather = info.Weather == 1 ? "Rain" : info.Weather == 2 ? "Night" : "Clear";
        char chips[96];
        snprintf( chips, sizeof( chips ), "%s   -   %s", pPlace, pWeather );
        renderer.drawText( pFont, chips, x0 + 76.0f, y0 + 36.0f, 14.0f, Rgba( 255, 190, 70 ) );
        float y = y0 + 62.0f;
        for ( const std::string &line : lines ) {
            renderer.drawText( pFont, line.c_str(), x0 + 76.0f, y, 16.0f, Rgba( 225, 230, 240 ) );
            y += 22.0f;
        }
        // The bottom bar: Help / A OK / B Cancel.
        renderer.drawRect( Rect2D( 330.0f, 668.0f, 620.0f, 40.0f ), Rgba( 8, 14, 24, 200 ) );
        renderer.drawText( pFont, "Help", 470.0f, 677.0f, 18.0f, Rgba( 200, 205, 215 ) );
        renderer.drawRect( Rect2D( 590.0f, 676.0f, 22.0f, 22.0f ), Rgba( 70, 170, 60, 255 ) );
        renderer.drawText( pFont, "A", 596.0f, 677.0f, 16.0f, Rgba( 255, 255, 255 ) );
        renderer.drawText( pFont, "OK", 622.0f, 677.0f, 18.0f, Rgba( 235, 240, 245 ) );
        renderer.drawRect( Rect2D( 730.0f, 676.0f, 22.0f, 22.0f ), Rgba( 200, 60, 50, 255 ) );
        renderer.drawText( pFont, "B", 736.0f, 677.0f, 16.0f, Rgba( 255, 255, 255 ) );
        renderer.drawText( pFont, "Cancel", 762.0f, 677.0f, 18.0f, Rgba( 235, 240, 245 ) );
    }
    // The fade to black that ends and restarts the demonstration (over the banner too, like the capture's loop).
    const float fade = getFade();
    if ( fade > 0.0f ) {
        renderer.drawRect( Rect2D( 0.0f, 0.0f, 1280.0f, 720.0f ), Rgba( 0, 0, 0, static_cast<uint32_t>( fade * 255.0f ) ) );
    }
    renderer.end();
}
