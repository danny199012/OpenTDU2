#pragma once

#include "audio/gps_voice.h"
#include "core/math.h"

class MapScreen;

// Spoken sat-nav guidance along the MapScreen route. The laws are the retail
// GPS manager's (TestDrive2.exe 0x933xxx..0x937xxx, readable half), see
// docs/re/gps_voice_spec.md:
//   - turn cues (FUN_00934740): at the next junction of the route, crossing
//     200 m says "At the next intersection, turn right / left", crossing 50 m
//     ([+0x1cf4] = 0xef01ec) says "Turn right / left"; right when the junction
//     angle (MapScreen::RouteTurn, 180 = straight on) is < 121 deg, left when
//     > 241 deg, nothing in between [CODE];
//   - destination countdown (FUN_00934a10): crossing 3000 / 2000 / 1500 / 1000
//     / 500 m of route left (or the same in miles: x 0.621504) within 50 m of
//     the threshold [CODE];
//   - wrong way (the tick at 0x936fb8): "turn around" once after 2 s against
//     the route, re-armed when back on it [CODE / INF: how retail measures it];
//   - arrival (0xa574c2): "You have reached your destination" [CODE];
//   - "Recalculating" when the car leaves the route [INF: no readable caller].
class GpsGuidance {
public:
    bool load();
    // Call every frame while driving, after MapScreen::tickRoute. velocity: the
    // car's, world m/s. bArrived: MapScreen::takeArrival() of this frame.
    void tick( const MapScreen &map,
               const Vector3f &position,
               const Vector3f &velocity,
               bool bArrived,
               float deltaTime );
    // Forgets the per-route state (a new destination).
    void reset();
    inline void setImperial( bool bValue ) { bImperial = bValue; }
    inline GPSVoice &getVoice() { return voice; }
    // The HUD turn arrow of the next junction (MapScreen::RouteTurn::Arrow,
    // 0..15) or kArrowEmpty.
    static constexpr int32_t kArrowEmpty = 16;
    inline int32_t getArrow() const { return arrow; }

private:
    void tickTurn( float angleDeg, float distance );
    void tickCountdown( float remaining );
    void say( int32_t cue, const char *pWhy, float value );

    GPSVoice voice;
    bool bImperial =
        false; // gps_Unit (retail [0x10017f0]+0x14 == 1); -gpsmiles [INF] until
               // the Game settings page answers
    bool bHadRoute = false;
    int32_t turnNode = -1;          // road node of the junction being announced
    float prevTurnDistance = -1.0f; // [+0x1ce0]
    float prevRemaining = -1.0f;    // [+0x1cdc]
    float wrongWayTime = 0.0f;      // [+0x84]
    bool bWrongWaySaid = false;     // [+0x88]
    bool bOffRoute = false;
    float recalcCooldown = 0.0f;
    int32_t arrow = kArrowEmpty;
};
