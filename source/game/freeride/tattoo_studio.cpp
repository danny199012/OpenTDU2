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

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

struct Ink {
    const char *Name;
    uint32_t Color; // 0xRRGGBB
};
const Ink kInks[] = { { "Black", 0x141414 },  { "Dark blue", 0x1c2a5a }, { "Red", 0x9a1a1a },
                      { "Green", 0x1f5a2c },  { "Purple", 0x4a1f6a },    { "Brown", 0x4a2c1a } };
constexpr float kDecalHalfSize = 0.045f; // of the skin texture's width
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
        ghost.U = hitU;
        ghost.V = hitV;
        ghost.Scale = kDecalHalfSize;
        ghost.Opacity = 0.55f;
        ghost.Color = kInks[ink].Color;
        shown.push_back( ghost );
    }
    host.Show( shown );
    shownU = bHit ? hitU : -1.0f;
    shownV = bHit ? hitV : -1.0f;
    shownDesign = design;
    shownInk = ink;
}

void TattooStudio::pay() {
    PlayerProfile &profile = GetPlayerProfile();
    const int32_t added = static_cast<int32_t>( working.size() - owned.size() );
    const int32_t price = added * kTattooPrice;
    if ( added <= 0 ) {
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
    OTDU_LOG_INFO( "TattooStudio: bought %d tattoo(s) for $%d (money now $%d)\n", added, price, profile.Money );
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
    bHit = host.Pick ? host.Pick( hitU, hitV ) : false;
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        if ( bHit ) {
            TattooDecal decal;
            decal.Design = designs[static_cast<size_t>( design )];
            decal.U = hitU;
            decal.V = hitV;
            decal.Scale = kDecalHalfSize;
            decal.Color = kInks[ink].Color;
            working.push_back( decal );
            OTDU_LOG_INFO( "TattooStudio: placed '%s' at u %.3f v %.3f (%zu worn + %zu new)\n", decal.Design.c_str(), hitU, hitV,
                           owned.size(), working.size() - owned.size() );
            PlayFrontEndCue( "valid" );
            shownDesign = -1; // refresh
        } else {
            PlayFrontEndCue( "wrong" );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        pay();
        return;
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        if ( working.size() > owned.size() ) {
            working.pop_back();
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
    if ( bMoved || design != shownDesign || ink != shownInk ) {
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
    const float x0 = 40.0f, y0 = 90.0f, w = 400.0f, h = 330.0f;
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
    const int32_t added = static_cast<int32_t>( working.size() - owned.size() );
    const std::string worn = "Worn: " + std::to_string( owned.size() ) + "   New: " + std::to_string( added );
    renderer.drawText( pFont, worn.c_str(), x0 + 24.0f, y, 22.0f, Rgba( 255, 255, 255 ) );
    y += 34.0f;
    const int32_t total = added * kTattooPrice;
    renderer.drawText( pFont, ( "Total " + FormatMoney( total ) ).c_str(), x0 + 24.0f, y, 24.0f,
                       profile.Money >= total ? Rgba( 255, 210, 80 ) : Rgba( 255, 110, 110 ) );
    if ( messageTimer > 0.0f ) {
        renderer.drawText( pFont, message.c_str(), x0 + w * 0.5f, y0 + h - 100.0f, 22.0f, Rgba( 255, 210, 80 ), eTextAlign::TA_Center );
    }
    renderer.drawText( pFont, "Left / Right: turn   Up / Down: aim", x0 + w * 0.5f, y0 + h - 78.0f, 17.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.drawText( pFont, "A: place   X: design   Y: ink", x0 + w * 0.5f, y0 + h - 56.0f, 17.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.drawText( pFont, "B: undo / leave   Start: pay and leave", x0 + w * 0.5f, y0 + h - 34.0f, 17.0f, Rgba( 210, 210, 210 ),
                       eTextAlign::TA_Center );
    renderer.end();
}
