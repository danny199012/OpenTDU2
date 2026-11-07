#include "shared.h"
#include "audio/ambience_audio.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace {
// bfx client of the 2D stereo island bed (kBfxClientNames index 10,
// "AmbienceInternal": the audiosetup.xmb pipeline of the same name has 2
// inputs, so the mixer routes it straight to the front pair).
constexpr int32_t kAmbienceClient = 10;

// Crossfade between two beds (s).
constexpr float kFadeTime = 2.5f;
// Below / above these heights over the sea plane the mountain beds take over.
constexpr float kCliffHeight = 70.0f;
constexpr float kMountainHeight = 160.0f;
// Shore distance (m) the Sea bed needs to come in, and the one it leaves at
// (the difference is the hysteresis that stops a bed flapping on a beach).
constexpr float kSeaEnter = 140.0f;
constexpr float kSeaLeave = 210.0f;
// Surf layer: full level at the waterline, silent at kSurfReach.
constexpr float kSurfFull = 25.0f;
constexpr float kSurfReach = 320.0f;
constexpr float kSurfLevel = 0.55f;
// Shore probe: 8 directions, 1 sample every kShoreStep up to kShoreSearch.
constexpr float kShoreStep = 15.0f;
constexpr float kShoreSearch = 480.0f;
constexpr float kShorePeriod = 0.6f; // s between two probes
// Cars close to the listener that turn the bed into a city one.
constexpr int32_t kCityCars = 3;
constexpr int32_t kResidentialCars = 1;

// Day window of the game clock (the retail day / night split of the beds is
// not in the data; 7 -> 20.5 matches the street lamp rule).
constexpr float kDayStart = 7.0f;
constexpr float kDayEnd = 20.5f;

bool EnvFlag( const char *pName, bool bDefault ) {
    const char *pValue = SDL_getenv( pName );
    if ( pValue == nullptr || pValue[0] == '\0' ) {
        return bDefault;
    }
    return pValue[0] != '0';
}

// The resource name of a record ("A_DaySandCoast") as a file name
// ("a_daysandcoast.wav"): lower case, no folder, no extension.
std::string ResourceFile( const std::string &resource ) {
    std::string name = ToLower( resource );
    const size_t slash = name.find_last_of( "\\/" );
    if ( slash != std::string::npos ) {
        name = name.substr( slash + 1 );
    }
    const size_t dot = name.rfind( '.' );
    if ( dot != std::string::npos ) {
        name = name.substr( 0, dot );
    }
    return name;
}
} // namespace

bool AmbienceAudio::load( const std::string &islandFolder ) {
    unload();
    if ( gpFile == nullptr ) {
        return false;
    }
    folder = islandFolder;
    if ( !folder.empty() && folder.back() != '/' && folder.back() != '\\' ) {
        folder += '/';
    }
    std::vector<uint8_t> buffer;
    const std::string xmbPath = folder + "sound/ambience/ambiences.xmb";
    if ( !gpFile->loadFile( xmbPath.c_str(), buffer ) ) {
        OTDU_LOG_WARN( "AmbienceAudio: '%s' not found\n", xmbPath.c_str() );
        return false;
    }
    XmbDocument document;
    if ( !document.load( buffer ) ) {
        OTDU_LOG_WARN( "AmbienceAudio: '%s' not parsed\n", xmbPath.c_str() );
        return false;
    }
    XmbValue root = document.getRoot();
    const uint32_t count = root.getArraySize( "Environments" );
    for ( uint32_t i = 0; i < count; i++ ) {
        XmbValue entry = root.getArrayElement( "Environments", i );
        Environment environment;
        environment.TypeId = static_cast<int32_t>( entry["TypeID"].asInt() );
        environment.Name = entry["TypeName"].asString();
        environment.ClassId = static_cast<int32_t>( entry["ClassID"].asInt() );
        environment.PriorityFactor = entry["PriorityFactor"].asFloat( 0.0f );
        environment.ClipNear = entry["ClipNear"].asFloat( 5.0f );
        environment.ClipFar = entry["ClipFar"].asFloat( 500.0f );
        environment.Volume = entry["Volume"].asFloat( 1.0f );
        environment.Pitch = entry["Pitch"].asFloat( 1.0f );
        environment.DayName = entry["Name_Resource_Day"].asString();
        environment.NightName = entry["Name_Resource_Night"].asString();
        if ( environment.Name.empty() ) {
            environment.Name = "Environment" + std::to_string( environment.TypeId );
        }
        environments.push_back( std::move( environment ) );
    }
    if ( environments.empty() ) {
        OTDU_LOG_WARN( "AmbienceAudio: '%s' has no Environment record\n", xmbPath.c_str() );
        return false;
    }
    // -ambience <name>: force one record (a test / a mod hook).
    if ( const char *pName = CmdLine::GetArgValue( "-ambience" ) ) {
        setEnvironmentOverride( pName );
    }
    OTDU_LOG_INFO( "AmbienceAudio: '%s': %zu records (beds in '%ssound/ambience/')\n", xmbPath.c_str(),
                   environments.size(), folder.c_str() );
    return true;
}

void AmbienceAudio::unload() {
    stop();
    environments.clear();
    folder.clear();
    bSurfLoaded = false;
    currentEnvironment = -1;
    pendingEnvironment = -1;
    level = 0.0f;
    shoreDistance = -1.0f;
    shoreTimer = 0.0f;
    loggedEnvironment = -2;
}

void AmbienceAudio::stop() {
    for ( Layer &layer : beds ) {
        stopLayer( layer );
    }
    stopLayer( surf );
}

void AmbienceAudio::stopLayer( Layer &layer ) {
    if ( gpAudio != nullptr && layer.Voice != GSAudio::kInvalidVoice ) {
        gpAudio->stop( layer.Voice );
    }
    layer.Voice = GSAudio::kInvalidVoice;
    layer.Environment = -1;
    layer.Level = 0.0f;
    layer.Target = 0.0f;
}


int32_t AmbienceAudio::findEnvironment( const char *pName ) const {
    if ( pName == nullptr ) {
        return -1;
    }
    // A number accepts the TypeID, a name the TypeName (case insensitive).
    if ( pName[0] >= '0' && pName[0] <= '9' ) {
        const int32_t id = atoi( pName );
        for ( size_t i = 0; i < environments.size(); i++ ) {
            if ( environments[i].TypeId == id ) {
                return static_cast<int32_t>( i );
            }
        }
        return -1;
    }
    for ( size_t i = 0; i < environments.size(); i++ ) {
        if ( StringCmpI( environments[i].Name.c_str(), pName ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

void AmbienceAudio::setEnvironmentOverride( const std::string &name ) {
    const int32_t index = findEnvironment( name.c_str() );
    if ( index < 0 ) {
        OTDU_LOG_WARN( "AmbienceAudio: -ambience '%s' is not a record of the island\n", name.c_str() );
        overrideEnvironment = -1;
        return;
    }
    overrideEnvironment = index;
    OTDU_LOG_INFO( "AmbienceAudio: forced bed '%s' (%d)\n", environments[index].Name.c_str(),
                   environments[index].TypeId );
}

const char *AmbienceAudio::getEnvironmentName() const {
    if ( currentEnvironment < 0 || static_cast<size_t>( currentEnvironment ) >= environments.size() ) {
        return "(none)";
    }
    return environments[static_cast<size_t>( currentEnvironment )].Name.c_str();
}

std::shared_ptr<AudioClip> AmbienceAudio::loadEnvironment( int32_t index, bool night ) {
    if ( index < 0 || static_cast<size_t>( index ) >= environments.size() || gpFile == nullptr ||
         gpAudio == nullptr ) {
        return nullptr;
    }
    Environment &environment = environments[static_cast<size_t>( index )];
    // Day first: it is the fallback of a record without a night bed.
    if ( !environment.bTriedDay && !environment.DayName.empty() ) {
        environment.bTriedDay = true;
        std::vector<uint8_t> wav;
        const std::string path = folder + "sound/ambience/" + ResourceFile( environment.DayName ) + ".wav";
        if ( gpFile->loadFile( path.c_str(), wav ) ) {
            environment.Day = gpAudio->loadClip( wav.data(), wav.size(), environment.DayName.c_str() );
        } else {
            OTDU_LOG_WARN( "AmbienceAudio: '%s' not found (%s)\n", path.c_str(), environment.Name.c_str() );
        }
    }
    if ( night && !environment.bTriedNight && !environment.NightName.empty() ) {
        environment.bTriedNight = true;
        std::vector<uint8_t> wav;
        const std::string path = folder + "sound/ambience/" + ResourceFile( environment.NightName ) + ".wav";
        if ( gpFile->loadFile( path.c_str(), wav ) ) {
            environment.Night = gpAudio->loadClip( wav.data(), wav.size(), environment.NightName.c_str() );
        } else {
            OTDU_LOG_WARN( "AmbienceAudio: '%s' not found (%s)\n", path.c_str(), environment.Name.c_str() );
        }
    }
    if ( night && environment.Night != nullptr ) {
        return environment.Night;
    }
    return environment.Day;
}



void AmbienceAudio::startLayer( Layer &layer, int32_t index, bool night, float startLevel ) {
    if ( gpAudio == nullptr ) {
        return;
    }
    const std::shared_ptr<AudioClip> clip = loadEnvironment( index, night );
    if ( clip == nullptr ) {
        // The record has no (readable) bed: nothing plays, the current
        // environment still updates so the shore / log state stays right.
        return;
    }
    const Environment &environment = environments[static_cast<size_t>( index )];
    GSAudio::VoiceParams params;
    params.Client = kAmbienceClient; // 2D stereo bed
    params.Volume = startLevel;
    params.Pitch = environment.Pitch > 0.05f && environment.Pitch < 4.0f ? environment.Pitch : 1.0f;
    params.bLoop = true;
    layer.Voice = gpAudio->play( clip, params );
    layer.Environment = index;
    layer.bNight = night;
    layer.Level = startLevel;
    layer.Target = startLevel;
}

void AmbienceAudio::fadeLayer( Layer &layer, float target, float deltaTime ) {
    if ( layer.Voice == GSAudio::kInvalidVoice ) {
        return;
    }
    layer.Target = target;
    const float step = deltaTime / kFadeTime;
    if ( layer.Level < target ) {
        layer.Level = Min( layer.Level + step, target );
    } else if ( layer.Level > target ) {
        layer.Level = Max( layer.Level - step, target );
    }
    if ( gpAudio != nullptr ) {
        gpAudio->setVolume( layer.Voice, layer.Level );
    }
    if ( layer.Level <= 0.0f && target <= 0.0f ) {
        stopLayer( layer );
    }
}

// The record the world says the listener is in. Hysteresis: the Sea bed keeps
// its slot until the shore is clearly out of reach [INF].
int32_t AmbienceAudio::pickEnvironment( const Context &context ) {
    if ( overrideEnvironment >= 0 ) {
        return overrideEnvironment;
    }
    auto indexOf = [this]( int32_t typeId ) {
        for ( size_t i = 0; i < environments.size(); i++ ) {
            if ( environments[i].TypeId == typeId ) {
                return static_cast<int32_t>( i );
            }
        }
        return -1;
    };
    const bool bWasSea = currentEnvironment >= 0 &&
                         environments[static_cast<size_t>( currentEnvironment )].TypeId == E_Sea;
    if ( shoreDistance >= 0.0f && shoreDistance < ( bWasSea ? kSeaLeave : kSeaEnter ) ) {
        const int32_t sea = indexOf( E_Sea );
        if ( sea >= 0 ) {
            return sea;
        }
    }
    if ( !bWasSea && shoreDistance < 0.0f && !context.bGroundValid ) {
        // Over open water without a probe (no height query): the Sea bed.
        const int32_t sea = indexOf( E_Sea );
        if ( sea >= 0 ) {
            return sea;
        }
    }
    if ( context.bGroundValid ) {
        const float altitude = context.GroundHeight - context.WaterHeight;
        if ( altitude > kMountainHeight ) {
            const int32_t mountain = indexOf( E_Mountain );
            if ( mountain >= 0 ) {
                return mountain;
            }
        }
        if ( altitude > kCliffHeight ) {
            const int32_t cliff = indexOf( E_Cliff );
            if ( cliff >= 0 ) {
                return cliff;
            }
        }
    }
    if ( context.NearbyCars >= kCityCars ) {
        const int32_t city = indexOf( E_City );
        if ( city >= 0 ) {
            return city;
        }
    }
    if ( context.NearbyCars >= kResidentialCars ) {
        const int32_t residential = indexOf( E_Residential );
        if ( residential >= 0 ) {
            return residential;
        }
    }
    const int32_t plain = indexOf( E_Plain );
    if ( plain >= 0 ) {
        return plain;
    }
    // Island without a Plain record: the first record that has a bed.
    for ( size_t i = 0; i < environments.size(); i++ ) {
        if ( !environments[i].DayName.empty() ) {
            return static_cast<int32_t>( i );
        }
    }
    return environments.empty() ? -1 : 0;
}

float AmbienceAudio::probeShoreDistance( const Vector3f &position, float waterHeight ) {
    if ( !heightQuery ) {
        return -1.0f;
    }
    // 8 directions, outward: the first sample at or below the water plane is
    // the shore of that direction. The smallest of the 8 wins (the nearest
    // water, which is what a beach / harbour needs).
    static constexpr float kRing[8][2] = { { 1.0f, 0.0f },  { 0.7071f, 0.7071f },  { 0.0f, 1.0f },
                                           { -0.7071f, 0.7071f }, { -1.0f, 0.0f }, { -0.7071f, -0.7071f },
                                           { 0.0f, -1.0f }, { 0.7071f, -0.7071f } };
    float nearest = -1.0f;
    for ( const auto &direction : kRing ) {
        for ( float distance = kShoreStep; distance <= kShoreSearch; distance += kShoreStep ) {
            const float x = position.x + direction[0] * distance;
            const float z = position.z + direction[1] * distance;
            float height = 0.0f;
            if ( !heightQuery( x, z, height ) ) {
                // Outside the streamed heightfield: open water as far as this
                // port can tell.
                if ( nearest < 0.0f || distance < nearest ) {
                    nearest = distance;
                }
                break;
            }
            if ( height <= waterHeight + 0.7f ) {
                if ( nearest < 0.0f || distance < nearest ) {
                    nearest = distance;
                }
                break;
            }
        }
    }
    return nearest;
}


void AmbienceAudio::updateBeds( const Context &context, float deltaTime ) {
    const int32_t wanted = pickEnvironment( context );
    if ( wanted < 0 ) {
        return;
    }
    // A switch (another record, or the same one going day <-> night) fades the
    // old bed out in slot 1 while the new one comes in on slot 0.
    const bool bChanged = wanted != currentEnvironment ||
                          ( beds[0].Voice != GSAudio::kInvalidVoice && beds[0].bNight != bNight );
    if ( bChanged ) {
        stopLayer( beds[1] );
        beds[1] = beds[0];
        beds[1].Target = 0.0f;
        beds[0] = Layer();
        startLayer( beds[0], wanted, bNight, 0.0f );
        currentEnvironment = wanted;
        pendingEnvironment = -1;
        if ( CmdLine::HasArg( "-logambience" ) || loggedEnvironment != wanted ) {
            loggedEnvironment = wanted;
            OTDU_LOG_INFO( "AmbienceAudio: bed '%s' (%s, shore %.0f m, %d cars, ground %.0f m)\n",
                           environments[static_cast<size_t>( wanted )].Name.c_str(),
                           bNight ? "night" : "day", shoreDistance, context.NearbyCars, context.GroundHeight );
        }
    }
    const Environment &environment = environments[static_cast<size_t>( currentEnvironment )];
    // Under a roof the island bed is muffled, not cut (a tunnel keeps the
    // storm / the sea through the walls).
    const float duck = 1.0f - 0.6f * Clamp( context.Shelter, 0.0f, 1.0f );
    level = Clamp( environment.Volume, 0.0f, 2.0f ) * duck;
    fadeLayer( beds[0], level, deltaTime );
    fadeLayer( beds[1], 0.0f, deltaTime );
}

// The shore layer: the Sea record played under any other bed when the listener
// is within kSurfReach of the water, so a coastal road always has waves. When
// the Sea bed is the active one the layer stays silent (it would be the same
// clip twice, and two identical loops comb-filter).
void AmbienceAudio::updateSurf( const Context &context, float deltaTime ) {
    float target = 0.0f;
    const bool bSeaBed = currentEnvironment >= 0 &&
                         environments[static_cast<size_t>( currentEnvironment )].TypeId == E_Sea;
    if ( !bSeaBed && shoreDistance >= 0.0f && shoreDistance < kSurfReach ) {
        int32_t seaIndex = -1;
        for ( size_t i = 0; i < environments.size(); i++ ) {
            if ( environments[i].TypeId == E_Sea ) {
                seaIndex = static_cast<int32_t>( i );
                break;
            }
        }
        if ( seaIndex >= 0 ) {
            const float t = Saturate( ( kSurfReach - shoreDistance ) / ( kSurfReach - kSurfFull ) );
            target = kSurfLevel * t * Clamp( environments[static_cast<size_t>( seaIndex )].Volume, 0.0f, 2.0f );
            if ( surf.Voice == GSAudio::kInvalidVoice && gpAudio != nullptr ) {
                startLayer( surf, seaIndex, bNight, 0.0f );
                surf.bSurf = true;
                if ( CmdLine::HasArg( "-logambience" ) ) {
                    OTDU_LOG_INFO( "AmbienceAudio: shore layer in at %.0f m (bed '%s')\n", shoreDistance,
                                   getEnvironmentName() );
                }
            }
        }
    }
    (void)context;
    if ( surf.Voice != GSAudio::kInvalidVoice ) {
        fadeLayer( surf, target, deltaTime );
    }
}

void AmbienceAudio::update( const Context &context, float deltaTime ) {
    static const bool bDisabled = CmdLine::HasArg( "-noambience" ) || !EnvFlag( "OTDU_AMBIENCE", true );
    if ( environments.empty() || gpAudio == nullptr || bDisabled ) {
        return;
    }
    if ( deltaTime <= 0.0f ) {
        return;
    }
    bNight = context.Hour < kDayStart || context.Hour >= kDayEnd;
    // The shore probe is the expensive part (a few hundred height samples):
    // every kShorePeriod is plenty for a drive.
    shoreTimer -= deltaTime;
    if ( shoreTimer <= 0.0f ) {
        shoreTimer = kShorePeriod;
        shoreDistance = context.bGroundValid ? probeShoreDistance( context.Position, context.WaterHeight ) : -1.0f;
    }
    updateBeds( context, deltaTime );
    updateSurf( context, deltaTime );
}

