#include "shared.h"
#include "audio/traffic_audio.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include <algorithm>

namespace {
// bfx client 32, "OtherVehicles": the audiosetup.xmb pipeline of that name has
// 5 inputs, i.e. it is spatialized (docs/re/sound3d_spec.md 2.1).
constexpr int32_t kOtherVehiclesClient = 32;

// The generic ambient car bank (CarVSTConfig + engine loops + the horn).
constexpr const char *kGenericCarBank = "Euro/Bnk/sound/vehicles/generic_high_high.bnk";

// Engine loops of the bank, in the order they are tried. pagani_onmid_ST is a
// mid-range steady loop: one clip modulated by pitch covers idle -> cruise.
constexpr const char *kEngineSamples[] = { "pagani_onmid_ST", "pagani_onlow_ST", "pagani_onhigh_ST",
                                           "pagani_idle_ST" };
constexpr const char *kHornSample = "Horn_class_1";

// Distance (m) the engine is at full level, and where it is gone.
constexpr float kFullDistance = 10.0f;
constexpr float kMaxDistance = 150.0f;
constexpr float kPoliceBoost = 1.25f; // a police car is louder (its idling V8)

// Speed (m/s) -> gain / pitch of the loop.
constexpr float kSpeedFull = 24.0f;
constexpr float kGainIdle = 0.32f;
constexpr float kPitchIdle = 0.82f;
constexpr float kPitchFull = 1.38f;
} // namespace

bool TrafficAudio::load( const char *pSoundBankPath ) {
    unload();
    if ( gpAudio == nullptr || !gpAudio->isReady() || gpFile == nullptr ) {
        return false;
    }
    if ( CmdLine::HasArg( "-notrafficaudio" ) ) {
        return false;
    }
    const char *pPath = pSoundBankPath != nullptr ? pSoundBankPath : kGenericCarBank;
    pBank = new Bank();
    if ( !pBank->load( pPath ) ) {
        OTDU_LOG_WARN( "TrafficAudio: no sound bank '%s'\n", pPath );
        unload();
        return false;
    }
    for ( const char *pSample : kEngineSamples ) {
        const BankEntry *pEntry = pBank->findEntry( pSample, "wav" );
        if ( pEntry == nullptr ) {
            continue;
        }
        engineClip = gpAudio->loadClip( pEntry->pData, pEntry->Size, pSample );
        if ( engineClip != nullptr ) {
            break;
        }
    }
    if ( const BankEntry *pEntry = pBank->findEntry( kHornSample, "wav" ) ) {
        hornClip = gpAudio->loadClip( pEntry->pData, pEntry->Size, kHornSample );
    }
    if ( engineClip == nullptr && hornClip == nullptr ) {
        OTDU_LOG_WARN( "TrafficAudio: '%s' has no engine / horn sample\n", pPath );
        unload();
        return false;
    }
    OTDU_LOG_INFO( "TrafficAudio: '%s': engine '%s', horn '%s'\n", pPath,
                   engineClip != nullptr ? engineClip->Name.c_str() : "(none)",
                   hornClip != nullptr ? hornClip->Name.c_str() : "(none)" );
    return true;
}

void TrafficAudio::unload() {
    stop();
    engineClip.reset();
    hornClip.reset();
    if ( pBank != nullptr ) {
        delete pBank;
        pBank = nullptr;
    }
}

void TrafficAudio::stop() {
    if ( gpAudio != nullptr ) {
        for ( EngineSlot &slot : engines ) {
            if ( slot.Voice != GSAudio::kInvalidVoice ) {
                gpAudio->stop( slot.Voice );
            }
        }
        for ( HornSlot &slot : horns ) {
            if ( slot.Voice != GSAudio::kInvalidVoice ) {
                gpAudio->stop( slot.Voice );
            }
        }
    }
    for ( EngineSlot &slot : engines ) {
        slot = EngineSlot();
    }
    for ( HornSlot &slot : horns ) {
        slot = HornSlot();
    }
    engineVoices = 0;
    nearestDistance = -1.0f;
}

void TrafficAudio::update( const std::vector<Agent> &agents, const Vector3f &listener, float deltaTime ) {
    if ( gpAudio == nullptr || engineClip == nullptr ) {
        return;
    }
    // Nearest agents first: only those get a voice.
    struct Candidate {
        const Agent *pAgent;
        float Distance;
    };
    std::vector<Candidate> candidates;
    candidates.reserve( agents.size() );
    nearestDistance = -1.0f;
    for ( const Agent &agent : agents ) {
        const Vector3f delta = agent.Position - listener;
        const float distance = std::sqrt( delta.x * delta.x + delta.y * delta.y + delta.z * delta.z );
        if ( nearestDistance < 0.0f || distance < nearestDistance ) {
            nearestDistance = distance;
        }
        if ( distance <= kMaxDistance ) {
            candidates.push_back( { &agent, distance } );
        }
    }
    std::sort( candidates.begin(), candidates.end(),
               []( const Candidate &a, const Candidate &b ) { return a.Distance < b.Distance; } );
    if ( candidates.size() > static_cast<size_t>( kMaxVoices ) ) {
        candidates.resize( static_cast<size_t>( kMaxVoices ) );
    }
    // Keep the slot of every agent that is still around (no restart click),
    // then hand the free slots to the newcomers.
    bool bUsed[kMaxVoices] = {};
    for ( int32_t i = 0; i < kMaxVoices; i++ ) {
        EngineSlot &slot = engines[i];
        if ( slot.Voice == GSAudio::kInvalidVoice ) {
            continue;
        }
        bool bFound = false;
        for ( size_t c = 0; c < candidates.size(); c++ ) {
            if ( candidates[c].pAgent->Id != slot.AgentId || bUsed[c] ) {
                continue;
            }
            bUsed[c] = true;
            bFound = true;
            const Agent &agent = *candidates[c].pAgent;
            const float span = kMaxDistance - kFullDistance;
            const float distanceGain =
                span > 0.0f ? Saturate( ( kMaxDistance - candidates[c].Distance ) / span ) : 0.0f;
            const float speed = Clamp( agent.Speed / kSpeedFull, 0.0f, 1.2f );
            const float gain = kGainIdle + ( 1.0f - kGainIdle ) * speed;
            slot.Gain = gain * distanceGain * ( agent.bPolice ? kPoliceBoost : 1.0f );
            slot.Pitch = kPitchIdle + ( kPitchFull - kPitchIdle ) * speed;
            gpAudio->setVoicePosition( slot.Voice, agent.Position, agent.Velocity );
            gpAudio->setVolume( slot.Voice, slot.Gain );
            gpAudio->setPitch( slot.Voice, slot.Pitch );
            break;
        }
        if ( !bFound ) {
            // Gone or out of range: the voice is stopped (the mixer's per
            // buffer ramp keeps the cut from popping).
            gpAudio->stop( slot.Voice );
            slot = EngineSlot();
        }
    }
    for ( size_t c = 0; c < candidates.size(); c++ ) {
        if ( bUsed[c] ) {
            continue;
        }
        for ( int32_t i = 0; i < kMaxVoices; i++ ) {
            EngineSlot &slot = engines[i];
            if ( slot.Voice != GSAudio::kInvalidVoice ) {
                continue;
            }
            const Agent &agent = *candidates[c].pAgent;
            GSAudio::VoiceParams params;
            params.Client = kOtherVehiclesClient;
            params.Volume = 0.0f; // update() brings it in next frame
            params.Pitch = kPitchIdle;
            params.bLoop = true;
            params.bSpatial = true;
            params.Position = agent.Position;
            params.Velocity = agent.Velocity;
            params.ClipNear = kFullDistance;
            params.ClipMid = kMaxDistance * 0.5f;
            params.ClipFar = kMaxDistance;
            params.ClipMidVolume = 0.55f;
            slot.Voice = gpAudio->play( engineClip, params );
            slot.AgentId = agent.Id;
            if ( slot.Voice != GSAudio::kInvalidVoice ) {
                slot.Gain = 0.0f;
                slot.Pitch = kPitchIdle;
            }
            bUsed[c] = true;
            break;
        }
    }
    int32_t voices = 0;
    for ( const EngineSlot &slot : engines ) {
        if ( slot.Voice != GSAudio::kInvalidVoice ) {
            voices++;
        }
    }
    engineVoices = voices;
    // Horn one shots: their slots free themselves when the clip ends.
    hornCount = 0;
    for ( HornSlot &slot : horns ) {
        if ( slot.Voice == GSAudio::kInvalidVoice ) {
            continue;
        }
        slot.TimeLeft -= deltaTime;
        if ( slot.TimeLeft <= 0.0f || !gpAudio->isPlaying( slot.Voice ) ) {
            gpAudio->stop( slot.Voice );
            slot = HornSlot();
            continue;
        }
        hornCount++;
    }
}

void TrafficAudio::horn( const Vector3f &position, float volume ) {
    if ( gpAudio == nullptr || hornClip == nullptr ) {
        return;
    }
    for ( HornSlot &slot : horns ) {
        if ( slot.Voice != GSAudio::kInvalidVoice ) {
            continue;
        }
        GSAudio::VoiceParams params;
        params.Client = kOtherVehiclesClient;
        params.Volume = Clamp( volume, 0.0f, 2.0f );
        params.bLoop = false;
        params.bSpatial = true;
        params.Position = position;
        params.ClipNear = kFullDistance;
        params.ClipMid = kMaxDistance * 0.5f;
        params.ClipFar = kMaxDistance;
        params.ClipMidVolume = 0.5f;
        slot.Voice = gpAudio->play( hornClip, params );
        slot.Level = params.Volume;
        slot.TimeLeft = hornClip->Duration > 0.0f ? hornClip->Duration : 1.0f;
        return;
    }
}

