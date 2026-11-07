#pragma once

#include "game/spots/spot_exterior.h"
#include "render/eden/eden_scene_view.h"

// The buildings of an island: one spot record per building of
// islands/<island>/level/spots.bin and the exterior banks
// islands/<island>/exterior/<bank>.bnk that hold their models.
//
// The retail game streams these around the player, so a dealership / shop /
// house is a real building; OpenTDU2 first used the list only for the map
// icons (MapScreen) and the world viewer (free-fly tool). FREERIDE now streams
// them around the car, which is what puts the buildings on the plots.
//
// [DATA] spots.bin: ".SPT", "SPTA", u32 size at +0x14, then 0x110-byte records
// starting at +0x20: u64 id, char bank[8] at +0x08, vec3 position at +0x10,
// f32 +0x1c, f32 heading at +0x20, the category byte at +0x100.
struct SpotRecord {
    uint64_t Id = 0;
    std::string Bank;     // building bank name (ECD_FERR, ETUN_A, EPH_L3DI...)
    Vector3f Position;
    float Heading = 0.0f; // radians, the frame is T(position) R_y(heading)
    // islands/<island>/exterior/<bank>.bnk when the file exists (several
    // spots of the same building type share one bank).
    std::string ExteriorPath;
    // Filled by the world viewer only (its spot panel probes the sector banks
    // around each spot to pick a start point).
    uint32_t NumSectors = 0;
    bool bHasSector = false;
};

// Reads spots.bin of an island (empty when the file is missing).
std::vector<SpotRecord> LoadSpotList( const std::string &island );

// The streamed set of spot exteriors around a position. The distances are the
// world viewer's developer-tool values [INF]: a bank is loaded within 1500 m
// and dropped beyond 2000 m (hysteresis), so driving across an island visits
// the buildings without ever holding more than the nearby ones.
class SpotExteriors {
public:
    SpotExteriors() = default;
    ~SpotExteriors() { unload(); }

    // Parses the island's spot list; the banks themselves load on demand.
    bool load( const std::string &island );
    // Takes an already parsed list (the world viewer fills the sector coverage
    // fields before handing it over).
    void setSpots( std::vector<SpotRecord> list );
    // A bank the mode already owns and draws (FREERIDE's house cinematic bank
    // eph_l3di.bnk): never streamed. The texture cache owner and the lightmap
    // entries of that bank belong to that other owner, and releasing the shared
    // owner would pull the textures from under it.
    void excludeBank( const std::string &bankPath );
    // Drops every loaded bank (call before the view / renderer is destroyed).
    void unload();
    inline const std::vector<SpotRecord> &getSpots() const { return spots; }
    inline size_t getNumBanks() const { return exteriors.size(); }
    inline const std::vector<std::unique_ptr<SpotExterior>> &getExteriors() const { return exteriors; }
    inline bool isLoaded() const { return !spots.empty(); }

    // Streams the banks around 'position'. bEnabled false drops them all
    // (-noexteriors). The list is only re-evaluated when the position moved
    // more than kRetestDistance, unless the enable state changed or a previous
    // call hit kMaxLoadsPerTick (a dense area loads over several frames).
    // Returns true when a bank was dropped: the view's lightmap list is cleared
    // and the streamer's own models re-registered, so a caller that registered
    // lightmapped models of its own must re-register them too.
    bool tick( EdenSceneView &view, const Vector3f &position, bool bEnabled );
    // Draws every loaded bank at its spot frames (call from the scene view's
    // draw callback, like the world viewer).
    void draw( EdenRenderer &renderer ) const;

    static constexpr float kLoadDistance = 1500.0f;   // [INF] world viewer value
    static constexpr float kUnloadDistance = 2000.0f; // [INF] world viewer value
    static constexpr float kRetestDistance = 10.0f;   // [INF] streaming granularity
    // Banks loaded per tick: a dense town can have dozens of spots inside
    // kLoadDistance, and loading them all in one frame stalls for seconds
    // (measured on the map view, where the radius is the whole 3 km island
    // view). The rest load on the following frames.
    static constexpr int32_t kMaxLoadsPerTick = 2;

private:
    std::vector<SpotRecord> spots;
    std::vector<std::string> excluded;
    std::vector<std::unique_ptr<SpotExterior>> exteriors;
    Vector3f lastPosition;
    bool bHasTicked = false;
    bool bLastEnabled = true;
    bool bPendingLoads = false;
};
