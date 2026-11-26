#include "shared.h"
#include "tattoo_studio.h"

#include "audio/frontend_audio.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <SDL3/SDL.h>

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

struct Ink {
    const char *Name;
    uint32_t Color; // 0xRRGGBB
};
const Ink kInks[] = { { "Black", 0x141414 },  { "Dark blue", 0x1c2a5a }, { "Red", 0x9a1a1a },
                      { "Green", 0x1f5a2c },  { "Purple", 0x4a1f6a },    { "Brown", 0x4a2c1a } };
constexpr float kDecalHalfSize = 0.045f; // of the skin texture's width
constexpr float kMinSize = 0.02f, kMaxSize = 0.12f;
constexpr float kSizeSpeed = 0.06f;      // per second
constexpr float kRotateSpeed = 120.0f;   // degrees per second
constexpr float kOrbitSpeed = 1.4f;      // rad / s
constexpr float kAimSpeed = 0.6f;        // m / s

std::string FormatMoney( int32_t value ) {
    std::string digits = std::to_string( value < 0 ? -value : value );
    std::string out;
    for ( size_t i = 0; i < digits.size(); i++ ) {
        if ( i > 0 && ( digits.size() - i ) % 3 == 0 ) {
            out += ',';
        }
        out += digits[i];
    }
    return ( value < 0 ? "-$" : "$" ) + out;
}
} // namespace

void TattooStudio::open( const Host &newHost, const std::string &newShopName ) {
    host = newHost;
    shopName = newShopName;
    designs = { "star", "ring", "cross", "bars", "tribal" };
    // <userdata>/tattoos/*.png: the player's own designs
    if ( gpFile != nullptr ) {
        std::error_code error;
        for ( const auto &entry : std::filesystem::directory_iterator( gpFile->getUserDataFolder() / "tattoos", error ) ) {
            if ( entry.path().extension() == ".png" ) {
                designs.push_back( entry.path().stem().string() );
            }
        }
    }
    owned = ParseTattoos( GetPlayerProfile().Tattoos );
    working = owned;
    isNew.assign( owned.size(), 0 );
    removed = 0;
    decalSize = kDecalHalfSize;
    decalRotation = 0.0f;
    shownSize = shownRotation = -1.0f;
    design = 0;
    ink = 0;
    bHit = false;
    shownU = shownV = -1.0f;
    shownDesign = shownInk = -1;
    message.clear();
    messageTimer = 0.0f;
    bOpen = true;
    OTDU_LOG_INFO( "TattooStudio: '%s' open, %zu design(s), %zu tattoo(s) worn\n", shopName.c_str(), designs.size(), owned.size() );
    PlayFrontEndCue( "mopen" );
}

void TattooStudio::close() {
    if ( !bOpen ) {
        return;
    }
    bOpen = false;
    if ( host.Show ) {
        host.Show( owned ); // the avatar goes back to what it wears
    }
    if ( host.Leave ) {
        host.Leave();
    }
}

void TattooStudio::showWorking() {
    if ( !host.Show ) {
        return;
    }
    std::vector<TattooDecal> shown = working;
    if ( bHit ) { // the ghost: where the next tattoo would go
        TattooDecal ghost;
        ghost.Design = designs[static_cast<size_t>( design )];
        ghost.Head = bHitHead;
        ghost.Side = bHitSide;
        ghost.U = hitU;
        ghost.V = hitV;
        ghost.Scale = decalSize;
        ghost.Rotation = decalRotation;
        ghost.Opacity = 0.55f;
        ghost.Color = kInks[ink].Color;
        shown.push_back( ghost );
    }
    host.Show( shown );
    shownU = bHit ? hitU : -1.0f;
    shownV = bHit ? hitV : -1.0f;
    shownDesign = design;
    shownInk = ink;
    shownSize = decalSize;
    shownRotation = decalRotation;
    shownSide = bHitSide;
}

int32_t TattooStudio::addedCount() const {
    int32_t count = 0;
    for ( char flag : isNew ) {
        count += flag != 0 ? 1 : 0;
    }
    return count;
}

// Removes the tattoo (worn or new) nearest the crosshair's skin point; false when none is within reach.
bool TattooStudio::removeNearest() {
    if ( !bHit ) {
        return false;
    }
    int32_t best = -1;
    float bestDistance = 1e9f;
    for ( size_t i = 0; i < working.size(); i++ ) {
        if ( working[i].Head != bHitHead || ( working[i].Side != 0 && bHitSide != 0 && working[i].Side != bHitSide ) ) {
            continue;
        }
        const float du = working[i].U - hitU, dv = working[i].V - hitV;
        const float distance = std::sqrt( du * du + dv * dv );
        if ( distance < std::max( 0.04f, working[i].Scale * 2.0f ) && distance < bestDistance ) {
            bestDistance = distance;
            best = static_cast<int32_t>( i );
        }
    }
    if ( best < 0 ) {
        return false;
    }
    const size_t index = static_cast<size_t>( best );
    if ( isNew[index] == 0 ) {
        removed++;
    }
    OTDU_LOG_INFO( "TattooStudio: removed '%s' at u %.3f v %.3f (%s)\n", working[index].Design.c_str(), working[index].U, working[index].V,
                   isNew[index] != 0 ? "new" : "worn" );
    working.erase( working.begin() + best );
    isNew.erase( isNew.begin() + best );
    return true;
}

void TattooStudio::pay() {
    PlayerProfile &profile = GetPlayerProfile();
    const int32_t added = addedCount();
    const int32_t price = added * kTattooPrice;
    if ( added <= 0 && removed <= 0 ) {
        close();
        return;
    }
    if ( profile.Money < price ) {
        message = "Not enough money";
        messageTimer = 3.0f;
        PlayFrontEndCue( "wrong" );
        return;
    }
    profile.Money -= price;
    profile.Tattoos = SerializeTattoos( working );
    owned = working;
    OTDU_LOG_INFO( "TattooStudio: bought %d tattoo(s), removed %d, for $%d (money now $%d)\n", added, removed, price, profile.Money );
    SaveProfile( "tattoo bought" );
    PlayFrontEndCue( "valid" );
    close();
}

void TattooStudio::tick( float deltaTime ) {
    if ( !bOpen ) {
        return;
    }
    messageTimer = std::max( 0.0f, messageTimer - deltaTime );
    float yaw = 0.0f, height = 0.0f;
    if ( gpInput->isDown( eInputAction::IA_MenuLeft ) ) {
        yaw += kOrbitSpeed * deltaTime;
    }
    if ( gpInput->isDown( eInputAction::IA_MenuRight ) ) {
        yaw -= kOrbitSpeed * deltaTime;
    }
    if ( gpInput->isDown( eInputAction::IA_MenuUp ) ) {
        height += kAimSpeed * deltaTime;
    }
    if ( gpInput->isDown( eInputAction::IA_MenuDown ) ) {
        height -= kAimSpeed * deltaTime;
    }
    // autoplay-friendly: a press with no hold still moves one step
    if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        yaw += 0.12f;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        yaw -= 0.12f;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ) {
        height += 0.06f;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ) {
        height -= 0.06f;
    }
    // Size and rotation of the next tattoo (LB / RB or V / C, the right stick, Q / E).
    float sizeDelta = 0.0f, turn = 0.0f;
    if ( gpInput->isDown( eInputAction::IA_CarCamera ) ) {
        sizeDelta += kSizeSpeed * deltaTime;
    }
    if ( gpInput->isDown( eInputAction::IA_CarBackView ) ) {
        sizeDelta -= kSizeSpeed * deltaTime;
    }
    if ( gpInput->wasPressed( eInputAction::IA_CarCamera ) ) {
        sizeDelta += 0.005f;
    }
    if ( gpInput->wasPressed( eInputAction::IA_CarBackView ) ) {
        sizeDelta -= 0.005f;
    }
    if ( std::fabs( gpInput->getRightStickY() ) > 0.3f ) {
        sizeDelta -= gpInput->getRightStickY() * kSizeSpeed * deltaTime;
    }
    if ( std::fabs( gpInput->getRightStickX() ) > 0.3f ) {
        turn += gpInput->getRightStickX() * kRotateSpeed * deltaTime;
    }
    if ( gpInput->isKeyDown( SDL_SCANCODE_E ) ) {
        turn += kRotateSpeed * deltaTime;
    }
    if ( gpInput->isKeyDown( SDL_SCANCODE_Q ) ) {
        turn -= kRotateSpeed * deltaTime;
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_E ) ) {
        turn += 15.0f;
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_Q ) ) {
        turn -= 15.0f;
    }
    decalSize = std::clamp( decalSize + sizeDelta, kMinSize, kMaxSize );
    decalRotation = std::fmod( decalRotation + turn + 360.0f, 360.0f );
    if ( host.Aim && ( yaw != 0.0f || height != 0.0f ) ) {
        host.Aim( yaw, height );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuOption1 ) ) {
        design = ( design + 1 ) % static_cast<int32_t>( designs.size() );
        PlayFrontEndCue( "move" );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuOption2 ) ) {
        ink = ( ink + 1 ) % static_cast<int32_t>( sizeof( kInks ) / sizeof( kInks[0] ) );
        PlayFrontEndCue( "move" );
    }
    if ( gpInput->wasPressed( eInputAction::IA_Map ) && host.Focus ) { // Select / M: face close-up <-> whole body
        bFace = !bFace;
        host.Focus( bFace );
        shownU = shownV = -1.0f;
        PlayFrontEndCue( "move" );
    }
    bHit = host.Pick ? host.Pick( hitU, hitV, bHitHead, bHitSide ) : false;
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        if ( bHit ) {
            TattooDecal decal;
            decal.Design = designs[static_cast<size_t>( design )];
            decal.Head = bHitHead;
            decal.Side = bHitSide;
            decal.U = hitU;
            decal.V = hitV;
            decal.Scale = decalSize;
            decal.Rotation = decalRotation;
            decal.Color = kInks[ink].Color;
            working.push_back( decal );
            isNew.push_back( 1 );
            OTDU_LOG_INFO( "TattooStudio: placed '%s' at u %.3f v %.3f side %d (%zu worn + %zu new)\n", decal.Design.c_str(), hitU, hitV, bHitSide,
                           owned.size(), static_cast<size_t>( addedCount() ) );
            PlayFrontEndCue( "valid" );
            shownDesign = -1; // refresh
        } else {
            PlayFrontEndCue( "wrong" );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_CarHorn ) || gpInput->wasKeyPressed( SDL_SCANCODE_DELETE ) ) {
        if ( removeNearest() ) {
            shownDesign = -1;
            PlayFrontEndCue( "back" );
        } else {
            OTDU_LOG_INFO( "TattooStudio: nothing to remove at u %.3f v %.3f (hit %d, %zu tattoo(s))\n", hitU, hitV, bHit ? 1 : 0, working.size() );
            PlayFrontEndCue( "wrong" );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        pay();
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        if ( addedCount() > 0 ) {
            // undo the last tattoo placed this visit
            size_t last = working.size();
            while ( last > 0 && isNew[last - 1] == 0 ) {
                last--;
            }
            working.erase( working.begin() + ( last - 1 ) );
            isNew.erase( isNew.begin() + ( last - 1 ) );
            shownDesign = -1;
            PlayFrontEndCue( "back" );
        } else {
            close();
            PlayFrontEndCue( "back" );
            return;
        }
    }
    // Recomposite only when something visible changed (the skin is re-uploaded each time).
    const bool bMoved = std::fabs( hitU - shownU ) > 0.004f || std::fabs( hitV - shownV ) > 0.004f ||
                        ( bHit != ( shownU >= 0.0f ) );
    if ( bMoved || design != shownDesign || ink != shownInk || decalSize != shownSize || decalRotation != shownRotation || bHitSide != shownSide ) {
        showWorking();
    }
}

void TattooStudio::draw( Renderer2D &renderer ) {
    if ( !bOpen ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    const PlayerProfile &profile = GetPlayerProfile();
    renderer.begin( 1280.0f, 720.0f );
    // The crosshair: green on the skin, red off the body.
    const uint32_t cross = bHit ? Rgba( 120, 255, 120 ) : Rgba( 255, 90, 90 );
    renderer.drawRect( Rect2D( 640.0f - 14.0f, 359.0f, 28.0f, 2.0f ), cross );
    renderer.drawRect( Rect2D( 639.0f, 346.0f, 2.0f, 28.0f ), cross );
    // The side panel.
    const float x0 = 40.0f, y0 = 90.0f, w = 400.0f, h = 390.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 22, 26, 34, 225 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 200, 200, 200, 255 ) );
    renderer.drawRect( Rect2D( x0, y0, w, 52.0f ), Rgba( 34, 52, 84, 255 ) );
    renderer.drawText( pFont, shopName.c_str(), x0 + 20.0f, y0 + 10.0f, 28.0f, Rgba( 255, 255, 255 ) );
    renderer.drawText( pFont, FormatMoney( profile.Money ).c_str(), x0 + w - 20.0f, y0 + 62.0f, 22.0f, Rgba( 120, 255, 120 ),
                       eTextAlign::TA_Right );
    float y = y0 + 66.0f;
    const std::string designLine = "Design: " + designs[static_cast<size_t>( design )];
    renderer.drawText( pFont, designLine.c_str(), x0 + 24.0f, y, 22.0f, Rgba( 255, 255, 255 ) );
    y += 34.0f;
    renderer.drawText( pFont, ( std::string( "Ink: " ) + kInks[ink].Name ).c_str(), x0 + 24.0f, y, 22.0f, Rgba( 255, 255, 255 ) );
    const uint32_t inkColor = kInks[ink].Color;
    renderer.drawRect( Rect2D( x0 + w - 60.0f, y, 36.0f, 22.0f ), Rgba( ( inkColor >> 16 ) & 255, ( inkColor >> 8 ) & 255, inkColor & 255 ) );
    y += 34.0f;
    char sizeLine[64];
    snprintf( sizeLine, sizeof( sizeLine ), "Size: %.0f%%   Rotation: %.0f deg", decalSize / kDecalHalfSize * 100.0f, decalRotation );
    renderer.drawText( pFont, sizeLine, x0 + 24.0f, y, 22.0f, Rgba( 255, 255, 255 ) );
    y += 34.0f;
    const int32_t added = addedCount();
    const std::string worn = "Worn: " + std::to_string( static_cast<int32_t>( owned.size() ) - removed ) + "   New: " + std::to_string( added ) +
                             "   Removed: " + std::to_string( removed );
    renderer.drawText( pFont, worn.c_str(), x0 + 24.0f, y, 22.0f, Rgba( 255, 255, 255 ) );
    y += 34.0f;
    const int32_t total = added * kTattooPrice;
    renderer.drawText( pFont, ( "Total " + FormatMoney( total ) ).c_str(), x0 + 24.0f, y, 24.0f,
                       profile.Money >= total ? Rgba( 255, 210, 80 ) : Rgba( 255, 110, 110 ) );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 150.0f, 22.0f, Rgba( 255, 210, 80 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, "Left / Right: turn   Up / Down: aim", x0 + w * 0.5f, y0 + h - 122.0f, 17.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.drawText( pFont, "A: place   X: design   Y: ink", x0 + w * 0.5f, y0 + h - 100.0f, 17.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.drawText( pFont, "LB / RB: size   R stick: size / rotate", x0 + w * 0.5f, y0 + h - 78.0f, 17.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.drawText( pFont, "R stick click / Del: remove   B: undo", x0 + w * 0.5f, y0 + h - 56.0f, 17.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.drawText( pFont, bFace ? "Select / M: whole body   Start: pay" : "Select / M: face   Start: pay and leave", x0 + w * 0.5f, y0 + h - 34.0f, 17.0f, Rgba( 210, 210, 210 ), eTextAlign::TA_Center );
    renderer.end();
}
