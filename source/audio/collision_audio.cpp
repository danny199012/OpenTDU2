#include "shared.h"
#include "audio/collision_audio.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include <algorithm>

namespace {
constexpr const char *kPhysicsBank = "Euro/Bnk/fx/physicsfxsetup.bnk";
constexpr int32_t kPhysicSfxClient = 21; // "PhysicSfx" in AudioSetup::GetBfxClientName
constexpr float kNear = 6.0f;
constexpr float kFar = 90.0f;
} // namespace

void CollisionAudio::loadPool( Bank &bank, Pool &pool, const char *pPrefix, int32_t first, int32_t last ) {
    for ( int32_t i = first; i <= last; i++ ) {
        char name[96];
        snprintf( name, sizeof( name ), "%s%02d_PHYSIC_SFX", pPrefix, i );
        if ( const BankEntry *pEntry = bank.findEntry( name, "wav" ) ) {
            if ( auto clip = gpAudio->loadClip( pEntry->pData, pEntry->Size, name ) ) {
                pool.Clips.push_back( std::move( clip ) );
            }
        }
    }
}

bool CollisionAudio::load() {
    unload();
    if ( gpAudio == nullptr || !gpAudio->isReady() || gpFile == nullptr || CmdLine::HasArg( "-nocollisionaudio" ) ) {
        return false;
    }
    bLog = CmdLine::HasArg( "-logcollisionaudio" );
    Bank bank;
    if ( !bank.load( kPhysicsBank ) ) {
        OTDU_LOG_WARN( "CollisionAudio: no bank '%s'\n", kPhysicsBank );
        return false;
    }
    loadPool( bank, metalLow, "carmetal_low", 1, 6 );
    loadPool( bank, metalHigh, "carmetal_high", 1, 6 );
    loadPool( bank, groundLow, "carground_low", 1, 6 );
    loadPool( bank, groundHigh, "carground_high", 1, 6 );
    loadPool( bank, carLow, "carcar_low", 1, 4 );
    loadPool( bank, carHigh, "carcar_high", 1, 4 );
    loadPool( bank, scrapeLoop, "carmetal_loop", 1, 4 );
    loadPool( bank, suspension, "carsuspension", 1, 7 );
    loadPool( bank, leavesLow, "carleaves_low", 1, 3 );
    loadPool( bank, leavesHigh, "carleaves_high", 1, 3 );
    OTDU_LOG_INFO( "CollisionAudio: metal %zu/%zu, ground %zu/%zu, car %zu/%zu, scrape loops %zu\n", metalLow.Clips.size(),
                   metalHigh.Clips.size(), groundLow.Clips.size(), groundHigh.Clips.size(), carLow.Clips.size(),
                   carHigh.Clips.size(), scrapeLoop.Clips.size() );
    bLoaded = !metalLow.Clips.empty() || !metalHigh.Clips.empty();
    return bLoaded;
}

void CollisionAudio::unload() {
    stop();
    for ( Pool *pool : { &metalLow, &metalHigh, &groundLow, &groundHigh, &carLow, &carHigh, &scrapeLoop, &suspension, &leavesLow, &leavesHigh } ) {
        pool->Clips.clear();
    }
    bLoaded = false;
}

void CollisionAudio::stop() {
    if ( gpAudio != nullptr && scrapeVoice != GSAudio::kInvalidVoice ) {
        gpAudio->stop( scrapeVoice );
    }
    scrapeVoice = GSAudio::kInvalidVoice;
    scrapeLevel = 0.0f;
}

void CollisionAudio::play( Pool &pool, const Vector3f &position, float volume, float pitch ) {
    if ( gpAudio == nullptr || pool.Clips.empty() ) {
        return;
    }
    seed = seed * 1664525u + 1013904223u;
    // A random clip, never the one that just played.
    uint32_t index = ( seed >> 16 ) % static_cast<uint32_t>( pool.Clips.size() );
    if ( pool.Clips.size() > 1 && index == pool.Cursor ) {
        index = ( index + 1 ) % static_cast<uint32_t>( pool.Clips.size() );
    }
    pool.Cursor = index;
    GSAudio::VoiceParams params;
    params.Client = kPhysicSfxClient;
    params.Volume = Clamp( volume, 0.0f, 2.0f );
    params.Pitch = pitch;
    params.bSpatial = true;
    params.Position = position;
    params.ClipNear = kNear;
    params.ClipMid = kFar * 0.5f;
    params.ClipFar = kFar;
    params.ClipMidVolume = 0.5f;
    gpAudio->play( pool.Clips[index], params );
    hitCount++;
    if ( bLog ) {
        OTDU_LOG_INFO( "CollisionAudio: '%s' volume %.2f pitch %.2f\n", pool.Clips[index]->Name.c_str(), params.Volume, pitch );
    }
}

void CollisionAudio::hitWall( const Vector3f &position, float speed ) {
    if ( !bLoaded || cooldown > 0.0f ) {
        return;
    }
    cooldown = 0.12f;
    play( speed < 6.0f ? metalLow : metalHigh, position, 0.35f + speed / 16.0f, 0.95f + 0.1f * static_cast<float>( ( seed >> 8 ) & 7 ) / 7.0f );
}

void CollisionAudio::hitFloor( const Vector3f &position, float speed ) {
    if ( !bLoaded || cooldown > 0.0f || speed < 1.8f ) {
        return;
    }
    cooldown = 0.15f;
    play( speed < 5.0f ? groundLow : groundHigh, position, 0.3f + speed / 14.0f, 1.0f );
}

void CollisionAudio::hitCar( const Vector3f &position, float speed ) {
    if ( !bLoaded ) {
        return;
    }
    cooldown = 0.2f;
    play( speed < 6.0f ? carLow : carHigh, position, 0.5f + speed / 14.0f, 1.0f );
}

void CollisionAudio::land( const Vector3f &position, float speed, uint32_t surface ) {
    if ( !bLoaded || speed < 2.0f ) {
        return;
    }
    const float volume = Clamp( 0.3f + speed / 10.0f, 0.3f, 1.5f );
    play( suspension, position, volume, 1.0f );
    if ( surface == 2 ) {
        play( speed < 5.0f ? leavesLow : leavesHigh, position, volume * 0.8f, 1.0f );
    } else if ( surface == 1 ) {
        play( speed < 5.0f ? groundLow : groundHigh, position, volume * 0.7f, 1.0f );
    }
}

void CollisionAudio::updateScrape( const Vector3f &position, float slideSpeed, float deltaTime ) {
    cooldown = Max( 0.0f, cooldown - deltaTime );
    if ( !bLoaded || gpAudio == nullptr || scrapeLoop.Clips.empty() ) {
        return;
    }
    const float target = slideSpeed > 1.5f ? Clamp( slideSpeed / 14.0f, 0.15f, 1.0f ) : 0.0f;
    scrapeLevel += ( target - scrapeLevel ) * Min( 1.0f, deltaTime * ( target > scrapeLevel ? 18.0f : 7.0f ) );
    if ( scrapeLevel < 0.02f && target == 0.0f ) {
        scrapeIdle += deltaTime;
        if ( scrapeVoice != GSAudio::kInvalidVoice && scrapeIdle > 0.2f ) {
            gpAudio->stop( scrapeVoice );
            scrapeVoice = GSAudio::kInvalidVoice;
        }
        return;
    }
    scrapeIdle = 0.0f;
    const float pitch = 0.8f + Clamp( slideSpeed / 25.0f, 0.0f, 0.5f );
    if ( scrapeVoice == GSAudio::kInvalidVoice || !gpAudio->isPlaying( scrapeVoice ) ) {
        GSAudio::VoiceParams params;
        params.Client = kPhysicSfxClient;
        params.Volume = scrapeLevel * 0.9f;
        params.Pitch = pitch;
        params.bLoop = true;
        params.bSpatial = true;
        params.Position = position;
        params.ClipNear = kNear;
        params.ClipMid = kFar * 0.5f;
        params.ClipFar = kFar;
        params.ClipMidVolume = 0.5f;
        scrapeVoice = gpAudio->play( scrapeLoop.Clips[0], params );
    } else {
        gpAudio->setVolume( scrapeVoice, scrapeLevel * 0.9f );
        gpAudio->setPitch( scrapeVoice, pitch );
        gpAudio->setVoicePosition( scrapeVoice, position );
    }
}
