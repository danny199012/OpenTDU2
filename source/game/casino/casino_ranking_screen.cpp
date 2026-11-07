#include "shared.h"
#include "casino_ranking_screen.h"

#include "core/xmb.h"
#include "flash/flash_localization.h"
#include "flash/flash_movie.h"
#include "flash/gs_flash.h"
#include "game/avatarchoice/intro_cinematic.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"

namespace {
constexpr const char *kRankingArray = "ArrayOf_Entity__Actor__CasinoFlashScreen__CasinoFlashScreenRanking";
// The tube cutscenes of icashal.bnk [DATA: no camera, the screen and the tube
// tracks only - closed: screen y -1.97 / tube -3.62 (under the floor), open:
// screen 0.22 / tube -0.95; OPEN / CLOSE move between them in 1 s].
constexpr const char *kStandClosed = "C_RANKINGS_STAND_CLOSED";
constexpr const char *kStandOpened = "C_RANKINGS_STAND_OPENED";
constexpr const char *kOpen = "C_RANKINGS_OPEN";
constexpr const char *kClose = "C_RANKINGS_CLOSE";
// The movie's render target: Cas_Ranking's stage is 4:3 like the frontend
// movies; the screen face samples the whole target [INF: the size].
constexpr uint32_t kTargetWidth = 1024;
constexpr uint32_t kTargetHeight = 768;
} // namespace

bool CasinoRankingScreen::attach( IntroCinematic &scene, const XmbDocument &entities ) {
    detach();
    if ( CmdLine::HasArg( "-nocasinorankings" ) ) {
        return false;
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( uint32_t i = 0; i < entityExport.getArraySize( kRankingArray ); i++ ) {
        XmbValue row = entityExport.getArrayElement( kRankingArray, i );
        if ( row["IsTimeAttack"].asInt() == 0 ) {
            continue;
        }
        XmbValue pos = row["orient_pos_scale"]["pos"];
        screenPosition = Vector3f( pos["X"].asFloat(), pos["Y"].asFloat(), pos["Z"].asFloat() );
        screenHash = row["__Hc"].asUInt64();
        cameraHash = row["RankingCamera"].asUInt64();
        zoneHash = row["SelectableZone"].asUInt64();
        // SelectableDist (6 m) [DATA]; that it is also the distance that raises
        // the tube is INF.
        openRadius = Max( 2.0f, row["SelectableDist"].asFloat( 6.0f ) );
        bAttached = true;
        OTDU_LOG_INFO( "CasinoRankings: '%s' at (%.2f, %.2f, %.2f), movie '%s', raised within %.1f m\n",
                       row["__Name"].asString().c_str(), screenPosition.x, screenPosition.y, screenPosition.z,
                       row["Movie"].asString().c_str(), openRadius );
        break;
    }
    if ( !bAttached ) {
        return false;
    }
    state = eState::Closed;
    play( scene, kStandClosed, true );
    if ( gpFlash != nullptr && !CmdLine::HasArg( "-norankingmovie" ) ) {
        if ( FlashMovie *pMovie = gpFlash->getMovie( "cas_ranking" ) ) {
            if ( CmdLine::HasArg( "-logcasinorankings" ) ) {
                for ( const std::string &label : pMovie->getVMF().getFrameLabels() ) {
                    OTDU_LOG_INFO( "CasinoRankings: label '%s'\n", label.c_str() );
                }
                for ( const auto &exported : pMovie->getVMF().getExports() ) {
                    OTDU_LOG_INFO( "CasinoRankings: export '%s'\n", exported.first.c_str() );
                }
            }
            bMovieLoaded = movie.load( pMovie );
            if ( bMovieLoaded ) {
                movie.setFSCommandCallback( []( const std::string &command, const std::string &args ) {
                    if ( CmdLine::HasArg( "-logcasinorankings" ) ) {
                        OTDU_LOG_INFO( "CasinoRankings: FSCommand '%s' '%s'\n", command.c_str(), args.c_str() );
                    }
                } );
                movie.tick( 1.0f / 30.0f );
                ApplyFlashLocalization( movie );
                // The screen's page: the movie's 'time_attack_rank' frame
                // (labels default / slot_rank / roul_rank / cash_rank /
                // sitgo_rank / tournament_rank / casino_rank /
                // time_attack_rank / end_menu) [DATA]; IsTimeAttack 1 picks it.
                const char *pPage = CmdLine::GetArgValue( "-rankingspage" );
                const bool bPage = movie.gotoLabel( "", pPage != nullptr ? pPage : "time_attack_rank", true );
                movie.tick( 1.0f / 30.0f );
                if ( CmdLine::HasArg( "-logcasinorankings" ) ) {
                    OTDU_LOG_INFO( "CasinoRankings: page '%s' %s\n", pPage != nullptr ? pPage : "time_attack_rank",
                                   bPage ? "shown" : "not found" );
                }
            }
        }
    }
    return true;
}

void CasinoRankingScreen::detach() {
    if ( bMovieLoaded ) {
        movie.unload();
        if ( gpFlash != nullptr ) {
            gpFlash->releaseMovie( "cas_ranking" );
        }
    }
    bMovieLoaded = false;
    if ( pTarget != nullptr && gpRender != nullptr ) {
        gpRender->getDevice().destroyRenderTarget( pTarget );
    }
    pTarget = nullptr;
    bAttached = false;
    playing.clear();
    state = eState::Closed;
    movieFrames = 0;
}

void CasinoRankingScreen::play( IntroCinematic &scene, const char *pName, bool bLoop ) {
    if ( !playing.empty() ) {
        scene.stopBackground( playing );
    }
    playing = pName;
    if ( !scene.playBackground( pName, bLoop ) ) {
        playing.clear();
    }
    if ( CmdLine::HasArg( "-logcasinorankings" ) ) {
        OTDU_LOG_INFO( "CasinoRankings: %s\n", pName );
    }
}

void CasinoRankingScreen::update( IntroCinematic &scene, const Vector3f &walkPosition, bool bWalking,
                                  float deltaTime ) {
    if ( !bAttached ) {
        return;
    }
    const float dx = walkPosition.x - screenPosition.x;
    const float dz = walkPosition.z - screenPosition.z;
    const float distance = std::sqrt( dx * dx + dz * dz );
    static const bool bForceOpen = CmdLine::HasArg( "-rankingsopen" );
    const bool bNear = bForceOpen || ( bWalking && distance < openRadius );
    const bool bFar = !bForceOpen && ( !bWalking || distance > openRadius + 1.5f );
    switch ( state ) {
    case eState::Closed:
        if ( bNear ) {
            play( scene, kOpen, false );
            state = eState::Opening;
        }
        break;
    case eState::Opening:
        if ( !scene.isBackgroundPlaying( kOpen ) ) {
            play( scene, kStandOpened, true );
            state = eState::Open;
        }
        break;
    case eState::Open:
        if ( bFar ) {
            play( scene, kClose, false );
            state = eState::Closing;
        }
        break;
    case eState::Closing:
        if ( !scene.isBackgroundPlaying( kClose ) ) {
            play( scene, kStandClosed, true );
            state = eState::Closed;
        }
        break;
    }
    if ( bMovieLoaded && isRaised() ) {
        movie.tick( deltaTime );
    }
}

void CasinoRankingScreen::drawOffscreen( IntroCinematic &scene ) {
    if ( !bAttached || !bMovieLoaded || gpRender == nullptr || !isRaised() ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    if ( pTarget == nullptr ) {
        pTarget = device.createRenderTarget( kTargetWidth, kTargetHeight, VK_FORMAT_UNDEFINED, true, "casino_rankings" );
        if ( pTarget == nullptr ) {
            return;
        }
    }
    // The movie barely moves: redrawn every other frame.
    if ( ( movieFrames++ % 2 ) == 0 ) {
        Renderer2D &renderer = gpRender->get2DRenderer();
        static const bool bRedClear = CmdLine::HasArg( "-rankingsclear" ); // A/B: the binding vs the movie
        device.beginRenderTarget( pTarget, true,
                                  bRedClear ? ColorRGBA( 1.0f, 0.0f, 0.0f, 1.0f ) : ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
        renderer.setTargetSize( kTargetWidth, kTargetHeight );
        movie.draw( renderer );
        renderer.end();
        renderer.setTargetSize( 0, 0 );
        device.endRenderTarget();
    }
    // The screen face (TD_NEONDECAL 'flashscreen_ran'): slot 1 the colour,
    // slot 0 the glow (empty -> white), self-lit - as the slot screens.
    scene.setPropTexture( screenHash, "flashscreen_ran", 1, &pTarget->Color, stamp );
    scene.setPropTexture( screenHash, "flashscreen_ran", 0, nullptr, stamp ^ 0x6c0u );
    scene.setPropEmissive( screenHash, 1.0f );
}
