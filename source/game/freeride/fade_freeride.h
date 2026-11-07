#pragma once

#include "filesystem/bank.h"
#include "game/avatar/eden_avatar.h"
#include "game/cinematic/cinematic_audio.h"
#include "game/cinematic/cinematic_scene.h"
#include "render/eden/eden_animation.h"
#include "render/eden/eden_scene_view.h"

// C_FADE_FREERIDE (story step 2 -> 3, docs/re/freeride_spec.md 1.5) and the
// retail order (user footage "TDU2 - Wake up!"): when the 90 s freeride
// timer runs out the car stops WHERE IT IS and the knock scene plays OVER
// the gameplay — Tess (entity WOMAN of fade_freeride.bnk, DBPNJ
// STO_Tess_Wintory) walks to the driver window wherever the car stopped,
// knocks (s_toctoc) and says the first lines (the Fx at 2.68 s,
// FxIntroNew-E_Meet_Tess_02_00, 12.34 s, through the language bank
// fade_freeride__<lang>.bnk). Her animation is the cinematic additional
// anim ev_tessincar_toplayer01_npc.anm (85 tracks, 29.2 s, pelvis fixed:
// in place at the window; the .sce scene position (y = -76) is the hidden
// state of the life scene). The cinematic has no camera of its own (shot
// camera "NO_CAMERA"): the game camera stays, the HUD stays (retail
// footage). The mode's white fade follows and hides the car being parked
// at the house for the dream scene (C_MEETING_TESS of meeting_tess.bnk,
// not implemented yet; the current wake-up placeholder replays her at the
// house entity placement, house frame (1.429, 0, 0), with the cinematic
// camera of the .sce). The scenes are CUT from the default flow — the
// white flash is the whole story beat; -wakeup plays them.
class FadeFreeride {
public:
    // Loads the bank, the cinematic and the WOMAN avatar. houseOrigin: the
    // house spot origin (FirstOutro::getSceneOrigin).
    bool load( const Matrix4x4f &houseOrigin );
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    void start( float startTime = 0.0f, bool bSounds = true );
    // Anchor the WOMAN at the driver window of the parked car (pre-flash
    // knock scene; re-while the car rolls to its stop).
    void anchorToCar( const Matrix4x4f &carWorld );
    // Back at the house entity placement (the wake-up placeholder scene).
    void anchorToHouse();
    void tick( float deltaTime );
    inline bool isPlaying() const { return bPlaying; }
    // True once started and until unload: the end pose is kept (CinEnd 3).
    inline bool isStarted() const { return bStarted; }
    inline float getDuration() const { return cinematic.getDuration(); }
    // Alpha of the white overlay (1 = opaque white): the shot fade in from
    // white (FadeType 1), gone after its FadeDuration (2.5 s). The knock
    // scene does not use it (no camera of its own).
    float getWhiteAlpha() const;

    // Cinematic camera (island space) — the wake-up placeholder scene only.
    bool getCamera( EdenSceneView::Camera &outCamera ) const;
    // The WOMAN avatar at her current anchor, animated by the additional
    // anim (scene passes; end pose after the end).
    void draw( EdenRenderer &renderer );

    inline const Matrix4x4f &getSceneOrigin() const { return sceneOrigin; }

private:
    Bank bank;
    BankHashcodes hashcodes;
    Cinematic cinematic;
    std::vector<CinematicScene> scenes;
    CinematicSounds sounds;
    std::vector<GSAudio::VoiceHandle> voices;
    EdenAvatarLibrary avatarLibrary;
    std::unique_ptr<EdenAvatar> woman;
    EdenAnimation anim;
    bool bAnim = false;
    Matrix4x4f sceneOrigin;
    Quaternionf sceneRotation;
    Matrix4x4f womanWorld;
    Vector3f womanLocal;          // entity placement (house frame)
    Quaternionf womanRot;
    bool bSounds = true;          // the placeholder wake-up replays it muted
    std::vector<BoneTransform> locals;
    std::vector<Matrix4x4f> worldBones;
    std::vector<Matrix4x4f> skin;
    float time = 0.0f;
    bool bPlaying = false;
    bool bStarted = false;
    bool bLoaded = false;
};
