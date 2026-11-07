#include "shared.h"
#include "fade_freeride.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

namespace {
constexpr const char *kBank = "scenarics/cinematics/intro/fade_freeride.bnk";
constexpr const char *kCinematic = "C_FADE_FREERIDE";
// The WOMAN entity of the .sce: the DBPNJ of Tess (avatar_spec 4,
// Tess_Wintory STO_Tess_Wintory -> STO_Tess, W_HR_R_Tess(14) hair colours).
constexpr const char *kWomanConfig = "STO_Tess_Wintory";
// Pose hold after the cinematic (see draw): the anim's climb into the car
// starts ~14.3 s; the seated pose just before it is kept.
constexpr float kEndPoseTime = 14.2f;

Quaternionf Conjugate( const Quaternionf &q ) { return Quaternionf( -q.x, -q.y, -q.z, q.w ); }

// Rotation part of a rigid matrix -> quaternion (gm_freeride.cpp).
Quaternionf QuaternionFromMatrix( const Matrix4x4f &m ) {
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

std::string FileStem( const std::string &path ) {
    const size_t slash = path.find_last_of( "\\/" );
    std::string file = slash != std::string::npos ? path.substr( slash + 1 ) : path;
    const size_t dot = file.find_last_of( '.' );
    return dot != std::string::npos ? file.substr( 0, dot ) : file;
}
} // namespace

bool FadeFreeride::load( const Matrix4x4f &houseOrigin ) {
    unload();
    sceneOrigin = houseOrigin;
    sceneRotation = QuaternionFromMatrix( houseOrigin );
    const std::string bankPath = gpFile->getBankFolder() + kBank;
    if ( !bank.load( bankPath.c_str() ) ) {
        OTDU_LOG_WARN( "FadeFreeride: '%s' not found\n", bankPath.c_str() );
        return false;
    }
    hashcodes.load( bank );
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "FadeFreeride: entities.xmb not found\n" );
        return false;
    }
    // Entity__Cinematic C_FADE_FREERIDE: CinFile[0] .cin, CinFile[1] .sce,
    // additionalsAnims[0].xan[0] = ev_tessincar_toplayer01_npc.anm.
    XmbValue entityRoot = entities.getRoot()["ExportType_Entity"];
    std::string cinName, sceName;
    uint64_t animHash = 0;
    for ( uint32_t i = 0; i < entityRoot.getArraySize( "ArrayOf_Entity__Cinematic" ); i++ ) {
        XmbValue entry = entityRoot.getArrayElement( "ArrayOf_Entity__Cinematic", i );
        if ( entry["__Name"].asString() != kCinematic ) {
            continue;
        }
        for ( uint32_t j = 0; j < entry.getArraySize( "CinFile" ); j++ ) {
            XmbValue file = entry.getArrayElement( "CinFile", j );
            const BankEntry *pFile = hashcodes.find( file["rsc"].asUInt64() );
            if ( pFile != nullptr ) {
                ( pFile->Extension == "cin" ? cinName : sceName ) = pFile->Name;
            }
        }
        if ( entry.getArraySize( "additionalsAnims" ) > 0 ) {
            XmbValue styleBase = entry.getArrayElement( "additionalsAnims", 0 );
            if ( styleBase.getArraySize( "xan" ) > 0 ) {
                animHash = styleBase.getArrayElement( "xan", 0 )["rsc"].asUInt64();
            }
        }
        OTDU_LOG_INFO( "FadeFreeride: %s CinProps %lld CinEnd %lld: '%s' / '%s' anim %016llx\n", kCinematic,
                       static_cast<long long>( entry.getArraySize( "CinProps" ) > 0
                                                   ? entry.getArrayElement( "CinProps", 0 ).asInt()
                                                   : 0 ),
                       static_cast<long long>( entry["CinEnd"].asInt() ), cinName.c_str(), sceName.c_str(),
                       static_cast<long long unsigned>( animHash ) );
    }
    // The WOMAN entity placement (the scene position track is the hidden
    // state of the life scene): the placeholder wake-up plays her here.
    bool bWomanFound = false;
    for ( uint32_t i = 0; i < entityRoot.getArraySize( "ArrayOf_Entity" ); i++ ) {
        XmbValue entry = entityRoot.getArrayElement( "ArrayOf_Entity", i );
        if ( entry["__Name"].asString() != "WOMAN" ) {
            continue;
        }
        XmbValue placement = entry["orient_pos_scale"];
        womanRot = Quaternionf( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                                placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
        womanLocal = Vector3f( placement["pos"]["X"].asFloat(), placement["pos"]["Y"].asFloat(),
                               placement["pos"]["Z"].asFloat() );
        anchorToHouse();
        OTDU_LOG_INFO( "FadeFreeride: WOMAN at (%.3f, %.3f, %.3f)\n", womanLocal.x, womanLocal.y, womanLocal.z );
        bWomanFound = true;
        break;
    }
    const BankEntry *pCin = bank.findEntry( cinName.empty() ? "fade_blanc_M_FADE_FREERIDE" : cinName.c_str(), "cin" );
    if ( pCin == nullptr || !cinematic.parse( pCin->pData, pCin->Size ) ) {
        OTDU_LOG_WARN( "FadeFreeride: %s .cin not found\n", kCinematic );
        return false;
    }
    for ( int32_t reference : cinematic.getSceneReferences() ) {
        CinematicScene scene;
        if ( reference >= 0 && reference < static_cast<int32_t>( cinematic.getReferences().size() ) ) {
            const BankEntry *pScene =
                bank.findEntry( FileStem( cinematic.getReferences()[reference].Text ).c_str(), "sce" );
            if ( pScene == nullptr || !scene.parseScene( pScene->pData, pScene->Size ) ) {
                OTDU_LOG_WARN( "FadeFreeride: scene of %s not found\n", kCinematic );
            }
        }
        scenes.push_back( std::move( scene ) );
    }
    if ( scenes.empty() ) {
        return false;
    }
    // The additional anim on the WOMAN skeleton (85 tracks, 29.2 s: the
    // knock and the talk are the first 15 s).
    if ( animHash != 0 ) {
        if ( const BankEntry *pAnim = hashcodes.find( animHash, 0, 8 ) ) {
            bAnim = anim.parse( pAnim->pData, pAnim->Size );
            if ( !bAnim ) {
                OTDU_LOG_WARN( "FadeFreeride: the WOMAN anim did not parse\n" );
            }
        }
    }
    // Tess (DBPNJ STO_Tess_Wintory). Optional: the cinematic plays without
    // her (camera, sounds) when the avatar library is not available.
    if ( bWomanFound && avatarLibrary.load() ) {
        woman = std::make_unique<EdenAvatar>();
        if ( !woman->load( avatarLibrary, kWomanConfig ) ) {
            OTDU_LOG_WARN( "FadeFreeride: avatar '%s' not found\n", kWomanConfig );
            woman.reset();
        }
    }
    sounds.load( bank );
    sounds.setSceneOrigin( sceneOrigin );
    bLoaded = true;
    return true;
}

void FadeFreeride::unload() {
    sounds.stop( voices );
    sounds.unload();
    woman.reset();
    avatarLibrary.unload();
    locals.clear();
    worldBones.clear();
    skin.clear();
    scenes.clear();
    if ( !bank.getEntries().empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( bank.getPath().c_str() );
    }
    bank.unload();
    bAnim = false;
    bPlaying = false;
    bStarted = false;
    bLoaded = false;
    time = 0.0f;
}

void FadeFreeride::start( float startTime, bool bSounds ) {
    time = startTime;
    this->bSounds = bSounds;
    bPlaying = bLoaded;
    bStarted = bLoaded;
}

void FadeFreeride::anchorToCar( const Matrix4x4f &carWorld ) {
    // Driver window of the car wherever it stopped [INF, retail footage]:
    // outboard of the door plane (the California is 0.95 m half width) at
    // the driver's longitudinal position, ~0.55 m from the glass, feet on
    // the ground (the car origin sits ~0.12 above it), facing the window.
    const Vector3f position = carWorld.transformPoint( Vector3f( 1.50f, -0.12f, -0.36f ) );
    const Vector3f outboard = carWorld.transformVector( Vector3f( 1.0f, 0.0f, 0.0f ) );
    const Vector3f forward = Vector3f( -outboard.x, 0.0f, -outboard.z );
    const float length = forward.length();
    const float yaw = length > 1e-4f ? atan2f( forward.x / length, forward.z / length ) : 0.0f;
    const Quaternionf heading( 0.0f, -std::sin( yaw * 0.5f ), 0.0f, std::cos( yaw * 0.5f ) );
    womanWorld = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( heading );
}

void FadeFreeride::anchorToHouse() {
    womanWorld = sceneOrigin * Matrix4x4f::Translation( womanLocal ) * EdenRotationFromQuaternion( womanRot );
}

void FadeFreeride::tick( float deltaTime ) {
    if ( !bPlaying ) {
        return;
    }
    const float previous = time;
    time += deltaTime;
    // Fx track: FxIntroNew-E_Meet_Tess_02_00 at 2.68 s (knock + voices).
    if ( bSounds && !CmdLine::HasArg( "-noaudio" ) ) {
        sounds.update( cinematic, previous, time, &voices );
    }
    // CinEnd 3: the end pose is kept.
    if ( time >= cinematic.getDuration() ) {
        time = cinematic.getDuration();
        bPlaying = false;
    }
}

float FadeFreeride::getWhiteAlpha() const {
    // Start transition of the shot: FadeType 1 = fade from FadeColor
    // (white, fade_blanc), over FadeDuration (2.5 s).
    const Cinematic::Shot *pShot = cinematic.getShot( time );
    if ( pShot == nullptr || pShot->FadeType == 0 || pShot->FadeDuration <= 0.0f ) {
        return 0.0f;
    }
    return Clamp( 1.0f - time / pShot->FadeDuration, 0.0f, 1.0f );
}

bool FadeFreeride::getCamera( EdenSceneView::Camera &outCamera ) const {
    Vector3f position;
    Quaternionf rotation;
    float fov = outCamera.Fov;
    if ( !cinematic.getCamera( scenes, Min( time, cinematic.getDuration() - 1e-3f ), position, rotation, fov ) ) {
        return false;
    }
    outCamera.Position = sceneOrigin.transformPoint( position );
    outCamera.Rotation = rotation * Conjugate( sceneRotation );
    outCamera.Fov = fov;
    outCamera.Near = 0.8f; // Znear -1 -> 0.8 (FUN_00b3eba0)
    outCamera.Far = 40000.0f;
    return true;
}

void FadeFreeride::draw( EdenRenderer &renderer ) {
    if ( woman == nullptr || !bAnim ) {
        return;
    }
    const EdenSkeleton &skeleton = woman->getSkeleton();
    locals.resize( skeleton.getNumBones() );
    for ( uint32_t b = 0; b < skeleton.getNumBones(); b++ ) {
        locals[b] = skeleton.getBindLocal( b );
    }
    // The anim (29.2 s) continues past the 15 s cinematic: its last second
    // is the climb into the car, which the next step-3 cinematics play
    // through. This port holds the seated pose just before that instead.
    anim.sample( skeleton, Min( time, kEndPoseTime ), false, locals );
    skeleton.computeSkinMatrices( locals, worldBones, skin );
    woman->draw( renderer, womanWorld, skin, nullptr );
}
