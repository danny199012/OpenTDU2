#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_neons.h"
#include "world/pmi.h"
#include "world/sign_text.h"

#include <functional>

// Island streaming data (Euro/Bnk/islands/<island>/level), rendered with the
// original shaders.
//
// Layout (worldconfig.cpr + FUN_00990020 in TestDrive2.exe):
//   - areaColumns x areaRows areas of 8 x 8 sectors, a sector is 768 m wide
//     (0x300). Area (ax, ay) covers [8 ax 768, 8 (ax + 1) 768] on X and the
//     same with ay on Z.
//   - Sector (ax, ay, sx, sy) corner: X = (8 ax + sx) 768 (+ 384 when sy is
//     odd: the rows are staggered like bricks), Z = (8 ay + sy) 768.
//     Path "Area-%d\Area-%d-%d\Sector-%d-%d-%d-%d.bnk".
//   - Sector bank: heightfield (sector-*.3dg: 257 x 257 heights, 3 m
//     spacing, R32 height + half4 normal, no index buffer, TD_HeightMap),
//     color / shadow maps (-c / -s .2db), objects (-o .3dd/.3dg/.2dm, sector
//     local), instance lists (.pmi), roads, collision...
//   - Area bank: low resolution heightfield (24 m spacing) + far objects.
//   - commonworld.bnk: shared models/materials/textures (library.*).
class EdenWorld {
public:
    static constexpr float kSectorSize = 768.0f;
    static constexpr uint32_t kSectorsPerArea = 8;
    static constexpr uint32_t kHeightfieldSize = 257;

    struct SectorId {
        int32_t AreaX, AreaY, X, Y;
        bool operator==( const SectorId &o ) const {
            return AreaX == o.AreaX && AreaY == o.AreaY && X == o.X && Y == o.Y;
        }
    };

public:
    EdenWorld();
    ~EdenWorld();

    bool initialize( EdenRenderer &renderer, const char *pIsland );
    void destroy();

    // Loads (synchronously) the sectors around a position and every area;
    // true when a sector was loaded. maxSectors caps the loads of one call
    // (0 = all): the world map calls it every frame with a small budget, so
    // panning spreads the square over frames instead of loading it in one.
    bool loadAround( const Vector3f &position, int32_t sectorRadius, int32_t maxSectors = 0 );
    // Load-ahead (2026-11-04, the streaming hitch): loads at most ONE sector of
    // the square of sectorRadius around 'position' that is not loaded yet, the
    // one nearest 'ahead' first, and keeps it hidden (not drawn) until a
    // loadAround square covers it - so the drawn set is exactly the one the
    // synchronous streaming would have, it just costs nothing at the border.
    // True when a sector was loaded.
    bool preloadOne( const Vector3f &position, const Vector3f &ahead, int32_t sectorRadius );
    // Queues the sector banks of the square in the file cache's background
    // reader (GSFile::prefetchFile).
    void prefetchAround( const Vector3f &position, int32_t sectorRadius );
    // Changes whenever the DRAWN sector set changes (a sector shown, revealed or
    // unloaded): the occlusion grid of an older set is not trusted (the coarse
    // area terrain a new sector replaces was still in its depth).
    inline uint32_t getDrawEpoch() const { return drawEpoch; }
    // Called when a sector / area object model has just loaded or is about to
    // be destroyed. EdenSceneView uses it to keep the NIGHTLMAP material list
    // pointing at live streamed buildings (register / unregister).
    using ModelCallback = std::function<void( EdenModel & )>;
    void setModelCallbacks( ModelCallback loaded, ModelCallback unloaded ) {
        modelLoadedCallback = std::move( loaded );
        modelUnloadedCallback = std::move( unloaded );
    }
    // Unloads the sectors farther than sectorRadius (sector rows / columns)
    // from a position; returns the number of sectors unloaded. The caller
    // must drop the renderer material caches (clearShadowCasterCache).
    // maxSectors caps the unloads of one call (0 = all), like loadAround.
    uint32_t unloadFar( const Vector3f &position, int32_t sectorRadius, int32_t maxSectors = 0 );
    // Same, keeping the sectors near either position (a camera and the point
    // it is heading to: a preloaded sector is not dropped the next frame).
    uint32_t unloadFar( const Vector3f &position, const Vector3f &ahead, int32_t sectorRadius,
                        int32_t maxSectors = 0 );
    // Whole-island survey (-scantunnels, headless): every sector bank is
    // loaded one at a time, the scene nodes named like a tunnel / bridge are
    // picked out and the sector heightfield is sampled inside their
    // footprint: the tunnel-mouths-filled-with-terrain class (docs/STATE.md
    // session 42 - the heightfield crosses the object's volume) reported per
    // node with the sample counts. The sector is freed again after its scan
    // (the spawn ring's sectors stay), so the resident set does not grow.
    void scanTunnelTerrain();
    // Island level heightfield (<island>.bnk <island>.3dd/.3dg/.2dm, 257 x
    // 257 heights, 384 m spacing: HMAP center 49152 = 128 x 384 [DATA],
    // TD_HeightMap S_USERTYPE4 = 2), drawn where no area is loaded. Off by
    // default (the world viewer turns it on).
    inline void setIslandTerrainEnabled( bool bEnable ) { bIslandTerrainEnabled = bEnable; }
    // The island area tiles (terrain + area objects). loadAround loads every
    // area whatever the sector radius, so a closed room in its own frame (the
    // casino rooms, the intro garage) drew the island's ground through its
    // floor; IntroCinematic turns them off when the scene has no island.
    inline void setAreasEnabled( bool bEnable ) { bAreasEnabled = bEnable; }
    void draw( EdenRenderer &renderer, const Vector3f &cameraPosition );
    // Small instance culling (size / distance below the ratio are skipped):
    // low resolution passes (environment cube faces). 0: off.
    inline void setMinInstanceRatio( float ratio ) { minInstanceRatio = ratio; }
    // Ocean on / off (island ocean, the TD_Ocean nodes of the areas and
    // sectors): closed rooms rendered without the island.
    void setOceanVisible( bool bVisible ) {
        bOceanVisible = bVisible;
        bOceanVisibilityDirty = true;
    }
    inline bool isOceanVisible() const { return bOceanVisible; }
    // OpenTDU2 shore swell (docs/re/ocean_spec.md 11.4): vertical offset (m)
    // applied to every TD_Ocean item at draw. The ocean levels are coplanar
    // at the island's water plane (0 on Ibiza / Casino, -1.8 on Hawai), so
    // one offset moves the whole sea and the waterline with it.
    inline void setSeaLevelOffset( float offset ) { seaLevelOffset = offset; }
    inline float getSeaLevelOffset() const { return seaLevelOffset; }
    // TD_Ocean materials of the loaded levels: kind 0 sector, 1 area, 2
    // island (<island>-o), with the level colour map (heightmap COLOR).
    void forEachOceanMaterial( const std::function<void( EdenMaterial &material, uint8_t kind,
                                                         GPUTexture *pColorMap )> &callback );
    // Wet ground (WFX_WetGrd, FUN_00aa0b60 / FUN_00aa0890): rain permutation
    // of the TD_Road materials (S_USERTYPE9 = 1, slots 6 ripples, 7 the
    // dynamic environment cube, 8 PUDDLES) and RainSpecs / RainRippleSpecs
    // from the weather. pEnvironmentCube nullptr or no wet weather: dry.
    void updateWetGround( EdenRenderer &renderer, const struct WeatherPreset *pWeather, GPUTexture *pEnvironmentCube,
                          float deltaTime );
    // World neon set + the .pmi blink states (FUN_009834d0): the shared /
    // sector / area neon materials get WeatherEmissive = building_neon_strength
    // and the blink AnimValues advance. -noworldneons disables registration
    // and update.
    void updateNeons( const struct WeatherPreset *pWeather, float deltaTime );
    // The street lamp glow (the library SF_Ligh* models' TD_Lamp items) draws
    // only at night (the weather hour rule of the car lights; -lampday keeps
    // them always on). Default off = lamps dark until a game mode says night.
    void setNightLights( bool bNight ) { bNightLights = bNight; }

    static SectorId getSectorAt( const Vector3f &position );
    static Vector3f getSectorOrigin( const SectorId &id );
    // -atsign (gm_freeride): scans the island's sector banks for the first
    // direction-sign actor (RS_Dir*, the text faces of sign_text.cpp) and
    // returns its position plus the yaw facing it. One-shot (loads a few
    // sector banks); false when the island has none.
    static bool findSignSpawn( const char *pIsland, Vector3f &outPosition, float &outYaw );
    // Heightfield sampling (loaded sectors only), returns false outside.
    bool getHeight( float x, float z, float &outHeight ) const;
    // The ground the drive physics stands on: the heightfield, except the
    // cells carved out of it (inside a tunnel / bridge volume, MarkCarvedCells)
    // return the topmost up-facing surface of the carving nodes' meshes below
    // fromY - the bore floor the sheet used to hide (the car rode that sheet
    // up like a ramp, session 56's carve was render-only). fromY = the query
    // origin (wheel hub / chassis centre).
    bool getGroundHeight( float x, float z, float fromY, float &outHeight ) const;
    // Highest surface (heightfield or sector objects: roads...) below
    // maxHeight.
    bool getSurfaceHeight( float x, float z, float maxHeight, float &outHeight ) const;
    // getSurfaceHeight scoped to the containing sector: the same answer where
    // the point is (a road triangle at (x, z) belongs to that sector's
    // objects), but one surface-grid lookup instead of a walk over every
    // loaded sector - the light pools query 4 corners per cone per frame.
    // headroom: how far above 'fromY' (the query origin) a surface still
    // counts (the road ahead of a climbing car sits above it).
    bool getDriveHeight( float x, float z, float fromY, float headroom, float &outHeight ) const;
    // True when a surface hangs over (x, z, fromY) within maxDist: the inside
    // of a tunnel bore (a carved cell) or anything the sector objects put over
    // the point (a building's floor / overhang, a covered passage). The falling
    // rain fades out there instead of clipping through the roof (EdenRain).
    bool isSheltered( float x, float z, float fromY, float maxDist ) const;

    inline const Render2DM &getLibraryMaterials() const { return libraryMaterials; }
    inline int32_t getAreaColumns() const { return areaColumns; }
    inline int32_t getAreaRows() const { return areaRows; }
    inline size_t getNumSectors() const { return sectors.size(); }
    inline size_t getNumAreas() const { return areas.size(); }
    inline size_t getNumFailedSectors() const { return failedSectors.size(); }
    inline bool hasIslandTerrain() const { return !islandTerrain.Materials.empty(); }
    // Island-wide colour map (<island>-c.2db, 1024 x 1024) and the world
    // extent of the island ocean mesh that carries it (the retail world map
    // draws this texture, docs/STATE.md session 23).
    inline GPUTexture *getIslandColorMap() const { return pIslandColorMap; }
    inline const Vector3f &getIslandBoundsMin() const { return islandOcean.getBoundsMin(); }
    inline const Vector3f &getIslandBoundsMax() const { return islandOcean.getBoundsMax(); }
    // Sector bank path (<level>/area-x/area-x-y/sector-x-y-sx-sy.bnk).
    std::string getSectorBankPath( const SectorId &id ) const;
    static std::string GetSectorBankPath( const char *pIsland, const SectorId &id );
    inline const Bank &getCommonBank() const { return commonBank; }
    // One object of the shared library (commonworld.bnk library.3dd) by its
    // hashcode: GetIdentifier64bit of the 8 character name the data uses
    // ("FU_AIR_B" / "FU_AIR_C", the ObjectInCommonRef of the aircrafts.bnk
    // Entity__Actor__Aircraft records, docs/re/aircraft_birds_spec.md). -1 when
    // the island's library has no such node.
    int32_t findLibraryNode( uint64_t hashcode ) const;
    // Model space bounds of a library node; false when it draws nothing.
    bool getLibraryNodeBounds( int32_t nodeIndex, Vector3f &outMin, Vector3f &outMax ) const;
    // Draws one library node at 'world': the call the .pmi V / O lists make per
    // instance (the model is shared, nothing is uploaded).
    void drawLibraryNode( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world ) const;

    // Hides the streamed instances (vegetation, props) within radius metres (flat) of a
    // world point: cutscenes whose set the world clutters (radius 0 = off).
    void setInstanceHide( const Vector3f &center, float radius ) {
        instanceHideCenter = center;
        instanceHideRadius = radius;
    }

private:
    struct Sector;
    // Marks the heightfield cells whose height lies inside a tunnel / bridge
    // node's volume (see eden_world.cpp): their sheet crosses the object's
    // shell and fills the tunnel mouth with terrain (docs/STATE.md session
    // 42; the session-56 island scan). Runs at sector load, -nocarve skips it.
    static void MarkCarvedCells( Sector &sector );
    // Indexes the up-facing triangles of the tunnel / bridge nodes (the
    // cells' carvers) into 8 m XZ buckets: the ground getGroundHeight hands
    // the drive physics where the sheet is carved away. Built once at sector
    // load, only when the sector actually has carved cells.
    static void BuildCarveGround( Sector &sector );
    // Topmost up-facing carve-ground surface below fromY at (x, z) (sector
    // local + Origin).
    static bool SampleCarveGround( const Sector &sector, float x, float z, float fromY, float &outHeight );
    // Unmarks the carved cells with no up-facing tunnel / bridge surface over
    // them: those are holes (the sheet is drawn without depth write over
    // nothing and the drive height function has no answer there - the owner's
    // "i thought it was transparent i fell through ... near tunnels/bridges").
    // They keep their opaque terrain instead. Runs after BuildCarveGround.
    static void PruneUncoveredCarveCells( Sector &sector );
    // Splits the sector's heightfield grid into the carved cells (inside a
    // tunnel / bridge volume, drawn without depth write) and the rest
    // (opaque with depth write, session 22's see-through fix). Runs once per
    // loaded sector, after MarkCarvedCells.
    void updateCarveBuffers( Sector &sector );
    // Frees the split / carved index buffers (unloadFar, the survey).
    static void destroyCarveBuffers( Sector &sector );

    struct Heightfield {
        EdenMesh Mesh;
        uint32_t MaterialIndex = 0;
        std::vector<EdenMaterial> Materials;
        GPUBuffer *pBuffer = nullptr;
        std::vector<float> Heights;
        float Spacing = 3.0f;
        // The cells whose height crosses a tunnel / bridge node's volume
        // (MarkCarvedCells): drawn first without depth write (see draw), and
        // refused by getHeight so the drive physics rides the tunnel's road
        // triangles instead of the sheet; the rest of the grid through
        // pSplitIndices as before.
        std::vector<uint8_t> Carved;
        GPUBuffer *pSplitIndices = nullptr;
        GPUBuffer *pCarvedIndices = nullptr;
        uint32_t NumSplitIndices = 0;
        uint32_t NumCarvedIndices = 0;
        bool bCarveIndicesDirty = true;
        bool bCarveMaterialDirty = true;
    };

    struct Sector {
        SectorId Id;
        Vector3f Origin;
        std::string BankPath;
        bool bPreloaded = false; // loaded ahead by preloadOne, not drawn yet
        Bank SectorBank;
        Heightfield Terrain;
        EdenModel Objects;
        // The up-facing triangles of the tunnel / bridge nodes (BuildCarveGround):
        // soup indices into Objects' collision copy (three per triangle) bucketed
        // by 8 m XZ cell (sector local, CellKey) - the ground the drive physics
        // stands on where the heightfield is carved away (getGroundHeight).
        std::vector<uint32_t> CarveTriangles;
        std::unordered_map<uint64_t, std::vector<uint32_t>> CarveCells;
        // .pmi lists (V / O static library models, A actors).
        std::vector<PmiInstance> Instances;
        std::vector<int32_t> InstanceModels;   // library node or actor model index
        std::vector<float> InstanceDistances;  // max draw distance
        // The .pgr grass grid payload, kept compressed (docs/re/grass_spec.md
        // 2): the rows decode on the fly for the cells around the camera.
        std::vector<uint8_t> Pgr;
        // Draw params of the TD_Ground library instances (night_lighting_spec
        // 2.1: sectrOff = the sector origin, w = 768, and for the O / S lists
        // the -s map on the SHADOW slot with a mirror sampler). Indexed by
        // list kind: 0 = V, 1 = O, 2 = S.
        EdenDrawParams GroundInstanceParams[3];
        // Neons of the sector objects (one model instance [INF]).
        std::vector<EdenMaterial *> NeonMaterials;
        std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> NeonBlinks;
        // AnimValue states of the .pmi instances (one per instance and
        // blinking material): instance i owns
        // InstanceBlinks[InstanceBlinkStart[i] .. InstanceBlinkStart[i + 1]).
        std::vector<uint32_t> InstanceBlinkStart;
        std::vector<std::pair<const EdenMaterial *, EdenNeons::Blink>> InstanceBlinks;
        // OpenTDU2 shore swell: horizontal beach-gradient vector of the
        // sector's TD_backwash foam strip (metres of travel per metre the sea
        // rises, directed uphill); zero when the sector has no foam.
        Vector3f FoamTideVector;    };

    struct Area {
        int32_t X = 0, Y = 0;
        Vector3f Origin;
        std::string BankPath;
        Bank AreaBank;
        Heightfield Terrain;
        EdenModel Objects;
        GPUBuffer *pIndexBuffer = nullptr; // quads not covered by loaded sectors
        uint32_t NumIndices = 0;
        bool bIndicesDirty = true;
        std::vector<EdenMaterial *> NeonMaterials;
        std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> NeonBlinks;
        // Area objects (-o: the 768 m ocean tile of each sector cell plus the
        // far objects) are grouped per sector by the dummy nodes
        // "Sector-ax-ay-sx-sy" of area-x-y-o.3dd [DATA]; ItemSectors[i] is the
        // sector of item i (AreaX = INT32_MIN: no group). The group is hidden
        // while that sector is loaded (finest level wins), which draws the
        // area tile exactly where no sector tile covers the sea.
        std::vector<SectorId> ItemSectors;    };

    EdenRenderer *pRenderer;
    std::string islandFolder;
    int32_t areaColumns;
    int32_t areaRows;
    Bank commonBank;
    Render2DM libraryMaterials;
    GPUBuffer *pGridBuffer;   // shared heightfield texcoords (x, z)
    GPUBuffer *pGridIndices;  // shared heightfield triangles
    uint32_t numGridIndices;
    std::vector<std::unique_ptr<Sector>> sectors;
    std::vector<std::unique_ptr<Area>> areas;
    std::unordered_set<uint32_t> failedSectors; // row * 1024 + column
    // commonworld.bnk library.3dd (models of the V / O .pmi lists).
    EdenModel libraryModel;
    std::unordered_map<uint64_t, int32_t> libraryNodes;
    // Library nodes whose LOD-0 items draw a TD_Ground material (they get the
    // sector -s map + sectrOff at draw, night_lighting_spec 2.1).
    std::unordered_map<int32_t, bool> libraryNodeGround;
    // Neon set materials of the shared models (library, actors): written in
    // place (the parameter is the same for every instance).
    std::vector<EdenMaterial *> sharedNeonMaterials;
    // Blinking materials drawn by a library node / an actor model (cache).
    std::unordered_map<int32_t, std::vector<const EdenMaterial *>> libraryNodeBlinks;
    std::unordered_map<int32_t, std::vector<const EdenMaterial *>> actorModelBlinks;
    // scenarics/packs/worldactors.bnk (actors of the A .pmi lists: road signs).
    Bank actorBank;
    std::unordered_map<uint64_t, std::string> actorNames;           // __Hc -> __Name
    std::unordered_map<uint64_t, const BankEntry *> actorScenes;    // root node hash -> 3dd
    std::unordered_map<uint64_t, int32_t> actorModelIndices;        // __Hc -> actorModels index (-1: none)
    std::vector<std::unique_ptr<EdenModel>> actorModels;
    // Direction-sign models (rs_dir[lr]_<variant>): the material slot that
    // carries the shared rs_maps face atlas (swap per instance at draw,
    // sign_text.cpp), the model's plate variant letter and its plate text
    // zones (where the composed text lands in the atlas).
    struct ActorSignInfo {
        int32_t MaterialIndex = -1;
        int32_t SlotIndex = -1;
        char Variant = 0;
        SignLayout Layout;
    };
    std::vector<ActorSignInfo> actorSignInfo; // parallel to actorModels
    // Composed direction-sign faces (payload x variant), freed with the world.
    SignFaceCache signFaces;
    // Island ocean (<island>.bnk <island>-o: AREA-x-y tiles + BORDER-* to the
    // horizon) and its colour map.
    Bank islandBank;
    EdenModel islandOcean;
    Heightfield islandTerrain;
    GPUBuffer *pIslandIndexBuffer = nullptr; // quads not covered by loaded areas
    uint32_t numIslandIndices = 0;
    bool bIslandIndicesDirty = true;
    bool bIslandTerrainEnabled = false;
    bool bAreasEnabled = true;
    void updateIslandIndices();
    bool loadWorldConfig( const char *pIsland );
    GPUTexture *pIslandColorMap = nullptr;
    bool bOceanVisibilityDirty = true;
    uint32_t drawEpoch = 0; // bumped when the drawn sector set changes (getDrawEpoch)
    bool bOceanVisible = true;
    float seaLevelOffset = 0.0f; // OpenTDU2 shore swell (setSeaLevelOffset)
    void updateOceanVisibility();
    // fx/weatherfx.bnk: RIPPLE_01..16, PUDDLES.
    Bank weatherFxBank;
    GPUTexture *pRipples[16] = {};
    GPUTexture *pPuddles = nullptr;
    float minInstanceRatio = 0.0f;
    bool bNightLights = false; // the street lamp glow gate (setNightLights)
    float rippleScroll = 0.0f;
    float rippleTimer = 0.0f;
    uint32_t rippleFrame = 0;

private:
    bool loadHeightfield( const Bank &bank, const char *pBaseName, const char *pTextureOwner, Heightfield &out );
    bool loadSector( const SectorId &id );
    bool loadArea( int32_t x, int32_t y );
    void updateAreaIndices( Area &area );
    void markAreasDirty( const SectorId &id );
    bool isSectorLoaded( const SectorId &id ) const;
    bool isSectorShown( const SectorId &id ) const; // loaded and not hidden ahead (bPreloaded)
    void loadInstances( Sector &sector, const std::string &name );
    // LOD-0 items of the library node draw a TD_Ground material (cached).
    bool libraryNodeHasGround( int32_t node );
    // Blinking neon materials drawn by a library node / an actor model (cache).
    const std::vector<const EdenMaterial *> &getInstanceBlinkMaterials( uint8_t type, int32_t model );
    static void RegisterNeons( EdenModel &model, uint32_t key, std::vector<EdenMaterial *> &outSet,
                               std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> &outBlinks );
    int32_t getActorModel( uint64_t hashcode );
    void drawInstances( EdenRenderer &renderer, const Sector &sector, const Vector3f &cameraPosition );
    // TD_Impostor batches of the pass being drawn: one per (material, vertex
    // layout), every visible instance's quad transformed to world space on the
    // CPU (the impostor shader has no world matrix). Filled by drawInstances,
    // drawn once each at the end of draw(). -noimpostors skips them (the old
    // per-item draw put every card near the world origin, unseen).
    struct ImpostorBatch {
        const EdenMaterial *pMaterial = nullptr;
        const EdenMesh *pLayout = nullptr; // strides / attributes / topology of the item
        std::vector<uint8_t> Streams[2];
        std::vector<uint32_t> Indices;
        uint32_t NumVertices = 0;
    };
    std::vector<ImpostorBatch> impostorBatches;
    size_t numImpostorBatches = 0;
    // Appends one instance of an impostor item; false when the item cannot be
    // batched (no CPU copy, a position format other than float3 / float4).
    bool addImpostor( const EdenMaterial &material, const EdenModel::Item &item, const Matrix4x4f &world,
                      const Vector3f &cameraPosition );
    void drawImpostors( EdenRenderer &renderer );
    // The impostors' light: TD_Impostor's only colour input is
    // __AmbLight_Ambient_0 + __AmbLight_Ambient_1 (vs_0: oD0 = c60 + c61
    // [SHADER]). Retail keeps an ambient-light object at renderer +0x45bc
    // (vtable 0xf2c310, constructed at 0xd82170) whose colour FUN_00d769e0
    // fills every frame from the weather's impostor_ambient (0xd769fb..0xd76a2e
    // [CODE]); the impostor batches draw with it. Set by EdenSceneView.
    Vector3f impostorAmbient = Vector3f( 0.5f, 0.5f, 0.5f );
    EdenDrawParams impostorParams;

public:
    void setImpostorAmbient( const Vector3f &color ) { impostorAmbient = color; }

private:
    // Tall grass (docs/re/grass_spec.md): the retail ground-cover layer —
    // fx/tallgrass.bnk (the tuft card template + the setgrass / setbush
    // atlases), fx/grasstypes.xml (the per-type size law) and the per-sector
    // .pgr grid (1024 x 1024 cells of 0.75 m, sparse (count, value) RLE).
    // The cards build from the grid cells around the camera every frame (the
    // impostor-batch shape: CPU-built world-space quads, transient upload,
    // one draw per material); the drive physics never sees them (getHeight /
    // getGroundHeight are untouched).
    struct GrassType {
        std::string Name;
        int32_t GrassBitmap = 0;
        int32_t GrassVariation = -1;
        float VariationProbability = 0.0f;
        float Height = 0.6f;
        float Width = 2.0f;
        float SizeDelta = 0.1f;
        bool bNoShadow = false;
    };
    bool loadGrassAssets( const char *pIsland );
    void drawGrass( EdenRenderer &renderer, const Vector3f &cameraPosition );
    std::vector<GrassType> grassTypes;
    Bank grassBank;
    EdenModel grassModel;               // the PLANE_01 / PLANE_02 card template
    EdenMesh grassLayout;               // the template's vertex layout
    const EdenMaterial *pGrassMaterial = nullptr;
    std::vector<uint8_t> pgrRowScratch; // per-frame row decode scratch
    bool bGrassLoaded = false;
    float grassDistance = 140.0f;
    uint32_t grassMaxCards = 9000;
    int32_t grassStride = 3;            // grid cells between cards (2.25 m)
    bool bGrassFlip = false;            // grid row order A/B (-grassflip)
    struct GrassBatch {
        std::vector<uint8_t> Streams[2];
        std::vector<uint32_t> Indices;
        uint32_t NumVertices = 0;
    };
    GrassBatch grassBatch;
    GrassBatch grassTallBatch;         // TD_TallGrass path: one 64-byte-corner stream
    EdenMesh grassTallLayout;          // position / color / texcoord / tangent, 4 x f32
    bool bGrassTallGrass = false;      // -grasstallgrass: the materials resolve TD_TallGrass

private:
    Vector3f instanceHideCenter;
    float instanceHideRadius = 0.0f;
    ModelCallback modelLoadedCallback;
    ModelCallback modelUnloadedCallback;
};
