#pragma once

#include "core/math.h"
#include "flash/flash_player.h"

#include <string>

class IntroCinematic;
class XmbDocument;
struct RenderTarget;
struct GPUTexture;

// The hall's rankings tube (docs/re/casino_spec.md 9.11): the time-attack
// screen (Entity__Actor__CasinoFlashScreen__CasinoFlashScreenRanking
// 'FlashScreen_CRNK_TimeAttack', IsTimeAttack 1) and its tube
// (TubeEcransRankings) behind the car podium. At rest both are under the floor
// (C_RANKINGS_STAND_CLOSED); the player coming near raises them
// (C_RANKINGS_OPEN, then C_RANKINGS_STAND_OPENED) and walking off lowers them
// (C_RANKINGS_CLOSE). The screen plays its movie (Movie 'Cas_Ranking',
// BitmapId 1) through a render target on the TD_NEONDECAL face.
class CasinoRankingScreen {
public:
    // The hall just loaded: finds the screen entity and parks the tube.
    bool attach( IntroCinematic &scene, const XmbDocument &entities );
    void detach();
    // walkPosition: the player (scene-local); bWalking: the walk drives it.
    void update( IntroCinematic &scene, const Vector3f &walkPosition, bool bWalking, float deltaTime );
    // The movie into its render target (before the 3D pass reads it).
    void drawOffscreen( IntroCinematic &scene );

    inline bool isAttached() const { return bAttached; }
    inline bool isRaised() const { return state == eState::Open || state == eState::Opening; }
    inline uint64_t getCameraHash() const { return cameraHash; }
    inline uint64_t getZoneHash() const { return zoneHash; }
    inline const Vector3f &getScreenPosition() const { return screenPosition; }

private:
    enum class eState : uint8_t { Closed = 0, Opening, Open, Closing };

    void play( IntroCinematic &scene, const char *pName, bool bLoop );

    bool bAttached = false;
    eState state = eState::Closed;
    std::string playing;
    Vector3f screenPosition;
    uint64_t screenHash = 0;
    uint64_t cameraHash = 0;
    uint64_t zoneHash = 0; // SelectableZone (an Entity__Volume)
    float openRadius = 6.0f;

    FlashPlayer movie;
    bool bMovieLoaded = false;
    RenderTarget *pTarget = nullptr;
    uint32_t movieFrames = 0;
    uint64_t stamp = 0x7a7e;
};
