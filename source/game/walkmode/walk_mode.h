#pragma once

#include "game/walkmode/emote_wheel.h"
#include "game/walkmode/walk_controller.h"

#include <functional>
#include <string>

class IntroCinematic;

// Walk mode manager (DAT_00fd95e0, docs/re/tess_garage_spec.md 4.1) on an
// IntroCinematic scene: first person controller on the scene collision
// (.shk), camera, FpSelectable selection (position within 10 m in front
// of the eye, reticle ray on the volume, not occluded; 0.2 s re-selection
// cooldown), INTRO movie ItemType('over'/'out').
// Look-at (docs/re/facefx_spec.md 6.3.3): the selection never makes the
// hovered character look at the player (no look-at call in FUN_00952410,
// FUN_0094f440 or the FpSelectable enter/leave vf14/vf18) [CODE]. The only
// walk-mode look-at, FUN_00b858f0 at 0x9520b9, is in the player-avatar state
// machine FUN_009519a0 (states 6 and 8: the self-view camera type 10, avatar
// faded in) and makes the PLAYER's avatar look at the camera; OpenTDU2 has no
// self-view yet, so there is no caller (API: IntroCinematic::setLookAtTarget).
class WalkMode {
public:
    // Starts at an Entity__Camera (cam_entree_xx / cam_entree).
    bool start( IntroCinematic &scene, const std::string &cameraName, float fov );
    void update( IntroCinematic &scene, float deltaTime, bool bInputEnabled );
    // Camera back to the walk pose (after a cinematic).
    void resume( IntroCinematic &scene );
    void clearSelection( IntroCinematic &scene );
    // Test hook (-shopaim): the eye at a scene-local point, looking at another.
    void placeLooking( IntroCinematic &scene, const Vector3f &eye, const Vector3f &target );

    inline const Vector3f &getPosition() const { return controller.getPosition(); }
    inline Vector3f getForward() const { return controller.getForward(); }
    // Selectables refused by the filter are not picked (FIRST_WM: the "car"
    // item needs the driver door open, FUN_009aa560 carReady).
    using SelectableFilter = std::function<bool( int32_t index )>;
    void setSelectableFilter( SelectableFilter filter ) { selectableFilter = std::move( filter ); }
    inline int32_t getSelected() const { return selected; }
    // Called when the selection changes, before /:SelItemType and
    // ItemType('over') (the selectable wrapper vf14 sets its own movie
    // variables first, e.g. the shop car data FUN_00da50e0); -1: none.
    using SelectionCallback = std::function<void( int32_t index )>;
    // FUN_0094f440 end: the walk HUD movie (+0xd10) gets gotoAndPlay("on" /
    // "off") on its "cursor" clip (selected / none) [CODE: 0x94f78a..0x94f7b0];
    // the SHOPSCAR cursor then calls ItemAction -> ItemType itself. Without
    // it (INTRO.vmf modes) ItemType('over' / 'out') is called on the root.
    void setCursorLabels( bool bEnable ) { bCursorLabels = bEnable; }
    void setSelectionCallback( SelectionCallback callback ) { selectionCallback = std::move( callback ); }
    // The world outside the room (a house: the island under its plot - the
    // room's .shk carries floors and walls for the room itself, the yard has
    // none and the walk would freeze at the door, walk_mode.cpp FindHavokFloor
    // failing). Pushes the body out of the world's obstacles (the sector
    // statics, the exterior shell, the terrain) and answers the ground under
    // it. Scene-local in/out; false: no ground (open sea, unloaded sector) -
    // the freeze law stands. bodyPrevious: the body centre of the last step
    // (the thin-surface push side).
    using WorldHook = std::function<bool( Vector3f &body, const Vector3f &bodyPrevious, float &outFloorY )>;
    void setWorldHook( WorldHook hook ) { worldHook = std::move( hook ); }
    // /:SelItemType of the selection ("garage", "car"...), empty if none.
    const char *getSelectedType( const IntroCinematic &scene ) const;

    // The self view (docs/re/emotes_spec.md; the retail walk-avatar state
    // machine 6.3.3, states 5-7): the camera glides from the eye to a
    // third-person spot behind the player avatar, which stands at the body's
    // floor place (IntroCinematic::playPlayerEmote / startPlayerSelfView
    // pose it; setPlayerEmotePlace is fed here every frame). Movement and
    // selection are paused; endSelfView puts the first person camera back.
    // [INF: the type 10 camera law is not decoded, the offsets are ours.]
    void startSelfView( IntroCinematic &scene );
    void endSelfView( IntroCinematic &scene );
    inline bool isInSelfView() const { return bSelfView; }
    // The emote wheel (GENERAL.vmf /emotes, emote_wheel.h): the emote key
    // opens the self view and the wheel, the movie's choice plays a slot, its
    // cancel (B) leaves the self view. Call every frame from the host's
    // updateEmotes (the key is consumed here); true while the wheel owns the
    // input (the host skips its own emote keys).
    bool updateEmoteWheel( IntroCinematic &scene, float deltaTime );
    // 2D draw of the wheel (the movie), over the 3D walk.
    void drawEmoteWheel( Renderer2D &renderer );
    inline bool isEmoteWheelOpen() const { return wheel.isOpen(); }

private:
    WalkController controller;
    float fov = 1.0472f;
    float eyeHeight = 1.6f; // floor + 0.9 (proxy centre) + 0.7 [CODE, walk_mode.cpp]
    int32_t selected = -1;
    float cooldown = 0.0f;
    SelectableFilter selectableFilter;
    SelectionCallback selectionCallback;
    WorldHook worldHook;
    bool bCursorLabels = false;
    EmoteWheel wheel;
    bool bSelfView = false;
    float selfViewBlend = 0.0f; // 0: the eye pose, 1: the third person pose
    Vector3f selfViewEye;       // the pose the glide starts from

    Vector3f collide( const IntroCinematic &scene, const Vector3f &from, const Vector3f &to ) const;
    void updateSelection( IntroCinematic &scene, float deltaTime );
    void updateSelfView( IntroCinematic &scene, float deltaTime );
};
