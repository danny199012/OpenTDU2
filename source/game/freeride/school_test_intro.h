#pragma once

#include "render/eden/eden_scene_view.h"

#include "game/freeride/test_replay.h"

#include <string>
#include <vector>

class Font;
class Renderer2D;

// The pre-test presentation of a racing school test (docs/re/licenses_spec.md 12.9, the 360 clips): after the test loads, a
// banner top left (the test's icon, title, objective, place and weather) and the "Help / A OK / B Cancel" bar sit over a
// demonstration - the car drives the course along the gates while the camera cuts between behind the car at the start,
// the chase view, the cockpit and a wide shot - which fades to black at its end and starts again until the player
// presses A (start the test) or B (cancel). [DATA: the 360 clip 02_RSC4T01 shows exactly this; the demo car, the cuts and
// their times are the port's own approximation, the retail shots come from the test's own camera data - not decoded.]
class SchoolTestIntro {
public:
    struct Info {
        std::string Title;
        std::string Description;
        int32_t Weather = 0; // rswh: 0 sun, 1 rain, 2 night
        int32_t Place = 0;   // rsl1: 0 the school grounds, 1 the road
    };
    // path: the car's way (the start pose first, then the gates in order).
    bool start( const std::vector<Vector3f> &path, const Info &info );
    // The retail demonstration: the test's recorded run (ig2/replays) drives the car instead of the gate path.
    bool startReplay( const std::string &testName, const Info &info );
    void stop();
    inline bool isActive() const { return bActive; }
    // false: RESULTS.vmf's own banner is drawn (preview_in), only the fade is drawn here.
    inline void setNativeBanner( bool bNative ) { bNativeBanner = bNative; }
    void tick( float deltaTime );
    // The demo car's pose now.
    inline const Vector3f &getCarPosition() const { return carPosition; }
    inline float getCarHeading() const { return carHeading; }
    bool getCamera( EdenSceneView::Camera &outCamera ) const;
    // 0..1 black over the picture (the fade at the loop's ends).
    float getFade() const;
    void draw( Renderer2D &renderer, const Font *pFont ) const;
    // How many times the demonstration has restarted (test hook / log).
    inline int32_t getLoops() const { return loops; }
    // The recorded run being played (nullptr for the gate-path fallback) and its clock (seconds into the recording).
    inline const TestReplay *getReplay() const { return bReplay ? &replay : nullptr; }
    inline float getReplayTime() const { return time < runTime ? time : runTime; }

private:
    Vector3f pointAt( float distance, Vector3f &outForward ) const;

    bool bActive = false;
    TestReplay replay;
    bool bReplay = false;
    bool bNativeBanner = true;
    Info info;
    std::vector<Vector3f> path;
    std::vector<float> cumulative;
    float length = 0.0f;
    float runTime = 0.0f;
    float loopTime = 0.0f;
    float time = 0.0f;
    int32_t loops = 0;
    Vector3f carPosition;
    float carHeading = 0.0f;
};
