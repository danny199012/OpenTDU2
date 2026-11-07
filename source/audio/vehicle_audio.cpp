#include "shared.h"
#include "audio/vehicle_audio.h"

#include "audio/audio_setup.h"
#include "core/xmb.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"

#include <algorithm>

namespace {
// The physics axes the curves are evaluated against (CarVSTConfig Physic[]).
constexpr int32_t kAxisRpm = 0;
constexpr int32_t kAxisSpeed = 1;
constexpr int32_t kAxisLoad = 3;
constexpr int32_t kAxisSlipAngle = 26; // 26..29: the 4 wheels
constexpr int32_t kAxisSlipRatio = 34; // 34..37: the 4 wheels
// Axis 43 (suspension travel) has no source in the dynamics yet: its volume
// links are ignored (neutral).

// Sample role -> client (bfx index, AudioSetup::kBfxClientNames).
int32_t PanelToClient( int32_t panel ) {
    switch ( panel ) {
    case 0: return 24; // Engine -> Engine
    case 1: return 24; // EngineStart -> Engine
    case 2: return 26; // BackFiring -> ExhaustPipe
    case 3: return 27; // Transmission -> Transmission
    case 4: return 28; // Aerodynamic -> Aerodynamic
    case 5: return 29; // Skid -> Skid
    case 6: return 30; // Rolling -> Rolling
    default: return 25; // panel 7 (gear shifts, doors...) -> EngineFx
    }
}

int32_t SurfaceFromName( const std::string &name ) {
    if ( name.find( "Road" ) != std::string::npos ) {
        return 0;
    }
    if ( name.find( "Dirt" ) != std::string::npos ) {
        return 1;
    }
    if ( name.find( "Grass" ) != std::string::npos ) {
        return 2;
    }
    return -1;
}

// Chase camera view: the external layer of the interior / external pairs.
bool WantedByView( const std::string &name ) {
    const bool bInternal = name.find( "Internal" ) != std::string::npos;
    return !bInternal;
}
} // namespace

float VehicleAudio::Curve::eval( float x ) const {
    if ( Xs.empty() ) {
        return 1.0f;
    }
    if ( x <= Xs.front() ) {
        return Ys.front();
    }
    if ( x >= Xs.back() ) {
        return Ys.back();
    }
    for ( size_t i = 1; i < Xs.size(); i++ ) {
        if ( x <= Xs[i] ) {
            const float span = Xs[i] - Xs[i - 1];
            const float t = span > 1e-9f ? ( x - Xs[i - 1] ) / span : 0.0f;
            return Ys[i - 1] + ( Ys[i] - Ys[i - 1] ) * t;
        }
    }
    return Ys.back();
}

bool VehicleAudio::load( const char *pSoundBankPath ) {
    unload();
    if ( gpAudio == nullptr || !gpAudio->isReady() || gpFile == nullptr ) {
        return false;
    }
    pBank = new Bank();
    if ( !pBank->load( pSoundBankPath ) ) {
        unload();
        return false;
    }
    const BankEntry *pConfig = pBank->findEntry( "CarVSTConfig", "xmb" );
    if ( pConfig == nullptr ) {
        OTDU_LOG_WARN( "VehicleAudio: no CarVSTConfig.xmb in '%s'\n", pSoundBankPath );
        unload();
        return false;
    }
    XmbDocument document;
    if ( !document.load( pConfig->pData, pConfig->Size ) ) {
        OTDU_LOG_WARN( "VehicleAudio: CarVSTConfig.xmb of '%s' not parsed\n", pSoundBankPath );
        unload();
        return false;
    }
    XmbValue root = document.getRoot();
    std::vector<std::string> sampleFiles;
    for ( uint32_t i = 0; i < root.getArraySize( "SampleFiles" ); i++ ) {
        sampleFiles.push_back( root.getArrayElement( "SampleFiles", i ).asString() );
    }
    for ( uint32_t i = 0; i < root.getArraySize( "SamplesConfig" ); i++ ) {
        XmbValue s = root.getArrayElement( "SamplesConfig", i );
        Layer layer;
        layer.Name = s["sSampleName"].asString();
        const int32_t wave = s["nWaveIndex"].asInt();
        layer.BaseVolume = s["fBaseVolume"].asFloat( 1.0f );
        layer.Panel = s["PanelID"].asInt();
        layer.bTrigger = s["bIsTrigger"].asBool();
        layer.bLoop = s["nMustBeLooped"].asBool();
        layer.SoundSurface = SurfaceFromName( layer.Name );
        layer.ClipNear = s["fClipNear"].asFloat( 4.0f );
        layer.ClipFar = s["fClipFar"].asFloat( 40.0f );
        // PlaybackStateVolume is a plain Single[]: read element 0 (the chase
        // camera view is playback state 0) through getArrayData.
        {
            uint32_t count = 0;
            if ( const uint8_t *pData = s.getArrayData( "PlaybackStateVolume", &count ) ) {
                if ( count > 0 ) {
                    float value;
                    memcpy( &value, pData, sizeof( value ) );
                    layer.StateVolume = value;
                }
            }
        }
        for ( uint32_t k = 0; k < s.getArraySize( "Links" ); k++ ) {
            XmbValue lk = s.getArrayElement( "Links", k );
            Layer::Link link;
            link.bPitch = lk["Audio"].asInt() == 0;
            if ( lk.getArraySize( "Physic" ) > 0 ) {
                link.Axis = lk.getArrayElement( "Physic", 0 ).asInt();
            }
            const uint32_t n = lk.getArraySize( "CurveSet" );
            link.CurveData.Xs.reserve( n / 2 );
            link.CurveData.Ys.reserve( n / 2 );
            for ( uint32_t c = 0; c + 1 < n; c += 2 ) {
                link.CurveData.Xs.push_back( lk.getArrayElement( "CurveSet", c ).asFloat() );
                link.CurveData.Ys.push_back( lk.getArrayElement( "CurveSet", c + 1 ).asFloat() );
            }
            layer.Links.push_back( std::move( link ) );
            if ( link.Axis == kAxisLoad && !link.bPitch ) {
                for ( float x : link.CurveData.Xs ) {
                    if ( x > 0.0f ) {
                        loadScale = Max( loadScale, x );
                    }
                }
            }
        }
        if ( wave < 0 || static_cast<uint32_t>( wave ) >= sampleFiles.size() ) {
            continue;
        }
        std::string file = sampleFiles[wave];
        const size_t dot = file.rfind( '.' );
        std::string name = dot != std::string::npos ? file.substr( 0, dot ) : file;
        std::string ext = dot != std::string::npos ? file.substr( dot + 1 ) : "wav";
        const BankEntry *pWav = pBank->findEntry( name.c_str(), ext.c_str() );
        if ( pWav == nullptr ) {
            continue;
        }
        layer.Clip = gpAudio->loadClip( pWav->pData, pWav->Size, pWav->Name.c_str() );
        if ( layer.Clip == nullptr ) {
            continue;
        }
        if ( layer.Name == "EngineStart" ) {
            startLayer = static_cast<int32_t>( layers.size() );
        }
        if ( layer.Name == "Horn" ) {
            hornLayer = static_cast<int32_t>( layers.size() );
        }
        if ( layer.Name == "GearBoxUpInternal" ) {
            gearUp[0] = static_cast<int32_t>( layers.size() );
        } else if ( layer.Name == "GearBoxUpExternal" ) {
            gearUp[1] = static_cast<int32_t>( layers.size() );
        } else if ( layer.Name == "GearBoxDownInternal" ) {
            gearDown[0] = static_cast<int32_t>( layers.size() );
        } else if ( layer.Name == "GearBoxDownExternal" ) {
            gearDown[1] = static_cast<int32_t>( layers.size() );
        }
        layers.push_back( std::move( layer ) );
    }
    OTDU_LOG_INFO( "VehicleAudio: '%s': %zu samples (load scale %.0f)\n", pSoundBankPath, layers.size(),
                   loadScale );
    return !layers.empty();
}

void VehicleAudio::unload() {
    stop();
    layers.clear();
    startLayer = -1;
    hornLayer = -1;
    hornVoice = GSAudio::kInvalidVoice;
    gearUp[0] = gearUp[1] = gearDown[0] = gearDown[1] = -1;
    if ( pBank != nullptr ) {
        delete pBank;
        pBank = nullptr;
    }
    smoothedLoad = 0.0f;
    smoothedRpm = 0.0f;
    smoothedSpeed = 0.0f;
    smoothedSlipRatio = 0.0f;
    smoothedSlipAngle = 0.0f;
    lastGear = 0;
}

void VehicleAudio::playLayer( Layer &layer ) {
    if ( layer.Voice != GSAudio::kInvalidVoice || layer.Clip == nullptr ) {
        return;
    }
    GSAudio::VoiceParams params;
    params.Client = PanelToClient( layer.Panel );
    params.Volume = 0.0f;
    params.bLoop = layer.bLoop;
    params.bSpatial = true;
    params.ClipNear = layer.ClipNear;
    params.ClipMid = Max( layer.ClipFar * 0.6f, layer.ClipNear );
    params.ClipFar = Max( layer.ClipFar, params.ClipMid );
    params.ClipMidVolume = 0.5f;
    layer.Voice = gpAudio->play( layer.Clip, params );
    layer.CurrentGain = -1.0f;
}

void VehicleAudio::startEngine() {
    if ( !isLoaded() || bEngineRunning ) {
        return;
    }
    bEngineRunning = true;
    smoothedLoad = 0.0f;
    smoothedRpm = 0.0f;
    smoothedSpeed = 0.0f;
    // The loops are started muted; update() brings them in.
    for ( Layer &layer : layers ) {
        layer.CurrentGain = -1.0f;
        if ( !layer.bTrigger && layer.Clip != nullptr && WantedByView( layer.Name ) ) {
            playLayer( layer );
        }
    }
    if ( startLayer >= 0 && layers[startLayer].Clip != nullptr ) {
        playLayer( layers[startLayer] );
    }
}

void VehicleAudio::stop() {
    if ( gpAudio == nullptr ) {
        return;
    }
    if ( hornVoice != GSAudio::kInvalidVoice ) {
        gpAudio->stop( hornVoice );
        hornVoice = GSAudio::kInvalidVoice;
    }
    for ( Layer &layer : layers ) {
        if ( layer.Voice != GSAudio::kInvalidVoice ) {
            gpAudio->stop( layer.Voice );
            layer.Voice = GSAudio::kInvalidVoice;
        }
        layer.CurrentGain = -1.0f;
    }
    bEngineRunning = false;
}

void VehicleAudio::updateLayer( Layer &layer, const Params &params, float loadRaw, float gainScale ) {
    if ( layer.Voice == GSAudio::kInvalidVoice ) {
        return;
    }
    // Surface families (Rolling / Skid x Road / Dirt / Grass): only the
    // layers of the current surface are audible.
    if ( layer.SoundSurface >= 0 && layer.SoundSurface != static_cast<int32_t>( params.Surface ) ) {
        if ( layer.CurrentGain != 0.0f ) {
            gpAudio->setVolume( layer.Voice, 0.0f );
            layer.CurrentGain = 0.0f;
        }
        return;
    }
    float gain = layer.BaseVolume * layer.StateVolume * gainScale;
    float pitch = 1.0f;
    bool bPitchSet = false;
    for ( const Layer::Link &link : layer.Links ) {
        float x = 0.0f;
        switch ( link.Axis ) {
        case kAxisRpm: x = smoothedRpm; break;
        case kAxisSpeed: x = smoothedSpeed; break;
        case kAxisLoad: x = loadRaw; break;
        case kAxisSlipAngle: x = smoothedSlipAngle; break;
        case kAxisSlipRatio: x = smoothedSlipRatio; break;
        default:
            // Unsupported axis (43 suspension, ...): the link is ignored
            // (neutral), except volume links that gate the sample hard: keep
            // the sample at the last gain of the other links [INF].
            continue;
        }
        const float value = link.CurveData.eval( x );
        if ( link.bPitch ) {
            if ( !bPitchSet ) {
                pitch = Max( value, 0.01f );
                bPitchSet = true;
            }
        } else {
            gain *= Max( value, 0.0f );
        }
    }
    gain = Clamp( gain, 0.0f, 2.0f );
    if ( layer.CurrentGain < 0.0f || fabsf( gain - layer.CurrentGain ) > 0.001f ) {
        gpAudio->setVolume( layer.Voice, gain );
        layer.CurrentGain = gain;
    }
    if ( !bPitchSet ) {
        pitch = 1.0f;
    }
    if ( fabsf( pitch - layer.CurrentPitch ) > 0.002f ) {
        gpAudio->setPitch( layer.Voice, pitch );
        layer.CurrentPitch = pitch;
    }
    gpAudio->setVoicePosition( layer.Voice, params.Position, smoothedVelocity );
}

void VehicleAudio::update( const Params &params, float deltaTime ) {
    if ( !isLoaded() || gpAudio == nullptr || !bEngineRunning ) {
        return;
    }
    // Smooth the physics inputs (the curves are piecewise linear; the raw
    // values would make the layer cross fades step with the 90 Hz ticks).
    const float k = deltaTime > 0.0f ? Clamp( deltaTime * 8.0f, 0.0f, 1.0f ) : 1.0f;
    smoothedRpm += ( params.Rpm - smoothedRpm ) * k;
    smoothedSpeed += ( fabsf( params.SpeedKmh ) - smoothedSpeed ) * k;
    smoothedSlipRatio += ( params.MaxSlipRatio - smoothedSlipRatio ) * k;
    smoothedSlipAngle += ( params.MaxSlipAngle - smoothedSlipAngle ) * k;
    smoothedVelocity += ( params.Velocity - smoothedVelocity ) * k;
    // Engine load: throttle ramps up with the pedal, releases to the overrun
    // side when it is lifted [INF: the original axis is the engine torque].
    const float targetLoad = params.bEngineOn ? ( params.Throttle > 0.03f ? params.Throttle * loadScale
                                                                          : -loadScale * 0.6f )
                                              : 0.0f;
    const float rate = targetLoad > smoothedLoad ? 6.0f : 4.0f; // load / s
    smoothedLoad += Clamp( targetLoad - smoothedLoad, -rate * deltaTime, rate * deltaTime );

    const float gainScale = params.bEngineOn ? 1.0f : 0.0f;
    for ( Layer &layer : layers ) {
        if ( layer.bTrigger ) {
            continue;
        }
        updateLayer( layer, params, smoothedLoad, gainScale );
    }
}

float VehicleAudio::getMaxLayerGain() const {
    float gain = 0.0f;
    for ( const Layer &layer : layers ) {
        if ( !layer.bTrigger && layer.Voice != GSAudio::kInvalidVoice ) {
            gain = Max( gain, layer.CurrentGain );
        }
    }
    return gain;
}

void VehicleAudio::setGear( int32_t gear ) {
    if ( !isLoaded() || gpAudio == nullptr || !bEngineRunning || gear == lastGear ) {
        lastGear = gear;
        return;
    }
    const int32_t previous = lastGear;
    lastGear = gear;
    // Forward gears only (auto gearbox: 0 = reverse / neutral).
    if ( gear <= 0 || previous <= 0 ) {
        return;
    }
    const bool bUp = gear > previous;
    const int32_t index = bUp ? gearUp[1] : gearDown[1]; // chase camera: external
    if ( index >= 0 && layers[index].Clip != nullptr ) {
        playLayer( layers[index] );
        gpAudio->stop( layers[index].Voice );
        layers[index].Voice = GSAudio::kInvalidVoice;
        playLayer( layers[index] );
    }
}

void VehicleAudio::setHorn( bool bOn ) {
    if ( gpAudio == nullptr || hornLayer < 0 || static_cast<size_t>( hornLayer ) >= layers.size() ) {
        return;
    }
    if ( !bOn ) {
        if ( hornVoice != GSAudio::kInvalidVoice ) {
            gpAudio->stop( hornVoice );
            hornVoice = GSAudio::kInvalidVoice;
        }
        return;
    }
    if ( hornVoice != GSAudio::kInvalidVoice ) {
        return; // already sounding
    }
    Layer &layer = layers[static_cast<size_t>( hornLayer )];
    if ( layer.Clip == nullptr ) {
        return;
    }
    // PanelID 7 -> EngineFx (2 inputs, the car's own bus, not spatialized):
    // the horn belongs to the car the listener sits in.
    GSAudio::VoiceParams params;
    params.Client = PanelToClient( layer.Panel );
    params.Volume = layer.BaseVolume * layer.StateVolume;
    params.Pitch = 1.0f;
    params.bLoop = true;
    hornVoice = gpAudio->play( layer.Clip, params );
}
