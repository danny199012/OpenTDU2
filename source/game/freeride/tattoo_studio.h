#pragma once

#include "game/avatar/tattoo.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Renderer2D;

// The tattoo studio (a new feature, not in the original; docs/re/tattoo_spec.md). The camera circles the player's whole
// body and the view's centre is the crosshair: the host turns it into a skin (u, v) (EdenAvatar::pickSkin) and shows the
// working tattoos on the avatar live. Controls (pad / keyboard): left / right orbit the avatar, up / down move the aim up
// and down the body, A / Enter places the tattoo under the crosshair, X (option 1) next design, Y (option 2) next ink,
// B / Esc removes the last one placed (leaves when none), Start / Pause pays for the new tattoos and leaves.
// Size: LB / RB (keys V / C) or the right stick up / down; rotation: the right stick left / right (keys Q / E); remove the
// tattoo nearest the crosshair (worn or new, free): right stick click (key Delete).
class TattooStudio {
public:
    struct Host {
        std::function<void( float yaw, float height )> Aim;                    // move the view by these deltas
        std::function<bool( float &u, float &v )> Pick;                        // the skin under the crosshair
        std::function<void( const std::vector<TattooDecal> & )> Show;          // draw these tattoos on the avatar
        std::function<void()> Leave;                                           // the studio closed (restore the camera)
    };

    // owned: the tattoos the player already wears (saved in the profile).
    void open( const Host &newHost, const std::string &shopName );
    void close();
    inline bool isOpen() const { return bOpen; }
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );

    // The price of one new tattoo [INF: the retail game has no tattoos].
    static constexpr int32_t kTattooPrice = 1500;

private:
    void showWorking();
    void pay();
    int32_t addedCount() const;
    bool removeNearest();

    Host host;
    bool bOpen = false;
    std::string shopName;
    std::vector<std::string> designs;
    std::vector<TattooDecal> owned;   // saved
    std::vector<TattooDecal> working; // owned (minus the removed) + the new ones
    std::vector<char> isNew;          // parallel to working: 1 = bought this visit
    int32_t removed = 0;              // worn tattoos removed this visit
    float decalSize = 0.045f;         // half size of the next tattoo (fraction of the skin width)
    float decalRotation = 0.0f;       // degrees
    float shownSize = -1.0f, shownRotation = -1.0f;
    int32_t design = 0;
    int32_t ink = 0;
    bool bHit = false;
    float hitU = 0.0f, hitV = 0.0f;
    float shownU = -1.0f, shownV = -1.0f;
    int32_t shownDesign = -1, shownInk = -1;
    std::string message;
    float messageTimer = 0.0f;
};
