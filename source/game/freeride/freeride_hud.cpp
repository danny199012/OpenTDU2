#include "shared.h"
#include "freeride_hud.h"
#include "core/path_utf8.h"

#include "database/gs_database.h"
#include "flash/flash_localization.h"
#include "game/freeride/cruising_zones.h"
#include "game/game_options.h"
#include "game/pause_menu.h"
#include "game/player_profile.h"
#include "game/units.h"
#include "input/gs_input.h"
#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "flash/flash_movie.h"
#include "flash/gs_flash.h"
#include "render/gs_render.h"
#include "render/image_file.h"
#include "render/texture_cache.h"
#include "physics/vehicle_collision.h"
#include "render/font.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"

#include "stb_image_write.h"

#include <chrono>
#include <set>

namespace {

// -loghudregion: every instance of a movie whose world position lands in a
// stage region (px), with its path - the probe that names a stray element on
// screen (the same idea as the pause menu's -logpause box probe).
void DumpHudRegion( const FlashPlayer::Clip &clip, const FlashDefinitions &definitions, const FlashMatrix &world,
                    const std::string &path, float minX, float maxX, float minY, float maxY ) {
    for ( const auto &entry : clip.DisplayList ) {
        const FlashPlayer::Instance &instance = *entry.second;
        const FlashMatrix instanceWorld = world * instance.Matrix;
        const float px = instanceWorld.TX / 20.0f;
        const float py = instanceWorld.TY / 20.0f;
        const std::string childPath =
            path + "/" + ( instance.Name.empty() ? std::to_string( entry.first ) : instance.Name );
        // The instance's own origin, or the box its shape actually covers (a
        // shape parked off stage can still reach into the region).
        bool bHit = px >= minX && px <= maxX && py >= minY && py <= maxY;
        float bx0 = 0.0f, by0 = 0.0f, bx1 = 0.0f, by1 = 0.0f;
        if ( !bHit ) {
            if ( const FlashShape *pShape = definitions.getShape( instance.CharacterId ) ) {
                bx0 = by0 = 1e9f;
                bx1 = by1 = -1e9f;
                for ( const FlashShapeMesh &mesh : pShape->Meshes ) {
                    for ( uint32_t index : mesh.Indices ) {
                        float x = 0.0f, y = 0.0f;
                        instanceWorld.transform( mesh.Positions[index * 2], mesh.Positions[index * 2 + 1], x, y );
                        bx0 = Min( bx0, x / 20.0f );
                        by0 = Min( by0, y / 20.0f );
                        bx1 = Max( bx1, x / 20.0f );
                        by1 = Max( by1, y / 20.0f );
                    }
                }
                bHit = bx1 > minX && bx0 < maxX && by1 > minY && by0 < maxY;
            }
        }
        if ( bHit ) {
            OTDU_LOG_INFO( "HudRegion: '%s' id %u at stage (%.0f, %.0f) scale (%.2f, %.2f) visible %d box "
                           "(%.0f, %.0f)-(%.0f, %.0f)\n",
                           childPath.c_str(), instance.CharacterId, px, py, instanceWorld.A, instanceWorld.D,
                           instance.bVisible ? 1 : 0, bx0, by0, bx1, by1 );
        }
        if ( instance.pClip != nullptr ) {
            DumpHudRegion( *instance.pClip, definitions, instanceWorld, childPath, minX, maxX, minY, maxY );
        }
    }
}

// -loghudtimes: CPU time of each part of FreeRideHud::draw, averaged and
// logged every 120 frames. The HUD is a Flash display list traversed every
// frame; this says which part of it is the CPU cost (docs/PERF_PLAN.md).
struct HudTimes {
    enum eStep { Flash, Minimap, Gauge, ZoneFuel, Prompt, GaugeTex, Gauges, General, Count };
    double Sum[Count] = {};
    uint32_t Frames = 0;
    std::chrono::steady_clock::time_point Last;
    void start() { Last = std::chrono::steady_clock::now(); }
    void mark( eStep step ) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        Sum[step] += std::chrono::duration<double, std::milli>( now - Last ).count();
        Last = now;
    }
    void endFrame() {
        if ( ++Frames < 120 ) {
            return;
        }
        OTDU_LOG_INFO( "[perf] hud: flash %.1f  minimap %.1f  gauge %.1f  zone/fuel %.1f  prompt %.1f  "
                       "gauge tex %.1f  gauges %.1f  general %.1f ms\n",
                       Sum[Flash] / Frames, Sum[Minimap] / Frames, Sum[Gauge] / Frames,
                       Sum[ZoneFuel] / Frames, Sum[Prompt] / Frames, Sum[GaugeTex] / Frames,
                       Sum[Gauges] / Frames, Sum[General] / Frames );
        *this = HudTimes();
    }
};
} // namespace

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

void PushTriangle( std::vector<Vertex2D> &out, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color ) {
    out.push_back( { x0, y0, 0.0f, 0.0f, color } );
    out.push_back( { x1, y1, 0.0f, 0.0f, color } );
    out.push_back( { x2, y2, 0.0f, 0.0f, color } );
}

void PushQuad( std::vector<Vertex2D> &out, const Vector2f &a, const Vector2f &b, const Vector2f &c, const Vector2f &d,
               uint32_t color ) {
    PushTriangle( out, a.x, a.y, b.x, b.y, c.x, c.y, color );
    PushTriangle( out, a.x, a.y, c.x, c.y, d.x, d.y, color );
}

// Arc band between radii r0 / r1 from angle a0 to a1 (radians, screen: 0 =
// right, positive = clockwise since y points down).
void PushArc( std::vector<Vertex2D> &out, float cx, float cy, float r0, float r1, float a0, float a1, uint32_t color ) {
    const int32_t segments = Max( 2, static_cast<int32_t>( std::abs( a1 - a0 ) / 0.05f ) );
    for ( int32_t i = 0; i < segments; i++ ) {
        const float t0 = a0 + ( a1 - a0 ) * i / segments, t1 = a0 + ( a1 - a0 ) * ( i + 1 ) / segments;
        PushQuad( out, { cx + std::cos( t0 ) * r0, cy + std::sin( t0 ) * r0 }, { cx + std::cos( t0 ) * r1, cy + std::sin( t0 ) * r1 },
                  { cx + std::cos( t1 ) * r1, cy + std::sin( t1 ) * r1 }, { cx + std::cos( t1 ) * r0, cy + std::sin( t1 ) * r0 },
                  color );
    }
}

// Reference screenshot layout (1280 x 720 canvas) [INF: GAUGES.vmf missing].
constexpr float kGaugeX = 1107.0f, kGaugeY = 570.0f, kGaugeRadius = 84.0f;
constexpr float kMapMetersPerPixel = 3.0f; // [INF]
} // namespace

bool FreeRideHud::load( const std::string &hudFileName ) {
    unload();
    auto onCommand = [ this ]( FlashPlayer &player ) {
        return [ this, &player ]( const std::string &command, const std::string & ) {
            if ( command == "sbdd" ) {
                ApplyFlashLocalization( player );
            } else if ( command.compare( 0, 5, "frgp:" ) == 0 ) {
                // The FRIM gauge bank animation (hud.bnk fr_gge/bank) tells the
                // game when to pay ("bank") and when it is done ("end_bank"):
                // routed to RiskDriving::onBankFSCommand (the fallback pays
                // 5 s after a level-up when the movie never asks).
                if ( bankCommandCallback_ ) {
                    bankCommandCallback_( command );
                } else {
                    OTDU_LOG_INFO( "FreeRideHud: FRIM bank FSCommand '%s'\n", command.c_str() );
                }
            } else if ( command.compare( 0, 5, "chmo:" ) == 0 ) {
                // The help system (helpSystemClosed): the Chase Mode help
                // popup closed itself (clos).
                if ( command == "chmo:hsys:clos" ) {
                    bChaseHelpOpen_ = false;
                    chaseHelpHide_ = 0.8f; // the popup's 'close' section plays out
                    OTDU_LOG_INFO( "FreeRideHud: Chase Mode help closed\n" );
                }
            }
        };
    };
    hud.setFSCommandCallback( onCommand( hud ) );
    general.setFSCommandCallback( onCommand( general ) );
    bool bHud = false;
    if ( FlashMovie *pMovie = gpFlash->getMovie( "hud" ) ) {
        // The dealer tiles' brand logos (Spot/ic/logo/lure) are cells of the
        // brands_map page of the logo bank [DATA] - known before the player
        // builds its fills (the map screen does the same).
        pMovie->addExtraPages( "FrontEnd/AllRes/LogoTexturePage.bnk" );
        bHud = hud.load( pMovie );
        ApplyFlashLocalization( hud );
        // The /HUD clip carries the screen-ratio arrangements (labels '16_9'
        // f1 / '4_3' f8 — the same WhatFormat choice GAUGES takes): the retail
        // game stops it on the ratio's label. Left playing it re-placed every
        // element each loop — the enter tile's prompt, plate and button
        // blinked off and back, twice a second.
        applyScreenRatio();
        // FREERIDE: the root clip HUD and its minimap frame shown (the root
        // `default` action hides them) [INF].
        hud.setClipVisible( "/HUD", true );
        hud.setClipVisible( "/HUD/minimap", true );
        // The map surface: mmap_lure is the solid quad the retail engine
        // renders the live map into — hidden here, the native map draws under
        // the movie at the same rect and the bezel / band / icons overlay it.
        hud.setClipVisible( "/HUD/minimap/mmap_lure", false );
        if ( computeMapRect() ) {
            // The player marker is the movie's own cursor clip (the retail
            // engine positions it): parked on the map centre — the heading-up
            // map keeps the car there. The movie draws it over the band, under
            // the inner frame, like the retail layering.
            float localX = 0.0f, localY = 0.0f;
            if ( canvasToMinimap( mapRectX + 0.5f * mapRectW, mapRectY + 0.62f * mapRectH, localX, localY ) ) {
                hud.setClipTransform( "/HUD/minimap/cursor", localX, localY, 0.0f, 100.0f, 100.0f );
            }
        }
        // The back on track hold element is drawn by the game (showBackOnTrack)
        // while the button is held, not by the movie's own state.
        hud.setClipVisible( "/HUD/ig_menu/backontrack", false );
        bBackOnTrackShown = false;
        // The radio panel and the car menu are drawn by the game while the
        // D-pad opened them.
        hud.setClipVisible( "/HUD/radio", false );
        bRadioShown = false;
        hud.setClipVisible( "/HUD/ig_menu", false );
        bCarMenuShown = false;
        // The frontend's own help bar and help popup are root clips of the same
        // movie (the `default` root frame hides them, and the port never plays
        // that frame): they belong to the menus, not to the driving screen.
        hud.setClipVisible( "/helpmore", false );
        hud.setClipVisible( "/help_system", false );
        if ( CmdLine::HasArg( "-dumphud" ) ) {
            dumpClips( *hud.getRoot(), "", 0 );
            // The root timeline itself (its per-frame placements are what the
            // playing root re-applies).
            {
                const FlashTimeline &rootTimeline = hud.getDefinitions().getRootTimeline();
                for ( const auto &label : rootTimeline.Labels ) {
                    OTDU_LOG_INFO( "HUD root label '%s' frame %u\n", label.first.c_str(), label.second );
                }
                for ( size_t f = 0; f < rootTimeline.Frames.size(); f++ ) {
                    for ( const FlashControlTag &tag : rootTimeline.Frames[f] ) {
                        if ( tag.Type == eFlashControlType::CT_Place ) {
                            OTDU_LOG_INFO( "HUD root f%zu place depth %u char %u move %d name '%s'\n", f, tag.Place.Depth,
                                           tag.Place.bHasCharacter ? tag.Place.CharacterId : 0, tag.Place.bMove ? 1 : 0,
                                           tag.Place.Name.c_str() );
                        } else if ( tag.Type == eFlashControlType::CT_Action ) {
                            OTDU_LOG_INFO( "HUD root f%zu action len %u\n", f, tag.ActionLength );
                        }
                    }
                }
            }
            // The root user functions (SetHelp / HelpFrame / Change...): their
            // bodies are the movie protocol the game drives them with.
            DumpFlashFunctions( hud );
            // Timeline of a sprite (-dumpsprite <id> or comma list).
            FlashDefinitions definitions;
            definitions.build( pMovie->getVMF(), pMovie );
            std::string spriteList = "150";
            if ( const char *pList = CmdLine::GetArgValue( "-dumpsprite" ) ) {
                spriteList = pList;
            }
            size_t pos = 0;
            while ( pos <= spriteList.size() ) {
                const size_t comma = spriteList.find( ',', pos );
                const std::string token =
                    spriteList.substr( pos, comma == std::string::npos ? std::string::npos : comma - pos );
                pos = comma == std::string::npos ? spriteList.size() + 1 : comma + 1;
                if ( token.empty() ) {
                    continue;
                }
                const uint16_t spriteId = static_cast<uint16_t>( atoi( token.c_str() ) );
                if ( const FlashTimeline *pSprite = definitions.getSprite( spriteId ) ) {
                    for ( const auto &label : pSprite->Labels ) {
                        OTDU_LOG_INFO( "HUD sprite label '%s' frame %u\n", label.first.c_str(), label.second );
                    }
                    for ( size_t f = 0; f < pSprite->Frames.size(); f++ ) {
                        for ( const FlashControlTag &tag : pSprite->Frames[f] ) {
                            if ( tag.Type == eFlashControlType::CT_Place ) {
                                OTDU_LOG_INFO( "HUD sprite f%zu place depth %u char %u move %d name '%s' at (%.1f, %.1f) scale (%.2f, %.2f)\n",
                                               f, tag.Place.Depth, tag.Place.bHasCharacter ? tag.Place.CharacterId : 0,
                                               tag.Place.bMove ? 1 : 0, tag.Place.Name.c_str(), tag.Place.Matrix.TX / 20.0f,
                                               tag.Place.Matrix.TY / 20.0f, tag.Place.Matrix.A, tag.Place.Matrix.D );
                            } else if ( tag.Type == eFlashControlType::CT_Remove ) {
                                OTDU_LOG_INFO( "HUD sprite f%zu remove depth %u\n", f, tag.Depth );
                            } else if ( tag.Type == eFlashControlType::CT_Action ) {
                                OTDU_LOG_INFO( "HUD sprite f%zu action at 0x%x len %u\n", f, tag.ActionOffset, tag.ActionLength );
                                if ( CmdLine::HasArg( "-dumpactionbytes" ) ) {
                                    const std::vector<uint8_t> &vmfData = pMovie->getVMF().getData();
                                    std::string hex;
                                    for ( uint32_t b = 0; b < tag.ActionLength && tag.ActionOffset + b < vmfData.size(); b++ ) {
                                        char byteText[4];
                                        snprintf( byteText, sizeof( byteText ), "%02x", vmfData[tag.ActionOffset + b] );
                                        hex += byteText;
                                    }
                                    OTDU_LOG_INFO( "HUD sprite f%zu bytes %s\n", f, hex.c_str() );
                                }
                            }
                        }
                    }
                }
                // The character's own art (shape bounds / fills) when it is a shape.
                if ( definitions.getType( spriteId ) == eFlashCharacterType::CT_Shape ) {
                    if ( const FlashShape *pShape = definitions.getShape( spriteId ) ) {
                        size_t verts = 0;
                        for ( const FlashShapeMesh &mesh : pShape->Meshes ) {
                            verts += mesh.Indices.size();
                        }
                        OTDU_LOG_INFO( "HUD sprite %u SHAPE bounds (%.1f..%.1f, %.1f..%.1f) px fills %zu tris %zu\n", spriteId,
                                       pShape->XMin / 20.0f, pShape->XMax / 20.0f, pShape->YMin / 20.0f, pShape->YMax / 20.0f,
                                       pShape->Fills.size(), verts / 3 );
                        for ( size_t fi = 0; fi < pShape->Fills.size(); fi++ ) {
                            const FlashFill &fill = pShape->Fills[fi];
                            if ( fill.Type == eFlashFillType::FF_Solid ) {
                                OTDU_LOG_INFO( "HUD sprite %u fill %zu solid %08x\n", spriteId, fi, fill.Color );
                            } else if ( fill.Type == eFlashFillType::FF_Bitmap ) {
                                OTDU_LOG_INFO( "HUD sprite %u fill %zu bitmap %u uv (%.3f..%.3f, %.3f..%.3f) %gx%g\n", spriteId, fi,
                                               fill.BitmapId, fill.U0, fill.U1, fill.V0, fill.V1, fill.BitmapWidth,
                                               fill.BitmapHeight );
                            }
                        }
                    }
                }
            }
            definitions.destroy();
        }
    } else {
        OTDU_LOG_WARN( "FreeRideHud: HUD movie (frontend/hires/hud.bnk) not found\n" );
    }
    if ( FlashMovie *pMovie = gpFlash->getMovie( "general" ) ) {
        general.load( pMovie );
        ApplyFlashLocalization( general );
    }
    // GAUGES (FUN_00965810): /:WhatFormat "4_3" / "16_9" (DAT_00fe4c78
    // [INF: the 16:9 flag]), /:Gear "0", Speed_c/d/u digits, /:Km, /:Centaine.
    if ( FlashMovie *pMovie = gpFlash->getMovie( "gauges" ) ) {
        gauges.setFSCommandCallback( onCommand( gauges ) );
        if ( gauges.load( pMovie ) ) {
            ApplyFlashLocalization( gauges );
            gaugeDefinitions.build( pMovie->getVMF(), pMovie );
            // WhatFormat (the GAUGES 16:9 / 4:3 arrangement) before the root
            // 'on' plays; the second call is a no-op for the HUD movie.
            applyScreenRatio();
            gauges.setVariable( "/:Gear", "0" );
            // Root label "on" (HUD fade in) [INF: the movie enable call
            // FUN_00d7fbe0 is not followed].
            gauges.gotoLabel( "/", "on", true );
            loadGaugeBank( hudFileName );
        }
    } else {
        OTDU_LOG_WARN( "FreeRideHud: GAUGES movie (frontend/hires/gauges.bnk) not found: placeholder gauge [INF]\n" );
    }
    return bHud;
}

float FreeRideHud::hudStageWidth() const {
    const FlashMovie *pMovie = hud.getMovie();
    return pMovie != nullptr ? pMovie->getVMF().getStageWidth() : 1280.0f;
}

float FreeRideHud::hudStageHeight() const {
    const FlashMovie *pMovie = hud.getMovie();
    return pMovie != nullptr ? pMovie->getVMF().getStageHeight() : 960.0f;
}

void FreeRideHud::applyScreenRatio() {
    // The screen-ratio arrangements of the two HUD movies (the /HUD label
    // stop and the GAUGES /:WhatFormat variable), from the effective ratio
    // (the override option, else the window aspect). Re-run when the option
    // changes (PauseMenu's Screen ratio row): the labels re-place the
    // elements, the map rect follows.
    const int32_t ratio = GetEffectiveScreenRatio();
    const bool b169 = ratio == 1;
    if ( hud.isLoaded() ) {
        hud.gotoLabel( "/HUD", b169 ? "16_9" : "4_3", false );
    }
    if ( gauges.isLoaded() ) {
        gauges.setVariable( "/:WhatFormat", b169 ? "16_9" : "4_3" );
        // The dials read WhatFormat when their frames are placed: re-run the
        // root 'on' so a live Screen ratio change re-places them (the game
        // drives the digits / gear every frame, nothing else is lost).
        if ( appliedScreenRatio_ != 0 && appliedScreenRatio_ != ratio ) {
            gauges.gotoLabel( "/", "on", true );
        }
    }
    appliedScreenRatio_ = ratio;
    if ( computeMapRect() ) {
        float localX = 0.0f, localY = 0.0f;
        if ( canvasToMinimap( mapRectX + 0.5f * mapRectW, mapRectY + 0.62f * mapRectH, localX, localY ) ) {
            hud.setClipTransform( "/HUD/minimap/cursor", localX, localY, 0.0f, 100.0f, 100.0f );
        }
    }
}

void FreeRideHud::unload() {
    hud.unload();
    general.unload();
    gauges.unload();
    gaugeDefinitions.destroy();
    if ( !gaugeBank.getEntries().empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( gaugeBank.getPath().c_str() );
    }
    gaugeBank.unload();
    pDialTexture = pNeedleTexture = nullptr;
    releaseRadioIcon();
    needleTables.clear();
    goalTimer = -1.0f;
    bBackOnTrackShown = false;
    bRadioShown = false;
    bCarMenuShown = false;
    enterSpotKind_.clear();
    bEnterShown_ = false;
    for ( MarkerClone &clone : markerClones_ ) {
        clone = MarkerClone();
    }
    bMarkersVisible_ = false;
    hintTimer_ = -1.0f;
    countdownNumber_ = -1;
    bViolationGaugeShown_ = false;
    violationGaugeMode_.clear();
    violationGaugeFrame_ = ~0u;
    violationGaugeHide_ = 0.0f;
    chaseHelpHide_ = 0.0f;
    bChaseTimerShown_ = false;
    chaseTimerHide_ = 0.0f;
    chaseTimerText_.clear();
    bArrestedShown_ = false;
    bChaseHelpOpen_ = false;
    bRaceTimerShown_ = false;
    raceTimerText_.clear();
}

void FreeRideHud::dumpClips( const FlashPlayer::Clip &clip, const std::string &path, int32_t depth ) {
    for ( const auto &entry : clip.DisplayList ) {
        const FlashPlayer::Instance &instance = *entry.second;
        const std::string name = instance.Name.empty() ? "#" + std::to_string( instance.Depth ) : instance.Name;
        OTDU_LOG_INFO( "HUD %s/%s char %u depth %u clipdepth %u at (%.1f, %.1f) scale (%.2f, %.2f) visible %d frames %u mulA %.2f addA %.2f\n",
                       path.c_str(), name.c_str(), instance.CharacterId, instance.Depth, instance.ClipDepth, instance.Matrix.TX,
                       instance.Matrix.TY, instance.Matrix.A, instance.Matrix.D, instance.bVisible ? 1 : 0,
                       instance.pClip && instance.pClip->pTimeline ? instance.pClip->pTimeline->FrameCount : 0,
                       instance.ColorTransform.Mul[3], instance.ColorTransform.Add[3] );
        // The character's own binding: an EditText reads its variable (the
        // authored text is the variable name in the Eden convention) and a
        // frame-swapped sprite (logos, icons) places one child per frame —
        // both are what the game drives.
        if ( hud.getDefinitions().getType( instance.CharacterId ) == eFlashCharacterType::CT_EditText ) {
            if ( const FlashEditText *pText = hud.getDefinitions().getEditText( instance.CharacterId ) ) {
                OTDU_LOG_INFO( "HUD %s/%s EDITTEXT var '%s' text '%s'\n", path.c_str(), name.c_str(),
                               pText->VariableName.c_str(), pText->Text.c_str() );
            }
        }
        if ( hud.getDefinitions().getType( instance.CharacterId ) == eFlashCharacterType::CT_Shape ) {
            if ( const FlashShape *pShape = hud.getDefinitions().getShape( instance.CharacterId ) ) {
                OTDU_LOG_INFO( "HUD %s/%s SHAPE bounds (%.1f..%.1f, %.1f..%.1f) px fills %zu\n", path.c_str(), name.c_str(),
                               pShape->XMin / 20.0f, pShape->XMax / 20.0f, pShape->YMin / 20.0f, pShape->YMax / 20.0f,
                               pShape->Fills.size() );
                for ( size_t fi = 0; fi < pShape->Fills.size(); fi++ ) {
                    const FlashFill &fill = pShape->Fills[fi];
                    if ( fill.Type == eFlashFillType::FF_Solid ) {
                        OTDU_LOG_INFO( "HUD %s/%s fill %zu solid %08x a%u\n", path.c_str(), name.c_str(), fi, fill.Color,
                                       ( fill.Color >> 24 ) & 0xff );
                    } else if ( fill.Type == eFlashFillType::FF_Bitmap ) {
                        OTDU_LOG_INFO( "HUD %s/%s fill %zu bitmap %u uv (%.3f..%.3f, %.3f..%.3f) %gx%g\n", path.c_str(), name.c_str(), fi,
                                       fill.BitmapId, fill.U0, fill.U1, fill.V0, fill.V1, fill.BitmapWidth, fill.BitmapHeight );
                    } else if ( fill.Type != eFlashFillType::FF_None ) {
                        OTDU_LOG_INFO( "HUD %s/%s fill %zu gradient stops %zu\n", path.c_str(), name.c_str(), fi,
                                       fill.GradientStops.size() );
                    }
                }
            }
        }
        if ( instance.pClip && instance.pClip->pTimeline && instance.pClip->pTimeline->FrameCount > 1 ) {
            std::string frames;
            for ( uint32_t f = 0; f < instance.pClip->pTimeline->Frames.size(); f++ ) {
                for ( const FlashControlTag &tag : instance.pClip->pTimeline->Frames[f] ) {
                    if ( tag.Type == eFlashControlType::CT_Place && tag.Place.bHasCharacter ) {
                        frames += " f" + std::to_string( f + 1 ) + ":ch" + std::to_string( tag.Place.CharacterId );
                    } else if ( tag.Type == eFlashControlType::CT_Label ) {
                        frames += " f" + std::to_string( f + 1 ) + ":'" + tag.Label + "'";
                    }
                }
            }
            if ( !frames.empty() ) {
                OTDU_LOG_INFO( "HUD %s/%s timeline:%s\n", path.c_str(), name.c_str(), frames.c_str() );
            }
        }
        if ( instance.pClip && depth < 4 ) {
            dumpClips( *instance.pClip, path + "/" + name, depth + 1 );
        }
    }
}

namespace {
// /HUD/enter_hold at 100 %, the 'stop' state (the movie's own per-frame
// placements, -dumpbot): the ring centre inside ic is the mask 807's bounds
// centre (64.85, -33.1) plus anim's placement (-0.4, 2.9) = (64.45, -30.2),
// ic rests at (-759.25, -72) of the element at 'stop' — so the ring centre is
// (-694.8, -102.2) of the element origin. The movie parks the element at
// (26695, 11768) twips = (1334.75, 588.4) px of the 1280 x 960 stage, which
// puts the ring at stage x 639.95 — the retail element shows at its parking
// spot, x exact. The owner's 4K capture puts the ring centre at canvas
// (640, 342) = stage (640, 462), 24 px above the parked y, so the element is
// nudged up by that much (docs/re/backontrack_ui_findings.md).
constexpr float kElementX = 1334.75f;
constexpr float kElementY = 588.4f - 24.0f;
} // namespace

void FreeRideHud::showBackOnTrack( float progress, bool bLocked ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    // The retail indicator (docs/re/backontrack_ui_findings.md): the circular
    // ring of /HUD/enter_hold. One label for both states (the shared root
    // variable /:m_Enter, loc 50 "Return to the road"), the blue gradient +
    // white down arrow while the reset can be used (ic 'on', frame 63: the
    // fill visible), and the red "forbidden" X when it cannot (ic 'lock',
    // frame 8: ch816 alpha 0.5 + the ch114 glyph over the hidden fill).
    //
    // The element's states (-dumpbot): f1 'default' parks the children at
    // alpha 0 with a placeholder bar at depth 13; 'open' (f9) removes the bar
    // and tweens the ring in; 'stop' (f17) is the settled state (ic and the
    // label at alpha 1). The fill sweep is ic/anim's 30-frame timeline —
    // ic/anim/#1 (char 807, ClipDepth 4) masks ic/anim/#2 (char 810), frames
    // 2..29 move the fill — driven here from the hold progress.
    // /HUD/BOT (char 821, "Back On Track") - NOT /HUD/enter_hold (char 828,
    // the generic hold-to-enter ring the port used until 2026-11-05): the two
    // are twins (the same open/stop/close tween, the same ic states and the
    // same sky sweep 811), but BOT's ic draws shape 813 = bitmap 812, the
    // white ring + DOWN arrow + road with lane dashes of the retail capture,
    // where enter_hold's draws 822 (an up-arrow cut out of a white disc).
    // BOT's own 'open' script picks the ic state from _root.IsBOT:
    //   if ( _root.IsBOT == true ) ic.gotoAndStop( "on" ) else ic.gotoAndPlay( "lock" )
    // and its label is its own text field (#11 = sprite 819, EDITTEXT var
    // '/:m_BackOnTrack_', default '_Back on track').
    static const char *kPath = "/HUD/BOT";
    static const char *kIcPath = "/HUD/BOT/ic";
    static const char *kAnimPath = "/HUD/BOT/ic/anim";
    if ( hud.findClip( kPath ) == nullptr ) {
        static bool bLogged = false;
        if ( !bLogged ) {
            bLogged = true;
            OTDU_LOG_WARN( "FreeRideHud: '%s' missing, no back on track indicator\n", kPath );
        }
        return;
    }
    // -botforce on|lock: show the element from frame 60 in the forced state
    // (no hold needed — the headless shots of the two states).
    static const char *pBotForce = CmdLine::GetArgValue( "-botforce" );
    static uint32_t botForceFrames = 0;
    if ( pBotForce != nullptr ) {
        botForceFrames++;
        if ( botForceFrames < 60 ) {
            return;
        }
        progress = 0.0f;
        bLocked = StringCmpI( pBotForce, "lock" ) == 0;
    }
    if ( progress < 0.0f ) {
        if ( bBackOnTrackShown ) {
            hud.setClipVisible( kPath, false );
            bBackOnTrackShown = false;
        }
        return;
    }
    if ( !bBackOnTrackShown ) {
        hud.setClipVisible( kPath, true );
        // The movie's 'default' frame parks the element with the ring hidden
        // (ic is placed with alpha 0 and a placeholder bar sits at depth 13);
        // the settled 'stop' state removes the bar and its tween brings the
        // ring in. Frame 0 is applied first so the display list exists before
        // the jump (the movie's own f1 action stops there).
        hud.gotoFrame( kPath, 0, false );
        hud.gotoLabel( kPath, "stop", false );
        // -dumpbot: the element's and ic's per-frame placements (frame, depth,
        // character, matrix, colour transform) - the animation law of the ring.
        if ( CmdLine::HasArg( "-dumpbot" ) ) {
            static bool bDumped = false;
            if ( !bDumped ) {
                bDumped = true;
                // Who uses the badge bitmaps: the shapes filled with them,
                // then the sprites placing those shapes (and their parents,
                // up to the root) - the element tree of the retail art.
                const FlashDefinitions &defs = hud.getDefinitions();
                std::set<uint16_t> users;
                for ( uint32_t id = 1; id < 0x10000; id++ ) {
                    const FlashShape *pShape = defs.getShape( static_cast<uint16_t>( id ) );
                    if ( pShape == nullptr ) {
                        continue;
                    }
                    for ( const FlashFill &fill : pShape->Fills ) {
                        if ( fill.Type == eFlashFillType::FF_Bitmap &&
                             ( fill.BitmapId == 812 || fill.BitmapId == 822 || fill.BitmapId == 571 ||
                               fill.BitmapId == 808 ) ) {
                            OTDU_LOG_INFO( "[botdump] shape %u fills bitmap %u bounds (%.1f %.1f)-(%.1f %.1f)\n", id,
                                           fill.BitmapId, pShape->XMin / 20.0f, pShape->YMin / 20.0f,
                                           pShape->XMax / 20.0f, pShape->YMax / 20.0f );
                            users.insert( static_cast<uint16_t>( id ) );
                        }
                    }
                }
                for ( int pass = 0; pass < 6 && !users.empty(); pass++ ) {
                    std::set<uint16_t> parents;
                    auto scan = [&]( const FlashTimeline &timeline, uint32_t ownerId ) {
                        for ( uint32_t f = 0; f < timeline.Frames.size(); f++ ) {
                            for ( const FlashControlTag &tag : timeline.Frames[f] ) {
                                if ( tag.Type == eFlashControlType::CT_Place && tag.Place.bHasCharacter &&
                                     users.count( tag.Place.CharacterId ) != 0 ) {
                                    OTDU_LOG_INFO( "[botdump] pass %d: %s %u f%u depth %u places %u name '%s' at "
                                                   "(%.2f, %.2f) scale (%.3f, %.3f)\n",
                                                   pass, ownerId == 0 ? "ROOT" : "sprite", ownerId, f + 1,
                                                   tag.Place.Depth, tag.Place.CharacterId, tag.Place.Name.c_str(),
                                                   tag.Place.Matrix.TX / 20.0f, tag.Place.Matrix.TY / 20.0f,
                                                   tag.Place.Matrix.A, tag.Place.Matrix.D );
                                    if ( ownerId != 0 ) {
                                        parents.insert( static_cast<uint16_t>( ownerId ) );
                                    }
                                }
                            }
                        }
                    };
                    scan( defs.getRootTimeline(), 0 );
                    for ( uint32_t id = 1; id < 0x10000; id++ ) {
                        if ( const FlashTimeline *pSprite = defs.getSprite( static_cast<uint16_t>( id ) ) ) {
                            scan( *pSprite, id );
                        }
                    }
                    users = parents;
                }
                for ( const char *pDumpPath :
                      { kPath, kIcPath, kAnimPath, "/HUD/enter_hold", "/HUD/enter_hold/ic", "/HUD/enter/Spot/offroad",
                        "/HUD/enter/Spot/forbidden", "/HUD/enter/Spot", "/HUD/enter" } ) {
                    const FlashPlayer::Clip *pDump = hud.findClip( pDumpPath );
                    if ( pDump == nullptr || pDump->pTimeline == nullptr ) {
                        continue;
                    }
                    OTDU_LOG_INFO( "[botdump] --- %s (char %u, %u frames) ---\n", pDumpPath,
                                   pDump->pInstance != nullptr ? pDump->pInstance->CharacterId : 0,
                                   pDump->pTimeline->FrameCount );
                    for ( const uint16_t shapeChar : { 813u, 807u, 810u, 823u, 816u, 114u, 825u, 826u, 827u, 60u } ) {
                        const FlashShape *pShape = hud.getDefinitions().getShape( shapeChar );
                        const int type = static_cast<int>( hud.getDefinitions().getType( shapeChar ) );
                        if ( pShape != nullptr ) {
                            OTDU_LOG_INFO( "[botdump] char %u type %d shape bounds (%.1f %.1f)-(%.1f %.1f) meshes %zu\n",
                                           shapeChar, type, pShape->XMin / 20.0f, pShape->YMin / 20.0f, pShape->XMax / 20.0f,
                                           pShape->YMax / 20.0f, pShape->Meshes.size() );
                        } else {
                            OTDU_LOG_INFO( "[botdump] char %u type %d no shape\n", shapeChar, type );
                        }
                    }
                    for ( uint32_t f = 0; f < pDump->pTimeline->Frames.size(); f++ ) {
                        for ( const FlashControlTag &tag : pDump->pTimeline->Frames[f] ) {
                            if ( tag.Type == eFlashControlType::CT_Label ) {
                                OTDU_LOG_INFO( "[botdump] f%u label '%s'\n", f + 1, tag.Label.c_str() );
                            } else if ( tag.Type == eFlashControlType::CT_Place ) {
                                const FlashPlaceInfo &pl = tag.Place;
                                std::string extra;
                                if ( pl.bHasColorTransform ) {
                                    extra += " mulA " + std::to_string( pl.ColorTransform.Mul[3] ) + " addA " +
                                             std::to_string( pl.ColorTransform.Add[3] );
                                }
                                if ( pl.bHasMatrix ) {
                                    extra += " xy (" + std::to_string( pl.Matrix.TX / 20.0f ) + "," +
                                             std::to_string( pl.Matrix.TY / 20.0f ) + ") scale (" +
                                             std::to_string( pl.Matrix.A ) + "," + std::to_string( pl.Matrix.D ) + ")";
                                }
                                OTDU_LOG_INFO( "[botdump] f%u %s depth %u char %u clip %u%s\n", f + 1,
                                               pl.bMove ? "move" : "PLACE", pl.Depth, pl.bHasCharacter ? pl.CharacterId : 0,
                                               pl.ClipDepth, extra.c_str() );
                            } else if ( tag.Type == eFlashControlType::CT_Remove ) {
                                OTDU_LOG_INFO( "[botdump] f%u remove depth %u\n", f + 1, tag.Depth );
                            } else if ( tag.Type == eFlashControlType::CT_Action ) {
                                std::string hex;
                                const std::vector<uint8_t> &raw = hud.getMovie()->getVMF().getData();
                                const uint32_t start = tag.ActionOffset + 4;
                                for ( uint32_t b = 0; b < tag.ActionLength + 8 && start + b < raw.size(); b++ ) {
                                    char byte[8];
                                    snprintf( byte, sizeof( byte ), "%02x ", raw[start + b] );
                                    hex += byte;
                                }
                                OTDU_LOG_INFO( "[botdump] f%u action %u bytes: %s\n", f + 1, tag.ActionLength,
                                               hex.c_str() );
                            }
                        }
                    }
                    // The art subtree with its fills (the bitmaps the badge
                    // draws — the missing/blank texture shows here). Depth 0:
                    // the walk's own recursion limit (4) reaches the fills of
                    // the badge art from the anim path.
                    if ( FlashPlayer::Clip *pWalk = hud.findClip( pDumpPath ) ) {
                        dumpClips( *pWalk, pDumpPath, 0 );
                    }
                }
            }
        }
        // The retail layout: the ring centre at stage (640, 462) — the
        // movie's own parking x, the capture's y (see kElementY).
        hud.setClipTransform( kPath, kElementX, kElementY, 0.0f, 100.0f, 100.0f );
        // The label: BOT's own text field (EDITTEXT var '/:m_BackOnTrack_')
        // with the retail line (loc 50; the field's default key
        // '_Back on track' is the [hold] prompt, not the capture's text). #11 is
        // NOT re-anchored: at the stop state the movie's own arrangement puts
        // the text where the capture has it (under the ring, centred).
        hud.setVariable( "/:m_BackOnTrack_", LocStr( 50, "Return to the road" ) );
        bBackOnTrackShown = true;
        // Every show makes the ic goto below (the gotoFrame( 0 ) above put
        // ic back on 'default', whatever the cache says): a show that starts
        // locked used to keep the 'on' art - the cache already said 'lock'.
        bBackOnTrackLocked = !bLocked;
    }
    // The ic state: 'on' (frame 63, the fill visible) while the reset can be
    // used, 'lock' (frame 8, the red X) when it cannot. Only on change.
    if ( bBackOnTrackLocked != bLocked ) {
        bBackOnTrackLocked = bLocked;
        // The movie's own switch (BOT f9 'open' reads it); the driver
        // below makes the same ic goto, so the two agree.
        hud.setVariableBool( "/:IsBOT", !bLocked );
        hud.gotoLabel( kIcPath, bLocked ? "lock" : "on", false );
        if ( !bLocked ) {
            // The goto replays the frames on the way (gotoFrame's forward
            // walk), so the 'lock' art placed at f8 (ch816 at depth 1) leaks
            // into the 'on' state — hidden (the movie's own f62 state has no
            // depth 1: it is placed by 'lock' only).
            hud.setDepthVisible( kIcPath, 1, false );
            // A refused press lets the movie close the element (ic f40
            // _parent.gotoAndPlay("close") -> 'default', everything at
            // alpha 0): settle it back on 'stop' so the badge shows again.
            hud.gotoLabel( kPath, "stop", false );
        } else {
            // ... and shown again for 'lock' (the red disc under the X): the
            // hide above outlives the goto, so an on -> lock switch drew the
            // X over the white badge.
            hud.setDepthVisible( kIcPath, 1, true );
        }
    }
    // ic/anim frame 1..30 from the hold progress (frame 1 = the fill at its
    // start position, frame 30 = full): 0-based for the player, 0..29.
    // -botframe <n>: park the sweep at a fixed frame (the state probe shots).
    uint32_t frame = static_cast<uint32_t>( Clamp( progress, 0.0f, 1.0f ) * 29.0f + 0.5f );
    if ( const char *pFrame = CmdLine::GetArgValue( "-botframe" ) ) {
        frame = static_cast<uint32_t>( Clamp( atoi( pFrame ), 0, 29 ) );
    }
    hud.gotoFrame( kAnimPath, frame, false );
    hud.stopSubtree( kAnimPath ); // the sweep parks where the driver put it
}

void FreeRideHud::showGpsArrow( int32_t arrow ) {
    if ( !hud.isLoaded() || arrow == gpsArrow_ || arrow < 0 || arrow > 16 ) {
        return;
    }
    static const char *kPath = "/HUD/arrow_gps";
    if ( hud.findClip( kPath ) == nullptr ) {
        return;
    }
    // The direction names: the string table at 0xf75fd8 [CODE] = the labels of
    // /HUD/arrow_gps/arrow [DATA].
    static const char *kDirections[17] = { "up",       "rightup",      "right",   "rightdown",   "down",    "leftdown",
                                           "left",     "leftup",       "up_ra",   "rightup_ra",  "right_ra", "rightdown_ra",
                                           "down_ra",  "leftdown_ra",  "left_ra", "leftup_ra",   "empty" };
    hud.setVariableBool( "/:IsRoundAbout", arrow >= 8 && arrow <= 15 );
    hud.setVariable( "/:ArrowDirection", kDirections[arrow] );
    const char *pLabel = arrow == 16 ? "close" : ( gpsArrow_ == 16 ? "open" : "change" );
    hud.setClipVisible( kPath, true );
    hud.gotoLabel( kPath, pLabel, true );
    if ( arrow != 16 ) {
        // [INF] the arrow child is also put on its label directly, in case
        // the movie reads /:ArrowDirection only inside a script the port skips.
        hud.gotoLabel( "/HUD/arrow_gps/arrow", kDirections[arrow], false );
    }
    static const bool bLog = CmdLine::HasArg( "-logroute" );
    if ( bLog ) {
        OTDU_LOG_INFO( "FreeRideHud: GPS arrow %s -> %s (%s)\n", kDirections[gpsArrow_], kDirections[arrow], pLabel );
    }
    gpsArrow_ = arrow;
}

void FreeRideHud::showRadio( bool bVisible, const RadioPlayer::Station &station, const std::string &artist,
                            const std::string &title, float volume ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    static const char *kPath = "/HUD/radio";
    if ( hud.findClip( kPath ) == nullptr ) {
        static bool bLogged = false;
        if ( !bLogged ) {
            bLogged = true;
            OTDU_LOG_WARN( "FreeRideHud: '%s' missing, no radio panel\n", kPath );
        }
        return;
    }
    if ( !bVisible ) {
        if ( bRadioShown ) {
            bRadioShown = false;
            // The movie's own slide out ("close", frame 33): the panel parks
            // off stage, so the clip itself can stay visible.
            if ( !hud.gotoLabel( kPath, "close", true ) ) {
                hud.setClipVisible( kPath, false );
            }
        }
        return;
    }
    if ( !bRadioShown ) {
        bRadioShown = true;
        hud.setClipVisible( kPath, true );
        // The movie's own slide in ("open" frames 8..25, which ends on the
        // "stop" label with the panel in place). Without this the clip is
        // visible but parked off stage: that was the "radio HUD is missing".
        if ( !hud.gotoLabel( kPath, "open", true ) ) {
            OTDU_LOG_WARN( "FreeRideHud: '%s' has no 'open' label\n", kPath );
        }
    }
    // Station logo: the movie's own sprite for the retail stations (the labels
    // 'hariba' / 'kalua' / 'roadtrack') or the station's art, drawn in the
    // sprite's box (drawRadioIcon) while the sprite stays hidden.
    static const char *kLogoPath = "/HUD/radio/radio_logo";
    if ( !station.LogoLabel.empty() ) {
        setRadioIcon( std::string() );
        hud.setClipVisible( kLogoPath, true );
        hud.gotoLabel( kLogoPath, station.LogoLabel, false );
    } else {
        hud.setClipVisible( kLogoPath, false );
        setRadioIcon( station.IconPath );
    }
    // Panel texts. Line 1 is the STATION name (/:RadioTitle, the movie's own
    // field with the play glyph — retail: "▶ Hariba Radio"), line 2 the
    // artist (/:RadioSinger), line 3 the title (the radio_track scroller: it
    // reads the radio_track clip variable when its "set" label plays).
    hud.setVariable( "/:RadioTitle", station.Name );
    hud.setVariable( "/:RadioSinger", artist );
    // The title scroller (the third line): /:RadioTrack [INF, the singer
    // scroller reads /:RadioSinger] and the clip variables of the same names.
    hud.setVariable( "/:RadioTrack", title );
    hud.setVariable( "/:radio_track", title );
    hud.setVariable( "/HUD/radio/texts/radio_track:txt", title );
    hud.setVariable( "/HUD/radio/texts/radio_track:radio_track", title );
    hud.gotoLabel( "/HUD/radio/texts/radio_track", "set", true );
    hud.gotoLabel( "/HUD/radio/texts/radio_singer", "set", true );
    // Volume bar: /HUD/radio/volume is an 11-frame clip (one fill per frame)
    // and the game stops it on frame 2 + trunc( 9 * volume ), frame 1 when
    // muted (FUN_009297c0 tail, 1-based gotoAndStop) [CODE].
    const float clamped = Clamp( volume, 0.0f, 1.0f );
    const uint32_t volumeFrame = clamped < 1e-5f ? 0u : 1u + static_cast<uint32_t>( ( clamped + 1e-5f ) * 9.0f );
    hud.gotoFrame( "/HUD/radio/volume", volumeFrame, false );
    if ( CmdLine::HasArg( "-logradiovol" ) ) {
        if ( FlashPlayer::Clip *pVolume = hud.findClip( "/HUD/radio/volume" ) ) {
            for ( const auto &entry : pVolume->DisplayList ) {
                OTDU_LOG_INFO( "radiovol %.2f frame %u: depth %u char %u clip %u vis %d sx %.3f tx %.0f\n", volume, volumeFrame, entry.first,
                               entry.second->CharacterId, entry.second->ClipDepth, entry.second->bVisible ? 1 : 0, entry.second->Matrix.A, entry.second->Matrix.TX );
                const FlashColorTransform &ct = entry.second->ColorTransform;
                OTDU_LOG_INFO( "radiovol   cx mul %.2f %.2f %.2f %.2f add %.2f %.2f %.2f %.2f\n", ct.Mul[0], ct.Mul[1], ct.Mul[2],
                               ct.Mul[3], ct.Add[0], ct.Add[1], ct.Add[2], ct.Add[3] );
            }
        }
    }
}

void FreeRideHud::showCarMenu( bool bVisible, bool bHasHood, bool bHasWindows ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    static const char *kPath = "/HUD/ig_menu";
    if ( hud.findClip( kPath ) == nullptr ) {
        static bool bLogged = false;
        if ( !bLogged ) {
            bLogged = true;
            OTDU_LOG_WARN( "FreeRideHud: '%s' missing, no car menu\n", kPath );
        }
        return;
    }
    if ( !bVisible ) {
        if ( bCarMenuShown ) {
            bCarMenuShown = false;
            // The movie's own slide out (labels: default f0, open f11, stop
            // f29, close f38; the close timeline ends on 'default').
            if ( !hud.gotoLabel( kPath, "close", true ) ) {
                hud.setClipVisible( kPath, false );
            }
        }
        return;
    }
    if ( bCarMenuShown ) {
        return;
    }
    bCarMenuShown = true;
    // FUN_00928840: the three availability flags the movie's SetIcons reads
    // (the turn signals are never locked by this movie: ig_lock_2 / 4 = 0).
    hud.setVariableBool( "/:HasHood", bHasHood );
    hud.setVariableBool( "/:HasWindows", bHasWindows );
    hud.setVariableBool( "/:HasTurnSignals", true );
    // FUN_00928c10: gotoAndPlay 'open'. The frame-10 script calls SetIcons
    // (entry glyphs, locks, IG_Title = _root.m_Accessories) and the 'stop'
    // frame holds the menu in place.
    hud.setClipVisible( kPath, true );
    if ( !hud.gotoLabel( kPath, "open", true ) ) {
        OTDU_LOG_WARN( "FreeRideHud: '%s' has no 'open' label\n", kPath );
    }
    if ( CmdLine::HasArg( "-dumphud" ) ) {
        dumpClips( *hud.findClip( kPath ), kPath, 3 );
    }
}

void FreeRideHud::validateCarMenu( int32_t entry ) {
    constexpr int32_t kEntryCount = static_cast<int32_t>( std::size( kCarMenuEntries ) );
    if ( !hud.isLoaded() || !bCarMenuShown || entry < 0 || entry >= kEntryCount ) {
        return;
    }
    bCarMenuShown = false;
    // The movie's ValidIGM(label) [DATA, ig_menu frame 0]: valid_icon plays
    // 'valid' with the entry's glyph and name, then ig_menu plays 'close'.
    if ( !hud.invoke( "/HUD/ig_menu", "ValidIGM", { kCarMenuEntries[entry].pLabel } ) ) {
        OTDU_LOG_WARN( "FreeRideHud: /HUD/ig_menu has no ValidIGM, closing\n" );
        hud.gotoLabel( "/HUD/ig_menu", "close", true );
    }
}

void FreeRideHud::setTestDrive( bool bEnable ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    hud.setVariableBool( "/:IsTest", bEnable );
    hud.setClipVisible( "/HUD/time_cd", bEnable );
    if ( bEnable ) {
        hud.gotoLabel( "/HUD/time_cd", "open", true );
    }
}

void FreeRideHud::setCountdown( float remaining ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    // FUN_00a11120: ms = (uint)ROUND(t * 1000.0 + 0.5); min = ms / 60000,
    // sec = ms % 60000 / 1000, cent = ms % 1000 / 10 [CODE]. Digits "%02d"
    // (binding width 2) [INF].
    const uint32_t ms = static_cast<uint32_t>( std::lround( std::max( remaining, 0.0f ) * 1000.0 + 0.5 ) );
    char text[16];
    snprintf( text, sizeof( text ), "%02u", ms / 60000u );
    hud.setVariable( "/:MinCd", text );
    snprintf( text, sizeof( text ), "%02u", ( ms % 60000u ) / 1000u );
    hud.setVariable( "/:SecCd", text );
    snprintf( text, sizeof( text ), "%02u", ( ( ms % 60000u ) % 1000u ) / 10u );
    hud.setVariable( "/:CentCd", text );
}

void FreeRideHud::showGoal( const std::string &text ) {
    // FUN_00d61790: /:GoalTxt, pop_goal goto "open" (/:IsWalkmode false),
    // 30 s timer (DAT_00ede1fc).
    if ( !general.isLoaded() ) {
        return;
    }
    general.setVariableBool( "/:IsWalkmode", false );
    general.setVariable( "/:GoalTxt", text );
    general.gotoLabel( "/pop_goal", "open", true );
    goalTimer = 30.0f;
}

void FreeRideHud::closeGoal() {
    // The 30 s timer path calls this with goalTimer already < 0 (the tick
    // decrements first) — the guard must not depend on the timer's sign or
    // the popup never closes (the bar stayed on screen forever).
    if ( general.isLoaded() ) {
        const bool bClosed = general.invoke( "/pop_goal", "close_popup", {} );
        OTDU_LOG_INFO( "FreeRideHud: goal closed -> close_popup %d\n", bClosed ? 1 : 0 );
    }
    goalTimer = -1.0f;
}

// ---- the world spot element (/HUD/enter) and its distant clones ----
//
// HUD.vmf /HUD/enter (char 251): parked off stage invisible; labels "off" f1,
// "on" f7, "stop" f28, "locked" f36, "end" f44, "out" f50. The Spot child
// (char 227, 751 frames) carries one frame label per spot kind ("clothes",
// "carDealer", "event", "estates"... [DATA: -dumpsprite 227]); the return to
// road art is the Spot/offroad child (labels "default" / "on"), shown over
// the Spot's "empty" frame. The prompt (enter_txt) reads /:m_Enter with the
// (A) glyph next to it. The element stays parked; the game positions the
// Spot tile and the prompt separately at the spot's projected screen point.

namespace {
// The halo children of the Spot tile, anchored to the element's parked
// origin: hidden (the tile art carries its own glow rim) [INF: the retail
// game re-places them per frame; the port shows the tile alone].
const char *kSpotHalos[] = { "news", "glow", "target", "hlight", "link", "rank", "lock", "forbidden", "favorite", "done" };
// The enter element's card: the dark panel and the button glyph behind the
// prompt, placed by the movie at depths 4 / 5 / 9 / 10 / 13 without names (so a
// path cannot reach them - setInstanceVisible takes the depth). The tile alone
// marks a spot the car is driving past; the card belongs to the prompt.
const uint16_t kEnterCardDepths[] = { 4, 5, 9, 10, 13 };
// The marker tile centre relative to the /HUD/enter origin (canvas px at 100 %).
constexpr float kMarkerTileDX = 7.0f;
constexpr float kMarkerTileDY = 280.0f;
// The return to road label: the vertical distance from the badge centre to
// the label's own origin (the retail capture: the badge centre canvas 342,
// the label centre 395 - 53 px; the text field's own offset inside the
// enter_txt sprite folded in).
constexpr float kOffroadLabelDy = 53.0f;
} // namespace

void FreeRideHud::showEnterSpot( const char *pSpotKind, const std::string &text, float canvasX, float canvasY,
                                 float scalePercent, bool bLocked ) {
    if ( !hud.isLoaded() || pSpotKind == nullptr ) {
        return;
    }
    if ( hud.findClip( "/HUD/enter" ) == nullptr ) {
        return;
    }
    const bool bOffroad = strcmp( pSpotKind, "offroad" ) == 0;
    if ( !bEnterShown_ ) {
        bEnterShown_ = true;
        hud.setClipVisible( "/HUD/enter", true );
        // Jump to the settled open state (playing "on" tweens from the parked
        // position; the game places the element itself every frame). 'on' was
        // tried and is wrong for this flow: its f6 only removes d24 / d25 —
        // the card groups (#4 / #5 / #9 / #10 / #13) stay placed and draw as
        // loose floating panels; 'stop' is the settled card+tile arrangement.
        hud.gotoLabel( "/HUD/enter", "stop", false );
        // The movie's own 'on' timeline parks an unnamed copy of the minimap
        // content (char 231) at depth 1, at (-459.6, 229.5) of the element:
        // off stage while the element is parked, but every move of the element
        // dragged it along and it rendered as a stray beige bar near the
        // satnav. Hidden (the retail engine never draws this copy — it renders
        // the map into the /HUD/minimap one).
        hud.setDepthVisible( "/HUD/enter", 1, false );
    }
    hud.setClipVisible( "/HUD/enter/Spot", true );
    // The element's own prompt bar (char 231, placed at depth 1 without a name)
    // is anchored to the parked origin too: left visible it paints a beige
    // rectangle at the bottom left of the screen, far from the tile. The port
    // draws the prompt itself (enter_txt), so the bar is hidden.
    hud.setInstanceVisible( "/HUD/enter", 1, false );
    if ( enterSpotKind_ != pSpotKind ) {
        enterSpotKind_ = pSpotKind;
        if ( bOffroad ) {
            // The retail return to road presentation (the owner's captures +
            // the Spot's own timeline): the Spot 'default' frame IS the
            // offroad kind — it places the offroad child (the circular
            // badge, bitmap 4754) and the halo children, no championship
            // card. The badge art is the child's 'on' label ('default' is
            // the invisible placeholder quad).
            hud.gotoLabel( "/HUD/enter/Spot", "default", false );
            hud.gotoLabel( "/HUD/enter/Spot/offroad", "on", false );
            hud.setClipVisible( "/HUD/enter/Spot/offroad", true );
        } else {
            // The offroad child's 'default' is not an empty placeholder: its
            // quad draws as a large glass plate over the property kinds (the
            // owner's capture has none). Parked AND hidden; the offroad kind
            // drives it back to 'on' below.
            hud.gotoLabel( "/HUD/enter/Spot/offroad", "default", false );
            hud.setClipVisible( "/HUD/enter/Spot/offroad", false );
            if ( !hud.gotoLabel( "/HUD/enter/Spot", pSpotKind, false ) ) {
                OTDU_LOG_WARN( "FreeRideHud: spot art '%s' not found\n", pSpotKind );
            }
            // The kind frames carry a play action: without the park the tile's
            // 751-frame timeline resumes after the goto and its loop rewind
            // erases the icon slot (depth 26 — f0 never places it, the loop's
            // display-list rebuild drops it), leaving the kind frame's plate
            // over a stale or missing icon. The world-marker clones park the
            // same way.
            hud.stopSubtree( "/HUD/enter/Spot" );
            hud.setClipVisible( "/HUD/enter/Spot/offroad", false );
        }
    }
    if ( bOffroad ) {
        // The red X (the forbidden halo, 33+45 px bitmaps) over the badge
        // while the reset cannot be used; hidden otherwise (its 'default'
        // authors both pieces at alpha 0).
        hud.setClipVisible( "/HUD/enter/Spot/forbidden", true );
        hud.gotoLabel( "/HUD/enter/Spot/forbidden", bLocked ? "on" : "default", false );
    }
    // The halo children are anchored to the parked origin, not to the tile:
    // hidden AFTER the kind goto (the goto re-places them, visible again) —
    // except the forbidden one for the return to road indicator: the retail
    // red X over the badge (the owner's capture: the action unavailable
    // state), driven per show above.
    for ( const char *pChild : kSpotHalos ) {
        if ( bOffroad && strcmp( pChild, "forbidden" ) == 0 ) {
            continue;
        }
        hud.setClipVisible( std::string( "/HUD/enter/Spot/" ) + pChild, false );
    }
    if ( bOffroad ) {
        // The badge arts as PNGs (the engine's own decode — the ground truth
        // of what the retail art is): the badge, the hold ring's fill and
        // glow, the red X pieces.
        static bool bArtDumped = false;
        if ( !bArtDumped && CmdLine::HasArg( "-dumpbot" ) ) {
            bArtDumped = true;
            for ( const uint16_t art : { 4754u, 4795u, 4783u, 4765u, 4767u, 808u, 822u } ) {
                std::vector<uint8_t> rgba;
                uint32_t w = 0, h = 0;
                if ( !hud.getMovie()->getBitmapPixels( art, rgba, w, h ) &&
                     !hud.getMovie()->getAtlasCellPixels( art, rgba, w, h ) ) {
                    continue;
                }
                char pPath[256];
                snprintf( pPath, sizeof( pPath ), "_flashart_%u.png", art );
                stbi_write_png( pPath, static_cast<int>( w ), static_cast<int>( h ), 4, rgba.data(),
                                static_cast<int>( w * 4 ) );
                OTDU_LOG_INFO( "[botdump] art %u -> %s (%ux%u)\n", art, pPath, w, h );
            }
            // The whole badge-cluster id range: the captures' road + arrow art
            // is one of these (the scan writes what exists).
            for ( uint32_t id = 4740u; id < 4830u; id++ ) {
                std::vector<uint8_t> rgba;
                uint32_t w = 0, h = 0;
                if ( !hud.getMovie()->getBitmapPixels( static_cast<uint16_t>( id ), rgba, w, h ) &&
                     !hud.getMovie()->getAtlasCellPixels( static_cast<uint16_t>( id ), rgba, w, h ) ) {
                    continue;
                }
                char pPath[256];
                snprintf( pPath, sizeof( pPath ), "_flashart_%u.png", id );
                stbi_write_png( pPath, static_cast<int>( w ), static_cast<int>( h ), 4, rgba.data(),
                                static_cast<int>( w * 4 ) );
            }
        }
    }
    // The kind goto re-places the tile's child clips, and a freshly placed
    // child plays its own timeline: the plate / prompt / A-glyph arts looped
    // on and off (the 'shape' child alone has a 'lod' frame that erases the
    // plate). The element is game-driven — nothing below it animates here.
    hud.stopSubtree( "/HUD/enter" );
    // The car-dealer icon's 'logo' child is the engine placeholder quad (the
    // retail engine binds the dealership's brand logo texture to it): hidden,
    // the plate shows — its car art is authored alpha 0 outside the multiplayer
    // states (ic/#4 ColorTransform mulA 0) [DATA].
    hud.setClipVisible( "/HUD/enter/Spot/ic/logo", false );
    // The prompt bar has 'ok' (f1..7) and 'locked' (f8..16) states: stopped
    // deterministically on 'ok' (the port never locks a spot).
    hud.gotoLabel( "/HUD/enter/enter_txt", "ok", false );
    // The element is moved as a whole (the internal arrangement of the prompt
    // bar and the tile is the movie's own 'stop' state; the per-child
    // placement of the original fights the timeline, the parent transform is
    // what survives).
    hud.setVariable( "/:m_Enter", text );
    hud.setClipVisible( "/HUD/enter/enter_txt", !text.empty() );
    // Driving past a spot the caller passes an empty text (the prompt only
    // offers the action when the car is slow): the tile still marks the spot,
    // but the card behind the prompt would be an empty dark panel, so it goes.
    // The return to road badge never carries the card (the retail capture has
    // the bare badge + label).
    for ( uint16_t depth : kEnterCardDepths ) {
        hud.setInstanceVisible( "/HUD/enter", depth, !text.empty() && !bOffroad );
    }
    if ( CmdLine::HasArg( "-dumpbot" ) ) {
        static bool bDumped = false;
        if ( !bDumped ) {
            bDumped = true;
            if ( FlashPlayer::Clip *pEnter = hud.findClip( "/HUD/enter" ) ) {
                dumpClips( *pEnter, "/HUD/enter", 2 );
            }
        }
    }
    if ( bOffroad ) {
        // The return to road indicator: the anchor is the BADGE CENTRE (the
        // retail layout, the owner's 4K capture: the badge at canvas
        // (640, 342), the label centred under it at (640, 395)). The movie's
        // own 'stop' arrangement holds the tile at (+23.6, +328) of the
        // element origin (measured, -dumpbot), so the origin is shifted back
        // by that much, scaled; and the label, which the arrangement holds
        // ABOVE the tile, is re-anchored below the badge (its own text field
        // offset folded into the constant).
        const float s = scalePercent / 100.0f;
        float anchorX = 0.0f, anchorY = 0.0f;
        FlashCanvasToStage( canvasX, canvasY, hudStageWidth(), hudStageHeight(), anchorX, anchorY );
        hud.setClipTransform( "/HUD/enter", anchorX - 23.6f * s, anchorY - 328.0f * s, 0.0f, scalePercent,
                              scalePercent );
        hud.setClipTransform( "/HUD/enter/enter_txt", anchorX - 59.1f * s, anchorY - 328.0f * s + kOffroadLabelDy,
                              0.0f, 100.0f, 100.0f );
    } else {
        // The property kinds anchor the same way: the arrangement's tile
        // offset (+23.6, +328) is origin-relative, so the unshifted origin
        // put the tile ~448 px below the projected spot — off screen — and
        // only the prompt card showed (the owner's retail capture: the
        // marker stands over the property).
        const float s = scalePercent / 100.0f;
        float anchorX = 0.0f, anchorY = 0.0f;
        FlashCanvasToStage( canvasX, canvasY, hudStageWidth(), hudStageHeight(), anchorX, anchorY );
        hud.setClipTransform( "/HUD/enter", anchorX - 23.6f * s, anchorY - 328.0f * s, 0.0f, scalePercent,
                              scalePercent );
    }
}

void FreeRideHud::closeEnterSpot() {
    if ( !bEnterShown_ ) {
        return;
    }
    bEnterShown_ = false;
    enterSpotKind_.clear();
    if ( hud.isLoaded() && hud.findClip( "/HUD/enter" ) != nullptr ) {
        // The tile, the prompt and the card behind them hide, and so does the
        // element: the world markers are /HUD/wm<N> siblings (duplicateMovieClip
        // puts the copy in the source's parent), so they are not affected.
        hud.setClipVisible( "/HUD/enter/Spot", false );
        hud.setClipVisible( "/HUD/enter/enter_txt", false );
        hud.setClipVisible( "/HUD/enter", false );
    }
}

void FreeRideHud::setWorldMarkers( const WorldMarker *pMarkers, int32_t count ) {
    if ( !hud.isLoaded() || hud.findClip( "/HUD/enter" ) == nullptr ) {
        return;
    }
    if ( count > kMaxWorldMarkers ) {
        count = kMaxWorldMarkers;
    }
    const bool bAny = count > 0;
    if ( bAny && !bMarkersVisible_ ) {
        bMarkersVisible_ = true;
    }
    for ( int32_t i = 0; i < kMaxWorldMarkers; i++ ) {
        // duplicateMovieClip puts the copy in the source's PARENT, so the
        // markers are /HUD/wm<N> siblings of the element, not its children.
        const std::string path = "/HUD/wm" + std::to_string( i );
        if ( i >= count ) {
            if ( markerClones_[i].bMade ) {
                hud.setClipVisible( path, false );
            }
            continue;
        }
        if ( !markerClones_[i].bMade ) {
            // A clone of the whole element (icon tile + prompt): the prompt
            // child is hidden, only the tile shows (duplicateMovieClip, the
            // way the map builds its spot icons).
            if ( !hud.duplicate( "/HUD/enter", "wm" + std::to_string( i ), 4000 + i ) ) {
                static bool bLogged = false;
                if ( !bLogged ) {
                    bLogged = true;
                    OTDU_LOG_WARN( "FreeRideHud: world marker clones not available\n" );
                }
                continue;
            }
            markerClones_[i].bMade = true;
            hud.setClipVisible( path + "/enter_txt", false );
            hud.gotoLabel( path, "stop", false );
            // The same parked minimap copy the 'stop' timeline places (see
            // showEnterSpot) — the clones move anywhere on screen, so theirs
            // would smudge beside every marker.
            hud.setDepthVisible( path, 1, false );
            hud.stopSubtree( path );
            for ( const char *pChild : kSpotHalos ) {
                hud.setClipVisible( path + "/Spot/" + pChild, false );
            }
            // The clone carries the element's unnamed prompt bar too (see
            // showEnterSpot): hidden, or every marker paints a beige rectangle
            // at its own offset.
            hud.setInstanceVisible( path, 1, false );
            // ... and its card (the dark panel + button glyph behind the
            // prompt): a distant marker is the tile alone (the owner's retail
            // capture: the house tile floating over the home down the road).
            for ( uint16_t depth : kEnterCardDepths ) {
                hud.setInstanceVisible( path, depth, false );
            }
            hud.setClipVisible( path, true );
        }
        if ( markerClones_[i].Kind != pMarkers[i].pSpotKind ) {
            markerClones_[i].Kind = pMarkers[i].pSpotKind;
            // "<kind>+<halo>": the tile with one Spot halo child on ('target'
            // for the GPS destination, ...).
            std::string kind = pMarkers[i].pSpotKind;
            std::string halo;
            const size_t plus = kind.find( '+' );
            if ( plus != std::string::npos ) {
                halo = kind.substr( plus + 1 );
                kind.resize( plus );
            }
            hud.gotoLabel( path + "/Spot/offroad", "default", false );
            if ( !hud.gotoLabel( path + "/Spot", kind, false ) ) {
                OTDU_LOG_WARN( "FreeRideHud: marker art '%s' not found\n", kind.c_str() );
            }
            // The kind goto re-places the halo children visible: hidden again
            // (showEnterSpot does the same), except the requested one.
            for ( const char *pChild : kSpotHalos ) {
                const bool bOn = halo == pChild;
                hud.setClipVisible( path + "/Spot/" + pChild, bOn );
                if ( bOn ) {
                    hud.gotoLabel( path + "/Spot/" + pChild, "on", false );
                }
            }
            hud.stopSubtree( path ); // the goto's fresh children play otherwise
            markerClones_[i].LogoIndex = -2; // the kind goto re-placed ic
        }
        if ( markerClones_[i].LogoIndex != pMarkers[i].LogoIndex ) {
            markerClones_[i].LogoIndex = pMarkers[i].LogoIndex;
            // A dealer's brand: Spot/ic 'brands', its logo clip on the brand's
            // frame (_logo_global_index + 1) and the lure quad bound to the
            // brand's 64 px cell of the 1024 x 256 brands_map atlas (16 x 4
            // cells) - the map screen's own rule (MapScreen spot build).
            const int32_t logo = pMarkers[i].LogoIndex;
            if ( logo >= 0 ) {
                hud.gotoLabel( path + "/Spot/ic", "brands", false );
                hud.gotoFrame( path + "/Spot/ic/logo", static_cast<uint32_t>( logo + 1 ), false );
                GPUTexture *pAtlas = hud.getMovie()->findPage( "brands_map" );
                bool bBound = false;
                if ( pAtlas != nullptr ) {
                    const float u0 = static_cast<float>( ( logo % 16 ) * 64 ) / 1024.0f;
                    const float v0 = static_cast<float>( ( logo / 16 ) * 64 ) / 256.0f;
                    bBound = hud.setBitmapOverride( path + "/Spot/ic/logo/lure", pAtlas, u0, v0, u0 + 64.0f / 1024.0f,
                                                    v0 + 64.0f / 256.0f );
                }
                if ( CmdLine::HasArg( "-logmarkers" ) ) {
                    OTDU_LOG_INFO( "FreeRideHud: %s logo %d: brands_map %p, lure bound %d\n", path.c_str(), logo,
                                   static_cast<void *>( pAtlas ), bBound ? 1 : 0 );
                }
                hud.stopSubtree( path );
            }
        }
        // The clone is a dynamic instance (no timeline re-places it): the
        // whole element moves, like the map's markers.
        // The tile is not at the element's origin: Spot sits at (8.45, 313.05)
        // of the element at 'stop' [DATA: -logmarkers clone dump] and its tile
        // centre ~33 px above the Spot origin - the marker is moved by that
        // much (scaled) so the TILE centre lands on the projected spot
        // (measured: -markertest, tile centre on the pinned canvas point).
        const float scale = pMarkers[i].ScalePercent * 0.01f;
        float anchorX = 0.0f, anchorY = 0.0f;
        FlashCanvasToStage( pMarkers[i].CanvasX, pMarkers[i].CanvasY, hudStageWidth(), hudStageHeight(), anchorX,
                            anchorY );
        hud.setClipTransform( path, anchorX - kMarkerTileDX * scale, anchorY - kMarkerTileDY * scale, 0.0f,
                              pMarkers[i].ScalePercent, pMarkers[i].ScalePercent );
    }
    if ( !bAny && bMarkersVisible_ ) {
        bMarkersVisible_ = false;
    }
    // -logmarkers: the first clone's tree once, 30 calls after it was made
    // (the visibility / alpha of every child: why a marker does not draw).
    static uint32_t markerCalls = 0;
    if ( CmdLine::HasArg( "-logmarkers" ) && count > 0 && ++markerCalls == 30 ) {
        if ( FlashPlayer::Clip *pClone = hud.findClip( "/HUD/wm0" ) ) {
            dumpClips( *pClone, "/HUD/wm0", 0 );
        } else {
            OTDU_LOG_INFO( "FreeRideHud: /HUD/wm0 not found\n" );
        }
    }
}

// ---- the Hint box (GENERAL.vmf /pop_tutorial) ----

void FreeRideHud::showHint( const std::string &text, float autoHide ) {
    // FUN_00d62ed0 / FUN_00d62a90 (the walk mode's "Tip" box): /:TutText,
    // pop_tutorial "open_tuto"; closed by the movie's close_popup (the same
    // call IntroCinematic::closeTutorial uses).
    if ( !general.isLoaded() ) {
        return;
    }
    general.setVariable( "/:TutText", text );
    general.gotoLabel( "/pop_tutorial", "open_tuto", true );
    hintTimer_ = autoHide;
}

void FreeRideHud::closeHint() {
    // Same as closeGoal: the auto-hide timer calls this after the timer went
    // negative, the guard must not depend on its sign.
    if ( general.isLoaded() ) {
        general.invoke( "/pop_tutorial", "close_popup", {} );
    }
    hintTimer_ = -1.0f;
}

// ---- the race countdown (/_3_2_1_go) ----

void FreeRideHud::showCountdown( int32_t number ) {
    if ( !hud.isLoaded() || hud.findClip( "/_3_2_1_go" ) == nullptr ) {
        return;
    }
    if ( number < 0 ) {
        if ( countdownNumber_ >= 0 ) {
            countdownNumber_ = -1;
            hud.gotoFrame( "/_3_2_1_go", 1, false );
            hud.setClipVisible( "/_3_2_1_go", false );
        }
        return;
    }
    if ( countdownNumber_ == number ) {
        return;
    }
    countdownNumber_ = number;
    hud.setClipVisible( "/_3_2_1_go", true );
    // The timeline sections: "3_go" f2, "2_go" f39, "1_go" f76, "go" f113
    // [DATA: -dumphud]; "/go" is the bare "GO!" flash used without a count.
    const char *pLabel = number == 0 ? "go" : number == 1 ? "1_go" : number == 2 ? "2_go" : "3_go";
    if ( !hud.gotoLabel( "/_3_2_1_go", pLabel, true ) ) {
        OTDU_LOG_WARN( "FreeRideHud: countdown label '%s' missing\n", pLabel );
    }
}

// ---- the chase mode HUD (police) ----

void FreeRideHud::setChaseHud( bool bChase, float awareness01, float escape01, bool bTimerVisible, float timerSeconds,
                               bool bArrested ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    // Root switches the movie reads (IsUnderArrest in its own test scripts
    // [DATA: HUD.vmf const pool]). IsPolice stays off: the movie shows its
    // online chase_target marker (the red car with the distance) on it, the
    // cop side of the retail mode the port does not play.
    hud.setVariableBool( "/:IsPolice", false );
    hud.setVariableBool( "/:IsUnderArrest", bArrested );
    // The infringement gauge [DATA: HUD.vmf, -dumphud -dumpsprite 502,501,487,478]:
    // /HUD/gauge_content (char 503) holds two copies of the generic HUD gauge
    // (char 502, `gauge` and `gauge2`). Its timeline: 'default' f1 (parked off
    // stage right), 'open' f8 (the frame script reads /:cur_gm and plays on to
    // 'up' f16 for a free-ride mode: the gauge slides in), 'close' f282.
    // Its `content` (char 501) is the fill: 1000 frames that slide the bar
    // (char 450, depth 10) 160 px under its mask together with the cursor -
    // frame 1 is full, frame 1000 is empty. `content/ic` (char 478) and
    // `content/bar` (char 487) are the mode, by label: driving / adrenaline /
    // jolt / penalty / infringement / escape / life / outlaw_life (each a
    // stop frame). The infringement icon's text is the EditText 467 bound to
    // /:m_Infraction (default "_Infringement"), the escape one /:m_escape_.
    // _root.isNoCursor hides the cursor + its text (the 'up' frame script).
    // (The tailing meter /HUD/tailing the port used before is the online
    // "Keep Your Distance" tailing event - /:distance_KYD - not the police.)
    // Which mode the retail chase shows is [ENC]; 'escape' during a pursuit
    // is [INF] from the label set (the outlaw's break-away bar).
    static const char *kGauge = "/HUD/gauge_content/gauge";
    if ( hud.findClip( kGauge ) != nullptr ) {
        const bool bWanted = !bArrested && ( bChase || awareness01 > 0.0f );
        if ( bWanted ) {
            if ( !bViolationGaugeShown_ ) {
                bViolationGaugeShown_ = true;
                violationGaugeHide_ = 0.0f;
                violationGaugeMode_.clear();
                violationGaugeFrame_ = ~0u;
                hud.setVariableBool( "/:isNoCursor", true );
                hud.setVariable( "/:m_Infraction", LocStr( 763, "Traffic violation" ) );
                hud.setVariable( "/:m_escape_", LocStr( 1978, "Escape" ) );
                hud.setClipVisible( "/HUD/gauge_content", true );
                hud.setClipVisible( "/HUD/gauge_content/gauge", true );
                hud.setClipVisible( "/HUD/gauge_content/gauge2", false );
                hud.gotoLabel( kGauge, "open", true );
            }
            const char *pMode = bChase ? "escape" : "infringement";
            if ( violationGaugeMode_ != pMode ) {
                violationGaugeMode_ = pMode;
                hud.gotoLabel( "/HUD/gauge_content/gauge/content/ic", pMode, false );
                hud.gotoLabel( "/HUD/gauge_content/gauge/content/bar", pMode, false );
            }
            const float fill = Clamp( bChase ? escape01 : awareness01, 0.0f, 1.0f );
            const uint32_t frame = static_cast<uint32_t>( ( 1.0f - fill ) * 999.0f + 0.5f );
            if ( frame != violationGaugeFrame_ ) {
                violationGaugeFrame_ = frame;
                hud.gotoFrame( "/HUD/gauge_content/gauge/content", frame, false );
            }
        } else if ( bViolationGaugeShown_ ) {
            bViolationGaugeShown_ = false;
            hud.gotoLabel( kGauge, "close", true );
            // 'close' f282 .. the 'default' stop at f303: ~0.9 s at 24 fps.
            violationGaugeHide_ = 1.0f;
        }
    }
    // The arrest countdown (char 931: "default" f1, "open" f8, "stop" f21,
    // "change" f27, "failure" f39, "success" f65 [DATA]).
    if ( hud.findClip( "/HUD/chase_timer" ) != nullptr ) {
        if ( bTimerVisible ) {
            if ( !bChaseTimerShown_ ) {
                bChaseTimerShown_ = true;
                hud.setClipVisible( "/HUD/chase_timer", true );
                // Top right, under the tailing meter (the retail capture):
                // canvas (1030, 100) through the UI layout mapping.
                float timerX = 0.0f, timerY = 0.0f;
                FlashCanvasToStage( 1030.0f, 100.0f, hudStageWidth(), hudStageHeight(), timerX, timerY );
                hud.setClipTransform( "/HUD/chase_timer", timerX, timerY, 0.0f, 100.0f, 100.0f );
                hud.gotoLabel( "/HUD/chase_timer", "open", true );
            }
            // mm'ss"cc of the seconds left (the retail digits 02'24"99).
            const int32_t total = Max( 0, static_cast<int32_t>( timerSeconds * 100.0f ) );
            char text[16];
            snprintf( text, sizeof( text ), "%02d'%02d\"%02d", total / 6000, ( total / 100 ) % 60, total % 100 );
            if ( chaseTimerText_ != text ) {
                chaseTimerText_ = text;
                // (/:m_escape_ is the escape gauge's label, not the timer.)
                hud.setVariable( "/:Xtimer", text );
                // The digits child (5 frames, one per digit slot) [DATA]; the
                // text variables are the fallback the movie reads [INF: the
                // retail digit feed is not decoded, the box shows its first
                // slot only].
            }
        } else if ( bChaseTimerShown_ ) {
            bChaseTimerShown_ = false;
            chaseTimerText_.clear();
            hud.gotoLabel( "/HUD/chase_timer", bArrested ? "failure" : "stop", true );
            // The "stop" / "failure" sections end on a frame that still shows
            // the ring (the movie's own close is the exe's job): hide the clip
            // once the section has played (~1.5 s [DATA: 'failure' f39 ..
            // 'success' f65, 26 frames at 24 fps]).
            chaseTimerHide_ = 1.6f;
        }
    }
    // The arrest (the handcuffs state of the movie [INF: IsUnderArrest is the
    // switch its own scripts read]).
    if ( bArrested != bArrestedShown_ ) {
        bArrestedShown_ = bArrested;
    }
}

void FreeRideHud::setRaceTimer( bool bVisible, float seconds, int32_t checkpoint, int32_t count ) {
    if ( !bVisible ) {
        bRaceTimerShown_ = false;
        return;
    }
    bRaceTimerShown_ = true;
    char text[48];
    snprintf( text, sizeof( text ), "%d:%05.2f   gate %d/%d", static_cast<int32_t>( seconds ) / 60,
              std::fmod( seconds, 60.0f ), checkpoint, count );
    raceTimerText_ = text;
}

void FreeRideHud::setRaceHud( const RaceHud &race ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    const bool bHasTime = hud.findClip( "/HUD/time" ) != nullptr;
    const bool bHasCheckpoints = hud.findClip( "/HUD/checkpoints" ) != nullptr;
    const bool bHasLaps = hud.findClip( "/HUD/laps" ) != nullptr;
    if ( !race.bVisible ) {
        if ( bRaceHudShown_ ) {
            bRaceHudShown_ = false;
            if ( bHasTime ) {
                hud.setClipVisible( "/HUD/time", false );
            }
            if ( bHasCheckpoints ) {
                hud.setClipVisible( "/HUD/checkpoints", false );
                hud.setClipVisible( "/HUD/checkpoint_bottom", false );
            }
        }
        if ( bRaceLapsShown_ ) {
            bRaceLapsShown_ = false;
            if ( bHasLaps ) {
                hud.setClipVisible( "/HUD/laps", false );
            }
        }
        raceTimeText_.clear();
        raceCheckpointShown_ = -1;
        raceLapShown_ = -1;
        return;
    }
    // The movie's own elements; the native line stays the fallback when the
    // HUD has none of them (an older movie).
    if ( !bHasTime && !bHasCheckpoints ) {
        setRaceTimer( true, race.Seconds, race.Checkpoint, race.Checkpoints );
        return;
    }
    bRaceTimerShown_ = false;
    if ( !bRaceHudShown_ ) {
        bRaceHudShown_ = true;
        if ( bHasTime ) {
            hud.setClipVisible( "/HUD/time", true );
            // Below the race position when it is up (setRacePosition).
            hud.gotoLabel( "/HUD/time", racePositionShown_ > 0 ? "down" : "open", true );
        }
        if ( bHasCheckpoints ) {
            hud.setClipVisible( "/HUD/checkpoints", true );
            hud.gotoLabel( "/HUD/checkpoints", "open", true );
            // The n / N counter fades in on its own "anim" frames (alpha 0 at
            // rest, like /HUD/laps/cart) [DATA: the timeline]. It sits where
            // the lap counter does: a circuit shows its laps instead [INF].
            if ( race.Laps <= 1 ) {
                hud.gotoLabel( "/HUD/checkpoints/cart", "anim", true );
            } else {
                hud.gotoLabel( "/HUD/checkpoints/cart", "default", false );
            }
            // The split / gap rows open on a checkpoint only.
            hud.gotoLabel( "/HUD/checkpoints/chck_box", "empty", false );
            hud.gotoLabel( "/HUD/checkpoints/cur_time/cur_time", "empty", false );
            hud.setVariable( "/:CheckTime", "" );
            hud.setVariable( "/:Gap", "" );
        }
        OTDU_LOG_INFO( "FreeRideHud: race HUD open (%d checkpoint(s), %d lap(s))\n", race.Checkpoints, race.Laps );
    }
    // mm:ss.cc (the movie's three fields; the separators are its own art).
    const int32_t total = Max( 0, static_cast<int32_t>( race.Seconds * 100.0f ) );
    char minutes[8], seconds[8], cents[8];
    snprintf( minutes, sizeof( minutes ), "%02d", total / 6000 );
    snprintf( seconds, sizeof( seconds ), "%02d", ( total / 100 ) % 60 );
    snprintf( cents, sizeof( cents ), "%02d", total % 100 );
    const std::string timeText = std::string( minutes ) + seconds + cents;
    if ( timeText != raceTimeText_ ) {
        raceTimeText_ = timeText;
        hud.setVariable( "/:Min", minutes );
        hud.setVariable( "/:Sec", seconds );
        hud.setVariable( "/:Cent", cents );
    }
    if ( race.Checkpoint != raceCheckpointShown_ ) {
        raceCheckpointShown_ = race.Checkpoint;
        hud.setVariable( "/:NbCheckpoints", std::to_string( race.Checkpoint ) );
        hud.setVariable( "/:TotalCheckpoints", std::to_string( race.Checkpoints ) );
    }
    if ( race.Laps > 1 && bHasLaps ) {
        if ( !bRaceLapsShown_ ) {
            bRaceLapsShown_ = true;
            hud.setClipVisible( "/HUD/laps", true );
            hud.gotoLabel( "/HUD/laps", "open", true );
            hud.gotoLabel( "/HUD/laps/cart", "anim", true );
        }
        if ( race.Lap != raceLapShown_ ) {
            raceLapShown_ = race.Lap;
            hud.setVariable( "/:NbTurns", std::to_string( race.Lap ) );
            hud.setVariable( "/:TotalTurns", std::to_string( race.Laps ) );
        }
    }
}

void FreeRideHud::setRacePosition( int32_t position, int32_t racers ) {
    if ( !hud.isLoaded() || hud.findClip( "/HUD/position" ) == nullptr ) {
        return;
    }
    if ( position <= 0 ) {
        if ( racePositionShown_ > 0 ) {
            hud.setClipVisible( "/HUD/position", false );
        }
        racePositionShown_ = -1;
        raceRacersShown_ = -1;
        return;
    }
    if ( racePositionShown_ <= 0 ) {
        hud.setClipVisible( "/HUD/position", true );
        hud.gotoLabel( "/HUD/position", "open", true );
        hud.gotoLabel( "/HUD/position/cart/ic", "race", false );
        // The position takes the timer's slot (top right): /HUD/time plays its
        // own 'down' (f40) below it [DATA: the labels; INF: the pairing].
        if ( bRaceHudShown_ && hud.findClip( "/HUD/time" ) != nullptr ) {
            hud.gotoLabel( "/HUD/time", "down", true );
        }
        OTDU_LOG_INFO( "FreeRideHud: race position open (%d of %d)\n", position, racers );
        if ( CmdLine::HasArg( "-dumphud" ) ) {
            dumpClips( *hud.findClip( "/HUD/position" ), "/HUD/position", 3 );
        }
    }
    static int32_t dumpFrames = 0;
    if ( CmdLine::HasArg( "-dumphud" ) && ++dumpFrames == 120 ) {
        const FlashPlayer::Clip *pClip = hud.findClip( "/HUD/position" );
        OTDU_LOG_INFO( "FreeRideHud: /HUD/position visible %d\n", pClip != nullptr && pClip->pInstance != nullptr && pClip->pInstance->bVisible ? 1 : 0 );
        dumpClips( *pClip, "/HUD/position", 3 );
    }
    if ( position != racePositionShown_ || racers != raceRacersShown_ ) {
        racePositionShown_ = position;
        raceRacersShown_ = racers;
        const char *pSuffix = "th";
        if ( position % 100 < 11 || position % 100 > 13 ) {
            pSuffix = position % 10 == 1 ? "st" : position % 10 == 2 ? "nd" : position % 10 == 3 ? "rd" : "th";
        }
        hud.setVariable( "/:Pos", std::to_string( position ) );
        hud.setVariable( "/:Exp", pSuffix );
        hud.setVariable( "/:NbPlayers", "/" + std::to_string( racers ) );
    }
}

void FreeRideHud::showCheckpointPassed( float seconds, bool bHasGap, float gap ) {
    if ( !hud.isLoaded() || hud.findClip( "/HUD/checkpoints" ) == nullptr ) {
        return;
    }
    const int32_t total = Max( 0, static_cast<int32_t>( seconds * 100.0f ) );
    char text[24];
    snprintf( text, sizeof( text ), "%02d:%02d.%02d", total / 6000, ( total / 100 ) % 60, total % 100 );
    hud.setVariable( "/:CheckTime", text );
    hud.gotoLabel( "/HUD/checkpoints", "chekpoint", true );
    hud.gotoLabel( "/HUD/checkpoints/cur_time", "gap", true );
    // The gap box: green ahead of the previous lap, red behind [INF: the
    // colours' meaning, from the retail convention of the HUD art].
    if ( bHasGap ) {
        const int32_t gapCents = static_cast<int32_t>( std::fabs( gap ) * 100.0f + 0.5f );
        char gapText[24];
        snprintf( gapText, sizeof( gapText ), "%c%d.%02d", gap < 0.0f ? '-' : '+', gapCents / 100, gapCents % 100 );
        hud.setVariable( "/:Gap", gapText );
        hud.gotoLabel( "/HUD/checkpoints/chck_box", gap < 0.0f ? "green" : "red", true );
        hud.gotoLabel( "/HUD/checkpoints/cur_time/cur_time", gap < 0.0f ? "green" : "red", true );
    } else {
        hud.setVariable( "/:Gap", "" );
        hud.gotoLabel( "/HUD/checkpoints/chck_box", "empty", false );
        hud.gotoLabel( "/HUD/checkpoints/cur_time/cur_time", "default", true );
    }
    OTDU_LOG_INFO( "FreeRideHud: checkpoint at %s%s%s\n", text, bHasGap ? " gap " : "",
                   bHasGap ? std::to_string( gap ).c_str() : "" );
}

bool FreeRideHud::canvasToMinimap( float canvasX, float canvasY, float &outX, float &outY ) {
    const FlashMovie *pMovie = hud.getMovie();
    float minimapX = 0.0f, minimapY = 0.0f;
    if ( pMovie == nullptr || !hud.getClipStagePosition( "/HUD/minimap", minimapX, minimapY ) ) {
        return false;
    }
    float stageX = 0.0f, stageY = 0.0f;
    FlashCanvasToStage( canvasX, canvasY, pMovie->getVMF().getStageWidth(), pMovie->getVMF().getStageHeight(),
                        stageX, stageY );
    outX = stageX - minimapX;
    outY = stageY - minimapY;
    return true;
}

void FreeRideHud::setMinimapObjectives( const MinimapObjective *pObjectives, int32_t count ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    count = Clamp( count, 0, kMaxMinimapObjectives );
    // The heading-up map (drawMinimap's toMap): the car at 62 % of the
    // height, kMapMetersPerPixel canvas pixels per metre.
    const float centreX = mapRectX + 0.5f * mapRectW, centreY = mapRectY + 0.62f * mapRectH;
    const Vector3f forward( state.HeadingX, 0.0f, state.HeadingZ );
    const Vector3f right( -forward.z, 0.0f, forward.x );
    // The icons are 31 px: their centre stays this far inside the edge.
    constexpr float kMargin = 12.0f;
    const float minX = mapRectX + kMargin, maxX = mapRectX + mapRectW - kMargin;
    const float minY = mapRectY + kMargin, maxY = mapRectY + mapRectH - kMargin;
    for ( int32_t i = 0; i < kMaxMinimapObjectives; i++ ) {
        char path[48];
        snprintf( path, sizeof( path ), "/HUD/minimap/objectif_%02d", i + 1 );
        if ( i >= count ) {
            if ( bMinimapObjectiveShown_[i] ) {
                bMinimapObjectiveShown_[i] = false;
                hud.setClipVisible( path, false );
            }
            continue;
        }
        const Vector3f d( pObjectives[i].Position.x - state.CarX, 0.0f, pObjectives[i].Position.z - state.CarZ );
        float x = centreX + d.dot( right ) / kMapMetersPerPixel;
        float y = centreY - d.dot( forward ) / kMapMetersPerPixel;
        // Off the map: on the edge, along the line from the car.
        const float dx = x - centreX, dy = y - centreY;
        float t = 1.0f;
        if ( dx > 0.0f && x > maxX ) {
            t = Min( t, ( maxX - centreX ) / dx );
        } else if ( dx < 0.0f && x < minX ) {
            t = Min( t, ( minX - centreX ) / dx );
        }
        if ( dy > 0.0f && y > maxY ) {
            t = Min( t, ( maxY - centreY ) / dy );
        } else if ( dy < 0.0f && y < minY ) {
            t = Min( t, ( minY - centreY ) / dy );
        }
        x = centreX + dx * t;
        y = centreY + dy * t;
        float localX = 0.0f, localY = 0.0f;
        if ( !canvasToMinimap( x, y, localX, localY ) ) {
            continue;
        }
        if ( !bMinimapObjectiveShown_[i] ) {
            bMinimapObjectiveShown_[i] = true;
            hud.setClipVisible( path, true );
        }
        if ( minimapObjectiveLabel_[i] == nullptr || strcmp( minimapObjectiveLabel_[i], pObjectives[i].pLabel ) != 0 ) {
            minimapObjectiveLabel_[i] = pObjectives[i].pLabel;
            if ( !hud.gotoLabel( path, pObjectives[i].pLabel, false ) ) {
                OTDU_LOG_WARN( "FreeRideHud: minimap icon '%s' not found\n", pObjectives[i].pLabel );
            }
        }
        hud.setClipTransform( path, localX, localY, 0.0f, 100.0f, 100.0f );
    }
}

void FreeRideHud::showLastLap() {
    if ( !hud.isLoaded() || hud.findClip( "/HUD/checkpoints/last_lap" ) == nullptr ) {
        return;
    }
    hud.setVariable( "/:m_LastLap_", LocStr( 7429, "Last lap!" ) );
    hud.gotoLabel( "/HUD/checkpoints/last_lap", "open", true );
    OTDU_LOG_INFO( "FreeRideHud: last lap\n" );
}

void FreeRideHud::openChaseHelp() {
    // OpenChaseModeHsys (HUD root function, [CODE: the movie's own bytecode]):
    // _root.Clip = this; _root.hsysID = "f_chasemode"; openHelpSystem(0) ->
    // _root.Input = false, hsysTitle = m_HelpSytem_ + ": " + m_ChaseMode_,
    // nbPages = 2, help_system.openHelpSystem(...) (which registers the popup
    // as _root.Clip). The movie closes itself and fires FSCommand
    // chmo:hsys:clos (helpSystemClosed).
    if ( !hud.isLoaded() || bChaseHelpOpen_ ) {
        return;
    }
    hud.setVariable( "/:m_HelpSytem_", LocStr( 5787, "Help" ) );
    hud.setVariable( "/:m_ChaseMode_", LocStr( 6726, "Chase Mode" ) );
    hud.setVariable( "/:hsysID", "f_chasemode" );
    // The two pages (labels f_chasemode0 / f_chasemode1 of the page sprite)
    // bind their EditTexts to _root.m_TutoChaseMode1..4 [DATA: hud.bnk, the
    // FR authoring defaults "Le mode Poursuite possede sa propre
    // progression..." = loc 6681]. The victory counts of 6683 are runtime
    // data [ENC]; 8 / 35 are the values the port's ChaseMode uses [INF].
    auto replaceToken = []( std::string text, const char *pToken, const char *pValue ) {
        const size_t at = text.find( pToken );
        if ( at != std::string::npos ) {
            text.replace( at, strlen( pToken ), pValue );
        }
        return text;
    };
    hud.setVariable( "/:m_TutoChaseMode1",
                     LocStr( 6681, "Chase Mode has its own progression. The goal is to evade police as an outlaw, or arrest "
                                   "outlaws as a cop." ) );
    hud.setVariable( "/:m_TutoChaseMode2",
                     LocStr( 6682, "There are four ranks you can achieve for both outlaw and cop, each offering exclusive rewards!" ) );
    std::string ranks = LocStr( 6683, "As an outlaw, you must obtain \xc2\xa7OUTLAWVICTORY\xc2\xa7 consecutive wins to achieve the "
                                      "rank of public enemy. If you are caught and arrested, your rank will be reset and you will "
                                      "need to start over! As a police officer, you must obtain \xc2\xa7" "COPVICTORY\xc2\xa7 victories to "
                                      "reach the rank of special forces." );
    ranks = replaceToken( ranks, "\xc2\xa7OUTLAWVICTORY\xc2\xa7", "8" );
    ranks = replaceToken( ranks, "\xc2\xa7" "COPVICTORY\xc2\xa7", "35" );
    hud.setVariable( "/:m_TutoChaseMode3", ranks );
    hud.setVariable( "/:m_TutoChaseMode4",
                     LocStr( 6684, "The total fine for an arrest is calculated based on your outlaw rank." ) );
    // The port hides the popup at load (the `default` root frame the port
    // never plays would); it is shown for as long as it is open.
    chaseHelpHide_ = 0.0f;
    hud.setClipVisible( "/help_system", true );
    if ( hud.invoke( "/", "OpenChaseModeHsys", {} ) ) {
        bChaseHelpOpen_ = true;
        OTDU_LOG_INFO( "FreeRideHud: Chase Mode help opened\n" );
    } else {
        hud.setClipVisible( "/help_system", false );
        OTDU_LOG_WARN( "FreeRideHud: OpenChaseModeHsys not found\n" );
    }
}

void FreeRideHud::routeChaseHelpInput() {
    if ( !bChaseHelpOpen_ || !hud.isLoaded() || gpInput == nullptr ) {
        return;
    }
    // The help system registers itself as _root.Clip and defines A / B
    // (both close it: FSCommand sndp:valid / sndp:back, then closeHelpSystem)
    // and Move('left' | 'right') for the pages [DATA: hud.bnk help_system
    // actions; the same popup the casino slot HUD drives].
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        hud.invokeMethod( "/:Clip", "A", {} );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        hud.invokeMethod( "/:Clip", "B", {} );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
        hud.invokeMethod( "/:Clip", "Move", { "left" } );
    } else if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
        hud.invokeMethod( "/:Clip", "Move", { "right" } );
    }
}

void FreeRideHud::tickModal( float deltaTime ) {
    if ( hud.isLoaded() ) {
        hud.tick( deltaTime );
    }
    tickSavingLogo( deltaTime );
}

void FreeRideHud::tick( float deltaTime, const State &newState ) {
    state = newState;
    if ( goalTimer >= 0.0f ) {
        goalTimer -= deltaTime;
        if ( goalTimer < 0.0f ) {
            closeGoal();
        }
    }
    if ( hintTimer_ >= 0.0f ) {
        hintTimer_ -= deltaTime;
        if ( hintTimer_ < 0.0f ) {
            closeHint();
        }
    }
    if ( violationGaugeHide_ > 0.0f ) {
        violationGaugeHide_ -= deltaTime;
        if ( violationGaugeHide_ <= 0.0f && !bViolationGaugeShown_ && hud.isLoaded() ) {
            hud.gotoLabel( "/HUD/gauge_content/gauge", "default", false );
        }
    }
    if ( chaseHelpHide_ > 0.0f ) {
        chaseHelpHide_ -= deltaTime;
        if ( chaseHelpHide_ <= 0.0f && !bChaseHelpOpen_ && hud.isLoaded() ) {
            hud.setClipVisible( "/help_system", false );
        }
    }
    if ( chaseTimerHide_ > 0.0f ) {
        chaseTimerHide_ -= deltaTime;
        if ( chaseTimerHide_ <= 0.0f && !bChaseTimerShown_ && hud.isLoaded() ) {
            hud.setClipVisible( "/HUD/chase_timer", false );
        }
    }
    if ( hud.isLoaded() ) {
        // HUD root vars read by the movie (WhatGear "auto": automatic box).
        hud.setVariable( "/:WhatGear", "auto" );
        // FUN_009365a0 (GPS on the minimap): the distance left to the target
        // in the movie's own readout (/HUD/minimap/gps_distance, top left of
        // the screen art): /:obj_Distance the number, /:gps_Unit the unit —
        // both "" when there is no target (the movie default texts "2500" /
        // "mil" disappear) [CODE].
        if ( pRoute_ != nullptr && pRoute_->size() > 1 && routeLength_ > 1.0f ) {
            char distance[24];
            if ( routeLength_ >= 1000.0f ) {
                snprintf( distance, sizeof( distance ), "%.1f", routeLength_ / 1000.0f );
                hud.setVariable( "/:gps_Unit", "km" );
            } else {
                snprintf( distance, sizeof( distance ), "%d", static_cast<int32_t>( routeLength_ / 10.0f ) * 10 );
                hud.setVariable( "/:gps_Unit", "m" );
            }
            hud.setVariable( "/:obj_Distance", distance );
        } else {
            hud.setVariable( "/:obj_Distance", "" );
            hud.setVariable( "/:gps_Unit", "" );
        }
        // FUN_00967f10: /:Rpm (int) in the HUD movie too.
        hud.setVariableNumber( "/:Rpm", static_cast<int32_t>( newState.Rpm ) );
        updateFrim();
        hud.tick( deltaTime );
        // The enter element is placed visible by the movie's own /HUD timeline;
        // the port shows it only for a spot (showEnterSpot). Left visible it
        // paints its dark card in the middle of the driving screen. The world
        // markers are /HUD/wm<N> siblings, so hiding the element leaves them.
        if ( !bEnterShown_ ) {
            hud.setClipVisible( "/HUD/enter", false );
        }
    }
    if ( general.isLoaded() ) {
        // The saving logo (GENERAL.vmf /saving_logo, labels open / close) shows
        // while a save notice runs [DATA: displaySavingLogo]; the clip path is [INF].
        updateSavingLogo( deltaTime );
        general.tick( deltaTime );
    }
    if ( gauges.isLoaded() ) {
        updateGauges();
        gauges.tick( deltaTime );
    }
    // Zone: the cruisingzones.bin polygon under the car, named by the
    // DBCruisingZones rows [DATA]; outside every polygon the last zone stays.
    const CruisingZones &zones = GetCruisingZones();
    const int32_t zone = zones.find( state.CarX, state.CarZ );
    if ( zone >= 0 ) {
        zoneIndex = zone + 1;
        zoneName = zones.getName( zone );
    } else if ( zoneIndex == 0 ) {
        zoneIndex = 1;
        zoneName = LocStr( 3336, "Ibiza Area 1" );
    }
}

void FreeRideHud::showLevelUp( int32_t grade, const std::string &reward, bool bNewTitle, const std::string &newTitle, int32_t tier,
                              const std::string &rewardDesc, const std::string &globalTitle ) {
    const ProgressSnapshot snapshot = progress_;
    popupQueue_.push_back( [=]() {
        progress_ = snapshot;
        startLevelUp( grade, reward, bNewTitle, newTitle, tier, rewardDesc, globalTitle, true, std::string(), std::string() );
    } );
}

void FreeRideHud::showEarn( int32_t grade, const std::string &eventName, int32_t points ) {
    const ProgressSnapshot snapshot = progress_;
    popupQueue_.push_back( [=]() {
        progress_ = snapshot;
        startLevelUp( grade, std::string(), false, std::string(), 1, std::string(), std::string(), false, eventName,
                      "+" + std::to_string( points ) + " Pts" );
    } );
}

void FreeRideHud::startLevelUp( int32_t grade, const std::string &reward, bool bNewTitle, const std::string &newTitle, int32_t tier,
                                const std::string &rewardDesc, const std::string &globalTitle, bool bLevelUp,
                                const std::string &header, const std::string &pointsText ) {
    if ( !general.isLoaded() ) {
        return;
    }
    static const char *kGrades[4] = { "racing", "cruising", "collection", "social" };
    static const char *kPrefixes[4] = { "Racing", "Cruising", "Collection", "Social" };
    grade &= 3;
    // _root.dbProgress( action ): WhatGrade picks the icon, WhatDbProgress the
    // body (earn_grade_points / earn_cruising_points ...), WhatArea the zone
    // icon of the Discovery popup (DBCruisingZones._flashlabel "i1" / "h2") [DATA].
    general.setVariable( "/:WhatGrade", kGrades[grade] );
    general.setVariable( "/:WhatDbProgress", grade == 1 ? "earn_cruising_points" : "earn_grade_points" );
    const std::string island = GetCruisingZones().getIsland();
    general.setVariable( "/:WhatArea", std::string( island == "hawai" ? "h" : "i" ) + std::to_string( Max( 1, zoneIndex ) ) );
    general.setVariable( "/:WhatTuto", "levels" );
    general.setVariableBool( "/:NewGradeTitle", bNewTitle );
    // A global title (every 10 global levels, DBPlayerLevel) adds its own page.
    general.setVariableBool( "/:NewGlobalTitle", !globalTitle.empty() );
    general.setVariableBool( "/:NewAreaTitle", false ); // the A_first branches compare it with 0 (undefined is not 0)
    levelUpGlobalTitle_ = globalTitle;
    general.setVariableBool( "/:ProgressCasinoOn", false );
    general.setVariableBool( "/:IsLevelUp", bLevelUp );
    general.setVariable( "/:TitleDbProgress", header.empty() ? general.getVariable( std::string( "/:m_" ) + kPrefixes[grade] + "Grade_" ) : header );
    general.setVariable( "/:NbPointsWon", pointsText );
    for ( int32_t i = 0; i < 4; i++ ) {
        const std::string prefix = std::string( "/:" ) + kPrefixes[i];
        general.setVariable( prefix + "LevelCount", progress_.LevelCount[i] );
        general.setVariable( prefix + "Title", progress_.Title[i] );
        general.setVariableNumber( prefix + "Points", progress_.Percent[i] );
    }
    // m_NextLevel_ = "Next level in §POINTS§ pts": the token is the points left.
    static std::string nextLevelTemplate;
    if ( nextLevelTemplate.empty() ) {
        nextLevelTemplate = general.getVariable( "/:m_NextLevel_" );
    }
    std::string nextLevel = nextLevelTemplate;
    const std::string token = "\xc2\xa7POINTS\xc2\xa7";
    const size_t tokenAt = nextLevel.find( token );
    if ( tokenAt != std::string::npos ) {
        nextLevel.replace( tokenAt, token.size(), std::to_string( progress_.NextLevelIn[grade] ) );
        general.setVariable( "/:m_NextLevel_", nextLevel );
    }
    // The popup has no reward line before the title page (its WhatReward
    // variable / A_first sequence are not driven [INF]): the reward goes to the goal bar.
    // Without a title page the reward only goes to the goal bar.
    if ( !bNewTitle && !reward.empty() ) {
        showGoal( reward );
    }
    general.setVariableNumber( "/:GlobalLevel", progress_.GlobalLevel );
    general.setVariable( "/:GlobalLevelTitle", globalTitle.empty() ? progress_.GlobalTitle : globalTitle );
    general.setVariable( "/:PlayerTitle", globalTitle.empty() ? progress_.GlobalTitle : globalTitle ); // the ID card of the global title page
    general.setVariable( "/:GlobalLevelCount", progress_.GlobalLevelCount );
    // New title page (disp frame grade_title): the reward icon <grade>_<tier>
    // (Discovery has a single one), /:NewTitle and m_NewTitleGrade_ "Congratulations,
    // you obtained a new §GRADE§ title!" [DATA, set_reward / grade_title scripts].
    if ( bNewTitle ) {
        general.setVariable( "/:WhatReward", grade == 1 ? std::string( "cruising" ) : std::string( kGrades[grade] ) + "_" + std::to_string( Clamp( tier, 1, 3 ) ) );
        general.setVariable( "/:NewTitle", newTitle );
        // The reward block of the title page: _descr (db_reward_desc), reward_name,
        // the header TitleDbProgress / NbPointsWon [DATA: variables of the text fields].
        general.setVariable( "/:reward_name", reward );
        general.setVariable( "/:db_reward_desc", rewardDesc );
        general.setVariable( "/:NbPointsWon", "" );
        levelUpTitle_ = newTitle;
        static std::string gradeTemplate;
        if ( gradeTemplate.empty() ) {
            gradeTemplate = general.getVariable( "/:m_NewTitleGrade_" );
        }
        std::string text = gradeTemplate;
        const std::string gradeToken = "\xc2\xa7GRADE\xc2\xa7";
        const size_t at = text.find( gradeToken );
        if ( at != std::string::npos ) {
            const std::string name = general.getVariable( std::string( "/:m_" ) + kPrefixes[grade] + "Grade_" );
            text.replace( at, gradeToken.size(), name );
            general.setVariable( "/:m_NewTitleGrade_", text );
        }
    }
    const bool bFound = general.invoke( "/", "dbProgress", { "open_mini" } );
    levelUpStage_ = 1;
    levelUpTimer_ = 0.0f;
    levelUpReward_ = reward;
    levelUpNewTitle_ = bNewTitle || !globalTitle.empty(); // any title page needs the A flow
    levelUpPresses_ = 0;
    OTDU_LOG_INFO( "FreeRideHud: level up '%s' %s -> dbProgress( open_mini ) %d\n", kGrades[grade], progress_.LevelCount[grade].c_str(),
                   bFound ? 1 : 0 );
}

void FreeRideHud::acceptLevelUp() {
    if ( levelUpStage_ != 2 || !general.isLoaded() ) {
        return;
    }
    levelUpTimer_ = 0.0f;
    if ( ++levelUpPresses_ > 3 ) {
        general.invoke( "/", "dbProgress", { "close_extd" } );
        levelUpStage_ = 0;
        general.setVariableBool( "/:IsLevelUp", false );
        return;
    }
    if ( levelUpPresses_ >= 2 && !levelUpTitle_.empty() ) {
        general.setVariable( "/:TitleDbProgress", levelUpTitle_ ); // the title page header
    }
    if ( levelUpPresses_ >= 3 && !levelUpGlobalTitle_.empty() ) {
        general.setVariable( "/:TitleDbProgress", levelUpGlobalTitle_ ); // the global title page header
    }
    general.sendKey( 13 );
    OTDU_LOG_INFO( "FreeRideHud: level up popup A press %d\n", levelUpPresses_ );
}

void FreeRideHud::updateSavingLogo( float deltaTime ) {
    if ( levelUpStage_ == 0 && !popupQueue_.empty() && general.isLoaded() ) {
        const std::function<void()> next = popupQueue_.front();
        popupQueue_.pop_front();
        next();
    }
    // Level-up popup: the mini bar for 3.5 s, then the extended gauges for 6 s.
    if ( levelUpStage_ > 0 ) {
        levelUpTimer_ += deltaTime;
        if ( levelUpStage_ == 1 && levelUpTimer_ > 3.5f ) {
            general.invoke( "/", "dbProgress", { "extend" } );
            levelUpStage_ = 2;
            levelUpTimer_ = 0.0f;
        } else if ( levelUpStage_ == 2 ) {
            // Without a new title the panel closes by itself after 6 s; with one,
            // the A button (or 3.5 s) steps through its pages, at most 3 presses.
            if ( !levelUpNewTitle_ && levelUpTimer_ > 6.0f ) {
                general.invoke( "/", "dbProgress", { "close_extd" } );
                levelUpStage_ = 0;
                general.setVariableBool( "/:IsLevelUp", false );
            } else if ( levelUpNewTitle_ && levelUpTimer_ > 3.5f ) {
                acceptLevelUp();
            }
        }
    }
    const bool bSaving = GetSaveNoticeTime() > 0.0f;
    if ( bSaving != bSavingLogo ) {
        bSavingLogo = bSaving;
        const bool bFound = general.gotoLabel( "/saving_logo", bSaving ? "open" : "close", true );
        OTDU_LOG_INFO( "FreeRideHud: saving logo %s -> %d\n", bSaving ? "open" : "close", bFound ? 1 : 0 );
    }
}

void FreeRideHud::tickSavingLogo( float deltaTime ) {
    // The pause menu and the map freeze the HUD; the logo still runs there.
    if ( general.isLoaded() ) {
        updateSavingLogo( deltaTime );
        general.tick( deltaTime );
    }
}

void FreeRideHud::drawSavingLogo( Renderer2D &renderer ) {
    if ( general.isLoaded() && bSavingLogo ) {
        general.drawClipOnly( renderer, "/saving_logo" );
    }
}

void FreeRideHud::draw( Renderer2D &renderer, const VehicleWorldCollision *pCollision ) {
    if ( bHidden ) {
        return; // photo camera: the frame is the photo
    }
    static const bool bLogTimes = CmdLine::HasArg( "-loghudtimes" );
    static HudTimes times;
    if ( bLogTimes ) {
        times.start();
    }
    if ( CmdLine::HasArg( "-loghudregion" ) ) {
        // -loghudregion [x0,x1,y0,y1] in canvas pixels (default: the bottom-left
        // band). Screen ratio mode 1 draws the 1280x960 stage at y -120 with
        // height 960, so canvas y 690 is stage y 810.
        static float region[4] = { 100.0f, 400.0f, 660.0f, 720.0f };
        static bool bRegionParsed = false;
        if ( !bRegionParsed ) {
            bRegionParsed = true;
            if ( const char *pRegion = CmdLine::GetArgValue( "-loghudregion" ) ) {
                float x0 = 0.0f, x1 = 0.0f, y0 = 0.0f, y1 = 0.0f;
                if ( sscanf( pRegion, "%f,%f,%f,%f", &x0, &x1, &y0, &y1 ) == 4 ) {
                    region[0] = x0;
                    region[1] = x1;
                    region[2] = y0;
                    region[3] = y1;
                }
            }
        }
        // Canvas -> stage (the stage is drawn at y -120, 1:1 in x).
        const float stageMinX = region[0], stageMaxX = region[1];
        const float stageMinY = region[2] + 120.0f, stageMaxY = region[3] + 120.0f;
        static int32_t regionFrames = 0;
        if ( ++regionFrames == 200 ) {
            OTDU_LOG_INFO( "HudRegion: --- hud.vmf (canvas %.0f..%.0f x %.0f..%.0f) ---\n", region[0], region[1],
                           region[2], region[3] );
            if ( hud.getRoot() != nullptr ) {
                DumpHudRegion( *hud.getRoot(), hud.getDefinitions(), FlashMatrix(), "", stageMinX, stageMaxX,
                               stageMinY, stageMaxY );
            }
            OTDU_LOG_INFO( "HudRegion: --- general.vmf ---\n" );
            if ( general.getRoot() != nullptr ) {
                DumpHudRegion( *general.getRoot(), general.getDefinitions(), FlashMatrix(), "", stageMinX, stageMaxX,
                               stageMinY, stageMaxY );
            }
            OTDU_LOG_INFO( "HudRegion: --- gauges.vmf ---\n" );
            if ( gauges.getRoot() != nullptr ) {
                DumpHudRegion( *gauges.getRoot(), gauges.getDefinitions(), FlashMatrix(), "", stageMinX, stageMaxX,
                               stageMinY, stageMaxY );
            }
        }
    }
    if ( !bShown ) {
        drawGeneralClips( renderer );
        return;
    }
    // The Screen ratio row changed the effective ratio: the HUD / GAUGES
    // arrangements (and the map rect) follow without a reload.
    if ( appliedScreenRatio_ != GetEffectiveScreenRatio() ) {
        applyScreenRatio();
    }
    const bool bSimpleHud = GetGameOptions().HudQuality <= 1;
    // The satnav map renders UNDER the HUD movie: the movie's minimap element
    // is the screen art (bezel, bottom band, status icons) and draws over the
    // map, like the retail engine rendering its map into the element.
    renderer.begin( 1280.0f, 720.0f );
    drawMinimap( renderer, pCollision );
    renderer.end();
    if ( !bSimpleHud && hud.isLoaded() ) {
        hud.draw( renderer );
    }
    if ( bLogTimes ) {
        times.mark( HudTimes::Flash );
    }
    // Custom station art: over the panel's logo box (the movie's own
    // radio_logo sprite is hidden while it is shown).
    if ( bRadioShown && pRadioIcon != nullptr ) {
        drawRadioIcon( renderer );
    }
    renderer.begin( 1280.0f, 720.0f );
    if ( bLogTimes ) {
        times.mark( HudTimes::Minimap );
    }
    if ( bSimpleHud || !gauges.isLoaded() ) {
        drawGauge( renderer );
    }
    if ( bLogTimes ) {
        times.mark( HudTimes::Gauge );
    }
    // The zone badge gives its corner to the race timer (/HUD/time sits
    // there) while a race runs.
    if ( !bRaceHudShown_ ) {
        drawZone( renderer );
    }
    drawFuel( renderer );
    if ( bLogTimes ) {
        times.mark( HudTimes::ZoneFuel );
    }
    if ( bRaceTimerShown_ && !raceTimerText_.empty() ) {
        renderer.drawText( gpFlash->getTitleFont(), raceTimerText_.c_str(), 640.0f, 44.0f, 26.0f, Rgba( 255, 60, 40 ),
                           eTextAlign::TA_Center );
    }
    if ( !prompt_.empty() ) {
        renderer.drawRect( Rect2D( 440.0f, 612.0f, 400.0f, 34.0f ), Rgba( 0, 0, 0, 170 ) );
        renderer.drawText( gpFlash->getTitleFont(), prompt_.c_str(), 640.0f, 618.0f, 20.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
    }
    renderer.end();
    if ( bLogTimes ) {
        times.mark( HudTimes::Prompt );
    }
    if ( !bSimpleHud && gauges.isLoaded() ) {
        // Dial texture (FUN_009661b0 / FUN_00966070 draw callbacks), the
        // movie, then the needle with the bank texture (FUN_009667a0 swaps
        // the aiguille bitmap) [INF: draw order].
        drawGaugeTextures( renderer, true );
        if ( bLogTimes ) {
            times.mark( HudTimes::GaugeTex );
        }
        gauges.draw( renderer );
        if ( bLogTimes ) {
            times.mark( HudTimes::Gauges );
        }
        drawGaugeTextures( renderer, false );
        if ( bLogTimes ) {
            times.mark( HudTimes::GaugeTex );
        }
        // Over the gauge cluster (the card stands on its right edge).
        drawCarSpecCard( renderer );
    }
    drawGeneralClips( renderer );
    if ( bLogTimes ) {
        times.mark( HudTimes::General );
        times.endFrame();
    }
}

void FreeRideHud::drawGeneralClips( Renderer2D &renderer ) {
    if ( !general.isLoaded() ) {
        return;
    }
    // drawClipOnly ignores the clip's own visibility (it only skips hidden
    // children), so a clip the movie has parked off its timeline still paints.
    // /db_progress is the level-up popup: its parked frame is a beige card that
    // landed at the bottom left of the driving screen, so it is drawn only while
    // the popup is up. /pop_tutorial is the hint box, drawn only while a hint is
    // open. /pop_goal (the goal bar) and /saving_logo are always drawn - the
    // movie's own open/close labels move them off stage.
    general.drawClipOnly( renderer, "/pop_goal" );
    if ( hintTimer_ >= 0.0f ) {
        general.drawClipOnly( renderer, "/pop_tutorial" );
    }
    if ( levelUpStage_ > 0 ) {
        general.drawClipOnly( renderer, "/db_progress" );
    }
    general.drawClipOnly( renderer, "/saving_logo" );
}

void FreeRideHud::drawGauge( Renderer2D &renderer ) {
    // Placeholder of the GAUGES movie [INF layout from the reference
    // screenshot]: rev counter 0..rpm_max (x1000), red from _red_line, speed
    // (km/h), gear, odometer.
    std::vector<Vertex2D> vertices;
    const float a0 = 0.75f * kPI, a1 = 2.25f * kPI; // 270 degrees, clockwise from bottom left
    const float maxRpm = std::ceil( state.RpmMax / 1000.0f ) * 1000.0f;
    PushArc( vertices, kGaugeX, kGaugeY, 0.0f, kGaugeRadius, 0.0f, 2.0f * kPI, Rgba( 0, 0, 0, 110 ) );
    PushArc( vertices, kGaugeX, kGaugeY, kGaugeRadius - 4.0f, kGaugeRadius, a0, a1, Rgba( 230, 230, 230, 220 ) );
    const float redStart = a0 + ( a1 - a0 ) * Clamp( state.RedLine / maxRpm, 0.0f, 1.0f );
    PushArc( vertices, kGaugeX, kGaugeY, kGaugeRadius - 14.0f, kGaugeRadius - 4.0f, redStart, a1, Rgba( 200, 20, 20, 230 ) );
    for ( int32_t k = 0; k <= static_cast<int32_t>( maxRpm / 1000.0f ); k++ ) {
        const float a = a0 + ( a1 - a0 ) * k * 1000.0f / maxRpm;
        PushArc( vertices, kGaugeX, kGaugeY, kGaugeRadius - 16.0f, kGaugeRadius - 4.0f, a - 0.012f, a + 0.012f,
                 Rgba( 240, 240, 240, 255 ) );
    }
    const float needle = a0 + ( a1 - a0 ) * Clamp( state.Rpm / maxRpm, 0.0f, 1.0f );
    const Vector2f dir( std::cos( needle ), std::sin( needle ) ), side( -dir.y, dir.x );
    const Vector2f c( kGaugeX, kGaugeY );
    PushTriangle( vertices, c.x + side.x * 3.0f, c.y + side.y * 3.0f, c.x - side.x * 3.0f, c.y - side.y * 3.0f,
                  c.x + dir.x * ( kGaugeRadius - 8.0f ), c.y + dir.y * ( kGaugeRadius - 8.0f ), Rgba( 220, 30, 30, 255 ) );
    PushArc( vertices, kGaugeX, kGaugeY, 0.0f, 7.0f, 0.0f, 2.0f * kPI, Rgba( 20, 20, 20, 255 ) );
    // Odometer box.
    PushQuad( vertices, { 1040.0f, 662.0f }, { 1165.0f, 662.0f }, { 1165.0f, 684.0f }, { 1040.0f, 684.0f },
              Rgba( 20, 20, 20, 200 ) );
    renderer.drawTriangles( nullptr, vertices.data(), static_cast<uint32_t>( vertices.size() ) );

    const Font *pFont = gpFlash->getTitleFont();
    char text[32];
    for ( int32_t k = 0; k <= static_cast<int32_t>( maxRpm / 1000.0f ); k++ ) {
        const float a = a0 + ( a1 - a0 ) * k * 1000.0f / maxRpm;
        snprintf( text, sizeof( text ), "%d", k );
        renderer.drawText( pFont, text, kGaugeX + std::cos( a ) * ( kGaugeRadius - 28.0f ),
                           kGaugeY + std::sin( a ) * ( kGaugeRadius - 28.0f ) - 8.0f, 15.0f, Rgba( 235, 235, 235 ),
                           eTextAlign::TA_Center );
    }
    snprintf( text, sizeof( text ), "%03d", static_cast<int32_t>( std::abs( SpeedForDisplay( state.SpeedKmh ) ) + 0.5f ) );
    renderer.drawText( pFont, text, kGaugeX + 34.0f, kGaugeY + 12.0f, 22.0f, Rgba( 255, 210, 40 ), eTextAlign::TA_Center );
    // The unit under the digits (the movie's own /:SpeedUnit text).
    renderer.drawText( pFont, SpeedUnitLabel(), kGaugeX + 34.0f, kGaugeY + 34.0f, 13.0f, Rgba( 235, 235, 235 ),
                       eTextAlign::TA_Center );
    const char *pGear = state.Gear < 0 ? "R" : nullptr;
    char gearText[8];
    if ( pGear == nullptr ) {
        if ( state.Gear == 0 ) {
            pGear = "N";
        } else {
            snprintf( gearText, sizeof( gearText ), "%d", state.Gear );
            pGear = gearText;
        }
    }
    renderer.drawText( pFont, pGear, kGaugeX + 34.0f, kGaugeY + 52.0f, 24.0f, Rgba( 255, 210, 40 ), eTextAlign::TA_Center );
    // Odometer: km or miles (game/units.h), one tenth in the box's last digit.
    snprintf( text, sizeof( text ), "%09d", static_cast<int32_t>( KmForDisplay( state.OdometerKm ) ) );
    renderer.drawText( pFont, text, 1102.0f, 663.0f, 19.0f, Rgba( 240, 240, 240 ), eTextAlign::TA_Center, 2.0f );
}

void FreeRideHud::drawMinimap( Renderer2D &renderer, const VehicleWorldCollision *pCollision ) {
    // The map surface: the movie's own map quad (computeMapRect), drawn under
    // the HUD movie so its bezel / bottom band / status icons overlay the map
    // like the retail satnav. No frame of our own — the movie is the frame.
    const float x0 = mapRectX, y0 = mapRectY, x1 = mapRectX + mapRectW, y1 = mapRectY + mapRectH;
    // The road triangles are clipped by a GPU scissor instead of
    // Sutherland-Hodgman on the CPU, and the vertex list is rebuilt at ~20 Hz
    // only when something moved (session 73 `cline`; the d92c85e merge kept
    // the satnav and lost this, -loghudtimes measured ~9 ms/frame again).
    // -nominimapcache rebuilds every frame (A/B).
    static const bool bNoCache = CmdLine::HasArg( "-nominimapcache" );
    MinimapCache &cache = minimapCache_;
    ++cache.Frame;
    const size_t triangleCount = pCollision != nullptr ? pCollision->getNumTriangles() : 0;
    const bool bChanged = !cache.bValid || bNoCache || pCollision != cache.pCollision ||
                          triangleCount != cache.TriangleCount || std::fabs( state.CarX - cache.CarX ) > 0.75f ||
                          std::fabs( state.CarZ - cache.CarZ ) > 0.75f ||
                          std::fabs( state.HeadingX - cache.HeadingX ) > 0.01f ||
                          std::fabs( state.HeadingZ - cache.HeadingZ ) > 0.01f || x0 != cache.RectX ||
                          y0 != cache.RectY || mapRectW != cache.RectW || mapRectH != cache.RectH ||
                          pRoute_ != cache.pRoute || routeLength_ != cache.RouteLength ||
                          ( pRoute_ != nullptr && pRoute_->size() != cache.RouteSize );
    const bool bRebuild = bChanged && ( !cache.bValid || bNoCache || cache.Frame - cache.BuildFrame >= 3 ||
                                        x0 != cache.RectX || y0 != cache.RectY || mapRectW != cache.RectW ||
                                        mapRectH != cache.RectH );
    renderer.setClipRect( Rect2D( x0, y0, mapRectW, mapRectH ) );
    if ( !bRebuild ) {
        renderer.drawTriangles( nullptr, cache.Vertices.data(), static_cast<uint32_t>( cache.Vertices.size() ) );
        renderer.clearClipRect();
    }
    std::vector<Vertex2D> &vertices = cache.Vertices;
    if ( bRebuild ) {
        cache.bValid = true;
        cache.BuildFrame = cache.Frame;
        cache.pCollision = pCollision;
        cache.TriangleCount = triangleCount;
        cache.CarX = state.CarX;
        cache.CarZ = state.CarZ;
        cache.HeadingX = state.HeadingX;
        cache.HeadingZ = state.HeadingZ;
        cache.RectX = x0;
        cache.RectY = y0;
        cache.RectW = mapRectW;
        cache.RectH = mapRectH;
        cache.pRoute = pRoute_;
        cache.RouteLength = routeLength_;
        cache.RouteSize = pRoute_ != nullptr ? pRoute_->size() : 0;
        vertices.clear();
        PushQuad( vertices, { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 }, Rgba( 12, 14, 18, 255 ) );
        const Vector2f center( 0.5f * ( x0 + x1 ), y0 + 0.62f * mapRectH );
        const Vector3f forward( state.HeadingX, 0.0f, state.HeadingZ );
        const Vector3f right( -forward.z, 0.0f, forward.x );
        auto toMap = [&]( const Vector3f &p ) {
            const Vector3f d( p.x - state.CarX, 0.0f, p.z - state.CarZ );
            return Vector2f( center.x + d.dot( right ) / kMapMetersPerPixel, center.y - d.dot( forward ) / kMapMetersPerPixel );
        };
        if ( pCollision != nullptr ) {
            // GamePC.cpr [HudMap]: AIRL_ROAD unlocked colour 0 255 255 (the
            // reference shows white roads at this stage [INF]). Unclipped: the
            // scissor above trims them to the map surface.
            const uint32_t roadColor = Rgba( 235, 235, 235, 255 );
            const float radius = mapRectW * kMapMetersPerPixel;
            constexpr float kQueryMargin = 150.0f;
            const float dx = state.CarX - cache.QueryX, dz = state.CarZ - cache.QueryZ;
            if ( cache.pQueryCollision != pCollision || cache.QueryTriangleCount != triangleCount ||
                 radius + kQueryMargin * 0.5f > cache.QueryRadius ||
                 dx * dx + dz * dz > kQueryMargin * kQueryMargin * 0.25f ) {
                cache.RoadTriangles.clear();
                cache.QueryX = state.CarX;
                cache.QueryZ = state.CarZ;
                cache.QueryRadius = radius + kQueryMargin;
                cache.QueryTriangleCount = triangleCount;
                cache.pQueryCollision = pCollision;
                pCollision->forEachRoadTriangle( Vector3f( state.CarX, 0.0f, state.CarZ ), cache.QueryRadius,
                                                 [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
                                                     cache.RoadTriangles.push_back( a );
                                                     cache.RoadTriangles.push_back( b );
                                                     cache.RoadTriangles.push_back( c );
                                                 } );
            }
            // Only what can reach the map square (the query cells cover the
            // square of `radius` around the car; the scissor trims the rest).
            const float reach = radius + 30.0f;
            for ( size_t t = 0; t + 2 < cache.RoadTriangles.size(); t += 3 ) {
                const Vector3f &a = cache.RoadTriangles[t];
                if ( std::fabs( a.x - state.CarX ) > reach || std::fabs( a.z - state.CarZ ) > reach ) {
                    continue;
                }
                const Vector2f pa = toMap( a ), pb = toMap( cache.RoadTriangles[t + 1] ),
                               pc = toMap( cache.RoadTriangles[t + 2] );
                PushTriangle( vertices, pa.x, pa.y, pb.x, pb.y, pc.x, pc.y, roadColor );
            }
        }
        // GPS route: a red line along the route within the minimap radius.
        if ( pRoute_ != nullptr && pRoute_->size() > 1 ) {
            const float radius = mapRectW * kMapMetersPerPixel;
            auto clipSegment = [&]( Vector2f &a, Vector2f &b ) {
                // Liang-Barsky against the minimap rectangle.
                float t0 = 0.0f, t1 = 1.0f;
                const float dx = b.x - a.x, dy = b.y - a.y;
                const float p[4] = { -dx, dx, -dy, dy };
                const float q[4] = { a.x - x0, x1 - a.x, a.y - y0, y1 - a.y };
                for ( int32_t i = 0; i < 4; i++ ) {
                    if ( p[i] == 0.0f ) {
                        if ( q[i] < 0.0f ) {
                            return false;
                        }
                    } else {
                        const float r = q[i] / p[i];
                        if ( p[i] < 0.0f ) {
                            t0 = Max( t0, r );
                        } else {
                            t1 = Min( t1, r );
                        }
                        if ( t0 > t1 ) {
                            return false;
                        }
                    }
                }
                const Vector2f start( a.x + t0 * dx, a.y + t0 * dy ), end( a.x + t1 * dx, a.y + t1 * dy );
                a = start;
                b = end;
                return true;
            };
            const uint32_t routeColor = Rgba( 255, 102, 0, 255 ); // GamePC.cpr [HudMap] GPS path
            for ( size_t i = 0; i + 1 < pRoute_->size(); i++ ) {
                const Vector3f &p0 = ( *pRoute_ )[i];
                const Vector3f &p1 = ( *pRoute_ )[i + 1];
                if ( std::fabs( p0.x - state.CarX ) > radius && std::fabs( p1.x - state.CarX ) > radius ) {
                    continue;
                }
                Vector2f a = toMap( p0 ), b = toMap( p1 );
                if ( !clipSegment( a, b ) ) {
                    continue;
                }
                const float dx = b.x - a.x, dy = b.y - a.y;
                const float length = std::sqrt( dx * dx + dy * dy );
                if ( length < 0.01f ) {
                    continue;
                }
                const float nx = -dy / length * 2.0f, ny = dx / length * 2.0f;
                PushQuad( vertices, { a.x + nx, a.y + ny }, { b.x + nx, b.y + ny }, { b.x - nx, b.y - ny }, { a.x - nx, a.y - ny },
                          routeColor );
            }
        }
        // The player marker is the movie's own /HUD/minimap/cursor clip, parked on
        // the map centre at load — no arrow of our own over the movie art.
        renderer.drawTriangles( nullptr, vertices.data(), static_cast<uint32_t>( vertices.size() ) );
        renderer.clearClipRect();
    }
    // Metal detector: a bar that fills as the wreck gets closer (the real device
    // bips faster, 1.35 s far to 0.20 s near) [INF look]; below the movie's own
    // bezel (its bottom edge is the inner frame of the satnav).
    if ( detectorMeters_ >= 0.0f && detectorMeters_ <= detectorRange_ ) {
        const float fill = 1.0f - detectorMeters_ / detectorRange_;
        const float y = y1 + 20.0f;
        renderer.drawRect( Rect2D( x0, y, mapRectW, 22.0f ), Rgba( 0, 0, 0, 150 ) );
        renderer.drawRect( Rect2D( x0 + 4.0f, y + 15.0f, ( mapRectW - 8.0f ) * fill, 4.0f ), Rgba( 255, 190, 40, 255 ) );
        char text[48];
        snprintf( text, sizeof( text ), "Detector  %d m", static_cast<int32_t>( detectorMeters_ ) );
        renderer.drawText( gpFlash->getTitleFont(), text, x0 + 8.0f, y + 1.0f, 13.0f, Rgba( 255, 210, 90 ) );
    }
}


void FreeRideHud::dumpGeneral( const char *pFilter ) {
    if ( general.isLoaded() ) {
        DumpFlashSubtree( general, pFilter, 6, 80 );
        DumpFlashFunctions( general );
    }
}

void FreeRideHud::setRisk( RiskDriving &risk ) {
    const bool bWasBanking = bFrimBanking_;
    frimLevel_ = risk.getLevel();
    frimProgress_ = risk.getProgress();
    frimThreshold_ = risk.getThreshold();
    frimLastPoints_ = risk.getLastPoints();
    frimMoney_ = risk.getMoneyForLevel();
    frimNextMoney_ = risk.getMoneyForNextLevel();
    frimTotal_ = risk.getTotal();
    frimPerfType_ = risk.getPerfType();
    bFrimBanking_ = risk.isBanking();
    if ( bFrimBanking_ && !bWasBanking ) {
        bFrimBankPending_ = true; // the level-up: play the bank animation once
    }
    bFrimNewPerf_ |= risk.takeNewPerf();
    bFrimHit_ |= risk.takeHit();
}

// The FRIM gauge (hud.bnk /HUD/FRG), driven with the retail protocol: the
// FRG_on / FRG_off labels toggle the whole gauge (FUN_00a766d0 [CODE]), the
// root variables carry the gauge state (FUN_00a75df0), a perf event jumps
// /HUD/FRG/FR to new_pts (FUN_00a74d90) and its icon to "on"
// (FUN_00a74810); a crash jumps it to hit (FUN_008c3682 area).
void FreeRideHud::updateFrim() {
    if ( !hud.isLoaded() ) {
        return;
    }
    if ( hud.findClip( "/HUD/FRG" ) == nullptr ) {
        if ( bFrimShown_ ) {
            bFrimShown_ = false;
        }
        bFrimAvailable_ = false;
        return;
    }
    bFrimAvailable_ = true;
    // The gauge is up while the chain runs (a level in progress, a bank
    // animation or a fresh perf badge); retail toggles it by race state
    // (FUN_00a766d0 called with the game state AND mask) [INF].
    const bool bActive = frimLevel_ > 0 || frimProgress_ > 0 || bFrimBanking_ || bFrimNewPerf_ || frimLastPoints_ > 0;
    static const bool bLogFrim = CmdLine::HasArg( "-logfrim" );
    if ( bLogFrim && !bFrimShown_ && bActive ) {
        // Label inventory of the FRG timeline (once, on the first activation).
        if ( const FlashTimeline *pTimeline = hud.findClip( "/HUD/FRG" )->pTimeline ) {
            for ( const auto &label : pTimeline->Labels ) {
                OTDU_LOG_INFO( "FreeRideHud: FRG label '%s' frame %u\n", label.first.c_str(), label.second );
            }
        }
    }
    if ( bActive != bFrimShown_ ) {
        bFrimShown_ = bActive;
        const bool bOk = hud.gotoLabel( "/HUD/FRG", bActive ? "FRG_on" : "FRG_off", true );
        // FR (the content clip) has its own label sequence, each section ends
        // in a Stop [DATA: hud.bnk sprite 909]: 'on' (f10) runs the slot
        // positioning script, 'start' (f27) slides the gauge and the perf
        // icons in (f27..f45 moves), new_pts (f46) / bank (f60) / hit (f71) /
        // end (f84) are the event choreography.
        if ( bActive ) {
            hud.gotoLabel( "/HUD/FRG/FR", "on", true );
            hud.gotoLabel( "/HUD/FRG/FR", "start", true );
        }
        if ( bLogFrim ) {
            OTDU_LOG_INFO( "FreeRideHud: FRG %s -> %d\n", bActive ? "FRG_on" : "FRG_off", bOk ? 1 : 0 );
        }
    }
    if ( bLogFrim && bFrimShown_ && ++frimProbeFrames_ % 60 == 1 ) {
        const FlashPlayer::Clip *pFrg = hud.findClip( "/HUD/FRG" );
        const FlashPlayer::Clip *pFr = hud.findClip( "/HUD/FRG/FR" );
        if ( pFrg != nullptr && pFr != nullptr ) {
            OTDU_LOG_INFO( "FreeRideHud: FRG frame %u/%u play %d | FR frame %u play %d at (%.0f, %.0f) visible %d\n",
                           pFrg->CurrentFrame, pFrg->pTimeline ? pFrg->pTimeline->FrameCount : 0, pFrg->bPlaying ? 1 : 0,
                           pFr->CurrentFrame, pFr->bPlaying ? 1 : 0, pFr->pInstance ? pFr->pInstance->Matrix.TX / 20.0f : -1.0f,
                           pFr->pInstance ? pFr->pInstance->Matrix.TY / 20.0f : -1.0f,
                           pFr->pInstance ? ( pFr->pInstance->bVisible ? 1 : 0 ) : -1 );
        }
    }
    if ( !bActive ) {
        return;
    }
    // The gauge state (FUN_00a764a0 [CODE]): PerfPts = the level progress,
    // PerfPtsTarget = the level threshold, PACurLevel = moneyTable[level]
    // (what banking now pays - the "$100" of the owner's screenshot),
    // PANextLevel = moneyTable[level+1], PATotal = the chain total.
    hud.setVariableNumber( "/:PerfPts", frimProgress_ );
    hud.setVariableNumber( "/:PerfPtsTarget", frimThreshold_ );
    hud.setVariableNumber( "/:LastPts", frimLastPoints_ );
    hud.setVariable( "/:PtsType", "normal" );
    // The badge (PS3 0x7c8d24): level + 1 up to 9, then 10.
    hud.setVariableNumber( "/:PALevel", frimLevel_ <= 9 ? frimLevel_ + 1 : frimLevel_ );
    hud.setVariableNumber( "/:PACurLevel", frimMoney_ );
    hud.setVariableNumber( "/:PANextLevel", frimNextMoney_ );
    hud.setVariableNumber( "/:PATotal", frimTotal_ );
    if ( bFrimBankPending_ ) {
        bFrimBankPending_ = false;
        // The level-up: the bank animation of fr_gge (227 frames; it asks the
        // game to pay with FSCommand frgp:bank) [CODE 0xa76aad].
        hud.gotoLabel( "/HUD/FRG/FR/fr_gge/bank", "on", true );
    } else if ( bFrimHit_ ) {
        bFrimHit_ = false;
        hud.gotoLabel( "/HUD/FRG/FR", "hit", true );
    } else if ( bFrimNewPerf_ ) {
        bFrimNewPerf_ = false;
        hud.gotoLabel( "/HUD/FRG/FR", "new_pts", true );
        if ( !frimPerfType_.empty() ) {
            hud.gotoLabel( "/HUD/FRG/FR/" + frimPerfType_, "on", true );
        }
    }
}

void FreeRideHud::playFrimBank() {
    if ( !hud.isLoaded() ) {
        return;
    }
    // The player confirmed the bank: the pay segment of fr_gge/bank (label
    // "bank", frame 160 [DATA: its timeline]).
    hud.gotoLabel( "/HUD/FRG/FR/fr_gge/bank", "bank", true );
}

void FreeRideHud::drawFuel( Renderer2D &renderer ) {
    // The car has no fuel needle of its own (its dashboard carries ND_SPD and
    // ND_RPM only) and the HUD movie has no fuel element (there is no `/:` global
    // for one), so the port draws a small bar. It sits **under the odometer box
    // of the gauge** (the placeholder gauge's box is 1040..1165 x 662..684, and
    // the GAUGES movie's own odometer lands in the same place), so the tank reads
    // as part of the instrument cluster instead of floating beside the zone
    // badge. Green, amber under a quarter, red under a tenth - and at 0 the
    // engine cuts and the Breakdown line shows.
    if ( fuelCapacity <= 0.0f || CmdLine::HasArg( "-nofuelhud" ) ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    // Centred on the odometer box (its centre is x 1102.5).
    const float x0 = 1030.0f, y0 = 686.0f, w = 145.0f, h = 26.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 0, 0, 0, 150 ) );
    const float fraction = Clamp( fuelLitres / fuelCapacity, 0.0f, 1.0f );
    const uint32_t fill = fraction <= 0.001f ? Rgba( 220, 40, 40 )
                                               : fraction < 0.1f ? Rgba( 235, 110, 30 )
                                                                 : fraction < 0.25f ? Rgba( 235, 200, 40 )
                                                                                    : Rgba( 90, 200, 90 );
    const float barX = x0 + 6.0f, barY = y0 + 9.0f, barW = 54.0f, barH = 9.0f;
    if ( fraction > 0.0f ) {
        renderer.drawRect( Rect2D( barX, barY, barW * fraction, barH ), fill );
    }
    renderer.drawRectOutline( Rect2D( barX, barY, barW, barH ), 1.0f, Rgba( 200, 200, 200 ) );
    char text[48];
    snprintf( text, sizeof( text ), "%s %.0f L", LocStr( 5821, "Fuel" ), fuelLitres );
    renderer.drawText( pFont, text, x0 + 66.0f, y0 + 2.0f, 12.0f, Rgba( 235, 235, 235 ) );
    // The range is 0 until the first burn sets the consumption: "--" then.
    char range[32] = "--";
    if ( fuelRangeKm > 0.0f ) {
        FormatDistance( range, sizeof( range ), fuelRangeKm * 1000.0f );
    }
    renderer.drawText( pFont, range, x0 + 66.0f, y0 + 14.0f, 11.0f, Rgba( 200, 210, 220 ) );
}

void FreeRideHud::drawZone( Renderer2D &renderer ) {
    const Font *pFont = gpFlash->getTitleFont();
    renderer.drawRect( Rect2D( 1124.0f, 52.0f, 54.0f, 54.0f ), Rgba( 40, 120, 200, 230 ) );
    renderer.drawRectOutline( Rect2D( 1124.0f, 52.0f, 54.0f, 54.0f ), 2.0f, Rgba( 230, 230, 230, 255 ) );
    std::string islandLabel = GetCruisingZones().getIsland().empty() ? "IBIZA" : GetCruisingZones().getIsland();
    if ( islandLabel == "hawai" ) {
        islandLabel = "hawaii"; // the folder name drops the last letter
    }
    for ( char &c : islandLabel ) {
        c = static_cast<char>( toupper( static_cast<unsigned char>( c ) ) );
    }
    renderer.drawText( pFont, islandLabel.c_str(), 1151.0f, 54.0f, 12.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
    char number[8];
    snprintf( number, sizeof( number ), "%d", zoneIndex );
    renderer.drawText( pFont, number, 1151.0f, 66.0f, 32.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
    renderer.drawRect( Rect2D( 1022.0f, 112.0f, 156.0f, 24.0f ), Rgba( 0, 0, 0, 140 ) );
    renderer.drawText( pFont, zoneName.c_str(), 1030.0f, 114.0f, 17.0f, Rgba( 255, 255, 255 ) );
}

void FreeRideHud::showCarSpecCard( const std::string &name, int32_t price, const CarSpec &spec ) {
    carSpecName_ = name;
    carSpecPrice_ = price;
    carSpec_ = spec;
    bCarSpecCard_ = true;
}

void FreeRideHud::drawCarSpecCard( Renderer2D &renderer ) {
    // The near-car spec card, bottom right (the compact shape of the retail
    // car screens: the name, the price and the three numbers). It is an
    // overlay of the drive HUD - hidden with the HUD itself (bShown). It is
    // drawn after the gauge cluster, in its own begin / end (the gauges draw
    // leaves their own canvas mapping active).
    if ( !bCarSpecCard_ || carSpecName_.empty() ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    renderer.begin( 1280.0f, 720.0f );
    const float x0 = 906.0f, y0 = 470.0f, w = 344.0f, h = 170.0f;
    renderer.drawRect( Rect2D( x0, y0, w, h ), Rgba( 12, 14, 18, 205 ) );
    renderer.drawRectOutline( Rect2D( x0, y0, w, h ), 2.0f, Rgba( 190, 190, 194, 220 ) );
    renderer.drawText( pFont, carSpecName_.c_str(), x0 + 18.0f, y0 + 12.0f, 23.0f, Rgba( 255, 255, 255 ) );
    // "$675,000" (the retail money shape); "--" when the DB has no price (the
    // police cars).
    char price[32];
    if ( carSpecPrice_ > 0 ) {
        std::string digits = std::to_string( carSpecPrice_ );
        std::string grouped;
        for ( size_t i = 0; i < digits.size(); i++ ) {
            if ( i > 0 && ( digits.size() - i ) % 3 == 0 ) {
                grouped += ',';
            }
            grouped += digits[i];
        }
        snprintf( price, sizeof( price ), "$%s", grouped.c_str() );
    } else {
        snprintf( price, sizeof( price ), "%s", "--" );
    }
    renderer.drawText( pFont, price, x0 + w - 18.0f, y0 + 14.0f, 20.0f, Rgba( 255, 210, 80 ), eTextAlign::TA_Right );
    struct Row {
        const char *pLabel;
        char value[24];
    };
    Row list[3];
    list[0] = { "Top speed", "" };
    list[1] = { "0-100 km/h", "" };
    list[2] = { "Power", "" };
    snprintf( list[0].value, sizeof( list[0].value ), "%d km/h", static_cast<int32_t>( carSpec_.MaxSpeed ) );
    // A 0 row is a DB gap (the traffic cars' _acceleration_0_100_kmh is often
    // 0): n/a, not 0.0 s.
    if ( carSpec_.Accel > 0.0f ) {
        snprintf( list[1].value, sizeof( list[1].value ), "%.1f s", carSpec_.Accel );
    } else {
        snprintf( list[1].value, sizeof( list[1].value ), "n/a" );
    }
    snprintf( list[2].value, sizeof( list[2].value ), "%d bhp", carSpec_.Power );
    float ry = y0 + 56.0f;
    for ( const Row &row : list ) {
        renderer.drawText( pFont, row.pLabel, x0 + 18.0f, ry, 17.0f, Rgba( 168, 170, 176 ) );
        renderer.drawText( pFont, row.value, x0 + w - 18.0f, ry, 19.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Right );
        ry += 34.0f;
    }
    renderer.end();
}

// ---------------------------------------------------------------------------
// GAUGES (docs: /tmp/claude-0/spec_freeride.md 4b)

float FreeRideHud::NeedleTable::map( float value ) const {
    // FUN_00968fe0: first key i with value < key[i] (or the last), clamp to
    // [key[i-1], key[i]], linear angle.
    const size_t n = Min( Values.size(), Angles.size() );
    if ( n < 2 ) {
        return n == 1 ? Angles[0] : 0.0f;
    }
    size_t segment = n - 1;
    for ( size_t i = 1; i < n; i++ ) {
        if ( value < Values[i] || i == n - 1 ) {
            segment = i;
            break;
        }
    }
    const float lo = Values[segment - 1], hi = Values[segment];
    float v = Min( value, hi );
    v = Max( v, lo );
    const float t = hi != lo ? ( v - lo ) / ( hi - lo ) : 1.0f;
    return ( 1.0f - t ) * Angles[segment - 1] + t * Angles[segment];
}

bool FreeRideHud::loadGaugeBank( const std::string &hudFileName ) {
    // FUN_0096fe90: "%sFrontEnd/%s/Gauges/%s.bnk" (HiRes, _hud_file_name).
    const std::string path = gpFile->getBankFolder() + "frontend/hires/gauges/" + ToLower( hudFileName ) + ".bnk";
    if ( hudFileName.empty() || !gaugeBank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "FreeRideHud: gauge bank '%s' not found\n", path.c_str() );
        return false;
    }
    TextureCache &textures = gpRender->getTextureCache();
    textures.loadBankTextures( gaugeBank, gaugeBank.getPath().c_str() );
    // Exterior view: ext_rpm dial, needle.2db [INF: texture by file name;
    // the FUN_0096fa50 arguments are not recovered].
    pDialTexture = textures.find( gaugeBank.getPath().c_str(), "ext_rpm" );
    pNeedleTexture = textures.find( gaugeBank.getPath().c_str(), "needle" );
    // hud.ini [HUD]: Style "<label>"; AddHud "<name>" v0 v1 a0 a1 [v a]...
    // (FUN_0096f720 needs more than 4 tokens; FUN_0096f670: N = (tokens -
    // 1) / 2 points: (t1, t3), (t5, t6), (t7, t8), (t9, t10), last = (t2, t4)).
    const BankEntry *pIni = gaugeBank.findEntry( "hud.ini" );
    if ( pIni != nullptr ) {
        const std::string text( reinterpret_cast<const char *>( pIni->pData ), pIni->Size );
        size_t start = 0;
        while ( start < text.size() ) {
            size_t end = text.find( '\n', start );
            if ( end == std::string::npos ) {
                end = text.size();
            }
            std::string line = text.substr( start, end - start );
            start = end + 1;
            const size_t comment = line.find( "//" );
            if ( comment != std::string::npos ) {
                line.resize( comment );
            }
            std::vector<std::string> tokens;
            size_t i = 0;
            while ( i < line.size() ) {
                while ( i < line.size() && isspace( static_cast<unsigned char>( line[i] ) ) ) {
                    i++;
                }
                if ( i >= line.size() ) {
                    break;
                }
                size_t j = i;
                if ( line[i] == '"' ) {
                    j = line.find( '"', i + 1 );
                    j = j == std::string::npos ? line.size() : j;
                    tokens.push_back( line.substr( i + 1, j - i - 1 ) );
                    i = j + 1;
                    continue;
                }
                while ( j < line.size() && !isspace( static_cast<unsigned char>( line[j] ) ) ) {
                    j++;
                }
                tokens.push_back( line.substr( i, j - i ) );
                i = j;
            }
            if ( tokens.size() >= 2 && StringCmpI( tokens[0].c_str(), "Style" ) == 0 ) {
                gaugeStyle = tokens[1];
            } else if ( tokens.size() > 5 && StringCmpI( tokens[0].c_str(), "AddHud" ) == 0 ) {
                // tokens[1] = name: FUN_0096f720 counts the name as token 0.
                std::vector<float> t;
                for ( size_t k = 1; k < tokens.size(); k++ ) {
                    t.push_back( static_cast<float>( atof( tokens[k].c_str() ) ) ); // t[0] = name (0)
                }
                const int32_t count = static_cast<int32_t>( tokens.size() - 1 );
                const int32_t points = ( count - 1 ) / 2;
                auto token = [&]( int32_t k ) { return k < static_cast<int32_t>( t.size() ) ? t[k] : 0.0f; };
                // val_min val_max ori_min ori_max, then the intermediate
                // (value, angle) pairs. The data is not monotonic: California's
                // interior_kmh carries a (320, 282) point outside its 0..300
                // range (the mph 200 mark of the dual scale). Keep the points
                // inside [val_min, val_max] and sort by value, so the map stays
                // a real piecewise line [DATA + INF].
                const float valMin = token( 1 );
                const float valMax = token( 2 );
                const float lo = Min( valMin, valMax );
                const float hi = Max( valMin, valMax );
                std::vector<std::pair<float, float>> pointsList;
                pointsList.emplace_back( valMin, token( 3 ) );
                for ( int32_t p = 1; p < points - 1 && p < 4; p++ ) {
                    const float value = token( 2 * p + 3 );
                    if ( value >= lo && value <= hi ) {
                        pointsList.emplace_back( value, token( 2 * p + 4 ) );
                    }
                }
                pointsList.emplace_back( valMax, token( 4 ) );
                std::sort( pointsList.begin(), pointsList.end(),
                           []( const std::pair<float, float> &a, const std::pair<float, float> &b ) {
                               return a.first < b.first;
                           } );
                NeedleTable table;
                for ( const auto &point : pointsList ) {
                    if ( !table.Values.empty() && point.first <= table.Values.back() ) {
                        continue; // duplicate / non increasing value
                    }
                    table.Values.push_back( point.first );
                    table.Angles.push_back( point.second );
                }
                needleTables[tokens[1]] = table;
            }
        }
    }
    // FUN_0096f8c0: compteur_ext / compteur_int go to the Style label.
    if ( !gaugeStyle.empty() ) {
        gauges.gotoLabel( "/HUD/compteur_ext", gaugeStyle, false );
        gauges.gotoLabel( "/HUD/compteur_int", gaugeStyle, false );
    }
    OTDU_LOG_INFO( "FreeRideHud: gauge bank '%s': style '%s', %zu needle tables, dial %s, needle %s\n", path.c_str(),
                   gaugeStyle.c_str(), needleTables.size(), pDialTexture != nullptr ? "ok" : "missing",
                   pNeedleTexture != nullptr ? "ok" : "missing" );
    return true;
}

void FreeRideHud::updateGauges() {
    // FUN_00966ed0: interior views 22 / 42 -> compteur_int, cockpit views
    // 23 / 43 -> speed_cokpit(_sh), otherwise compteur_ext.
    const bool bInterior = state.CameraMode == 0x16 || state.CameraMode == 0x2a;
    const bool bCockpit = state.CameraMode == 0x17 || state.CameraMode == 0x2b;
    gauges.setClipVisible( "/HUD/speed_cokpit", bCockpit );
    gauges.setClipVisible( "/HUD/speed_cokpit_sh", bCockpit );
    gauges.setClipVisible( "/HUD/compteur_ext", !bCockpit && !bInterior );
    gauges.setClipVisible( "/HUD/compteur_int", !bCockpit && bInterior );
    // The aiguille is drawn here with the bank needle (hidden in the movie).
    gauges.setClipVisible( "/HUD/compteur_ext/aiguille", false );
    // FUN_00964ef0 snapshot: gear char (car+0x250: 0 'R', 1 'N', 2.. '1'..),
    // speed x 3.6 (int), rpm = rad/s x 30 / pi.
    char gear[4] = { 'N', 0, 0, 0 };
    if ( state.Gear < 0 ) {
        gear[0] = 'R';
    } else if ( state.Gear > 0 && state.Gear <= 9 ) {
        gear[0] = static_cast<char>( '0' + state.Gear );
    }
    gauges.setVariable( "/:Gear", gear );
    // FUN_00968e20: Speed_u / _d / _c = digits of the speed clamped 0..999, in
    // the unit system of the game (/ :SpeedUnit, the unit table of
    // FUN_00965810: v x 3.6 km/h or v x 2.2374 mph - docs/re/freeride_spec.md
    // 4b). The port picks it from GameOptions::Units (game/units.h).
    gauges.setVariable( "/:SpeedUnit", SpeedUnitLabel() );
    const float displaySpeed = SpeedForDisplay( state.SpeedKmh );
    // The 3D cockpit needles (the car's interior model): the same interior_*
    // tables as the GAUGES movie. The speed one follows the player's unit
    // system (the table and the digits must agree); rpm is unit independent.
    const auto interiorSpeed = needleTables.find( IsImperialUnits() ? "interior_mph" : "interior_kmh" );
    interiorSpeedNeedleAngle = interiorSpeed != needleTables.end() ? interiorSpeed->second.map( displaySpeed ) : 0.0f;
    const auto interiorRpm = needleTables.find( "interior_rpm" );
    interiorRpmNeedleAngle = interiorRpm != needleTables.end() ? interiorRpm->second.map( state.Rpm ) : 0.0f;
    int32_t speed = Clamp( static_cast<int32_t>( std::abs( displaySpeed ) ), 0, 999 );
    for ( const char *pName : { "/:Speed_u", "/:Speed_d", "/:Speed_c" } ) {
        gauges.setVariable( pName, std::to_string( speed % 10 ) );
        speed /= 10;
    }
    // FUN_00968ef0: /:Km 0..99999999 (8 digits, zero padded), /:Centaine
    // 0..99 (1 digit) [INF: km and hundreds of metres of the odometer]. The
    // odometer itself is kept in km; the display unit converts it (miles in
    // imperial, the tenth digit is then a tenth of a mile).
    const float odometer = Clamp( KmForDisplay( state.OdometerKm ), 0.0f, 99999999.0f );
    char text[32];
    snprintf( text, sizeof( text ), "%08d", static_cast<int32_t>( odometer ) );
    gauges.setVariable( "/:Km", text );
    snprintf( text, sizeof( text ), "%d", Clamp( static_cast<int32_t>( ( odometer - std::floor( odometer ) ) * 10.0f ), 0, 9 ) );
    gauges.setVariable( "/:Centaine", text );
    // /:Rpm (int) in HUD and GAUGES.
    gauges.setVariableNumber( "/:Rpm", static_cast<int32_t>( state.Rpm ) );
    // Exterior needle: AddHud "exterior_rpm" (rpm -> degrees).
    auto it = needleTables.find( "exterior_rpm" );
    needleAngle = it != needleTables.end() ? it->second.map( state.Rpm ) : 0.0f;
}

bool FreeRideHud::clipMatrix( FlashPlayer &player, const std::string &path, FlashMatrix &outMatrix,
                             uint16_t &outCharacter ) {
    FlashPlayer::Clip *pClip = player.findClip( path );
    if ( pClip == nullptr || pClip->pInstance == nullptr ) {
        return false;
    }
    outCharacter = pClip->pInstance->CharacterId;
    FlashMatrix matrix;
    for ( FlashPlayer::Clip *pCurrent = pClip; pCurrent != nullptr && pCurrent->pInstance != nullptr;
          pCurrent = pCurrent->pParent ) {
        matrix = pCurrent->pInstance->Matrix * matrix;
    }
    outMatrix = matrix;
    return true;
}

bool FreeRideHud::clipMatrix( const std::string &path, FlashMatrix &outMatrix, uint16_t &outCharacter ) {
    return clipMatrix( gauges, path, outMatrix, outCharacter );
}

void FreeRideHud::beginStageRect( Renderer2D &renderer, const FlashPlayer &player ) const {
    // Same destination rect as FlashPlayer::beginStage (FUN_00d7faa0).
    const VMFFile &vmf = player.getMovie()->getVMF();
    float rectX0, rectY0, rectW, rectH;
    FlashStageRect( GetEffectiveScreenRatio(), vmf.getStageWidth(), vmf.getStageHeight(),
                    renderer.getTargetWidth(), renderer.getTargetHeight(), rectX0, rectY0, rectW, rectH );
    renderer.beginRect( vmf.getStageWidth(), vmf.getStageHeight(), rectX0, rectY0, rectW, rectH );
}

void FreeRideHud::beginGaugeStage( Renderer2D &renderer ) const { beginStageRect( renderer, gauges ); }

bool FreeRideHud::clipBox( FlashPlayer &player, const std::string &path, float &outX, float &outY, float &outWidth,
                           float &outHeight ) {
    // The union of the shapes of the sprite's current frame, in the stage's
    // units (twips / 20, the same mapping the gauge dial uses).
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    std::function<void( FlashPlayer::Clip &, const FlashMatrix & )> visit =
        [&]( FlashPlayer::Clip &clip, const FlashMatrix &matrix ) {
            for ( const auto &entry : clip.DisplayList ) {
                const FlashPlayer::Instance &instance = *entry.second;
                const FlashMatrix child = matrix * instance.Matrix;
                if ( instance.pClip != nullptr ) {
                    visit( *instance.pClip, child );
                    continue;
                }
                const FlashShape *pShape = player.getDefinitions().getShape( instance.CharacterId );
                if ( pShape == nullptr ) {
                    continue;
                }
                for ( const FlashShapeMesh &mesh : pShape->Meshes ) {
                    for ( uint32_t index : mesh.Indices ) {
                        float x = 0.0f, y = 0.0f;
                        child.transform( mesh.Positions[index * 2], mesh.Positions[index * 2 + 1], x, y );
                        minX = Min( minX, x );
                        minY = Min( minY, y );
                        maxX = Max( maxX, x );
                        maxY = Max( maxY, y );
                    }
                }
            }
        };
    FlashPlayer::Clip *pClip = player.findClip( path );
    FlashMatrix matrix;
    uint16_t character = 0;
    if ( pClip == nullptr || !clipMatrix( player, path, matrix, character ) ) {
        return false;
    }
    visit( *pClip, matrix );
    if ( maxX <= minX || maxY <= minY ) {
        return false;
    }
    constexpr float kTwipsToStage = 1.0f / 20.0f;
    outX = minX * kTwipsToStage;
    outY = minY * kTwipsToStage;
    outWidth = ( maxX - minX ) * kTwipsToStage;
    outHeight = ( maxY - minY ) * kTwipsToStage;
    return true;
}

bool FreeRideHud::radioLogoBox( float &outX, float &outY, float &outWidth, float &outHeight ) {
    return clipBox( hud, "/HUD/radio/radio_logo", outX, outY, outWidth, outHeight );
}

bool FreeRideHud::computeMapRect() {
    // The map surface: /HUD/minimap/mmap_lure is one solid quad — the
    // placeholder the retail engine renders the live map into (the port draws
    // its native map there instead, under the movie, and hides the quad). Its
    // box comes out of the movie, then maps stage -> canvas px through the
    // window (the UI layout mapping, like beginStage draws with).
    float boxX = 0.0f, boxY = 0.0f, boxW = 0.0f, boxH = 0.0f;
    if ( !clipBox( hud, "/HUD/minimap/mmap_lure", boxX, boxY, boxW, boxH ) ) {
        return false;
    }
    const FlashMovie *pMovie = hud.getMovie();
    if ( pMovie == nullptr ) {
        return false;
    }
    const float stageW = pMovie->getVMF().getStageWidth(), stageH = pMovie->getVMF().getStageHeight();
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    FlashStageToCanvas( boxX, boxY, stageW, stageH, x0, y0 );
    FlashStageToCanvas( boxX + boxW, boxY + boxH, stageW, stageH, x1, y1 );
    mapRectX = x0;
    mapRectY = y0;
    mapRectW = x1 - x0;
    mapRectH = y1 - y0;
    return true;
}

void FreeRideHud::setRadioIcon( const std::string &path ) {
    if ( path == radioIconPath_ ) {
        return;
    }
    releaseRadioIcon();
    radioIconPath_ = path;
    if ( path.empty() || gpRender == nullptr ) {
        return;
    }
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0;
    // The station art path is UTF-8 (RadioPlayer keeps every custom path so).
    if ( !LoadImageFileRGBA( PathFromUtf8( path ), pixels, width, height ) || width == 0 || height == 0 ) {
        OTDU_LOG_WARN( "FreeRideHud: station art '%s' not loaded\n", path.c_str() );
        return;
    }
    // createFromRGBA does not register the texture with an owner: the HUD owns
    // it (releaseRadioIcon destroys it).
    pRadioIcon = gpRender->getTextureCache().createFromRGBA( width, height, pixels.data(), path.c_str() );
    radioIconAspect_ = static_cast<float>( width ) / static_cast<float>( height );
    OTDU_LOG_INFO( "FreeRideHud: station art '%s' (%ux%u)\n", path.c_str(), width, height );
}

void FreeRideHud::releaseRadioIcon() {
    if ( pRadioIcon != nullptr && gpRender != nullptr ) {
        gpRender->getDevice().destroyTexture( pRadioIcon );
    }
    pRadioIcon = nullptr;
    radioIconPath_.clear();
    radioIconAspect_ = 1.0f;
}

void FreeRideHud::drawRadioIcon( Renderer2D &renderer ) {
    float boxX = 0.0f, boxY = 0.0f, boxWidth = 0.0f, boxHeight = 0.0f;
    if ( !radioLogoBox( boxX, boxY, boxWidth, boxHeight ) ) {
        static bool bLogged = false;
        if ( !bLogged ) {
            bLogged = true;
            OTDU_LOG_WARN( "FreeRideHud: no radio logo box, the station art is not drawn\n" );
        }
        return;
    }
    // Fitted in the box the movie's logo sprite occupies, aspect kept.
    float width = boxWidth, height = boxHeight;
    if ( radioIconAspect_ > 0.0f && boxHeight > 0.0f ) {
        if ( radioIconAspect_ > boxWidth / boxHeight ) {
            height = boxWidth / radioIconAspect_;
        } else {
            width = boxHeight * radioIconAspect_;
        }
    }
    const float x = boxX + ( boxWidth - width ) * 0.5f;
    const float y = boxY + ( boxHeight - height ) * 0.5f;
    if ( CmdLine::HasArg( "-logradio" ) ) {
        OTDU_LOG_INFO( "FreeRideHud: radio logo box %.1f,%.1f %.1fx%.1f -> art %.1f,%.1f %.1fx%.1f\n", boxX, boxY,
                       boxWidth, boxHeight, x, y, width, height );
    }
    beginStageRect( renderer, hud );
    renderer.drawQuad( pRadioIcon, Rect2D( x, y, width, height ), Rect2D( 0.0f, 0.0f, 1.0f, 1.0f ), 0xffffffffu );
    renderer.end();
}

void FreeRideHud::drawGaugeTextures( Renderer2D &renderer, bool bDial ) {
    if ( !gauges.isLoaded() || ( bDial ? pDialTexture : pNeedleTexture ) == nullptr ) {
        return;
    }
    FlashPlayer::Clip *pExterior = gauges.findClip( "/HUD/compteur_ext" );
    if ( pExterior == nullptr || pExterior->pInstance == nullptr || !pExterior->pInstance->bVisible ) {
        return;
    }
    FlashMatrix matrix;
    uint16_t character = 0;
    if ( !clipMatrix( bDial ? "/HUD/compteur_ext/c_ext" : "/HUD/compteur_ext/aiguille", matrix, character ) ) {
        return;
    }
    FlashMatrix stageToCanvas;
    stageToCanvas.A = stageToCanvas.D = 1.0f / 20.0f;
    if ( !bDial ) {
        // _rotation of the aiguille (FUN_00968d60, vfunc +0x90) [INF:
        // degrees, clockwise, the placement scale kept].
        FlashPlayer::Clip *pNeedle = gauges.findClip( "/HUD/compteur_ext/aiguille" );
        const FlashMatrix &base = pNeedle->pInstance->Matrix;
        const float sx = std::sqrt( base.A * base.A + base.B * base.B );
        const float sy = std::sqrt( base.C * base.C + base.D * base.D );
        const float angle = needleAngle * kDegToRad;
        FlashMatrix rotated = base;
        rotated.A = sx * std::cos( angle );
        rotated.B = sx * std::sin( angle );
        rotated.C = -sy * std::sin( angle );
        rotated.D = sy * std::cos( angle );
        FlashMatrix parent;
        if ( !clipMatrix( "/HUD/compteur_ext", parent, character ) ) {
            return;
        }
        matrix = parent * rotated;
        character = pNeedle->pInstance->CharacterId;
    }
    // Shapes of the sprite (frame 0, nested sprites included).
    std::vector<Vertex2D> vertices;
    float boundsMin[2] = { 1e9f, 1e9f }, boundsMax[2] = { -1e9f, -1e9f };
    std::function<void( uint16_t, const FlashMatrix & )> visit = [&]( uint16_t id, const FlashMatrix &m ) {
        if ( const FlashShape *pShape = gaugeDefinitions.getShape( id ) ) {
            const FlashMatrix toCanvas = stageToCanvas * m;
            for ( const FlashShapeMesh &mesh : pShape->Meshes ) {
                const FlashFill &fill = pShape->Fills[mesh.FillIndex];
                for ( uint32_t index : mesh.Indices ) {
                    const float x = mesh.Positions[index * 2], y = mesh.Positions[index * 2 + 1];
                    Vertex2D vertex;
                    toCanvas.transform( x, y, vertex.X, vertex.Y );
                    boundsMin[0] = Min( boundsMin[0], vertex.X );
                    boundsMin[1] = Min( boundsMin[1], vertex.Y );
                    boundsMax[0] = Max( boundsMax[0], vertex.X );
                    boundsMax[1] = Max( boundsMax[1], vertex.Y );
                    float fx = 0.0f, fy = 0.0f;
                    fill.InverseMatrix.transform( x, y, fx, fy );
                    // The bank texture replaces the whole bitmap.
                    vertex.U = fx / Max( fill.BitmapWidth, 1.0f );
                    vertex.V = fy / Max( fill.BitmapHeight, 1.0f );
                    vertex.Color = 0xffffffffu;
                    if ( fill.Type == eFlashFillType::FF_Bitmap ) {
                        vertices.push_back( vertex );
                    }
                }
            }
            return;
        }
        if ( const FlashTimeline *pSprite = gaugeDefinitions.getSprite( id ) ) {
            if ( pSprite->Frames.empty() ) {
                return;
            }
            for ( const FlashControlTag &tag : pSprite->Frames[0] ) {
                if ( tag.Type == eFlashControlType::CT_Place && tag.Place.bHasCharacter ) {
                    visit( tag.Place.CharacterId, m * tag.Place.Matrix );
                }
            }
        }
    };
    visit( character, matrix );
    beginGaugeStage( renderer );
    if ( bDial ) {
        // FUN_009661b0: the dial texture on the c_ext rectangle, UV (0, 0)
        // - (1, 1) above 640 pixels wide (DAT_00f90bbc) [CODE].
        if ( boundsMax[0] > boundsMin[0] ) {
            renderer.drawQuad( pDialTexture, Rect2D( boundsMin[0], boundsMin[1], boundsMax[0] - boundsMin[0], boundsMax[1] - boundsMin[1] ),
                               Rect2D( 0.0f, 0.0f, 1.0f, 1.0f ), 0xffffffffu );
        }
    } else if ( !vertices.empty() ) {
        renderer.drawTriangles( pNeedleTexture, vertices.data(), static_cast<uint32_t>( vertices.size() ) );
    }
    renderer.end();
}