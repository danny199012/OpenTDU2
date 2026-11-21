#pragma once

#include "filesystem/bank.h"
#include "game/cinematic/cinematic_audio.h"
#include "game/cinematic/cinematic_scene.h"
#include "render/eden/eden_animation.h"
#include "render/eden/eden_scene_view.h"
#include <map>
#include <memory>

#include <string>
#include <vector>

// The racing school's end-of-test cinematic (docs/re/licenses_spec.md 12.5): after a test the camera of
// pack_racingschool.bnk's M_<ASPHALT|DIRT|CLASSIC>_<WIN|LOOSE>_nn (7.4 s, two shots: the cabin through the windscreen,
// then the close-up) takes over, anchored to the car, and the 8 s _LOOP one repeats under the result menu [DATA: the
// names and durations; the 360 clips: the shot sits at the car with two figures in the cabin]. The cinematic's actors
// are not animated by the port yet: the instructor rides as a seated passenger (GMFreeRide), the camera is the cinematic's.
class SchoolEndScene {
public:
    // letter: A asphalt / B dirt / C classic; carWorld: the scene origin (the PLAYER_CAR actor sits at (0, 0, 0)).
    bool start( char letter, bool bWon, const Matrix4x4f &carWorld );
    void stop();
    void tick( float deltaTime );
    inline bool isActive() const { return bActive; }
    inline float getTime() const { return time; }
    // True while the main cinematic (not the loop) plays.
    inline bool isMain() const { return !bLooping; }
    bool getCamera( EdenSceneView::Camera &outCamera ) const;
    // The clip the cinematic plays on an actor now (the Animation track, as IntroCinematic::sampleAnimationTrack reads it):
    // pActor matches the actor's name (exact, or the prefix when it ends with '_'). nullptr before the first key.
    const EdenAnimation *getActorClip( const char *pActor, float &outClipTime );

private:
    struct Play {
        Cinematic Cin;
        std::vector<CinematicScene> Scenes;
    };
    bool loadPlay( const std::string &name, Play &out );
    const EdenAnimation *getClip( const Play &play, uint32_t refIndex, const CinematicScene &scene );
    std::map<std::string, std::unique_ptr<EdenAnimation>> clips;

    Bank bank;
    Play main, loop;
    Matrix4x4f origin;
    Quaternionf originRotation;
    float time = 0.0f;
    bool bActive = false;
    bool bLooping = false;
};
