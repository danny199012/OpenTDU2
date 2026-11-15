#include "shared.h"
#include "game/freeride/lightning_storm.h"

#include "filesystem/gs_file.h"
#include "render/eden/eden_weather.h"
#include "system/cmd_line.h"

#include <algorithm>

namespace {
constexpr int32_t kLightningClient = 17; // "AmbienceLightning" in AudioSetup::GetBfxClientName
constexpr float kSoundSpeed = 343.0f;

// File names of the island's thunder, grouped by how close the strike is (high = a sharp crack).
const char *const kClipNames[3][4] = {
    { "eclair_high", "eclair_high2", nullptr, nullptr },
    { "eclair_mid", "eclair_mid2", "eclair_mid3", nullptr },
    { "eclair_low", "eclair_low2", "eclair_low3", "eclair_low4" },
};

// A (min, max) pair of a thunder_*_min_max key, sorted, with a fallback.
void ReadRange( const WeatherPreset &weather, const char *pName, float defLo, float defHi, float &lo, float &hi ) {
    const Vector4f value = weather.get( pName, Vector4f( defLo, defHi, 0.0f, 0.0f ) );
    lo = Min( value.x, value.y );
    hi = Max( value.x, value.y );
    if ( hi <= 0.0f ) {
        lo = defLo;
        hi = defHi;
    }
}
} // namespace

bool LightningStorm::load( const std::string &islandFolder ) {
    unload();
    bForce = CmdLine::HasArg( "-lightning" );
    bLog = CmdLine::HasArg( "-logstorm" );
    if ( gpAudio == nullptr || !gpAudio->isReady() || gpFile == nullptr || CmdLine::HasArg( "-nolightning" ) ||
         CmdLine::HasArg( "-nothunder" ) ) {
        return false;
    }
    int32_t count = 0;
    for ( int32_t group = 0; group < 3; group++ ) {
        for ( const char *pName : kClipNames[group] ) {
            if ( pName == nullptr ) {
                continue;
            }
            std::vector<uint8_t> wav;
            if ( !gpFile->loadFile( ( islandFolder + "sound/ambience/" + pName + ".wav" ).c_str(), wav ) ) {
                continue;
            }
            if ( auto clip = gpAudio->loadClip( wav.data(), wav.size(), pName ) ) {
                clips[group].push_back( std::move( clip ) );
                count++;
            }
        }
    }
    OTDU_LOG_INFO( "LightningStorm: %d thunder clips (high %zu, mid %zu, low %zu)%s\n", count, clips[0].size(), clips[1].size(),
                   clips[2].size(), bForce ? ", forced" : "" );
    bLoaded = count > 0;
    return bLoaded;
}

void LightningStorm::unload() {
    for ( auto &group : clips ) {
        group.clear();
    }
    pending.clear();
    steps.clear();
    flash = 0.0f;
    bStorm = false;
    bLoaded = false;
}

float LightningStorm::random( float lo, float hi ) {
    return lo + ( hi - lo ) * std::uniform_real_distribution<float>( 0.0f, 1.0f )( rng );
}

void LightningStorm::startStrike( const WeatherPreset *pWeather, float shelter ) {
    float durationLo = 0.2f, durationHi = 0.6f, freqLo = 8.0f, freqHi = 18.0f, intensityLo = 0.5f, intensityHi = 1.0f;
    if ( pWeather != nullptr ) {
        ReadRange( *pWeather, "thunder_lightning_duration_min_max", durationLo, durationHi, durationLo, durationHi );
        ReadRange( *pWeather, "thunder_flicker_frequency_min_max", freqLo, freqHi, freqLo, freqHi );
        ReadRange( *pWeather, "thunder_flicker_intensity_min_max", intensityLo, intensityHi, intensityLo, intensityHi );
    }
    // Units of the retail keys are not decoded ([ENC] scheduler): clamp to a believable flash.
    strikeDuration = Clamp( random( durationLo, durationHi ), 0.3f, 1.2f );
    const float frequency = Clamp( random( freqLo, freqHi ), 4.0f, 30.0f );
    const float intensity = Clamp( random( intensityLo, intensityHi ), 0.6f, 1.0f );
    const int32_t count = Max( 5, static_cast<int32_t>( strikeDuration * frequency ) );
    steps.assign( static_cast<size_t>( count ), 0.0f );
    for ( int32_t i = 0; i < count; i++ ) {
        // The main stroke first, then a decaying flicker that drops out now and then.
        const float decay = 1.0f - static_cast<float>( i ) / static_cast<float>( count );
        steps[static_cast<size_t>( i )] = i == 0 ? 1.0f : ( random( 0.0f, 1.0f ) < 0.3f ? 0.0f : random( 0.3f, 1.0f ) * decay );
    }
    // Where it hit: close strikes are brighter and crack, far ones rumble.
    const float distance = random( 250.0f, 3000.0f );
    strikeBrightness = intensity * ( 1.0f - 0.3f * Clamp( distance / 3000.0f, 0.0f, 1.0f ) ) * ( 1.0f - 0.5f * shelter );
    strikeTime = 0.0f;
    strikes++;
    Thunder thunder;
    thunder.Delay = distance / kSoundSpeed;
    thunder.Volume = Clamp( 1.35f - distance / 2300.0f, 0.3f, 1.2f ) * ( 1.0f - 0.4f * shelter );
    thunder.Group = distance < 900.0f ? 0 : ( distance < 1900.0f ? 1 : 2 );
    pending.push_back( thunder );
    if ( bLog ) {
        OTDU_LOG_INFO( "LightningStorm: strike %d at %.0f m, flash %.2f for %.2f s (%d steps), thunder in %.1f s group %d\n", strikes,
                       distance, strikeBrightness, strikeDuration, count, thunder.Delay, thunder.Group );
    }
}

void LightningStorm::update( float deltaTime, const WeatherPreset *pWeather, float shelter ) {
    if ( !bLoaded ) {
        return;
    }
    const bool bWasStorm = bStorm;
    bStorm = bForce || ( pWeather != nullptr && pWeather->getFloat( "thunder_off_on", 0.0f ) > 0.5f );
    if ( bStorm && !bWasStorm ) {
        float lo = 6.0f, hi = 20.0f;
        if ( pWeather != nullptr ) {
            ReadRange( *pWeather, "thunder_time_spacing_min_max", 6.0f, 20.0f, lo, hi );
        }
        nextStrike = bForce ? 2.0f : random( Clamp( lo, 3.0f, 60.0f ) * 0.5f, Clamp( hi, 6.0f, 120.0f ) );
        if ( bLog ) {
            OTDU_LOG_INFO( "LightningStorm: storm begins, first strike in %.1f s\n", nextStrike );
        }
    }
    if ( bStorm && strikeTime >= strikeDuration ) {
        nextStrike -= deltaTime;
        if ( nextStrike <= 0.0f ) {
            startStrike( pWeather, shelter );
            float lo = 6.0f, hi = 20.0f;
            if ( pWeather != nullptr ) {
                ReadRange( *pWeather, "thunder_time_spacing_min_max", 6.0f, 20.0f, lo, hi );
            }
            if ( bForce ) {
                lo = 4.0f;
                hi = 9.0f;
            }
            nextStrike = random( Clamp( lo, 3.0f, 60.0f ), Clamp( hi, 5.0f, 120.0f ) );
        }
    }
    // The flash of the strike in progress.
    flash = 0.0f;
    if ( strikeTime < strikeDuration && !steps.empty() ) {
        strikeTime += deltaTime;
        const float u = Clamp( strikeTime / Max( strikeDuration, 1e-3f ), 0.0f, 0.999f );
        const float level = steps[static_cast<size_t>( u * static_cast<float>( steps.size() ) )];
        flash = level * strikeBrightness * ( 1.0f - 0.35f * u );
    }
    // Thunder arrives later.
    for ( size_t i = 0; i < pending.size(); ) {
        pending[i].Delay -= deltaTime;
        if ( pending[i].Delay > 0.0f ) {
            i++;
            continue;
        }
        const Thunder thunder = pending[i];
        pending.erase( pending.begin() + static_cast<std::ptrdiff_t>( i ) );
        std::vector<std::shared_ptr<AudioClip>> &pool = !clips[thunder.Group].empty() ? clips[thunder.Group]
                                                        : !clips[1].empty()            ? clips[1]
                                                                                       : clips[2];
        if ( pool.empty() || gpAudio == nullptr ) {
            continue;
        }
        int32_t index = static_cast<int32_t>( random( 0.0f, static_cast<float>( pool.size() ) - 0.001f ) );
        if ( pool.size() > 1 && index == lastClip[thunder.Group] ) {
            index = ( index + 1 ) % static_cast<int32_t>( pool.size() );
        }
        lastClip[thunder.Group] = index;
        GSAudio::VoiceParams params;
        params.Client = kLightningClient;
        params.Volume = thunder.Volume;
        params.Pitch = random( 0.92f, 1.06f );
        gpAudio->play( pool[static_cast<size_t>( index )], params );
        if ( bLog ) {
            OTDU_LOG_INFO( "LightningStorm: thunder '%s' volume %.2f\n", pool[static_cast<size_t>( index )]->Name.c_str(), thunder.Volume );
        }
    }
}

void LightningStorm::light( WeatherPreset &out, const WeatherPreset &source, float flash ) {
    out = source;
    // A cold blue-white light from the whole sky: the ambient, the fog (the distance goes white too)
    // and the sky's own sun term. The values are the preset's own, so the flash follows the weather.
    const Vector3f tint( 0.85f, 0.92f, 1.15f );
    // WeatherPreset::get() reads the active intempery's overlay first, so the lifted value goes into
    // the overlay as well (the rain_* keys of the same overlay stay as they are).
    auto set = [&]( const char *pName, const Vector4f &value ) {
        out.Values[pName] = value;
        if ( out.ActiveIntempery >= 0 && out.ActiveIntempery < static_cast<int32_t>( out.Intemperies.size() ) ) {
            out.Intemperies[out.ActiveIntempery][pName] = value;
        }
    };
    auto lift = [&]( const char *pName, float amount, const Vector3f &fallback ) {
        const Vector3f base = source.getVector3( pName, fallback );
        set( pName, Vector4f( base.x + tint.x * amount, base.y + tint.y * amount, base.z + tint.z * amount, 1.0f ) );
    };
    lift( "sun_ambient", flash * 1.6f, Vector3f( 0.3f, 0.3f, 0.3f ) );
    lift( "fog_color", flash * 0.9f, Vector3f( 0.5f, 0.5f, 0.5f ) );
    lift( "sun_diffuse", flash * 0.5f, Vector3f( 0.5f, 0.5f, 0.5f ) );
    const float sky = source.getFloat( "Skydome_sunIntensity", 0.4f );
    set( "Skydome_sunIntensity", Vector4f( sky + flash * 1.2f, 0.0f, 0.0f, 0.0f ) );
}
