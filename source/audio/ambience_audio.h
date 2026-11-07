#pragma once

#include "audio/gs_audio.h"
#include "core/math.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Island ambience beds (the ASTExport of
// islands/<island>/sound/ambience/ambiences.xmb, docs/re/ambience_audio_spec.md).
//
// The retail file lists 16 Environment records
//   Environment { TypeID; TypeName; ClassID; PriorityRank; PriorityFactor;
//                 IsTunnel; Path_Resource_Day/Night; Name_Resource_Day/Night;
//                 ReverbPresetIndex; ClipNear; ClipFar; Attenuation; Name;
//                 Path; Volume; Pitch }
// whose Name_Resource_* name a loose wav next to the xmb
// (islands/<island>/sound/ambience/<name>.wav, the same rule EdenRain uses
// for Rain[0]). Volume / Pitch are the record's gain and pitch, PriorityFactor
// ranks the zone types against each other (0 = never chosen: Airport, Tunnel),
// ClipNear / ClipFar are the record's own fade distances.
//
// Which record plays is [ENC] in the original (the ambience manager sits below
// 0x6c1000). This port picks it from what the world can tell cheaply and
// openly [INF]:
//   - under a roof / in a bore      -> keep the current bed, ducked (shelter)
//   - close to the waterline        -> Sea      (with an extra surf layer that
//                                      rides the shore distance, so driving the
//                                      coast always has waves)
//   - high above the sea plane      -> Mountain, then Cliff on the way down
//   - cars around                   -> City (many) / Residential (a few)
//   - otherwise                     -> Plain
// The picked record crossfades over kFadeTime; day / night come from the game
// clock (7 -> 20.5 is day) and fall back to the day clip when the file has no
// night one. -ambience <name> forces a record, -noambience turns the system
// off, -logambience prints every switch.
class AmbienceAudio {
public:
    // Environment TypeID values of ambiences.xmb (the TypeName order of the
    // 16 records; the ids are the file's own).
    enum eEnvironment {
        E_Empty = 0,
        E_Residential,
        E_Industrial,
        E_Plain,
        E_Airport,
        E_Sea,
        E_Cliff,
        E_Forest1,
        E_Forest2,
        E_Salines,
        E_Mountain,
        E_Desert,
        E_Tunnel,
        E_Harbor,
        E_River,
        E_City,
        E_Count
    };

    // World state of the frame the bed is chosen from.
    struct Context {
        Vector3f Position;
        float Hour = 12.0f;        // game clock, 0..24
        float GroundHeight = 0.0f; // terrain height under the listener
        float WaterHeight = 0.0f;  // sea plane height of the island
        bool bGroundValid = true;  // false: over water / outside the heightfield
        float Shelter = 0.0f;      // 0 open sky .. 1 under a roof (EdenWorld::isSheltered)
        int32_t NearbyCars = 0;    // live traffic agents within the city radius
    };

    using HeightQuery = std::function<bool( float x, float z, float &outHeight )>;

    AmbienceAudio() = default;
    ~AmbienceAudio() { unload(); }
    AmbienceAudio( const AmbienceAudio & ) = delete;
    AmbienceAudio &operator=( const AmbienceAudio & ) = delete;

    // islandFolder: "<bank folder>islands/<island>/" (a trailing slash is
    // optional). Reads the xmb; the wavs themselves are loaded lazily by the
    // record actually reached, so only the beds of the island are decoded.
    bool load( const std::string &islandFolder );
    void unload();

    // Heightfield probe (EdenWorld::getHeight). Without it the shore distance
    // stays unknown and Sea is never picked.
    inline void setHeightQuery( HeightQuery query ) { heightQuery = std::move( query ); }

    void update( const Context &context, float deltaTime );
    void stop();

    // -ambience <TypeName|TypeID>: force one record (>= E_Count clears it).
    void setEnvironmentOverride( const std::string &name );
    void clearEnvironmentOverride() { overrideEnvironment = -1; }

    inline bool isLoaded() const { return !environments.empty(); }
    inline int32_t getEnvironment() const { return currentEnvironment; }
    inline int32_t getPendingEnvironment() const { return pendingEnvironment; }
    const char *getEnvironmentName() const;
    // Shore distance used by the last pick (m, < 0 = unknown) and the level of
    // the bed (0 = silent, ~1 = the record's own volume).
    inline float getShoreDistance() const { return shoreDistance; }
    inline float getLevel() const { return level; }
    inline size_t getRecordCount() const { return environments.size(); }

private:
    struct Environment {
        int32_t TypeId = 0;
        std::string Name;
        int32_t ClassId = 0;
        float PriorityFactor = 0.0f;
        float ClipNear = 5.0f;
        float ClipFar = 500.0f;
        float Volume = 1.0f;
        float Pitch = 1.0f;
        std::string DayName;
        std::string NightName;
        std::shared_ptr<AudioClip> Day;
        std::shared_ptr<AudioClip> Night;
        bool bTriedDay = false;
        bool bTriedNight = false;
    };

    // One playing bed with its own crossfade ramp. The mixer ramps volume
    // changes over its callback buffer, but an explicit ramp keeps the two
    // sides of a crossfade balanced.
    struct Layer {
        GSAudio::VoiceHandle Voice = GSAudio::kInvalidVoice;
        int32_t Environment = -1;
        bool bNight = false;
        float Level = 0.0f;  // current gain
        float Target = 0.0f; // gain the ramp is heading to
        bool bSurf = false;  // the shore layer (always the Sea record)
    };

    std::shared_ptr<AudioClip> loadEnvironment( int32_t index, bool bNight );
    void startLayer( Layer &layer, int32_t environment, bool bNight, float level );
    void fadeLayer( Layer &layer, float target, float deltaTime );
    void stopLayer( Layer &layer );
    int32_t pickEnvironment( const Context &context );
    int32_t findEnvironment( const char *pName ) const;
    // Smallest distance (m) at which the heightfield drops to the water plane
    // in one of 8 directions, -1 when no water is within kShoreSearch.
    float probeShoreDistance( const Vector3f &position, float waterHeight );
    void updateSurf( const Context &context, float deltaTime );
    void updateBeds( const Context &context, float deltaTime );

    std::vector<Environment> environments;
    std::string folder;
    HeightQuery heightQuery;
    int32_t overrideEnvironment = -1;

    Layer beds[2]; // [0] the current bed, [1] the one fading out
    Layer surf;
    bool bSurfLoaded = false;

    int32_t currentEnvironment = -1;
    int32_t pendingEnvironment = -1;
    float level = 0.0f;
    float shoreDistance = -1.0f;
    float shoreTimer = 0.0f;
    bool bNight = false;
    int32_t loggedEnvironment = -2;
};
