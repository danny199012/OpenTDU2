#include "shared.h"
#include "school_end_scene.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"
#include <cstdlib>

namespace {
std::string FileStem( const std::string &path ) {
    const size_t slash = path.find_last_of( "\\/" );
    std::string file = slash != std::string::npos ? path.substr( slash + 1 ) : path;
    const size_t dot = file.find_last_of( '.' );
    return dot != std::string::npos ? file.substr( 0, dot ) : file;
}

Quaternionf Conjugate( const Quaternionf &q ) { return Quaternionf( -q.x, -q.y, -q.z, q.w ); }

// Rotation part of a rigid matrix -> quaternion (the same decomposition as fade_freeride.cpp / gm_freeride.cpp).
Quaternionf RotationOf( const Matrix4x4f &m ) {
    const float m00 = m.m[0][0], m11 = m.m[1][1], m22 = m.m[2][2];
    const float trace = m00 + m11 + m22;
    Quaternionf q;
    if ( trace > 0.0f ) {
        const float s = std::sqrt( trace + 1.0f ) * 2.0f;
        q = Quaternionf( ( m.m[1][2] - m.m[2][1] ) / s, ( m.m[2][0] - m.m[0][2] ) / s, ( m.m[0][1] - m.m[1][0] ) / s, 0.25f * s );
    } else if ( m00 > m11 && m00 > m22 ) {
        const float s = std::sqrt( 1.0f + m00 - m11 - m22 ) * 2.0f;
        q = Quaternionf( 0.25f * s, ( m.m[1][0] + m.m[0][1] ) / s, ( m.m[2][0] + m.m[1][2] ) / s, ( m.m[1][2] - m.m[2][1] ) / s );
    } else if ( m11 > m22 ) {
        const float s = std::sqrt( 1.0f + m11 - m00 - m22 ) * 2.0f;
        q = Quaternionf( ( m.m[1][0] + m.m[0][1] ) / s, 0.25f * s, ( m.m[2][1] + m.m[1][2] ) / s, ( m.m[2][0] - m.m[0][2] ) / s );
    } else {
        const float s = std::sqrt( 1.0f + m22 - m00 - m11 ) * 2.0f;
        q = Quaternionf( ( m.m[2][0] + m.m[0][2] ) / s, ( m.m[2][1] + m.m[1][2] ) / s, 0.25f * s, ( m.m[0][1] - m.m[1][0] ) / s );
    }
    return q;
}
} // namespace

bool SchoolEndScene::loadPlay( const std::string &name, Play &out ) {
    const BankEntry *pCin = bank.findEntry( name.c_str(), "cin" );
    if ( pCin == nullptr || !out.Cin.parse( pCin->pData, pCin->Size ) ) {
        return false;
    }
    for ( int32_t reference : out.Cin.getSceneReferences() ) {
        CinematicScene scene;
        if ( reference >= 0 && reference < static_cast<int32_t>( out.Cin.getReferences().size() ) ) {
            const BankEntry *pScene = bank.findEntry( FileStem( out.Cin.getReferences()[reference].Text ).c_str(), "sce" );
            if ( pScene == nullptr || !scene.parseScene( pScene->pData, pScene->Size ) ) {
                OTDU_LOG_WARN( "SchoolEndScene: scene '%s' of '%s' %s\n", FileStem( out.Cin.getReferences()[reference].Text ).c_str(),
                               name.c_str(), pScene == nullptr ? "is not in the bank" : "did not parse" );
                return false;
            }
        }
        if ( CmdLine::HasArg( "-logendscene" ) ) {
            for ( const CinematicScene::Object &object : scene.getObjects() ) {
                const CinematicScene::Track *pPos = object.findTrack( CinematicScene::kTrackPosition );
                OTDU_LOG_INFO( "EndScene %s: %s %s hash %08x parent %s%08x\n", name.c_str(), object.isCamera() ? "camera" : "actor",
                               object.Name.c_str(), object.NameHash, pPos != nullptr && pPos->bParented ? "" : "(none) ",
                               pPos != nullptr ? pPos->Parent : 0u );
                if ( pPos != nullptr && !pPos->Values.empty() && object.isCamera() ) {
                    const Vector3f v = object.samplePosition( pPos->Times[0] );
                    const CinematicScene::Track *pRot = object.findTrack( CinematicScene::kTrackRotation );
                    const Quaternionf q = object.sampleRotation( pPos->Times[0] );
                    OTDU_LOG_INFO( "  pos %.3f %.3f %.3f keys %d, heading comps %d q %.3f %.3f %.3f %.3f\n", v.x, v.y, v.z,
                                   static_cast<int>( pPos->Times.size() ), pRot != nullptr ? static_cast<int>( pRot->NumComponents ) : 0,
                                   q.x, q.y, q.z, q.w );
                    if ( pRot != nullptr ) {
                        for ( size_t k = 0; k < pRot->Times.size(); k++ ) {
                            char line[256];
                            int n = snprintf( line, sizeof( line ), "t %.2f:", pRot->Times[k] );
                            for ( uint32_t c = 0; c < pRot->NumComponents && n < 200; c++ ) {
                                const float f = pRot->Values[k * pRot->NumComponents + c];
                                uint32_t raw;
                                memcpy( &raw, &f, 4 );
                                n += snprintf( line + n, sizeof( line ) - n, " %g(%08x)", f, raw );
                            }
                            OTDU_LOG_INFO( "    rot key %zu %s\n", k, line );
                        }
                    }
                }
            }
        }
        out.Scenes.push_back( std::move( scene ) );
    }
    return !out.Scenes.empty();
}

bool SchoolEndScene::start( char letter, bool bWon, const Matrix4x4f &carWorld ) {
    stop();
    const char low = static_cast<char>( tolower( static_cast<unsigned char>( letter ) ) );
    const char *pSchool = low == 'a' ? "ASPHALT" : low == 'b' ? "DIRT" : low == 'c' ? "CLASSIC" : nullptr;
    if ( pSchool == nullptr || gpFile == nullptr ) {
        return false;
    }
    const std::string bankPath = gpFile->getBankFolder() + "scenarics/packs/pack_racingschool.bnk";
    if ( !gpFile->fileExists( bankPath.c_str() ) || !bank.load( bankPath.c_str() ) ) {
        return false;
    }
    // The numbered scenes of this school and outcome (win 01-02, loose 01-04), one picked at random [INF].
    std::vector<std::string> names;
    for ( int32_t n = 1; n <= 9; n++ ) {
        char name[160];
        snprintf( name, sizeof( name ), "pack_racingschool_cinematiks_M_%s_%s_%02d_totem_WinLoose", pSchool,
                  bWon ? "WIN" : "LOOSE", n );
        if ( bank.findEntry( name, "cin" ) != nullptr ) {
            names.push_back( name );
        }
    }
    if ( names.empty() ) {
        bank.unload();
        return false;
    }
    const std::string &pick = names[static_cast<size_t>( rand() ) % names.size()];
    char loopName[160];
    snprintf( loopName, sizeof( loopName ), "pack_racingschool_cinematiks_M_%s_%s_LOOP_totem_WinLoose", pSchool,
              bWon ? "WIN" : "LOOSE" );
    main = Play();
    loop = Play();
    if ( !loadPlay( pick, main ) ) {
        OTDU_LOG_WARN( "SchoolEndScene: '%s' did not load\n", pick.c_str() );
        bank.unload();
        return false;
    }
    loadPlay( loopName, loop ); // optional: without it the last shot holds
    origin = carWorld;
    originRotation = RotationOf( carWorld );
    time = 0.0f;
    bLooping = false;
    bActive = true;
    OTDU_LOG_INFO( "SchoolEndScene: %s (%.1f s), loop %s\n", pick.c_str(), main.Cin.getDuration(),
                   loop.Scenes.empty() ? "none" : loopName );
    return true;
}

void SchoolEndScene::stop() {
    main = Play();
    loop = Play();
    clips.clear();
    if ( !bank.getEntries().empty() ) {
        bank.unload();
    }
    bActive = false;
    bLooping = false;
}

void SchoolEndScene::tick( float deltaTime ) {
    if ( !bActive ) {
        return;
    }
    time += deltaTime;
    if ( !bLooping && time >= main.Cin.getDuration() ) {
        if ( loop.Scenes.empty() ) {
            time = main.Cin.getDuration() - 1e-3f; // holds the last shot
        } else {
            bLooping = true;
            time = 0.0f;
        }
    }
    if ( bLooping && time >= loop.Cin.getDuration() ) {
        time = std::fmod( time, Max( loop.Cin.getDuration(), 0.1f ) );
    }
}

bool SchoolEndScene::getCamera( EdenSceneView::Camera &outCamera ) const {
    if ( !bActive ) {
        return false;
    }
    const Play &play = bLooping ? loop : main;
    Vector3f position;
    Quaternionf rotation;
    float fov = outCamera.Fov;
    if ( !play.Cin.getCamera( play.Scenes, Min( time, play.Cin.getDuration() - 1e-3f ), position, rotation, fov ) ) {
        return false;
    }
    // The cabin close-ups (cam_*_loose_01 / _02, win_01 ...) have a NaN x and sit below the car origin with an identity
    // heading: they are keyed to an actor's head bone [INF], which the port does not animate. Those shots fall back to the
    // loop's exterior camera (cam_*_03, in the car's frame: 4 m ahead, 2.5 m to the side, looking back at the car).
    if ( ( std::isnan( position.x ) || position.y < 0.0f ) && !loop.Scenes.empty() ) {
        if ( !loop.Cin.getCamera( loop.Scenes, 0.1f, position, rotation, fov ) ) {
            return false;
        }
    }
    outCamera.Position = origin.transformPoint( position );
    outCamera.Rotation = rotation * Conjugate( originRotation );
    outCamera.Fov = fov;
    outCamera.Near = 0.1f;
    outCamera.Far = 40000.0f;
    return true;
}

const EdenAnimation *SchoolEndScene::getClip( const Play &play, uint32_t refIndex, const CinematicScene &scene ) {
    if ( refIndex >= scene.getReferences().size() ) {
        return nullptr;
    }
    const std::string stem = FileStem( scene.getReferences()[refIndex].Name );
    auto it = clips.find( stem );
    if ( it != clips.end() ) {
        return it->second.get();
    }
    std::unique_ptr<EdenAnimation> pClip;
    if ( const BankEntry *pEntry = bank.findEntry( stem.c_str(), "anm" ) ) {
        pClip = std::make_unique<EdenAnimation>();
        if ( !pClip->parse( pEntry->pData, pEntry->Size ) ) {
            pClip.reset();
        }
    }
    if ( pClip == nullptr && CmdLine::HasArg( "-logendscene" ) ) {
        OTDU_LOG_INFO( "SchoolEndScene: clip '%s' not in the pack\n", stem.c_str() );
    }
    const EdenAnimation *pResult = pClip.get();
    clips[stem] = std::move( pClip );
    return pResult;
}

const EdenAnimation *SchoolEndScene::getActorClip( const char *pActor, float &outClipTime ) {
    if ( !bActive || pActor == nullptr ) {
        return nullptr;
    }
    const Play &play = bLooping ? loop : main;
    const float cinTime = Min( time, play.Cin.getDuration() - 1e-3f );
    const Cinematic::Shot *pShot = play.Cin.getShot( cinTime );
    if ( pShot == nullptr || pShot->Scene >= play.Scenes.size() ) {
        return nullptr;
    }
    const CinematicScene &scene = play.Scenes[pShot->Scene];
    const float sceneTime = pShot->getSceneTime( cinTime );
    const size_t length = strlen( pActor );
    for ( const CinematicScene::Object &object : scene.getObjects() ) {
        const bool bPrefix = length > 0 && pActor[length - 1] == '_';
        if ( object.isCamera() || ( bPrefix ? object.Name.compare( 0, length, pActor ) != 0 : object.Name != pActor ) ) {
            continue;
        }
        const CinematicScene::Track *pTrack = nullptr;
        for ( const CinematicScene::Track &track : object.Tracks ) {
            if ( track.Id == 0xdcd2e210 ) { // "Animation"
                pTrack = &track;
            }
        }
        if ( pTrack == nullptr || pTrack->NumComponents < 10 || pTrack->Times.empty() || sceneTime < pTrack->Times[0] ) {
            return nullptr;
        }
        size_t key = 0;
        for ( size_t k = 0; k < pTrack->Times.size(); k++ ) {
            if ( pTrack->Times[k] <= sceneTime ) {
                key = k;
            }
        }
        const float *pKey = &pTrack->Values[key * pTrack->NumComponents];
        uint32_t ref = 0;
        memcpy( &ref, &pKey[0], 4 );
        outClipTime = pKey[2] - pKey[7] + ( sceneTime - pTrack->Times[key] );
        return getClip( play, ref, scene );
    }
    return nullptr;
}
